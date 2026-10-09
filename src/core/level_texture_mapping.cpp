/*
 * Texture projection conventions adapted from NetRadiant Custom q3map2,
 * tools/quake3/q3map2/map.cpp (TextureAxisFromPlane, QuakeTextureVecs) and
 * tools/quake3/common/qmath.h (ComputeAxisBase), revision
 * 68ecbed64b7be78741878c730279b5471d978c7c. See docs/CREDITS.md.
 *
 * Copyright (C) 1999-2007 id Software, Inc. and contributors.
 * For a list of contributors, see the accompanying CONTRIBUTORS file in
 * external/compilers/vibemap3.
 *
 * This file is part of GtkRadiant.
 *
 * GtkRadiant is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * GtkRadiant is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GtkRadiant; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * VibeStudio's double-precision rotation and inverse mapping solver below
 * are original implementations. The notice above covers the conventions.
 */
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
double dot(const LevelMapVec3 &a, const LevelMapVec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 scale(const LevelMapVec3 &a, double s) { return {a.x * s, a.y * s, a.z * s, a.valid}; }
LevelMapVec3 add(const LevelMapVec3 &a, const LevelMapVec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.valid && b.valid}; }
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, a.valid && b.valid};
}
double component(const LevelMapVec3 &p, int index) { return index == 0 ? p.x : index == 1 ? p.y : p.z; }
void setComponent(LevelMapVec3 *p, int index, double value) { (index == 0 ? p->x : index == 1 ? p->y : p->z) = value; }
bool finite(const LevelMapVec3 &p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
MapPlane plane(const LevelMapBrushFace &face)
{
	if (!face.explicitPlane) {
		return planeFromPoints(face.p0, face.p1, face.p2);
	}
	const double length = std::sqrt(dot(face.planeNormal, face.planeNormal));
	if (!finite(face.planeNormal) || !std::isfinite(length) || length < 1e-12) {
		return {};
	}
	return {face.planeNormal.x / length, face.planeNormal.y / length, face.planeNormal.z / length, -face.planeDistance / length, true};
}
LevelMapVec3 normal(const MapPlane &p) { return {p.normalX, p.normalY, p.normalZ, p.valid}; }

// q3map2's tie order is Z, X, Y with a 0.0001 preference threshold.
std::array<int, 3> classicAxes(const LevelMapVec3 &n)
{
	int dropped = 2;
	double best = std::abs(n.z);
	if (std::abs(n.x) > best + 0.0001) {
		dropped = 0;
		best = std::abs(n.x);
	}
	if (std::abs(n.y) > best + 0.0001) {
		dropped = 1;
	}
	return {dropped == 0 ? 1 : 0, dropped == 2 ? 1 : 2, dropped};
}
std::pair<double, double> sinCos(double degrees)
{
	degrees = std::remainder(degrees, 360.0);
	// Exact axial turns prevent drift on repeated quarter-turn commands.
	if (degrees == 0) {
		return {0, 1};
	}
	if (degrees == 90) {
		return {1, 0};
	}
	if (degrees == -90) {
		return {-1, 0};
	}
	if (std::abs(degrees) == 180) {
		return {0, -1};
	}
	const double radians = degrees * std::numbers::pi / 180.0;
	return {std::sin(radians), std::cos(radians)};
}
std::pair<LevelMapVec3, LevelMapVec3> primitiveBasis(const LevelMapVec3 &n)
{
	// Same pole convention and handedness as q3map2 ComputeAxisBase.
	if (std::abs(n.x) < 1e-6 && std::abs(n.y) < 1e-6 && std::abs(std::abs(n.z) - 1) < 1e-6) {
		return {{0, 1, 0, true}, {n.z > 0 ? 1.0 : -1.0, 0, 0, true}};
	}
	const double h = std::hypot(n.x, n.y);
	return {{-n.y / h, n.x / h, 0, true}, {n.x * n.z / h, n.y * n.z / h, -h, true}};
}
bool fitClassic(const LevelTextureProjection &projection, LevelMapBrushFace *face)
{
	const auto p = plane(*face);
	const auto n = normal(p);
	const auto indices = classicAxes(n);
	const double uNormal = component(projection.u, indices[2]) / component(n, indices[2]);
	const double vNormal = component(projection.v, indices[2]) / component(n, indices[2]);
	const auto u = add(projection.u, scale(n, -uNormal));
	const auto v = add(projection.v, scale(n, -vNormal));
	const double ux = component(u, indices[0]), uy = component(u, indices[1]);
	const double vx = component(v, indices[0]), vy = component(v, indices[1]);
	const double ul = std::hypot(ux, uy), vl = std::hypot(vx, vy);
	if (ul < 1e-12 || vl < 1e-12 || std::abs(ux * vx + uy * vy) > 1e-9 * ul * vl) {
		return false;
	}
	const double cos = ux / ul, sin = uy / ul;
	face->rotation = std::atan2(sin, cos) * 180.0 / std::numbers::pi;
	face->scaleX = 1 / ul;
	face->scaleY = 1 / (vx * sin - vy * cos);
	face->shiftX = projection.offsetU + uNormal * p.distance;
	face->shiftY = projection.offsetV + vNormal * p.distance;
	return std::isfinite(face->scaleY);
}
} // namespace

