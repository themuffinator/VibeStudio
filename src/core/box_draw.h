#pragma once

#include "core/box_resize.h"

namespace vibestudio {

// Axis-aligned construction plane; hidden axis 2 = XY, 1 = XZ, 0 = YZ.
// Source units and limits match the shared level brush primitive service.
struct BoxDrawDrag {
	BoxResizePoint start{};
	int axis = 2;
	double base = 0, depth = 64, grid = 0;
};

bool beginBoxDraw(BoxResizeRay ray, int axis, double base, double depth, double grid, BoxDrawDrag* drag);
// A failed/grazing/backward ray or collapsed/out-of-range box leaves output
// unchanged. Square constrains the footprint; cube also sets the depth.
bool updateBoxDraw(const BoxDrawDrag& drag, BoxResizeRay ray, bool square, bool cube, ResizeBox* box);
bool adjustBoxDrawDepth(BoxDrawDrag* drag, int steps);

} // namespace vibestudio
