#include "core/package_archive.h"

#include <QChar>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace vibestudio {

namespace {

constexpr int kMaximumPackageVirtualPathLength = 4096;
constexpr quint32 kPakSignature = 0x4b434150; // PACK
constexpr quint32 kZipCentralDirectorySignature = 0x02014b50;
constexpr quint32 kZipEndOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 kZipLocalFileSignature = 0x04034b50;

QString normalizedId(QString value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString fileExtensionLower(const QString& fileName)
{
	const QString suffix = QFileInfo(fileName).suffix().toLower();
	return suffix.trimmed();
}

bool containsControlCharacter(const QString& value)
{
	for (QChar ch : value) {
		const ushort code = ch.unicode();
		if (code < 0x20 || code == 0x7f) {
			return true;
		}
	}
	return false;
}

quint16 readLe16(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 2 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<quint16>(bytes[0] | (bytes[1] << 8));
}

quint32 readLe32(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 4 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<quint32>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
}

qint32 readSignedLe32(const QByteArray& data, qsizetype offset)
{
	return static_cast<qint32>(readLe32(data, offset));
}

QString fixedLatin1String(const char* data, qsizetype maxSize)
{
	qsizetype size = 0;
	while (size < maxSize && data[size] != '\0') {
		++size;
	}
	return QString::fromLatin1(data, size).trimmed();
}

QString entryTypeHint(const QString& virtualPath, PackageEntryKind kind)
{
	if (kind == PackageEntryKind::Directory) {
		return QStringLiteral("directory");
	}

	const PackageArchiveFormat nestedFormat = packageArchiveFormatFromFileName(virtualPath);
	if (nestedFormat != PackageArchiveFormat::Unknown) {
		return QStringLiteral("nested-%1").arg(packageArchiveFormatId(nestedFormat));
	}

	const QString ext = QFileInfo(virtualPath).suffix().toLower();
	if (ext.isEmpty()) {
		return QStringLiteral("binary");
	}
	if (QStringList {QStringLiteral("txt"), QStringLiteral("cfg"), QStringLiteral("shader"), QStringLiteral("map"), QStringLiteral("def"), QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("log")}.contains(ext)) {
		return QStringLiteral("text/%1").arg(ext);
	}
	if (QStringList {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("tga"), QStringLiteral("pcx"), QStringLiteral("wal"), QStringLiteral("mip"), QStringLiteral("lmp")}.contains(ext)) {
		return QStringLiteral("image/%1").arg(ext);
	}
	if (QStringList {QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("mp3")}.contains(ext)) {
		return QStringLiteral("audio/%1").arg(ext);
	}
	if (QStringList {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm")}.contains(ext)) {
		return QStringLiteral("model/%1").arg(ext);
	}
	return QStringLiteral("file/%1").arg(ext);
}

QString zipMethodName(quint16 method)
{
	switch (method) {
	case 0:
		return QStringLiteral("stored");
	case 8:
		return QStringLiteral("deflated");
	case 12:
		return QStringLiteral("bzip2");
	case 14:
		return QStringLiteral("lzma");
	case 93:
		return QStringLiteral("zstd");
	default:
		return QStringLiteral("method-%1").arg(method);
	}
}

QDateTime dosDateTimeUtc(quint16 date, quint16 time)
{
	const int year = 1980 + ((date >> 9) & 0x7f);
	const int month = (date >> 5) & 0x0f;
	const int day = date & 0x1f;
	const int hour = (time >> 11) & 0x1f;
	const int minute = (time >> 5) & 0x3f;
	const int second = (time & 0x1f) * 2;
	const QDate qdate(year, month, day);
	const QTime qtime(hour, minute, second);
	if (!qdate.isValid() || !qtime.isValid()) {
		return {};
	}
	return QDateTime(qdate, qtime, QTimeZone::UTC);
}

bool readAt(QFile& file, qint64 offset, qint64 size, QByteArray* out)
{
	if (!out || offset < 0 || size < 0) {
		return false;
	}
	if (!file.seek(offset)) {
		return false;
	}
	*out = file.read(size);
	return out->size() == size;
}

bool entryPathLess(const PackageEntry& left, const PackageEntry& right)
{
	if (left.kind != right.kind) {
		return left.kind == PackageEntryKind::Directory;
	}
	return left.virtualPath.compare(right.virtualPath, Qt::CaseInsensitive) < 0;
}

bool entryPathEquals(const QString& left, const QString& right)
{
	return left.compare(right, Qt::CaseInsensitive) == 0;
}

QString duplicateKey(const QString& path)
{
	return path.toCaseFolded();
}

void addExtractionResult(PackageExtractionReport* report, const PackageExtractionEntryResult& result)
{
	if (!report) {
		return;
	}

	report->entries.push_back(result);
	if (result.error.isEmpty()) {
		++report->processedCount;
	} else {
		++report->errorCount;
		report->warnings.push_back(result.virtualPath.isEmpty() ? result.error : QStringLiteral("%1: %2").arg(result.virtualPath, result.error));
	}
	if (result.kind == PackageEntryKind::Directory) {
		++report->directoryCount;
	}
	if (result.written) {
		++report->writtenCount;
		report->totalBytes += result.bytes;
	}
	if (result.skipped) {
		++report->skippedCount;
		if (!result.message.isEmpty()) {
			report->warnings.push_back(QStringLiteral("%1: %2").arg(result.virtualPath, result.message));
		}
	}
}

QVector<PackageEntry> selectedEntriesForExtraction(const QVector<PackageEntry>& entries, const QStringList& requestedPaths, PackageExtractionReport* report)
{
	QVector<PackageEntry> selected;
	QSet<QString> selectedKeys;

	for (const QString& requestedPath : requestedPaths) {
		const PackageVirtualPath normalized = normalizePackageVirtualPath(requestedPath, false);
		if (!normalized.isSafe()) {
			PackageExtractionEntryResult result;
			result.virtualPath = requestedPath;
			result.error = QStringLiteral("Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue));
			addExtractionResult(report, result);
			continue;
		}

		bool found = false;
		const QString prefix = normalized.normalizedPath + '/';
		for (const PackageEntry& entry : entries) {
			if (!entryPathEquals(entry.virtualPath, normalized.normalizedPath) && !entry.virtualPath.startsWith(prefix, Qt::CaseInsensitive)) {
				continue;
			}
			found = true;
			const QString key = duplicateKey(entry.virtualPath);
			if (!selectedKeys.contains(key)) {
				selectedKeys.insert(key);
				selected.push_back(entry);
			}
		}

		if (!found) {
			PackageExtractionEntryResult result;
			result.virtualPath = normalized.normalizedPath;
			result.error = QStringLiteral("Package entry not found.");
			addExtractionResult(report, result);
		}
	}

	std::sort(selected.begin(), selected.end(), entryPathLess);
	return selected;
}

void addSyntheticDirectories(QVector<PackageEntry>* entries, const QString& sourceArchiveId)
{
	if (!entries) {
		return;
	}

	QSet<QString> seen;
	for (const PackageEntry& entry : *entries) {
		seen.insert(duplicateKey(entry.virtualPath));
	}

	QVector<PackageEntry> directories;
	for (const PackageEntry& entry : *entries) {
		QString parent = packageVirtualPathParent(entry.virtualPath);
		while (!parent.isEmpty()) {
			const QString key = duplicateKey(parent);
			if (!seen.contains(key)) {
				seen.insert(key);
				PackageEntry directory;
				directory.virtualPath = parent;
				directory.kind = PackageEntryKind::Directory;
				directory.typeHint = entryTypeHint(parent, PackageEntryKind::Directory);
				directory.sourceArchiveId = sourceArchiveId;
				directory.storageMethod = QStringLiteral("synthetic");
				directory.readable = false;
				directories.push_back(directory);
			}
			parent = packageVirtualPathParent(parent);
		}
	}

	*entries += directories;
}

QString normalizedDirectoryForCompare(const QString& path)
{
	QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	normalized.replace('\\', '/');
	while (normalized.endsWith('/') && normalized.size() > 1) {
		normalized.chop(1);
	}
#ifdef Q_OS_WIN
	normalized = normalized.toCaseFolded();
#endif
	return normalized;
}

QString normalizedLayerMountPath(const QString& mountPath, QString* error)
{
	if (error) {
		error->clear();
	}

	const QString trimmed = mountPath.trimmed();
	if (trimmed.isEmpty() || trimmed == QStringLiteral("/")) {
		return {};
	}

	PackageVirtualPath normalized = normalizePackageVirtualPath(trimmed, false);
	if (!normalized.isSafe()) {
		if (error) {
			*error = QStringLiteral("Unsafe package mount path: %1").arg(packagePathIssueDisplayName(normalized.issue));
		}
		return {};
	}
	return normalized.normalizedPath;
}

} // namespace

bool PackageVirtualPath::isSafe() const
{
	return issue == PackagePathIssue::None && !normalizedPath.isEmpty();
}

bool PackageArchive::load(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}
	clear();

	const QFileInfo info(path);
	if (!info.exists()) {
		if (error) {
			*error = QStringLiteral("Package path does not exist.");
		}
		return false;
	}

	bool loaded = false;
	const QString absolutePath = info.absoluteFilePath();
	if (info.isDir()) {
		loaded = loadFolder(absolutePath, error);
	} else {
		const PackageArchiveFormat detected = packageArchiveFormatFromFileName(absolutePath);
		switch (detected) {
		case PackageArchiveFormat::Pak:
			loaded = loadPak(absolutePath, error);
			break;
		case PackageArchiveFormat::Wad:
			loaded = loadWad(absolutePath, error);
			break;
		case PackageArchiveFormat::Zip:
		case PackageArchiveFormat::Pk3:
			loaded = loadZipFamily(absolutePath, detected, error);
			break;
		case PackageArchiveFormat::Folder:
		case PackageArchiveFormat::Unknown:
			loaded = loadPak(absolutePath, nullptr) || loadWad(absolutePath, nullptr) || loadZipFamily(absolutePath, PackageArchiveFormat::Zip, nullptr);
			if (!loaded && error) {
				*error = QStringLiteral("Unsupported package format.");
			}
			break;
		}
	}

	if (!loaded) {
		return false;
	}

	finalizeEntries();
	m_open = true;
	return true;
}

void PackageArchive::clear()
{
	m_format = PackageArchiveFormat::Unknown;
	m_sourcePath.clear();
	m_open = false;
	m_entries.clear();
	m_warnings.clear();
}

PackageArchiveFormat PackageArchive::format() const
{
	return m_format;
}

QString PackageArchive::sourcePath() const
{
	return m_sourcePath;
}

bool PackageArchive::isOpen() const
{
	return m_open;
}

QVector<PackageEntry> PackageArchive::entries() const
{
	return m_entries;
}

QVector<PackageLoadWarning> PackageArchive::warnings() const
{
	return m_warnings;
}

PackageArchiveSummary PackageArchive::summary() const
{
	PackageArchiveSummary summary;
	summary.sourcePath = m_sourcePath;
	summary.format = m_format;
	summary.entryCount = m_entries.size();
	summary.warningCount = m_warnings.size();
	for (const PackageEntry& entry : m_entries) {
		if (entry.kind == PackageEntryKind::Directory) {
			++summary.directoryCount;
		} else {
			++summary.fileCount;
			summary.totalSizeBytes += entry.sizeBytes;
		}
		if (entry.nestedArchiveCandidate) {
			++summary.nestedArchiveCount;
		}
	}
	return summary;
}

bool PackageArchive::readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (error) {
		error->clear();
	}
	if (out) {
		out->clear();
	}
	if (!m_open) {
		if (error) {
			*error = QStringLiteral("No package is open.");
		}
		return false;
	}

	const PackageEntry* entry = findEntry(virtualPath);
	if (!entry) {
		if (error) {
			*error = QStringLiteral("Package entry not found.");
		}
		return false;
	}
	if (entry->kind == PackageEntryKind::Directory) {
		if (error) {
			*error = QStringLiteral("Package entry is a directory.");
		}
		return false;
	}
	if (!entry->readable) {
		if (error) {
			*error = entry->note.isEmpty() ? QStringLiteral("Package entry is not readable by the current reader.") : entry->note;
		}
		return false;
	}

	if (m_format == PackageArchiveFormat::Folder) {
		return readFileEntryBytes(*entry, out, error, maxBytes);
	}
	return readOffsetEntryBytes(*entry, out, error, maxBytes);
}

bool PackageArchive::loadFolder(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}

