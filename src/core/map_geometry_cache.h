#pragma once

#include "core/map_geometry.h"
#include <QByteArray>
#include <QHash>

namespace vibestudio {

struct MapBrushGeometryCacheStatistics {
	int solved = 0;
	int reused = 0;
	int retainedBrushes = 0;
	qint64 retainedBytes = 0;
};

// Caller-owned, copyable geometry cache. Copies use Qt's implicit sharing;
// separate copies can be used on different threads, never one mutable instance.
// Exact input keys include every plane input, material name and precision mode.
// UVs, selection and ownership remain live document data, not cached editor state.
class MapBrushGeometryCache {
public:
	explicit MapBrushGeometryCache(qint64 byteLimit = 64LL * 1024LL * 1024LL);
	void clear();
	// Starts statistics for a new build and drops objects absent from this map.
	// Call once even when a preview's triangle/cancellation budget ends it early.
	void beginBuild(const LevelMapDocument& document);
	MapBrushGeometry resolve(const LevelMapBrush& brush,
		MapGeometryPrecision precision = MapGeometryPrecision::CompilerCompatible, const std::function<bool()>& isCancelled = {});
	QVector<MapBrushGeometry> build(const LevelMapDocument& document,
		MapGeometryPrecision precision = MapGeometryPrecision::CompilerCompatible);
	[[nodiscard]] MapBrushGeometryCacheStatistics statistics() const;

private:
	struct Entry {
		QByteArray key;
		MapBrushGeometry geometry;
		qint64 bytes = 0;
	};
	QHash<int, Entry> m_entries;
	qint64 m_byteLimit = 0;
	qint64 m_retainedBytes = 0;
	MapBrushGeometryCacheStatistics m_statistics;
};

} // namespace vibestudio
