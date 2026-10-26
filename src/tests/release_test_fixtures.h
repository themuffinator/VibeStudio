#pragma once

// Fixtures shared by the release and asset register tests: generated stock
// packages for a fake Quake III, Quake and Doom II installation, and mod
// projects that mix stock and custom assets, including game code and scripts
// that load files by name. Every byte is generated here; no game data is used.

#include "core/game_installation.h"
#include "core/package_staging.h"
#include "core/project_manifest.h"
#include "tests/doom_preview_test_helpers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <utility>

namespace vibestudio::tests::release {

inline bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// Writes a PK3, ZIP or PAK through the studio's own package writer.
inline bool writePackage(const QString& path, PackageArchiveFormat format, const QVector<QPair<QString, QByteArray>>& files, QString* error = nullptr)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	// Fixtures replace earlier ones outright; the writer itself never overwrites.
	QFile::remove(path);
	PackageStagingModel model;
	QString failure;
	if (!model.createEmpty(format, QString(), &failure)) {
		if (error) { *error = failure; }
		return false;
	}
	for (const auto& file : files) {
		if (!model.addBytes(file.second, file.first, &failure)) {
			if (error) { *error = failure; }
			return false;
		}
	}
	PackageWriteRequest request;
	request.format = format;
	request.destinationPath = path;
	const PackageWriteReport report = model.writeArchive(request);
	if (!report.succeeded()) {
		if (error) { *error = report.blockedMessages.join(QLatin1Char(' ')); }
		return false;
	}
	return true;
}

// The stock bytes of files the fake Quake III ships, so tests can copy them.
inline QByteArray stockWall() { return QByteArray("stock wall texture pixels ").repeated(40); }
inline QByteArray stockOverridden() { return QByteArray("the game's own crate texture ").repeated(30); }

// <root>/baseq3/pak0.pk3 and pak1.pk3, <root>/missionpack/pak0.pk3.
inline bool makeQuake3Installation(const QString& root, QString* error = nullptr)
{
	const QByteArray baseShaders =
		"textures/base_wall/glass\n{\n\tqer_editorimage textures/base_wall/glass.tga\n\t{\n\t\tmap textures/base_wall/glass.tga\n\t}\n}\n"
		"textures/common/caulk\n{\n\tsurfaceparm nodraw\n}\n"
		"textures/skies/stocksky\n{\n\tskyParms env/stock 512 -\n}\n";
	return writePackage(QDir(root).filePath(QStringLiteral("baseq3/pak0.pk3")), PackageArchiveFormat::Pk3,
			   {{QStringLiteral("textures/base_wall/wall1.tga"), stockWall()},
				   {QStringLiteral("textures/base_wall/crate.tga"), stockOverridden()},
				   {QStringLiteral("textures/base_wall/glass.tga"), QByteArray("glass ").repeated(20)},
				   {QStringLiteral("scripts/base_wall.shader"), baseShaders},
				   {QStringLiteral("models/mapobjects/barrel.md3"), QByteArray("IDP3 stand-in")},
				   {QStringLiteral("sound/world/hum.wav"), QByteArray("RIFF stand-in")},
				   {QStringLiteral("music/fla22k_02.wav"), QByteArray("RIFF music")}},
			   error)
		&& writePackage(QDir(root).filePath(QStringLiteral("baseq3/pak1.pk3")), PackageArchiveFormat::Pk3,
			{{QStringLiteral("textures/base_floor/floor1.tga"), QByteArray("floor ").repeated(10)}}, error)
		&& writePackage(QDir(root).filePath(QStringLiteral("missionpack/pak0.pk3")), PackageArchiveFormat::Pk3,
			{{QStringLiteral("textures/team/flag.tga"), QByteArray("team arena flag")}}, error)
		&& writeFile(QDir(root).filePath(QStringLiteral("quake3.exe")), QByteArray());
}

inline GameInstallationProfile profile(const QString& id, const QString& gameKey, const QString& root)
{
	GameInstallationProfile installation;
	installation.id = id;
	installation.gameKey = gameKey;
	installation.engineFamily = gameDefinitionForKey(gameKey).engineFamily;
	installation.displayName = QStringLiteral("Test %1").arg(gameKey);
	installation.rootPath = root;
	return installation;
}

