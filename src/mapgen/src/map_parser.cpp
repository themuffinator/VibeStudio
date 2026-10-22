#include "qmg/map_parser.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>

namespace qmg {
namespace {

enum class TokenKind {
    end,
    word,
    quoted,
    left_brace,
    right_brace,
    left_parenthesis,
    right_parenthesis,
    left_bracket,
    right_bracket,
};

struct Token {
    TokenKind kind{TokenKind::end};
    std::string_view text;
    std::size_t offset{};
    std::size_t line{1};
    std::size_t column{1};
};

class ParseFailure final {
  public:
    explicit ParseFailure(MapParseIssue issue) : issue_(std::move(issue)) {}
    [[nodiscard]] const MapParseIssue &issue() const noexcept { return issue_; }

  private:
    MapParseIssue issue_;
};

[[noreturn]] void fail(const Token &token, std::string code, std::string message) {
    throw ParseFailure({std::move(code), token.offset, token.line, token.column, std::move(message)});
}

[[nodiscard]] const char *token_kind_name(const TokenKind kind) {
    switch (kind) {
    case TokenKind::end:
        return "end of file";
    case TokenKind::word:
        return "token";
    case TokenKind::quoted:
        return "quoted string";
    case TokenKind::left_brace:
        return "{";
    case TokenKind::right_brace:
        return "}";
    case TokenKind::left_parenthesis:
        return "(";
    case TokenKind::right_parenthesis:
        return ")";
    case TokenKind::left_bracket:
        return "[";
    case TokenKind::right_bracket:
        return "]";
    }
    return "unknown token";
}

class Lexer {
  public:
    Lexer(const std::string_view source, const std::size_t max_token_bytes)
        : source_(source), max_token_bytes_(max_token_bytes) {}

    [[nodiscard]] const Token &peek() {
        if (!lookahead_)
            lookahead_ = lex();
        return *lookahead_;
    }

    [[nodiscard]] Token take() {
        const Token token = peek();
        lookahead_.reset();
        return token;
    }

  private:
    void advance() {
        if (position_ >= source_.size())
            return;
        if (source_[position_] == '\r') {
            ++position_;
            if (position_ < source_.size() && source_[position_] == '\n')
                ++position_;
            ++line_;
            column_ = 1;
            return;
        }
        if (source_[position_] == '\n') {
            ++position_;
            ++line_;
            column_ = 1;
            return;
        }
        ++position_;
        ++column_;
    }

    void skip_space_and_comments() {
        for (;;) {
            while (position_ < source_.size()) {
                const unsigned char character = static_cast<unsigned char>(source_[position_]);
                if (character != ' ' && character != '\t' && character != '\v' && character != '\f' &&
                    character != '\r' && character != '\n')
                    break;
                advance();
            }
            if (position_ >= source_.size())
                return;
            if (source_[position_] == ';' || source_[position_] == '#') {
                while (position_ < source_.size() && source_[position_] != '\r' && source_[position_] != '\n')
                    advance();
                continue;
            }
            if (source_[position_] == '/' && position_ + 1 < source_.size() && source_[position_ + 1] == '/') {
                while (position_ < source_.size() && source_[position_] != '\r' && source_[position_] != '\n')
                    advance();
                continue;
            }
            if (source_[position_] == '/' && position_ + 1 < source_.size() && source_[position_ + 1] == '*') {
                const Token start{TokenKind::word, {}, position_, line_, column_};
                advance();
                advance();
                bool closed = false;
                while (position_ < source_.size()) {
                    if (source_[position_] == '*' && position_ + 1 < source_.size() && source_[position_ + 1] == '/') {
                        advance();
                        advance();
                        closed = true;
                        break;
                    }
                    advance();
                }
                if (!closed)
                    fail(start, "unterminated_comment", "block comment is missing its closing delimiter");
                continue;
            }
            return;
        }
    }

    [[nodiscard]] Token punctuation(const TokenKind kind) {
        const Token result{kind, source_.substr(position_, 1), position_, line_, column_};
        advance();
        return result;
    }

