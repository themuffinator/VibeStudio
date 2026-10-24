// Checks which entity owns a brush or patch (core/level_entity_ownership.h):
// Move Primitives to Entity with last, first and named targets, Move Selected
// to World, Make Structural under both policies and Explode Entity. Each edit
// is one undo step that undoes to the exact bytes it started from, keeps
// primitive ids, survives a save and reopen, and refuses locked, linked and
// unsuitable content without touching the map. With the vibestudio binary as
// its argument it drives the matching `map` CLI commands too.

#include "core/level_document.h"
#include "core/level_entity_ownership.h"
#include "core/level_linked_groups.h"
#include "core/level_map.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_entity_ownership_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

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
LevelMapSelectionRef patch(int id) { return {LevelMapSelectionKind::QuakePatch, id}; }
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity, id}; }

QByteArray bytesOf(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

LevelMapDocument loadBytes(const QByteArray& bytes, const QString& game)
{
	LevelMapDocument loaded;
	LevelMapLoadRequest request;
	request.path = QStringLiteral("ownership-%1.map").arg(game);
	request.engineHint = game == QStringLiteral("quake3") ? QStringLiteral("idtech3") : QStringLiteral("idtech2");
	QString error;
	expect(loadLevelMapBytes(request, bytes, &loaded, &error), "load map bytes", error);
	return loaded;
}

int brushNamed(const LevelMapDocument& document, const QString& texture)
{
	for (const LevelMapBrush& value : document.brushes) {
		if (!value.faces.isEmpty() && value.faces.first().textureName == texture) {
			return value.id;
		}
	}
	return -1;
}

int ownerOf(const LevelMapDocument& document, int brushId)
{
	for (const LevelMapBrush& value : document.brushes) {
		if (value.id == brushId) {
			return value.entityId;
		}
	}
	return -2;
}

QVector<int> entitiesOfClass(const LevelMapDocument& document, const QString& className)
{
	QVector<int> ids;
	for (const LevelMapEntity& value : document.entities) {
		if (value.className == className) {
			ids << value.id;
		}
	}
	return ids;
}

int entityOfClass(const LevelMapDocument& document, const QString& className)
{
	const QVector<int> ids = entitiesOfClass(document, className);
	return ids.isEmpty() ? -1 : ids.first();
}

int owned(const LevelMapDocument& document, int entityId)
{
	int count = 0;
	for (const LevelMapBrush& value : document.brushes) {
		count += value.entityId == entityId ? 1 : 0;
	}
	for (const LevelMapPatch& value : document.patches) {
		count += value.entityId == entityId ? 1 : 0;
	}
	return count;
}

QString keyOf(const LevelMapDocument& document, int entityId, const QString& key)
{
	for (const LevelMapEntity& value : document.entities) {
		if (value.id == entityId) {
			for (const LevelMapProperty& property : value.properties) {
				if (property.key == key) {
					return property.value;
				}
			}
		}
	}
	return QString();
}

bool select(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& refs)
{
	QString error;
	return expect(setLevelMapSelection(document, refs, &error), "select fixture objects", error);
}

// The map every check starts from, written and read back so every object owns
// its source lines: worldspawn with brushes floor and w1, a func_door holding
// d1, d2 and d3 with keys, a func_wall holding c1 and a light.
struct Fixture {
	LevelMapDocument map;
	int world = -1, door = -1, wall = -1, light = -1;
	int floor = -1, w1 = -1, d1 = -1, d2 = -1, d3 = -1, c1 = -1;
};

Fixture fixture(const QString& game = QStringLiteral("quake"))
{
	const QByteArray bytes = tests::entityOwnershipFixtureBytes(game);
	expect(!bytes.isEmpty(), "build the fixture map");
	Fixture result;
	result.map = loadBytes(bytes, game);
	result.world = entityOfClass(result.map, QStringLiteral("worldspawn"));
	result.door = entityOfClass(result.map, QStringLiteral("func_door"));
	result.wall = entityOfClass(result.map, QStringLiteral("func_wall"));
	result.light = entityOfClass(result.map, QStringLiteral("light"));
	result.floor = brushNamed(result.map, QStringLiteral("studio/floor"));
	result.w1 = brushNamed(result.map, QStringLiteral("studio/w1"));
	result.d1 = brushNamed(result.map, QStringLiteral("studio/d1"));
	result.d2 = brushNamed(result.map, QStringLiteral("studio/d2"));
	result.d3 = brushNamed(result.map, QStringLiteral("studio/d3"));
	result.c1 = brushNamed(result.map, QStringLiteral("studio/c1"));
	expect(result.world >= 0 && result.door >= 0 && result.wall >= 0 && result.light >= 0 && owned(result.map, result.door) == 3
			&& ownerOf(result.map, result.c1) == result.wall && result.map.undoStack.isEmpty(),
		"fixture shape");
	return result;
}

void moveToEntity()
{
	Fixture f = fixture();
	const QByteArray original = bytesOf(f.map);
	select(&f.map, {brush(f.w1), brush(f.c1), brush(f.d1)});
	LevelPrimitiveMoveReport report;
	QString error;
	expect(levelMapSelectionMovableToEntity(f.map, LevelPrimitiveTarget::LastSelected), "menus offer the move");
	expect(moveLevelMapSelectionToEntity(&f.map, {}, &report, &error), "move into the last-selected item's entity", error);
	expect(report.targetEntity == f.door && report.targetClass == QStringLiteral("func_door") && report.movedBrushes == 2 && report.movedPatches == 0
			&& report.removedEntities == QVector<int> {f.wall},
		"the report names the door, two moved brushes and the emptied wall");
	expect(ownerOf(f.map, f.w1) == f.door && ownerOf(f.map, f.c1) == f.door && owned(f.map, f.door) == 5, "the door holds the moved brushes under their ids");
	expect(entitiesOfClass(f.map, QStringLiteral("func_wall")).isEmpty(), "the emptied func_wall went");
	expect(keyOf(f.map, f.door, QStringLiteral("speed")) == QStringLiteral("100") && keyOf(f.map, f.door, QStringLiteral("targetname")) == QStringLiteral("door1"),
		"the door kept its keys");
	expect(f.map.undoStack.size() == 1, "the move is one undo step");
	expect(f.map.selection == QVector<LevelMapSelectionRef> {brush(f.w1), brush(f.c1), brush(f.d1)}, "the moved brushes stay selected");
	const QByteArray moved = bytesOf(f.map);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == original, "undo restores the original bytes", error);
	expect(redoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == moved, "redo moves them again", error);
	const LevelMapDocument reopened = loadBytes(moved, QStringLiteral("quake"));
	const int door = entityOfClass(reopened, QStringLiteral("func_door"));
	expect(door >= 0 && owned(reopened, door) == 5 && entitiesOfClass(reopened, QStringLiteral("func_wall")).isEmpty()
			&& owned(reopened, entityOfClass(reopened, QStringLiteral("worldspawn"))) == 1 && keyOf(reopened, door, QStringLiteral("speed")) == QStringLiteral("100"),
		"the move survives a save and reopen");

	// The first-selected item picks the target too; the same selection with the
	// last-selected target gives the brushes to the func_wall instead.
	Fixture first = fixture();
	select(&first.map, {brush(first.d1), brush(first.w1), brush(first.c1)});
	LevelPrimitiveMoveRequest request;
	request.target = LevelPrimitiveTarget::FirstSelected;
	expect(moveLevelMapSelectionToEntity(&first.map, request, &report, &error) && report.targetEntity == first.door && owned(first.map, first.door) == 5,
		"the first-selected target is the door", error);
	Fixture last = fixture();
	select(&last.map, {brush(last.d1), brush(last.w1), brush(last.c1)});
	expect(moveLevelMapSelectionToEntity(&last.map, {}, &report, &error) && report.targetEntity == last.wall && owned(last.map, last.wall) == 3
			&& owned(last.map, last.door) == 2 && report.removedEntities.isEmpty(),
		"the last-selected target is the func_wall and the door keeps its other brushes", error);

	// A named target, worldspawn included.
	Fixture named = fixture();
	select(&named.map, {brush(named.d1)});
	request.target = LevelPrimitiveTarget::Entity;
	request.entityId = named.world;
	expect(moveLevelMapSelectionToEntity(&named.map, request, &report, &error) && ownerOf(named.map, named.d1) == named.world && owned(named.map, named.door) == 2,
		"a named worldspawn target takes the brush", error);

	// Refusals leave the map and its history alone.
	Fixture refused = fixture();
	const QByteArray before = bytesOf(refused.map);
	const auto unchanged = [&](const char* label) {
		expect(bytesOf(refused.map) == before && refused.map.undoStack.isEmpty() && !error.isEmpty(), label, error);
	};
	select(&refused.map, {brush(refused.w1), entity(refused.light)});
	expect(!levelMapSelectionMovableToEntity(refused.map, LevelPrimitiveTarget::LastSelected), "menus do not offer a point entity target");
	expect(!moveLevelMapSelectionToEntity(&refused.map, {}, nullptr, &error), "a point entity cannot take brushes");
	unchanged("a point entity target leaves the map alone");
	select(&refused.map, {brush(refused.d1), brush(refused.d2)});
	expect(!moveLevelMapSelectionToEntity(&refused.map, {}, nullptr, &error), "brushes already in the target are refused");
	unchanged("nothing to move leaves the map alone");
	select(&refused.map, {});
	expect(!moveLevelMapSelectionToEntity(&refused.map, {}, nullptr, &error), "an empty selection is refused");
	unchanged("an empty selection leaves the map alone");
}

