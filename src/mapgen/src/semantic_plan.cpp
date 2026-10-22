#include "qmg/semantic_plan.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace qmg {
namespace {

constexpr std::size_t kMaximumPlanBytes = 65536;

class ParseFailure final : public std::runtime_error {
  public:
    ParseFailure(std::string code, std::string path, std::string message)
        : std::runtime_error(message), issue_{std::move(code), std::move(path), std::move(message)} {}

    [[nodiscard]] const PlanIssue &issue() const { return issue_; }

  private:
    PlanIssue issue_;
};

void append_utf8(std::string &output, const std::uint32_t codepoint) {
    if (codepoint <= 0x7FU) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
        output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

class JsonCursor {
  public:
    explicit JsonCursor(const std::string_view input) : input_(input) {}

    void whitespace() {
        while (position_ < input_.size()) {
            const char value = input_[position_];
            if (value != ' ' && value != '\t' && value != '\r' && value != '\n')
                return;
            ++position_;
        }
    }

    void expect(const char value, const std::string_view path) {
        whitespace();
        if (position_ >= input_.size() || input_[position_] != value)
            fail("json_syntax", path, std::string("expected '") + value + "'");
        ++position_;
    }

    [[nodiscard]] bool consume(const char value) {
        whitespace();
        if (position_ < input_.size() && input_[position_] == value) {
            ++position_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool done() {
        whitespace();
        return position_ == input_.size();
    }

    [[nodiscard]] std::string string(const std::string_view path) {
        whitespace();
        if (position_ >= input_.size() || input_[position_] != '"')
            fail("wrong_type", path, "expected a JSON string");
        ++position_;
        std::string result;
        while (position_ < input_.size()) {
            const auto character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') {
                if (!is_valid_utf8(result))
                    fail("invalid_utf8", path, "string contains invalid UTF-8");
                return result;
            }
            if (character < 0x20U)
                fail("json_syntax", path, "string contains an unescaped control character");
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= input_.size())
                fail("json_syntax", path, "unterminated string escape");
            const char escape = input_[position_++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                result.push_back(escape);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                std::uint32_t codepoint = hex_quad(path);
                if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\' || input_[position_ + 1] != 'u')
                        fail("json_syntax", path, "high surrogate is missing a low surrogate");
                    position_ += 2;
                    const std::uint32_t low = hex_quad(path);
                    if (low < 0xDC00U || low > 0xDFFFU)
                        fail("json_syntax", path, "invalid low surrogate");
                    codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
                } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                    fail("json_syntax", path, "unexpected low surrogate");
                }
                append_utf8(result, codepoint);
                break;
            }
            default:
                fail("json_syntax", path, "invalid string escape");
            }
        }
        fail("json_syntax", path, "unterminated JSON string");
    }

    [[nodiscard]] int integer(const std::string_view path) {
        whitespace();
        const std::size_t start = position_;
        if (position_ < input_.size() && input_[position_] == '-')
            ++position_;
        if (position_ >= input_.size() || input_[position_] < '0' || input_[position_] > '9')
            fail("wrong_type", path, "expected an integer");
        if (input_[position_] == '0') {
            ++position_;
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                fail("json_syntax", path, "integer has a leading zero");
        } else {
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                ++position_;
        }
        if (position_ < input_.size() &&
            (input_[position_] == '.' || input_[position_] == 'e' || input_[position_] == 'E'))
            fail("wrong_type", path, "expected an integer, not a floating-point number");
        int result{};
        const auto [end, error] = std::from_chars(input_.data() + start, input_.data() + position_, result);
        if (error != std::errc{} || end != input_.data() + position_)
            fail("integer_range", path, "integer is outside the supported range");
        return result;
    }

    [[noreturn]] void fail(const std::string_view code, const std::string_view path,
                           const std::string_view message) const {
        throw ParseFailure(std::string(code), std::string(path),
                           std::string(message) + " at byte " + std::to_string(position_));
    }

  private:
    [[nodiscard]] std::uint32_t hex_quad(const std::string_view path) {
        if (position_ + 4 > input_.size())
            fail("json_syntax", path, "incomplete Unicode escape");
        std::uint32_t value{};
        for (int index = 0; index < 4; ++index) {
            const char character = input_[position_++];
            value <<= 4U;
            if (character >= '0' && character <= '9')
                value |= static_cast<std::uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f')
                value |= static_cast<std::uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F')
                value |= static_cast<std::uint32_t>(character - 'A' + 10);
            else
                fail("json_syntax", path, "invalid Unicode escape");
        }
        return value;
    }

    std::string_view input_;
    std::size_t position_{};
};

