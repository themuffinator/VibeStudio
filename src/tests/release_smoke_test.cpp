// Releases end to end in core: planning what ships for each scope and game
// against a generated game asset register, the files game code and scripts
// load by name, the changelog and release notes, release records and their
// diffs, and publishing packages, readmes, notes and distribution archives
// through the atomic publisher.

#include "core/deflate.h"
#include "core/game_asset_register.h"
#include "core/level_document.h"
#include "core/release_notes.h"
#include "core/release_plan.h"
#include "core/release_publish.h"
#include "core/release_script_refs.h"
#include "core/studio_settings.h"
#include "tests/release_test_fixtures.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace fixtures = vibestudio::tests::release;
namespace doom = vibestudio::tests::doom;

namespace {

bool expect(bool condition, const char* message, const QString& detail = {})
{
	if (!condition) {
		std::cerr << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << qUtf8Printable(detail) << ")";
		}
		std::cerr << "\n";
	}
	return condition;
}

QStringList entryPaths(const ReleasePlan& plan)
{
	QStringList paths;
	for (const ReleaseEntry& entry : plan.entries) {
		paths << entry.virtualPath;
	}
	return paths;
}

QStringList stockPaths(const ReleasePlan& plan)
{
	QStringList paths;
	for (const ReleaseReference& reference : plan.stock) {
		paths << reference.path;
	}
	return paths;
}

QString problemKinds(const ReleasePlan& plan)
{
	QStringList kinds;
	for (const ReleaseProblem& problem : plan.problems) {
		kinds << problem.kind + (problem.blocking ? QStringLiteral("!") : QString()) + QLatin1Char(':') + problem.reference;
	}
	return kinds.join(QLatin1Char(' '));
}

bool hasProblem(const ReleasePlan& plan, const QString& kind, bool blocking)
{
	for (const ReleaseProblem& problem : plan.problems) {
		if (problem.kind == kind && problem.blocking == blocking) {
			return true;
		}
	}
	return false;
}

std::shared_ptr<const GameAssetRegister> baseRegister(const GameInstallationProfile& installation)
{
	const GameAssetRegisterBuildResult built = buildGameAssetRegister({installation, {}, {}});
	if (!built.succeeded) {
		std::cerr << qUtf8Printable(built.error) << "\n";
		return {};
	}
	return std::make_shared<GameAssetRegister>(built.registerData.filtered(built.registerData.defaultSourceIds()));
}

ReleaseRequest request(const QString& root, ReleaseScope scope, const QStringList& items, std::shared_ptr<const GameAssetRegister> stock)
{
	ReleaseRequest request;
	loadProjectManifest(root, &request.manifest);
	request.gameKey = effectiveProjectGameKey(request.manifest);
	request.release = effectiveProjectReleaseSettings(request.manifest, request.gameKey);
	request.scope = scope;
	for (const QString& item : items) {
		request.items << QDir(root).absoluteFilePath(item);
	}
	request.stock = std::move(stock);
	return request;
}

QStringList packageEntries(const QString& path)
{
	PackageArchive archive;
	QString error;
	QStringList names;
	if (!archive.load(path, &error)) {
		names << QStringLiteral("<unreadable: %1>").arg(error);
		return names;
	}
	// Directory order: listings sort by name, a WAD's order is its meaning.
	QVector<PackageEntry> entries = archive.entries();
	std::stable_sort(entries.begin(), entries.end(), [](const PackageEntry& left, const PackageEntry& right) {
		return left.sourceOrdinal < right.sourceOrdinal;
	});
	for (const PackageEntry& entry : std::as_const(entries)) {
		if (entry.kind == PackageEntryKind::File) {
			names << entry.virtualPath;
		}
	}
	if (archive.format() != PackageArchiveFormat::Wad) {
		std::sort(names.begin(), names.end());
	}
	return names;
}

bool setModified(const QString& path, const QDateTime& time)
{
	QFile file(path);
	return file.open(QIODevice::ReadWrite) && file.setFileTime(time, QFileDevice::FileModificationTime);
}

QByteArray readAll(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool runQuake3Maps(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("q3"));
	const QString root = QDir(base).filePath(QStringLiteral("mymod"));
	ok &= expect(fixtures::makeQuake3Installation(game) && fixtures::makeQuake3Project(root), "the Quake III fixtures are written");
	auto stock = baseRegister(fixtures::profile(QStringLiteral("q3"), QStringLiteral("quake3"), game));
	ok &= expect(stock != nullptr, "the stock register builds");

	ReleasePlan plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, stock));
	const QStringList expected {
		QStringLiteral("levelshots/arena1.jpg"), QStringLiteral("maps/arena1.aas"), QStringLiteral("maps/arena1.bsp"), QStringLiteral("maps/arena1/lm_0000.tga"),
		QStringLiteral("scripts/arena1.arena"), QStringLiteral("scripts/mymod.shader"), QStringLiteral("sound/mymod/hum.wav"),
		QStringLiteral("textures/base_wall/crate.tga"), QStringLiteral("textures/mymod/glow.tga"), QStringLiteral("textures/mymod/wall.tga"),
	};
	ok &= expect(entryPaths(plan) == expected, "a map ships its build, companions and custom assets, and nothing else", entryPaths(plan).join(QLatin1Char(' ')));
	ok &= expect(plan.canPublish() && plan.blockingCount() == 0, "the map plan can publish", problemKinds(plan));
	ok &= expect(plan.packageFileName == QStringLiteral("thepit.pk3") && plan.format == PackageArchiveFormat::Pk3 && plan.formatId == QStringLiteral("pk3"),
		"Quake III releases a PK3 named after the project", plan.packageFileName);
	ok &= expect(plan.overrideCount == 1 && plan.entry(QStringLiteral("textures/base_wall/crate.tga")) && plan.entry(QStringLiteral("textures/base_wall/crate.tga"))->replacesStock,
		"a project file replacing a stock file is included and flagged");
	ok &= expect(!plan.warnings.isEmpty() && plan.warnings.first().contains(QStringLiteral("replace")), "the override is warned about first");
	const QStringList stockList = stockPaths(plan);
	for (const QString& path : {QStringLiteral("textures/base_wall/wall1.tga"), QStringLiteral("scripts/base_wall.shader"), QStringLiteral("models/mapobjects/barrel.md3"),
			 QStringLiteral("sound/world/hum.wav"), QStringLiteral("music/fla22k_02.wav")}) {
		ok &= expect(stockList.contains(path), "a stock reference is listed as provided by the game", path);
	}
	bool identical = false;
	int shaders = 0;
	for (const ReleaseReference& reference : plan.stock) {
		identical = identical || (reference.path == QStringLiteral("textures/base_wall/wall1.tga") && reference.identicalCopy);
		shaders += reference.path == QStringLiteral("scripts/base_wall.shader") ? 1 : 0;
	}
	ok &= expect(identical, "the project's identical copy of a stock texture stays out");
	ok &= expect(shaders == 2, "two stock shaders from one script are two references", QString::number(shaders));
	ok &= expect(plan.maps.size() == 1 && plan.maps.first().title == QStringLiteral("The Pit") && plan.maps.first().built && !plan.maps.first().stale,
		"the map's title and build state are reported");
	QVector<ReleaseCompositionRow> composition = plan.composition();
	ok &= expect(!composition.isEmpty() && composition.first().role == QStringLiteral("map"), "composition starts with maps");
	ok &= expect(releasePlanJson(plan).value(QStringLiteral("canPublish")).toBool() && releasePlanText(plan).contains(QStringLiteral("Ready to publish")),
		"the plan reports as JSON and text");

	// A reference nothing provides blocks the release.
	QFile::remove(QDir(root).filePath(QStringLiteral("maps/arena1.map")));
	fixtures::writeFile(QDir(root).filePath(QStringLiteral("maps/arena1.map")), fixtures::quake3Map(QStringLiteral("mymod/missing")));
	setModified(QDir(root).filePath(QStringLiteral("maps/arena1.map")), QDateTime::currentDateTime().addSecs(-3600));
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, stock));
	ok &= expect(!plan.canPublish() && hasProblem(plan, QStringLiteral("missing"), true), "a missing texture blocks publishing", problemKinds(plan));

	// Without the game's index, unknown references are assumed stock, not blocking.
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, nullptr));
	ok &= expect(plan.canPublish() && hasProblem(plan, QStringLiteral("unverified"), false) && !plan.stockChecked,
		"without an index unknown references are unverified, not blocking", problemKinds(plan));
	ok &= expect(entryPaths(plan).contains(QStringLiteral("textures/base_wall/wall1.tga")), "without an index the project's copy of a stock file ships");
	ok &= expect(!plan.limitations.isEmpty() && plan.limitations.first().contains(QStringLiteral("Index the game")), "the plan says how to check stock files");

	// Formats the game cannot load are refused.
	ReleaseRequest wad = request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, stock);
	wad.release.packageFormat = QStringLiteral("wad");
	plan = planRelease(wad);
	ok &= expect(hasProblem(plan, QStringLiteral("format"), true) && !plan.canPublish(), "a WAD release of a Quake III map is refused");

	// A source newer than its build is out of date; a missing build blocks.
	fixtures::writeFile(QDir(root).filePath(QStringLiteral("maps/arena1.map")), fixtures::quake3Map());
	setModified(QDir(root).filePath(QStringLiteral("maps/arena1.bsp")), QDateTime::currentDateTime().addSecs(-7200));
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, stock));
	ok &= expect(hasProblem(plan, QStringLiteral("stale-map"), false) && plan.canPublish(), "an out-of-date build warns", problemKinds(plan));
	QFile::rename(QDir(root).filePath(QStringLiteral("maps/arena1.bsp")), QDir(root).filePath(QStringLiteral("maps/arena1.bsp.away")));
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, stock));
	ok &= expect(hasProblem(plan, QStringLiteral("unbuilt-map"), true) && !plan.canPublish(), "an unbuilt map blocks", problemKinds(plan));
	QFile::rename(QDir(root).filePath(QStringLiteral("maps/arena1.bsp.away")), QDir(root).filePath(QStringLiteral("maps/arena1.bsp")));
	// A build in the output folder counts.
	QDir(root).mkpath(QStringLiteral("build/maps"));
	QFile::rename(QDir(root).filePath(QStringLiteral("maps/arena1.bsp")), QDir(root).filePath(QStringLiteral("build/maps/arena1.bsp")));
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, stock));
	const ReleaseEntry* bsp = plan.entry(QStringLiteral("maps/arena1.bsp"));
	ok &= expect(bsp && bsp->sourcePath.endsWith(QStringLiteral("build/maps/arena1.bsp")), "a build in the output folder ships at maps/<name>.bsp");
	QFile::rename(QDir(root).filePath(QStringLiteral("build/maps/arena1.bsp")), QDir(root).filePath(QStringLiteral("maps/arena1.bsp")));

	// The catalog offers the map, its build state and the texture folders.
	ProjectManifest manifest;
	loadProjectManifest(root, &manifest);
	const ReleaseCatalog catalog = releaseCatalog(manifest, QStringLiteral("quake3"));
	ok &= expect(catalog.maps.size() == 1 && catalog.maps.first().name == QStringLiteral("arena1") && catalog.maps.first().built,
		"the catalog lists the map with its build");
	ok &= expect(catalog.textureFolders.size() == 2, "the catalog lists texture folders", catalog.textureFolders.join(QLatin1Char(' ')));
	return ok;
}

