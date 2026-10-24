#include "app/level_sweep_gizmo.h"
#include <QApplication>
#include <QPainter>
#include <cmath>

namespace vibestudio
{
namespace
{
bool finite(QPointF p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}
bool visible(const LevelComponentGizmoFrame& f, LevelMapVec3 p)
{
	return p.valid && (!f.visible || f.visible(p));
}
double squared(QPointF p)
{
	return QPointF::dotProduct(p, p);
}
LevelMapVec3 add(LevelMapVec3 a, LevelMapVec3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z, true};
}
QRectF labelRect(QPointF point, const QFont& font, const QString& text)
{
	const QFontMetricsF metrics(font);
	const QSizeF size(std::max(24.0, metrics.horizontalAdvance(text) + 12), std::max(22.0, metrics.height() + 6));
	return {point - QPointF(size.width() / 2, size.height() / 2), size};
}
QRectF scaleLabelRect(QPointF centre, QPointF handle, const QFont& font, const QString& text)
{
	auto direction = handle - centre;
	const double length = std::hypot(direction.x(), direction.y());
	direction = length > 1e-6 ? direction / length : QPointF(1, 0);
	const auto rect = labelRect(handle, font, text);
	const double clearance = 12 + (std::abs(direction.x()) * rect.width() + std::abs(direction.y()) * rect.height()) / 2;
	return rect.translated(direction * clearance);
}
} // namespace
LevelSweepGizmo::LevelSweepGizmo(LevelSweepSource source, LevelSweepRequest request, QObject* parent)
    : QObject(parent), m_source(std::move(source)), m_request(std::move(request)), m_rotation(this)
{
	setObjectName(QStringLiteral("levelSweepGizmo"));
	refreshRotation();
	connect(&m_rotation, &LevelComponentGizmo::previewRequested, this, [this](quint64 serial) {
		if (m_handle < 0 || m_handle > 2) {
			return;
		}
		auto next = m_before;
		LevelMapVec3 axis{m_handle == 0 ? 1.0 : 0, m_handle == 1 ? 1.0 : 0, m_handle == 2 ? 1.0 : 0, true};
		if (rotateLevelSweepDestination(&next, axis, m_rotation.request().amount, &m_error)) {
			m_request = std::move(next);
			publish();
		}
		m_rotation.setPreview(serial, {}, m_error);
	});
}
void LevelSweepGizmo::refreshRotation()
{
	if (!dragging() || m_handle >= 3) {
		m_rotation.setContext(LevelComponentTool::Rotate, centre(), true, 0, m_angleStep);
	}
}
void LevelSweepGizmo::setSteps(double grid, double angleStep)
{
	if (!std::isfinite(grid) || grid < 0 || grid > 32768 || !std::isfinite(angleStep) || angleStep < 0 || angleStep > 360) {
		return;
	}
	if (grid == m_grid && angleStep == m_angleStep) {
		return;
	}
	finish(false);
	m_grid = grid;
	m_angleStep = angleStep;
	refreshRotation();
	Q_EMIT changed();
}
void LevelSweepGizmo::setRequest(const LevelSweepRequest& request)
{
	m_request = request;
	refreshRotation();
	Q_EMIT changed();
}
void LevelSweepGizmo::setPreview(bool pending, const QVector<QPair<LevelMapVec3, LevelMapVec3>>& edges, const QString& status)
{
	m_pending = pending;
	m_edges = edges;
	m_status = status;
	Q_EMIT changed();
}
LevelMapVec3 LevelSweepGizmo::centre() const
{
	return levelSweepDestinationCentre(m_source, m_request);
}
LevelMapVec3 LevelSweepGizmo::scaleHandle() const
{
	return levelSweepScaleHandle(m_source, m_request);
}
QVector<LevelComponentGizmoHandle> LevelSweepGizmo::rotationHandles(const LevelComponentGizmoFrame& frame) const
{
	return m_rotation.handles(frame);
}
int LevelSweepGizmo::hitTest(QPointF point, const LevelComponentGizmoFrame& frame, const QFont& font) const
{
	if (dragging() || !frame.project || !finite(point)) {
		return -1;
	}
	const auto s = frame.project(scaleHandle()), c = frame.project(centre());
	if (visible(frame, scaleHandle()) && finite(s) &&
	    (squared(point - s) <= 64 || (finite(c) && scaleLabelRect(c, s, font, tr("Scale")).contains(point)))) {
		return 4;
	}
	if (visible(frame, centre()) && finite(c) && labelRect(c, font, tr("Move")).contains(point)) {
		return 3;
	}
	return visible(frame, centre()) ? m_rotation.hitTest(point, frame, font) : -1;
}
bool LevelSweepGizmo::begin(QPointF point, const LevelComponentGizmoFrame& frame, Qt::KeyboardModifiers modifiers, const QFont& font)
{
	if (!frame.ray || !frame.project) {
		return false;
	}
	const int handle = hitTest(point, frame, font);
	if (handle < 0) {
		return false;
	}
	// TB's centre accepts Alt at the press; rings and scale require no modifiers.
	if (handle == 3 ? (modifiers & ~Qt::AltModifier) != Qt::NoModifier : modifiers != Qt::NoModifier) {
		return false;
	}
	m_before = m_request;
	m_frame = frame;
	m_press = point;
	m_travelled = false;
	m_error.clear();
	if (handle < 3) {
		if (!m_rotation.begin(point, frame, font)) {
			return false;
		}
	} else {
		LevelComponentDragOptions options;
		options.perspective = frame.perspective;
		options.viewDirection = frame.viewDirection;
		options.grid = handle == 3 ? m_grid : 0;
		options.absoluteAllowed = handle == 3;
		if (!m_move.begin(handle == 3 ? centre() : scaleHandle(), frame.ray(point), options, modifiers)) {
			return false;
		}
	}
	m_handle = handle;
	Q_EMIT changed();
	return true;
}
bool LevelSweepGizmo::updateDrag(QPointF point, Qt::KeyboardModifiers modifiers)
{
	if (!dragging()) {
		return false;
	}
	if (!finite(point)) {
		m_error = tr("Cannot reach the sweep handle here.");
		Q_EMIT changed();
		return false;
	}
	if (!m_travelled && squared(point - m_press) < std::pow(QApplication::startDragDistance(), 2)) {
		return true;
	}
	m_travelled = true;
	if (m_handle < 3) {
		if(!m_rotation.updateDrag(point)) {
			m_error=tr("Cannot reach the sweep handle here."); Q_EMIT changed(); return false;
		}
		return true;
	}
	LevelMapVec3 delta;
	if (!m_move.update(m_frame.ray(point), modifiers, &delta)) {
		m_error = tr("Cannot reach the sweep handle here.");
		Q_EMIT changed();
		return false;
	}
	auto next = m_before;
	if (m_handle == 3) {
		next.translation = add(m_before.translation, delta);
	} else if (!scaleLevelSweepTo(m_source, &next, add(levelSweepScaleHandle(m_source, m_before), delta), m_grid, &m_error)) {
		Q_EMIT changed();
		return false;
	}
	m_move.accept(delta);
	m_error.clear();
	m_request = std::move(next);
	publish();
	return true;
}
void LevelSweepGizmo::publish()
{
	m_pending = true;
	m_edges.clear();
	refreshRotation();
	Q_EMIT requestChanged(m_request);
	Q_EMIT changed();
}
void LevelSweepGizmo::finish(bool keep)
{
	if (!dragging()) {
		return;
	}
	const auto before = m_before;
	m_handle = -1;
	m_rotation.finish(false);
	if (!keep || !m_error.isEmpty()) {
		m_request = before;
		m_error.clear();
		publish();
	} else {
		refreshRotation();
		Q_EMIT changed();
	}
}
void LevelSweepGizmo::reset()
{
	finish(false);
	m_request.translation = {0, 0, 0, true};
	m_request.rotationAxis = {0, 0, 1, true};
	m_request.angle = 0;
	m_request.scale = {1, 1, 1, true};
	m_error.clear();
	publish();
}
bool LevelSweepGizmo::cancel()
{
	if (dragging()) {
		finish(false);
		return true;
	}
	const auto& r = m_request;
	if (r.translation.x != 0 || r.translation.y != 0 || r.translation.z != 0 || r.angle != 0 || r.scale.x != 1 || r.scale.y != 1 ||
	    r.scale.z != 1) {
		reset();
		return true;
	}
	return false;
}
bool LevelSweepGizmo::scaleStep(int direction)
{
	if (dragging() || (direction != -1 && direction != 1)) {
		return false;
	}
	auto next = m_request;
	if (!stepLevelSweepScale(m_source, &next, direction * (m_grid > 0 ? m_grid : 1), m_grid, &m_error)) {
		Q_EMIT changed();
		return false;
	}
	m_request = std::move(next);
	publish();
	return true;
}
bool LevelSweepGizmo::translateDestination(const LevelMapVec3& delta)
{
	// TB MapViewBase::moveSweepCenter at 4c756be1 (GPL-3.0-or-later): a move
	// key moves the destination by a grid step. Original implementation.
	if (dragging() || !delta.valid || !std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z)) {
		return false;
	}
	m_request.translation = add(m_request.translation, delta);
	m_error.clear();
	publish();
	return true;
}
bool LevelSweepGizmo::rotateDestination(const LevelMapVec3& axis, double degrees, QString* error)
{
	// TB MapViewBase::rotate and SweepTool::rotateDestinationCap at 4c756be1
	// (GPL-3.0-or-later): a turn key turns the destination cap by the grid
	// angle. Original implementation.
	if (dragging()) {
		if (error) { *error = tr("Finish dragging the sweep destination before turning it."); }
		return false;
	}
	if (!std::isfinite(degrees) || degrees == 0) {
		if (error) { *error = tr("The sweep destination needs a turn angle other than zero."); }
		return false;
	}
	auto next = m_request;
	if (!rotateLevelSweepDestination(&next, axis, degrees, error)) {
		return false;
	}
	m_request = std::move(next);
	m_error.clear();
	publish();
	return true;
}
QString LevelSweepGizmo::summary() const
{
	const auto p = centre();
	return tr("Sweep destination: X %1, Y %2, Z %3; scale %4, %5, %6; rotation %7°.")
	           .arg(p.x, 0, 'g', 8)
	           .arg(p.y, 0, 'g', 8)
	           .arg(p.z, 0, 'g', 8)
	           .arg(m_request.scale.x, 0, 'g', 6)
	           .arg(m_request.scale.y, 0, 'g', 6)
	           .arg(m_request.scale.z, 0, 'g', 6)
	           .arg(m_request.angle, 0, 'g', 6) +
	       QLatin1Char(' ') + (m_error.isEmpty() ? m_status : m_error);
}
void LevelSweepGizmo::paint(QPainter& painter, const LevelComponentGizmoFrame& frame, bool highContrast) const
{
	if (!frame.project) {
		return;
	}
	painter.save();
	painter.setRenderHint(QPainter::Antialiasing);
	const QColor accent = highContrast ? QColor(Qt::yellow) : QColor("#ffbe41");
	const auto line = [&](LevelMapVec3 a, LevelMapVec3 b) {
		if (!visible(frame, a) || !visible(frame, b)) {
			return;
		}
		const auto x = frame.project(a), y = frame.project(b);
		if (finite(x) && finite(y)) {
			painter.drawLine(x, y);
		}
	};
	painter.setPen(QPen(accent, highContrast ? 2.5 : 1.5, Qt::DashLine));
	for (const auto& edge : m_edges) {
		line(edge.first, edge.second);
	}
	const auto caps = levelSweepDestinationCaps(m_source, m_request);
	for (int c = 0; c < caps.size(); ++c) {
		auto colour = accent;
		const bool first = c % std::max(1, m_request.iterations) == 0;
		if (!first) {
			colour.setAlpha(100);
		}
		painter.setPen(QPen(colour, first ? 2.5 : 1.5, m_pending ? Qt::DashLine : Qt::SolidLine));
		for (int i = 0; i < caps[c].size(); ++i) {
			line(caps[c][i], caps[c][(i + 1) % caps[c].size()]);
		}
	}
	if (visible(frame, centre())) {
		m_rotation.paint(painter, frame, highContrast);
	}
	painter.setPen(QPen(accent, 2));
	line(centre(), scaleHandle());
	const QVector<QPair<LevelMapVec3, QString>> handles{{centre(), tr("Move")}, {scaleHandle(), tr("Scale")}};
	for (int index = 0; index < handles.size(); ++index) {
		const auto& handle = handles[index];
		if (!visible(frame, handle.first)) {
			continue;
		}
		const auto p = frame.project(handle.first);
		if (!finite(p)) {
			continue;
		}
		auto rect = labelRect(p, painter.font(), handle.second);
		if (index == 1) {
			const auto c = frame.project(centre());
			if (finite(c)) {
				rect = scaleLabelRect(c, p, painter.font(), handle.second);
			}
			painter.setBrush(Qt::black);
			painter.setPen(QPen(accent, 2));
			painter.drawLine(p, rect.center());
			painter.drawEllipse(p, 5, 5);
		}
		painter.setBrush(Qt::black);
		painter.setPen(QPen(accent, 2));
		painter.drawRoundedRect(rect, 3, 3);
		painter.drawText(rect, Qt::AlignCenter, handle.second);
	}
	painter.restore();
}
} // namespace vibestudio
