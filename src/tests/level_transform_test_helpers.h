#pragma once

#include "core/level_document.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include <cmath>

namespace vibestudio::tests
{
inline QByteArray transformFixture(const QString &kind)
{
	LevelMapDocument source;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &source);
	addLevelMapBoxBrush(&source, {-32, 16, -8, true}, {96, 80, 56, true}, QStringLiteral("studio/checker"));
	const auto n = [](double v) { return QString::number(v, 'g', 15); };
	const auto point = [&n](const auto &v) { return QStringLiteral("( %1 %2 %3 )").arg(n(v.x), n(v.y), n(v.z)); };
	QStringList text{QStringLiteral("// untouched header\n{\n\"classname\" \"worldspawn\"\n{")};
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		text << kind << QStringLiteral("{");
	}
	for (const auto &face : source.brushes.first().faces) {
		QString geometry = point(face.p0) + ' ' + point(face.p1) + ' ' + point(face.p2);
		const auto plane = planeFromPoints(face.p0, face.p1, face.p2);
		if (kind == QStringLiteral("brushDef3")) {
			geometry = QStringLiteral("( %1 %2 %3 %4 )").arg(n(plane.normalX), n(plane.normalY), n(plane.normalZ), n(-plane.distance));
		}
		if (kind.startsWith(QStringLiteral("brushDef"))) {
			text << geometry +
						QStringLiteral(
							" ( ( 0.03125 /* matrix */ 0.0078125 0.125 ) ( -0.015625 0.0625 -0.75 ) ) \"studio/checker\" 2 4 8 // face");
		} else if (kind == QStringLiteral("valve220")) {
			const auto uv = levelTextureProjection(face);
			text << geometry + QStringLiteral(" \"studio/checker\" [ %1 %2 %3 7 ] [ %4 %5 %6 9 ] 15 -0.5 2 2 4 8 // face")
								   .arg(n(uv.u.x), n(uv.u.y), n(uv.u.z), n(uv.v.x), n(uv.v.y), n(uv.v.z));
		} else {
			text << geometry + QStringLiteral(" \"studio/checker\" 7 /* mapping */ 9 15 -0.5 2 2 4 8 // face");
		}
	}
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		text << QStringLiteral("}");
	}
	text << QStringLiteral(
		"}\n}\n// untouched model\n{\n\"classname\" \"misc_model\"\n\"origin\" \"200 150 70\"\n\"model\" \"models/test.md3\"\n}\n");
	return text.join('\n').toUtf8();
}

inline bool loadTransformFixture(const QByteArray &bytes, LevelMapDocument *map, QString *error = nullptr)
{
	return loadLevelMapBytes({QStringLiteral("transform.map"), {}, QStringLiteral("idtech3")}, bytes, map, error);
}

template <typename Transform>
bool transformUvsMatch(const LevelMapBrush &before, const LevelMapBrush &after, Transform transform, double tolerance = 0.0001)
{
	const auto mesh = solveBrushGeometry(before.faces);
	if (!mesh.solved || before.faces.size() != after.faces.size()) {
		return false;
	}
	for (int f = 0; f < before.faces.size(); ++f) {
		const auto u = levelTextureProjection(before.faces[f]), v = levelTextureProjection(after.faces[f]);
		if (!u.valid || !v.valid || u.normalizedCoordinates != v.normalizedCoordinates) {
			return false;
		}
		// Test corners and an interior point, independent of the affine solver.
		auto points = mesh.faces[f].points;
		LevelMapVec3 center{0, 0, 0, true};
		for (const auto &p : points) {
			center.x += p.x;
			center.y += p.y;
			center.z += p.z;
		}
		center.x /= points.size();
		center.y /= points.size();
		center.z /= points.size();
		points << center;
		for (const auto &p : points) {
			const auto a = u.at(p), b = v.at(transform(p));
			if (std::abs(a.x() - b.x()) > tolerance || std::abs(a.y() - b.y()) > tolerance) {
				return false;
			}
		}
	}
	return true;
}
} // namespace vibestudio::tests
