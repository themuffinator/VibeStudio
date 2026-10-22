#include "qmg/navigation.hpp"

#include "qmg/layout_fingerprint.hpp"
#include "qmg/quake2_mechanisms.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace qmg {
namespace {

constexpr std::size_t kPortableRoomLimit = 4096;
constexpr std::size_t kPortableConnectionLimit = 8192;
constexpr std::size_t kPortableSolidLimit = 32768;
constexpr std::size_t kPortablePlacementLimit = 16384;
constexpr std::size_t kPortablePassageLimit = 32768;
constexpr std::size_t kPortableConvexPlaneLimit = 64;
constexpr std::size_t kCoverageFragmentLimit = 4096;
constexpr std::int64_t kSweepSegments = 16;
constexpr std::int64_t kSweepScale = kSweepSegments * kSweepSegments;

struct Rect {
    GridCoord min_x{};
    GridCoord min_y{};
    GridCoord max_x{};
    GridCoord max_y{};

    auto operator<=>(const Rect &) const = default;
};

struct SourceVolume {
    NavigationSourceRef source;
    GridBounds bounds;
    std::optional<SolidId> ignored_solid;
    std::optional<PlacementId> ignored_owner;
};

struct Blocker {
    SolidId id;
    GridBounds bounds;
    std::optional<PlacementId> owner;
};

struct PendingNode {
    NavigationSourceRef source;
    NavigationArea area;
};

struct AxialRamp {
    SolidId id;
    GridBounds bounds;
    NavigationSlope slope;
};

struct PendingEdge {
    NavigationNodeId from;
    NavigationNodeId to;
    NavigationTransitionKind kind{NavigationTransitionKind::walk};
    NavigationCost cost{};
    NavigationSourceRef source;
};

struct LiftTransition {
    PlacementId placement;
    SolidId mover;
    NavigationSourceRef first;
    NavigationSourceRef second;
};

struct FixedBounds {
    std::int64_t min_x{};
    std::int64_t min_y{};
    std::int64_t min_z{};
    std::int64_t max_x{};
    std::int64_t max_y{};
    std::int64_t max_z{};

    [[nodiscard]] bool valid() const noexcept { return min_x < max_x && min_y < max_y && min_z < max_z; }
};

struct EdgeKey {
    NavigationNodeId from;
    NavigationNodeId to;
    NavigationTransitionKind kind{NavigationTransitionKind::walk};
    NavigationSourceRef source;

    auto operator<=>(const EdgeKey &) const = default;
};

[[noreturn]] void fail(std::string code, std::string message) {
    throw NavigationError(std::move(code), std::move(message));
}

[[nodiscard]] NavigationSourceRef source_ref(const RoomId id) { return {NavigationSourceKind::room, id.value, 0}; }

[[nodiscard]] NavigationSourceRef source_ref(const ConnectionId id, const std::uint32_t subindex) {
    return {NavigationSourceKind::connection, id.value, subindex};
}

[[nodiscard]] NavigationSourceRef source_ref(const SolidId id) { return {NavigationSourceKind::solid, id.value, 0}; }

[[nodiscard]] NavigationSourceRef source_ref(const PlacementId id, const std::uint32_t subindex = 0) {
    return {NavigationSourceKind::placement, id.value, subindex};
}

[[nodiscard]] bool bounds_less(const GridBounds &left, const GridBounds &right) {
    return std::tie(left.min.x, left.min.y, left.min.z, left.max.x, left.max.y, left.max.z) <
           std::tie(right.min.x, right.min.y, right.min.z, right.max.x, right.max.y, right.max.z);
}

[[nodiscard]] bool within_limit(const GridCoord value, const GridCoord limit) {
    const std::int64_t wide = value;
    return wide >= -static_cast<std::int64_t>(limit) && wide <= limit;
}

[[nodiscard]] bool within_limit(const GridBounds &bounds, const GridCoord limit) {
    return bounds.valid() && within_limit(bounds.min.x, limit) && within_limit(bounds.min.y, limit) &&
           within_limit(bounds.min.z, limit) && within_limit(bounds.max.x, limit) &&
           within_limit(bounds.max.y, limit) && within_limit(bounds.max.z, limit);
}

[[nodiscard]] bool within_limit(const GridVec3 point, const GridCoord limit) {
    return within_limit(point.x, limit) && within_limit(point.y, limit) && within_limit(point.z, limit);
}

[[nodiscard]] GridCoord checked_coordinate(const std::int64_t value, const NavigationOptions &options) {
    if (value < std::numeric_limits<GridCoord>::min() || value > std::numeric_limits<GridCoord>::max())
        fail("coordinate_overflow", "navigation hull arithmetic exceeded the signed grid range");
    const GridCoord result = static_cast<GridCoord>(value);
    if (!within_limit(result, options.max_coordinate))
        fail("coordinate_limit", "navigation hull expansion exceeded the configured coordinate limit");
    return result;
}

[[nodiscard]] std::uint64_t magnitude(const std::int64_t value) {
    return static_cast<std::uint64_t>(value < 0 ? -value : value);
}

[[nodiscard]] NavigationCost checked_cost_add(const NavigationCost left, const NavigationCost right) {
    if (right >= kUnreachableNavigationCost - left)
        fail("cost_overflow", "navigation path cost arithmetic overflowed");
    return left + right;
}

[[nodiscard]] NavigationCost checked_cost_multiply(const NavigationCost left, const NavigationCost right) {
    if (left != 0 && right > (kUnreachableNavigationCost - 1U) / left)
        fail("cost_overflow", "navigation path cost arithmetic overflowed");
    return left * right;
}

[[nodiscard]] NavigationCost anchor_distance_cost(const NavigationNode &from, const NavigationNode &to,
                                                  const NavigationOptions &options, const NavigationCost penalty) {
    const NavigationCost distance = magnitude(static_cast<std::int64_t>(from.origin.x) - to.origin.x) +
                                    magnitude(static_cast<std::int64_t>(from.origin.y) - to.origin.y) +
                                    magnitude(static_cast<std::int64_t>(from.origin.z) - to.origin.z);
    return checked_cost_add(checked_cost_multiply(distance, options.cost_per_unit), penalty);
}

[[nodiscard]] bool positive_overlap(const GridCoord first_min, const GridCoord first_max, const GridCoord second_min,
                                    const GridCoord second_max) {
    return std::max(first_min, second_min) < std::min(first_max, second_max);
}

[[nodiscard]] bool rects_overlap(const Rect &left, const Rect &right) {
    return positive_overlap(left.min_x, left.max_x, right.min_x, right.max_x) &&
           positive_overlap(left.min_y, left.max_y, right.min_y, right.max_y);
}

[[nodiscard]] std::uint64_t interval_gap(const GridCoord first_min, const GridCoord first_max,
                                         const GridCoord second_min, const GridCoord second_max) {
    if (first_max < second_min)
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(second_min) - first_max);
    if (second_max < first_min)
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(first_min) - second_max);
    return 0;
}

[[nodiscard]] std::uint64_t horizontal_gap(const NavigationArea &left, const NavigationArea &right) {
    return std::max(interval_gap(left.min_x, left.max_x, right.min_x, right.max_x),
                    interval_gap(left.min_y, left.max_y, right.min_y, right.max_y));
}

[[nodiscard]] bool laterally_aligned(const NavigationArea &left, const NavigationArea &right) {
    return positive_overlap(left.min_x, left.max_x, right.min_x, right.max_x) ||
           positive_overlap(left.min_y, left.max_y, right.min_y, right.max_y);
}

[[nodiscard]] bool same_floor_contact(const NavigationArea &left, const NavigationArea &right) {
    if (left.floor_z != right.floor_z)
        return false;
    const bool overlap_x = positive_overlap(left.min_x, left.max_x, right.min_x, right.max_x);
    const bool overlap_y = positive_overlap(left.min_y, left.max_y, right.min_y, right.max_y);
    const bool touch_x = left.max_x == right.min_x || right.max_x == left.min_x;
    const bool touch_y = left.max_y == right.min_y || right.max_y == left.min_y;
    return (overlap_x && overlap_y) || (touch_x && overlap_y) || (touch_y && overlap_x);
}

[[nodiscard]] bool physical_contact(const NavigationArea &left, const NavigationArea &right,
                                    const NavigationAgentProfile &profile) {
    const std::int64_t left_min_x = static_cast<std::int64_t>(left.min_x) + profile.hull_mins.x;
    const std::int64_t left_max_x = static_cast<std::int64_t>(left.max_x) + profile.hull_maxs.x;
    const std::int64_t left_min_y = static_cast<std::int64_t>(left.min_y) + profile.hull_mins.y;
    const std::int64_t left_max_y = static_cast<std::int64_t>(left.max_y) + profile.hull_maxs.y;
    const std::int64_t right_min_x = static_cast<std::int64_t>(right.min_x) + profile.hull_mins.x;
    const std::int64_t right_max_x = static_cast<std::int64_t>(right.max_x) + profile.hull_maxs.x;
    const std::int64_t right_min_y = static_cast<std::int64_t>(right.min_y) + profile.hull_mins.y;
    const std::int64_t right_max_y = static_cast<std::int64_t>(right.max_y) + profile.hull_maxs.y;
    const bool overlap_x = std::max(left_min_x, right_min_x) < std::min(left_max_x, right_max_x);
    const bool overlap_y = std::max(left_min_y, right_min_y) < std::min(left_max_y, right_max_y);
    const bool touch_x = left_max_x == right_min_x || right_max_x == left_min_x;
    const bool touch_y = left_max_y == right_min_y || right_max_y == left_min_y;
    return (overlap_x && overlap_y) || (touch_x && overlap_y) || (touch_y && overlap_x);
}

[[nodiscard]] bool support_purpose(const BrushPurpose purpose) {
    return purpose == BrushPurpose::raised_platform || purpose == BrushPurpose::navigation_stair ||
           purpose == BrushPurpose::ramp;
}

[[nodiscard]] bool blocks_navigation(const LayoutSolid &solid) {
    // This ground-navigation graph has no swim transition, so liquids are
    // conservatively non-walkable. Convex solids are likewise represented by
    // their checked aggregate bounds rather than optimistic exact clipping.
    return solid.purpose != BrushPurpose::trigger_volume;
}

// Change r1s-sp-coop-starts: a co-op player spawns at an info_player_coop, so it must bind to supported ground and be
// reachable from the start like every other spawn.
[[nodiscard]] bool spawn_role(const PlacementRole role) {
    return role == PlacementRole::player_start || role == PlacementRole::deathmatch_spawn ||
           role == PlacementRole::team_initial_spawn || role == PlacementRole::team_respawn ||
           role == PlacementRole::coop_start;
}

[[nodiscard]] bool objective_role(const PlacementRole role) {
    return role == PlacementRole::weapon || role == PlacementRole::health || role == PlacementRole::armor ||
           role == PlacementRole::flag || role == PlacementRole::exit_trigger ||
           role == PlacementRole::exit_destination;
}

[[nodiscard]] bool binding_required(const PlacementRole role) {
    return spawn_role(role) || objective_role(role) || role == PlacementRole::monster ||
           role == PlacementRole::teleporter_trigger || role == PlacementRole::teleporter_destination;
}

void add_finding(std::vector<NavigationFinding> &findings, const NavigationFindingKind kind,
                 const NavigationFindingSeverity severity, const std::optional<NavigationSourceRef> source,
                 std::string message, const std::optional<NavigationSourceRef> related = std::nullopt) {
    findings.push_back({severity, kind, source, related, std::move(message)});
}

template <typename Item, typename Projection>
void require_unique_nonzero(const std::vector<const Item *> &items, Projection projection,
                            const std::string_view kind) {
    std::uint32_t previous = 0;
    bool have_previous = false;
    for (const Item *item : items) {
        const std::uint32_t id = projection(*item).value;
        if (id == 0)
            fail("invalid_layout_id", std::string(kind) + " reserves identifier zero");
        if (have_previous && id == previous)
            fail("duplicate_layout_id", std::string(kind) + " identifiers must be unique");
        previous = id;
        have_previous = true;
    }
}

[[nodiscard]] std::vector<const Room *> sorted_rooms(const Layout &layout) {
    std::vector<const Room *> result;
    result.reserve(layout.rooms.size());
    for (const Room &room : layout.rooms)
        result.push_back(&room);
    std::sort(result.begin(), result.end(), [](const Room *left, const Room *right) { return left->id < right->id; });
    require_unique_nonzero(result, [](const Room &room) { return room.id; }, "room");
    return result;
}

[[nodiscard]] std::vector<const Connection *> sorted_connections(const Layout &layout) {
    std::vector<const Connection *> result;
    result.reserve(layout.connections.size());
    for (const Connection &connection : layout.connections)
        result.push_back(&connection);
    std::sort(result.begin(), result.end(),
              [](const Connection *left, const Connection *right) { return left->id < right->id; });
    require_unique_nonzero(result, [](const Connection &connection) { return connection.id; }, "connection");
    return result;
}