	const QFileInfo rootInfo(path);
	if (!rootInfo.exists() || !rootInfo.isDir()) {
		if (error) {
			*error = QStringLiteral("Folder package not found.");
		}
		return false;
	}

	m_format = PackageArchiveFormat::Folder;
	m_sourcePath = rootInfo.absoluteFilePath();
	const QDir root(m_sourcePath);

	QDirIterator it(m_sourcePath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		const QFileInfo fileInfo = it.fileInfo();
		QString relative = root.relativeFilePath(fileInfo.absoluteFilePath());
		const PackageVirtualPath normalized = normalizePackageVirtualPath(relative, false);
		if (!normalized.isSafe()) {
			addWarning(relative, QStringLiteral("Skipped unsafe folder entry: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			continue;
		}
		if (!packagePathIsInsideDirectory(m_sourcePath, fileInfo.absoluteFilePath())) {
			addWarning(relative, QStringLiteral("Skipped folder entry outside the package root."));
			continue;
		}

		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = PackageEntryKind::File;
		entry.sizeBytes = static_cast<quint64>(std::max<qint64>(0, fileInfo.size()));
		entry.compressedSizeBytes = entry.sizeBytes;
		entry.modifiedUtc = fileInfo.lastModified().toUTC();
		entry.typeHint = entryTypeHint(entry.virtualPath, entry.kind);
		entry.storageMethod = QStringLiteral("file");
		entry.sourceArchiveId = QFileInfo(m_sourcePath).fileName();
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);
		entry.readable = true;
		m_entries.push_back(entry);
	}
	return true;
}

bool PackageArchive::loadPak(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QStringLiteral("Unable to open PAK file.");
		}
		return false;
	}

	const QByteArray header = file.read(12);
	if (header.size() != 12 || readLe32(header, 0) != kPakSignature) {
		if (error) {
			*error = QStringLiteral("Invalid PAK header.");
		}
		return false;
	}

	const qint32 directoryOffset = readSignedLe32(header, 4);
	const qint32 directoryLength = readSignedLe32(header, 8);
	if (directoryOffset < 12 || directoryLength < 0 || directoryLength % 64 != 0 || static_cast<qint64>(directoryOffset) + directoryLength > file.size()) {
		if (error) {
			*error = QStringLiteral("Invalid PAK directory.");
		}
		return false;
	}

	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = QStringLiteral("Unable to seek to PAK directory.");
		}
		return false;
	}

	m_format = PackageArchiveFormat::Pak;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	const int count = directoryLength / 64;
	for (int index = 0; index < count; ++index) {
		const QByteArray record = file.read(64);
		if (record.size() != 64) {
			if (error) {
				*error = QStringLiteral("Unable to read PAK directory record.");
			}
			return false;
		}

		const QString rawName = fixedLatin1String(record.constData(), 56);
		const PackageVirtualPath normalized = normalizePackageVirtualPath(rawName, false);
		if (!normalized.isSafe()) {
			addWarning(rawName, QStringLiteral("Skipped unsafe PAK entry: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			continue;
		}

		const qint32 dataOffset = readSignedLe32(record, 56);
		const qint32 dataSize = readSignedLe32(record, 60);
		if (dataOffset < 0 || dataSize < 0 || static_cast<qint64>(dataOffset) + dataSize > file.size()) {
			addWarning(normalized.normalizedPath, QStringLiteral("Skipped PAK entry with invalid offset or size."));
			continue;
		}

		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = PackageEntryKind::File;
		entry.sizeBytes = static_cast<quint64>(dataSize);
		entry.compressedSizeBytes = entry.sizeBytes;
		entry.dataOffset = dataOffset;
		entry.typeHint = entryTypeHint(entry.virtualPath, entry.kind);
		entry.storageMethod = QStringLiteral("stored");
		entry.sourceArchiveId = QFileInfo(m_sourcePath).fileName();
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);
		entry.readable = true;
		m_entries.push_back(entry);
	}
	return true;
}

