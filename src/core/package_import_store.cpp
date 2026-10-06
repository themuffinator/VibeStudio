#include "core/package_import_store.h"
#include "core/package_storage.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>

namespace vibestudio {
namespace {
constexpr qint64 metadataLimit = 65536;
const QString suffix = QStringLiteral(".working");
QString message(const char* value) { return QCoreApplication::translate("PackageImportStore", value); }
bool fail(QString* error, const QString& value) { if (error) { *error = value; } return false; }
bool cancelled(const PackageReadControl& control) { return control.isCancelled && control.isCancelled(); }
bool validId(const QString& value) { const QUuid id(value); return !id.isNull() && id.toString(QUuid::WithoutBraces) == value; }
QString sessionPath(const QString& root, const QString& id) { return QDir(root).filePath(id + suffix); }
QString manifestPath(const QString& session) { return QDir(session).filePath(QStringLiteral("working.json")); }

QThreadPool& cleanupPool()
{
	// Keep teardown independent of preview/indexing work on the global pool.
	// Tasks can release their last session owner and enqueue its final cleanup.
	struct Pool : QThreadPool { Pool() { setMaxThreadCount(1); } };
	static Pool pool;
	return pool;
}

struct Counter {
	QString id;
	QDateTime created;
	qint64 bytes = 0;
	int files = 0;
};

bool readCounter(const QString& session, Counter* result, QString* error)
{
	const QString path = manifestPath(session); const QFileInfo info(path);
	if (!safePackageStoragePath(path, error)) { return false; }
	QFile file(path);
	if (!info.isFile() || info.size() > metadataLimit || !file.open(QIODevice::ReadOnly)) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import metadata is missing, unreadable or too large.")));
	}
	const auto bytes = file.read(metadataLimit + 1);
	QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse); const auto object = document.object();
	auto unsignedObject = object; unsignedObject.remove(QStringLiteral("sha256"));
	const auto expectedHash = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(unsignedObject).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
	Counter value; value.id = object.value(QStringLiteral("id")).toString();
	value.created = QDateTime::fromString(object.value(QStringLiteral("createdUtc")).toString(), Qt::ISODateWithMs);
	bool byteCountOk = false; const auto text = object.value(QStringLiteral("reservedBytes")).toString();
	value.bytes = text.toLongLong(&byteCountOk); value.files = object.value(QStringLiteral("reservedFiles")).toInt(-1);
	if (file.error() != QFileDevice::NoError || bytes.size() > metadataLimit || parse.error != QJsonParseError::NoError
		|| object.value(QStringLiteral("type")) != QStringLiteral("vibestudio-package-working")
		|| object.value(QStringLiteral("version")).toInt() != 1 || object.value(QStringLiteral("sha256")).toString() != expectedHash || !validId(value.id)
		|| QFileInfo(session).fileName() != value.id + suffix || !value.created.isValid()
		|| !byteCountOk || value.bytes < 0 || QString::number(value.bytes) != text || value.files < 0 || value.files > PackageImportFileLimit
		|| object.value(QStringLiteral("reservedFiles")).toDouble(-1) != value.files) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import metadata is incomplete or invalid. Review this session before discarding it.")));
	}
	*result = value; return true;
}

