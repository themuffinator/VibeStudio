#include "qmg/map_document.hpp"
#include "qmg/map_document_writer.hpp"
#include "qmg/map_parser.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename Function> void require_throws(Function &&function, const std::string_view message) {
    try {
        function();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] std::filesystem::path golden_directory() {
    std::filesystem::path source_path = __FILE__;
    if (source_path.is_relative())
        source_path = std::filesystem::absolute(source_path);
    return source_path.parent_path() / "golden";
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("could not open golden fixture: " + path.string());
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0)
        throw std::runtime_error("could not determine golden fixture size: " + path.string());
    input.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(length), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!input && !input.eof())
        throw std::runtime_error("could not read golden fixture: " + path.string());
    return contents;
}

[[nodiscard]] bool has_issue(const qmg::MapValidationResult &result, const std::string_view code) {
    for (const qmg::MapValidationIssue &issue : result.issues) {
        if (issue.code == code)
            return true;
    }
    return false;
}

[[nodiscard]] bool has_issue(const qmg::MapParseResult &result, const std::string_view code) {
    for (const qmg::MapParseIssue &issue : result.issues) {
        if (issue.code == code)
            return true;
    }
    return false;
}

[[nodiscard]] qmg::MapValidationOptions generated_validation_options() {
    qmg::MapValidationOptions options;
    options.profile = qmg::MapValidationProfile::generated_document;
    return options;
}

void require_sequential_ids(const qmg::MapDocument &document) {
    qmg::MapNodeId expected = 1;
    for (const qmg::MapEntity &entity : document.entities()) {
        require(entity.id == expected++, "entity ID is not deterministic preorder");
        for (const qmg::MapEntityProperty &property : entity.properties)
            require(property.id == expected++, "property ID is not deterministic preorder");
        for (const qmg::MapPrimitive &primitive : entity.primitives) {
            std::visit(
                [&](const auto &value) {
                    require(value.id == expected++, "primitive ID is not deterministic preorder");
                    using Primitive = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Primitive, qmg::MapBrush>) {
                        for (const qmg::MapBrushFace &face : value.faces)
                            require(face.id == expected++, "face ID is not deterministic preorder");
                    } else {
                        for (const qmg::MapPatchControlPoint &control : value.control_points)
                            require(control.id == expected++, "patch control ID is not deterministic preorder");
                    }
                },
                primitive);
        }
    }
}

[[nodiscard]] qmg::MapDocument parse_golden(const std::string_view name, const qmg::MapDialect dialect) {
    const std::string source = read_file(golden_directory() / name);
    qmg::MapParseResult parsed = qmg::MapParser{}.parse(source, dialect);
    if (!parsed.ok()) {
        const qmg::MapParseIssue &issue = parsed.issues.front();
        throw std::runtime_error(std::string(name) + " did not parse at " + std::to_string(issue.line) + ':' +
                                 std::to_string(issue.column) + ": " + issue.message);
    }
    qmg::MapDocument document = std::move(*parsed.document);
    require(document.source_is_pristine(), "parsed document did not retain pristine source");
    const qmg::MapDocumentWriter writer;
    qmg::MapWriteOptions preserving;
    preserving.mode = qmg::MapWriteMode::preserve_pristine_source;
    require(writer.write(document, preserving) == source, "pristine writer did not preserve source bytes");
    const std::string canonical = writer.write(document);
    require(canonical == source, "golden fixture is not the exact canonical representation");
    const qmg::MapDocument reparsed = qmg::MapParser{}.parse_or_throw(canonical, dialect);
    require(writer.write(reparsed) == canonical, "canonical rendering is not idempotent");
    require_sequential_ids(document);
    const qmg::MapValidationResult validation = qmg::validate_map_document(document);
    if (!validation.ok()) {
        const qmg::MapValidationIssue &issue = validation.issues.front();
        throw std::runtime_error(std::string(name) + " failed validation [" + issue.code + "]: " + issue.message);
    }
    return document;
}

