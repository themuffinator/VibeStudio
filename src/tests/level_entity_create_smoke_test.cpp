// Checks TrenchBroom's generated Create <class> commands through their core
// service (core/level_entity_create.h): the command ids and the shared
// command registry's entries for them; which classes get a command (point
// and brush classes, never worldspawn or base classes, one per id, sorted);
// a point class placed at an exact origin, against a plan view's plane and
// against the brush a camera ray hits, with its definition bounds and the
// grid; a brush class taking the selected brushes, and the brushes of a
// selected brush entity; each one undo step back to the exact bytes; locked,
// hidden and out-of-group objects and a locked current layer refused with a
// reason before anything changes; the CreateEntity placement operation,
// including cancellation. With the vibestudio binary as its argument it
// drives `map create-entity` too.

#include "core/entity_builtin_catalogue.h"
#include "core/entity_definitions.h"
#include "core/level_document.h"
#include "core/level_entity_create.h"
#include "core/level_map.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "core/studio_semantics.h"
#include "tests/level_entity_ownership_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
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

bool expect(bool condition, const char* label, const QString& detail = QString())
{
	if (!condition) {
		ok = false;
		std::cerr << "FAIL: " << label;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << std::endl;
	}
	return condition;
}

LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush, id}; }
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity, id}; }

QByteArray bytesOf(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

bool near(const LevelMapVec3& value, double x, double y, double z)
{
	return value.valid && std::abs(value.x - x) < 1e-9 && std::abs(value.y - y) < 1e-9 && std::abs(value.z - z) < 1e-9;
}

QString text(const LevelMapVec3& value)
{
	return QStringLiteral("%1 %2 %3").arg(value.x).arg(value.y).arg(value.z);
}

const LevelMapEntity* entityById(const LevelMapDocument& document, int id)
{
	for (const LevelMapEntity& value : document.entities) {
		if (value.id == id) {
			return &value;
		}
	}
	return nullptr;
}

// The entity ownership fixture: world brushes floor (0..64 cube) and w1, a
// func_door holding d1, d2 and d3, a func_wall holding c1, and a light.
struct Fixture {
	LevelMapDocument map;
	int door = -1, wall = -1, light = -1, floor = -1, w1 = -1, d1 = -1, c1 = -1;
};

Fixture fixture()
{
	Fixture f;
	LevelMapLoadRequest request;
	request.path = QStringLiteral("create.map");
	request.engineHint = QStringLiteral("idtech2");
	QString error;
	expect(loadLevelMapBytes(request, tests::entityOwnershipFixtureBytes(), &f.map, &error), "fixture loads", error);
	f.door = tests::entityOwnershipEntity(f.map, QStringLiteral("func_door"));
	f.wall = tests::entityOwnershipEntity(f.map, QStringLiteral("func_wall"));
	f.light = tests::entityOwnershipEntity(f.map, QStringLiteral("light"));
	f.floor = tests::entityOwnershipBrush(f.map, QStringLiteral("floor"));
	f.w1 = tests::entityOwnershipBrush(f.map, QStringLiteral("w1"));
	f.d1 = tests::entityOwnershipBrush(f.map, QStringLiteral("d1"));
	f.c1 = tests::entityOwnershipBrush(f.map, QStringLiteral("c1"));
	return f;
}

void select(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& refs)
{
	QString error;
	expect(setLevelMapSelection(document, refs, &error), "select", error);
}

const char* const kFgd = R"FGD(
@BaseClass = Targetname [ targetname(target_source) : "Name" ]
@SolidClass = worldspawn : "World" []
@PointClass base(Targetname) size(-16 -16 -24, 16 16 32) = info_player_start : "Player start" []
@PointClass base(Targetname) = light : "Light" [ light(integer) : "Brightness" : 300 ]
@SolidClass base(Targetname) = func_door : "Door" [ speed(integer) : "Speed" : 100 ]
@SolidClass = func_wall : "Wall" []
@PointClass = misc_lamp : "Lamp" []
@PointClass = miscLamp : "Same command id as misc_lamp" []
)FGD";

