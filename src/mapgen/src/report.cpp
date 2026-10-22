#include "qmg/report.hpp"

#include "qmg/layout_fingerprint.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace qmg {
namespace {

std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character)
                       << std::dec;
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    return output.str();
}

void quote(std::ostream &output, const std::string_view value) { output << '"' << json_escape(value) << '"'; }

[[nodiscard]] std::string_view format_name(const MapDialect dialect) {
    switch (dialect) {
    case MapDialect::quake1_valve220:
        return "q1-valve220";
    case MapDialect::quake2_valve220:
        return "q2-valve220";
    case MapDialect::quake3_legacy:
        return "q3-legacy";
    case MapDialect::quake3_brush_primitives:
        return "q3-brush-primitives";
    }
    throw std::invalid_argument("unknown map dialect");
}

[[nodiscard]] std::string_view navigation_agent_name(const NavigationAgentKind kind) {
    switch (kind) {
    case NavigationAgentKind::player:
        return "player";
    case NavigationAgentKind::monster_small:
        return "monster_small";
    case NavigationAgentKind::monster_large:
        return "monster_large";
    }
    throw std::invalid_argument("unknown navigation agent kind");
}

[[nodiscard]] std::string_view navigation_source_name(const NavigationSourceKind kind) {
    switch (kind) {
    case NavigationSourceKind::room:
        return "room";
    case NavigationSourceKind::connection:
        return "connection";
    case NavigationSourceKind::solid:
        return "solid";
    case NavigationSourceKind::placement:
        return "placement";
    }
    throw std::invalid_argument("unknown navigation source kind");
}

[[nodiscard]] std::string_view navigation_transition_name(const NavigationTransitionKind kind) {
    switch (kind) {
    case NavigationTransitionKind::walk:
        return "walk";
    case NavigationTransitionKind::step:
        return "step";
    case NavigationTransitionKind::jump:
        return "jump";
    case NavigationTransitionKind::drop:
        return "drop";
    case NavigationTransitionKind::door:
        return "door";
    case NavigationTransitionKind::lift:
        return "lift";
    case NavigationTransitionKind::teleporter:
        return "teleporter";
    }
    throw std::invalid_argument("unknown navigation transition kind");
}

[[nodiscard]] std::string_view navigation_finding_severity_name(const NavigationFindingSeverity severity) {
    switch (severity) {
    case NavigationFindingSeverity::warning:
        return "warning";
    case NavigationFindingSeverity::error:
        return "error";
    }
    throw std::invalid_argument("unknown navigation finding severity");
}

[[nodiscard]] std::string_view navigation_finding_name(const NavigationFindingKind kind) {
    switch (kind) {
    case NavigationFindingKind::insufficient_clearance:
        return "insufficient_clearance";
    case NavigationFindingKind::invalid_source_geometry:
        return "invalid_source_geometry";
    case NavigationFindingKind::missing_passage_geometry:
        return "missing_passage_geometry";
    case NavigationFindingKind::unbound_placement:
        return "unbound_placement";
    case NavigationFindingKind::unresolved_transition:
        return "unresolved_transition";
    case NavigationFindingKind::missing_spawn:
        return "missing_spawn";
    case NavigationFindingKind::unreachable_spawn:
        return "unreachable_spawn";
    case NavigationFindingKind::unreachable_objective:
        return "unreachable_objective";
    case NavigationFindingKind::unreachable_monster:
        return "unreachable_monster";
    }
    throw std::invalid_argument("unknown navigation finding kind");
}

[[nodiscard]] std::string_view spawn_safety_finding_name(const SpawnSafetyCode code) {
    switch (code) {
    case SpawnSafetyCode::missing_required_spawn:
        return "missing_required_spawn";
    case SpawnSafetyCode::hostile_line_of_sight:
        return "hostile_line_of_sight";
    case SpawnSafetyCode::insufficient_separation:
        return "insufficient_separation";
    case SpawnSafetyCode::wide_crossfire:
        return "wide_crossfire";
    case SpawnSafetyCode::no_cover_opportunity:
        return "no_cover_opportunity";
    }
    throw std::invalid_argument("unknown spawn safety finding code");
}

