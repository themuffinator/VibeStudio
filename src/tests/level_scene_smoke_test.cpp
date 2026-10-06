#include "core/level_document.h"
#include "core/level_scene.h"
#include "tests/doom_preview_test_helpers.h"
#include "tests/level_prefab_test_helpers.h"
#include "tests/level_geometry_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool pass, const char* label, const QString& error = {}) {
	if (!pass) {
		std::cerr << label << ": " << error.toStdString() << '\n';
	}
	return pass;
}
bool reload(const LevelMapDocument& document, LevelMapDocument* output, QString* error) {
	const auto serialized = serializeLevelMap(document);
	if (!serialized.succeeded()) {
		*error = serialized.errors.join(';');
		return false;
	}
	return loadLevelMapBytes(
		{document.format == LevelMapFormat::DoomWad ? QStringLiteral("scene.wad") : QStringLiteral("scene.map"), document.mapName, {}},
		serialized.bytes, output, error);
}
} // namespace
int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error, layer, group, child;
	LevelMapDocument document;
	ok &= expect(tests::loadPrefabFixture(&document, &error), "fixture", error);
	const auto original = serializeLevelMap(document).bytes;
	ok &= expect(createLevelSceneNode(&document, LevelSceneNodeKind::Layer, QStringLiteral("Architecture"), {}, &layer, &error) &&
					 createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Doors"), layer, &group, &error) &&
					 createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Hardware"), group, &child, &error),
				 "hierarchy", error);
	ok &= expect(assignLevelSceneObjects(&document, group, {QStringLiteral("brush:0"), QStringLiteral("entity:3")}, &error) &&
					 assignLevelSceneObjects(&document, child, {QStringLiteral("patch:0")}, &error),
				 "membership", error);
	const auto state = document.scene;
	const auto revision = document.revision;
	ok &= expect(!reparentLevelSceneNode(&document, group, child, &error) && document.scene == state && document.revision == revision,
				 "cycle rejected atomically");
	ok &= expect(!createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("doors"), layer, nullptr, &error) &&
					 !assignLevelSceneObjects(&document, layer, {QStringLiteral("brush:999")}, &error) &&
					 !assignLevelSceneObjects(&document, layer, {QStringLiteral("entity:0")}, &error) && document.scene == state,
				 "names and references validated");
	ok &= expect(levelSceneMembers(document, layer).size() == 3 && levelSceneMembers(document, group, false).size() == 2,
				 "recursive membership");
	ok &= expect(setLevelSceneVisible(&document, layer, false, &error) && levelSceneHiddenObjects(document).size() == 3 &&
					 levelSceneSelection(document, group).isEmpty(),
				 "ancestor visibility", error);
	ok &= expect(serializeLevelMap(document).bytes.startsWith(original), "hidden content remains in native map");
	LevelMapDocument loaded;
	ok &= expect(reload(document, &loaded, &error) && loaded.scene == document.scene, "metadata roundtrip", error);
	ok &= expect(undoLevelMapEdit(&document, &error) && document.scene == state && redoLevelMapEdit(&document, &error) &&
					 !document.scene.nodes[0].visible,
				 "scene exact undo redo", error);
	undoLevelMapEdit(&document);
	ok &= expect(duplicateLevelMapObjects(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, 128, 0, 0, &error), "duplicate", error);
	const auto copied = document.selection.last();
	ok &= expect(levelSceneMembership(document.scene, levelMapSelectionRefId(copied)) == group, "copy inherits membership");
	const auto duplicateState = document.scene;
	ok &= expect(undoLevelMapEdit(&document) && document.scene == state && redoLevelMapEdit(&document) && document.scene == duplicateState,
				 "copy membership history");
	// The new world brush emits before source door brushes, renumbering them.
	document.activeSceneNode = child;
	int inserted = -1;
	ok &= expect(addLevelMapBoxBrush(&document, {256, 0, 0, true}, {288, 32, 32, true}, QStringLiteral("stone"), &inserted, &error),
				 "create in active group", error);
	ok &=
		expect(levelSceneMembership(document.scene, QStringLiteral("brush:%1").arg(inserted)) == child && reload(document, &loaded, &error),
			   "insert and ordinal remap", error);
	for (const auto& brush : loaded.brushes) {
		const auto member = levelSceneMembership(loaded.scene, QStringLiteral("brush:%1").arg(brush.id));
		if (brush.entityId == 0) {
			ok &= expect(member == child, "world insertion membership follows emitted order");
		} else if (brush.mins.x < 1 || brush.mins.x > 100) {
			ok &= expect(member == group, "source and copy membership follow emitted order");
		}
	}
	// Clip results inherit their source; undo restores the original node state.
	setLevelMapSelection(&document, {{LevelMapSelectionKind::QuakeBrush, inserted}});
	const auto preClip = document.scene;
	int clipped = 0;
	ok &= expect(clipLevelMapSelection(&document, {272, 0, 0, true}, {272, 32, 0, true}, {272, 0, 32, true}, LevelMapClipKeep::Both,
									   &clipped, &error) &&
					 clipped == 1,
				 "clip", error);
	for (const auto& ref : document.selection) {
		ok &= expect(levelSceneMembership(document.scene, levelMapSelectionRefId(ref)) == child, "clip result membership");
	}
	ok &= expect(undoLevelMapEdit(&document) && document.scene == preClip, "clip undo membership");
	const auto beforeDelete = document.scene;
	ok &= expect(deleteLevelMapObjects(&document, {{LevelMapSelectionKind::QuakeBrush, inserted}}, &error) &&
					 !levelSceneMembers(document, child).contains(QStringLiteral("brush:%1").arg(inserted)) &&
					 undoLevelMapEdit(&document) && document.scene == beforeDelete,
				 "delete restores membership", error);
	// External geometry changes cannot retarget selectors silently. Raw metadata
	// survives an ordinary save and can be deliberately reset/undone.
	auto stale = serializeLevelMap(document).bytes;
	stale.prepend("// external edit\n");
	ok &= expect(loadLevelMapBytes({QStringLiteral("changed.map"), {}, {}}, stale, &loaded, &error) && loaded.scene.nodes.isEmpty() &&
					 !loaded.scene.opaqueMetadata.isEmpty() && !loaded.scene.problem.isEmpty(),
				 "source binding detects external change", error);
	const auto opaque = loaded.scene;
	ok &= expect(serializeLevelMap(loaded).bytes == stale &&
					 !createLevelSceneNode(&loaded, LevelSceneNodeKind::Layer, QStringLiteral("Lost"), {}, nullptr, &error),
				 "opaque metadata preserved and edits refused");
	ok &= expect(resetLevelScene(&loaded, &error) && loaded.scene.opaqueMetadata.isEmpty() && undoLevelMapEdit(&loaded) &&
					 loaded.scene == opaque,
				 "explicit reset is undoable", error);
	const QByteArray malformed = original + "\n// VibeStudioScene: !invalid!\n";
	ok &= expect(loadLevelMapBytes({QStringLiteral("invalid.map"), {}, {}}, malformed, &loaded, &error) &&
					 loaded.scene.opaqueMetadata == QByteArray("!invalid!") && serializeLevelMap(loaded).bytes == malformed,
				 "malformed carrier preserved", error);
	const QByteArray emptyCarrier = original + "\n// VibeStudioScene: \n";
	ok &= expect(loadLevelMapBytes({QStringLiteral("empty-scene.map"), {}, {}}, emptyCarrier, &loaded, &error) &&
					 !loaded.scene.problem.isEmpty() && serializeLevelMap(loaded).bytes == emptyCarrier,
				 "empty unknown carrier preserved", error);
	auto invalid = document.scene;
	invalid.nodes.last().objects << QStringLiteral("brush:0");
	ok &= expect(!validateLevelScene(document, invalid, &error), "conflicting membership refused");
	QJsonObject unknown{
		{QStringLiteral("version"), 99},
		{QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex())},
		{QStringLiteral("nodes"), QJsonArray()}};
	const auto newer = original + "\n// VibeStudioScene: " + QJsonDocument(unknown).toJson(QJsonDocument::Compact).toBase64() + '\n';
	ok &= expect(loadLevelMapBytes({QStringLiteral("future.map"), {}, {}}, newer, &loaded, &error) && !loaded.scene.problem.isEmpty() &&
					 serializeLevelMap(loaded).bytes == newer,
				 "future schema preserved", error);
	// Removal reparents children and preserves geometry. Reject name conflicts.
	ok &= expect(removeLevelSceneNode(&document, group, &error) && levelSceneNode(document.scene, child)->parentId == layer &&
					 levelSceneMembership(document.scene, QStringLiteral("brush:0")) == layer,
				 "remove reparents without deleting geometry", error);
	for (const auto& game : {QStringLiteral("doom"), QStringLiteral("hexen")}) {
		LevelMapCreateRequest request;
		request.game = game;
		LevelMapDocument doom;
		QString geometry;
		ok &= expect(createLevelMap(request, &doom, &error), "Doom fixture", error);
		ok &=
			expect(createLevelSceneNode(&doom, LevelSceneNodeKind::Layer, QStringLiteral("Room"), {}, &geometry, &error) &&
					   assignLevelSceneObjects(
						   &doom, geometry, {QStringLiteral("linedef:0"), QStringLiteral("sector:0"), QStringLiteral("entity:0")}, &error),
				   "Doom scene", error);
		const auto before = doom.scene;
		setLevelMapSelection(&doom, {{LevelMapSelectionKind::DoomLinedef, 0}});
		ok &= expect(splitLevelMapLinedefs(&doom, nullptr, &error), "Doom split", error);
		for (const auto& ref : doom.selection) {
			ok &= expect(levelSceneMembership(doom.scene, levelMapSelectionRefId(ref)) == geometry, "Doom split inherits");
		}
		ok &= expect(reload(doom, &loaded, &error) && loaded.scene == doom.scene, "Doom native metadata roundtrip", error);
		ok &= expect(undoLevelMapEdit(&doom) && doom.scene == before, "Doom split undo");
		// Delete a lower numbered line; the group's later line must track its
		// compacted id and must not transfer to a different surviving record.
		assignLevelSceneObjects(&doom, geometry, {QStringLiteral("linedef:2")});
		const auto beforeTopology = doom.scene;
		ok &= expect(deleteLevelMapObjects(&doom, {{LevelMapSelectionKind::DoomLinedef, 0}}, &error) &&
						 levelSceneMembership(doom.scene, QStringLiteral("linedef:1")) == geometry && undoLevelMapEdit(&doom) &&
						 doom.scene == beforeTopology,
					 "Doom compacted ids and undo", error);
		const auto native = serializeLevelMap(doom).bytes;
		auto lumps = tests::doom::lumps(native);
		lumps << tests::doom::Lump{"RESOURCE", "keep me"} << tests::doom::Lump{"VS_SCENE", "unrelated"};
		ok &= expect(loadLevelMapBytes({QStringLiteral("multi.wad"), doom.mapName, {}}, tests::doom::wad(lumps), &loaded, &error),
					 "archive fixture", error);
		const auto savedLumps = tests::doom::lumps(serializeLevelMap(loaded).bytes);
		ok &= expect(savedLumps.last().bytes == QByteArray("unrelated") && savedLumps[savedLumps.size() - 2].bytes == QByteArray("keep me"),
					 "unrelated WAD records untouched");
	}
	// A production-sized ownership set: visible descendants cannot override a
	// hidden parent, while unrelated owners and explicit memberships stay live.
	LevelMapDocument large;
	ok &= expect(tests::createGeometryFixture(10000, &large, &error), "visibility scale fixture", error);
	for (int id : {7, 99}) {
		LevelMapEntity owner; owner.id = id; owner.className = QStringLiteral("func_detail");
		owner.properties = {{QStringLiteral("classname"), owner.className}}; large.entities.append(owner);
	}
	for (int i = 0; i < large.brushes.size(); ++i) { large.brushes[i].id = i * 3 + 11; large.brushes[i].entityId = i % 2 ? 99 : 7; }
	LevelMapPatch ownedPatch; ownedPatch.id = 301; ownedPatch.entityId = 7;
	LevelMapPatch otherPatch; otherPatch.id = 309; otherPatch.entityId = 99;
	large.patches = {ownedPatch, otherPatch};
	LevelSceneNode parent; parent.id = QStringLiteral("6f9a7bce-370e-4f4e-aa22-1b74ee0c0673"); parent.name = QStringLiteral("Owner");
	parent.kind = LevelSceneNodeKind::Layer; parent.visible = false; parent.objects = {QStringLiteral("entity:7")};
	LevelSceneNode nested; nested.id = QStringLiteral("4cdaf1a5-13e8-47bb-98e7-8fa7d9c1ad35"); nested.name = QStringLiteral("Child");
	nested.kind = LevelSceneNodeKind::Group; nested.parentId = parent.id; nested.objects = {QStringLiteral("patch:309")};
	LevelSceneNode direct; direct.id = QStringLiteral("6658ffae-3b80-41f9-b3f6-7b31172d04c0"); direct.name = QStringLiteral("Direct");
	direct.kind = LevelSceneNodeKind::Layer; direct.visible = false; direct.objects = {QStringLiteral("brush:20")};
	large.scene.nodes = {parent, nested, direct};
	ok &= expect(validateLevelScene(large, large.scene, &error), "scale scene uses valid native references", error);
	QSet<QString> expected{QStringLiteral("entity:7"), QStringLiteral("patch:301"), QStringLiteral("patch:309"), QStringLiteral("brush:20")};
	for (int i = 0; i < 10000; i += 2) { expected.insert(QStringLiteral("brush:%1").arg(i * 3 + 11)); }
	QElapsedTimer timer; timer.start(); const auto hidden = levelSceneHiddenObjects(large);
	const double hiddenMs = timer.nsecsElapsed() / 1e6;
	ok &= expect(hidden == expected, "sparse owned brushes/patches inherit visibility exactly once across nested groups");
	large.scene.nodes[0].visible = true;
	ok &= expect(levelSceneHiddenObjects(large) == QSet<QString>{QStringLiteral("brush:20")}, "showing the owner preserves independent hidden membership");
	large.scene.nodes[2].visible = true; timer.restart();
	for (int sample = 0; sample < 20; ++sample) { ok &= expect(levelSceneHiddenObjects(large).isEmpty(), "all-visible scene has no inherited hidden primitives"); }
	const double visibleMs = timer.nsecsElapsed() / 1e6 / 20;
	large.scene.nodes.clear();
	ok &= expect(levelSceneHiddenObjects(large).isEmpty(), "implicit default layer needs no primitive visibility traversal");
	parent.objects = {QStringLiteral("entity:007"), QStringLiteral("entity:+7"), QStringLiteral("entity:7 ")};
	large.scene.nodes = {parent};
	ok &= expect(levelSceneHiddenObjects(large) == QSet<QString>(parent.objects.cbegin(), parent.objects.cend()),
		"unvalidated noncanonical selectors cannot silently acquire ownership");
	std::cout << QJsonDocument(QJsonObject{{"brushes", 10000}, {"hidden_owners_ms", hiddenMs}, {"visible_scene_ms", visibleMs}})
		.toJson(QJsonDocument::Compact).constData() << '\n';
	std::cout << (ok ? "Level scene smoke passed\n" : "Level scene smoke failed\n");
	return ok ? 0 : 1;
}
