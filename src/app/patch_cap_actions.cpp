#include "app/application_shell.h"
#include "app/patch_cap_dialog.h"
#include <QStatusBar>

namespace vibestudio
{
bool ApplicationShell::applyLevelPatchCaps(int patchId, const LevelPatchCapRequest &request, QString *error)
{
	if (!capLevelMapPatch(&m_levelMapDocument, patchId, request, nullptr, error)) {
		return false;
	}
	recordActivity(tr("Patch caps added"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Caps added. Save the map to include them in the next build."));
	return true;
}
void ApplicationShell::capLevelMapPatchFromUi()
{
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	PatchCapDialog dialog(m_levelMapDocument, archive, activePaletteId(), this);
	const auto serial = m_levelMapLoadSerial, revision = m_levelMapDocument.revision, packageRevision = m_packageStaging.revision(),
			   reload = m_levelPreviewReload;
	const auto selection = m_levelMapDocument.selection;
	const auto palette = activePaletteId();
	dialog.setApplyHandler(
		[this, serial, revision, packageRevision, reload, selection, palette](int id, const auto &request, QString *error) {
			if (serial != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection ||
				packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId()) {
				if (error) {
					*error = tr("The map, selection or package changed during preview. Reopen Cap Patch from the current map.");
				}
				return false;
			}
			return applyLevelPatchCaps(id, request, error);
		});
	dialog.exec();
}
} // namespace vibestudio
