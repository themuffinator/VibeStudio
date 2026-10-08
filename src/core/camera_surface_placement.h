#pragma once

#include "core/box_resize.h"

namespace vibestudio
{

struct CameraSurfacePoint
{
	BoxResizePoint position{}, normal{};
};

// Resolve the renderer's visible triangle against the actual view ray. The
// unit normal faces the camera, including two-sided preview geometry.
bool cameraSurfacePoint(BoxResizeRay ray, const std::array<BoxResizePoint, 3> &triangle, CameraSurfacePoint *result);

// Keep an origin-relative bounding box outside the sampled surface, with
// optional clearance. Grid snapping remains outside the surface half-space;
// sloped faces can consequently leave a small gap. Output is atomic on failure.
bool cameraSurfacePlacement(const CameraSurfacePoint &surface, const ResizeBox &bounds, double clearance, double grid,
							BoxResizePoint *origin);

} // namespace vibestudio
