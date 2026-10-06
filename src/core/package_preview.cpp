#include "core/package_preview.h"

#include "core/asset_tools.h"
#include "core/idtech_image.h"

#include <QCoreApplication>

#include <algorithm>
#include <limits>
#include <optional>

namespace vibestudio {

namespace {

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
		lines << QCoreApplication::translate("VibeStudioPackagePreview", "... truncated after %1 bytes").arg(maxBytes);
	}
	return lines.join('\n');
}

QString sizeText(quint64 bytes)
{
	if (bytes > (quint64(1) << 53)) {
		return QCoreApplication::translate("VibeStudioPackagePreview", "%1 B").arg(bytes);
	}
	if (bytes >= 1024ll * 1024ll) {
		return QCoreApplication::translate("VibeStudioPackagePreview", "%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ll) {
		return QCoreApplication::translate("VibeStudioPackagePreview", "%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return QCoreApplication::translate("VibeStudioPackagePreview", "%1 B").arg(bytes);
}

PackagePreview unavailablePreview(const QString& virtualPath, const QString& error, std::optional<quint64> totalBytes = std::nullopt)
{
	PackagePreview preview;
	preview.virtualPath = virtualPath;
	preview.kind = PackagePreviewKind::Unavailable;
	preview.title = QCoreApplication::translate("VibeStudioPackagePreview", "Preview unavailable");
	preview.summary = error;
	preview.error = error;
	preview.totalBytesKnown = totalBytes.has_value();
	preview.totalBytes = totalBytes.value_or(0);
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Preview state: unavailable");
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Reason: %1").arg(error);
	if (totalBytes) { preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Total size: %1").arg(sizeText(*totalBytes)); }
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
	preview->audioPlaybackCandidate = analysis.audioQtPlaybackCandidate;
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

PackagePreview cancelledPreview(const QString& path, std::optional<quint64> totalBytes = std::nullopt)
{
	auto preview = unavailablePreview(path, QCoreApplication::translate("VibeStudioPackagePreview", "Preview cancelled."), totalBytes);
	preview.cancelled = true;
	return preview;
}

bool readPreviewSample(const PackageArchiveReader& archive, qsizetype index, const PackageEntry& entry,
	qint64 limit, QByteArray* bytes, QString* error, const PackageReadControl& control)
{
	const auto cancelled = [&] { return control.isCancelled && control.isCancelled(); };
	if (cancelled()) { return false; }
	const auto expected = entry.sizeBytes;
	const quint64 wanted = limit < 0 ? expected : std::min(expected, static_cast<quint64>(limit));
	if (wanted > static_cast<quint64>(std::numeric_limits<qsizetype>::max())) {
		*error = QCoreApplication::translate("VibeStudioPackagePreview", "The preview sample is too large."); return false;
	}
	quint64 received = 0;
	bool prefixComplete = false, invalidSize = false;
	if (control.progress) { control.progress(entry.virtualPath, 0, static_cast<qint64>(wanted)); }
	const bool streamed = archive.streamEntryAt(index, [&](QByteArrayView chunk) {
		if (cancelled() || invalidSize) { return false; }
		if (prefixComplete) { invalidSize = true; return false; }
		if (static_cast<quint64>(chunk.size()) > expected - received) { invalidSize = true; return false; }
		received += static_cast<quint64>(chunk.size());
		const auto count = static_cast<qsizetype>(std::min<quint64>(chunk.size(), wanted - bytes->size()));
		bytes->append(chunk.data(), count);
		if (control.progress) { control.progress(entry.virtualPath, bytes->size(), static_cast<qint64>(wanted)); }
		if (cancelled()) { return false; }
		prefixComplete = static_cast<quint64>(bytes->size()) == wanted && wanted < expected;
		return !prefixComplete;
	}, error, cancelled);
	if (cancelled()) { bytes->clear(); return false; }
	if (invalidSize || static_cast<quint64>(bytes->size()) != wanted || (streamed && !prefixComplete && received != expected)) {
		*error = QCoreApplication::translate("VibeStudioPackagePreview", "The preview bytes do not match the declared entry size.");
		bytes->clear(); return false;
	}
	if (!streamed && !prefixComplete) { bytes->clear(); return false; }
	// A sink stop is intentional only for a strict prefix. A full sample must
	// let the reader finish, including any integrity failure after its last chunk.
	error->clear();
	return true;
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
		return QCoreApplication::translate("VibeStudioPackagePreview", "Unavailable");
	case PackagePreviewKind::Directory:
		return QCoreApplication::translate("VibeStudioPackagePreview", "Directory");
	case PackagePreviewKind::Text:
		return QCoreApplication::translate("VibeStudioPackagePreview", "Text");
	case PackagePreviewKind::Image:
		return QCoreApplication::translate("VibeStudioPackagePreview", "Image");
	case PackagePreviewKind::Model:
		return QCoreApplication::translate("VibeStudioPackagePreview", "Model");
	case PackagePreviewKind::Audio:
		return QCoreApplication::translate("VibeStudioPackagePreview", "Audio");
	case PackagePreviewKind::Binary:
		return QCoreApplication::translate("VibeStudioPackagePreview", "Binary");
	}
	return QCoreApplication::translate("VibeStudioPackagePreview", "Unavailable");
}

PackagePreview buildPackageEntryPreview(const PackageArchiveReader& archive, const QString& virtualPath, qint64 byteLimit, qint64 imageByteLimit, const PackageReadControl& control)
{
	if (control.isCancelled && control.isCancelled()) { return cancelledPreview(virtualPath); }
	const auto normalized = normalizePackageVirtualPath(virtualPath, false);
	qsizetype match = -1;
	const auto entries = archive.entries();
	for (qsizetype index = 0; normalized.isSafe() && index < entries.size(); ++index) {
		if (control.isCancelled && control.isCancelled()) { return cancelledPreview(virtualPath); }
		if (entries.at(index).virtualPath.compare(normalized.normalizedPath, Qt::CaseInsensitive) != 0) { continue; }
		if (match >= 0) {
			return unavailablePreview(virtualPath, QCoreApplication::translate("VibeStudioPackagePreview", "This path occurs more than once. Select a specific entry row to preview it."));
		}
		match = index;
	}
	if (match < 0) {
		return unavailablePreview(virtualPath, QCoreApplication::translate("VibeStudioPackagePreview", "Entry not found."));
	}
	return buildPackageEntryPreviewAt(archive, match, byteLimit, imageByteLimit, control);
}

PackagePreview buildPackageEntryPreviewAt(const PackageArchiveReader& archive, qsizetype entryIndex, qint64 byteLimit, qint64 imageByteLimit, const PackageReadControl& control)
{
	const auto cancelled = [&] { return control.isCancelled && control.isCancelled(); };
	if (cancelled()) { return cancelledPreview({}); }
	const auto entries = archive.entries();
	if (!archive.isOpen() || entryIndex < 0 || entryIndex >= entries.size()) {
		return unavailablePreview({}, QCoreApplication::translate("VibeStudioPackagePreview", "Entry not found."));
	}
	const PackageEntry& entry = entries.at(entryIndex);

	PackagePreview preview;
	preview.virtualPath = entry.virtualPath;
	preview.totalBytes = entry.sizeBytes;
	preview.totalBytesKnown = true;

	// Image and audio decoders need the whole payload, so those entries get
	// their own, much larger cap instead of the generic sampling limit: a
	// waveform of the first 64 KiB would stop a long sound short.
	const AssetPreviewKind candidateKind = entry.kind == PackageEntryKind::File
		? assetPreviewKindForEntry(entry.virtualPath, entry.typeHint)
		: AssetPreviewKind::Unknown;
	const bool imageCandidate = candidateKind == AssetPreviewKind::Image || candidateKind == AssetPreviewKind::Audio;
	qint64 effectiveLimit = byteLimit;
	if (imageCandidate && imageByteLimit > 0 && byteLimit >= 0) {
		effectiveLimit = std::max(byteLimit, imageByteLimit);
	}
	preview.truncated = effectiveLimit >= 0 && preview.totalBytes > static_cast<quint64>(effectiveLimit);

	if (entry.kind == PackageEntryKind::Directory) {
		preview.kind = PackagePreviewKind::Directory;
		preview.title = QCoreApplication::translate("VibeStudioPackagePreview", "Directory");
		preview.summary = QCoreApplication::translate("VibeStudioPackagePreview", "Directory placeholder generated from package entry paths.");
		preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Path: %1").arg(entry.virtualPath);
		preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Kind: directory");
		preview.rawLines = preview.detailLines;
		return preview;
	}

	if (!entry.readable) {
		return unavailablePreview(entry.virtualPath, entry.note.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePreview", "Entry bytes are not readable by the current package reader.") : entry.note, entry.sizeBytes);
	}

	QByteArray bytes;
	QString error;
	const qint64 readLimit = effectiveLimit < 0 ? -1 : std::max<qint64>(1, effectiveLimit);
	if (!readPreviewSample(archive, entryIndex, entry, readLimit, &bytes, &error, control)) {
		if (cancelled()) { return cancelledPreview(entry.virtualPath, entry.sizeBytes); }
		return unavailablePreview(entry.virtualPath, error.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePreview", "Unable to read entry bytes.") : error, entry.sizeBytes);
	}

	preview.bytesRead = bytes.size();
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Path: %1").arg(entry.virtualPath);
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Type hint: %1").arg(entry.typeHint);
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Storage: %1").arg(entry.storageMethod.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePreview", "unknown") : entry.storageMethod);
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Bytes read: %1").arg(sizeText(preview.bytesRead));
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Total size: %1").arg(sizeText(preview.totalBytes));
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Truncated: %1").arg(preview.truncated ? QCoreApplication::translate("VibeStudioPackagePreview", "yes") : QCoreApplication::translate("VibeStudioPackagePreview", "no"));

	// Resolve a real game palette out of the package before decoding, so a
	// paletted entry previews with the colours the project actually ships.
	const QString detectionPath = assetDetectionPath(entry.virtualPath, entry.typeHint);
	const IdTechImageFormat imageFormat = detectIdTechImageFormat(detectionPath, bytes);
	std::optional<IdTechPaletteResolution> paletteResolution;
	if (imageFormat != IdTechImageFormat::Unknown && idTechImageFormatIsPaletted(imageFormat)) {
		paletteResolution = resolveIdTechPalette(archive, defaultIdTechPaletteIdForImage(imageFormat, bytes.size()));
		preview.imagePaletteId = paletteResolution->palette.id;
		preview.imagePaletteFromPackage = paletteResolution->fromPackage;
		preview.imagePaletteGenerated = paletteResolution->palette.generated || !paletteResolution->fromPackage;
		preview.imagePaletteSourceVirtualPath = paletteResolution->sourceVirtualPath;
		preview.imagePaletteResolutionLines = idTechPaletteSummaryLines(*paletteResolution);
		preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Palette: %1").arg(preview.imagePaletteId.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePreview", "unknown") : preview.imagePaletteId);
		preview.detailLines << (preview.imagePaletteFromPackage
			? QCoreApplication::translate("VibeStudioPackagePreview", "Palette source: package entry %1").arg(paletteResolution->sourceVirtualPath.isEmpty() ? QCoreApplication::translate("VibeStudioPackagePreview", "(unnamed)") : paletteResolution->sourceVirtualPath)
			: QCoreApplication::translate("VibeStudioPackagePreview", "Palette source: generated fallback; this package has no matching game palette."));
	}

	if (cancelled()) { return cancelledPreview(entry.virtualPath, entry.sizeBytes); }
	const AssetAnalysis analysis = analyzeAssetSample(detectionPath, bytes, preview.totalBytes,
		paletteResolution.has_value() ? &paletteResolution->palette : nullptr);
	if (cancelled()) { return cancelledPreview(entry.virtualPath, entry.sizeBytes); }
	if (analysis.kind != AssetPreviewKind::Unknown && analysis.kind != AssetPreviewKind::Binary) {
		applyAssetAnalysis(&preview, analysis);
		return preview;
	}

	preview.kind = PackagePreviewKind::Binary;
	preview.title = QCoreApplication::translate("VibeStudioPackagePreview", "Binary metadata");
	preview.summary = QCoreApplication::translate("VibeStudioPackagePreview", "Binary entry, %1 sampled.").arg(sizeText(preview.bytesRead));
	preview.body = hexDump(bytes);
	preview.detailLines << QCoreApplication::translate("VibeStudioPackagePreview", "Binary sample bytes: %1").arg(sizeText(preview.bytesRead));
	preview.rawLines = preview.detailLines;
	preview.rawLines << QCoreApplication::translate("VibeStudioPackagePreview", "Hex sample:");
	preview.rawLines << preview.body;
	if (analysis.kind == AssetPreviewKind::Binary) {
		preview.assetKindId = analysis.kindId;
		preview.assetDetailLines = analysis.detailLines;
		preview.assetRawLines = analysis.rawLines;
	}
	return preview;
}

} // namespace vibestudio
