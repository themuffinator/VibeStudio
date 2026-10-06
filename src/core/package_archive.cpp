#include "core/package_archive.h"

#include "core/deflate.h"
#include "core/package_snapshot_p.h"
#include "core/package_protection_p.h"
#include "core/package_index_p.h"
#include "core/package_zip_p.h"

#include <QChar>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QScopeGuard>
#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace vibestudio {

namespace {

constexpr int kMaximumPackageVirtualPathLength = 4096;
constexpr quint32 kPakSignature = 0x4b434150; // PACK

// PKWARE .ZIP File Format Specification (APPNOTE.TXT), sections 4.3.6 - 4.3.16.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
constexpr quint32 kZipCentralDirectorySignature = 0x02014b50;
constexpr quint32 kZipEndOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 kZipLocalFileSignature = 0x04034b50;
constexpr quint32 kZipDataDescriptorSignature = 0x08074b50;
constexpr quint32 kZip64EndOfCentralDirectorySignature = 0x06064b50;

// Hard ceiling on a single inflate so a hostile central directory cannot ask
// the reader to materialise an absurd buffer.
constexpr qint64 kMaximumInflateBytes = 1024ll * 1024ll * 1024ll;

bool collectPackageSummary(const QVector<PackageEntry>& entries, PackageArchiveSummary* summary,
	const PackageReadControl& control, QString* error)
{
	const auto cancelled = [&] {
		if (!control.isCancelled || !control.isCancelled()) { return false; }
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package opening cancelled."); }
		return true;
	};
	summary->entryCount = static_cast<int>(entries.size());
	for (const auto& entry : entries) {
		if (cancelled()) { return false; }
		if (entry.kind == PackageEntryKind::Directory) { ++summary->directoryCount; }
		else { ++summary->fileCount; accumulatePackageBytes(entry.sizeBytes, &summary->totalSizeBytes, &summary->totalSizeOverflow); }
		if (entry.nestedArchiveCandidate) { ++summary->nestedArchiveCount; }
	}
	return !cancelled();
}

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
	if (ext == QStringLiteral("zip") || ext == QStringLiteral("pk4") || ext == QStringLiteral("pkz")) {
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
		QStringLiteral("def"), QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("log"), QStringLiteral("vprefab"),
	};
	static const QStringList kImageExtensions = {
		QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("tga"),
		QStringLiteral("pcx"), QStringLiteral("wal"), QStringLiteral("mip"), QStringLiteral("lmp"),
	};
	static const QStringList kAudioExtensions = {
		QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("mp3"),
	};
	static const QStringList kModelExtensions = {
		QStringLiteral("obj"),
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
	return QDateTime(qdate, qtime, QTimeZone::utc());
}

bool readAt(QIODevice& file, qint64 offset, qint64 size, QByteArray* out)
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

QString duplicateKey(const QString& path)
{
	return path.toCaseFolded();
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
QString resolveExistingPath(const QString& existingPath, const PackageReadControl& control = {})
{
	QString current = existingPath;
	for (int depth = 0; depth < 40; ++depth) {
		if (control.isCancelled && control.isCancelled()) { return {}; }
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
			if (control.isCancelled && control.isCancelled()) { return {}; }
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
				if (control.isCancelled && control.isCancelled()) { return {}; }
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
	return {}; // No resolved path after the bounded junction traversal.
}

// Resolves the longest existing prefix of `path` so symlinks and junctions
// inside the tree cannot be used to make a path look contained when the real
// target is elsewhere. Segments that do not exist yet (extraction creates them)
// are appended unresolved.
QString canonicalizedAbsolutePath(const QString& path, const PackageReadControl& control = {})
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
		if (control.isCancelled && control.isCancelled()) { return {}; }
		QString probePath = probe;
		if (probePath.endsWith(':')) {
			probePath += '/';
		}
		const QFileInfo info(probePath);
		if (info.exists()) {
			QString canonical = resolveExistingPath(probe, control);
			if (canonical.isEmpty()) { return {}; }
			for (const QString& segment : pending) {
				if (control.isCancelled && control.isCancelled()) { return {}; }
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
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unsafe package mount path: %1").arg(packagePathIssueDisplayName(normalized.issue));
		}
		return {};
	}
	return normalized.normalizedPath;
}

bool indexCancelled(const PackageReadControl& control, QString* error)
{
	if (!control.isCancelled || !control.isCancelled()) { return false; }
	if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package opening cancelled."); }
	return true;
}

bool chargeLayerUsage(PackageIndexUsage* usage, const PackageIndexUsage& addition, const PackageIndexLimits& limits, QString* error)
{
	if (addition.entries < 0 || addition.entries > limits.maximumEntries - usage->entries
		|| addition.metadataBytes < 0 || addition.metadataBytes > limits.maximumMetadataBytes - usage->metadataBytes
		|| addition.fingerprintBytes < 0 || addition.fingerprintBytes > limits.maximumFingerprintBytes - usage->fingerprintBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package layers exceed the limits of %1 records, %2 metadata bytes or %3 fingerprint bytes. Unmount a layer or use smaller sources.")
			.arg(limits.maximumEntries).arg(limits.maximumMetadataBytes).arg(limits.maximumFingerprintBytes); }
		return false;
	}
	usage->entries += addition.entries; usage->metadataBytes += addition.metadataBytes; usage->fingerprintBytes += addition.fingerprintBytes;
	return true;
}

} // namespace

bool dmxSoundHeaderLooksValid(const QByteArray& head, qint64 size, const QString& lumpName)
{
	static const QStringList mapLumps {
		QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"), QStringLiteral("VERTEXES"), QStringLiteral("SEGS"),
		QStringLiteral("SSECTORS"), QStringLiteral("NODES"), QStringLiteral("SECTORS"), QStringLiteral("REJECT"), QStringLiteral("BLOCKMAP"),
		QStringLiteral("BEHAVIOR"), QStringLiteral("SCRIPTS"), QStringLiteral("TEXTMAP"), QStringLiteral("ZNODES"), QStringLiteral("ENDMAP"),
		QStringLiteral("DIALOGUE"),
	};
	const QString upper = lumpName.trimmed().toUpper();
	if (!upper.isEmpty() && (mapLumps.contains(upper) || upper.startsWith(QStringLiteral("GL_")))) {
		return false;
	}
	if (head.size() < 8) {
		return false;
	}
	const auto* data = reinterpret_cast<const uchar*>(head.constData());
	const quint16 format = static_cast<quint16>(data[0] | (data[1] << 8));
	const quint16 rate = static_cast<quint16>(data[2] | (data[3] << 8));
	const quint32 count = static_cast<quint32>(data[4]) | (static_cast<quint32>(data[5]) << 8) | (static_cast<quint32>(data[6]) << 16)
		| (static_cast<quint32>(data[7]) << 24);
	const qint64 room = size - 8;
	return format == 3 && rate >= 4000 && rate <= 48000 && count > 0 && static_cast<qint64>(count) <= room && room - static_cast<qint64>(count) <= 32;
}

// The namespace a Doom marker lump opens or closes: flats (F_, FF_, F1_ to
// F3_), sprites (S_, SS_), wall patches (P_, PP_, P1_ to P3_), or ZDoom's
// textures (TX_), with `opens` set for _START. Anything else returns empty.
QString doomNamespaceMarker(const QString& lumpName, bool* opens)
{
	static const QHash<QString, QString> namespaces {
		{QStringLiteral("F"), QStringLiteral("flat")}, {QStringLiteral("FF"), QStringLiteral("flat")},
		{QStringLiteral("F1"), QStringLiteral("flat")}, {QStringLiteral("F2"), QStringLiteral("flat")}, {QStringLiteral("F3"), QStringLiteral("flat")},
		{QStringLiteral("S"), QStringLiteral("sprite")}, {QStringLiteral("SS"), QStringLiteral("sprite")},
		{QStringLiteral("P"), QStringLiteral("patch")}, {QStringLiteral("PP"), QStringLiteral("patch")},
		{QStringLiteral("P1"), QStringLiteral("patch")}, {QStringLiteral("P2"), QStringLiteral("patch")}, {QStringLiteral("P3"), QStringLiteral("patch")},
		{QStringLiteral("TX"), QStringLiteral("texture")},
	};
	const QString upper = lumpName.toUpper();
	for (const QString& suffix : {QStringLiteral("_START"), QStringLiteral("_END")}) {
		if (upper.endsWith(suffix)) {
			const QString found = namespaces.value(upper.left(upper.size() - suffix.size()));
			if (!found.isEmpty() && opens) {
				*opens = suffix == QStringLiteral("_START");
			}
			return found;
		}
	}
	return {};
}

bool PackageVirtualPath::isSafe() const
{
	return issue == PackagePathIssue::None && !normalizedPath.isEmpty();
}

bool PackageArchiveReader::readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (out) { out->clear(); }
	if (error) { error->clear(); }
	const auto listed = entries();
	if (index < 0 || index >= listed.size()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry index is out of range."); }
		return false;
	}
	const auto& entry = listed.at(index);
	int matches = 0;
	for (const auto& candidate : listed) {
		if (candidate.virtualPath.compare(entry.virtualPath, Qt::CaseInsensitive) == 0) { ++matches; }
	}
	if (matches != 1) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "This reader cannot resolve repeated entry names by position."); }
		return false;
	}
	return readEntryBytes(entry.virtualPath, out, error, maxBytes);
}

bool PackageArchiveReader::streamEntryAt(qsizetype, const std::function<bool(QByteArrayView)>&, QString* error,
	const std::function<bool()>&) const
{
	if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "This reader does not support bounded package streaming."); }
	return false;
}

