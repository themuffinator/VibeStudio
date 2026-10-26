// The Assemble Brush tool's geometry. Workflow reference: TrenchBroom
// AssembleBrushTool, AssembleBrushToolController3D (DrawFaceDragDelegate,
// DuplicateFaceDragDelegate) and Grid::snap(point, plane) in
// lib/TbUiLib/src and lib/TbMdlLib/src at 4c756be1 (GPL-3.0-or-later): points
// clicked on faces snap to the grid in the face's plane, a face drag proposes
// the grid rectangle on that plane, and a Shift-drag duplicates a flat outline
// along its normal by grid steps. Original implementation over VibeStudio's
// convex envelope (core/level_brush_points.h); see docs/CREDITS.md.

#include "core/level_brush_assemble.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibestudio {
namespace {
// levelBrushPointsHull()'s spread: closer than this to a point, line or
// plane does not count as leaving it.
constexpr double kSpread = 0.02;
constexpr double kLimit = 32768.0;

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

bool finite(const LevelMapVec3& p)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= kLimit && std::abs(p.y) <= kLimit
		&& std::abs(p.z) <= kLimit;
}

LevelMapVec3 minus(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z, true};
}

LevelMapVec3 plus(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z, true};
}

LevelMapVec3 scaled(const LevelMapVec3& a, double factor)
{
	return {a.x * factor, a.y * factor, a.z * factor, true};
}

double dot(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}

double& at(LevelMapVec3& p, int axis)
{
	return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}

double at(const LevelMapVec3& p, int axis)
{
	return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}

LevelMapVec3 normalOf(const MapPlane& plane)
{
	return {plane.normalX, plane.normalY, plane.normalZ, true};
}

bool samePoint(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Folds -0 to 0, so coordinates never read back as "-0".
double tidy(double value)
{
	return value == 0.0 ? 0.0 : value;
}

// The coordinate along the plane's dominant axis that puts `p` on the plane.
bool onPlane(const MapPlane& plane, int axis, LevelMapVec3* p)
{
	const auto normal = normalOf(plane);
	const double across = at(normal, axis);
	if (std::abs(across) < 1e-9) {
		return false;
	}
	double distance = plane.distance;
	for (int i = 0; i < 3; ++i) {
		if (i != axis) {
			distance -= at(normal, i) * at(*p, i);
		}
	}
	at(*p, axis) = tidy(distance / across);
	return std::isfinite(at(*p, axis));
}

// Whether `p`, on the plane, lies within the convex winding (either turn).
bool insideWinding(const QVector<LevelMapVec3>& winding, const LevelMapVec3& normal, const LevelMapVec3& p)
{
	if (winding.size() < 3) {
		return true;
	}
	bool positive = false;
	bool negative = false;
	for (int i = 0; i < winding.size(); ++i) {
		const auto& a = winding[i];
		const auto& b = winding[(i + 1) % winding.size()];
		const auto side = minus(b, a);
		const double length = std::sqrt(dot(side, side));
		if (length < 1e-9) {
			continue;
		}
		const double turn = dot(cross(side, minus(p, a)), normal) / length;
		positive = positive || turn > 0.01;
		negative = negative || turn < -0.01;
	}
	return !(positive && negative);
}

bool normalised(BoxResizeRay ray, LevelMapVec3* origin, LevelMapVec3* direction)
{
	const double length = std::hypot(ray.direction[0], ray.direction[1], ray.direction[2]);
	if (!std::isfinite(length) || length < 1e-12) {
		return false;
	}
	*origin = {ray.origin[0], ray.origin[1], ray.origin[2], true};
	*direction = {ray.direction[0] / length, ray.direction[1] / length, ray.direction[2] / length, true};
	return std::isfinite(origin->x) && std::isfinite(origin->y) && std::isfinite(origin->z);
}

// Andrew's monotone chain over the points projected across `axis`, as
// indices in counter-clockwise order about +axis; collinear points dropped.
QVector<int> flatHull(const QVector<LevelMapVec3>& points, int axis)
{
	const int u = (axis + 1) % 3;
	const int v = (axis + 2) % 3;
	QVector<int> order(points.size());
	for (int i = 0; i < order.size(); ++i) {
		order[i] = i;
	}
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		const double au = at(points[a], u), bu = at(points[b], u);
		return au != bu ? au < bu : at(points[a], v) < at(points[b], v);
	});
	const auto turn = [&](int o, int a, int b) {
		return (at(points[a], u) - at(points[o], u)) * (at(points[b], v) - at(points[o], v))
			- (at(points[a], v) - at(points[o], v)) * (at(points[b], u) - at(points[o], u));
	};
	QVector<int> hull;
	for (int pass = 0; pass < 2; ++pass) {
		const qsizetype floor = hull.size();
		for (qsizetype k = 0; k < order.size(); ++k) {
			const int index = pass == 0 ? order[k] : order[order.size() - 1 - k];
			while (hull.size() >= floor + 2 && turn(hull[hull.size() - 2], hull.last(), index) <= 1e-9) {
				hull.removeLast();
			}
			hull << index;
		}
		hull.removeLast();
	}
	return hull;
}
} // namespace

