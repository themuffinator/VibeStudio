#include "core/idtech_image.h"
#include "core/extra_image.h"
#include <QHash>

#include "core/package_archive.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vibestudio {

namespace {

// Packed streams and overlapping frame/mip offsets do not bound decoded storage.
// Charge every allocated surface, even when several refer to the same payload.
constexpr qsizetype kMaxDecodedPixels = 4096 * 4096;
constexpr qint64 kMaxTotalDecodedPixels = 32 * 1024 * 1024;
constexpr qint64 kMaxImagePayloadBytes = 64 * 1024 * 1024;
constexpr int kMaxStoredSpriteFrames = 4096;

struct DecodeBudget {
	IdTechImageDecodeContext limits;
	qint64 used = 0;
	bool cancelled = false;

	bool checkpoint()
	{
		cancelled = cancelled || (limits.isCancelled && limits.isCancelled());
		return !cancelled;
	}

	bool validSize(int width, int height, QString* error) const
	{
		if (width <= 0 || height <= 0 || width > limits.maximumDimension || height > limits.maximumDimension ||
			qint64(width) * height > limits.maximumImagePixels) {
			*error = QCoreApplication::translate("VibeStudioIdTechImage", "Image dimensions exceed the decode limit.");
			return false;
		}
		return true;
	}
	bool consume(int width, int height, QString* error)
	{
		if (!checkpoint()) { return false; }
		if (!validSize(width, height, error)) { return false; }
		const qint64 pixels = qint64(width) * height;
		if (pixels > limits.maximumTotalPixels - used) {
			*error = QCoreApplication::translate("VibeStudioIdTechImage", "Images, mip levels, and sprite frames exceed the aggregate decode limit.");
			return false;
		}
		used += pixels;
		return true;
	}
};

// ---------------------------------------------------------------------------
// Little-endian readers. Every idTech container covered by this module stores
// its scalars little-endian regardless of host byte order.
// ---------------------------------------------------------------------------

quint8 readU8(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset >= data.size()) {
		return 0;
	}
	return static_cast<quint8>(data.at(offset));
}

quint16 readLe16(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || data.size() < 2 || offset > data.size() - 2) {
		return 0;
	}
	// at() avoids GCC 13's false array-bounds inference through Qt 6.4's
	// constData() empty-array sentinel after the checked, nonempty range above.
	return static_cast<quint16>(static_cast<quint8>(data.at(offset)) |
		(static_cast<quint8>(data.at(offset + 1)) << 8));
}

qint16 readLe16Signed(const QByteArray& data, qsizetype offset)
{
	return static_cast<qint16>(readLe16(data, offset));
}

quint32 readLe32(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 4 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<quint32>(bytes[0]) | (static_cast<quint32>(bytes[1]) << 8)
		| (static_cast<quint32>(bytes[2]) << 16) | (static_cast<quint32>(bytes[3]) << 24);
}

qint32 readLe32Signed(const QByteArray& data, qsizetype offset)
{
	return static_cast<qint32>(readLe32(data, offset));
}

float readLeFloat(const QByteArray& data, qsizetype offset)
{
	const quint32 raw = readLe32(data, offset);
	float value = 0.0f;
	std::memcpy(&value, &raw, sizeof(value));
	return value;
}

QString fixedLatin1(const QByteArray& data, qsizetype offset, qsizetype length)
{
	if (offset < 0 || offset >= data.size() || length <= 0) {
		return {};
	}
	const qsizetype available = std::min(length, data.size() - offset);
	qsizetype size = 0;
	while (size < available && data.at(offset + size) != '\0') {
		++size;
	}
	return QString::fromLatin1(data.constData() + offset, size).trimmed();
}

QString pathFileName(const QString& virtualPath)
{
	const qsizetype slash = virtualPath.lastIndexOf('/');
	const qsizetype backslash = virtualPath.lastIndexOf('\\');
	const qsizetype cut = std::max(slash, backslash);
	return cut < 0 ? virtualPath : virtualPath.mid(cut + 1);
}

QString pathSuffix(const QString& virtualPath)
{
	const QString name = pathFileName(virtualPath);
	const qsizetype dot = name.lastIndexOf('.');
	if (dot <= 0) {
		return {};
	}
	return name.mid(dot + 1).toLower();
}

QString pathBaseName(const QString& virtualPath)
{
	const QString name = pathFileName(virtualPath);
	const qsizetype dot = name.lastIndexOf('.');
	if (dot <= 0) {
		return name;
	}
	return name.left(dot);
}

// ---------------------------------------------------------------------------
// Palette descriptors and generated (license-clean) palettes.
// ---------------------------------------------------------------------------

QString normalizedPaletteId(const QString& id)
{
	QString value = id.trimmed().toLower();
	value.replace('_', '-');
	if (value.isEmpty()) {
		return QStringLiteral("generic");
	}
	if (value == QStringLiteral("idtech1") || value == QStringLiteral("doom2") || value == QStringLiteral("doom-2")) {
		return QStringLiteral("doom");
	}
	if (value == QStringLiteral("idtech2") || value == QStringLiteral("quake1") || value == QStringLiteral("quake-1")) {
		return QStringLiteral("quake");
	}
	if (value == QStringLiteral("quake-2") || value == QStringLiteral("q2")) {
		return QStringLiteral("quake2");
	}
	return value;
}

struct GeneratedPaletteRecipe {
	const char* id;
	const char* displayName;
	const char* engineFamily;
	const char* description;
	int transparentIndex;
	int fullbrightStartIndex;
	double hueOffset;
	double saturationBase;
};

