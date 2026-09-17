#include "core/asset_tools.h"
#include "core/idtech_image.h"
#include "core/package_archive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return file.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

void appendLe16(QByteArray* data, quint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* data, quint32 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
	data->append(static_cast<char>((value >> 16) & 0xff));
	data->append(static_cast<char>((value >> 24) & 0xff));
}

void appendLe64(QByteArray* data, quint64 value)
{
	for (int index = 0; index < 8; ++index) {
		data->append(static_cast<char>((value >> (index * 8)) & 0xff));
	}
}

void appendFixed(QByteArray* data, const QByteArray& value, int size)
{
	QByteArray padded = value.left(size);
	padded.append(QByteArray(size - padded.size(), '\0'));
	data->append(padded);
}

// ---------------------------------------------------------------------------
// Image fixtures, built from the public format specifications.
// ---------------------------------------------------------------------------

// Quake "qpic" lump: int width, int height, byte indices[width * height].
// Quake Specifications (Olivier Montanuy), section on gfx.wad/lmp pictures.
QByteArray quakeLumpFixture(int width, int height)
{
	QByteArray data;
	appendLe32(&data, static_cast<quint32>(width));
	appendLe32(&data, static_cast<quint32>(height));
	for (int index = 0; index < width * height; ++index) {
		data.append(static_cast<char>(index % 200));
	}
	return data;
}

// Quake II miptex_t: name[32], width, height, offsets[4], animname[32], flags,
// contents, value. Header is 100 bytes, then four mip levels.
QByteArray quake2WalFixture(int width, int height, quint32 flags, quint32 contents, qint32 value)
{
	QByteArray data;
	appendFixed(&data, QByteArray("e1u1/floor3_3"), 32);
	appendLe32(&data, static_cast<quint32>(width));
	appendLe32(&data, static_cast<quint32>(height));
	quint32 offset = 100;
	QByteArray levels;
	QVector<quint32> offsets;
	for (int level = 0; level < 4; ++level) {
		const int levelWidth = std::max(1, width >> level);
		const int levelHeight = std::max(1, height >> level);
		offsets.append(offset);
		offset += static_cast<quint32>(levelWidth * levelHeight);
		for (int index = 0; index < levelWidth * levelHeight; ++index) {
			levels.append(static_cast<char>((index + level) % 200));
		}
	}
	for (int level = 0; level < 4; ++level) {
		appendLe32(&data, offsets[level]);
	}
	appendFixed(&data, QByteArray("e1u1/floor3_4"), 32);
	appendLe32(&data, flags);
	appendLe32(&data, contents);
	appendLe32(&data, static_cast<quint32>(value));
	data.append(levels);
	return data;
}

// ZSoft PCX: 128-byte header, RLE scanlines, then 0x0C plus a 768-byte palette.
QByteArray pcxFixture(int width, int height)
{
	QByteArray data(128, '\0');
	data[0] = static_cast<char>(0x0a); // manufacturer
	data[1] = static_cast<char>(5); // version
	data[2] = static_cast<char>(1); // RLE encoding
	data[3] = static_cast<char>(8); // bits per pixel
	const auto writeLe16At = [&data](int offset, quint16 value) {
		data[offset] = static_cast<char>(value & 0xff);
		data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
	};
	writeLe16At(4, 0); // xmin
	writeLe16At(6, 0); // ymin
	writeLe16At(8, static_cast<quint16>(width - 1)); // xmax
	writeLe16At(10, static_cast<quint16>(height - 1)); // ymax
	writeLe16At(12, 72);
	writeLe16At(14, 72);
	data[65] = static_cast<char>(1); // colour planes
	writeLe16At(66, static_cast<quint16>(width)); // bytes per line
	writeLe16At(68, 1); // palette info: colour

	for (int row = 0; row < height; ++row) {
		int column = 0;
		while (column < width) {
			const int run = std::min(width - column, 63);
			data.append(static_cast<char>(0xc0 | run));
			data.append(static_cast<char>((row * 7 + column) % 100));
			column += run;
		}
	}
	data.append(static_cast<char>(0x0c));
	for (int index = 0; index < 256; ++index) {
		data.append(static_cast<char>(index));
		data.append(static_cast<char>(255 - index));
		data.append(static_cast<char>((index * 3) % 256));
	}
	return data;
}

// Doom picture format: width, height, leftoffset, topoffset (all 16-bit), then
// one 32-bit column offset per column; each column is a chain of posts.
QByteArray doomPatchFixture(int width, int height)
{
	QByteArray header;
	appendLe16(&header, static_cast<quint16>(width));
	appendLe16(&header, static_cast<quint16>(height));
	appendLe16(&header, static_cast<quint16>(static_cast<qint16>(-width / 2)));
	appendLe16(&header, static_cast<quint16>(static_cast<qint16>(height)));

	QByteArray columns;
	const quint32 columnBase = static_cast<quint32>(8 + width * 4);
	QVector<quint32> offsets;
	for (int column = 0; column < width; ++column) {
		offsets.append(columnBase + static_cast<quint32>(columns.size()));
		columns.append(static_cast<char>(0)); // topdelta
		columns.append(static_cast<char>(height)); // length
		columns.append(static_cast<char>(0)); // unused padding byte
		for (int row = 0; row < height; ++row) {
			columns.append(static_cast<char>((column * 13 + row) % 200));
		}
		columns.append(static_cast<char>(0)); // unused padding byte
		columns.append(static_cast<char>(0xff)); // end of column
	}
	QByteArray data = header;
	for (int column = 0; column < width; ++column) {
		appendLe32(&data, offsets[column]);
	}
	data.append(columns);
	return data;
}

// ---------------------------------------------------------------------------
// Audio fixtures
// ---------------------------------------------------------------------------

