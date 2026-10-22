// Change r1s-monster-hull-support (plan 9.3 R1-s item 5, audit gap 6; switch r1s.monster_hull_support).
//
// Each monster class sets its own spawn box (Quake 1 setsize, e.g. VEC_HULL2_MIN/MAX '-32 -32 -24' '32 32 64' for the
// ogre, fiend and vore, quake-rerelease-qc quakec/monsters/ogre.qc:520; Quake 2 mins/maxs, e.g. the gladiator's
// game/m_gladiator.c:362-363), and the engine drops and clips that box, not the origin: droptofloor leaves a box that
// starts in solid where it is and walkmove(0, 0) then fails ("walkmonster in wall", quakec/monsters.qc:92-98; "in
// solid", game/g_monster.c:673-679), as it does for a box hanging over an edge (SV_CheckBottom / M_CheckBottom). The
// single-player allocator lifted a slot only when its origin lay over a solid, so at high intensity large monsters
// spawned 12-16 units inside platform brushes. With the switch on, every Quake 1 and Quake 2 single-player monster
// whose class box meets a solid, hangs over a support edge or touches a wall moves to the nearest station of its room
// that holds it, keeping its class, facing and id; every other placement and every multiplayer and Quake 3 map stay as
// they were.
#include "qmg/entities.hpp"
#include "qmg/layout_fingerprint.hpp"
#include "qmg/map_writer.hpp"
#include "qmg/pipeline.hpp"
#include "qmg/report.hpp"
#include "qmg/switches.hpp"
#include "qmg/validator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The registered default: off when introduced (change r1s-monster-hull-support), on since change
// r1s-monster-hull-support-flip.
constexpr bool kExpectedDefaultOn = true;

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::StageSwitches switches(const bool on) {
    const std::vector<std::string> assignment{std::string("r1s.monster_hull_support=") + (on ? "on" : "off")};
    return qmg::parse_stage_switch_assignments(assignment);
}

[[nodiscard]] qmg::PipelineResult run(const std::string_view prompt, const bool on,
                                      const qmg::PromptOverrides &overrides = {}) {
    qmg::PipelineOptions options;
    options.switches = switches(on);
    return qmg::GenerationPipeline{}.run(prompt, overrides, options);
}

[[nodiscard]] qmg::PromptOverrides single_player(const qmg::Game game, const int rooms, const std::uint64_t seed) {
    qmg::PromptOverrides overrides;
    overrides.game = game;
    overrides.mode = qmg::GameMode::single_player;
    overrides.room_count = rooms;
    overrides.seed = seed;
    return overrides;
}

[[nodiscard]] std::string origin_text(const qmg::GridVec3 origin) {
    return std::to_string(origin.x) + " " + std::to_string(origin.y) + " " + std::to_string(origin.z);
}

[[nodiscard]] bool has_issue(const qmg::ValidationResult &result, const std::string_view code) {
    return std::any_of(result.issues.begin(), result.issues.end(),
                       [code](const qmg::Issue &issue) { return issue.code == code; });
}

[[nodiscard]] std::size_t problems(const qmg::Layout &layout) {
    return static_cast<std::size_t>(
        std::count_if(layout.placements.begin(), layout.placements.end(), [&layout](const qmg::Placement &placement) {
            return placement.role == qmg::PlacementRole::monster && qmg::monster_hull_problem(layout, placement);
        }));
}

[[nodiscard]] std::size_t monsters(const qmg::Layout &layout) {
    return static_cast<std::size_t>(
        std::count_if(layout.placements.begin(), layout.placements.end(),
                      [](const qmg::Placement &placement) { return placement.role == qmg::PlacementRole::monster; }));
}

[[nodiscard]] bool same_rooms(const std::vector<qmg::Room> &left, const std::vector<qmg::Room> &right) {
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](const qmg::Room &a, const qmg::Room &b) {
        return a.id == b.id && a.name == b.name && a.bounds == b.bounds && a.tags == b.tags;
    });
}

