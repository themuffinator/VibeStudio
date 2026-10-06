#include "core/package_zip_p.h"

#include "core/deflate.h"

#include <QCoreApplication>
#include <QIODevice>
#include <QtEndian>

#include <algorithm>
#include <array>

namespace vibestudio {
namespace {

// Original implementation of PKWARE APPNOTE 6.3.10 (2022-11-01), sections
// 4.3.6-4.3.16, 4.5.3, 4.6.9 and Appendix D. No upstream code is incorporated.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
constexpr quint32 sentinel32 = 0xffffffffu;
constexpr quint16 sentinel16 = 0xffffu;

quint16 u16(QByteArrayView bytes, qsizetype offset) { return qFromLittleEndian<quint16>(bytes.data() + offset); }
quint32 u32(QByteArrayView bytes, qsizetype offset) { return qFromLittleEndian<quint32>(bytes.data() + offset); }
quint64 u64(QByteArrayView bytes, qsizetype offset) { return qFromLittleEndian<quint64>(bytes.data() + offset); }

bool fail(QString* error, const QString& message)
{
	if (error) { *error = message; }
	return false;
}

bool read(QIODevice& file, quint64 offset, qint64 count, QByteArray* bytes)
{
	const qint64 size = file.size();
	if (size < 0 || count < 0 || offset > quint64(size) || quint64(count) > quint64(size) - offset
		|| !file.seek(qint64(offset))) { return false; }
	*bytes = file.read(count);
	return bytes->size() == count;
}

struct ExtraFields {
	QByteArrayView zip64;
	QByteArrayView unicode;
	bool hasZip64 = false;
	bool hasUnicode = false;
};

bool extras(QByteArrayView bytes, ExtraFields* fields, QString* error)
{
	for (qsizetype at = 0; at < bytes.size();) {
		if (bytes.size() - at < 4 || u16(bytes, at + 2) > bytes.size() - at - 4) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP extra field is truncated."));
		}
		const auto id = u16(bytes, at);
		const auto length = u16(bytes, at + 2);
		if (id == 1 || id == 0x7075) {
			bool& present = id == 1 ? fields->hasZip64 : fields->hasUnicode;
			if (present) {
				return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP contains duplicate ZIP64 or Unicode Path fields."));
			}
			present = true;
			(id == 1 ? fields->zip64 : fields->unicode) = bytes.sliced(at + 4, length);
		}
		at += 4 + length;
	}
	return true;
}

bool utf8Name(QByteArrayView wire, QString* name, QString* error)
{
	*name = QString::fromUtf8(wire);
	// Round-trip validation rejects malformed/incomplete UTF-8 and BOM removal;
	// an actual, encoded U+FFFD remains a legitimate filename character.
	if (name->toUtf8() != wire || name->startsWith(QChar(0xfeff))) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP filename is not valid UTF-8 without a byte-order mark."));
	}
	return true;
}

// IBM CP437 high-byte mapping derived from Unicode's cp437_DOSLatinUS table
// version 2.00 (1996-04-24). Copyright Unicode, Inc.; Unicode License V3.
// https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/PC/CP437.TXT
// Full notice: docs/licenses/UNICODE-LICENSE.txt (also installed with binaries).
constexpr std::array<ushort, 128> cp437 = {
	0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
	0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
	0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
	0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
	0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
	0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
	0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
	0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
	0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
	0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
	0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
	0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
	0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
	0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

bool decodeName(QByteArrayView wire, quint16 flags, const ExtraFields& fields,
	QString* name, QString* error)
{
	if (flags & 0x0800) { return utf8Name(wire, name, error); }
	if (fields.hasUnicode) {
		if (fields.unicode.isEmpty()) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP Unicode Path field is truncated."));
		}
		if (fields.unicode[0] == 1) {
			if (fields.unicode.size() < 5) {
				return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP Unicode Path field is truncated."));
			}
			if (u32(fields.unicode, 1) == crc32View(wire)) { return utf8Name(fields.unicode.sliced(5), name, error); }
		}
		// APPNOTE 4.6.9: unknown versions and stale name checksums use the
		// original header encoding, not an untrusted replacement filename.
	}
	name->clear();
	name->reserve(wire.size());
	for (const unsigned char byte : wire) { name->append(QChar(byte < 128 ? ushort(byte) : cp437[byte - 128])); }
	return true;
}

