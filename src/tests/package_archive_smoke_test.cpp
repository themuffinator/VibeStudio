#include "core/deflate.h"
#include "core/package_archive.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPair>
#include <QProcess>
#include <QTemporaryDir>
#include <QVector>

#include <iostream>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

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

bool hasFormat(PackageArchiveFormat format)
{
	for (const PackageArchiveFormatDescriptor& descriptor : packageArchiveFormatDescriptors()) {
		if (descriptor.format == format) {
			return true;
		}
	}
	return false;
}

PackageArchiveFormatDescriptor descriptorFor(PackageArchiveFormat format)
{
	for (const PackageArchiveFormatDescriptor& descriptor : packageArchiveFormatDescriptors()) {
		if (descriptor.format == format) {
			return descriptor;
		}
	}
	return {};
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
	appendLe32(data, static_cast<quint32>(value & 0xffffffffu));
	appendLe32(data, static_cast<quint32>((value >> 32) & 0xffffffffu));
}

bool writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(data) == data.size();
}

bool readBackFile(const QString& path, QByteArray* out)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	*out = file.readAll();
	return true;
}

// Creates a directory symlink (POSIX) or an NTFS junction (Windows). Returns
// false when the platform or the current privileges do not allow it, so the
// traversal test can skip instead of failing.
bool makeDirectoryLink(const QString& linkPath, const QString& targetPath)
{
#ifdef Q_OS_WIN
	QProcess process;
	process.start(QStringLiteral("cmd.exe"),
		{QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
			QDir::toNativeSeparators(linkPath), QDir::toNativeSeparators(targetPath)});
	if (!process.waitForFinished(20000)) {
		process.kill();
		return false;
	}
	return process.exitCode() == 0 && QFileInfo::exists(linkPath);
#elif defined(Q_OS_UNIX)
	return ::symlink(targetPath.toLocal8Bit().constData(), linkPath.toLocal8Bit().constData()) == 0;
#else
	Q_UNUSED(linkPath);
	Q_UNUSED(targetPath);
	return false;
#endif
}

QByteArray pakFixture()
{
	const QByteArray first = "echo hello\n";
	const QByteArray second = "duplicate\n";
	const quint32 firstOffset = 12;
	const quint32 secondOffset = firstOffset + static_cast<quint32>(first.size());
	const quint32 directoryOffset = secondOffset + static_cast<quint32>(second.size());

	QByteArray data;
	data.append("PACK");
	appendLe32(&data, directoryOffset);
	appendLe32(&data, 64 * 3);
	data.append(first);
	data.append(second);

	auto appendRecord = [&data](const QByteArray& name, quint32 offset, quint32 size) {
		const qsizetype before = data.size();
		data.append(name.left(56));
		while (data.size() - before < 56) {
			data.append('\0');
		}
		appendLe32(&data, offset);
		appendLe32(&data, size);
	};
	appendRecord("scripts/autoexec.cfg", firstOffset, static_cast<quint32>(first.size()));
	appendRecord("scripts/autoexec.cfg", secondOffset, static_cast<quint32>(second.size()));
	appendRecord("../escape.cfg", firstOffset, static_cast<quint32>(first.size()));
	return data;
}

QByteArray wadFixture()
{
	const QByteArray first = "MAPDATA";
	const QByteArray second = "PLAYPAL";
	const quint32 firstOffset = 12;
	const quint32 secondOffset = firstOffset + static_cast<quint32>(first.size());
	const quint32 directoryOffset = secondOffset + static_cast<quint32>(second.size());

	QByteArray data;
	data.append("PWAD");
	appendLe32(&data, 2);
	appendLe32(&data, directoryOffset);
	data.append(first);
	data.append(second);

	auto appendRecord = [&data](const QByteArray& name, quint32 offset, quint32 size) {
		appendLe32(&data, offset);
		appendLe32(&data, size);
		const qsizetype before = data.size();
		data.append(name.left(8));
		while (data.size() - before < 8) {
			data.append('\0');
		}
	};
	appendRecord("MAP01", firstOffset, static_cast<quint32>(first.size()));
	appendRecord("PLAYPAL", secondOffset, static_cast<quint32>(second.size()));
	return data;
}

// A PWAD holding several maps, each with its own full set of map lumps, so the
// directory contains many duplicate lump names. Doom stores a map as the run of
// lumps following its marker (https://doomwiki.org/wiki/WAD), so THINGS,
// LINEDEFS, ... legitimately repeat once per map. The fixture is deliberately
// large enough that an unstable sort of the entry list actually reorders the
// duplicates instead of falling into a stable insertion-sort cutoff.
QByteArray multiMapWadFixture(int mapCount, QByteArray* firstThingsBytes)
{
	const QVector<QByteArray> mapLumps = {
		"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
		"SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"
	};

	struct Lump {
		QByteArray name;
		QByteArray payload;
	};
	QVector<Lump> lumps;
	for (int map = 1; map <= mapCount; ++map) {
		const QByteArray marker = QByteArrayLiteral("MAP") + QByteArray::number(map).rightJustified(2, '0');
		lumps.push_back({marker, marker + "-marker"});
		for (const QByteArray& name : mapLumps) {
			lumps.push_back({name, name + "-" + marker});
		}
	}
	if (firstThingsBytes) {
		*firstThingsBytes = QByteArrayLiteral("THINGS-MAP01");
	}

	QByteArray payload;
	QVector<QPair<quint32, quint32>> placement;
	for (const Lump& lump : lumps) {
		placement.push_back({static_cast<quint32>(12 + payload.size()), static_cast<quint32>(lump.payload.size())});
		payload.append(lump.payload);
	}

	QByteArray data;
	data.append("PWAD");
	appendLe32(&data, static_cast<quint32>(lumps.size()));
	appendLe32(&data, static_cast<quint32>(12 + payload.size()));
	data.append(payload);
	for (int index = 0; index < lumps.size(); ++index) {
		appendLe32(&data, placement[index].first);
		appendLe32(&data, placement[index].second);
		const qsizetype before = data.size();
		data.append(lumps[index].name.left(8));
		while (data.size() - before < 8) {
			data.append('\0');
		}
	}
	return data;
}

