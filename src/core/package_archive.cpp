#include "core/package_archive.h"

#include "core/deflate.h"

#include <QChar>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace vibestudio {

namespace {

QString packageText(const char* source)
{
	return QCoreApplication::translate("VibeStudioPackageArchive", source);
}

constexpr int kMaximumPackageVirtualPathLength = 4096;
constexpr quint32 kPakSignature = 0x4b434150; // PACK

// PKWARE .ZIP File Format Specification (APPNOTE.TXT), sections 4.3.6 - 4.3.16.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
constexpr quint32 kZipCentralDirectorySignature = 0x02014b50;
constexpr quint32 kZipEndOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 kZipLocalFileSignature = 0x04034b50;
constexpr quint32 kZipDataDescriptorSignature = 0x08074b50;
constexpr quint32 kZip64EndOfCentralDirectorySignature = 0x06064b50;
constexpr quint32 kZip64EndOfCentralDirectoryLocatorSignature = 0x07064b50;
constexpr quint16 kZip64ExtraHeaderId = 0x0001;
constexpr quint32 kZip32Sentinel = 0xffffffffu;
constexpr quint16 kZip16Sentinel = 0xffffu;

// Hard ceiling on a single inflate so a hostile central directory cannot ask
// the reader to materialise an absurd buffer.
constexpr qint64 kMaximumInflateBytes = 1024ll * 1024ll * 1024ll;

QString normalizedId(QString value)
{
	return value.trimmed().toLower().replace('_', '-');
}

// Equivalent to QFileInfo::suffix() but without building a QFileInfo: this runs
// once per archive entry, and a large pk3 has tens of thousands of them.
QString fileExtensionLower(const QString& fileName)
{
	const qsizetype slash = std::max(fileName.lastIndexOf('/'), fileName.lastIndexOf('\\'));
	const qsizetype dot = fileName.lastIndexOf('.');
	if (dot < 0 || dot <= slash) {
		return {};
	}
	return fileName.mid(dot + 1).trimmed().toLower();
}

// Extension-only classification. Unlike packageArchiveFormatFromFileName it
// never touches the filesystem, which matters for virtual paths (a package path
// must not be classified by whatever happens to exist in the process's working
// directory) and for throughput.
PackageArchiveFormat formatFromExtension(const QString& fileName)
{
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

quint64 readLe64(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 8 > data.size()) {
		return 0;
	}
	const quint64 low = readLe32(data, offset);
	const quint64 high = readLe32(data, offset + 4);
	return low | (high << 32);
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

	const PackageArchiveFormat nestedFormat = formatFromExtension(virtualPath);
	if (nestedFormat != PackageArchiveFormat::Unknown) {
		return QStringLiteral("nested-%1").arg(packageArchiveFormatId(nestedFormat));
	}

	const QString ext = fileExtensionLower(virtualPath);
	if (ext.isEmpty()) {
		return QStringLiteral("binary");
	}

	static const QStringList kTextExtensions = {
		QStringLiteral("txt"), QStringLiteral("cfg"), QStringLiteral("shader"), QStringLiteral("map"),
		QStringLiteral("def"), QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("log"),
	};
	static const QStringList kImageExtensions = {
		QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("tga"),
		QStringLiteral("pcx"), QStringLiteral("wal"), QStringLiteral("mip"), QStringLiteral("lmp"),
	};
	static const QStringList kAudioExtensions = {
		QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("mp3"),
	};
	static const QStringList kModelExtensions = {
		QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"),
		QStringLiteral("mdr"), QStringLiteral("iqm"),
	};

	if (kTextExtensions.contains(ext)) {
		return QStringLiteral("text/%1").arg(ext);
	}
	if (kImageExtensions.contains(ext)) {
		return QStringLiteral("image/%1").arg(ext);
	}
	if (kAudioExtensions.contains(ext)) {
		return QStringLiteral("audio/%1").arg(ext);
	}
	if (kModelExtensions.contains(ext)) {
		return QStringLiteral("model/%1").arg(ext);
	}
	return QStringLiteral("file/%1").arg(ext);
}

// Stable technical tokens from APPNOTE.TXT section 4.4.5 (compression method).
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

bool isDeflatedStorage(const QString& storageMethod)
{
	return storageMethod == QStringLiteral("deflated");
}

// APPNOTE.TXT section 4.4.6: MS-DOS packed date/time.
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
			result.error = packageText("Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue));
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
			result.error = packageText("Package entry not found.");
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

QString stripTrailingSlashes(QString value)
{
	while (value.size() > 1 && value.endsWith('/')) {
		value.chop(1);
	}
	return value;
}

// Fully resolves a path that exists on disk. QFileInfo::canonicalFilePath
// resolves symbolic links, but on Windows it leaves NTFS junctions alone even
// though a junction redirects a directory just as effectively, so junctions are
// resolved segment by segment there. The loop is bounded so a junction cycle
// cannot hang the reader.
QString resolveExistingPath(const QString& existingPath)
{
	QString current = existingPath;
	for (int depth = 0; depth < 40; ++depth) {
		const QFileInfo info(current.endsWith(':') ? current + QLatin1Char('/') : current);
		QString canonical = info.canonicalFilePath();
		if (canonical.isEmpty()) {
			canonical = info.absoluteFilePath();
		}
		canonical = QDir::cleanPath(canonical);
		canonical.replace('\\', '/');
		canonical = stripTrailingSlashes(canonical);

#ifdef Q_OS_WIN
		const QStringList segments = canonical.split('/');
		QString accumulated;
		bool redirected = false;
		for (qsizetype index = 0; index < segments.size(); ++index) {
			accumulated = index == 0 ? segments.at(0) : accumulated + QLatin1Char('/') + segments.at(index);
			if (accumulated.isEmpty()) {
				continue;
			}
			const QFileInfo segment(accumulated.endsWith(':') ? accumulated + QLatin1Char('/') : accumulated);
			if (!segment.isJunction()) {
				continue;
			}
			const QString target = segment.junctionTarget();
			if (target.isEmpty()) {
				continue;
			}
			QString resolved = QDir::cleanPath(QFileInfo(target).absoluteFilePath());
			resolved.replace('\\', '/');
			resolved = stripTrailingSlashes(resolved);
			for (qsizetype rest = index + 1; rest < segments.size(); ++rest) {
				resolved += QLatin1Char('/') + segments.at(rest);
			}
			current = resolved;
			redirected = true;
			break;
		}
		if (redirected) {
			continue;
		}
#endif
		return canonical;
	}
	return current;
}

// Resolves the longest existing prefix of `path` so symlinks and junctions
// inside the tree cannot be used to make a path look contained when the real
// target is elsewhere. Segments that do not exist yet (extraction creates them)
// are appended unresolved.
QString canonicalizedAbsolutePath(const QString& path)
{
	if (path.trimmed().isEmpty()) {
		return {};
	}

	QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	normalized.replace('\\', '/');
	normalized = stripTrailingSlashes(normalized);

	QStringList pending;
	QString probe = normalized;
	while (!probe.isEmpty()) {
		QString probePath = probe;
		if (probePath.endsWith(':')) {
			probePath += '/';
		}
		const QFileInfo info(probePath);
		if (info.exists()) {
			QString canonical = resolveExistingPath(probe);
			for (const QString& segment : pending) {
				if (segment.isEmpty()) {
					continue;
				}
				canonical = canonical.endsWith('/') ? canonical + segment : canonical + '/' + segment;
			}
			return stripTrailingSlashes(canonical);
		}

		const qsizetype slash = probe.lastIndexOf('/');
		if (slash < 0) {
			break;
		}
		pending.prepend(probe.mid(slash + 1));
		probe = slash == 0 ? QStringLiteral("/") : probe.left(slash);
		if (probe == QStringLiteral("/")) {
			const QFileInfo rootInfo(probe);
			if (!rootInfo.exists()) {
				break;
			}
		}
	}

	return normalized;
}

QString comparableDirectoryPath(const QString& path)
{
	QString normalized = canonicalizedAbsolutePath(path);
#ifdef Q_OS_WIN
	normalized = normalized.toCaseFolded();
#endif
	return normalized;
}

// Microsoft, "Naming Files, Paths, and Namespaces": the DOS device names are
// reserved with or without an extension, and a segment may not end in a space
// or a period.
bool isReservedDeviceSegment(const QString& segment)
{
	QString base = segment;
	const qsizetype dot = base.indexOf('.');
	if (dot >= 0) {
		base = base.left(dot);
	}
	base = base.trimmed().toUpper();
	if (base.isEmpty()) {
		return false;
	}
	static const QStringList kReservedNames = {
		QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
	};
	if (kReservedNames.contains(base)) {
		return true;
	}
	if (base.size() == 4 && (base.startsWith(QStringLiteral("COM")) || base.startsWith(QStringLiteral("LPT")))) {
		const QChar digit = base.at(3);
		return digit >= QLatin1Char('1') && digit <= QLatin1Char('9');
	}
	return false;
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
			*error = packageText("Unsafe package mount path: %1").arg(packagePathIssueDisplayName(normalized.issue));
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
			*error = packageText("Package path does not exist.");
		}
		return false;
	}

