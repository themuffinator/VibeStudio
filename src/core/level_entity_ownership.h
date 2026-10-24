#pragma once

// Which entity owns a brush or patch: NetRadiant Custom's Move Primitives to
// Entity (the entity of the last- or first-selected item takes the others),
// its Ungroup Primitives (only the selected primitives go back to the world),
// TrenchBroom's Make Structural (selected brushes also leave brush entities for
// the world or the current group) and bobToolz's Explode Entity (each
// primitive gets its own copy of its entity's keys).
//
// Each edit is one undo step. Brushes and patches keep their ids, so their
// scene membership, temporary visibility and selection follow them; their text
// moves with them to the end of the new owner and leaves its old lines. A
// brush entity the edit leaves with no brushes or patches is removed, as both
// editors remove it. Scene locks, linked groups, temporarily hidden objects and
// the open group are respected: a locked or linked object refuses the edit, and
// primitives hidden inside a selected entity stay where they are.
//
// Workflow references: NRC radiant/entity.cpp Entity_moveSelectedPrimitives and
// Entity_ungroupSelectedPrimitives, radiant/map.cpp
// Scene_parentSelectedBrushesToEntity, and contrib/bobtoolz/funchandlers-GTK.cpp
// DoExplodeEntity at 449778b3 (GPL-2.0-or-later); TB MapViewBase::makeStructural
// at 4c756be1 (GPL-3.0-or-later). Original Qt implementation in
// core/level_map.cpp; see docs/CREDITS.md.

#include "core/level_map.h"

namespace vibestudio {

// The entity that receives the moved primitives.
enum class LevelPrimitiveTarget {
	// The entity owning the last-selected item (NRC EntityMovePrimitivesToLast).
	LastSelected,
	// The entity owning the first-selected item (NRC EntityMovePrimitivesToFirst).
	FirstSelected,
	// The entity named by LevelPrimitiveMoveRequest::entityId.
	Entity,
};

struct LevelPrimitiveMoveRequest {
	LevelPrimitiveTarget target = LevelPrimitiveTarget::LastSelected;
	int entityId = -1;
};

struct LevelPrimitiveMoveReport {
	int targetEntity = -1;
	QString targetClass;
	int movedBrushes = 0;
	int movedPatches = 0;
	// Source entities the move left empty, removed in the same step.
	QVector<int> removedEntities;
	// Primitives of selected entities left in place because they are hidden.
	int keptHidden = 0;
};

// The entity a selected item belongs to: an entity is its own owner, a brush or
// patch the entity holding it. -1 when the item is not in the map.
[[nodiscard]] int levelMapSelectionOwner(const LevelMapDocument& document, const LevelMapSelectionRef& ref);
// Worldspawn, or an entity that holds brushes or patches or has no origin:
// one that can take brushes. A point entity cannot.
[[nodiscard]] bool levelMapEntityHoldsPrimitives(const LevelMapDocument& document, int entityId);

// Moves the selected brushes and patches, and those of selected brush entities
// other than the target, into the target entity, which keeps its keys.
// Worldspawn can be the target; brushes it takes join the current group or
// layer unless they already belong to a scene node. Needs a Quake-family map.
bool moveLevelMapSelectionToEntity(LevelMapDocument* document, const LevelPrimitiveMoveRequest& request,
	LevelPrimitiveMoveReport* report = nullptr, QString* error = nullptr);
// Cheap check for menus: whether moveLevelMapSelectionToEntity has a target
// that can hold brushes and something else to move into it.
[[nodiscard]] bool levelMapSelectionMovableToEntity(const LevelMapDocument& document, LevelPrimitiveTarget target);

// Moves only the selected brushes and patches (and those of selected brush
// entities) to worldspawn; entities keep their other primitives and keys.
bool moveLevelMapSelectedPrimitivesToWorld(LevelMapDocument* document, LevelPrimitiveMoveReport* report = nullptr, QString* error = nullptr);

enum class LevelStructuralPolicy {
	// Clear detail: the detail content flag in Quake II and III, or leave a
	// func_detail entity for the world in Quake.
	DetailOnly,
	// TrenchBroom: also move the selected brushes out of every other brush
	// entity to the world, into the current group or layer.
	LeaveEntities,
};

struct LevelStructuralReport {
	// Brushes the edit changed in any way.
	int changed = 0;
	int movedToWorld = 0;
	int flagsCleared = 0;
	QVector<int> removedEntities;
};

// Makes the selected brushes structural as one undo step, refused when nothing
// would change.
bool makeLevelMapSelectionStructural(LevelMapDocument* document, LevelStructuralPolicy policy, LevelStructuralReport* report = nullptr,
	QString* error = nullptr);

struct LevelExplodeRequest {
	// Give each new copy its own targetname, so triggers keep firing only the
	// original. Off by default: every copy keeps identical keys, as bobToolz.
	bool renameTargetNames = false;
};

struct LevelExplodeReport {
	QVector<int> explodedEntities;
	QVector<int> createdEntities;
	// Brushes and patches that moved into a new copy.
	int movedPrimitives = 0;
	int keptHidden = 0;
};

// Splits each selected brush entity (or the entity owning a selected brush or
// patch) so every brush and patch has an entity of its own: the entity keeps
// its first one and each other gets a copy of its keys. Worldspawn is refused.
bool explodeLevelMapSelectionEntities(LevelMapDocument* document, const LevelExplodeRequest& request = {},
	LevelExplodeReport* report = nullptr, QString* error = nullptr);
// Cheap check for menus: whether the selection reaches a brush entity with at
// least two brushes or patches.
[[nodiscard]] bool levelMapSelectionExplodable(const LevelMapDocument& document);

} // namespace vibestudio
