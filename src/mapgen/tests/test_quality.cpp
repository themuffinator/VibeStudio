#include "qmg/quality.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

class TestCompilerRecheckVerifier final : public qmg::CompilerRecheckVerifier {
  public:
    enum class Mode { accept, reject, throw_exception };

    std::string expected_envelope_sha256;
    std::string expected_authentication;
    Mode mode{Mode::accept};

    [[nodiscard]] bool authenticate(const qmg::CompilerRecheckEvidence &evidence,
                                    const std::string_view canonical_envelope_sha256) const override {
        if (mode == Mode::throw_exception)
            throw std::runtime_error("test verifier failure");
        return mode == Mode::accept && canonical_envelope_sha256 == expected_envelope_sha256 &&
               evidence.verifier_authentication == expected_authentication;
    }
};

template <typename Function>
void require_quality_error(Function &&function, const std::string_view code, const std::string_view message) {
    try {
        function();
    } catch (const qmg::QualityError &error) {
        if (error.code() == code)
            return;
        throw std::runtime_error(std::string(message) + ": got " + error.code());
    }
    throw std::runtime_error(std::string(message));
}

[[nodiscard]] qmg::Placement point_placement(const std::uint32_t id, const qmg::PlacementRole role,
                                             const qmg::GridVec3 origin) {
    qmg::Placement placement;
    placement.id = qmg::PlacementId{id};
    placement.role = role;
    placement.origin = origin;
    return placement;
}

void add_light(qmg::Layout &layout, const std::uint32_t id, const qmg::GridVec3 origin,
               const std::int32_t intensity = 100'000'000) {
    qmg::Placement light = point_placement(id, qmg::PlacementRole::light, origin);
    light.light_intensity = intensity;
    light.light_color = qmg::RgbColor{1000, 1000, 1000};
    layout.placements.push_back(std::move(light));
}

[[nodiscard]] qmg::Layout duel_layout(const bool with_light = false) {
    qmg::Layout layout;
    layout.spec.title = "Quality duel fixture";
    layout.spec.theme = "industrial";
    layout.spec.game = qmg::Game::quake3;
    layout.spec.mode = qmg::GameMode::duel;
    layout.spec.target_players = 2;
    layout.spec.room_count = 2;
    layout.rooms = {
        {qmg::RoomId{1}, "west", {{0, 0, 0}, {512, 512, 256}}, {"start"}},
        {qmg::RoomId{2}, "east", {{640, 0, 0}, {1152, 512, 256}}, {"arena"}},
    };
    layout.connections = {
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 256, 192, {{{512, 128, 0}, {640, 384, 192}}}}};
    layout.placements = {
        point_placement(1, qmg::PlacementRole::deathmatch_spawn, {128, 256, 48}),
        point_placement(2, qmg::PlacementRole::deathmatch_spawn, {1024, 256, 48}),
        point_placement(3, qmg::PlacementRole::weapon, {384, 256, 48}),
        point_placement(4, qmg::PlacementRole::weapon, {768, 256, 48}),
        point_placement(5, qmg::PlacementRole::health, {256, 128, 48}),
        point_placement(6, qmg::PlacementRole::armor, {896, 128, 48}),
    };
    if (with_light)
        add_light(layout, 7, {448, 256, 160});
    return layout;
}

[[nodiscard]] qmg::Placement exit_trigger(const qmg::Game game) {
    qmg::Placement exit = point_placement(5, qmg::PlacementRole::exit_trigger, {960, 256, 64});
    exit.volume = qmg::GridBounds{{896, 128, 0}, {1024, 384, 128}};
    if (game == qmg::Game::quake1) {
        exit.destination_map = "nextmap";
    } else {
        exit.link = qmg::PlacementId{6};
    }
    return exit;
}

[[nodiscard]] qmg::Layout single_player_layout(const qmg::Game game) {
    qmg::Layout layout;
    layout.spec.title = "Quality single-player fixture";
    layout.spec.theme = "industrial";
    layout.spec.game = game;
    layout.spec.mode = qmg::GameMode::single_player;
    layout.spec.target_players = 1;
    layout.spec.room_count = 2;
    layout.rooms = {
        {qmg::RoomId{1}, "start", {{0, 0, 0}, {512, 512, 256}}, {}},
        {qmg::RoomId{2}, "exit", {{640, 0, 0}, {1152, 512, 256}}, {}},
    };
    layout.connections = {
        {qmg::ConnectionId{1}, qmg::RoomId{1}, qmg::RoomId{2}, 192, 192, {{{512, 160, 0}, {640, 352, 192}}}}};
    layout.placements = {
        point_placement(1, qmg::PlacementRole::player_start, {128, 256, 48}),
        point_placement(2, qmg::PlacementRole::monster, {768, 256, 48}),
        point_placement(3, qmg::PlacementRole::weapon, {896, 96, 48}),
        point_placement(4, qmg::PlacementRole::health, {768, 384, 48}),
        exit_trigger(game),
    };
    if (game == qmg::Game::quake2) {
        qmg::Placement destination = point_placement(6, qmg::PlacementRole::exit_destination, {1040, 256, 48});
        destination.destination_map = "nextmap";
        layout.placements.push_back(std::move(destination));
    }
    add_light(layout, game == qmg::Game::quake1 ? 6U : 7U, {448, 256, 160});
    return layout;
}

