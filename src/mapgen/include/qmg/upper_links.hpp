#pragma once

#include "qmg/layout.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace qmg {

struct UpperLinkSelection {
    ConnectionId connection;
    RoomId from_room;
    RoomId to_room;
    SolidId from_deck;
    SolidId to_deck;
    GridCoord floor_z{};
    GridBounds passage;
    GridVec3 from_feet;
    GridVec3 to_feet;
    std::vector<SolidId> added_solids;

    bool operator==(const UpperLinkSelection &) const = default;
};

struct UpperLinkResult {
    bool applicable{};
    std::size_t considered_strips{};
    // Source support/clearance candidates, before emitted-shell feasibility.
    std::size_t feasible_strips{};
    std::size_t lowering_attempts{};
    std::size_t lowering_rejections{};
    bool lowering_cap_reached{};
    std::optional<UpperLinkSelection> selected;

    bool operator==(const UpperLinkResult &) const = default;
};

// Append at most one supported upper face connection between existing decks.
// Scope: marked wrapped Q2 DM/duel,5–8 rooms, planned verticality >=0.3.
// Call before placements are allocated. Unsupported requests are unchanged;
// malformed in-scope construction throws without changing the caller.
// A missing selection is a retained no-compatible-pair observation.
// At most64 feasible strips, ordered by a stable seeded key, are passed to
// MapBuilder. Shell-clearance failures are counted; exhausting that bounded
// prefix returns unchanged geometry, rather than rerolling or ranking layouts.
// The source construction certificate is not compiled/native movement evidence.
[[nodiscard]] UpperLinkResult add_compact_upper_link(Layout &layout);

} // namespace qmg
