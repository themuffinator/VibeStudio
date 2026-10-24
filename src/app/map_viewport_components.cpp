// Vertex editing in the 2D views, after Radiant's vertex, edge and face
// modes and TrenchBroom's vertex tool, and Radiant's Region Set XY drawing.
// The viewport only picks and previews; the shell commits a move through
// moveLevelMapComponents() as one undo step.

#include "app/map_viewport.h"
#include "app/map_viewport_p.h"

#include "core/level_brush.h"
#include "core/level_camera_keys.h"

#include <QKeyEvent>
#include <QAccessible>
#include <QMouseEvent>
#include "app/level_component_trace.h"
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr double kHandlePickPixels = 7.0;
constexpr double kHandleStackPixels = 0.75;
constexpr double kComponentDragPixels = 4.0;
constexpr int kComponentObjectLimit = 128;
constexpr int kComponentHandleLimit = 16384;

bool sameRef(const LevelMapSelectionRef& a, const LevelMapSelectionRef& b) { return a.kind == b.kind && a.objectId == b.objectId; }

bool samePosition(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01 && std::abs(a.z - b.z) < 0.01;
}

} // namespace

bool MapViewport::canAddComponentPatches(const QVector<int>& patches) const
{
	if (m_componentMode == ComponentMode::Off) { return true; }
	if (m_componentHandlesLimited) { return false; }
	qsizetype objects = m_componentSelectionTopology.size(), handles = m_componentHandles.size();
	QSet<int> existing;
	for (const auto& object : m_componentSelectionTopology) {
		if (object.object.kind == LevelMapSelectionKind::QuakePatch) { existing.insert(object.object.objectId); }
	}
	for (int id : patches) {
		if (existing.contains(id)) { continue; }
		const auto patch = std::find_if(m_document.patches.cbegin(),m_document.patches.cend(),[id](const auto& p) { return p.id == id; });
		if (patch == m_document.patches.cend()) { return false; }
		existing.insert(id); ++objects; handles += patch->controlPoints.size();
		if (objects > kComponentObjectLimit || handles > kComponentHandleLimit) { return false; }
	}
	return true;
}

void MapViewport::setTexturedSurfaceFaces(const QVector<LevelSurfaceFace>& faces)
{
	if (m_texturedFaces == faces) { return; }
	m_texturedFaces = faces; m_texturedFaceKeys.clear();
	for (const auto& face : faces) { m_texturedFaceKeys.insert({face.brushId,face.faceIndex}); }
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent event(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&event);
}

void MapViewport::setComponentMode(ComponentMode mode)
{
	if (mode != ComponentMode::Off && (!m_hasDocument || m_document.format == LevelMapFormat::DoomWad)) {
		mode = ComponentMode::Off;
	}
	if (mode == m_componentMode) {
		return;
	}
	// The shell converts exact solved IDs before switching every pane. A raw
	// viewport mode change starts clean until that converted selection is set.
	finishComponentMove(false);
	m_componentMode = mode;
	if (mode != ComponentMode::Edges && mode != ComponentMode::Faces) { m_componentSplit = false; }
	if (mode != ComponentMode::Faces) { m_componentExtrusion = 0; }
	m_componentArmed = m_componentDragging = m_componentBanding = false;
	m_componentGrabbed = -1;
	m_componentRepick.clear();
	m_componentRepickFallback.clear();
	m_pickedHandles.clear();
	if (mode != ComponentMode::Off) {
		cancelInteraction();
		setCursor(Qt::CrossCursor);
	} else {
		unsetCursor();
	}
	rebuildComponentHandles();
	setAccessibleDescription(accessibleSummary());
	Q_EMIT hoverChanged(mode == ComponentMode::Off ? hoverSummary() : componentSummary());
	update();
	Q_EMIT componentModeChanged(static_cast<int>(mode));
	Q_EMIT componentPicksChanged(0);
}

QString MapViewport::componentModeName() const
{
	switch (m_componentMode) {
	case ComponentMode::Vertices: return tr("Vertices");
	case ComponentMode::Edges: return tr("Edges");
	case ComponentMode::Faces: return tr("Faces");
	case ComponentMode::Off: break;
	}
	return {};
}

