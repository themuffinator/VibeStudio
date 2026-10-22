// Change r1s-sp-pickup-hull-support (plan 9.3 R1-s item 6, audit gap 7; switch r1s.sp_pickup_hull_support).
//
// The engine drops a pickup with a box, not a point: Quake 1 items set it with setsize (item_health '0 0 0' '32 32 56',
// weapons and armor '-16 -16 0' '16 16 56'; quake-rerelease-qc quakec/items.qc:176, 321, 590) and PlaceItem raises the
// origin 6 units and calls droptofloor, removing the item when the box starts in solid ("Bonus item fell out of level",
// quakec/items.qc:72-82); Quake 2 droptofloor sets '-15 -15 -15' '15 15 15' for every item and frees it when the trace
// starts in solid, face contact included (game/g_items.c:918-946; qcommon/cmodel.c:1048-1049, 1077-1079). The
// single-player allocator lifted a slot only when its origin lay over a solid, so health boxes overlapped platforms
// (the audit's seed-5 probe: the exit-room health at (141, 1280, 24) against the platform -176..176, 1288..1512,
// 0..48). With the switch on, every Quake 1 and Quake 2 single-player pickup whose drop box meets or touches a solid,
// hangs over a support edge or does not rest on the support under it moves to the nearest station of its room that
// holds it, keeping its role, class and id; every other placement and every multiplayer and Quake 3 map stay as they
// were.
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

// The registered default: off when introduced (change r1s-sp-pickup-hull-support).
constexpr bool kExpectedDefaultOn = false;

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::StageSwitches switches(const bool on) {
    const std::vector<std::string> assignment{std::string("r1s.sp_pickup_hull_support=") + (on ? "on" : "off")};
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

[[nodiscard]] std::size_t count_issue(const qmg::ValidationResult &result, const std::string_view code) {
    return static_cast<std::size_t>(std::count_if(result.issues.begin(), result.issues.end(),
                                                  [code](const qmg::Issue &issue) { return issue.code == code; }));
}

[[nodiscard]] std::size_t problems(const qmg::Layout &layout) {
    return static_cast<std::size_t>(
        std::count_if(layout.placements.begin(), layout.placements.end(), [&layout](const qmg::Placement &placement) {
            return qmg::is_pickup_role(placement.role) && qmg::pickup_hull_problem(layout, placement);
        }));
}

[[nodiscard]] std::size_t pickups(const qmg::Layout &layout) {
    return static_cast<std::size_t>(
        std::count_if(layout.placements.begin(), layout.placements.end(),
                      [](const qmg::Placement &placement) { return qmg::is_pickup_role(placement.role); }));
}

[[nodiscard]] bool same_rooms(const std::vector<qmg::Room> &left, const std::vector<qmg::Room> &right) {
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](const qmg::Room &a, const qmg::Room &b) {
        return a.id == b.id && a.name == b.name && a.bounds == b.bounds && a.tags == b.tags;
    });
}

void registry_row() {
    const auto registry = qmg::stage_switch_registry();
    const auto row = std::find_if(registry.begin(), registry.end(), [](const qmg::StageSwitchDefinition &definition) {
        return definition.name == "r1s.sp_pickup_hull_support";
    });
    require(row != registry.end(), "r1s.sp_pickup_hull_support is registered");
    require(row->id == qmg::StageSwitch::r1s_sp_pickup_hull_support, "the typed handle names the row");
    require(row->default_on == kExpectedDefaultOn, "the registered default is the expected one");
    require(row->sub_lane == qmg::SwitchSubLane::sp, "the cause touches single-player maps only: sub-lane sp");
    require(row->introduced_by == "r1s-sp-pickup-hull-support", "the row records its change id");
    require(qmg::StageSwitches{}.on(qmg::StageSwitch::r1s_sp_pickup_hull_support) == kExpectedDefaultOn,
            "a default set holds the registered default");
    require(qmg::is_pickup_role(qmg::PlacementRole::weapon) && qmg::is_pickup_role(qmg::PlacementRole::health) &&
                qmg::is_pickup_role(qmg::PlacementRole::armor) && !qmg::is_pickup_role(qmg::PlacementRole::monster) &&
                !qmg::is_pickup_role(qmg::PlacementRole::flag) &&
                !qmg::is_pickup_role(qmg::PlacementRole::player_start),
            "weapons, health and armor are the pickups");
}

