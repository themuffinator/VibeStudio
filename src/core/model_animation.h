#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
struct ModelEdit;
enum class ModelEditKind;

bool isModelAnimationEdit(ModelEditKind kind);
// Operates on a disposable, validated document candidate. The caller performs
// full all-pose validation before adoption. Insertion retains existing poses and
// component indices exactly; generated poses interpolate positions/origins linearly, normal
// directions by normalized linear blending and tag rotations by shortest-arc
// spherical interpolation. Reflected tags retain their common handedness.
bool applyModelAnimationEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error = nullptr, const ModelWorkControl &control = {});
} // namespace vibestudio
