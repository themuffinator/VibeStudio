#pragma once

#include "core/box_resize.h"
#include "core/level_brush_points.h"
#include "core/map_geometry.h"

#include <QPair>
#include <QVector>

namespace vibestudio {
// The Assemble Brush tool's geometry: TrenchBroom's Brush Tool collects
// points clicked on surfaces or a construction plane, faces drawn on a plane
// and flat outlines extruded along their normal, then Create Brush adds the
// convex brush enclosing them (addLevelMapBrushFromPoints) as one undo step.
// These functions only measure and propose points; nothing here edits a map.
// Workflow reference: TB AssembleBrushTool and AssembleBrushToolController3D
// at 4c756be1 (GPL-3.0-or-later). Original implementation; see
// docs/CREDITS.md.

enum class LevelBrushAssembleShape { Empty, Point, Segment, Polygon, Volume };

struct LevelBrushAssembleOutline {
	LevelBrushAssembleShape shape = LevelBrushAssembleShape::Empty;
	// The distinct corners: the one point, the segment's two ends, the flat
	// outline's corners in order around `plane`'s normal, or the hull's corners.
	QVector<LevelMapVec3> corners;
	// The wire to draw: the segment, the outline's sides or the hull's edges.
	QVector<QPair<LevelMapVec3, LevelMapVec3>> edges;
	// A flat outline's plane; its normal follows the corners' winding.
	MapPlane plane;
	// A hull's faces, and the points it absorbs (duplicates, interior points
	// and points on its faces or edges).
	int faces = 0;
	int unusedPoints = 0;
};

// What up to 4096 finite points within ±32768 units make, with the same
// tolerance as levelBrushPointsHull(). A volume's hull is solved only when
// `solveVolume` is set (it is the costly part); otherwise a volume reports its
// shape alone. Fails on bad input, or on a hull beyond the 128-face /
// 256-vertex limits, leaving the shape set.
bool levelBrushAssembleOutline(const QVector<LevelMapVec3>& points, LevelBrushAssembleOutline* outline, QString* error = nullptr,
	bool solveVolume = true);

// A brush face under the pointer, or an axis construction plane when nothing
// is under it (an empty winding).
struct LevelBrushAssembleSurface {
	MapPlane plane;
	QVector<LevelMapVec3> winding;
};

// The dominant axis of a plane's normal: 0, 1 or 2.
int levelBrushAssembleDominantAxis(const MapPlane& plane);

// TB Grid::snap(point, plane): the two coordinates across the plane's
// dominant axis round to the grid and the third follows the plane. Where
// rounding would leave the face (a coarse grid at its edge), the hit itself is
// kept. A grid of 0 keeps the hit.
LevelMapVec3 levelBrushAssembleSurfacePoint(const LevelMapVec3& hit, const LevelBrushAssembleSurface& surface, double grid);

// TB DrawFaceDragDelegate: the rectangle between two points of a plane.
// Across the plane's dominant axis the low corner rounds down and the high
// corner up to the grid; each corner then lies on the plane. Four points,
// which coincide in pairs when the drag stays on one grid line.
QVector<LevelMapVec3> levelBrushAssembleRectangle(const MapPlane& plane, const LevelMapVec3& from, const LevelMapVec3& to, double grid);

// Where a ray meets a plane, ahead of a forward-only ray's origin.
bool levelBrushAssembleRayPlane(const BoxResizeRay& ray, const MapPlane& plane, LevelMapVec3* point);
// Where a ray meets a flat outline, from either side (TB Polyhedron::pickFace).
bool levelBrushAssembleRayPolygon(const BoxResizeRay& ray, const LevelBrushAssembleOutline& outline, LevelMapVec3* point);

// TB DuplicateFaceDragDelegate: the signed distance along the unit `normal`
// from `start` to the point of that line nearest the ray, rounded to the grid
// (0 keeps it exact). Fails when the ray runs along the line.
bool levelBrushAssembleExtrusionDistance(const BoxResizeRay& ray, const LevelMapVec3& start, const LevelMapVec3& normal, double grid,
	double* distance);

// The points followed by copies of them moved by `offset`: a flat outline
// swept into a prism (Shift-drag, map add-brush --points --extrude).
QVector<LevelMapVec3> levelBrushPointsExtruded(const QVector<LevelMapVec3>& points, const LevelMapVec3& offset);

// Appends the points not already present exactly. Fails, appending nothing,
// when the result would exceed kLevelBrushPointsMaximum points.
bool levelBrushAssembleAppend(QVector<LevelMapVec3>* points, const QVector<LevelMapVec3>& added, int* appended = nullptr);
} // namespace vibestudio