QPointF LevelTextureProjection::at(const LevelMapVec3 &point) const { return {dot(u, point) + offsetU, dot(v, point) + offsetV}; }

QString levelMapBrushDialect(const LevelMapDocument& document)
{
	QSet<QString> kinds;
	for (const auto& brush : document.brushes) {
		for (const auto& face : brush.faces) {
			kinds.insert(face.explicitTextureMatrix ? (face.explicitPlane ? QStringLiteral("brushDef3") : QStringLiteral("brushDef"))
				: face.explicitTextureAxes ? QStringLiteral("valve220") : QStringLiteral("classic"));
		}
	}
	return kinds.size() > 1 ? QStringLiteral("mixed") : kinds.isEmpty() ? QString() : *kinds.cbegin();
}

LevelTextureProjection levelTextureProjection(const LevelMapBrushFace &face)
{
	LevelTextureProjection result;
	const auto p = plane(face);
	if (!p.valid) {
		return result;
	}
	if (face.explicitTextureMatrix) {
		const auto [s, t] = primitiveBasis(normal(p));
		const auto &m = face.textureMatrix;
		result.u = add(scale(s, m[0]), scale(t, m[1]));
		result.v = add(scale(s, m[3]), scale(t, m[4]));
		result.offsetU = m[2];
		result.offsetV = m[5];
		result.normalizedCoordinates = true;
	} else {
		// q3map2 interprets a legacy zero scale as one.
		const double sx = face.scaleX == 0 ? 1 : face.scaleX, sy = face.scaleY == 0 ? 1 : face.scaleY;
		if (face.explicitTextureAxes) {
			result.u = scale(face.uAxis, 1 / sx);
			result.v = scale(face.vAxis, 1 / sy);
			result.offsetU = face.uOffset;
			result.offsetV = face.vOffset;
		} else {
			const auto axes = classicAxes(normal(p));
			const auto [sin, cos] = sinCos(face.rotation);
			result.u = {0, 0, 0, true};
			result.v = result.u;
			setComponent(&result.u, axes[0], cos / sx);
			setComponent(&result.u, axes[1], sin / sx);
			setComponent(&result.v, axes[0], sin / sy);
			setComponent(&result.v, axes[1], -cos / sy);
			result.offsetU = face.shiftX;
			result.offsetV = face.shiftY;
		}
	}
	result.valid = finite(result.u) && finite(result.v) && std::isfinite(result.offsetU) && std::isfinite(result.offsetV);
	return result;
}

LevelTextureProjection wrappedLevelTextureProjection(const LevelMapBrushFace& source, const LevelMapBrushFace& target)
{
	// Behavior reference: NetRadiant Custom Face_setTexture/ePasteSeamless,
	// radiant/surfacedialog.cpp at 68ecbed6 (GPL-2.0-or-later). See credits.
	// Original double-precision Rodrigues implementation; no sampled UV fitting.
	auto result = levelTextureProjection(source);
	const auto from = plane(source), to = plane(target);
	if (!result.valid || !from.valid || !to.valid) { return {}; }
	const auto a = normal(from), b = normal(to), axis = cross(a, b);
	const double squared = dot(axis, axis);
	// Parallel planes have no unique hinge. Like the reference editor, project
	// unchanged, including for opposite normals and nearly parallel planes.
	if (squared <= 1e-10) { return result; }
	const double sine = std::sqrt(squared), cosine = std::clamp(dot(a, b), -1.0, 1.0);
	const auto unit = scale(axis, 1 / sine);
	const auto pivot = scale(add(scale(cross(b, axis), from.distance), scale(cross(axis, a), to.distance)), 1 / squared);
	const auto rotate = [&](const LevelMapVec3& vector) {
		return add(add(scale(vector, cosine), scale(cross(unit, vector), sine)), scale(unit, dot(unit, vector) * (1 - cosine)));
	};
	const auto u = rotate(result.u), v = rotate(result.v);
	result.offsetU += dot(add(result.u, scale(u, -1)), pivot);
	result.offsetV += dot(add(result.v, scale(v, -1)), pivot);
	result.u = u; result.v = v;
	result.valid = finite(u) && finite(v) && std::isfinite(result.offsetU) && std::isfinite(result.offsetV);
	return result;
}

