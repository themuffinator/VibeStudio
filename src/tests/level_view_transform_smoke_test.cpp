// View-relative quick transforms: the shared frame service (core/
// level_quick_transform.h) for plan views and cameras, TrenchBroom's half-grid
// flip pivot, the pivot-aware Mirror placement the shell sends, the profile
// keys and policy, and `map view-transform`.
#include "core/editor_profiles.h"
#include "core/level_document.h"
#include "core/level_editor_controls.h"
#include "core/level_gestures.h"
#include "core/level_placement.h"
#include "core/level_quick_transform.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
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
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
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
QString describe(const LevelAxisTransform& t)
{
	return QStringLiteral("axis %1 turns %2 mirror %3").arg(t.axis).arg(t.quarterTurns).arg(t.mirror);
}
LevelViewTransform transformFor(const char* id)
{
	LevelViewTransform transform;
	expect(levelViewTransformForCommand(QString::fromLatin1(id), &transform), "known view transform", QString::fromLatin1(id));
	return transform;
}
// Brush 0 spans 4..44 on X (centre 24, off the 16-unit grid by 4); brush 1 is
// on the grid.
LevelMapDocument boxMap()
{
	LevelMapDocument created;
	LevelMapCreateRequest request;
	request.starterRoom = false;
	QString error;
	expect(createLevelMap(request, &created, &error)
			&& addLevelMapBoxBrush(&created, {4, 0, 0, true}, {44, 32, 16, true}, QStringLiteral("studio/checker"), nullptr, &error)
			&& addLevelMapBoxBrush(&created, {64, 0, 0, true}, {96, 32, 32, true}, QStringLiteral("studio/checker"), nullptr, &error),
		"box fixture", error);
	LevelMapDocument map;
	expect(loadLevelMapBytes({QStringLiteral("view.map"), {}, QStringLiteral("idtech3")}, serializeLevelMap(created).bytes, &map, &error), "box reload", error);
	setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
	return map;
}

void commandTable()
{
	const QStringList ids {QStringLiteral("map.rollClockwise"), QStringLiteral("map.rollCounterclockwise"), QStringLiteral("map.rotateRight"),
		QStringLiteral("map.rotateLeft"), QStringLiteral("map.pitchClockwise"), QStringLiteral("map.pitchCounterclockwise"),
		QStringLiteral("map.flipHorizontal"), QStringLiteral("map.flipVertical")};
	const QStringList names {QStringLiteral("roll-clockwise"), QStringLiteral("roll-counterclockwise"), QStringLiteral("yaw-clockwise"),
		QStringLiteral("yaw-counterclockwise"), QStringLiteral("pitch-clockwise"), QStringLiteral("pitch-counterclockwise"),
		QStringLiteral("flip-horizontal"), QStringLiteral("flip-vertical")};
	const auto& transforms = levelViewTransforms();
	expect(transforms.size() == 8, "eight view-relative commands");
	for (int index = 0; index < transforms.size() && index < ids.size(); ++index) {
		const auto& transform = transforms.at(index);
		LevelViewTransform byId;
		LevelViewTransform byName;
		LevelViewTransform byPalette;
		QString palette = ids.at(index);
		for (int at = palette.size() - 1; at > 0; --at) {
			if (palette.at(at).isUpper()) {
				palette.replace(at, 1, QLatin1Char('-') + palette.at(at).toLower());
			}
		}
		CommandPaletteEntry entry;
		expect(transform.commandId == ids.at(index) && transform.name == names.at(index) && levelViewTransformForCommand(ids.at(index), &byId)
				&& byId.name == transform.name && levelViewTransformForName(names.at(index).toUpper(), &byName) && byName.commandId == transform.commandId
				&& levelViewTransformForCommand(palette, &byPalette) && byPalette.commandId == transform.commandId,
			"ordered ids, names and lookups", transform.commandId + QLatin1Char(' ') + palette);
		expect(shellCommandIdExists(transform.commandId) && commandPaletteEntryForCommandId(transform.commandId, &entry) && !entry.label.isEmpty()
				&& !entry.summary.isEmpty() && entry.defaultShortcut.isEmpty(),
			"palette entry without its own key", transform.commandId);
		expect(levelViewTransformMirrors(transform) == (index >= 6), "only the flips mirror", transform.commandId);
	}
	expect(!levelViewTransformForCommand(QStringLiteral("map.rotateX")) && !levelViewTransformForName(QStringLiteral("yaw")),
		"world-axis commands and partial names are not view-relative");
	expect(levelViewTransformNames().split(QStringLiteral(", ")) == names, "usage lists every transform name");
}

