#pragma once
#include "app/level_component_gizmo.h"
#include <QPalette>

namespace vibestudio {
struct LevelComponentTraceLabel {
	int axis = -1;
	QRectF bounds;
	QString text;
};
// Original axis-trace renderer after the behaviour of TrenchBroom v2026.2
// MoveHandleDragTracker.h (GPL-3.0-or-later); see docs/CREDITS.md.
// Non-colour labels, bounded layout, no scene/document mutation.
QVector<LevelComponentTraceLabel> paintLevelComponentTrace(QPainter& painter,
	const LevelComponentGizmoFrame& frame,LevelMapVec3 origin,LevelMapVec3 position,
	int constrainedAxis,bool pending,bool highContrast,const QPalette& palette);
} // namespace vibestudio