void registry_row() {
    const auto registry = qmg::stage_switch_registry();
    const auto row = std::find_if(registry.begin(), registry.end(), [](const qmg::StageSwitchDefinition &definition) {
        return definition.name == "r1s.monster_hull_support";
    });
    require(row != registry.end(), "r1s.monster_hull_support is registered");
    require(row->id == qmg::StageSwitch::r1s_monster_hull_support, "the typed handle names the row");
    require(row->default_on == kExpectedDefaultOn, "the registered default is the expected one");
    require(row->sub_lane == qmg::SwitchSubLane::sp, "the cause touches single-player maps only: sub-lane sp");
    require(row->introduced_by == "r1s-monster-hull-support", "the row records its change id");
    require(qmg::StageSwitches{}.on(qmg::StageSwitch::r1s_monster_hull_support) == kExpectedDefaultOn,
            "a default set holds the registered default");
}

// The hull contract for one Quake 1 or Quake 2 single-player request, and the one-cause contract against the
// switched-off run. Returns the number of monsters the switched-off run spawns unsafely.
std::size_t single_player_hulls(const std::string_view prompt, const qmg::PromptOverrides &overrides,
                                const std::string &label) {
    const qmg::PipelineResult off = run(prompt, false, overrides);
    const qmg::PipelineResult on = run(prompt, true, overrides);
    require(off.layout.spec.game != qmg::Game::quake3 && off.layout.spec.mode == qmg::GameMode::single_player,
            label + ": a Quake 1 or Quake 2 single-player request");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(off.validation.ok() && on.validation.ok(), label + ": both layouts validate");
    require(!has_issue(on.validation, "placement_in_solid"), label + ": the switched-on validator finds no unsafe box");
    require(problems(on.layout) == 0, label + ": every switched-on monster box is clear and fully supported");

    // One cause: geometry, the chosen candidate and every placement but the monsters (and the later stations that keep
    // clear of them: the Quake 1 camera and the coop starts) are unchanged; monsters keep id, class and facing, and a
    // monster whose slot held its box keeps its origin.
    require(same_rooms(on.layout.rooms, off.layout.rooms) && on.layout.solids == off.layout.solids &&
                on.layout.connections.size() == off.layout.connections.size() &&
                on.layout.candidate_seed == off.layout.candidate_seed && on.layout.rank_score == off.layout.rank_score,
            label + ": rooms, solids, connections, the chosen candidate and its rank score are unchanged");
    require(on.layout.placements.size() == off.layout.placements.size(),
            label + ": no monster is left out in the generator's layouts");
    std::size_t moved = 0;
    for (std::size_t index = 0; index < off.layout.placements.size(); ++index) {
        const qmg::Placement &before = off.layout.placements[index];
        const qmg::Placement &after = on.layout.placements[index];
        require(before.id == after.id && before.role == after.role, label + ": ids and roles keep their order");
        if (before.role == qmg::PlacementRole::monster) {
            require(before.variant == after.variant && before.angle_degrees == after.angle_degrees,
                    label + ": a monster keeps its class and facing");
            if (qmg::monster_hull_problem(off.layout, before)) {
                require(after.origin != before.origin, label + ": an unsafe slot's monster moves");
                ++moved;
            } else {
                require(after.origin == before.origin,
                        label + ": a safe slot keeps its monster (" + origin_text(before.origin) + ")");
            }
            continue;
        }
        if (before.role == qmg::PlacementRole::intermission || before.role == qmg::PlacementRole::coop_start)
            continue;
        require(before == after, label + ": every other placement is unchanged");
    }
    require(moved == problems(off.layout), label + ": exactly the unsafe monsters move");
    if (moved == 0) {
        require(qmg::MapWriter{}.render(on.map_build->document) == qmg::MapWriter{}.render(off.map_build->document),
                label + ": with nothing to move the map bytes are unchanged");
    }

    // The report names the switch only where it is away from its default; the run is deterministic.
    const qmg::PipelineResult &switched = kExpectedDefaultOn ? off : on;
    const qmg::PipelineResult &defaulted = kExpectedDefaultOn ? on : off;
    const std::string value = kExpectedDefaultOn ? "off" : "on";
    require(qmg::render_report(switched).find("\"r1s.monster_hull_support\": \"" + value + "\"") != std::string::npos,
            label + ": the non-default run's report lists the switch");
    require(qmg::render_report(defaulted).find("r1s.monster_hull_support") == std::string::npos,
            label + ": the default run's report does not name the switch");
    qmg::PipelineOptions defaults;
    const std::string on_map = qmg::MapWriter{}.render(on.map_build->document);
    require(qmg::MapWriter{}.render(qmg::GenerationPipeline{}.run(prompt, overrides, defaults).map_build->document) ==
                qmg::MapWriter{}.render(defaulted.map_build->document),
            label + ": a run with no --switch is the default side");
    const qmg::PipelineResult again = run(prompt, true, overrides);
    require(qmg::MapWriter{}.render(again.map_build->document) == on_map &&
                qmg::canonical_layout_sha256(again.layout) == qmg::canonical_layout_sha256(on.layout),
            label + ": deterministic");
    return problems(off.layout);
}

