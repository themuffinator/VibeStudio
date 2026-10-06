#include "app/map_viewport_overlay.h"
#include <QPainter>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vibestudio {

bool sameMapViewportOverlayKey(const MapViewportOverlayKey& a, const MapViewportOverlayKey& b)
{
	return a.sceneRevision == b.sceneRevision && a.selectionRevision == b.selectionRevision && a.projection == b.projection
		&& a.showGrid == b.showGrid && a.showMembers == b.showMembers && a.selectionColor == b.selectionColor
		&& a.markerWidth == b.markerWidth && sameMapGridView(a.view,b.view);
}

bool mapViewportMemberCenters(const MapViewportOverlayRequest& request, QVector<QPointF>* centers, const std::atomic_bool* cancelled)
{
	const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_relaxed); };
	if (!centers || stopped()) { return false; }
	QVector<QPointF> result; result.reserve(std::min(request.selection.size(),mapViewportMaxMemberMarkers));
	const auto& view = request.key.view;
	const QRectF visible = QRectF(QPointF(),view.viewport).adjusted(-10,-10,10,10);
	QSet<QPair<double,double>> marked;
	for (const auto& ref : request.selection) {
		if (stopped()) { return false; }
		if (result.size() >= mapViewportMaxMemberMarkers) { break; }
		if (ref == request.primary) { continue; }
		QPointF world;
		if (!mapViewportObjectPoint(request.document,request.sectors,request.brushes,request.index,request.key.projection,
			ref.kind,ref.objectId,&world,cancelled) || !std::isfinite(world.x()) || !std::isfinite(world.y())) { continue; }
		const QPointF center(view.viewport.width() * 0.5 + (world.x() - view.center.x()) * view.zoom,
			view.viewport.height() * 0.5 - (world.y() - view.center.y()) * view.zoom);
		if (!visible.contains(center)) { continue; }
		const QPair<double,double> point {world.x() == 0 ? 0.0 : world.x(),world.y() == 0 ? 0.0 : world.y()};
		if (marked.contains(point)) { continue; }
		marked.insert(point); result.append(center);
	}
	if (stopped()) { return false; }
	*centers = std::move(result); return true;
}

bool renderMapViewportOverlays(const MapViewportOverlayRequest& request, MapViewportOverlayResult* result, const std::atomic_bool* cancelled)
{
	const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_relaxed); };
	const auto& key = request.key;
	const QSize size = mapGridImageSize(key.view);
	if (!result || size.isEmpty() || !std::isfinite(key.markerWidth) || key.markerWidth <= 0 || stopped()) { return false; }
	MapViewportOverlayResult next; next.key = key;
	if (key.showGrid) {
		next.grid = request.previousGrid;
		if (!renderMapGrid(key.view,&next.grid,cancelled)) { return false; }
	}
	if (key.showMembers) {
		QVector<QPointF> centers;
		if (!mapViewportMemberCenters(request,&centers,cancelled)) { return false; }
		next.markerCount = centers.size();
		if (!centers.isEmpty()) {
			next.members = QImage(size,QImage::Format_ARGB32_Premultiplied);
			if (next.members.isNull()) { return false; }
			next.members.setDevicePixelRatio(key.view.pixelRatio); next.members.fill(Qt::transparent);
			QPainter painter(&next.members); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
			painter.translate(key.view.pixelPhase / key.view.pixelRatio);
			painter.setPen(QPen(QColor::fromRgba(key.selectionColor),key.markerWidth));
			for (const auto& center : centers) {
				if (stopped()) { return false; }
				painter.drawEllipse(center,8.0,8.0);
			}
		}
	}
	if (stopped()) { return false; }
	next.ready = true; *result = std::move(next); return true;
}

} // namespace vibestudio
