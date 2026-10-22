#include "qmg/map_builder.hpp"
#include "qmg/generated/game_profile_tables.hpp"
#include "qmg/quake2_mechanisms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace qmg {
namespace {

enum class Axis : std::uint8_t { x, y, z };

struct FaceKey {
    Axis axis{Axis::x};
    int sign{};
    GridCoord plane{};

    auto operator<=>(const FaceKey &) const = default;
};

struct Rect {
    GridCoord u0{};
    GridCoord u1{};
    GridCoord v0{};
    GridCoord v1{};
};

struct FaceCandidate {
    Rect rect;
    MapBuildSourceRef owner;
};

struct FreeVolume {
    GridBounds bounds;
    std::vector<MapBuildSourceRef> sources;
};

struct BoundaryCell {
    std::size_t u{};
    std::size_t v{};
    MapBuildSourceRef owner;
};

struct BoundaryRect {
    FaceKey face;
    Rect rect;
    MapBuildSourceRef owner;
    MaterialRole material{MaterialRole::wall};
};

struct TaggedBrush {
    MapBrush brush;
    MapBuildSourceRef source;
};

struct TaggedPatch {
    MapPatch patch;
    MapBuildSourceRef source;
};

struct NativeMoverProperties {
    std::string speed;
    std::string wait;
    std::optional<std::string> angle;
    std::optional<std::string> lip;
    std::optional<std::string> height;
    GridVec3 brush_translation;
};

struct PendingEntity {
    MapEntity entity;
    std::optional<MapBuildSourceRef> entity_source;
    std::vector<std::optional<MapBuildSourceRef>> primitive_sources;
};

[[noreturn]] void fail(std::string code, std::string message) {
    throw MapBuildError(std::move(code), std::move(message));
}

[[nodiscard]] MapBuildSourceRef source_ref(const RoomId id) { return {MapBuildSourceKind::room, id.value}; }

[[nodiscard]] MapBuildSourceRef source_ref(const ConnectionId id) { return {MapBuildSourceKind::connection, id.value}; }

[[nodiscard]] MapBuildSourceRef source_ref(const SolidId id) { return {MapBuildSourceKind::solid, id.value}; }

[[nodiscard]] MapBuildSourceRef source_ref(const PlacementId id) { return {MapBuildSourceKind::placement, id.value}; }

[[nodiscard]] GridCoord component(const GridVec3 value, const Axis axis) {
    switch (axis) {
    case Axis::x:
        return value.x;
    case Axis::y:
        return value.y;
    case Axis::z:
        return value.z;
    }
    fail("invalid_axis", "internal axis value is invalid");
}

[[nodiscard]] GridCoord minimum(const GridBounds &bounds, const Axis axis) { return component(bounds.min, axis); }

[[nodiscard]] GridCoord maximum(const GridBounds &bounds, const Axis axis) { return component(bounds.max, axis); }

void set_component(GridVec3 &value, const Axis axis, const GridCoord component_value) {
    switch (axis) {
    case Axis::x:
        value.x = component_value;
        return;
    case Axis::y:
        value.y = component_value;
        return;
    case Axis::z:
        value.z = component_value;
        return;
    }
    fail("invalid_axis", "internal axis value is invalid");
}

[[nodiscard]] std::array<Axis, 2> tangent_axes(const Axis normal) {
    switch (normal) {
    case Axis::x:
        return {Axis::y, Axis::z};
    case Axis::y:
        return {Axis::x, Axis::z};
    case Axis::z:
        return {Axis::x, Axis::y};
    }
    fail("invalid_axis", "internal axis value is invalid");
}

[[nodiscard]] Rect projected_rect(const GridBounds &bounds, const Axis normal) {
    const auto tangents = tangent_axes(normal);
    return {minimum(bounds, tangents[0]), maximum(bounds, tangents[0]), minimum(bounds, tangents[1]),
            maximum(bounds, tangents[1])};
}

[[nodiscard]] bool rect_contains(const Rect &outer, const GridCoord u0, const GridCoord u1, const GridCoord v0,
                                 const GridCoord v1) {
    return outer.u0 <= u0 && outer.u1 >= u1 && outer.v0 <= v0 && outer.v1 >= v1;
}

[[nodiscard]] bool positive_overlap(const GridCoord a0, const GridCoord a1, const GridCoord b0, const GridCoord b1) {
    return std::max(a0, b0) < std::min(a1, b1);
}

[[nodiscard]] bool rects_overlap(const Rect &left, const Rect &right) {
    return positive_overlap(left.u0, left.u1, right.u0, right.u1) &&
           positive_overlap(left.v0, left.v1, right.v0, right.v1);
}

[[nodiscard]] bool within_coordinate_limit(const GridCoord value, const GridCoord limit) {
    const std::int64_t widened = value;
    const std::int64_t magnitude = widened < 0 ? -widened : widened;
    return magnitude <= limit;
}

[[nodiscard]] bool within_coordinate_limit(const GridBounds &bounds, const GridCoord limit) {
    return bounds.valid() && within_coordinate_limit(bounds.min.x, limit) &&
           within_coordinate_limit(bounds.min.y, limit) && within_coordinate_limit(bounds.min.z, limit) &&
           within_coordinate_limit(bounds.max.x, limit) && within_coordinate_limit(bounds.max.y, limit) &&
           within_coordinate_limit(bounds.max.z, limit);
}

[[nodiscard]] bool bounds_less(const GridBounds &left, const GridBounds &right) {
    return std::tie(left.min.x, left.min.y, left.min.z, left.max.x, left.max.y, left.max.z) <
           std::tie(right.min.x, right.min.y, right.min.z, right.max.x, right.max.y, right.max.z);
}

[[nodiscard]] bool points_less(const PlanePointTriple &left, const PlanePointTriple &right) {
    return std::tie(left.first.x, left.first.y, left.first.z, left.second.x, left.second.y, left.second.z, left.third.x,
                    left.third.y, left.third.z) < std::tie(right.first.x, right.first.y, right.first.z, right.second.x,
                                                           right.second.y, right.second.z, right.third.x, right.third.y,
                                                           right.third.z);
}

template <typename Item, typename Projection>
void require_unique_nonzero_ids(const std::vector<const Item *> &items, Projection projection,
                                const std::string_view kind) {
    std::uint32_t previous{};
    bool have_previous = false;
    for (const Item *item : items) {
        const std::uint32_t id = projection(*item).value;
        if (id == 0)
            fail("invalid_layout_id", std::string(kind) + " has the reserved zero identifier");
        if (have_previous && id == previous)
            fail("duplicate_layout_id", std::string(kind) + " identifiers must be unique");
        previous = id;
        have_previous = true;
    }
}

[[nodiscard]] bool dialect_matches_game(const Game game, const MapDialect dialect) {
    switch (game) {
    case Game::quake1:
        return dialect == MapDialect::quake1_valve220;
    case Game::quake2:
        return dialect == MapDialect::quake2_valve220;
    case Game::quake3:
        return dialect == MapDialect::quake3_legacy || dialect == MapDialect::quake3_brush_primitives;
    }
    return false;
}

void validate_options(const MapBuildOptions &options) {
    if (options.shell_thickness <= 0)
        fail("invalid_build_options", "shell thickness must be positive");
    if (options.max_coordinate <= 0)
        fail("invalid_build_options", "coordinate limit must be positive");
    if (options.max_face_cells == 0 || options.max_shell_brushes == 0 || options.max_total_brushes == 0)
        fail("invalid_build_options", "lowering caps must be nonzero");
    if (options.max_shell_brushes > options.max_total_brushes)
        fail("invalid_build_options", "shell brush cap cannot exceed the total brush cap");
}

[[nodiscard]] std::int32_t contents_bits(const BrushContents contents) {
    switch (contents) {
    case BrushContents::solid:
        return 0;
    case BrushContents::water:
        return 32;
    case BrushContents::slime:
        return 16;
    case BrushContents::lava:
        return 8;
    }
    fail("invalid_brush_contents", "layout brush has an invalid contents role");
}

// Material names come from data/profiles/<game>/generator-current.json through the generated
// tables header; an empty entry is a role or contents the game cannot texture.
[[nodiscard]] std::string solid_material(const Game game, const MaterialRole role) {
    const std::string_view material = game_profile::solid_material(game, role);
    if (material.empty())
        fail("invalid_material_role", "solid brush cannot use the requested material role");
    return std::string(material);
}

[[nodiscard]] std::string liquid_material(const Game game, const BrushContents contents) {
    if (contents == BrushContents::solid)
        fail("invalid_liquid_contract", "liquid material requires non-solid contents");
    const std::string_view material = game_profile::liquid_material(game, contents);
    if (material.empty())
        fail("invalid_brush_contents", "cannot choose a material for invalid liquid contents");
    return std::string(material);
}

[[nodiscard]] std::string material_for(const Game game, const MaterialRole role, const BrushContents contents) {
    if (contents != BrushContents::solid) {
        if (role != MaterialRole::liquid)
            fail("invalid_liquid_contract", "non-solid contents require the liquid material role");
        return liquid_material(game, contents);
    }
    if (role == MaterialRole::liquid)
        fail("invalid_liquid_contract", "liquid material role requires non-solid contents");
    return solid_material(game, role);
}

[[nodiscard]] MapPatch lower_curve_patch(const LayoutCurve &curve) {
    MapPatch patch;
    patch.material = material_for(Game::quake3, curve.material, BrushContents::solid);
    patch.width = curve.width;
    patch.height = curve.height;
    patch.control_points.reserve(curve.control_points.size());
    for (const LayoutCurveControlPoint &control : curve.control_points) {
        patch.control_points.push_back({0, to_vec3(control.position), static_cast<double>(control.u_milli) / 1000.0,
                                        static_cast<double>(control.v_milli) / 1000.0});
    }
    return patch;
}

[[nodiscard]] Vec3 subtract(const Vec3 &left, const Vec3 &right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 cross(const Vec3 &left, const Vec3 &right) {
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

[[nodiscard]] Vec3 quake_normal(const std::array<Vec3, 3> &points) {
    return cross(subtract(points[2], points[0]), subtract(points[1], points[0]));
}

[[nodiscard]] MapTextureProjection projection_for(const MapDialect dialect, const Vec3 normal) {
    switch (dialect) {
    case MapDialect::quake1_valve220:
    case MapDialect::quake2_valve220: {
        Valve220Projection projection;
        const double x = std::abs(normal.x);
        const double y = std::abs(normal.y);
        const double z = std::abs(normal.z);
        if (z >= x && z >= y) {
            projection.s_axis.axis = {1.0, 0.0, 0.0};
            projection.t_axis.axis = {0.0, -1.0, 0.0};
        } else if (x >= y) {
            projection.s_axis.axis = {0.0, 1.0, 0.0};
            projection.t_axis.axis = {0.0, 0.0, -1.0};
        } else {
            projection.s_axis.axis = {1.0, 0.0, 0.0};
            projection.t_axis.axis = {0.0, 0.0, -1.0};
        }
        return projection;
    }
    case MapDialect::quake3_legacy:
        return Quake3LegacyProjection{};
    case MapDialect::quake3_brush_primitives: {
        constexpr double scale = 1.0 / 64.0;
        Quake3BrushPrimitiveProjection projection;
        projection.matrix = {{{{scale, 0.0, 0.0}}, {{0.0, scale, 0.0}}}};
        return projection;
    }
    }
    fail("invalid_dialect", "cannot create a projection for an invalid map dialect");
}

[[nodiscard]] MapBrushFace make_face(const PlanePointTriple &plane, const Vec3 &interior_witness,
                                     const MaterialRole role, const BrushContents contents, const bool detail,
                                     const Game game, const MapDialect dialect) {
    std::array<Vec3, 3> points{to_vec3(plane.first), to_vec3(plane.second), to_vec3(plane.third)};
    points = order_face_points_outward(points, interior_witness);
    MapBrushFace face;
    face.plane_points = points;
    face.material = material_for(game, role, contents);
    face.projection = projection_for(dialect, quake_normal(points));
    const std::int32_t base_contents = contents_bits(contents);
    if (dialect == MapDialect::quake2_valve220 && contents != BrushContents::solid)
        face.quake2_metadata = Quake2FaceMetadata{base_contents, 0, 0};
    if (dialect == MapDialect::quake3_legacy || dialect == MapDialect::quake3_brush_primitives) {
        const std::int32_t q3_contents = detail ? base_contents | kMapDetailContentsBit : base_contents;
        face.quake3_metadata = Quake3FaceMetadata{q3_contents, 0, 0};
    }
    return face;
}

struct ExposedBoxFace {
    Axis axis{Axis::x};
    int sign{};
    MaterialRole material{MaterialRole::wall};
};

[[nodiscard]] std::array<std::pair<PlanePointTriple, std::pair<Axis, int>>, 6> box_planes(const GridBounds &bounds) {
    const GridCoord x0 = bounds.min.x;
    const GridCoord y0 = bounds.min.y;
    const GridCoord z0 = bounds.min.z;
    const GridCoord x1 = bounds.max.x;
    const GridCoord y1 = bounds.max.y;
    const GridCoord z1 = bounds.max.z;
    return {{{{{x1, y0, z0}, {x1, y1, z0}, {x1, y0, z1}}, {Axis::x, 1}},
             {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z0}}, {Axis::x, -1}},
             {{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z0}}, {Axis::y, 1}},
             {{{x0, y0, z0}, {x1, y0, z0}, {x0, y0, z1}}, {Axis::y, -1}},
             {{{x0, y0, z1}, {x1, y0, z1}, {x0, y1, z1}}, {Axis::z, 1}},
             {{{x0, y0, z0}, {x0, y1, z0}, {x1, y0, z0}}, {Axis::z, -1}}}};
}

