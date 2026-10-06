#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace vibestudio {
namespace {
QString message(const char* text) { return QCoreApplication::translate("LevelScene", text); }
bool fail(QString* error, const char* text) {
	if (error) {
		*error = message(text);
	}
	return false;
}
QString selector(const QString& kind, int id) { return kind + QLatin1Char(':') + QString::number(id); }
bool editable(const LevelMapDocument* document, QString* error) {
	if (error) {
		error->clear();
	}
	if (!document || document->format == LevelMapFormat::Unknown) {
		return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Open a map before organizing its scene."));
	}
	if (document->doomFormat == LevelMapDoomFormat::Udmf && !document->doomUdmf) {
		return fail(error, QT_TRANSLATE_NOOP("LevelScene", "UDMF scene editing is not supported yet."));
	}
	if (!document->scene.opaqueMetadata.isEmpty() || !document->scene.problem.isEmpty()) {
		return fail(error,
					QT_TRANSLATE_NOOP("LevelScene", "Reset the unrecognized or stale scene metadata before changing scene organization."));
	}
	return true;
}
int nodeIndex(const LevelSceneState& state, const QString& id) {
	for (int i = 0; i < state.nodes.size(); ++i) {
		if (state.nodes[i].id == id) {
			return i;
		}
	}
	return -1;
}
bool nodeRequired(const LevelMapDocument* document, const QString& id, int* index, QString* error) {
	if (!editable(document, error)) {
		return false;
	}
	*index = nodeIndex(document->scene, id);
	return *index >= 0 || fail(error, QT_TRANSLATE_NOOP("LevelScene", "The scene node no longer exists."));
}
QJsonArray nodesJson(const LevelSceneState& state) {
	QJsonArray array;
	for (const auto& node : state.nodes) {
		array.append(QJsonObject{
			{QStringLiteral("id"), node.id},
			{QStringLiteral("name"), node.name},
			{QStringLiteral("parent"), node.parentId},
			{QStringLiteral("kind"), node.kind == LevelSceneNodeKind::Layer ? QStringLiteral("layer") : QStringLiteral("group")},
			{QStringLiteral("visible"), node.visible},
			{QStringLiteral("locked"), node.locked},
			{QStringLiteral("objects"), QJsonArray::fromStringList(node.objects)}});
	}
	return array;
}
} // namespace

QStringList levelSceneObjects(const LevelMapDocument& document) {
	QStringList objects;
	const auto append = [&](const auto& records, const QString& kind) {
		for (const auto& record : records) {
			objects << selector(kind, record.id);
		}
	};
	if (document.format == LevelMapFormat::DoomWad) {
		append(document.doomVertices, QStringLiteral("vertex"));
		append(document.doomLinedefs, QStringLiteral("linedef"));
		append(document.doomThings, QStringLiteral("thing"));
		append(document.doomSectors, QStringLiteral("sector"));
	} else {
		for (const auto& entity : document.entities) {
			if (entity.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
				objects << selector(QStringLiteral("entity"), entity.id);
			}
		}
		append(document.brushes, QStringLiteral("brush"));
		append(document.patches, QStringLiteral("patch"));
	}
	return objects;
}

QString levelSceneCanonicalObject(const LevelMapDocument& document, const QString& object) {
	if (document.format == LevelMapFormat::DoomWad && object.startsWith(QStringLiteral("entity:"))) {
		return QStringLiteral("thing:") + object.mid(7);
	}
	return object;
}

const LevelSceneNode* levelSceneNode(const LevelSceneState& state, const QString& id) {
	const int index = nodeIndex(state, id);
	return index < 0 ? nullptr : &state.nodes[index];
}

QString levelSceneMembership(const LevelSceneState& state, const QString& object) {
	for (const auto& node : state.nodes) {
		if (node.objects.contains(object)) {
			return node.id;
		}
	}
	return {};
}

