#pragma once

#include "core/texture_document.h"

#include <QJsonObject>

namespace vibestudio {

enum class TextureExportFormat { Png, IndexedPng, Targa, Dds, Ftx, Pcx, QuakeMiptex, QuakeWad2, Quake2Wal, DoomFlat, DoomPatch };
enum class TextureExportAlpha { Strict, Matte, Threshold };
enum class TextureMipmapFilter { Box, Nearest };
enum class TextureFullbrightMode { Preserve, Exclude, Allow };

struct TextureExportProfile {
	TextureExportFormat format;
	QString id, name, suffix, description;
	bool indexed = false;
	bool mipmapped = false;
};
QVector<TextureExportProfile> textureExportProfiles();
QString textureExportFormatId(TextureExportFormat format);
bool textureExportFormatFromId(const QString& id, TextureExportFormat* format);
QString textureExportSuffix(TextureExportFormat format);

struct TextureExportOptions {
	TextureExportFormat format = TextureExportFormat::Png;
	TextureExportAlpha alpha = TextureExportAlpha::Strict;
	QColor matte = Qt::black;
	int alphaThreshold = 128;
	bool dither = false;
	bool allowGeneratedPalette = false;
	// Allows documented source-port limits, including tall Doom patch posts.
	bool extendedLimits = false;
	TextureMipmapFilter mipFilter = TextureMipmapFilter::Box;
	TextureFullbrightMode fullbright = TextureFullbrightMode::Preserve;
	QString name = QStringLiteral("texture"), animationNext;
	quint32 surfaceFlags = 0, contentFlags = 0;
	qint32 surfaceValue = 0;
	int leftOffset = 0, topOffset = 0;
};

QJsonObject textureExportOptionsJson(const TextureExportOptions& options);
bool textureExportOptionsFromJson(const QJsonObject& object, TextureExportOptions* options, QString* error = nullptr);
// Import supported native metadata; mip levels are regenerated from edited pixels.
TextureExportOptions textureExportOptionsForImage(const IdTechImageDecodeResult& decoded);

struct TextureExportResult {
	bool succeeded = false;
	QByteArray bytes;
	QImage preview;
	QVector<QImage> mipLevels;
	QStringList warnings;
	QString error;
	qint64 colorChangedPixels = 0, alphaChangedPixels = 0;
	bool preservedIndices = false;
};

// Encodes a composite snapshot. Never edits layers or changes saved state.
TextureExportResult encodeTextureExport(const QImage& image, const TextureExportOptions& options,
	const IdTechPaletteResolution& palette = {}, const TextureProgress& progress = {});
bool saveTextureExport(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette,
	const QString& path, bool overwrite, bool dryRun, TextureExportResult* result = nullptr, QString* error = nullptr, const TextureProgress& progress = {});
QJsonObject textureExportReportJson(const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureExportResult& result);

// Stages a successful encoder result without writing a temporary asset. WAD2
// accepts miptexture lumps; Doom WADs place flats/patches in their namespaces.
bool stageTextureExport(const TextureExportResult& result, const TextureExportOptions& options, const QString& path,
	PackageStagingModel* staging, bool replaceExisting, QString* error = nullptr);
bool textureExportSupportsQuake3Map(TextureExportFormat format);

} // namespace vibestudio