// The pickup contract for one Quake 1 or Quake 2 single-player request, and the one-cause contract against the
// switched-off run. Returns the number of pickups the switched-off run places unsafely.
std::size_t single_player_pickups(const std::string_view prompt, const qmg::PromptOverrides &overrides,
                                  const std::string &label) {
    const qmg::PipelineResult off = run(prompt, false, overrides);
    const qmg::PipelineResult on = run(prompt, true, overrides);
    require(off.layout.spec.game != qmg::Game::quake3 && off.layout.spec.mode == qmg::GameMode::single_player,
            label + ": a Quake 1 or Quake 2 single-player request");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(off.validation.ok() && on.validation.ok(), label + ": both layouts validate");
    require(!has_issue(on.validation, "placement_in_solid"), label + ": the switched-on validator finds no unsafe box");
    require(problems(on.layout) == 0, label + ": every switched-on pickup box is clear and rests on support");

    // One cause: geometry, the chosen candidate and every placement but the pickups (and the later stations that keep
    // clear of them: the Quake 1 camera and the coop starts) are unchanged; pickups keep id, role and class, and a
    // pickup whose slot held its box keeps its origin.
    require(same_rooms(on.layout.rooms, off.layout.rooms) && on.layout.solids == off.layout.solids &&
                on.layout.connections.size() == off.layout.connections.size() &&
                on.layout.candidate_seed == off.layout.candidate_seed && on.layout.rank_score == off.layout.rank_score,
            label + ": rooms, solids, connections, the chosen candidate and its rank score are unchanged");
    require(on.layout.placements.size() == off.layout.placements.size(),
            label + ": no pickup is left out in the generator's layouts");
    std::size_t moved = 0;
    for (std::size_t index = 0; index < off.layout.placements.size(); ++index) {
        const qmg::Placement &before = off.layout.placements[index];
        const qmg::Placement &after = on.layout.placements[index];
        require(before.id == after.id && before.role == after.role, label + ": ids and roles keep their order");
        if (qmg::is_pickup_role(before.role)) {
            require(before.variant == after.variant && before.angle_degrees == after.angle_degrees,
                    label + ": a pickup keeps its class");
            if (qmg::pickup_hull_problem(off.layout, before)) {
                require(after.origin != before.origin, label + ": an unsafe slot's pickup moves");
                ++moved;
            } else {
                require(after.origin == before.origin,
                        label + ": a safe slot keeps its pickup (" + origin_text(before.origin) + ")");
            }
            continue;
        }
        if (before.role == qmg::PlacementRole::intermission || before.role == qmg::PlacementRole::coop_start)
            continue;
        require(before == after, label + ": every other placement is unchanged");
    }
    require(moved == problems(off.layout), label + ": exactly the unsafe pickups move");
    if (moved == 0) {
        require(qmg::MapWriter{}.render(on.map_build->document) == qmg::MapWriter{}.render(off.map_build->document),
                label + ": with nothing to move the map bytes are unchanged");
    }

    // The report names the switch only where it is away from its default; the run is deterministic.
    const qmg::PipelineResult &switched = kExpectedDefaultOn ? off : on;
    const qmg::PipelineResult &defaulted = kExpectedDefaultOn ? on : off;
    const std::string value = kExpectedDefaultOn ? "off" : "on";
    require(qmg::render_report(switched).find("\"r1s.sp_pickup_hull_support\": \"" + value + "\"") != std::string::npos,
            label + ": the non-default run's report lists the switch");
    require(qmg::render_report(defaulted).find("r1s.sp_pickup_hull_support") == std::string::npos,
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
    layout.spec.title = "pickup hull fixture";
    layout.spec.theme = "industrial";
    layout.spec.switches = switches(on);
    layout.candidate_seed = 0x5EEDU;
    layout.rooms.push_back({qmg::RoomId{1}, "room", {{0, 0, 0}, {512, 512, 256}}, {"start"}});
    return layout;
}

[[nodiscard]] qmg::Placement point(const std::uint32_t id, const qmg::PlacementRole role, const qmg::GridVec3 origin,
                                   const std::uint16_t variant = 0,
                                   const std::optional<std::int16_t> angle = std::nullopt) {
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
// Quake 1 boxes: item_health '0 0 0' '32 32 56' (origin at the minimum corner), weapon_nailgun (variant 0)
// '-16 -16 0' '16 16 56', raised 6 units before the drop; Quake 2 boxes: '-15 -15 -15' '15 15 15' for both.
void station_rule() {
    const auto fixture = [](const qmg::Game game, const bool on) {
        qmg::Layout level = one_room(game, qmg::GameMode::single_player, on);
        level.solids.push_back(platform(1, {{256, 128, 0}, {448, 384, 64}}));
        level.placements.push_back(point(1, qmg::PlacementRole::player_start, {64, 64, 32}, 0, 0));
        // Clear on the floor: kept.
        level.placements.push_back(point(2, qmg::PlacementRole::health, {96, 400, 24}));
        // On the floor, its box into the platform's west face (Quake 1 x 244..276, Quake 2 x 229..259): embedded.
        level.placements.push_back(point(3, qmg::PlacementRole::health, {244, 240, 24}));
        // On the platform top, its box over the platform's east edge (x 424..456 / 425..455 past 448): hanging.
        level.placements.push_back(point(4, qmg::PlacementRole::weapon, {440, 256, 88}));
        // Over the bare floor but 16 units too high: not resting on the support under it.
        level.placements.push_back(point(5, qmg::PlacementRole::health, {96, 200, 40}));
        return level;
    };

    // Off: nothing happens, and the validator does not test pickup boxes.
    qmg::Layout off = fixture(qmg::Game::quake1, false);
    const std::vector<qmg::Placement> before = off.placements;
    const qmg::PickupHullSupport off_outcome = qmg::support_pickup_hulls(off);
    require(!off_outcome.applied && off.placements == before, "the switched-off path changes nothing");
    require(!has_issue(qmg::LayoutValidator{}.validate(off), "placement_in_solid"),
            "the switched-off validator keeps its point test only");

    // On: the validator names the three unsafe boxes; the problem reader agrees.
    qmg::Layout level = fixture(qmg::Game::quake1, true);
    require(!qmg::pickup_hull_problem(level, level.placements[1]), "the clear floor slot holds its health");
    require(qmg::pickup_hull_problem(level, level.placements[2]).has_value(), "the embedded health is unsafe");
    require(qmg::pickup_hull_problem(level, level.placements[3]).has_value(), "the hanging weapon is unsafe");
    require(qmg::pickup_hull_problem(level, level.placements[4]).has_value(), "the floating health is unsafe");
    require(!qmg::pickup_hull_problem(level, level.placements[0]), "a non-pickup placement has no box problem");
    require(count_issue(qmg::LayoutValidator{}.validate(level), "placement_in_solid") == 3,
            "the switched-on validator reports the three unsafe boxes as placement_in_solid");
    require(qmg::pickup_drop_box(level, level.placements[1]) == qmg::GridBounds{{96, 400, 24}, {128, 432, 80}},
            "the Quake 1 health box is setsize '0 0 0' '32 32 56' from its origin");
    require(qmg::pickup_drop_box(level, level.placements[3]) == qmg::GridBounds{{424, 240, 88}, {456, 272, 144}},
            "the Quake 1 weapon box is setsize '-16 -16 0' '16 16 56' around its origin");

    // The embedded health moves onto the platform (16 units east; 16 units west its box still meets the platform face),
    // the hanging weapon 16 units west, where its whole box is strictly over the platform, and the floating health down
    // to rest on the floor. Role, class and id are kept, and the clear health and the start do not move.
    const qmg::PickupHullSupport outcome = qmg::support_pickup_hulls(level);
    require(outcome.applied && outcome.kept == 1 && outcome.moved == 3 && outcome.left_out == 0,
            "one pickup kept, three moved, none left out");
    require(level.placements.size() == 5, "no placement added or removed");
    require(level.placements[0] == before[0] && level.placements[1] == before[1],
            "the start and the clear health stay");
    require(level.placements[2].origin == qmg::GridVec3{260, 240, 88},
            "the embedded health rests on the platform top (" + origin_text(level.placements[2].origin) + ")");
    require(level.placements[3].origin == qmg::GridVec3{424, 256, 88},
            "the weapon stands fully on the platform top (" + origin_text(level.placements[3].origin) + ")");
    require(level.placements[4].origin == qmg::GridVec3{96, 200, 24},
            "the floating health rests on the floor (" + origin_text(level.placements[4].origin) + ")");
    for (std::size_t index = 1; index < 5; ++index) {
        require(level.placements[index].id == before[index].id && level.placements[index].role == before[index].role &&
                    level.placements[index].variant == before[index].variant,
                "a moved pickup keeps its id, role and class");
        require(!qmg::pickup_hull_problem(level, level.placements[index]), "every box is now safe");
    }
    require(!has_issue(qmg::LayoutValidator{}.validate(level), "placement_in_solid"), "the validator agrees");
    const qmg::PickupHullSupport again = qmg::support_pickup_hulls(level);
    require(again.kept == 4 && again.moved == 0, "the rule is idempotent");

    // Quake 2: the 30-wide centred box. The embedded health (x 229..259) moves 16 units west to the floor (x 213..243,
    // one unit clear of the platform face); the hanging weapon (x 425..455) moves 16 units west; the floating health
    // moves down.
    qmg::Layout quake2 = fixture(qmg::Game::quake2, true);
    require(qmg::pickup_drop_box(quake2, quake2.placements[1]) == qmg::GridBounds{{81, 385, 9}, {111, 415, 39}},
            "the Quake 2 box is droptofloor's '-15 -15 -15' '15 15 15'");
    const qmg::PickupHullSupport quake2_outcome = qmg::support_pickup_hulls(quake2);
    require(quake2_outcome.kept == 1 && quake2_outcome.moved == 3 && quake2_outcome.left_out == 0,
            "Quake 2 pickups use their own box");
    require(quake2.placements[2].origin == qmg::GridVec3{228, 240, 24},
            "the Quake 2 health rests on the floor clear of the platform (" + origin_text(quake2.placements[2].origin) +
                ")");
    require(quake2.placements[3].origin == qmg::GridVec3{424, 256, 88}, "the Quake 2 weapon stands on the platform");
    require(quake2.placements[4].origin == qmg::GridVec3{96, 200, 24}, "the Quake 2 health rests on the floor");

    // Quake 2 counts face contact as solid: a box exactly against the platform face is unsafe, one unit off it is not.
    qmg::Layout contact = one_room(qmg::Game::quake2, qmg::GameMode::single_player, true);
    contact.solids.push_back(platform(1, {{256, 128, 0}, {448, 384, 64}}));
    contact.placements.push_back(point(1, qmg::PlacementRole::health, {241, 240, 24}));
    contact.placements.push_back(point(2, qmg::PlacementRole::health, {240, 300, 24}));
    require(qmg::pickup_hull_problem(contact, contact.placements[0]).has_value(), "touching the face is unsafe");
    require(!qmg::pickup_hull_problem(contact, contact.placements[1]), "one unit off the face is safe");

    // A room whose floor is a block too high for the Quake 1 box to spawn under the ceiling: the pickup is left out,
    // never left to be removed by droptofloor.
    qmg::Layout full = one_room(qmg::Game::quake1, qmg::GameMode::single_player, true);
    full.solids.push_back(platform(1, {{0, 0, 0}, {512, 512, 200}}));
    full.placements.push_back(point(1, qmg::PlacementRole::player_start, {64, 64, 232}, 0, 0));
    full.placements.push_back(point(2, qmg::PlacementRole::health, {256, 256, 224}));
    const qmg::PickupHullSupport full_outcome = qmg::support_pickup_hulls(full);
    require(full_outcome.left_out == 1 && full.placements.size() == 1 &&
                full.placements.front().role == qmg::PlacementRole::player_start,
            "a pickup with no station is left out");

    // Multiplayer and Quake 3 layouts are outside the cause even with the switch on.
    for (const auto &[game, mode] : {std::pair{qmg::Game::quake1, qmg::GameMode::deathmatch},
                                     std::pair{qmg::Game::quake2, qmg::GameMode::capture_the_flag},
                                     std::pair{qmg::Game::quake3, qmg::GameMode::single_player}}) {
        qmg::Layout outside = fixture(game == qmg::Game::quake3 ? qmg::Game::quake1 : game, true);
        outside.spec.game = game;
        outside.spec.mode = mode;
        const std::vector<qmg::Placement> unchanged = outside.placements;
        const qmg::PickupHullSupport outside_outcome = qmg::support_pickup_hulls(outside);
        require(!outside_outcome.applied && outside.placements == unchanged,
                "multiplayer and Quake 3 layouts are not touched");
    }
}

// The audit's seed-5 probe: the exit-room health at (141, 1280, 24) overlaps the platform; with the switch on it moves.
void seed5_probe(const qmg::Game game, const std::string_view prompt, const std::string &label) {
    const qmg::PipelineResult off = run(prompt, false, single_player(game, 8, 5));
    const qmg::PipelineResult on = run(prompt, true, single_player(game, 8, 5));
    const auto at_audit_slot = [](const qmg::Placement &placement) {
        return placement.role == qmg::PlacementRole::health && placement.origin == qmg::GridVec3{141, 1280, 24};
    };
    const auto found = std::find_if(off.layout.placements.begin(), off.layout.placements.end(), at_audit_slot);
    require(found != off.layout.placements.end(), label + ": the switched-off map holds the audit's health slot");
    require(qmg::pickup_hull_problem(off.layout, *found).has_value(), label + ": its box is unsafe");
    require(std::none_of(on.layout.placements.begin(), on.layout.placements.end(), at_audit_slot),
            label + ": the switched-on map moves it");
    require(single_player_pickups(prompt, single_player(game, 8, 5), label) > 0,
            label + ": the one-cause contract holds and the switched-off map has unsafe pickups");
}

} // namespace

int main() {
    try {
        registry_row();
        station_rule();
        seed5_probe(qmg::Game::quake1, "quake 1 single player gothic level", "Q1 seed-5 probe");
        seed5_probe(qmg::Game::quake2, "quake 2 single player base level", "Q2 seed-5 probe");
        // The audit's brutal-intensity probes (8 rooms, seed 5, Quake 1; 32 rooms, seed 5, Quake 2).
        require(single_player_pickups("brutal single player level", single_player(qmg::Game::quake1, 8, 5),
                                      "Q1 brutal probe") > 0,
                "the Quake 1 brutal probe reproduces unsafe pickups with the switch off");
        require(single_player_pickups("brutal single player level", single_player(qmg::Game::quake2, 32, 5),
                                      "Q2 brutal probe") > 0,
                "the Quake 2 brutal probe reproduces them too");
        for (const std::string_view prompt :
             {"Quake 1 single-player base, 5 rooms, seed 31", "a quake 1 single player level",
              "Quake 1 vertical single-player base with high platforms and stairs, 6 rooms, seed 99",
              "Quake 2 single-player base, 5 rooms, seed 31",
              "Quake 2 brutal single-player outpost with many monsters, 8 rooms, seed 5",
              "Quake 2 small single-player base, 2 rooms, seed 3"}) {
            static_cast<void>(single_player_pickups(prompt, {}, std::string(prompt)));
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
                        require(problems(result.layout) == 0, label + ": every pickup box is safe");
                        require(pickups(result.layout) ==
                                    pickups(run(prompt, false, single_player(game, rooms, seed)).layout),
                                label + ": no pickup is left out");
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
    std::cout << "r1s sp pickup hull support tests passed\n";
    return 0;
}
