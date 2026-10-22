#pragma once

#include "qmg/layout.hpp"
#include "qmg/layout_ranker.hpp"
#include "qmg/lighting.hpp"
#include "qmg/map_builder.hpp"
#include "qmg/navigation.hpp"
#include "qmg/prompt.hpp"
#include "qmg/quality.hpp"
#include "qmg/repair.hpp"
#include "qmg/semantic_planner.hpp"
#include "qmg/switches.hpp"
#include "qmg/visibility.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qmg {

struct PipelineOptions {
    std::size_t candidate_count{6};
    int max_repair_iterations{8};
    std::optional<MapDialect> dialect;
    TextModel *text_model{};
    std::size_t planner_max_revisions{2};
    std::size_t planner_max_output_tokens{512};
    // D7 stage switches, copied into the parsed PromptSpec so every stage sees them through
    // layout.spec.switches. All at their defaults by default.
    StageSwitches switches;
    // VibeStudio: when the text model answers but planning fails (invalid plans past the revision
    // budget, bad provenance), plan by the rules instead and say why in planner_fallback. A
    // TextModelUnavailable still stops generation. Off by default.
    bool rules_fallback{false};
    // VibeStudio: which ranker scores the candidates; RankerBackend::ggml needs a build with ggml.
    RankerBackend ranker{RankerBackend::scalar};
};

struct PipelineResult {
    Layout layout;
    MapDialect dialect{MapDialect::quake1_valve220};
    std::optional<MapBuildResult> map_build;
    SemanticPlan semantic_plan;
    PlannerTrace planner;
    // VibeStudio: why the text model's planning was set aside for the rules (rules_fallback); empty
    // when it was not. The report adds planner.fallback only when this is set.
    std::string planner_fallback;
    ValidationResult validation;
    std::vector<RepairPass> repairs;
    // Set when the last repair pass refused the layout (fail closed); the
    // validation issues it was asked to fix are left in `validation`. Not yet
    // serialized: the report keeps schema v4 until the report v5 bump (D2).
    std::optional<RepairRefusal> repair_refusal;
    std::optional<NavigationAnalysis> player_navigation;
    std::optional<NavigationAnalysis> monster_small_navigation;
    std::optional<NavigationAnalysis> monster_large_navigation;
    std::optional<VisibilityAnalysis> visibility;
    std::optional<LightingAnalysis> lighting;
    std::optional<QualityAnalysis> quality;
    std::string ggml_backend;
    std::string ggml_version;
    std::string ggml_commit;
    std::string llama_revision;
};

class GenerationPipeline {
  public:
    [[nodiscard]] PipelineResult run(std::string_view prompt, const PromptOverrides &overrides = {},
                                     const PipelineOptions &options = {}) const;
};

} // namespace qmg