void test_golden_round_trips() {
    qmg::MapDocument q1 = parse_golden("map_document_q1_valve220.map", qmg::MapDialect::quake1_valve220);
    const qmg::MapEntity &q1_world = q1.entities().front();
    require(q1_world.properties.size() == 4, "Q1 duplicate properties were not preserved");
    require(q1_world.properties[2].key == "message" && q1_world.properties[2].value == "first" &&
                q1_world.properties[3].key == "message" && q1_world.properties[3].value == "second",
            "Q1 duplicate property order changed");
    require(q1_world.first_value("message") == "first" && q1_world.last_value("message") == "second",
            "first/last duplicate property lookup is incorrect");

    qmg::MapDocument q2 = parse_golden("map_document_q2_valve220.map", qmg::MapDialect::quake2_valve220);
    const auto &q2_brush = std::get<qmg::MapBrush>(q2.entities().front().primitives.front());
    require(q2_brush.faces[0].quake2_metadata.has_value(), "explicit Q2 zero triplet lost its presence");
    require(!q2_brush.faces[3].quake2_metadata.has_value(), "absent Q2 metadata became explicit zeros");
    require(q2_brush.faces[2].quake2_metadata->contents == 134217728, "Q2 contents metadata was not preserved exactly");

    qmg::MapDocument q3_legacy = parse_golden("map_document_q3_legacy.map", qmg::MapDialect::quake3_legacy);
    const auto &patch = std::get<qmg::MapPatch>(q3_legacy.entities().front().primitives[1]);
    require(patch.width == 3 && patch.height == 3 && patch.reserved[0] == 7 && patch.reserved[1] == -8 &&
                patch.reserved[2] == 9,
            "patchDef2 header dimensions or reserved values changed");
    require(patch.control_points.size() == 9, "patchDef2 control count changed");
    require(patch.control(1, 2).position.x == 64 && patch.control(1, 2).position.y == 128 &&
                patch.control(1, 2).position.z == 80,
            "patchDef2 width-major control ordering changed");

    static_cast<void>(parse_golden("map_document_q3_brush_primitives.map", qmg::MapDialect::quake3_brush_primitives));
}

void test_pristine_invalidation() {
    const std::string source = read_file(golden_directory() / "map_document_q1_valve220.map");
    qmg::MapDocument document = qmg::MapParser{}.parse_or_throw(source, qmg::MapDialect::quake1_valve220);
    document.edit_entities().front().properties[2].value = "mutated";
    require(!document.source_is_pristine(), "mutable access did not invalidate pristine source");
    qmg::MapWriteOptions preserving;
    preserving.mode = qmg::MapWriteMode::preserve_pristine_source;
    const std::string output = qmg::MapDocumentWriter{}.write(document, preserving);
    require(output != source && output.find("\"message\" \"mutated\"") != std::string::npos,
            "preserve mode did not fall back to canonical output after mutation");

    document = qmg::MapParser{}.parse_or_throw(source, qmg::MapDialect::quake1_valve220);
    document.set_dialect(qmg::MapDialect::quake2_valve220);
    require(!document.source_is_pristine(), "dialect mutation did not invalidate pristine source");
}

void test_comments_and_canonical_numbers() {
    const std::string source =
        "# authored header\r\n/* bounded block */\n{\n\"classname\" \"worldspawn\" ; property comment\n"
        "\"mapversion\" \"220\"\n{\n"
        "( 1 1 1 ) ( 1 1 -0 ) ( 1 0 1 ) test [ 1 0 0 -0 ] [ 0 1 0 0 ] -0 1 1 // face\n"
        "}\n}\n";
    qmg::MapDocument document = qmg::MapParser{}.parse_or_throw(source, qmg::MapDialect::quake1_valve220);
    qmg::MapWriteOptions preserving;
    preserving.mode = qmg::MapWriteMode::preserve_pristine_source;
    require(qmg::MapDocumentWriter{}.write(document, preserving) == source,
            "comments or CRLF were not preserved while pristine");
    const std::string canonical = qmg::MapDocumentWriter{}.write(document);
    require(canonical.find("-0") == std::string::npos, "canonical writer retained negative zero");
    require(qmg::MapDocumentWriter{}.write(
                qmg::MapParser{}.parse_or_throw(canonical, qmg::MapDialect::quake1_valve220)) == canonical,
            "comment-normalised source did not reach a canonical fixed point");
}