QStringList levelSceneMembers(const LevelMapDocument& document, const QString& nodeId, bool recursive) {
	QSet<QString> included{nodeId};
	if (recursive) {
		for (int depth = 0; depth < kLevelSceneMaxDepth; ++depth) {
			const auto before = included.size();
			for (const auto& node : document.scene.nodes) {
				if (node.kind == LevelSceneNodeKind::Group && included.contains(node.parentId)) {
					included.insert(node.id);
				}
			}
			if (before == included.size()) {
				break;
			}
		}
	}
	QSet<QString> assigned;
	QStringList result;
	for (const auto& node : document.scene.nodes) {
		for (const auto& object : node.objects) {
			assigned.insert(object);
		}
		if (included.contains(node.id)) {
			result += node.objects;
		}
	}
	if (nodeId.isEmpty()) {
		for (const auto& object : levelSceneObjects(document)) {
			if (!assigned.contains(object)) {
				result << object;
			}
		}
	}
	return result;
}

QSet<QString> levelSceneHiddenObjects(const LevelMapDocument& document) {
	QSet<QString> hiddenNodes;
	for (const auto& node : document.scene.nodes) {
		if (!node.visible) {
			hiddenNodes.insert(node.id);
		}
	}
	for (int depth = 0; depth < kLevelSceneMaxDepth; ++depth) {
		const auto before = hiddenNodes.size();
		for (const auto& node : document.scene.nodes) {
			if (hiddenNodes.contains(node.parentId)) {
				hiddenNodes.insert(node.id);
			}
		}
		if (before == hiddenNodes.size()) {
			break;
		}
	}
	QSet<QString> hidden;
	for (const auto& node : document.scene.nodes) {
		if (hiddenNodes.contains(node.id)) {
			for (const auto& object : node.objects) {
				hidden.insert(object);
			}
		}
	}
	// Hiding an entity also hides its owned primitives; showing a child does not
	// override a hidden parent. Explicit primitive memberships remain independent.
	if (hidden.isEmpty()) { return hidden; }
	QSet<int> hiddenEntities;
	for (const auto& object : std::as_const(hidden)) {
		if (!object.startsWith(QStringLiteral("entity:"))) { continue; }
		bool valid = false;
		const int id = object.mid(7).toInt(&valid);
		// Preserve exact selector matching, including for an unvalidated caller.
		if (valid && object == selector(QStringLiteral("entity"), id)) { hiddenEntities.insert(id); }
	}
	if (!hiddenEntities.isEmpty()) {
		for (const auto& brush : document.brushes) {
			if (hiddenEntities.contains(brush.entityId)) { hidden.insert(selector(QStringLiteral("brush"), brush.id)); }
		}
		for (const auto& patch : document.patches) {
			if (hiddenEntities.contains(patch.entityId)) { hidden.insert(selector(QStringLiteral("patch"), patch.id)); }
		}
	}
	if (document.format == LevelMapFormat::DoomWad && !hidden.isEmpty()) {
		QHash<int, int> sectors;
		for (const auto& side : document.doomSidedefs) {
			sectors.insert(side.id, side.sector);
		}
		QSet<int> usedVertices, visibleVertices;
		for (const auto& line : document.doomLinedefs) {
			bool hasSector = false, visibleSector = false;
			for (int side : {line.frontSidedef, line.backSidedef}) {
				if (sectors.contains(side)) {
					hasSector = true;
					visibleSector |= !hidden.contains(selector(QStringLiteral("sector"), sectors.value(side)));
				}
			}
			const auto id = selector(QStringLiteral("linedef"), line.id);
			if (hasSector && !visibleSector) {
				hidden.insert(id);
			}
			usedVertices.insert(line.startVertex);
			usedVertices.insert(line.endVertex);
			if (!hidden.contains(id)) {
				visibleVertices.insert(line.startVertex);
				visibleVertices.insert(line.endVertex);
			}
		}
		for (int vertex : usedVertices) {
			if (!visibleVertices.contains(vertex)) {
				hidden.insert(selector(QStringLiteral("vertex"), vertex));
			}
		}
	}
	return hidden;
}

