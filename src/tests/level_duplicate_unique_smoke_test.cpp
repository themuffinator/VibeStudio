#include "core/level_duplicate_p.h"
#include "core/level_placement.h"
#include "core/level_repeat.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include "tests/level_prefab_test_helpers.h"
#include "tests/level_udmf_test_helpers.h"
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
QString property(const LevelMapDocument& map, int id, const char* key)
{
	for (const auto& entity : map.entities) { if (entity.id == id) { return tests::prefabProperty(entity,QLatin1String(key)); } }
	return {};
}
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
bool unique(LevelMapDocument* map, QString* error, double dx = 0)
{
	return duplicateLevelMapSelection(map,dx,0,0,{true,true},error,LevelDuplicateMode::MakeUnique);
}
void namespaceRules()
{
	LevelMapEntity source;
	source.properties = {{"targetname","door"},{"target1","door001"},{"target2","door2"},{"target","door"},
		{"killtarget","outside"},{"target +3","door"},{"target-7","door"},{"target9999999999999999999999999999999","door"},
		{"target3x","door"},{"TARGETNAME","door"},{"target3 ","door"},{"target+","door"},{"target4",""},
		{"target5",QStringLiteral("long")+QString(3000,'9')},{"target6",QString::fromUtf8("門001")},
		{"target7",QString::fromUtf8("門1")},{"target8","000"},{"target9","00"}};
	auto copies = QVector<LevelMapEntity>{source};
	detail::makeLevelMapCopiesUnique({source},&copies);
	const auto& copied = copies.first();
	expect(tests::prefabProperty(copied,"targetname") == "door3" && tests::prefabProperty(copied,"target1") == "door4"
		&& tests::prefabProperty(copied,"target2") == "door5","numeric namespace collisions and zero padding");
	for (const auto& key : {"target","target +3","target-7","target9999999999999999999999999999999"}) {
		expect(tests::prefabProperty(copied,QLatin1String(key)) == "door3","names and integer-suffixed references share a mapping");
	}
	expect(tests::prefabProperty(copied,"killtarget") == "outside1","outgoing references renamed without a copied destination");
	for (const auto& key : {"target3x","TARGETNAME","target3 ","target+"}) { expect(tests::prefabProperty(copied,QLatin1String(key)) == "door","unrelated and differently cased keys retained"); }
	expect(tests::prefabProperty(copied,"target4").isEmpty() && tests::prefabProperty(copied,"target5") == "long1","empty names and unbounded numeric suffix");
	expect(tests::prefabProperty(copied,"target6") == QString::fromUtf8("門2") && tests::prefabProperty(copied,"target7") == QString::fromUtf8("門3"),"Unicode prefix and deterministic string order");
	expect(tests::prefabProperty(copied,"target8") == "2" && tests::prefabProperty(copied,"target9") == "1","all-numeric zero-padded names");
}
LevelMapDocument fixture()
{
	LevelMapDocument map; QString error;
	expect(tests::loadPrefabFixture(&map,&error),"load original assembly",error);
	const auto panel = levelPatchDefinition(map.patches.first()).join('\n')+QLatin1Char('\n');
	LevelMapDocument world; LevelMapCreateRequest create; create.starterRoom = false;
	expect(createLevelMap(create,&world,&error) && addLevelMapBoxBrush(&world,{200,0,0,true},{216,16,16,true},"studio/world",nullptr,&error),"world primitive",error);
	auto text = QString::fromUtf8(tests::prefabFixture());
	expect(text.contains(panel),"fixture patch source");
	text.replace(panel,QString());
	text.replace(QStringLiteral("\"speed\" \"160\"\n"),QStringLiteral("\"speed\" \"160\"\n")+panel);
	text.replace(QStringLiteral("\"message\" \"Do not export project settings\"\n"),QStringLiteral("\"message\" \"Do not export project settings\"\n")+levelMapSelectionText(world));
	expect(loadLevelMapBytes({"unique.map",{},"idTech3"},text.toUtf8(),&map,&error),"mixed owner fixture",error);
	expect(setLevelMapEntityProperty(&map,1,"origin","8 16 24",&error)
		&& setLevelMapEntityProperty(&map,3,"targetname","gate01",&error)
		&& setLevelMapEntityProperty(&map,4,"targetname","gate2",&error),"assembly origin and occupied names",error);
	const auto source = bytes(map);
	expect(loadLevelMapBytes({"unique.map",{},"idTech3"},source,&map,&error),"round-trip fixture",error);
	return map;
}
void transactions(const LevelMapDocument& source)
{
	QString error;
	auto map = source;
	setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,1},{LevelMapSelectionKind::QuakePatch,0},{LevelMapSelectionKind::Entity,2},{LevelMapSelectionKind::QuakeBrush,0}});
	const auto original = bytes(map);
	const auto selection = map.selection;
	expect(unique(&map,&error,32),"mixed unique duplication",error);
	if (!expect(map.entities.size() == 7 && map.brushes.size() == 5 && map.patches.size() == 2,"partial owner copies only selected children")) { return; }
	expect(map.brushes[4].entityId == 5 && map.patches[1].entityId == 5 && map.brushes[3].entityId == 0
		&& map.entities[5].origin.x == 40 && map.entities[5].origin.y == 16,"new owner, translated origin and world ownership");
	expect(property(map,5,"targetname") == "gate3" && property(map,6,"target") == "gate3" && property(map,6,"targetname") == "relay1"
		&& property(map,6,"killtarget") == "outside1" && property(map,1,"targetname") == "gate" && property(map,2,"target") == "gate","copied link graph renamed, originals retained");
	expect(map.selection == QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::Entity,5},{LevelMapSelectionKind::Entity,6},{LevelMapSelectionKind::QuakeBrush,3}}
		&& map.undoStack.size() == 1,"only copied roots selected and single undo");
	const auto after = bytes(map);
	LevelMapDocument reloaded;
	expect(loadLevelMapBytes({"unique.map",{},"idTech3"},after,&reloaded,&error) && reloaded.entities.size() == 7 && reloaded.brushes.size() == 5 && reloaded.patches.size() == 2,"copies save and reload",error);
	const auto recipe = levelRepeatCommandsJson(map);
	QVector<LevelRepeatAction> parsed;
	expect(parseLevelRepeatCommands(recipe,&parsed,&error) && parsed.size() == 1 && parsed.first().kind == LevelRepeatAction::Kind::DuplicateUnique
		&& !parsed.first().doomAllowed,"unique mode recorded in strict repeat recipe",error);
	expect(repeatLevelMapCommands(&map,{true,true},&error) && property(map,7,"targetname") == "gate4" && property(map,8,"target") == "gate4"
		&& property(map,8,"killtarget") == "outside2" && map.entities[7].origin.x == 72,"repeat resolves names in current namespace",error);
	expect(undoLevelMapEdit(&map,&error) && bytes(map) == after && undoLevelMapEdit(&map,&error) && bytes(map) == original && map.selection == selection,"exact undo of repeat and unique duplicate",error);
	expect(redoLevelMapEdit(&map,&error) && bytes(map) == after,"exact unique redo",error);
	parsed.first().doomAllowed = true;
	expect(!validateLevelRepeatActions(parsed,&error),"recipe rejects Doom-capable unique clone metadata");
	map = source; setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakePatch,0}});
	expect(unique(&map,&error) && map.entities.size() == 6 && map.brushes.size() == 3 && map.patches.size() == 2 && map.patches.last().entityId == 5,"patch-only partial owner",error);
	map = source; setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,1},{LevelMapSelectionKind::QuakeBrush,1}});
	expect(unique(&map,&error) && map.entities.size() == 6 && map.brushes.size() == 5 && map.patches.size() == 2,"explicit entity includes all children exactly once",error);
	map = source; setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,1}});
	expect(duplicateLevelMapSelection(&map,0,0,0,{true,true},&error) && map.entities.size() == 5 && map.brushes.last().entityId == 1,"ordinary duplication retains original owner",error);
	map = source; setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,1},{LevelMapSelectionKind::Entity,2}});
	expect(arrayLevelMapSelection(&map,{32,0,0,true},3,{true,true},&error,LevelDuplicateMode::MakeUnique) && map.undoStack.size() == 1
		&& map.entities.size() == 11 && map.brushes.size() == 6 && property(map,5,"targetname") == "gate3" && property(map,7,"targetname") == "gate4"
		&& property(map,9,"targetname") == "gate5" && property(map,10,"target") == "gate5" && map.entities[9].origin.x == 104,"array reserves previous copies and retains cumulative placement",error);
	expect(undoLevelMapEdit(&map,&error) && bytes(map) == bytes(source),"array single exact undo",error);
}
void sceneAndFailures(const LevelMapDocument& source)
{
	QString error, group;
	auto map = source;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Door",{},&group,&error) && assignLevelSceneObjects(&map,group,{"entity:1"},&error),"owner group",error);
	const auto grouped = map;
	setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,1}});
	expect(unique(&map,&error) && map.scene.nodes.size() == grouped.scene.nodes.size() && levelSceneMembership(map.scene,"entity:5") == group,"partial owner does not claim complete group",error);
	expect(undoLevelMapEdit(&map,&error) && bytes(map) == bytes(grouped),"partial hierarchy undo",error);
	map = grouped; setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,1}});
	expect(unique(&map,&error) && map.scene.nodes.size() == 2 && levelSceneMembership(map.scene,"entity:5") != group,"complete owner clones closed group",error);
	map = grouped; openLevelSceneGroup(&map,group,&error); setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,1}});
	expect(unique(&map,&error) && map.scene.nodes.size() == 1 && levelSceneMembership(map.scene,"entity:5") == group,"open group remains editing context",error);
	map = grouped; setLevelSceneLocked(&map,group,true,&error); setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,1}});
	const auto locked = bytes(map);
	const auto depth = map.undoStack.size();
	expect(!unique(&map,&error) && bytes(map) == locked && map.undoStack.size() == depth,"partial insertion into locked group is atomic",error);
	for (const auto policy : {LevelInsertionVisibility::PreserveAndReveal,LevelInsertionVisibility::FreshInherited}) {
		map = source; setLevelMapInsertionVisibility(&map,policy); map.hiddenObjects = {"entity:1","brush:1","patch:0"};
		setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,1}});
		expect(unique(&map,&error),"hidden owner duplication",error);
		const auto hidden = levelMapHiddenObjects(map);
		expect(!hidden.contains("entity:5") && hidden.contains("brush:3") == (policy == LevelInsertionVisibility::PreserveAndReveal)
			&& hidden.contains("patch:1") == (policy == LevelInsertionVisibility::PreserveAndReveal),"profile insertion visibility applies to unique subtree");
	}
	map = source; setLevelMapSelection(&map,{{LevelMapSelectionKind::QuakeBrush,1}});
	LevelPlacementRequest request; request.duplicateMode = LevelDuplicateMode::MakeUnique;
	const auto before = bytes(map);
	for (const auto phase : {LevelPlacementPhase::Inserting,LevelPlacementPhase::Finalizing}) {
		bool cancel = false;
		const auto result = prepareLevelPlacement(map,request,{[&] { return cancel; },[&](const LevelPlacementProgress& p) { cancel = p.phase == phase; }});
		expect(result.cancelled && !result.succeeded && bytes(map) == before && map.undoStack.isEmpty(),"cancelled unique candidate leaves source intact");
	}
	map.entities[1].origin.x = 32768;
	const auto outOfBounds = bytes(map);
	expect(!unique(&map,&error,32) && bytes(map) == outOfBounds && map.undoStack.isEmpty(),"partial owner origin bounds failure is atomic",error);
	map = source; setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,0}});
	expect(!unique(&map,&error) && bytes(map) == bytes(source),"worldspawn cannot be cloned");
	map = source; map.selection = {{LevelMapSelectionKind::QuakeBrush,9999}};
	expect(!unique(&map,&error) && bytes(map) == bytes(source),"stale selection rejected");
	LevelMapDocument doom;
	expect(loadLevelMapBytes({"unique.wad","MAP01",{}},tests::udmf::fixture(),&doom,&error),"UDMF fixture",error);
	setLevelMapSelection(&doom,{{LevelMapSelectionKind::DoomThing,0}}); const auto doomBefore = bytes(doom);
	expect(!unique(&doom,&error) && !arrayLevelMapSelection(&doom,{1,0,0,true},2,{true,true},&error,LevelDuplicateMode::MakeUnique)
		&& bytes(doom) == doomBefore && doom.undoStack.isEmpty(),"unique mode rejects UDMF before native thing array branch");
	// 129 selected children alone fit the array budget. Their new owners double
	// the record count and must be included before expansion begins.
	map = source; map.entities.resize(1); map.brushes.clear(); map.patches.clear(); map.selection.clear();
	for (int i = 1; i <= 129; ++i) {
		auto owner = source.entities[1]; owner.id = i; map.entities << owner;
		auto child = source.brushes[1]; child.id = i; child.entityId = i; map.brushes << child;
		map.selection << LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,i};
	}
	expect(!arrayLevelMapSelection(&map,{0,0,0,true},128,{true,true},&error,LevelDuplicateMode::MakeUnique)
		&& error.contains("32768") && map.entities.size() == 130 && map.brushes.size() == 129 && map.undoStack.isEmpty(),"array budget includes new partial owners",error);
}
void cli(const QString& executable, const LevelMapDocument& source)
{
	QTemporaryDir temp; if (!expect(temp.isValid(),"CLI temporary directory")) { return; }
	const auto input = temp.filePath("input.map"), output = temp.filePath("copy.map");
	QFile file(input); const auto original = bytes(source);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(original) == original.size(),"write CLI fixture")) { return; } file.close();
	const auto read = [](const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
	const auto run = [&](const QStringList& arguments, bool success = true) {
		QProcess process; process.setWorkingDirectory(temp.path());
		process.start(executable,QStringList{"--cli","--json","--settings-file",temp.filePath("settings.ini"),"map"}+arguments);
		const bool ended = process.waitForStarted() && process.waitForFinished(30000);
		const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
		expect(ended && process.exitStatus() == QProcess::NormalExit && (success ? process.exitCode() == 0 : process.exitCode() != 0),"CLI exit",QString::fromUtf8(out+err));
		if (!ended) { process.kill(); process.waitForFinished(); }
		return QJsonDocument::fromJson(out).object();
	};
	const QStringList args{"duplicate",input,"--object","brush:1","--object","entity:2","--make-unique","--output",output};
	auto report = run(args+QStringList{"--dry-run"});
	expect(report["cloneMode"] == "make-unique" && !QFile::exists(output) && read(input) == original,"unique CLI dry run");
	report = run(args);
	expect(report["copies"].toArray().size() == 2 && report["cloneMode"] == "make-unique","unique CLI report");
	LevelMapDocument saved; QString error;
	expect(loadLevelMap({output,{},{}},&saved,&error) && saved.entities.size() == 7 && saved.brushes.size() == 4
		&& property(saved,5,"targetname") == "gate3" && property(saved,6,"target") == "gate3","unique CLI saved ownership and links",error);
	const auto recipePath = temp.filePath("repeat.json"), repeatPath = temp.filePath("repeated.map");
	const auto recipe = QJsonDocument(report["map"].toObject()["repeatRecipe"].toObject()).toJson();
	QFile recipeFile(recipePath);
	expect(recipeFile.open(QIODevice::WriteOnly) && recipeFile.write(recipe) == recipe.size(),"save CLI unique repeat recipe"); recipeFile.close();
	const QStringList replay{"repeat",output,"--recipe",recipePath,"--object","entity:5","--object","entity:6","--output",repeatPath};
	run(replay+QStringList{"--dry-run"}); expect(!QFile::exists(repeatPath),"unique repeat CLI dry run");
	run(replay);
	LevelMapDocument repeated;
	expect(loadLevelMap({repeatPath,{},{}},&repeated,&error) && repeated.entities.size() == 9 && repeated.brushes.size() == 5
		&& property(repeated,7,"targetname") == "gate4" && property(repeated,8,"target") == "gate4","unique CLI recipe round trip resolves destination names",error);
	const auto valid = read(output); run(args,false); expect(read(output) == valid,"CLI protects existing output");
	for (const auto& bad : QVector<QStringList>{{"--make-unique"},{"--make-unique=false"},{"--typo"},{"extra"},{"--delta"},{"--delta","nan,0,0"},{"--copies","0"},{"--texture-lock","invalid"}}) {
		run(args+QStringList{"--overwrite"}+bad,false); expect(read(output) == valid && read(input) == original,"invalid CLI cannot publish");
	}
	for (const auto& alias : {QStringLiteral("duplicate"),QStringLiteral("clone")}) {
		auto aliased = args; aliased[0] = alias;
		run(aliased+QStringList{"--overwrite","--copies","3","--delta","32,0,0"});
		expect(loadLevelMap({output,{},{}},&saved,&error) && saved.entities.size() == 11 && property(saved,10,"target") == "gate5","aliases preserve unique arrays",error);
	}
	run({"duplicate","--input",input,"--object=brush:1","--make-unique","--output",output,"--overwrite"});
	expect(loadLevelMap({output,{},{}},&saved,&error) && saved.entities.size() == 6,"unique CLI explicit input and equals selector",error);
	const auto explicitInput = read(output);
	run({"--make-unique","duplicate",input,"--object","brush:1","--output",output,"--overwrite"});
	expect(read(output) == explicitInput,"global flag recognition accepts unique mode before the verb");
	run(args+QStringList{"--input",input,"--overwrite"},false);
	expect(read(output) == explicitInput,"ambiguous input path rejected");
	expect(read(input) == original,"CLI source bytes preserved throughout");
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv);
	namespaceRules(); const auto source = fixture();
	if (!ok) { return 1; }
	transactions(source); sceneAndFailures(source);
	if (argc == 2) { cli(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(),source); }
	return ok ? 0 : 1;
}
