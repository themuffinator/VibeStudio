#include "core/package_staging.h"

#include "core/deflate.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTime>

#include <algorithm>
#include <functional>
#include <limits>

namespace vibestudio {

namespace {

QString stageText(const char* source)
{
	return QCoreApplication::translate("VibeStudioPackageStaging", source);
}

// ZIP record signatures, PKWARE .ZIP File Format Specification (APPNOTE.TXT)
// sections 4.3.7, 4.3.12, 4.3.14, 4.3.15 and 4.3.16.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
constexpr quint32 kZipLocalFileSignature = 0x04034b50;
constexpr quint32 kZipCentralDirectorySignature = 0x02014b50;
constexpr quint32 kZipEndOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 kZip64EndOfCentralDirectorySignature = 0x06064b50;
constexpr quint32 kZip64EndOfCentralDirectoryLocatorSignature = 0x07064b50;
constexpr quint16 kZip64ExtraFieldId = 0x0001;
constexpr quint32 kZip32Sentinel = 0xffffffffu;
constexpr quint16 kZip16Sentinel = 0xffffu;

// Quake WAD2 / Half-Life WAD3 texture WAD directory records are 32 bytes:
// int32 offset, int32 diskSize, int32 size, uint8 type, uint8 compression,
// int16 padding, char name[16]. Doom IWAD/PWAD records are 16 bytes:
// int32 offset, int32 size, char name[8].
// Sources: Quake Standards Group "Quake Documentation Version 3.4" (WAD2), the
// Half-Life SDK WAD3 layout notes, and the Unofficial Doom Specs v1.666
// (https://www.gamers.org/dhs/helpdocs/dmsp1666.html).
constexpr int kDoomWadRecordSize = 16;
constexpr int kTextureWadRecordSize = 32;
constexpr int kWadHeaderSize = 12;
constexpr int kDoomWadNameLimit = 8;
constexpr int kTextureWadNameLimit = 16;
constexpr quint8 kWad2MiptexType = 0x44;
constexpr quint8 kWad3MiptexType = 0x43;

constexpr int kPakHeaderSize = 12;
constexpr int kPakRecordSize = 64;
constexpr int kPakNameLimit = 56;

QString normalizedId(QString value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString entryKey(const QString& virtualPath)
{
	return virtualPath.toCaseFolded();
}

// Total order: case-insensitive first so that related names stay together, then
// a case-sensitive tiebreak so entries that differ only in case (which PAK and
// ZIP both allow) have exactly one valid position.
bool stagedEntryLess(const PackageStagedEntry& left, const PackageStagedEntry& right)
{
	const int folded = left.virtualPath.compare(right.virtualPath, Qt::CaseInsensitive);
	if (folded != 0) {
		return folded < 0;
	}
	return left.virtualPath.compare(right.virtualPath, Qt::CaseSensitive) < 0;
}

void sortStagedEntries(QVector<PackageStagedEntry>* entries)
{
	if (!entries) {
		return;
	}
	std::stable_sort(entries->begin(), entries->end(), stagedEntryLess);
}

// Source order for formats where position, not path, identifies an entry.
// Entries with no ordinal (staged additions) keep their insertion order at the
// end, which is the only place a brand new lump can go without disturbing the
// map runs that are already there.
void sortStagedEntriesBySourceOrder(QVector<PackageStagedEntry>* entries)
{
	if (!entries) {
		return;
	}
	std::stable_sort(entries->begin(), entries->end(), [](const PackageStagedEntry& left, const PackageStagedEntry& right) {
		const int leftOrdinal = left.sourceOrdinal < 0 ? std::numeric_limits<int>::max() : left.sourceOrdinal;
		const int rightOrdinal = right.sourceOrdinal < 0 ? std::numeric_limits<int>::max() : right.sourceOrdinal;
		return leftOrdinal < rightOrdinal;
	});
}

void addConflict(QVector<PackageStageConflict>* conflicts, const QString& operationId, const QString& virtualPath, const QString& message, bool blocking = true)
{
	if (!conflicts || message.trimmed().isEmpty()) {
		return;
	}
	conflicts->push_back({operationId.trimmed(), virtualPath.trimmed(), message.trimmed(), blocking});
}

bool readSourceFile(const QString& sourceFilePath, QByteArray* bytes, QString* error)
{
	if (error) {
		error->clear();
	}
	if (bytes) {
		bytes->clear();
	}
	// Opened directly: one syscall instead of a stat plus an open, which matters
	// when a plan holds tens of thousands of staged files.
	QFile file(sourceFilePath);
	if (!file.open(QIODevice::ReadOnly)) {
		const QFileInfo info(sourceFilePath);
		if (error) {
			*error = info.exists() ? stageText("Unable to read source file.") : stageText("Source file does not exist.");
		}
		return false;
	}
	if (bytes) {
		*bytes = file.readAll();
	}
	return true;
}

QByteArray sha256Bytes(const QByteArray& bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QString topLevelLocation(const QString& virtualPath)
{
	const QString parent = packageVirtualPathParent(virtualPath);
	if (parent.isEmpty()) {
		return QStringLiteral("/");
	}
	const QString top = parent.section('/', 0, 0);
	return top.isEmpty() ? QStringLiteral("/") : top;
}

QString typeBucket(const QString& virtualPath)
{
	const QString suffix = QFileInfo(virtualPath).suffix().toLower();
	if (suffix.isEmpty()) {
		return QStringLiteral("binary");
	}
	if (QStringList {QStringLiteral("txt"), QStringLiteral("cfg"), QStringLiteral("shader"), QStringLiteral("map"), QStringLiteral("def"), QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("log")}.contains(suffix)) {
		return QStringLiteral("text");
	}
	if (QStringList {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("tga"), QStringLiteral("pcx"), QStringLiteral("wal"), QStringLiteral("mip"), QStringLiteral("lmp")}.contains(suffix)) {
		return QStringLiteral("image");
	}
	if (QStringList {QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("mp3")}.contains(suffix)) {
		return QStringLiteral("audio");
	}
	if (QStringList {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm")}.contains(suffix)) {
		return QStringLiteral("model");
	}
	return suffix;
}

QVector<PackageCompositionBucket> compositionBuckets(const QVector<PackageStagedEntry>& entries)
{
	QVector<PackageCompositionBucket> buckets;
	QHash<QString, int> index;
	for (const PackageStagedEntry& entry : entries) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		const QString id = QStringLiteral("%1:%2").arg(topLevelLocation(entry.virtualPath), typeBucket(entry.virtualPath));
		int slot = index.value(id, -1);
		if (slot < 0) {
			PackageCompositionBucket bucket;
			bucket.id = id;
			bucket.label = QStringLiteral("%1 / %2").arg(topLevelLocation(entry.virtualPath), typeBucket(entry.virtualPath));
			buckets.push_back(bucket);
			slot = static_cast<int>(buckets.size()) - 1;
			index.insert(id, slot);
		}
		++buckets[slot].fileCount;
		buckets[slot].sizeBytes += entry.sizeBytes;
	}
	std::stable_sort(buckets.begin(), buckets.end(), [](const PackageCompositionBucket& left, const PackageCompositionBucket& right) {
		if (left.sizeBytes != right.sizeBytes) {
			return left.sizeBytes > right.sizeBytes;
		}
		return left.label.compare(right.label, Qt::CaseInsensitive) < 0;
	});
	return buckets;
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
	for (int shift = 0; shift < 64; shift += 8) {
		data->append(static_cast<char>((value >> shift) & 0xff));
	}
}

// MS-DOS date/time packing, APPNOTE.TXT section 4.4.6.
quint16 dosDate(const QDate& date)
{
	if (!date.isValid() || date.year() < 1980 || date.year() > 2107) {
		return static_cast<quint16>((1 << 5) | 1);
	}
	return static_cast<quint16>(((date.year() - 1980) << 9) | (date.month() << 5) | date.day());
}

quint16 dosTime(const QTime& time)
{
	if (!time.isValid()) {
		return 0;
	}
	return static_cast<quint16>((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
}

quint16 fixedDosTime()
{
	return 0;
}

quint16 fixedDosDate()
{
	return static_cast<quint16>((1 << 5) | 1);
}

void entryDosStamp(const PackageStagedEntry& entry, PackageTimestampMode mode, quint16* date, quint16* time)
{
	if (mode == PackageTimestampMode::PreserveSource && entry.modifiedUtc.isValid()) {
		const QDateTime stamp = entry.modifiedUtc.toUTC();
		if (date) {
			*date = dosDate(stamp.date());
		}
		if (time) {
			*time = dosTime(stamp.time());
		}
		return;
	}
	if (date) {
		*date = fixedDosDate();
	}
	if (time) {
		*time = fixedDosTime();
	}
}

// Only the PAK and WAD writers use this bound, and both formats store their
// directory offsets and record sizes as *signed* int32 (id Software's PAK
// header is `int dirofs; int dirlen;`, and the Doom/Quake WAD header and
// directory records are int32 as well), so the usable ceiling is INT32_MAX,
// not UINT32_MAX. This repo's own readers agree: package_archive.cpp parses
// both directories with readSignedLe32 and rejects negative offsets.
// Sources: the Quake 1 PAK layout in the Quake Standards Group "Quake
// Documentation Version 3.4" and the Unofficial Doom Specs v1.666
// (https://www.gamers.org/dhs/helpdocs/dmsp1666.html).
// ZIP is deliberately not routed through here: its 32-bit fields are genuinely
// unsigned and writeZipStream has its own ZIP64 escape path.
bool validateArchiveSize(qint64 value, QString* error, const QString& label)
{
	if (value < 0 || value > std::numeric_limits<qint32>::max()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "%1 exceeds the signed 32-bit limit of this format.").arg(label);
		}
		return false;
	}
	return true;
}

// Streaming output sink: counts bytes and hashes everything that is written.
// A null device turns the sink into a dry-run/verification sink that produces
// the same size and digest without touching the filesystem.
class ByteSink final {
public:
	explicit ByteSink(QIODevice* device)
		: m_device(device)
	{
	}

	bool append(const char* data, qint64 size)
	{
		if (size <= 0) {
			return true;
		}
		m_hash.addData(QByteArrayView(data, size));
		m_size += size;
		if (!m_device) {
			return true;
		}
		m_buffer.append(data, size);
		return m_buffer.size() < kFlushThreshold || flush();
	}

	bool append(const QByteArray& data)
	{
		return append(data.constData(), data.size());
	}

	bool flush()
	{
		if (!m_device || m_buffer.isEmpty()) {
			return true;
		}
		if (m_device->write(m_buffer) != m_buffer.size()) {
			m_failed = true;
			return false;
		}
		m_buffer.clear();
		return true;
	}

	[[nodiscard]] qint64 size() const
	{
		return m_size;
	}

	[[nodiscard]] bool failed() const
	{
		return m_failed;
	}

	[[nodiscard]] QString digest()
	{
		return QString::fromLatin1(m_hash.result().toHex());
	}

private:
	static constexpr qsizetype kFlushThreshold = 1 << 20;

	QIODevice* m_device = nullptr;
	QByteArray m_buffer;
	QCryptographicHash m_hash {QCryptographicHash::Sha256};
	qint64 m_size = 0;
	bool m_failed = false;
};

using EntryBytesProvider = std::function<bool(const PackageStagedEntry&, QByteArray*, QString*)>;

struct PackageWriteOptions {
	DeflateLevel level = DeflateLevel::Default;
	PackageTimestampMode timestampMode = PackageTimestampMode::Reproducible;
	QString wadMagic;
};

struct PackageWriteStats {
	int fileCount = 0;
	int directoryCount = 0;
	int deflatedCount = 0;
	int storedCount = 0;
	quint64 uncompressedBytes = 0;
	quint64 payloadBytes = 0;
};

QVector<PackageStagedEntry> fileEntriesOnly(const QVector<PackageStagedEntry>& entries)
{
	QVector<PackageStagedEntry> files;
	files.reserve(entries.size());
	for (const PackageStagedEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File) {
			files.push_back(entry);
		}
	}
	return files;
}

bool sinkError(QString* error)
{
	if (error && error->isEmpty()) {
		*error = stageText("Unable to write package bytes.");
	}
	return false;
}

bool writePakStream(const QVector<PackageStagedEntry>& inputEntries, const EntryBytesProvider& provider, ByteSink* sink, PackageWriteStats* stats, QString* error)
{
	QVector<PackageStagedEntry> entries = fileEntriesOnly(inputEntries);
	sortStagedEntries(&entries);

	struct Record {
		QByteArray name;
		quint32 offset = 0;
		quint32 size = 0;
	};

	// The PAK header stores the directory offset up front, so the payload sizes
	// are precomputed from the plan instead of seeking back after streaming.
	qint64 payloadTotal = 0;
	for (const PackageStagedEntry& entry : entries) {
		const QByteArray name = entry.virtualPath.toLatin1();
		if (name.isEmpty() || name.size() > kPakNameLimit || QString::fromLatin1(name) != entry.virtualPath) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageStaging", "PAK entry path must be Latin-1 and at most 56 bytes: %1").arg(entry.virtualPath);
			}
			return false;
		}
		if (!validateArchiveSize(static_cast<qint64>(entry.sizeBytes), error, stageText("PAK entry size"))) {
			return false;
		}
		payloadTotal += static_cast<qint64>(entry.sizeBytes);
	}
	if (!validateArchiveSize(kPakHeaderSize + payloadTotal, error, stageText("PAK directory offset"))
		|| !validateArchiveSize(static_cast<qint64>(entries.size()) * kPakRecordSize, error, stageText("PAK directory"))) {
		return false;
	}