template <typename Value, std::size_t Count>
Value parse_enum(const std::string &text, const std::array<std::pair<std::string_view, Value>, Count> &values,
                 JsonCursor &cursor, const std::string_view path) {
    const auto found =
        std::find_if(values.begin(), values.end(), [&text](const auto &entry) { return entry.first == text; });
    if (found == values.end())
        cursor.fail("invalid_enum", path, "value is not in the allowed enum");
    return found->second;
}

constexpr std::array route_styles{
    std::pair{std::string_view{"linear"}, RouteStyle::linear},
    std::pair{std::string_view{"loop"}, RouteStyle::loop},
};
constexpr std::array symmetries{
    std::pair{std::string_view{"none"}, Symmetry::none},
    std::pair{std::string_view{"soft"}, Symmetry::soft},
    std::pair{std::string_view{"strict"}, Symmetry::strict},
};
constexpr std::array lighting_styles{
    std::pair{std::string_view{"low_key"}, LightingStyle::low_key},
    std::pair{std::string_view{"balanced"}, LightingStyle::balanced},
    std::pair{std::string_view{"high_contrast"}, LightingStyle::high_contrast},
};
constexpr std::array room_roles{
    std::pair{std::string_view{"start"}, RoomRole::start},
    std::pair{std::string_view{"exit"}, RoomRole::exit},
    std::pair{std::string_view{"traversal"}, RoomRole::traversal},
    std::pair{std::string_view{"combat"}, RoomRole::combat},
    std::pair{std::string_view{"resource"}, RoomRole::resource},
    std::pair{std::string_view{"setpiece"}, RoomRole::setpiece},
    std::pair{std::string_view{"spawn"}, RoomRole::spawn},
    std::pair{std::string_view{"connector"}, RoomRole::connector},
    std::pair{std::string_view{"control"}, RoomRole::control},
    std::pair{std::string_view{"vertical"}, RoomRole::vertical},
    std::pair{std::string_view{"objective"}, RoomRole::objective},
    std::pair{std::string_view{"arena"}, RoomRole::arena},
};

std::string quote_json(const std::string_view value) {
    std::string result{"\""};
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (character < 0x20U) {
                constexpr char hex[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back(hex[(character >> 4U) & 0x0FU]);
                result.push_back(hex[character & 0x0FU]);
            } else {
                result.push_back(static_cast<char>(character));
            }
        }
    }
    result.push_back('"');
    return result;
}

void add_issue(std::vector<PlanIssue> &issues, const std::string_view code, const std::string_view path,
               const std::string_view message) {
    issues.push_back({std::string(code), std::string(path), std::string(message)});
}

bool native_single_player(const PromptSpec &request) {
    return request.mode == GameMode::single_player && request.game != Game::quake3;
}

bool loop_feasible(const PromptSpec &request) {
    return native_single_player(request) ? request.room_count >= 5 : request.room_count >= 4;
}

