#pragma once
#include "core/level_build_artifacts.h"
#include "core/level_build_pak_deployment.h"

namespace vibestudio {
struct LevelBuildDeploymentPlan {
	GameInstallationProfile installation;
	QString gameDirectory, packagePath, backupPath;
	QString existingPackageSha256;
	quint64 existingPackageBytes = 0;
	bool packageExists = false;
	LevelBuildArtifacts artifacts;
	GameLaunchPlan launch;
	PackageFileIdentityPtr executableIdentity;
	LevelBuildPakSlot pakSlot;
	QStringList warnings;
	QString error;
	bool ready = false, cancelled = false;
};
struct LevelBuildDeploymentOptions {
	bool allowReadOnlyWrite = false; // One operation only; never changes profile settings.
	bool allowOverwrite = false;
	bool includeSourceMap = false;
	bool dryRun = false;
	bool launch = false;
	DeflateLevel compression = DeflateLevel::Default;
};
struct LevelBuildDeploymentResult {
	LevelBuildDeploymentPlan plan;
	LevelBuildPackageResult publication;
	QString error;
	QStringList warnings;
	qint64 processId = 0;
	bool launched = false, cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && publication.succeeded(); }
};
// Plans are read-only, even for a read-only installation. Publication separately
// requires the profile's write permission or explicit one-operation consent.
LevelBuildDeploymentPlan planLevelBuildDeployment(const LevelBuildWorkspace& workspace, const GameInstallationProfile& installation,
												  const QString& gameDirectory = {}, const PackageReadControl& control = {},
												  int pakSlot = -1);
bool levelBuildDeploymentInstallationCompatible(const LevelBuildWorkspace& workspace, const GameInstallationProfile& installation);
// Metadata-only preview for the main Build surface; full review verifies bytes.
GameLaunchPlan levelBuildDeploymentLaunchPlan(const LevelBuildWorkspace& workspace, const GameInstallationProfile& installation,
											  const QString& gameDirectory = {});
// Rechecks the reviewed build, destination and paths before atomic package
// publication. Launch is explicit, windowed, and only follows verified output.
LevelBuildDeploymentResult deployLevelBuild(const LevelBuildWorkspace& workspace, const LevelBuildDeploymentPlan& reviewed,
											const LevelBuildDeploymentOptions& options, const PackageReadControl& control = {});
QJsonObject levelBuildDeploymentPlanJson(const LevelBuildDeploymentPlan& plan);
QString levelBuildDeploymentReviewSha256(const LevelBuildDeploymentPlan& plan);
QJsonObject levelBuildDeploymentJson(const LevelBuildDeploymentResult& result);
QString levelBuildDeploymentPlanText(const LevelBuildDeploymentPlan& plan);
} // namespace vibestudio
