// The Levels view filters' material kinds (core/level_view_filters.h):
// - Quake III shader flags read from parsed scripts the way Radiant reads
//   them: qer_trans, qer_alphafunc, a stage's alphaFunc and fog make a shader
//   translucent; surfaceparm water, slime and lava a liquid; playerclip,
//   monsterclip, botclip, donotenter and nodraw their kinds; skyParms a sky;
// - a package scan through the material library, with images that no script
//   defines counted as plain shaders and shadowed definitions left out;
// - Translucent, Liquids, Clip with Player, Monster and Weapon Clip, Bot Clip,
//   Sky and Nodraw on a Quake III map with and without the package's shaders,
//   patches included for Clip and Translucent;
// - Quake II's translucency, nodraw and clip flags and Quake's { fence and
//   clip textures, which never read shaders;
// - the command ids, palette entries and NetRadiant keys (Alt+4 Translucent);
// - `map filters --package` reporting the shader-aware matches.
// With the vibestudio binary as the first argument the CLI is checked too.

#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_map.h"
#include "core/level_view_filters.h"
#include "core/material_library.h"
#include "core/material_script.h"
#include "core/package_archive.h"
#include "core/studio_semantics.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

QStringList selectors(const QVector<LevelMapSelectionRef>& refs)
{
	QStringList ids;
	for (const LevelMapSelectionRef& ref : refs) {
		ids << levelMapSelectionRefId(ref);
	}
	return ids;
}

QStringList brushes(std::initializer_list<int> ids)
{
	QStringList list;
	for (const int id : ids) {
		list << QStringLiteral("brush:%1").arg(id);
	}
	return list;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool load(const QByteArray& bytes, const QString& engine, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("materials.map"), {}, engine}, bytes, document, &error), "load a fixture", error);
}

// A package folder with the fixture's shaders, a later script that shadows
// one of them, and two images without a script.
bool writePackage(const QString& root)
{
	// ioquake3 reads the alphabetically last script's definition, so this
	// one's lavarock (a liquid here) never draws.
	const QByteArray shadowed = QByteArrayLiteral("textures/base_wall/lavarock\n{\n\tsurfaceparm lava\n}\n");
	return writeFile(root + QStringLiteral("/scripts/a_old.shader"), shadowed)
		&& writeFile(root + QStringLiteral("/scripts/filterkinds.shader"), tests::viewFilterMaterialKindsShaders())
		&& writeFile(root + QStringLiteral("/textures/skies/stars.tga"), QByteArrayLiteral("not decoded"))
		&& writeFile(root + QStringLiteral("/textures/base_wall/wall.tga"), QByteArrayLiteral("not decoded"));
}

void shaderFlags()
{
	const MaterialScript script = parseMaterialScript(QString::fromUtf8(tests::viewFilterMaterialKindsShaders()), MaterialEngine::Quake3,
		QStringLiteral("scripts/filterkinds.shader"));
	const auto flags = [&script](const char* name) {
		const MaterialDefinition* definition = script.find(QString::fromLatin1(name));
		expect(definition, "shader parsed", QString::fromLatin1(name));
		return definition ? levelViewFilterShader(*definition) : LevelViewFilterShader();
	};
	const LevelViewFilterShader glass = flags("textures/base_wall/glass");
	expect(glass.translucent && !glass.alphaTested && !glass.liquid && !glass.nodraw, "qer_trans is translucent");
	const LevelViewFilterShader grate = flags("textures/base_wall/grate");
	expect(!grate.translucent && grate.alphaTested, "a stage's alphaFunc is alpha-tested");
	const LevelViewFilterShader fence = flags("textures/base_wall/fence");
	expect(fence.alphaTested && !fence.translucent, "qer_alphafunc is alpha-tested");
	const LevelViewFilterShader mist = flags("textures/fogs/mist");
	expect(mist.translucent && !mist.liquid, "a fog shader is translucent");
	const LevelViewFilterShader lavarock = flags("textures/base_wall/lavarock");
	expect(!lavarock.liquid && !lavarock.translucent && !lavarock.sky, "a lavarock wall is solid");
	expect(flags("textures/base_floor/goo").liquid, "surfaceparm slime is a liquid");
	const LevelViewFilterShader nebula = flags("textures/base_sky/nebula");
	expect(nebula.sky && !nebula.translucent, "skyParms alone makes a sky");
	const LevelViewFilterShader invisible = flags("textures/base_wall/invisible");
	expect(invisible.playerClip && invisible.nodraw && !invisible.monsterClip && !invisible.botClip, "playerclip and nodraw");
	const LevelViewFilterShader weapclip = flags("textures/common/weapclip");
	expect(weapclip.translucent && weapclip.nodraw && !weapclip.playerClip, "Quake III's weapclip has no clip surfaceparm");
	expect(flags("textures/common/monsterclip").monsterClip, "surfaceparm monsterclip");
	const LevelViewFilterShader botwall = flags("textures/base_wall/botwall");
	expect(botwall.botClip && botwall.nodraw && !botwall.playerClip, "surfaceparm botclip");
}

