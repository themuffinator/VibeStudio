#include "qmg/pipeline.hpp"

#include "qmg/generator.hpp"
#include "qmg/layout_ranker.hpp"
#include "qmg/repair.hpp"
#include "qmg/validator.hpp"

#include <stdexcept>
#include <utility>

namespace qmg {
namespace {

[[nodiscard]] MapDialect default_dialect(const Game game) {
    switch (game) {
    case Game::quake1:
        return MapDialect::quake1_valve220;
    case Game::quake2:
        return MapDialect::quake2_valve220;
    case Game::quake3:
        return MapDialect::quake3_legacy;
    }
    throw std::invalid_argument("unsupported game");
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

} // namespace

PipelineResult GenerationPipeline::run(const std::string_view prompt, const PromptOverrides &overrides,
                                       const PipelineOptions &options) const {
    if (options.candidate_count == 0) {
        throw std::invalid_argument("candidate_count must be at least one");
    }
    if (options.candidate_count > 64) {
        throw std::invalid_argument("candidate_count cannot exceed 64");
    }
    if (options.max_repair_iterations < 0) {
        throw std::invalid_argument("max_repair_iterations cannot be negative");
    }
    if (options.max_repair_iterations > 64) {
        throw std::invalid_argument("max_repair_iterations cannot exceed 64");
    }

    if (options.planner_max_revisions > 8) {
        throw std::invalid_argument("planner revisions cannot exceed 8");
    }
    if (options.planner_max_output_tokens < 64 || options.planner_max_output_tokens > 4096) {
        throw std::invalid_argument("planner output tokens must be from 64 to 4096");
    }

    PromptSpec parsed_spec = PromptParser{}.parse(prompt, overrides);
    parsed_spec.switches = options.switches;
    const PromptSpec &spec = parsed_spec;
    const MapDialect dialect = options.dialect.value_or(default_dialect(spec.game));
    if (!dialect_matches_game(spec.game, dialect)) {
        throw std::invalid_argument("map format " + to_string(dialect) + " is incompatible with game " +
                                    to_string(spec.game));
    }
    std::string planner_fallback;
    const PlanningResult planning = [&] {
        try {
            return SemanticPlanner{}.plan(
                spec, {options.text_model, options.planner_max_revisions, options.planner_max_output_tokens});
        } catch (const TextModelUnavailable &) {
            throw;
        } catch (const std::exception &error) {
            if (!options.rules_fallback || options.text_model == nullptr)
                throw;
            planner_fallback = error.what();
            return SemanticPlanner{}.plan(spec, {nullptr, options.planner_max_revisions,
                                                 options.planner_max_output_tokens});
        }
    }();
    std::vector<Layout> candidates =
        LayoutGenerator{}.generate_candidates(spec, planning.plan, options.candidate_count);
    const LayoutRanker::Ranking ranking = LayoutRanker{}.rank(spec, candidates, options.ranker);

    PipelineResult result;
    result.dialect = dialect;
    result.semantic_plan = planning.plan;
    result.planner = planning.trace;
    result.planner_fallback = std::move(planner_fallback);
    result.layout = std::move(candidates[ranking.best_index]);
    result.layout.rank_score = ranking.scores[ranking.best_index];
    result.ggml_backend = ranking.backend;
    result.ggml_version = ranking.ggml_version;
    result.ggml_commit = ranking.ggml_commit;
    result.llama_revision = QMG_LLAMA_REVISION;

    const LayoutValidator validator;
    const LayoutRepairEngine repairer;
    result.validation = validator.validate(result.layout);
    for (int iteration = 0; iteration < options.max_repair_iterations && !result.validation.issues.empty();
         ++iteration) {
        RepairPass pass;
        pass.iteration = iteration + 1;
        pass.before = result.validation.issues;
        const RepairOutcome outcome = repairer.repair(result.layout, result.validation);
        pass.changed = outcome.changed;
        result.repairs.push_back(std::move(pass));
        if (outcome.refusal) {
            result.repair_refusal = outcome.refusal;
            break;
        }
        if (!result.repairs.back().changed)
            break;
        result.validation = validator.validate(result.layout);
    }

    if (result.validation.ok()) {
        MapBuildOptions build_options;
        build_options.dialect = dialect;
        result.map_build = MapBuilder{}.build(result.layout, build_options);

        const NavigationAnalyzer navigation;
        result.player_navigation = navigation.analyze(result.layout);
        const bool native_single_player =
            result.layout.spec.mode == GameMode::single_player && result.layout.spec.game != Game::quake3;
        if (native_single_player) {
            NavigationOptions monster_options;
            monster_options.agent_kind = NavigationAgentKind::monster_small;
            result.monster_small_navigation = navigation.analyze(result.layout, monster_options);
            monster_options.agent_kind = NavigationAgentKind::monster_large;
            result.monster_large_navigation = navigation.analyze(result.layout, monster_options);
        }
        result.visibility = VisibilityAnalyzer{}.analyze(result.layout);
        result.lighting = LightingAnalyzer{}.analyze(result.layout);
        const QualityAnalysisInputs quality_inputs{
            *result.player_navigation, *result.visibility, *result.lighting,
            result.monster_small_navigation ? &*result.monster_small_navigation : nullptr,
            result.monster_large_navigation ? &*result.monster_large_navigation : nullptr};
        result.quality = QualityAnalyzer{}.analyze(result.layout, quality_inputs);
    }

    return result;
}

} // namespace qmg
