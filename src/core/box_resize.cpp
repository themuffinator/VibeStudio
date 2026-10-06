#include "core/box_resize.h"
#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
bool finite(BoxResizePoint point)
{
	return std::all_of(point.begin(),point.end(),[](double value) { return std::isfinite(value); });
}
double dot(BoxResizePoint a, BoxResizePoint b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
bool intersection(const BoxResizeDrag& drag, BoxResizeRay ray, double* coordinate)
{
	if (!coordinate || drag.handle < 0 || drag.handle >= 6 || !finite(ray.origin) || !finite(ray.direction)) { return false; }
	const double length = std::hypot(ray.direction[0],ray.direction[1],ray.direction[2]);
	if (!std::isfinite(length) || length <= 1e-12) { return false; }
	for (auto& value : ray.direction) { value /= length; }
	const double facing = dot(ray.direction,drag.normal);
	if (std::abs(facing) < 1e-4) { return false; }
	BoxResizePoint relative;
	for (int axis = 0; axis < 3; ++axis) { relative[axis] = drag.anchor[axis] - ray.origin[axis]; }
	const double distance = dot(relative,drag.normal) / facing;
	const double limit = std::max(1.0,std::hypot(relative[0],relative[1],relative[2])) * 64;
	if (!std::isfinite(distance) || std::abs(distance) > limit || (ray.forwardOnly && distance <= 0)) { return false; }
	const int axis = drag.handle / 2;
	const double at = ray.origin[axis] + ray.direction[axis] * distance;
	if (!std::isfinite(at)) { return false; }
	*coordinate = at; return true;
}
}

bool validResizeBox(const ResizeBox& box)
{
	if (!finite(box.mins) || !finite(box.maxs)) { return false; }
	for (int axis = 0; axis < 3; ++axis) {
		if (box.mins[axis] > box.maxs[axis] || !std::isfinite(box.maxs[axis]-box.mins[axis])) { return false; }
	}
	return true;
}

bool boxResizeHandle(const ResizeBox& box, int handle, BoxResizePoint* point)
{
	if (!point || !validResizeBox(box) || handle < 0 || handle >= 6 || box.maxs[handle/2]-box.mins[handle/2] <= 1e-6) { return false; }
	for (int axis = 0; axis < 3; ++axis) { (*point)[axis] = box.mins[axis] + (box.maxs[axis]-box.mins[axis])*0.5; }
	(*point)[handle/2] = handle % 2 ? box.maxs[handle/2] : box.mins[handle/2];
	return true;
}

bool beginBoxResize(const ResizeBox& box, int handle, BoxResizePoint viewDirection,
	BoxResizeRay ray, double grid, BoxResizeDrag* drag)
{
	BoxResizeDrag next; next.source = box; next.handle = handle; next.grid = grid;
	if (!drag || !finite(viewDirection) || !std::isfinite(grid) || grid < 0 || grid > 1e6
		|| (grid > 0 && grid < 1e-6) || !boxResizeHandle(box,handle,&next.anchor)) { return false; }
	next.normal = viewDirection; next.normal[handle/2] = 0;
	const double length = std::hypot(next.normal[0],next.normal[1],next.normal[2]);
	const double viewLength = std::hypot(viewDirection[0],viewDirection[1],viewDirection[2]);
	if (!std::isfinite(viewLength) || viewLength <= 0 || length / viewLength < 0.05) { return false; }
	for (auto& value : next.normal) { value /= length; }
	if (!intersection(next,ray,&next.pressCoordinate)) { return false; }
	*drag = next; return true;
}

bool updateBoxResize(const BoxResizeDrag& drag, BoxResizeRay ray, ResizeBox* box)
{
	double coordinate = 0;
	if (!box || !validResizeBox(drag.source) || !intersection(drag,ray,&coordinate)) { return false; }
	const int axis = drag.handle / 2;
	const double travel = coordinate - drag.pressCoordinate;
	if (travel == 0) { *box = drag.source; return true; }
	const bool maximum = drag.handle % 2;
	double target = (maximum ? drag.source.maxs[axis] : drag.source.mins[axis]) + travel;
	if (drag.grid > 0) { target = std::round(target / drag.grid) * drag.grid; }
	if (!std::isfinite(target)) { return false; }
	const double smallest = std::min(drag.grid > 0 ? drag.grid : 1.0,drag.source.maxs[axis]-drag.source.mins[axis]);
	auto next = drag.source;
	if (maximum) { next.maxs[axis] = std::max(target,next.mins[axis]+smallest); }
	else { next.mins[axis] = std::min(target,next.maxs[axis]-smallest); }
	if (!validResizeBox(next)) { return false; }
	*box = next; return true;
}

BoxResizePoint resizeBoxPoint(const ResizeBox& from, const ResizeBox& to, BoxResizePoint point)
{
	for (int axis = 0; axis < 3; ++axis) {
		const double extent = from.maxs[axis]-from.mins[axis];
		point[axis] = to.mins[axis] + (point[axis]-from.mins[axis]) * (extent > 1e-6 ? (to.maxs[axis]-to.mins[axis])/extent : 1);
	}
	return point;
}

} // namespace vibestudio