bool PackageArchiveReader::visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
	QString* error, const PackageReadControl& control) const
{
	if (indexCancelled(control, error) || !visitor) { return false; }
	const auto source = sourcePath();
	return (source.isEmpty() || visitor(source)) && !indexCancelled(control, error);
}

bool PackageArchiveReader::protectsInputPath(const QString& path) const
{
	QString error;
	PackageInputProtectionSet inputs(PackageInputProtectionSet::pathCeiling, PackageInputProtectionSet::byteCeiling, &error);
	return !visitProtectedInputPaths([&](const QString& input) { return inputs.add(input); }, &error)
		|| !inputs.finish() || inputs.protects(path);
}

bool PackageArchive::load(const QString& path, QString* error, const PackageReadControl& control, const PackageIndexLimits& limits)
{
	QString localError; if (!error) { error = &localError; } error->clear();
	clear();
	const auto rememberFailure = qScopeGuard([&] { if (!m_open) { m_error = *error; } });
	if (!validPackageIndexLimits(limits, error)) { return false; }
	const auto cancelled = [&]() {
		if (!control.isCancelled || !control.isCancelled()) { return false; }
		clear();
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package opening cancelled."); }
		return true;
	};
	if (cancelled()) { return false; }

	const QFileInfo info(path);
	if (!info.exists()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package path does not exist.");
		}
		return false;
	}

	const QString absolutePath = info.absoluteFilePath();
	if (info.isDir()) {
		m_loadControl = control; m_indexLimits = limits;
		if (!loadFolder(absolutePath, error)) {
			clear();
			return false;
		}
		if (!finalizeEntries(error)) { clear(); return false; }
		if (cancelled()) { return false; }
		m_loadControl = {};
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

	const auto identity = capturePackageFileIdentity(absolutePath, error, control, limits.maximumFingerprintBytes);
	if (!identity) { return false; }

	QString firstError;
	for (PackageArchiveFormat candidate : candidates) {
		// Every attempt starts from a clean reader: the loaders publish entries
		// as they parse, so a truncated file must not leave a half-populated,
		// mixed-format listing behind for the next attempt (or for the caller).
		clear();
		m_fileIdentity = identity;
		m_loadControl = control; m_indexLimits = limits; m_indexFingerprintBytes = identity->chunkHashes.size();
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
			if (!finalizeEntries(error)) { clear(); return false; }
			if (cancelled()) { return false; }
			m_sourceSizeBytes = identity->size;
			m_sourceModifiedUtc = identity->modifiedUtc;
			m_loadControl = {};
			m_open = true;
			return true;
		}
		if (control.isCancelled && control.isCancelled()) {
			clear();
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package opening cancelled."); }
			return false;
		}
		if (firstError.isEmpty()) {
			firstError = attemptError;
		}
	}

	clear();
	if (error) {
		*error = firstError.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "Unsupported package format.") : firstError;
	}
	return false;
}

void PackageArchive::clear()
{
	m_format = PackageArchiveFormat::Unknown;
	m_sourcePath.clear(); m_error.clear();
	m_open = false;
	m_entries.clear();
	m_warnings.clear(); m_summary = {};
	m_sourceSizeBytes = -1;
	m_sourceModifiedUtc = {};
	m_wadMagic.clear();
	m_fileIdentity.reset();
	m_folderIdentities.clear();
	m_resolvedRoot.clear();
	m_loadControl = {}; m_indexLimits = {}; m_indexEntries = 0; m_indexMetadataBytes = 0; m_indexFingerprintBytes = 0;
	m_snapshotReader.reset();
}

bool PackageArchive::loadSnapshot(std::shared_ptr<const PackageArchiveReader> reader, QString* error,
	const QVector<PackageLoadWarning>& warnings, const PackageReadControl& control, const PackageIndexLimits& limits)
{
	QString localError; if (!error) { error = &localError; } error->clear();
	const auto rememberFailure = qScopeGuard([&] { if (!error->isEmpty()) { m_error = *error; } });
	if (!validPackageIndexLimits(limits, error) || indexCancelled(control, error)) { return false; }
	if (!reader || !reader->isOpen()) {
		*error = reader && !reader->errorString().isEmpty() ? reader->errorString()
			: QCoreApplication::translate("VibeStudioPackageArchive", "The package document snapshot is not available.");
		return false;
	}
	PackageIndexUsage backing;
	auto owner = reader;
	QSet<const PackageArchiveReader*> seen;
	for (;;) {
		if (indexCancelled(control, error)) { return false; }
		if (owner.get() == this || seen.contains(owner.get()) || seen.size() >= 64) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "The package snapshot has a cyclic or excessively nested reader chain."); return false;
		}
		seen.insert(owner.get());
		const auto* archive = dynamic_cast<const PackageArchive*>(owner.get());
		if (!archive) { break; }
		const auto retained = archive->indexUsage();
		backing.entries = qMax(backing.entries, retained.entries);
		backing.metadataBytes = qMax(backing.metadataBytes, retained.metadataBytes);
		backing.fingerprintBytes = qMax(backing.fingerprintBytes, retained.fingerprintBytes);
		if (backing.entries > limits.maximumEntries || backing.metadataBytes > limits.maximumMetadataBytes || backing.fingerprintBytes > limits.maximumFingerprintBytes) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "The snapshot's backing package exceeds the selected index limits."); return false;
		}
		if (archive->snapshotReader()) { owner = archive->snapshotReader(); }
		else { owner = std::make_shared<PackageArchive>(*archive); break; }
	}
	PackageArchive candidate;
	candidate.m_format = reader->format(); candidate.m_sourcePath = reader->sourcePath(); candidate.m_entries = reader->entries();
	if (candidate.m_format == PackageArchiveFormat::Wad) {
		const auto magic = reader->wadMagic();
		if (!magic.isEmpty() && magic != QStringLiteral("PWAD") && magic != QStringLiteral("IWAD")
			&& magic != QStringLiteral("WAD2") && magic != QStringLiteral("WAD3")) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "The package snapshot has an unsupported WAD layout."); return false;
		}
		candidate.m_wadMagic = QString(magic.constData(), magic.size());
	}
	candidate.m_warnings = warnings;
	if (warnings.isEmpty()) {
		if (const auto* archive = dynamic_cast<const PackageArchive*>(reader.get())) { candidate.m_warnings = archive->warnings(); }
	}
	PackageIndexUsage usage;
	if (!admitPackageSnapshot(candidate.m_sourcePath, candidate.m_entries, candidate.m_warnings, limits, control, &usage, nullptr, error)) { return false; }
	// Own raw-backed text only after its length has been admitted. Check the
	// owned values again: cancellation callbacks may have changed caller memory.
	const auto own = [](QString& value) { value = QString(value.constData(), value.size()); };
	own(candidate.m_sourcePath);
	for (auto& entry : candidate.m_entries) {
		if (indexCancelled(control, error)) { return false; }
		for (auto* value : {&entry.virtualPath, &entry.typeHint, &entry.storageMethod, &entry.sourceArchiveId, &entry.layerId, &entry.note}) { own(*value); }
	}
	for (auto& warning : candidate.m_warnings) { if (indexCancelled(control, error)) { return false; } own(warning.virtualPath); own(warning.message); }
	if (!admitPackageSnapshot(candidate.m_sourcePath, candidate.m_entries, candidate.m_warnings, limits, control, &usage, nullptr, error)) { return false; }
	candidate.m_indexLimits = limits;
	candidate.m_indexEntries = qMax(usage.entries, backing.entries);
	candidate.m_indexMetadataBytes = qMax(usage.metadataBytes, backing.metadataBytes);
	candidate.m_indexFingerprintBytes = backing.fingerprintBytes;
	candidate.m_summary.sourcePath = candidate.m_sourcePath; candidate.m_summary.format = candidate.m_format;
	candidate.m_summary.warningCount = static_cast<int>(candidate.m_warnings.size());
	if (!collectPackageSummary(candidate.m_entries, &candidate.m_summary, control, error)) { return false; }
	candidate.m_snapshotReader = std::move(owner); candidate.m_open = true;
	*this = std::move(candidate); return true;
}

QString PackageArchive::wadMagic() const { return m_wadMagic; }
std::shared_ptr<const PackageArchiveReader> PackageArchive::snapshotReader() const { return m_snapshotReader; }
PackageIndexUsage PackageArchive::indexUsage() const { return {m_indexEntries, m_indexMetadataBytes, m_indexFingerprintBytes}; }

bool PackageArchive::visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor,
	QString* error, const PackageReadControl& control) const
{
	if (!PackageArchiveReader::visitProtectedInputPaths(visitor, error, control)) { return false; }
	if (m_fileIdentity && !m_fileIdentity->resolvedPath.isEmpty() && !visitor(m_fileIdentity->resolvedPath)) { return false; }
	if (!m_resolvedRoot.isEmpty() && !visitor(m_resolvedRoot)) { return false; }
	return (!m_snapshotReader || m_snapshotReader->visitProtectedInputPaths(visitor, error, control)) && !indexCancelled(control, error);
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
	return m_summary;
}

bool PackageArchive::readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (m_snapshotReader) { return m_snapshotReader->readEntryBytes(virtualPath, out, error, maxBytes); }
	if (error) {
		error->clear();
	}
	if (out) {
		out->clear();
	}
	if (!m_open) {
		if (error) {
			*error = m_error.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "No package is open.") : m_error;
		}
		return false;
	}

	const PackageEntry* entry = findEntry(virtualPath);
	if (!entry) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry not found.");
		}
		return false;
	}
	return readEntry(*entry, out, error, maxBytes);
}

