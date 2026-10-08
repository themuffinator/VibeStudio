#pragma once

// The images a material reads, found and decoded the way each engine finds
// them, ready for the material renderer.
//
// Lookup follows each engine (sources in docs/CREDITS.md, "Materials"):
// - Quake III asks for `.tga` and falls back to `.jpg` (R_LoadImage);
//   ioquake3 also tries png, pcx and bmp. Shader names without an extension
//   get `.tga`. Sky boxes are `<farbox>_rt` .. `_dn`.
// - Doom 3 lower-cases the name, defaults to `.tga` and falls back to `.jpg`
//   (R_LoadImage), runs image programs (heightmap, addnormals, ...) over the
//   decoded pixels (Image_program.cpp), and knows built-ins such as _white,
//   _flat and _quadratic (Image_init.cpp). Cube maps add _px.._nz or, for
//   cameraCubeMap, _forward.._down.
// - Doom composes wall textures from PNAMES patches, keeps flats as raw
//   indices, and shades through COLORMAP; Quake and Quake II keep palette
//   indices too, so fullbrights and colormaps work on the real indices.
//
// Images are decoded once into straight (not premultiplied) ARGB with their
// palette indices when the file was paletted. Decoding is bounded and
// cancellable; nothing here touches the GUI.

#include "core/material_classic.h"
#include "core/material_model.h"
#include "core/package_archive.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <functional>
#include <memory>

namespace vibestudio {

struct MaterialTexture {
	int width = 0;
	int height = 0;
	// Straight ARGB, top row first.
	QVector<QRgb> pixels;
	// Palette indices when the source was paletted; empty otherwise.
	QVector<quint8> indices;
	QVector<QRgb> palette;
	// Box-filtered mip levels after level 0, smallest last.
	QVector<QVector<QRgb>> mipPixels;
	QVector<QSize> mipSizes;
	// What the material wrote, and what was actually read.
	QString reference;
	QString path;
	// "image", "composite", "builtin", "generated", "program", "missing",
	// "unreadable", "video", "render-target".
	QString status;
	QString note;

	[[nodiscard]] bool isValid() const
	{
		return width > 0 && height > 0 && pixels.size() == static_cast<qsizetype>(width) * height;
	}
	[[nodiscard]] bool hasIndices() const { return isValid() && indices.size() == pixels.size() && palette.size() >= 256; }
	[[nodiscard]] bool usable() const
	{
		return isValid() && status != QStringLiteral("missing") && status != QStringLiteral("unreadable");
	}
	void buildMips();
	[[nodiscard]] QImage toImage() const;
};

using MaterialTexturePtr = std::shared_ptr<const MaterialTexture>;

// Six faces in +X, -X, +Y, -Y, +Z, -Z order (Z up, idTech axes).
struct MaterialCubeTexture {
	std::array<MaterialTexturePtr, 6> faces;
	QString reference;
	QString status;
	QString note;

