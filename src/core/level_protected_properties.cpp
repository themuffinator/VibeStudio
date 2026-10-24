#include "core/level_protected_properties.h"
#include "core/level_history_p.h"
#include "core/level_linked_groups.h"
#include "core/level_linked_groups_p.h"
#include "core/level_linked_objects.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <algorithm>
#include <optional>

// Independently implemented behaviour reference: TrenchBroom v2026.2,
// Map_Entities.cpp and LinkedGroupUtils.cpp, GPL-3.0-or-later.
// See docs/CREDITS.md; no upstream implementation or metadata is incorporated.
namespace vibestudio
{
namespace
{
QString objectId(int id)
{
	return QStringLiteral("entity:%1").arg(id);
}
bool fail(QString* error, const char* message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelProtectedProperties", message);
	}
	return false;
}
const LevelMapEntity* entity(const LevelMapDocument& document, int id)
{
	for (const auto& item : document.entities) {
		if (item.id == id) {
			return &item;
		}
	}
	return nullptr;
}
std::optional<QString> property(const LevelMapEntity& entity, const QString& key)
{
	for (const auto& item : entity.properties) {
		if (item.key.compare(key, Qt::CaseInsensitive) == 0) {
			return item.value;
		}
	}
	return std::nullopt;
}
bool validKey(const QString& key)
{
	return !key.isEmpty() && key.size() <= 1024 && key == key.trimmed() && key == key.toCaseFolded() &&
	       std::none_of(key.cbegin(), key.cend(), [](QChar c) {
		       return c == QLatin1Char('"') || c.category() == QChar::Other_Control || c == QChar::LineSeparator ||
		              c == QChar::ParagraphSeparator;
	       });
}
QVector<int> peers(const LevelMapDocument& document, int id)
{
	const auto object = objectId(id);
	const auto* source = levelSceneNode(document.scene, levelSceneMembership(document.scene, object));
	if (!source || source->linkId.isEmpty() || !source->linkObjects.contains(object)) {
		return {};
	}
	const auto identity = source->linkObjects.value(object);
	QVector<int> result;
	for (const auto& node : document.scene.nodes) {
		if (node.linkId != source->linkId) {
			continue;
		}
		for (auto it = node.linkObjects.cbegin(); it != node.linkObjects.cend(); ++it) {
			if (it.value() == identity && it.key().startsWith(QLatin1String("entity:"))) {
				result << it.key().section(QLatin1Char(':'), 1).toInt();
				break;
			}
		}
	}
	return result;
}
std::optional<QString> unprotectedValue(const LevelMapDocument& document, int id, const QString& key)
{
	const auto corresponding = peers(document, id);
	if (corresponding.size() < 2) {
		return std::nullopt;
	}
	for (int peer : corresponding) {
		if (levelEntityPropertyProtected(document, peer, key)) {
			continue;
		}
		if (const auto* item = entity(document, peer)) {
			if (auto value = property(*item, key)) {
				return value;
			}
		}
	}
	return std::nullopt;
}
enum class Operation { Protect, Unprotect, Clear, SetValue };

bool edit(LevelMapDocument* document, const QVector<int>& requested, QStringList keys, Operation operation, const QStringList& values,
          QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document || document->format == LevelMapFormat::Unknown || document->format == LevelMapFormat::DoomWad) {
		return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties", "Protected properties need entities in a Quake-family map."));
	}
	if (!validateLevelScene(*document, document->scene, error)) {
		return false;
	}
	if (requested.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties", "Select entities whose property protection you want to change."));
	}
	for (auto& key : keys) {
		key = key.trimmed().toCaseFolded();
	}
	keys.removeDuplicates();
	keys.sort();
	if ((operation != Operation::Clear && keys.isEmpty()) ||
	    std::any_of(keys.cbegin(), keys.cend(), [](const auto& key) { return !validKey(key); })) {
		return fail(error, QT_TRANSLATE_NOOP(
		                       "LevelProtectedProperties",
		                       "Protected keys must be nonempty, at most 1,024 characters and contain no quotes or control characters."));
	}
	if (operation == Operation::SetValue && (keys.size() != 1 || (values.size() != 1 && values.size() != requested.size()))) {
		return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties", "Give one value, or one value for each selected entity."));
	}
	const auto locked = levelSceneLockedObjects(*document);
	const auto context = levelSceneSelectionContext(*document);
	QVector<int> entities;
	QStringList entityValues;
	QSet<int> seen;
	for (qsizetype i = 0; i < requested.size(); ++i) {
		const int id = requested[i];
		const auto* item = entity(*document, id);
		if (!item || item->className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0) {
			return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties", "Choose existing non-worldspawn entities."));
		}
		if (locked.contains(objectId(id))) {
			return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties",
			                                     "Unlock the selected entities before changing their property protection."));
		}
		if (!context.openGroup.isEmpty() && !context.allowedObjects.contains(objectId(id))) {
			return fail(error,
			            QT_TRANSLATE_NOOP("LevelProtectedProperties", "Close the open group before changing properties outside it."));
		}
		if (!seen.contains(id)) {
			seen.insert(id);
			entities << id;
			if (operation == Operation::SetValue) {
				entityValues << (values.size() == 1 ? values.first() : values[i]);
			}
		}
	}
	auto state = document->scene;
	struct Restore {
		int id;
		QString key;
		QString value;
	};
	QVector<Restore> restorations;
	for (int id : entities) {
		const auto object = objectId(id);
		const auto previous = state.protectedProperties.value(object);
		auto next = previous;
		auto requestedKeys = keys;
		if (operation == Operation::Clear) {
			if (peers(*document, id).size() < 2) {
				continue;
			}
			requestedKeys = previous;
		}
		for (const auto& key : requestedKeys) {
			if (operation == Operation::Protect || operation == Operation::SetValue) {
				if (!next.contains(key)) {
					next << key;
				}
			} else if (next.removeAll(key) > 0) {
				if (const auto value = unprotectedValue(*document, id, key)) {
					if (property(*entity(*document, id), key) != value) {
						restorations << Restore{id, key, *value};
					}
				}
			}
		}
		next.sort();
		if (next.isEmpty()) {
			state.protectedProperties.remove(object);
		} else {
			state.protectedProperties.insert(object, next);
		}
	}
	if (!validateLevelScene(*document, state, error)) {
		return false;
	}
	const auto description =
	    operation == Operation::Clear      ? QCoreApplication::translate("LevelProtectedProperties", "Clear protected properties")
	    : operation == Operation::SetValue ? QCoreApplication::translate("LevelProtectedProperties", "Set protected property")
	                                       : QCoreApplication::translate("LevelProtectedProperties", "Change property protection");
	auto candidate = *document;
	const auto selection = document->selection;
	const auto apply = [&](LevelMapDocument* working) {
		const detail::LevelMapHistoryScope history(*working);
		const auto before = working->undoStack.size();
		{
			const detail::LevelLinkedUpdateScope quiet;
			if (!commitLevelSceneState(working, state, description, error)) {
				return false;
			}
			for (const auto& restore : restorations) {
				if (!setLevelMapEntityProperty(working, restore.id, restore.key, restore.value, error)) {
					return false;
				}
			}
			if (operation == Operation::SetValue && !setLevelMapEntitiesProperty(working, entities, keys.first(), entityValues, error)) {
				return false;
			}
			const auto steps = working->undoStack.size() - before;
			if (steps > 0 &&
			    !collapseLevelMapUndoSteps(working, static_cast<int>(steps), description,
			                               QCoreApplication::translate("LevelProtectedProperties", "Restore property protection"), error)) {
				return false;
			}
		}
		if (working->undoStack.size() > before) {
			syncLevelLinkedGroups(working);
		}
		setLevelMapSelection(working, selection);
		return true;
	};
	if (const auto guarded = guardLevelSceneEdit(&candidate, error, apply); guarded.has_value()) {
		if (!*guarded) {
			return false;
		}
	} else if (!apply(&candidate)) {
		return false;
	}
	if (!validateLevelScene(candidate, candidate.scene, error)) {
		return false;
	}
	// A large batch must not leave an accepted edit that cannot be saved.
	// Native IDs can shrink on serialization; checking the current selectors
	// conservatively bounds the encoded payload without solving map geometry.
	QHash<QString, QString> emitted;
	for (const auto& object : levelSceneObjects(candidate)) {
		emitted.insert(object, object);
	}
	QString encodingError;
	encodeLevelScene(candidate, QByteArray(32, '\0'), emitted, &encodingError);
	if (!encodingError.isEmpty()) {
		if (error) {
			*error = encodingError;
		}
		return false;
	}
	*document = std::move(candidate);
	return true;
}
} // namespace