void require_normalized_request(const PromptSpec &request) {
    require_supported_layout_intent(request);
    const bool valid_game =
        request.game == Game::quake1 || request.game == Game::quake2 || request.game == Game::quake3;
    const bool valid_mode = request.mode == GameMode::single_player || request.mode == GameMode::deathmatch ||
                            request.mode == GameMode::duel || request.mode == GameMode::capture_the_flag;
    if (!valid_game || !valid_mode)
        throw std::invalid_argument("semantic planning requires a valid normalized game and mode");
    if (request.room_count < 2 || request.room_count > 32)
        throw std::invalid_argument("semantic planning requires a normalized room count from 2 to 32");
    if (request.target_players < 2 || request.target_players > 32)
        throw std::invalid_argument("semantic planning requires a normalized player count from 2 to 32");
    if (!std::isfinite(request.verticality) || request.verticality < 0.0 || request.verticality > 1.0 ||
        !std::isfinite(request.intensity) || request.intensity < 0.0 || request.intensity > 1.0)
        throw std::invalid_argument("semantic planning requires normalized intensity and verticality from 0 to 1");
    if (request.source_prompt.empty() || request.source_prompt.size() > 65536 ||
        !is_valid_utf8(request.source_prompt) ||
        std::any_of(request.source_prompt.begin(), request.source_prompt.end(), [](const unsigned char character) {
            return (character < 0x20U && character != '\t' && character != '\n' && character != '\r') ||
                   character == 0x7FU;
        }))
        throw std::invalid_argument("semantic planning requires a bounded, valid normalized source prompt");
    if (request.mode == GameMode::duel && (request.target_players != 2 || request.room_count < 4))
        throw std::invalid_argument("semantic planning requires normalized duel players and room count");
    if (request.mode == GameMode::capture_the_flag && request.target_players < 6)
        throw std::invalid_argument("semantic planning requires at least six normalized CTF players");
    if ((request.game == Game::quake3 || request.mode != GameMode::single_player) && !request.prefer_loops)
        throw std::invalid_argument("semantic planning requires normalized arena route-choice intent");
}

} // namespace

SemanticPlan make_rule_semantic_plan(const PromptSpec &request) {
    require_normalized_request(request);
    SemanticPlan plan;
    plan.route_style = request.prefer_loops && loop_feasible(request) ? RouteStyle::loop : RouteStyle::linear;
    plan.symmetry =
        request.mode == GameMode::capture_the_flag
            ? Symmetry::strict
            : (native_single_player(request) || request.layout_intent == LayoutIntent::wrapped_center ? Symmetry::none
                                                                                                      : Symmetry::soft);
    plan.lighting_style = LightingStyle::balanced;
    plan.verticality = std::clamp(static_cast<int>(std::lround(request.verticality * 100.0)), 0, 100);
    plan.intensity = std::clamp(static_cast<int>(std::lround(request.intensity * 100.0)), 0, 100);
    plan.room_roles.resize(static_cast<std::size_t>(request.room_count), RoomRole::traversal);

    if (native_single_player(request)) {
        plan.room_roles.front() = RoomRole::start;
        plan.room_roles.back() = RoomRole::exit;
        constexpr std::array middle_roles{RoomRole::combat, RoomRole::traversal};
        for (std::size_t index = 1; index + 1 < plan.room_roles.size(); ++index)
            plan.room_roles[index] = middle_roles[(index - 1) % middle_roles.size()];
    } else if (request.mode == GameMode::capture_the_flag) {
        plan.room_roles.front() = RoomRole::objective;
        plan.room_roles.back() = RoomRole::objective;
        constexpr std::array middle_roles{RoomRole::spawn, RoomRole::connector, RoomRole::vertical, RoomRole::arena};
        for (std::size_t index = 1; index + 1 < plan.room_roles.size() && index <= plan.room_roles.size() - 1 - index;
             ++index) {
            const RoomRole role = middle_roles[(index - 1) % middle_roles.size()];
            plan.room_roles[index] = role;
            plan.room_roles[plan.room_roles.size() - 1 - index] = role;
        }
    } else {
        constexpr std::array arena_roles{RoomRole::spawn, RoomRole::connector, RoomRole::vertical, RoomRole::arena};
        for (std::size_t index = 0; index < plan.room_roles.size(); ++index)
            plan.room_roles[index] = arena_roles[index % arena_roles.size()];
    }
    return plan;
}

