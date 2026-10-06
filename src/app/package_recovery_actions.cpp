#include "app/application_shell.h"
#include "app/package_operation_dialog.h"
#include "app/package_recovery_dialog.h"
#include "app/package_recovery_writer.h"
#include "core/package_publication.h"
#include "app/studio_actions.h"

#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QTimer>
#include <QUuid>

namespace vibestudio {

void ApplicationShell::initializePackageRecovery()
{
	m_packageRecoveryWriter = new PackageRecoveryWriter(packageRecoveryDirectory(), this);
	m_packageRecoveryWriter->finished = [this](const QString& id, quint64 revision, const PackageRecoveryWriteResult& result) {
		if (id == m_packageRecoveryId) {
			if (result.succeeded()) { m_packageRecoveryRevision = revision; m_packageRecoverySaved = QDateTime::currentDateTime(); }
			QString error = result.error.isEmpty() ? result.maintenanceError : result.error;
			if (result.succeeded() && result.unavailableBaseCount > 0) {
				if (!error.isEmpty()) { error += QLatin1Char('\n'); }
				error += tr("Recovery saved with %1 unavailable original entries. Repaired content is preserved; Undo may expose missing bytes.").arg(result.unavailableBaseCount);
			}
			if (!error.isEmpty() && error != m_packageRecoveryError) {
				recordActivity(tr("Package recovery needs attention"), packageOpenPath(), QStringLiteral("package"), OperationState::Warning, error);
			}
			m_packageRecoveryError = error; refreshPackageRecoveryStatus();
		} else if (!result.error.isEmpty()) {
			recordActivity(tr("Package recovery cleanup failed"), packageRecoveryPath(packageRecoveryDirectory(), id), QStringLiteral("package"), OperationState::Warning, result.error);
		}
	};
	m_packageRecoveryTimer = new QTimer(this);
	m_packageRecoveryTimer->setObjectName(QStringLiteral("packageRecoveryTimer"));
	m_packageRecoveryTimer->setInterval(m_settings.packageRecoveryIntervalSeconds() * 1000);
	m_packageRecoveryTimer->setTimerType(Qt::CoarseTimer);
	connect(m_packageRecoveryTimer, &QTimer::timeout, this, &ApplicationShell::checkpointPackageDocument);
	m_packageRecoveryTimer->start();
	auto* progress = new QTimer(this); progress->setInterval(250);
	connect(progress, &QTimer::timeout, this, [this]() { if (m_packageRecoveryWriter->busy()) { refreshPackageRecoveryStatus(); } });
	progress->start(); refreshPackageRecoveryStatus();
	QDirIterator copies(packageRecoveryDirectory(), {QStringLiteral("*.vibepackage")}, QDir::Dirs | QDir::NoDotAndDotDot);
	if (copies.hasNext()) {
		QTimer::singleShot(0, this, [this]() { statusBar()->showMessage(tr("Package recovery copies are available in File > Recover Packages.")); });
	}
}

void ApplicationShell::checkpointPackageDocument()
{
	if (!m_packageRecoveryWriter || m_packageRecoveryClosing || !m_settings.packageRecoveryEnabled()
		|| !m_packageStaging.isLoaded() || !m_packageStaging.isModified()
		|| m_packageSaveRunning || m_packageReadRunning || m_packageExtractionRunning) { return; }
	if (m_packageRecoveryId.isEmpty()) { m_packageRecoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces); }
	QString title = QFileInfo(packageOpenPath()).fileName();
	if (title.isEmpty()) { title = tr("Untitled %1 package").arg(packageArchiveFormatId(m_packageStaging.sourceFormat()).toUpper()); }
	m_packageRecoveryWriter->checkpoint(m_packageRecoveryId, m_packageStaging, title,
		{static_cast<qint64>(m_settings.packageRecoveryMaximumMiB()) * 1024 * 1024, m_settings.packageRecoveryMaximumCopies()});
	refreshPackageRecoveryStatus();
}

void ApplicationShell::retirePackageRecovery()
{
	if (m_packageRecoveryWriter && !m_packageRecoveryId.isEmpty()) { m_packageRecoveryWriter->retire(m_packageRecoveryId); }
	m_packageRecoveryId.clear(); m_packageRecoveryError.clear(); m_packageRecoverySaved = {}; m_packageRecoveryRevision = 0;
}

void ApplicationShell::refreshPackageRecoveryStatus()
{
	if (!m_packageRecoveryLabel) { return; }
	QString status;
	if (!m_settings.packageRecoveryEnabled()) { status = tr("Recovery: automatic checkpoints off"); }
	else if (!m_packageStaging.isLoaded()) { status = tr("Recovery: no package open"); }
	else if (!m_packageRecoveryError.isEmpty()) { status = tr("Recovery needs attention: %1").arg(m_packageRecoveryError); }
	else if (!m_packageStaging.isModified()) { status = tr("Recovery: no unsaved changes"); }
	else if (m_packageRecoveryWriter && m_packageRecoveryWriter->busy() && !m_packageRecoveryId.isEmpty()) {
		status = tr("Recovery: saving checkpoint (%1% of current file)").arg(m_packageRecoveryWriter->progress() / 10);
	} else if (m_packageRecoverySaved.isValid() && m_packageRecoveryRevision == m_packageStaging.revision()) {
		status = tr("Recovery: checkpoint saved at %1").arg(locale().toString(m_packageRecoverySaved.time(), QLocale::ShortFormat));
	} else { status = tr("Recovery: waiting for the next checkpoint"); }
	if (m_packageRecoveryLabel->text() != status) { m_packageRecoveryLabel->setText(status); }
	m_packageRecoveryLabel->setToolTip(tr("Local checkpoints every %1 seconds. Review copies and settings in File > Recover Packages.\n%2")
		.arg(m_settings.packageRecoveryIntervalSeconds()).arg(QDir::toNativeSeparators(packageRecoveryDirectory())));
}

