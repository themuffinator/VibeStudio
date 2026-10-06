#include "app/application_shell.h"
#include "app/patch_stitch_dialog.h"
#include <QStatusBar>

namespace vibestudio
{
bool ApplicationShell::applyLevelPatchStitch(int first, int second, const LevelPatchStitchRequest &request, QString *error)
{
	const auto revision = m_levelMapDocument.revision;
	if (!stitchLevelMapPatches(&m_levelMapDocument, first, second, request, nullptr, error)) {
		return false;
	}
	if (revision == m_levelMapDocument.revision) {
		return true;
	}
	recordActivity(tr("Patch boundaries stitched"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Patch seam updated. Save the map to include it in the next build."));
	return true;
}
void ApplicationShell::stitchLevelMapPatchesFromUi()
{
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	PatchStitchDialog dialog(m_levelMapDocument, archive, activePaletteId(), this);
	const auto serial = m_levelMapLoadSerial, revision = m_levelMapDocument.revision, packageRevision = m_packageStaging.revision(),
			   reload = m_levelPreviewReload;
	const auto selection = m_levelMapDocument.selection;
	const auto palette = activePaletteId();
	dialog.setApplyHandler(
		[this, serial, revision, selection, packageRevision, reload, palette](int first, int second, const auto &request, QString *error) {
			if (serial != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection ||
				packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId()) {
				if (error) {
					*error = tr("The map, selection or package changed during preview. Reopen Stitch Patches from the current map.");
				}
				return false;
			}
			return applyLevelPatchStitch(first, second, request, error);
		});
	dialog.exec();
}
} // namespace vibestudio