// Multiplayer and Quake 3 maps are outside the cause: the switch changes no placement and no map byte.
void outside_scope_unchanged(const std::string_view prompt) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false);
    const qmg::PipelineResult on = run(prompt, true);
    require(on.layout.spec.game == qmg::Game::quake3 || on.layout.spec.mode != qmg::GameMode::single_player,
            label + ": a multiplayer or Quake 3 request");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(on.layout.placements == off.layout.placements, label + ": placements unchanged");
    require(qmg::MapWriter{}.render(on.map_build->document) == qmg::MapWriter{}.render(off.map_build->document),
            label + ": map bytes unchanged");
}

[[nodiscard]] qmg::Layout one_room(const qmg::Game game, const qmg::GameMode mode, const bool on) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.mode = mode;
    layout.spec.target_players = 1;
    layout.spec.title = "monster hull fixture";
    layout.spec.theme = "industrial";
    layout.spec.switches = switches(on);
    layout.candidate_seed = 0x5EEDU;
    layout.rooms.push_back({qmg::RoomId{1}, "room", {{0, 0, 0}, {512, 512, 256}}, {"start"}});
    return layout;
}

[[nodiscard]] qmg::Placement point(const std::uint32_t id, const qmg::PlacementRole role, const qmg::GridVec3 origin,
                                   const std::uint16_t variant = 0, const std::optional<std::int16_t> angle = 0) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    placement.variant = variant;
    placement.angle_degrees = angle;
    return placement;
}

[[nodiscard]] qmg::LayoutSolid platform(const std::uint32_t id, const qmg::GridBounds bounds) {
    return {qmg::SolidId{id},
            bounds,
            qmg::MaterialRole::platform,
            qmg::BrushPurpose::raised_platform,
            qmg::BrushOwnership::structural,
            qmg::BrushContents::solid,
            std::nullopt};
}

