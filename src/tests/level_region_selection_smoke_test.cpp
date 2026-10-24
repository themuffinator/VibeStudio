#include "core/level_document.h"
#include "core/level_patch.h"
#include "core/level_linked_groups.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {}) {
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << '\n'; } return value;
}
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush,id}; }
LevelMapDocument empty() { LevelMapDocument map; LevelMapCreateRequest request; request.starterRoom = false; createLevelMap(request,&map); return map; }
void box(LevelMapDocument* map, LevelMapVec3 low, LevelMapVec3 high) { QString error; expect(addLevelMapBoxBrush(map,low,high,"studio/region",nullptr,&error),"fixture box",error); }
LevelMapDocument reload(const LevelMapDocument& map) {
	LevelMapDocument result; QString error; expect(loadLevelMapBytes({"regions.map",{},"idtech3"},serializeLevelMap(map).bytes,&result,&error),"fixture reload",error); return result;
}
LevelPlacementResult run(LevelMapDocument map, LevelRegionSelectionPolicy policy, LevelMapRegionSelection mode,
	QVector<LevelMapSelectionRef> selection, int axis = 2) {
	setLevelMapSelection(&map,selection); LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectRegion;
	request.region = {policy,mode,axis,{},{}}; return prepareLevelPlacement(map,request);
}
void history(LevelPlacementResult result, const LevelMapDocument& before, const QVector<LevelMapSelectionRef>& oldSelection) {
	const auto bytes = serializeLevelMap(result.document).bytes; const auto selection = result.document.selection;
	QString error;
	expect(result.document.undoStack.size() == 1,"one undo command");
	expect(result.document.undoLimit == before.undoLimit,"history limit retained");
	expect(undoLevelMapEdit(&result.document,&error) && serializeLevelMap(result.document).bytes == serializeLevelMap(before).bytes
		&& result.document.selection == oldSelection,"exact undo and original selection",error);
	expect(redoLevelMapEdit(&result.document,&error) && serializeLevelMap(result.document).bytes == bytes && result.document.selection == selection,"exact redo and selected matches",error);
}
}
int main(int argc, char** argv) {
	QCoreApplication app(argc,argv); QString error;
	auto map = empty();
	box(&map,{-16,-16,-16,true},{16,16,16,true});
	box(&map,{112,-16,-16,true},{144,16,16,true});
	box(&map,{-4,-4,-4,true},{4,4,4,true});
	box(&map,{60,-4,-4,true},{68,4,4,true});
	box(&map,{124,-4,-4,true},{132,4,4,true});
	box(&map,{16,-4,-4,true},{24,4,4,true}); map = reload(map);
	const QVector<LevelMapSelectionRef> sources{brush(0),brush(1)}, inside{brush(2),brush(4)};
	for (auto policy : {LevelRegionSelectionPolicy::Bounds,LevelRegionSelectionPolicy::Radiant,LevelRegionSelectionPolicy::TrenchBroom}) {
		auto result = run(map,policy,LevelMapRegionSelection::Inside,sources);
		expect(result.succeeded,"inside selection",result.error);
		if (policy == LevelRegionSelectionPolicy::Bounds) {
			expect(result.document.brushes.size() == 6 && result.document.undoStack.isEmpty() && result.regionReport.matches.contains(brush(3)),"legacy combined bounds includes the gap without deleting");
		} else {
			expect(result.regionReport.matches == inside && result.document.selection == inside && result.document.brushes.size() == 4,"individual volumes exclude gap"); history(result,map,sources);
		}
		result = run(map,policy,LevelMapRegionSelection::Touching,sources);
		expect(result.succeeded && result.regionReport.matches.contains(brush(5)),"boundary contact counts",result.error);
		if (policy == LevelRegionSelectionPolicy::Radiant) { expect(result.document.selection.size() == 5 && result.document.selection.contains(brush(0)) && result.document.brushes.size() == 6 && result.document.undoStack.isEmpty(),"NRC touching retains sources and adds hits"); }
		if (policy == LevelRegionSelectionPolicy::TrenchBroom) { history(result,map,sources); }
	}
	// A rotated brush's square AABB includes empty corners.
	auto rotated = empty(); box(&rotated,{-16,-16,-16,true},{16,16,16,true});
	LevelMapRotationRequest rotation; rotation.axis = 2; rotation.degrees = 45; rotation.pivot = {0,0,0,true}; rotation.textureLock = false;
	expect(rotateLevelMapSelection(&rotated,rotation,&error),"rotate volume",error);
	box(&rotated,{19,19,-2,true},{21,21,2,true}); box(&rotated,{-2,-2,-2,true},{2,2,2,true}); rotated = reload(rotated);
	auto result = run(rotated,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Inside,{brush(0)});
	expect(result.succeeded && result.document.selection == QVector<LevelMapSelectionRef>{brush(2)},"convex inside excludes empty AABB corner",result.error);
	result = run(rotated,LevelRegionSelectionPolicy::Radiant,LevelMapRegionSelection::Inside,{brush(0)});
	expect(result.succeeded && result.document.selection.size() == 2,"NRC deliberately tests AABB",result.error);
	// Crossing beams overlap although neither has a vertex inside the other.
	auto crossing = empty(); box(&crossing,{-64,-2,-2,true},{64,2,2,true});
	rotateLevelMapSelection(&crossing,rotation); box(&crossing,{-64,-2,-2,true},{64,2,2,true}); rotation.degrees = -45;
	rotateLevelMapSelection(&crossing,rotation); crossing = reload(crossing);
	result = run(crossing,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Touching,{brush(0)});
	expect(result.succeeded && result.document.selection == QVector<LevelMapSelectionRef>{brush(1)},"SAT finds crossing solids",result.error);
	// Skew rods: their axes are (1,1,0) and (0,1,1). Their centres
	// differ by 3 units along the common normal (1,-1,1). Each rod's
	// cross-section fits a radius sqrt(2) circle, proving them disjoint.
	// Face normals alone do not separate this pair; an edge-cross axis does.
	auto skew = empty(); box(&skew,{-40,-1,-1,true},{40,1,1,true}); rotation.axis = 2; rotation.degrees = 45;
	rotateLevelMapSelection(&skew,rotation); box(&skew,{-40,-1,-1,true},{40,1,1,true});
	rotation.degrees = 90; rotateLevelMapSelection(&skew,rotation); rotation.axis = 0; rotation.degrees = 45; rotateLevelMapSelection(&skew,rotation);
	moveLevelMapSelection(&skew,1.7320508075688772,-1.7320508075688772,1.7320508075688772); skew = reload(skew);
	result = run(skew,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Touching,{brush(0)});
	expect(result.succeeded && result.document.selection.isEmpty(),"SAT separates skew rods with overlapping face projections",result.error);
	for (int axis = 0; axis < 3; ++axis) {
		auto tall = empty(); box(&tall,{-16,-16,-16,true},{16,16,16,true});
		rotation.axis = axis; rotation.degrees = 45; rotateLevelMapSelection(&tall,rotation);
		LevelMapVec3 low{-2,-2,-2,true}, high{2,2,2,true};
		if (axis == 0) { low.x = 100; high.x = 200; } else if (axis == 1) { low.y = 100; high.y = 200; } else { low.z = 100; high.z = 200; }
		box(&tall,low,high); tall = reload(tall);
		result = run(tall,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::CompleteTall,{brush(0)},axis);
		expect(result.succeeded && result.document.selection == QVector<LevelMapSelectionRef>{brush(1)},"projected tall volume on each axis",result.error); history(result,tall,{brush(0)});
		result = run(tall,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Inside,{brush(0)},axis);
		expect(result.succeeded && result.document.selection.isEmpty() && result.document.brushes.size() == 1,"empty result still consumes volume",result.error); history(result,tall,{brush(0)});
	}
	// Control vertices protrude, but the evaluated quadratic surface fits.
	auto patched = empty(); box(&patched,{-16,-16,-1,true},{16,16,20,true});
	LevelMapPatch patch; patch.width = patch.height = 3; patch.textureName = "studio/patch"; patch.controlGridNormalized = true;
	for (int y = -1; y <= 1; ++y) { for (int x = -1; x <= 1; ++x) {
		patch.controlPoints.append({8.0*x,8.0*y,x == 0 && y == 0 ? 64.0 : 0.0,true}); patch.controlU.append(x); patch.controlV.append(y);
	} }
	expect(addLevelMapPatch(&patched,patch,nullptr,&error),"curved patch fixture",error); patched = reload(patched);
	result = run(patched,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Inside,{brush(0)});
	expect(result.succeeded && result.document.selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakePatch,0}},"TB patch uses sampled surface bounds",result.error);
	result = run(patched,LevelRegionSelectionPolicy::Radiant,LevelMapRegionSelection::Inside,{brush(0)});
	expect(result.succeeded && result.document.selection.isEmpty(),"NRC patch bounds include control cage",result.error);
	// A grid edge crosses a narrow volume without a sampled point inside it.
	patched = empty(); box(&patched,{-0.5,-0.5,-1,true},{0.5,0.5,1,true});
	for (auto& p : patch.controlPoints) { p.z = 0; p.x += 1; } addLevelMapPatch(&patched,patch); patched = reload(patched);
	result = run(patched,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Touching,{brush(0)});
	expect(result.succeeded && result.document.selection.size() == 1,"patch grid edge crossing",result.error);

	// Bounds from definitions, not only an origin point, determine selection.
	auto entities = empty(); box(&entities,{-16,-16,-16,true},{16,16,16,true});
	addLevelMapEntity(&entities,"custom_test",{12,0,0,true}); entities = reload(entities);
	result = run(entities,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Inside,{brush(0)});
	expect(result.succeeded && result.document.selection.isEmpty(),"fallback 16-unit entity box",result.error);
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectRegion; request.region.policy = LevelRegionSelectionPolicy::TrenchBroom;
	request.region.entityBounds.insert("custom_test",{{-2,-2,-2},{2,2,2}}); setLevelMapSelection(&entities,{brush(0)});
	result = prepareLevelPlacement(entities,request);
	expect(result.succeeded && result.document.selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::Entity,1}},"entity definition bounds",result.error);
	result = run(entities,LevelRegionSelectionPolicy::Radiant,LevelMapRegionSelection::Inside,{brush(0),{LevelMapSelectionKind::Entity,1}});
	expect(result.succeeded && result.document.brushes.isEmpty() && result.document.entities.size() == 1,"NRC inside removes all selected objects",result.error);
	result = run(entities,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Inside,{brush(0),{LevelMapSelectionKind::Entity,1}});
	expect(!result.succeeded,"TB mixed source selection rejected");

	// Closed groups are one bounds-tested unit. Hidden or locked descendants
	// cannot be selected by promoting a different visible descendant.
	auto grouped = map; QString group;
	createLevelSceneNode(&grouped,LevelSceneNodeKind::Group,"Pair",{},&group);
	assignLevelSceneObjects(&grouped,group,{"brush:2","brush:3"}); grouped = reload(grouped);
	result = run(grouped,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Inside,{brush(0)});
	expect(result.succeeded && result.document.selection.isEmpty(),"inside does not select partially contained closed group",result.error);
	result = run(grouped,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Touching,{brush(0)});
	expect(result.succeeded && result.document.selection.contains(brush(2)) && result.document.selection.contains(brush(3)),"touching selects complete closed group",result.error);
	setLevelMapSelection(&grouped,{brush(0)}); request.region.entityBounds.clear(); request.region.mode = LevelMapRegionSelection::Touching;
	request.region.excludedObjects = {"brush:3"}; result = prepareLevelPlacement(grouped,request);
	expect(result.succeeded && !result.document.selection.contains(brush(2)),"hidden descendant blocks group promotion",result.error);
	request.region.excludedObjects.clear(); setLevelSceneLocked(&grouped,group,true);
	result = prepareLevelPlacement(grouped,request); expect(result.succeeded && !result.document.selection.contains(brush(2)),"locked target excluded",result.error);
	setLevelSceneLocked(&grouped,group,false); setLevelSceneVisible(&grouped,group,false);
	result = prepareLevelPlacement(grouped,request); expect(result.succeeded && !result.document.selection.contains(brush(2)),"scene-hidden target excluded",result.error);
	setLevelSceneVisible(&grouped,group,true); openLevelSceneGroup(&grouped,group); setLevelMapSelection(&grouped,{brush(2)});
	result = prepareLevelPlacement(grouped,request); expect(result.succeeded && !result.document.selection.contains(brush(0)),"open context excludes enclosing outside brush",result.error);
	auto linked = empty(); box(&linked,{-32,-32,-32,true},{32,32,32,true}); box(&linked,{-8,-8,-8,true},{8,8,8,true});
	QString family, copy;
	expect(createLevelSceneNode(&linked,LevelSceneNodeKind::Group,"Linked volume",{},&family,&error)
		&& assignLevelSceneObjects(&linked,family,{"brush:0","brush:1"},&error)
		&& createLinkedLevelGroup(&linked,family,{256,0,0,true},&copy,&error),"linked volume fixture",error);
	linked = reload(linked); openLevelSceneGroup(&linked,family); linked.undoLimit = 1;
	result = run(linked,LevelRegionSelectionPolicy::TrenchBroom,LevelMapRegionSelection::Inside,{brush(0)});
	expect(result.succeeded && result.document.brushes.size() == 2 && result.document.selection == QVector<LevelMapSelectionRef>{brush(1)},"consuming volume propagates across linked family",result.error);
	if (result.succeeded) { history(result,linked,{brush(0)}); }

	setLevelMapSelection(&map,sources); request.region.mode = LevelMapRegionSelection::Inside;
	const auto original = serializeLevelMap(map).bytes; const auto selection = map.selection;
	for (auto phase : {LevelPlacementPhase::Selecting,LevelPlacementPhase::Finalizing,LevelPlacementPhase::Complete}) {
		bool cancel = false; LevelPlacementControl control;
		control.isCancelled = [&] { return cancel; }; control.progress = [&](const auto& progress) { if (progress.phase == phase) { cancel = true; } };
		result = prepareLevelPlacement(map,request,control);
		expect(result.cancelled && !result.succeeded && serializeLevelMap(map).bytes == original && map.selection == selection,"cancellation is atomic");
	}
	request.region.axis = 3; result = prepareLevelPlacement(map,request); expect(!result.succeeded,"invalid axis refused"); request.region.axis = 2;
	map.selection.append(brush(999)); result = prepareLevelPlacement(map,request); expect(!result.succeeded,"stale source refused"); map.selection.removeLast();
	QString locked; createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Locked",{},&locked); assignLevelSceneObjects(&map,locked,{"brush:0"}); setLevelSceneLocked(&map,locked,true);
	result = prepareLevelPlacement(map,request); expect(!result.succeeded,"locked source refuses entire operation");

	auto dense = empty(); tests::createGeometryFixture(4096,&dense,&error); setLevelMapSelection(&dense,{brush(0)});
	QElapsedTimer timer; timer.start(); result = prepareLevelPlacement(dense,request);
	expect(result.succeeded,"dense-map worker completes",result.error);
	std::cout << "Dense region selection: " << timer.elapsed() << " ms for 4096 brushes\n";
	return ok ? 0 : 1;
}
