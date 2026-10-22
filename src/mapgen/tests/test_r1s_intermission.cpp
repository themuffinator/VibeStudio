// Change r1s-q1-intermission (plan 9.3 R1-s item 1; D8 role intermission; switch r1s.q1_intermission).
//
// Quake 1 ends every level and every deathmatch match through FindIntermission, which takes an info_intermission,
// else an info_player_start, else a testplayerstart, and otherwise calls objerror (1996 id1 progs FindIntermission
// statements 5672-5715; quake-rerelease-qc quakec/client.qc:135-168). With the switch on, every Quake 1 layout gets
// one camera: a supported standing-eye viewpoint in the largest room, clear of solids and of every other entity,
// whose "mangle" faces the room centre. Nothing else changes: the other placements, the geometry and every Quake 2
// and Quake 3 map stay as they were.
#include "qmg/layout_fingerprint.hpp"
#include "qmg/map_builder.hpp"
#include "qmg/map_writer.hpp"
#include "qmg/pipeline.hpp"
#include "qmg/report.hpp"
#include "qmg/switches.hpp"
#include "qmg/validator.hpp"

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

// The registered default: off when introduced (change r1s-q1-intermission), on since change
// r1s-q1-intermission-flip.
constexpr bool kExpectedDefaultOn = true;

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

// Change r1s-mp-player-start-flip: since r1s.mp_player_start is on by default, every Quake 1 multiplayer map also gets
// an info_player_start appended after the camera, and its station keeps clear of the camera. This test isolates its own
// cause, so every run pins r1s.mp_player_start off; tests/test_r1s_mp_player_start.cpp covers the start.
// Change r1s-sp-coop-starts-flip: likewise r1s.sp_coop_starts is on by default, so every Quake 1 single-player map
// also gets three info_player_coop appended after the camera; every run pins it off too, and
// tests/test_r1s_sp_coop_starts.cpp covers the coop starts.
const std::vector<std::string> kOtherCausesOff{"r1s.mp_player_start=off", "r1s.sp_coop_starts=off"};

[[nodiscard]] qmg::StageSwitches switches(const bool on) {
    std::vector<std::string> assignment{std::string("r1s.q1_intermission=") + (on ? "on" : "off")};
    assignment.insert(assignment.end(), kOtherCausesOff.begin(), kOtherCausesOff.end());
    return qmg::parse_stage_switch_assignments(assignment);
}

[[nodiscard]] qmg::PipelineResult run(const std::string_view prompt, const bool on) {
    qmg::PipelineOptions options;
    options.switches = switches(on);
    return qmg::GenerationPipeline{}.run(prompt, {}, options);
}

[[nodiscard]] std::vector<const qmg::Placement *> cameras(const qmg::Layout &layout) {
    std::vector<const qmg::Placement *> found;
    for (const qmg::Placement &placement : layout.placements) {
        if (placement.role == qmg::PlacementRole::intermission)
            found.push_back(&placement);
    }
    return found;
}

[[nodiscard]] std::int64_t area(const qmg::Room &room) {
    return (static_cast<std::int64_t>(room.bounds.max.x) - room.bounds.min.x) *
           (static_cast<std::int64_t>(room.bounds.max.y) - room.bounds.min.y);
}

