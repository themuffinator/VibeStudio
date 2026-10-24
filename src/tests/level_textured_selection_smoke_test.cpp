#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_material_paint.h"
#include "tests/level_type_selection_test_helpers.h"
#include "tests/level_surface_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << std::endl; } return value;
}
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity,id}; }
LevelMapSelectionRef patch(int id) { return {LevelMapSelectionKind::QuakePatch,id}; }
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(loadLevelMapBytes({"textured.map",{},{}},tests::typeSelectionFixture(),&map,&error),"parse textured fixture",error); return map;
}
LevelTexturedSelectionReport query(LevelMapDocument* map, const LevelTexturedSelectionRequest& request)
{
	const auto bytes = serializeLevelMap(*map).bytes; const auto revision = map->revision;
	LevelTexturedSelectionReport report; QString error;
	expect(selectLevelMapTextured(map,request,&report,&error),"textured query",error);
	expect(serializeLevelMap(*map).bytes == bytes && map->revision == revision,"query never changes native content or geometry revision"); return report;
}
void additive()
{
	auto map = fixture(); setLevelMapSelection(&map,{entity(3),patch(2)}); const auto original = map.selection;
	LevelTexturedSelectionRequest r; r.material = "STUDIO/CHECKER"; r.faces = {{0,0},{0,0}}; r.undoSelection = true;
	auto report = query(&map,r);
	expect(report.faces.size() == 12 && report.faces.first() == LevelSurfaceFace{0,0} && report.faces.last() == LevelSurfaceFace{1,5}
		&& report.addedFaces == 11 && report.addedPatches == 2 && report.matchedPatches == QVector<int>{0,1,2},"additive faces, duplicate seed deduplication and whole-patch matches");
	expect(map.selection == QVector<LevelMapSelectionRef>{entity(3),patch(2),patch(0),patch(1)} && report.selection == map.selection,"keep ordered unrelated objects without selecting parent brushes");
	expect(report.brushGeometry.size() == 2 && report.brushGeometry.value(0) == levelSurfaceSelectionGeometryKey(map.brushes[0]),"returned identity binds exact brush geometry");
	expect(map.undoStack.size() == 1 && levelMapHistoryMatchesSaved(map,1),"only patch additions record non-content selection history");
	expect(undoLevelMapEdit(&map) && map.selection == original && redoLevelMapEdit(&map) && map.selection == report.selection,"patch object additions undo and redo exactly");
	r.faces = report.faces; const auto history = map.historyRevision; report = query(&map,r);
	expect(report.addedFaces == 0 && report.addedPatches == 0 && map.historyRevision == history,"repeated query is a no-op");
	const auto selection = map.selection; const auto faces = report.faces;
	r.material = "studio/checker "; report = query(&map,r);
	expect(map.selection == selection && report.faces == faces && report.matchedPatches.isEmpty() && map.historyRevision == history,"no match preserves all picks and whitespace is significant");
	map = fixture(); r = {}; r.material = "metal"; r.undoSelection = true; report = query(&map,r);
	expect(report.faces == QVector<LevelSurfaceFace>{{0,0}} && map.selection.isEmpty() && map.undoStack.isEmpty(),"face-only result does not select its parent or create object history");
	map = fixture(); map.hiddenObjects = {"brush:0"}; r.material = "studio/checker"; r.faces = {{0,0}}; report = query(&map,r);
	expect(report.faces.size() == 7 && report.faces.first() == LevelSurfaceFace{0,0} && report.addedFaces == 6,"temporarily hidden seed stays picked but no new hidden face is added");
}
void boundaries()
{
	LevelTexturedSelectionRequest r; r.material = "studio/checker";
	for (int boundary = 0; boundary < 3; ++boundary) {
		auto map = fixture(); QString group,error;
		if (boundary == 0) { map.hiddenObjects = {"entity:1"}; }
		if (boundary == 1) { r.excludedObjects = {"entity:1"}; }
		if (boundary == 2) {
			r.excludedObjects.clear();
			expect(createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Locked",{},&group,&error)
				&& assignLevelSceneObjects(&map,group,{"entity:1"},&error) && setLevelSceneLocked(&map,group,true,&error),"lock fixture",error);
		}
		const auto report = query(&map,r);
		expect(report.faces.isEmpty() && report.matchedPatches == QVector<int>{0,2},"hidden, filtered or locked owner excludes implicit children");
	}
	auto map = fixture(); QString group,error;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Closed",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"entity:1"},&error),"closed group fixture",error);
	auto report = query(&map,r); expect(report.faces.isEmpty() && report.matchedPatches == QVector<int>{0,2},"closed group never expands nonmatching material targets");
	openLevelSceneGroup(&map,group,&error); report = query(&map,r);
	expect(report.faces.size() == 11 && report.matchedPatches == QVector<int>{1} && map.selection == QVector<LevelMapSelectionRef>{patch(1)},"open group matches only its own faces and patches");
	map = fixture(); r.excludedObjects = {"brush:0","patch:1"}; report = query(&map,r);
	expect(report.faces.size() == 6 && report.matchedPatches == QVector<int>{0,2},"per-object filtering without owner expansion");
}
void identity()
{
	auto map = fixture(); auto brush = map.brushes.first(); const auto original = levelSurfaceSelectionGeometryKey(brush);
	for (auto& face : brush.faces) {
		face.textureName = "new/material"; face.shiftX = 123; face.scaleY = -2; face.rotation = 30; face.textureMatrix[2] = 0.5;
		face.uAxis = {0,1,0,true}; face.uOffset = 48; face.explicitTextureAxes = true;
		face.contentFlags = 123; face.surfaceFlags = 456; face.surfaceValue = 78; face.textureDirty = true; face.line += 10;
	}
	brush.primitiveKind = "valve220"; brush.selected = true; brush.geometryDirty = true; brush.startLine += 10;
	expect(levelSurfaceSelectionGeometryKey(brush) == original,"material, mapping, flags, conversion and bookkeeping retain geometry identity");
	brush.faces.swapItemsAt(0,1); expect(levelSurfaceSelectionGeometryKey(brush) != original,"face reorder invalidates indices");
	brush = map.brushes.first(); brush.faces[0].p0.x += 0.000001;
	expect(levelSurfaceSelectionGeometryKey(brush) != original,"sub-grid plane edit is not rounded away");
	brush = map.brushes.first(); ++brush.entityId; expect(levelSurfaceSelectionGeometryKey(brush) != original,"ownership change invalidates context");
	brush = map.brushes.first(); brush.faces[0].explicitPlane = true; brush.faces[0].planeNormal = {1,0,0,true};
	const auto explicitKey = levelSurfaceSelectionGeometryKey(brush); brush.faces[0].p0.x += 100;
	expect(levelSurfaceSelectionGeometryKey(brush) == explicitKey,"explicit-plane identity ignores unused generated points");
	brush.faces[0].planeDistance += 0.000001; expect(levelSurfaceSelectionGeometryKey(brush) != explicitKey,"explicit plane distance is exact");
}
void dialects()
{
	for (const auto& dialect : {QStringLiteral("classic"),QStringLiteral("valve220"),QStringLiteral("brushDef"),QStringLiteral("brushDef3")}) {
		LevelMapDocument map; QString error; const auto source = tests::surfaceFixture(dialect);
		if (!expect(loadLevelMapBytes({"dialect.map",{},{}},source,&map,&error),"textured dialect fixture",error)) { continue; }
		LevelTexturedSelectionRequest request; request.material = "STUDIO/GRID"; const auto report = query(&map,request);
		expect(report.faces.size() == 6 && report.addedFaces == 6 && report.matchedPatches.isEmpty() && map.selection.isEmpty(),"all native plane/UV dialects produce independent face picks",dialect);
		QVector<LevelMaterialTarget> targets;
		for (const auto& face : report.faces) { targets.append({LevelMaterialKind::BrushFace,face.brushId,face.faceIndex}); }
		LevelMaterialPaintPlan paint;
		expect(prepareLevelMaterialPaint(map,targets,"new/material",&paint,&error) && commitLevelMaterialPaint(&map,paint,&error),"dialect surface authoring handoff",error);
		expect(levelSurfaceSelectionGeometryKey(map.brushes.first()) == report.brushGeometry.value(map.brushes.first().id),"real surface edit preserves ordered geometry identity",dialect);
	}
}
void failureAndScale()
{
	auto map = fixture(); LevelPlacementRequest work; work.operation = LevelPlacementOperation::SelectTextured;
	work.texturedSelection.material = "studio/checker";
	for (auto phase : {LevelPlacementPhase::Preparing,LevelPlacementPhase::SelectingObjects,LevelPlacementPhase::Finalizing,LevelPlacementPhase::Complete}) {
		bool cancel = false; const auto result = prepareLevelPlacement(map,work,{[&]{return cancel;},[&](const auto& p){cancel = p.phase == phase;}});
		expect(result.cancelled && !result.succeeded && result.texturedSelectionReport.selection.isEmpty() && result.texturedSelectionReport.faces.isEmpty()
			&& result.texturedSelectionReport.brushGeometry.isEmpty() && result.document.selection.isEmpty(),"cancellation at every phase discards complete candidate and report");
	}
	for (int invalid = 0; invalid < 9; ++invalid) {
		auto copy = map; auto request = work.texturedSelection;
		if (invalid == 0) { request.material.clear(); }
		if (invalid == 1) { request.material = QString(4097,'a'); }
		if (invalid == 2) { request.faces = {{999,0}}; }
		if (invalid == 3) { request.faces = {{0,6}}; }
		if (invalid == 4) { copy.format = LevelMapFormat::DoomWad; }
		if (invalid == 5) { copy.scene.problem = "unsupported"; }
		if (invalid == 6) { copy.brushes[0].faces[1].p1 = copy.brushes[0].faces[1].p0; }
		if (invalid == 7) { copy.selection = {entity(999)}; }
		if (invalid == 8) { request.faces.fill({0,0},16385); }
		const auto bytes = serializeLevelMap(copy).bytes; const auto selected = copy.selection;
		LevelTexturedSelectionReport sentinel; sentinel.addedFaces = 77; QString error;
		expect(!selectLevelMapTextured(&copy,request,&sentinel,&error) && !error.isEmpty() && sentinel.addedFaces == 77
			&& copy.selection == selected && serializeLevelMap(copy).bytes == bytes,"invalid query leaves caller document and report untouched",error);
	}
	LevelMapDocument dense = map; dense.brushes.clear(); dense.patches.clear();
	for (int id = 0; id < 2731; ++id) {
		auto brush = map.brushes.last(); brush.id = id;
		if (id == 2730) { brush.faces[4].textureName = brush.faces[5].textureName = "other"; }
		dense.brushes.append(brush);
	}
	QElapsedTimer timer; timer.start(); auto result = prepareLevelPlacement(dense,work);
	expect(result.succeeded && result.texturedSelectionReport.faces.size() == 16384 && result.document.selection.isEmpty(),"complete 16,384 face result fits without selecting parents",result.error);
	dense.brushes.last().faces[4].textureName = "studio/checker"; result = prepareLevelPlacement(dense,work);
	expect(!result.succeeded && result.texturedSelectionReport.faces.isEmpty(),"overflow refuses the complete query instead of truncating");
	std::cout << "Bounded face queries: " << timer.elapsed() << " ms" << std::endl;
}
void cli(const QString& executable)
{
	QTemporaryDir temp; if (!expect(temp.isValid(),"CLI temporary output")) { return; }
	const auto path = temp.filePath("textured.map"), output = temp.filePath("painted.map"); const auto source = tests::typeSelectionFixture(); QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"CLI fixture")) { return; } file.close();
	const auto run = [&](const QStringList& args, int code = 0) {
		QProcess process; process.setWorkingDirectory(temp.path());
		process.start(executable,QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map"}+args);
		const bool done = process.waitForStarted() && process.waitForFinished(30000); const auto out = process.readAllStandardOutput(),err = process.readAllStandardError();
		expect(done && process.exitStatus() == QProcess::NormalExit && process.exitCode() == code,"CLI query status",QString::fromUtf8(out+err));
		if (!done) { process.kill(); process.waitForFinished(); } return QJsonDocument::fromJson(out).object();
	};
	auto report = run({"select-textured",path,"--material","STUDIO/CHECKER","--object","entity:3","--face","face:0:1"});
	expect(report["selection"].toArray() == QJsonArray{"entity:3","patch:0","patch:1","patch:2"} && report["faces"].toArray().size() == 12
		&& report["addedFaces"] == 11 && report["addedPatches"] == 3 && report["surfaceTargets"].toArray().size() == 15
		&& report["parentBrushesAdded"] == false && report["componentSelectionChanged"] == false && report["mapWritten"] == false && report["queryOnly"] == true,"CLI independent faces, patches and reusable authoring targets");
	expect(report["brushGeometrySha256"].toObject()["0"].toString().size() == 64 && report["sourceSha256"].toString().size() == 64,"CLI geometry and source identities");
	report = run({"select-textured",path,"--material","absent","--face","face:0:1","--object","entity:3"});
	expect(report["faces"].toArray() == QJsonArray{"face:0:1"} && report["selection"].toArray() == QJsonArray{"entity:3"} && report["addedFaces"] == 0,"CLI no-match retains independent seeds");
	report = run({"select-textured",path,"--material","STUDIO/CHECKER","--exclude","entity:1"});
	expect(report["faces"].toArray().isEmpty() && report["matchedPatches"].toArray() == QJsonArray{"patch:0","patch:2"},"CLI owner exclusion");
	for (const auto& bad : QVector<QStringList>{{},{"--material","metal","--face","patch:0"},{"--material","metal","--face","face:0:0"},
		{"--material","metal","--line","1"},{"--material","metal","--mode","faces"},{"--material","metal","--output",output},
		{"--material","metal","--material","metal"},{"--material","metal","extra"},{"--material","metal","--unknown"}}) {
		run(QStringList{"select-textured",path}+bad,2);
	}
	run({"select-textured",path,"--material","metal","--face","face:0:99"},4);
	run({"select-type",path,"--material","metal","--face","face:0:1"},2);
	report = run({"select-textured",path,"--material","studio/checker"});
	QStringList paint{"paint-material",path,"--texture","replacement","--output",output};
	for (const auto& target : report["surfaceTargets"].toArray()) { paint << "--target" << target.toString(); }
	run(paint); LevelMapDocument result; QString error;
	if (expect(loadLevelMap({output,{},{}},&result,&error),"CLI surface handoff output",error)) {
		expect(result.brushes.first().faces.first().textureName == "metal" && result.brushes.last().faces.last().textureName == "replacement"
			&& result.patches.first().textureName == "replacement", "CLI handoff changes exact face and whole-patch matches only");
	}
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source,"query and separate authoring handoff preserve source file");
}
}
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); additive(); boundaries(); identity(); dialects(); failureAndScale();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); } return ok ? 0 : 1;
}
