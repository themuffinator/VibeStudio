#include "core/texture_export.h"
#include "core/extra_image.h"

#include "core/package_staging.h"
#include "core/texture_output.h"
#include "core/deflate.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace vibestudio {
namespace {
bool fail(QString* error, const QString& text) { if (error) { *error = text; } return false; }
bool checkpoint(const TextureProgress& progress, qint64 done, qint64 total, QString* error)
{
	return !progress || progress(done, total) || fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Texture export cancelled."));
}
bool isQuake(TextureExportFormat format) { return format == TextureExportFormat::QuakeMiptex || format == TextureExportFormat::QuakeWad2; }
bool isMipmapped(TextureExportFormat format) { return isQuake(format) || format == TextureExportFormat::Quake2Wal; }
bool isIndexed(TextureExportFormat format) { return format != TextureExportFormat::Png && format != TextureExportFormat::Targa && format != TextureExportFormat::Dds && format != TextureExportFormat::Ftx; }
QString alphaId(TextureExportAlpha mode)
{
	switch (mode) { case TextureExportAlpha::Strict: return QStringLiteral("strict"); case TextureExportAlpha::Matte: return QStringLiteral("matte"); case TextureExportAlpha::Threshold: return QStringLiteral("threshold"); }
	return {};
}
QString fullbrightId(TextureFullbrightMode mode)
{
	switch (mode) { case TextureFullbrightMode::Preserve: return QStringLiteral("preserve"); case TextureFullbrightMode::Exclude: return QStringLiteral("exclude"); case TextureFullbrightMode::Allow: return QStringLiteral("allow"); }
	return {};
}
QString mipFilterId(TextureMipmapFilter mode)
{
	switch (mode) { case TextureMipmapFilter::Box: return QStringLiteral("box"); case TextureMipmapFilter::Nearest: return QStringLiteral("nearest"); }
	return {};
}
void put16(QByteArray& bytes, qsizetype at, quint16 value) { qToLittleEndian(value, bytes.data() + at); }
void put32(QByteArray& bytes, qsizetype at, quint32 value) { qToLittleEndian(value, bytes.data() + at); }
void putName(QByteArray& bytes, qsizetype at, const QString& name) { const auto encoded = name.toLatin1(); std::memcpy(bytes.data() + at, encoded.constData(), size_t(encoded.size())); }
QByteArray paletteRgb(const IdTechPalette& palette)
{
	QByteArray bytes; bytes.reserve(768);
	for (int i = 0; i < 256; ++i) { const QRgb color = palette.colorAt(i); bytes += char(qRed(color)); bytes += char(qGreen(color)); bytes += char(qBlue(color)); }
	return bytes;
}
bool validName(const QString& name, int limit, bool path, bool optional = false)
{
	if (name.isEmpty()) { return optional; }
	if (name.size() > limit || name == QStringLiteral(".") || name == QStringLiteral("..")) { return false; }
	for (const auto character : name) {
		const ushort c = character.unicode();
		const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (!alnum && !QStringLiteral("_+-!~{}*.").contains(character) && !(path && c == '/')) { return false; }
	}
	if (path) { const auto normalized = normalizePackageVirtualPath(name, false); return normalized.isSafe() && normalized.normalizedPath == name; }
	return true;
}
bool validate(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette, QString* error)
{
	if (image.isNull()) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Cannot export an empty texture.")); }
	if (!validTextureSize(image.size(), error)) { return false; }
	if (image.format() == QImage::Format_Indexed8) {
		TextureDocument validated;
		if (!validated.reset(image, error)) { return false; }
	}
	if (textureExportFormatId(options.format).isEmpty() || alphaId(options.alpha).isEmpty() || fullbrightId(options.fullbright).isEmpty() || mipFilterId(options.mipFilter).isEmpty() ||
		!options.matte.isValid() || options.matte.alpha() != 255 || options.alphaThreshold < 1 || options.alphaThreshold > 255 ||
		options.leftOffset < -32768 || options.leftOffset > 32767 || options.topOffset < -32768 || options.topOffset > 32767) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Invalid export profile, alpha mode, matte, threshold, mipmap filter, or patch offsets."));
	}
	if (isIndexed(options.format)) {
		if (!palette.palette.isValid()) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "This export profile needs a resolved 256-color palette.")); }
		if (palette.palette.generated && !options.allowGeneratedPalette) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The selected palette is a generated stand-in. Resolve a game palette or explicitly allow generated colors.")); }
	}
	if (isMipmapped(options.format) && (image.width() % 16 || image.height() % 16)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Mipmapped game textures require width and height divisible by 16.")); }
	if (isQuake(options.format)) {
		if (!validName(options.name, 15, false)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Quake texture names need 1–15 ASCII letters, digits, or texture-name punctuation, without folders.")); }
		if (!options.extendedLimits && qint64(image.width()) * image.height() * 85 / 64 + 40 > 2 * 1024 * 1024) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "This texture exceeds the original Quake compiler's entire 2 MiB texture lump. Enable source-port limits for a compatible compiler and engine.")); }
		if (options.name.startsWith(QStringLiteral("sky")) && !options.extendedLimits && image.size() != QSize(256, 128)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Classic Quake sky textures require a 256 × 128 canvas.")); }
	}
	if (options.format == TextureExportFormat::Quake2Wal) {
		if (!validName(options.name, 31, true) || !validName(options.animationNext, 31, true, true)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "WAL texture and animation names must be safe relative ASCII names of at most 31 characters.")); }
		if (!options.extendedLimits && qint64(image.width()) * image.height() > 512 * 256) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The original Quake II renderer accepts at most 131,072 indexed pixels. Enable source-port limits only for a compatible target.")); }
	}
	if (options.format == TextureExportFormat::DoomFlat && image.size() != QSize(64, 64)) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Doom flats require exactly 64 × 64 pixels.")); }
	if (options.format == TextureExportFormat::DoomPatch && !options.extendedLimits && image.height() > 255) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Classic Doom patches are limited to 255 rows. Enable source-port limits to write tall patch posts.")); }
	const bool alphaSupported = options.format == TextureExportFormat::Dds || options.format == TextureExportFormat::Ftx || options.format == TextureExportFormat::Png || options.format == TextureExportFormat::Targa || options.format == TextureExportFormat::IndexedPng || options.format == TextureExportFormat::DoomPatch;
	if (options.alpha == TextureExportAlpha::Threshold && !alphaSupported) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "This opaque profile cannot use alpha thresholding. Choose a matte or supply an opaque image.")); }
	return true;
}

