// Deterministic fuzz harness for every VibeStudio parser that reads
// attacker-controlled bytes.
//
// For each target the test builds valid seed inputs in memory, derives a fixed
// corpus of corrupted ones with core/parser_fuzz.h, and then asserts only what
// must always hold: the parser returns, it does not crash or hang, on failure
// it reports a non-empty error instead of claiming success, and any size or
// count it reports is consistent with the buffer it was handed.
//
// Nothing here is random at run time. The seed is the compile-time constant
// kDefaultSeed unless VIBESTUDIO_FUZZ_SEED overrides it, and every failure
// prints the seed and the case id so a human can re-run exactly that case:
//
//     VIBESTUDIO_FUZZ_SEED=0x... meson test parser-fuzz
//
// All fixtures are assembled from published format layouts; no commercial game
// data is embedded or read.

#include "core/bsp_inspect.h"
#include "core/deflate.h"
#include "core/idtech_image.h"
#include "core/level_map.h"
#include "core/model_mesh.h"
#include "core/package_archive.h"
#include "core/parser_fuzz.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLoggingCategory>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

// Change this to walk a different corpus; CI always runs this exact value.
constexpr quint64 kDefaultSeed = 0x5642f7a91d3c0007ull;
constexpr const char* kSeedVariable = "VIBESTUDIO_FUZZ_SEED";

// Case counts are per target and fixed. Almost all of this test's wall clock
// goes on writing one fixture file per case for the two targets that can only
// be driven through a path, so those get the fewest cases and the byte-level
// targets, which cost microseconds each, get the most. The whole run is about
// two seconds, of which roughly nine tenths is filesystem.
constexpr int kDeflateCases = 512;
constexpr int kImageCases = 384;
constexpr int kBspCases = 256;
constexpr int kModelCases = 256;
// Path-driven: one file write per case.
constexpr int kPackageCases = 96;
constexpr int kLevelMapCases = 96;

// The decoder's own ceiling, from src/core/idtech_image.cpp.
constexpr qint64 kMaxDecodedPixels = 4096ll * 4096ll;

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

// Feeding mutated bytes to a decoder is supposed to produce complaints, and Qt
// routes most of them through qWarning. They would bury the one message that
// matters, so they are dropped for the duration of the run; anything this test
// wants to say it writes to std::cerr itself. A handful of "libpng error"
// lines still appear, because libpng writes those itself rather than through
// Qt's logging; they come from the mutated PNG seed and are expected.
void silenceDecoderWarnings(QtMsgType, const QMessageLogContext&, const QString&)
{
}

// ---------------------------------------------------------------------------
// Little-endian writers shared by every fixture builder.
// ---------------------------------------------------------------------------

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