[[nodiscard]] std::vector<const LayoutSolid *> sorted_solids(const Layout &layout) {
    std::vector<const LayoutSolid *> result;
    result.reserve(layout.solids.size());
    for (const LayoutSolid &solid : layout.solids)
        result.push_back(&solid);
    std::sort(result.begin(), result.end(),
              [](const LayoutSolid *left, const LayoutSolid *right) { return left->id < right->id; });
    require_unique_nonzero(result, [](const LayoutSolid &solid) { return solid.id; }, "solid");
    return result;
}

[[nodiscard]] std::vector<const Placement *> sorted_placements(const Layout &layout) {
    std::vector<const Placement *> result;
    result.reserve(layout.placements.size());
    for (const Placement &placement : layout.placements)
        result.push_back(&placement);
    std::sort(result.begin(), result.end(),
              [](const Placement *left, const Placement *right) { return left->id < right->id; });
    require_unique_nonzero(result, [](const Placement &placement) { return placement.id; }, "placement");
    return result;
}

[[nodiscard]] const Placement *find_sorted_placement(const std::vector<const Placement *> &placements,
                                                     const PlacementId id) {
    const auto found =
        std::lower_bound(placements.begin(), placements.end(), id,
                         [](const Placement *placement, const PlacementId wanted) { return placement->id < wanted; });
    return found != placements.end() && (*found)->id == id ? *found : nullptr;
}

[[nodiscard]] const Room *find_sorted_room(const std::vector<const Room *> &rooms, const RoomId id) {
    const auto found = std::lower_bound(rooms.begin(), rooms.end(), id,
                                        [](const Room *room, const RoomId wanted) { return room->id < wanted; });
    return found != rooms.end() && (*found)->id == id ? *found : nullptr;
}

} // namespace

NavigationError::NavigationError(std::string code, std::string message)
    : std::runtime_error(code + ": " + message), code_(std::move(code)) {}

GridCoord navigation_floor_z(const NavigationArea &area, const GridCoord x, const GridCoord y) {
    if (!area.contains_xy({x, y, 0}))
        fail("invalid_endpoint_position", "floor query is outside its navigation area");
    if (!area.slope)
        return area.floor_z;
    const NavigationSlope &slope = *area.slope;
    // The exact supported wedge contract is bounded independently of custom
    // world limits, keeping every product below 2^35 without floating point.
    const std::int64_t coordinate = slope.along_x ? x : y;
    const std::int64_t signed_run = static_cast<std::int64_t>(slope.end) - slope.start;
    const std::int64_t run = static_cast<std::int64_t>(magnitude(signed_run));
    if (!within_limit(slope.start, 32768) || !within_limit(slope.end, 32768) || !within_limit(slope.low_z, 32768) ||
        !within_limit(slope.support_offset, 32768) || magnitude(coordinate) > 65536U || run == 0 || slope.rise <= 0 ||
        slope.rise > run)
        fail("invalid_slope", "navigation slope exceeds the supported exact axial contract");
    const std::int64_t distance = (coordinate - slope.start + slope.support_offset) * (signed_run > 0 ? 1 : -1);
    const std::int64_t numerator = static_cast<std::int64_t>(slope.low_z) * run + slope.rise * distance;
    const std::int64_t height = numerator / run + (numerator % run > 0 ? 1 : 0);
    return static_cast<GridCoord>(height);
}

NavigationAgentProfile standard_navigation_profile(const Game game, const NavigationAgentKind kind) {
    NavigationAgentProfile profile;
    profile.game = game;
    profile.kind = kind;
    if (game == Game::quake3 && kind != NavigationAgentKind::player)
        fail("unsupported_agent_profile", "stock Quake 3 has no ground-monster navigation profile");
    if (kind == NavigationAgentKind::monster_large) {
        profile.hull_mins = {-32, -32, -24};
        profile.hull_maxs = {32, 32, 64};
    } else if (game == Game::quake3) {
        profile.hull_mins = {-15, -15, -24};
        profile.hull_maxs = {15, 15, 32};
    }
    if (kind != NavigationAgentKind::player) {
        profile.max_jump_up = 0;
        profile.max_jump_gap = 0;
        profile.max_drop = 0;
    }
    return profile;
}

namespace {

[[nodiscard]] std::optional<AxialRamp> axial_ramp(const LayoutSolid &solid, const NavigationAgentProfile &profile) {
    const ConvexPolyhedron *polyhedron = get_convex_polyhedron(solid.geometry);
    if (polyhedron == nullptr || polyhedron->planes.size() != 6 || solid.owner ||
        solid.ownership == BrushOwnership::placement || solid.contents != BrushContents::solid)
        return std::nullopt;
    std::array<std::optional<GridCoord>, 4> sides;
    std::optional<GridCoord> bottom;
    std::optional<std::array<std::int64_t, 4>> top;
    for (const PlanePointTriple &plane : polyhedron->planes) {
        if (!within_limit(plane.first, 32768) || !within_limit(plane.second, 32768) ||
            !within_limit(plane.third, 32768))
            return std::nullopt;
        const std::array<std::int64_t, 3> a{static_cast<std::int64_t>(plane.second.x) - plane.first.x,
                                            static_cast<std::int64_t>(plane.second.y) - plane.first.y,
                                            static_cast<std::int64_t>(plane.second.z) - plane.first.z};
        const std::array<std::int64_t, 3> b{static_cast<std::int64_t>(plane.third.x) - plane.first.x,
                                            static_cast<std::int64_t>(plane.third.y) - plane.first.y,
                                            static_cast<std::int64_t>(plane.third.z) - plane.first.z};
        const std::array<std::int64_t, 3> normal{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                                                 a[0] * b[1] - a[1] * b[0]};
        const bool x = normal[0] != 0;
        const bool y = normal[1] != 0;
        if (normal[2] == 0 && x != y) {
            const std::size_t axis = x ? 0U : 1U;
            const std::size_t index = axis * 2U + (normal[axis] > 0 ? 1U : 0U);
            if (sides[index])
                return std::nullopt;
            sides[index] = x ? plane.first.x : plane.first.y;
        } else if (!x && !y && normal[2] < 0) {
            if (bottom)
                return std::nullopt;
            bottom = plane.first.z;
        } else if (x != y && normal[2] > 0) {
            if (top)
                return std::nullopt;
            top = {normal[0], normal[1], normal[2],
                   normal[0] * plane.first.x + normal[1] * plane.first.y + normal[2] * plane.first.z};
        } else {
            return std::nullopt;
        }
    }
    if (!bottom || !top || std::any_of(sides.begin(), sides.end(), [](const auto &side) { return !side; }))
        return std::nullopt;
    const bool along_x = (*top)[0] != 0;
    const std::int64_t gradient = (*top)[along_x ? 0U : 1U];
    const bool positive = gradient < 0;
    const GridCoord minimum = *sides[along_x ? 0U : 2U];
    const GridCoord maximum = *sides[along_x ? 1U : 3U];
    const GridCoord start = positive ? minimum : maximum;
    const GridCoord end = positive ? maximum : minimum;
    const std::int64_t low_numerator = (*top)[3] - gradient * start;
    const std::int64_t high_numerator = (*top)[3] - gradient * end;
    if (minimum >= maximum || low_numerator % (*top)[2] != 0 || high_numerator % (*top)[2] != 0)
        return std::nullopt;
    const std::int64_t low = low_numerator / (*top)[2];
    const std::int64_t high = high_numerator / (*top)[2];
    const std::int64_t rise = high - low;
    // 1:1 gives normal.z = 1/sqrt(2) > the stock 0.7 ground threshold.
    if (low <= *bottom || high > 32768 || low < -32768 || rise <= 0 ||
        rise > static_cast<std::int64_t>(maximum) - minimum)
        return std::nullopt;
    const GridCoord offset = along_x ? (positive ? profile.hull_maxs.x : profile.hull_mins.x)
                                     : (positive ? profile.hull_maxs.y : profile.hull_mins.y);
    if (!within_limit(offset, 32768))
        return std::nullopt;
    const GridBounds bounds{{*sides[0], *sides[2], *bottom}, {*sides[1], *sides[3], static_cast<GridCoord>(high)}};
    if (!bounds.valid())
        return std::nullopt;
    return AxialRamp{
        solid.id, bounds, {along_x, start, end, static_cast<GridCoord>(low), static_cast<GridCoord>(rise), offset}};
}

void validate_options_and_profile(const NavigationOptions &options, const NavigationAgentProfile &profile,
                                  const Game game) {
    if (options.max_coordinate <= 0 || options.placement_snap_height < 0 || options.max_mover_side_distance <= 0 ||
        options.cost_per_unit == 0 || options.max_rooms == 0 || options.max_connections == 0 ||
        options.max_solids == 0 || options.max_placements == 0 || options.max_passage_volumes == 0 ||
        options.max_convex_planes < 4 || options.max_source_volumes == 0 || options.max_grid_cells == 0 ||
        options.max_nodes == 0 || options.max_edges == 0 || options.max_pair_checks == 0 ||
        options.max_path_expansions == 0) {
        fail("invalid_navigation_options", "navigation bounds, costs, and caps must be positive");
    }
    if (options.max_rooms > kPortableRoomLimit || options.max_connections > kPortableConnectionLimit ||
        options.max_solids > kPortableSolidLimit || options.max_placements > kPortablePlacementLimit ||
        options.max_passage_volumes > kPortablePassageLimit || options.max_convex_planes > kPortableConvexPlaneLimit) {
        fail("invalid_navigation_options", "navigation input caps exceed their portable hard limits");
    }
    if (profile.game != game)
        fail("agent_title_mismatch", "navigation agent profile does not match the Layout title");
    if (game == Game::quake3 && profile.kind != NavigationAgentKind::player)
        fail("unsupported_agent_profile", "stock Quake 3 has no ground-monster navigation profile");
    if (profile.hull_mins.x >= profile.hull_maxs.x || profile.hull_mins.y >= profile.hull_maxs.y ||
        profile.hull_mins.z >= profile.hull_maxs.z || profile.hull_mins.z > 0 || profile.hull_maxs.z <= 0 ||
        profile.max_step_up < 0 || profile.max_jump_up < 0 || profile.max_jump_gap < 0 || profile.max_drop < 0) {
        fail("invalid_agent_profile", "navigation agent hull and movement limits are invalid");
    }
    if (profile.kind != NavigationAgentKind::player && (profile.max_jump_up != 0 || profile.max_jump_gap != 0))
        fail("invalid_agent_profile", "ground-monster profiles cannot plan jump transitions");
}

void validate_layout_caps(const Layout &layout, const NavigationOptions &options) {
    if (layout.rooms.size() > options.max_rooms)
        fail("room_limit", "navigation room count exceeds its configured cap");
    if (layout.connections.size() > options.max_connections)
        fail("connection_limit", "navigation connection count exceeds its configured cap");
    if (layout.solids.size() > options.max_solids)
        fail("solid_limit", "navigation solid count exceeds its configured cap");
    if (layout.placements.size() > options.max_placements)
        fail("placement_limit", "navigation placement count exceeds its configured cap");

    std::size_t passage_count = 0;
    for (const Connection &connection : layout.connections) {
        if (connection.passage_volumes.size() > options.max_passage_volumes - passage_count)
            fail("passage_limit", "navigation passage-volume count exceeds its configured cap");
        passage_count += connection.passage_volumes.size();
    }
    for (const LayoutSolid &solid : layout.solids) {
        const ConvexPolyhedron *polyhedron = get_convex_polyhedron(solid.geometry);
        if (polyhedron != nullptr && polyhedron->planes.size() > options.max_convex_planes)
            fail("convex_plane_limit", "navigation convex solid exceeds its configured plane cap");
    }
}

[[nodiscard]] Rect clipped_rect(const Rect &value, const Rect &clip) {
    return {std::max(value.min_x, clip.min_x), std::max(value.min_y, clip.min_y), std::min(value.max_x, clip.max_x),
            std::min(value.max_y, clip.max_y)};
}

[[nodiscard]] std::size_t checked_cell_count(const std::size_t x_count, const std::size_t y_count,
                                             const NavigationOptions &options, NavigationStats &stats) {
    if (x_count != 0 && y_count > std::numeric_limits<std::size_t>::max() / x_count)
        fail("grid_cell_limit", "navigation coordinate-compression cell count overflowed");
    const std::size_t cells = x_count * y_count;
    if (stats.evaluated_grid_cells > options.max_grid_cells ||
        cells > options.max_grid_cells - stats.evaluated_grid_cells) {
        fail("grid_cell_limit", "navigation coordinate-compression exceeded its cell cap");
    }
    stats.evaluated_grid_cells += cells;
    return cells;
}

struct SupportSlice {
    Rect footprint;
    GridCoord ceiling{};
};

[[nodiscard]] std::vector<SourceVolume> tile_support_top(const NavigationSourceRef source, const GridBounds &support,
                                                         const GridCoord floor,
                                                         const std::vector<SourceVolume> &free_volumes,
                                                         const std::optional<SolidId> ignored_solid,
                                                         const std::optional<PlacementId> ignored_owner,
                                                         const NavigationOptions &options, NavigationStats &stats) {
    const Rect support_footprint{support.min.x, support.min.y, support.max.x, support.max.y};
    std::vector<SupportSlice> slices;
    for (const SourceVolume &free : free_volumes) {
        if (floor < free.bounds.min.z || floor >= free.bounds.max.z)
            continue;
        const Rect overlap = clipped_rect(support_footprint,
                                          {free.bounds.min.x, free.bounds.min.y, free.bounds.max.x, free.bounds.max.y});
        if (overlap.min_x < overlap.max_x && overlap.min_y < overlap.max_y)
            slices.push_back({overlap, free.bounds.max.z});
    }
    if (slices.empty())
        return {};
    std::sort(slices.begin(), slices.end(), [](const SupportSlice &left, const SupportSlice &right) {
        return std::tie(left.footprint, left.ceiling) < std::tie(right.footprint, right.ceiling);
    });
    slices.erase(std::unique(slices.begin(), slices.end(),
                             [](const SupportSlice &left, const SupportSlice &right) {
                                 return left.footprint == right.footprint && left.ceiling == right.ceiling;
                             }),
                 slices.end());

    std::vector<GridCoord> xs;
    std::vector<GridCoord> ys;
    xs.reserve(slices.size() * 2);
    ys.reserve(slices.size() * 2);
    for (const SupportSlice &slice : slices) {
        xs.push_back(slice.footprint.min_x);
        xs.push_back(slice.footprint.max_x);
        ys.push_back(slice.footprint.min_y);
        ys.push_back(slice.footprint.max_y);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    const std::size_t width = xs.size() - 1;
    const std::size_t height = ys.size() - 1;
    const std::size_t cell_count = checked_cell_count(width, height, options, stats);
    const auto cell_index = [width](const std::size_t x, const std::size_t y) { return y * width + x; };
    std::vector<std::optional<GridCoord>> ceilings(cell_count);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const Rect cell{xs[x], ys[y], xs[x + 1], ys[y + 1]};
            for (const SupportSlice &slice : slices) {
                if (!rects_overlap(cell, slice.footprint))
                    continue;
                std::optional<GridCoord> &ceiling = ceilings[cell_index(x, y)];
                if (!ceiling || slice.ceiling > *ceiling)
                    ceiling = slice.ceiling;
            }
        }
    }

