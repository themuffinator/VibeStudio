#include "core/level_entity_connections.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_linked_groups.h"
#include "core/level_protected_properties.h"
#include "core/editor_profiles.h"
#include "tests/level_prefab_test_helpers.h"
#include <QCoreApplication>
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
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << std::endl; }
	return value;
}
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
QString property(const LevelMapDocument& map, int id, const char* key)
{
	for (const auto& entity : map.entities) { if (entity.id == id) { return tests::prefabProperty(entity,QLatin1String(key)); } }
	return {};
}
void pick(LevelMapDocument* map, int first, int last) { map->selection = {{LevelMapSelectionKind::Entity,first},{LevelMapSelectionKind::Entity,last}}; }
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(tests::loadPrefabFixture(&map,&error),"load fixture",error); pick(&map,2,1); return map;
}
void transactions()
{
	const auto source = fixture(); QString error;
	const LevelEntityConnectionRequest native{LevelEntityConnectionMode::RadiantTarget,"quake"};
	const LevelEntityConnectionRequest kill{LevelEntityConnectionMode::RadiantKillTarget,"quake"};
	LevelEntityConnectionReport report; auto map = source;
	expect(setLevelMapEntityProperty(&map,2,"target","source-name",&error),"distinct source target",error);
	pick(&map,2,1);
	const auto before = bytes(map); const auto depth = map.undoStack.size(); const auto selection = map.selection;
	expect(connectLevelMapEntities(&map,native,&report,&error) && report.targetName == "source-name" && report.changedEntities == 1
		&& property(map,1,"targetname") == "source-name" && property(map,2,"target") == "source-name"
		&& report.sourceEntities == QVector<int>{2} && report.targetEntity == 1 && report.property == "target"
		&& map.selection == selection && map.undoStack.size() == depth+1,"native source overrides named destination once",error);
	const auto after = bytes(map);
	expect(connectLevelMapEntities(&map,native,&report,&error) && report.changedEntities == 0 && map.undoStack.size() == depth+1,"redundant native connection succeeds without history",error);
	expect(undoLevelMapEdit(&map,&error) && bytes(map) == before && map.selection == selection && redoLevelMapEdit(&map,&error)
		&& bytes(map) == after,"exact native undo redo",error);
	map = source; pick(&map,3,1);
	expect(connectLevelMapEntities(&map,native,&report,&error) && property(map,3,"target") == "gate" && report.changedEntities == 1,"destination fallback",error);
	map = source; pick(&map,3,4);
	expect(connectLevelMapEntities(&map,native,&report,&error) && report.targetName == "target_speaker1" && report.changedEntities == 2,"classname-based fresh name",error);
	LevelMapDocument reloaded;
	expect(loadLevelMapBytes({"connection.map",{},{}},bytes(map),&reloaded,&error) && property(reloaded,4,"targetname") == "target_speaker1"
		&& property(reloaded,3,"target") == "target_speaker1","serialised links reload",error);
	map = source;
	expect(connectLevelMapEntities(&map,kill,&report,&error) && property(map,2,"killtarget") == "outside"
		&& property(map,2,"target") == "gate" && property(map,1,"targetname") == "outside" && report.property == "killtarget","KillConnect has independent source-priority property",error);
	map = source; map.selection = {{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::Entity,3}};
	expect(connectLevelMapEntities(&map,native,&report,&error) && property(map,1,"target") == "misc_model1","brush resolves its owner",error);
	map = source; map.selection = {{LevelMapSelectionKind::QuakePatch,0},{LevelMapSelectionKind::Entity,3}};
	expect(connectLevelMapEntities(&map,native,&report,&error) && property(map,0,"target") == "misc_model1","NRC world patch resolves worldspawn",error);
	map = source; map.selection = {{LevelMapSelectionKind::Entity,3},{LevelMapSelectionKind::QuakePatch,0}};
	expect(connectLevelMapEntities(&map,native,&report,&error) && property(map,0,"targetname") == "worldspawn1","worldspawn can be native destination",error);
	map = source; pick(&map,2,3);
	for (auto& p : map.entities[3].properties) { if (p.key == "classname") { p.key = "CLASSNAME"; } }
	map.entities[2].properties = {{"classname","target_relay"},{"TARGET","ignored"},{"targetname","relay"}};
	expect(connectLevelMapEntities(&map,native,&report,&error) && report.targetName == "t1","empty classname fallback and case-sensitive key recognition",error);
	auto caseFixture = tests::prefabFixture();
	caseFixture.replace("\"targetname\" \"gate\"","\"TARGETNAME\" \"custom\"\n\"targetname\" \"gate\"");
	caseFixture.replace("\"target\" \"gate\"","\"TARGET\" \"custom\"\n\"target\" \"gate\"");
	expect(loadLevelMapBytes({"case.map",{},{}},caseFixture,&map,&error),"parse distinct case-variant keys",error); pick(&map,2,1);
	expect(connectLevelMapEntities(&map,native,&report,&error) && report.targetName == "gate","case variants cannot mask real connection properties",error);
	const auto links = levelMapTargetLinks(map);
	expect(links.size() == 1 && links.first().name == "gate" && links.first().sourceEntityId == 2 && links.first().targetEntityId == 1,
		"link graph and authored connection agree on exact property keys");
	map = source; pick(&map,3,4);
	map.entities[0].properties << LevelMapProperty{"target +7","target_speaker001"} << LevelMapProperty{"killtarget","target_speaker2"}
		<< LevelMapProperty{"message","target_speaker3"} << LevelMapProperty{"TARGETNAME","target_speaker3"} << LevelMapProperty{"target3x","target_speaker3"};
	expect(connectLevelMapEntities(&map,native,&report,&error) && report.targetName == "target_speaker3","fresh names reserve numbered and outgoing references, ignore unrelated keys",error);
	map = source;
	setLevelMapEntityProperty(&map,2,"target","  Exact Name  ",&error);
	pick(&map,2,1);
	expect(connectLevelMapEntities(&map,native,&report,&error) && property(map,1,"targetname") == "  Exact Name  ","native names retain case and whitespace",error);
	// Existing non-NRC behaviour remains multi-source, destination-first.
	map = source; map.selection = {{LevelMapSelectionKind::Entity,2},{LevelMapSelectionKind::Entity,3},{LevelMapSelectionKind::Entity,1}};
	QString name;
	expect(connectLevelMapEntities(&map,&name,&error) && name == "gate" && property(map,3,"target") == "gate","legacy multi-source remains",error);
	expect(!connectLevelMapEntities(&map,&name,&error),"legacy redundant connection retains failure contract");
}
void failuresAndContext()
{
	const auto source = fixture(); QString error;
	const LevelEntityConnectionRequest native{LevelEntityConnectionMode::RadiantTarget,"quake"};
	for (const auto& selection : QVector<QVector<LevelMapSelectionRef>>{
		{},{{LevelMapSelectionKind::Entity,2}},{{LevelMapSelectionKind::Entity,2},{LevelMapSelectionKind::Entity,3},{LevelMapSelectionKind::Entity,1}},
		{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}},
		{{LevelMapSelectionKind::QuakeBrush,999},{LevelMapSelectionKind::Entity,3}},{{LevelMapSelectionKind::DoomThing,0},{LevelMapSelectionKind::Entity,3}}}) {
		auto map = source; map.selection = selection; const auto before = bytes(map);
		LevelEntityConnectionReport report; report.targetName = "sentinel";
		expect(!canConnectLevelMapEntities(map,native) && !connectLevelMapEntities(&map,native,&report,&error)
			&& bytes(map) == before && map.undoStack.isEmpty() && report.targetName == "sentinel","invalid selection is atomic and report unpublished",error);
	}
	for (const auto& game : {"quake","q1","nexuiz","xonotic","quake2","q2","quake3","doom","unknown"}) {
		auto map = source;
		const bool supported = QStringList{"quake","q1","nexuiz","xonotic"}.contains(QLatin1String(game));
		LevelEntityConnectionRequest request{LevelEntityConnectionMode::RadiantKillTarget,QLatin1String(game)};
		expect(levelEntityKillConnectionSupported(map,request.game) == supported && canConnectLevelMapEntities(map,request) == supported
			&& connectLevelMapEntities(&map,request,nullptr,&error) == supported,"KillConnect game gate",error);
	}
	expect(levelEntityConnectionGame(source) == "quake3" && !levelEntityKillConnectionSupported(source),"patch map inference prevents accidental Quake killtarget");
	auto doom = source; doom.format = LevelMapFormat::DoomWad;
	expect(!canConnectLevelMapEntities(doom,native) && !connectLevelMapEntities(&doom,native,nullptr,&error),"Doom is not a text-map connection");
	auto map = source; QString group;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Protected target",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"entity:1"},&error) && setLevelSceneLocked(&map,group,true,&error),"lock destination",error);
	const auto locked = bytes(map); const auto depth = map.undoStack.size();
	expect(!connectLevelMapEntities(&map,{LevelEntityConnectionMode::RadiantKillTarget,"quake"},nullptr,&error)
		&& bytes(map) == locked && map.undoStack.size() == depth,"locked destination rejects atomic connection",error);
	expect(setLevelSceneLocked(&map,group,false,&error) && openLevelSceneGroup(&map,group,&error),"open target group",error);
	pick(&map,1,3); const auto boundary = bytes(map); const auto boundaryDepth = map.undoStack.size();
	expect(!connectLevelMapEntities(&map,native,nullptr,&error) && bytes(map) == boundary && map.undoStack.size() == boundaryDepth,"connection cannot cross open-group edit boundary",error);
	map = source; map.entities[3].id = 101; map.brushes[0].id = 200;
	map.selection = {{LevelMapSelectionKind::QuakeBrush,200},{LevelMapSelectionKind::Entity,101}};
	expect(canConnectLevelMapEntities(map,native) && connectLevelMapEntities(&map,native,nullptr,&error)
		&& property(map,101,"targetname") == "misc_model1","sparse IDs resolve correctly",error);
	map = source; pick(&map,3,4);
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::ConnectEntities; request.connection = native;
	for (const auto phase : {LevelPlacementPhase::Connecting,LevelPlacementPhase::Finalizing,LevelPlacementPhase::Complete}) {
		bool cancel = false;
		const auto result = prepareLevelPlacement(map,request,{[&] { return cancel; },[&](const LevelPlacementProgress& p) { cancel = p.phase == phase; }});
		expect(result.cancelled && !result.succeeded && result.connectionReport.targetName.isEmpty() && map.undoStack.isEmpty()
			&& bytes(map) == bytes(source),"cancelled worker publishes no connection or report");
	}
	for (const auto& profile : editorProfileDescriptors()) {
		const bool nrc = profile.id == "netradiant-custom";
		expect(profile.controls.entityConnectionMode == (nrc ? LevelEntityConnectionMode::RadiantTarget : LevelEntityConnectionMode::SourcesToLast),"profile isolates NRC connection policy");
		QStringList keys; for (const auto& binding : profile.controls.keys) { if (binding.commandId == "map.killConnectEntities") { keys = binding.keys; } }
		expect(keys == (nrc ? QStringList{"Shift+K"} : QStringList{}),"KillConnect key belongs only to NRC");
	}
}
void linked()
{
	auto map = fixture(); QString error, group, peer;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Linked door",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"entity:1"},&error)
		&& createLinkedLevelGroup(&map,group,{128,0,0,true},&peer,&error),"linked door setup",error);
	if (!expect(map.entities.size() == 6,"linked peer exists")) { return; }
	const auto linked = map;
	pick(&map,2,1); const auto before = bytes(map); const auto depth = map.undoStack.size();
	expect(connectLevelMapEntities(&map,{LevelEntityConnectionMode::RadiantKillTarget,"quake"},nullptr,&error)
		&& property(map,1,"targetname") == "outside" && property(map,5,"targetname") == "outside" && map.undoStack.size() == depth+1,"connection participates in linked propagation",error);
	expect(undoLevelMapEdit(&map,&error) && bytes(map) == before,"linked connection exact undo",error);
	map = linked;
	expect(setLevelEntityPropertyProtection(&map,{5},{"targetname"},true,&error),"protect peer name",error);
	pick(&map,2,1);
	expect(connectLevelMapEntities(&map,{LevelEntityConnectionMode::RadiantKillTarget,"quake"},nullptr,&error)
		&& property(map,1,"targetname") == "outside" && property(map,5,"targetname") == "gate","protected peer keeps instance name",error);
}
void cli(const QString& executable)
{
	QTemporaryDir temp; if (!expect(temp.isValid(),"CLI scratch")) { return; }
	const auto input = temp.filePath("input.map"), output = temp.filePath("connected.map");
	const auto original = bytes(fixture()); QFile f(input);
	if (!expect(f.open(QIODevice::WriteOnly) && f.write(original) == original.size(),"CLI fixture")) { return; } f.close();
	const auto read = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; };
	const auto run = [&](const QStringList& args, bool success = true) {
		QProcess p; p.setWorkingDirectory(temp.path()); p.start(executable,QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map"}+args);
		const bool ended = p.waitForStarted() && p.waitForFinished(30000); const auto out = p.readAllStandardOutput(), err = p.readAllStandardError();
		expect(ended && p.exitStatus() == QProcess::NormalExit && (success ? p.exitCode() == 0 : p.exitCode() != 0),"CLI exit",QString::fromUtf8(out+err));
		if (!ended) { p.kill(); p.waitForFinished(); } return QJsonDocument::fromJson(out).object();
	};
	const QStringList args{"connect",input,"--object","entity:2","--object","brush:0","--profile","netradiant-custom","--kill-target","--game","quake","--output",output};
	auto report = run(args+QStringList{"--dry-run"});
	expect(report["targetname"] == "outside" && report["property"] == "killtarget" && report["connectionMode"] == "radiant-killtarget"
		&& report["targetEntity"] == "entity:1" && report["sourceEntities"].toArray() == QJsonArray{"entity:2"} && !QFile::exists(output),"CLI dry run and structured owner report");
	run(args); LevelMapDocument saved; QString error;
	expect(loadLevelMap({output,{},{}},&saved,&error) && property(saved,1,"targetname") == "outside" && property(saved,2,"target") == "gate","CLI save and reopen",error);
	const auto valid = read(output); run(args,false); expect(read(output) == valid,"Save As protects output");
	for (const auto& bad : QVector<QStringList>{{"--kill-target"},{"--kill-target=false"},{"--typo"},{"extra"},{"--profile","bad"},{"--game","quake2"},{"--input",input},{"--select","entity:3"},{"--object","entity:4"},{"--map"}}) {
		run(args+QStringList{"--overwrite"}+bad,false); expect(read(output) == valid && read(input) == original,"invalid options and selections cannot publish");
	}
	for (const auto& game : {"quake2","quake3","doom"}) {
		auto denied = args; denied[denied.indexOf("--game")+1] = QLatin1String(game); run(denied+QStringList{"--overwrite"},false);
		expect(read(output) == valid,"unsupported game cannot overwrite");
	}
	for (const auto& alias : {"connect","connect-entities"}) {
		report = run({QLatin1String(alias),"--input",input,"--object=entity:3","--object=entity:4","--profile=netradiant-custom","--output",output,"--overwrite"});
		expect(report["targetname"] == "target_speaker1" && report["connectionMode"] == "radiant-target","native alias and equals options");
	}
	report = run({"connect",input,"--object","entity:2","--object","entity:3","--object","entity:1","--output",output,"--overwrite"});
	expect(report["targetname"] == "gate" && report["connectionMode"] == "sources-to-last" && report["sourceEntities"].toArray().size() == 2,"CLI default keeps multi-source adaptation");
	report = run({"--kill-target","connect",input,"--object","entity:3","--object","entity:1","--game=xonotic","--output",output,"--overwrite"});
	expect(report["targetname"] == "gate" && report["game"] == "xonotic","global boolean recognition and explicit compatible game");
	expect(read(input) == original,"CLI never modifies input");
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv); transactions(); failuresAndContext(); linked();
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()); }
	return ok ? 0 : 1;
}