bool descriptorMatches(QByteArrayView bytes, qsizetype start, bool zip64, const ZipEntryMetadata& entry)
{
	if (bytes.size() - start < (zip64 ? 20 : 12) || u32(bytes, start) != entry.crc) { return false; }
	return zip64 ? u64(bytes, start + 4) == entry.compressedSize && u64(bytes, start + 12) == entry.size
		: u32(bytes, start + 4) == entry.compressedSize && u32(bytes, start + 8) == entry.size;
}

} // namespace

bool readZipDirectoryMetadata(QIODevice& file, qint64 maximumMetadataBytes,
	ZipDirectoryMetadata* directory, QString* error)
{
	*directory = {};
	const qint64 fileSize = file.size();
	QByteArray tail;
	const qint64 scanSize = std::min<qint64>(fileSize, 65535 + 22);
	if (fileSize < 22 || !read(file, fileSize - scanSize, scanSize, &tail)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP end directory not found."));
	}
	qsizetype end = tail.size() - 22;
	for (; end >= 0; --end) {
		if (u32(tail, end) == 0x06054b50 && end + 22 + u16(tail, end + 20) == tail.size()) { break; }
	}
	if (end < 0) { return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP end directory not found.")); }
	const quint64 endOffset = fileSize - scanSize + end;
	const quint16 disk = u16(tail, end + 4), centralDisk = u16(tail, end + 6);
	const quint16 diskEntries = u16(tail, end + 8), entries = u16(tail, end + 10);
	const quint32 size = u32(tail, end + 12), offset = u32(tail, end + 16);
	directory->entries = entries;
	directory->size = size;
	directory->offset = offset;
	directory->endRecordsOffset = endOffset;
	QByteArray locator;
	const bool zip64 = endOffset >= 20 && read(file, endOffset - 20, 20, &locator) && u32(locator, 0) == 0x07064b50;
	if (zip64) {
		if (u32(locator, 4) != 0 || u32(locator, 16) != 1) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "Multi-disk ZIP archives are not supported."));
		}
		const quint64 locatorOffset = endOffset - 20;
		const quint64 recordOffset = u64(locator, 8);
		QByteArray record;
		if (recordOffset > locatorOffset || locatorOffset - recordOffset < 56
			|| !read(file, recordOffset, 56, &record) || u32(record, 0) != 0x06064b50
			|| u64(record, 4) != locatorOffset - recordOffset - 12) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP64 end record has an invalid size or location."));
		}
		if (u32(record, 16) != 0 || u32(record, 20) != 0) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "Multi-disk ZIP archives are not supported."));
		}
		directory->entries = u64(record, 32);
		directory->size = u64(record, 40);
		directory->offset = u64(record, 48);
		directory->endRecordsOffset = recordOffset;
		if (u64(record, 24) != directory->entries
			|| (disk != sentinel16 && disk != 0) || (centralDisk != sentinel16 && centralDisk != 0)
			|| (entries != sentinel16 && entries != directory->entries)
			|| (diskEntries != sentinel16 && diskEntries != directory->entries)
			|| (size != sentinel32 && size != directory->size) || (offset != sentinel32 && offset != directory->offset)) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP and ZIP64 end directory fields disagree."));
		}
		const quint64 extensionBytes = locatorOffset - recordOffset - 56;
		if (extensionBytes > quint64(maximumMetadataBytes)) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP64 end record exceeds the metadata indexing limit."));
		}
		directory->additionalMetadataBytes = qint64(extensionBytes);
		// Version 1 extensible data uses a 16-bit ID and a 32-bit byte count.
		// Skip bounded unknown fields without allocating their payloads.
		for (quint64 at = recordOffset + 56; at < locatorOffset;) {
			QByteArray field;
			if (locatorOffset - at < 6 || !read(file, at, 6, &field) || u32(field, 2) > locatorOffset - at - 6) {
				return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP64 end record extension is truncated or unsupported."));
			}
			at += 6 + quint64(u32(field, 2));
		}
	} else {
		if (disk == sentinel16 || centralDisk == sentinel16 || diskEntries == sentinel16
			|| entries == sentinel16 || size == sentinel32 || offset == sentinel32) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP archive uses ZIP64 values but has no ZIP64 end of central directory record."));
		}
		if (disk != 0 || centralDisk != 0) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "Multi-disk ZIP archives are not supported."));
		}
		if (diskEntries != entries) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP end directory entry counts disagree."));
		}
	}
	if (directory->offset > directory->endRecordsOffset || directory->size > directory->endRecordsOffset - directory->offset) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP central directory is out of range."));
	}
	const quint64 gap = directory->endRecordsOffset - directory->offset - directory->size;
	// Some producers exclude the optional digital-signature record from the
	// declared directory size. finishZipDirectory verifies either placement.
	if (gap > 65535 + 6) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "Unexpected data follows the ZIP central directory."));
	}
	directory->additionalMetadataBytes += qint64(gap);
	return true;
}

