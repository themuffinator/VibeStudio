#include "qmg/mp_motifs.hpp"

#include "qmg/entities.hpp"
#include "qmg/spawn_refinement.hpp"
#include "qmg/upper_links.hpp"
#include "qmg/visibility.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace qmg {
namespace {

int choose(std::mt19937_64 &random, int low, int high) {
    const auto span = static_cast<std::uint64_t>(high - low + 1);
    const auto threshold = (0ULL - span) % span;
    std::uint64_t value;
    do {
        value = random();
    } while (value < threshold);
    return low + static_cast<int>(value % span);
}

template <typename T> void shuffle(std::vector<T> &values, std::mt19937_64 &random) {
    for (std::size_t i = values.size(); i > 1; --i)
        std::swap(values[i - 1], values[static_cast<std::size_t>(choose(random, 0, static_cast<int>(i - 1)))]);
}

bool overlap(const GridBounds &a, const GridBounds &b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z &&
           a.max.z > b.min.z;
}

struct LocalRoom {
    GridBounds bounds;
    int rotation;
    int width() const { return rotation % 2 ? bounds.max.y - bounds.min.y : bounds.max.x - bounds.min.x; }
    int depth() const { return rotation % 2 ? bounds.max.x - bounds.min.x : bounds.max.y - bounds.min.y; }
    GridVec3 point(int u, int v, int z) const {
        switch (rotation) {
        case 0:
            return {bounds.min.x + u, bounds.min.y + v, z};
        case 1:
            return {bounds.max.x - v, bounds.min.y + u, z};
        case 2:
            return {bounds.max.x - u, bounds.max.y - v, z};
        default:
            return {bounds.min.x + v, bounds.max.y - u, z};
        }
    }
    GridBounds box(int u0, int v0, int u1, int v1, int low, int high) const {
        const auto a = point(u0, v0, low), b = point(u1, v1, high);
        return {{std::min(a.x, b.x), std::min(a.y, b.y), low}, {std::max(a.x, b.x), std::max(a.y, b.y), high}};
    }
};

void solid(Layout &level, GridBounds bounds, BrushPurpose purpose, MaterialRole material) {
    level.solids.push_back(
        {next_solid_id(level), bounds, material, purpose, BrushOwnership::structural, BrushContents::solid, {}});
}

void deck(Layout &level, const LocalRoom &room, int u0, int v0, int u1, int v1, int z) {
    solid(level, room.box(u0, v0, u1, v1, z - 24, z), BrushPurpose::raised_platform, MaterialRole::platform);
}

void stair(Layout &level, const LocalRoom &room, int u, int v, int height) {
    for (int step = 0; step < height / 16; ++step)
        solid(level, room.box(u, v + step * 36, u + 128, v + (step + 1) * 36, 0, (step + 1) * 16),
              BrushPurpose::navigation_stair, MaterialRole::stair);
}

void light(Layout &level, GridVec3 point, int brightness) {
    Placement p;
    p.id = next_placement_id(level);
    p.role = PlacementRole::light;
    p.origin = point;
    p.light_intensity = brightness;
    p.light_color = RgbColor{860, 920, 1000};
    level.placements.push_back(p);
}

struct FaceJoin {
    RoomId from, to;
    bool x_axis;
    int low, high, near_face, far_face;
};

std::vector<GridBounds> pinwheel_bounds(std::mt19937_64 &random, int count) {
    const int left = choose(random, 43, 45) * 16;
    const int center_width = choose(random, 43, 47) * 16;
    const int right = choose(random, 43, 45) * 16;
    const int bottom = choose(random, 43, 45) * 16;
    const int center_depth = choose(random, 43, 47) * 16;
    const int top = choose(random, 43, 45) * 16;
    const int x0 = left + 48, x1 = x0 + center_width;
    const int y0 = bottom + 48, y1 = y0 + center_depth;
    const int width = x1 + 48 + right, depth = y1 + 48 + top;

    // Four rectangles turn around a playable center. No internal straight gap
    // cuts the whole envelope: each perimeter arm crosses the next arm's seam.
    std::array<GridBounds, 4> perimeter{
        GridBounds{{0, 0, 0}, {x1, bottom, 0}},
        GridBounds{{x1 + 48, 0, 0}, {width, y1, 0}},
        GridBounds{{x0, y1 + 48, 0}, {width, depth, 0}},
        GridBounds{{0, y0, 0}, {left, depth, 0}},
    };
    std::vector<int> split_order{0, 1, 2, 3};
    shuffle(split_order, random);
    const std::array<int, 4> seam_offsets{left, bottom, center_width, center_depth};
    std::vector<GridBounds> result{{{x0, y0, 0}, {x1, y1, 0}}};
    for (int side = 0; side < 4; ++side) {
        const auto bounds = perimeter[static_cast<std::size_t>(side)];
        if (std::find(split_order.begin(), split_order.begin() + (count - 5), side) ==
            split_order.begin() + (count - 5)) {
            result.push_back(bounds);
            continue;
        }
        const bool along_x = side % 2 == 0;
        const int low = along_x ? bounds.min.x : bounds.min.y;
        const int high = along_x ? bounds.max.x : bounds.max.y;
        // A cut coincident with the center-side seam would create a global slice.
        // The688-unit bands leave legal cuts with two640-unit-or-wider bays.
        std::vector<int> cuts;
        const int seam = seam_offsets[static_cast<std::size_t>(side)];
        for (int cut = 640; cut <= high - low - 48 - 640; cut += 16)
            if (cut + 48 <= seam || cut >= seam + 48)
                cuts.push_back(cut);
        if (cuts.empty())
            throw std::logic_error("pinwheel arm cannot split without aligning seams");
        const int first_span = cuts[static_cast<std::size_t>(choose(random, 0, static_cast<int>(cuts.size()) - 1))];
        auto first = bounds, second = bounds;
        if (along_x) {
            first.max.x = low + first_span;
            second.min.x = first.max.x + 48;
        } else {
            first.max.y = low + first_span;
            second.min.y = first.max.y + 48;
        }
        result.push_back(first);
        result.push_back(second);
    }
    return result;
}

void passage(Layout &level, const FaceJoin &join, int center, int width = 160) {
    GridBounds b = join.x_axis
                       ? GridBounds{{join.near_face, center - width / 2, 0}, {join.far_face, center + width / 2, 192}}
                       : GridBounds{{center - width / 2, join.near_face, 0}, {center + width / 2, join.far_face, 192}};
    level.connections.push_back({next_connection_id(level), join.from, join.to, width, 192, {b}});
}

std::vector<FaceJoin> joins(const Layout &level) {
    std::vector<FaceJoin> result;
    for (std::size_t i = 0; i < level.rooms.size(); ++i)
        for (std::size_t j = i + 1; j < level.rooms.size(); ++j) {
            const Room *a = &level.rooms[i], *b = &level.rooms[j];
            if (a->bounds.min.x > b->bounds.min.x)
                std::swap(a, b);
            int low = std::max(a->bounds.min.y, b->bounds.min.y), high = std::min(a->bounds.max.y, b->bounds.max.y);
            if (b->bounds.min.x - a->bounds.max.x == 48 && high - low >= 224)
                result.push_back({a->id, b->id, true, low, high, a->bounds.max.x, b->bounds.min.x});
            a = &level.rooms[i];
            b = &level.rooms[j];
            if (a->bounds.min.y > b->bounds.min.y)
                std::swap(a, b);
            low = std::max(a->bounds.min.x, b->bounds.min.x);
            high = std::min(a->bounds.max.x, b->bounds.max.x);
            if (b->bounds.min.y - a->bounds.max.y == 48 && high - low >= 224)
                result.push_back({a->id, b->id, false, low, high, a->bounds.max.y, b->bounds.min.y});
        }
    return result;
}

bool clear_supported(const Layout &level, const Room &room, GridVec3 feet) {
    const GridBounds hull{{feet.x - 16, feet.y - 16, feet.z}, {feet.x + 16, feet.y + 16, feet.z + 56}};
    if (hull.min.x < room.bounds.min.x || hull.max.x > room.bounds.max.x || hull.min.y < room.bounds.min.y ||
        hull.max.y > room.bounds.max.y || hull.max.z > room.bounds.max.z)
        return false;
    bool supported = feet.z == room.bounds.min.z;
    for (const auto &s : level.solids) {
        const auto b = checked_brush_bounds(s.geometry);
        if (overlap(hull, b))
            return false;
        if (s.purpose == BrushPurpose::raised_platform && b.max.z == feet.z && hull.min.x >= b.min.x &&
            hull.max.x <= b.max.x && hull.min.y >= b.min.y && hull.max.y <= b.max.y)
            supported = true;
    }
    return supported;
}

using SpawnChoices = SpawnRefinementChoices;

// A local visibility refinement, not a balance claim. Keep each selected room
// and floor, and change a station only for a strict reduction in spawn eye rays.
void refine_spawn_visibility(Layout &level, const std::vector<SpawnChoices> &choices) {
    const auto eye = [](GridVec3 origin) {
        origin.z += 22;
        return origin;
    };
    const auto distance_squared = [](GridVec3 a, GridVec3 b) {
        const long long dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    };
    const VisibilityAnalyzer analyzer;
    for (int pass = 0; pass < 2; ++pass) {
        bool changed = false;
        for (const auto &choice : choices) {
            auto &spawn = *std::find_if(level.placements.begin(), level.placements.end(),
                                        [&](const auto &p) { return p.id == choice.id; });
            std::vector<GridVec3> others;
            for (const auto &p : level.placements)
                if (p.role == PlacementRole::deathmatch_spawn && p.id != spawn.id)
                    others.push_back(eye(p.origin));
            if (others.empty())
                continue;
            std::vector<SegmentVisibilityQuery> queries;
            for (const auto &other : others)
                queries.push_back({eye(spawn.origin), other});
            const auto current = analyzer.classify_segments(level, queries);
            auto best_count = std::count_if(current.begin(), current.end(),
                                            [](const auto &r) { return r.kind == SegmentVisibilityKind::visible; });
            if (best_count == 0)
                continue;
            std::vector<GridVec3> candidates;
            queries.clear();
            for (const auto &candidate : choice.candidates) {
                const bool crowded = std::any_of(level.placements.begin(), level.placements.end(), [&](const auto &p) {
                    return p.id != spawn.id && p.role != PlacementRole::light && !p.volume &&
                           distance_squared(candidate, p.origin) < 64 * 64;
                });
                if (crowded || candidate == spawn.origin)
                    continue;
                candidates.push_back(candidate);
            }
            std::stable_sort(candidates.begin(), candidates.end(), [&](GridVec3 a, GridVec3 b) {
                return distance_squared(a, spawn.origin) < distance_squared(b, spawn.origin);
            });
            // Bound cost even for dense32-player requests and large room floors.
            if (candidates.size() > 24)
                candidates.resize(24);
            for (const auto &candidate : candidates)
                for (const auto &other : others)
                    queries.push_back({eye(candidate), other});
            if (queries.empty())
                continue;
            const auto results = analyzer.classify_segments(level, queries);
            auto best = spawn.origin;
            auto nearest = std::numeric_limits<long long>::max();
            const auto initial_count = best_count;
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const auto begin = results.begin() + static_cast<std::ptrdiff_t>(i * others.size());
                const auto count =
                    std::count_if(begin, begin + static_cast<std::ptrdiff_t>(others.size()),
                                  [](const auto &r) { return r.kind == SegmentVisibilityKind::visible; });
                const auto distance = distance_squared(candidates[i], spawn.origin);
                if (count < initial_count && (count < best_count || (count == best_count && distance < nearest))) {
                    best = candidates[i];
                    best_count = count;
                    nearest = distance;
                }
            }
            if (best != spawn.origin) {
                spawn.origin = best;
                changed = true;
            }
        }
        if (!changed)
            break;
    }
}

