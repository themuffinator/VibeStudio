#pragma once

#include "core/level_map.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace vibestudio {

// A compiler's visibility portal file: the windows between the map's leaves
// (or clusters) that vis tests sight lines through. Quake's qbsp and
// ericw-tools write PRT1, PRT1-AM and PRT2; Quake II's and Quake III's
// compilers write PRT1, q3map adding its solid faces after the portals.
// The header orders follow ericw-tools' reader, common/prtfile.cc: PRT1
// leaves and portals, PRT2 leaves, clusters and portals, and PRT1-AM
// clusters, portals and leaves.
struct PortalFile {
	bool valid = false;
	QString error;
	QString format;
	int leafCount = 0;
	int clusterCount = 0;
	// The portals as polygons, each wound as the file lists its points.
	QVector<QVector<LevelMapVec3>> portals;
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
};

[[nodiscard]] PortalFile parsePortalFile(const QByteArray& bytes);
[[nodiscard]] PortalFile loadPortalFile(const QString& path);

} // namespace vibestudio
