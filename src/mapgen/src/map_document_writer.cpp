#include "qmg/map_document_writer.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace qmg {
namespace {

[[nodiscard]] bool valid_property_text(const std::string_view value) {
    return std::none_of(value.begin(), value.end(), [](const unsigned char character) {
        return character == '"' || character == '\0' || character == '\r' || character == '\n' || character < 0x20;
    });
}

[[nodiscard]] bool valid_material(const std::string_view material) {
    if (material.empty())
        return false;
    if (material.find("//") != std::string_view::npos || material.find("/*") != std::string_view::npos ||
        material.find("*/") != std::string_view::npos)
        return false;
    return std::none_of(material.begin(), material.end(), [](const unsigned char character) {
        return character <= 0x20 || character == '"' || character == '(' || character == ')' || character == '{' ||
               character == '}' || character == '[' || character == ']' || character == ';' || character == '#';
    });
}

[[nodiscard]] bool starts_with_textures(const std::string_view material) {
    constexpr std::string_view prefix = "textures/";
    if (material.size() < prefix.size())
        return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        unsigned char character = static_cast<unsigned char>(material[index]);
        if (character >= 'A' && character <= 'Z')
            character = static_cast<unsigned char>(character - 'A' + 'a');
        if (character != static_cast<unsigned char>(prefix[index]))
            return false;
    }
    return true;
}

void append_number(std::string &output, double value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("cannot write a non-finite map number");
    if (value == 0.0)
        value = 0.0; // canonicalise negative zero
    char buffer[64]{};
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, std::chars_format::general);
    if (result.ec != std::errc{})
        throw std::invalid_argument("cannot render map number");
    output.append(buffer, result.ptr);
}

void append_integer(std::string &output, const std::int32_t value) {
    char buffer[16]{};
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, 10);
    if (result.ec != std::errc{})
        throw std::invalid_argument("cannot render map integer");
    output.append(buffer, result.ptr);
}

void append_unsigned(std::string &output, const std::uint32_t value) {
    char buffer[16]{};
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, 10);
    if (result.ec != std::errc{})
        throw std::invalid_argument("cannot render map dimension");
    output.append(buffer, result.ptr);
}

void append_vector3(std::string &output, const Vec3 &value) {
    output += "( ";
    append_number(output, value.x);
    output.push_back(' ');
    append_number(output, value.y);
    output.push_back(' ');
    append_number(output, value.z);
    output += " )";
}

void append_metadata(std::string &output, const std::int32_t contents, const std::int32_t surface_flags,
                     const std::int32_t value) {
    output.push_back(' ');
    append_integer(output, contents);
    output.push_back(' ');
    append_integer(output, surface_flags);
    output.push_back(' ');
    append_integer(output, value);
}

void append_plane_points(std::string &output, const MapBrushFace &face) {
    for (std::size_t index = 0; index < face.plane_points.size(); ++index) {
        if (index != 0)
            output.push_back(' ');
        append_vector3(output, face.plane_points[index]);
    }
}

void require_material(const std::string_view material, const bool q3) {
    if (!valid_material(material))
        throw std::invalid_argument("cannot write an invalid or empty map material token");
    if (q3 && starts_with_textures(material))
        throw std::invalid_argument("Quake 3 map material tokens must not include the implicit textures/ prefix");
}

void append_valve220_face(std::string &output, const MapBrushFace &face, const MapDialect dialect) {
    if (!std::holds_alternative<Valve220Projection>(face.projection))
        throw std::invalid_argument("face projection does not match the Valve 220 document dialect");
    if (face.quake3_metadata)
        throw std::invalid_argument("cannot write Quake 3 face metadata in a Valve 220 document");
    if (dialect == MapDialect::quake1_valve220 && face.quake2_metadata)
        throw std::invalid_argument("cannot write a Quake 2 face metadata triplet in a Quake 1 document");
    require_material(face.material, false);
    const Valve220Projection &projection = std::get<Valve220Projection>(face.projection);
    if (projection.s_scale == 0.0 || projection.t_scale == 0.0)
        throw std::invalid_argument("cannot write a zero Valve 220 texture scale");
    append_plane_points(output, face);
    output.push_back(' ');
    output += face.material;
    auto append_axis = [&](const Valve220Axis &axis) {
        output += " [ ";
        append_number(output, axis.axis.x);
        output.push_back(' ');
        append_number(output, axis.axis.y);
        output.push_back(' ');
        append_number(output, axis.axis.z);
        output.push_back(' ');
        append_number(output, axis.shift);
        output += " ]";
    };
    append_axis(projection.s_axis);
    append_axis(projection.t_axis);
    output.push_back(' ');
    append_number(output, projection.rotation);
    output.push_back(' ');
    append_number(output, projection.s_scale);
    output.push_back(' ');
    append_number(output, projection.t_scale);
    if (face.quake2_metadata) {
        append_metadata(output, face.quake2_metadata->contents, face.quake2_metadata->surface_flags,
                        face.quake2_metadata->value);
    }
    output.push_back('\n');
}

void append_q3_legacy_face(std::string &output, const MapBrushFace &face) {
    if (!std::holds_alternative<Quake3LegacyProjection>(face.projection))
        throw std::invalid_argument("face projection does not match the Quake 3 legacy document dialect");
    if (face.quake2_metadata)
        throw std::invalid_argument("cannot write Quake 2 face metadata in a Quake 3 document");
    require_material(face.material, true);
    const Quake3LegacyProjection &projection = std::get<Quake3LegacyProjection>(face.projection);
    if (projection.s_scale == 0.0 || projection.t_scale == 0.0)
        throw std::invalid_argument("cannot write a zero Quake 3 legacy texture scale");
    append_plane_points(output, face);
    output.push_back(' ');
    output += face.material;
    output.push_back(' ');
    append_number(output, projection.s_shift);
    output.push_back(' ');
    append_number(output, projection.t_shift);
    output.push_back(' ');
    append_number(output, projection.rotation);
    output.push_back(' ');
    append_number(output, projection.s_scale);
    output.push_back(' ');
    append_number(output, projection.t_scale);
    if (face.quake3_metadata) {
        append_metadata(output, face.quake3_metadata->contents, face.quake3_metadata->surface_flags,
                        face.quake3_metadata->value);
    }
    output.push_back('\n');
}