void appendFloat(QByteArray* data, float value)
{
	quint32 raw = 0;
	std::memcpy(&raw, &value, sizeof(raw));
	appendLe32(data, raw);
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

QByteArray zeros(int count)
{
	return QByteArray(count, '\0');
}

// Deterministic filler so the seeds themselves never depend on the host.
QByteArray filler(int size, quint32 state)
{
	QByteArray bytes;
	bytes.reserve(size);
	for (int index = 0; index < size; ++index) {
		state = state * 1664525u + 1013904223u;
		bytes.append(static_cast<char>(static_cast<quint8>((state >> 24) & 0xffu)));
	}
	return bytes;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	const bool written = file.write(bytes) == bytes.size();
	file.close();
	return written;
}

// ---------------------------------------------------------------------------
// The driver. Every target funnels through here so failures always print the
// seed and the case id.
// ---------------------------------------------------------------------------

using CaseCheck = std::function<QString(const FuzzCase&)>;

// FNV-1a over the target name, so each target walks its own corpus while the
// run as a whole is still described by one seed. Named here rather than
// XOR-ing a magic number at each call site, so the seed printed on failure is
// always the one a human should put in the environment variable.
// Reference: Fowler/Noll/Vo, http://www.isthe.com/chongo/tech/comp/fnv/
quint64 targetSalt(const char* targetName)
{
	quint64 hash = 0xcbf29ce484222325ull;
	for (const char* cursor = targetName; *cursor != '\0'; ++cursor) {
		hash ^= static_cast<quint64>(static_cast<unsigned char>(*cursor));
		hash *= 0x100000001b3ull;
	}
	return hash;
}

bool runTarget(const char* targetName, const QVector<QByteArray>& seeds, quint64 runSeed, int caseCount, const CaseCheck& check)
{
	const quint64 corpusSeed = runSeed ^ targetSalt(targetName);
	const QVector<FuzzCase> corpus = buildFuzzCorpus(seeds, corpusSeed, caseCount);
	if (corpus.size() != caseCount) {
		std::cerr << "fuzz target " << targetName << ": expected " << caseCount
			<< " cases but the generator produced " << corpus.size() << "\n";
		return false;
	}

	int failures = 0;
	for (const FuzzCase& generated : corpus) {
		const QString problem = check(generated);
		if (problem.isEmpty()) {
			continue;
		}
		++failures;
		if (failures <= 5) {
			std::cerr << "fuzz target " << targetName << " failed\n"
				<< "  re-run:  " << kSeedVariable << "=0x" << std::hex << runSeed << std::dec << "\n"
				<< "  case:    " << qUtf8Printable(generated.id) << "\n"
				<< "  bytes:   " << generated.data.size() << "\n"
				<< "  problem: " << qUtf8Printable(problem) << "\n";
		}
	}
	if (failures > 5) {
		std::cerr << "fuzz target " << targetName << ": " << failures << " failures in total\n";
	}
	return failures == 0;
}

// ---------------------------------------------------------------------------
// DEFLATE seeds. RFC 1951 / RFC 1950.
// ---------------------------------------------------------------------------

QVector<QByteArray> deflateSeeds()
{
	QVector<QByteArray> seeds;
	QByteArray repetitive;
	for (int index = 0; index < 300; ++index) {
		repetitive.append("maps/base1.bsp textures/e1u1/metal1_1 ");
	}
	seeds.push_back(deflateRaw(QByteArrayLiteral("abc"), DeflateLevel::Default));
	seeds.push_back(deflateRaw(repetitive, DeflateLevel::Default));
	seeds.push_back(deflateRaw(repetitive, DeflateLevel::Fast));
	seeds.push_back(deflateRaw(filler(4096, 0x51u), DeflateLevel::Default));
	seeds.push_back(deflateRaw(filler(3000, 0x99u), DeflateLevel::Store));
	// The canonical two-byte empty fixed-Huffman stream, and a stored block.
	seeds.push_back(QByteArray::fromHex("0300"));
	QByteArray stored = QByteArray::fromHex("010200fdff");
	stored.append("hi");
	seeds.push_back(stored);
	return seeds;
}

// RFC 1950 wrapper: 0x78 0x9c, raw deflate, big-endian Adler-32.
QByteArray zlibWrap(const QByteArray& rawStream, const QByteArray& payload)
{
	QByteArray bytes;
	bytes.append(static_cast<char>(0x78));
	bytes.append(static_cast<char>(0x9c));
	bytes.append(rawStream);
	const quint32 adler = adler32Bytes(payload);
	bytes.append(static_cast<char>((adler >> 24) & 0xff));
	bytes.append(static_cast<char>((adler >> 16) & 0xff));
	bytes.append(static_cast<char>((adler >> 8) & 0xff));
	bytes.append(static_cast<char>(adler & 0xff));
	return bytes;
}

QVector<QByteArray> zlibSeeds()
{
	QVector<QByteArray> seeds;
	const QByteArray payloads[3] = {
		QByteArrayLiteral("abc"),
		QByteArray(2048, 'q'),
		filler(1500, 0x2du),
	};
	for (const QByteArray& payload : payloads) {
		seeds.push_back(zlibWrap(deflateRaw(payload, DeflateLevel::Default), payload));
		seeds.push_back(zlibWrap(deflateRaw(payload, DeflateLevel::Store), payload));
	}
	return seeds;
}

QString checkInflate(const InflateResult& result, qsizetype inputSize, bool zlib)
{
	if (!result.ok && result.error.isEmpty()) {
		return QStringLiteral("a rejected stream reported no error");
	}
	if (result.ok && !result.error.isEmpty()) {
		return QStringLiteral("a successful inflate carried an error message");
	}
	if (result.bytesConsumed < 0 || result.bytesConsumed > inputSize) {
		return QStringLiteral("bytesConsumed %1 is outside the %2 byte input").arg(result.bytesConsumed).arg(inputSize);
	}
	if (!result.ok && !result.data.isEmpty()) {
		return QStringLiteral("a rejected stream still returned %1 bytes of output").arg(result.data.size());
	}
	// A conforming deflate stream cannot expand by more than about 1032:1, and
	// the decoder documents a 1 GiB ceiling, so anything beyond that means the
	// unbounded-growth guard did not fire.
	const qint64 ceiling = qMin<qint64>(qint64(1) << 30, (static_cast<qint64>(inputSize) + 16) * 1100);
	if (result.data.size() > ceiling) {
		return QStringLiteral("output of %1 bytes from a %2 byte %3 stream exceeds the expansion ceiling")
			.arg(result.data.size()).arg(inputSize).arg(zlib ? QStringLiteral("zlib") : QStringLiteral("raw"));
	}
	return {};
}

// ---------------------------------------------------------------------------
// idTech image seeds. Layouts as documented at the top of core/idtech_image.h.
// ---------------------------------------------------------------------------

QByteArray buildDoomPatch(int width, int height)
{
	QVector<QByteArray> columns;
	for (int column = 0; column < width; ++column) {
		QByteArray post;
		post.append(static_cast<char>(0));            // topdelta
		post.append(static_cast<char>(height));       // length
		post.append(static_cast<char>(0));            // leading pad
		for (int row = 0; row < height; ++row) {
			post.append(static_cast<char>((column * 8 + row) & 0x7f));
		}
		post.append(static_cast<char>(0));            // trailing pad
		post.append(static_cast<char>(0xff));         // column terminator
		columns.push_back(post);
	}

	QByteArray bytes;
	appendLe16(&bytes, static_cast<quint16>(width));
	appendLe16(&bytes, static_cast<quint16>(height));
	appendLe16(&bytes, 2);
	appendLe16(&bytes, 3);
	const qsizetype table = bytes.size();
	for (int column = 0; column < width; ++column) {
		appendLe32(&bytes, 0);
	}
	for (int column = 0; column < width; ++column) {
		writeLe32At(&bytes, table + column * 4, static_cast<quint32>(bytes.size()));
		bytes.append(columns.at(column));
	}
	return bytes;
}

QByteArray buildQuakeLump(int width, int height)
{
	QByteArray bytes;
	appendLe32(&bytes, static_cast<quint32>(width));
	appendLe32(&bytes, static_cast<quint32>(height));
	bytes.append(filler(width * height, 0x1234u));
	return bytes;
}

QByteArray buildMipTexture(const QByteArray& name, int width, int height, bool wad3Palette)
{
	QByteArray bytes;
	bytes.append(fixedName(name, 16));
	appendLe32(&bytes, static_cast<quint32>(width));
	appendLe32(&bytes, static_cast<quint32>(height));
	const quint32 offset0 = 40;
	const quint32 offset1 = offset0 + static_cast<quint32>(width * height);
	const quint32 offset2 = offset1 + static_cast<quint32>((width / 2) * (height / 2));
	const quint32 offset3 = offset2 + static_cast<quint32>((width / 4) * (height / 4));
	appendLe32(&bytes, offset0);
	appendLe32(&bytes, offset1);
	appendLe32(&bytes, offset2);
	appendLe32(&bytes, offset3);
	bytes.append(filler(width * height, 1u));
	bytes.append(filler((width / 2) * (height / 2), 2u));
	bytes.append(filler((width / 4) * (height / 4), 3u));
	bytes.append(filler((width / 8) * (height / 8), 4u));
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

QByteArray buildQuake2Wal(int width, int height)
{
	QByteArray bytes;
	bytes.append(fixedName("e1u1/metal1_1", 32));
	appendLe32(&bytes, static_cast<quint32>(width));
	appendLe32(&bytes, static_cast<quint32>(height));
	const quint32 offset0 = 100;
	const quint32 offset1 = offset0 + static_cast<quint32>(width * height);
	const quint32 offset2 = offset1 + static_cast<quint32>((width / 2) * (height / 2));
	const quint32 offset3 = offset2 + static_cast<quint32>((width / 4) * (height / 4));
	appendLe32(&bytes, offset0);
	appendLe32(&bytes, offset1);
	appendLe32(&bytes, offset2);
	appendLe32(&bytes, offset3);
	bytes.append(fixedName("e1u1/metal1_2", 32));
	appendLe32(&bytes, 0x79u);
	appendLe32(&bytes, 0x01u);
	appendLe32(&bytes, 42u);
	bytes.append(filler(width * height, 5u));
	bytes.append(filler((width / 2) * (height / 2), 6u));
	bytes.append(filler((width / 4) * (height / 4), 7u));
	bytes.append(filler((width / 8) * (height / 8), 8u));
	return bytes;
}

// ZSoft PCX, RLE encoded: a control byte with its top two bits set carries a
// run length in its low six bits, followed by the repeated value.
void appendPcxRow(QByteArray* data, const QByteArray& row)
{
	qsizetype index = 0;
	while (index < row.size()) {
		const char value = row.at(index);
		qsizetype run = 1;
		while (index + run < row.size() && row.at(index + run) == value && run < 63) {
			++run;
		}
		const auto raw = static_cast<quint8>(value);
		if (run > 1 || (raw & 0xc0) == 0xc0) {
			data->append(static_cast<char>(0xc0 | static_cast<quint8>(run)));
			data->append(value);
		} else {
			data->append(value);
		}
		index += run;
	}
}

QByteArray buildPcx(int width, int height)
{
	QByteArray header(128, '\0');
	header[0] = static_cast<char>(0x0a);   // ZSoft manufacturer byte
	header[1] = static_cast<char>(5);      // version
	header[2] = static_cast<char>(1);      // RLE encoding
	header[3] = static_cast<char>(8);      // bits per pixel per plane
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
	header[65] = static_cast<char>(1);     // planes
	header[66] = static_cast<char>(width & 0xff);
	header[67] = static_cast<char>((width >> 8) & 0xff);
	header[68] = static_cast<char>(1);     // palette info

	QByteArray bytes = header;
	for (int y = 0; y < height; ++y) {
		QByteArray row;
		for (int x = 0; x < width; ++x) {
			row.append(static_cast<char>(((y * 7) + (x / 3)) & 0xff));
		}
		appendPcxRow(&bytes, row);
	}
	bytes.append(static_cast<char>(0x0c));  // 256-colour tail palette marker
	for (int index = 0; index < 256; ++index) {
		bytes.append(static_cast<char>(index));
		bytes.append(static_cast<char>((index * 2) & 0xff));
		bytes.append(static_cast<char>((index * 3) & 0xff));
	}
	return bytes;
}

QByteArray targaHeaderBytes(int imageType, int width, int height, int depth, int descriptor)
{
	QByteArray bytes;
	bytes.append(static_cast<char>(0));    // id length
	bytes.append(static_cast<char>(0));    // colour map type
	bytes.append(static_cast<char>(imageType));
	appendLe16(&bytes, 0);                 // colour map origin
	appendLe16(&bytes, 0);                 // colour map length
	bytes.append(static_cast<char>(0));    // colour map entry size
	appendLe16(&bytes, 0);                 // x origin
	appendLe16(&bytes, 0);                 // y origin
	appendLe16(&bytes, static_cast<quint16>(width));
	appendLe16(&bytes, static_cast<quint16>(height));
	bytes.append(static_cast<char>(depth));
	bytes.append(static_cast<char>(descriptor));
	return bytes;
}

QByteArray buildUncompressedTarga(int width, int height)
{
	QByteArray bytes = targaHeaderBytes(2, width, height, 24, 0x20);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			bytes.append(static_cast<char>((x * 3) & 0xff));
			bytes.append(static_cast<char>((y * 5) & 0xff));
			bytes.append(static_cast<char>((x + y) & 0xff));
		}
	}
	return bytes;
}

QByteArray buildRleTarga(int width, int height)
{
	// Each row is one run packet covering the whole row, so the row width has to
	// stay at or below 128 pixels (the run packet count field is 7 bits).
	QByteArray bytes = targaHeaderBytes(10, width, height, 24, 0x20);
	for (int y = 0; y < height; ++y) {
		bytes.append(static_cast<char>(0x80 | static_cast<quint8>(width - 1)));
		bytes.append(static_cast<char>((y * 9) & 0xff));
		bytes.append(static_cast<char>((y * 5) & 0xff));
		bytes.append(static_cast<char>((y * 3) & 0xff));
	}
	return bytes;
}

QByteArray buildQuakeSprite()
{
	QByteArray bytes;
	bytes.append("IDSP", 4);
	appendLe32(&bytes, 1);      // version
	appendLe32(&bytes, 2);      // VP_PARALLEL
	appendFloat(&bytes, 3.5f);  // bounding radius
	appendLe32(&bytes, 8);      // max width
	appendLe32(&bytes, 8);      // max height
	appendLe32(&bytes, 2);      // frame count
	appendFloat(&bytes, 0.0f);  // beam length
	appendLe32(&bytes, 0);      // sync type
	for (int frame = 0; frame < 2; ++frame) {
		appendLe32(&bytes, 0);  // SPR_SINGLE
		appendLe32(&bytes, static_cast<quint32>(-4));
		appendLe32(&bytes, 4);
		appendLe32(&bytes, 8);
		appendLe32(&bytes, 8);
		bytes.append(filler(64, static_cast<quint32>(frame) + 11u));
	}
	return bytes;
}