bool setLevelTextureProjection(LevelMapBrushFace* face, const LevelTextureProjection& projection, bool allowValve220, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!face || !projection.valid || !finite(projection.u) || !finite(projection.v)
		|| !std::isfinite(projection.offsetU) || !std::isfinite(projection.offsetV)) {
		return fail(QCoreApplication::translate("LevelSurface", "The copied texture projection is not finite."));
	}
	const auto p = plane(*face);
	if (!p.valid || projection.normalizedCoordinates != face->explicitTextureMatrix) {
		return fail(QCoreApplication::translate("LevelSurface", "The target plane or texture-coordinate units are incompatible."));
	}
	auto result = *face;
	if (result.explicitTextureMatrix) {
		const auto n = normal(p);
		const auto [s, t] = primitiveBasis(n);
		result.textureMatrix = {dot(projection.u, s), dot(projection.u, t), projection.offsetU + dot(projection.u, n) * p.distance,
			dot(projection.v, s), dot(projection.v, t), projection.offsetV + dot(projection.v, n) * p.distance};
	} else if (result.explicitTextureAxes) {
		result.uAxis = projection.u; result.vAxis = projection.v;
		result.uOffset = projection.offsetU; result.vOffset = projection.offsetV;
		result.scaleX = 1; result.scaleY = 1; result.rotation = 0;
	} else if (!fitClassic(projection, &result)) {
		if (!allowValve220) {
			return fail(QCoreApplication::translate("LevelSurface", "This projection needs explicit texture axes. Enable Valve 220 conversion for this paste."));
		}
		result.explicitTextureAxes = true;
		result.uAxis = projection.u; result.vAxis = projection.v;
		result.uOffset = projection.offsetU; result.vOffset = projection.offsetV;
		result.scaleX = 1; result.scaleY = 1; result.rotation = 0;
	}
	*face = result;
	return true;
}

LevelMapVec3 rotateLevelVector(const LevelMapVec3 &vector, int axis, double degrees)
{
	if (axis < 0 || axis > 2 || !finite(vector) || !std::isfinite(degrees)) {
		return {};
	}
	const auto [s, c] = sinCos(degrees);
	if (axis == 0) {
		return {vector.x, c * vector.y - s * vector.z, s * vector.y + c * vector.z, true};
	}
	if (axis == 1) {
		return {c * vector.x + s * vector.z, vector.y, -s * vector.x + c * vector.z, true};
	}
	return {c * vector.x - s * vector.y, s * vector.x + c * vector.y, vector.z, true};
}
LevelMapVec3 rotateLevelPoint(const LevelMapVec3 &point, const LevelMapVec3 &pivot, int axis, double degrees)
{
	return add(rotateLevelVector(add(point, scale(pivot, -1)), axis, degrees), pivot);
}

LevelMapVec3 rotateLevelAngles(const LevelMapVec3 &angles, int axis, double degrees)
{
	if (!finite(angles)) {
		return {};
	}
	const auto transform = [&](const LevelMapVec3 &p) {
		return rotateLevelVector(rotateLevelVector(rotateLevelVector(rotateLevelVector(p, 0, angles.z), 1, angles.x), 2, angles.y), axis,
								 degrees);
	};
	const auto forward = transform({1, 0, 0, true});
	const auto side = transform({0, 1, 0, true});
	const auto up = transform({0, 0, 1, true});
	if (!forward.valid || !side.valid || !up.valid) {
		return {};
	}
	const double pitch = std::atan2(-forward.z, std::hypot(forward.x, forward.y));
	const bool pole = std::hypot(forward.x, forward.y) < 1e-10;
	const double yaw = pole ? std::atan2(-side.x, side.y) : std::atan2(forward.y, forward.x);
	const double roll = pole ? 0 : std::atan2(side.z, up.z);
	return {pitch * 180 / std::numbers::pi, yaw * 180 / std::numbers::pi, roll * 180 / std::numbers::pi, true};
}

