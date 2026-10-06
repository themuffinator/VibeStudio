#pragma once

#include "core/model_transform.h"

namespace vibestudio
{
struct ModelTrackballDrag
{
	std::array<double, 3> start{};
	ModelTransformBasis view, basis;
	double grid = 0;
};

// Pure, absolute virtual-sphere rotation. Coordinates are relative to the
// fixed screen centre/radius, with Y pointing up. Outside points project onto
// the equator. View columns are screen-right, screen-up and toward the viewer.
// Snap the shortest-arc angle (0..180), retaining its axis. The result uses
// the ordinary Rz * Ry * Rx Euler contract in the captured transform basis;
// callers must not snap those Euler components a second time.
// No input state, document or camera is changed. Invalid arguments leave
// output untouched, and updates never accumulate numerical drift.
bool beginModelTrackballDrag(double x, double y, const ModelTransformBasis &view, const ModelTransformBasis &basis, double grid,
							 ModelTrackballDrag *result);
bool modelTrackballRotation(const ModelTrackballDrag &drag, double x, double y, ModelVec3 *rotation);
} // namespace vibestudio