bool writeCounter(const QString& session, const Counter& counter, QString* error)
{
	const QString path = manifestPath(session);
	if (!safePackageStoragePath(path, error)) { return false; }
	QJsonObject object{
		{QStringLiteral("type"), QStringLiteral("vibestudio-package-working")}, {QStringLiteral("version"), 1},
		{QStringLiteral("id"), counter.id}, {QStringLiteral("createdUtc"), counter.created.toUTC().toString(Qt::ISODateWithMs)},
		{QStringLiteral("reservedBytes"), QString::number(counter.bytes)}, {QStringLiteral("reservedFiles"), counter.files}
	};
	object.insert(QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
	const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
	QSaveFile file(path); file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { return fail(error, file.errorString()); }
	return true;
}

std::unique_ptr<QLockFile> lockStore(const QString& root, QString* error, const PackageReadControl& control = {})
{
	const QString path = QDir(root).filePath(QStringLiteral(".store.lock"));
	if (!safePackageStoragePath(path, error)) { return {}; }
	auto lock = std::make_unique<QLockFile>(path); lock->setStaleLockTime(0);
	QElapsedTimer timer; timer.start();
	while (!lock->tryLock()) {
		if (cancelled(control) || timer.elapsed() >= 5000) {
			fail(error, cancelled(control) ? message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import operation cancelled."))
				: message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage is busy. Retry after its owner finishes, or review its lock files in Working Imports."))); return {};
		}
		QThread::msleep(10);
	}
	return lock;
}

QStringList sessionDirectories(const QString& root, QString* error, const PackageReadControl& control)
{
	QStringList result;
	if (!safePackageStoragePath(root, error)) { return {}; }
	if (!QFileInfo::exists(root)) { return {}; }
	if (!QFileInfo(root).isDir()) { fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage is not a directory."))); return {}; }
	QDirIterator iterator(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
	int seen = 0;
	while (iterator.hasNext()) {
		const QString path = iterator.next(); const QFileInfo info(path);
		if (cancelled(control)) { fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import operation cancelled."))); return {}; }
		if (++seen > PackageImportSessionLimit + 2 || !safePackageStoragePath(path, error)) {
			if (error && error->isEmpty()) { *error = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage exceeds the session scan limit.")); } return {};
		}
		if ((info.fileName() == QStringLiteral(".store.lock") || info.fileName() == QStringLiteral(".store.lock.rmlock")) && info.isFile()) { continue; }
		if (!info.isDir() || !info.fileName().endsWith(suffix) || !validId(info.fileName().chopped(suffix.size()))) {
			fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage contains an unrecognized path: %1")).arg(path)); return {};
		}
		if (result.size() >= PackageImportSessionLimit) {
			fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage exceeds the session scan limit."))); return {};
		}
		result << path;
	}
	result.sort(); return result;
}

struct Inspection {
	PackageImportInfo info;
	QVector<PackageStorageFile> members;
};

void hashField(QCryptographicHash& hash, const QByteArray& value)
{
	hash.addData(QByteArray::number(value.size())); hash.addData(":"); hash.addData(value);
}

Inspection inspectSession(const QString& path, const PackageReadControl& control, int maximumFiles)
{
	Inspection result; auto& info = result.info; info.path = path;
	info.id = QFileInfo(path).fileName().chopped(suffix.size());
	const auto unsafe = [&](const QString& error) { info.storageError = error; info.fingerprint.clear(); return result; };
	if (!validId(info.id) || !safePackageStoragePath(path, &info.storageError) || !QFileInfo(path).isDir()) {
		return unsafe(info.storageError.isEmpty() ? message(QT_TRANSLATE_NOOP("PackageImportStore", "Invalid working import session.")) : info.storageError);
	}
	const QFileInfo before(path), objects(QDir(path).filePath(QStringLiteral("objects")));
	Counter counter;
	if (readCounter(path, &counter, &info.error)) { info.reservedBytes = counter.bytes; info.reservedFiles = counter.files; info.createdUtc = counter.created; }
	const auto add = [&](const QString& filePath, const QString& relative, bool metadata) {
		if (cancelled(control)) { info.storageError = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import operation cancelled.")); return false; }
		const QFileInfo file(filePath);
		if (!safePackageStoragePath(filePath, &info.storageError) || !file.isFile() || file.size() < 0) {
			if (info.storageError.isEmpty()) { info.storageError = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage contains an unsafe file.")); } return false;
		}
		if (result.members.size() >= maximumFiles + 32 || (!metadata && ++info.files > maximumFiles)
			|| file.size() > std::numeric_limits<qint64>::max() - info.bytes) {
			info.storageError = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage exceeds its bounded scan limits.")); return false;
		}
		PackageStorageFile member; member.relativePath = relative; member.bytes = file.size();
		member.modifiedMs = file.lastModified().toMSecsSinceEpoch();
		// Match packageStorageFileUnchanged's sentinel when birth time is unavailable.
		const auto created = file.birthTime(); member.createdMs = created.isValid() ? created.toMSecsSinceEpoch() : 0;
		if (metadata && file.size() <= metadataLimit) {
			QFile input(filePath);
			if (!input.open(QIODevice::ReadOnly)) { info.storageError = input.errorString(); return false; }
			const auto bytes = input.read(metadataLimit + 1);
			if (bytes.size() != file.size() || input.error() != QFileDevice::NoError) {
				info.storageError = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import metadata changed during review.")); return false;
			}
			member.metadataSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
		}
		if (!metadata) { info.bytes += member.bytes; }
		result.members << member;
		if (control.progress) { control.progress(path, info.files, std::max(1, info.reservedFiles)); }
		return true;
	};
	QDirIterator top(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
	int topCount = 0;
	while (top.hasNext()) {
		const QString entry = top.next(); const QFileInfo file(entry); const auto name = file.fileName();
		if (++topCount > 32 || !safePackageStoragePath(entry, &info.storageError)) { return unsafe(info.storageError.isEmpty() ? message(QT_TRANSLATE_NOOP("PackageImportStore", "Too many working import metadata files.")) : info.storageError); }
		if (name == QStringLiteral(".lease") && file.isFile()) { info.leasePresent = true; continue; }
		if (name == QStringLiteral(".lease.rmlock") && file.isFile()) { continue; }
		if (name == QStringLiteral("objects") && file.isDir()) { continue; }
		if (name == QStringLiteral("working.json") || packageStorageTemporaryName(name, QStringLiteral("working.json."))
			|| packageStorageTemporaryName(name, QStringLiteral(".working.json."))) {
			if (!add(entry, name, true)) { return unsafe(info.storageError); } continue;
		}
		return unsafe(message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import session contains an unrecognized path: %1")).arg(entry));
	}
	if (objects.exists()) {
		if (!objects.isDir() || !safePackageStoragePath(objects.absoluteFilePath(), &info.storageError)) {
			return unsafe(info.storageError.isEmpty() ? message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import payload storage is unsafe.")) : info.storageError);
		}
		QDirIterator payloads(objects.absoluteFilePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
		while (payloads.hasNext()) {
			const auto entry = payloads.next(); const QFileInfo file(entry); const auto name = file.fileName();
			if (!name.endsWith(QStringLiteral(".blob")) || !validId(name.chopped(5))) { return unsafe(message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage contains an unrecognized payload."))); }
			if (!add(entry, QStringLiteral("objects/") + name, false)) { return unsafe(info.storageError); }
		}
	}
	for (const auto& member : result.members) {
		if (!packageStorageFileUnchanged(path, member)) { return unsafe(message(QT_TRANSLATE_NOOP("PackageImportStore", "Working imports changed during review. Refresh the list."))); }
	}
	const QFileInfo after(path), objectsAfter(objects.absoluteFilePath());
	if (before.canonicalFilePath() != after.canonicalFilePath() || before.birthTime() != after.birthTime()
		|| before.lastModified() != after.lastModified() || objects.exists() != objectsAfter.exists()
		|| objects.birthTime() != objectsAfter.birthTime() || objects.lastModified() != objectsAfter.lastModified()) {
		return unsafe(message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import membership changed during review. Refresh the list.")));
	}
	if (cancelled(control)) { return unsafe(message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import operation cancelled."))); }
	std::sort(result.members.begin(), result.members.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hashField(hash, before.canonicalFilePath().toUtf8()); hashField(hash, QByteArray::number(before.birthTime().toMSecsSinceEpoch()));
	for (const auto& member : result.members) {
		hashField(hash, member.relativePath.toUtf8()); hashField(hash, QByteArray::number(member.bytes));
		hashField(hash, QByteArray::number(member.modifiedMs)); hashField(hash, QByteArray::number(member.createdMs));
		hashField(hash, member.metadataSha256);
	}
	if (info.error.isEmpty() && (info.bytes > info.reservedBytes || info.files > info.reservedFiles)) {
		info.error = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working payloads exceed their recorded reservations. Review this session before continuing."));
	}
	info.fingerprint = hash.result(); return result;
}

bool removeReviewed(const Inspection& reviewed, QString* error, const PackageReadControl& control)
{
	const auto& path = reviewed.info.path;
	const auto current = inspectSession(path, control, PackageImportFileLimit);
	if (!current.info.reviewable() || current.info.fingerprint != reviewed.info.fingerprint) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working imports changed. Refresh the list before discarding them.")));
	}
	// Payloads and any interrupted metadata write go first; the committed
	// reservation remains conservative if cleanup is interrupted.
	for (const auto& member : reviewed.members) {
		if (member.relativePath == QStringLiteral("working.json")) { continue; }
		if (cancelled(control)) { return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import cleanup cancelled. Refresh to review the remaining files."))); }
		if (!packageStorageFileUnchanged(path, member) || !QFile::remove(QDir(path).filePath(member.relativePath))) {
			return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "A working import changed or could not be removed. Refresh to review the remaining files.")));
		}
	}
	const auto objects = QDir(path).filePath(QStringLiteral("objects"));
	if (QFileInfo::exists(objects) && (!safePackageStoragePath(objects, error) || !QDir().rmdir(objects))) { return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import payload directory could not be removed."))); }
	for (const auto& member : reviewed.members) {
		if (member.relativePath != QStringLiteral("working.json")) { continue; }
		if (!packageStorageFileUnchanged(path, member) || !QFile::remove(manifestPath(path))) { return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import metadata changed or could not be removed."))); }
	}
	return true;
}

class ImportSession final {
public:
	QString root, path, id;
	std::shared_ptr<QLockFile> lease;
	std::atomic_bool cleanupBlocked{false};
	~ImportSession()
	{
		// A failed cleanup already retained the reservation and reported why.
		// Do not repeat a five-second store wait for every queued file at exit.
		if (cleanupBlocked.load()) { return; }
		// Keep the process lease until queued cleanup has finished. QLockFile
		// is a plain lease object; no QObject or payload handle crosses threads.
		cleanupPool().start([root = root, path = path, lease = std::move(lease)]() {
			if (!lease) { return; }
			QString error; auto lock = lockStore(root, &error);
			if (lock) {
				const auto reviewed = inspectSession(path, {}, PackageImportFileLimit);
				if (reviewed.info.reviewable() && reviewed.info.error.isEmpty() && reviewed.info.files == 0
					&& reviewed.info.reservedFiles == 0 && reviewed.info.reservedBytes == 0
					&& removeReviewed(reviewed, &error, {})) {
					lease->unlock();
					if (safePackageStoragePath(path) && QDir().rmdir(path)) { return; }
				}
			}
			// An incomplete, changed or unremovable session stays available for
			// explicit reviewed maintenance; it is never recursively deleted.
			qWarning().noquote() << message(QT_TRANSLATE_NOOP("PackageImportStore", "Working imports were retained for review: %1. %2")).arg(path, error);
		});
	}
	bool release(const QString& object, qint64 reserved, const PackageFileIdentity* expected, const QDateTime& created, QString* error)
	{
		auto lock = lockStore(root, error); if (!lock) { return false; }
		Counter counter;
		if (!readCounter(path, &counter, error) || counter.files < 1 || counter.bytes < reserved) { return false; }
		const QFileInfo info(object);
		if (!safePackageStoragePath(object, error) || (info.exists() && !info.isFile())
			|| (info.exists() && expected && (!expected->matchesMetadata() || info.birthTime() != created))
			|| (info.exists() && !QFile::remove(object))) {
			return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "A working import changed or could not be removed; its reservation was kept.")));
		}
		counter.bytes -= reserved; --counter.files;
		return writeCounter(path, counter, error);
	}
};

QMutex sessionsMutex;
std::map<QString, std::weak_ptr<ImportSession>> sessions;
} // namespace

class PackageContentStorage final {
public:
	PackageContentStorage(std::shared_ptr<ImportSession> session, const PackageFileIdentity& identity, qint64 reserved)
		: m_session(std::move(session)), m_identity(identity), m_created(QFileInfo(identity.path).birthTime()), m_reserved(reserved) {}
	~PackageContentStorage()
	{
		cleanupPool().start([session = std::move(m_session), identity = m_identity, created = m_created, reserved = m_reserved]() {
			if (session->cleanupBlocked.load()) { return; }
			QString error;
			// Metadata checks belong to this specific file, not shared session
			// state: a document may retain other files from the same session.
			const QFileInfo file(identity.path);
			if (file.exists() && (!identity.matchesMetadata() || file.birthTime() != created)) {
				session->cleanupBlocked.store(true);
				qWarning().noquote() << message(QT_TRANSLATE_NOOP("PackageImportStore", "A changed working import was retained: %1")).arg(identity.path); return;
			}
			if (!session->release(identity.path, reserved, &identity, created, &error)) {
				session->cleanupBlocked.store(true);
				qWarning().noquote() << message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import cleanup needs attention: %1. %2")).arg(identity.path, error);
			}
		});
	}
private:
	std::shared_ptr<ImportSession> m_session;
	PackageFileIdentity m_identity;
	QDateTime m_created;
	qint64 m_reserved;
};

struct PackageImportReservation::State {
	std::shared_ptr<ImportSession> session;
	QString path;
	qint64 bytes = 0;
	bool retained = false;
};
PackageImportReservation::PackageImportReservation(std::unique_ptr<State> state) : m_state(std::move(state)) {}
PackageImportReservation::~PackageImportReservation()
{
	if (!m_state || m_state->retained) { return; }
	QString error;
	if (!m_state->session->release(m_state->path, m_state->bytes, nullptr, {}, &error)) {
		qWarning().noquote() << message(QT_TRANSLATE_NOOP("PackageImportStore", "An incomplete working import was retained: %1. %2")).arg(m_state->path, error);
	}
}
QString PackageImportReservation::path() const { return m_state->path; }
QString PackageImportReservation::directory() const { return m_state->session->root; }
std::shared_ptr<const PackageContentStorage> PackageImportReservation::retain(const PackageFileIdentity& identity)
{
	if (m_state->retained || identity.path != m_state->path || identity.size != m_state->bytes || identity.storage) { return {}; }
	auto storage = std::make_shared<PackageContentStorage>(m_state->session, identity, m_state->bytes);
	m_state->retained = true; return storage;
}

QString packageImportDirectory()
{
	std::error_code error;
	const auto resolved = std::filesystem::canonical(std::filesystem::path(QDir::tempPath().toStdU16String()), error);
	if (error) { return {}; }
	const QString root = QString::fromStdU16String(resolved.generic_u16string());
	return QFileInfo(root).isDir() ? QDir(root).filePath(QStringLiteral("vibestudio-package-imports")) : QString();
}

void waitForPackageImportCleanup() { cleanupPool().waitForDone(); }

std::unique_ptr<PackageImportReservation> reservePackageImport(const PackageFileIdentityPtr& source,
	QString* error, const PackageReadControl& control, const QString& directory)
{
	QString localError; if (!error) { error = &localError; } error->clear();
	if (!source || source->size < 0 || cancelled(control)) { fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import operation cancelled or missing a source."))); return {}; }
	const auto options = control.importOptions ? *control.importOptions : PackageImportOptions();
	const QString selected = !directory.isEmpty() ? directory : !options.directory.isEmpty() ? options.directory : packageImportDirectory();
	if (selected.isEmpty() || options.maximumBytes < 0 || options.maximumFiles < 1 || options.maximumFiles > PackageImportFileLimit) {
		fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage or limits are invalid."))); return {};
	}
	const QString root = QFileInfo(selected).absoluteFilePath();
	if (!safePackageStoragePath(root, error) || !QDir().mkpath(root)) {
		if (error && error->isEmpty()) { *error = message(QT_TRANSLATE_NOOP("PackageImportStore", "The working import directory is unavailable.")); } return {};
	}
	// The registry holds weak references. Document/undo/reader owners, pending
	// reservations and queued cleanup are the only things keeping a lease live.
	QMutexLocker registry(&sessionsMutex);
	for (auto it = sessions.begin(); it != sessions.end();) { it = it->second.expired() ? sessions.erase(it) : std::next(it); }
	auto lock = lockStore(root, error, control); if (!lock) { return {}; }
	const auto directories = sessionDirectories(root, error, control); if (error && !error->isEmpty()) { return {}; }
	qint64 bytes = 0; int files = 0;
	for (const auto& path : directories) {
		Counter counter;
		if (!readCounter(path, &counter, error) || counter.bytes > std::numeric_limits<qint64>::max() - bytes
			|| counter.files > PackageImportFileLimit - files) {
			if (error && error->isEmpty()) { *error = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import reservations exceed their limits.")); } return {};
		}
		bytes += counter.bytes; files += counter.files;
	}
	if (bytes > options.maximumBytes || source->size > options.maximumBytes - bytes || files >= options.maximumFiles) {
		fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage has reached its byte or file limit. Save or close documents, or review abandoned working sessions."))); return {};
	}
	auto session = sessions[root].lock();
	if (!session) {
		if (directories.size() >= PackageImportSessionLimit) { fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Too many working import sessions. Review abandoned sessions first."))); return {}; }
		session = std::make_shared<ImportSession>(); session->root = root;
		session->id = QUuid::createUuid().toString(QUuid::WithoutBraces); session->path = sessionPath(root, session->id);
		if (!QDir(root).mkdir(session->id + suffix) || !QDir(session->path).mkdir(QStringLiteral("objects"))) {
			fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Unable to create working import storage."))); return {};
		}
		session->lease = std::make_shared<QLockFile>(QDir(session->path).filePath(QStringLiteral(".lease")));
		session->lease->setStaleLockTime(0);
		if (!session->lease->tryLock() || !writeCounter(session->path, {session->id, QDateTime::currentDateTimeUtc(), 0, 0}, error)) { return {}; }
		sessions[root] = session;
	}
	Counter counter;
	if (!readCounter(session->path, &counter, error)) { return {}; }
	counter.bytes += source->size; ++counter.files;
	if (!writeCounter(session->path, counter, error)) { return {}; }
	auto state = std::make_unique<PackageImportReservation::State>(); state->session = session; state->bytes = source->size;
	state->path = QDir(session->path).filePath(QStringLiteral("objects/") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".blob"));
	return std::unique_ptr<PackageImportReservation>(new PackageImportReservation(std::move(state)));
}

PackageImportInventory listPackageImports(const QString& directory, const PackageReadControl& control)
{
	PackageImportInventory result; result.directory = QFileInfo(directory).absoluteFilePath();
	if (directory.isEmpty()) { result.error = message(QT_TRANSLATE_NOOP("PackageImportStore", "The working import directory is unavailable.")); return result; }
	const auto inspectLock = [&](const QString& relative) {
		if (cancelled(control)) { return; }
		const auto lock = inspectPackageImportLock(result.directory, relative, control);
		if (lock.exists || !lock.error.isEmpty()) { result.locks << lock; }
	};
	inspectLock(QStringLiteral(".store.lock")); inspectLock(QStringLiteral(".store.lock.rmlock"));
	const auto paths = sessionDirectories(result.directory, &result.error, control);
	int remaining = PackageImportFileLimit;
	for (const auto& path : paths) {
		if (cancelled(control)) { result.cancelled = true; break; }
		auto inspected = inspectSession(path, control, remaining); const auto& info = inspected.info;
		remaining -= info.files; result.files += info.files; result.reservedFiles += info.reservedFiles;
		if (info.bytes > std::numeric_limits<qint64>::max() - result.bytes
			|| info.reservedBytes > std::numeric_limits<qint64>::max() - result.reservedBytes) { result.error = message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import storage totals overflow.")); break; }
		result.bytes += info.bytes; result.reservedBytes += info.reservedBytes;
		result.sessions << info;
		inspectLock(info.id + suffix + QStringLiteral("/.lease")); inspectLock(info.id + suffix + QStringLiteral("/.lease.rmlock"));
		if (!info.reviewable() || !info.error.isEmpty()) { result.error = message(QT_TRANSLATE_NOOP("PackageImportStore", "Some working import sessions or reservations need review.")); }
		if (remaining < 0) { result.truncated = true; break; }
	}
	result.cancelled = result.cancelled || cancelled(control);
	return result;
}

QJsonObject packageImportInventoryJson(const PackageImportInventory& inventory)
{
	QJsonArray values, locks;
	for (const auto& info : inventory.sessions) {
		values.append(QJsonObject{{QStringLiteral("id"), info.id}, {QStringLiteral("path"), info.path},
			{QStringLiteral("createdUtc"), info.createdUtc.toUTC().toString(Qt::ISODateWithMs)},
			{QStringLiteral("payloadBytes"), info.bytes}, {QStringLiteral("reservedBytes"), info.reservedBytes},
			{QStringLiteral("files"), info.files}, {QStringLiteral("reservedFiles"), info.reservedFiles},
			{QStringLiteral("leasePresent"), info.leasePresent}, {QStringLiteral("reviewable"), info.reviewable()},
			{QStringLiteral("storageSha256"), QString::fromLatin1(info.fingerprint.toHex())},
			{QStringLiteral("error"), info.error}, {QStringLiteral("storageError"), info.storageError}});
	}
	for (const auto& lock : inventory.locks) {
		locks.append(QJsonObject{{QStringLiteral("relativePath"), lock.relativePath}, {QStringLiteral("bytes"), lock.bytes},
			{QStringLiteral("exists"), lock.exists}, {QStringLiteral("reviewable"), lock.reviewable()},
			{QStringLiteral("lockSha256"), QString::fromLatin1(lock.fingerprint.toHex())}, {QStringLiteral("error"), lock.error}});
	}
	return {{QStringLiteral("directory"), inventory.directory}, {QStringLiteral("sessions"), values}, {QStringLiteral("locks"), locks},
		{QStringLiteral("payloadBytes"), inventory.bytes}, {QStringLiteral("reservedBytes"), inventory.reservedBytes},
		{QStringLiteral("files"), inventory.files}, {QStringLiteral("reservedFiles"), inventory.reservedFiles},
		{QStringLiteral("complete"), inventory.complete()}, {QStringLiteral("cancelled"), inventory.cancelled},
		{QStringLiteral("error"), inventory.error}};
}

bool discardPackageImports(const QString& directory, const QString& id, const QByteArray& expectedFingerprint,
	bool dryRun, QString* error, const PackageReadControl& control)
{
	if (error) { error->clear(); }
	if (directory.isEmpty() || !validId(id) || expectedFingerprint.size() != 32) { return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Choose a working import session and its current storage checksum."))); }
	const QString root = QFileInfo(directory).absoluteFilePath(), path = sessionPath(root, id);
	if (!safePackageStoragePath(path, error) || !QFileInfo(path).isDir()) { return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import session is unavailable or unsafe."))); }
	std::unique_ptr<QLockFile> store;
	QLockFile lease(QDir(path).filePath(QStringLiteral(".lease"))); lease.setStaleLockTime(0);
	if (!dryRun) {
		store = lockStore(root, error, control); if (!store) { return false; }
		const auto guard = inspectPackageImportLock(root, id + suffix + QStringLiteral("/.lease.rmlock"), control);
		if (guard.exists || !guard.error.isEmpty()) {
			return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Review and release this session's stale-removal lock before discarding its files.")));
		}
		if (!safePackageStoragePath(QDir(path).filePath(QStringLiteral(".lease")), error) || !lease.tryLock()) {
			return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "This working import session still has live document or reader references.")));
		}
	}
	const auto reviewed = inspectSession(path, control, PackageImportFileLimit);
	if (!reviewed.info.reviewable() || reviewed.info.fingerprint != expectedFingerprint) {
		return fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working imports changed or could not be reviewed. Refresh the list before continuing.")));
	}
	if (dryRun) { return true; }
	if (!removeReviewed(reviewed, error, control)) { return false; }
	lease.unlock();
	return safePackageStoragePath(path, error) && (QDir().rmdir(path) || fail(error, message(QT_TRANSLATE_NOOP("PackageImportStore", "Working import session directory could not be removed."))));
}

} // namespace vibestudio