    [[nodiscard]] Token lex() {
        skip_space_and_comments();
        if (position_ == source_.size())
            return {TokenKind::end, {}, position_, line_, column_};
        switch (source_[position_]) {
        case '{':
            return punctuation(TokenKind::left_brace);
        case '}':
            return punctuation(TokenKind::right_brace);
        case '(':
            return punctuation(TokenKind::left_parenthesis);
        case ')':
            return punctuation(TokenKind::right_parenthesis);
        case '[':
            return punctuation(TokenKind::left_bracket);
        case ']':
            return punctuation(TokenKind::right_bracket);
        case '"': {
            const std::size_t start = position_;
            const std::size_t start_line = line_;
            const std::size_t start_column = column_;
            advance();
            const std::size_t contents_start = position_;
            while (position_ < source_.size() && source_[position_] != '"') {
                const unsigned char character = static_cast<unsigned char>(source_[position_]);
                if (character < 0x20)
                    fail({TokenKind::quoted, {}, start, start_line, start_column}, "invalid_quoted_string",
                         "quoted map strings cannot contain control characters or span lines");
                if (position_ - contents_start >= max_token_bytes_)
                    fail({TokenKind::quoted, {}, start, start_line, start_column}, "token_too_long",
                         "quoted map string exceeds the configured token bound");
                advance();
            }
            if (position_ == source_.size())
                fail({TokenKind::quoted, {}, start, start_line, start_column}, "unterminated_string",
                     "quoted map string is missing its closing quote");
            const std::string_view contents = source_.substr(contents_start, position_ - contents_start);
            advance();
            return {TokenKind::quoted, contents, start, start_line, start_column};
        }
        default:
            break;
        }

        const std::size_t start = position_;
        const std::size_t start_line = line_;
        const std::size_t start_column = column_;
        while (position_ < source_.size()) {
            const char character = source_[position_];
            const bool punctuation_character = character == '{' || character == '}' || character == '(' ||
                                               character == ')' || character == '[' || character == ']' ||
                                               character == '"' || character == ';' || character == '#';
            const bool whitespace = character == ' ' || character == '\t' || character == '\v' || character == '\f' ||
                                    character == '\r' || character == '\n';
            const bool comment = character == '/' && position_ + 1 < source_.size() &&
                                 (source_[position_ + 1] == '/' || source_[position_ + 1] == '*');
            if (punctuation_character || whitespace || comment)
                break;
            if (position_ - start >= max_token_bytes_)
                fail({TokenKind::word, {}, start, start_line, start_column}, "token_too_long",
                     "map token exceeds the configured token bound");
            advance();
        }
        if (position_ == start)
            fail({TokenKind::word, source_.substr(position_, 1), position_, line_, column_}, "invalid_character",
                 "unexpected character in map source");
        return {TokenKind::word, source_.substr(start, position_ - start), start, start_line, start_column};
    }

