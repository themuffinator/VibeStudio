// The Assemble Brush tool in the plan views. TrenchBroom's Brush Tool works
// in its 3D view; here the 2D views collect the same shared points too: a
// click adds a grid point at the depth of the last point (or the work zone's
// floor), a drag over empty space adds a grid rectangle at that depth, a drag
// on a point moves it and the points under it in this view's plane, and
// Shift-drag extrudes a flat outline whose side this view shows. Enter asks
// the owner for the brush, Backspace drops the last point and Escape clears
// them. Workflow reference: TB AssembleBrushTool at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/map_viewport.h"
#include "app/map_viewport_p.h"

#include <QAccessible>
#include <QLineF>
#include <QPainter>
#include <QScopedValueRollback>

#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
enum AssembleGesture { None = 0, Add = 1, Move = 2, Extrude = 3 };
constexpr double kAssembleDragPixels = 4.0;
constexpr double kAssembleGrabPixels = 7.0;

int hiddenAxis(MapViewportProjection projection)
{
	return projection == MapViewportProjection::TopXY ? 2 : projection == MapViewportProjection::FrontXZ ? 1 : 0;
}

// A point of this view's plane, with the hidden coordinate of `p`.
LevelMapVec3 inPlane(MapViewportProjection projection, const QPointF& point, LevelMapVec3 p)
{
	if (projection == MapViewportProjection::TopXY) {
		p.x = point.x();
		p.y = point.y();
	} else if (projection == MapViewportProjection::FrontXZ) {
		p.x = point.x();
		p.z = point.y();
	} else {
		p.y = point.x();
		p.z = point.y();
	}
	p.valid = true;
	return p;
}

bool finite(const QPointF& p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}
} // namespace