	const quint32 directoryOffset = static_cast<quint32>(kPakHeaderSize + payloadTotal);
	const quint32 directoryLength = static_cast<quint32>(entries.size() * kPakRecordSize);
	QByteArray header;
	header.append("PACK");
	appendLe32(&header, directoryOffset);
	appendLe32(&header, directoryLength);
	if (!sink->append(header)) {
		return sinkError(error);
	}

	QVector<Record> records;
	records.reserve(entries.size());
	for (const PackageStagedEntry& entry : entries) {
		QByteArray bytes;
		QString readError;
		if (!provider(entry, &bytes, &readError)) {
			if (error) {
				*error = readError.isEmpty() ? stageText("Unable to read staged entry bytes.") : readError;
			}
			return false;
		}
		if (static_cast<quint64>(bytes.size()) != entry.sizeBytes) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageStaging", "Entry changed size while writing: %1").arg(entry.virtualPath);
			}
			return false;
		}
		records.push_back({entry.virtualPath.toLatin1(), static_cast<quint32>(sink->size()), static_cast<quint32>(bytes.size())});
		if (!sink->append(bytes)) {
			return sinkError(error);
		}
		if (stats) {
			++stats->fileCount;
			++stats->storedCount;
			stats->uncompressedBytes += static_cast<quint64>(bytes.size());
			stats->payloadBytes += static_cast<quint64>(bytes.size());
		}
	}

	QByteArray directory;
	directory.reserve(static_cast<qsizetype>(directoryLength));
	for (const Record& record : records) {
		const qsizetype start = directory.size();
		directory.append(record.name);
		while (directory.size() - start < kPakNameLimit) {
			directory.append('\0');
		}
		appendLe32(&directory, record.offset);
		appendLe32(&directory, record.size);
	}
	if (!sink->append(directory)) {
		return sinkError(error);
	}
	return true;
}

struct ZipCompressedEntry {
	QByteArray payload;
	quint16 method = 0;
	quint32 crc = 0;
	quint64 uncompressedSize = 0;
};

bool writeZipStream(const QVector<PackageStagedEntry>& inputEntries, const EntryBytesProvider& provider, const PackageWriteOptions& options, ByteSink* sink, PackageWriteStats* stats, QString* error)
{
	QVector<PackageStagedEntry> entries = inputEntries;
	sortStagedEntries(&entries);

	QByteArray central;
	quint64 centralCount = 0;
	for (const PackageStagedEntry& entry : entries) {
		const bool directoryEntry = entry.kind == PackageEntryKind::Directory;
		QString name = entry.virtualPath;
		while (name.endsWith('/')) {
			name.chop(1);
		}
		if (directoryEntry) {
			name += '/';
		}
		const QByteArray nameBytes = name.toUtf8();
		if (nameBytes.isEmpty() || nameBytes.size() > std::numeric_limits<quint16>::max()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageStaging", "ZIP entry path is empty or too long: %1").arg(entry.virtualPath);
			}
			return false;
		}

		ZipCompressedEntry compressed;
		if (!directoryEntry) {
			QByteArray bytes;
			QString readError;
			if (!provider(entry, &bytes, &readError)) {
				if (error) {
					*error = readError.isEmpty() ? stageText("Unable to read staged entry bytes.") : readError;
				}
				return false;
			}
			compressed.uncompressedSize = static_cast<quint64>(bytes.size());
			compressed.crc = crc32Bytes(bytes);
			compressed.payload = bytes;
			if (options.level != DeflateLevel::Store && !bytes.isEmpty()) {
				const QByteArray deflated = deflateRaw(bytes, options.level);
				// Stored is the fallback whenever deflate would not shrink the
				// entry, which keeps already-compressed content byte-for-byte.
				if (!deflated.isEmpty() && deflated.size() < bytes.size()) {
					compressed.payload = deflated;
					compressed.method = 8;
				}
			}
		}

		const quint64 localOffset = static_cast<quint64>(sink->size());
		const quint64 payloadSize = static_cast<quint64>(compressed.payload.size());
		// APPNOTE.TXT 4.4.1.4 / 4.5.3: 0xffffffff in a 32-bit size or offset
		// field is the marker that says "the real value lives in the ZIP64
		// extended information extra field", so a value *equal to* the sentinel
		// is indistinguishable from the marker and needs the ZIP64 record too.
		// The comparisons are >= for that reason, not >.
		const bool zip64Sizes = payloadSize >= kZip32Sentinel || compressed.uncompressedSize >= kZip32Sentinel;
		const bool zip64Offset = localOffset >= kZip32Sentinel;
		// APPNOTE.TXT 4.4.3.2: 2.0 is the minimum version for deflate and for
		// folder records, 1.0 covers stored files, 4.5 covers ZIP64 extras.
		quint16 versionNeeded = (compressed.method == 8 || directoryEntry) ? 20 : 10;
		if (zip64Sizes || zip64Offset) {
			versionNeeded = 45;
		}
		quint16 stampDate = 0;
		quint16 stampTime = 0;
		entryDosStamp(entry, options.timestampMode, &stampDate, &stampTime);

		QByteArray localExtra;
		if (zip64Sizes) {
			appendLe16(&localExtra, kZip64ExtraFieldId);
			appendLe16(&localExtra, 16);
			appendLe64(&localExtra, compressed.uncompressedSize);
			appendLe64(&localExtra, payloadSize);
		}

		QByteArray local;
		appendLe32(&local, kZipLocalFileSignature);
		appendLe16(&local, versionNeeded);
		appendLe16(&local, 0x0800);
		appendLe16(&local, compressed.method);
		appendLe16(&local, stampTime);
		appendLe16(&local, stampDate);
		appendLe32(&local, compressed.crc);
		appendLe32(&local, zip64Sizes ? kZip32Sentinel : static_cast<quint32>(payloadSize));
		appendLe32(&local, zip64Sizes ? kZip32Sentinel : static_cast<quint32>(compressed.uncompressedSize));
		appendLe16(&local, static_cast<quint16>(nameBytes.size()));
		appendLe16(&local, static_cast<quint16>(localExtra.size()));
		local.append(nameBytes);
		local.append(localExtra);
		if (!sink->append(local) || !sink->append(compressed.payload)) {
			return sinkError(error);
		}

		QByteArray centralExtra;
		if (zip64Sizes || zip64Offset) {
			QByteArray payloadFields;
			if (zip64Sizes) {
				appendLe64(&payloadFields, compressed.uncompressedSize);
				appendLe64(&payloadFields, payloadSize);
			}
			if (zip64Offset) {
				appendLe64(&payloadFields, localOffset);
			}
			appendLe16(&centralExtra, kZip64ExtraFieldId);
			appendLe16(&centralExtra, static_cast<quint16>(payloadFields.size()));
			centralExtra.append(payloadFields);
		}

		appendLe32(&central, kZipCentralDirectorySignature);
		appendLe16(&central, 20);
		appendLe16(&central, versionNeeded);
		appendLe16(&central, 0x0800);
		appendLe16(&central, compressed.method);
		appendLe16(&central, stampTime);
		appendLe16(&central, stampDate);
		appendLe32(&central, compressed.crc);
		appendLe32(&central, zip64Sizes ? kZip32Sentinel : static_cast<quint32>(payloadSize));
		appendLe32(&central, zip64Sizes ? kZip32Sentinel : static_cast<quint32>(compressed.uncompressedSize));
		appendLe16(&central, static_cast<quint16>(nameBytes.size()));
		appendLe16(&central, static_cast<quint16>(centralExtra.size()));
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		// External attributes: MS-DOS directory bit for directory records.
		appendLe32(&central, directoryEntry ? 0x10u : 0u);
		appendLe32(&central, zip64Offset ? kZip32Sentinel : static_cast<quint32>(localOffset));
		central.append(nameBytes);
		central.append(centralExtra);
		++centralCount;

		if (stats) {
			if (directoryEntry) {
				++stats->directoryCount;
			} else {
				++stats->fileCount;
				if (compressed.method == 8) {
					++stats->deflatedCount;
				} else {
					++stats->storedCount;
				}
				stats->uncompressedBytes += compressed.uncompressedSize;
				stats->payloadBytes += payloadSize;
			}
		}
	}

	const quint64 centralOffset = static_cast<quint64>(sink->size());
	const quint64 centralSize = static_cast<quint64>(central.size());
	if (!sink->append(central)) {
		return sinkError(error);
	}

	// APPNOTE.TXT 4.4.21 / 4.4.1.4: an end of central directory field holding
	// 0xffff or 0xffffffff means "look in the ZIP64 end of central directory
	// record", so the record and its locator are required as soon as a value
	// reaches a sentinel, not only once it exceeds one.
	const bool needsZip64 = centralCount >= kZip16Sentinel || centralSize >= kZip32Sentinel || centralOffset >= kZip32Sentinel;
	QByteArray tail;
	if (needsZip64) {
		const quint64 zip64Offset = static_cast<quint64>(sink->size());
		appendLe32(&tail, kZip64EndOfCentralDirectorySignature);
		appendLe64(&tail, 44);
		appendLe16(&tail, 45);
		appendLe16(&tail, 45);
		appendLe32(&tail, 0);
		appendLe32(&tail, 0);
		appendLe64(&tail, centralCount);
		appendLe64(&tail, centralCount);
		appendLe64(&tail, centralSize);
		appendLe64(&tail, centralOffset);
		appendLe32(&tail, kZip64EndOfCentralDirectoryLocatorSignature);
		appendLe32(&tail, 0);
		appendLe64(&tail, zip64Offset);
		appendLe32(&tail, 1);
	}
	appendLe32(&tail, kZipEndOfCentralDirectorySignature);
	appendLe16(&tail, 0);
	appendLe16(&tail, 0);
	// Each field falls back to its own sentinel independently: a ZIP64 record
	// forced by the entry count alone must still carry the true 32-bit central
	// directory size and offset here.
	appendLe16(&tail, centralCount >= kZip16Sentinel ? kZip16Sentinel : static_cast<quint16>(centralCount));
	appendLe16(&tail, centralCount >= kZip16Sentinel ? kZip16Sentinel : static_cast<quint16>(centralCount));
	appendLe32(&tail, centralSize >= kZip32Sentinel ? kZip32Sentinel : static_cast<quint32>(centralSize));
	appendLe32(&tail, centralOffset >= kZip32Sentinel ? kZip32Sentinel : static_cast<quint32>(centralOffset));
	appendLe16(&tail, 0);
	if (!sink->append(tail)) {
		return sinkError(error);
	}
	return true;
}

