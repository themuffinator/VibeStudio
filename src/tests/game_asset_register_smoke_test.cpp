// The game asset register: discovery of stock packages per game, building the
// index from PK3/PAK/WAD sources, matching project files against it, Doom and
// Quake III names, persistence, staleness, requirements and merging.

#include "core/deflate.h"
#include "core/game_asset_register.h"
#include "core/release_plan.h"
#include "core/studio_settings.h"
#include "tests/release_test_fixtures.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace fixtures = vibestudio::tests::release;

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

QStringList sourceIds(const QVector<GameAssetRegisterSource>& sources)
{
	QStringList ids;
	for (const GameAssetRegisterSource& source : sources) {
		ids << source.id + QLatin1Char('=') + source.role;
	}
	return ids;
}

bool runQuake3()
{
	bool ok = true;
	QTemporaryDir temp;
	const QString root = QDir(temp.path()).filePath(QStringLiteral("q3"));
	QString error;
	ok &= expect(fixtures::makeQuake3Installation(root, &error), "the fake Quake III installation is written", error);
	const GameInstallationProfile installation = fixtures::profile(QStringLiteral("q3-test"), QStringLiteral("quake3"), root);

	const QVector<GameAssetRegisterSource> sources = discoverGameStockSources(installation);
	ok &= expect(sourceIds(sources) == QStringList {QStringLiteral("baseq3/pak0.pk3=base"), QStringLiteral("baseq3/pak1.pk3=base"), QStringLiteral("missionpack/pak0.pk3=expansion")},
		"discovery finds the stock paks in engine order with their roles", sourceIds(sources).join(QLatin1Char(' ')));
	// A user's own pk3 in baseq3 is not stock.
	ok &= expect(fixtures::writePackage(QDir(root).filePath(QStringLiteral("baseq3/zz-somebodys-map.pk3")), PackageArchiveFormat::Pk3,
		{{QStringLiteral("maps/other.bsp"), QByteArray("x")}}), "a user package is written");
	ok &= expect(discoverGameStockSources(installation).size() == 3, "a user's own package in baseq3 is never treated as stock");

	GameAssetRegisterBuildRequest request;
	request.installation = installation;
	int progressCalls = 0;
	request.control.progress = [&progressCalls](const QString&, qint64, qint64) { ++progressCalls; };
	const GameAssetRegisterBuildResult built = buildGameAssetRegister(request);
	ok &= expect(built.succeeded, "the Quake III register builds", built.error);
	const GameAssetRegister& reg = built.registerData;
	ok &= expect(reg.files.size() == 9, "every stock file is indexed", QString::number(reg.files.size()));
	ok &= expect(progressCalls > 0, "building reports progress");
	const GameAssetRegisterFile* wall = reg.file(QStringLiteral("Textures/Base_Wall/WALL1.tga"));
	ok &= expect(wall && wall->crc32 == crc32Bytes(fixtures::stockWall()) && wall->sizeBytes == quint64(fixtures::stockWall().size()),
		"ZIP entries keep their size and CRC, and lookup ignores case");
	ok &= expect(reg.hasShader(QStringLiteral("textures/base_wall/glass")) && reg.hasShader(QStringLiteral("TEXTURES/COMMON/CAULK")),
		"shader declarations are indexed without regard to case");
	ok &= expect(reg.shaderScript(QStringLiteral("textures/skies/stocksky")) && reg.shaderScript(QStringLiteral("textures/skies/stocksky"))->path == QStringLiteral("scripts/base_wall.shader"),
		"a shader name leads to its declaring script");
	ok &= expect(reg.match(QStringLiteral("textures/base_wall/wall1.tga"), quint64(fixtures::stockWall().size()), crc32Bytes(fixtures::stockWall())) == GameAssetMatch::Identical,
		"an identical copy matches");
	ok &= expect(reg.match(QStringLiteral("textures/base_wall/crate.tga"), 10, 1) == GameAssetMatch::Different, "a different file at a stock path is a replacement");
	ok &= expect(reg.match(QStringLiteral("textures/mymod/new.tga"), 10, 1) == GameAssetMatch::None, "a new path is not stock");

	// Base sources only: the mission pack counts when a project says so.
	const GameAssetRegister base = reg.filtered(reg.defaultSourceIds());
	ok &= expect(base.sources.size() == 2 && !base.file(QStringLiteral("textures/team/flag.tga")) && base.file(QStringLiteral("textures/base_floor/floor1.tga")),
		"filtering to the base sources leaves the expansion out");
	ok &= expect(base.shaderScript(QStringLiteral("textures/base_wall/glass")) && base.shaderScript(QStringLiteral("textures/base_wall/glass"))->source == 0,
		"filtering keeps shader declarations pointing at their script");

	// Persistence and status.
	QTemporaryDir store;
	StudioSettings::setOverrideFilePath(QDir(store.path()).filePath(QStringLiteral("settings.ini")));
	const QString path = gameAssetRegisterPath(installation.id);
	ok &= expect(path.startsWith(QDir(store.path()).absolutePath()), "the register lives beside an override settings file", path);
	GameAssetRegisterStatus status = gameAssetRegisterStatus(installation);
	ok &= expect(!status.exists && !status.usable(), "an unbuilt register is reported missing");
	ok &= expect(saveGameAssetRegister(reg, path, &error), "the register saves", error);
	GameAssetRegister loaded;
	status = gameAssetRegisterStatus(installation, &loaded);
	ok &= expect(status.usable() && loaded.files.size() == reg.files.size() && loaded.shaders.size() == reg.shaders.size(), "the saved register loads fresh");
	ok &= expect(loaded.file(QStringLiteral("textures/base_wall/wall1.tga")) && loaded.file(QStringLiteral("textures/base_wall/wall1.tga"))->crc32 == wall->crc32,
		"CRCs survive the round trip");
	status = gameAssetRegisterStatus(installation);
	ok &= expect(status.usable() && status.fileCount == int(reg.files.size()), "a status-only check reuses the cached header", QString::number(status.fileCount));

	// A stock package changing, appearing or vanishing makes it stale.
	QThread::msleep(1100);
	ok &= expect(fixtures::writePackage(QDir(root).filePath(QStringLiteral("baseq3/pak1.pk3")), PackageArchiveFormat::Pk3,
		{{QStringLiteral("textures/base_floor/floor1.tga"), QByteArray("patched floor")}}), "pak1 is rewritten");
	QFile::remove(QDir(root).filePath(QStringLiteral("baseq3/pak1.pk3.bak")));
	status = gameAssetRegisterStatus(installation);
	ok &= expect(status.loaded && !status.fresh && !status.staleReasons.isEmpty() && status.staleReasons.first().contains(QStringLiteral("pak1.pk3")),
		"a changed stock package makes the register stale", status.staleReasons.join(QLatin1Char(' ')));
	ok &= expect(fixtures::writePackage(QDir(root).filePath(QStringLiteral("baseq3/pak2.pk3")), PackageArchiveFormat::Pk3,
		{{QStringLiteral("textures/x.tga"), QByteArray("x")}}), "pak2 is added");
	status = gameAssetRegisterFreshness(loaded, installation);
	ok &= expect(!status.fresh && status.staleReasons.join(QLatin1Char(' ')).contains(QStringLiteral("pak2.pk3")), "a new stock package makes the register stale");
	StudioSettings::setOverrideFilePath(QString());

	// Malformed registers are refused, not half loaded.
	ok &= expect(!parseGameAssetRegister("{\"format\":\"something-else\"}", &loaded, &error), "a foreign JSON file is refused");
	ok &= expect(!parseGameAssetRegister("{\"format\":\"vibestudio-asset-register\",\"version\":99}", &loaded, &error) && error.contains(QStringLiteral("99")),
		"a future version is refused with its number");
	ok &= expect(!parseGameAssetRegister("{\"format\":\"vibestudio-asset-register\",\"version\":1,\"sources\":[{\"id\":\"a\",\"path\":\"a\"}],\"files\":[[\"x\",1,\"nothex!!\",0]]}", &loaded, &error),
		"a malformed CRC is refused");
	ok &= expect(!parseGameAssetRegister("{\"format\":\"vibestudio-asset-register\",\"version\":1,\"sources\":[],\"files\":[[\"x\",1,\"00000000\",3]]}", &loaded, &error),
		"a file row naming a missing source is refused");

	// Explicit packages override discovery; a missing one fails the build.
	GameAssetRegisterBuildRequest explicitRequest;
	explicitRequest.installation = installation;
	explicitRequest.packagePaths = {QStringLiteral("baseq3/pak0.pk3")};
	const GameAssetRegisterBuildResult explicitBuild = buildGameAssetRegister(explicitRequest);
	ok &= expect(explicitBuild.succeeded && explicitBuild.registerData.sources.size() == 1, "explicit packages are indexed alone", explicitBuild.error);
	explicitRequest.packagePaths = {QStringLiteral("baseq3/missing.pk3")};
	ok &= expect(!buildGameAssetRegister(explicitRequest).succeeded, "a missing explicit package fails the build");

	// Cancellation fails the whole build.
	GameAssetRegisterBuildRequest cancelled;
	cancelled.installation = installation;
	cancelled.control.isCancelled = []() { return true; };
	const GameAssetRegisterBuildResult cancelledBuild = buildGameAssetRegister(cancelled);
	ok &= expect(!cancelledBuild.succeeded && cancelledBuild.cancelled, "a cancelled build reports cancellation");

	// No stock packages at all is a clear failure.
	GameInstallationProfile empty = fixtures::profile(QStringLiteral("empty"), QStringLiteral("quake3"), temp.path());
	const GameAssetRegisterBuildResult none = buildGameAssetRegister({empty, {}, {}});
	ok &= expect(!none.succeeded && none.error.contains(QStringLiteral("No stock packages")), "an installation without stock packages says so", none.error);
	return ok;
}

