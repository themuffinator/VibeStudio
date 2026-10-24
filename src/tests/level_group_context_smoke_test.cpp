#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_linked_groups.h"
#include "core/level_brush.h"
#include "core/level_document.h"
#include "core/level_prefab.h"
#include "core/level_udmf.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_prefab_test_helpers.h"
#include "tests/level_udmf_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { std::cerr << label << ": " << detail.toStdString() << '\n'; } return value;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv); QString error; bool ok = true;
	LevelMapDocument document;
	if (!tests::createGeometryFixture(3,&document,&error)) { return 1; }
	QString layer,outer,inner;
	ok &= createLevelSceneNode(&document,LevelSceneNodeKind::Layer,QStringLiteral("Previous destination"),{},&layer,&error);
	ok &= createLevelSceneNode(&document,LevelSceneNodeKind::Group,QStringLiteral("Outer"),{},&outer,&error);
	ok &= createLevelSceneNode(&document,LevelSceneNodeKind::Group,QStringLiteral("Inner"),outer,&inner,&error);
	ok &= assignLevelSceneObjects(&document,outer,{QStringLiteral("brush:0")},&error);
	ok &= assignLevelSceneObjects(&document,inner,{QStringLiteral("brush:1")},&error);
	document.activeSceneNode = layer;
	const LevelMapSelectionRef first{LevelMapSelectionKind::QuakeBrush,0},second{LevelMapSelectionKind::QuakeBrush,1},outside{LevelMapSelectionKind::QuakeBrush,2};
	const auto baseline = document; const auto bytes = serializeLevelMap(document).bytes;
	auto context = levelSceneSelectionContext(document);
	ok &= expect(context.allowedObjects.contains("entity:0"),"map context permits world properties");
	const auto group = pickLevelSceneObjects(context,{}, {second},LevelScenePick::Replace);
	ok &= expect(group.size() == 2 && group.contains(first) && group.contains(second),"closed outer group is one selection unit");
	ok &= expect(pickLevelSceneObjects(context,group,{first,second},LevelScenePick::Toggle).isEmpty(),"band toggles one group once");
	ok &= expect(pickLevelSceneObjects(context,{first},{second},LevelScenePick::Toggle).isEmpty(),"existing partial selection is normalised to its group before toggle");
	ok &= expect(openLevelSceneGroup(&document,outer,&error) && document.activeSceneNode == outer,"open outer group directs creation",error);
	ok &= expect(document.undoStack.size() == baseline.undoStack.size() && serializeLevelMap(document).bytes == bytes
		&& document.editState == baseline.editState && document.revision > baseline.revision,"opening is session state and invalidates workers without dirtying map");
	context = levelSceneSelectionContext(document);
	ok &= expect(pickLevelSceneObjects(context,{}, {first,second,outside},LevelScenePick::Replace).size() == 2
		&& !context.closedGroups.contains("brush:0") && context.closedGroups.value("brush:1") == inner,"opened group exposes direct objects and nested closed group");
	const auto opened = document;
	ok &= expect(!moveLevelMapObject(&document,"brush",2,8,0,0,&error) && serializeLevelMap(document).bytes == bytes
		&& document.undoStack.size() == opened.undoStack.size() && document.revision == opened.revision,"outside edits refused atomically",error);
	ok &= expect(moveLevelMapObject(&document,"brush",0,8,0,0,&error),"inside brush remains editable",error);
	ok &= expect(undoLevelMapEdit(&document,&error) && serializeLevelMap(document).bytes == bytes,"inside edit undo preserves context",error);
	int added = -1;
	ok &= expect(addLevelMapBoxBrush(&document,{0,80,0,true},{16,96,16,true},"studio/new",&added,&error)
		&& levelSceneMembership(document.scene,QStringLiteral("brush:%1").arg(added)) == outer,"new geometry joins open group",error);
	ok &= undoLevelMapEdit(&document,&error);
	document.activeSceneNode = layer;
	ok &= expect(!addLevelMapBoxBrush(&document,{0,80,0,true},{16,96,16,true},"studio/new",nullptr,&error)
		&& serializeLevelMap(document).bytes == bytes,"creation cannot escape group boundary",error);
	document.activeSceneNode = outer;
	ok &= openLevelSceneGroup(&document,inner,&error);
	ok &= expect(document.openSceneGroups == QStringList{outer,inner},"nested context path");
	ok &= expect(!moveLevelMapObject(&document,"brush",0,8,0,0,&error),"parent content protected while child is open",error);
	ok &= expect(closeLevelSceneGroup(&document,false,&error) && document.openSceneGroups == QStringList{outer}
		&& document.selection == QVector<LevelMapSelectionRef>{second},"closing child selects it in parent",error);
	ok &= expect(closeLevelSceneGroup(&document,true,&error) && document.activeSceneNode == layer && document.openSceneGroups.isEmpty()
		&& document.selection.size() == 2,"closing all restores original creation destination",error);

	document = baseline; LevelPrefab prefab; LevelPrefabReport placed;
	setLevelMapSelection(&document,{first,second});
	ok &= createLevelPrefab(document,{QStringLiteral("Grouped assembly"),{},{}},&prefab,nullptr,&error);
	ok &= openLevelSceneGroup(&document,outer,&error);
	LevelPrefabPlacement placement; placement.position = {128,128,16,true};
	ok &= expect(insertLevelPrefab(&document,prefab,placement,&placed,&error) && placed.inserted.size() == 2,"prefab inserts inside open group",error);
	const auto placedContext = levelSceneSelectionContext(document);
	for (const auto& ref : placed.inserted) {
		const auto child = placedContext.closedGroups.value(levelMapSelectionRefId(ref));
		ok &= expect(!child.isEmpty() && levelSceneNode(document.scene,child)->parentId == outer,"prefab hierarchy joins open context");
	}
	ok &= expect(undoLevelMapEdit(&document,&error) && serializeLevelMap(document).bytes == bytes,"prefab context undo is exact",error);
	LevelMapDocument reopened;
	ok &= expect(loadLevelMapBytes({QStringLiteral("context.map"),{},{}},serializeLevelMap(document).bytes,&reopened,&error)
		&& reopened.openSceneGroups.isEmpty() && reopened.activeSceneNode.isEmpty(),"editing context never leaks into native persistence",error);
	ok &= expect(!setLevelMapEntityProperty(&document,0,"message","outside context",&error) && serializeLevelMap(document).bytes == bytes,"world properties stay protected while a group is open",error);

	document = baseline; QString copy;
	ok &= createLinkedLevelGroup(&document,inner,{0,192,0,true},&copy,&error);
	ok &= openLevelSceneGroup(&document,inner,&error);
	const auto linked = serializeLevelMap(document).bytes; const auto history = document.undoStack.size();
	LevelMapExtrusionReport report;
	ok &= expect(extrudeLevelMapFaces(&document,{{second,LevelBrushComponent::Face,{0}}},8,true,true,&report,&error)
		&& document.brushes.size() == 6 && document.undoStack.size() == history+1,"linked follow-up edits cross context boundary in same undo step",error);
	ok &= expect(undoLevelMapEdit(&document,&error) && serializeLevelMap(document).bytes == linked,"linked group context undo exact",error);
	const auto* linkedNode = levelSceneNode(document.scene,copy);
	if (!linkedNode || linkedNode->objects.isEmpty()) { return 1; }
	const auto member = linkedNode->objects.first(); const int linkedId = member.mid(member.indexOf(':')+1).toInt();
	ok &= expect(!moveLevelMapObject(&document,"brush",linkedId,8,0,0,&error) && serializeLevelMap(document).bytes == linked,"explicit edit to linked sibling is still blocked",error);

	document = baseline; ok &= openLevelSceneGroup(&document,inner,&error);
	ok &= removeLevelSceneNode(&document,inner,&error); reconcileLevelSceneContext(&document);
	ok &= expect(document.openSceneGroups == QStringList{outer},"removed context falls back to surviving parent");
	ok &= removeLevelSceneNode(&document,outer,&error); reconcileLevelSceneContext(&document);
	ok &= expect(document.openSceneGroups.isEmpty() && document.activeSceneNode == layer,"removed final context restores creation destination");
	document = baseline; QString ephemeral;
	ok &= createLevelSceneNode(&document,LevelSceneNodeKind::Group,"Ephemeral",{},&ephemeral,&error);
	ok &= openLevelSceneGroup(&document,ephemeral,&error);
	ok &= expect(undoLevelMapEdit(&document,&error) && document.openSceneGroups.isEmpty() && document.activeSceneNode == layer
		&& serializeLevelMap(document).bytes == bytes,"undo of group creation repairs editing context without another history step",error);
	ok &= expect(redoLevelMapEdit(&document,&error) && levelSceneNode(document.scene,ephemeral) && document.openSceneGroups.isEmpty(),"redo does not reopen session context",error);

	document = baseline;
	ok &= setLevelSceneVisible(&document,inner,false,&error);
	context = levelSceneSelectionContext(document);
	ok &= expect(pickLevelSceneObjects(context,{}, {first},LevelScenePick::Replace).contains(second),"closed group includes hidden descendants for whole-unit transforms");
	ok &= setLevelSceneLocked(&document,inner,true,&error);
	ok &= openLevelSceneGroup(&document,inner,&error);
	ok &= expect(!moveLevelMapObject(&document,"brush",1,8,0,0,&error),"opening a group cannot bypass its persistent lock",error);

	LevelMapDocument entities;
	ok &= tests::loadPrefabFixture(&entities,&error);
	QString entityGroup,otherGroup;
	ok &= createLevelSceneNode(&entities,LevelSceneNodeKind::Group,"Door",{},&entityGroup,&error);
	ok &= assignLevelSceneObjects(&entities,entityGroup,{"entity:1"},&error);
	context = levelSceneSelectionContext(entities);
	ok &= expect(pickLevelSceneObjects(context,{},{{LevelMapSelectionKind::QuakeBrush,0}},LevelScenePick::Replace)
		== QVector<LevelMapSelectionRef>{{LevelMapSelectionKind::Entity,1}},"brush entity selects one owner without double-transforming children");
	ok &= openLevelSceneGroup(&entities,entityGroup,&error);
	const auto entityBytes = serializeLevelMap(entities).bytes;
	ok &= expect(moveLevelMapObject(&entities,"entity",1,8,0,0,&error) && undoLevelMapEdit(&entities,&error)
		&& serializeLevelMap(entities).bytes == entityBytes,"open entity group owns all unassigned primitives",error);
	ok &= createLevelSceneNode(&entities,LevelSceneNodeKind::Group,"Other",{},&otherGroup,&error);
	ok &= assignLevelSceneObjects(&entities,otherGroup,{"brush:0"},&error);
	const auto separated = serializeLevelMap(entities).bytes;
	ok &= expect(!moveLevelMapObject(&entities,"entity",1,8,0,0,&error) && serializeLevelMap(entities).bytes == separated,
		"explicit primitive membership outside context protects it from owner transforms",error);

	for (const QString& game : {QStringLiteral("doom"),QStringLiteral("hexen")}) {
		LevelMapCreateRequest request; request.game = game; LevelMapDocument doom; QString room;
		ok &= createLevelMap(request,&doom,&error);
		ok &= createLevelSceneNode(&doom,LevelSceneNodeKind::Group,"Room",{},&room,&error);
		ok &= assignLevelSceneObjects(&doom,room,{"sector:0"},&error);
		ok &= openLevelSceneGroup(&doom,room,&error);
		context = levelSceneSelectionContext(doom);
		ok &= expect(context.allowedObjects.contains("linedef:0") && context.allowedObjects.contains("vertex:0")
			&& !context.allowedObjects.contains("thing:0"),"Doom group inherits exclusive boundaries but not unrelated things");
		const auto before = serializeLevelMap(doom).bytes;
		ok &= closeLevelSceneGroup(&doom,true,&error);
		context = levelSceneSelectionContext(doom);
		setLevelMapSelection(&doom,pickLevelSceneObjects(context,{},{{LevelMapSelectionKind::DoomLinedef,0}},LevelScenePick::Replace));
		const auto vertices = doom.doomVertices;
		ok &= expect(moveLevelMapSelection(&doom,8,0,0,&error),"closed sector group moves its complete boundary",error);
		for (qsizetype i = 0; i < vertices.size(); ++i) {
			ok &= expect(doom.doomVertices[i].x == vertices[i].x+8 && doom.doomVertices[i].y == vertices[i].y,"Doom group moves shared endpoints once");
		}
		ok &= expect(undoLevelMapEdit(&doom,&error) && serializeLevelMap(doom).bytes == before,"Doom group undo restores exact native bytes",error);
		ok &= openLevelSceneGroup(&doom,room,&error);
		ok &= expect(moveLevelMapObject(&doom,"vertex",0,8,0,0,&error) && undoLevelMapEdit(&doom,&error)
			&& serializeLevelMap(doom).bytes == before,"exclusive boundary is editable inside its open group",error);
		ok &= expect(!moveLevelMapObject(&doom,"thing",0,8,0,0,&error) && serializeLevelMap(doom).bytes == before,"Doom outside thing is protected",error);
		ok &= assignLevelSceneObjects(&doom,{}, {"sector:0"},&error);
		ok &= assignLevelSceneObjects(&doom,room,{"linedef:0"},&error);
		const auto partial = serializeLevelMap(doom).bytes;
		ok &= expect(!moveLevelMapObject(&doom,"linedef",0,8,0,0,&error) && serializeLevelMap(doom).bytes == partial,
			"line context refuses to move endpoints shared with outside lines",error);
		ok &= assignLevelSceneObjects(&doom,room,{"vertex:0"},&error);
		const auto explicitVertex = serializeLevelMap(doom).bytes;
		ok &= expect(!moveLevelMapObject(&doom,"vertex",0,8,0,0,&error) && serializeLevelMap(doom).bytes == explicitVertex,
			"explicit vertex membership cannot deform an outside sector",error);
	}
	LevelMapDocument udmf; QString udmfGroup;
	ok &= loadLevelMapBytes({QStringLiteral("context.wad"),QStringLiteral("MAP01"),{}},tests::udmf::fixture(),&udmf,&error);
	ok &= createLevelSceneNode(&udmf,LevelSceneNodeKind::Group,"UDMF context",{},&udmfGroup,&error);
	ok &= assignLevelSceneObjects(&udmf,udmfGroup,{"thing:0"},&error);
	ok &= openLevelSceneGroup(&udmf,udmfGroup,&error);
	const auto udmfBytes = serializeLevelMap(udmf).bytes;
	ok &= expect(editLevelMapUdmfProperties(&udmf,{{"thing:0","user_extra","42"}},&error) && undoLevelMapEdit(&udmf,&error)
		&& serializeLevelMap(udmf).bytes == udmfBytes,"inside UDMF extensions remain editable with exact undo",error);
	for (const QString& object : {QStringLiteral("global"),QStringLiteral("vertex:0"),QStringLiteral("sidedef:0")}) {
		const auto undoSize = udmf.undoStack.size(); const auto revision = udmf.revision;
		ok &= expect(!editLevelMapUdmfProperties(&udmf,{{"thing:0","user_extra","42"},{object,"user_outside","7"}},&error)
			&& serializeLevelMap(udmf).bytes == udmfBytes && udmf.undoStack.size() == undoSize && udmf.revision == revision,
			"UDMF outside extension properties refuse the entire mixed batch",error);
	}
	ok &= assignLevelSceneObjects(&udmf,{}, {"thing:0"},&error);
	ok &= assignLevelSceneObjects(&udmf,udmfGroup,{"sector:0"},&error);
	ok &= expect(editLevelMapUdmfProperties(&udmf,{{"vertex:0","user_extra","42"},{"sidedef:0","user_extra","7"}},&error),
		"exclusive UDMF boundary extensions follow their sector's context",error);

	LevelMapDocument large;
	ok &= tests::createGeometryFixture(10000,&large,&error);
	LevelSceneNode batch; batch.id = QStringLiteral("large-group"); batch.name = QStringLiteral("Large group"); batch.kind = LevelSceneNodeKind::Group;
	QVector<LevelMapSelectionRef> hits;
	for (const auto& brush : large.brushes) { const LevelMapSelectionRef ref{LevelMapSelectionKind::QuakeBrush,brush.id}; hits << ref; batch.objects << levelMapSelectionRefId(ref); }
	large.scene.nodes << batch;
	QElapsedTimer timer; timer.start(); context = levelSceneSelectionContext(large);
	const auto all = pickLevelSceneObjects(context,{},hits,LevelScenePick::Replace);
	ok &= expect(all.size() == 10000 && pickLevelSceneObjects(context,all,hits,LevelScenePick::Toggle).isEmpty(),"large band resolves one unit and toggles once");
	std::cout << "10,000-brush group index, selection and toggle: " << timer.elapsed() << " ms\n";
	if (!ok) { return 1; }
	std::cout << "Group context selection, isolation, creation, nested navigation and linked undo passed.\n"; return 0;
}
