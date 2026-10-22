#include "qmg/construction.hpp"
#include "qmg/quake2_mechanisms.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace qmg {
namespace {

[[noreturn]] void fail(std::string code, std::string message) {
    throw ConstructionError(std::move(code), std::move(message));
}

[[nodiscard]] bool valid_generated_ownership(const BrushOwnership ownership) {
    return ownership == BrushOwnership::structural || ownership == BrushOwnership::detail;
}

void require_ownership(const BrushOwnership ownership) {
    if (!valid_generated_ownership(ownership))
        fail("invalid_ownership", "constructed world geometry must be structural or detail-owned");
}

[[nodiscard]] bool within_limit(const GridCoord coordinate, const GridCoord limit) {
    return static_cast<std::int64_t>(coordinate) >= -static_cast<std::int64_t>(limit) &&
           static_cast<std::int64_t>(coordinate) <= static_cast<std::int64_t>(limit);
}

void require_point_limit(const GridVec3 point, const ConstructionLimits &limits) {
    if (!within_limit(point.x, limits.max_coordinate) || !within_limit(point.y, limits.max_coordinate) ||
        !within_limit(point.z, limits.max_coordinate)) {
        fail("coordinate_limit", "constructed geometry exceeds the configured coordinate limit");
    }
}

void require_bounds(const GridBounds &bounds, const ConstructionLimits &limits) {
    if (!bounds.valid())
        fail("invalid_bounds", "construction bounds must have positive volume");
    require_point_limit(bounds.min, limits);
    require_point_limit(bounds.max, limits);
}

[[nodiscard]] GridCoord multiply_coord(const GridCoord value, const std::uint32_t count) {
    const std::int64_t product = static_cast<std::int64_t>(value) * static_cast<std::int64_t>(count);
    try {
        return checked_grid_coord(product);
    } catch (const std::overflow_error &) {
        fail("coordinate_overflow", "construction dimension multiplication overflowed the integer grid");
    }
}

[[nodiscard]] GridCoord add_coord(const GridCoord value, const std::int64_t offset) {
    try {
        return checked_grid_add(value, offset);
    } catch (const std::overflow_error &) {
        fail("coordinate_overflow", "construction coordinate overflowed the integer grid");
    }
}

[[nodiscard]] GridVec3 add_vec(const GridVec3 left, const GridVec3 right) {
    return {add_coord(left.x, right.x), add_coord(left.y, right.y), add_coord(left.z, right.z)};
}

[[nodiscard]] GridVec3 scale_and_add(const GridVec3 origin, const GridVec3 first, const std::int64_t first_scale,
                                     const GridVec3 second = {}, const std::int64_t second_scale = 0,
                                     const std::int64_t z_offset = 0) {
    return {
        add_coord(origin.x, static_cast<std::int64_t>(first.x) * first_scale +
                                static_cast<std::int64_t>(second.x) * second_scale),
        add_coord(origin.y, static_cast<std::int64_t>(first.y) * first_scale +
                                static_cast<std::int64_t>(second.y) * second_scale),
        add_coord(origin.z, static_cast<std::int64_t>(first.z) * first_scale +
                                static_cast<std::int64_t>(second.z) * second_scale + z_offset),
    };
}

struct DirectionBasis {
    GridVec3 longitudinal;
    GridVec3 transverse;
};

[[nodiscard]] DirectionBasis direction_basis(const HorizontalDirection direction) {
    switch (direction) {
    case HorizontalDirection::positive_x:
        return {{1, 0, 0}, {0, 1, 0}};
    case HorizontalDirection::negative_x:
        return {{-1, 0, 0}, {0, 1, 0}};
    case HorizontalDirection::positive_y:
        return {{0, 1, 0}, {1, 0, 0}};
    case HorizontalDirection::negative_y:
        return {{0, -1, 0}, {1, 0, 0}};
    }
    fail("invalid_direction", "construction has an invalid horizontal direction");
}

[[nodiscard]] GridBounds bounds_from_points(const std::initializer_list<GridVec3> points) {
    if (points.size() == 0)
        fail("internal_geometry", "cannot derive bounds from an empty point set");
    auto iterator = points.begin();
    GridVec3 minimum = *iterator;
    GridVec3 maximum = *iterator;
    for (++iterator; iterator != points.end(); ++iterator) {
        minimum.x = std::min(minimum.x, iterator->x);
        minimum.y = std::min(minimum.y, iterator->y);
        minimum.z = std::min(minimum.z, iterator->z);
        maximum.x = std::max(maximum.x, iterator->x);
        maximum.y = std::max(maximum.y, iterator->y);
        maximum.z = std::max(maximum.z, iterator->z);
    }
    return {minimum, maximum};
}

[[nodiscard]] PlanePointTriple oriented_plane(const GridVec3 first, GridVec3 second, GridVec3 third,
                                              const Vec3 interior) {
    const long double ax = static_cast<long double>(second.x) - first.x;
    const long double ay = static_cast<long double>(second.y) - first.y;
    const long double az = static_cast<long double>(second.z) - first.z;
    const long double bx = static_cast<long double>(third.x) - first.x;
    const long double by = static_cast<long double>(third.y) - first.y;
    const long double bz = static_cast<long double>(third.z) - first.z;
    const long double nx = ay * bz - az * by;
    const long double ny = az * bx - ax * bz;
    const long double nz = ax * by - ay * bx;
    if (nx == 0.0L && ny == 0.0L && nz == 0.0L)
        fail("degenerate_geometry", "constructed face points are collinear");
    const long double side = nx * (static_cast<long double>(interior.x) - first.x) +
                             ny * (static_cast<long double>(interior.y) - first.y) +
                             nz * (static_cast<long double>(interior.z) - first.z);
    if (side == 0.0L)
        fail("degenerate_geometry", "constructed face does not strictly bound its interior witness");
    if (side > 0.0L)
        std::swap(second, third);
    return {first, second, third};
}

void require_geometry(const BrushGeometry &geometry, const ConstructionLimits &limits) {
    const BrushGeometryValidation validation = validate_brush_geometry(geometry);
    if (!validation.ok() || !validation.bounds)
        fail("invalid_generated_geometry", "construction did not produce a bounded closed convex volume");
    require_bounds(*validation.bounds, limits);
}

struct Point2 {
    GridCoord first{};
    GridCoord second{};

