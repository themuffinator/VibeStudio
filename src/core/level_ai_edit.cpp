#include "core/level_ai_edit.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr double kLargestCoordinate = 32768.0;
constexpr int kMaxActions = 64;

QJsonObject closedObject(const QJsonObject& properties)
{
	QJsonArray required;
	for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
		required.append(it.key());
	}
	return QJsonObject {
		{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), properties},
		{QStringLiteral("required"), required},
		{QStringLiteral("additionalProperties"), false},
	};
}

QJsonObject typed(const QString& type, const QString& description)
{
	return QJsonObject {{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

QJsonObject vectorSchema(const QString& description)
{
	return QJsonObject {
		{QStringLiteral("type"), QStringLiteral("array")},
		{QStringLiteral("items"), QJsonObject {{QStringLiteral("type"), QStringLiteral("number")}}},
		{QStringLiteral("maxItems"), 3},
		{QStringLiteral("description"), description},
	};
}

QString number(double value)
{
	return value == std::floor(value) ? QString::number(qint64(value)) : QString::number(value, 'f', 2);
}

QString vectorText(const LevelMapVec3& vector)
{
	return QStringLiteral("(%1 %2 %3)").arg(number(vector.x), number(vector.y), number(vector.z));
}

LevelMapVec3 vectorFrom(const QJsonValue& value)
{
	const QJsonArray array = value.toArray();
	if (array.size() != 3) {
		return {};
	}
	return {array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble(), true};
}

QJsonArray vectorJson(const LevelMapVec3& vector)
{
	return vector.valid ? QJsonArray {vector.x, vector.y, vector.z} : QJsonArray();
}

bool vectorUsable(const LevelMapVec3& vector)
{
	return vector.valid && std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z) && std::abs(vector.x) <= kLargestCoordinate
		&& std::abs(vector.y) <= kLargestCoordinate && std::abs(vector.z) <= kLargestCoordinate;
}

bool cleanText(const QString& text)
{
	return !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n')) && !text.contains(QLatin1Char('\r'));
}

const LevelMapEntity* entityById(const LevelMapDocument& document, int id)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (entity.id == id) {
			return &entity;
		}
	}
	return nullptr;
}

bool hasProperty(const LevelMapEntity& entity, const QString& key)
{
	return std::any_of(entity.properties.cbegin(), entity.properties.cend(), [&key](const LevelMapProperty& property) { return property.key == key; });
}

// A brush entity (or worldspawn): it has brushes or patches to texture.
bool ownsGeometry(const LevelMapDocument& document, int entityId)
{
	return std::any_of(document.brushes.cbegin(), document.brushes.cend(), [entityId](const LevelMapBrush& brush) { return brush.entityId == entityId; })
		|| std::any_of(document.patches.cbegin(), document.patches.cend(), [entityId](const LevelMapPatch& patch) { return patch.entityId == entityId; });
}

// A selector the summary listed, resolved; kind None when it names nothing.
LevelMapSelectionRef resolveSelector(const LevelMapDocument& document, const QString& selector)
{
	const qsizetype colon = selector.indexOf(QLatin1Char(':'));
	if (colon <= 0) {
		return {};
	}
	const LevelMapSelectionKind kind = levelMapSelectionKindFromId(selector.left(colon).trimmed());
	bool ok = false;
	const int id = selector.mid(colon + 1).trimmed().toInt(&ok);
	if (!ok) {
		return {};
	}
	switch (kind) {
	case LevelMapSelectionKind::Entity:
		return entityById(document, id) ? LevelMapSelectionRef {kind, id} : LevelMapSelectionRef();
	case LevelMapSelectionKind::QuakeBrush:
		for (const LevelMapBrush& brush : document.brushes) {
			if (brush.id == id) {
				return {kind, id};
			}
		}
		return {};
	case LevelMapSelectionKind::QuakePatch:
		for (const LevelMapPatch& patch : document.patches) {
			if (patch.id == id) {
				return {kind, id};
			}
		}
		return {};
	default:
		return {};
	}
}

} // namespace

QStringList levelAiEditActionKinds()
{
	return {
		QStringLiteral("add-entity"),
		QStringLiteral("set-key"),
		QStringLiteral("remove-key"),
		QStringLiteral("add-box"),
		QStringLiteral("set-texture"),
		QStringLiteral("move"),
		QStringLiteral("delete"),
	};
}

