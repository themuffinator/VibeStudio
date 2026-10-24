// TrenchBroom's generated Create <class> commands (see the header).
//
// Workflow reference: TB ActionManager::createEntityDefinitionActions,
// MapViewBase::createEntity, createPointEntity and canCreateBrushEntity at
// 4c756be1 (GPL-3.0-or-later). Original Qt implementation; the point
// placement reuses core/level_paste.h, as TB's computePointEntityPosition
// shares its paste placement. See docs/CREDITS.md.

#include "core/level_entity_create.h"

#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

constexpr double kWorldLimit = 32768.0;
constexpr qsizetype kMaximumClassNameLength = 128;

const QLatin1String kCamelPrefix("map.createEntity.");
const QLatin1String kKebabPrefix("map.create-entity.");

// The registry's rule (core/studio_semantics.cpp, app/studio_actions.cpp): a
// lower-to-upper step is a hyphen, and '_' and ' ' are hyphens too, so two
// ids with the same token name one command.
QString commandToken(const QString& id)
{
	QString token;
	token.reserve(id.size() + 8);
	for (qsizetype index = 0; index < id.size(); ++index) {
		const QChar ch = id.at(index);
		if (ch.isUpper() && index > 0 && (id.at(index - 1).isLower() || id.at(index - 1).isDigit())) {
			token += QLatin1Char('-');
		}
		token += ch.toLower();
	}
	token.replace(QLatin1Char('_'), QLatin1Char('-'));
	token.replace(QLatin1Char(' '), QLatin1Char('-'));
	return token;
}

bool textMap(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

bool isWorldspawn(const QString& className)
{
	return className.compare(QStringLiteral("worldspawn"), Qt::CaseInsensitive) == 0;
}

int worldspawnId(const LevelMapDocument& document)
{
	for (const LevelMapEntity& entity : document.entities) {
		if (isWorldspawn(entity.className)) {
			return entity.id;
		}
	}
	return -1;
}

// What a brush class takes: the selected brushes and patches, and every brush
// and patch of the selected brush entities, as tieLevelMapSelectionToEntity()
// gathers them.
QVector<LevelMapSelectionRef> brushEntityPrimitives(const LevelMapDocument& document)
{
	const int world = worldspawnId(document);
	QSet<int> entities;
	QSet<int> brushes;
	QSet<int> patches;
	for (const LevelMapSelectionRef& ref : document.selection) {
		if (ref.kind == LevelMapSelectionKind::Entity && ref.objectId != world) {
			entities.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakeBrush) {
			brushes.insert(ref.objectId);
		} else if (ref.kind == LevelMapSelectionKind::QuakePatch) {
			patches.insert(ref.objectId);
		}
	}
	QVector<LevelMapSelectionRef> primitives;
	for (const LevelMapBrush& brush : document.brushes) {
		if (brushes.contains(brush.id) || entities.contains(brush.entityId)) {
			primitives.push_back({LevelMapSelectionKind::QuakeBrush, brush.id});
		}
	}
	for (const LevelMapPatch& patch : document.patches) {
		if (patches.contains(patch.id) || entities.contains(patch.entityId)) {
			primitives.push_back({LevelMapSelectionKind::QuakePatch, patch.id});
		}
	}
	return primitives;
}

bool insideWorld(const LevelMapVec3& point)
{
	return point.valid && std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)
		&& std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)}) <= kWorldLimit;
}

} // namespace

QString levelEntityCreateCommandPrefix()
{
	return kCamelPrefix;
}

QString levelEntityCreateCommandId(const QString& className)
{
	return kCamelPrefix + className.trimmed();
}

bool levelEntityCreateClassNameUsable(const QString& className)
{
	if (className.isEmpty() || className.size() > kMaximumClassNameLength || className != className.trimmed() || isWorldspawn(className)) {
		return false;
	}
	return std::none_of(className.cbegin(), className.cend(), [](QChar ch) {
		return ch.isSpace() || ch.category() == QChar::Other_Control || ch == QLatin1Char('"') || ch == QLatin1Char('/')
			|| ch == QLatin1Char('\\');
	});
}

bool levelEntityCreateClassForCommand(const QString& commandId, QString* className)
{
	const QString id = commandId.trimmed();
	for (const QLatin1String& prefix : {kCamelPrefix, kKebabPrefix}) {
		if (!id.startsWith(prefix)) {
			continue;
		}
		const QString name = id.mid(prefix.size());
		if (!levelEntityCreateClassNameUsable(name)) {
			return false;
		}
		if (className) {
			*className = name;
		}
		return true;
	}
	return false;
}

