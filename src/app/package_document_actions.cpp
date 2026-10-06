#include "app/application_shell.h"

#include "app/package_operation_dialog.h"
#include "app/package_entry_view.h"
#include "app/package_folder_view.h"
#include "app/package_wad_groups_dialog.h"
#include "app/studio_actions.h"
#include "core/package_draft.h"

#include <QAction>
#include <QAbstractButton>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QTreeWidget>

#include <algorithm>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QStatusBar>

namespace vibestudio {

qsizetype ApplicationShell::selectedPackageEntryIndex() const
{
	if (!m_packageEntries || !m_packageEntries->readyFor(packageViewKey())) { return -1; }
	const auto item = m_packageEntries->currentIndex();
	bool valid = false; const auto index = item.data(Qt::UserRole + 6).toLongLong(&valid);
	const auto entries = packageViewArchive().entries();
	return valid && index >= 0 && index < entries.size() && entries.at(index).virtualPath == item.data(Qt::UserRole).toString() ? index : -1;
}

QVector<qsizetype> ApplicationShell::selectedPackageEntryIndexes() const
{
	if (!m_packageEntries || !m_packageEntries->readyFor(packageViewKey())) { return {}; }
	return m_packageEntries->selectedEntryIndexes();
}

QString ApplicationShell::packageViewKey() const
{
	return QStringLiteral("%1|%2|%3|%4|%5").arg(m_packageStaging.sourcePath(), m_packageStaging.draftPath())
		.arg(m_packageStaging.isLoaded()).arg(m_packageStaging.revision()).arg(m_levelPreviewReload);
}

const PackageArchive& ApplicationShell::packageViewArchive() const
{
	const QString key = packageViewKey();
	if (key != m_packageViewKey) {
		m_packageViewArchive = packagePlannedArchive(m_packageStaging);
		m_packageViewKey = key;
	}
	return m_packageViewArchive;
}

QString ApplicationShell::packageOpenPath() const
{
	return m_packageStaging.draftPath().isEmpty() ? m_packageArchive.sourcePath() : m_packageStaging.draftPath();
}

QString ApplicationShell::packageWatchPath() const
{
	return m_packageStaging.draftPath().isEmpty() ? m_packageArchive.sourcePath()
		: QDir(m_packageStaging.draftPath()).filePath(QStringLiteral("document.json"));
}

void ApplicationShell::openPackageDraft()
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return; }
	const QString path = QFileDialog::getExistingDirectory(this, tr("Open Package Draft (.vibepackage)"),
		m_packageStaging.draftPath().isEmpty() ? m_settings.currentProjectPath() : m_packageStaging.draftPath());
	if (path.isEmpty() || !confirmStagedPackageChangesHandled()) { return; }
	if (!path.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) {
		QMessageBox::warning(this, tr("Open Package Draft"), tr("Choose the complete .vibepackage draft directory.")); return;
	}
	loadPackagePath(path);
	setMode(StudioMode::Packages);
}

