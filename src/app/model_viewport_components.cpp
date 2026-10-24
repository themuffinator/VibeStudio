#include "app/model_viewport.h"
#include "core/level_brush.h"
#include "app/level_component_trace.h"
#include <QAccessible>
#include <QApplication>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {
namespace {
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
bool finite(ModelVec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool same(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
ModelVec3 add(ModelVec3 a, ModelVec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
double squared(QPointF p) { return QPointF::dotProduct(p,p); }
QPointF absent() { return {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()}; }
QRectF axisLabel(QPointF point, const QFont& font)
{
	const QFontMetricsF metrics(font);
	const QSizeF size(std::max(18.0,metrics.horizontalAdvance(QStringLiteral("X"))+10),std::max(18.0,metrics.height()+4));
	return {point-QPointF(size.width()*0.5,size.height()*0.5),size};
}
}

void ModelViewport::setComponentHandles(int mode, const QVector<ModelVec3>& positions, const QSet<int>& picked, bool limited,const QVector<QString>& geometryKeys)
{
	if (mode < 0 || mode > 3 || positions.size() > 16384
		|| std::any_of(positions.cbegin(),positions.cend(),[](auto p) { return !finite(p); })) { return; }
	QSet<int> valid;
	if (!geometryKeys.isEmpty() && geometryKeys.size() != positions.size()) { return; }
	QVector<QString> keys = geometryKeys;
	if (keys.isEmpty()) {
		for (int i = 0; i < positions.size(); ++i) {
			const auto p = positions[i];
			keys << (mode == 1 ? levelComponentGeometryKey({{p.x,p.y,p.z,true}}) : QString::number(i));
		}
	}
	for (int i : picked) { if (i >= 0 && i < positions.size()) { valid.insert(i); } }
	const bool sameGeometry = mode == m_componentMode && keys == m_componentGeometryKeys && limited == m_componentHandlesLimited && positions.size() == m_componentPositions.size()
		&& std::equal(positions.cbegin(),positions.cend(),m_componentPositions.cbegin(),same);
	if (sameGeometry && valid == m_componentPicks) { return; }
	m_componentCycle.reset();
	if (mode && m_clipMode) { setClipMode(false); }
	finishComponentMove(false); m_componentBanding = false;
	m_componentMode = mode; m_componentPositions = mode ? positions : QVector<ModelVec3>{}; m_componentPicks = mode ? valid : QSet<int>{};
	m_componentGeometryKeys = mode ? keys : QVector<QString>{};
	if (mode != 2 && mode != 3) { m_componentSplit = false; }
	if (mode != 3) { m_componentExtrusion = 0; }
	m_componentHandlesLimited = limited;
	if (mode) { finishBrushDraw(false); clearSelectionResizeBox(); finishSurfaceStroke(false); finishEditTransform(false); }
	setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::setComponentXray(bool enabled)
{
	if (enabled == m_componentXray) { return; }
	m_componentCycle.reset();
	finishComponentMove(false); m_componentBanding = false; m_componentXray = enabled;
	setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::setComponentSplit(bool enabled)
{
	enabled = enabled && (m_componentMode == 2 || m_componentMode == 3);
	if (enabled == m_componentSplit) { return; }
	finishComponentMove(false); m_componentSplit = enabled;
	if (enabled) { m_componentExtrusion = 0; }
	setAccessibleDescription(accessibleSummary()); update();
}

void ModelViewport::setComponentExtrusion(int mode, const LevelMapVec3& normal, const QVector<LevelMapVec3>& vertices)
{
	mode = m_componentMode == 3 ? std::clamp(mode,0,2) : 0;
	const auto equal = [](const LevelMapVec3& a,const LevelMapVec3& b) { return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z; };
	if (mode == m_componentExtrusion && equal(normal,m_componentExtrusionNormal) && vertices.size() == m_componentExtrusionVertices.size()
		&& std::equal(vertices.cbegin(),vertices.cend(),m_componentExtrusionVertices.cbegin(),equal)) { return; }
	finishComponentMove(false); m_componentExtrusion = mode; m_componentExtrusionNormal = normal; m_componentExtrusionVertices = vertices;
	if (mode) { m_componentSplit = false; }
	setAccessibleDescription(accessibleSummary()); update();
}

bool ModelViewport::requestComponentExtrusion(double distance)
{
	if (!isEnabled() || !m_componentExtrusion || !m_componentExtrusionNormal.valid || m_componentPicks.isEmpty() || !std::isfinite(distance)) { return false; }
	finishComponentMove(false);
	m_componentMoving = m_componentNumeric = true; m_componentPending = false; m_componentReleasePending = false; m_componentValid = true;
	m_componentWorldDrag = false;
	m_componentError.clear(); m_componentPreviewEdges.clear(); m_componentDelta = {}; m_componentExtrusionDistance = 0;
	Q_EMIT componentMoveActiveChanged(true);
	if (!m_componentMoving) { return false; }
	m_componentExtrusionDistance = distance;
	const auto n = m_componentExtrusionNormal;
	m_componentDelta = {float(n.x*distance),float(n.y*distance),float(n.z*distance)}; m_componentPending = true;
	Q_EMIT componentMovePreviewChanged(m_componentDelta); finishComponentMove(true); return true;
}

QPointF ModelViewport::componentScreenPosition(int index)
{
	if (!m_componentMode || index < 0 || index >= m_componentPositions.size()) { return absent(); }
	ensureProjection();
	auto position = m_componentPositions[index];
	if (m_componentGizmo->active() && m_componentGizmo->tool() != LevelComponentTool::Pivot && m_componentPicks.contains(index)) {
		const auto p = m_componentGizmo->transformed({position.x,position.y,position.z,true}); position = {float(p.x),float(p.y),float(p.z)};
	}
	if (m_componentMoving && m_componentPicks.contains(index)) { position = add(position,m_componentDelta); }
	return projectPoint(m_camera,position,nullptr);
}

bool ModelViewport::componentVisible(int index)
{
	const auto screen = componentScreenPosition(index);
	if (!finite(screen) || !QRectF(rect()).contains(screen)) { return false; }
	if (m_componentXray || (isMovingComponents() && m_componentPicks.contains(index))) { return true; }
	// Compare with the already rendered surface at this pixel. This is bounded
	// by the handle count, independent of scene triangle count, and honours
	// occluders outside the selected brush as well as its own back faces.
	CameraSurfacePoint surface;
	if (!surfacePointAt(screen,&surface)) { return !isRendering(); }
	double handleDepth = 0, surfaceDepth = 0;
	(void)projectPoint(m_camera,m_componentPositions[index],&handleDepth);
	(void)projectPoint(m_camera,{float(surface.position[0]),float(surface.position[1]),float(surface.position[2])},&surfaceDepth);
	return handleDepth >= surfaceDepth-0.15;
}

int ModelViewport::componentAt(const QPointF& point)
{
	if (!m_componentMode || !isEnabled() || !finite(point) || isMovingComponents() || m_looking) { return -1; }
	ensureProjection(); if (isRendering()) { return -1; }
	int result = -1; double nearest = 81, nearestDepth = -std::numeric_limits<double>::infinity();
	for (int i = 0; i < m_componentPositions.size(); ++i) {
		const auto screen = componentScreenPosition(i); const double distance = squared(screen-point);
		if (!finite(screen) || distance > nearest || !componentVisible(i)) { continue; }
		double depth = 0; (void)projectPoint(m_camera,m_componentPositions[i],&depth);
		if (std::abs(distance-nearest) < 0.5 && depth < nearestDepth) { continue; }
		nearest = distance; nearestDepth = depth; result = i;
	}
	return result;
}

void ModelViewport::announceComponentPicks()
{
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	Q_EMIT componentPicksChanged(); Q_EMIT hoverChanged(componentSummary());
}

bool ModelViewport::pickComponentAt(const QPointF& point, bool toggle)
{
	return pickComponentAt(point,toggle ? LevelComponentPick::Toggle : LevelComponentPick::Replace);
}
bool ModelViewport::pickComponentAt(const QPointF& point,LevelComponentPick operation)
{
	const int hit = componentAt(point); if (hit < 0) { return false; }
	m_componentCycle.reset(); m_componentCycle.aim(point);
	QSet<int> coincident{hit}; const auto key = m_componentGeometryKeys.value(hit);
	if (!key.isEmpty()) {
		for (int i = 0; i < m_componentPositions.size(); ++i) { if (m_componentGeometryKeys.value(i) == key) { coincident.insert(i); } }
	}
	m_componentPicks = selectLevelComponentHandles(m_componentPicks,coincident,operation,true);
	announceComponentPicks(); return true;
}

bool ModelViewport::pickComponentBand(const QRectF& band, bool toggle)
{
	return pickComponentBand(band,toggle ? LevelComponentPick::Toggle : LevelComponentPick::Replace);
}
bool ModelViewport::pickComponentBand(const QRectF& band,LevelComponentPick operation)
{
	if (!m_componentMode || !isEnabled() || isMovingComponents() || m_looking || !finite(band.topLeft()) || !finite(band.bottomRight())) { return false; }
	ensureProjection(); if (isRendering()) { return false; }
	const QRectF area = band.normalized();
	m_componentCycle.reset();
	QSet<int> hits;
	for (int i = 0; i < m_componentPositions.size(); ++i) {
		if (!area.contains(componentScreenPosition(i)) || !componentVisible(i)) { continue; }
		hits.insert(i);
	}
	m_componentPicks = selectLevelComponentHandles(m_componentPicks,hits,operation);
	announceComponentPicks(); return true;
}

void ModelViewport::selectAllComponents()
{
	if (!m_componentMode || !isEnabled() || isMovingComponents()) { return; }
	m_componentCycle.reset();
	for (int i = 0; i < m_componentPositions.size(); ++i) { m_componentPicks.insert(i); }
	announceComponentPicks();
}

void ModelViewport::escapeComponents()
{
	if (isMovingComponents() || m_componentNativeArmed) { finishComponentMove(false); return; }
	if (m_componentBanding) { m_componentBanding = false; update(); return; }
	if (m_texturedSurfaceCount > 0) { Q_EMIT texturedSelectionClearRequested(); return; }
	if (!m_componentPicks.isEmpty()) { m_componentCycle.reset(); m_componentPicks.clear(); announceComponentPicks(); return; }
	Q_EMIT componentModeExitRequested();
}

void ModelViewport::setTexturedSurfaceCount(int count)
{
	count = std::max(0,count);
	if (count == m_texturedSurfaceCount) { return; }
	m_texturedSurfaceCount = count; setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent event(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&event);
}

bool ModelViewport::cycleComponentAt(const QPointF& point,int steps)
{
	if (!m_componentMode || !isEnabled() || m_componentHandlesLimited || !finite(point) || !steps || isMovingComponents()
		|| m_componentBanding || m_componentNativeArmed || m_looking || m_panning || m_orbiting || m_pointerDriving || m_pressButton != Qt::NoButton) { return false; }
	ensureProjection(); if (isRendering()) { return false; }
	m_componentCycle.aim(point);
	QVector<LevelComponentCycleHit> hits;
	for (int i = 0; i < m_componentPositions.size(); ++i) {
		const auto screen = componentScreenPosition(i); const double distance = squared(screen-point);
		if (!finite(screen) || distance > 81 || !componentVisible(i)) { continue; }
		double depth = 0; (void)projectPoint(m_camera,m_componentPositions[i],&depth);
		hits << LevelComponentCycleHit{i,depth,distance,m_componentGeometryKeys.value(i)};
	}
	const auto result = cycleLevelComponentHits(hits,m_componentPicks,steps);
	m_componentCycle.ordinal = result.ordinal; m_componentCycle.count = result.count; m_componentCycle.handle = result.handle;
	if (!result.count) { setAccessibleDescription(accessibleSummary()); Q_EMIT hoverChanged(componentSummary()); return false; }
	m_componentPicks = result.picked; announceComponentPicks(); return true;
}

bool ModelViewport::cycleComponentSelection(int steps)
{
	if (m_componentCycle.point) { return cycleComponentAt(*m_componentCycle.point,steps); }
	ensureProjection(); if (isRendering()) { return false; }
	int first = -1;
	for (int i = 0; i < m_componentPositions.size(); ++i) {
		if (!componentVisible(i)) { continue; }
		if (first < 0) { first = i; }
		if (m_componentPicks.contains(i)) { first = i; break; }
	}
	return first >= 0 && cycleComponentAt(componentScreenPosition(first),steps);
}

bool ModelViewport::componentCycleWheel(const QPointF& point,int delta,Qt::KeyboardModifiers modifiers)
{
	if (!m_componentMode || !isEnabled() || m_controls.componentCycleWheelModifiers == Qt::NoModifier
		|| modifiers != m_controls.componentCycleWheelModifiers || !finite(point)) {
		m_componentCycle.remainder = 0; return false;
	}
	if (isMovingComponents() || m_componentBanding || m_componentNativeArmed || m_looking || m_panning || m_orbiting || m_pointerDriving
		|| m_pressButton != Qt::NoButton || isRendering()) { m_componentCycle.remainder = 0; return true; }
	m_componentCycle.aim(point); const int steps = m_componentCycle.wheelSteps(delta);
	if (steps) { cycleComponentAt(point,steps); } return true;
}

bool ModelViewport::componentPivot(ModelVec3* result) const
{
	if (!result || m_componentPicks.isEmpty()) { return false; }
	double x = 0,y = 0,z = 0;
	for (int i : m_componentPicks) { const auto p = m_componentPositions[i]; x += p.x; y += p.y; z += p.z; }
	*result = {float(x/m_componentPicks.size()),float(y/m_componentPicks.size()),float(z/m_componentPicks.size())};
	if (m_componentMoving) { *result = add(*result,m_componentDelta); }
	return true;
}

std::array<QPointF,4> ModelViewport::componentMoveHandles()
{
	std::array<QPointF,4> points; points.fill(absent()); ModelVec3 pivot;
	if (m_componentMoving && m_componentWorldDrag) { return points; }
	if (m_componentGizmo->tool() != LevelComponentTool::Move || m_vertexInsertion->tool() || m_vertexInsertion->active()) { return points; }
	if (!m_componentMode || !isEnabled() || !m_hasMesh || m_looking || !componentPivot(&pivot)) { return points; }
	ensureProjection();
	double depth = 0; points[3] = projectPoint(m_camera,pivot,&depth);
	if (!finite(points[3])) { return points; }
	const double radius = m_perspective ? 72*std::max(1.0,-depth)/m_camera.focal : 72/m_camera.scale;
	if (m_componentExtrusion) {
		const auto n = m_componentExtrusionNormal;
		if (!n.valid) { points.fill(absent()); return points; }
		const ModelVec3 end{pivot.x+float(n.x*radius),pivot.y+float(n.y*radius),pivot.z+float(n.z*radius)};
		const auto tip = projectPoint(m_camera,end,nullptr);
		ModelMoveDrag probe;
		if (finite(tip) && squared(tip-points[3]) >= 100 && beginModelAxisMoveDrag(pivot,{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z},
			{float(n.x),float(n.y),float(n.z)},editRay(tip),&probe)) { points[0] = tip; }
		return points;
	}
	for (int axis = 0; axis < 3; ++axis) {
		auto end = pivot;
		if (axis == 0) { end.x += float(radius); } else if (axis == 1) { end.y += float(radius); } else { end.z += float(radius); }
		const auto tip = projectPoint(m_camera,end,nullptr);
		ModelMoveDrag probe;
		if (finite(tip) && QRectF(rect()).contains(axisLabel(tip,font())) && squared(tip-points[3]) >= 100
			&& beginModelMoveDrag(pivot,{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z},ModelMoveConstraint(axis),editRay(tip),0,&probe)) { points[axis] = tip; }
	}
	return points;
}

int ModelViewport::componentMoveHandleAt(const QPointF& point)
{
	if (!finite(point) || m_componentMoving || isRendering()) { return -1; }
	const auto points = componentMoveHandles(); if (!finite(points[3])) { return -1; }
	if (squared(points[3]-point) <= 49) { return 3; }
	int result = -1; double nearest = 64;
	for (int axis = 0; axis < 3; ++axis) {
		if (!finite(points[axis])) { continue; }
		if (axisLabel(points[axis],font()).contains(point)) { return axis; }
		const auto line = points[axis]-points[3];
		const double t = std::clamp(QPointF::dotProduct(point-points[3],line)/squared(line),0.25,1.0);
		const double distance = squared(point-points[3]-line*t);
		if (distance < nearest) { nearest = distance; result = axis; }
	}
	return result;
}

bool ModelViewport::beginComponentMove(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (m_componentGizmo->active() || m_vertexInsertion->active() || m_componentGizmo->tool() != LevelComponentTool::Move) { return false; }
	if (!isEnabled() || m_looking || m_componentMoving || m_componentBanding || m_pressButton != Qt::NoButton) { return false; }
	ensureProjection(); if (isRendering()) { return false; }
	const int nativeHit = m_controls.componentAxisDrag && !m_componentExtrusion ? componentAt(point) : -1;
	int axis = nativeHit >= 0 ? -1 : componentMoveHandleAt(point);
	const int hit = nativeHit >= 0 ? nativeHit : axis < 0 ? componentAt(point) : -1;
	if (axis < 0 && hit >= 0 && m_componentPicks.contains(hit)) {
		const auto before = m_componentPicks; const auto key = m_componentGeometryKeys.value(hit);
		if (!key.isEmpty()) {
			for (int i = 0; i < m_componentPositions.size(); ++i) { if (m_componentGeometryKeys.value(i) == key) { m_componentPicks.insert(i); } }
		}
		if (before != m_componentPicks) { announceComponentPicks(); }
		axis = 3;
	}
	ModelVec3 pivot;
	if (axis < 0 || !componentPivot(&pivot)) { return false; }
	m_componentWorldDrag = m_controls.componentAxisDrag && !m_componentExtrusion;
	if (m_componentWorldDrag && hit >= 0) { pivot = m_componentPositions[hit]; }
	const auto n = m_componentExtrusionNormal;
	const bool started = m_componentExtrusion ? n.valid && beginModelAxisMoveDrag(pivot,{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z},
		{float(n.x),float(n.y),float(n.z)},editRay(point),&m_componentDrag)
		: m_componentWorldDrag ? m_componentAxisDrag.begin({pivot.x,pivot.y,pivot.z,true},editRay(point),
			{m_perspective,m_componentMode == 1,axis < 3 ? axis : -1,m_moveGrid,{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z}},modifiers)
		: beginModelMoveDrag(pivot,{-m_camera.eye.x,-m_camera.eye.y,-m_camera.eye.z},ModelMoveConstraint(axis),editRay(point),m_moveGrid,&m_componentDrag);
	if (!started) { Q_EMIT hoverChanged(tr("Use another view or numeric editing when the movement axis points along the camera.")); return false; }
	m_componentNumeric = false; m_componentExtrusionDistance = 0;
	m_componentMoving = true; m_componentValid = true; m_componentPending = m_componentReleasePending = false;
	m_componentDelta = {}; m_componentPress = point; m_componentError.clear(); m_componentPreviewEdges.clear();
	m_componentNativeArmed = false; m_componentLastPoint = point; m_componentDragModifiers = modifiers;
	m_heldKeys.clear(); m_flyTimer->stop();
	setAccessibleDescription(accessibleSummary()); update(); Q_EMIT componentMoveActiveChanged(true);
	return m_componentMoving;
}

bool ModelViewport::updateComponentMove(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (m_componentNativeArmed) {
		if (squared(point-m_componentNativePress) < std::pow(QApplication::startDragDistance(),2)) { return true; }
		const auto press = m_componentNativePress; const auto initial = m_componentNativeModifiers; m_componentNativeArmed = false;
		const int hit = componentAt(press); if (hit < 0) { return false; }
		QSet<int> clump{hit}; const auto key = m_componentGeometryKeys.value(hit);
		if (!key.isEmpty()) { for (int i = 0; i < m_componentPositions.size(); ++i) { if (m_componentGeometryKeys.value(i) == key) { clump.insert(i); } } }
		if (!std::all_of(clump.cbegin(),clump.cend(),[&](int id) { return m_componentPicks.contains(id); })) {
			m_componentPicks = clump; announceComponentPicks();
		}
		if (!beginComponentMove(press,initial)) { return false; }
	}
	if (!m_componentMoving || m_componentReleasePending || m_componentNumeric) { return false; }
	m_componentLastPoint = point; m_componentDragModifiers = modifiers;
	ModelVec3 delta;
	const auto previousDistance = m_componentExtrusionDistance;
	bool reached = finite(point);
	if (reached && m_componentExtrusion) {
		double distance = 0;
		reached = modelAxisMoveDragDistance(m_componentDrag,editRay(point),&distance);
		if (reached) {
			distance = snapLevelBrushExtrusionDistance(distance,m_componentExtrusionNormal,m_componentExtrusionVertices,m_moveGrid);
			reached = std::isfinite(distance);
			if (squared(point-m_componentPress) < std::pow(QApplication::startDragDistance(),2)) { distance = 0; }
			m_componentExtrusionDistance = distance;
			const auto n = m_componentExtrusionNormal; delta = {float(n.x*distance),float(n.y*distance),float(n.z*distance)};
		}
	} else if (reached && m_componentWorldDrag) {
		LevelMapVec3 world; reached = m_componentAxisDrag.update(editRay(point),modifiers,&world);
		if (reached) { delta = {float(world.x),float(world.y),float(world.z)}; }
	} else if (reached) { reached = modelMoveDragDelta(m_componentDrag,editRay(point),&delta); }
	if (!reached) {
		m_componentPending = false; m_componentValid = false; m_componentError = tr("Cannot reach the movement plane here.");
		m_componentPreviewEdges.clear(); setAccessibleDescription(accessibleSummary()); update(); return false;
	}
	if (squared(point-m_componentPress) < std::pow(QApplication::startDragDistance(),2)) { delta = {}; }
	if (same(delta,m_componentDelta) && m_componentValid && (!m_componentExtrusion || previousDistance == m_componentExtrusionDistance)) {
		setAccessibleDescription(accessibleSummary()); Q_EMIT hoverChanged(componentSummary()); update(); return true;
	}
	m_componentDelta = delta; m_componentPending = true; m_componentValid = true; m_componentError.clear(); m_componentPreviewEdges.clear();
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT componentMovePreviewChanged(delta); Q_EMIT hoverChanged(componentSummary()); return true;
}

void ModelViewport::setComponentPreview(ModelVec3 delta, const QVector<QPair<ModelVec3,ModelVec3>>& edges, const QString& error)
{
	if (!m_componentMoving || !m_componentPending || !same(delta,m_componentDelta)) { return; }
	m_componentPending = false; m_componentValid = error.isEmpty(); m_componentError = error;
	if (m_componentValid && m_componentWorldDrag) { m_componentAxisDrag.accept({delta.x,delta.y,delta.z,true}); }
	m_componentPreviewEdges = m_componentValid ? edges : QVector<QPair<ModelVec3,ModelVec3>>{};
	setAccessibleDescription(accessibleSummary()); update(); Q_EMIT hoverChanged(componentSummary());
	if (m_componentReleasePending) { finishComponentMove(true); }
}

void ModelViewport::finishComponentMove(bool commit)
{
	if (m_componentNativeArmed) {
		const auto point = m_componentNativePress; const auto keys = m_componentNativeModifiers; m_componentNativeArmed = false;
		if (commit && m_controls.componentQuickSnapModifiers != Qt::NoModifier && keys == m_controls.componentQuickSnapModifiers && requestVertexSnapAt(point)) { return; }
		const auto operation = levelComponentPickOperation(keys,m_controls.toggleModifiers,Qt::NoModifier,false,m_controls.componentBandToggles);
		if (commit && operation) { pickComponentAt(point,*operation); }
	}
	if (!commit && m_vertexInsertion) { m_vertexInsertion->finish(false); m_vertexInsertion->clearHover(); }
	if (!commit && m_componentGizmo) { m_componentGizmo->finish(false); }
	if (!m_componentMoving) { return; }
	if (commit && m_componentPending) {
		m_componentReleasePending = true; m_pressButton = Qt::NoButton;
		setAccessibleDescription(accessibleSummary()); update(); return;
	}
	const auto delta = m_componentDelta;
	const bool apply = commit && m_componentValid && !same(delta,{});
	const QString error = m_componentError;
	m_componentMoving = m_componentPending = m_componentReleasePending = false; m_componentDelta = {};
	m_componentPreviewEdges.clear(); m_pressButton = Qt::NoButton; unsetCursor();
	setAccessibleDescription(accessibleSummary()); update();
	// The owner consumes the prepared draft before retiring the gesture context.
	if (apply) { Q_EMIT componentMoveRequested(delta); }
	Q_EMIT componentMoveActiveChanged(false);
	if (commit && !error.isEmpty()) { Q_EMIT hoverChanged(tr("Components unchanged: %1").arg(error)); }
}

QString ModelViewport::componentSummary() const
{
	if (m_vertexInsertion && (m_vertexInsertion->tool() || m_vertexInsertion->active() || m_vertexInsertion->candidate().position.valid)) { return m_vertexInsertion->summary(); }
	if (!m_componentMode) { return {}; }
	QString overlap;
	if (!isMovingComponents() && m_componentCycle.count && m_componentCycle.handle >= 0 && m_componentCycle.handle < m_componentPositions.size()) {
		const auto p = m_componentPositions[m_componentCycle.handle];
		overlap = tr("Overlap %1 of %2 · XYZ %3, %4, %5 · ").arg(m_componentCycle.ordinal).arg(m_componentCycle.count).arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8);
	}
	if (m_componentGizmo->tool() != LevelComponentTool::Move) { return overlap+m_componentGizmo->summary(); }
	if (m_componentMoving) {
		if (!m_componentValid) { return tr("Components unchanged: %1").arg(m_componentError); }
		const auto delta = m_componentExtrusion ? tr("Extrude: %1 units").arg(m_componentExtrusionDistance,0,'g',8)
			: (m_componentSplit ? tr("Split components: %1, %2, %3") : tr("Move components: %1, %2, %3"))
			.arg(m_componentDelta.x,0,'g',6).arg(m_componentDelta.y,0,'g',6).arg(m_componentDelta.z,0,'g',6);
		return delta + QStringLiteral(" · ") + (m_componentWorldDrag ? m_componentAxisDrag.description()+QStringLiteral(" · ") : QString{})
			+ (m_componentPending ? tr("Checking geometry… Escape cancels") : tr("Valid preview · Release to apply"));
	}
	return overlap+(m_componentHandlesLimited ? tr("Handle limit reached; select fewer objects. ") : QString{})
		+ tr("%n component(s) picked",nullptr,m_componentPicks.size()) + QStringLiteral(" · ")
		+ (m_componentXray ? tr("X-ray handles") : tr("Visible handles")) + QStringLiteral(" · ")
		+ (m_componentExtrusion ? (m_componentExtrusion == 2 ? tr("Split extrusion: drag N or the face handle. Escape cancels.") : tr("Extrude: drag N or the face handle. Escape cancels."))
			: m_componentSplit ? tr("Split: drag XYZ or the centre to create corners. Escape cancels.")
			: tr("Drag XYZ or the centre; Escape clears picks. Edit Brush Components provides numeric editing."));
}

void ModelViewport::paintComponents(QPainter& painter)
{
	if (!m_componentMode || !isEnabled() || m_looking) { return; }
	painter.save();
	const QColor chosen = m_highContrast ? QColor(Qt::yellow) : QColor(255,190,65);
	m_componentGizmo->paint(painter,componentGizmoFrame(),m_highContrast);
	m_vertexInsertion->paint(painter,componentGizmoFrame(),m_highContrast);
	painter.setPen(QPen(chosen,m_highContrast ? 2.5 : 1.5,Qt::DashLine));
	for (const auto& edge : std::as_const(m_componentPreviewEdges)) { QPointF a,b; if (projectSegment(edge.first,edge.second,&a,&b)) { painter.drawLine(a,b); } }
	if (!isRendering()) { for (bool selectedPass : {false,true}) { for (int i = 0; i < m_componentPositions.size(); ++i) {
		if (m_componentPicks.contains(i) != selectedPass) { continue; }
		if (!componentVisible(i)) { continue; }
		const auto point = componentScreenPosition(i); const bool selected = m_componentPicks.contains(i);
		painter.setPen(QPen(selected ? Qt::black : Qt::white,1.5)); painter.setBrush(selected ? chosen : QColor(35,38,45));
		const double radius = selected ? 5 : 3.5;
		if (m_componentMode == 1) { painter.drawEllipse(point,radius,radius); }
		else if (m_componentMode == 2) { painter.drawPolygon(QPolygonF{point+QPointF(-radius,0),point+QPointF(0,-radius),point+QPointF(radius,0),point+QPointF(0,radius)}); }
		else { painter.drawRect(QRectF(point-QPointF(radius,radius),QSizeF(radius*2,radius*2))); }
	} } }
	const auto gizmo = componentMoveHandles();
	if (finite(gizmo[3])) {
		const QColor colours[]{QColor(255,100,100),QColor(120,255,150),QColor(140,180,255)};
		for (int axis = 0; axis < 3; ++axis) {
			if (!finite(gizmo[axis])) { continue; }
			painter.setPen(QPen(Qt::black,5)); painter.drawLine(gizmo[3],gizmo[axis]);
			painter.setPen(QPen(colours[axis],2.5)); painter.drawLine(gizmo[3],gizmo[axis]);
			const auto label = axisLabel(gizmo[axis],font());
			painter.setBrush(Qt::black); painter.drawRect(label); painter.drawText(label,Qt::AlignCenter|Qt::TextForceLeftToRight,m_componentExtrusion ? QStringLiteral("N") : QString(QLatin1Char("XYZ"[axis])));
		}
		painter.setPen(QPen(Qt::black,2)); painter.setBrush(chosen); painter.drawRect(QRectF(gizmo[3]-QPointF(5,5),QSizeF(10,10)));
	}
	if (m_componentMoving && !m_componentNumeric) {
		const auto start = m_componentWorldDrag ? m_componentAxisDrag.origin() : LevelMapVec3{m_componentDrag.origin[0],m_componentDrag.origin[1],m_componentDrag.origin[2],true};
		const auto origin = m_componentWorldDrag ? m_componentAxisDrag.traceOrigin() : start;
		paintLevelComponentTrace(painter,componentGizmoFrame(),origin,{start.x+m_componentDelta.x,start.y+m_componentDelta.y,start.z+m_componentDelta.z,true},
			m_componentWorldDrag ? m_componentAxisDrag.axis() : -1,m_componentPending,m_highContrast,palette());
	}
	if (m_componentBanding) { painter.setPen(QPen(chosen,1.5,Qt::DashLine)); painter.setBrush(Qt::NoBrush); painter.drawRect(QRectF(m_componentPress,m_componentBandEnd).normalized()); }
	painter.restore();
}
} // namespace vibestudio
