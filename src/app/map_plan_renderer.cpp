#include "app/map_plan_renderer.h"
#include "app/model_rasterizer.h"
#include "app/viewport_image.h"
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
bool stopped(const std::atomic_bool* cancelled) { return cancelled && cancelled->load(std::memory_order_relaxed); }
bool overlap(const QRectF& a, const QRectF& b)
{
	return a.left() <= b.right() && a.right() >= b.left() && a.top() <= b.bottom() && a.bottom() >= b.top();
}
QPointF project(const LevelMapVec3& point, MapViewportProjection projection)
{
	if (projection == MapViewportProjection::FrontXZ) { return {point.x, point.z}; }
	if (projection == MapViewportProjection::SideZY) { return {point.y, point.z}; }
	return {point.x, point.y};
}
QPointF screen(const MapPlanWireFrame& view, QPointF point)
{
	return {view.viewport.width() * 0.5 + (point.x() - view.center.x()) * view.zoom,
		view.viewport.height() * 0.5 - (point.y() - view.center.y()) * view.zoom};
}
QRectF visible(const MapPlanWireFrame& view)
{
	const double x = (view.viewport.width() * 0.5 + 4) / view.zoom;
	const double y = (view.viewport.height() * 0.5 + 4) / view.zoom;
	return {view.center.x() - x, view.center.y() - y, x * 2, y * 2};
}
void polygon(QPainter& painter, const MapPlanWireFrame& view, const QPolygonF& world)
{
	QPolygonF points; points.reserve(world.size());
	for (const auto& point : world) { points.append(screen(view, point)); }
	painter.drawPolygon(points);
}
void diagnostic(QPainter& painter, const MapPlanWireFrame& view, const QRectF& world)
{
	const auto box = QRectF(screen(view, world.topLeft()), screen(view, world.bottomRight())).normalized();
	painter.drawRect(box); painter.drawLine(box.topLeft(), box.bottomRight()); painter.drawLine(box.topRight(), box.bottomLeft());
}
QPen diagnosticPen(const MapPlanWireFrame& view)
{
	QPen pen(QColor::fromRgba(view.invalid), 1.6, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
	pen.setDashPattern({4.0,3.0}); return pen;
}
bool paintPatches(QPainter& painter, const MapPlanRenderRequest& request, const std::atomic_bool* cancelled)
{
	const auto clip = visible(request.view);
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QColor::fromRgba(request.view.patch), 1.4, Qt::DashDotLine, Qt::RoundCap, Qt::RoundJoin));
	for (const auto& patch : request.document.patches) {
		if (stopped(cancelled)) { return false; }
		if (patch.mins.valid && patch.maxs.valid
			&& !overlap(QRectF(project(patch.mins, request.projection), project(patch.maxs, request.projection)).normalized(), clip)) { continue; }
		const auto outline = mapPlanPatchOutline(patch, request.projection);
		if (outline.size() >= 3) { polygon(painter, request.view, outline); }
	}
	return !stopped(cancelled);
}
}

bool sameMapPlanView(const MapPlanWireFrame& a, const MapPlanWireFrame& b)
{
	return a.viewport == b.viewport && a.center.x() == b.center.x() && a.center.y() == b.center.y()
		&& a.zoom == b.zoom && a.pixelRatio == b.pixelRatio && a.highContrast == b.highContrast
		&& a.world == b.world && a.entity == b.entity && a.invalid == b.invalid && a.selection == b.selection && a.patch == b.patch
		&& a.pixelPhase.x() == b.pixelPhase.x() && a.pixelPhase.y() == b.pixelPhase.y();
}
bool supportedMapPlanFrame(const MapPlanWireFrame& view)
{
	return std::isfinite(view.zoom) && view.zoom > 0
		&& std::isfinite(view.center.x()) && std::isfinite(view.center.y())
		&& !viewportImageSize(view.viewport,view.pixelRatio,view.pixelPhase).isEmpty();
}