// The recipes below are invented for VibeStudio. They produce readable ramps
// with a family-specific tint so previews are useful before the user's own
// game palette is available. They intentionally do not reproduce any shipped
// commercial palette.
const GeneratedPaletteRecipe kPaletteRecipes[] = {
	{"quake", QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Quake (generated)"), "idtech2",
		QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Generated stand-in ramp palette for Quake-family indexed art."), 255, 224, 26.0, 0.56},
	{"quake2", QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Quake II (generated)"), "idtech2",
		QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Generated stand-in ramp palette for Quake II indexed art."), 255, 240, 198.0, 0.48},
	{"doom", QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Doom (generated)"), "idtech1",
		QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Generated stand-in ramp palette for Doom-family indexed art."), -1, -1, 4.0, 0.70},
	{"heretic", QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Heretic (generated)"), "idtech1",
		QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Generated stand-in ramp palette for Heretic indexed art."), -1, -1, 42.0, 0.62},
	{"hexen", QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Hexen (generated)"), "idtech1",
		QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Generated stand-in ramp palette for Hexen indexed art."), -1, -1, 282.0, 0.52},
	{"generic", QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Generic (generated)"), "generic",
		QT_TRANSLATE_NOOP("VibeStudioIdTechImage", "Generated neutral ramp palette used when no engine family is known."), 255, -1, 122.0, 0.64},
};

const GeneratedPaletteRecipe& recipeForId(const QString& paletteId)
{
	const QString id = normalizedPaletteId(paletteId);
	for (const GeneratedPaletteRecipe& recipe : kPaletteRecipes) {
		if (id == QLatin1String(recipe.id)) {
			return recipe;
		}
	}
	return kPaletteRecipes[std::size(kPaletteRecipes) - 1];
}

void hsvToRgb(double hue, double saturation, double value, int* red, int* green, int* blue)
{
	hue = std::fmod(hue, 360.0);
	if (hue < 0.0) {
		hue += 360.0;
	}
	saturation = std::clamp(saturation, 0.0, 1.0);
	value = std::clamp(value, 0.0, 1.0);

	const double chroma = value * saturation;
	const double sector = hue / 60.0;
	const double x = chroma * (1.0 - std::fabs(std::fmod(sector, 2.0) - 1.0));
	double r = 0.0;
	double g = 0.0;
	double b = 0.0;
	if (sector < 1.0) {
		r = chroma;
		g = x;
	} else if (sector < 2.0) {
		r = x;
		g = chroma;
	} else if (sector < 3.0) {
		g = chroma;
		b = x;
	} else if (sector < 4.0) {
		g = x;
		b = chroma;
	} else if (sector < 5.0) {
		r = x;
		b = chroma;
	} else {
		r = chroma;
		b = x;
	}
	const double m = value - chroma;
	*red = static_cast<int>(std::lround(std::clamp(r + m, 0.0, 1.0) * 255.0));
	*green = static_cast<int>(std::lround(std::clamp(g + m, 0.0, 1.0) * 255.0));
	*blue = static_cast<int>(std::lround(std::clamp(b + m, 0.0, 1.0) * 255.0));
}

// Deterministic de-duplication: quantization and the swatch round-trip rely on
// every generated entry being a distinct colour.
QRgb uniqueOpaqueColor(QSet<QRgb>* used, int red, int green, int blue)
{
	int r = std::clamp(red, 0, 255);
	int g = std::clamp(green, 0, 255);
	int b = std::clamp(blue, 0, 255);
	for (int attempt = 0; attempt < 8192; ++attempt) {
		const QRgb candidate = qRgb(r, g, b);
		if (!used->contains(candidate)) {
			used->insert(candidate);
			return candidate;
		}
		if (b < 255) {
			++b;
			continue;
		}
		b = 0;
		if (g < 255) {
			++g;
			continue;
		}
		g = 0;
		r = r < 255 ? r + 1 : 0;
	}
	return qRgb(r, g, b);
}

// ---------------------------------------------------------------------------
// Indexed helpers.
// ---------------------------------------------------------------------------

QVector<QRgb> paletteColorTable(const IdTechPalette& palette, bool applyTransparentIndex)
{
	QVector<QRgb> table = palette.colors;
	while (table.size() < 256) {
		const int index = static_cast<int>(table.size());
		table.push_back(qRgb(index, index, index));
	}
	if (table.size() > 256) {
		table.resize(256);
	}
	const int transparent = palette.transparentIndex;
	for (int index = 0; index < table.size(); ++index) {
		if (index == transparent && applyTransparentIndex) {
			table[index] = table.at(index) & 0x00ffffffu;
		} else {
			table[index] = table.at(index) | 0xff000000u;
		}
	}
	return table;
}

QImage makeIndexedImage(const uchar* pixels, qsizetype available, int width, int height, const QVector<QRgb>& table, DecodeBudget& budget)
{
	if (width <= 0 || height <= 0) {
		return {};
	}
	if (available < static_cast<qsizetype>(width) * height) {
		return {};
	}
	QImage image(width, height, QImage::Format_Indexed8);
	if (image.isNull()) {
		return {};
	}
	image.setColorTable(table);
	for (int y = 0; y < height; ++y) {
		if (!budget.checkpoint()) { return {}; }
		uchar* line = image.scanLine(y);
		std::memcpy(line, pixels + static_cast<qsizetype>(y) * width, static_cast<size_t>(width));
	}
	return image;
}

bool indexedImageUsesIndex(const QImage& image, int index, DecodeBudget& budget)
{
	if (image.isNull() || index < 0 || image.format() != QImage::Format_Indexed8) {
		return false;
	}
	for (int y = 0; y < image.height(); ++y) {
		if (!budget.checkpoint()) { return false; }
		const uchar* line = image.constScanLine(y);
		for (int x = 0; x < image.width(); ++x) {
			if (line[x] == static_cast<uchar>(index)) {
				return true;
			}
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Doom picture format.
// Reference: the Unofficial Doom Specs and https://doomwiki.org/wiki/Picture_format
// Header: int16 width, height, leftoffset, topoffset; then `width` uint32
// column offsets; then per-column posts of {topdelta, length, pad, pixels[],
// pad} terminated by topdelta 0xFF.
// ---------------------------------------------------------------------------

constexpr int kDoomPatchMaxDimension = 4096;
// Up to one one-pixel post per row plus bounded tall-post origin advances.
constexpr int kDoomPatchMaxPosts = kDoomPatchMaxDimension * 2;

bool walkDoomPatchColumn(const QByteArray& bytes, qsizetype start, int* postCountOut)
{
	const qsizetype size = bytes.size();
	qsizetype cursor = start;
	int posts = 0;
	while (true) {
		if (cursor < 0 || cursor >= size) {
			return false;
		}
		const quint8 topDelta = readU8(bytes, cursor);
		if (topDelta == 0xff) {
			break;
		}
		if (cursor + 4 > size) {
			return false;
		}
		const int length = static_cast<int>(readU8(bytes, cursor + 1));
		cursor += 3;
		if (cursor + length + 1 > size) {
			return false;
		}
		cursor += length + 1;
		if (++posts > kDoomPatchMaxPosts) {
			return false;
		}
	}
	if (postCountOut) {
		*postCountOut = posts;
	}
	return true;
}

bool doomPatchLooksValid(const QByteArray& bytes)
{
	const qsizetype size = bytes.size();
	if (size < 12) {
		return false;
	}
	const int width = readLe16Signed(bytes, 0);
	const int height = readLe16Signed(bytes, 2);
	if (width <= 0 || height <= 0 || width > kDoomPatchMaxDimension || height > kDoomPatchMaxDimension) {
		return false;
	}
	const qsizetype headerEnd = 8 + static_cast<qsizetype>(width) * 4;
	if (size < headerEnd + 1) {
		return false;
	}
	bool sawPixels = false;
	for (int column = 0; column < width; ++column) {
		const quint32 offset = readLe32(bytes, 8 + static_cast<qsizetype>(column) * 4);
		if (offset < static_cast<quint32>(headerEnd) || static_cast<qsizetype>(offset) >= size) {
			return false;
		}
		// Fully validate a bounded prefix of columns; offsets alone are checked
		// for the rest so very wide patches stay cheap to sniff.
		if (column < 16 || column == width - 1) {
			int posts = 0;
			if (!walkDoomPatchColumn(bytes, static_cast<qsizetype>(offset), &posts)) {
				return false;
			}
			if (posts > 0) {
				sawPixels = true;
			}
		}
	}
	return sawPixels;
}

// ---------------------------------------------------------------------------
// Quake .lmp, WAD2/WAD3 miptex, Quake II .wal.
// References: the Quake Specifications (Olivier Montanuy), the released id
// Software Quake/Quake II tool sources, and the Valve Developer Community WAD3
// page (https://developer.valvesoftware.com/wiki/WAD).
// ---------------------------------------------------------------------------

constexpr qsizetype kMipTexHeaderSize = 40;
constexpr qsizetype kWalHeaderSize = 100;

bool quakeLumpLooksValid(const QByteArray& bytes, int* widthOut = nullptr, int* heightOut = nullptr)
{
	if (bytes.size() < 9) {
		return false;
	}
	const quint32 width = readLe32(bytes, 0);
	const quint32 height = readLe32(bytes, 4);
	if (width == 0 || height == 0 || width > 8192 || height > 8192) {
		return false;
	}
	const qsizetype expected = 8 + static_cast<qsizetype>(width) * static_cast<qsizetype>(height);
	if (expected != bytes.size()) {
		return false;
	}
	if (widthOut) {
		*widthOut = static_cast<int>(width);
	}
	if (heightOut) {
		*heightOut = static_cast<int>(height);
	}
	return true;
}

struct MipHeader {
	int width = 0;
	int height = 0;
	quint32 offsets[4] = {0, 0, 0, 0};
	qsizetype lastMipEnd = 0;
};

bool parseMipHeader(const QByteArray& bytes, qsizetype headerSize, qsizetype offsetsAt, MipHeader* out)
{
	if (bytes.size() < headerSize) {
		return false;
	}
	const quint32 width = readLe32(bytes, offsetsAt - 8);
	const quint32 height = readLe32(bytes, offsetsAt - 4);
	if (width == 0 || height == 0 || width > 8192 || height > 8192) {
		return false;
	}
	if ((width % 8) != 0 || (height % 8) != 0) {
		return false;
	}
	MipHeader header;
	header.width = static_cast<int>(width);
	header.height = static_cast<int>(height);
	for (int level = 0; level < 4; ++level) {
		header.offsets[level] = readLe32(bytes, offsetsAt + static_cast<qsizetype>(level) * 4);
	}
	if (header.offsets[0] < static_cast<quint32>(headerSize)) {
		return false;
	}
	qsizetype end = 0;
	for (int level = 0; level < 4; ++level) {
		const qsizetype levelWidth = header.width >> level;
		const qsizetype levelHeight = header.height >> level;
		if (levelWidth <= 0 || levelHeight <= 0) {
			return false;
		}
		const qsizetype start = static_cast<qsizetype>(header.offsets[level]);
		const qsizetype stop = start + levelWidth * levelHeight;
		if (start < headerSize || stop > bytes.size()) {
			return false;
		}
		if (level > 0 && header.offsets[level] <= header.offsets[level - 1]) {
			return false;
		}
		end = std::max(end, stop);
	}
	header.lastMipEnd = end;
	*out = header;
	return true;
}

bool mipTextureLooksValid(const QByteArray& bytes, MipHeader* out = nullptr)
{
	MipHeader header;
	if (!parseMipHeader(bytes, kMipTexHeaderSize, 24, &header)) {
		return false;
	}
	if (out) {
		*out = header;
	}
	return true;
}

bool walLooksValid(const QByteArray& bytes, MipHeader* out = nullptr)
{
	MipHeader header;
	if (!parseMipHeader(bytes, kWalHeaderSize, 40, &header)) {
		return false;
	}
	if (out) {
		*out = header;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Quake II / Heretic II .m8 and .m32 extended mip textures.
//
// Layout reference: the `m8tex_t` and `m32tex_t` structures published in the
// GPL Quake II engine sources, e.g.
// https://github.com/yquake2/yquake2/blob/master/src/common/header/files.h
// Implemented from the published field layout; no code was copied.
//
// .m8  (1040-byte header, then 8-bit indexed pixels):
//   unsigned version (2); char name[32]; unsigned width[16];
//   unsigned height[16]; unsigned offsets[16]; char animname[32];
//   byte palette[768]; int flags; int contents; int value.
// .m32 (968-byte header, then RGBA pixels):
//   int version (4); char name[128]; char altname[128]; char animname[128];
//   char damagename[128]; unsigned width[16]; unsigned height[16];
//   unsigned offsets[16]; int flags; int contents; int value;
//   float scale_x; float scale_y; int mip_scale; then the detail-texture
//   fields (char dt_name[128], five floats, two ints) and int unused[20].
//
// Both store sixteen mip entries; the chain ends at the first entry with a
// zero width, height or offset. Every one of the sixteen is still bounds
// checked so a malformed tail cannot slip past.
// ---------------------------------------------------------------------------

constexpr int kExtendedMipLevels = 16;
constexpr qsizetype kM8HeaderSize = 1040;
constexpr qsizetype kM32HeaderSize = 968;
constexpr qsizetype kM8WidthsAt = 36;
constexpr qsizetype kM8PaletteAt = 260;
constexpr qsizetype kM32WidthsAt = 516;
constexpr quint32 kM8Version = 2;
constexpr quint32 kM32Version = 4;

struct ExtendedMipHeader {
	int levelCount = 0;
	int widths[kExtendedMipLevels] = {};
	int heights[kExtendedMipLevels] = {};
	quint32 offsets[kExtendedMipLevels] = {};
};

// `widthsAt` points at width[0]; height[0] and offsets[0] follow 64 and 128
// bytes later. `bytesPerPixel` is 1 for .m8 and 4 for .m32.
bool parseExtendedMipHeader(const QByteArray& bytes, qsizetype headerSize, qsizetype widthsAt, int bytesPerPixel, ExtendedMipHeader* out)
{
	if (bytes.size() < headerSize) {
		return false;
	}
	ExtendedMipHeader header;
	bool chainEnded = false;
	for (int level = 0; level < kExtendedMipLevels; ++level) {
		const qsizetype entry = widthsAt + static_cast<qsizetype>(level) * 4;
		const quint32 width = readLe32(bytes, entry);
		const quint32 height = readLe32(bytes, entry + 64);
		const quint32 offset = readLe32(bytes, entry + 128);
		if (width == 0 || height == 0 || offset == 0) {
			chainEnded = true;
			continue;
		}
		if (width > 65535u || height > 65535u) {
			return false;
		}
		const qsizetype pixels = static_cast<qsizetype>(width) * static_cast<qsizetype>(height);
		if (pixels > kMaxDecodedPixels) {
			return false;
		}
		const qsizetype start = static_cast<qsizetype>(offset);
		if (start < headerSize) {
			return false;
		}
		// `pixels` is capped above, so this product cannot overflow qsizetype.
		if (start + pixels * bytesPerPixel > bytes.size()) {
			return false;
		}
		if (chainEnded) {
			// A populated level after an empty one: the chain is inconsistent.
			return false;
		}
		header.widths[level] = static_cast<int>(width);
		header.heights[level] = static_cast<int>(height);
		header.offsets[level] = offset;
		header.levelCount = level + 1;
	}
	if (header.levelCount == 0) {
		return false;
	}
	*out = header;
	return true;
}

bool m8LooksValid(const QByteArray& bytes, ExtendedMipHeader* out = nullptr)
{
	if (bytes.size() < kM8HeaderSize || readLe32(bytes, 0) != kM8Version) {
		return false;
	}
	ExtendedMipHeader header;
	if (!parseExtendedMipHeader(bytes, kM8HeaderSize, kM8WidthsAt, 1, &header)) {
		return false;
	}
	if (out) {
		*out = header;
	}
	return true;
}

bool m32LooksValid(const QByteArray& bytes, ExtendedMipHeader* out = nullptr)
{
	if (bytes.size() < kM32HeaderSize || readLe32(bytes, 0) != kM32Version) {
		return false;
	}
	ExtendedMipHeader header;
	if (!parseExtendedMipHeader(bytes, kM32HeaderSize, kM32WidthsAt, 4, &header)) {
		return false;
	}
	if (out) {
		*out = header;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Quake II .sp2 sprite container.
//
// Layout reference: the `dsprite_t` / `dsprframe_t` structures in the GPL
// Quake II engine sources (see the files.h link above):
//   int ident ("IDS2"); int version (2); int numframes;
//   then per frame: int width; int height; int origin_x; int origin_y;
//   char name[64] naming an external image (classically a .pcx).
// The container carries no pixels of its own.
// ---------------------------------------------------------------------------

constexpr qsizetype kSp2HeaderSize = 12;
constexpr qsizetype kSp2FrameSize = 80;
constexpr qsizetype kSp2NameSize = 64;
constexpr qint32 kSp2MaxFrames = 4096;

bool sp2LooksValid(const QByteArray& bytes, int* frameCountOut = nullptr)
{
	if (bytes.size() < kSp2HeaderSize) {
		return false;
	}
	if (std::memcmp(bytes.constData(), "IDS2", 4) != 0) {
		return false;
	}
	if (readLe32Signed(bytes, 4) != 2) {
		return false;
	}
	const qint32 frameCount = readLe32Signed(bytes, 8);
	if (frameCount < 0 || frameCount > kSp2MaxFrames) {
		return false;
	}
	if (kSp2HeaderSize + static_cast<qsizetype>(frameCount) * kSp2FrameSize > bytes.size()) {
		return false;
	}
	if (frameCountOut) {
		*frameCountOut = static_cast<int>(frameCount);
	}
	return true;
}

// ---------------------------------------------------------------------------
// PCX. Reference: the ZSoft PCX File Format Technical Reference Manual.
// ---------------------------------------------------------------------------

constexpr qsizetype kPcxHeaderSize = 128;

struct PcxHeader {
	int version = 0;
	int encoding = 0;
	int bitsPerPixel = 0;
	int width = 0;
	int height = 0;
	int planes = 0;
	int bytesPerLine = 0;
};

bool parsePcxHeader(const QByteArray& bytes, PcxHeader* out)
{
	if (bytes.size() < kPcxHeaderSize + 1) {
		return false;
	}
	if (readU8(bytes, 0) != 0x0a) {
		return false;
	}
	PcxHeader header;
	header.version = static_cast<int>(readU8(bytes, 1));
	header.encoding = static_cast<int>(readU8(bytes, 2));
	header.bitsPerPixel = static_cast<int>(readU8(bytes, 3));
	const int xMin = static_cast<int>(readLe16(bytes, 4));
	const int yMin = static_cast<int>(readLe16(bytes, 6));
	const int xMax = static_cast<int>(readLe16(bytes, 8));
	const int yMax = static_cast<int>(readLe16(bytes, 10));
	header.planes = static_cast<int>(readU8(bytes, 65));
	header.bytesPerLine = static_cast<int>(readLe16(bytes, 66));

	if (header.version != 0 && header.version != 2 && header.version != 3 && header.version != 4 && header.version != 5) {
		return false;
	}
	if (header.encoding != 0 && header.encoding != 1) {
		return false;
	}
	if (header.bitsPerPixel != 1 && header.bitsPerPixel != 2 && header.bitsPerPixel != 4 && header.bitsPerPixel != 8) {
		return false;
	}
	if (header.planes < 1 || header.planes > 4) {
		return false;
	}
	if (xMax < xMin || yMax < yMin) {
		return false;
	}
	header.width = xMax - xMin + 1;
	header.height = yMax - yMin + 1;
	if (header.width <= 0 || header.height <= 0 || header.width > 16384 || header.height > 16384) {
		return false;
	}
	if (static_cast<qsizetype>(header.width) * header.height > kMaxDecodedPixels) {
		return false;
	}
	const int minimumBytesPerLine = (header.width * header.bitsPerPixel + 7) / 8;
	if (header.bytesPerLine < minimumBytesPerLine) {
		return false;
	}
	*out = header;
	return true;
}

bool pcxHasTailPalette(const QByteArray& bytes, const PcxHeader& header)
{
	if (header.bitsPerPixel != 8 || header.planes != 1) {
		return false;
	}
	if (bytes.size() < kPcxHeaderSize + 769) {
		return false;
	}
	return readU8(bytes, bytes.size() - 769) == 0x0c;
}

bool decodePcxScanlines(const QByteArray& bytes, qsizetype dataEnd, bool rle, qsizetype rowBytes, int height, QByteArray* out, DecodeBudget& budget)
{
	if (rowBytes <= 0 || height <= 0) {
		return false;
	}
	const qint64 total = qint64(rowBytes) * height;
	if (total <= 0 || total > kMaxImagePayloadBytes) {
		return false;
	}
	out->resize(total);
	auto* destination = reinterpret_cast<uchar*>(out->data());
	qsizetype written = 0;
	qsizetype cursor = kPcxHeaderSize;
	qsizetype nextCheckpoint = 0;
	while (written < total) {
		if (written >= nextCheckpoint) {
			if (!budget.checkpoint()) { return false; }
			nextCheckpoint = written + 16384;
		}
		if (cursor >= dataEnd) {
			return false;
		}
		const quint8 control = readU8(bytes, cursor++);
		if (rle && (control & 0xc0) == 0xc0) {
			const int count = control & 0x3f;
			if (cursor >= dataEnd) {
				return false;
			}
			const quint8 value = readU8(bytes, cursor++);
			for (int repeat = 0; repeat < count && written < total; ++repeat) {
				destination[written++] = value;
			}
		} else {
			destination[written++] = control;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Targa. Reference: the Truevision TGA File Format Specification 2.0.
// ---------------------------------------------------------------------------

struct TargaHeader {
	int idLength = 0;
	int colorMapType = 0;
	int imageType = 0;
	int colorMapLength = 0;
	int colorMapEntrySize = 0;
	int width = 0;
	int height = 0;
	int pixelDepth = 0;
	int attributeBits = 0;
	bool rightToLeft = false;
	bool topToBottom = false;
	qsizetype colorMapOffset = 0;
	qsizetype dataOffset = 0;
	bool rle = false;
};

bool parseTargaHeader(const QByteArray& bytes, TargaHeader* out)
{
	if (bytes.size() < 18 + 1) {
		return false;
	}
	TargaHeader header;
	header.idLength = static_cast<int>(readU8(bytes, 0));
	header.colorMapType = static_cast<int>(readU8(bytes, 1));
	header.imageType = static_cast<int>(readU8(bytes, 2));
	header.colorMapLength = static_cast<int>(readLe16(bytes, 5));
	header.colorMapEntrySize = static_cast<int>(readU8(bytes, 7));
	header.width = static_cast<int>(readLe16(bytes, 12));
	header.height = static_cast<int>(readLe16(bytes, 14));
	header.pixelDepth = static_cast<int>(readU8(bytes, 16));
	const int descriptor = static_cast<int>(readU8(bytes, 17));
	header.attributeBits = descriptor & 0x0f;
	header.rightToLeft = (descriptor & 0x10) != 0;
	header.topToBottom = (descriptor & 0x20) != 0;

	if (header.colorMapType != 0 && header.colorMapType != 1) {
		return false;
	}
	switch (header.imageType) {
	case 1:
	case 2:
	case 3:
		header.rle = false;
		break;
	case 9:
	case 10:
	case 11:
		header.rle = true;
		break;
	default:
		return false;
	}
	const bool colorMapped = header.imageType == 1 || header.imageType == 9;
	if (colorMapped && header.colorMapType != 1) {
		return false;
	}
	if (header.pixelDepth != 8 && header.pixelDepth != 15 && header.pixelDepth != 16
		&& header.pixelDepth != 24 && header.pixelDepth != 32) {
		return false;
	}
	if (header.width <= 0 || header.height <= 0 || header.width > 16384 || header.height > 16384) {
		return false;
	}
	if (static_cast<qsizetype>(header.width) * header.height > kMaxDecodedPixels) {
		return false;
	}
	if ((header.imageType == 3 || header.imageType == 11) && header.pixelDepth != 8 && header.pixelDepth != 16) {
		return false;
	}
	if (colorMapped && header.pixelDepth != 8) {
		return false;
	}
	header.colorMapOffset = 18 + header.idLength;
	qsizetype colorMapBytes = 0;
	if (header.colorMapType == 1) {
		if (header.colorMapEntrySize != 15 && header.colorMapEntrySize != 16
			&& header.colorMapEntrySize != 24 && header.colorMapEntrySize != 32) {
			return false;
		}
		colorMapBytes = static_cast<qsizetype>(header.colorMapLength) * ((header.colorMapEntrySize + 7) / 8);
	}
	header.dataOffset = header.colorMapOffset + colorMapBytes;
	if (header.dataOffset >= bytes.size()) {
		return false;
	}
	*out = header;
	return true;
}

bool targaFooterPresent(const QByteArray& bytes)
{
	static const char kSignature[] = "TRUEVISION-XFILE.";
	const qsizetype length = static_cast<qsizetype>(sizeof(kSignature)) - 1;
	if (bytes.size() < 26) {
		return false;
	}
	return std::memcmp(bytes.constData() + bytes.size() - 18, kSignature, static_cast<size_t>(length)) == 0;
}

bool targaLooksValid(const QByteArray& bytes, TargaHeader* out = nullptr)
{
	TargaHeader header;
	if (!parseTargaHeader(bytes, &header)) {
		return false;
	}
	if (!header.rle) {
		const qsizetype pixelBytes = static_cast<qsizetype>(header.width) * header.height * ((header.pixelDepth + 7) / 8);
		const qsizetype available = bytes.size() - header.dataOffset;
		if (available < pixelBytes) {
			return false;
		}
	}
	if (out) {
		*out = header;
	}
	return true;
}

bool decodeTargaPixelStream(const QByteArray& bytes, const TargaHeader& header, QByteArray* out, DecodeBudget& budget)
{
	const int bytesPerPixel = (header.pixelDepth + 7) / 8;
	const qsizetype pixelCount = static_cast<qsizetype>(header.width) * header.height;
	const qsizetype total = pixelCount * bytesPerPixel;
	if (total <= 0 || total > (1 << 28)) {
		return false;
	}
	out->resize(total);
	auto* destination = reinterpret_cast<uchar*>(out->data());
	qsizetype cursor = header.dataOffset;
	const qsizetype size = bytes.size();
	if (!header.rle) {
		if (cursor + total > size) {
			return false;
		}
		std::memcpy(destination, bytes.constData() + cursor, static_cast<size_t>(total));
		return true;
	}

	qsizetype written = 0;
	qsizetype nextCheckpoint = 0;
	while (written < total) {
		if (written >= nextCheckpoint) {
			if (!budget.checkpoint()) { return false; }
			nextCheckpoint = written + 16384;
		}
		if (cursor >= size) {
			return false;
		}
		const quint8 packet = readU8(bytes, cursor++);
		const int count = (packet & 0x7f) + 1;
		if ((packet & 0x80) != 0) {
			if (cursor + bytesPerPixel > size) {
				return false;
			}
			for (int repeat = 0; repeat < count && written < total; ++repeat) {
				std::memcpy(destination + written, bytes.constData() + cursor, static_cast<size_t>(bytesPerPixel));
				written += bytesPerPixel;
			}
			cursor += bytesPerPixel;
		} else {
			const qsizetype runBytes = static_cast<qsizetype>(count) * bytesPerPixel;
			if (cursor + runBytes > size) {
				return false;
			}
			const qsizetype copyBytes = std::min(runBytes, total - written);
			std::memcpy(destination + written, bytes.constData() + cursor, static_cast<size_t>(copyBytes));
			written += copyBytes;
			cursor += runBytes;
		}
	}
	return true;
}

QRgb targaColorFromBytes(const uchar* pixel, int depth, bool honourAlpha)
{
	switch (depth) {
	case 8:
		return qRgb(pixel[0], pixel[0], pixel[0]);
	case 15:
	case 16: {
		const quint16 value = static_cast<quint16>(pixel[0] | (pixel[1] << 8));
		const int r = ((value >> 10) & 0x1f) * 255 / 31;
		const int g = ((value >> 5) & 0x1f) * 255 / 31;
		const int b = (value & 0x1f) * 255 / 31;
		const int a = (depth == 16 && honourAlpha) ? (((value & 0x8000) != 0) ? 255 : 0) : 255;
		return qRgba(r, g, b, a);
	}
	case 24:
		return qRgb(pixel[2], pixel[1], pixel[0]);
	case 32:
		return qRgba(pixel[2], pixel[1], pixel[0], pixel[3]);
	default:
		break;
	}
	return qRgb(0, 0, 0);
}

// ---------------------------------------------------------------------------
// IDSP sprites, both dialects.
//
// Quake (version 1). Reference: the Quake Specifications sprite chapter and
// the released id Software Quake tool sources (spritegn.h):
//   int ident ("IDSP"); int version (1); int type; float boundingradius;
//   int width; int height; int numframes; float beamlength; int synctype.
//
// Half-Life (version 2). Reference: the Half-Life SDK sprite generator,
// https://github.com/ValveSoftware/halflife/blob/master/utils/sprgen/sprgen.c
// It inserts `int texFormat` after `type` and writes a `short` palette entry
// count plus that many RGB triplets directly after the header:
//   int ident ("IDSP"); int version (2); int type; int texFormat;
//   float boundingradius; int width; int height; int numframes;
//   float beamlength; int synctype; short paletteCount; rgb palette[count].
//
// The frame stream is identical in both: an int group marker per entry, then
// either one frame or a group header, an interval list and its frames, where a
// frame is {int origin_x, int origin_y, int width, int height, pixels}.
// ---------------------------------------------------------------------------

constexpr qsizetype kSpriteHeaderSize = 36;
constexpr qsizetype kHalfLifeSpriteHeaderSize = 40;

// Half-Life texFormat values, from sprgen.c / spritegn.h.
constexpr qint32 kSprNormal = 0;
constexpr qint32 kSprAdditive = 1;
constexpr qint32 kSprIndexAlpha = 2;
constexpr qint32 kSprAlphaTest = 3;

struct SpriteHeader {
	int version = 1;
	qint32 type = 0;
	qint32 texFormat = kSprNormal;
	float boundingRadius = 0.0f;
	int maxWidth = 0;
	int maxHeight = 0;
	int frameCount = 0;
	float beamLength = 0.0f;
	qint32 syncType = 0;
	int paletteCount = 0;
	qsizetype paletteAt = 0;   // 0 when the sprite carries no embedded palette
	qsizetype framesAt = 0;
};

bool spriteLooksValid(const QByteArray& bytes, SpriteHeader* out = nullptr)
{
	if (bytes.size() < kSpriteHeaderSize) {
		return false;
	}
	if (std::memcmp(bytes.constData(), "IDSP", 4) != 0) {
		return false;
	}
	const qint32 version = readLe32Signed(bytes, 4);
	if (version != 1 && version != 2) {
		return false;
	}
	SpriteHeader header;
	header.version = static_cast<int>(version);
	header.type = readLe32Signed(bytes, 8);
	if (version == 1) {
		header.boundingRadius = readLeFloat(bytes, 12);
		header.maxWidth = readLe32Signed(bytes, 16);
		header.maxHeight = readLe32Signed(bytes, 20);
		header.frameCount = readLe32Signed(bytes, 24);
		header.beamLength = readLeFloat(bytes, 28);
		header.syncType = readLe32Signed(bytes, 32);
		header.framesAt = kSpriteHeaderSize;
	} else {
		if (bytes.size() < kHalfLifeSpriteHeaderSize + 2) {
			return false;
		}
		header.texFormat = readLe32Signed(bytes, 12);
		header.boundingRadius = readLeFloat(bytes, 16);
		header.maxWidth = readLe32Signed(bytes, 20);
		header.maxHeight = readLe32Signed(bytes, 24);
		header.frameCount = readLe32Signed(bytes, 28);
		header.beamLength = readLeFloat(bytes, 32);
		header.syncType = readLe32Signed(bytes, 36);
		if (header.texFormat < kSprNormal || header.texFormat > kSprAlphaTest) {
			return false;
		}
		header.paletteCount = static_cast<int>(readLe16(bytes, kHalfLifeSpriteHeaderSize));
		if (header.paletteCount <= 0 || header.paletteCount > 256) {
			return false;
		}
		header.paletteAt = kHalfLifeSpriteHeaderSize + 2;
		if (header.paletteAt + static_cast<qsizetype>(header.paletteCount) * 3 > bytes.size()) {
			return false;
		}
		header.framesAt = header.paletteAt + static_cast<qsizetype>(header.paletteCount) * 3;
	}
	if (header.maxWidth <= 0 || header.maxHeight <= 0 || header.maxWidth > 8192 || header.maxHeight > 8192) {
		return false;
	}
	if (header.frameCount <= 0 || header.frameCount > 4096) {
		return false;
	}
	if (out) {
		*out = header;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Qt-native container sniffing (PNG/JPEG/GIF/BMP/TIFF/WebP).
// ---------------------------------------------------------------------------

bool qtNativeMagic(const QByteArray& bytes)
{
	if (bytes.size() < 8) {
		return false;
	}
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	if (std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
		return true;
	}
	if (data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) {
		return true;
	}
	if (std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0) {
		return true;
	}
	if (data[0] == 'B' && data[1] == 'M' && bytes.size() > 54) {
		return true;
	}
	if (std::memcmp(data, "II\x2a\x00", 4) == 0 || std::memcmp(data, "MM\x00\x2a", 4) == 0) {
		return true;
	}
	if (bytes.size() >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0) {
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Context helpers used only to break ties between equally valid candidates.
// ---------------------------------------------------------------------------

bool looksLikeFlatContext(const QString& virtualPath)
{
	const QString lower = virtualPath.toLower();
	const QString suffix = pathSuffix(virtualPath);
	if (lower.contains(QStringLiteral("flat"))) {
		return true;
	}
	return suffix.isEmpty() || suffix == QStringLiteral("lmp") || suffix == QStringLiteral("flat");
}

bool looksLikeStrongFlatContext(const QString& virtualPath)
{
	const QString lower = virtualPath.toLower();
	return lower.startsWith(QStringLiteral("flats/")) || lower.contains(QStringLiteral("/flats/"))
		|| pathSuffix(virtualPath) == QStringLiteral("flat");
}

bool nameHintsPalette(const QString& virtualPath)
{
	const QString base = pathBaseName(virtualPath).toLower();
	return base == QStringLiteral("playpal") || base == QStringLiteral("palette") || base.contains(QStringLiteral("palette"));
}

bool nameHintsColormap(const QString& virtualPath)
{
	const QString base = pathBaseName(virtualPath).toLower();
	return base.contains(QStringLiteral("colormap"));
}

bool isTargaSuffix(const QString& suffix)
{
	return suffix == QStringLiteral("tga") || suffix == QStringLiteral("vda")
		|| suffix == QStringLiteral("icb") || suffix == QStringLiteral("vst");
}

// ---------------------------------------------------------------------------
// Decoders.
// ---------------------------------------------------------------------------

void applyFormatLabels(IdTechImageDecodeResult* result)
{
	result->formatId = idTechImageFormatId(result->format);
	result->formatName = idTechImageFormatDisplayName(result->format);
}

void applyPaletteLabels(IdTechImageDecodeResult* result, const IdTechPalette& palette)
{
	result->paletted = true;
	result->paletteId = palette.id;
	result->paletteGenerated = palette.generated;
}

bool decodeDoomPatch(const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	if (!doomPatchLooksValid(bytes)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Doom picture header does not validate.");
		return false;
	}
	const int width = readLe16Signed(bytes, 0);
	const int height = readLe16Signed(bytes, 2);
	if (!budget.consume(width, height, &result->error)) { return false; }
	result->width = width;
	result->height = height;
	result->leftOffset = readLe16Signed(bytes, 4);
	result->topOffset = readLe16Signed(bytes, 6);

	QImage image(width, height, QImage::Format_ARGB32);
	if (image.isNull()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded picture.");
		return false;
	}
	image.fill(Qt::transparent);

	const QVector<QRgb> table = paletteColorTable(palette, false);
	const qsizetype size = bytes.size();
	bool sawTransparency = false;
	bool clamped = false;
	qint64 workBytes = 0;
	for (int column = 0; column < width; ++column) {
		if (!budget.checkpoint()) { return false; }
		qsizetype cursor = static_cast<qsizetype>(readLe32(bytes, 8 + static_cast<qsizetype>(column) * 4));
		int lastTopDelta = -1;
		int posts = 0;
		bool terminated = false;
		while (cursor >= 0 && cursor < size) {
			if ((posts % 64) == 0 && !budget.checkpoint()) { return false; }
			const int topDelta = static_cast<int>(readU8(bytes, cursor));
			if (topDelta == 0xff) {
				terminated = true;
				break;
			}
			if (cursor + 4 > size) {
				clamped = true;
				break;
			}
			const int length = static_cast<int>(readU8(bytes, cursor + 1));
			workBytes += length + 4;
			if (workBytes > kMaxImagePayloadBytes) {
				result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Doom picture column references exceed the 64 MiB decode work limit.");
				return false;
			}
			// "DeepSea" tall-patch convention: a topdelta that does not advance
			// is treated as relative to the previous post's start.
			int top = topDelta;
			if (lastTopDelta >= 0 && topDelta <= lastTopDelta) {
				top = lastTopDelta + topDelta;
			}
			lastTopDelta = top;
			cursor += 3;
			if (cursor + length + 1 > size) {
				clamped = true;
				break;
			}
			if (top < 0 || top + length > height) { clamped = true; }
			for (int row = std::max(0, -top); row < std::min(length, height - top); ++row) {
				const int y = top + row;
				const quint8 index = readU8(bytes, cursor + row);
				auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
				line[column] = table.at(index) | 0xff000000u;
			}
			cursor += length + 1;
			if (++posts > kDoomPatchMaxPosts) {
				clamped = true;
				break;
			}
		}
		if (!terminated) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "A Doom picture column is truncated or exceeds the post limit.");
			return false;
		}
	}

	for (int y = 0; y < height && !sawTransparency; ++y) {
		if (!budget.checkpoint()) { return false; }
		const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < width; ++x) {
			if (qAlpha(line[x]) == 0) {
				sawTransparency = true;
				break;
			}
		}
	}

	result->image = image;
	result->hasTransparency = sawTransparency;
	applyPaletteLabels(result, palette);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Doom picture format (column posts with transparent gaps).");
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Offsets: left %1, top %2").arg(result->leftOffset).arg(result->topOffset);
	if (clamped) {
		result->warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Some picture posts fell outside the declared bounds and were clipped.");
	}
	result->decoded = true;
	return true;
}

bool decodeDoomFlat(const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	int width = 0;
	int height = 0;
	switch (bytes.size()) {
	case 4096:
		width = 64;
		height = 64;
		break;
	case 4160:
		width = 64;
		height = 65;
		break;
	case 16384:
		width = 128;
		height = 128;
		break;
	default:
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Flat lumps must be exactly 4096, 4160, or 16384 bytes.");
		return false;
	}

	const QVector<QRgb> table = paletteColorTable(palette, false);
	if (!budget.consume(width, height, &result->error)) { return false; }
	const QImage image = makeIndexedImage(reinterpret_cast<const uchar*>(bytes.constData()), bytes.size(), width, height, table, budget);
	if (image.isNull()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded flat.");
		return false;
	}
	result->image = image;
	result->width = width;
	result->height = height;
	applyPaletteLabels(result, palette);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Raw indexed flat, %1x%2.").arg(width).arg(height);
	result->decoded = true;
	return true;
}

bool decodeQuakeLump(const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	int width = 0;
	int height = 0;
	if (!quakeLumpLooksValid(bytes, &width, &height)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake .lmp header does not match the payload size.");
		return false;
	}
	const QVector<QRgb> table = paletteColorTable(palette, false);
	if (!budget.consume(width, height, &result->error)) { return false; }
	const QImage image = makeIndexedImage(reinterpret_cast<const uchar*>(bytes.constData()) + 8, bytes.size() - 8, width, height, table, budget);
	if (image.isNull()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded lump.");
		return false;
	}
	result->image = image;
	result->width = width;
	result->height = height;
	applyPaletteLabels(result, palette);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Quake .lmp picture, %1x%2.").arg(width).arg(height);
	result->decoded = true;
	return true;
}

bool parseEmbeddedWad3Palette(const QByteArray& bytes, const MipHeader& header, IdTechPalette* palette)
{
	// WAD3 (Half-Life) appends `uint16 paletteSize` followed by paletteSize RGB
	// triplets after the last mip level.
	for (qsizetype padding = 0; padding <= 2; ++padding) {
		const qsizetype tail = header.lastMipEnd + padding;
		if (tail + 2 > bytes.size()) {
			return false;
		}
		const int count = static_cast<int>(readLe16(bytes, tail));
		if (count != 256) {
			continue;
		}
		if (tail + 2 + 768 > bytes.size()) {
			continue;
		}
		QVector<QRgb> colors;
		colors.reserve(256);
		for (int index = 0; index < 256; ++index) {
			const qsizetype at = tail + 2 + static_cast<qsizetype>(index) * 3;
			colors.push_back(qRgb(readU8(bytes, at), readU8(bytes, at + 1), readU8(bytes, at + 2)));
		}
		palette->colors = colors;
		palette->generated = false;
		return true;
	}
	return false;
}

bool decodeMipTexture(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	MipHeader header;
	if (!mipTextureLooksValid(bytes, &header)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "WAD2/WAD3 miptex header does not validate.");
		return false;
	}
	QString name = fixedLatin1(bytes, 0, 16);
	if (name.isEmpty()) {
		name = pathBaseName(virtualPath);
	}

	IdTechPalette effective = palette;
	bool embedded = false;
	IdTechPalette candidate = palette;
	if (parseEmbeddedWad3Palette(bytes, header, &candidate)) {
		effective = candidate;
		effective.id = QStringLiteral("wad3-embedded");
		effective.displayName = QCoreApplication::translate("VibeStudioIdTechImage", "Embedded WAD3 palette");
		effective.sourceDescription = QCoreApplication::translate("VibeStudioIdTechImage", "256-entry palette stored in the texture lump");
		effective.generated = false;
		// Quake/Half-Life "fence" textures mark index 255 as the cut-out colour;
		// those textures are named with a leading '{'.
		effective.transparentIndex = name.startsWith('{') ? 255 : -1;
		effective.fullbrightStartIndex = -1;
		embedded = true;
	}

	const bool applyTransparent = effective.transparentIndex >= 0 && name.startsWith('{');
	const QVector<QRgb> table = paletteColorTable(effective, applyTransparent);

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	for (int level = 0; level < 4; ++level) {
		const int levelWidth = header.width >> level;
		const int levelHeight = header.height >> level;
		const qsizetype start = static_cast<qsizetype>(header.offsets[level]);
		if (!budget.consume(levelWidth, levelHeight, &result->error)) { return false; }
		const QImage mip = makeIndexedImage(data + start, bytes.size() - start, levelWidth, levelHeight, table, budget);
		if (mip.isNull()) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate a miptex mip level.");
			return false;
		}
		result->mipLevels.push_back(mip);
	}

	result->image = result->mipLevels.at(0);
	result->width = header.width;
	result->height = header.height;
	result->textureName = name;
	applyPaletteLabels(result, effective);
	result->hasTransparency = applyTransparent && indexedImageUsesIndex(result->image, effective.transparentIndex, budget);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Texture name: %1").arg(name);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Mip levels: 4 (full, 1/2, 1/4, 1/8)");
	result->detailLines << (embedded
		? QCoreApplication::translate("VibeStudioIdTechImage", "Palette: embedded WAD3 palette (256 entries).")
		: QCoreApplication::translate("VibeStudioIdTechImage", "Palette: external WAD2 palette."));
	result->decoded = true;
	return true;
}

bool decodeQuake2Wal(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	MipHeader header;
	if (!walLooksValid(bytes, &header)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .wal header does not validate.");
		return false;
	}
	QString name = fixedLatin1(bytes, 0, 32);
	if (name.isEmpty()) {
		name = pathBaseName(virtualPath);
	}
	result->textureName = name;
	result->animationNextName = fixedLatin1(bytes, 56, 32);
	result->surfaceFlags = readLe32(bytes, 88);
	result->contentFlags = readLe32(bytes, 92);
	result->surfaceValue = readLe32Signed(bytes, 96);

	const QVector<QRgb> table = paletteColorTable(palette, false);
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	for (int level = 0; level < 4; ++level) {
		const int levelWidth = header.width >> level;
		const int levelHeight = header.height >> level;
		const qsizetype start = static_cast<qsizetype>(header.offsets[level]);
		if (!budget.consume(levelWidth, levelHeight, &result->error)) { return false; }
		const QImage mip = makeIndexedImage(data + start, bytes.size() - start, levelWidth, levelHeight, table, budget);
		if (mip.isNull()) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate a .wal mip level.");
			return false;
		}
		result->mipLevels.push_back(mip);
	}

	result->image = result->mipLevels.at(0);
	result->width = header.width;
	result->height = header.height;
	applyPaletteLabels(result, palette);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Texture name: %1").arg(name);
	if (!result->animationNextName.isEmpty()) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Next animation frame: %1").arg(result->animationNextName);
	}
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface flags: 0x%1").arg(result->surfaceFlags, 8, 16, QLatin1Char('0'));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Content flags: 0x%1").arg(result->contentFlags, 8, 16, QLatin1Char('0'));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface value: %1").arg(result->surfaceValue);
	result->decoded = true;
	return true;
}

bool decodeQuake2M8(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	ExtendedMipHeader header;
	if (!m8LooksValid(bytes, &header)) {
		if (bytes.size() >= 4 && bytes.size() < kM8HeaderSize) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m8 header is truncated.");
		} else if (bytes.size() >= 4 && readLe32(bytes, 0) != kM8Version) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m8 version %1 is not supported (expected 2).").arg(readLe32(bytes, 0));
		} else {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m8 mip table does not validate.");
		}
		return false;
	}
	QString name = fixedLatin1(bytes, 4, 32);
	if (name.isEmpty()) {
		name = pathBaseName(virtualPath);
	}
	result->textureName = name;
	result->animationNextName = fixedLatin1(bytes, 228, 32);
	result->surfaceFlags = readLe32(bytes, 1028);
	result->contentFlags = readLe32(bytes, 1032);
	result->surfaceValue = readLe32Signed(bytes, 1036);

	// The 768-byte palette lives inside the header and always wins over the
	// package palette, unless it is entirely black (nothing to preview with).
	IdTechPalette effective = palette;
	QVector<QRgb> colors;
	colors.reserve(256);
	bool anyColor = false;
	for (int index = 0; index < 256; ++index) {
		const qsizetype at = kM8PaletteAt + static_cast<qsizetype>(index) * 3;
		const int red = readU8(bytes, at);
		const int green = readU8(bytes, at + 1);
		const int blue = readU8(bytes, at + 2);
		anyColor = anyColor || red != 0 || green != 0 || blue != 0;
		colors.push_back(qRgb(red, green, blue));
	}
	const bool embedded = anyColor;
	if (embedded) {
		effective.colors = colors;
		effective.id = QStringLiteral("m8-embedded");
		effective.displayName = QCoreApplication::translate("VibeStudioIdTechImage", "Embedded .m8 palette");
		effective.sourceDescription = QCoreApplication::translate("VibeStudioIdTechImage", "256-entry palette stored in the .m8 header");
		effective.generated = false;
		effective.transparentIndex = -1;
		effective.fullbrightStartIndex = -1;
	} else {
		result->warnings << QCoreApplication::translate("VibeStudioIdTechImage", "The embedded .m8 palette is empty; the supplied palette was used instead.");
	}

	const QVector<QRgb> table = paletteColorTable(effective, false);
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	for (int level = 0; level < header.levelCount; ++level) {
		const qsizetype start = static_cast<qsizetype>(header.offsets[level]);
		if (!budget.consume(header.widths[level], header.heights[level], &result->error)) { return false; }
		const QImage mip = makeIndexedImage(data + start, bytes.size() - start, header.widths[level], header.heights[level], table, budget);
		if (mip.isNull()) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate a .m8 mip level.");
			return false;
		}
		result->mipLevels.push_back(mip);
	}

	result->image = result->mipLevels.at(0);
	result->width = header.widths[0];
	result->height = header.heights[0];
	applyPaletteLabels(result, effective);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Texture name: %1").arg(name);
	if (!result->animationNextName.isEmpty()) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Next animation frame: %1").arg(result->animationNextName);
	}
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Mip levels: %1 of 16 stored.").arg(header.levelCount);
	result->detailLines << (embedded
		? QCoreApplication::translate("VibeStudioIdTechImage", "Palette: embedded 256-entry .m8 palette.")
		: QCoreApplication::translate("VibeStudioIdTechImage", "Palette: external (the embedded palette is empty)."));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface flags: 0x%1").arg(result->surfaceFlags, 8, 16, QLatin1Char('0'));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Content flags: 0x%1").arg(result->contentFlags, 8, 16, QLatin1Char('0'));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface value: %1").arg(result->surfaceValue);
	result->decoded = true;
	return true;
}

bool decodeQuake2M32(const QString& virtualPath, const QByteArray& bytes, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	ExtendedMipHeader header;
	if (!m32LooksValid(bytes, &header)) {
		if (bytes.size() >= 4 && bytes.size() < kM32HeaderSize) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m32 header is truncated.");
		} else if (bytes.size() >= 4 && readLe32(bytes, 0) != kM32Version) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m32 version %1 is not supported (expected 4).").arg(readLe32(bytes, 0));
		} else {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m32 mip table does not validate.");
		}
		return false;
	}
	QString name = fixedLatin1(bytes, 4, 128);
	if (name.isEmpty()) {
		name = pathBaseName(virtualPath);
	}
	const QString altName = fixedLatin1(bytes, 132, 128);
	const QString damageName = fixedLatin1(bytes, 388, 128);
	result->textureName = name;
	result->animationNextName = fixedLatin1(bytes, 260, 128);
	result->surfaceFlags = readLe32(bytes, 708);
	result->contentFlags = readLe32(bytes, 712);
	result->surfaceValue = readLe32Signed(bytes, 716);
	const float scaleX = readLeFloat(bytes, 720);
	const float scaleY = readLeFloat(bytes, 724);
	const qint32 mipScale = readLe32Signed(bytes, 728);

	// Truecolour: the payload is straight RGBA bytes, no palette involved.
	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	bool sawTransparency = false;
	for (int level = 0; level < header.levelCount; ++level) {
		const int levelWidth = header.widths[level];
		const int levelHeight = header.heights[level];
		if (!budget.consume(levelWidth, levelHeight, &result->error)) { return false; }
		QImage mip(levelWidth, levelHeight, QImage::Format_ARGB32);
		if (mip.isNull()) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate a .m32 mip level.");
			return false;
		}
		const qsizetype start = static_cast<qsizetype>(header.offsets[level]);
		for (int y = 0; y < levelHeight; ++y) {
			if (!budget.checkpoint()) { return false; }
			const uchar* row = data + start + static_cast<qsizetype>(y) * levelWidth * 4;
			auto* line = reinterpret_cast<QRgb*>(mip.scanLine(y));
			for (int x = 0; x < levelWidth; ++x) {
				const uchar* pixel = row + static_cast<qsizetype>(x) * 4;
				if (pixel[3] != 255) {
					sawTransparency = true;
				}
				line[x] = qRgba(pixel[0], pixel[1], pixel[2], pixel[3]);
			}
		}
		result->mipLevels.push_back(mip);
	}

	result->image = result->mipLevels.at(0);
	result->width = header.widths[0];
	result->height = header.heights[0];
	result->hasTransparency = sawTransparency;
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Texture name: %1").arg(name);
	if (!altName.isEmpty()) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Substitute texture: %1").arg(altName);
	}
	if (!result->animationNextName.isEmpty()) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Next animation frame: %1").arg(result->animationNextName);
	}
	if (!damageName.isEmpty()) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Damaged texture: %1").arg(damageName);
	}
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Truecolour RGBA texture, %1 of 16 mip levels stored.").arg(header.levelCount);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Texture scale: %1 x %2, mip scale %3")
		.arg(static_cast<double>(scaleX), 0, 'f', 3)
		.arg(static_cast<double>(scaleY), 0, 'f', 3)
		.arg(mipScale);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface flags: 0x%1").arg(result->surfaceFlags, 8, 16, QLatin1Char('0'));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Content flags: 0x%1").arg(result->contentFlags, 8, 16, QLatin1Char('0'));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface value: %1").arg(result->surfaceValue);
	result->decoded = true;
	return true;
}

