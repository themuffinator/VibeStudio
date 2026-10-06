#pragma once

#include "core/level_document.h"
#include <algorithm>
#include <cmath>

namespace vibestudio::tests {
// Original synthetic geometry, laid out on a grid. No game data is required.
inline bool createGeometryFixture(int count, LevelMapDocument* map, QString* error)
{
	LevelMapCreateRequest request; request.starterRoom = false;
	if (!createLevelMap(request, map, error)
		|| !addLevelMapBoxBrush(map, {-16, -16, -16, true}, {16, 16, 16, true}, QStringLiteral("studio/cache"), nullptr, error)) { return false; }
	const auto seed = map->brushes.front();
	map->brushes.clear(); map->brushes.reserve(count);
	const int columns = std::max(1, int(std::ceil(std::sqrt(double(count)))));
	for (int i = 0; i < count; ++i) {
		auto brush = seed; brush.id = i;
		const double x = (i % columns) * 48.0, y = (i / columns) * 48.0;
		for (auto& face : brush.faces) {
			for (auto* point : {&face.p0, &face.p1, &face.p2}) { point->x += x; point->y += y; }
		}
		brush.mins.x += x; brush.mins.y += y; brush.maxs.x += x; brush.maxs.y += y;
		brush.geometryDirty = true; brush.selected = false;
		map->brushes.append(std::move(brush));
	}
	map->selection.clear(); map->selectionKind = LevelMapSelectionKind::None; map->selectedObjectId = -1;
	map->undoStack.clear(); map->redoStack.clear();
	return true;
}
} // namespace vibestudio::tests
