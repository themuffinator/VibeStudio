#pragma once

#include "core/level_brush.h"
#include <QJsonObject>

namespace vibestudio {
struct LevelWrapRequest {
	bool componentsOnly = false;
	QVector<LevelMapComponentSet> components;
	QVector<LevelMapVec3> points; // Up to three actual extra points; never a synthetic clip plane point.
	QString texture;
};
struct LevelWrapReport {
	bool sourcesRetained = false;
	int brushId = -1;
	int sourceBrushes = 0, sourcePatches = 0;
	int vertices = 0, faces = 0, inheritedFaces = 0, newFaces = 0;
};
// NRC CSG_WrapMerge: whole brushes are replaced; picked brush components and
// patch controls produce a new hull and retain all originals. Component hulls
// use the current material/default UVs on every face, with no inherited flags.
// The last selected object's native owner and scene membership are the
// destination (last component set when selection is empty; a point-entity
// selection falls back to worldspawn/current creation context). One atomic undo,
// shared scene protection and linked propagation. Up to 64 source objects,
// 4,096 input points, 128 faces / 256 output vertices within ±32768 units.
bool wrapLevelMap(LevelMapDocument* document, const LevelWrapRequest& request,
	LevelWrapReport* report = nullptr, QString* error = nullptr);
QJsonObject levelWrapReportJson(const LevelWrapReport& report);
} // namespace vibestudio
