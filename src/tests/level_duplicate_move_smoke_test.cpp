// Duplicate and Move, Move to Camera and Repeat Transforms: the command table
// (core/level_nudge.h), the profile keys, the placement edits the shell sends,
// NetRadiant Custom's repeatable-transform recording and replay
// (core/level_repeat.h), Move to Camera (core/level_paste.h), and the CLI
// routes `map nudge --duplicate`, `map move --camera` and
// `map repeat-transforms`.
#include "core/editor_profiles.h"
#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_editor_controls.h"
#include "core/level_nudge.h"
#include "core/level_paste.h"
#include "core/level_placement.h"
#include "core/level_repeat.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/studio_semantics.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
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
	return QStringLiteral("%1 %2 %3").arg(v.x).arg(v.y).arg(v.z);
}
bool near(const LevelMapVec3& v, double x, double y, double z)
{
	return v.valid && std::abs(v.x - x) < 1e-6 && std::abs(v.y - y) < 1e-6 && std::abs(v.z - z) < 1e-6;
}
// The bounds of one object, as "minx miny minz / maxx maxy maxz".
QString boundsOf(const LevelMapDocument& map, const LevelMapSelectionRef& ref)
{
	LevelMapVec3 mins, maxs;
	if (!levelMapObjectsBounds(map, {ref}, &mins, &maxs)) {
		return QStringLiteral("none");
	}
	return text(mins) + QStringLiteral(" / ") + text(maxs);
}
QString box(double x0, double y0, double z0, double x1, double y1, double z1)
{
	return text({x0, y0, z0, true}) + QStringLiteral(" / ") + text({x1, y1, z1, true});
}
LevelMapSelectionRef brush(int id)
{
	return {LevelMapSelectionKind::QuakeBrush, id};
}
// Brush 0 is 64 by 32 by 32 at the origin, brush 1 sits far off, and entity 1
// is an info_null at 100,0,0 facing 30 degrees.
LevelMapDocument fixture()
{
	LevelMapDocument created;
	LevelMapCreateRequest request;
	request.starterRoom = false;
	QString error;
	expect(createLevelMap(request, &created, &error)
			&& addLevelMapBoxBrush(&created, {0, 0, 0, true}, {64, 32, 32, true}, QStringLiteral("studio/checker"), nullptr, &error)
			&& addLevelMapBoxBrush(&created, {256, 0, 0, true}, {288, 32, 32, true}, QStringLiteral("studio/checker"), nullptr, &error)
			&& addLevelMapEntity(&created, QStringLiteral("info_null"), {100, 0, 0, true}, {{QStringLiteral("angle"), QStringLiteral("30"), 0}}, nullptr, &error),
		"fixture", error);
	LevelMapDocument map;
	expect(loadLevelMapBytes({QStringLiteral("duplicate-move.map"), {}, QStringLiteral("idtech3")}, serializeLevelMap(created).bytes, &map, &error),
		"fixture reload", error);
	setLevelMapSelection(&map, {brush(0)});
	return map;
}
QString property(const LevelMapDocument& map, int entityId, const QString& key)
{
	for (const auto& entity : map.entities) {
		if (entity.id != entityId) { continue; }
		for (const auto& value : entity.properties) {
			if (value.key == key) { return value.value; }
		}
	}
	return {};
}
int lastEntityId(const LevelMapDocument& map)
{
	int id = -1;
	for (const auto& entity : map.entities) { id = std::max(id, entity.id); }
	return id;
}

