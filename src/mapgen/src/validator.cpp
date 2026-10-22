#include "qmg/validator.hpp"

#include "qmg/entities.hpp"
#include "qmg/generated/game_profile_tables.hpp"
#include "qmg/switches.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace qmg {
namespace {

constexpr GridCoord kMaxCoordinate = 32768;

std::int64_t magnitude(const GridCoord value) {
    const std::int64_t widened = value;
    return widened < 0 ? -widened : widened;
}

bool within_world(const GridVec3 value) {
    return magnitude(value.x) <= kMaxCoordinate && magnitude(value.y) <= kMaxCoordinate &&
           magnitude(value.z) <= kMaxCoordinate;
}

bool within_world(const GridBounds &bounds, const GridCoord margin = 0) {
    if (!bounds.valid() || margin < 0 || margin > kMaxCoordinate)
        return false;
    const std::int64_t limit = static_cast<std::int64_t>(kMaxCoordinate) - margin;
    const auto within = [limit](const GridCoord value) { return magnitude(value) <= limit; };
    return within(bounds.min.x) && within(bounds.min.y) && within(bounds.min.z) && within(bounds.max.x) &&
           within(bounds.max.y) && within(bounds.max.z);
}

bool overlaps(const GridBounds &a, const GridBounds &b) {
    return a.valid() && b.valid() && a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

bool contains(const GridBounds &outer, const GridBounds &inner) {
    return outer.valid() && inner.valid() && inner.min.x >= outer.min.x && inner.min.y >= outer.min.y &&
           inner.min.z >= outer.min.z && inner.max.x <= outer.max.x && inner.max.y <= outer.max.y &&
           inner.max.z <= outer.max.z;
}

std::array<std::int64_t, 3> intersection_extents(const GridBounds &a, const GridBounds &b) {
    return {
        static_cast<std::int64_t>(std::min(a.max.x, b.max.x)) - std::max(a.min.x, b.min.x),
        static_cast<std::int64_t>(std::min(a.max.y, b.max.y)) - std::max(a.min.y, b.min.y),
        static_cast<std::int64_t>(std::min(a.max.z, b.max.z)) - std::max(a.min.z, b.min.z),
    };
}

bool face_connected(const GridBounds &a, const GridBounds &b) {
    const std::array<std::int64_t, 3> extents = intersection_extents(a, b);
    return std::count(extents.begin(), extents.end(), 0) == 1 &&
           std::count_if(extents.begin(), extents.end(), [](const std::int64_t extent) { return extent > 0; }) == 2;
}

bool passage_volumes_connected(const GridBounds &a, const GridBounds &b) {
    const std::array<std::int64_t, 3> extents = intersection_extents(a, b);
    if (std::all_of(extents.begin(), extents.end(), [](const std::int64_t extent) { return extent > 0; }))
        return true;
    return std::count(extents.begin(), extents.end(), 0) == 1 &&
           std::count_if(extents.begin(), extents.end(), [](const std::int64_t extent) { return extent > 0; }) == 2;
}

bool valid_passage_chain(const GridBounds &from, const GridBounds &to, const std::vector<GridBounds> &passages) {
    if (passages.empty() || std::any_of(passages.begin(), passages.end(), [](const GridBounds &passage) {
            return !passage.valid() || !within_world(passage);
        })) {
        return false;
    }

    std::vector<std::vector<std::size_t>> adjacency(passages.size());
    for (std::size_t first = 0; first < passages.size(); ++first) {
        for (std::size_t second = first + 1; second < passages.size(); ++second) {
            if (passage_volumes_connected(passages[first], passages[second])) {
                adjacency[first].push_back(second);
                adjacency[second].push_back(first);
            }
        }
    }

    std::vector<bool> reached(passages.size(), false);
    std::queue<std::size_t> frontier;
    for (std::size_t index = 0; index < passages.size(); ++index) {
        if (face_connected(from, passages[index])) {
            reached[index] = true;
            frontier.push(index);
        }
    }
    if (frontier.empty())
        return false;

    bool reaches_destination = false;
    while (!frontier.empty()) {
        const std::size_t current = frontier.front();
        frontier.pop();
        reaches_destination = reaches_destination || face_connected(to, passages[current]);
        for (const std::size_t next : adjacency[current]) {
            if (!reached[next]) {
                reached[next] = true;
                frontier.push(next);
            }
        }
    }
    return reaches_destination && std::find(reached.begin(), reached.end(), false) == reached.end();
}

void issue(ValidationResult &result, const Severity severity, std::string code, std::string message) {
    result.issues.push_back({severity, std::move(code), std::move(message)});
}

bool valid_lighting_style(const LightingStyle value) {
    switch (value) {
    case LightingStyle::low_key:
    case LightingStyle::balanced:
    case LightingStyle::high_contrast:
        return true;
    }
    return false;
}

bool valid_material(const MaterialRole value) {
    switch (value) {
    case MaterialRole::floor:
    case MaterialRole::ceiling:
    case MaterialRole::wall:
    case MaterialRole::platform:
    case MaterialRole::stair:
    case MaterialRole::ramp:
    case MaterialRole::arch:
    case MaterialRole::curve:
    case MaterialRole::liquid:
    case MaterialRole::door:
    case MaterialRole::lift:
    case MaterialRole::hidden:
    case MaterialRole::trigger:
        return true;
    }
    return false;
}

bool valid_purpose(const BrushPurpose value) {
    switch (value) {
    case BrushPurpose::structural:
    case BrushPurpose::raised_platform:
    case BrushPurpose::navigation_stair:
    case BrushPurpose::ramp:
    case BrushPurpose::arch:
    case BrushPurpose::curve:
    case BrushPurpose::liquid:
    case BrushPurpose::trigger_volume:
    case BrushPurpose::door:
    case BrushPurpose::lift:
    case BrushPurpose::hidden:
        return true;
    }
    return false;
}

bool valid_ownership(const BrushOwnership value) {
    switch (value) {
    case BrushOwnership::structural:
    case BrushOwnership::detail:
    case BrushOwnership::placement:
        return true;
    }
    return false;
}

bool valid_contents(const BrushContents value) {
    switch (value) {
    case BrushContents::solid:
    case BrushContents::water:
    case BrushContents::slime:
    case BrushContents::lava:
        return true;
    }
    return false;
}

bool valid_role(const PlacementRole value) {
    switch (value) {
    case PlacementRole::player_start:
    case PlacementRole::deathmatch_spawn:
    case PlacementRole::team_initial_spawn:
    case PlacementRole::team_respawn:
    case PlacementRole::weapon:
    case PlacementRole::health:
    case PlacementRole::armor:
    case PlacementRole::monster:
    case PlacementRole::flag:
    case PlacementRole::light:
    case PlacementRole::exit_trigger:
    case PlacementRole::exit_destination:
    case PlacementRole::door:
    case PlacementRole::door_trigger:
    case PlacementRole::door_destination:
    case PlacementRole::lift:
    case PlacementRole::lift_trigger:
    case PlacementRole::lift_destination:
    case PlacementRole::teleporter_trigger:
    case PlacementRole::teleporter_destination:
    case PlacementRole::intermission:
    case PlacementRole::coop_start:
        return true;
    }
    return false;
}

bool valid_team(const Team value) {
    switch (value) {
    case Team::red:
    case Team::blue:
        return true;
    }
    return false;
}

bool is_spawn(const PlacementRole role) {
    return role == PlacementRole::player_start || role == PlacementRole::deathmatch_spawn ||
           role == PlacementRole::team_initial_spawn || role == PlacementRole::team_respawn;
}

bool needs_team(const PlacementRole role) {
    return role == PlacementRole::team_initial_spawn || role == PlacementRole::team_respawn ||
           role == PlacementRole::flag;
}

bool may_have_volume(const PlacementRole role) {
    return role == PlacementRole::exit_trigger || role == PlacementRole::door || role == PlacementRole::door_trigger ||
           role == PlacementRole::lift || role == PlacementRole::lift_trigger ||
           role == PlacementRole::teleporter_trigger;
}

bool valid_link_roles(const PlacementRole source, const PlacementRole target) {
    if (source == PlacementRole::exit_trigger)
        return target == PlacementRole::exit_destination;
    if (source == PlacementRole::door || source == PlacementRole::door_trigger)
        return target == PlacementRole::door || target == PlacementRole::door_destination;
    if (source == PlacementRole::lift || source == PlacementRole::lift_trigger)
        return target == PlacementRole::lift || target == PlacementRole::lift_destination;
    if (source == PlacementRole::teleporter_trigger)
        return target == PlacementRole::teleporter_destination;
    return false;
}

std::uint16_t variant_count(const Game game, const PlacementRole role) {
    if (role == PlacementRole::weapon)
        return game == Game::quake1 ? 5 : 7;
    if (role == PlacementRole::monster)
        return game == Game::quake3 ? 0 : 5;
    return 1;
}

bool has_role(const Layout &level, const PlacementRole role) {
    return std::any_of(level.placements.begin(), level.placements.end(),
                       [role](const Placement &placement) { return placement.role == role; });
}

std::size_t count_role(const Layout &level, const PlacementRole role) {
    return static_cast<std::size_t>(
        std::count_if(level.placements.begin(), level.placements.end(),
                      [role](const Placement &placement) { return placement.role == role; }));
}

// Change r1s-mp-player-start: the map-command info_player_start of a Quake 1 or Quake 2 multiplayer map is never a
// deathmatch, duel or CTF spawn (deathmatch spawn selection does not read it; see src/entities.cpp), so it does not
// count toward the spawns a match needs.
std::size_t spawn_count(const Layout &level) {
    return static_cast<std::size_t>(
        std::count_if(level.placements.begin(), level.placements.end(), [&level](const Placement &placement) {
            return is_spawn(placement.role) && !is_multiplayer_map_start(level, placement);
        }));
}

bool has_team_role(const Layout &level, const PlacementRole role, const Team team) {
    return std::any_of(level.placements.begin(), level.placements.end(), [role, team](const Placement &placement) {
        return placement.role == role && placement.team == team;
    });
}

} // namespace

ConnectionRouteKey connection_route_key(const Connection &connection) {
    std::vector<GridBounds> passages = connection.passage_volumes;
    std::sort(passages.begin(), passages.end());
    passages.erase(std::unique(passages.begin(), passages.end()), passages.end());
    return {std::min(connection.from_room, connection.to_room), std::max(connection.from_room, connection.to_room),
            std::move(passages)};
}

bool connection_has_valid_passage(const Room &from, const Room &to, const Connection &connection) {
    return valid_passage_chain(from.bounds, to.bounds, connection.passage_volumes);
}

ValidationResult LayoutValidator::validate(const Layout &level) const {
    ValidationResult result;
    if (!valid_lighting_style(level.lighting_style))
        issue(result, Severity::error, "invalid_lighting_style", "the layout has an invalid lighting style");
    if (level.rooms.size() < 2)
        issue(result, Severity::error, "room_count", "a playable level needs at least two rooms");

    std::set<RoomId> room_ids;
    for (const Room &room : level.rooms) {
        if (!room.id) {
            issue(result, Severity::error, "invalid_room_id", "a room has the reserved zero identifier");
        } else if (!room_ids.insert(room.id).second) {
            issue(result, Severity::error, "duplicate_room_id",
                  "room id " + std::to_string(room.id.value) + " is duplicated");
        }
        if (!room.bounds.valid()) {
            issue(result, Severity::error, "invalid_room_bounds",
                  "room " + std::to_string(room.id.value) + " has inverted or empty bounds");
            continue;
        }
        if (!within_world(room.bounds, 32)) {
            issue(result, Severity::error, "world_bounds",
                  "room " + std::to_string(room.id.value) + " exceeds the conservative map coordinate limit");
        }
        const std::int64_t size_x = static_cast<std::int64_t>(room.bounds.max.x) - room.bounds.min.x;
        const std::int64_t size_y = static_cast<std::int64_t>(room.bounds.max.y) - room.bounds.min.y;
        const std::int64_t size_z = static_cast<std::int64_t>(room.bounds.max.z) - room.bounds.min.z;
        if (size_x < 384 || size_y < 384 || size_z < 192) {
            issue(result, Severity::error, "room_clearance",
                  "room " + std::to_string(room.id.value) + " is too small for reliable combat navigation");
        }
    }

    for (std::size_t first = 0; first < level.rooms.size(); ++first) {
        for (std::size_t second = first + 1; second < level.rooms.size(); ++second) {
            if (overlaps(level.rooms[first].bounds, level.rooms[second].bounds)) {
                issue(result, Severity::error, "room_overlap",
                      "rooms " + std::to_string(level.rooms[first].id.value) + " and " +
                          std::to_string(level.rooms[second].id.value) + " occupy the same volume");
            }
        }
    }

    std::vector<std::vector<std::size_t>> adjacency(level.rooms.size());
    std::set<ConnectionId> connection_ids;
    std::set<ConnectionRouteKey> unique_routes;
    for (const Connection &connection : level.connections) {
        if (!connection.id) {
            issue(result, Severity::error, "invalid_connection_id", "a connection has the reserved zero identifier");
        } else if (!connection_ids.insert(connection.id).second) {
            issue(result, Severity::error, "duplicate_connection_id",
                  "connection id " + std::to_string(connection.id.value) + " is duplicated");
        }
        const std::optional<std::size_t> from_index = room_index(level, connection.from_room);
        const std::optional<std::size_t> to_index = room_index(level, connection.to_room);
        if (!connection.from_room || !connection.to_room || !from_index || !to_index ||
            connection.from_room == connection.to_room) {
            issue(result, Severity::error, "invalid_connection", "a connection references an invalid room id");
            continue;
        }

        if (!unique_routes.insert(connection_route_key(connection)).second) {
            issue(result, Severity::error, "duplicate_connection",
                  "a connection repeats the same room pair and declared passage geometry");
            continue;
        }
        if (connection.width < 96 || connection.height < 128) {
            issue(result, Severity::error, "corridor_clearance",
                  "a connection's compatibility dimensions are too small for player navigation");
        }
        if ((connection.width & 1) != 0) {
            issue(result, Severity::error, "connection_centering",
                  "a connection's compatibility width must be even on the integer grid");
        }

        const bool passage_valid = valid_passage_chain(level.rooms[*from_index].bounds, level.rooms[*to_index].bounds,
                                                       connection.passage_volumes);
        if (!passage_valid) {
            issue(result, Severity::error, "invalid_passage",
                  "connection " + std::to_string(connection.id.value) +
                      " does not form a face-connected passage volume chain between its rooms");
            continue;
        }
        adjacency[*from_index].push_back(*to_index);
        adjacency[*to_index].push_back(*from_index);
    }

    if (!level.rooms.empty()) {
        std::vector<bool> visited(level.rooms.size(), false);
        std::queue<std::size_t> frontier;
        visited.front() = true;
        frontier.push(0);
        while (!frontier.empty()) {
            const std::size_t current = frontier.front();
            frontier.pop();
            for (const std::size_t next : adjacency[current]) {
                if (!visited[next]) {
                    visited[next] = true;
                    frontier.push(next);
                }
            }
        }
        if (std::find(visited.begin(), visited.end(), false) != visited.end())
            issue(result, Severity::error, "disconnected_graph", "not every room is reachable from the player start");
    }

    const bool native_single_player = level.spec.mode == GameMode::single_player && level.spec.game != Game::quake3;
    const std::size_t minimum_loop_rooms = native_single_player ? 5 : 4;
    if (level.spec.prefer_loops && level.rooms.size() >= minimum_loop_rooms &&
        level.connections.size() < level.rooms.size()) {
        issue(result, Severity::warning, "missing_loop",
              "the prompt asks for route choice but the room graph has no cycle");
    }
    if (level.planned_verticality >= 0.3 &&
        std::none_of(level.solids.begin(), level.solids.end(),
                     [](const LayoutSolid &solid) { return solid.purpose == BrushPurpose::raised_platform; })) {
        issue(result, Severity::warning, "missing_verticality",
              "the semantic plan asks for height variation but no raised combat route exists");
    }

    const std::size_t expected_spawns =
        native_single_player ? 1U : static_cast<std::size_t>(std::max(level.spec.target_players, 2));
    if (spawn_count(level) < expected_spawns)
        issue(result, Severity::error, "missing_spawns",
              "the level has fewer player spawns than its target player count");

    if (native_single_player) {
        if (!has_role(level, PlacementRole::monster))
            issue(result, Severity::error, "missing_encounters", "single-player progression has no monster encounters");
        // Change r1s-monster-hull-support (switch r1s.monster_hull_support): a monster's class box must meet no solid
        // and stand fully on support, or droptofloor leaves it where it is and walkmove(0, 0) fails ("walkmonster in
        // wall", "in solid"; src/entities.cpp has the id-source reading). Reported as placement_in_solid, the code of
        // a point embedded in a solid, so the repair pass re-runs the allocator, which moves the monster.
        if (level.spec.switches.on(StageSwitch::r1s_monster_hull_support)) {
            for (const Placement &placement : level.placements) {
                if (placement.role != PlacementRole::monster)
                    continue;
                if (const std::optional<std::string> problem = monster_hull_problem(level, placement))
                    issue(result, Severity::error, "placement_in_solid",
                          "monster placement " + std::to_string(placement.id.value) +
                              " cannot spawn with its class's box: " + *problem);
            }
        }
        // Change r1s-sp-pickup-hull-support (switch r1s.sp_pickup_hull_support): a pickup's drop box must be clear and
        // rest on support, or droptofloor removes the item at spawn (Quake 1 PlaceItem "Bonus item fell out of level";
        // Quake 2 "droptofloor: startsolid"; src/entities.cpp has the id-source reading). Reported as
        // placement_in_solid for the same reason as the monsters above, so the repair pass re-runs the allocator, which
        // moves the pickup.
        if (level.spec.switches.on(StageSwitch::r1s_sp_pickup_hull_support)) {
            for (const Placement &placement : level.placements) {
                if (!is_pickup_role(placement.role))
                    continue;
                if (const std::optional<std::string> problem = pickup_hull_problem(level, placement))
                    issue(result, Severity::error, "placement_in_solid",
                          "pickup placement " + std::to_string(placement.id.value) +
                              " cannot spawn with its drop box: " + *problem);
            }
        }
        if (!has_role(level, PlacementRole::exit_trigger))
            issue(result, Severity::error, "missing_exit", "single-player progression has no level exit");
        if (level.spec.game == Game::quake1) {
            const bool valid_exit =
                std::any_of(level.placements.begin(), level.placements.end(), [](const Placement &p) {
                    return p.role == PlacementRole::exit_trigger && p.destination_map && !p.destination_map->empty();
                });
            if (!valid_exit)
                issue(result, Severity::error, "exit_entity_contract",
                      "Quake single-player needs a destination exit trigger");
        } else {
            const bool linked_exit =
                std::any_of(level.placements.begin(), level.placements.end(), [&level](const Placement &placement) {
                    if (placement.role != PlacementRole::exit_trigger || !placement.link)
                        return false;
                    const Placement *target = find_placement(level, *placement.link);
                    return target != nullptr && target->role == PlacementRole::exit_destination &&
                           target->destination_map && !target->destination_map->empty();
                });
            if (!linked_exit)
                issue(result, Severity::error, "exit_entity_contract",
                      "Quake II single-player needs a linked exit trigger and destination");
        }
    } else if (count_role(level, PlacementRole::weapon) < std::max<std::size_t>(2, level.rooms.size() / 2)) {
        issue(result, Severity::warning, "weapon_distribution", "multiplayer weapon coverage is sparse");
    }

    if (level.spec.mode == GameMode::capture_the_flag) {
        if (count_role(level, PlacementRole::flag) != 2 || !has_team_role(level, PlacementRole::flag, Team::red) ||
            !has_team_role(level, PlacementRole::flag, Team::blue)) {
            issue(result, Severity::error, "missing_objectives",
                  "capture-the-flag requires one typed objective for each team");
        }
        if (level.spec.game == Game::quake3 && (!has_team_role(level, PlacementRole::team_respawn, Team::red) ||
                                                !has_team_role(level, PlacementRole::team_respawn, Team::blue) ||
                                                !has_team_role(level, PlacementRole::team_initial_spawn, Team::red) ||
                                                !has_team_role(level, PlacementRole::team_initial_spawn, Team::blue) ||
                                                !has_role(level, PlacementRole::deathmatch_spawn))) {
            issue(result, Severity::error, "ctf_spawn_contract",
                  "Quake 3 CTF needs typed team initial, team respawn, and fallback starts");
        }
        // Change r1s-q3-ctf-team-begin (switch r1s.q3_ctf_team_begin): every player of a full team needs a begin spot
        // of its own, or SelectRandomTeamSpawnPoint hands a taken spot back and the match start telefrags
        // (code/game/g_team.c:983-1024; src/entities.cpp). The same code as the contract above, so the repair pass
        // re-runs the allocator, which adds the spots.
        if (level.spec.game == Game::quake3 && level.spec.switches.on(StageSwitch::r1s_q3_ctf_team_begin)) {
            const auto per_team = static_cast<std::size_t>(quake3_ctf_team_begin_spots_per_team(level.spec));
            for (const Team team : {Team::red, Team::blue}) {
                const auto count = static_cast<std::size_t>(
                    std::count_if(level.placements.begin(), level.placements.end(), [team](const Placement &placement) {
                        return placement.role == PlacementRole::team_initial_spawn && placement.team == team;
                    }));
                if (count < per_team)
                    issue(result, Severity::error, "ctf_spawn_contract",
                          std::string("Quake 3 CTF needs ") + std::to_string(per_team) + " " +
                              std::string(game_profile::team_classname(level.spec.game,
                                                                       PlacementRole::team_initial_spawn, team)) +
                              " team-begin spots, one per player of a full team; the layout has " +
                              std::to_string(count));
            }
        }
        if (level.spec.game == Game::quake2 && (!has_team_role(level, PlacementRole::team_initial_spawn, Team::red) ||
                                                !has_team_role(level, PlacementRole::team_initial_spawn, Team::blue) ||
                                                !has_role(level, PlacementRole::deathmatch_spawn))) {
            issue(result, Severity::error, "ctf_spawn_contract",
                  "Quake 2 CTF needs typed starts for both teams and deathmatch respawns");
        }
        if (level.spec.game == Game::quake1) {
            issue(result, Severity::warning, "q1_ctf_mod_profile",
                  "Quake 1 has no stock CTF entity contract; lowering uses a common mod convention");
        }
    }

    std::set<SolidId> solid_ids;
    std::vector<std::optional<GridBounds>> solid_bounds(level.solids.size());
    for (std::size_t index = 0; index < level.solids.size(); ++index) {
        const LayoutSolid &solid = level.solids[index];
        if (!solid.id) {
            issue(result, Severity::error, "invalid_solid_id", "a layout solid has the reserved zero identifier");
        } else if (!solid_ids.insert(solid.id).second) {
            issue(result, Severity::error, "duplicate_solid_id",
                  "solid id " + std::to_string(solid.id.value) + " is duplicated");
        }
        if (!valid_material(solid.material))
            issue(result, Severity::error, "invalid_material_role", "a layout solid has an invalid material role");
        if (!valid_purpose(solid.purpose))
            issue(result, Severity::error, "invalid_brush_purpose", "a layout solid has an invalid brush purpose");
        if (!valid_ownership(solid.ownership))
            issue(result, Severity::error, "invalid_brush_ownership", "a layout solid has invalid ownership");
        if (!valid_contents(solid.contents))
            issue(result, Severity::error, "invalid_brush_contents", "a layout solid has invalid contents");
        const bool has_liquid_semantics = solid.contents != BrushContents::solid ||
                                          solid.material == MaterialRole::liquid ||
                                          solid.purpose == BrushPurpose::liquid;
        const bool valid_liquid_semantics = solid.contents != BrushContents::solid &&
                                            solid.material == MaterialRole::liquid &&
                                            solid.purpose == BrushPurpose::liquid;
        if (has_liquid_semantics && !valid_liquid_semantics) {
            issue(result, Severity::error, "invalid_liquid_contract",
                  "liquid brush contents, purpose, and material role must agree");
        }

        const BrushGeometryValidation geometry = validate_brush_geometry(solid.geometry);
        if (!geometry.ok()) {
            issue(result, Severity::error, "invalid_solid_geometry",
                  "solid " + std::to_string(solid.id.value) + " is not a bounded closed convex volume");
            continue;
        }
        solid_bounds[index] = geometry.bounds;
        if (!within_world(*geometry.bounds))
            issue(result, Severity::error, "world_bounds", "a layout solid exceeds the map coordinate limit");
    }

    std::set<PlacementId> placement_ids;
    for (const Placement &placement : level.placements) {
        if (!placement.id) {
            issue(result, Severity::error, "invalid_placement_id", "a placement has the reserved zero identifier");
        } else if (!placement_ids.insert(placement.id).second) {
            issue(result, Severity::error, "duplicate_placement_id",
                  "placement id " + std::to_string(placement.id.value) + " is duplicated");
        }
    }

    for (std::size_t index = 0; index < level.solids.size(); ++index) {
        const LayoutSolid &solid = level.solids[index];
        if (solid.ownership == BrushOwnership::placement) {
            const Placement *owner = solid.owner ? find_placement(level, *solid.owner) : nullptr;
            if (owner == nullptr || !may_have_volume(owner->role) ||
                (owner->volume && solid_bounds[index] && !contains(*owner->volume, *solid_bounds[index]))) {
                issue(result, Severity::error, "invalid_solid_owner",
                      "a placement-owned solid has no valid containing placement owner");
            }
        } else if (solid.owner) {
            issue(result, Severity::error, "invalid_solid_owner",
                  "a structural or detail solid cannot reference a placement owner");
        }
    }

    for (const Placement &placement : level.placements) {
        if (!valid_role(placement.role))
            issue(result, Severity::error, "invalid_placement_role", "a placement has an invalid typed role");
        if (placement.team && !valid_team(*placement.team))
            issue(result, Severity::error, "invalid_team", "a placement has an invalid team");
        if (needs_team(placement.role) != placement.team.has_value())
            issue(result, Severity::error, "invalid_team_contract", "a placement's team does not match its role");
        if (placement.variant >= variant_count(level.spec.game, placement.role))
            issue(result, Severity::error, "invalid_variant",
                  "a placement variant is unsupported for this game and role");
        if (placement.angle_degrees && (*placement.angle_degrees < 0 || *placement.angle_degrees >= 360))
            issue(result, Severity::error, "invalid_angle", "a placement angle must be from 0 through 359 degrees");
        if (placement.pitch_degrees && (placement.role != PlacementRole::intermission ||
                                        *placement.pitch_degrees < -90 || *placement.pitch_degrees > 90))
            issue(result, Severity::error, "invalid_pitch",
                  "only an intermission camera carries a pitch, from -90 through 90 degrees");
        if (placement.role == PlacementRole::intermission) {
            // Change r1s-q1-intermission: the Quake 1 info_intermission camera ("mangle" = pitch yaw roll).
            if (level.spec.game != Game::quake1)
                issue(result, Severity::error, "unsupported_intermission",
                      "intermission cameras are generated for Quake 1 only");
            if (!placement.angle_degrees || !placement.pitch_degrees)
                issue(result, Severity::error, "invalid_intermission",
                      "an intermission camera needs a yaw and a pitch");
        }
        // Change r1s-sp-coop-starts: info_player_coop is a Quake 1 and Quake 2 entity only (Quake 3's spawn table,
        // code/game/g_spawn.c:199-201, has none).
        if (placement.role == PlacementRole::coop_start && level.spec.game == Game::quake3)
            issue(result, Severity::error, "unsupported_coop_start",
                  "co-op starts are generated for Quake 1 and Quake 2 only");
        if (placement.role == PlacementRole::light) {
            if (!placement.light_intensity || *placement.light_intensity <= 0 || !placement.light_color ||
                !placement.light_color->valid()) {
                issue(result, Severity::error, "invalid_light", "a light needs positive intensity and valid RGB color");
            }
        } else if (placement.light_intensity || placement.light_color) {
            issue(result, Severity::error, "invalid_light", "only a light placement may carry light parameters");
        }
        if (placement.destination_map && placement.destination_map->empty())
            issue(result, Severity::error, "invalid_destination", "a destination map value cannot be empty");
        if (placement.link) {
            const Placement *target = find_placement(level, *placement.link);
            if (!*placement.link || target == nullptr || target->id == placement.id ||
                !valid_link_roles(placement.role, target->role)) {
                issue(result, Severity::error, "invalid_placement_link",
                      "a placement link must resolve to a different nonzero placement id");
            }
        }

        const bool owns_solid =
            std::any_of(level.solids.begin(), level.solids.end(), [&placement](const LayoutSolid &solid) {
                return solid.ownership == BrushOwnership::placement && solid.owner == placement.id;
            });
        if (placement.volume && !may_have_volume(placement.role))
            issue(result, Severity::error, "invalid_placement_volume", "this placement role cannot own a brush volume");
        if (may_have_volume(placement.role) && !placement.volume && !owns_solid)
            issue(result, Severity::error, "invalid_placement_volume",
                  "this brush placement has no volume or owned solid");

        if (placement.volume) {
            if (!placement.volume->valid()) {
                issue(result, Severity::error, "invalid_placement_volume", "a placement has invalid volume bounds");
                continue;
            }
            if (!within_world(*placement.volume))
                issue(result, Severity::error, "world_bounds", "a placement volume exceeds the map coordinate limit");
        } else if (!owns_solid && !within_world(placement.origin)) {
            issue(result, Severity::error, "world_bounds", "a point placement exceeds the map coordinate limit");
        }

        const bool inside_room = std::any_of(level.rooms.begin(), level.rooms.end(), [&placement](const Room &room) {
            return placement.volume ? contains(room.bounds, *placement.volume)
                                    : room.bounds.contains(placement.origin, 1);
        });
        // A point light can illuminate a sealed corridor directly. Gameplay
        // placements retain the room binding contract used by allocation/repair.
        const bool passage_light =
            placement.role == PlacementRole::light && !placement.volume &&
            std::any_of(level.connections.begin(), level.connections.end(), [&placement](const Connection &connection) {
                return std::any_of(
                    connection.passage_volumes.begin(), connection.passage_volumes.end(),
                    [&placement](const GridBounds &bounds) { return bounds.contains(placement.origin, 1); });
            });
        if (!inside_room && !owns_solid && !passage_light)
            issue(result, Severity::error, "placement_outside_hull",
                  "a placement is outside its permitted sealed room or light passage volume");

        if (!placement.volume && !owns_solid &&
            std::any_of(solid_bounds.begin(), solid_bounds.end(),
                        [&placement](const std::optional<GridBounds> &bounds) {
                            return bounds && bounds->contains(placement.origin);
                        })) {
            issue(result, Severity::error, "placement_in_solid",
                  "a point placement is embedded in a solid's aggregate bounds");
        }
    }

    for (std::size_t first = 0; first < level.placements.size(); ++first) {
        if (level.placements[first].volume || level.placements[first].role == PlacementRole::light)
            continue;
        for (std::size_t second = first + 1; second < level.placements.size(); ++second) {
            if (level.placements[second].volume || level.placements[second].role == PlacementRole::light)
                continue;
            const std::int64_t dx =
                static_cast<std::int64_t>(level.placements[first].origin.x) - level.placements[second].origin.x;
            const std::int64_t dy =
                static_cast<std::int64_t>(level.placements[first].origin.y) - level.placements[second].origin.y;
            const std::int64_t dz =
                static_cast<std::int64_t>(level.placements[first].origin.z) - level.placements[second].origin.z;
            if (std::abs(dx) < 24 && std::abs(dy) < 24 && std::abs(dz) < 24 && dx * dx + dy * dy + dz * dz < 24 * 24) {
                issue(result, Severity::error, "placement_overlap",
                      "placements " + std::to_string(level.placements[first].id.value) + " and " +
                          std::to_string(level.placements[second].id.value) + " overlap");
            }
        }
    }

    return result;
}

} // namespace qmg
