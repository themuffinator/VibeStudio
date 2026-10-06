#pragma once

#include "core/model_mesh.h"
#include "core/model_transform.h"

namespace vibestudio
{
enum class ModelEditKind;
struct ModelEdit;
struct ModelSelection;

QStringList modelTagNames(const ModelMesh &mesh);
const ModelTag *findModelTag(const ModelMesh &mesh, const QString &name, int frame);
bool validModelTagSelection(const ModelMesh &mesh, const ModelSelection &selection);
bool isModelTagEdit(ModelEditKind kind);
// Each MD3 row is a local basis vector expressed in model coordinates. These
// helpers preserve an existing basis (including handedness); rotation is rigid.
ModelVec3 modelTagPoint(const ModelTag &tag, ModelVec3 local);
ModelTag transformedModelTag(const ModelTag &tag, const ModelTransform &transform);
// Candidate-only implementation. applyModelEdit validates the complete source
// and result, and adopts mesh/selection atomically after cancellation checks.
bool applyModelTagEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
					   const ModelWorkControl &control = {});
} // namespace vibestudio