bool PackageArchive::readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (m_snapshotReader) { return m_snapshotReader->readEntryAt(index, out, error, maxBytes); }
	if (out) { out->clear(); }
	if (error) { error->clear(); }
	if (!m_open || index < 0 || index >= m_entries.size()) {
		if (error) { *error = !m_open && !m_error.isEmpty() ? m_error : QCoreApplication::translate("VibeStudioPackageArchive", "Package entry index is out of range or no package is open."); }
		return false;
	}
	return readEntry(m_entries.at(index), out, error, maxBytes);
}

bool PackageArchive::sourceMatchesSnapshot() const
{
	if (m_format == PackageArchiveFormat::Folder) {
		const QFileInfo root(m_sourcePath);
		return root.isDir() && root.canonicalFilePath() == m_resolvedRoot;
	}
	return m_fileIdentity && m_fileIdentity->matchesMetadata();
}

PackageFileIdentityPtr PackageArchive::fileIdentity(const QString& virtualPath) const
{
	return m_format == PackageArchiveFormat::Folder ? m_folderIdentities.value(virtualPath) : m_fileIdentity;
}

QByteArray PackageArchive::contentId() const
{
	// A document has no single encoded archive identity until it is exported.
	if (m_snapshotReader) { return {}; }
	if (!m_open) { return {}; }
	if (m_fileIdentity) { return m_fileIdentity->sha256; }
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(QByteArrayView("VibeStudio folder identity v2\0"));
	QStringList paths = m_folderIdentities.keys();
	paths.sort(Qt::CaseSensitive);
	for (const auto& path : paths) {
		const auto& identity = m_folderIdentities[path];
		hash.addData(path.toUtf8()); hash.addData(QByteArrayView("\0", 1));
		hash.addData(QByteArray::number(identity->size)); hash.addData(QByteArrayView("\0", 1));
		hash.addData(identity->sha256);
		hash.addData(QByteArray::number(identity->modifiedUtc.toMSecsSinceEpoch())); hash.addData(QByteArrayView("\0", 1));
	}
	QStringList directories;
	for (const auto& entry : m_entries) { if (entry.kind == PackageEntryKind::Directory) { directories << entry.virtualPath; } }
	directories.sort(Qt::CaseSensitive);
	for (const auto& path : directories) { hash.addData(QByteArrayView("directory\0", 10)); hash.addData(path.toUtf8()); hash.addData(QByteArrayView("\0", 1)); }
	return hash.result();
}

bool PackageArchive::verifySourceIdentity(QString* error, const PackageReadControl& control) const
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (control.isCancelled && control.isCancelled()) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package source verification cancelled.")); }
	if (m_snapshotReader) {
		bool cancelled = false;
		const auto stopped = [&] {
			cancelled = cancelled || (control.isCancelled && control.isCancelled());
			if (cancelled) { fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package source verification cancelled.")); }
			return cancelled;
		};
		for (qsizetype index = 0; index < m_entries.size(); ++index) {
			if (stopped()) { return false; }
			const auto& entry = m_entries.at(index);
			if (entry.kind != PackageEntryKind::File || !entry.readable) { continue; }
			quint64 read = 0; bool sizeChanged = false;
			const bool verified = m_snapshotReader->streamEntryAt(index, [&](QByteArrayView bytes) {
				if (stopped()) { return false; }
				if (quint64(bytes.size()) > entry.sizeBytes - read) { sizeChanged = true; return false; }
				read += quint64(bytes.size());
				if (control.progress) { control.progress(entry.virtualPath, qint64(qMin<quint64>(read, std::numeric_limits<qint64>::max())),
					qint64(qMin<quint64>(entry.sizeBytes, std::numeric_limits<qint64>::max()))); }
				return !stopped();
			}, error, stopped);
			if (stopped()) { return false; }
			if (sizeChanged || (verified && read != entry.sizeBytes)) {
				return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The source snapshot entry changed size: %1").arg(entry.virtualPath));
			}
			if (!verified) { return false; }
		}
		return !stopped();
	}
	if (!m_open || !sourceMatchesSnapshot()) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The source package changed or is not open. Reopen it before continuing."));
	}
	if (m_format != PackageArchiveFormat::Folder) { return verifyPackageFileIdentity(m_fileIdentity, error, control); }
	QSet<QString> found, foundDirectories, expectedDirectories;
	for (const auto& entry : m_entries) { if (entry.kind == PackageEntryKind::Directory) { expectedDirectories.insert(entry.virtualPath); } }
	const QDir root(m_sourcePath);
	QDirIterator iterator(m_sourcePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		if (control.isCancelled && control.isCancelled()) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package source verification cancelled.")); }
		iterator.next();
		const auto path = normalizePackageVirtualPath(root.relativeFilePath(iterator.filePath()), false);
		if (!path.isSafe() || !packagePathIsInsideDirectory(m_sourcePath, iterator.filePath()) || (iterator.fileInfo().isDir() ? !expectedDirectories.contains(path.normalizedPath) : !m_folderIdentities.contains(path.normalizedPath))) {
			return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The folder package listing changed. Refresh it before continuing."));
		}
		if (iterator.fileInfo().isDir()) { foundDirectories.insert(path.normalizedPath); continue; }
		found.insert(path.normalizedPath);
		if (!verifyPackageFileIdentity(m_folderIdentities.value(path.normalizedPath), error, control)) { return false; }
	}
	if (found.size() != m_folderIdentities.size() || foundDirectories != expectedDirectories) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Files or folders disappeared from the folder package. Refresh it before continuing."));
	}
	return true;
}

bool PackageArchive::readEntry(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (!sourceMatchesSnapshot()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "The source package changed after it was opened. Reopen it before reading entries."); }
		return false;
	}
	if (entry.kind == PackageEntryKind::Directory) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry is a directory.");
		}
		return false;
	}
	if (!entry.readable) {
		if (error) {
			*error = entry.note.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "Package entry is not readable by the current reader.") : entry.note;
		}
		return false;
	}

	if (m_format == PackageArchiveFormat::Folder) {
		return readFileEntryBytes(entry, out, error, maxBytes);
	}
	return readOffsetEntryBytes(entry, out, error, maxBytes);
}

