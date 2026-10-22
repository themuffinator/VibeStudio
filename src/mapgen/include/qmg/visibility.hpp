#pragma once

#include "qmg/layout.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace qmg {

enum class SegmentVisibilityKind : std::uint8_t {
    visible,
    occluded,
    outside_free_space,
};

enum class VisibilityPrecision : std::uint8_t {
    exact,
    conservative,
};

struct SegmentVisibilityResult {
    SegmentVisibilityKind kind{SegmentVisibilityKind::outside_free_space};
    VisibilityPrecision precision{VisibilityPrecision::exact};
    std::optional<SolidId> blocker;

    auto operator<=>(const SegmentVisibilityResult &) const = default;
};

struct SegmentVisibilityQuery {
    GridVec3 from;
    GridVec3 to;

    auto operator<=>(const SegmentVisibilityQuery &) const = default;
};

struct PlacementLineOfSight {
    PlacementId first;
    PlacementId second;
    std::uint64_t distance_squared{};
    SegmentVisibilityResult visibility;
    // Actual sampled endpoints. Q2 player spawns use standing eyes; other
    // placement roles and Q1/Q3 retain their explicitly reported origin policy.
    GridVec3 first_sample;
    GridVec3 second_sample;

    auto operator<=>(const PlacementLineOfSight &) const = default;
};

struct RoomExposureMetric {
    RoomId room;
    GridVec3 observation_point;
    std::uint32_t visible_rooms{};
    std::uint32_t tested_rooms{};
    std::uint32_t visible_gameplay_placements{};
    std::uint32_t tested_gameplay_placements{};
    std::uint32_t exposure_basis_points{};

    auto operator<=>(const RoomExposureMetric &) const = default;
};

enum class CoverDirection : std::uint8_t {
    negative_x,
    positive_x,
    negative_y,
    positive_y,
};

struct CoverOpportunity {
    RoomId room;
    SolidId occluder;
    CoverDirection sheltered_side{CoverDirection::negative_x};
    GridVec3 sheltered_point;
    GridVec3 opposite_point;
    VisibilityPrecision precision{VisibilityPrecision::exact};

    auto operator<=>(const CoverOpportunity &) const = default;
};

struct CrossfireAngleMetric {
    PlacementId focus;
    PlacementId first_observer;
    PlacementId second_observer;
    // Deterministic integer CORDIC result in thousandths of a degree.
    std::uint32_t angle_millidegrees{};
    bool qualifies{};

    auto operator<=>(const CrossfireAngleMetric &) const = default;
};

struct RoomTopologyMetric {
    RoomId room;
    std::uint32_t component{};
    std::uint32_t graph_degree{};
    std::uint32_t readable_connections{};
    std::uint32_t connection_count{};
    std::uint32_t readability_basis_points{};
    bool articulation{};

    auto operator<=>(const RoomTopologyMetric &) const = default;
};

struct ConnectionReadabilityMetric {
    ConnectionId connection;
    bool bridge{};
    bool narrow{};
    bool chokepoint{};
    SegmentVisibilityResult centerline_visibility;

    auto operator<=>(const ConnectionReadabilityMetric &) const = default;
};

struct SpawnSafetyMetric {
    PlacementId spawn;
    std::uint32_t hostile_origins{};
    std::uint32_t visible_hostile_origins{};
    std::uint32_t close_hostile_origins{};
    std::uint32_t qualifying_crossfires{};
    std::uint32_t room_cover_opportunities{};
    bool safe{};

    auto operator<=>(const SpawnSafetyMetric &) const = default;
};

enum class SpawnSafetyCode : std::uint8_t {
    missing_required_spawn,
    hostile_line_of_sight,
    insufficient_separation,
    wide_crossfire,
    no_cover_opportunity,
};

struct VisibilityFindingIdTag;
using VisibilityFindingId = StrongLayoutId<VisibilityFindingIdTag>;

struct SpawnSafetyFinding {
    VisibilityFindingId id;
    SpawnSafetyCode code{SpawnSafetyCode::hostile_line_of_sight};
    Severity severity{Severity::warning};
    std::optional<PlacementId> spawn;
    std::optional<PlacementId> threat;
    std::optional<PlacementId> secondary_threat;
    std::optional<PlacementRole> required_role;
    std::optional<Team> required_team;
    std::uint64_t observed{};
    std::uint64_t threshold{};

    auto operator<=>(const SpawnSafetyFinding &) const = default;
};

struct VisibilitySummary {
    std::uint32_t room_count{};
    std::uint32_t placement_count{};
    std::uint32_t connected_components{};
    std::uint32_t articulation_rooms{};
    std::uint32_t chokepoint_connections{};
    std::uint32_t readable_connections{};
    std::uint32_t evaluated_spawns{};
    std::uint32_t safe_spawns{};
    std::uint32_t warning_findings{};
    std::uint32_t error_findings{};
    std::uint64_t segment_tests{};

    auto operator<=>(const VisibilitySummary &) const = default;
};

struct VisibilityAnalysis {
    std::vector<PlacementLineOfSight> placement_lines_of_sight;
    std::vector<RoomExposureMetric> room_exposure;
    std::vector<CoverOpportunity> cover_opportunities;
    std::vector<CrossfireAngleMetric> crossfire_angles;
    std::vector<RoomTopologyMetric> room_topology;
    std::vector<ConnectionReadabilityMetric> connection_readability;
    std::vector<SpawnSafetyMetric> spawn_safety;
    std::vector<SpawnSafetyFinding> findings;
    VisibilitySummary summary;
    std::string layout_sha256;
    GridCoord player_spawn_eye_offset_z{};

    bool operator==(const VisibilityAnalysis &) const = default;
};

struct VisibilityLimits {
    GridCoord max_abs_coordinate{32768};
    std::size_t max_rooms{256};
    std::size_t max_connections{512};
    std::size_t max_free_volumes{8192};
    std::size_t max_solids{8192};
    std::size_t max_placements{2048};
    std::size_t max_segment_tests{4'000'000};
    std::size_t max_crossfire_records{1'000'000};
    std::size_t max_cover_opportunities{16384};
    std::size_t max_findings{65536};
};

struct VisibilityThresholds {
    GridCoord chokepoint_width{192};
    GridCoord cover_clearance{16};
    GridCoord cover_eye_height{48};
    GridCoord minimum_spawn_separation{256};
    std::uint32_t minimum_crossfire_angle_millidegrees{60'000};
    bool report_missing_cover{true};
};

struct VisibilityOptions {
    VisibilityLimits limits;
    VisibilityThresholds thresholds;
};

class VisibilityError final : public std::runtime_error {
  public:
    VisibilityError(std::string code, std::string message);

    [[nodiscard]] const std::string &code() const noexcept { return code_; }

  private:
    std::string code_;
};

class VisibilityAnalyzer {
  public:
    // Free-space coverage is exact for the union of room/passage boxes. Box
    // solids are exact occluders. Convex solids use their validated aggregate
    // bounds and therefore produce an explicitly conservative occlusion.
    [[nodiscard]] SegmentVisibilityResult classify_segment(const Layout &layout, GridVec3 from, GridVec3 to,
                                                           const VisibilityOptions &options = {}) const;
    // Validates and indexes the layout once for the complete query batch.
    [[nodiscard]] std::vector<SegmentVisibilityResult>
    classify_segments(const Layout &layout, const std::vector<SegmentVisibilityQuery> &queries,
                      const VisibilityOptions &options = {}) const;
    [[nodiscard]] VisibilityAnalysis analyze(const Layout &layout, const VisibilityOptions &options = {}) const;
};

} // namespace qmg
