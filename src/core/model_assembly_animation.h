#pragma once

#include "core/model_assembly.h"

namespace vibestudio
{
struct ModelAssemblyAnimationOptions
{
	double startSeconds = 0;
	int frameCount = 30;
	double framesPerSecond = 30;
	QString clipName = QStringLiteral("assembly");
};
struct ModelAssemblyAnimation
{
	ModelMesh mesh;
	QStringList notes;
};
// Uniform, endpoint-exclusive samples: startSeconds + index / framesPerSecond.
// Immutable inputs and all-or-nothing publication, with authoring storage limits.
bool bakeModelAssemblyAnimation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved,
								const ModelAssemblyAnimationOptions &options, ModelAssemblyAnimation *result, QString *error = nullptr,
								const ModelWorkControl &control = {});
QStringList modelAssemblyAnimationNotes(const ModelAssemblyResolved &resolved, const ModelAssemblyAnimationOptions &options);
// Editable .mesh.json retains clip timing; MD2/MD3 require game timing configuration.
bool exportModelAssemblyAnimation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved,
								  const ModelAssemblyAnimationOptions &options, const QString &path, const QString &sourcePath,
								  bool overwrite, bool dryRun, QString *error = nullptr, const ModelWorkControl &control = {},
								  ModelExportReport *report = nullptr, const std::shared_ptr<const PackageArchiveReader> &archive = {});
} // namespace vibestudio
