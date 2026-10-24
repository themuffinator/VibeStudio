#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/editor_profiles.h"
#include "tests/level_entity_selection_test_helpers.h"
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
bool expect(bool value, const char* message, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << message << ": " << error.toStdString() << std::endl; } return value;
}
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
LevelMapSelectionRef entity(int id) { return {LevelMapSelectionKind::Entity,id}; }
QSet<QString> keys(const QVector<LevelMapSelectionRef>& refs)
{
	QSet<QString> result; for (const auto& ref : refs) { result.insert(levelMapSelectionRefId(ref)); } return result;
}
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(loadLevelMapBytes({"connected.map",{},{}},tests::entitySelectionFixture(),&map,&error),"parse fixture",error);
	map.selection = {{LevelMapSelectionKind::QuakePatch,2}}; return map;
}
bool query(LevelMapDocument* map, bool history = false, const QSet<QString>& excluded = {})
{
	LevelObjectSelectionRequest request; request.mode = LevelObjectSelectionMode::ConnectedEntities; request.undoSelection = history; request.excludedObjects = excluded;
	LevelObjectSelectionReport report; QString error; const auto before = bytes(*map); const auto revision = map->revision;
	const auto result = selectLevelMapObjects(map,request,&report,&error);
	expect(result,"connected query",error); expect(bytes(*map) == before && map->revision == revision,"selection never changes native bytes or geometry revision");
	expect(!result || report.selection == map->selection,"published report matches selection"); return result;
}
void matching()
{
	auto map = fixture(); const auto initial = map.selection; const auto source = bytes(map);
	expect(map.patches.size() == 3 && map.entities.size() == 10,"fixture owners and negative cases");
	query(&map,true);
	expect(map.selection == tests::connectedEntityFixtureResult(),"original picks then incoming, then outgoing in source primitive order");
	expect(map.undoStack.size() == 1 && map.editState == "saved" && !map.entities[3].origin.valid,"unanchored match and clean selection undo");
	const auto connected = map.selection;
	expect(undoLevelMapEdit(&map) && map.selection == initial && redoLevelMapEdit(&map) && map.selection == connected && bytes(map) == source,"exact selection undo redo without dirtying source");
	query(&map,true); expect(keys(map.selection).contains("entity:4") && !keys(map.selection).contains("entity:5"),"second invocation adds next hop but not killtarget");
	map = fixture(); query(&map); expect(map.undoStack.isEmpty(),"NRC selection stays out of history");
	for (int id : {0,5,7,8,9}) {
		map = fixture(); map.selection = {entity(id)}; query(&map,true);
		expect(map.selection == QVector<LevelMapSelectionRef>{entity(id)} && map.undoStack.isEmpty(),"worldspawn, empty, uppercase, case and whitespace nonmatches are no-ops");
	}
	map = fixture(); map.selection = {{LevelMapSelectionKind::QuakePatch,0}}; query(&map);
	expect(map.selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::QuakePatch,0}},"world child is preserved but contributes no names");
	map = fixture(); map.selection = {entity(2)}; query(&map);
	expect(map.selection.first() == entity(2) && map.selection.size() == 7,"entity and selected patch owner seed same names");
	map = fixture(); map.selection = {entity(2),{LevelMapSelectionKind::QuakePatch,2}}; query(&map);
	expect(map.selection.size() == 8 && keys(map.selection).size() == 8,"owner plus child seed is deduplicated without dropping original picks");
	for (const auto& profile : editorProfileDescriptors()) {
		QStringList bound; for (const auto& binding : profile.controls.keys) { if (binding.commandId == "map.selectConnectedEntities") { bound = binding.keys; } }
		expect(bound == (profile.id == "netradiant-custom" ? QStringList{"Ctrl+Shift+E"} : QStringList{}),"only NRC gets native connected shortcut");
	}
}
void visibilityAndGroups()
{
	auto map = fixture(); const auto source = map;
	query(&map,false,{"entity:1"}); expect(keys(map.selection) == QSet<QString>{"patch:2","entity:3","entity:6"},"hidden owner excludes descendants");
	map = source; query(&map,false,{"brush:1"});
	expect(keys(map.selection) == QSet<QString>{"patch:2","entity:3","patch:1","brush:0","entity:6"},"excluded child cannot be selected implicitly through owner");
	map = source; map.hiddenObjects = {"entity:2"}; query(&map);
	expect(map.selection == tests::connectedEntityFixtureResult(),"original hidden seed remains and still contributes names");
	map = source; QString group,error;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Target assembly",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"entity:1","entity:5"},&error),"group fixture",error);
	map.selection = source.selection; const auto grouped = map;
	query(&map); expect(keys(map.selection) == QSet<QString>{"patch:2","entity:3","entity:1","entity:5","entity:6"},"matched closed group selects complete atomic unit");
	map = grouped; query(&map,false,{"brush:1"}); expect(keys(map.selection) == QSet<QString>{"patch:2","entity:3","entity:6"},"excluded implicit child blocks complete group");
	map = grouped; setLevelSceneLocked(&map,group,true,&error); map.selection = source.selection; query(&map);
	expect(keys(map.selection) == QSet<QString>{"patch:2","entity:3","entity:6"},"locked target group omitted");
	map = grouped; setLevelSceneVisible(&map,group,false,&error); map.selection = source.selection; query(&map);
	expect(keys(map.selection) == QSet<QString>{"patch:2","entity:3","entity:6"},"invisible target group omitted");
	map = grouped; openLevelSceneGroup(&map,group,&error); map.selection = {entity(1)}; query(&map);
	expect(map.selection == QVector<LevelMapSelectionRef>{entity(1)},"new hits cannot leave open-group boundary");
}
void failuresAndCancellation()
{
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectObjects; request.objectSelection.mode = LevelObjectSelectionMode::ConnectedEntities;
	request.objectSelection.undoSelection = true;
	const auto map = fixture(); const auto before = bytes(map);
	for (const auto phase : {LevelPlacementPhase::SelectingObjects,LevelPlacementPhase::Finalizing,LevelPlacementPhase::Complete}) {
		bool cancel = false;
		const auto result = prepareLevelPlacement(map,request,{[&] { return cancel; },[&](const auto& p) { cancel = p.phase == phase; }});
		expect(result.cancelled && !result.succeeded && result.objectSelectionReport.selection.isEmpty() && map.undoStack.isEmpty()
			&& bytes(map) == before,"cancellation never publishes partial picks or history");
	}
	for (int kind = 0; kind < 5; ++kind) {
		auto invalid = map; auto bad = request;
		if (kind == 0) { invalid.selection = {entity(99999)}; }
		if (kind == 1) { invalid.selection = {entity(2),entity(2)}; }
		if (kind == 2) { invalid.scene.problem = "unsupported"; }
		if (kind == 3) { invalid.format = LevelMapFormat::DoomWad; }
		if (kind == 4) { bad.objectSelection.lines = {1}; }
		const auto result = prepareLevelPlacement(invalid,bad);
		expect(!result.succeeded && result.objectSelectionReport.selection.isEmpty() && invalid.undoStack.isEmpty(),"invalid query fails atomically");
	}
	LevelMapDocument large; large.format = LevelMapFormat::QuakeMap;
	for (int i = 0; i <= 20000; ++i) {
		LevelMapEntity item; item.id = i; item.className = "info_null";
		item.properties = {{"classname","info_null"},{i == 0 ? QStringLiteral("targetname") : QStringLiteral("target"),"batch"}};
		large.entities.append(item);
	}
	large.selection = {entity(0)}; QElapsedTimer timer; timer.start();
	const auto result = prepareLevelPlacement(large,request);
	expect(result.succeeded && result.document.selection.size() == 20001,"large unanchored graph stays complete",result.error);
	std::cout << "20,000 direct neighbours: " << timer.elapsed() << " ms" << std::endl;
}
void cli(const QString& executable)
{
	QTemporaryDir temp; if (!expect(temp.isValid(),"CLI temp")) { return; }
	const auto path = temp.filePath("connected.map"); QFile file(path); const auto source = tests::entitySelectionFixture();
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"CLI fixture")) { return; } file.close();
	const auto run = [&](const QStringList& args, bool success = true) {
		QProcess p; p.setWorkingDirectory(temp.path()); p.start(executable,QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map","select-connected",path}+args);
		const bool ended = p.waitForStarted() && p.waitForFinished(30000); const auto out = p.readAllStandardOutput(), err = p.readAllStandardError();
		expect(ended && p.exitStatus() == QProcess::NormalExit && (success ? p.exitCode() == 0 : p.exitCode() != 0),"CLI exit",QString::fromUtf8(out+err));
		if (!ended) { p.kill(); p.waitForFinished(); } return QJsonDocument::fromJson(out).object();
	};
	auto report = run({"--object=patch:2"}); QJsonArray expected;
	for (const auto& ref : tests::connectedEntityFixtureResult()) { expected.append(levelMapSelectionRefId(ref)); }
	expect(report["selection"].toArray() == expected && report["added"].toInt() == 6 && report["originalSelection"].toArray() == QJsonArray{"patch:2"}
		&& report["queryOnly"] == true && report["mapWritten"] == false && report["transitive"] == false,"CLI additive, non-transitive read-only query");
	report = run({"--object","patch:2","--exclude","entity:1"});
	expect(report["selection"].toArray() == QJsonArray{"patch:2","entity:3","entity:6"},"CLI excludes complete owner");
	for (const auto& bad : QVector<QStringList>{{},{"--object","entity:999"},{"--object","patch:2","--line","1"},{"--object","patch:2","--output",temp.filePath("out.map")},
		{"--object","patch:2","--typo"},{"--object","patch:2","extra"},{"--object","patch:2","--context","missing"},{"--object","patch:2","--engine","auto","--engine","auto"}}) { run(bad,false); }
	expect(file.open(QIODevice::ReadOnly) && file.readAll() == source && !QFile::exists(temp.filePath("out.map")),"CLI preserves source and writes no map");
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv); matching(); visibilityAndGroups(); failuresAndCancellation();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
