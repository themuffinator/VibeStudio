#include "core/package_preview.h"

#include "core/asset_tools.h"
#include "core/idtech_image.h"

#include <QCoreApplication>

#include <algorithm>
#include <limits>
#include <optional>

namespace vibestudio {

namespace {

QString previewText(const char* source)
{
	return QCoreApplication::translate("VibeStudioPackagePreview", source);
}

std::optional<PackageEntry> findEntry(const PackageArchive& archive, const QString& virtualPath)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		return std::nullopt;
	}
	const QVector<PackageEntry> entries = archive.entries();
	for (const PackageEntry& entry : entries) {
		if (entry.virtualPath.compare(normalized.normalizedPath, Qt::CaseInsensitive) == 0) {
			return entry;
		}
	}
	return std::nullopt;
}

QString hexDump(const QByteArray& bytes)
{
	QStringList lines;
	const int maxBytes = std::min<int>(bytes.size(), 256);
	for (int offset = 0; offset < maxBytes; offset += 16) {
		QStringList hex;
		QString ascii;
		for (int index = 0; index < 16 && offset + index < maxBytes; ++index) {
			const uchar value = static_cast<uchar>(bytes[offset + index]);
			hex << QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
			ascii += (value >= 0x20 && value < 0x7f) ? QChar(value) : QChar('.');
		}
		lines << QStringLiteral("%1  %2  %3")
			.arg(offset, 8, 16, QLatin1Char('0'))
			.arg(hex.join(' ').leftJustified(47, QLatin1Char(' ')))
			.arg(ascii);
	}
	if (bytes.size() > maxBytes) {
		lines << previewText("... truncated after %1 bytes").arg(maxBytes);
	}
	return lines.join('\n');
}

