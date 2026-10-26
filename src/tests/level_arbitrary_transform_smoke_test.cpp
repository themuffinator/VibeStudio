// Arbitrary Rotation (Euler XYZ) and Arbitrary Scale (factors, negative
// factors mirror): core services, Repeat Transforms, scene locks, hidden
// objects, Doom limits and the `map rotate --euler` / `map resize --factors`
// CLI routes.
#include "core/level_document.h"
#include "core/level_placement.h"
#include "core/level_repeat.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "core/map_geometry.h"
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
namespace
{
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(error) << std::endl;
	}
	return value;
}
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity, id}; }

// Two brushes and a model with pitch, yaw and roll, in a Valve 220 map so
// locked textures never need a dialect change.
LevelMapDocument fixture()
{
	LevelMapDocument map;
	QString error;
	const QByteArray text = "// fixture\n{\n\"classname\" \"worldspawn\"\n"
		"\"mapversion\" \"220\"\n}\n";
	expect(loadLevelMapBytes({QStringLiteral("arbitrary.map"), {}, QStringLiteral("idTech2")}, text, &map, &error), "load fixture", error);
	expect(addLevelMapBoxBrush(&map, {-32, -16, 0, true}, {32, 16, 48, true}, QStringLiteral("studio/checker"), nullptr, &error)
		&& addLevelMapBoxBrush(&map, {64, 0, 0, true}, {96, 32, 32, true}, QStringLiteral("studio/stone"), nullptr, &error)
		&& addLevelMapEntity(&map, QStringLiteral("misc_model"), {16, 8, 24, true}, {{QStringLiteral("angles"), QStringLiteral("20 30 40")}},
			nullptr, &error), "fixture objects", error);
	return map;
}
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
LevelMapRotationRequest euler(double x, double y, double z, LevelMapVec3 pivot = {})
{
	LevelMapRotationRequest request;
	request.euler = true;
	request.eulerDegrees = {x, y, z};
	request.pivot = pivot;
	request.textureLock = true;
	request.allowValve220 = true;
	return request;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
bool sameGeometry(const LevelMapBrush& a, const LevelMapBrush& b)
{
	return near(a.mins.x, b.mins.x) && near(a.mins.y, b.mins.y) && near(a.mins.z, b.mins.z) && near(a.maxs.x, b.maxs.x)
		&& near(a.maxs.y, b.maxs.y) && near(a.maxs.z, b.maxs.z);
}

void rotation()
{
	// Euler XYZ equals turning about X, then Y, then Z about one pivot.
	auto map = fixture();
	setLevelMapSelection(&map, {brush(0), brush(1), entity(1)});
	const auto original = bytes(map);
	const auto undoDepth = map.undoStack.size();
	auto stepwise = map;
	QString error;
	const LevelMapVec3 pivot{8, 4, 16, true};
	expect(rotateLevelMapSelection(&map, euler(30, 15, 45, pivot), &error), "Euler rotation", error);
	expect(map.undoStack.size() == undoDepth + 1, "Euler rotation is one undo step");
	for (int axis = 0; axis < 3; ++axis) {
		LevelMapRotationRequest single;
		single.axis = axis;
		single.degrees = std::array<double, 3>{30, 15, 45}[axis];
		single.pivot = pivot;
		single.allowValve220 = true;
		expect(rotateLevelMapSelection(&stepwise, single, &error), "stepwise rotation", error);
	}
	expect(bytes(map) == bytes(stepwise), "Euler XYZ matches X, then Y, then Z about the same pivot");
	const auto rotated = bytes(map);
	expect(undoLevelMapEdit(&map, &error) && bytes(map) == original, "one undo restores the map", error);
	expect(redoLevelMapEdit(&map, &error) && bytes(map) == rotated, "one redo replays all three turns", error);

	// Without a pivot every turn keeps the first turn's centre.
	map = fixture();
	setLevelMapSelection(&map, {brush(0), brush(1)});
	LevelMapVec3 low, high;
	levelMapSelectionBounds(map, &low, &high);
	auto explicitPivot = map;
	expect(rotateLevelMapSelection(&map, euler(90, 0, 90), &error)
		&& rotateLevelMapSelection(&explicitPivot, euler(90, 0, 90, {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2, true}), &error)
		&& bytes(map) == bytes(explicitPivot), "default pivot is the selection centre before the first turn", error);

	// One nonzero angle is exactly the single-axis service.
	map = fixture();
	setLevelMapSelection(&map, {brush(0)});
	auto single = map;
	LevelMapRotationRequest about;
	about.axis = 2;
	about.degrees = 37.5;
	about.allowValve220 = true;
	expect(rotateLevelMapSelection(&map, euler(0, 360, 37.5), &error) && rotateLevelMapSelection(&single, about, &error)
		&& bytes(map) == bytes(single) && map.undoStack.last().description == single.undoStack.last().description,
		"a single Euler angle is the single-axis rotation", error);
	expect(!rotateLevelMapSelection(&map, euler(0, 720, -360), &error) && error.contains(QStringLiteral("full turns")),
		"whole turns change nothing", error);
	expect(!rotateLevelMapSelection(&map, euler(std::nan(""), 10, 0), &error), "non-finite angle refused");

	// Repeat Transforms repeats the whole Euler turn.
	map = fixture();
	setLevelMapSelection(&map, {brush(0)});
	expect(rotateLevelMapSelection(&map, euler(90, 0, 90), &error), "rotation before repeat", error);
	expect(map.repeat.transforms.rotationEulerXyz && map.repeat.transforms.rotationEuler == std::array<double, 3>{90, 0, 90},
		"Repeat Transforms records the Euler turn");
	expect(levelRepeatTransformsDescription(map.repeat.transforms).join(QLatin1Char(' ')).contains(QStringLiteral("about X")),
		"recorded turn is described");
	auto again = map;
	expect(rotateLevelMapSelection(&again, euler(90, 0, 90), &error), "second rotation", error);
	const auto depth = map.undoStack.size();
	expect(repeatLevelMapTransforms(&map, {true, false}, {}, nullptr, &error) && map.undoStack.size() == depth + 1
		&& map.brushes.size() == 3 && sameGeometry(map.brushes.last(), again.brushes.first()),
		"Repeat Transforms copies and turns by X, then Z", error);
	LevelMapRotationRequest plain;
	plain.axis = 1;
	plain.degrees = 45;
	plain.textureLock = false;
	expect(rotateLevelMapSelection(&map, plain, &error) && !map.repeat.transforms.rotationEulerXyz && map.repeat.transforms.rotationAxis == 1,
		"a single-axis turn replaces the recorded Euler turn", error);

	// Scene locks refuse atomically.
	map = fixture();
	QString group;
	expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &group, &error)
		&& assignLevelSceneObjects(&map, group, {QStringLiteral("brush:1")}, &error) && setLevelSceneLocked(&map, group, true, &error),
		"lock fixture", error);
	setLevelMapSelection(&map, {brush(0), brush(1)});
	const auto locked = bytes(map);
	const auto lockedDepth = map.undoStack.size();
	expect(!rotateLevelMapSelection(&map, euler(30, 0, 45), &error) && bytes(map) == locked && map.undoStack.size() == lockedDepth,
		"a locked object refuses the whole Euler rotation", error);
	LevelMapScaleRequest scaleLocked;
	scaleLocked.factors = {2, 2, 2};
	expect(!scaleLevelMapSelection(&map, scaleLocked, &error) && bytes(map) == locked, "a locked object refuses scaling", error);

	// Doom maps turn about Z only.
	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	expect(createLevelMap(create, &doom, &error), "Doom fixture", error);
	setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomThing, doom.doomThings.first().id}});
	expect(!rotateLevelMapSelection(&doom, euler(10, 0, 90), &error) && error.contains(QStringLiteral("Z only")), "Doom refuses X and Y turns", error);
	expect(rotateLevelMapSelection(&doom, euler(0, 0, 90), &error) && doom.doomThings.first().angle == 180, "Doom Z turn turns the thing", error);
}