[[nodiscard]] MapBrush make_box_brush(const GridBounds &bounds, const MaterialRole material,
                                      const BrushContents contents, const bool detail, const Game game,
                                      const MapDialect dialect,
                                      const std::optional<ExposedBoxFace> exposed = std::nullopt) {
    if (!bounds.valid())
        fail("invalid_box", "cannot lower invalid box bounds");
    const Vec3 witness{
        (static_cast<double>(bounds.min.x) + bounds.max.x) / 2.0,
        (static_cast<double>(bounds.min.y) + bounds.max.y) / 2.0,
        (static_cast<double>(bounds.min.z) + bounds.max.z) / 2.0,
    };
    MapBrush brush;
    brush.faces.reserve(6);
    for (const auto &[plane, orientation] : box_planes(bounds)) {
        MaterialRole face_material = material;
        if (exposed)
            face_material = orientation.first == exposed->axis && orientation.second == exposed->sign
                                ? exposed->material
                                : MaterialRole::hidden;
        brush.faces.push_back(make_face(plane, witness, face_material, contents, detail, game, dialect));
    }
    return brush;
}

[[nodiscard]] MapBrush make_convex_brush(const ConvexPolyhedron &polyhedron, const BrushGeometryValidation &validation,
                                         const MaterialRole material, const BrushContents contents, const bool detail,
                                         const Game game, const MapDialect dialect, const GridCoord coordinate_limit) {
    if (!validation.ok() || !validation.interior_witness)
        fail("invalid_convex_geometry", "cannot lower an invalid convex polyhedron");
    if (polyhedron.planes.size() > 512)
        fail("convex_face_limit", "convex polyhedron exceeds the portable face limit");
    std::vector<PlanePointTriple> planes = polyhedron.planes;
    std::sort(planes.begin(), planes.end(), points_less);
    MapBrush brush;
    brush.faces.reserve(planes.size());
    for (const PlanePointTriple &plane : planes) {
        for (const GridVec3 point : {plane.first, plane.second, plane.third}) {
            if (!within_coordinate_limit(point.x, coordinate_limit) ||
                !within_coordinate_limit(point.y, coordinate_limit) ||
                !within_coordinate_limit(point.z, coordinate_limit)) {
                fail("coordinate_limit", "convex plane point exceeds the configured map coordinate limit");
            }
        }
        brush.faces.push_back(
            make_face(plane, *validation.interior_witness, material, contents, detail, game, dialect));
    }
    return brush;
}

[[nodiscard]] MapBrush lower_geometry(const BrushGeometry &geometry, const MaterialRole material,
                                      const BrushContents contents, const bool detail, const Game game,
                                      const MapDialect dialect, const GridCoord coordinate_limit) {
    const BrushGeometryValidation validation = validate_brush_geometry(geometry);
    if (!validation.ok() || !validation.bounds)
        fail("invalid_brush_geometry", "layout brush is not a closed bounded convex volume");
    if (!within_coordinate_limit(*validation.bounds, coordinate_limit))
        fail("coordinate_limit", "layout brush exceeds the configured map coordinate limit");
    if (const GridBounds *box = get_box(geometry))
        return make_box_brush(*box, material, contents, detail, game, dialect);
    return make_convex_brush(*get_convex_polyhedron(geometry), validation, material, contents, detail, game, dialect,
                             coordinate_limit);
}

[[nodiscard]] bool extends_outward(const GridBounds &bounds, const FaceKey &face) {
    const GridCoord low = minimum(bounds, face.axis);
    const GridCoord high = maximum(bounds, face.axis);
    return face.sign > 0 ? low <= face.plane && high > face.plane : low < face.plane && high >= face.plane;
}