QByteArray buildPalette768()
{
	QByteArray bytes;
	for (int index = 0; index < 256; ++index) {
		bytes.append(static_cast<char>(index));
		bytes.append(static_cast<char>((index * 5) & 0xff));
		bytes.append(static_cast<char>((index * 11) & 0xff));
	}
	return bytes;
}

QByteArray buildPng()
{
	QImage image(8, 8, QImage::Format_ARGB32);
	image.fill(0xff204060u);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	buffer.close();
	return bytes;
}

struct ImageSeed {
	QString virtualPath;
	QByteArray bytes;
};

QVector<ImageSeed> imageSeeds()
{
	QVector<ImageSeed> seeds;
	seeds.push_back({QStringLiteral("patches/WALL01.lmp"), buildDoomPatch(16, 16)});
	seeds.push_back({QStringLiteral("flats/FLOOR0_1"), QByteArray(4096, '\x37')});
	seeds.push_back({QStringLiteral("gfx/conchars.lmp"), buildQuakeLump(32, 16)});
	seeds.push_back({QStringLiteral("textures/city1_1"), buildMipTexture("city1_1", 32, 32, false)});
	seeds.push_back({QStringLiteral("textures/hl_wall"), buildMipTexture("hl_wall", 32, 32, true)});
	seeds.push_back({QStringLiteral("textures/e1u1/metal1_1.wal"), buildQuake2Wal(32, 32)});
	seeds.push_back({QStringLiteral("progs/s_bubble.spr"), buildQuakeSprite()});
	seeds.push_back({QStringLiteral("pics/colormap.pcx"), buildPcx(24, 12)});
	seeds.push_back({QStringLiteral("textures/plain.tga"), buildUncompressedTarga(16, 8)});
	seeds.push_back({QStringLiteral("textures/runs.tga"), buildRleTarga(16, 8)});
	seeds.push_back({QStringLiteral("PLAYPAL"), buildPalette768()});
	seeds.push_back({QStringLiteral("COLORMAP"), QByteArray(34 * 256, '\x11')});
	seeds.push_back({QStringLiteral("textures/preview.png"), buildPng()});
	return seeds;
}

QString checkImageResult(const IdTechImageDecodeResult& result)
{
	if (!result.decoded && result.error.isEmpty()) {
		return QStringLiteral("a rejected image reported no error");
	}
	if (!result.decoded) {
		return {};
	}
	if (result.width < 0 || result.height < 0) {
		return QStringLiteral("decoded image reports a negative size (%1x%2)").arg(result.width).arg(result.height);
	}
	if (static_cast<qint64>(result.width) * static_cast<qint64>(result.height) > kMaxDecodedPixels) {
		return QStringLiteral("decoded image of %1x%2 is past the decode cap").arg(result.width).arg(result.height);
	}
	if (result.image.isNull() && result.frames.isEmpty() && result.mipLevels.isEmpty()) {
		return QStringLiteral("claimed success but produced no image, frame or mip level");
	}
	if (!result.image.isNull()
		&& static_cast<qint64>(result.image.width()) * result.image.height() > kMaxDecodedPixels) {
		return QStringLiteral("produced an image past the decode cap");
	}
	for (const QImage& mip : result.mipLevels) {
		if (mip.isNull()) {
			return QStringLiteral("claimed success but produced a null mip level");
		}
	}
	for (const IdTechImageFrame& frame : result.frames) {
		if (frame.image.isNull()) {
			return QStringLiteral("claimed success but produced a null frame image");
		}
	}
	return {};
}

// ---------------------------------------------------------------------------
// BSP seeds. Layouts as documented at the top of core/bsp_inspect.h.
// ---------------------------------------------------------------------------

QByteArray buildBspFile(const QByteArray& headerPrefix, int lumpCount, const QVector<QByteArray>& payloads)
{
	const auto headerBytes = static_cast<quint32>(headerPrefix.size() + lumpCount * 8);
	QByteArray table;
	QByteArray body;
	for (int index = 0; index < lumpCount; ++index) {
		const QByteArray data = index < payloads.size() ? payloads.at(index) : QByteArray();
		appendLe32(&table, headerBytes + static_cast<quint32>(body.size()));
		appendLe32(&table, static_cast<quint32>(data.size()));
		body.append(data);
	}
	return headerPrefix + table + body;
}

QByteArray buildQuakeBsp()
{
	QByteArray header;
	appendLe32(&header, 29);
	QVector<QByteArray> payloads(15);
	payloads[0] = QByteArray("{\n\"classname\" \"worldspawn\"\n\"message\" \"Fuzz Room\"\n}\n");
	payloads[1] = zeros(20 * 2);
	QByteArray miptex;
	appendLe32(&miptex, 1);
	appendLe32(&miptex, 4);
	miptex.append(fixedName("city1_1", 16));
	appendLe32(&miptex, 32);
	appendLe32(&miptex, 32);
	appendLe32(&miptex, 0);
	appendLe32(&miptex, 0);
	appendLe32(&miptex, 0);
	appendLe32(&miptex, 0);
	payloads[2] = miptex;
	QByteArray vertices;
	for (int index = 0; index < 4; ++index) {
		appendFloat(&vertices, static_cast<float>(index * 16));
		appendFloat(&vertices, static_cast<float>(index * 8));
		appendFloat(&vertices, 0.0f);
	}
	payloads[3] = vertices;
	payloads[4] = zeros(16);
	payloads[5] = zeros(24);
	QByteArray texinfo;
	texinfo.append(zeros(32));
	appendLe32(&texinfo, 0);
	appendLe32(&texinfo, 0);
	payloads[6] = texinfo;
	QByteArray faces;
	faces.append(zeros(16));
	appendLe32(&faces, 0);
	payloads[7] = faces;
	payloads[8] = zeros(256);
	payloads[9] = zeros(8);
	payloads[10] = zeros(28 * 2);
	payloads[11] = zeros(2 * 4);
	payloads[12] = zeros(4 * 4);
	payloads[13] = zeros(4 * 4);
	QByteArray models;
	appendFloat(&models, -64.0f);
	appendFloat(&models, -64.0f);
	appendFloat(&models, -32.0f);
	appendFloat(&models, 64.0f);
	appendFloat(&models, 64.0f);
	appendFloat(&models, 32.0f);
	models.append(zeros(64 - 24));
	payloads[14] = models;
	return buildBspFile(header, 15, payloads);
}

QByteArray buildQuake2Bsp()
{
	QByteArray header("IBSP");
	appendLe32(&header, 38);
	QVector<QByteArray> payloads(19);
	payloads[0] = QByteArray("{\n\"classname\" \"worldspawn\"\n}\n");
	payloads[1] = zeros(20 * 2);
	payloads[2] = zeros(12 * 3);
	payloads[3] = zeros(32);
	payloads[4] = zeros(28);
	QByteArray texinfo;
	texinfo.append(zeros(32));
	appendLe32(&texinfo, 8);
	appendLe32(&texinfo, 0);
	texinfo.append(fixedName("e1u1/metal1_1", 32));
	appendLe32(&texinfo, static_cast<quint32>(-1));
	payloads[5] = texinfo;
	payloads[6] = zeros(20 * 2);
	payloads[7] = zeros(96);
	payloads[8] = zeros(28 * 2);
	QByteArray models;
	appendFloat(&models, -128.0f);
	appendFloat(&models, -128.0f);
	appendFloat(&models, -64.0f);
	appendFloat(&models, 128.0f);
	appendFloat(&models, 128.0f);
	appendFloat(&models, 64.0f);
	models.append(zeros(48 - 24));
	payloads[13] = models;
	payloads[14] = zeros(12 * 2);
	return buildBspFile(header, 19, payloads);
}