	const QString absolutePath = info.absoluteFilePath();
	if (info.isDir()) {
		if (!loadFolder(absolutePath, error)) {
			clear();
			return false;
		}
		finalizeEntries();
		m_open = true;
		return true;
	}

	// Trust the magic bytes over the extension so a mislabelled .pk3 that is
	// really a PAK, or an extensionless file, still opens. The extension is only
	// a tiebreaker (ZIP vs PK3 share a container) and a last resort.
	const PackageArchiveFormat byExtension = packageArchiveFormatFromFileName(absolutePath);
	PackageArchiveFormat sniffed = packageArchiveFormatFromContent(absolutePath);
	if (sniffed == PackageArchiveFormat::Zip && byExtension == PackageArchiveFormat::Pk3) {
		sniffed = PackageArchiveFormat::Pk3;
	}

	QVector<PackageArchiveFormat> candidates;
	const auto addCandidate = [&candidates](PackageArchiveFormat candidate) {
		if (candidate == PackageArchiveFormat::Unknown || candidate == PackageArchiveFormat::Folder) {
			return;
		}
		if (!candidates.contains(candidate)) {
			candidates.push_back(candidate);
		}
	};
	addCandidate(sniffed);
	addCandidate(byExtension);
	addCandidate(PackageArchiveFormat::Pak);
	addCandidate(PackageArchiveFormat::Wad);
	addCandidate(PackageArchiveFormat::Zip);

	QString firstError;
	for (PackageArchiveFormat candidate : candidates) {
		// Every attempt starts from a clean reader: the loaders publish entries
		// as they parse, so a truncated file must not leave a half-populated,
		// mixed-format listing behind for the next attempt (or for the caller).
		clear();
		QString attemptError;
		bool attempted = false;
		switch (candidate) {
		case PackageArchiveFormat::Pak:
			attempted = loadPak(absolutePath, &attemptError);
			break;
		case PackageArchiveFormat::Wad:
			attempted = loadWad(absolutePath, &attemptError);
			break;
		case PackageArchiveFormat::Zip:
		case PackageArchiveFormat::Pk3:
			attempted = loadZipFamily(absolutePath, candidate, &attemptError);
			break;
		case PackageArchiveFormat::Folder:
		case PackageArchiveFormat::Unknown:
			break;
		}
		if (attempted) {
			finalizeEntries();
			m_open = true;
			return true;
		}
		if (firstError.isEmpty()) {
			firstError = attemptError;
		}
	}

	clear();
	if (error) {
		*error = firstError.isEmpty() ? packageText("Unsupported package format.") : firstError;
	}
	return false;
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
			*error = packageText("No package is open.");
		}
		return false;
	}

	const PackageEntry* entry = findEntry(virtualPath);
	if (!entry) {
		if (error) {
			*error = packageText("Package entry not found.");
		}
		return false;
	}
	if (entry->kind == PackageEntryKind::Directory) {
		if (error) {
			*error = packageText("Package entry is a directory.");
		}
		return false;
	}
	if (!entry->readable) {
		if (error) {
			*error = entry->note.isEmpty() ? packageText("Package entry is not readable by the current reader.") : entry->note;
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
			*error = packageText("Folder package not found.");
		}
		return false;
	}

	m_format = PackageArchiveFormat::Folder;
	m_sourcePath = rootInfo.absoluteFilePath();
	const QDir root(m_sourcePath);
	const QString sourceId = rootInfo.fileName();

	QDirIterator it(m_sourcePath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		const QFileInfo fileInfo = it.fileInfo();
		QString relative = root.relativeFilePath(fileInfo.absoluteFilePath());
		const PackageVirtualPath normalized = normalizePackageVirtualPath(relative, false);
		if (!normalized.isSafe()) {
			addWarning(relative, packageText("Skipped unsafe folder entry: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			continue;
		}
		if (!packagePathIsInsideDirectory(m_sourcePath, fileInfo.absoluteFilePath())) {
			addWarning(relative, packageText("Skipped folder entry outside the package root."));
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
		entry.sourceArchiveId = sourceId;
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);
		entry.readable = true;
		m_entries.push_back(entry);
	}
	return true;
}

// idTech2 PACK layout: "PACK", int32 directory offset, int32 directory length,
// then 64-byte records of { char name[56]; int32 offset; int32 size; }.
// Documented by the Quake source release and the Quake Standards Group PAK
// specification (https://quakewiki.org/wiki/.pak).
bool PackageArchive::loadPak(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = packageText("Unable to open PAK file.");
		}
		return false;
	}

	const QByteArray header = file.read(12);
	if (header.size() != 12 || readLe32(header, 0) != kPakSignature) {
		if (error) {
			*error = packageText("Invalid PAK header.");
		}
		return false;
	}

	const qint32 directoryOffset = readSignedLe32(header, 4);
	const qint32 directoryLength = readSignedLe32(header, 8);
	if (directoryOffset < 12 || directoryLength < 0 || directoryLength % 64 != 0 || static_cast<qint64>(directoryOffset) + directoryLength > file.size()) {
		if (error) {
			*error = packageText("Invalid PAK directory.");
		}
		return false;
	}

	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = packageText("Unable to seek to PAK directory.");
		}
		return false;
	}

	// Parse into a local list so a truncated directory cannot publish a partial
	// entry set under a PAK format tag.
	QVector<PackageEntry> parsed;
	QVector<PackageLoadWarning> parsedWarnings;
	const int count = directoryLength / 64;
	for (int index = 0; index < count; ++index) {
		const QByteArray record = file.read(64);
		if (record.size() != 64) {
			if (error) {
				*error = packageText("Unable to read PAK directory record.");
			}
			return false;
		}

		const QString rawName = fixedLatin1String(record.constData(), 56);
		const PackageVirtualPath normalized = normalizePackageVirtualPath(rawName, false);
		if (!normalized.isSafe()) {
			parsedWarnings.push_back({rawName.trimmed(), packageText("Skipped unsafe PAK entry: %1").arg(packagePathIssueDisplayName(normalized.issue))});
			continue;
		}

		const qint32 dataOffset = readSignedLe32(record, 56);
		const qint32 dataSize = readSignedLe32(record, 60);
		if (dataOffset < 0 || dataSize < 0 || static_cast<qint64>(dataOffset) + dataSize > file.size()) {
			parsedWarnings.push_back({normalized.normalizedPath, packageText("Skipped PAK entry with invalid offset or size.")});
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
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);
		entry.readable = true;
		parsed.push_back(entry);
	}

	m_format = PackageArchiveFormat::Pak;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	const QString sourceId = QFileInfo(m_sourcePath).fileName();
	for (PackageEntry& entry : parsed) {
		entry.sourceArchiveId = sourceId;
	}
	m_entries = parsed;
	m_warnings += parsedWarnings;
	return true;
}

