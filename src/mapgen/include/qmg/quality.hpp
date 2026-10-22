#pragma once

#include "qmg/layout_fingerprint.hpp"
#include "qmg/leak.hpp"
#include "qmg/lighting.hpp"
#include "qmg/map_builder.hpp"
#include "qmg/navigation.hpp"
#include "qmg/validator.hpp"
#include "qmg/visibility.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qmg {

enum class QualityTimingTarget : std::uint8_t {
    weapon,
    health,
    armor,
    monster,
    flag,
    exit,
};

struct QualityTimingMetricIdTag;
using QualityTimingMetricId = StrongLayoutId<QualityTimingMetricIdTag>;

struct PlacementTimingMetric {
    QualityTimingMetricId id;
    PlacementId spawn;
    PlacementId target;
    QualityTimingTarget target_kind{QualityTimingTarget::weapon};
    // Includes local XY approach costs; see navigation_path_between_positions.
    NavigationCost navigation_cost{kUnreachableNavigationCost};
    // Constant-rate conversion of a planner estimate, not measured engine time.
    std::uint64_t arrival_milliseconds{};
    bool reachable{};

    auto operator<=>(const PlacementTimingMetric &) const = default;
};

struct MonsterReachabilityMetricIdTag;
using MonsterReachabilityMetricId = StrongLayoutId<MonsterReachabilityMetricIdTag>;

struct MonsterReachabilityMetric {
    MonsterReachabilityMetricId id;
    NavigationAgentKind agent_kind{NavigationAgentKind::monster_small};
    PlacementId monster;
    PlacementId player_spawn;
    NavigationCost navigation_cost{kUnreachableNavigationCost};
    bool reachable{};

    auto operator<=>(const MonsterReachabilityMetric &) const = default;
};

enum class QualityFindingCode : std::uint8_t {
    unreachable_timing_target,
    timing_too_early,
    timing_too_late,
    monster_cannot_reach_player,
};

struct QualityFindingIdTag;
using QualityFindingId = StrongLayoutId<QualityFindingIdTag>;

struct QualityFinding {
    QualityFindingId id;
    QualityFindingCode code{QualityFindingCode::unreachable_timing_target};
    Severity severity{Severity::warning};
    std::optional<PlacementId> spawn;
    std::optional<PlacementId> target;
    std::optional<QualityTimingTarget> target_kind;
    std::optional<NavigationAgentKind> agent_kind;
    std::uint64_t observed{};
    std::uint64_t threshold{};

    auto operator<=>(const QualityFinding &) const = default;
};

