// Change r1s-q3-ctf-team-begin (plan 9.3 R1-s item 4; switch r1s.q3_ctf_team_begin; existing role team_initial_spawn).
//
// A Quake 3 client that enters a CTF game, or joins a team, spawns in TEAM_BEGIN at a team_CTF_redplayer or
// team_CTF_blueplayer spot that SpotWouldTelefrag finds free; when none is free, SelectRandomTeamSpawnPoint returns
// the class's first spot anyway (code/game/g_team.c:983-1024) and ClientSpawn telefrags whoever stands there
// (code/game/g_client.c:1205). Generated CTF maps had one begin spot per team, so at a match start the second player of
// each team telefragged the first. With the switch on, every Quake 3 CTF layout gets ceil(target_players / 2) begin
// spots per team, in the team's base, on supported floor, clear of each other and of every other entity, facing into
// the base, appended after every other placement. Nothing else changes: the other placements (the team_CTF_*spawn
// respawn spots included), the geometry, the chosen candidate and every other game and mode stay as they were.
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

// The registered default: off when introduced (change r1s-q3-ctf-team-begin).
constexpr bool kExpectedDefaultOn = false;

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::StageSwitches switches(const bool on) {
    const std::vector<std::string> assignment{std::string("r1s.q3_ctf_team_begin=") + (on ? "on" : "off")};
    return qmg::parse_stage_switch_assignments(assignment);
}

[[nodiscard]] qmg::PipelineResult run(const std::string_view prompt, const bool on,
                                      const qmg::PromptOverrides &overrides = {}) {
    qmg::PipelineOptions options;
    options.switches = switches(on);
    return qmg::GenerationPipeline{}.run(prompt, overrides, options);
}

[[nodiscard]] std::vector<const qmg::Placement *> begin_spots(const qmg::Layout &layout, const qmg::Team team) {
    std::vector<const qmg::Placement *> found;
    for (const qmg::Placement &placement : layout.placements) {
        if (placement.role == qmg::PlacementRole::team_initial_spawn && placement.team == team)
            found.push_back(&placement);
    }
    return found;
}

[[nodiscard]] const qmg::Room *base_room(const qmg::Layout &layout, const qmg::Team team) {
    for (const qmg::Placement &placement : layout.placements) {
        if (placement.role != qmg::PlacementRole::flag || placement.team != team)
            continue;
        for (const qmg::Room &room : layout.rooms) {
            if (room.bounds.contains(placement.origin))
                return &room;
        }
    }
    return nullptr;
}