SemanticPlanParseResult parse_semantic_plan(const std::string_view json) {
    SemanticPlanParseResult result;
    if (json.size() > kMaximumPlanBytes) {
        result.issues.push_back({"input_too_large", "/", "semantic plan exceeds the 65536-byte limit"});
        return result;
    }

    try {
        JsonCursor cursor(json);
        SemanticPlan plan;
        std::set<std::string> fields;
        cursor.expect('{', "/");
        if (cursor.consume('}'))
            cursor.fail("missing_field", "/", "semantic plan object is empty");
        while (true) {
            const std::string key = cursor.string("/");
            const std::string path = "/" + key;
            if (!fields.insert(key).second)
                cursor.fail("duplicate_field", path, "field appears more than once");
            cursor.expect(':', path);
            if (key == "schema") {
                const std::string schema = cursor.string(path);
                if (schema != kSemanticPlanSchemaId)
                    cursor.fail("schema_mismatch", path, "unsupported semantic plan schema");
            } else if (key == "schema_version") {
                plan.schema_version = cursor.integer(path);
            } else if (key == "route_style") {
                plan.route_style = parse_enum(cursor.string(path), route_styles, cursor, path);
            } else if (key == "symmetry") {
                plan.symmetry = parse_enum(cursor.string(path), symmetries, cursor, path);
            } else if (key == "lighting_style") {
                plan.lighting_style = parse_enum(cursor.string(path), lighting_styles, cursor, path);
            } else if (key == "verticality") {
                plan.verticality = cursor.integer(path);
            } else if (key == "intensity") {
                plan.intensity = cursor.integer(path);
            } else if (key == "room_roles") {
                cursor.expect('[', path);
                plan.room_roles.clear();
                if (!cursor.consume(']')) {
                    while (true) {
                        if (plan.room_roles.size() >= 32)
                            cursor.fail("array_limit", path, "room_roles cannot contain more than 32 entries");
                        const std::string item_path = path + "/" + std::to_string(plan.room_roles.size());
                        plan.room_roles.push_back(parse_enum(cursor.string(item_path), room_roles, cursor, item_path));
                        if (cursor.consume(']'))
                            break;
                        cursor.expect(',', path);
                    }
                }
            } else {
                cursor.fail("unknown_field", path, "field is not allowed by semantic-plan-v1");
            }

            if (cursor.consume('}'))
                break;
            cursor.expect(',', "/");
        }
        if (!cursor.done())
            cursor.fail("trailing_data", "/", "unexpected data follows the semantic plan");

        constexpr std::array required{std::string_view{"schema"},         std::string_view{"schema_version"},
                                      std::string_view{"route_style"},    std::string_view{"symmetry"},
                                      std::string_view{"lighting_style"}, std::string_view{"verticality"},
                                      std::string_view{"intensity"},      std::string_view{"room_roles"}};
        for (const std::string_view field : required) {
            if (!fields.contains(std::string(field)))
                cursor.fail("missing_field", "/" + std::string(field), "required field is missing");
        }
        if (plan.schema_version != kSemanticPlanSchemaVersion)
            cursor.fail("schema_version", "/schema_version", "schema_version must be 1");
        if (plan.verticality < 0 || plan.verticality > 100)
            cursor.fail("range", "/verticality", "verticality must be from 0 to 100");
        if (plan.intensity < 0 || plan.intensity > 100)
            cursor.fail("range", "/intensity", "intensity must be from 0 to 100");
        if (plan.room_roles.size() < 2)
            cursor.fail("array_limit", "/room_roles", "room_roles must contain at least two entries");
        result.plan = std::move(plan);
    } catch (const ParseFailure &failure) {
        result.issues.push_back(failure.issue());
    }
    return result;
}