QVector<LevelMapSelectionRef> levelSceneSelection(const LevelMapDocument& document, const QString& nodeId) {
	QVector<LevelMapSelectionRef> result;
	const auto hidden = levelSceneHiddenObjects(document);
	for (const auto& object : levelSceneMembers(document, nodeId)) {
		if (hidden.contains(object)) {
			continue;
		}
		const auto colon = object.indexOf(QLatin1Char(':'));
		result.push_back({levelMapSelectionKindFromId(object.left(colon)), object.mid(colon + 1).toInt()});
	}
	return result;
}

bool validateLevelScene(const LevelMapDocument& document, const LevelSceneState& state, QString* error) {
	if (error) {
		error->clear();
	}
	if (!state.opaqueMetadata.isEmpty() || !state.problem.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Unrecognized scene metadata cannot be applied as live scene organization."));
	}
	if (state.nodes.size() > kLevelSceneMaxNodes) {
		return fail(error, QT_TRANSLATE_NOOP("LevelScene", "A scene supports at most 1,000 layers and groups."));
	}
	QHash<QString, const LevelSceneNode*> nodes;
	QSet<QString> names;
	for (const auto& node : state.nodes) {
		const QUuid uuid(node.id);
		if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != node.id || nodes.contains(node.id)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Scene node identities must be unique canonical UUIDs."));
		}
		if (node.name.isEmpty() || node.name.size() > 128 || node.name != node.name.trimmed() ||
			std::any_of(node.name.cbegin(), node.name.cend(), [](QChar c) {
				return c.category() == QChar::Other_Control || c == QChar::ParagraphSeparator || c == QChar::LineSeparator;
			})) {
			return fail(
				error,
				QT_TRANSLATE_NOOP("LevelScene",
								  "Use a nonempty scene name of at most 128 characters without control characters or surrounding spaces."));
		}
		const QString key = node.parentId + QLatin1Char('/') + node.name.toCaseFolded();
		if (names.contains(key)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Sibling layers and groups must have distinct names."));
		}
		names.insert(key);
		nodes.insert(node.id, &node);
		if (node.kind != LevelSceneNodeKind::Layer && node.kind != LevelSceneNodeKind::Group) {
			return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Unknown scene node kind."));
		}
		if (node.kind == LevelSceneNodeKind::Layer && !node.parentId.isEmpty()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Layers must be at the scene root."));
		}
	}
	const auto all = levelSceneObjects(document);
	const QSet<QString> available(all.cbegin(), all.cend());
	QSet<QString> members;
	for (const auto& node : state.nodes) {
		QString at = node.id;
		QSet<QString> ancestors;
		while (!at.isEmpty()) {
			if (!nodes.contains(at) || ancestors.contains(at) || ancestors.size() >= kLevelSceneMaxDepth) {
				return fail(error,
							QT_TRANSLATE_NOOP("LevelScene", "Scene parents must exist, have no cycles, and nest no deeper than 32 nodes."));
			}
			ancestors.insert(at);
			at = nodes.value(at)->parentId;
		}
		for (const auto& object : node.objects) {
			if (!available.contains(object) || members.contains(object)) {
				return fail(
					error, QT_TRANSLATE_NOOP("LevelScene", "Every scene member must name an existing object and belong to only one node."));
			}
			members.insert(object);
			if (members.size() > kLevelSceneMaxMembers) {
				return fail(error, QT_TRANSLATE_NOOP("LevelScene", "A scene supports at most 100,000 explicit object memberships."));
			}
		}
	}
	return true;
}

bool createLevelSceneNode(LevelMapDocument* document, LevelSceneNodeKind kind, const QString& name, const QString& parentId,
						  QString* createdId, QString* error) {
	if (!editable(document, error)) {
		return false;
	}
	auto state = document->scene;
	LevelSceneNode node;
	node.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	node.name = name.trimmed();
	node.parentId = parentId;
	node.kind = kind;
	state.nodes << node;
	if (!commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelScene", "Create scene node")), error)) {
		return false;
	}
	if (createdId) {
		*createdId = node.id;
	}
	return true;
}