bool wadNameIsValid(const QString& name, int limit)
{
	if (name.isEmpty() || name.size() > limit || name.contains('/')) {
		return false;
	}
	const QByteArray latin1 = name.toLatin1();
	return !latin1.contains('\0') && QString::fromLatin1(latin1) == name;
}

bool isDoomMapMarker(const QString& name)
{
	if (name.size() == 4 && name[0] == QLatin1Char('E') && name[2] == QLatin1Char('M') && name[1].isDigit() && name[3].isDigit()) {
		return true;
	}
	return name.size() == 5 && name.startsWith(QStringLiteral("MAP")) && name[3].isDigit() && name[4].isDigit();
}

int doomMapLumpRank(const QString& name)
{
	if (isDoomMapMarker(name)) {
		return 0;
	}
	const QStringList order = {
		QStringLiteral("THINGS"),
		QStringLiteral("LINEDEFS"),
		QStringLiteral("SIDEDEFS"),
		QStringLiteral("VERTEXES"),
		QStringLiteral("SEGS"),
		QStringLiteral("SSECTORS"),
		QStringLiteral("NODES"),
		QStringLiteral("SECTORS"),
		QStringLiteral("REJECT"),
		QStringLiteral("BLOCKMAP"),
		QStringLiteral("BEHAVIOR"),
		QStringLiteral("SCRIPTS"),
	};
	const int index = order.indexOf(name);
	return index >= 0 ? index + 1 : 1000;
}

// Single-map fallback ordering: rank every lump globally so that a plan which
// arrived in path order (a folder converted to a WAD, say) still comes out as
// marker first, then the canonical map-lump run, then everything else.
QVector<PackageStagedEntry> wadRankOrderedEntries(QVector<PackageStagedEntry> entries)
{
	std::stable_sort(entries.begin(), entries.end(), [](const PackageStagedEntry& left, const PackageStagedEntry& right) {
		const QString leftName = left.virtualPath.toUpper();
		const QString rightName = right.virtualPath.toUpper();
		const int leftRank = doomMapLumpRank(leftName);
		const int rightRank = doomMapLumpRank(rightName);
		if (leftRank != rightRank) {
			return leftRank < rightRank;
		}
		if (leftRank >= 1000) {
			return stagedEntryLess(left, right);
		}
		return false;
	});
	return entries;
}

// Groups each map's lumps behind its own marker.
//
// A Doom engine locates a map's data as the run of lumps that immediately
// follows the map's marker lump, which is why every map in a WAD repeats
// THINGS, LINEDEFS, SIDEDEFS, VERTEXES, SEGS, SSECTORS, NODES, SECTORS, REJECT
// and BLOCKMAP (https://doomwiki.org/wiki/WAD). The input must therefore
// already be in source order: this walks it once, opens a group at every
// marker, attaches the known map lumps that follow to that group, and lets any
// other lump close the run and pass through where it stands. Each group is then
// emitted as marker + rank-ordered lumps, so a malformed run is repaired
// without ever moving a lump between maps.
//
// Returns false when the input cannot be grouped, which means it is not in
// source order: a map lump appears before any marker, or one map claims the
// same lump name twice. The caller falls back to the global rank order when
// there is at most one map, and reports the ambiguity otherwise.
bool wadGroupedEntries(const QVector<PackageStagedEntry>& entries, QVector<PackageStagedEntry>* out, QString* error)
{
	struct Run {
		bool isMap = false;
		QVector<PackageStagedEntry> entries;
	};

	QVector<Run> runs;
	int currentMap = -1;
	QSet<QString> currentMapLumps;
	for (const PackageStagedEntry& entry : entries) {
		const QString name = entry.virtualPath.toUpper();
		if (isDoomMapMarker(name)) {
			Run run;
			run.isMap = true;
			run.entries.push_back(entry);
			runs.push_back(run);
			currentMap = static_cast<int>(runs.size()) - 1;
			currentMapLumps.clear();
			continue;
		}
		const int rank = doomMapLumpRank(name);
		if (rank < 1000) {
			if (currentMap < 0) {
				if (error) {
					*error = QCoreApplication::translate("VibeStudioPackageStaging", "Cannot tell which map owns the lump %1: the plan is not in WAD source order.").arg(name);
				}
				return false;
			}
			if (currentMapLumps.contains(name)) {
				if (error) {
					*error = QCoreApplication::translate("VibeStudioPackageStaging", "One map cannot hold the lump %1 twice.").arg(name);
				}
				return false;
			}
			currentMapLumps.insert(name);
			runs[currentMap].entries.push_back(entry);
			continue;
		}
		// Any other lump ends the map run it follows.
		currentMap = -1;
		currentMapLumps.clear();
		Run run;
		run.entries.push_back(entry);
		runs.push_back(run);
	}

	QVector<PackageStagedEntry> ordered;
	ordered.reserve(entries.size());
	for (const Run& run : runs) {
		if (!run.isMap || run.entries.size() < 2) {
			ordered += run.entries;
			continue;
		}
		QVector<PackageStagedEntry> lumps = run.entries.mid(1);
		std::stable_sort(lumps.begin(), lumps.end(), [](const PackageStagedEntry& left, const PackageStagedEntry& right) {
			return doomMapLumpRank(left.virtualPath.toUpper()) < doomMapLumpRank(right.virtualPath.toUpper());
		});
		ordered.push_back(run.entries.first());
		ordered += lumps;
	}
	if (out) {
		*out = ordered;
	}
	return true;
}

bool wadMagicIsSupported(const QString& magic)
{
	return magic == QStringLiteral("IWAD") || magic == QStringLiteral("PWAD") || magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
}

