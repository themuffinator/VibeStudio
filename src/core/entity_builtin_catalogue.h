#pragma once

// Starter entity catalogues for the three Quake-family games, used when a
// project has no .def, .fgd or .ent files of its own.
//
// The class names, keys, spawnflag bits, sizes and editor colours are the
// games' own interface (from the GPL id Software sources: the Quake QuakeC
// progs, the Quake II game DLL and the Quake III game module); the
// descriptions are VibeStudio's own words. Definitions the user loads always
// replace these.

#include "core/entity_definitions.h"
#include "core/level_map.h"

#include <QString>

namespace vibestudio {

enum class BuiltinEntityGame {
	Quake,
	Quake2,
	Quake3,
};

[[nodiscard]] QString builtinEntityGameId(BuiltinEntityGame game);
[[nodiscard]] QString builtinEntityGameDisplayName(BuiltinEntityGame game);
[[nodiscard]] bool builtinEntityGameFromId(const QString& id, BuiltinEntityGame* out);

// The game whose classes suit an open map: Quake III for Quake III maps;
// Quake II for maps with Quake II evidence (its target header, folder-style
// texture names without a wad key, or classes only Quake II has); else Quake.
[[nodiscard]] BuiltinEntityGame builtinEntityGameForMap(const LevelMapDocument& document);

// Every class of the game's catalogue, sorted by class name, with
// sourcePaths naming the built-in catalogue rather than a file.
[[nodiscard]] EntityDefinitionCatalogue builtinEntityDefinitions(BuiltinEntityGame game);

} // namespace vibestudio