void planFrames()
{
	// Per projection: the expected world axis and quarter turns of Roll CW,
	// Yaw CW (Rotate 90° Right), Pitch CW, then Flip Horizontal and Vertical.
	// The counter-clockwise twins reverse each turn.
	struct Row {
		MapRenderProjection projection;
		int rollAxis, rollTurns, yawAxis, yawTurns, pitchAxis, pitchTurns, horizontal, vertical;
	};
	const QVector<Row> rows {
		{MapRenderProjection::TopXY, 1, 1, 2, -1, 0, -1, 0, 1},
		{MapRenderProjection::FrontXZ, 2, 1, 1, 1, 0, -1, 0, 2},
		{MapRenderProjection::SideZY, 2, 1, 0, -1, 1, -1, 1, 2},
	};
	for (const Row& row : rows) {
		const QString label = QString::number(static_cast<int>(row.projection));
		const auto resolve = [&](const char* id) {
			LevelAxisTransform resolved;
			expect(levelPlanViewTransform(row.projection, transformFor(id), &resolved), "plan views always resolve", label + QString::fromLatin1(id));
			return resolved;
		};
		const auto turn = [&](const char* id, int axis, int turns) {
			const auto resolved = resolve(id);
			expect(!resolved.mirror && resolved.axis == axis && resolved.quarterTurns == turns && resolved.commandId == QString::fromLatin1(id),
				"plan turn axis and direction", label + QLatin1Char(' ') + QString::fromLatin1(id) + QLatin1Char(' ') + describe(resolved));
		};
		turn("map.rollClockwise", row.rollAxis, row.rollTurns);
		turn("map.rollCounterclockwise", row.rollAxis, -row.rollTurns);
		turn("map.rotateRight", row.yawAxis, row.yawTurns);
		turn("map.rotateLeft", row.yawAxis, -row.yawTurns);
		turn("map.pitchClockwise", row.pitchAxis, row.pitchTurns);
		turn("map.pitchCounterclockwise", row.pitchAxis, -row.pitchTurns);
		const auto horizontal = resolve("map.flipHorizontal");
		const auto vertical = resolve("map.flipVertical");
		expect(horizontal.mirror && horizontal.axis == row.horizontal && horizontal.quarterTurns == 0 && vertical.mirror && vertical.axis == row.vertical,
			"plan flips mirror along the screen axes", label + QLatin1Char(' ') + describe(horizontal) + QLatin1Char('/') + describe(vertical));
	}
	// Rotate 90° Right is the shell's old clockwise quarter turn in each view:
	// -90 about Z from the top, +90 about Y from the front, -90 about X from the side.
	LevelAxisTransform resolved;
	expect(levelPlanViewTransform(MapRenderProjection::FrontXZ, transformFor("map.rotateRight"), &resolved) && resolved.axis == 1 && resolved.quarterTurns == 1,
		"front-view Yaw keeps the established clockwise turn", describe(resolved));
}