[[nodiscard]] bool begins_inside_shell(const GridBounds &bounds, const FaceKey &face, const GridCoord shell_thickness) {
    const GridCoord low = minimum(bounds, face.axis);
    const GridCoord high = maximum(bounds, face.axis);
    if (face.sign > 0) {
        const std::int64_t shell_end = static_cast<std::int64_t>(face.plane) + shell_thickness;
        return low > face.plane && static_cast<std::int64_t>(low) < shell_end;
    }
    const std::int64_t shell_end = static_cast<std::int64_t>(face.plane) - shell_thickness;
    return high<face.plane &&static_cast<std::int64_t>(high)> shell_end;
}

[[nodiscard]] MaterialRole boundary_material(const FaceKey &face) {
    if (face.axis == Axis::z)
        return face.sign < 0 ? MaterialRole::floor : MaterialRole::ceiling;
    return MaterialRole::wall;
}

void reject_edge_only_contacts(const std::vector<FreeVolume> &volumes) {
    for (std::size_t left = 0; left < volumes.size(); ++left) {
        for (std::size_t right = left + 1; right < volumes.size(); ++right) {
            const GridBounds &a = volumes[left].bounds;
            const GridBounds &b = volumes[right].bounds;
            const std::array<std::int64_t, 3> extents{
                static_cast<std::int64_t>(std::min(a.max.x, b.max.x)) - std::max(a.min.x, b.min.x),
                static_cast<std::int64_t>(std::min(a.max.y, b.max.y)) - std::max(a.min.y, b.min.y),
                static_cast<std::int64_t>(std::min(a.max.z, b.max.z)) - std::max(a.min.z, b.min.z),
            };
            if (std::any_of(extents.begin(), extents.end(), [](const std::int64_t value) { return value < 0; }))
                continue;
            const std::size_t positive = static_cast<std::size_t>(
                std::count_if(extents.begin(), extents.end(), [](const std::int64_t value) { return value > 0; }));
            if (positive < 2)
                fail("edge_only_free_volume_contact", "free-space volumes may not meet only along an edge or point");
        }
    }
}

[[nodiscard]] std::vector<FreeVolume> collect_free_volumes(const std::vector<const Room *> &rooms,
                                                           const std::vector<const Connection *> &connections,
                                                           const MapBuildOptions &options, MapBuildStats &stats) {
    std::set<RoomId> room_ids;
    std::vector<FreeVolume> raw;
    raw.reserve(rooms.size());
    for (const Room *room : rooms) {
        room_ids.insert(room->id);
        if (!room->bounds.valid())
            fail("invalid_free_volume", "room bounds must have positive volume");
        if (!within_coordinate_limit(room->bounds, options.max_coordinate))
            fail("coordinate_limit", "room bounds exceed the configured map coordinate limit");
        raw.push_back({room->bounds, {source_ref(room->id)}});
    }

    for (const Connection *connection : connections) {
        if (!room_ids.contains(connection->from_room) || !room_ids.contains(connection->to_room) ||
            connection->from_room == connection->to_room) {
            fail("invalid_connection", "connection endpoints must resolve to distinct rooms");
        }
        if (connection->passage_volumes.empty())
            fail("invalid_passage", "every connection requires at least one explicit passage volume");
        std::vector<GridBounds> passages = connection->passage_volumes;
        std::sort(passages.begin(), passages.end(), bounds_less);
        for (const GridBounds &passage : passages) {
            if (!passage.valid())
                fail("invalid_free_volume", "passage bounds must have positive volume");
            if (!within_coordinate_limit(passage, options.max_coordinate))
                fail("coordinate_limit", "passage bounds exceed the configured map coordinate limit");
            raw.push_back({passage, {source_ref(connection->id)}});
        }
    }

    stats.input_free_volumes = raw.size();
    std::sort(raw.begin(), raw.end(), [](const FreeVolume &left, const FreeVolume &right) {
        if (bounds_less(left.bounds, right.bounds))
            return true;
        if (bounds_less(right.bounds, left.bounds))
            return false;
        return left.sources.front() < right.sources.front();
    });

    std::vector<FreeVolume> unique;
    for (FreeVolume &volume : raw) {
        if (!unique.empty() && unique.back().bounds == volume.bounds) {
            unique.back().sources.insert(unique.back().sources.end(), volume.sources.begin(), volume.sources.end());
            std::sort(unique.back().sources.begin(), unique.back().sources.end());
            unique.back().sources.erase(std::unique(unique.back().sources.begin(), unique.back().sources.end()),
                                        unique.back().sources.end());
        } else {
            unique.push_back(std::move(volume));
        }
    }
    stats.unique_free_volumes = unique.size();
    reject_edge_only_contacts(unique);
    return unique;
}

[[nodiscard]] std::map<FaceKey, std::vector<FaceCandidate>>
build_face_candidates(const std::vector<FreeVolume> &volumes) {
    std::map<FaceKey, std::vector<FaceCandidate>> groups;
    constexpr std::array<Axis, 3> axes{Axis::x, Axis::y, Axis::z};
    for (const FreeVolume &volume : volumes) {
        const MapBuildSourceRef owner = *std::min_element(volume.sources.begin(), volume.sources.end());
        for (const Axis axis : axes) {
            const Rect rect = projected_rect(volume.bounds, axis);
            groups[{axis, -1, minimum(volume.bounds, axis)}].push_back({rect, owner});
            groups[{axis, 1, maximum(volume.bounds, axis)}].push_back({rect, owner});
        }
    }
    for (auto &[key, candidates] : groups) {
        static_cast<void>(key);
        std::sort(candidates.begin(), candidates.end(), [](const FaceCandidate &left, const FaceCandidate &right) {
            return std::tie(left.rect.u0, left.rect.v0, left.rect.u1, left.rect.v1, left.owner) <
                   std::tie(right.rect.u0, right.rect.v0, right.rect.u1, right.rect.v1, right.owner);
        });
    }
    return groups;
}

