#include "qmg/quality.hpp"

#include "qmg/entities.hpp"
#include "qmg/map_document_writer.hpp"
#include "qmg/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace qmg {
namespace {

constexpr std::size_t kMaximumTrustedRunnerIdBytes = 256;
constexpr std::size_t kMaximumEnvelopeBytes = 4096;
constexpr std::size_t kMaximumVerifierAuthenticationBytes = 16384;

[[nodiscard]] std::uint32_t narrow_count(const std::size_t value) {
    return static_cast<std::uint32_t>(
        std::min(value, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));
}

[[nodiscard]] bool is_spawn_role(const PlacementRole role) {
    return role == PlacementRole::player_start || role == PlacementRole::deathmatch_spawn ||
           role == PlacementRole::team_initial_spawn || role == PlacementRole::team_respawn;
}

[[nodiscard]] std::optional<QualityTimingTarget> timing_target(const PlacementRole role) {
    switch (role) {
    case PlacementRole::weapon:
        return QualityTimingTarget::weapon;
    case PlacementRole::health:
        return QualityTimingTarget::health;
    case PlacementRole::armor:
        return QualityTimingTarget::armor;
    case PlacementRole::monster:
        return QualityTimingTarget::monster;
    case PlacementRole::flag:
        return QualityTimingTarget::flag;
    case PlacementRole::exit_trigger:
        return QualityTimingTarget::exit;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] const TimingWindow &window_for(const QualityTimingThresholds &thresholds,
                                             const QualityTimingTarget target) {
    switch (target) {
    case QualityTimingTarget::weapon:
        return thresholds.weapon;
    case QualityTimingTarget::health:
        return thresholds.health;
    case QualityTimingTarget::armor:
        return thresholds.armor;
    case QualityTimingTarget::monster:
        return thresholds.monster;
    case QualityTimingTarget::flag:
        return thresholds.flag;
    case QualityTimingTarget::exit:
        return thresholds.exit;
    }
    return thresholds.weapon;
}

[[nodiscard]] bool critical_target(const QualityTimingTarget target) {
    return target == QualityTimingTarget::monster || target == QualityTimingTarget::flag ||
           target == QualityTimingTarget::exit;
}

[[nodiscard]] std::uint64_t arrival_milliseconds(const NavigationCost cost, const std::uint64_t cost_per_second) {
    if (cost == kUnreachableNavigationCost)
        return std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t seconds = cost / cost_per_second;
    const std::uint64_t remainder = cost % cost_per_second;
    if (seconds > std::numeric_limits<std::uint64_t>::max() / 1000U)
        return std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t whole = seconds * 1000U;
    const std::uint64_t fractional = (remainder * 1000U) / cost_per_second;
    return fractional > std::numeric_limits<std::uint64_t>::max() - whole ? std::numeric_limits<std::uint64_t>::max()
                                                                          : whole + fractional;
}

[[nodiscard]] std::size_t checked_product(const std::size_t first, const std::size_t second,
                                          const std::string_view code, const std::size_t limit) {
    if (first != 0 && second > limit / first)
        throw QualityError(std::string(code), "quality analysis pair cap exceeded");
    const std::size_t product = first * second;
    if (product > limit)
        throw QualityError(std::string(code), "quality analysis pair cap exceeded");
    return product;
}

[[nodiscard]] NavigationPath path_between(const NavigationAnalysis &analysis, const Placement &from,
                                          const Placement &to, const NavigationCost cost_per_unit,
                                          const std::size_t max_expansions) {
    const NavigationPlacementBinding *from_binding = analysis.find_binding(from.id);
    const NavigationPlacementBinding *to_binding = analysis.find_binding(to.id);
    if (from_binding == nullptr || to_binding == nullptr || !from_binding->node || !to_binding->node)
        return {};
    return navigation_path_between_positions(
        analysis.graph, *from_binding->node, from.volume ? from.volume->center() : from.origin, *to_binding->node,
        to.volume ? to.volume->center() : to.origin, cost_per_unit, max_expansions);
}

void validate_window(const TimingWindow &window) {
    if (window.minimum_milliseconds > window.maximum_milliseconds)
        throw QualityError("invalid_thresholds", "a quality timing window has its minimum after its maximum");
}

[[nodiscard]] bool exact_sha256(std::string_view value);

void validate_options(const QualityOptions &options) {
    if (options.limits.max_rooms == 0 || options.limits.max_connections == 0 || options.limits.max_solids == 0 ||
        options.limits.max_placements == 0 || options.limits.max_spawns == 0 || options.limits.max_targets == 0 ||
        options.limits.max_timing_pairs == 0 || options.limits.max_monster_pairs == 0 ||
        options.limits.max_path_expansions == 0 || options.limits.max_navigation_nodes == 0 ||
        options.limits.max_navigation_edges == 0 || options.limits.max_upstream_findings == 0 ||
        options.limits.max_findings == 0) {
        throw QualityError("invalid_limits", "quality analysis limits must all be positive");
    }
    if (options.timing.navigation_cost_per_second == 0 ||
        options.timing.navigation_cost_per_second > std::numeric_limits<std::uint64_t>::max() / 1000U) {
        throw QualityError("invalid_thresholds", "navigation cost per second is outside the bounded timing range");
    }
    validate_window(options.timing.weapon);
    validate_window(options.timing.health);
    validate_window(options.timing.armor);
    validate_window(options.timing.monster);
    validate_window(options.timing.flag);
    validate_window(options.timing.exit);
}

[[nodiscard]] std::uint32_t validation_error_count(const ValidationResult &validation) {
    return narrow_count(validation.error_count());
}

[[nodiscard]] std::uint32_t navigation_error_count(const NavigationAnalysis &analysis) {
    const auto count =
        std::count_if(analysis.findings.begin(), analysis.findings.end(), [](const NavigationFinding &finding) {
            return finding.severity == NavigationFindingSeverity::error;
        });
    return narrow_count(static_cast<std::size_t>(count));
}

[[nodiscard]] std::uint32_t navigation_warning_count(const NavigationAnalysis &analysis) {
    const auto count =
        std::count_if(analysis.findings.begin(), analysis.findings.end(), [](const NavigationFinding &finding) {
            return finding.severity == NavigationFindingSeverity::warning;
        });
    return narrow_count(static_cast<std::size_t>(count));
}

[[nodiscard]] std::uint32_t severity_count(const std::vector<SpawnSafetyFinding> &findings, const Severity severity) {
    const auto count = std::count_if(findings.begin(), findings.end(),
                                     [severity](const auto &finding) { return finding.severity == severity; });
    return narrow_count(static_cast<std::size_t>(count));
}

[[nodiscard]] std::uint32_t severity_count(const std::vector<LightingFinding> &findings, const Severity severity) {
    const auto count = std::count_if(findings.begin(), findings.end(),
                                     [severity](const auto &finding) { return finding.severity == severity; });
    return narrow_count(static_cast<std::size_t>(count));
}

[[nodiscard]] std::uint32_t severity_count(const std::vector<QualityFinding> &findings, const Severity severity) {
    const auto count = std::count_if(findings.begin(), findings.end(),
                                     [severity](const auto &finding) { return finding.severity == severity; });
    return narrow_count(static_cast<std::size_t>(count));
}

[[nodiscard]] std::uint32_t leak_error_count(const LeakDiagnosisResult *diagnosis) {
    if (diagnosis == nullptr || diagnosis->status != LeakDiagnosisStatus::diagnosed)
        return 0;
    return std::max<std::uint32_t>(1, narrow_count(diagnosis->exposed_crossing_count));
}

void validate_navigation_input(const Layout &layout, const NavigationAnalysis &analysis,
                               const NavigationAgentKind expected_kind, const QualityLimits &limits,
                               const std::string_view expected_layout_sha256) {
    if (analysis.profile.game != layout.spec.game || analysis.profile.kind != expected_kind)
        throw QualityError("analysis_mismatch", "navigation evidence has the wrong title or agent profile");
    if (!exact_sha256(analysis.layout_sha256) || analysis.layout_sha256 != expected_layout_sha256)
        throw QualityError("stale_analysis", "navigation evidence is not bound to the exact canonical layout");
    if (analysis.graph.nodes().size() > limits.max_navigation_nodes ||
        analysis.graph.edges().size() > limits.max_navigation_edges ||
        analysis.findings.size() > limits.max_upstream_findings) {
        throw QualityError("upstream_evidence_cap", "navigation evidence exceeds quality analysis caps");
    }
    std::set<PlacementId> bound;
    for (const NavigationPlacementBinding &binding : analysis.placement_bindings) {
        if (!binding.placement || find_placement(layout, binding.placement) == nullptr ||
            !bound.insert(binding.placement).second) {
            throw QualityError("analysis_mismatch", "navigation evidence has invalid placement bindings");
        }
    }
}

[[nodiscard]] bool navigation_analysis_equal(const NavigationAnalysis &left, const NavigationAnalysis &right) {
    return left.profile == right.profile && left.graph.nodes() == right.graph.nodes() &&
           left.graph.edges() == right.graph.edges() && left.placement_bindings == right.placement_bindings &&
           left.findings == right.findings && left.stats == right.stats && left.layout_sha256 == right.layout_sha256;
}

class StableHash {
  public:
    void byte(const std::uint8_t value) noexcept {
        value_ ^= value;
        value_ *= 1099511628211ULL;
    }

    template <typename Integer> void integer(const Integer value) noexcept {
        using Raw = std::remove_cv_t<Integer>;
        using Unsigned = std::make_unsigned_t<Raw>;
        const Unsigned bits = static_cast<Unsigned>(value);
        for (std::size_t shift = 0; shift < sizeof(Unsigned); ++shift)
            byte(static_cast<std::uint8_t>((bits >> (shift * 8U)) & static_cast<Unsigned>(0xffU)));
    }

    void text(const std::string_view value) noexcept {
        integer(value.size());
        for (const char character : value)
            byte(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
    }

    [[nodiscard]] std::uint64_t finish() const noexcept { return value_; }

  private:
    std::uint64_t value_{1469598103934665603ULL};
};

[[nodiscard]] std::vector<std::pair<PlacementId, PlacementRole>> placement_identities(const Layout &layout) {
    std::vector<std::pair<PlacementId, PlacementRole>> result;
    result.reserve(layout.placements.size());
    for (const Placement &placement : layout.placements)
        result.emplace_back(placement.id, placement.role);
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] bool no_new_structural_errors(const ValidationResult &baseline, const ValidationResult &candidate) {
    using IssueKey = std::pair<std::string, std::string>;
    std::map<IssueKey, std::size_t> allowed;
    for (const Issue &issue : baseline.issues) {
        if (issue.severity == Severity::error)
            ++allowed[{issue.code, issue.message}];
    }
    std::map<IssueKey, std::size_t> observed;
    for (const Issue &issue : candidate.issues) {
        if (issue.severity == Severity::error)
            ++observed[{issue.code, issue.message}];
    }
    return std::all_of(observed.begin(), observed.end(), [&](const auto &entry) {
        const auto found = allowed.find(entry.first);
        return found != allowed.end() && entry.second <= found->second;
    });
}

[[nodiscard]] MapDialect title_dialect(const Game game) {
    switch (game) {
    case Game::quake1:
        return MapDialect::quake1_valve220;
    case Game::quake2:
        return MapDialect::quake2_valve220;
    case Game::quake3:
        return MapDialect::quake3_legacy;
    }
    return MapDialect::quake1_valve220;
}

[[nodiscard]] bool dialect_matches_game(const Game game, const MapDialect dialect) noexcept {
    switch (game) {
    case Game::quake1:
        return dialect == MapDialect::quake1_valve220;
    case Game::quake2:
        return dialect == MapDialect::quake2_valve220;
    case Game::quake3:
        return dialect == MapDialect::quake3_legacy || dialect == MapDialect::quake3_brush_primitives;
    }
    return false;
}

[[nodiscard]] bool exact_sha256(const std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const char character) {
               return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
                      (character >= 'A' && character <= 'F');
           });
}

[[nodiscard]] CompilerPointfileDialect title_pointfile_dialect(const Game game) {
    switch (game) {
    case Game::quake1:
        return CompilerPointfileDialect::quake1;
    case Game::quake2:
        return CompilerPointfileDialect::quake2;
    case Game::quake3:
        return CompilerPointfileDialect::quake3;
    }
    return CompilerPointfileDialect::quake1;
}

void append_envelope_integer(std::string &encoded, const std::uint64_t value) {
    if (encoded.size() > kMaximumEnvelopeBytes - sizeof(value))
        throw std::length_error("compiler recheck envelope exceeds its canonical byte cap");
    for (std::size_t index = 0; index < sizeof(value); ++index)
        encoded.push_back(static_cast<char>((value >> (index * 8U)) & 0xffU));
}

void append_envelope_text(std::string &encoded, const std::string_view value) {
    if (value.size() > kMaximumEnvelopeBytes - sizeof(std::uint64_t) ||
        encoded.size() > kMaximumEnvelopeBytes - sizeof(std::uint64_t) - value.size()) {
        throw std::length_error("compiler recheck envelope exceeds its canonical byte cap");
    }
    append_envelope_integer(encoded, value.size());
    encoded.append(value);
}

[[nodiscard]] const PlacementTimingMetric *find_timing_metric(const QualityAnalysis &analysis,
                                                              const PlacementTimingMetric &wanted) {
    const auto found = std::find_if(analysis.placement_timing.begin(), analysis.placement_timing.end(),
                                    [&](const PlacementTimingMetric &metric) {
                                        return metric.spawn == wanted.spawn && metric.target == wanted.target &&
                                               metric.target_kind == wanted.target_kind;
                                    });
    return found == analysis.placement_timing.end() ? nullptr : &*found;
}

[[nodiscard]] const MonsterReachabilityMetric *find_monster_metric(const QualityAnalysis &analysis,
                                                                   const MonsterReachabilityMetric &wanted) {
    const auto found = std::find_if(analysis.monster_reachability.begin(), analysis.monster_reachability.end(),
                                    [&](const MonsterReachabilityMetric &metric) {
                                        return metric.agent_kind == wanted.agent_kind &&
                                               metric.monster == wanted.monster &&
                                               metric.player_spawn == wanted.player_spawn;
                                    });
    return found == analysis.monster_reachability.end() ? nullptr : &*found;
}

struct NavigationErrorIdentity {
    NavigationAgentKind agent_kind{NavigationAgentKind::player};
    NavigationFindingKind finding_kind{NavigationFindingKind::insufficient_clearance};
    std::optional<NavigationSourceRef> source;
    std::optional<NavigationSourceRef> related_source;
    std::string message;

