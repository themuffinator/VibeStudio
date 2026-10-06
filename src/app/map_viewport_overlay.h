#pragma once

#include "app/map_grid.h"
#include "app/map_viewport_projection.h"

namespace vibestudio {

inline constexpr qsizetype mapViewportMaxMemberMarkers = 512;

struct MapViewportOverlayKey {
	quint64 sceneRevision = 0, selectionRevision = 0;
	MapViewportProjection projection = MapViewportProjection::TopXY;
	MapGridView view;
	bool showGrid = false, showMembers = false;
	QRgb selectionColor = 0;
	double markerWidth = 1.4;
};

// Only immutable implicitly shared data crosses the worker boundary. Live
// primary markers, handles, picking and authoring state remain in the widget.
struct MapViewportOverlayRequest {
	MapViewportOverlayKey key;
	LevelMapDocument document;
	QVector<MapBrushGeometry> brushes;
	QVector<DoomSectorOutline> sectors;
	MapViewportSceneIndex index;
	QVector<LevelMapSelectionRef> selection;
	LevelMapSelectionRef primary;
	MapGridFrame previousGrid;
};

struct MapViewportOverlayResult {
	MapViewportOverlayKey key;
	MapGridFrame grid;
	QImage members;
	qsizetype markerCount = 0;
	bool ready = false, failed = false;
};

bool sameMapViewportOverlayKey(const MapViewportOverlayKey& a, const MapViewportOverlayKey& b);
// At most 512 distinct visible member positions; offscreen/coincident members
// do not spend that budget. The primary is painted separately, without a cap.
bool mapViewportMemberCenters(const MapViewportOverlayRequest& request, QVector<QPointF>* centers,
	const std::atomic_bool* cancelled = nullptr);
// Publish only complete images. Failure/cancellation leaves the output intact.
bool renderMapViewportOverlays(const MapViewportOverlayRequest& request, MapViewportOverlayResult* result,
	const std::atomic_bool* cancelled = nullptr);

} // namespace vibestudio
