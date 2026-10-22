#include "qmg/construction.hpp"
#include "qmg/navigation.hpp"
#include "qmg/quake2_mechanisms.hpp"
#include "qmg/report.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

template <typename Function>
void require_navigation_error(Function &&function, const std::string_view code, const std::string_view message) {
    try {
        function();
    } catch (const qmg::NavigationError &error) {
        if (error.code() == code)
            return;
        throw std::runtime_error(std::string(message) + ": got " + error.code());
    }
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::Layout base_layout(const qmg::Game game, const qmg::GridBounds bounds) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.title = "Navigation fixture";
    layout.spec.theme = "industrial";
    layout.rooms.push_back({qmg::RoomId{1}, "room", bounds, {}});
    return layout;
}

[[nodiscard]] qmg::LayoutSolid box_solid(const std::uint32_t id, const qmg::GridBounds bounds,
                                         const qmg::BrushPurpose purpose,
                                         const qmg::BrushOwnership ownership = qmg::BrushOwnership::detail,
                                         const qmg::MaterialRole material = qmg::MaterialRole::platform) {
    qmg::LayoutSolid solid;
    solid.id = qmg::SolidId{id};
    solid.geometry = bounds;
    solid.material = material;
    solid.purpose = purpose;
    solid.ownership = ownership;
    return solid;
}

[[nodiscard]] qmg::Placement point_placement(const std::uint32_t id, const qmg::PlacementRole role,
                                             const qmg::GridVec3 origin) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    return placement;
}

[[nodiscard]] qmg::Placement volume_placement(const std::uint32_t id, const qmg::PlacementRole role,
                                              const qmg::GridBounds volume) {
    qmg::Placement placement = point_placement(id, role, volume.center());
    placement.volume = volume;
    return placement;
}

[[nodiscard]] bool has_finding(const qmg::NavigationAnalysis &analysis, const qmg::NavigationFindingKind kind,
                               const std::optional<std::uint32_t> source_id = std::nullopt) {
    return std::any_of(analysis.findings.begin(), analysis.findings.end(), [&](const qmg::NavigationFinding &finding) {
        return finding.kind == kind && (!source_id || (finding.source && finding.source->id == *source_id));
    });
}

[[nodiscard]] const qmg::NavigationNode *node_for(const qmg::NavigationAnalysis &analysis,
                                                  const qmg::NavigationSourceKind kind, const std::uint32_t id,
                                                  const std::uint32_t subindex = 0) {
    const auto found = std::find_if(
        analysis.graph.nodes().begin(), analysis.graph.nodes().end(),
        [&](const qmg::NavigationNode &node) { return node.source == qmg::NavigationSourceRef{kind, id, subindex}; });
    return found == analysis.graph.nodes().end() ? nullptr : &*found;
}

[[nodiscard]] bool has_transition_source(const qmg::NavigationAnalysis &analysis,
                                         const qmg::NavigationTransitionKind kind, const std::uint32_t placement_id) {
    return std::any_of(analysis.graph.edges().begin(), analysis.graph.edges().end(),
                       [&](const qmg::NavigationEdge &edge) {
                           return edge.kind == kind && edge.source.kind == qmg::NavigationSourceKind::placement &&
                                  edge.source.id == placement_id;
                       });
}

[[nodiscard]] qmg::NavigationPath placement_path(const qmg::NavigationAnalysis &analysis, const qmg::PlacementId from,
                                                 const qmg::PlacementId to) {
    const qmg::NavigationPlacementBinding *from_binding = analysis.find_binding(from);
    const qmg::NavigationPlacementBinding *to_binding = analysis.find_binding(to);
    require(from_binding != nullptr && from_binding->node && to_binding != nullptr && to_binding->node,
            "path fixture placement did not bind to navigation nodes");
    return qmg::shortest_navigation_path(analysis.graph, *from_binding->node, *to_binding->node, 4096);
}

void test_official_profiles_and_overrides() {
    // Official local references used by the profile adapter:
    // Quake-master/QW/client/pmove.c:36-37,56,681 (Q1 hull, step, jump);
    // Quake-2-master/qcommon/pmove.c:25,820,980-1024 (Q2);
    // Quake-III-Arena-master/code/game/bg_local.h:27,29 and bg_pmove.c:1269-1308 (Q3).
    const qmg::NavigationAgentProfile q1 =
        qmg::standard_navigation_profile(qmg::Game::quake1, qmg::NavigationAgentKind::player);
    const qmg::NavigationAgentProfile q2 =
        qmg::standard_navigation_profile(qmg::Game::quake2, qmg::NavigationAgentKind::player);
    const qmg::NavigationAgentProfile q3 =
        qmg::standard_navigation_profile(qmg::Game::quake3, qmg::NavigationAgentKind::player);
    require(q1.hull_mins == qmg::GridVec3{-16, -16, -24} && q1.hull_maxs == qmg::GridVec3{16, 16, 32} &&
                q1.max_step_up == 18 && q1.max_jump_up == 40,
            "Quake 1 player profile constants changed");
    require(q2.hull_mins == q1.hull_mins && q2.hull_maxs == q1.hull_maxs && q2.max_step_up == 18,
            "Quake 2 player profile constants changed");
    require(q3.hull_mins == qmg::GridVec3{-15, -15, -24} && q3.hull_maxs == qmg::GridVec3{15, 15, 32} &&
                q3.max_step_up == 18,
            "Quake 3 player profile constants changed");
    const qmg::NavigationAgentProfile q2_large =
        qmg::standard_navigation_profile(qmg::Game::quake2, qmg::NavigationAgentKind::monster_large);
    require(q2_large.hull_mins == qmg::GridVec3{-32, -32, -24} && q2_large.hull_maxs == qmg::GridVec3{32, 32, 64} &&
                q2_large.max_jump_up == 0 && q2_large.max_drop == 0,
            "large ground-monster profile is not conservative");
    require_navigation_error(
        [] {
            static_cast<void>(
                qmg::standard_navigation_profile(qmg::Game::quake3, qmg::NavigationAgentKind::monster_small));
        },
        "unsupported_agent_profile", "Quake 3 accepted a stock ground-monster profile");

    qmg::NavigationOptions mismatch;
    mismatch.profile_override = q1;
    const qmg::Layout q2_layout = base_layout(qmg::Game::quake2, {{0, 0, 0}, {128, 128, 128}});
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(q2_layout, mismatch)); },
                             "agent_title_mismatch", "custom hull profile from another title was accepted");
}

[[nodiscard]] qmg::GridVec3 ramp_point(const qmg::HorizontalDirection direction, const qmg::GridCoord run,
                                       const qmg::GridCoord across, const qmg::GridCoord z) {
    switch (direction) {
    case qmg::HorizontalDirection::positive_x:
        return {run, across, z};
    case qmg::HorizontalDirection::negative_x:
        return {-run, across, z};
    case qmg::HorizontalDirection::positive_y:
        return {across, run, z};
    case qmg::HorizontalDirection::negative_y:
        return {across, -run, z};
    }
    throw std::runtime_error("invalid ramp fixture direction");
}