[[nodiscard]] bool overlaps(const qmg::GridBounds &a, const qmg::GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

[[nodiscard]] std::string origin_text(const qmg::GridVec3 origin) {
    return std::to_string(origin.x) + " " + std::to_string(origin.y) + " " + std::to_string(origin.z);
}

void registry_row() {
    const auto registry = qmg::stage_switch_registry();
    const auto row = std::find_if(registry.begin(), registry.end(), [](const qmg::StageSwitchDefinition &definition) {
        return definition.name == "r1s.q1_intermission";
    });
    require(row != registry.end(), "r1s.q1_intermission is registered");
    require(row->id == qmg::StageSwitch::r1s_q1_intermission, "the typed handle names the row");
    require(row->default_on == kExpectedDefaultOn, "the registered default is the expected one");
    require(row->sub_lane == qmg::SwitchSubLane::both, "the cause touches Q1 SP and MP cases: sub-lane both");
    require(row->introduced_by == "r1s-q1-intermission", "the row records its change id");
    require(qmg::StageSwitches{}.on(qmg::StageSwitch::r1s_q1_intermission) == kExpectedDefaultOn,
            "a default set holds the registered default");
}

// The camera contract for one Quake 1 prompt, and the one-cause contract against the switched-off run.
void quake1_camera(const std::string_view prompt) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false);
    const qmg::PipelineResult on = run(prompt, true);
    require(off.layout.spec.game == qmg::Game::quake1 && on.layout.spec.game == qmg::Game::quake1,
            label + ": a Quake 1 prompt");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(on.validation.ok(), label + ": the switched layout validates");
    require(cameras(off.layout).empty(), label + ": the switched-off layout has no camera");
    const std::vector<const qmg::Placement *> found = cameras(on.layout);
    require(found.size() == 1, label + ": exactly one camera");
    const qmg::Placement &camera = *found.front();

    // One cause: the camera is the last placement and everything else is unchanged.
    require(&camera == &on.layout.placements.back(), label + ": the camera is appended last");
    std::vector<qmg::Placement> others(on.layout.placements.begin(), on.layout.placements.end() - 1);
    require(others == off.layout.placements, label + ": every other placement is unchanged");
    require(on.layout.rooms.size() == off.layout.rooms.size() && on.layout.solids == off.layout.solids &&
                on.layout.candidate_seed == off.layout.candidate_seed,
            label + ": rooms, solids and the chosen candidate are unchanged");

    // Fields.
    require(!camera.volume && !camera.team && camera.variant == 0 && !camera.light_intensity && !camera.light_color &&
                !camera.destination_map && !camera.link,
            label + ": a bare point entity");
    require(camera.angle_degrees && *camera.angle_degrees >= 0 && *camera.angle_degrees < 360,
            label + ": yaw from 0 through 359");
    require(camera.pitch_degrees && *camera.pitch_degrees >= 0 && *camera.pitch_degrees <= 89,
            label + ": pitch looks level or down");

    // In the largest room, over its floor, at or above standing eye height (24 + 22 units).
    const qmg::Room *room = nullptr;
    for (const qmg::Room &candidate : on.layout.rooms) {
        if (candidate.bounds.contains(camera.origin))
            room = &candidate;
    }
    require(room != nullptr, label + ": the camera is inside a room");
    for (const qmg::Room &other : on.layout.rooms)
        require(area(other) <= area(*room), label + ": the camera room has the largest floor area");
    require(camera.origin.z - room->bounds.min.z >= 46, label + ": at least standing eye height");

    // Not in solid, clear of every other entity.
    const qmg::GridBounds column{{camera.origin.x - 16, camera.origin.y - 16, camera.origin.z - 46},
                                 {camera.origin.x + 16, camera.origin.y + 16, camera.origin.z + 16}};
    require(column.min.x >= room->bounds.min.x && column.max.x <= room->bounds.max.x &&
                column.min.y >= room->bounds.min.y && column.max.y <= room->bounds.max.y &&
                column.min.z >= room->bounds.min.z && column.max.z <= room->bounds.max.z,
            label + ": the standing column and camera box stay inside the room");
    for (const qmg::LayoutSolid &solid : on.layout.solids)
        require(!overlaps(column, qmg::checked_brush_bounds(solid.geometry)), label + ": no solid overlaps the camera");
    for (const qmg::Placement &placement : off.layout.placements) {
        if (placement.volume) {
            require(!overlaps(column, *placement.volume), label + ": no entity volume overlaps the camera");
            continue;
        }
        require(std::abs(static_cast<std::int64_t>(placement.origin.x) - camera.origin.x) >= 64 ||
                    std::abs(static_cast<std::int64_t>(placement.origin.y) - camera.origin.y) >= 64,
                label + ": at least 64 units from every other point entity");
    }

    // Facing the room centre and looking down at the centre of its floor, to the nearest degree.
    const qmg::GridVec3 center = room->bounds.center();
    const double dx = static_cast<double>(center.x) - camera.origin.x;
    const double dy = static_cast<double>(center.y) - camera.origin.y;
    const double yaw = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
    const double yaw_error = std::remainder(yaw - *camera.angle_degrees, 360.0);
    require(std::abs(yaw_error) <= 0.5 + 1e-9, label + ": the yaw faces the room centre");
    const double pitch =
        std::atan2(static_cast<double>(camera.origin.z) - room->bounds.min.z, std::sqrt(dx * dx + dy * dy)) * 180.0 /
        std::numbers::pi;
    require(std::abs(pitch - *camera.pitch_degrees) <= 0.5 + 1e-9, label + ": the pitch looks at the floor centre");

    // The map gains exactly the info_intermission entity: classname, mangle (pitch yaw roll), origin.
    const std::string off_map = qmg::MapWriter{}.render(off.map_build->document);
    const std::string on_map = qmg::MapWriter{}.render(on.map_build->document);
    const std::string block = "{\n\"classname\" \"info_intermission\"\n\"mangle\" \"" +
                              std::to_string(*camera.pitch_degrees) + " " + std::to_string(*camera.angle_degrees) +
                              " 0\"\n\"origin\" \"" + origin_text(camera.origin) + "\"\n}\n";
    const std::size_t at = on_map.find(block);
    require(at != std::string::npos, label + ": the map has the info_intermission entity");
    require(on_map.find("info_intermission", at + block.size()) == std::string::npos, label + ": only one");
    std::string stripped = on_map;
    stripped.erase(at, block.size());
    require(stripped == off_map, label + ": the map differs only by that entity");
    require(on.map_build->stats.entity_count == off.map_build->stats.entity_count + 1, label + ": one more entity");

    // The report names the switch only where it is away from its default; the fingerprint is deterministic.
    const qmg::PipelineResult &switched = kExpectedDefaultOn ? off : on;
    const qmg::PipelineResult &defaulted = kExpectedDefaultOn ? on : off;
    const std::string value = kExpectedDefaultOn ? "off" : "on";
    // The pinned r1s.mp_player_start=off and r1s.sp_coop_starts=off are listed too, so only this switch's own entry
    // is checked.
    require(qmg::render_report(switched).find("\"r1s.q1_intermission\": \"" + value + "\"") != std::string::npos,
            label + ": the non-default run's report lists the switch");
    require(qmg::render_report(defaulted).find("\"r1s.q1_intermission\"") == std::string::npos,
            label + ": the default run's report does not list the switch");
    qmg::PipelineOptions defaults;
    defaults.switches = qmg::parse_stage_switch_assignments(kOtherCausesOff);
    require(qmg::MapWriter{}.render(qmg::GenerationPipeline{}.run(prompt, {}, defaults).map_build->document) ==
                (kExpectedDefaultOn ? on_map : off_map),
            label + ": a run with no r1s.q1_intermission assignment is the default side");
    const qmg::PipelineResult again = run(prompt, true);
    require(qmg::MapWriter{}.render(again.map_build->document) == on_map &&
                qmg::canonical_layout_sha256(again.layout) == qmg::canonical_layout_sha256(on.layout),
            label + ": deterministic");
}

