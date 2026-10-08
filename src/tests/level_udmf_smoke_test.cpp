#include "core/doom_preview_geometry.h"
#include "core/level_document.h"
#include "core/level_doom_nodes.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_udmf.h"
#include "tests/level_udmf_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace vibestudio;
namespace f = vibestudio::tests::udmf;
namespace d = vibestudio::tests::doom;
namespace {
bool ok = true;
bool expect(bool condition, const char* message, const QString& error = {}) {
	if (!condition) {
		ok = false;
		std::cerr << message << ": " << error.toStdString() << '\n';
	}
	return condition;
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	QString error;
	const auto input = temp.filePath("source.wad");
	const auto original = f::fixture();
	expect(d::write(input, original), "write fixture");
	LevelMapDocument source;
	if (!expect(loadLevelMap({input, "MAP01", {}}, &source, &error), "load UDMF", error)) {
		return 1;
	}
	expect(source.doomUdmf && source.doomUdmf->nameSpace == "zdoom" && source.doomVertices.size() == 4 && source.doomLinedefs.size() == 4 &&
			   source.doomThings.size() == 1,
		   "native projection");
	expect(source.doomVertices[0].x == .25 && source.doomSectors[0].floorHeight == .5 && source.doomSidedefs[0].offsetX == 1.5 &&
			   source.entities[0].origin.z == .5,
		   "fractional native values and mirrors");
	expect(inspectLevelDoomNodes(source).state == LevelDoomNodeState::Present, "UDMF node validation");
	const auto preview = buildDoomPreviewGeometry(source);
	expect(preview.walls == 4 && preview.floors > 0 && preview.ceilings > 0, "UDMF room preview");
	expect(serializeLevelMap(source).bytes == original, "untouched WAD exact bytes");
	LevelMapDocument binary;
	expect(createLevelMap({.game = QStringLiteral("doom")}, &binary, &error), "binary regression fixture", error);
	binary.doomSectors[0].floorHeight = 0.5;
	expect(!serializeLevelMap(binary).errors.isEmpty(), "binary heights never silently truncate UDMF-capable storage");
	binary.doomSectors[0].floorHeight = 0;
	binary.doomSidedefs[0].offsetX = 1.5;
	expect(!serializeLevelMap(binary).errors.isEmpty(), "binary offsets never silently truncate UDMF-capable storage");
	const auto immutable = source;
	auto added = immutable;
	expect(editLevelMapUdmfProperties(&added, {{"global", "user_added", "\"new\""}, {"vertex:0", "user_extra", "-1.25e2"}}, &error) &&
			   added.doomUdmf->source.indexOf("user_added") < added.doomUdmf->source.indexOf("vertex {") &&
			   added.doomUdmf->source.contains("user_extra = -1.25e2;\r\n"),
		   "new globals precede map blocks and additions preserve CRLF", error);
	QByteArray largeText = f::textmap() + '\n';
	for (int i = 0; i < 20000; ++i) {
		largeText += "vertex { x = 1; y = 2; }\n";
	}
	auto large = immutable;
	expect(readLevelUdmfText(largeText, &large, &error), "large UDMF parse", error);
	QVector<LevelUdmfPropertyEdit> batch;
	for (int i = 0; i < 1000; ++i) {
		batch << LevelUdmfPropertyEdit{QStringLiteral("vertex:%1").arg(19004 + i), "x", "3.25"};
	}
	QElapsedTimer elapsed;
	elapsed.start();
	expect(editLevelMapUdmfProperties(&large, batch, &error) && large.doomVertices.last().x == 3.25 && large.undoStack.size() == 1,
		   "large batch resolves identities and commits once", error);
	std::cout << "20,004-vertex UDMF / 1,000-property edit: " << elapsed.elapsed() << " ms\n";
	selectLevelMapObject(&source, "vertex:0");
	expect(editLevelMapUdmfProperties(&source,
									  {{"vertex:0", "x", "16.5"},
									   {"sector:0", "heightfloor", "8.25"},
									   {"thing:0", "user_note", "\"new note\""},
									   {"extension:0", "version", "0xDEAD"},
									   {"sidedef:0", "user_style", {}, true}},
									  &error),
		   "atomic multi-object edit", error);
	expect(source.undoStack.size() == 1 && source.doomVertices[0].x == 16.5 && source.doomSectors[0].floorHeight == 8.25 &&
			   source.doomVertices[0].selected,
		   "one step and selection");
	expect(immutable.doomUdmf->source == f::textmap(), "immutable snapshot");
	expect(inspectLevelDoomNodes(source).state == LevelDoomNodeState::Stale, "edit invalidates nodes");
	const auto edited = serializeLevelMap(source);
	const auto records = d::lumps(edited.bytes);
	const auto oldRecords = d::lumps(original);
	expect(edited.errors.isEmpty() && records.size() == oldRecords.size(), "serialized edited map", edited.errors.join('\n'));
	for (int i = 0; i < records.size() && i < oldRecords.size(); ++i) {
		if (i == 1) {
			continue;
		}
		if (i == 3) {
			expect(records[i].bytes.isEmpty(), "clear ZNODES");
			continue;
		}
		expect(records[i].name == oldRecords[i].name && records[i].bytes == oldRecords[i].bytes,
			   "retain other maps, sidecars, assets, duplicates");
	}
	auto expected = f::textmap();
	expected.replace("x = +0.2500", "x = 16.5");
	expected.replace("heightfloor = 0.5", "heightfloor = 8.25");
	expected.replace("user_note = \"untouched\"", "user_note = \"new note\"");
	expected.replace("version = 9007199254740993", "version = 0xDEAD");
	expected.remove(expected.indexOf("user_style = Fancy;"), 19);
	expect(source.doomUdmf->source == expected, "replace only requested byte spans");
	expect(undoLevelMapEdit(&source, &error) && serializeLevelMap(source).bytes == original &&
			   inspectLevelDoomNodes(source).state == LevelDoomNodeState::Present,
		   "exact undo", error);
	expect(redoLevelMapEdit(&source, &error) && serializeLevelMap(source).bytes == edited.bytes, "exact redo", error);
	const auto committed = source.doomUdmf->source;
	const auto revision = source.revision;
	auto moved = source;
	expect(moveLevelMapObject(&moved, "vertex", 0, 16, 0, 0, &error) && moved.doomVertices[0].x == 32.5 &&
			   moved.doomUdmf->source.contains("x = 32.5") && source.revision == revision,
		   "native move writes UDMF text and leaves its source snapshot unchanged", error);
	for (const auto& edits : QVector<QVector<LevelUdmfPropertyEdit>>{{{"vertex:0", "x", "2"}, {"linedef:0", "v1", "999"}},
																	 {{"vertex:0", "x", {}, true}},
																	 {{"global", "namespace", "42"}},
																	 {{"vertex:0", "x", "1; injected=1"}},
																	 {{"vertex:0", "x y", "2"}},
																	 {{"vertex:0", "x", "1e500"}},
																	 {{"vertex:0", "x", "1"}, {"vertex:0", "x", "2"}},
																	 {{"unknown:0", "x", "1"}},
																	 {{"thing:0", "skill2", "notbool"}}}) {
		expect(!editLevelMapUdmfProperties(&source, edits, &error) && !error.isEmpty() && source.revision == revision &&
				   source.doomUdmf->source == committed,
			   "invalid batch atomic", error);
	}
	int checkpoints = 0;
	expect(!editLevelMapUdmfProperties(&source, {{"vertex:0", "x", "3"}}, &error, [&] { return ++checkpoints > 20; }) &&
			   source.doomUdmf->source == committed && source.revision == revision,
		   "cancelled parse atomic");
	expect(!setLevelMapSectorProperty(&source, 0, "floorheight", "10", &error) &&
			   !setLevelMapEntityProperty(&source, 0, "type", "2", &error),
		   "binary setters cannot lose UDMF changes");
	const auto recovery = writeLevelMapRecovery(source, temp.path(), "219c2f62-c97b-4db4-b48c-47cc18618f01", &error);
	LevelMapDocument restored;
	expect(!recovery.isEmpty() && restoreLevelMapRecovery(recovery, &restored, &error) && restored.doomUdmf->source == committed,
		   "recovery exact text", error);
	const auto output = temp.filePath("saved.wad");
	expect(saveLevelMapAs(source, output).succeeded(), "save edited WAD");
	LevelMapDocument loaded;
	expect(loadLevelMap({output, "MAP01", {}}, &loaded, &error) && loaded.doomUdmf->source == committed &&
			   inspectLevelDoomNodes(loaded).state == LevelDoomNodeState::Missing,
		   "reopen persistent rebuild state", error);
	auto locked = immutable;
	QString group;
	expect(createLevelSceneNode(&locked, LevelSceneNodeKind::Group, "Protected", {}, &group, &error) &&
			   assignLevelSceneObjects(&locked, group, {"sector:0", "thing:0"}, &error) &&
			   setLevelSceneLocked(&locked, group, true, &error),
		   "scene lock setup", error);
	for (const auto& selector : QStringList{"sidedef:0", "thing:0", "vertex:0", "global", "extension:0"}) {
		expect(!editLevelMapUdmfProperties(&locked, {{selector, "user_note", "\"changed\""}}, &error), "extension edits honor locks",
			   error);
	}
	expect(setLevelSceneLocked(&locked, group, false, &error) &&
			   editLevelMapUdmfProperties(&locked, {{"thing:0", "user_note", "\"changed\""}}, &error),
		   "unlock edit", error);
	const auto scenePath = temp.filePath("scene.wad");
	expect(saveLevelMapAs(locked, scenePath).succeeded(), "scene save");
	LevelMapDocument sceneLoaded;
	expect(loadLevelMap({scenePath, "MAP01", {}}, &sceneLoaded, &error) && sceneLoaded.scene.nodes.size() == 1 &&
			   sceneLoaded.scene.problem.isEmpty(),
		   "scene metadata inside ENDMAP", error);
	for (const auto& text : QList<QByteArray>{
			 "namespace=\"zdoom\"; vertex{x=1;}", "namespace=\"zdoom\"; /* unterminated", "namespace=\"zdoom\"; extension { bad = @; }",
			 "namespace=\"zdoom\"; extension { a = \"bad; }", QByteArray("namespace=\"zdoom\"; // nul") + QByteArray(1, '\0')}) {
		auto copy = immutable;
		expect(!readLevelUdmfText(text, &copy, &error) && copy.doomUdmf->source == f::textmap(), "malformed source atomic", error);
	}
	auto duplicate = f::textmap();
	duplicate.replace("x = +0.2500;", "x = 1; x = +0.2500;");
	auto dupeDoc = immutable;
	expect(readLevelUdmfText(duplicate, &dupeDoc, &error) && !editLevelMapUdmfProperties(&dupeDoc, {{"vertex:0", "x", "2"}}, &error),
		   "ambiguous key refused");
	auto missingEnd = oldRecords;
	missingEnd.removeAt(4);
	const auto malformed = temp.filePath("missing-end.wad");
	expect(d::write(malformed, d::wad(missingEnd)) && !loadLevelMap({malformed, "MAP01", {}}, &loaded, &error), "missing ENDMAP refused");
	if (argc > 1) {
		const auto run = [&](QStringList words) {
			QProcess p;
			p.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file", temp.filePath("cli.ini")} + words);
			expect(p.waitForFinished(30000) && p.exitCode() == 0, "CLI exit", QString::fromUtf8(p.readAllStandardError()));
			return QJsonDocument::fromJson(p.readAllStandardOutput()).object();
		};
		expect(run({"map", "inspect-udmf", input, "--map-name", "MAP01"}).value("udmf").toObject().value("namespace") == "zdoom",
			   "CLI inspection");
		const auto cliOutput = temp.filePath("cli.wad");
		const QStringList args{"map",	   "edit-udmf", input,	   "--map-name", "MAP01",  "--object",
							   "vertex:0", "--set",		"x=32.75", "--output",	 cliOutput};
		run(args + QStringList{"--dry-run"});
		expect(!QFileInfo::exists(cliOutput), "dry-run no write");
		run(args);
		expect(loadLevelMap({cliOutput, "MAP01", {}}, &loaded, &error) && loaded.doomVertices[0].x == 32.75, "CLI edit roundtrip", error);
		run({"package", "validate", cliOutput});
		const auto arrayOutput = temp.filePath("array-cli.wad");
		const QStringList arrayArgs{"map", "duplicate", input, "--map-name", "MAP01", "--object", "thing:0",
			"--delta", "0.375,-0.125,0.25", "--copies", "3", "--output", arrayOutput};
		run(arrayArgs + QStringList{"--dry-run"});
		expect(!QFileInfo::exists(arrayOutput), "UDMF array dry-run does not write");
		const auto arrayReport = run(arrayArgs);
		expect(arrayReport.value("copies").toArray().size() == 3 && loadLevelMap({arrayOutput, "MAP01", {}}, &loaded, &error)
			&& loaded.doomThings.size() == 4 && loaded.doomThings.last().x == 65.375 && loaded.doomThings.last().z == 1.25
			&& loaded.doomUdmf->source.startsWith(f::textmap()) && loaded.doomUdmf->source.count("user_note = \"untouched\";") == 4
			&& inspectLevelDoomNodes(loaded).state == LevelDoomNodeState::Present, "CLI UDMF thing array roundtrip", error);
	}
	return ok ? 0 : 1;
}
