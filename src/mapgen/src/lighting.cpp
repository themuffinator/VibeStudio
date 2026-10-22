#include "qmg/lighting.hpp"

#include "qmg/layout_fingerprint.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace qmg {
namespace {

constexpr GridCoord kPortableCoordinateLimit = 1'000'000;
constexpr std::size_t kPortableRoomLimit = 4096;
constexpr std::size_t kPortableConnectionLimit = 16384;
constexpr std::size_t kPortablePassageLimit = 65536;
constexpr std::size_t kPortablePlacementLimit = 8192;
constexpr std::size_t kPortableSampleLimit = 131072;
constexpr std::size_t kPortableLightLimit = 8192;
constexpr std::size_t kPortableVisibilityTestLimit = 16'000'000;
constexpr std::size_t kPortableFindingLimit = 262144;

[[noreturn]] void fail(std::string code, std::string message) {
    throw LightingError(std::move(code), std::move(message));
}

[[nodiscard]] bool within_limit(const GridCoord value, const GridCoord limit) {
    const std::int64_t wide = value;
    return wide >= -static_cast<std::int64_t>(limit) && wide <= static_cast<std::int64_t>(limit);
}

void require_point(const GridVec3 point, const LightingLimits &limits) {
    if (!within_limit(point.x, limits.max_abs_coordinate) || !within_limit(point.y, limits.max_abs_coordinate) ||
        !within_limit(point.z, limits.max_abs_coordinate)) {
        fail("coordinate_limit", "lighting analysis point exceeds the configured coordinate limit");
    }
}

void require_bounds(const GridBounds &bounds, const LightingLimits &limits) {
    if (!bounds.valid())
        fail("invalid_bounds", "lighting analysis requires valid room and passage bounds");
    require_point(bounds.min, limits);
    require_point(bounds.max, limits);
}

template <typename Value, typename IdProjection>
[[nodiscard]] std::vector<const Value *> sorted_unique(const std::vector<Value> &values, const std::size_t limit,
                                                       const char *limit_code, const char *id_code,
                                                       IdProjection id_projection) {
    if (values.size() > limit)
        fail(limit_code, "lighting analysis input exceeds its configured collection cap");
    std::vector<const Value *> sorted;
    sorted.reserve(values.size());
    for (const Value &value : values)
        sorted.push_back(&value);
    std::sort(sorted.begin(), sorted.end(),
              [&](const Value *left, const Value *right) { return id_projection(*left) < id_projection(*right); });
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        const auto id = id_projection(*sorted[index]);
        if (!id || (index != 0 && id == id_projection(*sorted[index - 1])))
            fail(id_code, "lighting analysis requires unique nonzero source identifiers");
    }
    return sorted;
}

[[nodiscard]] bool sampled_role(const PlacementRole role) {
    switch (role) {
    case PlacementRole::player_start:
    // Change r1s-sp-coop-starts: a co-op player spawns at the info_player_coop, so it is read like the start.
    case PlacementRole::coop_start:
    case PlacementRole::deathmatch_spawn:
    case PlacementRole::team_initial_spawn:
    case PlacementRole::team_respawn:
    case PlacementRole::weapon:
    case PlacementRole::health:
    case PlacementRole::armor:
    case PlacementRole::monster:
    case PlacementRole::flag:
    case PlacementRole::exit_trigger:
    case PlacementRole::exit_destination:
    case PlacementRole::teleporter_trigger:
    case PlacementRole::teleporter_destination:
        return true;
    case PlacementRole::light:
    case PlacementRole::door:
    case PlacementRole::door_trigger:
    case PlacementRole::door_destination:
    case PlacementRole::lift:
    case PlacementRole::lift_trigger:
    case PlacementRole::lift_destination:
    case PlacementRole::intermission:
        return false;
    }
    fail("invalid_placement_role", "lighting analysis received an unknown placement role");
}

[[nodiscard]] bool critical_role(const PlacementRole role) {
    switch (role) {
    case PlacementRole::player_start:
    case PlacementRole::coop_start:
    case PlacementRole::deathmatch_spawn:
    case PlacementRole::team_initial_spawn:
    case PlacementRole::team_respawn:
    case PlacementRole::monster:
    case PlacementRole::flag:
    case PlacementRole::exit_trigger:
    case PlacementRole::exit_destination:
        return true;
    case PlacementRole::weapon:
    case PlacementRole::health:
    case PlacementRole::armor:
    case PlacementRole::teleporter_trigger:
    case PlacementRole::teleporter_destination:
    case PlacementRole::light:
    case PlacementRole::door:
    case PlacementRole::door_trigger:
    case PlacementRole::door_destination:
    case PlacementRole::lift:
    case PlacementRole::lift_trigger:
    case PlacementRole::lift_destination:
    case PlacementRole::intermission:
        return false;
    }
    fail("invalid_placement_role", "lighting analysis received an unknown placement role");
}