[[nodiscard]] qmg::Layout ramp_layout(const qmg::Game game = qmg::Game::quake2,
                                      const qmg::HorizontalDirection direction = qmg::HorizontalDirection::positive_x,
                                      const qmg::GridCoord rise = 120, const qmg::GridCoord width = 160) {
    const auto bounds = [&](const qmg::GridVec3 low, const qmg::GridVec3 high) {
        const auto a = ramp_point(direction, low.x, low.y, low.z);
        const auto b = ramp_point(direction, high.x, high.y, high.z);
        return qmg::GridBounds{{std::min(a.x, b.x), std::min(a.y, b.y), low.z},
                               {std::max(a.x, b.x), std::max(a.y, b.y), high.z}};
    };
    qmg::Layout layout = base_layout(game, bounds({-128, 0, 0}, {736, width, 640}));
    layout.solids.push_back(
        box_solid(1, bounds({480, 0, 0}, {736, width, 8 + rise}), qmg::BrushPurpose::raised_platform));
    qmg::RampSpec ramp;
    ramp.key = qmg::ConstructionKey{1};
    ramp.direction = direction;
    ramp.width = width;
    ramp.run = 480;
    ramp.low_height = 8;
    ramp.rise = rise;
    qmg::ConstructionBuilder{}.build(layout, {ramp}).apply_to(layout);
    layout.placements.push_back(
        point_placement(1, qmg::PlacementRole::player_start, ramp_point(direction, -80, width / 2, 24)));
    layout.placements.push_back(
        point_placement(2, qmg::PlacementRole::weapon, ramp_point(direction, 640, width / 2, 32 + rise)));
    return layout;
}

[[nodiscard]] qmg::NavigationOptions ordinary_only(const qmg::Game game = qmg::Game::quake2) {
    qmg::NavigationOptions options;
    options.profile_override = qmg::standard_navigation_profile(game, qmg::NavigationAgentKind::player);
    options.profile_override->max_jump_up = 0;
    options.profile_override->max_jump_gap = 0;
    options.profile_override->max_drop = 0;
    return options;
}

void test_continuous_axial_ramp_navigation() {
    for (const auto game : {qmg::Game::quake1, qmg::Game::quake2, qmg::Game::quake3}) {
        for (const auto direction : {qmg::HorizontalDirection::positive_x, qmg::HorizontalDirection::negative_x,
                                     qmg::HorizontalDirection::positive_y, qmg::HorizontalDirection::negative_y}) {
            const auto layout = ramp_layout(game, direction);
            const auto analysis = qmg::NavigationAnalyzer{}.analyze(layout, ordinary_only(game));
            const auto *ramp = node_for(analysis, qmg::NavigationSourceKind::solid, 2);
            require(ramp && ramp->area.slope && ramp->origin == ramp_point(direction, 240, 80, 96),
                    "actual axial wedge did not produce a hull-aware continuous slope node");
            require(analysis.ok(), "ordinary ramp fixture has navigation findings");
            const auto up = placement_path(analysis, qmg::PlacementId{1}, qmg::PlacementId{2});
            const auto down = placement_path(analysis, qmg::PlacementId{2}, qmg::PlacementId{1});
            require(up.reachable() && down.reachable(), "continuous ramp needs jumping or cannot join both landings");
            require(std::find(up.nodes.begin(), up.nodes.end(), ramp->id) != up.nodes.end(),
                    "ordinary route bypassed the actual ramp support");
            for (const auto id : up.edges) {
                const auto kind = analysis.graph.find_edge(id)->kind;
                require(kind == qmg::NavigationTransitionKind::walk || kind == qmg::NavigationTransitionKind::step,
                        "ordinary ramp route used a special transition");
            }
            const qmg::GridCoord margin = game == qmg::Game::quake3 ? 15 : 16;
            const auto low = ramp_point(direction, margin, 80, 0);
            const auto high = ramp_point(direction, 480 - margin, 80, 0);
            require(qmg::navigation_floor_z(ramp->area, low.x, low.y) == 16 &&
                        qmg::navigation_floor_z(ramp->area, high.x, high.y) == 128,
                    "ramp feet do not use the uphill edge of the complete hull");
            auto reordered = layout;
            std::reverse(reordered.solids.begin(), reordered.solids.end());
            auto &planes = std::get<qmg::ConvexPolyhedron>(reordered.solids.front().geometry).planes;
            std::reverse(planes.begin(), planes.end());
            const auto second = qmg::NavigationAnalyzer{}.analyze(reordered, ordinary_only(game));
            require(analysis.graph.nodes() == second.graph.nodes() && analysis.graph.edges() == second.graph.edges(),
                    "ramp support changes when planes or collections are reordered");
        }
    }
}

void test_ramp_bindings_costs_and_rejections() {
    auto layout = ramp_layout();
    layout.placements.push_back(point_placement(3, qmg::PlacementRole::health, {32, 80, 44}));
    layout.placements.push_back(point_placement(4, qmg::PlacementRole::armor, {432, 80, 144}));
    auto options = ordinary_only();
    options.placement_snap_height = 0;
    const auto analysis = qmg::NavigationAnalyzer{}.analyze(layout, options);
    const auto *ramp = node_for(analysis, qmg::NavigationSourceKind::solid, 2);
    require(ramp && analysis.find_binding(qmg::PlacementId{3})->node == ramp->id &&
                analysis.find_binding(qmg::PlacementId{4})->node == ramp->id,
            "on-ramp placements were compared to the node center height instead of local support");
    const auto same = qmg::navigation_path_between_positions(analysis.graph, ramp->id, {32, 80, 999}, ramp->id,
                                                             {432, 80, -999}, options.cost_per_unit, 4096);
    require(same.cost == 500000, "same-ramp position cost omitted the 100-unit rise or trusted authored Z");
    const auto upper = *analysis.find_binding(qmg::PlacementId{2})->node;
    const auto approach = qmg::navigation_path_between_positions(analysis.graph, ramp->id, {32, 80, 44}, upper,
                                                                 {640, 80, 152}, options.cost_per_unit, 4096);
    require(approach.cost == 716000, "ramp endpoint approach or landing-gate travel cost was omitted");
    layout.placements.back().origin.z -= 4;
    require(!qmg::NavigationAnalyzer{}.analyze(layout, options).find_binding(qmg::PlacementId{4})->node,
            "zero-snap binding accepted point-center surface height inside the ramp hull");

    auto blocked = ramp_layout();
    blocked.solids.push_back(box_solid(3, {{241, 0, 80}, {242, 160, 81}}, qmg::BrushPurpose::hidden));
    const auto obstruction = qmg::NavigationAnalyzer{}.analyze(blocked, ordinary_only());
    require(!node_for(obstruction, qmg::NavigationSourceKind::solid, 2) &&
                has_finding(obstruction, qmg::NavigationFindingKind::insufficient_clearance, 2),
            "thin obstruction between endpoints escaped the continuous ramp hull proof");
    auto ceiling = ramp_layout();
    ceiling.rooms.front().bounds.max.z = 184;
    require(node_for(qmg::NavigationAnalyzer{}.analyze(ceiling, ordinary_only()), qmg::NavigationSourceKind::solid, 2),
            "exact ceiling contact was treated as positive hull overlap");
    ceiling.rooms.front().bounds.max.z = 183;
    require(!node_for(qmg::NavigationAnalyzer{}.analyze(ceiling, ordinary_only()), qmg::NavigationSourceKind::solid, 2),
            "ramp standing clearance ignored the highest end");
    require(
        !node_for(qmg::NavigationAnalyzer{}.analyze(
                      ramp_layout(qmg::Game::quake2, qmg::HorizontalDirection::positive_x, 120, 32), ordinary_only()),
                  qmg::NavigationSourceKind::solid, 2),
        "ramp as narrow as the hull emitted a positive-area node");
    const auto steep = qmg::NavigationAnalyzer{}.analyze(
        ramp_layout(qmg::Game::quake2, qmg::HorizontalDirection::positive_x, 481), ordinary_only());
    require(!node_for(steep, qmg::NavigationSourceKind::solid, 2) &&
                has_finding(steep, qmg::NavigationFindingKind::invalid_source_geometry, 2),
            "slope steeper than the conservative native ground limit became a flat top");
    const auto limit = qmg::NavigationAnalyzer{}.analyze(
        ramp_layout(qmg::Game::quake2, qmg::HorizontalDirection::positive_x, 480), ordinary_only());
    require(node_for(limit, qmg::NavigationSourceKind::solid, 2), "1:1 supported ground slope was rejected");
    require(!placement_path(limit, qmg::PlacementId{1}, qmg::PlacementId{2}).reachable(),
            "unclimbable low lip was hidden by the valid slope plane");
    auto unsupported = ramp_layout();
    unsupported.solids.back().purpose = qmg::BrushPurpose::raised_platform;
    require(
        !node_for(qmg::NavigationAnalyzer{}.analyze(unsupported, ordinary_only()), qmg::NavigationSourceKind::solid, 2),
        "unsupported convex support was treated as an aggregate-bounds flat top");
    auto gap = ramp_layout();
    std::get<qmg::GridBounds>(gap.solids.front().geometry).min.x += 1;
    const auto separated = qmg::NavigationAnalyzer{}.analyze(gap, ordinary_only());
    require(!placement_path(separated, qmg::PlacementId{1}, qmg::PlacementId{2}).reachable(),
            "a missing ramp landing contact was bridged by a fabricated ordinary edge");

    auto side = ramp_layout();
    side.rooms.front().bounds.max.y = 288;
    side.solids.push_back(box_solid(3, {{0, 160, 0}, {480, 288, 72}}, qmg::BrushPurpose::raised_platform));
    const auto side_analysis = qmg::NavigationAnalyzer{}.analyze(side, ordinary_only());
    const auto *side_ramp = node_for(side_analysis, qmg::NavigationSourceKind::solid, 2);
    const auto *side_deck = node_for(side_analysis, qmg::NavigationSourceKind::solid, 3);
    require(side_ramp && side_deck, "side-entry rejection fixture lost its clear support nodes");
    require(std::none_of(side_analysis.graph.edges().begin(), side_analysis.graph.edges().end(),
                         [&](const auto &edge) {
                             return (edge.from == side_ramp->id && edge.to == side_deck->id) ||
                                    (edge.from == side_deck->id && edge.to == side_ramp->id);
                         }),
            "a mid-slope side deck was incorrectly joined using the ramp's minimum/anchor floor");

    auto large_options = ordinary_only();
    large_options.profile_override =
        qmg::standard_navigation_profile(qmg::Game::quake2, qmg::NavigationAgentKind::monster_large);
    const auto large = qmg::NavigationAnalyzer{}.analyze(ramp_layout(), large_options);
    const auto *large_ramp = node_for(large, qmg::NavigationSourceKind::solid, 2);
    require(large_ramp && qmg::navigation_floor_z(large_ramp->area, 32, 80) == 24,
            "wide hull used player-width support on the slope");
    require(!placement_path(large, qmg::PlacementId{1}, qmg::PlacementId{2}).reachable(),
            "wide hull's 24-unit foot lip was accepted as an 18-unit ordinary step");

    qmg::NavigationArea negative{-464, 16, -16, 144, -112, 56, qmg::NavigationSlope{true, -480, 0, -128, 120, 16}};
    require(qmg::navigation_floor_z(negative, -463, 80) == -119,
            "negative fractional support height was not rounded toward positive infinity");
    require_navigation_error([&] { static_cast<void>(qmg::navigation_floor_z(negative, -480, 80)); },
                             "invalid_endpoint_position", "slope floor query accepted an out-of-area point");
    negative.slope->rise = 481;
    require_navigation_error([&] { static_cast<void>(qmg::navigation_floor_z(negative, -463, 80)); }, "invalid_slope",
                             "public floor query accepted unsupported slope metadata");
}

