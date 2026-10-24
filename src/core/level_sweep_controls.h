#pragma once
#include "core/level_sweep.h"

namespace vibestudio
{
// Destination interaction mathematics, independently implemented from the
// pinned TB v2026.2 SweepTool / Grid behavioural reference. See CREDITS.md.
LevelMapVec3 levelSweepDestinationCentre(const LevelSweepSource&, const LevelSweepRequest&);
LevelMapVec3 levelSweepScaleHandle(const LevelSweepSource&, const LevelSweepRequest&);
QVector<QVector<LevelMapVec3>> levelSweepDestinationCaps(const LevelSweepSource&, const LevelSweepRequest&);
bool rotateLevelSweepDestination(LevelSweepRequest*, LevelMapVec3 axis, double degrees, QString* error = nullptr);
bool scaleLevelSweepTo(const LevelSweepSource&, LevelSweepRequest*, LevelMapVec3 position, double grid, QString* error = nullptr);
bool stepLevelSweepScale(const LevelSweepSource&, LevelSweepRequest*, double distance, double grid, QString* error = nullptr);
} // namespace vibestudio