bool renameLevelSceneNode(LevelMapDocument* document, const QString& id, const QString& name, QString* error) {
	int index = -1;
	if (!nodeRequired(document, id, &index, error)) {
		return false;
	}
	auto state = document->scene;
	state.nodes[index].name = name.trimmed();
	return commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelScene", "Rename scene node")), error);
}

bool reparentLevelSceneNode(LevelMapDocument* document, const QString& id, const QString& parentId, QString* error) {
	int index = -1;
	if (!nodeRequired(document, id, &index, error)) {
		return false;
	}
	auto state = document->scene;
	state.nodes[index].parentId = parentId;
	return commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelScene", "Move scene node")), error);
}

bool assignLevelSceneObjects(LevelMapDocument* document, const QString& nodeId, const QStringList& objects, QString* error) {
	if (!editable(document, error)) {
		return false;
	}
	const int index = nodeIndex(document->scene, nodeId);
	if (!nodeId.isEmpty() && index < 0) {
		return fail(error, QT_TRANSLATE_NOOP("LevelScene", "The scene node no longer exists."));
	}
	const auto all = levelSceneObjects(*document);
	const QSet<QString> available(all.cbegin(), all.cend());
	QSet<QString> assigned;
	for (const auto& object : objects) {
		const auto canonical = levelSceneCanonicalObject(*document, object);
		if (!available.contains(canonical)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Choose existing map objects; worldspawn itself cannot be assigned."));
		}
		assigned.insert(canonical);
	}
	if (assigned.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Select objects to assign to the scene node."));
	}
	auto state = document->scene;
	for (auto& node : state.nodes) {
		node.objects.removeIf([&](const QString& object) { return assigned.contains(object); });
	}
	if (index >= 0) {
		QStringList ordered(assigned.cbegin(), assigned.cend());
		ordered.sort();
		state.nodes[index].objects += ordered;
	}
	return commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelScene", "Assign scene objects")), error);
}

bool setLevelSceneVisible(LevelMapDocument* document, const QString& id, bool visible, QString* error) {
	int index = -1;
	if (!nodeRequired(document, id, &index, error)) {
		return false;
	}
	auto state = document->scene;
	state.nodes[index].visible = visible;
	return commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelScene", "Change scene visibility")), error);
}

bool removeLevelSceneNode(LevelMapDocument* document, const QString& id, QString* error) {
	int index = -1;
	if (!nodeRequired(document, id, &index, error)) {
		return false;
	}
	auto state = document->scene;
	const auto removed = state.nodes.takeAt(index);
	for (auto& node : state.nodes) {
		if (node.parentId == id) {
			node.parentId = removed.parentId;
		}
		if (node.id == removed.parentId) {
			node.objects += removed.objects;
		}
	}
	return commitLevelSceneState(document, state, message(QT_TRANSLATE_NOOP("LevelScene", "Remove scene node")), error);
}

bool resetLevelScene(LevelMapDocument* document, QString* error) {
	if (!document) {
		return fail(error, QT_TRANSLATE_NOOP("LevelScene", "Open a map before organizing its scene."));
	}
	return commitLevelSceneState(document, {}, message(QT_TRANSLATE_NOOP("LevelScene", "Reset scene organization")), error);
}