    auto operator<=>(const Point2 &) const = default;
};

[[nodiscard]] std::array<std::size_t, 3> cap_triangle(const std::vector<Point2> &profile) {
    for (std::size_t first = 0; first + 2 < profile.size(); ++first) {
        for (std::size_t second = first + 1; second + 1 < profile.size(); ++second) {
            for (std::size_t third = second + 1; third < profile.size(); ++third) {
                const std::int64_t ax = static_cast<std::int64_t>(profile[second].first) - profile[first].first;
                const std::int64_t ay = static_cast<std::int64_t>(profile[second].second) - profile[first].second;
                const std::int64_t bx = static_cast<std::int64_t>(profile[third].first) - profile[first].first;
                const std::int64_t by = static_cast<std::int64_t>(profile[third].second) - profile[first].second;
                if (ax * by - ay * bx != 0)
                    return {first, second, third};
            }
        }
    }
    fail("degenerate_profile", "construction profile has no non-collinear points");
}

template <typename Mapper>
[[nodiscard]] ConvexPolyhedron extrude_profile(const std::vector<Point2> &profile, Mapper mapper,
                                               const GridCoord depth) {
    if (profile.size() < 3 || depth <= 0)
        fail("invalid_profile", "an extrusion requires a polygon and positive depth");
    std::vector<GridVec3> front;
    std::vector<GridVec3> back;
    front.reserve(profile.size());
    back.reserve(profile.size());
    for (const Point2 point : profile) {
        front.push_back(mapper(point, GridCoord{0}));
        back.push_back(mapper(point, depth));
    }

    long double witness_x = 0.0L;
    long double witness_y = 0.0L;
    long double witness_z = 0.0L;
    for (std::size_t index = 0; index < profile.size(); ++index) {
        witness_x += (static_cast<long double>(front[index].x) + back[index].x) / 2.0L;
        witness_y += (static_cast<long double>(front[index].y) + back[index].y) / 2.0L;
        witness_z += (static_cast<long double>(front[index].z) + back[index].z) / 2.0L;
    }
    const long double divisor = static_cast<long double>(profile.size());
    const Vec3 witness{static_cast<double>(witness_x / divisor), static_cast<double>(witness_y / divisor),
                       static_cast<double>(witness_z / divisor)};

    const std::array<std::size_t, 3> triangle = cap_triangle(profile);
    ConvexPolyhedron polyhedron;
    polyhedron.planes.reserve(profile.size() + 2);
    polyhedron.planes.push_back(oriented_plane(front[triangle[0]], front[triangle[1]], front[triangle[2]], witness));
    polyhedron.planes.push_back(oriented_plane(back[triangle[0]], back[triangle[1]], back[triangle[2]], witness));
    for (std::size_t index = 0; index < profile.size(); ++index) {
        const std::size_t next = (index + 1) % profile.size();
        polyhedron.planes.push_back(oriented_plane(front[index], front[next], back[index], witness));
    }
    return polyhedron;
}

[[nodiscard]] std::int64_t round_divide(const std::int64_t numerator, const std::int64_t denominator) {
    if (denominator <= 0)
        fail("internal_arithmetic", "rounding denominator must be positive");
    if (numerator >= 0)
        return (numerator + denominator / 2) / denominator;
    return -((-numerator + denominator / 2) / denominator);
}

[[nodiscard]] std::uint64_t nearest_integer_sqrt(const std::uint64_t value) {
    std::uint64_t low = 0;
    std::uint64_t high = value < 2 ? value : value / 2 + 1;
    while (low < high) {
        const std::uint64_t middle = low + (high - low + 1) / 2;
        if (middle <= value / middle)
            low = middle;
        else
            high = middle - 1;
    }
    const std::uint64_t lower_square = low * low;
    const std::uint64_t upper = low + 1;
    const std::uint64_t upper_square = upper * upper;
    return upper_square - value <= value - lower_square ? upper : low;
}

[[nodiscard]] Point2 half_circle_point(const GridCoord radius, const std::uint32_t index,
                                       const std::uint32_t divisions) {
    const std::int64_t signed_fraction = static_cast<std::int64_t>(2) * index - divisions;
    const GridCoord horizontal =
        checked_grid_coord(round_divide(static_cast<std::int64_t>(radius) * signed_fraction, divisions));
    const std::int64_t squared =
        static_cast<std::int64_t>(radius) * radius - static_cast<std::int64_t>(horizontal) * horizontal;
    if (squared < 0)
        fail("internal_arithmetic", "quantized arch point escaped its radius");
    const GridCoord vertical =
        checked_grid_coord(static_cast<std::int64_t>(nearest_integer_sqrt(static_cast<std::uint64_t>(squared))));
    return {horizontal, vertical};
}

[[nodiscard]] Point2 quarter_circle_point(const GridCoord radius, const std::uint32_t index,
                                          const std::uint32_t divisions) {
    const GridCoord first = checked_grid_coord(
        round_divide(static_cast<std::int64_t>(radius) * (static_cast<std::int64_t>(divisions) - index), divisions));
    const std::int64_t squared = static_cast<std::int64_t>(radius) * radius - static_cast<std::int64_t>(first) * first;
    if (squared < 0)
        fail("internal_arithmetic", "quantized curve point escaped its radius");
    const GridCoord second =
        checked_grid_coord(static_cast<std::int64_t>(nearest_integer_sqrt(static_cast<std::uint64_t>(squared))));
    return {first, second};
}

[[nodiscard]] Point2 transform_quadrant(Point2 point, const CurveQuadrant quadrant) {
    switch (quadrant) {
    case CurveQuadrant::positive_x_positive_y:
        return point;
    case CurveQuadrant::negative_x_positive_y:
        return {checked_grid_coord(-static_cast<std::int64_t>(point.first)), point.second};
    case CurveQuadrant::negative_x_negative_y:
        return {checked_grid_coord(-static_cast<std::int64_t>(point.first)),
                checked_grid_coord(-static_cast<std::int64_t>(point.second))};
    case CurveQuadrant::positive_x_negative_y:
        return {point.first, checked_grid_coord(-static_cast<std::int64_t>(point.second))};
    }
    fail("invalid_quadrant", "curved wall has an invalid quadrant");
}

[[nodiscard]] GridVec3 curve_world_point(const GridVec3 center, const Point2 offset, const GridCoord z_offset) {
    return {add_coord(center.x, offset.first), add_coord(center.y, offset.second), add_coord(center.z, z_offset)};
}

[[nodiscard]] ConstructionKey request_key(const ConstructionRequest &request) {
    return std::visit([](const auto &spec) { return spec.key; }, request);
}

struct IdAllocator {
    std::uint32_t next_solid{};
    std::uint32_t next_placement{};

