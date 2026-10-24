#include "core/level_linked_extraction.h"
#include "core/level_linked_groups.h"
#include "core/level_linked_groups_p.h"
#include "core/level_linked_objects.h"
#include "core/level_placement.h"
#include "core/level_protected_properties.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_prefab_test_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n';
	}
	return value;
}
QString object(const LevelMapDocument& document, const QString& group, const QString& identity)
{
	const auto* node = levelSceneNode(document.scene, group);
	if (node) {
		for (auto it = node->linkObjects.cbegin(); it != node->linkObjects.cend(); ++it) {
			if (it.value() == identity) {
				return it.key();
			}
		}
	}
	return {};
}
LevelMapSelectionRef ref(const QString& object)
{
	return {levelMapSelectionKindFromId(object.section(':', 0, 0)), object.section(':', 1).toInt()};
}
QJsonArray vector(const LevelMapVec3& p)
{
	return {p.x, p.y, p.z};
}
QJsonObject geometry(const LevelMapDocument& document, const QString& object)
{
	for (const auto& brush : document.brushes) {
		if (object != levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush, brush.id})) {
			continue;
		}
		QJsonArray faces;
		for (const auto& f : brush.faces) {
			QJsonArray matrix;
			for (double n : f.textureMatrix) {
				matrix.append(n);
			}
			faces.append(QJsonObject{{"points", QJsonArray{vector(f.p0), vector(f.p1), vector(f.p2)}},
			                         {"texture", f.textureName},
			                         {"mapping", QJsonArray{f.shiftX, f.shiftY, f.rotation, f.scaleX, f.scaleY}},
			                         {"axes", QJsonArray{f.explicitTextureAxes, vector(f.uAxis), f.uOffset, vector(f.vAxis), f.vOffset}},
			                         {"plane", QJsonArray{f.explicitPlane, vector(f.planeNormal), f.planeDistance}},
			                         {"matrix", matrix},
			                         {"flags", QJsonArray{f.contentFlags, f.surfaceFlags, f.surfaceValue}}});
		}
		return {{"kind", brush.primitiveKind}, {"faces", faces}};
	}
	for (const auto& patch : document.patches) {
		if (object != levelMapSelectionRefId({LevelMapSelectionKind::QuakePatch, patch.id})) {
			continue;
		}
		QJsonArray points, u, v, tail;
		for (const auto& point : patch.controlPoints) {
			points.append(vector(point));
		}
		for (double n : patch.controlU) {
			u.append(n);
		}
		for (double n : patch.controlV) {
			v.append(n);
		}
		for (double n : patch.headerTail) {
			tail.append(n);
		}
		return {{"texture", patch.textureName},
		        {"points", points},
		        {"u", u},
		        {"v", v},
		        {"tail", tail},
		        {"topology", QJsonArray{patch.width, patch.height, patch.fixedSubdivisions, patch.subdivisionsX, patch.subdivisionsY}}};
	}
	return {};
}
QString property(const LevelMapDocument& document, const QString& selector, const QString& key)
{
	for (const auto& entity : document.entities) {
		if (entity.id == ref(selector).objectId) {
			return tests::prefabProperty(entity, key);
		}
	}
	return {};
}
// The content of a group and of the groups nested in it.
QSet<QString> content(const LevelMapDocument& document, const QString& group)
{
	return levelLinkedObjects(document, levelLinkedGroupMembers(document.scene, group));
}
QSet<QString> known(const LevelMapDocument& document, const QString& group)
{
	const auto* node = levelSceneNode(document.scene, group);
	return node ? QSet<QString>(node->linkObjects.keyBegin(), node->linkObjects.keyEnd()) : QSet<QString>{};
}
QSet<QString> identities(const LevelMapDocument& document, const QString& group)
{
	const auto* node = levelSceneNode(document.scene, group);
	return node ? QSet<QString>(node->linkObjects.cbegin(), node->linkObjects.cend()) : QSet<QString>{};
}
// The group reached from `group` through nested groups of each link in turn.
QString nestedGroup(const LevelMapDocument& document, QString group, const QStringList& links)
{
	for (const auto& link : links) {
		const auto at = group;
		group.clear();
		for (const auto& node : document.scene.nodes) {
			if (node.parentId == at && node.linkId == link) {
				group = node.id;
			}
		}
	}
	return group;
}
// Every linked group knows its whole content, matches its baseline and is in
// sync: the next edit sees only its own change.
bool settled(const LevelMapDocument& document)
{
	return std::all_of(document.scene.nodes.cbegin(), document.scene.nodes.cend(), [&](const auto& node) {
		if (node.linkId.isEmpty()) {
			return true;
		}
		const auto now = levelLinkedGroupContent(document, node.id);
		return !node.linkOutOfSync && known(document, node.id) == content(document, node.id) && node.linkShape == now.shape &&
		       node.linkSurface == now.surface;
	});
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	LevelMapDocument initial;
	QString error, parent, group, second, third;
	if (!tests::loadPrefabFixture(&initial, &error) ||
	    !createLevelSceneNode(&initial, LevelSceneNodeKind::Group, "Outer", {}, &parent, &error) ||
	    !createLevelSceneNode(&initial, LevelSceneNodeKind::Group, "Assembly", parent, &group, &error) ||
	    !assignLevelSceneObjects(&initial, group, {"entity:1", "entity:2", "patch:0"}, &error) ||
	    !createLinkedLevelGroup(&initial, group, {256, 0, 0, true}, &second, &error) ||
	    !createLinkedLevelGroup(&initial, group, {512, 0, 0, true}, &third, &error)) {
		return expect(false, "fixture", error) ? 0 : 1;
	}
	const auto ids = levelSceneNode(initial.scene, group)->linkObjects;
	const auto door = ids.value("entity:1"), relay = ids.value("entity:2"), firstBrush = ids.value("brush:0"),
	           otherBrush = ids.value("brush:1"), patch = ids.value("patch:0");
	const QStringList groups{group, second, third};
	bool ok = true;
	setLevelMapSelection(&initial, levelLinkedContent(initial, levelSceneNode(initial.scene, second)->objects).roots);
	ok &= expect(rotateLevelMapSelection(&initial, 2, 1, &error), "rotated counterpart", error);
	setLevelMapSelection(&initial, levelLinkedContent(initial, levelSceneNode(initial.scene, third)->objects).roots);
	ok &= expect(flipLevelMapSelection(&initial, 0, &error), "mirrored counterpart", error);
	for (int i = 0; i < groups.size(); ++i) {
		ok &= expect(setLevelProtectedEntityProperty(&initial, {ref(object(initial, groups[i], door)).objectId}, "speed",
		                                             {QString::number(111 * (i + 1))}, &error) &&
		                 setLevelProtectedEntityProperty(&initial, {ref(object(initial, groups[i], relay)).objectId}, "targetname",
		                                                 {QStringLiteral("relay_%1").arg(i)}, &error),
		             "instance-local properties", error);
	}
	if (!ok) {
		return 1;
	}
	for (int variant = 0; variant < 4; ++variant) {
		auto document = initial;
		document.undoLimit = 1;
		openLevelSceneGroup(&document, group);
		QStringList picked;
		if (variant == 0) {
			picked = {firstBrush, relay};
		} else if (variant == 1) {
			picked = {door};
		} else if (variant == 2) {
			picked = {patch};
		} else {
			picked = {firstBrush, otherBrush};
		}
		QVector<LevelMapSelectionRef> selection;
		for (const auto& identity : picked) {
			selection << ref(object(document, group, identity));
		}
		setLevelMapSelection(&document, selection);
		const auto before = serializeLevelMap(document).bytes;
		const auto context = document.openSceneGroups;
		const auto creation = document.sceneCreationBeforeOpen;
		LevelLinkedExtractionReport report;
		ok &= expect(canExtractLevelLinkedGroups(document, &error) && extractLevelLinkedGroups(&document, &report, &error),
		             "extract selected contents", error);
		if (!ok) {
			return 1;
		}
		ok &= expect(report.createdGroups.size() == 3 && report.sourceGroup == group && !report.extractedGroup.isEmpty() &&
		                 report.splitEntities == (variant == 0 ? 3 : 0),
		             "report counts both families and partial owners");
		ok &= expect(document.scene.nodes.size() == initial.scene.nodes.size() + 3 && document.brushes.size() == initial.brushes.size() &&
		                 document.patches.size() == initial.patches.size() &&
		                 document.entities.size() == initial.entities.size() + (variant == 0 ? 3 : 0),
		             "partition preserves all geometry and only duplicates partial owners");
		const auto newLink = levelSceneNode(document.scene, report.extractedGroup)->linkId;
		ok &= expect(!newLink.isEmpty() && newLink != levelSceneNode(document.scene, group)->linkId &&
		                 levelLinkedGroupNodes(document.scene, report.extractedGroup).size() == 3 &&
		                 validateLevelScene(document, document.scene, &error),
		             "two valid independent linked families", error);
		for (const auto& oldGroup : groups) {
			QString newGroup;
			const auto* old = levelSceneNode(initial.scene, oldGroup);
			for (const auto& id : report.createdGroups) {
				const auto* node = levelSceneNode(document.scene, id);
				if (node->linkFrame == old->linkFrame) {
					newGroup = id;
				}
			}
			ok &= expect(!newGroup.isEmpty(), "extracted instance keeps its spatial frame");
			for (const auto& identity : {firstBrush, otherBrush, patch}) {
				const auto oldObject = object(initial, oldGroup, identity);
				auto kept = object(document, oldGroup, identity);
				if (kept.isEmpty()) {
					kept = object(document, newGroup, identity);
				}
				ok &= expect(!kept.isEmpty() && geometry(initial, oldObject) == geometry(document, kept),
				             "world coordinates and UVs stay exact");
			}
			for (const auto& destination : {oldGroup, newGroup}) {
				const auto owner = object(document, destination, door);
				if (!owner.isEmpty()) {
					ok &= expect(property(document, owner, "speed") == property(initial, object(initial, oldGroup, door), "speed") &&
					                 levelEntityPropertyProtected(document, ref(owner).objectId, "speed"),
					             "split owners keep instance-specific values and protection");
				}
			}
			auto relayObject = object(document, oldGroup, relay);
			if (relayObject.isEmpty()) {
				relayObject = object(document, newGroup, relay);
			}
			ok &=
			    expect(property(document, relayObject, "targetname") == property(initial, object(initial, oldGroup, relay), "targetname") &&
			               property(document, relayObject, "origin") == property(initial, object(initial, oldGroup, relay), "origin") &&
			               levelEntityPropertyProtected(document, ref(relayObject).objectId, "targetname"),
			           "point entities keep local properties and exact origins");
		}
		const auto after = serializeLevelMap(document).bytes;
		const auto selectedAfter = document.selection;
		ok &= expect(document.openSceneGroups == QStringList{parent} && document.activeSceneNode == parent && document.undoLimit == 1 &&
		                 document.undoStack.size() == 1 && !selectedAfter.isEmpty(),
		             "successful extraction closes the child and selects its new group");
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == before &&
		                 document.selection == selection && document.openSceneGroups == context &&
		                 document.sceneCreationBeforeOpen == creation && document.activeSceneNode == group,
		             "one undo restores exact map, selection and editing context", error);
		ok &= expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == after &&
		                 document.selection == selectedAfter && document.openSceneGroups == QStringList{parent},
		             "redo restores both families and context", error);
		LevelMapDocument reopened;
		ok &= expect(loadLevelMapBytes({"extracted.map", {}, "idTech3"}, after, &reopened, &error) &&
		                 serializeLevelMap(reopened).bytes == after && validateLevelScene(reopened, reopened.scene, &error),
		             "save/reopen retains extraction and protection", error);
		if (variant == 0) {
			// Future changes propagate within each new family only.
			closeLevelSceneGroup(&document, true);
			const int source = ref(object(document, report.extractedGroup, door)).objectId;
			ok &= expect(setLevelMapEntityProperty(&document, source, "health", "27", &error), "edit extracted family", error);
			for (const auto& id : report.createdGroups) {
				ok &= expect(property(document, object(document, id, door), "health") == "27", "extracted counterparts synchronise");
			}
			for (const auto& id : groups) {
				ok &= expect(property(document, object(document, id, door), "health").isEmpty(), "original family remains independent");
			}
			ok &= expect(setLevelMapEntityProperty(&document, ref(object(document, group, door)).objectId, "health", "59", &error),
			             "edit original family after extraction", error);
			for (const auto& id : groups) {
				ok &= expect(property(document, object(document, id, door), "health") == "59", "original counterparts still synchronise");
			}
			for (const auto& id : report.createdGroups) {
				ok &= expect(property(document, object(document, id, door), "health") == "27", "extracted family remains independent");
			}
			QVector<LevelMapSelectionRef> exported;
			openLevelSceneGroup(&document, parent);
			for (const auto& id : report.createdGroups) {
				exported += levelLinkedContent(document, levelSceneNode(document.scene, id)->objects).roots;
			}
			setLevelMapSelection(&document, exported);
			LevelPrefab prefab;
			LevelPrefabCreateRequest create;
			create.name = "Extracted assembly";
			LevelMapDocument destination;
			LevelPrefabReport inserted;
			ok &= expect(tests::loadPrefabFixture(&destination, &error) && createLevelPrefab(document, create, &prefab, nullptr, &error) &&
			                 insertLevelPrefab(&destination, prefab, {}, &inserted, &error) && inserted.linkedGroups == 3 &&
			                 validateLevelScene(destination, destination.scene, &error),
			             "extracted family transfers through the prefab pipeline", error);
			QSet<QString> importedFamilies;
			int protectedOwners = 0;
			for (const auto& node : destination.scene.nodes) {
				if (!node.linkId.isEmpty()) {
					importedFamilies.insert(node.linkId);
				}
				for (const auto& selector : node.objects) {
					protectedOwners +=
					    selector.startsWith("entity:") && levelEntityPropertyProtected(destination, ref(selector).objectId, "speed");
				}
			}
			ok &= expect(importedFamilies.size() == 1 && !importedFamilies.contains(newLink) && protectedOwners == 3,
			             "portable extraction gets a fresh family and retains all local owner protection");
		}
	}
	{
		// A patch and brushes can share an owner. Splitting its patch must keep
		// both owner halves, including the instance's protected local value.
		LevelMapDocument mixed;
		QString sourceGroup, peerGroup;
		int owner = -1;
		ok &= expect(tests::loadPrefabFixture(&mixed, &error) &&
		                 setLevelMapSelection(&mixed, {{LevelMapSelectionKind::Entity, 1}, {LevelMapSelectionKind::QuakePatch, 0}}) &&
		                 tieLevelMapSelectionToEntity(&mixed, "func_door", {}, &owner, &error) &&
		                 createLevelSceneNode(&mixed, LevelSceneNodeKind::Group, "Mixed owner", {}, &sourceGroup, &error) &&
		                 assignLevelSceneObjects(&mixed, sourceGroup, {QStringLiteral("entity:%1").arg(owner), "entity:2"}, &error) &&
		                 createLinkedLevelGroup(&mixed, sourceGroup, {256, 0, 0, true}, &peerGroup, &error),
		             "mixed brush/patch owner fixture", error);
		if (!ok) {
			return 1;
		}
		QString patchIdentity;
		const auto identities = levelSceneNode(mixed.scene, sourceGroup)->linkObjects;
		for (auto it = identities.cbegin(); it != identities.cend(); ++it) {
			if (it.key().startsWith("patch:")) {
				patchIdentity = it.value();
			}
		}
		const auto ownerIdentity = levelSceneNode(mixed.scene, sourceGroup)->linkObjects.value(QStringLiteral("entity:%1").arg(owner));
		ok &= expect(
		    setLevelProtectedEntityProperty(&mixed, {owner}, "speed", {"321"}, &error) &&
		        setLevelProtectedEntityProperty(&mixed, {ref(object(mixed, peerGroup, ownerIdentity)).objectId}, "speed", {"654"}, &error),
		    "mixed-owner local properties", error);
		const auto original = mixed;
		openLevelSceneGroup(&mixed, sourceGroup);
		setLevelMapSelection(&mixed, {ref(object(mixed, sourceGroup, patchIdentity))});
		LevelLinkedExtractionReport report;
		ok &= expect(extractLevelLinkedGroups(&mixed, &report, &error) && report.splitEntities == 2 &&
		                 mixed.patches.size() == original.patches.size() && mixed.brushes.size() == original.brushes.size() &&
		                 validateLevelScene(mixed, mixed.scene, &error),
		             "extract owned patches into two partial owners", error);
		QSet<QString> values;
		for (const auto& id : report.createdGroups) {
			const auto selector = object(mixed, id, ownerIdentity);
			values.insert(property(mixed, selector, "speed"));
			ok &= expect(levelEntityPropertyProtected(mixed, ref(selector).objectId, "speed"), "extracted patch owner retains protection");
			const auto value = property(mixed, selector, "speed");
			const auto oldGroup = value == "321" ? sourceGroup : peerGroup;
			ok &= expect(geometry(original, object(original, oldGroup, patchIdentity)) == geometry(mixed, object(mixed, id, patchIdentity)),
			             "owned patch control mesh and UVs remain exact");
		}
		ok &= expect(values == QSet<QString>{"321", "654"}, "both partial patch owners retain distinct local values");
	}
	{
		auto isolated = initial;
		QString unrelated, unrelatedCopy;
		ok &= expect(createLevelSceneNode(&isolated, LevelSceneNodeKind::Group, "Unrelated", {}, &unrelated, &error) &&
		                 assignLevelSceneObjects(&isolated, unrelated, {"entity:3"}, &error) &&
		                 createLinkedLevelGroup(&isolated, unrelated, {1024, 0, 0, true}, &unrelatedCopy, &error),
		             "independent family fixture", error);
		// A caller can defer linked propagation while staging a compound edit.
		// Extraction must not accept another family's pending state as baseline.
		const detail::LevelLinkedUpdateScope quiet;
		ok &= expect(setLevelMapEntityProperty(&isolated, 3, "health", "pending", &error), "unrelated staged edit", error);
		const auto unrelatedBefore = *levelSceneNode(isolated.scene, unrelated);
		const auto copyBefore = *levelSceneNode(isolated.scene, unrelatedCopy);
		openLevelSceneGroup(&isolated, group);
		setLevelMapSelection(&isolated, {ref(object(isolated, group, firstBrush))});
		ok &= expect(
		    extractLevelLinkedGroups(&isolated, nullptr, &error) && *levelSceneNode(isolated.scene, unrelated) == unrelatedBefore &&
		        *levelSceneNode(isolated.scene, unrelatedCopy) == copyBefore && property(isolated, "entity:3", "health") == "pending",
		    "extraction preserves unrelated families and their pending baselines", error);
	}
	const auto refused = [&](LevelMapDocument document, const char* reason) {
		const auto bytes = serializeLevelMap(document).bytes;
		const auto revision = document.revision;
		const auto depth = document.undoStack.size();
		const auto selection = document.selection;
		const auto context = document.openSceneGroups;
		LevelLinkedExtractionReport report;
		report.extractedGroup = "unchanged";
		return expect(!extractLevelLinkedGroups(&document, &report, &error) && !error.isEmpty() &&
		                  serializeLevelMap(document).bytes == bytes && document.revision == revision &&
		                  document.undoStack.size() == depth && document.selection == selection && document.openSceneGroups == context &&
		                  report.extractedGroup == "unchanged",
		              reason, error);
	};
	ok &= refused(initial, "closed group refuses extraction atomically");
	auto working = initial;
	openLevelSceneGroup(&working, group);
	setLevelMapSelection(&working, levelLinkedContent(working, levelSceneNode(working.scene, group)->objects).roots);
	ok &= refused(working, "whole group extraction is refused");
	setLevelMapSelection(&working, {ref(object(working, group, firstBrush))});
	auto locked = working;
	setLevelSceneLocked(&locked, second, true);
	ok &= refused(locked, "locked counterpart refuses the whole transaction");
	auto legacy = working;
	legacy.scene.protectedProperties.clear();
	for (auto& node : legacy.scene.nodes) {
		node.linkObjects.clear();
	}
	ok &= refused(legacy, "legacy correspondence is not guessed");
	auto missing = working;
	for (auto& node : missing.scene.nodes) {
		if (node.id == second) {
			node.linkObjects[object(working, second, firstBrush)] = QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
	}
	ok &= expect(validateLevelScene(missing, missing.scene, &error), "divergent but valid linked correspondence", error);
	ok &= refused(missing, "a missing selected counterpart refuses atomically");
	setLevelMapSelection(&missing, {ref(object(missing, group, door)), ref(object(missing, group, firstBrush))});
	ok &= expect(extractLevelLinkedGroups(&missing, nullptr, &error), "selected owners subsume redundant child selections", error);
	auto full = working;
	while (full.scene.nodes.size() < kLevelSceneMaxNodes - 2) {
		LevelSceneNode node;
		node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		node.name = QStringLiteral("Reserved %1").arg(full.scene.nodes.size());
		node.kind = LevelSceneNodeKind::Group;
		full.scene.nodes << node;
	}
	ok &= refused(full, "node capacity refuses the complete split before mutation");
	auto outside = working;
	LevelSceneNode elsewhere;
	elsewhere.kind = LevelSceneNodeKind::Group;
	elsewhere.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	elsewhere.name = "Outside ownership";
	elsewhere.objects = {object(outside, second, otherBrush)};
	outside.scene.nodes << elsewhere;
	ok &= expect(validateLevelScene(outside, outside.scene, &error), "explicit geometry membership outside its owner is representable",
	             error);
	ok &= refused(outside, "disjoint brush ownership refuses atomically");
	outside.scene.nodes.last().locked = true;
	ok &= refused(outside, "a locked owned primitive refuses the whole transaction");
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::ExtractLinkedGroups;
	bool cancel = false;
	LevelPlacementControl control;
	control.isCancelled = [&] { return cancel; };
	control.progress = [&](const auto& progress) {
		if (progress.phase == LevelPlacementPhase::Extracting && progress.completed == 1) {
			cancel = true;
		}
	};
	const auto original = serializeLevelMap(working).bytes;
	const auto cancelled = prepareLevelPlacement(working, request, control);
	ok &= expect(cancelled.cancelled && !cancelled.succeeded && serializeLevelMap(working).bytes == original,
	             "cancellation after one instance publishes nothing");
	cancel = false;
	control.progress = [&](const auto& progress) { cancel |= progress.phase == LevelPlacementPhase::Finalizing; };
	const auto lateCancelled = prepareLevelPlacement(working, request, control);
	ok &= expect(lateCancelled.cancelled && !lateCancelled.succeeded && serializeLevelMap(working).bytes == original,
	             "finalization cancellation discards both complete families");
	// The extracted group corresponding to `original`: beside it, in its frame.
	const auto extractedFor = [](const LevelMapDocument& document, const QStringList& created, const QString& original) {
		const auto* node = levelSceneNode(document.scene, original);
		for (const auto& id : created) {
			const auto* each = levelSceneNode(document.scene, id);
			if (node && each && each->parentId == node->parentId && each->linkFrame == node->linkFrame) {
				return id;
			}
		}
		return QString();
	};
	// Groups nested in linked groups: Assembly holds a door, Inner (a relay, a
	// speaker and Core, which holds the panel and a model) and Trim (a brush),
	// in three linked groups, turned and mirrored.
	LevelMapDocument nestedBase;
	QString outer, innerLink, coreLink, trimLink;
	QStringList assemblies;
	{
		int trimBrush = -1;
		QString assembly, inner, core, trim, copy2, copy3;
		ok &= expect(tests::loadPrefabFixture(&nestedBase, &error) &&
		                 addLevelMapBoxBrush(&nestedBase, {0, 96, 0, true}, {32, 128, 32, true}, "studio/checker", &trimBrush, &error) &&
		                 createLevelSceneNode(&nestedBase, LevelSceneNodeKind::Group, "Outer", {}, &outer, &error) &&
		                 createLevelSceneNode(&nestedBase, LevelSceneNodeKind::Group, "Assembly", outer, &assembly, &error) &&
		                 createLevelSceneNode(&nestedBase, LevelSceneNodeKind::Group, "Inner", assembly, &inner, &error) &&
		                 createLevelSceneNode(&nestedBase, LevelSceneNodeKind::Group, "Core", inner, &core, &error) &&
		                 createLevelSceneNode(&nestedBase, LevelSceneNodeKind::Group, "Trim", assembly, &trim, &error) &&
		                 assignLevelSceneObjects(&nestedBase, assembly, {"entity:1"}, &error) &&
		                 assignLevelSceneObjects(&nestedBase, inner, {"entity:2", "entity:4"}, &error) &&
		                 assignLevelSceneObjects(&nestedBase, core, {"patch:0", "entity:3"}, &error) &&
		                 assignLevelSceneObjects(&nestedBase, trim, {QStringLiteral("brush:%1").arg(trimBrush)}, &error) &&
		                 createLinkedLevelGroup(&nestedBase, assembly, {256, 0, 0, true}, &copy2, &error) &&
		                 createLinkedLevelGroup(&nestedBase, assembly, {512, 0, 0, true}, &copy3, &error),
		             "nested fixture", error);
		if (!ok) {
			return 1;
		}
		assemblies = {assembly, copy2, copy3};
		innerLink = levelSceneNode(nestedBase.scene, inner)->linkId;
		coreLink = levelSceneNode(nestedBase.scene, core)->linkId;
		trimLink = levelSceneNode(nestedBase.scene, trim)->linkId;
	}
	const auto nestedIds = levelSceneNode(nestedBase.scene, assemblies[0])->linkObjects;
	const auto nestedDoor = nestedIds.value("entity:1"), nestedRelay = nestedIds.value("entity:2"), speaker = nestedIds.value("entity:4"),
	           panel = nestedIds.value("patch:0");
	const auto turn = [&](const QString& group, bool mirror) {
		setLevelMapSelection(&nestedBase, levelLinkedGroupContent(nestedBase, group).roots);
		return mirror ? flipLevelMapSelection(&nestedBase, 0, &error) : rotateLevelMapSelection(&nestedBase, 2, 1, &error);
	};
	ok &= expect(turn(assemblies[1], false) && turn(assemblies[2], true), "turned and mirrored nested counterparts", error);
	for (int i = 0; i < assemblies.size(); ++i) {
		ok &= expect(setLevelProtectedEntityProperty(&nestedBase, {ref(object(nestedBase, assemblies[i], nestedDoor)).objectId}, "speed",
		                                             {QString::number(111 * (i + 1))}, &error) &&
		                 setLevelProtectedEntityProperty(&nestedBase, {ref(object(nestedBase, assemblies[i], nestedRelay)).objectId},
		                                                 "targetname", {QStringLiteral("relay_%1").arg(i)}, &error),
		             "nested instance-local properties", error);
	}
	ok &= expect(renameLevelSceneNode(&nestedBase, nestedGroup(nestedBase, assemblies[1], {innerLink, coreLink}), "Core B", &error) &&
	                 setLevelSceneVisible(&nestedBase, nestedGroup(nestedBase, assemblies[2], {innerLink, coreLink}), false, &error) &&
	                 setLevelMapSelection(&nestedBase, {}) && settled(nestedBase),
	             "nested counterparts keep their own names and visibility", error);
	if (!ok) {
		return 1;
	}
	{
		// Inner moves whole, with Core in it, out of each assembly.
		auto document = nestedBase;
		document.undoLimit = 1;
		QStringList inners, cores, trims;
		for (const auto& assembly : std::as_const(assemblies)) {
			inners << nestedGroup(document, assembly, {innerLink});
			cores << nestedGroup(document, assembly, {innerLink, coreLink});
			trims << nestedGroup(document, assembly, {trimLink});
		}
		openLevelSceneGroup(&document, assemblies[0]);
		setLevelMapSelection(&document, levelSceneSelection(document, inners[0]));
		const auto before = document;
		const auto bytes = serializeLevelMap(document).bytes;
		const auto selection = document.selection;
		LevelLinkedExtractionReport report;
		ok &= expect(canExtractLevelLinkedGroups(document, &error) && extractLevelLinkedGroups(&document, &report, &error),
		             "extract a nested group whole", error);
		if (!ok) {
			return 1;
		}
		ok &= expect(report.createdGroups.size() == 3 && report.nestedGroups == 3 && report.splitEntities == 0 &&
		                 document.entities.size() == before.entities.size() && document.brushes.size() == before.brushes.size() &&
		                 document.patches.size() == before.patches.size() && document.scene.nodes.size() == before.scene.nodes.size() + 3 &&
		                 validateLevelScene(document, document.scene, &error) && settled(document),
		             "nested groups move into the extracted groups without copies", error);
		QStringList extracted;
		for (int i = 0; i < assemblies.size(); ++i) {
			extracted << extractedFor(document, report.createdGroups, assemblies[i]);
			const auto* inner = levelSceneNode(document.scene, inners[i]);
			const auto* core = levelSceneNode(document.scene, cores[i]);
			const auto* trim = levelSceneNode(document.scene, trims[i]);
			const auto* old = levelSceneNode(before.scene, inners[i]);
			ok &= expect(!extracted[i].isEmpty() && inner && core && trim && inner->parentId == extracted[i] && inner->linkId == innerLink &&
			                 inner->linkFrame == old->linkFrame && inner->linkObjects == old->linkObjects && core->parentId == inners[i] &&
			                 core->name == (i == 1 ? "Core B" : "Core") && core->visible == (i != 2) && trim->parentId == assemblies[i],
			             "nested groups keep their structure, links, frames, names and visibility");
			ok &= expect(content(document, extracted[i]) == content(before, inners[i]) &&
			                 content(document, assemblies[i]) == content(before, assemblies[i]) - content(before, inners[i]) &&
			                 identities(document, extracted[i]) == identities(document, extracted[0]) &&
			                 object(document, extracted[i], panel) == object(before, assemblies[i], panel),
			             "each half knows its whole content by shared identities");
			for (const auto& selector : content(before, assemblies[i])) {
				ok &= expect(geometry(before, selector) == geometry(document, selector), "moved and kept geometry stays exact", selector);
			}
		}
		ok &= expect(document.openSceneGroups == QStringList{outer} && document.undoStack.size() == 1 &&
		                 levelSceneNode(document.scene, report.extractedGroup)->linkId != levelSceneNode(document.scene, assemblies[0])->linkId,
		             "nested extraction closes the source as one undo step");
		const auto after = serializeLevelMap(document).bytes;
		const auto selectedAfter = document.selection;
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == bytes && document.selection == selection &&
		                 document.openSceneGroups == before.openSceneGroups && settled(document),
		             "one undo restores the nested structure, selection and context", error);
		ok &= expect(redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == after && document.selection == selectedAfter &&
		                 settled(document),
		             "redo moves the nested groups again", error);
		LevelMapDocument reopened;
		ok &= expect(loadLevelMapBytes({"nested.map", {}, "idTech3"}, after, &reopened, &error) && serializeLevelMap(reopened).bytes == after &&
		                 validateLevelScene(reopened, reopened.scene, &error),
		             "save/reopen keeps the nested extraction", error);
		// An edit inside a moved group reaches its counterparts and nothing else.
		closeLevelSceneGroup(&document, true);
		const auto notices = document.linkedGroupNotices.size();
		ok &= expect(setLevelMapEntityProperty(&document, ref(object(document, extracted[0], nestedRelay)).objectId, "health", "27", &error),
		             "edit inside an extracted nested group", error);
		for (int i = 0; i < assemblies.size(); ++i) {
			const auto relayObject = object(document, extracted[i], nestedRelay);
			const auto* core = levelSceneNode(document.scene, nestedGroup(document, extracted[i], {innerLink, coreLink}));
			ok &= expect(property(document, relayObject, "health") == "27" &&
			                 property(document, relayObject, "targetname") == QStringLiteral("relay_%1").arg(i) &&
			                 property(document, object(document, assemblies[i], nestedDoor), "health").isEmpty() && core &&
			                 core->visible == (i != 2),
			             "the nested edit reaches every extracted group, keeping local values");
		}
		ok &= expect(document.linkedGroupNotices.size() == notices && settled(document), "the follow-up edit raises no conflict",
		             document.linkedGroupNotices.join('\n'));
	}
	{
		auto base = nestedBase;
		openLevelSceneGroup(&base, assemblies[0]);
		const auto inner = nestedGroup(base, assemblies[0], {innerLink});
		setLevelMapSelection(&base, {ref(object(base, assemblies[0], panel))});
		ok &= refused(base, "part of a closed nested group refuses");
		setLevelMapSelection(&base, levelSceneSelection(base, inner));
		auto unlinked = base;
		for (auto& node : unlinked.scene.nodes) {
			if (node.id == inner) {
				node.linkId.clear();
				node.linkObjects.clear();
			}
		}
		ok &= expect(validateLevelScene(unlinked, unlinked.scene, &error), "an unlinked nested group is representable", error);
		ok &= refused(unlinked, "an unlinked nested group refuses");
		auto missing = base;
		const auto third = nestedGroup(missing, assemblies[2], {innerLink});
		for (auto& node : missing.scene.nodes) {
			if (node.id == third) {
				node.linkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
			}
		}
		ok &= refused(missing, "a counterpart without the nested group refuses");
		auto unmatched = nestedBase;
		const auto opened = nestedGroup(unmatched, assemblies[0], {innerLink}), lost = nestedGroup(unmatched, assemblies[2], {innerLink});
		for (auto& node : unmatched.scene.nodes) {
			if (node.id == lost) {
				node.linkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
			}
		}
		openLevelSceneGroup(&unmatched, opened);
		setLevelMapSelection(&unmatched, levelSceneSelection(unmatched, nestedGroup(unmatched, opened, {coreLink})));
		ok &= refused(unmatched, "a linked group around the open one without its counterpart refuses");
		auto lockedCore = base;
		ok &= expect(setLevelSceneLocked(&lockedCore, nestedGroup(lockedCore, assemblies[1], {innerLink, coreLink}), true, &error),
		             "lock a group in a nested counterpart", error);
		ok &= refused(lockedCore, "a locked group inside a moving counterpart refuses");
		auto lockedTrim = base;
		const auto trim = nestedGroup(lockedTrim, assemblies[1], {trimLink});
		ok &= expect(setLevelSceneLocked(&lockedTrim, trim, true, &error) && extractLevelLinkedGroups(&lockedTrim, nullptr, &error) &&
		                 levelSceneNode(lockedTrim.scene, trim)->locked && levelSceneNode(lockedTrim.scene, trim)->parentId == assemblies[1] &&
		                 validateLevelScene(lockedTrim, lockedTrim.scene, &error),
		             "a locked nested group left behind keeps its place and lock", error);
	}
	{
		// The relay and Core leave Inner, itself linked in the linked
		// assemblies: the assemblies around stay matched.
		auto document = nestedBase;
		document.undoLimit = 1;
		QStringList inners, cores;
		for (const auto& assembly : std::as_const(assemblies)) {
			inners << nestedGroup(document, assembly, {innerLink});
			cores << nestedGroup(document, assembly, {innerLink, coreLink});
		}
		openLevelSceneGroup(&document, inners[0]);
		const auto innerRelay = levelSceneNode(document.scene, inners[0])->linkObjects.value(object(document, assemblies[0], nestedRelay));
		auto selection = levelSceneSelection(document, cores[0]);
		selection << ref(object(document, assemblies[0], nestedRelay));
		setLevelMapSelection(&document, selection);
		selection = document.selection;
		const auto before = document;
		const auto bytes = serializeLevelMap(document).bytes;
		LevelLinkedExtractionReport report;
		ok &= expect(extractLevelLinkedGroups(&document, &report, &error) && report.createdGroups.size() == 3 && report.nestedGroups == 3 &&
		                 validateLevelScene(document, document.scene, &error) && settled(document) &&
		                 document.openSceneGroups == QStringList({outer, assemblies[0]}),
		             "extract from a linked group nested in linked groups", error);
		if (!ok) {
			return 1;
		}
		const auto extractedLink = levelSceneNode(document.scene, report.extractedGroup)->linkId;
		for (int i = 0; i < assemblies.size(); ++i) {
			const auto extracted = extractedFor(document, report.createdGroups, inners[i]);
			const auto relayCopy = object(document, extracted, innerRelay);
			ok &= expect(!extracted.isEmpty() && levelSceneNode(document.scene, extracted)->parentId == assemblies[i] &&
			                 levelSceneNode(document.scene, cores[i])->parentId == extracted && !relayCopy.isEmpty() &&
			                 object(document, inners[i], innerRelay).isEmpty() &&
			                 property(document, relayCopy, "targetname") == QStringLiteral("relay_%1").arg(i) &&
			                 property(document, relayCopy, "origin") == property(before, object(before, assemblies[i], nestedRelay), "origin") &&
			                 levelSceneNode(document.scene, assemblies[i])->linkObjects.value(relayCopy) == nestedRelay &&
			                 identities(document, assemblies[i]) == identities(document, assemblies[0]),
			             "the assemblies around know each extracted object as before");
		}
		const auto after = serializeLevelMap(document).bytes;
		ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == bytes && document.selection == selection &&
		                 document.openSceneGroups == before.openSceneGroups && settled(document) && redoLevelMapEdit(&document, &error) &&
		                 serializeLevelMap(document).bytes == after && settled(document),
		             "undo and redo an extraction inside linked groups", error);
		// An edit to the assemblies carries the new structure and local values.
		closeLevelSceneGroup(&document, true);
		const auto notices = document.linkedGroupNotices.size();
		ok &= expect(setLevelMapEntityProperty(&document, ref(object(document, assemblies[0], speaker)).objectId, "health", "27", &error),
		             "edit the assembly around the extraction", error);
		for (int i = 0; i < assemblies.size(); ++i) {
			ok &= expect(property(document, object(document, assemblies[i], speaker), "health") == "27" &&
			                 property(document, object(document, assemblies[i], nestedRelay), "targetname") == QStringLiteral("relay_%1").arg(i) &&
			                 property(document, object(document, assemblies[i], nestedDoor), "speed") == QString::number(111 * (i + 1)) &&
			                 !nestedGroup(document, assemblies[i], {extractedLink, coreLink}).isEmpty(),
			             "every assembly takes the edit with the new structure and keeps local values");
		}
		ok &= expect(document.linkedGroupNotices.size() == notices && settled(document), "no conflict after extraction inside linked groups",
		             document.linkedGroupNotices.join('\n'));
	}
	{
		// A linked group in each of two linked frames, split through a brush
		// entity: the frames stay matched and later edits agree.
		LevelMapDocument frameBase;
		QStringList frames;
		QString heldLink;
		{
			QString frame, assembly, copy;
			ok &= expect(tests::loadPrefabFixture(&frameBase, &error) &&
			                 createLevelSceneNode(&frameBase, LevelSceneNodeKind::Group, "Frame", {}, &frame, &error) &&
			                 createLevelSceneNode(&frameBase, LevelSceneNodeKind::Group, "Assembly", frame, &assembly, &error) &&
			                 assignLevelSceneObjects(&frameBase, frame, {"entity:3"}, &error) &&
			                 assignLevelSceneObjects(&frameBase, assembly, {"entity:1", "entity:2", "patch:0"}, &error) &&
			                 createLinkedLevelGroup(&frameBase, frame, {512, 0, 0, true}, &copy, &error),
			             "linked groups in linked frames", error);
			if (!ok) {
				return 1;
			}
			frames = {frame, copy};
			heldLink = levelSceneNode(frameBase.scene, assembly)->linkId;
		}
		const auto frameIds = levelSceneNode(frameBase.scene, frames[0])->linkObjects;
		const auto frameDoor = frameIds.value("entity:1"), frameRelay = frameIds.value("entity:2"), frameBrush = frameIds.value("brush:0");
		for (bool local : {false, true}) {
			auto document = frameBase;
			for (int i = 0; local && i < frames.size(); ++i) {
				ok &= expect(setLevelProtectedEntityProperty(&document, {ref(object(document, frames[i], frameDoor)).objectId}, "speed",
				                                             {QString::number(111 * (i + 1))}, &error),
				             "local door speeds in each frame", error);
			}
			const auto held = nestedGroup(document, frames[0], {heldLink});
			const auto heldDoor = levelSceneNode(document.scene, held)->linkObjects.value(object(document, frames[0], frameDoor));
			const auto heldBrush = levelSceneNode(document.scene, held)->linkObjects.value(object(document, frames[0], frameBrush));
			openLevelSceneGroup(&document, held);
			setLevelMapSelection(&document, {ref(object(document, frames[0], frameBrush)), ref(object(document, frames[0], frameRelay))});
			LevelLinkedExtractionReport report;
			ok &= expect(extractLevelLinkedGroups(&document, &report, &error) && report.createdGroups.size() == 2 && report.splitEntities == 2 &&
			                 validateLevelScene(document, document.scene, &error) && settled(document) &&
			                 identities(document, frames[0]) == identities(document, frames[1]) &&
			                 (local || levelLinkedGroupContent(document, frames[0]).shape == levelLinkedGroupContent(document, frames[1]).shape),
			             "the linked frames around a split stay matched", error);
			if (!ok) {
				return 1;
			}
			const auto extractedLink = levelSceneNode(document.scene, report.extractedGroup)->linkId;
			QSet<QString> splitIdentities;
			for (const auto& frame : std::as_const(frames)) {
				const auto copy = object(document, nestedGroup(document, frame, {extractedLink}), heldDoor);
				const auto original = object(document, frame, frameDoor);
				splitIdentities.insert(levelSceneNode(document.scene, frame)->linkObjects.value(copy));
				ok &= expect(!copy.isEmpty() && !original.isEmpty() && copy != original, "the door is split in each frame");
			}
			ok &= expect(splitIdentities.size() == 1 && !splitIdentities.contains(frameDoor),
			             "a split owner's new half has one identity across the frames");
			closeLevelSceneGroup(&document, true);
			const auto notices = document.linkedGroupNotices.size();
			if (local) {
				ok &= expect(setLevelMapEntityProperty(&document,
				                                       ref(object(document, nestedGroup(document, frames[0], {extractedLink}), heldDoor)).objectId,
				                                       "health", "27", &error),
				             "edit the extracted half in one frame", error);
				for (int i = 0; i < frames.size(); ++i) {
					const auto copy = object(document, nestedGroup(document, frames[i], {extractedLink}), heldDoor);
					ok &= expect(property(document, copy, "health") == "27" && property(document, copy, "speed") == QString::number(111 * (i + 1)) &&
					                 property(document, object(document, frames[i], frameDoor), "health").isEmpty(),
					             "the edit reaches the other frame and keeps its local speed");
				}
			} else {
				QVector<LevelMapSelectionRef> both;
				for (const auto& frame : std::as_const(frames)) {
					both << ref(object(document, nestedGroup(document, frame, {extractedLink}), heldBrush));
				}
				ok &= expect(setLevelMapSelection(&document, both) && moveLevelMapSelection(&document, 0, 0, 16, &error),
				             "move the extracted brush in both frames at once", error);
			}
			ok &= expect(document.linkedGroupNotices.size() == notices && settled(document),
			             local ? "an edit after the split raises no conflict" : "the same edit to both frames agrees",
			             document.linkedGroupNotices.join('\n'));
		}
	}
	if (argc == 2) {
		QTemporaryDir temp;
		if (!temp.isValid()) {
			return 1;
		}
		const auto input = temp.filePath("assembly.map"), output = temp.filePath("extracted.map");
		QFile file(input);
		if (!file.open(QIODevice::WriteOnly)) {
			return 1;
		}
		file.write(serializeLevelMap(initial).bytes);
		file.close();
		LevelMapDocument saved;
		if (!loadLevelMap({input, {}, {}}, &saved, &error)) {
			return 1;
		}
		const auto chosen = object(saved, group, firstBrush) + ',' + object(saved, group, relay);
		const auto run = [&](const QStringList& arguments, bool success = true) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(QString::fromLocal8Bit(argv[1]),
			              QStringList{"--cli", "--json", "--settings-file", temp.filePath("cli.ini"), "editor", "scene"} + arguments);
			const bool ended = process.waitForStarted() && process.waitForFinished(30000);
			const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success,
			             "extraction CLI exit", QString::fromUtf8((out + err).left(1800)));
			if (!ended) {
				process.kill();
				process.waitForFinished();
			}
			return QJsonDocument::fromJson(out).object();
		};
		const QStringList extract{"extract-linked", input, "--context", group, "--objects", chosen, "--output", output};
		const auto dry = run(extract + QStringList{"--dry-run"});
		ok &= expect(dry.value("dryRun").toBool() && dry.value("extraction").toObject().value("instances").toInt() == 3 &&
		                 !QFile::exists(output),
		             "CLI dry run validates all instances without writing");
		const auto result = run(extract).value("extraction").toObject();
		const auto created = result.value("createdGroups").toArray();
		ok &= expect(created.size() == 3 && result.value("splitEntities").toInt() == 3 && loadLevelMap({output, {}, {}}, &saved, &error) &&
		                 levelSceneNode(saved.scene, result.value("extractedGroup").toString()) &&
		                 saved.scene.nodes.size() == initial.scene.nodes.size() + 3,
		             "CLI returns durable group IDs and saves both families", error);
		const auto read = [](const QString& path) {
			QFile file(path);
			return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
		};
		const auto stable = read(output);
		run(extract, false);
		run({"extract-linked", input, "--objects", chosen, "--output", output, "--overwrite"}, false);
		run({"extract-linked", input, "--context", group, "--objects", "entity:0", "--output", output, "--overwrite"}, false);
		run({"extract-linked", input, "--context", group, "--objects", "brush:99999", "--output", output, "--overwrite"}, false);
		run(extract + QStringList{"--overwrite", "--id", group}, false);
		run(extract + QStringList{"--overwrite", "--objects", chosen}, false);
		ok &= expect(read(output) == stable, "CLI refusals preserve existing output");
		// The CLI moves nested groups the same way.
		const auto nestedInput = temp.filePath("nested.map"), nestedOutput = temp.filePath("nested-extracted.map");
		QFile nestedFile(nestedInput);
		if (!nestedFile.open(QIODevice::WriteOnly)) {
			return 1;
		}
		nestedFile.write(serializeLevelMap(nestedBase).bytes);
		nestedFile.close();
		LevelMapDocument nestedSaved;
		if (!loadLevelMap({nestedInput, {}, {}}, &nestedSaved, &error)) {
			return 1;
		}
		const auto savedInner = nestedGroup(nestedSaved, assemblies[0], {innerLink});
		QStringList unit;
		for (const auto& member : levelSceneSelection(nestedSaved, savedInner)) {
			unit << levelMapSelectionRefId(member);
		}
		const auto nestedResult =
		    run({"extract-linked-groups", nestedInput, "--context", assemblies[0], "--objects", unit.join(','), "--output", nestedOutput})
		        .value("extraction")
		        .toObject();
		ok &= expect(nestedResult.value("nestedGroups").toInt() == 3 && nestedResult.value("createdGroups").toArray().size() == 3 &&
		                 loadLevelMap({nestedOutput, {}, {}}, &nestedSaved, &error) &&
		                 levelSceneNode(nestedSaved.scene, savedInner)->parentId == nestedResult.value("extractedGroup").toString(),
		             "the CLI moves nested groups into the extracted groups", error);
	}
	if (ok) {
		std::cout << "Linked extraction preserves instances, partial ownership, nested groups, the linked groups around, geometry, UVs, "
		             "protection, history and context.\n";
	}
	return ok ? 0 : 1;
}
