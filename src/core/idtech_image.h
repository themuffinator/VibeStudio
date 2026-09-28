#pragma once

// idTech image decoding.
//
// Format knowledge is derived from public specifications and long-standing
// community documentation rather than from any commercial game data:
// - Doom picture/flat/PLAYPAL layout: the Unofficial Doom Specs (v1.666) and
//   the Doom Wiki format pages (https://doomwiki.org/wiki/Picture_format,
//   https://doomwiki.org/wiki/Flat, https://doomwiki.org/wiki/PLAYPAL).
// - Quake LMP/WAD2 miptex layout: the Quake Specifications (Olivier Montanuy,
//   https://www.gamers.org/dEngine/quake/spec/quake-spec34/) and the released
//   id Software Quake tools source.
// - Quake II WAL layout: the Quake II file format documentation
//   (https://www.flipcode.com/archives/Quake_2_BSP_File_Format.shtml and the
//   released id Software Quake II source headers).
// - Half-Life WAD3 miptex palette tail: the Valve Developer Community WAD3
//   documentation (https://developer.valvesoftware.com/wiki/WAD).
// - Quake II .m8 / .m32 extended mip textures and the .sp2 sprite container:
//   the `m8tex_t`, `m32tex_t`, `dsprite_t` and `dsprframe_t` layouts published
//   in the GPL Quake II engine sources
//   (https://github.com/yquake2/yquake2/blob/master/src/common/header/files.h,
//   matching id Software's release at https://github.com/id-Software/Quake-2).
// - Half-Life sprite (IDSP version 2) header, texture format field and
//   embedded palette: the Half-Life SDK sprite generator
//   (https://github.com/ValveSoftware/halflife/blob/master/utils/sprgen/sprgen.c).
// - PCX layout: the ZSoft PCX File Format Technical Reference Manual.
// - Targa layout: the Truevision TGA File Format Specification 2.0.
//
// No commercial palettes, textures, or other game assets are embedded here.
// Real palettes are read at runtime from packages the user already owns; the
// built-in palettes are procedurally generated, license-clean approximations
// used only so previews remain useful before a game palette is available.

#include <QByteArray>
#include <QImage>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

class PackageArchiveReader;

enum class IdTechImageFormat {
	Unknown,
	QtNative,
	Targa,
	Pcx,
	QuakeLump,
	QuakeMipTexture,
	Quake2Wal,
	Quake2M8,
	Quake2M32,
	QuakeSprite,
	HalfLifeSprite,
	Quake2Sprite,
	DoomPatch,
	DoomFlat,
	DoomPalette,
	DoomColormap,
	Raw,
};

// A 256-entry indexed palette. `colors` is empty when the palette is invalid.
struct IdTechPalette {
	QString id;
	QString displayName;
	QString sourceDescription;
	QVector<QRgb> colors;
	int transparentIndex = -1;
	int fullbrightStartIndex = -1;
	bool generated = false;

	[[nodiscard]] bool isValid() const;
	[[nodiscard]] QRgb colorAt(int index) const;
};

struct IdTechPaletteDescriptor {
	QString id;
	QString displayName;
	QString engineFamily;
	QString description;
	int transparentIndex = -1;
	int fullbrightStartIndex = -1;
};

// Where a resolved palette came from, so the UI and CLI can be honest about
// whether a preview uses real game colours or a generated stand-in.
struct IdTechPaletteResolution {
	IdTechPalette palette;
	QString requestedPaletteId;
	QString sourceVirtualPath;
	bool fromPackage = false;
	QStringList searchedPaths;
	QStringList warnings;
};

struct IdTechImageFrame {
	QImage image;
	int originX = 0;
	int originY = 0;
	int durationMs = 0;
	QString label;
	// Quake II .sp2 frames carry no pixels: they name an external image.
	// `sourceName` is the name exactly as stored, `sourceVirtualPath` is where
	// that image was actually found, and `image` stays null when it was not.
	QString sourceName;
	QString sourceVirtualPath;
};

