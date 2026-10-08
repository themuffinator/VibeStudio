#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_placement_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (argc != 2 || !temp.isValid()) {
		return 1;
	}
	bool ok = true;
	const auto expect = [&](bool pass, const char* message, const QString& error = {}) {
		if (!pass) {
			ok = false;
			std::cerr << message << ": " << error.toStdString() << '\n';
		}
		return pass;
	};
	const auto read = [](const QString& path) {
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	};
	const auto write = [&](const QString& path, const QByteArray& bytes) {
		QFile f(path);
		return expect(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write fixture", f.errorString());
	};
	const auto run = [&](const QStringList& args, bool success = true) {
		QProcess p;
		p.setWorkingDirectory(temp.path());
		p.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file", temp.filePath("cli.ini"), "map"} + args);
		const bool ended = p.waitForStarted() && p.waitForFinished(30000);
		const auto out = p.readAllStandardOutput(), err = p.readAllStandardError();
		expect(ended && p.exitStatus() == QProcess::NormalExit && (success ? p.exitCode() == 0 : p.exitCode() != 0), "CLI result",
			   QString::fromUtf8(out + err));
		if (!ended) {
			p.kill();
			p.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto input = temp.filePath("input.map"), snippet = temp.filePath("snippet.map"), output = temp.filePath("placed.map");
	const auto bytes = tests::placementFixture("classic");
	write(input, bytes);
	QString error;
	LevelMapDocument original;
	expect(loadLevelMap({input, {}, {}}, &original, &error), "load fixture", error);
	selectLevelMapObject(&original, "brush:0");
	write(snippet, levelMapSelectionText(original).toUtf8());
	const QStringList paste{"paste", input, "--from", snippet, "--delta", "31,-17,11", "--output", output};
	auto report = run(paste + QStringList{"--dry-run"});
	expect(!QFile::exists(output) && read(input) == bytes && report["pasted"].toArray().size() == 1, "dry run");
	report = run(paste);
	expect(report["textureLockPolicy"] == "locked" && report["pasted"].toArray().size() == 1, "paste reports default lock");
	LevelMapDocument placed;
	expect(loadLevelMap({output, {}, {}}, &placed, &error), "reload paste", error);
	expect(tests::placementUvsMatch(original.brushes.first(), placed.brushes.last(), {31, -17, 11, true}), "CLI pasted UV oracle");
	const auto saved = read(output);
	run(paste, false);
	expect(read(output) == saved, "existing output protected");
	run(paste + QStringList{"--overwrite", "--texture-lock", "off"});
	loadLevelMap({output, {}, {}}, &placed, &error);
	expect(!tests::placementUvsMatch(original.brushes.first(), placed.brushes.last(), {31, -17, 11, true}), "CLI unlocked paste");
	for (const auto& invalid : QVector<QStringList>{{"--typo"}, {"--delta", "1,2,3"}, {"--texture-lock", "maybe"}, {"--from"}, {"extra"}}) {
		const auto before = read(output);
		run(paste + QStringList{"--overwrite"} + invalid, false);
		expect(read(output) == before && read(input) == bytes, "bad arguments do not publish");
	}
	write(snippet, QByteArray(9 * 1024 * 1024, ' '));
	run(paste + QStringList{"--overwrite"}, false);
	write(snippet, QByteArray("{\n\xff\n}\n"));
	run(paste + QStringList{"--overwrite"}, false);
	write(snippet, levelMapSelectionText(original).toUtf8());
	write(snippet, QByteArray::fromHex("efbbbf") + levelMapSelectionText(original).toUtf8());
	run(paste + QStringList{"--overwrite"});
	write(snippet, levelMapSelectionText(original).toUtf8());
	report = run({"duplicate", input, "--object", "brush:0", "--delta", "31,-17,11", "--output", output, "--overwrite"});
	expect(report["textureLockPolicy"] == "locked", "CLI duplicate locks by default");
	loadLevelMap({output, {}, {}}, &placed, &error);
	expect(tests::placementUvsMatch(original.brushes.first(), placed.brushes.last(), {31, -17, 11, true}), "CLI duplicate UV oracle");
	const QStringList array{"duplicate", input, "--object", "brush:0", "--delta", "31,-17,11", "--copies", "3", "--output", output, "--overwrite"};
	const auto beforeArray = read(output);
	report = run(array + QStringList{"--dry-run"});
	expect(report["copies"].toArray().size() == 3 && report["copyCount"] == 3 && read(output) == beforeArray,
		"CLI array dry-run reports every copy without publication");
	report = run(array);
	expect(loadLevelMap({output, {}, {}}, &placed, &error) && placed.brushes.size() == 4 &&
		tests::placementUvsMatch(original.brushes.first(), placed.brushes.last(), {93, -51, 33, true}), "CLI array cumulative UV oracle", error);
	for (const auto& count : QStringList{"0", "257", "-1", "1.5", "many"}) {
		const auto before = read(output);
		run({"duplicate", input, "--object", "brush:0", "--copies", count, "--output", output, "--overwrite"}, false);
		expect(read(output) == before && read(input) == bytes, "invalid CLI array count cannot publish");
	}
	const auto beforeFailure = read(output);
	run(array + QStringList{"--copies", "2"}, false);
	run({"duplicate", input, "--object", "brush:0", "--copies", "4", "--delta", "30000,0,0", "--output", output, "--overwrite"}, false);
	expect(read(output) == beforeFailure && read(input) == bytes, "ambiguous or later out-of-range array preserves output");
	report = run({"snap", input, "--object", "brush:0", "--grid", "16", "--output", output, "--overwrite"});
	expect(report["snapped"].toArray() == QJsonArray{"brush:0"} && report["textureLockPolicy"] == "locked", "CLI snap report");
	loadLevelMap({output, {}, {}}, &placed, &error);
	expect(tests::placementUvsMatch(original.brushes.first(), placed.brushes.first(), {-3, -3, 5, true}), "CLI snap UV oracle");
	run({"snap", input, "--object", "brush:0", "--grid", "nan", "--output", output, "--overwrite"}, false);
	QString node;
	createLevelSceneNode(&original, LevelSceneNodeKind::Layer, "Locked", {}, &node, &error);
	assignLevelSceneObjects(&original, node, {"brush:0"}, &error);
	setLevelSceneLocked(&original, node, true, &error);
	write(input, serializeLevelMap(original).bytes);
	const auto before = read(output);
	run({"duplicate", input, "--object", "brush:0", "--delta", "16,0,0", "--output", output, "--overwrite"}, false);
	run({"snap", input, "--object", "brush:0", "--grid", "16", "--output", output, "--overwrite"}, false);
	expect(read(output) == before, "locked CLI edits preserve output");
	return ok ? 0 : 1;
}