void scan(const QString& root)
{
	PackageArchive package;
	QString error;
	if (!expect(package.load(root, &error), "load the package folder", error)) {
		return;
	}
	const LevelViewFilterMaterials materials = scanLevelViewFilterMaterials(package);
	expect(materials.scriptCount == 2 && materials.scriptedShaderCount == 13 && materials.complete && !materials.cancelled, "scan counts",
		QStringLiteral("%1 scripts, %2 shaders").arg(materials.scriptCount).arg(materials.scriptedShaderCount));
	// 13 winning shaders and the two images without a script.
	expect(materials.shaders.size() == 15, "scripted shaders and plain images", QString::number(materials.shaders.size()));
	const LevelViewFilterShader* lavarock = materials.find(QStringLiteral("base_wall/LavaRock"));
	expect(lavarock && !lavarock->liquid, "the alphabetically last script's definition wins, by any case");
	const LevelViewFilterShader* stars = materials.find(QStringLiteral("skies/stars"));
	expect(stars && !stars->sky && !stars->translucent, "an image without a script is a plain shader");
	expect(materials.find(QStringLiteral("textures/base_wall/glass")) && materials.find(QStringLiteral("textures/base_wall/glass"))->translucent,
		"a name with its textures/ folder resolves too");
	expect(!materials.find(QStringLiteral("liquids/murk")) && !materials.find(QString()), "unknown names do not resolve");
	const LevelViewFilterMaterials cancelled = scanLevelViewFilterMaterials(package, [] { return true; });
	expect(cancelled.cancelled && cancelled.isEmpty(), "a cancelled scan holds nothing");
	expect(levelViewFilterReadsMaterials(QStringLiteral("translucent")) && levelViewFilterReadsMaterials(QStringLiteral("player-clip"))
			&& !levelViewFilterReadsMaterials(QStringLiteral("weapon-clip")) && !levelViewFilterReadsMaterials(QStringLiteral("caulk")),
		"the kinds that read shaders");
}