[[nodiscard]] std::string_view lighting_finding_name(const LightingFindingCode code) {
    switch (code) {
    case LightingFindingCode::no_lights:
        return "no_lights";
    case LightingFindingCode::unreadable_sample:
        return "unreadable_sample";
    case LightingFindingCode::unlit_critical_placement:
        return "unlit_critical_placement";
    case LightingFindingCode::unreadable_connection:
        return "unreadable_connection";
    case LightingFindingCode::excessive_connection_contrast:
        return "excessive_connection_contrast";
    }
    throw std::invalid_argument("unknown lighting finding code");
}

[[nodiscard]] std::string_view quality_finding_name(const QualityFindingCode code) {
    switch (code) {
    case QualityFindingCode::unreachable_timing_target:
        return "unreachable_timing_target";
    case QualityFindingCode::timing_too_early:
        return "timing_too_early";
    case QualityFindingCode::timing_too_late:
        return "timing_too_late";
    case QualityFindingCode::monster_cannot_reach_player:
        return "monster_cannot_reach_player";
    }
    throw std::invalid_argument("unknown quality finding code");
}

[[nodiscard]] std::string_view quality_timing_target_name(const QualityTimingTarget target) {
    switch (target) {
    case QualityTimingTarget::weapon:
        return "weapon";
    case QualityTimingTarget::health:
        return "health";
    case QualityTimingTarget::armor:
        return "armor";
    case QualityTimingTarget::monster:
        return "monster";
    case QualityTimingTarget::flag:
        return "flag";
    case QualityTimingTarget::exit:
        return "exit";
    }
    throw std::invalid_argument("unknown quality timing target");
}

[[nodiscard]] std::string_view placement_role_name(const PlacementRole role) {
    switch (role) {
    case PlacementRole::player_start:
        return "player_start";
    case PlacementRole::deathmatch_spawn:
        return "deathmatch_spawn";
    case PlacementRole::team_initial_spawn:
        return "team_initial_spawn";
    case PlacementRole::team_respawn:
        return "team_respawn";
    case PlacementRole::weapon:
        return "weapon";
    case PlacementRole::health:
        return "health";
    case PlacementRole::armor:
        return "armor";
    case PlacementRole::monster:
        return "monster";
    case PlacementRole::flag:
        return "flag";
    case PlacementRole::light:
        return "light";
    case PlacementRole::exit_trigger:
        return "exit_trigger";
    case PlacementRole::exit_destination:
        return "exit_destination";
    case PlacementRole::door:
        return "door";
    case PlacementRole::door_trigger:
        return "door_trigger";
    case PlacementRole::door_destination:
        return "door_destination";
    case PlacementRole::lift:
        return "lift";
    case PlacementRole::lift_trigger:
        return "lift_trigger";
    case PlacementRole::lift_destination:
        return "lift_destination";
    case PlacementRole::teleporter_trigger:
        return "teleporter_trigger";
    case PlacementRole::teleporter_destination:
        return "teleporter_destination";
    case PlacementRole::intermission:
        return "intermission";
    case PlacementRole::coop_start:
        return "coop_start";
    }
    throw std::invalid_argument("unknown placement role");
}

[[nodiscard]] std::string_view team_name(const Team team) {
    switch (team) {
    case Team::red:
        return "red";
    case Team::blue:
        return "blue";
    }
    throw std::invalid_argument("unknown team");
}

void write_navigation_source(std::ostream &output, const std::optional<NavigationSourceRef> &source) {
    if (!source) {
        output << "null";
        return;
    }
    output << "{\"kind\": ";
    quote(output, navigation_source_name(source->kind));
    output << ", \"id\": " << source->id << ", \"subindex\": " << source->subindex << '}';
}

template <typename Id> void write_optional_id(std::ostream &output, const std::optional<Id> &id) {
    if (id)
        output << id->value;
    else
        output << "null";
}

void write_grid_point(std::ostream &output, const GridVec3 point) {
    output << '[' << point.x << ',' << point.y << ',' << point.z << ']';
}

