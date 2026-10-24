#pragma once
// A Quake III map with one object of every kind the Levels view filters name,
// shared by level_view_filters_smoke_test and level_view_filters_ui_smoke_test.
//
// entity 0 worldspawn: brush 0 caulk, 1 sky face, 2 clip, 3 hint face,
//   4 water, 5 detail flag; patch 0
// entity 1 func_group: brush 6, patch 1
// entity 2 func_detail: brush 7
// entity 3 func_door: brush 8
// entity 4 trigger_multiple: brush 9
// entities 5 light, 6 misc_model, 7 info_player_start, 8 monster_army,
//   9 item_health, 10 weapon_shotgun, 11 path_corner, 12 target_speaker
// entity 13 an empty func_group, entity 14 a func_door with no brushes yet
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace vibestudio::tests {

// A box brush in Quake III's classic face syntax; textures are bottom, top,
// -Y, +Y, -X and +X, the first repeated for any not given.
inline QString viewFilterBox(int x0, int y0, int z0, int x1, int y1, int z1, const QStringList& textures, qint64 contents = 0)
{
	const auto point = [](int x, int y, int z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	const auto face = [&](const QString& a, const QString& b, const QString& c, int index) {
		return QStringLiteral("%1 %2 %3 %4 0 0 0 0.5 0.5 %5 0 0\n").arg(a, b, c, textures.value(index, textures.value(0))).arg(contents);
	};
	return QStringLiteral("{\n") + face(point(x0, y0, z0), point(x0, y0 + 1, z0), point(x0 + 1, y0, z0), 0)
		+ face(point(x0, y0, z1), point(x0 + 1, y0, z1), point(x0, y0 + 1, z1), 1)
		+ face(point(x0, y0, z0), point(x0 + 1, y0, z0), point(x0, y0, z0 + 1), 2)
		+ face(point(x0, y1, z0), point(x0, y1, z0 + 1), point(x0 + 1, y1, z0), 3)
		+ face(point(x0, y0, z0), point(x0, y0, z0 + 1), point(x0, y0 + 1, z0), 4)
		+ face(point(x1, y0, z0), point(x1, y0 + 1, z0), point(x1, y0, z0 + 1), 5) + QStringLiteral("}\n");
}

inline QString viewFilterPatch(int x, int y, int z)
{
	QString rows;
	for (int column = 0; column < 3; ++column) {
		rows += QStringLiteral("( ");
		for (int row = 0; row < 3; ++row) {
			rows += QStringLiteral("( %1 %2 %3 %4 %5 ) ").arg(x + column * 32).arg(y + row * 32).arg(z + (column == 1 && row == 1 ? 8 : 0))
				.arg(column * 0.5).arg(row * 0.5);
		}
		rows += QStringLiteral(")\n");
	}
	return QStringLiteral("{\npatchDef2\n{\nbase/curve\n( 3 3 0 0 0 )\n(\n") + rows + QStringLiteral(")\n}\n}\n");
}

inline QByteArray viewFilterFixture()
{
	const QString wall = QStringLiteral("base/wall");
	QString text = QStringLiteral("// Game: Quake 3\n{\n\"classname\" \"worldspawn\"\n");
	text += viewFilterBox(0, 0, 0, 64, 64, 64, {QStringLiteral("common/caulk")});
	text += viewFilterBox(64, 0, 0, 128, 64, 64, {wall, QStringLiteral("skies/blue"), wall});
	text += viewFilterBox(128, 0, 0, 192, 64, 64, {QStringLiteral("common/clip")});
	text += viewFilterBox(192, 0, 0, 256, 64, 64, {QStringLiteral("common/hint"), wall, wall});
	text += viewFilterBox(256, 0, 0, 320, 64, 64, {QStringLiteral("liquids/water")});
	text += viewFilterBox(320, 0, 0, 384, 64, 64, {wall}, 0x8000000);
	text += viewFilterPatch(0, 0, 128);
	text += QStringLiteral("}\n{\n\"classname\" \"func_group\"\n");
	text += viewFilterBox(0, 128, 0, 64, 192, 64, {wall});
	text += viewFilterPatch(0, 128, 128);
	text += QStringLiteral("}\n{\n\"classname\" \"func_detail\"\n");
	text += viewFilterBox(64, 128, 0, 128, 192, 64, {wall});
	text += QStringLiteral("}\n{\n\"classname\" \"func_door\"\n\"angle\" \"90\"\n");
	text += viewFilterBox(128, 128, 0, 192, 192, 64, {QStringLiteral("base/door")});
	text += QStringLiteral("}\n{\n\"classname\" \"trigger_multiple\"\n");
	text += viewFilterBox(192, 128, 0, 256, 192, 64, {QStringLiteral("common/trigger")});
	text += QStringLiteral("}\n"
						   "{\n\"classname\" \"light\"\n\"origin\" \"32 32 96\"\n\"light\" \"300\"\n}\n"
						   "{\n\"classname\" \"misc_model\"\n\"origin\" \"96 32 96\"\n\"model\" \"models/test.md3\"\n}\n"
						   "{\n\"classname\" \"info_player_start\"\n\"origin\" \"160 32 96\"\n}\n"
						   "{\n\"classname\" \"monster_army\"\n\"origin\" \"224 32 96\"\n}\n"
						   "{\n\"classname\" \"item_health\"\n\"origin\" \"288 32 96\"\n}\n"
						   "{\n\"classname\" \"weapon_shotgun\"\n\"origin\" \"352 32 96\"\n}\n"
						   "{\n\"classname\" \"path_corner\"\n\"origin\" \"32 96 96\"\n\"targetname\" \"p1\"\n}\n"
						   "{\n\"classname\" \"target_speaker\"\n\"origin\" \"96 96 96\"\n\"noise\" \"sound/test.wav\"\n}\n"
						   "{\n\"classname\" \"func_group\"\n}\n"
						   "{\n\"classname\" \"func_door\"\n\"targetname\" \"empty\"\n}\n");
	return text.toUtf8();
}

} // namespace vibestudio::tests