// Preserve the allocator's semantic roles/variants and room assignments while
// binding rotated upper pickups to real complete support instead of a fixed
// world-X offset from the first platform's center.
void bind_items(Layout &level) {
    std::vector<SpawnChoices> spawn_choices;
    for (const auto &room : level.rooms) {
        std::vector<GridVec3> ground, upper;
        for (int x = room.bounds.min.x + 64; x <= room.bounds.max.x - 64; x += 64)
            for (int y = room.bounds.min.y + 64; y <= room.bounds.max.y - 64; y += 64)
                if (clear_supported(level, room, {x, y, 0}))
                    ground.push_back({x, y, 24});
        for (const auto &s : level.solids)
            if (s.purpose == BrushPurpose::raised_platform) {
                const auto b = checked_brush_bounds(s.geometry);
                if (!room.bounds.contains(b.center()))
                    continue;
                for (int x = b.min.x + 32; x <= b.max.x - 32; x += 64)
                    for (int y = b.min.y + 32; y <= b.max.y - 32; y += 64)
                        if (clear_supported(level, room, {x, y, b.max.z}))
                            upper.push_back({x, y, b.max.z + 24});
            }
        std::vector<GridVec3> assigned;
        for (auto &p : level.placements) {
            if (p.role == PlacementRole::light || p.volume || !room.bounds.contains(p.origin))
                continue;
            const bool high = !upper.empty() && (p.role == PlacementRole::weapon || p.role == PlacementRole::armor ||
                                                 (p.role == PlacementRole::deathmatch_spawn && p.id.value % 3 == 0));
            const auto &candidates = high ? upper : ground;
            if (candidates.empty())
                throw std::logic_error("compact motif has no supported item station");
            auto best = candidates.front();
            long double best_score = -std::numeric_limits<long double>::infinity();
            for (const auto &candidate : candidates) {
                long double nearest = 1e12L;
                for (const auto &other : assigned) {
                    const long double dx = candidate.x - other.x, dy = candidate.y - other.y,
                                      dz = candidate.z - other.z;
                    nearest = std::min(nearest, dx * dx + dy * dy + dz * dz);
                }
                const long double dx = candidate.x - p.origin.x, dy = candidate.y - p.origin.y;
                const long double score = p.role == PlacementRole::deathmatch_spawn
                                              ? nearest
                                              : -(dx * dx + dy * dy) - (nearest < 4096 ? 1e10L : 0);
                if (score > best_score) {
                    best_score = score;
                    best = candidate;
                }
            }
            p.origin = best;
            assigned.push_back(best);
            if (p.role == PlacementRole::deathmatch_spawn)
                spawn_choices.push_back({p.id, candidates});
        }
    }
    refine_spawn_visibility(level, spawn_choices);
    // Keep the visibility pass's pair-specific occlusion gains, then consider
    // bounded separation improvements on the same supported room/floor stations.
    if (level.spec.room_count <= 8)
        (void)refine_compact_mp_spawns(level, spawn_choices);
}

} // namespace