QByteArray wavFixture()
{
	const QByteArray samples = QByteArray::fromHex("0000ff7f00800100");
	QByteArray data;
	data.append("RIFF");
	appendLe32(&data, static_cast<quint32>(36 + samples.size()));
	data.append("WAVE");
	data.append("fmt ");
	appendLe32(&data, 16);
	appendLe16(&data, 1);
	appendLe16(&data, 1);
	appendLe32(&data, 11025);
	appendLe32(&data, 11025 * 2);
	appendLe16(&data, 2);
	appendLe16(&data, 16);
	data.append("data");
	appendLe32(&data, static_cast<quint32>(samples.size()));
	data.append(samples);
	return data;
}

// Two channels of `frames` frames: channel 0 sits at positive full scale and
// channel 1 at negative full scale, so the envelope is easy to assert on.
QByteArray stereoFullScaleWav(quint16 formatTag, quint16 bitsPerSample, int frames, bool extensible)
{
	const int bytesPerSample = bitsPerSample / 8;
	const quint16 channels = 2;
	const quint16 blockAlign = static_cast<quint16>(channels * bytesPerSample);
	const quint32 sampleRate = 22050;
	QByteArray samples;
	for (int frame = 0; frame < frames; ++frame) {
		for (int channel = 0; channel < channels; ++channel) {
			const bool positive = channel == 0;
			if (formatTag == 0x0003) {
				if (bitsPerSample == 32) {
					const float value = positive ? 1.0f : -1.0f;
					quint32 raw = 0;
					std::memcpy(&raw, &value, sizeof(raw));
					appendLe32(&samples, raw);
				} else {
					const double value = positive ? 1.0 : -1.0;
					quint64 raw = 0;
					std::memcpy(&raw, &value, sizeof(raw));
					appendLe64(&samples, raw);
				}
			} else if (bitsPerSample == 8) {
				samples.append(static_cast<char>(positive ? 0xff : 0x00));
			} else if (bitsPerSample == 16) {
				appendLe16(&samples, positive ? 0x7fff : 0x8000);
			} else if (bitsPerSample == 24) {
				const quint32 value = positive ? 0x7fffffu : 0x800000u;
				samples.append(static_cast<char>(value & 0xff));
				samples.append(static_cast<char>((value >> 8) & 0xff));
				samples.append(static_cast<char>((value >> 16) & 0xff));
			} else {
				appendLe32(&samples, positive ? 0x7fffffffu : 0x80000000u);
			}
		}
	}

	const quint16 headerTag = extensible ? 0xfffe : formatTag;
	const quint32 formatChunkSize = extensible ? 40u : 16u;
	QByteArray data;
	data.append("RIFF");
	appendLe32(&data, static_cast<quint32>(4 + 8 + formatChunkSize + 8 + samples.size()));
	data.append("WAVE");
	data.append("fmt ");
	appendLe32(&data, formatChunkSize);
	appendLe16(&data, headerTag);
	appendLe16(&data, channels);
	appendLe32(&data, sampleRate);
	appendLe32(&data, sampleRate * blockAlign);
	appendLe16(&data, blockAlign);
	appendLe16(&data, bitsPerSample);
	if (extensible) {
		appendLe16(&data, 22); // cbSize
		appendLe16(&data, bitsPerSample); // wValidBitsPerSample
		appendLe32(&data, 3); // dwChannelMask: front left + front right
		// KSDATAFORMAT_SUBTYPE GUID: the first two bytes carry the real tag.
		appendLe16(&data, formatTag);
		data.append(QByteArray::fromHex("000000001000800000aa00389b71"));
	}
	data.append("data");
	appendLe32(&data, static_cast<quint32>(samples.size()));
	data.append(samples);
	return data;
}

// One BOS page carrying a 30-byte Vorbis identification header, then an EOS
// page whose granule position gives the stream length.
QByteArray oggVorbisFixture(int channels, quint32 sampleRate, quint64 granule)
{
	QByteArray packet;
	packet.append(static_cast<char>(0x01));
	packet.append("vorbis");
	appendLe32(&packet, 0);
	packet.append(static_cast<char>(channels));
	appendLe32(&packet, sampleRate);
	appendLe32(&packet, 160000); // bitrate maximum
	appendLe32(&packet, 128000); // bitrate nominal
	appendLe32(&packet, 96000); // bitrate minimum
	packet.append(static_cast<char>(0xb8));
	packet.append(static_cast<char>(0x01));

	QByteArray data;
	data.append("OggS");
	data.append(static_cast<char>(0)); // stream structure version
	data.append(static_cast<char>(0x02)); // beginning of stream
	appendLe64(&data, 0);
	appendLe32(&data, 0x12345678);
	appendLe32(&data, 0);
	appendLe32(&data, 0); // checksum, unchecked by the analyser
	data.append(static_cast<char>(1));
	data.append(static_cast<char>(packet.size()));
	data.append(packet);

	data.append("OggS");
	data.append(static_cast<char>(0));
	data.append(static_cast<char>(0x04)); // end of stream
	appendLe64(&data, granule);
	appendLe32(&data, 0x12345678);
	appendLe32(&data, 1);
	appendLe32(&data, 0);
	data.append(static_cast<char>(1));
	data.append(static_cast<char>(0));
	return data;
}

// The 28-byte end-of-stream page oggVorbisFixture appends last: a 27-byte page
// header plus a one-entry segment table and no payload.
constexpr qsizetype kOggEosPageBytes = 28;

// A mid-stream page: header_type 0, so RFC 3533 section 6.2 bit 0x04 is clear and
// its granule position is only a lower bound on the stream length.
QByteArray oggMidStreamPage(quint64 granule, quint32 sequence)
{
	QByteArray data;
	data.append("OggS");
	data.append(static_cast<char>(0));
	data.append(static_cast<char>(0));
	appendLe64(&data, granule);
	appendLe32(&data, 0x12345678);
	appendLe32(&data, sequence);
	appendLe32(&data, 0);
	data.append(static_cast<char>(1));
	data.append(static_cast<char>(0));
	return data;
}