// Doom IWAD/PWAD directories (id Software's Doom source release, the Unofficial
// Doom Specs v1.666 section 2) and Quake/Half-Life WAD2/WAD3 texture wads
// (https://quakewiki.org/wiki/WAD).
bool PackageArchive::loadWad(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = packageText("Unable to open WAD file.");
		}
		return false;
	}

	const QByteArray header = file.read(12);
	const QString magic = QString::fromLatin1(header.constData(), std::min<qsizetype>(4, header.size()));
	if (header.size() != 12 || !(magic == QStringLiteral("IWAD") || magic == QStringLiteral("PWAD") || magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3"))) {
		if (error) {
			*error = packageText("Invalid WAD header.");
		}
		return false;
	}

	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const qint32 lumpCount = readSignedLe32(header, 4);
	const qint32 directoryOffset = readSignedLe32(header, 8);
	const int recordSize = textureWad ? 32 : 16;
	if (lumpCount < 0 || directoryOffset < 12 || static_cast<qint64>(directoryOffset) + (static_cast<qint64>(lumpCount) * recordSize) > file.size()) {
		if (error) {
			*error = packageText("Invalid WAD directory.");
		}
		return false;
	}

	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = packageText("Unable to seek to WAD directory.");
		}
		return false;
	}

	QVector<PackageEntry> parsed;
	QVector<PackageLoadWarning> parsedWarnings;
	for (int index = 0; index < lumpCount; ++index) {
		const QByteArray record = file.read(recordSize);
		if (record.size() != recordSize) {
			if (error) {
				*error = packageText("Unable to read WAD directory record.");
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
			parsedWarnings.push_back({displayName.trimmed(), packageText("Skipped unsafe WAD entry: %1").arg(packagePathIssueDisplayName(normalized.issue))});
			continue;
		}
		if (dataOffset < 0 || diskSize < 0 || logicalSize < 0 || static_cast<qint64>(dataOffset) + diskSize > file.size()) {
			parsedWarnings.push_back({normalized.normalizedPath, packageText("Skipped WAD entry with invalid offset or size.")});
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
		entry.readable = compression == 0;
		entry.note = entry.readable ? QString() : packageText("WAD2/WAD3 compressed lumps are listed but not decoded.");
		parsed.push_back(entry);
	}

	m_format = PackageArchiveFormat::Wad;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	const QString sourceId = QFileInfo(m_sourcePath).fileName();
	for (PackageEntry& entry : parsed) {
		entry.sourceArchiveId = sourceId;
	}
	m_entries = parsed;
	m_warnings += parsedWarnings;
	return true;
}

// ZIP/PK3 central directory reader, implemented from the PKWARE .ZIP File
// Format Specification (APPNOTE.TXT):
// - 4.3.16 end of central directory record
// - 4.3.14 / 4.3.15 ZIP64 end of central directory record and locator
// - 4.3.12 central directory file header
// - 4.5.3 ZIP64 extended information extra field (header id 0x0001)
bool PackageArchive::loadZipFamily(const QString& path, PackageArchiveFormat format, QString* error)
{
	if (error) {
		error->clear();
	}

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = packageText("Unable to open ZIP/PK3 file.");
		}
		return false;
	}

	const qint64 fileSize = file.size();
	if (fileSize < 22) {
		if (error) {
			*error = packageText("ZIP end directory not found.");
		}
		return false;
	}

	// 64 KiB comment window plus the 22-byte record.
	const qint64 scanSize = std::min<qint64>(fileSize, 66000);
	const qint64 tailStart = fileSize - scanSize;
	QByteArray tail;
	if (!readAt(file, tailStart, scanSize, &tail)) {
		if (error) {
			*error = packageText("Unable to read ZIP end directory.");
		}
		return false;
	}

	qsizetype eocdOffsetInTail = -1;
	for (qsizetype offset = tail.size() - 22; offset >= 0; --offset) {
		if (readLe32(tail, offset) == kZipEndOfCentralDirectorySignature) {
			eocdOffsetInTail = offset;
			break;
		}
	}
	if (eocdOffsetInTail < 0) {
		if (error) {
			*error = packageText("ZIP end directory not found.");
		}
		return false;
	}

	const qint64 eocdAbsoluteOffset = tailStart + eocdOffsetInTail;
	quint32 diskNumber = readLe16(tail, eocdOffsetInTail + 4);
	quint32 centralDirectoryDisk = readLe16(tail, eocdOffsetInTail + 6);
	quint64 totalEntries = readLe16(tail, eocdOffsetInTail + 10);
	quint64 centralDirectorySize = readLe32(tail, eocdOffsetInTail + 12);
	quint64 centralDirectoryOffset = readLe32(tail, eocdOffsetInTail + 16);

	bool zip64 = false;
	const qint64 locatorOffset = eocdAbsoluteOffset - 20;
	if (locatorOffset >= 0) {
		QByteArray locator;
		if (readAt(file, locatorOffset, 20, &locator) && readLe32(locator, 0) == kZip64EndOfCentralDirectoryLocatorSignature) {
			const quint64 zip64RecordOffset = readLe64(locator, 8);
			if (zip64RecordOffset + 56 > static_cast<quint64>(fileSize)) {
				if (error) {
					*error = packageText("ZIP64 end of central directory record is out of range.");
				}
				return false;
			}
			QByteArray zip64Record;
			if (!readAt(file, static_cast<qint64>(zip64RecordOffset), 56, &zip64Record) || readLe32(zip64Record, 0) != kZip64EndOfCentralDirectorySignature) {
				if (error) {
					*error = packageText("Invalid ZIP64 end of central directory record.");
				}
				return false;
			}
			zip64 = true;
			diskNumber = readLe32(zip64Record, 16);
			centralDirectoryDisk = readLe32(zip64Record, 20);
			totalEntries = readLe64(zip64Record, 32);
			centralDirectorySize = readLe64(zip64Record, 40);
			centralDirectoryOffset = readLe64(zip64Record, 48);
		}
	}

	if (!zip64 && (centralDirectoryOffset == kZip32Sentinel || centralDirectorySize == kZip32Sentinel || totalEntries == kZip16Sentinel)) {
		if (error) {
			*error = packageText("ZIP archive uses ZIP64 values but has no ZIP64 end of central directory record.");
		}
		return false;
	}
	if (centralDirectorySize > static_cast<quint64>(std::numeric_limits<qsizetype>::max())
		|| centralDirectoryOffset > static_cast<quint64>(fileSize)
		|| centralDirectoryOffset + centralDirectorySize > static_cast<quint64>(fileSize)) {
		if (error) {
			*error = packageText("ZIP central directory is out of range.");
		}
		return false;
	}

	QByteArray centralDirectory;
	if (!readAt(file, static_cast<qint64>(centralDirectoryOffset), static_cast<qint64>(centralDirectorySize), &centralDirectory)) {
		if (error) {
			*error = packageText("Unable to read ZIP central directory.");
		}
		return false;
	}

	QVector<PackageEntry> parsed;
	QVector<PackageLoadWarning> parsedWarnings;
	if (diskNumber != 0 || centralDirectoryDisk != 0) {
		parsedWarnings.push_back({QString(), packageText("Multi-disk ZIP archives are not supported; only the current file is read.")});
	}

	qsizetype cursor = 0;
	quint64 index = 0;
	for (; index < totalEntries; ++index) {
		if (cursor + 46 > centralDirectory.size() || readLe32(centralDirectory, cursor) != kZipCentralDirectorySignature) {
			if (error) {
				*error = packageText("Invalid ZIP central directory record.");
			}
			return false;
		}

		const quint16 flags = readLe16(centralDirectory, cursor + 8);
		const quint16 method = readLe16(centralDirectory, cursor + 10);
		const quint16 modifiedTime = readLe16(centralDirectory, cursor + 12);
		const quint16 modifiedDate = readLe16(centralDirectory, cursor + 14);
		const quint32 crc = readLe32(centralDirectory, cursor + 16);
		const quint32 compressedSize32 = readLe32(centralDirectory, cursor + 20);
		const quint32 uncompressedSize32 = readLe32(centralDirectory, cursor + 24);
		const quint16 nameLength = readLe16(centralDirectory, cursor + 28);
		const quint16 extraLength = readLe16(centralDirectory, cursor + 30);
		const quint16 commentLength = readLe16(centralDirectory, cursor + 32);
		const quint16 diskStart16 = readLe16(centralDirectory, cursor + 34);
		const quint32 localHeaderOffset32 = readLe32(centralDirectory, cursor + 42);
		const qsizetype recordSize = 46 + static_cast<qsizetype>(nameLength) + extraLength + commentLength;
		if (cursor + recordSize > centralDirectory.size()) {
			if (error) {
				*error = packageText("Invalid ZIP central directory name length.");
			}
			return false;
		}

		quint64 compressedSize = compressedSize32;
		quint64 uncompressedSize = uncompressedSize32;
		quint64 localHeaderOffset = localHeaderOffset32;
		quint32 diskStart = diskStart16;

		// APPNOTE.TXT 4.5.3: the ZIP64 extended information field carries only
		// those values whose 32/16-bit counterpart holds the sentinel, in this
		// fixed order.
		const bool needsZip64Size = uncompressedSize32 == kZip32Sentinel;
		const bool needsZip64Compressed = compressedSize32 == kZip32Sentinel;
		const bool needsZip64Offset = localHeaderOffset32 == kZip32Sentinel;
		const bool needsZip64Disk = diskStart16 == kZip16Sentinel;
		bool zip64FieldComplete = !(needsZip64Size || needsZip64Compressed || needsZip64Offset || needsZip64Disk);
		if (!zip64FieldComplete) {
			qsizetype extraCursor = cursor + 46 + nameLength;
			const qsizetype extraEnd = extraCursor + extraLength;
			while (extraCursor + 4 <= extraEnd) {
				const quint16 headerId = readLe16(centralDirectory, extraCursor);
				const quint16 dataSize = readLe16(centralDirectory, extraCursor + 2);
				const qsizetype dataStart = extraCursor + 4;
				const qsizetype dataEnd = dataStart + dataSize;
				if (dataEnd > extraEnd) {
					break;
				}
				if (headerId == kZip64ExtraHeaderId) {
					qsizetype field = dataStart;
					bool complete = true;
					if (needsZip64Size) {
						if (field + 8 > dataEnd) {
							complete = false;
						} else {
							uncompressedSize = readLe64(centralDirectory, field);
							field += 8;
						}
					}
					if (complete && needsZip64Compressed) {
						if (field + 8 > dataEnd) {
							complete = false;
						} else {
							compressedSize = readLe64(centralDirectory, field);
							field += 8;
						}
					}
					if (complete && needsZip64Offset) {
						if (field + 8 > dataEnd) {
							complete = false;
						} else {
							localHeaderOffset = readLe64(centralDirectory, field);
							field += 8;
						}
					}
					if (complete && needsZip64Disk) {
						if (field + 4 > dataEnd) {
							complete = false;
						} else {
							diskStart = readLe32(centralDirectory, field);
							field += 4;
						}
					}
					zip64FieldComplete = complete;
					break;
				}
				extraCursor = dataEnd;
			}
		}

		const QString rawName = QString::fromUtf8(centralDirectory.constData() + cursor + 46, nameLength);
		const bool directoryEntry = rawName.endsWith('/');
		const PackageVirtualPath normalized = normalizePackageVirtualPath(rawName, directoryEntry);
		if (!normalized.isSafe()) {
			parsedWarnings.push_back({rawName.trimmed(), packageText("Skipped unsafe ZIP entry: %1").arg(packagePathIssueDisplayName(normalized.issue))});
			cursor += recordSize;
			continue;
		}
		if (!zip64FieldComplete) {
			parsedWarnings.push_back({normalized.normalizedPath, packageText("Skipped ZIP entry with an incomplete ZIP64 extended information field.")});
			cursor += recordSize;
			continue;
		}
		if (diskStart != 0) {
			parsedWarnings.push_back({normalized.normalizedPath, packageText("Skipped ZIP entry stored on another disk of a multi-disk archive.")});
			cursor += recordSize;
			continue;
		}

		qint64 dataOffset = -1;
		if (!directoryEntry && localHeaderOffset + 30 <= static_cast<quint64>(fileSize)) {
			QByteArray localHeader;
			if (readAt(file, static_cast<qint64>(localHeaderOffset), 30, &localHeader) && readLe32(localHeader, 0) == kZipLocalFileSignature) {
				const quint16 localNameLength = readLe16(localHeader, 26);
				const quint16 localExtraLength = readLe16(localHeader, 28);
				const quint64 candidate = localHeaderOffset + 30 + localNameLength + localExtraLength;
				if (candidate + compressedSize <= static_cast<quint64>(fileSize)) {
					dataOffset = static_cast<qint64>(candidate);
				}
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
		entry.crc32 = crc;
		entry.hasCrc32 = entry.kind == PackageEntryKind::File;
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);

		const bool encrypted = (flags & 0x1) != 0;
		const bool supportedMethod = method == 0 || method == 8;
		entry.readable = entry.kind == PackageEntryKind::File && supportedMethod && !encrypted && dataOffset >= 0;
		if (entry.kind == PackageEntryKind::Directory) {
			entry.readable = false;
		} else if (encrypted) {
			entry.note = packageText("Encrypted ZIP entries are not readable; this reader does not implement ZIP decryption.");
		} else if (!supportedMethod) {
			entry.note = packageText("ZIP entry uses %1 compression, which this reader does not decode; listing only.").arg(entry.storageMethod);
		} else if (dataOffset < 0) {
			entry.note = packageText("ZIP local file header could not be located for this entry.");
		}
		parsed.push_back(entry);
		cursor += recordSize;
	}

	m_format = format;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	const QString sourceId = QFileInfo(m_sourcePath).fileName();
	for (PackageEntry& entry : parsed) {
		entry.sourceArchiveId = sourceId;
	}
	m_entries = parsed;
	m_warnings += parsedWarnings;
	return true;
}

