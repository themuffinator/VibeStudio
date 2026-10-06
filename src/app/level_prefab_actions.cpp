#include "app/application_shell.h"
#include "app/level_prefab_dialog.h"
#include "app/map_viewport.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>

namespace vibestudio
{
void ApplicationShell::showLevelPrefab(bool capture, bool stage, const QString &path, bool packageEntry)
{
	if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		statusBar()->showMessage(tr("Open a Quake-family map to work with prefabs."));
		return;
	}
	if (capture && m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(tr("Select map objects to capture a prefab."));
		return;
	}
	QString input = path;
	if (!capture && input.isEmpty()) {
		input =
			QFileDialog::getOpenFileName(this, tr("Insert Prefab"), m_settings.currentProjectPath(), tr("VibeStudio prefabs (*.vprefab)"));
		if (input.isEmpty()) {
			return;
		}
	}
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	const auto mode = capture ? (stage ? LevelPrefabDialogMode::Stage : LevelPrefabDialogMode::Export) : LevelPrefabDialogMode::Insert;
	LevelPrefabDialog dialog(m_levelMapDocument, mode, input, packageEntry, archive, activePaletteId(), this);
	if (!capture && m_levelMapViewport) {
		LevelPrefabPlacement placement;
		placement.position = m_levelMapViewport->worldPositionAt(QPointF(m_levelMapViewport->rect().center()), 0);
		placement.textureLock = m_settings.levelTextureLock();
		dialog.setPlacement(placement);
	}
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto selection = m_levelMapDocument.selection;
	const auto packageRevision = m_packageStaging.revision(), reload = m_levelPreviewReload;
	const auto palette = activePaletteId();
	dialog.setGuard([this, load, revision, selection, packageRevision, reload, palette](QString *error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || selection != m_levelMapDocument.selection ||
			packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId()) {
			if (error) {
				*error = tr("The map, selection or package changed while this prefab preview was open. Reopen it from the current state.");
			}
			return false;
		}
		return true;
	});
	dialog.setInsertHandler([this](const LevelMapDocument &candidate, const LevelPrefabReport &report, QString *) {
		m_levelMapDocument = candidate;
		recordActivity(tr("Prefab inserted"), tr("%1 objects selected").arg(report.inserted.size()), QStringLiteral("level-map"),
					   OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Prefab inserted as one undo step. Save the map before building."));
		return true;
	});
	dialog.setStageHandler([this](const QByteArray &bytes, const QString &virtualPath, bool replace, QString *error) {
		if (m_packageSaveRunning || m_packageReadRunning || m_packageExtractionRunning ||
			m_packageStaging.sourceFormat() == PackageArchiveFormat::Wad) {
			if (error) {
				*error = tr("Prefab staging needs an idle folder, PAK, ZIP or PK3 package.");
			}
			return false;
		}
		if (!m_packageStaging.isLoaded()) {
			if (error) {
				*error = tr("Open or create a package before staging a prefab.");
			}
			return false;
		}
		if (!m_packageStaging.addBytes(bytes, virtualPath, error,
									   replace ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block)) {
			return false;
		}
		refreshPackageStagingSummary();
		recordActivity(tr("Prefab staged"), virtualPath, QStringLiteral("package"), OperationState::Warning, tr("Package save pending"));
		statusBar()->showMessage(tr("Prefab staged at %1. Save the package to publish it.").arg(virtualPath));
		return true;
	});
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	if (mode == LevelPrefabDialogMode::Export) {
		const auto &result = dialog.writeReport();
		QStringList details{result.path};
		if (!result.backupPath.isEmpty()) {
			details << tr("Backup: %1").arg(result.backupPath);
		}
		details += result.warnings;
		details += result.recoveryPaths;
		recordActivity(tr("Prefab exported"), details.join('\n'), QStringLiteral("level-map"),
					   result.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning, tr("Prefab saved"));
		statusBar()->showMessage(tr("Prefab saved to %1").arg(result.path));
		if (!result.warnings.isEmpty() || !result.recoveryPaths.isEmpty()) {
			QMessageBox::warning(this, tr("Prefab saved with recovery details"), details.join('\n'));
		}
	}
}
} // namespace vibestudio