struct EvidenceBundle {
    qmg::NavigationAnalysis player;
    qmg::VisibilityAnalysis visibility;
    qmg::LightingAnalysis lighting;
    std::optional<qmg::NavigationAnalysis> monster_small;
    std::optional<qmg::NavigationAnalysis> monster_large;
};

[[nodiscard]] EvidenceBundle gather_evidence(const qmg::Layout &layout) {
    EvidenceBundle bundle;
    bundle.player = qmg::NavigationAnalyzer{}.analyze(layout);
    bundle.visibility = qmg::VisibilityAnalyzer{}.analyze(layout);
    bundle.lighting = qmg::LightingAnalyzer{}.analyze(layout);
    if (layout.spec.game != qmg::Game::quake3 &&
        std::any_of(layout.placements.begin(), layout.placements.end(),
                    [](const qmg::Placement &placement) { return placement.role == qmg::PlacementRole::monster; })) {
        qmg::NavigationOptions options;
        options.agent_kind = qmg::NavigationAgentKind::monster_small;
        bundle.monster_small = qmg::NavigationAnalyzer{}.analyze(layout, options);
        options.agent_kind = qmg::NavigationAgentKind::monster_large;
        bundle.monster_large = qmg::NavigationAnalyzer{}.analyze(layout, options);
    }
    return bundle;
}

[[nodiscard]] qmg::QualityAnalysisInputs inputs(const EvidenceBundle &bundle,
                                                const qmg::LeakDiagnosisResult *diagnosis = nullptr) {
    return {bundle.player,
            bundle.visibility,
            bundle.lighting,
            bundle.monster_small ? &*bundle.monster_small : nullptr,
            bundle.monster_large ? &*bundle.monster_large : nullptr,
            diagnosis};
}

[[nodiscard]] qmg::QualityRepairProposal light_proposal(const qmg::Layout &layout) {
    qmg::AddOrAdjustLightRepair light;
    light.expected_new_id = qmg::next_placement_id(layout);
    light.expected_origin = {};
    light.new_origin = {448, 256, 160};
    light.new_intensity = 100'000'000;
    light.new_color = {1000, 1000, 1000};
    return {qmg::QualityRepairProposalId{1}, qmg::quality_layout_fingerprint(layout), light};
}

void timing_and_monster_evidence() {
    for (const qmg::Game game : {qmg::Game::quake1, qmg::Game::quake2}) {
        const qmg::Layout layout = single_player_layout(game);
        const EvidenceBundle evidence = gather_evidence(layout);
        const qmg::QualityAnalysis analysis = qmg::QualityAnalyzer{}.analyze(layout, inputs(evidence));
        require(analysis.summary.spawn_count == 1 && analysis.summary.timing_target_count == 4 &&
                    analysis.summary.timing_pair_count == 4 && analysis.summary.reachable_timing_pairs == 4,
                "single-player item/encounter timing matrix is incomplete");
        require(analysis.summary.monster_pair_count == 2 && analysis.summary.reachable_monster_pairs == 2,
                "Q1/Q2 small/large monster-to-player evidence is incomplete");
        require(analysis.monster_reachability[0].agent_kind == qmg::NavigationAgentKind::monster_small &&
                    analysis.monster_reachability[1].agent_kind == qmg::NavigationAgentKind::monster_large,
                "monster reachability evidence is not stable by agent kind");
    }

    qmg::Layout ctf = duel_layout(true);
    ctf.spec.game = qmg::Game::quake1;
    ctf.spec.mode = qmg::GameMode::capture_the_flag;
    qmg::Placement red_flag = point_placement(8, qmg::PlacementRole::flag, {256, 384, 48});
    red_flag.team = qmg::Team::red;
    ctf.placements.push_back(red_flag);
    qmg::Placement blue_flag = point_placement(9, qmg::PlacementRole::flag, {896, 384, 48});
    blue_flag.team = qmg::Team::blue;
    ctf.placements.push_back(blue_flag);
    const EvidenceBundle ctf_evidence = gather_evidence(ctf);
    const qmg::QualityAnalysis ctf_analysis = qmg::QualityAnalyzer{}.analyze(ctf, inputs(ctf_evidence));
    const auto flag_pairs = std::count_if(
        ctf_analysis.placement_timing.begin(), ctf_analysis.placement_timing.end(),
        [](const qmg::PlacementTimingMetric &metric) { return metric.target_kind == qmg::QualityTimingTarget::flag; });
    const auto armor_pairs = std::count_if(
        ctf_analysis.placement_timing.begin(), ctf_analysis.placement_timing.end(),
        [](const qmg::PlacementTimingMetric &metric) { return metric.target_kind == qmg::QualityTimingTarget::armor; });
    require(flag_pairs == 4 && armor_pairs == 2,
            "flag or armor timing was omitted from the complete player-to-target matrix");

    const qmg::Layout q3 = duel_layout(true);
    const EvidenceBundle q3_evidence = gather_evidence(q3);
    qmg::QualityAnalysisInputs invented = inputs(q3_evidence);
    invented.monster_small_navigation = &q3_evidence.player;
    require_quality_error([&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(q3, invented)); },
                          "q3_monster_semantics", "Q3 accepted invented monster navigation semantics");
}

