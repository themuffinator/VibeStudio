#include "qmg/construction.hpp"
#include "qmg/quake2_mechanisms.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename Function>
void require_error(Function &&function, const std::string_view code, const std::string_view message) {
    try {
        function();
    } catch (const qmg::ConstructionError &error) {
        require(error.code() == code, "construction failed with an unexpected error code");
        return;
    }
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::Layout base_layout() {
    qmg::Layout layout;
    layout.solids.push_back({qmg::SolidId{9}, qmg::GridBounds{{-32, -32, -32}, {-16, -16, -16}},
                             qmg::MaterialRole::wall, qmg::BrushPurpose::structural, qmg::BrushOwnership::structural,
                             qmg::BrushContents::solid, std::nullopt});
    layout.solids.push_back({qmg::SolidId{3}, qmg::GridBounds{{16, 16, 16}, {32, 32, 32}}, qmg::MaterialRole::platform,
                             qmg::BrushPurpose::raised_platform, qmg::BrushOwnership::detail, qmg::BrushContents::solid,
                             std::nullopt});

    qmg::Placement first;
    first.id = qmg::PlacementId{12};
    first.role = qmg::PlacementRole::health;
    first.origin = {-128, -128, 32};
    layout.placements.push_back(first);
    qmg::Placement second;
    second.id = qmg::PlacementId{4};
    second.role = qmg::PlacementRole::armor;
    second.origin = {-96, -128, 32};
    layout.placements.push_back(second);
    return layout;
}

void require_valid_geometry(const qmg::LayoutSolid &solid) {
    const qmg::BrushGeometryValidation validation = qmg::validate_brush_geometry(solid.geometry);
    require(validation.ok(), "constructed brush geometry is invalid");
    require(validation.bounds.has_value(), "constructed brush has no aggregate bounds");
    require(validation.interior_witness.has_value(), "constructed brush has no interior witness");
}

[[nodiscard]] const qmg::Placement &placement_with_role(const qmg::ConstructionResult &result,
                                                        const qmg::PlacementRole role) {
    const auto found = std::find_if(result.placements.begin(), result.placements.end(),
                                    [role](const qmg::Placement &placement) { return placement.role == role; });
    require(found != result.placements.end(), "expected typed placement was not constructed");
    return *found;
}

void exact_stair_runs() {
    qmg::StairRunSpec positive;
    positive.key = {1};
    positive.start = {100, 200, 24};
    positive.ascent = qmg::HorizontalDirection::positive_x;
    positive.width = 96;
    positive.tread_depth = 24;
    positive.riser_height = 12;
    positive.step_count = 5;
    positive.ownership = qmg::BrushOwnership::detail;

    const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {positive});
    require(result.solids.size() == 5, "stair run did not emit exactly one brush per tread");
    for (std::size_t index = 0; index < result.solids.size(); ++index) {
        const qmg::LayoutSolid &solid = result.solids[index];
        require(solid.id.value == index + 1, "stair SolidIds are not sequential");
        require(solid.material == qmg::MaterialRole::stair && solid.purpose == qmg::BrushPurpose::navigation_stair &&
                    solid.ownership == qmg::BrushOwnership::detail,
                "stair brush semantics changed");
        const qmg::GridBounds *bounds = qmg::get_box(solid.geometry);
        require(bounds != nullptr, "stair tread is not an integer-grid box");
        require(bounds->min.x == 100 + static_cast<qmg::GridCoord>(index) * 24 &&
                    bounds->max.x == 124 + static_cast<qmg::GridCoord>(index) * 24,
                "stair treads do not partition the exact requested run");
        require(bounds->min.y == 200 && bounds->max.y == 296, "stair width changed");
        require(bounds->min.z == 24 && bounds->max.z == 24 + static_cast<qmg::GridCoord>(index + 1) * 12,
                "stair risers do not partition the exact requested rise");
        require_valid_geometry(solid);
    }
    const qmg::GridBounds *last = qmg::get_box(result.solids.back().geometry);
    require(last != nullptr && last->max.x == 220 && last->max.z == 84,
            "stair run did not reach its exact run/rise endpoint");

    qmg::StairRunSpec negative = positive;
    negative.key = {2};
    negative.start = {-100, 300, 0};
    negative.ascent = qmg::HorizontalDirection::negative_y;
    negative.step_count = 2;
    const qmg::ConstructionResult reverse = qmg::ConstructionBuilder{}.build({}, {negative});
    const qmg::GridBounds *first = qmg::get_box(reverse.solids.front().geometry);
    const qmg::GridBounds *second = qmg::get_box(reverse.solids.back().geometry);
    require(first != nullptr && second != nullptr && first->min.y == 276 && first->max.y == 300 &&
                second->min.y == 252 && second->max.y == 276 && second->max.z == 24,
            "negative-axis stairs were not oriented deterministically");
}