LevelSceneState reconcileLevelScene(const LevelMapDocument& after, const LevelMapUndoCommand& command) {
	auto state = after.scene;
	if (state.nodes.isEmpty()) {
		return state;
	}
	const auto all = levelSceneObjects(after);
	const QSet<QString> available(all.cbegin(), all.cend());
	QHash<QString, QString> memberships;
	for (const auto& node : state.nodes) {
		for (const auto& object : node.objects) {
			memberships.insert(object, node.id);
		}
	}
	QHash<QString, QString> carried;
	for (auto it = memberships.cbegin(); it != memberships.cend(); ++it) {
		const auto now = command.sceneObjectRemap.value(it.key(), it.key());
		if (available.contains(now)) {
			carried.insert(now, it.value());
		}
	}
	for (auto it = command.sceneObjectOrigins.cbegin(); it != command.sceneObjectOrigins.cend(); ++it) {
		if (available.contains(it.key()) && memberships.contains(it.value())) {
			carried.insert(it.key(), memberships.value(it.value()));
		}
	}
	// New objects without an origin go to the current creation node. A copied
	// object with an explicit Default origin stays in Default.
	if (levelSceneNode(state, after.activeSceneNode)) {
		for (const auto& object : command.sceneAddedObjects) {
			if (available.contains(object) && !carried.contains(object) && !command.sceneObjectOrigins.contains(object)) {
				carried.insert(object, after.activeSceneNode);
			}
		}
	}
	for (auto& node : state.nodes) {
		node.objects.clear();
	}
	QHash<QString, int> indexes;
	for (int i = 0; i < state.nodes.size(); ++i) {
		indexes.insert(state.nodes[i].id, i);
	}
	for (const auto& object : all) {
		if (carried.contains(object)) {
			state.nodes[indexes.value(carried.value(object))].objects << object;
		}
	}
	return state;
}

QByteArray encodeLevelScene(const LevelMapDocument& document, const QByteArray& bodyHash, const QHash<QString, QString>& emittedObjects,
							QString* error) {
	if (error) {
		error->clear();
	}
	if (!document.scene.opaqueMetadata.isEmpty() || !document.scene.problem.isEmpty()) {
		return document.scene.opaqueMetadata;
	}
	if (document.scene.nodes.isEmpty()) {
		return {};
	}
	if (!validateLevelScene(document, document.scene, error)) {
		return {};
	}
	auto state = document.scene;
	QSet<QString> used;
	for (auto& node : state.nodes) {
		for (auto& object : node.objects) {
			const auto saved = emittedObjects.value(object);
			if (saved.isEmpty() || used.contains(saved)) {
				fail(error,
					 QT_TRANSLATE_NOOP("LevelScene", "Scene members could not be matched to the serialized map; the map was not saved."));
				return {};
			}
			object = saved;
			used.insert(saved);
		}
	}
	const QJsonObject json{{QStringLiteral("version"), 2},
						   {QStringLiteral("sha256"), QString::fromLatin1(bodyHash.toHex())},
						   {QStringLiteral("nodes"), nodesJson(state)}};
	const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact).toBase64();
	if (bytes.size() > kLevelSceneMaxMetadataBytes) {
		fail(error, QT_TRANSLATE_NOOP("LevelScene", "Scene metadata exceeds the 4 MiB limit."));
		return {};
	}
	return bytes;
}

