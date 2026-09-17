#pragma once

#include "core/idtech_image.h"
#include "core/package_archive.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class AssetPreviewKind {
	Unknown,
	Image,
	Model,
	Audio,
	Text,
	Binary,
};

// Decoded PCM envelope used by the waveform view and by CLI audio reports.
struct AssetAudioPeaks {
	bool valid = false;
	int channels = 0;
	int sampleRate = 0;
	int bitsPerSample = 0;
	qint64 durationMs = 0;
	qint64 frameCount = 0;
	// Interleaved min/max pairs in [-1, 1], `bucketCount` pairs per channel,
	// channel-major: channel 0 pairs first, then channel 1, and so on.
	QVector<float> peaks;
	int bucketCount = 0;
	QString error;
};

struct AssetAnalysis {
	AssetPreviewKind kind = AssetPreviewKind::Unknown;
	QString kindId;
	QString title;
	QString summary;
	QString body;
	QStringList detailLines;
	QStringList rawLines;
	QString error;

	QString imageFormat;
	QSize imageSize;
	int imageDepth = 0;
	int imageColorCount = 0;
	bool imageHasAlpha = false;
	bool imagePaletteAware = false;
	QStringList imagePaletteLines;
	// Real decoded pixels, empty when only header metadata could be recovered.
	QImage imagePixels;
	QString imageFormatId;
	bool imageIdTechFormat = false;
	int imageMipLevelCount = 0;
	int imageFrameCount = 0;
	int imageLeftOffset = 0;
	int imageTopOffset = 0;
	QString imagePaletteId;
	bool imagePaletteGenerated = false;
	QString imagePaletteSourceVirtualPath;
	QString imageTextureName;
	QString imageAnimationNextName;
	quint32 imageSurfaceFlags = 0;
	quint32 imageContentFlags = 0;
	qint32 imageSurfaceValue = 0;

	QString modelFormat;
	QString modelFamily;
	int modelFrameCount = 0;
	int modelSkinCount = 0;
	int modelSurfaceCount = 0;
	int modelTagCount = 0;
	int modelVertexCount = 0;
	int modelTriangleCount = 0;
	// True when the sampled byte range did not cover the whole model, so the
	// aggregated vertex/triangle/surface counts are partial.
	bool modelCountsPartial = false;
	QStringList modelSkinPaths;
	QStringList modelAnimationNames;
	QStringList modelViewportLines;
	QStringList modelMaterialLines;

	QString audioFormat;
	QString audioCodec;
	int audioChannels = 0;
	int audioSampleRate = 0;
	int audioBitsPerSample = 0;
	qint64 audioBitrateBitsPerSecond = 0;
	qint64 audioDurationMs = 0;
	qint64 audioFrameCount = 0;
	bool audioQtPlaybackCandidate = false;
	bool audioWavExportSupported = false;
	// True when WAV export needs a sample-format conversion rather than a copy.
	bool audioWavExportNeedsConversion = false;
	QStringList audioWaveformLines;
	AssetAudioPeaks audioPeaks;

	QString textLanguageId;
	QString textLanguageName;
	QString textSyntaxEngine;
	QString textSaveState;
	QStringList textHighlightLines;
	QStringList textDiagnosticLines;
};

struct AssetImageConversionRequest {
	QStringList virtualPaths;
	QString outputDirectory;
	QString outputFormat = QStringLiteral("png");
	QRect cropRect;
	QSize resizeSize;
	// "", "none" and "rgb" keep truecolor output; "grayscale" converts to 8-bit
	// grey; "indexed" and any id from idTechPaletteIds() quantize onto an
	// idTech palette.
	QString paletteMode;
	// Palette used for decoding paletted sources and for "indexed" output. When
	// empty a palette is picked from the source format and resolved against the
	// package.
	QString paletteId;
	bool dryRun = false;
	bool overwriteExisting = false;
};

struct AssetImageConversionEntryResult {
	QString virtualPath;
	QString outputPath;
	QString outputFormat;
	QSize beforeSize;
	QSize afterSize;
	qint64 inputBytes = 0;
	qint64 outputBytes = 0;
	bool dryRun = false;
	bool written = false;
	QString message;
	QString error;
	QStringList previewLines;
	QString sourceFormatId;
	QString paletteId;
	bool paletteFromPackage = false;
	bool paletteGenerated = false;
	QString paletteSourceVirtualPath;
};

struct AssetImageConversionReport {
	QString sourcePath;
	QString outputDirectory;
	int requestedCount = 0;
	int processedCount = 0;
	int writtenCount = 0;
	int errorCount = 0;
	qint64 totalInputBytes = 0;
	qint64 totalOutputBytes = 0;
	bool dryRun = false;
	QVector<AssetImageConversionEntryResult> entries;
	QStringList warnings;

	[[nodiscard]] bool succeeded() const;
};

struct AssetAudioExportReport {
	QString sourcePath;
	QString virtualPath;
	QString outputPath;
	qint64 bytes = 0;
	qint64 sourceBytes = 0;
	bool dryRun = false;
	bool written = false;
	bool converted = false;
	QString sourceFormat;
	QString conversionMode;
	QString message;
	QString error;
	QStringList detailLines;

	[[nodiscard]] bool succeeded() const;
};

struct AssetTextMatch {
	QString filePath;
	int line = 0;
	int column = 0;
	QString lineText;
};

struct AssetTextSearchRequest {
	QString rootPath;
	QString findText;
	QString replaceText;
	QStringList extensions;
	bool replace = false;
	bool dryRun = true;
	bool caseSensitive = false;
};

struct AssetTextSearchReport {
	QString rootPath;
	QString findText;
	QString replaceText;
	bool replace = false;
	bool dryRun = true;
	int filesScanned = 0;
	int filesWithMatches = 0;
	int matchCount = 0;
	int replacementCount = 0;
	QString saveState = QStringLiteral("clean");
	QVector<AssetTextMatch> matches;
	QStringList warnings;

	[[nodiscard]] bool succeeded() const;
};

QString assetPreviewKindId(AssetPreviewKind kind);
AssetPreviewKind assetPreviewKindForPath(const QString& virtualPath);

// `palette` is used when the payload turns out to be a paletted idTech image.
// When it is null a generated, license-clean palette is chosen from the
// detected format.
AssetAnalysis analyzeAssetBytes(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes = -1, const IdTechPalette* palette = nullptr);

// Palette id a paletted idTech format defaults to, so callers can resolve a
// real game palette out of the package before analysing.
QString defaultIdTechPaletteIdForFormat(IdTechImageFormat format);

// Extracts a min/max envelope from a RIFF/WAVE payload. Supports 8-bit
// unsigned, 16/24/32-bit signed PCM and 32-bit float, including
// WAVE_FORMAT_EXTENSIBLE.
AssetAudioPeaks extractWavePeaks(const QByteArray& bytes, int bucketCount = 512);

// ASCII envelope rendering used by the CLI and by compact preview panes.
QStringList assetWaveformLines(const AssetAudioPeaks& peaks, int buckets = 32, int width = 12);

AssetImageConversionReport convertPackageImages(const PackageArchive& archive, const AssetImageConversionRequest& request);
QString assetImageConversionReportText(const AssetImageConversionReport& report);

AssetAudioExportReport exportPackageAudioToWav(const PackageArchive& archive, const QString& virtualPath, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false);
QString assetAudioExportReportText(const AssetAudioExportReport& report);

AssetTextSearchReport findReplaceProjectText(const AssetTextSearchRequest& request);
QString assetTextSearchReportText(const AssetTextSearchReport& report);

} // namespace vibestudio