OpposedBridgeGeometry make_opposed_bridge_geometry(GridBounds bounds, int rotation, int height) {
    const auto in_range = [](GridVec3 point) {
        return point.x >= -32768 && point.x <= 32768 && point.y >= -32768 && point.y <= 32768 &&
               point.z >= -32768 && point.z <= 32768;
    };
    if (!bounds.valid() || bounds.min.z != 0 || !in_range(bounds.min) || !in_range(bounds.max) ||
        rotation < 0 || rotation > 3 || (height != 128 && height != 192))
        throw std::invalid_argument("opposed bridge requires a bounded floor-zero room, rotation0..3 and height128/192");
    const LocalRoom local{bounds, rotation};
    if (local.width() < 640 || local.depth() < (height == 192 ? 768 : 640) || bounds.max.z < height + 56)
        throw std::invalid_argument("opposed bridge room is too small for the requested standing route");

    const int end = 64 + (height / 16) * 36;
    const int middle = local.width() / 2;
    const int east_begin = local.depth() - end - 160;
    OpposedBridgeGeometry result;
    result.decks = {
        local.box(64, end, middle - 80, end + 160, height - 24, height),
        // Cover both complete joining faces, including long or aligned rooms.
        // A fixed [east_begin,end+160] interval can invert in a long perimeter arm.
        local.box(middle - 80, std::min(end, east_begin), middle + 80,
                  std::max(end + 160, east_begin + 160), height - 24, height),
        local.box(middle + 80, east_begin, local.width() - 64, east_begin + 160, height - 24, height),
    };
    for (int step = 0; step < height / 16; ++step) {
        result.stairs[0].push_back(local.box(64, 64 + step * 36, 192, 64 + (step + 1) * 36, 0, (step + 1) * 16));
        result.stairs[1].push_back(local.box(local.width() - 192, local.depth() - 64 - (step + 1) * 36,
                                            local.width() - 64, local.depth() - 64 - step * 36, 0, (step + 1) * 16));
    }
    return result;
}