void convex_ramps() {
    qmg::RampSpec spec;
    spec.key = {1};
    spec.start = {-64, 40, -8};
    spec.direction = qmg::HorizontalDirection::negative_x;
    spec.width = 80;
    spec.run = 160;
    spec.low_height = 8;
    spec.rise = 72;
    const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {spec});
    require(result.solids.size() == 1, "ramp did not emit one convex brush");
    const qmg::ConvexPolyhedron *ramp = qmg::get_convex_polyhedron(result.solids.front().geometry);
    require(ramp != nullptr && ramp->planes.size() == 6, "ramp is not a six-plane convex wedge");
    require_valid_geometry(result.solids.front());
    const qmg::GridBounds bounds = qmg::checked_brush_bounds(result.solids.front().geometry);
    require(bounds == qmg::GridBounds{{-224, 40, -8}, {-64, 120, 72}},
            "ramp aggregate bounds do not match exact run, width, and rise");
}

void segmented_arches() {
    qmg::ArchSpec spec;
    spec.key = {4};
    spec.origin = {20, 0, 32};
    spec.axis = qmg::ArchAxis::along_x;
    spec.depth = 40;
    spec.inner_radius = 128;
    spec.ring_thickness = 32;
    spec.segment_count = 8;
    const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {spec});
    require(result.solids.size() == 8, "arch did not emit the requested segment count");
    for (const qmg::LayoutSolid &solid : result.solids) {
        require(solid.material == qmg::MaterialRole::arch && solid.purpose == qmg::BrushPurpose::arch,
                "arch segment lost typed semantics");
        require(qmg::get_convex_polyhedron(solid.geometry) != nullptr,
                "arch segment is not an explicit convex polyhedron");
        require_valid_geometry(solid);
    }
    const qmg::GridBounds left = qmg::checked_brush_bounds(result.solids.front().geometry);
    const qmg::GridBounds right = qmg::checked_brush_bounds(result.solids.back().geometry);
    require(left.min.x == 20 && left.max.x == 60 && right.min.x == 20 && right.max.x == 60,
            "arch extrusion depth changed between segments");
    qmg::GridCoord minimum_plane_y = std::numeric_limits<qmg::GridCoord>::max();
    qmg::GridCoord maximum_plane_y = std::numeric_limits<qmg::GridCoord>::min();
    for (const qmg::LayoutSolid &solid : result.solids) {
        for (const qmg::PlanePointTriple &plane : qmg::get_convex_polyhedron(solid.geometry)->planes) {
            for (const qmg::GridVec3 point : {plane.first, plane.second, plane.third}) {
                minimum_plane_y = std::min(minimum_plane_y, point.y);
                maximum_plane_y = std::max(maximum_plane_y, point.y);
            }
        }
    }
    require(minimum_plane_y == -160 && maximum_plane_y == 160,
            "arch control planes did not span their exact outer diameter");
}