QImage prepareAlpha(const QImage& source, const TextureExportOptions& options, qint64* changed, QString* error, const TextureProgress& progress)
{
	QImage image = source.convertToFormat(QImage::Format_ARGB32);
	if (image.isNull()) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unable to allocate export pixels.")); return {}; }
	const bool opaque = options.format != TextureExportFormat::Dds && options.format != TextureExportFormat::Ftx && options.format != TextureExportFormat::Png && options.format != TextureExportFormat::IndexedPng && options.format != TextureExportFormat::Targa && options.format != TextureExportFormat::DoomPatch;
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height(), error)) { return {}; }
		auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			const int alpha = qAlpha(row[x]);
			if (options.alpha == TextureExportAlpha::Strict) {
				if ((opaque && alpha != 255) || (options.format == TextureExportFormat::DoomPatch && alpha != 0 && alpha != 255)) {
					fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The image contains alpha that this profile cannot store. Choose a matte, or threshold for a patch.")); return {};
				}
			} else if (options.alpha == TextureExportAlpha::Matte) {
				const auto blend = [alpha](int value, int background) { return (value * alpha + background * (255 - alpha) + 127) / 255; };
				row[x] = qRgb(blend(qRed(row[x]), options.matte.red()), blend(qGreen(row[x]), options.matte.green()), blend(qBlue(row[x]), options.matte.blue()));
			} else { row[x] = (row[x] & 0x00ffffffu) | (alpha >= options.alphaThreshold ? 0xff000000u : 0u); }
			if (qAlpha(row[x]) != alpha) { ++*changed; }
		}
	}
	return checkpoint(progress, 1, 1, error) ? image : QImage();
}

QImage quantize(const QImage& source, const QImage& prepared, const TextureExportOptions& options, const IdTechPalette& palette, bool* preserved, QString* error, const TextureProgress& progress)
{
	IdTechPalette effective = palette;
	const bool indexedPng = options.format == TextureExportFormat::IndexedPng;
	const int fullbright = isQuake(options.format) ? 224 : 256;
	const int limit = options.format == TextureExportFormat::Quake2Wal ? 255 : 256;
	for (int i = 0; i < 256; ++i) { effective.colors[i] = indexedPng ? palette.colorAt(i) : (palette.colorAt(i) | 0xff000000u); }
	if (!indexedPng) { effective.transparentIndex = -1; }
	const auto storedTable = effective.colors;
	const bool sourceIndexed = source.format() == QImage::Format_Indexed8 && source.colorCount() > 0 && source.colorCount() <= 256;
	bool matching[256]{};
	for (int index = 0; sourceIndexed && index < source.colorCount(); ++index) {
		matching[index] = index < limit && (options.fullbright != TextureFullbrightMode::Exclude || index < fullbright) &&
			(indexedPng ? source.color(index) == storedTable[index] : (source.color(index) & 0x00ffffffu) == (storedTable[index] & 0x00ffffffu));
	}
	const auto unchanged = [&](int index, QRgb preparedPixel) {
		return matching[index] && (indexedPng ? preparedPixel == storedTable[index] : (preparedPixel & 0x00ffffffu) == (storedTable[index] & 0x00ffffffu));
	};
	bool exact = sourceIndexed;
	if (exact) {
		for (int y = 0; y < source.height() && exact; ++y) {
			if (!checkpoint(progress, y, source.height() * 4, error)) { return {}; }
			const auto* row = source.constScanLine(y); const auto* rgba = reinterpret_cast<const QRgb*>(prepared.constScanLine(y));
			for (int x = 0; x < source.width(); ++x) { if (!unchanged(row[x], rgba[x])) { exact = false; break; } }
		}
	}
	if (exact) { QImage image = source; image.setColorTable(storedTable); *preserved = true; return checkpoint(progress, 1, 1, error) ? image : QImage(); }
	// Duplicate disallowed colors at the first entry. The common quantizer's
	// stable lowest-index tie break then excludes them without a second algorithm.
	const int candidates = options.fullbright == TextureFullbrightMode::Allow ? limit : std::min(limit, fullbright);
	for (int index = candidates; index < 256; ++index) { effective.colors[index] = effective.colors[0]; }
	QImage image = quantizeToIdTechPalette(prepared, effective, options.dither, [&](int done, int total) { return checkpoint(progress, total + done * 2, total * 4, error); });
	if (image.isNull()) { if (error && error->isEmpty()) { *error = QCoreApplication::translate("VibeStudioTextureExport", "Unable to quantize export pixels."); } return {}; }
	image.setColorTable(storedTable);
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, image.height() * 3 + y, image.height() * 4, error)) { return {}; }
		const auto* rgba = reinterpret_cast<const QRgb*>(prepared.constScanLine(y)); auto* indices = image.scanLine(y);
		const auto* original = sourceIndexed ? source.constScanLine(y) : nullptr;
		for (int x = 0; x < image.width(); ++x) {
			if (original && unchanged(original[x], rgba[x])) { indices[x] = original[x]; }
			if (indexedPng && qAlpha(storedTable[indices[x]]) != qAlpha(rgba[x])) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The selected palette cannot preserve this image's alpha. Choose a compatible palette, a matte, or RGBA PNG.")); return {}; }
		}
	}
	return checkpoint(progress, 1, 1, error) ? image : QImage();
}

