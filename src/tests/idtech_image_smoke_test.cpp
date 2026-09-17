#include "core/idtech_image.h"
#include "core/package_archive.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPair>
#include <QSet>
#include <QTemporaryDir>
#include <QVector>

#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace vibestudio;

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
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

void appendLeFloat(QByteArray* data, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	appendLe32(data, raw);
}

void writeLe16At(QByteArray* data, qsizetype offset, quint16 value)
{
	(*data)[offset] = static_cast<char>(value & 0xff);
	(*data)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void writeLe32At(QByteArray* data, qsizetype offset, quint32 value)
{
	(*data)[offset] = static_cast<char>(value & 0xff);
	(*data)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
	(*data)[offset + 2] = static_cast<char>((value >> 16) & 0xff);
	(*data)[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

QByteArray fixedName(const QByteArray& name, int size)
{
	QByteArray bytes = name.left(size);
	while (bytes.size() < size) {
		bytes.append('\0');
	}
	return bytes;
}

// ---------------------------------------------------------------------------
// Fixture builders. Every payload is assembled here from the published layout
// so the tests never depend on commercial game data.
// ---------------------------------------------------------------------------

QByteArray buildDoomPatch()
{
	// width 4, height 4. Column 0 carries two posts with a transparent gap at
	// rows 1 and 2; the remaining columns are solid.
	const int width = 4;
	const int height = 4;
	QVector<QByteArray> columns;
	{
		QByteArray column;
		column.append(static_cast<char>(0));   // topdelta
		column.append(static_cast<char>(1));   // length
		column.append(static_cast<char>(0));   // pad
		column.append(static_cast<char>(10));  // pixel
		column.append(static_cast<char>(0));   // pad
		column.append(static_cast<char>(3));   // topdelta
		column.append(static_cast<char>(1));
		column.append(static_cast<char>(0));
		column.append(static_cast<char>(20));
		column.append(static_cast<char>(0));
		column.append(static_cast<char>(0xff));
		columns.push_back(column);
	}
	for (int index = 1; index < width; ++index) {
		QByteArray column;
		column.append(static_cast<char>(0));
		column.append(static_cast<char>(height));
		column.append(static_cast<char>(0));
		for (int row = 0; row < height; ++row) {
			column.append(static_cast<char>(index * 16 + row));
		}
		column.append(static_cast<char>(0));
		column.append(static_cast<char>(0xff));
		columns.push_back(column);
	}

	QByteArray bytes;
	appendLe16(&bytes, static_cast<quint16>(width));
	appendLe16(&bytes, static_cast<quint16>(height));
	appendLe16(&bytes, static_cast<quint16>(2));  // left offset
	appendLe16(&bytes, static_cast<quint16>(3));  // top offset
	const qsizetype offsetTable = bytes.size();
	for (int index = 0; index < width; ++index) {
		appendLe32(&bytes, 0);
	}
	for (int index = 0; index < width; ++index) {
		writeLe32At(&bytes, offsetTable + index * 4, static_cast<quint32>(bytes.size()));
		bytes.append(columns.at(index));
	}
	return bytes;
}

QByteArray buildDoomFlat()
{
	QByteArray bytes;
	bytes.resize(4096);
	for (int y = 0; y < 64; ++y) {
		for (int x = 0; x < 64; ++x) {
			bytes[y * 64 + x] = static_cast<char>((x * 7 + y * 13) & 0xff);
		}
	}
	return bytes;
}

QByteArray buildQuakeLump(int width, int height)
{
	QByteArray bytes;
	appendLe32(&bytes, static_cast<quint32>(width));
	appendLe32(&bytes, static_cast<quint32>(height));
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			bytes.append(static_cast<char>((x + y * width) & 0xff));
		}
	}
	return bytes;
}

QByteArray buildMipPixels(int width, int height, int seed)
{
	QByteArray bytes;
	bytes.reserve(width * height);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			bytes.append(static_cast<char>((x + y * 3 + seed) & 0xff));
		}
	}
	return bytes;
}

QByteArray buildMipTexture(const QByteArray& name, int width, int height, bool wad3Palette, int transparentX, int transparentY)
{
	QByteArray mip0 = buildMipPixels(width, height, 0);
	if (transparentX >= 0 && transparentY >= 0) {
		mip0[transparentY * width + transparentX] = static_cast<char>(0xff);
	}
	const QByteArray mip1 = buildMipPixels(width / 2, height / 2, 1);
	const QByteArray mip2 = buildMipPixels(width / 4, height / 4, 2);
	const QByteArray mip3 = buildMipPixels(width / 8, height / 8, 3);

	QByteArray bytes;
	bytes.append(fixedName(name, 16));
	appendLe32(&bytes, static_cast<quint32>(width));
	appendLe32(&bytes, static_cast<quint32>(height));
	const quint32 offset0 = 40;
	const quint32 offset1 = offset0 + static_cast<quint32>(mip0.size());
	const quint32 offset2 = offset1 + static_cast<quint32>(mip1.size());
	const quint32 offset3 = offset2 + static_cast<quint32>(mip2.size());
	appendLe32(&bytes, offset0);
	appendLe32(&bytes, offset1);
	appendLe32(&bytes, offset2);
	appendLe32(&bytes, offset3);
	bytes.append(mip0);
	bytes.append(mip1);
	bytes.append(mip2);
	bytes.append(mip3);
	if (wad3Palette) {
		appendLe16(&bytes, 256);
		for (int index = 0; index < 256; ++index) {
			bytes.append(static_cast<char>(index));
			bytes.append(static_cast<char>(255 - index));
			bytes.append(static_cast<char>((index * 3) & 0xff));
		}
	}
	return bytes;
}