[[nodiscard]] std::vector<BoundaryRect> build_boundary_rectangles(const std::vector<FreeVolume> &volumes,
                                                                  const MapBuildOptions &options,
                                                                  MapBuildStats &stats) {
    const auto groups = build_face_candidates(volumes);
    std::vector<BoundaryRect> result;
    for (const auto &[face, candidates] : groups) {
        std::vector<GridCoord> u_coordinates;
        std::vector<GridCoord> v_coordinates;
        for (const FaceCandidate &candidate : candidates) {
            u_coordinates.push_back(candidate.rect.u0);
            u_coordinates.push_back(candidate.rect.u1);
            v_coordinates.push_back(candidate.rect.v0);
            v_coordinates.push_back(candidate.rect.v1);
        }
        for (const FreeVolume &volume : volumes) {
            if (!extends_outward(volume.bounds, face))
                continue;
            const Rect projection = projected_rect(volume.bounds, face.axis);
            const bool relevant =
                std::any_of(candidates.begin(), candidates.end(),
                            [&](const FaceCandidate &candidate) { return rects_overlap(projection, candidate.rect); });
            if (!relevant)
                continue;
            u_coordinates.push_back(projection.u0);
            u_coordinates.push_back(projection.u1);
            v_coordinates.push_back(projection.v0);
            v_coordinates.push_back(projection.v1);
        }
        std::sort(u_coordinates.begin(), u_coordinates.end());
        u_coordinates.erase(std::unique(u_coordinates.begin(), u_coordinates.end()), u_coordinates.end());
        std::sort(v_coordinates.begin(), v_coordinates.end());
        v_coordinates.erase(std::unique(v_coordinates.begin(), v_coordinates.end()), v_coordinates.end());
        if (u_coordinates.size() < 2 || v_coordinates.size() < 2)
            continue;
        const std::size_t u_count = u_coordinates.size() - 1;
        const std::size_t v_count = v_coordinates.size() - 1;
        if (v_count != 0 && u_count > options.max_face_cells / v_count)
            fail("face_cell_limit", "coordinate-compressed shell face exceeds the configured cell cap");
        const std::size_t group_cells = u_count * v_count;
        if (group_cells > options.max_face_cells - std::min(stats.evaluated_face_cells, options.max_face_cells))
            fail("face_cell_limit", "shell construction exceeds the configured total cell cap");
        stats.evaluated_face_cells += group_cells;

        std::vector<std::optional<MapBuildSourceRef>> boundary(group_cells);
        for (std::size_t v = 0; v < v_count; ++v) {
            for (std::size_t u = 0; u < u_count; ++u) {
                const GridCoord u0 = u_coordinates[u];
                const GridCoord u1 = u_coordinates[u + 1];
                const GridCoord v0 = v_coordinates[v];
                const GridCoord v1 = v_coordinates[v + 1];
                std::optional<MapBuildSourceRef> owner;
                for (const FaceCandidate &candidate : candidates) {
                    if (rect_contains(candidate.rect, u0, u1, v0, v1) && (!owner || candidate.owner < *owner))
                        owner = candidate.owner;
                }
                if (!owner)
                    continue;

                const bool outside_is_free = std::any_of(volumes.begin(), volumes.end(), [&](const FreeVolume &volume) {
                    return extends_outward(volume.bounds, face) &&
                           rect_contains(projected_rect(volume.bounds, face.axis), u0, u1, v0, v1);
                });
                if (outside_is_free)
                    continue;

                const Rect cell{u0, u1, v0, v1};
                const bool shell_intrudes = std::any_of(volumes.begin(), volumes.end(), [&](const FreeVolume &volume) {
                    return begins_inside_shell(volume.bounds, face, options.shell_thickness) &&
                           rects_overlap(projected_rect(volume.bounds, face.axis), cell);
                });
                if (shell_intrudes)
                    fail("shell_clearance", "shell thickness would intrude into a disconnected free-space volume");
                boundary[v * u_count + u] = owner;
            }
        }

        std::vector<bool> consumed(group_cells, false);
        for (std::size_t v = 0; v < v_count; ++v) {
            for (std::size_t u = 0; u < u_count; ++u) {
                const std::size_t start = v * u_count + u;
                if (consumed[start] || !boundary[start])
                    continue;
                const MapBuildSourceRef owner = *boundary[start];
                std::size_t u_end = u + 1;
                while (u_end < u_count) {
                    const std::size_t index = v * u_count + u_end;
                    if (consumed[index] || boundary[index] != owner)
                        break;
                    ++u_end;
                }
                std::size_t v_end = v + 1;
                for (; v_end < v_count; ++v_end) {
                    bool complete_row = true;
                    for (std::size_t column = u; column < u_end; ++column) {
                        const std::size_t index = v_end * u_count + column;
                        if (consumed[index] || boundary[index] != owner) {
                            complete_row = false;
                            break;
                        }
                    }
                    if (!complete_row)
                        break;
                }
                for (std::size_t row = v; row < v_end; ++row) {
                    for (std::size_t column = u; column < u_end; ++column)
                        consumed[row * u_count + column] = true;
                }
                result.push_back({face,
                                  {u_coordinates[u], u_coordinates[u_end], v_coordinates[v], v_coordinates[v_end]},
                                  owner,
                                  boundary_material(face)});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const BoundaryRect &left, const BoundaryRect &right) {
        return std::tie(left.face, left.rect.u0, left.rect.v0, left.rect.u1, left.rect.v1, left.owner) <
               std::tie(right.face, right.rect.u0, right.rect.v0, right.rect.u1, right.rect.v1, right.owner);
    });
    return result;
}

[[nodiscard]] GridBounds slab_bounds(const BoundaryRect &boundary, const MapBuildOptions &options) {
    GridBounds slab;
    const auto tangents = tangent_axes(boundary.face.axis);
    set_component(slab.min, tangents[0], boundary.rect.u0);
    set_component(slab.max, tangents[0], boundary.rect.u1);
    set_component(slab.min, tangents[1], boundary.rect.v0);
    set_component(slab.max, tangents[1], boundary.rect.v1);
    try {
        if (boundary.face.sign > 0) {
            set_component(slab.min, boundary.face.axis, boundary.face.plane);
            set_component(slab.max, boundary.face.axis, checked_grid_add(boundary.face.plane, options.shell_thickness));
        } else {
            set_component(slab.min, boundary.face.axis,
                          checked_grid_subtract(boundary.face.plane, options.shell_thickness));
            set_component(slab.max, boundary.face.axis, boundary.face.plane);
        }
    } catch (const std::overflow_error &) {
        fail("shell_overflow", "shell extrusion overflows the signed layout grid");
    }
    if (!slab.valid())
        fail("invalid_shell_brush", "shell extrusion produced invalid bounds");
    if (!within_coordinate_limit(slab, options.max_coordinate))
        fail("coordinate_limit", "shell extrusion exceeds the configured map coordinate limit");
    return slab;
}

[[nodiscard]] std::vector<TaggedBrush> build_shell(const std::vector<FreeVolume> &volumes, const Game game,
                                                   const MapBuildOptions &options, MapBuildStats &stats) {
    const std::vector<BoundaryRect> boundaries = build_boundary_rectangles(volumes, options, stats);
    if (boundaries.size() > options.max_shell_brushes)
        fail("shell_brush_limit", "free-space union exceeds the configured shell brush cap");
    std::vector<TaggedBrush> shell;
    shell.reserve(boundaries.size());
    for (const BoundaryRect &boundary : boundaries) {
        const GridBounds slab = slab_bounds(boundary, options);
        const ExposedBoxFace exposed{boundary.face.axis, -boundary.face.sign, boundary.material};
        shell.push_back(
            {make_box_brush(slab, MaterialRole::hidden, BrushContents::solid, false, game, options.dialect, exposed),
             boundary.owner});
    }
    stats.shell_brushes = shell.size();
    stats.structural_brushes += shell.size();
    return shell;
}

void add_property(MapEntity &entity, std::string key, std::string value) {
    const bool duplicate = std::any_of(entity.properties.begin(), entity.properties.end(),
                                       [&](const auto &property) { return property.key == key; });
    if (duplicate)
        fail("duplicate_generated_property", "native entity adapter attempted to emit a duplicate property key");
    entity.properties.push_back({0, std::move(key), std::move(value)});
}

[[nodiscard]] std::string origin_text(const GridVec3 origin) {
    return std::to_string(origin.x) + " " + std::to_string(origin.y) + " " + std::to_string(origin.z);
}

[[nodiscard]] std::string milli_text(const std::uint16_t value) {
    if (value > 1000)
        fail("invalid_light_color", "light color components must be from zero through one thousand");
    if (value == 0)
        return "0";
    if (value == 1000)
        return "1";
    std::string digits = std::to_string(value);
    if (digits.size() < 3)
        digits.insert(digits.begin(), 3 - digits.size(), '0');
    while (!digits.empty() && digits.back() == '0')
        digits.pop_back();
    return "0." + digits;
}

[[nodiscard]] std::string color_text(const RgbColor color) {
    if (!color.valid())
        fail("invalid_light_color", "light color components must be from zero through one thousand");
    return milli_text(color.red_milli) + " " + milli_text(color.green_milli) + " " + milli_text(color.blue_milli);
}

[[nodiscard]] std::string link_name(const PlacementId id) {
    if (!id)
        fail("invalid_placement_link", "cannot generate a link name for the reserved zero identifier");
    return "qmg_link_" + std::to_string(id.value);
}

// Change r1s-q1-intermission: the Quake 1 intermission camera's "mangle" (pitch yaw roll). execute_changelevel
// copies it into each client's angles and v_angle (progs1996 execute_changelevel statements 5889-5893;
// quakec/client.qc:314), so a positive pitch looks down; "angle" is not read for this entity. Quake 2 and Quake 3
// intermission points take other keys and are not part of this change, so they fail closed.
[[nodiscard]] std::string intermission_mangle_text(const Placement &placement, const Game game) {
    if (game != Game::quake1)
        fail("unsupported_placement", "intermission placements are emitted for Quake 1 only (r1s-q1-intermission)");
    if (!placement.angle_degrees || !placement.pitch_degrees)
        fail("invalid_intermission", "an intermission camera needs a yaw and a pitch");
    return std::to_string(*placement.pitch_degrees) + " " + std::to_string(*placement.angle_degrees) + " 0";
}

[[nodiscard]] Team require_team(const Placement &placement) {
    if (!placement.team)
        fail("missing_team", "team placement requires an explicit typed team");
    return *placement.team;
}

// Classnames come from data/profiles/<game>/generator-current.json through the generated tables
// header. Team roles need a typed team; weapon and monster roles select by variant index.
[[nodiscard]] std::string team_class(const Placement &placement, const Game game) {
    const Team team = require_team(placement);
    const std::string_view classname = game_profile::team_classname(game, placement.role, team);
    if (classname.empty())
        fail("invalid_team", "placement has an invalid typed team");
    return std::string(classname);
}

[[nodiscard]] std::string variant_class(const Placement &placement, const Game game) {
    if (placement.role == PlacementRole::weapon || placement.role == PlacementRole::monster) {
        if (const std::optional<game_profile::VariantTable> table = game_profile::variant_table(game, placement.role)) {
            if (!table->unsupported_reason.empty())
                fail("unsupported_placement", std::string(table->unsupported_reason));
            if (placement.variant >= table->classnames.size())
                fail("invalid_placement_variant", "placement variant is unsupported for its title and role");
            return std::string(table->classnames[placement.variant]);
        }
    }
    fail("invalid_placement_role", "variant entity mapping requested for a non-variant role");
}

[[nodiscard]] std::string classname_for(const Placement &placement, const Game game) {
    switch (placement.role) {
    case PlacementRole::team_initial_spawn:
    case PlacementRole::team_respawn:
    case PlacementRole::flag:
        return team_class(placement, game);
    case PlacementRole::weapon:
    case PlacementRole::monster:
        return variant_class(placement, game);
    case PlacementRole::player_start:
    case PlacementRole::deathmatch_spawn:
    case PlacementRole::health:
    case PlacementRole::armor:
    case PlacementRole::light:
    case PlacementRole::exit_trigger:
    case PlacementRole::exit_destination:
    case PlacementRole::door:
    case PlacementRole::door_trigger:
    case PlacementRole::door_destination:
    case PlacementRole::lift:
    case PlacementRole::lift_trigger:
    case PlacementRole::lift_destination:
    case PlacementRole::teleporter_trigger:
    case PlacementRole::teleporter_destination:
    case PlacementRole::intermission:
    case PlacementRole::coop_start:
        if (const std::string_view classname = game_profile::role_classname(game, placement.role); !classname.empty())
            return std::string(classname);
        break;
    }
    fail("invalid_placement_role", "placement has an invalid typed role");
}

[[nodiscard]] bool role_may_own_brushes(const PlacementRole role) {
    switch (role) {
    case PlacementRole::exit_trigger:
    case PlacementRole::door:
    case PlacementRole::door_trigger:
    case PlacementRole::lift:
    case PlacementRole::lift_trigger:
    case PlacementRole::teleporter_trigger:
        return true;
    case PlacementRole::player_start:
    case PlacementRole::deathmatch_spawn:
    case PlacementRole::team_initial_spawn:
    case PlacementRole::team_respawn:
    case PlacementRole::weapon:
    case PlacementRole::health:
    case PlacementRole::armor:
    case PlacementRole::monster:
    case PlacementRole::flag:
    case PlacementRole::light:
    case PlacementRole::exit_destination:
    case PlacementRole::door_destination:
    case PlacementRole::lift_destination:
    case PlacementRole::teleporter_destination:
    case PlacementRole::intermission:
    case PlacementRole::coop_start:
        return false;
    }
    fail("invalid_placement_role", "placement has an invalid typed role");
}

[[nodiscard]] MaterialRole volume_material(const PlacementRole role) {
    switch (role) {
    case PlacementRole::door:
        return MaterialRole::door;
    case PlacementRole::lift:
        return MaterialRole::lift;
    case PlacementRole::exit_trigger:
    case PlacementRole::door_trigger:
    case PlacementRole::lift_trigger:
    case PlacementRole::teleporter_trigger:
        return MaterialRole::trigger;
    default:
        break;
    }
    fail("invalid_placement_volume", "placement role cannot own a generated volume brush");
}

[[nodiscard]] bool quake2_point_teleporter(const Placement &placement, const Game game) {
    return game == Game::quake2 && placement.role == PlacementRole::teleporter_trigger;
}

void validate_placement_fields(const Placement &placement, const Game game, const GridCoord coordinate_limit) {
    if (game == Game::quake2 && (placement.role == PlacementRole::teleporter_trigger ||
                                 placement.role == PlacementRole::teleporter_destination)) {
        try {
            if (quake2_point_teleporter(placement, game) &&
                (!placement.volume || *placement.volume != quake2_teleporter_bounds(placement.origin, true)))
                fail("invalid_q2_teleporter_volume", "Quake 2 teleporter requires the stock point entity touch bounds");
            if (!within_coordinate_limit(quake2_teleporter_bounds(placement.origin, false), coordinate_limit))
                fail("coordinate_limit", "Quake 2 teleporter disc exceeds the configured map coordinate limit");
        } catch (const std::overflow_error &) {
            fail("coordinate_limit", "Quake 2 teleporter bounds overflow the integer grid");
        }
    }
    // Change r1s-sp-coop-starts: info_player_coop exists in Quake 1 (progs1996 info_player_coop statement 6175;
    // quakec/client.qc:855) and Quake 2 (game/g_spawn.c:156) only. Quake 3 has no co-op mode and no such entity
    // (code/game/g_spawn.c:199-201), so a Quake 3 coop start fails closed.
    if (placement.role == PlacementRole::coop_start && game == Game::quake3)
        fail("unsupported_placement", "co-op starts are emitted for Quake 1 and Quake 2 only (r1s-sp-coop-starts)");
    if (placement.angle_degrees && (*placement.angle_degrees < 0 || *placement.angle_degrees >= 360))
        fail("invalid_placement_angle", "placement angle must be from zero through 359 degrees");
    if (placement.pitch_degrees && (placement.role != PlacementRole::intermission || *placement.pitch_degrees < -90 ||
                                    *placement.pitch_degrees > 90))
        fail("invalid_placement_pitch", "only an intermission camera carries a pitch, from -90 through 90 degrees");
    const bool team_role = placement.role == PlacementRole::team_initial_spawn ||
                           placement.role == PlacementRole::team_respawn || placement.role == PlacementRole::flag;
    if (team_role != placement.team.has_value())
        fail("invalid_team_contract", "placement team does not match its typed role");
    if (placement.role == PlacementRole::light) {
        if (!placement.light_intensity || *placement.light_intensity <= 0 || !placement.light_color ||
            !placement.light_color->valid()) {
            fail("invalid_light", "light placement requires positive intensity and a valid typed color");
        }
    } else if (placement.light_intensity || placement.light_color) {
        fail("invalid_light", "only light placements may carry light parameters");
    }
    if (placement.volume) {
        if (!placement.volume->valid())
            fail("invalid_placement_volume", "placement volume must have positive bounds");
        if (!within_coordinate_limit(*placement.volume, coordinate_limit))
            fail("coordinate_limit", "placement volume exceeds the configured map coordinate limit");
    } else if (!within_coordinate_limit(placement.origin.x, coordinate_limit) ||
               !within_coordinate_limit(placement.origin.y, coordinate_limit) ||
               !within_coordinate_limit(placement.origin.z, coordinate_limit)) {
        fail("coordinate_limit", "point placement exceeds the configured map coordinate limit");
    }
    if (placement.destination_map && placement.destination_map->empty())
        fail("invalid_destination", "destination map cannot be empty");

    // Resolve variant bounds even before native entity assembly.
    if (placement.role == PlacementRole::weapon || placement.role == PlacementRole::monster)
        static_cast<void>(variant_class(placement, game));
    else if (placement.variant != 0)
        fail("invalid_placement_variant", "non-variant placement roles require variant zero");
}

[[nodiscard]] PendingEntity make_placement_entity(const Placement &placement, const Game game,
                                                  const MapBuildOptions &options,
                                                  const std::set<PlacementId> &referenced_targets,
                                                  const std::vector<TaggedBrush> &owned_brushes,
                                                  const NativeMoverProperties *mover, MapBuildStats &stats) {
    validate_placement_fields(placement, game, options.max_coordinate);
    PendingEntity pending;
    pending.entity_source = source_ref(placement.id);
    add_property(pending.entity, "classname", classname_for(placement, game));
    if (referenced_targets.contains(placement.id))
        add_property(pending.entity, "targetname", link_name(placement.id));
    if (placement.link)
        add_property(pending.entity, "target", link_name(*placement.link));
    if (placement.destination_map)
        add_property(pending.entity, "map", *placement.destination_map);
    if (placement.angle_degrees && mover != nullptr)
        fail("inconsistent_mover_directive", "a typed mover directive, not Placement, owns native motion properties");
    if (placement.role == PlacementRole::intermission)
        add_property(pending.entity, "mangle", intermission_mangle_text(placement, game));
    else if (placement.angle_degrees)
        add_property(pending.entity, "angle", std::to_string(*placement.angle_degrees));
    if (mover != nullptr) {
        add_property(pending.entity, "speed", mover->speed);
        add_property(pending.entity, "wait", mover->wait);
        if (mover->angle)
            add_property(pending.entity, "angle", *mover->angle);
        if (mover->lip)
            add_property(pending.entity, "lip", *mover->lip);
        if (mover->height)
            add_property(pending.entity, "height", *mover->height);
    }
    if (placement.role == PlacementRole::light) {
        add_property(pending.entity, "light", std::to_string(*placement.light_intensity));
        add_property(pending.entity, "_color", color_text(*placement.light_color));
    }

    const bool point_teleporter = quake2_point_teleporter(placement, game);
    if (!role_may_own_brushes(placement.role) && (!owned_brushes.empty() || placement.volume))
        fail("invalid_placement_volume", "point placement cannot own brush geometry");
    if (point_teleporter && !owned_brushes.empty())
        fail("invalid_placement_volume", "Quake 2 misc_teleporter cannot own layout brush geometry");

    for (const TaggedBrush &owned : owned_brushes) {
        MapBrush native_brush = owned.brush;
        if (mover != nullptr && mover->brush_translation != GridVec3{}) {
            for (MapBrushFace &face : native_brush.faces) {
                for (auto &point : face.plane_points) {
                    point.x += mover->brush_translation.x;
                    point.y += mover->brush_translation.y;
                    point.z += mover->brush_translation.z;
                    if (std::abs(point.x) > options.max_coordinate || std::abs(point.y) > options.max_coordinate ||
                        std::abs(point.z) > options.max_coordinate)
                        fail("coordinate_limit", "translated native lift brush exceeds the map coordinate limit");
                }
            }
        }
        pending.entity.primitives.push_back(std::move(native_brush));
        pending.primitive_sources.push_back(owned.source);
        ++stats.placement_brushes;
    }
    if (placement.volume && owned_brushes.empty() && !point_teleporter) {
        pending.entity.primitives.push_back(make_box_brush(*placement.volume, volume_material(placement.role),
                                                           BrushContents::solid, false, game, options.dialect));
        pending.primitive_sources.push_back(source_ref(placement.id));
        ++stats.placement_brushes;
    }
    if (role_may_own_brushes(placement.role) && !point_teleporter && pending.entity.primitives.empty())
        fail("missing_placement_geometry", "brush placement has neither a volume nor owned brush solids");
    if (pending.entity.primitives.empty())
        add_property(pending.entity, "origin", origin_text(placement.origin));
    return pending;
}

void count_total_brush_or_fail(const MapBuildOptions &options, std::size_t &brushes) {
    if (brushes >= options.max_total_brushes)
        fail("total_brush_limit", "lowered document exceeds the configured total brush cap");
    ++brushes;
}

[[nodiscard]] std::vector<const Room *> sorted_rooms(const Layout &layout) {
    std::vector<const Room *> result;
    result.reserve(layout.rooms.size());
    for (const Room &room : layout.rooms)
        result.push_back(&room);
    std::sort(result.begin(), result.end(), [](const Room *left, const Room *right) {
        if (left->id != right->id)
            return left->id < right->id;
        return bounds_less(left->bounds, right->bounds);
    });
    require_unique_nonzero_ids(result, [](const Room &room) { return room.id; }, "room");
    return result;
}

[[nodiscard]] std::vector<const Connection *> sorted_connections(const Layout &layout) {
    std::vector<const Connection *> result;
    result.reserve(layout.connections.size());
    for (const Connection &connection : layout.connections)
        result.push_back(&connection);
    std::sort(result.begin(), result.end(),
              [](const Connection *left, const Connection *right) { return left->id < right->id; });
    require_unique_nonzero_ids(result, [](const Connection &connection) { return connection.id; }, "connection");
    return result;
}

[[nodiscard]] std::vector<const LayoutSolid *> sorted_solids(const Layout &layout) {
    std::vector<const LayoutSolid *> result;
    result.reserve(layout.solids.size());
    for (const LayoutSolid &solid : layout.solids)
        result.push_back(&solid);
    std::sort(result.begin(), result.end(),
              [](const LayoutSolid *left, const LayoutSolid *right) { return left->id < right->id; });
    require_unique_nonzero_ids(result, [](const LayoutSolid &solid) { return solid.id; }, "solid");
    return result;
}

[[nodiscard]] std::vector<const Placement *> sorted_placements(const Layout &layout) {
    std::vector<const Placement *> result;
    result.reserve(layout.placements.size());
    for (const Placement &placement : layout.placements)
        result.push_back(&placement);
    std::sort(result.begin(), result.end(),
              [](const Placement *left, const Placement *right) { return left->id < right->id; });
    require_unique_nonzero_ids(result, [](const Placement &placement) { return placement.id; }, "placement");
    return result;
}

[[nodiscard]] const LayoutSolid *find_sorted_solid(const std::vector<const LayoutSolid *> &solids, const SolidId id) {
    const auto found =
        std::lower_bound(solids.begin(), solids.end(), id,
                         [](const LayoutSolid *solid, const SolidId wanted) { return solid->id < wanted; });
    return found != solids.end() && (*found)->id == id ? *found : nullptr;
}

[[nodiscard]] const Placement *find_sorted_placement(const std::vector<const Placement *> &placements,
                                                     const PlacementId id) {
    const auto found =
        std::lower_bound(placements.begin(), placements.end(), id,
                         [](const Placement *placement, const PlacementId wanted) { return placement->id < wanted; });
    return found != placements.end() && (*found)->id == id ? *found : nullptr;
}

[[nodiscard]] std::string seconds_text(const std::uint32_t milliseconds) {
    const std::uint32_t whole = milliseconds / 1000U;
    const std::uint32_t remainder = milliseconds % 1000U;
    std::string result = std::to_string(whole);
    if (remainder == 0)
        return result;
    std::string fractional = std::to_string(remainder + 1000U).substr(1);
    while (!fractional.empty() && fractional.back() == '0')
        fractional.pop_back();
    result.push_back('.');
    result += fractional;
    return result;
}

[[nodiscard]] Axis mover_axis(const GridVec3 delta, const PlacementRole role) {
    const unsigned nonzero_axes = (delta.x != 0 ? 1U : 0U) + (delta.y != 0 ? 1U : 0U) + (delta.z != 0 ? 1U : 0U);
    if (nonzero_axes != 1U)
        fail("invalid_mover_delta", "mover travel must be nonzero and axis-aligned");
    if (role == PlacementRole::lift && (delta.x != 0 || delta.y != 0))
        fail("invalid_mover_delta", "lift travel must use the vertical axis");
    if (delta.x != 0)
        return Axis::x;
    if (delta.y != 0)
        return Axis::y;
    return Axis::z;
}

[[nodiscard]] std::string door_angle(const GridVec3 delta, const Axis axis) {
    switch (axis) {
    case Axis::x:
        return delta.x > 0 ? "0" : "180";
    case Axis::y:
        return delta.y > 0 ? "90" : "270";
    case Axis::z:
        return delta.z > 0 ? "-1" : "-2";
    }
    fail("invalid_mover_delta", "mover travel axis is invalid");
}

[[nodiscard]] bool exact_delta(const GridVec3 from, const GridVec3 to, const GridVec3 expected) {
    return static_cast<std::int64_t>(to.x) - from.x == expected.x &&
           static_cast<std::int64_t>(to.y) - from.y == expected.y &&
           static_cast<std::int64_t>(to.z) - from.z == expected.z;
}

struct ValidatedConstruction {
    std::vector<const LayoutCurve *> curves;
    std::map<PlacementId, NativeMoverProperties> movers;
};

[[nodiscard]] ValidatedConstruction validate_construction(const ConstructionResult &construction,
                                                          const std::vector<const LayoutSolid *> &layout_solids,
                                                          const std::vector<const Placement *> &layout_placements,
                                                          const MapBuildOptions &options, const Game game) {
    std::vector<const LayoutSolid *> generated_solids;
    for (const LayoutSolid &solid : construction.solids)
        generated_solids.push_back(&solid);
    for (const LayoutCurve &curve : construction.curves) {
        for (const LayoutSolid &solid : curve.collision_backing_solids)
            generated_solids.push_back(&solid);
    }
    std::sort(generated_solids.begin(), generated_solids.end(),
              [](const LayoutSolid *left, const LayoutSolid *right) { return left->id < right->id; });
    for (std::size_t index = 0; index < generated_solids.size(); ++index) {
        const LayoutSolid &generated = *generated_solids[index];
        if (!generated.id)
            fail("invalid_construction_id", "construction contains a generated solid with the reserved zero ID");
        if (index != 0 && generated.id == generated_solids[index - 1]->id)
            fail("duplicate_construction_id", "construction generated SolidIds must be unique");
        const LayoutSolid *applied = find_sorted_solid(layout_solids, generated.id);
        if (applied == nullptr || !(*applied == generated))
            fail("construction_not_applied", "a generated construction solid is absent or changed in Layout");
    }

    std::vector<const Placement *> generated_placements;
    generated_placements.reserve(construction.placements.size());
    for (const Placement &placement : construction.placements)
        generated_placements.push_back(&placement);
    std::sort(generated_placements.begin(), generated_placements.end(),
              [](const Placement *left, const Placement *right) { return left->id < right->id; });
    for (std::size_t index = 0; index < generated_placements.size(); ++index) {
        const Placement &generated = *generated_placements[index];
        if (!generated.id)
            fail("invalid_construction_id", "construction contains a generated placement with the reserved zero ID");
        if (index != 0 && generated.id == generated_placements[index - 1]->id)
            fail("duplicate_construction_id", "construction generated PlacementIds must be unique");
        const Placement *applied = find_sorted_placement(layout_placements, generated.id);
        if (applied == nullptr || !(*applied == generated))
            fail("construction_not_applied", "a generated construction placement is absent or changed in Layout");
    }

    ValidatedConstruction validated;
    validated.curves.reserve(construction.curves.size());
    for (const LayoutCurve &curve : construction.curves)
        validated.curves.push_back(&curve);
    std::sort(validated.curves.begin(), validated.curves.end(),
              [](const LayoutCurve *left, const LayoutCurve *right) { return left->source_id < right->source_id; });
    if (!validated.curves.empty() && game != Game::quake3)
        fail("unsupported_curve_dialect", "patch veneers are only supported by Quake 3 map dialects");
    const std::int64_t texture_coordinate_limit = static_cast<std::int64_t>(options.max_coordinate) * 1000;
    for (std::size_t index = 0; index < validated.curves.size(); ++index) {
        const LayoutCurve &curve = *validated.curves[index];
        if (!curve.source_id)
            fail("invalid_curve_id", "curve source reserves SolidId zero");
        if (index != 0 && curve.source_id == validated.curves[index - 1]->source_id)
            fail("duplicate_curve_id", "curve source SolidIds must be unique");
        if (find_sorted_solid(layout_solids, curve.source_id) != nullptr)
            fail("curve_id_collision", "curve source SolidId collides with a LayoutSolid ID");
        if (curve.material != MaterialRole::curve || curve.ownership != BrushOwnership::detail)
            fail("invalid_curve_ownership", "patch veneers require the curve material and detail ownership");
        if (curve.width < 3 || curve.height < 3 || curve.width > 31 || curve.height > 31 || (curve.width & 1U) == 0 ||
            (curve.height & 1U) == 0) {
            fail("invalid_curve_dimensions", "patch dimensions must be odd values from 3 through 31");
        }
        const std::size_t expected_controls = static_cast<std::size_t>(curve.width) * curve.height;
        if (curve.control_points.size() != expected_controls)
            fail("invalid_curve_controls", "curve controls must exactly match width-major dimensions");
        if (curve.collision_backing_solids.empty())
            fail("missing_curve_backing", "a patch veneer requires explicit applied collision backing");
        for (const LayoutSolid &backing : curve.collision_backing_solids) {
            if (backing.material != MaterialRole::hidden || backing.purpose != BrushPurpose::hidden ||
                backing.ownership != BrushOwnership::detail || backing.contents != BrushContents::solid ||
                backing.owner) {
                fail("invalid_curve_ownership", "patch collision backing must be hidden, solid, and detail-owned");
            }
        }
        for (const LayoutCurveControlPoint &control : curve.control_points) {
            if (!within_coordinate_limit(control.position.x, options.max_coordinate) ||
                !within_coordinate_limit(control.position.y, options.max_coordinate) ||
                !within_coordinate_limit(control.position.z, options.max_coordinate) ||
                std::abs(static_cast<std::int64_t>(control.u_milli)) > texture_coordinate_limit ||
                std::abs(static_cast<std::int64_t>(control.v_milli)) > texture_coordinate_limit) {
                fail("coordinate_limit", "curve control position or texture coordinate exceeds the map limit");
            }
        }
    }

    std::vector<const MoverDirective *> directives;
    directives.reserve(construction.movers.size());
    for (const MoverDirective &directive : construction.movers)
        directives.push_back(&directive);
    std::sort(directives.begin(), directives.end(),
              [](const MoverDirective *left, const MoverDirective *right) { return left->mover < right->mover; });
    for (std::size_t index = 0; index < directives.size(); ++index) {
        const MoverDirective &directive = *directives[index];
        if (!directive.mover)
            fail("unresolved_mover_directive", "mover directive reserves PlacementId zero");
        if (index != 0 && directive.mover == directives[index - 1]->mover)
            fail("duplicate_mover_directive", "each construction mover may have only one directive");
        const Placement *mover = find_sorted_placement(generated_placements, directive.mover);
        if (mover == nullptr || (mover->role != PlacementRole::door && mover->role != PlacementRole::lift))
            fail("unresolved_mover_directive", "mover directive does not resolve to a generated door or lift");
        if (directive.speed_units_per_second <= 0)
            fail("invalid_mover_directive", "mover speed must be positive");
        if (!mover->volume || mover->origin != mover->volume->center() || !mover->link)
            fail("inconsistent_mover_directive", "mover placement does not carry its generated volume and destination");
        const Placement *destination = find_sorted_placement(generated_placements, *mover->link);
        const PlacementRole destination_role =
            mover->role == PlacementRole::door ? PlacementRole::door_destination : PlacementRole::lift_destination;
        if (destination == nullptr || destination->role != destination_role || destination->volume ||
            !exact_delta(mover->volume->center(), destination->origin, directive.travel_delta)) {
            fail("inconsistent_mover_directive", "mover travel does not match its typed destination");
        }
        const Axis axis = mover_axis(directive.travel_delta, mover->role);

        bool saw_owned_brush = false;
        std::int64_t brush_min = std::numeric_limits<std::int64_t>::max();
        std::int64_t brush_max = std::numeric_limits<std::int64_t>::min();
        for (const LayoutSolid *solid : generated_solids) {
            if (solid->owner != directive.mover)
                continue;
            saw_owned_brush = true;
            const MaterialRole expected_material =
                mover->role == PlacementRole::door ? MaterialRole::door : MaterialRole::lift;
            const BrushPurpose expected_purpose =
                mover->role == PlacementRole::door ? BrushPurpose::door : BrushPurpose::lift;
            const GridBounds *bounds = std::get_if<GridBounds>(&solid->geometry);
            if (solid->ownership != BrushOwnership::placement || solid->contents != BrushContents::solid ||
                solid->material != expected_material || solid->purpose != expected_purpose || bounds == nullptr) {
                fail("inconsistent_mover_directive", "mover directive requires matching box brush geometry");
            }
            brush_min = std::min(brush_min, static_cast<std::int64_t>(minimum(*bounds, axis)));
            brush_max = std::max(brush_max, static_cast<std::int64_t>(maximum(*bounds, axis)));
        }
        if (!saw_owned_brush || brush_min >= brush_max)
            fail("unresolved_mover_directive", "mover directive has no generated brush extent");

        const std::int64_t delta_component = component(directive.travel_delta, axis);
        const std::int64_t travel = delta_component < 0 ? -delta_component : delta_component;
        NativeMoverProperties native;
        native.speed = std::to_string(directive.speed_units_per_second);
        native.wait = seconds_text(directive.wait_milliseconds);
        if (mover->role == PlacementRole::door) {
            native.angle = door_angle(directive.travel_delta, axis);
            native.lip = std::to_string((brush_max - brush_min) - travel);
        } else {
            native.height = std::to_string(travel);
            if (game == Game::quake2 && directive.travel_delta.z > 0)
                native.brush_translation = directive.travel_delta;
        }
        validated.movers.emplace(directive.mover, std::move(native));
    }
    for (const Placement *placement : generated_placements) {
        if ((placement->role == PlacementRole::door || placement->role == PlacementRole::lift) &&
            !validated.movers.contains(placement->id)) {
            fail("missing_mover_directive", "generated door or lift has no typed motion directive");
        }
    }
    return validated;
}

void add_manifest_nodes(std::map<MapBuildSourceRef, std::vector<MapNodeId>> &manifest, const MapBuildSourceRef source,
                        const MapEntity &entity) {
    std::vector<MapNodeId> &nodes = manifest[source];
    nodes.push_back(entity.id);
    for (const MapEntityProperty &property : entity.properties)
        nodes.push_back(property.id);
}

void add_manifest_nodes(std::map<MapBuildSourceRef, std::vector<MapNodeId>> &manifest, const MapBuildSourceRef source,
                        const MapPrimitive &primitive) {
    std::vector<MapNodeId> &nodes = manifest[source];
    std::visit(
        [&](const auto &value) {
            nodes.push_back(value.id);
            using Primitive = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Primitive, MapBrush>) {
                for (const MapBrushFace &face : value.faces)
                    nodes.push_back(face.id);
            } else {
                for (const MapPatchControlPoint &control : value.control_points)
                    nodes.push_back(control.id);
            }
        },
        primitive);
}