QStringList levelProtectedEntityProperties(const LevelMapDocument& document, int entityId)
{
	return document.scene.protectedProperties.value(objectId(entityId));
}

bool levelEntityPropertyProtected(const LevelMapDocument& document, int entityId, const QString& key)
{
	return levelProtectedEntityProperties(document, entityId).contains(key.trimmed().toCaseFolded());
}

bool validateLevelProtectedProperties(const LevelMapDocument& document, const LevelSceneState& state, QString* error)
{
	if (state.protectedProperties.isEmpty()) {
		return true;
	}
	if (document.format == LevelMapFormat::Unknown || document.format == LevelMapFormat::DoomWad) {
		return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties", "Protected properties need entities in a Quake-family map."));
	}
	QSet<QString> available;
	for (const auto& item : document.entities) {
		if (item.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
			available.insert(objectId(item.id));
		}
	}
	qsizetype count = 0;
	QSet<QString> families;
	for (auto it = state.protectedProperties.cbegin(); it != state.protectedProperties.cend(); ++it) {
		if (!available.contains(it.key()) || it.value().isEmpty()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties",
			                                     "Protected properties must name an existing non-worldspawn entity and at least one key."));
		}
		QString previous;
		for (const auto& key : it.value()) {
			if (!validKey(key) || (!previous.isEmpty() && key <= previous) || ++count > kLevelSceneMaxMembers) {
				return fail(error, QT_TRANSLATE_NOOP("LevelProtectedProperties",
				                                     "Protected keys must be canonical, sorted, unique and total at most 100,000."));
			}
			previous = key;
		}
		const auto* node = levelSceneNode(state, levelSceneMembership(state, it.key()));
		if (node && !node->linkId.isEmpty()) {
			families.insert(node->linkId);
		}
	}
	for (const auto& node : state.nodes) {
		if (!families.contains(node.linkId)) {
			continue;
		}
		const QSet<QString> known(node.linkObjects.keyBegin(), node.linkObjects.keyEnd());
		if (known != levelLinkedObjects(document, levelLinkedGroupMembers(state, node.id))) {
			return fail(error,
			            QT_TRANSLATE_NOOP(
			                "LevelProtectedProperties",
			                "Use Update Linked Groups on the legacy linked groups to establish object correspondence before protecting their properties."));
		}
	}
	return true;
}