QImage boxMip(const QImage& source, bool quake, TextureFullbrightMode mode, QString* error, const TextureProgress& progress)
{
	QImage image(source.size() / 2, QImage::Format_Indexed8); image.setColorTable(source.colorTable());
	if (image.isNull()) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unable to allocate mipmap pixels.")); return {}; }
	QHash<quint32, uchar> cache;
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height(), error)) { return {}; }
		auto* output = image.scanLine(y);
		for (int x = 0; x < image.width(); ++x) {
			const int indices[]{source.pixelIndex(x * 2, y * 2), source.pixelIndex(x * 2 + 1, y * 2), source.pixelIndex(x * 2, y * 2 + 1), source.pixelIndex(x * 2 + 1, y * 2 + 1)};
			if (std::all_of(std::begin(indices) + 1, std::end(indices), [&](int value) { return value == indices[0]; })) { output[x] = uchar(indices[0]); continue; }
			const bool bright = quake && mode != TextureFullbrightMode::Exclude && std::any_of(std::begin(indices), std::end(indices), [](int value) { return value >= 224; });
			int red = 0, green = 0, blue = 0, count = 0;
			for (int index : indices) { if (bright && index < 224) { continue; } const QRgb c = source.color(index); red += qRed(c); green += qGreen(c); blue += qBlue(c); ++count; }
			red = (red + count / 2) / count; green = (green + count / 2) / count; blue = (blue + count / 2) / count;
			const quint32 key = quint32(qRgb(red, green, blue) & 0x00ffffffu) | (bright ? 0x01000000u : 0u);
			const auto found = cache.constFind(key); if (found != cache.constEnd()) { output[x] = found.value(); continue; }
			const int begin = bright ? 224 : 0, end = quake ? (bright ? 256 : 224) : 255;
			int best = begin, distance = std::numeric_limits<int>::max();
			for (int index = begin; index < end; ++index) {
				const QRgb c = source.color(index); const int dr = red - qRed(c), dg = green - qGreen(c), db = blue - qBlue(c);
				const int candidate = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
				if (candidate < distance) { best = index; distance = candidate; if (distance == 0) { break; } }
			}
			output[x] = uchar(best); if (cache.size() < 65536) { cache.insert(key, uchar(best)); }
		}
	}
	return checkpoint(progress, 1, 1, error) ? image : QImage();
}

QByteArray packedRows(const QImage& image)
{
	QByteArray bytes; bytes.resize(qsizetype(image.width()) * image.height());
	for (int y = 0; y < image.height(); ++y) { std::memcpy(bytes.data() + y * image.width(), image.constScanLine(y), size_t(image.width())); }
	return bytes;
}

// Original PNG 3 sections 5, 9–11 implementation (W3C, 2025-06-24), without
// specification sample code. Explicit color type 3 retains grayscale palettes.
QByteArray encodeIndexedPng(const QImage& image, QString* error, const TextureProgress& progress)
{
	QByteArray rows; rows.reserve((image.width() + 1) * image.height());
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height() * 2, error)) { return {}; }
		rows += '\0'; rows.append(reinterpret_cast<const char*>(image.constScanLine(y)), image.width());
	}
	if (!checkpoint(progress, 1, 2, error)) { return {}; }
	const auto compressed = qCompress(rows, 6);
	if (compressed.size() <= 4) { fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unable to compress indexed PNG pixels.")); return {}; }
	QByteArray bytes = QByteArray::fromHex("89504e470d0a1a0a");
	const auto chunk = [&](const QByteArray& type, const QByteArray& payload) {
		QByteArray size(4, '\0'); qToBigEndian(quint32(payload.size()), size.data()); bytes += size; bytes += type; bytes += payload;
		qToBigEndian(crc32Bytes(payload, crc32Bytes(type)), size.data()); bytes += size;
	};
	QByteArray header(13, '\0'); qToBigEndian(quint32(image.width()), header.data()); qToBigEndian(quint32(image.height()), header.data() + 4);
	header[8] = 8; header[9] = 3; chunk(QByteArrayLiteral("IHDR"), header);
	QByteArray colors, alpha; bool transparent = false;
	for (QRgb color : image.colorTable()) { colors += char(qRed(color)); colors += char(qGreen(color)); colors += char(qBlue(color)); alpha += char(qAlpha(color)); transparent |= qAlpha(color) != 255; }
	chunk(QByteArrayLiteral("PLTE"), colors); if (transparent) { chunk(QByteArrayLiteral("tRNS"), alpha); }
	chunk(QByteArrayLiteral("IDAT"), compressed.mid(4)); chunk(QByteArrayLiteral("IEND"), {});
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}