// The station rule outside the generator, on a 512 x 512 x 256 room with a 64-high platform at x 256-448, y 128-384.
// Quake 1 monster variants: 0 monster_army ('-16 -16 -24' '16 16 40'), 2 monster_ogre and 4 monster_shalrath
// ('-32 -32 -24' '32 32 64').
void station_rule() {
    const auto fixture = [](const qmg::Game game, const bool on) {
        qmg::Layout level = one_room(game, qmg::GameMode::single_player, on);
        level.solids.push_back(platform(1, {{256, 128, 0}, {448, 384, 64}}));
        level.placements.push_back(point(1, qmg::PlacementRole::player_start, {64, 64, 32}));
        // Clear on the floor: kept.
        level.placements.push_back(point(2, qmg::PlacementRole::monster, {128, 400, 32}, 0, 45));
        // On the floor, its box 16 units into the platform's west face: embedded.
        level.placements.push_back(point(3, qmg::PlacementRole::monster, {240, 256, 32}, 2, 90));
        // On the platform top, its box 16 units over the platform's east edge: hanging.
        level.placements.push_back(point(4, qmg::PlacementRole::monster, {432, 256, 96}, 4, 180));
        return level;
    };

    // Off: nothing happens, and the validator does not test boxes.
    qmg::Layout off = fixture(qmg::Game::quake1, false);
    const std::vector<qmg::Placement> before = off.placements;
    const qmg::MonsterHullSupport off_outcome = qmg::support_monster_hulls(off);
    require(!off_outcome.applied && off.placements == before, "the switched-off path changes nothing");
    require(!has_issue(qmg::LayoutValidator{}.validate(off), "placement_in_solid"),
            "the switched-off validator keeps its point test only");

    // On: the validator names both unsafe boxes; the problem reader agrees.
    qmg::Layout level = fixture(qmg::Game::quake1, true);
    require(!qmg::monster_hull_problem(level, level.placements[1]), "the clear floor slot holds its army");
    require(qmg::monster_hull_problem(level, level.placements[2]).has_value(), "the embedded ogre is unsafe");
    require(qmg::monster_hull_problem(level, level.placements[3]).has_value(), "the hanging vore is unsafe");
    require(!qmg::monster_hull_problem(level, level.placements[0]), "a non-monster placement has no box problem");
    const qmg::ValidationResult validation = qmg::LayoutValidator{}.validate(level);
    require(std::count_if(validation.issues.begin(), validation.issues.end(),
                          [](const qmg::Issue &issue) { return issue.code == "placement_in_solid"; }) == 2,
            "the switched-on validator reports the two unsafe boxes as placement_in_solid");
    require(qmg::monster_spawn_box(level, level.placements[2]) == qmg::GridBounds{{208, 224, 8}, {272, 288, 96}},
            "the ogre's box is VEC_HULL2_MIN/MAX around its origin");
    require(qmg::monster_spawn_box(level, level.placements[1]) == qmg::GridBounds{{112, 384, 8}, {144, 416, 72}},
            "the army's box is its setsize box around its origin");

    // The ogre moves 32 units west, where its box no longer touches the platform face (16 units: touching; 0: in);
    // the vore moves 32 units west on the platform top, where its whole box is strictly over the platform. Class,
    // facing and id are kept, and the army and the start do not move.
    const qmg::MonsterHullSupport outcome = qmg::support_monster_hulls(level);
    require(outcome.applied && outcome.kept == 1 && outcome.moved == 2 && outcome.left_out == 0,
            "one monster kept, two moved, none left out");
    require(level.placements.size() == 4, "no placement added or removed");
    require(level.placements[0] == before[0] && level.placements[1] == before[1], "the start and the army are kept");
    require(level.placements[2].origin == qmg::GridVec3{208, 256, 32},
            "the ogre stands on the floor clear of the platform (" + origin_text(level.placements[2].origin) + ")");
    require(level.placements[3].origin == qmg::GridVec3{400, 256, 96},
            "the vore stands fully on the platform top (" + origin_text(level.placements[3].origin) + ")");
    for (std::size_t index = 1; index < 4; ++index) {
        require(level.placements[index].id == before[index].id &&
                    level.placements[index].variant == before[index].variant &&
                    level.placements[index].angle_degrees == before[index].angle_degrees,
                "a moved monster keeps its id, class and facing");
        require(!qmg::monster_hull_problem(level, level.placements[index]), "every box is now safe");
    }
    require(!has_issue(qmg::LayoutValidator{}.validate(level), "placement_in_solid"), "the validator agrees");
    const qmg::MonsterHullSupport again = qmg::support_monster_hulls(level);
    require(again.kept == 3 && again.moved == 0, "the rule is idempotent");

    // Quake 2: the gladiator (variant 4, '-32 -32 -24' '32 32 64') moves the same way; the soldier (variant 0,
    // '-16 -16 -24' '16 16 32') fits where the army did.
    qmg::Layout quake2 = fixture(qmg::Game::quake2, true);
    const qmg::MonsterHullSupport quake2_outcome = qmg::support_monster_hulls(quake2);
    require(quake2_outcome.kept == 1 && quake2_outcome.moved == 2, "Quake 2 classes use their own boxes");
    require(quake2.placements[3].origin == qmg::GridVec3{400, 256, 96}, "the gladiator stands on the platform");
    require(qmg::monster_spawn_box(quake2, quake2.placements[1]) == qmg::GridBounds{{112, 384, 8}, {144, 416, 64}},
            "the soldier's box is its mins/maxs around its origin");

    // A room whose floor is a block too high for any box: the monster is left out, never left in solid.
    qmg::Layout full = one_room(qmg::Game::quake1, qmg::GameMode::single_player, true);
    full.solids.push_back(platform(1, {{0, 0, 0}, {512, 512, 200}}));
    full.placements.push_back(point(1, qmg::PlacementRole::player_start, {64, 64, 232}));
    full.placements.push_back(point(2, qmg::PlacementRole::monster, {256, 256, 232}, 2));
    const qmg::MonsterHullSupport full_outcome = qmg::support_monster_hulls(full);
    require(full_outcome.left_out == 1 && full.placements.size() == 1 &&
                full.placements.front().role == qmg::PlacementRole::player_start,
            "a monster with no station is left out");

    // Multiplayer and Quake 3 layouts are outside the cause even with the switch on.
    for (const auto &[game, mode] : {std::pair{qmg::Game::quake1, qmg::GameMode::deathmatch},
                                     std::pair{qmg::Game::quake2, qmg::GameMode::capture_the_flag},
                                     std::pair{qmg::Game::quake3, qmg::GameMode::single_player}}) {
        qmg::Layout outside = fixture(game == qmg::Game::quake3 ? qmg::Game::quake1 : game, true);
        outside.spec.game = game;
        outside.spec.mode = mode;
        const std::vector<qmg::Placement> unchanged = outside.placements;
        const qmg::MonsterHullSupport outside_outcome = qmg::support_monster_hulls(outside);
        require(!outside_outcome.applied && outside.placements == unchanged,
                "multiplayer and Quake 3 layouts are not touched");
    }
}

} // namespace

