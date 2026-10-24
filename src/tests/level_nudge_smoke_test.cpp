// Directional and depth nudges: the shared axis service (core/level_nudge.h),
// the profile keys and policy, the placement edits the shell sends, and
// `map nudge`.
#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_editor_controls.h"
#include "core/level_nudge.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/map_geometry.h"
#include "core/studio_semantics.h"
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
QString text(const LevelMapVec3& v)
{
	return QStringLiteral("%1 %2 %3 %4").arg(v.x).arg(v.y).arg(v.z).arg(v.valid ? QStringLiteral("valid") : QStringLiteral("invalid"));
}
bool same(const LevelMapVec3& v, double x, double y, double z)
{
	return v.valid && v.x == x && v.y == y && v.z == z;
}
LevelMapDocument boxMap()
{
	LevelMapDocument created;
	LevelMapCreateRequest request;
	request.starterRoom = false;
	QString error;
	expect(createLevelMap(request, &created, &error)
			&& addLevelMapBoxBrush(&created, {0, 0, 0, true}, {32, 32, 32, true}, QStringLiteral("studio/checker"), nullptr, &error)
			&& addLevelMapBoxBrush(&created, {64, 0, 0, true}, {96, 32, 32, true}, QStringLiteral("studio/checker"), nullptr, &error),
		"box fixture", error);
	LevelMapDocument map;
	expect(loadLevelMapBytes({QStringLiteral("nudge.map"), {}, QStringLiteral("idtech3")}, serializeLevelMap(created).bytes, &map, &error), "box reload", error);
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
	return map;
}

void commandTable()
{
	const QStringList ids {QStringLiteral("map.nudgeLeft"), QStringLiteral("map.nudgeRight"), QStringLiteral("map.nudgeForward"), QStringLiteral("map.nudgeBack"),
		QStringLiteral("map.nudgeDepthUp"), QStringLiteral("map.nudgeDepthDown"), QStringLiteral("map.nudgeWorldUp"), QStringLiteral("map.nudgeWorldDown")};
	const QStringList names {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("forward"), QStringLiteral("back"), QStringLiteral("depth-up"),
		QStringLiteral("depth-down"), QStringLiteral("z-up"), QStringLiteral("z-down")};
	// The palette's hyphenated spellings name the same commands.
	const QStringList paletteIds {QStringLiteral("map.nudge-left"), QStringLiteral("map.nudge-right"), QStringLiteral("map.nudge-forward"),
		QStringLiteral("map.nudge-back"), QStringLiteral("map.nudge-depth-up"), QStringLiteral("map.nudge-depth-down"), QStringLiteral("map.nudge-world-up"),
		QStringLiteral("map.nudge-world-down")};
	const auto& commands = levelNudgeCommands();
	expect(commands.size() == 8, "eight nudge commands");
	for (int index = 0; index < commands.size() && index < ids.size(); ++index) {
		const auto& command = commands.at(index);
		LevelNudgeCommand byId;
		LevelNudgeCommand byName;
		LevelNudgeCommand byPalette;
		CommandPaletteEntry entry;
		expect(command.commandId == ids.at(index) && command.name == names.at(index) && static_cast<int>(command.direction) == index
				&& levelNudgeCommandForId(ids.at(index), &byId) && byId.commandId == command.commandId
				&& levelNudgeCommandForName(names.at(index).toUpper(), &byName) && byName.commandId == command.commandId
				&& levelNudgeCommandForId(paletteIds.at(index), &byPalette) && byPalette.commandId == command.commandId,
			"ordered ids, names and lookups", command.commandId);
		expect(shellCommandIdExists(command.commandId) && commandPaletteEntryForCommandId(command.commandId, &entry) && !entry.label.isEmpty()
				&& !entry.summary.isEmpty() && entry.defaultShortcut.isEmpty(),
			"palette entry without its own key", command.commandId);
	}
	expect(!levelNudgeCommandForId(QStringLiteral("map.nudgeCameraTextureLeft")) && !levelNudgeCommandForName(QStringLiteral("up")),
		"texture nudges and unknown names are not object nudges");
	expect(levelNudgeDirectionNames().split(QStringLiteral(", ")) == names, "usage lists every direction name");
}