QVector<LevelEntityCreateClass> levelEntityCreateClasses(const EntityDefinitionCatalogue& catalogue)
{
	QVector<LevelEntityCreateClass> classes;
	QSet<QString> tokens;
	for (const EntityClassDefinition& definition : catalogue.classes) {
		if ((definition.kind != EntityClassKind::Point && definition.kind != EntityClassKind::Brush)
			|| !levelEntityCreateClassNameUsable(definition.className)) {
			continue;
		}
		const QString token = commandToken(levelEntityCreateCommandId(definition.className));
		if (tokens.contains(token)) {
			continue;
		}
		tokens.insert(token);
		LevelEntityCreateClass entry;
		entry.className = definition.className;
		entry.kind = definition.kind;
		entry.description = definition.description.simplified();
		// Built-in catalogues name themselves "builtin:<game>", not a file.
		entry.sourcePath = definition.sourcePath.startsWith(QLatin1String("builtin:")) ? QString() : definition.sourcePath;
		entry.hasSize = definition.kind == EntityClassKind::Point && definition.hasSize;
		if (entry.hasSize) {
			for (int axis = 0; axis < 3; ++axis) {
				entry.bounds.mins[axis] = definition.mins[axis];
				entry.bounds.maxs[axis] = definition.maxs[axis];
			}
		}
		classes.push_back(entry);
	}
	std::stable_sort(classes.begin(), classes.end(), [](const LevelEntityCreateClass& left, const LevelEntityCreateClass& right) {
		const int folded = left.className.compare(right.className, Qt::CaseInsensitive);
		return folded != 0 ? folded < 0 : left.className < right.className;
	});
	return classes;
}

QHash<QString, ResizeBox> levelEntityCreateBounds(const EntityDefinitionCatalogue& catalogue)
{
	QHash<QString, ResizeBox> bounds;
	for (const EntityClassDefinition& definition : catalogue.classes) {
		if (definition.kind == EntityClassKind::Point && definition.hasSize) {
			bounds.insert(definition.className.toCaseFolded(), {{definition.mins[0], definition.mins[1], definition.mins[2]},
				{definition.maxs[0], definition.maxs[1], definition.maxs[2]}});
		}
	}
	return bounds;
}

bool levelEntityCreateRequestForClass(const EntityDefinitionCatalogue& catalogue, const QString& className, LevelEntityCreateRequest* request,
	QString* error)
{
	if (error) {
		error->clear();
	}
	const QString name = className.trimmed();
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!request) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityCreate", "Creating an entity needs a request to fill."));
	}
	if (isWorldspawn(name)) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityCreate", "Worldspawn holds the world and is never created. Choose a point or brush class."));
	}
	const QVector<LevelEntityCreateClass> classes = levelEntityCreateClasses(catalogue);
	const LevelEntityCreateClass* found = nullptr;
	for (const LevelEntityCreateClass& entry : classes) {
		if (entry.className == name) {
			found = &entry;
			break;
		}
		if (!found && entry.className.compare(name, Qt::CaseInsensitive) == 0) {
			found = &entry;
		}
	}
	if (!found) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityCreate",
			"The entity definitions have no point or brush class named %1. Load the definitions that describe it, or use Add Entity or Make Brush Entity.")
				.arg(name));
	}
	request->className = found->className;
	request->kind = found->kind;
	request->placement.entityBounds.clear();
	if (found->hasSize) {
		request->placement.entityBounds.insert(found->className.toCaseFolded(), found->bounds);
	}
	return true;
}

bool levelMapSelectionCanCreateBrushEntity(const LevelMapDocument& document)
{
	return textMap(document) && !document.selection.isEmpty() && !brushEntityPrimitives(document).isEmpty();
}