void MapViewport::rebuildComponentHandles()
{
	m_componentCycle.reset();
	finishComponentMove(false);
	// Keep what is picked by object and position, so picks survive an edit
	// that renumbers a brush's vertices.
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> wanted = m_componentRepick;
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> fallback = m_componentRepickFallback;
	if (wanted.isEmpty() && fallback.isEmpty()) {
		wanted = pickedComponentPositions();
	}
	m_componentRepick.clear();
	m_componentRepickFallback.clear();
	m_componentHandles.clear();
	m_componentSelectionTopology.clear();
	m_componentInsertionData = std::make_shared<LevelVertexInsertionData>();
	m_componentHandlesLimited = false;
	m_pickedHandles.clear();
	if (m_componentMode == ComponentMode::Off || !m_hasDocument || m_document.format == LevelMapFormat::DoomWad) {
		return;
	}
	QSet<int> brushes, patches, entities;
	for (const LevelMapSelectionRef& ref : std::as_const(m_selection)) {
		if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			patches.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::Entity) {
			entities.insert(ref.objectId);
		}
	}
	int objects = 0;
	const auto full = [this, &objects]() {
		return objects >= kComponentObjectLimit || m_componentHandles.size() >= kComponentHandleLimit;
	};
	for (const LevelMapBrush& brush : std::as_const(m_document.brushes)) {
		const auto object = levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush,brush.id});
		if (!m_sceneContext.allowedObjects.contains(object) || m_sceneContext.closedGroups.contains(object)) { continue; }
		if (!brushes.contains(brush.id) && !entities.contains(brush.entityId)) {
			continue;
		}
		if (full()) { m_componentHandlesLimited = true; break; }
		LevelBrushTopology topology;
		if (!levelBrushTopology(brush, &topology)) {
			continue;
		}
		const auto count = m_componentMode == ComponentMode::Vertices ? topology.vertices.size()
			: m_componentMode == ComponentMode::Edges ? topology.edges.size() : topology.faces.size();
		if (m_componentHandles.size()+count > kComponentHandleLimit) { m_componentHandlesLimited = true; break; }
		++objects;
		const LevelMapSelectionRef ref {LevelMapSelectionKind::QuakeBrush, brush.id};
		m_componentSelectionTopology << levelComponentSelectionTopology(ref,topology);
		m_componentInsertionData->append({brush.id,topology});
		if (m_componentMode == ComponentMode::Vertices) {
			for (int i = 0; i < topology.vertices.size(); ++i) {
				m_componentHandles.push_back({ref, i, topology.vertices.at(i), levelComponentGeometryKey({topology.vertices.at(i)})});
			}
		} else if (m_componentMode == ComponentMode::Edges) {
			for (int i = 0; i < topology.edges.size(); ++i) {
				const LevelMapVec3& a = topology.vertices.at(topology.edges.at(i)[0]);
				const LevelMapVec3& b = topology.vertices.at(topology.edges.at(i)[1]);
				m_componentHandles.push_back({ref, i, {(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2, true}, levelComponentGeometryKey({a,b})});
			}
		} else {
			for (int i = 0; i < topology.faces.size(); ++i) {
				LevelMapVec3 centre {0, 0, 0, true};
				const QVector<int>& face = topology.faces.at(i);
				QVector<LevelMapVec3> corners;
				for (const int vertex : face) {
					corners << topology.vertices[vertex];
					centre.x += topology.vertices.at(vertex).x / face.size();
					centre.y += topology.vertices.at(vertex).y / face.size();
					centre.z += topology.vertices.at(vertex).z / face.size();
				}
				if (!face.isEmpty()) {
					m_componentHandles.push_back({ref, i, centre, levelComponentGeometryKey(corners)});
				}
			}
		}
	}
	// A patch shows its control points in every mode.
	for (const LevelMapPatch& patch : std::as_const(m_document.patches)) {
		const auto object = levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch,patch.id});
		if (!m_sceneContext.allowedObjects.contains(object) || m_sceneContext.closedGroups.contains(object)) { continue; }
		if (!patches.contains(patch.id) && !entities.contains(patch.entityId)) {
			continue;
		}
		if (full() || m_componentHandles.size()+patch.controlPoints.size() > kComponentHandleLimit) { m_componentHandlesLimited = true; break; }
		++objects;
		m_componentSelectionTopology << levelComponentSelectionTopology(patch);
		for (int i = 0; i < patch.controlPoints.size(); ++i) {
			m_componentHandles.push_back({{LevelMapSelectionKind::QuakePatch, patch.id}, i, patch.controlPoints.at(i), levelComponentGeometryKey({patch.controlPoints.at(i)})});
		}
	}
	const auto repick = [this](const QVector<QPair<LevelMapSelectionRef, LevelMapVec3>>& picks) {
		for (int i = 0; i < m_componentHandles.size(); ++i) {
			for (const auto& pick : picks) {
				if (sameRef(pick.first, m_componentHandles.at(i).object) && samePosition(pick.second, m_componentHandles.at(i).position)) {
					m_pickedHandles.insert(i);
					break;
				}
			}
		}
	};
	repick(wanted);
	if (m_pickedHandles.isEmpty()) {
		repick(fallback);
	}
}

QPointF MapViewport::componentHandlePosition(int index) const
{
	if (index < 0 || index >= m_componentHandles.size()) {
		return {};
	}
	auto position = m_componentHandles.at(index).position;
	if (m_pickedHandles.contains(index) && m_componentGizmo->active() && m_componentGizmo->tool() != LevelComponentTool::Pivot) { position = m_componentGizmo->transformed(position); }
	const QPointF plane = projectPoint(position);
	return worldToView(plane.x(), plane.y());
}

QVector<LevelMapComponentSet> MapViewport::pickedComponents() const
{
	QVector<LevelMapComponentSet> sets;
	QList<int> picked(m_pickedHandles.cbegin(), m_pickedHandles.cend());
	std::sort(picked.begin(), picked.end());
	const LevelBrushComponent kind = m_componentMode == ComponentMode::Edges ? LevelBrushComponent::Edge
		: m_componentMode == ComponentMode::Faces ? LevelBrushComponent::Face : LevelBrushComponent::Vertex;
	for (const int index : std::as_const(picked)) {
		const ComponentHandle& handle = m_componentHandles.at(index);
		auto set = std::find_if(sets.begin(), sets.end(), [&handle](const LevelMapComponentSet& s) { return sameRef(s.object, handle.object); });
		if (set == sets.end()) {
			LevelMapComponentSet added;
			added.object = handle.object;
			added.kind = handle.object.kind == LevelMapSelectionKind::QuakePatch ? LevelBrushComponent::Vertex : kind;
			sets.push_back(added);
			set = sets.end() - 1;
		}
		set->components.push_back(handle.component);
	}
	return sets;
}

QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> MapViewport::pickedComponentPositions() const
{
	QVector<QPair<LevelMapSelectionRef, LevelMapVec3>> picks;
	for (const int index : std::as_const(m_pickedHandles)) {
		if (index >= 0 && index < m_componentHandles.size()) {
			picks.push_back({m_componentHandles.at(index).object, m_componentHandles.at(index).position});
		}
	}
	return picks;
}

void MapViewport::setPickedComponentPositions(const QVector<QPair<LevelMapSelectionRef, LevelMapVec3>>& picks)
{
	const auto before = m_pickedHandles;
	m_pickedHandles.clear();
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		for (const auto& pick : picks) {
			if (sameRef(pick.first, m_componentHandles.at(i).object) && samePosition(pick.second, m_componentHandles.at(i).position)) {
				m_pickedHandles.insert(i);
				break;
			}
		}
	}
	if (before != m_pickedHandles) { finishComponentMove(false); m_componentCycle.reset(); }
	setAccessibleDescription(accessibleSummary());
	update();
}

void MapViewport::pickAllComponents()
{
	selectComponentHandles(LevelComponentPick::Replace);
}

bool MapViewport::setPickedComponentHandles(const QSet<int>& handles)
{
	if (std::any_of(handles.cbegin(),handles.cend(),[&](int id) { return id < 0 || id >= m_componentHandles.size(); })) { return false; }
	if (handles != m_pickedHandles) { finishComponentMove(false); m_componentCycle.reset(); }
	m_pickedHandles = handles;
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent event(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&event);
	return true;
}

bool MapViewport::setPickedComponents(const QVector<LevelMapComponentSet>& sets)
{
	QHash<QString,QSet<int>> requested;
	const auto kind = m_componentMode == ComponentMode::Edges ? LevelBrushComponent::Edge
		: m_componentMode == ComponentMode::Faces ? LevelBrushComponent::Face : LevelBrushComponent::Vertex;
	for (const auto& set : sets) {
		if (set.kind != (set.object.kind == LevelMapSelectionKind::QuakePatch ? LevelBrushComponent::Vertex : kind)) { return false; }
		requested[levelMapSelectionRefId(set.object)].unite(QSet<int>(set.components.cbegin(),set.components.cend()));
	}
	QSet<int> picked;
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		const auto& handle = m_componentHandles[i]; const auto object = levelMapSelectionRefId(handle.object);
		if (requested.contains(object) && requested[object].remove(handle.component)) { picked.insert(i); }
	}
	if (std::any_of(requested.cbegin(),requested.cend(),[](const auto& remaining) { return !remaining.isEmpty(); })) { return false; }
	return setPickedComponentHandles(picked);
}