[[nodiscard]] std::vector<MapBuildManifestEntry>
make_manifest(const MapDocument &document, const std::vector<PendingEntity> &pending,
              std::map<MapBuildSourceRef, std::vector<MapNodeId>> manifest) {
    if (document.entities().size() != pending.size())
        fail("internal_manifest_error", "pending and emitted entity counts diverged");
    for (std::size_t entity_index = 0; entity_index < pending.size(); ++entity_index) {
        const PendingEntity &pending_entity = pending[entity_index];
        const MapEntity &entity = document.entities()[entity_index];
        if (pending_entity.entity_source)
            add_manifest_nodes(manifest, *pending_entity.entity_source, entity);
        if (pending_entity.primitive_sources.size() != entity.primitives.size())
            fail("internal_manifest_error", "pending primitive provenance count diverged");
        for (std::size_t primitive_index = 0; primitive_index < entity.primitives.size(); ++primitive_index) {
            if (pending_entity.primitive_sources[primitive_index]) {
                add_manifest_nodes(manifest, *pending_entity.primitive_sources[primitive_index],
                                   entity.primitives[primitive_index]);
            }
        }
    }
    std::vector<MapBuildManifestEntry> result;
    result.reserve(manifest.size());
    for (auto &[source, nodes] : manifest) {
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        result.push_back({source, std::move(nodes)});
    }
    return result;
}

} // namespace

