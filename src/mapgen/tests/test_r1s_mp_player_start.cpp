// Change r1s-mp-player-start (plan 9.3 R1-s item 2; switch r1s.mp_player_start).
//
// A Quake 1 or Quake 2 server started with `map <name>` and deathmatch 0 puts every client at an info_player_start
// and aborts without one (Quake 1 "PutClientInServer: no info_player_start on level", 1996 id1 progs
// SelectSpawnPoint statements 6064-6081, quake-rerelease-qc quakec/client.qc:672-686; Quake 2 "Couldn't find spawn
// point", game/p_client.c:885-908). With the switch on, every Quake 1 and Quake 2 deathmatch, duel and CTF layout
// gets exactly one start at a supported, clear and (where possible) lit station, appended after every other placement.
// Deathmatch spawn selection never reads it (src/entities.cpp), so the analyses of match play leave it out.
// Nothing else changes: the other placements, the geometry, the chosen candidate and every single-player and
// Quake 3 map stay as they were.
#include "qmg/entities.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/map_writer.hpp"
#include "qmg/pipeline.hpp"
#include "qmg/report.hpp"
#include "qmg/switches.hpp"
#include "qmg/validator.hpp"
#include "qmg/visibility.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The registered default: off when introduced (change r1s-mp-player-start), on since change
// r1s-mp-player-start-flip.
constexpr bool kExpectedDefaultOn = true;

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::StageSwitches switches(const bool on) {
    const std::vector<std::string> assignment{std::string("r1s.mp_player_start=") + (on ? "on" : "off")};
    return qmg::parse_stage_switch_assignments(assignment);
}

[[nodiscard]] qmg::PipelineResult run(const std::string_view prompt, const bool on,
                                      const qmg::PromptOverrides &overrides = {}) {
    qmg::PipelineOptions options;
    options.switches = switches(on);
    return qmg::GenerationPipeline{}.run(prompt, overrides, options);
}

[[nodiscard]] std::vector<const qmg::Placement *> starts(const qmg::Layout &layout) {
    std::vector<const qmg::Placement *> found;
    for (const qmg::Placement &placement : layout.placements) {
        if (placement.role == qmg::PlacementRole::player_start)
            found.push_back(&placement);
    }
    return found;
}

[[nodiscard]] bool overlaps(const qmg::GridBounds &a, const qmg::GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

[[nodiscard]] bool has_tag(const qmg::Room &room, const std::string_view tag) {
    return std::find(room.tags.begin(), room.tags.end(), tag) != room.tags.end();
}

[[nodiscard]] std::string origin_text(const qmg::GridVec3 origin) {
    return std::to_string(origin.x) + " " + std::to_string(origin.y) + " " + std::to_string(origin.z);
}

[[nodiscard]] bool same_rooms(const std::vector<qmg::Room> &left, const std::vector<qmg::Room> &right) {
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](const qmg::Room &a, const qmg::Room &b) {
        return a.id == b.id && a.name == b.name && a.bounds == b.bounds && a.tags == b.tags;
    });
}

[[nodiscard]] bool same_connections(const std::vector<qmg::Connection> &left,
                                    const std::vector<qmg::Connection> &right) {
    return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                      [](const qmg::Connection &a, const qmg::Connection &b) {
                          return a.id == b.id && a.from_room == b.from_room && a.to_room == b.to_room &&
                                 a.width == b.width && a.height == b.height && a.passage_volumes == b.passage_volumes;
                      });
}

[[nodiscard]] bool has_issue(const qmg::ValidationResult &result, const std::string_view code) {
    return std::any_of(result.issues.begin(), result.issues.end(),
                       [code](const qmg::Issue &issue) { return issue.code == code; });
}

void registry_row() {
    const auto registry = qmg::stage_switch_registry();
    const auto row = std::find_if(registry.begin(), registry.end(), [](const qmg::StageSwitchDefinition &definition) {
        return definition.name == "r1s.mp_player_start";
    });
    require(row != registry.end(), "r1s.mp_player_start is registered");
    require(row->id == qmg::StageSwitch::r1s_mp_player_start, "the typed handle names the row");
    require(row->default_on == kExpectedDefaultOn, "the registered default is the expected one");
    require(row->sub_lane == qmg::SwitchSubLane::mp, "the cause touches multiplayer cases only: sub-lane mp");
    require(row->introduced_by == "r1s-mp-player-start", "the row records its change id");
    require(qmg::StageSwitches{}.on(qmg::StageSwitch::r1s_mp_player_start) == kExpectedDefaultOn,
            "a default set holds the registered default");
}