	[[nodiscard]] bool isValid() const;
};

using MaterialCubeTexturePtr = std::shared_ptr<const MaterialCubeTexture>;

MaterialTexture materialTextureFromImage(const QImage& image, const QString& reference, const QString& path, const QString& status);

// Decoded images shared between materials, keyed by source revision and path,
// bounded by bytes. Safe to use from several threads.
class MaterialImageCache {
public:
	explicit MaterialImageCache(qint64 byteLimit = 256LL * 1024 * 1024);
	MaterialTexturePtr find(const QString& key) const;
	void insert(const QString& key, const MaterialTexturePtr& texture);
	void clear();
	[[nodiscard]] qint64 bytes() const;

private:
	mutable QMutex m_mutex;
	qint64 m_limit = 0;
	qint64 m_bytes = 0;
	mutable quint64 m_clock = 0;
	struct Slot {
		MaterialTexturePtr texture;
		qint64 bytes = 0;
		mutable quint64 used = 0;
	};
	QHash<QString, Slot> m_slots;
};

struct MaterialImageSource {
	// Immutable snapshots: the open package first, then fallbacks such as a
	// game installation's base packages.
	std::shared_ptr<const PackageArchiveReader> archive;
	QVector<std::shared_ptr<const PackageArchiveReader>> fallbacks;
	// Identifies the readers' contents for the cache; empty disables caching.
	QString revision;
	// Palette for paletted images; empty or "auto" picks by engine.
	QString paletteId;
	// Doom catalog when the caller already read it.
	std::shared_ptr<const DoomMaterialCatalog> doomCatalog;
	// Quake II sky for SURF_SKY previews: env/<name><suffix>.
	QString quake2Sky;
	// Doom sky texture for F_SKY1 (SKY1 by default).
	QString doomSky;
	// Doom 3: draw _default as the developer grid instead of transparent
	// black (the release build's image).
	bool developerDefault = false;
	// Quake III: also try ioquake3's extra extensions.
	bool ioquake3Extensions = true;
	int maximumDimension = 2048;
	qint64 byteBudget = 192LL * 1024 * 1024;
	MaterialImageCache* cache = nullptr;
};

struct MaterialImageSet {
	QHash<QString, MaterialTexturePtr> textures;
	QHash<QString, MaterialCubeTexturePtr> cubes;
	// The palette paletted images were decoded with, and whether it is a
	// generated stand-in.
	QVector<QRgb> palette;
	bool paletteGenerated = true;
	QString paletteSource;
	// Doom COLORMAP (34 rows of 256) and Quake gfx/colormap.lmp (64 rows of
	// 256); generated stand-ins when the package has none.
	QByteArray doomColormap;
	bool doomColormapGenerated = true;
	QByteArray quakeColormap;
	bool quakeColormapGenerated = true;
	QStringList warnings;
	bool complete = true;
	bool cancelled = false;

	[[nodiscard]] MaterialTexturePtr find(const QString& reference) const;
	[[nodiscard]] MaterialCubeTexturePtr findCube(const QString& reference) const;
	// References that resolved to nothing usable, sorted.
	[[nodiscard]] QStringList missing() const;
	[[nodiscard]] int readyCount() const;
};

// The key images and cubes are stored under for a reference.
QString materialImageKey(const QString& reference);

// Candidate package paths for an image reference, in the engine's order.
QStringList materialImageCandidates(MaterialEngine engine, const QString& reference, bool ioquake3Extensions = true);

// Quake III sky box faces for a far box base: _rt _bk _lf _ft _up _dn, in
// that order (the order tr_sky.c loads them).
QStringList quake3SkyFaceSuffixes();

// Built-in images by name: Doom 3 _white, _black, _flat, _default,
// _quadratic, _noFalloff, _fog, _ambient and the like, Quake III's
// $whiteimage and its default box image. Null for unknown names.
MaterialTexturePtr materialBuiltinTexture(const QString& name, bool developerDefault = false);
MaterialTexturePtr quake3DefaultTexture();
// A placeholder for a video stage (RoQ/CIN are not decoded): dark with a
// moving bar, so playback is visible.
MaterialTexturePtr materialVideoPlaceholder();

// Everything a material needs: stage images and animation frames, sky
// boxes, cube maps, light falloff and projection images, classic frames,
// composites, palette and colormaps.
MaterialImageSet resolveMaterialImages(const MaterialDefinition& definition, const MaterialImageSource& source,
	const std::function<bool()>& cancelled = {});

// Doom 3 image programs over decoded images (Image_program.cpp), exposed for
// tests and the graph's preview thumbnails.
QImage doom3HeightmapToNormalMap(const QImage& heightmap, double scale);
QImage doom3AddNormalMaps(const QImage& first, const QImage& second);
QImage doom3SmoothNormalMap(const QImage& normalMap);

// Decodes a Doom picture (patch) into palette indices, with -1 for holes.
bool decodeDoomPatchIndices(const QByteArray& bytes, QVector<int>* indices, QSize* size, QPoint* offset = nullptr, QString* error = nullptr);

} // namespace vibestudio