std::vector<PlanIssue> validate_semantic_plan(const SemanticPlan &plan, const PromptSpec &request) {
    require_normalized_request(request);
    std::vector<PlanIssue> issues;
    if (plan.route_style != RouteStyle::linear && plan.route_style != RouteStyle::loop)
        add_issue(issues, "invalid_enum", "/route_style", "route_style has an invalid enum value");
    if (plan.symmetry != Symmetry::none && plan.symmetry != Symmetry::soft && plan.symmetry != Symmetry::strict)
        add_issue(issues, "invalid_enum", "/symmetry", "symmetry has an invalid enum value");
    if (request.layout_intent == LayoutIntent::wrapped_center && plan.symmetry != Symmetry::none)
        add_issue(issues, "wrapped_symmetry", "/symmetry", "wrapped_center construction requires symmetry none");
    if (plan.lighting_style != LightingStyle::low_key && plan.lighting_style != LightingStyle::balanced &&
        plan.lighting_style != LightingStyle::high_contrast)
        add_issue(issues, "invalid_enum", "/lighting_style", "lighting_style has an invalid enum value");
    for (std::size_t index = 0; index < plan.room_roles.size(); ++index) {
        const RoomRole role = plan.room_roles[index];
        const bool valid_role = std::any_of(room_roles.begin(), room_roles.end(),
                                            [role](const auto &entry) { return entry.second == role; });
        if (!valid_role)
            add_issue(issues, "invalid_enum", "/room_roles/" + std::to_string(index),
                      "room role has an invalid enum value");
    }
    if (plan.schema_version != kSemanticPlanSchemaVersion)
        add_issue(issues, "schema_version", "/schema_version", "schema_version must be 1");
    if (plan.verticality < 0 || plan.verticality > 100)
        add_issue(issues, "range", "/verticality", "verticality must be from 0 to 100");
    if (plan.intensity < 0 || plan.intensity > 100)
        add_issue(issues, "range", "/intensity", "intensity must be from 0 to 100");
    const int requested_verticality = static_cast<int>(std::lround(request.verticality * 100.0));
    const int requested_intensity = static_cast<int>(std::lround(request.intensity * 100.0));
    const auto verticality_drift = std::int64_t{plan.verticality} - std::int64_t{requested_verticality};
    const auto intensity_drift = std::int64_t{plan.intensity} - std::int64_t{requested_intensity};
    if (std::abs(verticality_drift) > 25)
        add_issue(issues, "request_drift", "/verticality",
                  "verticality differs from the normalized request by more than 25");
    if (std::abs(intensity_drift) > 25)
        add_issue(issues, "request_drift", "/intensity",
                  "intensity differs from the normalized request by more than 25");
    if (plan.room_roles.size() != static_cast<std::size_t>(request.room_count))
        add_issue(issues, "room_count", "/room_roles", "room_roles must contain exactly the requested room count");
    if (request.prefer_loops && loop_feasible(request) && plan.route_style != RouteStyle::loop)
        add_issue(issues, "route_contract", "/route_style", "the normalized request requires a loop route");
    if (!loop_feasible(request) && plan.route_style != RouteStyle::linear)
        add_issue(issues, "route_contract", "/route_style",
                  "the current layout profile cannot realize a cycle at the requested room count");

    const auto count_role = [&plan](const RoomRole role) {
        return static_cast<int>(std::count(plan.room_roles.begin(), plan.room_roles.end(), role));
    };
    if (native_single_player(request)) {
        if (plan.room_roles.empty() || plan.room_roles.front() != RoomRole::start)
            add_issue(issues, "sp_start", "/room_roles/0", "native single-player plans must begin with start");
        if (plan.room_roles.empty() || plan.room_roles.back() != RoomRole::exit)
            add_issue(issues, "sp_exit", "/room_roles", "native single-player plans must end with exit");
        if (count_role(RoomRole::start) != 1 || count_role(RoomRole::exit) != 1)
            add_issue(issues, "sp_endpoints", "/room_roles",
                      "native single-player plans require one start and one exit");
        if (count_role(RoomRole::objective) != 0 || count_role(RoomRole::spawn) != 0 ||
            count_role(RoomRole::arena) != 0)
            add_issue(issues, "sp_role", "/room_roles", "native single-player plans cannot contain arena roles");
        if (request.room_count > 2 && count_role(RoomRole::combat) + count_role(RoomRole::setpiece) == 0)
            add_issue(issues, "sp_encounter", "/room_roles", "native single-player plans require a combat beat");
        if (plan.symmetry == Symmetry::strict)
            add_issue(issues, "sp_symmetry", "/symmetry", "strict symmetry is not supported for native single-player");
    } else {
        const int campaign_roles = count_role(RoomRole::start) + count_role(RoomRole::exit) +
                                   count_role(RoomRole::combat) + count_role(RoomRole::setpiece);
        if (campaign_roles != 0)
            add_issue(issues, "arena_role", "/room_roles", "arena plans cannot contain campaign-only roles");
        if (count_role(RoomRole::spawn) == 0 &&
            !(request.mode == GameMode::capture_the_flag && request.room_count == 2))
            add_issue(issues, "arena_spawn", "/room_roles", "arena plans require at least one spawn role");
        if (request.mode == GameMode::capture_the_flag) {
            if (count_role(RoomRole::objective) != 2)
                add_issue(issues, "ctf_objectives", "/room_roles", "CTF plans require exactly two objective roles");
            if (plan.room_roles.empty() || plan.room_roles.front() != RoomRole::objective ||
                plan.room_roles.back() != RoomRole::objective)
                add_issue(issues, "ctf_endpoints", "/room_roles",
                          "CTF objectives must occupy the first and last rooms");
            if (plan.symmetry != Symmetry::strict)
                add_issue(issues, "ctf_symmetry", "/symmetry", "CTF plans require strict semantic symmetry");
            for (std::size_t index = 1; index + 1 < plan.room_roles.size(); ++index) {
                const std::size_t counterpart = plan.room_roles.size() - 1 - index;
                if (plan.room_roles[index] != plan.room_roles[counterpart]) {
                    add_issue(issues, "ctf_role_symmetry", "/room_roles/" + std::to_string(index),
                              "CTF room roles must mirror around the center");
                    break;
                }
            }
        } else if (count_role(RoomRole::objective) != 0) {
            add_issue(issues, "arena_objective", "/room_roles", "non-CTF arena plans cannot contain objectives");
        }
        if (request.mode != GameMode::capture_the_flag && plan.symmetry == Symmetry::strict)
            add_issue(issues, "arena_symmetry", "/symmetry", "strict symmetry is reserved for CTF paired roles in v1");
    }
    return issues;
}

