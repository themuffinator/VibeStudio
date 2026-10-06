#include "core/package_recovery.h"
#include "core/package_protection_p.h"
#include "core/package_draft_access.h"
#include "core/package_draft.h"
#include "core/package_storage.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace vibestudio {
namespace {

constexpr qint64 manifestLimit = 32 * 1024 * 1024;
const QString suffix = QStringLiteral(".vibepackage");

QString message(const char* text) { return QCoreApplication::translate("PackageRecovery", text); }
bool fail(QString* error, const QString& text) { if (error) { *error = text; } return false; }
bool cancelled(const PackageReadControl& control) { return control.isCancelled && control.isCancelled(); }
bool validId(const QString& id) { const QUuid uuid(id); return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id; }
bool validHash(const QString& name) { return name.size() == 64 && QByteArray::fromHex(name.toLatin1()).toHex() == name.toLatin1(); }
bool safePath(const QString& path, QString* error) { return safePackageStoragePath(path, error); }

std::unique_ptr<QLockFile> lockStore(const QString& directory, QString* error)
{
	const QString path = QDir(directory).filePath(QStringLiteral(".store.lock"));
	if (!safePath(path, error)) { return {}; }
	auto lock = std::make_unique<QLockFile>(path); lock->setStaleLockTime(0);
	if (!lock->tryLock()) { fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Another process is updating package recovery storage. Try again when it finishes."))); return {}; }
	return lock;
}

QString sessionPath(const QString& directory, const QString& id) { return QDir(directory).absoluteFilePath(id + QStringLiteral(".session.lock")); }

bool containsPath(const QString& directory, const QString& path)
{
	const QString root = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
	const QString candidate = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	return root.compare(candidate, Qt::CaseInsensitive) == 0 || candidate.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive);
}

QByteArray readManifest(const QString& path, QString* error, const PackageReadControl& control)
{
	const QString manifest = QDir(path).filePath(QStringLiteral("document.json"));
	if (cancelled(control)) { fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Package recovery cancelled."))); return {}; }
	if (!safePath(manifest, error)) { return {}; }
	const QFileInfo info(manifest);
	QFile file(manifest);
	if (!info.isFile() || info.size() > manifestLimit || !file.open(QIODevice::ReadOnly)) {
		fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery manifest is unreadable or exceeds 32 MiB."))); return {};
	}
	const QByteArray bytes = file.read(manifestLimit + 1);
	if (file.error() != QFileDevice::NoError || bytes.size() > manifestLimit || cancelled(control)) {
		fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery manifest could not be read completely, or reading was cancelled."))); return {};
	}
	return bytes;
}

bool removeRegularFile(const QString& path, QString* error)
{
	if (!safePath(path, error) || !QFileInfo(path).isFile() || !QFile::remove(path)) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Unable to remove a recovery file: %1")).arg(path));
	}
	return true;
}

// The store lock serializes quota accounting and maintenance across sessions.
// Preflight the complete two-level layout, then remove only reviewed files.
bool tidyCopy(const QString& path, const QByteArray& expected, bool discard, bool dryRun, QString* error,
	const QByteArray& expectedStorage = {}, const PackageReadControl& control = {})
{
	if (!safePath(path, error)) { return false; }
	if (!QFileInfo::exists(path)) { return true; }
	std::shared_ptr<const PackageDraftAccess> access;
	if (dryRun) {
		access = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Read, error);
		if (!access) { return false; }
	}
	const QString lockPath = QDir(path).filePath(QStringLiteral(".write.lock"));
	if (!safePath(lockPath, error)) { return false; }
	QLockFile lock(lockPath); lock.setStaleLockTime(0);
	if (dryRun ? QFileInfo::exists(lockPath) : !lock.tryLock()) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "This package recovery copy is being read or written. Try again when that operation finishes."))); }
	const auto storage = inspectPackageStorage(path, control);
	if (!storage.safe()) { return fail(error, storage.error); }
	if (!expectedStorage.isEmpty() ? storage.fingerprint != expectedStorage
		: expected.isEmpty() ? storage.manifestPresent : storage.manifestSha256 != expected) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery copy changed. Refresh the list before continuing.")));
	}
	QSet<QString> referenced;
	if (!discard && storage.manifestPresent) {
		QString readError; const QByteArray bytes = readManifest(path, &readError, control);
		if (!readError.isEmpty()) { return fail(error, readError); }
		QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse).object();
		if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != expected || parse.error != QJsonParseError::NoError
			|| !doc.value(QStringLiteral("base")).isArray() || !doc.value(QStringLiteral("operations")).isArray()) {
			return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery manifest cannot be compacted safely.")));
		}
		for (const char* key : {"base", "operations"}) {
			for (const auto value : doc.value(QLatin1String(key)).toArray()) {
				const QString object = value.toObject().value(QStringLiteral("object")).toString();
				if (!object.isEmpty()) { if (!validHash(object)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Invalid recovery object identity."))); } referenced.insert(object); }
			}
		}
	}
	// Revalidate membership as well as individual file stats before any removal.
	const auto checked = inspectPackageStorage(path, control);
	if (!checked.safe() || checked.fingerprint != storage.fingerprint) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery copy changed. Refresh the list before continuing."))); }
	if (dryRun) { return true; }
	access = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, error);
	if (!access) { return false; }
	if (!packageStorageLayoutUnchanged(checked)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery copy changed. Refresh the list before continuing."))); }
	for (const auto& file : storage.files) {
		if (cancelled(control)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Package recovery cancelled."))); }
		if (file.relativePath == QStringLiteral("document.json") || referenced.contains(QFileInfo(file.relativePath).fileName())) { continue; }
		if (!access->matchesDirectory() || !packageStorageFileUnchanged(path, file) || !removeRegularFile(QDir(path).filePath(file.relativePath), error)) {
			return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "A recovery file changed or could not be removed. Refresh the list before continuing.")));
		}
	}
	if (!discard) { return true; }
	const QString objectDirectory = QDir(path).filePath(QStringLiteral("objects"));
	if (QFileInfo::exists(objectDirectory) && (!safePath(objectDirectory, error) || !QDir().rmdir(objectDirectory))) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery object directory could not be removed.")));
	}
	for (const auto& file : storage.files) {
		if (file.relativePath == QStringLiteral("document.json") && (!packageStorageFileUnchanged(path, file)
			|| !removeRegularFile(QDir(path).filePath(file.relativePath), error))) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery manifest changed or could not be removed."))); }
	}
	lock.unlock(); access.reset();
	if (!safePath(path, error) || !QDir().rmdir(path)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery directory could not be removed."))); }
	return true;
}

} // namespace