bool PackageArchive::loadWad(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QStringLiteral("Unable to open WAD file.");
		}
		return false;
	}

	const QByteArray header = file.read(12);
	const QString magic = QString::fromLatin1(header.constData(), std::min<qsizetype>(4, header.size()));
	if (header.size() != 12 || !(magic == QStringLiteral("IWAD") || magic == QStringLiteral("PWAD") || magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3"))) {
		if (error) {
			*error = QStringLiteral("Invalid WAD header.");
		}
		return false;
	}

	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const qint32 lumpCount = readSignedLe32(header, 4);
	const qint32 directoryOffset = readSignedLe32(header, 8);
	const int recordSize = textureWad ? 32 : 16;
	if (lumpCount < 0 || directoryOffset < 12 || static_cast<qint64>(directoryOffset) + (static_cast<qint64>(lumpCount) * recordSize) > file.size()) {
		if (error) {
			*error = QStringLiteral("Invalid WAD directory.");
		}
		return false;
	}

	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = QStringLiteral("Unable to seek to WAD directory.");
		}
		return false;
	}

	m_format = PackageArchiveFormat::Wad;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	for (int index = 0; index < lumpCount; ++index) {
		const QByteArray record = file.read(recordSize);
		if (record.size() != recordSize) {
			if (error) {
				*error = QStringLiteral("Unable to read WAD directory record.");
			}
			return false;
		}

		const qint32 dataOffset = readSignedLe32(record, 0);
		const qint32 diskSize = readSignedLe32(record, 4);
		const qint32 logicalSize = textureWad ? readSignedLe32(record, 8) : diskSize;
		const quint8 compression = textureWad ? static_cast<quint8>(record[13]) : 0;
		const QString rawName = fixedLatin1String(record.constData() + (textureWad ? 16 : 8), textureWad ? 16 : 8);
		const QString displayName = rawName.isEmpty() ? QStringLiteral("lump-%1").arg(index, 4, 10, QLatin1Char('0')) : rawName;
		const PackageVirtualPath normalized = normalizePackageVirtualPath(displayName, false);
		if (!normalized.isSafe()) {
			addWarning(displayName, QStringLiteral("Skipped unsafe WAD entry: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			continue;
		}
		if (dataOffset < 0 || diskSize < 0 || logicalSize < 0 || static_cast<qint64>(dataOffset) + diskSize > file.size()) {
			addWarning(normalized.normalizedPath, QStringLiteral("Skipped WAD entry with invalid offset or size."));
			continue;
		}

		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = PackageEntryKind::File;
		entry.sizeBytes = static_cast<quint64>(logicalSize);
		entry.compressedSizeBytes = static_cast<quint64>(diskSize);
		entry.dataOffset = dataOffset;
		entry.typeHint = textureWad ? QStringLiteral("wad-texture") : QStringLiteral("wad-lump");
		entry.storageMethod = compression == 0 ? QStringLiteral("stored") : QStringLiteral("compressed-%1").arg(compression);
		entry.sourceArchiveId = QFileInfo(m_sourcePath).fileName();
		entry.readable = compression == 0;
		entry.note = entry.readable ? QString() : QStringLiteral("Compressed WAD lump reading is not implemented yet.");
		m_entries.push_back(entry);
	}
	return true;
}

bool PackageArchive::loadZipFamily(const QString& path, PackageArchiveFormat format, QString* error)
{
	if (error) {
		error->clear();
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QStringLiteral("Unable to open ZIP/PK3 file.");
		}
		return false;
	}

	const qint64 fileSize = file.size();
	const qint64 scanSize = std::min<qint64>(fileSize, 66000);
	QByteArray tail;
	if (!readAt(file, fileSize - scanSize, scanSize, &tail)) {
		if (error) {
			*error = QStringLiteral("Unable to read ZIP end directory.");
		}
		return false;
	}

	int eocdOffsetInTail = -1;
	for (int offset = tail.size() - 22; offset >= 0; --offset) {
		if (readLe32(tail, offset) == kZipEndOfCentralDirectorySignature) {
			eocdOffsetInTail = offset;
			break;
		}
	}
	if (eocdOffsetInTail < 0) {
		if (error) {
			*error = QStringLiteral("ZIP end directory not found.");
		}
		return false;
	}

	const quint16 entryCount = readLe16(tail, eocdOffsetInTail + 10);
	const quint32 centralDirectorySize = readLe32(tail, eocdOffsetInTail + 12);
	const quint32 centralDirectoryOffset = readLe32(tail, eocdOffsetInTail + 16);
	if (centralDirectoryOffset == std::numeric_limits<quint32>::max() || centralDirectorySize == std::numeric_limits<quint32>::max()
		|| static_cast<qint64>(centralDirectoryOffset) + centralDirectorySize > fileSize) {
		if (error) {
			*error = QStringLiteral("ZIP64 archives are not supported by the current reader.");
		}
		return false;
	}

	QByteArray centralDirectory;
	if (!readAt(file, centralDirectoryOffset, centralDirectorySize, &centralDirectory)) {
		if (error) {
			*error = QStringLiteral("Unable to read ZIP central directory.");
		}
		return false;
	}

	m_format = format;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	qsizetype cursor = 0;
	for (int index = 0; index < entryCount; ++index) {
		if (cursor + 46 > centralDirectory.size() || readLe32(centralDirectory, cursor) != kZipCentralDirectorySignature) {
			if (error) {
				*error = QStringLiteral("Invalid ZIP central directory record.");
			}
			return false;
		}

		const quint16 flags = readLe16(centralDirectory, cursor + 8);
		const quint16 method = readLe16(centralDirectory, cursor + 10);
		const quint16 modifiedTime = readLe16(centralDirectory, cursor + 12);
		const quint16 modifiedDate = readLe16(centralDirectory, cursor + 14);
		const quint32 compressedSize = readLe32(centralDirectory, cursor + 20);
		const quint32 uncompressedSize = readLe32(centralDirectory, cursor + 24);
		const quint16 nameLength = readLe16(centralDirectory, cursor + 28);
		const quint16 extraLength = readLe16(centralDirectory, cursor + 30);
		const quint16 commentLength = readLe16(centralDirectory, cursor + 32);
		const quint32 localHeaderOffset = readLe32(centralDirectory, cursor + 42);
		const qsizetype recordSize = 46 + nameLength + extraLength + commentLength;
		if (cursor + recordSize > centralDirectory.size()) {
			if (error) {
				*error = QStringLiteral("Invalid ZIP central directory name length.");
			}
			return false;
		}

		const QString rawName = QString::fromUtf8(centralDirectory.constData() + cursor + 46, nameLength);
		const bool directoryEntry = rawName.endsWith('/');
		const PackageVirtualPath normalized = normalizePackageVirtualPath(rawName, directoryEntry);
		if (!normalized.isSafe()) {
			addWarning(rawName, QStringLiteral("Skipped unsafe ZIP entry: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			cursor += recordSize;
			continue;
		}

		qint64 dataOffset = -1;
		if (!directoryEntry && localHeaderOffset < static_cast<quint32>(fileSize)) {
			QByteArray localHeader;
			if (readAt(file, localHeaderOffset, 30, &localHeader) && readLe32(localHeader, 0) == kZipLocalFileSignature) {
				const quint16 localNameLength = readLe16(localHeader, 26);
				const quint16 localExtraLength = readLe16(localHeader, 28);
				dataOffset = static_cast<qint64>(localHeaderOffset) + 30 + localNameLength + localExtraLength;
			}
		}

		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = directoryEntry ? PackageEntryKind::Directory : PackageEntryKind::File;
		entry.sizeBytes = uncompressedSize;
		entry.compressedSizeBytes = compressedSize;
		entry.dataOffset = dataOffset;
		entry.modifiedUtc = dosDateTimeUtc(modifiedDate, modifiedTime);
		entry.typeHint = entryTypeHint(entry.virtualPath, entry.kind);
		entry.storageMethod = zipMethodName(method);
		entry.sourceArchiveId = QFileInfo(m_sourcePath).fileName();
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);
		entry.readable = entry.kind == PackageEntryKind::File && method == 0 && (flags & 0x1) == 0 && dataOffset >= 0;
		if (entry.kind == PackageEntryKind::Directory) {
			entry.readable = false;
		} else if ((flags & 0x1) != 0) {
			entry.note = QStringLiteral("Encrypted ZIP entries are not readable.");
		} else if (method != 0) {
			entry.note = QStringLiteral("ZIP entry uses %1 compression; listing is supported, byte reading is deferred.").arg(entry.storageMethod);
		}
		m_entries.push_back(entry);
		cursor += recordSize;
	}
	return true;
}

bool PackageArchive::readFileEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const
{
	QString nativeRelative = entry.virtualPath;
	nativeRelative.replace('/', QDir::separator());
	const QString filePath = QDir(m_sourcePath).filePath(nativeRelative);
	if (!packagePathIsInsideDirectory(m_sourcePath, filePath)) {
		if (error) {
			*error = QStringLiteral("Folder entry escapes the package root.");
		}
		return false;
	}

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QStringLiteral("Unable to open folder entry.");
		}
		return false;
	}
	const qint64 available = std::max<qint64>(0, file.size());
	const qint64 toRead = maxBytes >= 0 ? std::min(maxBytes, available) : available;
	if (out) {
		*out = file.read(toRead);
		if (out->size() != toRead) {
			if (error) {
				*error = QStringLiteral("Unable to read folder entry.");
			}
			return false;
		}
	}
	return true;
}