// ID3v2 header with an empty tag body, then three MPEG-1 Layer III frames at
// 128 kbps / 44100 Hz joint stereo.
QByteArray mp3Fixture(int frameCount)
{
	QByteArray data;
	data.append("ID3");
	data.append(static_cast<char>(0x03));
	data.append(static_cast<char>(0x00));
	data.append(static_cast<char>(0x00));
	data.append(QByteArray(4, '\0')); // synchsafe size of zero
	const int frameBytes = 417;
	for (int frame = 0; frame < frameCount; ++frame) {
		QByteArray block(frameBytes, '\0');
		block[0] = static_cast<char>(0xff);
		block[1] = static_cast<char>(0xfb); // MPEG-1, Layer III, no CRC
		block[2] = static_cast<char>(0x90); // 128 kbps, 44100 Hz, no padding
		block[3] = static_cast<char>(0x40); // joint stereo
		data.append(block);
	}
	return data;
}

// "fLaC" plus a final STREAMINFO metadata block.
QByteArray flacFixture(quint32 sampleRate, int channels, int bitsPerSample, quint64 totalSamples)
{
	QByteArray info(34, '\0');
	const auto put = [&info](int offset, quint8 value) {
		info[offset] = static_cast<char>(value);
	};
	put(0, 0x10); // minimum block size 4096
	put(1, 0x00);
	put(2, 0x10); // maximum block size 4096
	put(3, 0x00);
	put(10, static_cast<quint8>((sampleRate >> 12) & 0xff));
	put(11, static_cast<quint8>((sampleRate >> 4) & 0xff));
	put(12, static_cast<quint8>(((sampleRate & 0x0f) << 4) | ((channels - 1) << 1) | (((bitsPerSample - 1) >> 4) & 0x01)));
	put(13, static_cast<quint8>((((bitsPerSample - 1) & 0x0f) << 4) | static_cast<quint8>((totalSamples >> 32) & 0x0f)));
	put(14, static_cast<quint8>((totalSamples >> 24) & 0xff));
	put(15, static_cast<quint8>((totalSamples >> 16) & 0xff));
	put(16, static_cast<quint8>((totalSamples >> 8) & 0xff));
	put(17, static_cast<quint8>(totalSamples & 0xff));

	QByteArray data;
	data.append("fLaC");
	data.append(static_cast<char>(0x80)); // last block, type 0 (STREAMINFO)
	data.append(static_cast<char>(0x00));
	data.append(static_cast<char>(0x00));
	data.append(static_cast<char>(0x22)); // 34 bytes
	data.append(info);
	return data;
}

float peakValue(const AssetAudioPeaks& peaks, int channel, int bucket, bool maximum)
{
	const qsizetype index = (static_cast<qsizetype>(channel) * peaks.bucketCount + bucket) * 2 + (maximum ? 1 : 0);
	if (index < 0 || index >= peaks.peaks.size()) {
		return 0.0f;
	}
	return peaks.peaks[index];
}

bool checkFullScalePeaks(const AssetAudioPeaks& peaks, const char* label)
{
	bool ok = true;
	ok &= expect(peaks.valid, label);
	ok &= expect(peaks.channels == 2, label);
	ok &= expect(peaks.frameCount == 8, label);
	ok &= expect(peaks.sampleRate == 22050, label);
	if (!peaks.valid || peaks.bucketCount <= 0) {
		return false;
	}
	float channelZeroMax = -2.0f;
	float channelOneMin = 2.0f;
	for (int bucket = 0; bucket < peaks.bucketCount; ++bucket) {
		channelZeroMax = std::max(channelZeroMax, peakValue(peaks, 0, bucket, true));
		channelOneMin = std::min(channelOneMin, peakValue(peaks, 1, bucket, false));
	}
	ok &= expect(channelZeroMax > 0.98f, label);
	ok &= expect(channelOneMin < -0.98f, label);
	return ok;
}