void moveSelectedToWorld()
{
	Fixture f = fixture();
	const QByteArray original = bytesOf(f.map);
	select(&f.map, {brush(f.d1)});
	LevelPrimitiveMoveReport report;
	QString error;
	expect(moveLevelMapSelectedPrimitivesToWorld(&f.map, &report, &error) && report.movedBrushes == 1 && report.removedEntities.isEmpty(),
		"move one door brush to the world", error);
	expect(ownerOf(f.map, f.d1) == f.world && owned(f.map, f.door) == 2 && keyOf(f.map, f.door, QStringLiteral("targetname")) == QStringLiteral("door1"),
		"the door keeps its other brushes and keys");
	expect(f.map.undoStack.size() == 1 && undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == original, "one step that undoes to the original bytes", error);
	select(&f.map, {brush(f.d1), brush(f.d2), brush(f.d3)});
	expect(moveLevelMapSelectedPrimitivesToWorld(&f.map, &report, &error) && report.removedEntities == QVector<int> {f.door}
			&& entitiesOfClass(f.map, QStringLiteral("func_door")).isEmpty(),
		"moving every door brush removes the door", error);
	expect(f.map.selection == QVector<LevelMapSelectionRef> {brush(f.d1), brush(f.d2), brush(f.d3)}, "the brushes stay selected");
	select(&f.map, {brush(f.floor)});
	expect(!moveLevelMapSelectedPrimitivesToWorld(&f.map, nullptr, &error) && !error.isEmpty(), "world brushes are refused with a reason");

	// Hidden brushes of a selected entity stay where they are.
	Fixture hidden = fixture();
	hidden.map.hiddenObjects.insert(QStringLiteral("brush:%1").arg(hidden.d2));
	select(&hidden.map, {entity(hidden.door)});
	expect(moveLevelMapSelectedPrimitivesToWorld(&hidden.map, &report, &error) && report.movedBrushes == 2 && report.keptHidden == 1
			&& ownerOf(hidden.map, hidden.d2) == hidden.door && owned(hidden.map, hidden.door) == 1 && report.removedEntities.isEmpty(),
		"a hidden brush keeps its entity", error);

	// A Quake III patch leaves its func_group; the brush stays.
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake3");
	create.starterRoom = false;
	expect(createLevelMap(create, &document, &error), "Quake III fixture", error);
	int brushId = -1;
	int patchId = -1;
	addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/g1"), &brushId, &error);
	LevelPatchCreateRequest patchRequest;
	patchRequest.center = {200, 0, 0, true};
	LevelMapPatch made;
	expect(createLevelPatch(patchRequest, &made, &error) && addLevelMapPatch(&document, made, &patchId, &error), "patch fixture", error);
	select(&document, {brush(brushId), patch(patchId)});
	expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_group"), {}, nullptr, &error), "func_group fixture", error);
	LevelMapDocument quake3 = loadBytes(bytesOf(document), QStringLiteral("quake3"));
	const QByteArray quake3Before = bytesOf(quake3);
	const int group = entityOfClass(quake3, QStringLiteral("func_group"));
	const int world = entityOfClass(quake3, QStringLiteral("worldspawn"));
	expect(group >= 0 && quake3.patches.size() == 1 && quake3.patches.first().entityId == group, "the patch starts in the func_group");
	select(&quake3, {patch(quake3.patches.first().id)});
	expect(moveLevelMapSelectedPrimitivesToWorld(&quake3, &report, &error) && report.movedPatches == 1 && quake3.patches.first().entityId == world
			&& owned(quake3, group) == 1,
		"the patch moves to the world and the func_group keeps its brush", error);
	const LevelMapDocument quake3Reopened = loadBytes(bytesOf(quake3), QStringLiteral("quake3"));
	expect(quake3Reopened.patches.size() == 1 && quake3Reopened.patches.first().entityId == entityOfClass(quake3Reopened, QStringLiteral("worldspawn"))
			&& owned(quake3Reopened, entityOfClass(quake3Reopened, QStringLiteral("func_group"))) == 1,
		"the patch is a world patch after a reopen");
	expect(undoLevelMapEdit(&quake3, &error) && bytesOf(quake3) == quake3Before, "undo puts the patch back in its func_group", error);
	// And a patch can join an entity.
	select(&quake3, {patch(quake3.patches.first().id)});
	expect(moveLevelMapSelectedPrimitivesToWorld(&quake3, nullptr, &error), "patch back to the world", error);
	select(&quake3, {patch(quake3.patches.first().id), brush(quake3.brushes.first().id)});
	expect(moveLevelMapSelectionToEntity(&quake3, {}, &report, &error) && report.movedPatches == 1 && quake3.patches.first().entityId == group,
		"a world patch joins the func_group", error);
}

