// TrenchBroom's layers, groups and linked groups in the map file. A map
// written the way TrenchBroom writes one (hand-written here, no game data)
// opens with its layer, its two linked groups, their frames and a protected
// key in the scene, and nothing of TrenchBroom's in the document; an edit to
// one linked group reaches the other in its frame; saving with TrenchBroom
// groups writes them back, and opening that again, with or without
// VibeStudio's scene note, gives the same scene and the same bytes. Maps
// without TrenchBroom's keys save as before; members that no longer match are
// marked out of sync; a stale note gives way to the keys; a layout VibeStudio
// cannot take apart opens as it is; and the CLI lists and converts the same.

#include "core/level_document.h"
#include "core/level_linked_groups.h"
#include "core/level_map.h"
#include "core/level_scene.h"
#include "core/level_scene_trenchbroom.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << '\n';
	}
	return value;
}

// An axis-aligned box as TrenchBroom writes one in the standard Quake format,
// its faces' points mapped by `frame` (3x4, row by row).
QString box(const QString& texture, double x0, double y0, double z0, double x1, double y1, double z1,
	const std::array<double, 12>& frame = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0})
{
	const auto point = [&frame](double x, double y, double z) {
		const auto at = [&](int row) { return frame[row * 4] * x + frame[row * 4 + 1] * y + frame[row * 4 + 2] * z + frame[row * 4 + 3]; };
		return QStringLiteral("( %1 %2 %3 )").arg(at(0)).arg(at(1)).arg(at(2));
	};
	const QString tail = QLatin1Char(' ') + texture + QStringLiteral(" 0 0 0 1 1");
	return QStringLiteral("{\n")
		+ point(x0, y0, z0) + QLatin1Char(' ') + point(x0, y0 + 1, z0) + QLatin1Char(' ') + point(x0, y0, z0 + 1) + tail + QLatin1Char('\n')
		+ point(x0, y0, z0) + QLatin1Char(' ') + point(x0, y0, z0 + 1) + QLatin1Char(' ') + point(x0 + 1, y0, z0) + tail + QLatin1Char('\n')
		+ point(x0, y0, z0) + QLatin1Char(' ') + point(x0 + 1, y0, z0) + QLatin1Char(' ') + point(x0, y0 + 1, z0) + tail + QLatin1Char('\n')
		+ point(x1, y1, z1) + QLatin1Char(' ') + point(x1, y1 + 1, z1) + QLatin1Char(' ') + point(x1 + 1, y1, z1) + tail + QLatin1Char('\n')
		+ point(x1, y1, z1) + QLatin1Char(' ') + point(x1 + 1, y1, z1) + QLatin1Char(' ') + point(x1, y1, z1 + 1) + tail + QLatin1Char('\n')
		+ point(x1, y1, z1) + QLatin1Char(' ') + point(x1, y1, z1 + 1) + QLatin1Char(' ') + point(x1, y1 + 1, z1) + tail + QLatin1Char('\n')
		+ QStringLiteral("}\n");
}

// The second pillar: the first turned a quarter turn anticlockwise about z
// and moved 256 units along x.
constexpr std::array<double, 12> kTurned {0, -1, 0, 256, 1, 0, 0, 0, 0, 0, 1, 0};

