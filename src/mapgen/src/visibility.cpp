#include "qmg/visibility.hpp"

#include "qmg/entities.hpp"
#include "qmg/layout_fingerprint.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace qmg {
namespace {

constexpr GridCoord kPortableCoordinateLimit = 1'000'000;
constexpr std::size_t kPortableRoomLimit = 4096;
constexpr std::size_t kPortableConnectionLimit = 16384;
constexpr std::size_t kPortableFreeVolumeLimit = 65536;
constexpr std::size_t kPortableSolidLimit = 65536;
constexpr std::size_t kPortablePlacementLimit = 8192;
constexpr std::size_t kPortableSegmentTestLimit = 16'000'000;
constexpr std::size_t kPortableCrossfireLimit = 4'000'000;
constexpr std::size_t kPortableCoverLimit = 262144;
constexpr std::size_t kPortableFindingLimit = 262144;

[[noreturn]] void fail(std::string code, std::string message) {
    throw VisibilityError(std::move(code), std::move(message));
}

[[nodiscard]] std::uint64_t magnitude(const std::int64_t value) {
    if (value >= 0)
        return static_cast<std::uint64_t>(value);
    return static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

[[nodiscard]] std::int64_t checked_multiply(const std::int64_t left, const std::int64_t right) {
    const std::uint64_t left_magnitude = magnitude(left);
    const std::uint64_t right_magnitude = magnitude(right);
    if (left_magnitude != 0U &&
        right_magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / left_magnitude) {
        fail("arithmetic_overflow", "visibility rational multiplication overflowed");
    }
    return left * right;
}

[[nodiscard]] std::int64_t checked_add64(const std::int64_t left, const std::int64_t right) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        fail("arithmetic_overflow", "visibility integer addition overflowed");
    }
    return left + right;
}

struct Rational {
    std::int64_t numerator{};
    std::int64_t denominator{1};
};

[[nodiscard]] Rational rational(std::int64_t numerator, std::int64_t denominator) {
    if (denominator == 0)
        fail("arithmetic_overflow", "visibility rational has a zero denominator");
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    const std::int64_t divisor = std::gcd(numerator, denominator);
    return {numerator / divisor, denominator / divisor};
}

[[nodiscard]] int compare(const Rational left, const Rational right) {
    const std::int64_t left_product = checked_multiply(left.numerator, right.denominator);
    const std::int64_t right_product = checked_multiply(right.numerator, left.denominator);
    if (left_product < right_product)
        return -1;
    if (left_product > right_product)
        return 1;
    return 0;
}

[[nodiscard]] bool less(const Rational left, const Rational right) { return compare(left, right) < 0; }
[[nodiscard]] bool less_equal(const Rational left, const Rational right) { return compare(left, right) <= 0; }

struct RationalInterval {
    Rational minimum;
    Rational maximum;
};

[[nodiscard]] std::array<GridCoord, 3> components(const GridVec3 point) { return {point.x, point.y, point.z}; }

[[nodiscard]] std::optional<RationalInterval> segment_box_interval(const GridVec3 from, const GridVec3 to,
                                                                   const GridBounds &bounds) {
    Rational minimum{0, 1};
    Rational maximum{1, 1};
    const std::array<GridCoord, 3> start = components(from);
    const std::array<GridCoord, 3> finish = components(to);
    const std::array<GridCoord, 3> lower = components(bounds.min);
    const std::array<GridCoord, 3> upper = components(bounds.max);
    for (std::size_t axis = 0; axis < start.size(); ++axis) {
        const std::int64_t delta = static_cast<std::int64_t>(finish[axis]) - start[axis];
        if (delta == 0) {
            if (start[axis] < lower[axis] || start[axis] > upper[axis])
                return std::nullopt;
            continue;
        }
        Rational first = rational(static_cast<std::int64_t>(lower[axis]) - start[axis], delta);
        Rational second = rational(static_cast<std::int64_t>(upper[axis]) - start[axis], delta);
        if (less(second, first))
            std::swap(first, second);
        if (less(minimum, first))
            minimum = first;
        if (less(second, maximum))
            maximum = second;
        if (less(maximum, minimum))
            return std::nullopt;
    }
    if (less(maximum, Rational{0, 1}) || less(Rational{1, 1}, minimum))
        return std::nullopt;
    if (less(minimum, Rational{0, 1}))
        minimum = {0, 1};
    if (less(Rational{1, 1}, maximum))
        maximum = {1, 1};
    return RationalInterval{minimum, maximum};
}

[[nodiscard]] bool within_limit(const GridCoord value, const GridCoord limit) {
    const std::int64_t wide_value = value;
    const std::int64_t wide_limit = limit;
    return wide_value >= -wide_limit && wide_value <= wide_limit;
}

void require_point_limit(const GridVec3 point, const VisibilityLimits &limits) {
    if (!within_limit(point.x, limits.max_abs_coordinate) || !within_limit(point.y, limits.max_abs_coordinate) ||
        !within_limit(point.z, limits.max_abs_coordinate)) {
        fail("coordinate_limit", "visibility input exceeds the configured coordinate limit");
    }
}

void require_bounds(const GridBounds &bounds, const VisibilityLimits &limits, const char *code) {
    if (!bounds.valid())
        fail(code, "visibility input contains inverted or empty bounds");
    require_point_limit(bounds.min, limits);
    require_point_limit(bounds.max, limits);
}

void validate_options(const VisibilityOptions &options) {
    const VisibilityLimits &limits = options.limits;
    if (limits.max_abs_coordinate <= 0 || limits.max_abs_coordinate > kPortableCoordinateLimit ||
        limits.max_rooms == 0 || limits.max_rooms > kPortableRoomLimit || limits.max_connections == 0 ||
        limits.max_connections > kPortableConnectionLimit || limits.max_free_volumes == 0 ||
        limits.max_free_volumes > kPortableFreeVolumeLimit || limits.max_solids == 0 ||
        limits.max_solids > kPortableSolidLimit || limits.max_placements == 0 ||
        limits.max_placements > kPortablePlacementLimit || limits.max_segment_tests == 0 ||
        limits.max_segment_tests > kPortableSegmentTestLimit || limits.max_crossfire_records == 0 ||
        limits.max_crossfire_records > kPortableCrossfireLimit || limits.max_cover_opportunities == 0 ||
        limits.max_cover_opportunities > kPortableCoverLimit || limits.max_findings == 0 ||
        limits.max_findings > kPortableFindingLimit) {
        fail("invalid_limits", "visibility limits must be nonzero and within portable hard caps");
    }
    const VisibilityThresholds &thresholds = options.thresholds;
    if (thresholds.chokepoint_width <= 0 || thresholds.cover_clearance <= 0 || thresholds.cover_eye_height <= 0 ||
        thresholds.minimum_spawn_separation < 0 || thresholds.chokepoint_width > limits.max_abs_coordinate * 2LL ||
        thresholds.cover_clearance > limits.max_abs_coordinate ||
        thresholds.cover_eye_height > limits.max_abs_coordinate ||
        thresholds.minimum_spawn_separation > limits.max_abs_coordinate * 2LL ||
        thresholds.minimum_crossfire_angle_millidegrees > 180000U) {
        fail("invalid_thresholds", "visibility thresholds are outside their portable ranges");
    }
}