void quake3(const QString& root)
{
	LevelMapDocument document;
	if (!load(tests::viewFilterMaterialKindsFixture(), QStringLiteral("idTech3"), &document)) {
		return;
	}
	expect(document.format == LevelMapFormat::Quake3Map && document.brushes.size() == 16 && document.patches.size() == 2, "Quake III fixture shape",
		QStringLiteral("%1 brushes, %2 patches").arg(document.brushes.size()).arg(document.patches.size()));
	const QByteArray before = serializeLevelMap(document).bytes;
	const QStringList patchGlass {QStringLiteral("patch:0")};
	const QStringList patchClip {QStringLiteral("patch:1")};

	// Without the package the material kinds go by names: lavarock is a
	// liquid, skies/ is sky, and Translucent finds nothing.
	const QHash<QString, QStringList> byName {
		{QStringLiteral("translucent"), {}},
		{QStringLiteral("liquids"), brushes({4, 13})},
		{QStringLiteral("sky"), brushes({6})},
		{QStringLiteral("clip"), brushes({9, 10}) + patchClip},
		{QStringLiteral("player-clip"), patchClip},
		{QStringLiteral("monster-clip"), brushes({10})},
		{QStringLiteral("weapon-clip"), brushes({9})},
		{QStringLiteral("nodraw"), brushes({11})},
		{QStringLiteral("botclip"), {}},
	};
	const QHash<QString, int> nameCounts = levelViewFilterCounts(document);
	for (auto it = byName.cbegin(); it != byName.cend(); ++it) {
		const QStringList matched = selectors(levelViewFilterMatches(document, it.key()));
		expect(matched == it.value(), "matches by name", it.key() + QLatin1String(": ") + matched.join(QLatin1Char(',')));
		expect(nameCounts.value(it.key()) == it.value().size(), "count by name", it.key());
	}

	PackageArchive package;
	QString error;
	if (!expect(package.load(root, &error), "load the package folder", error)) {
		return;
	}
	const LevelViewFilterMaterials materials = scanLevelViewFilterMaterials(package);
	// With it each face whose shader the package defines or whose image it
	// holds goes by that shader; liquids/murk keeps the name rule.
	const QHash<QString, QStringList> byShader {
		{QStringLiteral("translucent"), brushes({0, 1, 2, 3, 9}) + patchGlass},
		{QStringLiteral("liquids"), brushes({5, 13})},
		{QStringLiteral("sky"), brushes({7})},
		{QStringLiteral("clip"), brushes({8, 9, 10}) + patchClip},
		{QStringLiteral("player-clip"), brushes({8}) + patchClip},
		{QStringLiteral("monster-clip"), brushes({10})},
		{QStringLiteral("weapon-clip"), brushes({9})},
		{QStringLiteral("nodraw"), brushes({8, 9, 10, 11, 14})},
		{QStringLiteral("botclip"), brushes({14})},
	};
	const QHash<QString, int> shaderCounts = levelViewFilterCounts(document, materials);
	for (auto it = byShader.cbegin(); it != byShader.cend(); ++it) {
		const QStringList matched = selectors(levelViewFilterMatches(document, it.key(), materials));
		expect(matched == it.value(), "matches by shader", it.key() + QLatin1String(": ") + matched.join(QLatin1Char(',')));
		expect(shaderCounts.value(it.key()) == it.value().size(), "count by shader", it.key());
	}
	// The other kinds read no shaders.
	for (const QString& kind : {QStringLiteral("caulk"), QStringLiteral("hint"), QStringLiteral("world"), QStringLiteral("brushes"), QStringLiteral("decals")}) {
		expect(shaderCounts.value(kind) == nameCounts.value(kind), "kinds that read no shaders keep their counts", kind);
	}
	const QStringList hidden = selectors(levelViewFilteredObjects(document, {QStringLiteral("translucent"), QStringLiteral("sky")}, materials));
	expect(hidden == brushes({0, 1, 2, 3, 9}) + patchGlass + brushes({7}), "switched-off material kinds combine", hidden.join(QLatin1Char(',')));
	expect(levelViewFilteredFaces(document, {QStringLiteral("translucent"), QStringLiteral("nodraw")}, materials).isEmpty(),
		"material kinds hide no single faces");
	expect(serializeLevelMap(document).bytes == before && document.undoStack.isEmpty(), "classifying never changes the map");
}