void commandTable()
{
	const QStringList ids {QStringLiteral("map.duplicateMoveLeft"), QStringLiteral("map.duplicateMoveRight"), QStringLiteral("map.duplicateMoveForward"),
		QStringLiteral("map.duplicateMoveBack"), QStringLiteral("map.duplicateMoveDepthUp"), QStringLiteral("map.duplicateMoveDepthDown")};
	const QStringList palette {QStringLiteral("map.duplicate-move-left"), QStringLiteral("map.duplicate-move-right"), QStringLiteral("map.duplicate-move-forward"),
		QStringLiteral("map.duplicate-move-back"), QStringLiteral("map.duplicate-move-depth-up"), QStringLiteral("map.duplicate-move-depth-down")};
	const auto& commands = levelDuplicateMoveCommands();
	expect(commands.size() == 6, "six Duplicate and Move commands");
	const QVector<LevelNudgeDirection> directions {LevelNudgeDirection::Left, LevelNudgeDirection::Right, LevelNudgeDirection::Forward,
		LevelNudgeDirection::Back, LevelNudgeDirection::DepthUp, LevelNudgeDirection::DepthDown};
	for (int index = 0; index < commands.size() && index < ids.size(); ++index) {
		const auto& command = commands.at(index);
		LevelNudgeCommand byId, byPalette, nudge;
		CommandPaletteEntry entry;
		expect(command.commandId == ids.at(index) && command.direction == directions.at(index)
				&& levelDuplicateMoveCommandForId(ids.at(index), &byId) && byId.commandId == command.commandId
				&& levelDuplicateMoveCommandForId(palette.at(index), &byPalette) && byPalette.commandId == command.commandId
				&& levelNudgeCommandForName(command.name, &nudge) && nudge.direction == command.direction,
			"ordered ids, palette spellings and the nudge direction names", command.commandId);
		expect(!levelNudgeCommandForId(command.commandId), "a Duplicate and Move command is not a nudge", command.commandId);
		expect(shellCommandIdExists(command.commandId) && commandPaletteEntryForCommandId(command.commandId, &entry) && !entry.label.isEmpty()
				&& !entry.summary.isEmpty() && entry.defaultShortcut.isEmpty(),
			"palette entry without a key of its own", command.commandId);
	}
	for (const auto* id : {"map.moveToCamera", "map.repeatTransforms", "map.resetRepeatTransforms"}) {
		CommandPaletteEntry entry;
		expect(shellCommandIdExists(QString::fromLatin1(id)) && commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty()
				&& !entry.summary.isEmpty(),
			"palette entry", QString::fromLatin1(id));
	}
	expect(!levelDuplicateMoveCommandForId(QStringLiteral("map.nudgeLeft")) && !levelDuplicateMoveCommandForId(QStringLiteral("map.duplicateSelection")),
		"nudges and Duplicate Selection are not Duplicate and Move");
}

void profiles()
{
	const auto keysOf = [](const LevelEditorControls& controls, const char* id) {
		for (const auto& binding : controls.keys) {
			if (binding.commandId == QLatin1String(id)) { return binding.keys; }
		}
		return QStringList {};
	};
	const auto tb = trenchBroomLevelControls();
	expect(keysOf(tb, "map.duplicateMoveLeft") == QStringList {QStringLiteral("Ctrl+Left")} && keysOf(tb, "map.duplicateMoveRight") == QStringList {QStringLiteral("Ctrl+Right")}
			&& keysOf(tb, "map.duplicateMoveForward") == QStringList {QStringLiteral("Ctrl+Up")} && keysOf(tb, "map.duplicateMoveBack") == QStringList {QStringLiteral("Ctrl+Down")}
			&& keysOf(tb, "map.duplicateMoveDepthUp") == QStringList {QStringLiteral("Ctrl+PgUp")}
			&& keysOf(tb, "map.duplicateMoveDepthDown") == QStringList {QStringLiteral("Ctrl+PgDown")} && keysOf(tb, "map.repeatTransforms").isEmpty(),
		"TrenchBroom: Ctrl+arrows and Ctrl+Page Up/Down");
	const auto nrc = netRadiantCustomLevelControls();
	expect(keysOf(nrc, "map.repeatTransforms") == QStringList {QStringLiteral("Ctrl+R")} && keysOf(nrc, "map.moveToCamera") == QStringList {QStringLiteral("Ctrl+Shift+V")}
			&& keysOf(nrc, "map.duplicateMoveForward").isEmpty(),
		"NetRadiant Custom: Ctrl+R and Ctrl+Shift+V");
	for (const auto& profile : editorProfileDescriptors()) {
		expect(levelEditorControlProblems(profile.controls).isEmpty(), "the new keys keep every profile consistent",
			profile.id + QLatin1Char(' ') + levelEditorControlProblems(profile.controls).join(QLatin1Char(';')));
		QString conflict;
		expect(!editorProfileHasShortcutConflict(profile, &conflict), "no profile key conflict", profile.id + QLatin1Char(' ') + conflict);
	}
}

