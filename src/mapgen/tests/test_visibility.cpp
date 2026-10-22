#include "qmg/visibility.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(const bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Function>
void require_error(Function &&function, const std::string &code, const std::string &message) {
    try {
        function();
    } catch (const qmg::VisibilityError &error) {
        require(error.code() == code, message + " (wrong error code: " + error.code() + ")");
        return;
    }
    throw std::runtime_error(message);
}

qmg::Layout single_room(const qmg::Game game = qmg::Game::quake3,
                        const qmg::GameMode mode = qmg::GameMode::deathmatch) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.mode = mode;
    layout.spec.target_players = mode == qmg::GameMode::capture_the_flag ? 2 : 2;
    layout.rooms.push_back({qmg::RoomId{1}, "room", {{0, 0, 0}, {512, 512, 320}}, {}});
    return layout;
}

qmg::Placement point_placement(const std::uint32_t id, const qmg::PlacementRole role, const qmg::GridVec3 origin,
                               const std::optional<qmg::Team> team = std::nullopt) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    placement.team = team;
    return placement;
}

qmg::LayoutSolid box_solid(const std::uint32_t id, const qmg::GridBounds bounds,
                           const qmg::BrushPurpose purpose = qmg::BrushPurpose::structural,
                           const qmg::MaterialRole material = qmg::MaterialRole::wall,
                           const qmg::BrushContents contents = qmg::BrushContents::solid) {
    return {qmg::SolidId{id}, bounds, material, purpose, qmg::BrushOwnership::structural, contents, std::nullopt};
}

qmg::GridVec3 shifted(const qmg::GridVec3 point, const qmg::GridVec3 offset) {
    return {qmg::checked_grid_add(point.x, offset.x), qmg::checked_grid_add(point.y, offset.y),
            qmg::checked_grid_add(point.z, offset.z)};
}

qmg::ConvexPolyhedron shifted_tetrahedron(const qmg::GridVec3 offset) {
    qmg::ConvexPolyhedron result{{
        {{0, 0, 0}, {0, 0, 100}, {0, 100, 0}},
        {{0, 0, 0}, {100, 0, 0}, {0, 0, 100}},
        {{0, 0, 0}, {0, 100, 0}, {100, 0, 0}},
        {{100, 0, 0}, {0, 100, 0}, {0, 0, 100}},
    }};
    for (qmg::PlanePointTriple &plane : result.planes) {
        plane.first = shifted(plane.first, offset);
        plane.second = shifted(plane.second, offset);
        plane.third = shifted(plane.third, offset);
    }
    return result;
}

void public_segment_queries() {
    const qmg::VisibilityAnalyzer analyzer;
    qmg::Layout layout = single_room();
    require(analyzer.classify_segment(layout, {32, 32, 64}, {480, 480, 64}) ==
                qmg::SegmentVisibilityResult{qmg::SegmentVisibilityKind::visible, qmg::VisibilityPrecision::exact,
                                             std::nullopt},
            "unobstructed segment was not exactly visible");
    require(analyzer.classify_segment(layout, {32, 32, 64}, {700, 32, 64}).kind ==
                qmg::SegmentVisibilityKind::outside_free_space,
            "segment leaving room/passage free space was accepted");

    layout.solids.push_back(box_solid(9, {{240, 0, 0}, {272, 512, 320}}));
    layout.solids.push_back(box_solid(2, {{240, 0, 0}, {272, 512, 320}}));
    const qmg::SegmentVisibilityResult blocked = analyzer.classify_segment(layout, {32, 256, 64}, {480, 256, 64});
    require(blocked.kind == qmg::SegmentVisibilityKind::occluded &&
                blocked.precision == qmg::VisibilityPrecision::exact && blocked.blocker == qmg::SolidId{2},
            "exact box blockers did not use the stable smallest tied SolidId");

    qmg::Layout non_occluding = single_room();
    non_occluding.solids.push_back(
        box_solid(1, {{240, 0, 0}, {272, 512, 320}}, qmg::BrushPurpose::trigger_volume, qmg::MaterialRole::trigger));
    non_occluding.solids.push_back(box_solid(2, {{240, 0, 0}, {272, 512, 320}}, qmg::BrushPurpose::liquid,
                                             qmg::MaterialRole::liquid, qmg::BrushContents::water));
    require(analyzer.classify_segment(non_occluding, {32, 256, 64}, {480, 256, 64}).kind ==
                qmg::SegmentVisibilityKind::visible,
            "trigger or liquid contents incorrectly occluded visibility");

    qmg::Layout convex = single_room();
    convex.solids.push_back({qmg::SolidId{4}, qmg::BrushGeometry{shifted_tetrahedron({200, 200, 200})},
                             qmg::MaterialRole::arch, qmg::BrushPurpose::arch, qmg::BrushOwnership::detail,
                             qmg::BrushContents::solid, std::nullopt});
    const qmg::SegmentVisibilityResult conservative =
        analyzer.classify_segment(convex, {100, 290, 290}, {400, 290, 290});
    require(conservative.kind == qmg::SegmentVisibilityKind::occluded &&
                conservative.precision == qmg::VisibilityPrecision::conservative &&
                conservative.blocker == qmg::SolidId{4},
            "convex aggregate-bounds occlusion was not explicitly conservative");

    const std::vector<qmg::SegmentVisibilityQuery> queries = {
        {{100, 290, 290}, {400, 290, 290}},
        {{32, 32, 64}, {480, 32, 64}},
    };
    const std::vector<qmg::SegmentVisibilityResult> batch = analyzer.classify_segments(convex, queries);
    require(batch.size() == queries.size() && batch[0] == conservative &&
                batch[1] == analyzer.classify_segment(convex, queries[1].from, queries[1].to),
            "batched segment classification differs from individual queries");
    qmg::VisibilityOptions batch_cap;
    batch_cap.limits.max_segment_tests = 1;
    require_error([&] { static_cast<void>(analyzer.classify_segments(convex, queries, batch_cap)); },
                  "segment_test_limit", "batched segment-query cap was not enforced");
}

