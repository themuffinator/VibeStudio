// Change r1s-sp-coop-starts (plan 9.3 R1-s item 3; switch r1s.sp_coop_starts; D8 role coop_start).
//
// With coop set, Quake 1 SelectSpawnPoint cycles through the info_player_coop entities and then the
// info_player_start (1996 id1 progs SelectSpawnPoint statements 6014-6030; quake-rerelease-qc quakec/client.qc:562-572)
// and Quake 2 gives co-op client N >= 1 the Nth info_player_coop (game/p_client.c:831-866); the chosen spot is then
// telefragged clear (spawn_tdeath; KillBox). A map with no info_player_coop puts every co-op player on the one start.
// With the switch on, every Quake 1 and Quake 2 single-player layout gets three coop starts beside its start, on
// supported floor, clear of each other and of every other entity, facing the start's way, appended after every other
// placement. Nothing else changes: the other placements, the geometry, the chosen candidate, the single-player match
// analyses and every multiplayer and Quake 3 map stay as they were.
#include "qmg/entities.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/map_builder.hpp"
#include "qmg/map_writer.hpp"
#include "qmg/pipeline.hpp"
#include "qmg/report.hpp"
#include "qmg/switches.hpp"
#include "qmg/validator.hpp"
#include "qmg/visibility.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The registered default: off when introduced (change r1s-sp-coop-starts), on since change r1s-sp-coop-starts-flip.
constexpr bool kExpectedDefaultOn = true;

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::StageSwitches switches(const bool on) {
    const std::vector<std::string> assignment{std::string("r1s.sp_coop_starts=") + (on ? "on" : "off")};
    return qmg::parse_stage_switch_assignments(assignment);
}

[[nodiscard]] qmg::PipelineResult run(const std::string_view prompt, const bool on,
                                      const qmg::PromptOverrides &overrides = {}) {
    qmg::PipelineOptions options;
    options.switches = switches(on);
    return qmg::GenerationPipeline{}.run(prompt, overrides, options);
}

[[nodiscard]] std::vector<const qmg::Placement *> with_role(const qmg::Layout &layout, const qmg::PlacementRole role) {
    std::vector<const qmg::Placement *> found;
    for (const qmg::Placement &placement : layout.placements) {
        if (placement.role == role)
            found.push_back(&placement);
    }
    return found;
}

[[nodiscard]] bool overlaps(const qmg::GridBounds &a, const qmg::GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

[[nodiscard]] bool separated(const qmg::GridVec3 a, const qmg::GridVec3 b) {
    return std::abs(static_cast<std::int64_t>(a.x) - b.x) >= 64 || std::abs(static_cast<std::int64_t>(a.y) - b.y) >= 64;
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

[[nodiscard]] std::vector<qmg::NavigationPlacementBinding> bindings_without(const qmg::NavigationAnalysis &analysis,
                                                                            const std::vector<qmg::PlacementId> &ids) {
    std::vector<qmg::NavigationPlacementBinding> bindings = analysis.placement_bindings;
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                  [&ids](const qmg::NavigationPlacementBinding &entry) {
                                      return std::find(ids.begin(), ids.end(), entry.placement) != ids.end();
                                  }),
                   bindings.end());
    return bindings;
}

void registry_row() {
    const auto registry = qmg::stage_switch_registry();
    const auto row = std::find_if(registry.begin(), registry.end(), [](const qmg::StageSwitchDefinition &definition) {
        return definition.name == "r1s.sp_coop_starts";
    });
    require(row != registry.end(), "r1s.sp_coop_starts is registered");
    require(row->id == qmg::StageSwitch::r1s_sp_coop_starts, "the typed handle names the row");
    require(row->default_on == kExpectedDefaultOn, "the registered default is the expected one");
    require(row->sub_lane == qmg::SwitchSubLane::sp, "the cause touches single-player maps only: sub-lane sp");
    require(row->introduced_by == "r1s-sp-coop-starts", "the row records its change id");
    require(qmg::StageSwitches{}.on(qmg::StageSwitch::r1s_sp_coop_starts) == kExpectedDefaultOn,
            "a default set holds the registered default");
}

