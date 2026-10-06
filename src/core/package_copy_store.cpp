#include "core/package_copy_store.h"
#include "core/package_draft_access.h"
#include "core/package_storage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThreadPool>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <filesystem>
#include <limits>

namespace vibestudio {
namespace {
constexpr qint64 metadataLimit = 65536;
constexpr qint64 pathBytesLimit = 32 * 1024 * 1024;
const QString suffix = QStringLiteral(".copies");
QString text(const char* value) { return QCoreApplication::translate("PackageCopyStore", value); }
bool fail(QString* error, const QString& value) { if (error) { *error = value; } return false; }
bool stopped(const PackageReadControl& control) { return control.isCancelled && control.isCancelled(); }
bool validId(const QString& value) { const QUuid id(value); return !id.isNull() && id.toString(QUuid::WithoutBraces) == value; }
qint64 created(const QFileInfo& info) { return info.birthTime().isValid() ? info.birthTime().toMSecsSinceEpoch() : 0; }
QString sessionPath(const QString& root, const QString& id) { return QDir(root).filePath(id + suffix); }
QString busyMessage() { return text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copies are in use or cannot be protected for maintenance. Close their studio window and retry.")); }
QThreadPool& cleanupPool()
{
	struct Pool : QThreadPool { Pool() { setMaxThreadCount(1); } };
	static Pool pool;
	return pool;
}
struct Directory {
	QString relative, canonical;
	qint64 modified = 0, birth = 0;
};
Directory directoryState(const QString& root, const QString& path)
{
	const QFileInfo info(path);
	return {path == root ? QString() : QDir(root).relativeFilePath(path), info.canonicalFilePath(),
		info.lastModified().toMSecsSinceEpoch(), created(info)};
}
bool directoryUnchanged(const QString& root, const Directory& value, bool membership)
{
	const QString path = value.relative.isEmpty() ? root : QDir(root).filePath(value.relative);
	const QFileInfo info(path);
	return safePackageStoragePath(path) && info.isDir() && info.canonicalFilePath() == value.canonical
		&& created(info) == value.birth && (!membership || info.lastModified().toMSecsSinceEpoch() == value.modified);
}
QStringList sessionDirectories(const QString& root, QString* error, const PackageReadControl& control);
const QString controlName = QStringLiteral(".coordination");
const QString policyName = QStringLiteral("limits.json");
bool validLimits(const PackageCopyLimits& limits)
{
	return limits.maximumBytes > 0 && limits.maximumBytes <= quint64(PackageCopyMaximumMiB) * 1024 * 1024
		&& limits.maximumFiles > 0 && limits.maximumFiles <= PackageCopyMaximumFiles
		&& limits.maximumEntries > 0 && limits.maximumEntries <= PackageCopyStorePayloadEntryLimit
		&& limits.maximumBatches > 0 && limits.maximumBatches <= PackageCopyMaximumBatches;
}
bool integer(const QJsonObject& object, const QString& name, qint64 maximum, qint64* output)
{
	const auto value = object.value(name); const auto number = value.toInteger(-1);
	if (!value.isDouble() || number < 0 || number > maximum || value.toDouble() != static_cast<double>(number)) { return false; }
	*output = number; return true;
}
QByteArray usageFingerprint(const QJsonObject& record, const QJsonObject& usage)
{
	return QCryptographicHash::hash(QJsonDocument(QJsonObject{{QStringLiteral("id"), record.value(QStringLiteral("id"))},
		{QStringLiteral("createdUtc"), record.value(QStringLiteral("createdUtc"))}, {QStringLiteral("reservations"), usage}}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}
void storeUsage(QJsonObject& record, const PackageCopyUsage& usage)
{
	const auto object = packageCopyUsageJson(usage); record.insert(QStringLiteral("reservations"), object);
	record.insert(QStringLiteral("reservationSha256"), QString::fromLatin1(usageFingerprint(record, object).toHex()));
}
bool readUsage(const QJsonObject& record, PackageCopyUsage* usage)
{
	if (record.value(QStringLiteral("schemaVersion")).toInt(-1) != 2) { return false; }
	const auto object = record.value(QStringLiteral("reservations")).toObject(); qint64 bytes, files, entries, batches, pending, failed;
	if (record.value(QStringLiteral("reservationSha256")).toString().toLatin1() != usageFingerprint(record, object).toHex()
		|| !integer(object, QStringLiteral("bytes"), qint64(PackageCopyMaximumMiB) * 1024 * 1024, &bytes)
		|| !integer(object, QStringLiteral("files"), PackageCopyMaximumFiles, &files)
		|| !integer(object, QStringLiteral("entries"), PackageCopyMaximumEntries, &entries)
		|| !integer(object, QStringLiteral("batches"), PackageCopyMaximumBatches, &batches)
		|| !integer(object, QStringLiteral("pendingBatches"), batches, &pending)
		|| !integer(object, QStringLiteral("cleanupFailedBatches"), batches, &failed)
		|| entries < files || pending + failed > batches || (!batches && (bytes || files || entries))) { return false; }
	*usage = {static_cast<quint64>(bytes), static_cast<qsizetype>(files), static_cast<qsizetype>(entries),
		static_cast<int>(batches), static_cast<int>(pending), static_cast<int>(failed)};
	return true;
}
bool readMetadata(const QString& path, QByteArray* bytes, QString* error)
{
	const QFileInfo info(path);
	if (!safePackageStoragePath(path, error) || !info.isFile() || info.size() < 0 || info.size() > metadataLimit) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy reservation metadata is missing, unsafe or oversized: %1")).arg(path));
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return fail(error, file.errorString()); }
	*bytes = file.read(metadataLimit + 1);
	if (bytes->size() != info.size() || file.error() != QFileDevice::NoError) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy reservation metadata changed while being read.")));
	}
	return true;
}
bool readRecord(const QString& path, QJsonObject* record, QByteArray* bytes, PackageCopyUsage* usage, QString* error)
{
	if (!readMetadata(QDir(path).filePath(QStringLiteral("session.json")), bytes, error)) { return false; }
	*record = QJsonDocument::fromJson(*bytes).object();
	const QString id = QFileInfo(path).fileName().chopped(suffix.size());
	if (!validId(id) || record->value(QStringLiteral("id")).toString() != id
		|| record->value(QStringLiteral("kind")).toString() != QStringLiteral("vibestudio-package-copies")
		|| !QDateTime::fromString(record->value(QStringLiteral("createdUtc")).toString(), Qt::ISODateWithMs).isValid()
		|| !readUsage(*record, usage)) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "A copy session has no valid shared reservation record. Close older studio windows or review retained copies before preparing more: %1")).arg(path));
	}
	return true;
}
bool writeMetadata(const QString& path, const QByteArray& expected, bool existed, const QJsonObject& object, QString* error)
{
	if (!safePackageStoragePath(path, error)) { return false; }
	QByteArray current;
	if (existed ? !readMetadata(path, &current, error) || current != expected : QFileInfo::exists(path)) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy reservation metadata changed before it could be saved.")));
	}
	const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
	QSaveFile file(path); file.setDirectWriteFallback(false);
	if (bytes.size() > metadataLimit || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy reservation metadata could not be committed: %1")).arg(file.errorString()));
	}
	return true;
}
std::shared_ptr<const PackageDraftAccess> storeControl(const QString& root, bool create, PackageDraftAccess::Mode mode,
	QString* error, const PackageReadControl& control)
{
	if (!safePackageStoragePath(root, error) || (create && !QDir().mkpath(root))) { return {}; }
	const QString path = QDir(root).filePath(controlName); QElapsedTimer timer; timer.start();
	while (!stopped(control) && timer.elapsed() < 2000) {
		if (!safePackageStoragePath(path, error)) { return {}; }
		if (!QFileInfo::exists(path)) {
			if (!create) { return PackageDraftAccess::acquire(root, mode, error); }
			// Windows maintenance handles exclude enumeration. Scan while shared,
			// then prove root membership unchanged after acquiring exclusion.
			auto read = PackageDraftAccess::acquire(root, PackageDraftAccess::Mode::Read);
			if (read) {
				const auto before = directoryState(root, root); QString scanError;
				sessionDirectories(root, &scanError, control);
				if (!scanError.isEmpty()) { fail(error, scanError); return {}; }
				read.reset(); auto admission = PackageDraftAccess::acquire(root, PackageDraftAccess::Mode::Maintain);
				if (admission && directoryUnchanged(root, before, true) && !QFileInfo::exists(path) && !QDir().mkdir(path)) {
					fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Unable to create shared copy coordination storage."))); return {};
				}
			}
		}
		if (QFileInfo::exists(path)) {
			if (auto lock = PackageDraftAccess::acquire(path, mode)) { return lock; }
		}
		QThread::msleep(10);
	}
	fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Shared copy storage is busy or the operation was cancelled. Retry after current storage operations finish.")));
	return {};
}
PackageCopyQuota readPolicy(const QString& directory)
{
	PackageCopyQuota result; result.directory = directory; QByteArray bytes;
	const QString path = QDir(directory).filePath(policyName);
	result.configured = QFileInfo::exists(path);
	if (result.configured) {
		if (!readMetadata(path, &bytes, &result.error)) { return result; }
		const auto object = QJsonDocument::fromJson(bytes).object(), limits = object.value(QStringLiteral("limits")).toObject();
		qint64 payload, files, entries, batches;
		if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != 1
			|| !integer(limits, QStringLiteral("maximumBytes"), qint64(PackageCopyMaximumMiB) * 1024 * 1024, &payload)
			|| !integer(limits, QStringLiteral("maximumFiles"), PackageCopyMaximumFiles, &files)
			|| !integer(limits, QStringLiteral("maximumEntries"), PackageCopyStorePayloadEntryLimit, &entries)
			|| !integer(limits, QStringLiteral("maximumBatches"), PackageCopyMaximumBatches, &batches)
			|| !validLimits({static_cast<quint64>(payload), static_cast<qsizetype>(files), static_cast<qsizetype>(entries), static_cast<int>(batches)})) {
			result.error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "The shared copy limit policy is invalid. Preserve it for review before changing the store.")); return result;
		}
		result.limits = {static_cast<quint64>(payload), static_cast<qsizetype>(files), static_cast<qsizetype>(entries), static_cast<int>(batches)};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData("vibestudio-copy-policy-v1:"); hash.addData(directory.toUtf8());
	hash.addData(result.configured ? ":configured:" : ":default:"); hash.addData(bytes);
	result.policyFingerprint = hash.result(); return result;
}
PackageCopyQuota readQuota(const QString& directory, const PackageReadControl& control)
{
	auto result = readPolicy(directory); if (!result.error.isEmpty()) { return result; }
	const auto paths = sessionDirectories(directory, &result.error, control);
	for (const auto& path : paths) {
		if (stopped(control)) { result.cancelled = true; break; }
		QJsonObject record; QByteArray bytes; PackageCopyUsage usage;
		if (!readRecord(path, &record, &bytes, &usage, &result.error)) { break; }
		result.reserved.bytes += usage.bytes; result.reserved.files += usage.files; result.reserved.entries += usage.entries;
		result.reserved.batches += usage.batches; result.reserved.pendingBatches += usage.pendingBatches;
		result.reserved.cleanupFailedBatches += usage.cleanupFailedBatches; ++result.sessions;
	}
	result.cancelled = result.cancelled || stopped(control); return result;
}
bool changeReservation(const QString& sessionPath, const PackageCopyUsage& usage, bool release, bool failed, QString* error)
{
	const QString root = QFileInfo(sessionPath).absolutePath();
	auto lock = storeControl(root, false, PackageDraftAccess::Mode::Maintain, error, {}); if (!lock) { return false; }
	QJsonObject record; QByteArray bytes; PackageCopyUsage current;
	if (!readRecord(sessionPath, &record, &bytes, &current, error) || current.bytes < usage.bytes
		|| current.files < usage.files || current.entries < usage.entries || current.batches < 1 || current.pendingBatches < 1) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Shared copy reservations could not be reconciled; the previous charge was retained.")));
	}
	--current.pendingBatches;
	if (release) { current.bytes -= usage.bytes; current.files -= usage.files; current.entries -= usage.entries; --current.batches; }
	else if (failed) { ++current.cleanupFailedBatches; }
	storeUsage(record, current);
	return writeMetadata(QDir(sessionPath).filePath(QStringLiteral("session.json")), bytes, true, record, error);
}