bool PackageArchive::readFileEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const
{
	QString nativeRelative = entry.virtualPath;
	nativeRelative.replace('/', QDir::separator());
	const QString filePath = QDir(m_sourcePath).filePath(nativeRelative);
	if (!packagePathIsInsideDirectory(m_sourcePath, filePath)) {
		if (error) {
			*error = packageText("Folder entry escapes the package root.");
		}
		return false;
	}

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = packageText("Unable to open folder entry.");
		}
		return false;
	}
	const qint64 available = std::max<qint64>(0, file.size());
	const qint64 toRead = maxBytes >= 0 ? std::min(maxBytes, available) : available;
	if (out) {
		*out = file.read(toRead);
		if (out->size() != toRead) {
			if (error) {
				*error = packageText("Unable to read folder entry.");
			}
			return false;
		}
	}
	return true;
}

bool PackageArchive::readOffsetEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const
{
	const bool deflated = isDeflatedStorage(entry.storageMethod);
	const quint64 storedSize = deflated
		? entry.compressedSizeBytes
		: (entry.compressedSizeBytes > 0 ? entry.compressedSizeBytes : entry.sizeBytes);
	if (entry.dataOffset < 0 || storedSize > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
		if (error) {
			*error = packageText("Invalid package entry offset or size.");
		}
		return false;
	}
	if (deflated && entry.sizeBytes > static_cast<quint64>(kMaximumInflateBytes)) {
		if (error) {
			*error = packageText("Compressed package entry is too large to inflate in memory.");
		}
		return false;
	}

	QFile file(m_sourcePath);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = packageText("Unable to open package file.");
		}
		return false;
	}
	if (entry.dataOffset + static_cast<qint64>(storedSize) > file.size()) {
		if (error) {
			*error = packageText("Package entry extends beyond the file.");
		}
		return false;
	}
	if (!file.seek(entry.dataOffset)) {
		if (error) {
			*error = packageText("Unable to seek to package entry.");
		}
		return false;
	}

	if (!deflated) {
		const qint64 toRead = maxBytes >= 0 ? std::min(maxBytes, static_cast<qint64>(storedSize)) : static_cast<qint64>(storedSize);
		if (out) {
			*out = file.read(toRead);
			if (out->size() != toRead) {
				if (error) {
					*error = packageText("Unable to read package entry.");
				}
				return false;
			}
			// A stored entry carries a CRC in the central directory just like a
			// deflated one, so a truncated or corrupted archive is caught here
			// rather than surfacing as a broken texture much later. Only a full
			// read can be checked; a capped preview read cannot.
			const bool readWholeEntry = toRead == static_cast<qint64>(storedSize);
			if (entry.hasCrc32 && readWholeEntry && crc32Bytes(*out) != entry.crc32) {
				if (error) {
					*error = packageText("Package entry failed its CRC check; the archive is damaged.");
				}
				// Hand back nothing rather than the damaged bytes: a caller that
				// checks only for non-empty output must not be able to use them.
				out->clear();
				return false;
			}
		}
		return true;
	}

	// The DEFLATE stream has to be inflated as a whole: it is not seekable and
	// the CRC-32 only covers the complete payload. `expectedSize` caps the
	// decoder's output so a lying central directory cannot drive unbounded
	// growth, and `maxBytes` then truncates the preview.
	const QByteArray compressed = file.read(static_cast<qint64>(storedSize));
	if (compressed.size() != static_cast<qsizetype>(storedSize)) {
		if (error) {
			*error = packageText("Unable to read compressed package entry.");
		}
		return false;
	}

	const InflateResult inflated = inflateRaw(compressed, static_cast<qint64>(entry.sizeBytes));
	if (!inflated.ok) {
		if (error) {
			*error = inflated.error.isEmpty()
				? packageText("Unable to inflate the compressed package entry.")
				: packageText("Unable to inflate the compressed package entry: %1").arg(inflated.error);
		}
		return false;
	}
	if (static_cast<quint64>(inflated.data.size()) != entry.sizeBytes) {
		if (error) {
			*error = packageText("Inflated package entry size does not match the central directory (%1 of %2 bytes).")
				.arg(QString::number(inflated.data.size()), QString::number(entry.sizeBytes));
		}
		return false;
	}

	const bool partial = maxBytes >= 0 && maxBytes < static_cast<qint64>(entry.sizeBytes);
	if (entry.hasCrc32 && !partial) {
		const quint32 actual = crc32Bytes(inflated.data);
		if (actual != entry.crc32) {
			if (error) {
				*error = packageText("CRC-32 mismatch for package entry (expected %1, got %2); the archive is corrupt.")
					.arg(QString::number(entry.crc32, 16), QString::number(actual, 16));
			}
			return false;
		}
	}

	if (out) {
		*out = partial ? inflated.data.left(maxBytes) : inflated.data;
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
			addWarning(entry.virtualPath, packageText("Duplicate package entry path; first match will be used for byte reads."));
		} else {
			seen.insert(key);
		}
	}

	// Stability is load-bearing, not cosmetic. entryPathLess treats two entries
	// with the same folded path as equivalent, and duplicate paths really do
	// occur: loadWad names entries by lump name, so every Doom map in a PWAD
	// contributes its own THINGS, LINEDEFS, SECTORS, ... entry. findEntry
	// returns the first match in this vector and the warning above promises
	// that it is the archive-order first, so the tie has to be broken by the
	// on-disk order that the loop above walked.
	std::stable_sort(m_entries.begin(), m_entries.end(), entryPathLess);
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
	m_primary.layer = normalized;
	m_primary.archive.reset();
	m_hasPrimaryLayer = true;
	m_mountedLayers.clear();
	m_indexDirty = true;
	return true;
}