void scaling()
{
	QString error;
	auto map = fixture();
	setLevelMapSelection(&map, {brush(0)});
	const auto original = bytes(map);
	const auto depth = map.undoStack.size();
	LevelMapScaleRequest request;
	request.factors = {2, 0.5, 1};
	expect(scaleLevelMapSelection(&map, request, &error), "scale about the centre", error);
	const auto& scaled = map.brushes.first();
	expect(near(scaled.mins.x, -64) && near(scaled.maxs.x, 64) && near(scaled.mins.y, -8) && near(scaled.maxs.y, 8) && near(scaled.mins.z, 0)
		&& near(scaled.maxs.z, 48) && map.undoStack.size() == depth + 1, "factors about the centre, one undo step");
	expect(undoLevelMapEdit(&map, &error) && bytes(map) == original, "scale undo", error);

	// About a pivot.
	request.pivot = {32, 16, 0, true};
	expect(scaleLevelMapSelection(&map, request, &error) && near(map.brushes.first().mins.x, -96) && near(map.brushes.first().maxs.x, 32)
		&& near(map.brushes.first().mins.y, 0) && near(map.brushes.first().maxs.y, 16), "factors about a pivot", error);

	// A negative factor is Flip: the same geometry, faces wound outward.
	for (int axis = 0; axis < 3; ++axis) {
		auto scaledMap = fixture();
		setLevelMapSelection(&scaledMap, {brush(0), entity(1)});
		auto flipped = scaledMap;
		LevelMapScaleRequest mirror;
		mirror.factors = {axis == 0 ? -1.0 : 1.0, axis == 1 ? -1.0 : 1.0, axis == 2 ? -1.0 : 1.0};
		mirror.textures = {true, false};
		expect(scaleLevelMapSelection(&scaledMap, mirror, &error) && flipLevelMapSelection(&flipped, axis, {true, false}, &error)
			&& bytes(scaledMap) == bytes(flipped), "a -1 factor matches Flip on the same axis", error);
	}
	map = fixture();
	setLevelMapSelection(&map, {brush(1)});
	request = {};
	request.factors = {-1, 2, 1};
	request.textures = {true, false};
	expect(scaleLevelMapSelection(&map, request, &error), "mirror and stretch", error);
	const auto solved = solveBrushGeometry(map.brushes[1].faces);
	expect(solved.solved && near(map.brushes[1].mins.x, 64) && near(map.brushes[1].maxs.x, 96) && near(map.brushes[1].mins.y, -16)
		&& near(map.brushes[1].maxs.y, 48), "mirrored faces still enclose the brush");
	expect(map.repeat.transforms.scale == std::array<double, 3>{-1, 2, 1}, "Repeat Transforms records the factors");

	// Refusals change nothing.
	map = fixture();
	setLevelMapSelection(&map, {brush(0)});
	const auto before = bytes(map);
	for (const auto& factors : {std::array<double, 3>{0, 1, 1}, std::array<double, 3>{1, 1, 1}, std::array<double, 3>{1, std::nan(""), 1},
			 std::array<double, 3>{1, 1, 40000}}) {
		request = {};
		request.factors = factors;
		expect(!scaleLevelMapSelection(&map, request, &error) && !error.isEmpty() && bytes(map) == before, "invalid factors are refused");
	}

	// Hiding drops objects from the selection, and Select All, as the views
	// run it, leaves hidden objects out, so they stay where they are.
	map = fixture();
	setLevelMapSelection(&map, {brush(1)});
	LevelVisibilityRequest hide;
	expect(changeLevelMapVisibility(&map, hide, nullptr, &error) && map.selection.isEmpty(), "hide brush 1 and drop it from the selection", error);
	const auto hiddenBrush = map.brushes[1];
	const auto hidden = levelMapHiddenObjects(map);
	setLevelMapSelection(&map, levelMapSelectAllObjects(map, false, [&hidden](const LevelMapSelectionRef& ref) {
		return !hidden.contains(levelMapSelectionRefId(ref));
	}));
	request = {};
	request.factors = {2, 2, 2};
	expect(!map.selection.contains(brush(1)) && map.selection.contains(brush(0)), "Select All leaves the hidden brush out");
	expect(scaleLevelMapSelection(&map, request, &error) && sameGeometry(map.brushes[1], hiddenBrush), "hidden objects are not scaled", error);

	// The placement service shares the scale.
	map = fixture();
	setLevelMapSelection(&map, {brush(0)});
	LevelPlacementRequest placement;
	placement.operation = LevelPlacementOperation::Scale;
	placement.scale.factors = {2, 2, 2};
	const auto prepared = prepareLevelPlacement(map, placement);
	auto direct = map;
	expect(prepared.succeeded && scaleLevelMapSelection(&direct, placement.scale, &error) && bytes(prepared.document) == bytes(direct)
		&& bytes(map) == before, "placement Scale matches the service and leaves the source", prepared.error);

	// Doom: the top view only; mirrors reflect thing headings.
	LevelMapDocument doom;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("doom");
	expect(createLevelMap(create, &doom, &error), "Doom fixture", error);
	setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomThing, doom.doomThings.first().id}});
	request = {};
	request.factors = {1, 1, 2};
	expect(!scaleLevelMapSelection(&doom, request, &error) && error.contains(QStringLiteral("Z factor")), "Doom refuses a Z factor", error);
	request.factors = {1, -1, 1};
	expect(scaleLevelMapSelection(&doom, request, &error) && doom.doomThings.first().angle == 270, "Doom mirror reflects the heading", error);
	setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomSector, 0}});
	request.factors = {0.5, 0.5, 1};
	expect(scaleLevelMapSelection(&doom, request, &error), "Doom sector scales", error);
	double extent = 0;
	for (const auto& vertex : doom.doomVertices) {
		extent = std::max({extent, std::abs(vertex.x), std::abs(vertex.y)});
	}
	expect(near(extent, 128), "Doom vertices halve about the sector centre");
	request.factors = {-1, 1, 1};
	expect(scaleLevelMapSelection(&doom, request, &error), "Doom sector mirrors with linedef repair", error);
}

