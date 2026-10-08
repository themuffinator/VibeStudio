#pragma once

#include "core/level_map.h"

#include <QString>
#include <QVector>

namespace vibestudio {

// Doom Builder's stair builder and grid drawing, made through Draw Sector's
// service so new lines join and split the existing ones as a drawn sector's
// do. Both are one undo step that leaves the nodes to rebuild.

// Steps across a footprint, each a sector whose floor rises `stepHeight`
// above the one before, climbing towards "auto" (the far end of the longer
// side), "+x", "-x", "+y" or "-y". Corners are whole units.
struct LevelDoomStairsRequest {
	double minX = 0;
	double minY = 0;
	double maxX = 256;
	double maxY = 128;
	int steps = 8;
	int stepHeight = 8;
	QString rise = QStringLiteral("auto");
};
bool drawLevelMapDoomStairs(LevelMapDocument* document, const LevelDoomStairsRequest& request, QVector<int>* sectorIds = nullptr,
	QString* error = nullptr);

// A footprint cut into `columns` by `rows` sectors.
bool drawLevelMapDoomGrid(LevelMapDocument* document, double minX, double minY, double maxX, double maxY, int columns, int rows,
	QVector<int>* sectorIds = nullptr, QString* error = nullptr);

} // namespace vibestudio
