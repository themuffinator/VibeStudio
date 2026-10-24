#pragma once

#include "core/model_mesh.h"
#include "core/model_work.h"

namespace vibestudio
{
struct ModelEdit;
enum class ModelWeightPaintMode
{
	Replace,
	Add,
	Subtract,
	Smooth
};
struct ModelWeightSample
{
	int vertex = -1;
	double opacity = 1;
	bool operator==(const ModelWeightSample &) const = default;
};
struct ModelWeightStroke
{
	int surface = 0;
	QString joint;
	ModelWeightPaintMode mode = ModelWeightPaintMode::Replace;
	double value = 1, strength = .5;
	QStringList lockedJoints;
	// Used only when reducing a sole unlocked influence leaves nowhere to put
	// the remainder. Never choose a joint implicitly.
	QString remainderJoint;
	// Unique vertex indices; maximum coverage per stroke makes brush output
	// independent of pointer event rate and repeated passes within that stroke.
	QVector<ModelWeightSample> samples;
	bool operator==(const ModelWeightStroke &) const = default;
};
inline constexpr int modelWeightMaxStrokes = 1024;
inline constexpr int modelWeightMaxSamples = 1048576;

double modelVertexJointWeight(const ModelSurface &surface, int vertex, int joint);
// Candidate-only operation; ModelDocument adopts the result atomically. Every
// stroke uses a frozen source for smoothing. Bake affected poses once at the end.
bool applyModelWeightPaint(ModelMesh *candidate, const ModelEdit &edit, QString *error, const ModelWorkControl &control = {});
} // namespace vibestudio