void cameraFrames()
{
	const auto resolve = [](double yaw, double pitch, const char* id) {
		LevelAxisTransform resolved;
		expect(levelCameraViewTransform(yaw, pitch, transformFor(id), &resolved), "camera resolves", QString::fromLatin1(id));
		return resolved;
	};
	// Looking nearest +Y (60 degrees of yaw): forward +Y, right +X, up +Z.
	auto r = resolve(60, -20, "map.rollClockwise");
	expect(r.axis == 1 && r.quarterTurns == 1, "camera Roll turns about its nearest horizontal axis", describe(r));
	r = resolve(60, -20, "map.pitchClockwise");
	expect(r.axis == 0 && r.quarterTurns == -1, "camera Pitch turns about its snapped right axis", describe(r));
	r = resolve(60, -20, "map.rotateRight");
	expect(r.axis == 2 && r.quarterTurns == -1, "camera Yaw turns about world Z, clockwise from above", describe(r));
	r = resolve(60, -20, "map.rotateLeft");
	expect(r.axis == 2 && r.quarterTurns == 1, "camera Yaw counter-clockwise", describe(r));
	r = resolve(60, -20, "map.flipHorizontal");
	expect(r.mirror && r.axis == 0, "camera Flip Horizontal mirrors along its right axis", describe(r));
	r = resolve(60, -20, "map.flipVertical");
	expect(r.mirror && r.axis == 2, "camera Flip Vertical mirrors along world Z", describe(r));
	// Looking along -X: forward -X, right +Y.
	r = resolve(180, 0, "map.rollClockwise");
	expect(r.axis == 0 && r.quarterTurns == -1, "Roll follows the camera's heading", describe(r));
	r = resolve(180, 0, "map.flipHorizontal");
	expect(r.mirror && r.axis == 1, "Flip Horizontal follows the camera's right axis", describe(r));
	// Straight down, forward is up the picture (+Y for a heading of +Y).
	r = resolve(90, -90, "map.rollCounterclockwise");
	expect(r.axis == 1 && r.quarterTurns == -1, "looking straight down rolls about the picture's up axis", describe(r));
	// A rolled camera whose right snaps onto forward turns it a quarter about Z.
	LevelAxisTransform rolled;
	expect(levelCameraViewTransform({1, 0, 0, true}, {0.9, -0.1, 0, true}, {0, 0, 1, true}, transformFor("map.pitchClockwise"), &rolled)
			&& rolled.axis == 1 && rolled.quarterTurns == 1,
		"pitch never turns about the forward axis", describe(rolled));
	LevelAxisTransform none;
	expect(!levelCameraViewTransform({0, 0, 0, true}, {0, -1, 0, true}, {0, 0, 1, true}, transformFor("map.rollClockwise"), &none)
			&& !levelCameraViewTransform(std::nan(""), 0, transformFor("map.rollClockwise"), &none),
		"a degenerate camera has no frame");
	// Turn vectors for the sweep destination.
	expect(same(levelAxisTransformTurnVector({QString(), false, 2, -1}), 0, 0, -1) && same(levelAxisTransformTurnVector({QString(), false, 0, 1}), 1, 0, 0)
			&& !levelAxisTransformTurnVector({QString(), true, 1, 0}).valid,
		"turn vectors carry the turn's sign; mirrors have none");
}

