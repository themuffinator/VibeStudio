#pragma once

#include "core/map_geometry.h"

namespace vibestudio
{

// Component IDs describe the current solved brush, not the arbitrary three
// plane points in a .map file. Re-query after a topology-changing edit.
enum class LevelBrushComponent { Vertex, Edge, Face };

inline constexpr int kLevelBrushComponentMaxFaces = 128;
inline constexpr int kLevelBrushComponentMaxVertices = 256;

struct LevelBrushTopology {
	QVector<LevelMapVec3> vertices;	   // lexicographic XYZ order
	QVector<std::array<int, 2>> edges; // ascending endpoint IDs, sorted
	QVector<QVector<int>> faces;	   // source face order, winding vertex IDs
	MapBrushGeometry geometry;
};

struct LevelBrushEditReport {
	int verticesBefore = 0;
	int verticesAfter = 0;
	int facesBefore = 0;
	int facesAfter = 0;
	int collapsedVertices = 0;
	bool changed = false;
};

bool levelBrushTopology(const LevelMapBrush &brush, LevelBrushTopology *topology, QString *error = nullptr);
// Original shared convex-hull builder used by primitive creation. Points must
// be finite world coordinates; the result is a closed classic geometry draft.
// Document insertion supplies the target texture dialect and source bindings.
bool createLevelBrushHull(const QVector<LevelMapVec3> &points, const QString &texture, LevelMapBrush *brush, QString *error = nullptr);
QVector<int> levelBrushComponentVertices(const LevelBrushTopology &topology, LevelBrushComponent kind, const QVector<int> &components);
// Rebuilds the convex hull after moving the requested components. Coplanar
// triangles merge; bent faces split. New faces inherit mapping/flags from the
// best matching source face. Texture coordinates stay in their stored frame;
// this operation does not promise texture lock on a deformed face.
// A disappearing/merged vertex requires allowCollapse. Flat, open, non-finite
// and out-of-world candidates are always rejected without mutating the brush.
bool moveLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components, const LevelMapVec3 &delta,
							  double grid = 0, bool allowCollapse = false, LevelBrushEditReport *report = nullptr,
							  QString *error = nullptr);
// Commits a geometry draft through the map's shared undo/persistence service.
// Face source bindings must originate from this brush; material syntax, flags,
// comments and primitive dialect belong to the document and are retained.
bool replaceLevelMapBrushGeometry(LevelMapDocument *document, int brushId, const LevelMapBrush &replacement, QString *error = nullptr);

} // namespace vibestudio
