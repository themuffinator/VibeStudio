#include "core/asset_tools.h"

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

QString assetText(const char* source)
{
	return QCoreApplication::translate("VibeStudioAssetTools", source);
}

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
	*value = static_cast<quint32>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
	return true;
}

qint32 readLe32Signed(const QByteArray& bytes, qsizetype offset)
{
	quint32 value = 0;
	readLe32(bytes, offset, &value);
	return static_cast<qint32>(value);
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

QString sizeText(qint64 bytes)
{
	if (bytes >= 1024ll * 1024ll) {
		return assetText("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	if (bytes >= 1024ll) {
		return assetText("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 2);
	}
	return assetText("%1 B").arg(bytes);
}

QString durationText(qint64 durationMs)
{
	if (durationMs <= 0) {
		return assetText("unknown");
	}
	const double seconds = static_cast<double>(durationMs) / 1000.0;
	return assetText("%1 s").arg(seconds, 0, 'f', 2);
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

QStringList paletteLines(const QImage& image)
{
	QStringList lines;
	const QVector<QRgb> colors = image.colorTable();
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
		lines << assetText("... %1 more palette colors").arg(colors.size() - count);
	}
	return lines;
}

AssetAnalysis analyzeImage(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes)
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
	analysis.title = assetText("Texture and image preview");
	analysis.imageFormat = QString::fromLatin1(format).toUpper();
	analysis.imageSize = size.isValid() ? size : image.size();
	analysis.imageDepth = image.isNull() ? 0 : image.depth();
	analysis.imageHasAlpha = !image.isNull() && image.hasAlphaChannel();
	analysis.imageColorCount = image.isNull() ? 0 : image.colorTable().size();
	analysis.imagePaletteAware = analysis.imageColorCount > 0 || hasExtension(virtualPath, {QStringLiteral("pcx"), QStringLiteral("wal"), QStringLiteral("mip"), QStringLiteral("lmp")});
	analysis.imagePaletteLines = image.isNull() ? QStringList {} : paletteLines(image);
	analysis.summary = analysis.imageSize.isValid()
		? assetText("%1 image, %2 x %3").arg(analysis.imageFormat.isEmpty() ? assetText("image") : analysis.imageFormat).arg(analysis.imageSize.width()).arg(analysis.imageSize.height())
		: assetText("%1 image").arg(analysis.imageFormat.isEmpty() ? assetText("image") : analysis.imageFormat);
	analysis.body = analysis.summary;
	analysis.detailLines << assetText("Image format: %1").arg(analysis.imageFormat.isEmpty() ? assetText("unknown") : analysis.imageFormat);
	analysis.detailLines << assetText("Dimensions: %1").arg(analysis.imageSize.isValid() ? assetText("%1 x %2 px").arg(analysis.imageSize.width()).arg(analysis.imageSize.height()) : assetText("unknown"));
	analysis.detailLines << assetText("Depth: %1").arg(analysis.imageDepth > 0 ? assetText("%1 bpp").arg(analysis.imageDepth) : assetText("unknown"));
	analysis.detailLines << assetText("Alpha channel: %1").arg(analysis.imageHasAlpha ? assetText("yes") : assetText("no"));
	analysis.detailLines << assetText("Palette-aware: %1").arg(analysis.imagePaletteAware ? assetText("yes") : assetText("no"));
	analysis.detailLines << assetText("Palette colors: %1").arg(analysis.imageColorCount);
	analysis.detailLines << assetText("Conversion: crop, resize, palette conversion, and format export available through asset convert.");
	if (!analysis.imagePaletteLines.isEmpty()) {
		analysis.detailLines << assetText("Palette sample:");
		analysis.detailLines << analysis.imagePaletteLines;
	}
	analysis.detailLines << assetText("Bytes: %1").arg(sizeText(totalBytes >= 0 ? totalBytes : bytes.size()));
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

QStringList waveformLines(const QByteArray& sampleBytes, int channels, int bitsPerSample)
{
	QStringList lines;
	if (channels <= 0 || bitsPerSample != 16 || sampleBytes.size() < 2) {
		lines << assetText("Waveform unavailable for this codec or bit depth.");
		return lines;
	}
	const int sampleCount = sampleBytes.size() / 2;
	const int buckets = 32;
	for (int bucket = 0; bucket < buckets; ++bucket) {
		const int start = bucket * sampleCount / buckets;
		const int end = std::max(start + 1, (bucket + 1) * sampleCount / buckets);
		int peak = 0;
		for (int sample = start; sample < end; sample += std::max(1, channels)) {
			quint16 raw = 0;
			readLe16(sampleBytes, sample * 2, &raw);
			const int signedSample = static_cast<qint16>(raw);
			peak = std::max(peak, std::abs(signedSample));
		}
		const int bars = std::clamp(peak * 12 / 32768, 0, 12);
		lines << QStringLiteral("%1 %2").arg(bucket + 1, 2, 10, QLatin1Char('0')).arg(QString(bars, QLatin1Char('#')).leftJustified(12, QLatin1Char('.')));
	}
	return lines;
}

AssetAnalysis analyzeWav(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes)
{
	if (bytes.size() < 44 || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 4) != "WAVE") {
		return {};
	}
	quint16 formatTag = 0;
	quint16 channels = 0;
	quint32 sampleRate = 0;
	quint16 bitsPerSample = 0;
	quint32 dataOffset = 0;
	quint32 dataBytes = 0;
	for (qsizetype offset = 12; offset + 8 <= bytes.size();) {
		const QByteArray chunkId = bytes.mid(offset, 4);
		quint32 chunkSize = 0;
		readLe32(bytes, offset + 4, &chunkSize);
		const qsizetype payload = offset + 8;
		if (payload + chunkSize > bytes.size()) {
			break;
		}
		if (chunkId == "fmt " && chunkSize >= 16) {
			readLe16(bytes, payload, &formatTag);
			readLe16(bytes, payload + 2, &channels);
			readLe32(bytes, payload + 4, &sampleRate);
			readLe16(bytes, payload + 14, &bitsPerSample);
		} else if (chunkId == "data") {
			dataOffset = static_cast<quint32>(payload);
			dataBytes = chunkSize;
		}
		offset = payload + chunkSize + (chunkSize % 2);
	}
	if (channels == 0 || sampleRate == 0) {
		return {};
	}
	const qint64 bytesPerSecond = static_cast<qint64>(sampleRate) * channels * std::max<int>(1, bitsPerSample) / 8;
	const qint64 durationMs = bytesPerSecond > 0 ? (static_cast<qint64>(dataBytes) * 1000) / bytesPerSecond : 0;
	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Audio;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = assetText("Audio metadata");
	analysis.audioFormat = QStringLiteral("WAV");
	analysis.audioChannels = channels;
	analysis.audioSampleRate = static_cast<int>(sampleRate);
	analysis.audioBitsPerSample = bitsPerSample;
	analysis.audioDurationMs = durationMs;
	analysis.audioQtPlaybackCandidate = true;
	analysis.audioWavExportSupported = true;
	analysis.summary = assetText("WAV audio, %1 Hz, %2 channel(s)").arg(sampleRate).arg(channels);
	analysis.body = analysis.summary;
	analysis.detailLines << assetText("Audio format: WAV");
	analysis.detailLines << assetText("Codec tag: %1").arg(formatTag == 1 ? assetText("PCM") : QString::number(formatTag));
	analysis.detailLines << assetText("Channels: %1").arg(channels);
	analysis.detailLines << assetText("Sample rate: %1 Hz").arg(sampleRate);
	analysis.detailLines << assetText("Bits per sample: %1").arg(bitsPerSample);
	analysis.detailLines << assetText("Duration: %1").arg(durationText(durationMs));
	analysis.detailLines << assetText("Playback state: Qt backend candidate; playback control surface remains host-codec dependent.");
	analysis.detailLines << assetText("WAV export: supported");
	if (dataOffset > 0 && dataOffset < static_cast<quint32>(bytes.size())) {
		analysis.audioWaveformLines = waveformLines(bytes.mid(dataOffset, static_cast<int>(std::min<quint32>(dataBytes, 65536))), channels, bitsPerSample);
		analysis.detailLines << assetText("Waveform preview:");
		analysis.detailLines << analysis.audioWaveformLines;
	}
	analysis.detailLines << assetText("Bytes: %1").arg(sizeText(totalBytes >= 0 ? totalBytes : bytes.size()));
	analysis.rawLines = analysis.detailLines;
	Q_UNUSED(virtualPath);
	return analysis;
}

AssetAnalysis analyzeCompressedAudio(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes)
{
	if (!hasExtension(virtualPath, {QStringLiteral("ogg"), QStringLiteral("mp3"), QStringLiteral("flac")})) {
		return {};
	}
	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Audio;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = assetText("Audio metadata");
	analysis.audioFormat = normalizedExtension(virtualPath).toUpper();
	analysis.audioQtPlaybackCandidate = true;
	analysis.summary = assetText("%1 audio, codec metadata fallback").arg(analysis.audioFormat);
	analysis.body = analysis.summary;
	analysis.detailLines << assetText("Audio format: %1").arg(analysis.audioFormat);
	analysis.detailLines << assetText("Playback state: Qt backend candidate when the platform codec is available.");
	analysis.detailLines << assetText("WAV export: unavailable until decoder backend is enabled.");
	analysis.detailLines << assetText("Waveform preview: unavailable without decoded PCM samples.");
	analysis.detailLines << assetText("Bytes: %1").arg(sizeText(totalBytes >= 0 ? totalBytes : bytes.size()));
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

AssetAnalysis analyzeModel(const QString& virtualPath, const QByteArray& bytes)
{
	const QString ext = normalizedExtension(virtualPath);
	AssetAnalysis analysis;
	analysis.kind = AssetPreviewKind::Model;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = assetText("Model metadata");
	if (bytes.size() >= 72 && bytes.mid(0, 4) == "IDPO") {
		analysis.modelFormat = QStringLiteral("MDL");
		analysis.modelFamily = QStringLiteral("Quake MDL");
		analysis.modelSkinCount = readLe32Signed(bytes, 48);
		const int skinWidth = readLe32Signed(bytes, 52);
		const int skinHeight = readLe32Signed(bytes, 56);
		analysis.modelVertexCount = readLe32Signed(bytes, 60);
		analysis.modelTriangleCount = readLe32Signed(bytes, 64);
		analysis.modelFrameCount = readLe32Signed(bytes, 68);
		analysis.modelMaterialLines << assetText("Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
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
		analysis.modelMaterialLines << assetText("Skin size: %1 x %2").arg(skinWidth).arg(skinHeight);
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
		for (int surface = 0; surface < std::min(analysis.modelSurfaceCount, 8) && offset + 108 <= bytes.size(); ++surface) {
			if (bytes.mid(offset, 4) != "IDP3") {
				break;
			}
			const QString name = fixedLatin1(bytes, offset + 4, 64);
			const int shaderCount = readLe32Signed(bytes, offset + 76);
			const int vertexCount = readLe32Signed(bytes, offset + 80);
			const int triangleCount = readLe32Signed(bytes, offset + 84);
			const int nextOffset = readLe32Signed(bytes, offset + 108);
			analysis.modelVertexCount += std::max(0, vertexCount);
			analysis.modelTriangleCount += std::max(0, triangleCount);
			analysis.modelMaterialLines << assetText("Surface %1: %2 shaders, %3 vertices, %4 triangles").arg(name.isEmpty() ? QString::number(surface + 1) : name).arg(shaderCount).arg(vertexCount).arg(triangleCount);
			if (nextOffset <= 0) {
				break;
			}
			offset += nextOffset;
		}
	} else if (!QStringList {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm")}.contains(ext)) {
		return {};
	} else {
		analysis.modelFormat = ext.toUpper();
		analysis.modelFamily = assetText("Native idTech model boundary");
	}
	analysis.summary = assetText("%1 model, %2 frame(s)").arg(analysis.modelFormat.isEmpty() ? ext.toUpper() : analysis.modelFormat).arg(std::max(0, analysis.modelFrameCount));
	analysis.body = analysis.summary;
	analysis.detailLines << assetText("Model format: %1").arg(analysis.modelFormat.isEmpty() ? assetText("unknown") : analysis.modelFormat);
	analysis.detailLines << assetText("Native loader boundary: MDL, MD2, MD3 metadata first; Assimp remains optional future import/export.");
	analysis.detailLines << assetText("Frames: %1").arg(analysis.modelFrameCount);
	analysis.detailLines << assetText("Skins/materials: %1").arg(analysis.modelSkinCount);
	analysis.detailLines << assetText("Surfaces: %1").arg(analysis.modelSurfaceCount);
	analysis.detailLines << assetText("Tags: %1").arg(analysis.modelTagCount);
	analysis.detailLines << assetText("Vertices: %1").arg(analysis.modelVertexCount);
	analysis.detailLines << assetText("Triangles: %1").arg(analysis.modelTriangleCount);
	analysis.modelViewportLines << QStringLiteral("[model viewport]");
	analysis.modelViewportLines << assetText("Format: %1").arg(analysis.modelFormat);
	analysis.modelViewportLines << assetText("Frames: %1 / Surfaces: %2").arg(analysis.modelFrameCount).arg(analysis.modelSurfaceCount);
	analysis.modelViewportLines << assetText("Verts: %1 / Tris: %2").arg(analysis.modelVertexCount).arg(analysis.modelTriangleCount);
	if (!analysis.modelViewportLines.isEmpty()) {
		analysis.detailLines << assetText("Viewport summary:");
		analysis.detailLines << analysis.modelViewportLines;
	}
	if (!analysis.modelSkinPaths.isEmpty()) {
		analysis.detailLines << assetText("Skin paths:");
		analysis.detailLines << analysis.modelSkinPaths;
	}
	if (!analysis.modelAnimationNames.isEmpty()) {
		analysis.detailLines << assetText("Animation/frame names:");
		analysis.detailLines << analysis.modelAnimationNames;
	}
	if (!analysis.modelMaterialLines.isEmpty()) {
		analysis.detailLines << assetText("Skin/material dependencies:");
		analysis.detailLines << analysis.modelMaterialLines;
	}
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

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
		return assetText("CFG script");
	}
	if (languageId == QStringLiteral("shader")) {
		return assetText("idTech3 shader script");
	}
	if (languageId == QStringLiteral("quakec")) {
		return assetText("QuakeC");
	}
	if (languageId == QStringLiteral("idtech-text")) {
		return assetText("idTech text asset");
	}
	return assetText("Plain text");
}

QStringList syntaxHighlights(const QString& languageId, const QString& text)
{
	QStringList highlights;
	const QStringList lines = text.split('\n');
	const QStringList cfgKeywords = {QStringLiteral("bind"), QStringLiteral("set"), QStringLiteral("seta"), QStringLiteral("exec"), QStringLiteral("alias"), QStringLiteral("echo"), QStringLiteral("map")};
	const QStringList shaderKeywords = {QStringLiteral("shader"), QStringLiteral("surfaceparm"), QStringLiteral("qer_editorimage"), QStringLiteral("map"), QStringLiteral("blendfunc"), QStringLiteral("rgbgen"), QStringLiteral("tcmod")};
	const QStringList qcKeywords = {QStringLiteral("void"), QStringLiteral("float"), QStringLiteral("vector"), QStringLiteral("entity"), QStringLiteral("string"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("while"), QStringLiteral("return")};
	const QStringList keywords = languageId == QStringLiteral("cfg") ? cfgKeywords : (languageId == QStringLiteral("shader") ? shaderKeywords : (languageId == QStringLiteral("quakec") ? qcKeywords : QStringList {}));
	for (int lineIndex = 0; lineIndex < lines.size() && highlights.size() < 24; ++lineIndex) {
		const QString trimmed = lines[lineIndex].trimmed();
		if (trimmed.startsWith(QStringLiteral("//")) || trimmed.startsWith(QStringLiteral("#"))) {
			highlights << assetText("line %1: comment").arg(lineIndex + 1);
			continue;
		}
		for (const QString& keyword : keywords) {
			const QRegularExpression expression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(keyword)), QRegularExpression::CaseInsensitiveOption);
			if (expression.match(trimmed).hasMatch()) {
				highlights << assetText("line %1: keyword %2").arg(lineIndex + 1).arg(keyword);
				break;
			}
		}
		if (languageId == QStringLiteral("shader") && trimmed.endsWith(QLatin1Char('{'))) {
			highlights << assetText("line %1: shader block").arg(lineIndex + 1);
		}
	}
	return highlights;
}

QStringList textDiagnostics(const QString& text)
{
	QStringList diagnostics;
	int braceBalance = 0;
	const QStringList lines = text.split('\n');
	for (int index = 0; index < lines.size(); ++index) {
		const QString& line = lines[index];
		braceBalance += line.count('{');
		braceBalance -= line.count('}');
		if (line.size() > 160) {
			diagnostics << assetText("line %1: long line may wrap in compact editors").arg(index + 1);
		}
		if (line.contains(QStringLiteral("ERROR"), Qt::CaseInsensitive) || line.contains(QStringLiteral("WARNING"), Qt::CaseInsensitive)) {
			diagnostics << assetText("line %1: compiler-style diagnostic marker").arg(index + 1);
		}
	}
	if (braceBalance != 0) {
		diagnostics << assetText("brace balance: %1").arg(braceBalance);
	}
	if (diagnostics.isEmpty()) {
		diagnostics << assetText("No local text diagnostics.");
	}
	return diagnostics;
}

AssetAnalysis analyzeText(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes)
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
	analysis.title = assetText("Text and script preview");
	analysis.summary = assetText("%1 preview").arg(languageName(languageId));
	analysis.body = decoded;
	analysis.textLanguageId = languageId;
	analysis.textLanguageName = languageName(languageId);
	analysis.textSyntaxEngine = assetText("VibeStudio local highlighter boundary");
	analysis.textSaveState = assetText("clean read-only preview");
	analysis.textHighlightLines = syntaxHighlights(languageId, decoded);
	analysis.textDiagnosticLines = textDiagnostics(decoded);
	analysis.detailLines << assetText("Language: %1").arg(analysis.textLanguageName);
	analysis.detailLines << assetText("Syntax engine: %1").arg(analysis.textSyntaxEngine);
	analysis.detailLines << assetText("Encoding: UTF-8");
	analysis.detailLines << assetText("Save state: %1").arg(analysis.textSaveState);
	analysis.detailLines << assetText("Bytes: %1").arg(sizeText(totalBytes >= 0 ? totalBytes : bytes.size()));
	analysis.detailLines << assetText("Highlights:");
	analysis.detailLines << (analysis.textHighlightLines.isEmpty() ? QStringList {assetText("No syntax highlights in preview sample.")} : analysis.textHighlightLines);
	analysis.detailLines << assetText("Diagnostics:");
	analysis.detailLines << analysis.textDiagnosticLines;
	analysis.rawLines = analysis.detailLines;
	analysis.rawLines << assetText("Preview text follows:");
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

bool shouldSkipDirectory(const QString& path)
{
	const QString name = QFileInfo(path).fileName().toLower();
	return QStringList {QStringLiteral(".git"), QStringLiteral("builddir"), QStringLiteral("build"), QStringLiteral(".vibestudio")}.contains(name);
}

} // namespace

bool AssetImageConversionReport::succeeded() const
{
	return errorCount == 0;
}

bool AssetAudioExportReport::succeeded() const
{
	return error.isEmpty() && (dryRun || written);
}

bool AssetTextSearchReport::succeeded() const
{
	return saveState != QStringLiteral("failed");
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
	const QString ext = normalizedExtension(virtualPath);
	if (QStringList {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("tga"), QStringLiteral("webp"), QStringLiteral("pcx"), QStringLiteral("wal"), QStringLiteral("mip"), QStringLiteral("lmp")}.contains(ext)) {
		return AssetPreviewKind::Image;
	}
	if (QStringList {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm")}.contains(ext)) {
		return AssetPreviewKind::Model;
	}
	if (QStringList {QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("mp3"), QStringLiteral("flac")}.contains(ext)) {
		return AssetPreviewKind::Audio;
	}
	if (defaultTextExtensions().contains(ext)) {
		return AssetPreviewKind::Text;
	}
	return AssetPreviewKind::Unknown;
}

AssetAnalysis analyzeAssetBytes(const QString& virtualPath, const QByteArray& bytes, qint64 totalBytes)
{
	AssetAnalysis analysis = analyzeImage(virtualPath, bytes, totalBytes);
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
	analysis = analyzeModel(virtualPath, bytes);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis = analyzeText(virtualPath, bytes, totalBytes);
	if (analysis.kind != AssetPreviewKind::Unknown) {
		return analysis;
	}
	analysis.kind = AssetPreviewKind::Binary;
	analysis.kindId = assetPreviewKindId(analysis.kind);
	analysis.title = assetText("Binary asset");
	analysis.summary = assetText("Binary asset, %1 sampled.").arg(sizeText(bytes.size()));
	analysis.detailLines << assetText("Asset kind: binary");
	analysis.detailLines << assetText("Bytes: %1").arg(sizeText(totalBytes >= 0 ? totalBytes : bytes.size()));
	analysis.rawLines = analysis.detailLines;
	return analysis;
}

AssetImageConversionReport convertPackageImages(const PackageArchive& archive, const AssetImageConversionRequest& request)
{
	AssetImageConversionReport report;
	report.sourcePath = archive.sourcePath();
	report.outputDirectory = QFileInfo(request.outputDirectory).absoluteFilePath();
	report.dryRun = request.dryRun;
	if (!archive.isOpen()) {
		report.warnings << assetText("No package is open.");
		report.errorCount = 1;
		return report;
	}
	if (request.outputDirectory.trimmed().isEmpty()) {
		report.warnings << assetText("Output directory is required.");
		report.errorCount = 1;
		return report;
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
		report.warnings << assetText("Unable to create output directory.");
		report.errorCount = std::max(1, report.requestedCount);
		return report;
	}
	for (const QString& virtualPath : requestedPaths) {
		AssetImageConversionEntryResult result;
		result.virtualPath = virtualPath;
		result.outputFormat = QString::fromLatin1(imageFormatBytes(request.outputFormat)).toLower();
		result.dryRun = request.dryRun;
		result.outputPath = safePackageOutputPath(report.outputDirectory, outputFileNameForEntry(virtualPath, result.outputFormat), &result.error);
		QByteArray bytes;
		QString readError;
		if (result.error.isEmpty() && !archive.readEntryBytes(virtualPath, &bytes, &readError)) {
			result.error = readError.isEmpty() ? assetText("Unable to read package entry.") : readError;
		}
		result.inputBytes = bytes.size();
		QImage image;
		if (result.error.isEmpty()) {
			image.loadFromData(bytes);
			if (image.isNull()) {
				result.error = assetText("Entry is not a supported Qt image.");
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
			const QString paletteMode = request.paletteMode.trimmed().toLower();
			if (paletteMode == QStringLiteral("grayscale")) {
				output = output.convertToFormat(QImage::Format_Grayscale8);
			} else if (paletteMode == QStringLiteral("indexed")) {
				output = output.convertToFormat(QImage::Format_Indexed8);
			}
			result.afterSize = output.size();
			QByteArray outputBytes;
			QBuffer buffer(&outputBytes);
			buffer.open(QIODevice::WriteOnly);
			QImageWriter writer(&buffer, imageFormatBytes(request.outputFormat));
			if (!writer.write(output)) {
				result.error = writer.errorString().isEmpty() ? assetText("Unable to encode output image.") : writer.errorString();
			} else {
				result.outputBytes = outputBytes.size();
				result.previewLines << assetText("Before: %1 x %2").arg(result.beforeSize.width()).arg(result.beforeSize.height());
				result.previewLines << assetText("After: %1 x %2").arg(result.afterSize.width()).arg(result.afterSize.height());
				result.previewLines << assetText("Format: %1").arg(result.outputFormat);
				if (request.dryRun) {
					result.message = QFileInfo::exists(result.outputPath) ? assetText("Would overwrite converted image.") : assetText("Would write converted image.");
				} else {
					const QFileInfo outputInfo(result.outputPath);
					const bool existedBefore = outputInfo.exists();
					if (existedBefore && !request.overwriteExisting) {
						result.error = assetText("Output already exists. Use --overwrite to replace it.");
					} else if (!QDir().mkpath(outputInfo.absolutePath())) {
						result.error = assetText("Unable to create output parent directory.");
					} else {
						QSaveFile file(result.outputPath);
						if (!file.open(QIODevice::WriteOnly)) {
							result.error = assetText("Unable to open output image.");
						} else if (file.write(outputBytes) != outputBytes.size()) {
							result.error = assetText("Unable to write output image.");
						} else if (!file.commit()) {
							result.error = assetText("Unable to commit output image.");
						} else {
							result.written = true;
							result.message = existedBefore ? assetText("Overwrote converted image.") : assetText("Wrote converted image.");
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
	lines << assetText("Image conversion");
	lines << assetText("Source: %1").arg(report.sourcePath);
	lines << assetText("Output: %1").arg(report.outputDirectory);
	lines << assetText("Mode: %1").arg(report.dryRun ? assetText("dry run") : assetText("write"));
	lines << assetText("Requested: %1").arg(report.requestedCount);
	lines << assetText("Processed: %1").arg(report.processedCount);
	lines << assetText("Written: %1").arg(report.writtenCount);
	lines << assetText("Errors: %1").arg(report.errorCount);
	for (const AssetImageConversionEntryResult& result : report.entries) {
		lines << QStringLiteral("- %1 -> %2").arg(result.virtualPath, QDir::toNativeSeparators(result.outputPath));
		lines << QStringLiteral("  %1").arg(result.error.isEmpty() ? result.message : result.error);
		for (const QString& previewLine : result.previewLines) {
			lines << QStringLiteral("  %1").arg(previewLine);
		}
	}
	for (const QString& warning : report.warnings) {
		lines << assetText("Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

AssetAudioExportReport exportPackageAudioToWav(const PackageArchive& archive, const QString& virtualPath, const QString& outputPath, bool dryRun, bool overwriteExisting)
{
	AssetAudioExportReport report;
	report.sourcePath = archive.sourcePath();
	report.virtualPath = virtualPath;
	report.outputPath = QFileInfo(outputPath).absoluteFilePath();
	report.dryRun = dryRun;
	if (!archive.isOpen()) {
		report.error = assetText("No package is open.");
		return report;
	}
	if (outputPath.trimmed().isEmpty()) {
		report.error = assetText("WAV output path is required.");
		return report;
	}
	QByteArray bytes;
	QString readError;
	if (!archive.readEntryBytes(virtualPath, &bytes, &readError)) {
		report.error = readError.isEmpty() ? assetText("Unable to read audio entry.") : readError;
		return report;
	}
	const AssetAnalysis analysis = analyzeWav(virtualPath, bytes, bytes.size());
	if (analysis.kind != AssetPreviewKind::Audio || !analysis.audioWavExportSupported) {
		report.error = assetText("Only readable WAV/PCM entries can be exported by the MVP helper.");
		return report;
	}
	report.bytes = bytes.size();
	if (dryRun) {
		report.message = QFileInfo::exists(report.outputPath) ? assetText("Would overwrite WAV file.") : assetText("Would write WAV file.");
		return report;
	}
	const QFileInfo outputInfo(report.outputPath);
	const bool existedBefore = outputInfo.exists();
	if (existedBefore && !overwriteExisting) {
		report.error = assetText("Output already exists. Use --overwrite to replace it.");
		return report;
	}
	if (!QDir().mkpath(outputInfo.absolutePath())) {
		report.error = assetText("Unable to create output parent directory.");
		return report;
	}
	QSaveFile file(report.outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		report.error = assetText("Unable to open WAV output.");
	} else if (file.write(bytes) != bytes.size()) {
		report.error = assetText("Unable to write WAV output.");
	} else if (!file.commit()) {
		report.error = assetText("Unable to commit WAV output.");
	} else {
		report.written = true;
		report.message = existedBefore ? assetText("Overwrote WAV file.") : assetText("Wrote WAV file.");
	}
	return report;
}

QString assetAudioExportReportText(const AssetAudioExportReport& report)
{
	QStringList lines;
	lines << assetText("Audio WAV export");
	lines << assetText("Source: %1").arg(report.sourcePath);
	lines << assetText("Entry: %1").arg(report.virtualPath);
	lines << assetText("Output: %1").arg(report.outputPath);
	lines << assetText("Mode: %1").arg(report.dryRun ? assetText("dry run") : assetText("write"));
	lines << assetText("Bytes: %1").arg(report.bytes);
	lines << (report.error.isEmpty() ? report.message : assetText("Error: %1").arg(report.error));
	return lines.join('\n');
}

AssetTextSearchReport findReplaceProjectText(const AssetTextSearchRequest& request)
{
	AssetTextSearchReport report;
	report.rootPath = QFileInfo(request.rootPath).absoluteFilePath();
	report.findText = request.findText;
	report.replaceText = request.replaceText;
	report.replace = request.replace;
	report.dryRun = request.dryRun;
	if (request.rootPath.trimmed().isEmpty() || !QFileInfo(report.rootPath).isDir()) {
		report.warnings << assetText("Project root path is required and must be a directory.");
		report.saveState = QStringLiteral("failed");
		return report;
	}
	if (request.findText.isEmpty()) {
		report.warnings << assetText("Find text is required.");
		report.saveState = QStringLiteral("failed");
		return report;
	}
	const QStringList extensions = request.extensions.isEmpty() ? defaultTextExtensions() : request.extensions;
	const Qt::CaseSensitivity sensitivity = request.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
	QDirIterator iterator(report.rootPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		const QString filePath = iterator.next();
		if (shouldSkipDirectory(QFileInfo(filePath).absolutePath()) || !extensions.contains(normalizedExtension(filePath))) {
			continue;
		}
		++report.filesScanned;
		QFile file(filePath);
		if (!file.open(QIODevice::ReadOnly)) {
			report.warnings << assetText("Unable to read %1").arg(filePath);
			continue;
		}
		const QByteArray bytes = file.readAll();
		file.close();
		bool utf8Ok = false;
		QString text = decodeUtf8(bytes, &utf8Ok);
		if (!utf8Ok || !bytesLookTextual(bytes)) {
			continue;
		}
		const QStringList lines = text.split('\n');
		bool fileMatched = false;
		for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
			int column = lines[lineIndex].indexOf(request.findText, 0, sensitivity);
			while (column >= 0) {
				AssetTextMatch match;
				match.filePath = filePath;
				match.line = lineIndex + 1;
				match.column = column + 1;
				match.lineText = lines[lineIndex].trimmed();
				report.matches.push_back(match);
				++report.matchCount;
				fileMatched = true;
				column = lines[lineIndex].indexOf(request.findText, column + request.findText.size(), sensitivity);
			}
		}
		if (fileMatched) {
			++report.filesWithMatches;
			if (request.replace) {
				const int replacements = text.count(request.findText, sensitivity);
				QString replaced = text;
				replaced.replace(request.findText, request.replaceText, sensitivity);
				report.replacementCount += replacements;
				if (!request.dryRun && replacements > 0) {
					report.saveState = QStringLiteral("saving");
					QSaveFile output(filePath);
					if (!output.open(QIODevice::WriteOnly)) {
						report.warnings << assetText("Unable to open %1 for writing.").arg(filePath);
						report.saveState = QStringLiteral("failed");
					} else {
						const QByteArray encoded = replaced.toUtf8();
						if (output.write(encoded) != encoded.size() || !output.commit()) {
							report.warnings << assetText("Unable to save %1.").arg(filePath);
							report.saveState = QStringLiteral("failed");
						}
					}
				}
			}
		}
	}
	if (report.saveState != QStringLiteral("failed")) {
		if (request.replace && !request.dryRun && report.replacementCount > 0) {
			report.saveState = QStringLiteral("saved");
		} else if (request.replace && report.replacementCount > 0) {
			report.saveState = QStringLiteral("modified");
		} else {
			report.saveState = QStringLiteral("clean");
		}
	}
	return report;
}

QString assetTextSearchReportText(const AssetTextSearchReport& report)
{
	QStringList lines;
	lines << assetText("Project text search");
	lines << assetText("Root: %1").arg(report.rootPath);
	lines << assetText("Find: %1").arg(report.findText);
	lines << assetText("Replace: %1").arg(report.replace ? report.replaceText : assetText("disabled"));
	lines << assetText("Mode: %1").arg(report.dryRun ? assetText("dry run") : assetText("write"));
	lines << assetText("Files scanned: %1").arg(report.filesScanned);
	lines << assetText("Files with matches: %1").arg(report.filesWithMatches);
	lines << assetText("Matches: %1").arg(report.matchCount);
	lines << assetText("Replacements: %1").arg(report.replacementCount);
	lines << assetText("Save state: %1").arg(report.saveState);
	for (const AssetTextMatch& match : report.matches) {
		lines << QStringLiteral("- %1:%2:%3 %4").arg(QDir::toNativeSeparators(match.filePath)).arg(match.line).arg(match.column).arg(match.lineText);
	}
	for (const QString& warning : report.warnings) {
		lines << assetText("Warning: %1").arg(warning);
	}
	return lines.join('\n');
}

} // namespace vibestudio
