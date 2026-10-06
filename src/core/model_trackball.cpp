#include "core/model_trackball.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
using Vector = std::array<double, 3>;
Vector vector(ModelVec3 v)
{
	return {v.x, v.y, v.z};
}
double dot(Vector a, Vector b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vector cross(Vector a, Vector b)
{
	return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
bool unit(Vector v)
{
	return std::all_of(v.begin(), v.end(), [](double n) { return std::isfinite(n); }) && std::abs(dot(v, v) - 1) < 0.00001;
}
bool valid(const ModelTrackballDrag &drag)
{
	ModelVec3 checked;
	return unit(drag.start) && drag.start[2] >= 0 && validModelTransformBasis(drag.view) && validModelTransformBasis(drag.basis) &&
		   dot(cross(vector(drag.view.axes[0]), vector(drag.view.axes[1])), vector(drag.view.axes[2])) > 0 &&
		   snapModelRotation({}, drag.grid, &checked);
}
bool sphere(double x, double y, Vector *result)
{
	if (!std::isfinite(x) || !std::isfinite(y))
		return false;
	// Scaling first keeps even finite coordinates near DBL_MAX safe to lift.
	const double extent = std::max(std::abs(x), std::abs(y));
	if (extent > 1)
	{
		x /= extent;
		y /= extent;
		const double length = std::hypot(x, y);
		*result = {x / length, y / length, 0};
	}
	else
	{
		const double length = std::hypot(x, y);
		*result = length > 1 ? Vector{x / length, y / length, 0} : Vector{x, y, std::sqrt(std::max(0.0, 1 - length * length))};
	}
	return true;
}
Vector toBasis(Vector p, const ModelTransformBasis &basis)
{
	return {dot(p, vector(basis.axes[0])), dot(p, vector(basis.axes[1])), dot(p, vector(basis.axes[2]))};
}
Vector fromBasis(Vector p, const ModelTransformBasis &basis)
{
	Vector result{};
	for (int axis = 0; axis < 3; ++axis)
	{
		const auto column = vector(basis.axes[axis]);
		for (int i = 0; i < 3; ++i)
			result[i] += p[axis] * column[i];
	}
	return result;
}
} // namespace

bool beginModelTrackballDrag(double x, double y, const ModelTransformBasis &view, const ModelTransformBasis &basis, double grid,
							 ModelTrackballDrag *result)
{
	ModelTrackballDrag candidate;
	candidate.view = view;
	candidate.basis = basis;
	candidate.grid = grid;
	if (!result || !sphere(x, y, &candidate.start) || !valid(candidate))
		return false;
	*result = candidate;
	return true;
}

bool modelTrackballRotation(const ModelTrackballDrag &drag, double x, double y, ModelVec3 *rotation)
{
	Vector end;
	if (!rotation || !valid(drag) || !sphere(x, y, &end))
		return false;
	auto axis = cross(drag.start, end);
	const double sine = std::sqrt(dot(axis, axis)), cosine = std::clamp(dot(drag.start, end), -1.0, 1.0);
	double angle = std::atan2(sine, cosine) * 180 / std::numbers::pi;
	if (sine < 1e-12)
	{
		if (cosine >= 0)
		{
			*rotation = {};
			return true;
		}
		// Antipodal points on the visible hemisphere lie on its equator.
		// Continue a screen-plane half turn, with a deterministic roll axis.
		axis = {0, 0, 1};
	}
	else
		for (auto &value : axis)
			value /= sine;
	if (drag.grid > 0)
	{
		// Choose the nearest supported angle in the shortest-arc interval.
		angle = std::min(std::round(angle / drag.grid), std::floor(180 / drag.grid)) * drag.grid;
	}
	if (angle == 0)
	{
		*rotation = {};
		return true;
	}
	const double radians = angle * std::numbers::pi / 180, c = std::cos(radians), s = std::sin(radians);
	std::array<Vector, 3> columns;
	for (int i = 0; i < 3; ++i)
	{
		// Conjugate the screen-space rotation into the captured model basis.
		// This also supports an orthonormal reflected selection basis.
		const auto v = toBasis(vector(drag.basis.axes[i]), drag.view), perpendicular = cross(axis, v);
		const double along = dot(axis, v) * (1 - c);
		Vector turned;
		for (int j = 0; j < 3; ++j)
			turned[j] = v[j] * c + perpendicular[j] * s + axis[j] * along;
		columns[i] = toBasis(fromBasis(turned, drag.view), drag.basis);
	}
	const auto &a = columns[0], &b = columns[1], &d = columns[2];
	const double horizontal = std::hypot(a[0], a[1]), degrees = 180 / std::numbers::pi;
	// Decompose Rz * Ry * Rx; at a Y-axis singularity choose Z = 0.
	const ModelVec3 candidate{float((horizontal > 1e-7 ? std::atan2(b[2], d[2]) : std::atan2(-d[1], b[1])) * degrees),
							  float(std::atan2(-a[2], horizontal) * degrees),
							  float((horizontal > 1e-7 ? std::atan2(a[1], a[0]) : 0) * degrees)};
	if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) || !std::isfinite(candidate.z))
		return false;
	*rotation = candidate;
	return true;
}
} // namespace vibestudio