void test_report_preserves_continuous_navigation_surfaces() {
    for (const auto direction : {qmg::HorizontalDirection::positive_x, qmg::HorizontalDirection::negative_y}) {
        qmg::PipelineResult result;
        result.layout = ramp_layout(qmg::Game::quake2, direction);
        result.dialect = qmg::MapDialect::quake2_valve220;
        result.player_navigation = qmg::NavigationAnalyzer{}.analyze(result.layout);
        result.visibility = qmg::VisibilityAnalyzer{}.analyze(result.layout);
        result.lighting = qmg::LightingAnalyzer{}.analyze(result.layout);
        result.quality = qmg::QualityAnalyzer{}.analyze(
            result.layout, {*result.player_navigation, *result.visibility, *result.lighting});
        const std::string report = qmg::render_report(result);
        const std::string slope = direction == qmg::HorizontalDirection::positive_x
                                      ? "\"slope\": {\"along_x\": true, \"start\": 0, \"end\": 480, \"low_z\": 8, "
                                        "\"rise\": 120, \"support_offset\": 16}"
                                      : "\"slope\": {\"along_x\": false, \"start\": 0, \"end\": -480, \"low_z\": 8, "
                                        "\"rise\": 120, \"support_offset\": -16}";
        require(report.find(slope) != std::string::npos,
                "report discarded or changed the continuous slope's signed height function");
        require(report.find("\"floor_z\": 16, \"ceiling_z\": 184, " + slope) != std::string::npos &&
                    report.find("\"slope\": null") != std::string::npos,
                "report flattened a sloped area or made flat-area support ambiguous");
        const std::string origin =
            direction == qmg::HorizontalDirection::positive_x ? "\"origin\": [240,80,96]" : "\"origin\": [80,-240,96]";
        require(report.find(origin) != std::string::npos &&
                    report.find("\"hull_mins\": [-16,-16,-24]") != std::string::npos &&
                    report.find("\"hull_maxs\": [16,16,32]") != std::string::npos,
                "report cannot reconstruct the profile-dependent standing origin from the slope");
        require(report.find("\"kind\": \"step\"") != std::string::npos &&
                    report.find("\"kind\": \"walk\"") != std::string::npos &&
                    report.find("\"cost\": \"") != std::string::npos &&
                    report.find("\"placement_bindings\": [{\"placement\": 1, \"node\": ") != std::string::npos,
                "report omitted the graph evidence needed to audit ramp routes and anchor bindings");
        std::size_t exported_areas = 0;
        for (std::size_t position = 0; (position = report.find("\"area\": {", position)) != std::string::npos;
             ++position)
            ++exported_areas;
        require(exported_areas == result.player_navigation->graph.nodes().size(),
                "report did not export exactly one complete area per navigation node");
        require(report.find("\"player_spawn_eye_offset_z\": 22") != std::string::npos,
                "navigation report changes lost Q2 visibility eye metadata");
    }
}

void test_walk_step_jump_and_directional_drop() {
    qmg::Layout layout = base_layout(qmg::Game::quake1, {{0, 0, 0}, {512, 256, 256}});
    layout.solids.push_back(box_solid(1, {{96, 64, 0}, {176, 144, 18}}, qmg::BrushPurpose::navigation_stair));
    layout.solids.push_back(box_solid(2, {{224, 64, 0}, {304, 144, 40}}, qmg::BrushPurpose::raised_platform));
    layout.solids.push_back(box_solid(3, {{352, 64, 0}, {432, 144, 100}}, qmg::BrushPurpose::raised_platform));
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {32, 32, 24}));
    qmg::Placement objective = point_placement(2, qmg::PlacementRole::flag, {264, 104, 64});
    objective.team = qmg::Team::red;
    layout.placements.push_back(objective);

    const qmg::NavigationAnalysis analysis = qmg::NavigationAnalyzer{}.analyze(layout);
    const qmg::NavigationNode *step = node_for(analysis, qmg::NavigationSourceKind::solid, 1);
    const qmg::NavigationNode *jump = node_for(analysis, qmg::NavigationSourceKind::solid, 2);
    const qmg::NavigationNode *drop = node_for(analysis, qmg::NavigationSourceKind::solid, 3);
    require(step != nullptr && jump != nullptr && drop != nullptr, "walkable solid tops did not produce stable nodes");
    require(std::any_of(analysis.graph.edges().begin(), analysis.graph.edges().end(),
                        [&](const auto &edge) {
                            return edge.to == step->id && edge.kind == qmg::NavigationTransitionKind::step;
                        }),
            "18-unit stair did not produce an upward step edge");
    require(std::any_of(analysis.graph.edges().begin(), analysis.graph.edges().end(),
                        [&](const auto &edge) {
                            return edge.to == jump->id && edge.kind == qmg::NavigationTransitionKind::jump;
                        }),
            "40-unit platform did not produce an upward jump edge");
    require(std::any_of(analysis.graph.edges().begin(), analysis.graph.edges().end(),
                        [&](const auto &edge) {
                            return edge.from == drop->id && edge.kind == qmg::NavigationTransitionKind::drop;
                        }),
            "100-unit platform did not produce a downward drop edge");
    require(std::none_of(analysis.graph.edges().begin(), analysis.graph.edges().end(),
                         [&](const auto &edge) {
                             return edge.to == drop->id && (edge.kind == qmg::NavigationTransitionKind::step ||
                                                            edge.kind == qmg::NavigationTransitionKind::jump);
                         }),
            "directional drop incorrectly gained an upward traversal edge");
    const qmg::NavigationPath path = placement_path(analysis, qmg::PlacementId{1}, qmg::PlacementId{2});
    require(path.reachable() && path.cost > 0 && !path.edges.empty(),
            "spawn-to-objective jump path or deterministic cost is missing");
    require(analysis.graph.find_node(step->id) == step && analysis.graph.find_edge(path.edges.front()) != nullptr,
            "strong graph ID lookup failed");
    require(std::all_of(analysis.graph.edges().begin(), analysis.graph.edges().end(),
                        [](const auto &edge) { return edge.source.id != 0; }),
            "a navigation edge lacks stable Layout provenance");
}