// A Quake III map whose brushes and entities use, in order: a stock image, a
// custom image, a stock shader, a custom shader, an overridden stock image,
// a stock common shader, a custom sound, a stock model and a stock sound.
inline QByteArray quake3Map(const QString& extraTexture = {})
{
	QByteArray text =
		"// Game: Quake 3\n"
		"{\n\"classname\" \"worldspawn\"\n\"message\" \"The Pit\"\n\"music\" \"music/fla22k_02.wav\"\n"
		"{\n"
		"( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) base_wall/wall1 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) mymod/wall 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) base_wall/glass 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) mymod/glow 0 0 0 0.5 0.5 0 0 0\n"
		"( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) base_wall/crate 0 0 0 0.5 0.5 0 0 0\n"
		"( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
		"}\n}\n"
		"{\n\"classname\" \"target_speaker\"\n\"origin\" \"32 32 32\"\n\"noise\" \"sound/mymod/hum.wav\"\n}\n"
		"{\n\"classname\" \"misc_model\"\n\"origin\" \"16 16 16\"\n\"model\" \"models/mapobjects/barrel.md3\"\n}\n"
		"{\n\"classname\" \"target_speaker\"\n\"origin\" \"8 8 8\"\n\"noise\" \"sound/world/hum.wav\"\n}\n"
		"{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"32 32 24\"\n}\n";
	if (!extraTexture.isEmpty()) {
		text += "{\n\"classname\" \"func_group\"\n{\n"
				"( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) " + extraTexture.toUtf8() + " 0 0 0 0.5 0.5 0 0 0\n"
				"( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
				"( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
				"( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) common/caulk 0 0 0 0.5 0.5 0 0 0\n"
				"}\n}\n";
	}
	return text;
}

inline bool writeManifest(const QString& root, const QString& gameKey, const QJsonObject& release = {})
{
	QDir().mkpath(root);
	ProjectManifest manifest = defaultProjectManifest(root, QStringLiteral("My Mod"));
	manifest.gameKey = gameKey;
	manifest.outputFolder = QStringLiteral("build");
	QString error;
	if (!saveProjectManifest(manifest, &error)) {
		return false;
	}
	if (release.isEmpty()) {
		return true;
	}
	QFile file(projectManifestPath(root));
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
	file.close();
	object.insert(QStringLiteral("release"), release);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(object).toJson()) > 0;
}

// The mod project: the map, its build and companions, custom assets, an
// identical copy of a stock texture, an override, and files that never ship.
inline bool makeQuake3Project(const QString& root)
{
	bool ok = writeManifest(root, QStringLiteral("quake3"),
		QJsonObject {{QStringLiteral("title"), QStringLiteral("The Pit")}, {QStringLiteral("version"), QStringLiteral("1.0.0")},
			{QStringLiteral("authors"), QJsonArray {QStringLiteral("Test Author <test@example.com>")}},
			{QStringLiteral("description"), QStringLiteral("A small arena.")}, {QStringLiteral("license"), QStringLiteral("Free to share unmodified.")},
			{QStringLiteral("packageName"), QStringLiteral("thepit")}});
	const QDir dir(root);
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.map")), quake3Map());
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.bsp")), QByteArray("IBSP stand-in compiled map"));
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.aas")), QByteArray("EAAS bot file"));
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1/lm_0000.tga")), QByteArray("external lightmap"));
	ok &= writeFile(dir.filePath(QStringLiteral("maps/arena1.prt")), QByteArray("PRT1 portals"));
	ok &= writeFile(dir.filePath(QStringLiteral("levelshots/arena1.jpg")), QByteArray("JFIF shot"));
	ok &= writeFile(dir.filePath(QStringLiteral("scripts/arena1.arena")), QByteArray("{\nmap \"arena1\"\n}\n"));
	ok &= writeFile(dir.filePath(QStringLiteral("scripts/mymod.shader")),
		QByteArray("textures/mymod/glow\n{\n\tqer_editorimage textures/mymod/glow.tga\n\t{\n\t\tmap textures/mymod/glow.tga\n\t}\n}\n"));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/mymod/wall.tga")), QByteArray("my wall ").repeated(64));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/mymod/glow.tga")), QByteArray("my glow ").repeated(64));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/mymod/unused.tga")), QByteArray("unused ").repeated(8));
	ok &= writeFile(dir.filePath(QStringLiteral("textures/base_wall/wall1.tga")), stockWall());
	ok &= writeFile(dir.filePath(QStringLiteral("textures/base_wall/crate.tga")), QByteArray("my own crate ").repeated(30));
	ok &= writeFile(dir.filePath(QStringLiteral("sound/mymod/hum.wav")), QByteArray("RIFF my hum"));
	ok &= writeFile(dir.filePath(QStringLiteral("src/art.psd")), QByteArray("8BPS"));
	ok &= writeFile(dir.filePath(QStringLiteral("notes.bak")), QByteArray("old"));
	ok &= writeFile(dir.filePath(QStringLiteral("readme.txt")), QByteArray("Read me."));
	return ok;
}

