#include "core/asset_tools.h"
#include "core/asset_formats.h"
#include "core/texture_export.h"
#include "core/audio_clip.h"
#include "core/audio_decode.h"
#include "core/audio_export.h"

#include "core/idtech_image.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vibestudio {

namespace {

QString normalizedExtension(const QString& path)
{
	return QFileInfo(path).suffix().toLower();
}

bool hasExtension(const QString& path, const QStringList& extensions)
{
	return extensions.contains(normalizedExtension(path));
}

bool readLe16(const QByteArray& bytes, qsizetype offset, quint16* value)
{
	if (!value || offset < 0 || offset + 2 > bytes.size()) {
		return false;
	}
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + offset);
	*value = static_cast<quint16>(data[0] | (data[1] << 8));
	return true;
}

bool readLe32(const QByteArray& bytes, qsizetype offset, quint32* value)
{
	if (!value || offset < 0 || offset + 4 > bytes.size()) {
		return false;
	}
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + offset);
	*value = static_cast<quint32>(data[0]) | (static_cast<quint32>(data[1]) << 8) | (static_cast<quint32>(data[2]) << 16) | (static_cast<quint32>(data[3]) << 24);
	return true;
}

bool readLe64(const QByteArray& bytes, qsizetype offset, quint64* value)
{
	if (!value || offset < 0 || offset + 8 > bytes.size()) {
		return false;
	}
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + offset);
	quint64 result = 0;
	for (int index = 7; index >= 0; --index) {
		result = (result << 8) | static_cast<quint64>(data[index]);
	}
	*value = result;
	return true;
}

qint32 readLe32Signed(const QByteArray& bytes, qsizetype offset)
{
	quint32 value = 0;
	readLe32(bytes, offset, &value);
	return static_cast<qint32>(value);
}

bool readBe24(const QByteArray& bytes, qsizetype offset, quint32* value)
{
	if (!value || offset < 0 || offset + 3 > bytes.size()) {
		return false;
	}
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + offset);
	*value = (static_cast<quint32>(data[0]) << 16) | (static_cast<quint32>(data[1]) << 8) | static_cast<quint32>(data[2]);
	return true;
}

QString fixedLatin1(const QByteArray& bytes, qsizetype offset, qsizetype length)
{
	if (offset < 0 || offset >= bytes.size() || length <= 0) {
		return {};
	}
	const qsizetype available = std::min(length, bytes.size() - offset);
	qsizetype size = 0;
	while (size < available && bytes.at(offset + size) != '\0') {
		++size;
	}
	return QString::fromLatin1(bytes.constData() + offset, size).trimmed();
}

bool bytesLookTextual(const QByteArray& bytes)
{
	if (bytes.isEmpty()) {
		return true;
	}
	int printable = 0;
	int suspicious = 0;
	for (uchar byte : bytes) {
		if (byte == '\0') {
			++suspicious;
			continue;
		}
		if (byte == '\n' || byte == '\r' || byte == '\t' || (byte >= 0x20 && byte < 0x7f) || byte >= 0x80) {
			++printable;
		} else {
			++suspicious;
		}
	}
	return suspicious == 0 || static_cast<double>(printable) / static_cast<double>(bytes.size()) >= 0.88;
}

QString decodeUtf8(const QByteArray& bytes, bool* ok)
{
	QStringDecoder decoder(QStringDecoder::Utf8);
	const QString decoded = decoder.decode(bytes);
	if (ok) {
		*ok = !decoder.hasError();
	}
	return decoded;
}

QString sizeText(quint64 bytes)
{
	if (bytes > (quint64(1) << 53)) {
		return QCoreApplication::translate("VibeStudioAssetTools", "%1 B").arg(bytes);
	}
	if (bytes >= 1024ll * 1024ll) {
		return QCoreApplication::translate("VibeStudioAssetTools", "%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ll) {
		return QCoreApplication::translate("VibeStudioAssetTools", "%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return QCoreApplication::translate("VibeStudioAssetTools", "%1 B").arg(bytes);
}

QString durationText(qint64 durationMs)
{
	if (durationMs <= 0) {
		return QCoreApplication::translate("VibeStudioAssetTools", "unknown");
	}
	const double seconds = static_cast<double>(durationMs) / 1000.0;
	return QCoreApplication::translate("VibeStudioAssetTools", "%1 s").arg(seconds, 0, 'f', 2);
}

QString outputFileNameForEntry(const QString& virtualPath, const QString& outputFormat)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	const QString safePath = normalized.isSafe() ? normalized.normalizedPath : QFileInfo(virtualPath).fileName();
	QString outputPath = safePath;
	const int slash = outputPath.lastIndexOf('/');
	const QString baseName = QFileInfo(outputPath.mid(slash + 1)).completeBaseName();
	const QString directory = slash >= 0 ? outputPath.left(slash) : QString();
	const QString fileName = QStringLiteral("%1.%2").arg(baseName.isEmpty() ? QStringLiteral("asset") : baseName, outputFormat.toLower());
	return directory.isEmpty() ? fileName : QStringLiteral("%1/%2").arg(directory, fileName);
}

QByteArray imageFormatBytes(QString outputFormat)
{
	outputFormat = outputFormat.trimmed().toLower();
	if (outputFormat == QStringLiteral("jpg")) {
		outputFormat = QStringLiteral("jpeg");
	}
	return outputFormat.isEmpty() ? QByteArray("png") : outputFormat.toLatin1();
}

QStringList paletteSampleLines(const QVector<QRgb>& colors)
{
	QStringList lines;
	const int count = static_cast<int>(std::min<qsizetype>(8, colors.size()));
	for (int index = 0; index < count; ++index) {
		const QColor color(colors[index]);
		lines << QStringLiteral("#%1 %2,%3,%4")
				.arg(index, 2, 10, QLatin1Char('0'))
				.arg(color.red())
				.arg(color.green())
				.arg(color.blue());
	}
	if (colors.size() > count) {
		lines << QCoreApplication::translate("VibeStudioAssetTools", "... %1 more palette colors").arg(colors.size() - count);
	}
	return lines;
}

QStringList paletteLines(const QImage& image)
{
	return paletteSampleLines(image.colorTable());
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

AssetAnalysis analyzeQtImage(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes)
{
	QBuffer buffer;
	buffer.setData(bytes);
	buffer.open(QIODevice::ReadOnly);
	QImageReader reader(&buffer);
	const QByteArray format = reader.format();
	const QSize size = reader.size();
	if (format.isEmpty() && !size.isValid()) {
		return {};
	}

	QImage image;
	image.loadFromData(bytes);
	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Image;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Texture and image preview");
	analysis.imageFormat = QString::fromLatin1(format).toUpper();
	analysis.imageFormatId = QString::fromLatin1(format).toLower();
	analysis.imageIdTechFormat = false;
	analysis.imageSize = size.isValid() ? size : image.size();
	analysis.imageDepth = image.isNull() ? 0 : image.depth();
	analysis.imageHasAlpha = !image.isNull() && image.hasAlphaChannel();
	analysis.imageColorCount = image.isNull() ? 0 : static_cast<int>(image.colorTable().size());
	analysis.imagePaletteAware = analysis.imageColorCount > 0;
	analysis.imagePaletteLines = image.isNull() ? QStringList {} : paletteLines(image);
	analysis.imagePixels = image;
	analysis.imageFrameCount = image.isNull() ? 0 : 1;
	Q_UNUSED(virtualPath);
	Q_UNUSED(totalBytes);
	return analysis;
}

void appendImageDetailLines(AssetAnalysis* analysis, quint64 totalBytes)
{
	analysis->summary = analysis->imageSize.isValid()
		? QCoreApplication::translate("VibeStudioAssetTools", "%1 image, %2 x %3").arg(analysis->imageFormat.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "image") : analysis->imageFormat).arg(analysis->imageSize.width()).arg(analysis->imageSize.height())
		: QCoreApplication::translate("VibeStudioAssetTools", "%1 image").arg(analysis->imageFormat.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "image") : analysis->imageFormat);
	analysis->body = analysis->summary;
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Image format: %1").arg(analysis->imageFormat.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "unknown") : analysis->imageFormat);
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Dimensions: %1").arg(analysis->imageSize.isValid() ? QCoreApplication::translate("VibeStudioAssetTools", "%1 x %2 px").arg(analysis->imageSize.width()).arg(analysis->imageSize.height()) : QCoreApplication::translate("VibeStudioAssetTools", "unknown"));
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Depth: %1").arg(analysis->imageDepth > 0 ? QCoreApplication::translate("VibeStudioAssetTools", "%1 bpp").arg(analysis->imageDepth) : QCoreApplication::translate("VibeStudioAssetTools", "unknown"));
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Alpha channel: %1").arg(analysis->imageHasAlpha ? QCoreApplication::translate("VibeStudioAssetTools", "yes") : QCoreApplication::translate("VibeStudioAssetTools", "no"));
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Transparency: %1").arg(analysis->imageHasAlpha ? QCoreApplication::translate("VibeStudioAssetTools", "yes") : QCoreApplication::translate("VibeStudioAssetTools", "no"));
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Palette-aware: %1").arg(analysis->imagePaletteAware ? QCoreApplication::translate("VibeStudioAssetTools", "yes") : QCoreApplication::translate("VibeStudioAssetTools", "no"));
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Palette colors: %1").arg(analysis->imageColorCount);
	if (!analysis->imagePaletteId.isEmpty()) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Palette: %1").arg(analysis->imagePaletteId);
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Palette source: %1").arg(analysis->imagePaletteGenerated
			? QCoreApplication::translate("VibeStudioAssetTools", "generated fallback (no game palette available)")
			: (analysis->imagePaletteSourceVirtualPath.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "package") : analysis->imagePaletteSourceVirtualPath));
	}
	if (analysis->imageMipLevelCount > 0) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Mip levels: %1").arg(analysis->imageMipLevelCount);
	}
	if (analysis->imageFrameCount > 1) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Frames: %1").arg(analysis->imageFrameCount);
	}
	if (analysis->imageLeftOffset != 0 || analysis->imageTopOffset != 0) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Origin offset: %1, %2").arg(analysis->imageLeftOffset).arg(analysis->imageTopOffset);
	}
	if (!analysis->imageTextureName.isEmpty()) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Texture name: %1").arg(analysis->imageTextureName);
	}
	if (!analysis->imageAnimationNextName.isEmpty()) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Animation next: %1").arg(analysis->imageAnimationNextName);
	}
	if (analysis->imageSurfaceFlags != 0 || analysis->imageContentFlags != 0 || analysis->imageSurfaceValue != 0) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Surface flags: 0x%1").arg(analysis->imageSurfaceFlags, 8, 16, QLatin1Char('0'));
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Content flags: 0x%1").arg(analysis->imageContentFlags, 8, 16, QLatin1Char('0'));
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Surface value: %1").arg(analysis->imageSurfaceValue);
	}
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Conversion: crop, resize, palette conversion, and format export available through asset convert.");
	if (!analysis->imagePaletteLines.isEmpty()) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Palette sample:");
		analysis->detailLines << analysis->imagePaletteLines;
	}
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bytes: %1").arg(sizeText(totalBytes));
	analysis->rawLines = analysis->detailLines;
}

AssetAnalysis analyzeImage(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes, const IdTechPalette* palette)
{
	const IdTechImageFormat detected = detectIdTechImageFormat(virtualPath, bytes);
	// `Raw` is a last-resort classification, so only trust it when the path
	// really looks like an image; otherwise non-image payloads would never fall
	// through to the audio, model and text analysers.
	const bool imageExtension = assetPreviewKindForPath(virtualPath) == AssetPreviewKind::Image;
	// A bare 320x200 screen is specific enough: Heretic's and Hexen's
	// full-screen pictures have no extension to go by.
	const bool fullscreen = bytes.size() == 320 * 200;
	if (detected != IdTechImageFormat::Unknown && detected != IdTechImageFormat::QtNative
		&& (detected != IdTechImageFormat::Raw || imageExtension || fullscreen)) {
		IdTechPalette resolved;
		if (palette && palette->isValid()) {
			resolved = *palette;
		} else {
			resolved = generatedIdTechPalette(defaultIdTechPaletteIdForImage(detected, bytes.size()));
		}
		const IdTechImageDecodeResult decoded = decodeIdTechImage(virtualPath, bytes, resolved);
		if (decoded.decoded) {
			AssetAnalysis analysis;
			analysis.kind = AssetPreviewKind::Image;
			analysis.kindId = assetPreviewKindId(analysis.kind);
			analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Texture and image preview");
			analysis.imageFormat = decoded.formatName.isEmpty() ? idTechImageFormatDisplayName(decoded.format) : decoded.formatName;
			analysis.imageFormatId = decoded.formatId.isEmpty() ? idTechImageFormatId(decoded.format) : decoded.formatId;
			analysis.imageIdTechFormat = true;
			analysis.imageSize = QSize(decoded.width, decoded.height);
			if (!analysis.imageSize.isValid() && !decoded.image.isNull()) {
				analysis.imageSize = decoded.image.size();
			}
			analysis.imagePixels = decoded.image;
			analysis.imageDepth = decoded.image.isNull() ? 0 : decoded.image.depth();
			analysis.imageHasAlpha = decoded.hasTransparency || (!decoded.image.isNull() && decoded.image.hasAlphaChannel());
			analysis.imagePaletteAware = decoded.paletted;
			analysis.imageMipLevelCount = static_cast<int>(decoded.mipLevels.size());
			analysis.imageFrameCount = decoded.frames.isEmpty() ? (decoded.image.isNull() ? 0 : 1) : static_cast<int>(decoded.frames.size());
			analysis.imageLeftOffset = decoded.leftOffset;
			analysis.imageTopOffset = decoded.topOffset;
			analysis.imagePaletteId = decoded.paletteId.isEmpty() ? (decoded.paletted ? resolved.id : QString()) : decoded.paletteId;
			analysis.imagePaletteGenerated = decoded.paletteGenerated || (decoded.paletteSourceVirtualPath.isEmpty() && resolved.generated);
			analysis.imagePaletteSourceVirtualPath = decoded.paletteSourceVirtualPath;
			analysis.imageTextureName = decoded.textureName;
			analysis.imageAnimationNextName = decoded.animationNextName;
			analysis.imageSurfaceFlags = decoded.surfaceFlags;
			analysis.imageContentFlags = decoded.contentFlags;
			analysis.imageSurfaceValue = decoded.surfaceValue;
			const QVector<QRgb> imageColors = decoded.image.colorTable();
			if (!imageColors.isEmpty()) {
				analysis.imageColorCount = static_cast<int>(imageColors.size());
				analysis.imagePaletteLines = paletteSampleLines(imageColors);
			} else if (decoded.paletted) {
				analysis.imageColorCount = static_cast<int>(resolved.colors.size());
				analysis.imagePaletteLines = paletteSampleLines(resolved.colors);
			}
			appendImageDetailLines(&analysis, totalBytes);
			if (!decoded.detailLines.isEmpty()) {
				analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Format details:");
				analysis.detailLines << decoded.detailLines;
			}
			for (const QString& warning : decoded.warnings) {
				analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Warning: %1").arg(warning);
			}
			analysis.rawLines = analysis.detailLines;
			return analysis;
		}
	}

	AssetAnalysis analysis = analyzeQtImage(virtualPath, bytes, totalBytes);
	if (analysis.kind == AssetPreviewKind::Unknown) {
		return analysis;
	}
	appendImageDetailLines(&analysis, totalBytes);
	return analysis;
}

// ---------------------------------------------------------------------------
// Audio: RIFF/WAVE
// ---------------------------------------------------------------------------

// RIFF/WAVE chunk layout and WAVE_FORMAT_EXTENSIBLE follow the Microsoft
// "Multimedia Programming Interface and Data Specifications 1.0" RIFF/WAVE
// documentation and RFC 2361 wave format tag registry.
constexpr quint16 kWaveFormatPcm = 0x0001;
constexpr quint16 kWaveFormatIeeeFloat = 0x0003;
constexpr quint16 kWaveFormatAlaw = 0x0006;
constexpr quint16 kWaveFormatMulaw = 0x0007;
constexpr quint16 kWaveFormatExtensible = 0xFFFE;

struct WaveFormatInfo {
	bool valid = false;
	quint16 formatTag = 0;
	quint16 effectiveFormatTag = 0;
	quint16 channels = 0;
	quint32 sampleRate = 0;
	quint32 byteRate = 0;
	quint16 blockAlign = 0;
	quint16 bitsPerSample = 0;
	bool extensible = false;
	qint64 dataOffset = -1;
	qint64 declaredDataBytes = 0;
	qint64 availableDataBytes = 0;
	bool dataTruncated = false;
};