struct Snapshot {
	PackageCopySessionInfo info;
	QVector<PackageStorageFile> files;
	QVector<Directory> directories;
};
void hashField(QCryptographicHash& hash, const QByteArray& value)
{
	hash.addData(QByteArray::number(value.size())); hash.addData(":"); hash.addData(value);
}
bool snapshotUnchanged(const Snapshot& snapshot, const PackageReadControl& control)
{
	for (const auto& directory : snapshot.directories) {
		if (stopped(control) || !directoryUnchanged(snapshot.info.path, directory, true)) { return false; }
	}
	for (const auto& file : snapshot.files) {
		if (stopped(control) || !packageStorageFileUnchanged(snapshot.info.path, file)) { return false; }
		if (!file.metadataSha256.isEmpty()) {
			QFile input(QDir(snapshot.info.path).filePath(file.relativePath));
			if (!input.open(QIODevice::ReadOnly)) { return false; }
			const auto bytes = input.read(metadataLimit + 1);
			if (bytes.size() != file.bytes || input.error() != QFileDevice::NoError
				|| QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != file.metadataSha256) { return false; }
		}
	}
	return true;
}
Snapshot inspectSession(const QString& path, const PackageReadControl& control, int entryLimit = PackageCopyStoreEntryLimit)
{
	Snapshot result; auto& info = result.info; info.path = QFileInfo(path).absoluteFilePath();
	info.id = QFileInfo(path).fileName().chopped(suffix.size());
	const auto unsafe = [&](const QString& error) { info.storageError = error; info.fingerprint.clear(); return result; };
	if (!validId(info.id) || !safePackageStoragePath(path, &info.storageError) || !QFileInfo(path).isDir()) {
		return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy storage is unavailable or unsafe.")));
	}
	auto view = PackageDraftAccess::acquire(info.path, PackageDraftAccess::Mode::Read);
	if (!view) { return unsafe(busyMessage()); }
	result.directories << directoryState(info.path, info.path);
	QVector<QPair<QString, int>> pending{{info.path, 0}};
	qint64 pathBytes = 0; bool manifestPresent = false;
	while (!pending.isEmpty()) {
		const auto [directory, depth] = pending.takeLast();
		if (stopped(control)) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy review cancelled."))); }
		if (depth > PackageCopyStoreDepthLimit) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy storage exceeds its review depth limit."))); }
		QDirIterator children(directory, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
		while (children.hasNext()) {
			if (stopped(control)) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy review cancelled."))); }
			const auto entry = children.next(); const QFileInfo file(entry);
			const auto relative = QDir(info.path).relativeFilePath(entry);
			pathBytes += relative.toUtf8().size();
			if (++info.entries > entryLimit || pathBytes > pathBytesLimit) {
				return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy storage exceeds its bounded review limits.")));
			}
			if (!safePackageStoragePath(entry, &info.storageError) || (!file.isDir() && !file.isFile())) {
				return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy storage contains a link or an unsupported filesystem entry: %1")).arg(relative));
			}
			const bool metadata = depth == 0 && (relative == QStringLiteral("session.json")
				|| packageStorageTemporaryName(relative, QStringLiteral("session.json."))
				|| packageStorageTemporaryName(relative, QStringLiteral(".session.json.")));
			if (depth == 0 && !metadata && !(file.isDir() && packageStorageTemporaryName(relative, QStringLiteral("package-copy-")))) {
				return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy storage contains an unrecognized root entry: %1")).arg(relative));
			}
			if (file.isDir()) {
				if (metadata) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy metadata is not a regular file."))); }
				if (depth == 0) { ++info.batches; }
				result.directories << directoryState(info.path, entry); pending << qMakePair(entry, depth + 1); continue;
			}
			if (file.size() < 0 || file.size() > std::numeric_limits<qint64>::max() - info.bytes || (metadata && file.size() > metadataLimit)) {
				return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy storage exceeds its byte or metadata review limit.")));
			}
			PackageStorageFile member; member.relativePath = relative; member.bytes = file.size();
			member.modifiedMs = file.lastModified().toMSecsSinceEpoch(); member.createdMs = created(file);
			if (metadata) {
				QFile input(entry); if (!input.open(QIODevice::ReadOnly)) { return unsafe(input.errorString()); }
				const auto bytes = input.read(metadataLimit + 1);
				if (bytes.size() != member.bytes || input.error() != QFileDevice::NoError) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy metadata changed during review."))); }
				member.metadataSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
				if (relative == QStringLiteral("session.json")) {
					manifestPresent = true; const auto object = QJsonDocument::fromJson(bytes).object();
					info.createdUtc = QDateTime::fromString(object.value(QStringLiteral("createdUtc")).toString(), Qt::ISODateWithMs);
					const int schema = object.value(QStringLiteral("schemaVersion")).toInt(-1);
					info.reservationKnown = readUsage(object, &info.reserved);
					if ((schema != 1 && (schema != 2 || !info.reservationKnown)) || object.value(QStringLiteral("kind")).toString() != QStringLiteral("vibestudio-package-copies")
						|| object.value(QStringLiteral("id")).toString() != info.id || !info.createdUtc.isValid()) {
						info.error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "The session record is incomplete or invalid. Review the remaining files before discarding them."));
					}
				}
			} else { info.bytes += member.bytes; ++info.files; }
			result.files << member;
			if (control.progress) { control.progress(info.path, info.entries, 0); }
		}
	}
	if (!manifestPresent) { info.error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "The session record is missing; creation may have been interrupted.")); }
	std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
	std::sort(result.directories.begin(), result.directories.end(), [](const auto& a, const auto& b) { return a.relative < b.relative; });
	if (stopped(control) || !view->matchesDirectory() || !snapshotUnchanged(result, control)) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copies changed during review. Refresh the list."))); }
	QCryptographicHash hash(QCryptographicHash::Sha256); hashField(hash, QByteArrayLiteral("vibestudio-copy-review-v1")); hashField(hash, info.path.toUtf8());
	for (const auto& directory : result.directories) {
		if (stopped(control)) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy review cancelled."))); }
		for (const auto& field : {directory.relative.toUtf8(), directory.canonical.toUtf8(), QByteArray::number(directory.birth), QByteArray::number(directory.modified)}) { hashField(hash, field); }
	}
	for (const auto& file : result.files) {
		if (stopped(control)) { return unsafe(text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy review cancelled."))); }
		for (const auto& field : {file.relativePath.toUtf8(), QByteArray::number(file.bytes), QByteArray::number(file.modifiedMs), QByteArray::number(file.createdMs), file.metadataSha256}) { hashField(hash, field); }
	}
	info.fingerprint = hash.result(); return result;
}
bool removeSnapshot(const Snapshot& snapshot, const PackageDraftAccess& access, QString* error, const PackageReadControl& control)
{
	if (!access.matchesDirectory() || !snapshotUnchanged(snapshot, control)) { return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copies changed. Refresh before discarding them."))); }
	const auto& path = snapshot.info.path;
	qint64 removed = 0;
	const qint64 total = snapshot.files.size() + snapshot.directories.size();
	const auto remove = [&](const PackageStorageFile& file) {
		if (stopped(control)) { return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy cleanup cancelled. Refresh to review the remaining files."))); }
		if (!access.matchesDirectory() || !packageStorageFileUnchanged(path, file)
			|| !QFile::remove(QDir(path).filePath(file.relativePath))) {
			return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "A temporary copy changed or could not be removed. Remaining files were retained.")));
		}
		if (control.progress) { control.progress(path, ++removed, total); }
		return true;
	};
	for (const auto& file : snapshot.files) { if (file.relativePath != QStringLiteral("session.json") && !remove(file)) { return false; } }
	// Descendants sort after their ancestors. No recursive filesystem deletion.
	for (auto at = snapshot.directories.crbegin(); at != snapshot.directories.crend(); ++at) {
		if (at->relative.isEmpty()) { continue; }
		if (stopped(control) || !access.matchesDirectory() || !directoryUnchanged(path, *at, false)
			|| !QDir().rmdir(QDir(path).filePath(at->relative))) {
			return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy directories changed or cleanup was interrupted. Refresh to review the remaining files.")));
		}
		if (control.progress) { control.progress(path, ++removed, total); }
	}
	for (const auto& file : snapshot.files) { if (file.relativePath == QStringLiteral("session.json") && !remove(file)) { return false; } }
	if (stopped(control) || !access.matchesDirectory() || !QDir().rmdir(path)) { return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "The temporary copy session directory could not be removed."))); }
	if (control.progress) { control.progress(path, ++removed, total); }
	return true;
}
QStringList sessionDirectories(const QString& root, QString* error, const PackageReadControl& control)
{
	QStringList result;
	if (!safePackageStoragePath(root, error)) { return {}; }
	if (!QFileInfo::exists(root)) { return {}; }
	if (!QFileInfo(root).isDir()) { fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "The temporary copy store is not a directory."))); return {}; }
	QDirIterator entries(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
	int visited = 0;
	while (entries.hasNext()) {
		const auto path = entries.next(); const QFileInfo info(path);
		if (stopped(control)) { fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy review cancelled."))); return {}; }
		if (++visited > PackageCopyStoreSessionLimit + 16 || !safePackageStoragePath(path, error)) {
			fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Shared copy storage contains unsafe or excessive metadata."))); return {};
		}
		if (info.fileName() == controlName && info.isDir()) { continue; }
		if ((info.fileName() == policyName || packageStorageTemporaryName(info.fileName(), QStringLiteral("limits.json."))
			|| packageStorageTemporaryName(info.fileName(), QStringLiteral(".limits.json."))) && info.isFile() && info.size() <= metadataLimit) { continue; }
		if (result.size() >= PackageCopyStoreSessionLimit || !info.isDir() || !info.fileName().endsWith(suffix)
			|| !validId(info.fileName().chopped(suffix.size())) || !safePackageStoragePath(path, error)) {
			fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "The copy store contains an unrecognized path or exceeds its session review limit."))); return {};
		}
		result << path;
	}
	result.sort(); return result;
}
} // namespace