// A Quake II brush with both flag columns.
QString quake2Box(int x0, int x1, const QStringList& textures, qint64 contents, qint64 surface, bool flagsOnFirstFaceOnly = false)
{
	const auto point = [](int x, int y, int z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
	int index = 0;
	const auto face = [&](const QString& a, const QString& b, const QString& c) {
		const bool flagged = !flagsOnFirstFaceOnly || index == 0;
		const QString line = QStringLiteral("%1 %2 %3 %4 0 0 0 1 1 %5 %6 0\n")
								 .arg(a, b, c, textures.value(index, textures.value(0)))
								 .arg(flagged ? contents : 0)
								 .arg(flagged ? surface : 0);
		++index;
		return line;
	};
	QString text = QStringLiteral("{\n");
	text += face(point(x0, 0, 0), point(x0, 1, 0), point(x0 + 1, 0, 0));
	text += face(point(x0, 0, 64), point(x0 + 1, 0, 64), point(x0, 1, 64));
	text += face(point(x0, 0, 0), point(x0 + 1, 0, 0), point(x0, 0, 1));
	text += face(point(x0, 64, 0), point(x0, 64, 1), point(x0 + 1, 64, 0));
	text += face(point(x0, 0, 0), point(x0, 0, 1), point(x0, 1, 0));
	text += face(point(x1, 0, 0), point(x1, 1, 0), point(x1, 0, 1));
	return text + QStringLiteral("}\n");
}

void quake2(const QString& root)
{
	const QString floor = QStringLiteral("e1u1/floor1_3");
	QString text = QString::fromLatin1(kQuake2MapTargetHeader) + QStringLiteral("{\n\"classname\" \"worldspawn\"\n");
	text += quake2Box(0, 64, {QStringLiteral("e1u1/window1")}, 0, 0x10);
	text += quake2Box(64, 128, {QStringLiteral("e1u1/window2")}, 0, 0x20, true);
	text += quake2Box(128, 192, {QStringLiteral("e1u1/black")}, 0, 0x80);
	text += quake2Box(192, 256, {QStringLiteral("e1u1/black")}, 0, 0x80, true);
	text += quake2Box(256, 320, {floor}, 0x10000, 0);
	text += quake2Box(320, 384, {floor}, 0x20000, 0);
	text += quake2Box(384, 448, {floor}, 0x30000, 0);
	text += quake2Box(448, 512, {QStringLiteral("e1u1/clip")}, 0, 0);
	text += quake2Box(512, 576, {floor}, 0, 0);
	// CONTENTS_WATER on every face, and SURF_SKY on one.
	text += quake2Box(576, 640, {QStringLiteral("e1u1/fluid")}, 0x20, 0);
	text += quake2Box(640, 704, {floor}, 0, 0x4, true);
	text += QStringLiteral("}\n");
	LevelMapDocument document;
	if (!load(text.toUtf8(), QStringLiteral("idTech2"), &document)) {
		return;
	}
	expect(document.format == LevelMapFormat::QuakeMap && document.brushes.size() == 11, "Quake II fixture shape");
	expect(document.brushes.at(0).faces.at(0).surfaceFlags == 0x10 && document.brushes.at(4).faces.at(0).contentFlags == 0x10000,
		"Quake II flags parsed");
	const QHash<QString, QStringList> expected {
		// SURF_TRANS33 on every face, SURF_TRANS66 on one: any face will do.
		{QStringLiteral("translucent"), brushes({0, 1})},
		// SURF_NODRAW must be on every face.
		{QStringLiteral("nodraw"), brushes({2})},
		{QStringLiteral("player-clip"), brushes({4, 6, 7})},
		{QStringLiteral("monster-clip"), brushes({5, 6})},
		{QStringLiteral("weapon-clip"), {}},
		{QStringLiteral("clip"), brushes({4, 5, 6, 7})},
		// Liquids and Sky keep reading the content and surface flags.
		{QStringLiteral("liquids"), brushes({9})},
		{QStringLiteral("sky"), brushes({10})},
	};
	// The package's Quake III shaders never apply to a Quake II map.
	PackageArchive package;
	QString error;
	expect(package.load(root, &error), "load the package folder", error);
	const LevelViewFilterMaterials materials = scanLevelViewFilterMaterials(package);
	for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
		const QStringList matched = selectors(levelViewFilterMatches(document, it.key()));
		expect(matched == it.value(), "Quake II kind matches", it.key() + QLatin1String(": ") + matched.join(QLatin1Char(',')));
		expect(selectors(levelViewFilterMatches(document, it.key(), materials)) == it.value(), "Quake II ignores shaders", it.key());
	}
}