void conservative_evidence_cannot_establish_spawn_safety() {
    qmg::VisibilityOptions options;
    options.thresholds.minimum_spawn_separation = 0;
    options.thresholds.report_missing_cover = false;

    qmg::Layout threatened = single_room(qmg::Game::quake1, qmg::GameMode::single_player);
    threatened.spec.target_players = 1;
    threatened.placements = {
        point_placement(1, qmg::PlacementRole::player_start, {100, 290, 290}),
        point_placement(2, qmg::PlacementRole::monster, {400, 290, 290}),
    };
    threatened.solids.push_back({qmg::SolidId{4}, qmg::BrushGeometry{shifted_tetrahedron({200, 200, 200})},
                                 qmg::MaterialRole::arch, qmg::BrushPurpose::arch, qmg::BrushOwnership::detail,
                                 qmg::BrushContents::solid, std::nullopt});
    const qmg::VisibilityAnalysis threatened_analysis = qmg::VisibilityAnalyzer{}.analyze(threatened, options);
    require(threatened_analysis.placement_lines_of_sight.front().visibility.precision ==
                    qmg::VisibilityPrecision::conservative &&
                threatened_analysis.spawn_safety.front().visible_hostile_origins == 1 &&
                !threatened_analysis.spawn_safety.front().safe,
            "conservative convex bounds incorrectly established hostile occlusion and spawn safety");

    qmg::Layout no_cover = single_room(qmg::Game::quake1, qmg::GameMode::single_player);
    no_cover.spec.target_players = 1;
    no_cover.placements = {point_placement(1, qmg::PlacementRole::player_start, {64, 64, 32})};
    options.thresholds.report_missing_cover = true;
    const qmg::VisibilityAnalysis uncovered = qmg::VisibilityAnalyzer{}.analyze(no_cover, options);
    require(uncovered.spawn_safety.front().room_cover_opportunities == 0 && !uncovered.spawn_safety.front().safe,
            "required cover was reported as a finding but ignored by the spawn-safe verdict");

    no_cover.solids.push_back({qmg::SolidId{5}, qmg::BrushGeometry{shifted_tetrahedron({200, 200, 0})},
                               qmg::MaterialRole::arch, qmg::BrushPurpose::arch, qmg::BrushOwnership::detail,
                               qmg::BrushContents::solid, std::nullopt});
    const qmg::VisibilityAnalysis conservative_cover = qmg::VisibilityAnalyzer{}.analyze(no_cover, options);
    require(conservative_cover.cover_opportunities.empty() && !conservative_cover.spawn_safety.front().safe,
            "aggregate convex bounds were incorrectly accepted as proven cover");

    no_cover.solids.clear();
    no_cover.solids.push_back(box_solid(6, {{200, 200, 0}, {300, 300, 100}}, qmg::BrushPurpose::raised_platform,
                                        qmg::MaterialRole::platform));
    const qmg::VisibilityAnalysis exact_cover = qmg::VisibilityAnalyzer{}.analyze(no_cover, options);
    require(!exact_cover.cover_opportunities.empty() && exact_cover.spawn_safety.front().safe,
            "exact cover did not satisfy the configured spawn-safety requirement");
}