    [[nodiscard]] SolidId solid() {
        if (next_solid == 0 || next_solid == std::numeric_limits<std::uint32_t>::max())
            fail("solid_id_exhausted", "construction exhausted the SolidId namespace");
        return SolidId{next_solid++};
    }

    [[nodiscard]] PlacementId placement() {
        if (next_placement == 0 || next_placement == std::numeric_limits<std::uint32_t>::max())
            fail("placement_id_exhausted", "construction exhausted the PlacementId namespace");
        return PlacementId{next_placement++};
    }
};

template <typename Range, typename Projection>
[[nodiscard]] std::uint32_t checked_next_id(const Range &range, Projection projection, const char *kind) {
    std::set<std::uint32_t> identifiers;
    std::uint32_t maximum = 0;
    for (const auto &item : range) {
        const std::uint32_t identifier = projection(item).value;
        if (identifier == 0 || !identifiers.insert(identifier).second)
            fail("invalid_base_ids", std::string("base layout has invalid or duplicate ") + kind + " identifiers");
        maximum = std::max(maximum, identifier);
    }
    if (maximum == std::numeric_limits<std::uint32_t>::max())
        fail("id_exhausted", std::string("base layout has exhausted the ") + kind + " identifier namespace");
    return maximum + 1;
}

[[nodiscard]] std::size_t curve_backing_count(const ConstructionResult &result) {
    std::size_t count = 0;
    for (const LayoutCurve &curve : result.curves) {
        if (curve.collision_backing_solids.size() > std::numeric_limits<std::size_t>::max() - count)
            fail("solid_limit", "construction solid count overflowed");
        count += curve.collision_backing_solids.size();
    }
    return count;
}

void require_solid_capacity(const ConstructionResult &result, const std::size_t additional,
                            const ConstructionLimits &limits) {
    const std::size_t current = result.solids.size() + curve_backing_count(result);
    if (additional > limits.max_generated_solids || current > limits.max_generated_solids - additional)
        fail("solid_limit", "construction exceeds the configured generated-solid cap");
}

[[nodiscard]] LayoutSolid make_solid(const SolidId id, BrushGeometry geometry, const MaterialRole material,
                                     const BrushPurpose purpose, const BrushOwnership ownership,
                                     const BrushContents contents, const std::optional<PlacementId> owner,
                                     const ConstructionLimits &limits) {
    require_geometry(geometry, limits);
    return {id, std::move(geometry), material, purpose, ownership, contents, owner};
}

void require_segment_count(const std::uint32_t count, const GridCoord inner_radius, const ConstructionLimits &limits) {
    if (count == 0 || static_cast<std::size_t>(count) > limits.max_segments_per_feature)
        fail("segment_limit", "segment count is zero or exceeds the configured cap");
    if (inner_radius <= 0 || static_cast<std::uint64_t>(inner_radius) < count)
        fail("segment_resolution", "radius is too small for distinct integer-grid segments");
}

} // namespace