void planAxes()
{
	struct Row {
		MapRenderProjection projection;
		LevelMapVec3 right, up, towards;
	};
	// Top looks down -Z, Front along +Y and Side along -X.
	const QVector<Row> rows {{MapRenderProjection::TopXY, {1, 0, 0, true}, {0, 1, 0, true}, {0, 0, 1, true}},
		{MapRenderProjection::FrontXZ, {1, 0, 0, true}, {0, 0, 1, true}, {0, -1, 0, true}},
		{MapRenderProjection::SideZY, {0, 1, 0, true}, {0, 0, 1, true}, {1, 0, 0, true}}};
	for (const Row& row : rows) {
		const QString label = QString::number(static_cast<int>(row.projection));
		const auto axis = [&](LevelNudgeDirection direction) { return levelPlanNudgeAxis(row.projection, direction); };
		const auto& r = row.right;
		const auto& u = row.up;
		const auto& t = row.towards;
		expect(same(axis(LevelNudgeDirection::Right), r.x, r.y, r.z) && same(axis(LevelNudgeDirection::Left), -r.x, -r.y, -r.z), "left and right follow the screen", label);
		expect(same(axis(LevelNudgeDirection::Forward), u.x, u.y, u.z) && same(axis(LevelNudgeDirection::Back), -u.x, -u.y, -u.z), "forward is up the screen", label);
		expect(same(axis(LevelNudgeDirection::DepthUp), t.x, t.y, t.z) && same(axis(LevelNudgeDirection::DepthDown), -t.x, -t.y, -t.z),
			"depth moves towards and away from the viewer", label);
		expect(same(axis(LevelNudgeDirection::ZUp), 0, 0, 1) && same(axis(LevelNudgeDirection::ZDown), 0, 0, -1), "Z ignores the view", label);
		// Right x up = towards the viewer: a right-handed, unmirrored view.
		expect(r.y * u.z - r.z * u.y == t.x && r.z * u.x - r.x * u.z == t.y && r.x * u.y - r.y * u.x == t.z, "right-handed view", label);
		double right = 0, up = 0;
		const auto inPlane = levelNudgeDelta(axis(LevelNudgeDirection::Forward), 16);
		expect(levelPlanNudgePlaneDelta(row.projection, inPlane, &right, &up) && right == 0 && up == 16, "in-plane delta in screen terms", label);
		expect(!levelPlanNudgePlaneDelta(row.projection, levelNudgeDelta(axis(LevelNudgeDirection::DepthUp), 16), &right, &up),
			"depth delta is out of the plane", label);
	}
}

void cameraAxes()
{
	const auto camera = [](double yaw, double pitch, LevelNudgeDirection direction) { return levelCameraNudgeAxis(yaw, pitch, direction); };
	// Level, looking along +X: forward +X, right -Y (Y is left).
	expect(same(camera(0, 0, LevelNudgeDirection::Forward), 1, 0, 0) && same(camera(0, 0, LevelNudgeDirection::Back), -1, 0, 0)
			&& same(camera(0, 0, LevelNudgeDirection::Right), 0, -1, 0) && same(camera(0, 0, LevelNudgeDirection::Left), 0, 1, 0),
		"level camera along +X", text(camera(0, 0, LevelNudgeDirection::Right)));
	// The nearest horizontal world axis: 30 degrees still +X, 60 degrees +Y.
	expect(same(camera(30, -20, LevelNudgeDirection::Forward), 1, 0, 0) && same(camera(60, 10, LevelNudgeDirection::Forward), 0, 1, 0)
			&& same(camera(60, 10, LevelNudgeDirection::Right), 1, 0, 0) && same(camera(180, 0, LevelNudgeDirection::Forward), -1, 0, 0)
			&& same(camera(-100, 0, LevelNudgeDirection::Forward), 0, -1, 0),
		"camera snaps to the nearest horizontal axis", text(camera(60, 10, LevelNudgeDirection::Right)));
	// Straight down, forward is up the picture; straight up, still the same way.
	expect(same(camera(0, -90, LevelNudgeDirection::Forward), 1, 0, 0) && same(camera(90, -90, LevelNudgeDirection::Forward), 0, 1, 0)
			&& same(camera(0, 90, LevelNudgeDirection::Forward), 1, 0, 0),
		"looking straight down or up uses the camera's up axis", text(camera(0, -90, LevelNudgeDirection::Forward)));
	for (const auto direction : {LevelNudgeDirection::DepthUp, LevelNudgeDirection::ZUp}) {
		expect(same(camera(37, 12, direction), 0, 0, 1), "depth and Z move up world Z from the camera");
	}
	expect(same(camera(37, 12, LevelNudgeDirection::DepthDown), 0, 0, -1) && same(camera(37, 12, LevelNudgeDirection::ZDown), 0, 0, -1), "and down");
	// A rolled camera whose right snaps onto forward turns a quarter about Z.
	expect(same(levelCameraNudgeAxis({1, 0, 0, true}, {0.9, -0.1, 0, true}, {0, 0, 1, true}, LevelNudgeDirection::Right), 0, -1, 0),
		"right never equals forward");
	expect(!levelCameraNudgeAxis({0, 0, 0, true}, {0, -1, 0, true}, {0, 0, 1, true}, LevelNudgeDirection::Forward).valid
			&& !levelCameraNudgeAxis(std::nan(""), 0, LevelNudgeDirection::Forward).valid,
		"a degenerate camera has no axis");
}