bool parseWaveFormat(const QByteArray& bytes, WaveFormatInfo* info)
{
	if (!info || bytes.size() < 12 || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 4) != "WAVE") {
		return false;
	}
	WaveFormatInfo parsed;
	bool haveFormat = false;
	for (qsizetype offset = 12; offset + 8 <= bytes.size();) {
		const QByteArray chunkId = bytes.mid(offset, 4);
		quint32 chunkSize = 0;
		readLe32(bytes, offset + 4, &chunkSize);
		const qsizetype payload = offset + 8;
		const qint64 available = static_cast<qint64>(bytes.size()) - static_cast<qint64>(payload);
		if (available < 0) {
			break;
		}
		if (chunkId == "fmt " && chunkSize >= 16 && available >= 16) {
			readLe16(bytes, payload, &parsed.formatTag);
			readLe16(bytes, payload + 2, &parsed.channels);
			readLe32(bytes, payload + 4, &parsed.sampleRate);
			readLe32(bytes, payload + 8, &parsed.byteRate);
			readLe16(bytes, payload + 12, &parsed.blockAlign);
			readLe16(bytes, payload + 14, &parsed.bitsPerSample);
			parsed.effectiveFormatTag = parsed.formatTag;
			if (parsed.formatTag == kWaveFormatExtensible) {
				parsed.extensible = true;
				// cbSize (2) + wValidBitsPerSample (2) + dwChannelMask (4) then a
				// 16-byte SubFormat GUID whose first two bytes are the real tag.
				quint16 extensionSize = 0;
				readLe16(bytes, payload + 16, &extensionSize);
				if (chunkSize >= 40 && available >= 40 && extensionSize >= 22) {
					quint16 subTag = 0;
					readLe16(bytes, payload + 24, &subTag);
					parsed.effectiveFormatTag = subTag;
				}
			}
			haveFormat = true;
		} else if (chunkId == "data") {
			parsed.dataOffset = static_cast<qint64>(payload);
			parsed.declaredDataBytes = static_cast<qint64>(chunkSize);
			parsed.availableDataBytes = std::min<qint64>(parsed.declaredDataBytes, available);
			parsed.dataTruncated = parsed.availableDataBytes < parsed.declaredDataBytes;
			if (haveFormat) {
				break;
			}
		}
		const qint64 advance = static_cast<qint64>(chunkSize) + (chunkSize % 2);
		if (advance <= 0) {
			break;
		}
		offset = static_cast<qsizetype>(payload + advance);
	}
	if (!haveFormat || parsed.channels == 0 || parsed.sampleRate == 0) {
		return false;
	}
	if (parsed.blockAlign == 0 && parsed.bitsPerSample > 0) {
		parsed.blockAlign = static_cast<quint16>(parsed.channels * (parsed.bitsPerSample / 8));
	}
	parsed.valid = true;
	*info = parsed;
	return true;
}

bool waveSampleFormatSupported(const WaveFormatInfo& info)
{
	switch (info.effectiveFormatTag) {
	case kWaveFormatPcm:
		return info.bitsPerSample == 8 || info.bitsPerSample == 16 || info.bitsPerSample == 24 || info.bitsPerSample == 32;
	case kWaveFormatIeeeFloat:
		return info.bitsPerSample == 32 || info.bitsPerSample == 64;
	default:
		return false;
	}
}

float decodeWaveSample(const uchar* data, quint16 formatTag, quint16 bitsPerSample)
{
	if (formatTag == kWaveFormatIeeeFloat) {
		if (bitsPerSample == 32) {
			quint32 raw = static_cast<quint32>(data[0]) | (static_cast<quint32>(data[1]) << 8) | (static_cast<quint32>(data[2]) << 16) | (static_cast<quint32>(data[3]) << 24);
			float value = 0.0f;
			std::memcpy(&value, &raw, sizeof(value));
			return std::isfinite(value) ? value : 0.0f;
		}
		if (bitsPerSample == 64) {
			quint64 raw = 0;
			for (int index = 7; index >= 0; --index) {
				raw = (raw << 8) | static_cast<quint64>(data[index]);
			}
			double value = 0.0;
			std::memcpy(&value, &raw, sizeof(value));
			return std::isfinite(value) ? static_cast<float>(value) : 0.0f;
		}
		return 0.0f;
	}
	switch (bitsPerSample) {
	case 8:
		// 8-bit RIFF PCM is unsigned with 128 as silence.
		return (static_cast<float>(data[0]) - 128.0f) / 128.0f;
	case 16: {
		const auto value = static_cast<qint16>(static_cast<quint16>(data[0] | (data[1] << 8)));
		return static_cast<float>(value) / 32768.0f;
	}
	case 24: {
		qint32 value = static_cast<qint32>(static_cast<quint32>(data[0]) | (static_cast<quint32>(data[1]) << 8) | (static_cast<quint32>(data[2]) << 16));
		if (value & 0x800000) {
			value -= 0x1000000;
		}
		return static_cast<float>(value) / 8388608.0f;
	}
	case 32: {
		const quint32 raw = static_cast<quint32>(data[0]) | (static_cast<quint32>(data[1]) << 8) | (static_cast<quint32>(data[2]) << 16) | (static_cast<quint32>(data[3]) << 24);
		return static_cast<float>(static_cast<qint32>(raw)) / 2147483648.0f;
	}
	default:
		return 0.0f;
	}
}

QString waveFormatTagName(quint16 tag)
{
	switch (tag) {
	case kWaveFormatPcm:
		return QCoreApplication::translate("VibeStudioAssetTools", "PCM");
	case kWaveFormatIeeeFloat:
		return QCoreApplication::translate("VibeStudioAssetTools", "IEEE float");
	case kWaveFormatAlaw:
		return QCoreApplication::translate("VibeStudioAssetTools", "A-law");
	case kWaveFormatMulaw:
		return QCoreApplication::translate("VibeStudioAssetTools", "mu-law");
	case kWaveFormatExtensible:
		return QCoreApplication::translate("VibeStudioAssetTools", "extensible");
	default:
		return QCoreApplication::translate("VibeStudioAssetTools", "tag %1").arg(tag);
	}
}

// ---------------------------------------------------------------------------
// Audio: compressed container headers
// ---------------------------------------------------------------------------

struct OggPageHeader {
	qsizetype payloadOffset = 0;
	qsizetype payloadSize = 0;
	quint64 granulePosition = 0;
	quint32 serialNumber = 0;
	quint32 sequenceNumber = 0;
	quint8 headerType = 0;
	int segmentCount = 0;
	bool continuesPacket = false;
};

enum class OggPageState { Complete, Incomplete, Invalid };

// RFC 3533 section 6: page boundaries come from lacing, never a marker search
// inside codec bytes. This metadata reader does not verify Ogg CRC or packets;
// complete audio import remains the responsibility of audio_decode.cpp.
OggPageState parseOggPageHeader(const QByteArray& bytes, qsizetype offset, OggPageHeader* page)
{
	if (!page || offset < 0 || offset > bytes.size()) { return OggPageState::Invalid; }
	const qsizetype remaining = bytes.size() - offset;
	if (remaining < 4) { return OggPageState::Incomplete; }
	if (std::memcmp(bytes.constData() + offset, "OggS", 4) != 0) { return OggPageState::Invalid; }
	if (remaining < 27) { return OggPageState::Incomplete; }
	OggPageHeader parsed;
	parsed.headerType = static_cast<quint8>(bytes.at(offset + 5));
	if (bytes.at(offset + 4) != 0 || (parsed.headerType & ~7)) { return OggPageState::Invalid; }
	readLe64(bytes, offset + 6, &parsed.granulePosition);
	readLe32(bytes, offset + 14, &parsed.serialNumber);
	readLe32(bytes, offset + 18, &parsed.sequenceNumber);
	parsed.segmentCount = static_cast<quint8>(bytes.at(offset + 26));
	const qsizetype headerSize = 27 + parsed.segmentCount;
	if (headerSize > remaining) { return OggPageState::Incomplete; }
	for (int index = 0; index < parsed.segmentCount; ++index) {
		const auto length = static_cast<quint8>(bytes.at(offset + 27 + index));
		parsed.payloadSize += length;
		parsed.continuesPacket = length == 255;
	}
	if (parsed.payloadSize > remaining - headerSize) { return OggPageState::Incomplete; }
	parsed.payloadOffset = offset + headerSize;
	*page = parsed;
	return OggPageState::Complete;
}

struct OggSamplePages {
	quint64 position = 0;
	quint64 finalPosition = std::numeric_limits<quint64>::max();
	bool havePosition = false;
	bool finished = false;
	bool incomplete = false;
	bool inconsistent = false;
	bool outOfRange = false;
};

OggSamplePages inspectOggSample(const QByteArray& bytes, quint32 serial)
{
	OggSamplePages result;
	quint32 sequence = 0;
	bool continuation = false;
	for (qsizetype offset = 0; offset < bytes.size();) {
		OggPageHeader page;
		const auto state = parseOggPageHeader(bytes, offset, &page);
		if (state != OggPageState::Complete) {
			result.incomplete = state == OggPageState::Incomplete;
			result.inconsistent = state == OggPageState::Invalid || result.finished;
			return result;
		}
		if (result.finished || page.serialNumber != serial || page.sequenceNumber != sequence++
			|| bool(page.headerType & 1) != continuation || bool(page.headerType & 2) != (offset == 0)) {
			result.inconsistent = true; return result;
		}
		if (page.granulePosition != std::numeric_limits<quint64>::max()) {
			if (page.granulePosition > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
				result.outOfRange = true; return result;
			}
			if (result.havePosition && page.granulePosition < result.position) {
				result.inconsistent = true; return result;
			}
			result.position = page.granulePosition; result.havePosition = true;
		}
		if (page.segmentCount > 0) { continuation = page.continuesPacket; }
		result.finished = (page.headerType & 4) != 0;
		if (result.finished) {
			if (continuation) { result.inconsistent = true; return result; }
			result.finalPosition = page.granulePosition;
		}
		offset = page.payloadOffset + page.payloadSize;
	}
	return result;
}

// MPEG audio frame header tables: ISO/IEC 11172-3 / 13818-3, as documented in
// the public MPEG audio frame header reference at
// http://www.mp3-tech.org/programmer/frame_header.html
const int kMpegBitrateTable[2][3][16] = {
	// MPEG 1: layer I, layer II, layer III
	{
		{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1},
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, -1},
		{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1},
	},
	// MPEG 2 / 2.5: layer I, layer II, layer III
	{
		{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, -1},
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1},
		{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1},
	},
};

const int kMpegSampleRateTable[3][3] = {
	{44100, 48000, 32000}, // MPEG 1
	{22050, 24000, 16000}, // MPEG 2
	{11025, 12000, 8000}, // MPEG 2.5
};

struct MpegFrameHeader {
	qint64 offset = -1;
	int versionIndex = 0; // 0 = MPEG 1, 1 = MPEG 2, 2 = MPEG 2.5
	int layer = 0; // 1, 2 or 3
	int bitrateKbps = 0;
	int sampleRate = 0;
	int channelModeIndex = 0;
	int channels = 0;
	int frameBytes = 0;
	int samplesPerFrame = 0;
	bool padded = false;
	bool valid = false;
};

bool parseMpegFrameHeader(const QByteArray& bytes, qsizetype offset, MpegFrameHeader* frame)
{
	if (!frame || offset < 0 || offset + 4 > bytes.size()) {
		return false;
	}
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + offset);
	if (data[0] != 0xff || (data[1] & 0xe0) != 0xe0) {
		return false;
	}
	const int versionBits = (data[1] >> 3) & 0x03;
	const int layerBits = (data[1] >> 1) & 0x03;
	const int bitrateIndex = (data[2] >> 4) & 0x0f;
	const int sampleRateIndex = (data[2] >> 2) & 0x03;
	if (versionBits == 1 || layerBits == 0 || bitrateIndex == 0 || bitrateIndex == 15 || sampleRateIndex == 3) {
		return false;
	}
	MpegFrameHeader parsed;
	parsed.offset = offset;
	parsed.versionIndex = versionBits == 3 ? 0 : (versionBits == 2 ? 1 : 2);
	parsed.layer = 4 - layerBits;
	parsed.sampleRate = kMpegSampleRateTable[parsed.versionIndex][sampleRateIndex];
	parsed.bitrateKbps = kMpegBitrateTable[parsed.versionIndex == 0 ? 0 : 1][parsed.layer - 1][bitrateIndex];
	if (parsed.bitrateKbps <= 0 || parsed.sampleRate <= 0) {
		return false;
	}
	parsed.padded = ((data[2] >> 1) & 0x01) != 0;
	parsed.channelModeIndex = (data[3] >> 6) & 0x03;
	parsed.channels = parsed.channelModeIndex == 3 ? 1 : 2;
	if (parsed.layer == 1) {
		parsed.samplesPerFrame = 384;
		parsed.frameBytes = (12 * parsed.bitrateKbps * 1000 / parsed.sampleRate + (parsed.padded ? 1 : 0)) * 4;
	} else {
		parsed.samplesPerFrame = (parsed.layer == 3 && parsed.versionIndex != 0) ? 576 : 1152;
		parsed.frameBytes = parsed.samplesPerFrame / 8 * parsed.bitrateKbps * 1000 / parsed.sampleRate + (parsed.padded ? 1 : 0);
	}
	if (parsed.frameBytes <= 4) {
		return false;
	}
	parsed.valid = true;
	*frame = parsed;
	return true;
}

QString mpegVersionName(int versionIndex)
{
	switch (versionIndex) {
	case 0:
		return QStringLiteral("MPEG-1");
	case 1:
		return QStringLiteral("MPEG-2");
	default:
		return QStringLiteral("MPEG-2.5");
	}
}

QString mpegChannelModeName(int modeIndex)
{
	switch (modeIndex) {
	case 0:
		return QCoreApplication::translate("VibeStudioAssetTools", "stereo");
	case 1:
		return QCoreApplication::translate("VibeStudioAssetTools", "joint stereo");
	case 2:
		return QCoreApplication::translate("VibeStudioAssetTools", "dual channel");
	default:
		return QCoreApplication::translate("VibeStudioAssetTools", "mono");
	}
}

// ID3v2 tags use synchsafe 7-bit size bytes (ID3v2.4 structure, section 3.1).
qint64 id3v2TagLength(const QByteArray& bytes)
{
	if (bytes.size() < 10 || bytes.mid(0, 3) != "ID3") {
		return 0;
	}
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const quint8 flags = data[5];
	qint64 size = 0;
	for (int index = 6; index < 10; ++index) {
		size = (size << 7) | static_cast<qint64>(data[index] & 0x7f);
	}
	qint64 total = 10 + size;
	if (flags & 0x10) {
		total += 10; // footer
	}
	return total;
}

// ---------------------------------------------------------------------------
// Audio analysis
// ---------------------------------------------------------------------------