QByteArray buildQuake3Bsp()
{
	QByteArray header("IBSP");
	appendLe32(&header, 46);
	QVector<QByteArray> payloads(17);
	payloads[0] = QByteArray("{\n\"classname\" \"worldspawn\"\n}\n");
	QByteArray shaders;
	shaders.append(fixedName("textures/base_wall/c_met5_2", 64));
	appendLe32(&shaders, 4);
	appendLe32(&shaders, 1);
	payloads[1] = shaders;
	payloads[2] = zeros(16 * 2);
	payloads[3] = zeros(36);
	payloads[4] = zeros(48 * 2);
	QByteArray models;
	appendFloat(&models, -256.0f);
	appendFloat(&models, -256.0f);
	appendFloat(&models, -128.0f);
	appendFloat(&models, 256.0f);
	appendFloat(&models, 256.0f);
	appendFloat(&models, 128.0f);
	models.append(zeros(40 - 24));
	payloads[7] = models;
	payloads[8] = zeros(12 * 2);
	payloads[10] = zeros(44 * 3);
	QByteArray surfaces;
	appendLe32(&surfaces, 0);
	surfaces.append(zeros(100));
	payloads[13] = surfaces;
	payloads[14] = zeros(128 * 128 * 3);
	payloads[16] = zeros(64);
	return buildBspFile(header, 17, payloads);
}

QString checkBspInspection(const BspInspection& inspection, qsizetype inputSize)
{
	if (!inspection.valid && inspection.error.isEmpty() && inspection.errors.isEmpty()) {
		return QStringLiteral("an invalid BSP reported no error");
	}
	if (inspection.valid && !inspection.errors.isEmpty()) {
		return QStringLiteral("a valid BSP still carried %1 errors").arg(inspection.errors.size());
	}
	if (inspection.fileSizeBytes != inputSize) {
		return QStringLiteral("fileSizeBytes %1 does not match the %2 byte input").arg(inspection.fileSizeBytes).arg(inputSize);
	}
	if (inspection.entityCount != inspection.entities.size()) {
		return QStringLiteral("entityCount %1 disagrees with the %2 parsed entities").arg(inspection.entityCount).arg(inspection.entities.size());
	}
	if (inspection.lumps.size() > 64) {
		return QStringLiteral("reported %1 lumps, more than any supported layout has").arg(inspection.lumps.size());
	}
	for (const BspLumpInfo& lump : inspection.lumps) {
		if (!lump.withinFile) {
			// Out-of-range lumps must be refused, not read.
			if (inspection.errors.isEmpty()) {
				return QStringLiteral("lump %1 is outside the file but no error was raised").arg(lump.index);
			}
			continue;
		}
		if (static_cast<qint64>(lump.offset) + lump.length > inputSize) {
			return QStringLiteral("lump %1 is marked withinFile but spans past the %2 byte input").arg(lump.index).arg(inputSize);
		}
		if (lump.entrySize > 0 && static_cast<qint64>(lump.entryCount) * lump.entrySize > inputSize) {
			return QStringLiteral("lump %1 reports %2 entries, more than the input can hold").arg(lump.index).arg(lump.entryCount);
		}
	}
	const int counts[] = {inspection.modelCount, inspection.faceCount, inspection.vertexCount, inspection.leafCount,
		inspection.nodeCount, inspection.planeCount, inspection.brushCount, inspection.lightmapCount, inspection.entityCount};
	for (const int count : counts) {
		if (count < 0) {
			return QStringLiteral("a reported count is negative");
		}
		if (static_cast<qint64>(count) > inputSize) {
			return QStringLiteral("a reported count of %1 exceeds the %2 byte input").arg(count).arg(inputSize);
		}
	}
	return {};
}

// ---------------------------------------------------------------------------
// Package seeds: PAK, Doom WAD, Quake WAD2 and ZIP.
// ---------------------------------------------------------------------------

struct ArchiveFile {
	QByteArray name;
	QByteArray bytes;
};

// idTech2 PACK: "PACK", int32 directory offset, int32 directory length, then
// 64-byte records of { char name[56]; int32 offset; int32 size; }.
// https://quakewiki.org/wiki/.pak
QByteArray buildPak(const QVector<ArchiveFile>& files)
{
	QByteArray body;
	QVector<quint32> offsets;
	for (const ArchiveFile& file : files) {
		offsets.push_back(static_cast<quint32>(12 + body.size()));
		body.append(file.bytes);
	}
	QByteArray directory;
	for (int index = 0; index < files.size(); ++index) {
		directory.append(fixedName(files.at(index).name, 56));
		appendLe32(&directory, offsets.at(index));
		appendLe32(&directory, static_cast<quint32>(files.at(index).bytes.size()));
	}
	QByteArray bytes("PACK");
	appendLe32(&bytes, static_cast<quint32>(12 + body.size()));
	appendLe32(&bytes, static_cast<quint32>(directory.size()));
	bytes.append(body);
	bytes.append(directory);
	return bytes;
}

// Doom IWAD/PWAD: magic, int32 lump count, int32 directory offset, then
// 16-byte records of { int32 offset; int32 size; char name[8]; }.
// https://doomwiki.org/wiki/WAD
QByteArray buildDoomWad(const QByteArray& magic, const QVector<ArchiveFile>& lumps)
{
	QByteArray body;
	QVector<quint32> offsets;
	for (const ArchiveFile& lump : lumps) {
		offsets.push_back(static_cast<quint32>(12 + body.size()));
		body.append(lump.bytes);
	}
	QByteArray directory;
	for (int index = 0; index < lumps.size(); ++index) {
		appendLe32(&directory, offsets.at(index));
		appendLe32(&directory, static_cast<quint32>(lumps.at(index).bytes.size()));
		directory.append(fixedName(lumps.at(index).name, 8));
	}
	QByteArray bytes = magic;
	appendLe32(&bytes, static_cast<quint32>(lumps.size()));
	appendLe32(&bytes, static_cast<quint32>(12 + body.size()));
	bytes.append(body);
	bytes.append(directory);
	return bytes;
}

// Quake WAD2: same header, 32-byte records of
// { int32 offset; int32 diskSize; int32 size; char type; char compression;
//   int16 pad; char name[16]; }. https://quakewiki.org/wiki/WAD
QByteArray buildQuakeWad2(const QVector<ArchiveFile>& lumps)
{
	QByteArray body;
	QVector<quint32> offsets;
	for (const ArchiveFile& lump : lumps) {
		offsets.push_back(static_cast<quint32>(12 + body.size()));
		body.append(lump.bytes);
	}
	QByteArray directory;
	for (int index = 0; index < lumps.size(); ++index) {
		appendLe32(&directory, offsets.at(index));
		appendLe32(&directory, static_cast<quint32>(lumps.at(index).bytes.size()));
		appendLe32(&directory, static_cast<quint32>(lumps.at(index).bytes.size()));
		directory.append(static_cast<char>('D'));   // type
		directory.append(static_cast<char>(0));     // compression: none
		appendLe16(&directory, 0);                  // pad
		directory.append(fixedName(lumps.at(index).name, 16));
	}
	QByteArray bytes("WAD2");
	appendLe32(&bytes, static_cast<quint32>(lumps.size()));
	appendLe32(&bytes, static_cast<quint32>(12 + body.size()));
	bytes.append(body);
	bytes.append(directory);
	return bytes;
}

// PKWARE .ZIP File Format Specification (APPNOTE.TXT) 4.3.7 local file header,
// 4.3.12 central directory file header, 4.3.16 end of central directory.
QByteArray buildZip(const QVector<ArchiveFile>& files, bool deflated)
{
	QByteArray local;
	QByteArray central;
	for (const ArchiveFile& file : files) {
		const quint32 crc = crc32Bytes(file.bytes);
		const QByteArray payload = deflated ? deflateRaw(file.bytes, DeflateLevel::Default) : file.bytes;
		const auto method = static_cast<quint16>(deflated ? 8 : 0);
		const auto localOffset = static_cast<quint32>(local.size());

		appendLe32(&local, 0x04034b50u);
		appendLe16(&local, 20);
		appendLe16(&local, 0);
		appendLe16(&local, method);
		appendLe16(&local, 0);
		appendLe16(&local, 0x2a21);
		appendLe32(&local, crc);
		appendLe32(&local, static_cast<quint32>(payload.size()));
		appendLe32(&local, static_cast<quint32>(file.bytes.size()));
		appendLe16(&local, static_cast<quint16>(file.name.size()));
		appendLe16(&local, 0);
		local.append(file.name);
		local.append(payload);

		appendLe32(&central, 0x02014b50u);
		appendLe16(&central, 20);
		appendLe16(&central, 20);
		appendLe16(&central, 0);
		appendLe16(&central, method);
		appendLe16(&central, 0);
		appendLe16(&central, 0x2a21);
		appendLe32(&central, crc);
		appendLe32(&central, static_cast<quint32>(payload.size()));
		appendLe32(&central, static_cast<quint32>(file.bytes.size()));
		appendLe16(&central, static_cast<quint16>(file.name.size()));
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe32(&central, 0);
		appendLe32(&central, localOffset);
		central.append(file.name);
	}

	QByteArray bytes = local;
	const auto centralOffset = static_cast<quint32>(bytes.size());
	bytes.append(central);
	appendLe32(&bytes, 0x06054b50u);
	appendLe16(&bytes, 0);
	appendLe16(&bytes, 0);
	appendLe16(&bytes, static_cast<quint16>(files.size()));
	appendLe16(&bytes, static_cast<quint16>(files.size()));
	appendLe32(&bytes, static_cast<quint32>(central.size()));
	appendLe32(&bytes, centralOffset);
	appendLe16(&bytes, 0);
	return bytes;
}