void sceneAndLocks()
{
	QString error;
	// A brush leaving an entity in a group stays in that group.
	Fixture grouped = fixture();
	QString doors;
	expect(createLevelSceneNode(&grouped.map, LevelSceneNodeKind::Group, QStringLiteral("Doors"), {}, &doors, &error)
			&& assignLevelSceneObjects(&grouped.map, doors, {QStringLiteral("entity:%1").arg(grouped.door)}, &error),
		"group fixture", error);
	const QByteArray groupedBefore = bytesOf(grouped.map);
	const auto history = grouped.map.undoStack.size();
	select(&grouped.map, {brush(grouped.d1)});
	expect(moveLevelMapSelectedPrimitivesToWorld(&grouped.map, nullptr, &error), "move a grouped door brush to the world", error);
	expect(levelSceneMembership(grouped.map.scene, QStringLiteral("brush:%1").arg(grouped.d1)) == doors, "it joins the group that held its entity");
	expect(undoLevelMapEdit(&grouped.map, &error) && bytesOf(grouped.map) == groupedBefore && grouped.map.undoStack.size() == history
			&& levelSceneMembership(grouped.map.scene, QStringLiteral("brush:%1").arg(grouped.d1)).isEmpty(),
		"undo restores the scene too", error);
	// Without a group, the current layer takes it, as TrenchBroom's current group.
	QString layer;
	expect(createLevelSceneNode(&grouped.map, LevelSceneNodeKind::Layer, QStringLiteral("Active"), {}, &layer, &error), "layer fixture", error);
	grouped.map.activeSceneNode = layer;
	select(&grouped.map, {brush(grouped.c1)});
	expect(moveLevelMapSelectedPrimitivesToWorld(&grouped.map, nullptr, &error) && levelSceneMembership(grouped.map.scene, QStringLiteral("brush:%1").arg(grouped.c1)) == layer,
		"the current layer takes a brush from an ungrouped entity", error);
	// Exploded copies join their entity's group.
	select(&grouped.map, {entity(grouped.door)});
	LevelExplodeReport exploded;
	expect(explodeLevelMapSelectionEntities(&grouped.map, {}, &exploded, &error) && exploded.createdEntities.size() == 2, "explode the grouped door", error);
	for (const int copy : std::as_const(exploded.createdEntities)) {
		expect(levelSceneMembership(grouped.map.scene, QStringLiteral("entity:%1").arg(copy)) == doors, "an exploded copy joins its entity's group");
	}

	// A locked entity refuses every ownership edit.
	Fixture locked = fixture();
	QString lockedLayer;
	expect(createLevelSceneNode(&locked.map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &lockedLayer, &error)
			&& assignLevelSceneObjects(&locked.map, lockedLayer, {QStringLiteral("entity:%1").arg(locked.door)}, &error)
			&& setLevelSceneLocked(&locked.map, lockedLayer, true, &error),
		"lock fixture", error);
	const QByteArray lockedBefore = bytesOf(locked.map);
	const auto lockedHistory = locked.map.undoStack.size();
	const auto untouched = [&](const char* label) {
		expect(bytesOf(locked.map) == lockedBefore && locked.map.undoStack.size() == lockedHistory && !error.isEmpty(), label, error);
	};
	select(&locked.map, {brush(locked.w1), brush(locked.d1)});
	expect(!moveLevelMapSelectionToEntity(&locked.map, {}, nullptr, &error), "a locked target is refused");
	untouched("a locked target leaves the map alone");
	select(&locked.map, {brush(locked.d1), brush(locked.w1)});
	expect(!moveLevelMapSelectionToEntity(&locked.map, {}, nullptr, &error), "a locked source is refused");
	untouched("a locked source leaves the map alone");
	select(&locked.map, {brush(locked.d1)});
	expect(!explodeLevelMapSelectionEntities(&locked.map, {}, nullptr, &error), "a locked entity cannot be exploded");
	untouched("a refused explode leaves the map alone");
	expect(!makeLevelMapSelectionStructural(&locked.map, LevelStructuralPolicy::LeaveEntities, nullptr, &error), "a locked brush cannot leave its entity");
	untouched("a refused Make Structural leaves the map alone");

	// Linked content is refused rather than spread to the copies.
	Fixture linked = fixture();
	QString wallGroup;
	QString copy;
	expect(createLevelSceneNode(&linked.map, LevelSceneNodeKind::Group, QStringLiteral("Wall"), {}, &wallGroup, &error)
			&& assignLevelSceneObjects(&linked.map, wallGroup, {QStringLiteral("entity:%1").arg(linked.wall)}, &error)
			&& createLinkedLevelGroup(&linked.map, wallGroup, {0, 256, 0, true}, &copy, &error),
		"linked fixture", error);
	const QByteArray linkedBefore = bytesOf(linked.map);
	select(&linked.map, {brush(linked.c1), brush(linked.d1)});
	expect(!moveLevelMapSelectionToEntity(&linked.map, {}, nullptr, &error) && error.contains(QStringLiteral("linked")) && bytesOf(linked.map) == linkedBefore,
		"a linked brush is refused", error);
	select(&linked.map, {entity(linked.wall)});
	expect(!moveLevelMapSelectedPrimitivesToWorld(&linked.map, nullptr, &error) && bytesOf(linked.map) == linkedBefore, "a linked entity keeps its brushes", error);
}

