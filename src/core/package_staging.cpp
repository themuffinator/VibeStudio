#include "core/package_staging.h"
#include "core/package_selection.h"
#include "core/package_snapshot_p.h"
#include "core/package_index_p.h"
#include "core/package_plan_p.h"
#include "core/package_protection_p.h"
#include "core/package_wad_groups.h"

#include "core/deflate.h"
#include "core/package_directory.h"
#include "core/package_publication.h"

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
#include <QTimeZone>

#include <algorithm>
#include <functional>
#include <limits>

namespace vibestudio {

namespace {

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
	return virtualPath.normalized(QString::NormalizationForm_C).toCaseFolded();
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
bool stagedEntrySourceLess(const PackageStagedEntry& left, const PackageStagedEntry& right)
{
	const int leftOrdinal = left.sourceOrdinal < 0 ? std::numeric_limits<int>::max() : left.sourceOrdinal;
	const int rightOrdinal = right.sourceOrdinal < 0 ? std::numeric_limits<int>::max() : right.sourceOrdinal;
	return leftOrdinal < rightOrdinal;
}

bool readSourceFile(const PackageStagedEntry& entry, QByteArray* bytes, QString* error, qint64 maxBytes)
{
	if (error) {
		error->clear();
	}
	if (bytes) {
		bytes->clear();
	}
	const QFileInfo before(entry.sourceFilePath);
	if (!before.isFile() || before.size() < 0 || static_cast<quint64>(before.size()) != entry.sizeBytes
		|| !entry.sourceIdentity || !entry.sourceIdentity->matchesMetadata()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "A staged source file changed. Stage it again before saving."); }
		return false;
	}
	PackageContentDevice file(entry.sourceIdentity);
	if (!file.open(QIODevice::ReadOnly)) {
		const QFileInfo info(entry.sourceFilePath);
		if (error) {
			*error = info.exists() ? QCoreApplication::translate("VibeStudioPackageStaging", "Unable to read source file.") : QCoreApplication::translate("VibeStudioPackageStaging", "Source file does not exist.");
		}
		return false;
	}
	if (bytes) {
		const qint64 count = maxBytes < 0 ? file.size() : qMin(maxBytes, file.size());
		*bytes = file.read(count);
		const QFileInfo after(entry.sourceFilePath);
		if (bytes->size() != count || file.error() != QFileDevice::NoError || before.size() != after.size()
			|| before.lastModified() != after.lastModified()) {
			bytes->clear();
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Unable to read an unchanged staged source file completely."); }
			return false;
		}
	}
	return true;
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
	if (QStringList {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm"),
			QStringLiteral("md5mesh"), QStringLiteral("md5anim"), QStringLiteral("mds"), QStringLiteral("mdm"), QStringLiteral("mdx"), QStringLiteral("glm"),
			QStringLiteral("gla"), QStringLiteral("lwo"), QStringLiteral("ase"), QStringLiteral("fm"), QStringLiteral("kvx")}.contains(suffix)) {
		return QStringLiteral("model");
	}
	return suffix;
}

bool compositionBuckets(const QVector<PackageStagedEntry>& entries, QVector<PackageCompositionBucket>* output,
	quint64* total, bool* overflow, PackagePlanWork& work, const PackageStagingPlanLimits& limits)
{
	QVector<PackageCompositionBucket> buckets;
	QHash<QString, int> index;
	PackagePlanBudget budget(limits, work);
	if (!budget.validate()) { return false; }
	for (const auto& entry : entries) {
		if (!work.checkpoint()) { return false; }
		if (entry.kind != PackageEntryKind::File) { continue; }
		const QString location = topLevelLocation(entry.virtualPath), type = typeBucket(entry.virtualPath);
		const QString id = QStringLiteral("%1:%2").arg(location, type);
		int slot = index.value(id, -1);
		if (slot < 0) {
			PackageCompositionBucket bucket; bucket.id = id;
			bucket.label = QStringLiteral("%1 / %2").arg(location, type);
			if (!budget.addKey(id) || !budget.addRecord(PackagePlanBudget::textBytes(id) + PackagePlanBudget::textBytes(bucket.label))) { return false; }
			slot = static_cast<int>(buckets.size()); buckets.append(std::move(bucket)); index.insert(id, slot);
		}
		auto& bucket = buckets[slot]; ++bucket.fileCount;
		accumulatePackageBytes(entry.sizeBytes, &bucket.sizeBytes, &bucket.sizeOverflow);
		accumulatePackageBytes(entry.sizeBytes, total, overflow);
	}
	if (!sortPackagePlan(&buckets, [](const auto& left, const auto& right) {
		if (left.sizeOverflow != right.sizeOverflow) { return left.sizeOverflow; }
		if (left.sizeBytes != right.sizeBytes) { return left.sizeBytes > right.sizeBytes; }
		return left.label.compare(right.label, Qt::CaseInsensitive) < 0;
	}, work)) { return false; }
	*output = std::move(buckets); return true;
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
	explicit ByteSink(QIODevice* device, const PackageIndexLimits& limits, std::function<bool()> isCancelled = {})
		: m_device(device), m_isCancelled(std::move(isCancelled)), m_maximumSize(packageIndexMaximumFileBytes(limits))
	{
	}

	bool append(const char* data, qint64 size)
	{
		if (m_failed || size < 0 || size > std::numeric_limits<qint64>::max() - m_size) { m_failed = true; return false; }
		if (size > m_maximumSize - m_size) {
			m_failed = true;
			m_error = QCoreApplication::translate("VibeStudioPackageStaging", "The output exceeds the %1-byte size supported by the selected source fingerprint budget. Use a smaller package.").arg(m_maximumSize);
			return false;
		}
		while (size > 0) {
			if (m_isCancelled && m_isCancelled()) return false;
			const qsizetype count = static_cast<qsizetype>(qMin<qint64>(size, 65536));
			m_hash.addData(QByteArrayView(data, count));
			m_size += count;
			if (m_device) {
				m_buffer.append(data, count);
				if (m_buffer.size() >= kFlushThreshold && !flush()) return false;
			}
			data += count;
			size -= count;
		}
		return true;
	}

	bool append(const QByteArray& data)
	{
		return append(data.constData(), data.size());
	}

	bool flush()
	{
		if (m_failed || (m_isCancelled && m_isCancelled())) return false;
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

	[[nodiscard]] QString errorString() const { return m_error; }

	[[nodiscard]] QString digest()
	{
		return QString::fromLatin1(m_hash.result().toHex());
	}

private:
	static constexpr qsizetype kFlushThreshold = 1 << 20;

	QIODevice* m_device = nullptr;
	std::function<bool()> m_isCancelled;
	QByteArray m_buffer;
	QCryptographicHash m_hash {QCryptographicHash::Sha256};
	qint64 m_size = 0;
	qint64 m_maximumSize = 0;
	QString m_error;
	bool m_failed = false;
};

using EntryStreamProvider = std::function<bool(const PackageStagedEntry&, PackageWritePhase, const std::function<bool(QByteArrayView)>&, QString*)>;

struct PackageWriteOptions {
	DeflateLevel level = DeflateLevel::Default;
	PackageTimestampMode timestampMode = PackageTimestampMode::Reproducible;
	QString wadMagic;
	bool preserveWadOrder = false;
	PackageIndexLimits indexLimits;
	PackageIndexUsage indexUsage;
	std::function<bool()> isCancelled;
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
		*error = QCoreApplication::translate("VibeStudioPackageStaging", "Unable to write package bytes.");
	}
	return false;
}

bool writePakStream(const QVector<PackageStagedEntry>& inputEntries, const EntryStreamProvider& provider, ByteSink* sink, PackageWriteStats* stats, QString* error)
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
		if (!validateArchiveSize(static_cast<qint64>(entry.sizeBytes), error, QCoreApplication::translate("VibeStudioPackageStaging", "PAK entry size"))) {
			return false;
		}
		payloadTotal += static_cast<qint64>(entry.sizeBytes);
	}
	if (!validateArchiveSize(kPakHeaderSize + payloadTotal, error, QCoreApplication::translate("VibeStudioPackageStaging", "PAK directory offset"))
		|| !validateArchiveSize(static_cast<qint64>(entries.size()) * kPakRecordSize, error, QCoreApplication::translate("VibeStudioPackageStaging", "PAK directory"))) {
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
		const quint32 offset = static_cast<quint32>(sink->size());
		records.push_back({entry.virtualPath.toLatin1(), offset, static_cast<quint32>(entry.sizeBytes)});
		if (!provider(entry, PackageWritePhase::Write, [sink](QByteArrayView bytes) { return sink->append(bytes.data(), bytes.size()); }, error)) return false;
		if (stats) {
			++stats->fileCount;
			++stats->storedCount;
			stats->uncompressedBytes += entry.sizeBytes;
			stats->payloadBytes += entry.sizeBytes;
		}
	}

	for (const Record& record : records) {
		QByteArray directory;
		const qsizetype start = directory.size();
		directory.append(record.name);
		while (directory.size() - start < kPakNameLimit) {
			directory.append('\0');
		}
		appendLe32(&directory, record.offset);
		appendLe32(&directory, record.size);
		if (!sink->append(directory)) return sinkError(error);
	}
	return true;
}

struct ZipStreamEntry {
	quint16 method = 0;
	quint32 crc = 0;
	quint64 uncompressedSize = 0;
	quint64 payloadSize = 0;
	QByteArray sha256;
};

// ZIP local headers need the CRC and sizes before the payload. A read-only
// measurement pass retains the existing stored fallback without a payload spool
// or data descriptors; dry runs therefore never create temporary output files.
bool streamZipEntry(const PackageStagedEntry& entry, const EntryStreamProvider& provider,
	DeflateLevel level, ByteSink* output, PackageWritePhase phase,
	const std::function<bool()>& isCancelled, ZipStreamEntry* result, QString* error)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	const auto emitPayload = [&](QByteArrayView bytes) {
		if (output && !output->append(bytes.data(), bytes.size())) return false;
		result->payloadSize += static_cast<quint64>(bytes.size());
		return true;
	};
	std::unique_ptr<DeflateStreamEncoder> encoder;
	if (level != DeflateLevel::Store && entry.sizeBytes != 0) {
		if (entry.sizeBytes > static_cast<quint64>(std::numeric_limits<qint64>::max())) return sinkError(error);
		encoder = std::make_unique<DeflateStreamEncoder>(static_cast<qint64>(entry.sizeBytes), level, emitPayload, isCancelled);
	}
	const bool read = provider(entry, phase, [&](QByteArrayView bytes) {
		result->crc = crc32View(bytes, result->crc);
		result->uncompressedSize += static_cast<quint64>(bytes.size());
		hash.addData(bytes);
		return encoder ? encoder->append(bytes) : emitPayload(bytes);
	}, error);
	if (!read) {
		if (encoder && error && !encoder->result().error.isEmpty()) *error = encoder->result().error;
		return false;
	}
	if (encoder) {
		const auto encoded = encoder->finish();
		if (!encoded.ok) { if (error) *error = encoded.error; return false; }
	}
	result->sha256 = hash.result();
	return true;
}

bool writeZipStream(const QVector<PackageStagedEntry>& inputEntries, const EntryStreamProvider& provider, const PackageWriteOptions& options, ByteSink* sink, PackageWriteStats* stats, QString* error)
{
	QVector<PackageStagedEntry> entries = inputEntries;
	sortStagedEntries(&entries);

	QByteArray central;
	PackageIndexUsage indexUsage = options.indexUsage;
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

		if (options.isCancelled && options.isCancelled()) return false;
		ZipStreamEntry compressed;
		if (!directoryEntry) {
			if (!streamZipEntry(entry, provider, options.level, nullptr, PackageWritePhase::Measure, options.isCancelled, &compressed, error)) return false;
			if (compressed.payloadSize < compressed.uncompressedSize) compressed.method = 8;
			else compressed.payloadSize = compressed.uncompressedSize;
		}

		const quint64 localOffset = static_cast<quint64>(sink->size());
		const quint64 payloadSize = compressed.payloadSize;
		// APPNOTE.TXT 4.4.1.4 / 4.5.3: 0xffffffff in a 32-bit size or offset
		// field is the marker that says "the real value lives in the ZIP64
		// extended information extra field", so a value *equal to* the sentinel
		// is indistinguishable from the marker and needs the ZIP64 record too.
		// The comparisons are >= for that reason, not >.
		const bool zip64Sizes = payloadSize >= kZip32Sentinel || compressed.uncompressedSize >= kZip32Sentinel;
		const bool zip64Offset = localOffset >= kZip32Sentinel;
		// Fixed central records and names were admitted before any payload read.
		// Admit exact ZIP64 growth before retaining or writing its extra fields.
		const qint64 extraBytes = (zip64Sizes || zip64Offset) ? 4 + (zip64Sizes ? 16 : 0) + (zip64Offset ? 8 : 0) : 0;
		if (!admitPackageIndex(&indexUsage, options.indexLimits, 0, extraBytes, error)) { return false; }
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
		if (!sink->append(local)) return sinkError(error);
		if (!directoryEntry) {
			ZipStreamEntry written;
			if (!streamZipEntry(entry, provider, compressed.method == 8 ? options.level : DeflateLevel::Store,
				sink, PackageWritePhase::Write, options.isCancelled, &written, error)) return false;
			if (written.crc != compressed.crc || written.sha256 != compressed.sha256
				|| written.payloadSize != payloadSize || written.uncompressedSize != compressed.uncompressedSize) {
				if (error) *error = QCoreApplication::translate("VibeStudioPackageStaging", "Entry changed between compression measurement and writing: %1").arg(entry.virtualPath);
				return false;
			}
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

// Predict the same disk index that the reader will admit, without reading
// source payloads or creating output. ZIP64 extras are charged by the writer
// once measured payload sizes and offsets are known. Internal staging/history
// budgets remain separate: exporting must also fit this reopening policy.
bool admitWrittenIndex(const QVector<PackageStagedEntry>& entries, PackageArchiveFormat format,
	const PackageWriteRequest& request, PackageWriteOptions* options, QString* error)
{
	const bool zip = format == PackageArchiveFormat::Zip || format == PackageArchiveFormat::Pk3;
	const bool wad = format == PackageArchiveFormat::Wad;
	const bool textureWad = wad && (options->wadMagic == QStringLiteral("WAD2") || options->wadMagic == QStringLiteral("WAD3"));
	const auto stopped = [&] { return request.isCancelled && request.isCancelled(); };
	const auto progress = [&](qsizetype done, qsizetype total) {
		if (request.byteProgress) { request.byteProgress(PackageWritePhase::CheckIndex, {}, done, total); }
	};
	const qsizetype count = zip ? entries.size() : std::count_if(entries.cbegin(), entries.cend(), [](const auto& entry) { return entry.kind == PackageEntryKind::File; });
	PackageIndexUsage usage;
	if (!admitPackageIndex(&usage, request.indexLimits, count, 0, error)) { return false; }
	QVector<PackageEntry> projected;
	projected.reserve(count);
	qint64 nativePayloadTotal = 0;
	progress(0, count);
	for (const auto& entry : entries) {
		if (stopped()) { return false; }
		if (!zip && entry.kind == PackageEntryKind::Directory) { continue; }
		QString name = wad && !textureWad ? entry.virtualPath.toUpper() : entry.virtualPath;
		if (zip && entry.kind == PackageEntryKind::Directory) { name += QLatin1Char('/'); }
		const QByteArray encoded = zip ? name.toUtf8() : name.toLatin1();
		// Reject known native wire limits before source verification can stream
		// an owned provider. The writer repeats these checks defensively.
		if (!zip) {
			const int nameLimit = wad ? (textureWad ? kTextureWadNameLimit : kDoomWadNameLimit) : kPakNameLimit;
			if (wad ? !wadNameIsValid(name, nameLimit) : (encoded.isEmpty() || encoded.size() > nameLimit || QString::fromLatin1(encoded) != name)) {
				if (error) { *error = wad
					? QCoreApplication::translate("VibeStudioPackageStaging", "WAD lump names must be Latin-1, contain no folders, and be at most %1 characters: %2").arg(nameLimit).arg(entry.virtualPath)
					: QCoreApplication::translate("VibeStudioPackageStaging", "PAK entry path must be Latin-1 and at most 56 bytes: %1").arg(entry.virtualPath); }
				return false;
			}
			const auto size = qint64(qMin<quint64>(entry.sizeBytes, std::numeric_limits<qint64>::max()));
			if (!validateArchiveSize(size, error, wad ? QCoreApplication::translate("VibeStudioPackageStaging", "WAD lump size")
				: QCoreApplication::translate("VibeStudioPackageStaging", "PAK entry size"))) { return false; }
			nativePayloadTotal += size; // Count and each size are already bounded.
		}
		const auto normalized = normalizePackageVirtualPath(name, false);
		const QString expected = wad && !textureWad ? entry.virtualPath.toUpper() : entry.virtualPath;
		if (!normalized.isSafe() || normalized.normalizedPath != expected
			|| (zip ? QString::fromUtf8(encoded) : QString::fromLatin1(encoded)) != name) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "The output entry path cannot be reopened without changing its name: %1").arg(entry.virtualPath); }
			return false;
		}
		const qint64 directoryBytes = zip ? 46 + encoded.size() : wad ? (textureWad ? 32 : 16) : 64;
		if (!admitPackageIndex(&usage, request.indexLimits, 0, directoryBytes, error)
			|| !admitPackageIndexPath(&usage, request.indexLimits, name, error)) { return false; }
		PackageEntry item; item.virtualPath = expected; item.kind = entry.kind;
		projected.append(std::move(item));
		if (projected.size() % 256 == 0 || projected.size() == count) { progress(projected.size(), count); }
	}
	if (stopped()) { return false; }
	if (!zip && !validateArchiveSize((wad ? kWadHeaderSize : kPakHeaderSize) + nativePayloadTotal, error,
		wad ? QCoreApplication::translate("VibeStudioPackageStaging", "WAD directory offset")
			: QCoreApplication::translate("VibeStudioPackageStaging", "PAK directory offset"))) { return false; }
	if (!wad) {
		// Match wire ordering when two differently spelled parent paths share a
		// case-folded identity; the first spelling determines its text charge.
		std::stable_sort(projected.begin(), projected.end(), [](const auto& left, const auto& right) {
			const int folded = left.virtualPath.compare(right.virtualPath, Qt::CaseInsensitive);
			return folded != 0 ? folded < 0 : left.virtualPath.compare(right.virtualPath, Qt::CaseSensitive) < 0;
		});
	}
	qsizetype preparedFolders = 0;
	if (!addPackageIndexDirectories(&projected, {}, [&](const QString& path) {
		if (++preparedFolders % 256 == 0) { progress(count + preparedFolders, 0); }
		return !stopped() && admitPackageIndex(&usage, request.indexLimits, 1, 0, error)
			&& admitPackageIndexPath(&usage, request.indexLimits, path, error);
	}, stopped)) { return false; }
	if (!wad) {
		QSet<QString> seen;
		for (const auto& entry : projected) {
			if (stopped()) { return false; }
			const QString key = entry.virtualPath.toCaseFolded();
			if (seen.contains(key)) {
				const qint64 bytes = (entry.virtualPath.size() + packageIndexDuplicateWarning().size()) * qint64(sizeof(QChar));
				if (!admitPackageIndex(&usage, request.indexLimits, 0, bytes, error)) { return false; }
			} else { seen.insert(key); }
		}
	}
	options->indexLimits = request.indexLimits;
	options->indexUsage = usage;
	return !stopped();
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
		QStringLiteral("VS_SCENE"),
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