QString lastOutput;
int runCli(const QString& binary, const QStringList& arguments, QJsonObject* json)
{
	QProcess process;
	process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("map")} + arguments + QStringList{QStringLiteral("--json")});
	if (!process.waitForFinished(60000)) {
		process.kill();
		process.waitForFinished();
		return -1;
	}
	const auto output = process.readAllStandardOutput();
	lastOutput = QString::fromUtf8(output + process.readAllStandardError());
	if (json) {
		*json = QJsonDocument::fromJson(output).object();
	}
	return process.exitCode();
}

void cli(const QString& binary)
{
	QTemporaryDir temp;
	expect(temp.isValid(), "temporary folder");
	QString error;
	auto map = fixture();
	const QString input = QDir(temp.path()).filePath(QStringLiteral("in.map"));
	QFile file(input);
	expect(file.open(QIODevice::WriteOnly) && file.write(bytes(map)) > 0, "write CLI input");
	file.close();
	LevelMapDocument loaded;
	expect(loadLevelMap({input, {}, QStringLiteral("idTech2")}, &loaded, &error), "load CLI input", error);
	const QString rotatedPath = QDir(temp.path()).filePath(QStringLiteral("rotated.map"));
	QJsonObject json;
	expect(runCli(binary, {QStringLiteral("rotate"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--euler"),
		QStringLiteral("30,15,45"), QStringLiteral("--pivot"), QStringLiteral("8,4,16"), QStringLiteral("--allow-valve220"), QStringLiteral("--output"), rotatedPath}, &json) == 0
		&& json.value(QStringLiteral("eulerXyz")).toArray().size() == 3, "map rotate --euler", lastOutput);
	auto expected = loaded;
	setLevelMapSelection(&expected, {brush(0)});
	expect(rotateLevelMapSelection(&expected, euler(30, 15, 45, {8, 4, 16, true}), &error), "core Euler for CLI", error);
	QFile rotated(rotatedPath);
	expect(rotated.open(QIODevice::ReadOnly) && rotated.readAll() == bytes(expected), "CLI Euler writes the core result");
	expect(runCli(binary, {QStringLiteral("rotate"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--euler"),
		QStringLiteral("30,15,45"), QStringLiteral("--axis"), QStringLiteral("z"), QStringLiteral("--output"), rotatedPath, QStringLiteral("--overwrite")}, nullptr) == 2,
		"--euler with --axis is a usage error");
	expect(runCli(binary, {QStringLiteral("rotate"), input, QStringLiteral("--object"), QStringLiteral("brush:0"), QStringLiteral("--euler"),
		QStringLiteral("30,15"), QStringLiteral("--output"), rotatedPath, QStringLiteral("--overwrite")}, nullptr) == 2,
		"--euler needs three angles");

	const QString scaledPath = QDir(temp.path()).filePath(QStringLiteral("scaled.map"));
	expect(runCli(binary, {QStringLiteral("resize"), input, QStringLiteral("--object"), QStringLiteral("brush:1"), QStringLiteral("--factors"),
		QStringLiteral("-1,2,1"), QStringLiteral("--pivot"), QStringLiteral("0,0,0"), QStringLiteral("--texture-lock"), QStringLiteral("on"),
		QStringLiteral("--output"), scaledPath}, &json) == 0 && json.value(QStringLiteral("mirrored")).toBool()
		&& json.value(QStringLiteral("factors")).toArray().at(0).toDouble() == -1, "map resize --factors", lastOutput);
	expected = loaded;
	setLevelMapSelection(&expected, {brush(1)});
	LevelMapScaleRequest request;
	request.factors = {-1, 2, 1};
	request.pivot = {0, 0, 0, true};
	request.textures = {true, false};
	expect(scaleLevelMapSelection(&expected, request, &error), "core scale for CLI", error);
	QFile scaled(scaledPath);
	expect(scaled.open(QIODevice::ReadOnly) && scaled.readAll() == bytes(expected), "CLI factors write the core result");
	expect(runCli(binary, {QStringLiteral("resize"), input, QStringLiteral("--object"), QStringLiteral("brush:1"), QStringLiteral("--factors"),
		QStringLiteral("0,1,1"), QStringLiteral("--output"), scaledPath, QStringLiteral("--overwrite")}, nullptr) == 1, "a zero factor fails");
	expect(runCli(binary, {QStringLiteral("resize"), input, QStringLiteral("--object"), QStringLiteral("brush:1"), QStringLiteral("--factors"),
		QStringLiteral("2,2,2"), QStringLiteral("--size"), QStringLiteral("8,8,8"), QStringLiteral("--output"), scaledPath, QStringLiteral("--overwrite")}, nullptr) == 2,
		"--factors with --size is a usage error");
	expect(runCli(binary, {QStringLiteral("resize"), input, QStringLiteral("--object"), QStringLiteral("brush:1"), QStringLiteral("--size"),
		QStringLiteral("8,8,8"), QStringLiteral("--pivot"), QStringLiteral("0,0,0"), QStringLiteral("--output"), scaledPath, QStringLiteral("--overwrite")}, nullptr) == 2,
		"--pivot without --factors is a usage error");
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	rotation();
	scaling();
	if (argc > 1) {
		cli(QString::fromLocal8Bit(argv[1]));
	}
	if (ok) {
		std::cout << "Arbitrary rotation and scale checks passed." << std::endl;
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