// Duplicate and Move is one duplicate at the nudge offset: one undo step, one
// Repeat entry, the copies selected and the originals untouched.
void duplicateAndMove()
{
	auto map = fixture();
	const auto original = serializeLevelMap(map).bytes;
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Duplicate;
	request.offset = levelNudgeDelta(levelPlanNudgeAxis(MapRenderProjection::FrontXZ, LevelNudgeDirection::DepthUp), 16);
	request.textures = {true, false};
	auto result = prepareLevelPlacement(map, request);
	expect(result.succeeded && result.document.undoStack.size() == map.undoStack.size() + 1 && result.document.selection == QVector<LevelMapSelectionRef> {brush(2)},
		"one undo step selects the copy", result.error);
	expect(boundsOf(result.document, brush(2)) == box(0, -16, 0, 64, 16, 32) && boundsOf(result.document, brush(0)) == box(0, 0, 0, 64, 32, 32),
		"the copy moves towards the front view's viewer; the original stays", boundsOf(result.document, brush(2)));
	const auto& actions = result.document.repeat.actions;
	expect(actions.size() == 1 && actions.first().kind == LevelRepeatAction::Kind::Duplicate && actions.first().matrix[7] == -16,
		"one Repeat entry with the offset");
	auto repeated = result.document;
	QString error;
	expect(repeatLevelMapCommands(&repeated, {true, false}, &error) && boundsOf(repeated, brush(3)) == box(0, -32, 0, 64, -0, 32),
		"Repeat Last Commands builds the next copy of the array", error + boundsOf(repeated, brush(3)));
	auto undone = result.document;
	expect(undoLevelMapEdit(&undone) && serializeLevelMap(undone).bytes == original, "undo restores the exact source");

	// Doom maps copy things in the plane only.
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture", error);
	for (const auto& command : levelDuplicateMoveCommands()) {
		const auto axis = levelPlanNudgeAxis(MapRenderProjection::TopXY, command.direction);
		expect(levelNudgeAxisSupported(doom, axis) == (axis.z == 0), "Doom copies move in the plane only", command.name);
	}
}