bool PackageArchiveSession::hasPrimaryLayer() const
{
	return m_hasPrimaryLayer;
}

PackageMountLayer PackageArchiveSession::primaryLayer() const
{
	return m_hasPrimaryLayer ? m_primary.layer : PackageMountLayer {};
}

bool PackageArchiveSession::pushMountedLayer(const PackageMountLayer& layer, QString* error)
{
	PackageMountLayer normalized = layer;
	if (!normalizeLayer(&normalized, error)) {
		return false;
	}
	m_mountedLayers.push_back({normalized, {}});
	m_indexDirty = true;
	return true;
}

bool PackageArchiveSession::popMountedLayer()
{
	if (m_mountedLayers.isEmpty()) {
		return false;
	}
	m_mountedLayers.pop_back();
	m_indexDirty = true;
	return true;
}

void PackageArchiveSession::clearMountedLayers()
{
	m_mountedLayers.clear();
	m_indexDirty = true;
}

bool PackageArchiveSession::hasMountedLayer() const
{
	return !m_mountedLayers.isEmpty();
}

QVector<PackageMountLayer> PackageArchiveSession::mountedLayers() const
{
	QVector<PackageMountLayer> layers;
	layers.reserve(m_mountedLayers.size());
	for (const LayerState& state : m_mountedLayers) {
		layers.push_back(state.layer);
	}
	return layers;
}

PackageMountLayer PackageArchiveSession::currentLayer() const
{
	if (!m_mountedLayers.isEmpty()) {
		return m_mountedLayers.back().layer;
	}
	return primaryLayer();
}

int PackageArchiveSession::depth() const
{
	return (m_hasPrimaryLayer ? 1 : 0) + static_cast<int>(m_mountedLayers.size());
}

void PackageArchiveSession::clear()
{
	m_primary = LayerState {};
	m_hasPrimaryLayer = false;
	m_mountedLayers.clear();
	m_indexDirty = true;
	m_mergedEntries.clear();
	m_entryOwner.clear();
}

