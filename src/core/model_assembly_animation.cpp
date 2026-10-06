#include "core/model_assembly_animation.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
bool sameLayout(const ModelSurface &a, const ModelSurface &b)
{
	return a.name == b.name && a.vertexCount == b.vertexCount && a.skinPaths == b.skinPaths && a.uvSeams == b.uvSeams &&
		   std::equal(a.texCoords.cbegin(), a.texCoords.cend(), b.texCoords.cbegin(), b.texCoords.cend(),
					  [](const auto &x, const auto &y) { return x.u == y.u && x.v == y.v; }) &&
		   std::equal(a.triangles.cbegin(), a.triangles.cend(), b.triangles.cbegin(), b.triangles.cend(),
					  [](const auto &x, const auto &y) { return x.a == y.a && x.b == y.b && x.c == y.c; });
}
} // namespace

QStringList modelAssemblyAnimationNotes(const ModelAssemblyResolved &resolved, const ModelAssemblyAnimationOptions &options)
{
	auto notes = resolved.notes;
	notes << QCoreApplication::translate(
				 "VibeStudioModelAssemblyAnimation",
				 "Samples %1 poses from %2 seconds at %3 FPS; the last sample is %4 seconds. Clip duration is %5 seconds. "
				 "Keep the assembly and model sources to retain independent playback, attachment links and tags. The bake stores composed "
				 "geometry only.")
				 .arg(options.frameCount)
				 .arg(options.startSeconds, 0, 'g', 12)
				 .arg(options.framesPerSecond, 0, 'g', 12)
				 .arg(options.startSeconds + (options.frameCount - 1) / options.framesPerSecond, 0, 'g', 12)
				 .arg(options.frameCount / options.framesPerSecond, 0, 'g', 12);
	if (std::any_of(resolved.inputs.cbegin(), resolved.inputs.cend(),
					[](const auto &input) { return !input.mesh.collisionBoxes.isEmpty(); }))
	{
		notes << QCoreApplication::translate(
			"VibeStudioModelAssemblyAnimation",
			"Input collision boxes remain in their model sources. Author collision on the baked mesh separately.");
	}
	if (std::any_of(resolved.inputs.cbegin(), resolved.inputs.cend(),
					[](const auto &input) { return !input.mesh.embeddedSkins.isEmpty() || input.mesh.mdl.enabled; }))
	{
		notes << QCoreApplication::translate("VibeStudioModelAssemblyAnimation",
											 "Embedded skin images, native MDL timing and skin animation remain in their model sources. "
											 "The bake keeps external material paths only.");
	}
	notes << QCoreApplication::translate(
		"VibeStudioModelAssemblyAnimation",
		"Editable mesh sources retain the clip name and FPS. MD2/MD3 exports require separate game animation configuration at the sampled "
		"rate. "
		"MD2 requires one surface; use the Mesh Editor to explicitly join compatible surfaces before export. Looping the result can blend "
		"the last stored pose back to the first; choose a sampling interval that matches the intended motion.");
	return notes;
}