bool PackageArchive::readOffsetEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const
{
	const quint64 storedSize = entry.compressedSizeBytes > 0 ? entry.compressedSizeBytes : entry.sizeBytes;
	if (entry.dataOffset < 0 || storedSize > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
		if (error) {
			*error = QStringLiteral("Invalid package entry offset or size.");
		}
		return false;
	}

	QFile file(m_sourcePath);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QStringLiteral("Unable to open package file.");
		}
		return false;
	}
	if (entry.dataOffset + static_cast<qint64>(storedSize) > file.size()) {
		if (error) {
			*error = QStringLiteral("Package entry extends beyond the file.");
		}
		return false;
	}
	const qint64 toRead = maxBytes >= 0 ? std::min(maxBytes, static_cast<qint64>(storedSize)) : static_cast<qint64>(storedSize);
	if (!file.seek(entry.dataOffset)) {
		if (error) {
			*error = QStringLiteral("Unable to seek to package entry.");
		}
		return false;
	}
	if (out) {
		*out = file.read(toRead);
		if (out->size() != toRead) {
			if (error) {
				*error = QStringLiteral("Unable to read package entry.");
			}
			return false;
		}
	}
	return true;
}

const PackageEntry* PackageArchive::findEntry(const QString& virtualPath) const
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		return nullptr;
	}
	for (const PackageEntry& entry : m_entries) {
		if (entry.virtualPath.compare(normalized.normalizedPath, Qt::CaseInsensitive) == 0) {
			return &entry;
		}
	}
	return nullptr;
}