QVector<ArchiveFile> archiveContents()
{
	return {
		{QByteArrayLiteral("readme.txt"), QByteArrayLiteral("VibeStudio fixture archive.\n")},
		{QByteArrayLiteral("maps/base1.bsp"), buildQuakeBsp()},
		{QByteArrayLiteral("textures/e1u1/metal1_1.wal"), buildQuake2Wal(32, 32)},
		{QByteArrayLiteral("sound/ambience/drone.raw"), filler(777, 0x77u)},
	};
}

QVector<QByteArray> packageSeeds()
{
	const QVector<ArchiveFile> contents = archiveContents();
	QVector<QByteArray> seeds;
	seeds.push_back(buildPak(contents));
	seeds.push_back(buildDoomWad(QByteArrayLiteral("PWAD"), {
		{QByteArrayLiteral("MAP01"), QByteArray()},
		{QByteArrayLiteral("THINGS"), filler(100, 3u)},
		{QByteArrayLiteral("LINEDEFS"), filler(140, 4u)},
		{QByteArrayLiteral("SIDEDEFS"), filler(120, 5u)},
		{QByteArrayLiteral("VERTEXES"), filler(64, 6u)},
		{QByteArrayLiteral("SECTORS"), filler(52, 7u)},
	}));
	seeds.push_back(buildQuakeWad2({
		{QByteArrayLiteral("CITY1_1"), buildMipTexture("city1_1", 32, 32, false)},
		{QByteArrayLiteral("PALETTE"), buildPalette768()},
	}));
	seeds.push_back(buildZip(contents, false));
	seeds.push_back(buildZip(contents, true));
	return seeds;
}

QString checkArchive(const QString& path, const QByteArray& bytes)
{
	if (!writeFile(path, bytes)) {
		return QStringLiteral("could not write the fixture to disk");
	}
	PackageArchive archive;
	QString error;
	const bool opened = archive.load(path, &error);
	if (!opened) {
		return error.isEmpty() ? QStringLiteral("a rejected archive reported no error") : QString();
	}
	if (!archive.isOpen()) {
		return QStringLiteral("load() succeeded but the archive is not open");
	}

	const QVector<PackageEntry> entries = archive.entries();
	const auto fileSize = static_cast<qint64>(bytes.size());
	// Only the first entries are read back; that is enough to exercise every
	// storage path without turning the corpus into a benchmark.
	int read = 0;
	// A mutated directory can repeat a record, and a duplicate path makes a
	// path-keyed read ambiguous by design: the reader documents that the first
	// match wins. Only the first occurrence is read back here so the sizes being
	// compared belong to the same entry.
	QSet<QString> readPaths;
	for (const PackageEntry& entry : entries) {
		if (entry.virtualPath.isEmpty()) {
			return QStringLiteral("published an entry with an empty virtual path");
		}
		if (!isSafePackageVirtualPath(entry.virtualPath)) {
			return QStringLiteral("published an unsafe virtual path: %1").arg(entry.virtualPath);
		}
		if (entry.dataOffset >= 0 && entry.dataOffset > fileSize) {
			return QStringLiteral("entry %1 starts past the end of the %2 byte archive").arg(entry.virtualPath).arg(fileSize);
		}
		// A PAK entry is stored verbatim, so its size has to fit in the file. A
		// WAD2/WAD3 lump carries a disk size and a larger logical size, and a
		// ZIP entry's uncompressed size legitimately exceeds the archive, so
		// for those only the on-disk extent can be checked.
		const auto onDiskBytes = static_cast<qint64>(archive.format() == PackageArchiveFormat::Pak
			? entry.sizeBytes
			: entry.compressedSizeBytes);
		if (entry.dataOffset >= 0 && archive.format() != PackageArchiveFormat::Zip
			&& archive.format() != PackageArchiveFormat::Pk3
			&& entry.dataOffset + onDiskBytes > fileSize) {
			return QStringLiteral("stored entry %1 extends past the end of the archive").arg(entry.virtualPath);
		}
		if (entry.kind != PackageEntryKind::File || read >= 24) {
			continue;
		}
		const QString key = entry.virtualPath.toLower();
		if (readPaths.contains(key)) {
			continue;
		}
		readPaths.insert(key);
		++read;
		QByteArray payload;
		QString readError;
		const qint64 cap = 1 << 16;
		if (!archive.readEntryBytes(entry.virtualPath, &payload, &readError, cap)) {
			if (readError.isEmpty()) {
				return QStringLiteral("reading %1 failed without an error message").arg(entry.virtualPath);
			}
			continue;
		}
		if (payload.size() > cap) {
			return QStringLiteral("reading %1 returned %2 bytes despite a %3 byte cap").arg(entry.virtualPath).arg(payload.size()).arg(cap);
		}
		// A WAD2/WAD3 lump reads back its disk size, which can be larger than
		// the logical size the record declares, so both bounds are allowed.
		if (static_cast<quint64>(payload.size()) > qMax(entry.sizeBytes, entry.compressedSizeBytes)) {
			return QStringLiteral("reading %1 returned more bytes than the entry declares").arg(entry.virtualPath);
		}
	}
	return {};
}

// ---------------------------------------------------------------------------
// Level map seeds: a Quake-family .map and a Doom WAD carrying MAP01.
// ---------------------------------------------------------------------------

QByteArray buildQuakeMapText()
{
	return QByteArrayLiteral(
		"// Game: Quake\n"
		"// entity 0\n"
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"\"wad\" \"gfx/base.wad\"\n"
		"// brush 0\n"
		"{\n"
		"( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) METAL1_1 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) METAL1_1 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -63 -64 -16 ) ( -64 -63 -16 ) METAL1_1 0 0 0 1 1\n"
		"( 64 64 16 ) ( 64 65 16 ) ( 65 64 16 ) METAL1_1 0 0 0 1 1\n"
		"( 64 64 16 ) ( 65 64 16 ) ( 64 64 17 ) METAL1_1 0 0 0 1 1\n"
		"( 64 64 16 ) ( 64 64 17 ) ( 64 65 16 ) METAL1_1 0 0 0 1 1\n"
		"}\n"
		"}\n"
		"// entity 1\n"
		"{\n"
		"\"classname\" \"info_player_start\"\n"
		"\"origin\" \"0 0 24\"\n"
		"}\n");
}

// Four vertices, four one-sided linedefs, four sidedefs and one sector: the
// smallest Doom map that passes the required-lump check.
QByteArray buildDoomMapWad()
{
	QByteArray things;
	appendLe16(&things, 0);
	appendLe16(&things, 0);
	appendLe16(&things, 90);
	appendLe16(&things, 1);
	appendLe16(&things, 7);

	QByteArray vertexes;
	const int corners[4][2] = {{0, 0}, {128, 0}, {128, 128}, {0, 128}};
	for (const auto& corner : corners) {
		appendLe16(&vertexes, static_cast<quint16>(corner[0]));
		appendLe16(&vertexes, static_cast<quint16>(corner[1]));
	}

	QByteArray linedefs;
	for (int index = 0; index < 4; ++index) {
		appendLe16(&linedefs, static_cast<quint16>(index));
		appendLe16(&linedefs, static_cast<quint16>((index + 1) % 4));
		appendLe16(&linedefs, 1);                                  // impassable
		appendLe16(&linedefs, 0);                                  // special
		appendLe16(&linedefs, 0);                                  // tag
		appendLe16(&linedefs, static_cast<quint16>(index));        // front sidedef
		appendLe16(&linedefs, 0xffff);                             // no back sidedef
	}

	QByteArray sidedefs;
	for (int index = 0; index < 4; ++index) {
		appendLe16(&sidedefs, 0);
		appendLe16(&sidedefs, 0);
		sidedefs.append(fixedName("-", 8));
		sidedefs.append(fixedName("-", 8));
		sidedefs.append(fixedName("STARTAN3", 8));
		appendLe16(&sidedefs, 0);
	}

	QByteArray sectors;
	appendLe16(&sectors, 0);
	appendLe16(&sectors, 128);
	sectors.append(fixedName("FLOOR0_1", 8));
	sectors.append(fixedName("CEIL1_1", 8));
	appendLe16(&sectors, 160);
	appendLe16(&sectors, 0);
	appendLe16(&sectors, 0);

	return buildDoomWad(QByteArrayLiteral("PWAD"), {
		{QByteArrayLiteral("MAP01"), QByteArray()},
		{QByteArrayLiteral("THINGS"), things},
		{QByteArrayLiteral("LINEDEFS"), linedefs},
		{QByteArrayLiteral("SIDEDEFS"), sidedefs},
		{QByteArrayLiteral("VERTEXES"), vertexes},
		{QByteArrayLiteral("SECTORS"), sectors},
	});
}

