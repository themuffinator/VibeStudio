#pragma once

#include "qmg/semantic_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace qmg {

using GridCoord = std::int32_t;

struct GridVec3 {
    GridCoord x{};
    GridCoord y{};
    GridCoord z{};

    auto operator<=>(const GridVec3 &) const = default;
};

[[nodiscard]] inline GridCoord checked_grid_coord(const std::int64_t value) {
    if (value < static_cast<std::int64_t>(std::numeric_limits<GridCoord>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<GridCoord>::max())) {
        throw std::overflow_error("grid coordinate is outside the signed 32-bit range");
    }
    return static_cast<GridCoord>(value);
}

[[nodiscard]] inline GridCoord checked_grid_add(const GridCoord value, const std::int64_t offset) {
    return checked_grid_coord(static_cast<std::int64_t>(value) + offset);
}

[[nodiscard]] inline GridCoord checked_grid_subtract(const GridCoord value, const std::int64_t offset) {
    return checked_grid_coord(static_cast<std::int64_t>(value) - offset);
}

[[nodiscard]] inline GridCoord snap_grid_coord(const double value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("cannot snap a non-finite grid coordinate");
    const double rounded = std::round(value);
    if (rounded < static_cast<double>(std::numeric_limits<GridCoord>::min()) ||
        rounded > static_cast<double>(std::numeric_limits<GridCoord>::max())) {
        throw std::overflow_error("rounded grid coordinate is outside the signed 32-bit range");
    }
    return static_cast<GridCoord>(rounded);
}

[[nodiscard]] inline GridVec3 snap_grid_vec3(const Vec3 &value) {
    return {snap_grid_coord(value.x), snap_grid_coord(value.y), snap_grid_coord(value.z)};
}

[[nodiscard]] inline Vec3 to_vec3(const GridVec3 value) {
    return {static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z)};
}

struct GridBounds {
    GridVec3 min;
    GridVec3 max;

    [[nodiscard]] bool valid() const { return min.x < max.x && min.y < max.y && min.z < max.z; }

    [[nodiscard]] GridVec3 center() const {
        const auto midpoint = [](const GridCoord minimum, const GridCoord maximum) {
            const std::int64_t sum = static_cast<std::int64_t>(minimum) + static_cast<std::int64_t>(maximum);
            const std::int64_t rounded = sum >= 0 ? (sum + 1) / 2 : (sum - 1) / 2;
            return checked_grid_coord(rounded);
        };
        return {midpoint(min.x, max.x), midpoint(min.y, max.y), midpoint(min.z, max.z)};
    }

    [[nodiscard]] bool contains(const GridVec3 point, const GridCoord margin = 0) const {
        if (margin < 0)
            return false;
        const std::int64_t safe_margin = margin;
        return static_cast<std::int64_t>(point.x) >= static_cast<std::int64_t>(min.x) + safe_margin &&
               static_cast<std::int64_t>(point.x) <= static_cast<std::int64_t>(max.x) - safe_margin &&
               static_cast<std::int64_t>(point.y) >= static_cast<std::int64_t>(min.y) + safe_margin &&
               static_cast<std::int64_t>(point.y) <= static_cast<std::int64_t>(max.y) - safe_margin &&
               static_cast<std::int64_t>(point.z) >= static_cast<std::int64_t>(min.z) + safe_margin &&
               static_cast<std::int64_t>(point.z) <= static_cast<std::int64_t>(max.z) - safe_margin;
    }

    auto operator<=>(const GridBounds &) const = default;
};

[[nodiscard]] inline GridBounds snap_grid_bounds(const Bounds &bounds) {
    return {snap_grid_vec3(bounds.min), snap_grid_vec3(bounds.max)};
}

[[nodiscard]] inline Bounds to_bounds(const GridBounds &bounds) { return {to_vec3(bounds.min), to_vec3(bounds.max)}; }

