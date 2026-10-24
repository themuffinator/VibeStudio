#include "core/level_linked_groups.h"
#include "core/level_linked_objects.h"
#include "core/level_prefab.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "tests/level_prefab_test_helpers.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

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

QSet<QString> identities(const LevelSceneNode& node)
{
	return QSet<QString>(node.linkObjects.cbegin(), node.linkObjects.cend());
}

QString objectFor(const LevelMapDocument& document, const QString& group, const QString& identity)
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

int nativeId(const QString& object)
{
	return object.isEmpty() ? -1 : object.section(QLatin1Char(':'), 1).toInt();
}

QString property(const LevelMapDocument& document, int id, const QString& key)
{
	for (const auto& entity : document.entities) {
		if (entity.id == id) {
			for (const auto& item : entity.properties) {
				if (item.key == key) {
					return item.value;
				}
			}
		}
	}
	return {};
}

QJsonObject metadata(const QByteArray& bytes)
{
	const QByteArray marker("\n// VibeStudioScene: ");
	const auto at = bytes.lastIndexOf(marker);
	return at < 0 ? QJsonObject() : QJsonDocument::fromJson(QByteArray::fromBase64(bytes.mid(at + marker.size()).trimmed())).object();
}

bool validFamily(const LevelMapDocument& document, const QString& group, qsizetype size)
{
	const auto* source = levelSceneNode(document.scene, group);
	if (!source || source->linkObjects.size() != size || identities(*source).size() != size) {
		return false;
	}
	for (const auto& node : document.scene.nodes) {
		if (node.linkId == source->linkId && (node.linkObjects.size() != size || identities(node) != identities(*source))) {
			return false;
		}
	}
	return validateLevelScene(document, document.scene);
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	bool ok = true;
	LevelMapDocument document;
	QString group, copy;
	if (!tests::loadPrefabFixture(&document, &error) ||
	    !createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Assembly"), {}, &group, &error) ||
	    !assignLevelSceneObjects(&document, group, {QStringLiteral("entity:1"), QStringLiteral("entity:2"), QStringLiteral("patch:0")},
	                             &error)) {
		return expect(false, "fixture", error) ? 0 : 1;
	}
	const auto beforeLink = serializeLevelMap(document).bytes;
	{
		auto limited = document;
		limited.undoLimit = 1;
		QString limitedCopy;
		ok &= expect(createLinkedLevelGroup(&limited, group, {256, 0, 0, true}, &limitedCopy, &error),
		             "create linked copy with one history slot", error);
		const auto limitedAfter = serializeLevelMap(limited).bytes;
		ok &= expect(limited.undoLimit == 1 && limited.undoStack.size() == 1 && undoLevelMapEdit(&limited, &error) &&
		                 serializeLevelMap(limited).bytes == beforeLink && redoLevelMapEdit(&limited, &error) &&
		                 serializeLevelMap(limited).bytes == limitedAfter,
		             "one history slot retains the whole linked-copy transaction", error);
	}
	{
		auto full = document;
		while (full.scene.nodes.size() < kLevelSceneMaxNodes) {
			LevelSceneNode empty;
			empty.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
			empty.name = QStringLiteral("Empty %1").arg(full.scene.nodes.size());
			full.scene.nodes << empty;
		}
		const auto before = serializeLevelMap(full).bytes;
		const auto selection = full.selection;
		const auto revision = full.revision;
		const auto history = full.undoStack.size();
		const auto redo = full.redoStack.size();
		QString output = QStringLiteral("unchanged");
		ok &= expect(!before.isEmpty() && !createLinkedLevelGroup(&full, group, {256, 0, 0, true}, &output, &error) &&
		                 serializeLevelMap(full).bytes == before && full.selection == selection && full.revision == revision &&
		                 full.undoStack.size() == history && full.redoStack.size() == redo && output == QStringLiteral("unchanged"),
		             "metadata-limit refusal preserves geometry, selection, history, revision and caller output", error);
	}
	const auto linkHistory = document.undoStack.size();
	if (!expect(createLinkedLevelGroup(&document, group, {256, 0, 0, true}, &copy, &error), "link", error)) {
		return 1;
	}
	ok &= expect(validFamily(document, group, 5), "entities, implicit owned brushes and explicit patch have unique paired identities");
	const auto original = *levelSceneNode(document.scene, group);
	const auto brushIdentity = original.linkObjects.value(QStringLiteral("brush:0"));
	const auto remainingBrushIdentity = original.linkObjects.value(QStringLiteral("brush:1"));
	const auto entityIdentity = original.linkObjects.value(QStringLiteral("entity:2"));
	const auto ownedEntityIdentity = original.linkObjects.value(QStringLiteral("entity:1"));
	const auto afterLink = serializeLevelMap(document).bytes;
	ok &= expect(document.undoStack.size() == linkHistory + 1 && undoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == beforeLink && redoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == afterLink,
	             "link identity creation is an exact single undo/redo", error);

	const auto beforeEdit = serializeLevelMap(document).bytes;
	const auto editHistory = document.undoStack.size();
	ok &= expect(setLevelMapEntityProperty(&document, 2, QStringLiteral("target"), QStringLiteral("new_gate"), &error) &&
	                 validFamily(document, group, 5) &&
	                 property(document, nativeId(objectFor(document, copy, entityIdentity)), QStringLiteral("target")) ==
	                     QStringLiteral("new_gate"),
	             "entity identity survives rebuilding the other instance", error);
	const auto afterEdit = serializeLevelMap(document).bytes;
	ok &= expect(document.undoStack.size() == editHistory + 1 && undoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == beforeEdit && redoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == afterEdit,
	             "propagation restores identities with the same undo step", error);

	// Removing the first child must not pair the remaining child with the first
	// peer by position. Later additions and partial duplicates are new objects.
	ok &= expect(deleteLevelMapObjects(&document, {{LevelMapSelectionKind::QuakeBrush, 0}}, &error) && validFamily(document, group, 4) &&
	                 objectFor(document, copy, brushIdentity).isEmpty() && !objectFor(document, copy, remainingBrushIdentity).isEmpty(),
	             "delete removes only the correct owned-child identity", error);
	document.activeSceneNode = group;
	int added = -1;
	ok &= expect(addLevelMapBoxBrush(&document, {0, 0, 0, true}, {16, 16, 16, true}, QStringLiteral("studio/new"), &added, &error) &&
	                 validFamily(document, group, 5),
	             "new member receives correspondence across copies", error);
	const auto newIdentity = levelSceneNode(document.scene, group)->linkObjects.value(QStringLiteral("brush:%1").arg(added));
	ok &= expect(!newIdentity.isEmpty() && !identities(original).contains(newIdentity), "new geometry does not reuse a deleted identity");
	const auto sourceEntity = nativeId(objectFor(document, group, entityIdentity));
	ok &= expect(duplicateLevelMapObjects(&document, {{LevelMapSelectionKind::Entity, sourceEntity}}, 16, 0, 0, {}, &error, false) &&
	                 validFamily(document, group, 6),
	             "partial duplicate receives a distinct logical identity", error);
	ok &= expect(identities(*levelSceneNode(document.scene, group)).contains(entityIdentity),
	             "partial duplication retains its source identity");

	// Native ids may change during export. UUIDs must survive independently,
	// including children with no explicit membership.
	const auto saved = serializeLevelMap(document).bytes;
	const auto savedIdentities = identities(*levelSceneNode(document.scene, group));
	LevelMapDocument reopened;
	ok &= expect(metadata(saved).value(QStringLiteral("version")).toInt() == 6 &&
	                 loadLevelMapBytes({QStringLiteral("linked.map"), {}, document.engineFamily}, saved, &reopened, &error) &&
	                 validFamily(reopened, group, 6) && identities(*levelSceneNode(reopened.scene, group)) == savedIdentities &&
	                 serializeLevelMap(reopened).bytes == saved,
	             "version six remaps selectors and round trips exact bytes", error);
	if (!ok) {
		return 1;
	}
	document = reopened;
	const auto beforeLock = *levelSceneNode(document.scene, copy);
	ok &= expect(setLevelSceneLocked(&document, copy, true, &error) &&
	                 setLevelMapEntityProperty(&document, nativeId(objectFor(document, group, entityIdentity)), QStringLiteral("target"),
	                                           QStringLiteral("locked_gate"), &error) &&
	                 levelSceneNode(document.scene, copy)->linkObjects == beforeLock.linkObjects &&
	                 property(document, nativeId(objectFor(document, copy, entityIdentity)), QStringLiteral("target")) ==
	                     QStringLiteral("new_gate"),
	             "locked copy keeps its own records and correspondence", error);
	ok &= expect(setLevelSceneLocked(&document, copy, false, &error), "unlock copy", error);
	document.undoLimit = 1;
	const auto beforeUpdate = serializeLevelMap(document).bytes;
	ok &= expect(updateLinkedLevelGroups(&document, group, nullptr, &error) && validFamily(document, group, 6) &&
	                 property(document, nativeId(objectFor(document, copy, entityIdentity)), QStringLiteral("target")) ==
	                     QStringLiteral("locked_gate"),
	             "explicit update reuses the established identities", error);
	const auto afterUpdate = serializeLevelMap(document).bytes;
	ok &= expect(document.undoLimit == 1 && document.undoStack.size() == 1 && undoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == beforeUpdate && redoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == afterUpdate,
	             "one history slot retains the whole explicit update", error);

	// Complete group duplicates, portable families and prefabs retain pairing.
	setLevelMapSelection(&document, levelSceneSelection(document, group));
	ok &= expect(duplicateLevelMapSelection(&document, 512, 0, 0, &error) && document.scene.nodes.size() == 3 &&
	                 validFamily(document, group, 6),
	             "whole group duplicate shares object identities", error);
	{
		auto limited = document;
		limited.undoLimit = 1;
		ok &= expect(setLevelSceneLocked(&limited, copy, true, &error), "lock one of three copies", error);
		const auto before = serializeLevelMap(limited).bytes;
		ok &= expect(setLevelMapEntityProperty(&limited, nativeId(objectFor(limited, group, entityIdentity)), QStringLiteral("target"),
		                                       QStringLiteral("guarded_history"), &error) &&
		                 validFamily(limited, group, 6) &&
		                 property(limited, nativeId(objectFor(limited, copy, entityIdentity)), QStringLiteral("target")) ==
		                     QStringLiteral("locked_gate") &&
		                 property(limited, nativeId(objectFor(limited, limited.scene.nodes.last().id, entityIdentity)),
		                          QStringLiteral("target")) == QStringLiteral("guarded_history"),
		             "automatic propagation with a locked peer and one history slot", error);
		const auto after = serializeLevelMap(limited).bytes;
		ok &= expect(limited.undoLimit == 1 && limited.undoStack.size() == 1 && undoLevelMapEdit(&limited, &error) &&
		                 serializeLevelMap(limited).bytes == before && redoLevelMapEdit(&limited, &error) &&
		                 serializeLevelMap(limited).bytes == after,
		             "guarded propagation preserves all correspondence in one undo", error);
	}
	setLevelMapSelection(&document, pickLevelSceneObjects(levelSceneSelectionContext(document), {},
	                                                      levelMapSelectAllObjects(document, false), LevelScenePick::Replace));
	const auto clipboard = levelMapSelectionText(document, &error);
	LevelMapDocument imported;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("quake3");
	request.starterRoom = false;
	const bool pasted = createLevelMap(request, &imported, &error) && pasteLevelMapText(&imported, clipboard, &error);
	ok &= expect(pasted && imported.scene.nodes.size() == 3 && validFamily(imported, imported.scene.nodes.first().id, 6) &&
	                 imported.scene.nodes.first().linkId != levelSceneNode(document.scene, group)->linkId &&
	                 identities(imported.scene.nodes.first()) == savedIdentities,
	             "portable family remaps its link scope while retaining correspondence", error);
	if (!ok || imported.scene.nodes.isEmpty()) {
		return 1;
	}
	const auto firstImportedLink = imported.scene.nodes.first().linkId;
	ok &= expect(pasteLevelMapText(&imported, clipboard, &error) && imported.scene.nodes.size() == 6 &&
	                 imported.scene.nodes.last().linkId != firstImportedLink && validFamily(imported, imported.scene.nodes.last().id, 6),
	             "repeated paste cannot cross-link different families", error);
	LevelPrefab prefab;
	ok &= expect(createLevelPrefab(document, {QStringLiteral("Linked identities"), {}, {}}, &prefab, nullptr, &error), "capture prefab",
	             error);
	const auto beforePrefab = serializeLevelMap(imported).bytes;
	const auto prefabHistory = imported.undoStack.size();
	ok &= expect(insertLevelPrefab(&imported, prefab, {{0, 1024, 0, true}, {0, 0, 90, true}, true, {}}, nullptr, &error) &&
	                 imported.scene.nodes.size() == 9 && validFamily(imported, imported.scene.nodes.last().id, 6) &&
	                 imported.undoStack.size() == prefabHistory + 1 && undoLevelMapEdit(&imported, &error) &&
	                 serializeLevelMap(imported).bytes == beforePrefab,
	             "rotated prefab preserves identities and exact undo", error);
	setLevelMapSelection(&document, levelSceneSelection(document, group));
	const auto singleton = levelMapSelectionText(document, &error);
	ok &= expect(pasteLevelMapText(&imported, singleton, &error) && imported.scene.nodes.last().linkId.isEmpty() &&
	                 imported.scene.nodes.last().linkObjects.isEmpty(),
	             "portable singleton drops its link and correspondence", error);

	// Reading and resaving v3 does not invent correspondence; an actual update
	// establishes it without guessing by position.
	auto json = metadata(saved);
	const auto bodyHash = QByteArray::fromHex(json.value(QStringLiteral("sha256")).toString().toLatin1());
	auto oldJson = json;
	oldJson.insert(QStringLiteral("version"), 3);
	oldJson.remove(QStringLiteral("protectedProperties"));
	auto oldNodes = oldJson.value(QStringLiteral("nodes")).toArray();
	for (auto it = oldNodes.begin(); it != oldNodes.end(); ++it) {
		// Version 3 gave a frame as quarter turns and a mirror.
		auto node = it->toObject();
		node.remove(QStringLiteral("linkObjects"));
		node.remove(QStringLiteral("frame"));
		node.remove(QStringLiteral("outOfSync"));
		node.insert(QStringLiteral("turn"), 0);
		node.insert(QStringLiteral("mirror"), false);
		*it = node;
	}
	oldJson.insert(QStringLiteral("nodes"), oldNodes);
	auto legacy = reopened;
	legacy.scene = decodeLevelScene(legacy, QJsonDocument(oldJson).toJson(QJsonDocument::Compact).toBase64(), bodyHash);
	QHash<QString, QString> emitted;
	for (const auto& object : levelSceneObjects(legacy)) {
		emitted.insert(object, object);
	}
	const auto oldEncoded = encodeLevelScene(legacy, bodyHash, emitted, &error);
	ok &= expect(legacy.scene.problem.isEmpty() && legacy.scene.nodes.first().linkObjects.isEmpty() &&
	                 QJsonDocument::fromJson(QByteArray::fromBase64(oldEncoded)).object().value(QStringLiteral("version")).toInt() == 6,
	             "legacy read and write preserve unknown correspondence", error);
	{
		auto mixed = legacy;
		QString third;
		ok &= expect(
		    setLevelSceneLocked(&mixed, copy, true, &error) && createLinkedLevelGroup(&mixed, group, {512, 0, 0, true}, &third, &error) &&
		        levelSceneNode(mixed.scene, copy)->linkObjects.isEmpty() && levelSceneNode(mixed.scene, group)->linkObjects.size() == 6 &&
		        identities(*levelSceneNode(mixed.scene, group)) == identities(*levelSceneNode(mixed.scene, third)) &&
		        validateLevelScene(mixed, mixed.scene),
		    "new legacy duplicate establishes only known pairs and leaves the locked legacy peer unresolved", error);
		const auto mixedBytes = serializeLevelMap(mixed).bytes;
		LevelMapDocument mixedAgain;
		ok &= expect(loadLevelMapBytes({QStringLiteral("mixed.map"), {}, document.engineFamily}, mixedBytes, &mixedAgain, &error) &&
		                 levelSceneNode(mixedAgain.scene, copy) && levelSceneNode(mixedAgain.scene, copy)->linkObjects.isEmpty() &&
		                 serializeLevelMap(mixedAgain).bytes == mixedBytes,
		             "version six preserves a mixture of known and unresolved legacy correspondence", error);
	}
	ok &= expect(updateLinkedLevelGroups(&legacy, group, nullptr, &error) && validFamily(legacy, group, 6),
	             "legacy update establishes known correspondence", error);

	// Malformed, foreign and future metadata remains opaque, never attached to
	// the wrong native object. These codec checks are independent of reconcile.
	const auto preserved = [&](QJsonObject invalid) {
		const auto encoded = QJsonDocument(invalid).toJson(QJsonDocument::Compact).toBase64();
		const auto decoded = decodeLevelScene(reopened, encoded, bodyHash);
		return decoded.nodes.isEmpty() && decoded.opaqueMetadata == encoded && !decoded.problem.isEmpty();
	};
	const auto invalidNode = [&](const QJsonObject& node) {
		auto invalid = json;
		auto nodes = invalid.value(QStringLiteral("nodes")).toArray();
		nodes[0] = node;
		invalid.insert(QStringLiteral("nodes"), nodes);
		return preserved(invalid);
	};
	const auto validNode = json.value(QStringLiteral("nodes")).toArray().first().toObject();
	const auto validObjects = validNode.value(QStringLiteral("linkObjects")).toObject();
	for (int variant = 0; variant < 6; ++variant) {
		auto invalid = validNode;
		auto objects = validObjects;
		switch (variant) {
		case 0:
			objects.insert(QStringLiteral("entity:99999"), QUuid::createUuid().toString(QUuid::WithoutBraces));
			break;
		case 1:
			objects.insert(objects.begin().key(), QStringLiteral("not-a-uuid"));
			break;
		case 2:
			objects.insert(objects.begin().key(), objects.value((++objects.begin()).key()));
			break;
		case 3:
			objects.insert(objects.begin().key(), 42);
			break;
		case 4:
			invalid.insert(QStringLiteral("link"), QString());
			break;
		case 5:
			invalid.insert(QStringLiteral("turn"), 0.5);
			break;
		}
		invalid.insert(QStringLiteral("linkObjects"), objects);
		ok &= expect(invalidNode(invalid), "invalid linked correspondence stays opaque", QString::number(variant));
	}
	auto wrongKind = validNode;
	auto wrongKindObjects = validObjects;
	const auto brushKey = objectFor(reopened, group, remainingBrushIdentity);
	const auto entityKey = objectFor(reopened, group, ownedEntityIdentity);
	wrongKindObjects.insert(brushKey, ownedEntityIdentity);
	wrongKindObjects.insert(entityKey, remainingBrushIdentity);
	wrongKind.insert(QStringLiteral("linkObjects"), wrongKindObjects);
	ok &= expect(invalidNode(wrongKind), "cross-copy kind mismatch stays opaque");
	auto future = json;
	future.insert(QStringLiteral("version"), 7);
	ok &= expect(preserved(future), "future schema stays opaque");
	emitted.remove(brushKey);
	ok &= expect(encodeLevelScene(reopened, bodyHash, emitted, &error).isEmpty() && !error.isEmpty(),
	             "missing implicit-child export mapping refuses saving");
	ok &= expect(unlinkLevelGroup(&reopened, group, &error) && reopened.scene.nodes.first().linkObjects.isEmpty() &&
	                 reopened.scene.nodes.last().linkObjects.isEmpty() &&
	                 metadata(serializeLevelMap(reopened).bytes).value(QStringLiteral("version")).toInt() == 2,
	             "unlinked pair clears correspondence and writes version two", error);

	if (ok) {
		std::cout << "Linked object identity, propagation, codec, clipboard and prefab checks passed.\n";
	}
	return ok ? 0 : 1;
}