bool writeWadStream(const QVector<PackageStagedEntry>& inputEntries, const EntryBytesProvider& provider, const PackageWriteOptions& options, ByteSink* sink, PackageWriteStats* stats, QString* error)
{
	const QString magic = options.wadMagic;
	if (!wadMagicIsSupported(magic)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Unsupported WAD magic: %1").arg(magic);
		}
		return false;
	}
	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const int recordSize = textureWad ? kTextureWadRecordSize : kDoomWadRecordSize;
	const int nameLimit = textureWad ? kTextureWadNameLimit : kDoomWadNameLimit;

	QVector<PackageStagedEntry> entries = fileEntriesOnly(inputEntries);
	if (textureWad) {
		// A WAD2/WAD3 texture WAD has no ordering contract at all, so the
		// plan's own order is written as-is: source order for a WAD source,
		// the stable path order for anything else.
	} else {
		// Doom map data is positional: the lumps of a map are the run that
		// follows its marker, so several maps in one WAD legitimately repeat
		// THINGS, LINEDEFS, ... (https://doomwiki.org/wiki/WAD). Grouping is
		// tried first and is the only ordering that can express that; it needs
		// the plan to be in WAD source order, which a WAD-sourced plan is.
		int markerCount = 0;
		for (const PackageStagedEntry& entry : entries) {
			if (isDoomMapMarker(entry.virtualPath.toUpper())) {
				++markerCount;
			}
		}
		QVector<PackageStagedEntry> grouped;
		QString groupError;
		if (wadGroupedEntries(entries, &grouped, &groupError)) {
			entries = grouped;
		} else if (markerCount > 1) {
			// Two or more maps and no usable order: which map owns which lump
			// is genuinely unknowable, and guessing would emit an interleaved
			// WAD that no engine can read.
			if (error) {
				*error = groupError.isEmpty() ? stageText("Cannot order a multi-map Doom WAD from this plan.") : groupError;
			}
			return false;
		} else {
			// At most one map: fall back to the global rank order, which is
			// what a plan assembled from a folder or a path-sorted source
			// needs.
			QSet<QString> seenLumpNames;
			for (const PackageStagedEntry& entry : entries) {
				const QString name = entry.virtualPath.toUpper();
				if (seenLumpNames.contains(name)) {
					if (error) {
						*error = QCoreApplication::translate("VibeStudioPackageStaging", "Doom WAD write-back cannot represent duplicate lump names: %1").arg(name);
					}
					return false;
				}
				seenLumpNames.insert(name);
			}
			entries = wadRankOrderedEntries(entries);
		}
	}
	if (entries.size() > std::numeric_limits<qint32>::max()) {
		if (error) {
			*error = stageText("WAD entry count exceeds the 32-bit directory limit.");
		}
		return false;
	}

	struct Record {
		QByteArray name;
		quint32 offset = 0;
		quint32 size = 0;
		quint8 type = 0;
	};

	qint64 payloadTotal = 0;
	for (const PackageStagedEntry& entry : entries) {
		// Doom lump names are conventionally upper case; texture WAD names keep
		// the source case so Quake/Half-Life texture references still resolve.
		const QString name = textureWad ? entry.virtualPath : entry.virtualPath.toUpper();
		if (!wadNameIsValid(name, nameLimit)) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageStaging", "WAD lump names must be Latin-1, contain no folders, and be at most %1 characters: %2").arg(nameLimit).arg(entry.virtualPath);
			}
			return false;
		}
		if (!validateArchiveSize(static_cast<qint64>(entry.sizeBytes), error, stageText("WAD lump size"))) {
			return false;
		}
		payloadTotal += static_cast<qint64>(entry.sizeBytes);
	}
	if (!validateArchiveSize(kWadHeaderSize + payloadTotal, error, stageText("WAD directory offset"))) {
		return false;
	}

	QByteArray header;
	header.append(magic.toLatin1());
	appendLe32(&header, static_cast<quint32>(entries.size()));
	appendLe32(&header, static_cast<quint32>(kWadHeaderSize + payloadTotal));
	if (!sink->append(header)) {
		return sinkError(error);
	}

	QVector<Record> records;
	records.reserve(entries.size());
	for (const PackageStagedEntry& entry : entries) {
		QByteArray bytes;
		QString readError;
		if (!provider(entry, &bytes, &readError)) {
			if (error) {
				*error = readError.isEmpty() ? stageText("Unable to read staged entry bytes.") : readError;
			}
			return false;
		}
		if (static_cast<quint64>(bytes.size()) != entry.sizeBytes) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageStaging", "Entry changed size while writing: %1").arg(entry.virtualPath);
			}
			return false;
		}
		const QString name = textureWad ? entry.virtualPath : entry.virtualPath.toUpper();
		quint8 type = entry.wadLumpType;
		if (textureWad && type == 0) {
			type = magic == QStringLiteral("WAD3") ? kWad3MiptexType : kWad2MiptexType;
		}
		records.push_back({name.toLatin1(), static_cast<quint32>(sink->size()), static_cast<quint32>(bytes.size()), type});
		if (!sink->append(bytes)) {
			return sinkError(error);
		}
		if (stats) {
			++stats->fileCount;
			++stats->storedCount;
			stats->uncompressedBytes += static_cast<quint64>(bytes.size());
			stats->payloadBytes += static_cast<quint64>(bytes.size());
		}
	}

	QByteArray directory;
	directory.reserve(static_cast<qsizetype>(records.size()) * recordSize);
	for (const Record& record : records) {
		appendLe32(&directory, record.offset);
		appendLe32(&directory, record.size);
		if (textureWad) {
			appendLe32(&directory, record.size);
			directory.append(static_cast<char>(record.type));
			directory.append('\0');
			appendLe16(&directory, 0);
		}
		const qsizetype start = directory.size();
		directory.append(record.name);
		while (directory.size() - start < nameLimit) {
			directory.append('\0');
		}
	}
	if (!sink->append(directory)) {
		return sinkError(error);
	}
	return true;
}

QJsonObject operationJson(const PackageStageOperation& operation)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), operation.id);
	object.insert(QStringLiteral("type"), packageStageOperationTypeId(operation.type));
	object.insert(QStringLiteral("virtualPath"), operation.virtualPath);
	object.insert(QStringLiteral("targetVirtualPath"), operation.targetVirtualPath);
	object.insert(QStringLiteral("sourceFilePath"), operation.sourceFilePath);
	object.insert(QStringLiteral("conflictResolution"), packageStageConflictResolutionId(operation.conflictResolution));
	return object;
}

QJsonArray operationsJson(const QVector<PackageStageOperation>& operations)
{
	QJsonArray array;
	for (const PackageStageOperation& operation : operations) {
		array.append(operationJson(operation));
	}
	return array;
}

QJsonArray conflictsJson(const QVector<PackageStageConflict>& conflicts)
{
	QJsonArray array;
	for (const PackageStageConflict& conflict : conflicts) {
		QJsonObject object;
		object.insert(QStringLiteral("operationId"), conflict.operationId);
		object.insert(QStringLiteral("virtualPath"), conflict.virtualPath);
		object.insert(QStringLiteral("message"), conflict.message);
		object.insert(QStringLiteral("blocking"), conflict.blocking);
		array.append(object);
	}
	return array;
}

QJsonArray bucketsJson(const QVector<PackageCompositionBucket>& buckets)
{
	QJsonArray array;
	for (const PackageCompositionBucket& bucket : buckets) {
		QJsonObject object;
		object.insert(QStringLiteral("id"), bucket.id);
		object.insert(QStringLiteral("label"), bucket.label);
		object.insert(QStringLiteral("fileCount"), bucket.fileCount);
		object.insert(QStringLiteral("sizeBytes"), static_cast<double>(bucket.sizeBytes));
		array.append(object);
	}
	return array;
}

QString entryContentKey(const PackageStagedEntry& entry)
{
	if (!entry.sourceFilePath.isEmpty()) {
		return QStringLiteral("file:%1").arg(entry.sourceFilePath);
	}
	if (!entry.baseVirtualPath.isEmpty()) {
		// The ordinal is part of the key: a Doom WAD holds several distinct
		// lumps that share one name, and collapsing them here would hand every
		// map the first map's digest.
		return QStringLiteral("base:%1#%2").arg(entry.baseVirtualPath).arg(entry.sourceOrdinal);
	}
	return QStringLiteral("empty:%1").arg(entry.virtualPath);
}

struct PlanSlot {
	PackageStagedEntry entry;
	bool alive = true;
};

