#include "core/level_placement.h"
#include "core/level_editor_controls.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_prefab_test_helpers.h"
#include <QCoreApplication>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {}) {
	if (!value) { ok = false; std::cerr << label << ": " << error.toStdString() << '\n'; } return value;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	LevelMapDocument draft, source; QString error;
	if (!expect(tests::createGeometryFixture(64, &draft, &error)
		&& loadLevelMapBytes({"delete.map",{},{}}, serializeLevelMap(draft).bytes, &source, &error), "bound source", error)) { return 1; }
	QVector<LevelMapSelectionRef> selected;
	for (const auto& brush : source.brushes) { selected << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,brush.id}; }
	setLevelMapSelection(&source, selected);
	const auto original = serializeLevelMap(source).bytes;
	const auto revision = source.revision, history = source.historyRevision;
	for (auto operation : {LevelPlacementOperation::DeleteObjects, LevelPlacementOperation::RemoveComponents}) {
		LevelPlacementRequest request; request.operation = operation;
		for (const auto& ref : selected) { request.components << LevelMapComponentSet{ref,LevelBrushComponent::Vertex,{7}}; }
		for (auto phase : {LevelPlacementPhase::Preparing, LevelPlacementPhase::Deleting, LevelPlacementPhase::Finalizing, LevelPlacementPhase::Complete}) {
			bool stopped = false;
			LevelPlacementControl control;
			control.isCancelled = [&] { return stopped; };
			control.progress = [&](const auto& p) { if (p.phase == phase && (phase != LevelPlacementPhase::Deleting || p.completed >= 8)) { stopped = true; } };
			const auto result = prepareLevelPlacement(source, request, control);
			expect(stopped && result.cancelled && !result.succeeded && result.document.format == LevelMapFormat::Unknown
				&& result.componentReport.brushes == 0 && result.componentReport.removedVertices == 0, "cancel discards candidate and report", result.error);
			expect(source.revision == revision && source.historyRevision == history && source.selection == selected
				&& serializeLevelMap(source).bytes == original, "cancellation preserves bytes, selection and history");
		}
		auto result = prepareLevelPlacement(source, request);
		if (!expect(result.succeeded && result.document.undoStack.size() == source.undoStack.size()+1, "single deletion transaction", result.error)) { continue; }
		if (operation == LevelPlacementOperation::DeleteObjects) {
			expect(result.document.brushes.isEmpty() && result.document.entities.size() == 1, "delete all brushes retains worldspawn");
		} else {
			expect(result.componentReport.brushes == 64 && result.componentReport.removedVertices == 64, "multi-brush report");
			for (const auto& brush : result.document.brushes) {
				LevelBrushTopology topology; expect(levelBrushTopology(brush, &topology) && topology.vertices.size() == 7, "all remaining hulls valid");
			}
		}
		const auto after = serializeLevelMap(result.document).bytes;
		expect(undoLevelMapEdit(&result.document, &error) && serializeLevelMap(result.document).bytes == original
			&& result.document.selection == selected && redoLevelMapEdit(&result.document, &error)
			&& serializeLevelMap(result.document).bytes == after, "exact undo/redo including selection", error);
	}
	LevelPlacementRequest remove; remove.operation = LevelPlacementOperation::RemoveComponents;
	remove.components = {{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Vertex,{7}},
		{{LevelMapSelectionKind::QuakeBrush,1},LevelBrushComponent::Face,{0}}};
	const auto invalid = prepareLevelPlacement(source, remove);
	expect(!invalid.succeeded && !invalid.cancelled && invalid.componentReport.removedVertices == 0
		&& serializeLevelMap(source).bytes == original && source.historyRevision == history, "invalid second hull refuses entire batch", invalid.error);
	remove.components[1].kind = LevelBrushComponent::Vertex;
	QString group;
	expect(createLevelSceneNode(&source, LevelSceneNodeKind::Group, "Protected", {}, &group, &error)
		&& assignLevelSceneObjects(&source, group, {"brush:1"}, &error)
		&& setLevelSceneLocked(&source, group, true, &error), "protect deletion target", error);
	const auto locked = serializeLevelMap(source).bytes;
	for (auto operation : {LevelPlacementOperation::DeleteObjects, LevelPlacementOperation::RemoveComponents}) {
		remove.operation = operation; const auto result = prepareLevelPlacement(source, remove);
		expect(!result.succeeded && !result.cancelled && serializeLevelMap(source).bytes == locked, "locked target refuses entire operation", result.error);
	}
	LevelMapDocument owned;
	expect(tests::loadPrefabFixture(&owned, &error), "owned brush fixture", error);
	const auto entities = owned.entities.size();
	LevelPlacementRequest objects; objects.operation = LevelPlacementOperation::DeleteObjects;
	selectLevelMapObject(&owned,"brush:0"); auto first = prepareLevelPlacement(owned,objects);
	expect(first.succeeded && first.document.entities.size() == entities && first.document.brushes.size() == 1, "retain owner with surviving primitive", first.error);
	if (first.succeeded) {
		selectLevelMapObject(&first.document,"brush:1"); auto last = prepareLevelPlacement(first.document,objects);
		expect(last.succeeded && last.document.entities.size() == entities-1 && last.document.brushes.isEmpty(), "delete empty non-world owner", last.error);
	}
	const auto tb = trenchBroomLevelControls(), nrc = netRadiantCustomLevelControls();
	expect(tb.deleteRemovesClipPoint && !tb.deleteObjectsWithoutComponents && !tb.deleteIgnoresPatchPoints, "TB delete policy");
	expect(nrc.deleteObjectsWithoutComponents && nrc.deleteIgnoresPatchPoints && !nrc.deleteRemovesClipPoint, "NRC delete policy");
	return ok ? 0 : 1;
}