    std::string_view source_;
    std::size_t max_token_bytes_{};
    std::size_t position_{};
    std::size_t line_{1};
    std::size_t column_{1};
    std::optional<Token> lookahead_;
};

[[nodiscard]] bool parse_integer_text(std::string_view text, std::int32_t &output) {
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    if (text.empty())
        return false;
    std::int32_t parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return false;
    output = parsed;
    return true;
}

[[nodiscard]] std::optional<std::size_t> invalid_utf8_offset(const std::string_view source) {
    std::size_t index = 0;
    while (index < source.size()) {
        const auto lead = static_cast<unsigned char>(source[index]);
        if (lead <= 0x7f) {
            ++index;
            continue;
        }
        std::size_t continuation_count{};
        std::uint32_t value{};
        std::uint32_t minimum{};
        if ((lead & 0xe0U) == 0xc0U) {
            continuation_count = 1;
            value = lead & 0x1fU;
            minimum = 0x80U;
        } else if ((lead & 0xf0U) == 0xe0U) {
            continuation_count = 2;
            value = lead & 0x0fU;
            minimum = 0x800U;
        } else if ((lead & 0xf8U) == 0xf0U) {
            continuation_count = 3;
            value = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return index;
        }
        if (index + continuation_count >= source.size())
            return index;
        for (std::size_t continuation = 1; continuation <= continuation_count; ++continuation) {
            const auto byte = static_cast<unsigned char>(source[index + continuation]);
            if ((byte & 0xc0U) != 0x80U)
                return index + continuation;
            value = (value << 6U) | (byte & 0x3fU);
        }
        if (value < minimum || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
            return index;
        index += continuation_count + 1;
    }
    return std::nullopt;
}

[[nodiscard]] std::pair<std::size_t, std::size_t> line_and_column(const std::string_view source,
                                                                  const std::size_t offset) {
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t index = 0;
    while (index < std::min(offset, source.size())) {
        if (source[index] == '\r') {
            ++index;
            if (index < source.size() && source[index] == '\n')
                ++index;
            ++line;
            column = 1;
        } else if (source[index] == '\n') {
            ++index;
            ++line;
            column = 1;
        } else {
            ++index;
            ++column;
        }
    }
    return {line, column};
}

class DocumentParser {
  public:
    DocumentParser(const std::string_view source, const MapDialect dialect, const MapParseLimits &limits)
        : dialect_(dialect), limits_(limits), lexer_(source, limits.max_token_bytes) {}

    [[nodiscard]] MapDocument parse() {
        MapDocument document(dialect_);
        std::vector<MapEntity> &entities = document.edit_entities();
        while (lexer_.peek().kind != TokenKind::end) {
            if (entities.size() >= limits_.max_entities)
                fail(lexer_.peek(), "entity_limit", "map exceeds the configured entity bound");
            entities.push_back(parse_entity());
        }
        document.assign_deterministic_node_ids();
        return document;
    }

  private:
    void count_node(const Token &token) {
        if (node_count_ >= limits_.max_total_nodes)
            fail(token, "node_limit", "map exceeds the configured total node bound");
        ++node_count_;
    }

    [[nodiscard]] Token expect(const TokenKind kind, const std::string_view expectation) {
        const Token token = lexer_.take();
        if (required_line_ && token.kind != TokenKind::end && token.line != *required_line_)
            fail(token, "unexpected_line_break", "brush face tokens must remain on one source line");
        if (token.kind != kind) {
            std::ostringstream message;
            message << "expected " << expectation << ", found " << token_kind_name(token.kind);
            fail(token, "unexpected_token", message.str());
        }
        return token;
    }

    [[nodiscard]] Token expect_word(const std::string_view expectation) { return expect(TokenKind::word, expectation); }

    [[nodiscard]] double number(const std::string_view expectation) {
        Token token = expect_word(expectation);
        std::string_view text = token.text;
        if (!text.empty() && text.front() == '+')
            text.remove_prefix(1);
        if (text.empty())
            fail(token, "invalid_number", "numeric token is empty after its sign");
        double value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value))
            fail(token, "invalid_number", "expected a finite decimal number");
        if (std::abs(value) > limits_.max_absolute_number)
            fail(token, "number_out_of_range", "number exceeds the configured absolute bound");
        return value;
    }

    [[nodiscard]] std::int32_t integer(const std::string_view expectation) {
        const Token token = expect_word(expectation);
        std::int32_t value{};
        if (!parse_integer_text(token.text, value))
            fail(token, "invalid_integer", "expected a base-10 32-bit integer");
        return value;
    }

    [[nodiscard]] bool next_is_integer() {
        if (lexer_.peek().kind != TokenKind::word)
            return false;
        std::int32_t ignored{};
        return parse_integer_text(lexer_.peek().text, ignored);
    }

    [[nodiscard]] Vec3 vector3() {
        static_cast<void>(expect(TokenKind::left_parenthesis, "'('"));
        const Vec3 result{number("x coordinate"), number("y coordinate"), number("z coordinate")};
        static_cast<void>(expect(TokenKind::right_parenthesis, "')'"));
        return result;
    }