MapBuildError::MapBuildError(std::string code, std::string message)
    : std::runtime_error(code + ": " + message), code_(std::move(code)) {}

MapBuildResult MapBuilder::build(const Layout &layout, const MapBuildOptions &options) const {
    return build(layout, options, ConstructionResult{});
}

MapBuildResult MapBuilder::build(const Layout &layout, const MapBuildOptions &options,
                                 const ConstructionResult &construction) const {
    validate_options(options);
    if (!dialect_matches_game(layout.spec.game, options.dialect))
        fail("dialect_game_mismatch", "requested map dialect does not match the layout title");

    const std::vector<const Room *> rooms = sorted_rooms(layout);
    const std::vector<const Connection *> connections = sorted_connections(layout);
    const std::vector<const LayoutSolid *> solids = sorted_solids(layout);
    const std::vector<const Placement *> placements = sorted_placements(layout);
    const ValidatedConstruction validated_construction =
        validate_construction(construction, solids, placements, options, layout.spec.game);
    if (rooms.empty())
        fail("empty_free_space", "layout requires at least one room free-space volume");

    MapBuildStats stats;
    const std::vector<FreeVolume> free_volumes = collect_free_volumes(rooms, connections, options, stats);
    std::vector<TaggedBrush> world_brushes = build_shell(free_volumes, layout.spec.game, options, stats);
    std::vector<TaggedBrush> detail_brushes;
    std::vector<TaggedPatch> world_patches;
    world_patches.reserve(validated_construction.curves.size());
    for (const LayoutCurve *curve : validated_construction.curves)
        world_patches.push_back({lower_curve_patch(*curve), source_ref(curve->source_id)});
    std::map<PlacementId, std::vector<TaggedBrush>> placement_brushes;
    std::set<PlacementId> placement_ids;
    for (const Placement *placement : placements)
        placement_ids.insert(placement->id);

    std::size_t total_brushes = world_brushes.size();
    if (total_brushes > options.max_total_brushes)
        fail("total_brush_limit", "shell alone exceeds the configured total brush cap");
    for (const LayoutSolid *solid : solids) {
        const bool liquid_contract = solid->contents != BrushContents::solid &&
                                     solid->material == MaterialRole::liquid && solid->purpose == BrushPurpose::liquid;
        const bool non_liquid_contract = solid->contents == BrushContents::solid &&
                                         solid->material != MaterialRole::liquid &&
                                         solid->purpose != BrushPurpose::liquid;
        if (!liquid_contract && !non_liquid_contract)
            fail("invalid_liquid_contract", "liquid purpose, material, and contents must agree");
        if (solid->contents != BrushContents::solid && solid->ownership != BrushOwnership::structural)
            fail("invalid_liquid_ownership", "liquid volumes must be structural world brushes");
        if (solid->ownership == BrushOwnership::placement) {
            if (!solid->owner || !placement_ids.contains(*solid->owner))
                fail("invalid_solid_owner", "placement-owned solid has no valid placement owner");
        } else if (solid->owner) {
            fail("invalid_solid_owner", "world/detail solid cannot reference a placement owner");
        }

        count_total_brush_or_fail(options, total_brushes);
        const bool q3_detail = solid->ownership == BrushOwnership::detail && layout.spec.game == Game::quake3;
        TaggedBrush lowered{lower_geometry(solid->geometry, solid->material, solid->contents, q3_detail,
                                           layout.spec.game, options.dialect, options.max_coordinate),
                            source_ref(solid->id)};
        switch (solid->ownership) {
        case BrushOwnership::structural:
            world_brushes.push_back(std::move(lowered));
            ++stats.structural_brushes;
            break;
        case BrushOwnership::detail:
            ++stats.detail_brushes;
            if (layout.spec.game == Game::quake3)
                world_brushes.push_back(std::move(lowered));
            else
                detail_brushes.push_back(std::move(lowered));
            break;
        case BrushOwnership::placement:
            placement_brushes[*solid->owner].push_back(std::move(lowered));
            break;
        default:
            fail("invalid_brush_ownership", "layout solid has an invalid ownership role");
        }
    }

    std::set<PlacementId> referenced_targets;
    for (const Placement *placement : placements) {
        if (placement->link) {
            if (!*placement->link || !placement_ids.contains(*placement->link) || *placement->link == placement->id)
                fail("invalid_placement_link", "placement link must resolve to a different nonzero placement");
            referenced_targets.insert(*placement->link);
        }
    }

    std::vector<PendingEntity> pending;
    PendingEntity world;
    add_property(world.entity, "classname", "worldspawn");
    if (options.dialect == MapDialect::quake1_valve220 || options.dialect == MapDialect::quake2_valve220)
        add_property(world.entity, "mapversion", "220");
    add_property(world.entity, "message", layout.spec.title);
    add_property(world.entity, "_qmg_seed", std::to_string(layout.candidate_seed));
    add_property(world.entity, "_qmg_theme", layout.spec.theme);
    for (TaggedBrush &tagged : world_brushes) {
        world.entity.primitives.push_back(std::move(tagged.brush));
        world.primitive_sources.push_back(tagged.source);
    }
    constexpr std::size_t portable_primitive_limit = 65536;
    if (world_patches.size() > portable_primitive_limit ||
        world.entity.primitives.size() > portable_primitive_limit - world_patches.size())
        fail("total_primitive_limit", "world brushes and patches exceed the portable entity primitive cap");
    for (TaggedPatch &tagged : world_patches) {
        world.entity.primitives.push_back(std::move(tagged.patch));
        world.primitive_sources.push_back(tagged.source);
    }
    pending.push_back(std::move(world));

    if (!detail_brushes.empty()) {
        PendingEntity detail;
        add_property(detail.entity, "classname", "func_detail");
        for (TaggedBrush &tagged : detail_brushes) {
            detail.entity.primitives.push_back(std::move(tagged.brush));
            detail.primitive_sources.push_back(tagged.source);
        }
        pending.push_back(std::move(detail));
    }

    for (const Placement *placement : placements) {
        const auto found = placement_brushes.find(placement->id);
        const std::vector<TaggedBrush> no_brushes;
        const std::vector<TaggedBrush> &owned = found == placement_brushes.end() ? no_brushes : found->second;
        if (owned.empty() && placement->volume && !quake2_point_teleporter(*placement, layout.spec.game))
            count_total_brush_or_fail(options, total_brushes);
        const auto mover = validated_construction.movers.find(placement->id);
        const NativeMoverProperties *native_mover =
            mover == validated_construction.movers.end() ? nullptr : &mover->second;
        pending.push_back(make_placement_entity(*placement, layout.spec.game, options, referenced_targets, owned,
                                                native_mover, stats));
    }

    MapDocument document(options.dialect);
    std::vector<MapEntity> &entities = document.edit_entities();
    entities.reserve(pending.size());
    for (const PendingEntity &entity : pending)
        entities.push_back(entity.entity);
    document.assign_deterministic_node_ids();

    MapValidationOptions validation_options;
    validation_options.profile = MapValidationProfile::generated_document;
    validation_options.max_coordinate = options.max_coordinate;
    const MapValidationResult validation = validate_map_document(document, validation_options);
    if (!validation.ok()) {
        const MapValidationIssue &issue = validation.issues.front();
        fail("generated_document_validation", "[" + issue.code + "] " + issue.message);
    }

    stats.entity_count = document.entities().size();
    for (const MapEntity &entity : document.entities()) {
        for (const MapPrimitive &primitive : entity.primitives) {
            if (const MapBrush *brush = std::get_if<MapBrush>(&primitive)) {
                ++stats.brush_count;
                stats.face_count += brush->faces.size();
            } else
                ++stats.patch_count;
        }
    }
    if (stats.brush_count != total_brushes)
        fail("internal_brush_count", "lowered brush statistics diverged from emitted document");
    if (stats.patch_count != validated_construction.curves.size())
        fail("internal_patch_count", "lowered patch statistics diverged from emitted document");

    std::map<MapBuildSourceRef, std::vector<MapNodeId>> initial_manifest;
    for (const Room *room : rooms)
        initial_manifest[source_ref(room->id)];
    for (const Connection *connection : connections)
        initial_manifest[source_ref(connection->id)];
    for (const LayoutSolid *solid : solids)
        initial_manifest[source_ref(solid->id)];
    for (const LayoutCurve *curve : validated_construction.curves)
        initial_manifest[source_ref(curve->source_id)];
    for (const Placement *placement : placements)
        initial_manifest[source_ref(placement->id)];

    MapBuildResult result{std::move(document), {}, stats};
    result.manifest = make_manifest(result.document, pending, std::move(initial_manifest));
    return result;
}

} // namespace qmg
