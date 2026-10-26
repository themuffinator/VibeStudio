#include "core/level_primitive.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
struct PrimitiveText {
	Q_DECLARE_TR_FUNCTIONS(LevelPrimitive)
};
double coordinate(const LevelMapVec3 &p, int axis) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
bool finite(const LevelMapVec3 &p)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
		   std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) <= 32768;
}
double rounded(double value) { return std::round(value * 1e6) / 1e6 + 0.0; }
bool radialShape(const QString &shape)
{
	return shape == QStringLiteral("cylinder") || shape == QStringLiteral("cone") || shape == QStringLiteral("sphere");
}

// NetRadiant Custom's fit (Workflow reference: NRC radiant/brushmanip.cpp
// Brush_ConstructPrism, Brush_ConstructCone and Brush_ConstructSphere at
// 449778b3, GPL-2.0-or-later; original implementation, see docs/CREDITS.md).
// NRC writes each side as a plane tangent to the circle; these are the
// corners of the same solid, so the shared hull builder makes the brush.
QVector<LevelMapVec3> circlePoints(const LevelBrushPrimitiveRequest &request)
{
	const int x = (request.axis + 1) % 3, y = (request.axis + 2) % 3, z = request.axis;
	std::array<double, 3> centre{}, half{}, low{}, high{};
	for (int axis = 0; axis < 3; ++axis) {
		low[axis] = coordinate(request.mins, axis);
		high[axis] = coordinate(request.maxs, axis);
		centre[axis] = (low[axis] + high[axis]) / 2;
		half[axis] = (high[axis] - low[axis]) / 2;
	}
	QVector<LevelMapVec3> points;
	const auto add = [&points](const std::array<double, 3> &p) { points << LevelMapVec3{rounded(p[0]), rounded(p[1]), rounded(p[2]), true}; };
	const int sides = request.sides;
	if (request.shape == QStringLiteral("sphere")) {
		// Corners on a sphere as wide as the largest extent, a corner on the
		// first axis of each latitude ring and the poles on the depth axis.
		const double radius = std::max({half[0], half[1], half[2]});
		std::array<double, 3> pole = centre;
		pole[z] = centre[z] - radius;
		add(pole);
		for (int band = 1; band < request.bands; ++band) {
			const double latitude = std::numbers::pi * band / request.bands;
			const double ring = radius * std::sin(latitude);
			for (int side = 0; side < sides; ++side) {
				const double angle = 2 * std::numbers::pi * side / sides;
				std::array<double, 3> p = centre;
				p[x] = centre[x] + ring * std::cos(angle);
				p[y] = centre[y] + ring * std::sin(angle);
				p[z] = centre[z] - radius * std::cos(latitude);
				add(p);
			}
		}
		pole[z] = centre[z] + radius;
		add(pole);
		return points;
	}
	// Each side touches the circle at i * 360 / sides degrees, so its corners
	// lie halfway between, on the circumscribed circle.
	const double radius = std::max(half[x], half[y]) / std::cos(std::numbers::pi / sides);
	const auto ring = [&](double depth) {
		for (int side = 0; side < sides; ++side) {
			const double angle = std::numbers::pi * (2 * side + 1) / sides;
			std::array<double, 3> p{};
			p[x] = centre[x] + radius * std::cos(angle);
			p[y] = centre[y] + radius * std::sin(angle);
			p[z] = depth;
			add(p);
		}
	};
	ring(low[z]);
	if (request.shape == QStringLiteral("cylinder")) {
		ring(high[z]);
	} else {
		std::array<double, 3> apex = centre;
		apex[z] = high[z];
		add(apex);
	}
	return points;
}
} // namespace

QString levelBrushPrimitiveLabel(const QString &shape)
{
	if (shape == QStringLiteral("box")) {
		return PrimitiveText::tr("Box");
	}
	if (shape == QStringLiteral("wedge")) {
		return PrimitiveText::tr("Wedge");
	}
	if (shape == QStringLiteral("cylinder")) {
		return PrimitiveText::tr("Cylinder");
	}
	if (shape == QStringLiteral("cone")) {
		return PrimitiveText::tr("Cone");
	}
	if (shape == QStringLiteral("sphere")) {
		return PrimitiveText::tr("Sphere");
	}
	return shape;
}

QString levelBrushPrimitiveFitId(LevelBrushPrimitiveFit fit)
{
	return fit == LevelBrushPrimitiveFit::Circle ? QStringLiteral("circle") : QStringLiteral("box");
}

bool levelBrushPrimitiveFitFromId(const QString &id, LevelBrushPrimitiveFit *fit)
{
	const QString wanted = id.trimmed().toLower();
	if (wanted != QStringLiteral("box") && wanted != QStringLiteral("circle")) {
		return false;
	}
	if (fit) {
		*fit = wanted == QStringLiteral("circle") ? LevelBrushPrimitiveFit::Circle : LevelBrushPrimitiveFit::Box;
	}
	return true;
}

int levelBrushPrimitiveMaxSides(const QString &shape)
{
	// A cylinder has sides + 2 faces and 2 × sides corners, a cone sides + 1
	// of each; a sphere is bounded by sides × bands instead.
	if (shape == QStringLiteral("cylinder")) {
		return kLevelBrushComponentMaxFaces - 2;
	}
	if (shape == QStringLiteral("cone")) {
		return kLevelBrushComponentMaxFaces - 1;
	}
	return shape == QStringLiteral("sphere") ? 64 : 0;
}