QByteArray buildQuake2Wal(const QByteArray& name, const QByteArray& animName, int width, int height)
{
	const QByteArray mip0 = buildMipPixels(width, height, 5);
	const QByteArray mip1 = buildMipPixels(width / 2, height / 2, 6);
	const QByteArray mip2 = buildMipPixels(width / 4, height / 4, 7);
	const QByteArray mip3 = buildMipPixels(width / 8, height / 8, 8);

	QByteArray bytes;
	bytes.append(fixedName(name, 32));
	appendLe32(&bytes, static_cast<quint32>(width));
	appendLe32(&bytes, static_cast<quint32>(height));
	const quint32 offset0 = 100;
	const quint32 offset1 = offset0 + static_cast<quint32>(mip0.size());
	const quint32 offset2 = offset1 + static_cast<quint32>(mip1.size());
	const quint32 offset3 = offset2 + static_cast<quint32>(mip2.size());
	appendLe32(&bytes, offset0);
	appendLe32(&bytes, offset1);
	appendLe32(&bytes, offset2);
	appendLe32(&bytes, offset3);
	bytes.append(fixedName(animName, 32));
	appendLe32(&bytes, 0x00000079u);  // surface flags
	appendLe32(&bytes, 0x00000001u);  // content flags
	appendLe32(&bytes, 42u);          // surface value
	bytes.append(mip0);
	bytes.append(mip1);
	bytes.append(mip2);
	bytes.append(mip3);
	return bytes;
}

void appendPcxRow(QByteArray* data, const QByteArray& row)
{
	qsizetype index = 0;
	while (index < row.size()) {
		const char value = row.at(index);
		qsizetype run = 1;
		while (index + run < row.size() && row.at(index + run) == value && run < 63) {
			++run;
		}
		const quint8 raw = static_cast<quint8>(value);
		if (run > 1 || (raw & 0xc0) == 0xc0) {
			data->append(static_cast<char>(0xc0 | static_cast<quint8>(run)));
			data->append(value);
		} else {
			data->append(value);
		}
		index += run;
	}
}

QByteArray buildPcx8Bit(int width, int height)
{
	QByteArray header;
	header.resize(128);
	header.fill('\0');
	header[0] = static_cast<char>(0x0a);
	header[1] = static_cast<char>(5);
	header[2] = static_cast<char>(1);
	header[3] = static_cast<char>(8);
	QByteArray fields;
	appendLe16(&fields, 0);
	appendLe16(&fields, 0);
	appendLe16(&fields, static_cast<quint16>(width - 1));
	appendLe16(&fields, static_cast<quint16>(height - 1));
	appendLe16(&fields, 72);
	appendLe16(&fields, 72);
	for (int index = 0; index < fields.size(); ++index) {
		header[4 + index] = fields.at(index);
	}
	header[65] = static_cast<char>(1);  // planes
	header[66] = static_cast<char>(width & 0xff);
	header[67] = static_cast<char>((width >> 8) & 0xff);
	header[68] = static_cast<char>(1);  // palette info

	QByteArray bytes = header;
	for (int y = 0; y < height; ++y) {
		QByteArray row;
		for (int x = 0; x < width; ++x) {
			row.append(static_cast<char>(((y + 1) * 10 + (x / 4)) & 0xff));
		}
		appendPcxRow(&bytes, row);
	}
	bytes.append(static_cast<char>(0x0c));
	for (int index = 0; index < 256; ++index) {
		bytes.append(static_cast<char>(index));
		bytes.append(static_cast<char>((index * 2) & 0xff));
		bytes.append(static_cast<char>((index * 3) & 0xff));
	}
	return bytes;
}

QByteArray targaHeaderBytes(int idLength, int colorMapType, int imageType, int colorMapLength,
	int colorMapEntrySize, int width, int height, int depth, int descriptor)
{
	QByteArray bytes;
	bytes.append(static_cast<char>(idLength));
	bytes.append(static_cast<char>(colorMapType));
	bytes.append(static_cast<char>(imageType));
	appendLe16(&bytes, 0);
	appendLe16(&bytes, static_cast<quint16>(colorMapLength));
	bytes.append(static_cast<char>(colorMapEntrySize));
	appendLe16(&bytes, 0);
	appendLe16(&bytes, 0);
	appendLe16(&bytes, static_cast<quint16>(width));
	appendLe16(&bytes, static_cast<quint16>(height));
	bytes.append(static_cast<char>(depth));
	bytes.append(static_cast<char>(descriptor));
	return bytes;
}

QByteArray buildUncompressedTarga24()
{
	// 3x2, bottom-left origin, so the bottom row is stored first.
	QByteArray bytes = targaHeaderBytes(0, 0, 2, 0, 0, 3, 2, 24, 0x00);
	const int bottom[3][3] = {{10, 20, 30}, {40, 50, 60}, {70, 80, 90}};
	const int top[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
	for (const auto& pixel : bottom) {
		bytes.append(static_cast<char>(pixel[2]));
		bytes.append(static_cast<char>(pixel[1]));
		bytes.append(static_cast<char>(pixel[0]));
	}
	for (const auto& pixel : top) {
		bytes.append(static_cast<char>(pixel[2]));
		bytes.append(static_cast<char>(pixel[1]));
		bytes.append(static_cast<char>(pixel[0]));
	}
	return bytes;
}

QByteArray buildRleTarga24()
{
	// 4x2, top-left origin. Row 0 is a single run packet, row 1 a raw packet.
	QByteArray bytes = targaHeaderBytes(0, 0, 10, 0, 0, 4, 2, 24, 0x20);
	bytes.append(static_cast<char>(0x83));
	bytes.append(static_cast<char>(56));
	bytes.append(static_cast<char>(34));
	bytes.append(static_cast<char>(12));
	bytes.append(static_cast<char>(0x03));
	const int row[4][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12}};
	for (const auto& pixel : row) {
		bytes.append(static_cast<char>(pixel[2]));
		bytes.append(static_cast<char>(pixel[1]));
		bytes.append(static_cast<char>(pixel[0]));
	}
	return bytes;
}