QJsonObject levelAiEditSchema()
{
	const QJsonObject keyValue = closedObject({
		{QStringLiteral("key"), typed(QStringLiteral("string"), QStringLiteral("Entity key."))},
		{QStringLiteral("value"), typed(QStringLiteral("string"), QStringLiteral("Its value."))},
	});
	const QJsonObject action = closedObject({
		{QStringLiteral("kind"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), QJsonArray::fromStringList(levelAiEditActionKinds())},
									 {QStringLiteral("description"), QStringLiteral("add-entity: a point entity. set-key/remove-key: an entity key on the targets. "
																			   "add-box: a box brush in worldspawn. set-texture: every face of the target brushes. "
																			   "move: the targets by delta. delete: the targets.")}}},
		{QStringLiteral("reason"), typed(QStringLiteral("string"), QStringLiteral("One short sentence: why this action."))},
		{QStringLiteral("targets"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), QJsonObject {{QStringLiteral("type"), QStringLiteral("string")}}},
										{QStringLiteral("description"), QStringLiteral("Selectors from the map summary, such as entity:12 or brush:40. Empty for add-entity and add-box.")}}},
		{QStringLiteral("classname"), typed(QStringLiteral("string"), QStringLiteral("add-entity's class name; empty otherwise."))},
		{QStringLiteral("origin"), vectorSchema(QStringLiteral("add-entity's x, y, z; empty otherwise."))},
		{QStringLiteral("keys"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), keyValue},
									 {QStringLiteral("description"), QStringLiteral("add-entity's other keys; empty otherwise.")}}},
		{QStringLiteral("key"), typed(QStringLiteral("string"), QStringLiteral("set-key and remove-key's key; empty otherwise."))},
		{QStringLiteral("value"), typed(QStringLiteral("string"), QStringLiteral("set-key's value; empty otherwise."))},
		{QStringLiteral("mins"), vectorSchema(QStringLiteral("add-box's lowest corner; empty otherwise."))},
		{QStringLiteral("maxs"), vectorSchema(QStringLiteral("add-box's highest corner; empty otherwise."))},
		{QStringLiteral("texture"), typed(QStringLiteral("string"), QStringLiteral("add-box and set-texture's texture name; empty otherwise."))},
		{QStringLiteral("delta"), vectorSchema(QStringLiteral("move's x, y, z offset; empty otherwise."))},
	});
	return closedObject({
		{QStringLiteral("summary"), typed(QStringLiteral("string"), QStringLiteral("What the actions do, or why the request cannot be done with them."))},
		{QStringLiteral("actions"), QJsonObject {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), action}, {QStringLiteral("maxItems"), kMaxActions}}},
	});
}