bool writeWadStream(const QVector<PackageStagedEntry>& inputEntries, const EntryStreamProvider& provider, const PackageWriteOptions& options, ByteSink* sink, PackageWriteStats* stats, QString* error)
{
	const QString magic = options.wadMagic;
	if (!wadMagicIsSupported(magic)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Unsupported WAD magic: %1").arg(magic);
		}
		return false;
	}
	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const int nameLimit = textureWad ? kTextureWadNameLimit : kDoomWadNameLimit;

	QVector<PackageStagedEntry> entries = fileEntriesOnly(inputEntries);
	if (textureWad || options.preserveWadOrder) {
		// Existing WAD plans retain their exact positional layout.
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
				*error = groupError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "Cannot order a multi-map Doom WAD from this plan.") : groupError;
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
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "WAD entry count exceeds the 32-bit directory limit.");
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
		if (!validateArchiveSize(static_cast<qint64>(entry.sizeBytes), error, QCoreApplication::translate("VibeStudioPackageStaging", "WAD lump size"))) {
			return false;
		}
		payloadTotal += static_cast<qint64>(entry.sizeBytes);
	}
	if (!validateArchiveSize(kWadHeaderSize + payloadTotal, error, QCoreApplication::translate("VibeStudioPackageStaging", "WAD directory offset"))) {
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
		const quint32 offset = static_cast<quint32>(sink->size());
		const QString name = textureWad ? entry.virtualPath : entry.virtualPath.toUpper();
		quint8 type = entry.wadLumpType;
		// WAD base metadata and explicit native edits already carry exact types,
		// including zero. Only conversion from a non-WAD document needs a default.
		if (textureWad && type == 0 && !options.preserveWadOrder) {
			type = magic == QStringLiteral("WAD3") ? kWad3MiptexType : kWad2MiptexType;
		}
		records.push_back({name.toLatin1(), offset, static_cast<quint32>(entry.sizeBytes), type});
		if (!provider(entry, PackageWritePhase::Write, [sink](QByteArrayView bytes) { return sink->append(bytes.data(), bytes.size()); }, error)) return false;
		if (stats) {
			++stats->fileCount;
			++stats->storedCount;
			stats->uncompressedBytes += entry.sizeBytes;
			stats->payloadBytes += entry.sizeBytes;
		}
	}

	for (const Record& record : records) {
		QByteArray directory;
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
		if (!sink->append(directory)) return sinkError(error);
	}
	return true;
}

QJsonObject operationJson(const PackageStageOperation& operation, const QString& inlineDigest)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), operation.id);
	object.insert(QStringLiteral("type"), packageStageOperationTypeId(operation.type));
	object.insert(QStringLiteral("virtualPath"), operation.virtualPath);
	object.insert(QStringLiteral("targetVirtualPath"), operation.targetVirtualPath);
	object.insert(QStringLiteral("sourceFilePath"), operation.sourceFilePath);
	object.insert(QStringLiteral("sourceOrdinal"), operation.sourceOrdinal);
	if (!operation.sourceTreeIdentity.isEmpty()) { object.insert(QStringLiteral("sourceTreeIdentity"), operation.sourceTreeIdentity); }
	if (operation.wadLumpType >= 0) { object.insert(QStringLiteral("wadType"), operation.wadLumpType); }
	if (!operation.wadInsertBefore.isEmpty()) { object.insert(QStringLiteral("wadInsertBefore"), operation.wadInsertBefore); }
	if (!operation.wadNamespace.isEmpty()) { object.insert(QStringLiteral("wadNamespace"), operation.wadNamespace); }
	if (operation.hasInlineBytes) {
		object.insert(QStringLiteral("contentSource"), QStringLiteral("generated"));
		object.insert(QStringLiteral("bytes"), operation.inlineBytes.size());
		object.insert(QStringLiteral("sha256"), inlineDigest);
	} else if (operation.sourceIdentity) {
		object.insert(QStringLiteral("contentSource"), QStringLiteral("file"));
		object.insert(QStringLiteral("bytes"), operation.sourceIdentity->size);
		object.insert(QStringLiteral("sha256"), QString::fromLatin1(operation.sourceIdentity->sha256.toHex()));
		object.insert(QStringLiteral("modifiedUtc"), (operation.sourceModifiedUtc.isValid() ? operation.sourceModifiedUtc : operation.sourceIdentity->modifiedUtc).toString(Qt::ISODateWithMs));
		object.insert(QStringLiteral("contentStorage"), operation.sourceIdentity->storage ? QStringLiteral("owned-temporary") : QStringLiteral("verified-file"));
	} else if (!operation.sourceError.isEmpty()) {
		object.insert(QStringLiteral("sourceError"), operation.sourceError);
	}
	object.insert(QStringLiteral("conflictResolution"), packageStageConflictResolutionId(operation.conflictResolution));
	return object;
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
		object.insert(QStringLiteral("sizeBytes"), bucket.sizeOverflow ? QJsonValue() : QJsonValue(static_cast<double>(bucket.sizeBytes)));
		object.insert(QStringLiteral("sizeBytesExact"), bucket.sizeOverflow ? QJsonValue() : QJsonValue(QString::number(bucket.sizeBytes)));
		object.insert(QStringLiteral("sizeOverflow"), bucket.sizeOverflow);
		array.append(object);
	}
	return array;
}