bool PackageArchive::loadFolder(const QString& path, QString* error)
{
	if (error) {
		error->clear();
	}

	const QFileInfo rootInfo(path);
	if (!rootInfo.exists() || !rootInfo.isDir()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Folder package not found.");
		}
		return false;
	}

	m_format = PackageArchiveFormat::Folder;
	m_sourcePath = rootInfo.absoluteFilePath();
	m_resolvedRoot = rootInfo.canonicalFilePath();
	const QDir root(m_sourcePath);
	const QString sourceId = rootInfo.fileName();

	QDirIterator it(m_sourcePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		if (m_loadControl.isCancelled && m_loadControl.isCancelled()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package source verification cancelled."); }
			return false;
		}
		it.next();
		const QFileInfo fileInfo = it.fileInfo();
		QString relative = root.relativeFilePath(fileInfo.absoluteFilePath());
		if (m_loadControl.progress && m_indexEntries % 64 == 0) { m_loadControl.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Indexing %1").arg(path), 0, 0); }
		if (!accountIndex(1, 0, error) || !accountIndexPath(relative, error)) { return false; }
		const PackageVirtualPath normalized = normalizePackageVirtualPath(relative, false);
		if (!normalized.isSafe()) {
			if (!addWarning(relative, QCoreApplication::translate("VibeStudioPackageArchive", "Skipped unsafe folder entry: %1").arg(packagePathIssueDisplayName(normalized.issue)), true, error)) { return false; }
			continue;
		}
		if (!packagePathIsInsideDirectory(m_sourcePath, fileInfo.absoluteFilePath())) {
			if (!addWarning(relative, QCoreApplication::translate("VibeStudioPackageArchive", "Skipped folder entry outside the package root."), true, error)) { return false; }
			continue;
		}

		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = fileInfo.isDir() ? PackageEntryKind::Directory : PackageEntryKind::File;
		entry.sizeBytes = static_cast<quint64>(std::max<qint64>(0, fileInfo.size()));
		entry.compressedSizeBytes = entry.sizeBytes;
		entry.modifiedUtc = fileInfo.lastModified().toUTC();
		entry.typeHint = entryTypeHint(entry.virtualPath, entry.kind);
		entry.storageMethod = QStringLiteral("file");
		entry.sourceArchiveId = sourceId;
		if (entry.kind == PackageEntryKind::Directory) {
			entry.sizeBytes = 0; entry.compressedSizeBytes = 0; entry.readable = false;
			entry.storageMethod = QStringLiteral("directory");
			m_entries.push_back(entry);
			continue;
		}
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);
		QString identityError;
		const qint64 remainingHashes = m_indexLimits.maximumFingerprintBytes - m_indexFingerprintBytes;
		const qint64 chunks = fileInfo.size() / PackageFileIdentity::chunkBytes + (fileInfo.size() % PackageFileIdentity::chunkBytes != 0);
		if (chunks > remainingHashes / 32) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Folder source fingerprints exceed the %1-byte indexing limit.").arg(m_indexLimits.maximumFingerprintBytes); }
			return false;
		}
		if (!accountIndex(0, (fileInfo.absoluteFilePath().size() + fileInfo.canonicalFilePath().size()) * qint64(sizeof(QChar)), error)) { return false; }
		const auto identity = capturePackageFileIdentity(fileInfo.absoluteFilePath(), &identityError, m_loadControl, remainingHashes);
		if (m_loadControl.isCancelled && m_loadControl.isCancelled()) {
			if (error) { *error = identityError; }
			return false;
		}
		entry.readable = static_cast<bool>(identity);
		if (identity) {
			m_indexFingerprintBytes += identity->chunkHashes.size();
			m_folderIdentities.insert(entry.virtualPath, identity);
			entry.sizeBytes = static_cast<quint64>(identity->size);
			entry.compressedSizeBytes = entry.sizeBytes;
			entry.modifiedUtc = identity->modifiedUtc;
		} else {
			entry.note = identityError;
			if (!addWarning(entry.virtualPath, identityError, true, error)) { return false; }
		}
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

	PackageContentDevice file(m_fileIdentity, m_loadControl);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to open PAK file.");
		}
		return false;
	}

	const QByteArray header = file.read(12);
	if (header.size() != 12 || readLe32(header, 0) != kPakSignature) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Invalid PAK header.");
		}
		return false;
	}

	const qint32 directoryOffset = readSignedLe32(header, 4);
	const qint32 directoryLength = readSignedLe32(header, 8);
	if (directoryOffset < 12 || directoryLength < 0 || directoryLength % 64 != 0 || static_cast<qint64>(directoryOffset) + directoryLength > file.size()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Invalid PAK directory.");
		}
		return false;
	}

	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to seek to PAK directory.");
		}
		return false;
	}

	// Parse into a local list so a truncated directory cannot publish a partial
	// entry set under a PAK format tag.
	QVector<PackageEntry> parsed;
	const int count = directoryLength / 64;
	if (!accountIndex(count, directoryLength, error)) { return false; }
	for (int index = 0; index < count; ++index) {
		if (m_loadControl.progress && index % 64 == 0) { m_loadControl.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Indexing %1").arg(path), qint64(index) * 64, directoryLength); }
		if (!accountIndex(0, 0, error)) { return false; }
		const QByteArray record = file.read(64);
		if (record.size() != 64) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read PAK directory record.");
			}
			return false;
		}

		const QString rawName = fixedLatin1String(record.constData(), 56);
		if (!accountIndexPath(rawName, error)) { return false; }
		const PackageVirtualPath normalized = normalizePackageVirtualPath(rawName, false);
		if (!normalized.isSafe()) {
			if (!addWarning(rawName.trimmed(), QCoreApplication::translate("VibeStudioPackageArchive", "Skipped unsafe PAK entry: %1").arg(packagePathIssueDisplayName(normalized.issue)), true, error)) { return false; }
			continue;
		}

		const qint32 dataOffset = readSignedLe32(record, 56);
		const qint32 dataSize = readSignedLe32(record, 60);
		if (dataOffset < 0 || dataSize < 0 || static_cast<qint64>(dataOffset) + dataSize > file.size()) {
			if (!addWarning(normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageArchive", "Skipped PAK entry with invalid offset or size."), true, error)) { return false; }
			continue;
		}

		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = PackageEntryKind::File;
		entry.sizeBytes = static_cast<quint64>(dataSize);
		entry.sourceOrdinal = index;
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

	PackageContentDevice file(m_fileIdentity, m_loadControl);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to open WAD file.");
		}
		return false;
	}

	const QByteArray header = file.read(12);
	const QString magic = QString::fromLatin1(header.constData(), std::min<qsizetype>(4, header.size()));
	if (header.size() != 12 || !(magic == QStringLiteral("IWAD") || magic == QStringLiteral("PWAD") || magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3"))) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Invalid WAD header.");
		}
		return false;
	}

	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const qint32 lumpCount = readSignedLe32(header, 4);
	const qint32 directoryOffset = readSignedLe32(header, 8);
	const int recordSize = textureWad ? 32 : 16;
	if (lumpCount < 0 || directoryOffset < 12 || static_cast<qint64>(directoryOffset) + (static_cast<qint64>(lumpCount) * recordSize) > file.size()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Invalid WAD directory.");
		}
		return false;
	}

	if (!file.seek(directoryOffset)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to seek to WAD directory.");
		}
		return false;
	}

	QVector<PackageEntry> parsed;
	// A Doom WAD's namespace markers say what the lumps between them are:
	// X_START opens a namespace and X_END closes it, the doubled XX_ forms being
	// what a PWAD uses to add to an IWAD's (the Doom Wiki's "WAD" article).
	QStringList doomNamespaces;
	if (!accountIndex(lumpCount, qint64(lumpCount) * recordSize, error)) { return false; }
	for (int index = 0; index < lumpCount; ++index) {
		if (m_loadControl.progress && index % 64 == 0) { m_loadControl.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Indexing %1").arg(path), qint64(index) * recordSize, qint64(lumpCount) * recordSize); }
		if (!accountIndex(0, 0, error)) { return false; }
		const QByteArray record = file.read(recordSize);
		if (record.size() != recordSize) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read WAD directory record.");
			}
			return false;
		}

		const qint32 dataOffset = readSignedLe32(record, 0);
		const qint32 diskSize = readSignedLe32(record, 4);
		const qint32 logicalSize = textureWad ? readSignedLe32(record, 8) : diskSize;
		const quint8 compression = textureWad ? static_cast<quint8>(record[13]) : 0;
		const QString rawName = fixedLatin1String(record.constData() + (textureWad ? 16 : 8), textureWad ? 16 : 8);
		const QString displayName = rawName.isEmpty() ? QStringLiteral("lump-%1").arg(index, 4, 10, QLatin1Char('0')) : rawName;
		if (!accountIndexPath(displayName, error)) { return false; }
		const PackageVirtualPath normalized = normalizePackageVirtualPath(displayName, false);
		if (!normalized.isSafe()) {
			if (!addWarning(displayName.trimmed(), QCoreApplication::translate("VibeStudioPackageArchive", "Skipped unsafe WAD entry: %1").arg(packagePathIssueDisplayName(normalized.issue)), true, error)) { return false; }
			continue;
		}
		if (dataOffset < 0 || diskSize < 0 || logicalSize < 0 || static_cast<qint64>(dataOffset) + diskSize > file.size()) {
			if (!addWarning(normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageArchive", "Skipped WAD entry with invalid offset or size."), true, error)) { return false; }
			continue;
		}

		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = PackageEntryKind::File;
		entry.sizeBytes = static_cast<quint64>(logicalSize);
		entry.sourceOrdinal = index;
		entry.compressedSizeBytes = static_cast<quint64>(diskSize);
		entry.dataOffset = dataOffset;
		entry.typeHint = textureWad ? QStringLiteral("wad-texture") : QStringLiteral("wad-lump");
		entry.wadLumpType = textureWad ? static_cast<quint8>(record[12]) : 0;
		if (!textureWad) {
			// Namespaces nest (F1_ inside F_, a stray F_ inside S_), so an
			// _END closes the innermost open one of its kind, and the lumps
			// after it are back in the one around it.
			bool opens = false;
			const QString marked = doomNamespaceMarker(displayName, &opens);
			if (!marked.isEmpty()) {
				if (opens) {
					doomNamespaces.push_back(marked);
				} else if (const qsizetype open = doomNamespaces.lastIndexOf(marked); open >= 0) {
					doomNamespaces.removeAt(open);
				}
				entry.typeHint = QStringLiteral("wad-marker");
			} else if (!doomNamespaces.isEmpty()) {
				entry.typeHint = QStringLiteral("wad-%1").arg(doomNamespaces.last());
			}
		}
		entry.storageMethod = compression == 0 ? QStringLiteral("stored") : QStringLiteral("compressed-%1").arg(compression);
		entry.readable = compression == 0;
		entry.note = entry.readable ? QString() : QCoreApplication::translate("VibeStudioPackageArchive", "WAD2/WAD3 compressed lumps are listed but not decoded.");
		parsed.push_back(entry);
	}

	// A lump outside the namespaces that opens with a DMX sound header is a
	// sound, whatever its name: Heretic's and Hexen's have no DS prefix. Only
	// the first 8 bytes of each are read.
	if (!textureWad) {
		for (PackageEntry& entry : parsed) {
			if (!accountIndex(0, 0, error)) { return false; }
			if (entry.typeHint != QStringLiteral("wad-lump") || entry.sizeBytes < 8 || !file.seek(entry.dataOffset)) {
				continue;
			}
			if (dmxSoundHeaderLooksValid(file.read(8), static_cast<qint64>(entry.sizeBytes), entry.virtualPath)) {
				entry.typeHint = QStringLiteral("wad-sound");
			}
		}
	}

	m_format = PackageArchiveFormat::Wad;
	m_wadMagic = magic;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	const QString sourceId = QFileInfo(m_sourcePath).fileName();
	for (PackageEntry& entry : parsed) {
		entry.sourceArchiveId = sourceId;
	}
	m_entries = parsed;
	return true;
}