QString levelAiEditMapContext(const LevelMapDocument& document, const QString& projectRoot, const QString& homeDirectory, int maxObjects)
{
	QStringList lines;
	const LevelMapStatistics statistics = levelMapStatistics(document);
	const QString family = document.format == LevelMapFormat::Quake3Map ? QStringLiteral("Quake III") : QStringLiteral("Quake or Quake II");
	lines << QStringLiteral("Map: %1, a %2 .map with %3 entities, %4 brushes and %5 patches.")
				 .arg(document.mapName.isEmpty() ? QStringLiteral("untitled") : document.mapName, family)
				 .arg(document.entities.size())
				 .arg(document.brushes.size())
				 .arg(document.patches.size());
	if (statistics.mins.valid && statistics.maxs.valid) {
		lines << QStringLiteral("Bounds: %1 to %2. Z is up; players are 32 units wide and 56 tall; walls usually sit on a 16- or 32-unit grid.")
					 .arg(vectorText(statistics.mins), vectorText(statistics.maxs));
	}
	QStringList selected;
	QSet<QString> selectedIds;
	for (const LevelMapSelectionRef& ref : document.selection) {
		selected << levelMapSelectionRefId(ref);
		selectedIds.insert(levelMapSelectionRefId(ref));
	}
	lines << QStringLiteral("Selected: %1").arg(selected.isEmpty() ? QStringLiteral("nothing") : selected.join(QStringLiteral(", ")));

	QHash<int, int> brushesOf;
	for (const LevelMapBrush& brush : document.brushes) {
		brushesOf[brush.entityId] += 1;
	}
	// Selected objects are always described, past the cap, so a request
	// about "the selected" ones can be met on any size of map.
	lines << QString() << QStringLiteral("Entities:");
	int shown = 0;
	int listed = 0;
	for (const LevelMapEntity& entity : document.entities) {
		const bool isSelected = selectedIds.contains(QStringLiteral("entity:%1").arg(entity.id));
		if (shown >= maxObjects && !isSelected) {
			continue;
		}
		++listed;
		shown += isSelected ? 0 : 1;
		QString line = QStringLiteral("entity:%1 %2").arg(entity.id).arg(entity.className);
		if (entity.origin.valid) {
			line += QStringLiteral(" at %1").arg(vectorText(entity.origin));
		}
		if (brushesOf.value(entity.id) > 0) {
			line += QStringLiteral(" (owns %1 brushes)").arg(brushesOf.value(entity.id));
		}
		QStringList keys;
		for (const LevelMapProperty& property : entity.properties) {
			if (property.key == QStringLiteral("classname") || property.key == QStringLiteral("origin")) {
				continue;
			}
			keys << QStringLiteral("%1=\"%2\"").arg(property.key, property.value.left(80));
		}
		if (!keys.isEmpty()) {
			line += QStringLiteral(" keys: ") + keys.join(QLatin1Char(' '));
		}
		lines << line;
	}
	if (document.entities.size() > listed) {
		lines << QStringLiteral("(%1 more entities not listed)").arg(document.entities.size() - listed);
	}
	lines << QString() << QStringLiteral("Brushes:");
	shown = 0;
	listed = 0;
	QMap<QString, int> textureUse;
	for (const LevelMapBrush& brush : document.brushes) {
		QSet<QString> names;
		for (const LevelMapBrushFace& face : brush.faces) {
			names.insert(face.textureName);
			textureUse[face.textureName] += 1;
		}
		const bool isSelected = selectedIds.contains(QStringLiteral("brush:%1").arg(brush.id));
		if (shown >= maxObjects && !isSelected) {
			continue;
		}
		++listed;
		shown += isSelected ? 0 : 1;
		QStringList sorted(names.cbegin(), names.cend());
		sorted.sort();
		lines << QStringLiteral("brush:%1 of entity:%2 from %3 to %4 textures: %5")
					 .arg(brush.id)
					 .arg(brush.entityId)
					 .arg(brush.boundsSolved ? vectorText(brush.mins) : QStringLiteral("?"), brush.boundsSolved ? vectorText(brush.maxs) : QStringLiteral("?"),
						 sorted.join(QStringLiteral(", ")));
	}
	if (document.brushes.size() > listed) {
		lines << QStringLiteral("(%1 more brushes not listed)").arg(document.brushes.size() - listed);
	}
	if (!document.patches.isEmpty()) {
		lines << QString() << QStringLiteral("Patches (curved surfaces):");
		shown = 0;
		listed = 0;
		for (const LevelMapPatch& patch : document.patches) {
			const bool isSelected = selectedIds.contains(QStringLiteral("patch:%1").arg(patch.id));
			if (shown >= maxObjects && !isSelected) {
				continue;
			}
			++listed;
			shown += isSelected ? 0 : 1;
			lines << QStringLiteral("patch:%1 of entity:%2 from %3 to %4 texture: %5")
						 .arg(patch.id)
						 .arg(patch.entityId)
						 .arg(patch.mins.valid ? vectorText(patch.mins) : QStringLiteral("?"), patch.maxs.valid ? vectorText(patch.maxs) : QStringLiteral("?"),
							 patch.textureName);
		}
		if (document.patches.size() > listed) {
			lines << QStringLiteral("(%1 more patches not listed)").arg(document.patches.size() - listed);
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		textureUse[patch.textureName] += 1;
	}
	QStringList textures;
	for (auto it = textureUse.constBegin(); it != textureUse.constEnd(); ++it) {
		textures << QStringLiteral("%1 (%2)").arg(it.key()).arg(it.value());
	}
	lines << QString() << QStringLiteral("Textures in use (faces): %1").arg(textures.join(QStringLiteral(", ")));
	return redactAiText(redactAiContextPaths(lines.join(QLatin1Char('\n')), projectRoot, homeDirectory));
}

QString levelAiEditSystemPrompt()
{
	// Kept in English: it instructs the model, and the user reviews it as sent.
	return QStringLiteral(
		"You edit idTech level maps inside VibeStudio by proposing actions; you never write map text. The user reviews every "
		"action before any is applied, and each applies through the editor as an undoable edit.\n"
		"Actions: add-entity (classname, origin x y z, keys), set-key and remove-key (an entity key on target entities), "
		"add-box (an axis-aligned box brush from mins to maxs in worldspawn, with a texture), set-texture (every face of target "
		"brushes or of a brush entity's brushes), move (targets by a delta), delete (targets; never the worldspawn, entity:0).\n"
		"Rules: name targets only by the selectors in the map summary (entity:N, brush:N, patch:N). Use coordinates in map units, "
		"z up, on the map's grid. Prefer textures already in use and classnames the map or its game already has. Keep to what was "
		"asked, in as few actions as do it. Fill every field of every action, leaving the ones the kind does not use empty. If the "
		"request cannot be done with these actions, return no actions and say why in the summary.");
}

AiChatRequest levelAiEditRequest(const LevelMapDocument& document, const QString& instruction, const QString& connectorId, const QString& model,
	const QString& endpoint, const QString& projectRoot, const QString& homeDirectory)
{
	AiChatRequest request;
	request.connectorId = connectorId;
	request.model = model;
	request.endpoint = endpoint;
	request.system = levelAiEditSystemPrompt();
	AiContextItem context = makeAiContextItem(QStringLiteral("map"), QStringLiteral("Map summary"),
		levelAiEditMapContext(document, projectRoot, homeDirectory), 1400, 120 * 1024);
	request.messages.push_back({QStringLiteral("user"), QStringLiteral("%1\n\n## Request\n%2").arg(aiContextPromptText({context}), instruction.trimmed())});
	request.maxOutputTokens = 8000;
	request.responseSchema = levelAiEditSchema();
	request.responseSchemaName = QStringLiteral("map_edit_proposal");
	return request;
}

bool levelAiEditProposalFromJson(const QJsonObject& object, LevelAiEditProposal* proposal, QString* error)
{
	if (!proposal) {
		return false;
	}
	LevelAiEditProposal parsed;
	parsed.summary = object.value(QStringLiteral("summary")).toString().trimmed();
	if (!object.value(QStringLiteral("actions")).isArray()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelAiEdit", "The answer has no list of actions.");
		}
		return false;
	}
	parsed.problems = aiJsonSchemaProblems(object, levelAiEditSchema());
	for (const QJsonValue& value : object.value(QStringLiteral("actions")).toArray()) {
		const QJsonObject entry = value.toObject();
		LevelAiEditAction action;
		action.kind = entry.value(QStringLiteral("kind")).toString().trimmed().toLower();
		action.reason = entry.value(QStringLiteral("reason")).toString().trimmed();
		for (const QJsonValue& target : entry.value(QStringLiteral("targets")).toArray()) {
			if (!target.toString().trimmed().isEmpty()) {
				action.targets << target.toString().trimmed();
			}
		}
		action.classname = entry.value(QStringLiteral("classname")).toString().trimmed();
		action.origin = vectorFrom(entry.value(QStringLiteral("origin")));
		for (const QJsonValue& pair : entry.value(QStringLiteral("keys")).toArray()) {
			const QString key = pair.toObject().value(QStringLiteral("key")).toString().trimmed();
			if (!key.isEmpty()) {
				action.keys.push_back({key, pair.toObject().value(QStringLiteral("value")).toString(), 0});
			}
		}
		action.key = entry.value(QStringLiteral("key")).toString().trimmed();
		action.value = entry.value(QStringLiteral("value")).toString();
		action.mins = vectorFrom(entry.value(QStringLiteral("mins")));
		action.maxs = vectorFrom(entry.value(QStringLiteral("maxs")));
		action.texture = entry.value(QStringLiteral("texture")).toString().trimmed();
		action.delta = vectorFrom(entry.value(QStringLiteral("delta")));
		// A saved proposal keeps the reviewer's choices; a model's answer has none.
		action.enabled = entry.value(QStringLiteral("enabled")).toBool(true);
		parsed.actions.push_back(action);
	}
	if (parsed.actions.size() > kMaxActions) {
		parsed.problems << QCoreApplication::translate("VibeStudioLevelAiEdit", "Only the first %1 of %2 actions were kept.").arg(kMaxActions).arg(parsed.actions.size());
		parsed.actions.resize(kMaxActions);
	}
	*proposal = parsed;
	return true;
}

