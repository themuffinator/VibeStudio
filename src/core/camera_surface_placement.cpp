#include "core/camera_surface_placement.h"
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
bool finite(BoxResizePoint point)
{
	return std::all_of(point.begin(), point.end(), [](double value) { return std::isfinite(value); });
}
BoxResizePoint subtract(BoxResizePoint a, BoxResizePoint b)
{
	return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
double dot(BoxResizePoint a, BoxResizePoint b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
} // namespace

bool cameraSurfacePoint(BoxResizeRay ray, const std::array<BoxResizePoint, 3> &triangle, CameraSurfacePoint *result)
{
	if (!result || !finite(ray.origin) || !finite(ray.direction) ||
		!std::all_of(triangle.begin(), triangle.end(), [](const auto &point) { return finite(point); }))
	{
		return false;
	}
	const auto a = subtract(triangle[1], triangle[0]), b = subtract(triangle[2], triangle[0]);
	BoxResizePoint normal{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
	const double normalLength = std::hypot(normal[0], normal[1], normal[2]);
	const double rayLength = std::hypot(ray.direction[0], ray.direction[1], ray.direction[2]);
	if (!std::isfinite(normalLength) || normalLength < 1e-9 || !std::isfinite(rayLength) || rayLength < 1e-12)
	{
		return false;
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		normal[axis] /= normalLength;
		ray.direction[axis] /= rayLength;
	}
	const double denominator = dot(normal, ray.direction);
	if (std::abs(denominator) < 1e-4)
	{
		return false;
	}
	const double distance = dot(normal, subtract(triangle[0], ray.origin)) / denominator;
	if (!std::isfinite(distance) || (ray.forwardOnly && distance <= 0))
	{
		return false;
	}
	CameraSurfacePoint next;
	for (int axis = 0; axis < 3; ++axis)
	{
		next.position[axis] = ray.origin[axis] + ray.direction[axis] * distance;
	}
	if (!finite(next.position))
	{
		return false;
	}
	const auto offset = subtract(next.position, triangle[0]);
	const double aa = dot(a, a), ab = dot(a, b), bb = dot(b, b), determinant = aa * bb - ab * ab;
	if (!std::isfinite(determinant) || determinant <= 1e-12)
	{
		return false;
	}
	const double u = (bb * dot(offset, a) - ab * dot(offset, b)) / determinant;
	const double v = (aa * dot(offset, b) - ab * dot(offset, a)) / determinant;
	if (!std::isfinite(u) || !std::isfinite(v) || u < -1e-6 || v < -1e-6 || u + v > 1 + 1e-6)
	{
		return false;
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		next.normal[axis] = denominator > 0 ? -normal[axis] : normal[axis];
	}
	*result = next;
	return true;
}

bool cameraSurfacePlacement(const CameraSurfacePoint &surface, const ResizeBox &bounds, double clearance, double grid,
							BoxResizePoint *origin)
{
	if (!origin || !finite(surface.position) || !finite(surface.normal) || !finite(bounds.mins) || !finite(bounds.maxs) ||
		!std::isfinite(clearance) || clearance < 0 || clearance > 32768 || !std::isfinite(grid) || grid < 0 || grid > 32768)
	{
		return false;
	}
	const double length = std::hypot(surface.normal[0], surface.normal[1], surface.normal[2]);
	if (!std::isfinite(length) || std::abs(length - 1) > 1e-6)
	{
		return false;
	}
	double support = 0;
	int dominant = 0;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (bounds.mins[axis] > bounds.maxs[axis])
		{
			return false;
		}
		support += surface.normal[axis] * (surface.normal[axis] >= 0 ? bounds.mins[axis] : bounds.maxs[axis]);
		if (std::abs(surface.normal[axis]) > std::abs(surface.normal[dominant]))
		{
			dominant = axis;
		}
	}
	BoxResizePoint next;
	for (int axis = 0; axis < 3; ++axis)
	{
		next[axis] = surface.position[axis] + surface.normal[axis] * (clearance - support);
		if (grid > 0)
		{
			next[axis] = std::round(next[axis] / grid) * grid;
		}
	}
	// Rounding all coordinates can bury the lower corner. Move out on the most
	// stable axis, rounding away from the surface to preserve the chosen grid.
	const double missing = clearance - support - dot(subtract(next, surface.position), surface.normal);
	if (missing > 1e-8)
	{
		const double correction = missing / std::abs(surface.normal[dominant]);
		const double step = grid > 0 ? std::ceil(correction / grid - 1e-10) * grid : correction;
		next[dominant] += std::copysign(step, surface.normal[dominant]);
	}
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!std::isfinite(next[axis]) || std::abs(next[axis]) > 32768 || next[axis] + bounds.mins[axis] < -32768 ||
			next[axis] + bounds.maxs[axis] > 32768)
		{
			return false;
		}
	}
	*origin = next;
	return true;
}

} // namespace vibestudio