bool PackageArchiveSession::openPrimaryArchive(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}

	auto archive = std::make_shared<PackageArchive>();
	if (!archive->load(path, error)) {
		return false;
	}

	PackageMountLayer layer;
	layer.sourcePath = archive->sourcePath();
	layer.format = archive->format();
	layer.entryCount = static_cast<int>(archive->entries().size());
	layer.id = QFileInfo(layer.sourcePath).fileName();
	layer.displayName = layer.id;
	layer.readOnly = true;
	if (!normalizeLayer(&layer, error)) {
		return false;
	}

	m_mountedLayers.clear();
	m_primary.layer = layer;
	m_primary.archive = archive;
	m_hasPrimaryLayer = true;
	m_indexDirty = true;
	return true;
}

bool PackageArchiveSession::mountArchive(const QString& path, const QString& mountPath, QString* error)
{
	if (error) {
		error->clear();
	}

	auto archive = std::make_shared<PackageArchive>();
	if (!archive->load(path, error)) {
		return false;
	}

	PackageMountLayer layer;
	layer.sourcePath = archive->sourcePath();
	layer.mountPath = mountPath;
	layer.format = archive->format();
	layer.entryCount = static_cast<int>(archive->entries().size());
	layer.id = uniqueLayerId(QFileInfo(layer.sourcePath).fileName());
	layer.displayName = QFileInfo(layer.sourcePath).fileName();
	layer.readOnly = true;
	if (!normalizeLayer(&layer, error)) {
		return false;
	}

	m_mountedLayers.push_back({layer, archive});
	m_indexDirty = true;
	return true;
}

int PackageArchiveSession::openArchiveCount() const
{
	int count = m_hasPrimaryLayer && m_primary.archive ? 1 : 0;
	for (const LayerState& state : m_mountedLayers) {
		if (state.archive) {
			++count;
		}
	}
	return count;
}

bool PackageArchiveSession::hasOpenArchive() const
{
	return openArchiveCount() > 0;
}

QVector<PackageEntry> PackageArchiveSession::entries() const
{
	rebuildIndex();
	return m_mergedEntries;
}

QVector<PackageLoadWarning> PackageArchiveSession::warnings() const
{
	QVector<PackageLoadWarning> merged;
	const auto collect = [&merged](const LayerState& state) {
		if (!state.archive) {
			return;
		}
		for (const PackageLoadWarning& warning : state.archive->warnings()) {
			merged.push_back({warning.virtualPath, QStringLiteral("%1: %2").arg(state.layer.id, warning.message)});
		}
	};
	if (m_hasPrimaryLayer) {
		collect(m_primary);
	}
	for (const LayerState& state : m_mountedLayers) {
		collect(state);
	}
	return merged;
}

PackageArchiveSummary PackageArchiveSession::summary() const
{
	rebuildIndex();

	PackageArchiveSummary summary;
	summary.sourcePath = m_hasPrimaryLayer ? m_primary.layer.sourcePath : QString();
	summary.format = m_hasPrimaryLayer ? m_primary.layer.format : PackageArchiveFormat::Unknown;
	summary.entryCount = static_cast<int>(m_mergedEntries.size());
	summary.warningCount = static_cast<int>(warnings().size());
	for (const PackageEntry& entry : m_mergedEntries) {
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

bool PackageArchiveSession::readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (error) {
		error->clear();
	}
	if (out) {
		out->clear();
	}
	if (!hasOpenArchive()) {
		if (error) {
			*error = packageText("No package layer is open in this session.");
		}
		return false;
	}

	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		if (error) {
			*error = packageText("Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue));
		}
		return false;
	}

	rebuildIndex();
	const auto owner = m_entryOwner.constFind(duplicateKey(normalized.normalizedPath));
	if (owner == m_entryOwner.constEnd()) {
		if (error) {
			*error = packageText("Package entry not found.");
		}
		return false;
	}

	const LayerState* state = layerStateAt(owner.value());
	if (!state || !state->archive) {
		if (error) {
			*error = packageText("Package entry not found.");
		}
		return false;
	}

	QString relative = normalized.normalizedPath;
	if (!state->layer.mountPath.isEmpty()) {
		relative = relative.mid(state->layer.mountPath.size() + 1);
	}
	return state->archive->readEntryBytes(relative, out, error, maxBytes);
}

int PackageArchiveSession::entryLayerIndex(const QString& virtualPath) const
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		return -1;
	}
	rebuildIndex();
	const auto owner = m_entryOwner.constFind(duplicateKey(normalized.normalizedPath));
	return owner == m_entryOwner.constEnd() ? -1 : owner.value();
}

QString PackageArchiveSession::entryLayerId(const QString& virtualPath) const
{
	const int index = entryLayerIndex(virtualPath);
	const LayerState* state = layerStateAt(index);
	return state ? state->layer.id : QString();
}

PackageMountLayer PackageArchiveSession::layerAt(int index) const
{
	const LayerState* state = layerStateAt(index);
	return state ? state->layer : PackageMountLayer {};
}

const PackageArchive* PackageArchiveSession::archiveAt(int index) const
{
	const LayerState* state = layerStateAt(index);
	return state ? state->archive.get() : nullptr;
}

const PackageArchiveSession::LayerState* PackageArchiveSession::layerStateAt(int index) const
{
	if (index == 0) {
		return m_hasPrimaryLayer ? &m_primary : nullptr;
	}
	const int mounted = index - 1;
	if (mounted < 0 || mounted >= m_mountedLayers.size()) {
		return nullptr;
	}
	return &m_mountedLayers.at(mounted);
}

QString PackageArchiveSession::uniqueLayerId(const QString& candidate) const
{
	QString base = candidate.trimmed();
	if (base.isEmpty()) {
		base = QStringLiteral("layer");
	}

	const auto taken = [this](const QString& id) {
		if (m_hasPrimaryLayer && m_primary.layer.id.compare(id, Qt::CaseInsensitive) == 0) {
			return true;
		}
		for (const LayerState& state : m_mountedLayers) {
			if (state.layer.id.compare(id, Qt::CaseInsensitive) == 0) {
				return true;
			}
		}
		return false;
	};

	QString unique = base;
	int suffix = 2;
	while (taken(unique)) {
		unique = QStringLiteral("%1#%2").arg(base).arg(suffix);
		++suffix;
	}
	return unique;
}

// idTech pk3 semantics: layers are searched from the top of the stack down, so
// a file in a later layer completely shadows the same path in an earlier one.
void PackageArchiveSession::rebuildIndex() const
{
	if (!m_indexDirty) {
		return;
	}
	m_indexDirty = false;
	m_mergedEntries.clear();
	m_entryOwner.clear();

	QHash<QString, PackageEntry> byKey;
	const auto mergeLayer = [&byKey, this](int layerIndex) {
		const LayerState* state = layerStateAt(layerIndex);
		if (!state || !state->archive) {
			return;
		}
		const QString mount = state->layer.mountPath;
		const QVector<PackageEntry> layerEntries = state->archive->entries();
		for (PackageEntry entry : layerEntries) {
			if (!mount.isEmpty()) {
				entry.virtualPath = mount + QLatin1Char('/') + entry.virtualPath;
			}
			entry.layerId = state->layer.id;
			entry.sourceArchiveId = state->layer.id;
			const QString key = duplicateKey(entry.virtualPath);
			byKey.insert(key, entry);
			m_entryOwner.insert(key, layerIndex);
		}
	};

	mergeLayer(0);
	for (int index = 0; index < m_mountedLayers.size(); ++index) {
		mergeLayer(index + 1);
	}

	m_mergedEntries.reserve(byKey.size());
	for (auto it = byKey.cbegin(); it != byKey.cend(); ++it) {
		m_mergedEntries.push_back(it.value());
	}

	const QString sessionId = m_hasPrimaryLayer ? m_primary.layer.id : QStringLiteral("session");
	addSyntheticDirectories(&m_mergedEntries, sessionId);
	std::sort(m_mergedEntries.begin(), m_mergedEntries.end(), entryPathLess);
}

