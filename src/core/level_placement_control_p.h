#pragma once

#include "core/level_placement.h"

namespace vibestudio::detail {
// Only prepareLevelPlacement installs this scoped, thread-local control. The
// internal checkpoint exception never crosses that public transaction boundary.
// Existing direct authoring calls run without an installed control.
struct PlacementCancelled {};
class PlacementControlScope {
  public:
	explicit PlacementControlScope(const LevelPlacementControl& control);
	~PlacementControlScope();
	PlacementControlScope(const PlacementControlScope&) = delete;
	PlacementControlScope& operator=(const PlacementControlScope&) = delete;

  private:
	const LevelPlacementControl* m_previous = nullptr;
};
void placementCancellationCheckpoint();
void placementCheckpoint(LevelPlacementPhase phase, qint64 completed = 0, qint64 total = 0);
} // namespace vibestudio::detail