// The start contract for one Quake 1 or Quake 2 multiplayer prompt, and the one-cause contract against the
// switched-off run.
void multiplayer_start(const std::string_view prompt, const qmg::PromptOverrides &overrides = {}) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false, overrides);
    const qmg::PipelineResult on = run(prompt, true, overrides);
    require(off.layout.spec.game != qmg::Game::quake3 && off.layout.spec.mode != qmg::GameMode::single_player,
            label + ": a Quake 1 or Quake 2 multiplayer prompt");
    require(on.layout.spec.game == off.layout.spec.game && on.layout.spec.mode == off.layout.spec.mode,
            label + ": the switch does not change the resolved game or mode");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(off.validation.ok() && on.validation.ok(), label + ": both layouts validate");
    require(starts(off.layout).empty(), label + ": the switched-off layout has no start");
    const std::vector<const qmg::Placement *> found = starts(on.layout);
    require(found.size() == 1, label + ": exactly one start");
    const qmg::Placement &start = *found.front();
    require(qmg::is_multiplayer_map_start(on.layout, start), label + ": it is the multiplayer map start");

    // One cause: the start is the last placement and everything else is unchanged, deathmatch spawns included.
    require(&start == &on.layout.placements.back(), label + ": the start is appended last");
    std::vector<qmg::Placement> others(on.layout.placements.begin(), on.layout.placements.end() - 1);
    require(others == off.layout.placements, label + ": every other placement is unchanged");
    require(same_rooms(on.layout.rooms, off.layout.rooms) &&
                same_connections(on.layout.connections, off.layout.connections) &&
                on.layout.solids == off.layout.solids && on.layout.candidate_seed == off.layout.candidate_seed &&
                on.layout.rank_score == off.layout.rank_score,
            label + ": rooms, connections, solids, the chosen candidate and its rank score are unchanged");
    require(on.repairs.size() == off.repairs.size(), label + ": the same repair passes");

    // Fields.
    require(!start.volume && !start.team && start.variant == 0 && !start.pitch_degrees && !start.light_intensity &&
                !start.light_color && !start.destination_map && !start.link,
            label + ": a bare point entity");
    require(start.angle_degrees && *start.angle_degrees >= 0 && *start.angle_degrees < 360,
            label + ": yaw from 0 through 359");

    // In the start room, standing on its floor: feet on the floor, origin 24 units above it.
    const qmg::Room *room = nullptr;
    for (const qmg::Room &candidate : on.layout.rooms) {
        if (candidate.bounds.contains(start.origin))
            room = &candidate;
    }
    require(room != nullptr, label + ": the start is inside a room");
    require(has_tag(*room, "start"), label + ": the start is in the start room");
    require(start.origin.z == room->bounds.min.z + 24, label + ": the origin stands 24 units above the room floor");

    // The standing box (32 x 32 x 56) is inside the room, overlaps no solid and no entity volume, and keeps 64
    // units (on x or y) from every other point entity.
    const qmg::GridBounds hull{{start.origin.x - 16, start.origin.y - 16, start.origin.z - 24},
                               {start.origin.x + 16, start.origin.y + 16, start.origin.z + 32}};
    require(hull.min.x >= room->bounds.min.x && hull.max.x <= room->bounds.max.x && hull.min.y >= room->bounds.min.y &&
                hull.max.y <= room->bounds.max.y && hull.min.z >= room->bounds.min.z &&
                hull.max.z <= room->bounds.max.z,
            label + ": the standing box stays inside the room");
    for (const qmg::LayoutSolid &solid : on.layout.solids)
        require(!overlaps(hull, qmg::checked_brush_bounds(solid.geometry)), label + ": no solid overlaps the start");
    for (const qmg::Placement &placement : off.layout.placements) {
        if (placement.volume) {
            require(!overlaps(hull, *placement.volume), label + ": no entity volume overlaps the start");
            continue;
        }
        require(std::abs(static_cast<std::int64_t>(placement.origin.x) - start.origin.x) >= 64 ||
                    std::abs(static_cast<std::int64_t>(placement.origin.y) - start.origin.y) >= 64,
                label + ": at least 64 units from every other point entity");
    }

    // Facing the room centre, to the nearest degree.
    const qmg::GridVec3 center = room->bounds.center();
    const double dx = static_cast<double>(center.x) - start.origin.x;
    const double dy = static_cast<double>(center.y) - start.origin.y;
    if (dx != 0.0 || dy != 0.0) {
        const double yaw = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
        require(std::abs(std::remainder(yaw - *start.angle_degrees, 360.0)) <= 0.5 + 1e-9,
                label + ": the yaw faces the room centre");
    }

    // Supported: the player navigation graph binds it to a walkable node, and no navigation finding names it.
    require(on.player_navigation && off.player_navigation, label + ": player navigation ran");
    const qmg::NavigationPlacementBinding *binding = on.player_navigation->find_binding(start.id);
    require(binding != nullptr && binding->node.has_value(), label + ": the start is bound to a navigation node");
    std::vector<qmg::NavigationPlacementBinding> bindings = on.player_navigation->placement_bindings;
    bindings.erase(
        std::remove_if(bindings.begin(), bindings.end(),
                       [&start](const qmg::NavigationPlacementBinding &entry) { return entry.placement == start.id; }),
        bindings.end());
    require(bindings == off.player_navigation->placement_bindings, label + ": every other binding is unchanged");
    require(on.player_navigation->findings == off.player_navigation->findings,
            label + ": navigation findings are unchanged");

    // Match analyses leave the start out: spawn safety, room exposure and spawn timing are unchanged.
    require(on.visibility && off.visibility, label + ": visibility ran");
    require(on.visibility->spawn_safety == off.visibility->spawn_safety &&
                on.visibility->findings == off.visibility->findings &&
                on.visibility->room_exposure == off.visibility->room_exposure &&
                on.visibility->crossfire_angles == off.visibility->crossfire_angles &&
                on.visibility->summary.evaluated_spawns == off.visibility->summary.evaluated_spawns,
            label + ": visibility spawn safety, findings, room exposure and crossfire are unchanged");
    require(on.quality && off.quality, label + ": quality ran");
    require(on.quality->placement_timing == off.quality->placement_timing,
            label + ": spawn timing is measured from the match spawns only");
    for (const qmg::PlacementTimingMetric &metric : on.quality->placement_timing)
        require(metric.spawn != start.id, label + ": no timing metric starts at the map start");

    // Lit: the start's lighting sample is readable, so the lighting findings are unchanged.
    require(on.lighting && off.lighting, label + ": lighting ran");
    require(on.lighting->findings == off.lighting->findings, label + ": the start adds no lighting finding");

    // The map gains exactly the info_player_start entity, as its last entity.
    const std::string off_map = qmg::MapWriter{}.render(off.map_build->document);
    const std::string on_map = qmg::MapWriter{}.render(on.map_build->document);
    const std::string block = "{\n\"classname\" \"info_player_start\"\n\"angle\" \"" +
                              std::to_string(*start.angle_degrees) + "\"\n\"origin\" \"" + origin_text(start.origin) +
                              "\"\n}\n";
    require(on_map.size() > block.size() && on_map.compare(on_map.size() - block.size(), block.size(), block) == 0,
            label + ": the map ends with the info_player_start entity");
    require(on_map.find("info_player_start") == on_map.size() - block.size() + 15, label + ": only one");
    require(on_map.substr(0, on_map.size() - block.size()) == off_map, label + ": the map differs only by it");
    require(on.map_build->stats.entity_count == off.map_build->stats.entity_count + 1, label + ": one more entity");

    // The report names the switch only where it is away from its default; the run is deterministic.
    const qmg::PipelineResult &switched = kExpectedDefaultOn ? off : on;
    const qmg::PipelineResult &defaulted = kExpectedDefaultOn ? on : off;
    const std::string value = kExpectedDefaultOn ? "off" : "on";
    require(qmg::render_report(switched).find("\"switches\": {\"r1s.mp_player_start\": \"" + value + "\"}") !=
                std::string::npos,
            label + ": the non-default run's report lists the switch");
    require(qmg::render_report(defaulted).find("\"switches\"") == std::string::npos,
            label + ": the default run's report has no switches member");
    qmg::PipelineOptions defaults;
    require(qmg::MapWriter{}.render(qmg::GenerationPipeline{}.run(prompt, overrides, defaults).map_build->document) ==
                (kExpectedDefaultOn ? on_map : off_map),
            label + ": a run with no --switch is the default side");
    const qmg::PipelineResult again = run(prompt, true, overrides);
    require(qmg::MapWriter{}.render(again.map_build->document) == on_map &&
                qmg::canonical_layout_sha256(again.layout) == qmg::canonical_layout_sha256(on.layout),
            label + ": deterministic");
}