bool ApplicationShell::savePackageDraft(bool saveAs)
{
	if (!m_packageStaging.isLoaded() || m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return false; }
	QString path = saveAs ? QString() : m_packageStaging.draftPath();
	if (path.isEmpty()) {
		QString name = QFileInfo(m_packageStaging.sourcePath()).completeBaseName();
		if (name.isEmpty()) { name = QStringLiteral("package"); }
		const QString directory = !m_settings.currentProjectPath().isEmpty() ? m_settings.currentProjectPath()
			: m_packageStaging.sourcePath().isEmpty() ? QDir::currentPath() : QFileInfo(m_packageStaging.sourcePath()).absolutePath();
		path = QFileDialog::getSaveFileName(this, tr("Save Package Draft"), QDir(directory).filePath(name + QStringLiteral(".vibepackage")),
			tr("Package Draft Directory (*.vibepackage)"), nullptr, QFileDialog::DontConfirmOverwrite);
		if (path.isEmpty()) { return false; }
		if (!path.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) { path += QStringLiteral(".vibepackage"); }
		if (QFileInfo::exists(QDir(path).filePath(QStringLiteral("document.json")))
			&& QMessageBox::question(this, tr("Replace Package Draft"), tr("Replace the saved draft in %1?").arg(QDir::toNativeSeparators(path)),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) { return false; }
	}
	PackageLoadResult result;
	{
		QScopedValueRollback<bool> running(m_packageReadRunning, true);
		refreshCommandEnablement();
		result = runPackageDraftSaveDialog(this, m_packageStaging, path, true);
	}
	const bool succeeded = result.ready();
	if (succeeded) {
		retirePackageRecovery();
		m_packageStaging = std::move(result.staging);
		m_packageArchive = std::move(result.archive);
		++m_levelPreviewReload;
		m_levelPackagePreviewKey.clear();
		registerWatchedDocument(packageWatchPath(), DocumentWatchRole::Package);
		m_settings.recordRecentFile(QStringLiteral("package"), path);
		refreshPackageBrowser();
		recordActivity(tr("Package Draft Saved"), QDir::toNativeSeparators(path), tr("package"), OperationState::Completed,
			tr("Package content, staged edits, and undo/redo history are preserved in the draft directory."));
		statusBar()->showMessage(tr("Package draft saved: %1").arg(QDir::toNativeSeparators(path)));
	} else {
		refreshPackageStagingSummary(); refreshCommandEnablement();
		if (!result.cancelled) { QMessageBox::warning(this, tr("Save Package Draft"), result.error); }
		statusBar()->showMessage(result.cancelled ? tr("Package draft save cancelled.") : result.error);
	}
	return succeeded;
}

bool ApplicationShell::applyPackageEdit(const QString& label, const PackageEditOperation& edit)
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return false; }
	const auto key = packageViewKey();
	PackageEditResult result;
	{
		QScopedValueRollback<bool> running(m_packageReadRunning, true);
		refreshCommandEnablement();
		result = runPackageEditDialog(this, m_packageStaging, label, edit);
	}
	if (key != packageViewKey()) {
		refreshCommandEnablement();
		statusBar()->showMessage(tr("The package changed. The prepared edit was not applied.")); return false;
	}
	if (!result.ready()) {
		refreshCommandEnablement();
		statusBar()->showMessage(result.cancelled ? tr("Package edit cancelled.") : tr("Package edit blocked: %1").arg(result.error)); return false;
	}
	m_packageStaging = std::move(result.staging);
	m_packageViewArchive = std::move(result.view); m_packageViewKey = packageViewKey();
	refreshPackageStagingSummary(); refreshCommandEnablement();
	return true;
}

void ApplicationShell::undoPackageEdit(bool redo)
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return; }
	const QString label = redo ? m_packageStaging.redoLabel() : m_packageStaging.undoLabel();
	if (!(redo ? m_packageStaging.canRedo() : m_packageStaging.canUndo())) { return; }
	if (!applyPackageEdit(label, [redo](auto& plan, QString*, const PackageReadControl&) {
		return redo ? plan.redo() : plan.undo();
	})) { return; }
	statusBar()->showMessage(redo ? tr("Redone: %1").arg(label) : tr("Undone: %1").arg(label));
}

void ApplicationShell::newPackage()
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return; }
	const QStringList choices{tr("PK3 package"), tr("ZIP archive"), tr("Quake PAK"), tr("Doom PWAD"), tr("Doom IWAD"), tr("Quake WAD2"), tr("Half-Life WAD3")};
	bool accepted = false;
	const QString choice = QInputDialog::getItem(this, tr("New Package"), tr("Package format:"), choices, 0, false, &accepted);
	if (!accepted) { return; }
	const int index = choices.indexOf(choice);
	if (index < 0) { return; }
	const auto format = index == 0 ? PackageArchiveFormat::Pk3 : index == 1 ? PackageArchiveFormat::Zip : index == 2 ? PackageArchiveFormat::Pak : PackageArchiveFormat::Wad;
	const QString magic = index < 3 ? QString() : QStringList{QStringLiteral("PWAD"), QStringLiteral("IWAD"), QStringLiteral("WAD2"), QStringLiteral("WAD3")}.at(index - 3);
	PackageStagingModel next; QString error;
	if (!next.createEmpty(format, magic, &error)) { statusBar()->showMessage(error); return; }
	if (!confirmStagedPackageChangesHandled()) { return; }
	retirePackageRecovery();
	unloadAudioPlayback(); m_audioShownPath.clear();
	if (m_quickOpen && m_quickOpen->isVisible()) { m_quickOpen->reject(); }
	m_packageStaging = std::move(next);
	m_packageArchive = PackageDraft::baseArchive(m_packageStaging);
	++m_levelPreviewReload; m_levelPackagePreviewKey.clear();
	m_packageBrowseSource.clear(); m_packageBrowseFolder.clear();
	m_packageFolderHistory = {QString()}; m_packageFolderHistoryIndex = 0;
	if (m_packageFilter) { m_packageFilter->clear(); }
	refreshPackageBrowser(); refreshWorkspaceDashboard();
	setMode(StudioMode::Packages);
	recordActivity(tr("Package Created"), choice, tr("package"), OperationState::Completed, tr("Untitled package ready to edit."));
	statusBar()->showMessage(tr("New %1 ready. Save a draft to preserve its edit history.").arg(choice));
}