// Reimplemented layouts, not copied engine code. GPL-2.0-or-later references:
// id-Software/Quake WinQuake/bspfile.h and wad.h; Quake-2 qcommon/qfiles.h;
// Quake-III-Arena code/renderer/tr_image.c. Reviewed 2026-10-04; docs/CREDITS.md.
QByteArray encodeMip(const QVector<QImage>& mips, const TextureExportOptions& options)
{
	const bool wal = options.format == TextureExportFormat::Quake2Wal;
	QByteArray bytes(wal ? 100 : 40, '\0'); putName(bytes, 0, options.name);
	put32(bytes, wal ? 32 : 16, quint32(mips[0].width())); put32(bytes, wal ? 36 : 20, quint32(mips[0].height()));
	if (wal) { putName(bytes, 56, options.animationNext); put32(bytes, 88, options.surfaceFlags); put32(bytes, 92, options.contentFlags); put32(bytes, 96, quint32(options.surfaceValue)); }
	for (int level = 0; level < 4; ++level) { put32(bytes, (wal ? 40 : 24) + level * 4, quint32(bytes.size())); bytes += packedRows(mips[level]); }
	if (options.format != TextureExportFormat::QuakeWad2) { return bytes; }
	QByteArray wad(12, '\0'); std::memcpy(wad.data(), "WAD2", 4); put32(wad, 4, 1); put32(wad, 8, quint32(12 + bytes.size())); wad += bytes;
	QByteArray directory(32, '\0'); put32(directory, 0, 12); put32(directory, 4, quint32(bytes.size())); put32(directory, 8, quint32(bytes.size())); directory[12] = char(0x44); putName(directory, 16, options.name);
	return wad + directory;
}
QByteArray encodeTarga(const QImage& image, QString* error, const TextureProgress& progress)
{
	QByteArray bytes(18, '\0'); bytes[2] = 2; put16(bytes, 12, quint16(image.width())); put16(bytes, 14, quint16(image.height())); bytes[16] = 32; bytes[17] = 8;
	bytes.reserve(18 + image.width() * image.height() * 4 + 26);
	// Bottom-left origin works with original Quake II/III readers, which ignore
	// or reject top-origin images. Descriptor 8 explicitly declares alpha.
	for (int row = 0; row < image.height(); ++row) {
		if (!checkpoint(progress, row, image.height(), error)) { return {}; }
		const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(image.height() - 1 - row));
		for (int x = 0; x < image.width(); ++x) { bytes += char(qBlue(pixels[x])); bytes += char(qGreen(pixels[x])); bytes += char(qRed(pixels[x])); bytes += char(qAlpha(pixels[x])); }
	}
	bytes += QByteArray(8, '\0'); bytes += QByteArray("TRUEVISION-XFILE.\0", 18);
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}
QByteArray encodePcx(const QImage& image, const IdTechPalette& palette, QString* error, const TextureProgress& progress)
{
	const int stride = (image.width() + 1) & ~1;
	QByteArray bytes(128, '\0'); bytes[0] = 10; bytes[1] = 5; bytes[2] = 1; bytes[3] = 8;
	put16(bytes, 8, quint16(image.width() - 1)); put16(bytes, 10, quint16(image.height() - 1)); put16(bytes, 12, 72); put16(bytes, 14, 72); bytes[65] = 1; put16(bytes, 66, quint16(stride)); put16(bytes, 68, 1);
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(progress, y, image.height(), error)) { return {}; }
		const auto* row = image.constScanLine(y);
		for (int x = 0; x < stride;) {
			const uchar value = x < image.width() ? row[x] : 0; int run = 1;
			while (run < 63 && x + run < stride && (x + run < image.width() ? row[x + run] : 0) == value) { ++run; }
			if (run > 1 || value >= 0xc0) { bytes += char(0xc0 | run); } bytes += char(value); x += run;
		}
	}
	bytes += char(12); bytes += paletteRgb(palette);
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}

QByteArray encodePatch(const QImage& indexed, const QImage& alpha, const TextureExportOptions& options, QString* error, const TextureProgress& progress)
{
	// Doom's layout: Chocolate Doom 3.1.0 src/v_patch.h (GPL-2.0-or-later).
	// Relative top deltas: GZDoom src/common/textures/formats/patchtexture.cpp
	// (BSD-3-Clause), reviewed 2026-10-04. Layout/behavior only; no code copied.
	// Zero-length posts advance the origin without drawing any pixel.
	QByteArray bytes(8 + indexed.width() * 4, '\0'); put16(bytes, 0, quint16(indexed.width())); put16(bytes, 2, quint16(indexed.height()));
	put16(bytes, 4, quint16(options.leftOffset)); put16(bytes, 6, quint16(options.topOffset));
	for (int x = 0; x < indexed.width(); ++x) {
		if (!checkpoint(progress, x, indexed.width(), error)) { return {}; }
		put32(bytes, 8 + x * 4, quint32(bytes.size())); int previous = -1;
		const auto emptyPost = [&](int delta) { bytes += char(delta); bytes += QByteArray(3, '\0'); };
		for (int y = 0; y < indexed.height();) {
			if (qAlpha(alpha.pixel(x, y)) == 0) { ++y; continue; }
			int length = 1;
			while (length < 255 && y + length < indexed.height() && qAlpha(alpha.pixel(x, y + length)) != 0) { ++length; }
			int delta = y;
			if (y >= 255) {
				if (previous < 254) { emptyPost(254); previous = 254; }
				while (y - previous > 254) { emptyPost(254); previous += 254; }
				delta = y - previous;
			}
			bytes += char(delta); bytes += char(length); bytes += char(0);
			for (int row = 0; row < length; ++row) { bytes += char(indexed.pixelIndex(x, y + row)); }
			bytes += char(0); previous = y; y += length;
		}
		bytes += char(255);
	}
	return checkpoint(progress, 1, 1, error) ? bytes : QByteArray();
}
} // namespace