[[nodiscard]] bool overlaps(const qmg::GridBounds &a, const qmg::GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

[[nodiscard]] bool separated(const qmg::GridVec3 a, const qmg::GridVec3 b) {
    return std::abs(static_cast<std::int64_t>(a.x) - b.x) >= 64 || std::abs(static_cast<std::int64_t>(a.y) - b.y) >= 64;
}

[[nodiscard]] std::int16_t yaw_towards(const qmg::GridVec3 from, const qmg::GridVec3 to) {
    const long double dx = static_cast<long double>(to.x) - from.x;
    const long double dy = static_cast<long double>(to.y) - from.y;
    if (dx == 0.0L && dy == 0.0L)
        return 0;
    auto yaw = static_cast<std::int16_t>(std::lround(std::atan2(dy, dx) * 180.0L / std::numbers::pi_v<long double>));
    if (yaw < 0)
        yaw = static_cast<std::int16_t>(yaw + 360);
    if (yaw >= 360)
        yaw = static_cast<std::int16_t>(yaw - 360);
    return yaw;
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

[[nodiscard]] std::size_t count_issue(const qmg::ValidationResult &result, const std::string_view code) {
    return static_cast<std::size_t>(std::count_if(result.issues.begin(), result.issues.end(),
                                                  [code](const qmg::Issue &issue) { return issue.code == code; }));
}

void registry_row() {
    const auto registry = qmg::stage_switch_registry();
    const auto row = std::find_if(registry.begin(), registry.end(), [](const qmg::StageSwitchDefinition &definition) {
        return definition.name == "r1s.q3_ctf_team_begin";
    });
    require(row != registry.end(), "r1s.q3_ctf_team_begin is registered");
    require(row->id == qmg::StageSwitch::r1s_q3_ctf_team_begin, "the typed handle names the row");
    require(row->default_on == kExpectedDefaultOn, "the registered default is the expected one");
    require(row->sub_lane == qmg::SwitchSubLane::mp, "the cause touches Quake 3 CTF maps only: sub-lane mp");
    require(row->introduced_by == "r1s-q3-ctf-team-begin", "the row records its change id");
    require(qmg::StageSwitches{}.on(qmg::StageSwitch::r1s_q3_ctf_team_begin) == kExpectedDefaultOn,
            "a default set holds the registered default");
}

void per_team_rule() {
    qmg::PromptSpec spec;
    for (const auto &[players, expected] :
         {std::pair{0, 1}, std::pair{2, 1}, std::pair{3, 2}, std::pair{6, 3}, std::pair{7, 4}, std::pair{8, 4},
          std::pair{16, 8}, std::pair{31, 16}, std::pair{32, 16}}) {
        spec.target_players = players;
        require(qmg::quake3_ctf_team_begin_spots_per_team(spec) == expected,
                "ceil(max(target_players, 2) / 2) begin spots per team for " + std::to_string(players) + " players");
    }
}

// The team-begin contract for one Quake 3 CTF prompt, and the one-cause contract against the switched-off run.
void ctf_team_begin(const std::string_view prompt, const qmg::PromptOverrides &overrides = {}) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false, overrides);
    const qmg::PipelineResult on = run(prompt, true, overrides);
    require(off.layout.spec.game == qmg::Game::quake3 && off.layout.spec.mode == qmg::GameMode::capture_the_flag,
            label + ": a Quake 3 CTF prompt");
    require(on.layout.spec.game == off.layout.spec.game && on.layout.spec.mode == off.layout.spec.mode &&
                on.layout.spec.target_players == off.layout.spec.target_players,
            label + ": the switch does not change the resolved game, mode or players");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(off.validation.ok() && on.validation.ok(), label + ": both layouts validate");
    const auto per_team = static_cast<std::size_t>(qmg::quake3_ctf_team_begin_spots_per_team(on.layout.spec));
    require(per_team * 2 >= static_cast<std::size_t>(on.layout.spec.target_players) && per_team >= 3,
            label + ": a begin spot for every player of a full team (CTF holds at least six players)");
    const std::size_t off_red = begin_spots(off.layout, qmg::Team::red).size();
    const std::size_t off_blue = begin_spots(off.layout, qmg::Team::blue).size();
    require(off_red == 1 && off_blue == 1, label + ": the switched-off map has one begin spot per team");
    require(begin_spots(on.layout, qmg::Team::red).size() == per_team &&
                begin_spots(on.layout, qmg::Team::blue).size() == per_team,
            label + ": ceil(target_players / 2) begin spots per team");
    const std::size_t added = (per_team - off_red) + (per_team - off_blue);

    // One cause: the new spots are the last placements, red first, and everything else is unchanged.
    require(on.layout.placements.size() == off.layout.placements.size() + added, label + ": only begin spots added");
    std::vector<qmg::Placement> others(on.layout.placements.begin(),
                                       on.layout.placements.begin() +
                                           static_cast<std::ptrdiff_t>(off.layout.placements.size()));
    require(others == off.layout.placements, label + ": every other placement, the respawn spots included, unchanged");
    require(same_rooms(on.layout.rooms, off.layout.rooms) &&
                same_connections(on.layout.connections, off.layout.connections) &&
                on.layout.solids == off.layout.solids && on.layout.candidate_seed == off.layout.candidate_seed &&
                on.layout.rank_score == off.layout.rank_score,
            label + ": rooms, connections, solids, the chosen candidate and its rank score are unchanged");
    require(on.repairs.size() == off.repairs.size(), label + ": the same repair passes");

    std::vector<const qmg::Placement *> appended;
    for (std::size_t index = off.layout.placements.size(); index < on.layout.placements.size(); ++index) {
        const qmg::Placement &spot = on.layout.placements[index];
        require(spot.role == qmg::PlacementRole::team_initial_spawn && spot.team.has_value(),
                label + ": every appended placement is a team-begin spot");
        if (!appended.empty())
            require(!(*appended.back()->team == qmg::Team::blue && *spot.team == qmg::Team::red),
                    label + ": red spots come before blue spots");
        appended.push_back(&spot);
    }
    for (const qmg::Team team : {qmg::Team::red, qmg::Team::blue}) {
        const qmg::Room *base = base_room(on.layout, team);
        require(base != nullptr, label + ": the team's flag marks a base room");
        for (const qmg::Placement *spot : begin_spots(on.layout, team))
            require(base->bounds.contains(spot->origin), label + ": every begin spot of the team is in its base");
    }

    for (const qmg::Placement *spot : appended) {
        const qmg::Room *base = base_room(on.layout, *spot->team);
        require(!spot->volume && spot->variant == 0 && !spot->pitch_degrees && !spot->light_intensity &&
                    !spot->light_color && !spot->destination_map && !spot->link,
                label + ": a bare point entity");
        require(spot->angle_degrees && *spot->angle_degrees == yaw_towards(spot->origin, base->bounds.center()),
                label + ": facing into the base (its centre)");
        require(spot->origin.z == base->bounds.min.z + 24, label + ": the origin stands 24 units above the base floor");
        const qmg::GridBounds hull{{spot->origin.x - 16, spot->origin.y - 16, spot->origin.z - 24},
                                   {spot->origin.x + 16, spot->origin.y + 16, spot->origin.z + 32}};
        require(hull.min.x >= base->bounds.min.x && hull.max.x <= base->bounds.max.x &&
                    hull.min.y >= base->bounds.min.y && hull.max.y <= base->bounds.max.y &&
                    hull.min.z >= base->bounds.min.z && hull.max.z <= base->bounds.max.z,
                label + ": the standing box stays inside the base");
        for (const qmg::LayoutSolid &solid : on.layout.solids)
            require(!overlaps(hull, qmg::checked_brush_bounds(solid.geometry)), label + ": no solid overlaps it");
        for (const qmg::Placement &placement : on.layout.placements) {
            if (placement.id == spot->id)
                continue;
            if (placement.volume) {
                require(!overlaps(hull, *placement.volume), label + ": no entity volume overlaps it");
                continue;
            }
            require(separated(placement.origin, spot->origin), label + ": at least 64 units from every other entity");
        }
    }

    // Supported and reachable: player navigation binds each new spot to a walkable node; the other bindings stay.
    require(on.player_navigation && off.player_navigation, label + ": player navigation ran");
    for (const qmg::Placement *spot : appended) {
        const qmg::NavigationPlacementBinding *binding = on.player_navigation->find_binding(spot->id);
        require(binding != nullptr && binding->node.has_value(), label + ": each new begin spot binds to a node");
    }
    for (const qmg::NavigationPlacementBinding &binding : off.player_navigation->placement_bindings) {
        const qmg::NavigationPlacementBinding *same = on.player_navigation->find_binding(binding.placement);
        require(same != nullptr && *same == binding, label + ": every earlier binding is unchanged");
    }

    // The match analyses read them as the active team spawns they are.
    require(on.visibility && off.visibility, label + ": visibility ran");
    require(on.visibility->summary.evaluated_spawns == off.visibility->summary.evaluated_spawns + added,
            label + ": spawn safety evaluates every new begin spot");
    require(on.lighting && off.lighting, label + ": lighting ran");
    for (const qmg::Placement *spot : appended) {
        const auto sample =
            std::find_if(on.lighting->samples.begin(), on.lighting->samples.end(), [spot](const auto &metric) {
                return metric.kind == qmg::LightingSampleKind::gameplay_placement && metric.source_id == spot->id.value;
            });
        require(sample != on.lighting->samples.end() && sample->critical && sample->readable,
                label + ": each new begin spot is a readable critical lighting sample");
    }
    require(on.lighting->findings == off.lighting->findings, label + ": the new spots add no lighting finding");

    // The map gains exactly the new team_CTF_redplayer / team_CTF_blueplayer entities, as its last entities.
    const std::string off_map = qmg::MapWriter{}.render(off.map_build->document);
    const std::string on_map = qmg::MapWriter{}.render(on.map_build->document);
    std::string blocks;
    for (const qmg::Placement *spot : appended) {
        blocks += std::string("{\n\"classname\" \"") +
                  (*spot->team == qmg::Team::red ? "team_CTF_redplayer" : "team_CTF_blueplayer") + "\"\n";
        blocks += "\"angle\" \"" + std::to_string(*spot->angle_degrees) + "\"\n";
        blocks += "\"origin\" \"" + origin_text(spot->origin) + "\"\n}\n";
    }
    require(on_map.size() == off_map.size() + blocks.size() &&
                on_map.compare(off_map.size(), blocks.size(), blocks) == 0,
            label + ": the map ends with the new team-begin entities");
    require(on_map.substr(0, off_map.size()) == off_map, label + ": the map differs only by them");
    require(on.map_build->stats.entity_count == off.map_build->stats.entity_count + added,
            label + ": one more entity per new spot");

    // The report names the switch only where it is away from its default; the run is deterministic.
    const qmg::PipelineResult &switched = kExpectedDefaultOn ? off : on;
    const qmg::PipelineResult &defaulted = kExpectedDefaultOn ? on : off;
    const std::string value = kExpectedDefaultOn ? "off" : "on";
    require(qmg::render_report(switched).find("\"r1s.q3_ctf_team_begin\": \"" + value + "\"") != std::string::npos,
            label + ": the non-default run's report lists the switch");
    require(qmg::render_report(defaulted).find("r1s.q3_ctf_team_begin") == std::string::npos,
            label + ": the default run's report does not name the switch");
    qmg::PipelineOptions defaults;
    require(qmg::MapWriter{}.render(qmg::GenerationPipeline{}.run(prompt, overrides, defaults).map_build->document) ==
                (kExpectedDefaultOn ? on_map : off_map),
            label + ": a run with no --switch is the default side");
    const qmg::PipelineResult again = run(prompt, true, overrides);
    require(qmg::MapWriter{}.render(again.map_build->document) == on_map &&
                qmg::canonical_layout_sha256(again.layout) == qmg::canonical_layout_sha256(on.layout),
            label + ": deterministic");

    // The validator's switched contract: the layout meets it, and loses it with one spot fewer.
    qmg::Layout short_one = on.layout;
    short_one.placements.pop_back();
    require(count_issue(qmg::LayoutValidator{}.validate(short_one), "ctf_spawn_contract") == 1,
            label + ": a team with one begin spot too few breaks the switched CTF spawn contract");
}