template <typename Tag> struct StrongLayoutId {
    std::uint32_t value{};

    [[nodiscard]] explicit constexpr operator bool() const { return value != 0; }
    auto operator<=>(const StrongLayoutId &) const = default;
};

struct RoomIdTag;
struct ConnectionIdTag;
struct SolidIdTag;
struct PlacementIdTag;

using RoomId = StrongLayoutId<RoomIdTag>;
using ConnectionId = StrongLayoutId<ConnectionIdTag>;
using SolidId = StrongLayoutId<SolidIdTag>;
using PlacementId = StrongLayoutId<PlacementIdTag>;

enum class MaterialRole : std::uint8_t {
    floor,
    ceiling,
    wall,
    platform,
    stair,
    ramp,
    arch,
    curve,
    liquid,
    door,
    lift,
    hidden,
    trigger,
};

enum class BrushPurpose : std::uint8_t {
    structural,
    raised_platform,
    navigation_stair,
    ramp,
    arch,
    curve,
    liquid,
    trigger_volume,
    door,
    lift,
    hidden,
};

enum class BrushOwnership : std::uint8_t {
    structural,
    detail,
    placement,
};

enum class BrushContents : std::uint8_t {
    solid,
    water,
    slime,
    lava,
};

struct PlanePointTriple {
    // The cross product (second - first) x (third - first) points outward.
    // A valid polyhedron's common interior lies on the non-positive side of every ordered plane.
    GridVec3 first;
    GridVec3 second;
    GridVec3 third;

    auto operator<=>(const PlanePointTriple &) const = default;
};

struct ConvexPolyhedron {
    std::vector<PlanePointTriple> planes;

    bool operator==(const ConvexPolyhedron &) const = default;
};

using BrushGeometry = std::variant<GridBounds, ConvexPolyhedron>;

[[nodiscard]] inline const GridBounds *get_box(const BrushGeometry &geometry) {
    return std::get_if<GridBounds>(&geometry);
}

[[nodiscard]] inline const ConvexPolyhedron *get_convex_polyhedron(const BrushGeometry &geometry) {
    return std::get_if<ConvexPolyhedron>(&geometry);
}

[[nodiscard]] inline bool plane_points_are_nondegenerate(const PlanePointTriple &plane) {
    const auto normalized_direction = [](const GridVec3 from, const GridVec3 to) {
        std::array<std::int64_t, 3> direction{
            static_cast<std::int64_t>(to.x) - from.x,
            static_cast<std::int64_t>(to.y) - from.y,
            static_cast<std::int64_t>(to.z) - from.z,
        };
        const auto magnitude = [](const std::int64_t value) {
            return static_cast<std::uint64_t>(value < 0 ? -value : value);
        };
        const std::uint64_t divisor =
            std::gcd(std::gcd(magnitude(direction[0]), magnitude(direction[1])), magnitude(direction[2]));
        if (divisor != 0) {
            for (std::int64_t &component : direction)
                component /= static_cast<std::int64_t>(divisor);
        }
        return direction;
    };

    const std::array<std::int64_t, 3> first = normalized_direction(plane.first, plane.second);
    const std::array<std::int64_t, 3> second = normalized_direction(plane.first, plane.third);
    if (first == std::array<std::int64_t, 3>{} || second == std::array<std::int64_t, 3>{})
        return false;
    const bool same = first == second;
    const bool opposite = first[0] == -second[0] && first[1] == -second[1] && first[2] == -second[2];
    return !same && !opposite;
}

enum class BrushGeometryError : std::uint8_t {
    none,
    invalid_box_bounds,
    too_few_planes,
    degenerate_plane,
    inconsistent_half_spaces,
    unbounded_half_spaces,
    inactive_plane,
    aggregate_bounds_overflow,
    empty_aggregate_bounds,
};

struct BrushGeometryValidation {
    BrushGeometryError error{BrushGeometryError::none};
    std::optional<GridBounds> bounds;
    std::optional<Vec3> interior_witness;

    [[nodiscard]] bool ok() const {
        return error == BrushGeometryError::none && bounds.has_value() && interior_witness.has_value();
    }
};

