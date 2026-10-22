#include "qmg/repair.hpp"

#include "qmg/entities.hpp"
#include "qmg/validator.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace qmg {
namespace {

bool has_issue(const ValidationResult &validation, const std::string_view code) {
    return std::any_of(validation.issues.begin(), validation.issues.end(),
                       [code](const Issue &issue) { return issue.code == code; });
}

bool same_placements(const std::vector<Placement> &a, const std::vector<Placement> &b) { return a == b; }

bool connected(const Layout &level, const RoomId a, const RoomId b) {
    return std::any_of(level.connections.begin(), level.connections.end(), [a, b](const Connection &connection) {
        return (connection.from_room == a && connection.to_room == b) ||
               (connection.from_room == b && connection.to_room == a);
    });
}

bool aligned(const Room &a, const Room &b) {
    const GridVec3 first = a.bounds.center();
    const GridVec3 second = b.bounds.center();
    return first.x == second.x || first.y == second.y;
}

std::optional<int> side_between(const Room &room, const Room &other) {
    const GridVec3 a = room.bounds.center();
    const GridVec3 b = other.bounds.center();
    if (a.y == b.y)
        return b.x > a.x ? 0 : 1;
    if (a.x == b.x)
        return b.y > a.y ? 2 : 3;
    return std::nullopt;
}

std::int64_t room_gap(const Room &a, const Room &b) {
    const GridVec3 first = a.bounds.center();
    const GridVec3 second = b.bounds.center();
    if (first.y == second.y) {
        const Room &left = first.x < second.x ? a : b;
        const Room &right = first.x < second.x ? b : a;
        return static_cast<std::int64_t>(right.bounds.min.x) - left.bounds.max.x;
    }
    if (first.x == second.x) {
        const Room &south = first.y < second.y ? a : b;
        const Room &north = first.y < second.y ? b : a;
        return static_cast<std::int64_t>(north.bounds.min.y) - south.bounds.max.y;
    }
    return -1;
}

std::optional<GridBounds> axial_passage_volume(const Layout &level, const RoomId from, const RoomId to,
                                               const GridCoord width, const GridCoord height) {
    const Room *first = find_room(level, from);
    const Room *second = find_room(level, to);
    if (first == nullptr || second == nullptr)
        return std::nullopt;
    const GridVec3 first_center = first->bounds.center();
    const GridVec3 second_center = second->bounds.center();
    const GridCoord floor = std::max(first->bounds.min.z, second->bounds.min.z);
    const GridCoord ceiling = checked_grid_add(floor, height);
    if (first_center.y == second_center.y) {
        const Room &left = first_center.x < second_center.x ? *first : *second;
        const Room &right = first_center.x < second_center.x ? *second : *first;
        return GridBounds{{left.bounds.max.x, checked_grid_subtract(first_center.y, width / 2), floor},
                          {right.bounds.min.x, checked_grid_add(first_center.y, width / 2), ceiling}};
    }
    if (first_center.x == second_center.x) {
        const Room &south = first_center.y < second_center.y ? *first : *second;
        const Room &north = first_center.y < second_center.y ? *second : *first;
        return GridBounds{{checked_grid_subtract(first_center.x, width / 2), south.bounds.max.y, floor},
                          {checked_grid_add(first_center.x, width / 2), north.bounds.min.y, ceiling}};
    }
    return std::nullopt;
}

bool portal_is_free(const Layout &level, const RoomId room_id, const RoomId other_id) {
    const Room *room = find_room(level, room_id);
    const Room *other = find_room(level, other_id);
    if (room == nullptr || other == nullptr)
        return false;
    const std::optional<int> requested_side = side_between(*room, *other);
    if (!requested_side)
        return false;
    for (const Connection &connection : level.connections) {
        if (connection.from_room != room_id && connection.to_room != room_id)
            continue;
        const RoomId existing_other_id = connection.from_room == room_id ? connection.to_room : connection.from_room;
        const Room *existing_other = find_room(level, existing_other_id);
        if (existing_other == nullptr)
            continue;
        if (side_between(*room, *existing_other) == requested_side)
            return false;
    }
    return true;
}

bool can_add_connection(const Layout &level, const RoomId a_id, const RoomId b_id) {
    const Room *a = find_room(level, a_id);
    const Room *b = find_room(level, b_id);
    return a != nullptr && b != nullptr && aligned(*a, *b) && room_gap(*a, *b) >= 64 &&
           portal_is_free(level, a_id, b_id) && portal_is_free(level, b_id, a_id);
}

bool repair_connection_ids(Layout &level) {
    std::set<ConnectionId> used;
    bool changed = false;
    std::uint32_t candidate = 1;
    for (Connection &connection : level.connections) {
        if (connection.id && used.insert(connection.id).second)
            continue;
        while (used.contains(ConnectionId{candidate})) {
            if (candidate == std::numeric_limits<std::uint32_t>::max())
                throw std::overflow_error("connection identifier space is exhausted during repair");
            ++candidate;
        }
        connection.id = ConnectionId{candidate};
        used.insert(connection.id);
        changed = true;
    }
    return changed;
}

bool repair_solid_ids(Layout &level) {
    std::set<SolidId> used;
    bool changed = false;
    std::uint32_t candidate = 1;
    for (LayoutSolid &solid : level.solids) {
        if (solid.id && used.insert(solid.id).second)
            continue;
        while (used.contains(SolidId{candidate})) {
            if (candidate == std::numeric_limits<std::uint32_t>::max())
                throw std::overflow_error("solid identifier space is exhausted during repair");
            ++candidate;
        }
        solid.id = SolidId{candidate};
        used.insert(solid.id);
        changed = true;
    }
    return changed;
}

bool repair_placement_ids(Layout &level) {
    std::set<PlacementId> used;
    bool changed = false;
    std::uint32_t candidate = 1;
    for (Placement &placement : level.placements) {
        if (placement.id && used.insert(placement.id).second)
            continue;
        while (used.contains(PlacementId{candidate})) {
            if (candidate == std::numeric_limits<std::uint32_t>::max())
                throw std::overflow_error("placement identifier space is exhausted during repair");
            ++candidate;
        }
        placement.id = PlacementId{candidate};
        used.insert(placement.id);
        changed = true;
    }
    return changed;
}

bool sanitize_connections(Layout &level) {
    bool changed = repair_connection_ids(level);
    std::vector<Connection> cleaned;
    cleaned.reserve(level.connections.size());
    std::set<std::pair<RoomId, RoomId>> seen_edges;
    std::set<ConnectionRouteKey> seen_routes;
    std::set<std::pair<RoomId, int>> occupied_sides;

    const auto valid_explicit = [&level](const Connection &connection) {
        const Room *a = find_room(level, connection.from_room);
        const Room *b = find_room(level, connection.to_room);
        return a && b && a != b && connection.width >= 96 && connection.height >= 128 && (connection.width & 1) == 0 &&
               connection_has_valid_passage(*a, *b, connection);
    };
    std::vector<Connection> pending = level.connections;
    // Preserve authored routes before reconstructing any broken compatibility
    // corridor. One bad edge must not flatten unrelated stairs or upper links.
    std::stable_partition(pending.begin(), pending.end(), valid_explicit);
    for (Connection connection : pending) {
        const Room *a = find_room(level, connection.from_room);
        const Room *b = find_room(level, connection.to_room);
        if (a == nullptr || b == nullptr || connection.from_room == connection.to_room) {
            changed = true;
            continue;
        }
        const std::optional<int> side_a = side_between(*a, *b);
        const std::optional<int> side_b = side_between(*b, *a);
        const auto edge = std::minmax(connection.from_room, connection.to_room);
        if (valid_explicit(connection)) {
            if (!seen_routes.insert(connection_route_key(connection)).second) {
                changed = true;
                continue;
            }
            seen_edges.insert(edge);
            if (side_a)
                occupied_sides.insert({connection.from_room, *side_a});
            if (side_b)
                occupied_sides.insert({connection.to_room, *side_b});
            cleaned.push_back(std::move(connection));
            continue;
        }
        if (!side_a || !side_b || room_gap(*a, *b) < 64 || !seen_edges.insert(edge).second ||
            occupied_sides.contains({connection.from_room, *side_a}) ||
            occupied_sides.contains({connection.to_room, *side_b})) {
            changed = true;
            continue;
        }

        const bool along_x = *side_a == 0 || *side_a == 1;
        const std::int64_t available_width =
            along_x ? std::min(static_cast<std::int64_t>(a->bounds.max.y) - a->bounds.min.y,
                               static_cast<std::int64_t>(b->bounds.max.y) - b->bounds.min.y) -
                          128
                    : std::min(static_cast<std::int64_t>(a->bounds.max.x) - a->bounds.min.x,
                               static_cast<std::int64_t>(b->bounds.max.x) - b->bounds.min.x) -
                          128;
        const std::int64_t available_height = std::min(static_cast<std::int64_t>(a->bounds.max.z) - a->bounds.min.z,
                                                       static_cast<std::int64_t>(b->bounds.max.z) - b->bounds.min.z) -
                                              32;
        if (available_width < 96 || available_height < 128) {
            changed = true;
            continue;
        }
        const std::int64_t maximum_width =
            std::min<std::int64_t>(available_width, std::numeric_limits<GridCoord>::max());
        std::int64_t centered_width = std::clamp<std::int64_t>(connection.width, 96, maximum_width);
        if ((centered_width & 1) != 0)
            centered_width += centered_width < maximum_width ? 1 : -1;
        const GridCoord repaired_width = checked_grid_coord(centered_width);
        const GridCoord repaired_height = checked_grid_coord(std::clamp<std::int64_t>(
            connection.height, 128, std::min<std::int64_t>(available_height, std::numeric_limits<GridCoord>::max())));
        changed = changed || repaired_width != connection.width || repaired_height != connection.height;
        connection.width = repaired_width;
        connection.height = repaired_height;
        const std::optional<GridBounds> repaired_passage =
            axial_passage_volume(level, connection.from_room, connection.to_room, repaired_width, repaired_height);
        if (!repaired_passage) {
            changed = true;
            continue;
        }
        const std::vector<GridBounds> repaired_passages{*repaired_passage};
        changed = changed || connection.passage_volumes != repaired_passages;
        connection.passage_volumes = repaired_passages;
        if (!seen_routes.insert(connection_route_key(connection)).second) {
            changed = true;
            continue;
        }
        occupied_sides.insert({connection.from_room, *side_a});
        occupied_sides.insert({connection.to_room, *side_b});
        cleaned.push_back(connection);
    }

    if (cleaned.size() != level.connections.size())
        changed = true;
    level.connections = std::move(cleaned);
    return changed;
}

std::vector<int> component_ids(const Layout &level) {
    std::vector<std::vector<std::size_t>> adjacency(level.rooms.size());
    for (const Connection &connection : level.connections) {
        const std::optional<std::size_t> from = room_index(level, connection.from_room);
        const std::optional<std::size_t> to = room_index(level, connection.to_room);
        if (!from || !to)
            continue;
        adjacency[*from].push_back(*to);
        adjacency[*to].push_back(*from);
    }
    std::vector<int> components(level.rooms.size(), -1);
    int component = 0;
    for (std::size_t start = 0; start < level.rooms.size(); ++start) {
        if (components[start] != -1)
            continue;
        std::queue<std::size_t> frontier;
        frontier.push(start);
        components[start] = component;
        while (!frontier.empty()) {
            const std::size_t current = frontier.front();
            frontier.pop();
            for (const std::size_t next : adjacency[current]) {
                if (components[next] == -1) {
                    components[next] = component;
                    frontier.push(next);
                }
            }
        }
        ++component;
    }
    return components;
}

std::int64_t planar_distance(const Room &a, const Room &b) {
    const GridVec3 first = a.bounds.center();
    const GridVec3 second = b.bounds.center();
    return std::abs(static_cast<std::int64_t>(first.x) - second.x) +
           std::abs(static_cast<std::int64_t>(first.y) - second.y);
}

bool bridge_components(Layout &level) {
    const std::vector<int> components = component_ids(level);
    std::int64_t best_distance = std::numeric_limits<std::int64_t>::max();
    std::size_t best_a = level.rooms.size();
    std::size_t best_b = level.rooms.size();
    for (std::size_t a = 0; a < level.rooms.size(); ++a) {
        for (std::size_t b = a + 1; b < level.rooms.size(); ++b) {
            if (components[a] == components[b] || !can_add_connection(level, level.rooms[a].id, level.rooms[b].id))
                continue;
            const std::int64_t distance = planar_distance(level.rooms[a], level.rooms[b]);
            if (distance < best_distance) {
                best_distance = distance;
                best_a = a;
                best_b = b;
            }
        }
    }
    if (best_a == level.rooms.size())
        return false;
    const RoomId from = level.rooms[best_a].id;
    const RoomId to = level.rooms[best_b].id;
    const std::optional<GridBounds> passage = axial_passage_volume(level, from, to, 160, 192);
    if (!passage)
        return false;
    level.connections.push_back({next_connection_id(level), from, to, 160, 192, {*passage}});
    return true;
}

bool add_loop(Layout &level) {
    std::int64_t best_distance = std::numeric_limits<std::int64_t>::max();
    std::size_t best_a = level.rooms.size();
    std::size_t best_b = level.rooms.size();
    for (std::size_t a = 0; a < level.rooms.size(); ++a) {
        for (std::size_t b = a + 1; b < level.rooms.size(); ++b) {
            if (connected(level, level.rooms[a].id, level.rooms[b].id) ||
                !can_add_connection(level, level.rooms[a].id, level.rooms[b].id))
                continue;
            const std::int64_t distance = planar_distance(level.rooms[a], level.rooms[b]);
            if (distance < best_distance) {
                best_distance = distance;
                best_a = a;
                best_b = b;
            }
        }
    }
    if (best_a == level.rooms.size())
        return false;
    const RoomId from = level.rooms[best_a].id;
    const RoomId to = level.rooms[best_b].id;
    const std::optional<GridBounds> passage = axial_passage_volume(level, from, to, 160, 192);
    if (!passage)
        return false;
    level.connections.push_back({next_connection_id(level), from, to, 160, 192, {*passage}});
    return true;
}

void add_vertical_feature(Layout &level) {
    if (level.rooms.empty())
        return;
    const Room &room = level.rooms[level.rooms.size() / 2];
    const GridVec3 center = room.bounds.center();
    level.solids.push_back({
        next_solid_id(level),
        GridBounds{
            {checked_grid_subtract(center.x, 160), checked_grid_subtract(room.bounds.max.y, 288), room.bounds.min.z},
            {checked_grid_add(center.x, 160), checked_grid_subtract(room.bounds.max.y, 64),
             checked_grid_add(room.bounds.min.z, 64)}},
        MaterialRole::platform,
        BrushPurpose::raised_platform,
        BrushOwnership::structural,
        BrushContents::solid,
        std::nullopt,
    });
}

std::pair<GridCoord, GridCoord> expanded_axis(const GridCoord minimum, const GridCoord maximum,
                                              const GridCoord required_size) {
    const std::int64_t current_size = static_cast<std::int64_t>(maximum) - minimum;
    if (current_size >= required_size)
        return {minimum, maximum};

    const std::int64_t doubled_center = static_cast<std::int64_t>(minimum) + maximum;
    std::int64_t target_size = required_size;
    if ((target_size & 1) != (std::abs(doubled_center) & 1))
        ++target_size;
    return {checked_grid_coord((doubled_center - target_size) / 2),
            checked_grid_coord((doubled_center + target_size) / 2)};
}

} // namespace

