#include "app/model_viewport.h"
#include <QScopedValueRollback>
#include <QTimer>

namespace vibestudio {

void ModelViewport::setMaterialGesturesEnabled(bool enabled)
{
	if (!enabled) { finishMaterialStroke(false); clearMaterialStrokePreview(); }
	m_materialGesturesEnabled = enabled;
}

bool ModelViewport::materialGestureAt(const QPointF& point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
	const auto action = cameraMaterialGesture(m_controls, button, modifiers);
	if (!m_materialGesturesEnabled || action == CameraMaterialGesture::None || !isEnabled() || !hasMesh()) { return false; }
	if (m_looking || m_pointerDriving || m_pressButton != Qt::NoButton || m_surfaceStrokeActive || m_surfaceToolPressed || m_materialStrokeRaster
		|| m_editMoveActive || m_dragStarted || isDrawingBrush() || isResizingSelection() || !m_heldKeys.isEmpty()) {
		Q_EMIT surfaceActionMessage(tr("Finish the current gesture or camera movement before sampling or painting a material."));
		return false;
	}
	ensureProjection();
	if (isRendering()) {
		Q_EMIT surfaceActionMessage(tr("The camera preview is updating. Try the material action when it is ready."));
		return false;
	}
	const auto hit = hitTest(point);
	if (!hit.valid) {
		Q_EMIT surfaceActionMessage(tr("No map surface at this point."));
		return false;
	}
	// The shell resolves the current source identity and commits through the
	// shared material service. Never change the selected tool or scene selection.
	if (action == CameraMaterialGesture::Sample) { Q_EMIT surfaceSampleRequested(hit.triangle); }
	else if (action == CameraMaterialGesture::Paint) { Q_EMIT surfacePaintRequested(hit.triangle); }
	else { Q_EMIT surfacePasteRequested(hit.triangle, action); }
	return true;
}

namespace {
bool strokeAction(CameraMaterialGesture action)
{
	return action == CameraMaterialGesture::ValuesSelection || action == CameraMaterialGesture::ValuesSelectionOnly
		|| action == CameraMaterialGesture::ProjectSelection || action == CameraMaterialGesture::ProjectSelectionOnly
		|| action == CameraMaterialGesture::WrapFace || action == CameraMaterialGesture::WrapFaceOnly;
}
}

bool ModelViewport::beginMaterialStroke(const QPointF& point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
	const auto action = cameraMaterialGesture(m_controls, button, modifiers);
	if (!strokeAction(action)) { return materialGestureAt(point, button, modifiers); }
	if (!m_materialGesturesEnabled || !isEnabled() || !hasMesh()) { return false; }
	if (m_looking || m_pointerDriving || m_pressButton != Qt::NoButton || m_surfaceStrokeActive || m_surfaceToolPressed || m_materialStrokeRaster
		|| m_editMoveActive || m_dragStarted || isDrawingBrush() || isResizingSelection() || !m_heldKeys.isEmpty()) {
		Q_EMIT surfaceActionMessage(tr("Finish the current gesture or camera movement before sampling or painting a material.")); return false;
	}
	ensureProjection();
	if (isRendering()) { Q_EMIT surfaceActionMessage(tr("The camera preview is updating. Try the material action when it is ready.")); return false; }
	const auto hit = hitTest(point);
	if (!hit.valid) { Q_EMIT surfaceActionMessage(tr("No map surface at this point.")); return false; }
	m_materialStrokeRaster = m_presentedRaster; m_materialStrokeSkins = m_surfaceSkins;
	m_materialStrokeHighlights.clear();
	for (int i = 0; i < m_highlighted.size(); ++i) { if (m_highlighted[i]) { m_materialStrokeHighlights << i; } }
	m_materialStrokeButton = button; m_materialStrokePreviewChanged = false;
	Q_EMIT materialStrokeBegan();
	if (!materialStrokeActive()) { return false; }
	Q_EMIT materialStrokeTouched(hit.triangle, action);
	setAccessibleDescription(accessibleSummary()); return materialStrokeActive();
}

bool ModelViewport::extendMaterialStroke(const QPointF& point, Qt::KeyboardModifiers modifiers)
{
	if (!materialStrokeActive() || !isEnabled()) { return false; }
	const auto action = cameraMaterialGesture(m_controls, m_materialStrokeButton, modifiers);
	if (!strokeAction(action)) { return false; }
	const int triangle = materialStrokeTriangleAt(point);
	if (triangle < 0) { return false; }
	Q_EMIT materialStrokeTouched(triangle, action); return true;
}

void ModelViewport::finishMaterialStroke(bool commit)
{
	if (!materialStrokeActive()) {
		if (!commit && m_materialStrokeRaster) { clearMaterialStrokePreview(); Q_EMIT materialStrokeEnded(false); }
		return;
	}
	m_materialStrokeButton = Qt::NoButton;
	if (!commit) { clearMaterialStrokePreview(); }
	Q_EMIT materialStrokeEnded(commit);
	setAccessibleDescription(accessibleSummary());
}

void ModelViewport::setMaterialStrokePreview(const ModelMesh& mesh, const QHash<int, QImage>& skins, const QVector<int>& triangles, int count)
{
	if (!m_materialStrokeRaster) { return; }
	const QScopedValueRollback guard(m_materialStrokePreviewChange, true);
	m_materialStrokePreviewChanged = true;
	setMesh(mesh, true); setSurfaceSkins(skins); setHighlightedTriangles({});
	setSurfaceStrokePreview(triangles, count);
}

void ModelViewport::setSurfaceTool(ModelViewportSurfaceTool tool)
{
	setBrushDrawTool(false);
	if (m_surfaceTool == tool) { return; }
	finishMaterialStroke(false); clearMaterialStrokePreview();
	finishSurfaceStroke(false);
	finishEditTransform(false);
	finishSelectionResize(false);
	setLooking(false);
	if (m_dragStarted && m_dragAction == DragAction::Move) { cancelMove(); }
	m_pressButton = Qt::NoButton;
	m_orbiting = false;
	m_panning = false;
	m_dragStarted = false;
	m_dragAction = DragAction::None;
	m_heldKeys.clear();
	m_flyTimer->stop();
	m_surfaceTool = tool;
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT surfaceToolChanged();
}

bool ModelViewport::beginSurfaceStroke(const QPointF& point)
{
	if (m_surfaceTool != ModelViewportSurfaceTool::Paint || !isEnabled() || !hasMesh() || m_editMoveActive || m_looking) { return false; }
	ensureProjection();
	if (isRendering()) { return false; }
	finishSurfaceStroke(false);
	m_heldKeys.clear();
	m_flyTimer->stop();
	m_surfaceStrokeActive = true;
	Q_EMIT surfaceStrokeBegan();
	if (!m_surfaceStrokeActive) { return false; }
	extendSurfaceStroke(point);
	return m_surfaceStrokeActive;
}

bool ModelViewport::extendSurfaceStroke(const QPointF& point)
{
	if (!m_surfaceStrokeActive || !isEnabled()) { return false; }
	// Stroke highlighting may be rendering; projection and geometry are fixed
	// for this gesture, so hits remain valid against the same source triangles.
	const auto hit = hitTest(point);
	if (!hit.valid) { return false; }
	Q_EMIT surfaceTouched(hit.triangle);
	return true;
}

void ModelViewport::finishSurfaceStroke(bool commit)
{
	const bool active = m_surfaceStrokeActive;
	m_surfaceStrokeActive = false;
	m_surfaceToolPressed = false;
	setSurfaceStrokePreview({}, 0);
	if (active) { Q_EMIT surfaceStrokeEnded(commit); }
}

bool ModelViewport::sampleSurfaceAt(const QPointF& point)
{
	if (m_surfaceTool != ModelViewportSurfaceTool::Sample || !isEnabled() || !hasMesh() || m_editMoveActive || m_looking) { return false; }
	ensureProjection();
	if (isRendering()) { return false; }
	const auto hit = hitTest(point);
	if (!hit.valid) { return false; }
	Q_EMIT surfaceSampleRequested(hit.triangle);
	return true;
}

void ModelViewport::setSurfaceStrokePreview(const QVector<int>& triangles, int surfaceCount)
{
	QSet<int> valid;
	for (int triangle : triangles) { if (triangle >= 0 && triangle < m_meshTriangles.size()) { valid.insert(triangle); } }
	if (valid == m_surfaceStrokeTriangles && surfaceCount == m_surfaceStrokeCount) { return; }
	m_surfaceStrokeTriangles = valid;
	m_surfaceStrokeCount = surfaceCount;
	invalidateRaster();
	setAccessibleDescription(accessibleSummary());
	update();
}

} // namespace vibestudio