LevelSceneState decodeLevelScene(const LevelMapDocument& document, const QByteArray& metadata, const QByteArray& bodyHash) {
	const auto preserve = [&](const char* reason) {
		LevelSceneState state;
		state.opaqueMetadata = metadata;
		state.problem = message(reason);
		return state;
	};
	if (metadata.size() > kLevelSceneMaxMetadataBytes) {
		return preserve(QT_TRANSLATE_NOOP("LevelScene", "Scene metadata exceeds 4 MiB; it is preserved without applying memberships."));
	}
	const auto decoded = QByteArray::fromBase64Encoding(metadata, QByteArray::AbortOnBase64DecodingErrors);
	QJsonParseError parseError;
	const auto json = QJsonDocument::fromJson(decoded.decoded, &parseError);
	if (!decoded || parseError.error != QJsonParseError::NoError || !json.isObject()) {
		return preserve(QT_TRANSLATE_NOOP("LevelScene", "Scene metadata is malformed; it is preserved without applying memberships."));
	}
	const auto root = json.object();
	const auto version = root.value(QStringLiteral("version"));
	if (root.size() != 3 || (version != QJsonValue(1) && version != QJsonValue(2)) || !root.value(QStringLiteral("nodes")).isArray()) {
		return preserve(
			QT_TRANSLATE_NOOP("LevelScene", "Scene metadata uses an unknown schema; it is preserved without applying memberships."));
	}
	if (root.value(QStringLiteral("sha256")).toString() != QString::fromLatin1(bodyHash.toHex())) {
		return preserve(QT_TRANSLATE_NOOP(
			"LevelScene",
			"The map changed outside VibeStudio; scene metadata is preserved without applying potentially stale memberships."));
	}
	const auto nodes = root.value(QStringLiteral("nodes")).toArray();
	if (nodes.size() > kLevelSceneMaxNodes) {
		return preserve(
			QT_TRANSLATE_NOOP("LevelScene", "Scene metadata contains too many nodes; it is preserved without applying memberships."));
	}
	LevelSceneState state;
	int members = 0;
	for (const auto& value : nodes) {
		const auto object = value.toObject();
		if (object.size() != (version == QJsonValue(1) ? 6 : 7) ||
			(version == QJsonValue(2) && !object.value(QStringLiteral("locked")).isBool()) ||
			!object.value(QStringLiteral("id")).isString() || !object.value(QStringLiteral("name")).isString() ||
			!object.value(QStringLiteral("parent")).isString() || !object.value(QStringLiteral("visible")).isBool() ||
			!object.value(QStringLiteral("objects")).isArray() ||
			(object.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("layer")) &&
			 object.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("group")))) {
			return preserve(
				QT_TRANSLATE_NOOP("LevelScene", "Scene nodes use an unknown schema; metadata is preserved without applying memberships."));
		}
		LevelSceneNode node;
		node.id = object.value(QStringLiteral("id")).toString();
		node.name = object.value(QStringLiteral("name")).toString();
		node.parentId = object.value(QStringLiteral("parent")).toString();
		node.kind = object.value(QStringLiteral("kind")).toString() == QStringLiteral("layer") ? LevelSceneNodeKind::Layer
																							   : LevelSceneNodeKind::Group;
		node.visible = object.value(QStringLiteral("visible")).toBool();
		node.locked = object.value(QStringLiteral("locked")).toBool(false);
		for (const auto& member : object.value(QStringLiteral("objects")).toArray()) {
			if (!member.isString() || ++members > kLevelSceneMaxMembers) {
				return preserve(QT_TRANSLATE_NOOP(
					"LevelScene",
					"Scene membership data is invalid or exceeds its limit; metadata is preserved without applying memberships."));
			}
			node.objects << member.toString();
		}
		state.nodes << node;
	}
	QString error;
	if (!validateLevelScene(document, state, &error)) {
		state =
			preserve(QT_TRANSLATE_NOOP("LevelScene", "Scene organization is invalid; metadata is preserved without applying memberships."));
		state.problem += QLatin1Char(' ') + error;
	}
	return state;
}

QByteArray levelSceneDoomHash(const QMap<QString, QByteArray>& lumps) {
	QCryptographicHash hash(QCryptographicHash::Sha256);
	for (const auto& name : {"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SECTORS", "BEHAVIOR", "TEXTMAP"}) {
		const auto bytes = lumps.value(QString::fromLatin1(name));
		hash.addData(QByteArray(name) + ':' + QByteArray::number(bytes.size()) + ':');
		hash.addData(bytes);
	}
	return hash.result();
}

QJsonObject levelSceneJson(const LevelMapDocument& document) {
	auto nodes = nodesJson(document.scene);
	const auto locked = levelSceneLockedNodes(document.scene);
	for (auto value = nodes.begin(); value != nodes.end(); ++value) {
		auto object = value->toObject();
		object.insert(QStringLiteral("effectiveLocked"), locked.contains(object.value(QStringLiteral("id")).toString()));
		*value = object;
	}
	return {{QStringLiteral("version"), 2},
			{QStringLiteral("nodes"), nodes},
			{QStringLiteral("problem"), document.scene.problem},
			{QStringLiteral("preservedMetadataBytes"), document.scene.opaqueMetadata.size()},
			{QStringLiteral("defaultObjects"), QJsonArray::fromStringList(levelSceneMembers(document, {}, false))}};
}
} // namespace vibestudio
