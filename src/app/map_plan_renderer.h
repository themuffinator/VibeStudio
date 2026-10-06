#pragma once

#include "app/map_plan_wires.h"

class QPainter;
namespace vibestudio {

// Immutable snapshots: no QWidget, painter, source pointer or authoring state
// crosses into the render thread. Qt containers/images share read-only storage.
struct MapPlanRenderRequest {
	quint64 sceneRevision = 0, selectionRevision = 0;
	MapViewportProjection projection = MapViewportProjection::TopXY;
	int worldspawnId = -1;
	LevelMapDocument document;
	QVector<MapBrushGeometry> brushes;
	MapViewportSceneIndex index;
	QVector<LevelMapSelectionRef> selection;
	MapPlanWireFrame view;
	MapPlanWireLimits limits;
	MapPlanWires wires, selectionWires;
	bool wiresComputed = false, selectionWiresComputed = false;
	MapPlanWireFrame baseFrame, selectionFrame;
};

bool sameMapPlanView(const MapPlanWireFrame& a, const MapPlanWireFrame& b);
bool supportedMapPlanFrame(const MapPlanWireFrame& view);
// A completed frame includes patches in the base layer and is published only
// after all geometry has drawn. Cache exhaustion takes the complete painter path.
bool renderMapPlanFrame(const MapPlanRenderRequest& request, bool selected, const MapPlanWires& wires,
	MapPlanWireFrame* frame, const std::atomic_bool* cancelled = nullptr);
// Complete ordinary fallback, also used for physical targets above the bounded
// image size. It never silently reduces resolution or drops source geometry.
bool paintMapPlanFallback(QPainter& painter, const MapPlanRenderRequest& request, bool selected,
	const std::atomic_bool* cancelled = nullptr);

} // namespace vibestudio
