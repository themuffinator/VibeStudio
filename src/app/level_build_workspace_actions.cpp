#include "app/application_shell.h"
#include "app/level_build_package_dialog.h"
#include "app/level_build_workspace_dialog.h"
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::preparedLevelBuildCanLaunch() const {
	GameInstallationProfile installation;
	return m_levelBuildWorkspace && selectedGameInstallation(&installation) && installation.active && !installation.hidden &&
		   levelBuildDeploymentInstallationCompatible(*m_levelBuildWorkspace, installation);
}
void ApplicationShell::deployLevelBuildWorkspace(bool launchAfter) {
	if (!preparedLevelBuildWorkspaceActive() || m_buildPipelineThread) {
		return;
	}
	GameInstallationProfile installation;
	if (!selectedGameInstallation(&installation)) {
		statusBar()->showMessage(tr("Add a game installation on the Workspace surface before deployment."));
		return;
	}
	syncLaunchGameDirectory(installation);
	LevelBuildPackageDialog dialog(*m_levelBuildWorkspace, this);
	dialog.configureDeployment(installation, m_launchGameDirectory ? m_launchGameDirectory->text() : QString(), launchAfter);
	dialog.setDeploymentHandler([this](const LevelBuildDeploymentResult& result) {
		const auto detail = levelBuildDeploymentPlanText(result.plan) + QLatin1Char('\n') +
							packageWriteReportText(result.publication.write) + QLatin1Char('\n') + result.error;
		recordActivity(result.launched ? tr("Prepared build deployed and launched") : tr("Prepared build deployment"),
					   result.plan.packagePath, QStringLiteral("build"),
					   result.succeeded() ? OperationState::Completed
					   : result.cancelled ? OperationState::Cancelled
										  : OperationState::Failed,
					   detail, result.warnings);
		statusBar()->showMessage(result.succeeded() ? tr("Prepared map and assets deployed: %1").arg(result.plan.packagePath)
													: result.error);
	});
	dialog.exec();
}
void ApplicationShell::publishLevelBuildWorkspacePackage() {
	if (!preparedLevelBuildWorkspaceActive() || m_buildPipelineThread) {
		return;
	}
	LevelBuildPackageDialog dialog(*m_levelBuildWorkspace, this);
	dialog.setPublishedHandler([this](const LevelBuildPackageResult& result) {
		recordActivity(tr("Prepared build published"), result.write.outputPath, QStringLiteral("build"), OperationState::Completed,
					   packageWriteReportText(result.write), result.write.warnings);
		statusBar()->showMessage(tr("Prepared map and assets published: %1").arg(result.write.outputPath));
	});
	dialog.exec();
}
void ApplicationShell::openLevelBuildWorkspaceAssets() {
	if (!preparedLevelBuildWorkspaceActive() || m_buildPipelineThread) {
		return;
	}
	// Use the normal package-open path, including any pending-draft review.
	// The resulting package view includes captured assets and generated outputs.
	if (!confirmStagedPackageChangesHandled()) {
		return;
	}
	loadPackagePath(m_levelBuildWorkspace->assetsPath());
	setMode(StudioMode::Packages);
}
bool ApplicationShell::preparedLevelBuildWorkspaceActive() const {
	if (!m_levelBuildWorkspace || !m_buildPipelineInput || m_buildPipelineInput->text().trimmed().isEmpty()) {
		return false;
	}
	const auto input = QDir::cleanPath(QFileInfo(m_buildPipelineInput->text().trimmed()).absoluteFilePath());
	const auto prepared = QDir::cleanPath(QFileInfo(m_levelBuildWorkspace->inputPath()).absoluteFilePath());
#ifdef Q_OS_WIN
	return input.compare(prepared, Qt::CaseInsensitive) == 0;
#else
	return input == prepared;
#endif
}
void ApplicationShell::prepareLevelBuildWorkspaceFromUi() {
	if (m_buildPipelineThread ||
		(m_levelMapDocument.format != LevelMapFormat::Quake3Map && m_levelMapDocument.format != LevelMapFormat::QuakeMap)) {
		statusBar()->showMessage(tr("Open a Quake-family map and wait for the current build to finish."));
		return;
	}
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	if (!archive) {
		statusBar()->showMessage(tr("Open the package or folder containing the map assets first."));
		return;
	}
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision, packageRevision = m_packageStaging.revision();
	const auto reload = m_levelPreviewReload;
	const auto source = m_levelMapDocument.sourcePath, output = m_levelMapDocument.outputPath;
	const auto hash = m_levelMapDocument.sourceContentHash;
	LevelBuildWorkspaceDialog dialog(m_levelMapDocument, archive, this);
	auto name = QFileInfo(source).completeBaseName();
	if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,64}$")).match(name).hasMatch()) {
		name = QStringLiteral("studio_build");
	}
	QString parent = m_settings.currentProjectPath();
	if (parent.isEmpty() && !source.isEmpty()) {
		parent = QFileInfo(source).absolutePath();
	}
	const auto destination =
		parent.isEmpty()
			? QString()
			: QDir(parent).filePath(QStringLiteral(".vibestudio/builds/%1-%2")
										.arg(name, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
	dialog.setDestination(destination, name);
	dialog.setApplyHandler(
		[this, load, revision, packageRevision, reload, source, output, hash](const LevelBuildWorkspace& prepared, QString* error) {
			if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || packageRevision != m_packageStaging.revision() ||
				reload != m_levelPreviewReload || source != m_levelMapDocument.sourcePath || output != m_levelMapDocument.outputPath ||
				hash != m_levelMapDocument.sourceContentHash || m_buildPipelineThread) {
				*error = tr("The map or package changed during preparation. Prepare a fresh workspace from the current state.");
				return false;
			}
			m_levelBuildWorkspace = std::make_shared<const LevelBuildWorkspace>(prepared);
			m_buildInputFollowsMap = false;
			m_buildPipelineInput->setText(prepared.inputPath());
			if (m_launchMapName) {
				m_launchMapName->clear();
			}
			if (m_launchProfileChoice) {
				m_launchProfileChoice->setCurrentIndex(m_launchProfileChoice->findData(prepared.target + QStringLiteral("-source-port")));
			}
			if (!prepared.supportsPipeline(m_buildPipelineChoice->currentData().toString())) {
				m_buildPipelineChoice->setCurrentIndex(m_buildPipelineChoice->findData(prepared.defaultPipeline()));
			}
			m_launchAfterBuild = false;
			refreshBuildSurface();
			refreshCommandEnablement();
			setMode(StudioMode::Build);
			recordActivity(tr("Build workspace prepared"), prepared.directory, QStringLiteral("build"), OperationState::Completed,
						   levelBuildWorkspaceText(prepared), prepared.warnings);
			statusBar()->showMessage(tr("Build now uses the captured map and assets. Later edits require a new workspace."));
			return true;
		});
	dialog.exec();
}
} // namespace vibestudio