void supportAndDelta()
{
	LevelMapDocument doom;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("doom");
	QString error;
	expect(createLevelMap(request, &doom, &error), "Doom fixture", error);
	const auto quake = boxMap();
	for (const auto& command : levelNudgeCommands()) {
		const auto axis = levelPlanNudgeAxis(MapRenderProjection::TopXY, command.direction);
		const bool planar = axis.z == 0;
		expect(levelNudgeAxisSupported(quake, axis) && levelNudgeAxisSupported(doom, axis) == planar, "Doom maps nudge in the plane only", command.name);
	}
	expect(!levelNudgeAxisSupported(LevelMapDocument {}, {1, 0, 0, true}) && !levelNudgeAxisSupported(quake, {}), "no map or axis, no nudge");
	expect(same(levelNudgeDelta({0, -1, 0, true}, 16), 0, -16, 0) && same(levelNudgeDelta({1, 0, 0, true}, 0.125, 8), 1, 0, 0), "grid steps");
	expect(!levelNudgeDelta({1, 0, 0, true}, 0).valid && !levelNudgeDelta({1, 0, 0, true}, 16, 0).valid && !levelNudgeDelta({1, 0, 0, true}, 16, 65).valid
			&& !levelNudgeDelta({}, 16).valid && !levelNudgeDelta({1, 0, 0, true}, std::nan("")).valid,
		"invalid grids, steps and axes give no delta");
}

void profiles()
{
	expect(levelNudgeCameraFollowsPlan(netRadiantCustomLevelControls()) && levelNudgeCameraFollowsPlan(gtkRadiantLevelControls())
			&& !levelNudgeCameraFollowsPlan(trenchBroomLevelControls()) && !levelNudgeCameraFollowsPlan(vibeStudioLevelControls()),
		"Radiant cameras nudge in the plan view's axes; TrenchBroom's camera-relative");
	const auto keysOf = [](const LevelEditorControls& controls, const char* id) {
		for (const auto& binding : controls.keys) {
			if (binding.commandId == QLatin1String(id)) { return binding.keys; }
		}
		return QStringList {};
	};
	const auto tb = trenchBroomLevelControls();
	expect(keysOf(tb, "map.nudgeLeft") == QStringList {QStringLiteral("Left")} && keysOf(tb, "map.nudgeForward") == QStringList {QStringLiteral("Up")}
			&& keysOf(tb, "map.nudgeDepthUp") == QStringList {QStringLiteral("PgUp")} && keysOf(tb, "map.nudgeDepthDown") == QStringList {QStringLiteral("PgDown")}
			&& keysOf(tb, "map.nudgeWorldUp").isEmpty(),
		"TrenchBroom: arrows and Page Up/Down");
	for (const auto& controls : {netRadiantCustomLevelControls(), gtkRadiantLevelControls()}) {
		expect(keysOf(controls, "map.nudgeLeft") == QStringList {QStringLiteral("Alt+Left")} && keysOf(controls, "map.nudgeBack") == QStringList {QStringLiteral("Alt+Down")}
				&& keysOf(controls, "map.nudgeWorldUp") == QStringList {QStringLiteral("Num++")}
				&& keysOf(controls, "map.nudgeWorldDown") == QStringList {QStringLiteral("Num+-")} && keysOf(controls, "map.nudgeDepthUp").isEmpty(),
			"Radiant: Alt+arrows and the keypad's plus and minus");
	}
	expect(keysOf(vibeStudioLevelControls(), "map.nudgeRight") == QStringList {QStringLiteral("Right")}, "VibeStudio: the arrows");
	for (const auto& profile : editorProfileDescriptors()) {
		const QString held = profile.controls.plan.arrowsDriveCamera ? QStringLiteral("Alt+") : QString();
		expect(keysOf(profile.controls, "map.nudgeLeft") == QStringList {held + QStringLiteral("Left")}
				&& keysOf(profile.controls, "map.nudgeForward") == QStringList {held + QStringLiteral("Up")},
			"every profile names its plan nudge keys", profile.id);
		expect(levelEditorControlProblems(profile.controls).isEmpty(), "nudge keys keep every profile consistent",
			profile.id + QLatin1Char(' ') + levelEditorControlProblems(profile.controls).join(QLatin1Char(';')));
		QString conflict;
		expect(!editorProfileHasShortcutConflict(profile, &conflict), "no profile key conflict", profile.id + QLatin1Char(' ') + conflict);
		for (const EditorProfileBinding& binding : profile.bindings) {
			if (levelNudgeCommandForId(binding.commandId)) {
				expect(binding.implemented && !binding.shortcut.isEmpty(), "nudge bindings are implemented shell commands", profile.id + QLatin1Char(' ') + binding.commandId);
			}
		}
	}
}