int main() {
    try {
        registry_row();
        station_rule();
        // The audit's brutal-intensity probes (8 rooms, seed 5, Quake 1; 32 rooms, seed 5, Quake 2): the switched-off
        // maps hold monsters whose box is unsafe, the switched-on maps none.
        require(single_player_hulls("brutal single player level", single_player(qmg::Game::quake1, 8, 5),
                                    "Q1 brutal probe") > 0,
                "the Quake 1 brutal probe reproduces the audit's embedded monsters with the switch off");
        require(single_player_hulls("brutal single player level", single_player(qmg::Game::quake2, 32, 5),
                                    "Q2 brutal probe") > 0,
                "the Quake 2 brutal probe reproduces them too");
        for (const std::string_view prompt :
             {"Quake 1 single-player base, 5 rooms, seed 31", "a quake 1 single player level",
              "Quake 1 vertical single-player base with high platforms and stairs, 6 rooms, seed 99",
              "Quake 2 single-player base, 5 rooms, seed 31",
              "Quake 2 brutal single-player outpost with many monsters, 8 rooms, seed 5",
              "Quake 2 small single-player base, 2 rooms, seed 3"}) {
            static_cast<void>(single_player_hulls(prompt, {}, std::string(prompt)));
        }
        // A sweep of room counts, seeds and intensities in both games.
        for (const qmg::Game game : {qmg::Game::quake1, qmg::Game::quake2}) {
            for (const int rooms : {2, 3, 5, 8, 12}) {
                for (std::uint64_t seed = 1; seed <= 3; ++seed) {
                    for (const std::string_view prompt :
                         {"brutal vertical single-player base with stairs", "quiet single player level"}) {
                        const std::string label = std::string(prompt) + " game " +
                                                  std::to_string(static_cast<int>(game)) + " rooms " +
                                                  std::to_string(rooms) + " seed " + std::to_string(seed);
                        const qmg::PipelineResult result = run(prompt, true, single_player(game, rooms, seed));
                        require(result.validation.ok() && result.map_build, label + ": validates and builds");
                        require(problems(result.layout) == 0, label + ": every monster box is safe");
                        require(monsters(result.layout) ==
                                    monsters(run(prompt, false, single_player(game, rooms, seed)).layout),
                                label + ": no monster is left out");
                    }
                }
            }
        }
        for (const std::string_view prompt :
             {"Quake 1 deathmatch arena, 6 rooms, seed 42", "Quake 2 deathmatch arena, 6 rooms, seed 42",
              "Quake 2 capture the flag map, 6 rooms, seed 9", "Quake 1 duel map for two players, 4 rooms, seed 7",
              "Quake 3 capture the flag map, 6 rooms, seed 7", "Quake 3 single player campaign map, 5 rooms, seed 11"})
            outside_scope_unchanged(prompt);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "r1s monster hull support tests passed\n";
    return 0;
}
