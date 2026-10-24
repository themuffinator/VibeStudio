#include "core/level_linked_groups.h"
#include "core/level_prefab.h"
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
#include <iostream>
#include <optional>

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
int corresponding(const LevelMapDocument& document, const QString& group, const QString& identity)
{
	const auto* node = levelSceneNode(document.scene, group);
	if (node) {
		for (auto it = node->linkObjects.cbegin(); it != node->linkObjects.cend(); ++it) {
			if (it.value() == identity && it.key().startsWith(QLatin1String("entity:"))) {
				return it.key().section(':', 1).toInt();
			}
		}
	}
	return -1;
}
std::optional<QString> value(const LevelMapDocument& document, int id, const QString& key)
{
	for (const auto& entity : document.entities) {
		if (entity.id == id) {
			for (const auto& property : entity.properties) {
				if (property.key.compare(key, Qt::CaseInsensitive) == 0) {
					return property.value;
				}
			}
		}
	}
	return std::nullopt;
}
bool roundtrip(const LevelMapDocument& document, LevelMapDocument* loaded, QString* error)
{
	const auto bytes = serializeLevelMap(document).bytes;
	return !bytes.isEmpty() && loadLevelMapBytes({QStringLiteral("protected.map"), {}, document.engineFamily}, bytes, loaded, error) &&
	       validateLevelScene(*loaded, loaded->scene, error) && serializeLevelMap(*loaded).bytes == bytes;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error, group, copy;
	LevelMapDocument document;
	if (!tests::loadPrefabFixture(&document, &error) ||
	    !createLevelSceneNode(&document, LevelSceneNodeKind::Group, QStringLiteral("Assembly"), {}, &group, &error) ||
	    !assignLevelSceneObjects(&document, group, {QStringLiteral("entity:1"), QStringLiteral("entity:2"), QStringLiteral("patch:0")},
	                             &error) ||
	    !createLinkedLevelGroup(&document, group, {256, 0, 0, true}, &copy, &error)) {
		return expect(false, "fixture", error) ? 0 : 1;
	}
	const auto identity = levelSceneNode(document.scene, group)->linkObjects.value(QStringLiteral("entity:2"));
	const auto initial = document;
	const auto original = [&] { return corresponding(document, group, identity); };
	const auto peer = [&] { return corresponding(document, copy, identity); };
	bool ok = true;
	document.undoLimit = 1;
	const auto before = serializeLevelMap(document).bytes;
	ok &= expect(
	    setLevelProtectedEntityProperty(&document, {original()}, QStringLiteral("targetname"), {QStringLiteral("instance_a")}, &error) &&
	        value(document, original(), QStringLiteral("targetname")) == QStringLiteral("instance_a") &&
	        value(document, peer(), QStringLiteral("targetname")) == QStringLiteral("relay") &&
	        levelEntityPropertyProtected(document, original(), QStringLiteral("TARGETNAME")) &&
	        !levelEntityPropertyProtected(document, peer(), QStringLiteral("targetname")),
	    "source-only protection preserves the peer value without spreading its flags", error);
	const auto protectedBytes = serializeLevelMap(document).bytes;
	ok &= expect(document.undoStack.size() == 1 && document.undoLimit == 1 && undoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == before && redoLevelMapEdit(&document, &error) &&
	                 serializeLevelMap(document).bytes == protectedBytes,
	             "protected value and metadata are one exact undo step", error);
	ok &= expect(setLevelMapEntityProperty(&document, peer(), QStringLiteral("targetname"), QStringLiteral("shared"), &error) &&
	                 value(document, original(), QStringLiteral("targetname")) == QStringLiteral("instance_a") &&
	                 value(document, peer(), QStringLiteral("targetname")) == QStringLiteral("shared") &&
	                 levelEntityPropertyProtected(document, original(), QStringLiteral("targetname")),
	             "target-only protection preserves the target value and flags", error);
	ok &= expect(setLevelMapEntityProperty(&document, original(), QStringLiteral("style"), QStringLiteral("2"), &error) &&
	                 value(document, peer(), QStringLiteral("style")) == QStringLiteral("2"),
	             "unprotected properties still propagate", error);
	ok &= expect(setLevelEntityPropertyProtection(&document, {original()}, {QStringLiteral("optional")}, true, &error) &&
	                 setLevelMapEntityProperty(&document, peer(), QStringLiteral("optional"), QStringLiteral("peer_value"), &error) &&
	                 !value(document, original(), QStringLiteral("optional")) &&
	                 value(document, peer(), QStringLiteral("optional")) == QStringLiteral("peer_value"),
	             "protected absence survives a peer adding the key", error);
	const auto beforeClear = serializeLevelMap(document).bytes;
	ok &= expect(clearLevelEntityPropertyProtection(&document, {original()}, &error) &&
	                 levelProtectedEntityProperties(document, original()).isEmpty() &&
	                 value(document, original(), QStringLiteral("targetname")) == QStringLiteral("shared") &&
	                 value(document, original(), QStringLiteral("optional")) == QStringLiteral("peer_value"),
	             "clear restores existing values from unprotected counterparts", error);
	const auto afterClear = serializeLevelMap(document).bytes;
	ok &= expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == beforeClear &&
	                 redoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == afterClear,
	             "clearing multiple keys is an exact one-slot undo/redo", error);
	if (!ok) {
		return 1;
	}

	ok &= expect(setLevelProtectedEntityProperty(&document, {original()}, QStringLiteral("targetname"), {QStringLiteral("a")}, &error) &&
	                 setLevelProtectedEntityProperty(&document, {peer()}, QStringLiteral("targetname"), {QStringLiteral("b")}, &error) &&
	                 clearLevelEntityPropertyProtection(&document, {original(), peer()}, &error) &&
	                 value(document, original(), QStringLiteral("targetname")) == QStringLiteral("a") &&
	                 value(document, peer(), QStringLiteral("targetname")) == QStringLiteral("b"),
	             "clearing all-protected peers keeps values when no unprotected value exists", error);
	ok &= expect(setLevelEntityPropertyProtection(&document, {peer()}, {QStringLiteral("origin"), QStringLiteral("angle")}, true, &error) &&
	                 setLevelMapEntityProperty(&document, peer(), QStringLiteral("angle"), QStringLiteral("27"), &error),
	             "protect spatial keys", error);
	const auto peerOrigin = value(document, peer(), QStringLiteral("origin"));
	ok &= expect(setLevelMapEntityProperty(&document, original(), QStringLiteral("style"), QStringLiteral("3"), &error) &&
	                 value(document, peer(), QStringLiteral("origin")) == peerOrigin &&
	                 value(document, peer(), QStringLiteral("angle")) == QStringLiteral("27"),
	             "spatial values are restored after the linked transform", error);
	LevelMapDocument reopened;
	ok &= expect(roundtrip(document, &reopened, &error) &&
	                 levelEntityPropertyProtected(reopened, corresponding(reopened, copy, identity), QStringLiteral("origin")),
	             "native scene version six round trips protected keys", error);

	// Both grouped and standalone copies retain instance intent, including
	// when a portable singleton is intentionally detached from its family.
	setLevelMapSelection(&document, {{LevelMapSelectionKind::Entity, peer()}});
	const auto text = levelMapSelectionText(document, &error);
	LevelMapDocument standalone;
	LevelMapCreateRequest request;
	request.game = QStringLiteral("quake3");
	request.starterRoom = false;
	ok &= expect(createLevelMap(request, &standalone, &error) && pasteLevelMapText(&standalone, text, &error) &&
	                 standalone.scene.nodes.isEmpty() && standalone.scene.protectedProperties.size() == 1,
	             "standalone entity clipboard carries protection without scene nodes", error);
	LevelMapDocument standaloneAgain;
	ok &= expect(roundtrip(standalone, &standaloneAgain, &error), "standalone protection persists", error);
	const auto detachedId = standalone.entities.last().id;
	ok &= expect(setLevelEntityPropertyProtection(&standalone, {detachedId}, {QStringLiteral("origin")}, false, &error) &&
	                 !levelEntityPropertyProtected(standalone, detachedId, QStringLiteral("origin")),
	             "unprotect works after detaching from the family", error);
	const auto standaloneBeforeDelete = serializeLevelMap(standalone).bytes;
	ok &= expect(deleteLevelMapObjects(&standalone, {{LevelMapSelectionKind::Entity, detachedId}}, &error) &&
	                 standalone.scene.protectedProperties.isEmpty() && undoLevelMapEdit(&standalone, &error) &&
	                 serializeLevelMap(standalone).bytes == standaloneBeforeDelete,
	             "deleting and undoing an entity restores protection metadata", error);
	ok &= expect(unlinkLevelGroup(&reopened, copy, &error) && !reopened.scene.protectedProperties.isEmpty(),
	             "unlink retains protected property intent", error);

	auto guarded = document;
	ok &= expect(setLevelSceneLocked(&guarded, copy, true, &error), "lock fixture", error);
	const auto guardedBefore = serializeLevelMap(guarded).bytes;
	const auto guardedRevision = guarded.revision;
	ok &= expect(
	    !setLevelEntityPropertyProtection(&guarded, {corresponding(guarded, copy, identity)}, {QStringLiteral("style")}, true, &error) &&
	        serializeLevelMap(guarded).bytes == guardedBefore && guarded.revision == guardedRevision,
	    "locks refuse protection edits atomically", error);
	ok &= expect(!setLevelProtectedEntityProperty(&guarded, {0}, QStringLiteral("message"), {QStringLiteral("bad")}, &error) &&
	                 serializeLevelMap(guarded).bytes == guardedBefore,
	             "world properties cannot become instance-only", error);
	ok &= expect(!setLevelEntityPropertyProtection(&guarded, {original()}, {QStringLiteral("bad\nkey")}, true, &error) &&
	                 serializeLevelMap(guarded).bytes == guardedBefore,
	             "malformed keys refuse atomically", error);
	auto opened = document;
	ok &= expect(openLevelSceneGroup(&opened, group, &error) &&
	                 setLevelProtectedEntityProperty(&opened, {corresponding(opened, group, identity)}, QStringLiteral("health"),
	                                                 {QStringLiteral("12")}, &error) &&
	                 !value(opened, corresponding(opened, copy, identity), QStringLiteral("health")),
	             "protected edit inside an open group retains safe propagation", error);

	{
		auto turned = document;
		const auto roots = levelLinkedContent(turned, levelSceneNode(turned.scene, copy)->objects).roots;
		ok &= expect(setLevelMapSelection(&turned, roots, &error) && rotateLevelMapSelection(&turned, 2, 1, &error) &&
		                 !levelLinkFrameText(levelSceneNode(turned.scene, copy)->linkFrame).isEmpty(),
		             "rotate a complete protected instance", error);
		const auto savedOrigin = value(turned, corresponding(turned, copy, identity), "origin");
		const auto savedAngle = value(turned, corresponding(turned, copy, identity), "angle");
		ok &= expect(setLevelMapEntityProperty(&turned, corresponding(turned, group, identity), "style", "17", &error) &&
		                 value(turned, corresponding(turned, copy, identity), "origin") == savedOrigin &&
		                 value(turned, corresponding(turned, copy, identity), "angle") == savedAngle,
		             "protected spatial keys survive the rotated instance transform", error);
		const auto doorIdentity = levelSceneNode(initial.scene, group)->linkObjects.value("entity:1");
		ok &= expect(setLevelProtectedEntityProperty(&turned, {corresponding(turned, copy, doorIdentity)}, "speed", {"99"}, &error) &&
		                 value(turned, corresponding(turned, group, doorIdentity), "speed") == "160" &&
		                 value(turned, corresponding(turned, copy, doorIdentity), "speed") == "99",
		             "brush entity properties use ownership correspondence", error);
	}
	{
		auto portable = document;
		ok &= expect(
		    setLevelProtectedEntityProperty(&portable, {corresponding(portable, group, identity)}, "targetname", {"local_relay"}, &error),
		    "protected prefab target name", error);
		auto roots = levelLinkedContent(portable, levelSceneNode(portable.scene, group)->objects).roots;
		roots += levelLinkedContent(portable, levelSceneNode(portable.scene, copy)->objects).roots;
		setLevelMapSelection(&portable, roots);
		LevelPrefab prefab;
		LevelPrefabCreateRequest create;
		create.name = "Protected assembly";
		LevelMapDocument definition, inserted;
		ok &= expect(createLevelPrefab(portable, create, &prefab, nullptr, &error) &&
		                 inspectLevelPrefab(prefab, &definition, nullptr, &error) &&
		                 definition.scene.protectedProperties.size() == portable.scene.protectedProperties.size() &&
		                 createLevelMap(request, &inserted, &error) && insertLevelPrefab(&inserted, prefab, {}, nullptr, &error) &&
		                 inserted.scene.protectedProperties.size() == portable.scene.protectedProperties.size() &&
		                 validateLevelScene(inserted, inserted.scene, &error),
		             "prefabs transfer protected flags with a complete linked family", error);
		LevelMapDocument again;
		ok &= expect(roundtrip(inserted, &again, &error), "inserted protected prefab round trips", error);
		bool protectedName = false;
		for (const auto& entity : inserted.entities) {
			const auto name = value(inserted, entity.id, "targetname");
			protectedName |= levelEntityPropertyProtected(inserted, entity.id, "targetname") && name && *name != "local_relay" &&
			                 name->endsWith("local_relay");
		}
		ok &= expect(protectedName, "prefab target names are namespaced while retaining their protection");
		const auto oldCount = standalone.scene.protectedProperties.size();
		ok &= expect(duplicateLevelMapObjects(&standalone, {{LevelMapSelectionKind::Entity, detachedId}}, 128, 0, 0, &error) &&
		                 standalone.scene.protectedProperties.size() == oldCount + 1,
		             "ordinary duplication remaps standalone protection", error);
	}
	{
		auto fresh = initial;
		QString created;
		ok &= expect(unlinkLevelGroup(&fresh, copy, &error) &&
		                 setLevelProtectedEntityProperty(&fresh, {2}, "targetname", {"local_relay"}, &error),
		             "unlinked protected source", error);
		const auto beforeCopy = serializeLevelMap(fresh).bytes;
		fresh.undoLimit = 1;
		ok &= expect(createLinkedLevelGroup(&fresh, group, {512, 0, 0, true}, &created, &error), "new linked copy from protected source",
		             error);
		const auto newIdentity = levelSceneNode(fresh.scene, group)->linkObjects.value("entity:2");
		const int newEntity = corresponding(fresh, created, newIdentity);
		ok &=
		    expect(levelEntityPropertyProtected(fresh, newEntity, "targetname") && value(fresh, newEntity, "targetname") == "local_relay" &&
		               undoLevelMapEdit(&fresh, &error) && serializeLevelMap(fresh).bytes == beforeCopy,
		           "first linked clone inherits protection and undoes atomically", error);
	}
	{
		auto legacy = initial;
		for (auto& node : legacy.scene.nodes) {
			node.linkObjects.clear();
		}
		const auto bytes = serializeLevelMap(legacy).bytes;
		const auto revision = legacy.revision;
		ok &= expect(!setLevelEntityPropertyProtection(&legacy, {2}, {"targetname"}, true, &error) && error.contains("correspondence") &&
		                 serializeLevelMap(legacy).bytes == bytes && legacy.revision == revision,
		             "legacy groups refuse protection without guessing correspondence", error);
		ok &= expect(updateLinkedLevelGroups(&legacy, group, nullptr, &error) &&
		                 setLevelEntityPropertyProtection(&legacy, {2}, {"targetname"}, true, &error),
		             "explicit update enables legacy protection", error);
	}
	{
		const QByteArray marker("\n// VibeStudioScene: ");
		const auto bytes = serializeLevelMap(document).bytes;
		const auto json =
		    QJsonDocument::fromJson(QByteArray::fromBase64(bytes.mid(bytes.lastIndexOf(marker) + marker.size()).trimmed())).object();
		const auto hash = QByteArray::fromHex(json.value("sha256").toString().toLatin1());
		LevelMapDocument native;
		ok &= expect(roundtrip(document, &native, &error), "codec fixture", error);
		const auto preserves = [&](QJsonObject invalid) {
			const auto encoded = QJsonDocument(invalid).toJson(QJsonDocument::Compact).toBase64();
			const auto decoded = decodeLevelScene(native, encoded, hash);
			return !decoded.problem.isEmpty() && decoded.opaqueMetadata == encoded && decoded.protectedProperties.isEmpty();
		};
		const auto validFlags = json.value("protectedProperties").toObject();
		const auto object = validFlags.keys().first();
		for (const auto& flags : QVector<QJsonObject>{{{object, QJsonArray{}}},
		                                              {{object, QJsonArray{"origin", "origin"}}},
		                                              {{object, QJsonArray{"ORIGIN"}}},
		                                              {{object, QJsonArray{"origin", "angle"}}},
		                                              {{"entity:0", QJsonArray{"origin"}}},
		                                              {{"entity:99999", QJsonArray{"origin"}}},
		                                              {{object, QJsonArray{42}}}}) {
			auto invalid = json;
			invalid.insert("protectedProperties", flags);
			ok &= expect(preserves(invalid), "malformed version six metadata stays opaque");
		}
		auto future = json;
		future.insert("version", 7);
		ok &= expect(preserves(future), "future schema is preserved without attaching protection");
		auto failed = document;
		const auto revision = failed.revision;
		ok &= expect(!setLevelProtectedEntityProperty(&failed, {original()}, "unsafe", {"bad\nvalue"}, &error) &&
		                 failed.revision == revision && serializeLevelMap(failed).bytes == bytes,
		             "invalid protected value rolls back metadata, history and native data", error);
	}
	{
		auto bounded = initial;
		const auto stable = serializeLevelMap(bounded).bytes;
		const auto revision = bounded.revision, depth = static_cast<quint64>(bounded.undoStack.size());
		QStringList manyKeys;
		for (int i = 0; i < 4000; ++i) {
			manyKeys << QStringLiteral("key%1_").arg(i) + QString(1000, QLatin1Char('a'));
		}
		ok &= expect(!setLevelEntityPropertyProtection(&bounded, {2}, manyKeys, true, &error) && error.contains("4 MiB") &&
		                 serializeLevelMap(bounded).bytes == stable && bounded.revision == revision && bounded.undoStack.size() == depth,
		             "oversized protection is refused before publishing document or history", error);
	}
	if (argc == 2) {
		QTemporaryDir temp;
		if (!temp.isValid()) {
			return 1;
		}
		const auto input = temp.filePath("input.map"), output = temp.filePath("protected.map");
		QFile file(input);
		if (!file.open(QIODevice::WriteOnly)) {
			return 1;
		}
		file.write(serializeLevelMap(initial).bytes);
		file.close();
		const auto read = [](const QString& path) {
			QFile file(path);
			return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
		};
		const auto run = [&](const QStringList& arguments, bool success = true) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(QString::fromLocal8Bit(argv[1]),
			              QStringList{"--cli", "--json", "--settings-file", temp.filePath("cli.ini"), "editor", "scene"} + arguments);
			const bool done = process.waitForStarted() && process.waitForFinished(30000);
			const auto out = process.readAllStandardOutput(), err = process.readAllStandardError();
			ok &= expect(done && process.exitStatus() == QProcess::NormalExit && (process.exitCode() == 0) == success, "protected CLI exit",
			             QString::fromUtf8((out + err).left(1500)));
			if (!done) {
				process.kill();
				process.waitForFinished();
			}
			return QJsonDocument::fromJson(out).object();
		};
		const QStringList set{
		    "set-protected-property", input, "--objects", "entity:2", "--key", "targetname", "--value", "cli_local", "--output", output};
		const auto dry = run(set + QStringList{"--dry-run"});
		ok &= expect(dry.value("dryRun").toBool() && !QFile::exists(output), "protected CLI dry run publishes nothing");
		run(set);
		LevelMapDocument saved;
		ok &= expect(loadLevelMap({output, {}, {}}, &saved, &error) &&
		                 value(saved, corresponding(saved, group, identity), "targetname") == "cli_local" &&
		                 value(saved, corresponding(saved, copy, identity), "targetname") == "relay",
		             "CLI persists a protected local value", error);
		const auto listing = run({"list", output}).value("scene").toObject();
		ok &= expect(listing.value("version").toInt() == 6 && listing.value("protectedProperties").toObject().size() == 1,
		             "CLI diagnostics expose protection schema");
		run({"protect-properties", output, "--objects", "entity:2", "--keys", "optional,origin", "--protected", "true", "--context", group,
		     "--output", output, "--overwrite"});
		run({"set-protected-property", output, "--objects", "entity:2", "--key", "optional", "--value=", "--output", output,
		     "--overwrite"});
		ok &= expect(loadLevelMap({output, {}, {}}, &saved, &error) && value(saved, 2, "optional") == QString(),
		             "CLI distinguishes empty value from missing argument", error);
		const auto stable = read(output);
		run(set, false);
		run({"protect-properties", output, "--objects", "entity:2", "--keys", "origin", "--protected", "yes", "--output", output,
		     "--overwrite"},
		    false);
		run({"protect-properties", output, "--objects", "brush:0", "--keys", "origin", "--protected", "true", "--output", output,
		     "--overwrite"},
		    false);
		run({"set-protected-property", output, "--objects", "entity:0", "--key", "message", "--value", "bad", "--output", output,
		     "--overwrite"},
		    false);
		run({"set-protected-property", output, "--objects", "entity:2", "--key", "optional", "--value", "--output", output, "--overwrite"},
		    false);
		run({"clear-protected-properties", output, "--objects", "entity:2", "--keys", "origin", "--output", output, "--overwrite"}, false);
		ok &= expect(read(output) == stable, "invalid CLI operations preserve the existing output");
		run({"protect-properties", output, "--objects", "entity:2", "--keys", "targetname", "--protected", "false", "--output", output,
		     "--overwrite"});
		run({"clear-protected-properties", output, "--objects", "entity:2", "--output", output, "--overwrite"});
		ok &= expect(loadLevelMap({output, {}, {}}, &saved, &error) && saved.scene.protectedProperties.isEmpty() &&
		                 value(saved, 2, "targetname") == "relay",
		             "CLI unprotect and clear restore shared values", error);
	}

	if (ok) {
		std::cout << "Protected entity property propagation, undo, locks and persistence passed.\n";
	}
	return ok ? 0 : 1;
}