void test_negative_hull_clearance() {
    qmg::Layout layout = base_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    layout.rooms.push_back({qmg::RoomId{2}, "east", {{256, 0, 0}, {384, 128, 128}}, {}});
    layout.connections.push_back(
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 28, 80, {{{128, 50, 0}, {256, 78, 80}}}});
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 24}));
    layout.placements.push_back(point_placement(2, qmg::PlacementRole::deathmatch_spawn, {320, 64, 24}));
    const qmg::NavigationAnalysis analysis = qmg::NavigationAnalyzer{}.analyze(layout);
    require(has_finding(analysis, qmg::NavigationFindingKind::insufficient_clearance, 1),
            "passage narrower than the hull did not produce a clearance finding");
    require(node_for(analysis, qmg::NavigationSourceKind::connection, 1) == nullptr,
            "negative-clearance passage emitted a navigation node");
    require(!placement_path(analysis, qmg::PlacementId{1}, qmg::PlacementId{2}).reachable(),
            "negative-clearance passage remained traversable");
    require(has_finding(analysis, qmg::NavigationFindingKind::unreachable_spawn, 2),
            "disconnected secondary spawn was not reported");

    qmg::Layout separated = base_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    separated.rooms.push_back({qmg::RoomId{2}, "nearby", {{144, 0, 0}, {272, 128, 128}}, {}});
    separated.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 24}));
    separated.placements.push_back(point_placement(2, qmg::PlacementRole::deathmatch_spawn, {208, 64, 24}));
    const qmg::NavigationAnalysis separated_analysis = qmg::NavigationAnalyzer{}.analyze(separated);
    require(!placement_path(separated_analysis, qmg::PlacementId{1}, qmg::PlacementId{2}).reachable(),
            "nearby free-space bounds bridged a physical gap without a passage");
}

void test_position_path_costs() {
    qmg::Layout layout = base_layout(qmg::Game::quake2, {{0, 0, 0}, {512, 512, 256}});
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {128, 256, 48}));
    const qmg::NavigationAnalysis one_room = qmg::NavigationAnalyzer{}.analyze(layout);
    require(one_room.graph.nodes().size() == 1, "position fixture must have one convex walking area");
    const qmg::NavigationNodeId west = one_room.graph.nodes().front().id;
    const qmg::NavigationPath local =
        qmg::navigation_path_between_positions(one_room.graph, west, {128, 256, 48}, west, {384, 256, 32}, 1000, 64);
    require(local.reachable() && local.cost == 256'000 && local.nodes.size() == 1 && local.edges.empty(),
            "different positions on the same node did not retain their 256-unit walking cost");
    const qmg::NavigationPath coincident =
        qmg::navigation_path_between_positions(one_room.graph, west, {128, 256, 48}, west, {128, 256, 32}, 1000, 64);
    require(coincident.cost == 0, "authored height offsets were incorrectly charged as walking distance");
    require(qmg::navigation_path_between_positions(one_room.graph, west, {128, 256, 48}, west, {384, 256, 32}, 2000, 64)
                    .cost == 512'000,
            "local position costs ignored the configured cost scale");
    require_navigation_error(
        [&] {
            static_cast<void>(qmg::navigation_path_between_positions(one_room.graph, west, {0, 0, 48}, west,
                                                                     {128, 256, 32}, 1000, 64));
        },
        "invalid_endpoint_position", "position query accepted an endpoint outside its walking area");
    require_navigation_error(
        [&] {
            static_cast<void>(qmg::navigation_path_between_positions(
                one_room.graph, west, {128, 256, 48}, west, {129, 256, 32}, qmg::kUnreachableNavigationCost, 64));
        },
        "cost_overflow", "a finite endpoint cost collided with the unreachable sentinel");
    require_navigation_error(
        [&] {
            static_cast<void>(qmg::navigation_path_between_positions(one_room.graph, west, {128, 256, 48}, west,
                                                                     {384, 256, 32}, 1000, 0));
        },
        "invalid_navigation_options", "same-area position query bypassed the path expansion policy");

    layout.rooms.push_back({qmg::RoomId{2}, "east", {{640, 0, 0}, {1152, 512, 256}}, {}});
    const qmg::NavigationAnalysis disconnected = qmg::NavigationAnalyzer{}.analyze(layout);
    const qmg::NavigationNode *disconnected_east = node_for(disconnected, qmg::NavigationSourceKind::room, 2);
    require(disconnected_east != nullptr, "disconnected destination did not produce a walking area");
    require(!qmg::navigation_path_between_positions(disconnected.graph, west, {128, 256, 48}, disconnected_east->id,
                                                    {1024, 256, 48}, 1000, 64)
                 .reachable(),
            "endpoint approach costs connected disconnected rooms");
    layout.connections.push_back(
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 256, 192, {{{512, 128, 0}, {640, 384, 192}}}});
    const qmg::NavigationAnalysis connected = qmg::NavigationAnalyzer{}.analyze(layout);
    const qmg::NavigationNode *connected_west = node_for(connected, qmg::NavigationSourceKind::room, 1);
    const qmg::NavigationNode *connected_east = node_for(connected, qmg::NavigationSourceKind::room, 2);
    require(connected_west != nullptr && connected_east != nullptr, "connected position fixture lacks its rooms");
    const qmg::NavigationPath graph_path =
        qmg::shortest_navigation_path(connected.graph, connected_west->id, connected_east->id, 64);
    const qmg::NavigationPath positions = qmg::navigation_path_between_positions(
        connected.graph, connected_west->id, {128, 256, 48}, connected_east->id, {1024, 256, 48}, 1000, 64);
    require(graph_path.cost == 640'000 && positions.cost == 896'000 && positions.nodes == graph_path.nodes &&
                positions.edges == graph_path.edges,
            "separate-node route omitted endpoint approaches or changed its transition evidence");

    qmg::NavigationOptions expensive;
    expensive.cost_per_unit = qmg::kUnreachableNavigationCost / 700U;
    const qmg::NavigationAnalysis expensive_graph = qmg::NavigationAnalyzer{}.analyze(layout, expensive);
    require_navigation_error(
        [&] {
            static_cast<void>(qmg::navigation_path_between_positions(expensive_graph.graph, connected_west->id,
                                                                     {128, 256, 48}, connected_east->id,
                                                                     {1024, 256, 48}, expensive.cost_per_unit, 64));
        },
        "cost_overflow", "summing finite graph and approach costs did not fail closed on overflow");
}