bool runQuake3Project(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("q3"));
	const QString root = QDir(base).filePath(QStringLiteral("mymod"));
	auto stock = baseRegister(fixtures::profile(QStringLiteral("q3"), QStringLiteral("quake3"), game));
	ReleaseRequest whole = request(root, ReleaseScope::Project, {}, stock);
	ReleasePlan plan = planRelease(whole);
	const QStringList paths = entryPaths(plan);
	for (const QString& path : {QStringLiteral("readme.txt"), QStringLiteral("textures/mymod/unused.tga"), QStringLiteral("maps/arena1.bsp"),
			 QStringLiteral("scripts/mymod.shader"), QStringLiteral("maps/arena1/lm_0000.tga")}) {
		ok &= expect(paths.contains(path), "the whole project ships its content", path);
	}
	for (const QString& path : {QStringLiteral("src/art.psd"), QStringLiteral("notes.bak"), QStringLiteral("maps/arena1.prt"), QStringLiteral("maps/arena1.map"),
			 QStringLiteral("textures/base_wall/wall1.tga"), QStringLiteral(".vibestudio/project.json")}) {
		ok &= expect(!paths.contains(path), "sources, scratch files, metadata and stock copies stay out", path);
	}
	ok &= expect(plan.canPublish(), "the project plan can publish", problemKinds(plan));
	whole.release.exclude = {QStringLiteral("**/unused.tga")};
	whole.release.include = {QStringLiteral("src/*.psd")};
	whole.release.includeSources = true;
	plan = planRelease(whole);
	ok &= expect(!entryPaths(plan).contains(QStringLiteral("textures/mymod/unused.tga")), "exclude patterns win");
	ok &= expect(entryPaths(plan).contains(QStringLiteral("src/art.psd")) && entryPaths(plan).contains(QStringLiteral("maps/arena1.map")),
		"include patterns and Include sources ship source files");
	return ok;
}

bool runQuake3TexturesAndModels(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("q3"));
	const QString root = QDir(base).filePath(QStringLiteral("mymod"));
	auto stock = baseRegister(fixtures::profile(QStringLiteral("q3"), QStringLiteral("quake3"), game));
	ReleasePlan plan = planRelease(request(root, ReleaseScope::Textures, {QStringLiteral("textures/mymod")}, stock));
	ok &= expect(entryPaths(plan) == QStringList {QStringLiteral("scripts/mymod.shader"), QStringLiteral("textures/mymod/glow.tga"), QStringLiteral("textures/mymod/unused.tga"),
							QStringLiteral("textures/mymod/wall.tga")}, "a texture folder ships its images and the scripts declaring shaders in it",
		entryPaths(plan).join(QLatin1Char(' ')));

	fixtures::writeFile(QDir(root).filePath(QStringLiteral("models/mymod/statue.md3")),
		fixtures::md3({QByteArray("models/mymod/statue"), QByteArray("textures/base_wall/glass")}));
	fixtures::writeFile(QDir(root).filePath(QStringLiteral("models/mymod/statue.tga")), QByteArray("statue skin"));
	fixtures::writeFile(QDir(root).filePath(QStringLiteral("models/mymod/statue_red.skin")), QByteArray("surface0,models/mymod/statue_red.tga\ntag_head,\n"));
	fixtures::writeFile(QDir(root).filePath(QStringLiteral("models/mymod/statue_red.tga")), QByteArray("red skin"));
	plan = planRelease(request(root, ReleaseScope::Models, {QStringLiteral("models/mymod/statue.md3")}, stock));
	ok &= expect(entryPaths(plan) == QStringList {QStringLiteral("models/mymod/statue.md3"), QStringLiteral("models/mymod/statue.tga"),
							QStringLiteral("models/mymod/statue_red.skin"), QStringLiteral("models/mymod/statue_red.tga")},
		"a model ships with its skins and images", entryPaths(plan).join(QLatin1Char(' ')) + QStringLiteral(" / ") + problemKinds(plan));
	ok &= expect(stockPaths(plan).contains(QStringLiteral("scripts/base_wall.shader")), "a model's stock shader stays out");
	ok &= expect(plan.canPublish(), "the model plan can publish", problemKinds(plan));
	return ok;
}

bool runQuake(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("quake"));
	ok &= expect(fixtures::writePackage(QDir(game).filePath(QStringLiteral("id1/pak0.pak")), PackageArchiveFormat::Pak,
		{{QStringLiteral("progs/player.mdl"), QByteArray("IDPO")}, {QStringLiteral("sound/ambience/drone6.wav"), QByteArray("RIFF drone")}}), "the fake Quake pak is written");
	auto stock = baseRegister(fixtures::profile(QStringLiteral("quake"), QStringLiteral("quake"), game));
	const QString root = QDir(base).filePath(QStringLiteral("qmod"));
	ok &= fixtures::writeManifest(root, QStringLiteral("quake"), QJsonObject {{QStringLiteral("packageName"), QStringLiteral("start")}});
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("maps/start.map")),
		"{\n\"classname\" \"worldspawn\"\n\"wad\" \"q.wad\"\n\"sky\" \"mysky\"\n\"sounds\" \"4\"\n{\n"
		"( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) city4_6 0 0 0 1 1\n( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) city4_6 0 0 0 1 1\n"
		"( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) city4_6 0 0 0 1 1\n( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) city4_6 0 0 0 1 1\n}\n}\n"
		"{\n\"classname\" \"misc_noise\"\n\"origin\" \"16 16 16\"\n\"noise\" \"custom/hum.wav\"\n}\n"
		"{\n\"classname\" \"misc_noise\"\n\"origin\" \"8 8 8\"\n\"noise\" \"ambience/drone6.wav\"\n}\n");
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("maps/start.bsp")), QByteArray("BSP2"));
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("maps/start.lit")), QByteArray("QLIT"));
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("sound/custom/hum.wav")), QByteArray("RIFF hum"));
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("q.wad")), QByteArray("WAD2 texture source"));
	for (const QString& face : {QStringLiteral("rt"), QStringLiteral("bk"), QStringLiteral("lf"), QStringLiteral("ft"), QStringLiteral("up"), QStringLiteral("dn")}) {
		ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("gfx/env/mysky%1.tga").arg(face)), QByteArray("sky ") + face.toLatin1());
	}
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("music/track04.ogg")), QByteArray("OggS"));
	ReleasePlan plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/start.map")}, stock));
	const QStringList paths = entryPaths(plan);
	ok &= expect(paths.contains(QStringLiteral("maps/start.bsp")) && paths.contains(QStringLiteral("maps/start.lit")) && paths.contains(QStringLiteral("sound/custom/hum.wav"))
		&& paths.contains(QStringLiteral("gfx/env/myskyup.tga")) && paths.contains(QStringLiteral("music/track04.ogg")),
		"a Quake map ships its BSP, lighting, sounds, sky and music", paths.join(QLatin1Char(' ')));
	ok &= expect(!paths.contains(QStringLiteral("q.wad")) && !hasProblem(plan, QStringLiteral("missing"), true),
		"Quake textures live in the BSP: the WAD stays out and texture names are not checked", problemKinds(plan));
	ok &= expect(stockPaths(plan).contains(QStringLiteral("sound/ambience/drone6.wav")), "a stock Quake sound stays out");
	ok &= expect(plan.formatId == QStringLiteral("zip") && plan.packageFileName == QStringLiteral("start.zip"), "single Quake maps travel as loose files in a ZIP");

	ReleaseRequest pak = request(root, ReleaseScope::Project, {}, stock);
	plan = planRelease(pak);
	ok &= expect(plan.formatId == QStringLiteral("pak") && plan.packageFileName == QStringLiteral("start.pak") && !plan.warnings.filter(QStringLiteral("numbered")).isEmpty(),
		"a base-folder PAK warns that Quake loads only numbered packs");
	pak.release.gameFolder = QStringLiteral("mymod");
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("gamex86.dll")), QByteArray("MZ"));
	plan = planRelease(pak);
	ok &= expect(plan.packageFileName == QStringLiteral("pak0.pak") && plan.packageFolder == QStringLiteral("mymod"), "a mod folder's PAK is pak0.pak");
	const ReleaseEntry* code = plan.entry(QStringLiteral("gamex86.dll"));
	ok &= expect(code && code->loose && code->role == QStringLiteral("code"), "native game code ships beside the package");
	return ok;
}

