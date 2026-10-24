#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "core/level_placement.h"
#include "core/level_face_clipboard.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>

namespace vibestudio
{
void ApplicationShell::pasteLevelMapFromClipboard(LevelClipboardPlacement mode)
{
	const auto text = QGuiApplication::clipboard()->text();
	if (looksLikeLevelFaceText(text)) {
		const auto picked = levelPickedSurfaceFaces();
		const auto faces = levelComponentsActive() || !picked.isEmpty() ? picked : levelMapSelectedSurfaces(m_levelMapDocument);
		QVector<LevelMaterialTarget> targets;
		for (const auto& face : faces) { targets << LevelMaterialTarget{LevelMaterialKind::BrushFace, face.brushId, face.faceIndex}; }
		LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Attributes;
		QString error;
		if (!startLevelSurfacePaste(targets, options, text, &error)) { statusBar()->showMessage(tr("Could not paste face attributes: %1").arg(error)); }
		return;
	}
	if (mode == LevelClipboardPlacement::Profile) {
		mode = m_levelControls.pasteAtCursor ? LevelClipboardPlacement::Cursor : LevelClipboardPlacement::Original;
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Paste;
	request.text = text;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	auto& placement = request.paste;
	placement.grid = m_levelMapViewport && m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0;
	fillLevelPlacementEntityBounds(&placement);
	if (mode == LevelClipboardPlacement::Camera) {
		QString problem;
		if (!levelCameraAnchor(&placement.point, &problem)) {
			statusBar()->showMessage(problem);
			return;
		}
		placement.mode = LevelPasteMode::Camera;
	} else if (mode == LevelClipboardPlacement::Cursor) {
		// The camera's ray or the plan view's plane under the pointer, shared
		// with the Create <class> commands (level_entity_create_actions.cpp).
		QString problem;
		if (!levelCursorPlacement(&placement, &problem)) {
			statusBar()->showMessage(problem);
			return;
		}
	}
	QString error;
	if (!runLevelPlacementFromUi(request, &error)) {
		statusBar()->showMessage(tr("Could not paste: %1").arg(error));
		return;
	}
	const int count = static_cast<int>(m_levelMapDocument.selection.size());
	recordActivity(tr("Level map objects pasted"), tr("%n object(s)", nullptr, count), QStringLiteral("level-map"), OperationState::Warning,
	               tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Pasted %n map object(s); they are selected.", nullptr, count));
}

bool ApplicationShell::levelCameraAnchor(LevelMapVec3* point, QString* error) const
{
	if (!m_levelMap3D || !m_levelMap3D->isEnabled()) {
		if (error) {
			*error = tr("Wait for the map camera before placing objects there.");
		}
		return false;
	}
	// An orthographic orbit has no finite eye. Its visible centre on the
	// focus plane is the placement anchor, including any current pan.
	const auto position = m_levelMap3D->isPerspective()
	                          ? m_levelMap3D->cameraPosition()
	                          : m_levelMap3D->viewRay({m_levelMap3D->width() * 0.5, m_levelMap3D->height() * 0.5}).origin;
	if (point) {
		*point = {position.x, position.y, position.z, true};
	}
	return true;
}

void ApplicationShell::fillLevelPlacementEntityBounds(LevelPastePlacement* placement) const
{
	if (!placement) {
		return;
	}
	for (const auto& definition : m_entityDefinitions.classes) {
		if (definition.kind == EntityClassKind::Point && definition.hasSize) {
			placement->entityBounds.insert(definition.className.toCaseFolded(),
			                               {{definition.mins[0], definition.mins[1], definition.mins[2]},
			                                {definition.maxs[0], definition.maxs[1], definition.maxs[2]}});
		}
	}
}
} // namespace vibestudio
