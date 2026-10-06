#include "core/model_triangle_contact.h"
#include "core/model_geometry_helpers.h"
#include <algorithm>
#include <limits>

namespace vibestudio
{
namespace
{
using namespace model_geometry;
using Triangle = std::array<P3, 3>;
bool boxesTouch(const Triangle &a, const Triangle &b)
{
	for (int axis = 0; axis < 3; ++axis)
	{
		const auto coordinate = [axis](P3 p) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; };
		double aMin = coordinate(a[0]), aMax = aMin, bMin = coordinate(b[0]), bMax = bMin;
		for (int i = 1; i < 3; ++i)
		{
			aMin = std::min(aMin, coordinate(a[i]));
			aMax = std::max(aMax, coordinate(a[i]));
			bMin = std::min(bMin, coordinate(b[i]));
			bMax = std::max(bMax, coordinate(b[i]));
		}
		if (aMax < bMin || bMax < aMin)
		{
			return false;
		}
	}
	return true;
}
int dominant(P3 p)
{
	return std::abs(p.x) >= std::abs(p.y) && std::abs(p.x) >= std::abs(p.z) ? 0 : std::abs(p.y) >= std::abs(p.z) ? 1 : 2;
}
bool coplanarOverlap(const Triangle &a, const Triangle &b, P3 normal, double epsilon)
{
	const int axis = dominant(normal);
	QVector<P2> polygon;
	for (auto p : a)
	{
		polygon << project(p, axis);
	}
	const std::array<P2, 3> clip{project(b[0], axis), project(b[1], axis), project(b[2], axis)};
	const double sign = orient(clip[0], clip[1], clip[2]) > 0 ? 1 : -1;
	// Clip two convex triangles. Positive common area is an overlap even when
	// opposite windings or distinct UV-seam indices describe the same geometry.
	for (int edge = 0; edge < 3 && !polygon.isEmpty(); ++edge)
	{
		QVector<P2> output;
		const auto from = clip[edge], to = clip[(edge + 1) % 3];
		for (int i = 0; i < polygon.size(); ++i)
		{
			const auto start = polygon[i], end = polygon[(i + 1) % polygon.size()];
			const double ds = sign * orient(from, to, start), de = sign * orient(from, to, end);
			if ((ds >= 0) != (de >= 0))
			{
				const double t = ds / (ds - de);
				output << P2{start.x + t * (end.x - start.x), start.y + t * (end.y - start.y)};
			}
			if (de >= 0)
			{
				output << end;
			}
		}
		polygon = std::move(output);
	}
	return std::abs(area(polygon)) > epsilon;
}
QVector<P3> planeSlice(const Triangle &t, const std::array<double, 3> &distances, double epsilon)
{
	QVector<P3> points;
	for (int i = 0; i < 3; ++i)
	{
		const int j = (i + 1) % 3;
		if (std::abs(distances[i]) <= epsilon)
		{
			points << t[i];
		}
		if ((distances[i] > epsilon && distances[j] < -epsilon) || (distances[i] < -epsilon && distances[j] > epsilon))
		{
			points << t[i] + (t[j] - t[i]) * (distances[i] / (distances[i] - distances[j]));
		}
	}
	return points;
}
bool boundaryPoint(const Triangle &t, P3 p, double epsilon)
{
	for (int i = 0; i < 3; ++i)
	{
		const auto direction = t[(i + 1) % 3] - t[i];
		const double square = dot(direction, direction);
		if (square > 0)
		{
			const double fraction = std::clamp(dot(p - t[i], direction) / square, 0.0, 1.0);
			if (length(p - (t[i] + direction * fraction)) <= epsilon)
			{
				return true;
			}
		}
	}
	return false;
}
ModelTriangleContact contact(Triangle a, Triangle b)
{
	if (!boxesTouch(a, b))
	{
		return ModelTriangleContact::None;
	}
	const auto origin = a[0];
	for (int i = 0; i < 3; ++i)
	{
		a[i] = a[i] - origin;
		b[i] = b[i] - origin;
	}
	auto na = cross(a[1] - a[0], a[2] - a[0]), nb = cross(b[1] - b[0], b[2] - b[0]);
	const double areaA = length(na), areaB = length(nb);
	if (areaA == 0 || areaB == 0)
	{
		return ModelTriangleContact::Degenerate;
	}
	na = na * (1 / areaA);
	nb = nb * (1 / areaB);
	double scaleA = 0, scaleB = 0;
	for (int i = 0; i < 3; ++i)
	{
		scaleA = std::max(scaleA, length(a[i] - a[(i + 1) % 3]));
		scaleB = std::max(scaleB, length(b[i] - b[(i + 1) % 3]));
	}
	const double epsilon = std::min(scaleA, scaleB) * 1e-10 + std::max(scaleA, scaleB) * 64 * std::numeric_limits<double>::epsilon();
	std::array<double, 3> da, db;
	for (int i = 0; i < 3; ++i)
	{
		da[i] = dot(a[i] - b[0], nb);
		db[i] = dot(b[i] - a[0], na);
	}
	const auto separate = [epsilon](const auto &distances) {
		return std::all_of(distances.begin(), distances.end(), [epsilon](double d) { return d > epsilon; }) ||
			   std::all_of(distances.begin(), distances.end(), [epsilon](double d) { return d < -epsilon; });
	};
	if (separate(da) || separate(db))
	{
		return ModelTriangleContact::None;
	}
	const auto onPlane = [epsilon](const auto &distances) {
		return std::all_of(distances.begin(), distances.end(), [epsilon](double d) { return std::abs(d) <= epsilon; });
	};
	if (onPlane(da) && onPlane(db))
	{
		return coplanarOverlap(a, b, na, std::min(areaA, areaB) * 1e-10 + epsilon * epsilon) ? ModelTriangleContact::CoplanarOverlap
																							 : ModelTriangleContact::None;
	}
	const auto sliceA = planeSlice(a, da, epsilon), sliceB = planeSlice(b, db, epsilon);
	if (sliceA.isEmpty() || sliceB.isEmpty())
	{
		return ModelTriangleContact::None;
	}
	// Crossing almost parallel normals loses the shared edge's direction
	// through cancellation. Use the longest plane-slice segment instead;
	// boundary vertices in a slice retain their exact positions.
	P3 line{0, 0, 0};
	double lineLength = 0;
	for (const auto *slice : {&sliceA, &sliceB})
	{
		for (int i = 0; i < slice->size(); ++i)
		{
			for (int j = i + 1; j < slice->size(); ++j)
			{
				const auto segment = (*slice)[j] - (*slice)[i];
				const double size = length(segment);
				if (size > lineLength)
				{
					line = segment;
					lineLength = size;
				}
			}
		}
	}
	if (lineLength <= epsilon)
	{
		return ModelTriangleContact::None;
	}
	line = line * (1 / lineLength);
	const auto interval = [line](const auto &slice) {
		std::pair<double, double> range{dot(slice[0], line), dot(slice[0], line)};
		for (auto p : slice)
		{
			const double d = dot(p, line);
			range.first = std::min(range.first, d);
			range.second = std::max(range.second, d);
		}
		return range;
	};
	const auto rangeA = interval(sliceA), rangeB = interval(sliceB);
	const double low = std::max(rangeA.first, rangeB.first), high = std::min(rangeA.second, rangeB.second);
	if (high - low <= epsilon)
	{
		return ModelTriangleContact::None; // Isolated point contact is allowed.
	}
	const auto midpoint = sliceA[0] + line * ((low + high) * .5 - dot(sliceA[0], line));
	// Shared geometric boundary edges are legal, including separate UV copies.
	// An interval entering either triangle's interior is a new intersection.
	return !boundaryPoint(a, midpoint, epsilon) || !boundaryPoint(b, midpoint, epsilon) ? ModelTriangleContact::Crossing
																						: ModelTriangleContact::None;
}
} // namespace

ModelTriangleContact modelTriangleContact(const std::array<ModelVec3, 3> &a, const std::array<ModelVec3, 3> &b)
{
	for (const auto *vertices : {&a, &b})
		for (const auto p : *vertices)
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
				return ModelTriangleContact::Degenerate;
	return contact({point(a[0]), point(a[1]), point(a[2])}, {point(b[0]), point(b[1]), point(b[2])});
}
} // namespace vibestudio
