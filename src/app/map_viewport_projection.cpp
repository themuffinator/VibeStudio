#include "app/map_viewport_projection.h"
#include <algorithm>

namespace vibestudio {
namespace {
LevelMapVec3 makeVec(double x, double y, double z)
{
	LevelMapVec3 value;
	value.x = x;
	value.y = y;
	value.z = z;
	value.valid = true;
	return value;
}

QRectF minMaxRect(double minX, double minY, double maxX, double maxY)
{
	return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

// Doom lump records are normally stored in index order, so the identifier is
// usually the index. The linear fallback keeps lookups correct for documents
// whose identifiers were renumbered.
template <typename T>
const T* findById(const QVector<T>& items, int id)
{
	if (id < 0) {
		return nullptr;
	}
	if (id < items.size() && items.at(id).id == id) {
		return &items.at(id);
	}
	for (const T& item : items) {
		if (item.id == id) {
			return &item;
		}
	}
	return nullptr;
}

} // namespace

QPointF mapViewportProjectPoint(MapViewportProjection projection, const LevelMapVec3& point)
{
	switch (projection) {
	case MapViewportProjection::TopXY:
		return QPointF(point.x, point.y);
	case MapViewportProjection::FrontXZ:
		return QPointF(point.x, point.z);
	case MapViewportProjection::SideZY:
		return QPointF(point.y, point.z);
	}
	return QPointF(point.x, point.y);
}

QRectF mapViewportBounds(MapViewportProjection projection, const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	const QPointF a = mapViewportProjectPoint(projection, mins);
	const QPointF b = mapViewportProjectPoint(projection, maxs);
	return minMaxRect(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()), std::max(a.y(), b.y()));
}

const LevelMapDoomSector* mapViewportLineSector(const LevelMapDocument& document, const LevelMapDoomLinedef& linedef)
{
	const LevelMapDoomSidedef* side = findById(document.doomSidedefs, linedef.frontSidedef);
	if (side == nullptr) {
		side = findById(document.doomSidedefs, linedef.backSidedef);
	}
	if (side == nullptr) {
		return nullptr;
	}
	return findById(document.doomSectors, side->sector);
}

// Doom vertices are two-dimensional; elevation views therefore place a linedef
// at the floor height of the sector it fronts (see
// https://doomwiki.org/wiki/Sector). Things carry no height in the vanilla
// format, so they stay at zero.
double mapViewportLineHeight(const LevelMapDocument& document, const LevelMapDoomLinedef& linedef)
{
	const LevelMapDoomSector* sector = mapViewportLineSector(document, linedef);
	return sector != nullptr ? static_cast<double>(sector->floorHeight) : 0.0;
}

bool mapViewportObjectPoint(const LevelMapDocument& document, const QVector<DoomSectorOutline>& outlines,
	const QVector<MapBrushGeometry>& brushGeometry, const MapViewportSceneIndex& index, MapViewportProjection projection, LevelMapSelectionKind kind,
	int id, QPointF* out, const std::atomic_bool* cancelled)
{
	if (out == nullptr || id < 0 || (cancelled && cancelled->load(std::memory_order_relaxed))) {
		return false;
	}
	switch (kind) {
	case LevelMapSelectionKind::None:
		return false;
	case LevelMapSelectionKind::Entity: {
		const LevelMapEntity* entity = index.object(document.entities, kind, id);
		if (entity == nullptr || !entity->origin.valid) {
			return false;
		}
		*out = mapViewportProjectPoint(projection, entity->origin);
		return true;
	}
	case LevelMapSelectionKind::DoomVertex: {
		const LevelMapDoomVertex* vertex = index.object(document.doomVertices, kind, id);
		if (vertex == nullptr) {
			return false;
		}
		*out = mapViewportProjectPoint(projection, makeVec(vertex->x, vertex->y, 0.0));
		return true;
	}
	case LevelMapSelectionKind::DoomLinedef: {
		const LevelMapDoomLinedef* linedef = index.object(document.doomLinedefs, kind, id);
		if (linedef == nullptr) {
			return false;
		}
		const LevelMapDoomVertex* start = index.object(document.doomVertices, LevelMapSelectionKind::DoomVertex, linedef->startVertex);
		const LevelMapDoomVertex* end = index.object(document.doomVertices, LevelMapSelectionKind::DoomVertex, linedef->endVertex);
		if (start == nullptr || end == nullptr) {
			return false;
		}
		const double height = projection == MapViewportProjection::TopXY ? 0.0 : mapViewportLineHeight(document, *linedef);
		const QPointF a = mapViewportProjectPoint(projection, makeVec(start->x, start->y, height));
		const QPointF b = mapViewportProjectPoint(projection, makeVec(end->x, end->y, height));
		*out = QPointF((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
		return true;
	}
	case LevelMapSelectionKind::DoomThing: {
		const LevelMapDoomThing* thing = index.object(document.doomThings, kind, id);
		if (thing == nullptr) {
			return false;
		}
		*out = mapViewportProjectPoint(projection, makeVec(thing->x, thing->y, 0.0));
		return true;
	}
	case LevelMapSelectionKind::DoomSector: {
		if (const auto* outline = index.sector(outlines, id); outline && !outline->bounds.isNull()) {
			*out = outline->bounds.center();
			return true;
		}
		return false;
	}
	case LevelMapSelectionKind::QuakeBrush: {
		if (const auto* brush = index.brush(brushGeometry, id); brush && brush->solved) {
			*out = mapViewportBounds(projection, brush->mins, brush->maxs).center();
			return true;
		}
		const LevelMapBrush* brush = index.object(document.brushes, kind, id);
		if (brush == nullptr || !brush->boundsSolved) {
			return false;
		}
		*out = mapViewportBounds(projection, brush->mins, brush->maxs).center();
		return true;
	}
	case LevelMapSelectionKind::QuakePatch: {
		const LevelMapPatch* patch = index.object(document.patches, kind, id);
		if (patch == nullptr || patch->controlPoints.isEmpty()) {
			return false;
		}
		if (patch->mins.valid && patch->maxs.valid) {
			*out = mapViewportBounds(projection, patch->mins, patch->maxs).center();
			return true;
		}
		QPointF sum(0.0, 0.0);
		for (const LevelMapVec3& point : patch->controlPoints) {
			if (cancelled && cancelled->load(std::memory_order_relaxed)) { return false; }
			sum += mapViewportProjectPoint(projection, point);
		}
		*out = sum / static_cast<double>(patch->controlPoints.size());
		return true;
	}
	}
	return false;
}

} // namespace vibestudio
