#pragma once

#include "core/map_preview_mesh.h"

namespace vibestudio {

// Separate wall and flat namespaces even when their eight-character names match.
QString doomPreviewMaterialKey(const QString& name, bool flat);

struct DoomPreviewPolygon {
	QString material;
	QVector<LevelMapVec3> points;
	QVector<QPointF> uv;
	LevelMapVec3 normal;
	LevelMapSelectionRef owner;
	LevelMaterialTarget target;
};
struct DoomPreviewGeometry {
	QVector<DoomPreviewPolygon> polygons;
	QStringList warnings;
	int walls = 0, floors = 0, ceilings = 0;
	bool truncated = false, cancelled = false;
};

// No nodes or external compiler required. Sector interiors are decomposed into
// scanline trapezoids, preserving concavities, holes and disconnected islands.
// Invalid/open sectors produce diagnostics rather than a guessed filled polygon.
DoomPreviewGeometry buildDoomPreviewGeometry(const LevelMapDocument& document, const LevelMapPreviewMeshOptions& options = {});

} // namespace vibestudio