void test_outward_face_ordering() {
    const std::array<qmg::Vec3, 3> outward{{{128, 128, 128}, {128, 128, 0}, {128, 0, 128}}};
    const qmg::Vec3 interior{0, 0, 0};
    const std::array<qmg::Vec3, 3> unchanged = qmg::order_face_points_outward(outward, interior);
    require(unchanged[1].x == outward[1].x && unchanged[1].y == outward[1].y && unchanged[1].z == outward[1].z,
            "already-outward Quake face ordering changed");

    const std::array<qmg::Vec3, 3> inward{{outward[0], outward[2], outward[1]}};
    const std::array<qmg::Vec3, 3> corrected = qmg::order_face_points_outward(inward, interior);
    require(corrected[0].x == outward[0].x && corrected[1].y == outward[1].y && corrected[1].z == outward[1].z &&
                corrected[2].y == outward[2].y,
            "inward Quake face ordering was not reversed");

    require_throws(
        [&] {
            static_cast<void>(qmg::order_face_points_outward({{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}}, qmg::Vec3{0, 1, 0}));
        },
        "degenerate face ordering was accepted");
    require_throws(
        [&] {
            static_cast<void>(
                qmg::order_face_points_outward({{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}}}, qmg::Vec3{0.25, 0.25, 0}));
        },
        "coplanar interior point was accepted for face ordering");
    require_throws(
        [&] {
            static_cast<void>(qmg::order_face_points_outward(
                {{{0, 0, 0}, {0, 1, 0}, {1, 0, std::numeric_limits<double>::infinity()}}}, qmg::Vec3{0, 0, -1}));
        },
        "non-finite face ordering input was accepted");
}