QString entryContentKey(const PackageStagedEntry& entry)
{
	if (entry.hasInlineBytes) {
		// This cache exists only during one const manifest call. QByteArray
		// copies share immutable storage; use its identity without hashing the
		// whole payload just to decide whether a streamed digest is cached.
		// Neither the address nor this key is serialized into the report.
		return QStringLiteral("generated:%1:%2")
			.arg(reinterpret_cast<quintptr>(entry.inlineBytes.constData()), 0, 16).arg(entry.inlineBytes.size());
	}
	if (!entry.sourceFilePath.isEmpty()) {
		return QStringLiteral("file:%1:%2:%3").arg(entry.sourceFilePath,
			entry.sourceIdentity ? QString::fromLatin1(entry.sourceIdentity->sha256.toHex()) : QString()).arg(entry.sizeBytes);
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

bool assembleNewDoomWad(QVector<PackageStagedEntry>* entries, QString* error, PackagePlanWork& preparation)
{
	PackagePlanWork work(preparation.nestedControl(), error, QCoreApplication::translate("VibeStudioPackageStaging", "Ordering package WAD lumps"));
	if (!work.checkpoint()) { return false; }
	QStringList names; names.reserve(entries->size());
	for (const auto& entry : *entries) { if (!work.checkpoint()) { return false; } names << entry.virtualPath; }
	QVector<qsizetype> order; if (!orderNewPackageDoomLumps(names, &order, error, work.nestedControl())) { return false; }
	QVector<PackageStagedEntry> ordered; ordered.reserve(entries->size());
	for (const auto at : order) { if (!work.checkpoint()) { return false; } ordered << entries->at(at); }
	if (!work.finish()) { return false; }
	*entries = std::move(ordered); return true;
}

bool computePlan(const QVector<PackageStagedEntry>& baseEntries, const QVector<PackageStagedEntry>& baseDirectories, const QVector<PackageStageOperation>& operations, const QVector<PackageStageConflict>& baseConflicts, bool preserveSourceOrder, bool assembleDoomOrder, const QString& sourceWadMagic, QVector<PackageStageConflict>* conflicts, QVector<PackageStagedEntry>* result, PackagePlanWork& work, const PackageStagingPlanLimits& limits)
{
	PackagePlanBudget budget(limits, work);
	if (!budget.validate() || !budget.checkRecords(baseEntries.size() + baseDirectories.size() + baseConflicts.size())) { return false; }
	for (const auto& conflict : baseConflicts) {
		if (!work.checkpoint() || !budget.addRecord(PackagePlanBudget::conflictBytes(conflict))) { return false; }
	}
	*conflicts = baseConflicts;
	const auto addConflict = [&](QVector<PackageStageConflict>* output, const QString& id, const QString& path, const QString& message, bool blocking = true) {
		if (work.failed() || message.trimmed().isEmpty()) { return; }
		PackageStageConflict conflict{id.trimmed(), path.trimmed(), message.trimmed(), blocking};
		if (budget.addRecord(PackagePlanBudget::conflictBytes(conflict))) { output->append(std::move(conflict)); }
	};
	for (const auto& entry : baseEntries) {
		if (!work.checkpoint()) { return false; }
		if (!entry.unavailableReason.isEmpty()) {
			addConflict(conflicts, {}, entry.virtualPath, QCoreApplication::translate("VibeStudioPackageStaging",
				"Original content is unavailable for Undo: %1. %2").arg(entry.virtualPath, entry.unavailableReason), false);
		}
	}
	const auto resetRows = [&](const QVector<PackageStagedEntry>& entries) {
		budget.resetRows();
		for (const auto& conflict : *conflicts) {
			if (!work.checkpoint() || !budget.addRecord(PackagePlanBudget::conflictBytes(conflict))) { return false; }
		}
		for (const auto& entry : entries) {
			if (!work.checkpoint() || !budget.addRecord(PackagePlanBudget::entryBytes(entry))) { return false; }
		}
		return true;
	};
	QVector<PlanSlot> planSlots;
	planSlots.reserve(baseEntries.size() + baseDirectories.size());
	QHash<QString, QSet<int>> index;
	QHash<QString, int> parentUses;
	QHash<int, int> sourceIndex;
	const auto indexSlot = [&](int slot) {
		const auto& entry = planSlots.at(slot).entry;
		const auto key = entryKey(entry.virtualPath);
		if (!index.contains(key) && !budget.addKey(key)) { return false; }
		index[key].insert(slot);
		for (QString parent = packagePlanParent(key); !parent.isEmpty(); parent = packagePlanParent(parent)) {
			if (!work.checkpoint()) { return false; }
			if (!parentUses.contains(parent) && !budget.addKey(parent)) { return false; }
			++parentUses[parent];
		}
		return !work.cancelled();
	};
	const auto rebuildIndex = [&]() {
		index.clear(); parentUses.clear(); sourceIndex.clear(); budget.resetKeys();
		for (int slot = 0; slot < planSlots.size(); ++slot) {
			if (!work.checkpoint()) { return false; }
			if (!planSlots.at(slot).alive) { continue; }
			if (!indexSlot(slot)) { return false; }
			const int ordinal = planSlots.at(slot).entry.sourceOrdinal;
			if (ordinal >= 0) { sourceIndex.insert(ordinal, sourceIndex.contains(ordinal) ? -2 : slot); }
		}
		return true;
	};
	for (const auto& entry : baseEntries) { if (!work.checkpoint() || !budget.addRecord(PackagePlanBudget::entryBytes(entry))) { return false; } planSlots.push_back({entry, true}); }
	for (const auto& entry : baseDirectories) { if (!work.checkpoint() || !budget.addRecord(PackagePlanBudget::entryBytes(entry))) { return false; } planSlots.push_back({entry, true}); }
	if (!rebuildIndex()) { return false; }

	const auto liveIndex = [&index](const QString& virtualPath) {
		const auto found = index.constFind(entryKey(virtualPath));
		if (found == index.constEnd()) {
			return -1;
		}
		return found.value().isEmpty() ? -1 : *found.value().cbegin();
	};
	const auto removeIndex = [&](int slot) {
		auto found = index.find(entryKey(planSlots.at(slot).entry.virtualPath));
		if (found == index.end()) { return !work.cancelled(); }
		for (QString parent = packagePlanParent(entryKey(planSlots.at(slot).entry.virtualPath)); !parent.isEmpty(); parent = packagePlanParent(parent)) {
			if (!work.checkpoint()) { return false; }
			if (--parentUses[parent] == 0) { parentUses.remove(parent); budget.removeKey(parent); }
		}
		found.value().remove(slot);
		if (found.value().isEmpty()) { budget.removeKey(found.key()); index.erase(found); }
		return !work.cancelled();
	};

	const auto occupiedDirectory = [&](const QString& path) {
		const QString key = entryKey(path);
		if (parentUses.contains(key)) { return true; }
		for (const int slot : index.value(key)) { if (!work.checkpoint()) { return false; } if (planSlots.at(slot).entry.kind == PackageEntryKind::Directory) { return true; } }
		return false;
	};
	const auto fileParent = [&](const QString& path) {
		for (QString parent = packagePlanParent(entryKey(path)); !parent.isEmpty(); parent = packagePlanParent(parent)) {
			if (!work.checkpoint()) { return false; }
			for (const int slot : index.value(parent)) { if (!work.checkpoint()) { return false; } if (planSlots.at(slot).entry.kind == PackageEntryKind::File) { return true; } }
		}
		return false;
	};
	bool pendingWadAssembly = assembleDoomOrder;
	for (const PackageStageOperation& operation : operations) {
		if (!work.checkpoint()) { return false; }
		// Bind an assembled run before renaming/deleting its marker. Otherwise
		// a new MAP01 assembled from loose lumps would lose that membership as
		// soon as it was renamed to a non-conventional label such as INTRO.
		// Normalize once per addition batch, not once per group-deletion member.
		if (assembleDoomOrder && operation.type != PackageStageOperationType::Add && pendingWadAssembly) {
			QVector<PackageStagedEntry> candidate; for (const auto& slot : planSlots) { if (!work.checkpoint()) { return false; } if (slot.alive) { candidate << slot.entry; } }
			QString assemblyError;
			if (assembleNewDoomWad(&candidate, &assemblyError, work)) {
				planSlots.clear(); for (const auto& entry : candidate) { if (!work.checkpoint()) { return false; } planSlots.push_back({entry, true}); }
				if (!rebuildIndex()) { return false; }
			}
			if (work.cancelled()) { return false; }
			// Raw edits may repair an incomplete/ambiguous intermediate plan.
			// Only the final layout contributes a blocking diagnostic.
			pendingWadAssembly = false;
		}
		if (operation.type == PackageStageOperationType::Add) { pendingWadAssembly = assembleDoomOrder; }
		if (isPackageDirectoryOperation(operation.type)) {
			QVector<PackageStagedEntry> candidate;
			for (const auto& slot : planSlots) { if (!work.checkpoint()) { return false; } if (slot.alive) { candidate.append(slot.entry); } }
			QString error;
			if (preserveSourceOrder) { error = QCoreApplication::translate("VibeStudioPackageStaging", "WAD packages use flat lump names and cannot contain folders."); }
			else { applyPackageDirectoryOperation(&candidate, operation, &error, work.nestedControl(), limits); }
			if (work.cancelled()) { return false; }
			if (!error.isEmpty()) { addConflict(conflicts, operation.id, operation.virtualPath, error); continue; }
			if (!resetRows(candidate)) { return false; }
			planSlots.clear();
			for (const auto& entry : candidate) { if (!work.checkpoint()) { return false; } planSlots.push_back({entry, true}); }
			if (!rebuildIndex()) { return false; }
			continue;
		}
		const PackageVirtualPath normalized = normalizePackageVirtualPath(operation.virtualPath, false);
		if (!normalized.isSafe()) {
			addConflict(conflicts, operation.id, operation.virtualPath, QCoreApplication::translate("VibeStudioPackageStaging", "Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			continue;
		}

		if (occupiedDirectory(normalized.normalizedPath)
			|| ((operation.type == PackageStageOperationType::Add || operation.type == PackageStageOperationType::Replace) && fileParent(normalized.normalizedPath))) {
			addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "A file and folder would occupy the same package path. Use the folder controls for directory edits."));
			continue;
		}
		int existingIndex = liveIndex(normalized.normalizedPath);
		if (operation.sourceOrdinal < -1 || (operation.sourceOrdinal >= 0 && operation.type == PackageStageOperationType::Add)) {
			addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Invalid source occurrence selector."));
			continue;
		}
		if (operation.sourceOrdinal >= 0) {
			existingIndex = sourceIndex.value(operation.sourceOrdinal, -1);
			if (existingIndex < 0 || !planSlots.at(existingIndex).alive
				|| entryKey(planSlots.at(existingIndex).entry.virtualPath) != entryKey(normalized.normalizedPath)) {
				addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "The selected source occurrence is missing, ambiguous or has a different path. No entry was changed."));
				continue;
			}
		}
		if (operation.sourceOrdinal < 0 && index.value(entryKey(normalized.normalizedPath)).size() > 1
			&& !(operation.type == PackageStageOperationType::Add && operation.conflictResolution == PackageStageConflictResolution::Skip)) {
			addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "This path occurs more than once. Path-based editing cannot identify one occurrence; no entry was changed."));
			continue;
		}
		if (operation.type == PackageStageOperationType::Add || operation.type == PackageStageOperationType::Replace) {
			if (!operation.hasInlineBytes && !operation.sourceIdentity) {
				addConflict(conflicts, operation.id, normalized.normalizedPath, operation.sourceError);
				continue;
			}
			// Deliberately no isReadable() probe here: on Windows it costs an
			// ACL query per file, and the plan is recomputed far more often
			// than it is written. A permission failure surfaces as a write
			// error instead.
			PackageStagedEntry staged;
			staged.virtualPath = normalized.normalizedPath;
			staged.kind = PackageEntryKind::File;
			staged.sourceIdentity = operation.sourceIdentity;
			staged.sizeBytes = operation.sourceIdentity ? static_cast<quint64>(operation.sourceIdentity->size) : 0;
			staged.modifiedUtc = operation.sourceModifiedUtc.isValid() ? operation.sourceModifiedUtc : (operation.sourceIdentity ? operation.sourceIdentity->modifiedUtc : QDateTime());
			staged.operationId = operation.id;
			staged.sourceFilePath = operation.hasInlineBytes ? QString() : (operation.sourceIdentity ? operation.sourceIdentity->path : operation.sourceFilePath);
			staged.hasInlineBytes = operation.hasInlineBytes;
			staged.inlineBytes = operation.inlineBytes;
			const int defaultWadType = sourceWadMagic == QStringLiteral("WAD3") ? kWad3MiptexType
				: sourceWadMagic == QStringLiteral("WAD2") ? kWad2MiptexType : 0;
			staged.wadLumpType = static_cast<quint8>(operation.wadLumpType < 0 ? defaultWadType : operation.wadLumpType);
			staged.wadInsertBefore = operation.wadInsertBefore;
			staged.wadNamespace = operation.wadNamespace;
			if (operation.hasInlineBytes) {
				staged.sizeBytes = operation.inlineBytes.size();
				staged.modifiedUtc = QDateTime::fromSecsSinceEpoch(315532800, QTimeZone(0));
			}

			if (operation.type == PackageStageOperationType::Add) {
				if (existingIndex >= 0) {
					if (operation.conflictResolution == PackageStageConflictResolution::ReplaceExisting) {
						staged.source = QStringLiteral("staged-add-replace");
						// Replacing content must not move the entry: in a WAD
						// its position is what binds it to a map.
						staged.sourceOrdinal = planSlots[existingIndex].entry.sourceOrdinal;
						staged.sourceReaderIndex = planSlots[existingIndex].entry.sourceReaderIndex;
						if (operation.wadLumpType < 0) { staged.wadLumpType = planSlots[existingIndex].entry.wadLumpType; }
						if (staged.wadInsertBefore.isEmpty()) { staged.wadInsertBefore = planSlots[existingIndex].entry.wadInsertBefore; }
						if (staged.wadNamespace.isEmpty()) { staged.wadNamespace = planSlots[existingIndex].entry.wadNamespace; }
						if (!budget.replaceRecord(PackagePlanBudget::entryBytes(planSlots.at(existingIndex).entry), PackagePlanBudget::entryBytes(staged))) { return false; }
						planSlots[existingIndex].entry = staged;
					} else if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
						addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Skipped add because an entry already exists."), false);
					} else {
						addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Cannot add because an entry already exists. Use replace or replace-existing conflict resolution."));
					}
					continue;
				}
				staged.source = QStringLiteral("staged-add");
				if (!budget.addRecord(PackagePlanBudget::entryBytes(staged))) { return false; }
				if (planSlots.size() >= limits.maximumRecords) {
					// Reclaim dead slots without moving new WAD lumps before older ones.
					QVector<PlanSlot> compact; compact.reserve(planSlots.size());
					for (auto& slot : planSlots) { if (!work.checkpoint()) { return false; } if (slot.alive) { compact.append(std::move(slot)); } }
					planSlots = std::move(compact);
					if (!rebuildIndex()) { return false; }
				}
				planSlots.push_back({staged, true});
				if (!indexSlot(static_cast<int>(planSlots.size()) - 1)) { return false; }
				continue;
			}

			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Skipped replace because the entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Cannot replace because the entry is missing."));
				}
				continue;
			}
			staged.source = QStringLiteral("staged-replace");
			staged.sourceOrdinal = planSlots[existingIndex].entry.sourceOrdinal;
			staged.sourceReaderIndex = planSlots[existingIndex].entry.sourceReaderIndex;
			if (operation.wadLumpType < 0) { staged.wadLumpType = planSlots[existingIndex].entry.wadLumpType; }
			if (staged.wadInsertBefore.isEmpty()) { staged.wadInsertBefore = planSlots[existingIndex].entry.wadInsertBefore; }
			if (staged.wadNamespace.isEmpty()) { staged.wadNamespace = planSlots[existingIndex].entry.wadNamespace; }
			if (!budget.replaceRecord(PackagePlanBudget::entryBytes(planSlots.at(existingIndex).entry), PackagePlanBudget::entryBytes(staged))) { return false; }
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
					addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Skipped rename because the source entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Cannot rename because the source entry is missing."));
				}
				continue;
			}
			if (occupiedDirectory(target.normalizedPath) || fileParent(target.normalizedPath)) {
				addConflict(conflicts, operation.id, target.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "A file and folder would occupy the same destination path."));
				continue;
			}
			const int targetIndex = liveIndex(target.normalizedPath);
			const bool samePath = entryKey(target.normalizedPath) == entryKey(normalized.normalizedPath);
			if (!samePath && index.value(entryKey(target.normalizedPath)).size() > 1) {
				addConflict(conflicts, operation.id, target.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "The rename target occurs more than once. No entry was changed."));
				continue;
			}
			if (!samePath && targetIndex >= 0 && targetIndex != existingIndex) {
				if (operation.conflictResolution == PackageStageConflictResolution::ReplaceExisting) {
					planSlots[targetIndex].alive = false;
					if (!removeIndex(targetIndex)) { return false; }
					budget.removeRecord(PackagePlanBudget::entryBytes(planSlots.at(targetIndex).entry));
					planSlots[targetIndex].entry = {};
				} else if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, target.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Skipped rename because the target entry exists."), false);
					continue;
				} else {
					addConflict(conflicts, operation.id, target.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Cannot rename because the target entry already exists."));
					continue;
				}
			}
			if (!removeIndex(existingIndex)) { return false; }
			auto renamed = planSlots.at(existingIndex).entry;
			renamed.virtualPath = target.normalizedPath; renamed.source = QStringLiteral("staged-rename"); renamed.operationId = operation.id;
			if (!budget.replaceRecord(PackagePlanBudget::entryBytes(planSlots.at(existingIndex).entry), PackagePlanBudget::entryBytes(renamed))) { return false; }
			planSlots[existingIndex].entry = std::move(renamed);
			if (!indexSlot(existingIndex)) { return false; }
			continue;
		}

		if (operation.type == PackageStageOperationType::Delete) {
			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Skipped delete because the entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageStaging", "Cannot delete because the entry is missing."));
				}
				continue;
			}
			planSlots[existingIndex].alive = false;
			if (!removeIndex(existingIndex)) { return false; }
			budget.removeRecord(PackagePlanBudget::entryBytes(planSlots.at(existingIndex).entry));
			planSlots[existingIndex].entry = {};
		}
	}

	QVector<PackageStagedEntry> entries;
	if (!work.checkpoint()) { return false; }
	entries.reserve(planSlots.size());
	for (const PlanSlot& slot : planSlots) {
		if (!work.checkpoint()) { return false; }
		if (!slot.alive) { continue; }
		entries.push_back(slot.entry);
		if (!slot.entry.unavailableReason.isEmpty()) {
			addConflict(conflicts, slot.entry.operationId, slot.entry.virtualPath, QCoreApplication::translate("VibeStudioPackageStaging",
				"Content is unavailable: %1. Replace or delete this entry before exporting.").arg(slot.entry.virtualPath));
		}
	}
	// A WAD plan keeps the source lump order, because in a Doom WAD position
	// carries meaning that an alphabetical order would destroy; every other
	// format gets the stable total order over paths.
	if (preserveSourceOrder) {
		if (!sortPackagePlan(&entries, stagedEntrySourceLess, work)) { return false; }
		// Keep new native textures inside their namespace without changing the
		// source ordinals that identify repeated map lumps. An edited/deleted
		// marker blocks publication instead of silently relocating the texture.
		QHash<QString, int> names; QSet<QString> anchors; QStringList namespaces;
		for (const auto& entry : entries) {
			if (!work.checkpoint()) { return false; }
			const auto key = entryKey(entry.virtualPath); ++names[key]; bool opens = false;
			const auto marked = doomNamespaceMarker(entry.virtualPath, &opens);
			if (marked.isEmpty()) { continue; }
			if (opens) { namespaces.append(marked); }
			else if (const auto at = namespaces.lastIndexOf(marked); at >= 0) {
				if (at == namespaces.size() - 1) { anchors.insert(key); }
				namespaces.removeAt(at);
			}
		}
		QHash<QString, QVector<PackageStagedEntry>> insertions; QVector<PackageStagedEntry> remaining;
		for (const auto& entry : entries) {
			if (!work.checkpoint()) { return false; }
			if (entry.wadInsertBefore.isEmpty()) { remaining.append(entry); continue; }
			const auto key = entryKey(entry.wadInsertBefore);
			if (names.value(key) != 1 || !anchors.contains(key) || !doomNamespaceMarker(entry.virtualPath).isEmpty()) {
				addConflict(conflicts, entry.operationId, entry.virtualPath, QCoreApplication::translate("VibeStudioPackageStaging", "The texture namespace end marker is missing or ambiguous: %1").arg(entry.wadInsertBefore));
				remaining.append(entry); continue;
			}
			insertions[key].append(entry);
		}
		entries.clear();
		for (const auto& entry : remaining) {
			if (!work.checkpoint()) { return false; }
			for (const auto& inserted : insertions.value(entryKey(entry.virtualPath))) {
				if (!work.checkpoint()) { return false; }
				entries.append(inserted);
			}
			entries.append(entry);
		}
		namespaces.clear();
		for (const auto& entry : entries) {
			if (!work.checkpoint()) { return false; }
			bool opens = false; const auto marked = doomNamespaceMarker(entry.virtualPath, &opens);
			if (!marked.isEmpty()) {
				if (opens) { namespaces.append(marked); }
				else if (const auto at = namespaces.lastIndexOf(marked); at >= 0) { namespaces.removeAt(at); }
			}
			const bool wrongNamespace = entry.wadNamespace == QStringLiteral("global") ? !namespaces.isEmpty() : namespaces.isEmpty() || namespaces.last() != entry.wadNamespace;
			if (!entry.wadNamespace.isEmpty() && (!marked.isEmpty() || wrongNamespace)) {
				addConflict(conflicts, entry.operationId, entry.virtualPath, QCoreApplication::translate("VibeStudioPackageStaging", "The staged texture is outside its required WAD namespace: %1").arg(entry.wadNamespace));
			}
		}
		if (assembleDoomOrder) {
			QString assemblyError;
			if (!assembleNewDoomWad(&entries, &assemblyError, work)) {
				if (work.cancelled()) { return false; }
				addConflict(conflicts, {}, {}, assemblyError);
			}
		}
	} else {
		if (!sortPackagePlan(&entries, stagedEntryLess, work)) { return false; }
	}
	if (!work.finish()) { return false; }
	*result = std::move(entries);
	return true;
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
bool readWadSourceMetadata(const QString& path, QString* magicOut, QVector<PackageWadLumpLocation>* lumps,
	const PackageFileIdentityPtr& identity, const PackageReadControl& control, QString* error,
	const std::function<bool(const PackageStagingMetadataUsage&)>& charge)
{
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageStaging", "Reading package WAD directory"));
	if (!work.checkpoint()) { return false; }
	const auto failed = [&] {
		if (!work.cancelled() && error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "The WAD source directory could not be read from its original snapshot."); }
		return false;
	};
	std::unique_ptr<QIODevice> file;
	Q_UNUSED(path);
	if (!identity) { return failed(); }
	file = std::make_unique<PackageContentDevice>(identity, work.nestedControl());
	if (!file->open(QIODevice::ReadOnly)) { return failed(); }
	const QByteArray header = file->read(kWadHeaderSize);
	if (header.size() != kWadHeaderSize) { return failed(); }
	const QString magic = QString::fromLatin1(header.constData(), 4);
	if (!wadMagicIsSupported(magic)) { return failed(); }
	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const int recordSize = textureWad ? kTextureWadRecordSize : kDoomWadRecordSize;
	const int nameOffset = textureWad ? 16 : 8;
	const int nameLimit = textureWad ? kTextureWadNameLimit : kDoomWadNameLimit;
	const qint64 lumpCount = readSignedLe32(header, 4);
	const qint64 directoryOffset = readSignedLe32(header, 8);
	const qint64 fileSize = file->size();
	if (lumpCount < 0 || directoryOffset < kWadHeaderSize
		|| lumpCount > (fileSize - kWadHeaderSize) / recordSize
		|| directoryOffset + (lumpCount * recordSize) > fileSize) { return failed(); }
	// A generic reader is not necessarily an admitted disk index. Check its
	// wire count and the destination's retained-record allowance before reserve.
	PackageIndexUsage wire;
	if (!admitPackageIndex(&wire, {}, qsizetype(lumpCount), lumpCount * recordSize, error)
		|| !charge({qsizetype(lumpCount), magic.size() * 2})) { return false; }
	if (!file->seek(directoryOffset)) { return failed(); }
	lumps->reserve(qsizetype(lumpCount));
	for (qint64 index = 0; index < lumpCount; ++index) {
		if (!work.checkpoint()) { return false; }
		const QByteArray record = file->read(recordSize);
		if (record.size() != recordSize) { return failed(); }
		qsizetype nameLength = 0;
		while (nameLength < nameLimit && record[nameOffset + nameLength] != '\0') { ++nameLength; }
		PackageWadLumpLocation lump;
		lump.name = QString::fromLatin1(record.constData() + nameOffset, nameLength).trimmed();
		if (lump.name.isEmpty()) { lump.name = QStringLiteral("lump-%1").arg(index, 4, 10, QLatin1Char('0')); }
		if (!charge({0, lump.name.size() * 2})) { return false; }
		lump.dataOffset = readSignedLe32(record, 0);
		lump.diskSizeBytes = readSignedLe32(record, 4);
		lump.sizeBytes = textureWad ? readSignedLe32(record, 8) : lump.diskSizeBytes;
		lump.type = textureWad ? static_cast<quint8>(record[12]) : 0;
		lump.compression = textureWad ? static_cast<quint8>(record[13]) : 0;
		lumps->push_back(std::move(lump));
	}
	if (!work.finish()) { return false; }
	*magicOut = magic;
	return true;
}

// Bind a metadata-only filesystem adapter to the captured physical entries.
// Explicit ordinals win; otherwise only one unconsumed name/size/location match
// is safe. Each lookup index is admitted before growth and never reads payloads.
class PackageBaseLookup final {
public:
	PackageBaseLookup(const PackageArchive& reader, const PackageStagingPlanLimits& limits, PackagePlanWork& work)
		: m_entries(reader.entries()), m_budget(limits, work), m_work(work) {}
	bool prepare()
	{
		qint64 largest = -1;
		for (const auto& entry : m_entries) {
			if (!m_work.checkpoint()) { return false; }
			largest = qMax(largest, entry.sourceOrdinal);
		}
		if (largest >= PackageIndexLimits::entryCeiling) {
			return m_work.refuse(QCoreApplication::translate("VibeStudioPackageStaging", "The source package occurrence is outside the supported range."));
		}
		m_ordinals.fill(-1, qsizetype(largest + 1)); m_used.fill(false, m_entries.size());
		for (qsizetype index = 0; index < m_entries.size(); ++index) {
			if (!m_work.checkpoint()) { return false; }
			const auto& entry = m_entries.at(index);
			if (entry.kind != PackageEntryKind::File) { continue; }
			const auto name = key(entry);
			if (!add(m_names, name, index)) { return false; }
			if (entry.dataOffset >= 0 && !add(m_locations, name + QLatin1Char('|') + QString::number(entry.dataOffset), index)) { return false; }
			if (entry.sourceOrdinal >= 0) {
				auto& slot = m_ordinals[qsizetype(entry.sourceOrdinal)]; slot = slot == -1 ? index : -2;
			}
		}
		return true;
	}
	qsizetype take(const PackageEntry& wanted)
	{
		const auto matches = [&](qsizetype index) {
			const auto& entry = m_entries.at(index);
			return !m_used.at(index) && entryKey(entry.virtualPath) == entryKey(wanted.virtualPath)
				&& entry.sizeBytes == wanted.sizeBytes && (wanted.dataOffset < 0 || entry.dataOffset == wanted.dataOffset)
				&& (!wanted.hasCrc32 || (entry.hasCrc32 && entry.crc32 == wanted.crc32))
				&& (wanted.wadLumpType < 0 || wanted.wadLumpType == entry.wadLumpType);
		};
		qsizetype found = -1;
		if (wanted.sourceOrdinal >= 0) {
			if (wanted.sourceOrdinal < m_ordinals.size()) {
				const auto index = m_ordinals.at(qsizetype(wanted.sourceOrdinal));
				if (index >= 0 && matches(index)) { found = index; }
			}
		} else {
			const auto name = key(wanted);
			const auto& candidates = wanted.dataOffset >= 0 ? m_locations : m_names;
			const auto at = candidates.constFind(wanted.dataOffset >= 0 ? name + QLatin1Char('|') + QString::number(wanted.dataOffset) : name);
			if (at != candidates.cend()) {
				for (const auto index : *at) {
					if (!m_work.checkpoint()) { return -1; }
					if (!matches(index)) { continue; }
					if (found >= 0) { return -1; }
					found = index;
				}
			}
		}
		if (found >= 0) { m_used[found] = true; }
		return found;
	}
	const PackageEntry& entry(qsizetype index) const { return m_entries.at(index); }
private:
	static QString key(const PackageEntry& entry) { return entryKey(entry.virtualPath) + QLatin1Char('|') + QString::number(entry.sizeBytes); }
	bool add(QHash<QString, QVector<qsizetype>>& buckets, const QString& key, qsizetype index)
	{
		if (!buckets.contains(key) && !m_budget.addKey(key)) { return false; }
		buckets[key].append(index); return true;
	}
	QVector<PackageEntry> m_entries;
	QVector<qsizetype> m_ordinals;
	QVector<bool> m_used;
	QHash<QString, QVector<qsizetype>> m_names, m_locations;
	PackagePlanBudget m_budget;
	PackagePlanWork& m_work;
};