QVector<TextureExportProfile> textureExportProfiles()
{
	return {
		{TextureExportFormat::Png, QStringLiteral("png"), QCoreApplication::translate("VibeStudioTextureExport", "PNG · RGBA"), QStringLiteral("png"), QCoreApplication::translate("VibeStudioTextureExport", "Lossless color and alpha. Requires a PNG-capable target engine."), false, false},
		{TextureExportFormat::IndexedPng, QStringLiteral("png-indexed"), QCoreApplication::translate("VibeStudioTextureExport", "PNG · game palette"), QStringLiteral("png"), QCoreApplication::translate("VibeStudioTextureExport", "Indexed PNG with an embedded palette. Alpha must be representable by that palette."), true, false},
		{TextureExportFormat::Targa, QStringLiteral("tga"), QCoreApplication::translate("VibeStudioTextureExport", "Targa · RGBA"), QStringLiteral("tga"), QCoreApplication::translate("VibeStudioTextureExport", "32-bit BGRA, bottom-left origin, explicit alpha; suitable for Quake III image references."), false, false},
		{TextureExportFormat::Dds, QStringLiteral("dds"), QStringLiteral("DDS · BGRA8"), QStringLiteral("dds"), QCoreApplication::translate("VibeStudioTextureExport", "Uncompressed 32-bit color and alpha, one surface without mipmaps. Requires a DDS-capable target."), false, false},
		{TextureExportFormat::Ftx, QStringLiteral("ftx"), QStringLiteral("FTX · RGBA8"), QStringLiteral("ftx"), QCoreApplication::translate("VibeStudioTextureExport", "Lossless 32-bit color and alpha for FTX-capable tools and engines."), false, false},
		{TextureExportFormat::Pcx, QStringLiteral("pcx"), QCoreApplication::translate("VibeStudioTextureExport", "PCX · opaque palette"), QStringLiteral("pcx"), QCoreApplication::translate("VibeStudioTextureExport", "8-bit RLE with an embedded palette and even row stride. Original Quake II has additional size and row-stride limits."), true, false},
		{TextureExportFormat::QuakeMiptex, QStringLiteral("quake-miptex"), QCoreApplication::translate("VibeStudioTextureExport", "Quake · miptexture"), QStringLiteral("mip"), QCoreApplication::translate("VibeStudioTextureExport", "Four opaque indexed mip levels; dimensions divisible by 16. The game supplies the palette."), true, true},
		{TextureExportFormat::QuakeWad2, QStringLiteral("quake-wad2"), QCoreApplication::translate("VibeStudioTextureExport", "Quake · WAD2 texture"), QStringLiteral("wad"), QCoreApplication::translate("VibeStudioTextureExport", "One miptexture in a WAD2 archive, with lump type 0x44 and four mip levels."), true, true},
		{TextureExportFormat::Quake2Wal, QStringLiteral("quake2-wal"), QCoreApplication::translate("VibeStudioTextureExport", "Quake II · WAL"), QStringLiteral("wal"), QCoreApplication::translate("VibeStudioTextureExport", "Four opaque mips with animation and surface metadata. Index 255 is reserved; classic output is limited to 131,072 pixels."), true, true},
		{TextureExportFormat::DoomFlat, QStringLiteral("doom-flat"), QCoreApplication::translate("VibeStudioTextureExport", "Doom · flat"), QStringLiteral("lmp"), QCoreApplication::translate("VibeStudioTextureExport", "Raw 64 × 64 opaque indexed pixels. Place between flat namespace markers in a Doom WAD."), true, false},
		{TextureExportFormat::DoomPatch, QStringLiteral("doom-patch"), QCoreApplication::translate("VibeStudioTextureExport", "Doom · patch"), QStringLiteral("lmp"), QCoreApplication::translate("VibeStudioTextureExport", "Column posts with binary transparency and signed offsets. Tall patches require compatible source ports."), true, false}
	};
}
QString textureExportFormatId(TextureExportFormat format) { for (const auto& profile : textureExportProfiles()) { if (profile.format == format) { return profile.id; } } return {}; }
QString textureExportSuffix(TextureExportFormat format) { for (const auto& profile : textureExportProfiles()) { if (profile.format == format) { return profile.suffix; } } return {}; }
bool textureExportFormatFromId(const QString& id, TextureExportFormat* format) { for (const auto& profile : textureExportProfiles()) { if (profile.id == id) { if (format) { *format = profile.format; } return true; } } return false; }