void timing_includes_anchor_positions() {
    qmg::Layout layout = duel_layout(true);
    const EvidenceBundle evidence = gather_evidence(layout);
    const auto *spawn_binding = evidence.player.find_binding(qmg::PlacementId{1});
    const auto *weapon_binding = evidence.player.find_binding(qmg::PlacementId{3});
    require(spawn_binding && weapon_binding && spawn_binding->node == weapon_binding->node,
            "timing regression fixture does not share a walking node");
    const auto timing_for = [](const qmg::QualityAnalysis &analysis, const qmg::PlacementId spawn,
                               const qmg::PlacementId target) -> const qmg::PlacementTimingMetric & {
        const auto found = std::find_if(
            analysis.placement_timing.begin(), analysis.placement_timing.end(),
            [&](const qmg::PlacementTimingMetric &metric) { return metric.spawn == spawn && metric.target == target; });
        require(found != analysis.placement_timing.end(), "anchor timing metric is missing");
        return *found;
    };
    const qmg::QualityAnalysis first = qmg::QualityAnalyzer{}.analyze(layout, inputs(evidence));
    const auto &local = timing_for(first, qmg::PlacementId{1}, qmg::PlacementId{3});
    require(local.reachable && local.navigation_cost == 256'000 && local.arrival_milliseconds == 800,
            "quality timing still reports zero cost between distinct local placements");
    const auto &remote = timing_for(first, qmg::PlacementId{1}, qmg::PlacementId{4});
    require(remote.navigation_cost == 896'000 && remote.arrival_milliseconds == 2800,
            "quality timing omitted an endpoint approach from a multi-node route");

    layout.placements[2].origin.x = 256;
    layout.placements[2].origin.z = 32;
    const EvidenceBundle moved_evidence = gather_evidence(layout);
    const qmg::QualityAnalysis moved = qmg::QualityAnalyzer{}.analyze(layout, inputs(moved_evidence));
    const auto &nearer = timing_for(moved, qmg::PlacementId{1}, qmg::PlacementId{3});
    require(nearer.navigation_cost == 128'000 && nearer.arrival_milliseconds == 400,
            "moving a pickup within one area did not change timing, or charged its authored height offset");

    // Volume targets bind at their volume center, regardless of a decorative
    // origin elsewhere in the same volume. Timing must use that identical point.
    qmg::Layout volume_layout = single_player_layout(qmg::Game::quake1);
    volume_layout.placements[4].origin = {912, 240, 24};
    const EvidenceBundle volume_evidence = gather_evidence(volume_layout);
    const qmg::QualityAnalysis volume = qmg::QualityAnalyzer{}.analyze(volume_layout, inputs(volume_evidence));
    require(timing_for(volume, qmg::PlacementId{1}, qmg::PlacementId{5}).navigation_cost == 832'000,
            "volume target timing did not use its navigation binding point");
}