void curves_and_patch_extras() {
    qmg::CurvedWallSpec approximation;
    approximation.key = {1};
    approximation.center = {10, -20, 16};
    approximation.quadrant = qmg::CurveQuadrant::negative_x_positive_y;
    approximation.inner_radius = 128;
    approximation.thickness = 24;
    approximation.height = 160;
    approximation.segment_count = 4;
    const qmg::ConstructionResult brush_result = qmg::ConstructionBuilder{}.build({}, {approximation});
    require(brush_result.curves.empty() && brush_result.solids.size() == 4,
            "Q1/Q2 curved-wall approximation emitted unexpected patch data");
    for (const qmg::LayoutSolid &solid : brush_result.solids) {
        require(solid.material == qmg::MaterialRole::curve && solid.purpose == qmg::BrushPurpose::curve,
                "curve approximation lost visible typed semantics");
        require_valid_geometry(solid);
    }

    qmg::CurvedWallSpec patch = approximation;
    patch.key = {2};
    patch.emit_patch_veneer = true;
    const qmg::ConstructionResult patch_result = qmg::ConstructionBuilder{}.build({}, {patch});
    require(patch_result.solids.empty() && patch_result.curves.size() == 1,
            "patch-backed curve did not isolate its collision brushes");
    const qmg::LayoutCurve &curve = patch_result.curves.front();
    require(curve.source_id == qmg::SolidId{1}, "patch curve did not reserve a stable source SolidId");
    require(curve.width == 9 && curve.height == 3 && curve.control_points.size() == 27,
            "patch curve control dimensions are not odd or width-major complete");
    require(curve.control(0, 0).u_milli == 0 && curve.control(0, 0).v_milli == 0 &&
                curve.control(4, 1).u_milli == 500 && curve.control(4, 1).v_milli == 500 &&
                curve.control(8, 2).u_milli == 1000 && curve.control(8, 2).v_milli == 1000,
            "patch curve fixed-point texture coordinates changed");
    require(curve.control(0, 0).position == qmg::GridVec3{-118, -20, 16} &&
                curve.control(8, 2).position == qmg::GridVec3{10, 108, 176},
            "patch control endpoint orientation changed");
    require(curve.collision_backing_solids.size() == 4 && patch_result.all_solids().size() == 4,
            "patch curve lacks explicit collision/backing brushes");
    for (const qmg::LayoutSolid &solid : curve.collision_backing_solids) {
        require(solid.id.value > curve.source_id.value && solid.material == qmg::MaterialRole::hidden &&
                    solid.purpose == qmg::BrushPurpose::hidden,
                "patch backing brush is not hidden or does not follow its source ID");
        require_valid_geometry(solid);
    }
}

void orientation_matrix() {
    const std::vector<qmg::HorizontalDirection> directions{
        qmg::HorizontalDirection::positive_x,
        qmg::HorizontalDirection::negative_x,
        qmg::HorizontalDirection::positive_y,
        qmg::HorizontalDirection::negative_y,
    };
    for (std::size_t index = 0; index < directions.size(); ++index) {
        qmg::RampSpec ramp;
        ramp.key = {static_cast<std::uint64_t>(index + 1)};
        ramp.start = {0, 0, 0};
        ramp.direction = directions[index];
        ramp.width = 64;
        ramp.run = 96;
        ramp.low_height = 8;
        ramp.rise = 48;
        const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {ramp});
        require_valid_geometry(result.solids.front());
    }

    for (const qmg::ArchAxis axis : {qmg::ArchAxis::along_x, qmg::ArchAxis::along_y}) {
        qmg::ArchSpec arch;
        arch.key = {1};
        arch.origin = {0, 0, 0};
        arch.axis = axis;
        arch.depth = 32;
        arch.inner_radius = 96;
        arch.ring_thickness = 16;
        arch.segment_count = 6;
        const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {arch});
        for (const qmg::LayoutSolid &solid : result.solids)
            require_valid_geometry(solid);
    }

    const std::vector<qmg::CurveQuadrant> quadrants{
        qmg::CurveQuadrant::positive_x_positive_y,
        qmg::CurveQuadrant::negative_x_positive_y,
        qmg::CurveQuadrant::negative_x_negative_y,
        qmg::CurveQuadrant::positive_x_negative_y,
    };
    for (std::size_t index = 0; index < quadrants.size(); ++index) {
        qmg::CurvedWallSpec curve;
        curve.key = {static_cast<std::uint64_t>(index + 1)};
        curve.center = {0, 0, 0};
        curve.quadrant = quadrants[index];
        curve.inner_radius = 96;
        curve.thickness = 16;
        curve.height = 128;
        curve.segment_count = 4;
        curve.emit_patch_veneer = (index & 1U) != 0;
        const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {curve});
        for (const qmg::LayoutSolid &solid : result.all_solids())
            require_valid_geometry(solid);
    }
}