[[nodiscard]] std::uint64_t squared_distance(const GridVec3 first, const GridVec3 second) {
    const std::int64_t dx = static_cast<std::int64_t>(first.x) - second.x;
    const std::int64_t dy = static_cast<std::int64_t>(first.y) - second.y;
    const std::int64_t dz = static_cast<std::int64_t>(first.z) - second.z;
    const auto magnitude = [](const std::int64_t value) {
        return value >= 0 ? static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(-(value + 1)) + 1U;
    };
    std::uint64_t total = 0;
    for (const std::int64_t delta : {dx, dy, dz}) {
        const std::uint64_t absolute = magnitude(delta);
        if (absolute != 0 && absolute > std::numeric_limits<std::uint64_t>::max() / absolute)
            fail("distance_overflow", "lighting squared distance exceeds the deterministic integer range");
        const std::uint64_t square = absolute * absolute;
        if (square > std::numeric_limits<std::uint64_t>::max() - total)
            fail("distance_overflow", "lighting squared distance exceeds the deterministic integer range");
        total += square;
    }
    return total;
}

[[nodiscard]] std::uint32_t luminance_milli(const RgbColor color) {
    const std::uint64_t weighted = static_cast<std::uint64_t>(color.red_milli) * 2126U +
                                   static_cast<std::uint64_t>(color.green_milli) * 7152U +
                                   static_cast<std::uint64_t>(color.blue_milli) * 722U;
    return static_cast<std::uint32_t>((weighted + 5000U) / 10000U);
}

[[nodiscard]] std::uint64_t readable_threshold(const Layout &layout, const LightingThresholds &thresholds) {
    const std::uint64_t base = thresholds.minimum_readable_brightness_milli;
    switch (layout.lighting_style) {
    case LightingStyle::low_key:
        return (base + 1U) / 2U;
    case LightingStyle::balanced:
    case LightingStyle::high_contrast:
        return base;
    }
    fail("invalid_lighting_style", "lighting analysis received an unknown lighting style");
}

[[nodiscard]] std::uint32_t contrast_basis_points(const std::uint64_t minimum, const std::uint64_t maximum) {
    if (minimum == 0)
        return maximum == 0 ? 10'000U : std::numeric_limits<std::uint32_t>::max();
    const std::uint64_t scaled = maximum > std::numeric_limits<std::uint64_t>::max() / 10'000U
                                     ? std::numeric_limits<std::uint64_t>::max()
                                     : maximum * 10'000U;
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(scaled / minimum, std::numeric_limits<std::uint32_t>::max()));
}

struct PendingSample {
    LightingSampleMetric metric;
    std::optional<PlacementRole> placement_role;
};

} // namespace

LightingError::LightingError(std::string code, std::string message)
    : std::runtime_error(code + ": " + message), code_(std::move(code)) {}