// Every other game and mode is outside the cause: the switch changes no placement and no map byte.
void outside_scope_unchanged(const std::string_view prompt) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false);
    const qmg::PipelineResult on = run(prompt, true);
    require(on.layout.spec.game != qmg::Game::quake3 || on.layout.spec.mode != qmg::GameMode::capture_the_flag,
            label + ": not a Quake 3 CTF prompt");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(on.layout.placements == off.layout.placements, label + ": placements unchanged");
    require(qmg::MapWriter{}.render(on.map_build->document) == qmg::MapWriter{}.render(off.map_build->document),
            label + ": map bytes unchanged");
}

// Room counts, player counts and seeds: ceil(players / 2) begin spots per team each time, all in the team's base, and
// the layout validates.
void sweep() {
    for (const int rooms : {2, 3, 4, 6, 9, 12}) {
        for (const int players : {6, 7, 10, 16, 32}) {
            for (std::uint64_t seed = 1; seed <= 2; ++seed) {
                qmg::PromptOverrides overrides;
                overrides.game = qmg::Game::quake3;
                overrides.mode = qmg::GameMode::capture_the_flag;
                overrides.room_count = rooms;
                overrides.target_players = players;
                overrides.seed = seed;
                const qmg::PipelineResult result = run("vertical capture the flag base with stairs", true, overrides);
                const std::string label = "sweep rooms " + std::to_string(rooms) + " players " +
                                          std::to_string(players) + " seed " + std::to_string(seed);
                require(result.validation.ok() && result.map_build, label + ": validates and builds");
                const auto per_team =
                    static_cast<std::size_t>(qmg::quake3_ctf_team_begin_spots_per_team(result.layout.spec));
                require(per_team * 2 >= static_cast<std::size_t>(result.layout.spec.target_players),
                        label + ": enough for every player");
                for (const qmg::Team team : {qmg::Team::red, qmg::Team::blue}) {
                    const std::vector<const qmg::Placement *> spots = begin_spots(result.layout, team);
                    require(spots.size() == per_team, label + ": ceil(players / 2) begin spots per team");
                    const qmg::Room *base = base_room(result.layout, team);
                    require(base != nullptr, label + ": a base");
                    for (std::size_t first = 0; first < spots.size(); ++first) {
                        require(base->bounds.contains(spots[first]->origin), label + ": in the base");
                        for (std::size_t second = first + 1; second < spots.size(); ++second)
                            require(separated(spots[first]->origin, spots[second]->origin),
                                    label + ": begin spots 64 units apart");
                    }
                }
            }
        }
    }
}