void test_exact_support_tiling_and_swept_transitions() {
    qmg::Layout partial = base_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 160}});
    partial.solids.push_back(box_solid(1, {{64, 32, 0}, {192, 96, 32}}, qmg::BrushPurpose::raised_platform));
    partial.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {32, 32, 24}));
    const qmg::NavigationAnalysis partial_analysis = qmg::NavigationAnalyzer{}.analyze(partial);
    const qmg::NavigationNode *partial_support = node_for(partial_analysis, qmg::NavigationSourceKind::solid, 1);
    require(partial_support != nullptr && partial_support->area.min_x == 80 && partial_support->area.max_x == 112 &&
                partial_support->area.min_y == 48 && partial_support->area.max_y == 80,
            "partially covered support top was not tiled to the exact free-space intersection");

    qmg::Layout same_level = base_layout(qmg::Game::quake1, {{0, 0, 0}, {288, 128, 160}});
    same_level.solids.push_back(box_solid(1, {{32, 32, 0}, {112, 96, 32}}, qmg::BrushPurpose::raised_platform));
    same_level.solids.push_back(box_solid(2, {{144, 32, 0}, {224, 96, 32}}, qmg::BrushPurpose::raised_platform));
    same_level.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 56}));
    const qmg::NavigationAnalysis jump_analysis = qmg::NavigationAnalyzer{}.analyze(same_level);
    const qmg::NavigationNode *first = node_for(jump_analysis, qmg::NavigationSourceKind::solid, 1);
    const qmg::NavigationNode *second = node_for(jump_analysis, qmg::NavigationSourceKind::solid, 2);
    require(first != nullptr && second != nullptr &&
                std::any_of(jump_analysis.graph.edges().begin(), jump_analysis.graph.edges().end(),
                            [&](const qmg::NavigationEdge &edge) {
                                return edge.from == first->id && edge.to == second->id &&
                                       edge.kind == qmg::NavigationTransitionKind::jump;
                            }),
            "same-level platform gap did not produce a bounded jump transition");

    qmg::Layout blocked = base_layout(qmg::Game::quake1, {{0, 0, 0}, {384, 128, 128}});
    blocked.solids.push_back(box_solid(1, {{176, 0, 0}, {184, 128, 128}}, qmg::BrushPurpose::hidden,
                                       qmg::BrushOwnership::detail, qmg::MaterialRole::hidden));
    blocked.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 24}));
    blocked.placements.push_back(point_placement(2, qmg::PlacementRole::deathmatch_spawn, {320, 64, 24}));
    const qmg::NavigationAnalysis blocked_analysis = qmg::NavigationAnalyzer{}.analyze(blocked);
    require(std::none_of(
                blocked_analysis.graph.edges().begin(), blocked_analysis.graph.edges().end(),
                [](const qmg::NavigationEdge &edge) { return edge.kind == qmg::NavigationTransitionKind::jump; }) &&
                !placement_path(blocked_analysis, qmg::PlacementId{1}, qmg::PlacementId{2}).reachable(),
            "sampled jump sweep crossed a full-height obstacle");
}

[[nodiscard]] qmg::Layout door_layout(const qmg::Game game) {
    qmg::Layout layout = base_layout(game, {{0, 0, 0}, {384, 128, 128}});
    qmg::Placement door = volume_placement(2, qmg::PlacementRole::door, {{176, 0, 0}, {192, 128, 96}});
    door.link = qmg::PlacementId{3};
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 24}));
    layout.placements.push_back(door);
    layout.placements.push_back(point_placement(3, qmg::PlacementRole::door_destination, {248, 64, 48}));
    qmg::Placement flag = point_placement(4, qmg::PlacementRole::flag, {320, 64, 24});
    flag.team = qmg::Team::blue;
    layout.placements.push_back(flag);
    qmg::LayoutSolid leaf =
        box_solid(1, *door.volume, qmg::BrushPurpose::door, qmg::BrushOwnership::placement, qmg::MaterialRole::door);
    leaf.owner = door.id;
    layout.solids.push_back(leaf);
    return layout;
}