void test_parser_rejections_and_bounds() {
    require(qmg::MapParseLimits{}.max_faces_per_brush == 512,
            "default parser face bound diverged from bounded geometry validation");
    const std::string q1 = read_file(golden_directory() / "map_document_q1_valve220.map");
    qmg::MapParseLimits limits;
    limits.max_source_bytes = q1.size() - 1;
    require(has_issue(qmg::MapParser(limits).parse(q1, qmg::MapDialect::quake1_valve220), "source_too_large"),
            "source byte bound was not enforced");

    limits = {};
    limits.max_token_bytes = 4;
    require(has_issue(qmg::MapParser(limits).parse(q1, qmg::MapDialect::quake1_valve220), "token_too_long"),
            "token byte bound was not enforced");

    limits = {};
    limits.max_total_nodes = 2;
    require(has_issue(qmg::MapParser(limits).parse(q1, qmg::MapDialect::quake1_valve220), "node_limit"),
            "total node bound was not enforced");

    std::string invalid_utf8 = "{\n\"classname\" \"world";
    invalid_utf8.push_back(static_cast<char>(0xff));
    invalid_utf8 += "\"\n}\n";
    require(has_issue(qmg::MapParser{}.parse(invalid_utf8, qmg::MapDialect::quake1_valve220), "invalid_utf8"),
            "invalid UTF-8 was not rejected");

    std::string embedded_nul = "{}";
    embedded_nul.push_back('\0');
    require(has_issue(qmg::MapParser{}.parse(embedded_nul, qmg::MapDialect::quake1_valve220), "embedded_nul"),
            "embedded NUL was not rejected");

    const std::string partial_q2 = "{ \"classname\" \"worldspawn\" \"mapversion\" \"220\" { "
                                   "( 1 1 1 ) ( 1 1 0 ) ( 1 0 1 ) mat [ 1 0 0 0 ] [ 0 1 0 0 ] 0 1 1 2 3 } }";
    require(!qmg::MapParser{}.parse(partial_q2, qmg::MapDialect::quake2_valve220).ok(),
            "partial Q2 metadata triplet was accepted");

    const std::string zero_scale = "{ \"classname\" \"worldspawn\" \"mapversion\" \"220\" { "
                                   "( 1 1 1 ) ( 1 1 0 ) ( 1 0 1 ) mat [ 1 0 0 0 ] [ 0 1 0 0 ] 0 0 1 } }";
    require(has_issue(qmg::MapParser{}.parse(zero_scale, qmg::MapDialect::quake1_valve220), "zero_texture_scale"),
            "zero Valve 220 scale was accepted");

    const std::string wrapped_face = "{ \"classname\" \"worldspawn\" \"mapversion\" \"220\" {\n"
                                     "( 1 1 1 ) ( 1 1 0 ) ( 1 0 1 ) mat\n[ 0 1 0 0 ] [ 0 0 -1 0 ] 0 1 1\n} }";
    require(has_issue(qmg::MapParser{}.parse(wrapped_face, qmg::MapDialect::quake1_valve220), "unexpected_line_break"),
            "compiler-incompatible wrapped brush face was accepted");

    const std::string wrapped_property = "{\n\"classname\"\n\"worldspawn\"\n}";
    require(
        has_issue(qmg::MapParser{}.parse(wrapped_property, qmg::MapDialect::quake1_valve220), "unexpected_line_break"),
        "compiler-incompatible wrapped entity property was accepted");

    const std::string brush_def = "{ \"classname\" \"worldspawn\" { brushDef { "
                                  "( 1 1 1 ) ( 1 1 0 ) ( 1 0 1 ) ( ( 1 0 0 ) ( 0 1 0 ) ) mat 0 0 0 } } }";
    require(has_issue(qmg::MapParser{}.parse(brush_def, qmg::MapDialect::quake3_legacy), "brush_dialect_mismatch"),
            "brushDef was accepted in explicit Q3 legacy mode");

    const std::string invalid_patch = "{ \"classname\" \"worldspawn\" { patchDef2 { mat ( 4 3 0 0 0 ) ( ) } } }";
    require(
        has_issue(qmg::MapParser{}.parse(invalid_patch, qmg::MapDialect::quake3_legacy), "invalid_patch_dimensions"),
        "even patchDef2 width was accepted");

    require(!qmg::MapParser{}.parse("$include other.map", qmg::MapDialect::quake1_valve220).ok(),
            "$include was interpreted or accepted");
    require_throws(
        [&] { static_cast<void>(qmg::MapParser{}.parse_or_throw("not-a-map", qmg::MapDialect::quake1_valve220)); },
        "parse_or_throw did not throw MapParseError");
}

