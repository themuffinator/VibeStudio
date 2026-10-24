#include "core/level_scene.h"
#include "core/level_linked_groups.h"
#include "core/level_prefab.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/level_prefab_test_helpers.h"
#include "tests/level_udmf_test_helpers.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QUuid>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; } return value;
}
QString named(const LevelMapDocument& document, const QString& name)
{
	for (const auto& node : document.scene.nodes) { if (node.name == name) { return node.id; } } return {};
}
bool roundtrip(const LevelMapDocument& document, QString* error)
{
	const auto bytes = serializeLevelMap(document).bytes;
	LevelMapDocument loaded;
	return loadLevelMapBytes({QStringLiteral("copied.map"),{},document.engineFamily},bytes,&loaded,error) &&
		validateLevelScene(loaded,loaded.scene,error) && loaded.scene.nodes.size() == document.scene.nodes.size() &&
		serializeLevelMap(loaded).bytes == bytes;
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc,argv); QString error; bool ok = true;
	std::cout << "Nested duplication and clipboard\n" << std::flush;
	LevelMapDocument source;
	if (!tests::createGeometryFixture(3,&source,&error)) { return 1; }
	QString layer,other,outer,inner,empty;
	if (!createLevelSceneNode(&source,LevelSceneNodeKind::Layer,"Source layer",{},&layer,&error) ||
		!createLevelSceneNode(&source,LevelSceneNodeKind::Layer,"Destination",{},&other,&error) ||
		!createLevelSceneNode(&source,LevelSceneNodeKind::Group,"Assembly",layer,&outer,&error) ||
		!createLevelSceneNode(&source,LevelSceneNodeKind::Group,"Detail",outer,&inner,&error) ||
		!createLevelSceneNode(&source,LevelSceneNodeKind::Group,"Empty",inner,&empty,&error) ||
		!assignLevelSceneObjects(&source,outer,{"brush:0"},&error) || !assignLevelSceneObjects(&source,inner,{"brush:1"},&error)) { return 1; }
	source.activeSceneNode = other;
	setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
	const auto before = serializeLevelMap(source).bytes;
	const auto history = source.undoStack.size();
	auto duplicate = source;
	if (!expect(duplicateLevelMapSelection(&duplicate,128,0,0,&error),"duplicate nested assembly",error)) { return 1; }
	const auto copiedOuter = named(duplicate,"Assembly 2");
	ok &= expect(duplicate.scene.nodes.size() == 8 && !copiedOuter.isEmpty() &&
		levelSceneNode(duplicate.scene,copiedOuter)->parentId == layer && duplicate.undoStack.size() == history+1,
		"duplicate preserves three groups at original parent in one undo");
	const auto after = serializeLevelMap(duplicate).bytes;
	ok &= expect(roundtrip(duplicate,&error),"duplicate save/reload",error);
	ok &= expect(undoLevelMapEdit(&duplicate,&error) && serializeLevelMap(duplicate).bytes == before && duplicate.selection == source.selection,
		"duplicate exact undo",error);
	ok &= expect(redoLevelMapEdit(&duplicate,&error) && serializeLevelMap(duplicate).bytes == after,"duplicate exact redo",error);
	auto partial = source;
	setLevelMapSelection(&partial,{{LevelMapSelectionKind::QuakeBrush,0}});
	ok &= expect(duplicateLevelMapSelection(&partial,128,0,0,&error) && partial.scene.nodes.size() == source.scene.nodes.size() &&
		levelSceneMembership(partial.scene,levelMapSelectionRefId(partial.selection.first())) == outer,"partial copy retains source membership",error);
	auto opened = source;
	ok &= expect(openLevelSceneGroup(&opened,outer,&error),"open parent",error);
	setLevelMapSelection(&opened,{{LevelMapSelectionKind::QuakeBrush,1}});
	ok &= expect(duplicateLevelMapSelection(&opened,128,0,0,&error) && opened.scene.nodes.size() == 7 &&
		levelSceneNode(opened.scene,named(opened,"Detail 2"))->parentId == outer,"open parent duplicates only closed child and descendants",error);
	auto array = source;
	ok &= expect(arrayLevelMapSelection(&array,{128,0,0,true},3,{true,false},&error) && array.scene.nodes.size() == 14 &&
		array.undoStack.size() == history+1,"array preserves each hierarchy in one undo",error);
	ok &= expect(undoLevelMapEdit(&array,&error) && serializeLevelMap(array).bytes == before,"array exact undo",error);

	const auto clipboard = levelMapSelectionText(source,&error);
	ok &= expect(clipboard.contains("// VibeStudioScene:"),"clipboard carries native scene metadata",error);
	LevelMapDocument destination;
	if (!tests::createGeometryFixture(1,&destination,&error)) { return 1; }
	QString context;
	createLevelSceneNode(&destination,LevelSceneNodeKind::Group,"Editing",{},&context,&error);
	assignLevelSceneObjects(&destination,context,{"brush:0"},&error);
	openLevelSceneGroup(&destination,context,&error);
	const auto destinationBefore = serializeLevelMap(destination).bytes;
	const auto destinationHistory = destination.undoStack.size();
	if (!expect(pasteLevelMapText(&destination,clipboard,{256,0,0,true},{true,false},&error),"paste into open group",error)) { return 1; }
	ok &= expect(destination.scene.nodes.size() == 4 && levelSceneNode(destination.scene,named(destination,"Assembly"))->parentId == context &&
		destination.undoStack.size() == destinationHistory+1,"paste hierarchy uses current group and one undo");
	ok &= expect(roundtrip(destination,&error),"paste save/reload",error);
	ok &= expect(undoLevelMapEdit(&destination,&error) && serializeLevelMap(destination).bytes == destinationBefore,"paste exact undo",error);
	auto damaged = clipboard; damaged.replace("studio/cache","studio/other");
	ok &= expect(!pasteLevelMapText(&destination,damaged,&error) && serializeLevelMap(destination).bytes == destinationBefore &&
		destination.undoStack.size() == destinationHistory,"stale metadata refuses atomically",error);
	ok &= expect(!pasteLevelMapText(&destination,clipboard+"// trailing data\n",&error) && serializeLevelMap(destination).bytes == destinationBefore,
		"misplaced metadata refuses atomically",error);

	// Entity membership must continue to be inherited by its brushes; temporary
	// assignment to the destination group must not override it.
	LevelMapDocument owned;
	std::cout << "Owned geometry and prefabs\n" << std::flush;
	if (!tests::loadPrefabFixture(&owned,&error)) { return 1; }
	QString assembly;
	createLevelSceneNode(&owned,LevelSceneNodeKind::Group,"Door",{},&assembly,&error);
	assignLevelSceneObjects(&owned,assembly,{"entity:1","patch:0"},&error);
	setLevelMapSelection(&owned,{{LevelMapSelectionKind::Entity,1},{LevelMapSelectionKind::QuakePatch,0}});
	const auto ownedText = levelMapSelectionText(owned,&error);
	LevelMapDocument ownedDestination = owned;
	ownedDestination.activeSceneNode = assembly;
	if (!expect(pasteLevelMapText(&ownedDestination,ownedText,&error),"paste grouped brush entity and patch",error)) { return 1; }
	const auto newGroup = ownedDestination.scene.nodes.last().id;
	const auto newEntity = ownedDestination.selection.first().objectId;
	for (const auto& brush : ownedDestination.brushes) {
		if (brush.entityId == newEntity) {
			ok &= expect(levelSceneMembership(ownedDestination.scene,QStringLiteral("brush:%1").arg(brush.id)).isEmpty(),"owned brush inherits group");
		}
	}
	ok &= expect(!newGroup.isEmpty() && roundtrip(ownedDestination,&error),"owned hierarchy roundtrip",error);
	LevelPrefab prefab;
	LevelPrefabReport report;
	if (!expect(createLevelPrefab(owned,{"Door assembly",{},{}},&prefab,&report,&error),"capture grouped prefab",error)) { return 1; }
	const auto encoded = serializeLevelPrefab(prefab,&error);
	ok &= expect(report.groups == 1 && QJsonDocument::fromJson(encoded).object().value("version").toInt() == 2,"hierarchical prefab uses version two");
	LevelPrefab restored;
	ok &= expect(parseLevelPrefab(encoded,&restored,&error),"parse hierarchical prefab",error);
	const auto prefabBefore = serializeLevelMap(owned).bytes;
	const auto prefabHistory = owned.undoStack.size();
	ok &= expect(insertLevelPrefab(&owned,restored,{{256,128,0,true},{0,0,90,true},true,{}},&report,&error) &&
		report.groups == 1 && owned.scene.nodes.size() == 2 && owned.undoStack.size() == prefabHistory+1,"place rotated hierarchical prefab",error);
	ok &= expect(undoLevelMapEdit(&owned,&error) && serializeLevelMap(owned).bytes == prefabBefore,"prefab exact undo",error);
	auto malformedPrefab = restored;
	malformedPrefab.mapText += QStringLiteral("// after metadata\n");
	ok &= expect(!insertLevelPrefab(&owned,malformedPrefab,{},nullptr,&error) && serializeLevelMap(owned).bytes == prefabBefore,
		"misplaced prefab metadata refused atomically",error);
	malformedPrefab = restored;
	malformedPrefab.mapText.replace("studio/checker","studio/damaged");
	ok &= expect(!inspectLevelPrefab(malformedPrefab,nullptr,nullptr,&error),"stale prefab scene binding refused",error);
	auto opaqueSource = owned;
	opaqueSource.scene.opaqueMetadata = "unknown";
	ok &= expect(!createLevelPrefab(opaqueSource,{"Unknown scene",{},{}},&malformedPrefab,nullptr,&error),
		"capture refuses to discard unrecognised source metadata",error);
	auto protectedSource = source;
	std::cout << "Protected hierarchies\n" << std::flush;
	for (auto& node : protectedSource.scene.nodes) {
		if (node.id == inner) { node.locked = true; node.visible = false; }
	}
	auto protectedCopy = protectedSource;
	ok &= expect(duplicateLevelMapSelection(&protectedCopy,128,0,0,&error),"copy locked descendant without modifying source",error);
	std::cout << "Protected duplicate completed\n" << std::flush;
	int locked = 0, hidden = 0;
	for (const auto& node : protectedCopy.scene.nodes) { locked += node.locked; hidden += !node.visible; }
	ok &= expect(locked == 2 && hidden == 2,"copies retain lock and visibility");
	LevelPrefab protectedPrefab;
	ok &= expect(createLevelPrefab(protectedSource,{"Locked assembly",{},{}},&protectedPrefab,nullptr,&error),"capture protected prefab",error);
	std::cout << "Protected capture completed\n" << std::flush;
	ok &= expect(insertLevelPrefab(&destination,protectedPrefab,{{256,0,0,true},{0,0,45,true},true,{}},nullptr,&error),
		"locked ordinary prefab can be placed and rotated",error);

	// Linked duplicates in one map remain instances. Portable singleton copies
	// detach, while a complete family gets a fresh link identity on every paste.
	LevelMapDocument linked;
	std::cout << "Linked transfer\n" << std::flush;
	if (!tests::createGeometryFixture(1,&linked,&error)) { return 1; }
	QString first,second;
	createLevelSceneNode(&linked,LevelSceneNodeKind::Group,"Linked",{},&first,&error);
	assignLevelSceneObjects(&linked,first,{"brush:0"},&error);
	if (!expect(createLinkedLevelGroup(&linked,first,{128,0,0,true},&second,&error),"create linked fixture",error)) { return 1; }
	const auto family = levelSceneNode(linked.scene,first)->linkId;
	setLevelMapSelection(&linked,levelSceneSelection(linked,first));
	ok &= expect(duplicateLevelMapSelection(&linked,256,0,0,&error) && levelLinkedGroupNodes(linked.scene,first).size() == 3,
		"normal duplicate retains linked family",error);
	const auto linkedEditBefore = serializeLevelMap(linked).bytes;
	const auto linkedEditHistory = linked.undoStack.size();
	ok &= expect(setLevelMapBrushFaceProperty(&linked,0,0,"texture","studio/linked-edit",&error),"edit original linked face",error);
	for (const auto& brush : linked.brushes) {
		ok &= expect(brush.faces.first().textureName == QStringLiteral("studio/linked-edit"),"edit reaches ordinary linked duplicate");
	}
	ok &= expect(linked.scene.nodes.size() == 3 && linked.undoStack.size() == linkedEditHistory+1 &&
		undoLevelMapEdit(&linked,&error) && serializeLevelMap(linked).bytes == linkedEditBefore,
		"linked propagation preserves hierarchy with exact single undo",error);
	const auto singleton = levelMapSelectionText(linked,&error);
	LevelMapDocument imported;
	tests::createGeometryFixture(1,&imported,&error);
	ok &= expect(pasteLevelMapText(&imported,singleton,&error) && imported.scene.nodes.size() == 1 && imported.scene.nodes.first().linkId.isEmpty(),
		"portable singleton is independent",error);
	setLevelMapSelection(&linked,levelMapSelectAllObjects(linked,false));
	const auto familyText = levelMapSelectionText(linked,&error);
	ok &= expect(pasteLevelMapText(&imported,familyText,&error) && imported.scene.nodes.size() == 4,"paste linked family",error);
	QSet<QString> importedLinks;
	for (const auto& node : imported.scene.nodes) { if (!node.linkId.isEmpty()) { importedLinks.insert(node.linkId); } }
	ok &= expect(importedLinks.size() == 1 && !importedLinks.contains(family) && roundtrip(imported,&error),"portable family remapped and saved",error);
	ok &= expect(pasteLevelMapText(&imported,familyText,&error),"paste family again",error);
	for (const auto& node : imported.scene.nodes) { if (!node.linkId.isEmpty()) { importedLinks.insert(node.linkId); } }
	ok &= expect(importedLinks.size() == 2,"separate paste operations do not cross-link");
	const auto importedFamily = imported.scene.nodes[1].linkId;
	const auto importedBrush = imported.scene.nodes[1].objects.first().section(':',1).toInt();
	ok &= expect(setLevelMapBrushFaceProperty(&imported,importedBrush,0,"texture","studio/import-edit",&error),"edit imported linked family",error);
	int changed = 0;
	for (const auto& brush : imported.brushes) {
		if (brush.faces.first().textureName == QStringLiteral("studio/import-edit")) {
			++changed;
			const auto owner = levelSceneMembership(imported.scene,QStringLiteral("brush:%1").arg(brush.id));
			ok &= expect(levelSceneNode(imported.scene,owner)->linkId == importedFamily,"portable link propagation stays in its family");
		}
	}
	ok &= expect(changed == 3,"all and only imported family members update");
	LevelPrefab linkedPrefab;
	std::cout << "Linked prefab capture\n" << std::flush;
	ok &= expect(createLevelPrefab(linked,{"Linked assembly",{},{}},&linkedPrefab,nullptr,&error),"capture linked-family prefab",error);
	{
		// Linked frames take any rotation of the prefab, not only quarter turns.
		auto slanted = imported;
		ok &= expect(insertLevelPrefab(&slanted,linkedPrefab,{{0,0,0,true},{0,0,45,true},false,{}},nullptr,&error),
			"linked-family prefab placed at 45 degrees",error);
		for (qsizetype i = slanted.scene.nodes.size()-3; i < slanted.scene.nodes.size(); ++i) {
			ok &= expect(levelLinkFrameText(slanted.scene.nodes[i].linkFrame) == QStringLiteral("Turned 45 degrees"),
				"imported linked frames turn 45 degrees with the assembly",levelLinkFrameText(slanted.scene.nodes[i].linkFrame));
		}
	}
	ok &= expect(insertLevelPrefab(&imported,linkedPrefab,{{0,256,0,true},{0,0,90,true},true,{}},&report,&error) &&
		report.linkedGroups == 3,"linked-family prefab quarter-turn placement",error);
	for (qsizetype i = imported.scene.nodes.size()-3; i < imported.scene.nodes.size(); ++i) {
		ok &= expect(levelLinkFrameText(imported.scene.nodes[i].linkFrame) == QStringLiteral("Turned 90 degrees"),
			"imported linked frames rotate with assembly");
	}

	for (const auto& game : {QStringLiteral("doom"),QStringLiteral("hexen")}) {
		std::cout << "Binary Doom " << game.toStdString() << '\n' << std::flush;
		LevelMapDocument binary;
		if (!loadLevelMapBytes({"binary.wad","MAP01","idTech1"},tests::doom::fixture(game),&binary,&error)) { return 1; }
		QString group;
		createLevelSceneNode(&binary,LevelSceneNodeKind::Group,"Things",{},&group,&error);
		assignLevelSceneObjects(&binary,group,{"thing:0"},&error);
		setLevelMapSelection(&binary,{{LevelMapSelectionKind::DoomThing,0}});
		const auto original = serializeLevelMap(binary).bytes;
		ok &= expect(duplicateLevelMapSelection(&binary,32,0,0,&error) && binary.scene.nodes.size() == 2,
			"binary thing duplicate preserves group",game+error);
		ok &= expect(undoLevelMapEdit(&binary,&error) && serializeLevelMap(binary).bytes == original,"binary group duplicate exact undo",game+error);
	}

	LevelMapDocument udmf;
	std::cout << "UDMF and scene limits\n" << std::flush;
	if (!loadLevelMapBytes({"group.wad","MAP01","idTech1"},tests::udmf::fixture(),&udmf,&error)) { return 1; }
	QString things;
	createLevelSceneNode(&udmf,LevelSceneNodeKind::Group,"Things",{},&things,&error);
	assignLevelSceneObjects(&udmf,things,{"thing:0"},&error);
	setLevelMapSelection(&udmf,{{LevelMapSelectionKind::DoomThing,0}});
	const auto udmfBefore = serializeLevelMap(udmf).bytes;
	ok &= expect(arrayLevelMapSelection(&udmf,{32.5,0,0,true},3,{true,false},&error) && udmf.scene.nodes.size() == 4,
		"UDMF thing array preserves separate groups",error);
	ok &= expect(undoLevelMapEdit(&udmf,&error) && serializeLevelMap(udmf).bytes == udmfBefore,"UDMF group array exact undo",error);

	auto full = source;
	while (full.scene.nodes.size() < kLevelSceneMaxNodes) {
		LevelSceneNode node; node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		node.name = QStringLiteral("Unused %1").arg(full.scene.nodes.size()); full.scene.nodes << node;
	}
	const auto fullBefore = serializeLevelMap(full).bytes;
	ok &= expect(!duplicateLevelMapSelection(&full,128,0,0,&error) && serializeLevelMap(full).bytes == fullBefore && full.undoStack.size() == history,
		"node limit refuses duplicate atomically",error);
	ok &= expect(!pasteLevelMapText(&full,clipboard,&error) && serializeLevelMap(full).bytes == fullBefore,"node limit refuses paste atomically",error);
	return ok ? 0 : 1;
}