QString packageRecoveryDirectory()
{
	const QString root = StudioSettings::overrideFilePath().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
		: QFileInfo(StudioSettings::overrideFilePath()).absolutePath();
	return QDir(root).absoluteFilePath(QStringLiteral("package-recovery"));
}

QString packageRecoveryPath(const QString& directory, const QString& id)
{
	return directory.trimmed().isEmpty() || !validId(id) ? QString() : QDir(directory).absoluteFilePath(id + suffix);
}

PackageRecoveryInfo inspectPackageRecovery(const QString& path, const PackageReadControl& control, int maximumStorageFiles)
{
	PackageRecoveryInfo info; info.path = QFileInfo(path).absoluteFilePath();
	const auto invalid = [&](const QString& error) { info.error = error; return info; };
	const QFileInfo directory(path);
	info.id = directory.fileName().chopped(suffix.size());
	if (!directory.fileName().endsWith(suffix) || !validId(info.id) || !directory.isDir() || !safePath(path, &info.error)) {
		return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "Invalid or unsafe package recovery directory.")));
	}
	info.sessionFilePresent = QFileInfo::exists(sessionPath(directory.absolutePath(), info.id));
	const auto storage = inspectPackageStorage(path, control, maximumStorageFiles);
	info.storageSha256 = storage.fingerprint; info.storageBytes = storage.bytes; info.temporaryBytes = storage.temporaryBytes;
	info.storageFiles = static_cast<int>(storage.files.size()); info.storageError = storage.error; info.manifestPresent = storage.manifestPresent;
	if (cancelled(control)) { return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "Package recovery cancelled."))); }
	if (storage.safe() && !storage.manifestPresent) { return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "Incomplete recovery copy: no committed manifest."))); }
	const QByteArray bytes = readManifest(path, &info.error, control);
	info.metadataBytes = bytes.size();
	if (!info.error.isEmpty()) { return info; }
	info.manifestSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse); const auto root = document.object();
	const auto recovery = root.value(QStringLiteral("recovery")).toObject();
	if (parse.error != QJsonParseError::NoError || !document.isObject() || root.value(QStringLiteral("type")) != QStringLiteral("vibestudio-package-draft")
		|| (root.value(QStringLiteral("version")).toInt() != 2 && root.value(QStringLiteral("version")).toInt() != 3 && root.value(QStringLiteral("version")).toInt() != 4) || recovery.value(QStringLiteral("type")) != QStringLiteral("vibestudio-package-recovery")
		|| recovery.value(QStringLiteral("version")).toInt() != 1 || recovery.value(QStringLiteral("id")) != info.id) {
		return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery copy has a damaged or unsupported manifest.")));
	}
	if (root.value(QStringLiteral("version")).toInt() >= 4 || root.contains(QStringLiteral("protectedInputs"))) {
		const auto value = root.value(QStringLiteral("protectedInputs"));
		if (!value.isArray() || value.toArray().size() > PackageInputProtectionSet::pathCeiling) {
			return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery details are invalid.")));
		}
		QString inputError; PackageInputProtectionSet inputs(PackageInputProtectionSet::pathCeiling, PackageInputProtectionSet::byteCeiling, &inputError, control);
		for (const auto& path : value.toArray()) {
			if (!path.isString() || !PackageInputProtectionSet::validStoredPath(path.toString())) {
				return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery details are invalid.")));
			}
			if (!inputs.add(path.toString(), false)) { return invalid(inputError); }
		}
		if (!inputs.finish()) { return invalid(inputError); }
	}
	info.title = recovery.value(QStringLiteral("title")).toString();
	info.sourcePath = root.value(QStringLiteral("source")).toString();
	info.originalDraftPath = recovery.value(QStringLiteral("originalDraftPath")).toString();
	info.writtenUtc = QDateTime::fromString(recovery.value(QStringLiteral("writtenUtc")).toString(), Qt::ISODateWithMs).toUTC();
	bool revisionOk = false; const QString revision = root.value(QStringLiteral("revision")).toString();
	info.revision = revision.toULongLong(&revisionOk);
	const QString format = root.value(QStringLiteral("format")).toString(); info.format = packageArchiveFormatFromId(format);
	if (!revisionOk || QString::number(info.revision) != revision || !info.writtenUtc.isValid() || info.title.size() > 255
		|| info.sourcePath.size() > 32768 || info.originalDraftPath.size() > 32768 || info.format == PackageArchiveFormat::Unknown
		|| packageArchiveFormatId(info.format) != format || !root.value(QStringLiteral("current")).isArray() || !root.value(QStringLiteral("history")).isArray()) {
		return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery details are invalid.")));
	}
	info.operationCount = root.value(QStringLiteral("current")).toArray().size();
	info.historyCount = root.value(QStringLiteral("history")).toArray().size();
	for (const auto value : root.value(QStringLiteral("base")).toArray()) {
		const auto entry = value.toObject();
		if (!entry.contains(QStringLiteral("unavailable"))) { continue; }
		const auto reason = entry.value(QStringLiteral("unavailable")).toString();
		if (root.value(QStringLiteral("version")).toInt() < 3 || entry.contains(QStringLiteral("object"))
			|| reason.isEmpty() || reason.size() > 4096 || !reason.isValidUtf16() || reason.contains(QChar(0))) {
			return invalid(message(QT_TRANSLATE_NOOP("PackageRecovery", "Invalid unavailable original-content metadata.")));
		}
		++info.unavailableBaseCount;
	}
	return info;
}

