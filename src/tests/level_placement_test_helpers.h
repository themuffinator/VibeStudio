#pragma once
#include "core/level_document.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include <cmath>

namespace vibestudio::tests {
inline QString placementNumber(double n) { return QString::number(n, 'g', 15); }
inline QString placementPoint(const LevelMapVec3& p) {
	return QStringLiteral("( %1 %2 %3 )").arg(placementNumber(p.x), placementNumber(p.y), placementNumber(p.z));
}
inline QByteArray placementFixture(const QString& kind) {
	LevelMapDocument doc;
	LevelMapCreateRequest request;
	request.starterRoom = false;
	createLevelMap(request, &doc);
	addLevelMapBoxBrush(&doc, {-29, 19, -5, true}, {99, 83, 59, true}, QStringLiteral("studio/checker"));
	QStringList lines{QStringLiteral("// original source\n{\n\"classname\" \"worldspawn\"\n{")};
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << kind << QStringLiteral("{");
	}
	for (const auto& f : doc.brushes.first().faces) {
		QString geometry = placementPoint(f.p0) + ' ' + placementPoint(f.p1) + ' ' + placementPoint(f.p2);
		const auto p = planeFromPoints(f.p0, f.p1, f.p2);
		if (kind == QStringLiteral("brushDef3")) {
			geometry =
				QStringLiteral("( %1 %2 %3 %4 )")
					.arg(placementNumber(p.normalX), placementNumber(p.normalY), placementNumber(p.normalZ), placementNumber(-p.distance));
		}
		if (kind.startsWith(QStringLiteral("brushDef"))) {
			lines << geometry + QStringLiteral(" ( ( 0.03125 /* uv note */ 0.0078125 0.125 ) ( -0.015625 0.0625 -0.75 ) ) "
											   "\"studio/checker\" 2 4 8 // face note");
		} else if (kind == QStringLiteral("valve220")) {
			const auto projection = levelTextureProjection(f);
			lines << geometry + QStringLiteral(" \"studio/checker\" [ %1 %2 %3 7 ] [ %4 %5 %6 9 ] 15 -0.5 2 2 4 8 // face note")
									.arg(placementNumber(projection.u.x), placementNumber(projection.u.y), placementNumber(projection.u.z),
										 placementNumber(projection.v.x), placementNumber(projection.v.y), placementNumber(projection.v.z));
		} else {
			lines << geometry + QStringLiteral(" \"studio/checker\" 7 /* uv note */ 9 15 -0.5 2 2 4 8 // face note");
		}
	}
	if (kind.startsWith(QStringLiteral("brushDef"))) {
		lines << QStringLiteral("}");
	}
	lines << QStringLiteral("}\n}\n// untouched entity\n{\n\"classname\" \"misc_model\"\n\"origin\" \"200 150 70\"\n\"angles\" \"20 30 "
							"40\"\n\"model\" \"models/test.md3\"\n}\n");
	return lines.join('\n').toUtf8();
}

inline bool placementUvsMatch(const LevelMapBrush& before, const LevelMapBrush& after, const LevelMapVec3& delta) {
	const auto geometry = solveBrushGeometry(before.faces);
	if (!geometry.solved || before.faces.size() != after.faces.size()) {
		return false;
	}
	for (int i = 0; i < before.faces.size(); ++i) {
		const auto a = levelTextureProjection(before.faces[i]), b = levelTextureProjection(after.faces[i]);
		if (!a.valid || !b.valid || a.normalizedCoordinates != b.normalizedCoordinates) {
			return false;
		}
		for (const auto& p : geometry.faces[i].points) {
			const auto uv0 = a.at(p), uv1 = b.at({p.x + delta.x, p.y + delta.y, p.z + delta.z, true});
			if (std::abs(uv0.x() - uv1.x()) > 0.001 || std::abs(uv0.y() - uv1.y()) > 0.001) {
				return false;
			}
		}
	}
	return true;
}
} // namespace vibestudio::tests