struct PackageCopySession::State {
	QString path;
	std::shared_ptr<const PackageDraftAccess> owner;
};
PackageCopySession::PackageCopySession(std::unique_ptr<State> state) : m_state(std::move(state)) {}
PackageCopySession::~PackageCopySession()
{
	cleanupPool().start([path = m_state->path, owner = std::move(m_state->owner)] {
		QString error;
		if (!owner || !owner->matchesDirectory()) { qWarning().noquote() << text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copy ownership changed; files were retained: %1")).arg(path); return; }
		auto quota = storeControl(QFileInfo(path).absolutePath(), false, PackageDraftAccess::Mode::Maintain, &error, {});
		if (!quota) { qWarning().noquote() << error; return; }
		const auto snapshot = inspectSession(path, {});
		if (!snapshot.info.reviewable() || !removeSnapshot(snapshot, *owner, &error, {})) {
			qWarning().noquote() << text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copies were retained for review: %1. %2")).arg(path, snapshot.info.storageError.isEmpty() ? error : snapshot.info.storageError);
		}
	});
}
QString PackageCopySession::path() const { return m_state->path; }
bool PackageCopySession::isValid() const { return m_state->owner && m_state->owner->matchesDirectory(); }

std::shared_ptr<PackageCopySession> PackageCopySession::create(const QString& directory, QString* error, const PackageReadControl& control)
{
	QString local; if (!error) { error = &local; } error->clear();
	if (directory.isEmpty() || stopped(control) || !safePackageStoragePath(directory, error)) { if (error->isEmpty()) { *error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy session creation cancelled or no safe storage is available.")); } return {}; }
	const QString root = QFileInfo(directory).absoluteFilePath();
	if (!QDir().mkpath(root)) { fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Unable to create temporary copy storage."))); return {}; }
	auto quota = storeControl(root, true, PackageDraftAccess::Mode::Maintain, error, control); if (!quota) { return {}; }
	auto read = PackageDraftAccess::acquire(root, PackageDraftAccess::Mode::Read, error); if (!read) { return {}; }
	const auto before = directoryState(root, root); const auto sessions = sessionDirectories(root, error, control);
	if (!error->isEmpty() || sessions.size() >= PackageCopyStoreSessionLimit) { if (error->isEmpty()) { *error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "The copy store is full. Review unused sessions before preparing more copies.")); } return {}; }
	read.reset(); auto store = PackageDraftAccess::acquire(root, PackageDraftAccess::Mode::Maintain, error);
	if (!store || !directoryUnchanged(root, before, true)) { fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy storage changed or is busy. Retry the operation."))); return {}; }
	const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), path = sessionPath(root, id);
	if (stopped(control) || !QDir().mkdir(path)) { fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Unable to create the copy session."))); return {}; }
	auto owner = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Read, error);
	if (!owner || !owner->protectsReaders()) { owner.reset(); QDir().rmdir(path); fail(error, busyMessage()); return {}; }
	if (control.progress) { control.progress(path, 0, 0); }
	QJsonObject record{{QStringLiteral("schemaVersion"), 2}, {QStringLiteral("kind"), QStringLiteral("vibestudio-package-copies")},
		{QStringLiteral("id"), id}, {QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
	storeUsage(record, {});
	const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Compact);
	QSaveFile file(QDir(path).filePath(QStringLiteral("session.json"))); file.setDirectWriteFallback(false);
	if (stopped(control) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		file.cancelWriting(); owner.reset(); QDir().rmdir(path);
		fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "The copy session record could not be committed. Any remaining files are available for review."))); return {};
	}
	auto state = std::make_unique<State>(); state->path = path; state->owner = std::move(owner);
	return std::shared_ptr<PackageCopySession>(new PackageCopySession(std::move(state)));
}

