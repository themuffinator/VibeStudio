#include "app/map_viewport_scene_index.h"

namespace vibestudio {

void MapViewportSceneIndex::rebuild(const LevelMapDocument& document)
{
	m_objects.clear(); m_brushes.clear(); m_sectors.clear();
	m_objects.reserve(document.entities.size() + document.brushes.size() + document.patches.size()
		+ document.doomVertices.size() + document.doomLinedefs.size() + document.doomThings.size() + document.doomSectors.size());
	const auto add = [&](const auto& objects, LevelMapSelectionKind kind) {
		for (qsizetype offset = 0; offset < objects.size(); ++offset) {
			const QPair<int, int> key {int(kind), objects.at(offset).id};
			if (key.second >= 0 && !m_objects.contains(key)) { m_objects.insert(key, offset); }
		}
	};
	add(document.entities, LevelMapSelectionKind::Entity);
	add(document.brushes, LevelMapSelectionKind::QuakeBrush);
	add(document.patches, LevelMapSelectionKind::QuakePatch);
	add(document.doomVertices, LevelMapSelectionKind::DoomVertex);
	add(document.doomLinedefs, LevelMapSelectionKind::DoomLinedef);
	add(document.doomThings, LevelMapSelectionKind::DoomThing);
	add(document.doomSectors, LevelMapSelectionKind::DoomSector);
}

void MapViewportSceneIndex::rebuildGeometry(const QVector<MapBrushGeometry>& brushes, const QVector<DoomSectorOutline>& sectors)
{
	m_brushes.clear(); m_sectors.clear();
	m_brushes.reserve(brushes.size()); m_sectors.reserve(sectors.size());
	for (qsizetype offset = 0; offset < brushes.size(); ++offset) {
		const auto id = brushes.at(offset).brushId;
		if (id >= 0 && !m_brushes.contains(id)) { m_brushes.insert(id, offset); }
	}
	for (qsizetype offset = 0; offset < sectors.size(); ++offset) {
		const auto id = sectors.at(offset).sectorId;
		if (id >= 0 && !m_sectors.contains(id)) { m_sectors.insert(id, offset); }
	}
}

const MapBrushGeometry* MapViewportSceneIndex::brush(const QVector<MapBrushGeometry>& brushes, int id) const
{
	const auto offset = m_brushes.value(id, -1);
	return id >= 0 && offset >= 0 && offset < brushes.size() && brushes.at(offset).brushId == id ? &brushes.at(offset) : nullptr;
}

const DoomSectorOutline* MapViewportSceneIndex::sector(const QVector<DoomSectorOutline>& sectors, int id) const
{
	const auto offset = m_sectors.value(id, -1);
	return id >= 0 && offset >= 0 && offset < sectors.size() && sectors.at(offset).sectorId == id ? &sectors.at(offset) : nullptr;
}

} // namespace vibestudio