void PackageArchive::finalizeEntries()
{
	const QString sourceId = QFileInfo(m_sourcePath).fileName();
	addSyntheticDirectories(&m_entries, sourceId);

	QSet<QString> seen;
	for (PackageEntry& entry : m_entries) {
		entry.typeHint = entry.typeHint.isEmpty() ? entryTypeHint(entry.virtualPath, entry.kind) : entry.typeHint;
		entry.sourceArchiveId = entry.sourceArchiveId.isEmpty() ? sourceId : entry.sourceArchiveId;
		entry.nestedArchiveCandidate = entry.nestedArchiveCandidate || packageEntryLooksNestedArchive(entry.virtualPath);
		const QString key = duplicateKey(entry.virtualPath);
		if (seen.contains(key)) {
			addWarning(entry.virtualPath, QStringLiteral("Duplicate package entry path; first match will be used for byte reads."));
		} else {
			seen.insert(key);
		}
	}

	std::sort(m_entries.begin(), m_entries.end(), entryPathLess);
}

void PackageArchive::addWarning(const QString& virtualPath, const QString& message)
{
	if (message.trimmed().isEmpty()) {
		return;
	}
	m_warnings.push_back({virtualPath.trimmed(), message.trimmed()});
}

bool PackageArchiveSession::setPrimaryLayer(const PackageMountLayer& layer, QString* error)
{
	PackageMountLayer normalized = layer;
	if (!normalizeLayer(&normalized, error)) {
		return false;
	}
	m_primaryLayer = normalized;
	m_hasPrimaryLayer = true;
	m_mountedLayers.clear();
	return true;
}

bool PackageArchiveSession::hasPrimaryLayer() const
{
	return m_hasPrimaryLayer;
}

PackageMountLayer PackageArchiveSession::primaryLayer() const
{
	return m_hasPrimaryLayer ? m_primaryLayer : PackageMountLayer {};
}

bool PackageArchiveSession::pushMountedLayer(const PackageMountLayer& layer, QString* error)
{
	PackageMountLayer normalized = layer;
	if (!normalizeLayer(&normalized, error)) {
		return false;
	}
	m_mountedLayers.push_back(normalized);
	return true;
}

bool PackageArchiveSession::popMountedLayer()
{
	if (m_mountedLayers.isEmpty()) {
		return false;
	}
	m_mountedLayers.pop_back();
	return true;
}

void PackageArchiveSession::clearMountedLayers()
{
	m_mountedLayers.clear();
}

bool PackageArchiveSession::hasMountedLayer() const
{
	return !m_mountedLayers.isEmpty();
}

QVector<PackageMountLayer> PackageArchiveSession::mountedLayers() const
{
	return m_mountedLayers;
}

PackageMountLayer PackageArchiveSession::currentLayer() const
{
	if (!m_mountedLayers.isEmpty()) {
		return m_mountedLayers.back();
	}
	return primaryLayer();
}

int PackageArchiveSession::depth() const
{
	return (m_hasPrimaryLayer ? 1 : 0) + m_mountedLayers.size();
}

bool PackageArchiveSession::normalizeLayer(PackageMountLayer* layer, QString* error) const
{
	if (error) {
		error->clear();
	}
	if (!layer) {
		if (error) {
			*error = QStringLiteral("Package layer is missing.");
		}
		return false;
	}

	QString mountError;
	layer->mountPath = normalizedLayerMountPath(layer->mountPath, &mountError);
	if (!mountError.isEmpty()) {
		if (error) {
			*error = mountError;
		}
		return false;
	}

	if (layer->id.trimmed().isEmpty()) {
		layer->id = layer->sourcePath.trimmed().isEmpty() ? packageArchiveFormatId(layer->format) : QFileInfo(layer->sourcePath).fileName();
	}
	if (layer->displayName.trimmed().isEmpty()) {
		layer->displayName = layer->id;
	}
	return true;
}

bool PackageExtractionReport::succeeded() const
{
	return !cancelled && errorCount == 0;
}

QString packageArchiveFormatId(PackageArchiveFormat format)
{
	switch (format) {
	case PackageArchiveFormat::Folder:
		return QStringLiteral("folder");
	case PackageArchiveFormat::Pak:
		return QStringLiteral("pak");
	case PackageArchiveFormat::Wad:
		return QStringLiteral("wad");
	case PackageArchiveFormat::Zip:
		return QStringLiteral("zip");
	case PackageArchiveFormat::Pk3:
		return QStringLiteral("pk3");
	case PackageArchiveFormat::Unknown:
		break;
	}
	return QStringLiteral("unknown");
}