PackageRecoveryInventory listPackageRecoveries(const QString& directory, const PackageReadControl& control)
{
	PackageRecoveryInventory inventory;
	if (!safePath(directory, &inventory.error)) { inventory.storageComplete = false; return inventory; }
	if (!QFileInfo::exists(directory)) { return inventory; }
	if (!QFileInfo(directory).isDir()) { inventory.storageComplete = false; inventory.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery store is not a directory.")); return inventory; }
	const auto addBytes = [&](qint64 bytes) {
		if (bytes < 0 || inventory.storageBytes > std::numeric_limits<qint64>::max() - bytes) { inventory.storageComplete = false; }
		else { inventory.storageBytes += bytes; }
	};
	QDirIterator iterator(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	int scanned = 0;
	while (iterator.hasNext()) {
		if (cancelled(control)) { inventory.cancelled = true; break; }
		if (++scanned > 4096) { inventory.truncated = true; break; }
		const QString path = iterator.next(); const auto entry = iterator.fileInfo(); const QString name = entry.fileName();
		if (!name.endsWith(suffix) || !validId(name.chopped(suffix.size()))) {
			if (!safePath(path, nullptr) || !entry.isFile()) {
				inventory.storageComplete = false;
				if (inventory.error.isEmpty()) { inventory.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "Recovery storage contains an unrelated directory or unsafe path: %1")).arg(path); }
			} else if (name != QStringLiteral(".store.lock") && !(name.endsWith(QStringLiteral(".session.lock")) && validId(name.chopped(13)))) {
				addBytes(entry.size()); ++inventory.storageFiles;
			}
			continue;
		}
		if (inventory.records.size() >= PackageRecoveryScanLimit) { inventory.truncated = true; break; }
		const qint64 estimated = std::clamp<qint64>(QFileInfo(QDir(path).filePath(QStringLiteral("document.json"))).size(), 0, manifestLimit);
		if (inventory.metadataBytes + estimated > PackageRecoveryMetadataLimit) { inventory.truncated = true; break; }
		auto record = inspectPackageRecovery(path, control, std::max(0, PackageRecoveryStorageFileLimit - inventory.storageFiles));
		inventory.metadataBytes += record.metadataBytes; inventory.storageFiles += record.storageFiles;
		if (record.storageSha256.size() != 32) { inventory.storageComplete = false; } else { addBytes(record.storageBytes); }
		inventory.records.append(std::move(record));
	}
	inventory.cancelled = inventory.cancelled || cancelled(control);
	inventory.storageComplete = inventory.storageComplete && !inventory.cancelled && !inventory.truncated;
	std::sort(inventory.records.begin(), inventory.records.end(), [](const auto& a, const auto& b) {
		return a.writtenUtc != b.writtenUtc ? a.writtenUtc > b.writtenUtc : a.id < b.id;
	});
	return inventory;
}

