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
		&& (drag.direction == -1 || drag.direction == 1)
		&& std::isfinite(drag.depth) && std::abs(drag.base) <= limit && drag.depth >= 1
		&& std::abs(drag.base+drag.direction*drag.depth) <= limit
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

bool beginBoxDraw(BoxResizeRay ray, int axis, double base, double depth, double grid, BoxDrawDrag* drag, int direction)
{
	BoxDrawDrag next; next.axis = axis; next.base = base; next.depth = depth; next.grid = grid; next.direction = direction;
	if (!drag || !intersection(next,ray,&next.start)) { return false; }
	*drag = next; return true;
}

bool boxDrawPlaneFromSurface(BoxResizeRay ray, const std::array<BoxResizePoint,3>& triangle,
	double depth, double grid, BoxDrawDrag* drag)
{
	if (!drag || !finite(ray.origin) || !finite(ray.direction) || !std::isfinite(depth) || depth < 1
		|| !std::all_of(triangle.begin(),triangle.end(),[](const auto& point) { return finite(point); })) { return false; }
	const auto subtract = [](const auto& a, const auto& b) { return BoxResizePoint{a[0]-b[0],a[1]-b[1],a[2]-b[2]}; };
	const auto dot = [](const auto& a, const auto& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; };
	const auto edge0 = subtract(triangle[1],triangle[0]), edge1 = subtract(triangle[2],triangle[0]);
	BoxResizePoint normal{edge0[1]*edge1[2]-edge0[2]*edge1[1],edge0[2]*edge1[0]-edge0[0]*edge1[2],edge0[0]*edge1[1]-edge0[1]*edge1[0]};
	const double normalLength = std::hypot(normal[0],normal[1],normal[2]);
	const double rayLength = std::hypot(ray.direction[0],ray.direction[1],ray.direction[2]);
	if (!std::isfinite(normalLength) || normalLength < 1e-9 || !std::isfinite(rayLength) || rayLength < 1e-12) { return false; }
	for (int axis = 0; axis < 3; ++axis) { normal[axis] /= normalLength; ray.direction[axis] /= rayLength; }
	const double denominator = dot(normal,ray.direction);
	if (std::abs(denominator) < 1e-4) { return false; }
	const double distance = dot(normal,subtract(triangle[0],ray.origin))/denominator;
	if (!std::isfinite(distance) || (ray.forwardOnly && distance <= 0)) { return false; }
	BoxResizePoint hit;
	for (int axis = 0; axis < 3; ++axis) { hit[axis] = ray.origin[axis]+ray.direction[axis]*distance; }
	if (!finite(hit)) { return false; }
	// The renderer identifies the visible triangle; verify its actual ray
	// intersection as well, including borders shared by adjacent triangles.
	const auto offset = subtract(hit,triangle[0]);
	const double aa = dot(edge0,edge0), ab = dot(edge0,edge1), bb = dot(edge1,edge1);
	const double determinant = aa*bb-ab*ab;
	if (!std::isfinite(determinant) || determinant <= 1e-12) { return false; }
	const double u = (bb*dot(offset,edge0)-ab*dot(offset,edge1))/determinant;
	const double v = (aa*dot(offset,edge1)-ab*dot(offset,edge0))/determinant;
	if (!std::isfinite(u) || !std::isfinite(v) || u < -1e-6 || v < -1e-6 || u+v > 1+1e-6) { return false; }
	int axis = 2;
	for (int candidate : {0,1}) {
		if (std::abs(normal[candidate]) > std::abs(normal[axis])+1e-8
			|| (std::abs(std::abs(normal[candidate])-std::abs(normal[axis])) <= 1e-8
				&& std::abs(ray.direction[candidate]) > std::abs(ray.direction[axis]))) { axis = candidate; }
	}
	const int direction = ray.origin[axis] < hit[axis] ? -1 : 1;
	const double available = limit-direction*hit[axis];
	if (!std::isfinite(available) || available < 1) { return false; }
	return beginBoxDraw(ray,axis,hit[axis],std::min(depth,available),grid,drag,direction);
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
	const double endDepth = drag.base + drag.direction*(cube ? size : drag.depth);
	next.mins[drag.axis] = std::min(drag.base,endDepth); next.maxs[drag.axis] = std::max(drag.base,endDepth);
	for (int axis = 0; axis < 3; ++axis) {
		if (next.mins[axis] < -limit || next.maxs[axis] > limit || next.maxs[axis]-next.mins[axis] < 1) { return false; }
	}
	*box = next; return true;
}

bool adjustBoxDrawDepth(BoxDrawDrag* drag, int steps)
{
	if (!drag || !valid(*drag) || !steps) { return false; }
	const double step = drag->grid > 0 ? drag->grid : 1;
	double top = drag->base + drag->direction*(drag->depth + double(steps)*step);
	if (drag->grid > 0) { top = std::round(top/step)*step; }
	const double depth = std::clamp(drag->direction*(top-drag->base),1.0,limit-drag->direction*drag->base);
	if (depth == drag->depth) { return false; }
	drag->depth = depth; return true;
}

} // namespace vibestudio