bool runDoom(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("doom"));
	ok &= fixtures::writeFile(QDir(game).filePath(QStringLiteral("doom2.wad")), fixtures::doom2Iwad());
	auto stock = baseRegister(fixtures::profile(QStringLiteral("doom"), QStringLiteral("doom"), game));
	ok &= expect(stock != nullptr, "the Doom register builds");
	const QString root = QDir(base).filePath(QStringLiteral("dmod"));
	ok &= fixtures::writeManifest(root, QStringLiteral("doom"), QJsonObject {{QStringLiteral("packageName"), QStringLiteral("mywad")}});
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	create.wallTexture = QStringLiteral("STONE");
	create.floorTexture = QStringLiteral("MYFLAT");
	create.ceilingTexture = QStringLiteral("CEIL");
	LevelMapDocument map;
	QString error;
	ok &= expect(createLevelMap(create, &map, &error), "the Doom map is created", error);
	QVector<doom::Lump> lumps = doom::lumps(serializeLevelMap(map).bytes);
	const QVector<doom::Lump> stockAssets = doom::assets();
	lumps << stockAssets.first(); // PLAYPAL, identical to the IWAD's
	lumps << doom::Lump {"FF_START", {}} << doom::Lump {"MYFLAT", QByteArray(4096, char(33))} << doom::Lump {"FF_END", {}};
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("maps/mywad.wad")), doom::wad(lumps));
	ok &= fixtures::writeFile(QDir(root).filePath(QStringLiteral("mywad.txt")), QByteArray("author notes"));
	ReleasePlan plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/mywad.wad")}, stock));
	QStringList names = entryPaths(plan);
	ok &= expect(plan.formatId == QStringLiteral("wad") && plan.packageFileName == QStringLiteral("mywad.wad"), "Doom releases a WAD");
	ok &= expect(!names.contains(QStringLiteral("PLAYPAL")) && names.contains(QStringLiteral("MYFLAT")) && names.contains(QStringLiteral("FF_START"))
		&& names.first() == QStringLiteral("MAP01") && names.contains(QStringLiteral("THINGS")),
		"the map and custom flat ship; a lump identical to the IWAD's stays out", names.join(QLatin1Char(' ')));
	ok &= expect(plan.canPublish(), "the Doom plan can publish", problemKinds(plan));
	ok &= expect(stockPaths(plan).contains(QStringLiteral("STONE")) && stockPaths(plan).contains(QStringLiteral("PLAYPAL")), "stock textures and lumps are listed as the game's");
	ok &= expect(plan.maps.size() == 1 && plan.maps.first().doomMaps == QStringList {QStringLiteral("MAP01")}, "the WAD's maps are named");

	// Without the IWAD's index the stock names are unverified.
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/mywad.wad")}, nullptr));
	ok &= expect(plan.canPublish() && hasProblem(plan, QStringLiteral("unverified"), false), "without an index Doom stock names are unverified", problemKinds(plan));
	return ok;
}

bool runChangelogAndVersions()
{
	bool ok = true;
	const QString text = QStringLiteral(
		"# Changelog\r\n\r\nNotes about this file.\r\n\r\n## [Unreleased]\r\n\r\n### Fixed\r\n- A gap near the stairs.\r\n\r\n"
		"## [1.0.0] - 2026-10-01\r\n\r\n### Added\r\n- First release,\r\n  with a wrapped line.\r\n\r\nSome free text kept as written.\r\n\r\n[1.0.0]: https://example.com/1.0.0\r\n");
	ProjectChangelog changelog;
	ok &= expect(parseProjectChangelog(text, &changelog), "a changelog parses");
	ok &= expect(projectChangelogText(changelog) == text, "an untouched changelog prints back byte for byte, CRLF and all");
	ok &= expect(changelog.versions() == QStringList {QStringLiteral("1.0.0")} && changelog.unreleasedIndex() == 0, "versions and the Unreleased section are found");
	const QVector<ChangelogEntry> released = changelog.section(QStringLiteral("1.0.0"))->entries();
	ok &= expect(released.size() == 1 && released.first().text == QStringLiteral("First release, with a wrapped line."), "wrapped bullets join into one entry");
	QString error;
	ok &= expect(addChangelogEntry(&changelog, QStringLiteral("add"), QStringLiteral("New arena:\nThe Pit"), &error), "an Added entry is recorded", error);
	ok &= expect(addChangelogEntry(&changelog, QStringLiteral("fix"), QStringLiteral("A missing texture."), &error), "a second Fixed entry is recorded");
	ok &= expect(!addChangelogEntry(&changelog, QStringLiteral("whatever"), QStringLiteral("x"), &error), "an unknown category is refused");
	const QVector<ChangelogEntry> unreleased = changelog.unreleasedEntries();
	ok &= expect(unreleased.size() == 3 && unreleased[0].category == QStringLiteral("Added") && unreleased[0].text == QStringLiteral("New arena: The Pit")
		&& unreleased[2].text == QStringLiteral("A missing texture."), "entries land under their category, Added before Fixed");
	ok &= expect(releaseProjectChangelog(&changelog, QStringLiteral("1.1.0"), QDate(2026, 10, 8), &error), "Unreleased becomes 1.1.0", error);
	ok &= expect(changelog.unreleasedEntries().isEmpty() && changelog.section(QStringLiteral("1.1.0")) && changelog.section(QStringLiteral("1.1.0"))->date == QStringLiteral("2026-10-08")
		&& changelog.section(QStringLiteral("1.1.0"))->entries().size() == 3, "the released section keeps its entries and date");
	ok &= expect(!releaseProjectChangelog(&changelog, QStringLiteral("1.1.0"), QDate(2026, 10, 9), &error), "a version is released once");
	ok &= expect(projectChangelogText(changelog).contains(QStringLiteral("Some free text kept as written.")) && projectChangelogText(changelog).contains(QStringLiteral("[1.0.0]: https://example.com/1.0.0")),
		"text the parser does not understand is kept");
	ProjectChangelog fresh = defaultProjectChangelog(QStringLiteral("The Pit"));
	ok &= expect(fresh.unreleasedIndex() == 0 && projectChangelogText(fresh).contains(QStringLiteral("Keep a Changelog")), "a new changelog says its format");

	ok &= expect(bumpReleaseVersion(QStringLiteral("1.2.3"), QStringLiteral("patch")) == QStringLiteral("1.2.4"), "patch bump");
	ok &= expect(bumpReleaseVersion(QStringLiteral("1.2.3"), QStringLiteral("minor")) == QStringLiteral("1.3.0"), "minor bump");
	ok &= expect(bumpReleaseVersion(QStringLiteral("1.2.3"), QStringLiteral("major")) == QStringLiteral("2.0.0"), "major bump");
	ok &= expect(bumpReleaseVersion(QStringLiteral("1.0.0-beta.2"), QStringLiteral("patch")) == QStringLiteral("1.0.0"), "a pre-release bumps to its release");
	ok &= expect(bumpReleaseVersion(QStringLiteral("v2"), QStringLiteral("patch")) == QStringLiteral("v2.0.1"), "a short v-prefixed version keeps its prefix");
	ok &= expect(bumpReleaseVersion(QStringLiteral("beta"), QStringLiteral("patch")) == QStringLiteral("beta.1"), "a free-text version gains a number");
	ok &= expect(isSemanticVersion(QStringLiteral("1.0.0+build.5")) && !isSemanticVersion(QStringLiteral("1.0")), "Semantic Versioning is recognised");
	ok &= expect(isValidReleaseVersion(QStringLiteral("1.0.0-rc1")) && !isValidReleaseVersion(QStringLiteral("../evil")) && !isValidReleaseVersion(QString()),
		"versions are file-name safe");
	return ok;
}