void quantized_geometry_matrix() {
    for (const qmg::GridCoord radius : {16, 24, 32, 48, 64, 96, 128}) {
        const std::uint32_t maximum_segments = std::min<std::uint32_t>(10, static_cast<std::uint32_t>(radius));
        for (std::uint32_t segments = 2; segments <= maximum_segments; ++segments) {
            qmg::ArchSpec arch;
            arch.key = {1};
            arch.origin = {0, 0, 0};
            arch.depth = 16;
            arch.inner_radius = radius;
            arch.ring_thickness = 8;
            arch.segment_count = segments;
            const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {arch});
            require(result.solids.size() == segments, "quantized arch matrix changed segment cardinality");
        }

        for (std::uint32_t segments = 1; segments <= maximum_segments; ++segments) {
            qmg::CurvedWallSpec curve;
            curve.key = {1};
            curve.center = {0, 0, 0};
            curve.inner_radius = radius;
            curve.thickness = 8;
            curve.height = 64;
            curve.segment_count = segments;
            curve.emit_patch_veneer = static_cast<std::uint32_t>(radius) >= segments * 2U;
            const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {curve});
            require(result.all_solids().size() == segments, "quantized curved-wall matrix changed segment cardinality");
        }
    }
}

void liquids() {
    qmg::LiquidSpec water{{1}, {{0, 0, 0}, {64, 64, 24}}, qmg::BrushContents::water, qmg::BrushOwnership::structural};
    qmg::LiquidSpec slime{{2}, {{80, 0, 0}, {144, 64, 24}}, qmg::BrushContents::slime, qmg::BrushOwnership::structural};
    qmg::LiquidSpec lava{{3}, {{160, 0, 0}, {224, 64, 24}}, qmg::BrushContents::lava, qmg::BrushOwnership::structural};
    const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {lava, water, slime});
    require(result.solids.size() == 3, "liquid requests were not all emitted");
    require(result.solids[0].contents == qmg::BrushContents::water &&
                result.solids[1].contents == qmg::BrushContents::slime &&
                result.solids[2].contents == qmg::BrushContents::lava,
            "liquids are not ordered by stable construction key");
    for (const qmg::LayoutSolid &solid : result.solids) {
        require(solid.material == qmg::MaterialRole::liquid && solid.purpose == qmg::BrushPurpose::liquid,
                "liquid material/purpose/contents contract changed");
        require_valid_geometry(solid);
    }
}

[[nodiscard]] std::vector<qmg::ConstructionRequest> assembly_requests() {
    qmg::DoorAssemblySpec door;
    door.key = {30};
    door.leaf_bounds = {{-32, -8, 0}, {32, 8, 96}};
    door.travel_delta = {64, 0, 0};
    door.speed_units_per_second = 120;
    door.wait_milliseconds = 2500;
    door.trigger_bounds = qmg::GridBounds{{-64, -48, 0}, {64, 48, 96}};

    qmg::LiftAssemblySpec lift;
    lift.key = {10};
    lift.platform_bounds = {{200, 200, 0}, {328, 328, 16}};
    lift.travel_delta = {0, 0, 160};
    lift.speed_units_per_second = 80;
    lift.wait_milliseconds = 4000;
    lift.trigger_bounds = qmg::GridBounds{{216, 216, 16}, {312, 312, 80}};

    qmg::TeleporterAssemblySpec teleporter;
    teleporter.key = {20};
    teleporter.trigger_bounds = {{400, 0, 0}, {496, 96, 128}};
    teleporter.destination_origin = {640, 48, 24};
    teleporter.destination_angle_degrees = 270;
    return {door, teleporter, lift};
}

