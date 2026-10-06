#pragma once

#include "core/model_document.h"

namespace vibestudio
{
// Whole surfaces and component/tag/collision selections are mutually exclusive.
// The active inspector surface must belong to a nonempty whole-surface set.
bool validModelSurfaceSelection(const ModelMesh &mesh, const ModelSelection &selection);
bool modelSurfaceSelectionPivot(const ModelMesh &mesh, const QSet<int> &surfaces, int frame, ModelTransformPivot mode, ModelVec3 custom,
								ModelVec3 *result, QString *error = nullptr, const ModelWorkControl &control = {});
// Internal edit primitive. applyModelEdit owns validation and atomic publication.
bool applyModelSurfaceTransform(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