QVector<PackageStagedEntry> computePlan(const QVector<PackageStagedEntry>& baseEntries, const QVector<PackageStagedEntry>& baseDirectories, const QVector<PackageStageOperation>& operations, const QVector<PackageStageConflict>& baseConflicts, bool preserveSourceOrder, QVector<PackageStageConflict>* conflicts)
{
	if (conflicts) {
		*conflicts = baseConflicts;
	}

	QVector<PlanSlot> planSlots;
	planSlots.reserve(baseEntries.size() + operations.size());
	QHash<QString, int> index;
	index.reserve(static_cast<int>(baseEntries.size() + operations.size()));
	for (const PackageStagedEntry& entry : baseEntries) {
		planSlots.push_back({entry, true});
		index.insert(entryKey(entry.virtualPath), static_cast<int>(planSlots.size()) - 1);
	}

	const auto liveIndex = [&planSlots, &index](const QString& virtualPath) {
		const auto found = index.constFind(entryKey(virtualPath));
		if (found == index.constEnd()) {
			return -1;
		}
		const int slot = found.value();
		return planSlots[slot].alive ? slot : -1;
	};

	for (const PackageStageOperation& operation : operations) {
		const PackageVirtualPath normalized = normalizePackageVirtualPath(operation.virtualPath, false);
		if (!normalized.isSafe()) {
			addConflict(conflicts, operation.id, operation.virtualPath, QCoreApplication::translate("VibeStudioPackageStaging", "Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			continue;
		}

		const int existingIndex = liveIndex(normalized.normalizedPath);
		if (operation.type == PackageStageOperationType::Add || operation.type == PackageStageOperationType::Replace) {
			const QFileInfo sourceInfo(operation.sourceFilePath);
			if (!sourceInfo.exists() || !sourceInfo.isFile()) {
				addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Source file does not exist."));
				continue;
			}
			// Deliberately no isReadable() probe here: on Windows it costs an
			// ACL query per file, and the plan is recomputed far more often
			// than it is written. A permission failure surfaces as a write
			// error instead.
			PackageStagedEntry staged;
			staged.virtualPath = normalized.normalizedPath;
			staged.kind = PackageEntryKind::File;
			staged.sizeBytes = static_cast<quint64>(std::max<qint64>(0, sourceInfo.size()));
			staged.modifiedUtc = sourceInfo.lastModified().toUTC();
			staged.operationId = operation.id;
			staged.sourceFilePath = sourceInfo.absoluteFilePath();

			if (operation.type == PackageStageOperationType::Add) {
				if (existingIndex >= 0) {
					if (operation.conflictResolution == PackageStageConflictResolution::ReplaceExisting) {
						staged.source = QStringLiteral("staged-add-replace");
						// Replacing content must not move the entry: in a WAD
						// its position is what binds it to a map.
						staged.sourceOrdinal = planSlots[existingIndex].entry.sourceOrdinal;
						staged.wadLumpType = planSlots[existingIndex].entry.wadLumpType;
						planSlots[existingIndex].entry = staged;
					} else if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
						addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Skipped add because an entry already exists."), false);
					} else {
						addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Cannot add because an entry already exists. Use replace or replace-existing conflict resolution."));
					}
					continue;
				}
				staged.source = QStringLiteral("staged-add");
				planSlots.push_back({staged, true});
				index.insert(entryKey(staged.virtualPath), static_cast<int>(planSlots.size()) - 1);
				continue;
			}

			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Skipped replace because the entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Cannot replace because the entry is missing."));
				}
				continue;
			}
			staged.source = QStringLiteral("staged-replace");
			staged.sourceOrdinal = planSlots[existingIndex].entry.sourceOrdinal;
			staged.wadLumpType = planSlots[existingIndex].entry.wadLumpType;
			planSlots[existingIndex].entry = staged;
			continue;
		}

		if (operation.type == PackageStageOperationType::Rename) {
			const PackageVirtualPath target = normalizePackageVirtualPath(operation.targetVirtualPath, false);
			if (!target.isSafe()) {
				addConflict(conflicts, operation.id, operation.targetVirtualPath, QCoreApplication::translate("VibeStudioPackageStaging", "Unsafe target package path: %1").arg(packagePathIssueDisplayName(target.issue)));
				continue;
			}
			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Skipped rename because the source entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Cannot rename because the source entry is missing."));
				}
				continue;
			}
			const int targetIndex = liveIndex(target.normalizedPath);
			if (targetIndex >= 0 && targetIndex != existingIndex) {
				if (operation.conflictResolution == PackageStageConflictResolution::ReplaceExisting) {
					planSlots[targetIndex].alive = false;
					index.remove(entryKey(target.normalizedPath));
				} else if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, target.normalizedPath, stageText("Skipped rename because the target entry exists."), false);
					continue;
				} else {
					addConflict(conflicts, operation.id, target.normalizedPath, stageText("Cannot rename because the target entry already exists."));
					continue;
				}
			}
			index.remove(entryKey(planSlots[existingIndex].entry.virtualPath));
			planSlots[existingIndex].entry.virtualPath = target.normalizedPath;
			planSlots[existingIndex].entry.source = QStringLiteral("staged-rename");
			planSlots[existingIndex].entry.operationId = operation.id;
			index.insert(entryKey(target.normalizedPath), existingIndex);
			continue;
		}

		if (operation.type == PackageStageOperationType::Delete) {
			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Skipped delete because the entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, stageText("Cannot delete because the entry is missing."));
				}
				continue;
			}
			planSlots[existingIndex].alive = false;
			index.remove(entryKey(planSlots[existingIndex].entry.virtualPath));
		}
	}

	QVector<PackageStagedEntry> entries;
	entries.reserve(planSlots.size() + baseDirectories.size());
	QHash<QString, bool> liveKeys;
	liveKeys.reserve(static_cast<int>(planSlots.size()));
	for (const PlanSlot& slot : planSlots) {
		if (!slot.alive) {
			continue;
		}
		entries.push_back(slot.entry);
		liveKeys.insert(entryKey(slot.entry.virtualPath), true);
	}
	for (const PackageStagedEntry& directory : baseDirectories) {
		if (liveKeys.contains(entryKey(directory.virtualPath))) {
			continue;
		}
		entries.push_back(directory);
	}
	// A WAD plan keeps the source lump order, because in a Doom WAD position
	// carries meaning that an alphabetical order would destroy; every other
	// format gets the stable total order over paths.
	if (preserveSourceOrder) {
		sortStagedEntriesBySourceOrder(&entries);
	} else {
		sortStagedEntries(&entries);
	}
	return entries;
}

quint64 totalBytes(const QVector<PackageStagedEntry>& entries)
{
	quint64 total = 0;
	for (const PackageStagedEntry& entry : entries) {
		if (entry.kind == PackageEntryKind::File) {
			total += entry.sizeBytes;
		}
	}
	return total;
}

// An unused sibling of `destinationPath`. The new archive is written here
// first so the file it is about to replace is never the file being written.
QString uniqueSiblingPath(const QString& destinationPath)
{
	const QString base = QFileInfo(destinationPath).absoluteFilePath();
	for (int attempt = 0; attempt < 1000; ++attempt) {
		const QString candidate = QStringLiteral("%1.vibestudio-%2.tmp").arg(base).arg(attempt);
		if (!QFileInfo::exists(candidate)) {
			return candidate;
		}
	}
	return {};
}

// Streams a finished file back through SHA-256 so the overwrite path can prove
// the replacement is complete and readable before the original is moved aside.
QString fileSha256(const QString& path, quint64* sizeBytes)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return {};
	}
	if (sizeBytes) {
		*sizeBytes = static_cast<quint64>(std::max<qint64>(0, file.size()));
	}
	return QString::fromLatin1(hash.result().toHex());
}

QString canonicalComparePath(const QString& path)
{
	const QFileInfo info(path);
	const QString canonical = info.canonicalFilePath();
	if (!canonical.isEmpty()) {
		return QDir::cleanPath(canonical);
	}
	const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
	const QString base = parent.isEmpty() ? info.absolutePath() : parent;
	return QDir::cleanPath(base + QLatin1Char('/') + info.fileName());
}

qint64 readSignedLe32(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 4 > data.size()) {
		return -1;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	const quint32 value = static_cast<quint32>(bytes[0]) | (static_cast<quint32>(bytes[1]) << 8)
		| (static_cast<quint32>(bytes[2]) << 16) | (static_cast<quint32>(bytes[3]) << 24);
	return static_cast<qint64>(static_cast<qint32>(value));
}

// Reads the WAD magic and the lump directory in on-disk order.
//
// The on-disk order is what makes a multi-map Doom WAD writable: lump names
// repeat once per map, so the plan has to remember where each lump sat rather
// than key it by name. The layout is the one described by the Unofficial Doom
// Specs v1.666 (https://www.gamers.org/dhs/helpdocs/dmsp1666.html) for
// IWAD/PWAD and by the Quake Standards Group "Quake Documentation Version 3.4"
// / the Half-Life SDK notes for WAD2/WAD3: a 12-byte header of magic,
// int32 lumpCount, int32 directoryOffset, then fixed-size directory records.
void readWadSourceMetadata(const QString& path, QString* magicOut, QVector<PackageWadLumpLocation>* lumps)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const QByteArray header = file.read(kWadHeaderSize);
	if (header.size() != kWadHeaderSize) {
		return;
	}
	const QString magic = QString::fromLatin1(header.constData(), 4);
	if (!wadMagicIsSupported(magic)) {
		return;
	}
	if (magicOut) {
		*magicOut = magic;
	}
	if (!lumps) {
		return;
	}

	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const int recordSize = textureWad ? kTextureWadRecordSize : kDoomWadRecordSize;
	const int nameOffset = textureWad ? 16 : 8;
	const int nameLimit = textureWad ? kTextureWadNameLimit : kDoomWadNameLimit;
	const qint64 lumpCount = readSignedLe32(header, 4);
	const qint64 directoryOffset = readSignedLe32(header, 8);
	const qint64 fileSize = file.size();
	// Every bound is checked against the real file size before a seek, so a
	// truncated or hostile directory yields an empty result instead of a read
	// loop driven by an attacker-chosen count.
	if (lumpCount < 0 || directoryOffset < kWadHeaderSize
		|| lumpCount > (fileSize - kWadHeaderSize) / recordSize
		|| directoryOffset + (lumpCount * recordSize) > fileSize) {
		return;
	}
	if (!file.seek(directoryOffset)) {
		return;
	}

	lumps->reserve(static_cast<qsizetype>(lumpCount));
	for (qint64 index = 0; index < lumpCount; ++index) {
		const QByteArray record = file.read(recordSize);
		if (record.size() != recordSize) {
			lumps->clear();
			return;
		}
		qsizetype nameLength = 0;
		while (nameLength < nameLimit && record[nameOffset + nameLength] != '\0') {
			++nameLength;
		}
		PackageWadLumpLocation lump;
		lump.name = QString::fromLatin1(record.constData() + nameOffset, nameLength).trimmed();
		if (lump.name.isEmpty()) {
			// Matches the reader's placeholder for a blank directory name so
			// the two sides can be paired up.
			lump.name = QStringLiteral("lump-%1").arg(index, 4, 10, QLatin1Char('0'));
		}
		lump.dataOffset = readSignedLe32(record, 0);
		lump.diskSizeBytes = readSignedLe32(record, 4);
		lump.sizeBytes = textureWad ? readSignedLe32(record, 8) : lump.diskSizeBytes;
		lump.type = textureWad ? static_cast<quint8>(record[12]) : 0;
		lump.compression = textureWad ? static_cast<quint8>(record[13]) : 0;
		lumps->push_back(lump);
	}
}

// Pairing key for one directory record. Name alone is ambiguous in a Doom WAD,
// so the offset and stored size join it; two records that agree on all three
// are interchangeable by construction.
QString wadLumpPairKey(const QString& name, qint64 dataOffset, qint64 diskSizeBytes)
{
	return QStringLiteral("%1|%2|%3").arg(entryKey(name)).arg(dataOffset).arg(diskSizeBytes);
}

