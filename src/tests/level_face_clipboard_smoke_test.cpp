#include "core/level_face_clipboard.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << '\n'; } return value;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-5; }
bool write(const QString& path, const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
QByteArray read(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QString error;
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	const auto cli = [&](QStringList args, bool success = true) {
		QProcess process; process.setProgram(QString::fromLocal8Bit(argv[1]));
		process.setArguments(QStringList{"--cli", "--json", "--settings-file", temp.filePath("settings.ini")} + args);
		process.start(); const bool finished = process.waitForFinished(30000);
		const auto out = process.readAllStandardOutput();
		expect(finished && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success, "CLI exit", QString::fromUtf8(out + process.readAllStandardError()));
		return QJsonDocument::fromJson(out).object();
	};
	for (const QString& kind : {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument source, destination;
		if (!expect(loadLevelMapBytes({"face.map", {}, "idtech3"}, tests::surfaceFixture(kind), &source, &error), "load source", error)) { return 1; }
		destination = source;
		auto& last = source.brushes[0].faces[1];
		last.textureName = "studio/last"; last.rotation = 90; last.shiftX = last.uOffset = 13; last.shiftY = last.vOffset = -19;
		last.scaleX = -2; last.scaleY = 3; last.contentFlags = 99; last.surfaceFlags = 128; last.surfaceValue = 27;
		last.textureMatrix = {0.25, 0.125, -0.5, 0.03125, 0.0625, 0.75};
		source.brushes[0].faces[0].textureName = "studio/first";
		const auto text = levelFaceSelectionText(source, {{0, 0}, {0, 1}}, &error);
		QVector<LevelMapBrushFace> faces; LevelSurfaceClipboard clipboard;
		expect(!text.isEmpty() && looksLikeLevelFaceText("/* heading */\n" + text) && parseLevelFaceText("// heading\n" + text, &faces, &error)
			&& faces.size() == 2 && faces.last().textureName == "studio/last", "ordered face text round trip", error);
		expect(parseLevelFaceClipboard(text, &clipboard, &error) && clipboard.material() == "studio/last", "last record supplies attributes", error);
		if (kind == "valve220") {
			// Oblique UV basis: rotation must use its normal, not the face normal.
			destination.brushes[0].faces[1].uAxis = {1, 0, 1, true}; destination.brushes[0].faces[1].vAxis = {0, 1, 0, true};
		}
		const auto before = destination;
		LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Attributes;
		LevelSurfaceEditPlan plan;
		expect(prepareLevelSurfacePaste(destination, {{0, 1}}, clipboard, options, &plan, &error) && commitLevelSurfaceEdit(&destination, plan, &error), "attribute transaction", error);
		const auto& pasted = destination.brushes[0].faces[1];
		expect(pasted.contentFlags == 2 && pasted.surfaceFlags == 128 && pasted.surfaceValue == 27 && pasted.textureName == "studio/last", "contents survive; explicit surface fields transfer");
		if (kind == "valve220") {
			expect(near(pasted.uAxis.x, 0) && near(pasted.uAxis.y, std::sqrt(2.0)) && near(pasted.uAxis.z, 0)
				&& near(pasted.vAxis.x, -1 / std::sqrt(2.0)) && near(pasted.vAxis.y, 0) && near(pasted.vAxis.z, -1 / std::sqrt(2.0)), "Valve rotation uses the existing oblique UV basis");
		}
		if (!pasted.explicitTextureMatrix) { expect(near(pasted.scaleX, -2) && near(pasted.scaleY, 3) && near(pasted.rotation, 90), "mapping values transfer"); }
		const auto applied = serializeLevelMap(destination).bytes;
		LevelMapDocument reopened; expect(loadLevelMapBytes({"reopened.map", {}, "idtech3"}, applied, &reopened, &error)
			&& reopened.brushes[0].faces[1].contentFlags == 2 && reopened.brushes[0].faces[1].surfaceFlags == 128, "save/reopen retains flag policy", error);
		expect(undoLevelMapEdit(&destination) && serializeLevelMap(destination).bytes == serializeLevelMap(before).bytes, "single undo restores exact source");
		expect(redoLevelMapEdit(&destination) && serializeLevelMap(destination).bytes == applied, "single redo restores paste");
		// A prepared paste must not bypass locks or publish over a later map edit.
		destination = before; QString layer;
		expect(createLevelSceneNode(&destination, LevelSceneNodeKind::Layer, "Locked", {}, &layer, &error)
			&& assignLevelSceneObjects(&destination, layer, {"brush:0"}, &error) && setLevelSceneLocked(&destination, layer, true, &error), "locked target fixture", error);
		const auto locked = serializeLevelMap(destination).bytes;
		expect(prepareLevelSurfacePaste(destination, {{0, 1}}, clipboard, options, &plan, &error)
			&& !commitLevelSurfaceEdit(&destination, plan, &error) && serializeLevelMap(destination).bytes == locked, "attributes obey scene locks", error);
		destination = before;
		expect(prepareLevelSurfacePaste(destination, {{0, 1}}, clipboard, options, &plan, &error), "prepare stale plan", error);
		++destination.revision;
		expect(!commitLevelSurfaceEdit(&destination, plan, &error) && serializeLevelMap(destination).bytes == serializeLevelMap(before).bytes, "stale attributes cannot publish");
		last.contentFlags = last.surfaceFlags = last.surfaceValue = 0; last.flagsWritten = true;
		expect(parseLevelFaceClipboard(levelFaceSelectionText(source, {{0, 1}}), &clipboard, &error), "parse explicit zero flags", error);
		destination = before;
		expect(prepareLevelSurfacePaste(destination, {{0, 1}}, clipboard, options, &plan, &error) && commitLevelSurfaceEdit(&destination, plan, &error)
			&& loadLevelMapBytes({"zero.map", {}, "idtech3"}, serializeLevelMap(destination).bytes, &reopened, &error)
			&& reopened.brushes[0].faces[1].contentFlags == 2 && reopened.brushes[0].faces[1].surfaceFlags == 0
			&& reopened.brushes[0].faces[1].surfaceValue == 0 && reopened.brushes[0].faces[1].flagsWritten, "explicit zero clears surface fields through save/reopen", error);
		const auto absentBytes = tests::surfaceFixture(kind).replace(" 2 4 8", "");
		expect(loadLevelMapBytes({"absent.map", {}, "idtech3"}, absentBytes, &destination, &error) && !destination.brushes[0].faces[1].flagsWritten, "target omits flag fields", error);
		expect(prepareLevelSurfacePaste(destination, {{0, 1}}, clipboard, options, &plan, &error) && commitLevelSurfaceEdit(&destination, plan, &error)
			&& loadLevelMapBytes({"explicit-zero.map", {}, "idtech3"}, serializeLevelMap(destination).bytes, &reopened, &error)
			&& reopened.brushes[0].faces[1].flagsWritten && reopened.brushes[0].faces[1].contentFlags == 0
			&& reopened.brushes[0].faces[1].surfaceFlags == 0 && reopened.brushes[0].faces[1].surfaceValue == 0, "explicit zero writes previously absent fields", error);
		expect(undoLevelMapEdit(&destination) && serializeLevelMap(destination).bytes == absentBytes, "undo restores absent flag fields exactly");
		last.flagsWritten = false; last.contentFlags = last.surfaceFlags = last.surfaceValue = 0;
		expect(parseLevelFaceClipboard(levelFaceSelectionText(source, {{0, 1}}), &clipboard, &error), "parse absent flags", error);
		destination = before;
		expect(prepareLevelSurfacePaste(destination, {{0, 1}}, clipboard, options, &plan, &error) && commitLevelSurfaceEdit(&destination, plan, &error)
			&& destination.brushes[0].faces[1].surfaceFlags == 4 && destination.brushes[0].faces[1].surfaceValue == 8, "absent source flags preserve target values", error);
		bool cancelled = false;
		expect(!prepareLevelSurfacePaste(before, {{0, 1}}, clipboard, options, &plan, &error, [&] { cancelled = true; return true; }) && cancelled, "surface cancellation refuses plan");
		if (argc > 1) {
			const auto input = temp.filePath(kind + ".map"), copy = temp.filePath(kind + "-faces.map"), output = temp.filePath(kind + "-pasted.map");
			expect(write(input, tests::surfaceFixture(kind)) && write(copy, text.toUtf8()), "write CLI fixtures");
			const auto result = cli({"map", "paste-surface", input, "--clipboard", copy, "--target", "face:0:2", "--output", output});
			expect(result.value("mode") == "attributes" && result.value("changedFaces").toInt() == 1, "raw face CLI defaults to attributes");
			const auto protectedBytes = read(output);
			cli({"map", "paste-surface", input, "--clipboard", copy, "--target", "face:0:2", "--output", output}, false);
			cli({"map", "paste-surface", input, "--clipboard", copy, "--target", "face:0:2", "--output", output, "--dry-run", "--overwrite"});
			expect(read(output) == protectedBytes && read(input) == tests::surfaceFixture(kind), "CLI protects inputs/existing output");
			const auto copied = cli({"map", "copy-surface", input, "--target", "face:0:1", "--target", "face:0:2", "--format", "map", "--output", copy, "--overwrite"});
			expect(copied.value("faceCount").toInt() == 2 && parseLevelFaceText(QString::fromUtf8(read(copy)), &faces, &error) && faces.size() == 2, "CLI exports ordered raw faces", error);
			expect(write(copy, QByteArray("\xff( invalid", 10)), "invalid UTF-8 fixture");
			cli({"map", "paste-surface", input, "--clipboard", copy, "--target", "face:0:2", "--output", output, "--overwrite"}, false);
			expect(read(output) == protectedBytes, "malformed UTF-8 cannot overwrite output");
		}
	}
	{
		LevelMapDocument classic, valve, matrix; LevelSurfaceClipboard clipboard; LevelSurfaceEditPlan plan;
		LevelSurfacePasteOptions options; options.mode = LevelSurfacePasteMode::Attributes;
		loadLevelMapBytes({"classic.map", {}, "idtech3"}, tests::surfaceFixture("classic"), &classic, &error);
		loadLevelMapBytes({"valve.map", {}, "idtech3"}, tests::surfaceFixture("valve220"), &valve, &error);
		loadLevelMapBytes({"matrix.map", {}, "idtech3"}, tests::surfaceFixture("brushDef"), &matrix, &error);
		classic.brushes[0].faces[0].shiftX = 123;
		expect(copyLevelSurface(classic, {0, 0}, &clipboard, &error) && prepareLevelSurfacePaste(valve, {{0, 0}}, clipboard, options, &plan, &error)
			&& commitLevelSurfaceEdit(&valve, plan, &error) && valve.brushes[0].faces[0].explicitTextureAxes && valve.brushes[0].faces[0].uOffset == 123, "classic to Valve attributes preserves target dialect", error);
		expect(copyLevelSurface(valve, {0, 0}, &clipboard, &error) && prepareLevelSurfacePaste(classic, {{0, 2}}, clipboard, options, &plan, &error)
			&& commitLevelSurfaceEdit(&classic, plan, &error) && !classic.brushes[0].faces[2].explicitTextureAxes && classic.brushes[0].faces[2].shiftX == 123, "Valve to classic attributes preserves target dialect", error);
		expect(!prepareLevelSurfacePaste(matrix, {{0, 0}}, clipboard, options, &plan, &error) && !plan.ready(), "matrix/non-matrix attributes conversion refuses atomically");
		expect(!prepareLevelSurfaceTransfer(classic, {{LevelMaterialKind::Patch, 0, 0}}, clipboard, options, &plan, &error) && !plan.ready(), "attributes refuses patch targets");
	}
	const QString valid = "( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) studio/a 1 2 0 1 1\n";
	for (const auto& invalid : QStringList{valid + "garbage", valid + "/* unterminated", valid + "{ }", valid + "( 0 0 0 )",
		"( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) studio/a 1 2 0 0 1", valid.trimmed() + " 1 2", valid.trimmed() + " 1.5 2 3",
		valid.trimmed() + " 1 2 4294967295", QString(valid).replace(QStringLiteral("studio/a"), QStringLiteral("\"unterminated")), QString(4194305, ' ')}) {
		QVector<LevelMapBrushFace> faces(1); faces[0].textureName = "sentinel";
		const bool rejected = !parseLevelFaceText(invalid, &faces, &error);
		expect(rejected && faces.size() == 1 && faces[0].textureName == "sentinel", "invalid face text leaves output unchanged", invalid.left(150) + " / " + error);
	}
	QVector<LevelMapBrushFace> cancelledFaces;
	expect(!parseLevelFaceText(valid, &cancelledFaces, &error, [] { return true; }), "face parser cancellation");
	std::cout << (ok ? "Face clipboard parser, attributes, history and CLI checks passed.\n" : "Face clipboard checks failed.\n");
	return ok ? 0 : 1;
}