void test_typed_door_transition() {
    const qmg::NavigationAnalysis analysis = qmg::NavigationAnalyzer{}.analyze(door_layout(qmg::Game::quake1));
    require(has_transition_source(analysis, qmg::NavigationTransitionKind::door, 2),
            "typed door did not emit a provenance-bearing navigation transition");
    const qmg::NavigationPath path = placement_path(analysis, qmg::PlacementId{1}, qmg::PlacementId{4});
    require(path.reachable(), "typed door did not connect its hull-cleared sides");
    require(std::any_of(path.edges.begin(), path.edges.end(),
                        [&](const qmg::NavigationEdgeId id) {
                            const qmg::NavigationEdge *edge = analysis.graph.find_edge(id);
                            return edge != nullptr && edge->kind == qmg::NavigationTransitionKind::door;
                        }),
            "spawn-to-objective path bypassed the blocking door");

    qmg::Layout invalid = door_layout(qmg::Game::quake1);
    invalid.placements[2].origin.y += 32;
    const qmg::NavigationAnalysis invalid_analysis = qmg::NavigationAnalyzer{}.analyze(invalid);
    require(has_finding(invalid_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(invalid_analysis, qmg::NavigationTransitionKind::door, 2),
            "non-axis-aligned door travel produced a typed transition");

    qmg::Layout insufficient = door_layout(qmg::Game::quake1);
    insufficient.placements[2].origin.x = 216;
    const qmg::NavigationAnalysis insufficient_analysis = qmg::NavigationAnalyzer{}.analyze(insufficient);
    require(has_finding(insufficient_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(insufficient_analysis, qmg::NavigationTransitionKind::door, 2),
            "insufficient door travel left the open leaf inside the hull corridor");

    qmg::Layout wrong_axis = door_layout(qmg::Game::quake1);
    wrong_axis.rooms.front().bounds.max.y = 192;
    wrong_axis.placements[2].origin = {184, 96, 48};
    const qmg::NavigationAnalysis wrong_axis_analysis = qmg::NavigationAnalyzer{}.analyze(wrong_axis);
    require(has_finding(wrong_axis_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(wrong_axis_analysis, qmg::NavigationTransitionKind::door, 2),
            "wrong-axis door travel left the open leaf inside the hull corridor");

    qmg::Layout vertical = door_layout(qmg::Game::quake1);
    vertical.rooms.front().bounds.max.z = 192;
    vertical.placements[2].origin = {184, 64, 144};
    const qmg::NavigationAnalysis vertical_analysis = qmg::NavigationAnalyzer{}.analyze(vertical);
    require(has_transition_source(vertical_analysis, qmg::NavigationTransitionKind::door, 2),
            "sufficient vertical door travel was rejected despite clearing the hull corridor");

    qmg::Layout obstructed = door_layout(qmg::Game::quake1);
    obstructed.solids.push_back(box_solid(2, {{212, 0, 0}, {220, 8, 96}}, qmg::BrushPurpose::hidden,
                                          qmg::BrushOwnership::detail, qmg::MaterialRole::hidden));
    const qmg::NavigationAnalysis obstructed_analysis = qmg::NavigationAnalyzer{}.analyze(obstructed);
    require(has_finding(obstructed_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(obstructed_analysis, qmg::NavigationTransitionKind::door, 2),
            "door mover sweep crossed an intervening solid blocker");
}

[[nodiscard]] qmg::Layout lift_layout() {
    qmg::Layout layout = base_layout(qmg::Game::quake2, {{0, 0, 0}, {256, 256, 320}});
    qmg::Placement lift = volume_placement(2, qmg::PlacementRole::lift, {{64, 64, 0}, {160, 160, 16}});
    lift.link = qmg::PlacementId{3};
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {32, 32, 24}));
    layout.placements.push_back(lift);
    layout.placements.push_back(point_placement(3, qmg::PlacementRole::lift_destination, {112, 112, 136}));
    qmg::Placement flag = point_placement(4, qmg::PlacementRole::flag, {112, 112, 168});
    flag.team = qmg::Team::red;
    layout.placements.push_back(flag);
    qmg::LayoutSolid platform =
        box_solid(1, *lift.volume, qmg::BrushPurpose::lift, qmg::BrushOwnership::placement, qmg::MaterialRole::lift);
    platform.owner = lift.id;
    layout.solids.push_back(platform);
    return layout;
}

void test_typed_lift_transition() {
    const qmg::NavigationAnalysis analysis = qmg::NavigationAnalyzer{}.analyze(lift_layout());
    require(node_for(analysis, qmg::NavigationSourceKind::placement, 2, 0) != nullptr &&
                node_for(analysis, qmg::NavigationSourceKind::placement, 2, 1) != nullptr,
            "lift endpoints did not receive stable placement provenance nodes");
    require(has_transition_source(analysis, qmg::NavigationTransitionKind::lift, 2),
            "typed lift did not emit a provenance-bearing transition");
    const qmg::NavigationPath path = placement_path(analysis, qmg::PlacementId{1}, qmg::PlacementId{4});
    require(path.reachable(), "lift destination objective is unreachable");
    require(std::any_of(path.edges.begin(), path.edges.end(),
                        [&](const qmg::NavigationEdgeId id) {
                            const qmg::NavigationEdge *edge = analysis.graph.find_edge(id);
                            return edge != nullptr && edge->kind == qmg::NavigationTransitionKind::lift;
                        }),
            "path to elevated lift objective omitted the lift transition");

    qmg::Layout split = lift_layout();
    split.rooms.front().bounds.max.x = 112;
    split.rooms.push_back({qmg::RoomId{2}, "east", {{112, 0, 0}, {256, 256, 304}}, {}});
    const qmg::NavigationAnalysis split_analysis = qmg::NavigationAnalyzer{}.analyze(split);
    const std::size_t lift_edges = static_cast<std::size_t>(std::count_if(
        split_analysis.graph.edges().begin(), split_analysis.graph.edges().end(), [](const qmg::NavigationEdge &edge) {
            return edge.kind == qmg::NavigationTransitionKind::lift &&
                   edge.source == qmg::NavigationSourceRef{qmg::NavigationSourceKind::placement, 2, 0};
        }));
    require(lift_edges == 4, "lift did not connect every compatible lower/upper endpoint fragment deterministically");

    qmg::Layout blocked = lift_layout();
    blocked.solids.push_back(box_solid(2, {{64, 64, 72}, {160, 160, 80}}, qmg::BrushPurpose::hidden,
                                       qmg::BrushOwnership::detail, qmg::MaterialRole::hidden));
    const qmg::NavigationAnalysis blocked_analysis = qmg::NavigationAnalyzer{}.analyze(blocked);
    require(has_finding(blocked_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(blocked_analysis, qmg::NavigationTransitionKind::lift, 2),
            "lift mover sweep crossed an intervening shaft blocker between clear endpoints");
}

void test_mover_ownership_and_proximity() {
    qmg::Layout ownerless_door = door_layout(qmg::Game::quake1);
    ownerless_door.solids.front().owner.reset();
    const qmg::NavigationAnalysis ownerless_door_analysis = qmg::NavigationAnalyzer{}.analyze(ownerless_door);
    require(has_finding(ownerless_door_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(ownerless_door_analysis, qmg::NavigationTransitionKind::door, 2),
            "door without an exact placement-owned solid produced a transition");

    qmg::Layout distant = base_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    distant.rooms.push_back({qmg::RoomId{2}, "remote", {{384, 0, 0}, {512, 128, 128}}, {}});
    distant.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 24}));
    qmg::Placement door = volume_placement(2, qmg::PlacementRole::door, {{240, 0, 0}, {256, 128, 96}});
    door.link = qmg::PlacementId{3};
    distant.placements.push_back(door);
    distant.placements.push_back(point_placement(3, qmg::PlacementRole::door_destination, {312, 64, 48}));
    distant.placements.push_back(point_placement(4, qmg::PlacementRole::deathmatch_spawn, {448, 64, 24}));
    qmg::LayoutSolid leaf =
        box_solid(1, *door.volume, qmg::BrushPurpose::door, qmg::BrushOwnership::placement, qmg::MaterialRole::door);
    leaf.owner = door.id;
    distant.solids.push_back(leaf);
    const qmg::NavigationAnalysis distant_analysis = qmg::NavigationAnalyzer{}.analyze(distant);
    require(has_finding(distant_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(distant_analysis, qmg::NavigationTransitionKind::door, 2) &&
                !placement_path(distant_analysis, qmg::PlacementId{1}, qmg::PlacementId{4}).reachable(),
            "door connected navigation areas beyond the bounded mover-side distance");

    qmg::Layout ownerless_lift = lift_layout();
    ownerless_lift.solids.front().ownership = qmg::BrushOwnership::detail;
    const qmg::NavigationAnalysis ownerless_lift_analysis = qmg::NavigationAnalyzer{}.analyze(ownerless_lift);
    require(has_finding(ownerless_lift_analysis, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(ownerless_lift_analysis, qmg::NavigationTransitionKind::lift, 2) &&
                node_for(ownerless_lift_analysis, qmg::NavigationSourceKind::placement, 2, 0) == nullptr,
            "lift without an exact placement-owned solid produced endpoint navigation");
}

[[nodiscard]] qmg::Layout teleporter_layout() {
    qmg::Layout layout = base_layout(qmg::Game::quake3, {{0, 0, 0}, {128, 128, 128}});
    layout.rooms.push_back({qmg::RoomId{2}, "remote", {{512, 0, 0}, {640, 128, 128}}, {}});
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {24, 24, 24}));
    qmg::Placement trigger = volume_placement(2, qmg::PlacementRole::teleporter_trigger, {{32, 32, 0}, {96, 96, 64}});
    trigger.link = qmg::PlacementId{3};
    layout.placements.push_back(trigger);
    layout.placements.push_back(point_placement(3, qmg::PlacementRole::teleporter_destination, {576, 64, 24}));
    qmg::Placement flag = point_placement(4, qmg::PlacementRole::flag, {608, 96, 24});
    flag.team = qmg::Team::red;
    layout.placements.push_back(flag);
    return layout;
}

void test_typed_teleporter_transition() {
    qmg::NavigationOptions options;
    options.teleporter_penalty = 777;
    const qmg::NavigationAnalysis analysis = qmg::NavigationAnalyzer{}.analyze(teleporter_layout(), options);
    require(has_transition_source(analysis, qmg::NavigationTransitionKind::teleporter, 2),
            "typed teleporter did not emit a directed provenance-bearing transition");
    require(std::any_of(analysis.graph.edges().begin(), analysis.graph.edges().end(),
                        [](const auto &edge) {
                            return edge.kind == qmg::NavigationTransitionKind::teleporter && edge.cost == 777;
                        }),
            "teleporter path cost incorrectly depends on remote coordinate distance");
    require(placement_path(analysis, qmg::PlacementId{1}, qmg::PlacementId{4}).reachable(),
            "teleporter did not make the remote objective reachable");
    require(!placement_path(analysis, qmg::PlacementId{4}, qmg::PlacementId{1}).reachable(),
            "one-way teleporter incorrectly gained a reverse edge");
}

void test_quake2_native_teleporter_clearance() {
    qmg::Layout layout = teleporter_layout();
    layout.spec.game = qmg::Game::quake2;
    layout.placements[1].origin = {64, 64, 24};
    layout.placements[1].volume = qmg::quake2_teleporter_bounds(layout.placements[1].origin, true);
    layout.placements[2].angle_degrees = 270;
    const auto valid = qmg::NavigationAnalyzer{}.analyze(layout);
    require(has_transition_source(valid, qmg::NavigationTransitionKind::teleporter, 2),
            "supported stock Q2 discs with a clear native landing did not connect");

    auto low_ceiling = layout;
    low_ceiling.rooms[1].bounds.max.z = 60;
    const auto blocked = qmg::NavigationAnalyzer{}.analyze(low_ceiling);
    const auto *destination = blocked.find_binding(qmg::PlacementId{3});
    require(destination && destination->node, "arrival-clearance regression must still fit a normally standing player");
    require(has_finding(blocked, qmg::NavigationFindingKind::unresolved_transition, 2) &&
                !has_transition_source(blocked, qmg::NavigationTransitionKind::teleporter, 2),
            "Q2 teleporter accepted the destination+10 landing hull through a low ceiling");

    auto low_source = layout;
    low_source.rooms[0].bounds.max.z = 60;
    require(!has_transition_source(qmg::NavigationAnalyzer{}.analyze(low_source),
                                   qmg::NavigationTransitionKind::teleporter, 2),
            "Q2 source disc did not require headroom to step onto its eight-unit solid pad");

    auto oversized = layout;
    oversized.placements[1].volume = qmg::GridBounds{{32, 32, 0}, {96, 96, 64}};
    const auto oversized_analysis = qmg::NavigationAnalyzer{}.analyze(oversized);
    require(!has_transition_source(oversized_analysis, qmg::NavigationTransitionKind::teleporter, 2),
            "Q2 navigation accepted a touch volume absent from the native entity");

    auto unsupported_pad = layout;
    unsupported_pad.placements[1].origin.z += 8;
    unsupported_pad.placements[1].volume = qmg::quake2_teleporter_bounds(unsupported_pad.placements[1].origin, true);
    require(!has_transition_source(qmg::NavigationAnalyzer{}.analyze(unsupported_pad),
                                   qmg::NavigationTransitionKind::teleporter, 2),
            "floating Q2 source disc was accepted by floor snapping alone");

    auto wall_clipped_pad = layout;
    wall_clipped_pad.placements[2].origin.x = 536;
    require(!has_transition_source(qmg::NavigationAnalyzer{}.analyze(wall_clipped_pad),
                                   qmg::NavigationTransitionKind::teleporter, 2),
            "Q2 destination disc was allowed to protrude through a wall despite clear player XY");

    qmg::NavigationOptions monster_options;
    monster_options.agent_kind = qmg::NavigationAgentKind::monster_small;
    require(!has_transition_source(qmg::NavigationAnalyzer{}.analyze(layout, monster_options),
                                   qmg::NavigationTransitionKind::teleporter, 2),
            "Q2 ground monsters received a player-only native teleporter edge");
}

[[nodiscard]] qmg::Layout monster_corridor(const qmg::Game game) {
    qmg::Layout layout = base_layout(game, {{0, 0, 0}, {128, 128, 128}});
    layout.rooms.push_back({qmg::RoomId{2}, "monster", {{256, 0, 0}, {384, 128, 128}}, {}});
    layout.connections.push_back(
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 48, 96, {{{128, 40, 0}, {256, 88, 96}}}});
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 24}));
    layout.placements.push_back(point_placement(2, qmg::PlacementRole::monster, {320, 64, 24}));
    return layout;
}