void free_space_union_queries() {
    qmg::Layout layout;
    layout.spec.game = qmg::Game::quake3;
    layout.spec.mode = qmg::GameMode::duel;
    layout.spec.target_players = 2;
    layout.rooms = {
        {qmg::RoomId{1}, "left", {{0, 0, 0}, {100, 100, 100}}, {}},
        {qmg::RoomId{2}, "right", {{300, 0, 0}, {400, 100, 100}}, {}},
    };
    layout.connections.push_back(
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 20, 100, {{{100, 40, 0}, {300, 60, 100}}}});
    const qmg::VisibilityAnalyzer analyzer;
    require(analyzer.classify_segment(layout, {50, 50, 50}, {350, 50, 50}).kind == qmg::SegmentVisibilityKind::visible,
            "segment covered by the complete free-space union was rejected");
    require(analyzer.classify_segment(layout, {50, 10, 50}, {350, 10, 50}).kind ==
                qmg::SegmentVisibilityKind::outside_free_space,
            "a rational interval gap in passage coverage was missed");

    layout.connections.front().passage_volumes = {
        {{100, 40, 0}, {220, 60, 100}},
        {{220, 40, 0}, {300, 60, 100}},
    };
    require(analyzer.classify_segment(layout, {50, 50, 50}, {350, 50, 50}).kind == qmg::SegmentVisibilityKind::visible,
            "adjacent free-space intervals were not merged exactly");
}

qmg::Layout metric_layout() {
    qmg::Layout layout;
    layout.spec.game = qmg::Game::quake3;
    layout.spec.mode = qmg::GameMode::deathmatch;
    layout.spec.target_players = 3;
    layout.rooms = {
        {qmg::RoomId{1}, "west", {{0, 0, 0}, {400, 400, 200}}, {}},
        {qmg::RoomId{2}, "middle", {{500, 0, 0}, {900, 400, 200}}, {}},
        {qmg::RoomId{3}, "east", {{1000, 0, 0}, {1400, 400, 200}}, {}},
    };
    layout.connections = {
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 128, 192, {{{400, 0, 0}, {500, 400, 200}}}},
        {qmg::ConnectionId{2}, qmg::RoomId{2}, qmg::RoomId{3}, 256, 192, {{{900, 0, 0}, {1000, 400, 200}}}},
    };
    layout.solids.push_back(box_solid(7, {{600, 150, 0}, {640, 250, 120}}, qmg::BrushPurpose::raised_platform,
                                      qmg::MaterialRole::platform));
    layout.placements = {
        point_placement(1, qmg::PlacementRole::deathmatch_spawn, {100, 100, 48}),
        point_placement(2, qmg::PlacementRole::deathmatch_spawn, {700, 100, 48}),
        point_placement(3, qmg::PlacementRole::deathmatch_spawn, {1300, 100, 48}),
    };
    return layout;
}