// The coop-start contract for one Quake 1 or Quake 2 single-player prompt, and the one-cause contract against the
// switched-off run.
void single_player_coop_starts(const std::string_view prompt, const qmg::PromptOverrides &overrides = {}) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false, overrides);
    const qmg::PipelineResult on = run(prompt, true, overrides);
    require(off.layout.spec.game != qmg::Game::quake3 && off.layout.spec.mode == qmg::GameMode::single_player,
            label + ": a Quake 1 or Quake 2 single-player prompt");
    require(on.layout.spec.game == off.layout.spec.game && on.layout.spec.mode == off.layout.spec.mode,
            label + ": the switch does not change the resolved game or mode");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(off.validation.ok() && on.validation.ok(), label + ": both layouts validate");
    require(with_role(off.layout, qmg::PlacementRole::coop_start).empty(), label + ": the switched-off map has none");
    const std::vector<const qmg::Placement *> coops = with_role(on.layout, qmg::PlacementRole::coop_start);
    require(coops.size() == qmg::kSinglePlayerCoopStarts && coops.size() == 3, label + ": exactly three coop starts");
    const std::vector<const qmg::Placement *> starts = with_role(on.layout, qmg::PlacementRole::player_start);
    require(starts.size() == 1, label + ": one info_player_start");
    const qmg::Placement &start = *starts.front();

    // One cause: the coop starts are the last three placements and everything else is unchanged.
    require(on.layout.placements.size() == off.layout.placements.size() + 3, label + ": three placements added");
    for (std::size_t index = 0; index < 3; ++index)
        require(coops[index] == &on.layout.placements[off.layout.placements.size() + index],
                label + ": the coop starts are appended last");
    std::vector<qmg::Placement> others(on.layout.placements.begin(), on.layout.placements.end() - 3);
    require(others == off.layout.placements, label + ": every other placement is unchanged");
    require(same_rooms(on.layout.rooms, off.layout.rooms) &&
                same_connections(on.layout.connections, off.layout.connections) &&
                on.layout.solids == off.layout.solids && on.layout.candidate_seed == off.layout.candidate_seed &&
                on.layout.rank_score == off.layout.rank_score,
            label + ": rooms, connections, solids, the chosen candidate and its rank score are unchanged");
    require(on.repairs.size() == off.repairs.size(), label + ": the same repair passes");

    // The start room: the room that holds the info_player_start.
    const qmg::Room *room = nullptr;
    for (const qmg::Room &candidate : on.layout.rooms) {
        if (candidate.bounds.contains(start.origin)) {
            room = &candidate;
            break;
        }
    }
    require(room != nullptr, label + ": the start is inside a room");

    std::vector<qmg::PlacementId> ids;
    for (const qmg::Placement *coop : coops) {
        ids.push_back(coop->id);
        require(!coop->volume && !coop->team && coop->variant == 0 && !coop->pitch_degrees && !coop->light_intensity &&
                    !coop->light_color && !coop->destination_map && !coop->link,
                label + ": a bare point entity");
        require(coop->angle_degrees == start.angle_degrees, label + ": the start's facing");
        require(room->bounds.contains(coop->origin), label + ": in the start room");
        require(coop->origin.z == room->bounds.min.z + 24, label + ": the origin stands 24 units above the room floor");
        // The standing box (32 x 32 x 56) is inside the room and overlaps no solid and no entity volume.
        const qmg::GridBounds hull{{coop->origin.x - 16, coop->origin.y - 16, coop->origin.z - 24},
                                   {coop->origin.x + 16, coop->origin.y + 16, coop->origin.z + 32}};
        require(hull.min.x >= room->bounds.min.x && hull.max.x <= room->bounds.max.x &&
                    hull.min.y >= room->bounds.min.y && hull.max.y <= room->bounds.max.y &&
                    hull.min.z >= room->bounds.min.z && hull.max.z <= room->bounds.max.z,
                label + ": the standing box stays inside the room");
        for (const qmg::LayoutSolid &solid : on.layout.solids)
            require(!overlaps(hull, qmg::checked_brush_bounds(solid.geometry)), label + ": no solid overlaps it");
        // 64 units (on x or y) from every other point entity, the start, the other coop starts, lights and the
        // Quake 1 camera included.
        for (const qmg::Placement &placement : on.layout.placements) {
            if (placement.id == coop->id)
                continue;
            if (placement.volume) {
                require(!overlaps(hull, *placement.volume), label + ": no entity volume overlaps it");
                continue;
            }
            require(separated(placement.origin, coop->origin), label + ": at least 64 units from every other entity");
        }
    }

    // Supported and reachable: player navigation binds each to a walkable node and no finding names any of them;
    // every other binding and every navigation finding (player and monsters) is unchanged.
    require(on.player_navigation && off.player_navigation, label + ": player navigation ran");
    for (const qmg::Placement *coop : coops) {
        const qmg::NavigationPlacementBinding *binding = on.player_navigation->find_binding(coop->id);
        require(binding != nullptr && binding->node.has_value(), label + ": each coop start binds to a node");
    }
    require(bindings_without(*on.player_navigation, ids) == off.player_navigation->placement_bindings,
            label + ": every other player binding is unchanged");
    require(on.player_navigation->findings == off.player_navigation->findings,
            label + ": player navigation findings are unchanged");
    require(on.monster_small_navigation && off.monster_small_navigation && on.monster_large_navigation &&
                off.monster_large_navigation,
            label + ": monster navigation ran");
    require(on.monster_small_navigation->findings == off.monster_small_navigation->findings &&
                on.monster_large_navigation->findings == off.monster_large_navigation->findings,
            label + ": monster navigation findings are unchanged");

    // Single-player analyses leave the coop starts out: spawn safety, room exposure and timing are unchanged.
    require(on.visibility && off.visibility, label + ": visibility ran");
    require(on.visibility->spawn_safety == off.visibility->spawn_safety &&
                on.visibility->findings == off.visibility->findings &&
                on.visibility->room_exposure == off.visibility->room_exposure &&
                on.visibility->summary.evaluated_spawns == off.visibility->summary.evaluated_spawns,
            label + ": visibility spawn safety, findings and room exposure are unchanged");
    require(on.quality && off.quality, label + ": quality ran");
    require(on.quality->placement_timing == off.quality->placement_timing,
            label + ": timing is measured from the single-player start only");

    // Lit: each coop start is a readable critical lighting sample, so the lighting findings are unchanged.
    require(on.lighting && off.lighting, label + ": lighting ran");
    for (const qmg::Placement *coop : coops) {
        const auto sample =
            std::find_if(on.lighting->samples.begin(), on.lighting->samples.end(), [coop](const auto &metric) {
                return metric.kind == qmg::LightingSampleKind::gameplay_placement && metric.source_id == coop->id.value;
            });
        require(sample != on.lighting->samples.end() && sample->critical && sample->readable,
                label + ": each coop start is a readable critical lighting sample");
    }
    require(on.lighting->findings == off.lighting->findings, label + ": the coop starts add no lighting finding");

    // The map gains exactly the three info_player_coop entities, as its last entities, with no targetname.
    const std::string off_map = qmg::MapWriter{}.render(off.map_build->document);
    const std::string on_map = qmg::MapWriter{}.render(on.map_build->document);
    std::string blocks;
    for (const qmg::Placement *coop : coops) {
        blocks += "{\n\"classname\" \"info_player_coop\"\n";
        if (coop->angle_degrees)
            blocks += "\"angle\" \"" + std::to_string(*coop->angle_degrees) + "\"\n";
        blocks += "\"origin\" \"" + origin_text(coop->origin) + "\"\n}\n";
    }
    require(on_map.size() == off_map.size() + blocks.size() &&
                on_map.compare(off_map.size(), blocks.size(), blocks) == 0,
            label + ": the map ends with the three info_player_coop entities");
    require(on_map.substr(0, off_map.size()) == off_map, label + ": the map differs only by them");
    require(on.map_build->stats.entity_count == off.map_build->stats.entity_count + 3, label + ": three more entities");

    // The report names the switch only where it is away from its default; the run is deterministic.
    const qmg::PipelineResult &switched = kExpectedDefaultOn ? off : on;
    const qmg::PipelineResult &defaulted = kExpectedDefaultOn ? on : off;
    const std::string value = kExpectedDefaultOn ? "off" : "on";
    require(qmg::render_report(switched).find("\"r1s.sp_coop_starts\": \"" + value + "\"") != std::string::npos,
            label + ": the non-default run's report lists the switch");
    require(qmg::render_report(defaulted).find("r1s.sp_coop_starts") == std::string::npos,
            label + ": the default run's report does not name the switch");
    qmg::PipelineOptions defaults;
    require(qmg::MapWriter{}.render(qmg::GenerationPipeline{}.run(prompt, overrides, defaults).map_build->document) ==
                (kExpectedDefaultOn ? on_map : off_map),
            label + ": a run with no --switch is the default side");
    const qmg::PipelineResult again = run(prompt, true, overrides);
    require(qmg::MapWriter{}.render(again.map_build->document) == on_map &&
                qmg::canonical_layout_sha256(again.layout) == qmg::canonical_layout_sha256(on.layout),
            label + ": deterministic");
}