void write_navigation_area(std::ostream &output, const NavigationArea &area) {
    output << "{\"min_x\": " << area.min_x << ", \"min_y\": " << area.min_y << ", \"max_x\": " << area.max_x
           << ", \"max_y\": " << area.max_y << ", \"floor_z\": " << area.floor_z
           << ", \"ceiling_z\": " << area.ceiling_z << ", \"slope\": ";
    if (area.slope) {
        const NavigationSlope &slope = *area.slope;
        output << "{\"along_x\": " << (slope.along_x ? "true" : "false") << ", \"start\": " << slope.start
               << ", \"end\": " << slope.end << ", \"low_z\": " << slope.low_z << ", \"rise\": " << slope.rise
               << ", \"support_offset\": " << slope.support_offset << '}';
    } else {
        output << "null";
    }
    output << '}';
}

void write_navigation_graph(std::ostream &output, const NavigationAnalysis &analysis) {
    output << "{\"surface_height_basis\": \"ceil(low_z + rise * (axis - start + support_offset) / (end - start)); "
              "flat areas use floor_z\", \"slope_bounds_basis\": \"floor_z is minimum supported feet; "
              "ceiling_z is the certified standing-envelope top\", \"cost_basis\": "
              "\"Manhattan node-anchor proxy; ramp edges include end-gate approaches; not engine time\", \"nodes\": [";
    for (std::size_t index = 0; index < analysis.graph.nodes().size(); ++index) {
        if (index > 0)
            output << ',';
        const NavigationNode &node = analysis.graph.nodes()[index];
        output << "{\"id\": " << node.id.value << ", \"source\": ";
        write_navigation_source(output, node.source);
        output << ", \"fragment_index\": " << node.fragment_index << ", \"area\": ";
        write_navigation_area(output, node.area);
        output << ", \"origin\": ";
        write_grid_point(output, node.origin);
        output << '}';
    }
    output << "], \"edges\": [";
    for (std::size_t index = 0; index < analysis.graph.edges().size(); ++index) {
        if (index > 0)
            output << ',';
        const NavigationEdge &edge = analysis.graph.edges()[index];
        output << "{\"id\": " << edge.id.value << ", \"from\": " << edge.from.value << ", \"to\": " << edge.to.value
               << ", \"kind\": ";
        quote(output, navigation_transition_name(edge.kind));
        // Preserve the complete uint64 cost in readers whose numbers are doubles.
        output << ", \"cost\": \"" << edge.cost << "\", \"source\": ";
        write_navigation_source(output, edge.source);
        output << '}';
    }
    output << "], \"placement_bindings\": [";
    for (std::size_t index = 0; index < analysis.placement_bindings.size(); ++index) {
        if (index > 0)
            output << ',';
        const NavigationPlacementBinding &binding = analysis.placement_bindings[index];
        output << "{\"placement\": " << binding.placement.value << ", \"node\": ";
        write_optional_id(output, binding.node);
        output << '}';
    }
    output << "]}";
}