void quake()
{
	const auto box = [](int x0, const QString& texture) {
		const auto point = [](int x, int y, int z) { return QStringLiteral("( %1 %2 %3 )").arg(x).arg(y).arg(z); };
		const auto face = [&](const QString& a, const QString& b, const QString& c) { return QStringLiteral("%1 %2 %3 %4 0 0 0 1 1\n").arg(a, b, c, texture); };
		const int x1 = x0 + 64;
		return QStringLiteral("{\n") + face(point(x0, 0, 0), point(x0, 1, 0), point(x0 + 1, 0, 0)) + face(point(x0, 0, 64), point(x0 + 1, 0, 64), point(x0, 1, 64))
			+ face(point(x0, 0, 0), point(x0 + 1, 0, 0), point(x0, 0, 1)) + face(point(x0, 64, 0), point(x0, 64, 1), point(x0 + 1, 64, 0))
			+ face(point(x0, 0, 0), point(x0, 0, 1), point(x0, 1, 0)) + face(point(x1, 0, 0), point(x1, 1, 0), point(x1, 0, 1)) + QStringLiteral("}\n");
	};
	QString text = QStringLiteral("{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n");
	// The map tokenizer reads a bare { as a brace, so the fence texture is quoted.
	text += box(0, QStringLiteral("\"{grate1\""));
	text += box(64, QStringLiteral("clip"));
	text += box(128, QStringLiteral("*water1"));
	text += box(192, QStringLiteral("ground1_6"));
	text += QStringLiteral("}\n");
	LevelMapDocument document;
	if (!load(text.toUtf8(), QStringLiteral("idTech2"), &document)) {
		return;
	}
	expect(document.format == LevelMapFormat::QuakeMap && document.brushes.size() == 4, "Quake fixture shape");
	const QHash<QString, QStringList> expected {
		// Quake's { textures are alpha-tested fences.
		{QStringLiteral("translucent"), brushes({0})},
		// Quake's clip becomes hull clipping, which stops players and monsters.
		{QStringLiteral("player-clip"), brushes({1})},
		{QStringLiteral("monster-clip"), brushes({1})},
		{QStringLiteral("clip"), brushes({1})},
		{QStringLiteral("liquids"), brushes({2})},
		{QStringLiteral("nodraw"), {}},
	};
	for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
		const QStringList matched = selectors(levelViewFilterMatches(document, it.key()));
		expect(matched == it.value(), "Quake kind matches", it.key() + QLatin1String(": ") + matched.join(QLatin1Char(',')));
	}
}

void commands()
{
	const QHash<QString, QString> ids {{QStringLiteral("translucent"), QStringLiteral("map.filterTranslucent")},
		{QStringLiteral("nodraw"), QStringLiteral("map.filterNodraw")}, {QStringLiteral("player-clip"), QStringLiteral("map.filterPlayerClip")},
		{QStringLiteral("monster-clip"), QStringLiteral("map.filterMonsterClip")}, {QStringLiteral("weapon-clip"), QStringLiteral("map.filterWeaponClip")}};
	QStringList kinds;
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		kinds << kind.id;
	}
	for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
		expect(kinds.contains(it.key()), "kind offered", it.key());
		expect(levelViewFilterCommandId(it.key()) == it.value(), "command id", it.key());
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(it.value(), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(), "palette entry", it.value());
	}
	// The clip parts follow Clip, and Translucent follows Liquids, in the menu.
	expect(kinds.indexOf(QStringLiteral("player-clip")) == kinds.indexOf(QStringLiteral("clip")) + 1
			&& kinds.indexOf(QStringLiteral("translucent")) == kinds.indexOf(QStringLiteral("liquids")) + 1
			&& kinds.indexOf(QStringLiteral("nodraw")) == kinds.indexOf(QStringLiteral("caulk")) + 1,
		"menu order", kinds.join(QLatin1Char(',')));
	// NetRadiant Custom's and NetRadiant's ConstructFilters bind Alt+4 to
	// FilterTranslucent; the clip parts and Nodraw have no key upstream.
	for (const QString& profile : {QStringLiteral("netradiant-custom"), QStringLiteral("netradiant"), QStringLiteral("trenchbroom")}) {
		QHash<QString, QStringList> bound;
		for (const LevelEditorKeyBinding& binding : levelEditorControlsForProfile(profile).keys) {
			bound.insert(binding.commandId, binding.keys);
		}
		const bool radiant = profile != QLatin1String("trenchbroom");
		expect(radiant ? bound.value(QStringLiteral("map.filterTranslucent")) == QStringList {QStringLiteral("Alt+4")}
					   : bound.value(QStringLiteral("map.filterTranslucent")).isEmpty(),
			"Translucent key", profile);
		for (const char* id : {"map.filterNodraw", "map.filterPlayerClip", "map.filterMonsterClip", "map.filterWeaponClip"}) {
			expect(bound.value(QString::fromLatin1(id)).isEmpty(), "no upstream key", profile + QLatin1Char(' ') + QString::fromLatin1(id));
		}
		for (auto binding = bound.cbegin(); binding != bound.cend(); ++binding) {
			expect(binding.key() == QLatin1String("map.filterTranslucent") || !binding.value().contains(QStringLiteral("Alt+4")), "Alt+4 is Translucent's alone",
				profile + QLatin1Char(' ') + binding.key());
		}
	}
}