AssetAnalysis analyzeWav(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes)
{
	WaveFormatInfo info;
	if (!parseWaveFormat(bytes, &info)) {
		return {};
	}
	const qint64 bytesPerSecond = info.byteRate > 0
		? static_cast<qint64>(info.byteRate)
		: static_cast<qint64>(info.sampleRate) * info.channels * std::max<int>(1, info.bitsPerSample) / 8;
	const qint64 durationMs = bytesPerSecond > 0 ? (info.declaredDataBytes * 1000) / bytesPerSecond : 0;

	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Audio;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Audio metadata");
	analysis.audioFormat = QStringLiteral("WAV");
	analysis.audioCodec = waveFormatTagName(info.effectiveFormatTag);
	analysis.audioChannels = info.channels;
	analysis.audioSampleRate = static_cast<int>(info.sampleRate);
	analysis.audioBitsPerSample = info.bitsPerSample;
	analysis.audioBitrateBitsPerSecond = bytesPerSecond * 8;
	analysis.audioDurationMs = durationMs;
	analysis.audioFrameCount = info.blockAlign > 0 ? info.declaredDataBytes / info.blockAlign : 0;
	const bool supported = waveSampleFormatSupported(info);
	analysis.audioQtPlaybackCandidate = supported;
	analysis.audioWavExportSupported = supported;
	analysis.audioWavExportNeedsConversion = supported && !(info.effectiveFormatTag == kWaveFormatPcm && info.bitsPerSample == 16);
	analysis.summary = QCoreApplication::translate("VibeStudioAssetTools", "WAV audio, %1 Hz, %2 channel(s)").arg(info.sampleRate).arg(info.channels);
	analysis.body = analysis.summary;
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Audio format: WAV");
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Codec tag: %1").arg(info.extensible
		? QCoreApplication::translate("VibeStudioAssetTools", "%1 (WAVE_FORMAT_EXTENSIBLE)").arg(waveFormatTagName(info.effectiveFormatTag))
		: waveFormatTagName(info.formatTag));
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Channels: %1").arg(info.channels);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Sample rate: %1 Hz").arg(info.sampleRate);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bits per sample: %1").arg(info.bitsPerSample);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Frames: %1").arg(analysis.audioFrameCount);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: %1").arg(durationText(durationMs));
	analysis.detailLines << (supported
		? QCoreApplication::translate("VibeStudioAssetTools", "Playback state: decodable PCM; Qt playback still depends on the host audio backend.")
		: QCoreApplication::translate("VibeStudioAssetTools", "Playback state: unsupported WAV codec; VibeStudio cannot decode it without a codec backend."));
	if (info.dataTruncated) {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Warning: the data chunk is shorter than declared in this sample; the envelope covers the available bytes only.");
	}
	if (supported) {
		analysis.detailLines << (analysis.audioWavExportNeedsConversion
			? QCoreApplication::translate("VibeStudioAssetTools", "WAV export: supported, re-encoded to canonical 16-bit PCM")
			: QCoreApplication::translate("VibeStudioAssetTools", "WAV export: supported, copied without re-encoding"));
		analysis.audioPeaks = extractWavePeaks(bytes, 512);
		analysis.audioWaveformLines = assetWaveformLines(analysis.audioPeaks, 32, 12);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Waveform preview:");
		analysis.detailLines << analysis.audioWaveformLines;
	} else {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "WAV export: unavailable for this codec without a decoder backend.");
		analysis.audioWaveformLines << QCoreApplication::translate("VibeStudioAssetTools", "Waveform unavailable for this codec or bit depth.");
	}
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bytes: %1").arg(sizeText(totalBytes));
	analysis.rawLines = analysis.detailLines;
	Q_UNUSED(virtualPath);
	return analysis;
}

// Compute floor(value * multiplier / divisor) without overflowing an
// intermediate or narrowing an unrepresentable result into signed metadata.
bool scaledSignedCount(quint64 value, quint64 multiplier, quint64 divisor, qint64* result)
{
	if (!divisor || !multiplier) { return false; }
	const quint64 maximum = static_cast<quint64>(std::numeric_limits<qint64>::max());
	const quint64 quotient = value / divisor, remainder = value % divisor;
	if (quotient > maximum / multiplier || remainder > maximum / multiplier) { return false; }
	const quint64 whole = quotient * multiplier, fraction = remainder * multiplier / divisor;
	if (fraction > maximum - whole) { return false; }
	*result = static_cast<qint64>(whole + fraction);
	return true;
}

bool analyzeOgg(const QByteArray& bytes, quint64 totalBytes, AssetAnalysis* analysis)
{
	OggPageHeader first;
	if (parseOggPageHeader(bytes, 0, &first) != OggPageState::Complete
		|| !(first.headerType & 2) || (first.headerType & 1) || first.sequenceNumber != 0) {
		return false;
	}
	analysis->audioFormat = QStringLiteral("OGG");
	analysis->audioCodec = QCoreApplication::translate("VibeStudioAssetTools", "unknown Ogg codec");
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Container: Ogg bitstream (RFC 3533)");
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Stream serial: %1").arg(first.serialNumber);

	const QByteArray packet = bytes.mid(static_cast<int>(first.payloadOffset), static_cast<int>(std::min<qint64>(first.payloadSize, 128)));
	int sampleRate = 0;
	if (packet.size() >= 30 && static_cast<quint8>(packet.at(0)) == 0x01 && packet.mid(1, 6) == "vorbis") {
		// Vorbis I specification, section 4.2.2 (identification header).
		quint32 rate = 0;
		readLe32(packet, 12, &rate);
		const int channels = static_cast<quint8>(packet.at(11));
		quint32 version = 0; readLe32(packet, 7, &version);
		const int blockSizes = static_cast<quint8>(packet.at(28));
		const int smallBlock = blockSizes & 15, largeBlock = blockSizes >> 4;
		if (first.segmentCount != 1 || first.payloadSize != 30 || version != 0 || channels == 0 || rate == 0
			|| rate > static_cast<quint32>(std::numeric_limits<int>::max()) || smallBlock < 6 || largeBlock > 13
			|| smallBlock > largeBlock || !(static_cast<quint8>(packet.at(29)) & 1)) {
			analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Vorbis identification header is invalid or its sample rate exceeds the supported range.");
			return false;
		}
		const qint32 bitrateMaximum = readLe32Signed(packet, 16);
		const qint32 bitrateNominal = readLe32Signed(packet, 20);
		const qint32 bitrateMinimum = readLe32Signed(packet, 24);
		sampleRate = static_cast<int>(rate);
		analysis->audioCodec = QStringLiteral("Vorbis");
		analysis->audioChannels = channels;
		analysis->audioSampleRate = sampleRate;
		analysis->audioBitrateBitsPerSecond = bitrateNominal > 0 ? bitrateNominal : std::max(0, bitrateMaximum);
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Codec: Vorbis I");
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Channels: %1").arg(channels);
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Sample rate: %1 Hz").arg(sampleRate);
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bitrate (nominal): %1").arg(bitrateNominal > 0 ? QCoreApplication::translate("VibeStudioAssetTools", "%1 kbps").arg(bitrateNominal / 1000) : QCoreApplication::translate("VibeStudioAssetTools", "unspecified"));
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bitrate (minimum/maximum): %1 / %2").arg(bitrateMinimum / 1000).arg(bitrateMaximum / 1000);
	} else if (packet.size() >= 19 && packet.mid(0, 8) == "OpusHead") {
		// RFC 7845 section 5.1: Opus always decodes at 48 kHz.
		const int channels = static_cast<quint8>(packet.at(9));
		quint32 inputRate = 0;
		readLe32(packet, 12, &inputRate);
		sampleRate = 48000;
		analysis->audioCodec = QStringLiteral("Opus");
		analysis->audioChannels = channels;
		analysis->audioSampleRate = sampleRate;
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Codec: Opus (RFC 7845)");
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Channels: %1").arg(channels);
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Decoded sample rate: 48000 Hz");
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Original input rate: %1 Hz").arg(inputRate);
	} else if (packet.size() >= 9 && static_cast<quint8>(packet.at(0)) == 0x7f && packet.mid(1, 4) == "FLAC") {
		analysis->audioCodec = QStringLiteral("FLAC-in-Ogg");
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Codec: FLAC mapped into Ogg");
	} else {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Codec: unrecognized Ogg logical stream.");
	}

	// Previews hand this analyser a leading sample rather than the whole entry, so
	// the last page it can see is usually a page from the middle of the stream.
	// Unknown-size callers use the sample size. A known archive size always
	// stays unsigned so an oversized declaration cannot look like a full read.
	const bool sampleTruncated = totalBytes > static_cast<quint64>(bytes.size());
	const auto pages = inspectOggSample(bytes, first.serialNumber);
	const bool haveLast = pages.havePosition && pages.position > 0 && sampleRate > 0;
	qint64 granuleMs = 0;
	const bool durationFits = !haveLast || scaledSignedCount(pages.position, 1000, static_cast<quint64>(sampleRate), &granuleMs);
	if (pages.outOfRange || !durationFits) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown; the Ogg sample position or duration exceeds the supported range.");
	} else if (pages.inconsistent || (pages.incomplete && !sampleTruncated) || totalBytes < static_cast<quint64>(bytes.size())) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown; Ogg pages are incomplete, inconsistent, or contain more than one logical stream.");
	} else if (pages.finished && pages.finalPosition == std::numeric_limits<quint64>::max()) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown; the final Ogg page does not declare a sample position.");
	} else if (pages.finished && !pages.incomplete && !sampleTruncated && haveLast) {
		// RFC 3533: EOS belongs to one logical stream. Only a sequential walk
		// ending exactly at the end of a full sample may publish an estimate.
		// Codec pre-skip/trimming and nonzero initial offsets need packet timing.
		analysis->audioFrameCount = static_cast<qint64>(pages.position);
		analysis->audioDurationMs = granuleMs;
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration estimate: %1 (from the end-of-stream page granule position)").arg(durationText(granuleMs));
	} else if (haveLast) {
		// Keep totals unknown. A complete page before a sample cutoff carries
		// only a lower bound, including when the next page is partially sampled.
		analysis->detailLines << (sampleTruncated
			? QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown; only the first %1 was sampled. The last complete page gives a stream-position lower bound of %2.")
				.arg(sizeText(bytes.size()), durationText(granuleMs))
			: QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown; no end-of-stream page was found. The last complete page gives a stream-position lower bound of %1.")
				.arg(durationText(granuleMs)));
	} else {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown; the final Ogg page is not inside the sampled bytes.");
	}
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Header metadata only; codec trimming and stream offsets can change the playable length.");
	return true;
}

bool analyzeMp3(const QByteArray& bytes, quint64 totalBytes, AssetAnalysis* analysis)
{
	const qint64 tagLength = id3v2TagLength(bytes);
	qsizetype scanStart = static_cast<qsizetype>(std::min<qint64>(tagLength, bytes.size()));
	MpegFrameHeader frame;
	bool found = false;
	const qsizetype scanLimit = std::min<qsizetype>(bytes.size(), scanStart + 262144);
	for (qsizetype offset = scanStart; offset + 4 <= scanLimit; ++offset) {
		if (static_cast<uchar>(bytes.at(offset)) != 0xff) {
			continue;
		}
		MpegFrameHeader candidate;
		if (!parseMpegFrameHeader(bytes, offset, &candidate)) {
			continue;
		}
		// Require a second consecutive sync where the bytes are available, so a
		// random 0xFF pair inside a tag does not look like a frame.
		const qsizetype next = offset + candidate.frameBytes;
		MpegFrameHeader follower;
		if (next + 4 <= bytes.size() && !parseMpegFrameHeader(bytes, next, &follower)) {
			continue;
		}
		frame = candidate;
		found = true;
		break;
	}
	if (!found) {
		return false;
	}

	analysis->audioFormat = QStringLiteral("MP3");
	analysis->audioCodec = QCoreApplication::translate("VibeStudioAssetTools", "%1 Layer %2").arg(mpegVersionName(frame.versionIndex)).arg(frame.layer);
	analysis->audioChannels = frame.channels;
	analysis->audioSampleRate = frame.sampleRate;
	analysis->audioBitrateBitsPerSecond = static_cast<qint64>(frame.bitrateKbps) * 1000;
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Container: MPEG audio elementary stream");
	if (tagLength > 0) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "ID3v2 tag: %1 skipped").arg(sizeText(tagLength));
	}
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Codec: %1").arg(analysis->audioCodec);
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Sample rate: %1 Hz").arg(frame.sampleRate);
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Channel mode: %1").arg(mpegChannelModeName(frame.channelModeIndex));
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Channels: %1").arg(frame.channels);
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Frame bitrate: %1 kbps").arg(frame.bitrateKbps);
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "First frame offset: %1").arg(frame.offset);

	// Xing/Info sits after the side information of the first frame; VBRI sits at
	// a fixed 32-byte offset (Fraunhofer encoders).
	int sideInfoBytes = 32;
	if (frame.versionIndex == 0) {
		sideInfoBytes = frame.channels == 1 ? 17 : 32;
	} else {
		sideInfoBytes = frame.channels == 1 ? 9 : 17;
	}
	qint64 vbrFrames = 0;
	QString vbrTag;
	const qsizetype xingOffset = static_cast<qsizetype>(frame.offset) + 4 + sideInfoBytes;
	if (xingOffset + 12 <= bytes.size() && (bytes.mid(static_cast<int>(xingOffset), 4) == "Xing" || bytes.mid(static_cast<int>(xingOffset), 4) == "Info")) {
		vbrTag = QString::fromLatin1(bytes.mid(static_cast<int>(xingOffset), 4));
		// Xing header: tag(4), flag field(4, big endian); bit 0 selects the
		// frame count field that follows.
		const quint8 flagByte = static_cast<quint8>(bytes.at(xingOffset + 7));
		if (flagByte & 0x01) {
			const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + xingOffset + 8);
			vbrFrames = (static_cast<qint64>(data[0]) << 24) | (static_cast<qint64>(data[1]) << 16) | (static_cast<qint64>(data[2]) << 8) | static_cast<qint64>(data[3]);
		}
	}
	const qsizetype vbriOffset = static_cast<qsizetype>(frame.offset) + 36;
	if (vbrFrames == 0 && vbriOffset + 26 <= bytes.size() && bytes.mid(static_cast<int>(vbriOffset), 4) == "VBRI") {
		vbrTag = QStringLiteral("VBRI");
		const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + vbriOffset + 14);
		vbrFrames = (static_cast<qint64>(data[0]) << 24) | (static_cast<qint64>(data[1]) << 16) | (static_cast<qint64>(data[2]) << 8) | static_cast<qint64>(data[3]);
	}

	const quint64 streamBytes = totalBytes > static_cast<quint64>(frame.offset) ? totalBytes - static_cast<quint64>(frame.offset) : 0;
	if (vbrFrames > 0 && frame.sampleRate > 0) {
		analysis->audioFrameCount = vbrFrames * frame.samplesPerFrame;
		analysis->audioDurationMs = analysis->audioFrameCount * 1000 / frame.sampleRate;
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "VBR header: %1, %2 frames").arg(vbrTag).arg(vbrFrames);
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: %1 (from the VBR header)").arg(durationText(analysis->audioDurationMs));
	} else if (streamBytes > 0 && frame.bitrateKbps > 0
		&& scaledSignedCount(streamBytes, 8, static_cast<quint64>(frame.bitrateKbps), &analysis->audioDurationMs)) {
		// A duration can fit while its derived PCM frame count does not.
		scaledSignedCount(static_cast<quint64>(analysis->audioDurationMs), static_cast<quint64>(frame.sampleRate), 1000, &analysis->audioFrameCount);
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: %1 (estimated from size and the first frame bitrate)").arg(durationText(analysis->audioDurationMs));
	} else {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown");
	}
	return true;
}

bool analyzeFlac(const QByteArray& bytes, AssetAnalysis* analysis)
{
	if (bytes.size() < 8 || bytes.mid(0, 4) != "fLaC") {
		return false;
	}
	analysis->audioFormat = QStringLiteral("FLAC");
	analysis->audioCodec = QStringLiteral("FLAC");
	analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Container: native FLAC stream");

	// FLAC metadata block header and STREAMINFO layout: https://xiph.org/flac/format.html
	qsizetype offset = 4;
	bool streamInfo = false;
	int blocks = 0;
	while (offset + 4 <= bytes.size() && blocks < 128) {
		const quint8 header = static_cast<quint8>(bytes.at(offset));
		const bool last = (header & 0x80) != 0;
		const int blockType = header & 0x7f;
		quint32 blockLength = 0;
		readBe24(bytes, offset + 1, &blockLength);
		const qsizetype payload = offset + 4;
		if (blockType == 0 && blockLength >= 34 && payload + 34 <= bytes.size()) {
			const auto* data = reinterpret_cast<const uchar*>(bytes.constData() + payload);
			const int minBlockSize = (data[0] << 8) | data[1];
			const int maxBlockSize = (data[2] << 8) | data[3];
			const int sampleRate = (data[10] << 12) | (data[11] << 4) | (data[12] >> 4);
			const int channels = ((data[12] >> 1) & 0x07) + 1;
			const int bitsPerSample = (((data[12] & 0x01) << 4) | (data[13] >> 4)) + 1;
			quint64 totalSamples = static_cast<quint64>(data[13] & 0x0f) << 32;
			totalSamples |= static_cast<quint64>(data[14]) << 24;
			totalSamples |= static_cast<quint64>(data[15]) << 16;
			totalSamples |= static_cast<quint64>(data[16]) << 8;
			totalSamples |= static_cast<quint64>(data[17]);
			analysis->audioChannels = channels;
			analysis->audioSampleRate = sampleRate;
			analysis->audioBitsPerSample = bitsPerSample;
			analysis->audioFrameCount = static_cast<qint64>(totalSamples);
			analysis->audioDurationMs = sampleRate > 0 ? static_cast<qint64>(totalSamples) * 1000 / sampleRate : 0;
			analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Channels: %1").arg(channels);
			analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Sample rate: %1 Hz").arg(sampleRate);
			analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bits per sample: %1").arg(bitsPerSample);
			analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Block size range: %1 - %2").arg(minBlockSize).arg(maxBlockSize);
			analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Total samples: %1").arg(analysis->audioFrameCount);
			analysis->detailLines << (totalSamples > 0
				? QCoreApplication::translate("VibeStudioAssetTools", "Duration: %1 (from STREAMINFO)").arg(durationText(analysis->audioDurationMs))
				: QCoreApplication::translate("VibeStudioAssetTools", "Duration: unknown; STREAMINFO does not declare a total sample count."));
			streamInfo = true;
		}
		++blocks;
		if (last) {
			break;
		}
		offset = payload + static_cast<qsizetype>(blockLength);
	}
	if (!streamInfo) {
		analysis->detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Warning: the STREAMINFO block is not inside the sampled bytes.");
	}
	return true;
}

