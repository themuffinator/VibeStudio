#include "qmg/generator.hpp"

#include "qmg/entities.hpp"
#include "qmg/mp_motifs.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qmg {
namespace {

constexpr GridCoord kGrid = 1280;

int uniform_integer(std::mt19937_64 &random, const int minimum, const int maximum) {
    const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum + 1);
    const std::uint64_t rejection_threshold = (0ULL - span) % span;
    std::uint64_t value{};
    do {
        value = random();
    } while (value < rejection_threshold);
    return minimum + static_cast<int>(value % span);
}

GridBounds axial_passage_volume(const Layout &level, const RoomId from, const RoomId to, const GridCoord width,
                                const GridCoord height) {
    const Room *first = find_room(level, from);
    const Room *second = find_room(level, to);
    if (first == nullptr || second == nullptr)
        throw std::logic_error("cannot build a passage for an unknown room");
    const GridVec3 first_center = first->bounds.center();
    const GridVec3 second_center = second->bounds.center();
    const GridCoord floor = std::max(first->bounds.min.z, second->bounds.min.z);
    const GridCoord ceiling = checked_grid_add(floor, height);
    if (first_center.y == second_center.y) {
        const Room &left = first_center.x < second_center.x ? *first : *second;
        const Room &right = first_center.x < second_center.x ? *second : *first;
        return {{left.bounds.max.x, checked_grid_subtract(first_center.y, width / 2), floor},
                {right.bounds.min.x, checked_grid_add(first_center.y, width / 2), ceiling}};
    }
    if (first_center.x == second_center.x) {
        const Room &south = first_center.y < second_center.y ? *first : *second;
        const Room &north = first_center.y < second_center.y ? *second : *first;
        return {{checked_grid_subtract(first_center.x, width / 2), south.bounds.max.y, floor},
                {checked_grid_add(first_center.x, width / 2), north.bounds.min.y, ceiling}};
    }
    throw std::logic_error("compatibility passage generation requires axis-aligned room centers");
}

void add_connection(Layout &level, const RoomId from, const RoomId to, const GridCoord width, const GridCoord height) {
    if (from == to)
        return;
    const auto duplicate =
        std::find_if(level.connections.begin(), level.connections.end(), [from, to](const Connection &edge) {
            return (edge.from_room == from && edge.to_room == to) || (edge.from_room == to && edge.to_room == from);
        });
    if (duplicate == level.connections.end()) {
        level.connections.push_back({next_connection_id(level),
                                     from,
                                     to,
                                     width,
                                     height,
                                     {axial_passage_volume(level, from, to, width, height)}});
    }
}

std::vector<GridVec3> multiplayer_positions(const int count) {
    const int top_count = (count + 1) / 2;
    const int bottom_count = count - top_count;
    const GridCoord x_offset =
        checked_grid_coord(static_cast<std::int64_t>(top_count - 1) * static_cast<std::int64_t>(kGrid) / 2);
    std::vector<GridVec3> positions;
    positions.reserve(static_cast<std::size_t>(count));

    for (int column = 0; column < top_count; ++column) {
        positions.push_back(
            {checked_grid_subtract(checked_grid_coord(static_cast<std::int64_t>(column) * kGrid), x_offset),
             static_cast<GridCoord>(-kGrid / 2), 0});
    }
    const int starting_column = top_count - bottom_count;
    for (int index = 0; index < bottom_count; ++index) {
        const int column = starting_column + index;
        positions.push_back(
            {checked_grid_subtract(checked_grid_coord(static_cast<std::int64_t>(column) * kGrid), x_offset),
             static_cast<GridCoord>(kGrid / 2), 0});
    }
    return positions;
}

