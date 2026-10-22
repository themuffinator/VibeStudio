#pragma once

#include "qmg/layout.hpp"

namespace qmg {

// Stock game/g_misc.c: misc_teleporter creates a fixed solid disc and a
// separate touch trigger. These are entity bounds, not authored BSP brushes.
[[nodiscard]] inline GridBounds quake2_teleporter_bounds(const GridVec3 origin, const bool trigger) {
    const GridVec3 low = trigger ? GridVec3{-8, -8, 8} : GridVec3{-32, -32, -24};
    const GridVec3 high = trigger ? GridVec3{8, 8, 24} : GridVec3{32, 32, -16};
    const auto translated = [origin](const GridVec3 offset) {
        return GridVec3{checked_grid_coord(static_cast<std::int64_t>(origin.x) + offset.x),
                        checked_grid_coord(static_cast<std::int64_t>(origin.y) + offset.y),
                        checked_grid_coord(static_cast<std::int64_t>(origin.z) + offset.z)};
    };
    return {translated(low), translated(high)};
}

inline constexpr GridCoord kQuake2TeleporterArrivalOffset = 10;

} // namespace qmg