bool runQuake()
{
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.path();
	// Upper case on disk: discovery ignores case.
	ok &= expect(fixtures::writePackage(QDir(root).filePath(QStringLiteral("ID1/PAK0.PAK")), PackageArchiveFormat::Pak,
		{{QStringLiteral("progs/player.mdl"), QByteArray("IDPO stand-in")}, {QStringLiteral("sound/misc/null.wav"), QByteArray("RIFF")},
			{QStringLiteral("gfx.wad"), QByteArray("WAD2")}}), "the fake Quake pak is written");
	const GameInstallationProfile installation = fixtures::profile(QStringLiteral("quake-test"), QStringLiteral("quake"), root);
	const QVector<GameAssetRegisterSource> sources = discoverGameStockSources(installation);
	ok &= expect(sources.size() == 1 && sources.first().id == QStringLiteral("id1/pak0.pak"), "a stock PAK is found without regard to case",
		sourceIds(sources).join(QLatin1Char(' ')));
	const GameAssetRegisterBuildResult built = buildGameAssetRegister({installation, {}, {}});
	ok &= expect(built.succeeded && built.registerData.files.size() == 3, "the PAK is indexed", built.error);
	const GameAssetRegisterFile* player = built.registerData.file(QStringLiteral("progs/player.mdl"));
	ok &= expect(player && player->crc32 == crc32Bytes(QByteArray("IDPO stand-in")), "PAK entries are streamed for their CRC");
	ok &= expect(built.bytesRead > 0, "streamed bytes are counted");
	return ok;
}