QString packageArchiveFormatDisplayName(PackageArchiveFormat format)
{
	switch (format) {
	case PackageArchiveFormat::Folder:
		return QStringLiteral("Folder");
	case PackageArchiveFormat::Pak:
		return QStringLiteral("PAK");
	case PackageArchiveFormat::Wad:
		return QStringLiteral("WAD");
	case PackageArchiveFormat::Zip:
		return QStringLiteral("ZIP");
	case PackageArchiveFormat::Pk3:
		return QStringLiteral("PK3");
	case PackageArchiveFormat::Unknown:
		break;
	}
	return QStringLiteral("Unknown");
}

PackageArchiveFormat packageArchiveFormatFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("folder") || normalized == QStringLiteral("directory")) {
		return PackageArchiveFormat::Folder;
	}
	if (normalized == QStringLiteral("pak")) {
		return PackageArchiveFormat::Pak;
	}
	if (normalized == QStringLiteral("wad") || normalized == QStringLiteral("wad2") || normalized == QStringLiteral("wad3")) {
		return PackageArchiveFormat::Wad;
	}
	if (normalized == QStringLiteral("zip")) {
		return PackageArchiveFormat::Zip;
	}
	if (normalized == QStringLiteral("pk3")) {
		return PackageArchiveFormat::Pk3;
	}
	return PackageArchiveFormat::Unknown;
}

PackageArchiveFormat packageArchiveFormatFromFileName(const QString& fileName)
{
	const QFileInfo info(fileName);
	if (info.exists() && info.isDir()) {
		return PackageArchiveFormat::Folder;
	}

	const QString ext = fileExtensionLower(fileName);
	if (ext == QStringLiteral("pak")) {
		return PackageArchiveFormat::Pak;
	}
	if (ext == QStringLiteral("wad") || ext == QStringLiteral("wad2") || ext == QStringLiteral("wad3")) {
		return PackageArchiveFormat::Wad;
	}
	if (ext == QStringLiteral("pk3")) {
		return PackageArchiveFormat::Pk3;
	}
	if (ext == QStringLiteral("zip")) {
		return PackageArchiveFormat::Zip;
	}
	return PackageArchiveFormat::Unknown;
}

QVector<PackageArchiveFormatDescriptor> packageArchiveFormatDescriptors()
{
	return {
		{
			PackageArchiveFormat::Folder,
			QStringLiteral("folder"),
			QStringLiteral("Folder"),
			{},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract-source")},
			QStringLiteral("Directory-backed package session for read-only project/package browsing."),
		},
		{
			PackageArchiveFormat::Pak,
			QStringLiteral("pak"),
			QStringLiteral("Quake PAK"),
			{QStringLiteral("pak")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("future-write")},
			QStringLiteral("idTech2-style PAK archive interface; reader implementation lands in the next package slice."),
		},
		{
			PackageArchiveFormat::Wad,
			QStringLiteral("wad"),
			QStringLiteral("Doom/Quake WAD"),
			{QStringLiteral("wad"), QStringLiteral("wad2"), QStringLiteral("wad3")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract")},
			QStringLiteral("Doom-family and Quake texture WAD interface placeholder with shared path safety."),
		},
		{
			PackageArchiveFormat::Zip,
			QStringLiteral("zip"),
			QStringLiteral("ZIP"),
			{QStringLiteral("zip")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("nested-mount")},
			QStringLiteral("ZIP-family interface for future generic archive browsing."),
		},
		{
			PackageArchiveFormat::Pk3,
			QStringLiteral("pk3"),
			QStringLiteral("Quake III PK3"),
			{QStringLiteral("pk3")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("nested-mount")},
			QStringLiteral("idTech3 PK3 interface over the ZIP-family package model."),
		},
	};
}

QString packageEntryKindId(PackageEntryKind kind)
{
	switch (kind) {
	case PackageEntryKind::File:
		return QStringLiteral("file");
	case PackageEntryKind::Directory:
		return QStringLiteral("directory");
	}
	return QStringLiteral("file");
}

QString packageEntryKindDisplayName(PackageEntryKind kind)
{
	switch (kind) {
	case PackageEntryKind::File:
		return QStringLiteral("File");
	case PackageEntryKind::Directory:
		return QStringLiteral("Directory");
	}
	return QStringLiteral("File");
}

QString packagePathIssueId(PackagePathIssue issue)
{
	switch (issue) {
	case PackagePathIssue::None:
		return QStringLiteral("safe");
	case PackagePathIssue::Empty:
		return QStringLiteral("empty");
	case PackagePathIssue::AbsolutePath:
		return QStringLiteral("absolute-path");
	case PackagePathIssue::DriveQualifiedPath:
		return QStringLiteral("drive-qualified-path");
	case PackagePathIssue::TraversalSegment:
		return QStringLiteral("traversal-segment");
	case PackagePathIssue::CurrentDirectorySegment:
		return QStringLiteral("current-directory-segment");
	case PackagePathIssue::Colon:
		return QStringLiteral("colon");
	case PackagePathIssue::ControlCharacter:
		return QStringLiteral("control-character");
	case PackagePathIssue::TooLong:
		return QStringLiteral("too-long");
	}
	return QStringLiteral("unknown");
}

QString packagePathIssueDisplayName(PackagePathIssue issue)
{
	switch (issue) {
	case PackagePathIssue::None:
		return QStringLiteral("safe");
	case PackagePathIssue::Empty:
		return QStringLiteral("empty path");
	case PackagePathIssue::AbsolutePath:
		return QStringLiteral("absolute paths are not allowed inside packages");
	case PackagePathIssue::DriveQualifiedPath:
		return QStringLiteral("drive-qualified paths are not allowed inside packages");
	case PackagePathIssue::TraversalSegment:
		return QStringLiteral("parent-directory traversal is not allowed inside packages");
	case PackagePathIssue::CurrentDirectorySegment:
		return QStringLiteral("current-directory segments are not allowed inside packages");
	case PackagePathIssue::Colon:
		return QStringLiteral("colon characters are not allowed inside package paths");
	case PackagePathIssue::ControlCharacter:
		return QStringLiteral("control characters are not allowed inside package paths");
	case PackagePathIssue::TooLong:
		return QStringLiteral("package path is too long");
	}
	return QStringLiteral("unknown package path issue");
}

// Derived from PakFu's archive/path_safety.h rules at commit
// c82dfb0ef0b5d7442e243ace8cd83bc45f82f257, but reimplemented for
// VibeStudio's core package model and explicit issue reporting.
PackageVirtualPath normalizePackageVirtualPath(const QString& path, bool preserveTrailingSlash)
{
	PackageVirtualPath result;
	result.originalPath = path;

	QString trimmed = path.trimmed();
	result.trailingSlash = preserveTrailingSlash && (trimmed.endsWith('/') || trimmed.endsWith('\\'));

	if (trimmed.isEmpty()) {
		result.issue = PackagePathIssue::Empty;
		return result;
	}
	if (trimmed.size() > kMaximumPackageVirtualPathLength) {
		result.issue = PackagePathIssue::TooLong;
		return result;
	}
	if (containsControlCharacter(trimmed)) {
		result.issue = PackagePathIssue::ControlCharacter;
		return result;
	}
	if (trimmed.startsWith('/') || trimmed.startsWith('\\')) {
		result.issue = PackagePathIssue::AbsolutePath;
		return result;
	}

	const QRegularExpression driveQualified(QStringLiteral("^[A-Za-z]:"));
	if (driveQualified.match(trimmed).hasMatch()) {
		result.issue = PackagePathIssue::DriveQualifiedPath;
		return result;
	}
	if (trimmed.contains(':')) {
		result.issue = PackagePathIssue::Colon;
		return result;
	}

	trimmed.replace('\\', '/');
	const QStringList rawParts = trimmed.split('/', Qt::KeepEmptyParts);
	for (const QString& part : rawParts) {
		if (part == QStringLiteral("..")) {
			result.issue = PackagePathIssue::TraversalSegment;
			return result;
		}
		if (part == QStringLiteral(".")) {
			result.issue = PackagePathIssue::CurrentDirectorySegment;
			return result;
		}
	}

	QString normalized = QDir::cleanPath(trimmed);
	normalized.replace('\\', '/');
	while (normalized.startsWith('/')) {
		normalized.remove(0, 1);
	}
	if (normalized == QStringLiteral(".")) {
		normalized.clear();
	}
	if (result.trailingSlash && !normalized.isEmpty() && !normalized.endsWith('/')) {
		normalized += '/';
	}

	if (normalized.isEmpty()) {
		result.issue = PackagePathIssue::Empty;
		return result;
	}

	result.normalizedPath = normalized;
	result.issue = PackagePathIssue::None;
	return result;
}

bool isSafePackageVirtualPath(const QString& path)
{
	return normalizePackageVirtualPath(path).isSafe();
}

QString packageVirtualPathFileName(const QString& path)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(path, false);
	if (!normalized.isSafe()) {
		return {};
	}
	const int slash = normalized.normalizedPath.lastIndexOf('/');
	return slash < 0 ? normalized.normalizedPath : normalized.normalizedPath.mid(slash + 1);
}

