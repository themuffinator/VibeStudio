#pragma once

#include "core/model_mesh.h"

namespace vibestudio
{
struct ModelAnimationSample
{
	int frame = 0;
	int nextFrame = 0;
	double fraction = 0;
};

// Offset is measured in frames from the start of a nonempty clip. Arbitrarily
// delayed updates wrap in constant time; invalid inputs leave output untouched.
bool sampleModelAnimation(int first, int count, double offset, bool interpolate, ModelAnimationSample *output);

// Shared by generated poses and transient preview. Callers validate positions
// and amount; normal/tag helpers reject invalid or ambiguous blends atomically.
ModelVec3 interpolateModelPosition(ModelVec3 first, ModelVec3 second, double amount);
bool interpolateModelNormal(ModelVec3 first, ModelVec3 second, double amount, ModelVec3 *output);
// Endpoint poses are exact. Intermediate tag orientations use the shortest arc
// and preserve common handedness. Names must match; frameIndex comes from first.
bool interpolateModelTag(const ModelTag &first, const ModelTag &second, double amount, ModelTag *output);
} // namespace vibestudio