QString checkLevelMap(const QString& path, const QByteArray& bytes)
{
	if (!writeFile(path, bytes)) {
		return QStringLiteral("could not write the fixture to disk");
	}
	LevelMapLoadRequest request;
	request.path = path;

	LevelMapDocument document;
	QString error;
	if (!loadLevelMap(request, &document, &error)) {
		return error.isEmpty() ? QStringLiteral("a rejected map reported no error") : QString();
	}
	if (document.format == LevelMapFormat::Unknown) {
		return QStringLiteral("loaded a map but left the format unknown");
	}
	// Every decoded record has to fit in the lump it came from. This is the
	// check that catches an over-read turning into an out-of-bounds parse.
	const struct {
		const char* lump;
		qsizetype stride;
		qsizetype count;
	} records[] = {
		{"VERTEXES", 4, document.doomVertices.size()},
		{"SIDEDEFS", 30, document.doomSidedefs.size()},
		{"SECTORS", 26, document.doomSectors.size()},
	};
	for (const auto& record : records) {
		const qsizetype lumpBytes = document.doomLumps.value(QString::fromLatin1(record.lump)).size();
		if (record.count * record.stride > lumpBytes) {
			return QStringLiteral("%1 produced %2 records, more than its %3 bytes can hold")
				.arg(QString::fromLatin1(record.lump)).arg(record.count).arg(lumpBytes);
		}
	}
	const qsizetype linedefStride = document.doomFormat == LevelMapDoomFormat::Hexen ? 16 : 14;
	const qsizetype linedefBytes = document.doomLumps.value(QStringLiteral("LINEDEFS")).size();
	if (document.doomLinedefs.size() * linedefStride > linedefBytes) {
		return QStringLiteral("LINEDEFS produced %1 records, more than its %2 bytes can hold")
			.arg(document.doomLinedefs.size()).arg(linedefBytes);
	}
	const qsizetype thingStride = document.doomFormat == LevelMapDoomFormat::Hexen ? 20 : 10;
	const qsizetype thingBytes = document.doomLumps.value(QStringLiteral("THINGS")).size();
	if (document.doomThings.size() * thingStride > thingBytes) {
		return QStringLiteral("THINGS produced %1 records, more than its %2 bytes can hold")
			.arg(document.doomThings.size()).arg(thingBytes);
	}

	const LevelMapStatistics stats = levelMapStatistics(document);
	if (stats.doomVertexCount != document.doomVertices.size()
		|| stats.doomLinedefCount != document.doomLinedefs.size()
		|| stats.doomSectorCount != document.doomSectors.size()
		|| stats.brushCount != document.brushes.size()
		|| stats.entityCount != document.entities.size()) {
		return QStringLiteral("the statistics summary disagrees with the parsed document");
	}
	// A `.map` is text: it cannot contain more brushes or entities than it has
	// opening braces.
	if (document.format != LevelMapFormat::DoomWad) {
		const qsizetype braces = bytes.count('{');
		if (document.brushes.size() + document.entities.size() + document.patches.size() > braces) {
			return QStringLiteral("parsed %1 primitives from a text map with only %2 opening braces")
				.arg(document.brushes.size() + document.entities.size() + document.patches.size()).arg(braces);
		}
	}
	return {};
}

// ---------------------------------------------------------------------------
// Model seeds. Layouts as documented at the top of core/model_mesh.h.
// ---------------------------------------------------------------------------

// Quake MDL (IDPO 6), Quake Specifications chapter 5 and `modelgen.h`: an
// 84-byte header, then skins, texture coordinates, triangles and frames.
QByteArray buildQuakeMdl(int vertexCount, int triangleCount, int frameCount, int skinWidth, int skinHeight)
{
	QByteArray bytes("IDPO");
	appendLe32(&bytes, 6);
	for (int axis = 0; axis < 3; ++axis) {
		appendFloat(&bytes, 1.0f);
	}
	for (int axis = 0; axis < 3; ++axis) {
		appendFloat(&bytes, -64.0f);
	}
	appendFloat(&bytes, 48.0f);                 // bounding radius
	for (int axis = 0; axis < 3; ++axis) {
		appendFloat(&bytes, 0.0f);              // eye position
	}
	appendLe32(&bytes, 1);                      // skin count
	appendLe32(&bytes, static_cast<quint32>(skinWidth));
	appendLe32(&bytes, static_cast<quint32>(skinHeight));
	appendLe32(&bytes, static_cast<quint32>(vertexCount));
	appendLe32(&bytes, static_cast<quint32>(triangleCount));
	appendLe32(&bytes, static_cast<quint32>(frameCount));
	appendLe32(&bytes, 0);                      // sync type
	appendLe32(&bytes, 0);                      // flags
	appendFloat(&bytes, 1.0f);                  // average triangle size

	appendLe32(&bytes, 0);                      // single skin
	bytes.append(filler(skinWidth * skinHeight, 0x31u));

	for (int index = 0; index < vertexCount; ++index) {
		appendLe32(&bytes, 0);                                                     // onseam
		appendLe32(&bytes, static_cast<quint32>((index * 3) % skinWidth));         // s
		appendLe32(&bytes, static_cast<quint32>((index * 5) % skinHeight));        // t
	}
	for (int index = 0; index < triangleCount; ++index) {
		appendLe32(&bytes, 1);                                                     // faces front
		appendLe32(&bytes, static_cast<quint32>(index % vertexCount));
		appendLe32(&bytes, static_cast<quint32>((index + 1) % vertexCount));
		appendLe32(&bytes, static_cast<quint32>((index + 2) % vertexCount));
	}
	for (int frame = 0; frame < frameCount; ++frame) {
		appendLe32(&bytes, 0);                  // single frame
		bytes.append(zeros(4));                 // packed mins
		bytes.append(QByteArray(4, '\x7f'));    // packed maxs
		bytes.append(fixedName(QByteArray("stand") + QByteArray::number(frame + 1), 16));
		for (int index = 0; index < vertexCount; ++index) {
			bytes.append(static_cast<char>((index * 7) & 0x7f));
			bytes.append(static_cast<char>((index * 11) & 0x7f));
			bytes.append(static_cast<char>((index * 13) & 0x7f));
			bytes.append(static_cast<char>(index % 162));   // light normal index
		}
	}
	return bytes;
}

// Quake II MD2 (IDP2 8), released Quake II `qfiles.h`: a 68-byte header of 17
// int32 fields, then skins, texture coordinates, triangles, frames and GL
// commands at the offsets the header declares.
QByteArray buildQuake2Md2(int vertexCount, int triangleCount, int frameCount)
{
	const int skinWidth = 64;
	const int skinHeight = 64;
	const int frameSize = 40 + vertexCount * 4;
	const quint32 offsetSkins = 68;
	const quint32 offsetSt = offsetSkins + 64;
	const quint32 offsetTris = offsetSt + static_cast<quint32>(vertexCount * 4);
	const quint32 offsetFrames = offsetTris + static_cast<quint32>(triangleCount * 12);
	const quint32 offsetGlCmds = offsetFrames + static_cast<quint32>(frameCount * frameSize);
	const quint32 offsetEnd = offsetGlCmds + 4;

	QByteArray bytes("IDP2");
	appendLe32(&bytes, 8);
	appendLe32(&bytes, static_cast<quint32>(skinWidth));
	appendLe32(&bytes, static_cast<quint32>(skinHeight));
	appendLe32(&bytes, static_cast<quint32>(frameSize));
	appendLe32(&bytes, 1);                      // skin count
	appendLe32(&bytes, static_cast<quint32>(vertexCount));
	appendLe32(&bytes, static_cast<quint32>(vertexCount));   // texture coordinates
	appendLe32(&bytes, static_cast<quint32>(triangleCount));
	appendLe32(&bytes, 1);                      // GL command words
	appendLe32(&bytes, static_cast<quint32>(frameCount));
	appendLe32(&bytes, offsetSkins);
	appendLe32(&bytes, offsetSt);
	appendLe32(&bytes, offsetTris);
	appendLe32(&bytes, offsetFrames);
	appendLe32(&bytes, offsetGlCmds);
	appendLe32(&bytes, offsetEnd);

	bytes.append(fixedName("models/monsters/soldier/skin.pcx", 64));
	for (int index = 0; index < vertexCount; ++index) {
		appendLe16(&bytes, static_cast<quint16>((index * 3) % skinWidth));
		appendLe16(&bytes, static_cast<quint16>((index * 5) % skinHeight));
	}
	for (int index = 0; index < triangleCount; ++index) {
		for (int corner = 0; corner < 3; ++corner) {
			appendLe16(&bytes, static_cast<quint16>((index + corner) % vertexCount));
		}
		for (int corner = 0; corner < 3; ++corner) {
			appendLe16(&bytes, static_cast<quint16>((index + corner) % vertexCount));
		}
	}
	for (int frame = 0; frame < frameCount; ++frame) {
		for (int axis = 0; axis < 3; ++axis) {
			appendFloat(&bytes, 0.5f);
		}
		for (int axis = 0; axis < 3; ++axis) {
			appendFloat(&bytes, -32.0f);
		}
		bytes.append(fixedName(QByteArray("run") + QByteArray::number(frame + 1), 16));
		for (int index = 0; index < vertexCount; ++index) {
			bytes.append(static_cast<char>((index * 7) & 0x7f));
			bytes.append(static_cast<char>((index * 11) & 0x7f));
			bytes.append(static_cast<char>((index * 13) & 0x7f));
			bytes.append(static_cast<char>(index % 162));
		}
	}
	appendLe32(&bytes, 0);                      // GL command list terminator
	return bytes;
}

