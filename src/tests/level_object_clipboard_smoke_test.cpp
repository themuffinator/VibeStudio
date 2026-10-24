#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_protected_properties.h"
#include "tests/level_object_clipboard_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <filesystem>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << '\n'; } return value;
}
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
LevelMapDocument pasted(const QString& text)
{
	LevelMapDocument result; LevelMapCreateRequest request; request.starterRoom = false;
	QString error; createLevelMap(request, &result, &error);
	expect(pasteLevelMapText(&result, text, &error), "clipboard is pasteable", error);
	return result;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	QString error; LevelMapDocument source;
	const auto original = tests::objectClipboardFixture();
	if (!expect(loadLevelMapBytes({"objects.map", {}, "idtech3"}, original, &source, &error), "fixture loads", error)) { return 1; }
	expect(source.brushes.size() == 2 && source.patches.size() == 2 && source.patches[1].entityId == 1, "mixed owner fixture");
	LevelPlacementRequest copy, cut; copy.operation = LevelPlacementOperation::Copy; cut.operation = LevelPlacementOperation::Cut;
	setLevelMapSelection(&source, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakePatch, 1}});
	const auto prepared = prepareLevelPlacement(source, copy);
	expect(prepared.succeeded && prepared.document.undoStack.isEmpty() && serializeLevelMap(prepared.document).bytes == original, "copy is read-only", prepared.error);
	auto assembly = pasted(prepared.clipboardText);
	expect(assembly.brushes.size() == 1 && assembly.patches.size() == 1 && assembly.entities.size() == 2, "partial owner only contains selected children");
	expect(tests::prefabProperty(assembly.entities.last(), "classname") == "func_door" && tests::prefabProperty(assembly.entities.last(), "targetname") == "gate"
		&& tests::prefabProperty(assembly.entities.last(), "speed") == "160" && assembly.brushes[0].entityId == assembly.patches[0].entityId,
		"owner properties and one shared wrapper retained");
	expect(prepared.clipboardText.indexOf("patchDef") > prepared.clipboardText.indexOf("( "), "partial child selection order retained");
	auto remaining = prepareLevelPlacement(source, cut);
	expect(remaining.succeeded && remaining.clipboardText == prepared.clipboardText && remaining.document.brushes.size() == 1
		&& remaining.document.patches.size() == 1 && remaining.document.undoStack.size() == 1, "cut removes whole selected children in one transaction", remaining.error);
	expect(serializeLevelMap(source).bytes == original && source.undoStack.isEmpty(), "preparation leaves source untouched");
	const auto cutBytes = serializeLevelMap(remaining.document).bytes;
	expect(undoLevelMapEdit(&remaining.document, &error) && serializeLevelMap(remaining.document).bytes == original, "exact cut undo", error);
	expect(redoLevelMapEdit(&remaining.document, &error) && serializeLevelMap(remaining.document).bytes == cutBytes, "exact cut redo", error);
	LevelMapDocument reopened;
	expect(loadLevelMapBytes({"remaining.map", {}, "idtech3"}, cutBytes, &reopened, &error) && reopened.brushes.size() == 1 && reopened.patches.size() == 1, "cut save/reopen", error);

	setLevelMapSelection(&source, {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakePatch, 1}});
	const auto complete = prepareLevelPlacement(source, copy); assembly = pasted(complete.clipboardText);
	expect(assembly.entities.size() == 2 && assembly.brushes.size() == 2 && assembly.patches.size() == 1, "entity plus selected children emits no duplicates");
	LevelMapDocument ordered;
	expect(loadLevelMapBytes({"ordered.map", {}, "idtech3"}, complete.clipboardText.toUtf8(), &ordered, &error)
		&& ordered.patches[0].startLine < ordered.brushes[0].startLine, "whole owner retains mixed child source order", error);
	setLevelMapSelection(&source, {{LevelMapSelectionKind::Entity, 3}, {LevelMapSelectionKind::Entity, 4}});
	assembly = pasted(prepareLevelPlacement(source, copy).clipboardText);
	expect(tests::prefabProperty(assembly.entities[1], "model") == "models/test.md3" && tests::prefabProperty(assembly.entities[2], "noise") == "sound/test.wav", "model/audio handoff references retained");

	QString group;
	expect(createLevelSceneNode(&source, LevelSceneNodeKind::Group, "Door", {}, &group, &error)
		&& assignLevelSceneObjects(&source, group, {"entity:1"}, &error)
		&& setLevelEntityPropertyProtection(&source, {1}, {"speed", "missing-key"}, true, &error), "native scene fixture", error);
	setLevelMapSelection(&source, {{LevelMapSelectionKind::QuakeBrush, 0}});
	assembly = pasted(prepareLevelPlacement(source, copy).clipboardText);
	expect(assembly.scene.nodes.isEmpty() && levelEntityPropertyProtected(assembly, assembly.entities.last().id, "speed")
		&& levelEntityPropertyProtected(assembly, assembly.entities.last().id, "missing-key"), "partial wrapper keeps protection without fabricating complete group");
	setLevelMapSelection(&source, {{LevelMapSelectionKind::Entity, 1}});
	assembly = pasted(prepareLevelPlacement(source, copy).clipboardText);
	expect(assembly.scene.nodes.size() == 1 && assembly.scene.nodes[0].name == "Door" && assembly.brushes.size() == 2, "whole owner still transfers group");
	expect(setLevelSceneLocked(&source, group, true, &error), "lock owner", error);
	const auto lockedBytes = serializeLevelMap(source).bytes;
	expect(prepareLevelPlacement(source, copy).succeeded, "locked objects may be copied");
	const auto lockedCut = prepareLevelPlacement(source, cut);
	expect(!lockedCut.succeeded && lockedCut.clipboardText.isEmpty() && lockedCut.document.format == LevelMapFormat::Unknown
		&& serializeLevelMap(source).bytes == lockedBytes, "locked cut publishes neither text nor candidate", lockedCut.error);

	loadLevelMapBytes({"objects.map", {}, "idtech3"}, original, &source);
	setLevelMapSelection(&source, {{LevelMapSelectionKind::Entity, 0}});
	expect(prepareLevelPlacement(source, copy).succeeded && !prepareLevelPlacement(source, cut).succeeded, "worldspawn copies geometry but cannot be cut");
	setLevelMapSelection(&source, {{LevelMapSelectionKind::QuakeBrush, 0}});
	for (const auto phase : {LevelPlacementPhase::Copying, LevelPlacementPhase::Cutting, LevelPlacementPhase::Complete}) {
		bool cancelled = false; LevelPlacementControl control;
		control.progress = [&](const auto& progress) { cancelled = progress.phase == phase; };
		control.isCancelled = [&] { return cancelled; };
		const auto stopped = prepareLevelPlacement(source, cut, control);
		expect(stopped.cancelled && !stopped.succeeded && stopped.clipboardText.isEmpty() && stopped.document.format == LevelMapFormat::Unknown
			&& serializeLevelMap(source).bytes == original, "cancel at phase keeps clipboard/candidate private");
	}
	int checkpoints = 0; LevelPlacementControl control; control.isCancelled = [&] { return ++checkpoints > 25; };
	expect(prepareLevelPlacement(source, copy, control).cancelled, "copy has cancellation checkpoints inside serialization");
	source.selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush, 999};
	expect(!prepareLevelPlacement(source, copy).succeeded && !prepareLevelPlacement(source, cut).succeeded, "stale reference cannot silently export partial selection");
	setLevelMapSelection(&source, {{LevelMapSelectionKind::Entity, 2}});
	for (const auto& value : {QString(4194305, 'x'), QString(3000000, QChar(0x4e00)), QString("bad") + QChar::Null}) {
		auto invalid = source; invalid.entities[2].properties << LevelMapProperty{"test", value, 0};
		const auto rejected = prepareLevelPlacement(invalid, copy);
		expect(!rejected.succeeded && rejected.clipboardText.isEmpty() && !rejected.error.isEmpty(), "unadorned clipboard enforces UTF-16/UTF-8/NUL bounds");
	}
	for (const auto& kind : {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument dialect; loadLevelMapBytes({"dialect.map", {}, "idtech3"}, tests::surfaceFixture(kind), &dialect);
		setLevelMapSelection(&dialect, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto clipped = prepareLevelPlacement(dialect, copy);
		assembly = pasted(clipped.clipboardText);
		expect(assembly.brushes.size() == 1 && assembly.brushes[0].faces.size() == dialect.brushes[0].faces.size()
			&& assembly.brushes[0].faces[0].textureName == dialect.brushes[0].faces[0].textureName, "all brush dialects copy and paste", kind);
	}

	if (argc > 1) {
		const auto cli = [&](QStringList args, bool success = true) {
			QProcess process; process.setProgram(QString::fromLocal8Bit(argv[1]));
			process.setArguments(QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini")} + args);
			process.start(); const bool finished = process.waitForFinished(30000); const auto out = process.readAllStandardOutput();
			expect(finished && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success,
				"CLI exit", QString::fromUtf8(out + process.readAllStandardError()));
			const auto json = QJsonDocument::fromJson(out).object(); expect(json.value("ok").toBool() == success, "structured CLI status"); return json;
		};
		const auto input = temp.filePath("source.map"), fragment = temp.filePath("fragment.map"), output = temp.filePath("remaining.map");
		expect(write(input, original), "write CLI input");
		const QStringList copying{"map", "copy", input, "--object", "brush:0", "--object", "patch:1", "--output", fragment};
		const QStringList cutting{"map", "cut", input, "--object", "brush:0", "--object", "patch:1", "--clipboard", fragment, "--output", output};
		auto result = cli(copying + QStringList{"--dry-run"});
		expect(!result.value("clipboardWritten").toBool() && !QFileInfo::exists(fragment), "dry copy writes nothing");
		result = cli(copying);
		expect(result.value("clipboardWritten").toBool() && !result.value("mapWritten").toBool() && read(input) == original, "copy protects source");
		const auto fragmentBytes = read(fragment); assembly = pasted(QString::fromUtf8(fragmentBytes));
		expect(assembly.brushes.size() == 1 && assembly.patches.size() == 1 && tests::prefabProperty(assembly.entities.last(), "speed") == "160", "CLI copy retains partial owner");
		cli(copying, false); cli(copying + QStringList{"--dry-run"}, false);
		const auto alias = temp.filePath("source-alias.map");
		const auto native = [](const QString& path) {
			return std::filesystem::path(path.toStdU16String());
		};
		// Both files are disposable generated fixtures in this test's directory.
		// No installed file or user content is linked into the test workspace.
		std::error_code linkError; std::filesystem::create_hard_link(native(input), native(alias), linkError);
		if (!linkError) {
			result = cli({"map", "copy", input, "--object", "brush:0", "--output", alias, "--overwrite"}, false);
			expect(!result.value("clipboardWritten").toBool() && read(input) == original && read(alias) == original, "source identity alias cannot be clipboard output");
		} else { std::cout << "Hard-link alias check unavailable on this filesystem: " << linkError.message() << '\n'; }
		result = cli(cutting + QStringList{"--dry-run", "--overwrite"});
		expect(!QFileInfo::exists(output) && read(fragment) == fragmentBytes && read(input) == original, "dry cut protects all paths");
		result = cli(cutting + QStringList{"--overwrite"});
		expect(result.value("clipboardWritten").toBool() && result.value("mapWritten").toBool() && read(input) == original
			&& loadLevelMap({output, {}, {}}, &reopened, &error) && reopened.brushes.size() == 1 && reopened.patches.size() == 1, "cut writes both files with source preserved", error);
		const auto outputBytes = read(output);
		for (const auto& bad : QList<QStringList>{copying + QStringList{"--unexpected"}, copying + QStringList{"--output", output},
			{"map", "copy", input, "--object", "brush:999", "--output", fragment, "--overwrite"},
			{"map", "copy", input, "--object", "brush:0", "--output", input, "--overwrite"},
			{"map", "cut", input, "--object", "entity:0", "--output", output, "--clipboard", fragment, "--overwrite"},
			{"map", "cut", input, "--object", "brush:0", "--output", fragment, "--clipboard", fragment, "--overwrite"},
			{"map", "cut", input, "--object", "brush:0", "--output", temp.filePath("missing/remaining.map"), "--clipboard", fragment, "--overwrite"}}) {
			result = cli(bad, false);
			expect(!result.value("clipboardWritten").toBool() && !result.value("mapWritten").toBool() && read(fragment) == fragmentBytes
				&& read(input) == original && read(output) == outputBytes, "invalid CLI operation publishes nothing");
		}
		const auto inPlaceClipboard = temp.filePath("in-place.map");
		result = cli({"map", "cut", input, "--object", "entity:1", "--clipboard", inPlaceClipboard, "--output", input, "--overwrite"});
		expect(result.value("mapWritten").toBool() && read(result.value("backup").toString()) == original, "in-place cut keeps exact backup");
		// Force a failure after fragment commit: the map backup directory is a file.
		const auto folder = temp.filePath("save-failure"); QDir().mkpath(folder);
		const auto failedMap = QDir(folder).filePath("map.map"), failedCopy = QDir(folder).filePath("copy.map");
		expect(write(failedMap, original) && write(QDir(folder).filePath(".vibestudio"), "occupied"), "late save failure fixture");
		result = cli({"map", "cut", failedMap, "--object", "brush:0", "--clipboard", failedCopy, "--output", failedMap, "--overwrite"}, false);
		expect(result.value("clipboardWritten").toBool() && !result.value("mapWritten").toBool() && !read(failedCopy).isEmpty()
			&& read(failedMap) == original, "partial publication is explicit and never loses cut objects");
	}
	std::cout << (ok ? "Object clipboard, ownership, history, refusal and CLI checks passed.\n" : "Object clipboard checks failed.\n");
	return ok ? 0 : 1;
}