QByteArray buildColorMappedTarga()
{
	// 2x1, top-left origin, four 24-bit colour map entries.
	QByteArray bytes = targaHeaderBytes(0, 1, 1, 4, 24, 2, 1, 8, 0x20);
	const int map[4][3] = {{200, 100, 50}, {1, 2, 3}, {9, 99, 199}, {255, 255, 255}};
	for (const auto& entry : map) {
		bytes.append(static_cast<char>(entry[2]));
		bytes.append(static_cast<char>(entry[1]));
		bytes.append(static_cast<char>(entry[0]));
	}
	bytes.append(static_cast<char>(2));
	bytes.append(static_cast<char>(0));
	return bytes;
}

QByteArray buildSpriteFrame(int originX, int originY, int width, int height, int seed)
{
	QByteArray bytes;
	appendLe32(&bytes, static_cast<quint32>(originX));
	appendLe32(&bytes, static_cast<quint32>(originY));
	appendLe32(&bytes, static_cast<quint32>(width));
	appendLe32(&bytes, static_cast<quint32>(height));
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const int index = (x + y * width + seed);
			bytes.append(static_cast<char>((x == 1 && y == 1) ? 0xff : (index & 0x7f)));
		}
	}
	return bytes;
}

QByteArray buildQuakeSprite()
{
	QByteArray bytes;
	bytes.append("IDSP", 4);
	appendLe32(&bytes, 1);   // version
	appendLe32(&bytes, 2);   // type: VP_PARALLEL
	appendLeFloat(&bytes, 3.5f);
	appendLe32(&bytes, 4);   // maxwidth
	appendLe32(&bytes, 4);   // maxheight
	appendLe32(&bytes, 2);   // numframes
	appendLeFloat(&bytes, 0.0f);
	appendLe32(&bytes, 0);   // synctype

	appendLe32(&bytes, 0);   // single frame
	bytes.append(buildSpriteFrame(-2, 2, 2, 2, 1));

	appendLe32(&bytes, 1);   // frame group
	appendLe32(&bytes, 2);   // sub frame count
	appendLeFloat(&bytes, 0.1f);
	appendLeFloat(&bytes, 0.3f);
	bytes.append(buildSpriteFrame(-1, 1, 2, 2, 5));
	bytes.append(buildSpriteFrame(-1, 1, 2, 2, 9));
	return bytes;
}

QByteArray buildRgbPalette(int banks)
{
	QByteArray bytes;
	for (int bank = 0; bank < banks; ++bank) {
		for (int index = 0; index < 256; ++index) {
			bytes.append(static_cast<char>((index + bank * 4) & 0xff));
			bytes.append(static_cast<char>((255 - index) & 0xff));
			bytes.append(static_cast<char>((index * 5 + bank) & 0xff));
		}
	}
	return bytes;
}