ConstructionError::ConstructionError(std::string code, std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

const LayoutCurveControlPoint &LayoutCurve::control(const std::size_t column, const std::size_t row) const {
    if (column >= width || row >= height)
        throw std::out_of_range("layout curve control index is out of range");
    return control_points[column * static_cast<std::size_t>(height) + row];
}

namespace {

class BuildState {
  public:
    BuildState(IdAllocator allocator, ConstructionResult result, const ConstructionLimits &limits, const Game game)
        : ids_(allocator), result_(std::move(result)), limits_(limits), game_(game) {}

    void add(const StairRunSpec &spec) {
        require_key(spec.key);
        require_ownership(spec.ownership);
        if (spec.width <= 0 || spec.tread_depth <= 0 || spec.riser_height <= 0 || spec.step_count == 0)
            fail("invalid_stair_dimensions", "stairs require positive width, tread, riser, and step count");
        if (static_cast<std::size_t>(spec.step_count) > limits_.max_segments_per_feature)
            fail("segment_limit", "stair count exceeds the configured segment cap");
        require_solid_capacity(result_, spec.step_count, limits_);

        const DirectionBasis basis = direction_basis(spec.ascent);
        const GridCoord total_run = multiply_coord(spec.tread_depth, spec.step_count);
        const GridCoord total_rise = multiply_coord(spec.riser_height, spec.step_count);
        const GridVec3 far_corner =
            scale_and_add(spec.start, basis.longitudinal, total_run, basis.transverse, spec.width, total_rise);
        require_point_limit(spec.start, limits_);
        require_point_limit(far_corner, limits_);

        for (std::uint32_t step = 0; step < spec.step_count; ++step) {
            const GridCoord near_distance = multiply_coord(spec.tread_depth, step);
            const GridCoord far_distance = multiply_coord(spec.tread_depth, step + 1);
            const GridCoord top_offset = multiply_coord(spec.riser_height, step + 1);
            const GridVec3 near = scale_and_add(spec.start, basis.longitudinal, near_distance);
            const GridVec3 far = scale_and_add(spec.start, basis.longitudinal, far_distance);
            const GridVec3 near_wide = scale_and_add(near, basis.transverse, spec.width);
            const GridVec3 far_wide = scale_and_add(far, basis.transverse, spec.width);
            const GridBounds bounds = bounds_from_points(
                {near, far, near_wide, far_wide, add_vec(near, {0, 0, top_offset}), add_vec(far, {0, 0, top_offset}),
                 add_vec(near_wide, {0, 0, top_offset}), add_vec(far_wide, {0, 0, top_offset})});
            result_.solids.push_back(make_solid(ids_.solid(), bounds, MaterialRole::stair,
                                                BrushPurpose::navigation_stair, spec.ownership, BrushContents::solid,
                                                std::nullopt, limits_));
        }
    }

    void add(const RampSpec &spec) {
        require_key(spec.key);
        require_ownership(spec.ownership);
        if (spec.width <= 0 || spec.run <= 0 || spec.low_height <= 0 || spec.rise <= 0)
            fail("invalid_ramp_dimensions", "a ramp requires positive width, run, low height, and rise");
        require_solid_capacity(result_, 1, limits_);
        const DirectionBasis basis = direction_basis(spec.direction);

        const GridVec3 bottom_low_near = spec.start;
        const GridVec3 bottom_low_far = scale_and_add(spec.start, basis.transverse, spec.width);
        const GridVec3 bottom_high_near = scale_and_add(spec.start, basis.longitudinal, spec.run);
        const GridVec3 bottom_high_far =
            scale_and_add(spec.start, basis.longitudinal, spec.run, basis.transverse, spec.width);
        const GridVec3 top_low_near = add_vec(bottom_low_near, {0, 0, spec.low_height});
        const GridVec3 top_low_far = add_vec(bottom_low_far, {0, 0, spec.low_height});
        const GridCoord high_height = add_coord(spec.low_height, spec.rise);
        const GridVec3 top_high_near = add_vec(bottom_high_near, {0, 0, high_height});

        const Vec3 witness{
            static_cast<double>(spec.start.x) + (static_cast<double>(basis.longitudinal.x) * spec.run +
                                                 static_cast<double>(basis.transverse.x) * spec.width) /
                                                    2.0,
            static_cast<double>(spec.start.y) + (static_cast<double>(basis.longitudinal.y) * spec.run +
                                                 static_cast<double>(basis.transverse.y) * spec.width) /
                                                    2.0,
            static_cast<double>(spec.start.z) + static_cast<double>(spec.low_height) / 2.0,
        };
        ConvexPolyhedron ramp;
        ramp.planes.reserve(6);
        ramp.planes.push_back(oriented_plane(bottom_low_near, bottom_low_far, top_low_near, witness));
        ramp.planes.push_back(oriented_plane(bottom_high_near, top_high_near, bottom_high_far, witness));
        ramp.planes.push_back(oriented_plane(bottom_low_near, top_low_near, bottom_high_near, witness));
        ramp.planes.push_back(oriented_plane(bottom_low_far, bottom_high_far, top_low_far, witness));
        ramp.planes.push_back(oriented_plane(bottom_low_near, bottom_high_near, bottom_low_far, witness));
        ramp.planes.push_back(oriented_plane(top_low_near, top_low_far, top_high_near, witness));
        result_.solids.push_back(make_solid(ids_.solid(), std::move(ramp), MaterialRole::ramp, BrushPurpose::ramp,
                                            spec.ownership, BrushContents::solid, std::nullopt, limits_));
    }

    void add(const ArchSpec &spec) {
        require_key(spec.key);
        require_ownership(spec.ownership);
        require_segment_count(spec.segment_count, spec.inner_radius, limits_);
        if (spec.segment_count < 2)
            fail("segment_resolution", "an arch requires at least two convex segments");
        if (spec.depth <= 0 || spec.ring_thickness <= 0)
            fail("invalid_arch_dimensions", "an arch requires positive depth and ring thickness");
        const GridCoord outer_radius = add_coord(spec.inner_radius, spec.ring_thickness);
        if (outer_radius <= spec.inner_radius)
            fail("invalid_arch_dimensions", "arch outer radius must exceed its inner radius");
        if (spec.axis != ArchAxis::along_x && spec.axis != ArchAxis::along_y)
            fail("invalid_arch_axis", "arch has an invalid extrusion axis");
        require_solid_capacity(result_, spec.segment_count, limits_);

        for (std::uint32_t segment = 0; segment < spec.segment_count; ++segment) {
            const Point2 inner_first = half_circle_point(spec.inner_radius, segment, spec.segment_count);
            const Point2 inner_second = half_circle_point(spec.inner_radius, segment + 1, spec.segment_count);
            const Point2 outer_first = half_circle_point(outer_radius, segment, spec.segment_count);
            const Point2 outer_second = half_circle_point(outer_radius, segment + 1, spec.segment_count);
            const std::vector<Point2> profile{inner_first, inner_second, outer_second, outer_first};
            const ConvexPolyhedron polyhedron = extrude_profile(
                profile,
                [&](const Point2 point, const GridCoord extrusion) {
                    if (spec.axis == ArchAxis::along_x) {
                        return GridVec3{add_coord(spec.origin.x, extrusion), add_coord(spec.origin.y, point.first),
                                        add_coord(spec.origin.z, point.second)};
                    }
                    return GridVec3{add_coord(spec.origin.x, point.first), add_coord(spec.origin.y, extrusion),
                                    add_coord(spec.origin.z, point.second)};
                },
                spec.depth);
            result_.solids.push_back(make_solid(ids_.solid(), polyhedron, MaterialRole::arch, BrushPurpose::arch,
                                                spec.ownership, BrushContents::solid, std::nullopt, limits_));
        }
    }

    void add(const CurvedWallSpec &spec) {
        require_key(spec.key);
        require_ownership(spec.ownership);
        require_segment_count(spec.segment_count, spec.inner_radius, limits_);
        if (spec.thickness <= 0 || spec.height < 2)
            fail("invalid_curve_dimensions", "a curved wall requires positive thickness and height of at least two");
        const GridCoord outer_radius = add_coord(spec.inner_radius, spec.thickness);
        if (outer_radius <= spec.inner_radius)
            fail("invalid_curve_dimensions", "curve outer radius must exceed its inner radius");
        static_cast<void>(transform_quadrant({}, spec.quadrant));
        require_solid_capacity(result_, spec.segment_count, limits_);

        LayoutCurve curve;
        if (spec.emit_patch_veneer) {
            const std::uint64_t half_divisions = static_cast<std::uint64_t>(spec.segment_count) * 2U;
            if (static_cast<std::uint64_t>(spec.inner_radius) < half_divisions)
                fail("patch_resolution", "patch radius is too small for distinct half-segment controls");
            curve.width = spec.segment_count * 2U + 1U;
            curve.height = 3;
            constexpr std::uint32_t portable_patch_dimension = 31;
            if (curve.width > portable_patch_dimension || curve.height > portable_patch_dimension)
                fail("patch_dimension_limit", "curve patch exceeds the portable 31 by 31 control-grid limit");
            curve.source_id = ids_.solid();
            curve.material = MaterialRole::curve;
            curve.ownership = spec.ownership;
            const std::size_t control_count = static_cast<std::size_t>(curve.width) * curve.height;
            if (control_count > limits_.max_patch_control_points)
                fail("patch_control_limit", "curve patch exceeds the configured control-point cap");
            curve.control_points.reserve(control_count);
            const std::array<GridCoord, 3> row_heights{0, spec.height / 2, spec.height};
            for (std::uint32_t column = 0; column < curve.width; ++column) {
                const Point2 local = transform_quadrant(
                    quarter_circle_point(spec.inner_radius, column, curve.width - 1U), spec.quadrant);
                for (std::uint32_t row = 0; row < curve.height; ++row) {
                    const GridVec3 position = curve_world_point(spec.center, local, row_heights[row]);
                    require_point_limit(position, limits_);
                    curve.control_points.push_back({position,
                                                    static_cast<std::int32_t>(round_divide(
                                                        static_cast<std::int64_t>(1000) * column, curve.width - 1U)),
                                                    static_cast<std::int32_t>(row * 500U)});
                }
            }
            curve.collision_backing_solids.reserve(spec.segment_count);
        }

        for (std::uint32_t segment = 0; segment < spec.segment_count; ++segment) {
            const Point2 inner_first =
                transform_quadrant(quarter_circle_point(spec.inner_radius, segment, spec.segment_count), spec.quadrant);
            const Point2 inner_second = transform_quadrant(
                quarter_circle_point(spec.inner_radius, segment + 1, spec.segment_count), spec.quadrant);
            const Point2 outer_first =
                transform_quadrant(quarter_circle_point(outer_radius, segment, spec.segment_count), spec.quadrant);
            const Point2 outer_second =
                transform_quadrant(quarter_circle_point(outer_radius, segment + 1, spec.segment_count), spec.quadrant);
            const std::vector<Point2> profile{inner_first, inner_second, outer_second, outer_first};
            const ConvexPolyhedron polyhedron = extrude_profile(
                profile,
                [&](const Point2 point, const GridCoord height) {
                    return curve_world_point(spec.center, point, height);
                },
                spec.height);
            const MaterialRole material = spec.emit_patch_veneer ? MaterialRole::hidden : MaterialRole::curve;
            const BrushPurpose purpose = spec.emit_patch_veneer ? BrushPurpose::hidden : BrushPurpose::curve;
            LayoutSolid solid = make_solid(ids_.solid(), polyhedron, material, purpose, spec.ownership,
                                           BrushContents::solid, std::nullopt, limits_);
            if (spec.emit_patch_veneer)
                curve.collision_backing_solids.push_back(std::move(solid));
            else
                result_.solids.push_back(std::move(solid));
        }
        if (spec.emit_patch_veneer)
            result_.curves.push_back(std::move(curve));
    }

    void add(const LiquidSpec &spec) {
        require_key(spec.key);
        if (spec.ownership != BrushOwnership::structural)
            fail("invalid_liquid_ownership", "liquid construction must be structurally owned");
        if (spec.contents == BrushContents::solid)
            fail("invalid_liquid_contents", "liquid construction requires water, slime, or lava contents");
        require_solid_capacity(result_, 1, limits_);
        require_bounds(spec.bounds, limits_);
        result_.solids.push_back(make_solid(ids_.solid(), spec.bounds, MaterialRole::liquid, BrushPurpose::liquid,
                                            spec.ownership, spec.contents, std::nullopt, limits_));
    }

    void add(const DoorAssemblySpec &spec) {
        require_key(spec.key);
        require_mover(spec.leaf_bounds, spec.travel_delta, spec.speed_units_per_second, false);
        if (spec.trigger_bounds)
            require_bounds(*spec.trigger_bounds, limits_);
        require_solid_capacity(result_, 1, limits_);

        const PlacementId mover_id = ids_.placement();
        const PlacementId destination_id = ids_.placement();
        const std::optional<PlacementId> trigger_id =
            spec.trigger_bounds ? std::optional{ids_.placement()} : std::nullopt;
        const GridVec3 destination_origin = add_vec(spec.leaf_bounds.center(), spec.travel_delta);
        require_point_limit(destination_origin, limits_);

        Placement mover;
        mover.id = mover_id;
        mover.role = PlacementRole::door;
        mover.origin = spec.leaf_bounds.center();
        mover.volume = spec.leaf_bounds;
        mover.link = destination_id;
        result_.placements.push_back(mover);

        Placement destination;
        destination.id = destination_id;
        destination.role = PlacementRole::door_destination;
        destination.origin = destination_origin;
        result_.placements.push_back(destination);

        if (trigger_id) {
            Placement trigger;
            trigger.id = *trigger_id;
            trigger.role = PlacementRole::door_trigger;
            trigger.origin = spec.trigger_bounds->center();
            trigger.volume = spec.trigger_bounds;
            trigger.link = mover_id;
            result_.placements.push_back(trigger);
        }
        result_.solids.push_back(make_solid(ids_.solid(), spec.leaf_bounds, MaterialRole::door, BrushPurpose::door,
                                            BrushOwnership::placement, BrushContents::solid, mover_id, limits_));
        result_.movers.push_back({mover_id, spec.travel_delta, spec.speed_units_per_second, spec.wait_milliseconds});
    }

    void add(const LiftAssemblySpec &spec) {
        require_key(spec.key);
        require_mover(spec.platform_bounds, spec.travel_delta, spec.speed_units_per_second, true);
        if (spec.trigger_bounds)
            require_bounds(*spec.trigger_bounds, limits_);
        require_solid_capacity(result_, 1, limits_);

        const PlacementId mover_id = ids_.placement();
        const PlacementId destination_id = ids_.placement();
        const std::optional<PlacementId> trigger_id =
            spec.trigger_bounds ? std::optional{ids_.placement()} : std::nullopt;
        const GridVec3 destination_origin = add_vec(spec.platform_bounds.center(), spec.travel_delta);
        require_point_limit(destination_origin, limits_);

        Placement mover;
        mover.id = mover_id;
        mover.role = PlacementRole::lift;
        mover.origin = spec.platform_bounds.center();
        mover.volume = spec.platform_bounds;
        mover.link = destination_id;
        result_.placements.push_back(mover);

        Placement destination;
        destination.id = destination_id;
        destination.role = PlacementRole::lift_destination;
        destination.origin = destination_origin;
        result_.placements.push_back(destination);

        if (trigger_id) {
            Placement trigger;
            trigger.id = *trigger_id;
            trigger.role = PlacementRole::lift_trigger;
            trigger.origin = spec.trigger_bounds->center();
            trigger.volume = spec.trigger_bounds;
            trigger.link = mover_id;
            result_.placements.push_back(trigger);
        }
        result_.solids.push_back(make_solid(ids_.solid(), spec.platform_bounds, MaterialRole::lift, BrushPurpose::lift,
                                            BrushOwnership::placement, BrushContents::solid, mover_id, limits_));
        result_.movers.push_back({mover_id, spec.travel_delta, spec.speed_units_per_second, spec.wait_milliseconds});
    }

    void add(const TeleporterAssemblySpec &spec) {
        require_key(spec.key);
        require_bounds(spec.trigger_bounds, limits_);
        require_point_limit(spec.destination_origin, limits_);
        if (spec.destination_angle_degrees &&
            (*spec.destination_angle_degrees < 0 || *spec.destination_angle_degrees >= 360)) {
            fail("invalid_angle", "teleporter destination angle must be from 0 through 359 degrees");
        }

        const PlacementId trigger_id = ids_.placement();
        const PlacementId destination_id = ids_.placement();
        Placement trigger;
        trigger.id = trigger_id;
        trigger.role = PlacementRole::teleporter_trigger;
        trigger.origin = spec.trigger_bounds.center();
        trigger.volume = game_ == Game::quake2 ? quake2_teleporter_bounds(trigger.origin, true) : spec.trigger_bounds;
        require_bounds(*trigger.volume, limits_);
        if (game_ == Game::quake2) {
            require_bounds(quake2_teleporter_bounds(trigger.origin, false), limits_);
            require_bounds(quake2_teleporter_bounds(spec.destination_origin, false), limits_);
        }
        trigger.link = destination_id;
        result_.placements.push_back(trigger);

        Placement destination;
        destination.id = destination_id;
        destination.role = PlacementRole::teleporter_destination;
        destination.origin = spec.destination_origin;
        destination.angle_degrees = spec.destination_angle_degrees;
        result_.placements.push_back(destination);
    }

    [[nodiscard]] ConstructionResult finish() && {
        std::sort(result_.solids.begin(), result_.solids.end(),
                  [](const LayoutSolid &left, const LayoutSolid &right) { return left.id < right.id; });
        std::sort(result_.placements.begin(), result_.placements.end(),
                  [](const Placement &left, const Placement &right) { return left.id < right.id; });
        std::sort(result_.curves.begin(), result_.curves.end(),
                  [](const LayoutCurve &left, const LayoutCurve &right) { return left.source_id < right.source_id; });
        std::sort(result_.movers.begin(), result_.movers.end(),
                  [](const MoverDirective &left, const MoverDirective &right) { return left.mover < right.mover; });
        return std::move(result_);
    }

  private:
    void require_key(const ConstructionKey key) const {
        if (!key)
            fail("invalid_construction_key", "construction keys reserve zero and must be nonzero");
    }

    void require_mover(const GridBounds &bounds, const GridVec3 delta, const GridCoord speed,
                       const bool vertical_only) const {
        require_bounds(bounds, limits_);
        if (speed <= 0)
            fail("invalid_mover_speed", "mover speed must be positive");
        const int nonzero_axes = (delta.x != 0 ? 1 : 0) + (delta.y != 0 ? 1 : 0) + (delta.z != 0 ? 1 : 0);
        if (nonzero_axes != 1)
            fail("invalid_mover_delta", "mover travel must be nonzero and axis-aligned");
        if (vertical_only && (delta.x != 0 || delta.y != 0))
            fail("invalid_lift_delta", "lift travel must use only the vertical axis");
        const GridVec3 moved_min = add_vec(bounds.min, delta);
        const GridVec3 moved_max = add_vec(bounds.max, delta);
        require_point_limit(moved_min, limits_);
        require_point_limit(moved_max, limits_);
    }

    IdAllocator ids_;
    ConstructionResult result_;
    const ConstructionLimits &limits_;
    Game game_;
};

void validate_curve(const LayoutCurve &curve, const ConstructionLimits &limits) {
    if (!curve.source_id || curve.material != MaterialRole::curve || !valid_generated_ownership(curve.ownership))
        fail("invalid_curve", "curve has invalid source, material, or ownership semantics");
    if (curve.width < 3 || curve.height < 3 || (curve.width & 1U) == 0 || (curve.height & 1U) == 0)
        fail("invalid_curve_dimensions", "curve control dimensions must be odd and at least three");
    const std::size_t expected = static_cast<std::size_t>(curve.width) * curve.height;
    if (expected != curve.control_points.size() || expected > limits.max_patch_control_points)
        fail("invalid_curve_controls", "curve control storage does not match its dimensions");
    if (curve.collision_backing_solids.empty())
        fail("missing_curve_backing", "a patch curve requires explicit collision/backing solids");
    for (const LayoutCurveControlPoint &control : curve.control_points)
        require_point_limit(control.position, limits);
}

void validate_result(const ConstructionResult &result, const ConstructionLimits &limits) {
    std::set<SolidId> solid_ids;
    for (const LayoutSolid &solid : result.solids) {
        if (!solid.id || !solid_ids.insert(solid.id).second)
            fail("duplicate_generated_id", "construction generated a zero or duplicate SolidId");
        require_geometry(solid.geometry, limits);
    }
    for (const LayoutCurve &curve : result.curves) {
        validate_curve(curve, limits);
        if (!solid_ids.insert(curve.source_id).second)
            fail("duplicate_generated_id", "curve source collides with a generated SolidId");
        for (const LayoutSolid &solid : curve.collision_backing_solids) {
            if (!solid.id || !solid_ids.insert(solid.id).second)
                fail("duplicate_generated_id", "curve backing has a zero or duplicate SolidId");
            require_geometry(solid.geometry, limits);
        }
    }
    if (result.solids.size() + curve_backing_count(result) > limits.max_generated_solids)
        fail("solid_limit", "construction exceeds the configured generated-solid cap");

    std::set<PlacementId> placement_ids;
    for (const Placement &placement : result.placements) {
        if (!placement.id || !placement_ids.insert(placement.id).second)
            fail("duplicate_generated_id", "construction generated a zero or duplicate PlacementId");
        if (placement.volume)
            require_bounds(*placement.volume, limits);
        else
            require_point_limit(placement.origin, limits);
    }
    for (const Placement &placement : result.placements) {
        if (placement.link && !placement_ids.contains(*placement.link))
            fail("invalid_generated_link", "construction placement link does not resolve within its assembly result");
    }
    for (const MoverDirective &mover : result.movers) {
        const auto placement = std::find_if(result.placements.begin(), result.placements.end(),
                                            [&](const Placement &item) { return item.id == mover.mover; });
        if (placement == result.placements.end() ||
            (placement->role != PlacementRole::door && placement->role != PlacementRole::lift) ||
            mover.speed_units_per_second <= 0) {
            fail("invalid_mover_directive", "mover directive does not reference a valid generated door or lift");
        }
    }
}

} // namespace

ConstructionResult ConstructionBuilder::build(const Layout &base, std::vector<ConstructionRequest> requests,
                                              const ConstructionLimits &limits) const {
    if (limits.max_coordinate <= 0 || limits.max_requests == 0 || limits.max_generated_solids == 0 ||
        limits.max_segments_per_feature == 0 || limits.max_patch_control_points == 0) {
        fail("invalid_limits", "construction limits must all be positive");
    }
    if (requests.size() > limits.max_requests)
        fail("request_limit", "construction request count exceeds the configured cap");

    const std::uint32_t first_solid =
        checked_next_id(base.solids, [](const LayoutSolid &solid) { return solid.id; }, "solid");
    const std::uint32_t first_placement =
        checked_next_id(base.placements, [](const Placement &placement) { return placement.id; }, "placement");
    std::sort(requests.begin(), requests.end(), [](const ConstructionRequest &left, const ConstructionRequest &right) {
        return request_key(left) < request_key(right);
    });
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const ConstructionKey key = request_key(requests[index]);
        if (!key)
            fail("invalid_construction_key", "construction keys reserve zero and must be nonzero");
        if (index != 0 && key == request_key(requests[index - 1]))
            fail("duplicate_construction_key", "construction request keys must be unique");
    }

    ConstructionResult result;
    result.expected_first_solid_id = SolidId{first_solid};
    result.expected_first_placement_id = PlacementId{first_placement};
    BuildState state{{first_solid, first_placement}, std::move(result), limits, base.spec.game};
    try {
        for (const ConstructionRequest &request : requests)
            std::visit([&](const auto &spec) { state.add(spec); }, request);
    } catch (const ConstructionError &) {
        throw;
    } catch (const std::overflow_error &) {
        fail("coordinate_overflow", "construction arithmetic overflowed the integer grid");
    } catch (const std::invalid_argument &) {
        fail("invalid_generated_geometry", "construction produced invalid integer-grid geometry");
    }
    ConstructionResult completed = std::move(state).finish();
    validate_result(completed, limits);
    return completed;
}

