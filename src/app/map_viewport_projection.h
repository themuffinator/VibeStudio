#pragma once

#include "app/map_viewport_scene_index.h"
#include <atomic>

namespace vibestudio {

enum class MapViewportProjection { TopXY, FrontXZ, SideZY };

// Shared projection semantics for live interaction and immutable render snapshots.
QPointF mapViewportProjectPoint(MapViewportProjection projection, const LevelMapVec3& point);
QRectF mapViewportBounds(MapViewportProjection projection, const LevelMapVec3& mins, const LevelMapVec3& maxs);
const LevelMapDoomSector* mapViewportLineSector(const LevelMapDocument& document, const LevelMapDoomLinedef& linedef);
double mapViewportLineHeight(const LevelMapDocument& document, const LevelMapDoomLinedef& linedef);
bool mapViewportObjectPoint(const LevelMapDocument& document, const QVector<DoomSectorOutline>& outlines,
	const QVector<MapBrushGeometry>& brushes, const MapViewportSceneIndex& index, MapViewportProjection projection,
	LevelMapSelectionKind kind, int id, QPointF* out, const std::atomic_bool* cancelled = nullptr);

} // namespace vibestudio
