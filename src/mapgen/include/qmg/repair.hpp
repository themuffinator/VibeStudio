#pragma once

#include "qmg/layout.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace qmg {

// Why the generic repair engine refused to touch a layout. A refusal is a typed
// fail-closed result: the layout is left exactly as it was and the validation
// issues that asked for repair still stand.
enum class RepairRefusal : std::uint8_t {
    // A compact Q2 DM/duel layout (rooms tagged "compact_mp"). Its placements
    // come from station binding, spawn line-of-sight refinement and motif
    // lights, and its passages and decks from the motif builder. The generic
    // passes (EntityAllocator re-population, axial passage rebuilds, room
    // expansion, generic platforms) cannot preserve those contracts, so repair
    // must re-run the compact stage chain instead; until it does, it refuses.
    compact_mp_layout,
};

[[nodiscard]] std::string to_string(RepairRefusal refusal);

struct RepairOutcome {
    bool changed{};
    std::optional<RepairRefusal> refusal;

    [[nodiscard]] bool refused() const noexcept { return refusal.has_value(); }
};

// True when any room carries the compact builder's "compact_mp" tag. This is
// deliberately wider than the ranker's all-rooms test: a partly tagged layout
// is still refused, so the check fails closed.
[[nodiscard]] bool is_compact_mp_layout(const Layout &layout);

class LayoutRepairEngine {
  public:
    [[nodiscard]] RepairOutcome repair(Layout &layout, const ValidationResult &validation) const;
};

} // namespace qmg