void MapViewport::setBrushAssembleTool(bool enabled)
{
	if (m_assembleTool == enabled) {
		return;
	}
	cancelInteraction();
	m_assembleTool = enabled;
	m_assembleState.points.clear();
	m_assembleOutline = {};
	m_assemblePreview = {};
	m_assembleMessage.clear();
	if (enabled) {
		setCursor(Qt::CrossCursor);
	} else {
		unsetCursor();
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(enabled ? brushAssembleSummary() : hoverSummary());
	Q_EMIT brushAssembleToolChanged(enabled);
}

bool MapViewport::setBrushAssembleState(const LevelBrushAssembleState& state)
{
	if (!state.valid()) {
		return false;
	}
	if (state == m_assembleState) {
		return true;
	}
	if (!m_assembleChanging) {
		m_assembleGesture = None;
		m_assembleDragged = false;
		m_assembleGrabbed.clear();
	}
	m_assembleState = state;
	m_assemblePreview = {};
	m_assembleMessage.clear();
	if (!levelBrushAssembleOutline(m_assembleState.points, &m_assembleOutline, nullptr, false)) {
		m_assembleOutline = {};
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT brushAssembleStateChanged();
	Q_EMIT hoverChanged(brushAssembleSummary());
	return true;
}

void MapViewport::setBrushAssemblePreview(const LevelBrushAssemblePreview& preview)
{
	m_assemblePreview = preview;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(brushAssembleSummary());
	QAccessibleEvent changed(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&changed);
}

QPointF MapViewport::brushAssemblePointPosition(int index) const
{
	if (index < 0 || index >= m_assembleState.points.size()) {
		return {std::numeric_limits<double>::quiet_NaN(), 0};
	}
	const QPointF p = mapViewportProjectPoint(m_projection, m_assembleState.points[index]);
	return worldToView(p.x(), p.y());
}

QVector<int> MapViewport::brushAssemblePointsAt(const QPointF& position) const
{
	// The nearest point within reach, and every point drawn on top of it.
	int nearest = -1;
	double best = kAssembleGrabPixels * kAssembleGrabPixels;
	for (int i = 0; i < m_assembleState.points.size(); ++i) {
		const QPointF delta = brushAssemblePointPosition(i) - position;
		const double distance = QPointF::dotProduct(delta, delta);
		if (finite(delta) && distance <= best) {
			best = distance;
			nearest = i;
		}
	}
	QVector<int> under;
	if (nearest < 0) {
		return under;
	}
	const QPointF at = brushAssemblePointPosition(nearest);
	for (int i = 0; i < m_assembleState.points.size(); ++i) {
		if (QLineF(brushAssemblePointPosition(i), at).length() <= 0.5) {
			under << i;
		}
	}
	return under;
}

bool MapViewport::beginBrushAssemble(const QPointF& position, Qt::KeyboardModifiers modifiers)
{
	if (!m_assembleTool || !isEnabled() || !m_hasDocument || m_assembleGesture != None || !finite(position)) {
		return false;
	}
	m_assembleBefore = m_assembleState;
	m_assemblePress = position;
	m_assembleDragged = false;
	m_assembleGrabbed.clear();
	m_assembleMessage.clear();
	if (modifiers == Qt::ShiftModifier) {
		// TB DuplicateFacePart, seen side-on: extrude the flat outline along
		// its normal as far as the pointer travels along that normal here.
		const QPointF normal = mapViewportProjectPoint(m_projection,
			{m_assembleOutline.plane.normalX, m_assembleOutline.plane.normalY, m_assembleOutline.plane.normalZ, true});
		if (m_assembleOutline.shape != LevelBrushAssembleShape::Polygon) {
			m_assembleMessage = tr("Shift-drag extrudes a flat outline. Drag a rectangle, or click three or more points at one depth, first.");
		} else if (std::hypot(normal.x(), normal.y()) < 0.25) {
			m_assembleMessage = tr("The outline faces this view. Shift-drag it in a view that shows its side.");
		} else {
			m_assembleGesture = Extrude;
			m_assemblePressPlane = viewToWorld(position);
		}
		setAccessibleDescription(accessibleSummary());
		update();
		Q_EMIT hoverChanged(brushAssembleSummary());
		return m_assembleGesture == Extrude;
	}
	if (modifiers != Qt::NoModifier) {
		return false;
	}
	m_assembleGrabbed = brushAssemblePointsAt(position);
	if (!m_assembleGrabbed.isEmpty()) {
		m_assembleGesture = Move;
		return true;
	}
	// New points sit at the last point's depth, or the work zone's floor.
	m_assembleDepth = {0.0, 0.0, 0.0, true};
	const int axis = hiddenAxis(m_projection);
	if (!m_assembleState.points.isEmpty()) {
		m_assembleDepth = m_assembleState.points.last();
	} else {
		const double floor = brushDepthRange(axis).first;
		(axis == 0 ? m_assembleDepth.x : axis == 1 ? m_assembleDepth.y : m_assembleDepth.z) = floor;
	}
	m_assemblePressPlane = snappedPlanePoint(position);
	m_assembleGesture = Add;
	return true;
}

bool MapViewport::updateBrushAssemble(const QPointF& position)
{
	if (m_assembleGesture == None || !finite(position)) {
		return false;
	}
	if (!m_assembleDragged) {
		if (QLineF(position, m_assemblePress).length() < kAssembleDragPixels) {
			return true;
		}
		m_assembleDragged = true;
	}
	auto state = m_assembleBefore;
	QVector<LevelMapVec3> added;
	if (m_assembleGesture == Move) {
		const QPointF to = snappedPlanePoint(position);
		for (const int index : std::as_const(m_assembleGrabbed)) {
			state.points[index] = inPlane(m_projection, to, state.points[index]);
		}
	} else if (m_assembleGesture == Add) {
		// The grid rectangle between the press and the pointer, at one depth.
		const QPointF a = m_assemblePressPlane;
		const QPointF b = snappedPlanePoint(position);
		for (const QPointF& corner : {a, QPointF(b.x(), a.y()), b, QPointF(a.x(), b.y())}) {
			added << inPlane(m_projection, corner, m_assembleDepth);
		}
	} else {
		// Every move replaces the draft from the press-time points. The live
		// outline becomes a volume after the first move and has no plane.
		LevelBrushAssembleOutline original;
		if (!levelBrushAssembleOutline(m_assembleBefore.points, &original, nullptr, false)
			|| original.shape != LevelBrushAssembleShape::Polygon) {
			return false;
		}
		const QPointF normal = mapViewportProjectPoint(m_projection,
			{original.plane.normalX, original.plane.normalY, original.plane.normalZ, true});
		const QPointF travel = viewToWorld(position) - m_assemblePressPlane;
		double distance = QPointF::dotProduct(travel, normal) / QPointF::dotProduct(normal, normal);
		if (m_snapToGrid && m_gridSize > 0) {
			distance = snapLevelMapCoordinate(distance, m_gridSize);
		}
		if (std::isfinite(distance) && distance != 0.0) {
			const LevelMapVec3 offset {original.plane.normalX * distance, original.plane.normalY * distance,
				original.plane.normalZ * distance, true};
			added = levelBrushPointsExtruded(original.corners, offset).mid(original.corners.size());
		}
	}
	if (!levelBrushAssembleAppend(&state.points, added) || !state.valid()) {
		m_assembleMessage = tr("Assemble at most %1 points within ±32768 units.").arg(kLevelBrushPointsMaximum);
		setAccessibleDescription(accessibleSummary());
		Q_EMIT hoverChanged(brushAssembleSummary());
		return false;
	}
	const QScopedValueRollback guard(m_assembleChanging, true);
	return setBrushAssembleState(state);
}

void MapViewport::finishBrushAssemble(bool commit)
{
	if (m_assembleGesture == None) {
		return;
	}
	const int gesture = m_assembleGesture;
	const bool dragged = m_assembleDragged;
	m_assembleGesture = None;
	m_assembleDragged = false;
	m_assembleGrabbed.clear();
	if (!commit) {
		setBrushAssembleState(m_assembleBefore);
	} else if (!dragged && gesture == Add) {
		auto state = m_assembleBefore;
		if (!levelBrushAssembleAppend(&state.points, {inPlane(m_projection, m_assemblePressPlane, m_assembleDepth)}) || !state.valid()) {
			m_assembleMessage = tr("Assemble at most %1 points within ±32768 units.").arg(kLevelBrushPointsMaximum);
		} else {
			setBrushAssembleState(state);
		}
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(brushAssembleSummary());
}

void MapViewport::removeLastBrushAssemblePoint()
{
	finishBrushAssemble(false);
	auto state = m_assembleState;
	if (!state.points.isEmpty()) {
		state.points.removeLast();
		setBrushAssembleState(state);
	}
}

void MapViewport::clearBrushAssemblePoints()
{
	finishBrushAssemble(false);
	setBrushAssembleState({});
}

void MapViewport::requestBrushAssemble()
{
	if (!m_assembleTool) {
		return;
	}
	finishBrushAssemble(true);
	Q_EMIT brushAssembleRequested();
}

QString MapViewport::brushAssembleSummary(bool full) const
{
	if (!m_assembleTool) {
		return {};
	}
	const QString summary = tr("Assemble Brush · %n point(s)", nullptr, static_cast<int>(m_assembleState.points.size()));
	if (!full) {
		return summary;
	}
	if (!m_assembleMessage.isEmpty()) {
		return summary + QStringLiteral(". ") + m_assembleMessage;
	}
	if (!m_assemblePreview.message.isEmpty()) {
		return summary + QStringLiteral(". ") + m_assemblePreview.message;
	}
	return summary + QStringLiteral(". ")
		+ tr("Click to add a grid point at the last point's depth; drag empty space for a rectangle; drag a point to move it; "
			 "Shift-drag a flat outline's side to extrude it. Enter creates the brush; Backspace removes the last point; Escape "
			 "clears the points.");
}

void MapViewport::paintBrushAssemble(QPainter& painter, const Palette& palette) const
{
	if (!m_assembleTool) {
		return;
	}
	painter.save();
	const auto screen = [this](const LevelMapVec3& p) {
		const QPointF q = mapViewportProjectPoint(m_projection, p);
		return worldToView(q.x(), q.y());
	};
	// The checked hull when it is ready; otherwise the outline the points draw.
	const bool ready = m_assemblePreview.status == LevelBrushAssemblePreview::Status::Ready;
	const auto& edges = ready ? m_assemblePreview.edges : m_assembleOutline.edges;
	painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(palette.selection, m_highContrast ? 3 : 2, ready ? Qt::SolidLine : Qt::DashLine));
	for (const auto& edge : edges) {
		painter.drawLine(screen(edge.first), screen(edge.second));
	}
	// Handles: squares, larger while held, never colour alone.
	painter.setPen(QPen(palette.text, 2));
	painter.setBrush(palette.warning);
	for (int i = 0; i < m_assembleState.points.size(); ++i) {
		const QPointF p = brushAssemblePointPosition(i);
		const double half = m_assembleGrabbed.contains(i) ? 6.0 : 4.0;
		painter.drawRect(QRectF(p - QPointF(half, half), QSizeF(half * 2, half * 2)));
	}
	painter.restore();
}
} // namespace vibestudio
