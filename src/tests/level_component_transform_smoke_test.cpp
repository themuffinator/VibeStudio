#include "core/level_brush.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_linked_groups.h"
#include "core/level_patch.h"
#include "core/level_placement.h"
#include "tests/level_transform_test_helpers.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <iostream>
#include <limits>
#include <numeric>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { std::cerr << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool near(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return std::abs(a.x-b.x) < .002 && std::abs(a.y-b.y) < .002 && std::abs(a.z-b.z) < .002;
}
QVector<int> all(const LevelMapBrush& brush)
{
	LevelBrushTopology topology; levelBrushTopology(brush,&topology);
	QVector<int> result(topology.vertices.size()); std::iota(result.begin(),result.end(),0); return result;
}
bool uvMatches(const LevelMapBrush& before, const LevelMapBrush& after, const LevelTextureAffine& matrix)
{
	LevelBrushTopology source,target;
	if (!levelBrushTopology(before,&source) || !levelBrushTopology(after,&target)) { return false; }
	for (int face = 0; face < before.faces.size(); ++face) {
		const auto projection = levelTextureProjection(before.faces[face]);
		for (int vertex : source.faces[face]) {
			const auto moved = transformedLevelComponentPoint(source.vertices[vertex],matrix);
			bool matched = false;
			for (int next = 0; next < after.faces.size(); ++next) {
				if (after.faces[next].textureName != before.faces[face].textureName) { continue; }
				if (std::none_of(target.faces[next].cbegin(),target.faces[next].cend(),[&](int v) { return near(target.vertices[v],moved); })) { continue; }
				const auto expected = projection.at(source.vertices[vertex]), actual = levelTextureProjection(after.faces[next]).at(moved);
				if (std::abs(expected.x()-actual.x()) < .002 && std::abs(expected.y()-actual.y()) < .002) { matched = true; }
			}
			if (!matched) { return false; }
		}
	}
	return true;
}
}
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); bool ok = true; QString error;
	for (const auto& dialect : {QStringLiteral("classic"),QStringLiteral("valve220"),QStringLiteral("brushDef"),QStringLiteral("brushDef3")}) {
		LevelMapDocument source;
		auto lines = tests::transformFixture(dialect).split('\n'); int material = 0;
		for (auto& line : lines) { if (line.contains("studio/checker")) { line.replace("studio/checker",QByteArray("studio/face")+QByteArray::number(material++)); } }
		if (!tests::loadTransformFixture(lines.join('\n'),&source,&error)) { std::cerr << error.toStdString(); return 1; }
		const auto brush = source.brushes.first();
		const QVector<LevelMapComponentSet> sets{{{LevelMapSelectionKind::QuakeBrush,brush.id},LevelBrushComponent::Vertex,all(brush)}};
		for (auto kind : {LevelComponentTransformKind::Rotate,LevelComponentTransformKind::Scale,LevelComponentTransformKind::Shear}) {
			auto map = source; LevelComponentTransformRequest request;
			request.operation = kind; request.pivot = {8,12,16,true}; request.axis = 2; request.along = 0;
			request.amount = kind == LevelComponentTransformKind::Rotate ? 90 : .5;
			request.scale = {1.25,.75,1.5,true}; request.textureLock = true; request.allowValve220 = true;
			LevelTextureAffine matrix;
			if (!levelComponentTransformMatrix(request,{},&matrix,&error)) { return 1; }
			const auto bytes = serializeLevelMap(map).bytes; const auto history = map.undoStack.size();
			if (!expect(transformLevelMapComponents(&map,sets,request,nullptr,&error),"component affine",dialect+error)) { ok = false; continue; }
			LevelBrushTopology before,after; levelBrushTopology(brush,&before); levelBrushTopology(map.brushes.first(),&after);
			for (const auto& p : before.vertices) {
				const auto expected = transformedLevelComponentPoint(p,matrix);
				ok &= expect(std::any_of(after.vertices.cbegin(),after.vertices.cend(),[&](const auto& q) { return near(q,expected); }),"affine vertex correspondence",dialect);
			}
			ok &= expect(uvMatches(brush,map.brushes.first(),matrix),"locked UV oracle",dialect);
			const auto changed = serializeLevelMap(map).bytes;
			LevelMapDocument loaded;
			ok &= expect(loadLevelMapBytes({"transformed.map",{},source.engineFamily},changed,&loaded,&error) && uvMatches(brush,loaded.brushes.first(),matrix),"native UV reload",dialect+error);
			ok &= expect(map.undoStack.size() == history+1 && undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes &&
				redoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == changed,"affine exact undo redo",dialect+error);
		}
		// Move only a vertex, edge or face away from the pivot. All original
		// vertices must survive, and unpicked corners stay exactly where they were.
		for (auto kind : {LevelBrushComponent::Vertex,LevelBrushComponent::Edge,LevelBrushComponent::Face}) {
			auto map = source; LevelBrushTopology topology; levelBrushTopology(brush,&topology);
			const auto picked = levelBrushComponentVertices(topology,kind,{0});
			LevelComponentTransformRequest request; request.operation = LevelComponentTransformKind::Scale;
			request.scale = {1.25,1.25,1.25,true}; request.pivot = {32,48,24,true};
			LevelTextureAffine matrix; levelComponentTransformMatrix(request,{},&matrix);
			if (!expect(transformLevelMapComponents(&map,{{sets[0].object,kind,{0}}},request,nullptr,&error),"partial component scale",dialect+error)) { ok = false; continue; }
			LevelBrushTopology result; levelBrushTopology(map.brushes.first(),&result);
			for (int i = 0; i < topology.vertices.size(); ++i) {
				const auto p = picked.contains(i) ? transformedLevelComponentPoint(topology.vertices[i],matrix) : topology.vertices[i];
				ok &= expect(std::any_of(result.vertices.cbegin(),result.vertices.cend(),[&](const auto& q) { return near(p,q); }),"partial selection keeps untouched corners",dialect);
			}
		}
		auto unchanged = source; const auto original = serializeLevelMap(source).bytes;
		LevelComponentTransformRequest identity; identity.operation = LevelComponentTransformKind::Rotate; identity.amount = 360;
		ok &= expect(transformLevelMapComponents(&unchanged,sets,identity,nullptr,&error) && serializeLevelMap(unchanged).bytes == original && unchanged.undoStack.size() == source.undoStack.size(),"identity has no history",dialect+error);
		identity.operation = LevelComponentTransformKind::Scale; identity.scale.x = 0;
		ok &= expect(!transformLevelMapComponents(&unchanged,sets,identity,nullptr,&error) && serializeLevelMap(unchanged).bytes == original,"flat solid refused atomically",dialect+error);
		identity.scale.x = -1;
		ok &= expect(transformLevelMapComponents(&unchanged,sets,identity,nullptr,&error),"component reflection",dialect+error);
	}
	LevelMapDocument map;
	if (!tests::createGeometryFixture(2,&map,&error)) { return 1; }
	LevelPatchCreateRequest patchRequest; LevelMapPatch patch;
	if (!createLevelPatch(patchRequest,&patch,&error) || !addLevelMapPatch(&map,patch,nullptr,&error)) { return 1; }
	const auto before = map; const auto bytes = serializeLevelMap(map).bytes;
	const auto history = map.undoStack.size();
	QVector<LevelMapComponentSet> sets;
	for (const auto& brush : map.brushes) { sets << LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,brush.id},LevelBrushComponent::Face,{0}}; }
	sets << LevelMapComponentSet{{LevelMapSelectionKind::QuakePatch,map.patches.first().id},LevelBrushComponent::Vertex,{0,1,1}};
	LevelComponentTransformRequest move; move.delta = {-8,0,0,true};
	ok &= expect(transformLevelMapComponents(&map,sets,move,nullptr,&error) && map.undoStack.size() == history+1 &&
		map.patches.first().controlU == before.patches.first().controlU,"mixed brush and patch transform",error);
	ok &= expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes,"mixed exact undo",error);
	map = before; map.undoLimit = 1;
	ok &= expect(transformLevelMapComponents(&map,sets,move,nullptr,&error) && map.undoStack.size() == 1 &&
		undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes,"small undo limit retains whole batch",error);
	map = before;
	QString group; createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Locked",{},&group,&error);
	assignLevelSceneObjects(&map,group,{"brush:1"},&error); setLevelSceneLocked(&map,group,true,&error);
	const auto locked = serializeLevelMap(map).bytes; const auto lockedRevision = map.revision;
	ok &= expect(!transformLevelMapComponents(&map,sets,move,nullptr,&error) && serializeLevelMap(map).bytes == locked && map.revision == lockedRevision,"locked batch atomicity",error);
	map = before; move.delta.x = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!transformLevelMapComponents(&map,sets,move,nullptr,&error) && serializeLevelMap(map).bytes == bytes,"non-finite atomic refusal",error);
	move.delta = {-8,0,0,true};
	LevelPlacementRequest work; work.operation = LevelPlacementOperation::TransformComponents; work.components = sets; work.componentTransform = move;
	bool cancel = false; LevelPlacementControl control;
	control.progress = [&](const auto& progress) { if (progress.phase == LevelPlacementPhase::Transforming) { cancel = true; } };
	control.isCancelled = [&] { return cancel; };
	const auto cancelled = prepareLevelPlacement(before,work,control);
	ok &= expect(cancelled.cancelled && !cancelled.succeeded && serializeLevelMap(before).bytes == bytes,"component worker cancellation is atomic");
	cancel = false;
	control.progress = [&](const auto& progress) { if (progress.phase == LevelPlacementPhase::Finalizing) { cancel = true; } };
	ok &= expect(prepareLevelPlacement(before,work,control).cancelled && serializeLevelMap(before).bytes == bytes,"component cancellation at final publication");
	for (bool split : {false,true}) {
		map = before; map.undoLimit = 1; QVector<LevelMapComponentSet> edits;
		for (const auto& brush : map.brushes) {
			LevelBrushTopology topology; levelBrushTopology(brush,&topology); int top = -1;
			for (int f = 0; f < topology.faces.size(); ++f) { if (topology.geometry.faces[f].plane.normalZ > .99) { top = f; } }
			edits << LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,brush.id},split ? LevelBrushComponent::Face : LevelBrushComponent::Vertex,{split ? top : 0}};
		}
		const bool changed = split ? splitLevelMapComponents(&map,edits,{0,0,16,true},false,nullptr,&error) : removeLevelMapComponents(&map,edits,nullptr,&error);
		ok &= expect(changed && map.undoStack.size() == 1 && undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes,"topology batch survives one-step history limit",error);
	}
	for (const auto& invalid : QVector<QVector<LevelMapComponentSet>>{ {},{sets[0],sets[0]},{{sets[0].object,LevelBrushComponent::Vertex,{0,9999}}} }) {
		map = before;
		ok &= expect(!transformLevelMapComponents(&map,invalid,move,nullptr,&error) && serializeLevelMap(map).bytes == bytes,"invalid selection atomicity",error);
	}
	// Force classic -> Valve conversion with an oblique shear and a rotated,
	// nonuniform mapping; an untouched brush must convert without moving or
	// changing its UVs, and the complete edit remains one undo step.
	LevelMapDocument classic; tests::loadTransformFixture(tests::transformFixture("classic"),&classic,&error);
	addLevelMapBoxBrush(&classic,{256,0,0,true},{320,64,64,true},"studio/untouched",nullptr,&error);
	const auto classicBefore = classic; const auto classicBytes = serializeLevelMap(classic).bytes;
	const QVector<LevelMapComponentSet> first{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Vertex,all(classic.brushes.first())}};
	LevelComponentTransformRequest shear; shear.operation = LevelComponentTransformKind::Shear; shear.axis = 0; shear.along = 2; shear.amount = .35; shear.textureLock = true;
	ok &= expect(!transformLevelMapComponents(&classic,first,shear,nullptr,&error) && serializeLevelMap(classic).bytes == classicBytes,"classic requires explicit UV conversion",error);
	shear.allowValve220 = true;
	ok &= expect(transformLevelMapComponents(&classic,first,shear,nullptr,&error),"map-wide UV conversion",error);
	for (const auto& b : classic.brushes) { for (const auto& f : b.faces) { ok &= expect(f.explicitTextureAxes,"no mixed classic/Valve faces"); } }
	ok &= expect(uvMatches(classicBefore.brushes.last(),classic.brushes.last(),LevelTextureAffine{}),"unselected conversion preserves UVs");
	ok &= expect(undoLevelMapEdit(&classic,&error) && serializeLevelMap(classic).bytes == classicBytes,"map-wide conversion exact undo",error);
	// Both linked instances are staged before either can propagate. Different
	// simultaneous edits stay different, matching the shared linked-group policy.
	map = before; QString linked,copy;
	ok &= expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Source",{},&linked,&error) &&
		assignLevelSceneObjects(&map,linked,{"brush:0"},&error) && createLinkedLevelGroup(&map,linked,{300,0,0,true},&copy,&error),"linked fixture",error);
	const auto sourceBrush = map.brushes.first(), copyBrush = map.brushes.last(); const auto linkedBytes = serializeLevelMap(map).bytes;
	QVector<LevelMapComponentSet> linkedSets{{{LevelMapSelectionKind::QuakeBrush,sourceBrush.id},LevelBrushComponent::Face,{0}},{{LevelMapSelectionKind::QuakeBrush,copyBrush.id},LevelBrushComponent::Face,{1}}};
	move.delta = {8,0,0,true};
	ok &= expect(transformLevelMapComponents(&map,linkedSets,move,nullptr,&error),"linked simultaneous batch",error);
	// Compare with the exact two source drafts prepared before propagation.
	for (const auto& originalBrush : {sourceBrush,copyBrush}) {
		auto expected = originalBrush; const auto& set = linkedSets[originalBrush.id == sourceBrush.id ? 0 : 1];
		ok &= expect(moveLevelBrushComponents(&expected,set.kind,set.components,move.delta,0,false,nullptr,&error),"independent linked expected draft",error);
		const auto found = std::find_if(map.brushes.cbegin(),map.brushes.cend(),[&](const auto& b) { return b.id == originalBrush.id; });
		ok &= expect(found != map.brushes.cend() && near(found->mins,expected.mins) && near(found->maxs,expected.maxs),"neither linked draft overwritten",error);
	}
	ok &= expect(undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == linkedBytes,"linked batch exact undo",error);
	LevelMapDocument cube; tests::createGeometryFixture(1,&cube,&error);
	for (int face = 0; face < 6; ++face) { setLevelMapBrushFaceProperty(&cube,0,face,"texture",QStringLiteral("studio/face%1").arg(face),&error); }
	const auto cubeBefore = cube.brushes.first(); const auto cubeBytes = serializeLevelMap(cube).bytes; const auto cubeHistory = cube.undoStack.size();
	LevelComponentTransformRequest turn; turn.operation = LevelComponentTransformKind::Rotate; turn.amount = 90; turn.pivot = {0,0,0,true};
	ok &= expect(transformLevelMapComponents(&cube,{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Vertex,all(cubeBefore)}},turn,nullptr,&error)
		&& cube.undoStack.size() == cubeHistory+1 && serializeLevelMap(cube).bytes != cubeBytes,"symmetric rotation moves face identity",error);
	for (int i = 0; i < cubeBefore.faces.size(); ++i) {
		const auto beforePlane = planeFromPoints(cubeBefore.faces[i].p0,cubeBefore.faces[i].p1,cubeBefore.faces[i].p2);
		const auto afterPlane = planeFromPoints(cube.brushes.first().faces[i].p0,cube.brushes.first().faces[i].p1,cube.brushes.first().faces[i].p2);
		ok &= expect(std::abs(afterPlane.normalX+beforePlane.normalY) < 1e-5 && std::abs(afterPlane.normalY-beforePlane.normalX) < 1e-5
			&& cube.brushes.first().faces[i].textureName == cubeBefore.faces[i].textureName,"symmetric face material follows its plane");
	}
	// A top face scaled sideways makes trapezoid sides. Their four corner
	// coordinates cannot all retain the original rectangular UV projection.
	cube = {}; tests::createGeometryFixture(1,&cube,&error); const auto square = serializeLevelMap(cube).bytes;
	LevelBrushTopology cubeTopology; levelBrushTopology(cube.brushes.first(),&cubeTopology); int upper = -1;
	for (int i = 0; i < cubeTopology.faces.size(); ++i) { if (cubeTopology.geometry.faces[i].plane.normalZ > .99) { upper = i; } }
	LevelComponentTransformRequest taper; taper.operation = LevelComponentTransformKind::Scale; taper.scale = {1.25,1,1,true}; taper.textureLock = taper.allowValve220 = true;
	ok &= expect(!transformLevelMapComponents(&cube,{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{upper}}},taper,nullptr,&error)
		&& error.contains("planar projection") && serializeLevelMap(cube).bytes == square,"non-affine corner UVs refused atomically",error);
	taper.textureLock = false;
	ok &= expect(transformLevelMapComponents(&cube,{{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{upper}}},taper,nullptr,&error),"explicit unlocked taper succeeds",error);
	return ok ? 0 : 1;
}