// --- Game code and scripts -----------------------------------------------------

// <root>/id1/pak0.pak with a player model and an ambient sound.
inline bool makeQuakeInstallation(const QString& root)
{
	return writePackage(QDir(root).filePath(QStringLiteral("id1/pak0.pak")), PackageArchiveFormat::Pak,
		{{QStringLiteral("progs/player.mdl"), QByteArray("IDPO")}, {QStringLiteral("sound/ambience/drone6.wav"), QByteArray("RIFF drone")}});
}

// QuakeC for a mod monster. monster_fiend precaches its model and sounds (one
// stock, one that exists nowhere) and calls fiend_sounds, which precaches
// another; fiend_stand1 is only named, never called. item_rune is a class
// the map does not use. Comments, $ model lines, a non-literal argument and
// an inline brush model must not count.
inline QByteArray fiendQuakeC()
{
	return "// A custom monster for the release tests.\n"
		   "$cd id1/models/fiend\n"
		   "$frame stand1 stand2\n"
		   "\n"
		   "void() fiend_stand1;\n"
		   "\n"
		   "void() fiend_sounds =\n"
		   "{\n"
		   "\tprecache_sound2 (\"fiend/roar.wav\");\n"
		   "};\n"
		   "\n"
		   "void() fiend_stand1 = [$stand1, fiend_stand1]\n"
		   "{\n"
		   "\tsound (self, CHAN_VOICE, \"fiend/idle.wav\", 1, ATTN_IDLE);\n"
		   "};\n"
		   "\n"
		   "/*QUAKED monster_fiend (1 0 0) (-16 -16 -24) (16 16 40)\n"
		   "precache_model (\"progs/inside_comment.mdl\");\n"
		   "*/\n"
		   "void() monster_fiend =\n"
		   "{\n"
		   "\t// precache_sound (\"fiend/commented.wav\");\n"
		   "\tprecache_model (\"progs/fiend.mdl\");\n"
		   "\tprecache_model (\"*1\");\n"
		   "\tprecache_sound (\"fiend/idle.wav\");\n"
		   "\tprecache_sound (\"ambience/drone6.wav\");\n"
		   "\tprecache_sound (\"fiend/gone.wav\");\n"
		   "\tprecache_sound (self.noise);\n"
		   "\tfiend_sounds ();\n"
		   "\tsetmodel (self, \"progs/fiend.mdl\");\n"
		   "\tself.th_stand = fiend_stand1;\n"
		   "};\n"
		   "\n"
		   "void item_rune()\n"
		   "{\n"
		   "\tprecache_model (\"progs/rune.mdl\");\n"
		   "\tsetmodel (self, \"progs/rune.mdl\");\n"
		   "}\n";
}