// Minimal ZIP writer used by the fixtures below. Field layout follows the
// PKWARE .ZIP File Format Specification (APPNOTE.TXT) sections 4.3.7, 4.3.12
// and 4.3.16.
struct ZipInput {
	QByteArray name;
	QByteArray payload;
	bool deflate = false;
	quint32 crcOverride = 0;
	bool useCrcOverride = false;
};

struct ZipPlaced {
	QByteArray name;
	QByteArray stored;
	quint32 crc = 0;
	quint32 uncompressedSize = 0;
	quint16 method = 0;
	quint64 localOffset = 0;
};

void appendLocalHeader(QByteArray* data, const ZipPlaced& entry)
{
	appendLe32(data, 0x04034b50);
	appendLe16(data, 20);
	appendLe16(data, 0);
	appendLe16(data, entry.method);
	appendLe16(data, 0);
	appendLe16(data, 0);
	appendLe32(data, entry.crc);
	appendLe32(data, static_cast<quint32>(entry.stored.size()));
	appendLe32(data, entry.uncompressedSize);
	appendLe16(data, static_cast<quint16>(entry.name.size()));
	appendLe16(data, 0);
	data->append(entry.name);
	data->append(entry.stored);
}

void appendCentralRecord(QByteArray* data, const ZipPlaced& entry, const QByteArray& extra,
	quint32 compressedField, quint32 uncompressedField, quint32 offsetField)
{
	appendLe32(data, 0x02014b50);
	appendLe16(data, 20);
	appendLe16(data, 20);
	appendLe16(data, 0);
	appendLe16(data, entry.method);
	appendLe16(data, 0);
	appendLe16(data, 0);
	appendLe32(data, entry.crc);
	appendLe32(data, compressedField);
	appendLe32(data, uncompressedField);
	appendLe16(data, static_cast<quint16>(entry.name.size()));
	appendLe16(data, static_cast<quint16>(extra.size()));
	appendLe16(data, 0);
	appendLe16(data, 0);
	appendLe16(data, 0);
	appendLe32(data, 0);
	appendLe32(data, offsetField);
	data->append(entry.name);
	data->append(extra);
}

void appendEndOfCentralDirectory(QByteArray* data, quint16 entryCountField, quint32 sizeField, quint32 offsetField)
{
	appendLe32(data, 0x06054b50);
	appendLe16(data, 0);
	appendLe16(data, 0);
	appendLe16(data, entryCountField);
	appendLe16(data, entryCountField);
	appendLe32(data, sizeField);
	appendLe32(data, offsetField);
	appendLe16(data, 0);
}

void appendZip64EndOfCentralDirectory(QByteArray* data, quint64 entryCount, quint64 centralSize, quint64 centralOffset)
{
	appendLe32(data, 0x06064b50);
	appendLe64(data, 44);
	appendLe16(data, 45);
	appendLe16(data, 45);
	appendLe32(data, 0);
	appendLe32(data, 0);
	appendLe64(data, entryCount);
	appendLe64(data, entryCount);
	appendLe64(data, centralSize);
	appendLe64(data, centralOffset);
}

void appendZip64Locator(QByteArray* data, quint64 zip64RecordOffset)
{
	appendLe32(data, 0x07064b50);
	appendLe32(data, 0);
	appendLe64(data, zip64RecordOffset);
	appendLe32(data, 1);
}

QVector<ZipPlaced> placeZipEntries(QByteArray* data, const QVector<ZipInput>& inputs)
{
	QVector<ZipPlaced> placed;
	placed.reserve(inputs.size());
	for (const ZipInput& input : inputs) {
		ZipPlaced entry;
		entry.name = input.name;
		entry.uncompressedSize = static_cast<quint32>(input.payload.size());
		entry.crc = input.useCrcOverride ? input.crcOverride : crc32Bytes(input.payload);
		if (input.deflate) {
			entry.method = 8;
			entry.stored = deflateRaw(input.payload);
		} else {
			entry.method = 0;
			entry.stored = input.payload;
		}
		entry.localOffset = static_cast<quint64>(data->size());
		appendLocalHeader(data, entry);
		placed.push_back(entry);
	}
	return placed;
}

QByteArray storedZipFixture()
{
	const QVector<ZipInput> inputs = {
		{QByteArray("maps/test.map"), QByteArray("{\n}"), false, 0, false},
		{QByteArray("textures/wall.tga"), QByteArray("TGA"), false, 0, false},
		{QByteArray("../escape.cfg"), QByteArray("bad"), false, 0, false},
	};

	QByteArray data;
	const QVector<ZipPlaced> placed = placeZipEntries(&data, inputs);
	const quint32 centralOffset = static_cast<quint32>(data.size());
	for (const ZipPlaced& entry : placed) {
		appendCentralRecord(&data, entry, {}, static_cast<quint32>(entry.stored.size()), entry.uncompressedSize,
			static_cast<quint32>(entry.localOffset));
	}
	const quint32 centralSize = static_cast<quint32>(data.size()) - centralOffset;
	appendEndOfCentralDirectory(&data, static_cast<quint16>(placed.size()), centralSize, centralOffset);
	return data;
}

QByteArray deflatedZipFixture(const QByteArray& payload, bool corruptCrc)
{
	QVector<ZipInput> inputs;
	ZipInput compressedEntry;
	compressedEntry.name = QByteArray("scripts/big.cfg");
	compressedEntry.payload = payload;
	compressedEntry.deflate = true;
	if (corruptCrc) {
		compressedEntry.useCrcOverride = true;
		compressedEntry.crcOverride = crc32Bytes(payload) ^ 0x5a5a5a5au;
	}
	inputs.push_back(compressedEntry);

	ZipInput storedEntry;
	storedEntry.name = QByteArray("readme.txt");
	storedEntry.payload = QByteArray("stored alongside\n");
	inputs.push_back(storedEntry);

	QByteArray data;
	const QVector<ZipPlaced> placed = placeZipEntries(&data, inputs);
	const quint32 centralOffset = static_cast<quint32>(data.size());
	for (const ZipPlaced& entry : placed) {
		appendCentralRecord(&data, entry, {}, static_cast<quint32>(entry.stored.size()), entry.uncompressedSize,
			static_cast<quint32>(entry.localOffset));
	}
	const quint32 centralSize = static_cast<quint32>(data.size()) - centralOffset;
	appendEndOfCentralDirectory(&data, static_cast<quint16>(placed.size()), centralSize, centralOffset);
	return data;
}