bool runRecordsAndNotes(const QString& base)
{
	bool ok = true;
	const QString root = QDir(base).filePath(QStringLiteral("records"));
	QDir().mkpath(root);
	ReleaseRecord first;
	first.version = QStringLiteral("1.0.0");
	first.title = QStringLiteral("The Pit");
	first.publishedUtc = QDateTime(QDate(2026, 10, 1), QTime(12, 0), QTimeZone::UTC);
	first.files = {{QStringLiteral("maps/arena1.bsp"), QStringLiteral("map"), 10, 1}, {QStringLiteral("textures/a.tga"), QStringLiteral("texture"), 5, 2},
		{QStringLiteral("sound/old.wav"), QStringLiteral("sound"), 3, 3}};
	QString error;
	QString path;
	ok &= expect(saveReleaseRecord(root, first, false, &path, &error) && QFileInfo::exists(path), "a release record saves", error);
	ok &= expect(!saveReleaseRecord(root, first, false, &path, &error), "a version is recorded once without overwrite");
	ReleaseRecord second = first;
	second.version = QStringLiteral("1.1.0");
	second.publishedUtc = first.publishedUtc.addDays(1);
	ok &= expect(saveReleaseRecord(root, second, false, &path, &error), "a second record saves");
	const QVector<ReleaseRecord> records = listReleaseRecords(root);
	ok &= expect(records.size() == 2 && records.last().version == QStringLiteral("1.1.0"), "records list oldest first");
	ok &= expect(latestReleaseRecord(root, QStringLiteral("1.1.0")) && latestReleaseRecord(root, QStringLiteral("1.1.0"))->version == QStringLiteral("1.0.0"),
		"the latest other release is found");
	const QVector<ReleaseRecordFile> current {{QStringLiteral("maps/arena1.bsp"), QStringLiteral("map"), 11, 9}, {QStringLiteral("maps/arena2.bsp"), QStringLiteral("map"), 4, 4},
		{QStringLiteral("textures/a.tga"), QStringLiteral("texture"), 5, 2}};
	const ReleaseInventoryDiff diff = diffReleaseInventories(first, current);
	ok &= expect(diff.added.size() == 1 && diff.changed.size() == 1 && diff.removed.size() == 1 && diff.unchanged == 1, "the diff finds added, changed, removed and unchanged");
	const QStringList summary = releaseDiffSummaryLines(diff);
	ok &= expect(summary.join(QLatin1Char('|')).contains(QStringLiteral("arena2")) && summary.join(QLatin1Char('|')).contains(QStringLiteral("Sounds removed")),
		"the diff summary names maps and counts other files", summary.join(QLatin1Char('|')));
	const QVector<ChangelogEntry> suggested = suggestedChangelogEntries(diff);
	ok &= expect(!suggested.isEmpty() && suggested.first().category == QStringLiteral("Added") && suggested.first().text.contains(QStringLiteral("arena2")),
		"changes are suggested from the diff");
	ok &= expect(suggestedChangelogEntries({}).first().text == QStringLiteral("First release."), "a first release suggests itself");

	ReleaseNotesInput input;
	input.release.title = QStringLiteral("The Pit");
	input.release.version = QStringLiteral("1.1.0");
	input.release.authors = {QStringLiteral("Ada <ada@example.com>")};
	input.release.license = QStringLiteral("Free to share.");
	input.release.requirements = {{QStringLiteral("Team Arena"), QStringLiteral("missionpack"), QStringLiteral("https://example.com/ta")}};
	input.gameKey = QStringLiteral("quake3");
	input.releaseDate = QStringLiteral("2026-10-08");
	input.packageFileName = QStringLiteral("thepit.pk3");
	input.formatId = QStringLiteral("pk3");
	input.packageFolder = QStringLiteral("baseq3");
	ReleaseMapInfo map;
	map.name = QStringLiteral("arena1");
	map.title = QStringLiteral("The Pit");
	input.maps = {map};
	input.changes = {{QStringLiteral("Fixed"), QStringLiteral("A gap near the stairs.")}};
	input.diff = diff;
	input.composition = {{QStringLiteral("map"), 1, 11}, {QStringLiteral("texture"), 2, 2048}};
	input.fileCount = 3;
	input.totalBytes = 2059;
	input.studioVersion = QStringLiteral("0.1.0-test");
	const QString markdown = releaseNotesMarkdown(input);
	for (const QString& part : {QStringLiteral("# The Pit 1.1.0"), QStringLiteral("### Fixed"), QStringLiteral("A gap near the stairs."), QStringLiteral("Compared with 1.0.0"),
			 QStringLiteral("Copy thepit.pk3 into your Quake III Arena installation's baseq3 folder."), QStringLiteral("map arena1"), QStringLiteral("[Team Arena](https://example.com/ta)"),
			 releasePackageHashToken(), releasePackageSizeToken(), QStringLiteral("Free to share.")}) {
		ok &= expect(markdown.contains(part), "the release notes hold their parts", part);
	}
	const QString filled = fillReleasePackageTokens(markdown, QStringLiteral("abc123"), 2048);
	ok &= expect(!filled.contains(releasePackageHashToken()) && filled.contains(QStringLiteral("abc123")), "tokens fill in the package hash and size");
	const QString readme = releaseReadmeText(input);
	for (const QString& part : {QStringLiteral("Title                   : The Pit"), QStringLiteral("Author                  : Ada"), QStringLiteral("Email Address           : ada@example.com"),
			 QStringLiteral("* Play Information *"), QStringLiteral("Map #                   : arena1"), QStringLiteral("Other files required    : Team Arena")}) {
		ok &= expect(readme.contains(part), "the readme follows the familiar field layout", part);
	}
	ok &= expect(readme.contains(QStringLiteral("\r\n")), "the readme uses CRLF line endings");
	input.gameKey = QStringLiteral("doom");
	input.packageFileName = QStringLiteral("mywad.wad");
	input.maps.first().doomMaps = {QStringLiteral("MAP07")};
	ok &= expect(releaseInstallSteps(input).join(QLatin1Char(' ')).contains(QStringLiteral("-iwad doom2.wad -file mywad.wad -warp 7")), "Doom steps start the port on the map");
	input.gameKey = QStringLiteral("quake");
	input.formatId = QStringLiteral("pak");
	input.packageFolder = QStringLiteral("mymod");
	input.packageFileName = QStringLiteral("pak0.pak");
	input.maps.first().doomMaps.clear();
	ok &= expect(releaseInstallSteps(input).join(QLatin1Char(' ')).contains(QStringLiteral("-game mymod")), "a Quake mod starts with -game");
	return ok;
}

bool runPublish(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("q3"));
	const QString root = QDir(base).filePath(QStringLiteral("mymod"));
	auto stock = baseRegister(fixtures::profile(QStringLiteral("q3"), QStringLiteral("quake3"), game));
	const ReleasePlan plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/arena1.map")}, stock));
	ok &= expect(plan.canPublish(), "the plan to publish is ready", problemKinds(plan));
	ProjectManifest manifest;
	loadProjectManifest(root, &manifest);
	const QString output = defaultReleaseOutputDirectory(manifest, plan.release);
	ok &= expect(output.endsWith(QStringLiteral("build/releases/thepit-1.0.0")), "releases default under the output folder", output);
	ok &= expect(!releaseOutputProblem(plan, root, false).isEmpty(), "the project's own content folder is refused as output");
	ok &= expect(!releaseOutputProblem(plan, QStringLiteral("relative/folder"), false).isEmpty(), "a relative output folder is refused");

	ProjectChangelog changelog;
	loadProjectChangelog(QDir(root).filePath(QStringLiteral("CHANGELOG.md")), QStringLiteral("The Pit"), &changelog);
	ReleaseNotesInput input = releaseNotesInput(plan, {{QStringLiteral("Added"), QStringLiteral("First release.")}}, {}, QDate(2026, 10, 8));
	ReleasePublishRequest publish;
	publish.plan = plan;
	publish.projectRoot = root;
	publish.outputDirectory = output;
	publish.notesMarkdown = releaseNotesMarkdown(input);
	publish.readmeText = releaseReadmeText(input);
	publish.updateChangelog = true;
	publish.changelog = changelog;
	publish.changelogAdditions = {{QStringLiteral("Added"), QStringLiteral("First release.")}};
	publish.releaseDate = QDate(2026, 10, 8);

	ReleasePublishRequest dry = publish;
	dry.dryRun = true;
	const ReleasePublishResult dryResult = publishRelease(dry);
	ok &= expect(dryResult.succeeded && dryResult.dryRun && !QFileInfo::exists(output) && !dryResult.packageSha256.isEmpty(),
		"a dry run measures the package and writes nothing", dryResult.error);

	const ReleasePublishResult result = publishRelease(publish);
	ok &= expect(result.succeeded && result.packageCommitted, "the release publishes", result.error);
	ok &= expect(packageEntries(result.packagePath) == entryPaths(plan), "the package holds exactly the planned files", packageEntries(result.packagePath).join(QLatin1Char(' ')));
	ok &= expect(packageEntries(result.archivePath) == QStringList {QStringLiteral("thepit.pk3"), QStringLiteral("thepit.txt")},
		"the distribution archive holds the package and readme", packageEntries(result.archivePath).join(QLatin1Char(' ')));
	const QString notes = QString::fromUtf8(readAll(result.notesPath));
	ok &= expect(notes.contains(result.packageSha256) && !notes.contains(releasePackageHashToken()), "the notes name the written package's hash");
	ok &= expect(QString::fromUtf8(readAll(result.readmePath)).contains(result.packageSha256), "the readme names the package hash too");
	const auto record = latestReleaseRecord(root);
	ok &= expect(record && record->version == QStringLiteral("1.0.0") && record->packageSha256 == result.packageSha256
		&& record->outputDirectory == QStringLiteral("build/releases/thepit-1.0.0") && record->files.size() == plan.entries.size(),
		"the release record keeps the inventory and the project-relative folder", record ? record->outputDirectory : QString());
	ProjectChangelog after;
	loadProjectChangelog(QDir(root).filePath(QStringLiteral("CHANGELOG.md")), QStringLiteral("The Pit"), &after);
	ok &= expect(after.section(QStringLiteral("1.0.0")) && after.section(QStringLiteral("1.0.0"))->entries().size() == 1 && after.unreleasedEntries().isEmpty(),
		"the changelog moves the release's changes under its version");
	ok &= expect(!releaseOutputProblem(plan, output, false).isEmpty(), "an existing release folder is refused without replacing");

	// Replacing keeps backups.
	ReleasePublishRequest again = publish;
	again.overwrite = true;
	again.updateChangelog = false;
	const ReleasePublishResult replaced = publishRelease(again);
	ok &= expect(replaced.succeeded && !replaced.backupPaths.isEmpty() && QFileInfo::exists(result.packagePath + QStringLiteral(".bak")),
		"replacing a release keeps .bak copies", replaced.error);
	return ok;
}