bool virtualWadMetadata(const QString& magic, const QVector<PackageEntry>& entries, QVector<PackageWadLumpLocation>* lumps,
	QVector<int>* entrySlots, PackagePlanWork& work, const std::function<bool(const PackageStagingMetadataUsage&)>& charge)
{
	const auto invalid = [&] { return work.refuse(QCoreApplication::translate("VibeStudioPackageStaging", "The source snapshot has missing or inconsistent WAD directory metadata.")); };
	if (!wadMagicIsSupported(magic)) { return invalid(); }
	qsizetype count = 0;
	for (const auto& entry : entries) { if (!work.checkpoint()) { return false; } if (entry.kind == PackageEntryKind::File) { ++count; } }
	if (!charge({count, magic.size() * 2})) { return false; }
	lumps->resize(count); entrySlots->fill(-1, entries.size()); QVector<bool> used(count, false);
	qsizetype physical = 0;
	const bool texture = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	for (qsizetype index = 0; index < entries.size(); ++index) {
		if (!work.checkpoint()) { return false; }
		const auto& entry = entries.at(index);
		if (entry.kind != PackageEntryKind::File) { continue; }
		const auto ordinal = entry.sourceOrdinal < 0 ? physical : entry.sourceOrdinal; ++physical;
		if (ordinal < 0 || ordinal >= count || used.at(qsizetype(ordinal))
			|| (texture && (entry.wadLumpType < 0 || entry.wadLumpType > 255))
			|| entry.sizeBytes > quint64(std::numeric_limits<qint64>::max())
			|| entry.compressedSizeBytes > quint64(std::numeric_limits<qint64>::max())) { return invalid(); }
		if (!charge({0, PackagePlanBudget::textBytes(entry.virtualPath)})) { return false; }
		auto& lump = (*lumps)[qsizetype(ordinal)];
		lump.name = QString(entry.virtualPath.constData(), entry.virtualPath.size());
		// A decoded provider has no wire offset. Zero is the portable draft's
		// neutral location; all reads remain bound to the retained provider.
		lump.dataOffset = qMax<qint64>(0, entry.dataOffset);
		lump.sizeBytes = qint64(entry.sizeBytes); lump.diskSizeBytes = qint64(entry.compressedSizeBytes ? entry.compressedSizeBytes : entry.sizeBytes);
		lump.type = texture ? quint8(entry.wadLumpType) : 0;
		used[qsizetype(ordinal)] = true; (*entrySlots)[index] = int(ordinal);
	}
	return !work.cancelled();
}

} // namespace

bool PackageWriteReport::succeeded() const
{
	return blockedMessages.isEmpty() && !cancelled && !outputPath.isEmpty() && bytesWritten > 0 && (dryRun || outputCommitted);
}

bool PackageStagingModel::createEmpty(PackageArchiveFormat format, const QString& wadMagic, QString* error)
{
	if (error) { error->clear(); }
	const QString magic = wadMagic.isEmpty() ? QStringLiteral("PWAD") : wadMagic.trimmed().toUpper();
	if (!(format == PackageArchiveFormat::Pak || format == PackageArchiveFormat::Zip || format == PackageArchiveFormat::Pk3 || format == PackageArchiveFormat::Wad)
		|| (format == PackageArchiveFormat::Wad ? !wadMagicIsSupported(magic) : !wadMagic.isEmpty())) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Choose PAK, ZIP, PK3, or a supported WAD format for the new package."); }
		return false;
	}
	PackageStagingModel candidate(m_contentLimits, m_metadataLimits, m_planLimits, m_viewLimits);
	candidate.m_loaded = true; candidate.m_sourceFormat = format;
	if (format == PackageArchiveFormat::Wad) { candidate.m_sourceWadMagic = magic; }
	PackageStagingContentUsage content; PackageStagingMetadataUsage metadata;
	if (!candidate.contentUsage(&content, error) || !candidate.metadataUsage(&metadata, error) || !candidate.admitView(error)) { return false; }
	// Even an empty new document needs Save/Discard/Cancel before closing.
	candidate.m_revision = candidate.m_revisionSerial = 1;
	*this = std::move(candidate); return true;
}

bool PackageStagingModel::loadBaseArchive(const PackageArchiveReader& archive, QString* error, const PackageReadControl& control)
{
	QString localError; if (!error) { error = &localError; } error->clear();
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageStaging", "Opening editable package"));
	const auto nested = work.nestedControl();
	PackageStagingModel candidate(m_contentLimits, m_metadataLimits, m_planLimits, m_viewLimits);
	PackageStagingContentUsage usage; PackageStagingMetadataUsage metadata;
	if (!work.checkpoint() || !candidate.contentUsage(&usage, error, nested) || !candidate.metadataUsage(&metadata, error, nested)
		|| !candidate.preparePlan(error, nested) || !validPackageIndexLimits(m_viewLimits, error)
		|| !candidate.loadBaseArchiveUnchecked(archive, error, nested)
		|| !candidate.contentUsage(&usage, error, nested) || !candidate.metadataUsage(&metadata, error, nested)
		|| !candidate.admitView(error, nested) || !work.finish()) { return false; }
	// Consumers prepare their own caches with their cancellation/progress callbacks.
	candidate.invalidatePlan();
	*this = std::move(candidate);
	return true;
}

bool PackageStagingModel::loadBaseArchiveUnchecked(const PackageArchiveReader& archive, QString* error, const PackageReadControl& control)
{
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageStaging", "Reading package base metadata"));
	if (!work.checkpoint()) { return false; }
	PackageStagingMetadataUsage retained;
	const auto charge = [&](const PackageStagingMetadataUsage& addition) {
		if (!work.checkpoint() || !metadataFits(retained, addition, error)) { return false; }
		retained.records += addition.records; retained.metadataBytes += addition.metadataBytes;
		return true;
	};
	PackagePlanBudget budget(m_planLimits, work);
	const auto appendWarning = [&](const PackageLoadWarning& warning) {
		PackageStageConflict conflict{{}, warning.virtualPath, warning.message, warning.blocksSaving};
		const auto bytes = PackagePlanBudget::conflictBytes(conflict);
		if (!budget.addRecord(bytes) || !charge({1, bytes})) { return false; }
		m_baseConflicts.append(std::move(conflict)); return true;
	};
	if (const auto* source = dynamic_cast<const PackageArchive*>(&archive); source && source->snapshotReader()
		&& (dynamic_cast<const PackageArchive*>(source->snapshotReader().get()) || dynamic_cast<const PackageStagingArchive*>(source->snapshotReader().get()))) {
		if (!loadBaseArchive(*source->snapshotReader(), error, work.nestedControl())
			|| !metadataUsage(&retained, error, work.nestedControl())) { return false; }
		for (const auto& warning : source->warnings()) { if (!appendWarning(warning)) { return false; } }
		invalidatePlan();
		return work.finish();
	}
	if (const auto* staged = dynamic_cast<const PackageStagingArchive*>(&archive)) {
		const auto& source = staged->m_snapshot;
		if (!source.preparePlan(error, work.nestedControl())) { return false; }
		if (!staged->isOpen() || !source.summary().canSave) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Resolve staging conflicts before reading the planned package.");
			return false;
		}
		const auto limits = m_contentLimits; const auto metadataLimits = m_metadataLimits; const auto planLimits = m_planLimits; const auto viewLimits = m_viewLimits;
		*this = source;
		m_contentLimits = limits; m_metadataLimits = metadataLimits; m_planLimits = planLimits; m_viewLimits = viewLimits;
		m_baseEntries.clear(); m_baseDirectories.clear(); m_operations.clear(); m_baseConflicts.clear();
		m_draftPath.clear(); m_draftIdentity.reset(); resetHistory(); invalidatePlan();
		if (!metadataUsage(&retained, error, work.nestedControl())) { return false; }
		if (!freezeInputProtections(error, work.nestedControl(), {}, &source)
			|| !metadataUsage(&retained, error, work.nestedControl())) { return false; }
		const auto entries = source.plannedEntries();
		if (!budget.checkRecords(entries.size())) { return false; }
		for (const auto& entry : entries) {
			if (!budget.addRecord(PackagePlanBudget::entryBytes(entry)) || !charge(entryMetadata(entry))) { return false; }
			(entry.kind == PackageEntryKind::Directory ? m_baseDirectories : m_baseEntries).append(entry);
		}
		invalidatePlan();
		return work.finish();
	}
	if (!archive.isOpen()) {
		*error = QCoreApplication::translate("VibeStudioPackageStaging", "No package is open."); return false;
	}
	clear();
	const auto* source = dynamic_cast<const PackageArchive*>(&archive);
	if (source) { m_baseReader = std::make_shared<PackageArchive>(*source); }
	m_sourcePath = archive.sourcePath(); m_sourceFormat = archive.format(); m_loaded = true;
	PackageStagingContentUsage content;
	if (!metadataUsage(&retained, error, work.nestedControl()) || !contentUsage(&content, error, work.nestedControl())) { return false; }
	m_sourcePath = QString(m_sourcePath.constData(), m_sourcePath.size());
	PackageInputProtectionSet sourceInputs(m_metadataLimits.maximumRecords - retained.records,
		m_metadataLimits.maximumMetadataBytes - retained.metadataBytes, error, work.nestedControl());
	if (!archive.visitProtectedInputPaths([&](const QString& path) { return sourceInputs.add(path); }, error, work.nestedControl())
		|| !sourceInputs.finish() || !charge({sourceInputs.paths().size(), sourceInputs.metadataBytes()})) {
		if (error->isEmpty()) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Unable to retain all package source protections."); }
		return false;
	}
	m_protectedInputPaths = sourceInputs.paths();
	if (!work.checkpoint()) { return false; }
	const auto entries = archive.entries();
	PackageIndexUsage adapter;
	if (!admitPackageIndex(&adapter, {}, entries.size(), 0, error)) { return false; }
	// A borrowed adapter cannot retain arbitrary virtual callbacks. Capture its
	// declared filesystem base now, within the destination's remaining limits.
	// A missing path stays unavailable even if a different file appears later.
	if (!source && !m_sourcePath.isEmpty() && QFileInfo::exists(m_sourcePath)) {
		PackageIndexLimits limits;
		limits.maximumEntries = qMin(limits.maximumEntries, m_metadataLimits.maximumRecords - retained.records);
		limits.maximumMetadataBytes = qMin(limits.maximumMetadataBytes, m_metadataLimits.maximumMetadataBytes - retained.metadataBytes);
		limits.maximumFingerprintBytes = qMin(limits.maximumFingerprintBytes, m_contentLimits.maximumFingerprintBytes);
		auto captured = std::make_shared<PackageArchive>();
		if (!captured->load(m_sourcePath, error, work.nestedControl(), limits)) { return false; }
		m_baseReader = std::move(captured); m_metadataUsageValid = false; m_contentUsageValid = false;
		if (!metadataUsage(&retained, error, work.nestedControl()) || !contentUsage(&content, error, work.nestedControl())) { return false; }
	}
	if (m_baseReader) { for (const auto& warning : m_baseReader->warnings()) { if (!appendWarning(warning)) { return false; } } }
	const bool wadSource = m_sourceFormat == PackageArchiveFormat::Wad;
	QVector<int> virtualWadSlots;
	if (wadSource) {
		if (m_baseReader && m_baseReader->fileIdentity()) {
			if (m_baseReader->format() != PackageArchiveFormat::Wad) {
				return work.refuse(QCoreApplication::translate("VibeStudioPackageStaging", "The source snapshot has missing or inconsistent WAD directory metadata."));
			}
			if (!readWadSourceMetadata(m_sourcePath, &m_sourceWadMagic, &m_sourceWadLumps,
				m_baseReader->fileIdentity(), work.nestedControl(), error, charge)) { return false; }
			if (!archive.wadMagic().isEmpty() && archive.wadMagic() != m_sourceWadMagic) {
				return work.refuse(QCoreApplication::translate("VibeStudioPackageStaging", "The source snapshot has missing or inconsistent WAD directory metadata."));
			}
		} else {
			const auto magic = archive.wadMagic();
			if (!virtualWadMetadata(magic, entries, &m_sourceWadLumps, &virtualWadSlots, work, charge)) { return false; }
			m_sourceWadMagic = QString(magic.constData(), magic.size());
		}
	}
	std::unique_ptr<PackageBaseLookup> capturedLookup;
	if (!source && m_baseReader) {
		capturedLookup = std::make_unique<PackageBaseLookup>(*m_baseReader, m_planLimits, work);
		if (!capturedLookup->prepare()) { return false; }
	}
	for (qsizetype readerOrdinal = 0; readerOrdinal < entries.size(); ++readerOrdinal) {
		if (!work.checkpoint()) { return false; }
		const auto& entry = entries.at(readerOrdinal);
		const auto inputText = PackagePlanBudget::textBytes(entry.virtualPath)
			+ (entry.readable ? 0 : PackagePlanBudget::textBytes(entry.note));
		if (!budget.checkText(0, inputText) || !metadataFits(retained, {0, inputText}, error)) { return false; }
		PackageStagedEntry staged;
		if (entry.kind == PackageEntryKind::Directory) {
			if (entry.storageMethod == QStringLiteral("synthetic")) { continue; }
			QString path = entry.virtualPath;
			while (path.endsWith('/')) { path.chop(1); }
			if (path.isEmpty()) { continue; }
			staged.virtualPath = path; staged.kind = PackageEntryKind::Directory;
			staged.modifiedUtc = entry.modifiedUtc; staged.source = QStringLiteral("base-directory"); staged.baseVirtualPath = entry.virtualPath;
		} else if (entry.kind == PackageEntryKind::File) {
			if (!entry.readable) {
				staged.unavailableReason = entry.note.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "Base entry is not readable and cannot be preserved by the writer.") : entry.note;
			}
			staged.virtualPath = entry.virtualPath; staged.kind = PackageEntryKind::File; staged.sizeBytes = entry.sizeBytes;
			staged.modifiedUtc = entry.modifiedUtc; staged.source = QStringLiteral("base"); staged.baseVirtualPath = entry.virtualPath;
			if (entry.sourceOrdinal > std::numeric_limits<int>::max()) {
				*error = QCoreApplication::translate("VibeStudioPackageStaging", "The source package occurrence is outside the supported range."); return false;
			}
			staged.sourceOrdinal = entry.sourceOrdinal >= 0 ? int(entry.sourceOrdinal) : int(readerOrdinal);
			staged.sourceMetadataIndex = readerOrdinal;
			staged.sourceReaderIndex = source ? readerOrdinal : capturedLookup ? capturedLookup->take(entry) : -1;
			if (!work.checkpoint()) { return false; }
			const PackageEntry* captured = capturedLookup && staged.sourceReaderIndex >= 0 ? &capturedLookup->entry(staged.sourceReaderIndex) : nullptr;
			if (!source && staged.unavailableReason.isEmpty()) {
				if (!captured) { staged.unavailableReason = QCoreApplication::translate("VibeStudioPackageStaging", "The base entry has no matching retained source. Replace it or reopen an owned package snapshot."); }
				else if (!captured->readable) { staged.unavailableReason = captured->note.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "Base entry is not readable and cannot be preserved by the writer.") : captured->note; }
			}
			if (wadSource) {
				const int slot = !virtualWadSlots.isEmpty() ? virtualWadSlots.at(readerOrdinal)
					: captured ? int(captured->sourceOrdinal) : source ? staged.sourceOrdinal : -1;
				if (slot >= 0 && slot < m_sourceWadLumps.size()) {
					staged.sourceOrdinal = slot; staged.wadLumpType = m_sourceWadLumps.at(slot).type;
				} else if (!source && !staged.unavailableReason.isEmpty()) {
					// Unmatched metadata has no physical position; keep its identity
					// distinct until the user removes or replaces that unavailable row.
					staged.sourceOrdinal = int(m_sourceWadLumps.size() + readerOrdinal);
				} else { return work.refuse(QCoreApplication::translate("VibeStudioPackageStaging", "The source snapshot has missing or inconsistent WAD directory metadata.")); }
			}
		} else { continue; }
		if (!budget.addRecord(PackagePlanBudget::entryBytes(staged)) || !charge(entryMetadata(staged))) { return false; }
		if (!source) { staged.virtualPath.detach(); staged.baseVirtualPath.detach(); staged.unavailableReason.detach(); }
		(staged.kind == PackageEntryKind::Directory ? m_baseDirectories : m_baseEntries).append(std::move(staged));
	}

	PackagePlanWork sorting(work.nestedControl(), error, QCoreApplication::translate("VibeStudioPackageStaging", "Ordering package base entries"));
	if (!sortPackagePlan(&m_baseEntries, wadSource ? stagedEntrySourceLess : stagedEntryLess, sorting)
		|| !sortPackagePlan(&m_baseDirectories, stagedEntryLess, sorting) || !sorting.finish() || !work.finish()) { return false; }
	invalidatePlan();
	return true;
}

bool PackageStagingModel::loadBaseArchiveSubset(const PackageArchiveReader& archive, const QStringList& virtualPaths, QString* error, const PackageReadControl& control)
{
	PackageSelectionRequest request; request.entries = virtualPaths;
	const auto selection = selectPackageFiles(archive, request, control);
	if (!selection.succeeded()) { if (error) { *error = selection.errors.join(QLatin1Char('\n')); } return false; }
	return loadBaseArchiveSubsetAt(archive, selection.entryIndexes, error, nullptr, control);
}

