#pragma once

// Map geometry reconstruction shared by the level workbench, the map viewport,
// and map statistics.
//
// Brush solving follows the standard idTech convex-polytope approach: every
// brush face is a half-space, and a face polygon is produced by starting with a
// large quad on the face plane and clipping it against every other face plane.
// The plane-from-three-points convention matches id Software's released qbsp
// sources (ericw-tools `PlaneFromPoints`, upstream at
// https://github.com/ericwa/ericw-tools), the base of VibeStudio's VibeMap2
// compiler submodule.
//
// Doom sector outlines are traced from the linedef/sidedef/sector relationships
// described by the Doom Wiki map format pages
// (https://doomwiki.org/wiki/Linedef, https://doomwiki.org/wiki/Sidedef,
// https://doomwiki.org/wiki/Sector).

#include "core/level_map.h"

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct MapPlane {
	double normalX = 0.0;
	double normalY = 0.0;
	double normalZ = 0.0;
	double distance = 0.0;
	bool valid = false;
};

struct MapFacePolygon {
	int faceIndex = -1;
	QString textureName;
	MapPlane plane;
	QVector<LevelMapVec3> points;

	[[nodiscard]] bool isValid() const;
};

struct MapBrushGeometry {
	int brushId = -1;
	int entityId = -1;
	bool solved = false;
	bool cancelled = false;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	QVector<MapFacePolygon> faces;
	QStringList warnings;

	// Axis-aligned footprint on the XY plane, used by the 2D map viewport.
	[[nodiscard]] QRectF footprint() const;
	[[nodiscard]] QVector<QPolygonF> footprintPolygons() const;
};

struct DoomSectorOutline {
	int sectorId = -1;
	QVector<QPolygonF> loops;
	int openEdgeCount = 0;
	QRectF bounds;
};

struct MapGeometrySummary {
	int brushCount = 0;
	int solvedBrushCount = 0;
	int degenerateBrushCount = 0;
	int faceCount = 0;
	int polygonPointCount = 0;
	int patchCount = 0;
	int sectorOutlineCount = 0;
	int openSectorCount = 0;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	QStringList warnings;
};

// Coverage proofs must not snap near-integer distances or polygon vertices:
// cleanup useful for compiler-style previews can otherwise hide a small gap.
enum class MapGeometryPrecision { CompilerCompatible, PreserveCoordinates };
MapPlane planeFromPoints(const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c,
	MapGeometryPrecision precision = MapGeometryPrecision::CompilerCompatible);
double planeDistanceToPoint(const MapPlane& plane, const LevelMapVec3& point);

// Solves one brush from its face planes. Faces whose polygon is fully clipped
// away are reported as empty polygons rather than dropped, so face indices stay
// aligned with the source brush.
MapBrushGeometry solveBrushGeometry(const QVector<LevelMapBrushFace>& faces, int brushId = -1, int entityId = -1,
	MapGeometryPrecision precision = MapGeometryPrecision::CompilerCompatible, const std::function<bool()>& isCancelled = {});

QVector<MapBrushGeometry> buildLevelMapBrushGeometry(const LevelMapDocument& document);
QVector<DoomSectorOutline> buildDoomSectorOutlines(const LevelMapDocument& document);
MapGeometrySummary summarizeLevelMapGeometry(const LevelMapDocument& document);
QStringList mapGeometrySummaryLines(const MapGeometrySummary& summary);

// Tessellates a quadratic Bezier patch mesh (Quake III patchDef2/patchDef3)
// into a point grid at the requested subdivision level.
QVector<QVector<LevelMapVec3>> tessellatePatchMesh(const LevelMapPatch& patch, int subdivisions = 4);
QVector<QVector<QPointF>> tessellatePatchTexCoords(const LevelMapPatch& patch, int subdivisions = 4);

} // namespace vibestudio