// Multiplayer and Quake 3 maps are outside the cause: the switch changes no placement and no map byte.
void outside_scope_unchanged(const std::string_view prompt) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false);
    const qmg::PipelineResult on = run(prompt, true);
    require(on.layout.spec.game == qmg::Game::quake3 || on.layout.spec.mode != qmg::GameMode::single_player,
            label + ": a multiplayer or Quake 3 prompt");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(on.layout.placements == off.layout.placements, label + ": placements unchanged");
    require(qmg::MapWriter{}.render(on.map_build->document) == qmg::MapWriter{}.render(off.map_build->document),
            label + ": map bytes unchanged");
}

// Every room count and a spread of seeds and intensities: three coop starts each time, and the layout validates.
void sweep() {
    for (const qmg::Game game : {qmg::Game::quake1, qmg::Game::quake2}) {
        for (const int rooms : {2, 3, 5, 8, 12}) {
            for (std::uint64_t seed = 1; seed <= 3; ++seed) {
                qmg::PromptOverrides overrides;
                overrides.game = game;
                overrides.mode = qmg::GameMode::single_player;
                overrides.room_count = rooms;
                overrides.seed = seed;
                const qmg::PipelineResult result =
                    run("brutal vertical single-player base with stairs", true, overrides);
                const std::string label = "sweep game " + std::to_string(static_cast<int>(game)) + " rooms " +
                                          std::to_string(rooms) + " seed " + std::to_string(seed);
                require(with_role(result.layout, qmg::PlacementRole::coop_start).size() == 3,
                        label + ": three coop starts");
                require(result.validation.ok() && result.map_build, label + ": validates and builds");
            }
        }
    }
}