// Single-player and Quake 3 maps are outside the cause: the switch changes no placement and no map byte.
void outside_scope_unchanged(const std::string_view prompt) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false);
    const qmg::PipelineResult on = run(prompt, true);
    require(on.layout.spec.game == qmg::Game::quake3 || on.layout.spec.mode == qmg::GameMode::single_player,
            label + ": a single-player or Quake 3 prompt");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(on.layout.placements == off.layout.placements, label + ": placements unchanged");
    require(qmg::MapWriter{}.render(on.map_build->document) == qmg::MapWriter{}.render(off.map_build->document),
            label + ": map bytes unchanged");
}

// Many spawns in few rooms: the start still finds a clear station and the layout still validates.
void dense_arenas() {
    for (const qmg::Game game : {qmg::Game::quake1, qmg::Game::quake2}) {
        for (const qmg::GameMode mode :
             {qmg::GameMode::deathmatch, qmg::GameMode::duel, qmg::GameMode::capture_the_flag}) {
            for (std::uint64_t seed = 1; seed <= 4; ++seed) {
                qmg::PromptOverrides overrides;
                overrides.game = game;
                overrides.mode = mode;
                overrides.room_count = 2;
                overrides.target_players = 32;
                overrides.seed = seed;
                const qmg::PipelineResult result = run("vertical dense arena", true, overrides);
                const std::string label = "dense arena game " + std::to_string(static_cast<int>(game)) + " mode " +
                                          std::to_string(static_cast<int>(mode)) + " seed " + std::to_string(seed);
                require(starts(result.layout).size() == 1, label + ": exactly one start");
                require(result.validation.ok() && result.map_build, label + ": validates and builds");
            }
        }
    }
}