std::string render_semantic_plan(const SemanticPlan &plan, const bool pretty) {
    const std::string newline = pretty ? "\n" : "";
    const std::string indent = pretty ? "  " : "";
    const std::string separator = pretty ? ": " : ":";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << '{' << newline;
    output << indent << "\"schema\"" << separator << quote_json(kSemanticPlanSchemaId) << ',' << newline;
    output << indent << "\"schema_version\"" << separator << plan.schema_version << ',' << newline;
    output << indent << "\"route_style\"" << separator << quote_json(to_string(plan.route_style)) << ',' << newline;
    output << indent << "\"symmetry\"" << separator << quote_json(to_string(plan.symmetry)) << ',' << newline;
    output << indent << "\"lighting_style\"" << separator << quote_json(to_string(plan.lighting_style)) << ','
           << newline;
    output << indent << "\"verticality\"" << separator << plan.verticality << ',' << newline;
    output << indent << "\"intensity\"" << separator << plan.intensity << ',' << newline;
    output << indent << "\"room_roles\"" << separator << '[';
    for (std::size_t index = 0; index < plan.room_roles.size(); ++index) {
        if (index > 0)
            output << (pretty ? ", " : ",");
        output << quote_json(to_string(plan.room_roles[index]));
    }
    output << ']' << newline << '}' << newline;
    return output.str();
}

namespace {

// The grammar rule each room position takes; build_semantic_plan_json_schema reads the same rules.
std::vector<std::string_view> grammar_role_rules(const PromptSpec &request) {
    const std::size_t role_count = static_cast<std::size_t>(request.room_count);
    std::vector<std::string_view> role_rules(role_count, "arena-role");
    if (native_single_player(request)) {
        role_rules.front() = "start-role";
        role_rules.back() = "exit-role";
        if (role_count > 2) {
            role_rules[1] = "encounter-role";
            for (std::size_t index = 2; index + 1 < role_count; ++index)
                role_rules[index] = "sp-role";
        }
    } else if (request.mode == GameMode::capture_the_flag) {
        role_rules.front() = "objective-role";
        role_rules.back() = "objective-role";
        constexpr std::array paired_roles{std::string_view{"spawn-role"}, std::string_view{"connector-role"},
                                          std::string_view{"vertical-role"}, std::string_view{"arena-fixed-role"}};
        for (std::size_t index = 1; index + 1 < role_count && index <= role_count - 1 - index; ++index) {
            const std::string_view rule = paired_roles[(index - 1) % paired_roles.size()];
            role_rules[index] = rule;
            role_rules[role_count - 1 - index] = rule;
        }
    } else {
        role_rules.front() = "spawn-role";
    }
    return role_rules;
}

// The roles a grammar rule allows, as the grammar below spells them.
std::vector<std::string_view> grammar_rule_roles(const std::string_view rule) {
    if (rule == "start-role")
        return {"start"};
    if (rule == "exit-role")
        return {"exit"};
    if (rule == "encounter-role")
        return {"combat", "setpiece"};
    if (rule == "sp-role")
        return {"traversal", "combat", "resource", "setpiece"};
    if (rule == "spawn-role")
        return {"spawn"};
    if (rule == "objective-role")
        return {"objective"};
    if (rule == "connector-role")
        return {"connector"};
    if (rule == "vertical-role")
        return {"vertical"};
    if (rule == "arena-fixed-role")
        return {"arena"};
    if (rule == "arena-role")
        return {"traversal", "resource", "spawn", "connector", "control", "vertical", "arena"};
    throw std::logic_error("unknown semantic-plan grammar rule");
}

std::vector<std::string_view> grammar_route_styles(const PromptSpec &request) {
    if (!loop_feasible(request))
        return {"linear"};
    if (request.prefer_loops)
        return {"loop"};
    return {"linear", "loop"};
}

std::vector<std::string_view> grammar_symmetries(const PromptSpec &request) {
    if (request.mode == GameMode::capture_the_flag)
        return {"strict"};
    if (request.layout_intent == LayoutIntent::wrapped_center)
        return {"none"};
    return {"none", "soft"};
}

std::pair<int, int> grammar_percentage_window(const double requested) {
    const int center = std::clamp(static_cast<int>(std::lround(requested * 100.0)), 0, 100);
    return {std::max(0, center - 25), std::min(100, center + 25)};
}

} // namespace