PackageCopyQuota inspectPackageCopyQuota(const QString& directory, const PackageReadControl& control)
{
	PackageCopyQuota result; result.directory = directory;
	if (stopped(control)) { result.cancelled = true; return result; }
	if (directory.isEmpty() || !safePackageStoragePath(directory, &result.error)) {
		if (result.error.isEmpty()) { result.error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "Choose a temporary copy store.")); } return result;
	}
	const QString root = QFileInfo(directory).absoluteFilePath();
	if (!QFileInfo::exists(root)) { return readPolicy(root); }
	auto lock = storeControl(root, false, PackageDraftAccess::Mode::Read, &result.error, control);
	if (!lock) { result.cancelled = stopped(control); return result; }
	return readQuota(root, control);
}
QJsonObject packageCopyQuotaJson(const PackageCopyQuota& quota)
{
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("scope"), QStringLiteral("managed-copy-store")},
		{QStringLiteral("directory"), quota.directory}, {QStringLiteral("limits"), packageCopyLimitsJson(quota.limits)},
		{QStringLiteral("reserved"), packageCopyUsageJson(quota.reserved)}, {QStringLiteral("accountedSessions"), quota.sessions},
		{QStringLiteral("complete"), quota.complete()}, {QStringLiteral("configured"), quota.configured},
		{QStringLiteral("policyValid"), quota.policyFingerprint.size() == 32},
		{QStringLiteral("cancelled"), quota.cancelled}, {QStringLiteral("error"), quota.error},
		{QStringLiteral("policySha256"), QString::fromLatin1(quota.policyFingerprint.toHex())}};
}
bool configurePackageCopyQuota(const QString& directory, const PackageCopyLimits& limits,
	const QByteArray& expectedPolicyFingerprint, bool dryRun, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (directory.isEmpty() || !validLimits(limits) || expectedPolicyFingerprint.size() != 32 || stopped(control)
		|| !safePackageStoragePath(directory, error)) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Choose valid shared copy limits and their current policy review checksum.")));
	}
	const QString root = QFileInfo(directory).absoluteFilePath();
	std::shared_ptr<const PackageDraftAccess> lock;
	if (!dryRun || QFileInfo::exists(root)) {
		lock = storeControl(root, !dryRun, dryRun ? PackageDraftAccess::Mode::Read : PackageDraftAccess::Mode::Maintain, error, control);
		if (!lock) { return false; }
	}
	const auto policy = readPolicy(root);
	if (!policy.error.isEmpty() || policy.policyFingerprint != expectedPolicyFingerprint || stopped(control)) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Shared copy limits changed or could not be reviewed. Refresh before applying them.")));
	}
	if (dryRun) { return true; }
	QByteArray expected; const QString path = QDir(root).filePath(policyName);
	if (policy.configured && !readMetadata(path, &expected, error)) { return false; }
	return writeMetadata(path, expected, policy.configured,
		{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("limits"), packageCopyLimitsJson(limits)}}, error);
}
std::unique_ptr<PackageCopyStoreReservation> PackageCopySession::reserve(quint64 bytes, qsizetype files, qsizetype entries,
	QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	const QString root = QFileInfo(path()).absolutePath();
	if (!isValid() || files < 0 || entries < files || stopped(control)) {
		fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "The shared copy reservation is invalid or cancelled."))); return {};
	}
	auto lock = storeControl(root, false, PackageDraftAccess::Mode::Maintain, error, control); if (!lock) { return {}; }
	const auto quota = readQuota(root, control);
	if (!quota.complete()) { fail(error, quota.error.isEmpty() ? text(QT_TRANSLATE_NOOP("PackageCopyStore", "Shared copy admission cancelled.")) : quota.error); return {}; }
	const auto& used = quota.reserved; const auto& limits = quota.limits;
	if (used.bytes > limits.maximumBytes || bytes > limits.maximumBytes - used.bytes
		|| used.files > limits.maximumFiles || files > limits.maximumFiles - used.files
		|| used.entries > limits.maximumEntries || entries > limits.maximumEntries - used.entries || used.batches >= limits.maximumBatches) {
		fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "The shared temporary copy store has reached a reservation limit. Review unused sessions, adjust shared limits or use Extract Selected."))); return {};
	}
	QJsonObject record; QByteArray previous; PackageCopyUsage usage;
	if (!isValid() || !readRecord(path(), &record, &previous, &usage, error) || stopped(control)) { return {}; }
	usage.bytes += bytes; usage.files += files; usage.entries += entries; ++usage.batches; ++usage.pendingBatches;
	storeUsage(record, usage);
	if (!writeMetadata(QDir(path()).filePath(QStringLiteral("session.json")), previous, true, record, error)) { return {}; }
	PackageCopyUsage requested; requested.bytes = bytes; requested.files = files; requested.entries = entries;
	return std::unique_ptr<PackageCopyStoreReservation>(new PackageCopyStoreReservation(shared_from_this(), requested));
}
PackageCopyStoreReservation::PackageCopyStoreReservation(std::shared_ptr<PackageCopySession> session, PackageCopyUsage usage)
	: m_session(std::move(session)), m_usage(usage) {}