[[nodiscard]] qmg::Layout one_room(const qmg::Game game, const qmg::GameMode mode, const bool on) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.mode = mode;
    layout.spec.target_players = 2;
    layout.spec.title = "player start fixture";
    layout.spec.theme = "industrial";
    layout.spec.switches = switches(on);
    layout.candidate_seed = 0x5EEDU;
    layout.rooms.push_back({qmg::RoomId{1}, "room", {{0, 0, 0}, {512, 512, 256}}, {"start"}});
    return layout;
}

[[nodiscard]] qmg::Placement point(const std::uint32_t id, const qmg::PlacementRole role, const qmg::GridVec3 origin) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    return placement;
}

void require_throws_logic(qmg::Layout &layout) {
    bool threw = false;
    try {
        static_cast<void>(qmg::add_multiplayer_player_start(layout));
    } catch (const std::logic_error &) {
        threw = true;
    }
    require(threw, "a second start is refused");
}

// The station rule and the role's contract outside the generator.
void station_rule_and_contract() {
    // Off: nothing happens.
    qmg::Layout off = one_room(qmg::Game::quake2, qmg::GameMode::deathmatch, false);
    off.placements.push_back(point(1, qmg::PlacementRole::deathmatch_spawn, {256, 256, 24}));
    const std::vector<qmg::Placement> before = off.placements;
    require(!qmg::add_multiplayer_player_start(off) && off.placements == before, "the switched-off path adds nothing");

    // Single-player and Quake 3 layouts are outside the cause even with the switch on.
    for (const auto &[game, mode] : {std::pair{qmg::Game::quake1, qmg::GameMode::single_player},
                                     std::pair{qmg::Game::quake2, qmg::GameMode::single_player},
                                     std::pair{qmg::Game::quake3, qmg::GameMode::deathmatch},
                                     std::pair{qmg::Game::quake3, qmg::GameMode::capture_the_flag}}) {
        qmg::Layout outside = one_room(game, mode, true);
        require(!qmg::add_multiplayer_player_start(outside) && outside.placements.empty(),
                "single-player and Quake 3 layouts get no multiplayer start");
    }

    // On: the lit, clear lattice station nearest the room centre. The room light hangs over the centre, so the
    // four stations 64 units from it tie; the first in x-major order, (192, 256), has a pillar between it and the
    // light, so the next one, (256, 192), wins. A solid block in a corner and the other entities are kept clear of.
    qmg::Layout level = one_room(qmg::Game::quake1, qmg::GameMode::deathmatch, true);
    level.placements.push_back(point(1, qmg::PlacementRole::deathmatch_spawn, {64, 64, 32}));
    level.placements.push_back(point(2, qmg::PlacementRole::deathmatch_spawn, {448, 448, 32}));
    level.placements.push_back(point(3, qmg::PlacementRole::health, {64, 448, 24}));
    qmg::Placement lamp = point(4, qmg::PlacementRole::light, {256, 256, 192});
    lamp.light_intensity = 300;
    lamp.light_color = qmg::RgbColor{1000, 1000, 1000};
    level.placements.push_back(lamp);
    level.solids.push_back({qmg::SolidId{1}, qmg::GridBounds{{400, 0, 0}, {512, 112, 64}}, qmg::MaterialRole::wall,
                            qmg::BrushPurpose::structural, qmg::BrushOwnership::structural, qmg::BrushContents::solid,
                            std::nullopt});
    level.solids.push_back({qmg::SolidId{2}, qmg::GridBounds{{208, 240, 0}, {224, 272, 256}}, qmg::MaterialRole::wall,
                            qmg::BrushPurpose::structural, qmg::BrushOwnership::structural, qmg::BrushContents::solid,
                            std::nullopt});
    require(qmg::add_multiplayer_player_start(level), "a Quake 1 deathmatch layout gets a start");
    require(level.placements.size() == 5, "exactly one placement is added");
    const qmg::Placement &start = level.placements.back();
    require(start.role == qmg::PlacementRole::player_start && start.id == qmg::PlacementId{5},
            "the start takes the next placement id");
    require(start.origin == qmg::GridVec3{256, 192, 24}, "the lit station nearest the centre, feet on the floor");
    require(start.angle_degrees && *start.angle_degrees == 90, "facing the room centre");
    for (std::size_t index = 0; index + 1 < level.placements.size(); ++index) {
        const qmg::Placement &other = level.placements[index];
        require(std::abs(other.origin.x - start.origin.x) >= 64 || std::abs(other.origin.y - start.origin.y) >= 64,
                "64 units from every other entity");
    }

    // With no light in view anywhere, the clear station nearest the centre is still taken (a dark start is better
    // than an abort).
    qmg::Layout dark = one_room(qmg::Game::quake2, qmg::GameMode::duel, true);
    dark.placements.push_back(point(1, qmg::PlacementRole::deathmatch_spawn, {64, 64, 24}));
    require(qmg::add_multiplayer_player_start(dark) && dark.placements.back().origin == qmg::GridVec3{256, 256, 24},
            "an unlit room still gets its start at the clear station nearest the centre");
    require(qmg::is_multiplayer_map_start(level, start), "the start is the multiplayer map start");
    require_throws_logic(level);

    // A room with no clear station anywhere fails closed instead of silently leaving the map without a start.
    qmg::Layout blocked = one_room(qmg::Game::quake2, qmg::GameMode::capture_the_flag, true);
    blocked.solids.push_back({qmg::SolidId{1}, qmg::GridBounds{{0, 0, 0}, {512, 512, 64}}, qmg::MaterialRole::wall,
                              qmg::BrushPurpose::structural, qmg::BrushOwnership::structural, qmg::BrushContents::solid,
                              std::nullopt});
    bool threw = false;
    try {
        static_cast<void>(qmg::add_multiplayer_player_start(blocked));
    } catch (const std::runtime_error &) {
        threw = true;
    }
    require(threw && blocked.placements.empty(), "no clear station is an error, not a silent omission");

    // The validator does not count the map start toward the spawns a match needs.
    qmg::Layout short_of_spawns = one_room(qmg::Game::quake2, qmg::GameMode::deathmatch, true);
    short_of_spawns.placements.push_back(point(1, qmg::PlacementRole::deathmatch_spawn, {96, 96, 24}));
    short_of_spawns.placements.push_back(point(2, qmg::PlacementRole::player_start, {416, 416, 24}));
    require(has_issue(qmg::LayoutValidator{}.validate(short_of_spawns), "missing_spawns"),
            "one deathmatch spawn plus the map start is still short of two match spawns");
    qmg::Layout single = one_room(qmg::Game::quake2, qmg::GameMode::single_player, true);
    single.spec.target_players = 1;
    single.placements.push_back(point(1, qmg::PlacementRole::player_start, {96, 96, 24}));
    require(!has_issue(qmg::LayoutValidator{}.validate(single), "missing_spawns"),
            "a single-player start still counts as the single-player spawn");
    require(!qmg::is_multiplayer_map_start(single, single.placements.front()),
            "a single-player start is not the multiplayer map start");

    // Visibility accepts the start in Quake 1 and Quake 2 multiplayer maps, and still refuses it in Quake 3.
    for (const qmg::Game game : {qmg::Game::quake1, qmg::Game::quake2}) {
        qmg::Layout accepted = one_room(game, qmg::GameMode::deathmatch, true);
        accepted.placements.push_back(point(1, qmg::PlacementRole::deathmatch_spawn, {96, 96, 24}));
        accepted.placements.push_back(point(2, qmg::PlacementRole::deathmatch_spawn, {416, 96, 24}));
        accepted.placements.push_back(point(3, qmg::PlacementRole::player_start, {256, 416, 24}));
        const qmg::VisibilityAnalysis analysis = qmg::VisibilityAnalyzer{}.analyze(accepted);
        require(analysis.summary.evaluated_spawns == 2, "the map start is not an evaluated match spawn");
        for (const qmg::RoomExposureMetric &exposure : analysis.room_exposure)
            require(exposure.tested_gameplay_placements == 2, "room exposure leaves the map start out");
    }
    qmg::Layout q3 = one_room(qmg::Game::quake3, qmg::GameMode::deathmatch, true);
    q3.placements.push_back(point(1, qmg::PlacementRole::player_start, {96, 96, 24}));
    bool refused = false;
    try {
        static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(q3));
    } catch (const qmg::VisibilityError &error) {
        refused = error.code() == "invalid_mode_semantics";
    }
    require(refused, "Quake 3 turns info_player_start into a deathmatch spawn, so the role is refused there");
}

} // namespace

