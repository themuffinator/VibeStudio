#include "core/level_history_p.h"
#include "core/level_scene.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* text, const QString& detail = {})
{
	if (!value) { std::cerr << "FAIL: " << text << ": " << detail.toStdString() << '\n'; }
	return value;
}
const LevelMapSelectionRef a{LevelMapSelectionKind::QuakeBrush, 0}, b{LevelMapSelectionKind::QuakeBrush, 1}, c{LevelMapSelectionKind::QuakeBrush, 2};
bool clean(const LevelMapDocument& map) { return map.editState == QStringLiteral("saved"); }
bool fixture(LevelMapDocument* map, QString* error)
{
	if (!tests::createGeometryFixture(3, map, error)) { return false; }
	markLevelMapSaved(map);
	return true;
}
bool selections()
{
	LevelMapDocument map; QString error; bool ok = true;
	if (!fixture(&map, &error)) { return false; }
	const auto bytes = serializeLevelMap(map).bytes;
	const auto scene = map.scene;
	const auto revision = map.revision, history = map.historyRevision;
	ok &= expect(changeLevelMapSelection(&map, {a, b, a}, &error) && map.selection == QVector<LevelMapSelectionRef>{b, a}
		&& map.selectedObjectId == a.objectId && map.brushes[0].selected && map.brushes[1].selected,
		"selection deduplicates and retains primary", error);
	ok &= expect(clean(map) && map.undoStack.size() == 1 && map.revision == revision && map.historyRevision == history + 1
		&& serializeLevelMap(map).bytes == bytes && map.scene == scene, "selection history leaves content, scene and geometry revision unchanged");
	ok &= expect(levelMapHistoryMatchesSaved(map, 0) && levelMapHistoryMatchesSaved(map, 1)
		&& !levelMapHistoryMatchesSaved(map, -1) && !levelMapHistoryMatchesSaved(map, 2), "saved equivalence validates timeline bounds");
	ok &= expect(undoLevelMapEdit(&map, &error) && map.selection.isEmpty() && clean(map) && map.revision == revision, "undo selection stays clean", error);
	const auto undone = map.historyRevision;
	ok &= expect(changeLevelMapSelection(&map, {}, &error) && map.redoStack.size() == 1 && map.historyRevision == undone,
		"no-op preserves redo and history revision");
	ok &= expect(!changeLevelMapSelection(&map, {a, {LevelMapSelectionKind::QuakeBrush, 999}}, &error)
		&& map.selection.isEmpty() && map.redoStack.size() == 1 && map.historyRevision == undone,
		"invalid selection is atomic and preserves redo", error);
	ok &= expect(redoLevelMapEdit(&map, &error) && map.selection == QVector<LevelMapSelectionRef>{b, a}
		&& map.revision == revision && clean(map), "redo restores order and primary", error);
	ok &= expect(changeLevelMapSelection(&map, {a, b}, &error) && map.selectedObjectId == b.objectId
		&& undoLevelMapEdit(&map, &error) && map.selectedObjectId == a.objectId, "primary-only change is undoable", error);
	ok &= expect(changeLevelMapSelection(&map, {c}, &error) && map.redoStack.isEmpty() && clean(map), "new selection branches history without dirtying content", error);
	// A stale undo target must not partially restore either selection or context.
	map.undoStack.last().selectionSnapshot = {a, {LevelMapSelectionKind::QuakeBrush, 999}};
	const auto before = map;
	ok &= expect(!undoLevelMapEdit(&map, &error) && map.selection == before.selection && map.openSceneGroups == before.openSceneGroups
		&& map.undoStack.size() == before.undoStack.size() && map.historyRevision == before.historyRevision, "failed replay keeps live state and timeline", error);
	return ok;
}
bool savePoints()
{
	LevelMapDocument map; QString error; bool ok = true;
	if (!fixture(&map, &error)) { return false; }
	changeLevelMapSelection(&map, {a}); moveLevelMapSelection(&map, 16, 0, 0); changeLevelMapSelection(&map, {b}); markLevelMapSaved(&map);
	const auto saved = serializeLevelMap(map).bytes;
	ok &= expect(undoLevelMapEdit(&map, &error) && clean(map) && serializeLevelMap(map).bytes == saved,
		"undo selection across save leaves saved contents", error);
	ok &= expect(undoLevelMapEdit(&map, &error) && !clean(map), "undo content crosses save point", error);
	ok &= expect(redoLevelMapEdit(&map, &error) && clean(map) && serializeLevelMap(map).bytes == saved,
		"redo content reaches equivalent saved state before selection step", error);
	ok &= expect(changeLevelMapSelection(&map, {c}, &error) && clean(map) && map.redoStack.isEmpty()
		&& undoLevelMapEdit(&map, &error) && clean(map), "discarding only saved selection redo preserves save point", error);
	undoLevelMapEdit(&map); // Undo the move, leaving the saved content in redo.
	ok &= expect(changeLevelMapSelection(&map, {b}, &error) && map.savedUndoDepth < 0 && !clean(map),
		"selection branch invalidates unreachable saved content", error);
	while (!map.undoStack.isEmpty()) { if (!undoLevelMapEdit(&map, &error)) { return false; } }
	ok &= expect(map.editState == QStringLiteral("modified"), "exhausted history cannot make an unreachable save look clean");
	LevelMapCreateRequest request; request.starterRoom = false;
	createLevelMap(request, &map, &error);
	ok &= expect(changeLevelMapSelection(&map, {{LevelMapSelectionKind::Entity, 0}}, &error) && undoLevelMapEdit(&map, &error)
		&& map.editState == QStringLiteral("modified"), "new unsaved document remains modified after selection undo", error);
	return ok;
}
bool boundedAndCompound()
{
	LevelMapDocument map; QString error; bool ok = true;
	if (!fixture(&map, &error)) { return false; }
	map.undoLimit = 1;
	for (const auto& ref : {a, b, c}) { ok &= changeLevelMapSelection(&map, {ref}, &error); }
	ok &= expect(map.undoStack.size() == 1 && map.savedUndoDepth == 0 && clean(map) && undoLevelMapEdit(&map, &error)
		&& map.selection == QVector<LevelMapSelectionRef>{b} && clean(map), "evicting selection-only history keeps clean baseline", error);
	changeLevelMapSelection(&map, {a}); moveLevelMapSelection(&map, 16, 0, 0); changeLevelMapSelection(&map, {b});
	ok &= expect(map.savedUndoDepth == -1 && undoLevelMapEdit(&map, &error) && !clean(map), "evicting content invalidates baseline", error);
	for (bool followers : {false, true}) {
		for (int kind = 0; kind < 3; ++kind) {
			fixture(&map, &error);
			changeLevelMapSelection(&map, {a});
			if (kind == 1) { moveLevelMapSelection(&map, 16, 0, 0); }
			markLevelMapSaved(&map);
			if (kind == 2) { moveLevelMapSelection(&map, 16, 0, 0); }
			changeLevelMapSelection(&map, {b});
			const int size = static_cast<int>(map.undoStack.size());
			ok &= expect(followers ? foldLevelMapFollowUpEdits(&map, size - 1, &error)
				: collapseLevelMapUndoSteps(&map, size, QStringLiteral("Compound"), QStringLiteral("Undo compound"), &error), "fold selection and content", error);
			ok &= expect(map.undoStack.size() == 1 && (clean(map) == (kind != 2)), "fold preserves equivalent saved boundary");
			ok &= expect(undoLevelMapEdit(&map, &error) && map.selection.isEmpty() && (clean(map) == (kind != 1)), "compound undo clean state", error);
			ok &= expect(redoLevelMapEdit(&map, &error) && map.selection == QVector<LevelMapSelectionRef>{b}
				&& (clean(map) == (kind != 2)), "compound redo and followers account for content changes", error);
		}
	}
	return ok;
}
bool groupsAndDeletion()
{
	LevelMapDocument map; QString error; bool ok = true; QString outer, inner, layer;
	if (!fixture(&map, &error)) { return false; }
	createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Work"), {}, &layer);
	createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Outer"), layer, &outer);
	createLevelSceneNode(&map, LevelSceneNodeKind::Group, QStringLiteral("Inner"), outer, &inner);
	assignLevelSceneObjects(&map, inner, {QStringLiteral("brush:0")}); assignLevelSceneObjects(&map, outer, {QStringLiteral("brush:1")});
	map.activeSceneNode = layer; map.undoStack.clear(); map.redoStack.clear(); markLevelMapSaved(&map);
	changeLevelMapSelection(&map, {a, b});
	const auto bytes = serializeLevelMap(map).bytes;
	ok &= expect(changeLevelSceneGroupContext(&map, inner, false, &error) && map.openSceneGroups == QStringList{outer, inner}
		&& map.selection.isEmpty() && map.activeSceneNode == inner && clean(map), "open nested group records session context", error);
	changeLevelMapSelection(&map, {a});
	ok &= expect(changeLevelSceneGroupContext(&map, {}, true, &error) && map.openSceneGroups.isEmpty() && map.activeSceneNode == layer
		&& map.selection.contains(a) && map.selection.contains(b) && serializeLevelMap(map).bytes == bytes, "close all restores creation destination", error);
	ok &= expect(undoLevelMapEdit(&map, &error) && map.openSceneGroups == QStringList{outer, inner} && map.activeSceneNode == inner
		&& map.selection == QVector<LevelMapSelectionRef>{a} && clean(map), "undo close restores exact group and selection", error);
	undoLevelMapEdit(&map); undoLevelMapEdit(&map);
	ok &= expect(map.openSceneGroups.isEmpty() && map.activeSceneNode == layer && map.selection == QVector<LevelMapSelectionRef>{a, b}
		&& clean(map), "undo open restores closed group selection and prior destination");
	const auto before = map.historyRevision;
	ok &= expect(!changeLevelSceneGroupContext(&map, QStringLiteral("missing"), false, &error) && map.historyRevision == before
		&& map.redoStack.size() == 3, "invalid context change preserves redo", error);
	// Content edits still restore stable object references before a selection undo.
	fixture(&map, &error); changeLevelMapSelection(&map, {a}); changeLevelMapSelection(&map, {b});
	const auto baseline = serializeLevelMap(map).bytes;
	ok &= expect(deleteLevelMapSelection(&map, &error) && map.brushes.size() == 2, "delete selected brush", error);
	ok &= expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == baseline, "undo deletion restores content", error);
	ok &= expect(map.selection == QVector<LevelMapSelectionRef>{b}, "undo deletion restores selected brush");
	ok &= expect(undoLevelMapEdit(&map, &error) && map.selection == QVector<LevelMapSelectionRef>{a} && clean(map), "undo selection after deletion undo", error);
	return ok;
}
bool formatsAndScale()
{
	bool ok = true; QString error; QTemporaryDir temp;
	for (const auto& game : {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3"), QStringLiteral("doom"), QStringLiteral("hexen")}) {
		LevelMapDocument map; LevelMapCreateRequest request; request.game = game;
		if (!createLevelMap(request, &map, &error)) { return expect(false, "create format", error); }
		markLevelMapSaved(&map); const auto bytes = serializeLevelMap(map).bytes;
		const int nodes = map.doomGeometryEdits;
		const auto ref = map.format == LevelMapFormat::DoomWad ? LevelMapSelectionRef{LevelMapSelectionKind::DoomThing, 0} : a;
		ok &= expect(changeLevelMapSelection(&map, {ref}, &error) && clean(map) && map.doomGeometryEdits == nodes
			&& serializeLevelMap(map).bytes == bytes && undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == bytes,
			"selection history preserves map format, material bytes and Doom node state", error);
		const auto path = temp.filePath(game + (map.format == LevelMapFormat::DoomWad ? QStringLiteral(".wad") : QStringLiteral(".map")));
		auto report = saveLevelMapAs(map, path, false, false);
		adoptLevelDocumentSave(&map, report);
		ok &= expect(report.succeeded() && redoLevelMapEdit(&map, &error) && clean(map),
			"save adoption preserves selection-only redo", error);
	}
	LevelMapDocument dense;
	for (bool followers : {false, true}) {
		LevelMapCreateRequest request; request.game = QStringLiteral("doom");
		LevelMapDocument created, doom;
		createLevelMap(request, &created, &error);
		if (!loadLevelMapBytes({QStringLiteral("nodes.wad"), QStringLiteral("MAP01"), {}}, serializeLevelMap(created).bytes, &doom, &error)) { return false; }
		changeLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomVertex, 0}});
		moveLevelMapSelection(&doom, 16, 0, 0); moveLevelMapSelection(&doom, 16, 0, 0);
		ok &= expect(doom.doomGeometryEdits == 2, "Doom geometry edits counted before grouping");
		if (followers) { foldLevelMapFollowUpEdits(&doom, 2); }
		else { collapseLevelMapUndoSteps(&doom, 3, QStringLiteral("Compound"), QStringLiteral("Undo compound")); }
		ok &= expect(undoLevelMapEdit(&doom, &error) && doom.doomGeometryEdits == 0 && !doom.doomGeometryChanged
			&& redoLevelMapEdit(&doom, &error) && doom.doomGeometryEdits == 2 && doom.doomGeometryChanged,
			"Doom node validity follows content in selection-led compounds", error);
	}
	if (!tests::createGeometryFixture(4096, &dense, &error)) { return false; }
	markLevelMapSaved(&dense); QElapsedTimer timer; timer.start();
	QVector<LevelMapSelectionRef> all;
	for (const auto& brush : dense.brushes) { all.append({LevelMapSelectionKind::QuakeBrush, brush.id}); }
	ok &= expect(changeLevelMapSelection(&dense, all, &error) && undoLevelMapEdit(&dense, &error) && redoLevelMapEdit(&dense, &error)
		&& dense.selection.size() == 4096 && clean(dense), "dense selection round trip", error);
	std::cout << "4096 brush selection/undo/redo: " << timer.elapsed() << " ms\n";
	return ok;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = selections(); ok &= savePoints(); ok &= boundedAndCompound(); ok &= groupsAndDeletion(); ok &= formatsAndScale();
	return ok ? 0 : 1;
}
