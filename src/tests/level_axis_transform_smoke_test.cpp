// World-axis Flip X/Y/Z and Rotate X/Y/Z, and the grid-snapped quick-turn
// pivot: the shared core service, the profile policy and `map rotate
// --pivot-grid` (core/level_quick_transform.h).
#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_editor_controls.h"
#include "core/level_gestures.h"
#include "core/level_placement.h"
#include "core/level_quick_transform.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/map_geometry.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}
bool onGrid(double value, double grid) { return std::abs(value / grid - std::round(value / grid)) < 1e-9; }
// Every solved corner of every brush lies on the grid.
bool brushesOnGrid(const LevelMapDocument& map, double grid, QString* detail = nullptr)
{
	for (const LevelMapBrush& brush : map.brushes) {
		const MapBrushGeometry mesh = solveBrushGeometry(brush.faces);
		if (!mesh.solved) { if (detail) { *detail = QStringLiteral("brush %1 unsolved").arg(brush.id); } return false; }
		for (const auto& face : mesh.faces) {
			for (const LevelMapVec3& p : face.points) {
				if (!onGrid(p.x, grid) || !onGrid(p.y, grid) || !onGrid(p.z, grid)) {
					if (detail) { *detail = QStringLiteral("brush %1 corner %2 %3 %4").arg(brush.id).arg(p.x).arg(p.y).arg(p.z); }
					return false;
				}
			}
		}
	}
	return true;
}
LevelMapDocument boxMap(const LevelMapVec3& mins, const LevelMapVec3& maxs)
{
	LevelMapDocument created;
	LevelMapCreateRequest request;
	request.starterRoom = false;
	QString error;
	expect(createLevelMap(request, &created, &error) && addLevelMapBoxBrush(&created, mins, maxs, QStringLiteral("studio/checker"), nullptr, &error),
		"box fixture", error);
	LevelMapDocument map;
	expect(loadLevelMapBytes({QStringLiteral("axis.map"), {}, QStringLiteral("idtech3")}, serializeLevelMap(created).bytes, &map, &error), "box reload", error);
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
	return map;
}
LevelMapRotationRequest turn(int axis, int quarterTurns, const LevelMapVec3& pivot = {})
{
	LevelMapRotationRequest request;
	request.axis = axis;
	request.degrees = 90.0 * quarterTurns;
	request.pivot = pivot;
	return request;
}

void commandTable()
{
	const auto& transforms = levelAxisTransforms();
	const QStringList ids {QStringLiteral("map.flipX"), QStringLiteral("map.flipY"), QStringLiteral("map.flipZ"),
		QStringLiteral("map.rotateX"), QStringLiteral("map.rotateY"), QStringLiteral("map.rotateZ")};
	expect(transforms.size() == 6, "six world-axis transforms");
	for (int index = 0; index < transforms.size() && index < ids.size(); ++index) {
		const auto& transform = transforms.at(index);
		LevelAxisTransform found;
		expect(transform.commandId == ids.at(index) && transform.axis == index % 3 && transform.mirror == (index < 3)
				&& levelAxisTransformForCommand(ids.at(index), &found) && found.commandId == transform.commandId,
			"ordered ids, axes and kinds", transform.commandId);
	}
	// NetRadiant Custom's signs: X -90, Y +90, Z -90.
	expect(transforms.at(3).quarterTurns == -1 && transforms.at(4).quarterTurns == 1 && transforms.at(5).quarterTurns == -1
			&& transforms.at(0).quarterTurns == 0, "NRC rotation signs");
	expect(!levelAxisTransformForCommand(QStringLiteral("map.flipHorizontal")), "view-relative commands are not world-axis transforms");
	LevelMapDocument doom;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("doom");
	QString error;
	expect(createLevelMap(request, &doom, &error), "Doom fixture", error);
	const auto quake = boxMap({0, 0, 0, true}, {16, 16, 16, true});
	for (const auto& transform : transforms) {
		const bool planar = transform.mirror ? transform.axis != 2 : transform.axis == 2;
		expect(levelAxisTransformSupported(quake, transform) && levelAxisTransformSupported(doom, transform) == planar,
			"Doom geometry takes only planar transforms", transform.commandId);
	}
	expect(!levelAxisTransformSupported(LevelMapDocument {}, transforms.first()), "no map, no transform");
}

void pivotRounding()
{
	const LevelMapVec3 mins {0, 0, 0, true}, maxs {16, 32, 48, true};
	const auto exact = levelQuickTurnPivot(mins, maxs, 0);
	const auto snapped = levelQuickTurnPivot(mins, maxs, 16);
	expect(exact.valid && exact.x == 8 && exact.y == 16 && exact.z == 24, "no grid keeps the exact centre");
	// Ties round away from zero, as every VibeStudio grid snap does.
	expect(snapped.valid && snapped.x == 16 && snapped.y == 16 && snapped.z == 32, "grid rounds the centre", QStringLiteral("%1 %2 %3").arg(snapped.x).arg(snapped.y).arg(snapped.z));
	const auto negative = levelQuickTurnPivot({-16, -16, -16, true}, {0, 0, 0, true}, 16);
	expect(negative.x == -16 && negative.y == -16 && negative.z == -16, "negative ties mirror positive ones");
	LevelMapDocument empty;
	LevelMapVec3 pivot;
	QString error;
	expect(!levelMapQuickTurnPivot(empty, 16, &pivot, &error) && !error.isEmpty(), "empty selection has no pivot");
}