// The edits the shell sends for a nudge: one undo step each, undo restores the
// exact source, and locked objects refuse without changes.
void placements()
{
	auto map = boxMap();
	const auto original = serializeLevelMap(map).bytes;
	const auto delta = levelNudgeDelta(levelPlanNudgeAxis(MapRenderProjection::FrontXZ, LevelNudgeDirection::DepthUp), 16);
	LevelPlacementRequest move;
	move.operation = LevelPlacementOperation::Move;
	move.offset = delta;
	move.grid = 16;
	move.snapMoveDelta = true;
	move.textures = {true, false};
	auto result = prepareLevelPlacement(map, move);
	LevelMapVec3 mins, maxs;
	QString error;
	expect(result.succeeded && result.document.undoStack.size() == 1 && levelMapSelectionBounds(result.document, &mins, &maxs)
			&& same(mins, 0, -16, 0) && same(maxs, 32, 16, 32),
		"a depth nudge from the front view moves the selection along -Y", result.error + error + text(mins));
	auto undone = result.document;
	expect(undoLevelMapEdit(&undone) && serializeLevelMap(undone).bytes == original, "undo restores the exact source");

	LevelPlacementRequest components;
	components.operation = LevelPlacementOperation::TransformComponents;
	LevelMapComponentSet set;
	set.object = {LevelMapSelectionKind::QuakeBrush, 0};
	set.kind = LevelBrushComponent::Vertex;
	set.components = {0};
	components.components = {set};
	components.componentTransform.delta = levelNudgeDelta(levelCameraNudgeAxis(0, 0, LevelNudgeDirection::ZUp), 8);
	result = prepareLevelPlacement(map, components);
	expect(result.succeeded && result.document.undoStack.size() == 1 && result.componentReport.brushes == 1, "a Z nudge moves a picked vertex as one step",
		result.error);

	QString layer;
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&map, layer, true, &error),
		"lock fixture", error);
	const auto locked = serializeLevelMap(map).bytes;
	const auto depth = map.undoStack.size();
	result = prepareLevelPlacement(map, move);
	expect(!result.succeeded && !result.error.isEmpty() && serializeLevelMap(map).bytes == locked && map.undoStack.size() == depth,
		"a locked selection refuses the nudge without changes");
}