[[nodiscard]] qmg::Layout one_room(const qmg::Game game, const qmg::GameMode mode, const bool on) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.mode = mode;
    layout.spec.target_players = 1;
    layout.spec.title = "coop start fixture";
    layout.spec.theme = "industrial";
    layout.spec.switches = switches(on);
    layout.candidate_seed = 0x5EEDU;
    layout.rooms.push_back({qmg::RoomId{1}, "room", {{0, 0, 0}, {512, 512, 256}}, {"start"}});
    return layout;
}

[[nodiscard]] qmg::Placement point(const std::uint32_t id, const qmg::PlacementRole role, const qmg::GridVec3 origin,
                                   const std::optional<std::int16_t> angle = std::nullopt) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    placement.angle_degrees = angle;
    return placement;
}

[[nodiscard]] qmg::Placement lamp(const std::uint32_t id, const qmg::GridVec3 origin) {
    qmg::Placement light = point(id, qmg::PlacementRole::light, origin);
    light.light_intensity = 300;
    light.light_color = qmg::RgbColor{1000, 1000, 1000};
    return light;
}

[[nodiscard]] qmg::LayoutSolid block(const std::uint32_t id, const qmg::GridBounds bounds) {
    return {qmg::SolidId{id},
            bounds,
            qmg::MaterialRole::wall,
            qmg::BrushPurpose::structural,
            qmg::BrushOwnership::structural,
            qmg::BrushContents::solid,
            std::nullopt};
}

template <typename Error> [[nodiscard]] bool throws(qmg::Layout layout) {
    try {
        static_cast<void>(qmg::add_single_player_coop_starts(layout));
    } catch (const Error &) {
        return true;
    }
    return false;
}

[[nodiscard]] std::string build_error(const qmg::Layout &layout, const qmg::MapDialect dialect) {
    qmg::MapBuildOptions options;
    options.dialect = dialect;
    try {
        static_cast<void>(qmg::MapBuilder{}.build(layout, options));
    } catch (const qmg::MapBuildError &error) {
        return error.code();
    }
    return {};
}