// ZIP/PK3 admission streams one central record at a time. Structural, ZIP64,
// descriptor and encoding checks share the bounded helpers in package_zip.cpp.
bool PackageArchive::loadZipFamily(const QString& path, PackageArchiveFormat format, QString* error)
{
	if (error) { error->clear(); }
	PackageContentDevice file(m_fileIdentity, m_loadControl);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) { *error = file.errorString(); }
		return false;
	}
	ZipDirectoryMetadata directory;
	if (!readZipDirectoryMetadata(file, m_indexLimits.maximumMetadataBytes, &directory, error)) { return false; }
	if (directory.entries > quint64(m_indexLimits.maximumEntries)
		|| directory.size > quint64(m_indexLimits.maximumMetadataBytes)
		|| quint64(directory.additionalMetadataBytes) > quint64(m_indexLimits.maximumMetadataBytes) - directory.size) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "ZIP directory exceeds the indexing limits of %1 entries or %2 metadata bytes.").arg(m_indexLimits.maximumEntries).arg(m_indexLimits.maximumMetadataBytes); }
		return false;
	}
	if (!accountIndex(qsizetype(directory.entries), qint64(directory.size) + directory.additionalMetadataBytes, error)) { return false; }
	PackageContentDevice directoryFile(m_fileIdentity, m_loadControl);
	if (!directoryFile.open(QIODevice::ReadOnly)) {
		if (error) { *error = directoryFile.errorString(); }
		return false;
	}
	QVector<PackageEntry> parsed;
	quint64 consumed = 0;
	for (quint64 index = 0; index < directory.entries; ++index) {
		if (m_loadControl.progress && index % 64 == 0) { m_loadControl.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Indexing %1").arg(path), consumed, directory.size); }
		if (m_loadControl.isCancelled && m_loadControl.isCancelled()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package opening cancelled."); }
			return false;
		}
		QByteArray central;
		if (directory.size - consumed < 46
			|| !readAt(directoryFile, qint64(directory.offset + consumed), 46, &central)
			|| readLe32(central, 0) != kZipCentralDirectorySignature) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Invalid ZIP central directory record."); }
			return false;
		}
		const quint16 nameLength = readLe16(central, 28);
		const qsizetype recordSize = 46 + qsizetype(nameLength) + readLe16(central, 30) + readLe16(central, 32);
		QByteArray tail;
		if (quint64(recordSize) > directory.size - consumed
			|| !readAt(directoryFile, qint64(directory.offset + consumed + 46), recordSize - 46, &tail)) {
			if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Invalid ZIP central directory name length."); }
			return false;
		}
		central += tail;
		consumed += recordSize;
		ZipEntryMetadata metadata;
		QString issue;
		if (!readZipEntryMetadata(central, &metadata, &issue)) {
			if (!addWarning({}, QCoreApplication::translate("VibeStudioPackageArchive", "Skipped ZIP entry %1: %2").arg(index + 1).arg(issue), true, error)) { return false; }
			continue;
		}
		if (!accountIndexPath(metadata.name, error)) { return false; }
		const bool directoryEntry = metadata.name.endsWith('/');
		const PackageVirtualPath normalized = normalizePackageVirtualPath(metadata.name, false);
		if (!normalized.isSafe()) {
			if (!addWarning(metadata.name.trimmed(), QCoreApplication::translate("VibeStudioPackageArchive", "Skipped unsafe ZIP entry: %1").arg(packagePathIssueDisplayName(normalized.issue)), true, error)) { return false; }
			continue;
		}
		if (metadata.disk != 0) {
			if (!addWarning(normalized.normalizedPath, QCoreApplication::translate("VibeStudioPackageArchive", "Skipped ZIP entry stored on another disk of a multi-disk archive."), true, error)) { return false; }
			continue;
		}
		qint64 dataOffset = -1;
		QString headerIssue;
		readZipLocalMetadata(file, directory.offset, metadata, QByteArrayView(central).sliced(46, nameLength), &dataOffset, &headerIssue);
		const bool encrypted = (metadata.flags & 0x2041) != 0;
		const bool supportedFlags = (metadata.flags & ~quint16(0x080f)) == 0;
		const bool supportedMethod = metadata.method == 0 || metadata.method == 8;
		PackageEntry entry;
		entry.virtualPath = normalized.normalizedPath;
		entry.kind = directoryEntry ? PackageEntryKind::Directory : PackageEntryKind::File;
		entry.sizeBytes = metadata.size;
		entry.sourceOrdinal = qint64(index);
		entry.compressedSizeBytes = metadata.compressedSize;
		entry.dataOffset = dataOffset;
		entry.modifiedUtc = dosDateTimeUtc(readLe16(central, 14), readLe16(central, 12));
		entry.typeHint = entryTypeHint(entry.virtualPath, entry.kind);
		entry.storageMethod = zipMethodName(metadata.method);
		entry.crc32 = metadata.crc;
		entry.hasCrc32 = !directoryEntry;
		entry.nestedArchiveCandidate = packageEntryLooksNestedArchive(entry.virtualPath);
		entry.readable = !directoryEntry && supportedMethod && supportedFlags && !encrypted && dataOffset >= 0;
		if (encrypted) {
			entry.note = QCoreApplication::translate("VibeStudioPackageArchive", "Encrypted ZIP entries are not readable; this reader does not implement ZIP decryption.");
		} else if (!supportedFlags) {
			entry.note = QCoreApplication::translate("VibeStudioPackageArchive", "ZIP entry requires unsupported processing flags; listing only.");
		} else if (!supportedMethod) {
			entry.note = QCoreApplication::translate("VibeStudioPackageArchive", "ZIP entry uses %1 compression, which this reader does not decode; listing only.").arg(entry.storageMethod);
		} else if (!headerIssue.isEmpty()) {
			entry.note = headerIssue;
		}
		// Directories do not stream a payload during validation. Their header
		// failures must therefore be exposed as saving-blocking diagnostics.
		if (directoryEntry && !entry.note.isEmpty() && !addWarning(entry.virtualPath, entry.note, true, error)) { return false; }
		parsed.push_back(entry);
	}
	if (!finishZipDirectory(directoryFile, directory, consumed, error)) { return false; }
	m_format = format;
	m_sourcePath = QFileInfo(path).absoluteFilePath();
	const QString sourceId = QFileInfo(m_sourcePath).fileName();
	for (PackageEntry& entry : parsed) { entry.sourceArchiveId = sourceId; }
	m_entries = parsed;
	return true;
}

bool PackageArchive::readFileEntryBytes(const PackageEntry& entry, QByteArray* out, QString* error, qint64 maxBytes) const
{
	QString nativeRelative = entry.virtualPath;
	nativeRelative.replace('/', QDir::separator());
	const QString filePath = QDir(m_sourcePath).filePath(nativeRelative);
	if (!packagePathIsInsideDirectory(m_sourcePath, filePath)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Folder entry escapes the package root.");
		}
		return false;
	}
	const QFileInfo before(filePath);
	if (!before.isFile() || before.size() < 0 || static_cast<quint64>(before.size()) != entry.sizeBytes
		|| before.lastModified().toUTC() != entry.modifiedUtc) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "The folder entry changed after it was listed. Refresh the package before reading it."); }
		return false;
	}

	PackageContentDevice file(fileIdentity(entry.virtualPath));
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to open folder entry.");
		}
		return false;
	}
	const qint64 available = std::max<qint64>(0, file.size());
	const qint64 toRead = maxBytes >= 0 ? std::min(maxBytes, available) : available;
	if (out) {
		*out = file.read(toRead);
		const QFileInfo after(filePath);
		if (out->size() != toRead || file.failed() || before.size() != after.size() || before.lastModified() != after.lastModified()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read folder entry.");
			}
			out->clear();
			return false;
		}
	}
	return true;
}

