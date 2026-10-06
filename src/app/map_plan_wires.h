#pragma once

#include "app/map_viewport_projection.h"
#include <QLineF>
#include <QImage>
#include <functional>
#include <atomic>

namespace vibestudio {

enum class MapPlanWireStyle { World, Entity, Invalid, Selection };

struct MapPlanWireBatch {
	MapPlanWireStyle style = MapPlanWireStyle::World;
	QRectF bounds;
	QVector<QLineF> lines;
	// Invalid brushes retain closed dashed boxes and crosses, drawn in the
	// original order between world/entity runs, rather than becoming wire edges.
	QVector<QRectF> invalidBrushes;
};

struct MapPlanWireLimits {
	qsizetype sourceEdges = 2 * 1024 * 1024;
	qsizetype uniqueEdges = 256 * 1024;
	qsizetype batches = 64 * 1024;
	qint64 retainedBytes = 32 * 1024 * 1024;
};

struct MapPlanWireStatistics {
	bool ready = false;
	qsizetype sourceEdges = 0;
	qsizetype uniqueEdges = 0;
	qsizetype duplicateEdges = 0;
	qsizetype batches = 0;
	qint64 retainedBytes = 0;
};

struct MapPlanWires {
	QVector<MapPlanWireBatch> batches;
	MapPlanWireStatistics statistics;
};

struct MapPlanWireFrame {
	QImage image;
	QSize viewport;
	QPointF center;
	double zoom = 0, pixelRatio = 0;
	bool highContrast = false;
	QRgb world = 0, entity = 0, invalid = 0, selection = 0, patch = 0;
	QPointF pixelPhase;
};

// World-space drawing data only. Never used for picking, ownership, history,
// saving or compilation. Exact duplicate edges are removed within a contiguous
// pen-style run; style ordering and nearby noncoincident geometry are retained.
// A budget failure returns no partial scene: the caller must draw normally.
MapPlanWires buildMapPlanWires(const LevelMapDocument& document, const QVector<MapBrushGeometry>& brushes,
	const MapViewportSceneIndex& index, MapViewportProjection projection, int worldspawnId,
	const MapPlanWireLimits& limits = {}, const std::atomic_bool* cancelled = nullptr);

// The same projected, tessellated border is used for ordinary and selected
// patches. Coordinates remain in world units until the viewport draws them.
QPolygonF mapPlanPatchOutline(const LevelMapPatch& patch, MapViewportProjection projection);

// Visit visible geometry only. Direct refs use the scene index; selected owners
// expand in one scene pass. An owner plus its child never emits that child twice.
// Invalid brushes emit their parser-bounds perimeter, leaving the warning cross
// in the base drawing intact. False means the visitor stopped before completion.
bool visitMapPlanSelectionOutlines(const LevelMapDocument& document, const QVector<MapBrushGeometry>& brushes,
	const MapViewportSceneIndex& index, MapViewportProjection projection, const QVector<LevelMapSelectionRef>& selection,
	const std::function<bool(const QPolygonF&)>& visit, const std::atomic_bool* cancelled = nullptr);
MapPlanWires buildMapPlanSelectionWires(const LevelMapDocument& document, const QVector<MapBrushGeometry>& brushes,
	const MapViewportSceneIndex& index, MapViewportProjection projection, const QVector<LevelMapSelectionRef>& selection,
	const MapPlanWireLimits& limits = {}, const std::atomic_bool* cancelled = nullptr);

} // namespace vibestudio
