#include "app/application_shell.h"
#include "app/level_rotation_dialog.h"
#include "app/map_viewport.h"
#include "core/level_placement.h"
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::applyLevelRotation(const LevelMapRotationRequest& request, QString* error) {
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::Rotate;
	work.rotation = request;
	if (!runLevelPlacementFromUi(work, error)) {
		return false;
	}
	recordActivity(tr("Level selection rotated"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Selection rotated. Save the map to include it in the next build."));
	return true;
}
void ApplicationShell::rotateLevelMapSelectionPreciselyFromUi() {
	if (m_levelMapDocument.selection.isEmpty()) {
		return;
	}
	int axis = 2;
	if (m_levelMapViewport && m_levelMapDocument.format != LevelMapFormat::DoomWad) {
		if (m_levelMapViewport->projection() == MapViewportProjection::FrontXZ) {
			axis = 1;
		}
		if (m_levelMapViewport->projection() == MapViewportProjection::SideZY) {
			axis = 0;
		}
	}
	LevelRotationDialog dialog(m_levelMapDocument, axis, this);
	auto initial = dialog.request();
	initial.textureLock = m_settings.levelTextureLock();
	initial.allowValve220 = m_settings.levelAllowValve220();
	dialog.setRequest(initial);
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto selection = m_levelMapDocument.selection;
	dialog.setApplyHandler([this, load, revision, selection](const auto& request, QString* error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection) {
			if (error) {
				*error = tr("The map or selection changed while this preview was open. Reopen Rotate Selection from the current map.");
			}
			return false;
		}
		return applyLevelRotation(request, error);
	});
	dialog.exec();
}
} // namespace vibestudio