bool levelBrushAssembleOutline(const QVector<LevelMapVec3>& points, LevelBrushAssembleOutline* outline, QString* error, bool solveVolume)
{
	if (error) {
		error->clear();
	}
	LevelBrushAssembleOutline made;
	if (outline) {
		*outline = {};
	}
	const int count = static_cast<int>(points.size());
	if (count > kLevelBrushPointsMaximum) {
		return fail(error, QCoreApplication::translate("LevelBrushAssemble", "Assemble at most %1 points.").arg(kLevelBrushPointsMaximum));
	}
	for (int i = 0; i < count; ++i) {
		if (!finite(points[i])) {
			return fail(error, QCoreApplication::translate("LevelBrushAssemble", "Point %1 is not a finite position within ±32768 units.").arg(i + 1));
		}
	}
	if (count == 0) {
		if (outline) {
			*outline = made;
		}
		return true;
	}
	// As levelBrushPointsHull() names a flat input: the farthest point from
	// the first, the farthest from that line, then the farthest from the plane.
	const LevelMapVec3& a = points.first();
	int b = -1;
	double best = kSpread * kSpread;
	for (int i = 0; i < count; ++i) {
		const auto d = minus(points[i], a);
		if (dot(d, d) > best) {
			best = dot(d, d);
			b = i;
		}
	}
	if (b < 0) {
		made.shape = LevelBrushAssembleShape::Point;
		made.corners = {a};
		made.unusedPoints = count - 1;
		if (outline) {
			*outline = made;
		}
		return true;
	}
	const auto line = minus(points[b], a);
	int c = -1;
	best = kSpread * kSpread;
	for (int i = 0; i < count; ++i) {
		const auto offset = cross(minus(points[i], a), line);
		const double distance = dot(offset, offset) / dot(line, line);
		if (distance > best) {
			best = distance;
			c = i;
		}
	}
	if (c < 0) {
		// The extreme points along the line are its ends.
		int low = 0;
		int high = 0;
		for (int i = 1; i < count; ++i) {
			const double t = dot(minus(points[i], a), line);
			if (t < dot(minus(points[low], a), line)) {
				low = i;
			}
			if (t > dot(minus(points[high], a), line)) {
				high = i;
			}
		}
		made.shape = LevelBrushAssembleShape::Segment;
		made.corners = {points[low], points[high]};
		made.edges = {{points[low], points[high]}};
		made.unusedPoints = count - 2;
		if (outline) {
			*outline = made;
		}
		return true;
	}
	auto normal = cross(line, minus(points[c], a));
	normal = scaled(normal, 1.0 / std::sqrt(dot(normal, normal)));
	const bool flat = std::none_of(points.cbegin(), points.cend(), [&](const LevelMapVec3& p) { return std::abs(dot(normal, minus(p, a))) > kSpread; });
	if (flat) {
		const std::array<double, 3> components {std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
		const int axis = static_cast<int>(std::max_element(components.cbegin(), components.cend()) - components.cbegin());
		const QVector<int> hull = flatHull(points, axis);
		for (const int index : hull) {
			made.corners << points[index];
		}
		// Newell's normal follows the winding the corners now have.
		LevelMapVec3 newell {0, 0, 0, true};
		for (int i = 0; i < made.corners.size(); ++i) {
			const auto& p = made.corners[i];
			const auto& q = made.corners[(i + 1) % made.corners.size()];
			newell.x += (p.y - q.y) * (p.z + q.z);
			newell.y += (p.z - q.z) * (p.x + q.x);
			newell.z += (p.x - q.x) * (p.y + q.y);
		}
		const double length = std::sqrt(dot(newell, newell));
		if (made.corners.size() < 3 || !std::isfinite(length) || length < 1e-12) {
			return fail(error, QCoreApplication::translate("LevelBrushAssemble", "The flat points do not span an outline."));
		}
		newell = scaled(newell, 1.0 / length);
		made.shape = LevelBrushAssembleShape::Polygon;
		made.plane = {newell.x, newell.y, newell.z, dot(newell, made.corners.first()), true};
		for (int i = 0; i < made.corners.size(); ++i) {
			made.edges << qMakePair(made.corners[i], made.corners[(i + 1) % made.corners.size()]);
		}
		made.unusedPoints = count - static_cast<int>(made.corners.size());
		if (outline) {
			*outline = made;
		}
		return true;
	}
	made.shape = LevelBrushAssembleShape::Volume;
	if (outline) {
		*outline = made;
	}
	if (!solveVolume) {
		return true;
	}
	LevelBrushTopology topology;
	if (!levelBrushPointsHull(points, QStringLiteral("__assemble__"), &topology, error)) {
		return false;
	}
	made.corners = topology.vertices;
	for (const auto& edge : std::as_const(topology.edges)) {
		made.edges << qMakePair(topology.vertices[edge[0]], topology.vertices[edge[1]]);
	}
	made.faces = static_cast<int>(topology.faces.size());
	made.unusedPoints = count - static_cast<int>(topology.vertices.size());
	if (outline) {
		*outline = made;
	}
	return true;
}

int levelBrushAssembleDominantAxis(const MapPlane& plane)
{
	const std::array<double, 3> components {std::abs(plane.normalX), std::abs(plane.normalY), std::abs(plane.normalZ)};
	return static_cast<int>(std::max_element(components.cbegin(), components.cend()) - components.cbegin());
}

LevelMapVec3 levelBrushAssembleSurfacePoint(const LevelMapVec3& hit, const LevelBrushAssembleSurface& surface, double grid)
{
	if (!surface.plane.valid || !std::isfinite(grid) || grid <= 0.0 || !hit.valid) {
		return hit;
	}
	const int axis = levelBrushAssembleDominantAxis(surface.plane);
	LevelMapVec3 snapped = hit;
	for (int i = 0; i < 3; ++i) {
		if (i != axis) {
			at(snapped, i) = snapLevelMapCoordinate(at(hit, i), grid);
		}
	}
	if (!onPlane(surface.plane, axis, &snapped) || !finite(snapped) || !insideWinding(surface.winding, normalOf(surface.plane), snapped)) {
		return hit;
	}
	return snapped;
}

QVector<LevelMapVec3> levelBrushAssembleRectangle(const MapPlane& plane, const LevelMapVec3& from, const LevelMapVec3& to, double grid)
{
	if (!plane.valid) {
		return {};
	}
	const int axis = levelBrushAssembleDominantAxis(plane);
	const int u = (axis + 1) % 3;
	const int v = (axis + 2) % 3;
	const bool snap = std::isfinite(grid) && grid > 0.0;
	const auto low = [&](double value) { return snap ? tidy(std::floor(value / grid) * grid) : value; };
	const auto high = [&](double value) { return snap ? tidy(std::ceil(value / grid) * grid) : value; };
	const double u0 = low(std::min(at(from, u), at(to, u)));
	const double u1 = high(std::max(at(from, u), at(to, u)));
	const double v0 = low(std::min(at(from, v), at(to, v)));
	const double v1 = high(std::max(at(from, v), at(to, v)));
	QVector<LevelMapVec3> corners;
	for (const auto& [cu, cv] : {std::pair {u0, v0}, std::pair {u1, v0}, std::pair {u1, v1}, std::pair {u0, v1}}) {
		LevelMapVec3 corner {0, 0, 0, true};
		at(corner, u) = cu;
		at(corner, v) = cv;
		if (!onPlane(plane, axis, &corner)) {
			return {};
		}
		corners << corner;
	}
	return corners;
}

bool levelBrushAssembleRayPlane(const BoxResizeRay& ray, const MapPlane& plane, LevelMapVec3* point)
{
	LevelMapVec3 origin;
	LevelMapVec3 direction;
	if (!plane.valid || !normalised(ray, &origin, &direction)) {
		return false;
	}
	const auto normal = normalOf(plane);
	const double denominator = dot(normal, direction);
	if (std::abs(denominator) < 1e-6) {
		return false;
	}
	const double distance = (plane.distance - dot(normal, origin)) / denominator;
	if (!std::isfinite(distance) || (ray.forwardOnly && distance <= 0.0)) {
		return false;
	}
	const auto hit = plus(origin, scaled(direction, distance));
	if (!std::isfinite(hit.x) || !std::isfinite(hit.y) || !std::isfinite(hit.z)) {
		return false;
	}
	if (point) {
		*point = hit;
	}
	return true;
}

bool levelBrushAssembleRayPolygon(const BoxResizeRay& ray, const LevelBrushAssembleOutline& outline, LevelMapVec3* point)
{
	LevelMapVec3 hit;
	if (outline.shape != LevelBrushAssembleShape::Polygon || !levelBrushAssembleRayPlane(ray, outline.plane, &hit)
		|| !insideWinding(outline.corners, normalOf(outline.plane), hit)) {
		return false;
	}
	if (point) {
		*point = hit;
	}
	return true;
}

bool levelBrushAssembleExtrusionDistance(const BoxResizeRay& ray, const LevelMapVec3& start, const LevelMapVec3& normal, double grid,
	double* distance)
{
	LevelMapVec3 origin;
	LevelMapVec3 direction;
	const double length = std::sqrt(dot(normal, normal));
	if (!normalised(ray, &origin, &direction) || !std::isfinite(length) || length < 1e-9) {
		return false;
	}
	const auto along = scaled(normal, 1.0 / length);
	// The nearest points of the line start + s·along and the ray.
	const auto w = minus(start, origin);
	const double b = dot(along, direction);
	const double denominator = 1.0 - b * b;
	if (denominator < 1e-6) {
		return false;
	}
	double s = (b * dot(direction, w) - dot(along, w)) / denominator;
	if (!std::isfinite(s)) {
		return false;
	}
	if (std::isfinite(grid) && grid > 0.0) {
		s = snapLevelMapCoordinate(s, grid);
	}
	if (distance) {
		*distance = tidy(s);
	}
	return true;
}

QVector<LevelMapVec3> levelBrushPointsExtruded(const QVector<LevelMapVec3>& points, const LevelMapVec3& offset)
{
	QVector<LevelMapVec3> extruded = points;
	extruded.reserve(points.size() * 2);
	for (const auto& p : points) {
		extruded << LevelMapVec3 {tidy(p.x + offset.x), tidy(p.y + offset.y), tidy(p.z + offset.z), p.valid};
	}
	return extruded;
}

bool levelBrushAssembleAppend(QVector<LevelMapVec3>* points, const QVector<LevelMapVec3>& added, int* appended)
{
	if (appended) {
		*appended = 0;
	}
	if (!points) {
		return false;
	}
	QVector<LevelMapVec3> fresh;
	for (const auto& p : added) {
		const auto same = [&p](const LevelMapVec3& q) { return samePoint(p, q); };
		if (std::none_of(points->cbegin(), points->cend(), same) && std::none_of(fresh.cbegin(), fresh.cend(), same)) {
			fresh << p;
		}
	}
	if (points->size() + fresh.size() > kLevelBrushPointsMaximum) {
		return false;
	}
	*points += fresh;
	if (appended) {
		*appended = static_cast<int>(fresh.size());
	}
	return true;
}
} // namespace vibestudio