// Reads one lump by position. Every bound is re-checked against the file that
// is open right now, so a directory that was valid at load time but points
// outside a file that has since been truncated fails instead of allocating.
bool readWadLumpBytes(const QString& wadPath, const PackageWadLumpLocation& lump, QByteArray* out, QString* error)
{
	if (lump.compression != 0) {
		if (error) {
			*error = stageText("WAD2/WAD3 compressed lumps are listed but not decoded.");
		}
		return false;
	}
	QFile file(wadPath);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = stageText("Unable to reopen the source package for reading.");
		}
		return false;
	}
	const qint64 storedSize = lump.diskSizeBytes > 0 ? lump.diskSizeBytes : lump.sizeBytes;
	if (lump.dataOffset < 0 || storedSize < 0 || storedSize > file.size() || lump.dataOffset > file.size() - storedSize) {
		if (error) {
			*error = stageText("Invalid WAD lump offset or size.");
		}
		return false;
	}
	if (!file.seek(lump.dataOffset)) {
		if (error) {
			*error = stageText("Unable to seek to the WAD lump.");
		}
		return false;
	}
	QByteArray bytes = file.read(storedSize);
	if (bytes.size() != storedSize) {
		if (error) {
			*error = stageText("Unable to read the WAD lump.");
		}
		return false;
	}
	if (out) {
		*out = bytes;
	}
	return true;
}

} // namespace

bool PackageWriteReport::succeeded() const
{
	return blockedMessages.isEmpty() && !outputPath.isEmpty() && bytesWritten > 0;
}

bool PackageStagingModel::loadBaseArchive(const PackageArchiveReader& archive, QString* error)
{
	if (error) {
		error->clear();
	}
	clear();
	if (!archive.isOpen()) {
		if (error) {
			*error = stageText("No package is open.");
		}
		return false;
	}

	m_sourcePath = archive.sourcePath();
	m_sourceFormat = archive.format();
	m_loaded = true;

	const bool wadSource = m_sourceFormat == PackageArchiveFormat::Wad;
	if (wadSource) {
		readWadSourceMetadata(m_sourcePath, &m_sourceWadMagic, &m_sourceWadLumps);
	}

	// Maps a reader entry back to its slot in the WAD directory. The reader
	// sorts its entry list by path and only keeps duplicates in relative
	// on-disk order, so the directory read above is the only source of the
	// absolute position a lump occupies.
	QHash<QString, QVector<int>> wadPairSlots;
	QHash<QString, QVector<int>> wadNameSlots;
	QHash<QString, int> wadPairCursor;
	QHash<QString, int> wadNameCursor;
	for (int index = 0; index < static_cast<int>(m_sourceWadLumps.size()); ++index) {
		const PackageWadLumpLocation& lump = m_sourceWadLumps.at(index);
		wadPairSlots[wadLumpPairKey(lump.name, lump.dataOffset, lump.diskSizeBytes)].push_back(index);
		wadNameSlots[entryKey(lump.name)].push_back(index);
	}

	int readerOrdinal = -1;
	for (const PackageEntry& entry : archive.entries()) {
		++readerOrdinal;
		if (entry.kind == PackageEntryKind::Directory) {
			// Synthetic directories are re-derived from file paths by every
			// reader, so only real directory records are carried forward.
			if (entry.storageMethod == QStringLiteral("synthetic")) {
				continue;
			}
			QString path = entry.virtualPath;
			while (path.endsWith('/')) {
				path.chop(1);
			}
			if (path.isEmpty()) {
				continue;
			}
			PackageStagedEntry directory;
			directory.virtualPath = path;
			directory.kind = PackageEntryKind::Directory;
			directory.modifiedUtc = entry.modifiedUtc;
			directory.source = QStringLiteral("base-directory");
			directory.baseVirtualPath = entry.virtualPath;
			m_baseDirectories.push_back(directory);
			continue;
		}
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		if (!entry.readable) {
			m_baseConflicts.push_back({QString(), entry.virtualPath, entry.note.isEmpty() ? stageText("Base entry is not readable and cannot be preserved by the writer.") : entry.note, true});
			continue;
		}
		PackageStagedEntry staged;
		staged.virtualPath = entry.virtualPath;
		staged.kind = PackageEntryKind::File;
		staged.sizeBytes = entry.sizeBytes;
		staged.modifiedUtc = entry.modifiedUtc;
		staged.source = QStringLiteral("base");
		staged.baseVirtualPath = entry.virtualPath;
		staged.sourceOrdinal = readerOrdinal;
		if (wadSource && !m_sourceWadLumps.isEmpty()) {
			// Prefer the exact (name, offset, stored size) triple, then fall
			// back to the next unconsumed record with the same name so a
			// directory the reader partly skipped still lines up.
			const QString pairKey = wadLumpPairKey(entry.virtualPath, entry.dataOffset, static_cast<qint64>(entry.compressedSizeBytes));
			const QString nameKey = entryKey(entry.virtualPath);
			int slot = -1;
			const QVector<int> pairCandidates = wadPairSlots.value(pairKey);
			int& pairCursor = wadPairCursor[pairKey];
			if (pairCursor < static_cast<int>(pairCandidates.size())) {
				slot = pairCandidates.at(pairCursor++);
			} else {
				const QVector<int> nameCandidates = wadNameSlots.value(nameKey);
				int& nameCursor = wadNameCursor[nameKey];
				if (nameCursor < static_cast<int>(nameCandidates.size())) {
					slot = nameCandidates.at(nameCursor++);
				}
			}
			staged.sourceOrdinal = slot;
			if (slot >= 0) {
				staged.wadLumpType = m_sourceWadLumps.at(slot).type;
			}
		}
		m_baseEntries.push_back(staged);
	}
	if (wadSource) {
		sortStagedEntriesBySourceOrder(&m_baseEntries);
	} else {
		sortStagedEntries(&m_baseEntries);
	}
	sortStagedEntries(&m_baseDirectories);
	invalidatePlan();
	return true;
}

void PackageStagingModel::clear()
{
	m_sourcePath.clear();
	m_sourceFormat = PackageArchiveFormat::Unknown;
	m_sourceWadMagic.clear();
	m_sourceWadLumps.clear();
	m_loaded = false;
	m_baseEntries.clear();
	m_baseDirectories.clear();
	m_operations.clear();
	m_baseConflicts.clear();
	m_operationSerial = 0;
	m_baseReader.reset();
	invalidatePlan();
}

bool PackageStagingModel::isLoaded() const
{
	return m_loaded;
}

QString PackageStagingModel::sourcePath() const
{
	return m_sourcePath;
}

PackageArchiveFormat PackageStagingModel::sourceFormat() const
{
	return m_sourceFormat;
}

QString PackageStagingModel::sourceWadMagic() const
{
	return m_sourceWadMagic;
}

QVector<PackageWadLumpLocation> PackageStagingModel::sourceWadLumps() const
{
	return m_sourceWadLumps;
}

QVector<PackageStageOperation> PackageStagingModel::operations() const
{
	return m_operations;
}

void PackageStagingModel::invalidatePlan()
{
	m_planValid = false;
	m_planEntries.clear();
	m_planConflicts.clear();
}

void PackageStagingModel::ensurePlan() const
{
	if (m_planValid) {
		return;
	}
	m_planEntries = computePlan(m_baseEntries, m_baseDirectories, m_operations, m_baseConflicts, m_sourceFormat == PackageArchiveFormat::Wad, &m_planConflicts);
	m_planValid = true;
}

QVector<PackageStageConflict> PackageStagingModel::conflicts() const
{
	ensurePlan();
	return m_planConflicts;
}

QVector<PackageStagedEntry> PackageStagingModel::beforeEntries() const
{
	return m_baseEntries;
}

QVector<PackageStagedEntry> PackageStagingModel::plannedEntries() const
{
	ensurePlan();
	return m_planEntries;
}

bool PackageStagingModel::entryBytes(const PackageStagedEntry& entry, QByteArray* out, QString* error) const
{
	if (error) {
		error->clear();
	}
	if (out) {
		out->clear();
	}
	if (entry.kind == PackageEntryKind::Directory) {
		return true;
	}
	if (!entry.sourceFilePath.isEmpty()) {
		return readSourceFile(entry.sourceFilePath, out, error);
	}
	if (entry.baseVirtualPath.isEmpty()) {
		return true;
	}
	// WAD lumps resolve by position, never by name: a Doom WAD repeats
	// THINGS/LINEDEFS/... once per map, so a name lookup would hand every map
	// the first map's bytes.
	if (m_sourceFormat == PackageArchiveFormat::Wad && entry.sourceOrdinal >= 0 && entry.sourceOrdinal < static_cast<int>(m_sourceWadLumps.size())) {
		return readWadLumpBytes(m_sourcePath, m_sourceWadLumps.at(entry.sourceOrdinal), out, error);
	}
	if (!m_baseReader) {
		auto reader = std::make_shared<PackageArchive>();
		QString loadError;
		if (!reader->load(m_sourcePath, &loadError)) {
			if (error) {
				*error = loadError.isEmpty() ? stageText("Unable to reopen the source package for reading.") : loadError;
			}
			return false;
		}
		m_baseReader = reader;
	}
	return m_baseReader->readEntryBytes(entry.baseVirtualPath, out, error);
}

PackageStagingSummary PackageStagingModel::summary() const
{
	ensurePlan();
	PackageStagingSummary result;
	result.sourcePath = m_sourcePath;
	result.sourceFormat = m_sourceFormat;
	result.baseFileCount = static_cast<int>(m_baseEntries.size());
	result.baseDirectoryCount = static_cast<int>(m_baseDirectories.size());
	result.operationCount = static_cast<int>(m_operations.size());
	result.beforeBytes = totalBytes(m_baseEntries);
	result.afterBytes = totalBytes(m_planEntries);
	for (const PackageStagedEntry& entry : m_planEntries) {
		if (entry.kind == PackageEntryKind::Directory) {
			++result.stagedDirectoryCount;
		} else {
			++result.stagedFileCount;
		}
	}
	for (const PackageStageOperation& operation : m_operations) {
		switch (operation.type) {
		case PackageStageOperationType::Add:
			++result.addedCount;
			break;
		case PackageStageOperationType::Replace:
			++result.replacedCount;
			break;
		case PackageStageOperationType::Rename:
			++result.renamedCount;
			break;
		case PackageStageOperationType::Delete:
			++result.deletedCount;
			break;
		}
	}
	result.conflictCount = static_cast<int>(m_planConflicts.size());
	for (const PackageStageConflict& conflict : m_planConflicts) {
		if (conflict.blocking) {
			++result.blockingCount;
			result.blockedMessages.push_back(conflict.virtualPath.isEmpty() ? conflict.message : QStringLiteral("%1: %2").arg(conflict.virtualPath, conflict.message));
		}
	}
	result.canSave = m_loaded && result.blockingCount == 0;
	return result;
}

