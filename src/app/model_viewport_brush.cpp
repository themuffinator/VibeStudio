#include "app/model_viewport.h"
#include <QAccessible>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
ModelVec3 modelPoint(BoxResizePoint point) { return {float(point[0]),float(point[1]),float(point[2])}; }
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
bool held(Qt::KeyboardModifiers current, Qt::KeyboardModifiers required)
{
	return required != Qt::NoModifier && (current & required) == required;
}
QString planeName(int axis)
{
	return axis == 2 ? QStringLiteral("XY") : axis == 1 ? QStringLiteral("XZ") : QStringLiteral("YZ");
}
}

void ModelViewport::setBrushDrawTool(bool enabled)
{
	if (m_brushDrawTool == enabled) { return; }
	finishBrushDraw(false);
	if (enabled) {
		setSurfaceTool(ModelViewportSurfaceTool::None);
		finishEditTransform(false); finishSelectionResize(false);
		m_pressButton = Qt::NoButton; cancelMove(); setLooking(false);
		m_orbiting = false; m_panning = false; m_heldKeys.clear(); m_flyTimer->stop();
		// An empty map has no geometry to frame. Give its construction plane a
		// usable starting distance, retaining an already positioned camera.
		if (m_perspective && m_meshTriangles.isEmpty() && m_focusDistance < 64) {
			m_radius = 128; frameModelPerspective();
		}
	}
	m_brushDrawTool = enabled;
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT brushDrawToolChanged();
}

bool ModelViewport::setBrushDrawPlane(int axis, double base, double depth, Qt::KeyboardModifiers square, Qt::KeyboardModifiers cube)
{
	if (axis < 0 || axis > 2 || !std::isfinite(base) || !std::isfinite(depth) || base < -32768 || depth < 1 || base+depth > 32768) { return false; }
	if (axis == m_brushDraw.axis && base == m_brushDraw.base && depth == m_brushDraw.depth
		&& square == m_brushSquareModifiers && cube == m_brushCubeModifiers) { return true; }
	finishBrushDraw(false);
	m_brushDraw.axis = axis; m_brushDraw.base = base; m_brushDraw.depth = depth;
	m_brushSquareModifiers = square; m_brushCubeModifiers = cube;
	setAccessibleDescription(accessibleSummary()); update(); return true;
}

bool ModelViewport::beginBrushDraw(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	if (!m_brushDrawTool || !isEnabled() || m_looking || m_brushDrawing || m_pressButton != Qt::NoButton || !finite(point)) { return false; }
	ensureProjection();
	BoxDrawDrag next;
	if (!beginBoxDraw(selectionResizeRay(point),m_brushDraw.axis,m_brushDraw.base,m_brushDraw.depth,m_moveGrid,&next)) {
		Q_EMIT hoverChanged(tr("Cannot reach the construction plane here. Reposition the camera or choose another plane.")); return false;
	}
	m_brushDraw = next; m_brushDrawing = true; m_brushDrawValid = false; m_brushWheelRemainder = 0;
	m_heldKeys.clear(); m_flyTimer->stop();
	updateBrushDraw(point,modifiers);
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	Q_EMIT brushDrawActiveChanged(true);
	return m_brushDrawing;
}

bool ModelViewport::updateBrushDraw(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	if (!m_brushDrawing) { return false; }
	m_brushDrawPoint = point; m_brushDrawModifiers = modifiers;
	m_brushDrawValid = finite(point) && updateBoxDraw(m_brushDraw,selectionResizeRay(point),
		held(modifiers,m_brushSquareModifiers),held(modifiers,m_brushCubeModifiers),&m_brushDrawBox);
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT hoverChanged(brushDrawSummary(false)); Q_EMIT brushDrawPreviewChanged();
	return m_brushDrawValid;
}

bool ModelViewport::adjustBrushDrawDepth(int steps)
{
	if (!m_brushDrawing || !vibestudio::adjustBoxDrawDepth(&m_brushDraw,steps)) { return false; }
	updateBrushDraw(m_brushDrawPoint,m_brushDrawModifiers); return true;
}