void deterministic_reordering_and_caps() {
    const qmg::Layout ordered = single_player_layout(qmg::Game::quake1);
    const EvidenceBundle ordered_evidence = gather_evidence(ordered);
    const qmg::QualityAnalysis first = qmg::QualityAnalyzer{}.analyze(ordered, inputs(ordered_evidence));

    qmg::Layout shuffled = ordered;
    std::reverse(shuffled.rooms.begin(), shuffled.rooms.end());
    std::reverse(shuffled.connections.begin(), shuffled.connections.end());
    std::reverse(shuffled.solids.begin(), shuffled.solids.end());
    std::reverse(shuffled.placements.begin(), shuffled.placements.end());
    const EvidenceBundle shuffled_evidence = gather_evidence(shuffled);
    const qmg::QualityAnalysis second = qmg::QualityAnalyzer{}.analyze(shuffled, inputs(shuffled_evidence));
    require(qmg::quality_layout_fingerprint(ordered) == qmg::quality_layout_fingerprint(shuffled),
            "top-level vector reordering changed the quality fingerprint");
    require(first.placement_timing == second.placement_timing &&
                first.monster_reachability == second.monster_reachability && first.findings == second.findings &&
                first.summary == second.summary && first.score == second.score &&
                first.layout_sha256 == qmg::canonical_layout_sha256(ordered) &&
                second.layout_sha256 == qmg::canonical_layout_sha256(shuffled),
            "top-level vector reordering changed typed quality evidence");

    qmg::Layout changed_after_analysis = ordered;
    qmg::find_placement(changed_after_analysis, qmg::PlacementId{3})->origin.x += 1;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(changed_after_analysis, inputs(ordered_evidence))); },
        "stale_analysis", "quality analysis accepted upstream evidence for an older layout artifact");
    EvidenceBundle stale_monsters = ordered_evidence;
    stale_monsters.monster_large->layout_sha256 = std::string(64, '0');
    require_quality_error([&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(ordered, inputs(stale_monsters))); },
                          "stale_analysis", "quality analysis accepted stale large-monster navigation evidence");
    EvidenceBundle tampered_navigation = ordered_evidence;
    ++tampered_navigation.player.stats.node_count;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(ordered, inputs(tampered_navigation))); },
        "analysis_content_mismatch", "quality analysis accepted a mutated navigation payload");
    EvidenceBundle tampered_visibility = ordered_evidence;
    ++tampered_visibility.visibility.summary.safe_spawns;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(ordered, inputs(tampered_visibility))); },
        "analysis_content_mismatch", "quality analysis accepted a mutated visibility payload");
    EvidenceBundle tampered_lighting = ordered_evidence;
    ++tampered_lighting.lighting.summary.readable_samples;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(ordered, inputs(tampered_lighting))); },
        "analysis_content_mismatch", "quality analysis accepted a mutated lighting payload");

    qmg::NavigationAnalysis baseline_errors = ordered_evidence.player;
    baseline_errors.findings = {{qmg::NavigationFindingSeverity::error,
                                 qmg::NavigationFindingKind::unreachable_objective,
                                 qmg::NavigationSourceRef{qmg::NavigationSourceKind::placement, 3, 0}, std::nullopt,
                                 "baseline objective is unreachable"}};
    qmg::NavigationAnalysis swapped_errors = baseline_errors;
    swapped_errors.findings = {{qmg::NavigationFindingSeverity::error, qmg::NavigationFindingKind::unbound_placement,
                                qmg::NavigationSourceRef{qmg::NavigationSourceKind::placement, 4, 0}, std::nullopt,
                                "candidate placement is unbound"}};
    require(qmg::navigation_error_findings_preserved(baseline_errors, baseline_errors),
            "navigation regression policy rejected an unchanged error multiset");
    require(!qmg::navigation_error_findings_preserved(baseline_errors, swapped_errors),
            "navigation regression policy accepted a new error that merely preserved the aggregate count");

    qmg::QualityOptions capped;
    capped.limits.max_timing_pairs = 1;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(ordered, inputs(ordered_evidence), capped)); },
        "timing_pair_cap", "timing pair cap did not fail closed before path expansion");
    capped = {};
    capped.limits.max_monster_pairs = 1;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(ordered, inputs(ordered_evidence), capped)); },
        "monster_pair_cap", "monster pair cap did not fail closed before monster paths");
    capped = {};
    capped.limits.max_rooms = 1;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(ordered, inputs(ordered_evidence), capped)); },
        "input_cap", "layout input cap did not fail closed before structural quadratic work");
}

void accepted_light_and_stale_or_unhelpful_rejections() {
    const qmg::Layout layout = duel_layout(false);
    const EvidenceBundle evidence = gather_evidence(layout);
    const qmg::QualityRepairProposal proposal = light_proposal(layout);
    const qmg::QualityRepairResult accepted = qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), proposal);
    require(accepted.accepted() && accepted.after_score < accepted.before_score && accepted.accepted_layout &&
                accepted.accepted_layout->placements.size() == layout.placements.size() + 1 &&
                layout.placements.size() == 6,
            "localized light addition was not accepted without mutating its input");

    qmg::QualityRepairOptions lowering_capped;
    lowering_capped.lowering.max_shell_brushes = 1;
    const qmg::QualityRepairResult lowering_rejected =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), proposal, lowering_capped);
    require(lowering_rejected.rejection == qmg::QualityRepairRejection::lowering_failed &&
                !lowering_rejected.accepted_layout,
            "candidate bypassed successful MapBuilder lowering as a hard acceptance gate");

    qmg::QualityRepairProposal stale = proposal;
    stale.expected_layout_fingerprint ^= 1U;
    const qmg::QualityRepairResult stale_result = qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), stale);
    require(stale_result.rejection == qmg::QualityRepairRejection::stale_proposal && !stale_result.accepted_layout,
            "stale repair proposal was not rejected before mutation");

    qmg::MovePlacementRepair unchanged;
    unchanged.placement = qmg::PlacementId{3};
    unchanged.expected_role = qmg::PlacementRole::weapon;
    unchanged.expected_origin = {384, 256, 48};
    unchanged.new_origin = unchanged.expected_origin;
    const qmg::QualityRepairProposal no_change{qmg::QualityRepairProposalId{2}, qmg::quality_layout_fingerprint(layout),
                                               unchanged};
    const qmg::QualityRepairResult rejected = qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), no_change);
    require(rejected.rejection == qmg::QualityRepairRejection::not_strictly_improved && !rejected.accepted_layout,
            "unchanged candidate bypassed the strict score gate");
}

