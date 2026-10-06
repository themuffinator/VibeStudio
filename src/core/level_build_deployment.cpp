#include "core/level_build_deployment.h"
#include "core/package_storage.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>

namespace vibestudio {
namespace {
QString text(const char* source) { return QCoreApplication::translate("LevelBuildDeployment", source); }
bool stopped(const PackageReadControl& control) { return control.isCancelled && control.isCancelled(); }
bool samePath(const QString& a, const QString& b) {
	if (a.isEmpty() || b.isEmpty()) {
		return false;
	}
	const QFileInfo left(a), right(b);
	const auto x = left.exists() ? left.canonicalFilePath() : left.absoluteFilePath();
	const auto y = right.exists() ? right.canonicalFilePath() : right.absoluteFilePath();
	return QDir::cleanPath(x).compare(QDir::cleanPath(y), Qt::CaseInsensitive) == 0;
}
QStringList protectedPackages(const GameInstallationProfile& installation) {
	QStringList paths;
	for (const auto& value : installation.basePackagePaths + installation.modPackagePaths) {
		paths << normalizedInstallationPath(value, installation.rootPath);
	}
	return paths;
}
bool targetAllowed(const LevelBuildWorkspace& workspace, const GameInstallationProfile& installation, const QString& path, QString* error) {
	if (!safePackageStoragePath(path, error) || !packagePathIsInsideDirectory(installation.rootPath, path) ||
		packagePathIsInsideDirectory(workspace.directory, path) ||
		(!workspace.sourcePackagePath.isEmpty() && packagePathIsInsideDirectory(workspace.sourcePackagePath, path)) ||
		samePath(path, workspace.sourceMapPath) || samePath(path, installation.executablePath)) {
		if (error->isEmpty()) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The deployment target overlaps protected source content."));
		}
		return false;
	}
	for (const auto& protectedPath : protectedPackages(installation)) {
		if (samePath(path, protectedPath)) {
			*error = text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Deployment must not replace a registered installation package: %1"))
						 .arg(path);
			return false;
		}
	}
	return true;
}
} // namespace