void aggregate_metrics() {
    const qmg::VisibilityAnalysis analysis = qmg::VisibilityAnalyzer{}.analyze(metric_layout());
    require(analysis.placement_lines_of_sight.size() == 3 &&
                std::all_of(analysis.placement_lines_of_sight.begin(), analysis.placement_lines_of_sight.end(),
                            [](const qmg::PlacementLineOfSight &line) {
                                return line.visibility.kind == qmg::SegmentVisibilityKind::visible;
                            }),
            "per-placement LOS matrix is incomplete or unexpectedly blocked");
    require(analysis.room_exposure.size() == 3 && analysis.room_exposure[1].room == qmg::RoomId{2},
            "room exposure metrics are not stable by RoomId");
    require(analysis.cover_opportunities.size() == 4 &&
                std::all_of(analysis.cover_opportunities.begin(), analysis.cover_opportunities.end(),
                            [](const qmg::CoverOpportunity &cover) {
                                return cover.room == qmg::RoomId{2} && cover.occluder == qmg::SolidId{7} &&
                                       cover.precision == qmg::VisibilityPrecision::exact;
                            }),
            "deterministic axial cover opportunities changed");

    require(analysis.room_topology.size() == 3 && !analysis.room_topology[0].articulation &&
                analysis.room_topology[1].articulation && !analysis.room_topology[2].articulation,
            "linear room graph articulation analysis is incorrect");
    require(analysis.connection_readability.size() == 2 && analysis.connection_readability[0].bridge &&
                analysis.connection_readability[0].narrow && analysis.connection_readability[0].chokepoint &&
                analysis.connection_readability[0].centerline_visibility.kind == qmg::SegmentVisibilityKind::occluded &&
                analysis.connection_readability[1].bridge && !analysis.connection_readability[1].narrow &&
                analysis.connection_readability[1].chokepoint &&
                analysis.connection_readability[1].centerline_visibility.kind == qmg::SegmentVisibilityKind::visible,
            "bridge, width, chokepoint, or route-readability metrics changed");

    const auto middle_crossfire = std::find_if(analysis.crossfire_angles.begin(), analysis.crossfire_angles.end(),
                                               [](const qmg::CrossfireAngleMetric &metric) {
                                                   return metric.focus == qmg::PlacementId{2} &&
                                                          metric.first_observer == qmg::PlacementId{1} &&
                                                          metric.second_observer == qmg::PlacementId{3};
                                               });
    require(middle_crossfire != analysis.crossfire_angles.end() && middle_crossfire->angle_millidegrees == 180000U &&
                middle_crossfire->qualifies,
            "integer crossfire angle for opposing sight lines is not 180 degrees");
    require(analysis.spawn_safety.size() == 3 &&
                std::all_of(analysis.spawn_safety.begin(), analysis.spawn_safety.end(),
                            [](const qmg::SpawnSafetyMetric &spawn) { return !spawn.safe; }) &&
                analysis.spawn_safety[1].room_cover_opportunities == 4 &&
                analysis.spawn_safety[1].qualifying_crossfires == 1,
            "spawn safety did not combine LOS, cover, and crossfire metrics");
    require(analysis.summary.connected_components == 1 && analysis.summary.articulation_rooms == 1 &&
                analysis.summary.chokepoint_connections == 2 && analysis.summary.readable_connections == 1 &&
                analysis.summary.evaluated_spawns == 3 && analysis.summary.safe_spawns == 0 &&
                analysis.summary.segment_tests > analysis.placement_lines_of_sight.size(),
            "visibility summary counts changed");
    require(!analysis.findings.empty() && analysis.findings.front().id == qmg::VisibilityFindingId{1} &&
                analysis.findings.back().id.value == analysis.findings.size(),
            "spawn-safety finding identifiers are not dense and stable");
}

void angle_and_cycle_negative_cases() {
    qmg::Layout angle_layout = single_room();
    angle_layout.spec.target_players = 4;
    angle_layout.placements = {
        point_placement(1, qmg::PlacementRole::deathmatch_spawn, {200, 200, 32}),
        point_placement(2, qmg::PlacementRole::deathmatch_spawn, {300, 200, 32}),
        point_placement(3, qmg::PlacementRole::deathmatch_spawn, {300, 300, 32}),
        point_placement(4, qmg::PlacementRole::deathmatch_spawn, {200, 300, 32}),
    };
    qmg::VisibilityOptions angle_options;
    angle_options.thresholds.minimum_crossfire_angle_millidegrees = 90000;
    const qmg::VisibilityAnalysis angle_analysis = qmg::VisibilityAnalyzer{}.analyze(angle_layout, angle_options);
    const auto find_angle = [&](const qmg::PlacementId first, const qmg::PlacementId second) {
        return std::find_if(angle_analysis.crossfire_angles.begin(), angle_analysis.crossfire_angles.end(),
                            [&](const qmg::CrossfireAngleMetric &metric) {
                                return metric.focus == qmg::PlacementId{1} && metric.first_observer == first &&
                                       metric.second_observer == second;
                            });
    };
    const auto diagonal = find_angle(qmg::PlacementId{2}, qmg::PlacementId{3});
    const auto perpendicular = find_angle(qmg::PlacementId{2}, qmg::PlacementId{4});
    require(diagonal != angle_analysis.crossfire_angles.end() && diagonal->angle_millidegrees == 45000U &&
                !diagonal->qualifies && perpendicular != angle_analysis.crossfire_angles.end() &&
                perpendicular->angle_millidegrees == 90000U && perpendicular->qualifies,
            "integer CORDIC crossfire angles or threshold comparison changed");

    qmg::Layout cycle = metric_layout();
    cycle.connections.push_back(
        {qmg::ConnectionId{3}, qmg::RoomId{1}, qmg::RoomId{3}, 256, 192, {{{400, 300, 0}, {1000, 400, 200}}}});
    const qmg::VisibilityAnalysis cycle_analysis = qmg::VisibilityAnalyzer{}.analyze(cycle);
    require(std::none_of(cycle_analysis.room_topology.begin(), cycle_analysis.room_topology.end(),
                         [](const qmg::RoomTopologyMetric &room) { return room.articulation; }) &&
                std::none_of(cycle_analysis.connection_readability.begin(), cycle_analysis.connection_readability.end(),
                             [](const qmg::ConnectionReadabilityMetric &connection) { return connection.bridge; }),
            "cycle graph incorrectly reported an articulation room or bridge connection");
}