bool runPublishDoomAndZip(const QString& base)
{
	bool ok = true;
	const QString doomGame = QDir(base).filePath(QStringLiteral("doom"));
	auto doomStock = baseRegister(fixtures::profile(QStringLiteral("doom"), QStringLiteral("doom"), doomGame));
	const QString doomRoot = QDir(base).filePath(QStringLiteral("dmod"));
	const ReleasePlan doomPlan = planRelease(request(doomRoot, ReleaseScope::Maps, {QStringLiteral("maps/mywad.wad")}, doomStock));
	ReleasePublishRequest publish;
	publish.plan = doomPlan;
	publish.projectRoot = doomRoot;
	publish.outputDirectory = QDir(base).filePath(QStringLiteral("out/doom"));
	publish.readmeText = QStringLiteral("readme");
	const ReleasePublishResult result = publishRelease(publish);
	ok &= expect(result.succeeded, "the Doom release publishes", result.error);
	QStringList lumps = packageEntries(result.packagePath);
	QStringList planned = entryPaths(doomPlan);
	ok &= expect(lumps == planned, "the merged WAD holds the planned lumps in order", lumps.join(QLatin1Char(' ')));
	ok &= expect(readAll(result.packagePath).startsWith("PWAD"), "the merged WAD is a PWAD");

	const QString quakeGame = QDir(base).filePath(QStringLiteral("quake"));
	auto quakeStock = baseRegister(fixtures::profile(QStringLiteral("quake"), QStringLiteral("quake"), quakeGame));
	const QString quakeRoot = QDir(base).filePath(QStringLiteral("qmod"));
	const ReleasePlan zipPlan = planRelease(request(quakeRoot, ReleaseScope::Maps, {QStringLiteral("maps/start.map")}, quakeStock));
	publish = {};
	publish.plan = zipPlan;
	publish.projectRoot = quakeRoot;
	publish.outputDirectory = QDir(base).filePath(QStringLiteral("out/quake"));
	publish.readmeText = QStringLiteral("Quake readme ") + releasePackageHashToken();
	publish.writeRecord = false;
	const ReleasePublishResult zip = publishRelease(publish);
	ok &= expect(zip.succeeded && zip.archivePath.isEmpty(), "a ZIP release is its own distribution archive", zip.error);
	const QStringList zipEntries = packageEntries(zip.packagePath);
	ok &= expect(zipEntries.contains(QStringLiteral("maps/start.bsp")) && zipEntries.contains(QStringLiteral("start.txt")), "the ZIP holds game files and the readme",
		zipEntries.join(QLatin1Char(' ')));

	ReleaseRequest modRequest = request(quakeRoot, ReleaseScope::Project, {}, quakeStock);
	modRequest.release.gameFolder = QStringLiteral("mymod");
	const ReleasePlan modPlan = planRelease(modRequest);
	publish = {};
	publish.plan = modPlan;
	publish.projectRoot = quakeRoot;
	publish.outputDirectory = QDir(base).filePath(QStringLiteral("out/quakemod"));
	publish.writeRecord = false;
	const ReleasePublishResult mod = publishRelease(publish);
	ok &= expect(mod.succeeded, "a Quake mod publishes", mod.error);
	const QStringList archive = packageEntries(mod.archivePath);
	ok &= expect(archive.contains(QStringLiteral("mymod/pak0.pak")) && archive.contains(QStringLiteral("mymod/gamex86.dll")),
		"the distribution archive puts the mod in its folder, with native code beside the PAK", archive.join(QLatin1Char(' ')));
	ok &= expect(!packageEntries(mod.packagePath).contains(QStringLiteral("gamex86.dll")) && QFileInfo::exists(QDir(publish.outputDirectory).filePath(QStringLiteral("mymod/gamex86.dll"))),
		"native code stays out of the PAK and is written beside it");
	return ok;
}

// --- Game code and scripts -----------------------------------------------------

// "kind reference" for each reference the scan found, or for the given ones.
QStringList scanned(const ScriptReferenceScan& scan, const QVector<int>& only = {}, bool useOnly = false)
{
	QStringList rows;
	for (qsizetype i = 0; i < scan.references.size(); ++i) {
		if (!useOnly || only.contains(int(i))) {
			rows << scan.references[i].kind + QLatin1Char(' ') + scan.references[i].reference;
		}
	}
	return rows;
}

const ScriptAssetReference* scannedReference(const ScriptReferenceScan& scan, const QString& reference)
{
	for (const ScriptAssetReference& row : scan.references) {
		if (row.reference.compare(reference, Qt::CaseInsensitive) == 0) {
			return &row;
		}
	}
	return nullptr;
}

const ReleaseScriptReference* scriptRow(const ReleasePlan& plan, const QString& reference)
{
	for (const ReleaseScriptReference& row : plan.scriptReferences) {
		if (row.reference.compare(reference, Qt::CaseInsensitive) == 0) {
			return &row;
		}
	}
	return nullptr;
}

QString rowStatus(const ReleasePlan& plan, const QString& reference)
{
	const ReleaseScriptReference* row = scriptRow(plan, reference);
	return row ? row->status : QStringLiteral("<none>");
}

bool requiredBy(const ReleasePlan& plan, const QString& path, const QString& script)
{
	const ReleaseEntry* entry = plan.entry(path);
	return entry && entry->requiredBy.contains(script);
}

const ReleaseReference* stockReference(const ReleasePlan& plan, const QString& path)
{
	for (const ReleaseReference& reference : plan.stock) {
		if (reference.path.compare(path, Qt::CaseInsensitive) == 0) {
			return &reference;
		}
	}
	return nullptr;
}

