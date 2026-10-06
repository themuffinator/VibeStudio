#pragma once

// Generated textures for idTech games: the prompt an image model is given for
// a game texture, and the local steps that make its picture game-ready.
//
// The image model only draws (core/ai_image_transport.h). Everything after is
// deterministic and needs no AI, so any picture can take the same path
// (`texture generate --from-image`):
//  1. crop to the texture's shape,
//  2. blend the wrap edges so it tiles (two separable cross-fades, measured
//     before and after with textureSeamScore),
//  3. resample to the game's size with wrap-around area filtering,
//  4. convert through core/texture_export to the game's format and palette,
//     with its fullbright and transparency rules (miptex in a WAD2, .wal,
//     Doom flat or patch in a PWAD, TGA for Quake III),
//  5. derive the companion maps source ports read beside a texture: _norm,
//     _gloss and _glow for DarkPlaces, FTE and QuakeSpasm-family ports, and
//     _n, _s and a glow stage for ioquake3's OpenGL2 renderer.
//
// The per-surface prompt design and the idea of deriving companion layers
// from one texture follow TexAI (https://github.com/themuffinator/TexAI,
// GPL-3.0, src/core/pbr_generator.cpp at 7f56a4b, 2026-02-14); no code is
// copied from it.

#include "core/idtech_image.h"
#include "core/texture_export.h"

#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

// What a generated texture becomes in one game.
struct TextureGameProfile {
	QString id;
	QString displayName;
	// Empty for true-colour games (Quake III, generic).
	QString paletteId;
	// Wall and floor sizes the game's own textures mostly use.
	QSize wallSize;
	QSize floorSize;
	// The longest texture name the format stores.
	int maxNameLength = 15;
	// Words for the look of the game, for the prompt.
	QString eraHint;
	// Suffixes of the companion maps its source ports read.
	QString normalSuffix;
	QString glossSuffix;
	QString glowSuffix;
};

[[nodiscard]] QVector<TextureGameProfile> textureGameProfiles();
[[nodiscard]] QStringList textureGameProfileIds();
bool textureGameProfileForId(const QString& id, TextureGameProfile* out = nullptr);
// The surfaces a texture can be made for: wall, floor, ceiling, trim, panel,
// liquid, sky.
[[nodiscard]] QStringList textureGenerationSurfaceIds();

struct TextureGenerationSpec {
	// What to draw, in the user's words.
	QString prompt;
	QString game = QStringLiteral("quake");
	QString surface = QStringLiteral("wall");
	// Art direction added to the prompt: "gothic", "rusted tech", ...
	QString style;
	// The final size; empty takes the profile's size for the surface.
	QSize size;
	bool seamless = true;
	// Width of the blended band at each edge, as a percentage of the size.
	int seamBlendPercent = 18;
	bool dither = false;
	// Quake: let bright details take the palette's fullbright colours, which
	// glow in the dark. Off keeps every pixel lit by the map's lighting.
	bool fullbrights = false;
	// The texture's name in the game; empty derives one from the prompt.
	QString name;
	// Quake II and III: the folder under textures/; empty is "vibestudio".
	QString directory;
	// Derive companion maps (normal, gloss, glow) for source ports.
	bool companions = false;
	double normalStrength = 2.5;
	// Brightness (0-1) above which a colourful pixel counts as a light.
	double glowThreshold = 0.82;
};

// The name the texture gets in the game, made legal for its format: Quake's
// 15 characters (`*` for liquids, `sky` for skies), Doom's 8 upper-case
// characters, a folder-relative name for Quake II and III.
[[nodiscard]] QString textureGenerationName(const TextureGenerationSpec& spec);
// The map-facing name: textureGenerationName with the Quake II/III folder.
[[nodiscard]] QString textureGenerationMapName(const TextureGenerationSpec& spec);
// The spec for variant `index` of `count`: the same name with a number on
// the end, kept within the game's name limit. One variant keeps the spec.
[[nodiscard]] TextureGenerationSpec textureGenerationVariantSpec(const TextureGenerationSpec& spec, int index, int count);
[[nodiscard]] QSize textureGenerationOutputSize(const TextureGenerationSpec& spec);
// The shape asked of the image model: square, or 2:1 for a Quake sky.
[[nodiscard]] QSize textureGenerationRequestSize(const TextureGenerationSpec& spec);
// The prompt an image model is sent: the user's words with the surface, the
// game's look, and the rules for a usable texture (straight on, evenly lit,
// edge to edge, tileable, no text).
[[nodiscard]] QString textureGenerationPrompt(const TextureGenerationSpec& spec);
[[nodiscard]] QString textureGenerationNegativePrompt(const TextureGenerationSpec& spec);

