#include "app/map_plan_wires.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <optional>

namespace vibestudio {
namespace {
constexpr qsizetype kBatchLines = 2048;

QPointF project(const LevelMapVec3& point, MapViewportProjection projection)
{
	if (projection == MapViewportProjection::FrontXZ) { return {point.x, point.z}; }
	if (projection == MapViewportProjection::SideZY) { return {point.y, point.z}; }
	return {point.x, point.y};
}

// QPointF equality is fuzzy. Drawing deduplication deliberately uses exact
// scalar equality, so even very close separate edges remain visible/pickable.
struct Edge {
	double x1, y1, x2, y2;
	bool operator==(const Edge& other) const
	{
		return x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2;
	}
};
size_t qHash(const Edge& edge, size_t seed = 0) noexcept
{
	return qHashMulti(seed, edge.x1, edge.y1, edge.x2, edge.y2);
}
Edge edgeKey(QPointF a, QPointF b)
{
	if (a.x() > b.x() || (a.x() == b.x() && a.y() > b.y())) { std::swap(a, b); }
	// Signed zero is geometrically identical and must also hash identically.
	return {a.x() == 0 ? 0.0 : a.x(), a.y() == 0 ? 0.0 : a.y(),
		b.x() == 0 ? 0.0 : b.x(), b.y() == 0 ? 0.0 : b.y()};
}
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
void include(QRectF* bounds, QRectF area, bool first)
{
	area = area.normalized();
	*bounds = first ? area : QRectF(QPointF(std::min(bounds->left(), area.left()), std::min(bounds->top(), area.top())),
		QPointF(std::max(bounds->right(), area.right()), std::max(bounds->bottom(), area.bottom())));
}

class WireBuilder {
	const MapPlanWireLimits& limits;
	const std::atomic_bool* cancelled;
	MapPlanWires result;
	QSet<Edge> seen;
	std::optional<MapPlanWireStyle> currentStyle;
	qint64 payloadBytes = 0;
	bool budget() const {
		return !(cancelled && cancelled->load(std::memory_order_relaxed))
			&& result.statistics.sourceEdges <= limits.sourceEdges && result.statistics.uniqueEdges <= limits.uniqueEdges
			&& result.batches.size() <= limits.batches
			&& payloadBytes + result.batches.capacity() * qint64(sizeof(MapPlanWireBatch)) <= limits.retainedBytes;
	}
	void startBatch(MapPlanWireStyle style) {
		if (!currentStyle || *currentStyle != style) { seen.clear(); currentStyle = style; }
		if (result.batches.isEmpty() || result.batches.last().style != style
			|| result.batches.last().lines.size() >= kBatchLines || result.batches.last().invalidBrushes.size() >= kBatchLines) {
			MapPlanWireBatch batch; batch.style = style; result.batches.append(std::move(batch));
		}
	}
public:
	explicit WireBuilder(const MapPlanWireLimits& value, const std::atomic_bool* stop) : limits(value), cancelled(stop) {}
	bool appendEdge(QPointF a, QPointF b, MapPlanWireStyle style) {
		if (!finite(a) || !finite(b) || ++result.statistics.sourceEdges > limits.sourceEdges) { return false; }
		startBatch(style);
		const auto key = edgeKey(a, b);
		if (seen.contains(key)) { ++result.statistics.duplicateEdges; return budget(); }
		if (++result.statistics.uniqueEdges > limits.uniqueEdges) { return false; }
		seen.insert(key);
		auto& batch = result.batches.last();
		include(&batch.bounds, QRectF(a, b), batch.lines.isEmpty());
		const auto previousCapacity = batch.lines.capacity();
		batch.lines.append(QLineF(a, b));
		payloadBytes += (batch.lines.capacity() - previousCapacity) * qint64(sizeof(QLineF));
		return budget();
	}
	bool appendPolygon(const QPolygonF& polygon, MapPlanWireStyle style) {
		if (polygon.size() < 2) { return true; }
		for (qsizetype i = 0; i < polygon.size(); ++i) {
			if (!appendEdge(polygon.at(i), polygon.at((i + 1) % polygon.size()), style)) { return false; }
		}
		return true;
	}
	bool appendInvalid(QRectF bounds) {
		startBatch(MapPlanWireStyle::Invalid);
		auto& batch = result.batches.last();
		include(&batch.bounds, bounds, batch.invalidBrushes.isEmpty());
		const auto previousCapacity = batch.invalidBrushes.capacity();
		batch.invalidBrushes.append(bounds);
		payloadBytes += (batch.invalidBrushes.capacity() - previousCapacity) * qint64(sizeof(QRectF));
		return budget();
	}
	MapPlanWires finish() {
		result.statistics.ready = budget();
		result.statistics.batches = result.batches.size();
		result.statistics.retainedBytes = payloadBytes + result.batches.capacity() * qint64(sizeof(MapPlanWireBatch));
		return result.statistics.ready ? std::move(result) : MapPlanWires();
	}
};
}

MapPlanWires buildMapPlanWires(const LevelMapDocument& document, const QVector<MapBrushGeometry>& brushes,
	const MapViewportSceneIndex& index, MapViewportProjection projection, int worldspawnId, const MapPlanWireLimits& limits,
	const std::atomic_bool* cancelled)
{
	WireBuilder builder(limits, cancelled);
	for (const auto& brush : brushes) {
		if (cancelled && cancelled->load(std::memory_order_relaxed)) { return {}; }
		if (!brush.solved) {
			const auto* source = index.object(document.brushes, LevelMapSelectionKind::QuakeBrush, brush.brushId);
			if (!source || !source->boundsSolved) { continue; }
			const auto a = project(source->mins, projection), b = project(source->maxs, projection);
			if (!finite(a) || !finite(b)) { return {}; }
			if (!builder.appendInvalid(QRectF(a, b).normalized())) { return {}; }
			continue;
		}
		const auto style = brush.entityId < 0 || brush.entityId == worldspawnId ? MapPlanWireStyle::World : MapPlanWireStyle::Entity;
		if (projection == MapViewportProjection::TopXY) {
			for (const auto& polygon : brush.footprintPolygons()) { if (!builder.appendPolygon(polygon, style)) { return {}; } }
		} else {
			for (const auto& face : brush.faces) {
				if (face.points.size() < 3) { continue; }
				for (qsizetype i = 0; i < face.points.size(); ++i) {
					if (!builder.appendEdge(project(face.points.at(i), projection), project(face.points.at((i + 1) % face.points.size()), projection), style)) { return {}; }
				}
			}
		}
	}
	return builder.finish();
}

QPolygonF mapPlanPatchOutline(const LevelMapPatch& patch, MapViewportProjection projection)
{
	if (patch.width < 2 || patch.height < 2) { return {}; }
	const auto mesh = tessellatePatchMesh(patch, 3);
	if (mesh.isEmpty() || mesh.first().isEmpty()) { return {}; }
	const auto rows = mesh.size(), columns = mesh.first().size();
	QPolygonF polygon;
	for (qsizetype column = 0; column < columns; ++column) { polygon.append(project(mesh.first().at(column), projection)); }
	for (qsizetype row = 1; row < rows; ++row) {
		if (mesh.at(row).size() == columns) { polygon.append(project(mesh.at(row).at(columns - 1), projection)); }
	}
	for (qsizetype column = columns - 2; column >= 0; --column) {
		if (mesh.last().size() == columns) { polygon.append(project(mesh.last().at(column), projection)); }
	}
	for (qsizetype row = rows - 2; row >= 1; --row) {
		if (!mesh.at(row).isEmpty()) { polygon.append(project(mesh.at(row).first(), projection)); }
	}
	return polygon.size() >= 3 ? polygon : QPolygonF();
}

bool visitMapPlanSelectionOutlines(const LevelMapDocument& document, const QVector<MapBrushGeometry>& brushes,
	const MapViewportSceneIndex& index, MapViewportProjection projection, const QVector<LevelMapSelectionRef>& selection,
	const std::function<bool(const QPolygonF&)>& visit, const std::atomic_bool* cancelled)
{
	QSet<int> owners, emittedBrushes, emittedPatches;
	const auto emitBrush = [&](const MapBrushGeometry& brush) {
		if (cancelled && cancelled->load(std::memory_order_relaxed)) { return false; }
		if (emittedBrushes.contains(brush.brushId)) { return true; }
		emittedBrushes.insert(brush.brushId);
		if (!brush.solved) {
			const auto* source = index.object(document.brushes, LevelMapSelectionKind::QuakeBrush, brush.brushId);
			if (!source || !source->boundsSolved) { return true; }
			const auto bounds = QRectF(project(source->mins, projection), project(source->maxs, projection)).normalized();
			return visit({bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()});
		}
		if (projection == MapViewportProjection::TopXY) {
			for (const auto& polygon : brush.footprintPolygons()) { if (!visit(polygon)) { return false; } }
		} else {
			for (const auto& face : brush.faces) {
				if (cancelled && cancelled->load(std::memory_order_relaxed)) { return false; }
				if (face.points.size() < 3) { continue; }
				QPolygonF polygon; polygon.reserve(face.points.size());
				for (const auto& point : face.points) { polygon.append(project(point, projection)); }
				if (!visit(polygon)) { return false; }
			}
		}
		return true;
	};
	const auto emitPatch = [&](const LevelMapPatch& patch) {
		if (cancelled && cancelled->load(std::memory_order_relaxed)) { return false; }
		if (emittedPatches.contains(patch.id)) { return true; }
		emittedPatches.insert(patch.id);
		return visit(mapPlanPatchOutline(patch, projection));
	};
	for (const auto& ref : selection) {
		if (cancelled && cancelled->load(std::memory_order_relaxed)) { return false; }
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			const auto* brush = index.brush(brushes, ref.objectId);
			if (brush && !emitBrush(*brush)) { return false; }
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			const auto* patch = index.object(document.patches, ref.kind, ref.objectId);
			if (patch && !emitPatch(*patch)) { return false; }
		} else if (ref.kind == LevelMapSelectionKind::Entity && index.object(document.entities, ref.kind, ref.objectId)) {
			owners.insert(ref.objectId);
		}
	}
	if (!owners.isEmpty()) {
		for (const auto& brush : brushes) {
			if (cancelled && cancelled->load(std::memory_order_relaxed)) { return false; }
			if (owners.contains(brush.entityId) && !emitBrush(brush)) { return false; }
		}
		for (const auto& patch : document.patches) {
			if (cancelled && cancelled->load(std::memory_order_relaxed)) { return false; }
			if (owners.contains(patch.entityId) && !emitPatch(patch)) { return false; }
		}
	}
	return true;
}

MapPlanWires buildMapPlanSelectionWires(const LevelMapDocument& document, const QVector<MapBrushGeometry>& brushes,
	const MapViewportSceneIndex& index, MapViewportProjection projection, const QVector<LevelMapSelectionRef>& selection,
	const MapPlanWireLimits& limits, const std::atomic_bool* cancelled)
{
	WireBuilder builder(limits, cancelled);
	if (!visitMapPlanSelectionOutlines(document, brushes, index, projection, selection,
		[&](const QPolygonF& polygon) { return builder.appendPolygon(polygon, MapPlanWireStyle::Selection); }, cancelled)) { return {}; }
	return builder.finish();
}

} // namespace vibestudio