AssetAnalysis analyzeCompressedAudio(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes)
{
	const bool extensionMatch = hasExtension(virtualPath, {QStringLiteral("ogg"), QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("opus")});
	const bool magicMatch = bytes.startsWith("OggS") || bytes.startsWith("fLaC") || bytes.startsWith("ID3");
	if (!extensionMatch && !magicMatch) {
		return {};
	}

	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Audio;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Audio metadata");

	bool parsed = false;
	if (bytes.startsWith("OggS")) {
		parsed = analyzeOgg(bytes, totalBytes, &analysis);
	} else if (bytes.startsWith("fLaC")) {
		parsed = analyzeFlac(bytes, &analysis);
	} else {
		parsed = analyzeMp3(bytes, totalBytes, &analysis);
		if (!parsed && analysis.audioFormat.isEmpty()) {
			parsed = analyzeFlac(bytes, &analysis);
		}
	}

	if (!parsed) {
		if (!extensionMatch) {
			return {};
		}
		analysis.audioFormat = normalizedExtension(virtualPath).toUpper();
		analysis.audioCodec = QCoreApplication::translate("VibeStudioAssetTools", "unparsed");
		analysis.audioQtPlaybackCandidate = false;
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Audio format: %1").arg(analysis.audioFormat);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Header parse: failed; no valid container header was found in the sampled bytes.");
	} else {
		analysis.audioQtPlaybackCandidate = true;
		analysis.detailLines.prepend(QCoreApplication::translate("VibeStudioAssetTools", "Audio format: %1").arg(analysis.audioFormat));
	}

	analysis.summary = analysis.audioSampleRate > 0
		? QCoreApplication::translate("VibeStudioAssetTools", "%1 audio, %2 Hz, %3 channel(s)").arg(analysis.audioFormat).arg(analysis.audioSampleRate).arg(analysis.audioChannels)
		: QCoreApplication::translate("VibeStudioAssetTools", "%1 audio, header metadata only").arg(analysis.audioFormat);
	analysis.body = analysis.summary;
	analysis.detailLines << (analysis.audioQtPlaybackCandidate
		? QCoreApplication::translate("VibeStudioAssetTools", "Playback state: container header is valid; Qt playback depends on a host codec for this format.")
		: QCoreApplication::translate("VibeStudioAssetTools", "Playback state: no usable header, so playback is not offered."));
	const auto editableFormat = compressedAudioFormat(bytes);
	analysis.audioWavExportSupported = parsed && (editableFormat == QLatin1String("mp3") || editableFormat == QLatin1String("flac") || editableFormat == QLatin1String("vorbis"));
	analysis.audioWavExportNeedsConversion = analysis.audioWavExportSupported;
	analysis.detailLines << (analysis.audioWavExportSupported
		? QCoreApplication::translate("VibeStudioAssetTools", "Editing and WAV export: available after complete-stream validation within the audio editor limits.")
		: QCoreApplication::translate("VibeStudioAssetTools", "Editing and WAV export: this codec is unsupported."));
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "This browser preview reads header metadata. Open the sound in the audio editor to decode its waveform.");
	analysis.audioWaveformLines << QCoreApplication::translate("VibeStudioAssetTools", "Open in the audio editor for a decoded waveform.");
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bytes: %1").arg(sizeText(totalBytes));
	if (totalBytes > static_cast<quint64>(bytes.size())) {
		// These lines are the ones the audio surface renders, so the sampling limit
		// has to be stated here and not only on the generic preview detail list.
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Sampled: the first %1 of %2; anything past that point was not read.")
			.arg(sizeText(bytes.size()), sizeText(totalBytes));
	}
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

// ---------------------------------------------------------------------------
// Models
// ---------------------------------------------------------------------------

AssetAnalysis analyzeModel(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes)
{
	const QString ext = normalizedExtension(virtualPath);
	const bool sampleTruncated = totalBytes > 0 && totalBytes > static_cast<quint64>(bytes.size());
	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Model;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Model metadata");
	if (ext == QStringLiteral("obj")) {
		analysis.modelFormat = QStringLiteral("OBJ");
		analysis.modelFamily = QStringLiteral("Wavefront OBJ");
		analysis.modelCountsPartial = true;
		analysis.summary = QCoreApplication::translate("VibeStudioAssetTools", "Wavefront OBJ polygon model");
		analysis.body = analysis.summary;
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Geometry counts require the complete OBJ file. Open Models to decode and validate polygons, UVs, normals and material assignments.");
		analysis.modelViewportLines = analysis.detailLines;
		return analysis;
	}
	if (bytes.size() >= 72 && bytes.mid(0, 4) == "IDPO") {
		analysis.modelFormat = QStringLiteral("MDL");
		analysis.modelFamily = QStringLiteral("Quake MDL");
		analysis.modelSkinCount = readLe32Signed(bytes, 48);
		const int skinWidth = readLe32Signed(bytes, 52);
		const int skinHeight = readLe32Signed(bytes, 56);
		analysis.modelVertexCount = readLe32Signed(bytes, 60);
		analysis.modelTriangleCount = readLe32Signed(bytes, 64);
		analysis.modelFrameCount = readLe32Signed(bytes, 68);
		analysis.modelMaterialLines << QCoreApplication::translate("VibeStudioAssetTools", "Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
	} else if (bytes.size() >= 68 && bytes.mid(0, 4) == "IDP2") {
		analysis.modelFormat = QStringLiteral("MD2");
		analysis.modelFamily = QStringLiteral("Quake II MD2");
		const int skinWidth = readLe32Signed(bytes, 8);
		const int skinHeight = readLe32Signed(bytes, 12);
		const int frameSize = readLe32Signed(bytes, 16);
		analysis.modelSkinCount = readLe32Signed(bytes, 20);
		analysis.modelVertexCount = readLe32Signed(bytes, 24);
		analysis.modelTriangleCount = readLe32Signed(bytes, 32);
		analysis.modelFrameCount = readLe32Signed(bytes, 40);
		const int skinOffset = readLe32Signed(bytes, 44);
		const int frameOffset = readLe32Signed(bytes, 56);
		analysis.modelMaterialLines << QCoreApplication::translate("VibeStudioAssetTools", "Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
		for (int index = 0; index < std::min(analysis.modelSkinCount, 8); ++index) {
			const QString skin = fixedLatin1(bytes, skinOffset + index * 64, 64);
			if (!skin.isEmpty()) {
				analysis.modelSkinPaths << skin;
			}
		}
		for (int index = 0; frameSize > 0 && index < std::min(analysis.modelFrameCount, 12); ++index) {
			const QString frameName = fixedLatin1(bytes, frameOffset + index * frameSize + 24, 16);
			if (!frameName.isEmpty()) {
				analysis.modelAnimationNames << frameName;
			}
		}
	} else if (bytes.size() >= 108 && bytes.mid(0, 4) == "IDP3") {
		analysis.modelFormat = QStringLiteral("MD3");
		analysis.modelFamily = QStringLiteral("Quake III MD3");
		analysis.modelFrameCount = readLe32Signed(bytes, 76);
		analysis.modelTagCount = readLe32Signed(bytes, 80);
		analysis.modelSurfaceCount = readLe32Signed(bytes, 84);
		analysis.modelSkinCount = readLe32Signed(bytes, 88);
		const int surfaceOffset = readLe32Signed(bytes, 100);
		int offset = surfaceOffset;
		int walked = 0;
		bool walkTruncated = false;
		const int walkLimit = std::min(analysis.modelSurfaceCount, 64);
		// md3Surface_t (Quake III md3.h): ident, name[64], flags, numFrames,
		// numShaders, numVerts, numTriangles, ofsTriangles, ofsShaders, ofsSt,
		// ofsXyzNormals, ofsEnd. The header is 108 bytes and ofsEnd - the offset
		// to the next surface - sits at +104, not at the end of the struct.
		while (walked < walkLimit) {
			if (offset < 0 || offset + 108 > bytes.size()) {
				walkTruncated = true;
				break;
			}
			if (bytes.mid(offset, 4) != "IDP3") {
				walkTruncated = true;
				break;
			}
			const QString name = fixedLatin1(bytes, offset + 4, 64);
			const int shaderCount = readLe32Signed(bytes, offset + 76);
			const int vertexCount = readLe32Signed(bytes, offset + 80);
			const int triangleCount = readLe32Signed(bytes, offset + 84);
			const int nextOffset = readLe32Signed(bytes, offset + 104);
			analysis.modelVertexCount += std::max(0, vertexCount);
			analysis.modelTriangleCount += std::max(0, triangleCount);
			if (analysis.modelMaterialLines.size() < 16) {
				analysis.modelMaterialLines << QCoreApplication::translate("VibeStudioAssetTools", "Surface %1: %2 shaders, %3 vertices, %4 triangles").arg(name.isEmpty() ? QString::number(walked + 1) : name).arg(shaderCount).arg(vertexCount).arg(triangleCount);
			}
			++walked;
			if (nextOffset <= 0) {
				break;
			}
			offset += nextOffset;
		}
		if (walked < analysis.modelSurfaceCount) {
			walkTruncated = true;
		}
		if (walkTruncated) {
			analysis.modelCountsPartial = true;
			analysis.modelMaterialLines << (sampleTruncated
				? QCoreApplication::translate("VibeStudioAssetTools", "Surface walk incomplete: %1 of %2 surfaces are inside the sampled %3.").arg(walked).arg(analysis.modelSurfaceCount).arg(sizeText(bytes.size()))
				: QCoreApplication::translate("VibeStudioAssetTools", "Surface walk incomplete: only %1 of %2 surfaces could be read.").arg(walked).arg(analysis.modelSurfaceCount));
		}
	} else if (!QStringList {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm")}.contains(ext)) {
		return {};
	} else {
		analysis.modelFormat = ext.toUpper();
		analysis.modelFamily = QCoreApplication::translate("VibeStudioAssetTools", "Native idTech model boundary");
	}
	analysis.summary = QCoreApplication::translate("VibeStudioAssetTools", "%1 model, %2 frame(s)").arg(analysis.modelFormat.isEmpty() ? ext.toUpper() : analysis.modelFormat).arg(std::max(0, analysis.modelFrameCount));
	analysis.body = analysis.summary;
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Model format: %1").arg(analysis.modelFormat.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "unknown") : analysis.modelFormat);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Native loader boundary: MDL, MD2, MD3 metadata first; Assimp remains optional future import/export.");
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Frames: %1").arg(analysis.modelFrameCount);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Skins/materials: %1").arg(analysis.modelSkinCount);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Surfaces: %1").arg(analysis.modelSurfaceCount);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Tags: %1").arg(analysis.modelTagCount);
	if (analysis.modelCountsPartial) {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Vertices (partial): %1").arg(analysis.modelVertexCount);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Triangles (partial): %1").arg(analysis.modelTriangleCount);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Counts are partial: the surface list runs past the bytes available to this preview.");
	} else {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Vertices: %1").arg(analysis.modelVertexCount);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Triangles: %1").arg(analysis.modelTriangleCount);
	}
	analysis.modelViewportLines << QStringLiteral("[model viewport]");
	analysis.modelViewportLines << QCoreApplication::translate("VibeStudioAssetTools", "Format: %1").arg(analysis.modelFormat);
	analysis.modelViewportLines << QCoreApplication::translate("VibeStudioAssetTools", "Frames: %1 / Surfaces: %2").arg(analysis.modelFrameCount).arg(analysis.modelSurfaceCount);
	analysis.modelViewportLines << (analysis.modelCountsPartial
		? QCoreApplication::translate("VibeStudioAssetTools", "Verts: %1 / Tris: %2 (partial)").arg(analysis.modelVertexCount).arg(analysis.modelTriangleCount)
		: QCoreApplication::translate("VibeStudioAssetTools", "Verts: %1 / Tris: %2").arg(analysis.modelVertexCount).arg(analysis.modelTriangleCount));
	if (!analysis.modelViewportLines.isEmpty()) {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Viewport summary:");
		analysis.detailLines << analysis.modelViewportLines;
	}
	if (!analysis.modelSkinPaths.isEmpty()) {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Skin paths:");
		analysis.detailLines << analysis.modelSkinPaths;
	}
	if (!analysis.modelAnimationNames.isEmpty()) {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Animation/frame names:");
		analysis.detailLines << analysis.modelAnimationNames;
	}
	if (!analysis.modelMaterialLines.isEmpty()) {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Skin/material dependencies:");
		analysis.detailLines << analysis.modelMaterialLines;
	}
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

// ---------------------------------------------------------------------------
// Text and scripts
// ---------------------------------------------------------------------------

QString languageIdForPath(const QString& virtualPath)
{
	const QString ext = normalizedExtension(virtualPath);
	if (ext == QStringLiteral("cfg")) {
		return QStringLiteral("cfg");
	}
	if (ext == QStringLiteral("shader")) {
		return QStringLiteral("shader");
	}
	if (ext == QStringLiteral("qc") || ext == QStringLiteral("qh")) {
		return QStringLiteral("quakec");
	}
	if (QStringList {QStringLiteral("map"), QStringLiteral("def"), QStringLiteral("ent"), QStringLiteral("arena"), QStringLiteral("skin")}.contains(ext)) {
		return QStringLiteral("idtech-text");
	}
	return QStringLiteral("plain-text");
}

QString languageName(const QString& languageId)
{
	if (languageId == QStringLiteral("cfg")) {
		return QCoreApplication::translate("VibeStudioAssetTools", "CFG script");
	}
	if (languageId == QStringLiteral("shader")) {
		return QCoreApplication::translate("VibeStudioAssetTools", "idTech3 shader script");
	}
	if (languageId == QStringLiteral("quakec")) {
		return QCoreApplication::translate("VibeStudioAssetTools", "QuakeC");
	}
	if (languageId == QStringLiteral("idtech-text")) {
		return QCoreApplication::translate("VibeStudioAssetTools", "idTech text asset");
	}
	return QCoreApplication::translate("VibeStudioAssetTools", "Plain text");
}

// Console command and cvar-management keywords shared by the Quake, Quake II
// and Quake III console parsers (id Software released engine sources).
const QStringList& cfgKeywordList()
{
	static const QStringList keywords = {
		QStringLiteral("alias"), QStringLiteral("bind"), QStringLiteral("bindlist"), QStringLiteral("cmd"),
		QStringLiteral("cmdlist"), QStringLiteral("clear"), QStringLiteral("condump"), QStringLiteral("connect"),
		QStringLiteral("cvar_restart"), QStringLiteral("cvarlist"), QStringLiteral("demo"), QStringLiteral("devmap"),
		QStringLiteral("disconnect"), QStringLiteral("echo"), QStringLiteral("exec"), QStringLiteral("gameversion"),
		QStringLiteral("heartbeat"), QStringLiteral("in_restart"), QStringLiteral("kick"), QStringLiteral("killserver"),
		QStringLiteral("map"), QStringLiteral("map_restart"), QStringLiteral("messagemode"), QStringLiteral("model"),
		QStringLiteral("name"), QStringLiteral("quit"), QStringLiteral("rcon"), QStringLiteral("reconnect"),
		QStringLiteral("record"), QStringLiteral("say"), QStringLiteral("say_team"), QStringLiteral("screenshot"),
		QStringLiteral("screenshotjpeg"), QStringLiteral("seta"), QStringLiteral("setenv"), QStringLiteral("sets"),
		QStringLiteral("setu"), QStringLiteral("set"), QStringLiteral("snd_restart"), QStringLiteral("spdevmap"),
		QStringLiteral("spmap"), QStringLiteral("status"), QStringLiteral("stoprecord"), QStringLiteral("stopdemo"),
		QStringLiteral("team"), QStringLiteral("timedemo"), QStringLiteral("toggle"), QStringLiteral("toggleconsole"),
		QStringLiteral("unalias"), QStringLiteral("unbind"), QStringLiteral("unbindall"), QStringLiteral("vid_restart"),
		QStringLiteral("vstr"), QStringLiteral("wait"), QStringLiteral("writeconfig"),
	};
	return keywords;
}