void write_navigation_analysis(std::ostream &output, const std::optional<NavigationAnalysis> &analysis) {
    output << "{\"available\": " << (analysis ? "true" : "false");
    if (!analysis) {
        output << '}';
        return;
    }

    output << ", \"layout_sha256\": ";
    quote(output, analysis->layout_sha256);
    output << ", \"agent\": ";
    quote(output, navigation_agent_name(analysis->profile.kind));
    output << ", \"profile\": {\"game\": ";
    quote(output, to_string(analysis->profile.game));
    output << ", \"hull_mins\": ";
    write_grid_point(output, analysis->profile.hull_mins);
    output << ", \"hull_maxs\": ";
    write_grid_point(output, analysis->profile.hull_maxs);
    output << ", \"max_step_up\": " << analysis->profile.max_step_up
           << ", \"max_jump_up\": " << analysis->profile.max_jump_up
           << ", \"max_jump_gap\": " << analysis->profile.max_jump_gap
           << ", \"max_drop\": " << analysis->profile.max_drop << "}, \"graph\": ";
    write_navigation_graph(output, *analysis);
    output << ", \"ok\": " << (analysis->ok() ? "true" : "false") << ", \"summary\": {"
           << "\"source_volumes\": " << analysis->stats.source_volumes
           << ", \"rejected_source_volumes\": " << analysis->stats.rejected_source_volumes
           << ", \"evaluated_grid_cells\": " << analysis->stats.evaluated_grid_cells
           << ", \"evaluated_node_pairs\": " << analysis->stats.evaluated_node_pairs
           << ", \"nodes\": " << analysis->stats.node_count << ", \"edges\": " << analysis->stats.edge_count
           << ", \"placement_bindings\": " << analysis->stats.placement_bindings
           << ", \"findings\": " << analysis->findings.size() << "}, \"findings\": [";
    for (std::size_t index = 0; index < analysis->findings.size(); ++index) {
        if (index > 0)
            output << ',';
        const NavigationFinding &finding = analysis->findings[index];
        output << "{\"severity\": ";
        quote(output, navigation_finding_severity_name(finding.severity));
        output << ", \"code\": ";
        quote(output, navigation_finding_name(finding.kind));
        output << ", \"source\": ";
        write_navigation_source(output, finding.source);
        output << ", \"related_source\": ";
        write_navigation_source(output, finding.related_source);
        output << ", \"message\": ";
        quote(output, finding.message);
        output << '}';
    }
    output << "]}";
}

void write_visibility_analysis(std::ostream &output, const std::optional<VisibilityAnalysis> &analysis) {
    output << "{\"available\": " << (analysis ? "true" : "false");
    if (!analysis) {
        output << '}';
        return;
    }

    const VisibilitySummary &summary = analysis->summary;
    output << ", \"layout_sha256\": ";
    quote(output, analysis->layout_sha256);
    output << ", \"sampling\": {\"player_spawn_eye_offset_z\": " << analysis->player_spawn_eye_offset_z
           << ", \"other_placement_offset_z\": 0, \"distance_basis\": \"entity_origins\"}";
    output << ", \"summary\": {\"rooms\": " << summary.room_count << ", \"placements\": " << summary.placement_count
           << ", \"connected_components\": " << summary.connected_components
           << ", \"articulation_rooms\": " << summary.articulation_rooms
           << ", \"chokepoint_connections\": " << summary.chokepoint_connections
           << ", \"readable_connections\": " << summary.readable_connections
           << ", \"evaluated_spawns\": " << summary.evaluated_spawns << ", \"safe_spawns\": " << summary.safe_spawns
           << ", \"warning_findings\": " << summary.warning_findings
           << ", \"error_findings\": " << summary.error_findings << ", \"segment_tests\": " << summary.segment_tests
           << "}, \"findings\": [";
    for (std::size_t index = 0; index < analysis->findings.size(); ++index) {
        if (index > 0)
            output << ',';
        const SpawnSafetyFinding &finding = analysis->findings[index];
        output << "{\"id\": " << finding.id.value << ", \"severity\": ";
        quote(output, to_string(finding.severity));
        output << ", \"code\": ";
        quote(output, spawn_safety_finding_name(finding.code));
        output << ", \"spawn\": ";
        write_optional_id(output, finding.spawn);
        output << ", \"threat\": ";
        write_optional_id(output, finding.threat);
        output << ", \"secondary_threat\": ";
        write_optional_id(output, finding.secondary_threat);
        output << ", \"required_role\": ";
        if (finding.required_role)
            quote(output, placement_role_name(*finding.required_role));
        else
            output << "null";
        output << ", \"required_team\": ";
        if (finding.required_team)
            quote(output, team_name(*finding.required_team));
        else
            output << "null";
        output << ", \"observed\": " << finding.observed << ", \"threshold\": " << finding.threshold << '}';
    }
    output << "]}";
}