QString packageVirtualPathParent(const QString& path)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(path, false);
	if (!normalized.isSafe()) {
		return {};
	}
	const int slash = normalized.normalizedPath.lastIndexOf('/');
	return slash < 0 ? QString() : normalized.normalizedPath.left(slash);
}

bool packageEntryLooksNestedArchive(const QString& virtualPath)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		return false;
	}
	const PackageArchiveFormat format = packageArchiveFormatFromFileName(normalized.normalizedPath);
	return format == PackageArchiveFormat::Pak
		|| format == PackageArchiveFormat::Wad
		|| format == PackageArchiveFormat::Zip
		|| format == PackageArchiveFormat::Pk3;
}

bool packagePathIsInsideDirectory(const QString& rootDirectory, const QString& candidatePath)
{
	const QString root = normalizedDirectoryForCompare(rootDirectory);
	const QString candidate = normalizedDirectoryForCompare(candidatePath);
	if (root.isEmpty() || candidate.isEmpty()) {
		return false;
	}
	if (candidate == root) {
		return true;
	}
	const QString rootWithSlash = root.endsWith('/') ? root : root + '/';
	return candidate.startsWith(rootWithSlash);
}

QString safePackageOutputPath(const QString& rootDirectory, const QString& virtualPath, QString* error)
{
	if (error) {
		error->clear();
	}

	const QString root = normalizedDirectoryForCompare(rootDirectory);
	if (root.isEmpty()) {
		if (error) {
			*error = QStringLiteral("Output root is empty.");
		}
		return {};
	}

	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		if (error) {
			*error = QStringLiteral("Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue));
		}
		return {};
	}

	QString nativeRelative = normalized.normalizedPath;
	nativeRelative.replace('/', QDir::separator());
	const QString candidate = QDir(root).filePath(nativeRelative);
	if (!packagePathIsInsideDirectory(root, candidate)) {
		if (error) {
			*error = QStringLiteral("Output path escapes the selected root.");
		}
		return {};
	}
	return QDir::cleanPath(candidate);
}

