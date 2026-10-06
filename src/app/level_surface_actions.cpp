#include "app/application_shell.h"
#include "app/level_surface_dialog.h"
#include <QStatusBar>
#include <QTreeWidget>

namespace vibestudio
{
bool ApplicationShell::applyLevelSurfaceEdit(const LevelSurfaceEditPlan &plan, QString *error)
{
	if (!commitLevelSurfaceEdit(&m_levelMapDocument, plan, error)) {
		return false;
	}
	if (plan.faceCount() > 0) {
		recordActivity(tr("Level surfaces aligned"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
					   tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Surfaces aligned. Save the map to include the changes in the next build."));
	}
	return true;
}
void ApplicationShell::editLevelMapSurfacesFromUi()
{
	const auto faces = levelMapSelectedSurfaces(m_levelMapDocument);
	if (faces.isEmpty()) {
		return;
	}
	if (faces.size() > 16384) {
		statusBar()->showMessage(tr("Select at most 16,384 brush faces for Surface Alignment."));
		return;
	}
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	LevelSurfaceDialog dialog(m_levelMapDocument, faces, archive, activePaletteId(), this);
	const auto inspected = inspectedLevelMapSurface();
	if (m_levelMapDocument.selection.size() == 1 && faces.contains(inspected)) {
		dialog.setSelectedFaces({inspected});
	}
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto selection = m_levelMapDocument.selection;
	const auto packageRevision = m_packageStaging.revision();
	const auto reload = m_levelPreviewReload;
	const auto palette = activePaletteId();
	dialog.setApplyHandler([this, load, revision, selection, packageRevision, reload, palette](const auto &plan, QString *error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection ||
			packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId()) {
			if (error) {
				*error = tr("The map, selection or package changed during the preview. Reopen Surface Alignment from the current map.");
			}
			return false;
		}
		return applyLevelSurfaceEdit(plan, error);
	});
	dialog.exec();
}
} // namespace vibestudio