bool runDoom()
{
	bool ok = true;
	QTemporaryDir temp;
	const QString root = temp.path();
	ok &= expect(fixtures::writeFile(QDir(root).filePath(QStringLiteral("doom2.wad")), fixtures::doom2Iwad()), "the fake Doom II IWAD is written");
	QByteArray tnt = fixtures::doom2Iwad();
	ok &= expect(fixtures::writeFile(QDir(root).filePath(QStringLiteral("tnt.wad")), tnt), "the fake TNT IWAD is written");
	const GameInstallationProfile installation = fixtures::profile(QStringLiteral("doom-test"), QStringLiteral("doom"), root);
	const QVector<GameAssetRegisterSource> sources = discoverGameStockSources(installation);
	ok &= expect(sourceIds(sources) == QStringList {QStringLiteral("doom2.wad=base"), QStringLiteral("tnt.wad=alternative")},
		"only the first IWAD is the base; the others are alternatives", sourceIds(sources).join(QLatin1Char(' ')));
	const GameAssetRegisterBuildResult built = buildGameAssetRegister({installation, {}, {}});
	ok &= expect(built.succeeded, "the IWADs are indexed", built.error);
	const GameAssetRegister base = built.registerData.filtered(built.registerData.defaultSourceIds());
	ok &= expect(base.hasDoomName(QStringLiteral("texture"), QStringLiteral("stone")) && base.hasDoomName(QStringLiteral("texture"), QStringLiteral("FENCE")),
		"TEXTURE1 names are indexed");
	ok &= expect(base.hasDoomName(QStringLiteral("patch"), QStringLiteral("DECOR")), "PNAMES and patch names are indexed");
	ok &= expect(base.hasDoomName(QStringLiteral("flat"), QStringLiteral("CEIL")) && base.hasDoomName(QStringLiteral("flat"), QStringLiteral("ALTFLAT")),
		"flats between F_START and F_END are indexed");
	ok &= expect(base.hasDoomName(QStringLiteral("music"), QStringLiteral("D_RUNNIN")), "MUS music lumps are indexed");
	QStringList lumpNames;
	PackageArchive iwad;
	if (iwad.load(QDir(root).filePath(QStringLiteral("doom2.wad")))) {
		for (const PackageEntry& entry : iwad.entries()) {
			lumpNames << entry.virtualPath;
		}
	}
	ok &= expect(base.hasDoomName(QStringLiteral("map"), QStringLiteral("MAP01")), "map markers are indexed", lumpNames.join(QLatin1Char(' ')));
	ok &= expect(base.doomNameSource(QStringLiteral("texture"), QStringLiteral("STONE")) == 0 && base.sourceLabel(0).contains(QStringLiteral("Doom II")),
		"a Doom name leads to its IWAD");
	ok &= expect(base.file(QStringLiteral("PLAYPAL")) != nullptr, "lumps are indexed by name too");
	return ok;
}