void test_generated_validation_profile() {
    const qmg::MapValidationOptions generated = generated_validation_options();

    qmg::MapDocument q1 = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q1_valve220.map"), qmg::MapDialect::quake1_valve220);
    require(qmg::validate_map_document(q1).ok(), "syntax-preserving profile rejected duplicate parsed epairs");
    require(has_issue(qmg::validate_map_document(q1, generated), "generated_duplicate_entity_key"),
            "generated profile accepted duplicate entity keys");
    q1.edit_entities().front().properties.pop_back();
    q1.assign_deterministic_node_ids();
    require(qmg::validate_map_document(q1, generated).ok(), "valid generated Q1 document failed strict validation");

    qmg::MapEntityProperty duplicate_mapversion{0, "mapversion", "220"};
    q1.edit_entities().front().properties.push_back(duplicate_mapversion);
    q1.assign_deterministic_node_ids();
    const qmg::MapValidationResult duplicate_version = qmg::validate_map_document(q1, generated);
    require(has_issue(duplicate_version, "generated_mapversion"),
            "generated Valve document accepted more than one mapversion");

    qmg::MapDocument q3 = qmg::MapParser{}.parse_or_throw(read_file(golden_directory() / "map_document_q3_legacy.map"),
                                                          qmg::MapDialect::quake3_legacy);
    require(qmg::validate_map_document(q3, generated).ok(),
            "valid generated Q3 legacy document failed strict validation");
    auto &q3_world = q3.edit_entities().front();
    auto &q3_brush = std::get<qmg::MapBrush>(q3_world.primitives.front());
    q3_brush.faces.front().quake3_metadata.reset();
    q3_world.properties.push_back({0, "message", "first"});
    q3_world.properties.push_back({0, "message", "second"});
    q3.assign_deterministic_node_ids();
    require(qmg::validate_map_document(q3).ok(),
            "syntax-preserving profile rejected optional Q3 metadata or duplicate parsed epairs");
    const qmg::MapValidationResult strict_q3 = qmg::validate_map_document(q3, generated);
    require(has_issue(strict_q3, "generated_q3_face_metadata"),
            "generated profile accepted a Q3 brush face without c/s/v metadata");
    require(has_issue(strict_q3, "generated_duplicate_entity_key"),
            "generated profile accepted duplicate Q3 entity keys");
    const std::string permissive_canonical = qmg::MapDocumentWriter{}.write(q3);
    const qmg::MapDocument permissive_reparse =
        qmg::MapParser{}.parse_or_throw(permissive_canonical, qmg::MapDialect::quake3_legacy);
    require(qmg::MapDocumentWriter{}.write(permissive_reparse) == permissive_canonical,
            "syntax-preserving canonical output with strict-profile violations did not reparse idempotently");

    qmg::MapDocument q3_versioned = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q3_legacy.map"), qmg::MapDialect::quake3_legacy);
    q3_versioned.edit_entities().front().properties.push_back({0, "mapversion", "220"});
    q3_versioned.assign_deterministic_node_ids();
    require(qmg::validate_map_document(q3_versioned).ok(),
            "syntax-preserving profile rejected a parseable Q3 mapversion epair");
    require(has_issue(qmg::validate_map_document(q3_versioned, generated), "generated_mapversion"),
            "generated profile accepted mapversion in Quake 3");

    qmg::MapDocument duplicate_world = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q3_legacy.map"), qmg::MapDialect::quake3_legacy);
    qmg::MapEntity second_world;
    second_world.properties.push_back({0, "classname", "worldspawn"});
    duplicate_world.edit_entities().push_back(std::move(second_world));
    duplicate_world.assign_deterministic_node_ids();
    require(qmg::validate_map_document(duplicate_world).ok(),
            "syntax-preserving profile rejected a duplicate parsed worldspawn entity");
    require(has_issue(qmg::validate_map_document(duplicate_world, generated), "generated_worldspawn"),
            "generated profile accepted more than one worldspawn");

    qmg::MapDocument duplicate_classname = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q3_legacy.map"), qmg::MapDialect::quake3_legacy);
    duplicate_classname.edit_entities().front().properties.push_back({0, "classname", "func_group"});
    duplicate_classname.assign_deterministic_node_ids();
    require(has_issue(qmg::validate_map_document(duplicate_classname, generated), "generated_classname_count"),
            "generated profile accepted multiple classname properties");

    qmg::MapDocument missing_classname = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q3_legacy.map"), qmg::MapDialect::quake3_legacy);
    qmg::MapEntity classless_entity;
    classless_entity.properties.push_back({0, "origin", "0 0 0"});
    missing_classname.edit_entities().push_back(std::move(classless_entity));
    missing_classname.assign_deterministic_node_ids();
    require(has_issue(qmg::validate_map_document(missing_classname, generated), "generated_classname_count"),
            "generated profile accepted an entity without classname");

    qmg::MapDocument q2 = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q2_valve220.map"), qmg::MapDialect::quake2_valve220);
    require(has_issue(qmg::validate_map_document(q2, generated), "generated_detail_contents"),
            "generated Q2 profile accepted partial explicit detail metadata");
    auto &q2_brush = std::get<qmg::MapBrush>(q2.edit_entities().front().primitives.front());
    for (qmg::MapBrushFace &face : q2_brush.faces)
        face.quake2_metadata = qmg::Quake2FaceMetadata{qmg::kMapDetailContentsBit, 0, 0};
    q2.assign_deterministic_node_ids();
    require(qmg::validate_map_document(q2, generated).ok(),
            "generated Q2 profile rejected consistently explicit detail metadata");

    qmg::MapDocument q3_detail = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q3_legacy.map"), qmg::MapDialect::quake3_legacy);
    auto &detail_brush = std::get<qmg::MapBrush>(q3_detail.edit_entities().front().primitives.front());
    detail_brush.faces.front().quake3_metadata->contents = qmg::kMapDetailContentsBit;
    require(has_issue(qmg::validate_map_document(q3_detail, generated), "generated_detail_contents"),
            "generated Q3 profile accepted inconsistent C_DETAIL face metadata");
    for (qmg::MapBrushFace &face : detail_brush.faces)
        face.quake3_metadata->contents = qmg::kMapDetailContentsBit;
    q3_detail.assign_deterministic_node_ids();
    require(qmg::validate_map_document(q3_detail, generated).ok(),
            "generated Q3 profile rejected consistent C_DETAIL face metadata");

    qmg::MapWriteOptions strict_write;
    strict_write.validate_document = true;
    strict_write.validation_options = generated;
    const std::string strict_canonical = qmg::MapDocumentWriter{}.write(q3_detail, strict_write);
    const qmg::MapDocument strict_reparse =
        qmg::MapParser{}.parse_or_throw(strict_canonical, qmg::MapDialect::quake3_legacy);
    require(qmg::validate_map_document(strict_reparse, generated).ok(),
            "strict generated canonical output did not reparse under the same profile");
}

