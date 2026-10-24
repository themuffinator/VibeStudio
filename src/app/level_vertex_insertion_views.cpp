#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QTimer>

namespace vibestudio {
namespace {
bool gesture(bool tool,Qt::KeyboardModifiers keys,Qt::KeyboardModifiers chord,bool worldAxes)
{
	keys &= Qt::ShiftModifier|Qt::ControlModifier|Qt::AltModifier|Qt::MetaModifier;
	if (worldAxes && !chord.testFlag(Qt::ControlModifier)) { keys &= ~Qt::ControlModifier; }
	return (tool && (keys == Qt::NoModifier || keys == Qt::AltModifier))
		|| (chord != Qt::NoModifier && (keys == chord || keys == (chord|Qt::AltModifier)));
}
}
bool MapViewport::hoverVertexInsertion(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	const bool show = isEnabled() && m_componentMode == ComponentMode::Vertices && !isMovingComponents() && !m_componentBanding
		&& gesture(m_vertexInsertion->tool(),modifiers,m_controls.componentInsertModifiers,m_controls.componentAxisDrag) && componentHandleAt(point) < 0;
	return m_vertexInsertion->hover(point,componentGizmoFrame(),show);
}
bool MapViewport::beginVertexInsertion(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (!hoverVertexInsertion(point,modifiers)) { return false; }
	return m_vertexInsertion->begin(point,componentGizmoFrame(),modifiers.testFlag(Qt::AltModifier) ? ModelMoveConstraint::Z : ModelMoveConstraint::ViewPlane,modifiers);
}
bool ModelViewport::hoverVertexInsertion(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (!isEnabled() || m_componentMode != 1 || isMovingComponents() || m_componentBanding || m_looking || m_pressButton != Qt::NoButton
		|| !gesture(m_vertexInsertion->tool(),modifiers,m_controls.componentInsertModifiers,m_controls.componentAxisDrag)) { m_vertexInsertion->clearHover(); return false; }
	ensureProjection(); if (isRendering()) { m_vertexInsertion->clearHover(); return false; }
	return m_vertexInsertion->hover(point,componentGizmoFrame(),componentAt(point) < 0);
}
bool ModelViewport::beginVertexInsertion(const QPointF& point,Qt::KeyboardModifiers modifiers)
{
	if (!hoverVertexInsertion(point,modifiers)) { return false; }
	if (!m_vertexInsertion->begin(point,componentGizmoFrame(),modifiers.testFlag(Qt::AltModifier) ? ModelMoveConstraint::Z : ModelMoveConstraint::ViewPlane,modifiers)) { return false; }
	m_heldKeys.clear(); m_flyTimer->stop(); return true;
}
} // namespace vibestudio
