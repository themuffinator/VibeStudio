#include "qmg/entities.hpp"
#include "qmg/generated/game_profile_tables.hpp"
#include "qmg/switches.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qmg {
namespace {

struct Fraction {
    int numerator{};
    int denominator{100};
};

constexpr Fraction percent(const int value) { return {value, 100}; }

GridCoord interpolate(const GridCoord minimum, const GridCoord maximum, const Fraction fraction) {
    if (fraction.denominator <= 0 || fraction.numerator < 0 || fraction.numerator > fraction.denominator)
        throw std::invalid_argument("layout interpolation fraction is invalid");
    const std::int64_t span = static_cast<std::int64_t>(maximum) - minimum;
    if (span < 0)
        throw std::invalid_argument("layout interpolation bounds are inverted");

    const std::int64_t denominator = fraction.denominator;
    const std::int64_t scaled_coordinate =
        static_cast<std::int64_t>(minimum) * denominator + span * static_cast<std::int64_t>(fraction.numerator);
    std::int64_t rounded = scaled_coordinate / denominator;
    const std::int64_t remainder = scaled_coordinate % denominator;
    if (std::abs(remainder) * 2 >= denominator)
        rounded += scaled_coordinate < 0 ? -1 : 1;
    return checked_grid_coord(rounded);
}

GridBounds solid_bounds(const LayoutSolid &solid) { return checked_brush_bounds(solid.geometry); }

GridVec3 floor_point(const Room &room, const Fraction x_fraction, const Fraction y_fraction,
                     const GridCoord z_offset = 32) {
    return {
        interpolate(room.bounds.min.x, room.bounds.max.x, x_fraction),
        interpolate(room.bounds.min.y, room.bounds.max.y, y_fraction),
        checked_grid_add(room.bounds.min.z, z_offset),
    };
}

GridVec3 supported_point(const Layout &level, const Room &room, const Fraction x_fraction, const Fraction y_fraction,
                         const GridCoord z_offset = 32) {
    GridVec3 result = floor_point(room, x_fraction, y_fraction, z_offset);
    for (const LayoutSolid &solid : level.solids) {
        const GridBounds bounds = solid_bounds(solid);
        if (!room.bounds.contains(bounds.center()))
            continue;
        if (result.x >= bounds.min.x && result.x <= bounds.max.x && result.y >= bounds.min.y &&
            result.y <= bounds.max.y) {
            result.z = std::max(result.z, checked_grid_add(bounds.max.z, z_offset));
        }
    }
    return result;
}

void add_point_placement(Layout &level, const PlacementRole role, const GridVec3 origin,
                         const std::uint16_t variant = 0, const std::optional<Team> team = std::nullopt,
                         const std::optional<std::int16_t> angle = std::nullopt) {
    Placement placement;
    placement.id = next_placement_id(level);
    placement.role = role;
    placement.origin = origin;
    placement.team = team;
    placement.variant = variant;
    placement.angle_degrees = angle;
    level.placements.push_back(std::move(placement));
}

// Variant counts follow the weapon and monster lists in data/profiles/<game>/generator-current.json.
constexpr std::uint16_t variant_count(const Game game, const PlacementRole role) {
    const std::optional<game_profile::VariantTable> table = game_profile::variant_table(game, role);
    return table ? static_cast<std::uint16_t>(table->classnames.size()) : 0;
}

constexpr std::uint16_t weapon_variant_count(const Game game) { return variant_count(game, PlacementRole::weapon); }

constexpr std::uint16_t monster_variant_count(const Game game) { return variant_count(game, PlacementRole::monster); }

const LayoutSolid *platform_in_room(const Layout &level, const Room &room) {
    const auto found = std::find_if(level.solids.begin(), level.solids.end(), [&room](const LayoutSolid &solid) {
        return solid.purpose == BrushPurpose::raised_platform && room.bounds.contains(solid_bounds(solid).center());
    });
    return found == level.solids.end() ? nullptr : &*found;
}

bool has_tag(const Room &room, const std::string_view tag) {
    return std::find(room.tags.begin(), room.tags.end(), tag) != room.tags.end();
}

GridVec3 featured_pickup_point(const Layout &level, const Room &room, const GridCoord x_offset = 0) {
    if (const LayoutSolid *platform = platform_in_room(level, room)) {
        const GridBounds bounds = solid_bounds(*platform);
        const GridVec3 center = bounds.center();
        return {checked_grid_add(center.x, x_offset), center.y, checked_grid_add(bounds.max.z, 24)};
    }
    return floor_point(room, percent(50), percent(50), 24);
}

GridVec3 allocate_player_spawn(const Layout &level, const Room &room) {
    constexpr std::array<int, 8> percentages{10, 21, 32, 43, 57, 68, 79, 90};
    GridVec3 best = supported_point(level, room, percent(percentages.front()), percent(percentages.front()));
    long double best_separation = -1.0L;
    for (const int x_percent : percentages) {
        for (const int y_percent : percentages) {
            const GridVec3 candidate = supported_point(level, room, percent(x_percent), percent(y_percent));
            long double nearest = std::numeric_limits<long double>::max();
            for (const Placement &placement : level.placements) {
                if (placement.volume || placement.role == PlacementRole::light ||
                    !room.bounds.contains(placement.origin)) {
                    continue;
                }
                const std::int64_t dx = static_cast<std::int64_t>(candidate.x) - placement.origin.x;
                const std::int64_t dy = static_cast<std::int64_t>(candidate.y) - placement.origin.y;
                const long double distance = static_cast<long double>(dx) * dx + static_cast<long double>(dy) * dy;
                nearest = std::min(nearest, distance);
            }
            if (nearest > best_separation) {
                best_separation = nearest;
                best = candidate;
            }
        }
    }
    return best;
}

void add_player_spawn(Layout &level, const PlacementRole role, const Room &room, const int angle,
                      const std::optional<Team> team = std::nullopt) {
    add_point_placement(level, role, allocate_player_spawn(level, room), 0, team,
                        static_cast<std::int16_t>(angle % 360));
}

// Room light presets come from data/profiles/<game>/generator-current.json.
void add_lighting(Layout &level) {
    const std::optional<game_profile::LightPreset> preset =
        game_profile::light_preset(level.spec.game, level.lighting_style);
    if (!preset)
        throw std::invalid_argument("layout has no light preset for its game and lighting style");
    for (const Room &room : level.rooms) {
        const GridVec3 center = room.bounds.center();
        Placement light;
        light.id = next_placement_id(level);
        light.role = PlacementRole::light;
        light.origin = {center.x, center.y, checked_grid_subtract(room.bounds.max.z, 64)};
        light.light_intensity = preset->brightness;
        light.light_color = room.id.value % 2 == 1 ? preset->odd_room_color : preset->even_room_color;
        level.placements.push_back(std::move(light));
    }
}

void add_arena_spawns(Layout &level) {
    const int spawn_count = std::max(level.spec.target_players, 2);
    if (level.spec.mode == GameMode::capture_the_flag && level.spec.game != Game::quake1) {
        const int red_count = (spawn_count + 1) / 2;
        const int blue_count = spawn_count / 2;
        const std::size_t team_span = std::max<std::size_t>(1, level.rooms.size() / 2);
        const PlacementRole team_role =
            level.spec.game == Game::quake3 ? PlacementRole::team_respawn : PlacementRole::team_initial_spawn;
        for (int index = 0; index < red_count; ++index) {
            const Room &room = level.rooms[static_cast<std::size_t>(index) % team_span];
            add_player_spawn(level, team_role, room, index * 45, Team::red);
        }
        for (int index = 0; index < blue_count; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) % team_span;
            const Room &room = level.rooms[level.rooms.size() - 1 - offset];
            add_player_spawn(level, team_role, room, 180 + index * 45, Team::blue);
        }

        if (level.spec.game == Game::quake3) {
            add_player_spawn(level, PlacementRole::team_initial_spawn, level.rooms.front(), 0, Team::red);
            add_player_spawn(level, PlacementRole::team_initial_spawn, level.rooms.back(), 180, Team::blue);
            add_player_spawn(level, PlacementRole::deathmatch_spawn, level.rooms.front(), 0);
            add_player_spawn(level, PlacementRole::deathmatch_spawn, level.rooms.back(), 180);
        } else {
            for (int index = 0; index < spawn_count; ++index) {
                const Room &room = level.rooms[static_cast<std::size_t>(index) % level.rooms.size()];
                add_player_spawn(level, PlacementRole::deathmatch_spawn, room, index * 90);
            }
        }
    } else {
        for (int index = 0; index < spawn_count; ++index) {
            const Room &room = level.rooms[static_cast<std::size_t>(index) % level.rooms.size()];
            add_player_spawn(level, PlacementRole::deathmatch_spawn, room, index * 90);
        }
    }
}

void populate_arena(Layout &level) {
    const std::uint16_t weapon_count = weapon_variant_count(level.spec.game);
    for (std::size_t index = 0; index < level.rooms.size(); ++index) {
        const Room &room = level.rooms[index];
        add_point_placement(level, PlacementRole::weapon, featured_pickup_point(level, room, 64),
                            static_cast<std::uint16_t>(index % weapon_count));
        add_point_placement(level, PlacementRole::health,
                            supported_point(level, room, index % 2 == 0 ? percent(30) : percent(70), percent(25), 24));
        if (platform_in_room(level, room) != nullptr) {
            add_point_placement(level, PlacementRole::armor, featured_pickup_point(level, room, -96));
        }
        if (has_tag(room, "control") && platform_in_room(level, room) == nullptr) {
            add_point_placement(level, PlacementRole::armor,
                                supported_point(level, room, percent(78), percent(78), 24));
        }
        if (has_tag(room, "resource")) {
            add_point_placement(level, PlacementRole::health,
                                supported_point(level, room, percent(20), percent(76), 24));
        }
    }

    if (level.spec.mode == GameMode::capture_the_flag && level.rooms.size() >= 2) {
        add_point_placement(level, PlacementRole::flag,
                            supported_point(level, level.rooms.front(), percent(50), percent(25), 24), 0, Team::red);
        add_point_placement(level, PlacementRole::flag,
                            supported_point(level, level.rooms.back(), percent(50), percent(25), 24), 0, Team::blue);
    }
    add_arena_spawns(level);
}