void pivotsAndSupport()
{
	// Centre 24 on X: half of a 16-unit grid keeps it, the full grid rounds to 32.
	const LevelMapVec3 mins {4, 0, 0, true};
	const LevelMapVec3 maxs {44, 32, 16, true};
	expect(same(levelFlipPivot(mins, maxs, 16), 24, 16, 8), "the half-grid flip pivot keeps an odd centre", text(levelFlipPivot(mins, maxs, 16)));
	expect(same(levelQuickTurnPivot(mins, maxs, 16), 32, 16, 16), "the quick-turn pivot rounds to the full grid", text(levelQuickTurnPivot(mins, maxs, 16)));
	expect(same(levelFlipPivot({2, 0, 0, true}, {36, 16, 16, true}, 16), 16, 8, 8), "an off-grid centre rounds to half the grid",
		text(levelFlipPivot({2, 0, 0, true}, {36, 16, 16, true}, 16)));
	expect(same(levelFlipPivot({2, 0, 0, true}, {36, 16, 16, true}, 0), 19, 8, 8) && same(levelFlipPivot({2, 0, 0, true}, {36, 16, 16, true}, std::nan("")), 19, 8, 8),
		"no grid keeps the exact centre");
	const auto map = boxMap();
	LevelMapVec3 pivot;
	QString error;
	expect(levelMapFlipPivot(map, 16, &pivot) && same(pivot, 24, 16, 8), "the selection's flip pivot", text(pivot));
	auto empty = map;
	setLevelMapSelection(&empty, {});
	expect(!levelMapFlipPivot(empty, 16, &pivot, &error) && !error.isEmpty(), "nothing selected, no pivot", error);

	LevelMapDocument doom;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("doom");
	expect(createLevelMap(request, &doom, &error), "Doom fixture", error);
	for (const auto& transform : levelViewTransforms()) {
		LevelAxisTransform resolved;
		levelPlanViewTransform(MapRenderProjection::TopXY, transform, &resolved);
		const bool planar = transform.kind == LevelViewTransformKind::Roll || transform.kind == LevelViewTransformKind::Pitch;
		expect(levelAxisTransformSupported(map, resolved) && levelAxisTransformSupported(doom, resolved) == !planar,
			"Doom maps take top-view yaw and flips, never roll or pitch", transform.name);
	}
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
	const QVector<QPair<const char*, QString>> tbKeys {{"map.rollClockwise", QStringLiteral("Alt+Up")}, {"map.rollCounterclockwise", QStringLiteral("Alt+Down")},
		{"map.rotateRight", QStringLiteral("Alt+Left")}, {"map.rotateLeft", QStringLiteral("Alt+Right")}, {"map.pitchClockwise", QStringLiteral("Alt+PgUp")},
		{"map.pitchCounterclockwise", QStringLiteral("Alt+PgDown")}, {"map.flipHorizontal", QStringLiteral("Ctrl+F")},
		{"map.flipVertical", QStringLiteral("Ctrl+Alt+F")}};
	for (const auto& key : tbKeys) {
		expect(keysOf(tb, key.first) == QStringList {key.second}, "TrenchBroom's view-relative key", QString::fromLatin1(key.first));
		expect(keysOf(netRadiantCustomLevelControls(), key.first).isEmpty() && keysOf(vibeStudioLevelControls(), key.first).isEmpty(),
			"NetRadiant Custom and VibeStudio leave them unbound", QString::fromLatin1(key.first));
	}
	expect(tb.flipSnapsPivot && !netRadiantCustomLevelControls().flipSnapsPivot && !vibeStudioLevelControls().flipSnapsPivot,
		"only TrenchBroom rounds the flip pivot to half the grid");
	for (const auto& profile : editorProfileDescriptors()) {
		expect(profile.controls.flipSnapsPivot == (profile.id == QLatin1String("trenchbroom")), "the half-grid flip pivot is TrenchBroom's", profile.id);
		expect(levelGestureValues(profile.controls).value(QStringLiteral("editing.flipSnapsPivot")).toString()
				== (profile.controls.flipSnapsPivot ? QStringLiteral("on") : QStringLiteral("off")),
			"the flip pivot is a gesture field", profile.id);
		expect(levelEditorControlProblems(profile.controls).isEmpty(), "view-relative keys keep every profile consistent",
			profile.id + QLatin1Char(' ') + levelEditorControlProblems(profile.controls).join(QLatin1Char(';')));
		QString conflict;
		expect(!editorProfileHasShortcutConflict(profile, &conflict), "no profile key conflict", profile.id + QLatin1Char(' ') + conflict);
		for (const EditorProfileBinding& binding : profile.bindings) {
			if (levelViewTransformForCommand(binding.commandId)) {
				expect(binding.implemented, "view-relative bindings are implemented shell commands", profile.id + QLatin1Char(' ') + binding.commandId);
			}
		}
	}
	LevelEditorControls effective;
	QJsonObject normalized;
	QString error;
	expect(applyLevelGestureOverrides(vibeStudioLevelControls(), {{QStringLiteral("editing.flipSnapsPivot"), QStringLiteral("on")}}, &effective, &normalized, &error)
			&& effective.flipSnapsPivot && normalized.value(QStringLiteral("editing.flipSnapsPivot")) == QStringLiteral("on"),
		"any profile can round its flip pivot", error);
	bool listed = false;
	for (const auto& row : levelEditorControlRows(tb)) {
		listed = listed || (row.section == QLatin1String("layout") && row.gesture.contains(QStringLiteral("half")));
	}
	expect(listed, "the Controls reference names the flip pivot");
}