bool runRequirementsAndMerge()
{
	bool ok = true;
	QTemporaryDir temp;
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	const QString mod = QDir(temp.path()).filePath(QStringLiteral("game/ad"));
	ok &= expect(fixtures::writePackage(QDir(mod).filePath(QStringLiteral("pak0.pak")), PackageArchiveFormat::Pak,
		{{QStringLiteral("progs/ad_monster.mdl"), QByteArray("IDPO ad")}}), "the requirement's pak is written");
	ok &= expect(fixtures::writeFile(QDir(mod).filePath(QStringLiteral("maps/ad_start.bsp")), QByteArray("BSP2")), "a loose requirement file is written");
	ProjectReleaseRequirement requirement {QStringLiteral("Arcane Dimensions"), QStringLiteral("ad"), QStringLiteral("https://example.com/ad")};
	QString error;
	auto folder = requirementAssetRegister(requirement, QDir(temp.path()).filePath(QStringLiteral("game")), &error);
	ok &= expect(folder && folder->file(QStringLiteral("progs/ad_monster.mdl")) && folder->file(QStringLiteral("maps/ad_start.bsp")),
		"a folder requirement counts its loose files and the packages inside it", error);
	ok &= expect(folder && folder->sources.size() == 2 && folder->sources.first().role == QStringLiteral("requirement")
		&& folder->sourceLabel(0).startsWith(QStringLiteral("Arcane Dimensions")), "requirement sources carry the requirement's name");
	ProjectReleaseRequirement package {QStringLiteral("AD pak"), QDir(mod).filePath(QStringLiteral("pak0.pak")), {}};
	auto packed = requirementAssetRegister(package, QString(), &error);
	ok &= expect(packed && packed->files.size() == 1, "a package requirement is indexed", error);
	ok &= expect(!QDir(gameAssetRegisterDirectory()).entryList({QStringLiteral("requirement-*.json")}, QDir::Files).isEmpty(),
		"a package requirement's index is cached");
	auto again = requirementAssetRegister(package, QString(), &error);
	ok &= expect(again && again->files.size() == 1, "the cached requirement index loads");
	ProjectReleaseRequirement missing {QStringLiteral("Gone"), QStringLiteral("nowhere"), {}};
	ok &= expect(!requirementAssetRegister(missing, temp.path(), &error) && error.contains(QStringLiteral("Gone")), "a missing requirement is named", error);

	auto merged = mergeGameAssetRegisters({folder, packed});
	ok &= expect(merged && merged->sources.size() == 3 && merged->file(QStringLiteral("maps/ad_start.bsp")) && merged->file(QStringLiteral("progs/ad_monster.mdl"))->source == 2,
		"merging appends sources, and later registers shadow earlier paths");
	StudioSettings::setOverrideFilePath(QString());

	quint32 crc = 0;
	ok &= expect(fileCrc32(QDir(mod).filePath(QStringLiteral("maps/ad_start.bsp")), &crc, &error) && crc == crc32Bytes(QByteArray("BSP2")),
		"file CRCs match the in-memory CRC");
	return ok;
}

bool runCandidates()
{
	bool ok = true;
	ok &= expect(gameStockPackageCandidates(QStringLiteral("quake3")).size() == 13, "Quake III lists pak0-8 and Team Arena's pak0-3");
	ok &= expect(gameStockPackageCandidates(QStringLiteral("quake2")).first().relativePath == QStringLiteral("baseq2/pak0.pak"), "Quake II starts with baseq2/pak0.pak");
	ok &= expect(gameStockPackageCandidates(QStringLiteral("custom")).isEmpty(), "a custom game has no built-in stock packages");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	ok &= runCandidates();
	ok &= runQuake3();
	ok &= runQuake();
	ok &= runDoom();
	ok &= runRequirementsAndMerge();
	if (!ok) {
		return EXIT_FAILURE;
	}
	std::cout << "game asset register smoke passed\n";
	return EXIT_SUCCESS;
}