void structural()
{
	QString error;
	LevelStructuralReport report;
	// Quake: Make Structural takes only the selected brush out of func_detail.
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	expect(createLevelMap(create, &document, &error), "detail fixture", error);
	QVector<int> ids;
	for (int index = 0; index < 3; ++index) {
		int id = -1;
		addLevelMapBoxBrush(&document, {index * 96.0, 0, 0, true}, {index * 96.0 + 64, 64, 64, true}, QStringLiteral("studio/s%1").arg(index), &id, &error);
		ids << id;
	}
	select(&document, {brush(ids.at(0)), brush(ids.at(1))});
	expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_detail"), {}, nullptr, &error), "func_detail fixture", error);
	select(&document, {brush(brushNamed(document, QStringLiteral("studio/s2")))});
	expect(tieLevelMapSelectionToEntity(&document, QStringLiteral("func_door"), {}, nullptr, &error), "func_door fixture", error);
	LevelMapDocument quake = loadBytes(bytesOf(document), QStringLiteral("quake"));
	const QByteArray quakeBefore = bytesOf(quake);
	const int s0 = brushNamed(quake, QStringLiteral("studio/s0"));
	const int s1 = brushNamed(quake, QStringLiteral("studio/s1"));
	const int s2 = brushNamed(quake, QStringLiteral("studio/s2"));
	const int world = entityOfClass(quake, QStringLiteral("worldspawn"));
	const int detail = entityOfClass(quake, QStringLiteral("func_detail"));
	select(&quake, {brush(s0)});
	expect(makeLevelMapSelectionStructural(&quake, LevelStructuralPolicy::DetailOnly, &report, &error) && report.changed == 1 && report.movedToWorld == 1
			&& ownerOf(quake, s0) == world && ownerOf(quake, s1) == detail,
		"only the selected brush leaves func_detail", error);
	expect(undoLevelMapEdit(&quake, &error) && bytesOf(quake) == quakeBefore, "undo restores func_detail", error);
	int changed = 0;
	select(&quake, {brush(s1)});
	expect(setLevelMapSelectionDetail(&quake, false, &changed, &error) && changed == 1 && ownerOf(quake, s1) == world && ownerOf(quake, s0) == detail,
		"the detail service routes structural through the same policy", error);
	expect(undoLevelMapEdit(&quake, &error) && bytesOf(quake) == quakeBefore, "undo again", error);
	select(&quake, {brush(s2)});
	expect(!makeLevelMapSelectionStructural(&quake, LevelStructuralPolicy::DetailOnly, nullptr, &error) && bytesOf(quake) == quakeBefore,
		"a func_door brush is already structural without the TrenchBroom policy", error);
	const int door = entityOfClass(quake, QStringLiteral("func_door"));
	expect(makeLevelMapSelectionStructural(&quake, LevelStructuralPolicy::LeaveEntities, &report, &error) && report.movedToWorld == 1
			&& report.removedEntities == QVector<int> {door} && ownerOf(quake, s2) == world && entitiesOfClass(quake, QStringLiteral("func_door")).isEmpty(),
		"the TrenchBroom policy moves the func_door brush to the world and the door goes", error);
	expect(quake.undoStack.size() == 1 && undoLevelMapEdit(&quake, &error) && bytesOf(quake) == quakeBefore, "one step back to the original bytes", error);

	// Quake II: the TrenchBroom policy moves and clears detail in one step.
	LevelMapDocument flagged;
	create.game = QStringLiteral("quake2");
	expect(createLevelMap(create, &flagged, &error), "Quake II fixture", error);
	int a = -1;
	int c = -1;
	addLevelMapBoxBrush(&flagged, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/a"), &a, &error);
	addLevelMapBoxBrush(&flagged, {96, 0, 0, true}, {160, 64, 64, true}, QStringLiteral("studio/c"), &c, &error);
	select(&flagged, {brush(c)});
	expect(setLevelMapSelectionDetail(&flagged, true, nullptr, &error), "detail flag fixture", error);
	select(&flagged, {brush(c)});
	expect(tieLevelMapSelectionToEntity(&flagged, QStringLiteral("func_door"), {}, nullptr, &error), "Quake II door fixture", error);
	LevelMapDocument quake2 = loadBytes(bytesOf(flagged), QStringLiteral("quake2"));
	const QByteArray quake2Before = bytesOf(quake2);
	const int detailBrush = brushNamed(quake2, QStringLiteral("studio/c"));
	const int quake2World = entityOfClass(quake2, QStringLiteral("worldspawn"));
	const auto hasDetail = [](const LevelMapDocument& map, int id) {
		for (const LevelMapBrush& value : map.brushes) {
			if (value.id == id) {
				for (const LevelMapBrushFace& face : value.faces) {
					if ((face.contentFlags & 0x8000000) != 0) {
						return true;
					}
				}
			}
		}
		return false;
	};
	expect(levelMapUsesFaceFlags(quake2) && hasDetail(quake2, detailBrush), "the Quake II brush starts as detail in a func_door");
	select(&quake2, {brush(detailBrush)});
	expect(makeLevelMapSelectionStructural(&quake2, LevelStructuralPolicy::DetailOnly, &report, &error) && report.flagsCleared == 1 && report.movedToWorld == 0
			&& ownerOf(quake2, detailBrush) != quake2World && !hasDetail(quake2, detailBrush),
		"the detail policy only clears the flag", error);
	expect(undoLevelMapEdit(&quake2, &error) && bytesOf(quake2) == quake2Before, "undo restores the flag", error);
	select(&quake2, {brush(detailBrush)});
	expect(makeLevelMapSelectionStructural(&quake2, LevelStructuralPolicy::LeaveEntities, &report, &error) && report.flagsCleared == 1 && report.movedToWorld == 1
			&& report.changed == 1 && ownerOf(quake2, detailBrush) == quake2World && !hasDetail(quake2, detailBrush),
		"the TrenchBroom policy moves the brush and clears its flag", error);
	expect(quake2.undoStack.size() == 1, "both changes are one undo step");
	const LevelMapDocument quake2Reopened = loadBytes(bytesOf(quake2), QStringLiteral("quake2"));
	const int reopenedBrush = brushNamed(quake2Reopened, QStringLiteral("studio/c"));
	expect(ownerOf(quake2Reopened, reopenedBrush) == entityOfClass(quake2Reopened, QStringLiteral("worldspawn")) && !hasDetail(quake2Reopened, reopenedBrush)
			&& entitiesOfClass(quake2Reopened, QStringLiteral("func_door")).isEmpty(),
		"the structural world brush survives a reopen");
	expect(undoLevelMapEdit(&quake2, &error) && bytesOf(quake2) == quake2Before, "undo restores the door and the flag", error);

	// The open group takes a brush leaving its entity.
	Fixture open = fixture();
	QString doors;
	expect(createLevelSceneNode(&open.map, LevelSceneNodeKind::Group, QStringLiteral("Doors"), {}, &doors, &error)
			&& assignLevelSceneObjects(&open.map, doors, {QStringLiteral("entity:%1").arg(open.door)}, &error) && openLevelSceneGroup(&open.map, doors, &error),
		"open group fixture", error);
	select(&open.map, {brush(open.d2)});
	expect(makeLevelMapSelectionStructural(&open.map, LevelStructuralPolicy::LeaveEntities, &report, &error) && ownerOf(open.map, open.d2) == open.world
			&& levelSceneMembership(open.map.scene, QStringLiteral("brush:%1").arg(open.d2)) == doors,
		"a brush leaving its entity stays in the open group", error);
}

void explode()
{
	Fixture f = fixture();
	const QByteArray original = bytesOf(f.map);
	select(&f.map, {brush(f.d2)});
	LevelExplodeReport report;
	QString error;
	expect(levelMapSelectionExplodable(f.map), "menus offer Explode Entity");
	expect(explodeLevelMapSelectionEntities(&f.map, {}, &report, &error), "explode the door", error);
	expect(report.explodedEntities == QVector<int> {f.door} && report.createdEntities.size() == 2 && report.movedPrimitives == 2, "the report counts two copies");
	const QVector<int> doors = entitiesOfClass(f.map, QStringLiteral("func_door"));
	expect(doors.size() == 3 && ownerOf(f.map, f.d1) == f.door && owned(f.map, f.door) == 1, "the door keeps its first brush");
	for (const int copy : std::as_const(report.createdEntities)) {
		expect(owned(f.map, copy) == 1 && keyOf(f.map, copy, QStringLiteral("speed")) == QStringLiteral("100")
				&& keyOf(f.map, copy, QStringLiteral("targetname")) == QStringLiteral("door1"),
			"each copy holds one brush with identical keys");
	}
	expect(ownerOf(f.map, f.d2) == report.createdEntities.at(0) && ownerOf(f.map, f.d3) == report.createdEntities.at(1), "brushes keep their ids");
	expect(f.map.selection == QVector<LevelMapSelectionRef> {entity(f.door), entity(report.createdEntities.at(0)), entity(report.createdEntities.at(1))},
		"the pieces are selected");
	expect(f.map.undoStack.size() == 1, "exploding is one undo step");
	const QByteArray exploded = bytesOf(f.map);
	expect(undoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == original && f.map.selection == QVector<LevelMapSelectionRef> {brush(f.d2)},
		"undo gathers the pieces back", error);
	expect(redoLevelMapEdit(&f.map, &error) && bytesOf(f.map) == exploded, "redo explodes again", error);
	const LevelMapDocument reopened = loadBytes(exploded, QStringLiteral("quake"));
	const QVector<int> reopenedDoors = entitiesOfClass(reopened, QStringLiteral("func_door"));
	bool single = reopenedDoors.size() == 3;
	for (const int door : reopenedDoors) {
		single = single && owned(reopened, door) == 1 && keyOf(reopened, door, QStringLiteral("targetname")) == QStringLiteral("door1");
	}
	expect(single, "three one-brush doors with identical keys survive a reopen");

	// Unique names on request; the original keeps its own.
	Fixture renamed = fixture();
	select(&renamed.map, {entity(renamed.door)});
	LevelExplodeRequest request;
	request.renameTargetNames = true;
	expect(explodeLevelMapSelectionEntities(&renamed.map, request, &report, &error), "explode with unique names", error);
	QSet<QString> names {keyOf(renamed.map, renamed.door, QStringLiteral("targetname"))};
	for (const int copy : std::as_const(report.createdEntities)) {
		names.insert(keyOf(renamed.map, copy, QStringLiteral("targetname")));
		expect(keyOf(renamed.map, copy, QStringLiteral("speed")) == QStringLiteral("100"), "other keys stay identical");
	}
	expect(keyOf(renamed.map, renamed.door, QStringLiteral("targetname")) == QStringLiteral("door1") && names.size() == 3, "each piece has its own targetname");

	// Refusals.
	Fixture refused = fixture();
	const QByteArray before = bytesOf(refused.map);
	select(&refused.map, {brush(refused.floor)});
	expect(!levelMapSelectionExplodable(refused.map) && !explodeLevelMapSelectionEntities(&refused.map, {}, nullptr, &error) && error.contains(QStringLiteral("Worldspawn")),
		"worldspawn cannot be exploded", error);
	select(&refused.map, {brush(refused.c1)});
	expect(!explodeLevelMapSelectionEntities(&refused.map, {}, nullptr, &error) && !error.isEmpty(), "a one-brush entity has nothing to explode", error);
	select(&refused.map, {entity(refused.light)});
	expect(!explodeLevelMapSelectionEntities(&refused.map, {}, nullptr, &error) && !error.isEmpty(), "a point entity cannot be exploded", error);
	expect(bytesOf(refused.map) == before && refused.map.undoStack.isEmpty(), "refusals leave the map and history alone");
}

void cli(const QString& executable)
{
	QTemporaryDir temp;
	if (!expect(temp.isValid(), "CLI temporary directory")) {
		return;
	}
	const Fixture f = fixture();
	const QString path = temp.filePath(QStringLiteral("ownership.map"));
	QFile file(path);
	const QByteArray source = bytesOf(f.map);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "CLI fixture")) {
		return;
	}
	file.close();
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process;
		process.setWorkingDirectory(temp.path());
		process.start(executable, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
									  temp.filePath(QStringLiteral("settings.ini")), QStringLiteral("map")}
								+ args);
		const bool done = process.waitForStarted() && process.waitForFinished(60000);
		const QByteArray out = process.readAllStandardOutput();
		const QByteArray err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code, "CLI exit status", args.join(QLatin1Char(' ')) + QStringLiteral(": ") + QString::fromUtf8(out + err));
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
	const QString ref = QStringLiteral("brush:%1");
	QString output = temp.filePath(QStringLiteral("moved.map"));
	QJsonObject report = run({QStringLiteral("move-to-entity"), path, QStringLiteral("--object"), ref.arg(f.w1), QStringLiteral("--object"), ref.arg(f.c1),
		QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("target")).toString() == QStringLiteral("entity:%1").arg(f.door) && report.value(QStringLiteral("moved")).toInt() == 2
			&& report.value(QStringLiteral("removedEntities")).toArray() == QJsonArray {QStringLiteral("entity:%1").arg(f.wall)},
		"CLI move-to-entity reports the last-selected target");
	LevelMapDocument moved = load(output);
	expect(owned(moved, entityOfClass(moved, QStringLiteral("func_door"))) == 5 && entitiesOfClass(moved, QStringLiteral("func_wall")).isEmpty(), "CLI output holds the moved brushes");
	output = temp.filePath(QStringLiteral("first.map"));
	report = run({QStringLiteral("move-to-entity"), path, QStringLiteral("--object"), ref.arg(f.c1), QStringLiteral("--object"), ref.arg(f.d1),
		QStringLiteral("--target"), QStringLiteral("first"), QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("target")).toString() == QStringLiteral("entity:%1").arg(f.wall), "CLI --target first picks the func_wall");
	output = temp.filePath(QStringLiteral("named.map"));
	report = run({QStringLiteral("move-to-entity"), path, QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--target"), QStringLiteral("entity:%1").arg(f.world),
		QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("targetClass")).toString() == QStringLiteral("worldspawn"), "CLI --target entity:N names worldspawn");
	run({QStringLiteral("move-to-entity"), path, QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--target"), QStringLiteral("middle"), QStringLiteral("--output"), output}, 2);
	run({QStringLiteral("move-to-entity"), path, QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--object"), QStringLiteral("entity:%1").arg(f.light),
			QStringLiteral("--output"), temp.filePath(QStringLiteral("refused.map"))},
		1);
	expect(!QFileInfo::exists(temp.filePath(QStringLiteral("refused.map"))), "a refused CLI move writes nothing");

	output = temp.filePath(QStringLiteral("selected.map"));
	report = run({QStringLiteral("move-to-world"), path, QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--selected-only"), QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("selectedOnly")).toBool() && report.value(QStringLiteral("moved")).toInt() == 1, "CLI move-to-world --selected-only");
	LevelMapDocument selected = load(output);
	expect(owned(selected, entityOfClass(selected, QStringLiteral("func_door"))) == 2, "CLI selected-only keeps the door's other brushes");

	output = temp.filePath(QStringLiteral("exploded.map"));
	report = run({QStringLiteral("explode-entity"), path, QStringLiteral("--object"), QStringLiteral("entity:%1").arg(f.door), QStringLiteral("--rename-targetnames"),
		QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("createdEntities")).toArray().size() == 2 && report.value(QStringLiteral("renamedTargetnames")).toBool(), "CLI explode-entity");
	LevelMapDocument pieces = load(output);
	QSet<QString> names;
	for (const int door : entitiesOfClass(pieces, QStringLiteral("func_door"))) {
		names.insert(keyOf(pieces, door, QStringLiteral("targetname")));
	}
	expect(entitiesOfClass(pieces, QStringLiteral("func_door")).size() == 3 && names.size() == 3, "CLI exploded pieces with unique names");
	run({QStringLiteral("explode-entity"), path, QStringLiteral("--object"), ref.arg(f.floor), QStringLiteral("--output"), temp.filePath(QStringLiteral("world.map"))}, 1);

	output = temp.filePath(QStringLiteral("structural.map"));
	report = run({QStringLiteral("detail"), path, QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--structural"), QStringLiteral("--policy"), QStringLiteral("trenchbroom"),
		QStringLiteral("--output"), output});
	expect(report.value(QStringLiteral("policy")).toString() == QStringLiteral("trenchbroom") && report.value(QStringLiteral("movedToWorld")).toInt() == 1,
		"CLI Make Structural with the TrenchBroom policy");
	LevelMapDocument structuralMap = load(output);
	expect(owned(structuralMap, entityOfClass(structuralMap, QStringLiteral("func_door"))) == 2, "CLI TrenchBroom structural moved one brush");
	run({QStringLiteral("detail"), path, QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--structural"), QStringLiteral("--output"), temp.filePath(QStringLiteral("plain.map"))}, 1);
	run({QStringLiteral("detail"), path, QStringLiteral("--object"), ref.arg(f.d1), QStringLiteral("--policy"), QStringLiteral("trenchbroom"), QStringLiteral("--output"),
			temp.filePath(QStringLiteral("bad.map"))},
		2);
	run({QStringLiteral("move-to-entity")}, 2);
	run({QStringLiteral("explode-entity")}, 2);
	QFile check(path);
	expect(check.open(QIODevice::ReadOnly) && check.readAll() == source, "the CLI never rewrites its input");
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	moveToEntity();
	moveSelectedToWorld();
	sceneAndLocks();
	structural();
	explode();
	if (argc == 2) {
		cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath());
	}
	std::cout << (ok ? "Entity ownership checks passed" : "Entity ownership checks failed") << std::endl;
	return ok ? 0 : 1;
}