// Each script language on its own, as text.
bool runScriptScanner(const QString& base)
{
	bool ok = true;
	ScriptReferenceScanOptions quake;
	quake.gameKey = QStringLiteral("quake");

	ScriptReferenceScan qc;
	scanScriptText(QStringLiteral("quakec"), QString::fromUtf8(fixtures::fiendQuakeC()), QStringLiteral("qc/fiend.qc"), quake, &qc);
	const QStringList found = scanned(qc);
	for (const QString& row : {QStringLiteral("model progs/fiend.mdl"), QStringLiteral("sound fiend/roar.wav"), QStringLiteral("sound fiend/idle.wav"),
			 QStringLiteral("sound ambience/drone6.wav"), QStringLiteral("sound fiend/gone.wav"), QStringLiteral("model progs/rune.mdl")}) {
		ok &= expect(found.contains(row), "QuakeC precaches, sounds and models are found", row + QStringLiteral(" in ") + found.join(QStringLiteral(", ")));
	}
	ok &= expect(found.filter(QStringLiteral("comment")).isEmpty() && !found.contains(QStringLiteral("model *1")) && found.filter(QStringLiteral("noise")).isEmpty(),
		"comments, inline brush models and non-literal arguments are not references", found.join(QStringLiteral(", ")));
	const ScriptAssetReference* fiend = scannedReference(qc, QStringLiteral("progs/fiend.mdl"));
	ok &= expect(fiend && fiend->line == 23 && fiend->owner == QStringLiteral("monster_fiend") && fiend->via == QStringLiteral("precache_model")
			&& fiend->candidates == QStringList {QStringLiteral("progs/fiend.mdl")},
		"a precache keeps its line, function and call", fiend ? QStringLiteral("%1 %2 %3").arg(fiend->line).arg(fiend->owner, fiend->via) : QString());
	const ScriptAssetReference* idle = scannedReference(qc, QStringLiteral("fiend/idle.wav"));
	ok &= expect(idle && idle->candidates.first() == QStringLiteral("sound/fiend/idle.wav"), "QuakeC sounds are found under sound/",
		idle ? idle->candidates.join(QLatin1Char(' ')) : QString());
	ok &= expect(qc.unit(QStringLiteral("function"), QStringLiteral("monster_fiend")) && qc.unit(QStringLiteral("function"), QStringLiteral("fiend_stand1"))
			&& qc.unit(QStringLiteral("function"), QStringLiteral("item_rune")), "old- and new-style QuakeC functions are units");
	const ScriptReachable spawned = scriptReferencesForEntityClasses(qc, {QStringLiteral("worldspawn"), QStringLiteral("monster_fiend")});
	const QStringList reached = scanned(qc, spawned.references, true);
	ok &= expect(reached.contains(QStringLiteral("sound fiend/roar.wav")) && !reached.contains(QStringLiteral("model progs/rune.mdl"))
			&& spawned.units == QStringList {QStringLiteral("monster_fiend"), QStringLiteral("fiend_sounds")} && spawned.scripts == QStringList {QStringLiteral("qc/fiend.qc")},
		"a class reaches the functions it calls, not those it only names", reached.join(QStringLiteral(", ")) + QStringLiteral(" / ") + spawned.units.join(QLatin1Char(' ')));

	ScriptReferenceScanOptions quake3;
	quake3.gameKey = QStringLiteral("quake3");
	ScriptReferenceScan menu;
	scanScriptText(QStringLiteral("menu"), QString::fromUtf8(fixtures::quake3MenuText()), QStringLiteral("ui/mymain.menu"), quake3, &menu);
	const QStringList menuRows = scanned(menu);
	for (const QString& row : {QStringLiteral("script ui/menudef.h"), QStringLiteral("image ui/assets/mycursor"), QStringLiteral("sound sound/misc/missing.wav"),
			 QStringLiteral("image ui/assets/mybg.tga"), QStringLiteral("model models/mapobjects/barrel.md3"), QStringLiteral("image textures/skies/stocksky"),
			 QStringLiteral("sound sound/mymod/click.wav"), QStringLiteral("script mymod.cfg")}) {
		ok &= expect(menuRows.contains(row), "menu art, models, sounds, includes and exec'd configs are found", row + QStringLiteral(" in ") + menuRows.join(QStringLiteral(", ")));
	}
	ok &= expect(!menuRows.contains(QStringLiteral("image main")) && !menuRows.contains(QStringLiteral("image barrel")), "names are not files");
	const ScriptAssetReference* cursor = scannedReference(menu, QStringLiteral("ui/assets/mycursor"));
	ok &= expect(cursor && cursor->candidates.first() == QStringLiteral("ui/assets/mycursor.tga") && cursor->shaderName == QStringLiteral("ui/assets/mycursor"),
		"a menu image without an extension tries image files, then a shader", cursor ? cursor->candidates.join(QLatin1Char(' ')) : QString());
	const ScriptAssetReference* background = scannedReference(menu, QStringLiteral("ui/assets/mybg.tga"));
	ok &= expect(background && background->line == 11 && background->via == QStringLiteral("background"), "a menu keyword keeps its line");

	ScriptReferenceScan config;
	scanScriptText(QStringLiteral("config"), QString::fromUtf8(fixtures::quake3ConfigText()), QStringLiteral("mymod.cfg"), quake3, &config);
	const ScriptAssetReference* exec = scannedReference(config, QStringLiteral("other"));
	const ScriptAssetReference* bound = scannedReference(config, QStringLiteral("sound/world/hum.wav"));
	ok &= expect(exec && exec->candidates.contains(QStringLiteral("other.cfg")) && bound && bound->candidates.first() == QStringLiteral("sound/world/hum.wav")
			&& config.references.size() == 2, "configs follow exec and the commands a bind runs, not comments", scanned(config).join(QStringLiteral(", ")));
	ScriptReferenceScan quakeConfig;
	scanScriptText(QStringLiteral("config"), QStringLiteral("play misc/talk.wav; alias go \"exec go.cfg\"\nmap start"), QStringLiteral("autoexec.cfg"), quake, &quakeConfig);
	const ScriptAssetReference* talk = scannedReference(quakeConfig, QStringLiteral("misc/talk.wav"));
	ok &= expect(talk && talk->candidates.first() == QStringLiteral("sound/misc/talk.wav") && scannedReference(quakeConfig, QStringLiteral("go.cfg"))
			&& scannedReference(quakeConfig, QStringLiteral("start")) && scannedReference(quakeConfig, QStringLiteral("start"))->candidates.first() == QStringLiteral("maps/start.bsp"),
		"Quake configs play from sound/, follow aliases and name maps", scanned(quakeConfig).join(QStringLiteral(", ")));

	ScriptReferenceScanOptions doomOptions;
	doomOptions.gameKey = QStringLiteral("doom");
	ScriptReferenceScan doom;
	QStringList includes;
	scanScriptText(QStringLiteral("decorate"), QString::fromUtf8(fixtures::fireImpDecorate()), QStringLiteral("monsters.wad:DECORATE"), doomOptions, &doom, &includes);
	scanScriptText(QStringLiteral("decorate"), QString::fromUtf8(fixtures::fireBallDecorate()), QStringLiteral("monsters.wad:IMPBALL"), doomOptions, &doom);
	scanScriptText(QStringLiteral("sndinfo"), QStringLiteral("; Fire imps\nfireimp/sight dsfisit\nfireimp/boom \"sounds/fireimp/boom.ogg\"\n$random fireimp/pain { fireimp/sight fireimp/boom }\n$limit fireimp/sight 2\n"),
		QStringLiteral("monsters.wad:SNDINFO"), doomOptions, &doom);
	scanScriptText(QStringLiteral("mapinfo"), QStringLiteral("map MAP01 \"Fire\"\n{\n  music = \"D_FIRE\"\n  sky1 = \"FENCE\", 0\n  next = \"MAP02\"\n}\nDoomEdNums\n{\n  30003 = Spark, 1\n}\n"),
		QStringLiteral("monsters.wad:MAPINFO"), doomOptions, &doom);
	scanScriptText(QStringLiteral("mapinfo"), QStringLiteral("; Hexen style\nmap 1 \"Winnowing Hall\"\ncluster 1\nsky1 SKY2 0\nmusic hub1\n"), QStringLiteral("hexen:MAPINFO"),
		doomOptions, &doom);
	scanScriptText(QStringLiteral("zscript"),
		QStringLiteral("version \"4.10\"\nclass HotImp : DoomImp replaces DoomImp\n{\n\tDefault\n\t{\n\t\tSeeSound \"hotimp/sight\";\n\t\tInventory.Icon \"HIMPICON\";\n\t}\n"
					   "\tStates\n\t{\n\tSpawn:\n\t\tHIMP AB 10 A_Look;\n\t\tLoop;\n\tMissile:\n\t\tHIMP C 8 { A_SpawnProjectile(\"FireBall\"); }\n\t\tGoto See;\n\t}\n"
					   "\toverride void Tick() { super.Tick(); A_StartSound(\"hotimp/hiss\", CHAN_BODY); }\n}\n"),
		QStringLiteral("zscript.zs"), doomOptions, &doom);
	ok &= expect(includes == QStringList {QStringLiteral("IMPBALL")}, "DECORATE includes are reported", includes.join(QLatin1Char(' ')));
	const ScriptCodeUnit* imp = doom.unit(QStringLiteral("class"), QStringLiteral("fireimp"));
	ok &= expect(imp && imp->parent == QStringLiteral("DoomImp") && imp->editorNumber == 30001 && imp->uses.contains(QStringLiteral("FireBall")),
		"an actor's parent, editor number and the classes it fires are read", imp ? imp->uses.join(QLatin1Char(' ')) : QString());
	const QStringList doomRows = scanned(doom);
	ok &= expect(doomRows.contains(QStringLiteral("sprite FIMP")) && doomRows.contains(QStringLiteral("sprite UNUS")) && doomRows.contains(QStringLiteral("sprite HIMP"))
			&& doomRows.filter(QStringLiteral("TNT1")).isEmpty() && doomRows.filter(QStringLiteral("sprite FIMP")).size() == 1,
		"states give each sprite once, and TNT1 is no sprite", doomRows.join(QStringLiteral(", ")));
	const ScriptAssetReference* sight = scannedReference(doom, QStringLiteral("fireimp/sight"));
	const ScriptAssetReference* hiss = scannedReference(doom, QStringLiteral("hotimp/hiss"));
	const ScriptAssetReference* icon = scannedReference(doom, QStringLiteral("HIMPICON"));
	ok &= expect(sight && sight->soundName == QStringLiteral("fireimp/sight") && sight->via == QStringLiteral("SeeSound") && hiss && hiss->owner == QStringLiteral("HotImp")
			&& icon && icon->lumpName == QStringLiteral("HIMPICON"),
		"sound properties, sound calls and icons are read in DECORATE and ZScript");
	ok &= expect(doom.soundDefinition(QStringLiteral("fireimp/sight")) && doom.soundDefinition(QStringLiteral("fireimp/sight"))->target == QStringLiteral("dsfisit")
			&& scannedReference(doom, QStringLiteral("sounds/fireimp/boom.ogg")) && !doom.soundDefinition(QStringLiteral("$limit")),
		"SNDINFO maps logical names to lumps and files, and skips its commands");
	QStringList soundScripts;
	const QVector<int> pain = scriptSoundTargets(doom, QStringLiteral("fireimp/pain"), &soundScripts);
	ok &= expect(scanned(doom, pain, true) == QStringList {QStringLiteral("sound dsfisit"), QStringLiteral("sound sounds/fireimp/boom.ogg")}
			&& soundScripts == QStringList {QStringLiteral("monsters.wad:SNDINFO")},
		"a $random plays each member's lump or file", scanned(doom, pain, true).join(QStringLiteral(", ")));
	ok &= expect(doom.classForEditorNumber(30003) == QStringLiteral("Spark") && doom.classForEditorNumber(30001) == QStringLiteral("FireImp")
			&& doom.classForEditorNumber(3001) == QStringLiteral("DoomImp"), "editor numbers come from MAPINFO, DECORATE and the standard classes");
	const ScriptCodeUnit* hexen = doom.unit(QStringLiteral("map"), QStringLiteral("MAP01"));
	ok &= expect(hexen && scanned(doom, hexen->references, true).contains(QStringLiteral("texture SKY2")) && scanned(doom, hexen->references, true).contains(QStringLiteral("music hub1"))
			&& scanned(doom, hexen->references, true).contains(QStringLiteral("music D_FIRE")),
		"MAPINFO keys belong to their map, also in the old format where a map names its cluster", hexen ? scanned(doom, hexen->references, true).join(QStringLiteral(", ")) : QString());
	const ScriptReachable things = scriptReferencesForDoomThings(doom, {1, 30001, 3001}, {QStringLiteral("MAP01")});
	const QStringList thingRows = scanned(doom, things.references, true);
	ok &= expect(things.units.contains(QStringLiteral("FireImp")) && things.units.contains(QStringLiteral("FireBall")) && things.units.contains(QStringLiteral("HotImp"))
			&& !things.units.contains(QStringLiteral("Unused")) && thingRows.contains(QStringLiteral("sprite FBAL")) && thingRows.contains(QStringLiteral("sprite HIMP"))
			&& !thingRows.contains(QStringLiteral("sprite UNUS")),
		"a map's things reach their classes, the classes those fire, and replacements of stock classes", things.units.join(QLatin1Char(' ')) + QStringLiteral(" / ") + thingRows.join(QStringLiteral(", ")));
	for (const QString& script : {QStringLiteral("monsters.wad:DECORATE"), QStringLiteral("monsters.wad:IMPBALL"), QStringLiteral("monsters.wad:SNDINFO"),
			 QStringLiteral("monsters.wad:MAPINFO"), QStringLiteral("zscript.zs")}) {
		ok &= expect(things.scripts.contains(script), "the scripts that define what a map uses are needed", script + QStringLiteral(" in ") + things.scripts.join(QLatin1Char(' ')));
	}

	// Bounds and cancellation.
	ScriptReferenceScanOptions small = quake;
	small.maximumReferences = 3;
	ScriptReferenceScan bounded;
	scanScriptText(QStringLiteral("quakec"), QString::fromUtf8(fixtures::fiendQuakeC()), QStringLiteral("qc/fiend.qc"), small, &bounded);
	ok &= expect(bounded.references.size() == 3 && !bounded.complete && bounded.warnings.size() == 1, "the reference limit stops the scan and says so");
	ProjectContentReader project;
	ScriptReferenceScanOptions cancel;
	cancel.isCancelled = []() { return true; };
	const QString folder = QDir(base).filePath(QStringLiteral("scanned"));
	fixtures::writeFile(QDir(folder).filePath(QStringLiteral("qc/fiend.qc")), fixtures::fiendQuakeC());
	ProjectContentRoot root;
	root.path = folder;
	root.layerId = QStringLiteral("project");
	ok &= expect(project.load({root}), "the scanned folder is read");
	const ScriptReferenceScan cancelled = scanScriptReferences(project, {}, cancel);
	ok &= expect(cancelled.cancelled && !cancelled.complete && cancelled.references.isEmpty(), "a cancelled scan stops before reading");
	const ScriptReferenceScan full = scanScriptReferences(project);
	ok &= expect(full.complete && full.scripts.size() == 1 && full.references.size() == qc.references.size(), "a project's scripts are found by name and read");
	return ok;
}