// How visible the wrap seam is: the mean colour step across the wrap edges
// over the mean step between interior neighbours. About 1 reads as seamless;
// a hard seam scores several times that.
[[nodiscard]] double textureSeamScore(const QImage& image);
// Two separable cross-fades with a half-size offset of the image itself, so
// the wrap edges join; the band is `bandPercent` of each side.
[[nodiscard]] QImage makeTextureSeamless(const QImage& image, int bandPercent);
// Area-filtered resampling (bilinear when enlarging) in premultiplied alpha;
// with wrap, samples beyond an edge come from the other side, so a tiling
// texture stays tiling.
[[nodiscard]] QImage resampleTexture(const QImage& image, QSize size, bool wrap);
// The largest centred region with the shape of `aspect`.
[[nodiscard]] QImage cropTextureToAspect(const QImage& image, QSize aspect);

// A tangent-space normal map from the texture's brightness taken as height:
// OpenGL convention (green up), height in alpha as DarkPlaces' offset
// mapping reads it.
[[nodiscard]] QImage deriveTextureNormalMap(const QImage& image, double strength, bool wrap = true);
// Specular intensity: bright, unsaturated areas (bare metal) shine most.
[[nodiscard]] QImage deriveTextureGlossMap(const QImage& image);
// What should glow in the dark, on black. From the fullbright indices of a
// paletted result when one is given, else bright saturated pixels. Null when
// nothing glows.
[[nodiscard]] QImage deriveTextureGlowMap(const QImage& image, double threshold, const QImage& indexed = QImage(), int fullbrightStart = -1);

struct GeneratedTextureMap {
	// "normal", "gloss" or "glow".
	QString kind;
	// The file suffix the game's source ports look for.
	QString suffix;
	QImage image;
};

struct GeneratedTexture {
	bool ok = false;
	QString error;
	QString name;
	QString mapName;
	// True colour at the final size, tiling.
	QImage image;
	// As the game will show it: palette colours for paletted games.
	QImage preview;
	TextureExportOptions exportOptions;
	TextureExportResult encoded;
	QVector<GeneratedTextureMap> companions;
	double seamScoreBefore = -1.0;
	double seamScoreAfter = -1.0;
	// What was done, in order, for the report.
	QStringList steps;
	QStringList warnings;
};

// Steps 1-5 above for one picture. `palette` is the game's palette for
// paletted games; a generated stand-in is accepted with a warning.
[[nodiscard]] GeneratedTexture processGeneratedTexture(const QImage& raw, const TextureGenerationSpec& spec, const IdTechPaletteResolution& palette);

struct TextureGenerationOutput {
	// The game or project folder the texture belongs to.
	QString folder;
	// Quake and Doom: the WAD the texture is added to, created when missing.
	// Empty is <folder>/wads/vibestudio_generated.wad.
	QString wadPath;
	bool replaceExisting = false;
	bool dryRun = false;
	// Written to .vibestudio/generated/textures/<name>.json beside the
	// outputs: what made the texture (provider, model, prompts, seed).
	QJsonObject provenance;
};

struct TextureGenerationWriteReport {
	bool ok = false;
	QString error;
	// Refused because a texture or file of that name is there already;
	// replaceExisting writes over it.
	bool alreadyExists = false;
	QStringList writtenPaths;
	// The name a map uses for the texture.
	QString mapTextureName;
	// A Quake III shader written for glow or liquid surfaces.
	QString shaderText;
	QStringList notes;
};

// Writes the texture where its game reads it: a WAD2 lump (Quake), a .wal
// under textures/ (Quake II), a TGA and maybe a shader (Quake III), a flat or
// patch in a PWAD (Doom family), or a PNG (generic), then the companions and
// the provenance record. A dry run writes nothing and lists the paths.
[[nodiscard]] TextureGenerationWriteReport writeGeneratedTexture(const GeneratedTexture& texture, const TextureGenerationSpec& spec, const TextureGenerationOutput& output);

[[nodiscard]] QJsonObject textureGenerationSpecJson(const TextureGenerationSpec& spec);
bool textureGenerationSpecFromJson(const QJsonObject& object, TextureGenerationSpec* spec, QString* error = nullptr);
[[nodiscard]] QJsonObject generatedTextureJson(const GeneratedTexture& texture);
[[nodiscard]] QJsonObject textureGenerationWriteReportJson(const TextureGenerationWriteReport& report);

} // namespace vibestudio
