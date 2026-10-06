#include "app/application_shell.h"
#include "app/level_document_dialog.h"
#include "core/package_copy.h"
#include "core/package_copy_store.h"
#include "app/map_viewport.h"
#include "app/ui_primitives.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace vibestudio
{
namespace
{
QString mapWriteIdentity(const QString& path)
{
	if (path.isEmpty()) {
		return {};
	}
	const QFileInfo info(path);
	QString identity = QDir::cleanPath(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath());
#ifdef Q_OS_WIN
	identity = identity.toCaseFolded();
#endif
	return identity;
}
bool isTemporaryPackageCopyPath(const QString& path, const PackageCopySession* copies)
{
	return copies && copies->isValid() && packageCopyStorageContainsPath(copies->path(), path);
}

// Closing requests cancellation but keeps the parent modal until the worker
// acknowledges it. No edit or second save can race the snapshot being saved.
class MapSaveProgress final : public QDialog
{
public:
	using QDialog::QDialog;
	std::function<void()> cancel;
	void reject() override
	{
		if (cancel) {
			cancel();
		}
	}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		reject();
		event->ignore();
	}
};
} // namespace

void ApplicationShell::retireLevelRecovery()
{
	if (m_levelRecoveryWriter) {
		m_levelRecoveryWriter->retire();
	}

}

void ApplicationShell::adoptLevelMapDocument(LevelMapDocument document)
{
	retireLevelRecovery();
	m_levelMapDocument = std::move(document);
	++m_levelMapLoadSerial;
	m_leakTrailPath.clear();
	m_levelMapViewportKey.clear();
	m_levelMapViewportSourceKey.clear();
	if (m_levelMapViewport) {
		m_levelMapViewport->clearDocument();
	}
	if (m_levelMapPath) {
		m_levelMapPath->setText(m_levelMapDocument.sourcePath);
	}
	if (m_levelMapName && m_levelMapDocument.format == LevelMapFormat::DoomWad) {
		m_levelMapName->setEditText(m_levelMapDocument.mapName);
	}
	if (m_levelMapCompilerProfile) {
		const int index = m_levelMapCompilerProfile->findData(compilerRequestForLevelMap(m_levelMapDocument, {}).profileId);
		if (index >= 0) {
			m_levelMapCompilerProfile->setCurrentIndex(index);
		}
	}
	registerWatchedDocument(m_levelMapDocument.sourcePath, DocumentWatchRole::LevelMap, m_levelMapDocument.mapName);
	refreshLevelMapNameChoices();
	refreshLevelMapWorkbench();
	refreshSurfaceStates();
	setMode(StudioMode::Levels);
	if (m_levelViewLayout != LevelViewLayout::Single2D && !levelMap3DShowing()) {
		applyLevelViewLayout(m_levelViewLayout);
	}
}

bool ApplicationShell::createLevelDocument(const LevelMapCreateRequest& request, QString* error)
{
	LevelMapDocument document;
	if (!createLevelMap(request, &document, error)) {
		return false;
	}
	if (!confirmLevelMapEditsHandled(tr("Save before creating another map?"))) {
		return false;
	}
	adoptLevelMapDocument(std::move(document));
	statusBar()->showMessage(tr("Created %1. Choose Save to set its file location.").arg(m_levelMapDocument.mapName));
	return true;
}

void ApplicationShell::newLevelMapFromUi()
{
	NewLevelMapDialog dialog(this);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	QString error;
	if (!createLevelDocument(dialog.request(), &error) && !error.isEmpty()) {
		statusBar()->showMessage(error);
	}
}