// A brush whose two cross sizes differ by an odd number of grid steps leaves
// the grid when it turns about its exact centre and stays on it about the
// rounded centre, for both turn directions about each axis.
void oddFootprints()
{
	const QVector<LevelMapVec3> sizes {{16, 16, 32, true}, {32, 16, 16, true}, {16, 32, 16, true}};
	for (int axis = 0; axis < 3; ++axis) {
		for (const int quarterTurns : {-1, 1}) {
			const QString label = QStringLiteral("axis %1 turns %2").arg(axis).arg(quarterTurns);
			auto exact = boxMap({0, 0, 0, true}, sizes.at(axis));
			QString error, detail;
			expect(brushesOnGrid(exact, 16, &detail), "fixture on grid", detail);
			expect(rotateLevelMapSelection(&exact, turn(axis, quarterTurns), &error) && !brushesOnGrid(exact, 16), "exact centre leaves the grid", label + error);
			auto snapped = boxMap({0, 0, 0, true}, sizes.at(axis));
			const auto original = serializeLevelMap(snapped).bytes;
			LevelMapVec3 pivot;
			expect(levelMapQuickTurnPivot(snapped, 16, &pivot, &error), "selection pivot", error);
			expect(rotateLevelMapSelection(&snapped, turn(axis, quarterTurns, pivot), &error) && brushesOnGrid(snapped, 16, &detail),
				"rounded pivot keeps the grid", label + error + detail);
			expect(snapped.undoStack.size() == 1 && snapped.repeat.actions.size() == 1
					&& snapped.repeat.actions.first().rotationPivot == std::array<double, 3> {pivot.x, pivot.y, pivot.z},
				"one undo step; Repeat records the rounded pivot", label);
			expect(undoLevelMapEdit(&snapped) && serializeLevelMap(snapped).bytes == original, "undo restores the exact source", label);
		}
	}
}

// The six commands' edits through the shared placement service, as the shell
// sends them: one undo step each, and locked objects refuse atomically.
void placementRequests()
{
	for (const auto& transform : levelAxisTransforms()) {
		auto map = boxMap({0, 0, 0, true}, {16, 16, 32, true});
		const auto original = serializeLevelMap(map).bytes;
		LevelPlacementRequest request;
		request.operation = transform.mirror ? LevelPlacementOperation::Mirror : LevelPlacementOperation::Rotate;
		request.axis = transform.axis;
		request.textures = {true, false};
		request.rotation = turn(transform.axis, transform.quarterTurns);
		expect(levelMapQuickTurnPivot(map, 16, &request.rotation.pivot), "pivot for placement");
		auto result = prepareLevelPlacement(map, request);
		QString detail;
		expect(result.succeeded && result.document.undoStack.size() == 1 && serializeLevelMap(result.document).bytes != original
				&& brushesOnGrid(result.document, 16, &detail),
			"placement applies one on-grid undo step", transform.commandId + result.error + detail);
		QString layer, error;
		expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&map, layer, true, &error),
			"lock fixture", error);
		const auto locked = serializeLevelMap(map).bytes;
		const auto depth = map.undoStack.size();
		result = prepareLevelPlacement(map, request);
		expect(!result.succeeded && !result.error.isEmpty() && serializeLevelMap(map).bytes == locked && map.undoStack.size() == depth,
			"locked selection refuses without changes", transform.commandId);
	}
}

void profilePolicy()
{
	for (const auto& profile : editorProfileDescriptors()) {
		const bool audited = profile.id == QLatin1String("trenchbroom") || profile.id == QLatin1String("netradiant-custom");
		expect(profile.controls.quickTurnSnapsPivot == audited, "only the audited TB and NRC profiles round the quick-turn pivot", profile.id);
		expect(levelGestureValues(profile.controls).value(QStringLiteral("editing.quickTurnSnapsPivot")).toString()
				== (audited ? QStringLiteral("on") : QStringLiteral("off")),
			"the policy is a gesture preference", profile.id);
		bool row = false;
		for (const auto& reference : levelEditorControlRows(profile.controls)) {
			row |= reference.section == QLatin1String("layout") && !reference.gesture.isEmpty()
				&& reference.action == QCoreApplication::translate("VibeStudioLevelControls", "Quick-turn pivot");
		}
		expect(row, "the controls reference names the policy", profile.id);
	}
	LevelEditorControls effective;
	QJsonObject normalized;
	QString error;
	expect(applyLevelGestureOverrides(vibeStudioLevelControls(), {{QStringLiteral("editing.quickTurnSnapsPivot"), QStringLiteral("on")}}, &effective,
			   &normalized, &error)
			&& effective.quickTurnSnapsPivot && normalized.value(QStringLiteral("editing.quickTurnSnapsPivot")) == QStringLiteral("on"),
		"any profile can opt in", error);
	expect(applyLevelGestureOverrides(netRadiantCustomLevelControls(), {{QStringLiteral("editing.quickTurnSnapsPivot"), QStringLiteral("off")}},
			   &effective, nullptr, &error)
			&& !effective.quickTurnSnapsPivot,
		"NRC can opt out", error);
}