PackageCopyStoreReservation::~PackageCopyStoreReservation()
{
	if (!m_pending) { return; }
	QString error;
	if (!m_session->isValid() || !changeReservation(m_session->path(), m_usage, true, false, &error)) {
		qWarning().noquote() << text(QT_TRANSLATE_NOOP("PackageCopyStore", "Shared copy reservation retained for review: %1. %2")).arg(m_session->path(), error);
	}
}
void PackageCopyStoreReservation::retain(bool cleanupFailed)
{
	if (!m_pending) { return; }
	m_pending = false; QString error;
	if (!m_session->isValid() || !changeReservation(m_session->path(), m_usage, false, cleanupFailed, &error)) {
		qWarning().noquote() << text(QT_TRANSLATE_NOOP("PackageCopyStore", "Shared copy completion could not be recorded; its pending reservation remains charged: %1. %2")).arg(m_session->path(), error);
	}
}

QString packageCopyDirectory()
{
	std::error_code error; const auto path = std::filesystem::canonical(std::filesystem::path(QDir::tempPath().toStdU16String()), error);
	return error ? QString() : QDir(QString::fromStdU16String(path.generic_u16string())).filePath(QStringLiteral("vibestudio-package-copies"));
}
void waitForPackageCopyCleanup() { cleanupPool().waitForDone(); }