std::string build_semantic_plan_grammar(const PromptSpec &request) {
    require_normalized_request(request);
    const std::size_t role_count = static_cast<std::size_t>(request.room_count);
    const std::vector<std::string_view> role_rules = grammar_role_rules(request);

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "root ::= \"{\" ws \"\\\"schema\\\"\" ws \":\" ws \"\\\"qmg.semantic_plan\\\"\" ws \",\" ws "
              "\"\\\"schema_version\\\"\" ws \":\" ws \"1\" ws \",\" ws "
              "\"\\\"route_style\\\"\" ws \":\" ws route-style ws \",\" ws "
              "\"\\\"symmetry\\\"\" ws \":\" ws symmetry ws \",\" ws "
              "\"\\\"lighting_style\\\"\" ws \":\" ws lighting-style ws \",\" ws "
              "\"\\\"verticality\\\"\" ws \":\" ws verticality ws \",\" ws "
              "\"\\\"intensity\\\"\" ws \":\" ws intensity ws \",\" ws "
              "\"\\\"room_roles\\\"\" ws \":\" ws \"[\" ws "
           << role_rules.front();
    for (std::size_t index = 1; index < role_count; ++index)
        output << " ws \",\" ws " << role_rules[index];
    output << " ws \"]\" ws \"}\" ws\n";
    if (!loop_feasible(request))
        output << "route-style ::= \"\\\"linear\\\"\"\n";
    else if (request.prefer_loops)
        output << "route-style ::= \"\\\"loop\\\"\"\n";
    else
        output << "route-style ::= \"\\\"linear\\\"\" | \"\\\"loop\\\"\"\n";
    if (request.mode == GameMode::capture_the_flag)
        output << "symmetry ::= \"\\\"strict\\\"\"\n";
    else if (request.layout_intent == LayoutIntent::wrapped_center)
        output << "symmetry ::= \"\\\"none\\\"\"\n";
    else
        output << "symmetry ::= \"\\\"none\\\"\" | \"\\\"soft\\\"\"\n";
    output << "lighting-style ::= \"\\\"low_key\\\"\" | \"\\\"balanced\\\"\" | \"\\\"high_contrast\\\"\"\n";
    const auto percentage_rule = [&output](const std::string_view name, const double requested) {
        const int center = std::clamp(static_cast<int>(std::lround(requested * 100.0)), 0, 100);
        const int minimum = std::max(0, center - 25);
        const int maximum = std::min(100, center + 25);
        output << name << " ::= ";
        for (int value = minimum; value <= maximum; ++value) {
            if (value > minimum)
                output << " | ";
            output << "\"" << value << "\"";
        }
        output << '\n';
    };
    percentage_rule("verticality", request.verticality);
    percentage_rule("intensity", request.intensity);
    output << "start-role ::= \"\\\"start\\\"\"\n"
              "exit-role ::= \"\\\"exit\\\"\"\n"
              "encounter-role ::= \"\\\"combat\\\"\" | \"\\\"setpiece\\\"\"\n"
              "sp-role ::= \"\\\"traversal\\\"\" | \"\\\"combat\\\"\" | \"\\\"resource\\\"\" | "
              "\"\\\"setpiece\\\"\"\n"
              "spawn-role ::= \"\\\"spawn\\\"\"\n"
              "objective-role ::= \"\\\"objective\\\"\"\n"
              "connector-role ::= \"\\\"connector\\\"\"\n"
              "vertical-role ::= \"\\\"vertical\\\"\"\n"
              "arena-fixed-role ::= \"\\\"arena\\\"\"\n"
              "arena-role ::= \"\\\"traversal\\\"\" | \"\\\"resource\\\"\" | \"\\\"spawn\\\"\" | "
              "\"\\\"connector\\\"\" | \"\\\"control\\\"\" | \"\\\"vertical\\\"\" | "
              "\"\\\"arena\\\"\"\n";
    output << "ws ::= [ \\t\\n\\r]{0,8}\n";
    return output.str();
}

