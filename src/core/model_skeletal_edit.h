#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
struct ModelEdit;
enum class ModelEditKind;

struct ModelSkeletalEditOptions
{
	int clip = -1, sourceClip = -1;
	int first = 0, last = 0, sourceFrame = 0, frameCount = 1;
	QString joint, name;
	double rate = 24;
	bool loops = true, descendants = true, local = false;
	ModelVec3 translation, rotation, scale{1, 1, 1};
	// A complete model-space affine delta for shared viewport/CLI transforms.
	// It is mutually exclusive with the numeric local/model transform fields.
	bool matrixTransform = false;
	ModelJointMatrix delta;
	// SetSkeletalWorkingSet: the clip indexes to bake into editable frames
	// (core/model_skeletal_working_set.h). An empty list keeps only the bind
	// pose; every other operation leaves it empty.
	QVector<int> workingSet;
};

bool isModelSkeletalEdit(ModelEditKind kind);
bool defaultModelSkeletalOptions(const ModelSkeletalEditOptions &options);
// Replace a consecutive range without remapping clips/frames. Reuses the pose
// deformation guard and auxiliary-track preservation for procedural authoring.
bool replaceModelSkeletalSamples(ModelMesh *candidate, int clip, int first, const QVector<QVector<ModelJointMatrix>> &samples,
	QString *error, const ModelWorkControl &control = {}, const QVector<ModelPoseCurves> *curves = nullptr);
// Candidate-only edits. Poses are dense samples; interpolation works in parent
// space. Structural edits remap retained frame metadata and auxiliary tracks.
bool applyModelSkeletalEdit(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
