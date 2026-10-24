#pragma once

#include "core/box_resize.h"
#include "core/level_map.h"
#include "core/level_region_selection_types.h"
#include <QHash>
#include <QSet>

namespace vibestudio {
struct LevelRegionSelectionRequest {
	LevelRegionSelectionPolicy policy = LevelRegionSelectionPolicy::Bounds;
	LevelMapRegionSelection mode = LevelMapRegionSelection::Inside;
	int axis = 2;
	// Immutable snapshots from the view/catalogue. Scene visibility, locks and
	// group editing context always apply, including in headless callers.
	QSet<QString> excludedObjects;
	QHash<QString, ResizeBox> entityBounds;
};
struct LevelRegionSelectionReport {
	QVector<LevelMapSelectionRef> matches;
	int removedObjects = 0;
};

// Bounds replaces selection without changing geometry. TB uses each convex
// brush and consumes the selected brushes. Radiant uses each brush's AABB:
// Inside consumes all selected objects; Touching adds to the current selection.
// Tall modes project along axis; Radiant tall modes are a VibeStudio extension.
// Consuming operations restore source objects AND selection in one undo step.
// Failure/cancellation never publishes a partial map, selection or report.
bool selectLevelMapByRegion(LevelMapDocument* document, const LevelRegionSelectionRequest& request,
	LevelRegionSelectionReport* report = nullptr, QString* error = nullptr);
} // namespace vibestudio