std::string build_semantic_plan_json_schema(const PromptSpec &request) {
    require_normalized_request(request);
    const auto string_enum = [](const std::vector<std::string_view> &values) {
        std::string text = "{\"type\":\"string\",\"enum\":[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index > 0)
                text += ',';
            text += quote_json(values[index]);
        }
        return text + "]}";
    };
    const auto integer_window = [](const std::pair<int, int> window) {
        return "{\"type\":\"integer\",\"minimum\":" + std::to_string(window.first) +
               ",\"maximum\":" + std::to_string(window.second) + "}";
    };
    std::vector<std::string_view> roles;
    for (const std::string_view rule : grammar_role_rules(request)) {
        for (const std::string_view role : grammar_rule_roles(rule)) {
            if (std::find(roles.begin(), roles.end(), role) == roles.end())
                roles.push_back(role);
        }
    }
    // The room_roles enumeration order, so the schema text depends only on the request.
    std::vector<std::string_view> ordered_roles;
    for (const auto &[name, role] : room_roles) {
        static_cast<void>(role);
        if (std::find(roles.begin(), roles.end(), name) != roles.end())
            ordered_roles.push_back(name);
    }
    const std::string room_count = std::to_string(request.room_count);
    return "{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"schema\",\"schema_version\","
           "\"route_style\",\"symmetry\",\"lighting_style\",\"verticality\",\"intensity\",\"room_roles\"],"
           "\"properties\":{\"schema\":" +
           string_enum({kSemanticPlanSchemaId}) +
           ",\"schema_version\":{\"type\":\"integer\",\"enum\":[" + std::to_string(kSemanticPlanSchemaVersion) +
           "]},\"route_style\":" + string_enum(grammar_route_styles(request)) +
           ",\"symmetry\":" + string_enum(grammar_symmetries(request)) +
           ",\"lighting_style\":" + string_enum({"low_key", "balanced", "high_contrast"}) +
           ",\"verticality\":" + integer_window(grammar_percentage_window(request.verticality)) +
           ",\"intensity\":" + integer_window(grammar_percentage_window(request.intensity)) +
           ",\"room_roles\":{\"type\":\"array\",\"minItems\":" + room_count + ",\"maxItems\":" + room_count +
           ",\"items\":" + string_enum(ordered_roles) + "}}}";
}

std::string to_string(const RouteStyle value) {
    switch (value) {
    case RouteStyle::linear:
        return "linear";
    case RouteStyle::loop:
        return "loop";
    }
    throw std::invalid_argument("invalid RouteStyle enum value");
}

std::string to_string(const Symmetry value) {
    switch (value) {
    case Symmetry::none:
        return "none";
    case Symmetry::soft:
        return "soft";
    case Symmetry::strict:
        return "strict";
    }
    throw std::invalid_argument("invalid Symmetry enum value");
}

std::string to_string(const LightingStyle value) {
    switch (value) {
    case LightingStyle::low_key:
        return "low_key";
    case LightingStyle::balanced:
        return "balanced";
    case LightingStyle::high_contrast:
        return "high_contrast";
    }
    throw std::invalid_argument("invalid LightingStyle enum value");
}

std::string to_string(const RoomRole value) {
    for (const auto &[name, candidate] : room_roles) {
        if (candidate == value)
            return std::string(name);
    }
    throw std::invalid_argument("invalid RoomRole enum value");
}

} // namespace qmg
