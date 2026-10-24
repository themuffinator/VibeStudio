#include "core/level_wrap.h"
#include "core/level_patch.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_linked_groups.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <numeric>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value;
}
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
QVector<int> range(int count) { QVector<int> ids(count); std::iota(ids.begin(),ids.end(),0); return ids; }
LevelWrapRequest allVertices(const LevelMapDocument& map,int id = 0)
{
	LevelWrapRequest request; request.componentsOnly = true; request.texture = "studio/wrap";
	for (const auto& brush : map.brushes) { if (brush.id == id) {
		LevelBrushTopology topology; levelBrushTopology(brush,&topology);
		request.components << LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,id},LevelBrushComponent::Vertex,range(int(topology.vertices.size()))};
	} } return request;
}
}
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid() || argc != 2) { return 1; } QString error;
	const auto put = [](const QString& path,const QByteArray& data) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(data) == data.size(); };
	const auto read = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; };
	const auto run = [&](const QStringList& args,bool success = true) {
		QProcess p; p.setWorkingDirectory(temp.path()); p.start(QString::fromLocal8Bit(argv[1]),QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini")}+args);
		const bool finished = p.waitForStarted() && p.waitForFinished(30000); const auto output = p.readAllStandardOutput();
		expect(finished && p.exitStatus() == QProcess::NormalExit && (p.exitCode() == 0) == success,"CLI status",QString::fromUtf8(output+p.readAllStandardError()));
		if (!finished) { p.kill(); p.waitForFinished(); } return QJsonDocument::fromJson(output).object();
	};
	for (const auto& dialect : QStringList{"classic","valve220","brushDef","brushDef3"}) {
		LevelMapDocument source;
		if (!expect(loadLevelMapBytes({temp.filePath(dialect+".map"),{},"idtech3"},tests::surfaceFixture(dialect),&source,&error),"load surface fixture",error)) { continue; }
		setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0}});
		const auto before = bytes(source); auto request = allVertices(source); request.components[0].components = {0,1,2,4};
		auto map = source; LevelWrapReport report;
		if (!expect(wrapLevelMap(&map,request,&report,&error),"wrap only four picked vertices",dialect+error)) { continue; }
		expect(report.sourcesRetained && report.sourceBrushes == 1 && report.sourcePatches == 0 && report.vertices == 4 && report.faces == 4
			&& report.newFaces == 4 && report.inheritedFaces == 0 && report.brushId == 1,"tetrahedron from subset, no implicit parent corners",dialect);
		expect(map.brushes.size() == 2 && map.undoStack.size() == 1 && map.selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}},"sources remain selected with new hull",dialect);
		expect(map.brushes[0].sourceLines == source.brushes[0].sourceLines && map.brushes[0].geometryDirty == source.brushes[0].geometryDirty,"original source is unchanged",dialect);
		expect(map.brushes.last().primitiveKind == source.brushes[0].primitiveKind,"new brush keeps destination dialect",dialect);
		for (const auto& face : map.brushes.last().faces) {
			expect(face.textureName == request.texture && face.contentFlags == 0 && face.surfaceFlags == 0 && face.surfaceValue == 0,"component hull resets material and flags",dialect);
			const auto projection = levelTextureProjection(face); const auto uv = projection.at({0,0,0,true});
			expect(std::abs(uv.x()) < 1e-6 && std::abs(uv.y()) < 1e-6,"component hull uses default zero-offset projection",dialect);
		}
		const auto after = bytes(map);
		expect(after.count("// flags") == before.count("// flags") && after.count("/* retained */") == before.count("/* retained */"),"original comments retained once",dialect);
		expect(undoLevelMapEdit(&map,&error) && bytes(map) == before && map.selection == source.selection,"exact component wrap undo",dialect+error);
		expect(redoLevelMapEdit(&map,&error) && bytes(map) == after,"exact component wrap redo",dialect+error);
		LevelMapDocument reloaded; expect(loadLevelMapBytes({temp.filePath("reloaded.map"),{},"idtech3"},after,&reloaded,&error)
			&& reloaded.brushes.size() == 2 && reloaded.brushes.last().primitiveKind == source.brushes[0].primitiveKind,"native save reload",dialect+error);
		LevelBrushTopology topology; levelBrushTopology(source.brushes[0],&topology);
		QByteArray fullHull;
		for (const auto kind : {LevelBrushComponent::Vertex,LevelBrushComponent::Edge,LevelBrushComponent::Face}) {
			auto complete = source; auto selected = allVertices(source); selected.components[0].kind = kind;
			selected.components[0].components = range(int(kind == LevelBrushComponent::Vertex ? topology.vertices.size() : kind == LevelBrushComponent::Edge ? topology.edges.size() : topology.faces.size()));
			expect(wrapLevelMap(&complete,selected,nullptr,&error),"wrap vertices, edges and faces",dialect+error);
			if (fullHull.isEmpty()) { fullHull = bytes(complete); } else { expect(bytes(complete) == fullHull,"equivalent component sets produce identical hull",dialect); }
		}
		auto mixed = source; auto selected = allVertices(source); selected.components << LevelMapComponentSet{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{0}};
		expect(wrapLevelMap(&mixed,selected,&report,&error) && report.sourceBrushes == 1 && bytes(mixed) == fullHull,"mixed sets and duplicate points count one source",dialect+error);
		auto pointOwner = source; setLevelMapSelection(&pointOwner,{{LevelMapSelectionKind::Entity,1}});
		expect(wrapLevelMap(&pointOwner,request,nullptr,&error) && pointOwner.brushes.last().entityId == 0,"point entity destination falls back to worldspawn",dialect+error);
		auto flat = source; selected.components = {{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{0}}};
		expect(!wrapLevelMap(&flat,selected,&report,&error) && bytes(flat) == before && flat.undoStack.isEmpty() && report.brushId == -1,"flat picks never wrap whole parent",dialect+error);
		selected.points = {{160,120,120,true}};
		expect(wrapLevelMap(&flat,selected,&report,&error) && report.vertices == 5 && report.faces == 5,"face and extra point form a pyramid",dialect+error);
		const auto input = temp.filePath(dialect+".map"), output = temp.filePath(dialect+"-wrapped.map"); expect(put(input,before),"write CLI fixture");
		const QStringList command{"map","wrap-merge",input,"--set","brush:0/vertex/0,1,2,4","--texture","studio/wrap","--output",output};
		const auto dry = run(command+QStringList{"--dry-run"}); expect(dry["sourcesRetained"] == true && dry["vertices"] == 4 && !QFile::exists(output),"dry run validates without output");
		run(command); expect(read(output) == after,"CLI matches component core byte for byte",dialect);
		for (const auto& extra : QVector<QStringList>{{"--object","brush:0"},{"--unknown"},{"--texture","invalid"},{"--point","nan,0,0"},{"--set","brush:0/vertex/999"},{"--dry-run","--dry-run"}}) {
			run(command+QStringList{"--overwrite"}+extra,false); expect(read(input) == before && read(output) == after,"invalid CLI preserves input and output",dialect);
		}
		const auto pyramid = temp.filePath(dialect+"-pyramid.map");
		run({"map","wrap-merge",input,"--set","brush:0/face/0","--point","160,120,120","--texture","studio/wrap","--output",pyramid});
		expect(read(pyramid) == bytes(flat),"CLI extra point matches face pyramid",dialect);
	}
	LevelMapDocument seed; tests::createGeometryFixture(1,&seed,&error); setLevelMapSelection(&seed,{{LevelMapSelectionKind::QuakeBrush,0}});
	auto request = allVertices(seed); const auto original = bytes(seed);
	const auto refused = [&](LevelWrapRequest invalid,const char* label) {
		auto map = seed; LevelWrapReport report;
		expect(!wrapLevelMap(&map,invalid,&report,&error) && !error.isEmpty() && bytes(map) == original && map.undoStack.isEmpty() && report.brushId == -1,label,error);
	};
	auto invalid = request; invalid.components.clear(); refused(invalid,"empty component mode refuses fallback");
	invalid = request; invalid.components[0].components << -1; refused(invalid,"negative component refused");
	invalid = request; invalid.components[0].components << 999; refused(invalid,"partially invalid set refuses entire wrap");
	invalid = request; invalid.components[0].object.objectId = 999; refused(invalid,"missing source refused");
	invalid = request; invalid.texture = "bad\nmaterial"; refused(invalid,"unsafe material refused");
	invalid = request; invalid.componentsOnly = false; refused(invalid,"object mode rejects component payload");
	invalid = request; invalid.points = QVector<LevelMapVec3>(4,{64,0,0,true}); refused(invalid,"extra point limit");
	invalid = request; invalid.components = QVector<LevelMapComponentSet>(257,request.components[0]); refused(invalid,"component set limit");
	invalid = request; invalid.components[0].components = QVector<int>(4097,0); refused(invalid,"component ID limit");
	for (const auto& p : QVector<LevelMapVec3>{{32769,0,0,true},{0,std::numeric_limits<double>::infinity(),0,true},{0,0,0,false}}) {
		invalid = request; invalid.points = {p}; refused(invalid,"invalid extra point refused");
	}
	for (const int count : {64,65}) {
		LevelMapDocument many; tests::createGeometryFixture(count,&many,&error); auto selected = request; selected.components.clear();
		for (const auto& b : many.brushes) { selected.components += allVertices(many,b.id).components; }
		const auto before = bytes(many); LevelWrapReport report; const auto wrapped = wrapLevelMap(&many,selected,&report,&error);
		expect(count == 64 ? wrapped && report.sourceBrushes == count && many.brushes.size() == count+1
			: !wrapped && bytes(many) == before && many.undoStack.isEmpty(),"source object budget boundary",error);
	}
	// The native owner and editor group follow the last selected object, independently of component order.
	const auto brushText = seed.brushes[0].sourceLines.join('\n');
	const auto entityText = QString("{\n\"classname\" \"worldspawn\"\n}\n{\n\"classname\" \"func_wall\"\n%1\n}\n{\n\"classname\" \"func_detail\"\n%1\n}\n").arg(brushText);
	LevelMapDocument owners; expect(loadLevelMapBytes({temp.filePath("owners.map"),{},"idtech3"},entityText.toUtf8(),&owners,&error),"load owner fixture",error);
	setLevelMapSelection(&owners,{{LevelMapSelectionKind::QuakeBrush,1},{LevelMapSelectionKind::QuakeBrush,0}});
	QString group; createLevelSceneNode(&owners,LevelSceneNodeKind::Group,"destination",{},&group,&error); assignLevelSceneObjects(&owners,group,{"brush:0"},&error);
	const auto ownerBefore = bytes(owners); const auto ownerDepth = owners.undoStack.size();
	auto selected = allVertices(owners,1); LevelWrapReport report;
	expect(wrapLevelMap(&owners,selected,&report,&error) && owners.brushes.last().entityId == 1
		&& levelSceneNode(owners.scene,group)->objects.contains("brush:2") && owners.entities.size() == 3,"last selection chooses native owner and scene group",error);
	expect(owners.undoStack.size() == ownerDepth+1 && undoLevelMapEdit(&owners,&error) && bytes(owners) == ownerBefore,"owner and group exact undo",error);
	auto entityDestination = owners; setLevelMapSelection(&entityDestination,{{LevelMapSelectionKind::Entity,2}});
	expect(wrapLevelMap(&entityDestination,request,&report,&error) && entityDestination.brushes.last().entityId == 2,"explicit native brush entity destination",error);
	auto fallback = owners; fallback.selection.clear();
	expect(wrapLevelMap(&fallback,selected,&report,&error) && fallback.brushes.last().entityId == 2,"empty selection uses final component source",error);
	auto context = owners; openLevelSceneGroup(&context,group,&error); setLevelMapSelection(&context,{{LevelMapSelectionKind::QuakeBrush,1}});
	const auto contextBefore = bytes(context); const auto contextDepth = context.undoStack.size();
	expect(!wrapLevelMap(&context,request,nullptr,&error) && bytes(context) == contextBefore && context.undoStack.size() == contextDepth,"destination outside editing context is refused",error);
	setLevelSceneLocked(&owners,group,true,&error); const auto locked = bytes(owners); const auto depth = owners.undoStack.size();
	expect(!wrapLevelMap(&owners,selected,nullptr,&error) && bytes(owners) == locked && owners.undoStack.size() == depth,"locked destination refuses addition atomically",error);
	// Adding one hull to a linked source creates its sibling in the same transaction.
	auto linked = seed; QString originalGroup,copyGroup;
	expect(createLevelSceneNode(&linked,LevelSceneNodeKind::Group,"linked",{},&originalGroup,&error)
		&& assignLevelSceneObjects(&linked,originalGroup,{"brush:0"},&error)
		&& createLinkedLevelGroup(&linked,originalGroup,{0,128,0,true},&copyGroup,&error),"create linked fixture",error);
	setLevelMapSelection(&linked,seed.selection); const auto linkedBefore = bytes(linked); const auto linkedDepth = linked.undoStack.size();
	expect(wrapLevelMap(&linked,request,&report,&error) && linked.brushes.size() == 4 && linked.undoStack.size() == linkedDepth+1
		&& levelSceneNode(linked.scene,originalGroup)->objects.size() == 2 && levelSceneNode(linked.scene,copyGroup)->objects.size() == 2,"new component hull propagates to linked sibling",error);
	expect(undoLevelMapEdit(&linked,&error) && bytes(linked) == linkedBefore,"linked component wrap exact undo",error);
	// A curved patch contributes its controls, retains its native UV grid and supplies the destination group.
	auto patchMap = seed; patchMap.format = LevelMapFormat::Quake3Map; patchMap.engineFamily = "idtech3";
	LevelPatchCreateRequest patchRequest; LevelMapPatch patch; createLevelPatch(patchRequest,&patch,&error);
	patch.controlPoints[4].z = 64; refreshLevelPatchBounds(&patch); int patchId = -1;
	expect(addLevelMapPatch(&patchMap,patch,&patchId,&error),"add curved patch",error);
	QString patchGroup; createLevelSceneNode(&patchMap,LevelSceneNodeKind::Group,"patch",{},&patchGroup,&error); assignLevelSceneObjects(&patchMap,patchGroup,{QString("patch:%1").arg(patchId)},&error);
	setLevelMapSelection(&patchMap,{{LevelMapSelectionKind::QuakePatch,patchId}}); const auto patchBefore = bytes(patchMap); const auto patchDepth = patchMap.undoStack.size();
	selected = {}; selected.componentsOnly = true; selected.texture = "studio/wrap";
	selected.components = {{{LevelMapSelectionKind::QuakePatch,patchId},LevelBrushComponent::Vertex,{0,2,4,6,8}}};
	auto patchWrapped = patchMap;
	expect(wrapLevelMap(&patchWrapped,selected,&report,&error) && report.vertices == 5 && report.faces == 5 && report.sourcePatches == 1 && report.sourceBrushes == 0
		&& patchWrapped.patches[0].sourceLines == patchMap.patches[0].sourceLines && patchWrapped.patches[0].controlU == patchMap.patches[0].controlU
		&& levelSceneNode(patchWrapped.scene,patchGroup)->objects.contains("brush:1"),"patch controls form a retained-source pyramid in the patch group",error);
	expect(patchWrapped.undoStack.size() == patchDepth+1 && undoLevelMapEdit(&patchWrapped,&error) && bytes(patchWrapped) == patchBefore,"patch wrap exact undo",error);
	auto badPatch = patchMap; auto badSet = selected; badSet.components[0].kind = LevelBrushComponent::Edge;
	expect(!wrapLevelMap(&badPatch,badSet,nullptr,&error) && bytes(badPatch) == patchBefore,"patch edge IDs are refused",error);
	badSet = selected; badSet.components[0].components << 9;
	expect(!wrapLevelMap(&badPatch,badSet,nullptr,&error) && bytes(badPatch) == patchBefore,"invalid patch point is atomic",error);
	auto mixed = patchMap; selected.components = request.components+selected.components;
	expect(wrapLevelMap(&mixed,selected,&report,&error) && report.sourceBrushes == 1 && report.sourcePatches == 1 && mixed.patches.size() == 1,"mixed brush and patch inputs",error);
	const auto mixedInput = temp.filePath("mixed.map"), mixedOutput = temp.filePath("mixed-wrapped.map"); put(mixedInput,patchBefore);
	const auto mixedReport = run({"map","wrap-merge",mixedInput,"--engine","idtech3","--set","brush:0/vertex/0,1,2,3,4,5,6,7",
		"--set",QString("patch:%1/vertex/0,2,4,6,8").arg(patchId),"--texture","studio/wrap","--output",mixedOutput});
	expect(mixedReport["sourceBrushes"] == 1 && mixedReport["sourcePatches"] == 1,"mixed CLI reports both source types",QString::fromUtf8(QJsonDocument(mixedReport).toJson()));
	// The CLI starts with a persisted patch, whereas the earlier direct case
	// contains a newly inserted patch. Reopen the same source for a byte oracle:
	// native serialization preserves existing source order in both routes.
	LevelMapDocument persisted; expect(loadLevelMap({mixedInput,{},"idtech3"},&persisted,&error),"reload mixed CLI source",error);
	setLevelMapSelection(&persisted,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakePatch,patchId}});
	expect(wrapLevelMap(&persisted,selected,nullptr,&error) && read(mixedOutput) == bytes(persisted)
		&& levelSceneNode(persisted.scene,patchGroup)->objects.contains("brush:1"),"mixed brush and patch CLI matches shared hull and group destination",error);
	// Preparation never mutates source, including cancellation at final publication.
	LevelPlacementRequest work; work.operation = LevelPlacementOperation::WrapMerge; work.wrap = request;
	for (const auto phase : {LevelPlacementPhase::Preparing,LevelPlacementPhase::Building,LevelPlacementPhase::Inserting,LevelPlacementPhase::Finalizing}) {
		bool cancel = false; const auto result = prepareLevelPlacement(seed,work,{[&] { return cancel; },[&](const auto& p) { if (p.phase == phase) { cancel = true; } }});
		expect(result.cancelled && !result.succeeded && bytes(seed) == original && seed.undoStack.isEmpty(),"cancelled candidate cannot publish",levelPlacementPhaseName(phase));
	}
	const auto prepared = prepareLevelPlacement(seed,work); auto direct = seed; wrapLevelMap(&direct,request,nullptr,&error);
	expect(prepared.succeeded && bytes(prepared.document) == bytes(direct) && prepared.wrapReport.sourcesRetained && bytes(seed) == original,"worker and direct service agree");
	LevelMapDocument pair; tests::createGeometryFixture(2,&pair,&error); setLevelMapSelection(&pair,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
	const auto pairPath = temp.filePath("pair.map"), joinedPath = temp.filePath("joined.map"); put(pairPath,bytes(pair));
	LevelWrapRequest whole; whole.texture = "studio/wrap"; expect(wrapLevelMap(&pair,whole,&report,&error) && !report.sourcesRetained && pair.brushes.size() == 1,"standalone whole wrap fills gap",error);
	run({"map","wrap-merge",pairPath,"--object","brush:0","--object","brush:1","--texture","studio/wrap","--output",joinedPath});
	expect(read(joinedPath) == bytes(pair),"whole wrap CLI matches core");
	return ok ? 0 : 1;
}