bool finishZipDirectory(QIODevice& file, const ZipDirectoryMetadata& directory, quint64 consumed, QString* error)
{
	if (consumed > directory.size) { return false; }
	const quint64 remaining = directory.endRecordsOffset - directory.offset - consumed;
	if (!remaining) { return true; }
	QByteArray signature;
	if (remaining < 6 || !read(file, directory.offset + consumed, 6, &signature)
		|| u32(signature, 0) != 0x05054b50 || remaining != quint64(6 + u16(signature, 4))
		|| (consumed != directory.size && directory.offset + directory.size != directory.endRecordsOffset)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP central directory size does not match its records."));
	}
	// Only its structure is recognized. Package integrity validation does not
	// establish the authenticity of a digital signature.
	return true;
}

bool readZipEntryMetadata(QByteArrayView record, ZipEntryMetadata* entry, QString* error)
{
	*entry = {};
	if (record.size() < 46 || u32(record, 0) != 0x02014b50
		|| record.size() != 46 + u16(record, 28) + u16(record, 30) + u16(record, 32)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "Invalid ZIP central directory record."));
	}
	entry->flags = u16(record, 8);
	entry->method = u16(record, 10);
	entry->crc = u32(record, 16);
	entry->compressedSize = u32(record, 20);
	entry->size = u32(record, 24);
	entry->disk = u16(record, 34);
	entry->localOffset = u32(record, 42);
	ExtraFields fields;
	if (!extras(record.sliced(46 + u16(record, 28), u16(record, 30)), &fields, error)) { return false; }
	const bool size64 = entry->size == sentinel32, compressed64 = entry->compressedSize == sentinel32;
	const bool offset64 = entry->localOffset == sentinel32, disk64 = entry->disk == sentinel16;
	entry->zip64Sizes = size64 || compressed64;
	entry->hasZip64 = fields.hasZip64;
	const qsizetype required = (size64 ? 8 : 0) + (compressed64 ? 8 : 0) + (offset64 ? 8 : 0) + (disk64 ? 4 : 0);
	if (required && (!fields.hasZip64 || fields.zip64.size() < required)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP entry has an incomplete ZIP64 extended information field."));
	}
	qsizetype at = 0;
	if (size64) { entry->size = u64(fields.zip64, at); at += 8; }
	if (compressed64) { entry->compressedSize = u64(fields.zip64, at); at += 8; }
	if (offset64) { entry->localOffset = u64(fields.zip64, at); at += 8; }
	if (disk64) { entry->disk = u32(fields.zip64, at); }
	return decodeName(record.sliced(46, u16(record, 28)), entry->flags, fields, &entry->name, error);
}