void cli(const QString& executable, const QString& root)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary folder")) {
		return;
	}
	const QString path = temp.filePath(QStringLiteral("materials.map"));
	const QByteArray source = tests::viewFilterMaterialKindsFixture();
	if (!expect(writeFile(path, source), "CLI fixture")) {
		return;
	}
	const auto run = [&](const QStringList& args, int exitCode = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
									  temp.filePath(QStringLiteral("settings.ini")), QStringLiteral("map"), QStringLiteral("filters"), path}
				+ args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000);
		const QByteArray out = process.readAllStandardOutput();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == exitCode, "CLI exit status",
			args.join(QLatin1Char(' ')) + QLatin1String(" -> ") + QString::fromUtf8(out + process.readAllStandardError()));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto entry = [](const QJsonObject& report, const QString& id) {
		for (const QJsonValue& value : report.value(QStringLiteral("filters")).toArray()) {
			if (value.toObject().value(QStringLiteral("id")).toString() == id) {
				return value.toObject();
			}
		}
		return QJsonObject();
	};
	QJsonObject report = run({QStringLiteral("--package"), root, QStringLiteral("--off"), QStringLiteral("translucent"), QStringLiteral("--objects")});
	const QJsonObject shaders = report.value(QStringLiteral("shaders")).toObject();
	expect(shaders.value(QStringLiteral("read")).toBool() && shaders.value(QStringLiteral("shaders")).toInt() == 13
			&& shaders.value(QStringLiteral("scripts")).toInt() == 2 && shaders.value(QStringLiteral("complete")).toBool(),
		"CLI reads the package's shaders", QString::fromUtf8(QJsonDocument(shaders).toJson(QJsonDocument::Compact)));
	const QJsonArray translucent {QStringLiteral("brush:0"), QStringLiteral("brush:1"), QStringLiteral("brush:2"), QStringLiteral("brush:3"),
		QStringLiteral("brush:9"), QStringLiteral("patch:0")};
	const QJsonObject translucentEntry = entry(report, QStringLiteral("translucent"));
	expect(report.value(QStringLiteral("filteredObjects")).toArray() == translucent && translucentEntry.value(QStringLiteral("objects")).toArray() == translucent
			&& translucentEntry.value(QStringLiteral("readsShaders")).toBool()
			&& translucentEntry.value(QStringLiteral("command")).toString() == QLatin1String("map.filterTranslucent"),
		"CLI hides the translucent shaders", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	expect(entry(report, QStringLiteral("liquids")).value(QStringLiteral("count")).toInt() == 2
			&& entry(report, QStringLiteral("sky")).value(QStringLiteral("count")).toInt() == 1
			&& entry(report, QStringLiteral("player-clip")).value(QStringLiteral("count")).toInt() == 2
			&& !entry(report, QStringLiteral("weapon-clip")).value(QStringLiteral("readsShaders")).toBool(),
		"CLI counts the other material kinds by shader");
	report = run({QStringLiteral("--off"), QStringLiteral("translucent,liquids")});
	expect(!report.value(QStringLiteral("shaders")).toObject().value(QStringLiteral("read")).toBool()
			&& report.value(QStringLiteral("filteredObjects")).toArray() == QJsonArray {QStringLiteral("brush:4"), QStringLiteral("brush:13")},
		"without --package the kinds go by names", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	run({QStringLiteral("--package"), temp.filePath(QStringLiteral("missing"))}, 4);
	QFile file(path);
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source, "the CLI never changes the map");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "temporary folder")) {
		return 1;
	}
	const QString root = temp.filePath(QStringLiteral("baseq3"));
	if (!expect(writePackage(root), "package folder")) {
		return 1;
	}
	shaderFlags();
	scan(root);
	quake3(root);
	quake2(root);
	quake();
	commands();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), root);
	}
	if (ok) {
		std::cout << "Filter material kinds: shader flags, package scan, Quake, Quake II and Quake III classification, keys" << (argc == 2 ? " and CLI" : "")
				  << " passed." << std::endl;
	}
	return ok ? 0 : 1;
}
