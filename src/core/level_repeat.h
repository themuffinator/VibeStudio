#pragma once

#include "core/level_map.h"
#include <QJsonObject>

namespace vibestudio
{
bool canRepeatLevelMapCommands(const LevelMapDocument& document);
// Clear is session-only: it changes neither map bytes nor undo history.
void clearLevelMapRepeatCommands(LevelMapDocument* document);
// Replays a stable sequence against the current selection as one atomic undo
// transaction. Selection changes and recording are suppressed during replay.
// Current texture-lock/conversion preferences apply to the captured transforms.
bool repeatLevelMapCommands(LevelMapDocument* document, const LevelMapTextureLockOptions& textures = {}, QString* error = nullptr);
bool validateLevelRepeatActions(const QVector<LevelRepeatAction>& actions, QString* error = nullptr);
QJsonObject levelRepeatCommandsJson(const LevelMapDocument& document);
bool parseLevelRepeatCommands(const QJsonObject& json, QVector<LevelRepeatAction>* actions, QString* error = nullptr);

// NetRadiant Custom's Repeat Transforms (LevelRepeatTransforms in
// core/level_repeat_types.h). Every top-level object move, rotation, resize,
// mirror and shear, and every component move, rotation, scale and shear,
// replaces the recorded transform of its kind: a resize or mirror records its
// scale factors and a move its translation, never a pivot. Duplicates and
// snapping record nothing, and replaying records nothing.
//
// Workflow reference: NetRadiant Custom radiant/selection.cpp
// RadiantSelectionSystem::repeatTransforms, resetTransforms and the
// TransformsObserved slots, and radiant/select.cpp "RepeatTransforms" and
// "ResetTransforms" at 449778b3 (GPL-2.0-or-later). Original implementation;
// see docs/CREDITS.md.
struct LevelComponentTransformRequest;
// A Quake-family text map, a selection and at least one recorded transform.
[[nodiscard]] bool canRepeatLevelMapTransforms(const LevelMapDocument& document);
// Forgets the recorded transforms; the map and its history stay as they are.
void resetLevelMapRepeatTransforms(LevelMapDocument* document);
// One short line per recorded transform, such as "Translate 16, 0, 0" or
// "Rotate 90° about Z", in the order they apply; empty when none is recorded.
[[nodiscard]] QStringList levelRepeatTransformsDescription(const LevelRepeatTransforms& transforms);
[[nodiscard]] QJsonObject levelRepeatTransformsJson(const LevelRepeatTransforms& transforms);
// Object mode: copies the selection where it is, then scales, skews and turns
// the copies about `pivot` (the centre of their bounds when invalid) and moves
// them by the translation, as one undo step that leaves the copies selected.
// Pressing it again therefore builds an array. Scene locks, open groups and
// texture lock behave as for each transform alone. `usedPivot` reports the pivot.
bool repeatLevelMapTransforms(LevelMapDocument* document, const LevelMapTextureLockOptions& textures = {},
	const LevelMapVec3& pivot = {}, LevelMapVec3* usedPivot = nullptr, QString* error = nullptr);
// Component mode: the same scale, skew, turn and translation composed into one
// Affine component transform about request->pivot (the picked components'
// centre when invalid). Only the transform fields of `request` change.
bool levelRepeatTransformsComponentRequest(const LevelRepeatTransforms& transforms, LevelComponentTransformRequest* request,
	QString* error = nullptr);
} // namespace vibestudio