void populate_single_player(Layout &level) {
    const Room &start = level.rooms.front();
    add_point_placement(level, PlacementRole::player_start, supported_point(level, start, percent(35), percent(50)), 0,
                        std::nullopt, 0);

    const std::uint16_t monster_count = monster_variant_count(level.spec.game);
    const std::uint16_t weapon_count = weapon_variant_count(level.spec.game);
    const int per_room = std::clamp(static_cast<int>(std::round(level.planned_intensity * 4.0)), 1, 4);

    for (std::size_t index = 1; index < level.rooms.size(); ++index) {
        const Room &room = level.rooms[index];
        for (int monster = 0; monster < per_room; ++monster) {
            const Fraction x = monster % 2 == 0 ? percent(25) : percent(75);
            const Fraction y = monster < 2 ? percent(32) : percent(68);
            add_point_placement(level, PlacementRole::monster, supported_point(level, room, x, y),
                                static_cast<std::uint16_t>((index + static_cast<std::size_t>(monster)) % monster_count),
                                std::nullopt,
                                static_cast<std::int16_t>((static_cast<int>(index) * 73 + monster * 91) % 360));
        }
        if (has_tag(room, "setpiece")) {
            add_point_placement(
                level, PlacementRole::monster, supported_point(level, room, percent(50), percent(82)),
                static_cast<std::uint16_t>((index + static_cast<std::size_t>(per_room)) % monster_count), std::nullopt,
                static_cast<std::int16_t>((static_cast<int>(index) * 53 + 180) % 360));
        }
        if (index % 2 == 1) {
            add_point_placement(level, PlacementRole::weapon, featured_pickup_point(level, room),
                                static_cast<std::uint16_t>(index % weapon_count));
        }
        add_point_placement(level, PlacementRole::health, supported_point(level, room, percent(68), percent(50), 24));
        if (has_tag(room, "resource")) {
            add_point_placement(level, PlacementRole::health,
                                supported_point(level, room, percent(20), percent(76), 24));
        }
    }

    const Room &exit_room = level.rooms.back();
    const GridVec3 exit_center = exit_room.bounds.center();
    GridBounds trigger{{checked_grid_subtract(exit_room.bounds.max.x, 112), checked_grid_subtract(exit_center.y, 96),
                        exit_room.bounds.min.z},
                       {checked_grid_subtract(exit_room.bounds.max.x, 48), checked_grid_add(exit_center.y, 96),
                        checked_grid_add(exit_room.bounds.min.z, 128)}};
    const auto entrance =
        std::find_if(level.connections.begin(), level.connections.end(), [&exit_room](const Connection &connection) {
            return connection.from_room == exit_room.id || connection.to_room == exit_room.id;
        });
    if (entrance != level.connections.end()) {
        const RoomId other_id = entrance->from_room == exit_room.id ? entrance->to_room : entrance->from_room;
        const Room *other_room = find_room(level, other_id);
        if (other_room == nullptr)
            throw std::logic_error("single-player exit connection references an unknown room");
        const GridVec3 other = other_room->bounds.center();
        const std::int64_t delta_x = static_cast<std::int64_t>(other.x) - exit_center.x;
        const std::int64_t delta_y = static_cast<std::int64_t>(other.y) - exit_center.y;
        if (std::abs(delta_x) >= std::abs(delta_y)) {
            if (other.x > exit_center.x) {
                trigger = {{checked_grid_add(exit_room.bounds.min.x, 48), checked_grid_subtract(exit_center.y, 96),
                            exit_room.bounds.min.z},
                           {checked_grid_add(exit_room.bounds.min.x, 112), checked_grid_add(exit_center.y, 96),
                            checked_grid_add(exit_room.bounds.min.z, 128)}};
            }
        } else if (other.y < exit_center.y) {
            trigger = {{checked_grid_subtract(exit_center.x, 96), checked_grid_subtract(exit_room.bounds.max.y, 112),
                        exit_room.bounds.min.z},
                       {checked_grid_add(exit_center.x, 96), checked_grid_subtract(exit_room.bounds.max.y, 48),
                        checked_grid_add(exit_room.bounds.min.z, 128)}};
        } else {
            trigger = {{checked_grid_subtract(exit_center.x, 96), checked_grid_add(exit_room.bounds.min.y, 48),
                        exit_room.bounds.min.z},
                       {checked_grid_add(exit_center.x, 96), checked_grid_add(exit_room.bounds.min.y, 112),
                        checked_grid_add(exit_room.bounds.min.z, 128)}};
        }
    }

    Placement exit_trigger;
    exit_trigger.id = next_placement_id(level);
    exit_trigger.role = PlacementRole::exit_trigger;
    exit_trigger.origin = trigger.center();
    exit_trigger.volume = trigger;
    if (level.spec.game == Game::quake1) {
        exit_trigger.destination_map = "start";
        level.placements.push_back(std::move(exit_trigger));
    } else {
        const std::size_t trigger_index = level.placements.size();
        level.placements.push_back(std::move(exit_trigger));

        Placement destination;
        destination.id = next_placement_id(level);
        destination.role = PlacementRole::exit_destination;
        destination.origin = supported_point(level, exit_room, percent(50), percent(88));
        destination.destination_map = "base1";
        level.placements[trigger_index].link = destination.id;
        level.placements.push_back(std::move(destination));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// R1-s cause r1s-q1-intermission (switch r1s.q1_intermission; docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.3, D8).
//
// Every Quake 1 map needs an intermission camera. When a level or match ends (trigger_changelevel, or fraglimit
// or timelimit in deathmatch), execute_changelevel calls FindIntermission (1996 id1 progs.dat: execute_changelevel
// statement 5878; re-release quakec/client.qc:307). FindIntermission returns a random one of up to four
// info_intermission entities, else the first info_player_start, else testplayerstart, and otherwise calls
// objerror ("FindIntermission: no spot"), a fatal "Program error" (progs1996 FindIntermission statements
// 5672-5715; quakec/client.qc:135-168; quakec_ctf/client.qc:191-221). A generated deathmatch, duel or CTF map has
// no info_player_start, so without an info_intermission its match end is fatal. execute_changelevel then moves
// every client to the spot: view_ofs '0 0 0', angles = v_angle = the spot's "mangle", setorigin to the spot's
// origin (progs1996 execute_changelevel statements 5887-5893 and 5907-5910; quakec/client.qc:313-321). The camera
// is therefore exactly the entity origin, looking along "mangle" (pitch yaw roll; a positive pitch looks down).

// A standing player's eye above the floor: the player hull's mins z is -24 (setsize VEC_HULL_MIN '-16 -16 -24',
// progs1996 PutClientInServer 6149-6152; quakec/defs.qc:351, quakec/client.qc:794) and view_ofs is '0 0 22'
// (progs1996 PutClientInServer 6153-6154; quakec/client.qc:796).
constexpr GridCoord kQuake1StandingEyeHeight = 24 + 22;
// The camera's clearance box half-extent: the editor box of info_intermission is (-16 -16 -16) (16 16 16)
// (quakec/client.qc:41, quakec_ctf/client.qc:77).
constexpr GridCoord kIntermissionHalfExtent = 16;
// Minimum horizontal separation (on x or y) from every other point entity: two 32-wide boxes 32 units apart, the
// spawn-legality separation of plan 9.3 (>= 64 u for 32-wide hulls).
constexpr GridCoord kIntermissionEntitySeparation = 64;

[[nodiscard]] bool boxes_overlap(const GridBounds &first, const GridBounds &second) {
    return first.min.x < second.max.x && first.max.x > second.min.x && first.min.y < second.max.y &&
           first.max.y > second.min.y && first.min.z < second.max.z && first.max.z > second.min.z;
}

[[nodiscard]] bool box_inside(const GridBounds &outer, const GridBounds &inner) {
    return inner.min.x >= outer.min.x && inner.min.y >= outer.min.y && inner.min.z >= outer.min.z &&
           inner.max.x <= outer.max.x && inner.max.y <= outer.max.y && inner.max.z <= outer.max.z;
}

// A standing-eye camera origin over the walkable surface at the fraction point of the room, or nothing when the
// standing column under the camera plus the camera's own box is not clear: it must lie inside the room, overlap no
// layout solid and no placement volume, and keep kIntermissionEntitySeparation from every other point placement
// (lights included). The walkable surface is the floor raised by any solid of the room whose footprint holds the
// point, the rule supported_point uses for spawns.
[[nodiscard]] std::optional<GridVec3> clear_intermission_origin(const Layout &level, const Room &room,
                                                                const Fraction x_fraction, const Fraction y_fraction) {
    const GridCoord x = interpolate(room.bounds.min.x, room.bounds.max.x, x_fraction);
    const GridCoord y = interpolate(room.bounds.min.y, room.bounds.max.y, y_fraction);
    GridCoord support = room.bounds.min.z;
    for (const LayoutSolid &solid : level.solids) {
        const GridBounds bounds = solid_bounds(solid);
        if (room.bounds.contains(bounds.center()) && x >= bounds.min.x && x <= bounds.max.x && y >= bounds.min.y &&
            y <= bounds.max.y) {
            support = std::max(support, bounds.max.z);
        }
    }
    const GridVec3 origin{x, y, checked_grid_add(support, kQuake1StandingEyeHeight)};
    const GridBounds column{
        {checked_grid_subtract(x, kIntermissionHalfExtent), checked_grid_subtract(y, kIntermissionHalfExtent), support},
        {checked_grid_add(x, kIntermissionHalfExtent), checked_grid_add(y, kIntermissionHalfExtent),
         checked_grid_add(origin.z, kIntermissionHalfExtent)}};
    if (!box_inside(room.bounds, column))
        return std::nullopt;
    for (const LayoutSolid &solid : level.solids) {
        if (boxes_overlap(column, solid_bounds(solid)))
            return std::nullopt;
    }
    for (const Placement &placement : level.placements) {
        if (placement.volume) {
            if (boxes_overlap(column, *placement.volume))
                return std::nullopt;
            continue;
        }
        const std::int64_t dx = static_cast<std::int64_t>(placement.origin.x) - x;
        const std::int64_t dy = static_cast<std::int64_t>(placement.origin.y) - y;
        if (std::abs(dx) < kIntermissionEntitySeparation && std::abs(dy) < kIntermissionEntitySeparation)
            return std::nullopt;
    }
    return origin;
}

[[nodiscard]] std::int16_t whole_degrees(const long double radians) {
    return static_cast<std::int16_t>(std::lround(radians * 180.0L / std::numbers::pi_v<long double>));
}

void add_quake1_intermission(Layout &level) {
    // The largest room by floor area (then volume, then the lowest id) holds the camera.
    std::vector<const Room *> rooms;
    rooms.reserve(level.rooms.size());
    for (const Room &room : level.rooms)
        rooms.push_back(&room);
    const auto extent = [](const GridCoord minimum, const GridCoord maximum) {
        return static_cast<std::int64_t>(maximum) - minimum;
    };
    std::stable_sort(rooms.begin(), rooms.end(), [&extent](const Room *left, const Room *right) {
        const std::int64_t left_area =
            extent(left->bounds.min.x, left->bounds.max.x) * extent(left->bounds.min.y, left->bounds.max.y);
        const std::int64_t right_area =
            extent(right->bounds.min.x, right->bounds.max.x) * extent(right->bounds.min.y, right->bounds.max.y);
        if (left_area != right_area)
            return left_area > right_area;
        const std::int64_t left_height = extent(left->bounds.min.z, left->bounds.max.z);
        const std::int64_t right_height = extent(right->bounds.min.z, right->bounds.max.z);
        if (left_height != right_height)
            return left_height > right_height;
        return left->id < right->id;
    });

    // Corners first (a diagonal view across the room), then wall midpoints, then the spawn grid.
    std::vector<std::pair<int, int>> candidates{{12, 12}, {88, 12}, {12, 88}, {88, 88},
                                                {50, 12}, {50, 88}, {12, 50}, {88, 50}};
    constexpr std::array<int, 8> grid{10, 21, 32, 43, 57, 68, 79, 90};
    for (const int x_percent : grid) {
        for (const int y_percent : grid)
            candidates.emplace_back(x_percent, y_percent);
    }

    for (const Room *room : rooms) {
        for (const auto &[x_percent, y_percent] : candidates) {
            const std::optional<GridVec3> origin =
                clear_intermission_origin(level, *room, percent(x_percent), percent(y_percent));
            if (!origin)
                continue;
            // Face the room centre and look down at the centre of its floor.
            const GridVec3 center = room->bounds.center();
            const long double dx = static_cast<long double>(center.x) - origin->x;
            const long double dy = static_cast<long double>(center.y) - origin->y;
            const long double horizontal = std::sqrt(dx * dx + dy * dy);
            const long double drop = static_cast<long double>(origin->z) - room->bounds.min.z;
            std::int16_t yaw = horizontal > 0.0L ? whole_degrees(std::atan2(dy, dx)) : std::int16_t{0};
            if (yaw < 0)
                yaw = static_cast<std::int16_t>(yaw + 360);
            if (yaw >= 360)
                yaw = static_cast<std::int16_t>(yaw - 360);
            const std::int16_t pitch =
                horizontal > 0.0L ? std::clamp<std::int16_t>(whole_degrees(std::atan2(drop, horizontal)), 0, 89)
                                  : std::int16_t{0};

            Placement camera;
            camera.id = next_placement_id(level);
            camera.role = PlacementRole::intermission;
            camera.origin = *origin;
            camera.angle_degrees = yaw;
            camera.pitch_degrees = pitch;
            level.placements.push_back(std::move(camera));
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// R1-s cause r1s-mp-player-start (switch r1s.mp_player_start; docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.3, R1-s item 2).
//
// Why every Quake 1 and Quake 2 multiplayer map needs one info_player_start. A server started with `map <name>`
// while deathmatch and coop are 0 puts every client at an info_player_start, and aborts when there is none:
//  - Quake 1 SelectSpawnPoint ends in find(world, classname, "info_player_start") and, failing that,
//    error("PutClientInServer: no info_player_start on level") (progs1996 SelectSpawnPoint statements 6064-6081;
//    quakec/client.qc:672-686; ThreeWave quakec_ctf/client.qc:552-563).
//  - Quake 2 SelectSpawnPoint searches info_player_start and, failing that, calls gi.error("Couldn't find spawn
//    point %s") (game/p_client.c:885-908; ctf/p_client.c:918-941). A dedicated server forces deathmatch 1 unless
//    coop is set (server/sv_init.c:319-323), so the abort is the listen-server `map` load.
// Generated deathmatch, duel and CTF maps had no info_player_start.
//
// Why its presence does not change deathmatch, duel or CTF spawn selection (static reading of the selection code;
// the change record out/phase2/changes/r1s-mp-player-start/record.json keeps the statement listing):
//  - Quake 1, 1996 progs: with deathmatch set, the IFNOT at statement 6031 falls into the while (TRUE) loop
//    6033-6063 over find(spot, classname, "info_player_deathmatch"). Its only exits are the RETURNs at 6043 and
//    6062, and the IFNOT TRUE at 6033 never branches, so the info_player_start search at 6072-6081 is never reached.
//  - Quake 1 re-release: every path of the `else if ( deathmatch )` block returns an info_player_deathmatch (or
//    world while none is free) (quakec/client.qc:572-670); the start is searched only after the block (680).
//  - ThreeWave CTF: in deathmatch it tries TeamCaptureSpawn (info_player_team1/2), the vote destination, then
//    info_player_deathmatch, and returns whenever a deathmatch spot exists (quakec_ctf/client.qc:532-550).
//  - Quake 2: SelectDeathmatchSpawnPoint returns an info_player_deathmatch whenever one exists (random:
//    game/p_client.c:733-781, NULL only when there is none, 760-761; farthest: 789-820, falling back to the first
//    spot at 817), and SelectSpawnPoint searches info_player_start only `if (!spot)` (885). The CTF game's
//    SelectCTFSpawnPoint returns a team start or SelectRandom/FarthestDeathmatchSpawnPoint (ctf/g_ctf.c:441-507),
//    and its SelectSpawnPoint also reads the start only `if (!spot)` (ctf/p_client.c:903-918).
//  - Every search above is find/G_Find by classname, so an entity of another classname does not change which
//    deathmatch spot comes next. The start is appended as the last placement, so it is the last map entity and
//    every earlier entity keeps its entity number. Spawning it runs nothing selection depends on: Quake 1
//    info_player_start is an empty function (progs1996 info_player_start statement 6171; quakec/client.qc:825-827;
//    quakec_ctf/client.qc:664-666), and Quake 2 SP_info_player_start returns at once unless coop is set
//    (game/p_client.c:108-118; ctf/p_client.c:108-118), so it adds no rand() draw and no entity.
//  - Not a spawn effect, kept for completeness: a Quake 2 map without info_player_intermission uses the first
//    info_player_start, before the first info_player_deathmatch, as its match-end camera (game/p_hud.c:126-132;
//    ctf/p_hud.c:131-137), so the start faces its room centre. Quake 1 maps keep their info_intermission camera,
//    which FindIntermission takes before any info_player_start (progs1996 FindIntermission statements 5672-5705).

// The standing player box both games spawn at the start: mins '-16 -16 -24', maxs '16 16 32' (progs1996
// PutClientInServer statements 6149-6152 with VEC_HULL_MIN/VEC_HULL_MAX; quakec/defs.qc:351-352,
// quakec/client.qc:794; game/p_client.c:1099-1100, ctf/p_client.c:1052-1053). The origin is 24 units above the
// feet; Quake 1 adds 1 unit and Quake 2 adds 9 before dropping the player (progs1996 PutClientInServer
// 6130-6133, quakec/client.qc:783; game/p_client.c:911, ctf/p_client.c:944).
constexpr GridCoord kPlayerHullHalfWidth = 16;
constexpr GridCoord kPlayerHullBelowOrigin = 24;
constexpr GridCoord kPlayerHullAboveOrigin = 32;
// Candidate feet positions lie on a 32-unit lattice of the room floor, 32 units in from its walls.
constexpr GridCoord kStartLatticeStep = 32;
constexpr GridCoord kStartWallInset = 32;
// Horizontal separation (on x or y) from every other point entity, lights and the Quake 1 camera included: two
// 32-wide boxes 32 units apart, the spawn-legality separation of plan 9.3 (>= 64 u for 32-wide hulls). It also
// keeps the start's spawn_tdeath box (progs1996 PutClientInServer 6166-6169; quakec/client.qc:807) and its
// KillBox (game/p_client.c:1247, ctf/p_client.c:1192) clear of other spawns, items and the Quake 2 deathmatch
// teleporter pads (SP_info_player_deathmatch -> SP_misc_teleporter_dest, game/p_client.c:123-131), which a `map` load
// removes.
constexpr GridCoord kStartEntitySeparation = 64;

[[nodiscard]] std::int16_t yaw_towards(const GridVec3 from, const GridVec3 to) {
    const long double dx = static_cast<long double>(to.x) - from.x;
    const long double dy = static_cast<long double>(to.y) - from.y;
    if (dx == 0.0L && dy == 0.0L)
        return 0;
    std::int16_t yaw = whole_degrees(std::atan2(dy, dx));
    if (yaw < 0)
        yaw = static_cast<std::int16_t>(yaw + 360);
    if (yaw >= 360)
        yaw = static_cast<std::int16_t>(yaw - 360);
    return yaw;
}

// True when the closed segment meets the closed box (slab test).
[[nodiscard]] bool segment_meets_box(const GridVec3 from, const GridVec3 to, const GridBounds &box) {
    const std::array<long double, 3> start{static_cast<long double>(from.x), static_cast<long double>(from.y),
                                           static_cast<long double>(from.z)};
    const std::array<long double, 3> delta{static_cast<long double>(to.x) - from.x,
                                           static_cast<long double>(to.y) - from.y,
                                           static_cast<long double>(to.z) - from.z};
    const std::array<long double, 3> low{static_cast<long double>(box.min.x), static_cast<long double>(box.min.y),
                                         static_cast<long double>(box.min.z)};
    const std::array<long double, 3> high{static_cast<long double>(box.max.x), static_cast<long double>(box.max.y),
                                          static_cast<long double>(box.max.z)};
    long double enter = 0.0L;
    long double leave = 1.0L;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (delta[axis] == 0.0L) {
            if (start[axis] < low[axis] || start[axis] > high[axis])
                return false;
            continue;
        }
        long double first = (low[axis] - start[axis]) / delta[axis];
        long double second = (high[axis] - start[axis]) / delta[axis];
        if (first > second)
            std::swap(first, second);
        enter = std::max(enter, first);
        leave = std::min(leave, second);
        if (enter > leave)
            return false;
    }
    return true;
}

struct StartStation {
    GridVec3 origin;
    // A light of the same room is in clear view of the origin: no layout solid meets the segment between them.
    bool lit{};
    // Squared horizontal distance to the room centre.
    std::int64_t centre_distance{};
};

// Every supported, clear standing station of the room floor, in x-major lattice order. A station's player box (feet
// on the room floor, 32 x 32 x 56) lies inside the room, overlaps no layout solid (structure, platform, stair or
// trigger) and no placement volume, and keeps kStartEntitySeparation from every other point placement.
[[nodiscard]] std::vector<StartStation> start_stations(const Layout &level, const Room &room) {
    const GridCoord floor = room.bounds.min.z;
    const GridCoord origin_z = checked_grid_add(floor, kPlayerHullBelowOrigin);
    const GridCoord top = checked_grid_add(origin_z, kPlayerHullAboveOrigin);
    const GridVec3 centre = room.bounds.center();
    std::vector<GridVec3> lights;
    for (const Placement &placement : level.placements) {
        if (placement.role == PlacementRole::light && !placement.volume && room.bounds.contains(placement.origin))
            lights.push_back(placement.origin);
    }
    std::vector<StartStation> stations;
    for (std::int64_t x = static_cast<std::int64_t>(room.bounds.min.x) + kStartWallInset;
         x <= static_cast<std::int64_t>(room.bounds.max.x) - kStartWallInset; x += kStartLatticeStep) {
        for (std::int64_t y = static_cast<std::int64_t>(room.bounds.min.y) + kStartWallInset;
             y <= static_cast<std::int64_t>(room.bounds.max.y) - kStartWallInset; y += kStartLatticeStep) {
            const GridVec3 origin{checked_grid_coord(x), checked_grid_coord(y), origin_z};
            const GridBounds hull{{checked_grid_subtract(origin.x, kPlayerHullHalfWidth),
                                   checked_grid_subtract(origin.y, kPlayerHullHalfWidth), floor},
                                  {checked_grid_add(origin.x, kPlayerHullHalfWidth),
                                   checked_grid_add(origin.y, kPlayerHullHalfWidth), top}};
            if (!box_inside(room.bounds, hull))
                continue;
            const bool blocked =
                std::any_of(level.solids.begin(), level.solids.end(),
                            [&hull](const LayoutSolid &solid) { return boxes_overlap(hull, solid_bounds(solid)); });
            if (blocked)
                continue;
            const bool crowded =
                std::any_of(level.placements.begin(), level.placements.end(), [&hull, origin](const Placement &other) {
                    if (other.volume)
                        return boxes_overlap(hull, *other.volume);
                    return std::abs(static_cast<std::int64_t>(other.origin.x) - origin.x) < kStartEntitySeparation &&
                           std::abs(static_cast<std::int64_t>(other.origin.y) - origin.y) < kStartEntitySeparation;
                });
            if (crowded)
                continue;
            const bool lit = std::any_of(lights.begin(), lights.end(), [&level, origin](const GridVec3 light) {
                return std::none_of(level.solids.begin(), level.solids.end(),
                                    [origin, light](const LayoutSolid &solid) {
                                        return segment_meets_box(origin, light, solid_bounds(solid));
                                    });
            });
            const std::int64_t dx = static_cast<std::int64_t>(centre.x) - origin.x;
            const std::int64_t dy = static_cast<std::int64_t>(centre.y) - origin.y;
            stations.push_back({origin, lit, dx * dx + dy * dy});
        }
    }
    return stations;
}

// ---------------------------------------------------------------------------------------------------------------
// R1-s cause r1s-sp-coop-starts (switch r1s.sp_coop_starts; docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.3, R1-s item 3; D8
// role coop_start).
//
// Why every Quake 1 and Quake 2 single-player map needs info_player_coop. Every co-op player is put at a spawn spot,
// and the spot's telefrag check kills whoever already stands there:
//  - Quake 1, 1996 progs: with coop set, SelectSpawnPoint moves the global lastspawn to the next info_player_coop
//    after it and, past the last one, to the first info_player_start, and returns it (progs1996 SelectSpawnPoint
//    statements 6014-6030; re-release quakec/client.qc:562-572; ThreeWave quakec_ctf/client.qc:526-530).
//    PutClientInServer, on the first spawn and on every co-op respawn (progs1996 respawn statements 5975-5981;
//    quakec/client.qc:456-466), puts the player there and calls spawn_tdeath (progs1996 PutClientInServer statements
//    6166-6169; quakec/client.qc:807): a trigger one unit larger than the player box on every side that kills any
//    other player it touches (progs1996 spawn_tdeath statements 10219-10250 and tdeath_touch 10188-10218;
//    quakec/triggers.qc:352-390). Without an info_player_coop every co-op player lands on the one start and
//    telefrags the player before.
//  - Quake 2: SelectCoopSpawnPoint gives client 0 the info_player_start and client N >= 1 the Nth info_player_coop
//    whose targetname equals the arrival spawnpoint, or NULL when there are fewer, and SelectSpawnPoint then falls
//    back to the info_player_start (game/p_client.c:831-866, 875-908). PutClientInServer KillBox-es whatever overlaps
//    the player box there (game/p_client.c:1111, 1247; game/g_utils.c:549-568). A co-op server holds at most four
//    clients (server/sv_init.c:334-337), so three coop spots and the start give every client a spot of its own.
// Harmless in single player: Quake 1 info_player_coop is an empty function (progs1996 info_player_coop statement 6175;
// quakec/client.qc:855-857) and SelectSpawnPoint reads it only when coop is set (above); Quake 2 SP_info_player_coop
// frees the entity when coop is 0 (game/p_client.c:137-143).
// Quake 2 coop starts carry no targetname: a generated map is loaded directly (`map <name>` leaves the spawnpoint
// empty, server/sv_init.c:422-430) and SelectCoopSpawnPoint reads a missing targetname as "" (game/p_client.c:852-855).
// The starts are appended after every other placement, so every earlier entity keeps its number and position, and in
// Quake 1 they follow the start in entity order: find() searches forward from lastspawn (progs1996 SelectSpawnPoint
// statements 6015-6026), so the co-op cycle runs coop, coop, coop, start, coop, ... rather than sticking on the start.
//
// A station is a supported, clear standing station of start_stations(): feet on the room floor (origin 24 units above
// it), the 32 x 32 x 56 player box (VEC_HULL_MIN/MAX '-16 -16 -24' '16 16 32', cited above) inside the room,
// overlapping no layout solid and no placement volume, and at least kStartEntitySeparation (64 units, on x or y) from
// every other point placement: the start, the coop starts already added, lights and the Quake 1 camera. 64 units also
// keeps the 34-unit spawn_tdeath box and the KillBox of one spot clear of a player standing at the next.

struct CoopStation {
    GridVec3 origin;
    bool lit{};
    // Squared horizontal distance to the info_player_start.
    std::int64_t start_distance{};
};

// The rooms that may hold the coop starts, in preference order: the room that holds the start, then the rooms its
// connections join it to, in connection order.
[[nodiscard]] std::vector<const Room *> coop_start_rooms(const Layout &level, const Placement &start) {
    std::vector<const Room *> rooms;
    for (const Room &room : level.rooms) {
        if (room.bounds.contains(start.origin)) {
            rooms.push_back(&room);
            break;
        }
    }
    if (rooms.empty())
        throw std::logic_error("r1s-sp-coop-starts: the info_player_start is outside every room");
    const RoomId start_room = rooms.front()->id;
    for (const Connection &connection : level.connections) {
        if (connection.from_room != start_room && connection.to_room != start_room)
            continue;
        const RoomId other_id = connection.from_room == start_room ? connection.to_room : connection.from_room;
        const Room *other = find_room(level, other_id);
        if (other != nullptr && std::find(rooms.begin(), rooms.end(), other) == rooms.end())
            rooms.push_back(other);
    }
    return rooms;
}

// ---------------------------------------------------------------------------------------------------------------
// R1-s cause r1s-q3-ctf-team-begin (switch r1s.q3_ctf_team_begin; docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.3, R1-s item 4;
// existing role team_initial_spawn, classnames team_CTF_redplayer / team_CTF_blueplayer from
// data/profiles/q3/generator-current.json).
//
// Why every Quake 3 CTF map needs a team-begin spot per player of a full team (Quake III Arena 1.32b source):
//  - ClientBegin puts a client that enters the game in teamState.state TEAM_BEGIN (code/game/g_client.c:1013), and
//    SetTeam does the same on a team change ("he starts at 'base'", code/game/g_cmds.c:599-600). ClientSpawn, with
//    g_gametype >= GT_CTF, takes the spot from SelectCTFSpawnPoint(team, teamState.state, ...)
//    (code/game/g_client.c:1076-1081) and sets TEAM_ACTIVE afterwards (code/game/g_client.c:1109), so the first spawn
//    after entering the game or joining a team reads the team-begin spots and every later respawn the
//    team_CTF_redspawn / team_CTF_bluespawn spots, which this cause keeps as they are.
//  - SelectRandomTeamSpawnPoint (code/game/g_team.c:983-1024): in TEAM_BEGIN it searches team_CTF_redplayer or
//    team_CTF_blueplayer (990-996), otherwise team_CTF_redspawn or team_CTF_bluespawn (997-1004). It keeps the spots
//    SpotWouldTelefrag finds free (1009-1016) and picks one at random (1022-1023); when none is free it returns the
//    first spot of the class anyway (1018-1020). SelectCTFSpawnPoint copies that spot's origin, 9 units higher, and its
//    angles (code/game/g_team.c:1033-1047).
//  - SpotWouldTelefrag is true when any client touches the player box, playerMins '-15 -15 -24' to playerMaxs
//    '15 15 32' (code/game/g_client.c:27-28), around the spot origin (code/game/g_client.c:81-100). ClientSpawn then
//    G_KillBox-es every client the new player's box touches (code/game/g_client.c:1205; code/game/g_utils.c:525-545)
//    and links the player (code/game/g_client.c:1206), so the next client of the same frame sees the spot taken.
//  - When a match starts, every client of a team begins together. With one begin spot per team the second player of
//    a team finds no free spot, is given the first spot back and telefrags the first player. Automatic team
//    assignment joins the smaller team (PickTeam, code/game/g_client.c:566-583), so in a match of target_players
//    a team holds at most ceil(target_players / 2) players and needs that many begin spots. (Players who pick a team
//    by hand can stack one team further; g_teamForceBalance still allows a spread of two, code/game/g_cmds.c:547-560.
//    The players beyond the spots then get the class's first spot back, as before.)
//  - Harmless outside CTF: SP_team_CTF_redplayer and SP_team_CTF_blueplayer are empty (code/game/g_team.c:1163-1175)
//    and only SelectCTFSpawnPoint reads the class (above). Both classnames are in the spawn table
//    (code/game/g_spawn.c:256-257).
//
// The spots are appended after every other placement, red first, so every earlier entity keeps its number and
// position; G_Find walks entity numbers in order, so the team's existing spot stays the class's first, the fallback of
// a full house. A station is a supported, clear standing station of start_stations(): feet on the room floor (origin 24
// units above it; SelectCTFSpawnPoint adds 9 and the player settles), the 32 x 32 x 56 player box inside the room,
// overlapping no layout solid and no placement volume, and at least kStartEntitySeparation (64 units, on x or y) from
// every other point placement: the team's other begin spots, the respawn spots, the flags, items, lights and the
// deathmatch fallback spots. 64 units keeps two 30-wide SpotWouldTelefrag boxes 34 units apart.

// The rooms that may hold a team's begin spots, in preference order: the room that holds the team's flag (its base),
// then the rooms of the team's half joined to it, in connection order. The team's half is the respawn half of
// add_arena_spawns(): the first max(1, rooms / 2) rooms for red, the last max(1, rooms / 2) rooms for blue.
[[nodiscard]] std::vector<const Room *> team_begin_rooms(const Layout &level, const Team team) {
    const auto flag =
        std::find_if(level.placements.begin(), level.placements.end(), [team](const Placement &placement) {
            return placement.role == PlacementRole::flag && placement.team == team;
        });
    if (flag == level.placements.end())
        throw std::logic_error("r1s-q3-ctf-team-begin: a Quake 3 CTF team has no flag to mark its base");
    std::vector<const Room *> rooms;
    for (const Room &room : level.rooms) {
        if (room.bounds.contains(flag->origin)) {
            rooms.push_back(&room);
            break;
        }
    }
    if (rooms.empty())
        throw std::logic_error("r1s-q3-ctf-team-begin: a team's flag is outside every room");
    const std::size_t team_span = std::max<std::size_t>(1, level.rooms.size() / 2);
    const auto in_team_half = [&level, team, team_span](const Room &room) {
        for (std::size_t index = 0; index < level.rooms.size(); ++index) {
            if (level.rooms[index].id != room.id)
                continue;
            return team == Team::red ? index < team_span : index >= level.rooms.size() - team_span;
        }
        return false;
    };
    const RoomId base = rooms.front()->id;
    for (const Connection &connection : level.connections) {
        if (connection.from_room != base && connection.to_room != base)
            continue;
        const RoomId other_id = connection.from_room == base ? connection.to_room : connection.from_room;
        const Room *other = find_room(level, other_id);
        if (other != nullptr && in_team_half(*other) && std::find(rooms.begin(), rooms.end(), other) == rooms.end())
            rooms.push_back(other);
    }
    return rooms;
}

// ---------------------------------------------------------------------------------------------------------------
// R1-s cause r1s-monster-hull-support (switch r1s.monster_hull_support; docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.3, R1-s
// item 5; audit gap 6).
//
// Why a monster's whole spawn box, not its origin, has to be clear and stand on support (audit: at "brutal" intensity
// the y = 68% slots of a platform room put 64-wide ogres, fiends, vores and gladiators 12-16 units into the platform,
// because supported_point() lifts a slot only when its origin point lies over a solid):
//  - Each monster class sets its own box: Quake 1 monster_army and monster_knight setsize '-16 -16 -24' '16 16 40',
//    monster_ogre, monster_demon1 and monster_shalrath VEC_HULL2_MIN '-32 -32 -24' / VEC_HULL2_MAX '32 32 64'
//    (progs1996 monster_army 14962-14965, monster_knight 14445-14448, monster_ogre 12732-12735, monster_demon1
//    13101-13104, monster_shalrath 19855-19858; quakec/monsters/grunt.qc:319, knight.qc:279, ogre.qc:520,
//    fiend.qc:231, vore.qc:262; quakec/defs.qc:351-355); Quake 2 monster_soldier, monster_infantry, monster_gunner and
//    monster_berserk mins '-16 -16 -24' maxs '16 16 32', monster_gladiator '-32 -32 -24' '32 32 64'
//    (game/m_soldier.c:1205-1206 via 1269, m_infantry.c:582-583, m_gunner.c:603-604, m_berserk.c:430-431,
//    m_gladiator.c:362-363). The boxes are data: monster_hulls in data/profiles/q1|q2/generator-current.json (D10),
//    compiled into game_profile::monster_hull().
//  - The engine clips that box, not the origin, against the world. Quake 1 picks hull 1 for a box at most 32 wide and
//    hull 2 ('-32 -32 -24' '32 32 64') otherwise (WinQuake/world.c:148-153); the 32-wide classes' setsize box (64 high)
//    contains hull 1 (56 high), so testing the setsize box is the safe side. Quake 2 traces the box itself
//    (game/g_monster.c:319, MASK_MONSTERSOLID).
//  - At spawn the monster is raised 1 unit and dropped 256 units with its box: droptofloor leaves it where it is when
//    the box starts in solid (WinQuake/pr_cmds.c:1198-1202; game/g_monster.c:315-322), and walkmove(0, 0) then fails,
//    reported as "walkmonster in wall" / "in solid" (progs1996 walkmonster_start_go 8409-8427,
//    quakec/monsters.qc:92-98; game/g_monster.c:673-679): the monster is stuck in the brush.
//  - walkmove also fails when SV_CheckBottom / M_CheckBottom refuses the spot: a bottom corner whose floor is missing
//    or more than STEPSIZE (18) below the middle's (WinQuake/sv_move.c:37-97, 194; game/m_move.c:37-97). A box hanging
//    over a 48-112 unit platform edge fails it, and the monster cannot walk.
//
// The rule (switch on, Quake 1 and Quake 2 single player): after the single-player allocator has placed everything,
// each monster in placement order is tested at its slot. A station (x, y) of a room holds a monster when its box
// footprint lies inside the room at least kMonsterWallClearance from the walls, the highest solid top under the
// footprint (the floor where no solid is under it) is held by one solid whose footprint strictly contains the box's
// (no corner over an edge, so CheckBottom's quick test holds), and the box's whole fall, from its spawn origin
// kMonsterSpawnLift above that support (the height supported_point() gives) down to resting on it, meets no layout
// solid and stays under the ceiling. A slot that holds its monster keeps it, unchanged. Otherwise the monster moves,
// keeping its class, facing and id, to the nearest station of its room on a kMonsterStationStep lattice through the
// slot (ties: the lower x step, then the lower y step) that also keeps its box kMonsterEntityClearance clear of every
// other entity's box (other monsters' class boxes, a 32 x 32 box for every other point entity, lights excepted) and
// of every placement volume; with no such station the monster is left out. Only monster placements change, so every
// other placement keeps its id and position.

// Horizontal clearance of a monster's box from the room walls.
constexpr GridCoord kMonsterWallClearance = 8;
// Clearance of a monster's box from every layout solid it does not stand on (no face contact).
constexpr GridCoord kMonsterSolidClearance = 1;
// Horizontal clearance of a moved monster's box from every other entity's box.
constexpr GridCoord kMonsterEntityClearance = 16;
// Lattice step of the stations a monster may move to.
constexpr GridCoord kMonsterStationStep = 16;
// A monster's spawn origin above its support: supported_point()'s z_offset for monsters in populate_single_player().
constexpr GridCoord kMonsterSpawnLift = 32;
// Half the width of the box assumed for any other point entity (the standing player box, '-16 -16 -24' '16 16 32').
constexpr GridCoord kOtherEntityHalfWidth = 16;

[[nodiscard]] game_profile::MonsterHull monster_class_hull(const Layout &level, const Placement &monster) {
    const std::optional<game_profile::VariantTable> table =
        game_profile::variant_table(level.spec.game, PlacementRole::monster);
    if (!table || monster.variant >= table->classnames.size())
        throw std::logic_error("r1s-monster-hull-support: a monster placement has no classname in the game profile");
    const std::optional<game_profile::MonsterHull> hull =
        game_profile::monster_hull(level.spec.game, table->classnames[monster.variant]);
    if (!hull)
        throw std::logic_error("r1s-monster-hull-support: the game profile has no monster_hulls entry for " +
                               std::string(table->classnames[monster.variant]));
    return *hull;
}

struct HullFootprint {
    std::int64_t min_x{};
    std::int64_t min_y{};
    std::int64_t max_x{};
    std::int64_t max_y{};
};

[[nodiscard]] HullFootprint hull_footprint(const std::int64_t x, const std::int64_t y,
                                           const game_profile::MonsterHull &hull, const std::int64_t grow = 0) {
    return {x + hull.mins.x - grow, y + hull.mins.y - grow, x + hull.maxs.x + grow, y + hull.maxs.y + grow};
}

[[nodiscard]] bool footprints_overlap(const HullFootprint &first, const HullFootprint &second) {
    return first.min_x < second.max_x && first.max_x > second.min_x && first.min_y < second.max_y &&
           first.max_y > second.min_y;
}

[[nodiscard]] bool footprint_meets(const HullFootprint &footprint, const GridBounds &bounds) {
    return footprint.min_x < bounds.max.x && footprint.max_x > bounds.min.x && footprint.min_y < bounds.max.y &&
           footprint.max_y > bounds.min.y;
}

// The fall of a monster standing on `support`: from its spawn origin kMonsterSpawnLift above the support down to its
// box resting on the support (origin -mins.z above it).
[[nodiscard]] GridBounds monster_fall_column(const HullFootprint &footprint, const std::int64_t support,
                                             const game_profile::MonsterHull &hull) {
    const std::int64_t highest_origin = support + std::max<std::int64_t>(kMonsterSpawnLift, -hull.mins.z);
    return {{checked_grid_coord(footprint.min_x), checked_grid_coord(footprint.min_y), checked_grid_coord(support)},
            {checked_grid_coord(footprint.max_x), checked_grid_coord(footprint.max_y),
             checked_grid_coord(highest_origin + hull.maxs.z)}};
}

// The support height of a monster box at (x, y) of the room, or nothing when the station does not hold it: the
// footprint must lie inside the room kMonsterWallClearance from its walls, and the highest top of the solids under the
// footprint (the room floor when there is none) must be held by one solid whose footprint strictly contains the box's,
// and the fall column onto it must meet no layout solid and stay under the ceiling.
[[nodiscard]] std::optional<GridCoord> monster_station_support(const Layout &level, const Room &room,
                                                               const std::int64_t x, const std::int64_t y,
                                                               const game_profile::MonsterHull &hull) {
    const HullFootprint footprint = hull_footprint(x, y, hull);
    if (footprint.min_x < static_cast<std::int64_t>(room.bounds.min.x) + kMonsterWallClearance ||
        footprint.min_y < static_cast<std::int64_t>(room.bounds.min.y) + kMonsterWallClearance ||
        footprint.max_x > static_cast<std::int64_t>(room.bounds.max.x) - kMonsterWallClearance ||
        footprint.max_y > static_cast<std::int64_t>(room.bounds.max.y) - kMonsterWallClearance)
        return std::nullopt;
    std::int64_t support = room.bounds.min.z;
    const LayoutSolid *holder = nullptr;
    for (const LayoutSolid &solid : level.solids) {
        const GridBounds bounds = solid_bounds(solid);
        if (!footprint_meets(footprint, bounds) || bounds.max.z <= support)
            continue;
        support = bounds.max.z;
        holder = &solid;
    }
    if (holder != nullptr) {
        // Liquids and trigger volumes hold nothing up; the support must be a solid brush that the whole footprint
        // stands on, strictly inside its edges (a corner on the edge itself is not over solid for CheckBottom).
        const GridBounds bounds = solid_bounds(*holder);
        if (holder->contents != BrushContents::solid || holder->purpose == BrushPurpose::trigger_volume ||
            !(bounds.min.x < footprint.min_x && footprint.max_x < bounds.max.x && bounds.min.y < footprint.min_y &&
              footprint.max_y < bounds.max.y))
            return std::nullopt;
    }
    // The fall column, kMonsterSolidClearance wider on every side and above, must meet no layout solid: a box that only
    // touches a brush face is already in solid for Quake 2 (CM_TestBoxInBrush leaves a brush only when the box is
    // strictly in front of a face, qcommon/cmodel.c:1103-1150), and resting on the support itself is no contact (the
    // column starts at its top).
    const GridBounds column = monster_fall_column(footprint, support, hull);
    const GridBounds clearance{{checked_grid_subtract(column.min.x, kMonsterSolidClearance),
                                checked_grid_subtract(column.min.y, kMonsterSolidClearance), column.min.z},
                               {checked_grid_add(column.max.x, kMonsterSolidClearance),
                                checked_grid_add(column.max.y, kMonsterSolidClearance),
                                checked_grid_add(column.max.z, kMonsterSolidClearance)}};
    if (clearance.max.z > room.bounds.max.z)
        return std::nullopt;
    for (const LayoutSolid &solid : level.solids) {
        if (boxes_overlap(clearance, solid_bounds(solid)))
            return std::nullopt;
    }
    return checked_grid_coord(support);
}

[[nodiscard]] const Room *room_holding(const Layout &level, const GridVec3 origin) {
    for (const Room &room : level.rooms) {
        if (room.bounds.contains(origin))
            return &room;
    }
    return nullptr;
}

// Why the monster's box at its origin is unsafe, or nothing when its slot holds it.
[[nodiscard]] std::optional<std::string> monster_slot_problem(const Layout &level, const Placement &monster,
                                                              const game_profile::MonsterHull &hull) {
    const Room *room = room_holding(level, monster.origin);
    if (room == nullptr)
        return std::string("its origin is outside every room");
    const std::optional<GridCoord> support =
        monster_station_support(level, *room, monster.origin.x, monster.origin.y, hull);
    if (!support)
        return std::string("its box meets a solid brush, hangs over a support edge or touches a wall");
    if (static_cast<std::int64_t>(monster.origin.z) != static_cast<std::int64_t>(*support) + kMonsterSpawnLift)
        return std::string("its box is not standing on the support under it (its origin is ") +
               std::to_string(monster.origin.z) + ", the support top " + std::to_string(*support) + ")";
    return std::nullopt;
}

// True when a moved monster's box at the station keeps clear of every other entity.
[[nodiscard]] bool monster_station_clear_of_entities(const Layout &level, const std::size_t monster_index,
                                                     const HullFootprint &footprint, const GridBounds &column) {
    const HullFootprint grown{footprint.min_x - kMonsterEntityClearance, footprint.min_y - kMonsterEntityClearance,
                              footprint.max_x + kMonsterEntityClearance, footprint.max_y + kMonsterEntityClearance};
    for (std::size_t index = 0; index < level.placements.size(); ++index) {
        const Placement &other = level.placements[index];
        if (index == monster_index || other.role == PlacementRole::light)
            continue;
        if (other.volume) {
            if (boxes_overlap(column, *other.volume))
                return false;
            continue;
        }
        const HullFootprint other_footprint =
            other.role == PlacementRole::monster
                ? hull_footprint(other.origin.x, other.origin.y, monster_class_hull(level, other))
                : HullFootprint{static_cast<std::int64_t>(other.origin.x) - kOtherEntityHalfWidth,
                                static_cast<std::int64_t>(other.origin.y) - kOtherEntityHalfWidth,
                                static_cast<std::int64_t>(other.origin.x) + kOtherEntityHalfWidth,
                                static_cast<std::int64_t>(other.origin.y) + kOtherEntityHalfWidth};
        if (footprints_overlap(grown, other_footprint))
            return false;
    }
    return true;
}

// The nearest station of the monster's room that holds it and keeps it clear of every other entity, as a spawn origin.
[[nodiscard]] std::optional<GridVec3> nearest_monster_station(const Layout &level, const std::size_t monster_index,
                                                              const game_profile::MonsterHull &hull) {
    const Placement &monster = level.placements[monster_index];
    const Room *room = room_holding(level, monster.origin);
    if (room == nullptr)
        return std::nullopt;
    const std::int64_t x0 = monster.origin.x;
    const std::int64_t y0 = monster.origin.y;
    const auto steps = [](const std::int64_t from, const std::int64_t low, const std::int64_t high) {
        // Lattice steps k with low <= from + k * step <= high.
        const auto floor_div = [](const std::int64_t value, const std::int64_t divisor) {
            return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
        };
        return std::pair<std::int64_t, std::int64_t>{-floor_div(from - low, kMonsterStationStep),
                                                     floor_div(high - from, kMonsterStationStep)};
    };
    const auto [first_i, last_i] = steps(x0, room->bounds.min.x, room->bounds.max.x);
    const auto [first_j, last_j] = steps(y0, room->bounds.min.y, room->bounds.max.y);
    struct Step {
        std::int64_t distance;
        std::int64_t i;
        std::int64_t j;
    };
    std::vector<Step> order;
    for (std::int64_t i = first_i; i <= last_i; ++i) {
        for (std::int64_t j = first_j; j <= last_j; ++j)
            order.push_back({i * i + j * j, i, j});
    }
    std::sort(order.begin(), order.end(), [](const Step &left, const Step &right) {
        if (left.distance != right.distance)
            return left.distance < right.distance;
        if (left.i != right.i)
            return left.i < right.i;
        return left.j < right.j;
    });
    for (const Step &step : order) {
        const std::int64_t x = x0 + step.i * kMonsterStationStep;
        const std::int64_t y = y0 + step.j * kMonsterStationStep;
        const std::optional<GridCoord> support = monster_station_support(level, *room, x, y, hull);
        if (!support)
            continue;
        const HullFootprint footprint = hull_footprint(x, y, hull);
        if (!monster_station_clear_of_entities(level, monster_index, footprint,
                                               monster_fall_column(footprint, *support, hull)))
            continue;
        return GridVec3{checked_grid_coord(x), checked_grid_coord(y), checked_grid_add(*support, kMonsterSpawnLift)};
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------------------------
// R1-s cause r1s-sp-pickup-hull-support (switch r1s.sp_pickup_hull_support; docs/RC_PHASE1_AUDIT_AND_PLAN.md 9.3, R1-s
// item 6; audit gap 7).
//
// Why a pickup's whole drop box, not its origin, has to be clear and stand on support (audit: in the seed-5 probe the
// exit-room health at (141, 1280, 24) overlaps the platform brush (-176..176, 1288..1512, 0..48), because
// supported_point() lifts a slot only when its origin point lies over a solid, and droptofloor removes the item):
//  - Quake 1: each item's spawn function sets its box with setsize: item_health '0 0 0' '32 32 56' (its origin is the
//    box's minimum corner, not its centre), weapons and item_armor2 '-16 -16 0' '16 16 56' (progs1996 item_health
//    1834-1837, item_armor2 2045-2048, weapon_nailgun 2316-2318, weapon_supernailgun 2333-2335, weapon_grenadelauncher
//    2350-2352, weapon_rocketlauncher 2367-2369, weapon_lightning 2384-2386; quakec/items.qc:176, 321, 590, 604, 618,
//    632, 647). StartItem defers PlaceItem 0.2 s, after the other solids (progs1996 StartItem 1745-1749;
//    quakec/items.qc:94-95). PlaceItem raises the origin 6 units and calls droptofloor (progs1996 PlaceItem 1723-1729;
//    quakec/items.qc:72-75), which traces the box 256 units down and returns 0 when the trace starts all in solid or
//    finds no floor (WinQuake/pr_cmds.c:1198-1202); PlaceItem then removes the item ("Bonus item fell out of level",
//    progs1996 PlaceItem 1730-1743; quakec/items.qc:75-82). The engine clips a 32-wide box with hull 1, offset so the
//    hull covers exactly the box (WinQuake/world.c:150-156), so a box that only touches a brush face is not in solid
//    there.
//  - Quake 2: SpawnItem schedules droptofloor (game/g_items.c:1124-1125; every weapon and armor classname reaches it
//    from ED_CallSpawn's item table, game/g_spawn.c:291-298, and item_health through SP_item_health,
//    game/g_items.c:2121-2131). droptofloor sets every item's box to '-15 -15 -15' '15 15 15', traces it 128 units down
//    with MASK_SOLID and frees the item when the trace starts in solid (game/g_items.c:918-946). A box that only
//    touches a brush face starts in solid: CM_ClipBoxToBrush counts the start outside a brush only when it is strictly
//    in front of some face (qcommon/cmodel.c:1048-1049, 1077-1079).
// The boxes are data: pickup_hulls in data/profiles/q1|q2/generator-current.json (D10), compiled into
// game_profile::pickup_hull(); the classnames come from the same profiles (variants.weapon, classnames.health and
// classnames.armor).
//
// The rule (switch on, Quake 1 and Quake 2 single player): after the single-player allocator has placed everything and
// the monsters stand on support, each pickup in placement order is tested at its slot. A station (x, y) of a room holds
// a pickup when its box footprint lies inside the room at least kPickupWallClearance from the walls, the highest solid
// top under the footprint (the room floor where no solid is under it) is held by one solid whose footprint strictly
// contains the box's (no corner over an edge), and the box's whole fall, from its spawn origin kPickupSpawnLift above
// that support (the height supported_point() gives pickups) plus the engine's raise, down to resting on the support,
// keeps kPickupSolidClearance from every layout solid (face contact included, as Quake 2 counts it) and stays under the
// ceiling. A slot that holds its pickup keeps it, unchanged. Otherwise the pickup moves, keeping its role, class and
// id, to the nearest station of its room on a kPickupStationStep lattice through the slot (ties: the lower x step, then
// the lower y step) that also keeps its box kPickupEntityClearance clear of every other entity's box (monsters' class
// boxes, other pickups' drop boxes, a 32 x 32 box for every other point entity, lights excepted: Quake 1 droptofloor
// clips against the monsters, which spawn first) and of every placement volume; with no such station the pickup is left
// out. Only pickup placements change, so every other placement keeps its id and position.

// Horizontal clearance of a pickup's box from the room walls.
constexpr GridCoord kPickupWallClearance = 8;
// Clearance of a pickup's box from every layout solid it does not rest on (no face contact).
constexpr GridCoord kPickupSolidClearance = 1;
// Horizontal clearance of a moved pickup's box from every other entity's box.
constexpr GridCoord kPickupEntityClearance = 16;
// Lattice step of the stations a pickup may move to.
constexpr GridCoord kPickupStationStep = 16;
// A pickup's origin above its support: supported_point()'s and featured_pickup_point()'s z offset for pickups.
constexpr GridCoord kPickupSpawnLift = 24;
// Quake 1 PlaceItem raises the origin 6 units before droptofloor (progs1996 PlaceItem 1723-1726, the ADD_F of 6;
// quakec/items.qc:72); Quake 2 droptofloor traces from the origin itself (game/g_items.c:937-940).
constexpr GridCoord kQuake1PickupSpawnRaise = 6;
// droptofloor's trace length: Quake 1 256 units (WinQuake/pr_cmds.c:1198), Quake 2 128 units (game/g_items.c:937).
constexpr GridCoord kQuake1PickupDropDistance = 256;
constexpr GridCoord kQuake2PickupDropDistance = 128;

[[nodiscard]] constexpr GridCoord pickup_spawn_raise(const Game game) noexcept {
    return game == Game::quake1 ? kQuake1PickupSpawnRaise : 0;
}

[[nodiscard]] constexpr GridCoord pickup_drop_distance(const Game game) noexcept {
    return game == Game::quake1 ? kQuake1PickupDropDistance : kQuake2PickupDropDistance;
}

// The pickup's classname from the game profile, or an empty view for a role that is not a pickup or a variant outside
// the profile's list.
[[nodiscard]] std::string_view pickup_classname(const Layout &level, const Placement &pickup) {
    if (pickup.role == PlacementRole::weapon) {
        const std::optional<game_profile::VariantTable> table =
            game_profile::variant_table(level.spec.game, PlacementRole::weapon);
        if (!table || pickup.variant >= table->classnames.size())
            return {};
        return table->classnames[pickup.variant];
    }
    if (pickup.role == PlacementRole::health || pickup.role == PlacementRole::armor)
        return game_profile::role_classname(level.spec.game, pickup.role);
    return {};
}

[[nodiscard]] std::optional<game_profile::PickupHull> pickup_class_hull_if_any(const Layout &level,
                                                                               const Placement &pickup) {
    const std::string_view classname = pickup_classname(level, pickup);
    if (classname.empty())
        return std::nullopt;
    return game_profile::pickup_hull(level.spec.game, classname);
}

[[nodiscard]] game_profile::PickupHull pickup_class_hull(const Layout &level, const Placement &pickup) {
    const std::optional<game_profile::PickupHull> hull = pickup_class_hull_if_any(level, pickup);
    if (!hull)
        throw std::logic_error("r1s-sp-pickup-hull-support: the game profile has no pickup_hulls entry for " +
                               std::string(pickup_classname(level, pickup)));
    return *hull;
}

[[nodiscard]] HullFootprint pickup_footprint(const std::int64_t x, const std::int64_t y,
                                             const game_profile::PickupHull &hull) {
    return {x + hull.mins.x, y + hull.mins.y, x + hull.maxs.x, y + hull.maxs.y};
}

// The fall of a pickup resting on `support`: from its spawn box (origin kPickupSpawnLift above the support, raised by
// the engine) down to its box resting on the support (the box's bottom on the support's top).
[[nodiscard]] GridBounds pickup_fall_column(const Game game, const HullFootprint &footprint, const std::int64_t support,
                                            const game_profile::PickupHull &hull) {
    const std::int64_t spawn_origin = support + kPickupSpawnLift + pickup_spawn_raise(game);
    return {{checked_grid_coord(footprint.min_x), checked_grid_coord(footprint.min_y), checked_grid_coord(support)},
            {checked_grid_coord(footprint.max_x), checked_grid_coord(footprint.max_y),
             checked_grid_coord(spawn_origin + hull.maxs.z)}};
}

// The support height of a pickup box at (x, y) of the room, or nothing when the station does not hold it: the footprint
// must lie inside the room kPickupWallClearance from its walls, the highest top of the solids under the footprint (the
// room floor when there is none) must be held by one solid whose footprint strictly contains the box's, the box must
// fall onto it from its spawn height within droptofloor's reach, and the fall column must keep kPickupSolidClearance
// from every layout solid and stay under the ceiling.
[[nodiscard]] std::optional<GridCoord> pickup_station_support(const Layout &level, const Room &room,
                                                              const std::int64_t x, const std::int64_t y,
                                                              const game_profile::PickupHull &hull) {
    const HullFootprint footprint = pickup_footprint(x, y, hull);
    if (footprint.min_x < static_cast<std::int64_t>(room.bounds.min.x) + kPickupWallClearance ||
        footprint.min_y < static_cast<std::int64_t>(room.bounds.min.y) + kPickupWallClearance ||
        footprint.max_x > static_cast<std::int64_t>(room.bounds.max.x) - kPickupWallClearance ||
        footprint.max_y > static_cast<std::int64_t>(room.bounds.max.y) - kPickupWallClearance)
        return std::nullopt;
    std::int64_t support = room.bounds.min.z;
    const LayoutSolid *holder = nullptr;
    for (const LayoutSolid &solid : level.solids) {
        const GridBounds bounds = solid_bounds(solid);
        if (!footprint_meets(footprint, bounds) || bounds.max.z <= support)
            continue;
        support = bounds.max.z;
        holder = &solid;
    }
    if (holder != nullptr) {
        // Liquids and trigger volumes hold nothing up (droptofloor's trace passes through them: Quake 1 clipping hulls
        // hold only solid, Quake 2 MASK_SOLID is CONTENTS_SOLID | CONTENTS_WINDOW); the support must be a solid brush
        // that the whole footprint rests on, strictly inside its edges.
        const GridBounds bounds = solid_bounds(*holder);
        if (holder->contents != BrushContents::solid || holder->purpose == BrushPurpose::trigger_volume ||
            !(bounds.min.x < footprint.min_x && footprint.max_x < bounds.max.x && bounds.min.y < footprint.min_y &&
              footprint.max_y < bounds.max.y))
            return std::nullopt;
    }
    // The spawn box must start above the support and reach it within droptofloor's trace.
    const std::int64_t fall = kPickupSpawnLift + pickup_spawn_raise(level.spec.game) + hull.mins.z;
    if (fall < 0 || fall > pickup_drop_distance(level.spec.game))
        return std::nullopt;
    // The fall column, kPickupSolidClearance wider on every side and above, must meet no layout solid: in Quake 2 a box
    // that only touches a brush face starts in solid (cited above), and resting on the support itself is no contact
    // (the column starts at its top).
    const GridBounds column = pickup_fall_column(level.spec.game, footprint, support, hull);
    const GridBounds clearance{{checked_grid_subtract(column.min.x, kPickupSolidClearance),
                                checked_grid_subtract(column.min.y, kPickupSolidClearance), column.min.z},
                               {checked_grid_add(column.max.x, kPickupSolidClearance),
                                checked_grid_add(column.max.y, kPickupSolidClearance),
                                checked_grid_add(column.max.z, kPickupSolidClearance)}};
    if (clearance.max.z > room.bounds.max.z)
        return std::nullopt;
    for (const LayoutSolid &solid : level.solids) {
        if (boxes_overlap(clearance, solid_bounds(solid)))
            return std::nullopt;
    }
    return checked_grid_coord(support);
}

// Why the pickup's box at its origin is unsafe, or nothing when its slot holds it.
[[nodiscard]] std::optional<std::string> pickup_slot_problem(const Layout &level, const Placement &pickup,
                                                             const game_profile::PickupHull &hull) {
    const Room *room = room_holding(level, pickup.origin);
    if (room == nullptr)
        return std::string("its origin is outside every room");
    const std::optional<GridCoord> support =
        pickup_station_support(level, *room, pickup.origin.x, pickup.origin.y, hull);
    if (!support)
        return std::string("its box meets or touches a solid brush, hangs over a support edge or comes within ") +
               std::to_string(kPickupWallClearance) + " units of a wall";
    if (static_cast<std::int64_t>(pickup.origin.z) != static_cast<std::int64_t>(*support) + kPickupSpawnLift)
        return std::string("its box does not rest on the support under it (its origin is ") +
               std::to_string(pickup.origin.z) + ", the support top " + std::to_string(*support) + ")";
    return std::nullopt;
}

// The footprint of another entity a moved pickup keeps clear of: a monster's class box, a pickup's drop box, otherwise
// a 32 x 32 box around the origin.
[[nodiscard]] HullFootprint entity_footprint(const Layout &level, const Placement &other) {
    if (other.role == PlacementRole::monster)
        return hull_footprint(other.origin.x, other.origin.y, monster_class_hull(level, other));
    if (is_pickup_role(other.role)) {
        if (const std::optional<game_profile::PickupHull> hull = pickup_class_hull_if_any(level, other))
            return pickup_footprint(other.origin.x, other.origin.y, *hull);
    }
    return {static_cast<std::int64_t>(other.origin.x) - kOtherEntityHalfWidth,
            static_cast<std::int64_t>(other.origin.y) - kOtherEntityHalfWidth,
            static_cast<std::int64_t>(other.origin.x) + kOtherEntityHalfWidth,
            static_cast<std::int64_t>(other.origin.y) + kOtherEntityHalfWidth};
}

// True when a moved pickup's box at the station keeps clear of every other entity.
[[nodiscard]] bool pickup_station_clear_of_entities(const Layout &level, const std::size_t pickup_index,
                                                    const HullFootprint &footprint, const GridBounds &column) {
    const HullFootprint grown{footprint.min_x - kPickupEntityClearance, footprint.min_y - kPickupEntityClearance,
                              footprint.max_x + kPickupEntityClearance, footprint.max_y + kPickupEntityClearance};
    for (std::size_t index = 0; index < level.placements.size(); ++index) {
        const Placement &other = level.placements[index];
        if (index == pickup_index || other.role == PlacementRole::light)
            continue;
        if (other.volume) {
            if (boxes_overlap(column, *other.volume))
                return false;
            continue;
        }
        if (footprints_overlap(grown, entity_footprint(level, other)))
            return false;
    }
    return true;
}

// The nearest station of the pickup's room that holds it and keeps it clear of every other entity, as an origin.
[[nodiscard]] std::optional<GridVec3> nearest_pickup_station(const Layout &level, const std::size_t pickup_index,
                                                             const game_profile::PickupHull &hull) {
    const Placement &pickup = level.placements[pickup_index];
    const Room *room = room_holding(level, pickup.origin);
    if (room == nullptr)
        return std::nullopt;
    const std::int64_t x0 = pickup.origin.x;
    const std::int64_t y0 = pickup.origin.y;
    const auto steps = [](const std::int64_t from, const std::int64_t low, const std::int64_t high) {
        // Lattice steps k with low <= from + k * step <= high.
        const auto floor_div = [](const std::int64_t value, const std::int64_t divisor) {
            return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
        };
        return std::pair<std::int64_t, std::int64_t>{-floor_div(from - low, kPickupStationStep),
                                                     floor_div(high - from, kPickupStationStep)};
    };
    const auto [first_i, last_i] = steps(x0, room->bounds.min.x, room->bounds.max.x);
    const auto [first_j, last_j] = steps(y0, room->bounds.min.y, room->bounds.max.y);
    struct Step {
        std::int64_t distance;
        std::int64_t i;
        std::int64_t j;
    };
    std::vector<Step> order;
    for (std::int64_t i = first_i; i <= last_i; ++i) {
        for (std::int64_t j = first_j; j <= last_j; ++j)
            order.push_back({i * i + j * j, i, j});
    }
    std::sort(order.begin(), order.end(), [](const Step &left, const Step &right) {
        if (left.distance != right.distance)
            return left.distance < right.distance;
        if (left.i != right.i)
            return left.i < right.i;
        return left.j < right.j;
    });
    for (const Step &step : order) {
        const std::int64_t x = x0 + step.i * kPickupStationStep;
        const std::int64_t y = y0 + step.j * kPickupStationStep;
        const std::optional<GridCoord> support = pickup_station_support(level, *room, x, y, hull);
        if (!support)
            continue;
        const HullFootprint footprint = pickup_footprint(x, y, hull);
        if (!pickup_station_clear_of_entities(level, pickup_index, footprint,
                                              pickup_fall_column(level.spec.game, footprint, *support, hull)))
            continue;
        return GridVec3{checked_grid_coord(x), checked_grid_coord(y), checked_grid_add(*support, kPickupSpawnLift)};
    }
    return std::nullopt;
}

} // namespace

bool is_multiplayer_map_start(const Layout &layout, const Placement &placement) noexcept {
    return placement.role == PlacementRole::player_start && layout.spec.mode != GameMode::single_player &&
           layout.spec.game != Game::quake3;
}

bool add_multiplayer_player_start(Layout &level) {
    if (!level.spec.switches.on(StageSwitch::r1s_mp_player_start))
        return false;
    if ((level.spec.game != Game::quake1 && level.spec.game != Game::quake2) ||
        level.spec.mode == GameMode::single_player || level.rooms.empty())
        return false;
    if (std::any_of(level.placements.begin(), level.placements.end(),
                    [](const Placement &placement) { return placement.role == PlacementRole::player_start; }))
        throw std::logic_error("a multiplayer layout already has an info_player_start placement");

    // The start room (the "start" tag; the first room in both generator paths) first, then every other room in
    // order, so a crowded start room cannot leave the map without its start.
    std::vector<const Room *> rooms;
    rooms.reserve(level.rooms.size());
    for (const Room &room : level.rooms) {
        if (has_tag(room, "start"))
            rooms.push_back(&room);
    }
    for (const Room &room : level.rooms) {
        if (!has_tag(room, "start"))
            rooms.push_back(&room);
    }
    // A lit station (a light of its room in clear view, so the player, and the Quake 2 match-end camera, are not
    // left in the dark) nearest the room centre wins; ties keep the first in lattice order. Every room is searched
    // for a lit station before an unlit one is accepted.
    for (const bool need_light : {true, false}) {
        for (const Room *room : rooms) {
            const std::vector<StartStation> stations = start_stations(level, *room);
            const StartStation *best = nullptr;
            for (const StartStation &station : stations) {
                if (need_light && !station.lit)
                    continue;
                if (best == nullptr || station.centre_distance < best->centre_distance)
                    best = &station;
            }
            if (best == nullptr)
                continue;
            Placement start;
            start.id = next_placement_id(level);
            start.role = PlacementRole::player_start;
            start.origin = best->origin;
            start.angle_degrees = yaw_towards(best->origin, room->bounds.center());
            level.placements.push_back(std::move(start));
            return true;
        }
    }
    throw std::runtime_error("r1s-mp-player-start: no room has a supported, clear station for the map-command "
                             "info_player_start");
}

bool add_single_player_coop_starts(Layout &level) {
    if (!level.spec.switches.on(StageSwitch::r1s_sp_coop_starts))
        return false;
    // Quake 1 and Quake 2 single player only: Quake 3 has no co-op mode and no info_player_coop (its spawn table,
    // code/game/g_spawn.c:199-201, holds info_player_start, info_player_deathmatch and info_player_intermission).
    if ((level.spec.game != Game::quake1 && level.spec.game != Game::quake2) ||
        level.spec.mode != GameMode::single_player || level.rooms.empty())
        return false;
    const Placement *start = nullptr;
    for (const Placement &placement : level.placements) {
        if (placement.role == PlacementRole::coop_start)
            throw std::logic_error("r1s-sp-coop-starts: the layout already has an info_player_coop placement");
        if (placement.role != PlacementRole::player_start)
            continue;
        if (start != nullptr)
            throw std::logic_error("r1s-sp-coop-starts: a single-player layout has more than one info_player_start");
        start = &placement;
    }
    if (start == nullptr)
        throw std::logic_error("r1s-sp-coop-starts: a single-player layout has no info_player_start");
    // Copied before any push_back, which may move the placements.
    const GridVec3 start_origin = start->origin;
    const std::optional<std::int16_t> start_angle = start->angle_degrees;
    const std::vector<const Room *> rooms = coop_start_rooms(level, *start);

    for (std::size_t added = 0; added < kSinglePlayerCoopStarts; ++added) {
        // The stations are recomputed after every start, so each new one keeps clear of the ones before it. The start
        // room is searched first, lit stations (a light of the room in clear view) before unlit ones, and a joined
        // room only when the start room has no clear station left; within a room the station nearest the start wins,
        // ties keeping the first in x-major lattice order.
        std::optional<CoopStation> chosen;
        for (std::size_t index = 0; index < rooms.size() && !chosen; ++index) {
            const std::vector<StartStation> stations = start_stations(level, *rooms[index]);
            for (const bool need_light : {true, false}) {
                for (const StartStation &station : stations) {
                    if (need_light && !station.lit)
                        continue;
                    const std::int64_t dx = static_cast<std::int64_t>(station.origin.x) - start_origin.x;
                    const std::int64_t dy = static_cast<std::int64_t>(station.origin.y) - start_origin.y;
                    const std::int64_t distance = dx * dx + dy * dy;
                    if (!chosen || distance < chosen->start_distance)
                        chosen = CoopStation{station.origin, station.lit, distance};
                }
                if (chosen)
                    break;
            }
        }
        if (!chosen)
            throw std::runtime_error("r1s-sp-coop-starts: the start room and the rooms joined to it have fewer than " +
                                     std::to_string(kSinglePlayerCoopStarts) +
                                     " supported, clear stations for the info_player_coop");
        Placement coop;
        coop.id = next_placement_id(level);
        coop.role = PlacementRole::coop_start;
        coop.origin = chosen->origin;
        // The start's facing: Quake 1 PutClientInServer copies the spot's angles (progs1996 PutClientInServer
        // statements 6134-6136; quakec/client.qc:783), as Quake 2 SelectSpawnPoint does (game/p_client.c:912).
        coop.angle_degrees = start_angle;
        level.placements.push_back(std::move(coop));
    }
    return true;
}

int quake3_ctf_team_begin_spots_per_team(const PromptSpec &spec) noexcept {
    // The same floor of two players as add_arena_spawns(); a CTF request is normalized to at least six.
    const int players = std::max(spec.target_players, 2);
    return players / 2 + players % 2;
}

bool add_quake3_ctf_team_begin_spots(Layout &level) {
    if (!level.spec.switches.on(StageSwitch::r1s_q3_ctf_team_begin))
        return false;
    // Quake 3 CTF only: Quake 1 has no stock CTF and Quake 2 CTF reads its info_player_team1/2 starts differently
    // (later causes); the team-begin classes are read only when g_gametype >= GT_CTF (code/game/g_client.c:1076).
    if (level.spec.game != Game::quake3 || level.spec.mode != GameMode::capture_the_flag || level.rooms.size() < 2)
        return false;
    const auto per_team = static_cast<std::size_t>(quake3_ctf_team_begin_spots_per_team(level.spec));
    bool added_any = false;
    for (const Team team : {Team::red, Team::blue}) {
        const std::vector<const Room *> rooms = team_begin_rooms(level, team);
        const Room &base = *rooms.front();
        // The team's first begin spot anchors the others (they gather beside it); without one, the team's flag does.
        std::optional<GridVec3> anchor;
        std::size_t existing = 0;
        for (const Placement &placement : level.placements) {
            if (placement.role != PlacementRole::team_initial_spawn || placement.team != team)
                continue;
            if (!anchor)
                anchor = placement.origin;
            ++existing;
        }
        if (!anchor) {
            for (const Placement &placement : level.placements) {
                if (placement.role == PlacementRole::flag && placement.team == team) {
                    anchor = placement.origin;
                    break;
                }
            }
        }
        for (std::size_t count = existing; count < per_team; ++count) {
            // The stations are recomputed after every spot, so each new one keeps clear of the ones before it. The
            // base is searched first, lit stations (a light of the room in clear view) before unlit ones, and a
            // joined room of the team's half only when the base has no clear station left; within a room the station
            // nearest the anchor wins, ties keeping the first in x-major lattice order.
            std::optional<StartStation> chosen;
            std::int64_t chosen_distance = 0;
            for (std::size_t index = 0; index < rooms.size() && !chosen; ++index) {
                const std::vector<StartStation> stations = start_stations(level, *rooms[index]);
                for (const bool need_light : {true, false}) {
                    for (const StartStation &station : stations) {
                        if (need_light && !station.lit)
                            continue;
                        const std::int64_t dx = static_cast<std::int64_t>(station.origin.x) - anchor->x;
                        const std::int64_t dy = static_cast<std::int64_t>(station.origin.y) - anchor->y;
                        const std::int64_t distance = dx * dx + dy * dy;
                        if (!chosen || distance < chosen_distance) {
                            chosen = station;
                            chosen_distance = distance;
                        }
                    }
                    if (chosen)
                        break;
                }
            }
            if (!chosen)
                throw std::runtime_error(
                    std::string("r1s-q3-ctf-team-begin: the ") + (team == Team::red ? "red" : "blue") +
                    " base and the rooms of its half joined to it have fewer than " + std::to_string(per_team) +
                    " supported, clear stations for the team-begin spots");
            Placement spot;
            spot.id = next_placement_id(level);
            spot.role = PlacementRole::team_initial_spawn;
            spot.origin = chosen->origin;
            spot.team = team;
            // Facing into the base: the centre of the base room (SelectCTFSpawnPoint copies the spot's angles,
            // code/game/g_team.c:1044). A spot of a joined room faces the base centre too.
            spot.angle_degrees = yaw_towards(chosen->origin, base.bounds.center());
            level.placements.push_back(std::move(spot));
            added_any = true;
        }
    }
    return added_any;
}

std::optional<std::string> monster_hull_problem(const Layout &layout, const Placement &monster) {
    if (monster.role != PlacementRole::monster)
        return std::nullopt;
    const std::optional<game_profile::VariantTable> table =
        game_profile::variant_table(layout.spec.game, PlacementRole::monster);
    if (!table || monster.variant >= table->classnames.size() ||
        !game_profile::monster_hull(layout.spec.game, table->classnames[monster.variant]))
        return std::string("its class has no spawn box in the game profile (data/profiles monster_hulls)");
    return monster_slot_problem(layout, monster, monster_class_hull(layout, monster));
}

GridBounds monster_spawn_box(const Layout &layout, const Placement &monster) {
    if (monster.role != PlacementRole::monster)
        throw std::invalid_argument("monster_spawn_box needs a monster placement");
    const game_profile::MonsterHull hull = monster_class_hull(layout, monster);
    return {{checked_grid_add(monster.origin.x, hull.mins.x), checked_grid_add(monster.origin.y, hull.mins.y),
             checked_grid_add(monster.origin.z, hull.mins.z)},
            {checked_grid_add(monster.origin.x, hull.maxs.x), checked_grid_add(monster.origin.y, hull.maxs.y),
             checked_grid_add(monster.origin.z, hull.maxs.z)}};
}

MonsterHullSupport support_monster_hulls(Layout &level) {
    MonsterHullSupport outcome;
    if (!level.spec.switches.on(StageSwitch::r1s_monster_hull_support))
        return outcome;
    // Quake 1 and Quake 2 single player only: only populate_single_player() places monsters (Quake 3 single-player
    // prompts are arena maps without monsters).
    if ((level.spec.game != Game::quake1 && level.spec.game != Game::quake2) ||
        level.spec.mode != GameMode::single_player)
        return outcome;
    outcome.applied = true;
    for (std::size_t index = 0; index < level.placements.size();) {
        if (level.placements[index].role != PlacementRole::monster) {
            ++index;
            continue;
        }
        const game_profile::MonsterHull hull = monster_class_hull(level, level.placements[index]);
        if (!monster_slot_problem(level, level.placements[index], hull)) {
            ++outcome.kept;
            ++index;
            continue;
        }
        if (const std::optional<GridVec3> station = nearest_monster_station(level, index, hull)) {
            // Class (variant), facing and id are kept; only the origin moves.
            level.placements[index].origin = *station;
            ++outcome.moved;
            ++index;
            continue;
        }
        level.placements.erase(level.placements.begin() + static_cast<std::ptrdiff_t>(index));
        ++outcome.left_out;
    }
    return outcome;
}

bool is_pickup_role(const PlacementRole role) noexcept {
    return role == PlacementRole::weapon || role == PlacementRole::health || role == PlacementRole::armor;
}

std::optional<std::string> pickup_hull_problem(const Layout &layout, const Placement &pickup) {
    if (!is_pickup_role(pickup.role) || (layout.spec.game != Game::quake1 && layout.spec.game != Game::quake2))
        return std::nullopt;
    const std::optional<game_profile::PickupHull> hull = pickup_class_hull_if_any(layout, pickup);
    if (!hull)
        return std::string("its class has no drop box in the game profile (data/profiles pickup_hulls)");
    return pickup_slot_problem(layout, pickup, *hull);
}

GridBounds pickup_drop_box(const Layout &layout, const Placement &pickup) {
    if (!is_pickup_role(pickup.role))
        throw std::invalid_argument("pickup_drop_box needs a weapon, health or armor placement");
    const game_profile::PickupHull hull = pickup_class_hull(layout, pickup);
    return {{checked_grid_add(pickup.origin.x, hull.mins.x), checked_grid_add(pickup.origin.y, hull.mins.y),
             checked_grid_add(pickup.origin.z, hull.mins.z)},
            {checked_grid_add(pickup.origin.x, hull.maxs.x), checked_grid_add(pickup.origin.y, hull.maxs.y),
             checked_grid_add(pickup.origin.z, hull.maxs.z)}};
}

PickupHullSupport support_pickup_hulls(Layout &level) {
    PickupHullSupport outcome;
    if (!level.spec.switches.on(StageSwitch::r1s_sp_pickup_hull_support))
        return outcome;
    // Quake 1 and Quake 2 single player only: populate_single_player() places these pickups (Quake 3 single-player
    // prompts are arena maps, and multiplayer maps are another sub-lane).
    if ((level.spec.game != Game::quake1 && level.spec.game != Game::quake2) ||
        level.spec.mode != GameMode::single_player)
        return outcome;
    outcome.applied = true;
    for (std::size_t index = 0; index < level.placements.size();) {
        if (!is_pickup_role(level.placements[index].role)) {
            ++index;
            continue;
        }
        const game_profile::PickupHull hull = pickup_class_hull(level, level.placements[index]);
        if (!pickup_slot_problem(level, level.placements[index], hull)) {
            ++outcome.kept;
            ++index;
            continue;
        }
        if (const std::optional<GridVec3> station = nearest_pickup_station(level, index, hull)) {
            // Role, class (variant), facing and id are kept; only the origin moves.
            level.placements[index].origin = *station;
            ++outcome.moved;
            ++index;
            continue;
        }
        level.placements.erase(level.placements.begin() + static_cast<std::ptrdiff_t>(index));
        ++outcome.left_out;
    }
    return outcome;
}

void EntityAllocator::populate(Layout &level) const {
    populate_before_station_binding(level);
    // Change r1s-mp-player-start: last of all, after the lights and the Quake 1 camera, so every earlier placement
    // keeps its id and position and the start keeps clear of all of them. The compact Quake 2 builder calls
    // populate_before_station_binding() and adds the start after its own binding and lights instead.
    add_multiplayer_player_start(level);
    // Change r1s-sp-coop-starts: the single-player co-op starts, last of all for the same reason (single-player
    // layouts get no multiplayer start, so the two never meet).
    add_single_player_coop_starts(level);
    // Change r1s-q3-ctf-team-begin: the Quake 3 CTF team-begin spots, last of all for the same reason (Quake 3 layouts
    // get none of the three placements above).
    add_quake3_ctf_team_begin_spots(level);
}

void EntityAllocator::populate_before_station_binding(Layout &level) const {
    level.placements.clear();
    if (level.rooms.empty())
        return;

    if (level.spec.mode == GameMode::single_player && level.spec.game != Game::quake3) {
        populate_single_player(level);
        // Change r1s-monster-hull-support: each monster's class box must be clear and stand on support; a monster
        // whose slot fails moves to the nearest station that holds it. Before the lights and every later placement,
        // which keep their ids and avoid the monsters where they stand. Does nothing while the switch is off.
        support_monster_hulls(level);
        // Change r1s-sp-pickup-hull-support: each pickup's drop box must be clear and rest on support; a pickup whose
        // slot fails moves to the nearest station that holds it, clear of the monsters where they now stand. Before the
        // lights and every later placement, which keep their ids and avoid the pickups where they stand. Does nothing
        // while the switch is off.
        support_pickup_hulls(level);
    } else {
        populate_arena(level);
    }
    add_lighting(level);
    // Last, so every earlier placement keeps its id and position, and the camera can keep clear of the lights.
    if (level.spec.game == Game::quake1 && level.spec.switches.on(StageSwitch::r1s_q1_intermission))
        add_quake1_intermission(level);
}

} // namespace qmg