void test_material_comment_safety() {
    constexpr std::string_view unsafe_materials[]{"base//wall", "base/*wall", "base*/wall"};
    for (const std::string_view material : unsafe_materials) {
        qmg::MapDocument document = qmg::MapParser{}.parse_or_throw(
            read_file(golden_directory() / "map_document_q3_legacy.map"), qmg::MapDialect::quake3_legacy);
        std::get<qmg::MapBrush>(document.edit_entities().front().primitives.front()).faces.front().material = material;
        document.assign_deterministic_node_ids();
        require(has_issue(qmg::validate_map_document(document), "invalid_material"),
                "material containing a lexer-changing comment delimiter passed validation");
        require_throws([&] { static_cast<void>(qmg::MapDocumentWriter{}.write(document)); },
                       "canonical writer emitted a lexer-changing material token");
    }

    std::string preserving_source = read_file(golden_directory() / "map_document_q3_legacy.map");
    const std::size_t material_offset = preserving_source.find("base_wall/comp3text");
    require(material_offset != std::string::npos, "could not prepare pristine unsafe-material fixture");
    preserving_source.replace(material_offset, std::string_view("base_wall/comp3text").size(), "base*/wall");
    qmg::MapDocument preserving = qmg::MapParser{}.parse_or_throw(preserving_source, qmg::MapDialect::quake3_legacy);
    qmg::MapWriteOptions preserve_options;
    preserve_options.mode = qmg::MapWriteMode::preserve_pristine_source;
    require(qmg::MapDocumentWriter{}.write(preserving, preserve_options) == preserving_source,
            "pristine syntax preservation rejected an already-parsed legacy material token");
    require_throws([&] { static_cast<void>(qmg::MapDocumentWriter{}.write(preserving)); },
                   "canonical writer emitted a pristine material that changes lexer state");
}