// The station rule and the role's contract outside the generator.
void station_rule_and_contract() {
    // Off: nothing happens.
    qmg::Layout off = one_room(qmg::Game::quake1, qmg::GameMode::single_player, false);
    off.placements.push_back(point(1, qmg::PlacementRole::player_start, {192, 256, 32}, 0));
    const std::vector<qmg::Placement> before = off.placements;
    require(!qmg::add_single_player_coop_starts(off) && off.placements == before, "the switched-off path adds nothing");

    // Multiplayer and Quake 3 layouts are outside the cause even with the switch on.
    for (const auto &[game, mode] : {std::pair{qmg::Game::quake1, qmg::GameMode::deathmatch},
                                     std::pair{qmg::Game::quake2, qmg::GameMode::capture_the_flag},
                                     std::pair{qmg::Game::quake2, qmg::GameMode::duel},
                                     std::pair{qmg::Game::quake3, qmg::GameMode::single_player}}) {
        qmg::Layout outside = one_room(game, mode, true);
        outside.placements.push_back(point(1, qmg::PlacementRole::deathmatch_spawn, {192, 256, 24}));
        const std::vector<qmg::Placement> unchanged = outside.placements;
        require(!qmg::add_single_player_coop_starts(outside) && outside.placements == unchanged,
                "multiplayer and Quake 3 layouts get no coop starts");
    }

    // On: the three lit, clear lattice stations nearest the start, each 64 units clear of the start, the light and
    // the coop starts before it, facing the start's way. The lattice runs 32, 64, ..., 480 on both axes. A pillar at
    // x 208-224, y 224-288 hides the light at (256, 256) from the nearest stations west of it, (96, 256), (160, 192),
    // (160, 320) and the diagonals, so the two lit stations just east of it at 64 + 64 units win, then (160, 128).
    qmg::Layout level = one_room(qmg::Game::quake2, qmg::GameMode::single_player, true);
    level.placements.push_back(point(1, qmg::PlacementRole::player_start, {160, 256, 32}, 90));
    level.placements.push_back(lamp(2, {256, 256, 192}));
    level.solids.push_back(block(1, {{400, 0, 0}, {512, 112, 64}}));
    level.solids.push_back(block(2, {{208, 224, 0}, {224, 288, 256}}));
    require(qmg::add_single_player_coop_starts(level), "a Quake 2 single-player layout gets coop starts");
    require(level.placements.size() == 5, "exactly three placements are added");
    const std::vector<qmg::GridVec3> expected{{224, 192, 24}, {224, 320, 24}, {160, 128, 24}};
    for (std::size_t index = 0; index < 3; ++index) {
        const qmg::Placement &coop = level.placements[2 + index];
        require(coop.role == qmg::PlacementRole::coop_start &&
                    coop.id == qmg::PlacementId{static_cast<std::uint32_t>(3 + index)},
                "each coop start takes the next placement id");
        require(coop.origin == expected[index],
                "the lit stations nearest the start, feet on the floor, in order (" + origin_text(coop.origin) + ")");
        require(coop.angle_degrees && *coop.angle_degrees == 90, "facing the start's way");
        for (std::size_t other = 0; other < level.placements.size(); ++other) {
            if (other != 2 + index)
                require(separated(level.placements[other].origin, coop.origin), "64 units from every other entity");
        }
    }
    require(throws<std::logic_error>(level), "a second set of coop starts is refused");

    // The validator, visibility and lighting accept them in Quake 1 and Quake 2 single player.
    require(!has_issue(qmg::LayoutValidator{}.validate(level), "unsupported_coop_start") &&
                !has_issue(qmg::LayoutValidator{}.validate(level), "placement_overlap") &&
                !has_issue(qmg::LayoutValidator{}.validate(level), "placement_in_solid"),
            "the validator accepts the coop starts");
    const qmg::VisibilityAnalysis visibility = qmg::VisibilityAnalyzer{}.analyze(level);
    require(visibility.summary.evaluated_spawns == 1, "the coop starts are not single-player spawns");
    for (const qmg::RoomExposureMetric &exposure : visibility.room_exposure)
        require(exposure.tested_gameplay_placements == 1, "room exposure leaves the coop starts out");

    // A start room with too few clear stations falls back to the rooms joined to it, never to an unjoined room.
    qmg::Layout crowded = one_room(qmg::Game::quake1, qmg::GameMode::single_player, true);
    crowded.rooms.front().bounds = {{0, 0, 0}, {128, 128, 256}};
    crowded.rooms.push_back({qmg::RoomId{2}, "joined", {{128, 0, 0}, {640, 512, 256}}, {}});
    crowded.rooms.push_back({qmg::RoomId{3}, "apart", {{0, 1024, 0}, {512, 1536, 256}}, {}});
    crowded.connections.push_back({qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 64, 128, {}});
    crowded.placements.push_back(point(1, qmg::PlacementRole::player_start, {64, 64, 32}, 0));
    require(qmg::add_single_player_coop_starts(crowded) && crowded.placements.size() == 4,
            "the joined room takes the coop starts the start room cannot");
    for (std::size_t index = 1; index < crowded.placements.size(); ++index)
        require(crowded.rooms[1].bounds.contains(crowded.placements[index].origin),
                "the start room's lattice is full, so every coop start is in the joined room");

    // No start, two starts, or too few clear stations: fail closed, never a silent omission.
    qmg::Layout no_start = one_room(qmg::Game::quake1, qmg::GameMode::single_player, true);
    require(throws<std::logic_error>(no_start), "a single-player layout without a start is refused");
    qmg::Layout two_starts = no_start;
    two_starts.placements.push_back(point(1, qmg::PlacementRole::player_start, {96, 96, 32}, 0));
    two_starts.placements.push_back(point(2, qmg::PlacementRole::player_start, {416, 416, 32}, 0));
    require(throws<std::logic_error>(two_starts), "two starts are refused");
    qmg::Layout blocked = one_room(qmg::Game::quake2, qmg::GameMode::single_player, true);
    blocked.placements.push_back(point(1, qmg::PlacementRole::player_start, {256, 256, 96}, 0));
    blocked.solids.push_back(block(1, {{0, 0, 0}, {512, 512, 64}}));
    require(throws<std::runtime_error>(blocked), "no clear station is an error, not a silent omission");

    // The written entities: info_player_coop with the start's angle and no targetname, in both games.
    for (const auto &[game, dialect] : {std::pair{qmg::Game::quake1, qmg::MapDialect::quake1_valve220},
                                        std::pair{qmg::Game::quake2, qmg::MapDialect::quake2_valve220}}) {
        qmg::Layout written = level;
        written.spec.game = game;
        qmg::MapBuildOptions options;
        options.dialect = dialect;
        const std::string text = qmg::MapWriter{}.render(qmg::MapBuilder{}.build(written, options).document);
        require(text.find("{\n\"classname\" \"info_player_coop\"\n\"angle\" \"90\"\n\"origin\" \"224 192 24\"\n}\n") !=
                    std::string::npos,
                "a coop start is written as info_player_coop with angle and origin only");
        require(text.find("targetname") == std::string::npos, "no targetname: generated maps are loaded directly");
    }

    // Quake 3 has no info_player_coop: the validator, the builder and the visibility analysis refuse one.
    qmg::Layout q3 = one_room(qmg::Game::quake3, qmg::GameMode::deathmatch, true);
    q3.placements.push_back(point(1, qmg::PlacementRole::deathmatch_spawn, {96, 96, 24}));
    q3.placements.push_back(point(2, qmg::PlacementRole::deathmatch_spawn, {416, 416, 24}));
    q3.placements.push_back(point(3, qmg::PlacementRole::coop_start, {256, 96, 24}, 0));
    require(has_issue(qmg::LayoutValidator{}.validate(q3), "unsupported_coop_start"), "the validator refuses it");
    require(build_error(q3, qmg::MapDialect::quake3_legacy) == "unsupported_placement", "the builder refuses it");
    bool refused = false;
    try {
        static_cast<void>(qmg::VisibilityAnalyzer{}.analyze(q3));
    } catch (const qmg::VisibilityError &error) {
        refused = error.code() == "invalid_mode_semantics";
    }
    require(refused, "the visibility analysis refuses it");
}

} // namespace