    auto operator<=>(const NavigationErrorIdentity &) const = default;
};

void append_navigation_errors(std::vector<NavigationErrorIdentity> &errors, const NavigationAnalysis *analysis) {
    if (analysis == nullptr)
        return;
    for (const NavigationFinding &finding : analysis->findings) {
        if (finding.severity == NavigationFindingSeverity::error) {
            errors.push_back(
                {analysis->profile.kind, finding.kind, finding.source, finding.related_source, finding.message});
        }
    }
}

[[nodiscard]] std::vector<NavigationErrorIdentity> navigation_errors(const NavigationAnalysis &analysis) {
    std::vector<NavigationErrorIdentity> result;
    append_navigation_errors(result, &analysis);
    std::sort(result.begin(), result.end());
    return result;
}

[[nodiscard]] bool navigation_regressed(const QualityAnalysis &baseline, const QualityAnalysis &candidate,
                                        const QualityAnalysisInputs &baseline_inputs,
                                        const NavigationAnalysis &candidate_player,
                                        const NavigationAnalysis *candidate_monster_small,
                                        const NavigationAnalysis *candidate_monster_large) {
    if ((baseline_inputs.monster_small_navigation != nullptr) != (candidate_monster_small != nullptr) ||
        (baseline_inputs.monster_large_navigation != nullptr) != (candidate_monster_large != nullptr)) {
        return true;
    }
    for (const PlacementTimingMetric &metric : baseline.placement_timing) {
        if (!metric.reachable)
            continue;
        const PlacementTimingMetric *after = find_timing_metric(candidate, metric);
        if (after == nullptr || !after->reachable)
            return true;
    }
    for (const MonsterReachabilityMetric &metric : baseline.monster_reachability) {
        if (!metric.reachable)
            continue;
        const MonsterReachabilityMetric *after = find_monster_metric(candidate, metric);
        if (after == nullptr || !after->reachable)
            return true;
    }
    if (!navigation_error_findings_preserved(baseline_inputs.player_navigation, candidate_player))
        return true;
    if (candidate_monster_small != nullptr &&
        (baseline_inputs.monster_small_navigation == nullptr ||
         !navigation_error_findings_preserved(*baseline_inputs.monster_small_navigation, *candidate_monster_small))) {
        return true;
    }
    return candidate_monster_large != nullptr &&
           (baseline_inputs.monster_large_navigation == nullptr ||
            !navigation_error_findings_preserved(*baseline_inputs.monster_large_navigation, *candidate_monster_large));
}

struct ApplyResult {
    bool ok{};
    bool seal{};
    std::string message;
};

[[nodiscard]] ApplyResult apply_edit(Layout &candidate, const QualityRepairEdit &edit,
                                     const LeakDiagnosisLimits &leak_limits) {
    return std::visit(
        [&](const auto &typed) -> ApplyResult {
            using Edit = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Edit, AddOrAdjustLightRepair>) {
                if (typed.new_intensity <= 0 || !typed.new_color.valid())
                    return {false, false, "new light parameters are invalid"};
                if (typed.light) {
                    Placement *light = find_placement(candidate, *typed.light);
                    if (light == nullptr || light->role != PlacementRole::light ||
                        light->origin != typed.expected_origin || light->light_intensity != typed.expected_intensity ||
                        light->light_color != typed.expected_color)
                        return {false, false, "light precondition does not match the layout"};
                    light->origin = typed.new_origin;
                    light->light_intensity = typed.new_intensity;
                    light->light_color = typed.new_color;
                    return {true, false, {}};
                }
                if (!typed.expected_new_id || typed.expected_new_id != next_placement_id(candidate) ||
                    typed.expected_intensity || typed.expected_color)
                    return {false, false, "new-light identity precondition does not match the layout"};
                Placement light;
                light.id = typed.expected_new_id;
                light.role = PlacementRole::light;
                light.origin = typed.new_origin;
                light.light_intensity = typed.new_intensity;
                light.light_color = typed.new_color;
                candidate.placements.push_back(std::move(light));
                return {true, false, {}};
            } else if constexpr (std::is_same_v<Edit, MovePlacementRepair>) {
                Placement *placement = find_placement(candidate, typed.placement);
                if (placement == nullptr || placement->role != typed.expected_role ||
                    placement->origin != typed.expected_origin || placement->volume)
                    return {false, false, "placement move precondition does not match a point placement"};
                placement->origin = typed.new_origin;
                return {true, false, {}};
            } else if constexpr (std::is_same_v<Edit, ReplaceConnectionPassageRepair>) {
                const auto found =
                    std::find_if(candidate.connections.begin(), candidate.connections.end(),
                                 [&](const Connection &connection) { return connection.id == typed.connection; });
                if (found == candidate.connections.end() || found->from_room != typed.expected_from_room ||
                    found->to_room != typed.expected_to_room || found->width != typed.expected_width ||
                    found->height != typed.expected_height || found->passage_volumes != typed.expected_passage_volumes)
                    return {false, false, "connection passage precondition does not match the layout"};
                found->width = typed.new_width;
                found->height = typed.new_height;
                found->passage_volumes = typed.new_passage_volumes;
                return {true, false, {}};
            } else if constexpr (std::is_same_v<Edit, AddCoverSolidRepair>) {
                if (!typed.expected_new_id || typed.expected_new_id != next_solid_id(candidate) ||
                    !typed.bounds.valid() || typed.ownership == BrushOwnership::placement)
                    return {false, false, "cover-solid precondition or geometry is invalid"};
                LayoutSolid solid;
                solid.id = typed.expected_new_id;
                solid.geometry = typed.bounds;
                solid.material = typed.material;
                solid.purpose = typed.purpose;
                solid.ownership = typed.ownership;
                candidate.solids.push_back(std::move(solid));
                return {true, false, {}};
            } else {
                if (!typed.expected_new_id || typed.expected_new_id != next_solid_id(candidate) ||
                    !local_seal_proposal_preconditions_hold(candidate, typed.proposal, leak_limits))
                    return {false, true, "local-seal applicability or identity precondition failed"};
                LayoutSolid solid;
                solid.id = typed.expected_new_id;
                solid.geometry = typed.proposal.bounds;
                solid.material = typed.proposal.material;
                solid.purpose = typed.proposal.purpose;
                solid.ownership = typed.proposal.ownership;
                candidate.solids.push_back(std::move(solid));
                return {true, true, {}};
            }
        },
        edit);
}

} // namespace