bool bakeModelAssemblyAnimation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved,
								const ModelAssemblyAnimationOptions &options, ModelAssemblyAnimation *result, QString *error,
								const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, 0, options.frameCount, error))
	{
		return false;
	}
	if (!result || !std::isfinite(options.startSeconds) || options.startSeconds < 0 || options.startSeconds > 1000000 ||
		!std::isfinite(options.framesPerSecond) || options.framesPerSecond < .001 || options.framesPerSecond > 1000 ||
		options.frameCount < 1 || options.frameCount > modelDocumentMaxFrames ||
		options.startSeconds + (options.frameCount - 1) / options.framesPerSecond > 1000000 || options.clipName.isEmpty() ||
		options.clipName != options.clipName.trimmed() || options.clipName.size() > 128 ||
		std::any_of(options.clipName.cbegin(), options.clipName.cend(),
					[](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; }))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelAssemblyAnimation",
												"Choose 1–1024 frames, 0.001–1000 FPS, sample times within 0–1,000,000 seconds and a clip "
												"name of 1–128 characters without surrounding spaces or control characters."));
	}
	if (!validateModelAssembly(assembly, error) || assembly.parts.isEmpty() || resolved.inputs.size() != assembly.parts.size() ||
		resolved.recipeSha256 != modelAssemblyFingerprint(assembly))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelAssemblyAnimation", "Reload the assembly inputs before baking animation."));
	}
	qint64 vertices = 0;
	for (const auto &input : resolved.inputs)
	{
		for (const auto &surface : input.mesh.surfaces)
		{
			vertices += surface.vertexCount;
		}
	}
	if (vertices < 1 || vertices > modelDocumentMaxVertices || vertices * options.frameCount > modelDocumentMaxFrameVertices)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssemblyAnimation",
													   "The animated bake exceeds 65,536 vertices or 1,048,576 stored frame vertices. "
													   "Reduce the frame count or simplify the inputs."));
	}
	ModelAssemblyAnimation candidate;
	// Each pose validates its exact composed geometry. Keep nested progress from
	// replacing the stable sequence counter, while preserving cancellation polls.
	const ModelWorkControl sampling{control.cancelled, {}};
	for (int index = 0; index < options.frameCount; ++index)
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, index, options.frameCount, error))
		{
			return false;
		}
		ModelAssemblyPose pose;
		if (!sampleModelAssembly(assembly, resolved, options.startSeconds + index / options.framesPerSecond, &pose, error, sampling))
		{
			return false;
		}
		pose.mesh.frames[0].name = QStringLiteral("bake%1").arg(index, 4, 10, QLatin1Char('0'));
		if (index == 0)
		{
			candidate.mesh = std::move(pose.mesh);
			candidate.mesh.frames.reserve(options.frameCount);
			for (auto &surface : candidate.mesh.surfaces)
			{
				surface.frames.reserve(options.frameCount);
			}
			continue;
		}
		if (pose.mesh.surfaces.size() != candidate.mesh.surfaces.size())
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelAssemblyAnimation", "The sampled assembly changes surface count."));
		}
		for (int surface = 0; surface < candidate.mesh.surfaces.size(); ++surface)
		{
			auto &target = candidate.mesh.surfaces[surface];
			const auto &sample = pose.mesh.surfaces[surface];
			if (!sameLayout(target, sample))
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelAssemblyAnimation",
															   "Sample %1 changes topology, winding, UVs or materials. Repair reflected "
															   "attachment changes or bake separate compatible intervals.")
									   .arg(index));
			}
			target.frames.append(sample.frames[0]);
		}
		candidate.mesh.frames.append(pose.mesh.frames[0]);
	}
	candidate.mesh.animations = {{options.clipName, 0, options.frameCount, options.framesPerSecond}};
	updateEditableModelMetadata(&candidate.mesh);
	const auto issues = validateEditableModel(candidate.mesh, control);
	if (!issues.isEmpty())
	{
		return fail(error, issues.join(QLatin1Char('\n')));
	}
	candidate.notes = modelAssemblyAnimationNotes(resolved, options);
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Editing, options.frameCount, options.frameCount, error))
	{
		return false;
	}
	*result = std::move(candidate);
	return true;
}

bool exportModelAssemblyAnimation(const ModelAssembly &assembly, const ModelAssemblyResolved &resolved,
								  const ModelAssemblyAnimationOptions &options, const QString &path, const QString &sourcePath,
								  bool overwrite, bool dryRun, QString *error, const ModelWorkControl &control, ModelExportReport *report,
								  const std::shared_ptr<const PackageArchiveReader> &archive)
{
	if (error)
	{
		error->clear();
	}
	if (report)
	{
		*report = {};
	}
	const bool editable = path.endsWith(QStringLiteral(".mesh.json"), Qt::CaseInsensitive);
	const auto suffix = QFileInfo(path).suffix().toLower();
	if (!editable && suffix != QStringLiteral("md2") && suffix != QStringLiteral("md3"))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelAssemblyAnimation", "Export animation as .mesh.json, .md2 or .md3."));
	}
	const auto target = inspectModelWriteTarget(path, control);
	if (!target.isValid())
	{
		return fail(error, target.error);
	}
	bool protectedInput = modelPathsReferToSameFile(path, sourcePath) ||
						  (archive && (archive->protectsInputPath(target.path) || archive->protectsInputPath(target.resolvedPath)));
	for (const auto &input : resolved.inputs)
	{
		protectedInput |= modelAssemblyInputProtectsPath(input, path);
	}
	if (protectedInput || (target.existed && !overwrite))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelAssemblyAnimation",
													   "Choose a separate output; assembly, model and package inputs are protected, and "
													   "replacing another output requires explicit overwrite."));
	}
	ModelAssemblyAnimation animation;
	if (!bakeModelAssemblyAnimation(assembly, resolved, options, &animation, error, control))
	{
		return false;
	}
	ModelExportReport details;
	QByteArray bytes;
	if (editable)
	{
		const auto object = editableModelJson(animation.mesh, error, control);
		if (object.isEmpty())
		{
			return false;
		}
		bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
		if (bytes.size() > modelDocumentMaxSourceBytes)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelAssemblyAnimation",
														   "The editable animation exceeds the 64 MiB source limit."));
		}
		details.storedVertices = animation.mesh.vertexCount;
	}
	else
	{
		bytes = exportEditableModel(animation.mesh, suffix, 0, error, control, &details);
		if (suffix == QStringLiteral("md3"))
		{
			details.storedVertices = animation.mesh.vertexCount;
		}
	}
	if (bytes.isEmpty() || !modelWorkCheckpoint(control, ModelWorkPhase::Serializing, bytes.size(), bytes.size(), error) ||
		(!dryRun && !writeModelFile(target, bytes, error, control)))
	{
		return false;
	}
	details.notes += animation.notes;
	if (report)
	{
		*report = std::move(details);
	}
	return true;
}
} // namespace vibestudio