void test_q1_q2_monster_profiles() {
    for (const qmg::Game game : {qmg::Game::quake1, qmg::Game::quake2}) {
        const qmg::Layout layout = monster_corridor(game);
        qmg::NavigationOptions small_options;
        small_options.agent_kind = qmg::NavigationAgentKind::monster_small;
        const qmg::NavigationAnalysis small = qmg::NavigationAnalyzer{}.analyze(layout, small_options);
        require(!has_finding(small, qmg::NavigationFindingKind::unreachable_monster, 2),
                "small Q1/Q2 ground monster could not traverse a 48-unit passage");
        const qmg::NavigationPlacementBinding *monster = small.find_binding(qmg::PlacementId{2});
        const qmg::NavigationPlacementBinding *spawn = small.find_binding(qmg::PlacementId{1});
        require(monster && monster->node && spawn && spawn->node &&
                    qmg::shortest_navigation_path(small.graph, *monster->node, *spawn->node, 4096).reachable(),
                "small-monster path query did not reach the player spawn");

        qmg::NavigationOptions large_options;
        large_options.agent_kind = qmg::NavigationAgentKind::monster_large;
        const qmg::NavigationAnalysis large = qmg::NavigationAnalyzer{}.analyze(layout, large_options);
        require(has_finding(large, qmg::NavigationFindingKind::insufficient_clearance, 1) &&
                    has_finding(large, qmg::NavigationFindingKind::unreachable_monster, 2),
                "large Q1/Q2 monster was not blocked by a passage narrower than hull 2");
    }
}

void test_reachability_findings() {
    qmg::Layout layout = base_layout(qmg::Game::quake2, {{0, 0, 0}, {128, 128, 128}});
    layout.rooms.push_back({qmg::RoomId{2}, "spawn", {{256, 0, 0}, {384, 128, 128}}, {}});
    layout.rooms.push_back({qmg::RoomId{3}, "objective", {{512, 0, 0}, {640, 128, 128}}, {}});
    layout.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 24}));
    layout.placements.push_back(point_placement(2, qmg::PlacementRole::deathmatch_spawn, {320, 64, 24}));
    qmg::Placement flag = point_placement(3, qmg::PlacementRole::flag, {560, 48, 24});
    flag.team = qmg::Team::blue;
    layout.placements.push_back(flag);
    layout.placements.push_back(point_placement(4, qmg::PlacementRole::monster, {608, 80, 24}));
    const qmg::NavigationAnalysis analysis = qmg::NavigationAnalyzer{}.analyze(layout);
    require(has_finding(analysis, qmg::NavigationFindingKind::unreachable_spawn, 2),
            "disconnected spawn finding is missing");
    require(has_finding(analysis, qmg::NavigationFindingKind::unreachable_objective, 3),
            "disconnected objective finding is missing");
    require(has_finding(analysis, qmg::NavigationFindingKind::unreachable_monster, 4),
            "disconnected monster finding is missing");
}

void test_collection_reorder_invariance() {
    qmg::Layout ordered = door_layout(qmg::Game::quake1);
    ordered.rooms.push_back({qmg::RoomId{2}, "annex", {{384, 0, 0}, {512, 128, 128}}, {}});
    ordered.rooms.push_back({qmg::RoomId{3}, "far annex", {{512, 0, 0}, {640, 128, 128}}, {}});
    ordered.connections.push_back({qmg::ConnectionId{1},
                                   qmg::RoomId{1},
                                   qmg::RoomId{2},
                                   96,
                                   96,
                                   {{{368, 16, 0}, {448, 112, 96}}, {{448, 16, 0}, {512, 112, 96}}}});
    ordered.connections.push_back(
        {qmg::ConnectionId{2}, qmg::RoomId{2}, qmg::RoomId{3}, 96, 96, {{{496, 16, 0}, {528, 112, 96}}}});
    ordered.solids.push_back(box_solid(2, {{544, 32, 0}, {576, 96, 64}}, qmg::BrushPurpose::trigger_volume,
                                       qmg::BrushOwnership::placement, qmg::MaterialRole::trigger));
    qmg::Layout shuffled = ordered;
    std::reverse(shuffled.rooms.begin(), shuffled.rooms.end());
    std::reverse(shuffled.connections.begin(), shuffled.connections.end());
    for (qmg::Connection &connection : shuffled.connections)
        std::reverse(connection.passage_volumes.begin(), connection.passage_volumes.end());
    std::reverse(shuffled.solids.begin(), shuffled.solids.end());
    std::reverse(shuffled.placements.begin(), shuffled.placements.end());
    const qmg::NavigationAnalysis first = qmg::NavigationAnalyzer{}.analyze(ordered);
    const qmg::NavigationAnalysis second = qmg::NavigationAnalyzer{}.analyze(shuffled);
    require(first.profile == second.profile && first.graph.nodes() == second.graph.nodes() &&
                first.graph.edges() == second.graph.edges() && first.placement_bindings == second.placement_bindings &&
                first.findings == second.findings && first.stats == second.stats && !first.layout_sha256.empty() &&
                first.layout_sha256 == second.layout_sha256,
            "Layout collection reorder changed navigation IDs, provenance, findings, or statistics");

    qmg::Layout changed = ordered;
    changed.rooms.front().bounds.max.z += 1;
    const qmg::NavigationAnalysis changed_analysis = qmg::NavigationAnalyzer{}.analyze(changed);
    require(changed_analysis.layout_sha256 != first.layout_sha256,
            "navigation analysis fingerprint did not reject stale changed Layout evidence");
}