QJsonObject textureExportOptionsJson(const TextureExportOptions& options)
{
	return {{QStringLiteral("version"), 1}, {QStringLiteral("profile"), textureExportFormatId(options.format)}, {QStringLiteral("alpha"), alphaId(options.alpha)},
		{QStringLiteral("matte"), options.matte.name(QColor::HexArgb)}, {QStringLiteral("alphaThreshold"), options.alphaThreshold}, {QStringLiteral("dither"), options.dither},
		{QStringLiteral("allowGeneratedPalette"), options.allowGeneratedPalette}, {QStringLiteral("extendedLimits"), options.extendedLimits}, {QStringLiteral("mipFilter"), mipFilterId(options.mipFilter)},
		{QStringLiteral("fullbright"), fullbrightId(options.fullbright)}, {QStringLiteral("name"), options.name}, {QStringLiteral("animationNext"), options.animationNext},
		{QStringLiteral("surfaceFlags"), double(options.surfaceFlags)}, {QStringLiteral("contentFlags"), double(options.contentFlags)}, {QStringLiteral("surfaceValue"), options.surfaceValue},
		{QStringLiteral("leftOffset"), options.leftOffset}, {QStringLiteral("topOffset"), options.topOffset}};
}
TextureExportOptions textureExportOptionsForImage(const IdTechImageDecodeResult& decoded)
{
	TextureExportOptions options;
	textureExportFormatFromId(decoded.formatId == QStringLiteral("targa") ? QStringLiteral("tga") : decoded.formatId, &options.format);
	if (!decoded.textureName.isEmpty()) { options.name = decoded.textureName; }
	options.animationNext = decoded.animationNextName; options.surfaceFlags = decoded.surfaceFlags;
	options.contentFlags = decoded.contentFlags; options.surfaceValue = decoded.surfaceValue;
	options.leftOffset = decoded.leftOffset; options.topOffset = decoded.topOffset;
	return options;
}
bool textureExportOptionsFromJson(const QJsonObject& object, TextureExportOptions* options, QString* error)
{
	TextureExportOptions parsed;
	const auto defaults = textureExportOptionsJson(parsed);
	for (auto it = object.begin(); it != object.end(); ++it) { if (!defaults.contains(it.key())) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Unknown texture export option: %1").arg(it.key())); } }
	const auto field = [&](const char* key) { const auto name = QLatin1String(key); return object.value(name).isUndefined() ? defaults.value(name) : object.value(name); };
	const auto number = [&](const char* key, qint64 low, qint64 high, auto* output) {
		const auto value = field(key); const double n = value.toDouble(std::numeric_limits<double>::quiet_NaN());
		if (!value.isDouble() || !std::isfinite(n) || std::floor(n) != n || n < double(low) || n > double(high)) { return false; }
		*output = static_cast<std::remove_reference_t<decltype(*output)>>(n); return true;
	};
	const auto boolean = [&](const char* key, bool* output) { const auto value = field(key); if (!value.isBool()) { return false; } *output = value.toBool(); return true; };
	const auto textField = [&](const char* key, QString* output, int maximum) { const auto value = field(key); if (!value.isString() || value.toString().size() > maximum || value.toString().contains(QChar::Null)) { return false; } *output = value.toString(); return true; };
	int version = 0; QString alpha, mip, fullbright, matte;
	bool valid = number("version", 1, 1, &version) && textureExportFormatFromId(field("profile").toString(), &parsed.format) &&
		textField("alpha", &alpha, 16) && textField("mipFilter", &mip, 16) && textField("fullbright", &fullbright, 16) && textField("matte", &matte, 16) &&
		number("alphaThreshold", 1, 255, &parsed.alphaThreshold) && boolean("dither", &parsed.dither) && boolean("allowGeneratedPalette", &parsed.allowGeneratedPalette) && boolean("extendedLimits", &parsed.extendedLimits) &&
		textField("name", &parsed.name, 128) && textField("animationNext", &parsed.animationNext, 128) && number("surfaceFlags", 0, 0xffffffffll, &parsed.surfaceFlags) &&
		number("contentFlags", 0, 0xffffffffll, &parsed.contentFlags) && number("surfaceValue", -2147483648ll, 2147483647ll, &parsed.surfaceValue) &&
		number("leftOffset", -32768, 32767, &parsed.leftOffset) && number("topOffset", -32768, 32767, &parsed.topOffset);
	parsed.matte = QColor(matte);
	valid = valid && parsed.matte.isValid() && parsed.matte.alpha() == 255;
	if (alpha == QStringLiteral("strict")) { parsed.alpha = TextureExportAlpha::Strict; } else if (alpha == QStringLiteral("matte")) { parsed.alpha = TextureExportAlpha::Matte; } else if (alpha == QStringLiteral("threshold")) { parsed.alpha = TextureExportAlpha::Threshold; } else { valid = false; }
	if (mip == QStringLiteral("box")) { parsed.mipFilter = TextureMipmapFilter::Box; } else if (mip == QStringLiteral("nearest")) { parsed.mipFilter = TextureMipmapFilter::Nearest; } else { valid = false; }
	if (fullbright == QStringLiteral("preserve")) { parsed.fullbright = TextureFullbrightMode::Preserve; } else if (fullbright == QStringLiteral("exclude")) { parsed.fullbright = TextureFullbrightMode::Exclude; } else if (fullbright == QStringLiteral("allow")) { parsed.fullbright = TextureFullbrightMode::Allow; } else { valid = false; }
	if (!options || !valid) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Invalid or unsupported texture export options.")); }
	*options = std::move(parsed); return true;
}

TextureExportResult encodeTextureExport(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureProgress& progress)
{
	TextureExportResult result;
	if (!validate(image, options, palette, &result.error)) { return result; }
	const auto part = [&](int step, int count = 1) { return [&, step, count](qint64 done, qint64 total) { return !progress || progress(step * 1000 + done * count * 1000 / std::max(qint64(1), total), 8000); }; };
	QImage pixels = prepareAlpha(image, options, &result.alphaChangedPixels, &result.error, part(0));
	if (pixels.isNull()) { return result; }
	QImage indexed;
	if (isIndexed(options.format)) {
		indexed = quantize(image, pixels, options, palette.palette, &result.preservedIndices, &result.error, part(1, 2));
		if (indexed.isNull()) { return result; }
		result.preview = indexed;
		if (options.format == TextureExportFormat::DoomPatch) {
			result.preview = indexed.convertToFormat(QImage::Format_ARGB32);
			for (int y = 0; y < pixels.height(); ++y) { auto* row = reinterpret_cast<QRgb*>(result.preview.scanLine(y)); for (int x = 0; x < pixels.width(); ++x) { row[x] = (row[x] & 0x00ffffffu) | (pixels.pixel(x, y) & 0xff000000u); } }
		}
		if (palette.palette.generated) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Generated palette used: colors will not match an original game palette."); }
	} else { result.preview = pixels; }
	for (int y = 0; y < image.height(); ++y) {
		if (!checkpoint(part(3), y, image.height(), &result.error)) { return result; }
		for (int x = 0; x < image.width(); ++x) {
			if ((image.pixel(x, y) & 0x00ffffffu) != (result.preview.pixel(x, y) & 0x00ffffffu)) { ++result.colorChangedPixels; }
		}
	}
	if (isMipmapped(options.format)) {
		result.mipLevels << indexed;
		for (int level = 1; level < 4; ++level) {
			const auto report = [&](qint64 done, qint64 total) { return part(4, 2)((level - 1) * 1000 + done * 1000 / std::max(qint64(1), total), 3000); };
			const auto previous = result.mipLevels.last();
			QImage mip = options.mipFilter == TextureMipmapFilter::Nearest ? resizeTexturePixels(previous, previous.size() / 2, false, &result.error, report) : boxMip(previous, isQuake(options.format), options.fullbright, &result.error, report);
			if (mip.isNull()) { return result; } result.mipLevels << mip;
		}
		result.bytes = encodeMip(result.mipLevels, options);
	} else if (options.format == TextureExportFormat::IndexedPng) { result.bytes = encodeIndexedPng(indexed, &result.error, part(6)); }
	else if (options.format == TextureExportFormat::Png) {
		result.bytes = encodeTextureOutputPng(result.preview, &result.error, [&] { return !checkpoint(part(6), 0, 1, &result.error); });
		if (result.bytes.isEmpty()) { return result; }
	} else if (options.format == TextureExportFormat::Targa) { result.bytes = encodeTarga(pixels, &result.error, part(6)); }
	else if (options.format == TextureExportFormat::Dds || options.format == TextureExportFormat::Ftx) {
		result.bytes = encodeExtraImage(pixels, options.format == TextureExportFormat::Dds, &result.error, [&] { return !checkpoint(part(6), 0, 1, &result.error); });
	}
	else if (options.format == TextureExportFormat::Pcx) {
		result.bytes = encodePcx(indexed, palette.palette, &result.error, part(6));
		if (image.width() % 2 || image.width() > 640 || image.height() > 480 || qint64(image.width()) * image.height() > 512 * 256) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Valid PCX, but outside original Quake II's even-width, 640 × 480 and 131,072-pixel loading limits."); }
		result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "PCX stores no alpha. Quake II uses its global palette and interprets index 255 as transparent.");
	} else if (options.format == TextureExportFormat::DoomFlat) { result.bytes = packedRows(indexed); }
	else if (options.format == TextureExportFormat::DoomPatch) {
		result.bytes = encodePatch(indexed, pixels, options, &result.error, part(6));
		if (image.height() > 255) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Tall patch posts require a compatible source port."); }
		if (image.width() > 2048 || image.height() > 2048 || std::abs(options.leftOffset) >= 4096 || std::abs(options.topOffset) >= 4096) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "These patch dimensions or offsets exceed GZDoom's 2048-pixel and ±4095-offset recognition limits. Confirm support in the target port."); }
		if (result.bytes.size() > 65535) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Large patches can exceed original Doom wall-texture column offsets; review the target engine and texture composition."); }
	}
	if (result.bytes.isEmpty() || result.bytes.size() > textureOutputByteLimit) { if (result.error.isEmpty()) { result.error = QCoreApplication::translate("VibeStudioTextureExport", "The encoded texture is empty or exceeds 64 MiB."); } return result; }
	if (!checkpoint(progress, 8000, 8000, &result.error)) { result.bytes.clear(); return result; }
	if (options.extendedLimits && (isMipmapped(options.format) || options.format == TextureExportFormat::DoomPatch)) { result.warnings << QCoreApplication::translate("VibeStudioTextureExport", "Source-port limits enabled; validate the chosen engine and compiler before packaging."); }
	result.succeeded = true; return result;
}

