#pragma once

// idTech 1 and 2 materials: what Doom, Quake and Quake II derive from texture
// names and lumps, since these engines have no material scripts.
//
// - Doom: wall textures composed from patches (PNAMES, TEXTURE1/TEXTURE2),
//   flats, animation ranges (the vanilla table, Boom's ANIMATED lump or
//   Hexen/ZDoom ANIMDEFS) and switch pairs (vanilla or Boom's SWITCHES).
//   Tables and timings follow id Software's linuxdoom-1.10 p_spec.c and
//   p_switch.c (GPL-2.0-or-later); the lump layouts follow the Boom
//   documentation and the Doom Wiki.
// - Quake: `+0name`..`+9name` animations (and `+a`..`+j` alternates), `*`
//   liquids, `sky` two-layer skies and `{` masked textures, following
//   Mod_LoadTextures and R_TextureAnimation in id Software's Quake source
//   (GPL-2.0-or-later).
// - Quake II: the WAL header's next-frame chain and surface flags, following
//   id Software's Quake II source (GPL-2.0-or-later).

#include "core/idtech_image.h"
#include "core/material_model.h"
#include "core/operation_state.h"
#include "core/package_archive.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

// Doom runs its game logic at 35 tics a second.
inline constexpr double kDoomTicsPerSecond = 35.0;
// Quake shows each animation frame for 0.2 s (ANIM_CYCLE 2 at 10 Hz).
inline constexpr double kQuakeAnimationFrameSeconds = 0.2;
// Quake II advances the world's texture animations twice a second.
inline constexpr double kQuake2AnimationFrameSeconds = 0.5;

// --- Doom ------------------------------------------------------------------

struct DoomCompositeTexture {
	QString name;
	QSize size;
	QVector<MaterialPatchPlacement> patches;
	// TEXTURE1 or TEXTURE2.
	QString lump;
	// Position in the combined texture list (TEXTURE1 then TEXTURE2).
	int order = 0;
};

// One animated range, vanilla or Boom: every name between first and last in
// namespace order is a frame.
struct DoomAnimationRange {
	bool texture = false;
	QString first;
	QString last;
	int tics = 8;
	// "doom-vanilla" or "boom-animated".
	QString source;
	// MBF: decals may be drawn on the frames.
	bool decals = false;
};

struct DoomSwitchPair {
	QString off; // SW1...
	QString on;  // SW2...
	// 1 shareware, 2 registered, 3 commercial (Doom II); 0 any.
	int episode = 1;
	QString source;
};

// One Hexen/ZDoom ANIMDEFS entry.
struct DoomAnimdefsFrame {
	// Either a number counted from the base (pic 1 is the base itself) or
	// a name.
	int pic = 0;
	QString name;
	int tics = 8;
	int maximumTics = 0; // `rand min max`
};

struct DoomAnimdefsEntry {
	// "flat", "texture", "warp", "warp2", "switch", "cameratexture",
	// "animateddoor" and the like.
	QString kind;
	bool texture = false;
	QString base;
	bool optional = false;
	bool oscillate = false;
	bool allowDecals = false;
	QVector<DoomAnimdefsFrame> frames;
	bool range = false;
	QString rangeLast;
	int rangeTics = 8;
	int rangeMaximumTics = 0;
	// warp and warp2.
	double warpSpeed = 1.0;
	// switch: the "on" picture.
	QString switchOn;
	int line = 0;
	MaterialSourceSpan span;
};

struct DoomMaterialCatalog {
	QVector<DoomCompositeTexture> textures;
	QStringList patchNames;
	// F_START..F_END namespace order.
	QStringList flats;
	// Wall-sized pictures stored as their own lumps or files (TX_ markers,
	// ZDoom's textures/ folder).
	QStringList directTextures;
	QVector<DoomAnimationRange> ranges;
	QVector<DoomAnimdefsEntry> animdefs;
	QVector<DoomSwitchPair> switches;
	// "doom-vanilla" or "boom-animated"; "boom-switches" or "doom-vanilla".
	QString animationSource;
	QString switchSource;
	bool hasAnimdefs = false;
	QString animdefsPath;
	QString animdefsText;
	QStringList warnings;
	QVector<MaterialDiagnostic> diagnostics;

	[[nodiscard]] int textureIndex(const QString& name) const;
	[[nodiscard]] int flatIndex(const QString& name) const;
	[[nodiscard]] bool isFlat(const QString& name) const;
};

DoomMaterialCatalog readDoomMaterialCatalog(const PackageArchiveReader& reader, const PackageReadControl& control = {});

