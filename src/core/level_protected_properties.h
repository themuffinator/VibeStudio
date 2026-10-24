#pragma once

#include "core/level_map.h"

namespace vibestudio
{

QStringList levelProtectedEntityProperties(const LevelMapDocument& document, int entityId);
bool levelEntityPropertyProtected(const LevelMapDocument& document, int entityId, const QString& key);
bool validateLevelProtectedProperties(const LevelMapDocument& document, const LevelSceneState& state, QString* error = nullptr);
void reconcileLevelProtectedProperties(const LevelMapDocument& document, LevelSceneState* state, const LevelMapUndoCommand& command);

// Each operation stages all selected entities and commits one undo step.
// Unprotect restores an existing unprotected peer value when available;
// otherwise it retains this instance's value. Protection may name absent keys.
bool setLevelEntityPropertyProtection(LevelMapDocument* document, const QVector<int>& entities, const QStringList& keys, bool protect,
                                      QString* error = nullptr);
// TB's Clear Protected Properties: only entities with linked counterparts.
bool clearLevelEntityPropertyProtection(LevelMapDocument* document, const QVector<int>& entities, QString* error = nullptr);
// Add/edit an instance-only key atomically, installing protection before any
// linked propagation can see its value. One value or one per selected entity.
bool setLevelProtectedEntityProperty(LevelMapDocument* document, const QVector<int>& entities, const QString& key,
                                     const QStringList& values, QString* error = nullptr);

} // namespace vibestudio