QByteArray buildPakArchive(const QVector<QPair<QByteArray, QByteArray>>& files)
{
	QByteArray body;
	QVector<QPair<quint32, quint32>> locations;
	body.append("PACK", 4);
	appendLe32(&body, 0);
	appendLe32(&body, 0);
	for (const auto& file : files) {
		locations.push_back({static_cast<quint32>(body.size()), static_cast<quint32>(file.second.size())});
		body.append(file.second);
	}
	const quint32 directoryOffset = static_cast<quint32>(body.size());
	for (int index = 0; index < files.size(); ++index) {
		body.append(fixedName(files.at(index).first, 56));
		appendLe32(&body, locations.at(index).first);
		appendLe32(&body, locations.at(index).second);
	}
	writeLe32At(&body, 4, directoryOffset);
	writeLe32At(&body, 8, static_cast<quint32>(files.size() * 64));
	return body;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFileInfo(path).absoluteDir().mkpath(QStringLiteral("."));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

// ---------------------------------------------------------------------------
// Test sections.
// ---------------------------------------------------------------------------

bool runPaletteSmoke()
{
	bool ok = true;

	const IdTechPalette quake = generatedIdTechPalette(QStringLiteral("quake"));
	ok &= expect(quake.isValid(), "Generated Quake palette should have 256 entries.");
	ok &= expect(quake.generated, "Generated palette should be marked generated.");
	ok &= expect(quake.transparentIndex == 255, "Generated Quake palette should declare index 255 transparent.");
	ok &= expect(quake.fullbrightStartIndex == 224, "Generated Quake palette should declare a fullbright range.");
	ok &= expect(qAlpha(quake.colorAt(255)) == 0, "Transparent palette entry should carry zero alpha.");
	ok &= expect(qAlpha(quake.colorAt(0)) == 255, "Opaque palette entries should carry full alpha.");

	QSet<QRgb> unique;
	for (int index = 0; index < 256; ++index) {
		unique.insert(quake.colorAt(index) | 0xff000000u);
	}
	ok &= expect(unique.size() == 256, "Generated palette entries must all be distinct.");

	const IdTechPalette again = generatedIdTechPalette(QStringLiteral("quake"));
	ok &= expect(again.colors == quake.colors, "Generated palettes must be deterministic.");

	const IdTechPalette doom = generatedIdTechPalette(QStringLiteral("doom"));
	ok &= expect(doom.isValid() && doom.transparentIndex == -1, "Generated Doom palette should have no transparent index.");
	ok &= expect(doom.colors != quake.colors, "Palette families should differ from each other.");

	ok &= expect(idTechPaletteIds().size() == 6, "Six palette ids should be published.");
	IdTechPaletteDescriptor descriptor;
	ok &= expect(idTechPaletteDescriptorForId(QStringLiteral("quake2"), &descriptor), "Quake II descriptor should resolve.");
	ok &= expect(descriptor.engineFamily == QStringLiteral("idtech2"), "Quake II descriptor should be idTech2.");
	ok &= expect(!idTechPaletteDescriptorForId(QStringLiteral("not-a-palette")), "Unknown palette ids should not resolve.");

	const QByteArray playpal = buildRgbPalette(14);
	IdTechPalette parsed;
	QString error;
	ok &= expect(parseIdTechPaletteBytes(playpal, QStringLiteral("doom"), &parsed, &error), "PLAYPAL-shaped payload should parse.");
	ok &= expect(parsed.isValid() && !parsed.generated, "Parsed palette should be valid and not generated.");
	ok &= expect(parsed.colorAt(7) == qRgb(7, 248, 35), "Parsed palette entry should match the first bank.");

	ok &= expect(!parseIdTechPaletteBytes(playpal.left(700), QStringLiteral("doom"), &parsed, &error), "Short palettes must fail.");
	ok &= expect(!error.isEmpty(), "Palette failure should report an error.");
	ok &= expect(!parseIdTechPaletteBytes(playpal.left(800), QStringLiteral("doom"), &parsed, &error), "Non-multiple palette sizes must fail.");

	const QByteArray pcx = buildPcx8Bit(8, 4);
	IdTechPalette pcxPalette;
	ok &= expect(parsePcxPalette(pcx, QStringLiteral("quake2"), &pcxPalette, &error), "PCX tail palette should parse.");
	ok &= expect(pcxPalette.colorAt(3) == qRgb(3, 6, 9), "PCX tail palette entry mismatch.");
	ok &= expect(!parsePcxPalette(playpal, QStringLiteral("quake2"), &pcxPalette, &error), "A raw palette is not a PCX.");

	ok &= expect(idTechPaletteCandidatePaths(QStringLiteral("quake")).contains(QStringLiteral("gfx/palette.lmp")),
		"Quake palette candidates should include gfx/palette.lmp.");
	ok &= expect(idTechPaletteCandidatePaths(QStringLiteral("doom")).contains(QStringLiteral("PLAYPAL")),
		"Doom palette candidates should include PLAYPAL.");
	ok &= expect(idTechPaletteCandidatePaths(QStringLiteral("quake2")).contains(QStringLiteral("pics/colormap.pcx")),
		"Quake II palette candidates should include pics/colormap.pcx.");

	return ok;
}

bool runQuantizeSmoke()
{
	bool ok = true;
	for (const QString& id : {QStringLiteral("doom"), QStringLiteral("quake")}) {
		const IdTechPalette palette = generatedIdTechPalette(id);
		const QImage swatch = renderIdTechPaletteSwatch(palette, 4);
		if (!expect(!swatch.isNull() && swatch.width() == 64 && swatch.height() == 64, "Palette swatch should be 16 cells square.")) {
			ok = false;
			continue;
		}
		const QImage quantized = quantizeToIdTechPalette(swatch, palette, false);
		if (!expect(quantized.format() == QImage::Format_Indexed8, "Quantized output should be indexed.")) {
			ok = false;
			continue;
		}
		bool allMatched = true;
		for (int index = 0; index < 256; ++index) {
			const int x = (index % 16) * 4 + 2;
			const int y = (index / 16) * 4 + 2;
			if (quantized.pixelIndex(x, y) != index) {
				allMatched = false;
				break;
			}
		}
		ok &= expect(allMatched, "Quantizing a palette swatch should recover every original index.");

		const QImage dithered = quantizeToIdTechPalette(swatch, palette, true);
		ok &= expect(dithered.size() == swatch.size(), "Dithered quantization should keep the image size.");
	}

	const IdTechPalette quake = generatedIdTechPalette(QStringLiteral("quake"));
	QImage source(2, 1, QImage::Format_ARGB32);
	source.setPixel(0, 0, qRgba(0, 0, 0, 0));
	source.setPixel(1, 0, quake.colorAt(12) | 0xff000000u);
	const QImage mapped = quantizeToIdTechPalette(source, quake, false);
	ok &= expect(mapped.pixelIndex(0, 0) == 255, "Fully transparent pixels should map to the transparent index.");
	ok &= expect(mapped.pixelIndex(1, 0) == 12, "Opaque pixels should map to their exact palette index.");

	ok &= expect(quantizeToIdTechPalette(QImage(), quake, false).isNull(), "Quantizing a null image should fail cleanly.");
	ok &= expect(renderIdTechPaletteSwatch(IdTechPalette()).isNull(), "Rendering an invalid palette should fail cleanly.");
	return ok;
}

bool runDetectionSmoke()
{
	bool ok = true;
	ok &= expect(detectIdTechImageFormat(QStringLiteral("patches/TESTPAT"), buildDoomPatch()) == IdTechImageFormat::DoomPatch,
		"Doom patch detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("flats/TESTFLT"), buildDoomFlat()) == IdTechImageFormat::DoomFlat,
		"Doom flat detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("gfx/test.lmp"), buildQuakeLump(6, 5)) == IdTechImageFormat::QuakeLump,
		"Quake lump detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("textures/TEST_TEX"), buildMipTexture("test_tex", 16, 16, false, -1, -1)) == IdTechImageFormat::QuakeMipTexture,
		"WAD2 miptex detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("textures/{fence"), buildMipTexture("{fence", 16, 16, true, 2, 3)) == IdTechImageFormat::QuakeMipTexture,
		"WAD3 miptex detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("textures/e1u1/test.wal"), buildQuake2Wal("e1u1/test", "e1u1/test2", 16, 16)) == IdTechImageFormat::Quake2Wal,
		"Quake II .wal detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("pics/test.pcx"), buildPcx8Bit(8, 4)) == IdTechImageFormat::Pcx,
		"PCX detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("textures/plain.tga"), buildUncompressedTarga24()) == IdTechImageFormat::Targa,
		"Uncompressed Targa detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("textures/rle.tga"), buildRleTarga24()) == IdTechImageFormat::Targa,
		"RLE Targa detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("progs/test.spr"), buildQuakeSprite()) == IdTechImageFormat::QuakeSprite,
		"Quake sprite detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("PLAYPAL"), buildRgbPalette(14)) == IdTechImageFormat::DoomPalette,
		"Doom palette detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("gfx/palette.lmp"), buildRgbPalette(1)) == IdTechImageFormat::DoomPalette,
		"Quake palette.lmp detection failed.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("COLORMAP"), QByteArray(34 * 256, '\x10')) == IdTechImageFormat::DoomColormap,
		"Doom colormap detection failed.");

	QImage png(4, 4, QImage::Format_ARGB32);
	png.fill(Qt::red);
	QByteArray pngBytes;
	{
		QBuffer buffer(&pngBytes);
		buffer.open(QIODevice::WriteOnly);
		png.save(&buffer, "PNG");
	}
	ok &= expect(!pngBytes.isEmpty() && detectIdTechImageFormat(QStringLiteral("gfx/logo.png"), pngBytes) == IdTechImageFormat::QtNative,
		"PNG payloads should be reported as QtNative.");

	ok &= expect(detectIdTechImageFormat(QStringLiteral("junk.bin"), QByteArray()) == IdTechImageFormat::Unknown,
		"Empty payloads should be Unknown.");
	ok &= expect(detectIdTechImageFormat(QStringLiteral("junk.bin"), QByteArray(37, '\x01')) == IdTechImageFormat::Unknown,
		"Arbitrary short payloads should be Unknown.");

	ok &= expect(idTechImageFormatId(IdTechImageFormat::Quake2Wal) == QStringLiteral("quake2-wal"), "Format id mismatch.");
	ok &= expect(!idTechImageFormatDisplayName(IdTechImageFormat::DoomFlat).isEmpty(), "Format display name should not be empty.");
	ok &= expect(idTechImageFormatIsPaletted(IdTechImageFormat::DoomFlat), "Flats are paletted.");
	ok &= expect(!idTechImageFormatIsPaletted(IdTechImageFormat::Targa), "Targa is not palette-dependent.");
	return ok;
}

bool runDecodeSmoke()
{
	bool ok = true;
	const IdTechPalette doomPalette = generatedIdTechPalette(QStringLiteral("doom"));
	const IdTechPalette quakePalette = generatedIdTechPalette(QStringLiteral("quake"));

	// Doom patch: transparent gap plus offsets.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("patches/TESTPAT"), buildDoomPatch(), doomPalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::DoomPatch, "Doom patch should decode.");
		ok &= expect(result.width == 4 && result.height == 4, "Doom patch dimensions mismatch.");
		ok &= expect(result.leftOffset == 2 && result.topOffset == 3, "Doom patch offsets mismatch.");
		ok &= expect(result.hasTransparency, "Doom patch should report transparency.");
		ok &= expect(result.image.pixel(0, 0) == (doomPalette.colorAt(10) | 0xff000000u), "Doom patch first post pixel mismatch.");
		ok &= expect(qAlpha(result.image.pixel(0, 1)) == 0, "Doom patch gap should be transparent.");
		ok &= expect(qAlpha(result.image.pixel(0, 2)) == 0, "Doom patch gap should be transparent.");
		ok &= expect(result.image.pixel(0, 3) == (doomPalette.colorAt(20) | 0xff000000u), "Doom patch second post pixel mismatch.");
		ok &= expect(result.image.pixel(2, 1) == (doomPalette.colorAt(2 * 16 + 1) | 0xff000000u), "Doom patch solid column mismatch.");
		ok &= expect(!idTechImageSummaryLines(result).isEmpty(), "Summary lines should be produced.");
	}

	// Doom flat.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("flats/TESTFLT"), buildDoomFlat(), doomPalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::DoomFlat, "Doom flat should decode.");
		ok &= expect(result.width == 64 && result.height == 64, "Doom flat dimensions mismatch.");
		ok &= expect(result.image.pixelIndex(5, 9) == ((5 * 7 + 9 * 13) & 0xff), "Doom flat pixel index mismatch.");
		ok &= expect(!result.hasTransparency, "Doom flats have no transparency.");
	}

	// Heretic/Hexen-family 64x65 flats.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("flats/TALLFLT"), QByteArray(4160, '\x20'), doomPalette);
		ok &= expect(result.decoded && result.width == 64 && result.height == 65, "64x65 flats should decode.");
	}

	// Quake .lmp.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("gfx/test.lmp"), buildQuakeLump(6, 5), quakePalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::QuakeLump, "Quake lump should decode.");
		ok &= expect(result.width == 6 && result.height == 5, "Quake lump dimensions mismatch.");
		ok &= expect(result.image.pixelIndex(3, 2) == ((3 + 2 * 6) & 0xff), "Quake lump pixel index mismatch.");
	}

	// WAD2 miptex with four mip levels.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("textures/TEST_TEX"),
			buildMipTexture("test_tex", 16, 16, false, -1, -1), quakePalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::QuakeMipTexture, "WAD2 miptex should decode.");
		ok &= expect(result.textureName == QStringLiteral("test_tex"), "WAD2 texture name mismatch.");
		ok &= expect(result.mipLevels.size() == 4, "WAD2 miptex should expose four mip levels.");
		ok &= expect(result.mipLevels.at(0).width() == 16 && result.mipLevels.at(1).width() == 8
			&& result.mipLevels.at(2).width() == 4 && result.mipLevels.at(3).width() == 2, "Mip level sizes mismatch.");
		ok &= expect(result.image.pixelIndex(2, 3) == ((2 + 3 * 3) & 0xff), "WAD2 mip 0 pixel mismatch.");
		ok &= expect(result.mipLevels.at(1).pixelIndex(1, 1) == ((1 + 3 + 1) & 0xff), "WAD2 mip 1 pixel mismatch.");
		ok &= expect(!result.hasTransparency, "Opaque WAD2 textures should not report transparency.");
	}

	// WAD3 miptex with an embedded palette and a '{' cut-out name.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("textures/{fence"),
			buildMipTexture("{fence", 16, 16, true, 2, 3), quakePalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::QuakeMipTexture, "WAD3 miptex should decode.");
		ok &= expect(result.textureName == QStringLiteral("{fence"), "WAD3 texture name mismatch.");
		ok &= expect(result.paletteId == QStringLiteral("wad3-embedded"), "WAD3 textures should use their embedded palette.");
		ok &= expect(!result.paletteGenerated, "Embedded palettes are not generated.");
		ok &= expect(result.hasTransparency, "'{' textures should report transparency at index 255.");
		ok &= expect(qAlpha(result.image.pixel(2, 3)) == 0, "Index 255 should be transparent in a '{' texture.");
		ok &= expect(result.image.pixel(0, 0) == qRgba(0, 255, 0, 255), "Embedded WAD3 palette lookup mismatch.");
		ok &= expect(result.image.pixel(1, 0) == qRgba(1, 254, 3, 255), "Embedded WAD3 palette lookup mismatch.");
	}

	// Quake II .wal with flags.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("textures/e1u1/test.wal"),
			buildQuake2Wal("e1u1/test", "e1u1/test2", 16, 16), generatedIdTechPalette(QStringLiteral("quake2")));
		ok &= expect(result.decoded && result.format == IdTechImageFormat::Quake2Wal, "Quake II .wal should decode.");
		ok &= expect(result.textureName == QStringLiteral("e1u1/test"), "WAL texture name mismatch.");
		ok &= expect(result.animationNextName == QStringLiteral("e1u1/test2"), "WAL animation name mismatch.");
		ok &= expect(result.surfaceFlags == 0x79u, "WAL surface flags mismatch.");
		ok &= expect(result.contentFlags == 0x1u, "WAL content flags mismatch.");
		ok &= expect(result.surfaceValue == 42, "WAL surface value mismatch.");
		ok &= expect(result.mipLevels.size() == 4, "WAL should expose four mip levels.");
		ok &= expect(result.image.pixelIndex(4, 2) == ((4 + 2 * 3 + 5) & 0xff), "WAL mip 0 pixel mismatch.");
	}

	// PCX with a tail palette.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("pics/test.pcx"), buildPcx8Bit(8, 4), quakePalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::Pcx, "PCX should decode.");
		ok &= expect(result.width == 8 && result.height == 4, "PCX dimensions mismatch.");
		ok &= expect(result.paletteId == QStringLiteral("pcx-embedded"), "PCX should prefer its tail palette.");
		const int expectedIndex = (2 * 10 + (5 / 4)) & 0xff;
		ok &= expect(result.image.pixelIndex(5, 1) == expectedIndex, "PCX pixel index mismatch.");
		ok &= expect(result.image.pixel(5, 1) == qRgba(expectedIndex, (expectedIndex * 2) & 0xff, (expectedIndex * 3) & 0xff, 255),
			"PCX tail palette lookup mismatch.");
	}

	// Targa, uncompressed and RLE, plus a colour-mapped image.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("textures/plain.tga"), buildUncompressedTarga24(), doomPalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::Targa, "Uncompressed Targa should decode.");
		ok &= expect(result.width == 3 && result.height == 2, "Targa dimensions mismatch.");
		ok &= expect(result.image.pixel(0, 0) == qRgb(255, 0, 0), "Bottom-origin Targa should be flipped vertically.");
		ok &= expect(result.image.pixel(2, 0) == qRgb(0, 0, 255), "Targa top row mismatch.");
		ok &= expect(result.image.pixel(0, 1) == qRgb(10, 20, 30), "Targa bottom row mismatch.");
	}
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("textures/rle.tga"), buildRleTarga24(), doomPalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::Targa, "RLE Targa should decode.");
		ok &= expect(result.width == 4 && result.height == 2, "RLE Targa dimensions mismatch.");
		ok &= expect(result.image.pixel(0, 0) == qRgb(12, 34, 56) && result.image.pixel(3, 0) == qRgb(12, 34, 56),
			"RLE run packet mismatch.");
		ok &= expect(result.image.pixel(0, 1) == qRgb(1, 2, 3) && result.image.pixel(3, 1) == qRgb(10, 11, 12),
			"RLE raw packet mismatch.");
	}
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("textures/mapped.tga"), buildColorMappedTarga(), doomPalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::Targa, "Colour-mapped Targa should decode.");
		ok &= expect(result.image.pixel(0, 0) == qRgb(9, 99, 199), "Targa colour map lookup mismatch.");
		ok &= expect(result.image.pixel(1, 0) == qRgb(200, 100, 50), "Targa colour map lookup mismatch.");
	}

	// Quake sprite with a single frame and a two-frame group.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("progs/test.spr"), buildQuakeSprite(), quakePalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::QuakeSprite, "Quake sprite should decode.");
		ok &= expect(result.frames.size() == 3, "Quake sprite should expose one single frame plus two group frames.");
		ok &= expect(result.frames.at(0).originX == -2 && result.frames.at(0).originY == 2, "Sprite frame origin mismatch.");
		ok &= expect(result.frames.at(0).image.width() == 2 && result.frames.at(0).image.height() == 2, "Sprite frame size mismatch.");
		ok &= expect(result.frames.at(1).durationMs == 100, "First group frame interval mismatch.");
		ok &= expect(result.frames.at(2).durationMs == 200, "Second group frame interval mismatch.");
		ok &= expect(result.hasTransparency, "Sprites should treat index 255 as transparent.");
		ok &= expect(qAlpha(result.frames.at(0).image.pixel(1, 1)) == 0, "Sprite index 255 should be transparent.");
		ok &= expect(result.width == 4 && result.height == 4, "Sprite should report max frame dimensions.");
	}

	// Palette and colormap lumps are recognised rather than misreported.
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("PLAYPAL"), buildRgbPalette(14), doomPalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::DoomPalette, "Palette lumps should decode to a swatch.");
		ok &= expect(result.image.width() == 192 && result.image.height() == 192, "Palette swatch size mismatch.");
		ok &= expect(result.image.pixel(6, 6) == qRgba(0, 255, 0, 255), "Palette swatch first cell mismatch.");
	}
	{
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("COLORMAP"), QByteArray(34 * 256, '\x10'), doomPalette);
		ok &= expect(result.decoded && result.format == IdTechImageFormat::DoomColormap, "Colormap lumps should decode.");
		ok &= expect(result.width == 256 && result.height == 34, "Colormap dimensions mismatch.");
	}

	return ok;
}

