#include "app/level_vertex_insertion.h"
#include "app/level_component_trace.h"
#include <QApplication>
#include <QPainter>
#include <cmath>

namespace vibestudio {
namespace {
bool same(LevelMapVec3 a,LevelMapVec3 b) { return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z; }
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
}
void LevelVertexInsertion::setContext(std::shared_ptr<const LevelVertexInsertionData> data,bool available,bool tool,double grid)
{
	available = available && data && !data->isEmpty() && std::isfinite(grid) && grid >= 0 && grid <= 32768;
	if (m_data == data && m_available == available && m_tool == tool && m_grid == grid) { return; }
	finish(false); m_data = std::move(data); m_available = available; m_tool = tool; m_grid = grid;
	m_hit = {}; m_position = {}; Q_EMIT changed();
}
QVector<LevelMapVertexInsertion> LevelVertexInsertion::request() const
{
	QVector<LevelMapVertexInsertion> result;
	if (m_active && m_position.valid) { for (int brush : m_hit.brushes) { result << LevelMapVertexInsertion{brush,{m_position}}; } }
	return result;
}
bool LevelVertexInsertion::hover(const QPointF& point,const LevelComponentGizmoFrame& frame,bool show)
{
	if (m_active) { return true; }
	const auto hit = show ? pick(point,frame) : LevelVertexInsertionHit{};
	if (same(hit.position,m_hit.position) && hit.brushes == m_hit.brushes && hit.kind == m_hit.kind) { return hit.position.valid; }
	m_hit = hit; m_position = hit.position; m_error.clear(); Q_EMIT changed(); return hit.position.valid;
}
void LevelVertexInsertion::clearHover()
{
	if (!m_active && m_hit.position.valid) { m_hit = {}; m_position = {}; Q_EMIT changed(); }
}
bool LevelVertexInsertion::begin(const QPointF& point,const LevelComponentGizmoFrame& frame,ModelMoveConstraint constraint,Qt::KeyboardModifiers modifiers)
{
	if (m_active) { return false; }
	const auto hit = pick(point,frame); if (!hit.position.valid) { return false; }
	const auto p = hit.position;
	const bool started = frame.worldAxes ? m_axisDrag.begin(p,frame.ray(point),{frame.perspective,true,-1,m_grid,frame.viewDirection},modifiers)
		: beginModelMoveDrag({float(p.x),float(p.y),float(p.z)},frame.viewDirection,constraint,frame.ray(point),m_grid,&m_drag);
	if (!started) { return false; }
	m_hit = hit; m_position = p; m_frame = frame; m_press = m_lastPoint = point;
	m_modifiers = modifiers;
	m_active = true; m_pending = m_releasePending = m_valid = false; m_error.clear(); m_edges.clear(); ++m_serial;
	Q_EMIT activeChanged(true); Q_EMIT changed(); return m_active;
}
bool LevelVertexInsertion::updateDrag(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (!m_active || m_releasePending) { return false; }
	LevelMapVec3 delta{0,0,0,true}; ModelVec3 legacy;
	const bool reached = finite(point) && (m_frame.worldAxes ? m_axisDrag.update(m_frame.ray(point),modifiers,&delta) : modelMoveDragDelta(m_drag,m_frame.ray(point),&legacy));
	if (!reached) {
		++m_serial; m_valid = m_pending = false; m_edges.clear(); m_error = tr("Cannot reach the insertion plane here."); Q_EMIT changed(); return false;
	}
	m_lastPoint = point;
	m_modifiers = modifiers;
	if (!m_frame.worldAxes) { delta = {legacy.x,legacy.y,legacy.z,true}; }
	const auto travel = point-m_press;
	if (std::hypot(travel.x(),travel.y()) < QApplication::startDragDistance()) { delta = {}; }
	const auto start = m_hit.position;
	const LevelMapVec3 position{start.x+delta.x,start.y+delta.y,start.z+delta.z,true};
	if (same(position,m_position) && m_error.isEmpty()) { return true; }
	++m_serial; m_position = position; m_edges.clear(); m_error.clear(); m_valid = false;
	m_pending = !same(position,start);
	if (m_pending) { Q_EMIT previewRequested(m_serial); }
	Q_EMIT changed(); return true;
}
void LevelVertexInsertion::finish(bool commit)
{
	if (!m_active) { return; }
	if (commit && m_pending) { m_releasePending = true; Q_EMIT changed(); return; }
	const bool apply = commit && m_valid && !same(m_position,m_hit.position);
	m_active = m_pending = m_releasePending = false;
	if (apply) { Q_EMIT commitRequested(m_serial); }
	++m_serial; m_edges.clear(); m_hit = {}; m_position = {};
	Q_EMIT activeChanged(false); Q_EMIT changed();
}
void LevelVertexInsertion::setPreview(quint64 serial,const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges,const QString& error)
{
	if (!m_active || !m_pending || serial != m_serial) { return; }
	m_pending = false; m_valid = error.isEmpty(); m_error = error; m_edges = m_valid ? edges : QVector<QPair<LevelMapVec3,LevelMapVec3>>{};
	if (m_valid && m_frame.worldAxes) { m_axisDrag.accept({m_position.x-m_hit.position.x,m_position.y-m_hit.position.y,m_position.z-m_hit.position.z,true}); }
	Q_EMIT changed(); if (m_releasePending) { finish(true); }
}
QString LevelVertexInsertion::summary() const
{
	if (!m_error.isEmpty()) { return tr("Vertex insertion refused: %1").arg(m_error); }
	if (!m_available) { return tr("Insert Vertex: select editable brushes within the handle limit."); }
	if (!m_hit.position.valid) { return tr("Insert Vertex: drag from a grid point on an edge or face. Edit Brush Components provides numeric insertion."); }
	const auto location = tr("Insert vertex at X %1, Y %2, Z %3 in %n brush(es)",nullptr,m_hit.brushes.size()).arg(m_position.x,0,'g',8).arg(m_position.y,0,'g',8).arg(m_position.z,0,'g',8);
	return location+QStringLiteral(" · ")+(m_active && m_frame.worldAxes ? m_axisDrag.description()+QStringLiteral(" · ") : QString{})
		+(m_pending ? tr("Checking geometry… Escape cancels") : m_valid ? tr("Valid preview · Release to apply") : tr("Drag outward to create a corner"));
}
void LevelVertexInsertion::paint(QPainter& painter,const LevelComponentGizmoFrame& frame,bool highContrast) const
{
	if (!m_hit.position.valid || !frame.project) { return; }
	painter.save(); const QColor colour = highContrast ? QColor(Qt::yellow) : QColor(120,235,210);
	painter.setPen(QPen(colour,highContrast ? 3 : 2,Qt::DashLine));
	for (const auto& edge : m_edges) { const auto a = frame.project(edge.first),b = frame.project(edge.second); if (finite(a) && finite(b)) { painter.drawLine(a,b); } }
	if (m_active) { paintLevelComponentTrace(painter,frame,m_frame.worldAxes ? m_axisDrag.traceOrigin() : m_hit.position,m_position,
		m_frame.worldAxes ? m_axisDrag.axis() : -1,m_pending,highContrast,QApplication::palette()); }
	const auto point = frame.project(m_position);
	if (finite(point)) {
		const QPolygonF diamond{point+QPointF(0,-8),point+QPointF(8,0),point+QPointF(0,8),point+QPointF(-8,0)};
		painter.setPen(QPen(Qt::black,5)); painter.setBrush(Qt::black); painter.drawPolygon(diamond);
		painter.setPen(QPen(colour,2)); painter.drawPolygon(diamond);
		painter.drawLine(point+QPointF(-4,0),point+QPointF(4,0)); painter.drawLine(point+QPointF(0,-4),point+QPointF(0,4));
		if (!m_error.isEmpty()) { painter.drawText(point+QPointF(12,4),QStringLiteral("!")); }
		else if (m_pending) { painter.drawText(point+QPointF(12,4),QStringLiteral("…")); }
	}
	painter.restore();
}
} // namespace vibestudio