bool readZipLocalMetadata(QIODevice& file, quint64 directoryOffset, const ZipEntryMetadata& entry,
	QByteArrayView wireName, qint64* dataOffset, QString* error)
{
	*dataOffset = -1;
	QByteArray header;
	if (entry.localOffset > directoryOffset || directoryOffset - entry.localOffset < 30
		|| !read(file, entry.localOffset, 30, &header) || u32(header, 0) != 0x04034b50) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP local file header could not be located for this entry."));
	}
	const quint16 nameLength = u16(header, 26), extraLength = u16(header, 28);
	const quint64 tailOffset = entry.localOffset + 30;
	QByteArray tail;
	if (quint64(nameLength) + extraLength > directoryOffset - tailOffset
		|| !read(file, tailOffset, qint64(nameLength) + extraLength, &tail)
		|| QByteArrayView(tail).first(nameLength) != wireName
		|| u16(header, 6) != entry.flags || u16(header, 8) != entry.method) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP local header does not match its central directory record."));
	}
	ExtraFields fields;
	if (!extras(QByteArrayView(tail).sliced(nameLength), &fields, error)) { return false; }
	if (fields.hasUnicode) {
		QString localName;
		// A valid local Unicode override must agree with the directory. Missing,
		// stale or unknown overrides are allowed; the directory names the entry.
		if (!decodeName(wireName, entry.flags, fields, &localName, error)) { return false; }
		if (!(entry.flags & 0x0800) && !fields.unicode.isEmpty() && fields.unicode[0] == 1
			&& fields.unicode.size() >= 5 && u32(fields.unicode, 1) == crc32View(wireName) && localName != entry.name) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP local and central Unicode filenames disagree."));
		}
	}
	quint64 compressed = u32(header, 18), size = u32(header, 22);
	const bool descriptor = (entry.flags & 8) != 0;
	const bool localSentinel = compressed == sentinel32 || size == sentinel32;
	if ((fields.hasZip64 && fields.zip64.size() < 16) || (localSentinel && !fields.hasZip64)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP local ZIP64 field must contain both size values."));
	}
	if (fields.hasZip64) {
		const auto zipSize = u64(fields.zip64, 0), zipCompressed = u64(fields.zip64, 8);
		if ((compressed != sentinel32 && compressed != zipCompressed && !(descriptor && compressed == 0))
			|| (size != sentinel32 && size != zipSize && !(descriptor && size == 0))) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP local size fields disagree with their ZIP64 values."));
		}
		compressed = zipCompressed;
		size = zipSize;
	}
	const auto matches = [descriptor](quint64 local, quint64 central) { return local == central || (descriptor && local == 0); };
	if (!matches(u32(header, 14), entry.crc) || !matches(compressed, entry.compressedSize) || !matches(size, entry.size)) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP local header does not match its central directory record."));
	}
	const quint64 start = tailOffset + nameLength + extraLength;
	if (entry.compressedSize > directoryOffset - start) {
		return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP payload overlaps the central directory."));
	}
	if (descriptor) {
		const quint64 descriptorOffset = start + entry.compressedSize;
		const bool wide = entry.zip64Sizes || fields.hasZip64;
		// Offset/disk-only ZIP64 records can accompany either width: the local
		// streaming writer may not know its final central-directory offset.
		// Require an exact CRC and both sizes for every admitted interpretation.
		const bool allowWide = wide || entry.hasZip64;
		const auto matchesDescriptor = [&](QByteArrayView bytes, qsizetype at) {
			return (!wide && descriptorMatches(bytes, at, false, entry))
				|| (allowWide && descriptorMatches(bytes, at, true, entry));
		};
		QByteArray bytes;
		const qint64 count = qint64(std::min<quint64>(allowWide ? 24 : 16, directoryOffset - descriptorOffset));
		if (!read(file, descriptorOffset, count, &bytes)
			|| !(matchesDescriptor(bytes, 0)
				|| (bytes.size() >= 4 && u32(bytes, 0) == 0x08074b50 && matchesDescriptor(bytes, 4)))) {
			return fail(error, QCoreApplication::translate("VibeStudioPackageArchive", "ZIP data descriptor is missing or disagrees with its central directory record."));
		}
	}
	*dataOffset = qint64(start);
	return true;
}

} // namespace vibestudio