// A Quake mod whose map places the QuakeC monster.
inline bool makeQuakeCProject(const QString& root)
{
	bool ok = writeManifest(root, QStringLiteral("quake"), QJsonObject {{QStringLiteral("packageName"), QStringLiteral("fiend")}});
	const QDir dir(root);
	ok &= writeFile(dir.filePath(QStringLiteral("maps/fiend.map")),
		"{\n\"classname\" \"worldspawn\"\n\"message\" \"Fiend Lair\"\n{\n"
		"( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) city4_6 0 0 0 1 1\n( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) city4_6 0 0 0 1 1\n"
		"( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) city4_6 0 0 0 1 1\n( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) city4_6 0 0 0 1 1\n}\n}\n"
		"{\n\"classname\" \"info_player_start\"\n\"origin\" \"16 16 24\"\n}\n"
		"{\n\"classname\" \"monster_fiend\"\n\"origin\" \"32 32 24\"\n}\n");
	ok &= writeFile(dir.filePath(QStringLiteral("maps/fiend.bsp")), QByteArray("BSP2"));
	ok &= writeFile(dir.filePath(QStringLiteral("qc/fiend.qc")), fiendQuakeC());
	ok &= writeFile(dir.filePath(QStringLiteral("progs/fiend.mdl")), QByteArray("IDPO fiend"));
	ok &= writeFile(dir.filePath(QStringLiteral("progs/rune.mdl")), QByteArray("IDPO rune"));
	ok &= writeFile(dir.filePath(QStringLiteral("sound/fiend/idle.wav")), QByteArray("RIFF idle"));
	ok &= writeFile(dir.filePath(QStringLiteral("sound/fiend/roar.wav")), QByteArray("RIFF roar"));
	return ok;
}

// A Quake III menu that includes a header, shows project and stock art (one
// only a stock shader), names a sound that exists nowhere, and plays a sound
// and execs a config from an item's action. The background is on line 11.
inline QByteArray quake3MenuText()
{
	return "#include \"ui/menudef.h\"\n"
		   "{\n"
		   "\tassetGlobalDef\n\t{\n"
		   "\t\tcursor \"ui/assets/mycursor\"\n"
		   "\t\tmenuEnterSound \"sound/misc/missing.wav\"\n"
		   "\t}\n"
		   "\tmenuDef\n\t{\n"
		   "\t\tname \"main\"\n"
		   "\t\tbackground \"ui/assets/mybg.tga\"\n"
		   "\t\titemDef\n\t\t{\n"
		   "\t\t\tname \"barrel\"\n"
		   "\t\t\tasset_model \"models/mapobjects/barrel.md3\"\n"
		   "\t\t\tbackground \"textures/skies/stocksky\"\n"
		   "\t\t\taction { play \"sound/mymod/click.wav\" ; exec \"exec mymod.cfg\" }\n"
		   "\t\t}\n"
		   "\t}\n"
		   "}\n";
}

// A config that execs another and binds a key to a stock sound.
inline QByteArray quake3ConfigText()
{
	return "exec other\nbind x \"play sound/world/hum.wav\"\n// exec commented.cfg\n";
}

// Quake III menus and configs: a menu list loading the menu above, its art
// and sounds, and the configs it execs.
inline bool addQuake3MenuScripts(const QString& root)
{
	const QDir dir(root);
	bool ok = writeFile(dir.filePath(QStringLiteral("ui/menus.txt")), QByteArray("{\n\tloadMenu { \"ui/mymain.menu\" }\n}\n"));
	ok &= writeFile(dir.filePath(QStringLiteral("ui/mymain.menu")), quake3MenuText());
	ok &= writeFile(dir.filePath(QStringLiteral("ui/menudef.h")), QByteArray("#define ITEM_TYPE_TEXT 0\n"));
	ok &= writeFile(dir.filePath(QStringLiteral("ui/assets/mybg.tga")), QByteArray("menu background"));
	ok &= writeFile(dir.filePath(QStringLiteral("ui/assets/mycursor.tga")), QByteArray("cursor"));
	ok &= writeFile(dir.filePath(QStringLiteral("sound/mymod/click.wav")), QByteArray("RIFF click"));
	ok &= writeFile(dir.filePath(QStringLiteral("mymod.cfg")), quake3ConfigText());
	ok &= writeFile(dir.filePath(QStringLiteral("other.cfg")), QByteArray("set ui_mymod 1\n"));
	return ok;
}

// A DMX sound lump (format 3), which WAD readers recognise as a sound.
inline QByteArray dmxSound(int samples = 64)
{
	QByteArray bytes(8, '\0');
	vibestudio::tests::doom::put16(bytes, 0, 3);
	vibestudio::tests::doom::put16(bytes, 2, 11025);
	vibestudio::tests::doom::put32(bytes, 4, samples + 32);
	bytes += QByteArray(samples + 32, char(128));
	return bytes;
}

