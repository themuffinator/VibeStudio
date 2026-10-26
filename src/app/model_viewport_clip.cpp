#include "app/model_viewport.h"
#include "app/viewport_hud.h"
#include "core/level_clip_camera.h"
#include "core/level_component_drag.h"
#include <QAccessible>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {
ModelVec3 model(LevelMapVec3 p) { return {float(p.x),float(p.y),float(p.z)}; }
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
}
void ModelViewport::setClipMode(bool enabled)
{
	if (m_clipMode == enabled) { return; }
	if (enabled && m_componentMode) { setComponentHandles(0,{},{}); Q_EMIT componentModeExitRequested(); }
	finishClipPoint(false); m_clipMode = enabled; m_clipState.points.clear(); m_clipPreview = {};
	if (enabled) { finishComponentMove(false); finishBrushDraw(false); finishSurfaceStroke(false); clearSelectionResizeBox(); }
	setAccessibleDescription(accessibleSummary()); update(); Q_EMIT clipModeChanged(enabled);
}
bool ModelViewport::setClipState(const LevelClipState& state)
{
	if (!state.valid()) { return false; }
	if (state == m_clipState) { return true; }
	if (!m_clipChanging) { m_clipGrabbed = -1; m_clipMovementMessage.clear(); m_clipPointDrag = {}; }
	m_clipState = state; m_clipPreview = {}; setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT clipStateChanged(); Q_EMIT hoverChanged(clipSummary()); return true;
}
void ModelViewport::setClipPreview(const LevelClipPreview& preview)
{
	m_clipPreview = preview; setAccessibleDescription(accessibleSummary()); update(); Q_EMIT hoverChanged(clipSummary());
	QAccessibleEvent changed(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
QPointF ModelViewport::clipPointPosition(int index)
{
	if (index < 0 || index >= m_clipState.points.size()) { return {std::numeric_limits<double>::quiet_NaN(),0}; }
	ensureProjection(); return projectPoint(m_camera,model(m_clipState.points[index]),nullptr);
}
int ModelViewport::clipPointAt(const QPointF& position)
{
	int best = -1; double distance = 100;
	for (int i = 0; i < m_clipState.points.size(); ++i) {
		const auto delta = clipPointPosition(i)-position; const double d = QPointF::dotProduct(delta,delta);
		if (finite(delta) && d <= distance) { distance = d; best = i; }
	}
	return best;
}
bool ModelViewport::beginClipPoint(const QPointF& position)
{
	if (!m_clipMode || !isEnabled() || m_looking || isRendering() || !finite(position) || !m_clipSurfaceResolver) { return false; }
	finishClipPoint(false); m_clipBefore = m_clipState; m_clipGrabbed = clipPointAt(position); m_clipAdding = m_clipGrabbed < 0; m_clipPress = position;
	if (!m_clipAdding) { startClipPointDrag(position); return true; }
	CameraSurfacePoint surface; int triangle = -1;
	if (!surfacePointAt(position,&surface,&triangle)) { return false; }
	LevelMapVec3 point{surface.position[0],surface.position[1],surface.position[2],true};
	if (!m_clipSurfaceResolver(triangle,&point,nullptr) || !LevelClipState::finite(point)) { return false; }
	auto state = m_clipState; if (state.points.size() == 3) { state.points.clear(); }
	state.points << point; m_clipGrabbed = int(state.points.size()-1);
	if (state.points.size() == 1) { state.axis = 2; state.direction = 1; }
	if (!chooseCameraClipAxis(&state,false)) { m_clipGrabbed = -1; return false; }
	const QScopedValueRollback guard(m_clipChanging,true);
	const bool accepted = setClipState(state); if (accepted) { startClipPointDrag(position); } return accepted;
}
void ModelViewport::startClipPointDrag(const QPointF& position)
{
	m_clipLastPoint = position; m_clipMovementMessage.clear(); m_clipPointDrag = {};
	if (!m_perspective || m_controls.clipDragStyle == CameraClipDragStyle::Surface || m_clipGrabbed < 0 || m_clipGrabbed >= m_clipState.points.size()) { return; }
	int axis = 2;
	if (m_controls.clipDragStyle == CameraClipDragStyle::ViewAxes) {
		const auto forward = viewDirection(); const std::array<double,3> direction{forward.x,forward.y,forward.z};
		axis = int(std::max_element(direction.begin(),direction.end(),[](double a,double b) { return std::abs(a) < std::abs(b); })-direction.begin());
	}
	m_clipPointDrag.begin(m_clipState.points[m_clipGrabbed],editRay(position),axis,m_moveGrid);
}
void ModelViewport::updateClipReadout()
{
	setAccessibleDescription(accessibleSummary()); update(); Q_EMIT hoverChanged(clipSummary());
	QAccessibleEvent changed(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&changed);
}
bool ModelViewport::updateClipPointModifiers(int key,Qt::KeyboardModifiers modifiers,bool pressed)
{
	if (m_clipGrabbed < 0 || m_controls.clipDragStyle == CameraClipDragStyle::Surface || !levelComponentModifierKey(key)) { return false; }
	moveClipPoint(m_clipLastPoint,levelComponentModifierState(key,modifiers,pressed)); return true;
}
bool ModelViewport::moveClipPoint(const QPointF& position,Qt::KeyboardModifiers modifiers)
{
	if (m_clipGrabbed < 0 || m_clipGrabbed >= m_clipState.points.size() || !m_clipMode || !isEnabled() || isRendering() || !finite(position)) { return false; }
	m_clipLastPoint = position;
	const bool free = m_perspective && m_controls.clipDragStyle != CameraClipDragStyle::Surface
		&& !modifiers.testFlag(Qt::ControlModifier) && (modifiers.testFlag(Qt::ShiftModifier) || modifiers.testFlag(Qt::AltModifier));
	LevelMapVec3 point;
	if (free) {
		const bool vertical = modifiers.testFlag(Qt::AltModifier); const int axis = m_clipPointDrag.planeAxis();
		m_clipMovementMessage = vertical ? tr("Clip point %1: move along %2").arg(m_clipGrabbed+1).arg(QStringLiteral("XYZ").mid(axis,1))
			: tr("Clip point %1: constrain to strongest %2 axis").arg(m_clipGrabbed+1).arg(QStringList{"YZ","XZ","XY"}[axis]);
		if (!m_clipPointDrag.update(editRay(position),vertical,modifiers.testFlag(Qt::ShiftModifier),&point)) {
			m_clipMovementMessage = tr("The camera cannot resolve this point movement. Release the modifier to use a surface, or cancel and change the view.");
			updateClipReadout(); return false;
		}
	} else {
		m_clipMovementMessage.clear(); CameraSurfacePoint surface; int triangle = -1;
		if (!m_clipSurfaceResolver || !surfacePointAt(position,&surface,&triangle)) { updateClipReadout(); return false; }
		point = {surface.position[0],surface.position[1],surface.position[2],true};
		if (!m_clipSurfaceResolver(triangle,&point,nullptr) || !LevelClipState::finite(point)) { return false; }
	}
	auto state = m_clipState; bool addedSecond = false; int grabbed = m_clipGrabbed;
	if (!free && m_clipAdding && state.points.size() == 1) {
		if (QLineF(position,m_clipPress).length() < 4) { return true; }
		state.points << point; grabbed = 1; addedSecond = true;
	} else { state.points[grabbed] = point; }
	if (m_clipAdding && !m_controls.clipViewAxis && state.points.size() == 2 && !state.plane(nullptr,nullptr,nullptr)) {
		// A vertical segment needs a horizontal extrusion. This choice becomes
		// part of the shared state; changing panes never silently changes it.
		state.axis = 0;
	}
	if (!chooseCameraClipAxis(&state,!addedSecond)) { return false; }
	const QScopedValueRollback guard(m_clipChanging,true);
	if (!setClipState(state)) { return false; }
	m_clipGrabbed = grabbed; if (addedSecond) { startClipPointDrag(position); }
	updateClipReadout(); return true;
}
bool ModelViewport::chooseCameraClipAxis(LevelClipState* state,bool preserveAxis)
{
	if (!m_controls.clipViewAxis || !m_perspective || state->points.size() != 2) { return true; }
	LevelMapVec3 mins,maxs; if (m_clipBoundsResolver) { m_clipBoundsResolver(&mins,&maxs); }
	const auto forward = viewDirection(); QString error;
	if (levelCameraClipAxis(state->points,{m_eye.x,m_eye.y,m_eye.z,true},{forward.x,forward.y,forward.z,true},mins,maxs,
		&state->axis,&state->direction,preserveAxis,&error)) { return true; }
	m_clipMovementMessage = error; updateClipReadout(); return false;
}
void ModelViewport::finishClipPoint(bool accept)
{
	if (m_clipGrabbed < 0) { return; }
	m_clipGrabbed = -1; m_clipMovementMessage.clear(); m_clipPointDrag = {};
	if (!accept) { setClipState(m_clipBefore); } updateClipReadout();
}
bool ModelViewport::matchClipFace(const QPointF& position)
{
	if (!m_clipMode || !isEnabled() || m_looking || !m_clipSurfaceResolver) { return false; }
	finishClipPoint(false); CameraSurfacePoint surface; int triangle = -1;
	if (!surfacePointAt(position,&surface,&triangle)) { return false; }
	auto state = m_clipState;
	return m_clipSurfaceResolver(triangle,nullptr,&state) && setClipState(state);
}
void ModelViewport::removeLastClipPoint()
{
	finishClipPoint(false); auto state = m_clipState; if (!state.points.isEmpty()) { state.points.removeLast(); setClipState(state); }
}
void ModelViewport::requestClip()
{
	if (!m_clipMode) { return; } finishClipPoint(true);
	if (m_clipState.actionable(m_clipPreferences.wrapMerge)) { Q_EMIT clipRequested(); }
}
void ModelViewport::mouseDoubleClickEvent(QMouseEvent* event)
{
	if (m_assembleTool && event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && !m_looking) {
		addBrushAssembleFace(event->position()); event->accept(); return;
	}
	if (m_clipMode && event->button() == Qt::LeftButton) {
		if (event->modifiers() == Qt::AltModifier || (event->modifiers() == Qt::NoModifier && m_clipPreferences.doubleClick == LevelClipDoubleClick::None)) { matchClipFace(event->position()); }
		else if (event->modifiers() == Qt::NoModifier) { activateClipDoubleClick(); }
		event->accept(); return;
	}
	if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && activateGroupAt(event->position())) { event->accept(); return; }
	QWidget::mouseDoubleClickEvent(event);
}
bool ModelViewport::activateClipDoubleClick()
{
	if (!m_clipMode || !isEnabled() || m_looking || isRendering() || m_clipPreferences.doubleClick == LevelClipDoubleClick::None
		|| !m_clipState.actionable(m_clipPreferences.wrapMerge)) { return false; }
	finishClipPoint(true); auto state = m_clipState;
	state.keep = m_clipPreferences.doubleClick == LevelClipDoubleClick::Split ? LevelMapClipKeep::Both
		: state.keep == LevelMapClipKeep::Both ? LevelMapClipKeep::Back : state.keep;
	setClipState(state); requestClip(); return true;
}

void ModelViewport::setGroupContext(const QString& label)
{
	m_groupContextPicking = true;
	if (m_groupContextLabel == label) { return; }
	m_groupContextLabel = label; setAccessibleDescription(accessibleSummary()); update();
}

bool ModelViewport::activateGroupAt(const QPointF& point)
{
	if (!m_groupContextPicking || !isEnabled() || m_clipMode || m_componentMode || m_brushDrawTool || m_looking) { return false; }
	ensureProjection(); const auto hit = hitTest(point);
	Q_EMIT groupActivationRequested(hit.valid ? hit.triangle : -1); return true;
}
QString ModelViewport::clipSummary() const
{
	if (!m_clipMovementMessage.isEmpty()) { return m_clipMovementMessage+(m_clipPreview.message.isEmpty() ? QString() : QStringLiteral("; ")+m_clipPreview.message); }
	if (!m_clipPreview.message.isEmpty()) { return m_clipPreview.message; }
	return m_clipPreferences.doubleClick == LevelClipDoubleClick::None
		? tr("Clip: place points on brush faces; double-click matches a face; Tab chooses sides; Enter cuts.")
		: tr("Clip: double-click a numbered point to apply; Alt+double-click matches a face; Tab chooses sides; Enter cuts.");
}
void ModelViewport::paintClip(QPainter& painter)
{
	if (!m_clipMode) { return; }
	painter.save(); painter.setFont(viewportHudFont(font())); const auto colour = m_highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	painter.setPen(QPen(m_highContrast ? QColor(Qt::cyan) : QColor(120,225,255),m_highContrast ? 3 : 2.5,Qt::DashLine));
	for (const auto& edge : m_clipPreview.edges) {
		QPointF a,b; if (projectSegment(model(edge.first),model(edge.second),&a,&b)) { painter.drawLine(a,b); }
	}
	LevelMapVec3 a,b,c;
	if (m_clipState.plane(&a,&b,&c)) {
		painter.setPen(QPen(colour,2));
		for (const auto& edge : {qMakePair(a,b),qMakePair(b,c),qMakePair(c,a)}) {
			QPointF x,y; if (projectSegment(model(edge.first),model(edge.second),&x,&y)) { painter.drawLine(x,y); }
		}
		const auto plane = planeFromPoints(b,a,c); auto tip = a;
		tip.x += plane.normalX*32; tip.y += plane.normalY*32; tip.z += plane.normalZ*32;
		QPointF x,y; if (projectSegment(model(a),model(tip),&x,&y)) { painter.drawLine(x,y); painter.drawText(y,tr("Front")); }
	}
	for (int i = 0; i < m_clipState.points.size(); ++i) {
		const auto p = clipPointPosition(i); if (!finite(p)) { continue; }
		painter.setPen(QPen(Qt::white,2)); painter.setBrush(Qt::black); painter.drawEllipse(p,6,6);
		int stacked = 0;
		for (int j = 0; j < i; ++j) { if (QLineF(p,clipPointPosition(j)).length() < 12) { ++stacked; } }
		painter.drawText(p+QPointF(10,-10-stacked*QFontMetricsF(painter.font()).height()),QString::number(i+1));
	}
	painter.restore();
}
} // namespace vibestudio