bool lockTransformedLevelTexture(const LevelMapBrushFace &before, LevelMapBrushFace *after, const LevelTextureAffine &transform,
								 bool allowValve220, QString *error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString &message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!after) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "Missing face to texture-lock."));
	}
	const auto cross = [](const LevelMapVec3 &a, const LevelMapVec3 &b) {
		return LevelMapVec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
	};
	const auto &a = transform.columns[0], &b = transform.columns[1], &c = transform.columns[2];
	const auto x = cross(b, c), y = cross(c, a), z = cross(a, b);
	const double determinant = dot(a, x);
	const double magnitude = std::sqrt(dot(a, a)) * std::sqrt(dot(b, b)) * std::sqrt(dot(c, c));
	if (!finite(a) || !finite(b) || !finite(c) || !finite(transform.translation) || !std::isfinite(magnitude) ||
		!std::isfinite(determinant) || magnitude == 0 || std::abs(determinant) <= 1e-12 * magnitude) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The texture transform is non-finite or collapses an axis."));
	}
	const auto covector = [&](const LevelMapVec3 &value) {
		return scale(add(add(scale(x, value.x), scale(y, value.y)), scale(z, value.z)), 1 / determinant);
	};
	const auto original = levelTextureProjection(before);
	const auto p = plane(*after);
	if (!original.valid || !p.valid) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The face has an invalid plane or texture projection."));
	}
	LevelTextureProjection locked = original;
	locked.u = covector(original.u);
	locked.v = covector(original.v);
	locked.offsetU -= dot(transform.translation, locked.u);
	locked.offsetV -= dot(transform.translation, locked.v);
	LevelMapBrushFace result = *after;
	if (before.explicitTextureMatrix) {
		const auto n = normal(p);
		const auto [s, t] = primitiveBasis(n);
		result.textureMatrix = {dot(locked.u, s), dot(locked.u, t), locked.offsetU + dot(locked.u, n) * p.distance,
								dot(locked.v, s), dot(locked.v, t), locked.offsetV + dot(locked.v, n) * p.distance};
	} else if (before.explicitTextureAxes) {
		result.uAxis = covector(before.uAxis);
		result.vAxis = covector(before.vAxis);
		result.uOffset = locked.offsetU;
		result.vOffset = locked.offsetV;
		result.scaleX = before.scaleX == 0 ? 1 : before.scaleX;
		result.scaleY = before.scaleY == 0 ? 1 : before.scaleY;
	} else if (fitClassic(locked, &result)) {
		// The classic dialect can represent this transform exactly on the face.
	} else {
		if (!before.explicitTextureAxes && !allowValve220) {
			return fail(QCoreApplication::translate(
				"VibeStudioLevelMap", "This transform needs explicit texture axes. Enable Valve 220 conversion or turn texture lock off."));
		}
		result.explicitTextureAxes = true;
		result.uAxis = locked.u;
		result.vAxis = locked.v;
		result.uOffset = locked.offsetU;
		result.vOffset = locked.offsetV;
		result.scaleX = 1;
		result.scaleY = 1;
		result.rotation = 0;
	}
	const auto check = levelTextureProjection(result);
	if (!check.valid) {
		return fail(QCoreApplication::translate("VibeStudioLevelMap", "The transformed texture projection is not finite."));
	}
	result.textureParametersDirty = true;
	*after = result;
	return true;
}

bool lockRotatedLevelTexture(const LevelMapBrushFace &before, LevelMapBrushFace *after, int axis, double degrees, const LevelMapVec3 &pivot,
							 bool allowValve220, QString *error)
{
	LevelTextureAffine transform;
	for (auto &column : transform.columns) {
		column = rotateLevelVector(column, axis, degrees);
	}
	transform.translation = rotateLevelPoint({0, 0, 0, true}, pivot, axis, degrees);
	return lockTransformedLevelTexture(before, after, transform, allowValve220, error);
}
} // namespace vibestudio