// idTech3 shader directives. Names follow the Quake III Arena Shader Manual and
// the released Quake III / q3map2 shader parsers.
const QStringList& shaderGlobalKeywordList()
{
	static const QStringList keywords = {
		QStringLiteral("cull"), QStringLiteral("deformvertexes"), QStringLiteral("entitymergable"),
		QStringLiteral("fogonly"), QStringLiteral("fogparms"), QStringLiteral("light"), QStringLiteral("nomipmaps"),
		QStringLiteral("nopicmip"), QStringLiteral("polygonoffset"), QStringLiteral("portal"),
		QStringLiteral("qer_editorimage"), QStringLiteral("qer_alphafunc"), QStringLiteral("qer_nocarve"),
		QStringLiteral("qer_trans"), QStringLiteral("qer_keyword"), QStringLiteral("skyparms"),
		QStringLiteral("sort"), QStringLiteral("sunparms"), QStringLiteral("surfaceparm"),
		QStringLiteral("tesssize"), QStringLiteral("cloudparms"), QStringLiteral("implicitmap"),
		QStringLiteral("implicitblend"), QStringLiteral("implicitmask"), QStringLiteral("nocompress"),
		QStringLiteral("noshadows"), QStringLiteral("novlcollapse"), QStringLiteral("nodlight"),
		QStringLiteral("dpreflectcube"), QStringLiteral("backsided"),
	};
	return keywords;
}

const QStringList& shaderStageKeywordList()
{
	static const QStringList keywords = {
		QStringLiteral("alphafunc"), QStringLiteral("alphagen"), QStringLiteral("animmap"),
		QStringLiteral("blendfunc"), QStringLiteral("clampmap"), QStringLiteral("depthfunc"),
		QStringLiteral("depthwrite"), QStringLiteral("detail"), QStringLiteral("map"),
		QStringLiteral("rgbgen"), QStringLiteral("tcgen"), QStringLiteral("tcmod"),
		QStringLiteral("texgen"), QStringLiteral("videomap"), QStringLiteral("alphamap"),
		QStringLiteral("diffusemap"), QStringLiteral("bumpmap"), QStringLiteral("normalmap"),
		QStringLiteral("specularmap"), QStringLiteral("stage"), QStringLiteral("glossmap"),
		QStringLiteral("nextbundle"), QStringLiteral("alphashadow"),
	};
	return keywords;
}

const QStringList& quakeCKeywordList()
{
	static const QStringList keywords = {
		QStringLiteral("void"), QStringLiteral("float"), QStringLiteral("vector"), QStringLiteral("entity"),
		QStringLiteral("string"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("while"),
		QStringLiteral("do"), QStringLiteral("for"), QStringLiteral("return"), QStringLiteral("local"),
		QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("switch"), QStringLiteral("case"),
		QStringLiteral("default"), QStringLiteral("const"), QStringLiteral("var"), QStringLiteral("nosave"),
		QStringLiteral("self"), QStringLiteral("other"), QStringLiteral("world"), QStringLiteral("time"),
		QStringLiteral("frametime"), QStringLiteral("force_retouch"), QStringLiteral("mapname"),
		QStringLiteral("TRUE"), QStringLiteral("FALSE"), QStringLiteral("makevectors"),
		QStringLiteral("setorigin"), QStringLiteral("setmodel"), QStringLiteral("setsize"),
		QStringLiteral("sound"), QStringLiteral("spawn"), QStringLiteral("remove"), QStringLiteral("find"),
		QStringLiteral("findradius"), QStringLiteral("traceline"), QStringLiteral("precache_model"),
		QStringLiteral("precache_sound"), QStringLiteral("precache_file"), QStringLiteral("cvar"),
		QStringLiteral("cvar_set"), QStringLiteral("dprint"), QStringLiteral("bprint"), QStringLiteral("sprint"),
		QStringLiteral("centerprint"), QStringLiteral("vlen"), QStringLiteral("normalize"),
		QStringLiteral("random"), QStringLiteral("rint"), QStringLiteral("floor"), QStringLiteral("ceil"),
		QStringLiteral("fabs"), QStringLiteral("ftos"), QStringLiteral("vtos"), QStringLiteral("stuffcmd"),
		QStringLiteral("localcmd"), QStringLiteral("changelevel"), QStringLiteral("nextthink"),
		QStringLiteral("think"), QStringLiteral("touch"), QStringLiteral("use"), QStringLiteral("blocked"),
		QStringLiteral("movetype"), QStringLiteral("solid"), QStringLiteral("takedamage"),
	};
	return keywords;
}

QRegularExpression buildKeywordExpression(const QStringList& keywords, bool caseInsensitive)
{
	QStringList escaped;
	escaped.reserve(keywords.size());
	for (const QString& keyword : keywords) {
		escaped << QRegularExpression::escape(keyword);
	}
	QRegularExpression expression(QStringLiteral("\\b(%1)\\b").arg(escaped.join(QLatin1Char('|'))),
		caseInsensitive ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
	expression.optimize();
	return expression;
}

const QRegularExpression& cfgKeywordExpression()
{
	static const QRegularExpression expression = buildKeywordExpression(cfgKeywordList(), true);
	return expression;
}

const QRegularExpression& shaderKeywordExpression()
{
	static const QRegularExpression expression = buildKeywordExpression(shaderGlobalKeywordList() + shaderStageKeywordList(), true);
	return expression;
}

const QRegularExpression& quakeCKeywordExpression()
{
	static const QRegularExpression expression = buildKeywordExpression(quakeCKeywordList(), false);
	return expression;
}

const QRegularExpression& q3mapDirectiveExpression()
{
	static const QRegularExpression expression = [] {
		QRegularExpression pattern(QStringLiteral("\\bq3map_[A-Za-z0-9_]+"), QRegularExpression::CaseInsensitiveOption);
		pattern.optimize();
		return pattern;
	}();
	return expression;
}

const QRegularExpression* keywordExpressionForLanguage(const QString& languageId)
{
	if (languageId == QStringLiteral("cfg")) {
		return &cfgKeywordExpression();
	}
	if (languageId == QStringLiteral("shader")) {
		return &shaderKeywordExpression();
	}
	if (languageId == QStringLiteral("quakec")) {
		return &quakeCKeywordExpression();
	}
	return nullptr;
}

const QSet<QString>& shaderKnownDirectiveSet()
{
	static const QSet<QString> known = [] {
		QSet<QString> set;
		for (const QString& keyword : shaderGlobalKeywordList()) {
			set.insert(keyword);
		}
		for (const QString& keyword : shaderStageKeywordList()) {
			set.insert(keyword);
		}
		return set;
	}();
	return known;
}

constexpr int kMaxHighlightLines = 64;
constexpr int kMaxDiagnosticLines = 64;

QStringList syntaxHighlights(const QString& languageId, const QString& text)
{
	QStringList highlights;
	const QRegularExpression* keywordExpression = keywordExpressionForLanguage(languageId);
	const bool isShader = languageId == QStringLiteral("shader");
	const QStringList lines = text.split('\n');
	for (int lineIndex = 0; lineIndex < lines.size() && highlights.size() < kMaxHighlightLines; ++lineIndex) {
		const QString trimmed = lines[lineIndex].trimmed();
		if (trimmed.isEmpty()) {
			continue;
		}
		if (trimmed.startsWith(QStringLiteral("//")) || trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QStringLiteral("/*"))) {
			highlights << QCoreApplication::translate("VibeStudioAssetTools", "line %1: comment").arg(lineIndex + 1);
			continue;
		}
		if (isShader) {
			const QRegularExpressionMatch q3map = q3mapDirectiveExpression().match(trimmed);
			if (q3map.hasMatch()) {
				highlights << QCoreApplication::translate("VibeStudioAssetTools", "line %1: q3map directive %2").arg(lineIndex + 1).arg(q3map.captured(0));
				continue;
			}
		}
		if (keywordExpression) {
			const QRegularExpressionMatch match = keywordExpression->match(trimmed);
			if (match.hasMatch()) {
				highlights << QCoreApplication::translate("VibeStudioAssetTools", "line %1: keyword %2").arg(lineIndex + 1).arg(match.captured(1));
				continue;
			}
		}
		if (isShader && trimmed.endsWith(QLatin1Char('{'))) {
			highlights << QCoreApplication::translate("VibeStudioAssetTools", "line %1: shader block").arg(lineIndex + 1);
		}
	}
	return highlights;
}

// Counts unescaped double quotes so an odd result means the line leaves a
// string open. None of the idTech text formats support multi-line strings.
int unescapedQuoteCount(const QString& line)
{
	int count = 0;
	for (int index = 0; index < line.size(); ++index) {
		if (line.at(index) != QLatin1Char('"')) {
			continue;
		}
		int backslashes = 0;
		int scan = index - 1;
		while (scan >= 0 && line.at(scan) == QLatin1Char('\\')) {
			++backslashes;
			--scan;
		}
		if (backslashes % 2 == 0) {
			++count;
		}
	}
	return count;
}

QString strippedLineComment(const QString& line)
{
	const int comment = line.indexOf(QStringLiteral("//"));
	if (comment < 0) {
		return line;
	}
	// Do not treat "//" inside a quoted string as a comment.
	if (unescapedQuoteCount(line.left(comment)) % 2 != 0) {
		return line;
	}
	return line.left(comment);
}

bool looksMalformedKeyValue(const QString& trimmed)
{
	if (!trimmed.startsWith(QLatin1Char('"'))) {
		return false;
	}
	const int closing = trimmed.indexOf(QLatin1Char('"'), 1);
	if (closing < 0) {
		return true;
	}
	const QString remainder = trimmed.mid(closing + 1).trimmed();
	if (remainder.isEmpty()) {
		return true;
	}
	return !remainder.startsWith(QLatin1Char('"'));
}

QStringList textDiagnostics(const QString& languageId, const QString& text)
{
	QStringList diagnostics;
	const QStringList lines = text.split('\n');
	const bool isShader = languageId == QStringLiteral("shader");
	const bool isKeyValue = languageId == QStringLiteral("idtech-text");
	int braceBalance = 0;
	int firstUnbalancedLine = 0;
	bool inBlockComment = false;
	for (int index = 0; index < lines.size() && diagnostics.size() < kMaxDiagnosticLines; ++index) {
		const QString& rawLine = lines[index];
		if (rawLine.size() > 160) {
			diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "line %1: long line may wrap in compact editors").arg(index + 1);
		}
		if (rawLine.contains(QStringLiteral("ERROR"), Qt::CaseInsensitive) || rawLine.contains(QStringLiteral("WARNING"), Qt::CaseInsensitive)) {
			diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "line %1: compiler-style diagnostic marker").arg(index + 1);
		}
		QString line = rawLine;
		if (inBlockComment) {
			const int end = line.indexOf(QStringLiteral("*/"));
			if (end < 0) {
				continue;
			}
			line = line.mid(end + 2);
			inBlockComment = false;
		}
		const int blockStart = line.indexOf(QStringLiteral("/*"));
		if (blockStart >= 0) {
			const int blockEnd = line.indexOf(QStringLiteral("*/"), blockStart + 2);
			if (blockEnd < 0) {
				inBlockComment = true;
				line = line.left(blockStart);
			} else {
				line = line.left(blockStart) + line.mid(blockEnd + 2);
			}
		}
		line = strippedLineComment(line);
		const QString trimmed = line.trimmed();
		if (trimmed.isEmpty()) {
			continue;
		}
		if (unescapedQuoteCount(line) % 2 != 0) {
			diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "line %1: unterminated quoted string").arg(index + 1);
		}
		const int opens = static_cast<int>(line.count(QLatin1Char('{')));
		const int closes = static_cast<int>(line.count(QLatin1Char('}')));
		const int before = braceBalance;
		braceBalance += opens - closes;
		if (before == 0 && braceBalance > 0) {
			firstUnbalancedLine = index + 1;
		}
		if (braceBalance < 0) {
			diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "line %1: unexpected closing brace").arg(index + 1);
			braceBalance = 0;
			firstUnbalancedLine = 0;
		}
		if (isShader && before >= 1 && opens == 0 && closes == 0) {
			int tokenEnd = 0;
			while (tokenEnd < trimmed.size() && !trimmed.at(tokenEnd).isSpace()) {
				++tokenEnd;
			}
			const QString token = trimmed.left(tokenEnd).toLower();
			if (!token.isEmpty() && !token.startsWith(QStringLiteral("q3map_")) && !shaderKnownDirectiveSet().contains(token)) {
				diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "line %1: unknown shader directive '%2'").arg(index + 1).arg(token);
			}
		}
		if (isKeyValue && looksMalformedKeyValue(trimmed)) {
			diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "line %1: malformed key/value pair").arg(index + 1);
		}
	}
	if (braceBalance > 0) {
		diagnostics << (firstUnbalancedLine > 0
			? QCoreApplication::translate("VibeStudioAssetTools", "line %1: block opened here is never closed (brace balance %2)").arg(firstUnbalancedLine).arg(braceBalance)
			: QCoreApplication::translate("VibeStudioAssetTools", "brace balance: %1").arg(braceBalance));
	}
	if (inBlockComment) {
		diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "unterminated block comment reaches the end of the sample");
	}
	if (diagnostics.isEmpty()) {
		diagnostics << QCoreApplication::translate("VibeStudioAssetTools", "No local text diagnostics.");
	}
	return diagnostics;
}

AssetAnalysis analyzeText(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes)
{
	bool utf8Ok = false;
	const QString decoded = decodeUtf8(bytes, &utf8Ok);
	if (!utf8Ok || !bytesLookTextual(bytes)) {
		return {};
	}
	const QString languageId = languageIdForPath(virtualPath);
	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Text;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Text and script preview");
	analysis.summary = QCoreApplication::translate("VibeStudioAssetTools", "%1 preview").arg(languageName(languageId));
	analysis.body = decoded;
	analysis.textLanguageId = languageId;
	analysis.textLanguageName = languageName(languageId);
	analysis.textSyntaxEngine = QCoreApplication::translate("VibeStudioAssetTools", "VibeStudio local script analysis");
	analysis.textSaveState = QCoreApplication::translate("VibeStudioAssetTools", "clean read-only preview");
	analysis.textHighlightLines = syntaxHighlights(languageId, decoded);
	analysis.textDiagnosticLines = textDiagnostics(languageId, decoded);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Language: %1").arg(analysis.textLanguageName);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Syntax engine: %1").arg(analysis.textSyntaxEngine);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Encoding: UTF-8");
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Save state: %1").arg(analysis.textSaveState);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bytes: %1").arg(sizeText(totalBytes));
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Highlights:");
	analysis.detailLines << (analysis.textHighlightLines.isEmpty() ? QStringList {QCoreApplication::translate("VibeStudioAssetTools", "No syntax highlights in preview sample.")} : analysis.textHighlightLines);
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Diagnostics:");
	analysis.detailLines << analysis.textDiagnosticLines;
	analysis.rawLines = analysis.detailLines;
	analysis.rawLines << QCoreApplication::translate("VibeStudioAssetTools", "Preview text follows:");
	analysis.rawLines << decoded;
	return analysis;
}

QStringList defaultTextExtensions()
{
	return {
		QStringLiteral("cfg"),
		QStringLiteral("shader"),
		QStringLiteral("qc"),
		QStringLiteral("qh"),
		QStringLiteral("txt"),
		QStringLiteral("map"),
		QStringLiteral("def"),
		QStringLiteral("ent"),
		QStringLiteral("arena"),
		QStringLiteral("skin"),
	};
}

// ---------------------------------------------------------------------------
// WAV writing
// ---------------------------------------------------------------------------