std::vector<GridVec3> single_player_positions(const int count) {
    constexpr int columns = 3;
    const int rows = (count + columns - 1) / columns;
    constexpr GridCoord x_offset = kGrid;
    const GridCoord y_offset =
        checked_grid_coord(static_cast<std::int64_t>(rows - 1) * static_cast<std::int64_t>(kGrid) / 2);
    std::vector<GridVec3> positions;
    positions.reserve(static_cast<std::size_t>(count));

    for (int index = 0; index < count; ++index) {
        const int row = index / columns;
        const int within_row = index % columns;
        const int column = row % 2 == 0 ? within_row : columns - 1 - within_row;
        positions.push_back({
            checked_grid_subtract(checked_grid_coord(static_cast<std::int64_t>(column) * kGrid), x_offset),
            checked_grid_subtract(checked_grid_coord(static_cast<std::int64_t>(row) * kGrid), y_offset),
            0,
        });
    }
    return positions;
}

void connect_multiplayer(Layout &level) {
    const int count = static_cast<int>(level.rooms.size());
    const int top_count = (count + 1) / 2;
    const int bottom_count = count - top_count;
    for (int index = 1; index < top_count; ++index) {
        add_connection(level, level.rooms[static_cast<std::size_t>(index - 1)].id,
                       level.rooms[static_cast<std::size_t>(index)].id, 192, 192);
    }
    if (bottom_count == 0)
        return;

    add_connection(level, level.rooms[static_cast<std::size_t>(top_count - 1)].id,
                   level.rooms[static_cast<std::size_t>(count - 1)].id, 192, 192);
    for (int index = count - 1; index > top_count; --index) {
        add_connection(level, level.rooms[static_cast<std::size_t>(index)].id,
                       level.rooms[static_cast<std::size_t>(index - 1)].id, 192, 192);
    }
    const int aligned_top = top_count - bottom_count;
    add_connection(level, level.rooms[static_cast<std::size_t>(top_count)].id,
                   level.rooms[static_cast<std::size_t>(aligned_top)].id, 192, 192);
}

void connect_single_player(Layout &level, const RouteStyle route_style) {
    for (int index = 1; index < static_cast<int>(level.rooms.size()); ++index) {
        add_connection(level, level.rooms[static_cast<std::size_t>(index - 1)].id,
                       level.rooms[static_cast<std::size_t>(index)].id, 160, 192);
    }
    if (route_style == RouteStyle::loop && level.rooms.size() == 5) {
        add_connection(level, level.rooms[1].id, level.rooms[4].id, 160, 192);
    } else if (route_style == RouteStyle::loop && level.rooms.size() >= 6) {
        add_connection(level, level.rooms[0].id, level.rooms[5].id, 160, 192);
    }
}

void add_vertical_features(Layout &level, std::mt19937_64 &random) {
    if (level.planned_verticality < 0.3)
        return;

    for (std::size_t index = 1; index < level.rooms.size(); index += 2) {
        const Room &room = level.rooms[index];
        const GridVec3 center = room.bounds.center();
        const GridCoord platform_height = checked_grid_coord(
            static_cast<std::int64_t>(uniform_integer(random, 3, level.planned_verticality > 0.8 ? 7 : 5)) * 16);
        const GridCoord platform_min_y = checked_grid_subtract(room.bounds.max.y, 288);
        level.solids.push_back({
            next_solid_id(level),
            GridBounds{{checked_grid_subtract(center.x, 176), platform_min_y, room.bounds.min.z},
                       {checked_grid_add(center.x, 176), checked_grid_subtract(room.bounds.max.y, 64),
                        checked_grid_add(room.bounds.min.z, platform_height)}},
            MaterialRole::platform,
            BrushPurpose::raised_platform,
            BrushOwnership::structural,
            BrushContents::solid,
            std::nullopt,
        });

        const int stair_count = platform_height / 16;
        for (int step = 0; step < stair_count; ++step) {
            // Leave positive support after the standing 32-unit hull is eroded
            // from both sides of a tread by the navigation analyzer.
            constexpr GridCoord step_depth = 36;
            const GridCoord near_y =
                checked_grid_subtract(platform_min_y, static_cast<std::int64_t>(stair_count - step) * step_depth);
            const GridCoord step_height = checked_grid_coord(static_cast<std::int64_t>(step + 1) * 16);
            level.solids.push_back({
                next_solid_id(level),
                GridBounds{{checked_grid_subtract(center.x, 112), near_y, room.bounds.min.z},
                           {checked_grid_add(center.x, 112), checked_grid_add(near_y, step_depth),
                            checked_grid_add(room.bounds.min.z, step_height)}},
                MaterialRole::stair,
                BrushPurpose::navigation_stair,
                BrushOwnership::structural,
                BrushContents::solid,
                std::nullopt,
            });
        }
    }
}

} // namespace