// The edits the shell sends: a flip about a chosen pivot, one undo step,
// exact undo, and refusal of bad pivots and locked objects.
void placements()
{
	auto map = boxMap();
	const auto original = serializeLevelMap(map).bytes;
	QString error;
	LevelPlacementRequest flip;
	flip.operation = LevelPlacementOperation::Mirror;
	flip.axis = 0;
	flip.textures = {true, false};
	auto plain = prepareLevelPlacement(map, flip);
	auto direct = map;
	expect(plain.succeeded && flipLevelMapSelection(&direct, 0, flip.textures, &error) && serializeLevelMap(direct).bytes == serializeLevelMap(plain.document).bytes,
		"without a pivot the Mirror placement is the established flip", plain.error + error);
	flip.mirrorPivot = {32, 0, 0, true};
	auto pivoted = prepareLevelPlacement(map, flip);
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	expect(pivoted.succeeded && pivoted.document.undoStack.size() == 1 && levelMapSelectionBounds(pivoted.document, &mins, &maxs) && same(mins, 20, 0, 0)
			&& same(maxs, 60, 32, 16),
		"the mirror plane passes through the pivot", pivoted.error + text(mins) + text(maxs));
	auto reference = map;
	expect(flipLevelMapSelection(&reference, 0, flip.textures, flip.mirrorPivot, &error) && serializeLevelMap(reference).bytes == serializeLevelMap(pivoted.document).bytes,
		"the placement and the core flip agree", error);
	const auto& repeated = pivoted.document.repeat.actions;
	expect(!repeated.isEmpty() && repeated.last().mirror && repeated.last().matrix[3] == 64, "Repeat records the pivoted mirror",
		repeated.isEmpty() ? QString() : QString::number(repeated.last().matrix[3]));
	auto undone = pivoted.document;
	expect(undoLevelMapEdit(&undone) && serializeLevelMap(undone).bytes == original, "undo restores the exact source");
	flip.mirrorPivot = {std::nan(""), 0, 0, true};
	auto bad = prepareLevelPlacement(map, flip);
	expect(!bad.succeeded && !bad.error.isEmpty() && serializeLevelMap(map).bytes == original, "a non-finite pivot is refused", bad.error);

	QString layer;
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&map, layer, true, &error),
		"lock fixture", error);
	const auto locked = serializeLevelMap(map).bytes;
	const auto depth = map.undoStack.size();
	flip.mirrorPivot = {24, 0, 0, true};
	const auto refused = prepareLevelPlacement(map, flip);
	expect(!refused.succeeded && !refused.error.isEmpty() && serializeLevelMap(map).bytes == locked && map.undoStack.size() == depth,
		"a locked selection refuses the flip without changes", refused.error);
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
	const auto savedBytes = [](const QString& path) {
		LevelMapDocument saved;
		QString error;
		expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &saved, &error), "saved map loads", error);
		return serializeLevelMap(saved).bytes;
	};
	const auto reloaded = [&] {
		LevelMapDocument document;
		QString error;
		expect(loadLevelMap({input, {}, QStringLiteral("idtech3")}, &document, &error), "CLI input loads", error);
		return document;
	};
	const auto view = [](const QString& name) { return QStringList {QStringLiteral("map"), QStringLiteral("view-transform")} << name; };

	// Roll Clockwise from the front view turns +90 about Z, about the grid-
	// rounded centre with --pivot-grid.
	const QString rolled = root.filePath(QStringLiteral("rolled.map"));
	const auto roll = run(view(input) << QStringLiteral("--object") << QStringLiteral("brush:0") << QStringLiteral("--transform")
			<< QStringLiteral("roll-clockwise") << QStringLiteral("--view") << QStringLiteral("front") << QStringLiteral("--pivot-grid")
			<< QStringLiteral("16") << QStringLiteral("--output") << rolled,
		0);
	expect(roll.value(QStringLiteral("transform")) == QStringLiteral("roll-clockwise") && roll.value(QStringLiteral("commandId")) == QStringLiteral("map.rollClockwise")
			&& roll.value(QStringLiteral("frame")) == QStringLiteral("front") && roll.value(QStringLiteral("axis")) == QStringLiteral("z")
			&& roll.value(QStringLiteral("degrees")).toInt() == 90 && roll.value(QStringLiteral("pivotPolicy")) == QStringLiteral("grid")
			&& roll.value(QStringLiteral("objects")).toInt() == 1,
		"CLI reports the front view's roll", QString::fromUtf8(QJsonDocument(roll).toJson(QJsonDocument::Compact)).left(500));
	{
		auto expected = reloaded();
		QString error;
		LevelMapRotationRequest rotation;
		rotation.axis = 2;
		rotation.degrees = 90;
		expect(setLevelMapSelection(&expected, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error) && levelMapQuickTurnPivot(expected, 16, &rotation.pivot, &error)
				&& rotateLevelMapSelection(&expected, rotation, &error),
			"reference roll", error);
		expect(savedBytes(rolled) == serializeLevelMap(expected).bytes, "CLI and the shared rotation write the same map");
	}

	// Flip Horizontal from a camera looking nearest +Y mirrors along X, about
	// the half-grid centre.
	const QString flipped = root.filePath(QStringLiteral("flipped.map"));
	const auto flip = run(view(input) << QStringLiteral("--object") << QStringLiteral("brush:0") << QStringLiteral("--transform")
			<< QStringLiteral("flip-horizontal") << QStringLiteral("--camera-yaw") << QStringLiteral("60") << QStringLiteral("--camera-pitch")
			<< QStringLiteral("-20") << QStringLiteral("--pivot-grid") << QStringLiteral("16") << QStringLiteral("--output") << flipped,
		0);
	expect(flip.value(QStringLiteral("frame")) == QStringLiteral("camera") && flip.value(QStringLiteral("axis")) == QStringLiteral("x")
			&& flip.value(QStringLiteral("mirror")).toBool() && flip.value(QStringLiteral("pivotPolicy")) == QStringLiteral("half-grid")
			&& flip.value(QStringLiteral("pivot")).toObject().value(QStringLiteral("x")).toDouble() == 24,
		"CLI camera flip", QString::fromUtf8(QJsonDocument(flip).toJson(QJsonDocument::Compact)).left(500));
	{
		auto expected = reloaded();
		QString error;
		expect(setLevelMapSelection(&expected, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error)
				&& flipLevelMapSelection(&expected, 0, {true, false}, LevelMapVec3 {24, 16, 8, true}, &error),
			"reference flip", error);
		expect(savedBytes(flipped) == serializeLevelMap(expected).bytes, "CLI and the shared flip write the same map");
	}

	// Yaw from the side view: -90 about X; texture lock off is reported.
	const QString yawed = root.filePath(QStringLiteral("yawed.map"));
	const auto yaw = run(view(input) << QStringLiteral("--object") << QStringLiteral("brush:1") << QStringLiteral("--transform")
			<< QStringLiteral("yaw-clockwise") << QStringLiteral("--view") << QStringLiteral("side") << QStringLiteral("--texture-lock")
			<< QStringLiteral("off") << QStringLiteral("--output") << yawed,
		0);
	expect(yaw.value(QStringLiteral("axis")) == QStringLiteral("x") && yaw.value(QStringLiteral("degrees")).toInt() == -90
			&& yaw.value(QStringLiteral("commandId")) == QStringLiteral("map.rotateRight") && yaw.value(QStringLiteral("pivotPolicy")) == QStringLiteral("centre")
			&& yaw.value(QStringLiteral("textureLockPolicy")) == QStringLiteral("source-parameters"),
		"CLI side-view yaw", QString::fromUtf8(QJsonDocument(yaw).toJson(QJsonDocument::Compact)).left(500));

	// Usage errors write nothing.
	const QString bad = root.filePath(QStringLiteral("bad.map"));
	run(view(input) << QStringLiteral("--object") << QStringLiteral("brush:0") << QStringLiteral("--transform") << QStringLiteral("spin")
			<< QStringLiteral("--output") << bad,
		2);
	run(view(input) << QStringLiteral("--object") << QStringLiteral("brush:0") << QStringLiteral("--transform") << QStringLiteral("roll-clockwise")
			<< QStringLiteral("--view") << QStringLiteral("top") << QStringLiteral("--camera-yaw") << QStringLiteral("0") << QStringLiteral("--output") << bad,
		2);
	run(view(input) << QStringLiteral("--object") << QStringLiteral("brush:0") << QStringLiteral("--transform") << QStringLiteral("flip-vertical")
			<< QStringLiteral("--pivot") << QStringLiteral("0,0,0") << QStringLiteral("--pivot-grid") << QStringLiteral("16") << QStringLiteral("--output") << bad,
		2);
	run(view(input) << QStringLiteral("--transform") << QStringLiteral("flip-vertical") << QStringLiteral("--output") << bad, 2);
	expect(!QFile::exists(bad), "usage errors write nothing");

	// Doom maps refuse roll and pitch, and take yaw in the top view.
	LevelMapDocument doom;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("doom");
	QString error;
	const QString wad = root.filePath(QStringLiteral("doom.wad"));
	expect(createLevelMap(request, &doom, &error) && saveLevelMapAs(doom, wad).succeeded() && !doom.doomThings.isEmpty(), "Doom CLI fixture", error);
	const QString thing = QStringLiteral("thing:%1").arg(doom.doomThings.isEmpty() ? 0 : doom.doomThings.first().id);
	run(view(wad) << QStringLiteral("--object") << thing << QStringLiteral("--transform") << QStringLiteral("roll-clockwise") << QStringLiteral("--output")
			<< root.filePath(QStringLiteral("doom-roll.wad")),
		4);
	expect(!QFile::exists(root.filePath(QStringLiteral("doom-roll.wad"))), "a refused Doom roll writes nothing");
	run(view(wad) << QStringLiteral("--object") << thing << QStringLiteral("--transform") << QStringLiteral("yaw-counterclockwise") << QStringLiteral("--output")
			<< root.filePath(QStringLiteral("doom-yaw.wad")),
		0);
	expect(QFile::exists(root.filePath(QStringLiteral("doom-yaw.wad"))), "a Doom yaw writes the turned map");
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
	planFrames();
	cameraFrames();
	pivotsAndSupport();
	profiles();
	placements();
	if (argc > 1) {
		cli(QString::fromLocal8Bit(argv[1]), QDir(temp.path()));
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