void appendLe16Bytes(QByteArray* data, quint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32Bytes(QByteArray* data, quint32 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
	data->append(static_cast<char>((value >> 16) & 0xff));
	data->append(static_cast<char>((value >> 24) & 0xff));
}

QByteArray buildCanonicalPcm16Wav(int channels, int sampleRate, const QByteArray& samples)
{
	QByteArray data;
	data.reserve(samples.size() + 44);
	data.append("RIFF");
	appendLe32Bytes(&data, static_cast<quint32>(36 + samples.size()));
	data.append("WAVE");
	data.append("fmt ");
	appendLe32Bytes(&data, 16);
	appendLe16Bytes(&data, kWaveFormatPcm);
	appendLe16Bytes(&data, static_cast<quint16>(channels));
	appendLe32Bytes(&data, static_cast<quint32>(sampleRate));
	appendLe32Bytes(&data, static_cast<quint32>(sampleRate * channels * 2));
	appendLe16Bytes(&data, static_cast<quint16>(channels * 2));
	appendLe16Bytes(&data, 16);
	data.append("data");
	appendLe32Bytes(&data, static_cast<quint32>(samples.size()));
	data.append(samples);
	return data;
}

// ---------------------------------------------------------------------------
// Doom sound lumps
// ---------------------------------------------------------------------------

// Doom's DMX sound lumps, after the Doom Wiki's "Sound" article: a format word
// of 3, a 16-bit sample rate and a 32-bit sample count, then 8-bit unsigned mono
// PCM. The count includes 16 pad bytes at each end, which the DMX library
// skips, as Chocolate Doom's i_sdlsound.c does. Format 0 is a PC speaker sound
// instead: a 16-bit count of tone bytes, each held for 1/140 s.
struct DmxSoundInfo {
	bool pcSpeaker = false;
	int sampleRate = 0;
	qint64 declaredSamples = 0;
	// The playable samples, pads excluded when the count has room for them.
	qint64 sampleOffset = 0;
	qint64 sampleCount = 0;
	bool padded = false;
	int toneCount = 0;
	qint64 trailingBytes = 0;
};

constexpr int kPcSpeakerTonesPerSecond = 140;

// The graphics a Doom IWAD keeps outside any namespace, by their well-known
// names: the palette and light tables, full-screen pictures, and the menu
// (M_), status bar (ST), intermission (WI), and border (BRDR_) patches.
bool nameIsDoomGraphic(const QString& lumpName)
{
	static const QStringList exact {
		QStringLiteral("PLAYPAL"), QStringLiteral("COLORMAP"), QStringLiteral("TITLEPIC"), QStringLiteral("CREDIT"),
		QStringLiteral("HELP"), QStringLiteral("HELP1"), QStringLiteral("HELP2"), QStringLiteral("INTERPIC"),
		QStringLiteral("VICTORY2"), QStringLiteral("BOSSBACK"), QStringLiteral("PFUB1"), QStringLiteral("PFUB2"),
		QStringLiteral("END0"), QStringLiteral("END1"), QStringLiteral("END2"), QStringLiteral("END3"),
		QStringLiteral("END4"), QStringLiteral("END5"), QStringLiteral("END6"),
	};
	const QString upper = lumpName.toUpper();
	if (exact.contains(upper)) {
		return true;
	}
	// ST is the status bar's prefix, but Hexen's STARTUP screens are planar
	// VGA dumps, not patches.
	if (upper.startsWith(QStringLiteral("ST")) && !upper.startsWith(QStringLiteral("STARTUP"))) {
		return true;
	}
	return upper.startsWith(QStringLiteral("M_")) || upper.startsWith(QStringLiteral("WI")) || upper.startsWith(QStringLiteral("BRDR_"))
		|| upper.startsWith(QStringLiteral("AMMNUM"));
}

// Doom names its sounds DS* (digital) and DP* (PC speaker).
bool nameHintsDoomSound(const QString& virtualPath, QChar kind)
{
	const QString name = QFileInfo(virtualPath).completeBaseName();
	return name.size() > 2 && name.at(0).toUpper() == QLatin1Char('D') && name.at(1).toUpper() == kind;
}

// `totalBytes` is the entry's real size, so a sampled read still validates
// the header against the whole lump.
bool parseDmxSound(const QByteArray& bytes, const QString& virtualPath, quint64 totalBytes, DmxSoundInfo* info)
{
	quint16 format = 0;
	if (!info || !readLe16(bytes, 0, &format)) {
		return false;
	}
	// WAD sound sizes and the shared header validator use signed offsets.
	// An out-of-range archive declaration cannot describe a supported lump.
	if (totalBytes > static_cast<quint64>(std::numeric_limits<qint64>::max())) { return false; }
	const qint64 size = std::max<qint64>(static_cast<qint64>(totalBytes), bytes.size());
	if (format == 3) {
		quint16 rate = 0;
		quint32 declared = 0;
		if (!readLe16(bytes, 2, &rate) || !readLe32(bytes, 4, &declared)) {
			return false;
		}
		// The WAD reader's test: a playable rate, a count that fills the lump,
		// and never a map lump, whose numbers can look like a header.
		if (!dmxSoundHeaderLooksValid(bytes.left(8), size, QFileInfo(virtualPath).fileName())) {
			return false;
		}
		info->sampleRate = rate;
		info->declaredSamples = declared;
		info->padded = declared > 32;
		info->sampleOffset = info->padded ? 8 + 16 : 8;
		info->sampleCount = info->padded ? static_cast<qint64>(declared) - 32 : static_cast<qint64>(declared);
		info->trailingBytes = size - 8 - static_cast<qint64>(declared);
		return true;
	}
	if (format == 0) {
		// Two zero bytes open plenty of lumps, so a PC speaker sound also needs
		// its DP name.
		quint16 tones = 0;
		if (!nameHintsDoomSound(virtualPath, QLatin1Char('P')) || !readLe16(bytes, 2, &tones) || tones == 0
			|| static_cast<qint64>(tones) > size - 4) {
			return false;
		}
		info->pcSpeaker = true;
		info->toneCount = tones;
		info->trailingBytes = size - 4 - tones;
		return true;
	}
	return false;
}

AssetAudioPeaks unsignedPcm8Peaks(const uchar* samples, qint64 count, int sampleRate, int bucketCount)
{
	AssetAudioPeaks peaks;
	peaks.channels = 1;
	peaks.sampleRate = sampleRate;
	peaks.bitsPerSample = 8;
	peaks.frameCount = std::max<qint64>(0, count);
	peaks.durationMs = sampleRate > 0 ? peaks.frameCount * 1000 / sampleRate : 0;
	if (count <= 0) {
		peaks.error = QCoreApplication::translate("VibeStudioAssetTools", "The sound holds no samples.");
		return peaks;
	}
	const int buckets = std::clamp(bucketCount, 1, 8192);
	peaks.bucketCount = buckets;
	peaks.peaks.fill(0.0f, static_cast<qsizetype>(buckets) * 2);
	for (int bucket = 0; bucket < buckets; ++bucket) {
		const qint64 start = count * bucket / buckets;
		const qint64 end = std::min(count, std::max(start + 1, count * (bucket + 1) / buckets));
		float minimum = std::numeric_limits<float>::max();
		float maximum = -std::numeric_limits<float>::max();
		for (qint64 index = start; index < end; ++index) {
			const float value = decodeWaveSample(samples + index, kWaveFormatPcm, 8);
			minimum = std::min(minimum, value);
			maximum = std::max(maximum, value);
		}
		if (minimum > maximum) {
			minimum = 0.0f;
			maximum = 0.0f;
		}
		peaks.peaks[static_cast<qsizetype>(bucket) * 2] = std::clamp(minimum, -1.0f, 1.0f);
		peaks.peaks[static_cast<qsizetype>(bucket) * 2 + 1] = std::clamp(maximum, -1.0f, 1.0f);
	}
	peaks.valid = true;
	return peaks;
}

// The samples of a digital DMX sound that are actually in `bytes`.
qint64 availableDmxSamples(const QByteArray& bytes, const DmxSoundInfo& info)
{
	return std::clamp<qint64>(bytes.size() - info.sampleOffset, 0, info.sampleCount);
}

// 8-bit unsigned to 16-bit signed by shifting, so nothing is lost: the high
// byte plus 128 is the original sample.
QByteArray dmxSoundToPcm16Wav(const QByteArray& bytes, const DmxSoundInfo& info)
{
	const qint64 count = availableDmxSamples(bytes, info);
	QByteArray samples;
	samples.resize(static_cast<qsizetype>(count * 2));
	const auto* source = reinterpret_cast<const uchar*>(bytes.constData()) + info.sampleOffset;
	auto* target = reinterpret_cast<uchar*>(samples.data());
	for (qint64 index = 0; index < count; ++index) {
		const auto sample = static_cast<quint16>(static_cast<qint16>((static_cast<int>(source[index]) - 128) * 256));
		target[index * 2] = static_cast<uchar>(sample & 0xff);
		target[index * 2 + 1] = static_cast<uchar>(sample >> 8);
	}
	return buildCanonicalPcm16Wav(1, info.sampleRate, samples);
}

AssetAnalysis analyzeDmxSound(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes)
{
	DmxSoundInfo info;
	if (!parseDmxSound(bytes, virtualPath, totalBytes, &info)) {
		return {};
	}
	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Audio;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Audio metadata");
	analysis.audioFormat = QStringLiteral("DMX");
	if (info.pcSpeaker) {
		analysis.audioCodec = QCoreApplication::translate("VibeStudioAssetTools", "PC speaker tones");
		analysis.audioDurationMs = static_cast<qint64>(info.toneCount) * 1000 / kPcSpeakerTonesPerSecond;
		analysis.summary = QCoreApplication::translate("VibeStudioAssetTools", "Doom PC speaker sound, %1 tones").arg(info.toneCount);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Audio format: Doom PC speaker sound (DMX format 0)");
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Tones: %1, each held for 1/140 s").arg(info.toneCount);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: %1").arg(durationText(analysis.audioDurationMs));
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Playback state: a PC speaker sound is a tone sequence with no samples to play.");
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "WAV export: unavailable; a PC speaker sound has no samples.");
		analysis.audioWaveformLines << QCoreApplication::translate("VibeStudioAssetTools", "PC speaker sounds are tone sequences, not samples.");
	} else {
		const qint64 available = availableDmxSamples(bytes, info);
		analysis.audioCodec = QCoreApplication::translate("VibeStudioAssetTools", "8-bit unsigned PCM");
		analysis.audioChannels = 1;
		analysis.audioSampleRate = info.sampleRate;
		analysis.audioBitsPerSample = 8;
		analysis.audioBitrateBitsPerSecond = static_cast<qint64>(info.sampleRate) * 8;
		analysis.audioFrameCount = info.sampleCount;
		analysis.audioDurationMs = info.sampleCount * 1000 / info.sampleRate;
		analysis.audioQtPlaybackCandidate = true;
		analysis.audioWavExportSupported = true;
		analysis.audioWavExportNeedsConversion = true;
		analysis.summary = QCoreApplication::translate("VibeStudioAssetTools", "Doom sound, %1 Hz, mono").arg(info.sampleRate);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Audio format: Doom DMX sound (format 3)");
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Codec: 8-bit unsigned PCM, mono");
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Sample rate: %1 Hz").arg(info.sampleRate);
		analysis.detailLines << (info.padded
			? QCoreApplication::translate("VibeStudioAssetTools", "Samples: %1; the header counts %2 with the 16 pad bytes at each end").arg(info.sampleCount).arg(info.declaredSamples)
			: QCoreApplication::translate("VibeStudioAssetTools", "Samples: %1; too few to hold the usual pad bytes").arg(info.sampleCount));
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Duration: %1").arg(durationText(analysis.audioDurationMs));
		if (info.declaredSamples <= 48) {
			analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Warning: the DMX library does not play a sound of 48 samples or fewer, and neither do ports that follow it.");
		}
		if (info.trailingBytes > 0) {
			analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Trailing bytes: %1 after the samples, ignored.").arg(info.trailingBytes);
		}
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Playback state: decodable PCM, played as a converted WAV.");
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "WAV export: supported, re-encoded to canonical 16-bit PCM without the pad bytes");
		analysis.audioPeaks = unsignedPcm8Peaks(reinterpret_cast<const uchar*>(bytes.constData()) + std::min<qint64>(info.sampleOffset, bytes.size()),
			available, info.sampleRate, 512);
		analysis.audioWaveformLines = assetWaveformLines(analysis.audioPeaks, 32, 12);
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Waveform preview:");
		analysis.detailLines << analysis.audioWaveformLines;
	}
	analysis.body = analysis.summary;
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bytes: %1").arg(sizeText(totalBytes));
	if (totalBytes > static_cast<quint64>(bytes.size())) {
		analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Sampled: the first %1 of %2; anything past that point was not read.")
			.arg(sizeText(bytes.size()), sizeText(totalBytes));
	}
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

} // namespace

bool AssetImageConversionReport::succeeded() const
{
	return errorCount == 0;
}

bool AssetAudioExportReport::succeeded() const
{
	return !cancelled && error.isEmpty() && (dryRun || written);
}

QString assetPreviewKindId(AssetPreviewKind kind)
{
	switch (kind) {
	case AssetPreviewKind::Unknown:
		return QStringLiteral("unknown");
	case AssetPreviewKind::Image:
		return QStringLiteral("image");
	case AssetPreviewKind::Model:
		return QStringLiteral("model");
	case AssetPreviewKind::Audio:
		return QStringLiteral("audio");
	case AssetPreviewKind::Text:
		return QStringLiteral("text");
	case AssetPreviewKind::Binary:
		return QStringLiteral("binary");
	}
	return QStringLiteral("unknown");
}

AssetPreviewKind assetPreviewKindForPath(const QString& virtualPath)
{
	if (const auto* format = assetFormatForPath(virtualPath)) { return format->preview; }
	if (defaultTextExtensions().contains(normalizedExtension(virtualPath))) { return AssetPreviewKind::Text; }
	return AssetPreviewKind::Unknown;
}

AssetPreviewKind assetPreviewKindForEntry(const QString& virtualPath, const QString& typeHint)
{
	const QFileInfo file(virtualPath);
	const QString suffix = file.suffix().toLower();
	// WAD2 and WAD3 lumps are textures and pictures, and a Doom WAD's
	// namespaces hold flats, sprites, and wall patches.
	static const QStringList imageHints {QStringLiteral("wad-texture"), QStringLiteral("wad-flat"), QStringLiteral("wad-sprite"), QStringLiteral("wad-patch")};
	if (imageHints.contains(typeHint)) {
		return AssetPreviewKind::Image;
	}
	// A lump the WAD reader found a DMX header on, whatever its name.
	if (typeHint == QStringLiteral("wad-sound")) {
		return AssetPreviewKind::Audio;
	}
	// A WAD's lumps have no extension; Doom names its sounds DS* and DP*.
	if (typeHint == QStringLiteral("wad-lump") && suffix.isEmpty()
		&& (nameHintsDoomSound(virtualPath, QLatin1Char('S')) || nameHintsDoomSound(virtualPath, QLatin1Char('P')))) {
		return AssetPreviewKind::Audio;
	}
	if (typeHint == QStringLiteral("wad-lump") && suffix.isEmpty() && nameIsDoomGraphic(file.fileName())) {
		return AssetPreviewKind::Image;
	}
	// A PK3 keeps them under sounds/, bare or as .lmp.
	const QString folder = file.path().toLower();
	if ((folder == QStringLiteral("sounds") || folder.endsWith(QStringLiteral("/sounds"))) && (suffix.isEmpty() || suffix == QStringLiteral("lmp"))) {
		return AssetPreviewKind::Audio;
	}
	return assetPreviewKindForPath(virtualPath);
}

QString assetDetectionPath(const QString& virtualPath, const QString& typeHint)
{
	return typeHint == QStringLiteral("wad-flat") ? QStringLiteral("flats/") + virtualPath : virtualPath;
}

bool AssetAudioPlaybackSource::playable() const
{
	return error.isEmpty() && !bytes.isEmpty();
}