bool MapViewport::queryComponentSelection(const LevelComponentSelectionRequest& request, QVector<LevelMapComponentSet>* output, QString* error) const
{
	if (!m_hasDocument || m_componentMode == ComponentMode::Off) {
		if (error) { *error = tr("Open a map and enter component mode before changing its component selection."); } return false;
	}
	if (!m_pickedHandles.isEmpty()) {
		int handles = 0;
		for (const auto& t : m_componentSelectionTopology) {
			handles += t.object.kind == LevelMapSelectionKind::QuakePatch || request.target == LevelBrushComponent::Vertex ? t.vertexCount
				: int(request.target == LevelBrushComponent::Edge ? t.edges.size() : t.faces.size());
		}
		if (m_componentHandlesLimited || (request.operation == LevelComponentSelectionOperation::Convert && handles > kComponentHandleLimit)) {
			if (error) { *error = tr("Select fewer objects before changing component selection: the complete result must fit the handle limit."); } return false;
		}
	}
	return queryLevelComponentSelection(m_componentSelectionTopology,pickedComponents(),request,output,error);
}

void MapViewport::selectComponentHandles(LevelComponentPick operation)
{
	if (m_componentMode == ComponentMode::Off || !isEnabled()) { return; }
	m_componentCycle.reset();
	finishComponentMove(false); m_componentBanding = m_componentArmed = false;
	QSet<int> hits; for (int i = 0; i < m_componentHandles.size(); ++i) { hits.insert(i); }
	m_pickedHandles = selectLevelComponentHandles(m_pickedHandles,hits,operation);
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT hoverChanged(componentSummary()); Q_EMIT componentPicksChanged(pickedComponentCount());
}

bool MapViewport::pickComponentAt(const QPointF& point,LevelComponentPick operation,bool preservePicked)
{
	if (m_componentMode == ComponentMode::Off || !isEnabled() || isMovingComponents() || !std::isfinite(point.x()) || !std::isfinite(point.y())) { return false; }
	const int hit = componentHandleAt(point); if (hit < 0) { return false; }
	// A drag on an already selected depth must not reselect its projected stack.
	if (preservePicked && operation == LevelComponentPick::Replace && m_pickedHandles.contains(hit)) {
		m_componentCycle.aim(point); m_componentCycle.remainder = 0;
		setAccessibleDescription(accessibleSummary()); return true;
	}
	m_componentCycle.reset(); m_componentCycle.aim(point);
	const auto stack = componentHandlesStackedAt(hit);
	m_pickedHandles = selectLevelComponentHandles(m_pickedHandles,QSet<int>(stack.cbegin(),stack.cend()),operation,true);
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT hoverChanged(componentSummary()); Q_EMIT componentPicksChanged(pickedComponentCount()); return true;
}

bool MapViewport::pickComponentBand(const QRectF& rectangle,LevelComponentPick operation)
{
	if (m_componentMode == ComponentMode::Off || !isEnabled() || isMovingComponents() || !std::isfinite(rectangle.x()) || !std::isfinite(rectangle.y())
		|| !std::isfinite(rectangle.width()) || !std::isfinite(rectangle.height())) { return false; }
	m_componentCycle.reset();
	const auto band = rectangle.normalized(); QSet<int> hits;
	for (int i = 0; i < m_componentHandles.size(); ++i) { if (band.contains(componentHandlePosition(i))) { hits.insert(i); } }
	m_pickedHandles = selectLevelComponentHandles(m_pickedHandles,hits,operation);
	setAccessibleDescription(accessibleSummary()); update();
	Q_EMIT hoverChanged(componentSummary()); Q_EMIT componentPicksChanged(pickedComponentCount()); return true;
}

bool MapViewport::cycleComponentAt(const QPointF& point,int steps)
{
	if (m_componentMode == ComponentMode::Off || !isEnabled() || m_componentHandlesLimited || isMovingComponents()
		|| m_componentBanding || m_componentArmed || m_componentNativeArmed || m_panning || !std::isfinite(point.x()) || !std::isfinite(point.y()) || !steps) { return false; }
	m_componentCycle.aim(point);
	QVector<LevelComponentCycleHit> hits;
	const double radius = kHandlePickPixels*(m_highContrast ? 1.3 : 1.0);
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		const auto screen = componentHandlePosition(i); const auto delta = screen-point;
		const double distance = QPointF::dotProduct(delta,delta);
		if (!QRectF(rect()).contains(screen) || distance > radius*radius) { continue; }
		const auto& handle = m_componentHandles[i]; const auto p = handle.position;
		const double depth = m_projection == MapViewportProjection::TopXY ? p.z : m_projection == MapViewportProjection::FrontXZ ? -p.y : p.x;
		hits << LevelComponentCycleHit{i,depth,distance,handle.geometryKey};
	}
	const auto result = cycleLevelComponentHits(hits,m_pickedHandles,steps);
	m_componentCycle.ordinal = result.ordinal; m_componentCycle.count = result.count; m_componentCycle.handle = result.handle;
	if (!result.count) { setAccessibleDescription(accessibleSummary()); Q_EMIT hoverChanged(componentSummary()); return false; }
	m_pickedHandles = result.picked;
	setAccessibleDescription(accessibleSummary()); update();
	QAccessibleEvent event(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&event);
	Q_EMIT componentPicksChanged(pickedComponentCount()); Q_EMIT hoverChanged(componentSummary()); return true;
}

bool MapViewport::cycleComponentSelection(int steps)
{
	if (m_componentCycle.point) { return cycleComponentAt(*m_componentCycle.point,steps); }
	// Keyboard-only entry starts from a deterministic picked handle, or the
	// first on-screen handle when nothing is picked. No pointer warp is needed.
	int first = -1;
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		if (!QRectF(rect()).contains(componentHandlePosition(i))) { continue; }
		if (first < 0) { first = i; }
		if (m_pickedHandles.contains(i)) { first = i; break; }
	}
	return first >= 0 && cycleComponentAt(componentHandlePosition(first),steps);
}

