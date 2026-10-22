#pragma once

#include "qmg/layout.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qmg {

struct SpawnRefinementChoices {
    PlacementId id;
    std::vector<GridVec3> candidates;
};

struct SpawnRefinementObjective {
    std::uint32_t visible_pairs{};
    std::uint32_t close_pairs{};
    std::uint64_t squared_distance_deficit{};
    auto operator<=>(const SpawnRefinementObjective &) const = default;
};

struct SpawnRefinementResult {
    SpawnRefinementObjective before;
    SpawnRefinementObjective after;
    std::size_t accepted_moves{};
    std::size_t tested_candidates{};
    std::size_t passes{};
};

// Bounded local policy for marked 2–8-room Q2 DM/duel AABB construction only;
// Rooms share one shell-floor plane; passages may start at or above it.
// This preserves in-room floor support, not elevated passage-floor support.
// Candidate origins are untrusted: retain each initial room/floor, certify a
// complete32x32x56 standing hull and >=64 distance to other point anchors.
// At most24 nearest eligible stations and two passes. Preserve every known
// blocked pair; minimize visible pairs, pairs below the existing256 distance,
// then their squared-distance deficit. Move only for strict tuple improvement.
// Invalid construction/choice identities throw without changing the layout;
// inadmissible candidate stations are skipped. Only spawn origins can change.
[[nodiscard]] SpawnRefinementResult refine_compact_mp_spawns(Layout &layout,
                                                             const std::vector<SpawnRefinementChoices> &choices);

} // namespace qmg