void PackageStagingModel::clear()
{
	m_sourcePath.clear();
	m_protectedInputPaths.clear();
	m_sourceFormat = PackageArchiveFormat::Unknown;
	m_sourceWadMagic.clear();
	m_sourceWadLumps.clear();
	m_loaded = false;
	m_baseEntries.clear();
	m_baseDirectories.clear();
	m_operations.clear();
	m_baseConflicts.clear();
	m_operationSerial = 0;
	m_draftPath.clear(); m_draftIdentity.reset();
	resetHistory();
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
	m_metadataUsageValid = false; m_metadataUsageExact = false;
	m_contentUsageValid = false; m_contentUsageExact = false;
	m_planValid = false; m_summary = {}; m_beforeComposition.clear(); m_afterComposition.clear();
	m_planError.clear();
	m_planEntries.clear();
	m_planConflicts.clear();
}

bool PackageStagingModel::preparePlan(QString* error, const PackageReadControl& control) const
{
	if (error) { error->clear(); }
	QString preparationError;
	PackagePlanWork work(control, &preparationError, QCoreApplication::translate("VibeStudioPackageStaging", "Preparing package plan"));
	const auto fail = [&](PackagePlanWork& phase) {
		const bool cancelled = phase.cancelled();
		if (phase.failed() && !cancelled) {
			m_planError = preparationError; m_planConflicts = {{QString(), QString(), preparationError, true}};
			m_summary = {}; m_summary.sourcePath = m_sourcePath; m_summary.sourceFormat = m_sourceFormat;
			m_summary.baseFileCount = static_cast<int>(m_baseEntries.size()); m_summary.baseDirectoryCount = static_cast<int>(m_baseDirectories.size());
			m_summary.operationCount = static_cast<int>(m_operations.size());
			m_summary.conflictCount = m_summary.blockingCount = 1; m_summary.blockedMessages = {preparationError};
		}
		if (error) { *error = preparationError; }
		return false;
	};
	if (work.cancelled()) { return fail(work); }
	if (!m_planError.isEmpty()) { if (error) { *error = m_planError; } return false; }
	if (m_planValid) { return true; }
	if (!work.checkpoint()) { return fail(work); }
	const bool newDoomWad = m_sourceFormat == PackageArchiveFormat::Wad && m_sourceWadLumps.isEmpty()
		&& (m_sourceWadMagic == QStringLiteral("PWAD") || m_sourceWadMagic == QStringLiteral("IWAD"));
	QVector<PackageStagedEntry> entries; QVector<PackageStageConflict> conflicts;
	if (!computePlan(m_baseEntries, m_baseDirectories, m_operations, m_baseConflicts,
		m_sourceFormat == PackageArchiveFormat::Wad, newDoomWad, m_sourceWadMagic, &conflicts, &entries, work, m_planLimits)) { return fail(work); }
	PackagePlanWork stats(control, &preparationError, QCoreApplication::translate("VibeStudioPackageStaging", "Preparing package summaries"));
	PackageStagingSummary result; result.sourcePath = m_sourcePath; result.sourceFormat = m_sourceFormat;
	result.baseFileCount = static_cast<int>(m_baseEntries.size()); result.baseDirectoryCount = static_cast<int>(m_baseDirectories.size());
	result.operationCount = static_cast<int>(m_operations.size());
	QVector<PackageCompositionBucket> before, after;
	if (!compositionBuckets(m_baseEntries, &before, &result.beforeBytes, &result.beforeSizeOverflow, stats, m_planLimits)
		|| !compositionBuckets(entries, &after, &result.afterBytes, &result.afterSizeOverflow, stats, m_planLimits)) { return fail(stats); }
	if (result.afterSizeOverflow) {
		PackagePlanBudget budget(m_planLimits, stats);
		if (!budget.checkRecords(entries.size() + conflicts.size() + 1)) { return fail(stats); }
		for (const auto& entry : entries) {
			if (!stats.checkpoint() || !budget.addRecord(PackagePlanBudget::entryBytes(entry))) { return fail(stats); }
		}
		for (const auto& conflict : conflicts) {
			if (!stats.checkpoint() || !budget.addRecord(PackagePlanBudget::conflictBytes(conflict))) { return fail(stats); }
		}
		PackageStageConflict overflowConflict{{}, {}, QCoreApplication::translate("VibeStudioPackageStaging", "The planned package's total file size exceeds the supported range."), true};
		if (!budget.addRecord(PackagePlanBudget::conflictBytes(overflowConflict))) { return fail(stats); }
		conflicts.append(std::move(overflowConflict));
	}
	for (const PackageStagedEntry& entry : entries) {
		if (!stats.checkpoint()) { return fail(stats); }
		if (entry.kind == PackageEntryKind::Directory) {
			++result.stagedDirectoryCount;
		} else {
			++result.stagedFileCount;
		}
	}
	for (const PackageStageOperation& operation : m_operations) {
		if (!stats.checkpoint()) { return fail(stats); }
		switch (operation.type) {
		case PackageStageOperationType::Add:
		case PackageStageOperationType::CreateDirectory:
			++result.addedCount;
			break;
		case PackageStageOperationType::Replace:
			++result.replacedCount;
			break;
		case PackageStageOperationType::Rename:
		case PackageStageOperationType::RenameDirectory:
			++result.renamedCount;
			break;
		case PackageStageOperationType::Delete:
		case PackageStageOperationType::DeleteDirectory:
			++result.deletedCount;
			break;
		}
	}
	result.conflictCount = static_cast<int>(conflicts.size());
	PackagePlanBudget messagesBudget(m_planLimits, stats);
	for (const PackageStageConflict& conflict : conflicts) {
		if (!stats.checkpoint()) { return fail(stats); }
		if (conflict.blocking) {
			++result.blockingCount;
			const QString message = conflict.virtualPath.isEmpty() ? conflict.message : QStringLiteral("%1: %2").arg(conflict.virtualPath, conflict.message);
			if (!messagesBudget.addRecord(PackagePlanBudget::textBytes(message))) { return fail(stats); }
			result.blockedMessages.push_back(message);
		}
	}
	result.canSave = m_loaded && result.blockingCount == 0;
	if (!stats.finish()) { return fail(stats); }
	result.totalsAvailable = true;
	m_summary = std::move(result); m_beforeComposition = std::move(before); m_afterComposition = std::move(after);
	m_planEntries = std::move(entries); m_planConflicts = std::move(conflicts); m_planValid = true;
	return true;
}

void PackageStagingModel::ensurePlan() const
{
	preparePlan();
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

bool PackageStagingModel::entryBytes(const PackageStagedEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const
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
	if (!entry.unavailableReason.isEmpty()) { if (error) { *error = entry.unavailableReason; } return false; }
	if (entry.hasInlineBytes) { if (out) { *out = maxBytes < 0 ? entry.inlineBytes : entry.inlineBytes.left(maxBytes); } return true; }
	if (!entry.sourceFilePath.isEmpty()) {
		return readSourceFile(entry, out, error, maxBytes);
	}
	if (entry.baseVirtualPath.isEmpty()) {
		return true;
	}
	if (m_baseReader && entry.sourceReaderIndex >= 0) {
		return m_baseReader->readEntryAt(entry.sourceReaderIndex, out, error, maxBytes);
	}
	if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "The base entry has no retained source. Replace it or reopen the original package."); }
	return false;
}

PackageStagingSummary PackageStagingModel::summary() const
{
	ensurePlan();
	return m_summary;
}

bool PackageStagingModel::verifySources(QString* error, const PackageReadControl& control) const
{
	if (error) { error->clear(); }
	if (!preparePlan(error, control)) { return false; }
	if (m_baseReader && !m_baseReader->verifySourceIdentity(error, control)) { return false; }
	QSet<QString> checked;
	for (const auto& entry : plannedEntries()) {
		if (control.isCancelled && control.isCancelled()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Package read cancelled."); }
			return false;
		}
		if (!entry.sourceIdentity) { continue; }
		const auto& identity = entry.sourceIdentity;
		const QString key = identity->path + QLatin1Char('\n') + QString::fromLatin1(identity->sha256.toHex());
		if (checked.contains(key)) { continue; }
		if (!verifyPackageFileIdentity(identity, error, control)) { return false; }
		checked.insert(key);
	}
	return true;
}

QVector<PackageCompositionBucket> PackageStagingModel::beforeComposition() const
{
	ensurePlan();
	return m_beforeComposition;
}

QVector<PackageCompositionBucket> PackageStagingModel::afterComposition() const
{
	ensurePlan();
	return m_afterComposition;
}

QByteArray PackageStagingModel::manifestJson(QString* error, const PackageReadControl& control) const
{
	if (error) error->clear();
	bool failed = false;
	PackageStagingContentUsage retained;
	PackageStagingMetadataUsage metadata;
	if (!contentUsage(&retained, error, control) || !metadataUsage(&metadata, error, control)
		|| !preparePlan(error, control)) { return {}; }
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
	summaryObject.insert(QStringLiteral("totalsAvailable"), stagingSummary.totalsAvailable);
	summaryObject.insert(QStringLiteral("beforeSizeOverflow"), stagingSummary.beforeSizeOverflow);
	summaryObject.insert(QStringLiteral("afterSizeOverflow"), stagingSummary.afterSizeOverflow);
	const bool beforeExact = stagingSummary.totalsAvailable && !stagingSummary.beforeSizeOverflow;
	const bool afterExact = stagingSummary.totalsAvailable && !stagingSummary.afterSizeOverflow;
	summaryObject.insert(QStringLiteral("beforeBytes"), beforeExact ? QJsonValue(static_cast<double>(stagingSummary.beforeBytes)) : QJsonValue());
	summaryObject.insert(QStringLiteral("afterBytes"), afterExact ? QJsonValue(static_cast<double>(stagingSummary.afterBytes)) : QJsonValue());
	summaryObject.insert(QStringLiteral("beforeBytesExact"), beforeExact ? QJsonValue(QString::number(stagingSummary.beforeBytes)) : QJsonValue());
	summaryObject.insert(QStringLiteral("afterBytesExact"), afterExact ? QJsonValue(QString::number(stagingSummary.afterBytes)) : QJsonValue());
	summaryObject.insert(QStringLiteral("canSave"), stagingSummary.canSave);
	summaryObject.insert(QStringLiteral("retainedContent"), QJsonObject{
		{QStringLiteral("generatedBytes"), QString::number(retained.generatedBytes)},
		{QStringLiteral("fingerprintBytes"), QString::number(retained.fingerprintBytes)},
		{QStringLiteral("maximumGeneratedBytes"), QString::number(m_contentLimits.maximumGeneratedBytes)},
		{QStringLiteral("maximumFingerprintBytes"), QString::number(m_contentLimits.maximumFingerprintBytes)}});
	summaryObject.insert(QStringLiteral("retainedMetadata"), QJsonObject{
		{QStringLiteral("records"), QString::number(metadata.records)}, {QStringLiteral("metadataBytes"), QString::number(metadata.metadataBytes)},
		{QStringLiteral("maximumRecords"), QString::number(m_metadataLimits.maximumRecords)},
		{QStringLiteral("maximumMetadataBytes"), QString::number(m_metadataLimits.maximumMetadataBytes)}});
	summaryObject.insert(QStringLiteral("planLimits"), QJsonObject{
		{QStringLiteral("maximumRecords"), QString::number(m_planLimits.maximumRecords)},
		{QStringLiteral("maximumIndexKeys"), QString::number(m_planLimits.maximumIndexKeys)},
		{QStringLiteral("maximumMetadataBytes"), QString::number(m_planLimits.maximumMetadataBytes)}});
	summaryObject.insert(QStringLiteral("viewLimits"), QJsonObject{
		{QStringLiteral("maximumEntries"), QString::number(m_viewLimits.maximumEntries)},
		{QStringLiteral("maximumMetadataBytes"), QString::number(m_viewLimits.maximumMetadataBytes)},
		{QStringLiteral("maximumPathDepth"), m_viewLimits.maximumPathDepth}});

	const auto cancelled = [&]() {
		if (failed) return true;
		if (control.isCancelled && control.isCancelled()) {
			failed = true;
			if (error) *error = QCoreApplication::translate("VibeStudioPackageStaging", "Package read cancelled.");
		}
		return failed;
	};
	QHash<QString, QString> digestCache;
	const auto digestFor = [&](const PackageStagedEntry& entry) -> QString {
		if (cancelled()) return {};
		const QString key = entryContentKey(entry);
		if (const auto cached = digestCache.constFind(key); cached != digestCache.constEnd()) return cached.value();
		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (!streamEntry(entry, [&](QByteArrayView bytes) { hash.addData(bytes); return true; }, error, control)) {
			failed = true;
			return {};
		}
		const QString digest = QString::fromLatin1(hash.result().toHex());
		digestCache.insert(key, digest);
		return digest;
	};
	QJsonArray operations;
	for (const auto& operation : m_operations) {
		if (cancelled()) return {};
		QString digest;
		if (operation.hasInlineBytes) {
			PackageStagedEntry generated;
			generated.kind = PackageEntryKind::File;
			generated.virtualPath = operation.virtualPath;
			generated.hasInlineBytes = true;
			generated.inlineBytes = operation.inlineBytes;
			generated.sizeBytes = static_cast<quint64>(operation.inlineBytes.size());
			digest = digestFor(generated);
			if (failed) return {};
		}
		operations.append(operationJson(operation, digest));
	}
	const auto entryArray = [&](const QVector<PackageStagedEntry>& entries) {
		QJsonArray array;
		for (const PackageStagedEntry& entry : entries) {
			if (cancelled()) break;
			QJsonObject object;
			object.insert(QStringLiteral("virtualPath"), entry.virtualPath);
			object.insert(QStringLiteral("kind"), packageEntryKindId(entry.kind));
			object.insert(QStringLiteral("bytes"), static_cast<double>(entry.sizeBytes));
			object.insert(QStringLiteral("source"), entry.source);
			object.insert(QStringLiteral("operationId"), entry.operationId);
			if (entry.kind == PackageEntryKind::File) {
				if (entry.unavailableReason.isEmpty()) {
					object.insert(QStringLiteral("sha256"), digestFor(entry));
					if (failed) break;
				} else {
					object.insert(QStringLiteral("sha256"), QJsonValue());
					object.insert(QStringLiteral("contentAvailable"), false);
					object.insert(QStringLiteral("unavailableReason"), entry.unavailableReason);
				}
			}
			array.append(object);
		}
		return array;
	};

	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 2);
	root.insert(QStringLiteral("summary"), summaryObject);
	root.insert(QStringLiteral("operations"), operations);
	root.insert(QStringLiteral("conflicts"), conflictsJson(m_planConflicts));
	root.insert(QStringLiteral("beforeEntries"), entryArray(m_baseEntries));
	root.insert(QStringLiteral("afterEntries"), entryArray(m_planEntries));
	if (failed) return {};
	root.insert(QStringLiteral("beforeComposition"), bucketsJson(m_beforeComposition));
	root.insert(QStringLiteral("afterComposition"), bucketsJson(m_afterComposition));
	if (cancelled()) return {};
	const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
	return cancelled() ? QByteArray() : json;
}

bool PackageStagingModel::addFile(const QString& sourceFilePath, const QString& virtualPath, QString* error, PackageStageConflictResolution resolution, const PackageReadControl& control, PackageFileImportMode mode)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Add;
	operation.sourceFilePath = QFileInfo(sourceFilePath).absoluteFilePath();
	operation.virtualPath = virtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error, control, mode);
}

bool PackageStagingModel::replaceFile(const QString& virtualPath, const QString& sourceFilePath, QString* error, const PackageReadControl& control, PackageFileImportMode mode)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Replace;
	operation.virtualPath = virtualPath;
	operation.sourceFilePath = QFileInfo(sourceFilePath).absoluteFilePath();
	return appendOperation(operation, error, control, mode);
}

bool PackageStagingModel::addBytes(const QByteArray& bytes, const QString& virtualPath, QString* error, PackageStageConflictResolution resolution)
{
	if (bytes.size() > 64 * 1024 * 1024) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Generated assets are limited to 64 MiB per file."); }
		return false;
	}
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Add; operation.virtualPath = virtualPath;
	operation.hasInlineBytes = true; operation.inlineBytes = bytes; operation.conflictResolution = resolution;
	return appendOperation(operation, error);
}