void test_caps_and_overflow() {
    const qmg::Layout door = door_layout(qmg::Game::quake1);
    qmg::Layout two_rooms = door;
    two_rooms.rooms.push_back({qmg::RoomId{2}, "extra", {{512, 0, 0}, {640, 128, 128}}, {}});

    qmg::NavigationOptions room_cap;
    room_cap.max_rooms = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(two_rooms, room_cap)); },
                             "room_limit", "top-level room cap was not enforced before sorting");

    qmg::Layout two_connections = base_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    two_connections.rooms.push_back({qmg::RoomId{2}, "east", {{256, 0, 0}, {384, 128, 128}}, {}});
    two_connections.connections.push_back(
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 64, 96, {{{128, 32, 0}, {256, 96, 96}}}});
    two_connections.connections.push_back(
        {qmg::ConnectionId{2}, qmg::RoomId{1}, qmg::RoomId{2}, 64, 96, {{{128, 32, 0}, {256, 96, 96}}}});
    qmg::NavigationOptions connection_cap;
    connection_cap.max_connections = 1;
    require_navigation_error(
        [&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(two_connections, connection_cap)); },
        "connection_limit", "top-level connection cap was not enforced before sorting");

    qmg::Layout two_solids = door;
    two_solids.solids.push_back(box_solid(2, {{32, 32, 0}, {64, 64, 32}}, qmg::BrushPurpose::trigger_volume));
    qmg::NavigationOptions solid_cap;
    solid_cap.max_solids = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(two_solids, solid_cap)); },
                             "solid_limit", "top-level solid cap was not enforced before sorting");

    qmg::NavigationOptions placement_cap;
    placement_cap.max_placements = 3;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(door, placement_cap)); },
                             "placement_limit", "top-level placement cap was not enforced before sorting");

    qmg::Layout passages = base_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    passages.rooms.push_back({qmg::RoomId{2}, "east", {{256, 0, 0}, {384, 128, 128}}, {}});
    passages.connections.push_back({qmg::ConnectionId{1},
                                    qmg::RoomId{1},
                                    qmg::RoomId{2},
                                    64,
                                    96,
                                    {{{128, 32, 0}, {192, 96, 96}}, {{192, 32, 0}, {256, 96, 96}}}});
    qmg::NavigationOptions passage_cap;
    passage_cap.max_passage_volumes = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(passages, passage_cap)); },
                             "passage_limit", "raw passage-volume cap was not enforced before copying");

    qmg::Layout convex = base_layout(qmg::Game::quake1, {{0, 0, 0}, {128, 128, 128}});
    qmg::LayoutSolid excessive_planes = box_solid(1, {{32, 32, 0}, {96, 96, 32}}, qmg::BrushPurpose::raised_platform);
    qmg::ConvexPolyhedron polyhedron;
    polyhedron.planes.resize(5);
    excessive_planes.geometry = polyhedron;
    convex.solids.push_back(excessive_planes);
    qmg::NavigationOptions plane_cap;
    plane_cap.max_convex_planes = 4;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(convex, plane_cap)); },
                             "convex_plane_limit", "convex-plane cap was not enforced before validation");

    qmg::NavigationOptions source_cap;
    source_cap.max_source_volumes = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(two_rooms, source_cap)); },
                             "source_volume_limit", "source-volume cap was not enforced");

    qmg::NavigationOptions cell_cap;
    cell_cap.max_grid_cells = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(door, cell_cap)); },
                             "grid_cell_limit", "coordinate-compression cell cap was not enforced");

    qmg::NavigationOptions edge_cap;
    edge_cap.max_edges = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(door, edge_cap)); },
                             "edge_limit", "navigation edge cap was not enforced");

    qmg::NavigationOptions node_cap;
    node_cap.max_nodes = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(door, node_cap)); },
                             "node_limit", "navigation node cap was not enforced");

    qmg::NavigationOptions pair_cap;
    pair_cap.max_pair_checks = 1;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(lift_layout(), pair_cap)); },
                             "pair_check_limit", "node-pair cap was not enforced");

    const qmg::GridCoord maximum = std::numeric_limits<qmg::GridCoord>::max();
    qmg::Layout overflow = base_layout(qmg::Game::quake1, {{maximum - 256, 0, 0}, {maximum, 256, 128}});
    overflow.solids.push_back(box_solid(1, {{maximum - 32, 64, 0}, {maximum, 96, 64}}, qmg::BrushPurpose::hidden,
                                        qmg::BrushOwnership::detail, qmg::MaterialRole::hidden));
    overflow.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {maximum - 128, 128, 24}));
    qmg::NavigationOptions wide;
    wide.max_coordinate = std::numeric_limits<qmg::GridCoord>::max();
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(overflow, wide)); },
                             "coordinate_overflow", "hull expansion coordinate overflow was not rejected");

    qmg::Layout out_of_range_placement = door_layout(qmg::Game::quake1);
    out_of_range_placement.placements.front().origin.x = 40000;
    const qmg::NavigationAnalysis unbound = qmg::NavigationAnalyzer{}.analyze(out_of_range_placement);
    require(has_finding(unbound, qmg::NavigationFindingKind::unbound_placement, 1),
            "out-of-range navigation placement was not rejected deterministically");

    const qmg::NavigationAnalysis graph = qmg::NavigationAnalyzer{}.analyze(door);
    const qmg::NavigationPlacementBinding *spawn = graph.find_binding(qmg::PlacementId{1});
    require(spawn && spawn->node, "path-cap fixture spawn did not bind");
    require_navigation_error([&] { static_cast<void>(qmg::reachable_navigation_nodes(graph.graph, *spawn->node, 1)); },
                             "path_expansion_limit", "reachability expansion cap was not enforced");

    qmg::NavigationOptions cost_overflow;
    cost_overflow.cost_per_unit = std::numeric_limits<qmg::NavigationCost>::max();
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(door, cost_overflow)); },
                             "cost_overflow", "navigation edge cost overflow was not rejected");

    qmg::NavigationOptions invalid;
    invalid.max_nodes = 0;
    require_navigation_error([&] { static_cast<void>(qmg::NavigationAnalyzer{}.analyze(door, invalid)); },
                             "invalid_navigation_options", "zero navigation cap was accepted");
}

} // namespace

int main() {
    try {
        test_official_profiles_and_overrides();
        test_continuous_axial_ramp_navigation();
        test_ramp_bindings_costs_and_rejections();
        test_report_preserves_continuous_navigation_surfaces();
        test_walk_step_jump_and_directional_drop();
        test_negative_hull_clearance();
        test_position_path_costs();
        test_exact_support_tiling_and_swept_transitions();
        test_typed_door_transition();
        test_typed_lift_transition();
        test_mover_ownership_and_proximity();
        test_typed_teleporter_transition();
        test_quake2_native_teleporter_clearance();
        test_q1_q2_monster_profiles();
        test_reachability_findings();
        test_collection_reorder_invariance();
        test_caps_and_overflow();
        std::cout << "Navigation analyzer tests passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Navigation analyzer test failure: " << error.what() << '\n';
        return 1;
    }
}