[[nodiscard]] qmg::Placement point(const std::uint32_t id, const qmg::PlacementRole role, const qmg::GridVec3 origin,
                                   const std::optional<qmg::Team> team = std::nullopt,
                                   const std::optional<std::int16_t> angle = std::nullopt) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    placement.team = team;
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

// Two 512 x 512 bases joined by a connection, a flag and one begin spot in each, a light at each centre.
[[nodiscard]] qmg::Layout two_bases(const qmg::Game game, const qmg::GameMode mode, const bool on) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.mode = mode;
    layout.spec.target_players = 6;
    layout.spec.title = "team begin fixture";
    layout.spec.theme = "industrial";
    layout.spec.switches = switches(on);
    layout.candidate_seed = 0x5EEDU;
    layout.rooms.push_back({qmg::RoomId{1}, "red", {{0, 0, 0}, {512, 512, 256}}, {"start"}});
    layout.rooms.push_back({qmg::RoomId{2}, "blue", {{1024, 0, 0}, {1536, 512, 256}}, {"anchor"}});
    layout.connections.push_back({qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 128, 128, {}});
    layout.placements.push_back(point(1, qmg::PlacementRole::flag, {256, 128, 24}, qmg::Team::red));
    layout.placements.push_back(point(2, qmg::PlacementRole::flag, {1280, 128, 24}, qmg::Team::blue));
    layout.placements.push_back(point(3, qmg::PlacementRole::team_initial_spawn, {96, 96, 32}, qmg::Team::red, 0));
    layout.placements.push_back(
        point(4, qmg::PlacementRole::team_initial_spawn, {1440, 416, 32}, qmg::Team::blue, 180));
    layout.placements.push_back(lamp(5, {256, 256, 192}));
    layout.placements.push_back(lamp(6, {1280, 256, 192}));
    return layout;
}