bool PackageArchiveSession::normalizeLayer(PackageMountLayer* layer, QString* error) const
{
	if (error) {
		error->clear();
	}
	if (!layer) {
		if (error) {
			*error = packageText("Package layer is missing.");
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
		return packageText("Folder");
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
	return packageText("Unknown");
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
	return formatFromExtension(fileName);
}

// Magic-byte sniffing. ZIP returns the generic Zip format; callers use the file
// extension to decide between ZIP and PK3, which share the same container.
PackageArchiveFormat packageArchiveFormatFromContent(const QString& filePath)
{
	const QFileInfo info(filePath);
	if (info.exists() && info.isDir()) {
		return PackageArchiveFormat::Folder;
	}

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return PackageArchiveFormat::Unknown;
	}
	const QByteArray magic = file.read(4);
	if (magic.size() < 4) {
		return PackageArchiveFormat::Unknown;
	}

	if (magic == QByteArrayLiteral("PACK")) {
		return PackageArchiveFormat::Pak;
	}
	if (magic == QByteArrayLiteral("IWAD") || magic == QByteArrayLiteral("PWAD")
		|| magic == QByteArrayLiteral("WAD2") || magic == QByteArrayLiteral("WAD3")) {
		return PackageArchiveFormat::Wad;
	}

	const quint32 signature = readLe32(magic, 0);
	if (signature == kZipLocalFileSignature || signature == kZipCentralDirectorySignature
		|| signature == kZipEndOfCentralDirectorySignature || signature == kZip64EndOfCentralDirectorySignature
		|| signature == kZipDataDescriptorSignature) {
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
			packageText("Folder"),
			{},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract-source"), QStringLiteral("nested-mount")},
			packageText("Directory tree browsed as a read-only package: recursive listing, byte reads, and extraction as a copy source."),
		},
		{
			PackageArchiveFormat::Pak,
			QStringLiteral("pak"),
			packageText("Quake PAK"),
			{QStringLiteral("pak")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("nested-mount")},
			packageText("idTech2 PACK archive: lists, reads, and extracts entries. PAK stores every entry uncompressed, so all entries are readable."),
		},
		{
			PackageArchiveFormat::Wad,
			QStringLiteral("wad"),
			packageText("Doom/Quake WAD"),
			{QStringLiteral("wad"), QStringLiteral("wad2"), QStringLiteral("wad3")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("nested-mount")},
			packageText("Doom IWAD/PWAD lumps and Quake/Half-Life WAD2/WAD3 texture lumps: lists, reads, and extracts uncompressed lumps. WAD2/WAD3 compressed lumps are listed but not decoded."),
		},
		{
			PackageArchiveFormat::Zip,
			QStringLiteral("zip"),
			QStringLiteral("ZIP"),
			{QStringLiteral("zip")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("deflate"), QStringLiteral("zip64"), QStringLiteral("nested-mount")},
			packageText("ZIP archive with stored and DEFLATE entries, ZIP64 central directories, and CRC-32 verified reads. Encrypted entries and other compression methods are listed but not decoded."),
		},
		{
			PackageArchiveFormat::Pk3,
			QStringLiteral("pk3"),
			packageText("Quake III PK3"),
			{QStringLiteral("pk3")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("deflate"), QStringLiteral("zip64"), QStringLiteral("nested-mount")},
			packageText("idTech3 PK3 archive over the ZIP container: stored and DEFLATE entries, ZIP64, CRC-32 verified reads, and pk3 shadowing when several layers are mounted in one session."),
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
		return packageText("File");
	case PackageEntryKind::Directory:
		return packageText("Directory");
	}
	return packageText("File");
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
	case PackagePathIssue::ReservedDeviceName:
		return QStringLiteral("reserved-device-name");
	case PackagePathIssue::TrailingDotOrSpace:
		return QStringLiteral("trailing-dot-or-space");
	}
	return QStringLiteral("unknown");
}

QString packagePathIssueDisplayName(PackagePathIssue issue)
{
	switch (issue) {
	case PackagePathIssue::None:
		return packageText("safe");
	case PackagePathIssue::Empty:
		return packageText("empty path");
	case PackagePathIssue::AbsolutePath:
		return packageText("absolute paths are not allowed inside packages");
	case PackagePathIssue::DriveQualifiedPath:
		return packageText("drive-qualified paths are not allowed inside packages");
	case PackagePathIssue::TraversalSegment:
		return packageText("parent-directory traversal is not allowed inside packages");
	case PackagePathIssue::CurrentDirectorySegment:
		return packageText("current-directory segments are not allowed inside packages");
	case PackagePathIssue::Colon:
		return packageText("colon characters are not allowed inside package paths");
	case PackagePathIssue::ControlCharacter:
		return packageText("control characters are not allowed inside package paths");
	case PackagePathIssue::TooLong:
		return packageText("package path is too long");
	case PackagePathIssue::ReservedDeviceName:
		return packageText("the path contains a reserved device name (CON, PRN, AUX, NUL, COM1-9, LPT1-9)");
	case PackagePathIssue::TrailingDotOrSpace:
		return packageText("path segments may not end with a dot or a space");
	}
	return packageText("unknown package path issue");
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

	// Equivalent to matching "^[A-Za-z]:" without compiling a regular expression
	// on every call; this runs several times per archive entry.
	if (trimmed.size() >= 2 && trimmed.at(1) == QLatin1Char(':')) {
		const QChar drive = trimmed.at(0);
		if ((drive >= QLatin1Char('A') && drive <= QLatin1Char('Z')) || (drive >= QLatin1Char('a') && drive <= QLatin1Char('z'))) {
			result.issue = PackagePathIssue::DriveQualifiedPath;
			return result;
		}
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
	const qsizetype slash = normalized.normalizedPath.lastIndexOf('/');
	return slash < 0 ? normalized.normalizedPath : normalized.normalizedPath.mid(slash + 1);
}

QString packageVirtualPathParent(const QString& path)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(path, false);
	if (!normalized.isSafe()) {
		return {};
	}
	const qsizetype slash = normalized.normalizedPath.lastIndexOf('/');
	return slash < 0 ? QString() : normalized.normalizedPath.left(slash);
}

bool packageEntryLooksNestedArchive(const QString& virtualPath)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		return false;
	}
	const PackageArchiveFormat format = formatFromExtension(normalized.normalizedPath);
	return format == PackageArchiveFormat::Pak
		|| format == PackageArchiveFormat::Wad
		|| format == PackageArchiveFormat::Zip
		|| format == PackageArchiveFormat::Pk3;
}

PackagePathIssue packageFilesystemPathIssue(const QString& virtualPath)
{
	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		return normalized.issue;
	}

	const QStringList segments = normalized.normalizedPath.split('/', Qt::SkipEmptyParts);
	for (const QString& segment : segments) {
		if (segment.endsWith('.') || segment.endsWith(' ')) {
			return PackagePathIssue::TrailingDotOrSpace;
		}
		if (isReservedDeviceSegment(segment)) {
			return PackagePathIssue::ReservedDeviceName;
		}
	}
	return PackagePathIssue::None;
}