bool decodePcx(const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	PcxHeader header;
	if (!parsePcxHeader(bytes, &header)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "PCX header does not validate.");
		return false;
	}
	const bool tailPalette = pcxHasTailPalette(bytes, header);
	if (!budget.consume(header.width, header.height, &result->error)) { return false; }
	const qsizetype dataEnd = tailPalette ? bytes.size() - 769 : bytes.size();
	const qsizetype rowBytes = static_cast<qsizetype>(header.bytesPerLine) * header.planes;
	QByteArray scanlines;
	if (!decodePcxScanlines(bytes, dataEnd, header.encoding == 1, rowBytes, header.height, &scanlines, budget)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "PCX pixel data is truncated or malformed.");
		return false;
	}
	const auto* rows = reinterpret_cast<const uchar*>(scanlines.constData());

	result->width = header.width;
	result->height = header.height;
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "PCX version %1, %2 bit, %3 plane(s), %4 bytes per line.")
		.arg(header.version)
		.arg(header.bitsPerPixel)
		.arg(header.planes)
		.arg(header.bytesPerLine);

	if (header.bitsPerPixel == 8 && header.planes == 1) {
		IdTechPalette effective = palette;
		if (tailPalette) {
			QVector<QRgb> colors;
			colors.reserve(256);
			const qsizetype base = bytes.size() - 768;
			for (int index = 0; index < 256; ++index) {
				const qsizetype at = base + static_cast<qsizetype>(index) * 3;
				colors.push_back(qRgb(readU8(bytes, at), readU8(bytes, at + 1), readU8(bytes, at + 2)));
			}
			effective.colors = colors;
			effective.id = QStringLiteral("pcx-embedded");
			effective.displayName = QCoreApplication::translate("VibeStudioIdTechImage", "Embedded PCX palette");
			effective.sourceDescription = QCoreApplication::translate("VibeStudioIdTechImage", "256-entry palette stored in the PCX tail");
			effective.generated = false;
			effective.transparentIndex = -1;
			effective.fullbrightStartIndex = -1;
			result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette: embedded 256-entry tail palette.");
		} else {
			result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette: external (no tail palette present).");
		}

		QImage image(header.width, header.height, QImage::Format_Indexed8);
		if (image.isNull()) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded PCX.");
			return false;
		}
		image.setColorTable(paletteColorTable(effective, false));
		for (int y = 0; y < header.height; ++y) {
			if (!budget.checkpoint()) { return false; }
			uchar* line = image.scanLine(y);
			std::memcpy(line, rows + static_cast<qsizetype>(y) * rowBytes, static_cast<size_t>(header.width));
		}
		result->image = image;
		applyPaletteLabels(result, effective);
		result->decoded = true;
		return true;
	}

	if (header.bitsPerPixel == 8 && (header.planes == 3 || header.planes == 4)) {
		QImage image(header.width, header.height, QImage::Format_ARGB32);
		if (image.isNull()) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded PCX.");
			return false;
		}
		bool sawTransparency = false;
		for (int y = 0; y < header.height; ++y) {
			if (!budget.checkpoint()) { return false; }
			const uchar* row = rows + static_cast<qsizetype>(y) * rowBytes;
			auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (int x = 0; x < header.width; ++x) {
				const int red = row[x];
				const int green = row[header.bytesPerLine + x];
				const int blue = row[2 * static_cast<qsizetype>(header.bytesPerLine) + x];
				const int alpha = header.planes == 4 ? row[3 * static_cast<qsizetype>(header.bytesPerLine) + x] : 255;
				if (alpha != 255) {
					sawTransparency = true;
				}
				line[x] = qRgba(red, green, blue, alpha);
			}
		}
		result->image = image;
		result->hasTransparency = sawTransparency;
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Truecolour PCX (%1 planes).").arg(header.planes);
		result->decoded = true;
		return true;
	}

	result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Only 8-bit single-plane and 8-bit 3/4-plane PCX images are supported.");
	return false;
}