    [[nodiscard]] MapEntity parse_entity() {
        const Token start = expect(TokenKind::left_brace, "'{' starting an entity");
        count_node(start);
        MapEntity entity;
        bool saw_primitive = false;
        while (lexer_.peek().kind != TokenKind::right_brace) {
            if (lexer_.peek().kind == TokenKind::end)
                fail(lexer_.peek(), "unexpected_eof", "entity is missing its closing brace");
            if (lexer_.peek().kind == TokenKind::quoted) {
                if (saw_primitive)
                    fail(lexer_.peek(), "property_after_primitive",
                         "entity properties must precede brushes and patches in the strict map profile");
                if (entity.properties.size() >= limits_.max_properties_per_entity)
                    fail(lexer_.peek(), "property_limit", "entity exceeds the configured property bound");
                const Token key = lexer_.take();
                const Token value = expect(TokenKind::quoted, "a quoted entity property value");
                if (value.line != key.line)
                    fail(value, "unexpected_line_break", "an entity property key and value must share one source line");
                count_node(key);
                entity.properties.push_back({0, std::string(key.text), std::string(value.text)});
                continue;
            }
            if (lexer_.peek().kind == TokenKind::left_brace) {
                saw_primitive = true;
                if (entity.primitives.size() >= limits_.max_primitives_per_entity)
                    fail(lexer_.peek(), "primitive_limit", "entity exceeds the configured primitive bound");
                entity.primitives.push_back(parse_primitive());
                continue;
            }
            fail(lexer_.peek(), "unexpected_entity_token",
                 "expected a quoted property, brush, patch, or entity closing brace");
        }
        static_cast<void>(lexer_.take());
        return entity;
    }

    [[nodiscard]] MapPrimitive parse_primitive() {
        const Token start = expect(TokenKind::left_brace, "'{' starting a primitive");
        if (lexer_.peek().kind == TokenKind::word && lexer_.peek().text == "patchDef2") {
            if (dialect_ == MapDialect::quake1_valve220 || dialect_ == MapDialect::quake2_valve220)
                fail(lexer_.peek(), "patch_dialect_mismatch", "patchDef2 is only accepted in Quake 3 dialects");
            return parse_patch(start);
        }
        if (dialect_ == MapDialect::quake3_brush_primitives) {
            const Token marker = expect_word("brushDef");
            if (marker.text != "brushDef")
                fail(marker, "brush_dialect_mismatch", "Quake 3 brush primitive documents require brushDef wrappers");
            return parse_brush(start, true);
        }
        if (lexer_.peek().kind == TokenKind::word && lexer_.peek().text == "brushDef")
            fail(lexer_.peek(), "brush_dialect_mismatch", "brushDef is not valid in this explicit map dialect");
        return parse_brush(start, false);
    }

    [[nodiscard]] MapBrush parse_brush(const Token &start, const bool brush_primitive_wrapper) {
        count_node(start);
        MapBrush brush;
        if (brush_primitive_wrapper)
            static_cast<void>(expect(TokenKind::left_brace, "'{' after brushDef"));
        while (lexer_.peek().kind != TokenKind::right_brace) {
            if (lexer_.peek().kind == TokenKind::end)
                fail(lexer_.peek(), "unexpected_eof", "brush is missing its closing brace");
            if (brush.faces.size() >= limits_.max_faces_per_brush)
                fail(lexer_.peek(), "face_limit", "brush exceeds the configured face bound");
            brush.faces.push_back(parse_face());
        }
        static_cast<void>(lexer_.take());
        if (brush_primitive_wrapper)
            static_cast<void>(expect(TokenKind::right_brace, "'}' closing the brushDef wrapper"));
        return brush;
    }

    [[nodiscard]] MapBrushFace parse_face() {
        const Token start = lexer_.peek();
        required_line_ = start.line;
        count_node(start);
        MapBrushFace face;
        for (Vec3 &point : face.plane_points)
            point = vector3();

        if (dialect_ == MapDialect::quake3_brush_primitives) {
            face.projection = parse_brush_primitive_projection();
            face.material = std::string(expect_word("a relative Quake 3 material name").text);
            if (next_is_integer())
                face.quake3_metadata = parse_quake3_metadata();
            required_line_.reset();
            return face;
        }

        face.material = std::string(expect_word("a material name").text);
        if (dialect_ == MapDialect::quake1_valve220 || dialect_ == MapDialect::quake2_valve220) {
            face.projection = parse_valve220_projection();
            if (dialect_ == MapDialect::quake2_valve220 && next_is_integer())
                face.quake2_metadata = parse_quake2_metadata();
            else if (dialect_ == MapDialect::quake1_valve220 && next_is_integer())
                fail(lexer_.peek(), "q1_trailing_metadata",
                     "Quake 1 Valve 220 faces cannot have a trailing flag triplet");
        } else {
            face.projection = parse_legacy_projection();
            if (next_is_integer())
                face.quake3_metadata = parse_quake3_metadata();
        }
        required_line_.reset();
        return face;
    }