void write_lighting_analysis(std::ostream &output, const std::optional<LightingAnalysis> &analysis) {
    output << "{\"available\": " << (analysis ? "true" : "false");
    if (!analysis) {
        output << '}';
        return;
    }

    const LightingSummary &summary = analysis->summary;
    output << ", \"layout_sha256\": ";
    quote(output, analysis->layout_sha256);
    output << ", \"summary\": {\"lights\": " << summary.light_count << ", \"samples\": " << summary.sample_count
           << ", \"readable_samples\": " << summary.readable_samples
           << ", \"critical_samples\": " << summary.critical_samples
           << ", \"readable_critical_samples\": " << summary.readable_critical_samples
           << ", \"readable_connections\": " << summary.readable_connections
           << ", \"warning_findings\": " << summary.warning_findings
           << ", \"error_findings\": " << summary.error_findings
           << ", \"visibility_tests\": " << summary.visibility_tests
           << ", \"minimum_brightness_milli\": " << summary.minimum_brightness_milli
           << ", \"maximum_brightness_milli\": " << summary.maximum_brightness_milli
           << ", \"average_brightness_milli\": " << summary.average_brightness_milli << "}, \"findings\": [";
    for (std::size_t index = 0; index < analysis->findings.size(); ++index) {
        if (index > 0)
            output << ',';
        const LightingFinding &finding = analysis->findings[index];
        output << "{\"id\": " << finding.id.value << ", \"severity\": ";
        quote(output, to_string(finding.severity));
        output << ", \"code\": ";
        quote(output, lighting_finding_name(finding.code));
        output << ", \"sample\": ";
        write_optional_id(output, finding.sample);
        output << ", \"connection\": ";
        write_optional_id(output, finding.connection);
        output << ", \"observed\": " << finding.observed << ", \"threshold\": " << finding.threshold << '}';
    }
    output << "]}";
}

void write_quality_analysis(std::ostream &output, const std::optional<QualityAnalysis> &analysis) {
    output << "{\"available\": " << (analysis ? "true" : "false");
    if (!analysis) {
        output << '}';
        return;
    }

    const QualitySummary &summary = analysis->summary;
    const QualityScore &score = analysis->score;
    output << ", \"layout_sha256\": ";
    quote(output, analysis->layout_sha256);
    output << ", \"structural_validation_ok\": " << (analysis->structural_validation.ok() ? "true" : "false")
           << ", \"summary\": {\"leak_diagnosis_evaluated\": " << (summary.leak_diagnosis_evaluated ? "true" : "false")
           << ", \"spawns\": " << summary.spawn_count << ", \"timing_targets\": " << summary.timing_target_count
           << ", \"timing_pairs\": " << summary.timing_pair_count
           << ", \"reachable_timing_pairs\": " << summary.reachable_timing_pairs
           << ", \"monster_pairs\": " << summary.monster_pair_count
           << ", \"reachable_monster_pairs\": " << summary.reachable_monster_pairs
           << ", \"warning_findings\": " << summary.warning_findings
           << ", \"error_findings\": " << summary.error_findings << ", \"path_queries\": " << summary.path_queries
           << "}, \"score\": {\"structural_errors\": " << score.structural_errors
           << ", \"leak_errors\": " << score.leak_errors << ", \"navigation_errors\": " << score.navigation_errors
           << ", \"visibility_errors\": " << score.visibility_errors
           << ", \"lighting_errors\": " << score.lighting_errors << ", \"timing_errors\": " << score.timing_errors
           << ", \"warnings\": " << score.warnings << "}, \"findings\": [";
    for (std::size_t index = 0; index < analysis->findings.size(); ++index) {
        if (index > 0)
            output << ',';
        const QualityFinding &finding = analysis->findings[index];
        output << "{\"id\": " << finding.id.value << ", \"severity\": ";
        quote(output, to_string(finding.severity));
        output << ", \"code\": ";
        quote(output, quality_finding_name(finding.code));
        output << ", \"spawn\": ";
        write_optional_id(output, finding.spawn);
        output << ", \"target\": ";
        write_optional_id(output, finding.target);
        output << ", \"target_kind\": ";
        if (finding.target_kind)
            quote(output, quality_timing_target_name(*finding.target_kind));
        else
            output << "null";
        output << ", \"agent\": ";
        if (finding.agent_kind)
            quote(output, navigation_agent_name(*finding.agent_kind));
        else
            output << "null";
        output << ", \"observed\": " << finding.observed << ", \"threshold\": " << finding.threshold << '}';
    }
    output << "]}";
}