int main() {
    try {
        registry_row();
        for (const std::string_view prompt :
             {"Quake 1 deathmatch arena, 6 rooms, seed 42", "Quake 1 duel map for two players, 4 rooms, seed 7",
              "Quake 1 capture the flag map, 6 rooms, seed 9", "Quake 1 small two-room deathmatch, 2 rooms, seed 3",
              "Quake 1 vertical deathmatch with high platforms and stairs, 8 rooms, seed 99",
              "Quake 2 deathmatch arena, 6 rooms, seed 42", "Quake 2 duel map for two players, 4 rooms, seed 7",
              "Quake 2 capture the flag map, 6 rooms, seed 9",
              "Quake 2 deathmatch with very vertical combat, 8 rooms, seed 404",
              "Quake 2 small two-room deathmatch, 2 rooms, seed 3"})
            multiplayer_start(prompt);
        for (const std::string_view prompt :
             {"Quake 1 single-player base, 5 rooms, seed 31", "Quake 2 single-player base, 5 rooms, seed 31",
              "Quake 3 capture the flag map, 6 rooms, seed 7", "Quake 3 tourney map, 4 rooms, seed 16",
              "Quake 3 deathmatch arena, 6 rooms, seed 42"})
            outside_scope_unchanged(prompt);
        dense_arenas();
        station_rule_and_contract();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "r1s mp player start tests passed\n";
    return 0;
}