bool levelBuildDeploymentInstallationCompatible(const LevelBuildWorkspace& workspace, const GameInstallationProfile& installation) {
	const bool classic = workspace.target == QStringLiteral("quake") || workspace.target == QStringLiteral("quake2");
	if (!classic && workspace.target != QStringLiteral("quake3")) {
		return false;
	}
	const auto key = normalizedGameKey(installation.gameKey);
	return installation.engineFamily == (classic ? GameEngineFamily::IdTech2 : GameEngineFamily::IdTech3) &&
		   (key == workspace.target || key == QStringLiteral("custom"));
}
GameLaunchPlan levelBuildDeploymentLaunchPlan(const LevelBuildWorkspace& workspace, const GameInstallationProfile& installation,
											  const QString& gameDirectory) {
	if (!levelBuildDeploymentInstallationCompatible(workspace, installation)) {
		GameLaunchPlan plan;
		plan.errors << text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Choose an installation matching this prepared build target: %1."))
						   .arg(workspace.target);
		return plan;
	}
	GameLaunchRequest launch;
	launch.launchProfileId = workspace.target + QStringLiteral("-source-port");
	launch.mapName = workspace.mapName;
	launch.modDirectory = gameDirectory.trimmed().isEmpty() ? defaultGameDirectory(installation) : gameDirectory.trimmed();
	launch.baseDirectory = installation.rootPath;
	launch.workingDirectory = installation.rootPath;
	// Interface facts: id Software Quake III (GPL-2.0-or-later), files.c
	// FS_Startup, common.c Com_StartupVariable, renderer/tr_init.c r_fullscreen.
	// Reviewed 2026-10-05; upstream links and compatibility are in docs/CREDITS.md.
	// Pin home lookup to the reviewed installation and request a windowed devmap.
	if (workspace.target == QStringLiteral("quake3")) {
		launch.extraArguments = {"+set", "fs_homepath", installation.rootPath, "+set", "sv_pure", "0", "+set", "r_fullscreen", "0"};
	} else if (workspace.target == QStringLiteral("quake2")) {
		// Quake-2 win32/vid_dll.c: archived vid_fullscreen cvar; +set is applied
		// before video startup. GPL-2.0-or-later interface, reviewed 2026-10-05.
		launch.extraArguments = {"+set", "vid_fullscreen", "0"};
	} else {
		// Quake WinQuake/gl_vidnt.c VID_Init accepts -window. See docs/CREDITS.md.
		launch.extraArguments = {"-window"};
	}
	return buildGameLaunchPlan(launch, installation);
}
LevelBuildDeploymentPlan planLevelBuildDeployment(const LevelBuildWorkspace& workspace, const GameInstallationProfile& installation,
												  const QString& gameDirectory, const PackageReadControl& control, int pakSlot) {
	LevelBuildDeploymentPlan plan;
	plan.installation = installation;
	const auto fail = [&](const QString& error) {
		plan.error = error;
		plan.cancelled = stopped(control);
		return plan;
	};
	if (stopped(control)) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Deployment review cancelled.")));
	}
	if (!levelBuildDeploymentInstallationCompatible(workspace, installation)) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Choose an installation matching this prepared build target: %1."))
						.arg(workspace.target));
	}
	if (workspace.target == QStringLiteral("quake3") && pakSlot != -1) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "PAK slots apply only to Quake and Quake II builds.")));
	}
	if (!installation.active || installation.hidden || installation.rootPath.trimmed().isEmpty() ||
		!QFileInfo(installation.rootPath).isDir()) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Choose an active installation with an existing root folder.")));
	}
	plan.installation.rootPath = QDir::cleanPath(QFileInfo(installation.rootPath).absoluteFilePath());
	plan.gameDirectory = gameDirectory.trimmed().isEmpty() ? defaultGameDirectory(installation) : gameDirectory.trimmed();
	if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$")).match(plan.gameDirectory).hasMatch() ||
		plan.gameDirectory.contains("..") || packageFilesystemPathIssue(plan.gameDirectory) != PackagePathIssue::None) {
		return fail(
			text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The game folder must be one portable folder name inside the installation.")));
	}
	plan.artifacts = inspectLevelBuildArtifacts(workspace, control);
	if (!plan.artifacts.verified) {
		return fail(plan.artifacts.error);
	}
	const auto directory = QDir(plan.installation.rootPath).filePath(plan.gameDirectory);
	if (workspace.target == QStringLiteral("quake3")) {
		plan.packagePath = QDir(directory).filePath(QStringLiteral("vibestudio_%1.pk3").arg(workspace.mapName));
	} else {
		plan.pakSlot = planLevelBuildPakSlot(directory, workspace.target, workspace.mapName, pakSlot, control);
		if (!plan.pakSlot.ready()) {
			return fail(plan.pakSlot.error);
		}
		plan.packagePath = plan.pakSlot.packagePath;
		plan.warnings += plan.pakSlot.warnings;
		if (!targetAllowed(workspace, plan.installation, plan.pakSlot.receiptPath, &plan.error) ||
			!targetAllowed(workspace, plan.installation, QDir(directory).filePath(".vibestudio-deployment.lock"), &plan.error)) {
			return fail(plan.error);
		}
	}
	plan.backupPath = plan.packagePath + QStringLiteral(".bak");
	if (!targetAllowed(workspace, plan.installation, plan.packagePath, &plan.error) ||
		!targetAllowed(workspace, plan.installation, plan.backupPath, &plan.error)) {
		return fail(plan.error);
	}
	if (QFileInfo(directory).exists() && !QFileInfo(directory).isDir()) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The chosen game folder is occupied by a file.")));
	}
	plan.packageExists = QFileInfo::exists(plan.packagePath);
	if (plan.packageExists) {
		const auto identity = capturePackageFileIdentity(plan.packagePath, &plan.error, control);
		if (!identity) {
			return fail(plan.error);
		}
		plan.existingPackageSha256 = QString::fromLatin1(identity->sha256.toHex());
		plan.existingPackageBytes = quint64(identity->size);
	}
	if (QDir(directory).entryList({"*.pk3", "*.PK3"}, QDir::Files).size() > (plan.packageExists ? 1 : 0) ||
		QFileInfo(QDir(directory).filePath(QStringLiteral("maps/%1.bsp").arg(workspace.mapName))).exists()) {
		plan.warnings << text(QT_TRANSLATE_NOOP("LevelBuildDeployment",
												"This game folder also contains other packages or a loose copy of this map. Engine "
												"search precedence can affect which assets load."));
	}
	plan.launch = levelBuildDeploymentLaunchPlan(workspace, plan.installation, plan.gameDirectory);
	if (plan.launch.runnable) {
		plan.executableIdentity = capturePackageFileIdentity(plan.launch.program, &plan.error, control);
		if (!plan.executableIdentity) {
			return fail(plan.error);
		}
	}
	plan.ready = !stopped(control);
	if (!plan.ready) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Deployment review cancelled.")));
	}
	return plan;
}