void team_and_mode_semantics() {
    qmg::Layout q3_ctf = single_room(qmg::Game::quake3, qmg::GameMode::capture_the_flag);
    q3_ctf.spec.target_players = 6;
    q3_ctf.placements = {
        point_placement(1, qmg::PlacementRole::team_initial_spawn, {64, 64, 32}, qmg::Team::red),
        point_placement(2, qmg::PlacementRole::team_initial_spawn, {448, 448, 32}, qmg::Team::blue),
        point_placement(3, qmg::PlacementRole::team_respawn, {64, 448, 32}, qmg::Team::red),
        point_placement(4, qmg::PlacementRole::team_respawn, {448, 64, 32}, qmg::Team::blue),
        point_placement(5, qmg::PlacementRole::deathmatch_spawn, {160, 256, 32}),
        point_placement(6, qmg::PlacementRole::deathmatch_spawn, {352, 256, 32}),
    };
    const qmg::VisibilityAnalysis ctf_analysis = qmg::VisibilityAnalyzer{}.analyze(q3_ctf);
    const auto red =
        std::find_if(ctf_analysis.spawn_safety.begin(), ctf_analysis.spawn_safety.end(),
                     [](const qmg::SpawnSafetyMetric &spawn) { return spawn.spawn == qmg::PlacementId{1}; });
    require(red != ctf_analysis.spawn_safety.end() && red->hostile_origins == 4,
            "Q3 CTF hostility did not exclude a same-team initial/respawn origin");
    require(std::none_of(ctf_analysis.findings.begin(), ctf_analysis.findings.end(),
                         [](const qmg::SpawnSafetyFinding &finding) {
                             return finding.code == qmg::SpawnSafetyCode::missing_required_spawn;
                         }),
            "complete Q3 CTF typed spawn roles were reported missing");

    qmg::Layout q1_ctf = single_room(qmg::Game::quake1, qmg::GameMode::capture_the_flag);
    q1_ctf.placements = {
        point_placement(1, qmg::PlacementRole::deathmatch_spawn, {64, 64, 32}),
        point_placement(2, qmg::PlacementRole::deathmatch_spawn, {448, 448, 32}),
        point_placement(3, qmg::PlacementRole::flag, {64, 448, 32}, qmg::Team::red),
        point_placement(4, qmg::PlacementRole::flag, {448, 64, 32}, qmg::Team::blue),
    };
    require(qmg::VisibilityAnalyzer{}.analyze(q1_ctf).summary.evaluated_spawns == 2,
            "Q1 CTF mod-profile semantics did not use generic deathmatch spawns");

    qmg::Layout bad_team = single_room(qmg::Game::quake3, qmg::GameMode::capture_the_flag);
    bad_team.placements.push_back(point_placement(1, qmg::PlacementRole::flag, {64, 64, 32}));
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(bad_team)); }, "invalid_team_semantics",
                  "team objective without a team was accepted");

    qmg::Layout q2_bad_respawn = single_room(qmg::Game::quake2, qmg::GameMode::capture_the_flag);
    q2_bad_respawn.placements.push_back(
        point_placement(1, qmg::PlacementRole::team_respawn, {64, 64, 32}, qmg::Team::red));
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(q2_bad_respawn)); },
                  "invalid_mode_semantics", "Q2 accepted the Q3-only typed team-respawn role");

    qmg::Layout q3_bad_start = single_room(qmg::Game::quake3, qmg::GameMode::single_player);
    q3_bad_start.placements.push_back(point_placement(1, qmg::PlacementRole::player_start, {64, 64, 32}));
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(q3_bad_start)); }, "invalid_mode_semantics",
                  "Q3 tournament single-player accepted a native campaign start");

    qmg::Layout q2_bad_monster = single_room(qmg::Game::quake2, qmg::GameMode::deathmatch);
    q2_bad_monster.placements.push_back(point_placement(1, qmg::PlacementRole::monster, {64, 64, 32}));
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(q2_bad_monster)); },
                  "invalid_mode_semantics", "Q2 deathmatch accepted a single-player monster threat");
}

