#pragma once
#include "core/level_map.h"

namespace vibestudio {
enum class LevelVisibilityOperation { HideSelected, ShowAll, Isolate };
struct LevelVisibilityRequest {
	LevelVisibilityOperation operation = LevelVisibilityOperation::HideSelected;
	bool undoable = false;
	bool inheritGroups = false;
};
struct LevelVisibilityReport {
	int hidden = 0, shown = 0;
	bool changed = false;
};
// Does not dirty the map or rewrite existing states/history. Advances the
// session revision so pending authoring cannot publish with an old policy.
void setLevelMapInsertionVisibility(LevelMapDocument* document, LevelInsertionVisibility policy);
// Includes persistent scene visibility and inherited/dependent geometry.
QSet<QString> levelMapHiddenObjects(const LevelMapDocument& document);
// Valid session targets, including containers even when they are empty.
QSet<QString> levelMapVisibilityTargets(const LevelMapDocument& document);
// Atomic session operation. Closed groups are expanded, worldspawn stays
// visible, filters and persistent scene visibility are unchanged.
bool changeLevelMapVisibility(LevelMapDocument* document, const LevelVisibilityRequest& request,
	LevelVisibilityReport* report = nullptr, QString* error = nullptr);
// Explicit object-list selection reveals temporary hiding as part of the same
// selection transaction; persistent scene visibility is never overridden.
bool selectAndRevealLevelMapObjects(LevelMapDocument* document, const QVector<LevelMapSelectionRef>& selection,
	bool undoable, QString* error = nullptr);
namespace detail {
void prepareLevelMapSceneVisibility(const LevelMapDocument& before, LevelMapUndoCommand* command);
void recordLevelMapCopiedVisibility(const LevelMapDocument& source, const QHash<QString,QString>& newToSource, LevelMapUndoCommand* command);
void reconcileLevelMapVisibility(LevelMapDocument* document, LevelMapUndoCommand* command);
void replayLevelMapVisibilityRemap(LevelMapDocument* document, const LevelMapUndoCommand& command, bool forward);
}
} // namespace vibestudio