struct TimingWindow {
    std::uint64_t minimum_milliseconds{};
    std::uint64_t maximum_milliseconds{120'000};

    auto operator<=>(const TimingWindow &) const = default;
};

struct QualityTimingThresholds {
    TimingWindow weapon{0, 20'000};
    TimingWindow health{0, 30'000};
    TimingWindow armor{0, 45'000};
    TimingWindow monster{500, 60'000};
    TimingWindow flag{1'000, 120'000};
    TimingWindow exit{5'000, 180'000};
    // Navigation costs are deterministic planner units, including local endpoint
    // approaches and heuristic transition penalties. With the default 1,000
    // cost units per map unit, 320,000 cost units model one second at 320 u/s.
    // This conversion does not simulate acceleration, jumps, or mover waits.
    std::uint64_t navigation_cost_per_second{320'000};
};

struct QualityLimits {
    std::size_t max_rooms{256};
    std::size_t max_connections{512};
    std::size_t max_solids{8192};
    std::size_t max_placements{2048};
    std::size_t max_spawns{256};
    std::size_t max_targets{2048};
    std::size_t max_timing_pairs{262144};
    std::size_t max_monster_pairs{262144};
    std::size_t max_path_expansions{131072};
    std::size_t max_navigation_nodes{32768};
    std::size_t max_navigation_edges{131072};
    std::size_t max_upstream_findings{65536};
    std::size_t max_findings{65536};
};

struct QualityOptions {
    QualityLimits limits;
    QualityTimingThresholds timing;
    // Quality independently reruns every supplied analyzer under this exact
    // policy before consuming its mutable payload.
    NavigationOptions player_navigation;
    VisibilityOptions visibility;
    LightingOptions lighting;
    LeakDiagnosisLimits leak;
};

struct QualityScore {
    std::uint32_t structural_errors{};
    std::uint32_t leak_errors{};
    std::uint32_t navigation_errors{};
    std::uint32_t visibility_errors{};
    std::uint32_t lighting_errors{};
    std::uint32_t timing_errors{};
    std::uint32_t warnings{};

    auto operator<=>(const QualityScore &) const = default;
};

[[nodiscard]] bool quality_score_strictly_better(const QualityScore &candidate, const QualityScore &baseline) noexcept;

// Repair regression policy: every candidate navigation error occurrence must
// have an identical baseline occurrence; equal aggregate counts are not enough.
[[nodiscard]] bool navigation_error_findings_preserved(const NavigationAnalysis &baseline,
                                                       const NavigationAnalysis &candidate);

struct QualitySummary {
    bool leak_diagnosis_evaluated{};
    std::uint32_t spawn_count{};
    std::uint32_t timing_target_count{};
    std::uint32_t timing_pair_count{};
    std::uint32_t reachable_timing_pairs{};
    std::uint32_t monster_pair_count{};
    std::uint32_t reachable_monster_pairs{};
    std::uint32_t warning_findings{};
    std::uint32_t error_findings{};
    std::uint64_t path_queries{};

    auto operator<=>(const QualitySummary &) const = default;
};

struct QualityAnalysisInputs {
    const NavigationAnalysis &player_navigation;
    const VisibilityAnalysis &visibility;
    const LightingAnalysis &lighting;
    const NavigationAnalysis *monster_small_navigation{};
    const NavigationAnalysis *monster_large_navigation{};
    const LeakDiagnosisResult *leak_diagnosis{};
};

struct QualityAnalysis {
    std::vector<PlacementTimingMetric> placement_timing;
    std::vector<MonsterReachabilityMetric> monster_reachability;
    std::vector<QualityFinding> findings;
    ValidationResult structural_validation;
    QualitySummary summary;
    QualityScore score;
    std::string layout_sha256;
};

class QualityError final : public std::runtime_error {
  public:
    QualityError(std::string code, std::string message);

    [[nodiscard]] const std::string &code() const noexcept { return code_; }

  private:
    std::string code_;
};

class QualityAnalyzer {
  public:
    [[nodiscard]] QualityAnalysis analyze(const Layout &layout, const QualityAnalysisInputs &inputs,
                                          const QualityOptions &options = {}) const;
};

// The fingerprint is canonical over stable IDs and all fields that can affect
// validation, analysis, or lowering. Reordering top-level Layout vectors does
// not change it.
[[nodiscard]] std::uint64_t quality_layout_fingerprint(const Layout &layout);

struct QualityRepairProposalIdTag;
using QualityRepairProposalId = StrongLayoutId<QualityRepairProposalIdTag>;

struct AddOrAdjustLightRepair {
    // Null adds a new light with expected_new_id. Otherwise all expected
    // fields must exactly match the existing light before it is adjusted.
    std::optional<PlacementId> light;
    PlacementId expected_new_id;
    GridVec3 expected_origin;
    std::optional<std::int32_t> expected_intensity;
    std::optional<RgbColor> expected_color;
    GridVec3 new_origin;
    std::int32_t new_intensity{300};
    RgbColor new_color{1000, 1000, 1000};

    auto operator<=>(const AddOrAdjustLightRepair &) const = default;
};

struct MovePlacementRepair {
    PlacementId placement;
    PlacementRole expected_role{PlacementRole::health};
    GridVec3 expected_origin;
    GridVec3 new_origin;

    auto operator<=>(const MovePlacementRepair &) const = default;
};

struct ReplaceConnectionPassageRepair {
    ConnectionId connection;
    RoomId expected_from_room;
    RoomId expected_to_room;
    GridCoord expected_width{};
    GridCoord expected_height{};
    std::vector<GridBounds> expected_passage_volumes;
    GridCoord new_width{};
    GridCoord new_height{};
    std::vector<GridBounds> new_passage_volumes;

    bool operator==(const ReplaceConnectionPassageRepair &) const = default;
};

struct AddCoverSolidRepair {
    SolidId expected_new_id;
    GridBounds bounds;
    MaterialRole material{MaterialRole::wall};
    BrushPurpose purpose{BrushPurpose::structural};
    BrushOwnership ownership{BrushOwnership::detail};

    auto operator<=>(const AddCoverSolidRepair &) const = default;
};

struct LocalSealRepair {
    SolidId expected_new_id;
    LocalSealProposal proposal;

    bool operator==(const LocalSealRepair &) const = default;
};

using QualityRepairEdit = std::variant<AddOrAdjustLightRepair, MovePlacementRepair, ReplaceConnectionPassageRepair,
                                       AddCoverSolidRepair, LocalSealRepair>;

struct QualityRepairProposal {
    QualityRepairProposalId id;
    std::uint64_t expected_layout_fingerprint{};
    QualityRepairEdit edit;

    bool operator==(const QualityRepairProposal &) const = default;
};

enum class CompilerLeakResult : std::uint8_t {
    not_checked,
    leak_free,
    leaked,
};

struct CompilerRecheckEvidence {
    std::uint32_t schema_version{1};
    std::string trusted_runner_id;
    QualityRepairProposalId proposal;
    std::uint64_t baseline_layout_fingerprint{};
    std::uint64_t candidate_layout_fingerprint{};
    std::string baseline_layout_sha256;
    std::string candidate_layout_sha256;
    MapDialect map_dialect{MapDialect::quake1_valve220};
    std::string source_map_sha256;
    std::uint64_t source_map_bytes{};
    std::string compiler_executable_sha256;
    std::string compiler_options_sha256;
    std::string compiler_invocation_sha256;
    std::string output_bsp_sha256;
    std::uint64_t output_bsp_bytes{};
    std::string diagnostics_sha256;
    CompilerPointfileDialect pointfile_dialect{CompilerPointfileDialect::quake1};
    std::uint64_t witness_fingerprint{};
    bool compiler_succeeded{};
    bool postcompile_pointfile_absent{};
    CompilerLeakResult leak_result{CompilerLeakResult::not_checked};
    // Unkeyed integrity digest over every preceding envelope field in its
    // canonical length-prefixed encoding. This is not runner authentication.
    std::string envelope_sha256;
    // Opaque signature, trusted-IPC token, or other host-verifier material.
    // It is interpreted only by the configured CompilerRecheckVerifier.
    std::string verifier_authentication;

    bool operator==(const CompilerRecheckEvidence &) const = default;
};

[[nodiscard]] std::string compiler_recheck_envelope_sha256(const CompilerRecheckEvidence &evidence);

class CompilerRecheckVerifier {
  public:
    virtual ~CompilerRecheckVerifier() = default;

    // Core performs all local field/equality checks first. The host verifier
    // must then authenticate the canonical envelope using an out-of-process
    // trust mechanism; exceptions are caught and rejected by the core.
    [[nodiscard]] virtual bool authenticate(const CompilerRecheckEvidence &evidence,
                                            std::string_view canonical_envelope_sha256) const = 0;
};

struct CompilerRecheckRequirements {
    std::string trusted_runner_id;
    MapDialect map_dialect{MapDialect::quake1_valve220};
    CompilerPointfileDialect pointfile_dialect{CompilerPointfileDialect::quake1};
    std::string compiler_executable_sha256;
    std::string compiler_options_sha256;
    std::string compiler_invocation_sha256;

    bool operator==(const CompilerRecheckRequirements &) const = default;
};

enum class QualityRepairRejection : std::uint8_t {
    none,
    stale_proposal,
    precondition_failed,
    analysis_failed,
    new_structural_error,
    placement_identity_regression,
    navigation_regression,
    lowering_failed,
    compiler_recheck_required,
    compiler_evidence_mismatch,
    not_strictly_improved,
};

struct QualityRepairOptions {
    QualityOptions quality;
    // Defaults to the title's canonical dialect; Q3 callers can explicitly
    // bind repair lowering and compiler evidence to brush primitives.
    std::optional<MapDialect> dialect;
    MapBuildOptions lowering;
    const ConstructionResult *construction{};
    std::optional<CompilerRecheckRequirements> compiler_recheck;
    const CompilerRecheckVerifier *compiler_recheck_verifier{};
};

struct QualityRepairResult {
    QualityRepairRejection rejection{QualityRepairRejection::analysis_failed};
    std::string message;
    QualityScore before_score;
    QualityScore after_score;
    std::uint64_t baseline_layout_fingerprint{};
    std::uint64_t candidate_layout_fingerprint{};
    std::string baseline_layout_sha256;
    std::string candidate_layout_sha256;
    std::optional<MapDialect> candidate_map_dialect;
    std::string candidate_source_map_sha256;
    std::uint64_t candidate_source_map_bytes{};
    std::optional<Layout> accepted_layout;
    std::optional<QualityAnalysis> accepted_quality;

    [[nodiscard]] bool accepted() const noexcept {
        return rejection == QualityRepairRejection::none && accepted_layout.has_value() && accepted_quality.has_value();
    }
};

class QualityRepairEngine {
  public:
    // Candidate analysis is performed on a copy. A result contains a Layout
    // only after every regression gate and the strict score comparison pass.
    // Seal proposals deliberately return the evaluated candidate fingerprint
    // when compiler evidence is absent, allowing a compiler runner to bind its
    // subsequent evidence to that exact candidate.
    [[nodiscard]] QualityRepairResult
    evaluate(const Layout &layout, const QualityAnalysisInputs &baseline_inputs, const QualityRepairProposal &proposal,
             const QualityRepairOptions &options = {},
             const std::optional<CompilerRecheckEvidence> &compiler_evidence = std::nullopt) const;
};

} // namespace qmg
