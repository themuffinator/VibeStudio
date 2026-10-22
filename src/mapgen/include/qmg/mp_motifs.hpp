#pragma once

#include "qmg/layout.hpp"
#include "qmg/semantic_plan.hpp"

#include <array>
#include <vector>

namespace qmg {

struct OpposedBridgeGeometry {
    std::array<GridBounds, 3> decks;
    // Each run is ordered from its lowest to its highest tread.
    std::array<std::vector<GridBounds>, 2> stairs;

    bool operator==(const OpposedBridgeGeometry &) const = default;
};

// Pure room-local construction, returned in world coordinates. Requires floor0,
// rotation0..3, height128/192, local width>=640, local depth>=640/768 respectively,
// at least56 units above the deck and room coordinates within +/-32768.
// Invalid input throws; dimensions and elevation are never silently clamped.
[[nodiscard]] OpposedBridgeGeometry make_opposed_bridge_geometry(GridBounds room, int rotation, int height);

// Seeded original Q2 DM/duel construction. Five-to-eight-room requests offer
// row partitions and non-slicing pinwheels; other counts retain row partitions.
// Every third elevated room uses opposed stairs; other indices keep the existing
// crosswalk/gallery parity. Ordinary floor routes remain beneath these decks.
void build_compact_mp_layout(Layout &layout, const SemanticPlan &semantic_plan);

} // namespace qmg