    std::vector<bool> consumed(cell_count, false);
    std::vector<SourceVolume> result;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t first_index = cell_index(x, y);
            if (!ceilings[first_index] || consumed[first_index])
                continue;
            const GridCoord ceiling = *ceilings[first_index];
            std::size_t end_x = x + 1;
            while (end_x < width && !consumed[cell_index(end_x, y)] && ceilings[cell_index(end_x, y)] == ceiling) {
                ++end_x;
            }
            std::size_t end_y = y + 1;
            while (end_y < height) {
                bool matching_row = true;
                for (std::size_t test_x = x; test_x < end_x; ++test_x) {
                    if (consumed[cell_index(test_x, end_y)] || ceilings[cell_index(test_x, end_y)] != ceiling) {
                        matching_row = false;
                        break;
                    }
                }
                if (!matching_row)
                    break;
                ++end_y;
            }
            for (std::size_t consume_y = y; consume_y < end_y; ++consume_y) {
                for (std::size_t consume_x = x; consume_x < end_x; ++consume_x)
                    consumed[cell_index(consume_x, consume_y)] = true;
            }
            result.push_back(
                {source, {{xs[x], ys[y], floor}, {xs[end_x], ys[end_y], ceiling}}, ignored_solid, ignored_owner});
            if (result.size() > options.max_source_volumes)
                fail("source_volume_limit", "support tiling exceeded the navigation source-volume cap");
        }
    }
    return result;
}

void append_source_tiles(std::vector<SourceVolume> &sources, const std::vector<SourceVolume> &tiles,
                         const NavigationOptions &options) {
    if (tiles.size() > options.max_source_volumes - std::min(sources.size(), options.max_source_volumes))
        fail("source_volume_limit", "navigation source-volume cap was exceeded before graph partitioning");
    sources.insert(sources.end(), tiles.begin(), tiles.end());
}

void partition_source(const SourceVolume &source, const std::vector<Blocker> &blockers,
                      const NavigationAgentProfile &profile, const NavigationOptions &options,
                      std::vector<PendingNode> &nodes, std::vector<NavigationFinding> &findings,
                      NavigationStats &stats) {
    ++stats.source_volumes;
    if (stats.source_volumes > options.max_source_volumes)
        fail("source_volume_limit", "navigation source-volume cap was exceeded");
    const std::int64_t hull_height = static_cast<std::int64_t>(profile.hull_maxs.z) - profile.hull_mins.z;
    const std::int64_t available_height = static_cast<std::int64_t>(source.bounds.max.z) - source.bounds.min.z;
    const Rect allowed{
        checked_coordinate(static_cast<std::int64_t>(source.bounds.min.x) - profile.hull_mins.x, options),
        checked_coordinate(static_cast<std::int64_t>(source.bounds.min.y) - profile.hull_mins.y, options),
        checked_coordinate(static_cast<std::int64_t>(source.bounds.max.x) - profile.hull_maxs.x, options),
        checked_coordinate(static_cast<std::int64_t>(source.bounds.max.y) - profile.hull_maxs.y, options),
    };
    if (allowed.min_x >= allowed.max_x || allowed.min_y >= allowed.max_y || available_height < hull_height) {
        ++stats.rejected_source_volumes;
        add_finding(findings, NavigationFindingKind::insufficient_clearance, NavigationFindingSeverity::error,
                    source.source, "source volume cannot contain the selected navigation hull");
        return;
    }

    std::vector<Rect> expanded_blockers;
    for (const Blocker &blocker : blockers) {
        if (source.ignored_solid == blocker.id || (source.ignored_owner && blocker.owner == source.ignored_owner))
            continue;
        const std::int64_t agent_top = static_cast<std::int64_t>(source.bounds.min.z) + hull_height;
        if (static_cast<std::int64_t>(blocker.bounds.min.z) >= agent_top ||
            blocker.bounds.max.z <= source.bounds.min.z) {
            continue;
        }
        const Rect expanded{
            checked_coordinate(static_cast<std::int64_t>(blocker.bounds.min.x) - profile.hull_maxs.x, options),
            checked_coordinate(static_cast<std::int64_t>(blocker.bounds.min.y) - profile.hull_maxs.y, options),
            checked_coordinate(static_cast<std::int64_t>(blocker.bounds.max.x) - profile.hull_mins.x, options),
            checked_coordinate(static_cast<std::int64_t>(blocker.bounds.max.y) - profile.hull_mins.y, options),
        };
        const Rect clipped = clipped_rect(expanded, allowed);
        if (clipped.min_x < clipped.max_x && clipped.min_y < clipped.max_y)
            expanded_blockers.push_back(clipped);
    }
    std::sort(expanded_blockers.begin(), expanded_blockers.end());
    expanded_blockers.erase(std::unique(expanded_blockers.begin(), expanded_blockers.end()), expanded_blockers.end());

    std::vector<GridCoord> xs{allowed.min_x, allowed.max_x};
    std::vector<GridCoord> ys{allowed.min_y, allowed.max_y};
    for (const Rect blocker : expanded_blockers) {
        xs.push_back(blocker.min_x);
        xs.push_back(blocker.max_x);
        ys.push_back(blocker.min_y);
        ys.push_back(blocker.max_y);
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    const std::size_t width = xs.size() - 1;
    const std::size_t height = ys.size() - 1;
    const std::size_t cell_count = checked_cell_count(width, height, options, stats);
    std::vector<bool> open(cell_count, true);
    const auto cell_index = [width](const std::size_t x, const std::size_t y) { return y * width + x; };
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const Rect cell{xs[x], ys[y], xs[x + 1], ys[y + 1]};
            if (std::any_of(expanded_blockers.begin(), expanded_blockers.end(),
                            [&](const Rect blocker) { return rects_overlap(cell, blocker); })) {
                open[cell_index(x, y)] = false;
            }
        }
    }

    std::vector<bool> consumed(cell_count, false);
    std::size_t emitted = 0;
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            if (!open[cell_index(x, y)] || consumed[cell_index(x, y)])
                continue;
            std::size_t end_x = x + 1;
            while (end_x < width && open[cell_index(end_x, y)] && !consumed[cell_index(end_x, y)])
                ++end_x;
            std::size_t end_y = y + 1;
            while (end_y < height) {
                bool row_open = true;
                for (std::size_t test_x = x; test_x < end_x; ++test_x) {
                    if (!open[cell_index(test_x, end_y)] || consumed[cell_index(test_x, end_y)]) {
                        row_open = false;
                        break;
                    }
                }
                if (!row_open)
                    break;
                ++end_y;
            }
            for (std::size_t consume_y = y; consume_y < end_y; ++consume_y) {
                for (std::size_t consume_x = x; consume_x < end_x; ++consume_x)
                    consumed[cell_index(consume_x, consume_y)] = true;
            }
            NavigationArea area{xs[x], ys[y], xs[end_x], ys[end_y], source.bounds.min.z, source.bounds.max.z};
            if (area.valid()) {
                nodes.push_back({source.source, area});
                ++emitted;
                if (nodes.size() > options.max_nodes)
                    fail("node_limit", "navigation node cap was exceeded");
            }
        }
    }
    if (emitted == 0) {
        ++stats.rejected_source_volumes;
        add_finding(findings, NavigationFindingKind::insufficient_clearance, NavigationFindingSeverity::error,
                    source.source, "solid obstacles consume all hull-cleared navigation space");
    }
}

struct FixedPoint {
    std::int64_t x{};
    std::int64_t y{};
    std::int64_t z{};
};

[[nodiscard]] std::int64_t scaled(const GridCoord value) noexcept {
    return static_cast<std::int64_t>(value) * kSweepScale;
}

[[nodiscard]] FixedBounds fixed_bounds(const GridBounds &bounds) noexcept {
    return {scaled(bounds.min.x), scaled(bounds.min.y), scaled(bounds.min.z),
            scaled(bounds.max.x), scaled(bounds.max.y), scaled(bounds.max.z)};
}

[[nodiscard]] std::optional<FixedBounds> intersect_bounds(const FixedBounds &left, const FixedBounds &right) noexcept {
    FixedBounds result{std::max(left.min_x, right.min_x), std::max(left.min_y, right.min_y),
                       std::max(left.min_z, right.min_z), std::min(left.max_x, right.max_x),
                       std::min(left.max_y, right.max_y), std::min(left.max_z, right.max_z)};
    return result.valid() ? std::optional{result} : std::nullopt;
}

void append_if_valid(std::vector<FixedBounds> &bounds, const FixedBounds candidate) {
    if (candidate.valid())
        bounds.push_back(candidate);
}

void subtract_covered_bounds(const FixedBounds &source, const FixedBounds &covered, std::vector<FixedBounds> &result) {
    append_if_valid(result, {source.min_x, source.min_y, source.min_z, covered.min_x, source.max_y, source.max_z});
    append_if_valid(result, {covered.max_x, source.min_y, source.min_z, source.max_x, source.max_y, source.max_z});
    append_if_valid(result, {covered.min_x, source.min_y, source.min_z, covered.max_x, covered.min_y, source.max_z});
    append_if_valid(result, {covered.min_x, covered.max_y, source.min_z, covered.max_x, source.max_y, source.max_z});
    append_if_valid(result, {covered.min_x, covered.min_y, source.min_z, covered.max_x, covered.max_y, covered.min_z});
    append_if_valid(result, {covered.min_x, covered.min_y, covered.max_z, covered.max_x, covered.max_y, source.max_z});
}

[[nodiscard]] bool covered_by_free_union(const FixedBounds &query, const std::vector<SourceVolume> &free_volumes) {
    std::vector<FixedBounds> uncovered{query};
    std::vector<FixedBounds> next;
    for (const SourceVolume &free : free_volumes) {
        next.clear();
        for (const FixedBounds &part : uncovered) {
            const std::optional<FixedBounds> overlap = intersect_bounds(part, fixed_bounds(free.bounds));
            if (!overlap) {
                next.push_back(part);
                continue;
            }
            subtract_covered_bounds(part, *overlap, next);
            if (next.size() > kCoverageFragmentLimit)
                return false;
        }
        uncovered.swap(next);
        if (uncovered.empty())
            return true;
    }
    return false;
}