bool decodeTarga(const QByteArray& bytes, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	TargaHeader header;
	if (!targaLooksValid(bytes, &header)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Targa header does not validate.");
		return false;
	}
	if (!budget.consume(header.width, header.height, &result->error)) { return false; }
	QByteArray stream;
	if (!decodeTargaPixelStream(bytes, header, &stream, budget)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Targa pixel data is truncated or malformed.");
		return false;
	}

	QVector<QRgb> colorMap;
	if (header.colorMapType == 1 && header.colorMapLength > 0) {
		const int entryBytes = (header.colorMapEntrySize + 7) / 8;
		colorMap.reserve(header.colorMapLength);
		for (int index = 0; index < header.colorMapLength; ++index) {
			const qsizetype at = header.colorMapOffset + static_cast<qsizetype>(index) * entryBytes;
			if (at + entryBytes > bytes.size()) {
				break;
			}
			const auto* entry = reinterpret_cast<const uchar*>(bytes.constData() + at);
			colorMap.push_back(targaColorFromBytes(entry, header.colorMapEntrySize, true) | 0xff000000u);
		}
	}

	const bool colorMapped = header.imageType == 1 || header.imageType == 9;
	const bool greyscale = header.imageType == 3 || header.imageType == 11;
	const int bytesPerPixel = (header.pixelDepth + 7) / 8;
	const bool honourAlpha = header.attributeBits > 0;

	QImage image(header.width, header.height, QImage::Format_ARGB32);
	if (image.isNull()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded Targa.");
		return false;
	}
	const auto* source = reinterpret_cast<const uchar*>(stream.constData());
	bool anyAlpha = false;
	for (int y = 0; y < header.height; ++y) {
		if (!budget.checkpoint()) { return false; }
		// The image descriptor origin bits decide the storage order.
		const int destinationY = header.topToBottom ? y : (header.height - 1 - y);
		auto* line = reinterpret_cast<QRgb*>(image.scanLine(destinationY));
		for (int x = 0; x < header.width; ++x) {
			const uchar* pixel = source + (static_cast<qsizetype>(y) * header.width + x) * bytesPerPixel;
			QRgb color = 0;
			if (colorMapped) {
				const int index = pixel[0];
				color = (index >= 0 && index < colorMap.size()) ? colorMap.at(index) : qRgb(0, 0, 0);
			} else if (greyscale) {
				color = header.pixelDepth == 16
					? qRgba(pixel[0], pixel[0], pixel[0], pixel[1])
					: qRgb(pixel[0], pixel[0], pixel[0]);
			} else {
				color = targaColorFromBytes(pixel, header.pixelDepth, honourAlpha);
			}
			if (header.pixelDepth != 32 && !(header.pixelDepth == 16 && honourAlpha) && !(greyscale && header.pixelDepth == 16)) {
				color |= 0xff000000u;
			}
			if (qAlpha(color) != 0) {
				anyAlpha = true;
			}
			const int destinationX = header.rightToLeft ? (header.width - 1 - x) : x;
			line[destinationX] = color;
		}
	}

	// Undeclared legacy alpha may be a zero-filled spare byte. Explicit alpha
	// remains authoritative, including an intentionally fully transparent image.
	if (!anyAlpha && !honourAlpha) {
		for (int y = 0; y < header.height; ++y) {
			if (!budget.checkpoint()) { return false; }
			auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (int x = 0; x < header.width; ++x) {
				line[x] |= 0xff000000u;
			}
		}
		result->warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Targa alpha channel was entirely zero and was treated as opaque.");
	}

	bool sawTransparency = false;
	for (int y = 0; y < header.height && !sawTransparency; ++y) {
		if (!budget.checkpoint()) { return false; }
		const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
		for (int x = 0; x < header.width; ++x) {
			if (qAlpha(line[x]) != 255) {
				sawTransparency = true;
				break;
			}
		}
	}

	result->image = image;
	result->width = header.width;
	result->height = header.height;
	result->hasTransparency = sawTransparency;
	result->paletted = colorMapped;
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Targa type %1, %2 bit, %3.")
		.arg(header.imageType)
		.arg(header.pixelDepth)
		.arg(header.rle ? QCoreApplication::translate("VibeStudioIdTechImage", "run-length encoded") : QCoreApplication::translate("VibeStudioIdTechImage", "uncompressed"));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Origin: %1, %2")
		.arg(header.topToBottom ? QCoreApplication::translate("VibeStudioIdTechImage", "top") : QCoreApplication::translate("VibeStudioIdTechImage", "bottom"),
			header.rightToLeft ? QCoreApplication::translate("VibeStudioIdTechImage", "right") : QCoreApplication::translate("VibeStudioIdTechImage", "left"));
	result->decoded = true;
	return true;
}