void build_compact_mp_layout(Layout &level, const SemanticPlan &plan) {
    require_supported_layout_intent(level.spec);
    if (level.spec.layout_intent == LayoutIntent::wrapped_center && plan.symmetry != Symmetry::none)
        throw std::invalid_argument("wrapped-center layout intent requires asymmetric semantic planning");
    if (level.spec.game != Game::quake2 ||
        (level.spec.mode != GameMode::deathmatch && level.spec.mode != GameMode::duel) || level.spec.room_count < 2 ||
        level.spec.room_count > 32 || !level.rooms.empty() || !level.solids.empty())
        throw std::invalid_argument("compact motifs require an empty two-to-thirty-two room Q2 DM/duel layout");
    std::mt19937_64 random(level.candidate_seed);
    const int count = level.spec.room_count;
    // The ordinary generator advances candidate seeds by an odd stride. Parity
    // offers both families in every six-candidate batch without consuming a draw
    // from the existing row-family geometry stream.
    const bool pinwheel = level.spec.layout_intent == LayoutIntent::wrapped_center ||
                          (count >= 5 && count <= 8 && (level.candidate_seed & 1U));
    int total_width = 0, total_depth = 0;
    if (pinwheel) {
        for (auto bounds : pinwheel_bounds(random, count)) {
            total_width = std::max(total_width, bounds.max.x);
            total_depth = std::max(total_depth, bounds.max.y);
            bounds.max.z = choose(random, 12, 15) * 32;
            Room room;
            room.id = next_room_id(level);
            room.name = "Bay " + std::to_string(room.id.value);
            room.bounds = bounds;
            room.tags = {"compact_mp", to_string(plan.room_roles[level.rooms.size()])};
            if (level.rooms.empty() && room.tags.back() != "start")
                room.tags.push_back("start");
            if (static_cast<int>(level.rooms.size()) == count - 1)
                room.tags.push_back("anchor");
            level.rooms.push_back(std::move(room));
        }
    } else {
        int rows = count == 2 ? 1 : std::max(2, static_cast<int>(std::sqrt(count)));
        if (count >= 6 && choose(random, 0, 1))
            ++rows;
        std::vector<int> row_counts(static_cast<std::size_t>(rows), count / rows);
        for (int i = 0; i < count % rows; ++i)
            ++row_counts[static_cast<std::size_t>(i)];
        shuffle(row_counts, random);
        const int columns = *std::max_element(row_counts.begin(), row_counts.end());
        total_width = columns * 768 + (columns - 1) * 48 + choose(random, 0, 4) * 32;
        int y = 0;
        for (const int row_count : row_counts) {
            const int depth = choose(random, 24, 29) * 32;
            int x = 0, remaining = total_width - (row_count - 1) * 48;
            for (int c = 0; c < row_count; ++c) {
                const int left = row_count - c;
                const int mean = (remaining / left / 16) * 16;
                const int width =
                    left == 1 ? remaining
                              : std::clamp(mean + choose(random, -4, 4) * 16, 640, remaining - (left - 1) * 640);
                Room room;
                room.id = next_room_id(level);
                room.name = "Bay " + std::to_string(room.id.value);
                room.bounds = {{x, y, 0}, {x + width, y + depth, choose(random, 12, 15) * 32}};
                room.tags = {"compact_mp", to_string(plan.room_roles[level.rooms.size()])};
                if (level.rooms.empty() && room.tags.back() != "start")
                    room.tags.push_back("start");
                if (static_cast<int>(level.rooms.size()) == count - 1)
                    room.tags.push_back("anchor");
                level.rooms.push_back(std::move(room));
                x += width + 48;
                remaining -= width;
            }
            y += depth + 48;
        }
        total_depth = y - 48;
    }
    const int quarter = choose(random, 0, 3);
    // Global half-turn/reflection and transpose retain exact face relationships.
    for (auto &room : level.rooms) {
        room.tags.push_back(pinwheel ? "compact_family_pinwheel" : "compact_family_rows");
        auto b = room.bounds;
        b.min.x -= total_width / 2;
        b.max.x -= total_width / 2;
        b.min.y -= total_depth / 2;
        b.max.y -= total_depth / 2;
        if (quarter & 1) {
            std::swap(b.min.x, b.min.y);
            std::swap(b.max.x, b.max.y);
        }
        if (quarter & 2) {
            const int a = b.min.x;
            b.min.x = -b.max.x;
            b.max.x = -a;
        }
        room.bounds = b;
    }
    const auto adjacent = joins(level);
    for (const auto &join : adjacent) {
        const int middle = (join.low + join.high) / 2;
        const int offset = std::min(64, (join.high - join.low - 224) / 2);
        passage(level, join, middle + choose(random, -offset / 16, offset / 16) * 16);
    }
    // Small partitions may expose only one simple cycle. A second physical
    // doorway, separated along a long shared face, supplies an additional route.
    const std::size_t minimum = count == 2 ? 2 : static_cast<std::size_t>(count + 1);
    if (level.connections.size() < minimum && !adjacent.empty()) {
        auto join = *std::max_element(adjacent.begin(), adjacent.end(),
                                      [](const auto &a, const auto &b) { return a.high - a.low < b.high - b.low; });
        const auto existing = std::find_if(level.connections.begin(), level.connections.end(), [&](const auto &c) {
            return c.from_room == join.from && c.to_room == join.to;
        });
        existing->passage_volumes.clear();
        const int a = join.low + 176, b = join.high - 176;
        existing->passage_volumes.push_back(
            join.x_axis ? GridBounds{{join.near_face, a - 80, 0}, {join.far_face, a + 80, 192}}
                        : GridBounds{{a - 80, join.near_face, 0}, {a + 80, join.far_face, 192}});
        passage(level, join, b);
    }

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < level.rooms.size(); ++i)
        order.push_back(i);
    shuffle(order, random);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
        return (plan.room_roles[a] == RoomRole::vertical) > (plan.room_roles[b] == RoomRole::vertical);
    });
    const int elevated =
        level.planned_verticality < .3
            ? 0
            : std::clamp(static_cast<int>(std::ceil(count * (.3 + .4 * level.planned_verticality))), 2, count);
    std::vector<std::pair<LocalRoom, int>> lighting;
    for (int index = 0; index < elevated; ++index) {
        auto &room = level.rooms[order[static_cast<std::size_t>(index)]];
        LocalRoom local{room.bounds, choose(random, 0, 3)};
        const int h = level.planned_verticality >= .8 && local.depth() >= 768 && choose(random, 0, 2) == 0 ? 192 : 128;
        const int end = 64 + (h / 16) * 36;
        const bool opposed = index % 3 == 2;
        const bool horseshoe = index % 2 == 1;
        room.name = (opposed ? "Bridge " : horseshoe ? "Gallery " : "Crossing ") + std::to_string(room.id.value);
        room.tags.push_back(opposed ? "opposed_stair_bridge" : horseshoe ? "horseshoe_gallery" : "raised_crosswalk");
        room.tags.push_back("motif_rotation_" + std::to_string(local.rotation));
        if (opposed) {
            const auto bridge = make_opposed_bridge_geometry(room.bounds, local.rotation, h);
            for (const auto &box : bridge.decks)
                solid(level, box, BrushPurpose::raised_platform, MaterialRole::platform);
            for (const auto &run : bridge.stairs)
                for (const auto &box : run)
                    solid(level, box, BrushPurpose::navigation_stair, MaterialRole::stair);
        } else if (horseshoe) {
            deck(level, local, 64, local.depth() - 224, local.width() - 64, local.depth() - 64, h);
            deck(level, local, 64, end, 224, local.depth() - 224, h);
            deck(level, local, local.width() - 224, end, local.width() - 64, local.depth() - 224, h);
        } else {
            deck(level, local, 64, end, local.width() - 64, end + 160, h);
        }
        if (!opposed) {
            stair(level, local, 64, 64, h);
            stair(level, local, local.width() - 192, 64, h);
        }
        lighting.push_back({local, h});
    }
    // Flat rooms retain an offset view break with two ordinary sides. The core
    // stops below the ceiling and is never counted as an access to an upper route.
    for (auto &room : level.rooms)
        if (std::find(room.tags.begin(), room.tags.end(), "raised_crosswalk") == room.tags.end() &&
            std::find(room.tags.begin(), room.tags.end(), "horseshoe_gallery") == room.tags.end() &&
            std::find(room.tags.begin(), room.tags.end(), "opposed_stair_bridge") == room.tags.end()) {
            LocalRoom local{room.bounds, choose(random, 0, 3)};
            const int u = local.width() * 2 / 3, v = local.depth() * 2 / 3;
            solid(level, local.box(u - 48, v - 64, u + 48, v + 64, 0, 112), BrushPurpose::structural,
                  MaterialRole::wall);
            room.tags.push_back("offset_barrier");
        }
    (void)add_compact_upper_link(level);
    // Change r1s-mp-player-start: the allocator's placements without the map-command start, which is added last
    // (below), so station binding, the spawn refinement and the motif lights never see it.
    EntityAllocator{}.populate_before_station_binding(level);
    bind_items(level);
    for (const auto &[local, h] : lighting) {
        const int end = 64 + (h / 16) * 36;
        light(level, local.point(local.width() / 2, end + 80, 80), 260);
        light(level, local.point(local.width() / 2, local.depth() - 144, h + 112), 280);
    }
    for (const auto &connection : level.connections) {
        auto p = connection.passage_volumes.front().center();
        p.z = connection.passage_volumes.front().min.z + 144;
        light(level, p, 240);
    }
    // Change r1s-mp-player-start (switch r1s.mp_player_start): one info_player_start at a supported, clear station,
    // appended after every other placement so none of them moves or is renumbered (src/entities.cpp).
    (void)add_multiplayer_player_start(level);
}

} // namespace qmg
