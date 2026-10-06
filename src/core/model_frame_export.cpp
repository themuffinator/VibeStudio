#include "core/model_frame_export.h"

#include "core/model_file_io.h"
#include "core/model_collision.h"
#include "core/package_archive.h"

#include <QCoreApplication>

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
bool protect(const ModelWriteTarget &target, const ModelFrameExportRequest &request, QString *error)
{
	for (const auto &source : request.protectedFiles)
	{
		if (modelPathsReferToSameFile(target.path, source) || modelPathsReferToSameFile(target.resolvedPath, source))
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelExport",
													"An exported model cannot replace its source or package. Choose another output path."));
		}
	}
	if (request.sourceArchive &&
		(request.sourceArchive->protectsInputPath(target.path) || request.sourceArchive->protectsInputPath(target.resolvedPath)))
	{
		return fail(error, QCoreApplication::translate(
							   "VibeStudioModelExport",
							   "Export outside the source package, folder or draft. Use package staging to change its contents."));
	}
	return true;
}
} // namespace

static ModelFrameExportResult prepareAndWriteFrame(const ModelMesh &mesh, const ModelFrameExportRequest &request,
												   const ModelWorkControl &control)
{
	ModelFrameExportResult result;
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, 0, 0, &result.error))
	{
		return result;
	}
	if (!mesh.isValid() || !mesh.geometryAvailable || request.frame < 0 || request.frame >= mesh.frames.size())
	{
		result.error = mesh.error.isEmpty() ? QCoreApplication::translate("VibeStudioModelExport",
																		  "Select a decoded model and an existing frame before exporting.")
											: mesh.error;
		return result;
	}
	ModelWriteTarget target;
	if (!request.outputPath.isEmpty())
	{
		target = inspectModelWriteTarget(request.outputPath, control);
		if (!target.isValid())
		{
			result.error = target.error;
			return result;
		}
		if (!protect(target, request, &result.error))
		{
			return result;
		}
		if (target.existed && !request.overwrite)
		{
			result.error = QCoreApplication::translate("VibeStudioModelExport",
													   "The model output exists. Choose a new path or explicitly enable overwrite.");
			return result;
		}
	}
	const auto obj = exportModelFrameObj(mesh, request.frame, request.materialName, control, &result.error);
	if (obj.isEmpty())
	{
		if (result.error.isEmpty())
		{
			result.error = QCoreApplication::translate("VibeStudioModelExport", "The selected frame produced no exportable geometry.");
		}
		return result;
	}
	if (obj.size() > modelFileByteLimit || (result.bytes = obj.toUtf8()).size() > modelFileByteLimit)
	{
		result.bytes.clear();
		result.error = QCoreApplication::translate("VibeStudioModelExport", "The model output exceeds the 64 MiB export limit.");
		return result;
	}
	result.notes = mesh.warnings;
	if (!mesh.collisionBoxes.isEmpty()) { result.notes << modelCollisionOmissionNote(); }
	for (const auto &surface : mesh.surfaces)
	{
		result.notes += surface.warnings;
	}
	if (mesh.frames.size() > 1 || !mesh.animations.isEmpty())
	{
		result.notes << QCoreApplication::translate(
			"VibeStudioModelExport", "OBJ contains only the selected pose; other frames and animation clips remain in the source.");
	}
	if (!mesh.tags.isEmpty())
	{
		result.notes << QCoreApplication::translate(
			"VibeStudioModelExport", "OBJ does not store attachment tags. Retain the source or export MD3 from the mesh editor.");
	}
	if (!mesh.skinPaths.isEmpty() || !mesh.embeddedSkins.isEmpty())
	{
		result.notes << QCoreApplication::translate("VibeStudioModelExport",
													"This geometry export does not copy textures or native skin metadata. The mesh "
													"editor's OBJ export retains per-surface material assignments.");
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 1, 1, &result.error))
	{
		result.bytes.clear();
		return result;
	}
	if (!request.outputPath.isEmpty() && !request.dryRun)
	{
		if (!protect(target, request, &result.error) || !writeModelFile(target, result.bytes, &result.error, control))
		{
			return result;
		}
		result.written = true;
	}
	return result;
}

ModelFrameExportResult exportModelFrame(const ModelMesh &mesh, const ModelFrameExportRequest &request, const ModelWorkControl &control)
{
	auto result = prepareAndWriteFrame(mesh, request, control);
	// Publication is the durable boundary: cancellation after a successful
	// commit must not tell the caller that the completed file was discarded.
	if (!result.written && !modelWorkCheckpoint(control, ModelWorkPhase::Serializing, 1, 1, &result.error))
	{
		result.cancelled = true;
		result.bytes.clear();
	}
	return result;
}
} // namespace vibestudio