bool PackageStagingModel::addWadBytes(const QByteArray& bytes, const QString& name, const QString& namespaceId, int lumpType, bool replaceExisting, QString* error)
{
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	const bool textureWad = m_sourceWadMagic == QStringLiteral("WAD2") || m_sourceWadMagic == QStringLiteral("WAD3");
	if (!isLoaded() || sourceFormat() != PackageArchiveFormat::Wad || bytes.size() > 64 * 1024 * 1024
		|| !wadNameIsValid(name, textureWad ? 16 : 8) || !normalizePackageVirtualPath(name, false).isSafe()
		|| lumpType < 0 || lumpType > 255 || (textureWad ? !namespaceId.isEmpty() : !QStringList{QStringLiteral("flat"), QStringLiteral("patch"), QStringLiteral("sprite"), QStringLiteral("global")}.contains(namespaceId))
		|| (!textureWad && (!doomNamespaceMarker(name).isEmpty() || isDoomMapMarker(name.toUpper()) || doomMapLumpRank(name.toUpper()) < 1000))) {
		return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Choose a valid native WAD lump name, type, and namespace."));
	}
	if (!summary().canSave) { return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Resolve package conflicts before staging a native texture.")); }
	const auto entries = plannedEntries(); QStringList namespaces; QString anchor, existingNamespace; int matches = 0;
	QHash<QString, int> names; bool malformed = false;
	for (const auto& entry : entries) {
		++names[entryKey(entry.virtualPath)];
		if (entryKey(entry.virtualPath) == entryKey(name)) { ++matches; existingNamespace = namespaces.isEmpty() ? QString() : namespaces.last(); }
		if (textureWad) { continue; }
		bool opens = false; const auto marked = doomNamespaceMarker(entry.virtualPath, &opens);
		if (marked.isEmpty()) { continue; }
		if (entry.sizeBytes != 0) { malformed = true; }
		if (opens) { namespaces.append(marked); }
		else if (const auto at = namespaces.lastIndexOf(marked); at >= 0) {
			if (marked == namespaceId && at == namespaces.size() - 1) { anchor = entry.virtualPath; }
			namespaces.removeAt(at);
		} else { malformed = true; }
	}
	if (matches > 1 || (matches && !replaceExisting)) { return fail(QCoreApplication::translate("VibeStudioPackageStaging", "The lump name is ambiguous or already exists. Review replacement before staging.")); }
	const auto expectedNamespace = namespaceId == QStringLiteral("global") ? QString() : namespaceId;
	if (!textureWad && (malformed || !namespaces.isEmpty() || (matches && existingNamespace != expectedNamespace) || (!matches && namespaceId == QStringLiteral("global")))) {
		return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Repair WAD namespace markers or choose a name in the matching texture namespace."));
	}
	auto next = *this; if (!next.beginOperationGroup(QCoreApplication::translate("VibeStudioPackageStaging", "Stage native texture %1").arg(name), error)) { return false; }
	if (!textureWad && !matches) {
		if (anchor.isEmpty()) {
			const QString prefix = namespaceId == QStringLiteral("flat") ? QStringLiteral("F") : namespaceId == QStringLiteral("patch") ? QStringLiteral("P") : QStringLiteral("S");
			const QString start = prefix + QStringLiteral("_START"); anchor = prefix + QStringLiteral("_END");
			if (names.contains(entryKey(start)) || names.contains(entryKey(anchor))) { return fail(QCoreApplication::translate("VibeStudioPackageStaging", "WAD namespace marker names are already in use.")); }
			if (!next.addBytes({}, start, error) || !next.addBytes({}, anchor, error)) { return false; }
		} else if (names.value(entryKey(anchor)) != 1) {
			return fail(QCoreApplication::translate("VibeStudioPackageStaging", "The destination namespace has repeated end markers. Choose an unambiguous WAD."));
		}
	}
	PackageStageOperation operation; operation.type = PackageStageOperationType::Add;
	operation.virtualPath = textureWad ? name : name.toUpper(); operation.hasInlineBytes = true; operation.inlineBytes = bytes;
	operation.wadLumpType = lumpType; operation.wadInsertBefore = matches ? QString() : anchor;
	operation.wadNamespace = namespaceId;
	operation.conflictResolution = replaceExisting ? PackageStageConflictResolution::ReplaceExisting : PackageStageConflictResolution::Block;
	if (!next.appendOperation(operation, error) || !next.summary().canSave) { return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Native texture staging would create package conflicts.")); }
	if (!next.endOperationGroup(true, error)) { return false; }
	*this = std::move(next); return true;
}

bool PackageStagingModel::renameEntry(const QString& virtualPath, const QString& targetVirtualPath, QString* error, PackageStageConflictResolution resolution, const PackageReadControl& control)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Rename;
	operation.virtualPath = virtualPath;
	operation.targetVirtualPath = targetVirtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error, control);
}

bool PackageStagingModel::deleteEntry(const QString& virtualPath, QString* error, PackageStageConflictResolution resolution, const PackageReadControl& control)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Delete;
	operation.virtualPath = virtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error, control);
}

bool PackageStagingModel::createDirectory(const QString& virtualPath, QString* error, const PackageReadControl& control)
{
	PackageStageOperation operation; operation.type = PackageStageOperationType::CreateDirectory; operation.virtualPath = virtualPath;
	return appendDirectoryOperation(operation, error, control);
}

bool PackageStagingModel::renameDirectory(const QString& virtualPath, const QString& targetVirtualPath, QString* error, const PackageReadControl& control)
{
	PackageStageOperation operation; operation.type = PackageStageOperationType::RenameDirectory;
	operation.virtualPath = virtualPath; operation.targetVirtualPath = targetVirtualPath;
	return appendDirectoryOperation(operation, error, control);
}

bool PackageStagingModel::deleteDirectory(const QString& virtualPath, QString* error, const PackageReadControl& control)
{
	PackageStageOperation operation; operation.type = PackageStageOperationType::DeleteDirectory; operation.virtualPath = virtualPath;
	return appendDirectoryOperation(operation, error, control);
}

bool PackageStagingModel::appendDirectoryOperation(PackageStageOperation operation, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (!m_loaded || m_sourceFormat == PackageArchiveFormat::Wad) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Open a folder, PAK, ZIP, or PK3 package to edit folders."); }
		return false;
	}
	operation.virtualPath = normalizePackageVirtualPath(operation.virtualPath, false).normalizedPath;
	if (operation.type == PackageStageOperationType::RenameDirectory) { operation.targetVirtualPath = normalizePackageVirtualPath(operation.targetVirtualPath, false).normalizedPath; }
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageDirectory", "Preparing package folder edit"));
	const auto nestedControl = work.nestedControl();
	if (!admitHistoryEdit(true, error)) { return false; }
	operation.id = QStringLiteral("stage-%1").arg(m_operationSerial + 1);
	if (!preparePlan(error, nestedControl)) { return false; }
	const auto original = plannedEntries();
	auto entries = original;
	if (operation.type != PackageStageOperationType::CreateDirectory) {
		operation.sourceTreeIdentity = packageDirectoryIdentity(entries, operation.virtualPath, error, nestedControl, m_planLimits);
		if (work.cancelled() || (error && !error->isEmpty())) { return false; }
	}
	if (!applyPackageDirectoryOperation(&entries, operation, error, nestedControl, m_planLimits)) { return false; }
	PackagePlanBudget budget(m_planLimits, work);
	for (const auto& conflict : m_planConflicts) {
		if (!work.checkpoint() || !budget.addRecord(PackagePlanBudget::conflictBytes(conflict))) { return false; }
	}
	for (const auto& entry : entries) {
		if (!work.checkpoint() || !budget.addRecord(PackagePlanBudget::entryBytes(entry))) { return false; }
	}
	if (operation.type == PackageStageOperationType::RenameDirectory && original.size() == entries.size()) {
		bool same = true;
		for (qsizetype at = 0; at < entries.size(); ++at) {
			if (!work.checkpoint()) { return false; }
			if (original.at(at).virtualPath != entries.at(at).virtualPath) { same = false; break; }
		}
		if (same) { return work.finish(); }
	}
	return appendOperation(operation, error, nestedControl);
}

bool PackageStagingModel::replaceOccurrence(int sourceOrdinal, const QString& sourceFilePath, QString* error, const PackageReadControl& control, PackageFileImportMode mode)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Replace; operation.sourceOrdinal = sourceOrdinal;
	operation.sourceFilePath = QFileInfo(sourceFilePath).absoluteFilePath();
	return appendOccurrenceOperation(operation, error, control, mode);
}

bool PackageStagingModel::renameOccurrence(int sourceOrdinal, const QString& targetVirtualPath, QString* error, PackageStageConflictResolution resolution, const PackageReadControl& control)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Rename; operation.sourceOrdinal = sourceOrdinal;
	operation.targetVirtualPath = targetVirtualPath; operation.conflictResolution = resolution;
	return appendOccurrenceOperation(operation, error, control);
}

bool PackageStagingModel::deleteOccurrence(int sourceOrdinal, QString* error, PackageStageConflictResolution resolution, const PackageReadControl& control)
{
	PackageStageOperation operation;
	operation.type = PackageStageOperationType::Delete; operation.sourceOrdinal = sourceOrdinal;
	operation.conflictResolution = resolution;
	return appendOccurrenceOperation(operation, error, control);
}

bool PackageStagingModel::appendOccurrenceOperation(PackageStageOperation operation, QString* error, const PackageReadControl& control, PackageFileImportMode mode)
{
	if (error) { error->clear(); }
	if (!preparePlan(error, control)) { return false; }
	const PackageStagedEntry* match = nullptr;
	if (m_loaded && operation.sourceOrdinal >= 0) {
		for (const auto& entry : m_planEntries) {
			if (control.isCancelled && control.isCancelled()) {
				if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Package content admission cancelled."); }
				return false;
			}
			if (entry.kind != PackageEntryKind::File || entry.sourceOrdinal != operation.sourceOrdinal) { continue; }
			if (match) { match = nullptr; break; }
			match = &entry;
		}
	}
	if (!match) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Select one existing source occurrence before editing it."); }
		return false;
	}
	operation.virtualPath = match->virtualPath;
	return appendOperation(operation, error, control, mode);
}

bool PackageStagingModel::clearOperation(const QString& operationId, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageStaging", "Preparing package edit"));
	if (!work.checkpoint()) { return false; }
	for (qsizetype index = 0; index < m_operations.size(); ++index) {
		if (!work.checkpoint()) { return false; }
		if (m_operations.at(index).id != operationId) { continue; }
		if (!admitHistoryEdit(false, error)) { return false; }
		const QString label = QCoreApplication::translate("VibeStudioPackageStaging", "Unstage change");
		PackageStagingMetadataUsage usage, addition{1, 0};
		if (m_groupDepth == 0) { const auto step = historyMetadata(label); addition.records += step.records; addition.metadataBytes += step.metadataBytes; }
		if (!m_metadataUsageValid && !metadataUsage(&usage, error, control)) { return false; }
		usage = m_metadataUsage;
		std::unique_ptr<PackageStagingModel> candidate;
		PackageStagingModel* target = this;
		if (!metadataFits(usage, addition)) {
			candidate = std::make_unique<PackageStagingModel>(*this);
			if (candidate->m_groupDepth == 0) {
				candidate->m_history.resize(candidate->m_historyCursor);
				if (candidate->m_history.size() >= 256) { candidate->m_history.removeFirst(); --candidate->m_historyCursor; }
			}
			candidate->invalidatePlan();
			if (!candidate->metadataUsage(&usage, error, control) || !candidate->metadataFits(usage, addition, error)) { return false; }
			target = candidate.get();
		}
		if (m_groupDepth == 0 && !candidate) { candidate = std::make_unique<PackageStagingModel>(*this); target = candidate.get(); }
		const PackageStageHistoryChange change{target->m_operations.at(index), index, false};
		target->m_operations.removeAt(index); target->invalidatePlan();
		if (m_groupDepth == 0 && !target->admitView(error, control)) { return false; }
		target->recordHistory(change, label);
		usage.records += addition.records; usage.metadataBytes += addition.metadataBytes;
		target->m_metadataUsage = usage; target->m_metadataUsageValid = true;
		if (candidate) { *this = std::move(*candidate); }
		return true;
	}
	return false;
}

void PackageStagingModel::resetHistory()
{
	m_metadataUsageValid = false; m_metadataUsageExact = false;
	m_contentUsageValid = false; m_contentUsageExact = false;
	m_history.clear(); m_historyCursor = 0; m_groupDepth = 0; m_group = {};
	m_revision = 0; m_revisionSerial = 0; m_savedRevision = 0;
}

bool PackageStagingModel::beginOperationGroup(const QString& label, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	PackagePlanWork work(control, error, QCoreApplication::translate("VibeStudioPackageStaging", "Preparing package edit"));
	if (!work.checkpoint()) { return false; }
	if (m_groupDepth >= 256) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Package edit groups exceed the supported nesting limit."); }
		return false;
	}
	if (m_groupDepth > 0) { ++m_groupDepth; return true; }
	PackageStagingMetadataUsage usage; const auto addition = historyMetadata(label);
	if (!m_metadataUsageValid && !metadataUsage(&usage, error, control)) { return false; }
	usage = m_metadataUsage;
	if (!metadataFits(usage, addition) && !metadataUsage(&usage, error, control)) { return false; }
	if (!metadataFits(usage, addition, error)) { return false; }
	m_group = {}; m_group.label = QString(label.constData(), label.size()); m_groupDepth = 1;
	usage.records += addition.records; usage.metadataBytes += addition.metadataBytes;
	m_metadataUsage = usage; m_metadataUsageValid = true; m_metadataUsageExact = false;
	return true;
}

bool PackageStagingModel::endOperationGroup(bool commit, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (m_groupDepth == 0) { return true; }
	m_metadataUsageValid = false; m_metadataUsageExact = false;
	m_contentUsageValid = false; m_contentUsageExact = false;
	if (!commit) {
		applyHistory(m_group, false);
		m_group = {}; m_groupDepth = 0;
		return true;
	}
	if (m_groupDepth > 1) { --m_groupDepth; return true; }
	// Batch appends stay cheap. Validate once, before history drops redo or
	// advances its revision. Failed admission cannot leave a partial import.
	if (!m_group.changes.isEmpty() && !admitView(error, control)) {
		applyHistory(m_group, false); m_group = {}; m_groupDepth = 0;
		return false;
	}
	m_groupDepth = 0; commitHistory(std::move(m_group)); m_group = {};
	return true;
}

bool PackageStagingModel::admitView(QString* error, const PackageReadControl& control) const
{
	if (!preparePlan(error, control)) { return false; }
	// Reuse the browser projection so warnings, unreadable base rows, WAD
	// metadata and implied folders obey exactly the same admission policy.
	const PackageStagingArchive view(*this, PackageStagingReadMode::InspectPlan, control, m_viewLimits);
	if (!view.isOpen()) { if (error) { *error = view.errorString(); } return false; }
	return true;
}

bool PackageStagingModel::admitHistoryEdit(bool needsOperationId, QString* error) const
{
	if (m_revisionSerial >= maximumSerial || (needsOperationId && m_operationSerial >= maximumSerial)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "The package document has reached its edit-history counter limit. Undo/Redo and draft saving remain available. Export the package and reopen it to start a new history.");
		}
		return false;
	}
	return true;
}

void PackageStagingModel::recordHistory(PackageStageHistoryChange change, const QString& label)
{
	if (m_groupDepth > 0) { m_group.changes.append(std::move(change)); return; }
	PackageStageHistoryStep step;
	step.label = label; step.changes.append(std::move(change));
	commitHistory(std::move(step));
}

void PackageStagingModel::commitHistory(PackageStageHistoryStep step)
{
	if (step.changes.isEmpty()) { return; }
	Q_ASSERT(m_revisionSerial < maximumSerial);
	m_history.resize(m_historyCursor);
	step.beforeRevision = m_revision;
	step.afterRevision = ++m_revisionSerial;
	m_revision = step.afterRevision;
	m_history.append(std::move(step));
	// A bounded history keeps large import sessions practical. Operations still
	// in the plan are retained even when their undo step falls off this list.
	constexpr qsizetype maximumSteps = 256;
	if (m_history.size() > maximumSteps) { m_history.removeFirst(); }
	m_historyCursor = m_history.size();
}

void PackageStagingModel::applyHistory(const PackageStageHistoryStep& step, bool forward)
{
	const auto apply = [this, forward](const PackageStageHistoryChange& change) {
		if (change.inserted == forward) { m_operations.insert(change.index, change.operation); }
		else { m_operations.removeAt(change.index); }
	};
	if (forward) { for (const auto& change : step.changes) { apply(change); } }
	else { for (auto it = step.changes.crbegin(); it != step.changes.crend(); ++it) { apply(*it); } }
	invalidatePlan();
}

bool PackageStagingModel::canUndo() const { return m_groupDepth == 0 && m_historyCursor > 0; }
bool PackageStagingModel::canRedo() const { return m_groupDepth == 0 && m_historyCursor < m_history.size(); }
QString PackageStagingModel::undoLabel() const { return canUndo() ? m_history.at(m_historyCursor - 1).label : QString(); }
QString PackageStagingModel::redoLabel() const { return canRedo() ? m_history.at(m_historyCursor).label : QString(); }
bool PackageStagingModel::undo()
{
	if (!canUndo()) { return false; }
	const auto& step = m_history.at(--m_historyCursor);
	applyHistory(step, false); m_revision = step.beforeRevision;
	return true;
}
bool PackageStagingModel::redo()
{
	if (!canRedo()) { return false; }
	const auto& step = m_history.at(m_historyCursor++);
	applyHistory(step, true); m_revision = step.afterRevision;
	return true;
}
bool PackageStagingModel::isModified() const { return m_revision != m_savedRevision || !m_group.changes.isEmpty(); }
void PackageStagingModel::markSaved() { if (m_groupDepth == 0) { m_savedRevision = m_revision; } }
quint64 PackageStagingModel::revision() const { return m_revision; }
QString PackageStagingModel::draftPath() const { return m_draftPath; }

bool PackageStagingModel::exportManifest(const QString& outputPath, QString* error) const
{
	if (error) {
		error->clear();
	}
	if (outputPath.trimmed().isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Manifest output path is required.");
		}
		return false;
	}
	if (protectsInputPath(outputPath)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Manifest output would replace package source content.");
		}
		return false;
	}
	QSaveFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Unable to open package manifest for writing.");
		}
		return false;
	}
	const QByteArray json = manifestJson(error);
	if (json.isEmpty()) return false;
	if (file.write(json) != json.size()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Unable to write package manifest.");
		}
		return false;
	}
	if (!file.commit()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageStaging", "Unable to commit package manifest.");
		}
		return false;
	}
	return true;
}