void monster_evidence_scope_is_preserved() {
    qmg::Layout layout = single_player_layout(qmg::Game::quake1);
    std::erase_if(layout.placements, [](const qmg::Placement &placement) {
        return placement.role == qmg::PlacementRole::monster || placement.role == qmg::PlacementRole::light;
    });
    layout.connections.front().width = 48;
    layout.connections.front().passage_volumes = {{{512, 232, 0}, {640, 280, 192}}};

    EvidenceBundle evidence = gather_evidence(layout);
    qmg::NavigationOptions monster_options;
    monster_options.agent_kind = qmg::NavigationAgentKind::monster_small;
    evidence.monster_small = qmg::NavigationAnalyzer{}.analyze(layout, monster_options);
    monster_options.agent_kind = qmg::NavigationAgentKind::monster_large;
    evidence.monster_large = qmg::NavigationAnalyzer{}.analyze(layout, monster_options);
    const qmg::QualityAnalysis baseline = qmg::QualityAnalyzer{}.analyze(layout, inputs(evidence));
    require(baseline.score.navigation_errors > 0,
            "no-monster evidence-scope fixture did not exercise monster-profile navigation findings");

    const qmg::QualityRepairResult repaired =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), light_proposal(layout));
    require(repaired.accepted() && repaired.after_score.navigation_errors == repaired.before_score.navigation_errors,
            "repair candidate silently dropped optional baseline monster-analysis scope");
}

void construction_aware_lowering_preserves_veneers() {
    qmg::Layout layout = duel_layout(false);
    qmg::CurvedWallSpec curve;
    curve.key = qmg::ConstructionKey{1};
    curve.center = {64, 64, 0};
    curve.inner_radius = 64;
    curve.thickness = 16;
    curve.height = 96;
    curve.segment_count = 4;
    curve.emit_patch_veneer = true;
    const qmg::ConstructionResult construction = qmg::ConstructionBuilder{}.build(layout, {curve});
    construction.apply_to(layout);
    const EvidenceBundle evidence = gather_evidence(layout);
    const qmg::QualityRepairProposal proposal = light_proposal(layout);

    const qmg::QualityRepairResult brush_only = qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), proposal);
    qmg::QualityRepairOptions with_construction;
    with_construction.construction = &construction;
    const qmg::QualityRepairResult veneer =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), proposal, with_construction);
    require(brush_only.accepted() && veneer.accepted() && brush_only.candidate_source_map_sha256.size() == 64 &&
                veneer.candidate_source_map_sha256.size() == 64 &&
                brush_only.candidate_source_map_sha256 != veneer.candidate_source_map_sha256,
            "construction-aware repair lowering did not preserve the Q3 patch veneer artifact");

    qmg::QualityRepairOptions brush_primitives = with_construction;
    brush_primitives.dialect = qmg::MapDialect::quake3_brush_primitives;
    const qmg::QualityRepairResult bp =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), proposal, brush_primitives);
    require(bp.accepted() && bp.candidate_map_dialect == qmg::MapDialect::quake3_brush_primitives &&
                bp.candidate_source_map_sha256 != veneer.candidate_source_map_sha256,
            "repair lowering did not bind the Q3 brush-primitive dialect to its canonical source artifact");

    qmg::QualityRepairOptions wrong_dialect;
    wrong_dialect.dialect = qmg::MapDialect::quake2_valve220;
    const qmg::QualityRepairResult rejected =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), proposal, wrong_dialect);
    require(rejected.rejection == qmg::QualityRepairRejection::lowering_failed && !rejected.accepted_layout,
            "repair lowering accepted a map dialect incompatible with the candidate title");
}