// Quake 2 and Quake 3 are outside the cause: the switch changes no map byte.
void other_games_unchanged(const std::string_view prompt) {
    const std::string label(prompt);
    const qmg::PipelineResult off = run(prompt, false);
    const qmg::PipelineResult on = run(prompt, true);
    require(on.layout.spec.game != qmg::Game::quake1, label + ": not a Quake 1 prompt");
    require(off.map_build && on.map_build, label + ": both maps are built");
    require(cameras(on.layout).empty(), label + ": no camera outside Quake 1");
    require(on.layout.placements == off.layout.placements, label + ": placements unchanged");
    require(qmg::MapWriter{}.render(on.map_build->document) == qmg::MapWriter{}.render(off.map_build->document),
            label + ": map bytes unchanged");
}

[[nodiscard]] qmg::Layout one_room(const qmg::Game game) {
    qmg::Layout layout;
    layout.spec.game = game;
    layout.spec.title = "intermission fixture";
    layout.spec.theme = "industrial";
    layout.candidate_seed = 0x5EEDU;
    layout.rooms.push_back({qmg::RoomId{1}, "room", {{0, 0, 0}, {512, 512, 256}}, {}});
    return layout;
}

[[nodiscard]] qmg::Placement camera_at(const std::uint32_t id, const qmg::GridVec3 origin) {
    qmg::Placement camera;
    camera.id = qmg::PlacementId{id};
    camera.role = qmg::PlacementRole::intermission;
    camera.origin = origin;
    camera.angle_degrees = 45;
    camera.pitch_degrees = 10;
    return camera;
}

