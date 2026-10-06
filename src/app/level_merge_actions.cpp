#include "app/application_shell.h"
#include "app/level_merge_dialog.h"
#include <QStatusBar>

namespace vibestudio
{
bool ApplicationShell::applyLevelBrushMerge(const LevelBrushMergePlan &plan, QString *error)
{
	if (!commitLevelBrushMerge(&m_levelMapDocument, plan, error)) {
		return false;
	}
	recordActivity(tr("Level brushes merged"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Brushes merged. Save the map to include the change in the next build."));
	return true;
}
void ApplicationShell::mergeLevelMapBrushesFromUi()
{
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	LevelMergeDialog dialog(m_levelMapDocument, archive, activePaletteId(), this);
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto selection = m_levelMapDocument.selection;
	const auto packageRevision = m_packageStaging.revision(), reload = m_levelPreviewReload;
	const auto palette = activePaletteId();
	dialog.setApplyHandler([this, load, revision, selection, packageRevision, reload, palette](const auto &plan, QString *error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection ||
			packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId()) {
			if (error) {
				*error = tr("The map, selection or package changed during the preview. Reopen Merge Brushes from the current map.");
			}
			return false;
		}
		return applyLevelBrushMerge(plan, error);
	});
	dialog.exec();
}
} // namespace vibestudio
