#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
struct ModelEdit;
enum class ModelEditKind;

// Names address joints independently of hierarchy ordering. Empty parent means
// a root. New joints use an identity rotation at a model-space origin.
struct ModelRigOptions
{
	QString joint, name, parent;
	ModelVec3 origin;
	double weight = 1;
};

bool isModelRigEdit(ModelEditKind kind);
// Candidate-only operation; ModelDocument validates and commits atomically.
// Weight edits preserve bind geometry and refuse independent frame edits.
bool applyModelRigEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
