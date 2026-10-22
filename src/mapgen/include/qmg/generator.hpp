#pragma once

#include "qmg/layout.hpp"
#include "qmg/semantic_plan.hpp"

#include <cstddef>
#include <vector>

namespace qmg {

class LayoutGenerator {
  public:
    [[nodiscard]] std::vector<Layout> generate_candidates(const PromptSpec &spec, const SemanticPlan &semantic_plan,
                                                          std::size_t count) const;

  private:
    [[nodiscard]] Layout generate_one(const PromptSpec &spec, const SemanticPlan &semantic_plan,
                                      std::uint64_t candidate_seed) const;
};

} // namespace qmg