bool packagePathIsInsideDirectory(const QString& rootDirectory, const QString& candidatePath)
{
	const QString root = comparableDirectoryPath(rootDirectory);
	const QString candidate = comparableDirectoryPath(candidatePath);
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

	const QString root = canonicalizedAbsolutePath(rootDirectory);
	if (root.isEmpty()) {
		if (error) {
			*error = packageText("Output root is empty.");
		}
		return {};
	}

	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		if (error) {
			*error = packageText("Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue));
		}
		return {};
	}

	const PackagePathIssue filesystemIssue = packageFilesystemPathIssue(normalized.normalizedPath);
	if (filesystemIssue != PackagePathIssue::None) {
		if (error) {
			*error = packageText("Unsafe package path: %1").arg(packagePathIssueDisplayName(filesystemIssue));
		}
		return {};
	}

	QString nativeRelative = normalized.normalizedPath;
	nativeRelative.replace('/', QDir::separator());
	const QString candidate = QDir(root).filePath(nativeRelative);
	if (!packagePathIsInsideDirectory(root, candidate)) {
		if (error) {
			*error = packageText("Output path escapes the selected root.");
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
		failBeforeEntries(packageText("No package is open."));
		return report;
	}
	// An empty selection is never an implicit "extract everything": that turned a
	// caller's empty list into a full-archive write.
	if (!request.extractAll && request.virtualPaths.isEmpty()) {
		report.warnings.push_back(packageText("No package entries were selected; nothing was extracted. Request extract-all to write the whole archive."));
		return report;
	}
	if (request.targetDirectory.trimmed().isEmpty()) {
		failBeforeEntries(packageText("Output directory is required."));
		return report;
	}

	const QFileInfo targetInfo(report.targetDirectory);
	if (targetInfo.exists() && !targetInfo.isDir()) {
		failBeforeEntries(packageText("Output path exists but is not a directory."));
		return report;
	}
	if (!request.dryRun && !QDir().mkpath(report.targetDirectory)) {
		failBeforeEntries(packageText("Unable to create output directory."));
		return report;
	}

	const QVector<PackageEntry> archiveEntries = archive.entries();
	QVector<PackageEntry> selectedEntries;
	if (request.extractAll) {
		selectedEntries = archiveEntries;
		// Duplicate virtual paths (WAD lump names repeated across maps) are
		// equivalent under entryPathLess and share one output path, so a stable
		// sort keeps "the last archive-order duplicate wins on disk" instead of
		// leaving it to the standard library's introsort.
		std::stable_sort(selectedEntries.begin(), selectedEntries.end(), entryPathLess);
	} else {
		selectedEntries = selectedEntriesForExtraction(archiveEntries, request.virtualPaths, &report);
	}
	report.requestedCount = static_cast<int>(selectedEntries.size()) + report.errorCount;

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
				result.error = packageText("Cannot create directory because a file already exists at the output path.");
			} else if (request.dryRun) {
				result.message = packageText("Would create directory.");
			} else if (!QDir().mkpath(result.outputPath)) {
				result.error = packageText("Unable to create output directory.");
			} else {
				result.written = true;
				result.message = packageText("Created directory.");
			}
			addExtractionResult(&report, result);
		} else if (!entry.readable) {
			result.error = entry.note.isEmpty() ? packageText("Package entry is not readable by the current reader.") : entry.note;
			addExtractionResult(&report, result);
		} else {
			const QFileInfo outputInfo(result.outputPath);
			if (outputInfo.exists() && outputInfo.isDir()) {
				result.error = packageText("Cannot write file because a directory already exists at the output path.");
			} else if (outputInfo.exists() && !request.overwriteExisting) {
				result.skipped = true;
				result.message = packageText("Output exists; pass overwrite to replace it.");
			} else if (request.dryRun) {
				result.message = outputInfo.exists() ? packageText("Would overwrite file.") : packageText("Would write file.");
			} else {
				QByteArray bytes;
				QString readError;
				if (!archive.readEntryBytes(entry.virtualPath, &bytes, &readError)) {
					result.error = readError.isEmpty() ? packageText("Unable to read package entry.") : readError;
				} else {
					const QString parentPath = QFileInfo(result.outputPath).absolutePath();
					if (!QDir().mkpath(parentPath)) {
						result.error = packageText("Unable to create output parent directory.");
					} else if (!packagePathIsInsideDirectory(report.targetDirectory, result.outputPath)) {
						// Re-checked after the parent directories exist: a symlink or
						// junction already present in the target tree can only be
						// resolved once the path is real.
						result.error = packageText("Output path escapes the selected root.");
					} else {
						QSaveFile file(result.outputPath);
						if (!file.open(QIODevice::WriteOnly)) {
							result.error = packageText("Unable to open output file for writing.");
						} else if (file.write(bytes) != bytes.size()) {
							result.error = packageText("Unable to write all output bytes.");
							file.cancelWriting();
						} else if (!file.commit()) {
							result.error = packageText("Unable to commit output file.");
						} else {
							result.bytes = static_cast<quint64>(std::max<qsizetype>(0, bytes.size()));
							result.written = true;
							result.message = outputInfo.exists() ? packageText("Overwrote file.") : packageText("Wrote file.");
						}
					}
				}
			}
			addExtractionResult(&report, result);
		}

		if (progress && !progress(report.entries.back(), report)) {
			report.cancelled = true;
			report.warnings.push_back(packageText("Package extraction cancelled."));
			break;
		}
	}

	return report;
}

QString packageExtractionReportText(const PackageExtractionReport& report)
{
	QStringList lines;
	lines << packageText("Package extraction");
	lines << packageText("Source: %1").arg(QDir::toNativeSeparators(report.sourcePath));
	lines << packageText("Target: %1").arg(QDir::toNativeSeparators(report.targetDirectory));
	lines << packageText("Mode: %1").arg(report.dryRun ? packageText("dry-run") : packageText("write"));
	lines << packageText("Overwrite: %1").arg(report.overwriteExisting ? packageText("yes") : packageText("no"));
	lines << packageText("State: %1").arg(report.cancelled ? packageText("cancelled") : (report.succeeded() ? packageText("completed") : packageText("failed")));
	lines << packageText("Requested entries: %1").arg(report.requestedCount);
	lines << packageText("Processed entries: %1").arg(report.processedCount);
	lines << packageText("Written entries: %1").arg(report.writtenCount);
	lines << packageText("Created directories: %1").arg(report.directoryCount);
	lines << packageText("Skipped entries: %1").arg(report.skippedCount);
	lines << packageText("Errors: %1").arg(report.errorCount);
	lines << packageText("Bytes written: %1").arg(report.totalBytes);
	lines << packageText("Output paths");
	for (const PackageExtractionEntryResult& result : report.entries) {
		QString state = packageText("planned");
		if (!result.error.isEmpty()) {
			state = packageText("failed");
		} else if (result.skipped) {
			state = packageText("skipped");
		} else if (result.dryRun) {
			state = result.kind == PackageEntryKind::Directory ? packageText("would create") : packageText("would write");
		} else if (result.kind == PackageEntryKind::Directory && result.written) {
			state = packageText("created");
		} else if (result.written) {
			state = packageText("wrote");
		}
		lines << QStringLiteral("- %1: %2 -> %3")
			.arg(state,
				result.virtualPath.isEmpty() ? packageText("(package)") : result.virtualPath,
				result.outputPath.isEmpty() ? packageText("(no output path)") : QDir::toNativeSeparators(result.outputPath));
		if (!result.message.isEmpty()) {
			lines << QStringLiteral("  %1").arg(result.message);
		}
		if (!result.error.isEmpty()) {
			lines << packageText("  Error: %1").arg(result.error);
		}
	}
	if (!report.warnings.isEmpty()) {
		lines << packageText("Warnings");
		for (const QString& warning : report.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