bool saveTextureExport(const QImage& image, const TextureExportOptions& options, const IdTechPaletteResolution& palette, const QString& path,
	bool overwrite, bool dryRun, TextureExportResult* result, QString* error, const TextureProgress& progress)
{
	if (error) { error->clear(); }
	if (QFileInfo(path).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The export filename extension must match the selected profile.")); }
	TextureOutputTarget target; if (!inspectTextureOutputTarget(path, overwrite, &target, error, [&] { return progress && !progress(0, 1); })) { return false; }
	auto encoded = encodeTextureExport(image, options, palette, progress);
	const bool ready = encoded.succeeded;
	if (!ready) { if (error) { *error = encoded.error; } if (result) { *result = std::move(encoded); } return false; }
	const bool written = writeTextureOutput(target, encoded.bytes, dryRun, error);
	if (result) { *result = std::move(encoded); } return written;
}
QJsonObject textureExportReportJson(const TextureExportOptions& options, const IdTechPaletteResolution& palette, const TextureExportResult& result)
{
	QJsonArray mips;
	for (const auto& mip : result.mipLevels) { mips.append(QJsonObject{{QStringLiteral("width"), mip.width()}, {QStringLiteral("height"), mip.height()}}); }
	QJsonObject object{{QStringLiteral("options"), textureExportOptionsJson(options)}, {QStringLiteral("succeeded"), result.succeeded}, {QStringLiteral("error"), result.error},
		{QStringLiteral("width"), result.preview.width()}, {QStringLiteral("height"), result.preview.height()}, {QStringLiteral("bytes"), double(result.bytes.size())},
		{QStringLiteral("colorChangedPixels"), double(result.colorChangedPixels)}, {QStringLiteral("alphaChangedPixels"), double(result.alphaChangedPixels)},
		{QStringLiteral("preservedIndices"), result.preservedIndices}, {QStringLiteral("mipLevels"), mips}, {QStringLiteral("warnings"), QJsonArray::fromStringList(result.warnings)},
		{QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(result.bytes, QCryptographicHash::Sha256).toHex())}};
	if (isIndexed(options.format)) { object.insert(QStringLiteral("palette"), QJsonObject{{QStringLiteral("id"), palette.palette.id}, {QStringLiteral("generated"), palette.palette.generated},
		{QStringLiteral("source"), palette.palette.sourceDescription}, {QStringLiteral("sourcePackage"), palette.sourcePackagePath}, {QStringLiteral("sourceEntry"), palette.sourceVirtualPath},
		{QStringLiteral("rgbSha256"), QString::fromLatin1(QCryptographicHash::hash(paletteRgb(palette.palette), QCryptographicHash::Sha256).toHex())}}); }
	return object;
}