LightingAnalysis LightingAnalyzer::analyze(const Layout &layout, const LightingOptions &options) const {
    const LightingLimits &limits = options.limits;
    const LightingThresholds &thresholds = options.thresholds;
    if (limits.max_abs_coordinate <= 0 || limits.max_abs_coordinate > kPortableCoordinateLimit ||
        limits.max_rooms == 0 || limits.max_rooms > kPortableRoomLimit || limits.max_connections == 0 ||
        limits.max_connections > kPortableConnectionLimit || limits.max_passage_volumes == 0 ||
        limits.max_passage_volumes > kPortablePassageLimit || limits.max_placements == 0 ||
        limits.max_placements > kPortablePlacementLimit || limits.max_samples == 0 ||
        limits.max_samples > kPortableSampleLimit || limits.max_lights == 0 ||
        limits.max_lights > kPortableLightLimit || limits.max_visibility_tests == 0 ||
        limits.max_visibility_tests > kPortableVisibilityTestLimit || limits.max_findings == 0 ||
        limits.max_findings > kPortableFindingLimit || thresholds.eye_height <= 0 ||
        thresholds.eye_height > limits.max_abs_coordinate || thresholds.minimum_distance_squared == 0 ||
        thresholds.contribution_scale == 0 || thresholds.maximum_brightness_milli == 0 ||
        thresholds.minimum_readable_brightness_milli > thresholds.maximum_brightness_milli ||
        thresholds.maximum_connection_contrast_basis_points < 10'000U || thresholds.minimum_visible_lights == 0 ||
        thresholds.minimum_visible_lights > limits.max_lights || limits.max_lights > limits.max_placements) {
        fail("invalid_options", "lighting analysis limits and thresholds must be positive and internally consistent");
    }
    const VisibilityLimits &visibility_limits = options.visibility.limits;
    if (limits.max_abs_coordinate > visibility_limits.max_abs_coordinate ||
        limits.max_rooms > visibility_limits.max_rooms || limits.max_connections > visibility_limits.max_connections ||
        limits.max_placements > visibility_limits.max_placements ||
        limits.max_visibility_tests > visibility_limits.max_segment_tests ||
        limits.max_rooms > visibility_limits.max_free_volumes ||
        limits.max_passage_volumes > visibility_limits.max_free_volumes - limits.max_rooms) {
        fail("incompatible_visibility_options",
             "lighting limits must fit within the nested visibility limits used for batched traces");
    }

    const std::vector<const Room *> rooms = sorted_unique(layout.rooms, limits.max_rooms, "room_limit",
                                                          "invalid_room_id", [](const Room &room) { return room.id; });
    const std::vector<const Connection *> connections =
        sorted_unique(layout.connections, limits.max_connections, "connection_limit", "invalid_connection_id",
                      [](const Connection &connection) { return connection.id; });
    const std::vector<const Placement *> placements =
        sorted_unique(layout.placements, limits.max_placements, "placement_limit", "invalid_placement_id",
                      [](const Placement &placement) { return placement.id; });

    std::set<RoomId> room_ids;
    for (const Room *room : rooms) {
        require_bounds(room->bounds, limits);
        room_ids.insert(room->id);
    }
    std::size_t passage_count = 0;
    for (const Connection *connection : connections) {
        if (!room_ids.contains(connection->from_room) || !room_ids.contains(connection->to_room) ||
            connection->from_room == connection->to_room) {
            fail("invalid_connection", "lighting analysis connection endpoints must resolve to distinct rooms");
        }
        if (connection->passage_volumes.size() > limits.max_passage_volumes - passage_count)
            fail("passage_limit", "lighting analysis passage count exceeds its configured cap");
        passage_count += connection->passage_volumes.size();
        for (const GridBounds &passage : connection->passage_volumes)
            require_bounds(passage, limits);
    }
    for (const Placement *placement : placements)
        require_point(placement->origin, limits);

    std::vector<const Placement *> lights;
    for (const Placement *placement : placements) {
        if (placement->role != PlacementRole::light)
            continue;
        if (!placement->light_intensity || *placement->light_intensity <= 0 || !placement->light_color ||
            !placement->light_color->valid()) {
            fail("invalid_light", "typed lights require positive intensity and a valid color");
        }
        lights.push_back(placement);
    }
    if (lights.size() > limits.max_lights)
        fail("light_limit", "lighting analysis light count exceeds its configured cap");

    std::vector<PendingSample> pending_samples;
    std::map<RoomId, LightingSampleId> room_samples;
    std::map<ConnectionId, std::vector<LightingSampleId>> passage_samples;
    std::uint32_t next_sample_id = 1;
    const auto add_sample = [&](const LightingSampleKind kind, const std::uint32_t source_id,
                                const std::uint32_t ordinal, const GridVec3 position, const bool critical,
                                const std::optional<PlacementRole> role = std::nullopt) {
        if (pending_samples.size() >= limits.max_samples || next_sample_id == 0)
            fail("sample_limit", "lighting analysis sample count exceeds its configured cap");
        require_point(position, limits);
        const LightingSampleId id{next_sample_id++};
        pending_samples.push_back(
            {LightingSampleMetric{id, kind, source_id, ordinal, position, 0, 0, 0, 0, 0, critical, false}, role});
        return id;
    };

    for (const Room *room : rooms) {
        const std::int64_t height = static_cast<std::int64_t>(room->bounds.max.z) - room->bounds.min.z;
        const std::int64_t offset = std::min<std::int64_t>(thresholds.eye_height, height / 2);
        GridVec3 point = room->bounds.center();
        point.z = checked_grid_add(room->bounds.min.z, offset);
        room_samples.emplace(room->id, add_sample(LightingSampleKind::room, room->id.value, 0, point, false));
    }
    for (const Connection *connection : connections) {
        std::vector<GridBounds> sorted_passages = connection->passage_volumes;
        std::sort(sorted_passages.begin(), sorted_passages.end());
        sorted_passages.erase(std::unique(sorted_passages.begin(), sorted_passages.end()), sorted_passages.end());
        std::vector<LightingSampleId> &ids = passage_samples[connection->id];
        ids.reserve(sorted_passages.size());
        for (std::size_t index = 0; index < sorted_passages.size(); ++index) {
            ids.push_back(add_sample(LightingSampleKind::passage, connection->id.value,
                                     static_cast<std::uint32_t>(index), sorted_passages[index].center(), false));
        }
    }
    for (const Placement *placement : placements) {
        if (!sampled_role(placement->role))
            continue;
        add_sample(LightingSampleKind::gameplay_placement, placement->id.value, 0, placement->origin,
                   critical_role(placement->role), placement->role);
    }

    const std::uint64_t threshold = readable_threshold(layout, thresholds);
    if (!lights.empty() && pending_samples.size() > limits.max_visibility_tests / lights.size())
        fail("visibility_test_limit", "lighting analysis exceeded its configured visibility-test cap");
    const std::size_t visibility_tests = pending_samples.size() * lights.size();
    VisibilityAnalyzer visibility;
    std::vector<SegmentVisibilityQuery> queries;
    queries.reserve(visibility_tests);
    for (const PendingSample &sample : pending_samples) {
        for (const Placement *light : lights)
            queries.push_back({sample.metric.position, light->origin});
    }
    std::vector<SegmentVisibilityResult> traces;
    try {
        traces = visibility.classify_segments(layout, queries, options.visibility);
    } catch (const VisibilityError &error) {
        fail("visibility_" + error.code(), error.what());
    }
    std::size_t next_trace = 0;
    for (PendingSample &sample : pending_samples) {
        std::uint64_t brightness = 0;
        for (const Placement *light : lights) {
            if (next_trace >= traces.size())
                fail("result_invariant", "batched visibility trace count is incomplete");
            const SegmentVisibilityResult &trace = traces[next_trace++];
            if (trace.precision == VisibilityPrecision::conservative)
                ++sample.metric.conservative_traces;
            if (trace.kind == SegmentVisibilityKind::occluded) {
                ++sample.metric.occluded_lights;
                continue;
            }
            if (trace.kind == SegmentVisibilityKind::outside_free_space) {
                ++sample.metric.outside_lights;
                continue;
            }
            ++sample.metric.visible_lights;
            const std::uint64_t distance =
                std::max(squared_distance(sample.metric.position, light->origin), thresholds.minimum_distance_squared);
            const std::uint64_t intensity = static_cast<std::uint32_t>(*light->light_intensity);
            const std::uint64_t luminance = luminance_milli(*light->light_color);
            if (luminance != 0 &&
                intensity > std::numeric_limits<std::uint64_t>::max() / luminance / thresholds.contribution_scale) {
                fail("brightness_overflow", "lighting contribution exceeds the deterministic integer range");
            }
            const std::uint64_t contribution = intensity * luminance * thresholds.contribution_scale / distance;
            brightness = std::min(thresholds.maximum_brightness_milli,
                                  contribution > thresholds.maximum_brightness_milli - brightness
                                      ? thresholds.maximum_brightness_milli
                                      : brightness + contribution);
        }
        sample.metric.brightness_milli = brightness;
        sample.metric.readable =
            brightness >= threshold && sample.metric.visible_lights >= thresholds.minimum_visible_lights;
    }
    if (next_trace != traces.size())
        fail("result_invariant", "batched visibility trace count is inconsistent");

    LightingAnalysis result;
    result.layout_sha256 = canonical_layout_sha256(layout);
    result.samples.reserve(pending_samples.size());
    for (const PendingSample &sample : pending_samples)
        result.samples.push_back(sample.metric);

    std::map<LightingSampleId, const LightingSampleMetric *> samples_by_id;
    for (const LightingSampleMetric &sample : result.samples)
        samples_by_id.emplace(sample.id, &sample);
    for (const Connection *connection : connections) {
        const LightingSampleId from = room_samples.at(connection->from_room);
        const LightingSampleId to = room_samples.at(connection->to_room);
        const std::vector<LightingSampleId> &passages = passage_samples.at(connection->id);
        std::uint64_t minimum =
            std::min(samples_by_id.at(from)->brightness_milli, samples_by_id.at(to)->brightness_milli);
        std::uint64_t maximum =
            std::max(samples_by_id.at(from)->brightness_milli, samples_by_id.at(to)->brightness_milli);
        bool readable = samples_by_id.at(from)->readable && samples_by_id.at(to)->readable;
        for (const LightingSampleId id : passages) {
            const LightingSampleMetric &sample = *samples_by_id.at(id);
            minimum = std::min(minimum, sample.brightness_milli);
            maximum = std::max(maximum, sample.brightness_milli);
            readable = readable && sample.readable;
        }
        result.connections.push_back(
            {connection->id, from, to, passages, minimum, maximum, contrast_basis_points(minimum, maximum), readable});
    }

    std::uint32_t next_finding_id = 1;
    const auto add_finding = [&](const LightingFindingCode code, const Severity severity,
                                 const std::optional<LightingSampleId> sample,
                                 const std::optional<ConnectionId> connection, const std::uint64_t observed,
                                 const std::uint64_t finding_threshold) {
        if (result.findings.size() >= limits.max_findings || next_finding_id == 0)
            fail("finding_limit", "lighting analysis finding count exceeds its configured cap");
        result.findings.push_back(
            {{next_finding_id++}, code, severity, sample, connection, observed, finding_threshold});
    };
    if (lights.empty())
        add_finding(LightingFindingCode::no_lights, Severity::error, std::nullopt, std::nullopt, 0, 1);
    for (const PendingSample &sample : pending_samples) {
        if (sample.metric.readable)
            continue;
        add_finding(sample.metric.critical ? LightingFindingCode::unlit_critical_placement
                                           : LightingFindingCode::unreadable_sample,
                    sample.metric.critical ? Severity::error : Severity::warning, sample.metric.id, std::nullopt,
                    sample.metric.brightness_milli, threshold);
    }
    for (const ConnectionLightingMetric &connection : result.connections) {
        if (!connection.readable) {
            add_finding(LightingFindingCode::unreadable_connection, Severity::warning, std::nullopt,
                        connection.connection, connection.minimum_brightness_milli, threshold);
        }
        if (connection.contrast_basis_points > thresholds.maximum_connection_contrast_basis_points) {
            add_finding(LightingFindingCode::excessive_connection_contrast, Severity::warning, std::nullopt,
                        connection.connection, connection.contrast_basis_points,
                        thresholds.maximum_connection_contrast_basis_points);
        }
    }

    LightingSummary &summary = result.summary;
    summary.light_count = static_cast<std::uint32_t>(lights.size());
    summary.sample_count = static_cast<std::uint32_t>(result.samples.size());
    summary.visibility_tests = visibility_tests;
    if (!result.samples.empty())
        summary.minimum_brightness_milli = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t brightness_sum = 0;
    for (const LightingSampleMetric &sample : result.samples) {
        summary.readable_samples += sample.readable ? 1U : 0U;
        summary.critical_samples += sample.critical ? 1U : 0U;
        summary.readable_critical_samples += sample.critical && sample.readable ? 1U : 0U;
        summary.minimum_brightness_milli = std::min(summary.minimum_brightness_milli, sample.brightness_milli);
        summary.maximum_brightness_milli = std::max(summary.maximum_brightness_milli, sample.brightness_milli);
        if (sample.brightness_milli > std::numeric_limits<std::uint64_t>::max() - brightness_sum)
            fail("brightness_overflow", "lighting summary brightness exceeds the deterministic integer range");
        brightness_sum += sample.brightness_milli;
    }
    if (!result.samples.empty())
        summary.average_brightness_milli = brightness_sum / result.samples.size();
    for (const ConnectionLightingMetric &connection : result.connections)
        summary.readable_connections += connection.readable ? 1U : 0U;
    for (const LightingFinding &finding : result.findings) {
        if (finding.severity == Severity::error)
            ++summary.error_findings;
        else
            ++summary.warning_findings;
    }
    return result;
}

} // namespace qmg