bool decodeSprite(const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	SpriteHeader header;
	if (!spriteLooksValid(bytes, &header)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "IDSP sprite header does not validate.");
		return false;
	}
	const bool halfLife = header.version == 2;

	// Half-Life sprites carry their own palette; Quake sprites rely on the
	// palette resolved from the package.
	IdTechPalette effective = palette;
	if (halfLife) {
		QVector<QRgb> colors;
		colors.reserve(header.paletteCount);
		for (int index = 0; index < header.paletteCount; ++index) {
			const qsizetype at = header.paletteAt + static_cast<qsizetype>(index) * 3;
			colors.push_back(qRgb(readU8(bytes, at), readU8(bytes, at + 1), readU8(bytes, at + 2)));
		}
		effective.colors = colors;
		effective.id = QStringLiteral("spr-embedded");
		effective.displayName = QCoreApplication::translate("VibeStudioIdTechImage", "Embedded sprite palette");
		effective.sourceDescription = QCoreApplication::translate("VibeStudioIdTechImage", "Palette stored in the Half-Life sprite header");
		effective.generated = false;
		// Only SPR_ALPHTEST masks a palette index. SPR_INDEXALPHA is decoded
		// straight to ARGB below, and the other texture formats are opaque.
		effective.transparentIndex = header.texFormat == kSprAlphaTest ? 255 : -1;
		effective.fullbrightStartIndex = -1;
	}

	const bool indexAlpha = halfLife && header.texFormat == kSprIndexAlpha;
	const bool applyTransparent = !indexAlpha && effective.transparentIndex >= 0;
	const QVector<QRgb> table = paletteColorTable(effective, applyTransparent);
	// SPR_INDEXALPHA carries coverage in the index and takes its colour from the
	// last palette entry; sprgen.c writes a grey ramp for those sprites.
	const QRgb indexAlphaColor = table.isEmpty() ? qRgb(255, 255, 255) : table.at(table.size() - 1);

	const auto* data = reinterpret_cast<const uchar*>(bytes.constData());
	const qsizetype size = bytes.size();
	bool sawAlpha = false;

	auto makeFrameImage = [&](qsizetype at, int width, int height) -> QImage {
		if (!indexAlpha) {
			return makeIndexedImage(data + at, size - at, width, height, table, budget);
		}
		QImage image(width, height, QImage::Format_ARGB32);
		if (image.isNull()) {
			return {};
		}
		for (int y = 0; y < height; ++y) {
			if (!budget.checkpoint()) { return {}; }
			const uchar* row = data + at + static_cast<qsizetype>(y) * width;
			auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
			for (int x = 0; x < width; ++x) {
				const QRgb alpha = static_cast<QRgb>(row[x]);
				if (alpha != 255) {
					sawAlpha = true;
				}
				line[x] = (indexAlphaColor & 0x00ffffffu) | (alpha << 24);
			}
		}
		return image;
	};

	qsizetype cursor = header.framesAt;
	int groupCount = 0;
	auto readFrame = [&](int label, int groupLabel, float intervalSeconds) -> bool {
		if (cursor + 16 > size) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame header is truncated.");
			return false;
		}
		const int originX = readLe32Signed(bytes, cursor);
		const int originY = readLe32Signed(bytes, cursor + 4);
		const qint32 frameWidth = readLe32Signed(bytes, cursor + 8);
		const qint32 frameHeight = readLe32Signed(bytes, cursor + 12);
		cursor += 16;
		if (frameWidth <= 0 || frameHeight <= 0 || frameWidth > 8192 || frameHeight > 8192) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame has implausible dimensions.");
			return false;
		}
		const qsizetype pixelBytes = static_cast<qsizetype>(frameWidth) * frameHeight;
		if (pixelBytes > kMaxDecodedPixels) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame exceeds the decoded pixel limit.");
			return false;
		}
		if (cursor + pixelBytes > size) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame pixel data is truncated.");
			return false;
		}
		IdTechImageFrame frame;
		if (result->frames.size() >= kMaxStoredSpriteFrames) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "A sprite supports at most 4,096 stored frames across all groups.");
			return false;
		}
		if (!budget.consume(frameWidth, frameHeight, &result->error)) { return false; }
		frame.image = makeFrameImage(cursor, frameWidth, frameHeight);
		if (frame.image.isNull()) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate a sprite frame.");
			return false;
		}
		frame.originX = originX;
		frame.originY = originY;
		if (!std::isfinite(intervalSeconds) || intervalSeconds < 0.0f || intervalSeconds > 86400.0f) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame intervals must be finite and between zero and one day.");
			return false;
		}
		frame.durationMs = static_cast<int>(std::lround(double(intervalSeconds) * 1000.0));
		frame.label = groupLabel >= 0
			? QCoreApplication::translate("VibeStudioIdTechImage", "Group %1 frame %2").arg(groupLabel + 1).arg(label + 1)
			: QCoreApplication::translate("VibeStudioIdTechImage", "Frame %1").arg(label + 1);
		cursor += pixelBytes;
		result->frames.push_back(frame);
		return true;
	};

	for (int index = 0; index < header.frameCount; ++index) {
		if (cursor + 4 > size) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame table is truncated.");
			return false;
		}
		const qint32 group = readLe32Signed(bytes, cursor);
		cursor += 4;
		if (group == 0) {
			if (!readFrame(index, -1, 0.0f)) {
				return false;
			}
			continue;
		}
		if (group != 1) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unsupported sprite frame group type.");
			return false;
		}
		if (cursor + 4 > size) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame group header is truncated.");
			return false;
		}
		const qint32 subFrameCount = readLe32Signed(bytes, cursor);
		cursor += 4;
		if (subFrameCount <= 0 || subFrameCount > 4096) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame group count is implausible.");
			return false;
		}
		if (cursor + static_cast<qsizetype>(subFrameCount) * 4 > size) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame group interval list is truncated.");
			return false;
		}
		QVector<float> intervals;
		intervals.reserve(subFrameCount);
		for (int sub = 0; sub < subFrameCount; ++sub) {
			intervals.push_back(readLeFloat(bytes, cursor + static_cast<qsizetype>(sub) * 4));
		}
		cursor += static_cast<qsizetype>(subFrameCount) * 4;
		float previous = 0.0f;
		for (int sub = 0; sub < subFrameCount; ++sub) {
			const float absolute = intervals.at(sub);
			const float delta = absolute > previous ? absolute - previous : absolute;
			previous = absolute;
			if (!readFrame(sub, groupCount, delta)) {
				return false;
			}
		}
		++groupCount;
	}

	if (result->frames.isEmpty()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite contains no frames.");
		return false;
	}
	result->image = result->frames.at(0).image;
	result->width = header.maxWidth;
	result->height = header.maxHeight;
	result->leftOffset = result->frames.at(0).originX;
	result->topOffset = result->frames.at(0).originY;
	applyPaletteLabels(result, effective);
	if (indexAlpha) {
		result->hasTransparency = sawAlpha;
	} else if (applyTransparent) {
		for (const IdTechImageFrame& frame : result->frames) {
			if (indexedImageUsesIndex(frame.image, effective.transparentIndex, budget)) {
				result->hasTransparency = true;
				break;
			}
		}
	}
	result->detailLines << (halfLife
		? QCoreApplication::translate("VibeStudioIdTechImage", "Half-Life sprite (IDSP version 2), %1 frame entries, %2 stored frames.").arg(header.frameCount).arg(result->frames.size())
		: QCoreApplication::translate("VibeStudioIdTechImage", "Quake sprite, %1 frame entries, %2 stored frames.").arg(header.frameCount).arg(result->frames.size()));
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Sprite orientation type: %1").arg(header.type);
	if (halfLife) {
		QString textureFormat;
		switch (header.texFormat) {
		case kSprAdditive:
			textureFormat = QCoreApplication::translate("VibeStudioIdTechImage", "additive");
			break;
		case kSprIndexAlpha:
			textureFormat = QCoreApplication::translate("VibeStudioIdTechImage", "index alpha");
			break;
		case kSprAlphaTest:
			textureFormat = QCoreApplication::translate("VibeStudioIdTechImage", "alpha test (index 255 masked)");
			break;
		default:
			textureFormat = QCoreApplication::translate("VibeStudioIdTechImage", "normal");
			break;
		}
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Texture format: %1 (%2)").arg(textureFormat).arg(header.texFormat);
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette: embedded %1-entry sprite palette.").arg(header.paletteCount);
	}
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Max frame size: %1x%2").arg(header.maxWidth).arg(header.maxHeight);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Bounding radius: %1").arg(static_cast<double>(header.boundingRadius), 0, 'f', 3);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Beam length: %1").arg(static_cast<double>(header.beamLength), 0, 'f', 3);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Sync type: %1").arg(header.syncType);
	result->decoded = true;
	return true;
}

bool decodeDoomPaletteLump(const QByteArray& bytes, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	IdTechPalette palette;
	QString error;
	if (!parseIdTechPaletteBytes(bytes, QStringLiteral("doom"), &palette, &error)) {
		result->error = error.isEmpty() ? QCoreApplication::translate("VibeStudioIdTechImage", "Palette lump does not validate.") : error;
		return false;
	}
	const int banks = static_cast<int>(bytes.size() / 768);
	if (!budget.consume(192, 192, &result->error)) { return false; }
	result->image = renderIdTechPaletteSwatch(palette, 12);
	result->width = result->image.width();
	result->height = result->image.height();
	applyPaletteLabels(result, palette);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette lump with %1 bank(s) of 256 RGB triplets.").arg(banks);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Rendered as a 16x16 swatch grid of the first bank.");
	result->decoded = true;
	return true;
}

bool decodeDoomColormapLump(const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	// COLORMAP: 34 tables of 256 palette indices (32 light levels, the
	// invulnerability map, and one unused table).
	if (bytes.size() != 34 * 256) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Colormap lumps must be exactly 8704 bytes.");
		return false;
	}
	const QVector<QRgb> table = paletteColorTable(palette, false);
	if (!budget.consume(256, 34, &result->error)) { return false; }
	const QImage image = makeIndexedImage(reinterpret_cast<const uchar*>(bytes.constData()), bytes.size(), 256, 34, table, budget);
	if (image.isNull()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded colormap.");
		return false;
	}
	result->image = image;
	result->width = 256;
	result->height = 34;
	applyPaletteLabels(result, palette);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Colormap: 34 tables of 256 palette indices.");
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Rendered one table per row.");
	result->decoded = true;
	return true;
}

bool decodeRawIndexed(const QByteArray& bytes, const IdTechPalette& palette, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	// Heretic's and Hexen's full-screen pictures (TITLE, CREDIT, HELP1...) are
	// a bare 320x200 VGA screen; anything else square is guessed.
	const bool fullscreen = bytes.size() == 320 * 200;
	const auto side = static_cast<int>(std::lround(std::sqrt(static_cast<double>(bytes.size()))));
	if (!fullscreen && (side <= 0 || static_cast<qsizetype>(side) * side != bytes.size())) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Raw indexed data needs known dimensions; only square payloads are guessed.");
		return false;
	}
	const int width = fullscreen ? 320 : side;
	const int height = fullscreen ? 200 : side;
	if (!budget.consume(width, height, &result->error)) { return false; }
	const QVector<QRgb> table = paletteColorTable(palette, false);
	const QImage image = makeIndexedImage(reinterpret_cast<const uchar*>(bytes.constData()), bytes.size(), width, height, table, budget);
	if (image.isNull()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded raw image.");
		return false;
	}
	result->image = image;
	result->width = width;
	result->height = height;
	applyPaletteLabels(result, palette);
	if (fullscreen) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Raw 320x200 full-screen picture, as Heretic and Hexen store them.");
	} else {
		result->warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Dimensions were guessed from the payload size.");
	}
	result->decoded = true;
	return true;
}

bool decodeQtNative(const QByteArray& bytes, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	QBuffer buffer;
	buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
	QImageReader reader(&buffer);
	const QSize size = reader.size();
	if (!budget.consume(size.width(), size.height(), &result->error)) { return false; }
	// High-depth plugins may need more than four bytes per pixel before the
	// editor converts to RGBA8. Keep this temporary allocation bounded as well.
	const int reportedDepth = QImage::toPixelFormat(reader.imageFormat()).bitsPerPixel();
	const int depth = reportedDepth > 0 ? reportedDepth : 128;
	const qint64 rowBytes = ((qint64(size.width()) * depth + 31) / 32) * 4;
	if (rowBytes * size.height() > 128 * 1024 * 1024) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "The source image exceeds the 128 MiB decode buffer limit.");
		return false;
	}
	QImage image = reader.read();
	if (image.isNull() || image.size() != size) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Qt could not decode this image payload.");
		return false;
	}
	const bool hasAlpha = image.hasAlphaChannel();
	if (image.depth() > 32) { image = image.convertToFormat(QImage::Format_ARGB32); }
	if (image.isNull()) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Unable to allocate the decoded picture.");
		return false;
	}
	result->image = image;
	result->width = image.width();
	result->height = image.height();
	result->hasTransparency = hasAlpha;
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Decoded by the Qt image plugins, not by an idTech decoder.");
	result->decoded = true;
	return true;
}

// ---------------------------------------------------------------------------
// Palette resolution helpers.
// ---------------------------------------------------------------------------

bool looksLikePcx(const QByteArray& bytes)
{
	PcxHeader header;
	return parsePcxHeader(bytes, &header);
}

bool parsePaletteCandidateBytes(const QByteArray& bytes, const QString& paletteId, const QString& candidatePath, IdTechPalette* palette, QString* error)
{
	const QString suffix = pathSuffix(candidatePath);
	if (suffix == QStringLiteral("pcx") || looksLikePcx(bytes)) {
		if (parsePcxPalette(bytes, paletteId, palette, error)) {
			return true;
		}
	}
	return parseIdTechPaletteBytes(bytes, paletteId, palette, error);
}

bool findArchiveEntry(const PackageArchiveReader& archive, const QString& candidate, QString* pathOut)
{
	const QVector<PackageEntry> entries = archive.entries();
	for (const PackageEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File && entry.virtualPath == candidate) {
			*pathOut = entry.virtualPath;
			return true;
		}
	}
	for (const PackageEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File
			&& entry.virtualPath.compare(candidate, Qt::CaseInsensitive) == 0) {
			*pathOut = entry.virtualPath;
			return true;
		}
	}
	// Doom WAD lumps are bare uppercase names with no directory; fall back to a
	// file-name match so "PLAYPAL" is found regardless of how it was mounted.
	const QString wanted = pathFileName(candidate);
	for (const PackageEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File
			&& pathFileName(entry.virtualPath).compare(wanted, Qt::CaseInsensitive) == 0) {
			*pathOut = entry.virtualPath;
			return true;
		}
	}
	return false;
}

