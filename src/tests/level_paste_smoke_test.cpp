#include "core/level_linked_groups.h"
#include "core/level_paste.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_placement_test_helpers.h"
#include "tests/level_prefab_test_helpers.h"
#include <QCoreApplication>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool ok = true;
bool expect(bool pass, const char* label, const QString& error = {})
{
	if (!pass) {
		std::cerr << "FAIL: " << label << ": " << error.toStdString() << '\n';
		ok = false;
	}
	return pass;
}
bool near(LevelMapVec3 a, LevelMapVec3 b)
{
	return a.valid && b.valid && std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < 0.0001;
}
void unchanged(const LevelMapDocument& before, const LevelMapDocument& after)
{
	expect(serializeLevelMap(before).bytes == serializeLevelMap(after).bytes && before.selection == after.selection &&
	           before.scene == after.scene && before.activeSceneNode == after.activeSceneNode && before.revision == after.revision &&
	           before.repeat == after.repeat && before.undoStack.size() == after.undoStack.size() &&
	           before.redoStack.size() == after.redoStack.size(),
	       "failed placement is atomic");
}
LevelMapDocument empty()
{
	LevelMapDocument result;
	LevelMapCreateRequest request;
	request.starterRoom = false;
	expect(createLevelMap(request, &result), "create destination");
	return result;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	for (const QString& dialect :
	     {QStringLiteral("classic"), QStringLiteral("valve220"), QStringLiteral("brushDef"), QStringLiteral("brushDef3")}) {
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({"source.map", {}, "idtech3"}, tests::placementFixture(dialect), &source, &error),
		            "load clipboard fixture", error)) {
			return 1;
		}
		selectLevelMapObject(&source, "brush:0");
		const auto text = levelMapSelectionText(source, &error);
		for (const auto mode : {LevelPasteMode::Offset, LevelPasteMode::Camera, LevelPasteMode::Plane}) {
			auto document = empty();
			const auto before = document;
			LevelPlacementRequest request;
			request.operation = LevelPlacementOperation::Paste;
			request.text = text;
			request.textures.enabled = true;
			request.paste.mode = mode;
			request.paste.grid = 16;
			request.paste.point =
			    mode == LevelPasteMode::Camera ? LevelMapVec3{100, 200, 300, true} : LevelMapVec3{17.25, -8.2, 41.5, true};
			request.paste.normal = {0, 0, 1, true};
			request.paste.direction = {0.2, -0.2, -1, true};
			const auto result = prepareLevelPlacement(document, request);
			if (!expect(result.succeeded, "targeted dialect paste", result.error)) {
				continue;
			}
			const LevelMapVec3 expected = mode == LevelPasteMode::Offset   ? LevelMapVec3{0, 0, 0, true}
			                              : mode == LevelPasteMode::Camera ? LevelMapVec3{64, 144, 272, true}
			                                                               : LevelMapVec3{45, -83, 46.5, true};
			expect(near(result.pasteOffset, expected), "placement offset has reference semantics");
			document = result.document;
			expect(document.brushes.size() == 1 && document.undoStack.size() == before.undoStack.size() + 1 &&
			           document.selection.size() == 1,
			       "paste inserts once and selects result");
			expect(tests::placementUvsMatch(source.brushes.first(), document.brushes.first(), expected),
			       "locked UVs follow pasted geometry");
			expect(document.brushes.first().faces.first().contentFlags == 2 && document.brushes.first().faces.first().surfaceFlags == 4,
			       "face flags survive target placement");
			const auto after = serializeLevelMap(document).bytes;
			expect(undoLevelMapEdit(&document) && serializeLevelMap(document).bytes == serializeLevelMap(before).bytes &&
			           document.selection == before.selection,
			       "single undo restores destination");
			expect(redoLevelMapEdit(&document) && serializeLevelMap(document).bytes == after, "single redo restores placement");
			LevelMapDocument reopened;
			expect(loadLevelMapBytes({"reopened.map", {}, "idtech3"}, after, &reopened, &error) && reopened.brushes.size() == 1 &&
			           tests::placementUvsMatch(source.brushes.first(), reopened.brushes.first(), expected),
			       "save/reopen preserves placement and texture mapping", error);
			for (auto phase : {LevelPlacementPhase::Parsing, LevelPlacementPhase::Transforming, LevelPlacementPhase::Inserting,
			                   LevelPlacementPhase::Finalizing, LevelPlacementPhase::Complete}) {
				bool cancel = false, reached = false;
				const auto cancellationSource = before;
				const auto cancelled = prepareLevelPlacement(cancellationSource, request,
				                                             {[&] { return cancel; },
				                                              [&](const LevelPlacementProgress& progress) {
					                                              if (progress.phase == phase) {
						                                              cancel = reached = true;
					                                              }
				                                              }});
				expect(reached && cancelled.cancelled && !cancelled.succeeded, "cancellation discards candidate at every phase");
				unchanged(before, cancellationSource);
				expect(cancelled.document.brushes.isEmpty(), "cancelled result publishes no geometry");
			}
		}
	}
	auto source = empty();
	expect(addLevelMapBoxBrush(&source, {-16, -24, -32, true}, {16, 24, 32, true}, "studio/checker"), "seed box");
	selectLevelMapObject(&source, "brush:0");
	const auto text = levelMapSelectionText(source);
	auto destination = empty();
	expect(addLevelMapBoxBrush(&destination, {-128, -128, -16, true}, {128, 128, 0, true}, "studio/floor"), "target floor");
	LevelPlacementRequest ray;
	ray.operation = LevelPlacementOperation::Paste;
	ray.text = text;
	ray.paste.mode = LevelPasteMode::ViewRay;
	ray.paste.rayOrigin = {20, 35, 100, true};
	ray.paste.direction = {0, 0, -1, true};
	ray.paste.viewDirection = {0, 0, -1, true};
	ray.paste.grid = 16;
	auto placed = prepareLevelPlacement(destination, ray);
	expect(placed.succeeded && near(placed.pasteOffset, {32, 56, 32, true}), "ray hits original brush before insertion", placed.error);
	ray.paste.filterBrushes = true; // empty visible set excludes the floor
	placed = prepareLevelPlacement(destination, ray);
	expect(placed.succeeded && near(placed.pasteOffset, {32, 56, -128, true}), "hidden surface uses snapped 256-unit fallback",
	       placed.error);
	ray.paste.filterBrushes = false;
	ray.paste.rayOrigin = {20, 35, -100, true};
	ray.paste.direction = {0, 0, 1, true};
	ray.paste.viewDirection = {0, 0, 1, true};
	placed = prepareLevelPlacement(destination, ray);
	expect(placed.succeeded && near(placed.pasteOffset, {32, 56, -48, true}), "ceiling placement aligns assembly maximum", placed.error);
	// Direction magnitude must not change the fallback or plane corner.
	ray.paste.direction.z = 100;
	const auto scaledRay = prepareLevelPlacement(destination, ray);
	expect(scaledRay.succeeded && near(scaledRay.pasteOffset, placed.pasteOffset), "normalise incoming view direction");
	// An orbit view is an infinite line through its focus plane. The front
	// surface can be on either side; an empty line stays at the focus depth.
	auto orbit = ray;
	orbit.paste.rayForwardOnly = false;
	orbit.paste.direction = orbit.paste.viewDirection = {0, 0, -1, true};
	auto orbitResult = prepareLevelPlacement(destination, orbit);
	expect(orbitResult.succeeded && near(orbitResult.pasteOffset, {32, 56, 32, true}),
	       "orthographic target can precede the focus plane", orbitResult.error);
	orbit.paste.direction.x = -6e-17;
	orbitResult = prepareLevelPlacement(destination, orbit);
	expect(orbitResult.succeeded && near(orbitResult.pasteOffset, {32, 56, 32, true}),
	       "orbit pole roundoff does not select another grid corner", orbitResult.error);
	auto occluded = destination;
	expect(addLevelMapBoxBrush(&occluded, {-128, -128, 64, true}, {128, 128, 80, true}, "studio/above"), "nearer orbit target");
	orbitResult = prepareLevelPlacement(occluded, orbit);
	expect(orbitResult.succeeded && near(orbitResult.pasteOffset, {32, 56, 112, true}),
	       "orthographic target is the frontmost entry", orbitResult.error);
	orbit.paste.filterBrushes = true;
	orbit.paste.visibleBrushes = {0};
	orbitResult = prepareLevelPlacement(occluded, orbit);
	expect(orbitResult.succeeded && near(orbitResult.pasteOffset, {32, 56, 32, true}),
	       "orbit target respects the visible brush snapshot", orbitResult.error);
	orbit.paste.visibleBrushes.clear();
	orbitResult = prepareLevelPlacement(occluded, orbit);
	expect(orbitResult.succeeded && near(orbitResult.pasteOffset, {32, 56, -64, true}),
	       "orthographic miss uses snapped focus depth", orbitResult.error);
	// Definition bounds matter for asymmetric point classes.
	auto entitySource = empty();
	expect(addLevelMapEntity(&entitySource, "info_test", {11, 23, 37, true}, {}, nullptr, &error), "point clipboard", error);
	selectLevelMapObject(&entitySource, "entity:1");
	LevelPlacementRequest point;
	point.operation = LevelPlacementOperation::Paste;
	point.text = levelMapSelectionText(entitySource);
	point.paste.mode = LevelPasteMode::Camera;
	point.paste.point = {100, 200, 300, true};
	point.paste.entityBounds.insert("info_test", {{-4, -8, 0}, {12, 16, 40}});
	const auto pointResult = prepareLevelPlacement(empty(), point);
	expect(pointResult.succeeded && near(pointResult.pasteOffset, {85, 173, 243, true}), "point entity definition controls assembly centre",
	       pointResult.error);
	// Plan selection's far depth, including negative-facing front views.
	selectLevelMapObject(&destination, "brush:0");
	LevelPlacementRequest plan = ray;
	plan.paste.mode = LevelPasteMode::Plane;
	plan.paste.referenceAxis = 1;
	plan.paste.point = {17, 999, 49, true};
	plan.paste.normal = {0, -1, 0, true};
	plan.paste.direction = {0, 1, 0, true};
	const auto planResult = prepareLevelPlacement(destination, plan);
	expect(planResult.succeeded && near(planResult.pasteOffset, {32, 104, 80, true}),
	       "front view uses reference maximum and aligns pasted maximum", planResult.error);
	for (int malformed = 0; malformed < 7; ++malformed) {
		auto bad = ray;
		if (malformed == 0) {
			bad.paste.direction = {0, 0, 0, true};
		}
		if (malformed == 1) {
			bad.paste.grid = -1;
		}
		if (malformed == 2) {
			bad.paste.grid = std::numeric_limits<double>::quiet_NaN();
		}
		if (malformed == 3) {
			bad.paste.rayOrigin.x = std::numeric_limits<double>::infinity();
		}
		if (malformed == 4) {
			bad.paste.viewDirection = {0, 0, -1, true};
		}
		if (malformed == 5) {
			bad.paste.mode = LevelPasteMode::Camera;
			bad.paste.point = {1048576, 0, 0, true};
		}
		if (malformed == 6) {
			bad.text = "{ broken";
		}
		const auto before = destination;
		const auto result = prepareLevelPlacement(destination, bad);
		expect(!result.succeeded && !result.error.isEmpty(), "invalid target or input refuses");
		unchanged(before, destination);
		LevelMapVec3 output{3, 5, 7, true};
		expect(!pasteLevelMapTextAt(&destination, bad.text, bad.paste, {}, &error, &output) && near(output, {3, 5, 7, true}),
		       "direct refusal preserves output", error);
		unchanged(before, destination);
	}
	// Native closed-group transfer and destination locks use the same insertion
	// transaction even when the placement anchor is computed by the worker.
	QString group, sibling;
	expect(createLevelSceneNode(&source, LevelSceneNodeKind::Group, "Linked paste", {}, &group, &error) &&
	           assignLevelSceneObjects(&source, group, {"brush:0"}, &error) &&
	           createLinkedLevelGroup(&source, group, {128, 0, 0, true}, &sibling, &error),
	       "linked clipboard fixture", error);
	setLevelMapSelection(&source, {{LevelMapSelectionKind::QuakeBrush, 0}, {LevelMapSelectionKind::QuakeBrush, 1}});
	auto groupedDestination = empty();
	QString layer;
	expect(createLevelSceneNode(&groupedDestination, LevelSceneNodeKind::Layer, "Pasted assets", {}, &layer, &error), "destination layer",
	       error);
	groupedDestination.activeSceneNode = layer;
	LevelPlacementRequest grouped;
	grouped.operation = LevelPlacementOperation::Paste;
	grouped.text = levelMapSelectionText(source, &error);
	grouped.paste.mode = LevelPasteMode::Camera;
	grouped.paste.point = {100, 200, 300, true};
	const auto groupResult = prepareLevelPlacement(groupedDestination, grouped);
	expect(groupResult.succeeded && groupResult.document.scene.nodes.size() == 3 && groupResult.document.brushes.size() == 2 &&
	           groupResult.document.undoStack.size() == groupedDestination.undoStack.size() + 1,
	       "targeted paste transfers linked hierarchy once", groupResult.error);
	if (groupResult.succeeded) {
		const auto& a = groupResult.document.scene.nodes[1];
		const auto& b = groupResult.document.scene.nodes[2];
		expect(a.parentId == layer && b.parentId == layer && a.id != group && b.id != sibling && !a.linkId.isEmpty() &&
		           a.linkId == b.linkId && a.linkId != levelSceneNode(source.scene, group)->linkId,
		       "fresh linked family retains its internal relationship and destination");
		auto reopened = empty();
		expect(loadLevelMapBytes({"groups.map", {}, "idtech3"}, serializeLevelMap(groupResult.document).bytes, &reopened, &error) &&
		           validateLevelScene(reopened, reopened.scene, &error),
		       "linked paste saves valid native metadata", error);
		auto undo = groupResult.document;
		expect(undoLevelMapEdit(&undo, &error) && undo.scene == groupedDestination.scene &&
		           serializeLevelMap(undo).bytes == serializeLevelMap(groupedDestination).bytes,
		       "linked paste exact undo", error);
	}
	expect(setLevelSceneLocked(&groupedDestination, layer, true, &error), "lock destination", error);
	const auto locked = groupedDestination;
	LevelMapVec3 uncommitted{7, 8, 9, true};
	expect(!pasteLevelMapTextAt(&groupedDestination, grouped.text, grouped.paste, {}, &error, &uncommitted) &&
	           near(uncommitted, {7, 8, 9, true}),
	       "locked destination refuses geometry, hierarchy and output", error);
	unchanged(locked, groupedDestination);
	// Mixed owned brushes, patches, model and sound references move as one assembly.
	LevelMapDocument assembly;
	expect(loadLevelMapBytes({"assembly.map", {}, "idtech3"}, tests::prefabFixture(), &assembly, &error), "mixed clipboard fixture", error);
	setLevelMapSelection(&assembly, {{LevelMapSelectionKind::Entity, 1},
	                                 {LevelMapSelectionKind::QuakePatch, 0},
	                                 {LevelMapSelectionKind::Entity, 3},
	                                 {LevelMapSelectionKind::Entity, 4}});
	LevelPlacementRequest mixed;
	mixed.operation = LevelPlacementOperation::Paste;
	mixed.text = levelMapSelectionText(assembly, &error);
	mixed.paste.mode = LevelPasteMode::Camera;
	mixed.paste.point = {256, 128, 64, true};
	const auto mixedResult = prepareLevelPlacement(empty(), mixed);
	expect(mixedResult.succeeded && mixedResult.document.brushes.size() == 2 && mixedResult.document.patches.size() == 1 &&
	           mixedResult.document.patches.first().controlU == assembly.patches.first().controlU &&
	           mixedResult.document.patches.first().controlV == assembly.patches.first().controlV,
	       "mixed geometry and patch UVs survive targeted paste", mixedResult.error);
	if (mixedResult.succeeded) {
		bool model = false, sound = false;
		for (const auto& entity : mixedResult.document.entities) {
			model |= tests::prefabProperty(entity, "model") == "models/test.md3";
			sound |= tests::prefabProperty(entity, "noise") == "sound/test.wav";
		}
		expect(model && sound && mixedResult.document.brushes[0].entityId == mixedResult.document.brushes[1].entityId &&
		           mixedResult.document.brushes[0].entityId != 0,
		       "owner and cross-studio asset references retained");
	}
	std::cout << (ok ? "Paste bounds, targets, dialect UVs, history, cancellation, hierarchy and validation passed.\n"
	                 : "Paste checks failed.\n");
	return ok ? 0 : 1;
}
