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
// -Y, +Y, -X and +X, the first repeated for any not given. Its planes face
// inwards, so it classifies but does not solve; viewFilterSolidBox solves.
inline QString viewFilterBox(int x0, int y0, int z0, int x1, int y1, int z1, const QStringList& textures, qint64 contents = 0, bool solid = false)
{
	const auto point = [](int x, int y, int z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	const auto face = [&](const QString& a, const QString& b, const QString& c, int index) {
		return QStringLiteral("%1 %2 %3 %4 0 0 0 0.5 0.5 %5 0 0\n").arg(a, solid ? c : b, solid ? b : c, textures.value(index, textures.value(0))).arg(contents);
	};
	return QStringLiteral("{\n") + face(point(x0, y0, z0), point(x0, y0 + 1, z0), point(x0 + 1, y0, z0), 0)
		+ face(point(x0, y0, z1), point(x0 + 1, y0, z1), point(x0, y0 + 1, z1), 1)
		+ face(point(x0, y0, z0), point(x0 + 1, y0, z0), point(x0, y0, z0 + 1), 2)
		+ face(point(x0, y1, z0), point(x0, y1, z0 + 1), point(x0 + 1, y1, z0), 3)
		+ face(point(x0, y0, z0), point(x0, y0, z0 + 1), point(x0, y0 + 1, z0), 4)
		+ face(point(x1, y0, z0), point(x1, y0 + 1, z0), point(x1, y0, z0 + 1), 5) + QStringLiteral("}\n");
}

// The same box with outward planes, as editors write them, so it solves and
// the camera draws it; face indices are the same.
inline QString viewFilterSolidBox(int x0, int y0, int z0, int x1, int y1, int z1, const QStringList& textures, qint64 contents = 0)
{
	return viewFilterBox(x0, y0, z0, x1, y1, z1, textures, contents, true);
}

inline QString viewFilterPatch(int x, int y, int z, const QString& texture = QStringLiteral("base/curve"))
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
	return QStringLiteral("{\npatchDef2\n{\n") + texture + QStringLiteral("\n( 3 3 0 0 0 )\n(\n") + rows + QStringLiteral(")\n}\n}\n");
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

// A Quake III map with one brush of each texture kind, shared by
// level_filter_texture_kinds_smoke_test and its UI test:
// worldspawn: 0 areaportal, 1 area portal bit, 2 clusterportal, 3 cluster
//   portal bit, 4 lightgrid, 5 lightgrid with one base/wall face, 6 botclip,
//   7 donotenter, 8 clip, 9 bot clip bit, 10 one decals/ face,
//   11 textures/decals/ face, 12 base/decals/ (no decal), 13 detail flag,
//   14 caulk but its top, 15 caulk all over, 16 hint with skip;
// entity 1 func_detail: brush 17; entity 2 func_door: brush 18, caulk but
//   its +Y face.
inline QByteArray viewFilterTextureKindsFixture()
{
	const QString caulk = QStringLiteral("common/caulk");
	const QString wall = QStringLiteral("base/wall");
	const QString lightgrid = QStringLiteral("common/lightgrid");
	QString text = QStringLiteral("// Game: Quake 3\n{\n\"classname\" \"worldspawn\"\n");
	const auto at = [](int index) { return index * 64; };
	text += viewFilterSolidBox(at(0), 0, 0, at(1), 64, 64, {QStringLiteral("common/areaportal")});
	text += viewFilterSolidBox(at(1), 0, 0, at(2), 64, 64, {wall}, 0x8000);
	text += viewFilterSolidBox(at(2), 0, 0, at(3), 64, 64, {QStringLiteral("common/clusterportal")});
	text += viewFilterSolidBox(at(3), 0, 0, at(4), 64, 64, {wall}, 0x100000);
	text += viewFilterSolidBox(at(4), 0, 0, at(5), 64, 64, {lightgrid});
	text += viewFilterSolidBox(at(5), 0, 0, at(6), 64, 64, {lightgrid, wall, lightgrid});
	text += viewFilterSolidBox(at(6), 0, 0, at(7), 64, 64, {QStringLiteral("common/botclip")});
	text += viewFilterSolidBox(at(7), 0, 0, at(8), 64, 64, {QStringLiteral("common/donotenter")});
	text += viewFilterSolidBox(at(8), 0, 0, at(9), 64, 64, {QStringLiteral("common/clip")});
	text += viewFilterSolidBox(at(9), 0, 0, at(10), 64, 64, {wall}, 0x400000);
	text += viewFilterSolidBox(at(10), 0, 0, at(11), 64, 64, {wall, QStringLiteral("decals/blood1"), wall});
	text += viewFilterSolidBox(at(11), 0, 0, at(12), 64, 64, {wall, QStringLiteral("textures/decals/scorch"), wall});
	text += viewFilterSolidBox(at(12), 0, 0, at(13), 64, 64, {wall, QStringLiteral("base/decals/plate"), wall});
	text += viewFilterSolidBox(at(13), 0, 0, at(14), 64, 64, {wall}, 0x8000000);
	text += viewFilterSolidBox(at(14), 0, 0, at(15), 64, 64, {caulk, wall, caulk});
	text += viewFilterSolidBox(at(15), 0, 0, at(16), 64, 64, {caulk});
	text += viewFilterSolidBox(at(16), 0, 0, at(17), 64, 64, {QStringLiteral("common/skip"), QStringLiteral("common/hint"), QStringLiteral("common/skip")});
	text += QStringLiteral("}\n{\n\"classname\" \"func_detail\"\n");
	text += viewFilterSolidBox(0, 128, 0, 64, 192, 64, {wall});
	text += QStringLiteral("}\n{\n\"classname\" \"func_door\"\n");
	text += viewFilterSolidBox(64, 128, 0, 128, 192, 64, {caulk, caulk, caulk, QStringLiteral("base/door"), caulk, caulk});
	text += QStringLiteral("}\n");
	return text.toUtf8();
}

// The shader script of the material-kinds fixture, for a package's
// scripts/ folder; shared by level_filter_material_kinds_smoke_test and its UI
// test. textures/skies/stars and textures/base_wall/wall are images without a
// script in that package, and liquids/murk is neither.
inline QByteArray viewFilterMaterialKindsShaders()
{
	return QByteArrayLiteral(
		"textures/base_wall/glass\n{\n\tqer_trans 0.5\n\tsurfaceparm trans\n\t{\n\t\tmap textures/base_wall/glass.tga\n\t\tblendFunc blend\n\t}\n}\n"
		"textures/base_wall/grate\n{\n\tcull none\n\t{\n\t\tmap textures/base_wall/grate.tga\n\t\talphaFunc GE128\n\t\tdepthWrite\n\t}\n}\n"
		"textures/base_wall/fence\n{\n\tqer_alphafunc greater 0.5\n\t{\n\t\tmap textures/base_wall/fence.tga\n\t}\n}\n"
		"textures/fogs/mist\n{\n\tqer_nocarve\n\tsurfaceparm trans\n\tsurfaceparm nonsolid\n\tsurfaceparm fog\n\tsurfaceparm nolightmap\n"
		"\tfogparms ( 0.5 0.5 0.5 ) 512\n}\n"
		"textures/base_wall/lavarock\n{\n\t{\n\t\tmap textures/base_wall/lavarock.tga\n\t}\n}\n"
		"textures/base_floor/goo\n{\n\tsurfaceparm slime\n\tsurfaceparm nonsolid\n\tsurfaceparm trans\n\t{\n\t\tmap textures/base_floor/goo.tga\n\t}\n}\n"
		"textures/base_sky/nebula\n{\n\tsurfaceparm noimpact\n\tsurfaceparm nolightmap\n\tskyParms - 512 -\n\t{\n\t\tmap textures/base_sky/nebula.tga\n\t}\n}\n"
		"textures/base_wall/invisible\n{\n\tsurfaceparm nodraw\n\tsurfaceparm playerclip\n\tsurfaceparm nonsolid\n}\n"
		"textures/common/weapclip\n{\n\tqer_trans 0.40\n\tsurfaceparm nodraw\n\tsurfaceparm trans\n\tsurfaceparm nomarks\n}\n"
		"textures/common/monsterclip\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nonsolid\n\tsurfaceparm monsterclip\n}\n"
		"textures/common/nodraw\n{\n\tsurfaceparm nodraw\n\tsurfaceparm nonsolid\n}\n"
		"textures/base_wall/botwall\n{\n\tsurfaceparm nodraw\n\tsurfaceparm botclip\n}\n"
		"textures/common/clip\n{\n\tsurfaceparm nodraw\n\tsurfaceparm playerclip\n\tsurfaceparm nonsolid\n}\n");
}

// A Quake III map for the material kinds, with the shaders above:
// worldspawn brushes 0 glass (qer_trans), 1 one grate face (alphaFunc),
//   2 fence (qer_alphafunc), 3 fogs/mist (fog), 4 lavarock (named lava, not a
//   liquid), 5 one goo face (slime), 6 skies/stars (an image only),
//   7 one base_sky/nebula face (skyParms), 8 invisible (playerclip, nodraw),
//   9 weapclip (qer_trans, nodraw), 10 monsterclip (nodraw), 11 nodraw,
//   12 wall with one nodraw face, 13 liquids/murk (unknown to the package),
//   14 botwall (botclip, nodraw), 15 wall;
// patches 0 glass and 1 common/clip.
inline QByteArray viewFilterMaterialKindsFixture()
{
	const QString wall = QStringLiteral("base_wall/wall");
	QString text = QStringLiteral("// Game: Quake 3\n{\n\"classname\" \"worldspawn\"\n");
	const auto box = [&text](int index, const QStringList& textures) {
		text += viewFilterSolidBox(index * 64, 0, 0, index * 64 + 64, 64, 64, textures);
	};
	box(0, {QStringLiteral("base_wall/glass")});
	box(1, {wall, QStringLiteral("base_wall/grate"), wall});
	box(2, {QStringLiteral("base_wall/fence")});
	box(3, {QStringLiteral("fogs/mist")});
	box(4, {QStringLiteral("base_wall/lavarock")});
	box(5, {wall, QStringLiteral("base_floor/goo"), wall});
	box(6, {QStringLiteral("skies/stars")});
	box(7, {wall, QStringLiteral("base_sky/nebula"), wall});
	box(8, {QStringLiteral("base_wall/invisible")});
	box(9, {QStringLiteral("common/weapclip")});
	box(10, {QStringLiteral("common/monsterclip")});
	box(11, {QStringLiteral("common/nodraw")});
	box(12, {wall, QStringLiteral("common/nodraw"), wall});
	box(13, {QStringLiteral("liquids/murk")});
	box(14, {QStringLiteral("base_wall/botwall")});
	box(15, {wall});
	text += viewFilterPatch(0, 0, 128, QStringLiteral("base_wall/glass"));
	text += viewFilterPatch(128, 0, 128, QStringLiteral("common/clip"));
	text += QStringLiteral("}\n");
	return text.toUtf8();
}

// A box brush with outward planes and the same content and surface flags on
// every face, from x0 to x1 along X; textures as viewFilterBox. Shared by
// level_filter_tags_smoke_test and its UI test.
inline QString viewFilterFlaggedBox(int x0, int x1, const QStringList& textures, qint64 contents = 0, qint64 surface = 0)
{
	const auto point = [](int x, int y, int z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	int index = 0;
	const auto face = [&](const QString& a, const QString& b, const QString& c) {
		const QString line =
			QStringLiteral("%1 %2 %3 %4 0 0 0 0.5 0.5 %5 %6 0\n").arg(a, b, c, textures.value(index, textures.value(0))).arg(contents).arg(surface);
		++index;
		return line;
	};
	QString text = QStringLiteral("{\n");
	text += face(point(x0, 0, 0), point(x0 + 1, 0, 0), point(x0, 1, 0));
	text += face(point(x0, 0, 64), point(x0, 1, 64), point(x0 + 1, 0, 64));
	text += face(point(x0, 0, 0), point(x0, 0, 1), point(x0 + 1, 0, 0));
	text += face(point(x0, 64, 0), point(x0 + 1, 64, 0), point(x0, 64, 1));
	text += face(point(x0, 0, 0), point(x0, 1, 0), point(x0, 0, 1));
	text += face(point(x1, 0, 0), point(x1, 0, 1), point(x1, 1, 0));
	return text + QStringLiteral("}\n");
}

// A Quake III map for the tag and class filters:
// worldspawn: 0 skip all over, 1 hint with skip sides, 2 sfx/fog_red,
//   3 wall with the fog content bit, 4 water all over, 5 wall with one water
//   face (its top), 6 wall with the detail bit; patch 0 water;
// entity 1 trigger_multiple: brush 7; entity 2 trigger_hurt: brush 8;
// entities 3 light and 4 LIGHT, 5 my_custom_thing; entity 6 func_door:
//   brush 9.
inline QByteArray viewFilterTagsFixture()
{
	const QString wall = QStringLiteral("base_wall/wall");
	const QString water = QStringLiteral("liquids/water_calm");
	const auto at = [](int index) { return index * 64; };
	QString text = QStringLiteral("// Game: Quake 3\n{\n\"classname\" \"worldspawn\"\n");
	text += viewFilterFlaggedBox(at(0), at(1), {QStringLiteral("common/skip")});
	text += viewFilterFlaggedBox(at(1), at(2), {QStringLiteral("common/skip"), QStringLiteral("common/hint"), QStringLiteral("common/skip")});
	text += viewFilterFlaggedBox(at(2), at(3), {QStringLiteral("sfx/fog_red")});
	text += viewFilterFlaggedBox(at(3), at(4), {wall}, 0x40);
	text += viewFilterFlaggedBox(at(4), at(5), {water});
	text += viewFilterFlaggedBox(at(5), at(6), {wall, water, wall});
	text += viewFilterFlaggedBox(at(6), at(7), {wall}, 0x8000000);
	text += viewFilterPatch(0, 0, 128, water);
	text += QStringLiteral("}\n{\n\"classname\" \"trigger_multiple\"\n");
	text += viewFilterFlaggedBox(at(7), at(8), {QStringLiteral("common/trigger")});
	text += QStringLiteral("}\n{\n\"classname\" \"trigger_hurt\"\n");
	text += viewFilterFlaggedBox(at(8), at(9), {QStringLiteral("common/trigger")});
	text += QStringLiteral("}\n"
						   "{\n\"classname\" \"light\"\n\"origin\" \"32 32 96\"\n}\n"
						   "{\n\"classname\" \"LIGHT\"\n\"origin\" \"96 32 96\"\n}\n"
						   "{\n\"classname\" \"my_custom_thing\"\n\"origin\" \"160 32 96\"\n}\n"
						   "{\n\"classname\" \"func_door\"\n");
	text += viewFilterFlaggedBox(at(9), at(10), {QStringLiteral("base_door/door")});
	text += QStringLiteral("}\n");
	return text.toUtf8();
}

// Tags in TrenchBroom's layout for viewFilterTagsFixture(), with five
// entries that cannot be used.
inline QByteArray viewFilterTagLayout()
{
	return QByteArrayLiteral(R"({
  "brush": [
    { "name": "Triggers", "attribs": [ "transparent" ], "match": "classname", "pattern": "trigger_*", "texture": "common/trigger" }
  ],
  "brushface": [
    { "name": "Water", "match": "material", "pattern": "*water*" },
    { "name": "Fog Volume", "match": "surfaceparm", "pattern": "fog" },
    { "name": "Detail", "match": "contentflag", "flags": [ "detail" ] },
    { "name": "Mist", "match": "contentflag", "flags": [ 64 ] },
    { "name": "Skip Faces", "match": "texture", "pattern": "common/skip" },
    { "name": "Bad Match", "match": "colour", "pattern": "red" },
    { "match": "texture", "pattern": "nameless" },
    { "name": "No Pattern", "match": "texture" },
    { "name": "Face Class", "match": "classname", "pattern": "func_*" },
    { "name": "water", "match": "texture", "pattern": "again" }
  ]
})");
}

// A game configuration written the way TrenchBroom writes its own, which is
// not strict JSON: '//' and '/* */' comments, bare names, a comma before a
// closing bracket, an exponent, and "\**" (an escaped '*' and then any run,
// so textures whose names start with '*'). Original test data in that
// layout; tags Trigger, Liquid, Hint and Murk, murk being content bit 1.
inline QByteArray viewFilterTrenchBroomConfig()
{
	return QByteArrayLiteral(R"({
    "version": 8, // the layout version
    "name": "Example",
    /* the definitions and
       the model scale expression */
    "entities": { "definitions": [ "Example.fgd" ], "scale": [ modelscale, modelscale_vec ] },
    "tags": {
        "brush": [
            {
                "name": "Trigger",
                "attribs": [ "transparent" ],
                "match": "classname",
                "pattern": "trigger*",
                "texture": "trigger" // what Turn into Trigger would set
            }
        ],
        "brushface": [
            { "name": "Liquid", "match": "texture", "pattern": "\**" },
            { "name": "Hint", "match": "texture", "pattern": "hint*" }, // a trailing comma follows
            { "name": "Murk", "match": "contentflag", "flags": [ "murk" ] },
        ]
    },
    "faceattribs": {
        "surfaceflags": [],
        "contentflags": [
            { "unused": true }, // 1
            { "name": "murk", "description": "Murky water" } // 2
        ]
    },
    "ratio": 1.5e-1
}
)");
}

// A Quake map for the configuration above: brush 0 *water1 all over, brush 1
// ground with a *lava1 top (face 1), brush 2 ground.
inline QByteArray viewFilterQuakeLiquidFixture()
{
	QString text = QStringLiteral("{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n");
	text += viewFilterFlaggedBox(0, 64, {QStringLiteral("*water1")});
	text += viewFilterFlaggedBox(64, 128, {QStringLiteral("ground1_6"), QStringLiteral("*lava1"), QStringLiteral("ground1_6")});
	text += viewFilterFlaggedBox(128, 192, {QStringLiteral("ground1_6")});
	text += QStringLiteral("}\n");
	// Quake faces carry no flags.
	return text.replace(QStringLiteral(" 0 0 0\n"), QStringLiteral("\n")).toUtf8();
}

} // namespace vibestudio::tests