void recording()
{
	auto map = fixture();
	const LevelMapTextureLockOptions locked {true, false};
	QString error;
	expect(map.repeat.transforms.isIdentity(), "a fresh map records nothing");
	expect(moveLevelMapSelection(&map, 16, 0, 0, locked, &error) && map.repeat.transforms.translation == std::array<double, 3> {16, 0, 0},
		"a move records its translation", error);
	LevelMapRotationRequest turn;
	turn.axis = 2;
	turn.degrees = 90;
	expect(rotateLevelMapSelection(&map, turn, &error) && map.repeat.transforms.rotationAxis == 2 && map.repeat.transforms.rotationDegrees == 90
			&& map.repeat.transforms.translation == std::array<double, 3> {16, 0, 0},
		"a turn records its rotation and keeps the translation", error);
	LevelMapVec3 mins, maxs;
	expect(levelMapSelectionBounds(map, &mins, &maxs)
			&& resizeLevelMapSelection(&map, mins, {mins.x + (maxs.x - mins.x) * 2, maxs.y, maxs.z, true}, locked, &error)
			&& map.repeat.transforms.scale == std::array<double, 3> {2, 1, 1},
		"a resize records its scale factors", error);
	expect(shearLevelMapSelection(&map, 0, 2, 0.5, {false, false}, &error) && map.repeat.transforms.skewAxis == 0 && map.repeat.transforms.skewAlong == 2
			&& map.repeat.transforms.skewAmount == 0.5,
		"a shear records its skew", error);
	expect(flipLevelMapSelection(&map, 1, locked, &error) && map.repeat.transforms.scale == std::array<double, 3> {1, -1, 1},
		"a mirror replaces the scale", error);
	const auto recorded = map.repeat.transforms;
	expect(levelRepeatTransformsDescription(recorded).size() == 4, "four recorded transforms are described",
		levelRepeatTransformsDescription(recorded).join(QStringLiteral(" | ")));
	const auto json = levelRepeatTransformsJson(recorded);
	expect(!json.value(QStringLiteral("identity")).toBool() && json.value(QStringLiteral("rotation")).toObject().value(QStringLiteral("axis")) == QStringLiteral("z")
			&& json.value(QStringLiteral("shear")).toObject().value(QStringLiteral("along")) == QStringLiteral("z"),
		"the transforms as JSON", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));

	// NetRadiant Custom keeps them across selection changes, undo and copies.
	expect(setLevelMapSelection(&map, {brush(1)}, &error) && map.repeat.transforms == recorded && map.repeat.clearOnNextAction,
		"a selection change keeps them (Repeat Last Commands starts afresh)", error);
	expect(undoLevelMapEdit(&map, &error) && map.repeat.transforms == recorded && map.repeat.actions.isEmpty(), "undo keeps them", error);
	expect(duplicateLevelMapSelection(&map, 0, 0, 0, locked, &error) && map.repeat.transforms == recorded, "a copy records nothing", error);
	expect(snapLevelMapSelectionToGrid(&map, 16, locked, &error) || !error.isEmpty(), "snap runs");
	expect(map.repeat.transforms == recorded, "snapping records nothing");

	// Component transforms record too; rounding to the grid and the replay do not.
	LevelMapComponentSet set;
	set.object = brush(0);
	set.kind = LevelBrushComponent::Vertex;
	set.components = {0};
	LevelComponentTransformRequest move;
	move.delta = {0, 0, 8, true};
	expect(transformLevelMapComponents(&map, {set}, move, nullptr, &error) && map.repeat.transforms.translation == std::array<double, 3> {0, 0, 8},
		"a component move records its translation", error);
	LevelComponentTransformRequest scale;
	scale.operation = LevelComponentTransformKind::Scale;
	scale.scale = {1, 1, 0.5, true};
	LevelMapComponentSet all = set;
	all.components = {0, 1, 2, 3, 4, 5, 6, 7};
	expect(transformLevelMapComponents(&map, {all}, scale, nullptr, &error) && map.repeat.transforms.scale == std::array<double, 3> {1, 1, 0.5},
		"a component scale records its factors", error);
	const auto before = map.repeat.transforms;
	LevelComponentTransformRequest round;
	round.grid = 4;
	transformLevelMapComponents(&map, {all}, round, nullptr, &error);
	LevelComponentTransformRequest affine;
	expect(levelRepeatTransformsComponentRequest(before, &affine, &error) && affine.operation == LevelComponentTransformKind::Affine, "an Affine request", error);
	transformLevelMapComponents(&map, {all}, affine, nullptr, &error);
	expect(map.repeat.transforms == before, "rounding and the Affine replay record nothing");

	const auto revision = map.repeat.revision;
	resetLevelMapRepeatTransforms(&map);
	expect(map.repeat.transforms.isIdentity() && map.repeat.revision > revision && !canRepeatLevelMapTransforms(map), "reset forgets them");
}