void reconcileLevelProtectedProperties(const LevelMapDocument& document, LevelSceneState* state, const LevelMapUndoCommand& command)
{
	if (!state || state->protectedProperties.isEmpty()) {
		return;
	}
	QSet<QString> available;
	for (const auto& item : document.entities) {
		if (item.className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) != 0) {
			available.insert(objectId(item.id));
		}
	}
	QMap<QString, QStringList> result;
	for (auto it = state->protectedProperties.cbegin(); it != state->protectedProperties.cend(); ++it) {
		const auto now = command.sceneObjectRemap.value(it.key(), it.key());
		if (available.contains(now)) {
			result.insert(now, it.value());
		}
	}
	for (auto it = command.sceneObjectOrigins.cbegin(); it != command.sceneObjectOrigins.cend(); ++it) {
		if (available.contains(it.key()) && state->protectedProperties.contains(it.value())) {
			result.insert(it.key(), state->protectedProperties.value(it.value()));
		}
	}
	state->protectedProperties = result;
}

bool setLevelEntityPropertyProtection(LevelMapDocument* document, const QVector<int>& entities, const QStringList& keys, bool protect,
                                      QString* error)
{
	return edit(document, entities, keys, protect ? Operation::Protect : Operation::Unprotect, {}, error);
}

bool clearLevelEntityPropertyProtection(LevelMapDocument* document, const QVector<int>& entities, QString* error)
{
	return edit(document, entities, {}, Operation::Clear, {}, error);
}

bool setLevelProtectedEntityProperty(LevelMapDocument* document, const QVector<int>& entities, const QString& key,
                                     const QStringList& values, QString* error)
{
	return edit(document, entities, {key}, Operation::SetValue, values, error);
}
} // namespace vibestudio