[[nodiscard]] bool overlaps_blocker(const FixedBounds &query, const std::vector<Blocker> &blockers) {
    return std::any_of(blockers.begin(), blockers.end(), [&](const Blocker &blocker) {
        return intersect_bounds(query, fixed_bounds(blocker.bounds)).has_value();
    });
}

[[nodiscard]] bool overlaps_blocker_other_than(const FixedBounds &query, const std::vector<Blocker> &blockers,
                                               const SolidId ignored_solid) {
    return std::any_of(blockers.begin(), blockers.end(), [&](const Blocker &blocker) {
        return blocker.id != ignored_solid && intersect_bounds(query, fixed_bounds(blocker.bounds)).has_value();
    });
}

void append_ramp_node(const AxialRamp &ramp, const std::vector<SourceVolume> &free_volumes,
                      const std::vector<Blocker> &blockers, const NavigationAgentProfile &profile,
                      const NavigationOptions &options, std::vector<PendingNode> &nodes,
                      std::vector<NavigationFinding> &findings, NavigationStats &stats) {
    if (++stats.source_volumes > options.max_source_volumes)
        fail("source_volume_limit", "navigation ramp source-volume cap was exceeded");
    static_cast<void>(checked_cell_count(1, 1, options, stats));
    NavigationArea area{checked_coordinate(static_cast<std::int64_t>(ramp.bounds.min.x) - profile.hull_mins.x, options),
                        checked_coordinate(static_cast<std::int64_t>(ramp.bounds.min.y) - profile.hull_mins.y, options),
                        checked_coordinate(static_cast<std::int64_t>(ramp.bounds.max.x) - profile.hull_maxs.x, options),
                        checked_coordinate(static_cast<std::int64_t>(ramp.bounds.max.y) - profile.hull_maxs.y, options),
                        0,
                        0,
                        ramp.slope};
    bool clear = area.min_x < area.max_x && area.min_y < area.max_y;
    if (clear) {
        const GridCoord first = navigation_floor_z(area, area.min_x, area.min_y);
        const GridCoord second = navigation_floor_z(area, area.max_x, area.max_y);
        area.floor_z = std::min(first, second);
        area.ceiling_z = checked_coordinate(
            static_cast<std::int64_t>(std::max(first, second)) + profile.hull_maxs.z - profile.hull_mins.z, options);
        // Every point of this continuous affine support has its complete hull
        // inside this enclosing prism. It is deliberately conservative near
        // overhead obstructions; no sampled collision holes or fake treads.
        const FixedBounds envelope = fixed_bounds({{ramp.bounds.min.x, ramp.bounds.min.y, area.floor_z},
                                                   {ramp.bounds.max.x, ramp.bounds.max.y, area.ceiling_z}});
        clear =
            !overlaps_blocker_other_than(envelope, blockers, ramp.id) && covered_by_free_union(envelope, free_volumes);
    }
    if (!clear) {
        ++stats.rejected_source_volumes;
        add_finding(findings, NavigationFindingKind::insufficient_clearance, NavigationFindingSeverity::error,
                    source_ref(ramp.id),
                    "continuous ramp standing-hull envelope is obstructed or outside explicit free space");
        return;
    }
    nodes.push_back({source_ref(ramp.id), area});
    if (nodes.size() > options.max_nodes)
        fail("node_limit", "navigation node cap was exceeded by a ramp");
}

[[nodiscard]] FixedBounds swept_volume_bounds(const GridBounds &from, const GridBounds &to) noexcept {
    return {scaled(std::min(from.min.x, to.min.x)), scaled(std::min(from.min.y, to.min.y)),
            scaled(std::min(from.min.z, to.min.z)), scaled(std::max(from.max.x, to.max.x)),
            scaled(std::max(from.max.y, to.max.y)), scaled(std::max(from.max.z, to.max.z))};
}

[[nodiscard]] GridBounds translated_bounds(const GridBounds &bounds, const std::array<std::int64_t, 3> &delta,
                                           const NavigationOptions &options) {
    return {{checked_coordinate(static_cast<std::int64_t>(bounds.min.x) + delta[0], options),
             checked_coordinate(static_cast<std::int64_t>(bounds.min.y) + delta[1], options),
             checked_coordinate(static_cast<std::int64_t>(bounds.min.z) + delta[2], options)},
            {checked_coordinate(static_cast<std::int64_t>(bounds.max.x) + delta[0], options),
             checked_coordinate(static_cast<std::int64_t>(bounds.max.y) + delta[1], options),
             checked_coordinate(static_cast<std::int64_t>(bounds.max.z) + delta[2], options)}};
}

[[nodiscard]] bool clear_mover_sweep(const GridBounds &from, const GridBounds &to, const SolidId mover,
                                     const std::vector<SourceVolume> &free_volumes,
                                     const std::vector<Blocker> &blockers) {
    const FixedBounds query = swept_volume_bounds(from, to);
    return !overlaps_blocker_other_than(query, blockers, mover) && covered_by_free_union(query, free_volumes);
}

struct TransitionEndpoints {
    GridCoord from_x{};
    GridCoord from_y{};
    GridCoord to_x{};
    GridCoord to_y{};
};

struct AxisEndpoints {
    GridCoord from{};
    GridCoord to{};
};

[[nodiscard]] AxisEndpoints closest_axis_endpoints(const GridCoord from_min, const GridCoord from_max,
                                                   const GridCoord to_min, const GridCoord to_max) {
    const GridCoord shared_min = std::max(from_min, to_min);
    const GridCoord shared_max = std::min(from_max, to_max);
    if (shared_min <= shared_max) {
        const GridCoord midpoint = static_cast<GridCoord>((static_cast<std::int64_t>(shared_min) + shared_max) / 2);
        return {midpoint, midpoint};
    }
    return from_max < to_min ? AxisEndpoints{from_max, to_min} : AxisEndpoints{from_min, to_max};
}

[[nodiscard]] TransitionEndpoints closest_endpoints(const NavigationArea &from, const NavigationArea &to) {
    const AxisEndpoints x = closest_axis_endpoints(from.min_x, from.max_x, to.min_x, to.max_x);
    const AxisEndpoints y = closest_axis_endpoints(from.min_y, from.max_y, to.min_y, to.max_y);
    return {x.from, y.from, x.to, y.to};
}

[[nodiscard]] FixedPoint fixed_agent_origin(const GridCoord x, const GridCoord y, const GridCoord floor,
                                            const NavigationAgentProfile &profile) noexcept {
    return {scaled(x), scaled(y), (static_cast<std::int64_t>(floor) - profile.hull_mins.z) * kSweepScale};
}

[[nodiscard]] FixedBounds swept_hull_bounds(const FixedPoint &from, const FixedPoint &to,
                                            const NavigationAgentProfile &profile,
                                            const std::int64_t upward_curve_allowance = 0) noexcept {
    return {std::min(from.x, to.x) + static_cast<std::int64_t>(profile.hull_mins.x) * kSweepScale,
            std::min(from.y, to.y) + static_cast<std::int64_t>(profile.hull_mins.y) * kSweepScale,
            std::min(from.z, to.z) + static_cast<std::int64_t>(profile.hull_mins.z) * kSweepScale,
            std::max(from.x, to.x) + static_cast<std::int64_t>(profile.hull_maxs.x) * kSweepScale,
            std::max(from.y, to.y) + static_cast<std::int64_t>(profile.hull_maxs.y) * kSweepScale,
            std::max(from.z, to.z) + static_cast<std::int64_t>(profile.hull_maxs.z) * kSweepScale +
                upward_curve_allowance};
}

[[nodiscard]] bool clear_sweep_segment(const FixedPoint &from, const FixedPoint &to,
                                       const NavigationAgentProfile &profile,
                                       const std::vector<SourceVolume> &free_volumes,
                                       const std::vector<Blocker> &blockers,
                                       const std::int64_t upward_curve_allowance = 0) {
    const FixedBounds query = swept_hull_bounds(from, to, profile, upward_curve_allowance);
    return !overlaps_blocker(query, blockers) && covered_by_free_union(query, free_volumes);
}

[[nodiscard]] bool clear_linear_transition(const NavigationNode &from, const NavigationNode &to,
                                           const NavigationTransitionKind kind, const NavigationAgentProfile &profile,
                                           const std::vector<SourceVolume> &free_volumes,
                                           const std::vector<Blocker> &blockers) {
    const TransitionEndpoints endpoints = closest_endpoints(from.area, to.area);
    const FixedPoint start = fixed_agent_origin(endpoints.from_x, endpoints.from_y, from.area.floor_z, profile);
    const FixedPoint finish = fixed_agent_origin(endpoints.to_x, endpoints.to_y, to.area.floor_z, profile);
    if (kind != NavigationTransitionKind::step && kind != NavigationTransitionKind::drop)
        return clear_sweep_segment(start, finish, profile, free_volumes, blockers);

    if (to.area.floor_z > from.area.floor_z) {
        const FixedPoint raised{start.x, start.y, finish.z};
        return clear_sweep_segment(start, raised, profile, free_volumes, blockers) &&
               clear_sweep_segment(raised, finish, profile, free_volumes, blockers);
    }
    const FixedPoint over_destination{finish.x, finish.y, start.z};
    return clear_sweep_segment(start, over_destination, profile, free_volumes, blockers) &&
           clear_sweep_segment(over_destination, finish, profile, free_volumes, blockers);
}

[[nodiscard]] FixedPoint jump_point(const FixedPoint &start, const FixedPoint &finish, const std::int64_t sample,
                                    const GridCoord arc_lift) noexcept {
    const std::int64_t remaining = kSweepSegments - sample;
    const auto interpolate = [&](const std::int64_t first, const std::int64_t second) {
        return (remaining * first + sample * second) / kSweepSegments;
    };
    return {interpolate(start.x, finish.x), interpolate(start.y, finish.y),
            interpolate(start.z, finish.z) + 4 * static_cast<std::int64_t>(arc_lift) * sample * remaining};
}

[[nodiscard]] bool clear_jump_transition(const NavigationNode &from, const NavigationNode &to,
                                         const NavigationAgentProfile &profile,
                                         const std::vector<SourceVolume> &free_volumes,
                                         const std::vector<Blocker> &blockers) {
    const TransitionEndpoints endpoints = closest_endpoints(from.area, to.area);
    const FixedPoint start = fixed_agent_origin(endpoints.from_x, endpoints.from_y, from.area.floor_z, profile);
    const FixedPoint finish = fixed_agent_origin(endpoints.to_x, endpoints.to_y, to.area.floor_z, profile);
    // Upward jumps first prove an in-place rise to ledge height, matching the
    // conservative jump-then-forward route used to mount a vertical lip. The
    // fixed parabola then adds half the accepted rise at t=1/2. Each chord is
    // expanded by the exact concavity error bound
    // (arc_lift / kSweepSegments^2), so thin blockers cannot hide between the
    // sixteen deterministic samples.
    const GridCoord arc_lift =
        static_cast<GridCoord>(std::max<std::int64_t>(1, (static_cast<std::int64_t>(profile.max_jump_up) + 1) / 2));
    FixedPoint launch = start;
    if (finish.z > start.z) {
        launch.z = finish.z;
        if (!clear_sweep_segment(start, launch, profile, free_volumes, blockers))
            return false;
    }
    FixedPoint previous = launch;
    for (std::int64_t sample = 1; sample <= kSweepSegments; ++sample) {
        const FixedPoint current = jump_point(launch, finish, sample, arc_lift);
        if (!clear_sweep_segment(previous, current, profile, free_volumes, blockers, arc_lift))
            return false;
        previous = current;
    }
    return true;
}