[[nodiscard]] bool valid_game(const Game game) {
    switch (game) {
    case Game::quake1:
    case Game::quake2:
    case Game::quake3:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_mode(const GameMode mode) {
    switch (mode) {
    case GameMode::single_player:
    case GameMode::deathmatch:
    case GameMode::duel:
    case GameMode::capture_the_flag:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_role(const PlacementRole role) {
    switch (role) {
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

[[nodiscard]] bool valid_team(const Team team) { return team == Team::red || team == Team::blue; }

[[nodiscard]] bool needs_team(const PlacementRole role) {
    return role == PlacementRole::team_initial_spawn || role == PlacementRole::team_respawn ||
           role == PlacementRole::flag;
}

void validate_role_semantics(const Layout &layout, const Placement &placement) {
    if (!valid_role(placement.role))
        fail("invalid_placement_role", "visibility input has an invalid typed placement role");
    if (placement.team && !valid_team(*placement.team))
        fail("invalid_team_semantics", "visibility input has an invalid team value");
    if (needs_team(placement.role) != placement.team.has_value())
        fail("invalid_team_semantics", "team data does not match the typed placement role");

    const bool native_single_player = layout.spec.mode == GameMode::single_player && layout.spec.game != Game::quake3;
    // Change r1s-mp-player-start: Quake 1 and Quake 2 maps of every mode may carry an info_player_start (a
    // multiplayer map has exactly one, for `map` loads with deathmatch 0; src/entities.cpp). Quake 3 has none: its
    // SP_info_player_start turns the entity into an info_player_deathmatch (code/game/g_client.c:53-56).
    if (placement.role == PlacementRole::player_start && layout.spec.game == Game::quake3)
        fail("invalid_mode_semantics", "player-start placement is valid only for Quake 1 and Quake 2");
    // Change r1s-sp-coop-starts: info_player_coop exists in Quake 1 and Quake 2 only (Quake 3's spawn table,
    // code/game/g_spawn.c:199-201, has none).
    if (placement.role == PlacementRole::coop_start && layout.spec.game == Game::quake3)
        fail("invalid_mode_semantics", "co-op start placement is valid only for Quake 1 and Quake 2");
    if (placement.role == PlacementRole::monster && !native_single_player)
        fail("invalid_mode_semantics", "monster placement is only valid for native Q1/Q2 single-player");
    if (placement.role == PlacementRole::deathmatch_spawn && native_single_player)
        fail("invalid_mode_semantics", "deathmatch spawn is invalid for native Q1/Q2 single-player");
    if (placement.role == PlacementRole::flag && layout.spec.mode != GameMode::capture_the_flag)
        fail("invalid_mode_semantics", "team objective placement requires capture-the-flag mode");
    if (placement.role == PlacementRole::team_initial_spawn &&
        (layout.spec.mode != GameMode::capture_the_flag || layout.spec.game == Game::quake1)) {
        fail("invalid_mode_semantics", "typed team-initial spawn is supported only by Q2/Q3 CTF");
    }
    if (placement.role == PlacementRole::team_respawn &&
        (layout.spec.mode != GameMode::capture_the_flag || layout.spec.game != Game::quake3)) {
        fail("invalid_mode_semantics", "typed team-respawn placement is supported only by Q3 CTF");
    }
}

struct Occluder {
    const LayoutSolid *solid{};
    GridBounds bounds;
    bool conservative{};
};

struct AnalysisContext {
    const Layout *layout{};
    const VisibilityOptions *options{};
    std::vector<const Room *> rooms;
    std::vector<const Connection *> connections;
    std::vector<const Placement *> placements;
    std::vector<GridBounds> free_volumes;
    std::vector<Occluder> occluders;
    std::map<PlacementId, GridVec3> placement_samples;
};

// Change r1s-sp-coop-starts: a co-op player stands at an info_player_coop exactly as at the start, so its sample is
// taken at the same standing eye.
[[nodiscard]] bool player_spawn_role(const PlacementRole role) {
    return role == PlacementRole::player_start || role == PlacementRole::deathmatch_spawn ||
           role == PlacementRole::team_initial_spawn || role == PlacementRole::team_respawn ||
           role == PlacementRole::coop_start;
}

[[nodiscard]] GridCoord player_spawn_eye_offset(const Layout &layout) {
    // Original baseq2 p_client.c PutClientInServer sets standing viewheight=22.
    // Other title/actor view policies need separate source and physics review.
    return layout.spec.game == Game::quake2 ? 22 : 0;
}

[[nodiscard]] bool point_in_free_space(const AnalysisContext &context, const GridVec3 point) {
    return std::any_of(context.free_volumes.begin(), context.free_volumes.end(),
                       [point](const GridBounds &bounds) { return bounds.contains(point); });
}

[[nodiscard]] bool segment_in_free_space(const AnalysisContext &context, const GridVec3 from, const GridVec3 to) {
    std::vector<RationalInterval> intervals;
    intervals.reserve(context.free_volumes.size());
    for (const GridBounds &bounds : context.free_volumes) {
        if (const std::optional<RationalInterval> interval = segment_box_interval(from, to, bounds))
            intervals.push_back(*interval);
    }
    std::sort(intervals.begin(), intervals.end(), [](const RationalInterval &left, const RationalInterval &right) {
        const int minimum_comparison = compare(left.minimum, right.minimum);
        return minimum_comparison != 0 ? minimum_comparison < 0 : less(left.maximum, right.maximum);
    });

    Rational covered{0, 1};
    bool started = false;
    for (const RationalInterval &interval : intervals) {
        if (!started) {
            if (compare(interval.minimum, Rational{0, 1}) != 0)
                return false;
            covered = interval.maximum;
            started = true;
        } else {
            if (!less_equal(interval.minimum, covered))
                return false;
            if (less(covered, interval.maximum))
                covered = interval.maximum;
        }
        if (!less(covered, Rational{1, 1}))
            return true;
    }
    return false;
}

[[nodiscard]] bool occluding_purpose(const LayoutSolid &solid) {
    return solid.contents == BrushContents::solid && solid.purpose != BrushPurpose::trigger_volume &&
           solid.material != MaterialRole::trigger;
}

[[nodiscard]] AnalysisContext make_context(const Layout &layout, const VisibilityOptions &options) {
    validate_options(options);
    if (!valid_game(layout.spec.game) || !valid_mode(layout.spec.mode))
        fail("invalid_mode_semantics", "visibility input has an invalid game or mode");
    if (layout.spec.target_players < 1 ||
        static_cast<std::size_t>(layout.spec.target_players) > options.limits.max_placements) {
        fail("invalid_mode_semantics", "visibility input has an invalid target-player count");
    }
    if (layout.rooms.empty())
        fail("empty_free_space", "visibility analysis requires at least one room");
    if (layout.rooms.size() > options.limits.max_rooms)
        fail("room_limit", "visibility room cap was exceeded");
    if (layout.connections.size() > options.limits.max_connections)
        fail("connection_limit", "visibility connection cap was exceeded");
    if (layout.solids.size() > options.limits.max_solids)
        fail("solid_limit", "visibility solid cap was exceeded");
    if (layout.placements.size() > options.limits.max_placements)
        fail("placement_limit", "visibility placement cap was exceeded");

    AnalysisContext context;
    context.layout = &layout;
    context.options = &options;

    std::set<std::uint32_t> room_ids;
    context.rooms.reserve(layout.rooms.size());
    context.free_volumes.reserve(layout.rooms.size());
    for (const Room &room : layout.rooms) {
        if (!room.id || !room_ids.insert(room.id.value).second)
            fail("invalid_room_ids", "visibility input requires unique nonzero room identifiers");
        require_bounds(room.bounds, options.limits, "invalid_room_bounds");
        context.rooms.push_back(&room);
        context.free_volumes.push_back(room.bounds);
    }
    std::sort(context.rooms.begin(), context.rooms.end(),
              [](const Room *left, const Room *right) { return left->id < right->id; });

    std::set<std::uint32_t> connection_ids;
    context.connections.reserve(layout.connections.size());
    for (const Connection &connection : layout.connections) {
        if (!connection.id || !connection_ids.insert(connection.id.value).second)
            fail("invalid_connection_ids", "visibility input requires unique nonzero connection identifiers");
        if (!room_ids.contains(connection.from_room.value) || !room_ids.contains(connection.to_room.value) ||
            connection.from_room == connection.to_room || connection.width <= 0 || connection.height <= 0 ||
            connection.passage_volumes.empty()) {
            fail("invalid_connection", "visibility input has an invalid typed room connection");
        }
        if (connection.passage_volumes.size() >
            options.limits.max_free_volumes - std::min(options.limits.max_free_volumes, context.free_volumes.size())) {
            fail("free_volume_limit", "visibility free-volume cap was exceeded");
        }
        for (const GridBounds &passage : connection.passage_volumes) {
            require_bounds(passage, options.limits, "invalid_passage_bounds");
            context.free_volumes.push_back(passage);
        }
        context.connections.push_back(&connection);
    }
    if (context.free_volumes.size() > options.limits.max_free_volumes)
        fail("free_volume_limit", "visibility free-volume cap was exceeded");
    std::sort(context.connections.begin(), context.connections.end(),
              [](const Connection *left, const Connection *right) { return left->id < right->id; });
    std::sort(context.free_volumes.begin(), context.free_volumes.end(),
              [](const GridBounds &left, const GridBounds &right) { return left < right; });

    std::set<std::uint32_t> placement_ids;
    context.placements.reserve(layout.placements.size());
    for (const Placement &placement : layout.placements) {
        if (!placement.id || !placement_ids.insert(placement.id.value).second)
            fail("invalid_placement_ids", "visibility input requires unique nonzero placement identifiers");
        require_point_limit(placement.origin, options.limits);
        if (placement.volume)
            require_bounds(*placement.volume, options.limits, "invalid_placement_volume");
        validate_role_semantics(layout, placement);
        context.placements.push_back(&placement);
        auto sample = placement.origin;
        if (player_spawn_role(placement.role))
            sample.z += player_spawn_eye_offset(layout);
        // Origins were already bounded by the portable one-million-unit cap,
        // so the small title offset cannot overflow GridCoord arithmetic.
        require_point_limit(sample, options.limits);
        context.placement_samples.emplace(placement.id, sample);
    }
    std::sort(context.placements.begin(), context.placements.end(),
              [](const Placement *left, const Placement *right) { return left->id < right->id; });
    for (const Placement *placement : context.placements) {
        if (placement->link && !placement_ids.contains(placement->link->value))
            fail("invalid_placement_link", "visibility input has a dangling placement link");
        if (!point_in_free_space(context, placement->origin))
            fail("placement_outside_free_space", "visibility placement origin is outside room/passage free space");
        if (!point_in_free_space(context, context.placement_samples.at(placement->id)))
            fail("sample_outside_free_space", "visibility placement sample is outside room/passage free space");
    }

    std::set<std::uint32_t> solid_ids;
    context.occluders.reserve(layout.solids.size());
    for (const LayoutSolid &solid : layout.solids) {
        if (!solid.id || !solid_ids.insert(solid.id.value).second)
            fail("invalid_solid_ids", "visibility input requires unique nonzero solid identifiers");
        const BrushGeometryValidation geometry = validate_brush_geometry(solid.geometry);
        if (!geometry.ok())
            fail("invalid_solid_geometry", "visibility input has invalid convex solid geometry");
        require_bounds(*geometry.bounds, options.limits, "invalid_solid_geometry");
        if (solid.owner && !placement_ids.contains(solid.owner->value))
            fail("invalid_solid_owner", "visibility input has a dangling solid owner");
        if (occluding_purpose(solid))
            context.occluders.push_back({&solid, *geometry.bounds, get_convex_polyhedron(solid.geometry) != nullptr});
    }
    std::sort(context.occluders.begin(), context.occluders.end(),
              [](const Occluder &left, const Occluder &right) { return left.solid->id < right.solid->id; });
    for (const auto *placement : context.placements) {
        if (layout.spec.game != Game::quake2 || !player_spawn_role(placement->role))
            continue;
        const auto sample = context.placement_samples.at(placement->id);
        for (const auto &occluder : context.occluders)
            if (!occluder.conservative && occluder.bounds.contains(sample))
                fail("spawn_eye_in_solid", "standing Q2 spawn-eye sample is inside an exact solid blocker");
    }
    return context;
}

[[nodiscard]] SegmentVisibilityResult
classify_segment(const AnalysisContext &context, const GridVec3 from, const GridVec3 to,
                 const std::optional<PlacementId> first_placement = std::nullopt,
                 const std::optional<PlacementId> second_placement = std::nullopt) {
    require_point_limit(from, context.options->limits);
    require_point_limit(to, context.options->limits);
    if (!segment_in_free_space(context, from, to))
        return {SegmentVisibilityKind::outside_free_space, VisibilityPrecision::exact, std::nullopt};

    struct Candidate {
        Rational entry;
        SolidId id;
    };
    std::optional<Candidate> exact;
    std::optional<Candidate> conservative;
    const auto earlier = [](const Candidate &left, const Candidate &right) {
        const int entry_comparison = compare(left.entry, right.entry);
        return entry_comparison != 0 ? entry_comparison < 0 : left.id < right.id;
    };
    for (const Occluder &occluder : context.occluders) {
        if (occluder.solid->owner &&
            (occluder.solid->owner == first_placement || occluder.solid->owner == second_placement)) {
            continue;
        }
        const std::optional<RationalInterval> interval = segment_box_interval(from, to, occluder.bounds);
        if (!interval || !less(Rational{0, 1}, interval->maximum) || !less(interval->minimum, Rational{1, 1}))
            continue;
        const Candidate candidate{interval->minimum, occluder.solid->id};
        std::optional<Candidate> &destination = occluder.conservative ? conservative : exact;
        if (!destination || earlier(candidate, *destination))
            destination = candidate;
    }
    if (exact)
        return {SegmentVisibilityKind::occluded, VisibilityPrecision::exact, exact->id};
    if (conservative)
        return {SegmentVisibilityKind::occluded, VisibilityPrecision::conservative, conservative->id};
    return {SegmentVisibilityKind::visible, VisibilityPrecision::exact, std::nullopt};
}

// Conservative convex bounds can prove that a ray may be blocked, but cannot
// prove that it is blocked by the actual convex solid. Safety metrics therefore
// keep those rays in the potentially-visible set.
[[nodiscard]] bool potentially_visible(const SegmentVisibilityResult &visibility) noexcept {
    return visibility.kind == SegmentVisibilityKind::visible ||
           (visibility.kind == SegmentVisibilityKind::occluded &&
            visibility.precision == VisibilityPrecision::conservative);
}

[[nodiscard]] std::uint64_t squared_distance(const GridVec3 first, const GridVec3 second) {
    const std::int64_t dx = static_cast<std::int64_t>(first.x) - second.x;
    const std::int64_t dy = static_cast<std::int64_t>(first.y) - second.y;
    const std::int64_t dz = static_cast<std::int64_t>(first.z) - second.z;
    const std::int64_t total =
        checked_add64(checked_add64(checked_multiply(dx, dx), checked_multiply(dy, dy)), checked_multiply(dz, dz));
    return static_cast<std::uint64_t>(total);
}

[[nodiscard]] std::uint32_t basis_points(const std::uint64_t numerator, const std::uint64_t denominator) {
    if (denominator == 0U)
        return 0;
    return static_cast<std::uint32_t>((numerator * 10000U + denominator / 2U) / denominator);
}

[[nodiscard]] std::uint64_t pair_key(const PlacementId first, const PlacementId second) {
    const std::uint32_t low = std::min(first.value, second.value);
    const std::uint32_t high = std::max(first.value, second.value);
    return (static_cast<std::uint64_t>(low) << 32U) | high;
}

class SegmentBudget {
  public:
    SegmentBudget(const AnalysisContext &context, std::uint64_t &count) : context_(context), count_(count) {}

    [[nodiscard]] SegmentVisibilityResult test(const GridVec3 from, const GridVec3 to,
                                               const std::optional<PlacementId> first = std::nullopt,
                                               const std::optional<PlacementId> second = std::nullopt) {
        if (count_ >= context_.options->limits.max_segment_tests)
            fail("segment_test_limit", "visibility segment-test cap was exceeded");
        ++count_;
        return classify_segment(context_, from, to, first, second);
    }

  private:
    const AnalysisContext &context_;
    std::uint64_t &count_;
};

[[nodiscard]] bool gameplay_placement(const PlacementRole role) {
    switch (role) {
    case PlacementRole::player_start:
    case PlacementRole::deathmatch_spawn:
    case PlacementRole::team_initial_spawn:
    case PlacementRole::team_respawn:
    case PlacementRole::weapon:
    case PlacementRole::health:
    case PlacementRole::armor:
    case PlacementRole::monster:
    case PlacementRole::flag:
        return true;
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
    // Change r1s-sp-coop-starts: the co-op starts of a single-player map are freed (Quake 2) or never read (Quake 1)
    // in single-player play (src/entities.cpp), so the single-player room exposure leaves them out.
    case PlacementRole::coop_start:
        return false;
    }
    return false;
}

[[nodiscard]] bool native_single_player(const Layout &layout) {
    return layout.spec.mode == GameMode::single_player && layout.spec.game != Game::quake3;
}

[[nodiscard]] bool active_spawn(const Layout &layout, const Placement &placement) {
    if (native_single_player(layout))
        return placement.role == PlacementRole::player_start;
    if (layout.spec.mode != GameMode::capture_the_flag)
        return placement.role == PlacementRole::deathmatch_spawn;
    if (layout.spec.game == Game::quake1)
        return placement.role == PlacementRole::deathmatch_spawn;
    if (layout.spec.game == Game::quake2) {
        return placement.role == PlacementRole::deathmatch_spawn || placement.role == PlacementRole::team_initial_spawn;
    }
    return placement.role == PlacementRole::deathmatch_spawn || placement.role == PlacementRole::team_initial_spawn ||
           placement.role == PlacementRole::team_respawn;
}

[[nodiscard]] bool hostile_to_focus(const Layout &layout, const Placement &focus, const Placement &observer) {
    if (focus.id == observer.id)
        return false;
    if (native_single_player(layout))
        return focus.role == PlacementRole::player_start && observer.role == PlacementRole::monster;
    if (!active_spawn(layout, observer))
        return false;
    if (focus.team && observer.team && focus.team == observer.team)
        return false;
    return active_spawn(layout, focus) || focus.role == PlacementRole::flag;
}

[[nodiscard]] bool point_in_occluder(const AnalysisContext &context, const GridVec3 point) {
    return std::any_of(context.occluders.begin(), context.occluders.end(),
                       [point](const Occluder &occluder) { return occluder.bounds.contains(point); });
}

[[nodiscard]] GridCoord checked_visibility_coord(const std::int64_t value) {
    try {
        return checked_grid_coord(value);
    } catch (const std::overflow_error &) {
        fail("arithmetic_overflow", "visibility candidate coordinate overflowed the integer grid");
    }
}

[[nodiscard]] GridCoord lower_midpoint(const GridCoord minimum, const GridCoord maximum) {
    return checked_visibility_coord(static_cast<std::int64_t>(minimum) +
                                    (static_cast<std::int64_t>(maximum) - minimum) / 2);
}

[[nodiscard]] std::int64_t divide_power_of_two(const std::int64_t value, const std::size_t shift) {
    const std::int64_t divisor = static_cast<std::int64_t>(std::uint64_t{1} << shift);
    return value / divisor;
}

[[nodiscard]] std::uint32_t cordic_acute_angle_millidegrees(std::int64_t x, std::int64_t y) {
    if (y == 0)
        return 0;
    if (x == 0)
        return 90000;
    constexpr std::array<std::int32_t, 31> angles{
        45000, 26565, 14036, 7125, 3576, 1790, 895, 448, 224, 112, 56, 28, 14, 7, 3, 2,
        1,     0,     0,     0,    0,    0,    0,   0,   0,   0,   0,  0,  0,  0, 0,
    };
    constexpr std::int64_t target_scale = std::int64_t{1} << 50;
    while (std::max(x, y) < target_scale) {
        x = checked_add64(x, x);
        y = checked_add64(y, y);
    }

    std::int64_t angle = 0;
    for (std::size_t iteration = 0; iteration < angles.size(); ++iteration) {
        if (y == 0)
            break;
        const std::int64_t shifted_x = divide_power_of_two(x, iteration);
        const std::int64_t shifted_y = divide_power_of_two(y, iteration);
        if (y > 0) {
            x = checked_add64(x, shifted_y);
            y = checked_add64(y, -shifted_x);
            angle += angles[iteration];
        } else {
            x = checked_add64(x, -shifted_y);
            y = checked_add64(y, shifted_x);
            angle -= angles[iteration];
        }
    }
    angle = std::clamp<std::int64_t>(angle, 0, 90000);
    return static_cast<std::uint32_t>(angle);
}

[[nodiscard]] std::optional<std::uint32_t> horizontal_angle_millidegrees(const GridVec3 focus, const GridVec3 first,
                                                                         const GridVec3 second) {
    const std::int64_t first_x = static_cast<std::int64_t>(first.x) - focus.x;
    const std::int64_t first_y = static_cast<std::int64_t>(first.y) - focus.y;
    const std::int64_t second_x = static_cast<std::int64_t>(second.x) - focus.x;
    const std::int64_t second_y = static_cast<std::int64_t>(second.y) - focus.y;
    if ((first_x == 0 && first_y == 0) || (second_x == 0 && second_y == 0))
        return std::nullopt;

    const std::int64_t dot = checked_add64(checked_multiply(first_x, second_x), checked_multiply(first_y, second_y));
    const std::int64_t cross = checked_add64(checked_multiply(first_x, second_y), -checked_multiply(first_y, second_x));
    const std::int64_t absolute_cross = static_cast<std::int64_t>(magnitude(cross));
    const std::int64_t absolute_dot = static_cast<std::int64_t>(magnitude(dot));
    const std::uint32_t acute = cordic_acute_angle_millidegrees(absolute_dot, absolute_cross);
    return dot < 0 ? 180000U - acute : acute;
}

struct GraphEdge {
    std::size_t neighbor{};
    std::size_t connection{};
};

struct GraphAnalysis {
    std::vector<std::vector<GraphEdge>> adjacency;
    std::vector<std::uint32_t> component;
    std::vector<bool> articulation;
    std::vector<bool> bridge;
    std::uint32_t component_count{};
};

[[nodiscard]] GraphAnalysis analyze_graph(const AnalysisContext &context,
                                          const std::map<std::uint32_t, std::size_t> &room_indices) {
    GraphAnalysis result;
    result.adjacency.resize(context.rooms.size());
    result.component.resize(context.rooms.size());
    result.articulation.resize(context.rooms.size());
    result.bridge.resize(context.connections.size());
    for (std::size_t index = 0; index < context.connections.size(); ++index) {
        const Connection &connection = *context.connections[index];
        const std::size_t from = room_indices.at(connection.from_room.value);
        const std::size_t to = room_indices.at(connection.to_room.value);
        result.adjacency[from].push_back({to, index});
        result.adjacency[to].push_back({from, index});
    }
    for (std::vector<GraphEdge> &edges : result.adjacency) {
        std::sort(edges.begin(), edges.end(), [&context](const GraphEdge &left, const GraphEdge &right) {
            const RoomId left_room = context.rooms[left.neighbor]->id;
            const RoomId right_room = context.rooms[right.neighbor]->id;
            return left_room != right_room
                       ? left_room < right_room
                       : context.connections[left.connection]->id < context.connections[right.connection]->id;
        });
    }

    std::vector<std::int32_t> discovery(context.rooms.size(), -1);
    std::vector<std::int32_t> low(context.rooms.size(), -1);
    std::int32_t next_discovery = 0;
    const std::size_t no_edge = std::numeric_limits<std::size_t>::max();
    std::function<void(std::size_t, std::size_t)> visit = [&](const std::size_t room, const std::size_t parent_edge) {
        discovery[room] = next_discovery;
        low[room] = next_discovery;
        ++next_discovery;
        result.component[room] = result.component_count;
        std::size_t children = 0;
        for (const GraphEdge edge : result.adjacency[room]) {
            if (discovery[edge.neighbor] < 0) {
                ++children;
                visit(edge.neighbor, edge.connection);
                low[room] = std::min(low[room], low[edge.neighbor]);
                if (low[edge.neighbor] > discovery[room])
                    result.bridge[edge.connection] = true;
                if (parent_edge != no_edge && low[edge.neighbor] >= discovery[room])
                    result.articulation[room] = true;
            } else if (edge.connection != parent_edge) {
                low[room] = std::min(low[room], discovery[edge.neighbor]);
            }
        }
        if (parent_edge == no_edge && children > 1)
            result.articulation[room] = true;
    };

    for (std::size_t room = 0; room < context.rooms.size(); ++room) {
        if (discovery[room] >= 0)
            continue;
        ++result.component_count;
        visit(room, no_edge);
    }
    return result;
}

[[nodiscard]] std::size_t containing_room_index(const AnalysisContext &context, const GridVec3 point) {
    for (std::size_t index = 0; index < context.rooms.size(); ++index) {
        if (context.rooms[index]->bounds.contains(point))
            return index;
    }
    return std::numeric_limits<std::size_t>::max();
}

void add_cover_candidate(VisibilityAnalysis &analysis, SegmentBudget &budget, const AnalysisContext &context,
                         const Room &room, const Occluder &occluder, const CoverDirection direction,
                         const GridVec3 sheltered, const GridVec3 opposite) {
    if (!room.bounds.contains(sheltered) || !room.bounds.contains(opposite) ||
        !point_in_free_space(context, sheltered) || !point_in_free_space(context, opposite) ||
        point_in_occluder(context, sheltered) || point_in_occluder(context, opposite)) {
        return;
    }
    const SegmentVisibilityResult visibility = budget.test(sheltered, opposite);
    if (visibility.kind != SegmentVisibilityKind::occluded || visibility.precision != VisibilityPrecision::exact ||
        visibility.blocker != occluder.solid->id)
        return;
    if (analysis.cover_opportunities.size() >= context.options->limits.max_cover_opportunities)
        fail("cover_opportunity_limit", "visibility cover-opportunity cap was exceeded");
    analysis.cover_opportunities.push_back(
        {room.id, occluder.solid->id, direction, sheltered, opposite, visibility.precision});
}

void find_cover_opportunities(VisibilityAnalysis &analysis, SegmentBudget &budget, const AnalysisContext &context) {
    const GridCoord clearance = context.options->thresholds.cover_clearance;
    const GridCoord eye_height = context.options->thresholds.cover_eye_height;
    for (const Room *room : context.rooms) {
        const std::int64_t room_height = static_cast<std::int64_t>(room->bounds.max.z) - room->bounds.min.z;
        const std::int64_t requested_z = static_cast<std::int64_t>(room->bounds.min.z) + eye_height;
        const GridCoord eye_z =
            requested_z < room->bounds.max.z
                ? checked_visibility_coord(requested_z)
                : checked_visibility_coord(static_cast<std::int64_t>(room->bounds.min.z) + room_height / 2);
        for (const Occluder &occluder : context.occluders) {
            const GridBounds &solid = occluder.bounds;
            if (eye_z < solid.min.z || eye_z > solid.max.z)
                continue;
            const GridCoord overlap_min_x = std::max(room->bounds.min.x, solid.min.x);
            const GridCoord overlap_max_x = std::min(room->bounds.max.x, solid.max.x);
            const GridCoord overlap_min_y = std::max(room->bounds.min.y, solid.min.y);
            const GridCoord overlap_max_y = std::min(room->bounds.max.y, solid.max.y);
            if (overlap_min_x > overlap_max_x || overlap_min_y > overlap_max_y)
                continue;

            const GridCoord middle_x = lower_midpoint(overlap_min_x, overlap_max_x);
            const GridCoord middle_y = lower_midpoint(overlap_min_y, overlap_max_y);
            const GridCoord negative_x = checked_visibility_coord(static_cast<std::int64_t>(solid.min.x) - clearance);
            const GridCoord positive_x = checked_visibility_coord(static_cast<std::int64_t>(solid.max.x) + clearance);
            const GridCoord negative_y = checked_visibility_coord(static_cast<std::int64_t>(solid.min.y) - clearance);
            const GridCoord positive_y = checked_visibility_coord(static_cast<std::int64_t>(solid.max.y) + clearance);

            const GridVec3 x_negative{negative_x, middle_y, eye_z};
            const GridVec3 x_positive{positive_x, middle_y, eye_z};
            add_cover_candidate(analysis, budget, context, *room, occluder, CoverDirection::negative_x, x_negative,
                                x_positive);
            add_cover_candidate(analysis, budget, context, *room, occluder, CoverDirection::positive_x, x_positive,
                                x_negative);

            const GridVec3 y_negative{middle_x, negative_y, eye_z};
            const GridVec3 y_positive{middle_x, positive_y, eye_z};
            add_cover_candidate(analysis, budget, context, *room, occluder, CoverDirection::negative_y, y_negative,
                                y_positive);
            add_cover_candidate(analysis, budget, context, *room, occluder, CoverDirection::positive_y, y_positive,
                                y_negative);
        }
    }
    std::sort(analysis.cover_opportunities.begin(), analysis.cover_opportunities.end());
}

[[nodiscard]] std::uint32_t checked_count(const std::size_t value) {
    if (value > std::numeric_limits<std::uint32_t>::max())
        fail("result_overflow", "visibility result count exceeds the 32-bit metric range");
    return static_cast<std::uint32_t>(value);
}

} // namespace

VisibilityError::VisibilityError(std::string code, std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

SegmentVisibilityResult VisibilityAnalyzer::classify_segment(const Layout &layout, const GridVec3 from,
                                                             const GridVec3 to,
                                                             const VisibilityOptions &options) const {
    return classify_segments(layout, {{from, to}}, options).front();
}

std::vector<SegmentVisibilityResult>
VisibilityAnalyzer::classify_segments(const Layout &layout, const std::vector<SegmentVisibilityQuery> &queries,
                                      const VisibilityOptions &options) const {
    const AnalysisContext context = make_context(layout, options);
    if (queries.size() > options.limits.max_segment_tests)
        fail("segment_test_limit", "visibility segment-query batch exceeds the segment-test cap");
    std::vector<SegmentVisibilityResult> results;
    results.reserve(queries.size());
    for (const SegmentVisibilityQuery &query : queries)
        results.push_back(qmg::classify_segment(context, query.from, query.to));
    return results;
}

VisibilityAnalysis VisibilityAnalyzer::analyze(const Layout &layout, const VisibilityOptions &options) const {
    const AnalysisContext context = make_context(layout, options);
    VisibilityAnalysis result;
    result.layout_sha256 = canonical_layout_sha256(layout);
    result.player_spawn_eye_offset_z = player_spawn_eye_offset(layout);
    std::uint64_t segment_tests = 0;
    SegmentBudget budget(context, segment_tests);

    std::map<std::uint32_t, std::size_t> room_indices;
    for (std::size_t index = 0; index < context.rooms.size(); ++index)
        room_indices.emplace(context.rooms[index]->id.value, index);

    std::vector<std::vector<SegmentVisibilityResult>> room_visibility(
        context.rooms.size(), std::vector<SegmentVisibilityResult>(context.rooms.size()));
    for (std::size_t room = 0; room < context.rooms.size(); ++room) {
        room_visibility[room][room] = {SegmentVisibilityKind::visible, VisibilityPrecision::exact, std::nullopt};
        for (std::size_t other = room + 1; other < context.rooms.size(); ++other) {
            const SegmentVisibilityResult visibility =
                budget.test(context.rooms[room]->bounds.center(), context.rooms[other]->bounds.center());
            room_visibility[room][other] = visibility;
            room_visibility[other][room] = visibility;
        }
    }

    std::map<std::uint64_t, SegmentVisibilityResult> placement_visibility;
    const std::size_t placement_count = context.placements.size();
    if (placement_count > 1 && placement_count > options.limits.max_segment_tests * 2U / (placement_count - 1)) {
        fail("segment_test_limit", "visibility placement-pair count exceeds the segment-test cap");
    }
    result.placement_lines_of_sight.reserve(placement_count * (placement_count > 0 ? placement_count - 1 : 0) / 2);
    for (std::size_t first = 0; first < placement_count; ++first) {
        for (std::size_t second = first + 1; second < placement_count; ++second) {
            const Placement &left = *context.placements[first];
            const Placement &right = *context.placements[second];
            const auto first_sample = context.placement_samples.at(left.id);
            const auto second_sample = context.placement_samples.at(right.id);
            const SegmentVisibilityResult visibility = budget.test(first_sample, second_sample, left.id, right.id);
            result.placement_lines_of_sight.push_back({left.id, right.id, squared_distance(left.origin, right.origin),
                                                       visibility, first_sample, second_sample});
            placement_visibility.emplace(pair_key(left.id, right.id), visibility);
        }
    }

    const GraphAnalysis graph = analyze_graph(context, room_indices);
    std::vector<std::uint32_t> readable_by_room(context.rooms.size());
    result.connection_readability.reserve(context.connections.size());
    for (std::size_t index = 0; index < context.connections.size(); ++index) {
        const Connection &connection = *context.connections[index];
        const std::size_t from = room_indices.at(connection.from_room.value);
        const std::size_t to = room_indices.at(connection.to_room.value);
        const SegmentVisibilityResult visibility = room_visibility[from][to];
        const bool readable = potentially_visible(visibility);
        if (readable) {
            ++readable_by_room[from];
            ++readable_by_room[to];
        }
        const bool narrow = connection.width <= options.thresholds.chokepoint_width;
        result.connection_readability.push_back(
            {connection.id, graph.bridge[index], narrow, graph.bridge[index] || narrow, visibility});
    }

    result.room_topology.reserve(context.rooms.size());
    for (std::size_t index = 0; index < context.rooms.size(); ++index) {
        const std::size_t connection_count = graph.adjacency[index].size();
        result.room_topology.push_back(
            {context.rooms[index]->id, graph.component[index], checked_count(connection_count), readable_by_room[index],
             checked_count(connection_count), basis_points(readable_by_room[index], connection_count),
             graph.articulation[index]});
    }

    std::vector<const Placement *> gameplay_placements;
    for (const Placement *placement : context.placements) {
        // Change r1s-mp-player-start: the map-command info_player_start of a Quake 1 or Quake 2 multiplayer map is
        // not part of match play (src/entities.cpp), so room exposure leaves it out.
        if (gameplay_placement(placement->role) && !is_multiplayer_map_start(layout, *placement))
            gameplay_placements.push_back(placement);
    }
    result.room_exposure.reserve(context.rooms.size());
    for (std::size_t room = 0; room < context.rooms.size(); ++room) {
        std::uint32_t visible_rooms = 0;
        for (std::size_t other = 0; other < context.rooms.size(); ++other) {
            if (other != room && potentially_visible(room_visibility[room][other]))
                ++visible_rooms;
        }
        std::uint32_t visible_placements = 0;
        const GridVec3 observation = context.rooms[room]->bounds.center();
        for (const Placement *placement : gameplay_placements) {
            if (potentially_visible(budget.test(observation, context.placement_samples.at(placement->id), std::nullopt,
                                                placement->id))) {
                ++visible_placements;
            }
        }
        const std::uint32_t tested_rooms = checked_count(context.rooms.size() - 1);
        const std::uint32_t tested_placements = checked_count(gameplay_placements.size());
        result.room_exposure.push_back({context.rooms[room]->id, observation, visible_rooms, tested_rooms,
                                        visible_placements, tested_placements,
                                        basis_points(static_cast<std::uint64_t>(visible_rooms) + visible_placements,
                                                     static_cast<std::uint64_t>(tested_rooms) + tested_placements)});
    }

    find_cover_opportunities(result, budget, context);

    const auto potentially_visible_between = [&placement_visibility](const PlacementId first,
                                                                     const PlacementId second) {
        const auto found = placement_visibility.find(pair_key(first, second));
        if (found == placement_visibility.end())
            fail("result_invariant", "visibility placement pair lookup is incomplete");
        return potentially_visible(found->second);
    };

    std::vector<const Placement *> focuses;
    for (const Placement *placement : context.placements) {
        if (active_spawn(layout, *placement) || placement->role == PlacementRole::flag)
            focuses.push_back(placement);
    }
    std::size_t crossfire_candidates = 0;
    for (const Placement *focus : focuses) {
        std::vector<const Placement *> observers;
        for (const Placement *placement : context.placements) {
            if (hostile_to_focus(layout, *focus, *placement))
                observers.push_back(placement);
        }
        for (std::size_t first = 0; first < observers.size(); ++first) {
            for (std::size_t second = first + 1; second < observers.size(); ++second) {
                if (crossfire_candidates >= options.limits.max_crossfire_records)
                    fail("crossfire_limit", "visibility crossfire-combination cap was exceeded");
                ++crossfire_candidates;
                if (!potentially_visible_between(focus->id, observers[first]->id) ||
                    !potentially_visible_between(focus->id, observers[second]->id)) {
                    continue;
                }
                const std::optional<std::uint32_t> angle =
                    horizontal_angle_millidegrees(focus->origin, observers[first]->origin, observers[second]->origin);
                if (!angle)
                    continue;
                result.crossfire_angles.push_back({focus->id, observers[first]->id, observers[second]->id, *angle,
                                                   *angle >= options.thresholds.minimum_crossfire_angle_millidegrees});
            }
        }
    }
    std::sort(result.crossfire_angles.begin(), result.crossfire_angles.end());

    const auto append_finding = [&result, &options](SpawnSafetyFinding finding) {
        if (result.findings.size() >= options.limits.max_findings)
            fail("finding_limit", "visibility spawn-safety finding cap was exceeded");
        result.findings.push_back(std::move(finding));
    };

    std::vector<const Placement *> spawns;
    for (const Placement *placement : context.placements) {
        if (active_spawn(layout, *placement))
            spawns.push_back(placement);
    }
    const std::size_t expected_spawn_count =
        native_single_player(layout) ? 1U : static_cast<std::size_t>(std::max(layout.spec.target_players, 2));
    if (spawns.size() < expected_spawn_count) {
        SpawnSafetyFinding finding;
        finding.code = SpawnSafetyCode::missing_required_spawn;
        finding.severity = Severity::error;
        finding.observed = spawns.size();
        finding.threshold = expected_spawn_count;
        append_finding(std::move(finding));
    }

    const auto require_spawn_role = [&](const PlacementRole role, const std::optional<Team> team) {
        const std::size_t count = static_cast<std::size_t>(
            std::count_if(spawns.begin(), spawns.end(), [role, team](const Placement *placement) {
                return placement->role == role && (!team || placement->team == team);
            }));
        if (count != 0)
            return;
        SpawnSafetyFinding finding;
        finding.code = SpawnSafetyCode::missing_required_spawn;
        finding.severity = Severity::error;
        finding.required_role = role;
        finding.required_team = team;
        finding.threshold = 1;
        append_finding(std::move(finding));
    };
    if (native_single_player(layout)) {
        require_spawn_role(PlacementRole::player_start, std::nullopt);
    } else if (layout.spec.mode != GameMode::capture_the_flag || layout.spec.game == Game::quake1) {
        require_spawn_role(PlacementRole::deathmatch_spawn, std::nullopt);
    } else {
        require_spawn_role(PlacementRole::deathmatch_spawn, std::nullopt);
        require_spawn_role(PlacementRole::team_initial_spawn, Team::red);
        require_spawn_role(PlacementRole::team_initial_spawn, Team::blue);
        if (layout.spec.game == Game::quake3) {
            require_spawn_role(PlacementRole::team_respawn, Team::red);
            require_spawn_role(PlacementRole::team_respawn, Team::blue);
        }
    }

    std::map<std::uint32_t, std::uint32_t> qualifying_crossfires;
    for (const CrossfireAngleMetric &crossfire : result.crossfire_angles) {
        if (!crossfire.qualifies)
            continue;
        ++qualifying_crossfires[crossfire.focus.value];
        const Placement *focus = find_placement(layout, crossfire.focus);
        if (focus == nullptr || !active_spawn(layout, *focus))
            continue;
        SpawnSafetyFinding finding;
        finding.code = SpawnSafetyCode::wide_crossfire;
        finding.severity = Severity::warning;
        finding.spawn = crossfire.focus;
        finding.threat = crossfire.first_observer;
        finding.secondary_threat = crossfire.second_observer;
        finding.observed = crossfire.angle_millidegrees;
        finding.threshold = options.thresholds.minimum_crossfire_angle_millidegrees;
        append_finding(std::move(finding));
    }

    std::vector<std::uint32_t> cover_by_room(context.rooms.size());
    for (const CoverOpportunity &cover : result.cover_opportunities)
        ++cover_by_room[room_indices.at(cover.room.value)];
    const std::int64_t separation = options.thresholds.minimum_spawn_separation;
    const std::uint64_t minimum_separation_squared =
        static_cast<std::uint64_t>(checked_multiply(separation, separation));
    result.spawn_safety.reserve(spawns.size());
    for (const Placement *spawn : spawns) {
        std::uint32_t hostile_origins = 0;
        std::uint32_t visible_hostiles = 0;
        std::uint32_t close_hostiles = 0;
        for (const Placement *threat : context.placements) {
            if (!hostile_to_focus(layout, *spawn, *threat))
                continue;
            ++hostile_origins;
            const std::uint64_t distance = squared_distance(spawn->origin, threat->origin);
            if (distance < minimum_separation_squared) {
                ++close_hostiles;
                SpawnSafetyFinding finding;
                finding.code = SpawnSafetyCode::insufficient_separation;
                finding.severity = Severity::warning;
                finding.spawn = spawn->id;
                finding.threat = threat->id;
                finding.observed = distance;
                finding.threshold = minimum_separation_squared;
                append_finding(std::move(finding));
            }
            if (potentially_visible_between(spawn->id, threat->id)) {
                ++visible_hostiles;
                SpawnSafetyFinding finding;
                finding.code = SpawnSafetyCode::hostile_line_of_sight;
                finding.severity = Severity::warning;
                finding.spawn = spawn->id;
                finding.threat = threat->id;
                finding.observed = distance;
                append_finding(std::move(finding));
            }
        }
        const std::size_t room_index = containing_room_index(context, spawn->origin);
        const std::uint32_t cover_count =
            room_index == std::numeric_limits<std::size_t>::max() ? 0U : cover_by_room[room_index];
        if (options.thresholds.report_missing_cover && cover_count == 0) {
            SpawnSafetyFinding finding;
            finding.code = SpawnSafetyCode::no_cover_opportunity;
            finding.severity = Severity::warning;
            finding.spawn = spawn->id;
            finding.threshold = 1;
            append_finding(std::move(finding));
        }
        const std::uint32_t crossfires = qualifying_crossfires[spawn->id.value];
        const bool cover_requirement_met = !options.thresholds.report_missing_cover || cover_count > 0;
        result.spawn_safety.push_back(
            {spawn->id, hostile_origins, visible_hostiles, close_hostiles, crossfires, cover_count,
             visible_hostiles == 0 && close_hostiles == 0 && crossfires == 0 && cover_requirement_met});
    }

    const auto optional_id = [](const std::optional<PlacementId> id) { return id ? id->value : 0U; };
    const auto optional_role = [](const std::optional<PlacementRole> role) {
        return role ? static_cast<std::uint32_t>(*role) + 1U : 0U;
    };
    const auto optional_team = [](const std::optional<Team> team) {
        return team ? static_cast<std::uint32_t>(*team) + 1U : 0U;
    };
    std::sort(result.findings.begin(), result.findings.end(),
              [&](const SpawnSafetyFinding &left, const SpawnSafetyFinding &right) {
                  return std::tuple{static_cast<std::uint32_t>(left.code),
                                    optional_id(left.spawn),
                                    optional_id(left.threat),
                                    optional_id(left.secondary_threat),
                                    optional_role(left.required_role),
                                    optional_team(left.required_team),
                                    left.observed,
                                    left.threshold} < std::tuple{static_cast<std::uint32_t>(right.code),
                                                                 optional_id(right.spawn),
                                                                 optional_id(right.threat),
                                                                 optional_id(right.secondary_threat),
                                                                 optional_role(right.required_role),
                                                                 optional_team(right.required_team),
                                                                 right.observed,
                                                                 right.threshold};
              });
    for (std::size_t index = 0; index < result.findings.size(); ++index)
        result.findings[index].id = VisibilityFindingId{static_cast<std::uint32_t>(index + 1)};

    result.summary.room_count = checked_count(context.rooms.size());
    result.summary.placement_count = checked_count(context.placements.size());
    result.summary.connected_components = graph.component_count;
    result.summary.articulation_rooms =
        checked_count(static_cast<std::size_t>(std::count(graph.articulation.begin(), graph.articulation.end(), true)));
    result.summary.chokepoint_connections = checked_count(static_cast<std::size_t>(
        std::count_if(result.connection_readability.begin(), result.connection_readability.end(),
                      [](const ConnectionReadabilityMetric &connection) { return connection.chokepoint; })));
    result.summary.readable_connections = checked_count(static_cast<std::size_t>(
        std::count_if(result.connection_readability.begin(), result.connection_readability.end(),
                      [](const ConnectionReadabilityMetric &connection) {
                          return potentially_visible(connection.centerline_visibility);
                      })));
    result.summary.evaluated_spawns = checked_count(result.spawn_safety.size());
    result.summary.safe_spawns = checked_count(
        static_cast<std::size_t>(std::count_if(result.spawn_safety.begin(), result.spawn_safety.end(),
                                               [](const SpawnSafetyMetric &spawn) { return spawn.safe; })));
    result.summary.warning_findings = checked_count(static_cast<std::size_t>(
        std::count_if(result.findings.begin(), result.findings.end(),
                      [](const SpawnSafetyFinding &finding) { return finding.severity == Severity::warning; })));
    result.summary.error_findings = checked_count(static_cast<std::size_t>(
        std::count_if(result.findings.begin(), result.findings.end(),
                      [](const SpawnSafetyFinding &finding) { return finding.severity == Severity::error; })));
    result.summary.segment_tests = segment_tests;
    return result;
}

} // namespace qmg