// A small archive that nonetheless uses the ZIP64 end of central directory and
// the 0x0001 extended information extra field, including one entry that only
// needs the two size fields and one that also needs the local header offset.
QByteArray zip64Fixture()
{
	const QVector<ZipInput> inputs = {
		{QByteArray("zip64/sizes.txt"), QByteArray("sizes only"), false, 0, false},
		{QByteArray("zip64/offset.txt"), QByteArray("sizes and offset"), true, 0, false},
	};

	QByteArray data;
	const QVector<ZipPlaced> placed = placeZipEntries(&data, inputs);
	const quint64 centralOffset = static_cast<quint64>(data.size());

	{
		const ZipPlaced& entry = placed.at(0);
		QByteArray extra;
		appendLe16(&extra, 0x0001);
		appendLe16(&extra, 16);
		appendLe64(&extra, entry.uncompressedSize);
		appendLe64(&extra, static_cast<quint64>(entry.stored.size()));
		appendCentralRecord(&data, entry, extra, 0xffffffffu, 0xffffffffu, static_cast<quint32>(entry.localOffset));
	}
	{
		const ZipPlaced& entry = placed.at(1);
		QByteArray extra;
		appendLe16(&extra, 0x0001);
		appendLe16(&extra, 24);
		appendLe64(&extra, entry.uncompressedSize);
		appendLe64(&extra, static_cast<quint64>(entry.stored.size()));
		appendLe64(&extra, entry.localOffset);
		appendCentralRecord(&data, entry, extra, 0xffffffffu, 0xffffffffu, 0xffffffffu);
	}

	const quint64 centralSize = static_cast<quint64>(data.size()) - centralOffset;
	const quint64 zip64Offset = static_cast<quint64>(data.size());
	appendZip64EndOfCentralDirectory(&data, static_cast<quint64>(placed.size()), centralSize, centralOffset);
	appendZip64Locator(&data, zip64Offset);
	appendEndOfCentralDirectory(&data, 0xffff, 0xffffffffu, 0xffffffffu);
	return data;
}

// More than 65535 entries: the 16-bit EOCD counter saturates, so the real count
// only exists in the ZIP64 end of central directory record.
QByteArray manyEntryZipFixture(int count)
{
	QByteArray data;
	data.reserve(static_cast<qsizetype>(count) * 96);

	QVector<quint32> offsets;
	offsets.reserve(count);
	QVector<QByteArray> names;
	names.reserve(count);

	for (int index = 0; index < count; ++index) {
		const QByteArray name = QByteArray("e/") + QByteArray::number(index).rightJustified(5, '0');
		names.push_back(name);
		offsets.push_back(static_cast<quint32>(data.size()));
		appendLe32(&data, 0x04034b50);
		appendLe16(&data, 20);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, 0);
		appendLe16(&data, static_cast<quint16>(name.size()));
		appendLe16(&data, 0);
		data.append(name);
	}

	const quint64 centralOffset = static_cast<quint64>(data.size());
	for (int index = 0; index < count; ++index) {
		appendLe32(&data, 0x02014b50);
		appendLe16(&data, 20);
		appendLe16(&data, 20);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, 0);
		appendLe16(&data, static_cast<quint16>(names.at(index).size()));
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, offsets.at(index));
		data.append(names.at(index));
	}

	const quint64 centralSize = static_cast<quint64>(data.size()) - centralOffset;
	const quint64 zip64Offset = static_cast<quint64>(data.size());
	appendZip64EndOfCentralDirectory(&data, static_cast<quint64>(count), centralSize, centralOffset);
	appendZip64Locator(&data, zip64Offset);
	appendEndOfCentralDirectory(&data, 0xffff, static_cast<quint32>(centralSize), static_cast<quint32>(centralOffset));
	return data;
}

// Valid records followed by garbage, with the end record still claiming three
// entries: the reader must fail and leave nothing behind.
QByteArray truncatedCentralDirectoryZipFixture()
{
	const QVector<ZipInput> inputs = {
		{QByteArray("ok/one.txt"), QByteArray("one"), false, 0, false},
		{QByteArray("ok/two.txt"), QByteArray("two"), false, 0, false},
	};

	QByteArray data;
	const QVector<ZipPlaced> placed = placeZipEntries(&data, inputs);
	const quint32 centralOffset = static_cast<quint32>(data.size());
	for (const ZipPlaced& entry : placed) {
		appendCentralRecord(&data, entry, {}, static_cast<quint32>(entry.stored.size()), entry.uncompressedSize,
			static_cast<quint32>(entry.localOffset));
	}
	for (int index = 0; index < 46; ++index) {
		data.append(static_cast<char>(0x5a));
	}
	const quint32 centralSize = static_cast<quint32>(data.size()) - centralOffset;
	appendEndOfCentralDirectory(&data, 3, centralSize, centralOffset);
	return data;
}

QByteArray layerZipFixture(const QVector<QPair<QByteArray, QByteArray>>& files)
{
	QVector<ZipInput> inputs;
	inputs.reserve(files.size());
	for (const QPair<QByteArray, QByteArray>& file : files) {
		ZipInput input;
		input.name = file.first;
		input.payload = file.second;
		input.deflate = true;
		inputs.push_back(input);
	}

	QByteArray data;
	const QVector<ZipPlaced> placed = placeZipEntries(&data, inputs);
	const quint32 centralOffset = static_cast<quint32>(data.size());
	for (const ZipPlaced& entry : placed) {
		appendCentralRecord(&data, entry, {}, static_cast<quint32>(entry.stored.size()), entry.uncompressedSize,
			static_cast<quint32>(entry.localOffset));
	}
	const quint32 centralSize = static_cast<quint32>(data.size()) - centralOffset;
	appendEndOfCentralDirectory(&data, static_cast<quint16>(placed.size()), centralSize, centralOffset);
	return data;
}

} // namespace