void append_q3_brush_primitive_face(std::string &output, const MapBrushFace &face) {
    if (!std::holds_alternative<Quake3BrushPrimitiveProjection>(face.projection))
        throw std::invalid_argument("face projection does not match the Quake 3 brush primitive document dialect");
    if (face.quake2_metadata)
        throw std::invalid_argument("cannot write Quake 2 face metadata in a Quake 3 document");
    require_material(face.material, true);
    append_plane_points(output, face);
    const Quake3BrushPrimitiveProjection &projection = std::get<Quake3BrushPrimitiveProjection>(face.projection);
    output += " (";
    for (const auto &row : projection.matrix) {
        output += " ( ";
        for (std::size_t column = 0; column < row.size(); ++column) {
            if (column != 0)
                output.push_back(' ');
            append_number(output, row[column]);
        }
        output += " )";
    }
    output += " ) ";
    output += face.material;
    if (face.quake3_metadata) {
        append_metadata(output, face.quake3_metadata->contents, face.quake3_metadata->surface_flags,
                        face.quake3_metadata->value);
    }
    output.push_back('\n');
}

void append_brush(std::string &output, const MapBrush &brush, const MapDialect dialect) {
    output += "{\n";
    const bool brush_primitives = dialect == MapDialect::quake3_brush_primitives;
    if (brush_primitives)
        output += "brushDef\n{\n";
    for (const MapBrushFace &face : brush.faces) {
        switch (dialect) {
        case MapDialect::quake1_valve220:
        case MapDialect::quake2_valve220:
            append_valve220_face(output, face, dialect);
            break;
        case MapDialect::quake3_legacy:
            append_q3_legacy_face(output, face);
            break;
        case MapDialect::quake3_brush_primitives:
            append_q3_brush_primitive_face(output, face);
            break;
        }
    }
    output += "}\n";
    if (brush_primitives)
        output += "}\n";
}

void append_patch(std::string &output, const MapPatch &patch, const MapDialect dialect) {
    if (dialect == MapDialect::quake1_valve220 || dialect == MapDialect::quake2_valve220)
        throw std::invalid_argument("cannot write patchDef2 in a Quake 1 or Quake 2 document");
    require_material(patch.material, true);
    if (patch.width < 3 || patch.height < 3 || patch.width > 31 || patch.height > 31 || patch.width % 2 == 0 ||
        patch.height % 2 == 0)
        throw std::invalid_argument("patchDef2 dimensions must be odd values from 3 through 31");
    const std::size_t expected_controls = static_cast<std::size_t>(patch.width) * patch.height;
    if (patch.control_points.size() != expected_controls)
        throw std::invalid_argument("patchDef2 control-point count does not match width times height");

    output += "{\npatchDef2\n{\n";
    output += patch.material;
    output += "\n( ";
    append_unsigned(output, patch.width);
    output.push_back(' ');
    append_unsigned(output, patch.height);
    for (const std::int32_t reserved : patch.reserved) {
        output.push_back(' ');
        append_integer(output, reserved);
    }
    output += " )\n(\n";
    for (std::uint32_t column = 0; column < patch.width; ++column) {
        output.push_back('(');
        for (std::uint32_t row = 0; row < patch.height; ++row) {
            const MapPatchControlPoint &control = patch.control(column, row);
            output += " ( ";
            append_number(output, control.position.x);
            output.push_back(' ');
            append_number(output, control.position.y);
            output.push_back(' ');
            append_number(output, control.position.z);
            output.push_back(' ');
            append_number(output, control.s);
            output.push_back(' ');
            append_number(output, control.t);
            output += " )";
        }
        output += " )\n";
    }
    output += ")\n}\n}\n";
}

[[nodiscard]] std::string render_canonical(const MapDocument &document) {
    std::string output;
    for (const MapEntity &entity : document.entities()) {
        output += "{\n";
        for (const MapEntityProperty &property : entity.properties) {
            if (property.key.empty() || !valid_property_text(property.key) || !valid_property_text(property.value))
                throw std::invalid_argument(
                    "cannot write an empty, quoted, multiline, or control-bearing entity property");
            output.push_back('"');
            output += property.key;
            output += "\" \"";
            output += property.value;
            output += "\"\n";
        }
        for (const MapPrimitive &primitive : entity.primitives) {
            std::visit(
                [&](const auto &value) {
                    using Primitive = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Primitive, MapBrush>)
                        append_brush(output, value, document.dialect());
                    else
                        append_patch(output, value, document.dialect());
                },
                primitive);
        }
        output += "}\n";
    }
    return output;
}

} // namespace

std::string MapDocumentWriter::write(const MapDocument &document, const MapWriteOptions &options) const {
    if (options.validate_document) {
        const MapValidationResult validation = validate_map_document(document, options.validation_options);
        if (!validation.ok()) {
            const MapValidationIssue &issue = validation.issues.front();
            throw std::invalid_argument("map document validation failed [" + issue.code + "]: " + issue.message);
        }
    }
    if (options.mode == MapWriteMode::preserve_pristine_source) {
        if (const std::optional<std::string_view> source = document.pristine_source())
            return std::string(*source);
    }
    return render_canonical(document);
}

} // namespace qmg