void ApplicationShell::createPackageDirectory()
{
	if (!m_packageStaging.isLoaded() || m_packageStaging.sourceFormat() == PackageArchiveFormat::Wad
		|| m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return; }
	const QString selectionKey = packageViewKey();
	bool accepted = false;
	const QString initial = m_packageBrowseFolder.isEmpty() ? QString() : m_packageBrowseFolder + QLatin1Char('/');
	const QString path = QInputDialog::getText(this, tr("New Package Folder"), tr("Virtual folder path:"), QLineEdit::Normal, initial, &accepted).trimmed();
	if (!accepted || path.isEmpty()) { return; }
	if (selectionKey != packageViewKey()) { statusBar()->showMessage(tr("The package changed; choose the folder again.")); return; }
	if (!applyPackageEdit(tr("New Package Folder"), [path](auto& plan, QString* error, const auto& control) {
		return plan.createDirectory(path, error, control);
	})) { return; }
	revealPackageEntry(normalizePackageVirtualPath(path, false).normalizedPath);
	statusBar()->showMessage(tr("Folder created: %1").arg(path));
}

void ApplicationShell::editPackageWadGroups()
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning || !m_packageStaging.isLoaded()) { return; }
	const auto key = packageViewKey();
	auto* dialog = new PackageWadGroupsDialog(m_packageStaging, this);
	dialog->apply = [this, key](PackageStagingModel&& candidate, const PackageWadGroupEditReview& review, QString* error) {
		if (key != packageViewKey() || m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) {
			*error = tr("The package changed. Close this review and inspect its groups again."); return false;
		}
		m_packageStaging = std::move(candidate); refreshPackageStagingSummary();
		recordActivity(tr("Edit WAD Group"), review.groupName, tr("package"), OperationState::Completed,
			tr("Changes staged as one undoable edit: %1.").arg(locale().toString(static_cast<qlonglong>(review.changes.size()))));
		statusBar()->showMessage(tr("WAD group edit staged. Undo restores the complete group.")); return true;
	};
	dialog->show();
}

void ApplicationShell::stagePackageRenameSelected()
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning || !m_packageStaging.isLoaded()) { return; }
	const auto index = selectedPackageEntryIndex();
	const auto entries = packageViewArchive().entries();
	if (index < 0) { statusBar()->showMessage(tr("Select a file or folder to rename")); return; }
	stagePackageRenameEntry(entries.at(index));
}

void ApplicationShell::stagePackageRenameEntry(const PackageEntry& selected)
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning || !m_packageStaging.isLoaded()) { return; }
	const QString virtualPath = selected.virtualPath;
	const QString selectionKey = packageViewKey();
	const bool directory = selected.kind == PackageEntryKind::Directory;
	const QString entryLabel = selected.sourceOrdinal >= 0 ? tr("%1 (source entry %2)").arg(virtualPath).arg(selected.sourceOrdinal + 1) : virtualPath;
	bool accepted = false;
	const QString target = QInputDialog::getText(this, directory ? tr("Rename Package Folder") : tr("Stage Rename"),
		tr("New virtual package path:"), QLineEdit::Normal, virtualPath, &accepted).trimmed();
	if (!accepted || target.isEmpty()) { return; }
	PackageStageConflictResolution resolution = PackageStageConflictResolution::Block;
	if (!directory && !choosePackageStageResolution(tr("Stage Rename Conflict Policy"), &resolution)) { return; }
	if (selectionKey != packageViewKey()) { statusBar()->showMessage(tr("The package changed; select the entry again.")); return; }
	if (!applyPackageEdit(directory ? tr("Rename Package Folder") : tr("Stage Rename"),
		[selected, target, resolution](auto& plan, QString* error, const auto& control) {
			return selected.kind == PackageEntryKind::Directory ? plan.renameDirectory(selected.virtualPath, target, error, control)
				: selected.sourceOrdinal >= 0 ? plan.renameOccurrence(static_cast<int>(selected.sourceOrdinal), target, error, resolution, control)
				: plan.renameEntry(selected.virtualPath, target, error, resolution, control);
		})) { return; }
	revealPackageEntry(target, directory ? -1 : selected.sourceOrdinal);
	statusBar()->showMessage(tr("Rename staged: %1").arg(target));
	recordActivity(tr("Package Stage Rename"), tr("%1 -> %2").arg(entryLabel, target), tr("package"), OperationState::Completed,
		tr("Rename staged for save-as."));
}