// Quake III MD3 (IDP3 15), released Quake III Arena `md3.h`: a 108-byte header,
// then frames, tags and surfaces, each surface carrying its own 108-byte header.
QByteArray buildQuake3Md3(int vertexCount, int triangleCount, int frameCount)
{
	QByteArray frames;
	for (int frame = 0; frame < frameCount; ++frame) {
		for (int axis = 0; axis < 3; ++axis) {
			appendFloat(&frames, -32.0f);
		}
		for (int axis = 0; axis < 3; ++axis) {
			appendFloat(&frames, 32.0f);
		}
		for (int axis = 0; axis < 3; ++axis) {
			appendFloat(&frames, 0.0f);
		}
		appendFloat(&frames, 56.0f);
		frames.append(fixedName(QByteArray("frame") + QByteArray::number(frame), 16));
	}

	QByteArray shaders;
	shaders.append(fixedName("models/weapons2/rocketl/rocketl", 64));
	appendLe32(&shaders, 0);

	QByteArray triangles;
	for (int index = 0; index < triangleCount; ++index) {
		appendLe32(&triangles, static_cast<quint32>(index % vertexCount));
		appendLe32(&triangles, static_cast<quint32>((index + 1) % vertexCount));
		appendLe32(&triangles, static_cast<quint32>((index + 2) % vertexCount));
	}

	QByteArray texCoords;
	for (int index = 0; index < vertexCount; ++index) {
		appendFloat(&texCoords, static_cast<float>(index) / static_cast<float>(vertexCount));
		appendFloat(&texCoords, 0.25f);
	}

	QByteArray positions;
	for (int frame = 0; frame < frameCount; ++frame) {
		for (int index = 0; index < vertexCount; ++index) {
			appendLe16(&positions, static_cast<quint16>(index * 64));
			appendLe16(&positions, static_cast<quint16>(index * 32));
			appendLe16(&positions, static_cast<quint16>(frame * 16));
			appendLe16(&positions, 0x2040);     // packed normal
		}
	}

	const quint32 surfaceHeaderBytes = 108;
	const quint32 offsetTriangles = surfaceHeaderBytes;
	const quint32 offsetShaders = offsetTriangles + static_cast<quint32>(triangles.size());
	const quint32 offsetSt = offsetShaders + static_cast<quint32>(shaders.size());
	const quint32 offsetXyz = offsetSt + static_cast<quint32>(texCoords.size());
	const quint32 surfaceEnd = offsetXyz + static_cast<quint32>(positions.size());

	QByteArray surface("IDP3");
	surface.append(fixedName("hull", 64));
	appendLe32(&surface, 0);                    // flags
	appendLe32(&surface, static_cast<quint32>(frameCount));
	appendLe32(&surface, 1);                    // shader count
	appendLe32(&surface, static_cast<quint32>(vertexCount));
	appendLe32(&surface, static_cast<quint32>(triangleCount));
	appendLe32(&surface, offsetTriangles);
	appendLe32(&surface, offsetShaders);
	appendLe32(&surface, offsetSt);
	appendLe32(&surface, offsetXyz);
	appendLe32(&surface, surfaceEnd);
	surface.append(triangles);
	surface.append(shaders);
	surface.append(texCoords);
	surface.append(positions);

	const quint32 offsetFrames = 108;
	const quint32 offsetTags = offsetFrames + static_cast<quint32>(frames.size());
	const quint32 offsetSurfaces = offsetTags;
	const quint32 offsetEnd = offsetSurfaces + static_cast<quint32>(surface.size());

	QByteArray bytes("IDP3");
	appendLe32(&bytes, 15);
	bytes.append(fixedName("models/weapons2/rocketl/rocketl.md3", 64));
	appendLe32(&bytes, 0);                      // flags
	appendLe32(&bytes, static_cast<quint32>(frameCount));
	appendLe32(&bytes, 0);                      // tag count
	appendLe32(&bytes, 1);                      // surface count
	appendLe32(&bytes, 0);                      // skin count
	appendLe32(&bytes, offsetFrames);
	appendLe32(&bytes, offsetTags);
	appendLe32(&bytes, offsetSurfaces);
	appendLe32(&bytes, offsetEnd);
	bytes.append(frames);
	bytes.append(surface);
	return bytes;
}

QVector<QByteArray> modelSeeds()
{
	QVector<QByteArray> seeds;
	seeds.push_back(buildQuakeMdl(12, 8, 3, 32, 32));
	seeds.push_back(buildQuake2Md2(12, 8, 3));
	seeds.push_back(buildQuake3Md3(12, 8, 3));
	return seeds;
}