LevelBuildDeploymentResult deployLevelBuild(const LevelBuildWorkspace& workspace, const LevelBuildDeploymentPlan& reviewed,
											const LevelBuildDeploymentOptions& options, const PackageReadControl& control) {
	LevelBuildDeploymentResult result;
	result.plan = reviewed;
	const auto fail = [&](const QString& error) {
		result.error = error;
		result.cancelled = stopped(control);
		return result;
	};
	if (!reviewed.ready || !reviewed.artifacts.verified) {
		return fail(
			text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Review a successful prepared build and installation before deployment.")));
	}
	if (!options.dryRun && reviewed.installation.readOnly && !options.allowReadOnlyWrite) {
		return fail(
			text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "This installation is read-only. Allow this asset deployment explicitly.")));
	}
	const auto current =
		planLevelBuildDeployment(workspace, reviewed.installation, reviewed.gameDirectory, control, reviewed.pakSlot.requested);
	if (!current.ready) {
		return fail(current.error);
	}
	if (current.packagePath != reviewed.packagePath || current.packageExists != reviewed.packageExists ||
		current.pakSlot.receiptSha256 != reviewed.pakSlot.receiptSha256 || current.pakSlot.layoutSha256 != reviewed.pakSlot.layoutSha256 ||
		current.existingPackageSha256 != reviewed.existingPackageSha256 ||
		current.artifacts.recordSha256 != reviewed.artifacts.recordSha256 ||
		(options.launch &&
		 (!current.launch.runnable || current.launch.commandLine != reviewed.launch.commandLine || !reviewed.executableIdentity ||
		  !current.executableIdentity || current.executableIdentity->sha256 != reviewed.executableIdentity->sha256))) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment",
										   "The reviewed build, destination or launch executable changed. Review deployment again.")));
	}
	if (current.packageExists && !options.allowOverwrite) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The deployment package exists. Enable replacement with a backup.")));
	}
	LevelBuildPackageRequest publication;
	publication.outputPath = current.packagePath;
	publication.expectedRecordSha256 = reviewed.artifacts.recordSha256;
	publication.expectedDestinationSha256 = reviewed.existingPackageSha256;
	publication.allowOverwrite = options.allowOverwrite;
	publication.includeSourceMap = options.includeSourceMap;
	publication.compression = options.compression;
	publication.dryRun = options.dryRun;
	if (stopped(control)) {
		return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Deployment cancelled before publication.")));
	}
	const auto directory = QFileInfo(current.packagePath).absolutePath();
	bool createdDirectory = false;
	if (!options.dryRun && !QFileInfo::exists(directory)) {
		QString error;
		if (!targetAllowed(workspace, current.installation, current.packagePath, &error) || !QDir().mkdir(directory)) {
			return fail(error.isEmpty() ? text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Could not create the reviewed game folder."))
										: error);
		}
		createdDirectory = true;
	}
	std::unique_ptr<QLockFile> deploymentLock;
	if (!options.dryRun && current.pakSlot.ready()) {
		const auto lockPath = QDir(directory).filePath(".vibestudio-deployment.lock");
		QString error;
		if (!targetAllowed(workspace, current.installation, lockPath, &error)) {
			return fail(error);
		}
		deploymentLock = std::make_unique<QLockFile>(lockPath);
		deploymentLock->setStaleLockTime(0);
		if (!deploymentLock->tryLock()) {
			return fail(text(
				QT_TRANSLATE_NOOP("LevelBuildDeployment", "Another deployment is using this game folder. Try again after it finishes.")));
		}
		const auto locked =
			planLevelBuildDeployment(workspace, reviewed.installation, reviewed.gameDirectory, control, reviewed.pakSlot.requested);
		if (!locked.ready || locked.packagePath != current.packagePath || locked.existingPackageSha256 != current.existingPackageSha256 ||
			locked.pakSlot.receiptSha256 != current.pakSlot.receiptSha256 || locked.pakSlot.layoutSha256 != current.pakSlot.layoutSha256) {
			return fail(
				text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The PAK slot changed while acquiring the deployment lock. Review again.")));
		}
	}
	result.publication = publishLevelBuildPackage(workspace, publication, control);
	if (!result.publication.succeeded()) {
		// Only this operation's still-empty new directory may be removed.
		deploymentLock.reset();
		if (createdDirectory && safePackageStoragePath(directory)) {
			QDir().rmdir(directory);
		}
		return fail(result.publication.error);
	}
	result.warnings = current.warnings + result.publication.write.warnings;
	if (!options.dryRun && current.pakSlot.ready()) {
		QString error;
		if (!targetAllowed(workspace, current.installation, current.pakSlot.receiptPath, &error) ||
			!saveLevelBuildPakReceipt(current.pakSlot, workspace.target, workspace.mapName, result.publication.write.sha256, &error)) {
			result.warnings << text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The PAK was deployed, but its slot receipt could not be "
																			  "saved: %1. Inspect the receipt before the next deployment."))
								   .arg(error);
		}
	}
	if (options.launch && !options.dryRun) {
		if (stopped(control)) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The package was deployed; launch was cancelled.")));
		}
		QString error;
		if (current.pakSlot.ready()) {
			const auto slot = planLevelBuildPakSlot(directory, workspace.target, workspace.mapName, current.pakSlot.number, control);
			if (!slot.ready() || slot.layoutSha256 != current.pakSlot.layoutSha256) {
				return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment",
												   "The PAK was deployed, but the game folder changed before launch. Review again.")));
			}
		}
		const auto deployed = capturePackageFileIdentity(current.packagePath, &error, control);
		if (!deployed || QString::fromLatin1(deployed->sha256.toHex()) != result.publication.write.sha256 ||
			!verifyPackageFileIdentity(reviewed.executableIdentity, &error, control)) {
			return fail(error.isEmpty()
							? text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The deployed package or executable changed before launch."))
							: error);
		}
		if (stopped(control)) {
			return fail(text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "The package was deployed; launch was cancelled.")));
		}
		if (!startGameLaunch(current.launch, &result.processId, &error)) {
			return fail(error);
		}
		result.launched = true;
	}
	return result;
}
QString levelBuildDeploymentReviewSha256(const LevelBuildDeploymentPlan& plan) {
	if (!plan.ready) {
		return {};
	}
	const QJsonArray token{
		plan.installation.id,		plan.installation.readOnly,
		plan.installation.rootPath, plan.gameDirectory,
		plan.packagePath,			plan.packageExists,
		plan.existingPackageSha256, QString::fromLatin1(plan.artifacts.recordSha256.toHex()),
		plan.pakSlot.receiptSha256, QString::fromLatin1(plan.pakSlot.layoutSha256.toHex()),
		plan.launch.commandLine,	plan.executableIdentity ? QString::fromLatin1(plan.executableIdentity->sha256.toHex()) : QString()};
	return QString::fromLatin1(
		QCryptographicHash::hash(QJsonDocument(token).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QJsonObject levelBuildDeploymentPlanJson(const LevelBuildDeploymentPlan& plan) {
	return {{"installationId", plan.installation.id},
			{"installationName", plan.installation.displayName},
			{"installationRoot", plan.installation.rootPath},
			{"readOnly", plan.installation.readOnly},
			{"gameDirectory", plan.gameDirectory},
			{"packagePath", plan.packagePath},
			{"reviewSha256", levelBuildDeploymentReviewSha256(plan)},
			{"backupPath", plan.backupPath},
			{"requestedPakSlot", plan.pakSlot.requested},
			{"pakSlot", plan.pakSlot.number},
			{"pakReceiptPath", plan.pakSlot.receiptPath},
			{"pakReceiptSha256", plan.pakSlot.receiptSha256},
			{"pakLayoutSha256", QString::fromLatin1(plan.pakSlot.layoutSha256.toHex())},
			{"packageExists", plan.packageExists},
			{"existingPackageSha256", plan.existingPackageSha256},
			{"existingPackageBytes", qint64(plan.existingPackageBytes)},
			{"artifacts", levelBuildArtifactsJson(plan.artifacts)},
			{"launch", gameLaunchPlanJson(plan.launch)},
			{"warnings", QJsonArray::fromStringList(plan.warnings)},
			{"ready", plan.ready},
			{"cancelled", plan.cancelled},
			{"error", plan.error}};
}
QJsonObject levelBuildDeploymentJson(const LevelBuildDeploymentResult& result) {
	return {{"plan", levelBuildDeploymentPlanJson(result.plan)},
			{"publication", levelBuildPackageJson(result.publication)},
			{"launched", result.launched},
			{"processId", result.processId},
			{"cancelled", result.cancelled},
			{"error", result.error},
			{"succeeded", result.succeeded()},
			{"warnings", QJsonArray::fromStringList(result.warnings)}};
}
QString levelBuildDeploymentPlanText(const LevelBuildDeploymentPlan& plan) {
	if (!plan.error.isEmpty()) {
		return plan.error;
	}
	QStringList lines{plan.installation.displayName, plan.packagePath,
					  plan.packageExists
						  ? text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Existing package: %1 bytes; SHA-256: %2"))
								.arg(plan.existingPackageBytes)
								.arg(plan.existingPackageSha256)
						  : text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "New package; no existing file will be replaced."))};
	if (plan.installation.readOnly) {
		lines << text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "Installation writes require permission for this deployment."));
	}
	if (plan.pakSlot.ready()) {
		lines << text(QT_TRANSLATE_NOOP("LevelBuildDeployment", "PAK slot %1; deployment receipt: %2"))
					 .arg(plan.pakSlot.number)
					 .arg(plan.pakSlot.receiptPath);
	}
	lines << plan.launch.commandLine << plan.launch.errors << plan.warnings;
	return lines.join(QLatin1Char('\n'));
}
} // namespace vibestudio
