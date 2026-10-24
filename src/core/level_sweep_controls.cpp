#include "core/level_sweep_controls.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio
{
namespace
{
using V = LevelMapVec3;
V add(V a, V b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z, true};
}
V sub(V a, V b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z, true};
}
V mul(V a, double s)
{
	return {a.x * s, a.y * s, a.z * s, true};
}
double dot(V a, V b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
V cross(V a, V b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
bool finite(V a)
{
	return a.valid && std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
struct Quaternion {
	double w = 1;
	V v{0, 0, 0, true};
};
bool quaternion(V axis, double degrees, Quaternion* q)
{
	if (!q || !finite(axis) || !std::isfinite(degrees)) {
		return false;
	}
	const double radians = std::remainder(degrees, 360.0) * std::numbers::pi / 180;
	if (std::abs(radians) < 1e-12) {
		*q = {};
		return true;
	}
	const double length = std::hypot(axis.x, axis.y, axis.z);
	if (!std::isfinite(length) || length < 1e-9) {
		return false;
	}
	*q = {std::cos(radians / 2), mul(axis, std::sin(radians / 2) / length)};
	return true;
}
V rotate(V p, const Quaternion& q)
{
	return add(p, add(mul(cross(q.v, p), 2 * q.w), mul(cross(q.v, cross(q.v, p)), 2)));
}
bool transform(const LevelSweepRequest& r, Quaternion* q)
{
	return finite(r.translation) && finite(r.scale) && std::min({r.scale.x, r.scale.y, r.scale.z}) >= .01 &&
	       quaternion(r.rotationAxis, r.angle, q);
}
bool fail(QString* error)
{
	if (error) {
		*error = QCoreApplication::translate("LevelSweepControls",
		                                     "The sweep destination needs a finite transform, a non-zero scale arm and a valid grid.");
	}
	return false;
}
// The closest intersection with a grid plane through the rounded target,
// matching TB Grid::snapToGridPlane (Kristian Duske, GPL-3.0-or-later,
// 4c756be1a54a28d921d18c77716a5f170bdc31d0). Original implementation.
V snappedOnArm(V centre, V arm, V target, double grid)
{
	const double length = std::sqrt(dot(arm, arm));
	const auto direction = mul(arm, 1 / length);
	const double distance = dot(sub(target, centre), direction);
	double nearest = distance, best = std::numeric_limits<double>::infinity();
	if (grid > 0) {
		const auto point = add(centre, mul(direction, distance));
		const double p[]{point.x, point.y, point.z}, c[]{centre.x, centre.y, centre.z}, d[]{direction.x, direction.y, direction.z};
		for (int i = 0; i < 3; ++i) {
			if (std::abs(d[i]) > 1e-9) {
				const double candidate = (std::round(p[i] / grid) * grid - c[i]) / d[i], difference = std::abs(candidate - distance);
				if (difference < best) {
					best = difference;
					nearest = candidate;
				}
			}
		}
	}
	return add(centre, mul(direction, nearest));
}
} // namespace
V levelSweepDestinationCentre(const LevelSweepSource& source, const LevelSweepRequest& r)
{
	return add(source.centre, r.translation);
}
V levelSweepScaleHandle(const LevelSweepSource& source, const LevelSweepRequest& r)
{
	Quaternion q;
	if (!transform(r, &q) || !finite(source.scaleArm) || !finite(source.centre)) {
		return {};
	}
	return add(levelSweepDestinationCentre(source, r),
	           rotate({source.scaleArm.x * r.scale.x, source.scaleArm.y * r.scale.y, source.scaleArm.z * r.scale.z, true}, q));
}
QVector<QVector<V>> levelSweepDestinationCaps(const LevelSweepSource& source, const LevelSweepRequest& r)
{
	Quaternion q;
	QVector<QVector<V>> result;
	if (!transform(r, &q) || !finite(source.centre) || r.iterations < 1 || r.iterations > 8) {
		return result;
	}
	for (const auto& profile : source.profiles) {
		auto points = profile.vertices;
		for (int iteration = 0; iteration < r.iterations; ++iteration) {
			for (auto& point : points) {
				const auto p = sub(point, source.centre);
				point = add(levelSweepDestinationCentre(source, r), rotate({p.x * r.scale.x, p.y * r.scale.y, p.z * r.scale.z, true}, q));
			}
			result << points;
		}
	}
	return result;
}
bool rotateLevelSweepDestination(LevelSweepRequest* r, V axis, double degrees, QString* error)
{
	if (error) {
		error->clear();
	}
	Quaternion initial, delta;
	if (!r || !transform(*r, &initial) || !quaternion(axis, degrees, &delta)) {
		return fail(error);
	}
	Quaternion q{delta.w * initial.w - dot(delta.v, initial.v),
	             add(add(mul(initial.v, delta.w), mul(delta.v, initial.w)), cross(delta.v, initial.v))};
	const double norm = std::sqrt(q.w * q.w + dot(q.v, q.v));
	if (!std::isfinite(norm) || norm < 1e-9) {
		return fail(error);
	}
	const double sign = q.w < 0 ? -1 : 1;
	q.w *= sign / norm;
	q.v = mul(q.v, sign / norm);
	const double sine = std::sqrt(dot(q.v, q.v));
	auto candidate = *r;
	candidate.rotationAxis = sine < 1e-9 ? V{0, 0, 1, true} : mul(q.v, 1 / sine);
	candidate.angle = sine < 1e-9 ? 0 : 2 * std::atan2(sine, std::clamp(q.w, 0.0, 1.0)) * 180 / std::numbers::pi;
	*r = std::move(candidate);
	return true;
}
bool scaleLevelSweepTo(const LevelSweepSource& source, LevelSweepRequest* r, V position, double grid, QString* error)
{
	if (error) {
		error->clear();
	}
	Quaternion q;
	if (!r || !transform(*r, &q) || !finite(position) || !finite(source.centre) || !finite(source.scaleArm) || !std::isfinite(grid) ||
	    grid < 0 || grid > 32768 || dot(source.scaleArm, source.scaleArm) < 1e-9) {
		return fail(error);
	}
	const auto centre = levelSweepDestinationCentre(source, *r), arm = rotate(source.scaleArm, q),
	           current = sub(levelSweepScaleHandle(source, *r), centre);
	if (!finite(current) || dot(current, current) < 1e-12) {
		return fail(error);
	}
	const auto snapped = snappedOnArm(centre, current, position, grid);
	const double factor = std::max(.01, dot(sub(snapped, centre), arm) / dot(arm, arm));
	if (!std::isfinite(factor) || factor > 10000) {
		return fail(error);
	}
	r->scale = {factor, factor, factor, true};
	return true;
}
bool stepLevelSweepScale(const LevelSweepSource& source, LevelSweepRequest* r, double distance, double grid, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!r || !std::isfinite(distance)) {
		return fail(error);
	}
	const auto centre = levelSweepDestinationCentre(source, *r), arm = sub(levelSweepScaleHandle(source, *r), centre);
	const double largest = std::max({std::abs(arm.x), std::abs(arm.y), std::abs(arm.z)});
	if (!finite(arm) || largest < 1e-9) {
		return fail(error);
	}
	return scaleLevelSweepTo(source, r, add(centre, mul(arm, 1 + distance / largest)), grid, error);
}
} // namespace vibestudio
