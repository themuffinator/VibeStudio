// Releases end to end in core: planning what ships for each scope and game
// against a generated game asset register, the changelog and release notes,
// release records and their diffs, and publishing packages, readmes, notes
// and distribution archives through the atomic publisher.

#include "core/deflate.h"
#include "core/game_asset_register.h"
#include "core/level_document.h"
#include "core/release_notes.h"
#include "core/release_plan.h"
#include "core/release_publish.h"
#include "core/studio_settings.h"
#include "tests/release_test_fixtures.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
	if (!ok) {
		return EXIT_FAILURE;
	}
	std::cout << "release smoke passed\n";
	return EXIT_SUCCESS;
}
