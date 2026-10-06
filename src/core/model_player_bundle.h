#pragma once

#include "core/level_dependencies.h"
#include "core/model_assembly.h"
#include "core/package_staging.h"

namespace vibestudio
{
struct ModelPlayerBundleOptions
{
	QString modelName, skinName = QStringLiteral("default"), headPart;
	QString iconSource;
	ModelAssemblySource iconKind = ModelAssemblySource::File;
	int iconEntryIndex = -1;
};
struct ModelPlayerBundleFile
{
	QString path, role;
	QByteArray bytes, sha256;
};
struct ModelPlayerBundle
{
	ModelPlayerBundleOptions options;
	QVector<ModelPlayerBundleFile> files;
	LevelDependencyReport dependencies;
	QJsonObject receipt;
	QStringList notes, protectedPaths;
	std::shared_ptr<const PackageArchiveReader> sourceArchive;
	// Optional caller revision for the immutable package context; never serialized.
	QString contextRevision;
	QByteArray recipeSha256;
	qint64 totalBytes = 0;
};

// Original Quake III player-loader layout; see docs/CREDITS.md. Native frames,
// tags and shader stages remain separate, unlike a composed animation bake.
// All content is owned by the reviewed candidate. Failure never replaces it.
bool prepareModelPlayerBundle(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved, const ModelAssemblyContext &context,
							  const ModelPlayerBundleOptions &options, const QString &sourcePath, ModelPlayerBundle *result,
							  QString *error = nullptr, const ModelWorkControl &control = {});
QJsonObject modelPlayerBundleJson(const ModelPlayerBundle &bundle);
QStringList modelPlayerBundleText(const ModelPlayerBundle &bundle);
// Shared PK3 writer: atomic publication, guarded overwrite, cancellation and a
// second hash-only pass proving determinism. Inputs and generated backup paths
// are protected; no game installation or source package is modified.
PackageWriteReport writeModelPlayerBundle(const ModelPlayerBundle &bundle, const QString &path, bool overwrite, bool dryRun,
										  const ModelWorkControl &control = {});
} // namespace vibestudio