std::vector<LayoutSolid> ConstructionResult::all_solids() const {
    std::vector<LayoutSolid> flattened = solids;
    const std::size_t backing_count = curve_backing_count(*this);
    if (backing_count > std::numeric_limits<std::size_t>::max() - flattened.size())
        fail("solid_limit", "construction solid count overflowed");
    flattened.reserve(flattened.size() + backing_count);
    for (const LayoutCurve &curve : curves) {
        flattened.insert(flattened.end(), curve.collision_backing_solids.begin(), curve.collision_backing_solids.end());
    }
    std::sort(flattened.begin(), flattened.end(),
              [](const LayoutSolid &left, const LayoutSolid &right) { return left.id < right.id; });
    return flattened;
}

void ConstructionResult::apply_to(Layout &layout) const {
    const std::uint32_t current_first_solid =
        checked_next_id(layout.solids, [](const LayoutSolid &solid) { return solid.id; }, "solid");
    const std::uint32_t current_first_placement =
        checked_next_id(layout.placements, [](const Placement &placement) { return placement.id; }, "placement");
    if (expected_first_solid_id.value != current_first_solid)
        fail("stale_solid_id_base", "layout SolidId base changed after construction was built");
    if (expected_first_placement_id.value != current_first_placement)
        fail("stale_placement_id_base", "layout PlacementId base changed after construction was built");

    std::vector<LayoutSolid> generated_solids = all_solids();
    std::set<SolidId> occupied_solid_ids;
    for (const LayoutSolid &solid : layout.solids)
        occupied_solid_ids.insert(solid.id);
    for (const LayoutCurve &curve : curves) {
        if (!curve.source_id || !occupied_solid_ids.insert(curve.source_id).second)
            fail("solid_id_collision", "curve source SolidId collides while applying construction");
    }
    for (const LayoutSolid &solid : generated_solids) {
        if (!solid.id || !occupied_solid_ids.insert(solid.id).second)
            fail("solid_id_collision", "generated SolidId collides while applying construction");
        if (!validate_brush_geometry(solid.geometry).ok())
            fail("invalid_generated_geometry", "generated solid became invalid before construction was applied");
    }

    std::set<PlacementId> occupied_placement_ids;
    for (const Placement &placement : layout.placements)
        occupied_placement_ids.insert(placement.id);
    for (const Placement &placement : placements) {
        if (!placement.id || !occupied_placement_ids.insert(placement.id).second)
            fail("placement_id_collision", "generated PlacementId collides while applying construction");
    }

    for (const LayoutSolid &solid : generated_solids) {
        if (solid.ownership == BrushOwnership::placement) {
            if (!solid.owner || !occupied_placement_ids.contains(*solid.owner))
                fail("invalid_generated_owner", "placement-owned construction solid has no applied owner");
        } else if (solid.owner) {
            fail("invalid_generated_owner", "non-placement construction solid unexpectedly has an owner");
        }
    }
    for (const Placement &placement : placements) {
        if (placement.link && !occupied_placement_ids.contains(*placement.link))
            fail("invalid_generated_link", "construction placement link cannot resolve in the target layout");
    }

    std::vector<LayoutSolid> merged_solids = layout.solids;
    std::vector<Placement> merged_placements = layout.placements;
    merged_solids.insert(merged_solids.end(), generated_solids.begin(), generated_solids.end());
    merged_placements.insert(merged_placements.end(), placements.begin(), placements.end());
    std::sort(merged_solids.begin(), merged_solids.end(),
              [](const LayoutSolid &left, const LayoutSolid &right) { return left.id < right.id; });
    std::sort(merged_placements.begin(), merged_placements.end(),
              [](const Placement &left, const Placement &right) { return left.id < right.id; });
    layout.solids = std::move(merged_solids);
    layout.placements = std::move(merged_placements);
}

} // namespace qmg