QString checkModelMesh(const ModelMesh& mesh, qsizetype inputSize)
{
	if (!mesh.isValid() && mesh.error.isEmpty()) {
		return QStringLiteral("a rejected model reported no error");
	}
	if (mesh.frameCount < 0 || mesh.surfaceCount < 0 || mesh.vertexCount < 0
		|| mesh.triangleCount < 0 || mesh.tagCount < 0 || mesh.skinCount < 0) {
		return QStringLiteral("a reported model count is negative");
	}
	// Nothing in these formats costs less than a byte per record, so no count
	// can exceed the file that declared it.
	const int counts[] = {mesh.frameCount, mesh.surfaceCount, mesh.vertexCount, mesh.triangleCount, mesh.tagCount, mesh.skinCount};
	for (const int count : counts) {
		if (static_cast<qint64>(count) > inputSize) {
			return QStringLiteral("a reported count of %1 exceeds the %2 byte input").arg(count).arg(inputSize);
		}
	}
	// No record in these formats costs less than a byte, so neither list can be
	// longer than the file. The counts are only required to agree with the lists
	// once the mesh is accepted: a rejected model may have collected records
	// before it gave up, and its counts are not published state.
	if (mesh.surfaces.size() > inputSize || mesh.frames.size() > inputSize) {
		return QStringLiteral("produced more surface or frame records than the %1 byte input could hold").arg(inputSize);
	}
	if (mesh.isValid()) {
		if (mesh.surfaces.size() != mesh.surfaceCount) {
			return QStringLiteral("accepted a model with %1 surfaces but a surfaceCount of %2").arg(mesh.surfaces.size()).arg(mesh.surfaceCount);
		}
		if (mesh.frames.size() != mesh.frameCount) {
			return QStringLiteral("accepted a model with %1 frame records but a frameCount of %2").arg(mesh.frames.size()).arg(mesh.frameCount);
		}
	}
	if (!mesh.geometryAvailable) {
		return {};
	}
	for (const ModelSurface& surface : mesh.surfaces) {
		if (surface.vertexCount < 0) {
			return QStringLiteral("surface %1 reports a negative vertex count").arg(surface.index);
		}
		if (surface.texCoords.size() != surface.vertexCount) {
			return QStringLiteral("surface %1 has %2 texture coordinates for %3 vertices")
				.arg(surface.index).arg(surface.texCoords.size()).arg(surface.vertexCount);
		}
		for (const ModelTriangle& triangle : surface.triangles) {
			const int indices[3] = {triangle.a, triangle.b, triangle.c};
			for (const int index : indices) {
				if (index < 0 || index >= surface.vertexCount) {
					return QStringLiteral("surface %1 has a triangle index %2 outside its %3 vertices")
						.arg(surface.index).arg(index).arg(surface.vertexCount);
				}
			}
		}
		for (const ModelFrameGeometry& frame : surface.frames) {
			if (frame.positions.size() != surface.vertexCount) {
				return QStringLiteral("surface %1 has a frame with %2 positions for %3 vertices")
					.arg(surface.index).arg(frame.positions.size()).arg(surface.vertexCount);
			}
			if (!frame.normals.isEmpty() && frame.normals.size() != surface.vertexCount) {
				return QStringLiteral("surface %1 has a frame with %2 normals for %3 vertices")
					.arg(surface.index).arg(frame.normals.size()).arg(surface.vertexCount);
			}
		}
	}
	return {};
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------

bool runDeflateTarget(quint64 seed)
{
	bool ok = true;
	ok &= runTarget("inflateRaw", deflateSeeds(), seed, kDeflateCases, [](const FuzzCase& generated) {
		const QString sized = checkInflate(inflateRaw(generated.data, 1 << 20), generated.data.size(), false);
		if (!sized.isEmpty()) {
			return QStringLiteral("with an expected size: %1").arg(sized);
		}
		return checkInflate(inflateRaw(generated.data), generated.data.size(), false);
	});
	ok &= runTarget("inflateZlib", zlibSeeds(), seed, kDeflateCases, [](const FuzzCase& generated) {
		const QString sized = checkInflate(inflateZlib(generated.data, 1 << 20), generated.data.size(), true);
		if (!sized.isEmpty()) {
			return QStringLiteral("with an expected size: %1").arg(sized);
		}
		return checkInflate(inflateZlib(generated.data), generated.data.size(), true);
	});
	return ok;
}

bool runImageTarget(quint64 seed)
{
	const QVector<ImageSeed> seeds = imageSeeds();
	const IdTechPalette palette = generatedIdTechPalette(QStringLiteral("quake"));

	// Mutating a fixture usually changes which format it is detected as, so the
	// virtual path travels with the seed index rather than with the bytes.
	QVector<QByteArray> raw;
	QVector<QString> paths;
	raw.reserve(seeds.size());
	paths.reserve(seeds.size());
	for (const ImageSeed& seed_ : seeds) {
		raw.push_back(seed_.bytes);
		paths.push_back(seed_.virtualPath);
	}

	bool ok = true;
	ok &= runTarget("decodeIdTechImage", raw, seed, kImageCases, [&paths, &palette](const FuzzCase& generated) {
		const QString path = paths.at(generated.seedIndex);
		const IdTechImageFormat detected = detectIdTechImageFormat(path, generated.data);
		const IdTechImageDecodeResult result = decodeIdTechImage(path, generated.data, palette);
		if (result.format != detected) {
			return QStringLiteral("decode reported format %1 but detection said %2")
				.arg(idTechImageFormatId(result.format), idTechImageFormatId(detected));
		}
		if (detected == IdTechImageFormat::Unknown && result.decoded) {
			return QStringLiteral("decoded a payload whose format was not recognised");
		}
		return checkImageResult(result);
	});

	// Detection on its own must also survive a bare path, which is how Doom WAD
	// lumps arrive.
	ok &= runTarget("detectIdTechImageFormat", raw, seed, kImageCases / 2, [](const FuzzCase& generated) {
		const IdTechImageFormat bare = detectIdTechImageFormat(QString(), generated.data);
		const IdTechImageFormat named = detectIdTechImageFormat(QStringLiteral("a/b/c.dat"), generated.data);
		if (generated.data.isEmpty() && (bare != IdTechImageFormat::Unknown || named != IdTechImageFormat::Unknown)) {
			return QStringLiteral("an empty payload was given a format");
		}
		if (idTechImageFormatId(bare).isEmpty() || idTechImageFormatId(named).isEmpty()) {
			return QStringLiteral("a detected format has no id");
		}
		return QString();
	});
	return ok;
}

bool runBspTarget(quint64 seed)
{
	const QVector<QByteArray> seeds = {buildQuakeBsp(), buildQuake2Bsp(), buildQuake3Bsp()};
	return runTarget("inspectBspBytes", seeds, seed, kBspCases, [](const FuzzCase& generated) {
		const BspInspection inspection = inspectBspBytes(QStringLiteral("fuzz/case.bsp"), generated.data);
		return checkBspInspection(inspection, generated.data.size());
	});
}

bool runPackageTarget(quint64 seed, const QDir& root)
{
	const QString path = root.filePath(QStringLiteral("fuzz-archive.pak"));
	return runTarget("PackageArchive::load", packageSeeds(), seed, kPackageCases, [&path](const FuzzCase& generated) {
		return checkArchive(path, generated.data);
	});
}

bool runLevelMapTarget(quint64 seed, const QDir& root)
{
	bool ok = true;
	const QString mapPath = root.filePath(QStringLiteral("fuzz-map.map"));
	ok &= runTarget("loadLevelMap (.map)", {buildQuakeMapText()}, seed, kLevelMapCases, [&mapPath](const FuzzCase& generated) {
		return checkLevelMap(mapPath, generated.data);
	});

	const QString wadPath = root.filePath(QStringLiteral("fuzz-map.wad"));
	ok &= runTarget("loadLevelMap (Doom WAD)", {buildDoomMapWad()}, seed, kLevelMapCases, [&wadPath](const FuzzCase& generated) {
		return checkLevelMap(wadPath, generated.data);
	});
	return ok;
}

bool runModelTarget(quint64 seed)
{
	return runTarget("decodeModelMesh", modelSeeds(), seed, kModelCases, [](const FuzzCase& generated) {
		const ModelMesh mesh = decodeModelMesh(QStringLiteral("progs/fuzz.mdl"), generated.data, nullptr);
		return checkModelMesh(mesh, generated.data.size());
	});
}

// A guard on the generator itself: the same seed has to produce the same corpus
// every time, and a different seed has to produce a different one. Without this
// a "deterministic" harness could silently stop being deterministic.
bool runGeneratorSmoke()
{
	const QVector<QByteArray> seeds = {QByteArray(64, 'a'), QByteArray::fromHex("4944504f0600000061626364656667")};
	const QVector<FuzzCase> first = buildFuzzCorpus(seeds, kDefaultSeed, 96);
	const QVector<FuzzCase> again = buildFuzzCorpus(seeds, kDefaultSeed, 96);
	if (first.size() != 96 || again.size() != 96) {
		std::cerr << "generator: the corpus size does not match the requested case count\n";
		return false;
	}
	for (int index = 0; index < first.size(); ++index) {
		if (first.at(index).data != again.at(index).data || first.at(index).id != again.at(index).id) {
			std::cerr << "generator: case " << index << " is not reproducible from the same seed\n";
			return false;
		}
	}
	const QVector<FuzzCase> other = buildFuzzCorpus(seeds, kDefaultSeed ^ 0xffull, 96);
	bool anyDifferent = false;
	for (int index = 0; index < first.size(); ++index) {
		anyDifferent = anyDifferent || first.at(index).data != other.at(index).data;
	}
	if (!anyDifferent) {
		std::cerr << "generator: a different seed produced an identical corpus\n";
		return false;
	}
	// Every mutation has to appear, or a target is quietly untested.
	QVector<bool> seen(7, false);
	for (const FuzzCase& generated : first) {
		seen[static_cast<int>(generated.mutation)] = true;
	}
	for (int index = 0; index < seen.size(); ++index) {
		if (!seen.at(index)) {
			std::cerr << "generator: mutation " << index << " never appeared in a 96 case corpus\n";
			return false;
		}
	}
	if (buildFuzzCorpus({}, kDefaultSeed, 16).size() != 0 || buildFuzzCorpus({QByteArray()}, kDefaultSeed, 16).size() != 0) {
		std::cerr << "generator: an empty seed list should produce no cases\n";
		return false;
	}
	if (parserFuzzSeed("VIBESTUDIO_FUZZ_SEED_THAT_IS_NEVER_SET", kDefaultSeed) != kDefaultSeed) {
		std::cerr << "generator: an unset environment variable must fall back to the constant seed\n";
		return false;
	}
	return true;
}

} // namespace

int main()
{
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected temporary directory.");
	}
	const QDir root(tempDir.path());
	qInstallMessageHandler(silenceDecoderWarnings);
	const quint64 seed = parserFuzzSeed(kSeedVariable, kDefaultSeed);
	std::cerr << "parser fuzz seed: 0x" << std::hex << seed << std::dec << "\n";

	bool ok = true;
	ok &= runGeneratorSmoke();
	ok &= runDeflateTarget(seed);
	ok &= runImageTarget(seed);
	ok &= runBspTarget(seed);
	ok &= runPackageTarget(seed, root);
	ok &= runLevelMapTarget(seed, root);
	ok &= runModelTarget(seed);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
