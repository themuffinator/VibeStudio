#pragma once

#include "core/level_map.h"
#include "core/map_geometry.h"
#include <QHash>
#include <QPair>

namespace vibestudio {

// Scene-local offsets, never pointers into copy-on-write document arrays. Sibling
// panes share the Qt hashes until a new document/filter generation is adopted.
class MapViewportSceneIndex {
public:
	void rebuild(const LevelMapDocument& document);
	void rebuildGeometry(const QVector<MapBrushGeometry>& brushes, const QVector<DoomSectorOutline>& sectors);

	template<typename T>
	[[nodiscard]] const T* object(const QVector<T>& objects, LevelMapSelectionKind kind, int id) const
	{
		if (id < 0) { return nullptr; }
		// Preserve the document's usual dense-id fast path and its lookup order.
		if (id < objects.size() && objects.at(id).id == id) { return &objects.at(id); }
		const auto offset = m_objects.value({int(kind), id}, -1);
		return offset >= 0 && offset < objects.size() && objects.at(offset).id == id ? &objects.at(offset) : nullptr;
	}
	[[nodiscard]] const MapBrushGeometry* brush(const QVector<MapBrushGeometry>& brushes, int id) const;
	[[nodiscard]] const DoomSectorOutline* sector(const QVector<DoomSectorOutline>& sectors, int id) const;

private:
	QHash<QPair<int, int>, qsizetype> m_objects;
	QHash<int, qsizetype> m_brushes;
	QHash<int, qsizetype> m_sectors;
};

} // namespace vibestudio