EntityDefinitionCatalogue catalogue()
{
	return parseEntityDefinitions(QStringLiteral("create.fgd"), QByteArray(kFgd), EntityDefinitionFormat::ValveFgd);
}

void identifiers()
{
	expect(levelEntityCreateCommandId(QStringLiteral("light")) == QStringLiteral("map.createEntity.light"), "command id");
	QString name;
	expect(levelEntityCreateClassForCommand(QStringLiteral("map.createEntity.info_player_start"), &name) && name == QStringLiteral("info_player_start"),
		"camelCase id names its class", name);
	expect(levelEntityCreateClassForCommand(QStringLiteral("map.create-entity.light"), &name) && name == QStringLiteral("light"), "kebab id names its class");
	for (const char* id : {"map.createEntity.", "map.createEntity.worldspawn", "map.createEntity.WorldSpawn", "map.createEntity.a b", "map.createEntity.a\"b",
			 "map.createEntity.a/b", "map.create-entity", "map.addEntity", "createEntity.light"}) {
		expect(!levelEntityCreateClassForCommand(QString::fromLatin1(id)), "not a generated id", QString::fromLatin1(id));
	}
	// The shared registry knows the family and each generated command.
	expect(shellCommandIdExists(QStringLiteral("map.createEntity.light")) && shellCommandIdExists(QStringLiteral("map.create-entity.func-door"))
			&& !shellCommandIdExists(QStringLiteral("map.createEntity.worldspawn")),
		"generated ids resolve in the shell registry");
	CommandPaletteEntry entry;
	expect(commandPaletteEntryForCommandId(QStringLiteral("map.createEntity.light"), &entry) && entry.label == QStringLiteral("Create light")
			&& entry.category == QStringLiteral("Levels") && entry.commandId == QStringLiteral("map.createEntity.light") && !entry.summary.isEmpty()
			&& entry.id == QStringLiteral("map-create-entity-light") && entry.defaultShortcut.isEmpty(),
		"a generated command has a palette entry", entry.id + QStringLiteral(" / ") + entry.label);
	expect(commandPaletteEntryForCommandId(QStringLiteral("map.createEntity"), &entry) && entry.id == QStringLiteral("map-create-entity")
			&& entry.label == QStringLiteral("Create Entity") && shellCommandIdExists(QStringLiteral("map.createEntity")),
		"Create Entity is in the registry");
	bool listed = false;
	for (const CommandPaletteEntry& documented : commandPaletteEntries()) {
		listed = listed || documented.commandId.startsWith(QLatin1String("map.create-entity."));
	}
	expect(!listed, "generated commands are not static palette entries");
}

void classes()
{
	const EntityDefinitionCatalogue defs = catalogue();
	expect(defs.error.isEmpty() && defs.classes.size() >= 6, "FGD parses", defs.error);
	const QVector<LevelEntityCreateClass> list = levelEntityCreateClasses(defs);
	QStringList names;
	for (const LevelEntityCreateClass& entry : list) {
		names << entry.className;
	}
	expect(names == QStringList {QStringLiteral("func_door"), QStringLiteral("func_wall"), QStringLiteral("info_player_start"), QStringLiteral("light"), QStringLiteral("misc_lamp")},
		"point and brush classes, one per id, no worldspawn or base class, sorted", names.join(QLatin1Char(',')));
	for (const LevelEntityCreateClass& entry : list) {
		if (entry.className == QLatin1String("info_player_start")) {
			expect(entry.kind == EntityClassKind::Point && entry.hasSize && entry.bounds.mins[2] == -24 && entry.bounds.maxs[2] == 32
					&& entry.sourcePath.endsWith(QLatin1String("create.fgd")),
				"point class bounds and source");
		} else if (entry.className == QLatin1String("func_door")) {
			expect(entry.kind == EntityClassKind::Brush && !entry.hasSize, "brush class");
		} else if (entry.className == QLatin1String("light")) {
			expect(entry.kind == EntityClassKind::Point && !entry.hasSize, "point class without bounds");
		}
	}
	LevelEntityCreateRequest request;
	QString error;
	expect(levelEntityCreateRequestForClass(defs, QStringLiteral("INFO_PLAYER_START"), &request, &error) && request.className == QStringLiteral("info_player_start")
			&& request.kind == EntityClassKind::Point && request.placement.entityBounds.contains(QStringLiteral("info_player_start")),
		"a request finds its class ignoring case", error);
	expect(!levelEntityCreateRequestForClass(defs, QStringLiteral("monster_army"), &request, &error) && error.contains(QStringLiteral("monster_army")),
		"an undefined class is refused with its name", error);
	expect(!levelEntityCreateRequestForClass(defs, QStringLiteral("worldspawn"), &request, &error) && !error.isEmpty(), "worldspawn is never created");
	expect(levelEntityCreateBounds(defs).size() == 1, "only sized point classes have bounds");
	// A built-in catalogue names no file.
	Fixture f = fixture();
	const auto builtin = levelEntityCreateClasses(builtinEntityDefinitions(builtinEntityGameForMap(f.map)));
	expect(!builtin.isEmpty() && builtin.first().sourcePath.isEmpty(), "built-in classes have no source file");
}