bool PackageArchive::streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink,
	QString* error, const std::function<bool()>& isCancelled) const
{
	if (m_snapshotReader) { return m_snapshotReader->streamEntryAt(index, sink, error, isCancelled); }
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) {
		if (error) { *error = message; }
		return false;
	};
	const auto cancelled = [&]() { return isCancelled && isCancelled(); };
	if (!m_open || index < 0 || index >= m_entries.size() || !sink) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Invalid package entry streaming request."));
	}
	const auto& entry = m_entries.at(index);
	if (entry.kind != PackageEntryKind::File || !entry.readable) {
		return fail(entry.note.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "Package entry is not a readable file.") : entry.note);
	}
	if (cancelled()) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package read cancelled.")); }
	if (!sourceMatchesSnapshot()) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The source package changed after it was opened. Reopen it before reading entries."));
	}
	QString filePath = m_sourcePath;
	const bool folder = m_format == PackageArchiveFormat::Folder;
	const bool deflated = !folder && isDeflatedStorage(entry.storageMethod);
	if (folder) {
		filePath = QDir(m_sourcePath).filePath(entry.virtualPath);
		if (!packagePathIsInsideDirectory(m_sourcePath, filePath)) {
			return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Folder entry escapes the package root."));
		}
	}
	const QFileInfo before(filePath);
	if (folder && (!before.isFile() || before.size() < 0 || static_cast<quint64>(before.size()) != entry.sizeBytes
		|| before.lastModified().toUTC() != entry.modifiedUtc)) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The folder entry changed after it was listed. Refresh the package before reading it."));
	}
	PackageReadControl control; control.isCancelled = isCancelled;
	PackageContentDevice file(fileIdentity(folder ? entry.virtualPath : QString()), control);
	if (!file.open(QIODevice::ReadOnly)) { return fail(file.errorString()); }
	const quint64 storedSize = folder ? entry.sizeBytes : entry.compressedSizeBytes;
	const qint64 offset = folder ? 0 : entry.dataOffset;
	if (offset < 0 || offset > file.size() || storedSize > static_cast<quint64>(file.size() - offset)
		|| entry.sizeBytes > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package entry extends beyond the file."));
	}
	if (!deflated && storedSize != entry.sizeBytes) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Stored package entry sizes do not match."));
	}
	if (!file.seek(offset)) { return fail(file.errorString()); }
	quint32 crc = 0;
	quint64 received = 0;
	bool sinkFailed = false;
	const auto accept = [&](QByteArrayView chunk) {
		if (cancelled()) { return false; }
		crc = crc32Bytes(QByteArray::fromRawData(chunk.data(), chunk.size()), crc);
		received += static_cast<quint64>(chunk.size());
		if (!sink(chunk)) { sinkFailed = true; return false; }
		return true;
	};
	if (deflated) {
		const auto inflated = inflateRawToSink(file, static_cast<qint64>(storedSize), static_cast<qint64>(entry.sizeBytes), accept, isCancelled);
		if (!inflated.ok) {
			return fail(cancelled() ? QCoreApplication::translate("VibeStudioPackageArchive", "Package read cancelled.")
				: sinkFailed ? QCoreApplication::translate("VibeStudioPackageArchive", "The package byte consumer stopped the read.") : inflated.error);
		}
		if (static_cast<quint64>(inflated.bytesConsumed) != storedSize) {
			return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Compressed package entry contains bytes after its DEFLATE stream."));
		}
	} else {
		while (received < storedSize) {
			if (cancelled()) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package read cancelled.")); }
			const qint64 count = static_cast<qint64>(qMin<quint64>(65536, storedSize - received));
			const QByteArray chunk = file.read(count);
			if (chunk.size() != count) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read the complete package entry.")); }
			if (!accept(QByteArrayView(chunk))) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The package byte consumer stopped the read.")); }
		}
	}
	if (cancelled()) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package read cancelled.")); }
	if (received != entry.sizeBytes) { return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package entry size does not match its directory record.")); }
	if (entry.hasCrc32 && crc != entry.crc32) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "Package entry failed its CRC check; the archive is damaged."));
	}
	const QFileInfo after(filePath);
	if (file.failed()) { return fail(file.errorString()); }
	if (!sourceMatchesSnapshot() || before.size() != after.size() || before.lastModified() != after.lastModified()) {
		return fail(QCoreApplication::translate("VibeStudioPackageArchive", "The package source changed while reading an entry."));
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
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Invalid package entry offset or size.");
		}
		return false;
	}
	if (deflated && entry.sizeBytes > static_cast<quint64>(kMaximumInflateBytes)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Compressed package entry is too large to inflate in memory.");
		}
		return false;
	}

	PackageContentDevice file(m_fileIdentity);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to open package file.");
		}
		return false;
	}
	if (entry.dataOffset > file.size() || storedSize > static_cast<quint64>(file.size() - entry.dataOffset)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry extends beyond the file.");
		}
		return false;
	}
	if (!file.seek(entry.dataOffset)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to seek to package entry.");
		}
		return false;
	}

	if (!deflated) {
		const qint64 toRead = maxBytes >= 0 ? std::min(maxBytes, static_cast<qint64>(storedSize)) : static_cast<qint64>(storedSize);
		if (out) {
			*out = file.read(toRead);
			if (out->size() != toRead || file.failed()) {
				out->clear();
				if (error) {
					*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read package entry.");
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
					*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry failed its CRC check; the archive is damaged.");
				}
				// Hand back nothing rather than the damaged bytes: a caller that
				// checks only for non-empty output must not be able to use them.
				out->clear();
				return false;
			}
		}
		return true;
	}

	// Prefix previews stop the streaming inflater once the requested bytes are
	// available. They cannot verify the unseen tail's size or CRC. Whole-entry
	// consumers (including texture imports) still use the checked path below.
	if (maxBytes >= 0 && quint64(maxBytes) < entry.sizeBytes) {
		QByteArray prefix;
		bool complete = maxBytes == 0;
		if (!complete) {
			const auto streamed = inflateRawToSink(file, static_cast<qint64>(storedSize), static_cast<qint64>(entry.sizeBytes), [&](QByteArrayView chunk) {
				const auto count = std::min<qint64>(chunk.size(), maxBytes - prefix.size());
				prefix.append(chunk.data(), count);
				complete = prefix.size() == maxBytes;
				return !complete;
			});
			if (!complete) {
				if (error) { *error = streamed.error.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read the requested compressed entry prefix.") : streamed.error; }
				return false;
			}
		}
		if (file.failed()) { if (error) { *error = file.errorString(); } return false; }
		if (out) { *out = std::move(prefix); }
		return true;
	}

	// Whole streams verify the declared output size and CRC. expectedSize caps
	// allocation even when the central directory lies about the decoded length.
	const QByteArray compressed = file.read(static_cast<qint64>(storedSize));
	if (compressed.size() != static_cast<qsizetype>(storedSize) || file.failed()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unable to read compressed package entry.");
		}
		return false;
	}

	const InflateResult inflated = inflateRaw(compressed, static_cast<qint64>(entry.sizeBytes));
	if (!inflated.ok) {
		if (error) {
			*error = inflated.error.isEmpty()
				? QCoreApplication::translate("VibeStudioPackageArchive", "Unable to inflate the compressed package entry.")
				: QCoreApplication::translate("VibeStudioPackageArchive", "Unable to inflate the compressed package entry: %1").arg(inflated.error);
		}
		return false;
	}
	if (static_cast<quint64>(inflated.data.size()) != entry.sizeBytes) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Inflated package entry size does not match the central directory (%1 of %2 bytes).")
				.arg(QString::number(inflated.data.size()), QString::number(entry.sizeBytes));
		}
		return false;
	}

	const bool partial = maxBytes >= 0 && maxBytes < static_cast<qint64>(entry.sizeBytes);
	if (entry.hasCrc32 && !partial) {
		const quint32 actual = crc32Bytes(inflated.data);
		if (actual != entry.crc32) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioPackageArchive", "CRC-32 mismatch for package entry (expected %1, got %2); the archive is corrupt.")
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

bool PackageArchive::finalizeEntries(QString* error)
{
	if (m_loadControl.progress) { m_loadControl.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Preparing package index…"), 0, 0); }
	for (qsizetype index = 0; index < m_entries.size(); ++index) {
		if (!accountIndex(0, 0, error)) { return false; }
		// Loaders set a physical ordinal where records may have been skipped.
		if (m_entries[index].sourceOrdinal < 0) { m_entries[index].sourceOrdinal = index; }
	}
	const QString sourceId = QFileInfo(m_sourcePath).fileName();
	if (!addPackageIndexDirectories(&m_entries, sourceId,
		[&](const QString& path) { return accountIndex(1, 0, error) && accountIndexPath(path, error); },
		[&] { return !accountIndex(0, 0, error); })) { return false; }

	QSet<QString> seen;
	for (PackageEntry& entry : m_entries) {
		if (!accountIndex(0, 0, error)) { return false; }
		if (!accountIndex(0, entry.note.size() * qint64(sizeof(QChar)), error)) { return false; }
		entry.typeHint = entry.typeHint.isEmpty() ? entryTypeHint(entry.virtualPath, entry.kind) : entry.typeHint;
		entry.sourceArchiveId = entry.sourceArchiveId.isEmpty() ? sourceId : entry.sourceArchiveId;
		entry.nestedArchiveCandidate = entry.nestedArchiveCandidate || packageEntryLooksNestedArchive(entry.virtualPath);
		const QString key = duplicateKey(entry.virtualPath);
		if (seen.contains(key) && m_format != PackageArchiveFormat::Wad) {
			if (!addWarning(entry.virtualPath, packageIndexDuplicateWarning(), false, error)) { return false; }
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
	PackageArchiveSummary summary; summary.sourcePath = m_sourcePath; summary.format = m_format;
	summary.warningCount = static_cast<int>(m_warnings.size());
	if (!collectPackageSummary(m_entries, &summary, m_loadControl, error) || !accountIndex(0, 0, error)) { return false; }
	m_summary = std::move(summary); return true;
}

bool PackageArchive::accountIndex(qsizetype entries, qint64 metadataBytes, QString* error)
{
	if (indexCancelled(m_loadControl, error)) { return false; }
	PackageIndexUsage usage = indexUsage();
	if (!admitPackageIndex(&usage, m_indexLimits, entries, metadataBytes, error)) { return false; }
	m_indexEntries = usage.entries; m_indexMetadataBytes = usage.metadataBytes;
	return true;
}
bool PackageArchive::accountIndexPath(const QString& path, QString* error)
{
	if (indexCancelled(m_loadControl, error)) { return false; }
	PackageIndexUsage usage = indexUsage();
	if (!admitPackageIndexPath(&usage, m_indexLimits, path, error)) { return false; }
	m_indexMetadataBytes = usage.metadataBytes;
	return true;
}
bool PackageArchive::addWarning(const QString& virtualPath, const QString& message, bool blocksSaving, QString* error)
{
	if (message.trimmed().isEmpty()) { return true; }
	if (!accountIndex(0, (virtualPath.size() + message.size()) * qint64(sizeof(QChar)), error)) { return false; }
	m_warnings.push_back({virtualPath.trimmed(), message.trimmed(), blocksSaving});
	return true;
}

PackageArchiveSession::PackageArchiveSession(const PackageIndexLimits& limits) : m_limits(limits) {}
PackageIndexUsage PackageArchiveSession::indexUsage() const { return m_usage; }

void PackageArchiveSession::updateUsage()
{
	m_usage = {};
	const auto add = [this](const LayerState& state) {
		m_usage.entries += state.usage.entries; m_usage.metadataBytes += state.usage.metadataBytes;
		m_usage.fingerprintBytes += state.usage.fingerprintBytes;
	};
	if (m_hasPrimaryLayer) { add(m_primary); }
	for (const auto& state : m_mountedLayers) { add(state); }
}

bool PackageArchiveSession::admitLayer(const PackageMountLayer& layer, const std::shared_ptr<PackageArchive>& archive,
	PackageIndexUsage* usage, QString* error, const PackageReadControl& control) const
{
	if (!validPackageIndexLimits(m_limits, error) || indexCancelled(control, error)) { return false; }
	if (depth() >= layerCeiling) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "A package session supports at most %1 layers. Unmount a layer before adding another.").arg(layerCeiling); }
		return false;
	}
	PackageIndexUsage total = m_usage;
	const auto charge = [&](const PackageIndexUsage& addition) { return chargeLayerUsage(&total, addition, m_limits, error); };
	if (!charge({0, (layer.id.size() + layer.displayName.size() + layer.sourcePath.size() + layer.mountPath.size()) * qint64(sizeof(QChar)), 0})) { return false; }
	const qsizetype mountDepth = layer.mountPath.isEmpty() ? 0 : layer.mountPath.count(QLatin1Char('/')) + 1;
	if (mountDepth > m_limits.maximumPathDepth) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Mounted entry paths exceed the indexing depth limit of %1 components.").arg(m_limits.maximumPathDepth); }
		return false;
	}
	if (!archive) {
		if (!charge({layer.entryCount, 0, 0})) { return false; }
	} else {
		if (!charge(archive->indexUsage())) { return false; }
		const auto entries = archive->entries();
		if (!layer.mountPath.isEmpty() && !entries.isEmpty()) {
			// Reserve every prefix even when another layer currently provides it.
			// Popping an override can reveal these directories again.
			for (QString parent = layer.mountPath; !parent.isEmpty(); parent = packageVirtualPathParent(parent)) {
				if (!charge({1, parent.size() * qint64(sizeof(QChar)), 0})) { return false; }
			}
			for (const auto& entry : entries) {
				if (indexCancelled(control, error)) { return false; }
				const qsizetype depth = entry.virtualPath.count(QLatin1Char('/')) + 1 - entry.virtualPath.endsWith(QLatin1Char('/'));
				if (depth + mountDepth > m_limits.maximumPathDepth) {
					if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Mounted entry paths exceed the indexing depth limit of %1 components.").arg(m_limits.maximumPathDepth); }
					return false;
				}
				const qsizetype relocatedLength = layer.mountPath.size() + 1 + entry.virtualPath.size();
				if (relocatedLength > kMaximumPackageVirtualPathLength) {
					if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Mounted entry paths exceed the length limit of %1 characters.").arg(kMaximumPackageVirtualPathLength); }
					return false;
				}
				if (!charge({0, relocatedLength * qint64(sizeof(QChar)), 0})) { return false; }
			}
		}
		const qint64 warningPrefix = (layer.id.size() + 2) * qint64(sizeof(QChar));
		for (const auto& warning : archive->warnings()) {
			Q_UNUSED(warning);
			if (indexCancelled(control, error) || !charge({0, warningPrefix, 0})) { return false; }
		}
	}
	*usage = {total.entries - m_usage.entries, total.metadataBytes - m_usage.metadataBytes, total.fingerprintBytes - m_usage.fingerprintBytes};
	return true;
}

