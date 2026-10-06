#include "core/model_assembly.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

namespace vibestudio
{
bool validateModelAssemblySkin(const ModelAssemblySkin &skin, QString *error)
{
	if (skin.source.trimmed().isEmpty() || skin.source.size() > 4096 || skin.source.contains(QChar(0)) ||
		(skin.sourceKind != ModelAssemblySource::File && skin.sourceKind != ModelAssemblySource::Package) || skin.entryIndex < -1 ||
		(skin.sourceKind == ModelAssemblySource::File && skin.entryIndex != -1) ||
		(skin.sourceKind == ModelAssemblySource::Package &&
		 (skin.source.startsWith('/') || skin.source.contains('\\') || skin.source.contains(':') ||
		  skin.source.split('/').contains(QStringLiteral("..")) || QDir::cleanPath(skin.source) != skin.source)))
	{
		if (error)
			*error = QCoreApplication::translate(
				"VibeStudioModelAssembly",
				"Linked skins need a valid file or normalized package path. Only package skins can name an entry index.");
		return false;
	}
	return true;
}
bool resolveModelAssemblySkin(const ModelAssemblySkin &skin, const ModelAssemblyContext &context, ModelAssemblyInput *input, QString *error,
							  const ModelWorkControl &control)
{
	if (error)
		error->clear();
	if (!input || !validateModelAssemblySkin(skin, error))
		return false;
	ModelSkinBindingInput content;
	ModelAssemblySkinInput result;
	result.sourceKind = skin.sourceKind;
	if (skin.sourceKind == ModelAssemblySource::File)
	{
		if (!QFileInfo(skin.source).isAbsolute() && context.directory.isEmpty())
		{
			if (error)
				*error =
					QCoreApplication::translate("VibeStudioModelAssembly", "Relative skin references need an assembly source directory.");
			return false;
		}
		result.source = QDir::cleanPath(QDir(context.directory).absoluteFilePath(skin.source));
		result.resolvedPath = QFileInfo(result.source).canonicalFilePath();
		if (!readModelSkinBindings(result.source, &content, error, control))
			return false;
		if (result.resolvedPath.isEmpty() || result.resolvedPath != QFileInfo(result.source).canonicalFilePath())
		{
			if (error)
				*error =
					QCoreApplication::translate("VibeStudioModelAssembly", "The linked skin path changed while reading. Reload its input.");
			return false;
		}
	}
	else
	{
		if (!context.archive || !context.archive->isOpen())
		{
			if (error)
				*error = QCoreApplication::translate("VibeStudioModelAssembly",
													 "Open the package used by this assembly before loading linked skins.");
			return false;
		}
		if (!readModelSkinBindings(*context.archive, {skin.source, skin.entryIndex}, &content, error, control))
			return false;
		result.source = content.path;
		result.entryIndex = content.entryIndex;
	}
	if (!planModelSkinBindings(input->mesh, content.bytes, &result.bindings, error, control))
		return false;
	result.bytes = content.bytes.size();
	result.sha256 = QCryptographicHash::hash(content.bytes, QCryptographicHash::Sha256);
	auto mesh = input->mesh;
	for (const auto &binding : result.bindings.assignments)
	{
		auto &paths = mesh.surfaces[binding.surface].skinPaths;
		if (paths.isEmpty())
			paths.append(binding.material);
		else
			paths[0] = binding.material;
	}
	updateEditableModelMetadata(&mesh);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.bytes, result.bytes, error))
		return false;
	input->mesh = std::move(mesh);
	input->skin = std::move(result);
	return true;
}

bool modelAssemblyInputProtectsPath(const ModelAssemblyInput &input, const QString &path)
{
	return (input.sourceKind == ModelAssemblySource::File && modelPathsReferToSameFile(path, input.source)) ||
		   (input.skin && input.skin->sourceKind == ModelAssemblySource::File &&
			(modelPathsReferToSameFile(path, input.skin->source) || modelPathsReferToSameFile(path, input.skin->resolvedPath)));
}

QJsonObject modelAssemblySkinInputJson(const ModelAssemblySkinInput &skin)
{
	return {{"source", skin.source},		 {"kind", skin.sourceKind == ModelAssemblySource::File ? "file" : "package"},
			{"entryIndex", skin.entryIndex}, {"sha256", QString::fromLatin1(skin.sha256.toHex())},
			{"bytes", skin.bytes},			 {"bindings", modelSkinBindingPlanJson(skin.bindings)}};
}
} // namespace vibestudio