QJsonObject packageRecoveryInventoryJson(const PackageRecoveryInventory& inventory)
{
	QJsonArray records;
	for (const auto& info : inventory.records) {
		records.append(QJsonObject{{QStringLiteral("id"), info.id}, {QStringLiteral("path"), info.path}, {QStringLiteral("title"), info.title},
			{QStringLiteral("sourcePath"), info.sourcePath}, {QStringLiteral("originalDraftPath"), info.originalDraftPath},
			{QStringLiteral("format"), packageArchiveFormatId(info.format)}, {QStringLiteral("writtenUtc"), info.writtenUtc.toString(Qt::ISODateWithMs)},
			{QStringLiteral("revision"), QString::number(info.revision)}, {QStringLiteral("metadataBytes"), info.metadataBytes},
			{QStringLiteral("operationCount"), info.operationCount}, {QStringLiteral("historyCount"), info.historyCount},
			{QStringLiteral("unavailableBaseCount"), info.unavailableBaseCount},
			{QStringLiteral("manifestSha256"), QString::fromLatin1(info.manifestSha256.toHex())}, {QStringLiteral("metadataValid"), info.readable()},
			{QStringLiteral("storageSha256"), QString::fromLatin1(info.storageSha256.toHex())}, {QStringLiteral("storageBytes"), info.storageBytes},
			{QStringLiteral("storageFiles"), info.storageFiles}, {QStringLiteral("temporaryBytes"), info.temporaryBytes},
			{QStringLiteral("manifestPresent"), info.manifestPresent}, {QStringLiteral("storageError"), info.storageError},
			{QStringLiteral("sessionFilePresent"), info.sessionFilePresent}, {QStringLiteral("error"), info.error}});
	}
	return {{QStringLiteral("records"), records}, {QStringLiteral("metadataBytes"), inventory.metadataBytes},
		{QStringLiteral("storageBytes"), inventory.storageBytes}, {QStringLiteral("storageFiles"), inventory.storageFiles},
		{QStringLiteral("storageComplete"), inventory.storageComplete},
		{QStringLiteral("truncated"), inventory.truncated}, {QStringLiteral("cancelled"), inventory.cancelled}, {QStringLiteral("error"), inventory.error}};
}

PackageRecoverySession::PackageRecoverySession(QString directory, QString id, std::unique_ptr<QLockFile> lease)
	: m_directory(std::move(directory)), m_id(std::move(id)), m_lease(std::move(lease)) {}
PackageRecoverySession::~PackageRecoverySession() = default;

