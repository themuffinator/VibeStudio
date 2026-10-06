#include "app/application_shell.h"
#include "app/level_placement_dialog.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>

namespace vibestudio {
std::function<bool(const LevelMapDocument&, QString*)> ApplicationShell::levelPlacementCommitter() {
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto selection = m_levelMapDocument.selection;
	const auto activeNode = m_levelMapDocument.activeSceneNode;
	const auto sourcePath = m_levelMapDocument.sourcePath;
	const auto sourceHash = m_levelMapDocument.sourceContentHash;
	const auto savedDepth = m_levelMapDocument.savedUndoDepth;
	const auto packageRevision = m_packageStaging.revision(), reload = m_levelPreviewReload;
	const auto palette = activePaletteId();
	return [this, load, revision, selection, activeNode, sourcePath, sourceHash, savedDepth, packageRevision, reload,
			palette](const LevelMapDocument& candidate, QString* error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection ||
			activeNode != m_levelMapDocument.activeSceneNode || sourcePath != m_levelMapDocument.sourcePath ||
			sourceHash != m_levelMapDocument.sourceContentHash || savedDepth != m_levelMapDocument.savedUndoDepth ||
			packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId()) {
			if (error) {
				*error = tr("The map, selection, scene destination or package changed. Reopen placement from the current state.");
			}
			return false;
		}
		m_levelMapDocument = candidate;
		return true;
	};
}
bool ApplicationShell::runLevelPlacementFromUi(const LevelPlacementRequest& request, QString* error) {
	const auto publish = levelPlacementCommitter();
	const auto result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, request);
	if (!result.succeeded) {
		if (error) {
			*error = result.error;
		}
		return false;
	}
	return publish(result.document, error);
}
void ApplicationShell::placeLevelMapObjectsFromUi(bool paste) {
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if ((paste && !textMap) || (!paste && !levelMapSelectionIsDuplicable(m_levelMapDocument))) {
		return;
	}
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	LevelPlacementDialog dialog(m_levelMapDocument, paste ? LevelPlacementMode::Paste : LevelPlacementMode::Duplicate,
								paste ? QGuiApplication::clipboard()->text() : QString(), archive, activePaletteId(), this);
	dialog.setTextureLock(m_settings.levelTextureLock());
	const double step = m_levelMapViewport ? std::max(1, m_levelMapViewport->gridSize()) : 16;
	dialog.setOffset({step, 0, 0, true});
	const auto publish = levelPlacementCommitter();
	dialog.setApplyHandler([this, publish](const LevelMapDocument& candidate, QString* error) {
		if (!publish(candidate, error)) {
			return false;
		}
		recordActivity(tr("Level objects placed"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
					   tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Objects placed as one undo step. Save the map before building."));
		return true;
	});
	dialog.exec();
}
} // namespace vibestudio
