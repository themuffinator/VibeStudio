#pragma once

#include "core/box_resize.h"

namespace vibestudio {

// Axis-aligned construction plane; hidden axis 2 = XY, 1 = XZ, 0 = YZ.
// Source units and limits match the shared level brush primitive service.
struct BoxDrawDrag {
	BoxResizePoint start{};
	int axis = 2;
	double base = 0, depth = 64, grid = 0;
	int direction = 1;
};

bool beginBoxDraw(BoxResizeRay ray, int axis, double base, double depth, double grid, BoxDrawDrag* drag, int direction = 1);
// Pick a cardinal construction plane through a visible triangle hit. Depth
// extends towards the ray origin; a sloped face uses its dominant normal axis.
// Invalid/behind/grazing/out-of-bounds input leaves the output untouched.
bool boxDrawPlaneFromSurface(BoxResizeRay ray, const std::array<BoxResizePoint,3>& triangle,
	double depth, double grid, BoxDrawDrag* drag);
// A failed/grazing/backward ray or collapsed/out-of-range box leaves output
// unchanged. Square constrains the footprint; cube also sets the depth.
bool updateBoxDraw(const BoxDrawDrag& drag, BoxResizeRay ray, bool square, bool cube, ResizeBox* box);
bool adjustBoxDrawDepth(BoxDrawDrag* drag, int steps);

} // namespace vibestudio