QString sizeText(qint64 bytes)
{
	if (bytes >= 1024ll * 1024ll) {
		return previewText("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ll) {
		return previewText("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return previewText("%1 B").arg(bytes);
}

PackagePreview unavailablePreview(const QString& virtualPath, const QString& error)
{
	PackagePreview preview;
	preview.virtualPath = virtualPath;
	preview.kind = PackagePreviewKind::Unavailable;
	preview.title = previewText("Preview unavailable");
	preview.summary = error;
	preview.error = error;
	preview.detailLines << previewText("Preview state: unavailable");
	preview.detailLines << previewText("Reason: %1").arg(error);
	preview.rawLines = preview.detailLines;
	return preview;
}

void applyAssetAnalysis(PackagePreview* preview, const AssetAnalysis& analysis)
{
	if (!preview || analysis.kind == AssetPreviewKind::Unknown) {
		return;
	}
	preview->assetKindId = analysis.kindId;
	preview->assetDetailLines = analysis.detailLines;
	preview->assetRawLines = analysis.rawLines;
	preview->summary = analysis.summary;
	preview->title = analysis.title;
	preview->body = analysis.body;
	preview->detailLines << analysis.detailLines;
	preview->rawLines = preview->detailLines;
	if (!analysis.rawLines.isEmpty()) {
		preview->rawLines << analysis.rawLines;
	}

	preview->imageFormat = analysis.imageFormat;
	preview->imageSize = analysis.imageSize;
	preview->imageDepth = analysis.imageDepth;
	preview->imageColorCount = analysis.imageColorCount;
	preview->imagePaletteAware = analysis.imagePaletteAware;
	preview->imagePaletteLines = analysis.imagePaletteLines;
	preview->imagePixels = analysis.imagePixels;
	preview->imageFormatId = analysis.imageFormatId;
	preview->imageIdTechFormat = analysis.imageIdTechFormat;
	preview->imageMipLevelCount = analysis.imageMipLevelCount;
	preview->imageFrameCount = analysis.imageFrameCount;
	// A palette the caller already resolved against the package wins; only fill
	// in what the decoder reported when nothing was resolved.
	if (preview->imagePaletteId.isEmpty() && !analysis.imagePaletteId.isEmpty()) {
		preview->imagePaletteId = analysis.imagePaletteId;
		preview->imagePaletteGenerated = analysis.imagePaletteGenerated;
		preview->imagePaletteSourceVirtualPath = analysis.imagePaletteSourceVirtualPath;
		preview->imagePaletteFromPackage = !analysis.imagePaletteSourceVirtualPath.isEmpty();
	}
	preview->modelFormat = analysis.modelFormat;
	preview->modelCountsPartial = analysis.modelCountsPartial;
	preview->modelViewportLines = analysis.modelViewportLines;
	preview->modelMaterialLines = analysis.modelMaterialLines;
	preview->modelAnimationLines = analysis.modelAnimationNames;
	preview->audioFormat = analysis.audioFormat;
	preview->audioCodec = analysis.audioCodec;
	preview->audioChannels = analysis.audioChannels;
	preview->audioSampleRate = analysis.audioSampleRate;
	preview->audioBitsPerSample = analysis.audioBitsPerSample;
	preview->audioDurationMs = analysis.audioDurationMs;
	preview->audioPeaks = analysis.audioPeaks;
	preview->audioWaveformLines = analysis.audioWaveformLines;
	preview->textLanguageId = analysis.textLanguageId;
	preview->textLanguageName = analysis.textLanguageName;
	preview->textHighlightLines = analysis.textHighlightLines;
	preview->textDiagnosticLines = analysis.textDiagnosticLines;
	preview->textSaveState = analysis.textSaveState;

	switch (analysis.kind) {
	case AssetPreviewKind::Image:
		preview->kind = PackagePreviewKind::Image;
		break;
	case AssetPreviewKind::Model:
		preview->kind = PackagePreviewKind::Model;
		break;
	case AssetPreviewKind::Audio:
		preview->kind = PackagePreviewKind::Audio;
		break;
	case AssetPreviewKind::Text:
		preview->kind = PackagePreviewKind::Text;
		break;
	case AssetPreviewKind::Binary:
		preview->kind = PackagePreviewKind::Binary;
		break;
	case AssetPreviewKind::Unknown:
		break;
	}
}

} // namespace

QString packagePreviewKindId(PackagePreviewKind kind)
{
	switch (kind) {
	case PackagePreviewKind::Unavailable:
		return QStringLiteral("unavailable");
	case PackagePreviewKind::Directory:
		return QStringLiteral("directory");
	case PackagePreviewKind::Text:
		return QStringLiteral("text");
	case PackagePreviewKind::Image:
		return QStringLiteral("image");
	case PackagePreviewKind::Model:
		return QStringLiteral("model");
	case PackagePreviewKind::Audio:
		return QStringLiteral("audio");
	case PackagePreviewKind::Binary:
		return QStringLiteral("binary");
	}
	return QStringLiteral("unavailable");
}

QString packagePreviewKindDisplayName(PackagePreviewKind kind)
{
	switch (kind) {
	case PackagePreviewKind::Unavailable:
		return previewText("Unavailable");
	case PackagePreviewKind::Directory:
		return previewText("Directory");
	case PackagePreviewKind::Text:
		return previewText("Text");
	case PackagePreviewKind::Image:
		return previewText("Image");
	case PackagePreviewKind::Model:
		return previewText("Model");
	case PackagePreviewKind::Audio:
		return previewText("Audio");
	case PackagePreviewKind::Binary:
		return previewText("Binary");
	}
	return previewText("Unavailable");
}

PackagePreview buildPackageEntryPreview(const PackageArchive& archive, const QString& virtualPath, qint64 byteLimit, qint64 imageByteLimit)
{
	const std::optional<PackageEntry> maybeEntry = findEntry(archive, virtualPath);
	if (!maybeEntry.has_value()) {
		return unavailablePreview(virtualPath, previewText("Entry not found."));
	}
	const PackageEntry& entry = maybeEntry.value();

	PackagePreview preview;
	preview.virtualPath = entry.virtualPath;
	preview.totalBytes = static_cast<qint64>(std::min<quint64>(entry.sizeBytes, static_cast<quint64>(std::numeric_limits<qint64>::max())));

	// Image decoders need the whole payload, so an image entry gets its own,
	// much larger cap instead of the generic sampling limit.
	const bool imageCandidate = entry.kind == PackageEntryKind::File
		&& assetPreviewKindForPath(entry.virtualPath) == AssetPreviewKind::Image;
	qint64 effectiveLimit = byteLimit;
	if (imageCandidate && imageByteLimit > 0 && byteLimit >= 0) {
		effectiveLimit = std::max(byteLimit, imageByteLimit);
	}
	preview.truncated = effectiveLimit >= 0 && preview.totalBytes > effectiveLimit;

	if (entry.kind == PackageEntryKind::Directory) {
		preview.kind = PackagePreviewKind::Directory;
		preview.title = previewText("Directory");
		preview.summary = previewText("Directory placeholder generated from package entry paths.");
		preview.detailLines << previewText("Path: %1").arg(entry.virtualPath);
		preview.detailLines << previewText("Kind: directory");
		preview.rawLines = preview.detailLines;
		return preview;
	}

	if (!entry.readable) {
		return unavailablePreview(entry.virtualPath, entry.note.isEmpty() ? previewText("Entry bytes are not readable by the current package reader.") : entry.note);
	}

	QByteArray bytes;
	QString error;
	const qint64 readLimit = effectiveLimit < 0 ? -1 : std::max<qint64>(1, effectiveLimit);
	if (!archive.readEntryBytes(entry.virtualPath, &bytes, &error, readLimit)) {
		return unavailablePreview(entry.virtualPath, error.isEmpty() ? previewText("Unable to read entry bytes.") : error);
	}

	preview.bytesRead = bytes.size();
	preview.detailLines << previewText("Path: %1").arg(entry.virtualPath);
	preview.detailLines << previewText("Type hint: %1").arg(entry.typeHint);
	preview.detailLines << previewText("Storage: %1").arg(entry.storageMethod.isEmpty() ? previewText("unknown") : entry.storageMethod);
	preview.detailLines << previewText("Bytes read: %1").arg(sizeText(preview.bytesRead));
	preview.detailLines << previewText("Total size: %1").arg(sizeText(preview.totalBytes));
	preview.detailLines << previewText("Truncated: %1").arg(preview.truncated ? previewText("yes") : previewText("no"));

	// Resolve a real game palette out of the package before decoding, so a
	// paletted entry previews with the colours the project actually ships.
	const IdTechImageFormat imageFormat = detectIdTechImageFormat(entry.virtualPath, bytes);
	std::optional<IdTechPaletteResolution> paletteResolution;
	if (imageFormat != IdTechImageFormat::Unknown && idTechImageFormatIsPaletted(imageFormat)) {
		paletteResolution = resolveIdTechPalette(archive, defaultIdTechPaletteIdForFormat(imageFormat));
		preview.imagePaletteId = paletteResolution->palette.id;
		preview.imagePaletteFromPackage = paletteResolution->fromPackage;
		preview.imagePaletteGenerated = paletteResolution->palette.generated || !paletteResolution->fromPackage;
		preview.imagePaletteSourceVirtualPath = paletteResolution->sourceVirtualPath;
		preview.imagePaletteResolutionLines = idTechPaletteSummaryLines(*paletteResolution);
		preview.detailLines << previewText("Palette: %1").arg(preview.imagePaletteId.isEmpty() ? previewText("unknown") : preview.imagePaletteId);
		preview.detailLines << (preview.imagePaletteFromPackage
			? previewText("Palette source: package entry %1").arg(paletteResolution->sourceVirtualPath.isEmpty() ? previewText("(unnamed)") : paletteResolution->sourceVirtualPath)
			: previewText("Palette source: generated fallback; this package has no matching game palette."));
	}

	const AssetAnalysis analysis = analyzeAssetBytes(entry.virtualPath, bytes, preview.totalBytes,
		paletteResolution.has_value() ? &paletteResolution->palette : nullptr);
	if (analysis.kind != AssetPreviewKind::Unknown && analysis.kind != AssetPreviewKind::Binary) {
		applyAssetAnalysis(&preview, analysis);
		return preview;
	}

	preview.kind = PackagePreviewKind::Binary;
	preview.title = previewText("Binary metadata");
	preview.summary = previewText("Binary entry, %1 sampled.").arg(sizeText(preview.bytesRead));
	preview.body = hexDump(bytes);
	preview.detailLines << previewText("Binary sample bytes: %1").arg(sizeText(preview.bytesRead));
	preview.rawLines = preview.detailLines;
	preview.rawLines << previewText("Hex sample:");
	preview.rawLines << preview.body;
	if (analysis.kind == AssetPreviewKind::Binary) {
		preview.assetKindId = analysis.kindId;
		preview.assetDetailLines = analysis.detailLines;
		preview.assetRawLines = analysis.rawLines;
	}
	return preview;
}

} // namespace vibestudio