// A Quake map as TrenchBroom writes it: a Details layer holding a brush and
// two linked groups, each a pillar and a light; the second light's "light" is
// protected.
QString trenchBroomMap(double secondPillarTop = 128)
{
	return QStringLiteral("// Game: Quake\n// Format: Standard\n// entity 0\n{\n\"classname\" \"worldspawn\"\n\"wad\" \"studio.wad\"\n// brush 0\n")
		+ box(QStringLiteral("studio/floor"), -512, -512, -16, 512, 512, 0)
		+ QStringLiteral("}\n// entity 1\n{\n\"classname\" \"func_group\"\n\"_tb_type\" \"_tb_layer\"\n\"_tb_name\" \"Details\"\n\"_tb_id\" \"1\"\n"
						 "\"_tb_layer_sort_index\" \"0\"\n\"_tb_layer_color\" \"0.2 0.4 0.6\"\n\"_tb_layer_omit_from_export\" \"1\"\n// brush 0\n")
		+ box(QStringLiteral("studio/trim"), -64, -64, 0, -32, -32, 32)
		+ QStringLiteral("}\n// entity 2\n{\n\"classname\" \"func_group\"\n\"_tb_type\" \"_tb_group\"\n\"_tb_name\" \"pillar\"\n\"_tb_id\" \"2\"\n"
						 "\"_tb_layer\" \"1\"\n\"_tb_linked_group_id\" \"{38B3B39D-A165-4999-985D-D40563CE51C1}\"\n"
						 "\"_tb_transformation\" \"1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\"\n// brush 0\n")
		+ box(QStringLiteral("studio/pillar"), 0, 0, 0, 32, 64, 128)
		+ QStringLiteral("}\n// entity 3\n{\n\"classname\" \"light\"\n\"origin\" \"16 16 96\"\n\"light\" \"200\"\n\"_tb_group\" \"2\"\n}\n"
						 "// entity 4\n{\n\"classname\" \"func_group\"\n\"_tb_type\" \"_tb_group\"\n\"_tb_name\" \"pillar\"\n\"_tb_id\" \"3\"\n"
						 "\"_tb_layer\" \"1\"\n\"_tb_linked_group_id\" \"{38b3b39d-a165-4999-985d-d40563ce51c1}\"\n"
						 "\"_tb_transformation\" \"0 -1 0 256 1 0 0 0 0 0 1 0 0 0 0 1\"\n// brush 0\n")
		+ box(QStringLiteral("studio/pillar"), 0, 0, 0, 32, 64, secondPillarTop, kTurned)
		+ QStringLiteral("}\n// entity 5\n{\n\"classname\" \"light\"\n\"origin\" \"240 16 96\"\n\"light\" \"300\"\n\"_tb_group\" \"3\"\n"
						 "\"_tb_protected_properties\" \"light\"\n}\n");
}

const LevelSceneNode* named(const LevelMapDocument& document, const QString& name)
{
	for (const LevelSceneNode& node : document.scene.nodes) {
		if (node.name == name) {
			return &node;
		}
	}
	return nullptr;
}

const LevelMapBrush* brushIn(const LevelMapDocument& document, const LevelSceneNode* group)
{
	if (group) {
		for (const QString& member : group->objects) {
			for (const LevelMapBrush& brush : document.brushes) {
				if (member == QStringLiteral("brush:%1").arg(brush.id)) {
					return &brush;
				}
			}
		}
	}
	return nullptr;
}

const LevelMapEntity* entityIn(const LevelMapDocument& document, const LevelSceneNode* group)
{
	if (group) {
		for (const QString& member : group->objects) {
			for (const LevelMapEntity& entity : document.entities) {
				if (member == QStringLiteral("entity:%1").arg(entity.id)) {
					return &entity;
				}
			}
		}
	}
	return nullptr;
}

QString property(const LevelMapEntity* entity, const QString& key)
{
	if (entity) {
		for (const LevelMapProperty& each : entity->properties) {
			if (each.key == key) {
				return each.value;
			}
		}
	}
	return {};
}

bool near(double a, double b)
{
	return std::abs(a - b) < 0.01;
}

bool frameIs(const LevelSceneNode* node, const std::array<double, 12>& frame)
{
	if (!node) {
		return false;
	}
	for (int index = 0; index < 12; ++index) {
		if (!near(node->linkFrame[index], frame[index])) {
			return false;
		}
	}
	return true;
}

bool hasTrenchBroomKey(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.className.compare(QStringLiteral("func_group"), Qt::CaseInsensitive) == 0) {
			return true;
		}
		for (const LevelMapProperty& each : entity.properties) {
			if (each.key == QStringLiteral("_tb_group") || each.key == QStringLiteral("_tb_layer") || each.key == QStringLiteral("_tb_protected_properties")) {
				return true;
			}
		}
	}
	return false;
}

int issues(const LevelMapDocument& document, const QString& code)
{
	int count = 0;
	for (const LevelMapIssue& issue : document.issues) {
		count += issue.code == code ? 1 : 0;
	}
	return count;
}

bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray body(const QByteArray& bytes)
{
	const auto at = bytes.lastIndexOf("\n// VibeStudioScene: ");
	return at < 0 ? bytes : bytes.left(at);
}