void cli(const QString& binary, const QDir& root)
{
	const auto map = boxMap();
	const QString input = root.filePath(QStringLiteral("boxes.map"));
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
	const auto reference = [&](const LevelMapVec3& delta, const QVector<LevelMapSelectionRef>& objects) {
		auto expected = map;
		QString error;
		expect(setLevelMapSelection(&expected, objects, &error) && moveLevelMapSelection(&expected, delta.x, delta.y, delta.z, {true, false}, &error),
			"reference move", error);
		return serializeLevelMap(expected).bytes;
	};
	const auto savedBytes = [](const QString& path) {
		LevelMapDocument saved;
		QString error;
		expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &saved, &error), "saved map loads", error);
		return serializeLevelMap(saved).bytes;
	};

	// A depth nudge from the front view: towards the viewer, -Y.
	const QString front = root.filePath(QStringLiteral("front.map"));
	const auto depth = run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--object"), QStringLiteral("brush:0"),
		QStringLiteral("--object"), QStringLiteral("brush:1"), QStringLiteral("--direction"), QStringLiteral("depth-up"), QStringLiteral("--view"),
		QStringLiteral("front"), QStringLiteral("--grid"), QStringLiteral("8"), QStringLiteral("--output"), front}, 0);
	const auto delta = depth.value(QStringLiteral("delta")).toObject();
	expect(depth.value(QStringLiteral("frame")) == QStringLiteral("front") && depth.value(QStringLiteral("direction")) == QStringLiteral("depth-up")
			&& depth.value(QStringLiteral("commandId")) == QStringLiteral("map.nudgeDepthUp") && depth.value(QStringLiteral("objects")).toInt() == 2
			&& delta.value(QStringLiteral("x")).toDouble() == 0 && delta.value(QStringLiteral("y")).toDouble() == -8 && delta.value(QStringLiteral("z")).toDouble() == 0,
		"CLI reports the front view's depth axis", QString::fromUtf8(QJsonDocument(depth).toJson(QJsonDocument::Compact)).left(500));
	expect(savedBytes(front) == reference({0, -8, 0, true}, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}}),
		"CLI and the shared move write the same map");

	// Camera-relative: 60 degrees of yaw is nearest +Y; three steps.
	const QString cameraOutput = root.filePath(QStringLiteral("camera.map"));
	const auto camera = run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--object"), QStringLiteral("brush:0"),
		QStringLiteral("--direction"), QStringLiteral("forward"), QStringLiteral("--camera-yaw"), QStringLiteral("60"), QStringLiteral("--camera-pitch"),
		QStringLiteral("-30"), QStringLiteral("--steps"), QStringLiteral("3"), QStringLiteral("--texture-lock"), QStringLiteral("off"), QStringLiteral("--output"),
		cameraOutput}, 0);
	expect(camera.value(QStringLiteral("frame")) == QStringLiteral("camera") && camera.value(QStringLiteral("delta")).toObject().value(QStringLiteral("y")).toDouble() == 48
			&& camera.value(QStringLiteral("textureLockPolicy")) == QStringLiteral("source-parameters"),
		"CLI camera frame", QString::fromUtf8(QJsonDocument(camera).toJson(QJsonDocument::Compact)).left(500));

	// Components along world Z.
	const QString componentOutput = root.filePath(QStringLiteral("components.map"));
	const auto components = run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--set"), QStringLiteral("brush:0/face/0"),
		QStringLiteral("--direction"), QStringLiteral("z-down"), QStringLiteral("--uv-lock"), QStringLiteral("on"), QStringLiteral("--output"), componentOutput}, 0);
	expect(components.value(QStringLiteral("mode")) == QStringLiteral("components") && components.value(QStringLiteral("brushes")).toInt() == 1
			&& components.value(QStringLiteral("delta")).toObject().value(QStringLiteral("z")).toDouble() == -16,
		"CLI component nudge", QString::fromUtf8(QJsonDocument(components).toJson(QJsonDocument::Compact)).left(500));

	// Usage errors write nothing.
	const QString bad = root.filePath(QStringLiteral("bad.map"));
	run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--direction"),
		QStringLiteral("up"), QStringLiteral("--output"), bad}, 2);
	run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--direction"),
		QStringLiteral("left"), QStringLiteral("--view"), QStringLiteral("top"), QStringLiteral("--camera-yaw"), QStringLiteral("0"), QStringLiteral("--output"), bad}, 2);
	run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--direction"),
		QStringLiteral("left"), QStringLiteral("--steps"), QStringLiteral("0"), QStringLiteral("--output"), bad}, 2);
	run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--set"), QStringLiteral("brush:0/face/0"), QStringLiteral("--direction"),
		QStringLiteral("left"), QStringLiteral("--texture-lock"), QStringLiteral("on"), QStringLiteral("--output"), bad}, 2);
	expect(!QFile::exists(bad), "usage errors write nothing");

	// Doom maps nudge in the plane only.
	LevelMapDocument doom;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("doom");
	QString error;
	const QString wad = root.filePath(QStringLiteral("doom.wad"));
	expect(createLevelMap(request, &doom, &error) && saveLevelMapAs(doom, wad).succeeded(), "Doom CLI fixture", error);
	const auto things = doom.doomThings.isEmpty() ? QStringLiteral("thing:0") : QStringLiteral("thing:%1").arg(doom.doomThings.first().id);
	run({QStringLiteral("map"), QStringLiteral("nudge"), wad, QStringLiteral("--object"), things, QStringLiteral("--direction"), QStringLiteral("z-up"),
		QStringLiteral("--output"), root.filePath(QStringLiteral("doom-z.wad"))}, 4);
	expect(!QFile::exists(root.filePath(QStringLiteral("doom-z.wad"))), "a refused Doom Z nudge writes nothing");
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
	planAxes();
	cameraAxes();
	supportAndDelta();
	profiles();
	placements();
	if (argc > 1) {
		cli(QString::fromLocal8Bit(argv[1]), QDir(temp.path()));
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