void test_brush_primitive_matrix_validation() {
    qmg::MapDocument document =
        qmg::MapParser{}.parse_or_throw(read_file(golden_directory() / "map_document_q3_brush_primitives.map"),
                                        qmg::MapDialect::quake3_brush_primitives);
    require(qmg::validate_map_document(document, generated_validation_options()).ok(),
            "valid generated brush primitive document failed strict validation");
    auto &projection = std::get<qmg::Quake3BrushPrimitiveProjection>(
        std::get<qmg::MapBrush>(document.edit_entities().front().primitives.front()).faces.front().projection);
    projection.matrix = {{{{1, 0, 0}}, {{2, 0, 1}}}};
    require(has_issue(qmg::validate_map_document(document), "invalid_brush_primitive_projection"),
            "singular brush primitive linear transform passed validation");

    projection.matrix = {{{{1, 0, 0}}, {{2, 0, 0}}}};
    require(has_issue(qmg::validate_map_document(document), "invalid_brush_primitive_projection"),
            "rank-deficient brush primitive rows passed validation");

    const std::string syntax_only = qmg::MapDocumentWriter{}.write(document);
    require(qmg::MapParser{}.parse(syntax_only, qmg::MapDialect::quake3_brush_primitives).ok(),
            "syntax-preserving writer produced an unparsable rank-deficient BP matrix");
    qmg::MapWriteOptions validated;
    validated.validate_document = true;
    validated.validation_options = generated_validation_options();
    require_throws([&] { static_cast<void>(qmg::MapDocumentWriter{}.write(document, validated)); },
                   "validated writer accepted a rank-deficient brush primitive matrix");
}

void test_document_validation() {
    qmg::MapDocument q1 = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q1_valve220.map"), qmg::MapDialect::quake1_valve220);
    auto &open_brush = std::get<qmg::MapBrush>(q1.edit_entities().front().primitives.front());
    open_brush.faces.pop_back();
    q1.assign_deterministic_node_ids();
    const qmg::MapValidationResult open_validation = qmg::validate_map_document(q1);
    require(has_issue(open_validation, "unbounded_brush") || has_issue(open_validation, "empty_brush_face"),
            "open brush was not rejected as non-closed");

    qmg::MapDocument invalid_projection = qmg::MapParser{}.parse_or_throw(
        read_file(golden_directory() / "map_document_q1_valve220.map"), qmg::MapDialect::quake1_valve220);
    auto &projected_face =
        std::get<qmg::MapBrush>(invalid_projection.edit_entities().front().primitives.front()).faces.front();
    std::get<qmg::Valve220Projection>(projected_face.projection).s_axis.axis = {1, 0, 0};
    std::get<qmg::Valve220Projection>(projected_face.projection).t_axis.axis = {0, 1, 0};
    require(has_issue(qmg::validate_map_document(invalid_projection), "invalid_valve220_projection"),
            "Valve 220 texture axes that cannot span the face were not rejected");

    qmg::MapDocument q3 = qmg::MapParser{}.parse_or_throw(read_file(golden_directory() / "map_document_q3_legacy.map"),
                                                          qmg::MapDialect::quake3_legacy);
    auto &q3_brush = std::get<qmg::MapBrush>(q3.edit_entities().front().primitives.front());
    q3_brush.faces.front().projection = qmg::Quake3BrushPrimitiveProjection{};
    q3.assign_deterministic_node_ids();
    const qmg::MapValidationResult mixed_validation = qmg::validate_map_document(q3);
    require(has_issue(mixed_validation, "q3_mixed_brush_formats"),
            "mixed Q3 legacy and brush primitive projections were not detected");
    require(has_issue(mixed_validation, "projection_dialect_mismatch"), "projection/dialect mismatch was not detected");

    q3_brush.faces.front().material = "textures/base_wall/comp3text";
    require(has_issue(qmg::validate_map_document(q3), "q3_material_prefix"),
            "absolute Quake 3 textures/ source token was not rejected");
    require_throws([&] { static_cast<void>(qmg::MapDocumentWriter{}.write(q3)); },
                   "writer accepted a double-prefix-prone Quake 3 material");

    qmg::MapWriteOptions validated;
    validated.validate_document = true;
    require_throws([&] { static_cast<void>(qmg::MapDocumentWriter{}.write(q1, validated)); },
                   "validated writer accepted an open brush");
}

} // namespace

int main() {
    try {
        test_golden_round_trips();
        test_pristine_invalidation();
        test_comments_and_canonical_numbers();
        test_outward_face_ordering();
        test_parser_rejections_and_bounds();
        test_generated_validation_profile();
        test_material_comment_safety();
        test_brush_primitive_matrix_validation();
        test_document_validation();
        std::cout << "MapDocument format tests passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "MapDocument format test failure: " << error.what() << '\n';
        return 1;
    }
}
