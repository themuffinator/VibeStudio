#include "core/box_draw.h"
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
constexpr double limit = 32768;
bool finite(BoxResizePoint point)
{
	return std::all_of(point.begin(),point.end(),[](double value) { return std::isfinite(value); });
}
bool valid(const BoxDrawDrag& drag)
{
	return drag.axis >= 0 && drag.axis < 3 && finite(drag.start) && std::isfinite(drag.base)
		&& std::isfinite(drag.depth) && drag.base >= -limit && drag.depth >= 1 && drag.base+drag.depth <= limit
		&& std::isfinite(drag.grid) && drag.grid >= 0 && drag.grid <= limit && (drag.grid == 0 || drag.grid >= 1);
}
bool intersection(const BoxDrawDrag& drag, BoxResizeRay ray, BoxResizePoint* point)
{
	if (!valid(drag) || !finite(ray.origin) || !finite(ray.direction)) { return false; }
	const double length = std::hypot(ray.direction[0],ray.direction[1],ray.direction[2]);
	if (!std::isfinite(length) || length <= 1e-12) { return false; }
	for (auto& value : ray.direction) { value /= length; }
	if (std::abs(ray.direction[drag.axis]) < 1e-4) { return false; }
	const double distance = (drag.base-ray.origin[drag.axis]) / ray.direction[drag.axis];
	if (!std::isfinite(distance) || (ray.forwardOnly && distance <= 0)) { return false; }
	BoxResizePoint next;
	for (int axis = 0; axis < 3; ++axis) {
		double at = ray.origin[axis] + ray.direction[axis]*distance;
		if (axis == drag.axis) { at = drag.base; }
		else if (drag.grid > 0) { at = std::round(at/drag.grid)*drag.grid; }
		if (!std::isfinite(at) || std::abs(at) > limit) { return false; }
		next[axis] = at;
	}
	*point = next; return true;
}
}

bool beginBoxDraw(BoxResizeRay ray, int axis, double base, double depth, double grid, BoxDrawDrag* drag)
{
	BoxDrawDrag next; next.axis = axis; next.base = base; next.depth = depth; next.grid = grid;
	if (!drag || !intersection(next,ray,&next.start)) { return false; }
	*drag = next; return true;
}

bool updateBoxDraw(const BoxDrawDrag& drag, BoxResizeRay ray, bool square, bool cube, ResizeBox* box)
{
	BoxResizePoint end;
	if (!box || !intersection(drag,ray,&end)) { return false; }
	const int u = (drag.axis+1)%3, v = (drag.axis+2)%3;
	const double du = end[u]-drag.start[u], dv = end[v]-drag.start[v];
	const double size = std::max(std::abs(du),std::abs(dv));
	if (square || cube) {
		end[u] = drag.start[u] + (du < 0 ? -size : size);
		end[v] = drag.start[v] + (dv < 0 ? -size : size);
	}
	ResizeBox next;
	for (int axis = 0; axis < 3; ++axis) {
		next.mins[axis] = std::min(drag.start[axis],end[axis]); next.maxs[axis] = std::max(drag.start[axis],end[axis]);
	}
	next.mins[drag.axis] = drag.base; next.maxs[drag.axis] = drag.base + (cube ? size : drag.depth);
	for (int axis = 0; axis < 3; ++axis) {
		if (next.mins[axis] < -limit || next.maxs[axis] > limit || next.maxs[axis]-next.mins[axis] < 1) { return false; }
	}
	*box = next; return true;
}

bool adjustBoxDrawDepth(BoxDrawDrag* drag, int steps)
{
	if (!drag || !valid(*drag) || !steps) { return false; }
	const double step = drag->grid > 0 ? drag->grid : 1;
	double top = drag->base + drag->depth + double(steps)*step;
	if (drag->grid > 0) { top = std::round(top/step)*step; }
	const double depth = std::clamp(top-drag->base,1.0,limit-drag->base);
	if (depth == drag->depth) { return false; }
	drag->depth = depth; return true;
}

} // namespace vibestudio
