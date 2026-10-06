#pragma once

#include "core/level_map.h"
#include <QSet>
#include <functional>
#include <optional>

namespace vibestudio {
// Local locks inherit through groups. The returned objects include geometric
// dependencies (entity primitives, Doom sector boundaries and line endpoints).
QSet<QString> levelSceneLockedNodes(const LevelSceneState& scene);
QSet<QString> levelSceneLockedObjects(const LevelMapDocument& document);
bool setLevelSceneLocked(LevelMapDocument* document, const QString& id, bool locked, QString* error = nullptr);

// Scene-only edits may change names, visibility and lock flags, but cannot move
// or remove protected nodes/members in the same operation as unlocking them.
bool validateLevelSceneOrganizationEdit(const LevelMapDocument& before, const LevelSceneState& after, QString* error);

// Authoring entry points call this before touching any document state. With no
// locks (or when already inside this transaction), nullopt means run normally.
// Otherwise it runs the operation on a COW candidate, checks protected native
// records and membership, and publishes only a complete valid result. Caller
// output parameters must be staged until the returned result is true.
std::optional<bool> guardLevelSceneEdit(LevelMapDocument* document, QString* error,
										const std::function<bool(LevelMapDocument*)>& operation);
// Called by the shared undo recorder; captures identity remaps even when the
// undo limit evicts a command. History replay deliberately bypasses this guard.
void recordLevelSceneEdit(const LevelMapDocument* document, const LevelMapUndoCommand& command);
} // namespace vibestudio