bool levelAiEditProposalFromAnswer(const QString& answer, LevelAiEditProposal* proposal, QString* error)
{
	QJsonObject object;
	if (!extractAiJsonObject(answer, &object, error)) {
		return false;
	}
	return levelAiEditProposalFromJson(object, proposal, error);
}

QJsonObject levelAiEditProposalJson(const LevelAiEditProposal& proposal)
{
	QJsonArray actions;
	for (const LevelAiEditAction& action : proposal.actions) {
		QJsonArray keys;
		for (const LevelMapProperty& property : action.keys) {
			keys.append(QJsonObject {{QStringLiteral("key"), property.key}, {QStringLiteral("value"), property.value}});
		}
		actions.append(QJsonObject {
			{QStringLiteral("kind"), action.kind},
			{QStringLiteral("reason"), action.reason},
			{QStringLiteral("targets"), QJsonArray::fromStringList(action.targets)},
			{QStringLiteral("classname"), action.classname},
			{QStringLiteral("origin"), vectorJson(action.origin)},
			{QStringLiteral("keys"), keys},
			{QStringLiteral("key"), action.key},
			{QStringLiteral("value"), action.value},
			{QStringLiteral("mins"), vectorJson(action.mins)},
			{QStringLiteral("maxs"), vectorJson(action.maxs)},
			{QStringLiteral("texture"), action.texture},
			{QStringLiteral("delta"), vectorJson(action.delta)},
			{QStringLiteral("description"), action.description},
			{QStringLiteral("problems"), QJsonArray::fromStringList(action.problems)},
			{QStringLiteral("enabled"), action.enabled},
		});
	}
	return QJsonObject {
		{QStringLiteral("summary"), proposal.summary},
		{QStringLiteral("actions"), actions},
		{QStringLiteral("problems"), QJsonArray::fromStringList(proposal.problems)},
	};
}

