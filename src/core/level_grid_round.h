#pragma once

#include "core/level_brush.h"

#include <QStringList>

namespace vibestudio
{

// Radiant's Snap To Grid: every point rounds onto the grid on its own, so an
// off-grid shape is cleaned up where it stands instead of moving as a whole.
struct LevelMapGridRoundRequest {
	double grid = 16;
	// Empty: round the document selection. Brushes, and the brushes of selected
	// brush entities, round every vertex; patches round every control point;
	// point entities round their origin. Brush entities keep their origin key.
	// Not empty: round only these picked components, all or nothing.
	QVector<LevelMapComponentSet> components;
	// Picked components only; whole objects never merge corners.
	bool allowCollapse = false;
	// Component UV lock, as for component transforms.
	bool textureLock = false, fitUvLock = false, allowValve220 = false;
};

struct LevelMapGridRoundReport {
	int brushes = 0, patches = 0, entities = 0;
	int collapsedVertices = 0, fittedFaces = 0;
	// Objects left as they were, each with its reason: hidden, worldspawn, or
	// a brush the grid would collapse or that cannot keep its UV lock.
	QStringList skipped;
	bool changed() const { return brushes + patches + entities > 0; }
};

// One undo step. Quake-family maps only. Scene locks and the open group are
// checked for the whole edit; hidden objects are skipped with a reason. A
// selection that is already on the grid succeeds without adding history.
// Fails, changing nothing, when every selected object is refused.
bool roundLevelMapSelectionToGrid(LevelMapDocument* document, const LevelMapGridRoundRequest& request,
	LevelMapGridRoundReport* report = nullptr, QString* error = nullptr);

} // namespace vibestudio