std::vector<Layout> LayoutGenerator::generate_candidates(const PromptSpec &spec, const SemanticPlan &semantic_plan,
                                                         const std::size_t count) const {
    const std::vector<PlanIssue> semantic_issues = validate_semantic_plan(semantic_plan, spec);
    if (!semantic_issues.empty())
        throw std::invalid_argument("layout generation requires a valid semantic plan: " +
                                    semantic_issues.front().code);
    if (count == 0 || count > 64)
        throw std::invalid_argument("layout generation candidate count must be from 1 to 64");
    const std::size_t actual_count = count;
    std::vector<Layout> result;
    result.reserve(actual_count);
    for (std::size_t index = 0; index < actual_count; ++index) {
        const auto offset = static_cast<std::uint64_t>(index) * 0x9E3779B97F4A7C15ULL;
        result.push_back(generate_one(spec, semantic_plan, spec.seed + offset));
    }
    return result;
}

Layout LayoutGenerator::generate_one(const PromptSpec &spec, const SemanticPlan &semantic_plan,
                                     const std::uint64_t candidate_seed) const {
    Layout level;
    level.spec = spec;
    level.planned_verticality = static_cast<double>(semantic_plan.verticality) / 100.0;
    level.planned_intensity = static_cast<double>(semantic_plan.intensity) / 100.0;
    level.lighting_style = semantic_plan.lighting_style;
    level.candidate_seed = candidate_seed;

    if (spec.game == Game::quake2 && (spec.mode == GameMode::deathmatch || spec.mode == GameMode::duel)) {
        build_compact_mp_layout(level, semantic_plan);
        return level;
    }

    std::mt19937_64 random(candidate_seed);

    const bool multiplayer = spec.mode != GameMode::single_player || spec.game == Game::quake3;
    const std::vector<GridVec3> positions =
        multiplayer ? multiplayer_positions(spec.room_count) : single_player_positions(spec.room_count);

    for (int index = 0; index < spec.room_count; ++index) {
        const GridVec3 position = positions[static_cast<std::size_t>(index)];
        const GridCoord width = checked_grid_coord(static_cast<std::int64_t>(uniform_integer(random, 38, 56)) * 16);
        const GridCoord depth = checked_grid_coord(static_cast<std::int64_t>(uniform_integer(random, 36, 54)) * 16);
        const GridCoord height = checked_grid_coord(static_cast<std::int64_t>(uniform_integer(random, 20, 30)) * 16);
        Room room;
        room.id = next_room_id(level);
        room.name = multiplayer ? "arena_" + std::to_string(index + 1) : "beat_" + std::to_string(index + 1);
        room.bounds = {
            {checked_grid_subtract(position.x, width / 2), checked_grid_subtract(position.y, depth / 2), 0},
            {checked_grid_add(position.x, width / 2), checked_grid_add(position.y, depth / 2), height},
        };
        if (index == 0)
            room.tags.push_back("start");
        if (index == spec.room_count - 1)
            room.tags.push_back(multiplayer ? "anchor" : "exit");
        const std::string semantic_role = to_string(semantic_plan.room_roles[static_cast<std::size_t>(index)]);
        if (std::find(room.tags.begin(), room.tags.end(), semantic_role) == room.tags.end())
            room.tags.push_back(semantic_role);
        level.rooms.push_back(std::move(room));
    }

    if (multiplayer) {
        connect_multiplayer(level);
    } else {
        connect_single_player(level, semantic_plan.route_style);
    }

    add_vertical_features(level, random);
    EntityAllocator{}.populate(level);
    return level;
}

} // namespace qmg