void replayObjects()
{
	auto map = fixture();
	const auto original = serializeLevelMap(map).bytes;
	map.repeat.transforms.translation = {64, 0, 0};
	map.repeat.transforms.rotationAxis = 2;
	map.repeat.transforms.rotationDegrees = 90;
	expect(canRepeatLevelMapTransforms(map), "a selection and transforms can repeat");
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::RepeatTransforms;
	request.textures = {true, false};
	auto result = prepareLevelPlacement(map, request);
	// Brush 0 turns a quarter about its centre (32,16,16), then moves 64.
	expect(result.succeeded && result.document.undoStack.size() == map.undoStack.size() + 1 && result.document.selection == QVector<LevelMapSelectionRef> {brush(2)}
			&& near(result.repeatPivot, 32, 16, 16),
		"one undo step selects the transformed copy", result.error + text(result.repeatPivot));
	expect(boundsOf(result.document, brush(2)) == box(80, -16, 0, 112, 48, 32) && boundsOf(result.document, brush(0)) == box(0, 0, 0, 64, 32, 32),
		"the copy turns about its centre and moves; the original stays", boundsOf(result.document, brush(2)));
	expect(result.document.repeat.transforms == map.repeat.transforms && result.document.repeat.actions.isEmpty(), "replaying records nothing");
	expect(result.document.undoStack.last().description.contains(QStringLiteral("Rotate")), "the history names what repeated", result.document.undoStack.last().description);
	// Again: the copy is copied, so presses build an array.
	auto again = prepareLevelPlacement(result.document, request);
	expect(again.succeeded && again.document.selection == QVector<LevelMapSelectionRef> {brush(3)} && boundsOf(again.document, brush(3)) == box(128, 0, 0, 192, 32, 32),
		"a second press continues the array", again.error + boundsOf(again.document, brush(3)));
	auto undone = result.document;
	expect(undoLevelMapEdit(&undone) && serializeLevelMap(undone).bytes == original, "undo restores the exact source");

	// An explicit pivot, and a mirror's entity heading.
	auto entityMap = fixture();
	const int entity = lastEntityId(entityMap);
	setLevelMapSelection(&entityMap, {{LevelMapSelectionKind::Entity, entity}});
	entityMap.repeat.transforms.scale = {-1, 1, 1};
	QString error;
	LevelMapVec3 pivot;
	expect(repeatLevelMapTransforms(&entityMap, {true, false}, {0, 0, 0, true}, &pivot, &error) && near(pivot, 0, 0, 0), "a mirror about the origin", error);
	const int copy = lastEntityId(entityMap);
	expect(copy != entity && property(entityMap, copy, QStringLiteral("angle")) == QStringLiteral("150") && property(entityMap, entity, QStringLiteral("angle")) == QStringLiteral("30")
			&& near(entityMap.entities.last().origin, -100, 0, 0),
		"the mirrored copy faces 180 - a and lands across the pivot", property(entityMap, copy, QStringLiteral("angle")) + QLatin1Char(' ') + text(entityMap.entities.last().origin));

	// Refusals change nothing.
	auto none = fixture();
	expect(!repeatLevelMapTransforms(&none, {}, {}, nullptr, &error) && !error.isEmpty() && serializeLevelMap(none).bytes == original, "nothing recorded, nothing repeats");
	auto lockedMap = fixture();
	lockedMap.repeat.transforms.translation = {16, 0, 0};
	QString layer;
	expect(createLevelSceneNode(&lockedMap, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&lockedMap, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&lockedMap, layer, true, &error),
		"lock fixture", error);
	const auto lockedBytes = serializeLevelMap(lockedMap).bytes;
	const auto depth = lockedMap.undoStack.size();
	expect(!repeatLevelMapTransforms(&lockedMap, {}, {}, nullptr, &error) && serializeLevelMap(lockedMap).bytes == lockedBytes && lockedMap.undoStack.size() == depth,
		"a locked selection refuses without changes", error);
	LevelMapDocument doom;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	expect(createLevelMap(doomRequest, &doom, &error), "Doom fixture", error);
	doom.repeat.transforms.translation = {16, 0, 0};
	if (!doom.doomThings.isEmpty()) { setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomThing, doom.doomThings.first().id}}); }
	expect(!canRepeatLevelMapTransforms(doom) && !repeatLevelMapTransforms(&doom, {}, {}, nullptr, &error), "Doom maps use Repeat Last Commands");
}