PackageCopyInventory listPackageCopies(const QString& directory, const PackageReadControl& control)
{
	PackageCopyInventory result; result.directory = directory; result.quota = inspectPackageCopyQuota(directory, control);
	if (stopped(control)) { result.cancelled = true; return result; }
	if (directory.isEmpty() || !safePackageStoragePath(directory, &result.error)) { if (result.error.isEmpty()) { result.error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "Choose a temporary copy store.")); } return result; }
	if (!QFileInfo::exists(directory)) { return result; }
	auto store = PackageDraftAccess::acquire(directory, PackageDraftAccess::Mode::Read, &result.error); if (!store) { return result; }
	const auto paths = sessionDirectories(directory, &result.error, control);
	for (const auto& path : paths) {
		if (stopped(control)) { result.cancelled = true; break; }
		auto info = inspectSession(path, control, PackageCopyStoreEntryLimit - result.entries).info;
		if (info.reviewable()) {
			auto access = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain);
			info.discardAvailable = access != nullptr;
			if (!access) { info.leaseError = busyMessage(); }
		}
		if (!info.reviewable() || info.bytes > std::numeric_limits<qint64>::max() - result.bytes
			|| info.entries > PackageCopyStoreEntryLimit - result.entries) {
			result.error = text(QT_TRANSLATE_NOOP("PackageCopyStore", "Copy storage review is incomplete or exceeds its aggregate entry limit."));
			result.sessions << info; break;
		}
		result.bytes += info.bytes; result.files += info.files; result.entries += info.entries; result.sessions << info;
	}
	result.cancelled = result.cancelled || stopped(control); return result;
}