namespace layout_detail {

struct WideVec3 {
    long double x{};
    long double y{};
    long double z{};
};

[[nodiscard]] inline WideVec3 widen(const GridVec3 value) {
    return {static_cast<long double>(value.x), static_cast<long double>(value.y), static_cast<long double>(value.z)};
}

[[nodiscard]] inline WideVec3 operator+(const WideVec3 a, const WideVec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] inline WideVec3 operator-(const WideVec3 a, const WideVec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] inline WideVec3 operator*(const WideVec3 value, const long double scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] inline WideVec3 operator/(const WideVec3 value, const long double scale) {
    return {value.x / scale, value.y / scale, value.z / scale};
}

[[nodiscard]] inline long double dot(const WideVec3 a, const WideVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

[[nodiscard]] inline WideVec3 cross(const WideVec3 a, const WideVec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline long double length(const WideVec3 value) { return std::sqrt(dot(value, value)); }

struct HalfSpace {
    WideVec3 normal;
    long double distance{};
};

[[nodiscard]] inline bool normals_bound_space(const std::vector<HalfSpace> &planes) {
    // A half-space intersection is bounded exactly when its recession cone
    // { d | dot(normal, d) <= 0 for every plane } contains only the origin.
    // In three dimensions every non-trivial polyhedral recession cone has a
    // ray parallel to the intersection of two independent boundary planes.
    // Testing both directions of every such cross product avoids assuming
    // that four normals alone positively span space; valid prisms and boxes
    // commonly require five or six normals respectively.
    constexpr long double independent_epsilon = 1.0e-12L;
    constexpr long double inside_epsilon = 1.0e-10L;
    bool found_independent_pair = false;
    for (std::size_t first = 0; first + 1 < planes.size(); ++first) {
        for (std::size_t second = first + 1; second < planes.size(); ++second) {
            WideVec3 direction = cross(planes[first].normal, planes[second].normal);
            const long double direction_length = length(direction);
            if (!(direction_length > independent_epsilon) || !std::isfinite(direction_length))
                continue;
            found_independent_pair = true;
            direction = direction / direction_length;
            for (const long double sign : {-1.0L, 1.0L}) {
                const WideVec3 candidate = direction * sign;
                const bool recession_direction = std::all_of(planes.begin(), planes.end(), [&](const HalfSpace &plane) {
                    return dot(plane.normal, candidate) <= inside_epsilon;
                });
                if (recession_direction)
                    return false;
            }
        }
    }
    return found_independent_pair;
}

[[nodiscard]] inline bool same_vertex(const WideVec3 a, const WideVec3 b) {
    constexpr long double epsilon = 1.0e-7L;
    return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon && std::abs(a.z - b.z) <= epsilon;
}

[[nodiscard]] inline bool plane_has_face(const HalfSpace &plane, const std::vector<WideVec3> &vertices) {
    constexpr long double on_plane_epsilon = 1.0e-6L;
    std::vector<WideVec3> on_plane;
    for (const WideVec3 vertex : vertices) {
        if (std::abs(dot(plane.normal, vertex) - plane.distance) <=
            on_plane_epsilon * (1.0L + std::abs(plane.distance))) {
            on_plane.push_back(vertex);
        }
    }
    for (std::size_t first = 0; first + 2 < on_plane.size(); ++first) {
        for (std::size_t second = first + 1; second + 1 < on_plane.size(); ++second) {
            for (std::size_t third = second + 1; third < on_plane.size(); ++third) {
                if (length(cross(on_plane[second] - on_plane[first], on_plane[third] - on_plane[first])) >
                    on_plane_epsilon) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace layout_detail

[[nodiscard]] inline BrushGeometryValidation validate_brush_geometry(const BrushGeometry &geometry) {
    if (const GridBounds *box = get_box(geometry)) {
        if (!box->valid())
            return {BrushGeometryError::invalid_box_bounds, std::nullopt, std::nullopt};
        const Vec3 witness{
            (static_cast<double>(box->min.x) + box->max.x) / 2.0,
            (static_cast<double>(box->min.y) + box->max.y) / 2.0,
            (static_cast<double>(box->min.z) + box->max.z) / 2.0,
        };
        return {BrushGeometryError::none, *box, witness};
    }

    using namespace layout_detail;
    const ConvexPolyhedron &polyhedron = std::get<ConvexPolyhedron>(geometry);
    if (polyhedron.planes.size() < 4)
        return {BrushGeometryError::too_few_planes, std::nullopt, std::nullopt};

    std::vector<HalfSpace> planes;
    planes.reserve(polyhedron.planes.size());
    for (const PlanePointTriple &plane : polyhedron.planes) {
        if (!plane_points_are_nondegenerate(plane))
            return {BrushGeometryError::degenerate_plane, std::nullopt, std::nullopt};
        const WideVec3 first = widen(plane.first);
        const WideVec3 normal = cross(widen(plane.second) - first, widen(plane.third) - first);
        const long double normal_length = length(normal);
        if (!(normal_length > 0.0L) || !std::isfinite(normal_length))
            return {BrushGeometryError::degenerate_plane, std::nullopt, std::nullopt};
        const WideVec3 unit_normal = normal / normal_length;
        planes.push_back({unit_normal, dot(unit_normal, first)});
    }

    if (!normals_bound_space(planes))
        return {BrushGeometryError::unbounded_half_spaces, std::nullopt, std::nullopt};

    constexpr long double determinant_epsilon = 1.0e-10L;
    constexpr long double inside_epsilon = 1.0e-7L;
    std::vector<WideVec3> vertices;
    for (std::size_t first = 0; first + 2 < planes.size(); ++first) {
        for (std::size_t second = first + 1; second + 1 < planes.size(); ++second) {
            for (std::size_t third = second + 1; third < planes.size(); ++third) {
                const WideVec3 second_cross_third = cross(planes[second].normal, planes[third].normal);
                const long double determinant = dot(planes[first].normal, second_cross_third);
                if (std::abs(determinant) <= determinant_epsilon)
                    continue;
                const WideVec3 intersection =
                    (second_cross_third * planes[first].distance +
                     cross(planes[third].normal, planes[first].normal) * planes[second].distance +
                     cross(planes[first].normal, planes[second].normal) * planes[third].distance) /
                    determinant;
                const bool inside = std::all_of(planes.begin(), planes.end(), [intersection](const HalfSpace &plane) {
                    return dot(plane.normal, intersection) - plane.distance <=
                           inside_epsilon * (1.0L + std::abs(plane.distance));
                });
                if (inside && std::none_of(vertices.begin(), vertices.end(), [intersection](const WideVec3 vertex) {
                        return same_vertex(vertex, intersection);
                    })) {
                    vertices.push_back(intersection);
                }
            }
        }
    }

    if (vertices.size() < 4)
        return {BrushGeometryError::inconsistent_half_spaces, std::nullopt, std::nullopt};
    if (std::any_of(planes.begin(), planes.end(),
                    [&vertices](const HalfSpace &plane) { return !plane_has_face(plane, vertices); })) {
        return {BrushGeometryError::inactive_plane, std::nullopt, std::nullopt};
    }

    long double minimum_x = vertices.front().x;
    long double minimum_y = vertices.front().y;
    long double minimum_z = vertices.front().z;
    long double maximum_x = minimum_x;
    long double maximum_y = minimum_y;
    long double maximum_z = minimum_z;
    for (const WideVec3 vertex : vertices) {
        minimum_x = std::min(minimum_x, vertex.x);
        minimum_y = std::min(minimum_y, vertex.y);
        minimum_z = std::min(minimum_z, vertex.z);
        maximum_x = std::max(maximum_x, vertex.x);
        maximum_y = std::max(maximum_y, vertex.y);
        maximum_z = std::max(maximum_z, vertex.z);
    }
    constexpr long double extent_epsilon = 1.0e-7L;
    if (maximum_x - minimum_x <= extent_epsilon || maximum_y - minimum_y <= extent_epsilon ||
        maximum_z - minimum_z <= extent_epsilon) {
        return {BrushGeometryError::empty_aggregate_bounds, std::nullopt, std::nullopt};
    }

    const long double rounded_minimum_x = std::floor(minimum_x);
    const long double rounded_minimum_y = std::floor(minimum_y);
    const long double rounded_minimum_z = std::floor(minimum_z);
    const long double rounded_maximum_x = std::ceil(maximum_x);
    const long double rounded_maximum_y = std::ceil(maximum_y);
    const long double rounded_maximum_z = std::ceil(maximum_z);
    constexpr long double grid_minimum = static_cast<long double>(std::numeric_limits<GridCoord>::min());
    constexpr long double grid_maximum = static_cast<long double>(std::numeric_limits<GridCoord>::max());
    if (rounded_minimum_x < grid_minimum || rounded_minimum_y < grid_minimum || rounded_minimum_z < grid_minimum ||
        rounded_maximum_x > grid_maximum || rounded_maximum_y > grid_maximum || rounded_maximum_z > grid_maximum) {
        return {BrushGeometryError::aggregate_bounds_overflow, std::nullopt, std::nullopt};
    }

    const GridBounds bounds{{static_cast<GridCoord>(rounded_minimum_x), static_cast<GridCoord>(rounded_minimum_y),
                             static_cast<GridCoord>(rounded_minimum_z)},
                            {static_cast<GridCoord>(rounded_maximum_x), static_cast<GridCoord>(rounded_maximum_y),
                             static_cast<GridCoord>(rounded_maximum_z)}};
    if (!bounds.valid())
        return {BrushGeometryError::empty_aggregate_bounds, std::nullopt, std::nullopt};
    WideVec3 witness;
    for (const WideVec3 vertex : vertices)
        witness = witness + vertex;
    witness = witness / static_cast<long double>(vertices.size());
    return {BrushGeometryError::none, bounds,
            Vec3{static_cast<double>(witness.x), static_cast<double>(witness.y), static_cast<double>(witness.z)}};
}

[[nodiscard]] inline GridBounds checked_brush_bounds(const BrushGeometry &geometry) {
    const BrushGeometryValidation validation = validate_brush_geometry(geometry);
    if (!validation.ok())
        throw std::invalid_argument("brush geometry is not a bounded, closed convex volume");
    return *validation.bounds;
}

enum class PlacementRole : std::uint8_t {
    player_start,
    deathmatch_spawn,
    team_initial_spawn,
    team_respawn,
    weapon,
    health,
    armor,
    monster,
    flag,
    light,
    exit_trigger,
    exit_destination,
    door,
    door_trigger,
    door_destination,
    lift,
    lift_trigger,
    lift_destination,
    teleporter_trigger,
    teleporter_destination,
    // D8 R1-s role (change r1s-q1-intermission): the camera point FindIntermission moves every client to at the
    // end of a level or match. Quake 1 only for now (switch r1s.q1_intermission). Appended last so the existing
    // enumerator values, which the layout fingerprint encodes, keep their numbers.
    intermission,
    // D8 R1-s role (change r1s-sp-coop-starts): a co-op spawn point, info_player_coop. Quake 1 and Quake 2 only
    // (switch r1s.sp_coop_starts). Appended last for the same reason as intermission.
    coop_start,
};

enum class Team : std::uint8_t {
    red,
    blue,
};

struct RgbColor {
    std::uint16_t red_milli{};
    std::uint16_t green_milli{};
    std::uint16_t blue_milli{};

    [[nodiscard]] bool valid() const { return red_milli <= 1000 && green_milli <= 1000 && blue_milli <= 1000; }
    auto operator<=>(const RgbColor &) const = default;
};

struct Room {
    RoomId id;
    std::string name;
    GridBounds bounds;
    std::vector<std::string> tags;
};

struct Connection {
    ConnectionId id;
    RoomId from_room;
    RoomId to_room;
    GridCoord width{192};
    GridCoord height{192};
    std::vector<GridBounds> passage_volumes;
};

struct LayoutSolid {
    SolidId id;
    BrushGeometry geometry;
    MaterialRole material{MaterialRole::wall};
    BrushPurpose purpose{BrushPurpose::structural};
    BrushOwnership ownership{BrushOwnership::structural};
    BrushContents contents{BrushContents::solid};
    std::optional<PlacementId> owner;

    bool operator==(const LayoutSolid &) const = default;
};

struct Placement {
    PlacementId id;
    PlacementRole role{PlacementRole::health};
    GridVec3 origin;
    std::optional<GridBounds> volume;
    std::optional<Team> team;
    std::uint16_t variant{};
    std::optional<std::int16_t> angle_degrees;
    // View pitch in degrees, positive looking down; only an intermission camera carries one (its "mangle" is
    // pitch, yaw from angle_degrees, roll 0).
    std::optional<std::int16_t> pitch_degrees;
    std::optional<std::int32_t> light_intensity;
    std::optional<RgbColor> light_color;
    std::optional<std::string> destination_map;
    std::optional<PlacementId> link;

    bool operator==(const Placement &) const = default;
};

struct Layout {
    PromptSpec spec;
    std::vector<Room> rooms;
    std::vector<Connection> connections;
    std::vector<LayoutSolid> solids;
    std::vector<Placement> placements;
    double planned_verticality{0.5};
    double planned_intensity{0.5};
    LightingStyle lighting_style{LightingStyle::balanced};
    std::uint64_t candidate_seed{};
    double rank_score{};
};

[[nodiscard]] inline const Room *find_room(const Layout &layout, const RoomId id) {
    for (const Room &room : layout.rooms) {
        if (room.id == id)
            return &room;
    }
    return nullptr;
}

[[nodiscard]] inline Room *find_room(Layout &layout, const RoomId id) {
    for (Room &room : layout.rooms) {
        if (room.id == id)
            return &room;
    }
    return nullptr;
}

[[nodiscard]] inline const Placement *find_placement(const Layout &layout, const PlacementId id) {
    for (const Placement &placement : layout.placements) {
        if (placement.id == id)
            return &placement;
    }
    return nullptr;
}

[[nodiscard]] inline Placement *find_placement(Layout &layout, const PlacementId id) {
    for (Placement &placement : layout.placements) {
        if (placement.id == id)
            return &placement;
    }
    return nullptr;
}

[[nodiscard]] inline std::optional<std::size_t> room_index(const Layout &layout, const RoomId id) {
    for (std::size_t index = 0; index < layout.rooms.size(); ++index) {
        if (layout.rooms[index].id == id)
            return index;
    }
    return std::nullopt;
}

template <typename Id, typename Range, typename Projection>
[[nodiscard]] inline Id next_layout_id(const Range &range, Projection projection) {
    std::uint32_t maximum = 0;
    for (const auto &item : range)
        maximum = std::max(maximum, projection(item).value);
    if (maximum == std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("layout identifier space is exhausted");
    return Id{maximum + 1};
}

[[nodiscard]] inline RoomId next_room_id(const Layout &layout) {
    return next_layout_id<RoomId>(layout.rooms, [](const Room &room) { return room.id; });
}

[[nodiscard]] inline ConnectionId next_connection_id(const Layout &layout) {
    return next_layout_id<ConnectionId>(layout.connections, [](const Connection &connection) { return connection.id; });
}

[[nodiscard]] inline SolidId next_solid_id(const Layout &layout) {
    return next_layout_id<SolidId>(layout.solids, [](const LayoutSolid &solid) { return solid.id; });
}

[[nodiscard]] inline PlacementId next_placement_id(const Layout &layout) {
    return next_layout_id<PlacementId>(layout.placements, [](const Placement &placement) { return placement.id; });
}

} // namespace qmg