QVector<PackageCompositionBucket> PackageStagingModel::beforeComposition() const
{
	return compositionBuckets(m_baseEntries);
}

QVector<PackageCompositionBucket> PackageStagingModel::afterComposition() const
{
	ensurePlan();
	return compositionBuckets(m_planEntries);
}

QByteArray PackageStagingModel::manifestJson() const
{
	ensurePlan();
	const PackageStagingSummary stagingSummary = summary();
	QJsonObject summaryObject;
	summaryObject.insert(QStringLiteral("sourcePath"), stagingSummary.sourcePath);
	summaryObject.insert(QStringLiteral("sourceFormat"), packageArchiveFormatId(stagingSummary.sourceFormat));
	summaryObject.insert(QStringLiteral("baseFileCount"), stagingSummary.baseFileCount);
	summaryObject.insert(QStringLiteral("baseDirectoryCount"), stagingSummary.baseDirectoryCount);
	summaryObject.insert(QStringLiteral("stagedFileCount"), stagingSummary.stagedFileCount);
	summaryObject.insert(QStringLiteral("stagedDirectoryCount"), stagingSummary.stagedDirectoryCount);
	summaryObject.insert(QStringLiteral("operationCount"), stagingSummary.operationCount);
	summaryObject.insert(QStringLiteral("addedCount"), stagingSummary.addedCount);
	summaryObject.insert(QStringLiteral("replacedCount"), stagingSummary.replacedCount);
	summaryObject.insert(QStringLiteral("renamedCount"), stagingSummary.renamedCount);
	summaryObject.insert(QStringLiteral("deletedCount"), stagingSummary.deletedCount);
	summaryObject.insert(QStringLiteral("conflictCount"), stagingSummary.conflictCount);
	summaryObject.insert(QStringLiteral("blockingCount"), stagingSummary.blockingCount);
	summaryObject.insert(QStringLiteral("beforeBytes"), static_cast<double>(stagingSummary.beforeBytes));
	summaryObject.insert(QStringLiteral("afterBytes"), static_cast<double>(stagingSummary.afterBytes));
	summaryObject.insert(QStringLiteral("canSave"), stagingSummary.canSave);

	QHash<QString, QString> digestCache;
	const auto entryArray = [this, &digestCache](const QVector<PackageStagedEntry>& entries) {
		QJsonArray array;
		for (const PackageStagedEntry& entry : entries) {
			QJsonObject object;
			object.insert(QStringLiteral("virtualPath"), entry.virtualPath);
			object.insert(QStringLiteral("kind"), packageEntryKindId(entry.kind));
			object.insert(QStringLiteral("bytes"), static_cast<double>(entry.sizeBytes));
			object.insert(QStringLiteral("source"), entry.source);
			object.insert(QStringLiteral("operationId"), entry.operationId);
			if (entry.kind == PackageEntryKind::File) {
				const QString cacheKey = entryContentKey(entry);
				auto cached = digestCache.constFind(cacheKey);
				if (cached == digestCache.constEnd()) {
					QByteArray bytes;
					QString readError;
					const QString digest = entryBytes(entry, &bytes, &readError)
						? QString::fromLatin1(sha256Bytes(bytes))
						: QString();
					cached = digestCache.insert(cacheKey, digest);
				}
				object.insert(QStringLiteral("sha256"), cached.value());
			}
			array.append(object);
		}
		return array;
	};

	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 2);
	root.insert(QStringLiteral("summary"), summaryObject);
	root.insert(QStringLiteral("operations"), operationsJson(m_operations));
	root.insert(QStringLiteral("conflicts"), conflictsJson(m_planConflicts));
	root.insert(QStringLiteral("beforeEntries"), entryArray(m_baseEntries));
	root.insert(QStringLiteral("afterEntries"), entryArray(m_planEntries));
	root.insert(QStringLiteral("beforeComposition"), bucketsJson(compositionBuckets(m_baseEntries)));
	root.insert(QStringLiteral("afterComposition"), bucketsJson(compositionBuckets(m_planEntries)));
	return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool PackageStagingModel::addFile(const QString& sourceFilePath, const QString& virtualPath, QString* error, PackageStageConflictResolution resolution)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Add;
	operation.sourceFilePath = QFileInfo(sourceFilePath).absoluteFilePath();
	operation.virtualPath = virtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error);
}

bool PackageStagingModel::replaceFile(const QString& virtualPath, const QString& sourceFilePath, QString* error)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Replace;
	operation.virtualPath = virtualPath;
	operation.sourceFilePath = QFileInfo(sourceFilePath).absoluteFilePath();
	return appendOperation(operation, error);
}

bool PackageStagingModel::renameEntry(const QString& virtualPath, const QString& targetVirtualPath, QString* error, PackageStageConflictResolution resolution)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Rename;
	operation.virtualPath = virtualPath;
	operation.targetVirtualPath = targetVirtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error);
}

bool PackageStagingModel::deleteEntry(const QString& virtualPath, QString* error, PackageStageConflictResolution resolution)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Delete;
	operation.virtualPath = virtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error);
}

bool PackageStagingModel::clearOperation(const QString& operationId)
{
	for (qsizetype index = 0; index < m_operations.size(); ++index) {
		if (m_operations[index].id == operationId) {
			m_operations.removeAt(index);
			invalidatePlan();
			return true;
		}
	}
	return false;
}

bool PackageStagingModel::exportManifest(const QString& outputPath, QString* error) const
{
	if (error) {
		error->clear();
	}
	if (outputPath.trimmed().isEmpty()) {
		if (error) {
			*error = stageText("Manifest output path is required.");
		}
		return false;
	}
	QSaveFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = stageText("Unable to open package manifest for writing.");
		}
		return false;
	}
	const QByteArray json = manifestJson();
	if (file.write(json) != json.size()) {
		if (error) {
			*error = stageText("Unable to write package manifest.");
		}
		return false;
	}
	if (!file.commit()) {
		if (error) {
			*error = stageText("Unable to commit package manifest.");
		}
		return false;
	}
	return true;
}