QString levelEntityCreateBlockedReason(const LevelMapDocument& document, const LevelEntityCreateRequest& request)
{
	if (!textMap(document)) {
		return QCoreApplication::translate("VibeStudioLevelEntityCreate", "Entities are created in Quake-family .map files.");
	}
	const QString name = request.className.trimmed();
	if (!levelEntityCreateClassNameUsable(name)) {
		if (isWorldspawn(name)) {
			return QCoreApplication::translate("VibeStudioLevelEntityCreate", "Worldspawn holds the world and is never created. Choose a point or brush class.");
		}
		return QCoreApplication::translate("VibeStudioLevelEntityCreate", "A class name needs letters and no spaces, quotes or slashes.");
	}
	if (request.kind != EntityClassKind::Point && request.kind != EntityClassKind::Brush) {
		return QCoreApplication::translate("VibeStudioLevelEntityCreate", "%1 is not a point or brush class of the loaded entity definitions.").arg(name);
	}
	// New objects go to the current layer or open group (activeSceneNode).
	if (!document.activeSceneNode.isEmpty() && levelSceneLockedNodes(document.scene).contains(document.activeSceneNode)) {
		return QCoreApplication::translate("VibeStudioLevelEntityCreate", "The current layer or group is locked. Unlock it, or make another current, before creating an entity.");
	}
	if (request.kind == EntityClassKind::Point) {
		return {};
	}
	const QVector<LevelMapSelectionRef> primitives = brushEntityPrimitives(document);
	if (primitives.isEmpty()) {
		return QCoreApplication::translate("VibeStudioLevelEntityCreate", "Select brushes or patches to make a %1 of.").arg(name);
	}
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	const QSet<QString> locked = levelSceneLockedObjects(document);
	const LevelSceneSelectionContext context = levelSceneSelectionContext(document);
	int hiddenCount = 0;
	for (const LevelMapSelectionRef& primitive : primitives) {
		const QString object = levelMapSelectionRefId(primitive);
		if (locked.contains(object)) {
			return QCoreApplication::translate("VibeStudioLevelEntityCreate", "%1 is in a locked layer or group. Unlock it before making a brush entity of it.")
				.arg(object);
		}
		if (!context.openGroup.isEmpty() && !context.allowedObjects.contains(object)) {
			return QCoreApplication::translate("VibeStudioLevelEntityCreate", "%1 is outside the open group. Close the group, or select inside it.").arg(object);
		}
		hiddenCount += hidden.contains(object) ? 1 : 0;
	}
	if (hiddenCount > 0) {
		return QCoreApplication::translate("VibeStudioLevelEntityCreate",
			"%n hidden brush(es) or patch(es) would move into the new entity. Show them, or leave their entity out of the selection.", nullptr, hiddenCount);
	}
	return {};
}

bool createLevelMapEntityOfClass(LevelMapDocument* document, const LevelEntityCreateRequest& request, LevelEntityCreateReport* report, QString* error)
{
	if (report) {
		*report = {};
	}
	if (error) {
		error->clear();
	}
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (!document) {
		return fail(QCoreApplication::translate("VibeStudioLevelEntityCreate", "Missing map document."));
	}
	if (const QString reason = levelEntityCreateBlockedReason(*document, request); !reason.isEmpty()) {
		return fail(reason);
	}
	LevelEntityCreateReport made;
	made.className = request.className.trimmed();
	made.kind = request.kind;
	if (request.kind == EntityClassKind::Point) {
		// The new entity is placed the way a cursor paste places a pasted point
		// entity: its definition bounds, at the origin, moved by the offset the
		// placement finds against the map as it is now.
		LevelMapDocument stamp;
		stamp.format = document->format;
		LevelMapEntity entity;
		entity.id = 0;
		entity.className = made.className;
		entity.origin = {0.0, 0.0, 0.0, true};
		stamp.entities.push_back(entity);
		LevelMapVec3 origin;
		if (!levelPasteOffset(*document, stamp, {{LevelMapSelectionKind::Entity, 0}}, request.placement, &origin, error)) {
			return false;
		}
		if (!insideWorld(origin)) {
			return fail(QCoreApplication::translate("VibeStudioLevelEntityCreate", "The new entity would be outside the editable map range."));
		}
		int entityId = -1;
		if (!addLevelMapEntity(document, made.className, origin, request.properties, &entityId, error)) {
			return false;
		}
		made.entityId = entityId;
		made.origin = origin;
	} else {
		int entityId = -1;
		if (!tieLevelMapSelectionToEntity(document, made.className, request.properties, &entityId, error)) {
			return false;
		}
		made.entityId = entityId;
		for (const LevelMapBrush& brush : std::as_const(document->brushes)) {
			made.brushes += brush.entityId == entityId ? 1 : 0;
		}
		for (const LevelMapPatch& patch : std::as_const(document->patches)) {
			made.patches += patch.entityId == entityId ? 1 : 0;
		}
	}
	if (report) {
		*report = made;
	}
	return true;
}

} // namespace vibestudio