bool levelBrushPrimitivePoints(const LevelBrushPrimitiveRequest &request, QVector<LevelMapVec3> *points, QString *error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [&](const QString &message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!points || !finite(request.mins) || !finite(request.maxs) || request.maxs.x - request.mins.x < 1 ||
		request.maxs.y - request.mins.y < 1 || request.maxs.z - request.mins.z < 1) {
		return fail(PrimitiveText::tr("Use finite bounds within ±32768 units, with at least one unit of size on every axis."));
	}
	if (request.axis < 0 || request.axis > 2) {
		return fail(PrimitiveText::tr("The primitive axis must be X, Y or Z."));
	}
	const auto shape = request.shape;
	if (shape != QStringLiteral("box") && shape != QStringLiteral("wedge") && !radialShape(shape)) {
		return fail(PrimitiveText::tr("Choose box, wedge, cylinder, cone or sphere."));
	}
	const bool radial = radialShape(shape);
	if (radial && (request.sides < 3 || request.sides > levelBrushPrimitiveMaxSides(shape))) {
		return fail(PrimitiveText::tr("Cylinders take 3 to %1 sides, cones 3 to %2 and spheres 3 to %3.")
						.arg(levelBrushPrimitiveMaxSides(QStringLiteral("cylinder")))
						.arg(levelBrushPrimitiveMaxSides(QStringLiteral("cone")))
						.arg(levelBrushPrimitiveMaxSides(QStringLiteral("sphere"))));
	}
	if (shape == QStringLiteral("sphere") && (request.bands < 2 || request.bands > 16 || request.sides * request.bands > 128)) {
		return fail(PrimitiveText::tr("A sphere needs 2 to 16 latitude bands and at most 128 faces (sides × bands)."));
	}
	if (radial && request.fit == LevelBrushPrimitiveFit::Circle) {
		*points = circlePoints(request);
		return true;
	}
	QVector<LevelMapVec3> local;
	if (shape == QStringLiteral("box") || shape == QStringLiteral("wedge")) {
		for (int x : {-1, 1}) {
			for (int y : {-1, 1}) {
				local << LevelMapVec3{double(x), double(y), -1, true};
				if (shape == QStringLiteral("box") || x == 1) {
					local << LevelMapVec3{double(x), double(y), 1, true};
				}
			}
		}
	} else {
		const auto ring = [&](double z, double radius) {
			for (int side = 0; side < request.sides; ++side) {
				const double angle = 2 * std::numbers::pi * side / request.sides;
				local << LevelMapVec3{radius * std::cos(angle), radius * std::sin(angle), z, true};
			}
		};
		if (shape == QStringLiteral("sphere")) {
			local << LevelMapVec3{0, 0, -1, true};
			for (int band = 1; band < request.bands; ++band) {
				const double angle = std::numbers::pi * band / request.bands;
				ring(-std::cos(angle), std::sin(angle));
			}
			local << LevelMapVec3{0, 0, 1, true};
		} else {
			ring(-1, 1);
			if (shape == QStringLiteral("cylinder")) {
				ring(1, 1);
			} else {
				local << LevelMapVec3{0, 0, 1, true};
			}
		}
	}
	LevelMapVec3 low{1, 1, 1, true}, high{-1, -1, -1, true};
	for (const auto &p : local) {
		low.x = std::min(low.x, p.x);
		low.y = std::min(low.y, p.y);
		low.z = std::min(low.z, p.z);
		high.x = std::max(high.x, p.x);
		high.y = std::max(high.y, p.y);
		high.z = std::max(high.z, p.z);
	}
	for (auto &p : local) {
		std::array<double, 3> placed{};
		for (int axis = 0; axis < 3; ++axis) {
			const int world = (request.axis + 1 + axis) % 3;
			const double unit = (coordinate(p, axis) - coordinate(low, axis)) / (coordinate(high, axis) - coordinate(low, axis));
			placed[world] =
				std::round((coordinate(request.mins, world) + unit * (coordinate(request.maxs, world) - coordinate(request.mins, world))) *
						   1e6) /
				1e6;
		}
		p = {placed[0], placed[1], placed[2], true};
	}
	*points = local;
	return true;
}

bool createLevelBrushPrimitive(const LevelBrushPrimitiveRequest &request, LevelMapBrush *brush, QString *error)
{
	if (error) {
		error->clear();
	}
	const auto fail = [&](const QString &message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!brush) {
		return fail(PrimitiveText::tr("No destination brush was supplied."));
	}
	QVector<LevelMapVec3> points;
	if (!levelBrushPrimitivePoints(request, &points, error)) {
		return false;
	}
	const auto name = request.texture.trimmed();
	if (name.isEmpty() || name.size() > 1024 || name.contains(QStringLiteral("//")) || name.contains(QStringLiteral("/*")) ||
		name.contains(QStringLiteral("*/")) || std::any_of(name.cbegin(), name.cend(), [](QChar c) {
			return c.isSpace() || c.isNull() || c.category() == QChar::Other_Control || QStringLiteral("\"{}()[]\\").contains(c);
		})) {
		return fail(
			PrimitiveText::tr("Use a material path without spaces, control characters, quotes, brackets, backslashes or comment markers."));
	}
	return createLevelBrushHull(points, name, brush, error);
}
} // namespace vibestudio