void replayComponents()
{
	auto map = fixture();
	LevelRepeatTransforms transforms;
	transforms.scale = {2, 1, 1};
	transforms.translation = {0, 0, 8};
	LevelComponentTransformRequest request;
	QString error;
	expect(levelRepeatTransformsComponentRequest(transforms, &request, &error), "component request", error);
	LevelTextureAffine matrix;
	expect(levelComponentTransformMatrix(request, {32, 16, 16, true}, &matrix, &error) && near(transformedLevelComponentPoint({0, 0, 0, true}, matrix), -32, 0, 8),
		"the Affine matrix scales about the pivot, then translates", error);
	LevelMapComponentSet all;
	all.object = brush(0);
	all.kind = LevelBrushComponent::Vertex;
	all.components = {0, 1, 2, 3, 4, 5, 6, 7};
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::TransformComponents;
	work.components = {all};
	work.componentTransform = request;
	const auto result = prepareLevelPlacement(map, work);
	expect(result.succeeded && result.document.undoStack.size() == map.undoStack.size() + 1 && boundsOf(result.document, brush(0)) == box(-32, 0, 8, 96, 32, 40)
			&& result.document.brushes.size() == map.brushes.size(),
		"picked components transform in place, without a copy", result.error + boundsOf(result.document, brush(0)));
	LevelComponentTransformRequest bad;
	expect(!levelRepeatTransformsComponentRequest({}, &bad, &error) && !error.isEmpty(), "identity transforms refuse");
	LevelComponentTransformRequest singular;
	singular.operation = LevelComponentTransformKind::Affine;
	singular.linear[0] = {0, 0, 0, true};
	expect(!levelComponentTransformMatrix(singular, {0, 0, 0, true}, &matrix, &error), "a singular linear part refuses");
}

void moveToCamera()
{
	auto map = fixture();
	const auto original = serializeLevelMap(map).bytes;
	LevelPastePlacement placement;
	placement.point = {100, 50, 20, true};
	placement.grid = 16;
	LevelMapVec3 offset;
	QString error;
	// Brush 0's centre is 32,16,16: 68,34,4 snaps to 64,32,0.
	expect(levelMoveToCameraOffset(map, placement, &offset, &error) && near(offset, 64, 32, 0), "a grid-snapped translation", error + text(offset));
	placement.grid = 0;
	expect(levelMoveToCameraOffset(map, placement, &offset, &error) && near(offset, 68, 34, 4), "no grid, an exact centre", error + text(offset));
	placement.grid = 16;
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::MoveToCamera;
	request.paste = placement;
	request.textures = {true, false};
	const auto result = prepareLevelPlacement(map, request);
	expect(result.succeeded && near(result.pasteOffset, 64, 32, 0) && result.document.undoStack.size() == map.undoStack.size() + 1
			&& boundsOf(result.document, brush(0)) == box(64, 32, 0, 128, 64, 32) && result.document.selection == map.selection
			&& result.document.brushes.size() == map.brushes.size() && result.document.brushes.first().id == 0,
		"one undo step moves the objects in place", result.error + boundsOf(result.document, brush(0)));
	expect(!result.document.repeat.actions.isEmpty() && result.document.repeat.actions.last().kind == LevelRepeatAction::Kind::Move
			&& result.document.repeat.transforms.translation == std::array<double, 3> {64, 32, 0},
		"Repeat records the move");
	auto undone = result.document;
	expect(undoLevelMapEdit(&undone) && serializeLevelMap(undone).bytes == original, "undo restores the exact source");
	// Point entities use their definition's box.
	auto entityMap = fixture();
	const int entity = lastEntityId(entityMap);
	setLevelMapSelection(&entityMap, {{LevelMapSelectionKind::Entity, entity}});
	placement.point = {116, 0, 4, true};
	placement.entityBounds.insert(QStringLiteral("info_null"), ResizeBox {{-8, -8, -8}, {8, 8, 8}});
	expect(levelMoveToCameraOffset(entityMap, placement, &offset, &error) && near(offset, 16, 0, 0), "a point entity centres on its origin", error + text(offset));
	// Already centred, or nothing to move: refused without changes.
	placement.point = {32, 16, 16, true};
	auto centred = fixture();
	expect(!moveLevelMapSelectionToCamera(&centred, placement, {}, nullptr, &error) && !error.isEmpty() && serializeLevelMap(centred).bytes == original,
		"an object already at the camera stays put", error);
	auto empty = fixture();
	setLevelMapSelection(&empty, {});
	expect(!levelMoveToCameraOffset(empty, placement, &offset, &error), "nothing selected, nothing moves");
	placement.point = {};
	expect(!levelMoveToCameraOffset(map, placement, &offset, &error), "no camera, no move");
}

