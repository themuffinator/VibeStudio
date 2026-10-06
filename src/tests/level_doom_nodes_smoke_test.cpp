#include "core/build_pipeline.h"
#include "core/compiler_artifact_validation.h"
#include "core/level_doom_nodes.h"
#include "core/level_doom_selection.h"
#include "tests/level_doom_nodes_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <iostream>

using namespace vibestudio;
namespace d = vibestudio::tests::doom;
namespace f = vibestudio::tests::doomNodes;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& detail = {}) {
	if (!value) {
		ok = false;
		std::cerr << message << ": " << detail.toStdString() << '\n';
	}
	return value;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	const auto args = app.arguments();
	if (args.contains("--record-launch")) {
		return d::write(args.value(args.indexOf("--record-launch") + 1), "started") ? 0 : 1;
	}
	QTemporaryDir temp;
	QString error;
	for (bool hexen : {false, true}) {
		const auto bytes = f::fixture(hexen);
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({"nodes.wad", "MAP01", {}}, bytes, &source, &error), "load", error)) {
			return 1;
		}
		auto report = inspectLevelDoomNodes(source);
		expect(report.state == LevelDoomNodeState::Present && report.segs == 12 && report.subsectors == 3 && report.nodes == 2,
			   "classic tree and native references", report.message);
		auto edit = source;
		expect(setLevelMapSelection(&edit, {{LevelMapSelectionKind::DoomLinedef, 0}}, &error) &&
				   selectConnectedLevelMapDoomGeometry(&edit, &error) && flipLevelMapSelection(&edit, 0, &error),
			   "edit", error);
		expect(inspectLevelDoomNodes(edit).state == LevelDoomNodeState::Stale, "edited node state");
		const auto saved = serializeLevelMap(edit);
		expect(saved.errors.isEmpty() && saved.staleLumps.contains("NODES"), "save reports rebuild", saved.errors.join(';'));
		auto lumps = d::lumps(saved.bytes);
		bool selected = true;
		const auto originalLumps = d::lumps(bytes);
		for (int i = 0; i < lumps.size(); ++i) {
			if (lumps[i].name == "MAP02") {
				selected = false;
			}
			if (selected && isDoomNodeProduct(QString::fromLatin1(lumps[i].name))) {
				expect(lumps[i].bytes.isEmpty(), "obsolete runtime bytes cleared");
			}
			if (!selected) {
				expect(lumps[i].bytes == originalLumps[i].bytes, "second map bytes preserved");
			}
		}
		LevelMapDocument reopened;
		expect(loadLevelMapBytes({"saved.wad", "MAP01", {}}, saved.bytes, &reopened, &error) && !reopened.doomGeometryChanged &&
				   inspectLevelDoomNodes(reopened).state == LevelDoomNodeState::Missing,
			   "reopened node readiness persists", error);
		expect(levelMapValidationLines(reopened).join('\n').contains("doom-nodes-missing"), "health explains rebuild after reopen");
		expect(undoLevelMapEdit(&edit, &error) && serializeLevelMap(edit).bytes == bytes &&
				   inspectLevelDoomNodes(edit).state == LevelDoomNodeState::Present,
			   "undo restores original caches", error);
		for (const auto& magic : {"XNOD", "ZNOD", "XGLN", "ZGLN", "XGL2", "ZGL2", "XGL3", "ZGL3"}) {
			auto encoded = source;
			encoded.doomNodeReport.reset();
			encoded.doomLumps["SEGS"].clear();
			encoded.doomLumps["SSECTORS"].clear();
			encoded.doomLumps["NODES"].clear();
			const auto name = QByteArray(magic).mid(1, 2) == "GL" ? "SSECTORS" : "NODES";
			encoded.doomLumps[name] = f::extended(magic);
			report = inspectLevelDoomNodes(encoded);
			expect(report.state == LevelDoomNodeState::Present && report.format == magic && report.nodes == 2, magic, report.message);
			encoded.doomLumps[name].chop(1);
			expect(inspectLevelDoomNodes(encoded).needsBuild(), "truncated extended nodes refuse");
		}
		for (int defect = 0; defect < 13; ++defect) {
			auto broken = source;
			broken.doomNodeReport.reset();
			if (defect == 0) {
				d::put16(broken.doomLumps["SEGS"], 6, 99);
			}
			if (defect == 1) {
				d::put16(broken.doomLumps["SEGS"], 8, 1);
			}
			if (defect == 2) {
				d::put16(broken.doomLumps["SEGS"], 0, 99);
			}
			if (defect == 3) {
				d::put16(broken.doomLumps["SSECTORS"], 6, 3);
			}
			if (defect == 4) {
				d::put16(broken.doomLumps["NODES"], 52, 1);
			}
			if (defect == 5) {
				d::put16(broken.doomLumps["NODES"], 54, 0x8001);
			}
			if (defect == 6) {
				broken.doomLumps["NODES"].append('\0');
			}
			if (defect == 7) {
				d::put16(broken.doomLumps["BLOCKMAP"], 8, 1);
			}
			if (defect == 8) {
				d::put16(broken.doomLumps["BLOCKMAP"], 34, 77);
			}
			if (defect == 9) {
				broken.doomLumps["REJECT"].chop(1);
			}
			if (defect == 10) {
				broken.doomLumps["NODES"] = "XNOD" + QByteArray::fromHex("0a000000ffffffff");
			}
			if (defect == 11) {
				broken.doomVertices.clear();
			}
			if (defect == 12) {
				broken.doomLumps["NODES"] = "XGL";
			}
			expect(inspectLevelDoomNodes(broken).state == LevelDoomNodeState::Invalid, "malformed/cyclic/overlapping node records refuse");
		}
		auto one = source;
		one.doomNodeReport.reset();
		one.doomLumps["NODES"].clear();
		one.doomLumps["SEGS"] = f::segs().left(48);
		one.doomLumps["SSECTORS"] = f::subs().left(4);
		expect(inspectLevelDoomNodes(one).state == LevelDoomNodeState::Present, "single subsector needs no NODES records");
		auto compressed = source;
		compressed.doomNodeReport.reset();
		compressed.doomLumps["NODES"] = f::extended("ZNOD");
		int calls = 0;
		expect(inspectLevelDoomNodes(compressed, [&] { return ++calls > 3; }).state == LevelDoomNodeState::Cancelled,
			   "cancel compressed reader");
		QByteArray longRaw(1024 * 1024, '\0'), checksum(4, '\0');
		qToBigEndian(adler32Bytes(longRaw), checksum.data());
		const auto zlib = QByteArray::fromHex("789c") + deflateRaw(longRaw) + checksum;
		calls = 0;
		expect(!inflateZlib(zlib, longRaw.size(), [&] { return ++calls >= 5; }).ok && calls >= 5,
			   "cancellation interrupts decompression before checksum");
		expect(!inflateZlib(zlib, 1024).ok, "zlib output cannot exceed node validation budget");
		const auto recovery = writeLevelMapRecovery(reopened, temp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
		LevelMapDocument restored;
		expect(!recovery.isEmpty() && restoreLevelMapRecovery(recovery, &restored, &error) && inspectLevelDoomNodes(restored).needsBuild(),
			   "recovery preserves saved rebuild requirement", error);
		const auto path = temp.filePath("multi.wad");
		d::write(path, bytes);
		auto wad = inspectLevelDoomWadNodes(path);
		expect(wad.errors.isEmpty() && wad.maps.size() == 2 && wad.sourceHash.size() == 32, "all-map snapshot validation",
			   wad.errors.join(';'));
		d::write(path, saved.bytes);
		expect(inspectLevelDoomWadNodes(path, {"MAP02"}).errors.isEmpty() && !inspectLevelDoomWadNodes(path).errors.isEmpty(),
			   "targeted inspection ignores untouched groups");
		expect(!inspectLevelDoomWadNodes(path, {"MAP03"}).errors.isEmpty(), "missing map refuses");
		expect(inspectLevelDoomWadNodes(path, {}, [] { return true; }).cancelled, "cancel WAD snapshot");
		CompilerCommandManifest manifest;
		manifest.toolId = "zdbsp";
		manifest.profileId = "zdbsp-nodes";
		manifest.expectedOutputPaths = {path};
		manifest.arguments = {"-m", "MAP02"};
		expect(!validateCompilerArtifacts(manifest).hasErrors(), "targeted compiler validates built map only");
		manifest.arguments = {"--map=MAP01"};
		expect(validateCompilerArtifacts(manifest).hasErrors(), "compiler zero exit cannot hide absent nodes");
		manifest.arguments = {"-mMAP02"};
		expect(!validateCompilerArtifacts(manifest).hasErrors(), "attached compiler map selector");
		manifest.toolId = "zokumbsp";
		manifest.arguments = {path, "MAP02"};
		expect(!validateCompilerArtifacts(manifest).hasErrors(), "ZokumBSP map selection");
		manifest.arguments = {path, "MAP01+MAP02"};
		expect(validateCompilerArtifacts(manifest).hasErrors(), "ZokumBSP combined selection");
		expect(validateCompilerArtifacts(manifest, [] { return true; }).cancelled, "compiler validation cancellation");
		GameInstallationProfile installation;
		installation.gameKey = "doom";
		installation.engineFamily = GameEngineFamily::IdTech1;
		installation.rootPath = temp.path();
		installation.executablePath = app.applicationFilePath();
		GameLaunchRequest launch;
		launch.mapName = "MAP01";
		launch.bspPath = path;
		const auto marker = temp.filePath(hexen ? "hexen-started" : "doom-started");
		launch.extraArguments = {"--record-launch", marker};
		expect(!buildGameLaunchPlan(launch, installation).runnable, "launch refuses saved unbuilt geometry");
		d::write(path, bytes);
		const auto deferred = buildGameLaunchPlan(launch, installation, {}, true);
		qint64 pid = 0;
		expect(deferred.artifactValidationPending && !deferred.runnable && !startGameLaunch(deferred, &pid, &error),
			   "preview cannot bypass validation");
		const auto ready = buildGameLaunchPlan(launch, installation);
		expect(ready.runnable && ready.validatedArtifactHash.size() == 32 &&
				   ready.nodeBuild.value("MAP01").toObject().value("state") == "present",
			   "launch shares node validation", ready.errors.join(';'));
		auto shorthand = launch;
		shorthand.mapName = "1";
		shorthand.bspPath = QDir::current().relativeFilePath(path);
		const auto shortPlan = buildGameLaunchPlan(shorthand, installation);
		expect(shortPlan.runnable && shortPlan.arguments.value(shortPlan.arguments.indexOf("-file") + 1) == path &&
				   shortPlan.nodeBuild.contains("MAP01"),
			   "numeric warp and relative artifact use the validated WAD");
		d::write(path, saved.bytes);
		expect(!startGameLaunch(ready, &pid, &error) && pid == 0 && !QFileInfo::exists(marker),
			   "changed reviewed WAD blocks recorder launch", error);
		d::write(path, bytes);
		expect(!startGameLaunch(ready, &pid, &error, [] { return true; }) && pid == 0 && !QFileInfo::exists(marker),
			   "cancel before starting recorder");
		expect(startGameLaunch(ready, &pid, &error) && pid > 0, "validated launch starts harmless recorder", error);
		QElapsedTimer elapsed;
		elapsed.start();
		while (!QFileInfo::exists(marker) && elapsed.elapsed() < 10000) {
			QThread::msleep(10);
		}
		expect(QFileInfo::exists(marker), "recorder ran; no game launched");
		if (argc > 1) {
			d::write(QString::fromLocal8Bit(argv[1]) + (hexen ? "/hexen.wad" : "/doom.wad"), saved.bytes);
		}
	}
	// Separate GL groups are removed only for the edited owner. Duplicates or
	// truncated-name collisions refuse atomically instead of guessing ownership.
	const auto raw = d::lumps(f::fixture());
	QVector<LevelMapWadSourceLump> archive;
	for (const auto& lump : raw) {
		archive << LevelMapWadSourceLump{QString::fromLatin1(lump.name), lump.bytes};
	}
	archive << LevelMapWadSourceLump{"GL_MAP01", {}} << LevelMapWadSourceLump{"GL_VERT", "old"} << LevelMapWadSourceLump{"GL_SEGS", "old"}
			<< LevelMapWadSourceLump{"USERDATA", "preserve"} << LevelMapWadSourceLump{"GL_MAP02", {}}
			<< LevelMapWadSourceLump{"GL_VERT", "keep"};
	auto candidate = archive;
	QStringList invalidated;
	expect(invalidateLevelDoomNodeProducts(&candidate, 0, &invalidated, &error) && candidate.size() == archive.size() - 3 &&
			   candidate.last().bytes == "keep" && invalidated.contains("GL_VERT"),
		   "selected GL companion removed, other group preserved", error);
	archive << LevelMapWadSourceLump{"GL_MAP01", {}} << LevelMapWadSourceLump{"GL_VERT", "duplicate"};
	candidate = archive;
	expect(!invalidateLevelDoomNodeProducts(&candidate, 0, &invalidated, &error) && candidate.size() == archive.size() &&
			   candidate[5].bytes == archive[5].bytes,
		   "ambiguous GL cache refuses without clearing native nodes", error);
	const auto duplicatePath = temp.filePath("duplicates.wad");
	auto duplicates = raw;
	for (auto& lump : duplicates) {
		if (lump.name == "MAP02") {
			lump.name = "MAP01";
		}
	}
	d::write(duplicatePath, d::wad(duplicates));
	expect(!inspectLevelDoomWadNodes(duplicatePath, {"MAP01"}).errors.isEmpty(), "duplicate map label refuses");
	return ok ? 0 : 1;
}
