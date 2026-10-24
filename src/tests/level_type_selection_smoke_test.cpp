#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/editor_profiles.h"
#include "tests/level_type_selection_test_helpers.h"
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
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush,id}; }
LevelMapSelectionRef patch(int id) { return {LevelMapSelectionKind::QuakePatch,id}; }
QSet<QString> keys(const QVector<LevelMapSelectionRef>& refs)
{
	QSet<QString> result; for (const auto& ref : refs) { result.insert(levelMapSelectionRefId(ref)); } return result;
}
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(loadLevelMapBytes({"types.map",{},{}},tests::typeSelectionFixture(),&map,&error),"parse original type fixture",error); return map;
}
LevelObjectSelectionRequest request(LevelTypeSelectionScope scope = LevelTypeSelectionScope::Objects)
{
	LevelObjectSelectionRequest r; r.mode = LevelObjectSelectionMode::RadiantType; r.typeScope = scope; return r;
}
LevelObjectSelectionReport query(LevelMapDocument* map, const LevelObjectSelectionRequest& r)
{
	const auto before = serializeLevelMap(*map).bytes; const auto revision = map->revision;
	LevelObjectSelectionReport report; QString error;
	expect(selectLevelMapObjects(map,r,&report,&error),"type query",error);
	expect(serializeLevelMap(*map).bytes == before && map->revision == revision,"query preserves native content and geometry revision"); return report;
}
void objects()
{
	auto map = fixture(); expect(map.entities.size() == 15 && map.brushes[0].faces[0].textureName == "metal","fixture order and mixed materials");
	setLevelMapSelection(&map,{patch(1)}); auto r = request(); r.undoSelection = true; const auto initial = map.selection;
	auto report = query(&map,r);
	expect(map.selection == tests::doorTypeSelection() && report.matchedClasses == QStringList{"func_door"} && !report.materialFallback,"patch owner class, exact case and mixed primitive order");
	expect(map.undoStack.size() == 1 && levelMapHistoryMatchesSaved(map,1),"one non-content history step");
	expect(undoLevelMapEdit(&map) && map.selection == initial && redoLevelMapEdit(&map) && map.selection == tests::doorTypeSelection(),"type query exact undo redo");
	query(&map,r); expect(map.undoStack.size() == 1,"same complete class selection is a no-op");
	map = fixture(); setLevelMapSelection(&map,{entity(3),brush(0)}); report = query(&map,request());
	expect(keys(map.selection) == QSet<QString>{"entity:1","patch:1","brush:0","brush:1","entity:3","entity:7","entity:10"}
		&& report.matchedClasses == QStringList{"func_door","info_null"} && map.undoStack.isEmpty(),"every selected owner class, source order, unanchored results and transient history");
	map = fixture(); setLevelMapSelection(&map,{entity(11)}); report = query(&map,request());
	expect(map.selection == QVector<LevelMapSelectionRef>{entity(11),entity(12),entity(13)} && report.matchedClasses == QStringList{QString{}} && !report.materialFallback,"empty exact class values are classes, not material fallback");
	for (const auto& seeds : QVector<QVector<LevelMapSelectionRef>>{{},{patch(0)},{entity(0)}}) {
		map = fixture(); setLevelMapSelection(&map,seeds); r = request(); r.material = "STUDIO/CHECKER"; report = query(&map,r);
		expect(map.selection == tests::materialTypeSelection() && report.materialFallback && report.matchedClasses.isEmpty(),"world or empty seeds use current material with case-insensitive brush-then-patch matching");
	}
	map = fixture(); setLevelMapSelection(&map,{patch(0)}); r = request(); r.material = "studio/checker "; query(&map,r);
	expect(map.selection.isEmpty(),"nonmatching material clears selection and whitespace is significant");
	map = fixture(); map.hiddenObjects = {"entity:1"}; setLevelMapSelection(&map,{brush(0)}); query(&map,request());
	expect(map.selection == QVector<LevelMapSelectionRef>{entity(10)},"hidden seed supplies class but is removed from new results");
	for (const auto& profile : editorProfileDescriptors()) {
		QStringList bound; for (const auto& binding : profile.controls.keys) { if (binding.commandId == "map.selectAllOfType") { bound = binding.keys; } }
		expect(bound == (profile.id == "netradiant-custom" ? QStringList{"Shift+A"} : QStringList{}),"only NRC binds new type command");
	}
}
void boundaries()
{
	auto map = fixture(); setLevelMapSelection(&map,{brush(0)}); auto r = request(); r.excludedObjects = {"brush:1"}; query(&map,r);
	expect(keys(map.selection) == QSet<QString>{"patch:1","brush:0","entity:10"},"filtered child cannot be reached through owner wrapper");
	map = fixture(); QString group,error;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Assembly",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"entity:1","entity:5"},&error),"closed group fixture",error);
	setLevelMapSelection(&map,{brush(0)}); const auto grouped = map; query(&map,request());
	expect(keys(map.selection) == QSet<QString>{"entity:1","entity:5","entity:10"},"matching owner selects closed group atomically");
	map = grouped; query(&map,r); expect(map.selection == QVector<LevelMapSelectionRef>{entity(10)},"excluded implicit child blocks entire group");
	map = grouped; setLevelSceneLocked(&map,group,true,&error); setLevelMapSelection(&map,{brush(0)}); query(&map,request());
	expect(map.selection == QVector<LevelMapSelectionRef>{entity(10)},"locked group does not become a result");
	map = grouped; openLevelSceneGroup(&map,group,&error); setLevelMapSelection(&map,{brush(0)}); query(&map,request());
	expect(keys(map.selection) == QSet<QString>{"entity:1","patch:1","brush:0","brush:1"},"results stay inside open group");
}
void faces()
{
	auto map = fixture(); setLevelMapSelection(&map,{entity(1),brush(0),patch(2)}); const auto selection = map.selection;
	auto r = request(LevelTypeSelectionScope::Faces); r.material = "STUDIO/CHECKER"; r.undoSelection = true;
	const auto report = query(&map,r);
	expect(map.selection == selection && map.undoStack.isEmpty() && report.componentSelectionChanged && report.components.size() == 2,"face selection keeps objects and does not create map history");
	if (report.components.size() == 2) {
		expect(report.components[0].object == brush(0) && report.components[0].kind == LevelBrushComponent::Face
			&& report.components[0].components == QVector<int>{1,2,3,4,5} && report.components[1].components == QVector<int>{0,1,2,3,4,5},"exact solved face IDs and selected-owner expansion without duplicate brush");
	}
	r.excludedObjects = {"brush:1"}; expect(query(&map,r).components.size() == 1,"excluded faces never selected");
	r.excludedObjects = {"entity:1"}; expect(query(&map,r).components.isEmpty(),"excluded owner suppresses face children");
	r.excludedObjects.clear(); r.material = "missing"; expect(query(&map,r).components.isEmpty(),"no material match returns an empty replacement");
	setLevelMapSelection(&map,{patch(2)}); r.material = "studio/checker"; expect(query(&map,r).components.isEmpty(),"face mode does not select patch points");
	for (auto scope : {LevelTypeSelectionScope::Vertices,LevelTypeSelectionScope::Edges}) {
		r = request(scope); const auto before = map.selection; const auto noOp = query(&map,r);
		expect(map.selection == before && !noOp.componentSelectionChanged && noOp.components.isEmpty() && map.undoStack.isEmpty(),"vertex and edge modes do not replace existing picks");
	}
	map = fixture(); setLevelMapSelection(&map,{brush(0)}); QString error;
	expect(moveLevelMapSelectionToWorld(&map,nullptr,&error),"world brush fixture",error);
	r = request(LevelTypeSelectionScope::Faces); r.material = "metal";
	const auto world = query(&map,r); expect(world.components.size() == 1 && world.components.first().components == QVector<int>{0},"world brushes participate in face matching");
	map = fixture(); QString group;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Face assembly",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"entity:1"},&error),"face group fixture",error);
	setLevelMapSelection(&map,{entity(1)}); expect(query(&map,r).components.isEmpty(),"closed groups do not expose component picks");
	openLevelSceneGroup(&map,group,&error); setLevelMapSelection(&map,{entity(1)});
	expect(query(&map,r).components.size() == 1,"open group exposes matching faces");
	setLevelSceneLocked(&map,group,true,&error); expect(query(&map,r).components.isEmpty(),"locked open group cannot supply face results");
}
void failureAndScale()
{
	LevelPlacementRequest work; work.operation = LevelPlacementOperation::SelectObjects; work.objectSelection = request();
	auto map = fixture(); setLevelMapSelection(&map,{brush(0)});
	for (auto scope : {LevelTypeSelectionScope::Objects,LevelTypeSelectionScope::Faces}) {
		work.objectSelection.typeScope = scope; work.objectSelection.material = "studio/checker";
		for (auto phase : {LevelPlacementPhase::SelectingObjects,LevelPlacementPhase::Finalizing,LevelPlacementPhase::Complete}) {
			bool cancel = false; const auto result = prepareLevelPlacement(map,work,{[&]{return cancel;},[&](const auto& p){cancel = p.phase == phase;}});
			expect(result.cancelled && !result.succeeded && result.objectSelectionReport.selection.isEmpty() && result.objectSelectionReport.components.isEmpty(),"cancel never publishes partial object or face selections");
		}
	}
	for (int invalid = 0; invalid < 7; ++invalid) {
		auto copy = map; work.objectSelection = request();
		if (invalid == 0) { copy.selection = {}; }
		if (invalid == 1) { copy.selection = {entity(999)}; }
		if (invalid == 2) { work.objectSelection.lines = {1}; }
		if (invalid == 3) { work.objectSelection.typeScope = static_cast<LevelTypeSelectionScope>(99); }
		if (invalid == 4) { copy.format = LevelMapFormat::DoomWad; }
		if (invalid == 5) { copy.scene.problem = "unsupported"; }
		if (invalid == 6) { work.objectSelection.typeScope = LevelTypeSelectionScope::Faces; work.objectSelection.material = "studio/checker"; copy.brushes[0].faces[1].p1 = copy.brushes[0].faces[1].p0; }
		const auto result = prepareLevelPlacement(copy,work); expect(!result.succeeded && result.objectSelectionReport.selection.isEmpty() && result.objectSelectionReport.components.isEmpty(),"invalid query fails atomically");
	}
	LevelMapDocument dense; dense.format = LevelMapFormat::QuakeMap;
	for (int id = 0; id < 20000; ++id) { LevelMapEntity e; e.id = id; e.className = "info_null"; e.properties = {{"classname","info_null"}}; dense.entities.append(e); }
	setLevelMapSelection(&dense,{entity(1)}); work.objectSelection = request(); QElapsedTimer timer; timer.start();
	const auto result = prepareLevelPlacement(dense,work); expect(result.succeeded && result.document.selection.size() == 20000,"large unanchored class query remains complete",result.error);
	std::cout << "20,000 class matches: " << timer.elapsed() << " ms" << std::endl;
}
void cli(const QString& executable)
{
	QTemporaryDir temp; if (!expect(temp.isValid(),"CLI temporary output")) { return; }
	const auto path = temp.filePath("types.map"), output = temp.filePath("out.map"); QFile file(path); const auto source = tests::typeSelectionFixture();
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"CLI source")) { return; } file.close();
	const auto run = [&](const QStringList& args,bool success = true) {
		QProcess p; p.setWorkingDirectory(temp.path()); p.start(executable,QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map","select-type",path}+args);
		const bool done = p.waitForStarted() && p.waitForFinished(30000); const auto out = p.readAllStandardOutput(),err = p.readAllStandardError();
		expect(done && p.exitStatus() == QProcess::NormalExit && (success ? p.exitCode() == 0 : p.exitCode() != 0),"CLI query status",QString::fromUtf8(out+err));
		if (!done) { p.kill(); p.waitForFinished(); } return QJsonDocument::fromJson(out).object();
	};
	auto r = run({"--object","patch:1"}); expect(r["selection"].toArray() == QJsonArray{"entity:1","patch:1","brush:0","brush:1","entity:10"}
		&& r["matchedClasses"].toArray() == QJsonArray{"func_door"} && r["materialFallback"] == false && r["queryOnly"] == true && r["mapWritten"] == false,"CLI class query");
	r = run({"--material=STUDIO/CHECKER"}); expect(r["selection"].toArray().size() == 5 && r["materialFallback"] == true,"CLI current material without object seeds");
	r = run({"--mode","faces","--object","entity:1","--material","studio/checker"});
	expect(r["selection"].toArray() == QJsonArray{"entity:1"} && r["componentSelectionChanged"] == true && r["componentCount"] == 11
		&& r["selectors"].toArray() == QJsonArray{"brush:0/face/1,2,3,4,5","brush:1/face/0,1,2,3,4,5"},"CLI faces use reusable component selectors and retain object selection");
	r = run({"--mode","vertices","--object","brush:0"}); expect(r["componentSelectionChanged"] == false && r["selection"].toArray() == QJsonArray{"brush:0"},"CLI vertex mode is a no-op");
	for (const auto& bad : QVector<QStringList>{{},{"--mode","faces","--object","brush:0"},{"--object","entity:999"},{"--line","1"},{"--material","stone","--output",output},{"--mode","bad"},{"--mode","faces","--mode","faces"},{"--material","stone","--typo"},{"--material","stone","extra"}}) { run(bad,false); }
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source && !QFile::exists(output),"CLI never writes map content");
}
}
int main(int argc,char** argv)
{
	QCoreApplication app(argc,argv); objects(); boundaries(); faces(); failureAndScale();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); } return ok ? 0 : 1;
}