void pointClasses()
{
	const EntityDefinitionCatalogue defs = catalogue();
	QString error;
	LevelEntityCreateRequest request;
	expect(levelEntityCreateRequestForClass(defs, QStringLiteral("info_player_start"), &request, &error), "player start request", error);
	request.properties = {{QStringLiteral("targetname"), QStringLiteral("spawn1"), 0}};

	// An exact origin.
	Fixture f = fixture();
	select(&f.map, {brush(f.w1)});
	const QByteArray before = bytesOf(f.map);
	const auto depth = f.map.undoStack.size();
	request.placement.mode = LevelPasteMode::Offset;
	request.placement.offset = {-64, 32, 16, true};
	LevelEntityCreateReport report;
	expect(createLevelMapEntityOfClass(&f.map, request, &report, &error) && report.kind == EntityClassKind::Point && near(report.origin, -64, 32, 16)
			&& f.map.undoStack.size() == depth + 1,
		"a point class goes in at its origin as one step", error);
	const LevelMapEntity* made = entityById(f.map, report.entityId);
	expect(made && made->className == QStringLiteral("info_player_start") && near(made->origin, -64, 32, 16)
			&& f.map.selection == QVector<LevelMapSelectionRef> {entity(report.entityId)},
		"the new entity is selected with its origin");
	bool named = false;
	for (const LevelMapProperty& property : made ? made->properties : QVector<LevelMapProperty> {}) {
		named = named || (property.key == QLatin1String("targetname") && property.value == QLatin1String("spawn1"));
	}
	expect(named, "extra keys are written");
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo restores the exact bytes", error);
	request.placement.offset = {40000, 0, 0, true};
	expect(!createLevelMapEntityOfClass(&f.map, request, &report, &error) && bytesOf(f.map) == before, "an origin outside the map range is refused", error);

	// A plan view's plane under the pointer, at depth 0, on a 16-unit grid: the
	// box corner snaps to the grid and the box stands on the plane.
	request.placement = {};
	request.placement.entityBounds.insert(QStringLiteral("info_player_start"), {{-16, -16, -24}, {16, 16, 32}});
	request.placement.mode = LevelPasteMode::Plane;
	request.placement.point = {100, 37, 0, true};
	request.placement.normal = {0, 0, 1, true};
	request.placement.direction = {0, 0, -1, true};
	request.placement.grid = 16;
	expect(createLevelMapEntityOfClass(&f.map, request, &report, &error) && near(report.origin, 112, 48, 24), "a plan view's plane places it on the grid",
		text(report.origin) + QStringLiteral(" ") + error);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo the plane placement", error);

	// A camera ray straight down onto the floor brush (0..64): an 8-unit box on
	// its top face for a class without bounds.
	LevelEntityCreateRequest light;
	expect(levelEntityCreateRequestForClass(defs, QStringLiteral("light"), &light, &error), "light request", error);
	light.placement.mode = LevelPasteMode::ViewRay;
	light.placement.rayOrigin = {32, 32, 500, true};
	light.placement.direction = {0, 0, -1, true};
	light.placement.viewDirection = {0, 0, -1, true};
	expect(createLevelMapEntityOfClass(&f.map, light, &report, &error) && near(report.origin, 40, 40, 72), "a camera ray places it on the brush it hits",
		text(report.origin) + QStringLiteral(" ") + error);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo the ray placement", error);
	// Hidden brushes do not take the placement: the ray falls through to its
	// 256-unit fallback plane.
	select(&f.map, {brush(f.floor)});
	LevelVisibilityRequest hide;
	expect(changeLevelMapVisibility(&f.map, hide, nullptr, &error), "hide the floor", error);
	const QByteArray hiddenFloor = bytesOf(f.map);
	const auto hiddenDepth = f.map.undoStack.size();
	expect(createLevelMapEntityOfClass(&f.map, light, &report, &error) && near(report.origin, 40, 40, 252), "a hidden brush does not catch the ray",
		text(report.origin) + QStringLiteral(" ") + error);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == hiddenFloor && f.map.undoStack.size() == hiddenDepth, "undo the fallback placement", error);

	// A locked current layer takes no new entity.
	Fixture locked = fixture();
	QString layer;
	expect(createLevelSceneNode(&locked.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error), "make a layer", error);
	locked.map.activeSceneNode = layer;
	expect(setLevelSceneLocked(&locked.map, layer, true, &error), "lock it", error);
	const QByteArray lockedBefore = bytesOf(locked.map);
	request.placement = {};
	request.placement.mode = LevelPasteMode::Offset;
	request.placement.offset = {0, 0, 0, true};
	expect(levelEntityCreateBlockedReason(locked.map, request).contains(QStringLiteral("locked")), "a locked current layer is named",
		levelEntityCreateBlockedReason(locked.map, request));
	expect(!createLevelMapEntityOfClass(&locked.map, request, &report, &error) && bytesOf(locked.map) == lockedBefore, "and refused", error);

	// A new entity joins the open group.
	Fixture open = fixture();
	QString group;
	expect(createLevelSceneNode(&open.map, LevelSceneNodeKind::Group, QStringLiteral("Lamp"), {}, &group, &error)
			&& assignLevelSceneObjects(&open.map, group, {QStringLiteral("entity:%1").arg(open.light)}, &error) && openLevelSceneGroup(&open.map, group, &error),
		"open the lamp group", error);
	expect(createLevelMapEntityOfClass(&open.map, request, &report, &error)
			&& levelSceneMembership(open.map.scene, QStringLiteral("entity:%1").arg(report.entityId)) == group,
		"the new entity is created in the open group", error);

	// Refusals without a Quake map or a point or brush kind.
	LevelEntityCreateRequest unknown = request;
	unknown.kind = EntityClassKind::Base;
	expect(levelEntityCreateBlockedReason(f.map, unknown).contains(QStringLiteral("info_player_start")), "a base class is refused");
	LevelMapDocument doom;
	doom.format = LevelMapFormat::DoomWad;
	expect(!levelEntityCreateBlockedReason(doom, request).isEmpty(), "a Doom map has no Create commands");
}

