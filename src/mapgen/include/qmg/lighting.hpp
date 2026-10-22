#pragma once

#include "qmg/visibility.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qmg {

enum class LightingSampleKind : std::uint8_t {
    room,
    passage,
    gameplay_placement,
};

struct LightingSampleIdTag;
using LightingSampleId = StrongLayoutId<LightingSampleIdTag>;

struct LightingSampleMetric {
    LightingSampleId id;
    LightingSampleKind kind{LightingSampleKind::room};
    std::uint32_t source_id{};
    std::uint32_t source_ordinal{};
    GridVec3 position;
    std::uint64_t brightness_milli{};
    std::uint32_t visible_lights{};
    std::uint32_t occluded_lights{};
    std::uint32_t outside_lights{};
    std::uint32_t conservative_traces{};
    bool critical{};
    bool readable{};

    auto operator<=>(const LightingSampleMetric &) const = default;
};

struct ConnectionLightingMetric {
    ConnectionId connection;
    LightingSampleId from_room_sample;
    LightingSampleId to_room_sample;
    std::vector<LightingSampleId> passage_samples;
    std::uint64_t minimum_brightness_milli{};
    std::uint64_t maximum_brightness_milli{};
    std::uint32_t contrast_basis_points{};
    bool readable{};

    auto operator<=>(const ConnectionLightingMetric &) const = default;
};

enum class LightingFindingCode : std::uint8_t {
    no_lights,
    unreadable_sample,
    unlit_critical_placement,
    unreadable_connection,
    excessive_connection_contrast,
};

struct LightingFindingIdTag;
using LightingFindingId = StrongLayoutId<LightingFindingIdTag>;

struct LightingFinding {
    LightingFindingId id;
    LightingFindingCode code{LightingFindingCode::unreadable_sample};
    Severity severity{Severity::warning};
    std::optional<LightingSampleId> sample;
    std::optional<ConnectionId> connection;
    std::uint64_t observed{};
    std::uint64_t threshold{};

    auto operator<=>(const LightingFinding &) const = default;
};

struct LightingSummary {
    std::uint32_t light_count{};
    std::uint32_t sample_count{};
    std::uint32_t readable_samples{};
    std::uint32_t critical_samples{};
    std::uint32_t readable_critical_samples{};
    std::uint32_t readable_connections{};
    std::uint32_t warning_findings{};
    std::uint32_t error_findings{};
    std::uint64_t visibility_tests{};
    std::uint64_t minimum_brightness_milli{};
    std::uint64_t maximum_brightness_milli{};
    std::uint64_t average_brightness_milli{};

    auto operator<=>(const LightingSummary &) const = default;
};

struct LightingAnalysis {
    std::vector<LightingSampleMetric> samples;
    std::vector<ConnectionLightingMetric> connections;
    std::vector<LightingFinding> findings;
    LightingSummary summary;
    std::string layout_sha256;

    bool operator==(const LightingAnalysis &) const = default;
};

struct LightingLimits {
    GridCoord max_abs_coordinate{32768};
    std::size_t max_rooms{256};
    std::size_t max_connections{512};
    std::size_t max_passage_volumes{4096};
    std::size_t max_placements{2048};
    std::size_t max_samples{8192};
    std::size_t max_lights{2048};
    std::size_t max_visibility_tests{4'000'000};
    std::size_t max_findings{65536};
};

struct LightingThresholds {
    GridCoord eye_height{48};
    std::uint64_t minimum_distance_squared{4096};
    std::uint64_t contribution_scale{1000};
    std::uint64_t minimum_readable_brightness_milli{500};
    std::uint64_t maximum_brightness_milli{1'000'000'000};
    std::uint32_t maximum_connection_contrast_basis_points{80'000};
    std::uint32_t minimum_visible_lights{1};
};

struct LightingOptions {
    LightingLimits limits;
    LightingThresholds thresholds;
    VisibilityOptions visibility;
};

class LightingError final : public std::runtime_error {
  public:
    LightingError(std::string code, std::string message);

    [[nodiscard]] const std::string &code() const noexcept { return code_; }

  private:
    std::string code_;
};

class LightingAnalyzer {
  public:
    // Brightness is a deterministic project metric, not physical lux. Each
    // visible typed light contributes intensity * perceptual color luminance
    // with inverse-square attenuation and an explicit near-distance clamp.
    [[nodiscard]] LightingAnalysis analyze(const Layout &layout, const LightingOptions &options = {}) const;
};

} // namespace qmg
