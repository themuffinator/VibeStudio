#pragma once

#include "qmg/semantic_plan.hpp"
#include "qmg/text_model.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace qmg {

struct PlannerOptions {
    TextModel *model{};
    std::size_t max_revisions{2};
    std::size_t max_output_tokens{512};
};

struct PlannerTrace {
    std::string requested_backend{"rules"};
    std::string used_backend{"rules/v1"};
    std::string status{"ok"};
    int schema_version{kSemanticPlanSchemaVersion};
    int prompt_template_version{1};
    std::size_t attempts{};
    std::size_t revisions{};
    std::size_t prompt_tokens{};
    std::size_t output_tokens{};
    std::size_t max_output_tokens{};
    std::string prompt_sha256;
    std::string grammar_sha256;
    std::string transcript_sha256;
    struct Attempt {
        std::size_t index{};
        std::string prompt_sha256;
        std::string output_sha256;
        std::string finish_reason;
        std::size_t prompt_tokens{};
        std::size_t output_tokens{};
    };
    std::vector<Attempt> attempt_log;
    ModelIdentity model;
    InferenceConfig inference;
};

struct PlanningResult {
    SemanticPlan plan;
    PlannerTrace trace;
};

class SemanticPlanner {
  public:
    [[nodiscard]] PlanningResult plan(const PromptSpec &request, const PlannerOptions &options = {}) const;
};

// VibeStudio: the first request plan() sends a model for this normalized request, so a host can show
// it, or ask before it leaves the machine, before planning starts. Later attempts repeat it with the
// rejected output and its issues.
[[nodiscard]] TextGenerationRequest semantic_plan_initial_request(const PromptSpec &request,
                                                                  const PlannerOptions &options = {});

} // namespace qmg