void ApplicationShell::stagePackageDeleteSelected()
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning || !m_packageStaging.isLoaded()) { return; }
	const auto selected = selectedPackageEntryIndexes();
	const auto entries = packageViewArchive().entries();
	if (selected.isEmpty()) { statusBar()->showMessage(tr("Select files or folders to delete")); return; }
	QVector<PackageEntry> selectedEntries;
	for (const auto index : selected) { selectedEntries << entries.at(index); }
	stagePackageDeleteEntries(selectedEntries);
}

void ApplicationShell::stagePackageDeleteEntries(const QVector<PackageEntry>& selectedEntries)
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning || !m_packageStaging.isLoaded() || selectedEntries.isEmpty()) { return; }
	QStringList paths;
	for (const auto& entry : selectedEntries) {
		paths << (entry.sourceOrdinal >= 0 ? tr("%1 (source entry %2)").arg(entry.virtualPath).arg(entry.sourceOrdinal + 1) : entry.virtualPath);
	}
	const auto label = tr("Delete selected entries");
	if (!applyPackageEdit(label, [selectedEntries, label](auto& plan, QString* error, const auto& control) {
		const auto key = [](const QString& path) { return path.normalized(QString::NormalizationForm_C).toCaseFolded(); };
		QStringList folders;
		for (const auto& entry : selectedEntries) { if (entry.kind == PackageEntryKind::Directory) { folders << key(entry.virtualPath); } }
		if (!plan.beginOperationGroup(label, error, control)) { return false; }
		for (const auto& entry : selectedEntries) {
			if (control.isCancelled && control.isCancelled()) { return false; }
			const auto pathKey = key(entry.virtualPath);
			const bool nested = std::any_of(folders.cbegin(), folders.cend(), [&](const QString& folder) { return pathKey.startsWith(folder + QLatin1Char('/')); });
			if (nested) { continue; }
			const bool staged = entry.kind == PackageEntryKind::Directory ? plan.deleteDirectory(entry.virtualPath, error, control)
				: entry.sourceOrdinal >= 0 ? plan.deleteOccurrence(static_cast<int>(entry.sourceOrdinal), error, PackageStageConflictResolution::Block, control)
					: plan.deleteEntry(entry.virtualPath, error, PackageStageConflictResolution::Block, control);
			if (!staged) { return false; }
		}
		return plan.endOperationGroup(true, error, control);
	})) { return; }
	recordActivity(tr("Package Stage Delete"), paths.join(QStringLiteral("; ")), tr("package"), OperationState::Completed, tr("Delete staged for save-as."));
	statusBar()->showMessage(tr("Deletion staged. Undo restores the selected entries."));
}

void ApplicationShell::stagePackageFolder(bool rename, const QString& path, const QString& revision)
{
	if (!m_packageTree || path.isEmpty() || m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return; }
	if (revision != packageViewKey() || !m_packageTree->readyFor(revision) || !m_packageTree->folderIndex(path).isValid()) {
		statusBar()->showMessage(tr("The package changed; choose the folder again.")); return;
	}
	// Folder identity is already established by the prepared tree. Entry-list
	// navigation can still be filtering, so it must not supply this selection.
	PackageEntry entry; entry.virtualPath = path; entry.kind = PackageEntryKind::Directory;
	if (rename) { stagePackageRenameEntry(entry); } else { stagePackageDeleteEntries({entry}); }
}

void ApplicationShell::showPackageTreeContextMenu(const QPoint& position)
{
	if (!m_packageTree || !m_packageTree->readyFor(packageViewKey()) || !m_packageStaging.isLoaded()
		|| m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return; }
	auto item = m_packageTree->indexAt(position);
	if (!item.isValid()) { item = m_packageTree->currentIndex(); }
	if (!item.isValid() || !item.data(Qt::UserRole).isValid()) { return; }
	const auto path = item.data(Qt::UserRole).toString();
	const auto revision = packageViewKey();
	m_packageTree->setCurrentIndex(item);
	QMenu menu(this); menu.setAccessibleName(tr("Package folder actions"));
	auto* create = menu.addAction(tr("New Folder…")); create->setEnabled(m_packageStaging.sourceFormat() != PackageArchiveFormat::Wad);
	auto* rename = menu.addAction(tr("Rename Folder…")); rename->setEnabled(!path.isEmpty());
	auto* remove = menu.addAction(tr("Delete Folder")); remove->setEnabled(!path.isEmpty());
	auto* chosen = menu.exec(m_packageTree->viewport()->mapToGlobal(position));
	if (revision != packageViewKey()) { statusBar()->showMessage(tr("The package changed; choose the folder again.")); return; }
	if (chosen == create) { navigatePackageFolder(path); createPackageDirectory(); }
	else if (chosen == rename || chosen == remove) { stagePackageFolder(chosen == rename, path, revision); }
}

} // namespace vibestudio
