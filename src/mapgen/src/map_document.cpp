#include "qmg/map_document.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace qmg {
namespace {

struct Plane {
    Vec3 normal;
    double distance{};
    MapNodeId face_id{};
};

[[nodiscard]] Vec3 subtract(const Vec3 &left, const Vec3 &right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 add(const Vec3 &left, const Vec3 &right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3 scale(const Vec3 &value, const double factor) {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] double dot(const Vec3 &left, const Vec3 &right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Vec3 cross(const Vec3 &left, const Vec3 &right) {
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

[[nodiscard]] double length(const Vec3 &value) { return std::sqrt(dot(value, value)); }

[[nodiscard]] bool finite(const double value, const double maximum) {
    return std::isfinite(value) && std::abs(value) <= maximum;
}

[[nodiscard]] bool finite(const Vec3 &value, const double maximum) {
    return finite(value.x, maximum) && finite(value.y, maximum) && finite(value.z, maximum);
}

[[nodiscard]] std::optional<Plane> plane_for_face(const MapBrushFace &face, const double epsilon) {
    // This is the point order used by the Quake compilers: (p2 - p0) x (p1 - p0).
    Vec3 normal = cross(subtract(face.plane_points[2], face.plane_points[0]),
                        subtract(face.plane_points[1], face.plane_points[0]));
    const double normal_length = length(normal);
    if (!std::isfinite(normal_length) || normal_length <= epsilon)
        return std::nullopt;
    normal = scale(normal, 1.0 / normal_length);
    return Plane{normal, dot(face.plane_points[0], normal), face.id};
}

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
        const auto character = static_cast<unsigned char>(material[index]);
        if (static_cast<char>(std::tolower(character)) != prefix[index])
            return false;
    }
    return true;
}

[[nodiscard]] std::vector<Vec3> clip_winding(const std::vector<Vec3> &input, const Plane &clip_plane,
                                             const double epsilon) {
    std::vector<Vec3> output;
    if (input.empty())
        return output;
    output.reserve(input.size() + 1);
    Vec3 previous = input.back();
    double previous_distance = dot(clip_plane.normal, previous) - clip_plane.distance;
    bool previous_inside = previous_distance <= epsilon;
    for (const Vec3 &current : input) {
        const double current_distance = dot(clip_plane.normal, current) - clip_plane.distance;
        const bool current_inside = current_distance <= epsilon;
        if (previous_inside != current_inside) {
            const double denominator = previous_distance - current_distance;
            if (std::abs(denominator) > std::numeric_limits<double>::epsilon()) {
                const double fraction = previous_distance / denominator;
                output.push_back(add(previous, scale(subtract(current, previous), fraction)));
            }
        }
        if (current_inside)
            output.push_back(current);
        previous = current;
        previous_distance = current_distance;
        previous_inside = current_inside;
    }
    return output;
}

[[nodiscard]] bool winding_has_area(const std::vector<Vec3> &winding, const double epsilon) {
    if (winding.size() < 3)
        return false;
    for (std::size_t index = 2; index < winding.size(); ++index) {
        const Vec3 area = cross(subtract(winding[index - 1], winding[0]), subtract(winding[index], winding[0]));
        if (length(area) > epsilon)
            return true;
    }
    return false;
}

template <typename AddIssue>
void validate_closed_brush(const MapBrush &brush, const MapValidationOptions &options, AddIssue &&add_issue) {
    if (brush.faces.size() < 4) {
        add_issue("brush_too_few_faces", brush.id, "a closed convex brush requires at least four faces");
        return;
    }
    // Geometry work is deliberately bounded independently of parser limits.
    constexpr std::size_t max_geometry_faces = 512;
    if (brush.faces.size() > max_geometry_faces) {
        add_issue("brush_geometry_limit", brush.id, "brush has too many faces for bounded convexity validation");
        return;
    }

    std::vector<Plane> planes;
    planes.reserve(brush.faces.size());
    double largest_distance = 0.0;
    for (const MapBrushFace &face : brush.faces) {
        const std::optional<Plane> plane = plane_for_face(face, options.plane_epsilon);
        if (!plane) {
            add_issue("degenerate_plane", face.id, "brush face plane points are collinear or coincident");
            continue;
        }
        largest_distance = std::max(largest_distance, std::abs(plane->distance));
        planes.push_back(*plane);
    }
    if (planes.size() != brush.faces.size())
        return;

    for (std::size_t left = 0; left < planes.size(); ++left) {
        for (std::size_t right = left + 1; right < planes.size(); ++right) {
            if (dot(planes[left].normal, planes[right].normal) > 1.0 - options.plane_epsilon &&
                std::abs(planes[left].distance - planes[right].distance) <= options.point_epsilon) {
                add_issue("duplicate_plane", planes[right].face_id, "brush contains duplicate coplanar faces");
            }
        }
    }

    const double radius = std::max(1024.0, largest_distance * 16.0 + 1024.0);
    for (std::size_t face_index = 0; face_index < planes.size(); ++face_index) {
        const Plane &plane = planes[face_index];
        const Vec3 helper = std::abs(plane.normal.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{0.0, 1.0, 0.0};
        Vec3 basis_s = cross(helper, plane.normal);
        basis_s = scale(basis_s, 1.0 / length(basis_s));
        const Vec3 basis_t = cross(plane.normal, basis_s);
        const Vec3 origin = scale(plane.normal, plane.distance);
        std::vector<Vec3> winding{
            add(origin, add(scale(basis_s, -radius), scale(basis_t, -radius))),
            add(origin, add(scale(basis_s, radius), scale(basis_t, -radius))),
            add(origin, add(scale(basis_s, radius), scale(basis_t, radius))),
            add(origin, add(scale(basis_s, -radius), scale(basis_t, radius))),
        };
        for (std::size_t clip_index = 0; clip_index < planes.size() && !winding.empty(); ++clip_index) {
            if (clip_index != face_index)
                winding = clip_winding(winding, planes[clip_index], options.point_epsilon);
        }
        if (!winding_has_area(winding, options.point_epsilon)) {
            add_issue("empty_brush_face", plane.face_id,
                      "brush face does not bound a non-degenerate polygon in the common convex volume");
            continue;
        }
        const bool touches_artificial_boundary = std::any_of(winding.begin(), winding.end(), [&](const Vec3 &point) {
            const Vec3 local = subtract(point, origin);
            return std::abs(dot(local, basis_s)) >= radius * (1.0 - 1.0e-7) ||
                   std::abs(dot(local, basis_t)) >= radius * (1.0 - 1.0e-7);
        });
        if (touches_artificial_boundary) {
            add_issue("unbounded_brush", brush.id, "brush half-spaces do not enclose a bounded convex volume");
            return;
        }
    }
}

} // namespace

const MapPatchControlPoint &MapPatch::control(const std::size_t column, const std::size_t row) const {
    if (column >= width || row >= height)
        throw std::out_of_range("patch control-point coordinate is outside the declared dimensions");
    return control_points.at(column * static_cast<std::size_t>(height) + row);
}

MapPatchControlPoint &MapPatch::control(const std::size_t column, const std::size_t row) {
    if (column >= width || row >= height)
        throw std::out_of_range("patch control-point coordinate is outside the declared dimensions");
    return control_points.at(column * static_cast<std::size_t>(height) + row);
}

std::optional<std::string_view> MapEntity::first_value(const std::string_view key) const {
    const auto property = std::find_if(properties.begin(), properties.end(),
                                       [key](const MapEntityProperty &candidate) { return candidate.key == key; });
    if (property == properties.end())
        return std::nullopt;
    return property->value;
}

std::optional<std::string_view> MapEntity::last_value(const std::string_view key) const {
    const auto property = std::find_if(properties.rbegin(), properties.rend(),
                                       [key](const MapEntityProperty &candidate) { return candidate.key == key; });
    if (property == properties.rend())
        return std::nullopt;
    return property->value;
}

MapDocument::MapDocument(const MapDialect dialect) : dialect_(dialect) {}

void MapDocument::set_dialect(const MapDialect dialect) {
    if (dialect_ == dialect)
        return;
    clear_pristine_source();
    dialect_ = dialect;
}

std::vector<MapEntity> &MapDocument::edit_entities() noexcept {
    clear_pristine_source();
    return entities_;
}

void MapDocument::assign_deterministic_node_ids() {
    clear_pristine_source();
    MapNodeId next_id = 1;
    for (MapEntity &entity : entities_) {
        entity.id = next_id++;
        for (MapEntityProperty &property : entity.properties)
            property.id = next_id++;
        for (MapPrimitive &primitive : entity.primitives) {
            std::visit(
                [&](auto &value) {
                    value.id = next_id++;
                    using Primitive = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<Primitive, MapBrush>) {
                        for (MapBrushFace &face : value.faces)
                            face.id = next_id++;
                    } else {
                        for (MapPatchControlPoint &control : value.control_points)
                            control.id = next_id++;
                    }
                },
                primitive);
        }
    }
}

std::optional<std::string_view> MapDocument::pristine_source() const noexcept {
    if (!pristine_source_)
        return std::nullopt;
    return *pristine_source_;
}

void MapDocument::clear_pristine_source() noexcept { pristine_source_.reset(); }

void MapDocument::adopt_pristine_source(std::string source) { pristine_source_ = std::move(source); }

std::array<Vec3, 3> order_face_points_outward(std::array<Vec3, 3> plane_points, const Vec3 &interior_point,
                                              const double epsilon) {
    if (!std::isfinite(epsilon) || epsilon <= 0.0)
        throw std::invalid_argument("face orientation epsilon must be finite and positive");
    constexpr double maximum = std::numeric_limits<double>::max();
    if (!finite(interior_point, maximum) ||
        std::any_of(plane_points.begin(), plane_points.end(),
                    [](const Vec3 &point) { return !finite(point, std::numeric_limits<double>::max()); })) {
        throw std::invalid_argument("cannot orient a face with non-finite input points");
    }

    const Vec3 normal = cross(subtract(plane_points[2], plane_points[0]), subtract(plane_points[1], plane_points[0]));
    const double normal_length = length(normal);
    if (!std::isfinite(normal_length) || normal_length <= epsilon)
        throw std::invalid_argument("cannot orient a face whose plane points are degenerate");
    const Vec3 relative_interior = subtract(interior_point, plane_points[0]);
    if (!finite(relative_interior, maximum))
        throw std::invalid_argument("cannot orient a face whose coordinate differences overflow");
    const double signed_distance = dot(scale(normal, 1.0 / normal_length), relative_interior);
    if (!std::isfinite(signed_distance) || std::abs(signed_distance) <= epsilon)
        throw std::invalid_argument("interior point must lie strictly away from the face plane");
    if (signed_distance > 0.0)
        std::swap(plane_points[1], plane_points[2]);
    return plane_points;
}

MapValidationResult validate_map_document(const MapDocument &document, const MapValidationOptions &options) {
    MapValidationResult result;
    auto add_issue = [&](std::string code, const MapNodeId node_id, std::string message) {
        if (result.issues.size() < std::max<std::size_t>(1, options.max_issues))
            result.issues.push_back({std::move(code), node_id, std::move(message)});
    };

    if (!std::isfinite(options.plane_epsilon) || options.plane_epsilon <= 0.0 ||
        !std::isfinite(options.point_epsilon) || options.point_epsilon <= 0.0 ||
        !std::isfinite(options.max_coordinate) || options.max_coordinate <= 0.0) {
        add_issue("invalid_validation_options", 0,
                  "validation tolerances and coordinate bound must be finite and positive");
        return result;
    }

    if (document.entities().empty()) {
        add_issue("empty_document", 0, "map document has no entities");
        return result;
    }
    if (options.require_worldspawn) {
        const std::optional<std::string_view> classname = document.entities().front().first_value("classname");
        if (!classname || *classname != "worldspawn")
            add_issue("missing_worldspawn", document.entities().front().id,
                      "the first entity must have classname worldspawn");
    }
    if (document.dialect() == MapDialect::quake1_valve220 || document.dialect() == MapDialect::quake2_valve220) {
        bool has_mapversion = false;
        for (const MapEntityProperty &property : document.entities().front().properties)
            has_mapversion = has_mapversion || (property.key == "mapversion" && property.value == "220");
        if (!has_mapversion)
            add_issue("missing_mapversion", document.entities().front().id,
                      "Valve 220 worldspawn must contain a mapversion property with value 220");
    }

    const bool generated_profile = options.profile == MapValidationProfile::generated_document;
    if (generated_profile) {
        std::size_t worldspawn_entities = 0;
        std::size_t mapversion_properties = 0;
        std::size_t mapversion_entity_index = 0;
        std::string_view mapversion_value;
        MapNodeId mapversion_id = 0;
        bool first_is_worldspawn = false;
        bool world_has_brush = false;
        for (std::size_t entity_index = 0; entity_index < document.entities().size(); ++entity_index) {
            const MapEntity &entity = document.entities()[entity_index];
            std::unordered_set<std::string_view> keys;
            std::size_t classname_properties = 0;
            std::string_view classname;
            bool entity_is_worldspawn = false;
            for (const MapEntityProperty &property : entity.properties) {
                if (!keys.insert(property.key).second)
                    add_issue("generated_duplicate_entity_key", property.id,
                              "generated entities must not contain duplicate property keys");
                if (property.key == "classname") {
                    ++classname_properties;
                    classname = property.value;
                    entity_is_worldspawn = entity_is_worldspawn || property.value == "worldspawn";
                }
                if (property.key == "mapversion") {
                    ++mapversion_properties;
                    mapversion_entity_index = entity_index;
                    mapversion_value = property.value;
                    mapversion_id = property.id;
                }
            }
            if (classname_properties != 1)
                add_issue("generated_classname_count", entity.id,
                          "every generated entity must contain exactly one classname property");
            else if (classname.empty())
                add_issue("generated_classname_value", entity.id,
                          "generated entity classname values must not be empty");
            if (entity_is_worldspawn)
                ++worldspawn_entities;
            if (entity_index == 0)
                first_is_worldspawn = classname_properties == 1 && classname == "worldspawn";
        }
        for (const MapPrimitive &primitive : document.entities().front().primitives)
            world_has_brush = world_has_brush || std::holds_alternative<MapBrush>(primitive);
        if (!first_is_worldspawn || worldspawn_entities != 1)
            add_issue("generated_worldspawn", document.entities().front().id,
                      "generated documents require exactly one worldspawn and it must be the first entity");
        if (!world_has_brush)
            add_issue("generated_world_geometry", document.entities().front().id,
                      "generated worldspawn must contain at least one structural brush");
        if (document.dialect() == MapDialect::quake1_valve220 || document.dialect() == MapDialect::quake2_valve220) {
            if (mapversion_properties != 1 || mapversion_entity_index != 0 || mapversion_value != "220")
                add_issue("generated_mapversion", mapversion_id,
                          "generated Valve 220 documents require exactly one worldspawn mapversion of 220");
        } else if (mapversion_properties != 0) {
            add_issue("generated_mapversion", mapversion_id,
                      "generated Quake 3 documents must not contain a mapversion property");
        }
    }

    std::unordered_set<MapNodeId> ids;
    auto validate_id = [&](const MapNodeId id) {
        if (id == 0)
            add_issue("missing_node_id", id, "map node has no deterministic identifier");
        else if (!ids.insert(id).second)
            add_issue("duplicate_node_id", id, "map node identifier is not unique");
    };
    bool saw_q3_legacy = false;
    bool saw_q3_brush_primitives = false;

    for (const MapEntity &entity : document.entities()) {
        validate_id(entity.id);
        for (const MapEntityProperty &property : entity.properties) {
            validate_id(property.id);
            if (property.key.empty() || !valid_property_text(property.key) || !valid_property_text(property.value))
                add_issue("invalid_entity_property", property.id,
                          "entity property keys must be non-empty and property text cannot contain quotes or controls");
        }
        for (const MapPrimitive &primitive : entity.primitives) {
            std::visit(
                [&](const auto &value) {
                    using Primitive = std::decay_t<decltype(value)>;
                    validate_id(value.id);
                    if constexpr (std::is_same_v<Primitive, MapBrush>) {
                        for (const MapBrushFace &face : value.faces) {
                            validate_id(face.id);
                            if (!valid_material(face.material))
                                add_issue("invalid_material", face.id, "face material is not a valid map token");
                            for (const Vec3 &point : face.plane_points) {
                                if (!finite(point, options.max_coordinate))
                                    add_issue("invalid_plane_point", face.id,
                                              "face plane point is non-finite or outside the coordinate bound");
                            }

                            if (std::holds_alternative<Valve220Projection>(face.projection)) {
                                const Valve220Projection &projection = std::get<Valve220Projection>(face.projection);
                                if (!finite(projection.s_axis.axis, options.max_coordinate) ||
                                    !finite(projection.t_axis.axis, options.max_coordinate) ||
                                    !finite(projection.s_axis.shift, options.max_coordinate) ||
                                    !finite(projection.t_axis.shift, options.max_coordinate) ||
                                    !finite(projection.rotation, options.max_coordinate) ||
                                    !finite(projection.s_scale, options.max_coordinate) ||
                                    !finite(projection.t_scale, options.max_coordinate) ||
                                    std::abs(projection.s_scale) <= options.plane_epsilon ||
                                    std::abs(projection.t_scale) <= options.plane_epsilon) {
                                    add_issue("invalid_valve220_projection", face.id,
                                              "Valve 220 projection must be finite and have non-zero scales");
                                }
                                const std::optional<Plane> face_plane = plane_for_face(face, options.plane_epsilon);
                                const Vec3 texture_normal = cross(projection.s_axis.axis, projection.t_axis.axis);
                                const double texture_normal_length = length(texture_normal);
                                if (face_plane && (!std::isfinite(texture_normal_length) ||
                                                   texture_normal_length <= options.plane_epsilon ||
                                                   std::abs(dot(scale(texture_normal, 1.0 / texture_normal_length),
                                                                face_plane->normal)) <= options.plane_epsilon)) {
                                    add_issue("invalid_valve220_projection", face.id,
                                              "Valve 220 texture axes do not span this face plane");
                                }
                                if (document.dialect() == MapDialect::quake3_legacy ||
                                    document.dialect() == MapDialect::quake3_brush_primitives)
                                    add_issue("projection_dialect_mismatch", face.id,
                                              "Valve 220 projection is not valid in this Quake 3 dialect");
                            } else if (std::holds_alternative<Quake3LegacyProjection>(face.projection)) {
                                saw_q3_legacy = true;
                                const Quake3LegacyProjection &projection =
                                    std::get<Quake3LegacyProjection>(face.projection);
                                if (!finite(projection.s_shift, options.max_coordinate) ||
                                    !finite(projection.t_shift, options.max_coordinate) ||
                                    !finite(projection.rotation, options.max_coordinate) ||
                                    !finite(projection.s_scale, options.max_coordinate) ||
                                    !finite(projection.t_scale, options.max_coordinate) ||
                                    std::abs(projection.s_scale) <= options.plane_epsilon ||
                                    std::abs(projection.t_scale) <= options.plane_epsilon) {
                                    add_issue("invalid_legacy_projection", face.id,
                                              "legacy projection must be finite and have non-zero scales");
                                }
                                if (document.dialect() != MapDialect::quake3_legacy)
                                    add_issue("projection_dialect_mismatch", face.id,
                                              "Quake 3 legacy projection does not match the document dialect");
                            } else {
                                saw_q3_brush_primitives = true;
                                const Quake3BrushPrimitiveProjection &projection =
                                    std::get<Quake3BrushPrimitiveProjection>(face.projection);
                                bool projection_finite = true;
                                for (const auto &row : projection.matrix) {
                                    for (const double component : row)
                                        if (!finite(component, options.max_coordinate))
                                            projection_finite = false;
                                }
                                if (!projection_finite) {
                                    add_issue("invalid_brush_primitive_projection", face.id,
                                              "brush primitive matrix contains an invalid number");
                                } else {
                                    const auto &s_row = projection.matrix[0];
                                    const auto &t_row = projection.matrix[1];
                                    const Vec3 s_vector{s_row[0], s_row[1], s_row[2]};
                                    const Vec3 t_vector{t_row[0], t_row[1], t_row[2]};
                                    const double row_scale = length(s_vector) * length(t_vector);
                                    const double row_rank = length(cross(s_vector, t_vector));
                                    const double s_linear_length = std::hypot(s_row[0], s_row[1]);
                                    const double t_linear_length = std::hypot(t_row[0], t_row[1]);
                                    const double linear_scale = s_linear_length * t_linear_length;
                                    const double determinant = s_row[0] * t_row[1] - s_row[1] * t_row[0];
                                    if (!std::isfinite(row_scale) || !std::isfinite(row_rank) || row_scale == 0.0 ||
                                        row_rank <= options.plane_epsilon * row_scale || !std::isfinite(linear_scale) ||
                                        linear_scale == 0.0 ||
                                        std::abs(determinant) <= options.plane_epsilon * linear_scale) {
                                        add_issue("invalid_brush_primitive_projection", face.id,
                                                  "brush primitive matrix must have independent rows and a "
                                                  "non-singular linear transform");
                                    }
                                }
                                if (document.dialect() != MapDialect::quake3_brush_primitives)
                                    add_issue("projection_dialect_mismatch", face.id,
                                              "Quake 3 brush primitive projection does not match the document dialect");
                            }

                            if (document.dialect() == MapDialect::quake1_valve220 &&
                                (face.quake2_metadata || face.quake3_metadata))
                                add_issue("metadata_dialect_mismatch", face.id,
                                          "Quake 1 Valve 220 faces cannot contain trailing face metadata");
                            if (document.dialect() == MapDialect::quake2_valve220 && face.quake3_metadata)
                                add_issue("metadata_dialect_mismatch", face.id,
                                          "Quake 2 faces cannot contain Quake 3 metadata");
                            if ((document.dialect() == MapDialect::quake3_legacy ||
                                 document.dialect() == MapDialect::quake3_brush_primitives) &&
                                face.quake2_metadata)
                                add_issue("metadata_dialect_mismatch", face.id,
                                          "Quake 3 faces cannot contain Quake 2 metadata");
                            if ((document.dialect() == MapDialect::quake3_legacy ||
                                 document.dialect() == MapDialect::quake3_brush_primitives) &&
                                starts_with_textures(face.material))
                                add_issue("q3_material_prefix", face.id,
                                          "Quake 3 source material names must be relative to textures/");
                        }
                        if (generated_profile && (document.dialect() == MapDialect::quake3_legacy ||
                                                  document.dialect() == MapDialect::quake3_brush_primitives)) {
                            bool missing_metadata = false;
                            std::optional<bool> detail_state;
                            bool inconsistent_detail = false;
                            for (const MapBrushFace &face : value.faces) {
                                if (!face.quake3_metadata) {
                                    missing_metadata = true;
                                    continue;
                                }
                                const bool detail = (face.quake3_metadata->contents & kMapDetailContentsBit) != 0;
                                if (detail_state && *detail_state != detail)
                                    inconsistent_detail = true;
                                else if (!detail_state)
                                    detail_state = detail;
                            }
                            if (missing_metadata)
                                add_issue("generated_q3_face_metadata", value.id,
                                          "every generated Quake 3 brush face requires an explicit c/s/v triplet");
                            if (inconsistent_detail)
                                add_issue("generated_detail_contents", value.id,
                                          "C_DETAIL must be consistent across every face of a generated Quake 3 brush");
                        }
                        if (generated_profile && document.dialect() == MapDialect::quake2_valve220) {
                            const bool uses_explicit_detail =
                                std::any_of(value.faces.begin(), value.faces.end(), [](const MapBrushFace &face) {
                                    return face.quake2_metadata &&
                                           (face.quake2_metadata->contents & kMapDetailContentsBit) != 0;
                                });
                            if (uses_explicit_detail) {
                                const bool consistent_detail =
                                    std::all_of(value.faces.begin(), value.faces.end(), [](const MapBrushFace &face) {
                                        return face.quake2_metadata &&
                                               (face.quake2_metadata->contents & kMapDetailContentsBit) != 0;
                                    });
                                if (!consistent_detail)
                                    add_issue(
                                        "generated_detail_contents", value.id,
                                        "explicit Q2 detail metadata must mark every face of its generated brush");
                            }
                        }
                        if (options.require_closed_convex_brushes)
                            validate_closed_brush(value, options, add_issue);
                    } else {
                        if (document.dialect() == MapDialect::quake1_valve220 ||
                            document.dialect() == MapDialect::quake2_valve220)
                            add_issue("patch_dialect_mismatch", value.id, "patchDef2 is only valid in Quake 3 maps");
                        if (!valid_material(value.material))
                            add_issue("invalid_material", value.id, "patch material is not a valid map token");
                        if (starts_with_textures(value.material))
                            add_issue("q3_material_prefix", value.id,
                                      "Quake 3 source material names must be relative to textures/");
                        if (value.width < 3 || value.height < 3 || value.width > 31 || value.height > 31 ||
                            value.width % 2 == 0 || value.height % 2 == 0)
                            add_issue("invalid_patch_dimensions", value.id,
                                      "portable patchDef2 dimensions must be odd values from 3 through 31");
                        const std::size_t expected = static_cast<std::size_t>(value.width) * value.height;
                        if (value.control_points.size() != expected)
                            add_issue("patch_control_count", value.id,
                                      "patch control-point count does not match width times height");
                        for (const MapPatchControlPoint &control : value.control_points) {
                            validate_id(control.id);
                            if (!finite(control.position, options.max_coordinate) ||
                                !finite(control.s, options.max_coordinate) ||
                                !finite(control.t, options.max_coordinate))
                                add_issue("invalid_patch_control", control.id,
                                          "patch control point contains a non-finite or out-of-bounds number");
                        }
                    }
                },
                primitive);
        }
    }

    if (saw_q3_legacy && saw_q3_brush_primitives)
        add_issue("q3_mixed_brush_formats", 0,
                  "a Quake 3 map document cannot mix legacy and brush primitive brush projections");
    return result;
}

std::string to_string(const MapDialect dialect) {
    switch (dialect) {
    case MapDialect::quake1_valve220:
        return "quake1_valve220";
    case MapDialect::quake2_valve220:
        return "quake2_valve220";
    case MapDialect::quake3_legacy:
        return "quake3_legacy";
    case MapDialect::quake3_brush_primitives:
        return "quake3_brush_primitives";
    }
    throw std::invalid_argument("unknown map dialect");
}

} // namespace qmg