[[nodiscard]] std::optional<NavigationTransitionKind>
axial_ramp_transition(const NavigationNode &from, const NavigationNode &to, const NavigationAgentProfile &profile,
                      const std::vector<SourceVolume> &free_volumes, const std::vector<Blocker> &blockers) {
    if (from.area.slope.has_value() == to.area.slope.has_value())
        return std::nullopt;
    const NavigationNode &ramp = from.area.slope ? from : to;
    const NavigationNode &flat = from.area.slope ? to : from;
    const bool x = ramp.area.slope->along_x;
    const GridCoord hull_min = x ? profile.hull_mins.x : profile.hull_mins.y;
    const GridCoord hull_max = x ? profile.hull_maxs.x : profile.hull_maxs.y;
    const GridCoord ramp_min = x ? ramp.area.min_x : ramp.area.min_y;
    const GridCoord ramp_max = x ? ramp.area.max_x : ramp.area.max_y;
    const GridCoord flat_min = x ? flat.area.min_x : flat.area.min_y;
    const GridCoord flat_max = x ? flat.area.max_x : flat.area.max_y;
    // A flat landing must meet one physical longitudinal end. Mid-slope side
    // entry, gaps, another ramp, and jumping onto a slope are not inferred.
    if (static_cast<std::int64_t>(flat_max) + hull_max != static_cast<std::int64_t>(ramp_min) + hull_min &&
        static_cast<std::int64_t>(flat_min) + hull_min != static_cast<std::int64_t>(ramp_max) + hull_max)
        return std::nullopt;
    if (!(x ? positive_overlap(ramp.area.min_y, ramp.area.max_y, flat.area.min_y, flat.area.max_y)
            : positive_overlap(ramp.area.min_x, ramp.area.max_x, flat.area.min_x, flat.area.max_x)))
        return std::nullopt;
    const TransitionEndpoints endpoints = closest_endpoints(from.area, to.area);
    const GridCoord first_floor = navigation_floor_z(from.area, endpoints.from_x, endpoints.from_y);
    const GridCoord second_floor = navigation_floor_z(to.area, endpoints.to_x, endpoints.to_y);
    if (magnitude(static_cast<std::int64_t>(second_floor) - first_floor) >
        static_cast<std::uint64_t>(profile.max_step_up))
        return std::nullopt;
    const FixedPoint start = fixed_agent_origin(endpoints.from_x, endpoints.from_y, first_floor, profile);
    const FixedPoint finish = fixed_agent_origin(endpoints.to_x, endpoints.to_y, second_floor, profile);
    const auto clear = [&](const FixedPoint &first, const FixedPoint &second) {
        const FixedBounds query = swept_hull_bounds(first, second, profile);
        return !overlaps_blocker_other_than(query, blockers, SolidId{ramp.source.id}) &&
               covered_by_free_union(query, free_volumes);
    };
    // The end lip is crossed at the higher feet height, then lowered over the
    // destination. The affine ramp under the overlap never exceeds this height.
    const FixedPoint corner =
        finish.z > start.z ? FixedPoint{start.x, start.y, finish.z} : FixedPoint{finish.x, finish.y, start.z};
    if (!clear(start, corner) || !clear(corner, finish))
        return std::nullopt;
    return first_floor == second_floor ? NavigationTransitionKind::walk : NavigationTransitionKind::step;
}

[[nodiscard]] std::optional<NavigationTransitionKind>
natural_transition(const NavigationNode &from, const NavigationNode &to, const NavigationAgentProfile &profile,
                   const std::vector<SourceVolume> &free_volumes, const std::vector<Blocker> &blockers) {
    if (from.area.slope || to.area.slope)
        return axial_ramp_transition(from, to, profile, free_volumes, blockers);
    std::optional<NavigationTransitionKind> candidate;
    if (same_floor_contact(from.area, to.area))
        candidate = NavigationTransitionKind::walk;
    if (!laterally_aligned(from.area, to.area))
        return std::nullopt;
    const std::uint64_t gap = horizontal_gap(from.area, to.area);
    const std::int64_t delta = static_cast<std::int64_t>(to.area.floor_z) - from.area.floor_z;
    const std::uint64_t hull_width =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(profile.hull_maxs.x) - profile.hull_mins.x);
    const std::uint64_t hull_depth =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(profile.hull_maxs.y) - profile.hull_mins.y);
    const std::uint64_t step_gap = std::max(hull_width, hull_depth);
    if (!candidate && delta == 0) {
        if (from.source != to.source && physical_contact(from.area, to.area, profile))
            candidate = NavigationTransitionKind::walk;
        else if (profile.max_jump_up > 0 && gap > 0 && gap <= static_cast<std::uint64_t>(profile.max_jump_gap))
            candidate = NavigationTransitionKind::jump;
    } else if (!candidate && delta > 0) {
        if (delta <= profile.max_step_up && gap <= step_gap)
            candidate = NavigationTransitionKind::step;
        else if (profile.max_jump_up > 0 && delta <= profile.max_jump_up &&
                 gap <= static_cast<std::uint64_t>(profile.max_jump_gap))
            candidate = NavigationTransitionKind::jump;
    } else if (!candidate) {
        const std::int64_t drop = -delta;
        if (drop <= profile.max_step_up && gap <= step_gap)
            candidate = NavigationTransitionKind::step;
        else if (profile.max_drop > 0 && drop <= profile.max_drop &&
                 gap <= static_cast<std::uint64_t>(profile.max_jump_gap))
            candidate = NavigationTransitionKind::drop;
    }
    if (!candidate)
        return std::nullopt;
    const bool clear = *candidate == NavigationTransitionKind::jump
                           ? clear_jump_transition(from, to, profile, free_volumes, blockers)
                           : clear_linear_transition(from, to, *candidate, profile, free_volumes, blockers);
    return clear ? candidate : std::nullopt;
}

[[nodiscard]] NavigationCost transition_penalty(const NavigationTransitionKind kind, const NavigationOptions &options) {
    switch (kind) {
    case NavigationTransitionKind::walk:
        return 0;
    case NavigationTransitionKind::step:
        return options.step_penalty;
    case NavigationTransitionKind::jump:
        return options.jump_penalty;
    case NavigationTransitionKind::drop:
        return options.drop_penalty;
    case NavigationTransitionKind::door:
        return options.door_penalty;
    case NavigationTransitionKind::lift:
        return options.lift_penalty;
    case NavigationTransitionKind::teleporter:
        return options.teleporter_penalty;
    }
    fail("invalid_transition", "navigation transition kind is invalid");
}

[[nodiscard]] NavigationSourceRef natural_edge_source(const NavigationNode &from, const NavigationNode &to) {
    if (from.area.slope || to.area.slope)
        return from.area.slope ? from.source : to.source;
    if (from.area.floor_z != to.area.floor_z)
        return from.area.floor_z > to.area.floor_z ? from.source : to.source;
    if (from.source.kind == NavigationSourceKind::connection)
        return from.source;
    if (to.source.kind == NavigationSourceKind::connection)
        return to.source;
    return std::min(from.source, to.source);
}

} // namespace

