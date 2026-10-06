#pragma once

#include "core/level_build_workspace.h"
#include "core/package_staging.h"

namespace vibestudio {
enum class LevelBuildArtifactKind { Unknown, CompiledMap, GeneratedShader, ExternalLightmap, Diagnostic, ExternalLighting };
LevelBuildArtifactKind levelBuildArtifactKind(const QString& workspaceRelativePath, const QString& mapName,
											  const QString& target = QStringLiteral("quake3"));
QString levelBuildArtifactKindId(LevelBuildArtifactKind kind);

struct LevelBuildArtifacts {
	QString workspaceDirectory;
	QString mapName;
	QString target = QStringLiteral("quake3");
	QString runId;
	QString pipelineId;
	QString state;
	QByteArray inputSha256;
	QByteArray recordSha256;
	QVector<LevelBuildInput> outputs;
	QStringList warnings;
	QString error;
	bool verified = false;
	bool cancelled = false;
};

// Verifies both the captured inputs and every output in the last successful
// run. Failed, interrupted, changed and unrecorded outputs cannot be published.
LevelBuildArtifacts inspectLevelBuildArtifacts(const LevelBuildWorkspace& workspace, const PackageReadControl& control = {});
QJsonObject levelBuildArtifactsJson(const LevelBuildArtifacts& artifacts);
QString levelBuildArtifactsText(const LevelBuildArtifacts& artifacts);

struct LevelBuildPackageRequest {
	QString outputPath;
	QByteArray expectedRecordSha256; // GUI review token; optional for direct CLI use.
	bool includeSourceMap = false;
	bool allowOverwrite = false;
	bool dryRun = false;
	DeflateLevel compression = DeflateLevel::Default;
	std::optional<QString> expectedDestinationSha256;
};
struct LevelBuildPackageResult {
	LevelBuildArtifacts artifacts;
	PackageWriteReport write;
	QStringList paths;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && write.succeeded(); }
};
// Uses the ordinary deterministic package writer and atomic publication service.
// The package includes captured assets and runtime outputs; diagnostics are kept
// in the workspace. Sources are checked per chunk while the archive is written.
LevelBuildPackageResult publishLevelBuildPackage(const LevelBuildWorkspace& workspace, const LevelBuildPackageRequest& request,
												 const PackageReadControl& control = {});
QJsonObject levelBuildPackageJson(const LevelBuildPackageResult& result);
} // namespace vibestudio