void rollback_regression_and_passage_repair() {
    const qmg::Layout layout = duel_layout(false);
    const EvidenceBundle evidence = gather_evidence(layout);
    qmg::MovePlacementRepair outside;
    outside.placement = qmg::PlacementId{3};
    outside.expected_role = qmg::PlacementRole::weapon;
    outside.expected_origin = {384, 256, 48};
    outside.new_origin = {20'000, 20'000, 20'000};
    const qmg::QualityRepairProposal bad{qmg::QualityRepairProposalId{3}, qmg::quality_layout_fingerprint(layout),
                                         outside};
    const qmg::QualityRepairResult rolled_back = qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), bad);
    require(rolled_back.rejection == qmg::QualityRepairRejection::new_structural_error &&
                !rolled_back.accepted_layout &&
                qmg::find_placement(layout, qmg::PlacementId{3})->origin == qmg::GridVec3{384, 256, 48},
            "structurally invalid placement move was not rolled back");

    qmg::Layout narrow = layout;
    narrow.connections.front().width = 28;
    narrow.connections.front().passage_volumes = {{{512, 242, 0}, {640, 270, 192}}};
    const EvidenceBundle narrow_evidence = gather_evidence(narrow);
    qmg::ReplaceConnectionPassageRepair replacement;
    replacement.connection = qmg::ConnectionId{1};
    replacement.expected_from_room = qmg::RoomId{1};
    replacement.expected_to_room = qmg::RoomId{2};
    replacement.expected_width = 28;
    replacement.expected_height = 192;
    replacement.expected_passage_volumes = narrow.connections.front().passage_volumes;
    replacement.new_width = 256;
    replacement.new_height = 192;
    replacement.new_passage_volumes = {{{512, 128, 0}, {640, 384, 192}}};
    const qmg::QualityRepairProposal passage{qmg::QualityRepairProposalId{4}, qmg::quality_layout_fingerprint(narrow),
                                             replacement};
    const qmg::QualityRepairResult repaired =
        qmg::QualityRepairEngine{}.evaluate(narrow, inputs(narrow_evidence), passage);
    require(repaired.accepted() && repaired.after_score.structural_errors < repaired.before_score.structural_errors &&
                repaired.accepted_layout->connections.front().width == 256,
            "localized passage replacement did not repair structural/navigation quality");

    qmg::AddCoverSolidRepair cover;
    cover.expected_new_id = qmg::next_solid_id(layout);
    cover.bounds = {{120, 240, 0}, {160, 272, 96}};
    const qmg::QualityRepairProposal colliding_cover{qmg::QualityRepairProposalId{5},
                                                     qmg::quality_layout_fingerprint(layout), cover};
    const qmg::QualityRepairResult cover_rejected =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), colliding_cover);
    require(cover_rejected.rejection == qmg::QualityRepairRejection::new_structural_error &&
                !cover_rejected.accepted_layout,
            "cover solid embedding a required spawn bypassed structural rollback");

    qmg::ReplaceConnectionPassageRepair high_passage;
    high_passage.connection = qmg::ConnectionId{1};
    high_passage.expected_from_room = qmg::RoomId{1};
    high_passage.expected_to_room = qmg::RoomId{2};
    high_passage.expected_width = 256;
    high_passage.expected_height = 192;
    high_passage.expected_passage_volumes = layout.connections.front().passage_volumes;
    high_passage.new_width = 96;
    high_passage.new_height = 128;
    high_passage.new_passage_volumes = {{{512, 208, 100}, {640, 304, 228}}};
    const qmg::QualityRepairProposal navigation_loss{qmg::QualityRepairProposalId{7},
                                                     qmg::quality_layout_fingerprint(layout), high_passage};
    const qmg::QualityRepairResult navigation_rejected =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence), navigation_loss);
    require(navigation_rejected.rejection == qmg::QualityRepairRejection::navigation_regression &&
                !navigation_rejected.accepted_layout,
            "candidate losing a previously reachable spawn/target path bypassed navigation rollback");
}

[[nodiscard]] qmg::LeakWitness leak_witness() {
    const qmg::PointfileIngestResult parsed =
        qmg::parse_compiler_pointfile("128 256 64\n128 -100 64\n", qmg::CompilerPointfileDialect::quake3);
    require(parsed.ok(), "quality seal fixture pointfile did not parse");
    return *parsed.witness;
}