void brushClasses()
{
	const EntityDefinitionCatalogue defs = catalogue();
	QString error;
	LevelEntityCreateRequest request;
	expect(levelEntityCreateRequestForClass(defs, QStringLiteral("func_wall"), &request, &error) && request.kind == EntityClassKind::Brush, "wall request", error);
	request.properties = {{QStringLiteral("targetname"), QStringLiteral("w"), 0}};

	Fixture f = fixture();
	select(&f.map, {});
	LevelEntityCreateReport report;
	expect(!levelMapSelectionCanCreateBrushEntity(f.map) && levelEntityCreateBlockedReason(f.map, request).contains(QStringLiteral("func_wall")),
		"nothing selected names what to select", levelEntityCreateBlockedReason(f.map, request));
	select(&f.map, {entity(f.light)});
	expect(!levelMapSelectionCanCreateBrushEntity(f.map), "a point entity has nothing to take");

	// A world brush and a door brush become one func_wall; the door keeps the rest.
	select(&f.map, {brush(f.w1), brush(f.d1)});
	expect(levelMapSelectionCanCreateBrushEntity(f.map), "brushes can make a brush entity");
	const QByteArray before = bytesOf(f.map);
	const auto depth = f.map.undoStack.size();
	expect(createLevelMapEntityOfClass(&f.map, request, &report, &error) && report.kind == EntityClassKind::Brush && report.brushes == 2 && report.patches == 0
			&& f.map.undoStack.size() == depth + 1,
		"a brush class takes the selected brushes as one step", error);
	const LevelMapEntity* made = entityById(f.map, report.entityId);
	expect(made && made->className == QStringLiteral("func_wall") && f.map.selection == QVector<LevelMapSelectionRef> {entity(report.entityId)}
			&& entityById(f.map, f.door) != nullptr,
		"the new brush entity is selected and the door stays");
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo puts the brushes back", error);
	expect(redoLevelMapEdit(&f.map, &error) && entityById(f.map, report.entityId), "redo makes it again", error);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo once more", error);

	// A selected brush entity brings all of its brushes; the emptied wall goes.
	select(&f.map, {entity(f.wall)});
	LevelEntityCreateRequest door;
	expect(levelEntityCreateRequestForClass(defs, QStringLiteral("func_door"), &door, &error), "door request", error);
	expect(createLevelMapEntityOfClass(&f.map, door, &report, &error) && report.brushes == 1 && entityById(f.map, f.wall) == nullptr,
		"a selected brush entity brings its brushes and goes", error);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == before, "undo brings the wall back", error);

	// A locked door brush refuses the whole edit with its id.
	Fixture locked = fixture();
	QString layer;
	expect(createLevelSceneNode(&locked.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
			&& assignLevelSceneObjects(&locked.map, layer, {QStringLiteral("entity:%1").arg(locked.door)}, &error)
			&& setLevelSceneLocked(&locked.map, layer, true, &error),
		"lock the door", error);
	select(&locked.map, {brush(locked.w1), brush(locked.d1)});
	const QByteArray lockedBefore = bytesOf(locked.map);
	expect(levelEntityCreateBlockedReason(locked.map, request).contains(QString::number(locked.d1)), "a locked brush is named",
		levelEntityCreateBlockedReason(locked.map, request));
	expect(!createLevelMapEntityOfClass(&locked.map, request, &report, &error) && bytesOf(locked.map) == lockedBefore, "and the edit refused", error);

	// Hidden brushes of a selected brush entity would move: refused.
	Fixture hidden = fixture();
	select(&hidden.map, {brush(hidden.d1)});
	LevelVisibilityRequest hide;
	expect(changeLevelMapVisibility(&hidden.map, hide, nullptr, &error), "hide d1", error);
	QString selectError;
	if (expect(setLevelMapSelection(&hidden.map, {entity(hidden.door)}, &selectError), "select the door", selectError)) {
		const QByteArray hiddenBefore = bytesOf(hidden.map);
		expect(levelEntityCreateBlockedReason(hidden.map, request).contains(QStringLiteral("hidden")), "hidden brushes are named",
			levelEntityCreateBlockedReason(hidden.map, request));
		expect(!createLevelMapEntityOfClass(&hidden.map, request, &report, &error) && bytesOf(hidden.map) == hiddenBefore, "and the edit refused", error);
	}

	// Outside the open group: refused.
	Fixture open = fixture();
	QString group;
	expect(createLevelSceneNode(&open.map, LevelSceneNodeKind::Group, QStringLiteral("Lamp"), {}, &group, &error)
			&& assignLevelSceneObjects(&open.map, group, {QStringLiteral("entity:%1").arg(open.light)}, &error) && openLevelSceneGroup(&open.map, group, &error),
		"open the lamp group", error);
	if (expect(setLevelMapSelection(&open.map, {brush(open.w1)}, &selectError), "select a brush outside the group", selectError)) {
		const QByteArray openBefore = bytesOf(open.map);
		expect(levelEntityCreateBlockedReason(open.map, request).contains(QStringLiteral("open group")), "outside the open group is named",
			levelEntityCreateBlockedReason(open.map, request));
		expect(!createLevelMapEntityOfClass(&open.map, request, &report, &error) && bytesOf(open.map) == openBefore, "and the edit refused", error);
	}
}