void ApplicationShell::recoverPackagesFromUi()
{
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning) { return; }
	if (m_packageRecoveryDialog) { m_packageRecoveryDialog->show(); m_packageRecoveryDialog->raise(); return; }
	QStringList roots;
	if (!packageOpenPath().isEmpty()) { roots << QFileInfo(packageOpenPath()).absolutePath(); }
	if (!m_settings.currentProjectPath().isEmpty()) { roots << m_settings.currentProjectPath(); }
	for (const auto& path : m_settings.recentFiles(QStringLiteral("package"))) { roots << QFileInfo(path).absolutePath(); }
	roots.removeDuplicates();
	auto* dialog = new PackageRecoveryDialog(packageRecoveryDirectory(), this, roots); m_packageRecoveryDialog = dialog;
	dialog->openPublicationOutput = [this](const QString& path) {
		if (!confirmStagedPackageChangesHandled()) { return; } loadPackagePath(path);
	};
	dialog->publicationFinished = [this](const PackageRecoveryReport& report) {
		recordActivity(report.finished ? tr("Package save recovery completed") : tr("Package save recovery needs attention"),
			report.destinationPath, QStringLiteral("package"), report.finished ? OperationState::Completed : OperationState::Warning,
			report.finished ? tr("Output verified; transaction files removed.") : report.error);
	};
	dialog->preferencesChanged = [this]() {
		m_packageRecoveryTimer->setInterval(m_settings.packageRecoveryIntervalSeconds() * 1000); refreshPackageRecoveryStatus();
	};
	dialog->restore = [this](const PackageRecoveryInfo& info) {
		const QString directory = m_settings.currentProjectPath().isEmpty() ? QDir::currentPath() : m_settings.currentProjectPath();
		QString destination = QFileDialog::getSaveFileName(this, tr("Restore Package to New Draft"),
			QDir(directory).filePath(QStringLiteral("recovered.vibepackage")), tr("Package Draft Directory (*.vibepackage)"), nullptr, QFileDialog::DontConfirmOverwrite);
		if (destination.isEmpty()) { return; }
		if (!destination.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive)) { destination += QStringLiteral(".vibepackage"); }
		QString error;
		if (!recoverPackageDocument(packageRecoveryDirectory(), info.id, info.manifestSha256, destination, &error) && !error.isEmpty()) {
			QMessageBox::warning(this, tr("Package Recovery"), error);
		}
	};
	dialog->show();
}

bool ApplicationShell::recoverPackageDocument(const QString& directory, const QString& id, const QByteArray& manifestSha256,
	const QString& destination, QString* error)
{
	if (error) { error->clear(); }
	if (m_packageReadRunning || m_packageSaveRunning || m_packageExtractionRunning || !confirmStagedPackageChangesHandled()) { return false; }
	PackageLoadResult result;
	{
		QScopedValueRollback<bool> reading(m_packageReadRunning, true); refreshCommandEnablement();
		result = runPackageRecoveryRestoreDialog(this, directory, id, manifestSha256, destination);
	}
	if (!result.ready()) {
		if (error) { *error = result.cancelled ? QString() : result.error; }
		refreshCommandEnablement(); statusBar()->showMessage(result.cancelled ? tr("Package recovery cancelled.") : result.error); return false;
	}
	retirePackageRecovery(); unloadAudioPlayback(); m_audioShownPath.clear();
	if (m_quickOpen && m_quickOpen->isVisible()) { m_quickOpen->reject(); }
	m_packageStaging = std::move(result.staging); m_packageArchive = std::move(result.archive);
	++m_levelPreviewReload; m_levelPackagePreviewKey.clear();
	m_packageBrowseSource = m_packageStaging.draftPath(); m_packageBrowseFolder.clear();
	m_packageFolderHistory = {QString()}; m_packageFolderHistoryIndex = 0;
	if (m_packageFilter) { m_packageFilter->clear(); }
	registerWatchedDocument(packageWatchPath(), DocumentWatchRole::Package);
	m_settings.recordRecentFile(QStringLiteral("package"), m_packageStaging.draftPath());
	refreshPackageBrowser(); refreshWorkspaceDashboard(); setMode(StudioMode::Packages);
	recordActivity(tr("Package Recovered"), QDir::toNativeSeparators(m_packageStaging.draftPath()), QStringLiteral("package"), OperationState::Completed,
		tr("Content and edit history restored to an independent draft. The recovery copy is still available."));
	statusBar()->showMessage(tr("Package recovered to %1").arg(QDir::toNativeSeparators(m_packageStaging.draftPath())));
	return true;
}

} // namespace vibestudio