bool ApplicationShell::saveLevelDocument(const QString& path, bool overwrite, QString* error)
{
	if (error) {
		error->clear();
	}
	const QString target = mapWriteIdentity(path);
	const auto blocked = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		statusBar()->showMessage(message);
		return false;
	};
	if (isTemporaryPackageCopyPath(path, m_sessionCopies.get())) {
		return blocked(tr("Temporary package copies are removed when the studio closes. Choose a project or another permanent folder."));
	}
	for (const auto& tab : m_codeTabs) {
		if (mapWriteIdentity(tab.path) == target && tab.document && tab.document->isModified()) {
			return blocked(tr("The same file has unsaved Code edits. Save the map to another path or resolve those edits first."));
		}
	}
	const bool sharedPackage = m_packageArchive.isOpen() && mapWriteIdentity(m_packageArchive.sourcePath()) == target;
	if (sharedPackage && (!m_packageStaging.operations().isEmpty() || m_packageSaveRunning || m_packageReadRunning || m_packageExtractionRunning)) {
		return blocked(tr(
			"This WAD has staged changes or an active package operation. Save the map to another path or finish that package work first."));
	}
	// Adopt the save and refresh other clean views before their watcher can
	// mistake our write for an outside edit during the modal event loop.
	const QSignalBlocker watcherBlocker(m_documentWatchTimer);
	m_levelMapState->setState(OperationState::Running, tr("Saving"));
	LevelDocumentSaveRequest request;
	request.path = path;
	request.overwrite = overwrite;
	MapSaveProgress progress(this);
	progress.setWindowTitle(tr("Save Map"));
	progress.setWindowModality(Qt::WindowModal);
	progress.setAccessibleName(progress.windowTitle());
	auto* layout = new QVBoxLayout(&progress);
	auto* label = new QLabel(tr("Saving %1").arg(QDir::toNativeSeparators(path)));
	label->setWordWrap(true);
	layout->addWidget(label);
	auto* bar = new QProgressBar;
	bar->setRange(0, 0);
	bar->setAccessibleName(tr("Saving map"));
	layout->addWidget(bar);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	LevelMapSaveReport report;
	const auto snapshot = m_levelMapDocument;
	request.isCancelled = []() { return QThread::currentThread()->isInterruptionRequested(); };
	auto* worker = QThread::create([snapshot, request, &report]() { report = writeLevelDocument(snapshot, request); });
	progress.cancel = [&]() {
		worker->requestInterruption();
		buttons->setEnabled(false);
		label->setText(tr("Cancelling map save…"));
	};
	connect(buttons, &QDialogButtonBox::rejected, &progress, &QDialog::reject);
	connect(worker, &QThread::finished, &progress, &QDialog::accept);
	worker->start();
	progress.exec();
	worker->wait();
	delete worker;
	if (!report.succeeded()) {
		const QString message = report.errors.join(QStringLiteral("; "));
		if (error) {
			*error = message;
		}
		m_levelMapState->setState(OperationState::Failed, tr("Save failed"));
		m_levelMapState->setDetail(message);
		recordActivity(tr("Level map save failed"), path, QStringLiteral("level-map"), OperationState::Failed, message);
		statusBar()->showMessage(message);
		return false;
	}
	adoptLevelDocumentSave(&m_levelMapDocument, report);
	copyLevelBookmarksAfterSave();
	for (int index = 0; index < m_codeTabs.size(); ++index) {
		if (mapWriteIdentity(m_codeTabs.at(index).path) == target) {
			reloadCodeTab(index);
		}
	}
	if (sharedPackage) {
		loadPackagePath(report.outputPath);
	}
	retireLevelRecovery();
	m_levelMapPath->setText(report.outputPath);
	m_levelMapNamesPath.clear();
	refreshLevelMapNameChoices();
	registerWatchedDocument(report.outputPath, DocumentWatchRole::LevelMap, m_levelMapDocument.mapName);
	m_settings.recordRecentFile(QStringLiteral("map"), report.outputPath);
	recordActivity(tr("Level map saved"), report.outputPath, QStringLiteral("level-map"),
				   report.warnings.isEmpty() ? OperationState::Completed : OperationState::Warning, levelMapSaveReportText(report),
				   report.warnings);
	refreshLevelMapWorkbench();
	refreshSurfaceStates();
	statusBar()->showMessage(m_levelBookmarkError.isEmpty() ? tr("Saved %1").arg(QDir::toNativeSeparators(report.outputPath))
		: tr("Map saved. Saved views need attention: %1").arg(m_levelBookmarkError));
	return true;
}

