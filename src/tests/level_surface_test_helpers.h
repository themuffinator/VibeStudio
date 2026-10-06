#pragma once
#include "core/level_document.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"

namespace vibestudio::tests
{
// Original, synthetic source with comments and flags in each supported dialect.
inline QByteArray surfaceFixture(const QString &dialect)
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &document);
	addLevelMapBoxBrush(&document, {-32, 16, -8, true}, {96, 80, 56, true}, QStringLiteral("studio/grid"));
	const auto number = [](double n) { return QString::number(n, 'g', 15); };
	const auto point = [&](const LevelMapVec3 &p) { return QStringLiteral("( %1 %2 %3 )").arg(number(p.x), number(p.y), number(p.z)); };
	const bool matrix = dialect.startsWith(QStringLiteral("brushDef"));
	QStringList lines{QStringLiteral("// surface test\n{\n\"classname\" \"worldspawn\"\n{")};
	if (matrix) {
		lines << dialect << QStringLiteral("{");
	}
	for (const auto &face : document.brushes.first().faces) {
		const auto p = planeFromPoints(face.p0, face.p1, face.p2);
		const auto geometry =
			dialect == QStringLiteral("brushDef3")
				? QStringLiteral("( %1 %2 %3 %4 )").arg(number(p.normalX), number(p.normalY), number(p.normalZ), number(-p.distance))
				: point(face.p0) + ' ' + point(face.p1) + ' ' + point(face.p2);
		if (matrix) {
			lines << geometry +
						 QStringLiteral(
							 " ( ( -0.03125 /* retained */ 0.0078125 0.125 ) ( 0.015625 0.0625 -0.75 ) ) \"studio/grid\" 2 4 8 // flags");
		} else if (dialect == QStringLiteral("valve220")) {
			const auto projection = levelTextureProjection(face);
			lines << geometry + QStringLiteral(" \"studio/grid\" [ %1 %2 %3 7 ] [ %4 %5 %6 9 ] 0 -0.5 2 2 4 8 // flags")
									.arg(number(projection.u.x), number(projection.u.y), number(projection.u.z), number(projection.v.x),
										 number(projection.v.y), number(projection.v.z));
		} else {
			lines << geometry + QStringLiteral(" \"studio/grid\" 7 /* retained */ 9 17 -0.5 2 2 4 8 // flags");
		}
	}
	if (matrix) {
		lines << QStringLiteral("}");
	}
	lines << QStringLiteral("}\n}\n// untouched point entity\n{\n\"classname\" \"light\"\n\"origin\" \"8 16 24\"\n}\n");
	return lines.join('\n').replace(QStringLiteral("\n"), QStringLiteral("\r\n")).toUtf8();
}
} // namespace vibestudio::tests
