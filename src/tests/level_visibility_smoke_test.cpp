#include "core/level_visibility.h"
#include "core/level_placement.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_linked_groups.h"
#include "core/level_repeat.h"
#include "core/level_editor_controls.h"
#include "tests/level_prefab_test_helpers.h"
#include "tests/level_udmf_test_helpers.h"
#include "core/level_object_selection.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
void expect(bool result, const char* message, const QString& error = {}) {
	if (!result) { ok = false; std::cerr << "FAIL: " << message << ": " << error.toStdString() << std::endl; }
}
LevelMapSelectionRef brush(int id) { return {LevelMapSelectionKind::QuakeBrush,id}; }
LevelMapDocument fixture(int count = 4) {
	LevelMapDocument map; QString error;
	expect(tests::createGeometryFixture(count,&map,&error),"fixture",error); markLevelMapSaved(&map); return map;
}
bool visibility(LevelMapDocument* map, LevelVisibilityOperation op, bool undoable = true) {
	QString error; const bool result = changeLevelMapVisibility(map,{op,undoable},nullptr,&error);
	expect(result,"visibility service",error); return result;
}
void history()
{
	auto map = fixture(); QString error; const auto bytes = serializeLevelMap(map).bytes, scene = encodeLevelScene(map,{},{});
	setLevelMapSelection(&map,{brush(1),brush(2)});
	const auto revision = map.revision, serial = map.historyRevision;
	visibility(&map,LevelVisibilityOperation::HideSelected);
	expect(map.hiddenObjects == QSet<QString>{"brush:1","brush:2"} && map.selection.isEmpty() && map.undoStack.size() == 1
		&& map.revision == revision && map.historyRevision == serial+1 && map.editState == "saved", "hide is one non-content transaction");
	expect(serializeLevelMap(map).bytes == bytes && encodeLevelScene(map,{},{}) == scene,"visibility never changes native or scene bytes");
	expect(undoLevelMapEdit(&map,&error) && map.hiddenObjects.isEmpty() && map.selection == QVector<LevelMapSelectionRef>{brush(1),brush(2)}
		&& map.editState == "saved", "undo restores exact selection and visibility",error);
	expect(redoLevelMapEdit(&map,&error) && map.selection.isEmpty() && map.hiddenObjects.size() == 2,"redo hides",error);
	visibility(&map,LevelVisibilityOperation::ShowAll);
	expect(map.hiddenObjects.isEmpty() && map.selection.isEmpty() && map.undoStack.size() == 2,"show all is a single visibility transaction");
	markLevelMapSaved(&map); undoLevelMapEdit(&map); expect(map.editState == "saved","undo across a saved visibility step remains clean");
	const auto before = map;
	expect(selectAndRevealLevelMapObjects(&map,{brush(1)},true,&error) && map.hiddenObjects == QSet<QString>{"brush:2"}
		&& map.selection == QVector<LevelMapSelectionRef>{brush(1)} && map.redoStack.isEmpty(),"object list reveal and selection share a branch",error);
	expect(undoLevelMapEdit(&map,&error) && map.hiddenObjects == before.hiddenObjects && map.selection == before.selection,"undo reveal",error);
	visibility(&map,LevelVisibilityOperation::ShowAll,false);
	const auto depth = map.undoStack.size(), redo = map.redoStack.size(); const auto stamp = map.historyRevision;
	visibility(&map,LevelVisibilityOperation::ShowAll,false);
	expect(map.undoStack.size() == depth && map.redoStack.size() == redo && map.historyRevision == stamp,"transient no-op preserves timeline");
	setLevelMapSelection(&map,{brush(0)}); visibility(&map,LevelVisibilityOperation::HideSelected,false);
	expect(map.undoStack.size() == depth && map.redoStack.size() == redo && map.historyRevision == stamp+1,"NRC transient hiding invalidates staged work without changing undo");
	map = fixture(); map.undoLimit = 1; setLevelMapSelection(&map,{brush(0)});
	visibility(&map,LevelVisibilityOperation::HideSelected); visibility(&map,LevelVisibilityOperation::ShowAll);
	expect(map.undoStack.size() == 1 && map.editState == "saved" && undoLevelMapEdit(&map) && map.hiddenObjects.contains("brush:0"),"bounded visibility history keeps saved equivalence");
}
void hierarchy()
{
	auto map = fixture(); QString error, group;
	setLevelMapSelection(&map,{brush(0),brush(1)});
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Pair",{},&group,&error)
		&& assignLevelSceneObjects(&map,group,{"brush:0","brush:1"},&error),"create group",error);
	map.undoStack.clear(); markLevelMapSaved(&map);
	setLevelMapSelection(&map,{brush(0)}); visibility(&map,LevelVisibilityOperation::HideSelected);
	expect(map.hiddenObjects == QSet<QString>{"brush:0","brush:1"},"closed group hide is atomic");
	undoLevelMapEdit(&map); setLevelMapSelection(&map,{brush(2)}); setLevelSceneLocked(&map,group,true,&error);
	visibility(&map,LevelVisibilityOperation::Isolate);
	expect(map.hiddenObjects == QSet<QString>{"brush:0","brush:1","brush:3"} && map.selection == QVector<LevelMapSelectionRef>{brush(2)},"isolate hides locked objects outside selected unit");
	visibility(&map,LevelVisibilityOperation::ShowAll); setLevelSceneVisible(&map,group,false,&error);
	visibility(&map,LevelVisibilityOperation::ShowAll);
	expect(map.hiddenObjects.isEmpty() && levelMapHiddenObjects(map).contains("brush:0"),"show all preserves persistent scene visibility");
	map = fixture(); setLevelMapSelection(&map,{brush(0),brush(1)});
	int owner = -1;
	expect(tieLevelMapSelectionToEntity(&map,"func_detail",{},&owner,&error),"brush entity",error);
	QVector<LevelMapSelectionRef> children;
	for (const auto& child : map.brushes) { if (child.entityId == owner) { children.append(brush(child.id)); } }
	if (children.size() != 2) { expect(false,"entity children"); return; }
	const auto first = levelMapSelectionRefId(children[0]), second = levelMapSelectionRefId(children[1]);
	const auto ownerKey = QStringLiteral("entity:%1").arg(owner);
	setLevelMapSelection(&map,{children[0]}); visibility(&map,LevelVisibilityOperation::Isolate);
	auto hidden = levelMapHiddenObjects(map);
	expect(!hidden.contains(first) && !hidden.contains(ownerKey) && hidden.contains(second),"isolating a child keeps its owner but hides siblings");
	setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,owner}}); visibility(&map,LevelVisibilityOperation::Isolate);
	hidden = levelMapHiddenObjects(map);
	expect(!hidden.contains(first) && !hidden.contains(second) && hidden.contains("brush:2"),"isolating an owner includes children");
	visibility(&map,LevelVisibilityOperation::HideSelected);
	expect(levelMapHiddenObjects(map).contains(first) && levelMapHiddenObjects(map).contains(ownerKey),"entity hiding inherits");
	expect(selectAndRevealLevelMapObjects(&map,{children[0]},true,&error) && !levelMapHiddenObjects(map).contains(first),"revealing hidden child reveals hidden owner",error);
	map.hiddenObjects = {first};
	expect(selectAndRevealLevelMapObjects(&map,{{LevelMapSelectionKind::Entity,owner}},true,&error)
		&& levelMapHiddenObjects(map).contains(first) && !levelMapHiddenObjects(map).contains(second),"selecting owner preserves independently hidden children",error);
	map = fixture(); map.hiddenObjects = {"brush:1"}; setLevelMapSelection(&map,{brush(0)});
	LevelObjectSelectionReport query; expect(selectLevelMapObjects(&map,{},&query,&error) && !query.selection.contains(brush(1)),"headless selection respects session visibility",error);
}
void structural()
{
	auto map = fixture(); QString error;
	map.hiddenObjects = {"brush:1","brush:2"}; setLevelMapSelection(&map,{brush(1)});
	expect(deleteLevelMapSelection(&map,&error) && map.hiddenObjects == QSet<QString>{"brush:2"},"delete removes hidden identity",error);
	map.hiddenObjects.insert("brush:3");
	expect(undoLevelMapEdit(&map,&error) && map.hiddenObjects == QSet<QString>{"brush:1","brush:2","brush:3"},"undo deletion restores hidden identity and preserves later transient hiding",error);
	expect(redoLevelMapEdit(&map,&error) && map.hiddenObjects == QSet<QString>{"brush:2","brush:3"},"redo deletion remaps current visibility",error);
	undoLevelMapEdit(&map); visibility(&map,LevelVisibilityOperation::ShowAll,false); redoLevelMapEdit(&map); undoLevelMapEdit(&map);
	expect(map.hiddenObjects.isEmpty(),"transient Show All is preserved across later delete redo/undo");
	map = fixture(); int inserted = -1;
	expect(addLevelMapBoxBrush(&map,{128,0,0,true},{160,32,32,true},"visibility/test",&inserted,&error),"append fixture",error);
	setLevelMapSelection(&map,{brush(inserted)}); visibility(&map,LevelVisibilityOperation::HideSelected,false);
	expect(undoLevelMapEdit(&map,&error) && map.hiddenObjects.isEmpty() && redoLevelMapEdit(&map,&error)
		&& map.hiddenObjects == QSet<QString>{QStringLiteral("brush:%1").arg(inserted)},"hidden inserted object survives undo and redo",error);
	visibility(&map,LevelVisibilityOperation::ShowAll,false); undoLevelMapEdit(&map); redoLevelMapEdit(&map);
	expect(map.hiddenObjects.isEmpty(),"later transient reveal replaces remembered insertion visibility");
	for (const auto& game : {QStringLiteral("doom"),QStringLiteral("hexen"),QStringLiteral("quake"),QStringLiteral("quake2"),QStringLiteral("quake3")}) {
		LevelMapCreateRequest request; request.game = game;
		expect(createLevelMap(request,&map,&error),"native fixture",error); markLevelMapSaved(&map);
		const auto bytes = serializeLevelMap(map).bytes; const auto revision = map.revision; const auto nodeEdits = map.doomGeometryEdits;
		const auto target = map.format == LevelMapFormat::DoomWad ? LevelMapSelectionRef{LevelMapSelectionKind::DoomLinedef,0} : brush(0);
		setLevelMapSelection(&map,{target}); visibility(&map,LevelVisibilityOperation::Isolate);
		expect(!levelMapHiddenObjects(map).contains(levelMapSelectionRefId(target)) && map.revision == revision && map.doomGeometryEdits == nodeEdits
			&& serializeLevelMap(map).bytes == bytes && undoLevelMapEdit(&map,&error) && serializeLevelMap(map).bytes == bytes,"native visibility preserves contents and node validity",error);
		if (map.format == LevelMapFormat::DoomWad) {
			for (const auto kind : {LevelMapSelectionKind::DoomVertex,LevelMapSelectionKind::DoomSector,LevelMapSelectionKind::DoomThing}) {
				setLevelMapSelection(&map,{{kind,0}}); visibility(&map,LevelVisibilityOperation::Isolate);
				const auto hidden = levelMapHiddenObjects(map);
				expect(!hidden.contains(levelMapSelectionRefId({kind,0})),"Doom isolated primitive remains visible");
				if (kind == LevelMapSelectionKind::DoomVertex) {
					for (const auto& line : map.doomLinedefs) {
						if (line.startVertex != 0 && line.endVertex != 0) { continue; }
						expect(!hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomLinedef,line.id}))
							&& !hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomVertex,line.startVertex}))
							&& !hidden.contains(levelMapSelectionRefId({LevelMapSelectionKind::DoomVertex,line.endVertex})),
							"Doom isolated vertex retains incident geometry");
					}
				}
				expect(undoLevelMapEdit(&map,&error) && map.hiddenObjects.isEmpty() && serializeLevelMap(map).bytes == bytes,
					"Doom primitive visibility undo preserves contents",error);
			}
			map.hiddenObjects = {"sector:0","linedef:0"};
			expect(selectAndRevealLevelMapObjects(&map,{{LevelMapSelectionKind::DoomLinedef,0}},false,&error)
				&& !levelMapHiddenObjects(map).contains("linedef:0"),"Doom line reveal clears hidden sector dependencies",error);
			map.hiddenObjects = {"sector:0"};
			expect(selectAndRevealLevelMapObjects(&map,{{LevelMapSelectionKind::DoomVertex,0}},false,&error)
				&& !levelMapHiddenObjects(map).contains("vertex:0"),"Doom vertex reveal clears hidden incident geometry",error);
			map.hiddenObjects = {"linedef:0","linedef:2"};
			expect(deleteLevelMapObjects(&map,{{LevelMapSelectionKind::DoomLinedef,0}},&error)
				&& map.hiddenObjects == QSet<QString>{"linedef:1"},"Doom compaction preserves hidden identity",error);
			map.hiddenObjects.insert("linedef:0");
			expect(undoLevelMapEdit(&map,&error) && map.hiddenObjects == QSet<QString>{"linedef:0","linedef:1","linedef:2"},"Doom undo remaps later hiding and restores deleted hidden line",error);
			expect(redoLevelMapEdit(&map,&error) && map.hiddenObjects == QSet<QString>{"linedef:0","linedef:1"},"Doom redo keeps current hidden survivors",error);
		}
	}
}
void inheritedHierarchy()
{
	auto map = fixture(5); QString error, outer, inner, empty;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Outer",{},&outer,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Inner",outer,&inner,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Empty",inner,&empty,&error)
		&& assignLevelSceneObjects(&map,outer,{"brush:0"},&error)
		&& assignLevelSceneObjects(&map,inner,{"brush:1"},&error),"nested visibility fixture",error);
	const auto outerKey = QStringLiteral("group:")+outer, innerKey = QStringLiteral("group:")+inner, emptyKey = QStringLiteral("group:")+empty;
	map.undoStack.clear(); markLevelMapSaved(&map); const auto original = serializeLevelMap(map).bytes;
	map.hiddenObjects = {"brush:1"}; map.shownObjects = {innerKey}; setLevelMapSelection(&map,{brush(0)});
	expect(changeLevelMapVisibility(&map,{LevelVisibilityOperation::HideSelected,true,true},nullptr,&error)
		&& map.hiddenObjects == QSet<QString>{outerKey,"brush:1"} && map.shownObjects.isEmpty()
		&& levelMapHiddenObjects(map).contains("brush:0") && levelMapHiddenObjects(map).contains("brush:1"),
		"hide container resets shown descendants but preserves hidden descendants",error);
	expect(serializeLevelMap(map).bytes == original && map.editState == "saved","container state is session only");
	expect(undoLevelMapEdit(&map) && map.shownObjects == QSet<QString>{innerKey} && map.hiddenObjects == QSet<QString>{"brush:1"}
		&& redoLevelMapEdit(&map) && map.shownObjects.isEmpty(),"container visibility exact undo redo");
	setLevelMapSelection(&map,{brush(2)});
	expect(assignLevelSceneObjects(&map,inner,{"brush:2"},&error) && levelMapHiddenObjects(map).contains("brush:2")
		&& map.selection.isEmpty(),"existing member inherits hidden group and is deselected",error);
	expect(undoLevelMapEdit(&map) && !levelMapHiddenObjects(map).contains("brush:2") && map.selection == QVector<LevelMapSelectionRef>{brush(2)}
		&& redoLevelMapEdit(&map) && levelMapHiddenObjects(map).contains("brush:2"),"membership undo preserves inherited state");
	map.activeSceneNode = inner; int created = -1; const auto depth = map.undoStack.size();
	expect(addLevelMapBoxBrush(&map,{300,0,0,true},{332,32,32,true},"visibility/new",&created,&error),"create in hidden group",error);
	const auto createdKey = levelMapSelectionRefId(brush(created));
	expect(map.shownObjects.contains(createdKey) && !levelMapHiddenObjects(map).contains(createdKey)
		&& map.undoStack.size() == depth+1,"authoring ensures new object visible in same history step");
	expect(undoLevelMapEdit(&map) && !map.shownObjects.contains(createdKey) && redoLevelMapEdit(&map)
		&& map.shownObjects.contains(createdKey),"created shown identity survives undo redo");
	const auto beforeReveal = map;
	expect(selectAndRevealLevelMapObjects(&map,{brush(0)},true,&error) && map.shownObjects.contains("brush:0")
		&& map.hiddenObjects.contains(outerKey) && !levelMapHiddenObjects(map).contains("brush:0")
		&& levelMapHiddenObjects(map).contains("brush:2"),"reveal hidden child leaves container and sibling hidden",error);
	expect(undoLevelMapEdit(&map) && map.shownObjects == beforeReveal.shownObjects && redoLevelMapEdit(&map),"reveal exact history");
	const auto shownBeforeHide = map.shownObjects;
	expect(changeLevelMapVisibility(&map,{LevelVisibilityOperation::HideSelected,true,true},nullptr,&error)
		&& map.shownObjects.isEmpty() && undoLevelMapEdit(&map) && map.shownObjects == shownBeforeHide,
		"rehide clears descendant overrides and undo restores them",error);
	expect(removeLevelSceneNode(&map,outer,&error) && !map.hiddenObjects.contains(outerKey)
		&& undoLevelMapEdit(&map) && map.hiddenObjects.contains(outerKey)
		&& redoLevelMapEdit(&map) && !map.hiddenObjects.contains(outerKey),"removed group visibility tracks UUID through history",error);
	undoLevelMapEdit(&map); visibility(&map,LevelVisibilityOperation::ShowAll,false);
	expect(map.hiddenObjects.isEmpty() && map.shownObjects.isEmpty(),"show all clears hidden and shown states");
	setLevelMapSelection(&map,{brush(0)});
	expect(changeLevelMapVisibility(&map,{LevelVisibilityOperation::Isolate,true,true},nullptr,&error)
		&& map.shownObjects.contains(emptyKey) && map.shownObjects.contains(innerKey)
		&& map.hiddenObjects.contains("brush:3") && !levelMapHiddenObjects(map).contains("brush:2"),"isolate closed group keeps nested empty containers",error);
	visibility(&map,LevelVisibilityOperation::ShowAll,false); setLevelSceneVisible(&map,outer,false,&error);
	map.shownObjects = {"brush:0",outerKey};
	expect(levelMapHiddenObjects(map).contains("brush:0"),"session overrides respect saved scene visibility");
	map = fixture(); setLevelMapSelection(&map,{brush(0),brush(1)}); int owner = -1;
	expect(tieLevelMapSelectionToEntity(&map,"func_detail",{},&owner,&error),"owner fixture",error);
	QVector<LevelMapSelectionRef> children;
	for (const auto& item : map.brushes) { if (item.entityId == owner) { children.append(brush(item.id)); } }
	const auto ownerKey = levelMapSelectionRefId({LevelMapSelectionKind::Entity,owner});
	map.hiddenObjects = {ownerKey};
	expect(children.size() == 2 && selectAndRevealLevelMapObjects(&map,{children.first()},true,&error)
		&& map.hiddenObjects.contains(ownerKey) && map.shownObjects.contains(levelMapSelectionRefId(children.first()))
		&& levelMapHiddenObjects(map).contains(levelMapSelectionRefId(children.last()))
		&& buildLevelMapPreviewMesh(map).triangles == 36,"explicit child override renders below hidden owner without revealing sibling",error);
	const auto childKey = levelMapSelectionRefId(children.first());
	expect(deleteLevelMapSelection(&map,&error) && !map.shownObjects.contains(childKey)
		&& undoLevelMapEdit(&map) && map.shownObjects.contains(childKey),"deleted shown object restored by identity",error);
	visibility(&map,LevelVisibilityOperation::ShowAll,false); redoLevelMapEdit(&map); undoLevelMapEdit(&map);
	expect(map.shownObjects.isEmpty(),"later transient show all replaces remembered shown state");
	QString ownerGroup, childLayer;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Owner group",{},&ownerGroup,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Independent child",{},&childLayer,&error)
		&& assignLevelSceneObjects(&map,ownerGroup,{ownerKey},&error)
		&& assignLevelSceneObjects(&map,childLayer,{childKey},&error),"independent membership fixture",error);
	setLevelMapSelection(&map,{children.first()});
	expect(changeLevelMapVisibility(&map,{LevelVisibilityOperation::Isolate,true,true},nullptr,&error)
		&& !levelMapHiddenObjects(map).contains(childKey) && levelMapHiddenObjects(map).contains(levelMapSelectionRefId(children.last())),
		"keeping a required owner does not select its closed group and reveal siblings",error);

}
void layerTransfersAndCopies()
{
	auto map = fixture(4); QString error, layerA, layerB, outer, inner, other;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"A",{},&layerA,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"B",{},&layerB,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Outer",layerA,&outer,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Inner",outer,&inner,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Other",layerA,&other,&error)
		&& assignLevelSceneObjects(&map,outer,{"brush:0"},&error)
		&& assignLevelSceneObjects(&map,inner,{"brush:1"},&error),"layer fixtures",error);
	const auto group = [](const QString& id) { return QStringLiteral("group:")+id; };
	map.hiddenObjects = {group(outer),"brush:1"}; map.shownObjects = {"brush:0",group(inner)};
	setLevelMapSelection(&map,{brush(0)}); map.undoStack.clear(); map.redoStack.clear();
	const auto before = map; const auto bytes = serializeLevelMap(map).bytes;
	expect(reparentLevelSceneNode(&map,outer,other,&error) && map.shownObjects == before.shownObjects,
		"moving within same layer preserves explicit shown states",error);
	expect(undoLevelMapEdit(&map) && serializeLevelMap(map).bytes == bytes,"same-layer move exact undo");
	expect(reparentLevelSceneNode(&map,outer,layerB,&error) && map.shownObjects.isEmpty()
		&& map.hiddenObjects == before.hiddenObjects && map.selection.isEmpty(),"layer move resets shown descendants, preserves hidden and removes hidden selection",error);
	expect(undoLevelMapEdit(&map) && map.shownObjects == before.shownObjects && map.selection == before.selection
		&& serializeLevelMap(map).bytes == bytes && redoLevelMapEdit(&map) && map.shownObjects.isEmpty(),"layer transfer exact history");
	map.hiddenObjects.insert("brush:0");
	expect(undoLevelMapEdit(&map) && !map.shownObjects.contains("brush:0") && map.hiddenObjects.contains("brush:0"),"later transient hide survives transfer undo");
	visibility(&map,LevelVisibilityOperation::ShowAll,false); redoLevelMapEdit(&map); undoLevelMapEdit(&map);
	expect(map.shownObjects.isEmpty(),"later transient reset replaces remembered layer shown states");
	map = before;
	expect(assignLevelSceneObjects(&map,layerB,{"brush:0"},&error) && !map.shownObjects.contains("brush:0")
		&& map.shownObjects.contains(group(inner)) && undoLevelMapEdit(&map) && map.shownObjects == before.shownObjects,
		"direct object layer assignment resets only transferred state",error);
	map = before;
	expect(removeLevelSceneNode(&map,layerA,&error) && map.shownObjects.isEmpty() && map.hiddenObjects == before.hiddenObjects
		&& map.selection.isEmpty() && undoLevelMapEdit(&map) && map.shownObjects == before.shownObjects
		&& map.selection == before.selection && serializeLevelMap(map).bytes == bytes,
		"removing a layer transfers descendants to Default and restores their states on undo",error);
	map = before;
	expect(resetLevelScene(&map,&error) && map.shownObjects.isEmpty() && map.hiddenObjects == QSet<QString>{"brush:1"}
		&& map.selection == before.selection && undoLevelMapEdit(&map) && map.shownObjects == before.shownObjects
		&& map.hiddenObjects == before.hiddenObjects && serializeLevelMap(map).bytes == bytes,
		"scene reset prunes removed group states and restores surviving object exceptions on undo",error);
	map = before; QString fresh;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Fresh",outer,&fresh,&error) && map.shownObjects.contains(group(fresh))
		&& undoLevelMapEdit(&map) && !map.shownObjects.contains(group(fresh)) && redoLevelMapEdit(&map) && map.shownObjects.contains(group(fresh)),
		"new empty group in hidden parent is ensured visible in its creation step",error);
	map = before; setLevelMapSelection(&map,{brush(0),brush(1)});
	const auto sourceHidden = map.hiddenObjects, sourceShown = map.shownObjects;
	const auto baseline = serializeLevelMap(map).bytes; const auto depth = map.undoStack.size();
	expect(duplicateLevelMapSelection(&map,256,0,0,&error) && map.undoStack.size() == depth+1,"duplicate visibility hierarchy is one authoring step",error);
	QString copiedOuter, copiedInner;
	for (const auto& node : map.scene.nodes) { if (node.name == "Outer 2") { copiedOuter = node.id; } }
	for (const auto& node : map.scene.nodes) { if (node.parentId == copiedOuter && node.name == "Inner") { copiedInner = node.id; } }
	expect(!copiedOuter.isEmpty() && !copiedInner.isEmpty() && map.shownObjects.contains(group(copiedOuter))
		&& map.shownObjects.contains(group(copiedInner)),"copied hidden root is revealed and descendant shown attribute retained");
	const auto* copiedRootNode = levelSceneNode(map.scene,copiedOuter);
	const auto* copiedInnerNode = levelSceneNode(map.scene,copiedInner);
	if (!copiedRootNode || !copiedInnerNode || copiedRootNode->objects.isEmpty() || copiedInnerNode->objects.isEmpty()) { expect(false,"copied nodes exist"); return; }
	const auto shownCopy = copiedRootNode->objects.first(), hiddenCopy = copiedInnerNode->objects.first();
	expect(map.shownObjects.contains(shownCopy) && map.hiddenObjects.contains(hiddenCopy) && levelMapHiddenObjects(map).contains(hiddenCopy)
		&& !levelMapHiddenObjects(map).contains(shownCopy),"hidden descendants are not revealed by duplicate root insertion");
	const auto finalHidden = map.hiddenObjects, finalShown = map.shownObjects;
	expect(undoLevelMapEdit(&map) && map.hiddenObjects == sourceHidden && map.shownObjects == sourceShown
		&& serializeLevelMap(map).bytes == baseline && redoLevelMapEdit(&map) && map.hiddenObjects == finalHidden && map.shownObjects == finalShown,
		"copied hierarchy states have exact undo redo");
	visibility(&map,LevelVisibilityOperation::ShowAll,false); undoLevelMapEdit(&map); redoLevelMapEdit(&map);
	expect(map.hiddenObjects.isEmpty() && map.shownObjects.isEmpty(),"redo copy preserves later session reset rather than recopying source attributes");
	map = before; setLevelMapSelection(&map,{brush(0),brush(1)});
	expect(arrayLevelMapSelection(&map,{256,0,0,true},2,{true,false},&error),"array hierarchy states",error);
	int copiedHidden = 0;
	for (const auto& id : map.hiddenObjects) { if (id.startsWith("brush:") && id != "brush:1") { ++copiedHidden; } }
	expect(copiedHidden == 2 && undoLevelMapEdit(&map) && map.hiddenObjects == before.hiddenObjects && map.shownObjects == before.shownObjects,
		"array carries independent descendant attributes in a single undo");
	map = fixture(); setLevelMapSelection(&map,{brush(0),brush(1)}); int owner = -1;
	expect(tieLevelMapSelectionToEntity(&map,"func_detail",{},&owner,&error),"copy owner fixture",error);
	QStringList children;
	for (const auto& item : map.brushes) { if (item.entityId == owner) { children << levelMapSelectionRefId(brush(item.id)); } }
	if (children.size() != 2) { expect(false,"owner children"); return; }
	QString ownerLayer, childLayer;
	const auto ownerKey = levelMapSelectionRefId({LevelMapSelectionKind::Entity,owner});
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Owner layer",{},&ownerLayer,&error)
		&& createLevelSceneNode(&map,LevelSceneNodeKind::Layer,"Child layer",{},&childLayer,&error)
		&& assignLevelSceneObjects(&map,ownerLayer,{ownerKey},&error)
		&& assignLevelSceneObjects(&map,childLayer,{children.first()},&error),"owner layer fixture",error);
	map.shownObjects = {ownerKey,children.first(),children.last()};
	expect(assignLevelSceneObjects(&map,childLayer,{ownerKey},&error) && map.shownObjects == QSet<QString>{children.first()}
		&& undoLevelMapEdit(&map) && map.shownObjects == QSet<QString>{ownerKey,children.first(),children.last()},
		"owner layer transfer resets inherited children and retains independently assigned child state",error);
	map.hiddenObjects = {children.last()}; map.shownObjects = {children.first()}; setLevelMapSelection(&map,{{LevelMapSelectionKind::Entity,owner}});
	expect(duplicateLevelMapSelection(&map,256,0,0,&error),"owner duplicate",error);
	const auto& duplicate = map.undoStack.last();
	for (auto it = duplicate.sceneObjectOrigins.cbegin(); it != duplicate.sceneObjectOrigins.cend(); ++it) {
		if (it.value() == children.last()) { expect(map.hiddenObjects.contains(it.key()),"owned hidden child survives recursive clone"); }
		if (it.value() == children.first()) { expect(map.shownObjects.contains(it.key()),"owned shown child survives recursive clone"); }
	}
	map = fixture(); QString linked;
	expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Linked",{},&linked,&error)
		&& assignLevelSceneObjects(&map,linked,{"brush:0","brush:1"},&error),"linked visibility fixture",error);
	map.shownObjects = {group(linked)}; QString linkedCopy;
	expect(createLinkedLevelGroup(&map,linked,{256,0,0,true},&linkedCopy,&error) && map.shownObjects.contains(group(linkedCopy)),
		"linked copy carries explicit root shown attribute",error);
	map.hiddenObjects = {group(linkedCopy),"brush:0"}; map.shownObjects = {group(linked),"brush:1"};
	const auto linkedBefore = map;
	expect(moveLevelMapObject(&map,"brush",1,0,0,16,&error),"linked edit with independent visibility",error);
	const auto* updated = levelSceneNode(map.scene,linkedCopy);
	if (updated) {
		for (const auto& member : updated->objects) {
			expect(!map.hiddenObjects.contains(member) && !map.shownObjects.contains(member) && levelMapHiddenObjects(map).contains(member),
				"linked updates use fresh inherited children under the retained hidden target group");
		}
	}
	expect(updated && undoLevelMapEdit(&map) && map.hiddenObjects == linkedBefore.hiddenObjects && map.shownObjects == linkedBefore.shownObjects,
		"linked update restores exact visibility through compound undo");
	for (const auto& dialect : {QStringLiteral("doom"),QStringLiteral("hexen"),QStringLiteral("udmf")}) {
		LevelMapDocument native; QString things;
		const auto bytes = dialect == "udmf" ? tests::udmf::fixture() : tests::doom::fixture(dialect);
		expect(loadLevelMapBytes({"visibility.wad","MAP01","idTech1"},bytes,&native,&error)
			&& createLevelSceneNode(&native,LevelSceneNodeKind::Group,"Things",{},&things,&error)
			&& assignLevelSceneObjects(&native,things,{"thing:0"},&error),"native hierarchy copy fixture",error);
		native.hiddenObjects = {"thing:0"}; native.shownObjects = {group(things)};
		setLevelMapSelection(&native,{{LevelMapSelectionKind::DoomThing,0}}); const auto original = serializeLevelMap(native).bytes;
		expect(arrayLevelMapSelection(&native,{32,0,0,true},2,{true,false},&error),"native array copies explicit attributes",error);
		expect(native.hiddenObjects.size() == 3 && native.shownObjects.size() == 3 && undoLevelMapEdit(&native)
			&& native.hiddenObjects == QSet<QString>{"thing:0"} && native.shownObjects == QSet<QString>{group(things)}
			&& serializeLevelMap(native).bytes == original && redoLevelMapEdit(&native) && native.hiddenObjects.size() == 3,
			"binary/UDMF group copy preserves states and exact original bytes on undo",dialect+error);
	}
	map = before; setLevelMapSelection(&map,{brush(0),brush(1)}); const auto unchanged = map;
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::Duplicate; request.offset = {128,0,0,true};
	bool cancel = false; LevelPlacementControl control; control.isCancelled = [&] { return cancel; };
	control.progress = [&](const auto& progress) { cancel |= progress.phase == LevelPlacementPhase::Finalizing; };
	const auto cancelled = prepareLevelPlacement(map,request,control);
	expect(cancelled.cancelled && map.shownObjects == unchanged.shownObjects && map.hiddenObjects == unchanged.hiddenObjects
		&& map.scene == unchanged.scene && map.undoStack.size() == unchanged.undoStack.size(),"cancelled copy leaves source state and history unchanged");
}
void insertionPolicies()
{
	QString error;
	expect(netRadiantCustomLevelControls().insertionVisibility == LevelInsertionVisibility::FreshInherited
		&& trenchBroomLevelControls().insertionVisibility == LevelInsertionVisibility::PreserveAndReveal
		&& vibeStudioLevelControls().insertionVisibility == LevelInsertionVisibility::PreserveAndReveal,"profile insertion policies");
	for (const auto policy : {LevelInsertionVisibility::PreserveAndReveal,LevelInsertionVisibility::FreshInherited}) {
		const bool fresh = policy == LevelInsertionVisibility::FreshInherited;
		auto map = fixture(3); QString group;
		expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Pair",{},&group,&error)
			&& assignLevelSceneObjects(&map,group,{"brush:0","brush:1"},&error),"policy group",error);
		map.hiddenObjects = {"brush:1"}; map.shownObjects = {QStringLiteral("group:")+group};
		markLevelMapSaved(&map); const auto bytes = serializeLevelMap(map).bytes;
		const auto history = map.historyRevision, revision = map.revision;
		setLevelMapInsertionVisibility(&map,policy);
		expect(map.historyRevision == history+(fresh ? 1 : 0) && map.revision == revision && map.editState == "saved"
			&& serializeLevelMap(map).bytes == bytes,"policy changes invalidate drafts without dirtying or serializing state");
		setLevelMapSelection(&map,{brush(0),brush(1)}); const auto before = map;
		LevelPlacementRequest request; request.operation = LevelPlacementOperation::Duplicate; request.copies = 2; request.offset = {256,0,0,true};
		const auto result = prepareLevelPlacement(map,request);
		expect(result.succeeded && map.brushes.size() == 3 && result.document.brushes.size() == 7,"profile array prepared privately",result.error);
		if (!result.succeeded) { continue; } map = result.document;
		expect(map.hiddenObjects.size() == (fresh ? 1 : 3) && map.shownObjects.size() == (fresh ? 1 : 3),"fresh clone states differ from recursive attribute copies");
		const auto copiedHidden = map.hiddenObjects, copiedShown = map.shownObjects;
		if (fresh) {
			auto repeated = before;
			setLevelMapInsertionVisibility(&repeated,LevelInsertionVisibility::PreserveAndReveal);
			expect(duplicateLevelMapSelection(&repeated,128,0,0,&error),"record repeatable duplication",error);
			const auto repeatHidden = repeated.hiddenObjects, repeatShown = repeated.shownObjects;
			const auto repeatCount = repeated.brushes.size();
			setLevelMapInsertionVisibility(&repeated,LevelInsertionVisibility::FreshInherited);
			expect(repeatLevelMapCommands(&repeated,{true,false},&error) && repeated.brushes.size() > repeatCount
				&& repeated.hiddenObjects == repeatHidden && repeated.shownObjects == repeatShown,"repeat uses current fresh policy after a profile change",error);
		}
		const auto count = map.undoStack.size();
		setLevelMapInsertionVisibility(&map,fresh ? LevelInsertionVisibility::PreserveAndReveal : LevelInsertionVisibility::FreshInherited);
		expect(map.undoStack.size() == count && undoLevelMapEdit(&map) && map.hiddenObjects == before.hiddenObjects
			&& map.shownObjects == before.shownObjects && serializeLevelMap(map).bytes == bytes
			&& redoLevelMapEdit(&map) && map.hiddenObjects == copiedHidden && map.shownObjects == copiedShown,
			"changing profile does not reinterpret a completed array on undo or redo");
		map = before; map.hiddenObjects = {QStringLiteral("group:")+group}; map.shownObjects.clear(); map.activeSceneNode = group;
		int created = -1;
		expect(addLevelMapBoxBrush(&map,{512,0,0,true},{544,32,32,true},"studio/visibility",&created,&error),"author inside hidden group",error);
		const auto createdKey = levelMapSelectionRefId(brush(created));
		expect(levelMapHiddenObjects(map).contains(createdKey) == fresh && map.shownObjects.contains(createdKey) != fresh,
			"new geometry inherits or overrides hidden destination according to policy");
		expect(undoLevelMapEdit(&map) && map.hiddenObjects == QSet<QString>{QStringLiteral("group:")+group} && map.shownObjects.isEmpty(),"creation exact visibility undo");
		QString empty;
		expect(createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Empty",group,&empty,&error)
			&& map.shownObjects.contains(QStringLiteral("group:")+empty) != fresh,"empty groups follow insertion policy",error);
		expect(undoLevelMapEdit(&map),"empty group undo");
		auto clipboard = fixture(1); setLevelMapSelection(&clipboard,{brush(0)});
		expect(pasteLevelMapText(&map,levelMapSelectionText(clipboard),&error),"paste inside hidden destination",error);
		expect(map.selection.size() == 1 && levelMapHiddenObjects(map).contains(levelMapSelectionRefId(map.selection.first())) == fresh,
			"portable paste honours destination policy without copying session attributes");
	}
	LevelMapDocument owned;
	expect(tests::loadPrefabFixture(&owned,&error),"mixed clone fixture",error);
	setLevelMapSelection(&owned,{{LevelMapSelectionKind::Entity,1},{LevelMapSelectionKind::QuakePatch,0},{LevelMapSelectionKind::Entity,2}});
	owned.hiddenObjects = {"entity:1","brush:1","patch:0"}; owned.shownObjects = {"entity:2"};
	setLevelMapInsertionVisibility(&owned,LevelInsertionVisibility::FreshInherited);
	const auto original = owned;
	expect(duplicateLevelMapSelection(&owned,256,0,0,&error),"fresh mixed owner patch and point clone",error);
	expect(owned.hiddenObjects == original.hiddenObjects && owned.shownObjects == original.shownObjects,"NRC owner patch and point clones do not copy exclusion flags");
	const auto hidden = levelMapHiddenObjects(owned);
	for (const auto& ref : owned.selection) { expect(!hidden.contains(levelMapSelectionRefId(ref)),"fresh cloned root is visible without explicit flags"); }
	expect(undoLevelMapEdit(&owned) && serializeLevelMap(owned).bytes == serializeLevelMap(original).bytes,"mixed clone exact undo");
	for (const auto& dialect : {QStringLiteral("doom"),QStringLiteral("hexen"),QStringLiteral("udmf")}) {
		LevelMapDocument map; QString group;
		const auto bytes = dialect == "udmf" ? tests::udmf::fixture() : tests::doom::fixture(dialect);
		expect(loadLevelMapBytes({"fresh.wad","MAP01","idTech1"},bytes,&map,&error)
			&& createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Things",{},&group,&error)
			&& assignLevelSceneObjects(&map,group,{"thing:0"},&error),"native fresh fixture",error);
		map.hiddenObjects = {"thing:0"}; map.shownObjects = {QStringLiteral("group:")+group};
		setLevelMapInsertionVisibility(&map,LevelInsertionVisibility::FreshInherited);
		setLevelMapSelection(&map,{{LevelMapSelectionKind::DoomThing,0}}); const auto originalBytes = serializeLevelMap(map).bytes;
		expect(arrayLevelMapSelection(&map,{32,0,0,true},2,{true,false},&error) && map.hiddenObjects.size() == 1 && map.shownObjects.size() == 1
			&& map.insertionVisibility == LevelInsertionVisibility::FreshInherited && undoLevelMapEdit(&map)
			&& serializeLevelMap(map).bytes == originalBytes,"native arrays keep fresh policy through binary and text adoption",dialect+error);
	}
}
void cancellation()
{
	auto map = fixture(); setLevelMapSelection(&map,{brush(0)}); const auto before = map; QString error;
	LevelVisibilityReport report; report.hidden = 37;
	expect(!changeLevelMapVisibility(&map,{static_cast<LevelVisibilityOperation>(99),true},&report,&error) && report.hidden == 37 && map.historyRevision == before.historyRevision,"invalid requests preserve report and document");
	map.selection.append(brush(999)); expect(!changeLevelMapVisibility(&map,{},&report,&error) && map.hiddenObjects.isEmpty(),"invalid selection atomic"); map = before;
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::Visibility; request.visibility = {LevelVisibilityOperation::Isolate,true};
	for (auto phase : {LevelPlacementPhase::Visibility,LevelPlacementPhase::Finalizing,LevelPlacementPhase::Complete}) {
		bool cancel = false; LevelPlacementControl control; control.isCancelled = [&] { return cancel; };
		control.progress = [&](const auto& progress) { cancel |= progress.phase == phase; };
		const auto result = prepareLevelPlacement(map,request,control);
		expect(result.cancelled && !result.succeeded && !result.visibilityReport.changed && result.document.hiddenObjects.isEmpty()
			&& map.hiddenObjects == before.hiddenObjects && map.selection == before.selection && map.historyRevision == before.historyRevision,"cancellation discards candidate and report at every boundary");
	}
	map = fixture(16000); setLevelMapSelection(&map,{brush(0)}); QElapsedTimer timer; timer.start();
	const auto result = prepareLevelPlacement(map,request);
	expect(result.succeeded && result.document.hiddenObjects.size() == 15999 && result.document.undoStack.size() == 1,"dense isolation",result.error);
	std::cout << "16,000 brush isolate: " << timer.elapsed() << " ms" << std::endl;
}
}
int main(int argc, char** argv) { QCoreApplication app(argc,argv); history(); hierarchy(); structural(); inheritedHierarchy(); layerTransfersAndCopies(); insertionPolicies(); cancellation(); return ok ? 0 : 1; }
