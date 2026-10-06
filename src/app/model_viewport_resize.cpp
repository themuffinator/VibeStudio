#include "app/model_viewport.h"
#include <QAccessible>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
ModelVec3 modelPoint(BoxResizePoint point) { return {float(point[0]),float(point[1]),float(point[2])}; }
BoxResizePoint coordinates(ModelVec3 point) { return {point.x,point.y,point.z}; }
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
QPointF absent() { return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()}; }
QString axisLabel(int handle)
{
	return QString(QLatin1Char("XYZ"[handle/2])) + (handle%2 ? QStringLiteral("+") : QStringLiteral("−"));
}
}

void ModelViewport::setSelectionResizeBox(const ResizeBox& box, const QHash<int,BoxResizePoint>& pointOrigins, int axes)
{
	if (!validResizeBox(box)) { clearSelectionResizeBox(); return; }
	if (m_selectionResizeEnabled && m_selectionResizeBox == box && m_selectionResizeOrigins == pointOrigins && m_selectionResizeAxes == axes) { return; }
	finishSelectionResize(false);
	m_selectionResizeBox = box; m_selectionResizeOrigins = pointOrigins; m_selectionResizeEnabled = true;
	m_selectionResizeAxes = axes & 7;
	setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::clearSelectionResizeBox()
{
	finishSelectionResize(false); m_selectionResizeEnabled = false; m_selectionResizeOrigins.clear();
	setAccessibleDescription(accessibleSummary()); update();
}

BoxResizeRay ModelViewport::selectionResizeRay(const QPointF& point) const
{
	const auto ray = editRay(point);
	return {coordinates(ray.origin),coordinates(ray.direction),ray.forwardOnly};
}

std::array<QPointF,6> ModelViewport::selectionResizeHandles()
{
	std::array<QPointF,6> points; points.fill(absent());
	if (!m_selectionResizeEnabled || !isEnabled() || !m_hasMesh || m_looking || m_brushDrawTool || m_surfaceTool != ModelViewportSurfaceTool::None) { return points; }
	ensureProjection();
	const auto box = selectionResizeBox();
	const auto direction = coordinates(m_perspective ? m_camera.forward : ModelVec3{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z});
	for (int handle = 0; handle < 6; ++handle) {
		BoxResizePoint world;
		if (!(m_selectionResizeAxes & (1 << (handle/2)))) { continue; }
		if (!boxResizeHandle(box,handle,&world)) { continue; }
		const auto point = projectPoint(m_camera,modelPoint(world),nullptr);
		if (!finite(point) || !QRectF(rect()).adjusted(8,8,-8,-8).contains(point)) { continue; }
		BoxResizeDrag probe;
		if (beginBoxResize(box,handle,direction,selectionResizeRay(point),m_moveGrid,&probe)) { points[handle] = point; }
	}
	// Nearly coincident opposing faces cannot be picked unambiguously.
	for (int axis = 0; axis < 3; ++axis) {
		const auto delta = points[axis*2]-points[axis*2+1];
		if (finite(delta) && QPointF::dotProduct(delta,delta) < 144) { points[axis*2] = points[axis*2+1] = absent(); }
	}
	return points;
}

std::array<QRectF,6> ModelViewport::selectionResizeLabels()
{
	std::array<QRectF,6> labels;
	const auto handles = selectionResizeHandles();
	const QFontMetricsF metrics(font());
	const QRectF bounds = QRectF(rect()).adjusted(3,metrics.height()*2.5+8,-3,-3);
	QPointF centre; int count = 0; QVector<QRectF> occupied;
	for (const auto& point : handles) {
		if (!finite(point)) { continue; }
		centre += point; ++count;
		occupied.append(QRectF(point-QPointF(11,11),QSizeF(22,22)));
	}
	if (!count) { return labels; }
	centre /= count;
	// Place short, fixed-direction axis names outside the handle cluster.
	// Leader lines preserve the face association when large fonts need room.
	for (int handle = 0; handle < 6; ++handle) {
		const auto point = handles[handle];
		if (!finite(point)) { continue; }
		const QSizeF size(metrics.horizontalAdvance(axisLabel(handle))+8,metrics.height()+4);
		if (size.width() > bounds.width() || size.height() > bounds.height()) { continue; }
		auto direction = point-centre;
		const double length = std::hypot(direction.x(),direction.y());
		direction = length > 1 ? direction/length : QPointF(handle%2 ? 1 : -1,0);
		for (int attempt = 0; attempt < 32; ++attempt) {
			const auto at = point + direction*(16+size.height()*0.5+attempt*(size.height()+8));
			QRectF candidate(QPointF(std::clamp(at.x()-size.width()*0.5,bounds.left(),bounds.right()-size.width()),
				std::clamp(at.y()-size.height()*0.5,bounds.top(),bounds.bottom()-size.height())),size);
			if (std::any_of(occupied.cbegin(),occupied.cend(),[&](const auto& other) { return other.intersects(candidate); })) { continue; }
			labels[handle] = candidate; occupied.append(candidate.adjusted(-4,-4,4,4)); break;
		}
	}
	return labels;
}

int ModelViewport::selectionResizeHandleAt(const QPointF& point)
{
	if (!finite(point) || isResizingSelection() || m_editMoveActive || m_surfaceStrokeActive || m_looking) { return -1; }
	const auto handles = selectionResizeHandles(); int result = -1; double nearest = 81;
	for (int handle = 0; handle < 6; ++handle) {
		if (!finite(handles[handle])) { continue; }
		const auto delta = handles[handle]-point; const double distance = QPointF::dotProduct(delta,delta);
		if (distance < nearest) { result = handle; nearest = distance; }
	}
	if (result >= 0) { return result; }
	const auto labels = selectionResizeLabels();
	for (int handle = 0; handle < 6; ++handle) { if (labels[handle].contains(point)) { return handle; } }
	return -1;
}

bool ModelViewport::beginSelectionResize(const QPointF& point)
{
	const int handle = selectionResizeHandleAt(point);
	if (handle < 0 || m_pressButton != Qt::NoButton) { return false; }
	const auto direction = coordinates(m_perspective ? m_camera.forward : ModelVec3{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z});
	if (!beginBoxResize(m_selectionResizeBox,handle,direction,selectionResizeRay(point),m_moveGrid,&m_selectionResizeDrag)) { return false; }
	m_selectionResizeHandle = handle; m_selectionResizePreview = m_selectionResizeBox; m_selectionResizeValid = true;
	m_heldKeys.clear(); m_flyTimer->stop();
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	Q_EMIT selectionResizeActiveChanged(true);
	return isResizingSelection();
}

bool ModelViewport::updateSelectionResize(const QPointF& point)
{
	if (!isResizingSelection()) { return false; }
	ResizeBox next;
	const bool valid = finite(point) && updateBoxResize(m_selectionResizeDrag,selectionResizeRay(point),&next);
	const bool changed = valid && next != m_selectionResizePreview;
	m_selectionResizeValid = valid;
	if (changed) { m_selectionResizePreview = next; ++m_projectionRevision; invalidateRaster(); }
	setAccessibleDescription(accessibleSummary());
	Q_EMIT hoverChanged(selectionResizeSummary(false));
	Q_EMIT selectionResizePreviewChanged(); update();
	return valid;
}

void ModelViewport::finishSelectionResize(bool commit)
{
	if (!isResizingSelection()) { return; }
	const auto box = m_selectionResizePreview;
	const bool changed = commit && m_selectionResizeValid && box != m_selectionResizeBox;
	m_selectionResizeHandle = -1; m_selectionResizeValid = false; m_pressButton = Qt::NoButton;
	++m_projectionRevision; invalidateRaster(); unsetCursor();
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	Q_EMIT selectionResizeActiveChanged(false);
	if (changed) { Q_EMIT selectionResizeRequested(box); }
}

QString ModelViewport::selectionResizeSummary(bool describeControls) const
{
	if (!m_selectionResizeEnabled) { return {}; }
	if (!isResizingSelection()) { return tr("Selection resize handles: drag a labelled face to resize along its axis. Resize Selection provides numeric keyboard editing."); }
	if (!m_selectionResizeValid) { return describeControls
		? tr("Resize preview unavailable at this pointer position. Releasing cancels the edit; Escape cancels.") : tr("Resize unavailable"); }
	const auto dimensions = tr("Resize %1: %2 × %3 × %4 units")
		.arg(QString(QChar(0x2066))+axisLabel(m_selectionResizeHandle)+QChar(0x2069))
		.arg(m_selectionResizePreview.maxs[0]-m_selectionResizePreview.mins[0],0,'g',8)
		.arg(m_selectionResizePreview.maxs[1]-m_selectionResizePreview.mins[1],0,'g',8)
		.arg(m_selectionResizePreview.maxs[2]-m_selectionResizePreview.mins[2],0,'g',8);
	return describeControls ? dimensions + QStringLiteral(". ") + tr("Release applies one undo step; Escape cancels.") : dimensions;
}

void ModelViewport::paintSelectionResize(QPainter& painter)
{
	if (!m_selectionResizeEnabled || !isEnabled() || m_looking || m_brushDrawTool || m_surfaceTool != ModelViewportSurfaceTool::None) { return; }
	const auto box = selectionResizeBox();
	const QColor color = m_highContrast ? QColor(Qt::yellow) : QColor(255,205,70);
	painter.save(); painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(color,m_highContrast ? 2.0 : 1.2,Qt::DashLine));
	for (int corner = 0; corner < 8; ++corner) {
		BoxResizePoint from;
		for (int axis = 0; axis < 3; ++axis) { from[axis] = corner & (1<<axis) ? box.maxs[axis] : box.mins[axis]; }
		for (int axis = 0; axis < 3; ++axis) {
			if (corner & (1<<axis)) { continue; }
			auto to = from; to[axis] = box.maxs[axis]; QPointF a,b;
			if (projectSegment(modelPoint(from),modelPoint(to),&a,&b)) { painter.drawLine(a,b); }
		}
	}
	const auto handles = selectionResizeHandles(); const auto labels = selectionResizeLabels();
	for (int handle = 0; handle < 6; ++handle) {
		if (!finite(handles[handle])) { continue; }
		const QPointF point = handles[handle];
		painter.setPen(QPen(color,1));
		const auto text = labels[handle];
		if (!text.isEmpty()) { painter.drawLine(point,QPointF(std::clamp(point.x(),text.left(),text.right()),std::clamp(point.y(),text.top(),text.bottom()))); }
		painter.setPen(QPen(Qt::black,1)); painter.setBrush(color);
		painter.drawRect(QRectF(point-QPointF(4,4),QSizeF(8,8)));
		if (text.isEmpty()) { continue; }
		painter.setPen(QPen(color,1)); painter.setBrush(Qt::black); painter.drawRoundedRect(text,2,2);
		painter.drawText(text,Qt::AlignCenter|Qt::TextForceLeftToRight,axisLabel(handle));
	}
	painter.restore();
}

} // namespace vibestudio