bool linesContain(const QStringList& lines, const QString& needle)
{
	for (const QString& line : lines) {
		if (line.contains(needle, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

} // namespace

int main()
{
	bool ok = true;
	QTemporaryDir tempDir;
	ok &= expect(tempDir.isValid(), "temporary directory should be valid");
	QDir root(tempDir.path());
	ok &= expect(root.mkpath(QStringLiteral("package/textures")), "package textures directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("package/sound")), "package sound directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("package/gfx")), "package gfx directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("package/pics")), "package pics directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("project/scripts")), "project scripts directory should be created");

	QImage image(4, 4, QImage::Format_ARGB32);
	image.fill(qRgba(80, 40, 20, 255));
	ok &= expect(image.save(root.filePath(QStringLiteral("package/textures/wall.png")), "PNG"), "image fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("package/sound/pickup.wav")), wavFixture()), "WAV fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("project/scripts/autoexec.cfg")), QByteArray("set developer 1\nbind SPACE +jump\n")), "CFG fixture should be written");

	const QByteArray lumpBytes = quakeLumpFixture(16, 8);
	const QByteArray walBytes = quake2WalFixture(16, 16, 0x00000010u, 0x00000001u, 42);
	const QByteArray pcxBytes = pcxFixture(8, 4);
	const QByteArray patchBytes = doomPatchFixture(6, 5);
	ok &= expect(writeFile(root.filePath(QStringLiteral("package/gfx/menu.lmp")), lumpBytes), "LMP fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("package/textures/floor3_3.wal")), walBytes), "WAL fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("package/pics/conchars.pcx")), pcxBytes), "PCX fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("package/textures/troo.lmp")), patchBytes), "Doom patch fixture should be written");

	// ---- idTech image analysis ------------------------------------------------

	const AssetAnalysis lumpAnalysis = analyzeAssetBytes(QStringLiteral("gfx/menu.lmp"), lumpBytes, lumpBytes.size());
	ok &= expect(lumpAnalysis.kind == AssetPreviewKind::Image, "Quake lump should analyse as an image");
	ok &= expect(lumpAnalysis.imageSize == QSize(16, 8), "Quake lump dimensions mismatch");
	ok &= expect(lumpAnalysis.imageIdTechFormat, "Quake lump should be reported as an idTech format");
	ok &= expect(lumpAnalysis.imagePaletteAware, "Quake lump should be palette-aware");
	ok &= expect(!lumpAnalysis.imagePixels.isNull(), "Quake lump should decode to real pixels");
	ok &= expect(!lumpAnalysis.imagePaletteId.isEmpty(), "Quake lump should name the palette it used");
	ok &= expect(lumpAnalysis.imagePaletteGenerated, "a standalone lump has no package palette, so the fallback must be reported");

	const AssetAnalysis walAnalysis = analyzeAssetBytes(QStringLiteral("textures/floor3_3.wal"), walBytes, walBytes.size());
	ok &= expect(walAnalysis.kind == AssetPreviewKind::Image, "WAL should analyse as an image");
	ok &= expect(walAnalysis.imageSize == QSize(16, 16), "WAL dimensions mismatch");
	ok &= expect(walAnalysis.imageMipLevelCount >= 4, "WAL should report four mip levels");
	ok &= expect(walAnalysis.imageSurfaceFlags == 0x00000010u, "WAL surface flags mismatch");
	ok &= expect(walAnalysis.imageContentFlags == 0x00000001u, "WAL content flags mismatch");
	ok &= expect(walAnalysis.imageSurfaceValue == 42, "WAL surface value mismatch");
	ok &= expect(linesContain(walAnalysis.detailLines, QStringLiteral("Mip levels")), "WAL detail lines should mention mip levels");

	const AssetAnalysis pcxAnalysis = analyzeAssetBytes(QStringLiteral("pics/conchars.pcx"), pcxBytes, pcxBytes.size());
	ok &= expect(pcxAnalysis.kind == AssetPreviewKind::Image, "PCX should analyse as an image");
	ok &= expect(pcxAnalysis.imageSize == QSize(8, 4), "PCX dimensions mismatch");
	ok &= expect(!pcxAnalysis.imagePixels.isNull(), "PCX should decode to real pixels");

	const AssetAnalysis patchAnalysis = analyzeAssetBytes(QStringLiteral("textures/troo.lmp"), patchBytes, patchBytes.size());
	ok &= expect(patchAnalysis.kind == AssetPreviewKind::Image, "Doom patch should analyse as an image");
	ok &= expect(patchAnalysis.imageSize == QSize(6, 5), "Doom patch dimensions mismatch");
	ok &= expect(!patchAnalysis.imagePixels.isNull(), "Doom patch should decode to real pixels");

	// A payload that is not an image must still fall through to the other
	// analysers rather than being claimed by the image path.
	const AssetAnalysis wavAsImage = analyzeAssetBytes(QStringLiteral("sound/pickup.wav"), wavFixture(), -1);
	ok &= expect(wavAsImage.kind == AssetPreviewKind::Audio, "WAV must not be captured by the image analyser");

	PackageArchive archive;
	QString error;
	ok &= expect(archive.load(root.filePath(QStringLiteral("package")), &error), "folder package should load");

	// ---- conversion ----------------------------------------------------------

	AssetImageConversionRequest dryImageRequest;
	dryImageRequest.virtualPaths = {QStringLiteral("textures/wall.png")};
	dryImageRequest.outputDirectory = root.filePath(QStringLiteral("converted-dry"));
	dryImageRequest.outputFormat = QStringLiteral("bmp");
	dryImageRequest.resizeSize = QSize(2, 2);
	dryImageRequest.paletteMode = QStringLiteral("grayscale");
	dryImageRequest.dryRun = true;
	const AssetImageConversionReport dryImageReport = convertPackageImages(archive, dryImageRequest);
	ok &= expect(dryImageReport.succeeded(), "dry-run image conversion should succeed");
	ok &= expect(dryImageReport.processedCount == 1, "dry-run image conversion count mismatch");
	ok &= expect(!QFileInfo::exists(root.filePath(QStringLiteral("converted-dry/textures/wall.bmp"))), "dry-run image conversion should not write output");
	ok &= expect(!dryImageReport.entries.isEmpty() && dryImageReport.entries.front().afterSize == QSize(2, 2), "dry-run image conversion resize preview mismatch");

	AssetImageConversionRequest writeImageRequest = dryImageRequest;
	writeImageRequest.outputDirectory = root.filePath(QStringLiteral("converted"));
	writeImageRequest.dryRun = false;
	const AssetImageConversionReport writeImageReport = convertPackageImages(archive, writeImageRequest);
	ok &= expect(writeImageReport.succeeded(), "write image conversion should succeed");
	ok &= expect(writeImageReport.writtenCount == 1, "write image conversion count mismatch");
	ok &= expect(QFileInfo::exists(root.filePath(QStringLiteral("converted/textures/wall.bmp"))), "converted image should exist");

	// The idTech formats that used to fail with "not a supported Qt image".
	AssetImageConversionRequest idTechRequest;
	idTechRequest.virtualPaths = {
		QStringLiteral("gfx/menu.lmp"),
		QStringLiteral("textures/floor3_3.wal"),
		QStringLiteral("pics/conchars.pcx"),
		QStringLiteral("textures/troo.lmp"),
	};
	idTechRequest.outputDirectory = root.filePath(QStringLiteral("converted-idtech"));
	idTechRequest.outputFormat = QStringLiteral("png");
	const AssetImageConversionReport idTechReport = convertPackageImages(archive, idTechRequest);
	ok &= expect(idTechReport.succeeded(), "idTech image conversion should succeed");
	ok &= expect(idTechReport.writtenCount == 4, "idTech image conversion should write every entry");
	ok &= expect(QFileInfo::exists(root.filePath(QStringLiteral("converted-idtech/gfx/menu.png"))), "converted LMP should exist");
	ok &= expect(QFileInfo::exists(root.filePath(QStringLiteral("converted-idtech/textures/floor3_3.png"))), "converted WAL should exist");
	for (const AssetImageConversionEntryResult& entry : idTechReport.entries) {
		ok &= expect(!entry.paletteId.isEmpty(), "converted idTech entry should name a palette");
		ok &= expect(!entry.paletteFromPackage, "this package has no game palette, so the fallback must be reported");
	}

	// Auto-selection must pick up the idTech formats too.
	AssetImageConversionRequest autoRequest;
	autoRequest.outputDirectory = root.filePath(QStringLiteral("converted-auto"));
	autoRequest.dryRun = true;
	const AssetImageConversionReport autoReport = convertPackageImages(archive, autoRequest);
	ok &= expect(autoReport.succeeded(), "auto-selected image conversion should succeed");
	ok &= expect(autoReport.requestedCount == 5, "auto-selected image conversion should see every image entry");

	// Palette quantization onto an idTech palette.
	AssetImageConversionRequest indexedRequest;
	indexedRequest.virtualPaths = {QStringLiteral("textures/wall.png")};
	indexedRequest.outputDirectory = root.filePath(QStringLiteral("converted-indexed"));
	indexedRequest.outputFormat = QStringLiteral("png");
	indexedRequest.paletteMode = QStringLiteral("indexed");
	indexedRequest.dryRun = true;
	const AssetImageConversionReport indexedReport = convertPackageImages(archive, indexedRequest);
	ok &= expect(indexedReport.succeeded(), "indexed image conversion should succeed");
	ok &= expect(!indexedReport.entries.isEmpty() && !indexedReport.entries.front().paletteId.isEmpty(), "indexed conversion should name the palette used");

	AssetImageConversionRequest badPaletteRequest = indexedRequest;
	badPaletteRequest.paletteMode = QStringLiteral("not-a-palette");
	const AssetImageConversionReport badPaletteReport = convertPackageImages(archive, badPaletteRequest);
	ok &= expect(!badPaletteReport.succeeded(), "an unknown palette mode should be rejected");

	// Resolving a real palette out of the package.
	const QString quakePaletteId = defaultIdTechPaletteIdForFormat(IdTechImageFormat::QuakeLump);
	QString paletteCandidate;
	for (const QString& candidate : idTechPaletteCandidatePaths(quakePaletteId)) {
		if (candidate.endsWith(QStringLiteral(".lmp"), Qt::CaseInsensitive)) {
			paletteCandidate = candidate;
			break;
		}
	}
	if (!paletteCandidate.isEmpty()) {
		QDir paletteRoot(root.filePath(QStringLiteral("package")));
		const int slash = paletteCandidate.lastIndexOf('/');
		if (slash > 0) {
			ok &= expect(paletteRoot.mkpath(paletteCandidate.left(slash)), "palette directory should be created");
		}
		QByteArray paletteBytes;
		for (int index = 0; index < 256; ++index) {
			paletteBytes.append(static_cast<char>(index));
			paletteBytes.append(static_cast<char>((index * 5) % 256));
			paletteBytes.append(static_cast<char>((index * 9) % 256));
		}
		ok &= expect(writeFile(paletteRoot.filePath(paletteCandidate), paletteBytes), "palette fixture should be written");

		PackageArchive paletteArchive;
		QString paletteError;
		ok &= expect(paletteArchive.load(root.filePath(QStringLiteral("package")), &paletteError), "package with a palette should load");

		AssetImageConversionRequest resolvedRequest;
		resolvedRequest.virtualPaths = {QStringLiteral("gfx/menu.lmp")};
		resolvedRequest.outputDirectory = root.filePath(QStringLiteral("converted-palette"));
		resolvedRequest.outputFormat = QStringLiteral("png");
		resolvedRequest.paletteId = quakePaletteId;
		resolvedRequest.dryRun = true;
		const AssetImageConversionReport resolvedReport = convertPackageImages(paletteArchive, resolvedRequest);
		ok &= expect(resolvedReport.succeeded(), "palette-resolved conversion should succeed");
		ok &= expect(!resolvedReport.entries.isEmpty() && resolvedReport.entries.front().paletteFromPackage, "the package palette should be used and reported");
		ok &= expect(!resolvedReport.entries.isEmpty() && !resolvedReport.entries.front().paletteGenerated, "a package palette must not be reported as generated");
	}

	// ---- WAV peaks -----------------------------------------------------------

	const AssetAudioPeaks pcm8 = extractWavePeaks(stereoFullScaleWav(0x0001, 8, 8, false), 4);
	ok &= checkFullScalePeaks(pcm8, "8-bit WAV peaks mismatch");
	ok &= expect(pcm8.bitsPerSample == 8, "8-bit WAV bit depth mismatch");

	const AssetAudioPeaks pcm16 = extractWavePeaks(stereoFullScaleWav(0x0001, 16, 8, false), 4);
	ok &= checkFullScalePeaks(pcm16, "16-bit WAV peaks mismatch");

	const AssetAudioPeaks pcm24 = extractWavePeaks(stereoFullScaleWav(0x0001, 24, 8, false), 4);
	ok &= checkFullScalePeaks(pcm24, "24-bit WAV peaks mismatch");

	const AssetAudioPeaks pcm32 = extractWavePeaks(stereoFullScaleWav(0x0001, 32, 8, false), 4);
	ok &= checkFullScalePeaks(pcm32, "32-bit WAV peaks mismatch");

	const AssetAudioPeaks float32 = extractWavePeaks(stereoFullScaleWav(0x0003, 32, 8, false), 4);
	ok &= checkFullScalePeaks(float32, "32-bit float WAV peaks mismatch");

	const AssetAudioPeaks extensible16 = extractWavePeaks(stereoFullScaleWav(0x0001, 16, 8, true), 4);
	ok &= checkFullScalePeaks(extensible16, "WAVE_FORMAT_EXTENSIBLE peaks mismatch");

	const AssetAudioPeaks extensibleFloat = extractWavePeaks(stereoFullScaleWav(0x0003, 32, 8, true), 4);
	ok &= checkFullScalePeaks(extensibleFloat, "extensible float WAV peaks mismatch");

	ok &= expect(pcm16.durationMs == 8 * 1000 / 22050, "WAV peak duration mismatch");
	ok &= expect(!extractWavePeaks(QByteArray("not a wav")).valid, "a non-WAV payload should not produce peaks");

	const QStringList waveform = assetWaveformLines(pcm16, 4, 12);
	ok &= expect(waveform.size() == 2 + 8, "stereo waveform should render a header and four buckets per channel");

	// The envelope must cover the whole data chunk, not just the first 64 KiB.
	const int longFrames = 40000;
	QByteArray longWav = stereoFullScaleWav(0x0001, 16, longFrames, false);
	// Silence everything but the very last frame so a truncated walk would miss it.
	const qsizetype dataStart = longWav.size() - static_cast<qsizetype>(longFrames) * 4;
	for (qsizetype index = dataStart; index < longWav.size() - 4; ++index) {
		longWav[index] = '\0';
	}
	const AssetAudioPeaks longPeaks = extractWavePeaks(longWav, 8);
	ok &= expect(longPeaks.valid && longPeaks.frameCount == longFrames, "long WAV frame count mismatch");
	ok &= expect(peakValue(longPeaks, 0, longPeaks.bucketCount - 1, true) > 0.98f, "the envelope must reach the end of the data chunk");

	// ---- WAV export ----------------------------------------------------------

	const QString wavOutput = root.filePath(QStringLiteral("pickup-copy.wav"));
	const AssetAudioExportReport dryAudioReport = exportPackageAudioToWav(archive, QStringLiteral("sound/pickup.wav"), wavOutput, true, false);
	ok &= expect(dryAudioReport.succeeded() && !QFileInfo::exists(wavOutput), "dry-run WAV export should not write output");
	const AssetAudioExportReport writeAudioReport = exportPackageAudioToWav(archive, QStringLiteral("sound/pickup.wav"), wavOutput, false, false);
	ok &= expect(writeAudioReport.succeeded() && QFileInfo::exists(wavOutput), "WAV export should write output");
	ok &= expect(writeAudioReport.conversionMode == QStringLiteral("copy"), "canonical 16-bit PCM should be copied, not re-encoded");
	ok &= expect(!writeAudioReport.converted, "canonical 16-bit PCM should not be flagged as converted");
	ok &= expect(readFile(wavOutput) == wavFixture(), "WAV export bytes should match source");

	ok &= expect(writeFile(root.filePath(QStringLiteral("package/sound/float.wav")), stereoFullScaleWav(0x0003, 32, 8, false)), "float WAV fixture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("package/sound/music.ogg")), oggVorbisFixture(2, 44100, 44100)), "OGG fixture should be written");
	PackageArchive audioArchive;
	QString audioError;
	ok &= expect(audioArchive.load(root.filePath(QStringLiteral("package")), &audioError), "package with extra audio should load");

	const QString floatOutput = root.filePath(QStringLiteral("float-export.wav"));
	const AssetAudioExportReport floatReport = exportPackageAudioToWav(audioArchive, QStringLiteral("sound/float.wav"), floatOutput, false, false);
	ok &= expect(floatReport.succeeded() && floatReport.converted, "float WAV should be converted on export");
	ok &= expect(floatReport.conversionMode == QStringLiteral("pcm16"), "float WAV export conversion mode mismatch");
	const AssetAudioPeaks exported = extractWavePeaks(readFile(floatOutput), 4);
	ok &= expect(exported.valid && exported.bitsPerSample == 16 && exported.channels == 2, "exported WAV should be 16-bit stereo PCM");
	ok &= expect(exported.frameCount == 8, "exported WAV frame count mismatch");

	const AssetAudioExportReport oggReport = exportPackageAudioToWav(audioArchive, QStringLiteral("sound/music.ogg"), root.filePath(QStringLiteral("music.wav")), false, false);
	ok &= expect(!oggReport.succeeded(), "compressed audio should not be exported as WAV");
	ok &= expect(oggReport.error.contains(QStringLiteral("decoder"), Qt::CaseInsensitive), "compressed export should explain the missing decoder backend");

	// ---- compressed audio headers -------------------------------------------

	const AssetAnalysis oggAnalysis = analyzeAssetBytes(QStringLiteral("sound/music.ogg"), oggVorbisFixture(2, 44100, 44100), -1);
	ok &= expect(oggAnalysis.kind == AssetPreviewKind::Audio, "OGG should analyse as audio");
	ok &= expect(oggAnalysis.audioFormat == QStringLiteral("OGG"), "OGG format mismatch");
	ok &= expect(oggAnalysis.audioCodec == QStringLiteral("Vorbis"), "OGG codec mismatch");
	ok &= expect(oggAnalysis.audioChannels == 2, "OGG channel count mismatch");
	ok &= expect(oggAnalysis.audioSampleRate == 44100, "OGG sample rate mismatch");
	ok &= expect(oggAnalysis.audioDurationMs == 1000, "OGG duration mismatch");
	ok &= expect(oggAnalysis.audioQtPlaybackCandidate, "a parsed OGG should be a playback candidate");
	ok &= expect(linesContain(oggAnalysis.detailLines, QStringLiteral("end-of-stream page")),
		"a complete OGG should say the duration came from the end-of-stream page");

	// A preview only ever samples the head of a music track, so the granule
	// position at the last page it can see is not the track length.
	QByteArray partialOgg = oggVorbisFixture(2, 44100, 44100);
	partialOgg.chop(kOggEosPageBytes);
	partialOgg.append(oggMidStreamPage(220500, 1));
	const AssetAnalysis truncatedOgg = analyzeAssetBytes(QStringLiteral("music/theme.ogg"), partialOgg, 4 * 1024 * 1024);
	ok &= expect(truncatedOgg.audioSampleRate == 44100, "a truncated OGG sample should still report its identification header");
	ok &= expect(truncatedOgg.audioDurationMs == 0, "a truncated OGG sample must not report a duration");
	ok &= expect(truncatedOgg.audioFrameCount == 0, "a truncated OGG sample must not report a frame count");
	ok &= expect(linesContain(truncatedOgg.detailLines, QStringLiteral("lower bound")),
		"a truncated OGG should present the last granule position as a lower bound");
	ok &= expect(linesContain(truncatedOgg.detailLines, QStringLiteral("Sampled:")),
		"the audio detail lines should state how much of the entry was read");

	// The same bytes read in full: still no end-of-stream page, so still no duration.
	const AssetAnalysis unterminatedOgg = analyzeAssetBytes(QStringLiteral("sound/stream.ogg"), partialOgg, partialOgg.size());
	ok &= expect(unterminatedOgg.audioDurationMs == 0, "an OGG without an end-of-stream page must not report a duration");
	ok &= expect(linesContain(unterminatedOgg.detailLines, QStringLiteral("lower bound")),
		"an unterminated OGG should present the last granule position as a lower bound");
	ok &= expect(!linesContain(unterminatedOgg.detailLines, QStringLiteral("Sampled:")),
		"a fully read entry must not claim it was only sampled");

	const QByteArray mp3Bytes = mp3Fixture(3);
	const AssetAnalysis mp3Analysis = analyzeAssetBytes(QStringLiteral("sound/track.mp3"), mp3Bytes, mp3Bytes.size());
	ok &= expect(mp3Analysis.kind == AssetPreviewKind::Audio, "MP3 should analyse as audio");
	ok &= expect(mp3Analysis.audioFormat == QStringLiteral("MP3"), "MP3 format mismatch");
	ok &= expect(mp3Analysis.audioCodec.contains(QStringLiteral("MPEG-1")), "MP3 version mismatch");
	ok &= expect(mp3Analysis.audioCodec.contains(QStringLiteral("Layer 3")), "MP3 layer mismatch");
	ok &= expect(mp3Analysis.audioSampleRate == 44100, "MP3 sample rate mismatch");
	ok &= expect(mp3Analysis.audioChannels == 2, "MP3 channel count mismatch");
	ok &= expect(mp3Analysis.audioBitrateBitsPerSecond == 128000, "MP3 bitrate mismatch");
	ok &= expect(mp3Analysis.audioDurationMs == (3 * 417) * 8 / 128, "MP3 duration estimate mismatch");
	ok &= expect(linesContain(mp3Analysis.detailLines, QStringLiteral("ID3v2")), "MP3 report should mention the skipped ID3v2 tag");

	const AssetAnalysis flacAnalysis = analyzeAssetBytes(QStringLiteral("sound/ambient.flac"), flacFixture(44100, 2, 16, 88200), -1);
	ok &= expect(flacAnalysis.kind == AssetPreviewKind::Audio, "FLAC should analyse as audio");
	ok &= expect(flacAnalysis.audioFormat == QStringLiteral("FLAC"), "FLAC format mismatch");
	ok &= expect(flacAnalysis.audioChannels == 2, "FLAC channel count mismatch");
	ok &= expect(flacAnalysis.audioSampleRate == 44100, "FLAC sample rate mismatch");
	ok &= expect(flacAnalysis.audioBitsPerSample == 16, "FLAC bit depth mismatch");
	ok &= expect(flacAnalysis.audioDurationMs == 2000, "FLAC duration mismatch");

	const AssetAnalysis brokenOgg = analyzeAssetBytes(QStringLiteral("sound/broken.ogg"), QByteArray(64, '\0'), -1);
	ok &= expect(brokenOgg.kind == AssetPreviewKind::Audio, "an unparsable OGG should still classify as audio");
	ok &= expect(!brokenOgg.audioQtPlaybackCandidate, "an unparsable stream must not claim to be playable");

	// ---- text and script analysis -------------------------------------------

	const QByteArray shaderBytes =
		"textures/base_wall/c_met5_2\n"
		"{\n"
		"\tqer_editorimage textures/base_wall/c_met5_2.tga\n"
		"\tsurfaceparm metalsteps\n"
		"\tq3map_lightimage textures/colors/white.tga\n"
		"\tcull none\n"
		"\tsort additive\n"
		"\tdeformVertexes wave 100 sin 0 1 0 1\n"
		"\t{\n"
		"\t\tmap textures/base_wall/c_met5_2.tga\n"
		"\t\tblendFunc GL_ONE GL_ZERO\n"
		"\t\trgbGen identity\n"
		"\t\talphaGen const 1.0\n"
		"\t\ttcGen environment\n"
		"\t\ttcMod scroll 0.1 0\n"
		"\t}\n"
		"}\n";
	const AssetAnalysis shaderAnalysis = analyzeAssetBytes(QStringLiteral("scripts/base_wall.shader"), shaderBytes, -1);
	ok &= expect(shaderAnalysis.kind == AssetPreviewKind::Text, "shader should analyse as text");
	ok &= expect(shaderAnalysis.textLanguageId == QStringLiteral("shader"), "shader language id mismatch");
	ok &= expect(linesContain(shaderAnalysis.textHighlightLines, QStringLiteral("q3map_lightimage")), "shader highlights should report q3map directives");
	ok &= expect(linesContain(shaderAnalysis.textHighlightLines, QStringLiteral("surfaceparm")), "shader highlights should report surfaceparm");
	ok &= expect(linesContain(shaderAnalysis.textHighlightLines, QStringLiteral("deformVertexes")), "shader highlights should report deformVertexes");
	ok &= expect(linesContain(shaderAnalysis.textHighlightLines, QStringLiteral("tcMod")), "shader highlights should report tcMod");
	ok &= expect(linesContain(shaderAnalysis.textHighlightLines, QStringLiteral("blendFunc")), "shader highlights should report blendFunc");
	ok &= expect(linesContain(shaderAnalysis.textHighlightLines, QStringLiteral("alphaGen")), "shader highlights should report alphaGen");
	ok &= expect(shaderAnalysis.textDiagnosticLines.size() == 1 && shaderAnalysis.textDiagnosticLines.front().contains(QStringLiteral("No local text diagnostics")), "a well-formed shader should be clean");

	const QByteArray brokenShaderBytes =
		"textures/broken/one\n"
		"{\n"
		"\tsurfaceparm nonsolid\n"
		"\tbogusdirective 3\n"
		"\t{\n"
		"\t\tmap $lightmap\n"
		"\t}\n";
	const AssetAnalysis brokenShader = analyzeAssetBytes(QStringLiteral("scripts/broken.shader"), brokenShaderBytes, -1);
	ok &= expect(linesContain(brokenShader.textDiagnosticLines, QStringLiteral("unknown shader directive")), "an unknown shader directive should be reported");
	ok &= expect(linesContain(brokenShader.textDiagnosticLines, QStringLiteral("never closed")), "an unbalanced brace should name the line it opened on");
	ok &= expect(linesContain(brokenShader.textDiagnosticLines, QStringLiteral("line 2")), "the unclosed block should be attributed to line 2");

	const QByteArray cfgBytes =
		"// autoexec\n"
		"seta cg_fov \"90\"\n"
		"bind mouse2 \"+zoom\"\n"
		"set g_gametype \"3\n"
		"vstr nextmap\n";
	const AssetAnalysis cfgAnalysis = analyzeAssetBytes(QStringLiteral("baseq3/autoexec.cfg"), cfgBytes, -1);
	ok &= expect(cfgAnalysis.textLanguageId == QStringLiteral("cfg"), "CFG language id mismatch");
	ok &= expect(linesContain(cfgAnalysis.textHighlightLines, QStringLiteral("seta")), "CFG highlights should report seta");
	ok &= expect(linesContain(cfgAnalysis.textHighlightLines, QStringLiteral("vstr")), "CFG highlights should report vstr");
	ok &= expect(linesContain(cfgAnalysis.textHighlightLines, QStringLiteral("comment")), "CFG highlights should report the comment line");
	ok &= expect(linesContain(cfgAnalysis.textDiagnosticLines, QStringLiteral("unterminated quoted string")), "an unterminated string should be reported");

	const QByteArray quakeCBytes =
		"void() player_run =\n"
		"{\n"
		"\tlocal float speed;\n"
		"\tspeed = vlen(self.velocity);\n"
		"\tif (speed < 1)\n"
		"\t\treturn;\n"
		"\tself.nextthink = time + 0.1;\n"
		"};\n";
	const AssetAnalysis quakeCAnalysis = analyzeAssetBytes(QStringLiteral("progs/player.qc"), quakeCBytes, -1);
	ok &= expect(quakeCAnalysis.textLanguageId == QStringLiteral("quakec"), "QuakeC language id mismatch");
	ok &= expect(linesContain(quakeCAnalysis.textHighlightLines, QStringLiteral("void")), "QuakeC highlights should report void");
	ok &= expect(linesContain(quakeCAnalysis.textHighlightLines, QStringLiteral("local")), "QuakeC highlights should report local");
	ok &= expect(linesContain(quakeCAnalysis.textHighlightLines, QStringLiteral("return")), "QuakeC highlights should report return");

	// ---- project text search -------------------------------------------------

	AssetTextSearchRequest searchRequest;
	searchRequest.rootPath = root.filePath(QStringLiteral("project"));
	searchRequest.findText = QStringLiteral("developer");
	const AssetTextSearchReport searchReport = findReplaceProjectText(searchRequest);
	ok &= expect(searchReport.succeeded(), "project text search should succeed");
	ok &= expect(searchReport.matchCount == 1, "project text search match count mismatch");
	ok &= expect(searchReport.saveState == QStringLiteral("clean"), "project text search save state mismatch");

	AssetTextSearchRequest replaceDryRequest = searchRequest;
	replaceDryRequest.replace = true;
	replaceDryRequest.replaceText = QStringLiteral("sv_cheats");
	replaceDryRequest.dryRun = true;
	const AssetTextSearchReport replaceDryReport = findReplaceProjectText(replaceDryRequest);
	ok &= expect(replaceDryReport.succeeded(), "dry-run project text replace should succeed");
	ok &= expect(replaceDryReport.replacementCount == 1, "dry-run project text replace count mismatch");
	ok &= expect(replaceDryReport.saveState == QStringLiteral("modified"), "dry-run project text replace save state mismatch");
	ok &= expect(readFile(root.filePath(QStringLiteral("project/scripts/autoexec.cfg"))).contains("developer"), "dry-run project text replace should not write");

	AssetTextSearchRequest replaceWriteRequest = replaceDryRequest;
	replaceWriteRequest.dryRun = false;
	const AssetTextSearchReport replaceWriteReport = findReplaceProjectText(replaceWriteRequest);
	ok &= expect(replaceWriteReport.succeeded(), "write project text replace should succeed");
	ok &= expect(replaceWriteReport.saveState == QStringLiteral("saved"), "write project text replace save state mismatch");
	ok &= expect(readFile(root.filePath(QStringLiteral("project/scripts/autoexec.cfg"))).contains("sv_cheats"), "write project text replace should update file");

	return ok ? 0 : 1;
}
