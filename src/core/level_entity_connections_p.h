#pragma once

#include "core/level_entity_connections.h"

namespace vibestudio::detail {
bool prepareLevelEntityConnection(const LevelMapDocument& document, const LevelEntityConnectionRequest& request,
	LevelMapUndoCommand* command, LevelEntityConnectionReport* report, QString* error);
} // namespace vibestudio::detail
