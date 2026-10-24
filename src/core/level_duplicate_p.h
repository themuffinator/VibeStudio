#pragma once

#include "core/level_map.h"

namespace vibestudio::detail {
// Remap the copied entities' namespace properties as one set against the
// destination. Original entities and unrelated properties remain unchanged.
void makeLevelMapCopiesUnique(const QVector<LevelMapEntity>& destination, QVector<LevelMapEntity>* copies);
QString makeLevelMapNameUnique(const QVector<LevelMapEntity>& destination, const QString& proposed);
} // namespace vibestudio::detail