void cli(const QString& binary, const QDir& root)
{
	const auto map = boxMap({0, 0, 0, true}, {16, 16, 32, true});
	const QString input = root.filePath(QStringLiteral("odd.map"));
	QFile file(input);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(serializeLevelMap(map).bytes) > 0, "CLI fixture")) { return; }
	file.close();
	const QString settings = root.filePath(QStringLiteral("cli.ini"));
	const auto run = [&](const QStringList& arguments, int expected) {
		QProcess process;
		process.setWorkingDirectory(root.path());
		process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("--settings-file"), settings} + arguments + QStringList {QStringLiteral("--json")});
		const bool finished = process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI exit",
			arguments.join(QLatin1Char(' ')) + QLatin1Char(' ') + QString::fromUtf8(out + process.readAllStandardError()).left(600));
		return QJsonDocument::fromJson(out).object();
	};
	const QString output = root.filePath(QStringLiteral("rotate-x.map"));
	const auto rotated = run({QStringLiteral("map"), QStringLiteral("rotate"), input, QStringLiteral("--object"), QStringLiteral("brush:0"),
		QStringLiteral("--axis"), QStringLiteral("x"), QStringLiteral("--turns"), QStringLiteral("-1"), QStringLiteral("--pivot-grid"), QStringLiteral("16"),
		QStringLiteral("--output"), output}, 0);
	const auto pivot = rotated.value(QStringLiteral("pivot")).toObject();
	expect(rotated.value(QStringLiteral("pivotGrid")).toDouble() == 16 && pivot.value(QStringLiteral("x")).toDouble() == 16
			&& pivot.value(QStringLiteral("y")).toDouble() == 16 && pivot.value(QStringLiteral("z")).toDouble() == 16,
		"CLI reports the rounded pivot", QString::fromUtf8(QJsonDocument(rotated).toJson(QJsonDocument::Compact)).left(400));
	LevelMapDocument saved;
	QString error, detail;
	expect(loadLevelMap({output, {}, QStringLiteral("idtech3")}, &saved, &error) && brushesOnGrid(saved, 16, &detail), "CLI Rotate X stays on grid", error + detail);
	auto reference = map;
	LevelMapVec3 expected;
	levelMapQuickTurnPivot(reference, 16, &expected);
	expect(rotateLevelMapSelection(&reference, turn(0, -1, expected), &error)
			&& serializeLevelMap(saved).bytes == serializeLevelMap(reference).bytes,
		"CLI and the shared service write the same map", error);
	run({QStringLiteral("map"), QStringLiteral("rotate"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--pivot-grid"),
		QStringLiteral("0"), QStringLiteral("--output"), root.filePath(QStringLiteral("zero.map"))}, 2);
	run({QStringLiteral("map"), QStringLiteral("rotate"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--pivot"),
		QStringLiteral("0,0,0"), QStringLiteral("--pivot-grid"), QStringLiteral("16"), QStringLiteral("--output"), root.filePath(QStringLiteral("both.map"))}, 2);
	expect(!QFile::exists(root.filePath(QStringLiteral("zero.map"))) && !QFile::exists(root.filePath(QStringLiteral("both.map"))), "usage errors write nothing");
	// The profile policy reaches the editor CLI and its gesture route.
	const auto nrc = run({QStringLiteral("editor"), QStringLiteral("controls"), QStringLiteral("netradiant-custom")}, 0).value(QStringLiteral("controls")).toObject();
	const auto vibe = run({QStringLiteral("editor"), QStringLiteral("controls"), QStringLiteral("vibestudio-default")}, 0).value(QStringLiteral("controls")).toObject();
	expect(nrc.value(QStringLiteral("quickTurnPivot")) == QStringLiteral("grid") && vibe.value(QStringLiteral("quickTurnPivot")) == QStringLiteral("centre"),
		"editor controls reports the quick-turn pivot");
	run({QStringLiteral("editor"), QStringLiteral("gestures"), QStringLiteral("vibestudio-default"), QStringLiteral("--set"), QStringLiteral("editing.quickTurnSnapsPivot=on")}, 0);
	expect(run({QStringLiteral("editor"), QStringLiteral("controls"), QStringLiteral("vibestudio-default")}, 0).value(QStringLiteral("controls")).toObject()
				   .value(QStringLiteral("quickTurnPivot")) == QStringLiteral("grid"),
		"the gesture preference changes the policy");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	commandTable();
	pivotRounding();
	oddFootprints();
	placementRequests();
	profilePolicy();
	if (argc > 1) {
		cli(QString::fromLocal8Bit(argv[1]), QDir(temp.path()));
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