bool discardPackageCopies(const QString& directory, const QString& id, const QByteArray& expectedFingerprint,
	bool dryRun, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (directory.isEmpty() || !validId(id) || expectedFingerprint.size() != 32 || stopped(control)) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Choose a copy session and its current storage review checksum.")));
	}
	auto quota = storeControl(QFileInfo(directory).absoluteFilePath(), false, PackageDraftAccess::Mode::Maintain, error, control); if (!quota) { return false; }
	const auto snapshot = inspectSession(sessionPath(QFileInfo(directory).absoluteFilePath(), id), control);
	if (!snapshot.info.reviewable() || snapshot.info.fingerprint != expectedFingerprint) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copies changed or cannot be reviewed. Refresh before discarding them.")));
	}
	auto access = PackageDraftAccess::acquire(snapshot.info.path, PackageDraftAccess::Mode::Maintain);
	if (!access) { return fail(error, busyMessage()); }
	if (!access->matchesDirectory() || !snapshotUnchanged(snapshot, control) || stopped(control)) {
		return fail(error, text(QT_TRANSLATE_NOOP("PackageCopyStore", "Temporary copies changed or the review was cancelled.")));
	}
	return dryRun || removeSnapshot(snapshot, *access, error, control);
}

QJsonObject packageCopyInventoryJson(const PackageCopyInventory& inventory)
{
	QJsonArray sessions;
	for (const auto& info : inventory.sessions) {
		sessions << QJsonObject{{QStringLiteral("id"), info.id}, {QStringLiteral("path"), info.path},
			{QStringLiteral("createdUtc"), info.createdUtc.toString(Qt::ISODateWithMs)}, {QStringLiteral("payloadBytes"), info.bytes},
			{QStringLiteral("files"), info.files}, {QStringLiteral("entries"), info.entries}, {QStringLiteral("batches"), info.batches},
			{QStringLiteral("reviewable"), info.reviewable()}, {QStringLiteral("discardAvailable"), info.discardAvailable},
			{QStringLiteral("reservationKnown"), info.reservationKnown}, {QStringLiteral("reserved"), packageCopyUsageJson(info.reserved)},
			{QStringLiteral("storageSha256"), QString::fromLatin1(info.fingerprint.toHex())},
			{QStringLiteral("error"), info.error}, {QStringLiteral("storageError"), info.storageError}, {QStringLiteral("leaseError"), info.leaseError}};
	}
	return {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("directory"), inventory.directory}, {QStringLiteral("sessions"), sessions},
		{QStringLiteral("quota"), packageCopyQuotaJson(inventory.quota)},
		{QStringLiteral("payloadBytes"), inventory.bytes}, {QStringLiteral("files"), inventory.files}, {QStringLiteral("entries"), inventory.entries},
		{QStringLiteral("complete"), inventory.complete()}, {QStringLiteral("cancelled"), inventory.cancelled}, {QStringLiteral("error"), inventory.error}};
}
} // namespace vibestudio