PackageExtractionReport extractPackageEntries(const PackageArchive& archive, const PackageExtractionRequest& request, PackageExtractionProgressCallback progress)
{
	PackageExtractionReport report;
	report.sourcePath = archive.sourcePath();
	report.targetDirectory = QDir::cleanPath(QFileInfo(request.targetDirectory).absoluteFilePath());
	report.extractAll = request.extractAll;
	report.dryRun = request.dryRun;
	report.overwriteExisting = request.overwriteExisting;

	auto failBeforeEntries = [&report](const QString& message) {
		PackageExtractionEntryResult result;
		result.outputPath = report.targetDirectory;
		result.error = message;
		addExtractionResult(&report, result);
	};

	if (!archive.isOpen()) {
		failBeforeEntries(QStringLiteral("No package is open."));
		return report;
	}
	if (request.targetDirectory.trimmed().isEmpty()) {
		failBeforeEntries(QStringLiteral("Output directory is required."));
		return report;
	}

	const QFileInfo targetInfo(report.targetDirectory);
	if (targetInfo.exists() && !targetInfo.isDir()) {
		failBeforeEntries(QStringLiteral("Output path exists but is not a directory."));
		return report;
	}
	if (!request.dryRun && !QDir().mkpath(report.targetDirectory)) {
		failBeforeEntries(QStringLiteral("Unable to create output directory."));
		return report;
	}

	const QVector<PackageEntry> archiveEntries = archive.entries();
	QVector<PackageEntry> selectedEntries;
	if (request.extractAll || request.virtualPaths.isEmpty()) {
		selectedEntries = archiveEntries;
		std::sort(selectedEntries.begin(), selectedEntries.end(), entryPathLess);
	} else {
		selectedEntries = selectedEntriesForExtraction(archiveEntries, request.virtualPaths, &report);
	}
	report.requestedCount = selectedEntries.size() + report.errorCount;

	for (const PackageEntry& entry : selectedEntries) {
		PackageExtractionEntryResult result;
		result.virtualPath = entry.virtualPath;
		result.kind = entry.kind;
		result.bytes = entry.sizeBytes;
		result.dryRun = request.dryRun;

		QString pathError;
		result.outputPath = safePackageOutputPath(report.targetDirectory, entry.virtualPath, &pathError);
		if (!pathError.isEmpty()) {
			result.error = pathError;
			addExtractionResult(&report, result);
		} else if (entry.kind == PackageEntryKind::Directory) {
			const QFileInfo outputInfo(result.outputPath);
			if (outputInfo.exists() && !outputInfo.isDir()) {
				result.error = QStringLiteral("Cannot create directory because a file already exists at the output path.");
			} else if (request.dryRun) {
				result.message = QStringLiteral("Would create directory.");
			} else if (!QDir().mkpath(result.outputPath)) {
				result.error = QStringLiteral("Unable to create output directory.");
			} else {
				result.written = true;
				result.message = QStringLiteral("Created directory.");
			}
			addExtractionResult(&report, result);
		} else if (!entry.readable) {
			result.error = entry.note.isEmpty() ? QStringLiteral("Package entry is not readable by the current reader.") : entry.note;
			addExtractionResult(&report, result);
		} else {
			const QFileInfo outputInfo(result.outputPath);
			if (outputInfo.exists() && outputInfo.isDir()) {
				result.error = QStringLiteral("Cannot write file because a directory already exists at the output path.");
			} else if (outputInfo.exists() && !request.overwriteExisting) {
				result.skipped = true;
				result.message = QStringLiteral("Output exists; pass overwrite to replace it.");
			} else if (request.dryRun) {
				result.message = outputInfo.exists() ? QStringLiteral("Would overwrite file.") : QStringLiteral("Would write file.");
			} else {
				QByteArray bytes;
				QString readError;
				if (!archive.readEntryBytes(entry.virtualPath, &bytes, &readError)) {
					result.error = readError.isEmpty() ? QStringLiteral("Unable to read package entry.") : readError;
				} else {
					const QString parentPath = QFileInfo(result.outputPath).absolutePath();
					if (!QDir().mkpath(parentPath)) {
						result.error = QStringLiteral("Unable to create output parent directory.");
					} else {
						QSaveFile file(result.outputPath);
						if (!file.open(QIODevice::WriteOnly)) {
							result.error = QStringLiteral("Unable to open output file for writing.");
						} else if (file.write(bytes) != bytes.size()) {
							result.error = QStringLiteral("Unable to write all output bytes.");
							file.cancelWriting();
						} else if (!file.commit()) {
							result.error = QStringLiteral("Unable to commit output file.");
						} else {
							result.bytes = static_cast<quint64>(std::max<qsizetype>(0, bytes.size()));
							result.written = true;
							result.message = outputInfo.exists() ? QStringLiteral("Overwrote file.") : QStringLiteral("Wrote file.");
						}
					}
				}
			}
			addExtractionResult(&report, result);
		}

		if (progress && !progress(report.entries.back(), report)) {
			report.cancelled = true;
			report.warnings.push_back(QStringLiteral("Package extraction cancelled."));
			break;
		}
	}

	return report;
}

QString packageExtractionReportText(const PackageExtractionReport& report)
{
	QStringList lines;
	lines << QStringLiteral("Package extraction");
	lines << QStringLiteral("Source: %1").arg(QDir::toNativeSeparators(report.sourcePath));
	lines << QStringLiteral("Target: %1").arg(QDir::toNativeSeparators(report.targetDirectory));
	lines << QStringLiteral("Mode: %1").arg(report.dryRun ? QStringLiteral("dry-run") : QStringLiteral("write"));
	lines << QStringLiteral("Overwrite: %1").arg(report.overwriteExisting ? QStringLiteral("yes") : QStringLiteral("no"));
	lines << QStringLiteral("State: %1").arg(report.cancelled ? QStringLiteral("cancelled") : (report.succeeded() ? QStringLiteral("completed") : QStringLiteral("failed")));
	lines << QStringLiteral("Requested entries: %1").arg(report.requestedCount);
	lines << QStringLiteral("Processed entries: %1").arg(report.processedCount);
	lines << QStringLiteral("Written entries: %1").arg(report.writtenCount);
	lines << QStringLiteral("Created directories: %1").arg(report.directoryCount);
	lines << QStringLiteral("Skipped entries: %1").arg(report.skippedCount);
	lines << QStringLiteral("Errors: %1").arg(report.errorCount);
	lines << QStringLiteral("Bytes written: %1").arg(report.totalBytes);
	lines << QStringLiteral("Output paths");
	for (const PackageExtractionEntryResult& result : report.entries) {
		QString state = QStringLiteral("planned");
		if (!result.error.isEmpty()) {
			state = QStringLiteral("failed");
		} else if (result.skipped) {
			state = QStringLiteral("skipped");
		} else if (result.dryRun) {
			state = result.kind == PackageEntryKind::Directory ? QStringLiteral("would create") : QStringLiteral("would write");
		} else if (result.kind == PackageEntryKind::Directory && result.written) {
			state = QStringLiteral("created");
		} else if (result.written) {
			state = QStringLiteral("wrote");
		}
		lines << QStringLiteral("- %1: %2 -> %3")
			.arg(state,
				result.virtualPath.isEmpty() ? QStringLiteral("(package)") : result.virtualPath,
				result.outputPath.isEmpty() ? QStringLiteral("(no output path)") : QDir::toNativeSeparators(result.outputPath));
		if (!result.message.isEmpty()) {
			lines << QStringLiteral("  %1").arg(result.message);
		}
		if (!result.error.isEmpty()) {
			lines << QStringLiteral("  Error: %1").arg(result.error);
		}
	}
	if (!report.warnings.isEmpty()) {
		lines << QStringLiteral("Warnings");
		for (const QString& warning : report.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
