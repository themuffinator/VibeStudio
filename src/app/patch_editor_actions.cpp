#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/patch_editor_dialog.h"

#include <QComboBox>
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::applyLevelPatch(const LevelMapPatch& patch, int patchId, QString* error)
{
	const auto previousRevision = m_levelMapDocument.revision;
	int created = -1;
	const bool succeeded = patchId < 0 ? addLevelMapPatch(&m_levelMapDocument, patch, &created, error)
	                                   : replaceLevelMapPatch(&m_levelMapDocument, patchId, patch, error);
	if (!succeeded) {
		return false;
	}
	if (previousRevision == m_levelMapDocument.revision) {
		return true;
	}
	recordActivity(patchId < 0 ? tr("Level patch added") : tr("Level patch edited"),
	               QStringLiteral("patch:%1").arg(patchId < 0 ? created : patchId), QStringLiteral("level-map"), OperationState::Warning,
	               tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Patch updated. Save the map to include it in the next build."));
	return true;
}

void ApplicationShell::editLevelMapPatchFromUi(bool creating)
{
	if (m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		statusBar()->showMessage(tr("Open a Quake III-family map to author patches."));
		return;
	}
	LevelMapPatch patch;
	int patchId = -1;
	QString error;
	if (creating) {
		LevelPatchCreateRequest request;
		if (m_levelMapViewport) {
			request.center = m_levelMapViewport->worldPositionAt(m_levelMapViewport->rect().center(), 0);
			request.center = snapLevelMapPosition(request.center, m_levelMapViewport->gridSize());
			if (m_levelMapViewport->projection() == MapViewportProjection::FrontXZ) {
				request.plane = QStringLiteral("xz");
			} else if (m_levelMapViewport->projection() == MapViewportProjection::SideZY) {
				request.plane = QStringLiteral("yz");
			}
		}
		const auto textures = levelMapTextureUsage(m_levelMapDocument);
		if (!textures.isEmpty()) {
			request.texture = QStringLiteral("textures/") + textures.first().name;
		}
		if (!createLevelPatch(request, &patch, &error)) {
			statusBar()->showMessage(error);
			return;
		}
	} else {
		if (m_levelMapDocument.selectionKind != LevelMapSelectionKind::QuakePatch) {
			statusBar()->showMessage(tr("Select a patch to edit its control points."));
			return;
		}
		for (const auto& candidate : m_levelMapDocument.patches) {
			if (candidate.id == m_levelMapDocument.selectedObjectId) {
				patch = candidate;
				patchId = candidate.id;
				break;
			}
		}
		if (patchId < 0 || !validateLevelPatch(patch, &error)) {
			statusBar()->showMessage(error);
			return;
		}
		LevelMapDocument probe = m_levelMapDocument;
		if (!replaceLevelMapPatch(&probe, patchId, patch, &error)) {
			statusBar()->showMessage(error);
			return;
		}
	}
	PatchEditorDialog dialog(patch, creating, m_levelMapViewport ? m_levelMapViewport->gridSize() : 8, this);
	if (auto* textures = dialog.findChild<QComboBox*>(QStringLiteral("patchTexture"))) {
		for (const auto& use : levelMapTextureUsage(m_levelMapDocument)) {
			if (textures->findText(use.name) < 0) {
				textures->addItem(use.name, use.name);
			}
		}
	}
	if (dialog.exec() == QDialog::Accepted && !applyLevelPatch(dialog.patch(), patchId, &error)) {
		statusBar()->showMessage(error);
	}
}
} // namespace vibestudio