void typed_assemblies_and_apply() {
    qmg::Layout base = base_layout();
    const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build(base, assembly_requests());
    require(result.expected_first_solid_id == qmg::SolidId{10} &&
                result.expected_first_placement_id == qmg::PlacementId{13},
            "construction did not seed IDs from order-independent maxima");
    require(result.solids.size() == 2 && result.placements.size() == 8 && result.movers.size() == 2,
            "typed mover/teleporter assembly cardinalities changed");

    const qmg::Placement &lift = placement_with_role(result, qmg::PlacementRole::lift);
    const qmg::Placement &lift_destination = placement_with_role(result, qmg::PlacementRole::lift_destination);
    const qmg::Placement &teleporter = placement_with_role(result, qmg::PlacementRole::teleporter_trigger);
    const qmg::Placement &teleporter_destination =
        placement_with_role(result, qmg::PlacementRole::teleporter_destination);
    const qmg::Placement &door = placement_with_role(result, qmg::PlacementRole::door);
    require(lift.link == lift_destination.id && teleporter.link == teleporter_destination.id,
            "lift or teleporter typed links do not resolve to destinations");
    require(door.link.has_value(), "door does not link to its typed destination");
    require(teleporter_destination.angle_degrees == 270, "teleporter destination angle changed");

    for (const qmg::LayoutSolid &solid : result.solids) {
        require(solid.ownership == qmg::BrushOwnership::placement && solid.owner.has_value(),
                "mover solid is not explicitly placement-owned");
        require_valid_geometry(solid);
    }
    require(result.movers[0].mover == lift.id && result.movers[0].travel_delta == qmg::GridVec3{0, 0, 160} &&
                result.movers[0].speed_units_per_second == 80 && result.movers[0].wait_milliseconds == 4000,
            "typed lift motion directive changed");
    require(result.movers[1].mover == door.id && result.movers[1].travel_delta == qmg::GridVec3{64, 0, 0},
            "typed door motion directive changed");

    result.apply_to(base);
    require(base.solids.size() == 4 && base.placements.size() == 10,
            "checked construction application emitted the wrong layout cardinalities");
    require(
        std::is_sorted(
            base.solids.begin(), base.solids.end(),
            [](const qmg::LayoutSolid &left, const qmg::LayoutSolid &right) { return left.id < right.id; }) &&
            std::is_sorted(base.placements.begin(), base.placements.end(),
                           [](const qmg::Placement &left, const qmg::Placement &right) { return left.id < right.id; }),
        "apply_to did not canonicalize Layout vector order by stable IDs");
    require_error([&] { result.apply_to(base); }, "stale_solid_id_base",
                  "applying the same construction twice was accepted");
}

void patch_backing_is_applied() {
    qmg::CurvedWallSpec patch;
    patch.key = {1};
    patch.center = {0, 0, 0};
    patch.inner_radius = 96;
    patch.thickness = 16;
    patch.height = 128;
    patch.segment_count = 4;
    patch.emit_patch_veneer = true;
    const qmg::ConstructionResult result = qmg::ConstructionBuilder{}.build({}, {patch});
    qmg::Layout layout;
    result.apply_to(layout);
    require(layout.solids == result.curves.front().collision_backing_solids,
            "apply_to did not merge patch collision/backing solids into Layout");
    require(qmg::next_solid_id(layout).value > result.curves.front().source_id.value,
            "reserved patch source SolidId was not protected by backing IDs");
}