int main() {
    try {
        registry_row();
        for (const std::string_view prompt :
             {"Quake 1 single-player base, 5 rooms, seed 31", "a quake 1 single player level",
              "Quake 1 brutal single-player castle with many monsters, 8 rooms, seed 5",
              "Quake 1 vertical single-player base with high platforms and stairs, 6 rooms, seed 99",
              "Quake 2 single-player base, 5 rooms, seed 31", "a quake 2 single player level",
              "Quake 2 brutal single-player outpost with many monsters, 8 rooms, seed 5",
              "Quake 2 vertical single-player base with high platforms and stairs, 6 rooms, seed 99",
              "Quake 2 small single-player base, 2 rooms, seed 3"})
            single_player_coop_starts(prompt);
        for (const std::string_view prompt :
             {"Quake 1 deathmatch arena, 6 rooms, seed 42", "Quake 2 deathmatch arena, 6 rooms, seed 42",
              "Quake 2 capture the flag map, 6 rooms, seed 9", "Quake 1 duel map for two players, 4 rooms, seed 7",
              "Quake 3 capture the flag map, 6 rooms, seed 7", "Quake 3 deathmatch arena, 6 rooms, seed 42",
              "Quake 3 single player campaign map, 5 rooms, seed 11"})
            outside_scope_unchanged(prompt);
        sweep();
        station_rule_and_contract();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "r1s sp coop starts tests passed\n";
    return 0;
}