PackageWriteReport PackageStagingModel::writeArchive(const PackageWriteRequest& request) const
{
	PackageWriteReport report;
	report.sourcePath = m_sourcePath;
	report.outputPath = QFileInfo(request.destinationPath).absoluteFilePath();
	report.format = request.format == PackageArchiveFormat::Unknown ? packageArchiveFormatFromFileName(request.destinationPath) : request.format;
	report.dryRun = request.dryRun;
	report.timestampModeId = packageTimestampModeId(request.timestampMode);

	const PackageStagingSummary stagingSummary = summary();
	if (!stagingSummary.canSave) {
		report.blockedMessages = stagingSummary.blockedMessages;
		if (report.blockedMessages.isEmpty()) {
			report.blockedMessages.push_back(stageText("Package staging model is not saveable."));
		}
		return report;
	}
	if (request.destinationPath.trimmed().isEmpty()) {
		report.blockedMessages.push_back(stageText("Save-as destination path is required."));
		return report;
	}
	// Canonical paths so a junction, symlink, or mapped path cannot point the
	// save-as output back at the package that is being read.
	const bool destinationIsSource = canonicalComparePath(request.destinationPath).compare(canonicalComparePath(m_sourcePath), Qt::CaseInsensitive) == 0;
	if (destinationIsSource && !request.allowInPlaceOverwrite) {
		report.blockedMessages.push_back(stageText("Save-as destination must be different from the source package path."));
		return report;
	}
	const bool destinationExists = QFileInfo::exists(request.destinationPath);
	// The backup dance only exists to protect a file that is really there, and
	// a dry run never touches the filesystem at all.
	const bool overwriteInPlace = request.allowInPlaceOverwrite && destinationExists && !request.dryRun;
	if (destinationExists && !request.allowOverwrite && !overwriteInPlace) {
		report.blockedMessages.push_back(stageText("Destination already exists. Choose a new save-as path or enable overwrite explicitly."));
		return report;
	}
	if (destinationExists && !QFileInfo(request.destinationPath).isFile()) {
		report.blockedMessages.push_back(stageText("Destination exists and is not a file."));
		return report;
	}

	const bool zipFamily = report.format == PackageArchiveFormat::Zip || report.format == PackageArchiveFormat::Pk3;
	if (!(report.format == PackageArchiveFormat::Pak || zipFamily || report.format == PackageArchiveFormat::Wad)) {
		report.blockedMessages.push_back(stageText("Write-back supports PAK, ZIP, PK3, and WAD outputs."));
		return report;
	}

	PackageWriteOptions options;
	options.level = zipFamily ? request.compression : DeflateLevel::Store;
	options.timestampMode = request.timestampMode;
	if (report.format == PackageArchiveFormat::Wad) {
		QString magic = request.wadMagic.trimmed().toUpper();
		if (magic.isEmpty()) {
			magic = m_sourceWadMagic;
		}
		if (magic.isEmpty()) {
			magic = QStringLiteral("PWAD");
		}
		if (!wadMagicIsSupported(magic)) {
			report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Unsupported WAD magic: %1").arg(magic));
			return report;
		}
		options.wadMagic = magic;
		report.wadMagic = magic;
	}
	report.compressionId = zipFamily ? deflateLevelId(options.level) : QStringLiteral("stored");

	const QVector<PackageStagedEntry> entries = plannedEntries();
	const EntryBytesProvider provider = [this](const PackageStagedEntry& entry, QByteArray* out, QString* readError) {
		return entryBytes(entry, out, readError);
	};

	const auto runWriter = [&entries, &provider, &options, &report](ByteSink* sink, PackageWriteStats* stats, QString* writerError) {
		if (report.format == PackageArchiveFormat::Pak) {
			return writePakStream(entries, provider, sink, stats, writerError);
		}
		if (report.format == PackageArchiveFormat::Wad) {
			return writeWadStream(entries, provider, options, sink, stats, writerError);
		}
		return writeZipStream(entries, provider, options, sink, stats, writerError);
	};

	QString writePath = report.outputPath;
	if (overwriteInPlace) {
		writePath = uniqueSiblingPath(report.outputPath);
		if (writePath.isEmpty()) {
			report.blockedMessages.push_back(stageText("Unable to reserve a temporary file beside the destination."));
			return report;
		}
	}

	QSaveFile outputFile(writePath);
	if (!request.dryRun && !outputFile.open(QIODevice::WriteOnly)) {
		report.blockedMessages.push_back(stageText("Unable to open save-as package path."));
		return report;
	}

	PackageWriteStats stats;
	QString writerError;
	ByteSink sink(request.dryRun ? nullptr : &outputFile);
	if (!runWriter(&sink, &stats, &writerError) || !sink.flush()) {
		report.blockedMessages.push_back(writerError.isEmpty() ? stageText("Unable to write package bytes.") : writerError);
		if (overwriteInPlace) {
			// QSaveFile discards its own scratch file when it is never
			// committed, so the original is already untouched; this only
			// clears a partial file if the platform left one behind.
			outputFile.cancelWriting();
			QFile::remove(writePath);
		}
		return report;
	}

	report.entryCount = stats.fileCount;
	report.directoryCount = stats.directoryCount;
	report.bytesWritten = static_cast<quint64>(sink.size());
	report.uncompressedBytes = stats.uncompressedBytes;
	report.compressionRatio = stats.uncompressedBytes == 0
		? 1.0
		: static_cast<double>(stats.payloadBytes) / static_cast<double>(stats.uncompressedBytes);
	report.sha256 = sink.digest();
	// Every writer here is a pure function of the plan plus the compression
	// level, so output is reproducible whenever the fixed timestamps are used.
	// Preserving source timestamps makes the bytes depend on filesystem
	// metadata, which is deliberately not claimed as reproducible.
	report.deterministic = request.timestampMode == PackageTimestampMode::Reproducible;

	if (request.verifyDeterminism) {
		PackageWriteStats verifyStats;
		QString verifyError;
		ByteSink verifySink(nullptr);
		if (!runWriter(&verifySink, &verifyStats, &verifyError)) {
			report.deterministic = false;
			report.warnings.push_back(verifyError.isEmpty() ? stageText("Determinism verification pass failed.") : verifyError);
		} else {
			report.determinismVerified = true;
			report.deterministic = verifySink.digest() == report.sha256;
			if (!report.deterministic) {
				report.warnings.push_back(stageText("Determinism verification found different bytes on a repeat write."));
			}
		}
	}

	if (request.dryRun) {
		return report;
	}

	if (!outputFile.commit()) {
		report.blockedMessages.push_back(stageText("Unable to commit save-as package file."));
		if (overwriteInPlace) {
			QFile::remove(writePath);
		}
		return report;
	}

	if (overwriteInPlace) {
		// Verify before anything irreversible happens: the replacement has to
		// be complete and readable on disk, not merely reported as written.
		quint64 verifiedBytes = 0;
		const QString verifiedDigest = fileSha256(writePath, &verifiedBytes);
		if (verifiedDigest.isEmpty() || verifiedDigest != report.sha256 || verifiedBytes != report.bytesWritten) {
			QFile::remove(writePath);
			report.blockedMessages.push_back(stageText("The replacement package did not verify; the original package was left untouched."));
			return report;
		}

		const QString backupPath = request.backupPath.trimmed().isEmpty()
			? QStringLiteral("%1.bak").arg(report.outputPath)
			: QFileInfo(request.backupPath).absoluteFilePath();
		if (canonicalComparePath(backupPath).compare(canonicalComparePath(report.outputPath), Qt::CaseInsensitive) == 0) {
			QFile::remove(writePath);
			report.blockedMessages.push_back(stageText("Backup path must be different from the destination path."));
			return report;
		}
		if (QFileInfo::exists(backupPath) && !QFile::remove(backupPath)) {
			QFile::remove(writePath);
			report.blockedMessages.push_back(stageText("Unable to replace the previous backup file; the original package was left untouched."));
			return report;
		}
		if (!QFile::rename(report.outputPath, backupPath)) {
			QFile::remove(writePath);
			report.blockedMessages.push_back(stageText("Unable to move the original package to its backup path; the original package was left untouched."));
			return report;
		}
		if (!QFile::rename(writePath, report.outputPath)) {
			// Put the original back before giving up, so a failure here still
			// ends with the package where the caller left it.
			QFile::rename(backupPath, report.outputPath);
			QFile::remove(writePath);
			report.blockedMessages.push_back(stageText("Unable to move the new package into place; the original package was restored."));
			return report;
		}
		report.backupPath = backupPath;
		report.overwroteInPlace = true;
		// The cached reader described the file that has just been replaced.
		m_baseReader.reset();
	}

	if (request.writeManifest) {
		report.manifestPath = request.manifestPath.trimmed().isEmpty()
			? QStringLiteral("%1.manifest.json").arg(report.outputPath)
			: QFileInfo(request.manifestPath).absoluteFilePath();
		QString manifestError;
		if (exportManifest(report.manifestPath, &manifestError)) {
			report.wroteManifest = true;
		} else {
			report.warnings.push_back(manifestError.isEmpty() ? stageText("Unable to write package manifest.") : manifestError);
		}
	}
	return report;
}

QString PackageStagingModel::nextOperationId()
{
	// Monotonic: ids are never reused, so clearing an operation cannot make a
	// later operation collide with an existing one.
	++m_operationSerial;
	return QStringLiteral("stage-%1").arg(m_operationSerial);
}

bool PackageStagingModel::appendOperation(PackageStageOperation operation, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!m_loaded) {
		if (error) {
			*error = stageText("Load a package before staging changes.");
		}
		return false;
	}
	if (operation.id.trimmed().isEmpty()) {
		operation.id = nextOperationId();
	}
	m_operations.push_back(operation);
	invalidatePlan();
	return true;
}

QString packageStageOperationTypeId(PackageStageOperationType type)
{
	switch (type) {
	case PackageStageOperationType::Add:
		return QStringLiteral("add");
	case PackageStageOperationType::Replace:
		return QStringLiteral("replace");
	case PackageStageOperationType::Rename:
		return QStringLiteral("rename");
	case PackageStageOperationType::Delete:
		return QStringLiteral("delete");
	}
	return QStringLiteral("add");
}

QString packageStageOperationTypeDisplayName(PackageStageOperationType type)
{
	switch (type) {
	case PackageStageOperationType::Add:
		return stageText("Add");
	case PackageStageOperationType::Replace:
		return stageText("Replace");
	case PackageStageOperationType::Rename:
		return stageText("Rename");
	case PackageStageOperationType::Delete:
		return stageText("Delete");
	}
	return stageText("Add");
}

PackageStageOperationType packageStageOperationTypeFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("replace")) {
		return PackageStageOperationType::Replace;
	}
	if (normalized == QStringLiteral("rename")) {
		return PackageStageOperationType::Rename;
	}
	if (normalized == QStringLiteral("delete") || normalized == QStringLiteral("remove")) {
		return PackageStageOperationType::Delete;
	}
	return PackageStageOperationType::Add;
}

QString packageStageConflictResolutionId(PackageStageConflictResolution resolution)
{
	switch (resolution) {
	case PackageStageConflictResolution::Block:
		return QStringLiteral("block");
	case PackageStageConflictResolution::ReplaceExisting:
		return QStringLiteral("replace-existing");
	case PackageStageConflictResolution::Skip:
		return QStringLiteral("skip");
	}
	return QStringLiteral("block");
}

PackageStageConflictResolution packageStageConflictResolutionFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("replace") || normalized == QStringLiteral("replace-existing") || normalized == QStringLiteral("staged-wins")) {
		return PackageStageConflictResolution::ReplaceExisting;
	}
	if (normalized == QStringLiteral("skip") || normalized == QStringLiteral("keep-existing")) {
		return PackageStageConflictResolution::Skip;
	}
	return PackageStageConflictResolution::Block;
}

QString packageTimestampModeId(PackageTimestampMode mode)
{
	switch (mode) {
	case PackageTimestampMode::Reproducible:
		return QStringLiteral("reproducible");
	case PackageTimestampMode::PreserveSource:
		return QStringLiteral("preserve-source");
	}
	return QStringLiteral("reproducible");
}

PackageTimestampMode packageTimestampModeFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("preserve-source") || normalized == QStringLiteral("preserve") || normalized == QStringLiteral("source")) {
		return PackageTimestampMode::PreserveSource;
	}
	return PackageTimestampMode::Reproducible;
}

QString packageWriteReportText(const PackageWriteReport& report)
{
	QStringList lines;
	lines << stageText("Package save-as");
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Mode: %1").arg(report.dryRun ? stageText("dry run") : stageText("write"));
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Output: %1").arg(report.outputPath.isEmpty() ? stageText("not written") : report.outputPath);
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Format: %1").arg(packageArchiveFormatId(report.format));
	if (!report.wadMagic.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioPackageStaging", "WAD magic: %1").arg(report.wadMagic);
	}
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Entries: %1").arg(report.entryCount);
	if (report.directoryCount > 0) {
		lines << QCoreApplication::translate("VibeStudioPackageStaging", "Directory records: %1").arg(report.directoryCount);
	}
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Bytes: %1").arg(report.bytesWritten);
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Compression: %1").arg(report.compressionId.isEmpty() ? QStringLiteral("stored") : report.compressionId);
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Compression ratio: %1").arg(report.compressionRatio, 0, 'f', 3);
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Timestamps: %1").arg(report.timestampModeId.isEmpty() ? packageTimestampModeId(PackageTimestampMode::Reproducible) : report.timestampModeId);
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "SHA-256: %1").arg(report.sha256.isEmpty() ? stageText("not available") : report.sha256);
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Manifest: %1").arg(report.wroteManifest ? report.manifestPath : stageText("not written"));
	if (report.overwroteInPlace || !report.backupPath.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioPackageStaging", "Replaced in place, backup: %1").arg(report.backupPath.isEmpty() ? stageText("not written") : report.backupPath);
	}
	if (!report.blockedMessages.isEmpty()) {
		lines << stageText("Blocked:");
		for (const QString& blocked : report.blockedMessages) {
			lines << QStringLiteral("- %1").arg(blocked);
		}
	}
	if (!report.warnings.isEmpty()) {
		lines << stageText("Warnings:");
		for (const QString& warning : report.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