// Quake II .sp2 sprites are a table of frames naming external images, so a
// decode always yields the frame list and only fills in pixels when a package
// reader was supplied to resolve those names.
constexpr int kSp2MaxResolvedFrames = 256;

bool decodeQuake2Sprite(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette& palette, const IdTechImageDecodeContext& context, IdTechImageDecodeResult* result, DecodeBudget& budget)
{
	int frameCount = 0;
	if (!sp2LooksValid(bytes, &frameCount)) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .sp2 header does not validate.");
		return false;
	}
	if (frameCount == 0) {
		result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .sp2 sprite contains no frames.");
		return false;
	}

	result->externalFrames = true;
	const QString directory = virtualPath.left(std::max<qsizetype>(virtualPath.lastIndexOf('/') + 1, 0));
	int resolved = 0;
	int attempted = 0;
	int maxWidth = 0;
	int maxHeight = 0;
	qint64 externalBytes = 0;
	const auto entries = context.archive ? context.archive->entries() : QVector<PackageEntry>{};
	for (int index = 0; index < frameCount; ++index) {
		if (!budget.checkpoint()) { return false; }
		const qsizetype base = kSp2HeaderSize + static_cast<qsizetype>(index) * kSp2FrameSize;
		const qint32 frameWidth = readLe32Signed(bytes, base);
		const qint32 frameHeight = readLe32Signed(bytes, base + 4);
		if (frameWidth <= 0 || frameHeight <= 0 || frameWidth > 8192 || frameHeight > 8192) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .sp2 frame %1 has implausible dimensions.").arg(index + 1);
			return false;
		}
		if (static_cast<qsizetype>(frameWidth) * frameHeight > kMaxDecodedPixels) {
			result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .sp2 frame %1 exceeds the decoded pixel limit.").arg(index + 1);
			return false;
		}
		if (!budget.validSize(frameWidth, frameHeight, &result->error)) { return false; }
		IdTechImageFrame frame;
		frame.originX = readLe32Signed(bytes, base + 8);
		frame.originY = readLe32Signed(bytes, base + 12);
		frame.sourceName = fixedLatin1(bytes, base + 16, kSp2NameSize);
		frame.label = frame.sourceName.isEmpty()
			? QCoreApplication::translate("VibeStudioIdTechImage", "Frame %1").arg(index + 1)
			: QCoreApplication::translate("VibeStudioIdTechImage", "Frame %1: %2").arg(index + 1).arg(frame.sourceName);
		maxWidth = std::max(maxWidth, static_cast<int>(frameWidth));
		maxHeight = std::max(maxHeight, static_cast<int>(frameHeight));

		if (context.archive != nullptr && !frame.sourceName.isEmpty() && attempted < kSp2MaxResolvedFrames) {
			++attempted;
			QStringList candidates;
			candidates << frame.sourceName;
			if (!frame.sourceName.contains('/') && !directory.isEmpty()) {
				candidates << directory + frame.sourceName;
			}
			for (const QString& candidate : candidates) {
				if (!budget.checkpoint()) { return false; }
				QString entryPath;
				QByteArray entryBytes;
				QString readError;
				if (!findArchiveEntry(*context.archive, candidate, &entryPath)) {
					continue;
				}
				qint64 readWork = 0;
				for (const auto& entry : entries) {
					if (entry.virtualPath == entryPath) {
						// Clamp before converting a potentially hostile unsigned directory size.
						readWork = qint64(std::min<quint64>(std::max(entry.sizeBytes, entry.compressedSizeBytes), kMaxImagePayloadBytes + 1));
						break;
					}
				}
				if (readWork > context.maximumExternalBytes - externalBytes) {
					result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame reads exceed the aggregate external-image byte limit.");
					return false;
				}
				externalBytes += readWork;
				if (!readIdTechImageEntry(*context.archive, entryPath, &entryBytes, &readError)) {
					result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame %1 could not be read: %2").arg(entryPath, readError);
					return false;
				}
				if (!budget.checkpoint()) { return false; }
				// An empty context stops a hostile chain of sprites referring to
				// each other from recursing.
				auto frameContext = budget.limits;
				frameContext.archive = nullptr;
				frameContext.maximumTotalPixels -= budget.used;
				const IdTechImageDecodeResult frameResult = decodeIdTechImage(entryPath, entryBytes, palette, frameContext);
				if (!frameResult.decoded || frameResult.image.isNull()) {
					result->error = QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame %1 could not be decoded: %2").arg(entryPath, frameResult.error);
					return false;
				}
				if (!budget.consume(frameResult.image.width(), frameResult.image.height(), &result->error)) { return false; }
				frame.image = frameResult.image;
				frame.sourceVirtualPath = entryPath;
				++resolved;
				break;
			}
			if (frame.image.isNull()) {
				result->warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Sprite frame image was not found in the package: %1").arg(frame.sourceName);
			}
		}
		result->frames.push_back(frame);
	}

	for (const IdTechImageFrame& frame : result->frames) {
		if (!frame.image.isNull()) {
			result->image = frame.image;
			result->leftOffset = frame.originX;
			result->topOffset = frame.originY;
			result->hasTransparency = frame.image.hasAlphaChannel();
			break;
		}
	}
	result->width = maxWidth;
	result->height = maxHeight;
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Quake II sprite sheet, %1 frame(s) referencing external images.").arg(frameCount);
	result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Largest declared frame: %1x%2").arg(maxWidth).arg(maxHeight);
	if (context.archive != nullptr) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Frame images resolved from the package: %1 of %2.").arg(resolved).arg(frameCount);
		if (attempted < frameCount) {
			result->warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Only the first %1 sprite frames were resolved.").arg(kSp2MaxResolvedFrames);
		}
	} else {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "No package was available, so the frame images were not resolved.");
	}
	// Long sprite sheets would swamp the summary, so only the first entries are
	// listed individually.
	constexpr int kListedFrames = 32;
	const int listed = std::min(static_cast<int>(result->frames.size()), kListedFrames);
	for (int index = 0; index < listed; ++index) {
		const IdTechImageFrame& frame = result->frames.at(index);
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "Frame %1: %2 (origin %3, %4)")
			.arg(index + 1)
			.arg(frame.sourceName.isEmpty() ? QCoreApplication::translate("VibeStudioIdTechImage", "(unnamed)") : frame.sourceName)
			.arg(frame.originX)
			.arg(frame.originY);
	}
	if (listed < result->frames.size()) {
		result->detailLines << QCoreApplication::translate("VibeStudioIdTechImage", "... and %1 further frame(s).").arg(result->frames.size() - listed);
	}
	result->decoded = true;
	return true;
}

bool resolveCaseInsensitiveFile(const QString& directoryPath, const QString& relativePath, QString* resolved)
{
	QString current = directoryPath;
	const QStringList segments = relativePath.split('/', Qt::SkipEmptyParts);
	for (int index = 0; index < segments.size(); ++index) {
		const QString wanted = segments.at(index);
		const bool last = index == segments.size() - 1;
		const QDir dir(current);
		if (!dir.exists()) {
			return false;
		}
		QString match;
		const QStringList names = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
		for (const QString& name : names) {
			if (name.compare(wanted, Qt::CaseInsensitive) == 0) {
				match = name;
				break;
			}
		}
		if (match.isEmpty()) {
			return false;
		}
		current = dir.filePath(match);
		if (last) {
			if (!QFileInfo(current).isFile()) {
				return false;
			}
			*resolved = current;
			return true;
		}
	}
	return false;
}

IdTechPaletteResolution makeGeneratedResolution(const QString& paletteId, const QStringList& searched, const QStringList& warnings)
{
	IdTechPaletteResolution resolution;
	resolution.requestedPaletteId = normalizedPaletteId(paletteId);
	resolution.palette = generatedIdTechPalette(paletteId);
	resolution.fromPackage = false;
	resolution.searchedPaths = searched;
	resolution.warnings = warnings;
	resolution.warnings << QCoreApplication::translate("VibeStudioIdTechImage", "No game palette was found; previews use the generated stand-in palette.");
	return resolution;
}

} // namespace

// ---------------------------------------------------------------------------
// IdTechPalette
// ---------------------------------------------------------------------------

bool IdTechPalette::isValid() const
{
	return colors.size() == 256;
}

QRgb IdTechPalette::colorAt(int index) const
{
	if (index < 0 || index >= colors.size()) {
		return qRgba(0, 0, 0, 0);
	}
	return colors.at(index);
}

// ---------------------------------------------------------------------------
// Format metadata
// ---------------------------------------------------------------------------

QString idTechImageFormatId(IdTechImageFormat format)
{
	switch (format) {
	case IdTechImageFormat::QtNative:
		return QStringLiteral("qt-native");
	case IdTechImageFormat::Targa:
		return QStringLiteral("targa");
	case IdTechImageFormat::Dds: return QStringLiteral("dds");
	case IdTechImageFormat::Ftx: return QStringLiteral("ftx");
	case IdTechImageFormat::SinSwl: return QStringLiteral("sin-swl");
	case IdTechImageFormat::Pcx:
		return QStringLiteral("pcx");
	case IdTechImageFormat::QuakeLump:
		return QStringLiteral("quake-lmp");
	case IdTechImageFormat::QuakeMipTexture:
		return QStringLiteral("quake-miptex");
	case IdTechImageFormat::Quake2Wal:
		return QStringLiteral("quake2-wal");
	case IdTechImageFormat::Quake2M8:
		return QStringLiteral("quake2-m8");
	case IdTechImageFormat::Quake2M32:
		return QStringLiteral("quake2-m32");
	case IdTechImageFormat::QuakeSprite:
		return QStringLiteral("quake-spr");
	case IdTechImageFormat::HalfLifeSprite:
		return QStringLiteral("halflife-spr");
	case IdTechImageFormat::Quake2Sprite:
		return QStringLiteral("quake2-sp2");
	case IdTechImageFormat::DoomPatch:
		return QStringLiteral("doom-patch");
	case IdTechImageFormat::DoomFlat:
		return QStringLiteral("doom-flat");
	case IdTechImageFormat::DoomPalette:
		return QStringLiteral("doom-palette");
	case IdTechImageFormat::DoomColormap:
		return QStringLiteral("doom-colormap");
	case IdTechImageFormat::Raw:
		return QStringLiteral("raw-indexed");
	case IdTechImageFormat::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString idTechImageFormatDisplayName(IdTechImageFormat format)
{
	switch (format) {
	case IdTechImageFormat::Dds: return QStringLiteral("DDS");
	case IdTechImageFormat::Ftx: return QStringLiteral("FTX");
	case IdTechImageFormat::SinSwl: return QStringLiteral("SiN SWL");
	case IdTechImageFormat::QtNative:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Standard image (Qt decoder)");
	case IdTechImageFormat::Targa:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Targa image");
	case IdTechImageFormat::Pcx:
		return QCoreApplication::translate("VibeStudioIdTechImage", "ZSoft PCX image");
	case IdTechImageFormat::QuakeLump:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Quake .lmp picture");
	case IdTechImageFormat::QuakeMipTexture:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Quake WAD2/WAD3 miptex");
	case IdTechImageFormat::Quake2Wal:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .wal texture");
	case IdTechImageFormat::Quake2M8:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m8 texture");
	case IdTechImageFormat::Quake2M32:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .m32 texture");
	case IdTechImageFormat::QuakeSprite:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Quake sprite");
	case IdTechImageFormat::HalfLifeSprite:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Half-Life sprite");
	case IdTechImageFormat::Quake2Sprite:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Quake II .sp2 sprite");
	case IdTechImageFormat::DoomPatch:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Doom picture (patch)");
	case IdTechImageFormat::DoomFlat:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Doom flat");
	case IdTechImageFormat::DoomPalette:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Doom palette lump");
	case IdTechImageFormat::DoomColormap:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Doom colormap lump");
	case IdTechImageFormat::Raw:
		return QCoreApplication::translate("VibeStudioIdTechImage", "Raw indexed data");
	case IdTechImageFormat::Unknown:
		break;
	}
	return QCoreApplication::translate("VibeStudioIdTechImage", "Unknown image format");
}

bool idTechImageFormatIsPaletted(IdTechImageFormat format)
{
	switch (format) {
	case IdTechImageFormat::QuakeLump:
	case IdTechImageFormat::QuakeMipTexture:
	case IdTechImageFormat::Quake2Wal:
	case IdTechImageFormat::Quake2M8:
	case IdTechImageFormat::QuakeSprite:
	case IdTechImageFormat::HalfLifeSprite:
	// .sp2 stores no pixels of its own, but its frames point at indexed images,
	// so a palette is still worth resolving before one is decoded.
	case IdTechImageFormat::Quake2Sprite:
	case IdTechImageFormat::DoomPatch:
	case IdTechImageFormat::DoomFlat:
	case IdTechImageFormat::DoomColormap:
	case IdTechImageFormat::Raw:
		return true;
	default:
		break;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Palette descriptors
// ---------------------------------------------------------------------------

QVector<IdTechPaletteDescriptor> idTechPaletteDescriptors()
{
	QVector<IdTechPaletteDescriptor> descriptors;
	descriptors.reserve(static_cast<int>(std::size(kPaletteRecipes)));
	for (const GeneratedPaletteRecipe& recipe : kPaletteRecipes) {
		IdTechPaletteDescriptor descriptor;
		descriptor.id = QString::fromLatin1(recipe.id);
		descriptor.displayName = QCoreApplication::translate("VibeStudioIdTechImage", recipe.displayName);
		descriptor.engineFamily = QString::fromLatin1(recipe.engineFamily);
		descriptor.description = QCoreApplication::translate("VibeStudioIdTechImage", recipe.description);
		descriptor.transparentIndex = recipe.transparentIndex;
		descriptor.fullbrightStartIndex = recipe.fullbrightStartIndex;
		descriptors.push_back(descriptor);
	}
	return descriptors;
}

QStringList idTechPaletteIds()
{
	QStringList ids;
	for (const GeneratedPaletteRecipe& recipe : kPaletteRecipes) {
		ids << QString::fromLatin1(recipe.id);
	}
	return ids;
}

bool idTechPaletteDescriptorForId(const QString& id, IdTechPaletteDescriptor* out)
{
	const QString wanted = normalizedPaletteId(id);
	for (const IdTechPaletteDescriptor& descriptor : idTechPaletteDescriptors()) {
		if (descriptor.id == wanted) {
			if (out) {
				*out = descriptor;
			}
			return true;
		}
	}
	return false;
}

IdTechPalette generatedIdTechPalette(const QString& paletteId)
{
	const GeneratedPaletteRecipe& recipe = recipeForId(paletteId);
	IdTechPalette palette;
	palette.id = QString::fromLatin1(recipe.id);
	palette.displayName = QCoreApplication::translate("VibeStudioIdTechImage", recipe.displayName);
	palette.sourceDescription = QCoreApplication::translate("VibeStudioIdTechImage", "Procedurally generated ramp palette (not a game palette)");
	palette.transparentIndex = recipe.transparentIndex;
	palette.fullbrightStartIndex = recipe.fullbrightStartIndex;
	palette.generated = true;

	QSet<QRgb> used;
	QVector<QRgb> colors;
	colors.reserve(256);
	for (int index = 0; index < 256; ++index) {
		const int ramp = index / 16;
		const int step = index % 16;
		const double t = static_cast<double>(step) / 15.0;
		int red = 0;
		int green = 0;
		int blue = 0;
		if (index == recipe.transparentIndex) {
			// Distinct marker colour; the entry is published with zero alpha.
			hsvToRgb(recipe.hueOffset + 300.0, 1.0, 1.0, &red, &green, &blue);
		} else if (recipe.fullbrightStartIndex >= 0 && index >= recipe.fullbrightStartIndex) {
			const int offset = index - recipe.fullbrightStartIndex;
			hsvToRgb(recipe.hueOffset + 12.0 * offset, 0.95, 0.72 + 0.28 * (static_cast<double>(offset % 8) / 7.0), &red, &green, &blue);
		} else if (ramp == 0) {
			const int level = static_cast<int>(std::lround(t * 255.0));
			red = level;
			green = level;
			blue = level;
		} else {
			const double hue = recipe.hueOffset + (360.0 / 15.0) * static_cast<double>(ramp - 1);
			const double saturation = recipe.saturationBase * (1.0 - 0.35 * t);
			const double value = 0.08 + 0.92 * t;
			hsvToRgb(hue, saturation, value, &red, &green, &blue);
		}
		QRgb color = uniqueOpaqueColor(&used, red, green, blue);
		if (index == recipe.transparentIndex) {
			color &= 0x00ffffffu;
		} else {
			color |= 0xff000000u;
		}
		colors.push_back(color);
	}
	palette.colors = colors;
	return palette;
}

// ---------------------------------------------------------------------------
// Palette parsing
// ---------------------------------------------------------------------------

bool parseIdTechPaletteBytes(const QByteArray& bytes, const QString& paletteId, IdTechPalette* palette, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!palette) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioIdTechImage", "No palette output was provided.");
		}
		return false;
	}
	// Doom PLAYPAL stores 14 banks of 256 RGB triplets; Quake gfx/palette.lmp
	// stores exactly one. Only the first bank is the base palette.
	if (bytes.size() < 768 || (bytes.size() % 768) != 0) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioIdTechImage", "Palette payloads must be a whole number of 768-byte RGB banks.");
		}
		return false;
	}

	IdTechPaletteDescriptor descriptor;
	const bool known = idTechPaletteDescriptorForId(paletteId, &descriptor);

	IdTechPalette parsed;
	parsed.id = known ? descriptor.id : normalizedPaletteId(paletteId);
	parsed.displayName = known ? descriptor.displayName : parsed.id;
	parsed.sourceDescription = QCoreApplication::translate("VibeStudioIdTechImage", "768-byte RGB triplet palette");
	parsed.transparentIndex = known ? descriptor.transparentIndex : -1;
	parsed.fullbrightStartIndex = known ? descriptor.fullbrightStartIndex : -1;
	parsed.generated = false;
	parsed.colors.reserve(256);
	for (int index = 0; index < 256; ++index) {
		const qsizetype at = static_cast<qsizetype>(index) * 3;
		QRgb color = qRgb(readU8(bytes, at), readU8(bytes, at + 1), readU8(bytes, at + 2));
		if (index == parsed.transparentIndex) {
			color &= 0x00ffffffu;
		}
		parsed.colors.push_back(color);
	}
	*palette = parsed;
	return true;
}