std::unique_ptr<PackageRecoverySession> PackageRecoverySession::acquire(const QString& directory, const QString& id, QString* error)
{
	if (error) { error->clear(); }
	if (packageRecoveryPath(directory, id).isEmpty() || !safePath(directory, error) || !QDir().mkpath(directory) || !safePath(directory, error)) {
		fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The package recovery destination is unavailable or unsafe."))); return {};
	}
	const QString root = QFileInfo(directory).absoluteFilePath();
	const QString lockPath = sessionPath(root, id);
	if (!safePath(lockPath, error)) { return {}; }
	auto lease = std::make_unique<QLockFile>(lockPath); lease->setStaleLockTime(0);
	if (!lease->tryLock()) { fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "This package recovery copy belongs to an active editor session."))); return {}; }
	auto session = std::unique_ptr<PackageRecoverySession>(new PackageRecoverySession(root, id, std::move(lease)));
	const QString path = packageRecoveryPath(root, id);
	if (QFileInfo::exists(QDir(path).filePath(QStringLiteral("document.json")))) {
		const auto info = inspectPackageRecovery(path);
		if (info.manifestSha256.size() != 32) { fail(error, info.error); return {}; }
		session->m_manifestSha256 = info.manifestSha256;
	}
	return session;
}

PackageRecoveryWriteResult PackageRecoverySession::checkpoint(const PackageStagingModel& staging, const QString& title, const PackageReadControl& control, const PackageRecoveryLimits& limits)
{
	PackageRecoveryWriteResult result;
	if (m_retired || !m_lease || !m_lease->isLocked()) { result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery session is no longer active.")); return result; }
	const QString path = packageRecoveryPath(m_directory, m_id);
	const QString input = staging.draftPath().isEmpty() ? staging.sourcePath() : staging.draftPath();
	if (!input.isEmpty() && containsPath(path, input)) {
		result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "A recovery copy cannot checkpoint itself. Restore it to a separate draft first.")); return result;
	}
	if (!m_manifestSha256.isEmpty() && !inspectPackageRecovery(path).readable()) { result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "The existing recovery manifest is damaged; it was preserved for review.")); return result; }
	if (limits.maximumBytes <= 0 || limits.maximumCopies < 1 || limits.maximumCopies > PackageRecoveryScanLimit) {
		result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "Choose positive recovery storage limits and between 1 and 128 copies.")); return result;
	}
	auto storeLock = lockStore(m_directory, &result.error); if (!storeLock) { return result; }
	if (cancelled(control)) { result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "Package recovery cancelled.")); return result; }
	// Maintenance is optional: an older reader can retain unreachable objects
	// while a newer checkpoint commits. Count all retained bytes against quota;
	// inventory and the writer still reject unsafe layouts or changed manifests.
	if (QFileInfo::exists(path)) { tidyCopy(path, m_manifestSha256, false, false, &result.maintenanceError, {}, control); }
	const auto inventory = listPackageRecoveries(m_directory, control);
	if (!inventory.storageComplete) { result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "Recovery storage usage could not be measured safely. Review the recovery copies before retrying.")); return result; }
	if ((!QFileInfo::exists(path) && inventory.records.size() >= limits.maximumCopies) || inventory.records.size() > limits.maximumCopies) {
		result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery copy limit has been reached. Review copies or increase the limit; existing copies are preserved.")); return result;
	}
	if (inventory.storageBytes >= limits.maximumBytes) {
		result.error = message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery storage limit has been reached. Review copies or increase the limit; existing copies are preserved.")); return result;
	}
	const QJsonObject metadata{{QStringLiteral("type"), QStringLiteral("vibestudio-package-recovery")}, {QStringLiteral("version"), 1},
		{QStringLiteral("id"), m_id}, {QStringLiteral("title"), title.left(255)}, {QStringLiteral("originalDraftPath"), staging.draftPath()},
		{QStringLiteral("writtenUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
	PackageStagingModel copy = staging;
	if (!PackageDraft::saveWithRecovery(path, &copy, !m_manifestSha256.isEmpty(), &result.error, control, false, metadata, m_manifestSha256, limits.maximumBytes - inventory.storageBytes)) { return result; }
	const auto info = inspectPackageRecovery(path);
	if (!info.readable()) { result.error = info.error; return result; }
	result.path = path; result.manifestSha256 = info.manifestSha256; m_manifestSha256 = info.manifestSha256;
	result.unavailableBaseCount = info.unavailableBaseCount;
	copy.clear(); // The saved model's read lease must end before maintenance.
	// Reclaim only objects not reachable from the newly committed base/history.
	// Failed maintenance leaves the valid checkpoint and is reported separately.
	result.maintenanceError.clear();
	tidyCopy(path, m_manifestSha256, false, false, &result.maintenanceError);
	return result;
}

bool PackageRecoverySession::retire(QString* error)
{
	if (error) { error->clear(); }
	if (m_retired) { return true; }
	if (!m_lease || !m_lease->isLocked()) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery session is no longer active."))); }
	auto storeLock = lockStore(m_directory, error); if (!storeLock) { return false; }
	if (!tidyCopy(packageRecoveryPath(m_directory, m_id), m_manifestSha256, true, false, error)) { return false; }
	m_retired = true; m_lease.reset(); return true;
}

bool restorePackageRecovery(const QString& directory, const QString& id, const QByteArray& expectedManifestSha256,
	const QString& destination, PackageStagingModel* restored, QString* error, const PackageReadControl& control, bool dryRun)
{
	if (error) { error->clear(); }
	const QString path = packageRecoveryPath(directory, id);
	if (path.isEmpty() || expectedManifestSha256.size() != 32 || !safePath(path, error)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Select a recovery copy and its manifest checksum."))); }
	const QString lockPath = QDir(path).filePath(QStringLiteral(".write.lock"));
	if (!safePath(lockPath, error)) { return false; }
	QLockFile lock(lockPath); lock.setStaleLockTime(0);
	if (dryRun ? QFileInfo::exists(lockPath) : !lock.tryLock()) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "This package recovery copy is being read or written. Try again when that operation finishes."))); }
	const auto info = inspectPackageRecovery(path, control);
	if (!info.readable()) { return fail(error, info.error); }
	if (info.manifestSha256 != expectedManifestSha256) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery copy changed. Refresh the list before continuing."))); }
	if (destination.trimmed().isEmpty() || containsPath(directory, destination) || QFileInfo::exists(destination)
		|| (!info.originalDraftPath.isEmpty() && containsPath(info.originalDraftPath, destination))) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Restore to a new .vibepackage directory outside the recovery store and original draft.")));
	}
	PackageStagingModel copy;
	if (!PackageDraft::load(path, &copy, error, control)) { return false; }
	// Recheck after payload verification in dry runs too, where no lock is taken.
	if (inspectPackageRecovery(path, control).manifestSha256 != expectedManifestSha256) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The recovery copy changed while it was being verified."))); }
	if (!PackageDraft::save(destination, &copy, false, error, control, dryRun)) { return false; }
	if (restored && !dryRun) { *restored = std::move(copy); }
	return true;
}