// A scene with each node's members in one order: the note keeps the order
// edits left them in, TrenchBroom's keys give file order.
LevelSceneState sorted(LevelSceneState state)
{
	for (LevelSceneNode& node : state.nodes) {
		node.objects.sort();
	}
	return state;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	QTemporaryDir temp;
	const QString source = temp.filePath(QStringLiteral("trenchbroom.map"));
	ok &= expect(write(source, trenchBroomMap().toUtf8()), "fixture written");

	LevelSceneStorage storage = LevelSceneStorage::VibeStudio;
	ok &= expect(levelSceneStorageFromId(QStringLiteral("TrenchBroom"), &storage) && storage == LevelSceneStorage::TrenchBroom
			&& levelSceneStorageId(storage) == QStringLiteral("trenchbroom") && !levelSceneStorageFromId(QStringLiteral("radiant"), &storage),
		"storage ids");

	// Opening: the scene comes from TrenchBroom's keys, and the document holds
	// none of them.
	LevelMapDocument map;
	ok &= expect(loadLevelMap({source, {}, {}}, &map, &error), "TrenchBroom map opens", error);
	const LevelSceneNode* layer = named(map, QStringLiteral("Details"));
	const LevelSceneNode* first = named(map, QStringLiteral("pillar"));
	const LevelSceneNode* second = named(map, QStringLiteral("pillar 2"));
	ok &= expect(map.sceneStorage == LevelSceneStorage::TrenchBroom && map.scene.problem.isEmpty() && map.scene.nodes.size() == 3 && layer && first
			&& second,
		"the layer and both groups open as scene nodes; the later sibling is numbered",
		QString::fromUtf8(QJsonDocument(levelSceneJson(map)).toJson(QJsonDocument::Compact)));
	if (!layer || !first || !second) {
		return 1;
	}
	const QString firstId = first->id;
	const QString secondId = second->id;
	ok &= expect(layer->kind == LevelSceneNodeKind::Layer && first->parentId == layer->id && second->parentId == layer->id
			&& layer->trenchBroomProperties.value(QStringLiteral("_tb_layer_color")) == QStringLiteral("0.2 0.4 0.6")
			&& layer->trenchBroomProperties.value(QStringLiteral("_tb_layer_omit_from_export")) == QStringLiteral("1")
			&& second->trenchBroomProperties.value(QStringLiteral("_tb_name")) == QStringLiteral("pillar"),
		"groups sit in the layer, which keeps the keys VibeStudio does not edit");
	ok &= expect(first->linkId == QStringLiteral("38b3b39d-a165-4999-985d-d40563ce51c1") && second->linkId == first->linkId
			&& frameIs(first, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}) && frameIs(second, kTurned) && !first->linkOutOfSync && !second->linkOutOfSync,
		"the link id is canonical and frames come from _tb_transformation", second->linkId);
	ok &= expect(first->linkObjects.size() == 2 && second->linkObjects.size() == 2
			&& QSet<QString>(first->linkObjects.cbegin(), first->linkObjects.cend()) == QSet<QString>(second->linkObjects.cbegin(), second->linkObjects.cend()),
		"corresponding objects share identities, as after Update Linked Groups");
	const LevelMapEntity* secondLight = entityIn(map, second);
	ok &= expect(secondLight && map.scene.protectedProperties.value(QStringLiteral("entity:%1").arg(secondLight->id)) == QStringList {QStringLiteral("light")},
		"the protected key is read");
	ok &= expect(!hasTrenchBroomKey(map) && map.entities.size() == 3 && map.brushes.size() == 4 && layer->objects.size() == 1
			&& issues(map, QStringLiteral("trenchbroom-groups")) == 0,
		"the layer and group entities are gone and their brushes are in worldspawn");
	LevelMapDocument again;
	ok &= expect(loadLevelMap({source, {}, {}}, &again, &error) && again.scene == map.scene, "every open gives the same scene", error);

	// An edit to one linked group reaches the other, in its frame; the
	// protected key keeps its value.
	const LevelMapBrush* pillar = brushIn(map, first);
	const LevelMapBrush* turned = brushIn(map, second);
	const LevelMapVec3 turnedBefore = turned ? turned->mins : LevelMapVec3 {};
	ok &= expect(pillar && turned && setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, pillar->id}}, &error)
			&& moveLevelMapSelection(&map, 8, 0, 0, &error),
		"move the first pillar", error);
	turned = brushIn(map, levelSceneNode(map.scene, secondId));
	ok &= expect(turned && near(turned->mins.x, turnedBefore.x) && near(turned->mins.y, turnedBefore.y + 8) && near(turned->mins.z, turnedBefore.z),
		"the turned pillar moves along y", turned ? QStringLiteral("%1 %2 %3").arg(turned->mins.x).arg(turned->mins.y).arg(turned->mins.z) : QString());
	const LevelMapEntity* firstLight = entityIn(map, levelSceneNode(map.scene, firstId));
	ok &= expect(firstLight && setLevelMapEntityProperty(&map, firstLight->id, QStringLiteral("light"), QStringLiteral("250"), &error)
			&& setLevelMapEntityProperty(&map, entityIn(map, levelSceneNode(map.scene, firstId))->id, QStringLiteral("_color"), QStringLiteral("1 0 0"), &error),
		"edit the first light", error);
	secondLight = entityIn(map, levelSceneNode(map.scene, secondId));
	ok &= expect(property(secondLight, QStringLiteral("light")) == QStringLiteral("300") && property(secondLight, QStringLiteral("_color")) == QStringLiteral("1 0 0"),
		"the protected light keeps its value; the other key reaches it", property(secondLight, QStringLiteral("light")));

	// Saving writes TrenchBroom's keys back, with the scene note after them.
	const QString saved = temp.filePath(QStringLiteral("saved.map"));
	LevelDocumentSaveRequest save;
	save.path = saved;
	LevelMapSaveReport report = writeLevelDocument(map, save);
	const QByteArray bytes = read(saved);
	const QString text = QString::fromUtf8(body(bytes));
	ok &= expect(report.succeeded() && report.sceneStorage == LevelSceneStorage::TrenchBroom, "save with TrenchBroom groups", report.errors.join('\n'));
	ok &= expect(text.count(QStringLiteral("\"_tb_type\" \"_tb_group\"")) == 2 && text.count(QStringLiteral("\"_tb_type\" \"_tb_layer\"")) == 1
			&& text.count(QStringLiteral("\"_tb_name\" \"pillar\"")) == 2 && text.contains(QStringLiteral("\"_tb_id\" \"3\""))
			&& text.count(QStringLiteral("\"_tb_linked_group_id\" \"{38b3b39d-a165-4999-985d-d40563ce51c1}\"")) == 2
			&& text.contains(QStringLiteral("\"_tb_transformation\" \"0 -1 0 256 1 0 0 0 0 0 1 0 0 0 0 1\""))
			&& text.contains(QStringLiteral("\"_tb_layer_omit_from_export\" \"1\"")) && text.contains(QStringLiteral("\"_tb_protected_properties\" \"light\""))
			&& text.count(QStringLiteral("\"_tb_group\" \"")) == 2 && text.count(QStringLiteral("\"_tb_layer\" \"1\"")) == 2,
		"the layer, both linked groups and the protected key are written for TrenchBroom", text);
	ok &= expect(bytes.contains("\n// VibeStudioScene: ") && report.warnings.isEmpty(), "the scene note follows", report.warnings.join('\n'));

	// Opening that again gives the same scene, whether VibeStudio's note is
	// there or TrenchBroom dropped it, and saving gives the same bytes.
	LevelMapDocument noted;
	ok &= expect(loadLevelMap({saved, {}, {}}, &noted, &error) && noted.sceneStorage == LevelSceneStorage::TrenchBroom && noted.scene.problem.isEmpty()
			&& issues(noted, QStringLiteral("trenchbroom-groups")) == 0,
		"the saved map opens with its note", error);
	const QString stripped = temp.filePath(QStringLiteral("stripped.map"));
	ok &= expect(write(stripped, body(bytes)), "note dropped");
	LevelMapDocument keysOnly;
	ok &= expect(loadLevelMap({stripped, {}, {}}, &keysOnly, &error) && keysOnly.sceneStorage == LevelSceneStorage::TrenchBroom, "the saved map opens without its note", error);
	ok &= expect(sorted(noted.scene) == sorted(keysOnly.scene) && levelSceneNode(noted.scene, firstId) && levelSceneNode(noted.scene, secondId),
		"note and keys give the same scene, with the same ids",
		QString::fromUtf8(QJsonDocument(levelSceneJson(noted)).toJson(QJsonDocument::Compact)) + QStringLiteral("\n")
			+ QString::fromUtf8(QJsonDocument(levelSceneJson(keysOnly)).toJson(QJsonDocument::Compact)));
	ok &= expect(serializeLevelMap(noted).bytes == bytes && body(serializeLevelMap(keysOnly).bytes) == body(bytes), "saving it again changes nothing",
		QString::fromUtf8(body(serializeLevelMap(keysOnly).bytes)));
	turned = brushIn(keysOnly, levelSceneNode(keysOnly.scene, secondId));
	ok &= expect(turned && near(turned->mins.y, turnedBefore.y + 8) && !levelSceneNode(keysOnly.scene, secondId)->linkOutOfSync, "the edit survives the round trip");

	// Saving the VibeStudio way writes none of TrenchBroom's keys, and the
	// scene, and the keys kept for TrenchBroom, come back from the note.
	const QString plainPath = temp.filePath(QStringLiteral("plain.map"));
	save.path = plainPath;
	save.sceneStorage = LevelSceneStorage::VibeStudio;
	report = writeLevelDocument(noted, save);
	const QByteArray plainBytes = read(plainPath);
	LevelMapDocument plain;
	ok &= expect(report.succeeded() && !body(plainBytes).contains("_tb_") && loadLevelMap({plainPath, {}, {}}, &plain, &error)
			&& plain.sceneStorage == LevelSceneStorage::VibeStudio && plain.scene.problem.isEmpty() && plain.scene.nodes.size() == 3
			&& named(plain, QStringLiteral("Details"))->trenchBroomProperties.value(QStringLiteral("_tb_layer_color")) == QStringLiteral("0.2 0.4 0.6"),
		"a VibeStudio save keeps the scene in its note alone", error);
	adoptLevelDocumentSave(&noted, report);
	ok &= expect(noted.sceneStorage == LevelSceneStorage::VibeStudio, "the save's choice stays with the document");

	// A map without TrenchBroom's keys saves as before; with TrenchBroom
	// groups, linked groups made in VibeStudio open in TrenchBroom's form.
	LevelMapCreateRequest create;
	create.game = QStringLiteral("quake");
	create.starterRoom = false;
	LevelMapDocument made;
	QString group;
	QString copy;
	ok &= expect(createLevelMap(create, &made, &error)
			&& addLevelMapBoxBrush(&made, {0, 0, 0, true}, {32, 32, 64, true}, QStringLiteral("studio/pillar"), nullptr, &error)
			&& createLevelSceneNode(&made, LevelSceneNodeKind::Group, QStringLiteral("Column"), {}, &group, &error)
			&& assignLevelSceneObjects(&made, group, {QStringLiteral("brush:%1").arg(made.brushes.last().id)}, &error)
			&& createLinkedLevelGroup(&made, group, {128, 0, 0, true}, &copy, &error),
		"linked groups made in VibeStudio", error);
	const QByteArray native = serializeLevelMap(made).bytes;
	ok &= expect(made.sceneStorage == LevelSceneStorage::VibeStudio && !native.contains("_tb_") && native.contains("\n// VibeStudioScene: "),
		"by default the scene is kept in the note alone");
	const QString madePath = temp.filePath(QStringLiteral("made.map"));
	ok &= expect(write(madePath, body(serializeLevelMap(made, LevelSceneStorage::TrenchBroom).bytes)), "made map written for TrenchBroom");
	LevelMapDocument madeAgain;
	ok &= expect(loadLevelMap({madePath, {}, {}}, &madeAgain, &error) && madeAgain.scene.nodes.size() == 2
			&& levelLinkedGroupNodes(madeAgain.scene, madeAgain.scene.nodes.first().id).size() == 2
			&& madeAgain.scene.nodes.first().linkId == levelSceneNode(made.scene, group)->linkId
			&& frameIs(&madeAgain.scene.nodes.last(), levelSceneNode(made.scene, copy)->linkFrame) && !madeAgain.scene.nodes.last().linkOutOfSync,
		"TrenchBroom's keys alone give the same linked groups back", error);

	// Members that no longer match the rest of their set are out of sync.
	const QString changed = temp.filePath(QStringLiteral("changed.map"));
	ok &= expect(write(changed, trenchBroomMap(160).toUtf8()), "changed fixture written");
	LevelMapDocument differing;
	ok &= expect(loadLevelMap({changed, {}, {}}, &differing, &error) && named(differing, QStringLiteral("pillar"))
			&& !named(differing, QStringLiteral("pillar"))->linkOutOfSync && named(differing, QStringLiteral("pillar 2"))->linkOutOfSync
			&& issues(differing, QStringLiteral("trenchbroom-groups")) == 1,
		"a linked group TrenchBroom left different is out of sync", error);

	// A note that no longer matches gives way to TrenchBroom's keys.
	QByteArray edited = bytes;
	edited.replace("\"light\" \"300\"", "\"light\" \"301\"");
	const QString stale = temp.filePath(QStringLiteral("stale.map"));
	ok &= expect(write(stale, edited), "stale fixture written");
	LevelMapDocument restored;
	ok &= expect(loadLevelMap({stale, {}, {}}, &restored, &error) && restored.scene.problem.isEmpty() && restored.scene.opaqueMetadata.isEmpty()
			&& restored.scene.nodes.size() == 3 && issues(restored, QStringLiteral("trenchbroom-groups")) == 1
			&& property(entityIn(restored, levelSceneNode(restored.scene, secondId)), QStringLiteral("light")) == QStringLiteral("301"),
		"a stale note is set aside and the keys are read", error);

	// A layout VibeStudio cannot take apart opens as it is.
	QString tangled = trenchBroomMap();
	tangled.replace(QStringLiteral("}\n// entity 3\n{\n"), QStringLiteral("} {\n"));
	const QString tangledPath = temp.filePath(QStringLiteral("tangled.map"));
	ok &= expect(write(tangledPath, tangled.toUtf8()), "tangled fixture written");
	LevelMapDocument asIs;
	ok &= expect(loadLevelMap({tangledPath, {}, {}}, &asIs, &error) && asIs.scene.nodes.isEmpty() && asIs.sceneStorage == LevelSceneStorage::VibeStudio
			&& hasTrenchBroomKey(asIs) && issues(asIs, QStringLiteral("trenchbroom-groups")) == 1 && serializeLevelMap(asIs).bytes == tangled.toUtf8(),
		"the map opens with TrenchBroom's entities as they are, and saves unchanged", error);

	// The CLI lists and converts through the same service.
	if (argc > 1) {
		const QString cli = QString::fromLocal8Bit(argv[1]);
		const auto run = [&](const QStringList& arguments) {
			QProcess process;
			process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"),
								   temp.filePath(QStringLiteral("cli.ini")), QStringLiteral("editor"), QStringLiteral("scene")}
					+ arguments);
			const bool ended = process.waitForStarted() && process.waitForFinished(60000);
			const QByteArray out = process.readAllStandardOutput();
			ok &= expect(ended && process.exitCode() == 0, "CLI run", QString::fromUtf8(out + process.readAllStandardError()));
			return QJsonDocument::fromJson(out).object();
		};
		const QJsonObject listed = run({QStringLiteral("list"), source});
		const QJsonObject scene = listed.value(QStringLiteral("scene")).toObject();
		const QJsonArray nodes = scene.value(QStringLiteral("nodes")).toArray();
		bool sameIds = nodes.size() == 3;
		for (int index = 0; sameIds && index < nodes.size(); ++index) {
			sameIds = nodes.at(index).toObject().value(QStringLiteral("id")).toString() == again.scene.nodes.at(index).id;
		}
		ok &= expect(scene.value(QStringLiteral("storage")).toString() == QStringLiteral("trenchbroom") && sameIds
				&& nodes.at(2).toObject().value(QStringLiteral("link")).toString() == QStringLiteral("38b3b39d-a165-4999-985d-d40563ce51c1"),
			"editor scene list shows the TrenchBroom scene with the same ids");
		const QString converted = temp.filePath(QStringLiteral("converted.map"));
		const QJsonObject native = run({QStringLiteral("save"), source, QStringLiteral("--scene-format"), QStringLiteral("vibestudio"), QStringLiteral("--output"), converted});
		const QString back = temp.filePath(QStringLiteral("back.map"));
		const QJsonObject trenchBroom = run({QStringLiteral("save"), converted, QStringLiteral("--scene-format"), QStringLiteral("trenchbroom"), QStringLiteral("--output"), back});
		const QJsonObject kept = run({QStringLiteral("rename"), back, QStringLiteral("--id"), firstId, QStringLiteral("--name"), QStringLiteral("column"),
			QStringLiteral("--output"), temp.filePath(QStringLiteral("renamed.map"))});
		ok &= expect(native.value(QStringLiteral("sceneStorage")).toString() == QStringLiteral("vibestudio") && !body(read(converted)).contains("_tb_")
				&& trenchBroom.value(QStringLiteral("sceneStorage")).toString() == QStringLiteral("trenchbroom")
				&& QString::fromUtf8(body(read(back))).count(QStringLiteral("\"_tb_type\" \"_tb_group\"")) == 2
				&& kept.value(QStringLiteral("sceneStorage")).toString() == QStringLiteral("trenchbroom")
				&& body(read(temp.filePath(QStringLiteral("renamed.map")))).contains("\"_tb_name\" \"column\""),
			"editor scene save converts both ways, and other changes keep the form a map opened with");
	}

	if (ok) {
		std::cout << "Level TrenchBroom groups smoke test passed.\n";
	}
	return ok ? 0 : 1;
}
