#include "core/map_geometry_cache.h"
#include <QSet>
#include <algorithm>
#include <array>
#include <cstring>

namespace vibestudio {
namespace {
constexpr qsizetype keyByteLimit = 1024 * 1024;
constexpr qsizetype entryLimit = 32768;

// A process-local byte key, never a file format or a probabilistic hash. Compare
// values even when QVector storage is shared: a caller may retain a mutable
// reference across a cache query. Reading pointer identity alone can go stale.
QByteArray geometryKey(const LevelMapBrush& brush, MapGeometryPrecision precision)
{
	// Bound the complete allocation before writing. Unchanged brushes still
	// compare every input value, but need no repeated QByteArray append/detach
	// checks while assembling their process-local key.
	qint64 size = 1 + sizeof(qint64);
	for (const auto& face : brush.faces) {
		if (face.textureName.size() > keyByteLimit / qsizetype(sizeof(QChar))) { return {}; }
		size += 13 * sizeof(double) + sizeof(qint64) + 1 + face.textureName.size() * qint64(sizeof(QChar));
		if (size > keyByteLimit) { return {}; }
	}
	QByteArray key(size, Qt::Uninitialized);
	char* cursor = key.data();
	const auto write = [&cursor](const void* data, qsizetype bytes) {
		if (bytes > 0) { std::memcpy(cursor, data, static_cast<size_t>(bytes)); cursor += bytes; }
	};
	*cursor++ = char(precision);
	const qint64 count = brush.faces.size();
	write(&count, sizeof(count));
	for (const auto& face : brush.faces) {
		const std::array<double, 13> coordinates {face.p0.x, face.p0.y, face.p0.z,
			face.p1.x, face.p1.y, face.p1.z, face.p2.x, face.p2.y, face.p2.z,
			face.planeNormal.x, face.planeNormal.y, face.planeNormal.z, face.planeDistance};
		const qint64 textBytes = face.textureName.size() * qint64(sizeof(QChar));
		write(coordinates.data(), sizeof(coordinates));
		*cursor++ = char(int(face.p0.valid) | (int(face.p1.valid) << 1) | (int(face.p2.valid) << 2)
			| (int(face.explicitPlane) << 3) | (int(face.planeNormal.valid) << 4));
		write(&textBytes, sizeof(textBytes));
		write(face.textureName.utf16(), textBytes);
	}
	Q_ASSERT(cursor == key.constData() + key.size());
	return key;
}

qint64 entryBytes(const QByteArray& key, const MapBrushGeometry& geometry)
{
	// Account for capacities and owned text, plus conservative entry/container
	// overhead. This bounds retained payload, not allocator/process resident size.
	qint64 bytes = 256 + key.capacity() + qint64(geometry.faces.capacity()) * sizeof(MapFacePolygon);
	for (const auto& face : geometry.faces) {
		bytes += qint64(face.points.capacity()) * sizeof(LevelMapVec3) + 64 + face.textureName.size() * qint64(sizeof(QChar));
	}
	return bytes;
}
} // namespace

MapBrushGeometryCache::MapBrushGeometryCache(qint64 byteLimit) : m_byteLimit(std::max<qint64>(0, byteLimit)) {}

void MapBrushGeometryCache::clear()
{
	m_entries.clear(); m_retainedBytes = 0; m_statistics = {};
}

void MapBrushGeometryCache::beginBuild(const LevelMapDocument& document)
{
	m_statistics = {};
	QSet<int> ids; ids.reserve(document.brushes.size());
	for (const auto& brush : document.brushes) { ids.insert(brush.id); }
	QVector<int> removed;
	// Do not detach a shared table merely to inspect it. An unchanged scene can
	// reuse the whole table when a sibling pane becomes the editing source.
	for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
		if (!ids.contains(it.key())) { removed.append(it.key()); }
	}
	for (const int id : removed) { m_retainedBytes -= m_entries.value(id).bytes; m_entries.remove(id); }
}

MapBrushGeometry MapBrushGeometryCache::resolve(const LevelMapBrush& brush, MapGeometryPrecision precision,
	const std::function<bool()>& isCancelled)
{
	if (isCancelled && isCancelled()) {
		MapBrushGeometry result; result.brushId = brush.id; result.entityId = brush.entityId; result.cancelled = true;
		return result;
	}
	const auto key = m_byteLimit > 0 ? geometryKey(brush, precision) : QByteArray();
	const auto found = m_entries.constFind(brush.id);
	if (!key.isEmpty() && found != m_entries.cend() && found->key == key) {
		++m_statistics.reused;
		auto geometry = found->geometry;
		geometry.entityId = brush.entityId;
		return geometry;
	}
	++m_statistics.solved;
	auto geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId, precision, isCancelled);
	if (geometry.cancelled) { return geometry; }
	if (found != m_entries.cend()) { m_retainedBytes -= found->bytes; m_entries.remove(brush.id); }
	// Recompute diagnostics instead of retaining translated warnings across a
	// language change. Reaching a cache limit never omits or simplifies geometry.
	if (!key.isEmpty() && geometry.solved && geometry.warnings.isEmpty() && m_entries.size() < entryLimit) {
		const auto bytes = entryBytes(key, geometry);
		if (bytes <= m_byteLimit - m_retainedBytes) {
			m_entries.insert(brush.id, {key, geometry, bytes}); m_retainedBytes += bytes;
		}
	}
	return geometry;
}

QVector<MapBrushGeometry> MapBrushGeometryCache::build(const LevelMapDocument& document, MapGeometryPrecision precision)
{
	beginBuild(document);
	QVector<MapBrushGeometry> result; result.reserve(document.brushes.size());
	for (const auto& brush : document.brushes) { result.append(resolve(brush, precision)); }
	return result;
}

MapBrushGeometryCacheStatistics MapBrushGeometryCache::statistics() const
{
	auto statistics = m_statistics;
	statistics.retainedBrushes = int(m_entries.size()); statistics.retainedBytes = m_retainedBytes;
	return statistics;
}
} // namespace vibestudio