template <typename Error> [[nodiscard]] bool throws(qmg::Layout layout) {
    try {
        static_cast<void>(qmg::add_quake3_ctf_team_begin_spots(layout));
    } catch (const Error &) {
        return true;
    }
    return false;
}

// The station rule and the role's contract outside the generator.
void station_rule_and_contract() {
    // Off: nothing happens.
    qmg::Layout off = two_bases(qmg::Game::quake3, qmg::GameMode::capture_the_flag, false);
    const std::vector<qmg::Placement> before = off.placements;
    require(!qmg::add_quake3_ctf_team_begin_spots(off) && off.placements == before,
            "the switched-off path adds nothing");

    // Other games and modes are outside the cause even with the switch on.
    for (const auto &[game, mode] : {std::pair{qmg::Game::quake2, qmg::GameMode::capture_the_flag},
                                     std::pair{qmg::Game::quake1, qmg::GameMode::capture_the_flag},
                                     std::pair{qmg::Game::quake3, qmg::GameMode::deathmatch}}) {
        qmg::Layout outside = two_bases(game, mode, true);
        const std::vector<qmg::Placement> unchanged = outside.placements;
        require(!qmg::add_quake3_ctf_team_begin_spots(outside) && outside.placements == unchanged,
                "only Quake 3 CTF layouts get team-begin spots");
    }

    // On: two more spots per team (six players: three per team), the lit, clear lattice stations nearest the team's
    // existing begin spot, 64 units clear of it, the flag, the light and the spots before them, facing the base
    // centre. Red: from (96, 96) the four stations 64 units away tie and x-major order takes (32, 96), then (96, 32).
    // Blue: from (1440, 416), (1376, 416), then (1440, 352).
    qmg::Layout level = two_bases(qmg::Game::quake3, qmg::GameMode::capture_the_flag, true);
    require(qmg::add_quake3_ctf_team_begin_spots(level), "a Quake 3 CTF layout gets team-begin spots");
    require(level.placements.size() == 10, "exactly two spots per team are added");
    const std::vector<std::pair<qmg::Team, qmg::GridVec3>> expected{{qmg::Team::red, {32, 96, 24}},
                                                                    {qmg::Team::red, {96, 32, 24}},
                                                                    {qmg::Team::blue, {1376, 416, 24}},
                                                                    {qmg::Team::blue, {1440, 352, 24}}};
    const std::vector<std::int16_t> yaws{36, 54, 239, 211};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const qmg::Placement &spot = level.placements[6 + index];
        require(spot.role == qmg::PlacementRole::team_initial_spawn &&
                    spot.id == qmg::PlacementId{static_cast<std::uint32_t>(7 + index)},
                "each spot takes the next placement id");
        require(spot.team == expected[index].first && spot.origin == expected[index].second,
                "the lit stations nearest the team's begin spot, feet on the floor, red first (" +
                    origin_text(spot.origin) + ")");
        require(spot.angle_degrees && *spot.angle_degrees == yaws[index], "facing the base centre");
        for (std::size_t other = 0; other < level.placements.size(); ++other) {
            if (other != 6 + index)
                require(separated(level.placements[other].origin, spot.origin), "64 units from every other entity");
        }
    }
    const std::vector<qmg::Placement> full = level.placements;
    require(!qmg::add_quake3_ctf_team_begin_spots(level) && level.placements == full,
            "a team that already holds its spots gets no more");

    // The validator's switched contract counts the spots per team; switched off it does not count.
    qmg::Layout counted = two_bases(qmg::Game::quake3, qmg::GameMode::capture_the_flag, true);
    counted.placements.push_back(point(7, qmg::PlacementRole::team_respawn, {416, 416, 32}, qmg::Team::red, 0));
    counted.placements.push_back(point(8, qmg::PlacementRole::team_respawn, {1120, 416, 32}, qmg::Team::blue, 180));
    counted.placements.push_back(point(9, qmg::PlacementRole::deathmatch_spawn, {416, 96, 32}, std::nullopt, 0));
    require(count_issue(qmg::LayoutValidator{}.validate(counted), "ctf_spawn_contract") == 2,
            "one begin spot per team breaks the switched contract once per team");
    qmg::Layout uncounted = counted;
    uncounted.spec.switches = switches(false);
    require(count_issue(qmg::LayoutValidator{}.validate(uncounted), "ctf_spawn_contract") == 0,
            "switched off, the contract keeps its old reading");
    require(qmg::add_quake3_ctf_team_begin_spots(counted) &&
                count_issue(qmg::LayoutValidator{}.validate(counted), "ctf_spawn_contract") == 0,
            "with the spots added the contract holds");

    // The written entities: team_CTF_redplayer and team_CTF_blueplayer with an angle and an origin only. (The
    // builder needs explicit passage volumes, which the station rule does not read, so the copy written has no
    // connection.)
    qmg::Layout written = level;
    written.connections.clear();
    qmg::MapBuildOptions options;
    options.dialect = qmg::MapDialect::quake3_legacy;
    const std::string text = qmg::MapWriter{}.render(qmg::MapBuilder{}.build(written, options).document);
    require(text.find("{\n\"classname\" \"team_CTF_redplayer\"\n\"angle\" \"36\"\n\"origin\" \"32 96 24\"\n}\n") !=
                    std::string::npos &&
                text.find("{\n\"classname\" \"team_CTF_blueplayer\"\n\"angle\" \"211\"\n\"origin\" \"1440 352 "
                          "24\"\n}\n") != std::string::npos,
            "the spots are written as team_CTF_redplayer / team_CTF_blueplayer with angle and origin only");

    // A base with no clear station falls back to the joined rooms of the team's half, never to the other half. Four
    // rooms: red half = rooms 1-2, blue half = rooms 3-4. Room 1 is too small to keep a station 64 units from its flag,
    // and it is joined to room 2 (red half) and room 3 (blue half).
    qmg::Layout crowded;
    crowded.spec.game = qmg::Game::quake3;
    crowded.spec.mode = qmg::GameMode::capture_the_flag;
    crowded.spec.target_players = 6;
    crowded.spec.switches = switches(true);
    crowded.rooms.push_back({qmg::RoomId{1}, "red", {{0, 0, 0}, {128, 128, 256}}, {"start"}});
    crowded.rooms.push_back({qmg::RoomId{2}, "red-half", {{0, 128, 0}, {512, 640, 256}}, {}});
    crowded.rooms.push_back({qmg::RoomId{3}, "blue-half", {{128, 0, 0}, {640, 128, 256}}, {}});
    crowded.rooms.push_back({qmg::RoomId{4}, "blue", {{1024, 0, 0}, {1536, 512, 256}}, {"anchor"}});
    crowded.connections.push_back({qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{3}, 64, 128, {}});
    crowded.connections.push_back({qmg::ConnectionId{2}, qmg::RoomId{1}, qmg::RoomId{2}, 64, 128, {}});
    crowded.connections.push_back({qmg::ConnectionId{3}, qmg::RoomId{3}, qmg::RoomId{4}, 64, 128, {}});
    crowded.placements.push_back(point(1, qmg::PlacementRole::flag, {64, 64, 24}, qmg::Team::red));
    crowded.placements.push_back(point(2, qmg::PlacementRole::flag, {1280, 128, 24}, qmg::Team::blue));
    require(qmg::add_quake3_ctf_team_begin_spots(crowded) && crowded.placements.size() == 8,
            "three spots per team, the red ones in the joined room of the red half");
    for (std::size_t index = 2; index < 5; ++index)
        require(crowded.placements[index].team == qmg::Team::red &&
                    crowded.rooms[1].bounds.contains(crowded.placements[index].origin),
                "the red base has no clear station, so every red spot is in the joined red-half room");
    for (std::size_t index = 5; index < 8; ++index)
        require(crowded.placements[index].team == qmg::Team::blue &&
                    crowded.rooms[3].bounds.contains(crowded.placements[index].origin),
                "the blue spots are in the blue base");

    // No flag, or no clear station in the base and its half: fail closed, never a silent omission.
    qmg::Layout flagless = two_bases(qmg::Game::quake3, qmg::GameMode::capture_the_flag, true);
    flagless.placements.erase(flagless.placements.begin());
    require(throws<std::logic_error>(flagless), "a team without a flag has no base and is refused");
    qmg::Layout blocked = two_bases(qmg::Game::quake3, qmg::GameMode::capture_the_flag, true);
    blocked.connections.clear();
    blocked.solids.push_back(block(1, {{0, 0, 0}, {512, 512, 64}}));
    require(throws<std::runtime_error>(blocked), "no clear station is an error, not a silent omission");
}

} // namespace

int main() {
    try {
        registry_row();
        per_team_rule();
        for (const std::string_view prompt :
             {"Quake 3 capture the flag map, 6 rooms, seed 7", "a quake 3 ctf map",
              "Quake 3 CTF map for 16 players, 8 rooms, seed 3",
              "Quake 3 vertical capture the flag map with high platforms and stairs, 6 rooms, seed 99",
              "Quake 3 small capture the flag map, 2 rooms, seed 3", "Quake 3 capture the flag map, 5 rooms, seed 11"})
            ctf_team_begin(prompt);
        for (const std::string_view prompt :
             {"Quake 3 deathmatch arena, 6 rooms, seed 42", "Quake 3 duel map for two players, 4 rooms, seed 7",
              "Quake 3 single player campaign map, 5 rooms, seed 11", "Quake 2 capture the flag map, 6 rooms, seed 9",
              "Quake 1 capture the flag map, 6 rooms, seed 9", "Quake 2 deathmatch arena, 6 rooms, seed 42",
              "Quake 1 single-player base, 5 rooms, seed 31"})
            outside_scope_unchanged(prompt);
        sweep();
        station_rule_and_contract();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "r1s q3 ctf team begin tests passed\n";
    return 0;
}
