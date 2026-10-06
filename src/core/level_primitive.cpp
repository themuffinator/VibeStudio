#include "core/level_primitive.h"

#include <QCoreApplication>
#include <algorithm>
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
	if (!brush || !finite(request.mins) || !finite(request.maxs) || request.maxs.x - request.mins.x < 1 ||
		request.maxs.y - request.mins.y < 1 || request.maxs.z - request.mins.z < 1) {
		return fail(PrimitiveText::tr("Use finite bounds within ±32768 units, with at least one unit of size on every axis."));
	}
	if (request.axis < 0 || request.axis > 2) {
		return fail(PrimitiveText::tr("The primitive axis must be X, Y or Z."));
	}
	const auto shape = request.shape;
	if (shape != QStringLiteral("box") && shape != QStringLiteral("wedge") && shape != QStringLiteral("cylinder") &&
		shape != QStringLiteral("cone") && shape != QStringLiteral("sphere")) {
		return fail(PrimitiveText::tr("Choose box, wedge, cylinder, cone or sphere."));
	}
	const auto name = request.texture.trimmed();
	if (name.isEmpty() || name.size() > 1024 || name.contains(QStringLiteral("//")) || name.contains(QStringLiteral("/*")) ||
		name.contains(QStringLiteral("*/")) || std::any_of(name.cbegin(), name.cend(), [](QChar c) {
			return c.isSpace() || c.isNull() || c.category() == QChar::Other_Control || QStringLiteral("\"{}()[]\\").contains(c);
		})) {
		return fail(
			PrimitiveText::tr("Use a material path without spaces, control characters, quotes, brackets, backslashes or comment markers."));
	}
	const bool radial = shape == QStringLiteral("cylinder") || shape == QStringLiteral("cone") || shape == QStringLiteral("sphere");
	if (radial && (request.sides < 3 || request.sides > 64)) {
		return fail(PrimitiveText::tr("Radial primitives need 3 to 64 sides."));
	}
	if (shape == QStringLiteral("sphere") && (request.bands < 2 || request.bands > 16 || request.sides * request.bands > 128)) {
		return fail(PrimitiveText::tr("A sphere needs 2 to 16 latitude bands and at most 128 faces (sides × bands)."));
	}
	QVector<LevelMapVec3> points;
	if (shape == QStringLiteral("box") || shape == QStringLiteral("wedge")) {
		for (int x : {-1, 1}) {
			for (int y : {-1, 1}) {
				points << LevelMapVec3{double(x), double(y), -1, true};
				if (shape == QStringLiteral("box") || x == 1) {
					points << LevelMapVec3{double(x), double(y), 1, true};
				}
			}
		}
	} else {
		const auto ring = [&](double z, double radius) {
			for (int side = 0; side < request.sides; ++side) {
				const double angle = 2 * std::numbers::pi * side / request.sides;
				points << LevelMapVec3{radius * std::cos(angle), radius * std::sin(angle), z, true};
			}
		};
		if (shape == QStringLiteral("sphere")) {
			points << LevelMapVec3{0, 0, -1, true};
			for (int band = 1; band < request.bands; ++band) {
				const double angle = std::numbers::pi * band / request.bands;
				ring(-std::cos(angle), std::sin(angle));
			}
			points << LevelMapVec3{0, 0, 1, true};
		} else {
			ring(-1, 1);
			if (shape == QStringLiteral("cylinder")) {
				ring(1, 1);
			} else {
				points << LevelMapVec3{0, 0, 1, true};
			}
		}
	}
	LevelMapVec3 low{1, 1, 1, true}, high{-1, -1, -1, true};
	for (const auto &p : points) {
		low.x = std::min(low.x, p.x);
		low.y = std::min(low.y, p.y);
		low.z = std::min(low.z, p.z);
		high.x = std::max(high.x, p.x);
		high.y = std::max(high.y, p.y);
		high.z = std::max(high.z, p.z);
	}
	for (auto &p : points) {
		std::array<double, 3> placed{};
		for (int local = 0; local < 3; ++local) {
			const int world = (request.axis + 1 + local) % 3;
			const double unit = (coordinate(p, local) - coordinate(low, local)) / (coordinate(high, local) - coordinate(low, local));
			placed[world] =
				std::round((coordinate(request.mins, world) + unit * (coordinate(request.maxs, world) - coordinate(request.mins, world))) *
						   1e6) /
				1e6;
		}
		p = {placed[0], placed[1], placed[2], true};
	}
	return createLevelBrushHull(points, name, brush, error);
}
} // namespace vibestudio
