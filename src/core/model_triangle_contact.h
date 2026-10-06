#pragma once

#include "core/model_mesh.h"
#include <array>

namespace vibestudio
{
enum class ModelTriangleContact
{
	None,
	Crossing,
	CoplanarOverlap,
	Degenerate
};

// Shared floating-point predicate for boundary authoring and mesh inspection.
// A positive-length interval entering either face, or positive coplanar area,
// is a finding. Isolated point contacts and common geometric boundary segments
// are allowed, including distinct seam indices. Inputs should be nondegenerate;
// Degenerate is a defensive failure result, not an intersection classification.
// Uses scale-relative tolerances; this is not an exact-predicate certificate.
ModelTriangleContact modelTriangleContact(const std::array<ModelVec3, 3> &a, const std::array<ModelVec3, 3> &b);
} // namespace vibestudio