[[nodiscard]] bool validation_equal(const ValidationResult &left, const ValidationResult &right) {
    if (left.issues.size() != right.issues.size())
        return false;
    for (std::size_t index = 0; index < left.issues.size(); ++index) {
        const Issue &left_issue = left.issues[index];
        const Issue &right_issue = right.issues[index];
        if (left_issue.severity != right_issue.severity || left_issue.code != right_issue.code ||
            left_issue.message != right_issue.message) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool quality_equal(const QualityAnalysis &left, const QualityAnalysis &right) {
    return left.placement_timing == right.placement_timing && left.monster_reachability == right.monster_reachability &&
           left.findings == right.findings &&
           validation_equal(left.structural_validation, right.structural_validation) && left.summary == right.summary &&
           left.score == right.score && left.layout_sha256 == right.layout_sha256;
}

void validate_quality_artifact(const PipelineResult &result, const std::string_view layout_sha256) {
    const bool has_any_analysis = result.player_navigation || result.monster_small_navigation ||
                                  result.monster_large_navigation || result.visibility || result.lighting;
    if (!result.quality) {
        if (has_any_analysis)
            throw std::invalid_argument("cannot report a partial pipeline analysis without quality evidence");
        return;
    }
    if (!result.player_navigation || !result.visibility || !result.lighting)
        throw std::invalid_argument("cannot report quality without its complete upstream analysis artifacts");
    const QualityAnalysisInputs inputs{*result.player_navigation, *result.visibility, *result.lighting,
                                       result.monster_small_navigation ? &*result.monster_small_navigation : nullptr,
                                       result.monster_large_navigation ? &*result.monster_large_navigation : nullptr};
    try {
        const QualityAnalysis expected = QualityAnalyzer{}.analyze(result.layout, inputs);
        if (result.quality->layout_sha256 != layout_sha256 || !quality_equal(*result.quality, expected)) {
            throw std::invalid_argument("quality artifact does not match the report layout and upstream analyses");
        }
    } catch (const std::invalid_argument &) {
        throw;
    } catch (const std::exception &error) {
        throw std::invalid_argument(std::string("cannot validate quality artifact for reporting: ") + error.what());
    }
}

} // namespace

std::string render_report(const PipelineResult &result) {
    const Layout &level = result.layout;
    if (!std::isfinite(level.rank_score))
        throw std::invalid_argument("cannot render a JSON report with a non-finite rank score");
    const std::string layout_sha256 = canonical_layout_sha256(level);
    validate_quality_artifact(result, layout_sha256);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(4);
    output << "{\n  \"schema_version\": 4,\n";
    output << "  \"generator_version\": \"" << QMG_VERSION << "\",\n";
    // D7: only a run with a switch away from its default gains this member, so default
    // report bytes (schema v4) are unchanged.
    if (!level.spec.switches.all_default()) {
        output << "  \"switches\": {";
        const std::vector<StageSwitches::Entry> switched = level.spec.switches.non_default();
        for (std::size_t index = 0; index < switched.size(); ++index) {
            if (index > 0)
                output << ", ";
            quote(output, switched[index].name);
            output << ": " << (switched[index].on ? "\"on\"" : "\"off\"");
        }
        output << "},\n";
    }
    output << "  \"layout_sha256\": ";
    quote(output, layout_sha256);
    output << ",\n";
    output << "  \"prompt\": ";
    quote(output, level.spec.source_prompt);
    output << ",\n";
    output << "  \"game\": \"" << to_string(level.spec.game) << "\",\n";
    output << "  \"mode\": \"" << to_string(level.spec.mode) << "\",\n";
    if (level.spec.layout_intent != LayoutIntent::unspecified)
        output << "  \"layout_intent\": \"" << to_string(level.spec.layout_intent) << "\",\n";
    output << "  \"theme\": ";
    quote(output, level.spec.theme);
    output << ",\n";
    output << "  \"request_seed\": \"" << level.spec.seed << "\",\n";
    output << "  \"candidate_seed\": \"" << level.candidate_seed << "\",\n";
    output << "  \"ggml_backend\": ";
    quote(output, result.ggml_backend);
    output << ",\n";
    output << "  \"ggml_version\": ";
    quote(output, result.ggml_version);
    output << ",\n";
    output << "  \"ggml_commit\": ";
    quote(output, result.ggml_commit);
    output << ",\n";
    output << "  \"llama_revision\": ";
    quote(output, result.llama_revision);
    output << ",\n";
    MapBuildStats map_stats;
    std::size_t manifest_entries{};
    std::size_t manifest_node_ids{};
    if (result.map_build) {
        map_stats = result.map_build->stats;
        manifest_entries = result.map_build->manifest.size();
        for (const MapBuildManifestEntry &entry : result.map_build->manifest)
            manifest_node_ids += entry.node_ids.size();
    }
    output << "  \"map\": {\n";
    output << "    \"dialect\": ";
    quote(output, format_name(result.dialect));
    output << ",\n    \"built\": " << (result.map_build ? "true" : "false") << ",\n";
    output << "    \"manifest_entries\": " << manifest_entries << ",\n";
    output << "    \"manifest_node_ids\": " << manifest_node_ids << ",\n";
    output << "    \"stats\": {\n";
    output << "      \"input_free_volumes\": " << map_stats.input_free_volumes << ",\n";
    output << "      \"unique_free_volumes\": " << map_stats.unique_free_volumes << ",\n";
    output << "      \"evaluated_face_cells\": " << map_stats.evaluated_face_cells << ",\n";
    output << "      \"shell_brushes\": " << map_stats.shell_brushes << ",\n";
    output << "      \"structural_brushes\": " << map_stats.structural_brushes << ",\n";
    output << "      \"detail_brushes\": " << map_stats.detail_brushes << ",\n";
    output << "      \"placement_brushes\": " << map_stats.placement_brushes << ",\n";
    output << "      \"entity_count\": " << map_stats.entity_count << ",\n";
    output << "      \"brush_count\": " << map_stats.brush_count << ",\n";
    output << "      \"patch_count\": " << map_stats.patch_count << ",\n";
    output << "      \"face_count\": " << map_stats.face_count << "\n";
    output << "    }\n  },\n";
    output << "  \"semantic_plan\": " << render_semantic_plan(result.semantic_plan, false) << ",\n";
    output << "  \"planner\": {\n";
    output << "    \"requested_backend\": ";
    quote(output, result.planner.requested_backend);
    output << ",\n    \"used_backend\": ";
    quote(output, result.planner.used_backend);
    output << ",\n    \"status\": ";
    quote(output, result.planner.status);
    output << ",\n    \"schema_version\": " << result.planner.schema_version << ",\n";
    output << "    \"prompt_template_version\": " << result.planner.prompt_template_version << ",\n";
    output << "    \"attempts\": " << result.planner.attempts << ",\n";
    output << "    \"revisions\": " << result.planner.revisions << ",\n";
    output << "    \"prompt_tokens\": " << result.planner.prompt_tokens << ",\n";
    output << "    \"output_tokens\": " << result.planner.output_tokens << ",\n";
    output << "    \"max_output_tokens\": " << result.planner.max_output_tokens << ",\n";
    output << "    \"prompt_sha256\": ";
    quote(output, result.planner.prompt_sha256);
    output << ",\n    \"grammar_sha256\": ";
    quote(output, result.planner.grammar_sha256);
    output << ",\n    \"transcript_sha256\": ";
    quote(output, result.planner.transcript_sha256);
    output << ",\n    \"attempt_log\": [";
    for (std::size_t index = 0; index < result.planner.attempt_log.size(); ++index) {
        if (index > 0)
            output << ',';
        const PlannerTrace::Attempt &attempt = result.planner.attempt_log[index];
        output << "{\"index\":" << attempt.index << ",\"prompt_sha256\":";
        quote(output, attempt.prompt_sha256);
        output << ",\"output_sha256\":";
        quote(output, attempt.output_sha256);
        output << ",\"finish_reason\":";
        quote(output, attempt.finish_reason);
        output << ",\"prompt_tokens\":" << attempt.prompt_tokens << ",\"output_tokens\":" << attempt.output_tokens
               << '}';
    }
    output << ']';
    output << ",\n    \"model\": {\"backend\": ";
    quote(output, result.planner.model.backend);
    output << ", \"backend_version\": ";
    quote(output, result.planner.model.backend_version);
    output << ", \"model_id\": ";
    quote(output, result.planner.model.model_id);
    output << ", \"model_sha256\": ";
    quote(output, result.planner.model.model_sha256);
    output << ", \"tokenizer_sha256\": ";
    quote(output, result.planner.model.tokenizer_sha256);
    // VibeStudio: only a connector-named model adds this key, so rules and GGUF reports keep their bytes.
    if (result.planner.model.provenance == ModelProvenance::named_service)
        output << ", \"provenance\": \"named_service\"";
    output << "},\n    \"inference\": {\"device\": ";
    quote(output, result.planner.inference.device);
    output << ", \"threads\": " << result.planner.inference.threads
           << ", \"context_limit\": " << result.planner.inference.context_limit << ", \"sampling\": ";
    quote(output, result.planner.inference.sampling);
    output << '}';
    // VibeStudio: only a rules fallback adds this key, so other reports keep their bytes.
    if (!result.planner_fallback.empty()) {
        output << ",\n    \"fallback\": ";
        quote(output, result.planner_fallback);
    }
    output << "\n  },\n";
    output << "  \"rank_score\": " << level.rank_score << ",\n";
    output << "  \"counts\": {\"rooms\": " << level.rooms.size() << ", \"connections\": " << level.connections.size()
           << ", \"solids\": " << level.solids.size() << ", \"entities\": " << level.placements.size() << "},\n";
    output << "  \"analysis\": {\n";
    output << "    \"advisory\": true,\n";
    output << "    \"execution_required\": true,\n";
    output << "    \"navigation\": {\n";
    output << "      \"player\": ";
    write_navigation_analysis(output, result.player_navigation);
    output << ",\n      \"monster_small\": ";
    write_navigation_analysis(output, result.monster_small_navigation);
    output << ",\n      \"monster_large\": ";
    write_navigation_analysis(output, result.monster_large_navigation);
    output << "\n    },\n";
    output << "    \"visibility\": ";
    write_visibility_analysis(output, result.visibility);
    output << ",\n    \"lighting\": ";
    write_lighting_analysis(output, result.lighting);
    output << ",\n    \"quality\": ";
    write_quality_analysis(output, result.quality);
    output << "\n  },\n";
    output << "  \"repair_passes\": [";
    for (std::size_t index = 0; index < result.repairs.size(); ++index) {
        if (index > 0)
            output << ',';
        const RepairPass &pass = result.repairs[index];
        output << "\n    {\"iteration\": " << pass.iteration << ", \"changed\": " << (pass.changed ? "true" : "false")
               << ", \"issue_count_before\": " << pass.before.size() << '}';
    }
    if (!result.repairs.empty())
        output << '\n' << "  ";
    output << "],\n";
    output << "  \"validation\": {\n";
    output << "    \"ok\": " << (result.validation.ok() ? "true" : "false") << ",\n";
    output << "    \"issues\": [";
    for (std::size_t index = 0; index < result.validation.issues.size(); ++index) {
        if (index > 0)
            output << ',';
        const Issue &issue = result.validation.issues[index];
        output << "\n      {\"severity\": \"" << to_string(issue.severity) << "\", \"code\": ";
        quote(output, issue.code);
        output << ", \"message\": ";
        quote(output, issue.message);
        output << '}';
    }
    if (!result.validation.issues.empty())
        output << '\n' << "    ";
    output << "]\n  }\n}\n";
    return output.str();
}

void write_report(const PipelineResult &result, const std::filesystem::path &path) {
    const std::string rendered = render_report(result);
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output)
        throw std::runtime_error("cannot open report output: " + path.string());
    output << rendered;
    if (!output)
        throw std::runtime_error("failed while writing report output: " + path.string());
}

} // namespace qmg
