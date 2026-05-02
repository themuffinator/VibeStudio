#pragma once

#include "core/package_archive.h"

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

	QString modelFormat;
	QString modelFamily;
	int modelFrameCount = 0;
	int modelSkinCount = 0;
	int modelSurfaceCount = 0;
	int modelTagCount = 0;
	int modelVertexCount = 0;
	int modelTriangleCount = 0;
	QStringList modelSkinPaths;
	QStringList modelAnimationNames;
	QStringList modelViewportLines;
	QStringList modelMaterialLines;

	QString audioFormat;
	int audioChannels = 0;
	int audioSampleRate = 0;
	int audioBitsPerSample = 0;
	qint64 audioDurationMs = 0;
	bool audioQtPlaybackCandidate = false;
	bool audioWavExportSupported = false;
	QStringList audioWaveformLines;

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
	QString paletteMode;
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
	bool dryRun = false;
	bool written = false;
	QString message;
	QString error;

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
AssetAnalysis analyzeAssetBytes(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes = -1);

AssetImageConversionReport convertPackageImages(const PackageArchive& archive, const AssetImageConversionRequest& request);
QString assetImageConversionReportText(const AssetImageConversionReport& report);

AssetAudioExportReport exportPackageAudioToWav(const PackageArchive& archive, const QString& virtualPath, const QString& outputPath, bool dryRun = false, bool overwriteExisting = false);
QString assetAudioExportReportText(const AssetAudioExportReport& report);

AssetTextSearchReport findReplaceProjectText(const AssetTextSearchRequest& request);
QString assetTextSearchReportText(const AssetTextSearchReport& report);

} // namespace vibestudio