namespace {
bool discardReviewedCopy(const QString& directory, const QString& id, const QByteArray& expected, bool storageReview, bool dryRun, QString* error)
{
	if (error) { error->clear(); }
	const QString path = packageRecoveryPath(directory, id);
	if (path.isEmpty() || expected.size() != 32 || !safePath(path, error)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "Select a recovery copy and its review checksum."))); }
	if (!QFileInfo::exists(path)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "The selected recovery copy no longer exists."))); }
	const QString leasePath = sessionPath(directory, id), storePath = QDir(directory).filePath(QStringLiteral(".store.lock"));
	if (!safePath(leasePath, error) || !safePath(storePath, error)) { return false; }
	QLockFile lease(leasePath); lease.setStaleLockTime(0);
	std::unique_ptr<QLockFile> storeLock;
	if (dryRun) {
		if (QFileInfo::exists(leasePath) || QFileInfo::exists(storePath)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "A recovery lock is present. Close the owning editor or wait for its operation before discarding this copy."))); }
	} else {
		if (!lease.tryLock()) { return fail(error, message(QT_TRANSLATE_NOOP("PackageRecovery", "This package recovery copy belongs to an active editor session."))); }
		storeLock = lockStore(directory, error); if (!storeLock) { return false; }
	}
	return tidyCopy(path, storageReview ? QByteArray() : expected, true, dryRun, error, storageReview ? expected : QByteArray());
}
} // namespace

bool discardPackageRecovery(const QString& directory, const QString& id, const QByteArray& expectedManifestSha256, bool dryRun, QString* error)
{
	return discardReviewedCopy(directory, id, expectedManifestSha256, false, dryRun, error);
}

bool discardPackageRecoveryStorage(const QString& directory, const QString& id, const QByteArray& expectedStorageSha256, bool dryRun, QString* error)
{
	return discardReviewedCopy(directory, id, expectedStorageSha256, true, dryRun, error);
}

} // namespace vibestudio