void ModelViewport::finishBrushDraw(bool commit)
{
	if (!m_brushDrawing) { return; }
	const bool apply = commit && m_brushDrawValid; const auto box = m_brushDrawBox;
	m_brushDrawing = false; m_brushDrawValid = false; m_pressButton = Qt::NoButton; m_brushWheelRemainder = 0;
	unsetCursor(); setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	Q_EMIT hoverChanged(brushDrawSummary(false)); Q_EMIT brushDrawActiveChanged(false);
	if (apply) { Q_EMIT brushDrawRequested(box); }
}

QString ModelViewport::brushDrawSummary(bool describeControls) const
{
	if (!m_brushDrawTool) { return {}; }
	const auto plane = QString(QChar(0x2066)) + planeName(m_brushDraw.axis) + QChar(0x2069);
	QString summary = tr("Draw Brush · %1 at %2 · Depth %3").arg(plane).arg(m_brushDraw.base,0,'g',8).arg(m_brushDraw.depth,0,'g',8);
	if (m_brushDrawing) {
		summary = m_brushDrawValid ? tr("New brush · %1 × %2 × %3 units")
			.arg(m_brushDrawBox.maxs[0]-m_brushDrawBox.mins[0],0,'g',8)
			.arg(m_brushDrawBox.maxs[1]-m_brushDrawBox.mins[1],0,'g',8)
			.arg(m_brushDrawBox.maxs[2]-m_brushDrawBox.mins[2],0,'g',8)
			: tr("No valid brush footprint · Releasing cancels");
	}
	if (describeControls) {
		summary += QStringLiteral(". ") + tr("Left-drag a footprint; wheel changes depth during the drag. Release creates one brush; Escape cancels. Add Brush provides numeric keyboard creation.");
	}
	return summary;
}

void ModelViewport::paintBrushDraw(QPainter& painter)
{
	if (!m_brushDrawTool) { return; }
	ensureProjection(); painter.save(); painter.setBrush(Qt::NoBrush);
	const int axis = m_brushDraw.axis, u = (axis+1)%3, v = (axis+2)%3;
	const QColor color = m_highContrast ? QColor(Qt::yellow) : QColor(95,220,240);
	// A bounded construction grid costs the same for empty and large scenes.
	const double spacing = std::max({16.0,m_moveGrid,std::pow(2.0,std::ceil(std::log2(std::max(1.0,m_focusDistance/24))))});
	BoxResizePoint centre{m_center.x,m_center.y,m_center.z};
	if (m_brushDrawing) { centre = m_brushDraw.start; }
	for (int a : {u,v}) { centre[a] = std::round(centre[a]/spacing)*spacing; }
	centre[axis] = m_brushDraw.base;
	for (int i = -16; i <= 16; ++i) {
		for (int a : {u,v}) {
			auto from = centre, to = centre; const int other = a == u ? v : u;
			from[a] = to[a] = std::clamp(centre[a]+i*spacing,-32768.0,32768.0);
			from[other] = std::max(-32768.0,centre[other]-16*spacing); to[other] = std::min(32768.0,centre[other]+16*spacing);
			QPointF p,q;
			painter.setPen(QPen(m_highContrast ? QColor(115,115,115) : QColor(62,83,90),i == 0 ? 1.5 : 0.7,Qt::DotLine));
			if (projectSegment(modelPoint(from),modelPoint(to),&p,&q)) { painter.drawLine(p,q); }
		}
	}
	if (m_brushDrawing && m_brushDrawValid) {
		painter.setPen(QPen(color,m_highContrast ? 3 : 2,Qt::DashLine));
		for (int corner = 0; corner < 8; ++corner) {
			BoxResizePoint from;
			for (int a = 0; a < 3; ++a) { from[a] = corner & (1<<a) ? m_brushDrawBox.maxs[a] : m_brushDrawBox.mins[a]; }
			for (int a = 0; a < 3; ++a) {
				if (corner & (1<<a)) { continue; }
				auto to = from; to[a] = m_brushDrawBox.maxs[a]; QPointF p,q;
				if (projectSegment(modelPoint(from),modelPoint(to),&p,&q)) { painter.drawLine(p,q); }
			}
		}
	}
	painter.restore();
}

} // namespace vibestudio
