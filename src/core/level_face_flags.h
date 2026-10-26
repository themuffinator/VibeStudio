#pragma once

#include "core/level_surface.h"
#include "core/material_model.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// Per-face content flags, surface flags and surface value: the three numbers a
// Quake II or Quake III brush face writes after its texture placement.
// NetRadiant Custom's Surface Inspector edits them with a box per flag and a
// Value field, and TrenchBroom's Face Inspector with its surface and content
// flag lists. Workflow reference: NRC radiant/surfacedialog.cpp
// SurfaceInspector::ApplyFlags and brush.h ContentsFlagsValue_assignMasked at
// 449778b3 (GPL-2.0-or-later) and TB FaceAttribsEditor at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.

// Which game names a map's face flags: Quake III maps, and Quake-format maps
// that are Quake II, either because a face already writes the three numbers
// (levelMapUsesFaceFlags()) or by builtinEntityGameForMap()'s evidence (a
// Quake II header, Quake II classes, folder texture names). Quake maps carry
// none.
enum class LevelFaceFlagGame { None, Quake2, Quake3 };
LevelFaceFlagGame levelFaceFlagGame(const LevelMapDocument& document);

enum class LevelFaceFlagWord { Content, Surface };

// The named bits of one flag word in bit order, with translated labels and
// descriptions: Quake II's from qfiles.h and q_shared.h (the Materials
// module's lists, re-release bits included) and Quake III's from
// code/game/surfaceflags.h. Bits a game leaves unnamed are not listed.
QVector<MaterialFlagDescriptor> levelFaceFlagNames(LevelFaceFlagGame game, LevelFaceFlagWord word);
// The flag ids set in `bits`, in bit order, then any unnamed bits as one hex
// number (0x...).
QStringList levelFaceFlagIds(LevelFaceFlagGame game, LevelFaceFlagWord word, quint32 bits);
// Reads a comma-separated list of flag ids ("playerclip,water"), numbers
// (decimal or 0x hex) or both into one mask. Unknown names fail.
bool parseLevelFaceFlagBits(LevelFaceFlagGame game, LevelFaceFlagWord word, const QString& text, quint32* bits, QString* error = nullptr);

// The detail content bit (CONTENTS_DETAIL in both games). Make Detail and Make
// Structural set it on whole brushes, so face flag edits keep it as it is, as
// NetRadiant Custom's Surface Inspector does.
inline constexpr quint32 kLevelFaceDetailContent = 0x8000000;

// The flags of a set of brush faces: the bits set on every face, the bits set
// on any, and the value, unless the faces differ.
struct LevelFaceFlagsSummary {
	int faces = 0;
	quint32 contentAll = 0, contentAny = 0;
	quint32 surfaceAll = 0, surfaceAny = 0;
	qint64 value = 0;
	bool valueMixed = false;
};
// Faces that no longer exist are left out of the count.
LevelFaceFlagsSummary levelFaceFlagsSummary(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces);

// Bits to set and to clear in each word; every other bit of each face stays as
// it is, so one box changes one flag across faces that differ. `setValue`
// gives every face `value`.
struct LevelFaceFlagsEdit {
	quint32 setContent = 0, clearContent = 0;
	quint32 setSurface = 0, clearSurface = 0;
	bool setValue = false;
	qint64 value = 0;
	[[nodiscard]] bool changesSomething() const { return setContent || clearContent || setSurface || clearSurface || setValue; }
	// NetRadiant Custom's Unset: every flag but detail cleared, value 0.
	static LevelFaceFlagsEdit clearAll();
};

// Prepares the edit as a plan that commitLevelSurfaceEdit() makes one undo
// step, rewriting only the three numbers on each changed face's line (adding
// them to a line that has none). A changed flag word is written as a signed
// 32-bit number, so bit 31 reads -2147483648, which every compiler parses. Refuses maps without face flags, the detail
// bit, a value outside a signed 32-bit field, faces that no longer exist, and
// faces of hidden objects or of objects outside the open group; a locked
// layer or group refuses on commit with the scene-lock guard. Refuses an edit
// that would change nothing.
bool prepareLevelFaceFlagsEdit(const LevelMapDocument& document, const QVector<LevelSurfaceFace>& faces, const LevelFaceFlagsEdit& edit,
	LevelSurfaceEditPlan* plan, QString* error = nullptr);

struct LevelFaceFlagsReport {
	int faces = 0;
	int changedFaces = 0;
	int brushes = 0;
};
// Prepares and commits in one go, as the Surfaces tab and map face-flags do.
bool setLevelMapFaceFlags(LevelMapDocument* document, const QVector<LevelSurfaceFace>& faces, const LevelFaceFlagsEdit& edit,
	LevelFaceFlagsReport* report = nullptr, QString* error = nullptr);

} // namespace vibestudio
