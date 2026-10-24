#include "core/level_document.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/package_archive.h"
#include "core/package_staging.h"
#include "core/package_wad_groups.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool pass, const char* label, const QString& error = {}) {
	if (!pass) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return pass;
}
QByteArray read(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || argc != 2) {
		return 1;
	}
	bool ok = true;
	QString error;
	const auto cli = QString::fromLocal8Bit(argv[1]);
	const auto run = [&](QStringList arguments, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(cli, QStringList{QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
									   temp.filePath(QStringLiteral("cli.ini")), QStringLiteral("editor"), QStringLiteral("scene")} +
							   arguments);
		const bool ended = process.waitForStarted() && process.waitForFinished(30000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		ok &= expect(ended && process.exitStatus() == QProcess::NormalExit &&
						 (expected == 0 ? process.exitCode() == 0 : process.exitCode() != 0),
					 "CLI exit", QString::fromUtf8(out + err));
		if (!ended) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	LevelMapDocument fixture;
	LevelMapCreateRequest request;
	ok &= expect(createLevelMap(request, &fixture, &error), "fixture", error);
	const auto input = temp.filePath(QStringLiteral("input.map")), output = temp.filePath(QStringLiteral("scene.map"));
	QFile file(input);
	ok &= file.open(QIODevice::WriteOnly);
	file.write(serializeLevelMap(fixture).bytes);
	file.close();
	const auto initial = read(input);
	const QStringList creation{"create", input, "--kind", "layer", "--name", "Architecture", "--output", output};
	auto result = run(creation + QStringList{"--dry-run"});
	ok &= expect(result.value(QStringLiteral("dryRun")).toBool() && !QFile::exists(output) && read(input) == initial,
				 "dry-run leaves files untouched");
	result = run(creation);
	const auto id = result.value(QStringLiteral("createdId")).toString();
	ok &= expect(!id.isEmpty() && QFile::exists(output), "creation returns durable node ID");
	run({"assign", output, "--id", id, "--objects", "brush:0", "--output", output, "--overwrite"});
	run({"visibility", output, "--id", id, "--visible", "false", "--output", output, "--overwrite"});
	run({"lock", output, "--id", id, "--locked", "true", "--output", output, "--overwrite"});
	result = run({"list", output});
	const auto node = result.value(QStringLiteral("scene")).toObject().value(QStringLiteral("nodes")).toArray().first().toObject();
	ok &= expect(node.value(QStringLiteral("objects")).toArray().first().toString() == QStringLiteral("brush:0") &&
					 !node.value(QStringLiteral("visible")).toBool() && node.value(QStringLiteral("locked")).toBool(),
				 "CLI reads shared membership, visibility and locks");
	const auto stable = read(output);
	run({"assign", output, "--id", id, "--objects", "brush:99999", "--output", output, "--overwrite"}, 1);
	run({"rename", output, "--id", id, "--name", "No overwrite", "--output", output}, 1);
	run({"list", output, "--objects", "brush:0"}, 1);
	run({"visibility", output, "--id", id, "--visible", "yes", "--output", output, "--overwrite"}, 1);
	run({"lock", output, "--id", id, "--locked", "yes", "--output", output, "--overwrite"}, 1);
	run({"assign", output, "--id", "default", "--objects", "brush:0", "--output", output, "--overwrite"}, 1);
	run({"remove", output, "--id", id, "--output", output, "--overwrite"}, 1);
	QProcess edit;
	const auto refused = temp.filePath(QStringLiteral("refused.map"));
	edit.setWorkingDirectory(temp.path());
	edit.start(cli, {"--cli", "--json", "--settings-file", temp.filePath("cli.ini"), "map", "move", output,
		"--object", "brush:0", "--delta", "16,0,0", "--output", refused});
	const bool completed = edit.waitForStarted() && edit.waitForFinished(30000);
	const auto editOutput = edit.readAllStandardOutput() + edit.readAllStandardError();
	ok &= expect(completed && edit.exitStatus() == QProcess::NormalExit && edit.exitCode() != 0 &&
		editOutput.contains("Unlock") && !QFile::exists(refused), "actual map CLI refuses locked edit", QString::fromUtf8(editOutput));
	ok &= expect(read(output) == stable, "invalid and unapproved replacement operations preserve output");
	LevelMapDocument document;
	ok &= expect(loadLevelMap({output, {}, {}}, &document, &error), "load CLI output", error);
	const auto recovery = writeLevelMapRecovery(document, temp.filePath(QStringLiteral("recovery")),
												QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	LevelMapDocument restored;
	ok &= expect(!recovery.isEmpty() && restoreLevelMapRecovery(recovery, &restored, &error) && restored.scene == document.scene &&
					 restored.brushes.size() == document.brushes.size(),
				 "recovery carries scene and all geometry", error);
	// A package snapshot includes the exact native scene carrier, including the
	// hidden brush; publication must not use a filtered viewport document.
	const auto assets = temp.filePath(QStringLiteral("assets"));
	QDir().mkpath(assets);
	QFile packaged(QDir(assets).filePath(QStringLiteral("scene.map")));
	ok &= packaged.open(QIODevice::WriteOnly);
	packaged.write(stable);
	packaged.close();
	PackageArchive archive;
	ok &= expect(archive.load(assets, &error), "package folder load", error);
	PackageStagingModel package;
	ok &= expect(package.loadBaseArchiveSubset(archive, {QStringLiteral("scene.map")}, &error), "stage map snapshot", error);
	PackageWriteRequest publication;
	publication.destinationPath = temp.filePath(QStringLiteral("scene.pk3"));
	publication.verifyDeterminism = true;
	const auto published = package.writeArchive(publication);
	ok &= expect(published.succeeded() && published.determinismVerified && published.deterministic &&
					 archive.load(publication.destinationPath, &error),
				 "publish deterministic package", error);
	QByteArray entry;
	ok &= expect(archive.readEntryBytes(QStringLiteral("scene.map"), &entry, &error) && entry == stable &&
					 loadLevelMapBytes({QStringLiteral("packaged.map"), {}, {}}, entry, &restored, &error) &&
					 restored.scene == document.scene && restored.brushes.size() == document.brushes.size(),
				 "package entry roundtrip retains hidden authoring data", error);
	run({"lock", output, "--id", id, "--locked", "false", "--output", output, "--overwrite"});
	run({"assign", output, "--id", "default", "--objects", "brush:0", "--output", output, "--overwrite"});
	run({"remove", output, "--id", id, "--output", output, "--overwrite"});
	ok &= expect(loadLevelMap({output, {}, {}}, &restored, &error) && restored.scene.nodes.isEmpty() &&
					 !read(output).contains("VibeStudioScene:"),
				 "last-node removal removes metadata carrier", error);
	for (const auto& game : {QStringLiteral("doom"), QStringLiteral("hexen")}) {
		request.game = game;
		createLevelMap(request, &fixture);
		const auto wad = temp.filePath(game + QStringLiteral(".wad"));
		QFile binary(wad);
		ok &= binary.open(QIODevice::WriteOnly);
		binary.write(serializeLevelMap(fixture).bytes);
		binary.close();
		run({"list", wad}, 1);
		result = run({"create", wad, "--map-name", fixture.mapName, "--kind", "group", "--name", "Room", "--output", wad, "--overwrite"});
		const auto room = result.value(QStringLiteral("createdId")).toString();
		run({"assign", wad, "--map-name", fixture.mapName, "--id", room, "--objects", "sector:0,entity:0", "--output", wad, "--overwrite"});
		run({"lock", wad, "--map-name", fixture.mapName, "--id", room, "--locked", "true", "--output", wad, "--overwrite"});
		ok &= expect(loadLevelMap({wad, fixture.mapName, {}}, &restored, &error) &&
						 levelSceneMembership(restored.scene, QStringLiteral("thing:0")) == room &&
						 !moveLevelMapObject(&restored, "vertex", 0, 8, 0, 0, &error),
					 "binary map CLI routes selected marker", error);
		QStringList names;
		for (const auto& lump : restored.doomArchiveLumps) {
			names << lump.name;
		}
		QSet<qsizetype> included{0};
		QHash<qsizetype, QString> reasons;
		QStringList warnings;
		ok &= expect(expandPackageWadGroups(names, {0}, &included, &reasons, &warnings, &error) &&
						 included.contains(names.indexOf(QStringLiteral("VS_SCENE"))),
					 "WAD map subset keeps scene metadata with its map", error);
	}
	const auto groups = temp.filePath(QStringLiteral("groups.map")), moved = temp.filePath(QStringLiteral("groups-moved.map"));
	result = run({"create",input,"--kind","group","--name","Assembly","--output",groups});
	const auto groupId = result.value(QStringLiteral("createdId")).toString();
	run({"assign",groups,"--id",groupId,"--objects","brush:0,brush:1","--output",groups,"--overwrite"});
	const auto grouped = read(groups);
	result = run({"select",groups,"--objects","brush:1"});
	ok &= expect(result.value(QStringLiteral("selection")).toArray().size() == 2,"CLI selects closed group as unit");
	result = run({"select",groups,"--context",groupId,"--objects","brush:0,brush:2"});
	ok &= expect(result.value(QStringLiteral("selection")).toArray() == QJsonArray{"brush:0"}
		&& result.value(QStringLiteral("openGroups")).toArray() == QJsonArray{groupId},"CLI open context isolates direct objects");
	result = run({"select",groups,"--objects","brush:0,brush:1","--selection","brush:0,brush:1","--mode","toggle"});
	ok &= expect(result.value(QStringLiteral("selection")).toArray().isEmpty() && read(groups) == grouped,"CLI band toggles group once without writing");
	result = run({"select",groups,"--objects","brush:1","--selection","brush:0","--mode","toggle"});
	ok &= expect(result.value(QStringLiteral("selection")).toArray().isEmpty(),"CLI initial selection names group units");
	LevelMapDocument expectedMove;
	ok &= loadLevelMap({groups,{},{}},&expectedMove,&error);
	setLevelMapSelection(&expectedMove,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
	ok &= moveLevelMapSelection(&expectedMove,-8,0,0,{true,false},&error);
	result = run({"translate",groups,"--objects","brush:0","--offset","-8,0,0","--output",moved,"--dry-run"});
	ok &= expect(!QFile::exists(moved) && result.value(QStringLiteral("dryRun")).toBool(),"group translate dry run stays read-only");
	run({"translate",groups,"--objects","brush:0","--offset","-8,0,0","--output",moved});
	ok &= expect(read(moved) == serializeLevelMap(expectedMove).bytes && read(groups) == grouped,"CLI translates whole group through shared texture lock and persistence");
	const auto refusedContext = temp.filePath(QStringLiteral("refused-context.map"));
	run({"translate",groups,"--context",groupId,"--objects","brush:0,brush:2","--offset","8,0,0","--output",refusedContext},1);
	ok &= expect(!QFile::exists(refusedContext),"mixed outside-context CLI move cannot publish partial output");
	run({"select",groups,"--objects","brush:0","--mode","surprise"},1);
	run({"select",groups,"--objects","brush:0","--context",groupId,"--context",groupId},1);
	run({"translate",groups,"--objects","brush:0","--offset","nan,0,0","--output",refusedContext},1);
	run({"select",groups,"--objects","brush:99999"},1);
	std::cout << (ok ? "Level scene CLI smoke passed\n" : "Level scene CLI smoke failed\n");
	return ok ? 0 : 1;
}