struct IdTechImageDecodeResult {
	bool decoded = false;
	IdTechImageFormat format = IdTechImageFormat::Unknown;
	QString formatId;
	QString formatName;
	QImage image;
	QVector<QImage> mipLevels;
	QVector<IdTechImageFrame> frames;
	// True when the frames reference images stored elsewhere (Quake II .sp2).
	// Frame images are only populated when a package reader was supplied.
	bool externalFrames = false;
	int width = 0;
	int height = 0;
	int leftOffset = 0;
	int topOffset = 0;
	bool paletted = false;
	bool hasTransparency = false;
	QString paletteId;
	bool paletteGenerated = false;
	QString paletteSourceVirtualPath;
	QString textureName;
	QString animationNextName;
	quint32 surfaceFlags = 0;
	quint32 contentFlags = 0;
	qint32 surfaceValue = 0;
	QStringList detailLines;
	QStringList warnings;
	QString error;
};

QString idTechImageFormatId(IdTechImageFormat format);
QString idTechImageFormatDisplayName(IdTechImageFormat format);
bool idTechImageFormatIsPaletted(IdTechImageFormat format);

QVector<IdTechPaletteDescriptor> idTechPaletteDescriptors();
QStringList idTechPaletteIds();
bool idTechPaletteDescriptorForId(const QString& id, IdTechPaletteDescriptor* out = nullptr);

// Deterministic, license-clean generated palette used when no game palette is
// available. Never claims to match a shipped game palette.
IdTechPalette generatedIdTechPalette(const QString& paletteId);

// 768-byte RGB triplet palettes: Doom PLAYPAL (first 256 entries) and Quake
// gfx/palette.lmp.
bool parseIdTechPaletteBytes(const QByteArray& bytes, const QString& paletteId, IdTechPalette* palette, QString* error = nullptr);

// Quake II stores its canonical palette inside pics/colormap.pcx.
bool parsePcxPalette(const QByteArray& bytes, const QString& paletteId, IdTechPalette* palette, QString* error = nullptr);

// Candidate virtual paths searched, in order, when resolving a palette for a
// given palette id.
QStringList idTechPaletteCandidatePaths(const QString& paletteId);

// Reads a real palette out of a mounted package when one is present, otherwise
// returns the generated fallback. Never fails; check `fromPackage`.
IdTechPaletteResolution resolveIdTechPalette(const PackageArchiveReader& archive, const QString& paletteId);
IdTechPaletteResolution resolveIdTechPaletteFromDirectory(const QString& directoryPath, const QString& paletteId);

IdTechImageFormat detectIdTechImageFormat(const QString& virtualPath, const QByteArray& bytes);

// Everything a decoder may need beyond the payload itself. Only Quake II .sp2
// sprites use it today: they name external frame images that have to be read
// back out of the package the sprite came from. Decoding without a reader is
// still valid and simply leaves the frame images empty.
struct IdTechImageDecodeContext {
	const PackageArchiveReader* archive = nullptr;
};

IdTechImageDecodeResult decodeIdTechImage(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette& palette, const IdTechImageDecodeContext& context = {});

// Convenience wrapper that resolves the palette from the archive first.
IdTechImageDecodeResult decodeIdTechImageFromArchive(const PackageArchiveReader& archive, const QString& virtualPath, const QString& paletteId, IdTechPaletteResolution* resolutionOut = nullptr);

// Maps an RGB image onto an indexed palette. Used by palette conversion and by
// sprite/texture authoring previews.
QImage quantizeToIdTechPalette(const QImage& source, const IdTechPalette& palette, bool dither = false);

// Renders a palette as a 16x16 swatch grid for preview surfaces.
QImage renderIdTechPaletteSwatch(const IdTechPalette& palette, int cellSize = 12);

QStringList idTechPaletteSummaryLines(const IdTechPaletteResolution& resolution);
QStringList idTechImageSummaryLines(const IdTechImageDecodeResult& result);

} // namespace vibestudio