// A Quake map that places a monster from the project's QuakeC.
bool runQuakeScripts(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("quake"));
	ok &= expect(fixtures::makeQuakeInstallation(game), "the fake Quake installation is written");
	auto stock = baseRegister(fixtures::profile(QStringLiteral("quake"), QStringLiteral("quake"), game));
	const QString root = QDir(base).filePath(QStringLiteral("qcmod"));
	ok &= expect(fixtures::makeQuakeCProject(root), "the QuakeC project is written");
	ReleasePlan plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/fiend.map")}, stock));
	ok &= expect(entryPaths(plan) == QStringList {QStringLiteral("maps/fiend.bsp"), QStringLiteral("progs/fiend.mdl"), QStringLiteral("sound/fiend/idle.wav"), QStringLiteral("sound/fiend/roar.wav")},
		"a map ships what its monster's spawn function and helpers precache, and nothing an unused class needs", entryPaths(plan).join(QLatin1Char(' ')));
	ok &= expect(requiredBy(plan, QStringLiteral("progs/fiend.mdl"), QStringLiteral("qc/fiend.qc")) && requiredBy(plan, QStringLiteral("sound/fiend/roar.wav"), QStringLiteral("qc/fiend.qc")),
		"each file the code loads names the script");
	const ReleaseReference* drone = stockReference(plan, QStringLiteral("sound/ambience/drone6.wav"));
	ok &= expect(drone && drone->requiredBy.contains(QStringLiteral("qc/fiend.qc")), "a stock sound the code precaches is the game's");
	ok &= expect(plan.canPublish() && hasProblem(plan, QStringLiteral("unresolved"), false) && !hasProblem(plan, QStringLiteral("unresolved"), true),
		"a precache nothing provides is an advisory, not a blocker", problemKinds(plan));
	ok &= expect(!plan.warnings.filter(QStringLiteral("compiled game code")).isEmpty(), "the review says the mod's compiled code is not in a map release");
	const ReleaseScriptReference* model = scriptRow(plan, QStringLiteral("progs/fiend.mdl"));
	ok &= expect(model && model->status == QStringLiteral("included") && model->line == 23 && model->owner == QStringLiteral("monster_fiend") && model->occurrences == 2
			&& model->requiredBy == QStringList {QStringLiteral("fiend")} && model->language == QStringLiteral("quakec"),
		"the plan records where the code names each file", model ? QStringLiteral("%1 %2 %3 %4").arg(model->status).arg(model->line).arg(model->owner).arg(model->occurrences) : QString());
	ok &= expect(rowStatus(plan, QStringLiteral("fiend/gone.wav")) == QStringLiteral("missing") && rowStatus(plan, QStringLiteral("ambience/drone6.wav")) == QStringLiteral("stock")
			&& !scriptRow(plan, QStringLiteral("progs/rune.mdl")) && plan.scriptsRead == 1,
		"missing and stock names are told apart, and an unused class is not followed");
	const QJsonArray json = releasePlanJson(plan).value(QStringLiteral("scriptReferences")).toArray();
	bool jsonRow = false;
	for (const QJsonValue& value : json) {
		const QJsonObject row = value.toObject();
		jsonRow = jsonRow || (row.value(QStringLiteral("reference")).toString() == QStringLiteral("progs/fiend.mdl") && row.value(QStringLiteral("status")).toString() == QStringLiteral("included")
			&& row.value(QStringLiteral("script")).toString() == QStringLiteral("qc/fiend.qc") && row.value(QStringLiteral("line")).toInt() == 23
			&& row.value(QStringLiteral("owner")).toString() == QStringLiteral("monster_fiend"));
	}
	ok &= expect(jsonRow && releasePlanText(plan).contains(QStringLiteral("Names in game code and scripts")), "the plan's JSON and text report the script references");

	// Without the game's index, unknown names are unverified.
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/fiend.map")}, nullptr));
	ok &= expect(plan.canPublish() && hasProblem(plan, QStringLiteral("unverified"), false) && rowStatus(plan, QStringLiteral("fiend/gone.wav")) == QStringLiteral("unverified"),
		"without an index the code's unknown names are unverified", problemKinds(plan));

	// The whole project checks every name, and the QuakeC source stays out.
	plan = planRelease(request(root, ReleaseScope::Project, {}, stock));
	ok &= expect(requiredBy(plan, QStringLiteral("progs/rune.mdl"), QStringLiteral("qc/fiend.qc")) && requiredBy(plan, QStringLiteral("progs/rune.mdl"), QStringLiteral("project"))
			&& !entryPaths(plan).contains(QStringLiteral("qc/fiend.qc")) && hasProblem(plan, QStringLiteral("unresolved"), false) && plan.canPublish(),
		"a whole project names the scripts behind its files", entryPaths(plan).join(QLatin1Char(' ')) + QStringLiteral(" / ") + problemKinds(plan));

	// Reading the scripts can be cancelled.
	ReleaseRequest cancelled = request(root, ReleaseScope::Maps, {QStringLiteral("maps/fiend.map")}, stock);
	cancelled.progress = [](const QString& phase, int, int) { return !phase.contains(QStringLiteral("game code")); };
	plan = planRelease(cancelled);
	ok &= expect(plan.cancelled && !plan.complete, "cancelling while the scripts are read cancels the plan");
	return ok;
}