bool PackageArchiveSession::setPrimaryLayer(const PackageMountLayer& layer, QString* error)
{
	PackageArchiveSession candidate(m_limits);
	PackageMountLayer normalized = layer;
	PackageIndexUsage usage;
	if (!candidate.normalizeLayer(&normalized, error) || !candidate.admitLayer(normalized, {}, &usage, error)) { return false; }
	candidate.m_primary = {normalized, {}, usage}; candidate.m_hasPrimaryLayer = true;
	candidate.updateUsage();
	*this = std::move(candidate);
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
	PackageIndexUsage usage;
	if (!normalizeLayer(&normalized, error) || !admitLayer(normalized, {}, &usage, error)) { return false; }
	m_mountedLayers.push_back({normalized, {}, usage}); updateUsage();
	m_indexDirty = true;
	return true;
}

bool PackageArchiveSession::popMountedLayer()
{
	if (m_mountedLayers.isEmpty()) {
		return false;
	}
	m_mountedLayers.pop_back();
	updateUsage();
	m_indexDirty = true;
	return true;
}

void PackageArchiveSession::clearMountedLayers()
{
	m_mountedLayers.clear();
	updateUsage();
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
	m_usage = {};
	m_primary = LayerState {};
	m_hasPrimaryLayer = false;
	m_mountedLayers.clear();
	m_indexDirty = true;
	m_mergedEntries.clear(); m_summary = {};
	m_entryOwner.clear();
}

bool PackageArchiveSession::openPrimaryArchive(const QString& path, QString* error, const PackageReadControl& control)
{
	return openLayer(path, {}, true, error, control);
}

bool PackageArchiveSession::mountArchive(const QString& path, const QString& mountPath, QString* error, const PackageReadControl& control)
{
	return openLayer(path, mountPath, false, error, control);
}

bool PackageArchiveSession::openLayer(const QString& path, const QString& mountPath, bool primary, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	PackageArchiveSession candidate = primary ? PackageArchiveSession(m_limits) : *this;
	PackageMountLayer layer;
	layer.sourcePath = QFileInfo(path).absoluteFilePath(); layer.mountPath = mountPath;
	layer.id = candidate.uniqueLayerId(QFileInfo(layer.sourcePath).fileName());
	layer.displayName = QFileInfo(layer.sourcePath).fileName(); layer.readOnly = true;
	PackageIndexUsage preliminary;
	if (!candidate.normalizeLayer(&layer, error) || !candidate.admitLayer(layer, {}, &preliminary, error, control)) { return false; }
	PackageIndexLimits remaining = m_limits;
	remaining.maximumEntries -= candidate.m_usage.entries + preliminary.entries;
	remaining.maximumMetadataBytes -= candidate.m_usage.metadataBytes + preliminary.metadataBytes;
	remaining.maximumFingerprintBytes -= candidate.m_usage.fingerprintBytes;
	auto archive = std::make_shared<PackageArchive>();
	if (!archive->load(layer.sourcePath, error, control, remaining)) { return false; }
	layer.sourcePath = archive->sourcePath(); layer.format = archive->format();
	layer.entryCount = static_cast<int>(archive->entries().size());
	PackageIndexUsage usage;
	if (!candidate.admitLayer(layer, archive, &usage, error, control)) { return false; }
	if (primary) { candidate.m_primary = {layer, archive, usage}; candidate.m_hasPrimaryLayer = true; }
	else { candidate.m_mountedLayers.push_back({layer, archive, usage}); }
	candidate.updateUsage(); candidate.m_indexDirty = true;
	if (!candidate.rebuildIndex(error, control) || indexCancelled(control, error)) { return false; }
	*this = std::move(candidate);
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
			merged.push_back({warning.virtualPath, QStringLiteral("%1: %2").arg(state.layer.id, warning.message), warning.blocksSaving});
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
	return m_summary;
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
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "No package layer is open in this session.");
		}
		return false;
	}

	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue));
		}
		return false;
	}

	rebuildIndex();
	const auto owner = m_entryOwner.constFind(duplicateKey(normalized.normalizedPath));
	if (owner == m_entryOwner.constEnd()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry not found.");
		}
		return false;
	}

	const LayerState* state = layerStateAt(owner.value());
	if (!state || !state->archive) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry not found.");
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
bool PackageArchiveSession::rebuildIndex(QString* error, const PackageReadControl& control) const
{
	if (!m_indexDirty) { return !indexCancelled(control, error); }
	if (control.progress) { control.progress(QCoreApplication::translate("VibeStudioPackageArchive", "Combining package layers…"), 0, 0); }
	if (indexCancelled(control, error)) { return false; }
	QHash<QString, PackageEntry> byKey;
	QHash<QString, int> owners;
	const auto mergeLayer = [&](int layerIndex) {
		const LayerState* state = layerStateAt(layerIndex);
		if (!state || !state->archive) { return true; }
		const auto layerEntries = state->archive->entries();
		QSet<QString> layerPaths;
		for (PackageEntry entry : layerEntries) {
			if (indexCancelled(control, error)) { return false; }
			if (!state->layer.mountPath.isEmpty()) { entry.virtualPath = state->layer.mountPath + QLatin1Char('/') + entry.virtualPath; }
			entry.layerId = state->layer.id; entry.sourceArchiveId = state->layer.id;
			const QString key = duplicateKey(entry.virtualPath);
			// The owning reader resolves the first physical duplicate. Match its
			// metadata instead of advertising the last record's size/offset.
			if (layerPaths.contains(key)) { continue; }
			layerPaths.insert(key); byKey.insert(key, entry); owners.insert(key, layerIndex);
		}
		return true;
	};
	if (!mergeLayer(0)) { return false; }
	for (int index = 0; index < m_mountedLayers.size(); ++index) { if (!mergeLayer(index + 1)) { return false; } }
	QVector<PackageEntry> merged;
	merged.reserve(byKey.size());
	for (auto it = byKey.cbegin(); it != byKey.cend(); ++it) {
		if (indexCancelled(control, error)) { return false; }
		merged.push_back(it.value());
	}
	const QString sessionId = m_hasPrimaryLayer ? m_primary.layer.id : QStringLiteral("session");
	if (!addPackageIndexDirectories(&merged, sessionId, {}, [&] { return indexCancelled(control, error); })) { return false; }
	std::sort(merged.begin(), merged.end(), entryPathLess);
	if (indexCancelled(control, error)) { return false; }
	PackageArchiveSummary summary;
	summary.sourcePath = m_hasPrimaryLayer ? m_primary.layer.sourcePath : QString();
	summary.format = m_hasPrimaryLayer ? m_primary.layer.format : PackageArchiveFormat::Unknown;
	summary.warningCount = static_cast<int>(warnings().size());
	if (!collectPackageSummary(merged, &summary, control, error)) { return false; }
	m_summary = std::move(summary);
	m_mergedEntries = std::move(merged); m_entryOwner = std::move(owners); m_indexDirty = false;
	return true;
}