int main()
{
	bool ok = true;

	ok &= expect(hasFormat(PackageArchiveFormat::Folder), "missing folder package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Pak), "missing PAK package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Wad), "missing WAD package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Zip), "missing ZIP package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Pk3), "missing PK3 package descriptor");
	ok &= expect(packageArchiveFormatFromId(QStringLiteral("directory")) == PackageArchiveFormat::Folder, "directory id should map to folder");
	ok &= expect(packageArchiveFormatFromFileName(QStringLiteral("pak0.PAK")) == PackageArchiveFormat::Pak, "PAK extension not detected");
	ok &= expect(packageArchiveFormatFromFileName(QStringLiteral("textures.wad3")) == PackageArchiveFormat::Wad, "WAD3 extension not detected");
	ok &= expect(packageArchiveFormatFromFileName(QStringLiteral("baseq3/pak0.pk3")) == PackageArchiveFormat::Pk3, "PK3 extension not detected");

	// The advertised capabilities and descriptions must describe what is
	// implemented now, not a future slice.
	for (const PackageArchiveFormatDescriptor& descriptor : packageArchiveFormatDescriptors()) {
		ok &= expect(!descriptor.description.contains(QStringLiteral("placeholder")), "format description should not advertise a placeholder");
		ok &= expect(!descriptor.description.contains(QStringLiteral("next package slice")), "format description should not defer the reader");
		ok &= expect(!descriptor.capabilities.contains(QStringLiteral("future-write")), "format capabilities should not advertise unimplemented writing");
	}
	ok &= expect(descriptorFor(PackageArchiveFormat::Pk3).capabilities.contains(QStringLiteral("deflate")), "PK3 should advertise deflate support");
	ok &= expect(descriptorFor(PackageArchiveFormat::Pk3).capabilities.contains(QStringLiteral("zip64")), "PK3 should advertise zip64 support");
	ok &= expect(descriptorFor(PackageArchiveFormat::Zip).capabilities.contains(QStringLiteral("deflate")), "ZIP should advertise deflate support");

	PackageVirtualPath safe = normalizePackageVirtualPath(QStringLiteral("textures\\stone//wall01.tga"));
	ok &= expect(safe.isSafe(), "backslash path should normalize safely");
	ok &= expect(safe.normalizedPath == QStringLiteral("textures/stone/wall01.tga"), "normalized path mismatch");
	ok &= expect(packageVirtualPathFileName(safe.normalizedPath) == QStringLiteral("wall01.tga"), "file name extraction failed");
	ok &= expect(packageVirtualPathParent(safe.normalizedPath) == QStringLiteral("textures/stone"), "parent extraction failed");

	PackageVirtualPath trailing = normalizePackageVirtualPath(QStringLiteral("maps/start/"));
	ok &= expect(trailing.isSafe(), "trailing slash path should be safe");
	ok &= expect(trailing.normalizedPath == QStringLiteral("maps/start/"), "trailing slash should be preserved");

	ok &= expect(normalizePackageVirtualPath(QStringLiteral("")).issue == PackagePathIssue::Empty, "empty path should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("/absolute/path")).issue == PackagePathIssue::AbsolutePath, "absolute path should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("C:/games/pak0.pak")).issue == PackagePathIssue::DriveQualifiedPath, "drive path should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("../autoexec.cfg")).issue == PackagePathIssue::TraversalSegment, "leading traversal should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("textures/../autoexec.cfg")).issue == PackagePathIssue::TraversalSegment, "inner traversal should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("./textures/wall.tga")).issue == PackagePathIssue::CurrentDirectorySegment, "current-dir segment should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("pak0.pak:textures/wall.tga")).issue == PackagePathIssue::Colon, "colon should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("textures/\x01wall.tga")).issue == PackagePathIssue::ControlCharacter, "control character should be rejected");

	// Filesystem-only restrictions, enforced on every platform.
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("textures/wall.tga")) == PackagePathIssue::None, "ordinary path should pass filesystem checks");
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("CON")) == PackagePathIssue::ReservedDeviceName, "CON should be rejected");
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("scripts/nul.cfg")) == PackagePathIssue::ReservedDeviceName, "NUL with extension should be rejected");
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("com1/wall.tga")) == PackagePathIssue::ReservedDeviceName, "COM1 directory should be rejected");
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("scripts/lpt9.txt")) == PackagePathIssue::ReservedDeviceName, "LPT9 should be rejected");
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("scripts/com10.txt")) == PackagePathIssue::None, "COM10 is not a reserved device name");
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("scripts/trailing.")) == PackagePathIssue::TrailingDotOrSpace, "trailing dot should be rejected");
	ok &= expect(packageFilesystemPathIssue(QStringLiteral("scripts /wall.tga")) == PackagePathIssue::TrailingDotOrSpace, "trailing space segment should be rejected");
	ok &= expect(packagePathIssueId(PackagePathIssue::ReservedDeviceName) == QStringLiteral("reserved-device-name"), "reserved device issue id mismatch");
	ok &= expect(!packagePathIssueDisplayName(PackagePathIssue::TrailingDotOrSpace).isEmpty(), "trailing dot issue should have a display name");

	ok &= expect(packageEntryLooksNestedArchive(QStringLiteral("maps/detail.pk3")), "PK3 should look like nested archive");
	ok &= expect(packageEntryLooksNestedArchive(QStringLiteral("id1/pak1.pak")), "PAK should look like nested archive");
	ok &= expect(!packageEntryLooksNestedArchive(QStringLiteral("textures/wall.tga")), "TGA should not look like nested archive");

	QTemporaryDir root;
	ok &= expect(root.isValid(), "temporary root should be valid");
	QString error;
	const QString outputPath = safePackageOutputPath(root.path(), QStringLiteral("textures/stone/wall01.tga"), &error);
	ok &= expect(error.isEmpty(), "safe output path should not report error");
	ok &= expect(!outputPath.isEmpty(), "safe output path should be created");
	ok &= expect(packagePathIsInsideDirectory(root.path(), outputPath), "safe output path should stay inside root");
	const QString blocked = safePackageOutputPath(root.path(), QStringLiteral("../escape.cfg"), &error);
	ok &= expect(blocked.isEmpty(), "unsafe output path should be empty");
	ok &= expect(!error.isEmpty(), "unsafe output path should report error");
	const QString reservedOutput = safePackageOutputPath(root.path(), QStringLiteral("scripts/CON.cfg"), &error);
	ok &= expect(reservedOutput.isEmpty(), "reserved device name should not produce an output path");
	ok &= expect(!error.isEmpty(), "reserved device name should report an error");

	// A symlink or junction inside the output root must not let a write escape.
	{
		QTemporaryDir linkRoot;
		ok &= expect(linkRoot.isValid(), "symlink fixture root should be valid");
		const QString insideRoot = QDir(linkRoot.path()).filePath(QStringLiteral("inside"));
		const QString outsideRoot = QDir(linkRoot.path()).filePath(QStringLiteral("outside"));
		ok &= expect(QDir().mkpath(insideRoot), "symlink fixture inside root should be created");
		ok &= expect(QDir().mkpath(outsideRoot), "symlink fixture outside root should be created");
		const QString linkPath = QDir(insideRoot).filePath(QStringLiteral("escape"));
		if (makeDirectoryLink(linkPath, outsideRoot)) {
			ok &= expect(!packagePathIsInsideDirectory(insideRoot, QDir(linkPath).filePath(QStringLiteral("evil.cfg"))),
				"a path through a linked directory should not count as inside the root");
			QString linkError;
			const QString escaped = safePackageOutputPath(insideRoot, QStringLiteral("escape/evil.cfg"), &linkError);
			ok &= expect(escaped.isEmpty(), "extraction through a linked directory should be refused");
			ok &= expect(!linkError.isEmpty(), "extraction through a linked directory should report an error");
		} else {
			std::cerr << "note: skipping symlink traversal check (platform or privileges do not allow creating a directory link)\n";
		}
	}

	PackageArchiveSession session;
	PackageMountLayer primary;
	primary.id = QStringLiteral("pak0");
	primary.displayName = QStringLiteral("pak0.pak");
	primary.sourcePath = QStringLiteral("C:/Games/Quake/id1/pak0.pak");
	primary.format = PackageArchiveFormat::Pak;
	primary.entryCount = 42;
	ok &= expect(session.setPrimaryLayer(primary, &error), "primary layer should be accepted");
	ok &= expect(session.depth() == 1, "primary layer depth mismatch");
	ok &= expect(session.currentLayer().id == QStringLiteral("pak0"), "primary layer should be current");

	PackageMountLayer nested;
	nested.id = QStringLiteral("nested");
	nested.displayName = QStringLiteral("nested.pk3");
	nested.sourcePath = QStringLiteral("maps/nested.pk3");
	nested.mountPath = QStringLiteral("maps");
	nested.format = PackageArchiveFormat::Pk3;
	nested.entryCount = 3;
	ok &= expect(session.pushMountedLayer(nested, &error), "nested mount should be accepted");
	ok &= expect(session.depth() == 2, "nested mount depth mismatch");
	ok &= expect(session.currentLayer().mountPath == QStringLiteral("maps"), "nested mount path should normalize");
	ok &= expect(session.popMountedLayer(), "nested mount should pop");
	ok &= expect(session.depth() == 1, "depth after pop mismatch");

	nested.mountPath = QStringLiteral("../escape");
	ok &= expect(!session.pushMountedLayer(nested, &error), "unsafe nested mount should be rejected");
	ok &= expect(!error.isEmpty(), "unsafe nested mount should report error");

	QTemporaryDir packageRoot;
	ok &= expect(packageRoot.isValid(), "package fixture root should be valid");
	QDir packageDir(packageRoot.path());
	ok &= expect(packageDir.mkpath(QStringLiteral("folder/textures")), "folder fixture directory should be created");
	ok &= expect(writeFile(packageDir.filePath(QStringLiteral("folder/textures/wall.txt")), QByteArray("wall")), "folder fixture file should be written");

	PackageArchive archive;
	ok &= expect(archive.load(packageDir.filePath(QStringLiteral("folder")), &error), "folder package should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Folder, "folder package format mismatch");
	ok &= expect(archive.summary().fileCount == 1, "folder package file count mismatch");
	ok &= expect(archive.summary().directoryCount >= 1, "folder package should include synthetic directory");
	QByteArray bytes;
	ok &= expect(archive.readEntryBytes(QStringLiteral("textures/wall.txt"), &bytes, &error), "folder entry bytes should read");
	ok &= expect(bytes == QByteArray("wall"), "folder entry bytes mismatch");

	QTemporaryDir extractionRoot;
	ok &= expect(extractionRoot.isValid(), "extraction output root should be valid");
	PackageExtractionRequest dryRunRequest;
	dryRunRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("dry-run"));
	dryRunRequest.virtualPaths = {QStringLiteral("textures/wall.txt")};
	dryRunRequest.dryRun = true;
	PackageExtractionReport dryRunReport = extractPackageEntries(archive, dryRunRequest);
	ok &= expect(dryRunReport.succeeded(), "dry-run extraction should succeed");
	ok &= expect(dryRunReport.entries.size() == 1, "dry-run extraction should report one entry");
	ok &= expect(!dryRunReport.entries.front().outputPath.isEmpty(), "dry-run extraction should report output path");
	ok &= expect(!QFileInfo::exists(dryRunReport.entries.front().outputPath), "dry-run extraction should not write output file");
	ok &= expect(packageExtractionReportText(dryRunReport).contains(QStringLiteral("would write")), "dry-run report should describe staged write");

	PackageExtractionRequest extractRequest;
	extractRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("actual"));
	extractRequest.virtualPaths = {QStringLiteral("textures/wall.txt")};
	PackageExtractionReport extractReport = extractPackageEntries(archive, extractRequest);
	ok &= expect(extractReport.succeeded(), "selected extraction should succeed");
	ok &= expect(extractReport.writtenCount == 1, "selected extraction should write one entry");
	ok &= expect(extractReport.entries.size() == 1, "selected extraction should report one output entry");
	ok &= expect(packagePathIsInsideDirectory(extractRequest.targetDirectory, extractReport.entries.front().outputPath), "reported output should stay under extraction root");
	ok &= expect(QFileInfo::exists(extractReport.entries.front().outputPath), "selected extraction should create output file");
	QByteArray extractedBytes;
	ok &= expect(readBackFile(extractReport.entries.front().outputPath, &extractedBytes), "extracted file should open");
	ok &= expect(extractedBytes == QByteArray("wall"), "extracted file bytes mismatch");

	PackageExtractionReport noOverwriteReport = extractPackageEntries(archive, extractRequest);
	ok &= expect(noOverwriteReport.succeeded(), "no-overwrite extraction should not fail");
	ok &= expect(noOverwriteReport.skippedCount == 1, "no-overwrite extraction should skip existing output");
	ok &= expect(packageExtractionReportText(noOverwriteReport).contains(QStringLiteral("skipped")), "no-overwrite report should include skip state");

	// An empty selection with extractAll == false must be a no-op, not a silent
	// full-archive extraction.
	PackageExtractionRequest emptySelectionRequest;
	emptySelectionRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("empty-selection"));
	PackageExtractionReport emptySelectionReport = extractPackageEntries(archive, emptySelectionRequest);
	ok &= expect(emptySelectionReport.entries.isEmpty(), "empty selection should process no entries");
	ok &= expect(emptySelectionReport.writtenCount == 0, "empty selection should write nothing");
	ok &= expect(emptySelectionReport.requestedCount == 0, "empty selection should request nothing");
	ok &= expect(!emptySelectionReport.warnings.isEmpty(), "empty selection should warn");
	ok &= expect(!QFileInfo::exists(emptySelectionRequest.targetDirectory), "empty selection should not create the output directory");

	PackageExtractionRequest treeRequest;
	treeRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("tree"));
	treeRequest.virtualPaths = {QStringLiteral("textures")};
	treeRequest.dryRun = true;
	PackageExtractionReport treeReport = extractPackageEntries(archive, treeRequest);
	ok &= expect(treeReport.succeeded(), "directory extraction dry-run should succeed");
	ok &= expect(treeReport.requestedCount >= 2, "directory extraction should include subtree entries");

	PackageExtractionRequest cancelRequest;
	cancelRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("cancelled"));
	cancelRequest.extractAll = true;
	cancelRequest.dryRun = true;
	PackageExtractionReport cancelledReport = extractPackageEntries(archive, cancelRequest, [](const PackageExtractionEntryResult&, const PackageExtractionReport&) {
		return false;
	});
	ok &= expect(cancelledReport.cancelled, "extraction callback should be able to cancel");
	ok &= expect(!cancelledReport.succeeded(), "cancelled extraction should not report success");

	const QString pakPath = packageDir.filePath(QStringLiteral("tiny.pak"));
	ok &= expect(writeFile(pakPath, pakFixture()), "PAK fixture should be written");
	ok &= expect(archive.load(pakPath, &error), "PAK fixture should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Pak, "PAK format mismatch");
	ok &= expect(archive.summary().fileCount == 2, "PAK duplicate entries should remain visible");
	ok &= expect(!archive.warnings().isEmpty(), "PAK unsafe/duplicate warnings should be reported");
	ok &= expect(archive.readEntryBytes(QStringLiteral("scripts/autoexec.cfg"), &bytes, &error, 4), "PAK entry bytes should read");
	ok &= expect(bytes == QByteArray("echo"), "PAK entry byte prefix mismatch");

	const QString wadPath = packageDir.filePath(QStringLiteral("tiny.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "WAD fixture should be written");
	ok &= expect(archive.load(wadPath, &error), "WAD fixture should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Wad, "WAD format mismatch");
	ok &= expect(archive.summary().fileCount == 2, "WAD file count mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("MAP01"), &bytes, &error), "WAD lump bytes should read");
	ok &= expect(bytes == QByteArray("MAPDATA"), "WAD lump bytes mismatch");

	// A multi-map PWAD repeats THINGS/LINEDEFS/... once per map, so the entry
	// list legitimately holds duplicate virtual paths. entryPathLess treats
	// those as equivalent, so the entry sort has to be stable for findEntry to
	// really return the archive-order first match that the duplicate warning
	// promises; an unstable sort makes the returned lump compiler-dependent.
	{
		const int multiMapCount = 8;
		QByteArray expectedThings;
		const QString multiMapPath = packageDir.filePath(QStringLiteral("multimap.wad"));
		ok &= expect(writeFile(multiMapPath, multiMapWadFixture(multiMapCount, &expectedThings)), "multi-map WAD fixture should be written");
		ok &= expect(archive.load(multiMapPath, &error), "multi-map WAD fixture should load");
		ok &= expect(archive.summary().fileCount == multiMapCount * 11, "multi-map WAD should keep every duplicate lump");

		bool duplicateWarned = false;
		for (const PackageLoadWarning& warning : archive.warnings()) {
			if (warning.virtualPath.compare(QStringLiteral("THINGS"), Qt::CaseInsensitive) == 0) {
				duplicateWarned = true;
				break;
			}
		}
		ok &= expect(duplicateWarned, "duplicate lump names should be reported as a load warning");

		ok &= expect(archive.readEntryBytes(QStringLiteral("THINGS"), &bytes, &error), "duplicated THINGS lump should read");
		ok &= expect(bytes == expectedThings, "duplicated lump reads must return the first archive-order match");

		// The same tie-break has to hold for the entry list the UI and the
		// extractor walk, not just for findEntry.
		QByteArray firstListedThings;
		for (const PackageEntry& entry : archive.entries()) {
			if (entry.virtualPath.compare(QStringLiteral("THINGS"), Qt::CaseInsensitive) == 0) {
				ok &= expect(archive.readEntryBytes(entry.virtualPath, &firstListedThings, &error), "listed THINGS lump should read");
				break;
			}
		}
		ok &= expect(firstListedThings == expectedThings, "the first listed duplicate must be the first archive-order one");
	}

	const QString pk3Path = packageDir.filePath(QStringLiteral("tiny.pk3"));
	ok &= expect(writeFile(pk3Path, storedZipFixture()), "PK3 fixture should be written");
	ok &= expect(archive.load(pk3Path, &error), "PK3 fixture should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Pk3, "PK3 format mismatch");
	ok &= expect(archive.summary().fileCount == 2, "PK3 unsafe entry should be skipped");
	ok &= expect(archive.summary().directoryCount >= 2, "PK3 synthetic directories should be visible");
	ok &= expect(!archive.warnings().isEmpty(), "PK3 unsafe warning should be reported");
	ok &= expect(archive.readEntryBytes(QStringLiteral("maps/test.map"), &bytes, &error), "PK3 stored entry bytes should read");
	ok &= expect(bytes == QByteArray("{\n}"), "PK3 stored entry bytes mismatch");

	// Content sniffing: the magic wins over the extension, and an extensionless
	// file still opens.
	const QString disguisedPk3 = packageDir.filePath(QStringLiteral("disguised.pk3"));
	ok &= expect(writeFile(disguisedPk3, pakFixture()), "disguised PAK fixture should be written");
	ok &= expect(archive.load(disguisedPk3, &error), "a PAK named .pk3 should still load");
	ok &= expect(archive.format() == PackageArchiveFormat::Pak, "a PAK named .pk3 should be reported as PAK");

	const QString disguisedPak = packageDir.filePath(QStringLiteral("disguised.pak"));
	ok &= expect(writeFile(disguisedPak, storedZipFixture()), "disguised ZIP fixture should be written");
	ok &= expect(archive.load(disguisedPak, &error), "a ZIP named .pak should still load");
	ok &= expect(archive.format() == PackageArchiveFormat::Zip, "a ZIP named .pak should be reported as ZIP");
	ok &= expect(archive.readEntryBytes(QStringLiteral("maps/test.map"), &bytes, &error), "disguised ZIP entry should read");
	ok &= expect(bytes == QByteArray("{\n}"), "disguised ZIP entry bytes mismatch");

	const QString extensionless = packageDir.filePath(QStringLiteral("noextension"));
	ok &= expect(writeFile(extensionless, wadFixture()), "extensionless WAD fixture should be written");
	ok &= expect(archive.load(extensionless, &error), "an extensionless WAD should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Wad, "an extensionless WAD should be reported as WAD");

	// Format fallback must never leave a half-parsed, mixed-format listing.
	const QString brokenPath = packageDir.filePath(QStringLiteral("broken.dat"));
	ok &= expect(writeFile(brokenPath, truncatedCentralDirectoryZipFixture()), "broken ZIP fixture should be written");
	ok &= expect(!archive.load(brokenPath, &error), "a corrupt central directory should fail to load");
	ok &= expect(!error.isEmpty(), "a failed load should report an error");
	ok &= expect(!archive.isOpen(), "a failed load should leave the archive closed");
	ok &= expect(archive.entries().isEmpty(), "a failed load should leave no entries behind");
	ok &= expect(archive.format() == PackageArchiveFormat::Unknown, "a failed load should leave no format behind");
	ok &= expect(archive.warnings().isEmpty(), "a failed load should leave no warnings behind");

	// Deflated entries must read back byte-identical, honour maxBytes, and fail
	// loudly on a CRC mismatch.
	QByteArray payload;
	for (int index = 0; index < 400; ++index) {
		payload += QByteArray("// vibestudio deflate fixture line ") + QByteArray::number(index) + QByteArray("\n");
	}
	const QString deflatedPath = packageDir.filePath(QStringLiteral("deflated.pk3"));
	ok &= expect(writeFile(deflatedPath, deflatedZipFixture(payload, false)), "deflated PK3 fixture should be written");
	ok &= expect(archive.load(deflatedPath, &error), "deflated PK3 fixture should load");
	bool foundDeflatedEntry = false;
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.virtualPath == QStringLiteral("scripts/big.cfg")) {
			foundDeflatedEntry = true;
			ok &= expect(entry.storageMethod == QStringLiteral("deflated"), "deflated entry storage method mismatch");
			ok &= expect(entry.readable, "deflated entry should be readable");
			ok &= expect(entry.hasCrc32, "deflated entry should carry a CRC-32");
			ok &= expect(entry.sizeBytes == static_cast<quint64>(payload.size()), "deflated entry uncompressed size mismatch");
			ok &= expect(entry.compressedSizeBytes < entry.sizeBytes, "deflated entry should be smaller than its payload");
		}
	}
	ok &= expect(foundDeflatedEntry, "deflated entry should be listed");
	ok &= expect(archive.readEntryBytes(QStringLiteral("scripts/big.cfg"), &bytes, &error), "deflated entry should read");
	ok &= expect(bytes == payload, "deflated entry should read back byte-identical");
	ok &= expect(archive.readEntryBytes(QStringLiteral("scripts/big.cfg"), &bytes, &error, 24), "deflated entry preview should read");
	ok &= expect(bytes == payload.left(24), "deflated entry preview should be truncated to maxBytes");
	ok &= expect(archive.readEntryBytes(QStringLiteral("readme.txt"), &bytes, &error), "stored sibling entry should read");
	ok &= expect(bytes == QByteArray("stored alongside\n"), "stored sibling entry bytes mismatch");

	QTemporaryDir deflateExtractionRoot;
	ok &= expect(deflateExtractionRoot.isValid(), "deflate extraction root should be valid");
	PackageExtractionRequest deflateExtract;
	deflateExtract.targetDirectory = deflateExtractionRoot.path();
	deflateExtract.extractAll = true;
	deflateExtract.overwriteExisting = true;
	const PackageExtractionReport deflateReport = extractPackageEntries(archive, deflateExtract);
	ok &= expect(deflateReport.succeeded(), "deflated archive should extract without errors");
	QByteArray extractedPayload;
	ok &= expect(readBackFile(QDir(deflateExtractionRoot.path()).filePath(QStringLiteral("scripts/big.cfg")), &extractedPayload), "deflated entry should be written to disk");
	ok &= expect(extractedPayload == payload, "extracted deflated entry bytes mismatch");

	const QString corruptPath = packageDir.filePath(QStringLiteral("corrupt.pk3"));
	ok &= expect(writeFile(corruptPath, deflatedZipFixture(payload, true)), "CRC-mismatch fixture should be written");
	ok &= expect(archive.load(corruptPath, &error), "CRC-mismatch fixture should still list");
	ok &= expect(!archive.readEntryBytes(QStringLiteral("scripts/big.cfg"), &bytes, &error), "a CRC mismatch must fail the read");
	ok &= expect(!error.isEmpty(), "a CRC mismatch must report an error");
	ok &= expect(error.contains(QStringLiteral("CRC")), "a CRC mismatch must name the CRC check");
	ok &= expect(bytes.isEmpty(), "a failed read must not hand back partial bytes");

	// ZIP64 extended information extra field.
	const QString zip64Path = packageDir.filePath(QStringLiteral("zip64.pk3"));
	ok &= expect(writeFile(zip64Path, zip64Fixture()), "ZIP64 fixture should be written");
	ok &= expect(archive.load(zip64Path, &error), "ZIP64 fixture should load");
	ok &= expect(archive.summary().fileCount == 2, "ZIP64 fixture file count mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("zip64/sizes.txt"), &bytes, &error), "ZIP64 stored entry should read");
	ok &= expect(bytes == QByteArray("sizes only"), "ZIP64 stored entry bytes mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("zip64/offset.txt"), &bytes, &error), "ZIP64 offset entry should read");
	ok &= expect(bytes == QByteArray("sizes and offset"), "ZIP64 offset entry bytes mismatch");

	// More than 65535 entries: the listing must not truncate at the 16-bit EOCD
	// counter.
	const int manyEntryCount = 70000;
	const QString manyPath = packageDir.filePath(QStringLiteral("many.pk3"));
	ok &= expect(writeFile(manyPath, manyEntryZipFixture(manyEntryCount)), "large-entry fixture should be written");
	ok &= expect(archive.load(manyPath, &error), "large-entry fixture should load");
	ok &= expect(archive.summary().fileCount == manyEntryCount, "large-entry fixture should list every entry");
	ok &= expect(archive.readEntryBytes(QStringLiteral("e/69999"), &bytes, &error), "the last of 70000 entries should be addressable");
	ok &= expect(bytes.isEmpty(), "the last of 70000 entries is empty");

	// Nested mounting: later layers shadow earlier ones, pk3 style.
	const QString baseLayerPath = packageDir.filePath(QStringLiteral("pak0-base.pk3"));
	const QString overLayerPath = packageDir.filePath(QStringLiteral("pak1-over.pk3"));
	ok &= expect(writeFile(baseLayerPath, layerZipFixture({
		{QByteArray("scripts/base-only.cfg"), QByteArray("from base layer")},
		{QByteArray("shared.cfg"), QByteArray("base wins nothing")},
	})), "base layer fixture should be written");
	ok &= expect(writeFile(overLayerPath, layerZipFixture({
		{QByteArray("scripts/over-only.cfg"), QByteArray("from overriding layer")},
		{QByteArray("shared.cfg"), QByteArray("override wins")},
	})), "override layer fixture should be written");

	PackageArchiveSession mounted;
	ok &= expect(mounted.openPrimaryArchive(baseLayerPath, &error), "primary archive should open");
	ok &= expect(mounted.hasOpenArchive(), "session should report an open archive");
	ok &= expect(mounted.openArchiveCount() == 1, "session should count the primary archive");
	ok &= expect(mounted.readEntryBytes(QStringLiteral("shared.cfg"), &bytes, &error), "primary layer entry should read");
	ok &= expect(bytes == QByteArray("base wins nothing"), "primary layer entry bytes mismatch");

	ok &= expect(mounted.mountArchive(overLayerPath, QString(), &error), "override archive should mount");
	ok &= expect(mounted.openArchiveCount() == 2, "session should count both archives");
	ok &= expect(mounted.depth() == 2, "session depth should include the mounted layer");
	ok &= expect(mounted.readEntryBytes(QStringLiteral("shared.cfg"), &bytes, &error), "shadowed entry should read");
	ok &= expect(bytes == QByteArray("override wins"), "the later layer should shadow the earlier one");
	ok &= expect(mounted.entryLayerIndex(QStringLiteral("shared.cfg")) == 1, "shadowed entry should belong to the mounted layer");
	ok &= expect(mounted.entryLayerId(QStringLiteral("shared.cfg")) == QStringLiteral("pak1-over.pk3"), "shadowed entry layer id mismatch");
	ok &= expect(mounted.entryLayerIndex(QStringLiteral("scripts/base-only.cfg")) == 0, "unshadowed entry should belong to the primary layer");
	ok &= expect(mounted.entryLayerId(QStringLiteral("scripts/base-only.cfg")) == QStringLiteral("pak0-base.pk3"), "primary entry layer id mismatch");
	ok &= expect(mounted.readEntryBytes(QStringLiteral("scripts/over-only.cfg"), &bytes, &error), "mounted-only entry should read");
	ok &= expect(bytes == QByteArray("from overriding layer"), "mounted-only entry bytes mismatch");
	ok &= expect(mounted.readEntryBytes(QStringLiteral("scripts/base-only.cfg"), &bytes, &error), "primary-only entry should read");
	ok &= expect(bytes == QByteArray("from base layer"), "primary-only entry bytes mismatch");

	int mergedFileCount = 0;
	bool sawSharedOnce = false;
	bool duplicateShared = false;
	for (const PackageEntry& entry : mounted.entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		++mergedFileCount;
		if (entry.virtualPath == QStringLiteral("shared.cfg")) {
			if (sawSharedOnce) {
				duplicateShared = true;
			}
			sawSharedOnce = true;
			ok &= expect(entry.layerId == QStringLiteral("pak1-over.pk3"), "merged entry should name its owning layer");
		}
	}
	ok &= expect(mergedFileCount == 3, "merged listing should collapse the shadowed path");
	ok &= expect(sawSharedOnce && !duplicateShared, "merged listing should contain exactly one shared.cfg");
	ok &= expect(mounted.summary().fileCount == 3, "session summary should count merged files");

	// A mounted layer can be relocated under a virtual subdirectory.
	PackageArchiveSession relocated;
	ok &= expect(relocated.openPrimaryArchive(baseLayerPath, &error), "relocated session primary should open");
	ok &= expect(relocated.mountArchive(pakPath, QStringLiteral("mounted/pak"), &error), "PAK layer should mount under a subdirectory");
	ok &= expect(relocated.readEntryBytes(QStringLiteral("mounted/pak/scripts/autoexec.cfg"), &bytes, &error), "mounted PAK entry should read through its mount path");
	ok &= expect(bytes == QByteArray("echo hello\n"), "mounted PAK entry bytes mismatch");
	ok &= expect(relocated.entryLayerIndex(QStringLiteral("mounted/pak/scripts/autoexec.cfg")) == 1, "mounted PAK entry should belong to the mounted layer");
	ok &= expect(!relocated.readEntryBytes(QStringLiteral("scripts/autoexec.cfg"), &bytes, &error), "a mounted entry must not be visible at the session root");

	relocated.clear();
	ok &= expect(!relocated.hasOpenArchive(), "cleared session should hold no archives");
	ok &= expect(relocated.depth() == 0, "cleared session depth should be zero");

	return ok ? 0 : 1;
}