[[nodiscard]] bool has_issue(const qmg::ValidationResult &result, const std::string_view code) {
    return std::any_of(result.issues.begin(), result.issues.end(),
                       [code](const qmg::Issue &issue) { return issue.code == code; });
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

// The role's contract outside the generator: the builder and the validator fail closed.
void contract_fails_closed() {
    qmg::Layout q1 = one_room(qmg::Game::quake1);
    q1.placements.push_back(camera_at(1, {96, 96, 46}));
    qmg::MapBuildOptions options;
    options.dialect = qmg::MapDialect::quake1_valve220;
    const qmg::MapBuildResult built = qmg::MapBuilder{}.build(q1, options);
    const std::string text = qmg::MapWriter{}.render(built.document);
    require(text.find("{\n\"classname\" \"info_intermission\"\n\"mangle\" \"10 45 0\"\n\"origin\" \"96 96 46\"\n}\n") !=
                std::string::npos,
            "a Quake 1 camera is written as info_intermission with mangle and origin");
    require(text.find("\"angle\"") == std::string::npos, "the camera has no angle key");
    require(!has_issue(qmg::LayoutValidator{}.validate(q1), "invalid_intermission") &&
                !has_issue(qmg::LayoutValidator{}.validate(q1), "invalid_pitch") &&
                !has_issue(qmg::LayoutValidator{}.validate(q1), "unsupported_intermission"),
            "a valid camera raises no intermission issue");

    qmg::Layout no_pitch = q1;
    no_pitch.placements.front().pitch_degrees.reset();
    require(has_issue(qmg::LayoutValidator{}.validate(no_pitch), "invalid_intermission"), "a camera needs a pitch");
    require(build_error(no_pitch, qmg::MapDialect::quake1_valve220) == "invalid_intermission",
            "the builder refuses a camera without a pitch");

    qmg::Layout steep = q1;
    steep.placements.front().pitch_degrees = 91;
    require(has_issue(qmg::LayoutValidator{}.validate(steep), "invalid_pitch"), "pitch beyond 90 is invalid");
    require(build_error(steep, qmg::MapDialect::quake1_valve220) == "invalid_placement_pitch",
            "the builder refuses pitch beyond 90");

    qmg::Layout pitched_item = one_room(qmg::Game::quake1);
    qmg::Placement health;
    health.id = qmg::PlacementId{1};
    health.role = qmg::PlacementRole::health;
    health.origin = {96, 96, 24};
    health.pitch_degrees = 10;
    pitched_item.placements.push_back(health);
    require(has_issue(qmg::LayoutValidator{}.validate(pitched_item), "invalid_pitch"),
            "only an intermission camera carries a pitch");
    require(build_error(pitched_item, qmg::MapDialect::quake1_valve220) == "invalid_placement_pitch",
            "the builder refuses a pitch on another role");

    for (const auto &[game, dialect] : {std::pair{qmg::Game::quake2, qmg::MapDialect::quake2_valve220},
                                        std::pair{qmg::Game::quake3, qmg::MapDialect::quake3_legacy}}) {
        qmg::Layout other = one_room(game);
        other.placements.push_back(camera_at(1, {96, 96, 46}));
        require(has_issue(qmg::LayoutValidator{}.validate(other), "unsupported_intermission"),
                "Quake 2 and Quake 3 cameras are not part of this cause");
        require(build_error(other, dialect) == "unsupported_placement", "the builder refuses a non-Quake-1 camera");
    }

    // The pitch is part of the fingerprint of an intermission camera.
    qmg::Layout tilted = q1;
    tilted.placements.front().pitch_degrees = 11;
    require(qmg::canonical_layout_sha256(tilted) != qmg::canonical_layout_sha256(q1), "the pitch is fingerprinted");
}

} // namespace

int main() {
    try {
        registry_row();
        for (const std::string_view prompt :
             {"Quake 1 single-player base, 5 rooms, seed 31", "Quake 1 deathmatch arena, 6 rooms, seed 42",
              "Quake 1 duel map for two players, 4 rooms, seed 7", "Quake 1 capture the flag map, 6 rooms, seed 9",
              "Quake 1 brutal single-player level with lots of monsters, 8 rooms, seed 5",
              "Quake 1 vertical deathmatch with high platforms and stairs, 8 rooms, seed 99",
              "Quake 1 small two-room deathmatch, 2 rooms, seed 3",
              "Quake 1 gothic single player level with loops, 7 rooms, seed 1234"})
            quake1_camera(prompt);
        for (const std::string_view prompt :
             {"Quake 2 deathmatch arena, 6 rooms, seed 42", "Quake 2 single-player base, 5 rooms, seed 31",
              "Quake 3 capture the flag map, 6 rooms, seed 7", "Quake 3 tourney map, 4 rooms, seed 16"})
            other_games_unchanged(prompt);
        contract_fails_closed();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "r1s intermission tests passed\n";
    return 0;
}
