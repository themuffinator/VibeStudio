// The Assemble Brush tool in the camera: TrenchBroom's Brush Tool. A click
// puts a grid point on the brush face under the pointer, or on the
// construction plane where no face is; a drag from a face draws the grid
// rectangle on its plane; Shift-drag extrudes a flat outline along its
// normal by grid steps; double-click adds a face's corners. Enter asks the
// owner for the brush, Backspace drops the last point and Escape clears them.
// Workflow reference: TB AssembleBrushToolController3D at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/model_viewport.h"
#include "app/viewport_hud.h"

#include <QAccessible>
#include <QLineF>
#include <QPainter>
#include <QScopedValueRollback>

#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
enum AssembleGesture { None = 0, Face = 1, Extrude = 2 };
constexpr double kAssembleDragThreshold = 4.0;

ModelVec3 model(const LevelMapVec3& p)
{
	return {float(p.x), float(p.y), float(p.z)};
}

bool finite(const QPointF& p)
{
	return std::isfinite(p.x()) && std::isfinite(p.y());
}

LevelMapVec3 normalOf(const MapPlane& plane)
{
	return {plane.normalX, plane.normalY, plane.normalZ, true};
}
} // namespace

void ModelViewport::setBrushAssembleTool(bool enabled)
{
	if (m_assembleTool == enabled) {
		return;
	}
	finishBrushAssemble(false);
	if (enabled) {
		// One construction tool at a time, as setBrushDrawTool() does.
		if (m_clipMode) {
			setClipMode(false);
		}
		if (m_componentMode) {
			setComponentHandles(0, {}, {});
			Q_EMIT componentModeExitRequested();
		}
		setBrushDrawTool(false);
		setSurfaceTool(ModelViewportSurfaceTool::None);
		finishEditTransform(false);
		finishSelectionResize(false);
	}
	m_assembleTool = enabled;
	m_assembleState.points.clear();
	m_assembleOutline = {};
	m_assemblePreview = {};
	m_assembleMessage.clear();
	m_assembleHoverOutline = false;
	if (enabled) {
		setCursor(Qt::CrossCursor);
	} else {
		unsetCursor();
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(brushAssembleSummary(true));
	Q_EMIT brushAssembleToolChanged(enabled);
}

bool ModelViewport::setBrushAssembleState(const LevelBrushAssembleState& state)
{
	if (!state.valid()) {
		return false;
	}
	if (state == m_assembleState) {
		return true;
	}
	if (!m_assembleChanging) {
		// The points changed elsewhere: a gesture here is no longer current.
		m_assembleGesture = None;
		m_assembleDragged = false;
	}
	m_assembleState = state;
	m_assemblePreview = {};
	m_assembleMessage.clear();
	// The flat outline, which Shift-drag picks, without the costly hull.
	if (!levelBrushAssembleOutline(m_assembleState.points, &m_assembleOutline, nullptr, false)) {
		m_assembleOutline = {};
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT brushAssembleStateChanged();
	Q_EMIT hoverChanged(brushAssembleSummary(true));
	return true;
}

void ModelViewport::setBrushAssemblePreview(const LevelBrushAssemblePreview& preview)
{
	m_assemblePreview = preview;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(brushAssembleSummary(true));
	QAccessibleEvent changed(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&changed);
}

QPointF ModelViewport::brushAssemblePointPosition(int index)
{
	if (index < 0 || index >= m_assembleState.points.size()) {
		return {std::numeric_limits<double>::quiet_NaN(), 0};
	}
	ensureProjection();
	return projectPoint(m_camera, model(m_assembleState.points[index]), nullptr);
}

bool ModelViewport::brushAssembleSurfaceAt(const QPointF& point, LevelBrushAssembleSurface* surface, LevelMapVec3* hit)
{
	m_assembleMessage.clear();
	if (isRendering()) {
		m_assembleMessage = tr("The camera is still rendering. Try again when it finishes.");
		return false;
	}
	CameraSurfacePoint sampled;
	int triangle = -1;
	ensureProjection();
	if (surfacePointAt(point, &sampled, &triangle)) {
		// A brush face: TB adds points only on brushes.
		if (!m_assembleSurfaceResolver || !m_assembleSurfaceResolver(triangle, surface)) {
			m_assembleMessage = tr("Points go on brush faces. Click a brush, or empty space for the construction plane.");
			return false;
		}
		*hit = {sampled.position[0], sampled.position[1], sampled.position[2], true};
		// Where the render and the face disagree slightly, put the hit on the face.
		const double off = planeDistanceToPoint(surface->plane, *hit);
		hit->x -= surface->plane.normalX * off;
		hit->y -= surface->plane.normalY * off;
		hit->z -= surface->plane.normalZ * off;
		return true;
	}
	if (hitAt(point).valid) {
		m_assembleMessage = tr("Points go on brush faces. Click a brush, or empty space for the construction plane.");
		return false;
	}
	// Nothing under the pointer: the construction plane Draw Brush uses.
	MapPlane plane;
	plane.valid = true;
	plane.distance = m_brushDraw.base;
	(m_brushDraw.axis == 0 ? plane.normalX : m_brushDraw.axis == 1 ? plane.normalY : plane.normalZ) = 1.0;
	if (!levelBrushAssembleRayPlane(selectionResizeRay(point), plane, hit)) {
		m_assembleMessage = tr("Cannot reach the construction plane here. Aim at a brush face, or turn the camera towards the plane.");
		return false;
	}
	*surface = {plane, {}};
	return true;
}

bool ModelViewport::beginBrushAssemble(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	if (!m_assembleTool || !isEnabled() || m_looking || m_assembleGesture != None || !finite(point)) {
		return false;
	}
	ensureProjection();
	m_assembleBefore = m_assembleState;
	m_assemblePress = point;
	m_assembleDragged = false;
	if (modifiers == Qt::ShiftModifier) {
		// TB DuplicateFacePart: Shift-drag a flat outline to extrude it.
		LevelMapVec3 hit;
		if (!levelBrushAssembleRayPolygon(selectionResizeRay(point), m_assembleOutline, &hit)) {
			m_assembleMessage = m_assembleOutline.shape == LevelBrushAssembleShape::Polygon
				? tr("Shift-drag starts on the flat outline. Aim at the outline's face.")
				: tr("Shift-drag extrudes a flat outline. Draw a face, or click three or more points on one plane, first.");
			updateBrushAssembleReadout();
			return false;
		}
		m_assembleGesture = Extrude;
		m_assembleAnchor = hit;
		m_assembleSurface = {m_assembleOutline.plane, m_assembleOutline.corners};
		updateBrushAssembleReadout();
		return true;
	}
	if (modifiers != Qt::NoModifier) {
		return false;
	}
	LevelBrushAssembleSurface surface;
	LevelMapVec3 hit;
	if (!brushAssembleSurfaceAt(point, &surface, &hit)) {
		updateBrushAssembleReadout();
		return false;
	}
	m_assembleGesture = Face;
	m_assembleAnchor = hit;
	m_assembleSurface = surface;
	return true;
}

bool ModelViewport::updateBrushAssemble(const QPointF& point)
{
	if (m_assembleGesture == None || !finite(point)) {
		return false;
	}
	if (!m_assembleDragged) {
		if (QLineF(point, m_assemblePress).length() < kAssembleDragThreshold) {
			return true;
		}
		m_assembleDragged = true;
	}
	ensureProjection();
	const auto ray = selectionResizeRay(point);
	auto state = m_assembleBefore;
	QVector<LevelMapVec3> added;
	if (m_assembleGesture == Face) {
		// TB DrawFacePart: the grid rectangle on the face's plane.
		LevelMapVec3 to;
		if (!levelBrushAssembleRayPlane(ray, m_assembleSurface.plane, &to)) {
			m_assembleMessage = tr("The pointer is off the face's plane. Turn the camera towards it.");
			updateBrushAssembleReadout();
			return false;
		}
		added = levelBrushAssembleRectangle(m_assembleSurface.plane, m_assembleAnchor, to, m_moveGrid);
	} else {
		// TB DuplicateFacePart: the outline's corners moved along its normal.
		double distance = 0;
		const auto normal = normalOf(m_assembleSurface.plane);
		if (!levelBrushAssembleExtrusionDistance(ray, m_assembleAnchor, normal, m_moveGrid, &distance)) {
			m_assembleMessage = tr("The camera looks along the extrusion. Turn it to see the outline's side.");
			updateBrushAssembleReadout();
			return false;
		}
		if (distance != 0.0) {
			added = levelBrushPointsExtruded(m_assembleSurface.winding,
				{normal.x * distance, normal.y * distance, normal.z * distance, true}).mid(m_assembleSurface.winding.size());
		}
	}
	if (!levelBrushAssembleAppend(&state.points, added) || !state.valid()) {
		m_assembleMessage = tr("Assemble at most %1 points within ±32768 units.").arg(kLevelBrushPointsMaximum);
		updateBrushAssembleReadout();
		return false;
	}
	m_assembleMessage.clear();
	const QScopedValueRollback guard(m_assembleChanging, true);
	return setBrushAssembleState(state);
}

void ModelViewport::finishBrushAssemble(bool commit)
{
	if (m_assembleGesture == None) {
		return;
	}
	const int gesture = m_assembleGesture;
	const bool dragged = m_assembleDragged;
	m_assembleGesture = None;
	m_assembleDragged = false;
	if (!commit) {
		setBrushAssembleState(m_assembleBefore);
		updateBrushAssembleReadout();
		return;
	}
	if (!dragged && gesture == Face) {
		// TB AssembleBrushToolController3D::mouseClick: the grid point on the face.
		auto state = m_assembleBefore;
		const auto point = levelBrushAssembleSurfacePoint(m_assembleAnchor, m_assembleSurface, m_moveGrid);
		if (!levelBrushAssembleAppend(&state.points, {point}) || !state.valid()) {
			m_assembleMessage = tr("Assemble at most %1 points within ±32768 units.").arg(kLevelBrushPointsMaximum);
		} else {
			setBrushAssembleState(state);
		}
	}
	updateBrushAssembleReadout();
}

bool ModelViewport::addBrushAssembleFace(const QPointF& point)
{
	if (!m_assembleTool || !isEnabled() || m_looking || !finite(point)) {
		return false;
	}
	finishBrushAssemble(false);
	LevelBrushAssembleSurface surface;
	LevelMapVec3 hit;
	if (!brushAssembleSurfaceAt(point, &surface, &hit) || surface.winding.isEmpty()) {
		if (m_assembleMessage.isEmpty()) {
			m_assembleMessage = tr("Double-click a brush face to add its corners.");
		}
		updateBrushAssembleReadout();
		return false;
	}
	// TB AssembleBrushToolController3D::mouseDoubleClick: the face's corners.
	auto state = m_assembleState;
	if (!levelBrushAssembleAppend(&state.points, surface.winding) || !state.valid()) {
		m_assembleMessage = tr("Assemble at most %1 points within ±32768 units.").arg(kLevelBrushPointsMaximum);
		updateBrushAssembleReadout();
		return false;
	}
	return setBrushAssembleState(state);
}

void ModelViewport::removeLastBrushAssemblePoint()
{
	finishBrushAssemble(false);
	auto state = m_assembleState;
	if (!state.points.isEmpty()) {
		state.points.removeLast();
		setBrushAssembleState(state);
	}
}

void ModelViewport::clearBrushAssemblePoints()
{
	finishBrushAssemble(false);
	setBrushAssembleState({});
}

void ModelViewport::requestBrushAssemble()
{
	if (!m_assembleTool) {
		return;
	}
	finishBrushAssemble(true);
	Q_EMIT brushAssembleRequested();
}

void ModelViewport::updateBrushAssembleHover(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	bool over = false;
	if (m_assembleTool && m_assembleGesture == None && modifiers == Qt::ShiftModifier && finite(point)
		&& m_assembleOutline.shape == LevelBrushAssembleShape::Polygon) {
		ensureProjection();
		over = levelBrushAssembleRayPolygon(selectionResizeRay(point), m_assembleOutline, nullptr);
	}
	if (over != m_assembleHoverOutline) {
		m_assembleHoverOutline = over;
		update();
	}
}

void ModelViewport::updateBrushAssembleReadout()
{
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT hoverChanged(brushAssembleSummary(true));
}

QString ModelViewport::brushAssembleSummary(bool full) const
{
	if (!m_assembleTool) {
		return {};
	}
	const int count = static_cast<int>(m_assembleState.points.size());
	QString summary = tr("Assemble Brush · %n point(s)", nullptr, count);
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
		+ tr("Click brush faces, or empty space for the construction plane, to add grid points; drag on a face to draw a rectangle; "
			 "Shift-drag a flat outline to extrude it; double-click a face to add its corners. Enter creates the brush; Backspace "
			 "removes the last point; Escape clears the points.");
}

void ModelViewport::paintBrushAssemble(QPainter& painter)
{
	if (!m_assembleTool) {
		return;
	}
	ensureProjection();
	painter.save();
	painter.setBrush(Qt::NoBrush);
	const QColor handle = m_highContrast ? QColor(Qt::yellow) : QColor(255, 190, 65);
	const QColor wire = m_highContrast ? QColor(Qt::cyan) : QColor(120, 225, 255);
	// The flat outline Shift-drag picks, filled while it can be picked.
	if ((m_assembleHoverOutline || m_assembleGesture == Extrude) && m_assembleOutline.shape == LevelBrushAssembleShape::Polygon) {
		QPolygonF polygon;
		for (const auto& corner : std::as_const(m_assembleOutline.corners)) {
			const QPointF p = projectPoint(m_camera, model(corner), nullptr);
			if (!finite(p)) {
				polygon.clear();
				break;
			}
			polygon << p;
		}
		QColor fill = handle;
		fill.setAlpha(m_highContrast ? 140 : 90);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QBrush(fill, m_highContrast ? Qt::BDiagPattern : Qt::SolidPattern));
		painter.drawPolygon(polygon);
		painter.setBrush(Qt::NoBrush);
	}
	// The checked hull when it is ready; otherwise the outline the points draw.
	const auto& edges = m_assemblePreview.status == LevelBrushAssemblePreview::Status::Ready ? m_assemblePreview.edges : m_assembleOutline.edges;
	const bool ready = m_assemblePreview.status == LevelBrushAssemblePreview::Status::Ready;
	painter.setPen(QPen(wire, m_highContrast ? 3 : 2, ready ? Qt::SolidLine : Qt::DashLine));
	for (const auto& edge : edges) {
		QPointF a;
		QPointF b;
		if (projectSegment(model(edge.first), model(edge.second), &a, &b)) {
			painter.drawLine(a, b);
		}
	}
	// Handles: a square marks every point, never colour alone.
	painter.setPen(QPen(m_highContrast ? QColor(Qt::black) : QColor(20, 24, 29), 2));
	painter.setBrush(handle);
	for (int i = 0; i < m_assembleState.points.size(); ++i) {
		const QPointF p = brushAssemblePointPosition(i);
		if (finite(p)) {
			painter.drawRect(QRectF(p - QPointF(4, 4), QSizeF(8, 8)));
		}
	}
	painter.restore();
}
} // namespace vibestudio
