#include "app/level_component_gizmo.h"
#include <QApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace vibestudio {
namespace {
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
bool finite(LevelMapVec3 p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= 32768 && std::abs(p.y) <= 32768 && std::abs(p.z) <= 32768; }
bool same(LevelMapVec3 a,LevelMapVec3 b) { return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z; }
double squared(QPointF p) { return QPointF::dotProduct(p,p); }
double coordinate(LevelMapVec3 p,int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
void setCoordinate(LevelMapVec3& p,int axis,double value) { if (axis == 0) { p.x = value; } else if (axis == 1) { p.y = value; } else { p.z = value; } }
LevelMapVec3 offset(LevelMapVec3 p,int axis,double distance) { setCoordinate(p,axis,coordinate(p,axis)+distance); return p; }
ModelVec3 model(LevelMapVec3 p) { return {float(p.x),float(p.y),float(p.z)}; }
double snapped(double value,double step) { return step > 0 ? std::round(value/step)*step : value; }
QRectF labelRect(QPointF point,const QFont& font,const QString& text) {
	const QFontMetricsF metrics(font); const QSizeF size(std::max(20.0,metrics.horizontalAdvance(text)+10),std::max(20.0,metrics.height()+4));
	return {point-QPointF(size.width()/2,size.height()/2),size};
}
constexpr std::array<std::array<int,2>,6> shearAxes{{{0,1},{0,2},{1,0},{1,2},{2,0},{2,1}}};
}
LevelComponentGizmo::LevelComponentGizmo(QObject* parent) : QObject(parent) {}

void LevelComponentGizmo::setContext(LevelComponentTool tool,const LevelMapVec3& pivot,bool available,double grid,
	double angleStep,double scaleStep,double shearStep)
{
	available = available && finite(pivot) && std::isfinite(grid) && grid >= 0 && grid <= 32768
		&& std::isfinite(angleStep) && angleStep >= 0 && angleStep <= 360 && std::isfinite(scaleStep) && scaleStep >= 0 && scaleStep <= 1
		&& std::isfinite(shearStep) && shearStep >= 0 && shearStep <= 1;
	if (tool == m_tool && same(pivot,m_pivot) && available == m_available && grid == m_grid && angleStep == m_angleStep && scaleStep == m_scaleStep && shearStep == m_shearStep) { return; }
	finish(false); m_tool = tool; m_pivot = pivot; m_available = available; m_grid = grid;
	m_angleStep = angleStep; m_scaleStep = scaleStep; m_shearStep = shearStep; Q_EMIT changed();
}
QVector<LevelComponentGizmoHandle> LevelComponentGizmo::handles(const LevelComponentGizmoFrame& frame) const
{
	QVector<LevelComponentGizmoHandle> result;
	if (!m_available || m_tool == LevelComponentTool::Move || !frame.project || !std::isfinite(frame.radius) || frame.radius <= 0) { return result; }
	const auto pivot = m_active && m_tool == LevelComponentTool::Pivot ? transformed(m_pivot) : m_pivot;
	const auto centre = frame.project(pivot); if (!finite(centre)) { return result; }
	if (m_tool == LevelComponentTool::Rotate) {
		for (int axis = 0; axis < 3; ++axis) {
			if(frame.rotationAxis>=0 && axis!=frame.rotationAxis) { continue; }
			LevelComponentGizmoHandle handle; handle.id = handle.axis = axis; handle.text = QString(QLatin1Char("XYZ"[axis]));
			for (int i = 0; i <= 96; ++i) {
				const double radians = 2*std::numbers::pi*i/96;
				handle.points << frame.project(offset(offset(pivot,(axis+1)%3,frame.radius*std::cos(radians)),(axis+2)%3,frame.radius*std::sin(radians)));
			}
			handle.label = handle.points[(axis*17+12)%96]; result << handle;
		}
	} else if (m_tool == LevelComponentTool::Shear) {
		for (int id = 0; id < 6; ++id) {
			const auto axes = shearAxes[id];
			const auto anchor = offset(pivot,axes[1],frame.radius);
			LevelComponentGizmoHandle handle; handle.id = id; handle.axis = axes[0];
			handle.label = frame.project(offset(anchor,axes[0],frame.radius*.4));
			handle.points << frame.project(anchor) << handle.label;
			handle.text = QStringLiteral("%1/%2").arg(QLatin1Char("XYZ"[axes[0]])).arg(QLatin1Char("XYZ"[axes[1]]));
			if (finite(handle.points[0]) && finite(handle.label) && squared(handle.label-handle.points[0]) >= 64) { result << handle; }
		}
	} else {
		for (int axis = 0; axis < 3; ++axis) {
			LevelComponentGizmoHandle handle; handle.id = handle.axis = axis; handle.text = QString(QLatin1Char("XYZ"[axis]));
			handle.label = frame.project(offset(pivot,axis,frame.radius)); handle.points << centre << handle.label;
			if (finite(handle.label) && squared(handle.label-centre) >= 100) { result << handle; }
		}
		LevelComponentGizmoHandle centreHandle; centreHandle.id = 3; centreHandle.axis = 3; centreHandle.label = centre;
		centreHandle.text = m_tool == LevelComponentTool::Scale ? tr("All") : tr("Pivot"); result << centreHandle;
	}
	return result;
}
int LevelComponentGizmo::hitTest(const QPointF& point,const LevelComponentGizmoFrame& frame,const QFont& font) const
{
	if (m_active || !finite(point)) { return -1; }
	int result = -1; double nearest = 100;
	for (const auto& handle : handles(frame)) {
		if (finite(handle.label) && labelRect(handle.label,font,handle.text).contains(point)) { return handle.id; }
		for (int i = 1; i < handle.points.size(); ++i) {
			const auto a = handle.points[i-1], b = handle.points[i]; if (!finite(a) || !finite(b)) { continue; }
			const auto line = b-a; if (squared(line) < .01) { continue; }
			const double t = std::clamp(QPointF::dotProduct(point-a,line)/squared(line),m_tool == LevelComponentTool::Rotate ? 0.0 : .3,1.0);
			const double distance = squared(point-a-line*t);
			if (distance < nearest) { nearest = distance; result = handle.id; }
		}
	}
	return result;
}
bool LevelComponentGizmo::begin(const QPointF& point,const LevelComponentGizmoFrame& frame,const QFont& font)
{
	if (m_active || !frame.ray || !frame.project || !finite(point)) { return false; }
	const int handle = hitTest(point,frame,font); if (handle < 0) { return false; }
	m_linearRotation = false;
	if (m_tool == LevelComponentTool::Rotate) {
		const double direction[]{frame.viewDirection.x,frame.viewDirection.y,frame.viewDirection.z};
		m_linearRotation = std::abs(direction[handle]) < .15 || !beginModelRotateDrag(model(m_pivot),handle,frame.ray(point),0,&m_rotate);
		if (m_linearRotation) {
			const auto ring = handles(frame)[handle].points; double closest = std::numeric_limits<double>::infinity(); m_tangent = {};
			for (int i = 0; i < 96; ++i) {
				const auto tangent = (ring[(i+1)%96]-ring[(i+95)%96])/(4*std::numbers::pi/96);
				if (!finite(ring[i]) || !finite(tangent) || squared(tangent) < 1) { continue; }
				const double distance = squared(ring[i]-point); if (distance < closest) { closest = distance; m_tangent = tangent; }
			}
			if (squared(m_tangent) < 1) { return false; }
		}
	} else if (m_tool != LevelComponentTool::Scale || handle < 3) {
		const int axis = m_tool == LevelComponentTool::Shear ? shearAxes[handle][0] : handle;
		if (!beginModelMoveDrag(model(m_pivot),frame.viewDirection,ModelMoveConstraint(axis),frame.ray(point),m_tool == LevelComponentTool::Pivot ? m_grid : 0,&m_move)) { return false; }
	}
	m_frame = frame; m_handle = handle; m_press = point; ++m_serial;
	m_request = {}; m_request.pivot = m_pivot;
	m_request.operation = m_tool == LevelComponentTool::Rotate ? LevelComponentTransformKind::Rotate : m_tool == LevelComponentTool::Scale ? LevelComponentTransformKind::Scale
		: m_tool == LevelComponentTool::Shear ? LevelComponentTransformKind::Shear : LevelComponentTransformKind::Move;
	m_request.axis = handle; if (m_tool == LevelComponentTool::Shear) { m_request.axis = shearAxes[handle][0]; m_request.along = shearAxes[handle][1]; }
	m_active = m_valid = true; m_pending = m_releasePending = m_travelled = false; m_edges.clear(); m_error.clear();
	Q_EMIT activeChanged(true); Q_EMIT changed(); return m_active;
}
void LevelComponentGizmo::refuse(const QString& error)
{
	++m_serial; m_valid = m_pending = false; m_edges.clear(); m_error = error; Q_EMIT changed();
}
bool LevelComponentGizmo::updateDrag(const QPointF& point)
{
	if (!m_active || m_releasePending) { return false; }
	if (!finite(point)) { refuse(tr("Cannot reach the manipulator here.")); return false; }
	if (!m_travelled && squared(point-m_press) < std::pow(QApplication::startDragDistance(),2)) { return true; }
	m_travelled = true; auto next = m_request; bool valid = true;
	if (m_tool == LevelComponentTool::Rotate) {
		double angle = 0;
		if (m_linearRotation) { angle = QPointF::dotProduct(point-m_press,m_tangent)/squared(m_tangent)*180/std::numbers::pi; }
		else { valid = modelRotateDragAngle(&m_rotate,m_frame.ray(point),&angle); }
		next.amount = snapped(angle,m_angleStep);
	} else if (m_tool == LevelComponentTool::Scale && m_handle == 3) {
		const auto travel = point-m_press; const double factor = 1+snapped(std::exp2((travel.x()-travel.y())/96.0)-1,m_scaleStep);
		next.scale = {factor,factor,factor,true};
	} else {
		ModelVec3 delta; valid = modelMoveDragDelta(m_move,m_frame.ray(point),&delta);
		const LevelMapVec3 moved{delta.x,delta.y,delta.z,true};
		if (m_tool == LevelComponentTool::Pivot) { next.delta = moved; valid = valid && finite({m_pivot.x+delta.x,m_pivot.y+delta.y,m_pivot.z+delta.z,true}); }
		else if (m_tool == LevelComponentTool::Scale) { setCoordinate(next.scale,m_handle,1+snapped(coordinate(moved,m_handle)/m_frame.radius,m_scaleStep)); }
		else { next.amount = snapped(coordinate(moved,next.axis)/m_frame.radius,m_shearStep); }
	}
	LevelTextureAffine matrix; QString error;
	if (!valid || !levelComponentTransformMatrix(next,m_pivot,&matrix,&error)) { refuse(error.isEmpty() ? tr("Cannot reach the manipulator plane here.") : error); return false; }
	if (same(next.delta,m_request.delta) && same(next.scale,m_request.scale) && next.amount == m_request.amount && m_valid) { return true; }
	m_request = next; ++m_serial; m_valid = true; m_error.clear(); m_edges.clear();
	m_pending = m_tool != LevelComponentTool::Pivot;
	Q_EMIT changed(); if (m_pending) { Q_EMIT previewRequested(m_serial); } return true;
}
void LevelComponentGizmo::setPreview(quint64 serial,const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges,const QString& error)
{
	if (!m_active || !m_pending || serial != m_serial) { return; }
	m_pending = false; m_valid = error.isEmpty(); m_error = error; m_edges = m_valid ? edges : QVector<QPair<LevelMapVec3,LevelMapVec3>>{};
	Q_EMIT changed(); if (m_releasePending) { finish(true); }
}
void LevelComponentGizmo::finish(bool commit)
{
	if (!m_active) { return; }
	if (commit && m_pending) { m_releasePending = true; Q_EMIT changed(); return; }
	const bool altered = m_request.delta.x != 0 || m_request.delta.y != 0 || m_request.delta.z != 0 || m_request.amount != 0
		|| m_request.scale.x != 1 || m_request.scale.y != 1 || m_request.scale.z != 1;
	const bool apply = commit && m_valid && m_travelled && altered; const auto serial = m_serial;
	const auto pivot = transformed(m_pivot);
	m_active = m_pending = m_releasePending = false; m_edges.clear();
	if (apply) { if (m_tool == LevelComponentTool::Pivot) { Q_EMIT pivotChanged(pivot); } else { Q_EMIT commitRequested(serial); } }
	++m_serial; Q_EMIT activeChanged(false); Q_EMIT changed();
}
LevelMapVec3 LevelComponentGizmo::transformed(const LevelMapVec3& point) const
{
	LevelTextureAffine matrix;
	if (!m_active || !levelComponentTransformMatrix(m_request,m_pivot,&matrix)) { return point; }
	return transformedLevelComponentPoint(point,matrix);
}
QString LevelComponentGizmo::summary() const
{
	if (!m_available || m_tool == LevelComponentTool::Move) { return {}; }
	if (!m_valid && m_active) { return tr("Components unchanged: %1").arg(m_error); }
	QString operation;
	if (m_tool == LevelComponentTool::Rotate) { operation = tr("Rotate %1: %2°").arg(QLatin1Char("XYZ"[std::clamp(m_request.axis,0,2)])).arg(m_active ? m_request.amount : 0,0,'g',6); }
	else if (m_tool == LevelComponentTool::Scale) { operation = tr("Scale: %1, %2, %3").arg(m_active ? m_request.scale.x : 1,0,'g',6).arg(m_active ? m_request.scale.y : 1,0,'g',6).arg(m_active ? m_request.scale.z : 1,0,'g',6); }
	else if (m_tool == LevelComponentTool::Shear) { operation = tr("Shear: %1").arg(m_active ? m_request.amount : 0,0,'g',6); }
	else { const auto p = transformed(m_pivot); operation = tr("Pivot: %1, %2, %3").arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8); }
	if (!m_active) { return operation + QStringLiteral(" · ") + tr("Drag a labelled handle; Transform Components provides numeric editing."); }
	return operation + QStringLiteral(" · ") + (m_pending ? tr("Checking geometry… Escape cancels") : m_tool == LevelComponentTool::Pivot ? tr("Release to set the pivot; Escape cancels") : tr("Valid preview · Release to apply"));
}
void LevelComponentGizmo::paint(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	if (!m_available || m_tool == LevelComponentTool::Move || !frame.project) { return; }
	painter.save();
	const QColor selected = highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	painter.setPen(QPen(selected,highContrast ? 2.5 : 1.5,Qt::DashLine));
	for (const auto& edge : m_edges) { const auto a = frame.project(edge.first),b = frame.project(edge.second); if (finite(a) && finite(b)) { painter.drawLine(a,b); } }
	const std::array<QColor,4> colours{QColor(255,110,110),QColor(120,255,150),QColor(140,180,255),selected};
	for (const auto& handle : handles(frame)) {
		for (int i = 1; i < handle.points.size(); ++i) {
			if (!finite(handle.points[i-1]) || !finite(handle.points[i])) { continue; }
			painter.setPen(QPen(Qt::black,5)); painter.drawLine(handle.points[i-1],handle.points[i]);
			painter.setPen(QPen(m_active && handle.id == m_handle ? selected : colours[handle.axis],highContrast ? 3 : 2)); painter.drawLine(handle.points[i-1],handle.points[i]);
		}
		if (finite(handle.label)) {
			const auto rect = labelRect(handle.label,painter.font(),handle.text); painter.setBrush(Qt::black); painter.setPen(QPen(colours[handle.axis],1.5));
			painter.drawRect(rect); painter.drawText(rect,Qt::AlignCenter|Qt::TextForceLeftToRight,handle.text);
		}
	}
	painter.restore();
}
} // namespace vibestudio
