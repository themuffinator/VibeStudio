#include "qmg/prompt.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace qmg {
namespace {

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

bool contains_any(const std::string &value, const std::vector<std::string_view> &needles) {
    return std::any_of(needles.begin(), needles.end(),
                       [&value](const std::string_view needle) { return value.find(needle) != std::string::npos; });
}

bool contains_word(const std::string &value, const std::string_view word) {
    std::size_t position = value.find(word);
    while (position != std::string::npos) {
        const bool left_boundary = position == 0 || !std::isalnum(static_cast<unsigned char>(value[position - 1]));
        const std::size_t end = position + word.size();
        const bool right_boundary = end == value.size() || !std::isalnum(static_cast<unsigned char>(value[end]));
        if (left_boundary && right_boundary)
            return true;
        position = value.find(word, position + 1);
    }
    return false;
}

bool ascii_space(const char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f' ||
           character == '\v';
}

std::string normalize_spaces(const std::string_view value) {
    std::string result;
    for (const char character : value) {
        if (ascii_space(character)) {
            if (!result.empty() && result.back() != ' ')
                result.push_back(' ');
        } else {
            result.push_back(character);
        }
    }
    return result;
}

LayoutIntent detect_layout_intent(const std::string &prompt) {
    const std::string text = normalize_spaces(prompt);
    constexpr std::string_view phrases[]{"wrapped layout",
                                         "wraparound layout",
                                         "wrap-around layout",
                                         "layout wrapped around a playable center",
                                         "layout wrapped around a playable centre",
                                         "layout wrapping around a playable center",
                                         "layout wrapping around a playable centre"};
    // This is a bounded phrase grammar, not general natural-language negation.
    // Reject explicit negative constraints rather than treating them as the
    // mixed-family default, which can itself produce a wrapped layout.
    constexpr std::string_view negative_prefixes[]{"no",           "not",        "without",       "avoid",
                                                   "never use",    "do not use", "do not create", "do not generate",
                                                   "do not build", "don't use",  "don't create",  "don't generate",
                                                   "don't build"};
    bool positive = false, negative = false;
    for (const auto phrase : phrases) {
        for (std::size_t start = text.find(phrase); start != std::string::npos; start = text.find(phrase, start + 1)) {
            const auto end = start + phrase.size();
            if ((start != 0 && std::isalnum(static_cast<unsigned char>(text[start - 1]))) ||
                (end != text.size() && std::isalnum(static_cast<unsigned char>(text[end]))))
                continue;
            auto prefix = std::string_view(text).substr(0, start);
            while (!prefix.empty() && prefix.back() == ' ')
                prefix.remove_suffix(1);
            bool negated = false;
            for (const auto head : negative_prefixes) {
                for (const std::string_view article : {"", " a", " the", " any"}) {
                    const std::string ending = std::string(head) + std::string(article);
                    if (prefix.ends_with(ending)) {
                        const auto boundary = prefix.size() - ending.size();
                        negated |= boundary == 0 || !std::isalnum(static_cast<unsigned char>(prefix[boundary - 1]));
                    }
                }
            }
            negative |= negated;
            positive |= !negated;
        }
    }
    if (positive && negative)
        throw std::invalid_argument("conflicting positive and negative wrapped layout requests");
    if (negative)
        throw std::invalid_argument(
            "negative wrapped layout constraints are not supported; omit the constraint or request a wrapped layout");
    if (positive && (contains_word(text, "strict symmetry") || contains_word(text, "strictly symmetric") ||
                     contains_word(text, "strictly symmetrical")))
        throw std::invalid_argument("wrapped layout intent conflicts with strict symmetry");
    return positive ? LayoutIntent::wrapped_center : LayoutIntent::unspecified;
}

std::optional<int> extract_integer_before(const std::string &value,
                                          const std::initializer_list<std::string_view> suffixes) {
    for (std::size_t position = 0; position < value.size();) {
        if (value[position] < '0' || value[position] > '9') {
            ++position;
            continue;
        }
        const std::size_t start = position;
        while (position < value.size() && value[position] >= '0' && value[position] <= '9')
            ++position;
        const std::size_t end = position;
        while (position < value.size() && ascii_space(value[position]))
            ++position;
        if (position < value.size() && value[position] == '-') {
            ++position;
            while (position < value.size() && ascii_space(value[position]))
                ++position;
        }
        const bool matched = std::any_of(suffixes.begin(), suffixes.end(), [&](const std::string_view suffix) {
            return value.compare(position, suffix.size(), suffix) == 0;
        });
        if (!matched)
            continue;

        std::uint64_t parsed{};
        const auto [parsed_end, error] = std::from_chars(value.data() + start, value.data() + end, parsed);
        if (error == std::errc::result_out_of_range)
            return std::numeric_limits<int>::max();
        if (error != std::errc{} || parsed_end != value.data() + end)
            return std::nullopt;
        return static_cast<int>(std::min<std::uint64_t>(parsed, std::numeric_limits<int>::max()));
    }
    return std::nullopt;
}

std::optional<std::uint64_t> extract_seed(const std::string &value) {
    for (std::size_t search = 0;;) {
        const std::size_t marker = value.find("seed", search);
        if (marker == std::string::npos)
            return std::nullopt;
        std::size_t position = marker + 4;
        while (position < value.size() && ascii_space(value[position]))
            ++position;
        if (position < value.size() && (value[position] == ':' || value[position] == '='))
            ++position;
        while (position < value.size() && ascii_space(value[position]))
            ++position;
        const std::size_t start = position;
        while (position < value.size() && value[position] >= '0' && value[position] <= '9')
            ++position;
        if (position == start) {
            search = marker + 4;
            continue;
        }
        std::uint64_t parsed{};
        const auto [end, error] = std::from_chars(value.data() + start, value.data() + position, parsed);
        if (error == std::errc::result_out_of_range)
            throw std::invalid_argument("the prompt seed exceeds the unsigned 64-bit range");
        if (error == std::errc{} && end == value.data() + position)
            return parsed;
        search = marker + 4;
    }
}

std::string detect_theme(const std::string &prompt) {
    const std::vector<std::pair<std::string_view, std::string_view>> themes{
        {"gothic", "gothic"},    {"medieval", "medieval"},     {"castle", "medieval"},      {"tech", "techbase"},
        {"base", "techbase"},    {"industrial", "industrial"}, {"factory", "industrial"},   {"sewer", "sewer"},
        {"water", "waterworks"}, {"lava", "lava fortress"},    {"slime", "toxic facility"}, {"space", "space"},
        {"ruin", "ruins"},       {"temple", "temple"},
    };
    for (const auto &[needle, theme] : themes) {
        if (prompt.find(needle) != std::string::npos) {
            return std::string(theme);
        }
    }
    return "industrial";
}

} // namespace