void stock_quake2_teleporter_contract() {
    qmg::TeleporterAssemblySpec spec;
    spec.key = {1};
    spec.trigger_bounds = {{32, 32, 0}, {96, 96, 48}};
    spec.destination_origin = {384, 64, 24};
    spec.destination_angle_degrees = 270;
    for (const auto game : {qmg::Game::quake1, qmg::Game::quake2, qmg::Game::quake3}) {
        qmg::Layout base;
        base.spec.game = game;
        const auto construction = qmg::ConstructionBuilder{}.build(base, {spec});
        const auto &source = placement_with_role(construction, qmg::PlacementRole::teleporter_trigger);
        const auto &destination = placement_with_role(construction, qmg::PlacementRole::teleporter_destination);
        require(source.origin == qmg::GridVec3{64, 64, 24} && destination.origin == spec.destination_origin &&
                    destination.angle_degrees == 270,
                "native teleporter conversion moved its disc or lost arrival facing");
        require(source.volume ==
                    (game == qmg::Game::quake2 ? qmg::GridBounds{{56, 56, 32}, {72, 72, 48}} : spec.trigger_bounds),
                "teleporter touch volume does not match its title's native mechanism");
    }
}

void vector_order_determinism() {
    qmg::Layout first_base = base_layout();
    qmg::Layout second_base = first_base;
    std::reverse(second_base.solids.begin(), second_base.solids.end());
    std::reverse(second_base.placements.begin(), second_base.placements.end());
    std::vector<qmg::ConstructionRequest> first_requests = assembly_requests();
    std::vector<qmg::ConstructionRequest> second_requests = first_requests;
    std::reverse(second_requests.begin(), second_requests.end());
    const qmg::ConstructionResult first = qmg::ConstructionBuilder{}.build(first_base, first_requests);
    const qmg::ConstructionResult second = qmg::ConstructionBuilder{}.build(second_base, second_requests);
    require(first == second, "construction changed when base/request vectors were reordered");
    first.apply_to(first_base);
    second.apply_to(second_base);
    require(first_base.solids == second_base.solids && first_base.placements == second_base.placements,
            "applied construction retained caller vector ordering");
}