namespace {

[[nodiscard]] GridVec3 area_origin(const NavigationArea &area, const NavigationAgentProfile &profile,
                                   const NavigationOptions &options) {
    const auto midpoint = [&](const GridCoord minimum, const GridCoord maximum) {
        return checked_coordinate((static_cast<std::int64_t>(minimum) + maximum) / 2, options);
    };
    const GridCoord x = midpoint(area.min_x, area.max_x);
    const GridCoord y = midpoint(area.min_y, area.max_y);
    return {
        x, y,
        checked_coordinate(static_cast<std::int64_t>(navigation_floor_z(area, x, y)) - profile.hull_mins.z, options)};
}

[[nodiscard]] GridVec3 position_on_node(const NavigationNode &node, const GridCoord x, const GridCoord y) {
    const std::int64_t z = static_cast<std::int64_t>(node.origin.z) + navigation_floor_z(node.area, x, y) -
                           navigation_floor_z(node.area, node.origin.x, node.origin.y);
    if (z < std::numeric_limits<GridCoord>::min() || z > std::numeric_limits<GridCoord>::max())
        fail("coordinate_overflow", "navigation surface origin exceeded the signed grid range");
    return {x, y, static_cast<GridCoord>(z)};
}

[[nodiscard]] NavigationCost position_distance(const GridVec3 first, const GridVec3 second) {
    return magnitude(static_cast<std::int64_t>(first.x) - second.x) +
           magnitude(static_cast<std::int64_t>(first.y) - second.y) +
           magnitude(static_cast<std::int64_t>(first.z) - second.z);
}

[[nodiscard]] std::vector<NavigationNode> materialize_nodes(std::vector<PendingNode> pending,
                                                            const NavigationAgentProfile &profile,
                                                            const NavigationOptions &options) {
    std::sort(pending.begin(), pending.end(), [](const PendingNode &left, const PendingNode &right) {
        return std::tie(left.source, left.area) < std::tie(right.source, right.area);
    });
    pending.erase(std::unique(pending.begin(), pending.end(),
                              [](const PendingNode &left, const PendingNode &right) {
                                  return left.source == right.source && left.area == right.area;
                              }),
                  pending.end());
    if (pending.size() > options.max_nodes || pending.size() > std::numeric_limits<std::uint32_t>::max())
        fail("node_limit", "navigation node count exceeds its configured or identifier cap");
    std::vector<NavigationNode> result;
    result.reserve(pending.size());
    std::optional<NavigationSourceRef> previous_source;
    std::uint32_t fragment = 0;
    for (std::size_t index = 0; index < pending.size(); ++index) {
        if (!previous_source || *previous_source != pending[index].source)
            fragment = 0;
        else if (fragment == std::numeric_limits<std::uint32_t>::max())
            fail("node_limit", "navigation source fragment identifier overflowed");
        else
            ++fragment;
        previous_source = pending[index].source;
        result.push_back({NavigationNodeId{static_cast<std::uint32_t>(index + 1)}, pending[index].source, fragment,
                          pending[index].area, area_origin(pending[index].area, profile, options)});
    }
    return result;
}

void add_pending_edge(std::vector<PendingEdge> &edges, std::set<EdgeKey> &keys, const NavigationNode &from,
                      const NavigationNode &to, const NavigationTransitionKind kind, const NavigationSourceRef source,
                      const NavigationOptions &options) {
    if (from.id == to.id)
        return;
    const EdgeKey key{from.id, to.id, kind, source};
    if (!keys.insert(key).second)
        return;
    if (edges.size() >= options.max_edges)
        fail("edge_limit", "navigation edge cap was exceeded");
    // Teleport distance is instantaneous; its configurable penalty alone
    // represents activation/routing preference. Physical transitions include
    // deterministic Manhattan travel between their stable area anchors.
    NavigationCost cost = kind == NavigationTransitionKind::teleporter
                              ? transition_penalty(kind, options)
                              : anchor_distance_cost(from, to, options, transition_penalty(kind, options));
    if ((from.area.slope || to.area.slope) &&
        (kind == NavigationTransitionKind::walk || kind == NavigationTransitionKind::step)) {
        const TransitionEndpoints endpoints = closest_endpoints(from.area, to.area);
        const GridVec3 first = position_on_node(from, endpoints.from_x, endpoints.from_y);
        const GridVec3 second = position_on_node(to, endpoints.to_x, endpoints.to_y);
        const NavigationCost distance =
            checked_cost_add(position_distance(from.origin, first),
                             checked_cost_add(position_distance(first, second), position_distance(second, to.origin)));
        cost =
            checked_cost_add(checked_cost_multiply(distance, options.cost_per_unit), transition_penalty(kind, options));
    }
    edges.push_back({from.id, to.id, kind, cost, source});
}

[[nodiscard]] std::vector<const NavigationNode *> nodes_for_source(const std::vector<NavigationNode> &nodes,
                                                                   const NavigationSourceRef source) {
    const auto first = std::lower_bound(
        nodes.begin(), nodes.end(), source,
        [](const NavigationNode &node, const NavigationSourceRef wanted) { return node.source < wanted; });
    std::vector<const NavigationNode *> result;
    for (auto node = first; node != nodes.end() && node->source == source; ++node)
        result.push_back(&*node);
    return result;
}

[[nodiscard]] bool compatible_lift_fragments(const NavigationNode &first, const NavigationNode &second) noexcept {
    return positive_overlap(first.area.min_x, first.area.max_x, second.area.min_x, second.area.max_x) &&
           positive_overlap(first.area.min_y, first.area.max_y, second.area.min_y, second.area.max_y);
}

[[nodiscard]] const LayoutSolid *
matching_mover_solid(const Placement &placement, const BrushPurpose purpose,
                     const std::vector<std::pair<const LayoutSolid *, GridBounds>> &valid_solids) {
    if (!placement.volume)
        return nullptr;
    const auto found = std::find_if(valid_solids.begin(), valid_solids.end(), [&](const auto &candidate) {
        const LayoutSolid &solid = *candidate.first;
        return solid.purpose == purpose && solid.ownership == BrushOwnership::placement &&
               solid.owner == placement.id && candidate.second == *placement.volume;
    });
    return found == valid_solids.end() ? nullptr : found->first;
}

[[nodiscard]] GridVec3 placement_binding_point(const Placement &placement, const NavigationAgentProfile &profile,
                                               const NavigationOptions &options) {
    if (profile.game == Game::quake2 && placement.role == PlacementRole::teleporter_trigger)
        return placement.origin;
    if (!placement.volume)
        return placement.origin;
    const GridVec3 center = placement.volume->center();
    return {center.x, center.y,
            checked_coordinate(static_cast<std::int64_t>(placement.volume->min.z) - profile.hull_mins.z, options)};
}

[[nodiscard]] std::optional<NavigationNodeId> bind_placement(const Placement &placement,
                                                             const std::vector<NavigationNode> &nodes,
                                                             const NavigationAgentProfile &profile,
                                                             const NavigationOptions &options) {
    if ((placement.volume && !within_limit(*placement.volume, options.max_coordinate)) ||
        (!placement.volume && !within_limit(placement.origin, options.max_coordinate))) {
        return std::nullopt;
    }
    const GridVec3 point = placement_binding_point(placement, profile, options);
    const NavigationNode *best = nullptr;
    std::tuple<std::uint64_t, std::uint64_t, std::uint32_t> best_score{};
    for (const NavigationNode &node : nodes) {
        if (!node.area.contains_xy(point))
            continue;
        // Stock Q2 discs require a complete flat pad support footprint.
        if (node.area.slope && profile.game == Game::quake2 &&
            (placement.role == PlacementRole::teleporter_trigger ||
             placement.role == PlacementRole::teleporter_destination))
            continue;
        const std::uint64_t vertical =
            magnitude(static_cast<std::int64_t>(position_on_node(node, point.x, point.y).z) - point.z);
        if (vertical > static_cast<std::uint64_t>(options.placement_snap_height))
            continue;
        const std::uint64_t horizontal = magnitude(static_cast<std::int64_t>(node.origin.x) - point.x) +
                                         magnitude(static_cast<std::int64_t>(node.origin.y) - point.y);
        const auto score = std::tuple{vertical, horizontal, node.id.value};
        if (best == nullptr || score < best_score) {
            best = &node;
            best_score = score;
        }
    }
    return best == nullptr ? std::nullopt : std::optional{best->id};
}

[[nodiscard]] const NavigationPlacementBinding *find_binding(const std::vector<NavigationPlacementBinding> &bindings,
                                                             const PlacementId id) {
    const auto found = std::lower_bound(
        bindings.begin(), bindings.end(), id,
        [](const NavigationPlacementBinding &binding, const PlacementId wanted) { return binding.placement < wanted; });
    return found != bindings.end() && found->placement == id ? &*found : nullptr;
}

struct DoorSides {
    const NavigationNode *negative{};
    const NavigationNode *positive{};
};

[[nodiscard]] DoorSides door_sides(const Placement &door, const std::vector<NavigationNode> &nodes,
                                   const NavigationAgentProfile &profile, const NavigationOptions &options) {
    if (!door.volume)
        return {};
    const GridBounds &bounds = *door.volume;
    const std::int64_t extent_x = static_cast<std::int64_t>(bounds.max.x) - bounds.min.x;
    const std::int64_t extent_y = static_cast<std::int64_t>(bounds.max.y) - bounds.min.y;
    const bool along_x = extent_x <= extent_y;
    const GridCoord negative_boundary =
        checked_coordinate((along_x ? static_cast<std::int64_t>(bounds.min.x) - profile.hull_maxs.x
                                    : static_cast<std::int64_t>(bounds.min.y) - profile.hull_maxs.y),
                           options);
    const GridCoord positive_boundary =
        checked_coordinate((along_x ? static_cast<std::int64_t>(bounds.max.x) - profile.hull_mins.x
                                    : static_cast<std::int64_t>(bounds.max.y) - profile.hull_mins.y),
                           options);
    const GridCoord lateral_min =
        checked_coordinate((along_x ? static_cast<std::int64_t>(bounds.min.y) - profile.hull_maxs.y
                                    : static_cast<std::int64_t>(bounds.min.x) - profile.hull_maxs.x),
                           options);
    const GridCoord lateral_max =
        checked_coordinate((along_x ? static_cast<std::int64_t>(bounds.max.y) - profile.hull_mins.y
                                    : static_cast<std::int64_t>(bounds.max.x) - profile.hull_mins.x),
                           options);
    DoorSides result;
    std::tuple<std::uint64_t, std::uint64_t, std::uint32_t> negative_score{};
    std::tuple<std::uint64_t, std::uint64_t, std::uint32_t> positive_score{};
    for (const NavigationNode &node : nodes) {
        if (node.area.slope)
            continue; // Door-side proofs currently require flat standing floors.
        const GridCoord node_lateral_min = along_x ? node.area.min_y : node.area.min_x;
        const GridCoord node_lateral_max = along_x ? node.area.max_y : node.area.max_x;
        if (!positive_overlap(node_lateral_min, node_lateral_max, lateral_min, lateral_max))
            continue;
        const std::uint64_t vertical = magnitude(static_cast<std::int64_t>(node.area.floor_z) - bounds.min.z);
        if (vertical > static_cast<std::uint64_t>(profile.max_step_up))
            continue;
        const GridCoord node_min = along_x ? node.area.min_x : node.area.min_y;
        const GridCoord node_max = along_x ? node.area.max_x : node.area.max_y;
        if (node_max <= negative_boundary) {
            const std::uint64_t distance = magnitude(static_cast<std::int64_t>(negative_boundary) - node_max);
            if (distance > static_cast<std::uint64_t>(options.max_mover_side_distance))
                continue;
            const auto score = std::tuple{distance, vertical, node.id.value};
            if (result.negative == nullptr || score < negative_score) {
                result.negative = &node;
                negative_score = score;
            }
        }
        if (node_min >= positive_boundary) {
            const std::uint64_t distance = magnitude(static_cast<std::int64_t>(node_min) - positive_boundary);
            if (distance > static_cast<std::uint64_t>(options.max_mover_side_distance))
                continue;
            const auto score = std::tuple{distance, vertical, node.id.value};
            if (result.positive == nullptr || score < positive_score) {
                result.positive = &node;
                positive_score = score;
            }
        }
    }
    return result;
}

[[nodiscard]] FixedPoint overlapping_fragment_origin(const NavigationNode &first, const NavigationNode &second,
                                                     const GridCoord floor,
                                                     const NavigationAgentProfile &profile) noexcept {
    const GridCoord min_x = std::max(first.area.min_x, second.area.min_x);
    const GridCoord max_x = std::min(first.area.max_x, second.area.max_x);
    const GridCoord min_y = std::max(first.area.min_y, second.area.min_y);
    const GridCoord max_y = std::min(first.area.max_y, second.area.max_y);
    const GridCoord x = static_cast<GridCoord>((static_cast<std::int64_t>(min_x) + max_x) / 2);
    const GridCoord y = static_cast<GridCoord>((static_cast<std::int64_t>(min_y) + max_y) / 2);
    return fixed_agent_origin(x, y, floor, profile);
}

[[nodiscard]] bool clear_lift_agent_sweep(const NavigationNode &first, const NavigationNode &second,
                                          const SolidId mover, const NavigationAgentProfile &profile,
                                          const std::vector<SourceVolume> &free_volumes,
                                          const std::vector<Blocker> &blockers) {
    const FixedPoint start = overlapping_fragment_origin(first, second, first.area.floor_z, profile);
    const FixedPoint finish = overlapping_fragment_origin(first, second, second.area.floor_z, profile);
    const FixedBounds query = swept_hull_bounds(start, finish, profile);
    return !overlaps_blocker_other_than(query, blockers, mover) && covered_by_free_union(query, free_volumes);
}

[[nodiscard]] bool clear_open_door_agent_sweep(const NavigationNode &negative, const NavigationNode &positive,
                                               const GridBounds &open_mover_bounds, const SolidId mover,
                                               const NavigationAgentProfile &profile,
                                               const std::vector<SourceVolume> &free_volumes,
                                               const std::vector<Blocker> &blockers) {
    const TransitionEndpoints endpoints = closest_endpoints(negative.area, positive.area);
    const FixedPoint start = fixed_agent_origin(endpoints.from_x, endpoints.from_y, negative.area.floor_z, profile);
    const FixedPoint finish = fixed_agent_origin(endpoints.to_x, endpoints.to_y, positive.area.floor_z, profile);
    const FixedBounds query = swept_hull_bounds(start, finish, profile);
    return !intersect_bounds(query, fixed_bounds(open_mover_bounds)).has_value() &&
           !overlaps_blocker_other_than(query, blockers, mover) && covered_by_free_union(query, free_volumes);
}

[[nodiscard]] std::vector<NavigationNodeId> reachable_many(const NavigationGraph &graph,
                                                           const std::vector<NavigationNodeId> &starts,
                                                           const std::size_t max_expansions, const bool reverse) {
    if (max_expansions == 0)
        fail("invalid_navigation_options", "reachability expansion cap must be positive");
    const std::size_t node_count = graph.nodes().size();
    std::vector<std::vector<NavigationNodeId>> adjacency(node_count);
    for (const NavigationEdge &edge : graph.edges()) {
        if (!edge.from || !edge.to || edge.from.value > node_count || edge.to.value > node_count)
            fail("invalid_graph", "navigation graph contains an unresolved edge endpoint");
        const NavigationNodeId from = reverse ? edge.to : edge.from;
        const NavigationNodeId to = reverse ? edge.from : edge.to;
        adjacency[from.value - 1].push_back(to);
    }
    for (auto &neighbors : adjacency) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    std::vector<bool> visited(node_count, false);
    std::queue<NavigationNodeId> queue;
    for (const NavigationNodeId start : starts) {
        if (!start || start.value > node_count)
            fail("invalid_node_id", "reachability start node does not exist");
        if (!visited[start.value - 1]) {
            visited[start.value - 1] = true;
            queue.push(start);
        }
    }
    std::size_t expansions = 0;
    while (!queue.empty()) {
        if (expansions >= max_expansions)
            fail("path_expansion_limit", "navigation reachability exceeded its expansion cap");
        const NavigationNodeId current = queue.front();
        queue.pop();
        ++expansions;
        for (const NavigationNodeId next : adjacency[current.value - 1]) {
            if (!visited[next.value - 1]) {
                visited[next.value - 1] = true;
                queue.push(next);
            }
        }
    }
    std::vector<NavigationNodeId> result;
    for (std::size_t index = 0; index < visited.size(); ++index) {
        if (visited[index])
            result.push_back(NavigationNodeId{static_cast<std::uint32_t>(index + 1)});
    }
    return result;
}

[[nodiscard]] bool contains_node(const std::vector<NavigationNodeId> &nodes, const NavigationNodeId id) {
    return std::binary_search(nodes.begin(), nodes.end(), id);
}

void sort_findings(std::vector<NavigationFinding> &findings) {
    std::sort(findings.begin(), findings.end(), [](const NavigationFinding &left, const NavigationFinding &right) {
        return std::tie(left.severity, left.kind, left.source, left.related_source, left.message) <
               std::tie(right.severity, right.kind, right.source, right.related_source, right.message);
    });
    findings.erase(std::unique(findings.begin(), findings.end()), findings.end());
}

} // namespace