AssetAudioPlaybackSource assetAudioPlaybackSource(const QString& virtualPath, const QByteArray& bytes, qint64 maxBytes)
{
	AssetAudioPlaybackSource source;
	if (maxBytes < 1 || bytes.size() > maxBytes) {
		source.error = QCoreApplication::translate("VibeStudioAssetTools", "The sound exceeds the audition byte limit."); return source;
	}
	const QString baseName = QFileInfo(virtualPath).completeBaseName().isEmpty() ? QStringLiteral("sound") : QFileInfo(virtualPath).completeBaseName();
	WaveFormatInfo wave;
	DmxSoundInfo dmx;
	if (parseWaveFormat(bytes, &wave)) {
		source.format = QStringLiteral("WAV");
		source.fileName = baseName + QStringLiteral(".wav");
		// Audition preserves precision and metadata. The host may also support
		// codecs outside the editor's import contract, such as ADPCM or A-law.
		source.bytes = bytes;
		return source;
	}
	if (parseDmxSound(bytes, virtualPath, bytes.size(), &dmx)) {
		source.format = QStringLiteral("DMX");
		if (dmx.pcSpeaker) {
			source.error = QCoreApplication::translate("VibeStudioAssetTools", "A PC speaker sound is a tone sequence with no samples to play.");
			return source;
		}
		if (maxBytes < 44 || availableDmxSamples(bytes, dmx) > (maxBytes - 44) / 2) {
			source.error = QCoreApplication::translate("VibeStudioAssetTools", "The converted sound exceeds the audition byte limit."); return source;
		}
		source.fileName = baseName + QStringLiteral(".wav");
		source.bytes = dmxSoundToPcm16Wav(bytes, dmx);
		return source;
	}
	const AssetAnalysis compressed = analyzeCompressedAudio(virtualPath, bytes, bytes.size());
	if (compressed.kind == AssetPreviewKind::Audio && compressed.audioQtPlaybackCandidate) {
		source.format = compressed.audioFormat;
		const QString extension = normalizedExtension(virtualPath);
		const bool known = QStringList {QStringLiteral("ogg"), QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("opus")}.contains(extension);
		source.fileName = baseName + QLatin1Char('.') + (known ? extension : compressed.audioFormat.toLower());
		source.bytes = bytes;
		return source;
	}
	source.error = QCoreApplication::translate("VibeStudioAssetTools", "This entry is not audio VibeStudio can play.");
	return source;
}

AssetAnalysis analyzeAssetBytes(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes, const IdTechPalette* palette)
{
	return analyzeAssetSample(virtualPath, bytes, totalBytes < 0 ? static_cast<quint64>(bytes.size()) : static_cast<quint64>(totalBytes), palette);
}

AssetAnalysis analyzeAssetSample(const QString& virtualPath, const QByteArray& bytes, quint64 totalBytes, const IdTechPalette* palette)
{
	// A Doom sound goes first: its header is specific, and a patch or lump
	// reading of the same bytes is not.
	AssetAnalysis analysis = analyzeDmxSound(virtualPath, bytes, totalBytes);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis = analyzeImage(virtualPath, bytes, totalBytes, palette);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis = analyzeWav(virtualPath, bytes, totalBytes);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis = analyzeCompressedAudio(virtualPath, bytes, totalBytes);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis = analyzeModel(virtualPath, bytes, totalBytes);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis = analyzeText(virtualPath, bytes, totalBytes);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis.kind = AssetPreviewKind::Binary;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = QCoreApplication::translate("VibeStudioAssetTools", "Binary asset");
	analysis.summary = QCoreApplication::translate("VibeStudioAssetTools", "Binary asset, %1 sampled.").arg(sizeText(bytes.size()));
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Asset kind: binary");
	analysis.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Bytes: %1").arg(sizeText(totalBytes));
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

AssetAudioPeaks extractWavePeaks(const QByteArray& bytes, int bucketCount)
{
	AssetAudioPeaks peaks;
	WaveFormatInfo info;
	if (!parseWaveFormat(bytes, &info)) {
		peaks.error = QCoreApplication::translate("VibeStudioAssetTools", "Not a readable RIFF/WAVE payload.");
		return peaks;
	}
	peaks.channels = info.channels;
	peaks.sampleRate = static_cast<int>(info.sampleRate);
	peaks.bitsPerSample = info.bitsPerSample;
	if (!waveSampleFormatSupported(info)) {
		peaks.error = QCoreApplication::translate("VibeStudioAssetTools", "Unsupported WAV sample format (%1, %2-bit).").arg(waveFormatTagName(info.effectiveFormatTag)).arg(info.bitsPerSample);
		return peaks;
	}
	if (info.dataOffset < 0 || info.availableDataBytes <= 0 || info.blockAlign == 0) {
		peaks.error = QCoreApplication::translate("VibeStudioAssetTools", "The WAV data chunk is empty or missing.");
		return peaks;
	}
	const int bytesPerSample = info.bitsPerSample / 8;
	if (info.blockAlign < bytesPerSample * info.channels) {
		peaks.error = QCoreApplication::translate("VibeStudioAssetTools", "The WAV block alignment does not match the declared sample format.");
		return peaks;
	}
	const qint64 frameCount = info.availableDataBytes / info.blockAlign;
	peaks.frameCount = frameCount;
	peaks.durationMs = info.sampleRate > 0 ? frameCount * 1000 / info.sampleRate : 0;
	if (frameCount <= 0) {
		peaks.error = QCoreApplication::translate("VibeStudioAssetTools", "The WAV data chunk holds no complete frames.");
		return peaks;
	}
	const int buckets = std::clamp(bucketCount, 1, 8192);
	peaks.bucketCount = buckets;
	peaks.peaks.fill(0.0f, static_cast<qsizetype>(buckets) * info.channels * 2);
	const auto* base = reinterpret_cast<const uchar*>(bytes.constData()) + info.dataOffset;
	for (int bucket = 0; bucket < buckets; ++bucket) {
		qint64 start = frameCount * bucket / buckets;
		qint64 end = frameCount * (bucket + 1) / buckets;
		if (end <= start) {
			end = std::min(frameCount, start + 1);
		}
		for (int channel = 0; channel < info.channels; ++channel) {
			float minimum = std::numeric_limits<float>::max();
			float maximum = -std::numeric_limits<float>::max();
			for (qint64 frame = start; frame < end; ++frame) {
				const float value = decodeWaveSample(base + frame * info.blockAlign + channel * bytesPerSample, info.effectiveFormatTag, info.bitsPerSample);
				minimum = std::min(minimum, value);
				maximum = std::max(maximum, value);
			}
			if (minimum > maximum) {
				minimum = 0.0f;
				maximum = 0.0f;
			}
			const qsizetype index = (static_cast<qsizetype>(channel) * buckets + bucket) * 2;
			peaks.peaks[index] = std::clamp(minimum, -1.0f, 1.0f);
			peaks.peaks[index + 1] = std::clamp(maximum, -1.0f, 1.0f);
		}
	}
	peaks.valid = true;
	return peaks;
}

AudioClipResult decodeAudioClip(const QString& sourceName, const QByteArray& bytes, const AudioWorkControl& control)
{
	AudioClipResult result;
	const auto reject = [&](const char* message) {
		result.error = QCoreApplication::translate("VibeStudioAudio", message);
		return result;
	};
	if (bytes.size() > AudioInputByteLimit) { return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The audio input exceeds the 128 MiB editing limit.")); }
	WaveFormatInfo wave;
	DmxSoundInfo dmx;
	qint64 offset = 0, frames = 0;
	int stride = 0, sampleBytes = 0;
	quint16 format = kWaveFormatPcm, bits = 8;
	if (bytes.startsWith("RIFF")) {
		// Preview readers intentionally accept partial data. Editing must never
		// turn a truncated preview into an apparently complete saved sound.
		quint32 riffSize = 0;
		if (!readLe32(bytes, 4, &riffSize) || riffSize < 4 || qint64(riffSize) + 8 > bytes.size()) {
			return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The WAV container is truncated or has an invalid RIFF size."));
		}
		const qint64 riffEnd = qint64(riffSize) + 8;
		int formats = 0, dataChunks = 0;
		for (qint64 chunk = 12; chunk < riffEnd;) {
			quint32 size = 0;
			if (chunk + 8 > riffEnd || !readLe32(bytes, chunk + 4, &size) || chunk + 8 + size > riffEnd) {
				return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The WAV contains an incomplete chunk."));
			}
			const auto id = bytes.mid(chunk, 4);
			if (id == "fmt ") {
				++formats;
				if (size < 16) { return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The WAV format chunk is incomplete.")); }
				quint16 tag = 0;
				readLe16(bytes, chunk + 8, &tag);
				if (tag == kWaveFormatExtensible) {
					// PCM/float use the standard wave subtype GUID; arbitrary GUIDs
					// with the same low word are not PCM codecs.
					const QByteArray tail = QByteArray::fromHex("000000001000800000aa00389b71");
					quint16 extra = 0, validBits = 0, containerBits = 0;
					readLe16(bytes, chunk + 24, &extra); readLe16(bytes, chunk + 26, &validBits); readLe16(bytes, chunk + 22, &containerBits);
					if (size < 40 || extra < 22 || quint32(extra) + 18 > size || validBits > containerBits || bytes.mid(chunk + 8 + 26, 14) != tail) {
						return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The extensible WAV subtype is unsupported."));
					}
				}
			} else if (id == "data") { ++dataChunks; }
			chunk += 8 + qint64(size) + (size % 2);
			if (chunk > riffEnd) { return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The WAV chunk padding is incomplete.")); }
		}
		if (formats != 1 || dataChunks != 1 || !parseWaveFormat(bytes.left(riffEnd), &wave) || !waveSampleFormatSupported(wave)) {
			return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "Editing requires one PCM or floating-point WAV stream, or a digital Doom DMX sound."));
		}
		sampleBytes = wave.bitsPerSample / 8;
		if (wave.blockAlign != wave.channels * sampleBytes || wave.blockAlign == 0 || wave.dataTruncated ||
			wave.declaredDataBytes % wave.blockAlign != 0 || qint64(wave.byteRate) != qint64(wave.sampleRate) * wave.blockAlign) {
			return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The WAV sample data does not contain complete, correctly aligned frames."));
		}
		result.clip.channels = wave.channels;
		result.clip.sampleRate = static_cast<int>(wave.sampleRate);
		offset = wave.dataOffset; stride = wave.blockAlign;
		frames = wave.declaredDataBytes / stride;
		format = wave.effectiveFormatTag; bits = wave.bitsPerSample;
	} else if (parseDmxSound(bytes, sourceName, bytes.size(), &dmx)) {
		if (dmx.pcSpeaker) { return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "A PC speaker sound is a tone sequence with no decoded samples to edit or export.")); }
		result.clip.channels = 1; result.clip.sampleRate = dmx.sampleRate;
		offset = dmx.sampleOffset; frames = dmx.sampleCount; stride = 1; sampleBytes = 1;
		if (frames > availableDmxSamples(bytes, dmx)) { return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The Doom DMX sample data is truncated.")); }
	} else {
		return decodeCompressedAudio(bytes, control);
	}
	if (result.clip.channels < 1 || result.clip.channels > 8 || result.clip.sampleRate < 1 || result.clip.sampleRate > 384000 ||
		frames <= 0 || frames > AudioSampleLimit / result.clip.channels) {
		return reject(QT_TRANSLATE_NOOP("VibeStudioAudio", "The sound exceeds the editor's channel, sample rate, or sample count limits, or contains no samples."));
	}
	result.clip.samples.resize(frames * result.clip.channels);
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData()) + offset;
	for (qint64 frame = 0; frame < frames; ++frame) {
		if (frame % 4096 == 0 && control.cancelled && control.cancelled()) {
			result.cancelled = true; result.clip = {}; return result;
		}
		for (int channel = 0; channel < result.clip.channels; ++channel) {
			result.clip.samples[frame * result.clip.channels + channel] = decodeWaveSample(data + frame * stride + channel * sampleBytes, format, bits);
		}
	}
	if (bytes.startsWith("RIFF") && !decodeWavAudioMarkers(bytes, frames, &result.clip.markers, &result.error)) { return result; }
	result.error = validateAudioClip(result.clip);
	return result;
}

QStringList assetWaveformLines(const AssetAudioPeaks& peaks, int buckets, int width)
{
	QStringList lines;
	if (!peaks.valid || peaks.bucketCount <= 0 || peaks.channels <= 0) {
		lines << (peaks.error.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "Waveform unavailable for this codec or bit depth.") : peaks.error);
		return lines;
	}
	const int outputBuckets = std::clamp(buckets, 1, peaks.bucketCount);
	const int barWidth = std::clamp(width, 4, 64);
	for (int channel = 0; channel < peaks.channels; ++channel) {
		if (peaks.channels > 1) {
			lines << QCoreApplication::translate("VibeStudioAssetTools", "Channel %1:").arg(channel + 1);
		}
		for (int bucket = 0; bucket < outputBuckets; ++bucket) {
			const int sourceStart = peaks.bucketCount * bucket / outputBuckets;
			int sourceEnd = peaks.bucketCount * (bucket + 1) / outputBuckets;
			if (sourceEnd <= sourceStart) {
				sourceEnd = std::min(peaks.bucketCount, sourceStart + 1);
			}
			float magnitude = 0.0f;
			for (int source = sourceStart; source < sourceEnd; ++source) {
				const qsizetype index = (static_cast<qsizetype>(channel) * peaks.bucketCount + source) * 2;
				if (index + 1 >= peaks.peaks.size()) {
					break;
				}
				magnitude = std::max(magnitude, std::max(std::abs(peaks.peaks[index]), std::abs(peaks.peaks[index + 1])));
			}
			const int bars = std::clamp(static_cast<int>(std::lround(magnitude * barWidth)), 0, barWidth);
			lines << QStringLiteral("%1 %2")
					.arg(bucket + 1, 2, 10, QLatin1Char('0'))
					.arg(QString(bars, QLatin1Char('#')).leftJustified(barWidth, QLatin1Char('.')));
		}
	}
	return lines;
}