bool paintMapPlanFallback(QPainter& painter, const MapPlanRenderRequest& request, bool selected, const std::atomic_bool* cancelled)
{
	const auto& view = request.view;
	const auto clip = visible(view);
	painter.setBrush(Qt::NoBrush);
	if (selected) {
		painter.setPen(QPen(QColor::fromRgba(view.selection), view.highContrast ? 3.6 : 2.8, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
		return visitMapPlanSelectionOutlines(request.document, request.brushes, request.index, request.projection, request.selection,
			[&](const QPolygonF& outline) {
				if (stopped(cancelled)) { return false; }
				if (outline.size() >= 2 && overlap(outline.boundingRect(), clip)) { polygon(painter, view, outline); }
				return true;
			}, cancelled);
	}
	const QPen world(QColor::fromRgba(view.world), view.highContrast ? 1.6 : 1.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	const QPen entity(QColor::fromRgba(view.entity), view.highContrast ? 2.8 : 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	for (const auto& brush : request.brushes) {
		if (stopped(cancelled)) { return false; }
		if (!brush.solved) {
			const auto* source = request.index.object(request.document.brushes, LevelMapSelectionKind::QuakeBrush, brush.brushId);
			if (!source || !source->boundsSolved) { continue; }
			const auto box = QRectF(project(source->mins, request.projection), project(source->maxs, request.projection)).normalized();
			if (overlap(box, clip)) { painter.setPen(diagnosticPen(view)); diagnostic(painter, view, box); }
			continue;
		}
		if (!overlap(QRectF(project(brush.mins, request.projection), project(brush.maxs, request.projection)).normalized(), clip)) { continue; }
		painter.setPen(brush.entityId < 0 || brush.entityId == request.worldspawnId ? world : entity);
		if (request.projection == MapViewportProjection::TopXY) {
			for (const auto& outline : brush.footprintPolygons()) { if (outline.size() >= 2) { polygon(painter, view, outline); } }
		} else {
			for (const auto& face : brush.faces) {
				if (stopped(cancelled)) { return false; }
				if (face.points.size() < 3) { continue; }
				QPolygonF outline; outline.reserve(face.points.size());
				for (const auto& point : face.points) { outline.append(project(point, request.projection)); }
				polygon(painter, view, outline);
			}
		}
	}
	return paintPatches(painter, request, cancelled);
}

bool renderMapPlanFrame(const MapPlanRenderRequest& request, bool selected, const MapPlanWires& wires,
	MapPlanWireFrame* frame, const std::atomic_bool* cancelled)
{
	if (!frame || !supportedMapPlanFrame(request.view) || stopped(cancelled)) { return false; }
	if (!frame->image.isNull() && sameMapPlanView(*frame, request.view)) { return true; }
	auto next = request.view;
	next.image = QImage(viewportImageSize(next.viewport,next.pixelRatio,next.pixelPhase),QImage::Format_ARGB32_Premultiplied);
	if (next.image.isNull()) { return false; }
	next.image.setDevicePixelRatio(next.pixelRatio); next.image.fill(Qt::transparent);
	const QPointF origin = next.pixelPhase / next.pixelRatio;
	if (!wires.statistics.ready) {
		QPainter painter(&next.image); painter.setRenderHint(QPainter::Antialiasing);
		painter.translate(origin);
		if (!paintMapPlanFallback(painter, request, selected, cancelled)) { return false; }
	} else {
		const auto clip = visible(next);
		QVector<ModelWireSegment> segments;
		for (const auto& batch : wires.batches) {
			if (stopped(cancelled)) { return false; }
			if (!overlap(batch.bounds, clip)) { continue; }
			if (batch.style == MapPlanWireStyle::Invalid) {
				QPainter painter(&next.image); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush); painter.setPen(diagnosticPen(next));
				painter.translate(origin);
				for (const auto& bounds : batch.invalidBrushes) {
					if (stopped(cancelled)) { return false; }
					if (overlap(bounds, clip)) { diagnostic(painter, next, bounds); }
				}
			} else {
				segments.clear(); segments.reserve(batch.lines.size());
				for (const auto& line : batch.lines) { segments.append({screen(next, line.p1()) + origin, screen(next, line.p2()) + origin, selected}); }
				const bool world = batch.style == MapPlanWireStyle::World;
				ModelWireStyle style; style.pixelRatio = next.pixelRatio; style.wire = world ? next.world : next.entity;
				style.width = world ? (next.highContrast ? 1.6 : 1.1) : (next.highContrast ? 2.8 : 2.2);
				style.selection = next.selection; style.selectionWidth = next.highContrast ? 3.6 : 2.8;
				if (!paintModelWireframe(&next.image, segments, style, cancelled)) { return false; }
			}
		}
		if (!selected) {
			QPainter painter(&next.image); painter.setRenderHint(QPainter::Antialiasing);
			painter.translate(origin);
			if (!paintPatches(painter, request, cancelled)) { return false; }
		}
	}
	if (stopped(cancelled)) { return false; }
	*frame = std::move(next); return true;
}
} // namespace vibestudio