// linuxdoom-1.10 p_spec.c animdefs[] and p_switch.c alphSwitchList[].
QVector<DoomAnimationRange> doomVanillaAnimations();
QVector<DoomSwitchPair> doomVanillaSwitches();

// Boom's binary lumps. ANIMATED records are 23 bytes (type, last[9],
// first[9], speed int32), ended by type 0xFF; SWITCHES records are 20 bytes
// (off[9], on[9], episode int16), ended by episode 0.
bool parseBoomAnimatedLump(const QByteArray& bytes, QVector<DoomAnimationRange>* ranges, QString* error = nullptr);
bool parseBoomSwitchesLump(const QByteArray& bytes, QVector<DoomSwitchPair>* switches, QString* error = nullptr);
QByteArray boomAnimatedLump(const QVector<DoomAnimationRange>& ranges);
QByteArray boomSwitchesLump(const QVector<DoomSwitchPair>& switches);

// Boom's SWANTBLS text (DEFSWANI.DAT): [FLATS], [TEXTURES] and [SWITCHES]
// sections, the form Boom users edit before compiling the binary lumps.
QString swantblsText(const QVector<DoomAnimationRange>& ranges, const QVector<DoomSwitchPair>& switches);
bool parseSwantblsText(const QString& text, QVector<DoomAnimationRange>* ranges, QVector<DoomSwitchPair>* switches,
	QVector<MaterialDiagnostic>* diagnostics);

// Hexen/ZDoom ANIMDEFS, the parts that shape how a texture looks.
QVector<DoomAnimdefsEntry> parseAnimdefs(const QString& text, QVector<MaterialDiagnostic>* diagnostics = nullptr);
// One entry written back as ANIMDEFS text.
QString animdefsEntryText(const DoomAnimdefsEntry& entry);

// The material a Doom wall texture or flat becomes: its composition,
// animation (ANIMDEFS first, then the Boom or vanilla ranges) and switch.
MaterialDefinition doomMaterialDefinition(const DoomMaterialCatalog& catalog, const QString& name, bool flat);
// Every material the catalog defines: walls, then flats.
QVector<MaterialDefinition> doomMaterialDefinitions(const DoomMaterialCatalog& catalog);

// --- Quake -----------------------------------------------------------------

struct QuakeTextureName {
	QString name;
	// The name without its +N or +A prefix.
	QString base;
	// 0-9 for +0..+9, 0-9 for the alternate +a..+j; -1 when not animated.
	int frame = -1;
	bool alternate = false;
	bool liquid = false;      // `*` (and Half-Life `!`)
	bool sky = false;         // starts with "sky"
	bool masked = false;      // `{`
	bool randomTiling = false; // Half-Life `-0`..`-9`
	bool special = false;     // clip, trigger, skip, hint, origin...
};

QuakeTextureName parseQuakeTextureName(const QString& name);
// `siblings` are the other texture names in the same container; frames are
// gathered from them like Mod_LoadTextures does. Missing frames are an error,
// as they stop the engine.
MaterialDefinition quakeMaterialDefinition(const QString& name, const QStringList& siblings, const QSize& size = QSize(),
	MaterialEngine engine = MaterialEngine::Quake);
// Source-port companion images (DarkPlaces, FTE): name_glow, name_luma,
// name_norm, name_gloss, name_bump, name_pants, name_shirt.
QStringList quakeCompanionSuffixes();

// --- Quake II --------------------------------------------------------------

struct Quake2WalInfo {
	QString name;
	QString nextFrame;
	quint32 surfaceFlags = 0;
	quint32 contentFlags = 0;
	qint32 value = 0;
	QSize size;
};

bool readQuake2WalInfo(const QByteArray& bytes, Quake2WalInfo* info, QString* error = nullptr);
// Rewrites the WAL header's name, next frame, flags and value, leaving the
// pixels alone.
bool rewriteQuake2WalHeader(QByteArray* bytes, const Quake2WalInfo& info, QString* error = nullptr);
// The WAL's package path for a texture name: textures/<name>.wal.
QString quake2WalPath(const QString& textureName);
// Follows next-frame names until the chain closes, reading each WAL through
// `lookup`. At most 64 frames.
MaterialDefinition quake2MaterialDefinition(const QString& name, const Quake2WalInfo& info,
	const std::function<bool(const QString& name, Quake2WalInfo* info)>& lookup);

// Text forms of the classic metadata, so the script editor can show and
// edit them: Quake II WAL headers as JSON and Doom animations as SWANTBLS.
QString quake2WalInfoText(const Quake2WalInfo& info);
bool parseQuake2WalInfoText(const QString& text, Quake2WalInfo* info, QString* error = nullptr);

} // namespace vibestudio
