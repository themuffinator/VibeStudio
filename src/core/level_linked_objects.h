#pragma once

#include "core/level_map.h"

namespace vibestudio
{

// Native records acted on by a linked group, including brushes and patches
// owned by member entities. This is independent of transient record numbering.
[[nodiscard]] QSet<QString> levelLinkedObjects(const LevelMapDocument& document, const QStringList& members);

// Keep known correspondence, remap native selectors after structural edits and
// give genuinely new records fresh identities. A group's identities cover the
// content of the groups nested in it, as found in `state`. The caller
// explicitly chooses when to establish identities for a legacy group.
[[nodiscard]] QMap<QString, QString> levelLinkObjectIdentities(const LevelMapDocument& document, const LevelSceneState& state,
                                                               const LevelSceneNode& node, const QHash<QString, QString>& remap = {});

// Reconcile groups that already have identities. Legacy groups stay untouched;
// unlinked groups have no correspondence to retain.
void reconcileLevelLinkedObjects(const LevelMapDocument& document, LevelSceneState* state, const QHash<QString, QString>& remap = {});
bool validateLevelLinkedObjects(const LevelMapDocument& document, const LevelSceneState& state, QString* error = nullptr);

} // namespace vibestudio
