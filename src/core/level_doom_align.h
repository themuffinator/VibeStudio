#pragma once

// Doom Builder's texture auto-align for Doom and Hexen walls. From one side
// of a linedef, the walls joined to it end to start that show the same
// texture in the same part are walked both ways, and each side's offsets are
// set so the texture runs on across every join: X by the length walked, Y so
// the texture's rows stay level as floors and ceilings change.
//
// Y follows the Doom renderer's pegging (linuxdoom-1.10 r_segs.c,
// R_StoreWallRange): a one-sided middle hangs from the ceiling, or stands on
// the floor when lower-unpegged; an upper hangs from the back ceiling, or from
// the front ceiling when upper-unpegged; a lower starts at the back floor, or
// continues from the front ceiling when lower-unpegged. A texture's height
// drops out, as rows repeat every texture height.
//
// Binary Doom and Hexen maps; UDMF offsets are edited as UDMF properties.

#include "core/level_map.h"

#include <QHash>
#include <QSet>

namespace vibestudio {

enum class LevelDoomWallPart { Upper, Middle, Lower };

struct LevelDoomAlignRequest {
	// The side to start from; it keeps its own offsets.
	int sidedef = -1;
	LevelDoomWallPart part = LevelDoomWallPart::Middle;
	bool alignX = true;
	bool alignY = true;
	// When not empty, the walk stays on these linedefs.
	QSet<int> within;
	// Texture widths by upper-case name keep X offsets within one width; a
	// texture of unknown width keeps the length walked, wrapped at 4096 only
	// beyond what a sidedef can hold.
	QHash<QString, int> widths;
};

// The first part of a side showing a texture: middle, then upper, then lower.
bool levelDoomSideTexturedPart(const LevelMapDocument& document, int sidedef, LevelDoomWallPart* part);

// One undo step. `aligned` counts the sides changed, the start excluded.
bool alignLevelMapDoomWallTextures(LevelMapDocument* document, const LevelDoomAlignRequest& request, int* aligned = nullptr,
	QString* error = nullptr);

} // namespace vibestudio
