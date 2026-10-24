#pragma once

#include "core/level_map.h"
#include "core/level_entity_connection_types.h"

namespace vibestudio {
QString levelEntityConnectionGame(const LevelMapDocument& document, const QString& requested = {});
bool levelEntityKillConnectionSupported(const LevelMapDocument& document, const QString& game = {});
// Selection eligibility only; naming and locked-record validation happen in the
// transaction. Radiant modes require two instances with distinct entity owners.
bool canConnectLevelMapEntities(const LevelMapDocument& document, const LevelEntityConnectionRequest& request);
bool connectLevelMapEntities(LevelMapDocument* document, const LevelEntityConnectionRequest& request,
	LevelEntityConnectionReport* report = nullptr, QString* error = nullptr);
} // namespace vibestudio