bool parsePcxPalette(const QByteArray& bytes, const QString& paletteId, IdTechPalette* palette, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!palette) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioIdTechImage", "No palette output was provided.");
		}
		return false;
	}
	PcxHeader header;
	if (!parsePcxHeader(bytes, &header)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioIdTechImage", "Not a valid PCX payload.");
		}
		return false;
	}
	if (!pcxHasTailPalette(bytes, header)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioIdTechImage", "PCX payload has no 0x0C tail palette.");
		}
		return false;
	}
	const QByteArray tail = bytes.right(768);
	if (!parseIdTechPaletteBytes(tail, paletteId, palette, error)) {
		return false;
	}
	palette->sourceDescription = QCoreApplication::translate("VibeStudioIdTechImage", "256-entry palette stored in the PCX tail");
	return true;
}

QStringList idTechPaletteCandidatePaths(const QString& paletteId)
{
	const QString id = normalizedPaletteId(paletteId);
	if (id == QStringLiteral("quake")) {
		return {QStringLiteral("gfx/palette.lmp"), QStringLiteral("palette.lmp")};
	}
	if (id == QStringLiteral("quake2")) {
		return {QStringLiteral("pics/colormap.pcx"), QStringLiteral("pics/palette.pcx"), QStringLiteral("colormap.pcx")};
	}
	if (id == QStringLiteral("doom") || id == QStringLiteral("heretic") || id == QStringLiteral("hexen")) {
		return {QStringLiteral("PLAYPAL"), QStringLiteral("playpal.lmp"), QStringLiteral("gfx/playpal.lmp")};
	}
	return {QStringLiteral("gfx/palette.lmp"), QStringLiteral("palette.lmp"), QStringLiteral("PLAYPAL"), QStringLiteral("pics/colormap.pcx")};
}

namespace {

// The first of `candidates` that names a known palette, else the first known.
QString firstKnownPaletteId(const QStringList& candidates)
{
	for (const QString& candidate : candidates) {
		if (!candidate.isEmpty() && idTechPaletteDescriptorForId(candidate)) {
			return candidate;
		}
	}
	const QStringList known = idTechPaletteIds();
	return known.isEmpty() ? QString() : known.front();
}

} // namespace

QString defaultIdTechPaletteIdForFormat(IdTechImageFormat format)
{
	switch (format) {
	case IdTechImageFormat::DoomPatch:
	case IdTechImageFormat::DoomFlat:
	case IdTechImageFormat::DoomPalette:
	case IdTechImageFormat::DoomColormap:
		return firstKnownPaletteId({QStringLiteral("doom"), QStringLiteral("idtech1"), QStringLiteral("doom-playpal")});
	case IdTechImageFormat::Quake2Wal:
		return firstKnownPaletteId({QStringLiteral("quake2"), QStringLiteral("quake-ii"), QStringLiteral("idtech2-quake2")});
	case IdTechImageFormat::Pcx:
		return firstKnownPaletteId({QStringLiteral("quake2"), QStringLiteral("quake-ii"), QStringLiteral("quake")});
	case IdTechImageFormat::QuakeLump:
	case IdTechImageFormat::QuakeMipTexture:
	case IdTechImageFormat::QuakeSprite:
		return firstKnownPaletteId({QStringLiteral("quake"), QStringLiteral("idtech2"), QStringLiteral("quake1")});
	default:
		return firstKnownPaletteId({QStringLiteral("quake"), QStringLiteral("doom")});
	}
}

QString defaultIdTechPaletteIdForImage(IdTechImageFormat format, qsizetype byteCount)
{
	if (format == IdTechImageFormat::Raw && byteCount == 320 * 200) {
		return defaultIdTechPaletteIdForFormat(IdTechImageFormat::DoomFlat);
	}
	return defaultIdTechPaletteIdForFormat(format);
}

QString idTechPaletteIdInPackage(const PackageArchiveReader& archive)
{
	if (!archive.isOpen()) {
		return {};
	}
	for (const QString& id : {QStringLiteral("doom"), QStringLiteral("quake"), QStringLiteral("quake2")}) {
		for (const QString& candidate : idTechPaletteCandidatePaths(id)) {
			QString found;
			if (findArchiveEntry(archive, candidate, &found)) {
				return id;
			}
		}
	}
	return {};
}

IdTechPaletteResolution resolveIdTechPalette(const PackageArchiveReader& archive, const QString& paletteId)
{
	const QStringList candidates = idTechPaletteCandidatePaths(paletteId);
	QStringList warnings;
	if (!archive.isOpen()) {
		warnings << QCoreApplication::translate("VibeStudioIdTechImage", "The package is not open; no palette lookup was attempted.");
		return makeGeneratedResolution(paletteId, candidates, warnings);
	}

	for (const QString& candidate : candidates) {
		QString actualPath;
		if (!findArchiveEntry(archive, candidate, &actualPath)) {
			continue;
		}
		QByteArray bytes;
		QString readError;
		if (!readIdTechImageEntry(archive, actualPath, &bytes, &readError)) {
			warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Unable to read %1: %2").arg(actualPath, readError);
			continue;
		}
		IdTechPalette palette;
		QString parseError;
		if (!parsePaletteCandidateBytes(bytes, paletteId, actualPath, &palette, &parseError)) {
			warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Ignored %1: %2").arg(actualPath, parseError);
			continue;
		}
		IdTechPaletteResolution resolution;
		resolution.palette = palette;
		resolution.requestedPaletteId = normalizedPaletteId(paletteId);
		resolution.sourceVirtualPath = actualPath;
		resolution.fromPackage = true;
		resolution.searchedPaths = candidates;
		resolution.warnings = warnings;
		return resolution;
	}

	return makeGeneratedResolution(paletteId, candidates, warnings);
}