bool textureExportSupportsQuake3Map(TextureExportFormat format)
{
	return format == TextureExportFormat::Png || format == TextureExportFormat::IndexedPng || format == TextureExportFormat::Targa;
}

bool stageTextureExport(const TextureExportResult& result, const TextureExportOptions& options, const QString& path,
	PackageStagingModel* staging, bool replaceExisting, QString* error)
{
	if (error) { error->clear(); }
	if (!staging || !staging->isLoaded() || !result.succeeded || result.bytes.isEmpty() || result.bytes.size() > 64 * 1024 * 1024
		|| result.preview.isNull() || !validTextureSize(result.preview.size(), error)) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Open a package and validate a bounded texture export before staging."));
	}
	if (staging->sourceFormat() == PackageArchiveFormat::Wad) {
		const auto magic = staging->sourceWadMagic();
		if (magic == QStringLiteral("WAD2") && options.format == TextureExportFormat::QuakeMiptex) {
			if (path != options.name) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "The WAD lump name must match the miptexture export name.")); }
			return staging->addWadBytes(result.bytes, path, {}, 0x44, replaceExisting, error);
		}
		if ((magic == QStringLiteral("PWAD") || magic == QStringLiteral("IWAD"))
			&& (options.format == TextureExportFormat::DoomFlat || options.format == TextureExportFormat::DoomPatch)) {
			QString namespaceId = options.format == TextureExportFormat::DoomFlat ? QStringLiteral("flat") : QStringLiteral("patch");
			if (replaceExisting && options.format == TextureExportFormat::DoomPatch) {
				// Menus and title pictures are valid unnamespaced patches. Retain
				// their position after checking the existing native payload; a
				// palette, map lump, or arbitrary global blob is not a patch target.
				const PackageStagingArchive planned(*staging);
				for (const auto& entry : planned.entries()) {
					if (entry.virtualPath.compare(path, Qt::CaseInsensitive) != 0 || entry.typeHint != QStringLiteral("wad-lump")) { continue; }
					QByteArray existing;
					if (planned.readEntryBytes(path, &existing, error, 64 * 1024 * 1024) && detectIdTechImageFormat(path, existing) == IdTechImageFormat::DoomPatch) { namespaceId = QStringLiteral("global"); }
					break;
				}
			}
			return staging->addWadBytes(result.bytes, path, namespaceId, 0, replaceExisting, error);
		}
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Choose Quake miptexture for WAD2, or Doom flat/patch for IWAD/PWAD. WAD3 encoding is unavailable."));
	}
	const auto normalized = normalizePackageVirtualPath(path, false);
	if (!normalized.isSafe() || QFileInfo(normalized.normalizedPath).suffix().compare(textureExportSuffix(options.format), Qt::CaseInsensitive) != 0) {
		return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Choose a safe package-relative path with the selected export profile's extension."));
	}
	auto next = *staging;
	if (!next.addBytes(result.bytes, normalized.normalizedPath, error, replaceExisting ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block)
		|| !next.summary().canSave) { return fail(error, QCoreApplication::translate("VibeStudioTextureExport", "Resolve package conflicts or enable replacement before staging this texture.")); }
	*staging = std::move(next); return true;
}

} // namespace vibestudio