AssetImageConversionReport convertPackageImages(const PackageArchive& archive, const AssetImageConversionRequest& request)
{
	AssetImageConversionReport report;
	report.sourcePath = archive.sourcePath();
	report.outputDirectory = QFileInfo(request.outputDirectory).absoluteFilePath();
	report.dryRun = request.dryRun;
	if (!archive.isOpen()) {
		report.warnings << QCoreApplication::translate("VibeStudioAssetTools", "No package is open.");
		report.errorCount = 1;
		return report;
	}
	if (request.outputDirectory.trimmed().isEmpty()) {
		report.warnings << QCoreApplication::translate("VibeStudioAssetTools", "Output directory is required.");
		report.errorCount = 1;
		return report;
	}

	const QString paletteMode = request.paletteMode.trimmed().toLower();
	const bool grayscaleMode = paletteMode == QStringLiteral("grayscale");
	bool quantizeMode = false;
	QString quantizePaletteId;
	if (!grayscaleMode && !paletteMode.isEmpty() && paletteMode != QStringLiteral("none") && paletteMode != QStringLiteral("rgb")) {
		if (paletteMode == QStringLiteral("indexed")) {
			quantizeMode = true;
		} else if (idTechPaletteDescriptorForId(paletteMode)) {
			quantizeMode = true;
			quantizePaletteId = paletteMode;
		} else {
			report.warnings << QCoreApplication::translate("VibeStudioAssetTools", "Unknown palette mode '%1'. Use grayscale, rgb, indexed, or one of: %2.").arg(paletteMode, idTechPaletteIds().join(QStringLiteral(", ")));
			report.errorCount = 1;
			return report;
		}
	}

	QStringList requestedPaths = request.virtualPaths;
	if (requestedPaths.isEmpty()) {
		for (const PackageEntry& entry : archive.entries()) {
			if (entry.kind == PackageEntryKind::File && assetPreviewKindForPath(entry.virtualPath) == AssetPreviewKind::Image) {
				requestedPaths << entry.virtualPath;
			}
		}
	}
	report.requestedCount = requestedPaths.size();
	if (!request.dryRun && !QDir().mkpath(report.outputDirectory)) {
		report.warnings << QCoreApplication::translate("VibeStudioAssetTools", "Unable to create output directory.");
		report.errorCount = std::max(1, report.requestedCount);
		return report;
	}

	QHash<QString, IdTechPaletteResolution> resolutionCache;
	const auto resolvePalette = [&](const QString& paletteId) -> IdTechPaletteResolution {
		const auto cached = resolutionCache.constFind(paletteId);
		if (cached != resolutionCache.constEnd()) {
			return cached.value();
		}
		const IdTechPaletteResolution resolution = resolveIdTechPalette(archive, paletteId);
		resolutionCache.insert(paletteId, resolution);
		return resolution;
	};

	for (const QString& virtualPath : requestedPaths) {
		AssetImageConversionEntryResult result;
		result.virtualPath = virtualPath;
		result.outputFormat = QString::fromLatin1(imageFormatBytes(request.outputFormat)).toLower();
		result.dryRun = request.dryRun;
		result.outputPath = safePackageOutputPath(report.outputDirectory, outputFileNameForEntry(virtualPath, result.outputFormat), &result.error);
		QByteArray bytes;
		QString readError;
		if (result.error.isEmpty() && !archive.readEntryBytes(virtualPath, &bytes, &readError)) {
			result.error = readError.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "Unable to read package entry.") : readError;
		}
		result.inputBytes = bytes.size();

		QImage image;
		if (result.error.isEmpty()) {
			const IdTechImageFormat detected = detectIdTechImageFormat(virtualPath, bytes);
			result.sourceFormatId = idTechImageFormatId(detected);
			if (detected != IdTechImageFormat::Unknown && detected != IdTechImageFormat::QtNative) {
				const QString decodePaletteId = !request.paletteId.trimmed().isEmpty()
					? request.paletteId.trimmed()
					: defaultIdTechPaletteIdForImage(detected, bytes.size());
				IdTechPaletteResolution resolution;
				const IdTechImageDecodeResult decoded = decodeIdTechImageFromArchive(archive, virtualPath, decodePaletteId, &resolution);
				result.paletteId = resolution.palette.id.isEmpty() ? decodePaletteId : resolution.palette.id;
				result.paletteFromPackage = resolution.fromPackage;
				result.paletteGenerated = resolution.palette.generated || !resolution.fromPackage;
				result.paletteSourceVirtualPath = resolution.sourceVirtualPath;
				if (decoded.decoded && !decoded.image.isNull()) {
					image = decoded.image;
					if (!decoded.formatName.isEmpty()) {
						result.sourceFormatId = decoded.formatId.isEmpty() ? result.sourceFormatId : decoded.formatId;
					}
				} else if (!decoded.error.isEmpty()) {
					result.error = decoded.error;
				}
			}
			if (image.isNull() && result.error.isEmpty()) {
				image.loadFromData(bytes);
				if (image.isNull()) {
					result.error = QCoreApplication::translate("VibeStudioAssetTools", "Entry is not a decodable idTech or Qt image.");
				}
			}
		}

		if (result.error.isEmpty()) {
			result.beforeSize = image.size();
			QImage output = image;
			if (request.cropRect.isValid()) {
				output = output.copy(request.cropRect.intersected(QRect(QPoint(0, 0), output.size())));
			}
			if (request.resizeSize.isValid()) {
				output = output.scaled(request.resizeSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			}
			if (grayscaleMode) {
				output = output.convertToFormat(QImage::Format_Grayscale8);
				result.previewLines << QCoreApplication::translate("VibeStudioAssetTools", "Palette mode: grayscale");
			} else if (quantizeMode) {
				QString targetPaletteId = quantizePaletteId;
				if (targetPaletteId.isEmpty()) {
					targetPaletteId = !request.paletteId.trimmed().isEmpty()
						? request.paletteId.trimmed()
						: (!result.paletteId.isEmpty() ? result.paletteId : defaultIdTechPaletteIdForImage(detectIdTechImageFormat(virtualPath, bytes), bytes.size()));
				}
				const IdTechPaletteResolution resolution = resolvePalette(targetPaletteId);
				if (!resolution.palette.isValid()) {
					result.error = QCoreApplication::translate("VibeStudioAssetTools", "Palette '%1' could not be resolved.").arg(targetPaletteId);
				} else {
					output = quantizeToIdTechPalette(output, resolution.palette);
					result.paletteId = resolution.palette.id.isEmpty() ? targetPaletteId : resolution.palette.id;
					result.paletteFromPackage = resolution.fromPackage;
					result.paletteGenerated = resolution.palette.generated || !resolution.fromPackage;
					result.paletteSourceVirtualPath = resolution.sourceVirtualPath;
					result.previewLines << QCoreApplication::translate("VibeStudioAssetTools", "Palette mode: %1").arg(result.paletteId);
				}
			}
			result.afterSize = output.size();
			if (result.error.isEmpty()) {
				QByteArray outputBytes;
				QBuffer buffer(&outputBytes);
				buffer.open(QIODevice::WriteOnly);
				QImageWriter writer(&buffer, imageFormatBytes(request.outputFormat));
				bool encoded = false;
				if (result.outputFormat == QStringLiteral("dds") || result.outputFormat == QStringLiteral("ftx")) {
					TextureExportOptions options;
					textureExportFormatFromId(result.outputFormat, &options.format);
					const auto native = encodeTextureExport(output, options);
					encoded = native.succeeded; outputBytes = native.bytes; result.error = native.error;
				} else { encoded = writer.write(output); }
				if (!encoded) {
					if (result.error.isEmpty()) { result.error = writer.errorString().isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "Unable to encode output image.") : writer.errorString(); }
				} else {
					result.outputBytes = outputBytes.size();
					result.previewLines << QCoreApplication::translate("VibeStudioAssetTools", "Source format: %1").arg(result.sourceFormatId.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "unknown") : result.sourceFormatId);
					result.previewLines << QCoreApplication::translate("VibeStudioAssetTools", "Before: %1 x %2").arg(result.beforeSize.width()).arg(result.beforeSize.height());
					result.previewLines << QCoreApplication::translate("VibeStudioAssetTools", "After: %1 x %2").arg(result.afterSize.width()).arg(result.afterSize.height());
					result.previewLines << QCoreApplication::translate("VibeStudioAssetTools", "Format: %1").arg(result.outputFormat);
					if (!result.paletteId.isEmpty()) {
						result.previewLines << (result.paletteFromPackage
							? QCoreApplication::translate("VibeStudioAssetTools", "Palette source: package entry %1").arg(result.paletteSourceVirtualPath.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "(unnamed)") : result.paletteSourceVirtualPath)
							: QCoreApplication::translate("VibeStudioAssetTools", "Palette source: generated fallback (this package has no %1 palette)").arg(result.paletteId));
					}
					if (request.dryRun) {
						result.message = QFileInfo::exists(result.outputPath) ? QCoreApplication::translate("VibeStudioAssetTools", "Would overwrite converted image.") : QCoreApplication::translate("VibeStudioAssetTools", "Would write converted image.");
					} else {
						const QFileInfo outputInfo(result.outputPath);
						const bool existedBefore = outputInfo.exists();
						if (existedBefore && !request.overwriteExisting) {
							result.error = QCoreApplication::translate("VibeStudioAssetTools", "Output already exists. Use --overwrite to replace it.");
						} else if (!QDir().mkpath(outputInfo.absolutePath())) {
							result.error = QCoreApplication::translate("VibeStudioAssetTools", "Unable to create output parent directory.");
						} else {
							QSaveFile file(result.outputPath);
							if (!file.open(QIODevice::WriteOnly)) {
								result.error = QCoreApplication::translate("VibeStudioAssetTools", "Unable to open output image.");
							} else if (file.write(outputBytes) != outputBytes.size()) {
								result.error = QCoreApplication::translate("VibeStudioAssetTools", "Unable to write output image.");
							} else if (!file.commit()) {
								result.error = QCoreApplication::translate("VibeStudioAssetTools", "Unable to commit output image.");
							} else {
								result.written = true;
								result.message = existedBefore ? QCoreApplication::translate("VibeStudioAssetTools", "Overwrote converted image.") : QCoreApplication::translate("VibeStudioAssetTools", "Wrote converted image.");
							}
						}
					}
				}
			}
		}
		++report.processedCount;
		report.totalInputBytes += result.inputBytes;
		report.totalOutputBytes += result.outputBytes;
		if (result.error.isEmpty()) {
			if (result.written) {
				++report.writtenCount;
			}
		} else {
			++report.errorCount;
		}
		report.entries.push_back(result);
	}
	return report;
}

QString assetImageConversionReportText(const AssetImageConversionReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Image conversion");
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Source: %1").arg(report.sourcePath);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Output: %1").arg(report.outputDirectory);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Mode: %1").arg(report.dryRun ? QCoreApplication::translate("VibeStudioAssetTools", "dry run") : QCoreApplication::translate("VibeStudioAssetTools", "write"));
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Requested: %1").arg(report.requestedCount);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Processed: %1").arg(report.processedCount);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Written: %1").arg(report.writtenCount);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Errors: %1").arg(report.errorCount);
	for (const AssetImageConversionEntryResult& result : report.entries) {
		lines << QStringLiteral("- %1 -> %2").arg(result.virtualPath, QDir::toNativeSeparators(result.outputPath));
		lines << QStringLiteral("  %1").arg(result.error.isEmpty() ? result.message : result.error);
		for (const QString& previewLine : result.previewLines) {
			lines << QStringLiteral("  %1").arg(previewLine);
		}
	}
	for (const QString& warning : report.warnings) {
		lines << QCoreApplication::translate("VibeStudioAssetTools", "Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

AssetAudioExportReport exportPackageAudioToWav(const PackageArchive& archive, const QString& virtualPath, const QString& outputPath, bool dryRun, bool overwriteExisting, const std::function<bool()>& cancelled)
{
	const auto normalized = normalizePackageVirtualPath(virtualPath, false);
	const auto entries = archive.entries();
	qsizetype index = -1;
	for (qsizetype row = 0; normalized.isSafe() && row < entries.size(); ++row) {
		if (entries.at(row).virtualPath.compare(normalized.normalizedPath, Qt::CaseInsensitive) != 0) { continue; }
		if (index >= 0) {
			AssetAudioExportReport report; report.sourcePath = archive.sourcePath(); report.virtualPath = virtualPath;
			report.outputPath = outputPath; report.dryRun = dryRun;
			report.error = QCoreApplication::translate("VibeStudioAssetTools", "This audio path occurs more than once. Select a specific entry row to export it.");
			return report;
		}
		index = row;
	}
	if (index < 0) {
		AssetAudioExportReport report; report.sourcePath = archive.sourcePath(); report.virtualPath = virtualPath;
		report.outputPath = outputPath; report.dryRun = dryRun;
		report.error = QCoreApplication::translate("VibeStudioAssetTools", "Audio entry not found."); return report;
	}
	return exportPackageAudioToWavAt(archive, index, outputPath, dryRun, overwriteExisting, cancelled);
}

AssetAudioExportReport exportPackageAudioToWavAt(const PackageArchive& archive, qsizetype entryIndex, const QString& outputPath, bool dryRun, bool overwriteExisting, const std::function<bool()>& cancelled)
{
	AssetAudioExportReport report;
	const auto entries = archive.entries();
	const QString virtualPath = entryIndex >= 0 && entryIndex < entries.size() ? entries.at(entryIndex).virtualPath : QString();
	report.sourcePath = archive.sourcePath(); report.virtualPath = virtualPath;
	report.outputPath = QFileInfo(outputPath).absoluteFilePath(); report.dryRun = dryRun;
	const auto stop = [&]() {
		if (cancelled && cancelled()) {
			report.cancelled = true;
			report.message = QCoreApplication::translate("VibeStudioAssetTools", "Cancelled before writing the WAV output.");
		}
		return report.cancelled;
	};
	if (!archive.isOpen() || virtualPath.isEmpty() || outputPath.trimmed().isEmpty()) {
		report.error = QCoreApplication::translate("VibeStudioAssetTools", "An open package and separate WAV output path are required.");
		return report;
	}
	QStringList protectedPaths{archive.sourcePath()};
	if (archive.format() == PackageArchiveFormat::Folder) { protectedPaths << QDir(archive.sourcePath()).filePath(virtualPath); }
	// A new sibling WAV is a valid export from a folder package. Protect
	// existing package/staged inputs, without banning every new file below it.
	if (QFileInfo::exists(report.outputPath) && archive.protectsInputPath(report.outputPath)) {
		report.error = QCoreApplication::translate("VibeStudioAssetTools", "Export to a separate path to preserve the source package and sound."); return report;
	}
	for (const auto& source : protectedPaths) {
		if (audioPathsReferToSameFile(source, report.outputPath)) {
			report.error = QCoreApplication::translate("VibeStudioAssetTools", "Export to a separate path to preserve the source package and sound.");
			return report;
		}
	}
	if (stop()) { return report; }
	QByteArray bytes;
	const auto& entry = entries.at(entryIndex);
	if (entry.sizeBytes > AudioInputByteLimit) {
		report.error = QCoreApplication::translate("VibeStudioAssetTools", "The audio input exceeds the 128 MiB editing limit."); return report;
	}
	const bool read = archive.streamEntryAt(entryIndex, [&](QByteArrayView chunk) {
		if (chunk.size() > AudioInputByteLimit - bytes.size()) { return false; }
		bytes.append(chunk.data(), chunk.size()); return true;
	}, &report.error, cancelled);
	if (stop()) { return report; }
	if (!read || static_cast<quint64>(bytes.size()) != entry.sizeBytes) {
		if (report.error.isEmpty()) { report.error = QCoreApplication::translate("VibeStudioAssetTools", "Unable to read and verify the complete audio entry."); }
		return report;
	}
	report.sourceBytes = bytes.size();
	const auto decoded = decodeAudioClip(virtualPath, bytes, {cancelled});
	if (decoded.cancelled || stop()) {
		report.cancelled = true;
		report.message = QCoreApplication::translate("VibeStudioAssetTools", "Cancelled before writing the WAV output.");
		return report;
	}
	if (!decoded.succeeded()) { report.error = decoded.error; return report; }
	report.detailLines = decoded.warnings;
	WaveFormatInfo wave;
	const bool wav = parseWaveFormat(bytes, &wave);
	report.sourceFormat = wav ? QStringLiteral("WAV") : compressedAudioFormat(bytes).toUpper();
	if (report.sourceFormat.isEmpty()) { report.sourceFormat = QStringLiteral("DMX"); }
	report.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Source layout: %1 Hz, %2 channel(s)").arg(decoded.clip.sampleRate).arg(decoded.clip.channels);
	const bool canonical = wav && wave.effectiveFormatTag == kWaveFormatPcm && wave.bitsPerSample == 16 && !wave.extensible;
	QByteArray outputBytes;
	if (canonical) {
		outputBytes = bytes;
		report.conversionMode = QStringLiteral("copy");
		report.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Already canonical 16-bit PCM; copied without re-encoding.");
	} else {
		AudioWavOptions options;
		if (wav) {
			AudioMarkers markers;
			if (!decodeWavAudioMarkers(bytes, decoded.clip.frameCount(), &markers, &report.error, &options.markers)) { return report; }
		}
		outputBytes = encodeAudioWav(decoded.clip, options, &report.error, {cancelled});
		if (stop()) { return report; }
		if (!report.error.isEmpty() || outputBytes.isEmpty()) { return report; }
		report.converted = true; report.conversionMode = QStringLiteral("pcm16");
		report.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Decoded and converted to 16-bit PCM using the audio editor's shared pipeline.");
		if (!decoded.clip.markers.empty()) { report.detailLines << QCoreApplication::translate("VibeStudioAssetTools", "Preserved supported cue and loop metadata during PCM conversion."); }
	}
	report.bytes = outputBytes.size();
	if (stop()) { return report; }
	// Cancellation stops preparation. Once atomic publication begins, report its
	// actual result even if the caller subsequently requests cancellation.
	if (!writeAudioExportBytes(outputBytes, report.outputPath, {QStringLiteral("wav")}, overwriteExisting, protectedPaths, &report.error, dryRun)) { return report; }
	report.written = !dryRun;
	report.message = dryRun ? QCoreApplication::translate("VibeStudioAssetTools", "Would write the separate WAV output.")
	                       : QCoreApplication::translate("VibeStudioAssetTools", "Wrote the separate WAV output.");
	return report;
}

QString assetAudioExportReportText(const AssetAudioExportReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Audio WAV export");
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Source: %1").arg(report.sourcePath);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Entry: %1").arg(report.virtualPath);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Output: %1").arg(report.outputPath);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Mode: %1").arg(report.dryRun ? QCoreApplication::translate("VibeStudioAssetTools", "dry run") : QCoreApplication::translate("VibeStudioAssetTools", "write"));
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Source format: %1").arg(report.sourceFormat.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "unknown") : report.sourceFormat);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Conversion: %1").arg(report.conversionMode.isEmpty() ? QCoreApplication::translate("VibeStudioAssetTools", "none") : report.conversionMode);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Source bytes: %1").arg(report.sourceBytes);
	lines << QCoreApplication::translate("VibeStudioAssetTools", "Bytes: %1").arg(report.bytes);
	for (const QString& detail : report.detailLines) {
		lines << QStringLiteral("  %1").arg(detail);
	}
	lines << (report.error.isEmpty() ? report.message : QCoreApplication::translate("VibeStudioAssetTools", "Error: %1").arg(report.error));
	return lines.join('\n');
}

} // namespace vibestudio