bool runMalformedSmoke()
{
	bool ok = true;
	const IdTechPalette palette = generatedIdTechPalette(QStringLiteral("quake"));

	auto failsCleanly = [&](const QString& path, const QByteArray& bytes, const char* message) {
		const IdTechImageDecodeResult result = decodeIdTechImage(path, bytes, palette);
		const bool clean = !result.decoded && !result.error.isEmpty() && result.image.isNull();
		return expect(clean, message);
	};

	ok &= failsCleanly(QStringLiteral("patches/TESTPAT"), QByteArray(), "Empty payloads must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("patches/TESTPAT"), buildDoomPatch().left(14), "Truncated Doom patches must fail cleanly.");
	{
		QByteArray patch = buildDoomPatch();
		writeLe32At(&patch, 8, 0x7fffffffu);
		ok &= failsCleanly(QStringLiteral("patches/TESTPAT"), patch, "Doom patches with out-of-range column offsets must fail cleanly.");
	}
	{
		QByteArray patch = buildDoomPatch();
		patch.chop(3);
		ok &= failsCleanly(QStringLiteral("patches/TESTPAT"), patch, "Doom patches missing their terminator must fail cleanly.");
	}
	ok &= failsCleanly(QStringLiteral("flats/TESTFLT"), buildDoomFlat().left(4095), "Short flats must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("gfx/test.lmp"), buildQuakeLump(6, 5).left(20), "Truncated Quake lumps must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("textures/TEST_TEX"), buildMipTexture("test_tex", 16, 16, false, -1, -1).left(200),
		"Truncated miptex must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("textures/e1u1/test.wal"), buildQuake2Wal("e1u1/test", "e1u1/test2", 16, 16).left(300),
		"Truncated .wal must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("pics/test.pcx"), buildPcx8Bit(8, 4).left(130), "Truncated PCX must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("textures/plain.tga"), buildUncompressedTarga24().left(20), "Truncated Targa must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("textures/rle.tga"), buildRleTarga24().left(20), "Truncated RLE Targa must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("progs/test.spr"), buildQuakeSprite().left(60), "Truncated Quake sprites must fail cleanly.");
	ok &= failsCleanly(QStringLiteral("junk/blob.bin"), QByteArray(101, '\x7f'), "Unrecognised payloads must fail cleanly.");

	{
		// A sprite with an implausible frame count must be rejected outright.
		QByteArray sprite = buildQuakeSprite();
		writeLe32At(&sprite, 24, 0xffffff00u);
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("progs/test.spr"), sprite, palette);
		ok &= expect(!result.decoded, "Sprites with implausible frame counts must fail.");
	}
	{
		// A PCX claiming 24-bit with 2 planes is not something we decode.
		QByteArray pcx = buildPcx8Bit(8, 4);
		pcx[65] = static_cast<char>(2);
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("pics/test.pcx"), pcx, palette);
		ok &= expect(!result.decoded && !result.error.isEmpty(), "Unsupported PCX plane counts must fail cleanly.");
	}
	{
		// Run-length headers must be sized against the decode target, not against
		// the packed stream: a 19-byte RLE greyscale Targa claiming 16384x16384
		// packs into 256 MiB of samples and a 1 GiB ARGB32 image.
		QByteArray tga = targaHeaderBytes(0, 0, 11, 0, 0, 16384, 16384, 8, 0x20);
		tga.append(static_cast<char>(0xff));  // one 128-pixel run packet
		tga.append(static_cast<char>(0x40));
		ok &= expect(detectIdTechImageFormat(QStringLiteral("textures/huge.tga"), tga) != IdTechImageFormat::Targa,
			"An RLE Targa above the decoded pixel cap must not be classified as Targa.");
		ok &= failsCleanly(QStringLiteral("textures/huge.tga"), tga, "Oversized RLE Targa must fail cleanly.");
	}
	{
		// The same shape for PCX: 8192x8192 single-plane 8-bit stays under the old
		// packed-byte cap of 1<<28 while still asking for a 256 MiB ARGB32 image.
		QByteArray pcx = buildPcx8Bit(8, 4);
		writeLe16At(&pcx, 8, 8191);   // xMax
		writeLe16At(&pcx, 10, 8191);  // yMax
		writeLe16At(&pcx, 66, 8192);  // bytes per line
		ok &= expect(detectIdTechImageFormat(QStringLiteral("pics/huge.pcx"), pcx) != IdTechImageFormat::Pcx,
			"A PCX above the decoded pixel cap must not be classified as PCX.");
		ok &= failsCleanly(QStringLiteral("pics/huge.pcx"), pcx, "Oversized PCX must fail cleanly.");
	}
	{
		// The cap bounds the pixel count, not either dimension, so a wide but short
		// image well inside it still decodes.
		const QByteArray pcx = buildPcx8Bit(8192, 64);
		const IdTechImageDecodeResult result = decodeIdTechImage(QStringLiteral("pics/wide.pcx"), pcx, palette);
		ok &= expect(result.decoded && result.width == 8192 && result.height == 64,
			"A PCX within the decoded pixel cap must still decode.");
	}

	return ok;
}