PackageStagingArchive::PackageStagingArchive(const PackageStagingModel& staging, PackageStagingReadMode mode,
	const PackageReadControl& control, const PackageIndexLimits& requested) : m_snapshot(staging)
{
	if (!validPackageIndexLimits(requested, &m_error) || !validPackageIndexLimits(staging.viewLimits(), &m_error)) { return; }
	auto limits = staging.viewLimits();
	limits.maximumEntries = std::min(limits.maximumEntries, requested.maximumEntries);
	limits.maximumMetadataBytes = std::min(limits.maximumMetadataBytes, requested.maximumMetadataBytes);
	limits.maximumPathDepth = std::min(limits.maximumPathDepth, requested.maximumPathDepth);
	limits.maximumFingerprintBytes = std::min(limits.maximumFingerprintBytes, requested.maximumFingerprintBytes);
	PackageIndexUsage usage;
	if (!admitPackageSnapshot(sourcePath(), {}, {}, limits, control, &usage, nullptr, &m_error)
		|| !m_snapshot.preparePlan(&m_error, control)) { return; }
	m_entries = m_snapshot.plannedEntries();
	const auto refused = [&] { m_ready = false; m_entries.clear(); m_metadata.clear(); };
	if (m_entries.size() > limits.maximumEntries) {
		m_error = QCoreApplication::translate("VibeStudioPackageStaging", "The planned package exceeds the %1-entry snapshot limit. Undo recent edits or use a smaller package.").arg(limits.maximumEntries);
		refused(); return;
	}
	m_ready = mode == PackageStagingReadMode::InspectPlan ? staging.isLoaded() : m_snapshot.summary().canSave;
	// The base reader is an immutable directory snapshot, safe to share. Keep
	// its source-change checks instead of silently reopening a different file.
	const QVector<PackageEntry> base = m_snapshot.m_baseReader ? m_snapshot.m_baseReader->entries() : QVector<PackageEntry>();

	const bool wad = format() == PackageArchiveFormat::Wad;
	const bool textureWad = m_snapshot.sourceWadMagic() == QStringLiteral("WAD2") || m_snapshot.sourceWadMagic() == QStringLiteral("WAD3");
	QStringList doomNamespaces;
	for (const auto& staged : m_entries) {
		if (control.isCancelled && control.isCancelled()) { m_error = QCoreApplication::translate("VibeStudioPackageArchive", "Package snapshot preparation cancelled."); refused(); return; }
		const bool fromBase = staged.sourceReaderIndex >= 0 && staged.sourceReaderIndex < base.size()
			&& !staged.hasInlineBytes && staged.sourceFilePath.isEmpty();
		PackageEntry entry = fromBase ? base.at(staged.sourceReaderIndex) : PackageEntry();
		entry.virtualPath = staged.virtualPath; entry.kind = staged.kind; entry.sizeBytes = staged.sizeBytes;
		entry.modifiedUtc = staged.modifiedUtc; entry.sourceArchiveId = sourcePath(); entry.sourceOrdinal = staged.sourceOrdinal;
		entry.wadLumpType = wad ? staged.wadLumpType : -1;
		entry.layerId = staged.hasInlineBytes ? QStringLiteral("generated") : (!staged.sourceFilePath.isEmpty() ? QStringLiteral("staged-file") : QStringLiteral("base"));
		if (!fromBase) { entry.storageMethod = staged.source == QStringLiteral("synthetic") ? QStringLiteral("synthetic")
			: staged.hasInlineBytes ? QStringLiteral("memory") : QStringLiteral("staged"); }
		if (!fromBase || staged.virtualPath != staged.baseVirtualPath) { entry.typeHint = QFileInfo(entry.virtualPath).suffix().toLower(); }
		if (wad) {
			// WAD namespaces and header-detected sounds cannot be reconstructed
			// from extensions. Preserve base-reader hints through pending views;
			// generated Doom lumps retain the normal DS/DP name classification.
			entry.typeHint = staged.sourceReaderIndex >= 0 && staged.sourceReaderIndex < base.size()
				? base.at(staged.sourceReaderIndex).typeHint
				: textureWad ? QStringLiteral("wad-texture") : QStringLiteral("wad-lump");
			if (!textureWad) {
				bool opens = false; const auto marked = doomNamespaceMarker(entry.virtualPath, &opens);
				if (!marked.isEmpty()) {
					if (opens) { doomNamespaces.append(marked); }
					else if (const auto at = doomNamespaces.lastIndexOf(marked); at >= 0) { doomNamespaces.removeAt(at); }
					entry.typeHint = QStringLiteral("wad-marker");
				} else if (!doomNamespaces.isEmpty()) { entry.typeHint = QStringLiteral("wad-%1").arg(doomNamespaces.last()); }
				else if (entry.typeHint != QStringLiteral("wad-sound")) { entry.typeHint = QStringLiteral("wad-lump"); }
			}
		}
		if (entry.kind == PackageEntryKind::Directory) { entry.typeHint = QStringLiteral("directory"); }
		entry.nestedArchiveCandidate = entry.kind == PackageEntryKind::File && packageEntryLooksNestedArchive(entry.virtualPath);
		entry.readable = m_ready && entry.readable && staged.unavailableReason.isEmpty();
		if (!staged.unavailableReason.isEmpty()) { entry.note = staged.unavailableReason; }
		m_metadata.append(entry);
	}
	QVector<PackageEntry> implied;
	if (!admitPackageSnapshot(sourcePath(), m_metadata, warnings(), limits, control, &usage, mode == PackageStagingReadMode::InspectPlan ? &implied : nullptr, &m_error)) { refused(); return; }
	if (mode == PackageStagingReadMode::InspectPlan) {
		for (auto& metadata : implied) {
			if (control.isCancelled && control.isCancelled()) { m_error = QCoreApplication::translate("VibeStudioPackageArchive", "Package snapshot preparation cancelled."); refused(); return; }
			PackageStagedEntry entry; entry.virtualPath = metadata.virtualPath;
			entry.kind = PackageEntryKind::Directory; entry.source = QStringLiteral("synthetic");
			m_entries.append(std::move(entry)); m_metadata.append(std::move(metadata));
		}
	}
}
PackageArchiveFormat PackageStagingArchive::format() const { return m_snapshot.sourceFormat(); }
QString PackageStagingArchive::sourcePath() const { return m_snapshot.sourcePath(); }
QString PackageStagingArchive::wadMagic() const { return m_snapshot.sourceWadMagic(); }
bool PackageStagingArchive::isOpen() const { return m_ready; }
QVector<PackageEntry> PackageStagingArchive::entries() const { return m_metadata; }
QVector<PackageLoadWarning> PackageStagingArchive::warnings() const
{
	if (!m_error.isEmpty()) { return {{{}, m_error, true}}; }
	QVector<PackageLoadWarning> result;
	for (const auto& conflict : m_snapshot.conflicts()) { result.append({conflict.virtualPath, conflict.message, conflict.blocking}); }
	return result;
}

PackageArchive packagePlannedArchive(const PackageStagingModel& staging, QString* error, const PackageReadControl& control, const PackageIndexLimits& limits)
{
	PackageArchive result;
	if (!staging.isLoaded()) { if (error) { error->clear(); } return result; }
	const auto reader = std::make_shared<PackageStagingArchive>(staging, PackageStagingReadMode::InspectPlan, control, limits);
	result.loadSnapshot(reader, error, reader->warnings(), control, limits);
	return result;
}
bool PackageStagingArchive::readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (out) { out->clear(); } if (error) { error->clear(); }
	const PackageStagedEntry* match = nullptr;
	for (const auto& entry : m_entries) {
		if (entry.kind == PackageEntryKind::File && entryKey(entry.virtualPath) == entryKey(virtualPath)) {
			if (match) { if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "The staged path is ambiguous."); } return false; }
			match = &entry;
		}
	}
	if (!m_ready || !match) { if (error) { *error = (m_error.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "The staged file is unavailable.") : m_error); } return false; }
	return m_snapshot.entryBytes(*match, out, error, maxBytes);
}

bool PackageStagingArchive::readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (out) { out->clear(); } if (error) { error->clear(); }
	if (!m_ready || index < 0 || index >= m_entries.size() || !m_metadata.at(index).readable || m_entries.at(index).kind != PackageEntryKind::File) {
		if (error) { *error = (m_error.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "The staged file is unavailable.") : m_error); }
		return false;
	}
	return m_snapshot.entryBytes(m_entries.at(index), out, error, maxBytes);
}


bool PackageStagingArchive::streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink,
	QString* error, const std::function<bool()>& isCancelled) const
{
	if (error) error->clear();
	if (!m_ready || !sink || index < 0 || index >= m_entries.size() || !m_metadata.at(index).readable || m_entries.at(index).kind != PackageEntryKind::File) {
		if (error) *error = (m_error.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "The staged file is unavailable.") : m_error);
		return false;
	}
	PackageReadControl control; control.isCancelled = isCancelled;
	return m_snapshot.streamEntry(m_entries.at(index), sink, error, control);
}

bool PackageStagingModel::streamEntry(const PackageStagedEntry& entry, const std::function<bool(QByteArrayView)>& sink,
	QString* error, const PackageReadControl& control) const
{
	if (error) error->clear();
	const auto fail = [error](const QString& message) { if (error) *error = message; return false; };
	const auto cancelled = [&]() { return control.isCancelled && control.isCancelled(); };
	if (!sink || entry.kind != PackageEntryKind::File || entry.sizeBytes > static_cast<quint64>(std::numeric_limits<qint64>::max()))
		return fail(QCoreApplication::translate("VibeStudioPackageStaging", "The staged file is unavailable."));
	if (cancelled()) return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Package read cancelled."));
	if (!entry.unavailableReason.isEmpty()) { return fail(entry.unavailableReason); }
	quint64 receivedBytes = 0;
	if (control.progress) control.progress(entry.virtualPath, 0, static_cast<qint64>(entry.sizeBytes));
	const auto consume = [&](QByteArrayView bytes) {
		if (cancelled() || static_cast<quint64>(bytes.size()) > entry.sizeBytes - receivedBytes) return false;
		if (!sink(bytes)) return false;
		receivedBytes += static_cast<quint64>(bytes.size());
		if (control.progress) control.progress(entry.virtualPath, static_cast<qint64>(receivedBytes), static_cast<qint64>(entry.sizeBytes));
		return !cancelled();
	};
	if (entry.hasInlineBytes) {
		for (qsizetype offset = 0; offset < entry.inlineBytes.size(); offset += 65536) {
			if (cancelled()) { return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Package read cancelled.")); }
			if (!consume(QByteArrayView(entry.inlineBytes).sliced(offset, qMin<qsizetype>(65536, entry.inlineBytes.size() - offset)))) {
				return fail(QCoreApplication::translate("VibeStudioPackageStaging", "The package byte consumer stopped the read."));
			}
		}
	} else if (!entry.sourceFilePath.isEmpty()) {
		PackageContentDevice file(entry.sourceIdentity, control);
		if (!file.open(QIODevice::ReadOnly)) { return fail(file.errorString()); }
		quint64 received = 0;
		while (received < entry.sizeBytes) {
			if (cancelled()) { return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Package read cancelled.")); }
			const qint64 count = static_cast<qint64>(qMin<quint64>(65536, entry.sizeBytes - received));
			const QByteArray chunk = file.read(count);
			if (chunk.size() != count || file.failed()) { return fail(file.errorString()); }
			if (!consume(QByteArrayView(chunk))) { return fail(QCoreApplication::translate("VibeStudioPackageStaging", "The package byte consumer stopped the read.")); }
			received += static_cast<quint64>(chunk.size());
		}
		if (!entry.sourceIdentity->matchesMetadata()) {
			return fail(QCoreApplication::translate("VibeStudioPackageStaging", "The staged source changed while reading it."));
		}
	} else if (m_baseReader && entry.sourceReaderIndex >= 0) {
		if (!m_baseReader->streamEntryAt(entry.sourceReaderIndex, consume, error, control.isCancelled)) return false;
	} else {
		return fail(QCoreApplication::translate("VibeStudioPackageStaging", "The base entry has no retained source. Replace it or reopen the original package."));
	}
	if (receivedBytes != entry.sizeBytes)
		return fail(QCoreApplication::translate("VibeStudioPackageStaging", "Entry changed size while writing: %1").arg(entry.virtualPath));
	return !cancelled() || fail(QCoreApplication::translate("VibeStudioPackageStaging", "Package read cancelled."));
}

PackageWriteReport PackageStagingModel::writeArchive(const PackageWriteRequest& request) const
{
	PackageWriteReport report;
	report.sourcePath = m_sourcePath;
	report.outputPath = QFileInfo(request.destinationPath).absoluteFilePath();
	report.format = request.format == PackageArchiveFormat::Unknown ? packageArchiveFormatFromFileName(request.destinationPath) : request.format;
	report.dryRun = request.dryRun;
	report.timestampModeId = packageTimestampModeId(request.timestampMode);
	const auto cancelled = [&]() {
		if (report.cancelled || (request.isCancelled && request.isCancelled())) {
			if (!report.cancelled) {
				report.blockedMessages << QCoreApplication::translate("VibeStudioPackageStaging", "Package export cancelled; the destination was left untouched.");
			}
			report.cancelled = true;
			return true;
		}
		return false;
	};
	if (cancelled()) { return report; }
	QString indexError;
	if (!validPackageIndexLimits(request.indexLimits, &indexError)) { report.blockedMessages << indexError; return report; }

	bool preparationCancelled = false;
	PackageReadControl planControl;
	planControl.isCancelled = [&] {
		preparationCancelled = preparationCancelled || (request.isCancelled && request.isCancelled());
		return preparationCancelled;
	};
	planControl.progress = [&](const QString& phase, qint64 done, qint64 total) {
		if (request.byteProgress) { request.byteProgress(PackageWritePhase::CheckIndex, phase, done, total); }
	};
	if (!preparePlan(&indexError, planControl)) {
		report.cancelled = preparationCancelled;
		report.blockedMessages << indexError;
		return report;
	}
	const PackageStagingSummary stagingSummary = summary();
	if (!stagingSummary.canSave) {
		report.blockedMessages = stagingSummary.blockedMessages;
		if (report.blockedMessages.isEmpty()) {
			report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Package staging model is not saveable."));
		}
		return report;
	}
	if (request.destinationPath.trimmed().isEmpty()) {
		report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Save-as destination path is required."));
		return report;
	}
	PackageInputProtectionSet inputs(PackageInputProtectionSet::pathCeiling, PackageInputProtectionSet::byteCeiling, &indexError, planControl);
	if (!inputs.finish(visitProtectedInputPaths([&](const QString& path) { return inputs.add(path); }, &indexError, planControl))) {
		report.cancelled = preparationCancelled; report.blockedMessages << inputs.errorString(); return report;
	}
	const auto checkInput = [&](const QString& path, bool* protectedPath) {
		if (inputs.check(path, protectedPath)) { return true; }
		report.cancelled = preparationCancelled; report.blockedMessages << inputs.errorString(); return false;
	};
	bool destinationProtected = false, lockProtected = false, manifestProtected = false, backupProtected = false;
	if (!checkInput(request.destinationPath, &destinationProtected)) { return report; }
	// Canonical paths so a junction, symlink, or mapped path cannot point the
	// save-as output back at the package that is being read.
	const bool destinationIsSource = !m_sourcePath.isEmpty() && canonicalComparePath(request.destinationPath).compare(canonicalComparePath(m_sourcePath), Qt::CaseInsensitive) == 0;
	if (destinationIsSource && !request.allowInPlaceOverwrite) {
		report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Save-as destination must be different from the source package path."));
		return report;
	}
	if (destinationProtected && !(destinationIsSource && request.allowInPlaceOverwrite && m_sourceFormat != PackageArchiveFormat::Folder)) {
		report.blockedMessages << QCoreApplication::translate("VibeStudioPackageStaging", "Choose an output outside the source folder and imported input files.");
		return report;
	}
	const QString manifestPath = request.manifestPath.trimmed().isEmpty() ? report.outputPath + QStringLiteral(".manifest.json") : request.manifestPath;
	const QString backupPath = request.backupPath.trimmed().isEmpty() ? report.outputPath + QStringLiteral(".bak") : request.backupPath;
	const QString lockPath = report.outputPath + QStringLiteral(".vibestudio-save.lock");
	if (!checkInput(lockPath, &lockProtected)) { return report; }
	if (lockProtected) {
		report.blockedMessages << QCoreApplication::translate("VibeStudioPackageStaging", "The package save lock would modify protected source content. Choose another output path.");
		return report;
	}
	if (request.writeManifest && !checkInput(manifestPath, &manifestProtected)) { return report; }
	if (request.writeManifest && (manifestProtected
		|| canonicalComparePath(manifestPath).compare(canonicalComparePath(report.outputPath), Qt::CaseInsensitive) == 0
		|| canonicalComparePath(manifestPath).compare(canonicalComparePath(lockPath), Qt::CaseInsensitive) == 0
		|| ((request.allowOverwrite || request.allowInPlaceOverwrite) && canonicalComparePath(manifestPath).compare(canonicalComparePath(backupPath), Qt::CaseInsensitive) == 0))) {
		report.blockedMessages << QCoreApplication::translate("VibeStudioPackageStaging", "Manifest path must be separate from package output, backup, save lock, and source content.");
		return report;
	}
	if ((request.allowOverwrite || request.allowInPlaceOverwrite) && !checkInput(backupPath, &backupProtected)) { return report; }
	if ((request.allowOverwrite || request.allowInPlaceOverwrite) && (backupProtected
		|| canonicalComparePath(backupPath).compare(canonicalComparePath(report.outputPath), Qt::CaseInsensitive) == 0)) {
		report.blockedMessages << QCoreApplication::translate("VibeStudioPackageStaging", "Backup path must be separate from package output and source content.");
		return report;
	}
	const QFileInfo destinationBefore(request.destinationPath);
	const bool destinationExists = destinationBefore.exists();
	if (destinationExists && !request.allowOverwrite && !request.allowInPlaceOverwrite) {
		report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Destination already exists. Choose a new save-as path or enable overwrite explicitly."));
		return report;
	}
	if (destinationExists && !QFileInfo(request.destinationPath).isFile()) {
		report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Destination exists and is not a file."));
		return report;
	}

	const bool zipFamily = report.format == PackageArchiveFormat::Zip || report.format == PackageArchiveFormat::Pk3;
	if (!(report.format == PackageArchiveFormat::Pak || zipFamily || report.format == PackageArchiveFormat::Wad)) {
		report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Write-back supports PAK, ZIP, PK3, and WAD outputs."));
		return report;
	}

	PackageWriteOptions options;
	options.isCancelled = request.isCancelled;
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
		// WAD documents already have a positional plan, including newly added
		// lumps, texture anchors and source-free draft/subset snapshots. Save
		// exactly that reviewed order. Canonical fallback ordering is only for
		// conversion from non-WAD sources, which do not carry WAD membership.
		options.preserveWadOrder = m_sourceFormat == PackageArchiveFormat::Wad;
		report.wadMagic = magic;
	}
	report.compressionId = zipFamily ? deflateLevelId(options.level) : QStringLiteral("stored");

	const QVector<PackageStagedEntry> entries = plannedEntries();
	if (!admitWrittenIndex(entries, report.format, request, &options, &indexError)) {
		if (!cancelled()) { report.blockedMessages << indexError; }
		return report;
	}
	if (!zipFamily) {
		QSet<QString> populated;
		for (const auto& entry : entries) {
			if (entry.kind != PackageEntryKind::File) { continue; }
			for (QString parent = packageVirtualPathParent(entryKey(entry.virtualPath)); !parent.isEmpty(); parent = packageVirtualPathParent(parent)) { populated.insert(parent); }
		}
		for (const auto& entry : entries) {
			if (entry.kind == PackageEntryKind::Directory && !populated.contains(entryKey(entry.virtualPath))) {
				report.blockedMessages << QCoreApplication::translate("VibeStudioPackageStaging", "This format cannot preserve the empty folder %1. Add a file, remove the folder, or choose ZIP/PK3.").arg(entry.virtualPath);
			}
		}
		if (!report.blockedMessages.isEmpty()) { return report; }
	}
	PackageReadControl sourceControl;
	sourceControl.isCancelled = request.isCancelled;
	sourceControl.progress = [&](const QString& path, qint64 done, qint64 total) {
		if (request.byteProgress) request.byteProgress(PackageWritePhase::VerifySources, path, static_cast<quint64>(done), static_cast<quint64>(total));
	};
	QString sourceError;
	if (!verifySources(&sourceError, sourceControl)) {
		if (!cancelled()) { report.blockedMessages << sourceError; }
		return report;
	}
	const int totalFiles = static_cast<int>(std::count_if(entries.cbegin(), entries.cend(), [](const auto& entry) { return entry.kind == PackageEntryKind::File; }));
	int completedFiles = 0;
	bool verifyingDeterminism = false;
	const EntryStreamProvider provider = [this, &cancelled, &request, &completedFiles, &verifyingDeterminism, totalFiles](
		const PackageStagedEntry& entry, PackageWritePhase phase, const std::function<bool(QByteArrayView)>& consume, QString* readError) {
		if (request.progress) request.progress(completedFiles, totalFiles, entry.virtualPath);
		if (cancelled()) return false;
		PackageReadControl control;
		control.isCancelled = request.isCancelled;
		control.progress = [&](const QString& path, qint64 done, qint64 total) {
			if (request.byteProgress) request.byteProgress(verifyingDeterminism ? PackageWritePhase::VerifyDeterminism : phase,
				path, static_cast<quint64>(done), static_cast<quint64>(total));
		};
		const bool read = streamEntry(entry, consume, readError, control);
		if (read && phase == PackageWritePhase::Write) ++completedFiles;
		return read && !cancelled();
	};

	const auto runWriter = [&entries, &provider, &options, &report, &completedFiles](ByteSink* sink, PackageWriteStats* stats, QString* writerError) {
		completedFiles = 0;
		if (report.format == PackageArchiveFormat::Pak) {
			return writePakStream(entries, provider, sink, stats, writerError);
		}
		if (report.format == PackageArchiveFormat::Wad) {
			return writeWadStream(entries, provider, options, sink, stats, writerError);
		}
		return writeZipStream(entries, provider, options, sink, stats, writerError);
	};

	PackagePublicationOptions publicationOptions;
	publicationOptions.destinationPath = report.outputPath;
	publicationOptions.allowOverwrite = request.allowOverwrite || request.allowInPlaceOverwrite;
	publicationOptions.backupPath = backupPath;
	publicationOptions.isCancelled = request.isCancelled;
	publicationOptions.expectedDestinationSha256 = request.expectedDestinationSha256;
	PackagePublication publication(publicationOptions);
	QString publicationError;
	if (!request.dryRun && !publication.begin(&publicationError)) {
		if (!cancelled()) { report.blockedMessages << publicationError; }
		return report;
	}

	PackageWriteStats stats;
	QString writerError;
	ByteSink sink(request.dryRun ? nullptr : publication.device(), request.indexLimits, request.isCancelled);
	if (!runWriter(&sink, &stats, &writerError) || !sink.flush()) {
		if (cancelled()) return report;
		if (!sink.errorString().isEmpty()) { writerError = sink.errorString(); }
		report.blockedMessages.push_back(writerError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "Unable to write package bytes.") : writerError);
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
		verifyingDeterminism = true;
		PackageWriteStats verifyStats;
		QString verifyError;
		ByteSink verifySink(nullptr, request.indexLimits, request.isCancelled);
		if (!runWriter(&verifySink, &verifyStats, &verifyError)) {
			report.deterministic = false;
			if (!verifySink.errorString().isEmpty()) { verifyError = verifySink.errorString(); }
			report.blockedMessages.push_back(verifyError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "Determinism verification pass failed.") : verifyError);
		} else {
			report.determinismVerified = true;
			report.deterministic = verifySink.digest() == report.sha256;
			if (!report.deterministic) {
				report.blockedMessages.push_back(QCoreApplication::translate("VibeStudioPackageStaging", "Determinism verification found different bytes on a repeat write."));
			}
		}
	}

	if (cancelled() || !report.blockedMessages.isEmpty()) { return report; }
	QByteArray preparedManifest;
	if (request.writeManifest) {
		PackageReadControl manifestControl;
		manifestControl.isCancelled = request.isCancelled;
		manifestControl.progress = [&](const QString& path, qint64 done, qint64 total) {
			if (request.byteProgress) request.byteProgress(PackageWritePhase::Manifest, path, static_cast<quint64>(done), static_cast<quint64>(total));
		};
		preparedManifest = manifestJson(&sourceError, manifestControl);
		if (preparedManifest.isEmpty()) {
			if (!cancelled()) report.blockedMessages << sourceError;
			return report;
		}
	}
	if (request.progress) { request.progress(totalFiles, totalFiles, QString()); }
	if (cancelled()) { return report; }
	if (!verifySources(&sourceError, sourceControl)) {
		if (!cancelled()) { report.blockedMessages << sourceError; }
		return report;
	}
	if (cancelled() || request.dryRun) return report;
	if (request.byteProgress) request.byteProgress(PackageWritePhase::Publish, report.outputPath, 0, 0);
	const auto published = publication.commit(report.bytesWritten, report.sha256);
	report.outputCommitted = published.committed;
	report.cancelled = published.cancelled;
	report.backupPath = published.backupPath;
	report.recoveryPaths = published.recoveryPaths;
	report.warnings << published.warnings;
	if (!published.committed) {
		report.blockedMessages << published.error;
		return report;
	}
	report.overwroteInPlace = destinationExists;
	// The caller must reopen/rebase after a committed save; this plan retains
	// its original reader snapshot and must never bind old offsets to new data.

	if (request.writeManifest) {
		report.manifestPath = request.manifestPath.trimmed().isEmpty()
			? QStringLiteral("%1.manifest.json").arg(report.outputPath)
			: QFileInfo(request.manifestPath).absoluteFilePath();
		QString manifestError;
		QSaveFile manifest(report.manifestPath);
		if (manifest.open(QIODevice::WriteOnly) && manifest.write(preparedManifest) == preparedManifest.size() && manifest.commit()) {
			report.wroteManifest = true;
		} else {
			manifestError = manifest.errorString();
			report.warnings.push_back(manifestError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "Unable to write package manifest.") : manifestError);
		}
	}
	return report;
}

