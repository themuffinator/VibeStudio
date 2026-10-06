#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_placement_test_helpers.h"
#include "tests/level_prefab_test_helpers.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool pass, const char* label, const QString& error = {}) {
	if (!pass) {
		std::cerr << label << ": " << error.toStdString() << '\n';
		ok = false;
	}
	return pass;
}
bool load(const QByteArray& bytes, LevelMapDocument* map, QString* error) {
	return loadLevelMapBytes({QStringLiteral("placement.map"), {}, QStringLiteral("idtech3")}, bytes, map, error);
}
void unchanged(const LevelMapDocument& before, const LevelMapDocument& after) {
	expect(serializeLevelMap(before).bytes == serializeLevelMap(after).bytes && before.selection == after.selection &&
			   before.scene == after.scene && before.revision == after.revision && before.undoStack.size() == after.undoStack.size() &&
			   before.redoStack.size() == after.redoStack.size() && before.savedUndoDepth == after.savedUndoDepth &&
			   before.editState == after.editState && before.doomGeometryEdits == after.doomGeometryEdits,
		   "refusal is atomic");
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	QString error;
	if (!temp.isValid()) {
		return 1;
	}
	for (const QString& kind :
		 {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		for (int operation = 0; operation < 3; ++operation) {
			LevelMapDocument map;
			if (!expect(load(tests::placementFixture(kind), &map, &error), "dialect fixture", error)) {
				return 1;
			}
			selectLevelMapObject(&map, QStringLiteral("brush:0"));
			const auto before = map;
			const auto bytes = serializeLevelMap(before).bytes;
			const auto text = levelMapSelectionText(map);
			const LevelMapVec3 delta = operation == 0 ? LevelMapVec3{-3, -3, 5, true} : LevelMapVec3{31, -17, 11, true};
			const bool applied = operation == 0	  ? snapLevelMapSelectionToGrid(&map, 16, {true, false}, &error)
								 : operation == 1 ? duplicateLevelMapSelection(&map, delta.x, delta.y, delta.z, {true, false}, &error)
												  : pasteLevelMapText(&map, text, delta, {true, false}, &error);
			if (!expect(applied, "placement", kind + QString::number(operation) + error)) {
				continue;
			}
			const auto& placed = operation == 0 ? map.brushes.first() : map.brushes.last();
			expect(tests::placementUvsMatch(before.brushes.first(), placed, delta), "locked UVs at all solved vertices", kind);
			expect(map.undoStack.size() == before.undoStack.size() + 1 && map.revision == before.revision + 1, "single edit");
			const auto after = serializeLevelMap(map);
			expect(after.succeeded() && (!bytes.contains("/* uv note */") || after.bytes.contains("/* uv note */")) &&
					   after.bytes.contains("// face note"),
				   "comments preserved", after.errors.join(';'));
			LevelMapDocument reloaded;
			expect(load(after.bytes, &reloaded, &error), "round trip", error);
			expect(tests::placementUvsMatch(before.brushes.first(), operation == 0 ? reloaded.brushes.first() : reloaded.brushes.last(),
											delta),
				   "saved UV oracle", kind);
			const auto selection = map.selection;
			expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == bytes && map.selection == before.selection,
				   "exact undo and selection", error);
			expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == after.bytes && map.selection == selection,
				   "exact redo and selection", error);
			map = before;
			if (operation == 0) {
				snapLevelMapSelectionToGrid(&map, 16, {false, false}, &error);
			} else if (operation == 1) {
				duplicateLevelMapSelection(&map, delta.x, delta.y, delta.z, {false, false}, &error);
			} else {
				pasteLevelMapText(&map, text, delta, {false, false}, &error);
			}
			const auto& unlocked = operation == 0 ? map.brushes.first() : map.brushes.last();
			expect(!tests::placementUvsMatch(before.brushes.first(), unlocked, delta), "unlocked policy is distinct", kind);
			const auto a = levelTextureProjection(before.brushes.first().faces[0]), b = levelTextureProjection(unlocked.faces[0]);
			expect(a.at({0, 0, 0, true}) == b.at({0, 0, 0, true}), "unlocked mapping stays in source frame");
		}
	}
	LevelMapDocument assembly;
	{
		LevelMapDocument seed, sparse;
		expect(tests::createGeometryFixture(48, &seed, &error) && load(serializeLevelMap(seed).bytes, &sparse, &error),
			   "bulk source fixture", error);
		QVector<LevelMapSelectionRef> removed, selected;
		for (const auto& brush : sparse.brushes) {
			(brush.id % 2 ? removed : selected).append({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
		expect(deleteLevelMapObjects(&sparse, removed, &error) && setLevelMapSelection(&sparse, selected, &error),
			   "sparse IDs after deletion", error);
		expect(moveLevelMapSelection(&sparse, 3, 3, 3, {true, false}, &error), "bulk off-grid source", error);
		const auto before = sparse;
		const auto original = serializeLevelMap(before).bytes;
		expect(snapLevelMapSelectionToGrid(&sparse, 16, {true, false}, &error), "bulk sparse snap", error);
		for (int index = 0; index < before.brushes.size(); ++index) {
			expect(sparse.brushes[index].id == before.brushes[index].id &&
					   tests::placementUvsMatch(before.brushes[index], sparse.brushes[index], {-3, -3, -3, true}),
				   "sparse IDs preserve geometry and UVs");
		}
		const auto snapped = serializeLevelMap(sparse).bytes;
		expect(undoLevelMapEdit(&sparse, &error) && serializeLevelMap(sparse).bytes == original && redoLevelMapEdit(&sparse, &error) &&
				   serializeLevelMap(sparse).bytes == snapped,
			   "sparse snap exact history", error);
		expect(duplicateLevelMapSelection(&sparse, 16, 0, 0, {true, false}, &error) && sparse.brushes.size() == before.brushes.size() * 2,
			   "bulk sparse duplicate", error);
		const auto duplicated = serializeLevelMap(sparse).bytes;
		expect(undoLevelMapEdit(&sparse, &error) && serializeLevelMap(sparse).bytes == snapped && redoLevelMapEdit(&sparse, &error) &&
				   serializeLevelMap(sparse).bytes == duplicated,
			   "bulk insertion exact history", error);
		// Two native brushes sharing face lines must still refuse atomically.
		sparse = before;
		auto overlapping = sparse.brushes.first();
		overlapping.id = 1000;
		sparse.brushes.append(overlapping);
		const auto ambiguous = sparse;
		expect(!snapLevelMapSelectionToGrid(&sparse, 16, {true, false}, &error) && error.contains("one line"),
			   "bulk source ownership conflict refused", error);
		unchanged(ambiguous, sparse);
	}
	{
		LevelMapDocument classic, valve;
		load(tests::placementFixture("classic"), &classic, &error);
		load(tests::placementFixture("valve220"), &valve, &error);
		selectLevelMapObject(&valve, "brush:0");
		const auto before = classic;
		expect(!pasteLevelMapText(&classic, levelMapSelectionText(valve), {16, 0, 0, true}, {true, false}, &error) &&
				   error.contains("dialects"),
			   "incompatible clipboard dialect refused", error);
		unchanged(before, classic);
	}
	expect(tests::loadPrefabFixture(&assembly, &error), "assembly fixture", error);
	for (const auto& bad : QStringList{QString(kLevelMapClipboardMaxCharacters + 1, ' '),
									   QStringLiteral("{\n") + QString(600, '(') + QString(600, ')') + "\n}",
									   QStringLiteral("{\n") + QChar::Null + QStringLiteral("\n}"), QString(9, '{') + QString(9, '}')}) {
		const auto before = assembly;
		expect(!pasteLevelMapText(&assembly, bad, {16, 0, 0, true}, {true, false}, &error), "bounded clipboard rejected", error);
		unchanged(before, assembly);
	}
	setLevelMapSelection(&assembly, {{LevelMapSelectionKind::Entity, 1},
									 {LevelMapSelectionKind::QuakeBrush, 0},
									 {LevelMapSelectionKind::QuakeBrush, 1},
									 {LevelMapSelectionKind::QuakePatch, 0},
									 {LevelMapSelectionKind::Entity, 3},
									 {LevelMapSelectionKind::Entity, 4}});
	expect(moveLevelMapSelection(&assembly, 3, 3, 3, {true, false}, &error), "move assembly off grid", error);
	const auto offGrid = assembly;
	const auto oldUv = assembly.patches.first().controlU;
	expect(snapLevelMapSelectionToGrid(&assembly, 16, {true, false}, &error), "snap overlapping owner and children", error);
	expect(!assembly.entities[1].origin.valid && assembly.brushes[0].mins.x == 0 && assembly.brushes[1].mins.x == 32,
		   "owner assembly translates once without a synthesized origin");
	expect(assembly.patches.first().controlU == oldUv && tests::prefabProperty(assembly.entities[3], "model") == "models/test.md3" &&
			   tests::prefabProperty(assembly.entities[4], "noise") == "sound/test.wav",
		   "patch UVs, model and audio references preserved");
	expect(undoLevelMapEdit(&assembly, &error) && serializeLevelMap(assembly).bytes == serializeLevelMap(offGrid).bytes,
		   "assembly exact undo", error);
	// Locked descendants veto the whole snap, even when their owner is selected.
	QString node;
	expect(createLevelSceneNode(&assembly, LevelSceneNodeKind::Group, "Assembly", {}, &node, &error) &&
			   assignLevelSceneObjects(&assembly, node, {"brush:1"}, &error) && setLevelSceneLocked(&assembly, node, true, &error),
		   "lock descendant", error);
	auto before = assembly;
	expect(!snapLevelMapSelectionToGrid(&assembly, 16, {true, false}, &error) && error.contains("Unlock"), "locked snap refused", error);
	unchanged(before, assembly);
	expect(!duplicateLevelMapSelection(&assembly, 16, 0, 0, {true, false}, &error) && error.contains("Unlock"), "locked copies refused",
		   error);
	unchanged(before, assembly);
	const auto copied = levelMapSelectionText(assembly, &error);
	expect(pasteLevelMapText(&assembly, copied, {128, 0, 0, true}, {true, false}, &error),
		   "locked originals can be copied into unlocked destination", error);
	expect(undoLevelMapEdit(&assembly, &error), "undo paste", error);
	assembly.activeSceneNode = node;
	before = assembly;
	expect(!pasteLevelMapText(&assembly, copied, {128, 0, 0, true}, {true, false}, &error), "locked destination refuses paste", error);
	unchanged(before, assembly);
	// Metadata follows duplication and the active creation destination.
	setLevelSceneLocked(&assembly, node, false, &error);
	selectLevelMapObject(&assembly, "brush:1");
	expect(duplicateLevelMapSelection(&assembly, 64, 0, 0, {true, false}, &error), "unlocked duplicate", error);
	expect(levelSceneMembership(assembly.scene, levelMapSelectionRefId(assembly.selection.first())) == node,
		   "copies inherit scene membership");
	for (double bad : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
		before = assembly;
		expect(!snapLevelMapSelectionToGrid(&assembly, bad, {true, false}, &error), "invalid grid refused", error);
		unchanged(before, assembly);
	}
	for (const LevelMapVec3 delta : {LevelMapVec3{70000, 0, 0, true}, LevelMapVec3{std::numeric_limits<double>::quiet_NaN(), 0, 0, true}}) {
		before = assembly;
		expect(!duplicateLevelMapSelection(&assembly, delta.x, delta.y, delta.z, {true, false}, &error), "invalid copy offset", error);
		unchanged(before, assembly);
		expect(!pasteLevelMapText(&assembly, copied, delta, {true, false}, &error), "invalid paste offset", error);
		unchanged(before, assembly);
	}
	for (const QString& game : {QStringLiteral("doom"), QStringLiteral("hexen")}) {
		LevelMapDocument doom;
		LevelMapCreateRequest request;
		request.game = game;
		expect(createLevelMap(request, &doom, &error), "Doom fixture", error);
		QVector<LevelMapSelectionRef> vertices;
		for (const auto& v : doom.doomVertices) {
			vertices << LevelMapSelectionRef{LevelMapSelectionKind::DoomVertex, v.id};
		}
		setLevelMapSelection(&doom, vertices);
		expect(moveLevelMapSelection(&doom, 3, 3, 0, &error), "offset sector", error);
		setLevelMapSelection(
			&doom,
			{{LevelMapSelectionKind::DoomSector, 0}, {LevelMapSelectionKind::DoomLinedef, 0}, {LevelMapSelectionKind::DoomVertex, 0}});
		const auto unsnapped = doom;
		expect(snapLevelMapSelectionToGrid(&doom, 16, {true, false}, &error), "snap shared boundary once", error);
		for (const auto& v : doom.doomVertices) {
			expect(std::fmod(v.x, 16) == 0 && std::fmod(v.y, 16) == 0, "boundary on grid");
		}
		expect(doom.undoStack.size() == unsnapped.undoStack.size() + 1 && doom.doomGeometryChanged, "one edit invalidates nodes");
		expect(undoLevelMapEdit(&doom, &error) && serializeLevelMap(doom).bytes == serializeLevelMap(unsnapped).bytes, "Doom exact undo",
			   error);
		before = doom;
		expect(!snapLevelMapSelectionToGrid(&doom, 10000, {true, false}, &error) && error.contains("collapse"),
			   "collapsed boundary refused", error);
		unchanged(before, doom);
		expect(!snapLevelMapSelectionToGrid(&doom, 1.5, {true, false}, &error), "fractional WAD grid refused", error);
		unchanged(before, doom);
		selectLevelMapObject(&doom, "thing:0");
		const auto thing = doom.doomThings.first();
		expect(duplicateLevelMapSelection(&doom, 1.7, -1.7, 3.8, {true, false}, &error), "duplicate WAD thing", error);
		expect(doom.doomThings.last().x == std::round(thing.x + 1.7) && doom.doomThings.last().y == std::round(thing.y - 1.7) &&
				   doom.doomThings.last().z == (game == "hexen" ? std::round(thing.z + 3.8) : thing.z),
			   "WAD preview matches saved rounding");
		const auto save = saveLevelMapAs(doom, temp.filePath(game + ".wad"));
		expect(save.succeeded(), "save Doom placement", save.errors.join(';'));
		LevelMapDocument reloaded;
		expect(loadLevelMap({save.outputPath, "MAP01", {}}, &reloaded, &error), "reload WAD placement", error);
		expect(reloaded.doomThings.last().x == doom.doomThings.last().x && reloaded.doomThings.last().z == doom.doomThings.last().z,
			   "WAD exact coordinate persistence");
	}
	return ok ? 0 : 1;
}