bool MapViewport::componentCycleWheel(const QPointF& point,int delta,Qt::KeyboardModifiers modifiers)
{
	if (m_componentMode == ComponentMode::Off || !isEnabled() || m_controls.componentCycleWheelModifiers == Qt::NoModifier
		|| modifiers != m_controls.componentCycleWheelModifiers || !std::isfinite(point.x()) || !std::isfinite(point.y())) {
		m_componentCycle.remainder = 0; return false;
	}
	if (isMovingComponents() || m_componentBanding || m_componentArmed || m_componentNativeArmed || m_panning) { m_componentCycle.remainder = 0; return true; }
	m_componentCycle.aim(point); const int steps = m_componentCycle.wheelSteps(delta);
	if (steps) { cycleComponentAt(point,steps); } return true;
}

int MapViewport::componentHandleAt(const QPointF& viewPoint) const
{
	int best = -1;
	double bestDistance = kHandlePickPixels * (m_highContrast ? 1.3 : 1.0);
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		const QPointF at = componentHandlePosition(i);
		const double distance = std::hypot(at.x() - viewPoint.x(), at.y() - viewPoint.y());
		// Ties go to a handle already picked, so a drag can start on any
		// picked one of a stack.
		if (distance < bestDistance || (best >= 0 && distance == bestDistance && m_pickedHandles.contains(i) && !m_pickedHandles.contains(best))) {
			best = i;
			bestDistance = distance;
		}
	}
	return best;
}

QVector<int> MapViewport::componentHandlesStackedAt(int handle) const
{
	// Every handle this view draws at the same spot: a box's top and bottom
	// corners from above, or a corner two brushes share.
	QVector<int> stacked;
	const QPointF at = componentHandlePosition(handle);
	for (int i = 0; i < m_componentHandles.size(); ++i) {
		const QPointF other = componentHandlePosition(i);
		if (std::hypot(other.x() - at.x(), other.y() - at.y()) <= kHandleStackPixels) {
			stacked.push_back(i);
		}
	}
	return stacked;
}

QPointF MapViewport::componentDragDelta() const
{
	if (!m_componentDragging) {
		return {};
	}
	return m_componentDragPlane - m_componentGrabPlane;
}

bool MapViewport::componentMousePress(QMouseEvent* event)
{
	if (m_componentNativeArmed) { finishComponentMove(false); event->accept(); return true; }
	if (m_vertexInsertion->active()) { m_vertexInsertion->finish(false); event->accept(); return true; }
	if (event->button() == Qt::LeftButton && beginVertexInsertion(event->position(),event->modifiers())) { event->accept(); return true; }
	if (m_componentGizmo->active()) { m_componentGizmo->finish(false); event->accept(); return true; }
	if (event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && beginComponentTransform(event->position())) { event->accept(); return true; }
	if (m_componentReleasePending) { event->accept(); return true; }
	if (event->button() != Qt::LeftButton) {
		return false;
	}
	if (armComponentMove(event->position(),event->modifiers())) { event->accept(); return true; }
	const Qt::KeyboardModifiers keys = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	const auto operation = levelComponentPickOperation(keys,m_controls.toggleModifiers,m_controls.addModifiers,false,m_controls.componentBandToggles);
	if (!operation) { return false; }
	m_componentPressView = event->position();
	m_componentModifiers = keys;
	const int hit = componentHandleAt(event->position());
	if (hit < 0) {
		// Over empty space a drag bands handles; a click (decided on release)
		// may pick an object instead.
		m_componentBanding = true;
		m_componentBandView = event->position();
		update();
		event->accept();
		return true;
	}
	if (!pickComponentAt(event->position(),*operation,true)) { return false; }
	if (*operation == LevelComponentPick::Replace && m_pickedHandles.contains(hit) && m_componentGizmo->tool() == LevelComponentTool::Move) {
		m_componentArmed = true;
		m_componentGrabbed = hit;
		m_componentGrabPlane = projectPoint(m_componentHandles.at(hit).position);
		m_componentDragPlane = m_componentGrabPlane;
	}
	Q_EMIT hoverChanged(componentSummary());
	setAccessibleDescription(accessibleSummary());
	update();
	event->accept();
	return true;
}

bool MapViewport::componentMouseMove(QMouseEvent* event)
{
	if (m_vertexInsertion->active()) {
		if (!m_vertexInsertion->releasePending() && event->buttons() != Qt::LeftButton) { m_vertexInsertion->finish(false); }
		else if (!m_vertexInsertion->releasePending()) { m_vertexInsertion->updateDrag(event->position(),event->modifiers()); }
		event->accept(); return true;
	}
	if (event->buttons() == Qt::NoButton && hoverVertexInsertion(event->position(),event->modifiers())) { event->accept(); return true; }
	if (m_componentGizmo->active()) {
		if (!m_componentGizmo->releasePending() && event->buttons() != Qt::LeftButton) { m_componentGizmo->finish(false); }
		else if (!m_componentGizmo->releasePending()) { m_componentGizmo->updateDrag(event->position()); }
		event->accept(); return true;
	}
	if (m_componentReleasePending) { event->accept(); return true; }
	const QPointF position = event->position();
	if (m_componentNativeArmed) {
		if (event->buttons() != Qt::LeftButton) { finishComponentMove(false); }
		else { updateComponentMove(position,event->modifiers()); }
		event->accept(); return true;
	}
	if (m_componentBanding) {
		m_componentBandView = position;
		update();
		event->accept();
		return true;
	}
	if (m_componentArmed && !m_componentDragging) {
		const QPointF travel = position - m_componentPressView;
		if (std::hypot(travel.x(), travel.y()) < kComponentDragPixels) {
			event->accept();
			return true;
		}
		beginComponentMove(m_componentPressView);
	}
	if (m_componentDragging) {
		if (!(event->buttons() & Qt::LeftButton)) { finishComponentMove(false); }
		else { updateComponentMove(position,event->modifiers()); }
		event->accept();
		return true;
	}
	// Hovering: say which handle the pointer rests on.
	const int hit = componentHandleAt(position);
	if (hit >= 0) {
		const ComponentHandle& handle = m_componentHandles.at(hit);
		const QString kind = handle.object.kind == LevelMapSelectionKind::QuakePatch ? tr("Control point %1 of patch %2")
			: m_componentMode == ComponentMode::Edges ? tr("Edge %1 of brush %2")
			: m_componentMode == ComponentMode::Faces ? tr("Face %1 of brush %2") : tr("Vertex %1 of brush %2");
		Q_EMIT hoverChanged(kind.arg(handle.component).arg(handle.object.objectId) + QStringLiteral(" · ")
			+ tr("X %1, Y %2, Z %3").arg(handle.position.x, 0, 'g', 8).arg(handle.position.y, 0, 'g', 8).arg(handle.position.z, 0, 'g', 8));
	} else {
		Q_EMIT hoverChanged(componentSummary());
	}
	event->accept();
	return true;
}