void negative_findings_and_thresholds() {
    qmg::Layout layout = single_room(qmg::Game::quake1, qmg::GameMode::single_player);
    layout.spec.target_players = 1;
    layout.placements = {
        point_placement(1, qmg::PlacementRole::player_start, {64, 64, 32}),
        point_placement(2, qmg::PlacementRole::monster, {128, 64, 32}),
    };
    qmg::VisibilityOptions options;
    options.thresholds.minimum_spawn_separation = 128;
    options.thresholds.report_missing_cover = false;
    const qmg::VisibilityAnalysis exposed = qmg::VisibilityAnalyzer{}.analyze(layout, options);
    require(exposed.spawn_safety.size() == 1 && exposed.spawn_safety.front().visible_hostile_origins == 1 &&
                exposed.spawn_safety.front().close_hostile_origins == 1 && !exposed.spawn_safety.front().safe,
            "native single-player spawn threat did not produce LOS and proximity findings");
    require(std::count_if(exposed.findings.begin(), exposed.findings.end(),
                          [](const qmg::SpawnSafetyFinding &finding) {
                              return finding.code == qmg::SpawnSafetyCode::hostile_line_of_sight;
                          }) == 1 &&
                std::count_if(exposed.findings.begin(), exposed.findings.end(),
                              [](const qmg::SpawnSafetyFinding &finding) {
                                  return finding.code == qmg::SpawnSafetyCode::insufficient_separation;
                              }) == 1,
            "spawn finding codes do not distinguish direct exposure from proximity");

    layout.solids.push_back(box_solid(1, {{88, 0, 0}, {104, 160, 128}}));
    const qmg::VisibilityAnalysis sheltered = qmg::VisibilityAnalyzer{}.analyze(layout, options);
    require(sheltered.spawn_safety.front().visible_hostile_origins == 0 &&
                sheltered.spawn_safety.front().close_hostile_origins == 1,
            "solid cover did not remove exact hostile LOS while retaining distance risk");
}

void caps_and_invalid_inputs() {
    const qmg::Layout layout = metric_layout();
    qmg::VisibilityOptions room_cap;
    room_cap.limits.max_rooms = 2;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, room_cap)); }, "room_limit",
                  "room cap was not enforced");

    qmg::VisibilityOptions segment_cap;
    segment_cap.limits.max_segment_tests = 1;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, segment_cap)); },
                  "segment_test_limit", "segment-test cap was not enforced");

    qmg::VisibilityOptions crossfire_cap;
    crossfire_cap.limits.max_crossfire_records = 1;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, crossfire_cap)); },
                  "crossfire_limit", "crossfire combination cap was not enforced");

    qmg::VisibilityOptions cover_cap;
    cover_cap.limits.max_cover_opportunities = 1;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, cover_cap)); },
                  "cover_opportunity_limit", "cover-opportunity cap was not enforced");

    qmg::VisibilityOptions finding_cap;
    finding_cap.limits.max_findings = 1;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, finding_cap)); }, "finding_limit",
                  "spawn-finding cap was not enforced");

    qmg::VisibilityOptions invalid_limits;
    invalid_limits.limits.max_abs_coordinate = 1'000'001;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, invalid_limits)); },
                  "invalid_limits", "portable coordinate hard cap was not enforced");

    qmg::VisibilityOptions coordinate_limit;
    coordinate_limit.limits.max_abs_coordinate = 1000;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, coordinate_limit)); },
                  "coordinate_limit", "configured coordinate bound was not enforced");

    qmg::VisibilityOptions invalid_threshold;
    invalid_threshold.thresholds.minimum_crossfire_angle_millidegrees = 180001;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(layout, invalid_threshold)); },
                  "invalid_thresholds", "crossfire-angle threshold above 180 degrees was accepted");

    qmg::Layout duplicate = layout;
    duplicate.placements.back().id = duplicate.placements.front().id;
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(duplicate)); }, "invalid_placement_ids",
                  "duplicate PlacementIds were accepted");

    qmg::Layout outside = layout;
    outside.placements.front().origin = {450, 450, 48};
    require_error([&] { static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(outside)); },
                  "placement_outside_free_space", "placement outside the free-space union was accepted");
}