NavigationAnalysis NavigationAnalyzer::analyze(const Layout &layout, const NavigationOptions &options) const {
    const NavigationAgentProfile profile =
        options.profile_override.value_or(standard_navigation_profile(layout.spec.game, options.agent_kind));
    validate_options_and_profile(options, profile, layout.spec.game);
    validate_layout_caps(layout, options);
    const std::vector<const Room *> rooms = sorted_rooms(layout);
    const std::vector<const Connection *> connections = sorted_connections(layout);
    const std::vector<const LayoutSolid *> solids = sorted_solids(layout);
    const std::vector<const Placement *> placements = sorted_placements(layout);

    NavigationAnalysis analysis;
    analysis.profile = profile;
    std::vector<SourceVolume> free_volumes;
    for (const Room *room : rooms) {
        const NavigationSourceRef source = source_ref(room->id);
        if (!within_limit(room->bounds, options.max_coordinate)) {
            add_finding(analysis.findings, NavigationFindingKind::invalid_source_geometry,
                        NavigationFindingSeverity::error, source, "room bounds are invalid or out of range");
            continue;
        }
        free_volumes.push_back({source, room->bounds, std::nullopt, std::nullopt});
    }
    for (const Connection *connection : connections) {
        const bool endpoints_resolve = find_sorted_room(rooms, connection->from_room) != nullptr &&
                                       find_sorted_room(rooms, connection->to_room) != nullptr;
        if (!endpoints_resolve) {
            add_finding(analysis.findings, NavigationFindingKind::invalid_source_geometry,
                        NavigationFindingSeverity::error, source_ref(connection->id, 0),
                        "connection references a missing room");
            continue;
        }
        std::vector<GridBounds> passages = connection->passage_volumes;
        std::sort(passages.begin(), passages.end(), bounds_less);
        passages.erase(std::unique(passages.begin(), passages.end()), passages.end());
        if (passages.empty()) {
            add_finding(analysis.findings, NavigationFindingKind::missing_passage_geometry,
                        NavigationFindingSeverity::error, source_ref(connection->id, 0),
                        "connection has no explicit passage volume");
            continue;
        }
        for (std::size_t index = 0; index < passages.size(); ++index) {
            if (index > std::numeric_limits<std::uint32_t>::max())
                fail("source_volume_limit", "connection passage provenance index overflowed");
            const NavigationSourceRef source = source_ref(connection->id, static_cast<std::uint32_t>(index));
            if (!within_limit(passages[index], options.max_coordinate)) {
                add_finding(analysis.findings, NavigationFindingKind::invalid_source_geometry,
                            NavigationFindingSeverity::error, source,
                            "connection passage bounds are invalid or out of range");
                continue;
            }
            free_volumes.push_back({source, passages[index], std::nullopt, std::nullopt});
        }
    }
    std::sort(free_volumes.begin(), free_volumes.end(), [](const SourceVolume &left, const SourceVolume &right) {
        return std::tie(left.source, left.bounds) < std::tie(right.source, right.bounds);
    });

    std::vector<Blocker> blockers;
    std::vector<AxialRamp> ramps;
    std::vector<std::pair<const LayoutSolid *, GridBounds>> valid_solids;
    for (const LayoutSolid *solid : solids) {
        const NavigationSourceRef source = source_ref(solid->id);
        const BrushGeometryValidation validation = validate_brush_geometry(solid->geometry);
        if (!validation.ok() || !within_limit(*validation.bounds, options.max_coordinate)) {
            add_finding(analysis.findings, NavigationFindingKind::invalid_source_geometry,
                        NavigationFindingSeverity::error, source, "solid geometry is invalid or out of range");
            continue;
        }
        const std::optional<AxialRamp> ramp =
            solid->purpose == BrushPurpose::ramp ? axial_ramp(*solid, profile) : std::nullopt;
        // Exact axial side planes avoid rounding an adjacent landing into the
        // ramp's conservative aggregate bounds by an extra grid unit.
        const GridBounds bounds = ramp ? ramp->bounds : *validation.bounds;
        valid_solids.emplace_back(solid, bounds);
        if (ramp)
            ramps.push_back(*ramp);
        if (blocks_navigation(*solid))
            blockers.push_back({solid->id, bounds, solid->owner});
    }
    std::sort(blockers.begin(), blockers.end(), [](const Blocker &left, const Blocker &right) {
        return std::tie(left.id, left.bounds, left.owner) < std::tie(right.id, right.bounds, right.owner);
    });

    std::vector<SourceVolume> sources = free_volumes;
    if (sources.size() > options.max_source_volumes)
        fail("source_volume_limit", "free-space volume count exceeds the navigation source-volume cap");
    for (const auto &[solid, bounds] : valid_solids) {
        if (!support_purpose(solid->purpose))
            continue;
        if (solid->purpose == BrushPurpose::ramp) {
            if (std::none_of(ramps.begin(), ramps.end(), [&](const AxialRamp &ramp) { return ramp.id == solid->id; }))
                add_finding(analysis.findings, NavigationFindingKind::invalid_source_geometry,
                            NavigationFindingSeverity::error, source_ref(solid->id),
                            "ramp requires a solid six-plane axial wedge with integral ends and slope at most 1:1");
            continue;
        }
        if (get_convex_polyhedron(solid->geometry) != nullptr) {
            add_finding(analysis.findings, NavigationFindingKind::invalid_source_geometry,
                        NavigationFindingSeverity::error, source_ref(solid->id),
                        "unsupported convex support cannot be represented by a flat aggregate-bounds top");
            continue;
        }
        std::vector<SourceVolume> support_tiles =
            tile_support_top(source_ref(solid->id), bounds, bounds.max.z, free_volumes, solid->id, std::nullopt,
                             options, analysis.stats);
        if (support_tiles.empty()) {
            add_finding(analysis.findings, NavigationFindingKind::insufficient_clearance,
                        NavigationFindingSeverity::error, source_ref(solid->id),
                        "walkable solid top has no enclosing free-space clearance");
            continue;
        }
        append_source_tiles(sources, support_tiles, options);
    }

    std::vector<LiftTransition> lifts;
    for (const Placement *placement : placements) {
        if (placement->role != PlacementRole::lift)
            continue;
        const NavigationSourceRef transition_source = source_ref(placement->id);
        const Placement *destination = placement->link ? find_sorted_placement(placements, *placement->link) : nullptr;
        const LayoutSolid *mover = matching_mover_solid(*placement, BrushPurpose::lift, valid_solids);
        if (!placement->volume || !within_limit(*placement->volume, options.max_coordinate) || destination == nullptr ||
            destination->role != PlacementRole::lift_destination ||
            !within_limit(destination->origin, options.max_coordinate) || mover == nullptr) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "lift requires valid bounds, an exact placement-owned solid, and a linked destination");
            continue;
        }
        const GridVec3 center = placement->volume->center();
        const std::array<std::int64_t, 3> delta{
            static_cast<std::int64_t>(destination->origin.x) - center.x,
            static_cast<std::int64_t>(destination->origin.y) - center.y,
            static_cast<std::int64_t>(destination->origin.z) - center.z,
        };
        if (delta[0] != 0 || delta[1] != 0 || delta[2] == 0) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "lift destination must encode nonzero vertical travel");
            continue;
        }
        const GridBounds destination_bounds = translated_bounds(*placement->volume, delta, options);
        if (!clear_mover_sweep(*placement->volume, destination_bounds, mover->id, free_volumes, blockers)) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "lift mover sweep crosses a solid blocker or leaves explicit free space");
            continue;
        }
        const NavigationSourceRef first_source = source_ref(placement->id, 0);
        const NavigationSourceRef second_source = source_ref(placement->id, 1);
        std::vector<SourceVolume> first_tiles =
            tile_support_top(first_source, *placement->volume, placement->volume->max.z, free_volumes, mover->id,
                             std::nullopt, options, analysis.stats);
        std::vector<SourceVolume> second_tiles =
            tile_support_top(second_source, destination_bounds, destination_bounds.max.z, free_volumes, mover->id,
                             std::nullopt, options, analysis.stats);
        if (first_tiles.empty() || second_tiles.empty()) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "lift endpoints do not lie inside explicit free space");
            continue;
        }
        append_source_tiles(sources, first_tiles, options);
        append_source_tiles(sources, second_tiles, options);
        lifts.push_back({placement->id, mover->id, first_source, second_source});
    }
    std::sort(sources.begin(), sources.end(), [](const SourceVolume &left, const SourceVolume &right) {
        return std::tie(left.source, left.bounds, left.ignored_solid, left.ignored_owner) <
               std::tie(right.source, right.bounds, right.ignored_solid, right.ignored_owner);
    });

    std::vector<PendingNode> pending_nodes;
    for (const SourceVolume &source : sources)
        partition_source(source, blockers, profile, options, pending_nodes, analysis.findings, analysis.stats);
    for (const AxialRamp &ramp : ramps)
        append_ramp_node(ramp, free_volumes, blockers, profile, options, pending_nodes, analysis.findings,
                         analysis.stats);
    analysis.graph.nodes_ = materialize_nodes(std::move(pending_nodes), profile, options);

    std::vector<PendingEdge> pending_edges;
    std::set<EdgeKey> edge_keys;
    for (std::size_t first = 0; first < analysis.graph.nodes_.size(); ++first) {
        for (std::size_t second = first + 1; second < analysis.graph.nodes_.size(); ++second) {
            if (analysis.stats.evaluated_node_pairs >= options.max_pair_checks)
                fail("pair_check_limit", "navigation node-pair evaluation cap was exceeded");
            ++analysis.stats.evaluated_node_pairs;
            const NavigationNode &left = analysis.graph.nodes_[first];
            const NavigationNode &right = analysis.graph.nodes_[second];
            if (const auto kind = natural_transition(left, right, profile, free_volumes, blockers))
                add_pending_edge(pending_edges, edge_keys, left, right, *kind, natural_edge_source(left, right),
                                 options);
            if (const auto kind = natural_transition(right, left, profile, free_volumes, blockers))
                add_pending_edge(pending_edges, edge_keys, right, left, *kind, natural_edge_source(right, left),
                                 options);
        }
    }

    for (const Placement *placement : placements) {
        if (placement->role != PlacementRole::door)
            continue;
        const NavigationSourceRef transition_source = source_ref(placement->id);
        const Placement *destination = placement->link ? find_sorted_placement(placements, *placement->link) : nullptr;
        const LayoutSolid *mover = matching_mover_solid(*placement, BrushPurpose::door, valid_solids);
        if (!placement->volume || !within_limit(*placement->volume, options.max_coordinate) || destination == nullptr ||
            destination->role != PlacementRole::door_destination ||
            !within_limit(destination->origin, options.max_coordinate) || mover == nullptr) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "door requires valid bounds, an exact placement-owned solid, and a linked destination");
            continue;
        }
        const GridVec3 center = placement->volume->center();
        const std::array<std::int64_t, 3> delta{
            static_cast<std::int64_t>(destination->origin.x) - center.x,
            static_cast<std::int64_t>(destination->origin.y) - center.y,
            static_cast<std::int64_t>(destination->origin.z) - center.z,
        };
        if (std::count_if(delta.begin(), delta.end(), [](const std::int64_t component) { return component != 0; }) !=
            1) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "door destination must encode nonzero axis-aligned travel");
            continue;
        }
        const GridBounds destination_bounds = translated_bounds(*placement->volume, delta, options);
        if (!clear_mover_sweep(*placement->volume, destination_bounds, mover->id, free_volumes, blockers)) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "door mover sweep crosses a solid blocker or leaves explicit free space");
            continue;
        }
        const DoorSides sides = door_sides(*placement, analysis.graph.nodes_, profile, options);
        if (sides.negative == nullptr || sides.positive == nullptr) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "door has no hull-cleared navigation areas on both sides");
            continue;
        }
        if (!clear_open_door_agent_sweep(*sides.negative, *sides.positive, destination_bounds, mover->id, profile,
                                         free_volumes, blockers)) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "door travel does not leave a blocker-free hull corridor between its navigation sides");
            continue;
        }
        add_pending_edge(pending_edges, edge_keys, *sides.negative, *sides.positive, NavigationTransitionKind::door,
                         transition_source, options);
        add_pending_edge(pending_edges, edge_keys, *sides.positive, *sides.negative, NavigationTransitionKind::door,
                         transition_source, options);
    }
    for (const LiftTransition &lift : lifts) {
        const std::vector<const NavigationNode *> first_nodes = nodes_for_source(analysis.graph.nodes_, lift.first);
        const std::vector<const NavigationNode *> second_nodes = nodes_for_source(analysis.graph.nodes_, lift.second);
        bool connected = false;
        for (const NavigationNode *first : first_nodes) {
            for (const NavigationNode *second : second_nodes) {
                if (!compatible_lift_fragments(*first, *second) ||
                    !clear_lift_agent_sweep(*first, *second, lift.mover, profile, free_volumes, blockers))
                    continue;
                add_pending_edge(pending_edges, edge_keys, *first, *second, NavigationTransitionKind::lift,
                                 source_ref(lift.placement), options);
                add_pending_edge(pending_edges, edge_keys, *second, *first, NavigationTransitionKind::lift,
                                 source_ref(lift.placement), options);
                connected = true;
            }
        }
        if (!connected) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, source_ref(lift.placement),
                        "lift endpoints lack a compatible blocker-free swept hull corridor");
        }
    }

    analysis.placement_bindings.reserve(placements.size());
    for (const Placement *placement : placements) {
        const std::optional<NavigationNodeId> node =
            bind_placement(*placement, analysis.graph.nodes_, profile, options);
        analysis.placement_bindings.push_back({placement->id, node});
        if (node)
            ++analysis.stats.placement_bindings;
        else if (binding_required(placement->role))
            add_finding(analysis.findings, NavigationFindingKind::unbound_placement, NavigationFindingSeverity::error,
                        source_ref(placement->id), "navigation-relevant placement cannot bind to a hull-cleared area");
    }

    for (const Placement *placement : placements) {
        if (placement->role != PlacementRole::teleporter_trigger)
            continue;
        const NavigationSourceRef transition_source = source_ref(placement->id);
        const Placement *destination = placement->link ? find_sorted_placement(placements, *placement->link) : nullptr;
        const NavigationPlacementBinding *from_binding = find_binding(analysis.placement_bindings, placement->id);
        const NavigationPlacementBinding *to_binding =
            destination == nullptr ? nullptr : find_binding(analysis.placement_bindings, destination->id);
        if (destination == nullptr || destination->role != PlacementRole::teleporter_destination ||
            from_binding == nullptr || to_binding == nullptr || !from_binding->node || !to_binding->node) {
            add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                        NavigationFindingSeverity::error, transition_source,
                        "teleporter trigger and destination do not resolve to navigation areas");
            continue;
        }
        const NavigationNode *from = analysis.graph.find_node(*from_binding->node);
        const NavigationNode *to = analysis.graph.find_node(*to_binding->node);
        if (from == nullptr || to == nullptr)
            fail("invalid_graph", "teleporter binding resolved to a missing navigation node");
        if (layout.spec.game == Game::quake2) {
            // The stock touch function accepts clients only. A ground monster
            // cannot independently use this edge.
            if (profile.kind != NavigationAgentKind::player)
                continue;
            bool native_clear = false;
            try {
                const GridBounds source_pad = quake2_teleporter_bounds(placement->origin, false);
                const GridBounds destination_pad = quake2_teleporter_bounds(destination->origin, false);
                const FixedPoint boarding =
                    fixed_agent_origin(placement->origin.x, placement->origin.y, source_pad.max.z, profile);
                const FixedBounds boarding_hull = swept_hull_bounds(boarding, boarding, profile);
                const FixedPoint arrival{
                    scaled(destination->origin.x), scaled(destination->origin.y),
                    (static_cast<std::int64_t>(destination->origin.z) + kQuake2TeleporterArrivalOffset) * kSweepScale};
                const FixedBounds arrival_hull = swept_hull_bounds(arrival, arrival, profile);
                native_clear =
                    placement->volume && *placement->volume == quake2_teleporter_bounds(placement->origin, true) &&
                    source_pad.min.z == from->area.floor_z && destination_pad.min.z == to->area.floor_z &&
                    !overlaps_blocker(fixed_bounds(source_pad), blockers) &&
                    !overlaps_blocker(fixed_bounds(destination_pad), blockers) &&
                    covered_by_free_union(fixed_bounds(source_pad), free_volumes) &&
                    covered_by_free_union(fixed_bounds(destination_pad), free_volumes) &&
                    !overlaps_blocker(boarding_hull, blockers) && covered_by_free_union(boarding_hull, free_volumes) &&
                    !overlaps_blocker(arrival_hull, blockers) && covered_by_free_union(arrival_hull, free_volumes);
            } catch (const std::overflow_error &) {
                native_clear = false;
            }
            if (!native_clear) {
                add_finding(analysis.findings, NavigationFindingKind::unresolved_transition,
                            NavigationFindingSeverity::error, transition_source,
                            "Quake 2 teleporter requires canonical touch bounds, supported clear discs, and a clear "
                            "native arrival hull");
                continue;
            }
        }
        add_pending_edge(pending_edges, edge_keys, *from, *to, NavigationTransitionKind::teleporter, transition_source,
                         options);
    }

    std::sort(pending_edges.begin(), pending_edges.end(), [](const PendingEdge &left, const PendingEdge &right) {
        return std::tie(left.from, left.to, left.kind, left.source, left.cost) <
               std::tie(right.from, right.to, right.kind, right.source, right.cost);
    });
    if (pending_edges.size() > std::numeric_limits<std::uint32_t>::max())
        fail("edge_limit", "navigation edge identifier space was exhausted");
    analysis.graph.edges_.reserve(pending_edges.size());
    for (std::size_t index = 0; index < pending_edges.size(); ++index) {
        const PendingEdge &edge = pending_edges[index];
        analysis.graph.edges_.push_back({NavigationEdgeId{static_cast<std::uint32_t>(index + 1)}, edge.from, edge.to,
                                         edge.kind, edge.cost, edge.source});
    }

    std::vector<const Placement *> spawns;
    std::vector<NavigationNodeId> spawn_nodes;
    std::vector<const Placement *> monsters;
    for (const Placement *placement : placements) {
        const NavigationPlacementBinding *binding = find_binding(analysis.placement_bindings, placement->id);
        if (spawn_role(placement->role)) {
            spawns.push_back(placement);
            if (binding && binding->node)
                spawn_nodes.push_back(*binding->node);
        }
        if (placement->role == PlacementRole::monster)
            monsters.push_back(placement);
    }
    std::sort(spawn_nodes.begin(), spawn_nodes.end());
    spawn_nodes.erase(std::unique(spawn_nodes.begin(), spawn_nodes.end()), spawn_nodes.end());
    if (spawns.empty()) {
        add_finding(analysis.findings, NavigationFindingKind::missing_spawn, NavigationFindingSeverity::error,
                    std::nullopt, "layout has no typed player spawn for navigation reachability");
    } else if (!spawn_nodes.empty()) {
        if (profile.kind == NavigationAgentKind::player) {
            const NavigationPlacementBinding *primary_binding =
                find_binding(analysis.placement_bindings, spawns.front()->id);
            if (primary_binding && primary_binding->node) {
                const std::vector<NavigationNodeId> primary_reachable =
                    reachable_many(analysis.graph, {*primary_binding->node}, options.max_path_expansions, false);
                for (const Placement *spawn : spawns) {
                    const NavigationPlacementBinding *binding = find_binding(analysis.placement_bindings, spawn->id);
                    if (binding && binding->node && !contains_node(primary_reachable, *binding->node)) {
                        add_finding(analysis.findings, NavigationFindingKind::unreachable_spawn,
                                    NavigationFindingSeverity::error, source_ref(spawn->id),
                                    "spawn is not reachable from the lowest-ID bound spawn",
                                    source_ref(spawns.front()->id));
                    }
                }
            }
            const std::vector<NavigationNodeId> reachable =
                reachable_many(analysis.graph, spawn_nodes, options.max_path_expansions, false);
            for (const Placement *placement : placements) {
                const NavigationPlacementBinding *binding = find_binding(analysis.placement_bindings, placement->id);
                if (binding == nullptr || !binding->node || contains_node(reachable, *binding->node))
                    continue;
                if (objective_role(placement->role)) {
                    add_finding(analysis.findings, NavigationFindingKind::unreachable_objective,
                                NavigationFindingSeverity::error, source_ref(placement->id),
                                "objective is unreachable from every bound spawn");
                } else if (placement->role == PlacementRole::monster) {
                    add_finding(analysis.findings, NavigationFindingKind::unreachable_monster,
                                NavigationFindingSeverity::error, source_ref(placement->id),
                                "monster encounter is unreachable from every bound spawn");
                }
            }
        } else {
            const std::vector<NavigationNodeId> can_reach_spawn =
                reachable_many(analysis.graph, spawn_nodes, options.max_path_expansions, true);
            for (const Placement *monster : monsters) {
                const NavigationPlacementBinding *binding = find_binding(analysis.placement_bindings, monster->id);
                if (binding && binding->node && !contains_node(can_reach_spawn, *binding->node)) {
                    add_finding(analysis.findings, NavigationFindingKind::unreachable_monster,
                                NavigationFindingSeverity::error, source_ref(monster->id),
                                "ground monster cannot reach any bound player spawn");
                }
            }
        }
    }

    analysis.stats.node_count = analysis.graph.nodes_.size();
    analysis.stats.edge_count = analysis.graph.edges_.size();
    analysis.layout_sha256 = canonical_layout_sha256(layout);
    sort_findings(analysis.findings);
    return analysis;
}

