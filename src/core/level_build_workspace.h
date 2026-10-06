#pragma once

#include "core/build_pipeline.h"
#include "core/level_dependencies.h"

namespace vibestudio {
QString levelBuildTargetForDocument(const LevelMapDocument& document);

struct LevelBuildWorkspaceRequest {
	QString directory;
	QString mapName = QStringLiteral("studio_build");
	QString target; // Empty chooses quake for QuakeMap, quake3 for Quake3Map.
	bool dryRun = false;
	quint64 maximumBytes = 4ULL * 1024 * 1024 * 1024;
};

struct LevelBuildInput {
	QString path; // Portable path relative to the workspace.
	quint64 bytes = 0;
	QByteArray sha256;
};

struct LevelBuildWorkspace {
	QString directory;
	QString mapName;
	QString target = QStringLiteral("quake3");
	QString sourceMapPath;
	QString sourcePackagePath;
	quint64 sourceRevision = 0;
	QByteArray sourceContentHash;
	QVector<LevelBuildInput> inputs;
	LevelDependencyReport dependencies;
	QStringList omittedPaths;
	QStringList warnings;
	QString error;
	bool ready = false;
	bool dryRun = false;
	bool cancelled = false;
	[[nodiscard]] QString inputPath() const;
	[[nodiscard]] QString assetPrefix() const;
	[[nodiscard]] QString defaultPipeline() const;
	[[nodiscard]] QString packageSuffix() const;
	[[nodiscard]] QString textureWadPath() const;
	[[nodiscard]] bool supportsPipeline(const QString& pipeline) const;
	[[nodiscard]] QString assetsPath() const;
	[[nodiscard]] QString manifestPath() const;
};

// Captures the current Quake-family map and the complete immutable package reader
// as independent files. A new directory is published only after every input has
// been verified. Never saves the live map or changes the package draft.
// Progress/cancellation run on the calling worker. Dry runs read/hash without
// creating files. Quake generates a captured WAD2; Doom node builds remain separate.
LevelBuildWorkspace prepareLevelBuildWorkspace(const LevelMapDocument& document, const PackageArchiveReader& archive,
											   const LevelBuildWorkspaceRequest& request, const PackageReadControl& control = {});
LevelBuildWorkspace readLevelBuildWorkspace(const QString& directory, const PackageReadControl& control = {});
bool verifyLevelBuildWorkspace(const LevelBuildWorkspace& workspace, QString* error = nullptr, const PackageReadControl& control = {});
bool configureLevelBuildPipeline(const LevelBuildWorkspace& workspace, BuildPipelineRequest* request, QString* error = nullptr);
BuildPipelineResult runLevelBuildWorkspace(const LevelBuildWorkspace& workspace, BuildPipelineRequest request,
										   const BuildPipelineCallbacks& callbacks = {});
QJsonObject levelBuildWorkspaceJson(const LevelBuildWorkspace& workspace);
QString levelBuildWorkspaceText(const LevelBuildWorkspace& workspace);

} // namespace vibestudio