bool runResolutionSmoke(const QDir& root)
{
	bool ok = true;

	// Directory-backed resolution, including case-insensitive lookup.
	const QString gameDir = root.filePath(QStringLiteral("game"));
	ok &= expect(writeFile(QDir(gameDir).filePath(QStringLiteral("GFX/Palette.LMP")), buildRgbPalette(1)),
		"Palette fixture should be written.");
	const IdTechPaletteResolution fromDir = resolveIdTechPaletteFromDirectory(gameDir, QStringLiteral("quake"));
	ok &= expect(fromDir.fromPackage, "Directory palette resolution should find the fixture.");
	ok &= expect(fromDir.palette.isValid() && !fromDir.palette.generated, "Resolved palette should be real.");
	ok &= expect(fromDir.sourceVirtualPath == QStringLiteral("gfx/palette.lmp"), "Resolved palette path mismatch.");
	ok &= expect(fromDir.palette.colorAt(1) == qRgb(1, 254, 5), "Resolved palette entry mismatch.");
	ok &= expect(!idTechPaletteSummaryLines(fromDir).isEmpty(), "Palette summary lines should be produced.");

	const QString emptyDir = root.filePath(QStringLiteral("empty"));
	ok &= expect(QDir().mkpath(emptyDir), "Empty directory should be created.");
	const IdTechPaletteResolution fallback = resolveIdTechPaletteFromDirectory(emptyDir, QStringLiteral("quake"));
	ok &= expect(!fallback.fromPackage, "Missing palettes should fall back.");
	ok &= expect(fallback.palette.isValid() && fallback.palette.generated, "Fallback palette should be the generated one.");
	ok &= expect(!fallback.warnings.isEmpty(), "Fallback should warn about the missing palette.");

	const IdTechPaletteResolution missingDir = resolveIdTechPaletteFromDirectory(root.filePath(QStringLiteral("nope")), QStringLiteral("doom"));
	ok &= expect(!missingDir.fromPackage && missingDir.palette.isValid(), "Palette resolution must never fail.");

	// Package-backed resolution and decoding through the archive reader.
	QVector<QPair<QByteArray, QByteArray>> files;
	files.push_back({QByteArray("gfx/palette.lmp"), buildRgbPalette(1)});
	files.push_back({QByteArray("textures/e1u1/test.wal"), buildQuake2Wal("e1u1/test", "e1u1/test2", 16, 16)});
	const QString pakPath = root.filePath(QStringLiteral("fixture.pak"));
	ok &= expect(writeFile(pakPath, buildPakArchive(files)), "PAK fixture should be written.");

	PackageArchive archive;
	QString error;
	if (!expect(archive.load(pakPath, &error), "PAK fixture should load.")) {
		return false;
	}
	const IdTechPaletteResolution fromPak = resolveIdTechPalette(archive, QStringLiteral("quake"));
	ok &= expect(fromPak.fromPackage, "Package palette resolution should find gfx/palette.lmp.");
	ok &= expect(fromPak.sourceVirtualPath == QStringLiteral("gfx/palette.lmp"), "Package palette path mismatch.");

	IdTechPaletteResolution used;
	const IdTechImageDecodeResult result = decodeIdTechImageFromArchive(archive, QStringLiteral("textures/e1u1/test.wal"),
		QStringLiteral("quake"), &used);
	ok &= expect(result.decoded && result.format == IdTechImageFormat::Quake2Wal, "Archive decode should work.");
	ok &= expect(result.paletteSourceVirtualPath == QStringLiteral("gfx/palette.lmp"), "Archive decode should record the palette source.");
	ok &= expect(!result.paletteGenerated, "Archive decode should use the package palette.");
	ok &= expect(used.fromPackage, "Resolution output should be populated.");
	ok &= expect(result.image.pixel(0, 0) == qRgb(5, 250, 25), "Archive decode palette lookup mismatch.");

	const IdTechImageDecodeResult missing = decodeIdTechImageFromArchive(archive, QStringLiteral("textures/nope.wal"),
		QStringLiteral("quake"), nullptr);
	ok &= expect(!missing.decoded && !missing.error.isEmpty(), "Missing archive entries must fail cleanly.");
	return ok;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	const QDir root(tempDir.path());
	bool ok = true;
	ok &= runPaletteSmoke();
	ok &= runQuantizeSmoke();
	ok &= runDetectionSmoke();
	ok &= runDecodeSmoke();
	ok &= runMalformedSmoke();
	ok &= runResolutionSmoke(root);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