QString PackageStagingModel::nextOperationId()
{
	// Monotonic: ids are never reused, so clearing an operation cannot make a
	// later operation collide with an existing one.
	Q_ASSERT(m_operationSerial < maximumSerial);
	++m_operationSerial;
	return QStringLiteral("stage-%1").arg(m_operationSerial);
}

bool PackageStagingModel::appendOperation(PackageStageOperation operation, QString* error, const PackageReadControl& control, PackageFileImportMode mode)
{
	if (error) { error->clear(); }
	if (!m_loaded) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Load a package before staging changes."); }
		return false;
	}
	const auto stopped = [&] {
		if (!control.isCancelled || !control.isCancelled()) { return false; }
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Package content admission cancelled."); }
		return true;
	};
	if (stopped() || !admitHistoryEdit(true, error)) { return false; }
	PackageStagingContentUsage usage;
	if (!m_contentUsageValid && !contentUsage(&usage, error, control)) { return false; }
	usage = m_contentUsage;
	PackageStagingMetadataUsage metadata;
	if (!m_metadataUsageValid && !metadataUsage(&metadata, error, control)) { return false; }
	metadata = m_metadataUsage;
	if (operation.id.trimmed().isEmpty()) { operation.id = QStringLiteral("stage-%1").arg(m_operationSerial + 1); }
	const QString label = operation.sourceOrdinal >= 0
		? QCoreApplication::translate("VibeStudioPackageStaging", "%1 %2 (source entry %3)").arg(packageStageOperationTypeDisplayName(operation.type), operation.virtualPath).arg(operation.sourceOrdinal + 1)
		: QCoreApplication::translate("VibeStudioPackageStaging", "%1 %2").arg(packageStageOperationTypeDisplayName(operation.type), operation.virtualPath);
	const auto metadataAddition = [&] {
		auto value = operationMetadata(operation); ++value.records;
		if (m_groupDepth == 0) { const auto step = historyMetadata(label); value.records += step.records; value.metadataBytes += step.metadataBytes; }
		return value;
	};
	PackageStagingMetadataUsage addedMetadata;
	qint64 generated = operation.hasInlineBytes ? operation.inlineBytes.size() : 0;
	qint64 fingerprints = 0;
	const bool sourceFile = !operation.hasInlineBytes
		&& (operation.type == PackageStageOperationType::Add || operation.type == PackageStageOperationType::Replace);
	if (sourceFile) {
		const QFileInfo input(operation.sourceFilePath);
		const qint64 size = input.size();
		if (input.isFile() && size >= 0) {
			fingerprints = (size / PackageFileIdentity::chunkBytes + (size % PackageFileIdentity::chunkBytes != 0)) * 32;
			// Reserve known identity metadata before hashing. Snapshot storage paths
			// are checked again after the independent copy has been captured.
			auto identity = std::make_shared<PackageFileIdentity>();
			identity->path = input.absoluteFilePath(); identity->resolvedPath = input.canonicalFilePath();
			operation.sourceIdentity = std::move(identity);
		}
	}
	addedMetadata = metadataAddition();
	const auto fits = [&] {
		return generated <= m_contentLimits.maximumGeneratedBytes - usage.generatedBytes
			&& fingerprints <= m_contentLimits.maximumFingerprintBytes - usage.fingerprintBytes;
	};
	PackageStagingModel* target = this;
	std::unique_ptr<PackageStagingModel> candidate;
	const auto reclaim = [&]() {
		// Reclaim only when the conservative allowance is exhausted. Prepare
		// the successful edit's history transition privately: a refused edit
		// must retain redo, and an open group must remain cancellable.
		candidate = std::make_unique<PackageStagingModel>(*this);
		if (candidate->m_groupDepth == 0) {
			candidate->m_history.resize(candidate->m_historyCursor);
			if (candidate->m_history.size() >= 256) { candidate->m_history.removeFirst(); --candidate->m_historyCursor; }
		}
		candidate->invalidatePlan();
		if (!candidate->contentUsage(&usage, error, control) || !candidate->metadataUsage(&metadata, error, control)) { return false; }
		target = candidate.get(); return true;
	};
	if ((!fits() || !metadataFits(metadata, addedMetadata)) && !reclaim()) { return false; }
	if (generated > m_contentLimits.maximumGeneratedBytes - usage.generatedBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Retained generated content exceeds the %1-byte document limit. Save and reopen the draft to release inline content, or stage smaller assets.").arg(m_contentLimits.maximumGeneratedBytes); }
		return false;
	}
	if (fingerprints > m_contentLimits.maximumFingerprintBytes - usage.fingerprintBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageStaging", "Retained source fingerprints exceed the %1-byte document limit. Use smaller inputs or save a smaller package and reopen it.").arg(m_contentLimits.maximumFingerprintBytes); }
		return false;
	}
	if (!metadataFits(metadata, addedMetadata, error)) { return false; }
	// Own input text as well as payloads: QString::fromRawData can otherwise
	// let a caller alter a staged path after the document accepted it.
	for (auto* value : {&operation.id, &operation.virtualPath, &operation.targetVirtualPath, &operation.sourceFilePath,
		&operation.sourceError, &operation.sourceTreeIdentity, &operation.wadInsertBefore, &operation.wadNamespace}) {
		*value = QString(value->constData(), value->size());
	}
	if (operation.hasInlineBytes) {
		// QByteArray::fromRawData does not own its bytes. Copy at admission;
		// later document/history/snapshot copies share this owned allocation.
		operation.inlineBytes = QByteArray(operation.inlineBytes.constData(), operation.inlineBytes.size());
	} else if (sourceFile) {
		const qint64 remaining = m_contentLimits.maximumFingerprintBytes - usage.fingerprintBytes;
		bool limitExceeded = false;
		operation.sourceIdentity = capturePackageFileIdentity(operation.sourceFilePath, &operation.sourceError, control, remaining, &limitExceeded);
		if (limitExceeded) { if (error) { *error = operation.sourceError; } return false; }
		if (operation.sourceIdentity) {
			operation.sourceModifiedUtc = operation.sourceIdentity->modifiedUtc;
			if (mode == PackageFileImportMode::Snapshot) {
				operation.sourceIdentity = retainPackageFileContent(operation.sourceIdentity, &operation.sourceError, control);
				if (!operation.sourceIdentity) { if (error) { *error = operation.sourceError; } return false; }
			}
		}
		fingerprints = operation.sourceIdentity ? operation.sourceIdentity->chunkHashes.size() : 0;
	}
	if (stopped()) { return false; }
	// Captured identity paths and source errors become known after source work.
	// Recheck them before adoption; failures retain the original history/ids.
	addedMetadata = metadataAddition();
	if (!metadataFits(metadata, addedMetadata) && !candidate && !reclaim()) { return false; }
	if (!metadataFits(metadata, addedMetadata, error)) { return false; }
	if (m_groupDepth == 0 && !candidate) { candidate = std::make_unique<PackageStagingModel>(*this); target = candidate.get(); }
	target->m_operations.push_back(operation); target->invalidatePlan();
	if (m_groupDepth == 0 && !target->admitView(error, control)) { return false; }
	target->nextOperationId();
	target->recordHistory({operation, target->m_operations.size() - 1, true}, label);
	usage.generatedBytes += generated; usage.fingerprintBytes += fingerprints;
	target->m_contentUsage = usage; target->m_contentUsageValid = true;
	// History eviction may have released content. Keep an upper bound until
	// an exact report or a later admission needs to reclaim that allowance.
	target->m_contentUsageExact = false;
	metadata.records += addedMetadata.records; metadata.metadataBytes += addedMetadata.metadataBytes;
	target->m_metadataUsage = metadata; target->m_metadataUsageValid = true; target->m_metadataUsageExact = false;
	if (candidate) { *this = std::move(*candidate); }
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
	case PackageStageOperationType::CreateDirectory:
		return QStringLiteral("create-directory");
	case PackageStageOperationType::RenameDirectory:
		return QStringLiteral("rename-directory");
	case PackageStageOperationType::DeleteDirectory:
		return QStringLiteral("delete-directory");
	}
	return QStringLiteral("add");
}

QString packageStageOperationTypeDisplayName(PackageStageOperationType type)
{
	switch (type) {
	case PackageStageOperationType::Add:
		return QCoreApplication::translate("VibeStudioPackageStaging", "Add");
	case PackageStageOperationType::Replace:
		return QCoreApplication::translate("VibeStudioPackageStaging", "Replace");
	case PackageStageOperationType::Rename:
		return QCoreApplication::translate("VibeStudioPackageStaging", "Rename");
	case PackageStageOperationType::Delete:
		return QCoreApplication::translate("VibeStudioPackageStaging", "Delete");
	case PackageStageOperationType::CreateDirectory:
		return QCoreApplication::translate("VibeStudioPackageStaging", "Create folder");
	case PackageStageOperationType::RenameDirectory:
		return QCoreApplication::translate("VibeStudioPackageStaging", "Rename folder");
	case PackageStageOperationType::DeleteDirectory:
		return QCoreApplication::translate("VibeStudioPackageStaging", "Delete folder");
	}
	return QCoreApplication::translate("VibeStudioPackageStaging", "Add");
}

PackageStageOperationType packageStageOperationTypeFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("create-directory")) { return PackageStageOperationType::CreateDirectory; }
	if (normalized == QStringLiteral("rename-directory")) { return PackageStageOperationType::RenameDirectory; }
	if (normalized == QStringLiteral("delete-directory")) { return PackageStageOperationType::DeleteDirectory; }
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
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Package save-as");
	if (!report.blockedMessages.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioPackageStaging", "Blocked:");
		for (const QString& blocked : report.blockedMessages) {
			lines << QStringLiteral("- %1").arg(blocked);
		}
	}
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Mode: %1").arg(report.dryRun ? QCoreApplication::translate("VibeStudioPackageStaging", "dry run") : QCoreApplication::translate("VibeStudioPackageStaging", "write"));
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Output: %1").arg(report.outputPath.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "not written") : report.outputPath);
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
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "SHA-256: %1").arg(report.sha256.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "not available") : report.sha256);
	lines << QCoreApplication::translate("VibeStudioPackageStaging", "Manifest: %1").arg(report.wroteManifest ? report.manifestPath : QCoreApplication::translate("VibeStudioPackageStaging", "not written"));
	if (report.overwroteInPlace) {
		lines << QCoreApplication::translate("VibeStudioPackageStaging", "Replaced in place, backup: %1").arg(report.backupPath.isEmpty() ? QCoreApplication::translate("VibeStudioPackageStaging", "not written") : report.backupPath);
	}
	for (const auto& path : report.recoveryPaths) {
		lines << QCoreApplication::translate("VibeStudioPackageStaging", "Recovery file: %1").arg(path);
	}
	if (!report.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioPackageStaging", "Warnings:");
		for (const QString& warning : report.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
