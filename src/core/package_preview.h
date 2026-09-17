#pragma once

#include "core/asset_tools.h"
#include "core/idtech_image.h"
#include "core/package_archive.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

namespace vibestudio {

enum class PackagePreviewKind {
	Unavailable,
	Directory,
	Text,
	Image,
	Model,
	Audio,
	Binary,
};

struct PackagePreview {
	QString virtualPath;
	PackagePreviewKind kind = PackagePreviewKind::Unavailable;
	QString title;
	QString summary;
	QString body;
	QStringList detailLines;
	QStringList rawLines;
	bool truncated = false;
	qint64 bytesRead = 0;
	qint64 totalBytes = 0;
	QString error;
	QString assetKindId;
	QStringList assetDetailLines;
	QStringList assetRawLines;
	QString imageFormat;
	QSize imageSize;
	int imageDepth = 0;
	int imageColorCount = 0;
	bool imagePaletteAware = false;
	QStringList imagePaletteLines;
	// Real decoded pixels for image entries, null when only metadata could be
	// recovered.
	QImage imagePixels;
	QString imageFormatId;
	bool imageIdTechFormat = false;
	int imageMipLevelCount = 0;
	int imageFrameCount = 0;
	// Palette actually used to decode the entry, and where it came from.
	QString imagePaletteId;
	bool imagePaletteFromPackage = false;
	bool imagePaletteGenerated = false;
	QString imagePaletteSourceVirtualPath;
	QStringList imagePaletteResolutionLines;
	QString modelFormat;
	bool modelCountsPartial = false;
	QStringList modelViewportLines;
	QStringList modelMaterialLines;
	QStringList modelAnimationLines;
	QString audioFormat;
	QString audioCodec;
	int audioChannels = 0;
	int audioSampleRate = 0;
	int audioBitsPerSample = 0;
	qint64 audioDurationMs = 0;
	AssetAudioPeaks audioPeaks;
	QStringList audioWaveformLines;
	QString textLanguageId;
	QString textLanguageName;
	QStringList textHighlightLines;
	QStringList textDiagnosticLines;
	QString textSaveState;
};

QString packagePreviewKindId(PackagePreviewKind kind);
QString packagePreviewKindDisplayName(PackagePreviewKind kind);

// `byteLimit` caps how much of an entry is sampled. Image entries are read up
// to `imageByteLimit` instead, because a decoder needs the whole payload; pass
// 0 to keep images on `byteLimit` as well.
PackagePreview buildPackageEntryPreview(const PackageArchive& archive, const QString& virtualPath, qint64 byteLimit = 65536, qint64 imageByteLimit = 64ll * 1024ll * 1024ll);

} // namespace vibestudio