void validateLevelAiEditProposal(const LevelMapDocument& document, LevelAiEditProposal* proposal)
{
	if (!proposal) {
		return;
	}
	const bool quakeFamily = document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
	for (LevelAiEditAction& action : proposal->actions) {
		action.problems.clear();
		const auto problem = [&action](const QString& text) { action.problems << text; };
		if (!quakeFamily) {
			problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "Proposed edits apply to Quake-family .map files only."));
		}
		if (!levelAiEditActionKinds().contains(action.kind)) {
			problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "\"%1\" is not an action VibeStudio can apply.").arg(action.kind));
		}
		// Targets: they exist, and are what the action works on.
		QVector<LevelMapSelectionRef> refs;
		for (const QString& selector : action.targets) {
			const LevelMapSelectionRef ref = resolveSelector(document, selector);
			if (ref.kind == LevelMapSelectionKind::None) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "%1 is not an object in this map.").arg(selector));
			} else {
				refs << ref;
			}
		}
		const bool needsTargets = action.kind == QStringLiteral("set-key") || action.kind == QStringLiteral("remove-key") || action.kind == QStringLiteral("set-texture")
			|| action.kind == QStringLiteral("move") || action.kind == QStringLiteral("delete");
		if (needsTargets && action.targets.isEmpty()) {
			problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "It names nothing to act on."));
		}
		const bool entitiesOnly = action.kind == QStringLiteral("set-key") || action.kind == QStringLiteral("remove-key");
		for (const LevelMapSelectionRef& ref : refs) {
			if (entitiesOnly && ref.kind != LevelMapSelectionKind::Entity) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "%1 is not an entity, so it has no keys.").arg(levelMapSelectionRefId(ref)));
			}
			if (action.kind == QStringLiteral("delete") && ref.kind == LevelMapSelectionKind::Entity) {
				const LevelMapEntity* entity = entityById(document, ref.objectId);
				if (entity && entity->className == QStringLiteral("worldspawn")) {
					problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The worldspawn holds the world and is never deleted."));
				}
			}
		}
		const QString targetText = action.targets.join(QStringLiteral(", "));
		if (action.kind == QStringLiteral("add-entity")) {
			if (action.classname.isEmpty() || action.classname.contains(QLatin1Char(' ')) || !cleanText(action.classname)) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The class name must be one word."));
			}
			if (action.classname == QStringLiteral("worldspawn")) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "A map has one worldspawn already."));
			}
			if (!vectorUsable(action.origin)) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The origin must be three numbers between -%1 and %1.").arg(kLargestCoordinate));
			}
			for (const LevelMapProperty& property : action.keys) {
				if (property.key.contains(QLatin1Char(' ')) || !cleanText(property.key) || !cleanText(property.value)) {
					problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "Key %1 has a space, quote, or line break in it.").arg(property.key));
				}
			}
			QStringList keys;
			for (const LevelMapProperty& property : action.keys) {
				keys << QStringLiteral("%1=%2").arg(property.key, property.value);
			}
			action.description = keys.isEmpty() ? QCoreApplication::translate("VibeStudioLevelAiEdit", "Add %1 at %2.").arg(action.classname, vectorText(action.origin))
												 : QCoreApplication::translate("VibeStudioLevelAiEdit", "Add %1 at %2 with %3.").arg(action.classname, vectorText(action.origin), keys.join(QStringLiteral(", ")));
		} else if (action.kind == QStringLiteral("set-key")) {
			if (action.key.isEmpty() || action.key.contains(QLatin1Char(' ')) || !cleanText(action.key) || !cleanText(action.value)) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The key must be one word and the value one line without quotes."));
			}
			if (action.key == QStringLiteral("classname") && action.value == QStringLiteral("worldspawn")) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "A map has one worldspawn already."));
			}
			action.description = QCoreApplication::translate("VibeStudioLevelAiEdit", "Set %1 to \"%2\" on %3.").arg(action.key, action.value, targetText);
		} else if (action.kind == QStringLiteral("remove-key")) {
			if (action.key.isEmpty() || action.key == QStringLiteral("classname")) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "Name a key to remove other than classname."));
			}
			bool anyHas = false;
			for (const LevelMapSelectionRef& ref : refs) {
				const LevelMapEntity* entity = ref.kind == LevelMapSelectionKind::Entity ? entityById(document, ref.objectId) : nullptr;
				anyHas = anyHas || (entity && hasProperty(*entity, action.key));
			}
			if (!refs.isEmpty() && !anyHas) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "None of the targets has %1.").arg(action.key));
			}
			action.description = QCoreApplication::translate("VibeStudioLevelAiEdit", "Remove %1 from %2.").arg(action.key, targetText);
		} else if (action.kind == QStringLiteral("add-box")) {
			if (!vectorUsable(action.mins) || !vectorUsable(action.maxs)) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The corners must be three numbers each between -%1 and %1.").arg(kLargestCoordinate));
			} else if (action.maxs.x - action.mins.x < 1.0 || action.maxs.y - action.mins.y < 1.0 || action.maxs.z - action.mins.z < 1.0) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The box must be at least one unit on every side, maxs above mins."));
			}
			if (action.texture.isEmpty() || !cleanText(action.texture) || action.texture.contains(QLatin1Char(' '))) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The box needs one texture name."));
			}
			action.description = QCoreApplication::translate("VibeStudioLevelAiEdit", "Add a %1 box from %2 to %3.").arg(action.texture, vectorText(action.mins), vectorText(action.maxs));
		} else if (action.kind == QStringLiteral("set-texture")) {
			if (action.texture.isEmpty() || !cleanText(action.texture) || action.texture.contains(QLatin1Char(' '))) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "Name one texture."));
			}
			for (const LevelMapSelectionRef& ref : refs) {
				if (ref.kind == LevelMapSelectionKind::Entity && !ownsGeometry(document, ref.objectId)) {
					problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "%1 is a point entity, with no faces to texture.").arg(levelMapSelectionRefId(ref)));
				}
			}
			action.description = QCoreApplication::translate("VibeStudioLevelAiEdit", "Texture %1 with %2.").arg(targetText, action.texture);
		} else if (action.kind == QStringLiteral("move")) {
			if (!vectorUsable(action.delta) || (action.delta.x == 0.0 && action.delta.y == 0.0 && action.delta.z == 0.0)) {
				problem(QCoreApplication::translate("VibeStudioLevelAiEdit", "The move needs a non-zero x, y, z offset."));
			}
			action.description = QCoreApplication::translate("VibeStudioLevelAiEdit", "Move %1 by %2.").arg(targetText, vectorText(action.delta));
		} else if (action.kind == QStringLiteral("delete")) {
			action.description = QCoreApplication::translate("VibeStudioLevelAiEdit", "Delete %1.").arg(targetText);
		}
		if (action.description.isEmpty()) {
			action.description = action.kind;
		}
		action.enabled = action.enabled && action.problems.isEmpty();
	}
}

