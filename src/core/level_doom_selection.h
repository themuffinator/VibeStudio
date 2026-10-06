#pragma once
#include "core/level_map.h"
#include <QSet>

namespace vibestudio {
// Expands selected vertices/lines/sectors to all geometry joined to their
// vertices. Things remain independently selected; no geometry is changed.
bool selectConnectedLevelMapDoomGeometry(LevelMapDocument* document, QString* error = nullptr);
// A reflection reverses boundary winding. All incident lines must participate
// so an attached, unreflected sector cannot silently acquire inside-out sides.
bool prepareLevelMapDoomMirrorLines(const LevelMapDocument& document, const QSet<int>& vertices, QVector<LevelMapDoomLinedef>* before,
									QVector<LevelMapDoomLinedef>* after, QString* error);
} // namespace vibestudio
