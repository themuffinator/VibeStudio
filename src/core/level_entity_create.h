#pragma once

// TrenchBroom's generated Create <class> commands: one command for each point
// and brush class of the loaded entity definitions, worldspawn aside. A point
// class goes in at the pointer, or the middle of the view, with its
// definition bounds kept against the surface there; a brush class takes the
// selected brushes and patches. Each is one undo step through the services
// every other route uses (addLevelMapEntity and tieLevelMapSelectionToEntity),
// so scene locks, hidden objects and the open group hold as they do there.
// The Levels page runs it as the CreateEntity placement operation, and
// `map create-entity` runs the same function.
//
// Workflow reference: TB ActionManager::createEntityDefinitionActions and
// MapViewBase::createEntity at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "core/entity_definitions.h"
#include "core/level_paste.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace vibestudio {

// The generated command ids are "map.createEntity.<class>", so a key the user
// gives one class still works after the definitions are reloaded, or a map
// of another game is opened and closed again.
QString levelEntityCreateCommandPrefix();
QString levelEntityCreateCommandId(const QString& className);
// The class a generated id names, from either spelling the registry accepts
// ("map.createEntity.light" or "map.create-entity.light"). False for any
// other id and for a class name a map cannot hold.
bool levelEntityCreateClassForCommand(const QString& commandId, QString* className = nullptr);
// A class name a .map can hold and a command id can carry: no spaces,
// quotes, slashes or control characters, and not worldspawn.
bool levelEntityCreateClassNameUsable(const QString& className);

struct LevelEntityCreateClass {
	QString className;
	EntityClassKind kind = EntityClassKind::Point;
	QString description;
	// The file the class came from, empty for a built-in catalogue.
	QString sourcePath;
	bool hasSize = false;
	ResizeBox bounds {{-8.0, -8.0, -8.0}, {8.0, 8.0, 8.0}};
};

// The classes that get a command: point and brush classes with usable names,
// without worldspawn or base classes, one per command id (the first wins
// when two names differ only by case or by '_' and '-'), sorted ignoring case.
QVector<LevelEntityCreateClass> levelEntityCreateClasses(const EntityDefinitionCatalogue& catalogue);
// Point class bounds, keyed the way LevelPastePlacement::entityBounds reads them.
QHash<QString, ResizeBox> levelEntityCreateBounds(const EntityDefinitionCatalogue& catalogue);

struct LevelEntityCreateRequest {
	QString className;
	// Point or Brush. Unknown, or Base, is refused.
	EntityClassKind kind = EntityClassKind::Unknown;
	// Point classes only: where the new entity goes. Offset mode places its
	// origin at `placement.offset` exactly; the targeted modes place its
	// definition bounds the way a cursor paste places a pasted entity.
	LevelPastePlacement placement;
	// Keys beyond the classname (and a point entity's origin).
	QVector<LevelMapProperty> properties;
};

// Fills the class name, kind and point bounds of `request` from the
// catalogue. False, with a reason, when the catalogue has no usable point or
// brush class of that name.
bool levelEntityCreateRequestForClass(const EntityDefinitionCatalogue& catalogue, const QString& className, LevelEntityCreateRequest* request,
	QString* error = nullptr);

struct LevelEntityCreateReport {
	QString className;
	EntityClassKind kind = EntityClassKind::Unknown;
	int entityId = -1;
	// Point classes: the new entity's origin.
	LevelMapVec3 origin;
	// Brush classes: what the new entity holds.
	int brushes = 0;
	int patches = 0;
};

// True when the selection holds brushes or patches, or brush entities, that
// a brush class could take.
bool levelMapSelectionCanCreateBrushEntity(const LevelMapDocument& document);
// Why the request would be refused before anything changes: a locked layer or
// group, hidden brushes a brush class would take, objects outside the open
// group, or nothing to take. Empty when it can run.
QString levelEntityCreateBlockedReason(const LevelMapDocument& document, const LevelEntityCreateRequest& request);

// Creates the entity as one undo step and selects it. A point class is added
// in the current layer or open group; a brush class takes the selected
// brushes and patches, with those of selected brush entities, out of the
// entities they belong to (an entity left empty goes). Refuses, changing
// nothing, for the reasons levelEntityCreateBlockedReason() gives and for a
// placement outside the editable map range.
bool createLevelMapEntityOfClass(LevelMapDocument* document, const LevelEntityCreateRequest& request, LevelEntityCreateReport* report = nullptr,
	QString* error = nullptr);

} // namespace vibestudio