std::optional<Game> parse_game(const std::string_view value) {
    const std::string normalized = lowercase(value);
    if (contains_any(normalized, {"quake 3", "quake iii", "quake3", "q3a", "q3"})) {
        return Game::quake3;
    }
    if (contains_any(normalized, {"quake 2", "quake ii", "quake2", "q2"})) {
        return Game::quake2;
    }
    if (contains_any(normalized, {"quake 1", "quake i", "quake1", "q1", "quake"})) {
        return Game::quake1;
    }
    return std::nullopt;
}

std::optional<GameMode> parse_mode(const std::string_view value) {
    const std::string normalized = lowercase(value);
    if (contains_word(normalized, "ctf") || contains_any(normalized, {"capture the flag", "capture-the-flag"})) {
        return GameMode::capture_the_flag;
    }
    if (contains_any(normalized, {"duel", "1v1", "one versus one"})) {
        return GameMode::duel;
    }
    if (contains_word(normalized, "dm") || contains_word(normalized, "mp") ||
        contains_any(normalized, {"deathmatch", "free for all", "free-for-all", "multiplayer"})) {
        return GameMode::deathmatch;
    }
    if (contains_word(normalized, "sp") ||
        contains_any(normalized, {"single-player", "single player", "singleplayer", "campaign"})) {
        return GameMode::single_player;
    }
    return std::nullopt;
}