bool PackageArchiveSession::normalizeLayer(PackageMountLayer* layer, QString* error) const
{
	if (error) {
		error->clear();
	}
	if (!layer) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Package layer is missing.");
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
		return QCoreApplication::translate("VibeStudioPackageArchive", "Folder");
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
	return QCoreApplication::translate("VibeStudioPackageArchive", "Unknown");
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
			QCoreApplication::translate("VibeStudioPackageArchive", "Folder"),
			{},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract-source"), QStringLiteral("nested-mount")},
			QCoreApplication::translate("VibeStudioPackageArchive", "Directory tree browsed as a read-only package: recursive listing, byte reads, and extraction as a copy source."),
		},
		{
			PackageArchiveFormat::Pak,
			QStringLiteral("pak"),
			QCoreApplication::translate("VibeStudioPackageArchive", "Quake PAK"),
			{QStringLiteral("pak")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("nested-mount")},
			QCoreApplication::translate("VibeStudioPackageArchive", "idTech2 PACK archive: lists, reads, and extracts entries. PAK stores every entry uncompressed, so all entries are readable."),
		},
		{
			PackageArchiveFormat::Wad,
			QStringLiteral("wad"),
			QCoreApplication::translate("VibeStudioPackageArchive", "Doom/Quake WAD"),
			{QStringLiteral("wad"), QStringLiteral("wad2"), QStringLiteral("wad3")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("nested-mount")},
			QCoreApplication::translate("VibeStudioPackageArchive", "Doom IWAD/PWAD lumps and Quake/Half-Life WAD2/WAD3 texture lumps: lists, reads, and extracts uncompressed lumps. WAD2/WAD3 compressed lumps are listed but not decoded."),
		},
		{
			PackageArchiveFormat::Zip,
			QStringLiteral("zip"),
			QStringLiteral("ZIP"),
			{QStringLiteral("zip"), QStringLiteral("pk4"), QStringLiteral("pkz")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("deflate"), QStringLiteral("zip64"), QStringLiteral("nested-mount")},
			QCoreApplication::translate("VibeStudioPackageArchive", "ZIP archive with stored and DEFLATE entries, ZIP64 central directories, and CRC-32 verified reads. Encrypted entries and other compression methods are listed but not decoded."),
		},
		{
			PackageArchiveFormat::Pk3,
			QStringLiteral("pk3"),
			QCoreApplication::translate("VibeStudioPackageArchive", "Quake III PK3"),
			{QStringLiteral("pk3")},
			{QStringLiteral("list"), QStringLiteral("read"), QStringLiteral("extract"), QStringLiteral("deflate"), QStringLiteral("zip64"), QStringLiteral("nested-mount")},
			QCoreApplication::translate("VibeStudioPackageArchive", "idTech3 PK3 archive over the ZIP container: stored and DEFLATE entries, ZIP64, CRC-32 verified reads, and pk3 shadowing when several layers are mounted in one session."),
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
		return QCoreApplication::translate("VibeStudioPackageArchive", "File");
	case PackageEntryKind::Directory:
		return QCoreApplication::translate("VibeStudioPackageArchive", "Directory");
	}
	return QCoreApplication::translate("VibeStudioPackageArchive", "File");
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
		return QCoreApplication::translate("VibeStudioPackageArchive", "safe");
	case PackagePathIssue::Empty:
		return QCoreApplication::translate("VibeStudioPackageArchive", "empty path");
	case PackagePathIssue::AbsolutePath:
		return QCoreApplication::translate("VibeStudioPackageArchive", "absolute paths are not allowed inside packages");
	case PackagePathIssue::DriveQualifiedPath:
		return QCoreApplication::translate("VibeStudioPackageArchive", "drive-qualified paths are not allowed inside packages");
	case PackagePathIssue::TraversalSegment:
		return QCoreApplication::translate("VibeStudioPackageArchive", "parent-directory traversal is not allowed inside packages");
	case PackagePathIssue::CurrentDirectorySegment:
		return QCoreApplication::translate("VibeStudioPackageArchive", "current-directory segments are not allowed inside packages");
	case PackagePathIssue::Colon:
		return QCoreApplication::translate("VibeStudioPackageArchive", "colon characters are not allowed inside package paths");
	case PackagePathIssue::ControlCharacter:
		return QCoreApplication::translate("VibeStudioPackageArchive", "control characters are not allowed inside package paths");
	case PackagePathIssue::TooLong:
		return QCoreApplication::translate("VibeStudioPackageArchive", "package path is too long");
	case PackagePathIssue::ReservedDeviceName:
		return QCoreApplication::translate("VibeStudioPackageArchive", "the path contains a reserved device name (CON, PRN, AUX, NUL, COM1-9, LPT1-9)");
	case PackagePathIssue::TrailingDotOrSpace:
		return QCoreApplication::translate("VibeStudioPackageArchive", "path segments may not end with a dot or a space");
	}
	return QCoreApplication::translate("VibeStudioPackageArchive", "unknown package path issue");
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

StudioQueryProperties packageEntryQueryProperties(const PackageEntry& entry)
{
	StudioQueryProperties properties;
	const QString name = packageVirtualPathFileName(entry.virtualPath);
	properties.insert(QStringLiteral("path"), entry.virtualPath);
	properties.insert(QStringLiteral("name"), name);
	const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
	if (dot > 0 && entry.kind != PackageEntryKind::Directory) {
		properties.insert(QStringLiteral("ext"), name.mid(dot + 1).toLower());
	}
	properties.insert(QStringLiteral("folder"), packageVirtualPathParent(entry.virtualPath));
	if (!entry.typeHint.isEmpty()) {
		properties.insert(QStringLiteral("type"), entry.typeHint);
	}
	if (!entry.storageMethod.isEmpty()) {
		properties.insert(QStringLiteral("storage"), entry.storageMethod);
	}
	properties.insert(QStringLiteral("kind"), packageEntryKindId(entry.kind));
	if (entry.kind != PackageEntryKind::Directory) {
		properties.insert(QStringLiteral("size"), QString::number(entry.sizeBytes));
		properties.insert(QStringLiteral("packed"), QString::number(entry.compressedSizeBytes));
	}
	return properties;
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

QString packageResolvedAbsolutePath(const QString& path, const PackageReadControl& control)
{
	QString resolved = canonicalizedAbsolutePath(path, control);
#ifdef Q_OS_WIN
	if (resolved.size() == 2 && resolved.at(1) == ':') { resolved += '/'; }
#endif
	return resolved;
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
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Output root is empty.");
		}
		return {};
	}

	const PackageVirtualPath normalized = normalizePackageVirtualPath(virtualPath, false);
	if (!normalized.isSafe()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue));
		}
		return {};
	}

	const PackagePathIssue filesystemIssue = packageFilesystemPathIssue(normalized.normalizedPath);
	if (filesystemIssue != PackagePathIssue::None) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Unsafe package path: %1").arg(packagePathIssueDisplayName(filesystemIssue));
		}
		return {};
	}

	QString nativeRelative = normalized.normalizedPath;
	nativeRelative.replace('/', QDir::separator());
	const QString candidate = QDir(root).filePath(nativeRelative);
	if (!packagePathIsInsideDirectory(root, candidate)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioPackageArchive", "Output path escapes the selected root.");
		}
		return {};
	}
	return QDir::cleanPath(candidate);
}

QString packageExtractionReportText(const PackageExtractionReport& report)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Package extraction");
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Source: %1").arg(QDir::toNativeSeparators(report.sourcePath));
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Target: %1").arg(QDir::toNativeSeparators(report.targetDirectory));
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Mode: %1").arg(report.dryRun ? QCoreApplication::translate("VibeStudioPackageArchive", "dry-run") : QCoreApplication::translate("VibeStudioPackageArchive", "write"));
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Overwrite: %1").arg(report.overwriteExisting ? QCoreApplication::translate("VibeStudioPackageArchive", "yes") : QCoreApplication::translate("VibeStudioPackageArchive", "no"));
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "State: %1").arg(report.cancelled ? QCoreApplication::translate("VibeStudioPackageArchive", "cancelled") : (report.succeeded() ? QCoreApplication::translate("VibeStudioPackageArchive", "completed") : QCoreApplication::translate("VibeStudioPackageArchive", "failed")));
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Requested entries: %1").arg(report.requestedCount);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Processed entries: %1").arg(report.processedCount);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Written entries: %1").arg(report.writtenCount);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Created directories: %1").arg(report.directoryCount);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Skipped entries: %1").arg(report.skippedCount);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Errors: %1").arg(report.errorCount);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Bytes written: %1").arg(report.totalBytes);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Payload bytes read: %1").arg(report.bytesRead);
	lines << QCoreApplication::translate("VibeStudioPackageArchive", "Output paths");
	for (const PackageExtractionEntryResult& result : report.entries) {
		QString state = QCoreApplication::translate("VibeStudioPackageArchive", "planned");
		if (!result.error.isEmpty()) {
			state = QCoreApplication::translate("VibeStudioPackageArchive", "failed");
		} else if (result.skipped) {
			state = QCoreApplication::translate("VibeStudioPackageArchive", "skipped");
		} else if (result.dryRun) {
			state = result.kind == PackageEntryKind::Directory ? QCoreApplication::translate("VibeStudioPackageArchive", "would create") : QCoreApplication::translate("VibeStudioPackageArchive", "would write");
		} else if (result.kind == PackageEntryKind::Directory && result.written) {
			state = QCoreApplication::translate("VibeStudioPackageArchive", "created");
		} else if (result.written) {
			state = QCoreApplication::translate("VibeStudioPackageArchive", "wrote");
		}
		lines << QStringLiteral("- %1: %2 -> %3")
			.arg(state,
				result.virtualPath.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "(package)") : result.virtualPath,
				result.outputPath.isEmpty() ? QCoreApplication::translate("VibeStudioPackageArchive", "(no output path)") : QDir::toNativeSeparators(result.outputPath));
		if (result.entryIndex >= 0) {
			lines << QCoreApplication::translate("VibeStudioPackageArchive", "  Entry index: %1; source ordinal: %2").arg(result.entryIndex).arg(result.sourceOrdinal);
		}
		if (!result.message.isEmpty()) {
			lines << QStringLiteral("  %1").arg(result.message);
		}
		if (!result.error.isEmpty()) {
			lines << QCoreApplication::translate("VibeStudioPackageArchive", "  Error: %1").arg(result.error);
		}
	}
	if (!report.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioPackageArchive", "Warnings");
		for (const QString& warning : report.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