IdTechPaletteResolution resolveIdTechPaletteFromDirectory(const QString& directoryPath, const QString& paletteId)
{
	const QStringList candidates = idTechPaletteCandidatePaths(paletteId);
	QStringList warnings;
	const QFileInfo rootInfo(directoryPath);
	if (directoryPath.isEmpty() || !rootInfo.isDir()) {
		warnings << QCoreApplication::translate("VibeStudioIdTechImage", "The palette search directory does not exist.");
		return makeGeneratedResolution(paletteId, candidates, warnings);
	}

	for (const QString& candidate : candidates) {
		QString resolvedPath;
		if (!resolveCaseInsensitiveFile(rootInfo.absoluteFilePath(), candidate, &resolvedPath)) {
			continue;
		}
		QFile file(resolvedPath);
		if (!file.open(QIODevice::ReadOnly)) {
			warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Unable to read %1: %2").arg(resolvedPath, file.errorString());
			continue;
		}
		const QByteArray bytes = file.read(16 * 1024 * 1024);
		file.close();
		IdTechPalette palette;
		QString parseError;
		if (!parsePaletteCandidateBytes(bytes, paletteId, resolvedPath, &palette, &parseError)) {
			warnings << QCoreApplication::translate("VibeStudioIdTechImage", "Ignored %1: %2").arg(resolvedPath, parseError);
			continue;
		}
		IdTechPaletteResolution resolution;
		resolution.palette = palette;
		resolution.requestedPaletteId = normalizedPaletteId(paletteId);
		resolution.sourceVirtualPath = candidate;
		resolution.fromPackage = true;
		resolution.searchedPaths = candidates;
		resolution.warnings = warnings;
		return resolution;
	}

	return makeGeneratedResolution(paletteId, candidates, warnings);
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------

bool readIdTechImageEntryAt(const PackageArchiveReader& archive, qsizetype index, QByteArray* bytes, QString* error)
{
	if (bytes) { bytes->clear(); }
	const auto entries = archive.entries();
	if (!bytes || index < 0 || index >= entries.size()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioIdTechImage", "Image entry is unavailable."); }
		return false;
	}
	const auto& entry = entries[index];
	if (!entry.readable || entry.kind != PackageEntryKind::File || entry.sizeBytes > quint64(kMaxImagePayloadBytes) ||
		entry.compressedSizeBytes > quint64(kMaxImagePayloadBytes)) {
		if (error) { *error = QCoreApplication::translate("VibeStudioIdTechImage", "Image entry is unreadable or exceeds the 64 MiB import limit."); }
		return false;
	}
	QByteArray payload;
	// Probe at most one byte beyond the captured size. A loose source growing
	// during a sprite read must not consume the whole per-file allowance.
	if (!archive.readEntryAt(index, &payload, error, qint64(entry.sizeBytes) + 1)) { return false; }
	if (payload.size() > kMaxImagePayloadBytes || quint64(payload.size()) != entry.sizeBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioIdTechImage", "Image entry size changed or its complete payload could not be read."); }
		return false;
	}
	*bytes = std::move(payload); return true;
}

bool readIdTechImageEntry(const PackageArchiveReader& archive, const QString& virtualPath, QByteArray* bytes, QString* error)
{
	const auto entries = archive.entries();
	for (auto sensitivity : {Qt::CaseSensitive, Qt::CaseInsensitive}) {
		qsizetype found = -1;
		for (qsizetype i = 0; i < entries.size(); ++i) {
			if (entries[i].virtualPath.compare(virtualPath, sensitivity) != 0) { continue; }
			if (found >= 0) {
				if (bytes) { bytes->clear(); }
				if (error) { *error = QCoreApplication::translate("VibeStudioIdTechImage", "Image entry name is ambiguous. Select its package occurrence."); }
				return false;
			}
			found = i;
		}
		if (found >= 0) { return readIdTechImageEntryAt(archive, found, bytes, error); }
	}
	return readIdTechImageEntryAt(archive, -1, bytes, error);
}

IdTechImageFormat detectIdTechImageFormat(const QString& virtualPath, const QByteArray& bytes)
{
	if (bytes.isEmpty()) {
		return IdTechImageFormat::Unknown;
	}

	if (bytes.startsWith("DDS ")) { return IdTechImageFormat::Dds; }
	// These formats have no magic. The suffix selects a strict bounded decoder.
	if (pathSuffix(virtualPath) == QStringLiteral("dds")) { return IdTechImageFormat::Dds; }
	if (pathSuffix(virtualPath) == QStringLiteral("ftx")) { return IdTechImageFormat::Ftx; }
	if (pathSuffix(virtualPath) == QStringLiteral("swl")) { return IdTechImageFormat::SinSwl; }
	// Content sniffing first: every candidate below must have a self-consistent
	// header. The virtual path is consulted only to break ties.
	if (qtNativeMagic(bytes)) {
		return IdTechImageFormat::QtNative;
	}
	SpriteHeader spriteHeader;
	if (spriteLooksValid(bytes, &spriteHeader)) {
		return spriteHeader.version == 2 ? IdTechImageFormat::HalfLifeSprite : IdTechImageFormat::QuakeSprite;
	}
	if (sp2LooksValid(bytes)) {
		return IdTechImageFormat::Quake2Sprite;
	}
	if (looksLikePcx(bytes)) {
		return IdTechImageFormat::Pcx;
	}

	const QString suffix = pathSuffix(virtualPath);
	const bool walOk = walLooksValid(bytes);
	const bool mipOk = mipTextureLooksValid(bytes);
	const bool lumpOk = quakeLumpLooksValid(bytes);
	const bool patchOk = doomPatchLooksValid(bytes);
	const bool flatSizeOk = bytes.size() == 4096 || bytes.size() == 4160 || bytes.size() == 16384;
	const bool flatOk = flatSizeOk && looksLikeFlatContext(virtualPath);
	const bool paletteSizeOk = bytes.size() >= 768 && (bytes.size() % 768) == 0 && bytes.size() <= 768 * 32;
	const bool colormapSizeOk = bytes.size() == 34 * 256;
	TargaHeader targaHeader;
	const bool targaHeaderOk = targaLooksValid(bytes, &targaHeader);
	const bool targaOk = targaHeaderOk && (isTargaSuffix(suffix) || targaFooterPresent(bytes));

	if (targaOk) {
		return IdTechImageFormat::Targa;
	}
	// .m8 and .m32 have no magic, but a matching version word plus sixteen
	// self-consistent, in-bounds mip entries is specific enough to trust.
	if (m8LooksValid(bytes)) {
		return IdTechImageFormat::Quake2M8;
	}
	if (m32LooksValid(bytes)) {
		return IdTechImageFormat::Quake2M32;
	}
	if (walOk && (suffix == QStringLiteral("wal") || !mipOk)) {
		return IdTechImageFormat::Quake2Wal;
	}
	if (mipOk) {
		return IdTechImageFormat::QuakeMipTexture;
	}
	if (walOk) {
		return IdTechImageFormat::Quake2Wal;
	}
	if (colormapSizeOk && (nameHintsColormap(virtualPath) || !patchOk)) {
		return IdTechImageFormat::DoomColormap;
	}
	if (paletteSizeOk && (nameHintsPalette(virtualPath) || !lumpOk)) {
		return IdTechImageFormat::DoomPalette;
	}
	if (flatOk && looksLikeStrongFlatContext(virtualPath)) {
		return IdTechImageFormat::DoomFlat;
	}
	if (lumpOk && (suffix == QStringLiteral("lmp") || !flatOk)) {
		return IdTechImageFormat::QuakeLump;
	}
	if (patchOk) {
		return IdTechImageFormat::DoomPatch;
	}
	if (lumpOk) {
		return IdTechImageFormat::QuakeLump;
	}
	if (flatOk) {
		return IdTechImageFormat::DoomFlat;
	}
	// A bare 320x200 screen, Heretic's and Hexen's full-screen picture format,
	// when nothing with a header claimed the bytes.
	if (bytes.size() == 320 * 200 && !patchOk && pathSuffix(virtualPath).isEmpty()) {
		return IdTechImageFormat::Raw;
	}
	if (targaHeaderOk) {
		return IdTechImageFormat::Targa;
	}

	QBuffer buffer;
	buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
	QImageReader probe(&buffer);
	if (probe.canRead()) {
		return IdTechImageFormat::QtNative;
	}
	return IdTechImageFormat::Unknown;
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

IdTechImageDecodeResult decodeIdTechImage(const QString& virtualPath, const QByteArray& bytes, const IdTechPalette& palette, const IdTechImageDecodeContext& context)
{
	IdTechImageDecodeResult result;
	if (bytes.size() > kMaxImagePayloadBytes) {
		result.error = QCoreApplication::translate("VibeStudioIdTechImage", "The image payload exceeds the 64 MiB import limit.");
		return result;
	}
	if (context.maximumDimension < 1 || context.maximumDimension > 65535 || context.maximumImagePixels < 1 ||
		context.maximumImagePixels > kMaxDecodedPixels || context.maximumTotalPixels < 1 || context.maximumTotalPixels > kMaxTotalDecodedPixels ||
		context.maximumExternalBytes < 0 || context.maximumExternalBytes > kMaxImagePayloadBytes) {
		result.error = QCoreApplication::translate("VibeStudioIdTechImage", "Invalid image decode limits.");
		return result;
	}
	DecodeBudget budget{context};
	if (!budget.checkpoint()) {
		result.error = QCoreApplication::translate("VibeStudioIdTechImage", "Image decoding cancelled.");
		return result;
	}
	result.format = detectIdTechImageFormat(virtualPath, bytes);
	applyFormatLabels(&result);

	if (bytes.isEmpty()) {
		result.error = QCoreApplication::translate("VibeStudioIdTechImage", "The payload is empty.");
		return result;
	}

	IdTechPalette effective = palette;
	if (!effective.isValid()) {
		effective = generatedIdTechPalette(QStringLiteral("generic"));
		if (idTechImageFormatIsPaletted(result.format)) {
			result.warnings << QCoreApplication::translate("VibeStudioIdTechImage", "No valid palette was supplied; the generated stand-in palette was used.");
		}
	}

	switch (result.format) {
	case IdTechImageFormat::QtNative:
		decodeQtNative(bytes, &result, budget);
		break;
	case IdTechImageFormat::Dds:
	case IdTechImageFormat::Ftx:
	case IdTechImageFormat::SinSwl:
		result = decodeExtraImage(result.format, bytes, context);
		break;
	case IdTechImageFormat::Targa:
		decodeTarga(bytes, &result, budget);
		break;
	case IdTechImageFormat::Pcx:
		decodePcx(bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::QuakeLump:
		decodeQuakeLump(bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::QuakeMipTexture:
		decodeMipTexture(virtualPath, bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::Quake2Wal:
		decodeQuake2Wal(virtualPath, bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::Quake2M8:
		decodeQuake2M8(virtualPath, bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::Quake2M32:
		decodeQuake2M32(virtualPath, bytes, &result, budget);
		break;
	case IdTechImageFormat::QuakeSprite:
	case IdTechImageFormat::HalfLifeSprite:
		decodeSprite(bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::Quake2Sprite:
		decodeQuake2Sprite(virtualPath, bytes, effective, context, &result, budget);
		break;
	case IdTechImageFormat::DoomPatch:
		decodeDoomPatch(bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::DoomFlat:
		decodeDoomFlat(bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::DoomPalette:
		decodeDoomPaletteLump(bytes, &result, budget);
		break;
	case IdTechImageFormat::DoomColormap:
		decodeDoomColormapLump(bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::Raw:
		decodeRawIndexed(bytes, effective, &result, budget);
		break;
	case IdTechImageFormat::Unknown:
		result.error = QCoreApplication::translate("VibeStudioIdTechImage", "No idTech image decoder recognised this payload.");
		break;
	}

	if (!budget.checkpoint()) {
		result.decoded = false;
		result.error = QCoreApplication::translate("VibeStudioIdTechImage", "Image decoding cancelled.");
	}
	if (!result.decoded && result.error.isEmpty()) {
		result.error = QCoreApplication::translate("VibeStudioIdTechImage", "Decoding failed.");
	}
	if (!result.decoded) { result.image = {}; result.mipLevels.clear(); result.frames.clear(); }
	if (result.decoded && result.paletted && result.paletteSourceVirtualPath.isEmpty() && !effective.generated) {
		result.paletteSourceVirtualPath = effective.sourceDescription;
	}
	return result;
}

IdTechImageDecodeResult decodeIdTechImageFromArchive(const PackageArchiveReader& archive, const QString& virtualPath, const QString& paletteId, IdTechPaletteResolution* resolutionOut)
{
	QByteArray bytes;
	QString error;
	if (!readIdTechImageEntry(archive, virtualPath, &bytes, &error)) {
		IdTechImageDecodeResult result;
		result.format = IdTechImageFormat::Unknown;
		applyFormatLabels(&result);
		result.error = error.isEmpty() ? QCoreApplication::translate("VibeStudioIdTechImage", "Unable to read the package entry.") : error;
		if (resolutionOut) {
			*resolutionOut = resolveIdTechPalette(archive, paletteId);
		}
		return result;
	}
	// A WAD flat is a flat because of its namespace marker, not its bytes.
	QString decodePath = virtualPath;
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.typeHint == QStringLiteral("wad-flat") && entry.virtualPath.compare(virtualPath, Qt::CaseInsensitive) == 0) {
			decodePath = QStringLiteral("flats/") + virtualPath;
			break;
		}
	}
	// With no palette asked for, the one the entry's format implies, read out
	// of the package when it ships one: what the package preview shows.
	const QString resolvedId = paletteId.trimmed().isEmpty() ? defaultIdTechPaletteIdForImage(detectIdTechImageFormat(decodePath, bytes), bytes.size()) : paletteId;
	const IdTechPaletteResolution resolution = resolveIdTechPalette(archive, resolvedId);
	if (resolutionOut) {
		*resolutionOut = resolution;
	}

	// The archive doubles as the source for formats that reference other
	// entries, such as the external frame images named by a .sp2 sprite.
	IdTechImageDecodeContext context;
	context.archive = &archive;
	IdTechImageDecodeResult result = decodeIdTechImage(decodePath, bytes, resolution.palette, context);
	if (result.paletted) {
		result.paletteSourceVirtualPath = resolution.fromPackage ? resolution.sourceVirtualPath : QString();
		result.paletteGenerated = resolution.palette.generated;
	}
	result.warnings += resolution.warnings;
	return result;
}

// ---------------------------------------------------------------------------
// Quantization and swatches
// ---------------------------------------------------------------------------

QImage quantizeToIdTechPalette(const QImage& source, const IdTechPalette& palette, bool dither, const std::function<bool(int, int)>& progress)
{
	if (source.isNull() || !palette.isValid()) {
		return {};
	}
	const QImage rgb = source.convertToFormat(QImage::Format_ARGB32);
	const int width = rgb.width();
	const int height = rgb.height();

	QImage output(width, height, QImage::Format_Indexed8);
	if (output.isNull()) {
		return {};
	}
	output.setColorTable(paletteColorTable(palette, palette.transparentIndex >= 0));

	const int transparentIndex = palette.transparentIndex;
	QVector<int> reds(256, 0);
	QVector<int> greens(256, 0);
	QVector<int> blues(256, 0);
	for (int index = 0; index < 256; ++index) {
		const QRgb color = palette.colorAt(index);
		reds[index] = qRed(color);
		greens[index] = qGreen(color);
		blues[index] = qBlue(color);
	}

	auto nearestIndex = [&](double r, double g, double b) -> int {
		int best = 0;
		double bestDistance = -1.0;
		for (int index = 0; index < 256; ++index) {
			if (index == transparentIndex) {
				continue;
			}
			const double dr = r - reds.at(index);
			const double dg = g - greens.at(index);
			const double db = b - blues.at(index);
			// Weighted squared distance keeps the match perceptually sane while
			// still matching exact colours with distance zero.
			const double distance = 2.0 * dr * dr + 4.0 * dg * dg + 3.0 * db * db;
			if (bestDistance < 0.0 || distance < bestDistance) {
				bestDistance = distance;
				best = index;
				if (distance == 0.0) { break; }
			}
		}
		return best;
	};

	if (!dither) {
		// Repeated texels are common in game art. Bound the exact-color cache so
		// photographs cannot grow it to one entry per pixel.
		QHash<QRgb, uchar> matches;
		for (int y = 0; y < height; ++y) {
			if (progress && !progress(y, height)) { return {}; }
			const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
			uchar* destination = output.scanLine(y);
			for (int x = 0; x < width; ++x) {
				const QRgb color = line[x];
				if (transparentIndex >= 0 && qAlpha(color) == 0) {
					destination[x] = static_cast<uchar>(transparentIndex);
					continue;
				}
				const QRgb rgbKey = color & 0x00ffffffu;
				const auto found = matches.constFind(rgbKey);
				if (found != matches.constEnd()) { destination[x] = found.value(); continue; }
				const auto index = static_cast<uchar>(nearestIndex(qRed(color), qGreen(color), qBlue(color)));
				destination[x] = index;
				if (matches.size() < 65536) { matches.insert(rgbKey, index); }
			}
		}
		if (progress && !progress(height, height)) { return {}; }
		return output;
	}

	// Floyd-Steinberg error diffusion over two rolling error rows.
	QVector<double> currentError(static_cast<qsizetype>(width) * 3, 0.0);
	QVector<double> nextError(static_cast<qsizetype>(width) * 3, 0.0);
	for (int y = 0; y < height; ++y) {
		if (progress && !progress(y, height)) { return {}; }
		const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
		uchar* destination = output.scanLine(y);
		std::fill(nextError.begin(), nextError.end(), 0.0);
		for (int x = 0; x < width; ++x) {
			const QRgb color = line[x];
			if (transparentIndex >= 0 && qAlpha(color) == 0) {
				destination[x] = static_cast<uchar>(transparentIndex);
				continue;
			}
			const qsizetype base = static_cast<qsizetype>(x) * 3;
			const double r = std::clamp(qRed(color) + currentError.at(base), 0.0, 255.0);
			const double g = std::clamp(qGreen(color) + currentError.at(base + 1), 0.0, 255.0);
			const double b = std::clamp(qBlue(color) + currentError.at(base + 2), 0.0, 255.0);
			const int index = nearestIndex(r, g, b);
			destination[x] = static_cast<uchar>(index);
			const double errorR = r - reds.at(index);
			const double errorG = g - greens.at(index);
			const double errorB = b - blues.at(index);
			auto diffuse = [&](QVector<double>* target, int column, double factor) {
				if (column < 0 || column >= width) {
					return;
				}
				const qsizetype at = static_cast<qsizetype>(column) * 3;
				(*target)[at] += errorR * factor;
				(*target)[at + 1] += errorG * factor;
				(*target)[at + 2] += errorB * factor;
			};
			diffuse(&currentError, x + 1, 7.0 / 16.0);
			diffuse(&nextError, x - 1, 3.0 / 16.0);
			diffuse(&nextError, x, 5.0 / 16.0);
			diffuse(&nextError, x + 1, 1.0 / 16.0);
		}
		currentError = nextError;
	}
	if (progress && !progress(height, height)) { return {}; }
	return output;
}

QImage renderIdTechPaletteSwatch(const IdTechPalette& palette, int cellSize)
{
	if (!palette.isValid()) {
		return {};
	}
	const int cell = std::clamp(cellSize, 1, 256);
	QImage image(cell * 16, cell * 16, QImage::Format_ARGB32);
	if (image.isNull()) {
		return {};
	}
	image.fill(Qt::transparent);

	QPainter painter(&image);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	for (int index = 0; index < 256; ++index) {
		const int column = index % 16;
		const int row = index / 16;
		painter.fillRect(column * cell, row * cell, cell, cell, QColor::fromRgba(palette.colorAt(index)));
	}
	painter.end();
	return image;
}

// ---------------------------------------------------------------------------
// Summaries
// ---------------------------------------------------------------------------

QStringList idTechPaletteSummaryLines(const IdTechPaletteResolution& resolution)
{
	QStringList lines;
	const IdTechPalette& palette = resolution.palette;
	lines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette: %1").arg(palette.displayName.isEmpty() ? palette.id : palette.displayName);
	lines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette id: %1").arg(palette.id);
	if (!palette.generated && !resolution.sourceVirtualPath.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Source: %1").arg(resolution.sourceVirtualPath);
	} else if (palette.generated) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Source: generated stand-in (no game palette found)");
	} else {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Source: %1").arg(palette.sourceDescription.isEmpty() ? QCoreApplication::translate("VibeStudioIdTechImage", "embedded or supplied palette") : palette.sourceDescription);
	}
	lines << QCoreApplication::translate("VibeStudioIdTechImage", "Entries: %1").arg(palette.colors.size());
	lines << (palette.transparentIndex >= 0
		? QCoreApplication::translate("VibeStudioIdTechImage", "Transparent index: %1").arg(palette.transparentIndex)
		: QCoreApplication::translate("VibeStudioIdTechImage", "Transparent index: none"));
	lines << (palette.fullbrightStartIndex >= 0
		? QCoreApplication::translate("VibeStudioIdTechImage", "Fullbright range starts at index %1").arg(palette.fullbrightStartIndex)
		: QCoreApplication::translate("VibeStudioIdTechImage", "Fullbright range: none"));
	if (!resolution.searchedPaths.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Searched: %1").arg(resolution.searchedPaths.join(QStringLiteral(", ")));
	}
	for (const QString& warning : resolution.warnings) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Warning: %1").arg(warning);
	}
	return lines;
}

QStringList idTechImageSummaryLines(const IdTechImageDecodeResult& result)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioIdTechImage", "Format: %1 [%2]").arg(result.formatName.isEmpty() ? idTechImageFormatDisplayName(result.format) : result.formatName,
		result.formatId.isEmpty() ? idTechImageFormatId(result.format) : result.formatId);
	if (!result.decoded) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Decoded: no");
		if (!result.error.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioIdTechImage", "Error: %1").arg(result.error);
		}
		for (const QString& warning : result.warnings) {
			lines << QCoreApplication::translate("VibeStudioIdTechImage", "Warning: %1").arg(warning);
		}
		return lines;
	}

	lines << QCoreApplication::translate("VibeStudioIdTechImage", "Dimensions: %1x%2").arg(result.width).arg(result.height);
	if (result.leftOffset != 0 || result.topOffset != 0) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Offsets: %1, %2").arg(result.leftOffset).arg(result.topOffset);
	}
	if (!result.textureName.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Texture name: %1").arg(result.textureName);
	}
	if (!result.animationNextName.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Animation next: %1").arg(result.animationNextName);
	}
	if (result.paletted) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette: %1%2")
			.arg(result.paletteId.isEmpty() ? QCoreApplication::translate("VibeStudioIdTechImage", "unknown") : result.paletteId,
				result.paletteGenerated ? QCoreApplication::translate("VibeStudioIdTechImage", " (generated stand-in)") : QString());
		if (!result.paletteSourceVirtualPath.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioIdTechImage", "Palette source: %1").arg(result.paletteSourceVirtualPath);
		}
	}
	lines << (result.hasTransparency ? QCoreApplication::translate("VibeStudioIdTechImage", "Transparency: yes") : QCoreApplication::translate("VibeStudioIdTechImage", "Transparency: no"));
	if (!result.mipLevels.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Mip levels: %1").arg(result.mipLevels.size());
	}
	if (!result.frames.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Frames: %1").arg(result.frames.size());
	}
	if (result.externalFrames) {
		int resolved = 0;
		for (const IdTechImageFrame& frame : result.frames) {
			if (!frame.image.isNull()) {
				++resolved;
			}
		}
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Frame images: external, %1 of %2 resolved").arg(resolved).arg(result.frames.size());
	}
	if (result.surfaceFlags != 0 || result.contentFlags != 0 || result.surfaceValue != 0) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface flags: 0x%1").arg(result.surfaceFlags, 8, 16, QLatin1Char('0'));
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Content flags: 0x%1").arg(result.contentFlags, 8, 16, QLatin1Char('0'));
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Surface value: %1").arg(result.surfaceValue);
	}
	lines += result.detailLines;
	for (const QString& warning : result.warnings) {
		lines << QCoreApplication::translate("VibeStudioIdTechImage", "Warning: %1").arg(warning);
	}
	return lines;
}

} // namespace vibestudio