PromptSpec PromptParser::parse(const std::string_view prompt, const PromptOverrides &overrides) const {
    if (prompt.empty()) {
        throw std::invalid_argument("the generation prompt must not be empty");
    }
    if (prompt.size() > 65536)
        throw std::invalid_argument("the generation prompt cannot exceed 65536 bytes");
    if (!is_valid_utf8(prompt))
        throw std::invalid_argument("the generation prompt must be valid UTF-8");
    if (std::any_of(prompt.begin(), prompt.end(), [](const unsigned char character) {
            return (character < 0x20U && character != '\t' && character != '\n' && character != '\r') ||
                   character == 0x7FU;
        }))
        throw std::invalid_argument("the generation prompt contains an unsupported control character");

    PromptSpec spec;
    spec.source_prompt = std::string(prompt);
    const std::string normalized = lowercase(prompt);
    spec.layout_intent = detect_layout_intent(normalized);

    const std::optional<Game> detected_game = parse_game(normalized);
    const std::optional<GameMode> detected_mode = parse_mode(normalized);
    if (detected_game)
        spec.game = *detected_game;
    if (overrides.game)
        spec.game = *overrides.game;

    if (detected_mode)
        spec.mode = *detected_mode;
    else if (spec.game == Game::quake3)
        spec.mode = GameMode::deathmatch;
    if (overrides.mode)
        spec.mode = *overrides.mode;

    spec.theme = detect_theme(normalized);

    if (const auto rooms = extract_integer_before(normalized, {"room", "area"})) {
        spec.room_count = std::clamp(*rooms, 2, 32);
    }
    if (const auto players = extract_integer_before(normalized, {"player"})) {
        spec.target_players = std::clamp(*players, 2, 32);
    }
    if (const auto seed = extract_seed(normalized)) {
        spec.seed = *seed;
    }

    if (contains_any(normalized, {"extremely vertical", "very vertical", "high verticality"})) {
        spec.verticality = 0.95;
    } else if (contains_any(normalized, {"flat", "low verticality"})) {
        spec.verticality = 0.15;
    } else if (contains_any(normalized, {"vertical", "multi-level", "multilevel", "height variation"})) {
        spec.verticality = 0.75;
    }

    if (contains_any(normalized, {"brutal", "relentless", "intense", "arena combat", "high intensity"})) {
        spec.intensity = 0.9;
    } else if (contains_any(normalized, {"quiet", "exploration", "low intensity", "atmospheric"})) {
        spec.intensity = 0.25;
    }

    if (overrides.seed)
        spec.seed = *overrides.seed;
    if (overrides.room_count)
        spec.room_count = std::clamp(*overrides.room_count, 2, 32);
    if (overrides.target_players)
        spec.target_players = std::clamp(*overrides.target_players, 2, 32);
    if (overrides.verticality) {
        if (!std::isfinite(*overrides.verticality))
            throw std::invalid_argument("the verticality override must be finite");
        spec.verticality = std::clamp(*overrides.verticality, 0.0, 1.0);
    }
    if (overrides.intensity) {
        if (!std::isfinite(*overrides.intensity))
            throw std::invalid_argument("the intensity override must be finite");
        spec.intensity = std::clamp(*overrides.intensity, 0.0, 1.0);
    }

    if (spec.mode == GameMode::duel) {
        spec.target_players = 2;
        spec.room_count = std::max(spec.room_count, 4);
    }
    if (spec.mode == GameMode::capture_the_flag) {
        spec.target_players = std::max(spec.target_players, 6);
    }

    spec.prefer_loops = spec.game == Game::quake3 || spec.mode != GameMode::single_player ||
                        contains_any(normalized, {"loop", "loops", "nonlinear", "non-linear"});
    spec.title = "Generated " + spec.theme + " " + to_string(spec.mode);

    require_supported_layout_intent(spec);

    return spec;
}

} // namespace qmg
