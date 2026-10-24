#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QTimer>
#include <cmath>

namespace vibestudio {
bool MapViewport::canSnapPickedVertex() const
{
	if (!isEnabled() || m_componentMode != ComponentMode::Vertices || m_componentHandlesLimited || m_pickedHandles.isEmpty()) { return false; }
	QString key;
	for (int id : m_pickedHandles) {
		if (id < 0 || id >= m_componentHandles.size()) { return false; }
		const auto& handle = m_componentHandles[id];
		if (handle.object.kind != LevelMapSelectionKind::QuakeBrush || handle.geometryKey.isEmpty()) { return false; }
		if (key.isEmpty()) { key = handle.geometryKey; } else if (key != handle.geometryKey) { return false; }
	}
	return true;
}
bool MapViewport::requestVertexSnapAt(const QPointF& point)
{
	if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !canSnapPickedVertex() || isMovingComponents() || m_componentBanding) { return false; }
	const int target = componentHandleAt(point);
	if (target < 0 || m_componentHandles[target].object.kind != LevelMapSelectionKind::QuakeBrush) { return false; }
	const auto handle = m_componentHandles[target];
	Q_EMIT vertexSnapRequested(handle.object,handle.component); return true;
}
bool ModelViewport::requestVertexSnapAt(const QPointF& point)
{
	if (m_componentMode != 1 || m_componentHandlesLimited || m_componentPicks.isEmpty() || isMovingComponents() || m_componentBanding) { return false; }
	QString key;
	for (int id : m_componentPicks) {
		const auto current = m_componentGeometryKeys.value(id); if (current.isEmpty()) { return false; }
		if (key.isEmpty()) { key = current; } else if (key != current) { return false; }
	}
	const int target = componentAt(point); if (target < 0) { return false; }
	Q_EMIT vertexSnapRequested(target); return true;
}
// Click selection and drag movement share the same native chords. Defer a
// handle click until release/drag threshold; Ctrl must not deselect the handle
// just before starting an absolute-grid drag. See docs/CREDITS.md for the
// TrenchBroom v2026.2 NodeHandleToolBase / controller behaviour reference.
bool MapViewport::armComponentMove(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	const bool quickSnap = m_componentMode == ComponentMode::Vertices && m_controls.componentQuickSnapModifiers != Qt::NoModifier && modifiers == m_controls.componentQuickSnapModifiers;
	if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !isEnabled() || (!m_controls.componentAxisDrag && !quickSnap) || isMovingComponents() || m_componentBanding
		|| m_componentExtrusion || m_componentGizmo->tool() != LevelComponentTool::Move
		|| (!quickSnap && !levelComponentDragChord(int(m_componentMode),modifiers)) || componentHandleAt(point) < 0) { return false; }
	m_componentNativeArmed = true; m_componentNativePress = point; m_componentNativeModifiers = modifiers; return true;
}
bool ModelViewport::armComponentMove(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	const bool quickSnap = m_componentMode == 1 && m_controls.componentQuickSnapModifiers != Qt::NoModifier && modifiers == m_controls.componentQuickSnapModifiers;
	if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !isEnabled() || (!m_controls.componentAxisDrag && !quickSnap) || isMovingComponents() || m_componentBanding || m_looking
		|| m_pressButton != Qt::NoButton || m_componentExtrusion || m_componentGizmo->tool() != LevelComponentTool::Move
		|| (!quickSnap && !levelComponentDragChord(m_componentMode,modifiers)) || componentAt(point) < 0) { return false; }
	m_componentNativeArmed = true; m_componentNativePress = point; m_componentNativeModifiers = modifiers;
	m_heldKeys.clear(); m_flyTimer->stop(); return true;
}
bool MapViewport::updateComponentModifiers(int key,Qt::KeyboardModifiers modifiers,bool pressed)
{
	if (!m_controls.componentAxisDrag || !levelComponentModifierKey(key)) { return false; }
	modifiers = levelComponentModifierState(key,modifiers,pressed);
	if (m_vertexInsertion->active()) { m_vertexInsertion->updateModifiers(modifiers); return true; }
	if (m_componentDragging && m_componentWorldDrag) { updateComponentMove(m_componentLastPoint,modifiers); return true; }
	return m_componentNativeArmed;
}
bool ModelViewport::updateComponentModifiers(int key,Qt::KeyboardModifiers modifiers,bool pressed)
{
	if (!m_controls.componentAxisDrag || !levelComponentModifierKey(key)) { return false; }
	modifiers = levelComponentModifierState(key,modifiers,pressed);
	if (m_vertexInsertion->active()) { m_vertexInsertion->updateModifiers(modifiers); return true; }
	if (m_componentMoving && m_componentWorldDrag) { updateComponentMove(m_componentLastPoint,modifiers); return true; }
	return m_componentNativeArmed;
}
} // namespace vibestudio