    [[nodiscard]] Valve220Projection parse_valve220_projection() {
        Valve220Projection projection;
        auto parse_axis = [&]() {
            static_cast<void>(expect(TokenKind::left_bracket, "'[' starting a Valve 220 texture axis"));
            Valve220Axis axis{{number("texture-axis x"), number("texture-axis y"), number("texture-axis z")},
                              number("texture-axis shift")};
            static_cast<void>(expect(TokenKind::right_bracket, "']' ending a Valve 220 texture axis"));
            return axis;
        };
        projection.s_axis = parse_axis();
        projection.t_axis = parse_axis();
        projection.rotation = number("Valve 220 rotation");
        projection.s_scale = number("Valve 220 s scale");
        projection.t_scale = number("Valve 220 t scale");
        if (projection.s_scale == 0.0 || projection.t_scale == 0.0)
            fail(lexer_.peek(), "zero_texture_scale", "Valve 220 texture scales must be non-zero");
        return projection;
    }

    [[nodiscard]] Quake3LegacyProjection parse_legacy_projection() {
        Quake3LegacyProjection projection;
        projection.s_shift = number("legacy s shift");
        projection.t_shift = number("legacy t shift");
        projection.rotation = number("legacy rotation");
        projection.s_scale = number("legacy s scale");
        projection.t_scale = number("legacy t scale");
        if (projection.s_scale == 0.0 || projection.t_scale == 0.0)
            fail(lexer_.peek(), "zero_texture_scale", "legacy texture scales must be non-zero");
        return projection;
    }

    [[nodiscard]] Quake3BrushPrimitiveProjection parse_brush_primitive_projection() {
        Quake3BrushPrimitiveProjection projection;
        static_cast<void>(expect(TokenKind::left_parenthesis, "'(' starting a brush primitive matrix"));
        for (auto &row : projection.matrix) {
            static_cast<void>(expect(TokenKind::left_parenthesis, "'(' starting a brush primitive matrix row"));
            for (double &component : row)
                component = number("brush primitive matrix component");
            static_cast<void>(expect(TokenKind::right_parenthesis, "')' ending a brush primitive matrix row"));
        }
        static_cast<void>(expect(TokenKind::right_parenthesis, "')' ending a brush primitive matrix"));
        return projection;
    }

    [[nodiscard]] Quake2FaceMetadata parse_quake2_metadata() {
        return {integer("Quake 2 contents"), integer("Quake 2 surface flags"), integer("Quake 2 value")};
    }

    [[nodiscard]] Quake3FaceMetadata parse_quake3_metadata() {
        return {integer("Quake 3 contents"), integer("Quake 3 surface flags"), integer("Quake 3 value")};
    }