// Quake III menus and configs in a whole-project release.
bool runQuake3Menus(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("q3"));
	ok &= expect(fixtures::makeQuake3Installation(game), "the fake Quake III installation is written");
	auto stock = baseRegister(fixtures::profile(QStringLiteral("q3"), QStringLiteral("quake3"), game));
	const QString root = QDir(base).filePath(QStringLiteral("menumod"));
	ok &= expect(fixtures::writeManifest(root, QStringLiteral("quake3"), QJsonObject {{QStringLiteral("packageName"), QStringLiteral("menumod")}})
			&& fixtures::addQuake3MenuScripts(root), "the menu project is written");
	const ReleasePlan plan = planRelease(request(root, ReleaseScope::Project, {}, stock));
	ok &= expect(plan.canPublish(), "the menu project can publish", problemKinds(plan));
	const QString menu = QStringLiteral("ui/mymain.menu");
	ok &= expect(requiredBy(plan, QStringLiteral("ui/assets/mybg.tga"), menu) && requiredBy(plan, QStringLiteral("ui/assets/mycursor.tga"), menu)
			&& requiredBy(plan, QStringLiteral("ui/menudef.h"), menu) && requiredBy(plan, QStringLiteral("sound/mymod/click.wav"), menu)
			&& requiredBy(plan, QStringLiteral("mymod.cfg"), menu) && requiredBy(plan, menu, QStringLiteral("ui/menus.txt"))
			&& requiredBy(plan, QStringLiteral("other.cfg"), QStringLiteral("mymod.cfg")),
		"menus and configs name the files they load, other menus and configs included");
	ok &= expect(plan.entry(QStringLiteral("ui/assets/mybg.tga")) && plan.entry(QStringLiteral("ui/assets/mybg.tga"))->role == QStringLiteral("texture"), "menu art counts as a texture");
	const ReleaseReference* barrel = stockReference(plan, QStringLiteral("models/mapobjects/barrel.md3"));
	const ReleaseReference* hum = stockReference(plan, QStringLiteral("sound/world/hum.wav"));
	const ReleaseReference* sky = stockReference(plan, QStringLiteral("scripts/base_wall.shader"));
	ok &= expect(barrel && barrel->requiredBy.contains(menu) && hum && hum->requiredBy.contains(QStringLiteral("mymod.cfg")) && sky && sky->requiredBy.contains(menu),
		"stock models, sounds and shaders named by menus and configs are the game's");
	ok &= expect(rowStatus(plan, QStringLiteral("sound/misc/missing.wav")) == QStringLiteral("missing") && hasProblem(plan, QStringLiteral("unresolved"), false),
		"a menu sound nothing provides is an advisory", problemKinds(plan));
	const ReleaseScriptReference* background = scriptRow(plan, QStringLiteral("ui/assets/mybg.tga"));
	ok &= expect(background && background->line == 11 && background->via == QStringLiteral("background") && background->language == QStringLiteral("menu")
			&& plan.scriptSourceIndex().value(QStringLiteral("ui/assets/mybg.tga")).size() == 1,
		"the plan indexes the menu line behind each file");
	return ok;
}

// A Doom map that places a DECORATE monster from a resource WAD.
bool runDoomScripts(const QString& base)
{
	bool ok = true;
	const QString game = QDir(base).filePath(QStringLiteral("doom"));
	ok &= fixtures::writeFile(QDir(game).filePath(QStringLiteral("doom2.wad")), fixtures::doom2Iwad());
	auto stock = baseRegister(fixtures::profile(QStringLiteral("doom"), QStringLiteral("doom"), game));
	const QString root = QDir(base).filePath(QStringLiteral("decomod"));
	ok &= expect(fixtures::makeDecorateProject(root), "the DECORATE project is written");
	ReleasePlan plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/fireimps.wad")}, stock));
	const QStringList names = entryPaths(plan);
	for (const QString& lump : {QStringLiteral("DECORATE"), QStringLiteral("IMPBALL"), QStringLiteral("SNDINFO"), QStringLiteral("MAPINFO"), QStringLiteral("FIMPA1"),
			 QStringLiteral("FBALA0"), QStringLiteral("DSFISIT"), QStringLiteral("D_FIRE"), QStringLiteral("MAP01")}) {
		ok &= expect(names.contains(lump), "a map placing a mod monster ships the WAD that defines it", lump + QStringLiteral(" in ") + names.join(QLatin1Char(' ')));
	}
	ok &= expect(plan.canPublish() && hasProblem(plan, QStringLiteral("unresolved"), false), "a sound lump nothing provides is an advisory", problemKinds(plan));
	ok &= expect(requiredBy(plan, QStringLiteral("FIMPA1"), QStringLiteral("monsters.wad:DECORATE")) && requiredBy(plan, QStringLiteral("FBALA0"), QStringLiteral("monsters.wad:IMPBALL"))
			&& requiredBy(plan, QStringLiteral("DSFISIT"), QStringLiteral("monsters.wad:SNDINFO")) && requiredBy(plan, QStringLiteral("DSFISIT"), QStringLiteral("monsters.wad:DECORATE"))
			&& requiredBy(plan, QStringLiteral("D_FIRE"), QStringLiteral("monsters.wad:MAPINFO")),
		"lumps name the DECORATE, SNDINFO and MAPINFO that use them");
	const ReleaseScriptReference* sprite = scriptRow(plan, QStringLiteral("FIMP"));
	ok &= expect(sprite && sprite->status == QStringLiteral("included") && sprite->paths.contains(QStringLiteral("FIMPA1")) && sprite->paths.contains(QStringLiteral("FIMPC1"))
			&& !sprite->paths.contains(QStringLiteral("FBALA0")), "a sprite resolves to its frames", sprite ? sprite->paths.join(QLatin1Char(' ')) : QString());
	ok &= expect(rowStatus(plan, QStringLiteral("imp/death")) == QStringLiteral("engine") && rowStatus(plan, QStringLiteral("FENCE")) == QStringLiteral("stock")
			&& rowStatus(plan, QStringLiteral("fireimp/sight")) == QStringLiteral("included") && !scriptRow(plan, QStringLiteral("UNUS")),
		"engine sounds, stock textures and unused classes are told apart");
	plan = planRelease(request(root, ReleaseScope::Maps, {QStringLiteral("maps/plain.wad")}, stock));
	ok &= expect(!entryPaths(plan).contains(QStringLiteral("FIMPA1")) && !entryPaths(plan).contains(QStringLiteral("DECORATE")) && plan.canPublish(),
		"a map with only stock things leaves the monster WAD out", entryPaths(plan).join(QLatin1Char(' ')) + QStringLiteral(" / ") + problemKinds(plan));
	plan = planRelease(request(root, ReleaseScope::Project, {}, stock));
	ok &= expect(rowStatus(plan, QStringLiteral("UNUS")) == QStringLiteral("included") && hasProblem(plan, QStringLiteral("unresolved"), false) && plan.canPublish(),
		"a whole project checks every class", problemKinds(plan));
	return ok;
}

// The window and the CLI pick a project's installation the same way.
bool runInstallationChoice()
{
	bool ok = true;
	const QVector<GameInstallationProfile> saved {
		fixtures::profile(QStringLiteral("doom-1"), QStringLiteral("doom"), QStringLiteral("C:/games/doom")),
		fixtures::profile(QStringLiteral("q3-1"), QStringLiteral("quake3"), QStringLiteral("C:/games/q3")),
		fixtures::profile(QStringLiteral("q3-2"), QStringLiteral("quake3"), QStringLiteral("D:/q3")),
	};
	ProjectManifest manifest = defaultProjectManifest(QStringLiteral("C:/projects/mymod"));
	GameInstallationProfile chosen;
	ok &= expect(releaseInstallationFor(manifest, saved, QStringLiteral("doom-1"), &chosen) && chosen.id == QStringLiteral("doom-1"),
		"a project that names no game uses the installation in use");
	ok &= expect(!releaseInstallationFor(manifest, saved, QString(), &chosen), "with nothing in use and no game, no installation is guessed");
	manifest.gameKey = QStringLiteral("quake3");
	ok &= expect(releaseInstallationFor(manifest, saved, QStringLiteral("doom-1"), &chosen) && chosen.id == QStringLiteral("q3-1"),
		"a Quake III project is checked against a Quake III installation, not the Doom one in use", chosen.id);
	ok &= expect(releaseInstallationFor(manifest, saved, QStringLiteral("q3-2"), &chosen) && chosen.id == QStringLiteral("q3-2"),
		"the installation in use wins when it plays the project's game", chosen.id);
	manifest.selectedInstallationId = QStringLiteral("doom-1");
	ok &= expect(releaseInstallationFor(manifest, saved, QStringLiteral("q3-2"), &chosen) && chosen.id == QStringLiteral("doom-1"),
		"an installation the project links always wins", chosen.id);
	manifest.selectedInstallationId = QStringLiteral("gone");
	ok &= expect(releaseInstallationFor(manifest, saved, QString(), &chosen) && chosen.id == QStringLiteral("q3-1"),
		"a linked installation that no longer exists falls back to the project's game", chosen.id);
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	const QString base = temp.path();
	bool ok = true;
	ok &= runQuake3Maps(base);
	ok &= runQuake3Project(base);
	ok &= runQuake3TexturesAndModels(base);
	ok &= runQuake(base);
	ok &= runDoom(base);
	ok &= runChangelogAndVersions();
	ok &= runRecordsAndNotes(base);
	ok &= runPublish(base);
	ok &= runPublishDoomAndZip(base);
	ok &= runInstallationChoice();
	ok &= runScriptScanner(base);
	ok &= runQuakeScripts(base);
	ok &= runQuake3Menus(base);
	ok &= runDoomScripts(base);
	if (!ok) {
		return EXIT_FAILURE;
	}
	std::cout << "release smoke passed\n";
	return EXIT_SUCCESS;
}
