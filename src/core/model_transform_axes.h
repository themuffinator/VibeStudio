#pragma once

#include "core/model_transform.h"
#include "core/model_work.h"

namespace vibestudio
{
struct ModelEdit;
// Options are operation controls, not persistent mesh metadata. Selection axes
// are captured from one reference pose and shared by all affected poses.
bool validModelTransformAxesOptions(const ModelEdit &edit, QString *error = nullptr);
bool resolveModelTransformAxes(const ModelMesh &mesh, const ModelEdit &edit, ModelTransformBasis *result,
							   QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