    [[nodiscard]] MapPatch parse_patch(const Token &start) {
        count_node(start);
        static_cast<void>(lexer_.take()); // patchDef2
        static_cast<void>(expect(TokenKind::left_brace, "'{' after patchDef2"));
        MapPatch patch;
        patch.material = std::string(expect_word("a relative Quake 3 patch material name").text);
        static_cast<void>(expect(TokenKind::left_parenthesis, "'(' starting the patchDef2 header"));
        const std::int32_t width = integer("patch width");
        const std::int32_t height = integer("patch height");
        for (std::int32_t &reserved : patch.reserved)
            reserved = integer("patchDef2 reserved header value");
        static_cast<void>(expect(TokenKind::right_parenthesis, "')' ending the patchDef2 header"));
        const std::size_t maximum_dimension =
            std::min(limits_.max_patch_dimension, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
        if (width < 3 || height < 3 || width % 2 == 0 || height % 2 == 0 ||
            static_cast<std::size_t>(width) > maximum_dimension || static_cast<std::size_t>(height) > maximum_dimension)
            fail(start, "invalid_patch_dimensions",
                 "patchDef2 width and height must be odd values from 3 through the configured maximum");
        patch.width = static_cast<std::uint32_t>(width);
        patch.height = static_cast<std::uint32_t>(height);
        const std::size_t control_count = static_cast<std::size_t>(patch.width) * patch.height;
        if (control_count > limits_.max_total_nodes - std::min(node_count_, limits_.max_total_nodes))
            fail(start, "node_limit", "patch control grid exceeds the remaining configured node bound");
        patch.control_points.reserve(control_count);

        static_cast<void>(expect(TokenKind::left_parenthesis, "'(' starting the patch control matrix"));
        for (std::uint32_t column = 0; column < patch.width; ++column) {
            static_cast<void>(expect(TokenKind::left_parenthesis, "'(' starting a patch control column"));
            for (std::uint32_t row = 0; row < patch.height; ++row) {
                const Token control_start = expect(TokenKind::left_parenthesis, "'(' starting a patch control point");
                count_node(control_start);
                MapPatchControlPoint control;
                control.position = {number("patch x coordinate"), number("patch y coordinate"),
                                    number("patch z coordinate")};
                control.s = number("patch s coordinate");
                control.t = number("patch t coordinate");
                static_cast<void>(expect(TokenKind::right_parenthesis, "')' ending a patch control point"));
                patch.control_points.push_back(control);
            }
            static_cast<void>(expect(TokenKind::right_parenthesis, "')' ending a patch control column"));
        }
        static_cast<void>(expect(TokenKind::right_parenthesis, "')' ending the patch control matrix"));
        static_cast<void>(expect(TokenKind::right_brace, "'}' closing patchDef2"));
        static_cast<void>(expect(TokenKind::right_brace, "'}' closing the patch primitive wrapper"));
        return patch;
    }

    MapDialect dialect_;
    const MapParseLimits &limits_;
    Lexer lexer_;
    std::size_t node_count_{};
    std::optional<std::size_t> required_line_;
};

[[nodiscard]] bool valid_limits(const MapParseLimits &limits) {
    return limits.max_source_bytes > 0 && limits.max_token_bytes > 0 && limits.max_entities > 0 &&
           limits.max_properties_per_entity > 0 && limits.max_primitives_per_entity > 0 &&
           limits.max_faces_per_brush > 0 && limits.max_patch_dimension >= 3 && limits.max_total_nodes > 0 &&
           std::isfinite(limits.max_absolute_number) && limits.max_absolute_number > 0.0;
}

} // namespace

MapParseError::MapParseError(MapParseIssue issue)
    : std::runtime_error("map parse error at " + std::to_string(issue.line) + ':' + std::to_string(issue.column) +
                         ": " + issue.message),
      issue_(std::move(issue)) {}

MapParser::MapParser(MapParseLimits limits) : limits_(std::move(limits)) {}

MapParseResult MapParser::parse(const std::string_view source, const MapDialect dialect) const {
    MapParseResult result;
    if (!valid_limits(limits_)) {
        result.issues.push_back({"invalid_parse_limits", 0, 1, 1,
                                 "parser bounds must be non-zero and the numeric bound must be finite and positive"});
        return result;
    }
    if (source.size() > limits_.max_source_bytes) {
        result.issues.push_back({"source_too_large", 0, 1, 1, "map source exceeds the configured byte-size bound"});
        return result;
    }
    if (const std::size_t nul = source.find('\0'); nul != std::string_view::npos) {
        const auto [line, column] = line_and_column(source, nul);
        result.issues.push_back({"embedded_nul", nul, line, column, "map source contains an embedded NUL byte"});
        return result;
    }
    if (const std::optional<std::size_t> invalid = invalid_utf8_offset(source)) {
        const auto [line, column] = line_and_column(source, *invalid);
        result.issues.push_back({"invalid_utf8", *invalid, line, column, "map source is not valid UTF-8"});
        return result;
    }
    try {
        MapDocument document = DocumentParser(source, dialect, limits_).parse();
        if (limits_.preserve_pristine_source)
            document.adopt_pristine_source(std::string(source));
        result.document = std::move(document);
    } catch (const ParseFailure &failure) {
        result.issues.push_back(failure.issue());
    }
    return result;
}

MapDocument MapParser::parse_or_throw(const std::string_view source, const MapDialect dialect) const {
    MapParseResult result = parse(source, dialect);
    if (!result.ok())
        throw MapParseError(std::move(result.issues.front()));
    return std::move(*result.document);
}

} // namespace qmg
