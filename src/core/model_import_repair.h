#pragma once

#include "core/model_document.h"

namespace vibestudio
{
inline constexpr qint64 modelImportRepairMaxFacePoses = 16777216;
enum class ModelImportRepairKind
{
	InvalidIndex,
	RepeatedVertex,
	CollapsedFace,
	RebuildNormal,
	RemoveSeam
};
struct ModelImportRepairChange
{
	ModelImportRepairKind kind;
	int surface, frame, element, other = -1;
	bool fallback = false;
};
struct ModelImportRepairPlan
{
	QString inputPath, resolvedPath;
	QByteArray sourceSha256;
	ModelMesh mesh;
	QVector<ModelImportRepairChange> changes;
	int removedFaces = 0, rebuiltNormals = 0, fallbackNormals = 0, removedSeams = 0;
};

// Quarantined, read-only preparation. Removes invalid/collapsed faces across
// every pose, replaces only unusable normals, and removes orphaned seam marks.
// Surviving indices and all other authored data are retained. Partial native
// decodes, invalid metadata and missing coordinate arrays are never guessed.
// Failure/cancellation leaves the caller's plan untouched.
bool prepareModelImportRepair(const QString &path, ModelImportRepairPlan *plan, QString *error = nullptr,
							  const ModelWorkControl &control = {});
// Publishes a NEW .mesh.json only, after rechecking the reviewed input identity.
// The repaired document becomes the ordinary saved source for further authoring.
// No overwrite mode exists. A completed durable write is a successful result.
bool saveModelImportRepair(const ModelImportRepairPlan &plan, const QString &output, ModelDocument *document, QString *error = nullptr,
						   const ModelWorkControl &control = {});
QString modelImportRepairKindId(ModelImportRepairKind kind);
QJsonObject modelImportRepairJson(const ModelImportRepairPlan &plan);
} // namespace vibestudio