// DECORATE for a mod monster: FireImp (editor number 30001) fires FireBall,
// defined in an included lump; Unused (30002) is never placed. FireImp's
// death sound is the engine's own; FireBall's plays a lump nothing holds.
inline QByteArray fireImpDecorate()
{
	return "// Fire imps for the release tests.\n"
		   "#include \"IMPBALL\"\n"
		   "actor FireImp : DoomImp 30001\n"
		   "{\n"
		   "  Health 80\n"
		   "  SeeSound \"fireimp/sight\"\n"
		   "  DeathSound \"imp/death\"\n"
		   "  States\n"
		   "  {\n"
		   "  Spawn:\n"
		   "    FIMP AB 10 A_Look\n"
		   "    Loop\n"
		   "  Missile:\n"
		   "    FIMP C 8 A_CustomMissile(\"FireBall\", 32, 0, 0)\n"
		   "    Goto See\n"
		   "  Death:\n"
		   "    TNT1 A 0\n"
		   "    Stop\n"
		   "  }\n"
		   "}\n"
		   "actor Unused 30002\n"
		   "{\n"
		   "  States\n"
		   "  {\n"
		   "  Spawn:\n"
		   "    UNUS A -1\n"
		   "    Stop\n"
		   "  }\n"
		   "}\n";
}

inline QByteArray fireBallDecorate()
{
	return "actor FireBall\n"
		   "{\n"
		   "  DeathSound \"fireimp/boom\"\n"
		   "  States\n"
		   "  {\n"
		   "  Spawn:\n"
		   "    FBAL A 4 Bright\n"
		   "    Loop\n"
		   "  }\n"
		   "}\n";
}

// A Doom map WAD: the shared starter room with things of the given types.
inline QByteArray doomMapWad(const QString& mapName, const QVector<int>& thingTypes)
{
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	create.mapName = mapName;
	create.wallTexture = QStringLiteral("STONE");
	create.floorTexture = QStringLiteral("STONE");
	create.ceilingTexture = QStringLiteral("CEIL");
	LevelMapDocument map;
	QString error;
	if (!createLevelMap(create, &map, &error)) {
		return {};
	}
	int id = 0;
	for (const LevelMapDoomThing& thing : std::as_const(map.doomThings)) {
		id = std::max(id, thing.id + 1);
	}
	double y = 24.0;
	for (const int type : thingTypes) {
		LevelMapDoomThing thing;
		thing.id = id++;
		thing.x = 48.0;
		thing.y = y;
		thing.type = type;
		thing.flags = 7;
		map.doomThings.push_back(thing);
		y += 24.0;
	}
	return vibestudio::tests::doom::wad(vibestudio::tests::doom::lumps(serializeLevelMap(map).bytes));
}

// A Doom mod: monsters.wad holds DECORATE (including IMPBALL), SNDINFO,
// MAPINFO (MAP01's music and sky), sprites, a sound and a music lump;
// maps/fireimps.wad places FireImp in MAP01, maps/plain.wad only stock
// things in MAP02.
inline bool makeDecorateProject(const QString& root)
{
	namespace doom = vibestudio::tests::doom;
	bool ok = writeManifest(root, QStringLiteral("doom"), QJsonObject {{QStringLiteral("packageName"), QStringLiteral("fireimps")}});
	QByteArray music("MUS\x1a", 4);
	music += QByteArray(28, '\0');
	const QVector<doom::Lump> resources {
		{"DECORATE", fireImpDecorate()},
		{"IMPBALL", fireBallDecorate()},
		{"SNDINFO", "fireimp/sight dsfisit\nfireimp/boom dsfiboom\n"},
		{"MAPINFO", "map MAP01 \"Fire Imps\"\n{\n  music = \"D_FIRE\"\n  sky1 = \"FENCE\", 0\n}\n"},
		{"S_START", {}},
		{"FIMPA1", doom::patch(16, 16, 40)},
		{"FIMPB1", doom::patch(16, 16, 41)},
		{"FIMPC1", doom::patch(16, 16, 42)},
		{"FBALA0", doom::patch(8, 8, 43)},
		{"UNUSA0", doom::patch(8, 8, 44)},
		{"S_END", {}},
		{"DSFISIT", dmxSound()},
		{"D_FIRE", music},
	};
	ok &= writeFile(QDir(root).filePath(QStringLiteral("monsters.wad")), doom::wad(resources));
	ok &= writeFile(QDir(root).filePath(QStringLiteral("maps/fireimps.wad")), doomMapWad(QStringLiteral("MAP01"), {30001, 3001}));
	ok &= writeFile(QDir(root).filePath(QStringLiteral("maps/plain.wad")), doomMapWad(QStringLiteral("MAP02"), {3001}));
	return ok;
}