void vector_order_determinism() {
    qmg::Layout ordered = metric_layout();
    ordered.solids.push_back(box_solid(11, {{1100, 300, 0}, {1150, 350, 64}}, qmg::BrushPurpose::liquid,
                                       qmg::MaterialRole::liquid, qmg::BrushContents::slime));
    qmg::Layout shuffled = ordered;
    std::reverse(shuffled.rooms.begin(), shuffled.rooms.end());
    std::reverse(shuffled.connections.begin(), shuffled.connections.end());
    for (qmg::Connection &connection : shuffled.connections)
        std::reverse(connection.passage_volumes.begin(), connection.passage_volumes.end());
    std::reverse(shuffled.solids.begin(), shuffled.solids.end());
    std::reverse(shuffled.placements.begin(), shuffled.placements.end());

    const qmg::VisibilityAnalysis first = qmg::VisibilityAnalyzer{}.analyze(ordered);
    const qmg::VisibilityAnalysis second = qmg::VisibilityAnalyzer{}.analyze(shuffled);
    require(first == second, "visibility metrics or finding IDs depend on Layout vector order");
}

void quake2_spawn_eye_sampling() {
    auto layout = single_room(qmg::Game::quake2);
    layout.placements = {point_placement(1, qmg::PlacementRole::deathmatch_spawn, {64, 256, 24}),
                         point_placement(2, qmg::PlacementRole::deathmatch_spawn, {448, 256, 24}),
                         point_placement(3, qmg::PlacementRole::weapon, {64, 128, 24})};
    layout.solids.push_back(box_solid(1, {{240, 0, 0}, {272, 512, 32}}));
    qmg::VisibilityOptions options;
    options.thresholds.minimum_spawn_separation = 0;
    options.thresholds.report_missing_cover = false;
    const qmg::VisibilityAnalyzer analyzer;
    require(analyzer.classify_segment(layout, {64, 256, 24}, {448, 256, 24}).kind ==
                qmg::SegmentVisibilityKind::occluded,
            "control: low barrier must block origin-height ray");
    const auto report = analyzer.analyze(layout, options);
    const auto &pair = report.placement_lines_of_sight[0];
    require(report.player_spawn_eye_offset_z == 22 && pair.first_sample.z == 46 && pair.second_sample.z == 46,
            "Q2 spawn samples do not match standing eye height");
    require(pair.visibility.kind == qmg::SegmentVisibilityKind::visible && !report.spawn_safety[0].safe &&
                !report.spawn_safety[1].safe,
            "origin-height cover incorrectly hid an exposed Q2 head from the safety review");
    require(report.placement_lines_of_sight[1].second_sample.z == 24,
            "pickup targets must retain their actual entity sample");
    auto ceiling = layout;
    ceiling.solids.push_back(box_solid(2, {{32, 224, 40}, {96, 288, 52}}));
    require_error([&] { static_cast<void>(analyzer.analyze(ceiling, options)); }, "spawn_eye_in_solid",
                  "embedded eye was accepted as a sheltered spawn");
    auto outside = layout;
    outside.placements[0].origin.z = 310;
    require_error([&] { static_cast<void>(analyzer.analyze(outside, options)); }, "sample_outside_free_space",
                  "eye outside room coverage was accepted");
    layout.spec.game = qmg::Game::quake3;
    require(analyzer.analyze(layout, options).player_spawn_eye_offset_z == 0,
            "Q2 policy must not silently claim a verified Q3 viewpoint");
}

} // namespace

int main() {
    try {
        public_segment_queries();
        conservative_evidence_cannot_establish_spawn_safety();
        free_space_union_queries();
        aggregate_metrics();
        angle_and_cycle_negative_cases();
        team_and_mode_semantics();
        negative_findings_and_thresholds();
        caps_and_invalid_inputs();
        vector_order_determinism();
        quake2_spawn_eye_sampling();
        std::cout << "visibility tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "visibility test failure: " << error.what() << '\n';
        return 1;
    }
}