QualityError::QualityError(std::string code, std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

bool quality_score_strictly_better(const QualityScore &candidate, const QualityScore &baseline) noexcept {
    return candidate < baseline;
}

bool navigation_error_findings_preserved(const NavigationAnalysis &baseline, const NavigationAnalysis &candidate) {
    const std::vector<NavigationErrorIdentity> baseline_errors = navigation_errors(baseline);
    const std::vector<NavigationErrorIdentity> candidate_errors = navigation_errors(candidate);
    return std::includes(baseline_errors.begin(), baseline_errors.end(), candidate_errors.begin(),
                         candidate_errors.end());
}

std::string compiler_recheck_envelope_sha256(const CompilerRecheckEvidence &evidence) {
    std::string encoded;
    encoded.reserve(1024);
    append_envelope_text(encoded, "qmg-compiler-recheck-envelope-v1");
    append_envelope_integer(encoded, evidence.schema_version);
    append_envelope_text(encoded, evidence.trusted_runner_id);
    append_envelope_integer(encoded, evidence.proposal.value);
    append_envelope_integer(encoded, evidence.baseline_layout_fingerprint);
    append_envelope_integer(encoded, evidence.candidate_layout_fingerprint);
    append_envelope_text(encoded, evidence.baseline_layout_sha256);
    append_envelope_text(encoded, evidence.candidate_layout_sha256);
    append_envelope_integer(encoded, static_cast<std::uint64_t>(evidence.map_dialect));
    append_envelope_text(encoded, evidence.source_map_sha256);
    append_envelope_integer(encoded, evidence.source_map_bytes);
    append_envelope_text(encoded, evidence.compiler_executable_sha256);
    append_envelope_text(encoded, evidence.compiler_options_sha256);
    append_envelope_text(encoded, evidence.compiler_invocation_sha256);
    append_envelope_text(encoded, evidence.output_bsp_sha256);
    append_envelope_integer(encoded, evidence.output_bsp_bytes);
    append_envelope_text(encoded, evidence.diagnostics_sha256);
    append_envelope_integer(encoded, static_cast<std::uint64_t>(evidence.pointfile_dialect));
    append_envelope_integer(encoded, evidence.witness_fingerprint);
    append_envelope_integer(encoded, evidence.compiler_succeeded ? 1U : 0U);
    append_envelope_integer(encoded, evidence.postcompile_pointfile_absent ? 1U : 0U);
    append_envelope_integer(encoded, static_cast<std::uint64_t>(evidence.leak_result));
    return sha256(encoded);
}

QualityAnalysis QualityAnalyzer::analyze(const Layout &layout, const QualityAnalysisInputs &inputs,
                                         const QualityOptions &options) const {
    validate_options(options);
    if (layout.rooms.size() > options.limits.max_rooms || layout.connections.size() > options.limits.max_connections ||
        layout.solids.size() > options.limits.max_solids || layout.placements.size() > options.limits.max_placements) {
        throw QualityError("input_cap", "quality analysis layout input cap exceeded");
    }
    if (inputs.visibility.findings.size() > options.limits.max_upstream_findings ||
        inputs.lighting.findings.size() > options.limits.max_upstream_findings) {
        throw QualityError("upstream_evidence_cap", "visibility or lighting evidence exceeds quality analysis caps");
    }
    const std::string layout_sha256 = canonical_layout_sha256(layout);
    if (!exact_sha256(layout_sha256) || inputs.visibility.layout_sha256 != layout_sha256 ||
        inputs.lighting.layout_sha256 != layout_sha256 || !exact_sha256(inputs.visibility.layout_sha256) ||
        !exact_sha256(inputs.lighting.layout_sha256)) {
        throw QualityError("stale_analysis", "visibility or lighting evidence is not bound to the exact layout");
    }
    if (inputs.leak_diagnosis != nullptr && (!exact_sha256(inputs.leak_diagnosis->layout_sha256) ||
                                             inputs.leak_diagnosis->layout_sha256 != layout_sha256)) {
        throw QualityError("stale_analysis", "leak diagnosis is not bound to the exact canonical layout");
    }
    if (layout.spec.game == Game::quake3 &&
        (inputs.monster_small_navigation != nullptr || inputs.monster_large_navigation != nullptr)) {
        throw QualityError("q3_monster_semantics",
                           "Quake 3 quality analysis cannot invent stock monster hull semantics");
    }
    validate_navigation_input(layout, inputs.player_navigation, NavigationAgentKind::player, options.limits,
                              layout_sha256);
    if (inputs.leak_diagnosis != nullptr &&
        (!inputs.leak_diagnosis->ok() || !inputs.leak_diagnosis->witness.has_value())) {
        throw QualityError("invalid_leak_evidence",
                           "a supplied pointfile diagnosis must prove a unique exposed crossing");
    }

    try {
        NavigationOptions player_options = options.player_navigation;
        player_options.agent_kind = NavigationAgentKind::player;
        const NavigationAnalysis expected_player = NavigationAnalyzer{}.analyze(layout, player_options);
        const VisibilityAnalysis expected_visibility = VisibilityAnalyzer{}.analyze(layout, options.visibility);
        const LightingAnalysis expected_lighting = LightingAnalyzer{}.analyze(layout, options.lighting);
        if (!navigation_analysis_equal(inputs.player_navigation, expected_player) ||
            inputs.visibility != expected_visibility || inputs.lighting != expected_lighting) {
            throw QualityError("analysis_content_mismatch",
                               "supplied analyzer payload differs from bounded independent recomputation");
        }
        if (inputs.leak_diagnosis != nullptr) {
            const LeakDiagnosisResult expected_leak =
                diagnose_layout_leak(layout, *inputs.leak_diagnosis->witness, options.leak);
            if (*inputs.leak_diagnosis != expected_leak) {
                throw QualityError("analysis_content_mismatch",
                                   "supplied leak diagnosis differs from bounded independent recomputation");
            }
        }
    } catch (const QualityError &) {
        throw;
    } catch (const std::exception &error) {
        throw QualityError("analysis_recheck_failed",
                           std::string("independent analyzer recomputation failed: ") + error.what());
    }

    std::vector<const Placement *> spawns;
    std::vector<std::pair<const Placement *, QualityTimingTarget>> targets;
    std::vector<const Placement *> monsters;
    for (const Placement &placement : layout.placements) {
        // Change r1s-mp-player-start: match timing is measured from the spawns a match uses; the map-command
        // info_player_start of a Quake 1 or Quake 2 multiplayer map is not one (src/entities.cpp).
        if (is_spawn_role(placement.role) && !is_multiplayer_map_start(layout, placement))
            spawns.push_back(&placement);
        if (const std::optional<QualityTimingTarget> target = timing_target(placement.role);
            target && !(layout.spec.game == Game::quake3 && *target == QualityTimingTarget::monster)) {
            targets.emplace_back(&placement, *target);
        }
        if (placement.role == PlacementRole::monster)
            monsters.push_back(&placement);
    }
    const auto placement_order = [](const Placement *first, const Placement *second) { return first->id < second->id; };
    std::sort(spawns.begin(), spawns.end(), placement_order);
    std::sort(monsters.begin(), monsters.end(), placement_order);
    std::sort(targets.begin(), targets.end(), [](const auto &first, const auto &second) {
        return std::tie(first.first->id, first.second) < std::tie(second.first->id, second.second);
    });
    if (spawns.size() > options.limits.max_spawns)
        throw QualityError("spawn_cap", "quality analysis spawn cap exceeded");
    if (targets.size() > options.limits.max_targets)
        throw QualityError("target_cap", "quality analysis target cap exceeded");
    const std::size_t timing_pair_count =
        checked_product(spawns.size(), targets.size(), "timing_pair_cap", options.limits.max_timing_pairs);

    QualityAnalysis result;
    result.layout_sha256 = layout_sha256;
    result.structural_validation = LayoutValidator{}.validate(layout);
    result.placement_timing.reserve(timing_pair_count);
    auto add_finding = [&](QualityFinding finding) {
        if (result.findings.size() >= options.limits.max_findings)
            throw QualityError("finding_cap", "quality analysis finding cap exceeded");
        finding.id = QualityFindingId{narrow_count(result.findings.size() + 1U)};
        result.findings.push_back(std::move(finding));
    };

    for (const Placement *spawn : spawns) {
        for (const auto &[target, target_kind] : targets) {
            const NavigationPath path =
                path_between(inputs.player_navigation, *spawn, *target, options.player_navigation.cost_per_unit,
                             options.limits.max_path_expansions);
            PlacementTimingMetric metric;
            metric.id = QualityTimingMetricId{narrow_count(result.placement_timing.size() + 1U)};
            metric.spawn = spawn->id;
            metric.target = target->id;
            metric.target_kind = target_kind;
            metric.navigation_cost = path.cost;
            metric.reachable = path.reachable();
            metric.arrival_milliseconds =
                metric.reachable ? arrival_milliseconds(path.cost, options.timing.navigation_cost_per_second)
                                 : std::numeric_limits<std::uint64_t>::max();
            result.placement_timing.push_back(metric);
            ++result.summary.path_queries;
            const TimingWindow &window = window_for(options.timing, target_kind);
            if (!metric.reachable) {
                add_finding({{},
                             QualityFindingCode::unreachable_timing_target,
                             critical_target(target_kind) ? Severity::error : Severity::warning,
                             spawn->id,
                             target->id,
                             target_kind,
                             std::nullopt,
                             0,
                             window.maximum_milliseconds});
            } else if (metric.arrival_milliseconds < window.minimum_milliseconds) {
                add_finding({{},
                             QualityFindingCode::timing_too_early,
                             Severity::warning,
                             spawn->id,
                             target->id,
                             target_kind,
                             std::nullopt,
                             metric.arrival_milliseconds,
                             window.minimum_milliseconds});
            } else if (metric.arrival_milliseconds > window.maximum_milliseconds) {
                add_finding({{},
                             QualityFindingCode::timing_too_late,
                             critical_target(target_kind) ? Severity::error : Severity::warning,
                             spawn->id,
                             target->id,
                             target_kind,
                             std::nullopt,
                             metric.arrival_milliseconds,
                             window.maximum_milliseconds});
            }
        }
    }

    if (layout.spec.game != Game::quake3) {
        const bool has_small = inputs.monster_small_navigation != nullptr;
        const bool has_large = inputs.monster_large_navigation != nullptr;
        if (has_small != has_large || (!monsters.empty() && !has_small))
            throw QualityError("missing_monster_evidence",
                               "Q1/Q2 monsters require small and large navigation evidence");
        if (has_small) {
            validate_navigation_input(layout, *inputs.monster_small_navigation, NavigationAgentKind::monster_small,
                                      options.limits, layout_sha256);
            validate_navigation_input(layout, *inputs.monster_large_navigation, NavigationAgentKind::monster_large,
                                      options.limits, layout_sha256);
            try {
                NavigationOptions small_options = options.player_navigation;
                small_options.agent_kind = NavigationAgentKind::monster_small;
                small_options.profile_override.reset();
                const NavigationAnalysis expected_small = NavigationAnalyzer{}.analyze(layout, small_options);
                NavigationOptions large_options = small_options;
                large_options.agent_kind = NavigationAgentKind::monster_large;
                const NavigationAnalysis expected_large = NavigationAnalyzer{}.analyze(layout, large_options);
                if (!navigation_analysis_equal(*inputs.monster_small_navigation, expected_small) ||
                    !navigation_analysis_equal(*inputs.monster_large_navigation, expected_large)) {
                    throw QualityError("analysis_content_mismatch",
                                       "supplied monster navigation differs from bounded independent recomputation");
                }
            } catch (const QualityError &) {
                throw;
            } catch (const std::exception &error) {
                throw QualityError("analysis_recheck_failed",
                                   std::string("independent monster navigation recomputation failed: ") + error.what());
            }
        }
        if (!monsters.empty()) {
            const std::size_t single_profile_pairs =
                checked_product(monsters.size(), spawns.size(), "monster_pair_cap", options.limits.max_monster_pairs);
            if (single_profile_pairs > options.limits.max_monster_pairs / 2U)
                throw QualityError("monster_pair_cap", "quality analysis monster pair cap exceeded");
            result.monster_reachability.reserve(single_profile_pairs * 2U);
            for (const auto &[kind, analysis] : std::vector<std::pair<NavigationAgentKind, const NavigationAnalysis *>>{
                     {NavigationAgentKind::monster_small, inputs.monster_small_navigation},
                     {NavigationAgentKind::monster_large, inputs.monster_large_navigation}}) {
                for (const Placement *monster : monsters) {
                    for (const Placement *spawn : spawns) {
                        const NavigationPath path =
                            path_between(*analysis, *monster, *spawn, options.player_navigation.cost_per_unit,
                                         options.limits.max_path_expansions);
                        MonsterReachabilityMetric metric;
                        metric.id = MonsterReachabilityMetricId{narrow_count(result.monster_reachability.size() + 1U)};
                        metric.agent_kind = kind;
                        metric.monster = monster->id;
                        metric.player_spawn = spawn->id;
                        metric.navigation_cost = path.cost;
                        metric.reachable = path.reachable();
                        result.monster_reachability.push_back(metric);
                        ++result.summary.path_queries;
                        if (!metric.reachable) {
                            add_finding(
                                {{},
                                 QualityFindingCode::monster_cannot_reach_player,
                                 kind == NavigationAgentKind::monster_small ? Severity::error : Severity::warning,
                                 spawn->id,
                                 monster->id,
                                 QualityTimingTarget::monster,
                                 kind,
                                 0,
                                 1});
                        }
                    }
                }
            }
        }
    }

    result.summary.leak_diagnosis_evaluated = inputs.leak_diagnosis != nullptr;
    result.summary.spawn_count = narrow_count(spawns.size());
    result.summary.timing_target_count = narrow_count(targets.size());
    result.summary.timing_pair_count = narrow_count(result.placement_timing.size());
    const auto reachable_timing = std::count_if(result.placement_timing.begin(), result.placement_timing.end(),
                                                [](const PlacementTimingMetric &metric) { return metric.reachable; });
    result.summary.reachable_timing_pairs = narrow_count(static_cast<std::size_t>(reachable_timing));
    result.summary.monster_pair_count = narrow_count(result.monster_reachability.size());
    const auto reachable_monsters =
        std::count_if(result.monster_reachability.begin(), result.monster_reachability.end(),
                      [](const MonsterReachabilityMetric &metric) { return metric.reachable; });
    result.summary.reachable_monster_pairs = narrow_count(static_cast<std::size_t>(reachable_monsters));
    result.summary.warning_findings = severity_count(result.findings, Severity::warning);
    result.summary.error_findings = severity_count(result.findings, Severity::error);

    result.score.structural_errors = validation_error_count(result.structural_validation);
    result.score.leak_errors = leak_error_count(inputs.leak_diagnosis);
    result.score.navigation_errors = navigation_error_count(inputs.player_navigation);
    result.score.warnings = navigation_warning_count(inputs.player_navigation);
    if (inputs.monster_small_navigation != nullptr) {
        result.score.navigation_errors += navigation_error_count(*inputs.monster_small_navigation);
        result.score.warnings += navigation_warning_count(*inputs.monster_small_navigation);
    }
    if (inputs.monster_large_navigation != nullptr) {
        result.score.navigation_errors += navigation_error_count(*inputs.monster_large_navigation);
        result.score.warnings += navigation_warning_count(*inputs.monster_large_navigation);
    }
    result.score.visibility_errors = severity_count(inputs.visibility.findings, Severity::error);
    result.score.lighting_errors = severity_count(inputs.lighting.findings, Severity::error);
    result.score.timing_errors = result.summary.error_findings;
    result.score.warnings += severity_count(inputs.visibility.findings, Severity::warning);
    result.score.warnings += severity_count(inputs.lighting.findings, Severity::warning);
    result.score.warnings += result.summary.warning_findings;
    return result;
}

std::uint64_t quality_layout_fingerprint(const Layout &layout) {
    StableHash hash;
    hash.text("qmg-quality-layout-v2");
    hash.text(canonical_layout_sha256(layout));
    return hash.finish();
}

QualityRepairResult
QualityRepairEngine::evaluate(const Layout &layout, const QualityAnalysisInputs &baseline_inputs,
                              const QualityRepairProposal &proposal, const QualityRepairOptions &options,
                              const std::optional<CompilerRecheckEvidence> &compiler_evidence) const {
    QualityRepairResult result;
    result.baseline_layout_fingerprint = quality_layout_fingerprint(layout);
    if (!proposal.id || proposal.expected_layout_fingerprint != result.baseline_layout_fingerprint) {
        result.rejection = QualityRepairRejection::stale_proposal;
        result.message = "repair proposal fingerprint is stale or its ID is invalid";
        return result;
    }

    QualityAnalysis baseline;
    try {
        baseline = QualityAnalyzer{}.analyze(layout, baseline_inputs, options.quality);
    } catch (const std::exception &error) {
        result.rejection = QualityRepairRejection::analysis_failed;
        result.message = std::string("baseline quality analysis failed: ") + error.what();
        return result;
    }
    result.before_score = baseline.score;
    result.baseline_layout_sha256 = canonical_layout_sha256(layout);

    Layout candidate = layout;
    ApplyResult applied;
    try {
        applied = apply_edit(candidate, proposal.edit, options.quality.leak);
    } catch (const std::exception &error) {
        applied = {false, std::holds_alternative<LocalSealRepair>(proposal.edit), error.what()};
    }
    if (!applied.ok) {
        result.rejection = QualityRepairRejection::precondition_failed;
        result.message = applied.message;
        return result;
    }
    result.candidate_layout_fingerprint = quality_layout_fingerprint(candidate);
    result.candidate_layout_sha256 = canonical_layout_sha256(candidate);

    const ValidationResult candidate_validation = LayoutValidator{}.validate(candidate);
    if (!no_new_structural_errors(baseline.structural_validation, candidate_validation)) {
        result.rejection = QualityRepairRejection::new_structural_error;
        result.message = "candidate introduces a structural validation error";
        return result;
    }
    const std::vector<std::pair<PlacementId, PlacementRole>> before_identities = placement_identities(layout);
    const std::vector<std::pair<PlacementId, PlacementRole>> after_identities = placement_identities(candidate);
    if (!std::includes(after_identities.begin(), after_identities.end(), before_identities.begin(),
                       before_identities.end())) {
        result.rejection = QualityRepairRejection::placement_identity_regression;
        result.message = "candidate loses or changes an existing placement identity";
        return result;
    }

    NavigationAnalysis player_navigation;
    VisibilityAnalysis visibility;
    LightingAnalysis lighting;
    std::optional<NavigationAnalysis> small_navigation;
    std::optional<NavigationAnalysis> large_navigation;
    QualityAnalysis candidate_quality;
    try {
        NavigationOptions player_options = options.quality.player_navigation;
        player_options.agent_kind = NavigationAgentKind::player;
        player_navigation = NavigationAnalyzer{}.analyze(candidate, player_options);
        visibility = VisibilityAnalyzer{}.analyze(candidate, options.quality.visibility);
        lighting = LightingAnalyzer{}.analyze(candidate, options.quality.lighting);
        const bool has_monsters =
            std::any_of(candidate.placements.begin(), candidate.placements.end(),
                        [](const Placement &placement) { return placement.role == PlacementRole::monster; });
        const bool preserve_monster_scope =
            baseline_inputs.monster_small_navigation != nullptr || baseline_inputs.monster_large_navigation != nullptr;
        if (candidate.spec.game != Game::quake3 && (has_monsters || preserve_monster_scope)) {
            NavigationOptions small_options = options.quality.player_navigation;
            small_options.agent_kind = NavigationAgentKind::monster_small;
            small_options.profile_override.reset();
            small_navigation = NavigationAnalyzer{}.analyze(candidate, small_options);
            NavigationOptions large_options = small_options;
            large_options.agent_kind = NavigationAgentKind::monster_large;
            large_navigation = NavigationAnalyzer{}.analyze(candidate, large_options);
        }
        const QualityAnalysisInputs candidate_inputs{player_navigation,
                                                     visibility,
                                                     lighting,
                                                     small_navigation ? &*small_navigation : nullptr,
                                                     large_navigation ? &*large_navigation : nullptr,
                                                     nullptr};
        candidate_quality = QualityAnalyzer{}.analyze(candidate, candidate_inputs, options.quality);
        candidate_quality.score.leak_errors = baseline.score.leak_errors;
    } catch (const std::exception &error) {
        result.rejection = QualityRepairRejection::analysis_failed;
        result.message = std::string("candidate quality analysis failed: ") + error.what();
        return result;
    }
    result.after_score = candidate_quality.score;
    if (navigation_regressed(baseline, candidate_quality, baseline_inputs, player_navigation,
                             small_navigation ? &*small_navigation : nullptr,
                             large_navigation ? &*large_navigation : nullptr)) {
        result.rejection = QualityRepairRejection::navigation_regression;
        result.message = "candidate loses established reachability or adds a critical navigation finding";
        return result;
    }

    const MapDialect lowered_dialect = options.dialect.value_or(title_dialect(candidate.spec.game));
    if (!dialect_matches_game(candidate.spec.game, lowered_dialect)) {
        result.rejection = QualityRepairRejection::lowering_failed;
        result.message = "candidate repair dialect is incompatible with its target game";
        return result;
    }
    try {
        MapBuildOptions map_options = options.lowering;
        map_options.dialect = lowered_dialect;
        const MapBuildResult lowered = options.construction != nullptr
                                           ? MapBuilder{}.build(candidate, map_options, *options.construction)
                                           : MapBuilder{}.build(candidate, map_options);
        const std::string canonical_map = MapDocumentWriter{}.write(lowered.document);
        result.candidate_map_dialect = lowered_dialect;
        result.candidate_source_map_sha256 = sha256(canonical_map);
        result.candidate_source_map_bytes = static_cast<std::uint64_t>(canonical_map.size());
    } catch (const std::exception &error) {
        result.rejection = QualityRepairRejection::lowering_failed;
        result.message = std::string("candidate map lowering failed: ") + error.what();
        return result;
    }

    if (applied.seal) {
        const LocalSealRepair &seal = std::get<LocalSealRepair>(proposal.edit);
        const bool matching_diagnosis = baseline_inputs.leak_diagnosis != nullptr &&
                                        baseline_inputs.leak_diagnosis->ok() &&
                                        baseline_inputs.leak_diagnosis->proposal == seal.proposal;
        if (!matching_diagnosis) {
            result.rejection = QualityRepairRejection::precondition_failed;
            result.message = "seal proposal is not backed by the baseline leak diagnosis";
            return result;
        }
        if (!compiler_evidence) {
            result.rejection = QualityRepairRejection::compiler_recheck_required;
            result.message = "a local seal requires compiler evidence bound to the evaluated candidate";
            return result;
        }
        const CompilerRecheckEvidence &evidence = *compiler_evidence;
        const CompilerPointfileDialect pointfile_dialect = title_pointfile_dialect(candidate.spec.game);
        const CompilerRecheckRequirements *requirements =
            options.compiler_recheck ? &*options.compiler_recheck : nullptr;
        const bool valid_requirements = requirements != nullptr && !requirements->trusted_runner_id.empty() &&
                                        requirements->trusted_runner_id.size() <= kMaximumTrustedRunnerIdBytes &&
                                        requirements->map_dialect == lowered_dialect &&
                                        requirements->pointfile_dialect == pointfile_dialect &&
                                        exact_sha256(requirements->compiler_executable_sha256) &&
                                        exact_sha256(requirements->compiler_options_sha256) &&
                                        exact_sha256(requirements->compiler_invocation_sha256);
        const bool complete_envelope = evidence.schema_version == 1 && requirements != nullptr &&
                                       evidence.trusted_runner_id == requirements->trusted_runner_id &&
                                       evidence.proposal == proposal.id &&
                                       evidence.baseline_layout_fingerprint == result.baseline_layout_fingerprint &&
                                       evidence.candidate_layout_fingerprint == result.candidate_layout_fingerprint;
        const bool matching_artifacts =
            valid_requirements && complete_envelope &&
            evidence.baseline_layout_sha256 == result.baseline_layout_sha256 &&
            evidence.candidate_layout_sha256 == result.candidate_layout_sha256 &&
            evidence.map_dialect == lowered_dialect &&
            evidence.source_map_sha256 == result.candidate_source_map_sha256 &&
            evidence.source_map_bytes == result.candidate_source_map_bytes && requirements != nullptr &&
            evidence.compiler_executable_sha256 == requirements->compiler_executable_sha256 &&
            evidence.compiler_options_sha256 == requirements->compiler_options_sha256 &&
            evidence.compiler_invocation_sha256 == requirements->compiler_invocation_sha256 &&
            exact_sha256(evidence.output_bsp_sha256) && evidence.output_bsp_bytes > 0 &&
            exact_sha256(evidence.diagnostics_sha256) && evidence.pointfile_dialect == pointfile_dialect &&
            evidence.witness_fingerprint == seal.proposal.applicability.witness_fingerprint &&
            evidence.compiler_succeeded && evidence.postcompile_pointfile_absent &&
            evidence.leak_result == CompilerLeakResult::leak_free && exact_sha256(evidence.envelope_sha256) &&
            evidence.envelope_sha256 == compiler_recheck_envelope_sha256(evidence) &&
            evidence.verifier_authentication.size() <= kMaximumVerifierAuthenticationBytes;
        bool runner_authenticated = false;
        if (matching_artifacts && options.compiler_recheck_verifier != nullptr) {
            try {
                runner_authenticated =
                    options.compiler_recheck_verifier->authenticate(evidence, evidence.envelope_sha256);
            } catch (...) {
                runner_authenticated = false;
            }
        }
        if (!matching_artifacts || !runner_authenticated) {
            result.rejection = QualityRepairRejection::compiler_evidence_mismatch;
            result.message =
                "compiler evidence is incomplete, unauthenticated, unsuccessful, or bound to another candidate";
            return result;
        }
        candidate_quality.score.leak_errors = 0;
        result.after_score = candidate_quality.score;
    }

    if (!quality_score_strictly_better(candidate_quality.score, baseline.score)) {
        result.rejection = QualityRepairRejection::not_strictly_improved;
        result.message = "candidate does not strictly improve the lexicographic quality score";
        return result;
    }
    result.rejection = QualityRepairRejection::none;
    result.message = "candidate accepted after all quality and regression gates";
    result.accepted_layout = std::move(candidate);
    result.accepted_quality = std::move(candidate_quality);
    return result;
}

} // namespace qmg
