// Named corrupted fixtures, one per known failure mode.
//
// This is the readable counterpart to parser_fuzz_smoke_test.cpp. Where the
// fuzz harness asks "does anything crash", this file asks "does the reader say
// the right thing". Each case is hand-built, carries a comment explaining what
// is wrong with it, and asserts the exact message the parser produces, so the
// messages themselves are pinned: changing one of them has to be a deliberate
// edit to this file too.
//
// Every fixture is assembled here from the published format layouts cited next
// to it; no commercial game data is embedded or read.

#include "core/bsp_inspect.h"
#include "core/deflate.h"
#include "core/idtech_image.h"
#include "core/level_map.h"
#include "core/model_mesh.h"
#include "core/package_archive.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QString>
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

// Message assertions print both sides, because the whole point of this file is
// that the wording is the thing under test.
bool expectMessage(const QString& actual, const QString& expected, const char* label)
{
	if (actual == expected) {
		return true;
	}
	std::cerr << label << "\n"
		<< "  expected: " << qUtf8Printable(expected) << "\n"
		<< "  actual:   " << qUtf8Printable(actual) << "\n";
	return false;
}

// ---------------------------------------------------------------------------
// Byte helpers
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

void patchLe32(QByteArray* data, qsizetype offset, quint32 value)
{
	(*data)[offset] = static_cast<char>(value & 0xff);
	(*data)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
	(*data)[offset + 2] = static_cast<char>((value >> 16) & 0xff);
	(*data)[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

quint32 readLe32(const QByteArray& data, qsizetype offset)
{
	return static_cast<quint32>(static_cast<quint8>(data.at(offset)))
		| (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8)
		| (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16)
		| (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
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

// RFC 1951 3.1.1 bit packing: plain values least significant bit first, Huffman
// codes most significant bit first.
class BitWriter {
public:
	void bits(quint32 value, int count)
	{
		for (int index = 0; index < count; ++index) {
			m_buffer |= ((value >> index) & 1u) << m_count;
			if (++m_count == 8) {
				m_out.append(static_cast<char>(static_cast<quint8>(m_buffer)));
				m_buffer = 0;
				m_count = 0;
			}
		}
	}

	void code(quint32 value, int count)
	{
		for (int index = count - 1; index >= 0; --index) {
			bits((value >> index) & 1u, 1);
		}
	}

	QByteArray finish()
	{
		if (m_count > 0) {
			m_out.append(static_cast<char>(static_cast<quint8>(m_buffer)));
			m_buffer = 0;
			m_count = 0;
		}
		return m_out;
	}

private:
	QByteArray m_out;
	quint32 m_buffer = 0;
	int m_count = 0;
};

// ---------------------------------------------------------------------------
// Archive builders
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
QByteArray buildDoomWad(const QVector<ArchiveFile>& lumps)
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
	QByteArray bytes("PWAD");
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

// The first central directory record starts at the offset the end-of-central
// directory record names; its fields are at the APPNOTE 4.3.12 positions.
qsizetype firstCentralRecordOffset(const QByteArray& zip)
{
	return static_cast<qsizetype>(readLe32(zip, zip.size() - 22 + 16));
}

// ---------------------------------------------------------------------------
// Cases: PAK, WAD and ZIP
// ---------------------------------------------------------------------------

bool runPakDirectoryPastEof(const QDir& root)
{
	bool ok = true;
	// A PAK whose directory offset points past the end of the file. The reader
	// must refuse it up front rather than seeking there and reading whatever it
	// finds: "Invalid PAK directory."
	QByteArray bytes = buildPak({
		{QByteArrayLiteral("readme.txt"), QByteArrayLiteral("fixture\n")},
		{QByteArrayLiteral("maps/base1.bsp"), filler(64, 3u)},
	});
	patchLe32(&bytes, 4, 0x7ffffff0u);

	const QString path = root.filePath(QStringLiteral("dir-past-eof.pak"));
	ok &= expect(writeFile(path, bytes), "PAK past-EOF fixture should be written.");

	PackageArchive archive;
	QString error;
	ok &= expect(!archive.load(path, &error), "A PAK whose directory is past EOF must not load.");
	ok &= expectMessage(error, QStringLiteral("Invalid PAK directory."), "PAK directory past EOF");
	ok &= expect(!archive.isOpen() && archive.entries().isEmpty(), "A refused PAK must leave no entries behind.");
	return ok;
}

bool runWadNegativeLumpCount(const QDir& root)
{
	bool ok = true;
	// A WAD whose lump count is 0xFFFFFFFF. Read as the signed int32 the format
	// specifies it is -1, which a reader that forgot the sign would turn into a
	// four-billion-iteration directory walk: "Invalid WAD directory."
	QByteArray bytes = buildDoomWad({
		{QByteArrayLiteral("MAP01"), QByteArray()},
		{QByteArrayLiteral("THINGS"), filler(40, 9u)},
	});
	patchLe32(&bytes, 4, 0xffffffffu);

	const QString path = root.filePath(QStringLiteral("negative-count.wad"));
	ok &= expect(writeFile(path, bytes), "WAD negative-count fixture should be written.");

	PackageArchive archive;
	QString error;
	ok &= expect(!archive.load(path, &error), "A WAD with a negative lump count must not load.");
	ok &= expectMessage(error, QStringLiteral("Invalid WAD directory."), "WAD negative lump count");
	ok &= expect(archive.entries().isEmpty(), "A refused WAD must leave no entries behind.");
	return ok;
}

bool runZipCentralDirectoryDisagreesWithLocalHeader(const QDir& root)
{
	bool ok = true;
	// A ZIP whose central directory points an entry at a local header that is
	// not there: the offset lands one byte into the real local header, so the
	// "PK\x03\x04" signature does not match. The entry must still be listed -
	// the central directory is the authoritative index - but it must be marked
	// unreadable and say why.
	const QVector<ArchiveFile> files = {
		{QByteArrayLiteral("shaderlist.txt"), QByteArrayLiteral("common\nbase_wall\n")},
	};
	QByteArray bytes = buildZip(files, false);
	patchLe32(&bytes, firstCentralRecordOffset(bytes) + 42, 1u);

	const QString path = root.filePath(QStringLiteral("local-header-mismatch.zip"));
	ok &= expect(writeFile(path, bytes), "ZIP local-header mismatch fixture should be written.");

	PackageArchive archive;
	QString error;
	ok &= expect(archive.load(path, &error), "A ZIP with one unlocatable entry should still open.");
	ok &= expect(archive.entries().size() == 1, "The entry should still be listed from the central directory.");
	if (archive.entries().size() == 1) {
		const PackageEntry entry = archive.entries().first();
		ok &= expect(entry.virtualPath == QStringLiteral("shaderlist.txt"), "The listed path should come from the central directory.");
		ok &= expect(!entry.readable, "An entry whose local header is missing must not be marked readable.");
		ok &= expect(entry.dataOffset < 0, "An entry whose local header is missing must have no data offset.");
		ok &= expectMessage(entry.note, QStringLiteral("ZIP local file header could not be located for this entry."),
			"ZIP entry with a mismatched local header");

		QByteArray payload;
		QString readError;
		ok &= expect(!archive.readEntryBytes(entry.virtualPath, &payload, &readError), "Reading an unlocatable entry must fail.");
		ok &= expectMessage(readError, QStringLiteral("ZIP local file header could not be located for this entry."),
			"reading a ZIP entry with a mismatched local header");
	}
	return ok;
}

bool runZipCrcMismatch(const QDir& root)
{
	bool ok = true;
	// A stored ZIP entry whose payload has been altered after the fact, so the
	// CRC-32 in the central directory no longer describes it. Listing succeeds;
	// reading it must fail rather than hand back silently damaged bytes.
	const QVector<ArchiveFile> files = {
		{QByteArrayLiteral("readme.txt"), QByteArrayLiteral("VibeStudio corrupt fixture.\n")},
	};
	QByteArray stored = buildZip(files, false);
	// Local header is 30 bytes plus the 10-byte name, so the payload starts at
	// byte 40.
	stored[40] = static_cast<char>(static_cast<quint8>(stored.at(40)) ^ 0xffu);

	const QString storedPath = root.filePath(QStringLiteral("crc-stored.zip"));
	ok &= expect(writeFile(storedPath, stored), "ZIP stored CRC fixture should be written.");

	PackageArchive storedArchive;
	QString error;
	ok &= expect(storedArchive.load(storedPath, &error), "A ZIP with a damaged payload should still open.");
	QByteArray payload;
	QString readError;
	ok &= expect(!storedArchive.readEntryBytes(QStringLiteral("readme.txt"), &payload, &readError),
		"A stored entry that fails its CRC must not be returned.");
	ok &= expectMessage(readError, QStringLiteral("Package entry failed its CRC check; the archive is damaged."),
		"stored ZIP entry with a bad CRC");
	// A failed read hands back nothing. The stored path reads into the caller's
	// buffer before it can verify the CRC, so it clears the buffer on failure:
	// a caller that checks only for non-empty output must not end up with the
	// damaged bytes.
	ok &= expect(payload.isEmpty(), "A stored entry that fails its CRC must leave the output buffer empty.");

	// The deflated path reports the same problem differently: there the stream
	// inflates cleanly and only the checksum disagrees, so the message names
	// both values. Keep local and central CRC fields consistent so this fixture
	// reaches payload verification instead of the header-consistency check.
	QByteArray deflated = buildZip(files, true);
	patchLe32(&deflated, 14, 0xdeadbeefu);
	patchLe32(&deflated, firstCentralRecordOffset(deflated) + 16, 0xdeadbeefu);

	const QString deflatedPath = root.filePath(QStringLiteral("crc-deflated.zip"));
	ok &= expect(writeFile(deflatedPath, deflated), "ZIP deflated CRC fixture should be written.");

	PackageArchive deflatedArchive;
	ok &= expect(deflatedArchive.load(deflatedPath, &error), "A ZIP with a wrong stored CRC should still open.");
	readError.clear();
	payload.clear();
	ok &= expect(!deflatedArchive.readEntryBytes(QStringLiteral("readme.txt"), &payload, &readError),
		"A deflated entry that fails its CRC must not be returned.");
	ok &= expectMessage(readError,
		QStringLiteral("CRC-32 mismatch for package entry (expected deadbeef, got %1); the archive is corrupt.")
			.arg(QString::number(crc32Bytes(files.first().bytes), 16)),
		"deflated ZIP entry with a bad CRC");
	return ok;
}

// ---------------------------------------------------------------------------
// Case: DEFLATE
// ---------------------------------------------------------------------------

bool runDeflateIncompleteHuffmanTable()
{
	bool ok = true;
	// A dynamic Huffman block (RFC 1951 3.2.7) whose literal/length alphabet
	// declares exactly one code of length 1. That leaves half the code space
	// unassigned, so the table is incomplete: a decoder that builds it anyway
	// will happily decode a bit pattern that the encoder never emitted.
	//
	// Code length alphabet: symbols 1 and 18 get length 1, which is a complete
	// two-code set; canonically symbol 1 is "0" and symbol 18 is "1". The code
	// length list then reads: one length-1 literal, then 138 + 119 zeros, which
	// fills the 257 literal lengths and the single distance length.
	BitWriter writer;
	writer.bits(1, 1);          // BFINAL
	writer.bits(2, 2);          // BTYPE = 10, dynamic Huffman
	writer.bits(257 - 257, 5);  // HLIT
	writer.bits(1 - 1, 5);      // HDIST
	writer.bits(18 - 4, 4);     // HCLEN
	{
		// RFC 1951 3.2.7 orders the code length alphabet like this.
		const int order[18] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1};
		for (const int symbol : order) {
			writer.bits(symbol == 1 || symbol == 18 ? 1u : 0u, 3);
		}
	}
	writer.code(0x0, 1);                            // symbol 1: literal 0 has length 1
	writer.code(0x1, 1); writer.bits(138 - 11, 7);  // symbol 18: 138 zero lengths
	writer.code(0x1, 1); writer.bits(119 - 11, 7);  // symbol 18: 119 more zero lengths

	const QByteArray stream = writer.finish();
	const InflateResult result = inflateRaw(stream, 64);
	ok &= expect(!result.ok, "An incomplete literal/length table must be rejected.");
	ok &= expect(result.data.isEmpty(), "A rejected deflate stream must return no data.");
	ok &= expectMessage(result.error, QStringLiteral("Deflate literal/length code table is malformed."),
		"deflate stream with an incomplete literal/length table");

	// The same stream behind a zlib wrapper must fail with the same reason.
	QByteArray wrapped;
	wrapped.append(static_cast<char>(0x78));
	wrapped.append(static_cast<char>(0x9c));
	wrapped.append(stream);
	appendLe32(&wrapped, 0);
	const InflateResult zlibResult = inflateZlib(wrapped, 64);
	ok &= expect(!zlibResult.ok, "An incomplete table must be rejected through the zlib wrapper too.");
	ok &= expectMessage(zlibResult.error, QStringLiteral("Deflate literal/length code table is malformed."),
		"zlib stream with an incomplete literal/length table");
	return ok;
}

// ---------------------------------------------------------------------------
// Case: BSP
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

// Quake BSP29: int32 version, then 15 { int32 offset; int32 length; } pairs.
// Quake Specifications, https://www.gamers.org/dEngine/quake/spec/quake-spec34/
QByteArray buildQuakeBsp()
{
	QByteArray header;
	appendLe32(&header, 29);
	QVector<QByteArray> payloads(15);
	payloads[0] = QByteArray("{\n\"classname\" \"worldspawn\"\n}\n");
	payloads[1] = zeros(20 * 2);
	payloads[3] = zeros(12 * 4);
	payloads[5] = zeros(24);
	payloads[7] = zeros(20 * 2);
	payloads[10] = zeros(28 * 2);
	payloads[11] = zeros(2 * 4);    // marksurfaces: 4 entries of 2 bytes
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

bool runBspLumpPastEof()
{
	bool ok = true;
	// A BSP whose marksurfaces lump starts a gigabyte into a file that is only
	// a few hundred bytes long. The inspector must flag that one lump, refuse
	// to read it, and keep inspecting the rest of the file.
	QByteArray bytes = buildQuakeBsp();
	const quint32 badOffset = 0x40000000u;
	patchLe32(&bytes, 4 + 11 * 8, badOffset);
	const quint32 lumpLength = 2 * 4;

	const BspInspection inspection = inspectBspBytes(QStringLiteral("fixtures/past-eof.bsp"), bytes);
	ok &= expect(!inspection.valid, "A BSP with an out-of-range lump must not be valid.");
	ok &= expect(inspection.lumps.size() == 15, "The Quake layout should still expose 15 lumps.");
	if (inspection.lumps.size() == 15) {
		ok &= expect(!inspection.lumps.at(11).withinFile, "The out-of-range lump must be flagged.");
		ok &= expect(inspection.lumps.at(0).withinFile, "Other lumps must stay usable.");
	}
	ok &= expect(inspection.errors.size() == 1, "Exactly one lump is out of range, so exactly one error is expected.");
	if (!inspection.errors.isEmpty()) {
		ok &= expectMessage(inspection.errors.first(),
			QStringLiteral("Lump 11 (marksurfaces) spans bytes %1..%2, which is outside the %3 byte file; it was not read.")
				.arg(badOffset).arg(badOffset + lumpLength).arg(bytes.size()),
			"BSP lump that extends past EOF");
		ok &= expectMessage(inspection.error, inspection.errors.first(), "BSP summary error");
	}
	ok &= expect(inspection.entityCount == 1, "The entity lump must still be parsed.");
	ok &= expect(inspection.state() == OperationState::Failed, "An out-of-range lump is a failure, not a warning.");
	return ok;
}

// ---------------------------------------------------------------------------
// Cases: Doom WAD map and Quake-family .map
// ---------------------------------------------------------------------------

QByteArray doomThingsLump()
{
	QByteArray bytes;
	appendLe16(&bytes, 64);
	appendLe16(&bytes, 64);
	appendLe16(&bytes, 90);
	appendLe16(&bytes, 1);      // player 1 start
	appendLe16(&bytes, 7);
	return bytes;
}

QByteArray doomVertexesLump()
{
	QByteArray bytes;
	const int corners[4][2] = {{0, 0}, {128, 0}, {128, 128}, {0, 128}};
	for (const auto& corner : corners) {
		appendLe16(&bytes, static_cast<quint16>(corner[0]));
		appendLe16(&bytes, static_cast<quint16>(corner[1]));
	}
	return bytes;
}

QByteArray doomLinedefsLump()
{
	QByteArray bytes;
	for (int index = 0; index < 4; ++index) {
		appendLe16(&bytes, static_cast<quint16>(index));
		appendLe16(&bytes, static_cast<quint16>((index + 1) % 4));
		appendLe16(&bytes, 1);                              // impassable
		appendLe16(&bytes, 0);                              // special
		appendLe16(&bytes, 0);                              // tag
		appendLe16(&bytes, static_cast<quint16>(index));    // front sidedef
		appendLe16(&bytes, 0xffff);                         // no back sidedef
	}
	return bytes;
}

QByteArray doomSidedefsLump()
{
	QByteArray bytes;
	for (int index = 0; index < 4; ++index) {
		appendLe16(&bytes, 0);
		appendLe16(&bytes, 0);
		bytes.append(fixedName("-", 8));
		bytes.append(fixedName("-", 8));
		bytes.append(fixedName("STARTAN3", 8));
		appendLe16(&bytes, 0);
	}
	return bytes;
}

bool runDoomWadMissingSectors(const QDir& root)
{
	bool ok = true;
	// A Doom map that has every lump except SECTORS. The marker and the other
	// lumps are valid, so the map loads; the missing lump has to be reported as
	// an error against the map rather than silently producing a sectorless map.
	// Required lumps: https://doomwiki.org/wiki/WAD
	const QByteArray bytes = buildDoomWad({
		{QByteArrayLiteral("MAP01"), QByteArray()},
		{QByteArrayLiteral("THINGS"), doomThingsLump()},
		{QByteArrayLiteral("LINEDEFS"), doomLinedefsLump()},
		{QByteArrayLiteral("SIDEDEFS"), doomSidedefsLump()},
		{QByteArrayLiteral("VERTEXES"), doomVertexesLump()},
	});

	const QString path = root.filePath(QStringLiteral("no-sectors.wad"));
	ok &= expect(writeFile(path, bytes), "Doom map fixture without SECTORS should be written.");

	LevelMapLoadRequest request;
	request.path = path;
	LevelMapDocument document;
	QString error;
	ok &= expect(loadLevelMap(request, &document, &error), "A Doom map missing SECTORS should still load for inspection.");
	ok &= expect(document.format == LevelMapFormat::DoomWad, "The map should be recognised as a Doom WAD map.");
	ok &= expect(document.doomSectors.isEmpty(), "There are no sectors to parse.");

	bool found = false;
	for (const LevelMapIssue& issue : document.issues) {
		if (issue.code != QStringLiteral("missing-doom-lump") || issue.objectId != QStringLiteral("SECTORS")) {
			continue;
		}
		found = true;
		ok &= expect(issue.severity == LevelMapIssueSeverity::Error, "A missing required lump is an error.");
		ok &= expectMessage(issue.message, QStringLiteral("Required Doom map lump is missing: SECTORS"),
			"Doom map missing the SECTORS lump");
	}
	ok &= expect(found, "A Doom map without SECTORS must raise a missing-doom-lump issue for SECTORS.");
	return ok;
}

bool runMapUnterminatedBrush(const QDir& root)
{
	bool ok = true;
	// A `.map` that stops in the middle of a brush. Because the brush sits
	// inside the worldspawn entity, the unmatched brace the parser actually
	// notices is the entity's: it reports that the map ended before an entity
	// closed, and still keeps the faces it managed to read.
	const QByteArray text = QByteArrayLiteral(
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"{\n"
		"( -64 -64 -16 ) ( -64 -63 -16 ) ( -64 -64 -15 ) METAL1_1 0 0 0 1 1\n"
		"( -64 -64 -16 ) ( -64 -64 -15 ) ( -63 -64 -16 ) METAL1_1 0 0 0 1 1\n");

	const QString path = root.filePath(QStringLiteral("unterminated-brush.map"));
	ok &= expect(writeFile(path, text), "Unterminated brush fixture should be written.");

	LevelMapLoadRequest request;
	request.path = path;
	LevelMapDocument document;
	QString error;
	ok &= expect(loadLevelMap(request, &document, &error), "An unterminated .map should still load for inspection.");
	ok &= expect(document.entities.size() == 1, "The single opened entity should still be reported.");
	ok &= expect(document.brushes.size() == 1, "The partially read brush should still be reported.");
	if (document.brushes.size() == 1) {
		ok &= expect(document.brushes.first().faces.size() == 2, "Both readable faces should be kept.");
		ok &= expect(!document.brushes.first().boundsSolved, "Two faces cannot enclose a volume.");
	}

	bool found = false;
	for (const LevelMapIssue& issue : document.issues) {
		if (issue.code != QStringLiteral("unterminated-entity")) {
			continue;
		}
		found = true;
		ok &= expect(issue.severity == LevelMapIssueSeverity::Error, "An unterminated entity is an error.");
		ok &= expectMessage(issue.message, QStringLiteral("Map ended before an entity closed."),
			".map that ends inside a brush");
	}
	ok &= expect(found, "An unterminated brush must raise an unterminated-entity issue.");
	return ok;
}

bool runMapUnbalancedBraces(const QDir& root)
{
	bool ok = true;
	// A `.map` with one closing brace too many. The stray brace is outside any
	// entity, so it is reported as an unexpected token at its own line and the
	// rest of the file still parses.
	const QByteArray text = QByteArrayLiteral(
		"{\n"
		"\"classname\" \"worldspawn\"\n"
		"}\n"
		"}\n"
		"{\n"
		"\"classname\" \"info_player_start\"\n"
		"\"origin\" \"0 0 24\"\n"
		"}\n");

	const QString path = root.filePath(QStringLiteral("unbalanced-braces.map"));
	ok &= expect(writeFile(path, text), "Unbalanced brace fixture should be written.");

	LevelMapLoadRequest request;
	request.path = path;
	LevelMapDocument document;
	QString error;
	ok &= expect(loadLevelMap(request, &document, &error), "A .map with a stray brace should still load.");
	ok &= expect(document.entities.size() == 2, "Both well-formed entities should survive the stray brace.");

	bool found = false;
	for (const LevelMapIssue& issue : document.issues) {
		if (issue.code != QStringLiteral("unexpected-token")) {
			continue;
		}
		found = true;
		ok &= expect(issue.severity == LevelMapIssueSeverity::Warning, "A stray brace is a warning, not a hard failure.");
		ok &= expectMessage(issue.message, QStringLiteral("Unexpected token outside an entity: }"),
			".map with an unbalanced closing brace");
		ok &= expect(issue.line == 4, "The stray brace is on line 4.");
	}
	ok &= expect(found, "A stray closing brace must raise an unexpected-token issue.");
	return ok;
}

// ---------------------------------------------------------------------------
// Cases: PCX and Targa
// ---------------------------------------------------------------------------

bool runPcxBadRleRun()
{
	bool ok = true;
	// A PCX whose last RLE packet is a run control byte with no value byte
	// after it: the run is announced and then the scanline data ends. The
	// header is valid, so the file is recognised as a PCX and only the pixel
	// data is rejected.
	// ZSoft PCX File Format Technical Reference Manual: a control byte with its
	// top two bits set carries a run length in the low six bits, followed by
	// the repeated byte.
	const int width = 8;
	const int height = 4;
	QByteArray header(128, '\0');
	header[0] = static_cast<char>(0x0a);   // ZSoft manufacturer byte
	header[1] = static_cast<char>(5);      // version
	header[2] = static_cast<char>(1);      // RLE encoding
	header[3] = static_cast<char>(8);      // bits per pixel per plane
	QByteArray fields;
	appendLe16(&fields, 0);                             // x min
	appendLe16(&fields, 0);                             // y min
	appendLe16(&fields, static_cast<quint16>(width - 1));
	appendLe16(&fields, static_cast<quint16>(height - 1));
	appendLe16(&fields, 72);
	appendLe16(&fields, 72);
	for (int index = 0; index < fields.size(); ++index) {
		header[4 + index] = fields.at(index);
	}
	header[65] = static_cast<char>(1);                  // planes
	header[66] = static_cast<char>(width);              // bytes per line
	header[67] = static_cast<char>(0);
	header[68] = static_cast<char>(1);                  // palette info

	QByteArray bytes = header;
	bytes.append(static_cast<char>(0xc4));  // run of four
	bytes.append(static_cast<char>(0x11));  // ... of this value
	bytes.append(static_cast<char>(0xc4));  // run of four, and then nothing
	bytes.append(static_cast<char>(0x0c));  // 256-colour tail palette marker
	for (int index = 0; index < 256; ++index) {
		bytes.append(static_cast<char>(index));
		bytes.append(static_cast<char>(index));
		bytes.append(static_cast<char>(index));
	}

	const QString path = QStringLiteral("pics/truncated.pcx");
	ok &= expect(detectIdTechImageFormat(path, bytes) == IdTechImageFormat::Pcx, "The fixture should still be recognised as a PCX.");
	const IdTechImageDecodeResult result = decodeIdTechImage(path, bytes, generatedIdTechPalette(QStringLiteral("quake2")));
	ok &= expect(!result.decoded, "A PCX with a dangling RLE run must not decode.");
	ok &= expect(result.image.isNull(), "A rejected PCX must produce no image.");
	ok &= expectMessage(result.error, QStringLiteral("PCX pixel data is truncated or malformed."),
		"PCX with a bad RLE run");
	return ok;
}

bool runTargaBadRleRun()
{
	bool ok = true;
	// A run-length encoded Targa whose first run packet asks for a three-byte
	// pixel but only two bytes follow. Truevision TGA File Format
	// Specification 2.0: a packet byte with bit 7 set is a run packet whose low
	// seven bits are the repeat count minus one, followed by one pixel value.
	QByteArray bytes;
	bytes.append(static_cast<char>(0));    // id length
	bytes.append(static_cast<char>(0));    // colour map type
	bytes.append(static_cast<char>(10));   // run-length encoded true colour
	appendLe16(&bytes, 0);                 // colour map origin
	appendLe16(&bytes, 0);                 // colour map length
	bytes.append(static_cast<char>(0));    // colour map entry size
	appendLe16(&bytes, 0);                 // x origin
	appendLe16(&bytes, 0);                 // y origin
	appendLe16(&bytes, 4);                 // width
	appendLe16(&bytes, 2);                 // height
	bytes.append(static_cast<char>(24));   // pixel depth
	bytes.append(static_cast<char>(0x20)); // top-left origin
	bytes.append(static_cast<char>(0x81)); // run of two pixels
	bytes.append(static_cast<char>(0x0a)); // blue
	bytes.append(static_cast<char>(0x0b)); // green, and then the file ends

	const QString path = QStringLiteral("textures/broken.tga");
	ok &= expect(detectIdTechImageFormat(path, bytes) == IdTechImageFormat::Targa, "The fixture should still be recognised as a Targa.");
	const IdTechImageDecodeResult result = decodeIdTechImage(path, bytes, generatedIdTechPalette(QStringLiteral("quake2")));
	ok &= expect(!result.decoded, "A Targa with a truncated RLE run must not decode.");
	ok &= expect(result.image.isNull(), "A rejected Targa must produce no image.");
	ok &= expectMessage(result.error, QStringLiteral("Targa pixel data is truncated or malformed."),
		"Targa with a bad RLE run");
	return ok;
}

// ---------------------------------------------------------------------------
// Case: MDL
// ---------------------------------------------------------------------------

// Quake MDL (IDPO version 6), Quake Specifications chapter 5 and the released
// Quake `modelgen.h`: an 84-byte header, then one skin, `numverts` texture
// coordinates, `numtris` triangles and `numframes` frames.
QByteArray buildQuakeMdl(int vertexCount, int triangleCount, int frameCount, int skinWidth, int skinHeight)
{
	QByteArray bytes("IDPO");
	appendLe32(&bytes, 6);
	for (int axis = 0; axis < 3; ++axis) {
		appendFloat(&bytes, 1.0f);              // scale
	}
	for (int axis = 0; axis < 3; ++axis) {
		appendFloat(&bytes, -64.0f);            // scale origin
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

	appendLe32(&bytes, 0);                      // single (ungrouped) skin
	bytes.append(filler(skinWidth * skinHeight, 0x31u));

	for (int index = 0; index < vertexCount; ++index) {
		appendLe32(&bytes, 0);                                                // onseam
		appendLe32(&bytes, static_cast<quint32>((index * 3) % skinWidth));    // s
		appendLe32(&bytes, static_cast<quint32>((index * 5) % skinHeight));   // t
	}
	for (int index = 0; index < triangleCount; ++index) {
		appendLe32(&bytes, 1);                  // faces front
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

bool runMdlFrameCountOverflow()
{
	bool ok = true;
	// An MDL header that claims 0x7fffffff frames. Each frame is
	// 24 + numverts * 4 bytes, so `numframes * frameSize` overflows a signed
	// 32-bit multiply and a reader that trusts it either allocates absurdly or
	// wraps to a small, wrong number and walks off the end of the file.
	QByteArray bytes = buildQuakeMdl(12, 8, 3, 32, 32);
	patchLe32(&bytes, 68, 0x7fffffffu);   // numframes

	const ModelMesh mesh = decodeModelMesh(QStringLiteral("progs/overflow.mdl"), bytes, nullptr);
	ok &= expect(!mesh.isValid(), "An MDL whose frame count overflows must not be accepted.");
	// The wording of this one is not pinned: src/core/model_mesh.cpp lands in
	// the same round as this test, so only the contract is asserted here. If
	// the message settles, pin it with expectMessage like the cases above.
	ok &= expect(!mesh.error.isEmpty(), "A rejected MDL must explain itself.");
	ok &= expect(!mesh.geometryAvailable, "A rejected MDL must not claim usable geometry.");
	ok &= expect(static_cast<qint64>(mesh.frames.size()) * 4 <= bytes.size(),
		"A rejected MDL must not materialise more frames than the file could hold.");
	ok &= expect(mesh.surfaces.isEmpty(), "A rejected MDL must not publish surfaces.");

	// The same file with an honest frame count is the control: it has to decode,
	// so the case above is testing the overflow and not the fixture.
	const ModelMesh good = decodeModelMesh(QStringLiteral("progs/ok.mdl"), buildQuakeMdl(12, 8, 3, 32, 32), nullptr);
	ok &= expect(good.isValid() && good.error.isEmpty(), "The unmodified MDL fixture must decode.");
	ok &= expect(good.frameCount == 3, "The unmodified MDL fixture has three frames.");
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
	ok &= runPakDirectoryPastEof(root);
	ok &= runWadNegativeLumpCount(root);
	ok &= runZipCentralDirectoryDisagreesWithLocalHeader(root);
	ok &= runZipCrcMismatch(root);
	ok &= runDeflateIncompleteHuffmanTable();
	ok &= runBspLumpPastEof();
	ok &= runDoomWadMissingSectors(root);
	ok &= runMapUnterminatedBrush(root);
	ok &= runMapUnbalancedBraces(root);
	ok &= runPcxBadRleRun();
	ok &= runTargaBadRleRun();
	ok &= runMdlFrameCountOverflow();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
