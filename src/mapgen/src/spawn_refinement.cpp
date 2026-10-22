#include "qmg/spawn_refinement.hpp"

#include "qmg/visibility.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace qmg {
namespace {

constexpr std::uint64_t separation_squared = 256ULL * 256;
constexpr std::uint64_t anchor_squared = 64ULL * 64;
constexpr std::size_t station_cap = 24;
constexpr std::size_t choice_cap = 4096;

enum class Relation { visible, explicit_solid, shell };

void require(bool condition, const char *message) {
    if (!condition)
        throw std::invalid_argument(message);
}

std::uint64_t distance_squared(GridVec3 a, GridVec3 b) {
    // Context validation bounds all retained coordinates to +/-32768.
    const std::int64_t dx = static_cast<std::int64_t>(a.x) - b.x;
    const std::int64_t dy = static_cast<std::int64_t>(a.y) - b.y;
    const std::int64_t dz = static_cast<std::int64_t>(a.z) - b.z;
    return static_cast<std::uint64_t>(dx * dx + dy * dy + dz * dz);
}

bool overlap(const GridBounds &a, const GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

bool supported(const Layout &layout, const Room &room, GridVec3 origin) {
    // Reject before arithmetic so arbitrary caller coordinates cannot overflow.
    if (origin.x < -32736 || origin.x > 32736 || origin.y < -32736 || origin.y > 32736 || origin.z < -32736 ||
        origin.z > 32736)
        return false;
    const GridBounds hull{{origin.x - 16, origin.y - 16, origin.z - 24}, {origin.x + 16, origin.y + 16, origin.z + 32}};
    if (hull.min.x < room.bounds.min.x || hull.max.x > room.bounds.max.x || hull.min.y < room.bounds.min.y ||
        hull.max.y > room.bounds.max.y || hull.min.z < room.bounds.min.z || hull.max.z > room.bounds.max.z)
        return false;
    bool support = hull.min.z == room.bounds.min.z;
    for (const auto &solid : layout.solids) {
        const auto &box = *get_box(solid.geometry); // construction validated first
        if (overlap(hull, box))
            return false;
        support |= box.max.z == hull.min.z && hull.min.x >= box.min.x && hull.max.x <= box.max.x &&
                   hull.min.y >= box.min.y && hull.max.y <= box.max.y;
    }
    return support;
}

GridVec3 eye(GridVec3 origin) {
    origin.z = checked_grid_add(origin.z, 22);
    return origin;
}

bool exact_visible(const SegmentVisibilityResult &value) {
    return value.kind == SegmentVisibilityKind::visible && value.precision == VisibilityPrecision::exact;
}

Relation relation(const SegmentVisibilityResult &value) {
    require(value.precision == VisibilityPrecision::exact, "spawn refinement rejects conservative visibility");
    if (value.kind == SegmentVisibilityKind::visible)
        return Relation::visible;
    if (value.kind == SegmentVisibilityKind::occluded)
        return Relation::explicit_solid;
    // Endpoints are separately certified exact-visible. For this all-solid box
    // construction, leaving the room/passage union crosses an implicit shell.
    // Preserve the API's outside_free_space meaning; do not call it an explicit
    // solid hit or extend this interpretation to arbitrary layouts/mechanisms.
    require(value.kind == SegmentVisibilityKind::outside_free_space, "unknown spawn ray classification");
    return Relation::shell;
}

void add_pair(SpawnRefinementObjective &objective, Relation ray, GridVec3 a, GridVec3 b) {
    objective.visible_pairs += ray == Relation::visible ? 1U : 0U;
    const auto distance = distance_squared(a, b);
    if (distance < separation_squared) {
        ++objective.close_pairs;
        objective.squared_distance_deficit += separation_squared - distance;
    }
}

struct Station {
    std::size_t placement;
    std::size_t room;
    GridCoord original_z;
    std::vector<GridVec3> candidates;
};

SpawnRefinementObjective objective(const Layout &layout, const std::vector<Station> &stations,
                                   const std::vector<std::vector<Relation>> &rays) {
    SpawnRefinementObjective value;
    for (std::size_t i = 0; i < stations.size(); ++i)
        for (std::size_t j = i + 1; j < stations.size(); ++j)
            add_pair(value, rays[i][j], layout.placements[stations[i].placement].origin,
                     layout.placements[stations[j].placement].origin);
    return value;
}

} // namespace

SpawnRefinementResult refine_compact_mp_spawns(Layout &layout, const std::vector<SpawnRefinementChoices> &choices) {
    require_supported_layout_intent(layout.spec);
    require(layout.spec.game == Game::quake2 &&
                (layout.spec.mode == GameMode::deathmatch || layout.spec.mode == GameMode::duel) &&
                layout.spec.room_count >= 2 && layout.spec.room_count <= 8 &&
                layout.rooms.size() == static_cast<std::size_t>(layout.spec.room_count),
            "spawn refinement requires a matching2–8-room Q2 DM/duel request");
    const auto common_floor = layout.rooms.front().bounds.min.z;
    for (const auto &room : layout.rooms) {
        require(std::find(room.tags.begin(), room.tags.end(), "compact_mp") != room.tags.end(),
                "spawn refinement requires marked compact rooms");
        require(room.bounds.min.z == common_floor, "spawn refinement requires a common room shell floor");
    }
    // No free volume may extend below the common room floor, so the union's
    // downward boundary still supports every accepted in-room ground station.
    // Elevated passage bottoms need their own route-support proof; this guard
    // does not claim that an overlapping lower passage leaves them intact.
    for (const auto &connection : layout.connections)
        for (const auto &volume : connection.passage_volumes)
            require(volume.min.z >= common_floor, "spawn refinement rejects passages below the common room floor");
    for (const auto &solid : layout.solids)
        require(get_box(solid.geometry) && get_box(solid.geometry)->valid() && solid.contents == BrushContents::solid &&
                    !solid.owner &&
                    (solid.ownership == BrushOwnership::structural || solid.ownership == BrushOwnership::detail) &&
                    solid.material >= MaterialRole::floor && solid.material <= MaterialRole::hidden &&
                    solid.material != MaterialRole::liquid &&
                    (solid.purpose == BrushPurpose::structural || solid.purpose == BrushPurpose::raised_platform ||
                     solid.purpose == BrushPurpose::navigation_stair),
                "spawn refinement requires static all-solid AABB architecture");
    for (const auto &placement : layout.placements)
        require(!placement.volume &&
                    (placement.role == PlacementRole::deathmatch_spawn || placement.role == PlacementRole::weapon ||
                     placement.role == PlacementRole::health || placement.role == PlacementRole::armor ||
                     placement.role == PlacementRole::light),
                "spawn refinement excludes unsupported placement roles and mechanisms");
    require(choices.size() <= 32, "spawn refinement choice count exceeds compact request bound");

    // Work privately so failed public-input validation never partially edits the caller.
    Layout work = layout;
    const VisibilityAnalyzer analyzer;
    (void)analyzer.classify_segments(work, {}); // structural limits, IDs, coordinate bounds
    std::map<PlacementId, std::vector<GridVec3>> candidate_map;
    for (const auto &choice : choices) {
        require(choice.candidates.size() <= choice_cap, "spawn refinement station count exceeds bound");
        const auto *placement = find_placement(work, choice.id);
        require(placement && placement->role == PlacementRole::deathmatch_spawn,
                "spawn refinement choice must name a deathmatch spawn");
        require(candidate_map.emplace(choice.id, choice.candidates).second, "duplicate spawn refinement choice");
    }
    std::vector<Station> stations;
    for (std::size_t p = 0; p < work.placements.size(); ++p) {
        const auto &spawn = work.placements[p];
        if (spawn.role != PlacementRole::deathmatch_spawn)
            continue;
        std::vector<std::size_t> rooms;
        for (std::size_t i = 0; i < work.rooms.size(); ++i)
            if (work.rooms[i].bounds.contains(spawn.origin))
                rooms.push_back(i);
        require(rooms.size() == 1 && supported(work, work.rooms[rooms.front()], spawn.origin),
                "spawn refinement requires a supported unambiguous initial station");
        auto candidates = candidate_map.contains(spawn.id) ? candidate_map.at(spawn.id) : std::vector<GridVec3>{};
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        std::erase_if(candidates, [&](GridVec3 candidate) {
            return candidate.z != spawn.origin.z || !supported(work, work.rooms[rooms.front()], candidate);
        });
        stations.push_back({p, rooms.front(), spawn.origin.z, std::move(candidates)});
    }
    require(stations.size() >= 2 && stations.size() <= 32, "spawn refinement needs two to32 spawn stations");
    std::sort(stations.begin(), stations.end(), [&](const auto &a, const auto &b) {
        return work.placements[a.placement].id < work.placements[b.placement].id;
    });
    std::vector<SegmentVisibilityQuery> queries;
    for (const auto &station : stations) {
        const auto point = eye(work.placements[station.placement].origin);
        queries.push_back({point, point});
    }
    for (std::size_t i = 0; i < stations.size(); ++i)
        for (std::size_t j = i + 1; j < stations.size(); ++j)
            queries.push_back({eye(work.placements[stations[i].placement].origin),
                               eye(work.placements[stations[j].placement].origin)});
    const auto initial = analyzer.classify_segments(work, queries);
    for (std::size_t i = 0; i < stations.size(); ++i)
        require(exact_visible(initial[i]), "spawn refinement initial eye is invalid");
    std::vector<std::vector<Relation>> rays(stations.size(), std::vector<Relation>(stations.size(), Relation::visible));
    auto index = stations.size();
    for (std::size_t i = 0; i < stations.size(); ++i)
        for (std::size_t j = i + 1; j < stations.size(); ++j)
            rays[i][j] = rays[j][i] = relation(initial[index++]);

    SpawnRefinementResult result;
    result.before = objective(work, stations, rays);
    for (std::size_t pass = 0; pass < 2; ++pass) {
        bool changed = false;
        for (std::size_t i = 0; i < stations.size(); ++i) {
            const auto &station = stations[i];
            auto &spawn = work.placements[station.placement];
            SpawnRefinementObjective current;
            std::vector<std::size_t> others;
            for (std::size_t j = 0; j < stations.size(); ++j)
                if (i != j) {
                    others.push_back(j);
                    add_pair(current, rays[i][j], spawn.origin, work.placements[stations[j].placement].origin);
                }
            if (current.visible_pairs == 0 && current.close_pairs == 0)
                continue;
            std::vector<GridVec3> candidates;
            for (const auto candidate : station.candidates)
                if (candidate != spawn.origin &&
                    !std::any_of(work.placements.begin(), work.placements.end(), [&](const auto &other) {
                        return other.id != spawn.id && other.role != PlacementRole::light &&
                               distance_squared(candidate, other.origin) < anchor_squared;
                    }))
                    candidates.push_back(candidate);
            std::sort(candidates.begin(), candidates.end(), [&](GridVec3 a, GridVec3 b) {
                return std::tuple{distance_squared(a, spawn.origin), a} <
                       std::tuple{distance_squared(b, spawn.origin), b};
            });
            if (candidates.size() > station_cap)
                candidates.resize(station_cap);
            if (candidates.empty())
                continue;
            queries.clear();
            for (const auto candidate : candidates) {
                queries.push_back({eye(candidate), eye(candidate)});
                for (const auto j : others)
                    queries.push_back({eye(candidate), eye(work.placements[stations[j].placement].origin)});
            }
            const auto tested = analyzer.classify_segments(work, queries);
            result.tested_candidates += candidates.size();
            auto best = current;
            auto best_origin = spawn.origin;
            std::vector<Relation> best_rays;
            for (std::size_t c = 0; c < candidates.size(); ++c) {
                const auto offset = c * stations.size();
                if (!exact_visible(tested[offset]))
                    continue;
                SpawnRefinementObjective value;
                std::vector<Relation> candidate_rays;
                bool eligible = true;
                for (std::size_t k = 0; k < others.size(); ++k) {
                    const auto &ray = tested[offset + k + 1];
                    if (ray.precision != VisibilityPrecision::exact) {
                        eligible = false;
                        break;
                    }
                    const auto new_ray = relation(ray);
                    eligible &= rays[i][others[k]] == Relation::visible || new_ray != Relation::visible;
                    candidate_rays.push_back(new_ray);
                    add_pair(value, new_ray, candidates[c], work.placements[stations[others[k]].placement].origin);
                }
                if (eligible && value < current &&
                    (value < best ||
                     (value == best && std::tuple{distance_squared(candidates[c], spawn.origin), candidates[c]} <
                                           std::tuple{distance_squared(best_origin, spawn.origin), best_origin}))) {
                    best = value;
                    best_origin = candidates[c];
                    best_rays = std::move(candidate_rays);
                }
            }
            if (best_origin != spawn.origin) {
                spawn.origin = best_origin;
                for (std::size_t k = 0; k < others.size(); ++k)
                    rays[i][others[k]] = rays[others[k]][i] = best_rays[k];
                ++result.accepted_moves;
                changed = true;
            }
        }
        result.passes = pass + 1;
        if (!changed)
            break;
    }
    result.after = objective(work, stations, rays);
    for (const auto &station : stations)
        layout.placements[station.placement].origin = work.placements[station.placement].origin;
    return result;
}

} // namespace qmg