bool MapViewport::componentMouseRelease(QMouseEvent* event)
{
	if (m_vertexInsertion->active() && event->button() == Qt::LeftButton) { m_vertexInsertion->updateDrag(event->position(),event->modifiers()); m_vertexInsertion->finish(true); event->accept(); return true; }
	if (m_componentGizmo->active() && event->button() == Qt::LeftButton) { m_componentGizmo->updateDrag(event->position()); m_componentGizmo->finish(true); event->accept(); return true; }
	if (event->button() != Qt::LeftButton) {
		return false;
	}
	if (m_componentBanding) {
		m_componentBanding = false;
		const QPointF travel = m_componentBandView - m_componentPressView;
		if (std::hypot(travel.x(), travel.y()) < kComponentDragPixels) {
			// A click on nothing: pick the brush or patch there, else drop the
			// picks.
			const MapViewportHit hit = hitTest(event->position());
			if ((hit.kind == LevelMapSelectionKind::QuakeBrush || hit.kind == LevelMapSelectionKind::QuakePatch)
				&& !selectionContains(hit.kind, hit.objectId)) {
				if (m_componentModifiers == Qt::NoModifier) {
					replaceSelection(hit.kind, hit.objectId);
				} else {
					addToSelection(hit.kind, hit.objectId);
				}
				announceSelection();
				rebuildComponentHandles();
			} else if (m_componentModifiers == Qt::NoModifier && !m_pickedHandles.isEmpty()) {
				m_componentCycle.reset();
				m_pickedHandles.clear();
				Q_EMIT componentPicksChanged(0);
			}
		} else {
			const QRectF band = QRectF(m_componentPressView, m_componentBandView).normalized();
			const auto operation = levelComponentPickOperation(m_componentModifiers,m_controls.toggleModifiers,m_controls.addModifiers,true,m_controls.componentBandToggles);
			if (operation) { pickComponentBand(band,*operation); }
		}
		Q_EMIT hoverChanged(componentSummary());
		setAccessibleDescription(accessibleSummary());
		update();
		event->accept();
		return true;
	}
	if (m_componentDragging || m_componentNativeArmed) {
		updateComponentMove(event->position(),event->modifiers());
		finishComponentMove(true);
		update();
		event->accept();
		return true;
	}
	if (m_componentArmed) {
		m_componentArmed = false;
		m_componentGrabbed = -1;
		event->accept();
		return true;
	}
	return false;
}