void invalid_inputs_and_caps() {
    qmg::StairRunSpec stair;
    stair.key = {1};
    stair.start = {0, 0, 0};
    stair.width = 64;
    stair.tread_depth = 32;
    stair.riser_height = 16;
    stair.step_count = 4;

    qmg::StairRunSpec zero_key = stair;
    zero_key.key = {};
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {zero_key})); },
                  "invalid_construction_key", "zero construction key was accepted");
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {stair, stair})); },
                  "duplicate_construction_key", "duplicate construction keys were accepted");

    qmg::StairRunSpec empty_stair = stair;
    empty_stair.step_count = 0;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {empty_stair})); },
                  "invalid_stair_dimensions", "empty stair run was accepted");
    qmg::RampSpec ramp;
    ramp.key = {2};
    ramp.run = 0;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {ramp})); }, "invalid_ramp_dimensions",
                  "zero-run ramp was accepted");

    qmg::ArchSpec arch;
    arch.key = {3};
    arch.inner_radius = 4;
    arch.segment_count = 8;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {arch})); }, "segment_resolution",
                  "under-resolved integer-grid arch was accepted");
    arch.inner_radius = 32;
    arch.segment_count = 1;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {arch})); }, "segment_resolution",
                  "single non-convex arch span was accepted");

    qmg::CurvedWallSpec patch;
    patch.key = {4};
    patch.inner_radius = 4;
    patch.segment_count = 4;
    patch.emit_patch_veneer = true;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {patch})); }, "patch_resolution",
                  "under-resolved patch controls were accepted");

    qmg::CurvedWallSpec oversized_patch;
    oversized_patch.key = {40};
    oversized_patch.inner_radius = 64;
    oversized_patch.thickness = 16;
    oversized_patch.height = 128;
    oversized_patch.segment_count = 16;
    oversized_patch.emit_patch_veneer = true;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {oversized_patch})); },
                  "patch_dimension_limit", "patch wider than the portable 31-control limit was accepted");

    qmg::LiquidSpec dry;
    dry.key = {5};
    dry.bounds = {{0, 0, 0}, {16, 16, 16}};
    dry.contents = qmg::BrushContents::solid;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {dry})); }, "invalid_liquid_contents",
                  "solid contents were accepted as liquid");
    qmg::LiquidSpec detail_liquid = dry;
    detail_liquid.key = {50};
    detail_liquid.contents = qmg::BrushContents::water;
    detail_liquid.ownership = qmg::BrushOwnership::detail;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {detail_liquid})); },
                  "invalid_liquid_ownership", "detail-owned liquid was accepted");
    qmg::LiquidSpec placement_liquid = detail_liquid;
    placement_liquid.key = {51};
    placement_liquid.ownership = qmg::BrushOwnership::placement;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {placement_liquid})); },
                  "invalid_liquid_ownership", "placement-owned liquid was accepted");

    qmg::DoorAssemblySpec diagonal_door;
    diagonal_door.key = {6};
    diagonal_door.leaf_bounds = {{0, 0, 0}, {16, 16, 32}};
    diagonal_door.travel_delta = {16, 16, 0};
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {diagonal_door})); },
                  "invalid_mover_delta", "diagonal mover travel was accepted");

    qmg::LiftAssemblySpec horizontal_lift;
    horizontal_lift.key = {7};
    horizontal_lift.platform_bounds = {{0, 0, 0}, {64, 64, 16}};
    horizontal_lift.travel_delta = {32, 0, 0};
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {horizontal_lift})); },
                  "invalid_lift_delta", "horizontal lift travel was accepted");

    qmg::TeleporterAssemblySpec teleporter;
    teleporter.key = {8};
    teleporter.trigger_bounds = {{0, 0, 0}, {64, 64, 64}};
    teleporter.destination_origin = {128, 128, 16};
    teleporter.destination_angle_degrees = 360;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {teleporter})); }, "invalid_angle",
                  "out-of-range teleporter angle was accepted");

    qmg::ConstructionLimits small_world;
    small_world.max_coordinate = 100;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {stair}, small_world)); },
                  "coordinate_limit", "construction beyond the configured world bound was accepted");
    qmg::ConstructionLimits solid_cap;
    solid_cap.max_generated_solids = 3;
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build({}, {stair}, solid_cap)); }, "solid_limit",
                  "construction beyond the generated-solid cap was accepted");

    qmg::Layout duplicate_base = base_layout();
    duplicate_base.solids.push_back(duplicate_base.solids.front());
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build(duplicate_base, {stair})); },
                  "invalid_base_ids", "duplicate base SolidIds were accepted");
    qmg::Layout exhausted;
    exhausted.solids.push_back({qmg::SolidId{std::numeric_limits<std::uint32_t>::max()},
                                qmg::GridBounds{{0, 0, 0}, {1, 1, 1}}, qmg::MaterialRole::wall,
                                qmg::BrushPurpose::structural, qmg::BrushOwnership::structural,
                                qmg::BrushContents::solid, std::nullopt});
    require_error([&] { static_cast<void>(qmg::ConstructionBuilder{}.build(exhausted, {stair})); }, "id_exhausted",
                  "exhausted SolidId namespace was accepted");
}

void curve_control_bounds() {
    qmg::LayoutCurve curve;
    curve.width = 3;
    curve.height = 3;
    curve.control_points.resize(9);
    try {
        static_cast<void>(curve.control(3, 0));
    } catch (const std::out_of_range &) {
        return;
    }
    throw std::runtime_error("out-of-range curve control access was accepted");
}

} // namespace

int main() {
    try {
        exact_stair_runs();
        convex_ramps();
        segmented_arches();
        curves_and_patch_extras();
        orientation_matrix();
        quantized_geometry_matrix();
        liquids();
        typed_assemblies_and_apply();
        stock_quake2_teleporter_contract();
        patch_backing_is_applied();
        vector_order_determinism();
        invalid_inputs_and_caps();
        curve_control_bounds();
        std::cout << "construction tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "construction test failure: " << error.what() << '\n';
        return 1;
    }
}
