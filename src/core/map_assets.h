#pragma once

// Map texture auditing: which textures a level asks for, where it asks for
// them, and whether the packages the project mounts actually provide them.
//
// `LevelMapDocument` records every texture name it parses, but nothing checked
// those names against the mounted packages. This module walks the document's
// own objects (brush faces, patch shaders, Doom sidedefs and sectors), resolves
// each unique name once against a `PackageArchiveReader`, and reports the
// result as a missing-texture audit with per-use-site attribution.
//
// Naming and lookup rules are taken from public format documentation:
// - Doom sidedef/sector texture fields and the reserved `-` "no texture"
//   placeholder: https://doomwiki.org/wiki/Sidedef and
//   https://doomwiki.org/wiki/Sector. Lump names are eight characters,
//   uppercase: https://doomwiki.org/wiki/Lump.
// - Doom flats and wall patches live in their own WAD namespaces:
//   https://doomwiki.org/wiki/Flat and https://doomwiki.org/wiki/Patch.
// - Quake miptex names and the engine-handled `clip`/`skip`/`trigger`/`hint`/
//   `origin`/`*liquid`/`sky*` families: the Quake Specifications
//   (https://www.gamers.org/dEngine/quake/spec/quake-spec34/) and the released
//   id Software Quake tools sources.
// - Quake III shader scripts under `scripts/*.shader`, the `textures/common/*`
//   and `textures/editor/*` compiler families, and the `noshader` placeholder:
//   the Quake III Arena Shader Manual
//   (https://www.qeradiant.com/manual/Q3AShader_Manual/) and the released
//   Quake III Arena sources.
//
// No commercial game data is embedded here; every lookup runs against packages
// the user already owns.

#include "core/level_map.h"
#include "core/operation_state.h"
#include "core/package_archive.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

class PackageArchiveReader;

// Where one texture reference came from.
enum class MapTextureUseKind {
	Unknown,
	WorldspawnFace,
	BrushEntityFace,
	PatchShader,
	DoomSidedefUpper,
	DoomSidedefLower,
	DoomSidedefMiddle,
	DoomSectorFloor,
	DoomSectorCeiling,
};

// How a name was accounted for. `EngineHandled` names are provided by the
// engine or the compiler and are never counted as missing.
enum class MapTextureResolution {
	Missing,
	ResolvedEntry,
	ResolvedByShader,
	EngineHandled,
};

// Use sites recorded per reference. A single wall texture can appear thousands
// of times, so the audit keeps a bounded sample and reports the true total in
// `MapTextureReference::useCount`.
inline constexpr int kMapTextureUseSampleLimit = 16;

struct MapTextureUse {
	MapTextureUseKind kind = MapTextureUseKind::Unknown;
	// Brush, patch, sidedef or sector id that carries the reference.
	int objectId = -1;
	// Owning entity for brush and patch references, -1 for Doom map lumps.
	int entityId = -1;
	// Face index inside the owning brush, -1 when not a brush face.
	int faceIndex = -1;
	// Source line in the `.map` text, or 0 for binary Doom lumps.
	int line = 0;
};

struct MapTextureReference {
	// The name exactly as the map writes it.
	QString textureName;
	// Case-folded lookup and sort key: uppercase for Doom lump names,
	// lowercase with forward slashes for the Quake families.
	QString lookupKey;
	// Total use sites, including those beyond the recorded sample.
	int useCount = 0;
	// Bounded sample of use sites, in document walk order.
	QVector<MapTextureUse> uses;
	// True when `uses` was truncated.
	bool usesTruncated = false;
	// True when a real package entry or a shader declaration backs the name.
	bool resolved = false;
	// True when the engine or the compiler provides the name itself.
	bool engineHandled = false;
	MapTextureResolution resolution = MapTextureResolution::Missing;
	QString resolutionId;
	// Package entry that satisfied the lookup, or the declaring shader script
	// when `resolution` is `ResolvedByShader`.
	QString resolvedPath;
	// Mount layer id, or the source archive id when the reader has no layers.
	QString sourceLayer;
	// Populated when the resolved entry was decoded.
	bool decoded = false;
	int width = 0;
	int height = 0;
	QString formatId;
	// Ordered virtual paths that were searched.
	QStringList candidatePaths;
	// Untranslated reason token, e.g. "quake-special-name".
	QString noteId;

	// A name that neither resolved nor is provided by the engine.
	[[nodiscard]] bool isMissing() const;
};

struct MapTextureAudit {
	QString mapName;
	QString sourcePath;
	QString engineFamily;
	LevelMapFormat format = LevelMapFormat::Unknown;
	QString packageSource;
	QString paletteId;
	bool decodeRequested = false;
	// False means source admission failed or a requested folder was unavailable.
	bool sourceIndexComplete = true;
	// False when collection, reads/decoding, shader parsing or cancellation prevent a full audit.
	bool complete = true;
	bool cancelled = false;
	// Total use sites across the whole document.
	int referenceCount = 0;
	int uniqueCount = 0;
	int resolvedCount = 0;
	int missingCount = 0;
	int engineHandledCount = 0;
	int undecodableCount = 0;
	// Shader names collected from `scripts/*.shader`, Quake III only.
	int shaderNameCount = 0;
	// Sorted by `lookupKey`.
	QVector<MapTextureReference> references;
	QStringList warnings;

	[[nodiscard]] OperationState state() const;
};

QString mapTextureUseKindId(MapTextureUseKind kind);
QString mapTextureUseKindDisplayName(MapTextureUseKind kind);
QString mapTextureResolutionId(MapTextureResolution resolution);
QString mapTextureResolutionDisplayName(MapTextureResolution resolution);

// Case-folded lookup and sort key for one texture name.
QString normalizeMapTextureKey(const QString& textureName, LevelMapFormat format);

// Doom's reserved `-` placeholder, plus empty names. These are dropped before
// the audit sees them: they mean "no texture", not "missing texture".
// https://doomwiki.org/wiki/Sidedef
bool isMapTexturePlaceholder(const QString& textureName, LevelMapFormat format);

// Names the engine or the compiler provides, which must never be counted as
// missing even when no package holds them.
bool isEngineHandledMapTexture(const QString& textureName, LevelMapFormat format, const QString& engineFamily);

// Material names for package lookup. Quake III map tokens omit the textures/
// prefix that q3map2 supplies. Full paths from existing maps remain accepted.
QStringList mapTextureMaterialCandidates(const QString& textureName, LevelMapFormat format, const QString& engineFamily);

// Ordered virtual paths to try for one texture name. Empty for placeholders.
QStringList mapTextureCandidatePaths(const QString& textureName, LevelMapFormat format, const QString& engineFamily);

// Collects the shader names declared by a Quake III `.shader` script: a name at
// brace depth 0 followed by a `{` block. Line and block comments and nested
// braces are skipped. Malformed scripts warn instead of failing.
QStringList collectShaderScriptNames(const QByteArray& bytes, QStringList* warnings = nullptr, const PackageReadControl& control = {});

MapTextureAudit auditLevelMapTextures(const LevelMapDocument& document, const PackageArchiveReader& archive, bool decodeSizes = true, const PackageReadControl& control = {});
MapTextureAudit auditLevelMapTexturesInDirectories(const LevelMapDocument& document, const QStringList& roots, bool decodeSizes = true,
	const PackageIndexLimits& limits = {});

QStringList mapTextureAuditLines(const MapTextureAudit& audit);
QString mapTextureAuditText(const MapTextureAudit& audit);
QJsonObject mapTextureAuditJson(const MapTextureAudit& audit);

} // namespace vibestudio
