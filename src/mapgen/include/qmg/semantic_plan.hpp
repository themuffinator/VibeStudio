#pragma once

#include "qmg/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qmg {

inline constexpr int kSemanticPlanSchemaVersion = 1;
inline constexpr std::string_view kSemanticPlanSchemaId = "qmg.semantic_plan";

enum class RouteStyle {
    linear,
    loop,
};

enum class Symmetry {
    none,
    soft,
    strict,
};

enum class LightingStyle {
    low_key,
    balanced,
    high_contrast,
};

enum class RoomRole {
    start,
    exit,
    traversal,
    combat,
    resource,
    setpiece,
    spawn,
    connector,
    control,
    vertical,
    objective,
    arena,
};

struct SemanticPlan {
    int schema_version{kSemanticPlanSchemaVersion};
    RouteStyle route_style{RouteStyle::linear};
    Symmetry symmetry{Symmetry::none};
    LightingStyle lighting_style{LightingStyle::balanced};
    int verticality{50};
    int intensity{50};
    std::vector<RoomRole> room_roles;
};

struct PlanIssue {
    std::string code;
    std::string path;
    std::string message;
};

struct SemanticPlanParseResult {
    std::optional<SemanticPlan> plan;
    std::vector<PlanIssue> issues;

    [[nodiscard]] bool ok() const { return plan.has_value() && issues.empty(); }
};

[[nodiscard]] SemanticPlan make_rule_semantic_plan(const PromptSpec &request);
[[nodiscard]] SemanticPlanParseResult parse_semantic_plan(std::string_view json);
[[nodiscard]] std::vector<PlanIssue> validate_semantic_plan(const SemanticPlan &plan, const PromptSpec &request);
[[nodiscard]] std::string render_semantic_plan(const SemanticPlan &plan, bool pretty = false);
[[nodiscard]] std::string build_semantic_plan_grammar(const PromptSpec &request);
// VibeStudio: build_semantic_plan_grammar's contract as a JSON Schema for structured output. It holds
// what a schema can (allowed enum values, the requested room count, the verticality and intensity
// windows); per-position roles stay in the prompt and validate_semantic_plan.
[[nodiscard]] std::string build_semantic_plan_json_schema(const PromptSpec &request);

[[nodiscard]] std::string to_string(RouteStyle value);
[[nodiscard]] std::string to_string(Symmetry value);
[[nodiscard]] std::string to_string(LightingStyle value);
[[nodiscard]] std::string to_string(RoomRole value);

} // namespace qmg