const NavigationNode *NavigationGraph::find_node(const NavigationNodeId id) const noexcept {
    if (!id || id.value > nodes_.size())
        return nullptr;
    const NavigationNode &node = nodes_[id.value - 1];
    return node.id == id ? &node : nullptr;
}

const NavigationEdge *NavigationGraph::find_edge(const NavigationEdgeId id) const noexcept {
    if (!id || id.value > edges_.size())
        return nullptr;
    const NavigationEdge &edge = edges_[id.value - 1];
    return edge.id == id ? &edge : nullptr;
}

NavigationPath shortest_navigation_path(const NavigationGraph &graph, const NavigationNodeId from,
                                        const NavigationNodeId to, const std::size_t max_expansions) {
    if (max_expansions == 0)
        fail("invalid_navigation_options", "shortest-path expansion cap must be positive");
    if (graph.find_node(from) == nullptr || graph.find_node(to) == nullptr)
        fail("invalid_node_id", "shortest-path endpoint does not exist");
    NavigationPath result;
    if (from == to) {
        result.cost = 0;
        result.nodes.push_back(from);
        return result;
    }
    std::vector<std::vector<const NavigationEdge *>> adjacency(graph.nodes().size());
    for (const NavigationEdge &edge : graph.edges()) {
        if (graph.find_node(edge.from) == nullptr || graph.find_node(edge.to) == nullptr)
            fail("invalid_graph", "navigation edge has an unresolved endpoint");
        adjacency[edge.from.value - 1].push_back(&edge);
    }
    for (auto &edges : adjacency) {
        std::sort(edges.begin(), edges.end(),
                  [](const NavigationEdge *left, const NavigationEdge *right) { return left->id < right->id; });
    }
    std::vector<NavigationCost> distance(graph.nodes().size(), kUnreachableNavigationCost);
    std::vector<std::optional<NavigationEdgeId>> predecessor(graph.nodes().size());
    using QueueItem = std::pair<NavigationCost, NavigationNodeId>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<>> queue;
    distance[from.value - 1] = 0;
    queue.push({0, from});
    std::size_t expansions = 0;
    while (!queue.empty()) {
        const auto [current_cost, current] = queue.top();
        queue.pop();
        if (current_cost != distance[current.value - 1])
            continue;
        if (expansions >= max_expansions)
            fail("path_expansion_limit", "shortest-path search exceeded its expansion cap");
        ++expansions;
        if (current == to)
            break;
        for (const NavigationEdge *edge : adjacency[current.value - 1]) {
            const NavigationCost candidate = checked_cost_add(current_cost, edge->cost);
            if (candidate < distance[edge->to.value - 1]) {
                distance[edge->to.value - 1] = candidate;
                predecessor[edge->to.value - 1] = edge->id;
                queue.push({candidate, edge->to});
            }
        }
    }
    if (distance[to.value - 1] == kUnreachableNavigationCost)
        return result;
    result.cost = distance[to.value - 1];
    NavigationNodeId current = to;
    result.nodes.push_back(current);
    while (current != from) {
        const std::optional<NavigationEdgeId> edge_id = predecessor[current.value - 1];
        if (!edge_id)
            fail("invalid_graph", "shortest-path predecessor chain is incomplete");
        const NavigationEdge *edge = graph.find_edge(*edge_id);
        if (edge == nullptr || edge->to != current)
            fail("invalid_graph", "shortest-path predecessor edge is invalid");
        result.edges.push_back(edge->id);
        current = edge->from;
        result.nodes.push_back(current);
        if (result.nodes.size() > graph.nodes().size() + 1)
            fail("invalid_graph", "shortest-path predecessor chain contains a cycle");
    }
    std::reverse(result.nodes.begin(), result.nodes.end());
    std::reverse(result.edges.begin(), result.edges.end());
    return result;
}

NavigationPath navigation_path_between_positions(const NavigationGraph &graph, const NavigationNodeId from,
                                                 const GridVec3 from_position, const NavigationNodeId to,
                                                 const GridVec3 to_position, const NavigationCost cost_per_unit,
                                                 const std::size_t max_expansions) {
    if (cost_per_unit == 0)
        fail("invalid_navigation_options", "position path cost per unit must be positive");
    const NavigationNode *from_node = graph.find_node(from);
    const NavigationNode *to_node = graph.find_node(to);
    if (from_node == nullptr || to_node == nullptr)
        fail("invalid_node_id", "position path endpoint node does not exist");
    if (!from_node->area.contains_xy(from_position) || !to_node->area.contains_xy(to_position))
        fail("invalid_endpoint_position", "position path endpoint is outside its bound navigation area");

    NavigationPath result = shortest_navigation_path(graph, from, to, max_expansions);
    if (!result.reachable())
        return result;
    const auto surface_cost = [cost_per_unit](const GridVec3 first, const GridVec3 second) {
        return checked_cost_multiply(position_distance(first, second), cost_per_unit);
    };
    const GridVec3 first = position_on_node(*from_node, from_position.x, from_position.y);
    const GridVec3 second = position_on_node(*to_node, to_position.x, to_position.y);
    if (from == to) {
        result.cost = surface_cost(first, second);
    } else {
        result.cost = checked_cost_add(result.cost, surface_cost(first, from_node->origin));
        result.cost = checked_cost_add(result.cost, surface_cost(to_node->origin, second));
    }
    return result;
}

std::vector<NavigationNodeId> reachable_navigation_nodes(const NavigationGraph &graph, const NavigationNodeId from,
                                                         const std::size_t max_expansions) {
    return reachable_many(graph, {from}, max_expansions, false);
}

bool NavigationAnalysis::ok() const noexcept {
    return std::none_of(findings.begin(), findings.end(), [](const NavigationFinding &finding) {
        return finding.severity == NavigationFindingSeverity::error;
    });
}

const NavigationPlacementBinding *NavigationAnalysis::find_binding(const PlacementId placement) const noexcept {
    const auto found = std::lower_bound(
        placement_bindings.begin(), placement_bindings.end(), placement,
        [](const NavigationPlacementBinding &binding, const PlacementId wanted) { return binding.placement < wanted; });
    return found != placement_bindings.end() && found->placement == placement ? &*found : nullptr;
}

} // namespace qmg