void seal_requires_bound_external_evidence() {
    const qmg::Layout layout = duel_layout(true);
    const qmg::LeakDiagnosisResult diagnosis = qmg::diagnose_layout_leak(layout, leak_witness());
    require(diagnosis.ok(), "quality seal fixture did not produce a local seal proposal");
    const EvidenceBundle evidence = gather_evidence(layout);
    require(qmg::QualityAnalyzer{}.analyze(layout, inputs(evidence, &diagnosis)).summary.leak_diagnosis_evaluated,
            "quality summary did not distinguish diagnosed leak evidence from an unevaluated leak state");
    const qmg::PointfileIngestResult inside_pointfile =
        qmg::parse_compiler_pointfile("128 256 64\n256 256 64\n", qmg::CompilerPointfileDialect::quake3);
    require(inside_pointfile.ok(), "quality unusable-leak fixture pointfile did not parse");
    const qmg::LeakDiagnosisResult no_exposed_crossing = qmg::diagnose_layout_leak(layout, *inside_pointfile.witness);
    require(!no_exposed_crossing.ok(), "quality unusable-leak fixture unexpectedly diagnosed a crossing");
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(layout, inputs(evidence, &no_exposed_crossing))); },
        "invalid_leak_evidence", "quality treated an unusable pointfile diagnosis as leak-free evidence");
    qmg::LeakDiagnosisResult tampered_diagnosis = diagnosis;
    ++tampered_diagnosis.exposed_crossing_count;
    require_quality_error(
        [&] { static_cast<void>(qmg::QualityAnalyzer{}.analyze(layout, inputs(evidence, &tampered_diagnosis))); },
        "analysis_content_mismatch", "quality analysis accepted a mutated leak diagnosis payload");
    qmg::LocalSealRepair seal;
    seal.expected_new_id = qmg::next_solid_id(layout);
    seal.proposal = *diagnosis.proposal;
    const qmg::QualityRepairProposal proposal{qmg::QualityRepairProposalId{6}, qmg::quality_layout_fingerprint(layout),
                                              seal};

    qmg::CompilerRecheckRequirements requirements;
    requirements.trusted_runner_id = "quality-test-runner";
    requirements.map_dialect = qmg::MapDialect::quake3_legacy;
    requirements.pointfile_dialect = qmg::CompilerPointfileDialect::quake3;
    requirements.compiler_executable_sha256 = std::string(64, 'A');
    requirements.compiler_options_sha256 = std::string(64, 'b');
    requirements.compiler_invocation_sha256 = std::string(64, 'c');
    qmg::QualityRepairOptions seal_options;
    seal_options.compiler_recheck = requirements;

    const qmg::QualityRepairResult needs_recheck =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence, &diagnosis), proposal, seal_options);
    require(needs_recheck.rejection == qmg::QualityRepairRejection::compiler_recheck_required &&
                needs_recheck.candidate_layout_fingerprint != 0 && needs_recheck.candidate_layout_sha256.size() == 64 &&
                needs_recheck.candidate_map_dialect && needs_recheck.candidate_source_map_sha256.size() == 64 &&
                needs_recheck.candidate_source_map_bytes > 0 && !needs_recheck.accepted_layout,
            "seal was accepted without explicit compiler recheck evidence");

    qmg::CompilerRecheckEvidence attestation;
    attestation.trusted_runner_id = requirements.trusted_runner_id;
    attestation.proposal = proposal.id;
    attestation.baseline_layout_fingerprint = needs_recheck.baseline_layout_fingerprint;
    attestation.candidate_layout_fingerprint = needs_recheck.candidate_layout_fingerprint;
    attestation.baseline_layout_sha256 = needs_recheck.baseline_layout_sha256;
    attestation.candidate_layout_sha256 = needs_recheck.candidate_layout_sha256;
    attestation.map_dialect = *needs_recheck.candidate_map_dialect;
    attestation.source_map_sha256 = needs_recheck.candidate_source_map_sha256;
    attestation.source_map_bytes = needs_recheck.candidate_source_map_bytes;
    attestation.compiler_executable_sha256 = requirements.compiler_executable_sha256;
    attestation.compiler_options_sha256 = requirements.compiler_options_sha256;
    attestation.compiler_invocation_sha256 = requirements.compiler_invocation_sha256;
    attestation.output_bsp_sha256 = std::string(64, 'D');
    attestation.output_bsp_bytes = 4096;
    attestation.diagnostics_sha256 = std::string(64, 'E');
    attestation.pointfile_dialect = requirements.pointfile_dialect;
    attestation.witness_fingerprint = diagnosis.proposal->applicability.witness_fingerprint;
    attestation.compiler_succeeded = true;
    attestation.postcompile_pointfile_absent = true;
    attestation.leak_result = qmg::CompilerLeakResult::leak_free;
    attestation.envelope_sha256 = qmg::compiler_recheck_envelope_sha256(attestation);
    attestation.verifier_authentication = "authenticated-by-test-host";

    TestCompilerRecheckVerifier verifier;
    verifier.expected_envelope_sha256 = attestation.envelope_sha256;
    verifier.expected_authentication = attestation.verifier_authentication;
    seal_options.compiler_recheck_verifier = &verifier;

    qmg::CompilerRecheckEvidence oversized_attestation = attestation;
    oversized_attestation.trusted_runner_id = std::string(4097, 'x');
    bool oversized_attestation_rejected = false;
    try {
        static_cast<void>(qmg::compiler_recheck_envelope_sha256(oversized_attestation));
    } catch (const std::length_error &) {
        oversized_attestation_rejected = true;
    }
    require(oversized_attestation_rejected, "oversized compiler attestation bypassed its canonical byte cap");

    const auto require_attestation_rejected = [&](qmg::CompilerRecheckEvidence stale, const std::string_view message) {
        const qmg::QualityRepairResult rejected =
            qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence, &diagnosis), proposal, seal_options, stale);
        require(rejected.rejection == qmg::QualityRepairRejection::compiler_evidence_mismatch &&
                    !rejected.accepted_layout,
                message);
    };
    qmg::CompilerRecheckEvidence stale_dialect = attestation;
    stale_dialect.map_dialect = qmg::MapDialect::quake3_brush_primitives;
    stale_dialect.envelope_sha256 = qmg::compiler_recheck_envelope_sha256(stale_dialect);
    require_attestation_rejected(stale_dialect, "seal accepted an attested stale map dialect");
    qmg::CompilerRecheckEvidence stale_source = attestation;
    stale_source.source_map_sha256 = std::string(64, 'f');
    stale_source.envelope_sha256 = qmg::compiler_recheck_envelope_sha256(stale_source);
    require_attestation_rejected(stale_source, "seal accepted attestation for stale canonical map source");
    qmg::CompilerRecheckEvidence stale_config = attestation;
    stale_config.compiler_options_sha256 = std::string(64, 'f');
    stale_config.envelope_sha256 = qmg::compiler_recheck_envelope_sha256(stale_config);
    require_attestation_rejected(stale_config, "seal accepted attestation for stale compiler options");
    qmg::CompilerRecheckEvidence stale_artifact = attestation;
    stale_artifact.output_bsp_sha256 = std::string(64, 'f');
    require_attestation_rejected(stale_artifact, "seal accepted a BSP artifact not bound by the attestation");
    qmg::CompilerRecheckEvidence pointfile_remained = attestation;
    pointfile_remained.postcompile_pointfile_absent = false;
    pointfile_remained.envelope_sha256 = qmg::compiler_recheck_envelope_sha256(pointfile_remained);
    require_attestation_rejected(pointfile_remained, "seal accepted a postcompile pointfile");

    const qmg::QualityRepairResult missing_trust_policy =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence, &diagnosis), proposal, {}, attestation);
    require(missing_trust_policy.rejection == qmg::QualityRepairRejection::compiler_evidence_mismatch,
            "seal accepted a self-asserted envelope without trusted-runner policy and authentication");
    qmg::QualityRepairOptions missing_verifier = seal_options;
    missing_verifier.compiler_recheck_verifier = nullptr;
    const qmg::QualityRepairResult unauthenticated = qmg::QualityRepairEngine{}.evaluate(
        layout, inputs(evidence, &diagnosis), proposal, missing_verifier, attestation);
    require(unauthenticated.rejection == qmg::QualityRepairRejection::compiler_evidence_mismatch,
            "seal accepted an integrity envelope without a host authentication decision");
    TestCompilerRecheckVerifier rejecting_verifier = verifier;
    rejecting_verifier.mode = TestCompilerRecheckVerifier::Mode::reject;
    qmg::QualityRepairOptions rejected_verifier_options = seal_options;
    rejected_verifier_options.compiler_recheck_verifier = &rejecting_verifier;
    const qmg::QualityRepairResult rejected_authentication = qmg::QualityRepairEngine{}.evaluate(
        layout, inputs(evidence, &diagnosis), proposal, rejected_verifier_options, attestation);
    require(rejected_authentication.rejection == qmg::QualityRepairRejection::compiler_evidence_mismatch,
            "seal bypassed a negative host authentication decision");
    TestCompilerRecheckVerifier throwing_verifier = verifier;
    throwing_verifier.mode = TestCompilerRecheckVerifier::Mode::throw_exception;
    qmg::QualityRepairOptions throwing_verifier_options = seal_options;
    throwing_verifier_options.compiler_recheck_verifier = &throwing_verifier;
    const qmg::QualityRepairResult verifier_exception = qmg::QualityRepairEngine{}.evaluate(
        layout, inputs(evidence, &diagnosis), proposal, throwing_verifier_options, attestation);
    require(verifier_exception.rejection == qmg::QualityRepairRejection::compiler_evidence_mismatch,
            "seal did not fail closed when host authentication threw");
    qmg::QualityRepairOptions oversized_runner_options = seal_options;
    oversized_runner_options.compiler_recheck->trusted_runner_id = std::string(257, 'x');
    const qmg::QualityRepairResult oversized_runner = qmg::QualityRepairEngine{}.evaluate(
        layout, inputs(evidence, &diagnosis), proposal, oversized_runner_options, attestation);
    require(oversized_runner.rejection == qmg::QualityRepairRejection::compiler_evidence_mismatch,
            "seal accepted an oversized trusted-runner identifier");
    const qmg::QualityRepairResult accepted =
        qmg::QualityRepairEngine{}.evaluate(layout, inputs(evidence, &diagnosis), proposal, seal_options, attestation);
    require(accepted.accepted() && accepted.before_score.leak_errors > 0 && accepted.after_score.leak_errors == 0 &&
                accepted.accepted_layout->solids.size() == layout.solids.size() + 1,
            "compiler-verified local seal did not pass the strict leak-quality gate");
}

} // namespace

int main() {
    try {
        timing_and_monster_evidence();
        timing_includes_anchor_positions();
        deterministic_reordering_and_caps();
        accepted_light_and_stale_or_unhelpful_rejections();
        monster_evidence_scope_is_preserved();
        construction_aware_lowering_preserves_veneers();
        rollback_regression_and_passage_repair();
        seal_requires_bound_external_evidence();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "quality timing and localized repair tests passed\n";
    return 0;
}