bool ApplicationShell::saveLevelMapFromUi()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return false;
	}
	const QString path = m_levelMapDocument.sourcePath;
	if (path.isEmpty() || isTemporaryPackageCopyPath(path, m_sessionCopies.get())) {
		return saveLevelMapAsFromUi();
	}
	return saveLevelDocument(path, true);
}

bool ApplicationShell::saveLevelMapAsFromUi()
{
	if (m_levelMapDocument.format == LevelMapFormat::Unknown) {
		return false;
	}
	const QString suffix = m_levelMapDocument.format == LevelMapFormat::DoomWad ? QStringLiteral("wad") : QStringLiteral("map");
	QString directory =
		m_levelMapDocument.sourcePath.isEmpty() ? m_settings.currentProjectPath() : QFileInfo(m_levelMapDocument.sourcePath).absolutePath();
	if (isTemporaryPackageCopyPath(directory, m_sessionCopies.get())) {
		const QString packageDirectory = m_packageArchive.isOpen() && !m_packageArchive.sourcePath().isEmpty()
			? QFileInfo(m_packageArchive.sourcePath()).absolutePath() : QString();
		const QStringList candidates{m_settings.currentProjectPath(), packageDirectory,
			QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), QDir::homePath()};
		directory.clear();
		for (const QString& candidate : candidates) {
			if (!candidate.isEmpty() && QFileInfo(candidate).isDir() && !isTemporaryPackageCopyPath(candidate, m_sessionCopies.get())) {
				directory = QFileInfo(candidate).absoluteFilePath();
				break;
			}
		}
	}
	const QString basename = m_levelMapDocument.sourcePath.isEmpty()
								 ? m_levelMapDocument.mapName
								 : QFileInfo(m_levelMapDocument.sourcePath).completeBaseName() + QStringLiteral("-edited");
	const QString suggested = QDir(directory).filePath(basename + QLatin1Char('.') + suffix);
	const QString output = QFileDialog::getSaveFileName(this, tr("Save Level Map As"), suggested,
														suffix == QStringLiteral("wad") ? tr("Doom WAD (*.wad)") : tr("Level map (*.map)"));
	if (output.isEmpty()) {
		return false;
	}
	// QFileDialog has already confirmed replacement of an existing destination.
	return saveLevelDocument(output, true);
}

void ApplicationShell::checkpointLevelDocument()
{
	if (m_levelRecoveryWriter && m_settings.levelRecoveryEnabled() && m_levelRecoveryWriter->checkpoint(m_levelMapDocument)) {
		statusBar()->showMessage(tr("Saving map recovery checkpoint…"));
	}
}

bool ApplicationShell::recoverLevelDocument(const QString& path, QString* error)
{
	LevelMapDocument document;
	if (!restoreLevelMapRecovery(path, &document, error)) {
		return false;
	}
	if (!confirmLevelMapEditsHandled(tr("Save before restoring a recovery checkpoint?"))) {
		return false;
	}
	adoptLevelMapDocument(std::move(document));

	statusBar()->showMessage(tr("Recovery opened. Review the map and save to keep it."));
	return true;
}

void ApplicationShell::recoverLevelMapFromUi()
{
	const QString path = chooseLevelMapRecovery(this, levelMapRecoveryDirectory());
	if (path.isEmpty()) {
		return;
	}
	QString error;
	if (!recoverLevelDocument(path, &error) && !error.isEmpty()) {
		statusBar()->showMessage(error);
	}
}

} // namespace vibestudio