std::string to_string(const RepairRefusal refusal) {
    switch (refusal) {
    case RepairRefusal::compact_mp_layout:
        return "compact_mp_layout";
    }
    throw std::invalid_argument("unsupported repair refusal");
}

bool is_compact_mp_layout(const Layout &layout) {
    return std::any_of(layout.rooms.begin(), layout.rooms.end(), [](const Room &room) {
        return std::find(room.tags.begin(), room.tags.end(), "compact_mp") != room.tags.end();
    });
}

RepairOutcome LayoutRepairEngine::repair(Layout &level, const ValidationResult &validation) const {
    // Fail closed before any pass runs: every generic pass below either re-runs
    // the legacy EntityAllocator (dropping station binding, refined spawns and
    // motif lights) or rewrites rooms, passages and solids the compact motif
    // builder owns. The layout is returned untouched and its issues stand.
    if (!validation.issues.empty() && is_compact_mp_layout(level))
        return {false, RepairRefusal::compact_mp_layout};

    bool changed = false;

    if (has_issue(validation, "invalid_connection_id") || has_issue(validation, "duplicate_connection_id"))
        changed = repair_connection_ids(level) || changed;
    if (has_issue(validation, "invalid_solid_id") || has_issue(validation, "duplicate_solid_id"))
        changed = repair_solid_ids(level) || changed;
    if (has_issue(validation, "invalid_placement_id") || has_issue(validation, "duplicate_placement_id"))
        changed = repair_placement_ids(level) || changed;

    if (has_issue(validation, "invalid_connection") || has_issue(validation, "duplicate_connection") ||
        has_issue(validation, "corridor_clearance") || has_issue(validation, "connection_centering") ||
        has_issue(validation, "invalid_passage")) {
        changed = sanitize_connections(level) || changed;
    }

    if (has_issue(validation, "disconnected_graph")) {
        changed = bridge_components(level) || changed;
    }
    if (has_issue(validation, "missing_loop")) {
        changed = add_loop(level) || changed;
    }
    if (has_issue(validation, "missing_verticality")) {
        add_vertical_feature(level);
        changed = true;
    }

    if (has_issue(validation, "room_clearance")) {
        for (Room &room : level.rooms) {
            const auto [min_x, max_x] = expanded_axis(room.bounds.min.x, room.bounds.max.x, 384);
            const auto [min_y, max_y] = expanded_axis(room.bounds.min.y, room.bounds.max.y, 384);
            const auto [min_z, max_z] = expanded_axis(room.bounds.min.z, room.bounds.max.z, 192);
            room.bounds = {
                {min_x, min_y, min_z},
                {max_x, max_y, max_z},
            };
        }
        for (Connection &connection : level.connections) {
            if (const std::optional<GridBounds> passage = axial_passage_volume(
                    level, connection.from_room, connection.to_room, connection.width, connection.height)) {
                connection.passage_volumes = {*passage};
            }
        }
        changed = true;
    }

    if (has_issue(validation, "missing_spawns") || has_issue(validation, "missing_encounters") ||
        has_issue(validation, "missing_exit") || has_issue(validation, "missing_objectives") ||
        has_issue(validation, "ctf_spawn_contract") || has_issue(validation, "exit_entity_contract") ||
        has_issue(validation, "weapon_distribution") || has_issue(validation, "placement_outside_hull") ||
        has_issue(validation, "placement_in_solid") || has_issue(validation, "placement_overlap")) {
        const std::vector<Placement> before = level.placements;
        EntityAllocator{}.populate(level);
        changed = !same_placements(before, level.placements) || changed;
    }

    return {changed, std::nullopt};
}

} // namespace qmg