void cli(const QString& binary, const QDir& root)
{
	const auto map = fixture();
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
	const auto savedBytes = [](const QString& path) {
		LevelMapDocument saved;
		QString error;
		expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &saved, &error), "saved map loads", error);
		return serializeLevelMap(saved).bytes;
	};
	const auto compact = [](const QJsonObject& object) { return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)).left(600); };
	QString error;

	// map nudge --duplicate: Duplicate and Move from the command line.
	const QString copied = root.filePath(QStringLiteral("copied.map"));
	const auto duplicate = run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--object"), QStringLiteral("brush:0"),
		QStringLiteral("--direction"), QStringLiteral("right"), QStringLiteral("--duplicate"), QStringLiteral("--grid"), QStringLiteral("32"),
		QStringLiteral("--output"), copied}, 0);
	expect(duplicate.value(QStringLiteral("duplicate")).toBool() && duplicate.value(QStringLiteral("copies")).toArray() == QJsonArray {QStringLiteral("brush:2")}
			&& duplicate.value(QStringLiteral("delta")).toObject().value(QStringLiteral("x")).toDouble() == 32,
		"CLI Duplicate and Move reports the copy", compact(duplicate));
	auto expectedCopy = map;
	expect(duplicateLevelMapSelection(&expectedCopy, 32, 0, 0, {true, false}, &error), "reference duplicate", error);
	expect(savedBytes(copied) == serializeLevelMap(expectedCopy).bytes, "CLI and the shared duplicate write the same map");
	run({QStringLiteral("map"), QStringLiteral("nudge"), input, QStringLiteral("--set"), QStringLiteral("brush:0/face/0"), QStringLiteral("--direction"),
		QStringLiteral("left"), QStringLiteral("--duplicate"), QStringLiteral("--output"), root.filePath(QStringLiteral("bad-copy.map"))}, 2);

	// map move --camera.
	const QString atCamera = root.filePath(QStringLiteral("camera.map"));
	const auto camera = run({QStringLiteral("map"), QStringLiteral("move"), input, QStringLiteral("--object"), QStringLiteral("brush:0"),
		QStringLiteral("--camera"), QStringLiteral("100,50,20"), QStringLiteral("--grid"), QStringLiteral("16"), QStringLiteral("--output"), atCamera}, 0);
	const auto delta = camera.value(QStringLiteral("delta")).toObject();
	expect(camera.value(QStringLiteral("placement")) == QStringLiteral("camera") && delta.value(QStringLiteral("x")).toDouble() == 64
			&& delta.value(QStringLiteral("y")).toDouble() == 32 && delta.value(QStringLiteral("z")).toDouble() == 0 && camera.value(QStringLiteral("objects")).toInt() == 1,
		"CLI Move to Camera reports the snapped translation", compact(camera));
	auto expectedMove = map;
	expect(moveLevelMapSelection(&expectedMove, 64, 32, 0, {true, false}, &error), "reference move", error);
	expect(savedBytes(atCamera) == serializeLevelMap(expectedMove).bytes, "CLI and the shared move write the same map");
	run({QStringLiteral("map"), QStringLiteral("move"), input, QStringLiteral("--camera"), QStringLiteral("0,0,0"), QStringLiteral("--output"),
		root.filePath(QStringLiteral("bad-camera.map"))}, 2);
	run({QStringLiteral("map"), QStringLiteral("move"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--camera"),
		QStringLiteral("32,16,16"), QStringLiteral("--output"), root.filePath(QStringLiteral("centred.map"))}, 4);

	// map repeat-transforms on objects and on components.
	const QString array = root.filePath(QStringLiteral("array.map"));
	const auto repeated = run({QStringLiteral("map"), QStringLiteral("repeat-transforms"), input, QStringLiteral("--object"), QStringLiteral("brush:0"),
		QStringLiteral("--rotate"), QStringLiteral("z,90"), QStringLiteral("--translate"), QStringLiteral("64,0,0"), QStringLiteral("--output"), array}, 0);
	expect(repeated.value(QStringLiteral("mode")) == QStringLiteral("objects") && repeated.value(QStringLiteral("copies")).toArray() == QJsonArray {QStringLiteral("brush:2")}
			&& repeated.value(QStringLiteral("pivot")).toObject().value(QStringLiteral("x")).toDouble() == 32
			&& repeated.value(QStringLiteral("transforms")).toObject().value(QStringLiteral("rotation")).toObject().value(QStringLiteral("degrees")).toDouble() == 90,
		"CLI Repeat Transforms reports the copy and pivot", compact(repeated));
	auto expectedArray = map;
	expectedArray.repeat.transforms.translation = {64, 0, 0};
	expectedArray.repeat.transforms.rotationAxis = 2;
	expectedArray.repeat.transforms.rotationDegrees = 90;
	expect(repeatLevelMapTransforms(&expectedArray, {true, false}, {}, nullptr, &error), "reference repeat", error);
	expect(savedBytes(array) == serializeLevelMap(expectedArray).bytes, "CLI and the shared repeat write the same map");
	const auto components = run({QStringLiteral("map"), QStringLiteral("repeat-transforms"), input, QStringLiteral("--set"),
		QStringLiteral("brush:0/vertex/0,1,2,3,4,5,6,7"), QStringLiteral("--scale"), QStringLiteral("2,1,1"), QStringLiteral("--translate"), QStringLiteral("0,0,8"),
		QStringLiteral("--output"), root.filePath(QStringLiteral("components.map"))}, 0);
	expect(components.value(QStringLiteral("mode")) == QStringLiteral("components") && components.value(QStringLiteral("brushes")).toInt() == 1,
		"CLI Repeat Transforms on components", compact(components));
	const QString bad = root.filePath(QStringLiteral("bad.map"));
	run({QStringLiteral("map"), QStringLiteral("repeat-transforms"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--output"), bad}, 2);
	run({QStringLiteral("map"), QStringLiteral("repeat-transforms"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--scale"),
		QStringLiteral("0,1,1"), QStringLiteral("--output"), bad}, 2);
	run({QStringLiteral("map"), QStringLiteral("repeat-transforms"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--set"),
		QStringLiteral("brush:0/face/0"), QStringLiteral("--translate"), QStringLiteral("8,0,0"), QStringLiteral("--output"), bad}, 2);
	run({QStringLiteral("map"), QStringLiteral("repeat-transforms"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--rotate"),
		QStringLiteral("w,90"), QStringLiteral("--output"), bad}, 2);
	expect(!QFile::exists(bad) && !QFile::exists(root.filePath(QStringLiteral("bad-copy.map"))) && !QFile::exists(root.filePath(QStringLiteral("bad-camera.map")))
			&& !QFile::exists(root.filePath(QStringLiteral("centred.map"))),
		"refusals write nothing");

	// Doom maps: Move to Camera and Repeat Transforms are Quake-family tools.
	LevelMapDocument doom;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("doom");
	const QString wad = root.filePath(QStringLiteral("doom.wad"));
	expect(createLevelMap(request, &doom, &error) && saveLevelMapAs(doom, wad).succeeded(), "Doom CLI fixture", error);
	const auto thing = doom.doomThings.isEmpty() ? QStringLiteral("thing:0") : QStringLiteral("thing:%1").arg(doom.doomThings.first().id);
	run({QStringLiteral("map"), QStringLiteral("move"), wad, QStringLiteral("--object"), thing, QStringLiteral("--camera"), QStringLiteral("64,64,0"),
		QStringLiteral("--output"), root.filePath(QStringLiteral("doom-camera.wad"))}, 4);
	run({QStringLiteral("map"), QStringLiteral("repeat-transforms"), wad, QStringLiteral("--object"), thing, QStringLiteral("--translate"), QStringLiteral("64,0,0"),
		QStringLiteral("--output"), root.filePath(QStringLiteral("doom-repeat.wad"))}, 4);
	expect(!QFile::exists(root.filePath(QStringLiteral("doom-camera.wad"))) && !QFile::exists(root.filePath(QStringLiteral("doom-repeat.wad"))),
		"refused Doom edits write nothing");
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
	profiles();
	duplicateAndMove();
	recording();
	replayObjects();
	replayComponents();
	moveToCamera();
	if (argc > 1) {
		cli(QString::fromLocal8Bit(argv[1]), QDir(temp.path()));
	}
	std::cout << (ok ? "Duplicate and Move, Move to Camera and Repeat Transforms checks passed.\n" : "Checks failed.\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