// Doom II IWAD: the shared Doom test assets (PLAYPAL, flats, patches,
// PNAMES, TEXTURE1) plus a map and a music lump.
inline QByteArray doom2Iwad()
{
	using namespace vibestudio::tests;
	QByteArray music("MUS\x1a", 4);
	music += QByteArray(28, '\0');
	QVector<doom::Lump> lumps = doom::assets();
	lumps << doom::Lump {"D_RUNNIN", music};
	QByteArray bytes = doom::wad(lumps + doom::lumps(doom::fixture()).mid(0, 11));
	bytes.replace(0, 4, "IWAD");
	return bytes;
}

// MD3 fixture layout after src/tests/model_mesh_smoke_test.cpp: one frame,
// one surface per shader.
inline void appendI32(QByteArray& bytes, qint32 value)
{
	char raw[4];
	qToLittleEndian(value, raw);
	bytes.append(raw, 4);
}
inline void appendI16(QByteArray& bytes, qint16 value)
{
	char raw[2];
	qToLittleEndian(value, raw);
	bytes.append(raw, 2);
}
inline void appendF32(QByteArray& bytes, float value)
{
	qint32 raw = 0;
	std::memcpy(&raw, &value, 4);
	appendI32(bytes, raw);
}
inline void appendFixed(QByteArray& bytes, const QByteArray& text, int size)
{
	QByteArray fixed = text.left(size - 1);
	fixed.resize(size, '\0');
	bytes += fixed;
}
inline QByteArray md3(const QByteArrayList& shaders)
{
	constexpr int surfaceBytes = 236;
	const int frameOffset = 108;
	const int tagOffset = frameOffset + 56;
	const int surfaceOffset = tagOffset;
	const int endOffset = surfaceOffset + surfaceBytes * int(shaders.size());
	QByteArray bytes("IDP3", 4);
	appendI32(bytes, 15);
	appendFixed(bytes, "models/test/release", 64);
	appendI32(bytes, 0);
	appendI32(bytes, 1);
	appendI32(bytes, 0);
	appendI32(bytes, int(shaders.size()));
	appendI32(bytes, 0);
	appendI32(bytes, frameOffset);
	appendI32(bytes, tagOffset);
	appendI32(bytes, surfaceOffset);
	appendI32(bytes, endOffset);
	for (float value : {-16.f, -16.f, -16.f, 16.f, 16.f, 16.f, 0.f, 0.f, 0.f, 27.7f}) {
		appendF32(bytes, value);
	}
	appendFixed(bytes, "idle", 16);
	for (int surface = 0; surface < shaders.size(); ++surface) {
		const int shaderOffset = 108;
		const int triangleOffset = shaderOffset + 68;
		const int stOffset = triangleOffset + 12;
		const int vertexOffset = stOffset + 24;
		bytes.append("IDP3", 4);
		appendFixed(bytes, "surface" + QByteArray::number(surface), 64);
		appendI32(bytes, 0);
		appendI32(bytes, 1);
		appendI32(bytes, 1);
		appendI32(bytes, 3);
		appendI32(bytes, 1);
		appendI32(bytes, triangleOffset);
		appendI32(bytes, shaderOffset);
		appendI32(bytes, stOffset);
		appendI32(bytes, vertexOffset);
		appendI32(bytes, vertexOffset + 24);
		appendFixed(bytes, shaders[surface], 64);
		appendI32(bytes, 0);
		appendI32(bytes, 0);
		appendI32(bytes, 1);
		appendI32(bytes, 2);
		for (float value : {0.f, 0.f, 1.f, 0.f, 1.f, 1.f}) {
			appendF32(bytes, value);
		}
		const int raw[3][3] = {{64, 128, -64}, {-128, 0, 320}, {0, -256, 64}};
		for (const auto& vertex : raw) {
			appendI16(bytes, qint16(vertex[0]));
			appendI16(bytes, qint16(vertex[1]));
			appendI16(bytes, qint16(vertex[2]));
			appendI16(bytes, 0);
		}
	}
	return bytes;
}

} // namespace vibestudio::tests::release