bool MapViewport::componentKeyPress(QKeyEvent* event)
{
	if (updateComponentModifiers(event->key(),event->modifiers(),true)) { event->accept(); return true; }
	switch (event->key()) {
	case Qt::Key_Tab:
	case Qt::Key_Backtab:
		if ((event->modifiers() & ~Qt::ShiftModifier) != Qt::NoModifier) { return false; }
		cycleComponentSelection(event->key() == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
		event->accept(); return true;
	case Qt::Key_Left:
	case Qt::Key_Right:
	case Qt::Key_Up:
	case Qt::Key_Down: {
		const auto modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
		if (m_pickedHandles.isEmpty() && modifiers == Qt::NoModifier && m_componentMode == ComponentMode::Faces && !m_texturedFaces.isEmpty()) {
			// NRC face mode nudges every picked face, including Select Textured
			// picks whose brushes are not selected.
			if (!isMovingComponents() && !m_componentBanding && !m_componentNativeArmed) {
				const double sign = (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down) ? -1.0 : 1.0;
				const double step = sign * (m_gridSize > 0 ? static_cast<double>(m_gridSize) : 1.0) * ((event->modifiers() & Qt::ShiftModifier) != 0 ? 8 : 1);
				const bool horizontal = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;
				LevelMapVec3 delta{0, 0, 0, true};
				planeDeltaToWorld(horizontal ? QPointF(step, 0.0) : QPointF(0.0, step), &delta.x, &delta.y, &delta.z);
				Q_EMIT texturedFacesMoveRequested(delta);
			}
			event->accept();
			return true;
		}
		if (m_pickedHandles.isEmpty() || modifiers != Qt::NoModifier) {
			return false;
		}
		if (isMovingComponents() || m_componentBanding || m_componentNativeArmed) {
			event->accept();
			return true;
		}
		const double sign = (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down) ? -1.0 : 1.0;
		const bool horizontal = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;
		const double grid = m_gridSize > 0 ? static_cast<double>(m_gridSize) : 1.0;
		const double step = sign * grid * ((event->modifiers() & Qt::ShiftModifier) != 0 ? 8 : 1);
		requestComponentMove(horizontal ? QPointF(step, 0.0) : QPointF(0.0, step));
		event->accept();
		return true;
	}
	case Qt::Key_Escape:
		if (isMovingComponents() || m_componentBanding || m_componentArmed || m_componentNativeArmed) {
			finishComponentMove(false);
			m_componentDragging = m_componentBanding = m_componentArmed = false;
			m_componentGrabbed = -1;
			Q_EMIT hoverChanged(componentSummary());
			update();
		} else if (!m_texturedFaces.isEmpty()) {
			Q_EMIT texturedSelectionClearRequested();
		} else if (!m_pickedHandles.isEmpty()) {
			m_componentCycle.reset();
			m_pickedHandles.clear();
			Q_EMIT componentPicksChanged(0);
			Q_EMIT hoverChanged(componentSummary());
			setAccessibleDescription(accessibleSummary());
			update();
		} else {
			setComponentMode(ComponentMode::Off);
		}
		event->accept();
		return true;
	default:
		return false;
	}
}

void MapViewport::setComponentSplit(bool enabled)
{
	enabled = enabled && (m_componentMode == ComponentMode::Edges || m_componentMode == ComponentMode::Faces);
	if (enabled == m_componentSplit) { return; }
	finishComponentMove(false); m_componentSplit = enabled;
	if (enabled) { m_componentExtrusion = 0; }
	setAccessibleDescription(accessibleSummary()); update();
}

void MapViewport::setComponentExtrusion(int mode, const LevelMapVec3& normal, const QVector<LevelMapVec3>& vertices)
{
	mode = m_componentMode == ComponentMode::Faces ? std::clamp(mode,0,2) : 0;
	const auto equal = [](const LevelMapVec3& a,const LevelMapVec3& b) { return a.valid == b.valid && a.x == b.x && a.y == b.y && a.z == b.z; };
	if (mode == m_componentExtrusion && equal(normal,m_componentExtrusionNormal) && vertices.size() == m_componentExtrusionVertices.size()
		&& std::equal(vertices.cbegin(),vertices.cend(),m_componentExtrusionVertices.cbegin(),equal)) { return; }
	finishComponentMove(false); m_componentExtrusion = mode; m_componentExtrusionNormal = normal; m_componentExtrusionVertices = vertices;
	if (mode) { m_componentSplit = false; }
	setAccessibleDescription(accessibleSummary()); update();
}

bool MapViewport::beginComponentMove(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (m_componentGizmo->active() || m_vertexInsertion->active() || m_componentGizmo->tool() != LevelComponentTool::Move) { return false; }
	if (!isEnabled() || m_componentMode == ComponentMode::Off || m_componentDragging || m_componentBanding
		|| !std::isfinite(point.x()) || !std::isfinite(point.y())) { return false; }
	const int hit = componentHandleAt(point);
	if (hit < 0 || !m_pickedHandles.contains(hit)) { return false; }
	if (m_componentExtrusion) {
		const auto n = projectPoint(m_componentExtrusionNormal);
		if (!m_componentExtrusionNormal.valid || QPointF::dotProduct(n,n) < 1e-8) {
			Q_EMIT hoverChanged(tr("Use another view or numeric extrusion when the face normal points along this view.")); return false;
		}
	}
	m_componentWorldDrag = m_controls.componentAxisDrag && !m_componentExtrusion;
	if (m_componentWorldDrag) {
		const auto frame = componentGizmoFrame();
		if (!m_componentAxisDrag.begin(m_componentHandles[hit].position,frame.ray(point),
			{false,m_componentMode == ComponentMode::Vertices,-1,m_snapToGrid ? double(m_gridSize) : 0,frame.viewDirection},modifiers)) { return false; }
	}
	m_componentAxisDelta = {0,0,0,true}; m_componentDragModifiers = modifiers; m_componentLastPoint = point;
	m_componentNativeArmed = false;
	m_componentExtrusionDistance = 0;
	m_componentGrabbed = hit;
	m_componentGrabPlane = projectPoint(m_componentHandles[hit].position);
	m_componentDragPlane = m_componentGrabPlane; m_componentPressView = point;
	m_componentDragging = true; m_componentArmed = m_componentPending = m_componentReleasePending = false;
	m_componentError.clear(); m_componentPreviewEdges.clear();
	Q_EMIT componentMoveActiveChanged(true);
	setAccessibleDescription(accessibleSummary()); update();
	return m_componentDragging;
}

LevelMapVec3 MapViewport::componentMoveDelta() const
{
	if (m_componentDragging && m_componentWorldDrag) { return m_componentAxisDelta; }
	if (m_componentExtrusion && m_componentDragging) {
		const auto n = m_componentExtrusionNormal; const double d = m_componentExtrusionDistance;
		return {n.x*d,n.y*d,n.z*d,true};
	}
	LevelMapVec3 delta{0,0,0,true};
	planeDeltaToWorld(componentDragDelta(), &delta.x, &delta.y, &delta.z);
	return delta;
}

bool MapViewport::updateComponentMove(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (m_componentNativeArmed) {
		const auto travel = point-m_componentNativePress;
		if (std::hypot(travel.x(),travel.y()) < kComponentDragPixels) { return true; }
		const auto press = m_componentNativePress; const auto initial = m_componentNativeModifiers; m_componentNativeArmed = false;
		const int hit = componentHandleAt(press); if (hit < 0) { return false; }
		QSet<int> clump{hit}; const auto key = m_componentHandles[hit].geometryKey;
		if (!key.isEmpty()) { for (int i = 0; i < m_componentHandles.size(); ++i) { if (m_componentHandles[i].geometryKey == key) { clump.insert(i); } } }
		if (!std::all_of(clump.cbegin(),clump.cend(),[&](int id) { return m_pickedHandles.contains(id); })) {
			setPickedComponentHandles(clump); Q_EMIT componentPicksChanged(pickedComponentCount());
		}
		if (!beginComponentMove(press,initial)) { return false; }
	}
	if (!m_componentDragging || m_componentReleasePending) { return false; }
	if (!std::isfinite(point.x()) || !std::isfinite(point.y())) { finishComponentMove(false); return false; }
	m_componentLastPoint = point; m_componentDragModifiers = modifiers;
	if (m_componentWorldDrag) {
		LevelMapVec3 delta;
		if (!m_componentAxisDrag.update(componentGizmoFrame().ray(point),modifiers,&delta)) {
			m_componentPending = false; m_componentError = tr("Cannot reach the movement plane here."); m_componentPreviewEdges.clear();
			setAccessibleDescription(accessibleSummary()); Q_EMIT hoverChanged(componentSummary()); update(); return false;
		}
		const bool changed = delta.x != m_componentAxisDelta.x || delta.y != m_componentAxisDelta.y || delta.z != m_componentAxisDelta.z || !m_componentError.isEmpty();
		m_componentAxisDelta = delta; m_componentDragPlane = m_componentGrabPlane+projectPoint(delta);
		if (changed) {
			m_componentPending = true; m_componentError.clear(); m_componentPreviewEdges.clear(); Q_EMIT componentMovePreviewChanged(delta);
		}
		Q_EMIT hoverChanged(componentSummary()); setAccessibleDescription(accessibleSummary()); update(); return true;
	}
	QPointF target = m_componentGrabPlane + viewToWorld(point) - viewToWorld(m_componentPressView);
	if (m_componentExtrusion) {
		const auto n = projectPoint(m_componentExtrusionNormal);
		const double distance = QPointF::dotProduct(target-m_componentGrabPlane,n)/QPointF::dotProduct(n,n);
		m_componentExtrusionDistance = snapLevelBrushExtrusionDistance(distance,m_componentExtrusionNormal,m_componentExtrusionVertices,m_snapToGrid ? m_gridSize : 0);
		if (!std::isfinite(m_componentExtrusionDistance)) { finishComponentMove(false); return false; }
		target = m_componentGrabPlane + n*m_componentExtrusionDistance;
	} else if (m_snapToGrid && m_gridSize > 0) {
		target = {snapLevelMapCoordinate(target.x(),m_gridSize),snapLevelMapCoordinate(target.y(),m_gridSize)};
	}
	if (target == m_componentDragPlane && m_componentError.isEmpty()) { return true; }
	m_componentDragPlane = target; m_componentPending = true; m_componentError.clear(); m_componentPreviewEdges.clear();
	Q_EMIT componentMovePreviewChanged(componentMoveDelta());
	Q_EMIT hoverChanged(componentSummary()); setAccessibleDescription(accessibleSummary()); update();
	return true;
}

void MapViewport::setComponentPreview(const LevelMapVec3& delta, const QVector<QPair<LevelMapVec3,LevelMapVec3>>& edges, const QString& error)
{
	const auto current = componentMoveDelta();
	if (!m_componentDragging || !m_componentPending || delta.x != current.x || delta.y != current.y || delta.z != current.z) { return; }
	m_componentPending = false; m_componentError = error;
	if (error.isEmpty() && m_componentWorldDrag) { m_componentAxisDrag.accept(delta); }
	m_componentPreviewEdges = error.isEmpty() ? edges : QVector<QPair<LevelMapVec3,LevelMapVec3>>{};
	setAccessibleDescription(accessibleSummary()); update(); Q_EMIT hoverChanged(componentSummary());
	QAccessibleEvent announcement(this,QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&announcement);
	if (m_componentReleasePending) { finishComponentMove(true); }
}

void MapViewport::finishComponentMove(bool commit)
{
	if (m_componentNativeArmed) {
		const auto point = m_componentNativePress; const auto keys = m_componentNativeModifiers; m_componentNativeArmed = false;
		if (commit && m_controls.componentQuickSnapModifiers != Qt::NoModifier && keys == m_controls.componentQuickSnapModifiers && requestVertexSnapAt(point)) { return; }
		const auto operation = levelComponentPickOperation(keys,m_controls.toggleModifiers,m_controls.addModifiers,false,m_controls.componentBandToggles);
		if (commit && operation) { pickComponentAt(point,*operation,true); }
	}
	if (!commit && m_vertexInsertion) { m_vertexInsertion->finish(false); m_vertexInsertion->clearHover(); }
	if (!commit && m_componentGizmo) { m_componentGizmo->finish(false); }
	if (!m_componentDragging) { return; }
	if (commit && m_componentPending) {
		m_componentReleasePending = true;
		setAccessibleDescription(accessibleSummary()); Q_EMIT hoverChanged(componentSummary()); update(); return;
	}
	const auto delta = componentMoveDelta(); const auto sets = pickedComponents();
	const auto error = m_componentError;
	const bool apply = commit && error.isEmpty() && (delta.x != 0 || delta.y != 0 || delta.z != 0);
	m_componentDragging = m_componentArmed = m_componentPending = m_componentReleasePending = false;
	m_componentGrabbed = -1; m_componentPreviewEdges.clear();
	if (apply) { Q_EMIT componentMoveRequested(sets,delta); }
	Q_EMIT componentMoveActiveChanged(false);
	setAccessibleDescription(accessibleSummary()); update();
	if (commit && !error.isEmpty()) { Q_EMIT hoverChanged(tr("Components unchanged: %1").arg(error)); }
}

bool MapViewport::nudgeComponents(const QPointF& planeDelta)
{
	if (m_componentMode == ComponentMode::Off || m_pickedHandles.isEmpty() || planeDelta.isNull()
		|| m_componentGizmo->tool() != LevelComponentTool::Move) {
		return false;
	}
	if (isMovingComponents() || m_componentBanding || m_componentNativeArmed) {
		return true;
	}
	requestComponentMove(planeDelta);
	return true;
}

bool MapViewport::keepsKey(int key, Qt::KeyboardModifiers modifiers) const
{
	return m_dragging || m_banding || m_resizing || isMovingComponents() || m_componentBanding || m_componentNativeArmed
		|| navigationHoldKeyMatches(m_controls.panHoldKey, key, modifiers) || !planArrowPanDirection(m_controls, key, modifiers).isNull();
}

void MapViewport::requestComponentMove(const QPointF& planeDelta)
{
	if (m_pickedHandles.isEmpty() || planeDelta.isNull() || !beginComponentMove(componentHandlePosition(*m_pickedHandles.cbegin()))) { return; }
	m_componentWorldDrag = false;
	m_componentDragPlane = m_componentGrabPlane + planeDelta;
	if (m_componentExtrusion) {
		const auto n = projectPoint(m_componentExtrusionNormal);
		const double distance = QPointF::dotProduct(planeDelta,n)/QPointF::dotProduct(n,n);
		m_componentExtrusionDistance = snapLevelBrushExtrusionDistance(distance,m_componentExtrusionNormal,m_componentExtrusionVertices,m_snapToGrid ? m_gridSize : 0);
		m_componentDragPlane = m_componentGrabPlane + n*m_componentExtrusionDistance;
	}
	m_componentPending = true;
	Q_EMIT componentMovePreviewChanged(componentMoveDelta());
	finishComponentMove(true);
}

QString MapViewport::componentSummary() const
{
	if (m_vertexInsertion && (m_vertexInsertion->tool() || m_vertexInsertion->active() || m_vertexInsertion->candidate().position.valid)) { return m_vertexInsertion->summary(); }
	if (m_componentMode == ComponentMode::Off) {
		return {};
	}
	if (m_componentHandles.isEmpty()) {
		return tr("Vertex editing: select brushes or patches to show their handles.");
	}
	QString overlap;
	if (!isMovingComponents() && m_componentCycle.count && m_componentCycle.handle >= 0 && m_componentCycle.handle < m_componentHandles.size()) {
		const auto p = m_componentHandles[m_componentCycle.handle].position;
		overlap = tr("Overlap %1 of %2 · XYZ %3, %4, %5 · ").arg(m_componentCycle.ordinal).arg(m_componentCycle.count).arg(p.x,0,'g',8).arg(p.y,0,'g',8).arg(p.z,0,'g',8);
	}
	if (m_componentGizmo->tool() != LevelComponentTool::Move) { return overlap+m_componentGizmo->summary(); }
	if (m_componentDragging) {
		if (!m_componentError.isEmpty()) { return tr("Components unchanged: %1").arg(m_componentError); }
		if (m_componentWorldDrag) {
			const auto d = componentMoveDelta();
			return tr("Move components: X %1, Y %2, Z %3").arg(d.x,0,'g',8).arg(d.y,0,'g',8).arg(d.z,0,'g',8)
				+QStringLiteral(" · ")+m_componentAxisDrag.description()+QStringLiteral(" · ")
				+(m_componentPending ? tr("Checking geometry… Escape cancels") : tr("Valid preview · Release to apply"));
		}
		if (m_componentExtrusion) {
			return tr("Extrude: %1 units").arg(m_componentExtrusionDistance,0,'g',8) + QStringLiteral(" · ")
				+ (m_componentPending ? tr("Checking geometry… Escape cancels") : tr("Valid preview · Release to apply"));
		}
		const QPointF delta = componentDragDelta();
		return (m_componentSplit ? tr("Splitting %n handle(s) by %1 %2, %3 %4", nullptr, pickedComponentCount())
			: tr("Moving %n handle(s) by %1 %2, %3 %4", nullptr, pickedComponentCount()))
			.arg(m_projection == MapViewportProjection::SideZY ? QStringLiteral("Y") : QStringLiteral("X"))
			.arg(delta.x(), 0, 'g', 8)
			.arg(m_projection == MapViewportProjection::TopXY ? QStringLiteral("Y") : QStringLiteral("Z"))
			.arg(delta.y(), 0, 'g', 8) + QStringLiteral(" · ")
			+ (m_componentPending ? tr("Checking geometry… Escape cancels") : tr("Valid preview · Release to apply"));
	}
	return overlap+(m_componentHandlesLimited ? tr("Handle limit reached; select fewer objects. ") : QString{})
		+ (m_componentExtrusion ? (m_componentExtrusion == 2 ? tr("Split extrusion: drag a face handle along its normal. ") : tr("Extrude: drag a face handle along its normal. ")) : QString{})
		+ (m_componentSplit ? tr("Split: drag edge or face handles to create corners. ") : QString{})
		+ tr("%1: %2 of %n handle(s) picked; drag a picked handle or use the arrow keys to move them.", nullptr, componentHandleCount())
		.arg(componentModeName())
		.arg(pickedComponentCount());
}

void MapViewport::paintComponents(QPainter& painter, const Palette& palette) const
{
	if (m_componentMode == ComponentMode::Off || m_componentHandles.isEmpty()) {
		return;
	}
	painter.save();
	const double lineWidth = m_highContrast ? 2.0 : 1.2;
	m_componentGizmo->paint(painter,componentGizmoFrame(),m_highContrast);
	m_vertexInsertion->paint(painter,componentGizmoFrame(),m_highContrast);
	painter.setPen(QPen(palette.selection,lineWidth+0.5,Qt::DashLine));
	for (const auto& edge : m_componentPreviewEdges) {
		const auto a = projectPoint(edge.first), b = projectPoint(edge.second);
		painter.drawLine(worldToView(a.x(),a.y()),worldToView(b.x(),b.y()));
	}
	// A patch's control lattice, so its rows and columns read as a grid.
	QSet<int> patches;
	for (const ComponentHandle& handle : m_componentHandles) {
		if (handle.object.kind == LevelMapSelectionKind::QuakePatch) {
			patches.insert(handle.object.objectId);
		}
	}
	painter.setPen(QPen(palette.patch, m_highContrast ? 1.5 : 1.0, Qt::DotLine));
	for (const LevelMapPatch& patch : m_document.patches) {
		if (!patches.contains(patch.id) || patch.controlPoints.size() != patch.width * patch.height) {
			continue;
		}
		const auto view = [this](const LevelMapVec3& point) {
			const QPointF plane = projectPoint(point);
			return worldToView(plane.x(), plane.y());
		};
		for (int row = 0; row < patch.height; ++row) {
			for (int column = 0; column < patch.width; ++column) {
				const QPointF at = view(patch.controlPoints.at(row * patch.width + column));
				if (column + 1 < patch.width) {
					painter.drawLine(at, view(patch.controlPoints.at(row * patch.width + column + 1)));
				}
				if (row + 1 < patch.height) {
					painter.drawLine(at, view(patch.controlPoints.at((row + 1) * patch.width + column)));
				}
			}
		}
	}
	const QPointF moved = componentDragDelta() * m_zoom;
	// Selected handles are painted last so an overlapping unselected depth
	// cannot hide the selection fill.
	for (bool selectedPass : {false,true}) { for (int i = 0; i < m_componentHandles.size(); ++i) {
		const ComponentHandle& handle = m_componentHandles.at(i);
		const bool picked = m_pickedHandles.contains(i);
		if (picked != selectedPass) { continue; }
		QPointF at = componentHandlePosition(i);
		if (picked && m_componentDragging) {
			// Where the drag takes it, with a line back to where it was.
			const QPointF to = at + QPointF(moved.x(), -moved.y());
			painter.setPen(QPen(palette.selection, 1.0, Qt::DashLine));
			painter.drawLine(at, to);
			at = to;
		}
		// Picked handles are filled and larger; brush vertices are squares,
		// edges diamonds, faces and patch points circles, so the shapes
		// carry the meaning as well as the colours.
		const double size = picked ? 4.5 : 3.25;
		painter.setPen(QPen(picked ? palette.selection : palette.vertex, lineWidth));
		painter.setBrush(picked ? QBrush(palette.selection) : QBrush(palette.background));
		if (handle.object.kind == LevelMapSelectionKind::QuakePatch || m_componentMode == ComponentMode::Faces) {
			painter.drawEllipse(at, size, size);
		} else if (m_componentMode == ComponentMode::Edges) {
			QPainterPath diamond;
			diamond.moveTo(at.x(), at.y() - size - 1);
			diamond.lineTo(at.x() + size + 1, at.y());
			diamond.lineTo(at.x(), at.y() + size + 1);
			diamond.lineTo(at.x() - size - 1, at.y());
			diamond.closeSubpath();
			painter.drawPath(diamond);
		} else {
			painter.drawRect(QRectF(at.x() - size, at.y() - size, size * 2, size * 2));
		}
	}
	}
	if (m_componentDragging && m_componentGrabbed >= 0 && m_componentGrabbed < m_componentHandles.size()) {
		const auto start = m_componentHandles[m_componentGrabbed].position,delta = componentMoveDelta();
		const auto origin = m_componentWorldDrag ? m_componentAxisDrag.traceOrigin() : start;
		paintLevelComponentTrace(painter,componentGizmoFrame(),origin,{start.x+delta.x,start.y+delta.y,start.z+delta.z,true},
			m_componentWorldDrag ? m_componentAxisDrag.axis() : -1,m_componentPending,m_highContrast,this->palette());
	}
	if (m_componentBanding) {
		painter.setPen(QPen(palette.selection, 1.0, Qt::DashLine));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(QRectF(m_componentPressView, m_componentBandView).normalized());
	}
	painter.restore();
}

void MapViewport::setRegionDrawMode(bool enabled)
{
	enabled = enabled && m_hasDocument && m_document.format != LevelMapFormat::DoomWad;
	if (enabled == m_regionDrawMode) {
		return;
	}
	cancelInteraction();
	m_regionDrawMode = enabled;
	if (enabled) {
		setCursor(Qt::CrossCursor);
	} else if (m_componentMode == ComponentMode::Off) {
		unsetCursor();
	}
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT regionDrawModeChanged(enabled);
}

} // namespace vibestudio