void placementOperation()
{
	const EntityDefinitionCatalogue defs = catalogue();
	Fixture f = fixture();
	select(&f.map, {brush(f.w1)});
	const QByteArray before = bytesOf(f.map);
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::CreateEntity;
	QString error;
	expect(levelEntityCreateRequestForClass(defs, QStringLiteral("func_door"), &request.entityCreate, &error), "door request", error);
	LevelPlacementResult result = prepareLevelPlacement(f.map, request);
	expect(result.succeeded && result.entityCreateReport.kind == EntityClassKind::Brush && result.entityCreateReport.brushes == 1
			&& entityById(result.document, result.entityCreateReport.entityId) && bytesOf(f.map) == before,
		"CreateEntity prepares a candidate and leaves the source alone", result.error);
	LevelPlacementControl control;
	control.isCancelled = [] { return true; };
	result = prepareLevelPlacement(f.map, request, control);
	expect(!result.succeeded && result.cancelled && result.entityCreateReport.entityId == -1 && bytesOf(f.map) == before, "cancelling discards it");
	select(&f.map, {});
	result = prepareLevelPlacement(f.map, request);
	expect(!result.succeeded && !result.error.isEmpty(), "a refusal reaches the placement result", result.error);
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary directory")) {
		return;
	}
	const Fixture f = fixture();
	const QString path = temp.filePath(QStringLiteral("create.map"));
	const QString fgd = temp.filePath(QStringLiteral("create.fgd"));
	const QByteArray source = bytesOf(f.map);
	for (const auto& [file, bytes] : {std::pair {path, source}, std::pair {fgd, QByteArray(kFgd)}}) {
		QFile out(file);
		expect(out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size(), "CLI fixture", file);
	}
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
									  temp.filePath(QStringLiteral("settings.ini")), QStringLiteral("map"), QStringLiteral("create-entity")}
								+ args);
		const bool done = process.waitForStarted() && process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status",
			args.join(QLatin1Char(' ')) + QStringLiteral(": ") + QString::fromUtf8(out + err));
		if (!done) {
			process.kill();
			process.waitForFinished();
		}
		return QJsonDocument::fromJson(out).object();
	};
	const auto load = [&](const QString& output) {
		LevelMapDocument result;
		QString error;
		expect(loadLevelMap({output, {}, {}}, &result, &error), "load CLI output", error);
		return result;
	};

	// A point class of the built-in catalogue at an exact origin.
	QString output = temp.filePath(QStringLiteral("light.map"));
	QJsonObject report = run({path, QStringLiteral("--class"), QStringLiteral("light"), QStringLiteral("--origin"), QStringLiteral("-64,0,96"), QStringLiteral("--key"),
		QStringLiteral("light=200"), QStringLiteral("--output"), output});
	const int lightId = report.value(QStringLiteral("entityId")).toInt(-1);
	expect(report.value(QStringLiteral("kind")).toString() == QStringLiteral("point") && report.value(QStringLiteral("placement")).toString() == QStringLiteral("origin")
			&& report.value(QStringLiteral("origin")).toArray() == QJsonArray {-64, 0, 96} && report.value(QStringLiteral("definitions")).toString() == QStringLiteral("built-in")
			&& report.value(QStringLiteral("commandId")).toString() == QStringLiteral("map.createEntity.light") && report.value(QStringLiteral("mapWritten")).toBool(),
		"CLI reports a point entity", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	LevelMapDocument lit = load(output);
	const LevelMapEntity* light = entityById(lit, lightId);
	expect(light && light->className == QStringLiteral("light") && near(light->origin, -64, 0, 96), "CLI output holds the light");

	// A targeted placement with definitions: the ray hits the floor.
	output = temp.filePath(QStringLiteral("start.map"));
	report = run({path, QStringLiteral("--class"), QStringLiteral("info_player_start"), QStringLiteral("--definitions"), fgd, QStringLiteral("--ray-origin"),
		QStringLiteral("32,32,500"), QStringLiteral("--ray-direction"), QStringLiteral("0,0,-1"), QStringLiteral("--view-direction"), QStringLiteral("0,0,-1"),
		QStringLiteral("--grid"), QStringLiteral("16"), QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("placement")).toString() == QStringLiteral("view-ray") && report.value(QStringLiteral("origin")).toArray() == QJsonArray {48, 48, 88},
		"CLI places a point class against the brush the ray hits", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));

	// A brush class takes the named brushes.
	output = temp.filePath(QStringLiteral("wall.map"));
	report = run({path, QStringLiteral("--class"), QStringLiteral("func_wall"), QStringLiteral("--object"), QStringLiteral("brush:%1").arg(f.w1), QStringLiteral("--object"),
		QStringLiteral("brush:%1").arg(f.d1), QStringLiteral("--output"), output});
	const int wallId = report.value(QStringLiteral("entityId")).toInt(-1);
	expect(report.value(QStringLiteral("kind")).toString() == QStringLiteral("brush") && report.value(QStringLiteral("brushes")).toInt() == 2,
		"CLI reports a brush entity", QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact)));
	LevelMapDocument walled = load(output);
	int held = 0;
	for (const LevelMapBrush& value : walled.brushes) {
		held += value.entityId == wallId ? 1 : 0;
	}
	expect(held == 2 && entityById(walled, wallId) && entityById(walled, wallId)->className == QStringLiteral("func_wall"), "CLI output holds the wall");

	// Dry runs write nothing; refusals write nothing and name their reason.
	output = temp.filePath(QStringLiteral("dry.map"));
	report = run({path, QStringLiteral("--class"), QStringLiteral("light"), QStringLiteral("--origin"), QStringLiteral("0,0,0"), QStringLiteral("--output"), output,
		QStringLiteral("--dry-run")});
	expect(report.value(QStringLiteral("dryRun")).toBool() && !QFileInfo::exists(output), "a dry run writes nothing");
	run({}, 2);
	run({path, QStringLiteral("--class"), QStringLiteral("light"), QStringLiteral("--origin"), QStringLiteral("0,0,0")}, 2);
	run({path, QStringLiteral("--class"), QStringLiteral("light"), QStringLiteral("--object"), QStringLiteral("brush:%1").arg(f.w1), QStringLiteral("--origin"),
			QStringLiteral("0,0,0"), QStringLiteral("--output"), temp.filePath(QStringLiteral("x.map"))},
		2);
	run({path, QStringLiteral("--class"), QStringLiteral("light"), QStringLiteral("--origin"), QStringLiteral("0,0"), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("x.map"))},
		2);
	run({path, QStringLiteral("--class"), QStringLiteral("light"), QStringLiteral("--origin"), QStringLiteral("0,0,0"), QStringLiteral("--camera"), QStringLiteral("0,0,0"),
			QStringLiteral("--output"), temp.filePath(QStringLiteral("x.map"))},
		2);
	run({path, QStringLiteral("--class"), QStringLiteral("func_wall"), QStringLiteral("--output"), temp.filePath(QStringLiteral("x.map"))}, 2);
	run({path, QStringLiteral("--class"), QStringLiteral("func_wall"), QStringLiteral("--object"), QStringLiteral("entity:%1").arg(f.light), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("x.map"))},
		4);
	run({path, QStringLiteral("--class"), QStringLiteral("monster_nothing"), QStringLiteral("--origin"), QStringLiteral("0,0,0"), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("x.map"))},
		4);
	run({path, QStringLiteral("--class"), QStringLiteral("worldspawn"), QStringLiteral("--object"), QStringLiteral("brush:%1").arg(f.w1), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("x.map"))},
		4);
	expect(!QFileInfo::exists(temp.filePath(QStringLiteral("x.map"))), "refused CLI edits write nothing");
	QFile check(path);
	expect(check.open(QIODevice::ReadOnly) && check.readAll() == source, "the CLI never rewrites its input");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	identifiers();
	classes();
	pointClasses();
	brushClasses();
	placementOperation();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
	}
	std::cout << (ok ? "Entity create checks passed" : "Entity create checks failed") << std::endl;
	return ok ? 0 : 1;
}