LevelAiEditApplyReport applyLevelAiEditProposal(LevelMapDocument* document, const LevelAiEditProposal& proposal)
{
	LevelAiEditApplyReport report;
	if (!document) {
		return report;
	}
	QVector<const LevelAiEditAction*> order;
	for (const LevelAiEditAction& action : proposal.actions) {
		if (action.kind != QStringLiteral("delete")) {
			order << &action;
		}
	}
	// Deletes last, so the other actions still find what they name.
	for (const LevelAiEditAction& action : proposal.actions) {
		if (action.kind == QStringLiteral("delete")) {
			order << &action;
		}
	}
	for (const LevelAiEditAction* action : order) {
		if (!action->enabled || !action->valid()) {
			++report.skipped;
			continue;
		}
		QVector<LevelMapSelectionRef> refs;
		QVector<int> entityIds;
		for (const QString& selector : action->targets) {
			const LevelMapSelectionRef ref = resolveSelector(*document, selector);
			if (ref.kind != LevelMapSelectionKind::None) {
				refs << ref;
				if (ref.kind == LevelMapSelectionKind::Entity) {
					entityIds << ref.objectId;
				}
			}
		}
		QString error;
		bool done = false;
		if (!action->targets.isEmpty() && refs.isEmpty()) {
			// An earlier action took them, as deleting an entity takes its brushes.
			error = QCoreApplication::translate("VibeStudioLevelAiEdit", "what it names is no longer in the map");
		} else if (action->kind == QStringLiteral("add-entity")) {
			int id = -1;
			done = addLevelMapEntity(document, action->classname, action->origin, action->keys, &id, &error);
		} else if (action->kind == QStringLiteral("set-key")) {
			done = setLevelMapEntitiesProperty(document, entityIds, action->key, {action->value}, &error);
		} else if (action->kind == QStringLiteral("remove-key")) {
			done = removeLevelMapEntitiesProperty(document, entityIds, action->key, &error);
		} else if (action->kind == QStringLiteral("add-box")) {
			done = addLevelMapBoxBrush(document, action->mins, action->maxs, action->texture, nullptr, &error);
		} else if (action->kind == QStringLiteral("set-texture")) {
			done = setLevelMapSelection(document, refs, &error) && applyLevelMapTexture(document, action->texture, nullptr, &error);
		} else if (action->kind == QStringLiteral("move")) {
			done = setLevelMapSelection(document, refs, &error) && moveLevelMapSelection(document, action->delta.x, action->delta.y, action->delta.z, &error);
		} else if (action->kind == QStringLiteral("delete")) {
			done = deleteLevelMapObjects(document, refs, &error);
		}
		const int index = int(action - proposal.actions.constData());
		if (done) {
			++report.applied;
			report.appliedActions << index;
			report.lines << QCoreApplication::translate("VibeStudioLevelAiEdit", "Done: %1").arg(action->description);
		} else {
			++report.skipped;
			report.failedActions << index;
			const QString line = QCoreApplication::translate("VibeStudioLevelAiEdit", "Not done: %1 (%2)").arg(action->description, error);
			report.lines << line;
			report.errors << line;
		}
	}
	return report;
}

} // namespace vibestudio
