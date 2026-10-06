#include "core/audio_recovery_store.h"
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
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error) {
		*error = message;
	}
	return false;
}
bool safeDirectory(const QString &directory)
{
	if (directory.trimmed().isEmpty()) {
		return false;
	}
	QFileInfo current(QDir(directory).absolutePath());
	for (;;) {
		if (current.isSymLink() || current.isJunction()) {
			return false;
		}
		const QString parent = current.dir().absolutePath();
		if (parent == current.absoluteFilePath()) {
			break;
		}
		current.setFile(parent);
	}
	return true;
}
bool stopped(const AudioWorkControl &control) { return control.cancelled && control.cancelled(); }

constexpr qint64 SessionRecoveryMetadataLimit = 64 * 1024;
constexpr qint64 SessionRecoveryByteLimit = AudioSessionByteLimit + SessionRecoveryMetadataLimit + 48;
QString recordPath(const QString &directory, const QString &id, AudioRecoveryKind kind)
{
	return kind == AudioRecoveryKind::Session ? audioSessionRecoveryPath(directory, id)
	                                          : audioRecoveryPath(directory, id);
}
qint64 recordLimit(AudioRecoveryKind kind)
{
	return kind == AudioRecoveryKind::Session ? SessionRecoveryByteLimit : AudioProjectByteLimit;
}
bool decodeSessionRecovery(const QByteArray &bytes, AudioSessionRecovery *recovery, QString *error,
                           const AudioWorkControl &control)
{
	if (!recovery || bytes.size() < 48 || bytes.size() > SessionRecoveryByteLimit ||
	    !bytes.startsWith(QByteArrayLiteral("VSRMX\r\n\x1a")) ||
	    qFromLittleEndian<quint32>(bytes.constData() + 8) != 1 ||
	    QCryptographicHash::hash(QByteArrayView(bytes.constData(), bytes.size() - 32), QCryptographicHash::Sha256) !=
	        bytes.right(32)) {
		return fail(error, QCoreApplication::translate("AudioRecovery",
		                                               "Invalid session recovery header, size, version or checksum."));
	}
	const auto size = qFromLittleEndian<quint32>(bytes.constData() + 12);
	if (size > SessionRecoveryMetadataLimit || size > bytes.size() - 48) {
		return fail(error, QCoreApplication::translate("AudioRecovery", "Invalid session recovery metadata length."));
	}
	const auto json = QJsonDocument::fromJson(bytes.mid(16, size));
	const auto metadata = json.object();
	AudioSessionRecovery next;
	next.sourcePath = metadata.value(QStringLiteral("sourcePath")).toString();
	const auto timestamp = metadata.value(QStringLiteral("writtenUtc")).toString();
	next.writtenUtc = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
	if (!json.isObject() || metadata.size() != 2 || !metadata.value(QStringLiteral("sourcePath")).isString() ||
	    next.sourcePath.contains(QChar(0)) || next.sourcePath.size() > 32768 || !next.writtenUtc.isValid() ||
	    !timestamp.endsWith(QLatin1Char('Z'))) {
		return fail(error,
		            QCoreApplication::translate("AudioRecovery", "Invalid session recovery provenance or timestamp."));
	}
	if (stopped(control) ||
	    !decodeAudioSession(bytes.mid(16 + size, bytes.size() - 48 - size), &next.session, error, control)) {
		return false;
	}
	*recovery = std::move(next);
	return true;
}

AudioRecoveryInventory scan(const QString &directory, const AudioWorkControl &control)
{
	AudioRecoveryInventory result;
	if (!safeDirectory(directory) || (QFileInfo::exists(directory) && !QFileInfo(directory).isDir())) {
		result.error =
		    QCoreApplication::translate("AudioRecovery", "The audio recovery folder is unavailable or unsafe.");
		return result;
	}
	if (!QFileInfo::exists(directory)) {
		return result;
	}
	if (!QFileInfo(directory).isReadable()) {
		result.error = QCoreApplication::translate("AudioRecovery", "The audio recovery folder cannot be read.");
		return result;
	}
	QDirIterator entries(directory, {QStringLiteral("*.vsaudio"), QStringLiteral("*.vssession-recovery")},
	                     QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
	int visited = 0;
	while (entries.hasNext()) {
		if (stopped(control)) {
			result.cancelled = true;
			return result;
		}
		if (++visited > AudioRecoveryScanLimit) {
			result.truncated = true;
			break;
		}
		entries.next();
		const QFileInfo file = entries.fileInfo();
		const QString id = file.completeBaseName();
		const auto kind = file.suffix() == QStringLiteral("vssession-recovery") ? AudioRecoveryKind::Session
		                                                                        : AudioRecoveryKind::Waveform;
		if (recordPath(directory, id, kind).isEmpty() ||
		    (file.suffix() != QStringLiteral("vsaudio") && file.suffix() != QStringLiteral("vssession-recovery"))) {
			continue;
		}
		AudioRecoveryInfo record;
		record.kind = kind;
		record.id = id;
		record.path = file.absoluteFilePath();
		record.modifiedUtc = file.lastModified().toUTC();
		record.sessionFilePresent = QFileInfo::exists(record.path + QStringLiteral(".active"));
		if (!file.isFile() || file.isSymLink() || file.isJunction()) {
			record.error = QCoreApplication::translate("AudioRecovery", "Recovery entry is not a regular file.");
		} else {
			record.bytes = file.size();
			result.totalBytes += std::min(record.bytes, std::numeric_limits<qint64>::max() - result.totalBytes);
		}
		result.records.append(record);
	}
	std::sort(result.records.begin(), result.records.end(), [](const auto &a, const auto &b) {
		return a.modifiedUtc == b.modifiedUtc ? a.id < b.id : a.modifiedUtc > b.modifiedUtc;
	});
	return result;
}

QByteArray readRecord(const QString &directory, const QString &id, QString *error, AudioRecoveryKind kind,
                      qint64 maxBytes)
{
	const QString path = recordPath(directory, id, kind);
	const QFileInfo info(path);
	if (path.isEmpty() || !info.isFile() || info.isSymLink() || info.isJunction() || info.size() > maxBytes) {
		fail(error, QCoreApplication::translate(
		                "AudioRecovery", "Recovery file is missing, unsafe, or exceeds the document size limit."));
		return {};
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		fail(error, file.errorString());
		return {};
	}
	const QByteArray bytes = file.read(maxBytes + 1);
	if (file.error() != QFileDevice::NoError || bytes.size() > maxBytes) {
		fail(error, QCoreApplication::translate("AudioRecovery",
		                                        "Recovery file could not be read within the document size limit."));
		return bytes; // The inventory charges partial/failed reads to its budget too.
	}
	return bytes;
}

bool lockSafe(QLockFile &lock, const QString &path, QString *error, int waitMilliseconds = 0)
{
	const QFileInfo info(path);
	if (info.isSymLink() || info.isJunction() || info.isDir() || !lock.tryLock(waitMilliseconds)) {
		return fail(error, QCoreApplication::translate(
		                       "AudioRecovery",
		                       "Recovery is in use by another operation or editor. Try again after it closes."));
	}
	return true;
}

bool fitsInventory(const QString &directory, const QString &id, AudioRecoveryKind kind, qint64 encodedSize,
                   QString *error)
{
	const auto inventory = scan(directory, {});
	const auto existing = std::find_if(inventory.records.cbegin(), inventory.records.cend(),
	                                   [&](const auto &record) { return record.id == id && record.kind == kind; });
	const qint64 previousBytes = existing == inventory.records.cend() ? 0 : existing->bytes;
	const qsizetype count = inventory.records.size() + (existing == inventory.records.cend() ? 1 : 0);
	if (!inventory.error.isEmpty() || inventory.truncated || count > AudioRecoveryCountLimit ||
	    inventory.totalBytes - previousBytes > AudioRecoveryStorageLimit - encodedSize) {
		return fail(error,
		            QCoreApplication::translate(
		                "AudioRecovery", "Recovery storage is full or cannot be inspected safely. Existing copies are "
		                                 "preserved. Save your project or review Recoveries to free space."));
	}
	return true;
}
} // namespace

QString audioRecoveryKindId(AudioRecoveryKind kind)
{
	return kind == AudioRecoveryKind::Session ? QStringLiteral("session") : QStringLiteral("waveform");
}

QString audioRecoveryDirectory()
{
	const QString profile = StudioSettings::overrideFilePath();
	const QString root = profile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
	                                       : QFileInfo(profile).absolutePath();
	return qEnvironmentVariable("VIBESTUDIO_AUDIO_RECOVERY_ROOT",
	                            QDir(root).filePath(QStringLiteral("audio-recovery")));
}

QString audioRecoveryPath(const QString &directory, const QString &id)
{
	const QUuid uuid(id);
	if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != id || !safeDirectory(directory)) {
		return {};
	}
	return QDir(directory).absoluteFilePath(id + QStringLiteral(".vsaudio"));
}

QString audioSessionRecoveryPath(const QString &directory, const QString &id)
{
	return audioRecoveryPath(directory, id).isEmpty()
	           ? QString()
	           : QDir(directory).absoluteFilePath(id + QStringLiteral(".vssession-recovery"));
}

std::unique_ptr<QLockFile> acquireAudioRecoverySession(const QString &directory, const QString &id, QString *error,
                                                       AudioRecoveryKind kind)
{
	if (error) {
		error->clear();
	}
	const QString path = recordPath(directory, id, kind);
	if (path.isEmpty() || !QDir().mkpath(directory) || !safeDirectory(directory)) {
		fail(error,
		     QCoreApplication::translate("AudioRecovery", "The audio recovery folder is unavailable or unsafe."));
		return {};
	}
	auto lock = std::make_unique<QLockFile>(path + QStringLiteral(".active"));
	// A long-lived editor lease must not expire merely because it is old.
	lock->setStaleLockTime(0);
	if (!lockSafe(*lock, path + QStringLiteral(".active"), error)) {
		return {};
	}
	return lock;
}

AudioRecoveryInventory listAudioRecoveries(const QString &directory, const AudioWorkControl &control)
{
	auto inventory = discoverAudioRecoveries(directory, control);
	qint64 readBytes = 0;
	for (auto &record : inventory.records) {
		if (stopped(control)) {
			inventory.cancelled = true;
			break;
		}
		if (!record.error.isEmpty()) {
			continue;
		}
		if (readBytes >= AudioRecoveryStorageLimit || record.bytes > AudioRecoveryStorageLimit - readBytes) {
			record.error = QCoreApplication::translate(
			    "AudioRecovery", "Not verified: inventory read budget reached. Review this file separately.");
			continue;
		}
		const QByteArray bytes = readRecord(directory, record.id, &record.error, record.kind,
		                                    std::min(recordLimit(record.kind), AudioRecoveryStorageLimit - readBytes));
		readBytes += bytes.size();
		if (!record.error.isEmpty()) {
			continue;
		}
		record.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
		if (record.kind == AudioRecoveryKind::Session) {
			AudioSessionRecovery recovery;
			if (decodeSessionRecovery(bytes, &recovery, &record.error, control)) {
				record.sourceName = recovery.session.name;
				record.sourcePath = recovery.sourcePath;
				record.writtenUtc = recovery.writtenUtc;
				record.sampleRate = recovery.session.sampleRate;
				record.channels = 2;
				record.frames = audioSessionFrames(recovery.session);
				record.tracks = int(recovery.session.tracks.size());
				for (const auto &track : recovery.session.tracks) {
					record.clips += int(track.regions.size());
				}
			}
			continue;
		}
		AudioProject project;
		if (!decodeAudioProject(bytes, &project, &record.error, control)) {
			continue;
		}
		record.sourceName = project.sourceName;
		record.sourcePath = project.sourcePath;
		record.channels = project.clip.channels;
		record.sampleRate = project.clip.sampleRate;
		record.frames = project.clip.frameCount();
		record.writtenUtc =
		    QDateTime::fromString(project.metadata.value(QStringLiteral("recoveryWrittenUtc")).toString(),
		                          Qt::ISODateWithMs)
		        .toUTC();
		if (!record.writtenUtc.isValid()) {
			record.error =
			    QCoreApplication::translate("AudioRecovery", "This document has no valid recovery timestamp.");
		}
	}
	if (stopped(control)) {
		inventory.cancelled = true;
	}
	return inventory;
}

AudioRecoveryInventory discoverAudioRecoveries(const QString &directory, const AudioWorkControl &control)
{
	return scan(directory, control);
}

QString writeAudioRecovery(const AudioProject &project, const QString &directory, const QString &id, QString *error)
{
	if (error) {
		error->clear();
	}
	const QString path = audioRecoveryPath(directory, id);
	if (path.isEmpty() || !QDir().mkpath(directory) || !safeDirectory(directory)) {
		fail(error,
		     QCoreApplication::translate("AudioRecovery", "The audio recovery folder is unavailable or unsafe."));
		return {};
	}
	const QString lockPath = QDir(directory).filePath(QStringLiteral(".inventory.lock"));
	QLockFile lock(lockPath);
	// Background editors serialize the shared budget without dropping a lone
	// dirty edit merely because another checkpoint is finishing.
	if (!lockSafe(lock, lockPath, error, 5000)) {
		return {};
	}
	AudioProject snapshot = project;
	snapshot.metadata.insert(QStringLiteral("recoveryWrittenUtc"),
	                         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	snapshot.metadata.insert(QStringLiteral("recoveryOwnerPid"), QCoreApplication::applicationPid());
	qint64 encodedSize = 0;
	{
		const auto encoded = encodeAudioProject(snapshot, error);
		if (encoded.isEmpty()) {
			return {};
		}
		encodedSize = encoded.size();
	}
	if (!fitsInventory(directory, id, AudioRecoveryKind::Waveform, encodedSize, error)) {
		return {};
	}
	const auto report = writeAudioProject(snapshot, {path, true, false, {}, project.sourcePath});
	if (!report.succeeded) {
		fail(error, report.error);
		return {};
	}
	return path;
}

static bool removeRecovery(const QString &directory, const QString &id, QString *error, AudioRecoveryKind kind)
{
	if (error) {
		error->clear();
	}
	const QString path = recordPath(directory, id, kind);
	const QFileInfo info(path);
	if (path.isEmpty() || info.isSymLink() || info.isJunction() || info.isDir()) {
		return fail(error,
		            QCoreApplication::translate("AudioRecovery", "The audio recovery path is invalid or unsafe."));
	}
	if (!info.exists()) {
		return true;
	}
	const QString folderLockPath = QDir(directory).filePath(QStringLiteral(".inventory.lock"));
	QLockFile folderLock(folderLockPath), fileLock(path + QStringLiteral(".lock"));
	if (!lockSafe(folderLock, folderLockPath, error) || !lockSafe(fileLock, path + QStringLiteral(".lock"), error)) {
		return false;
	}
	if (!QFile::remove(path)) {
		return fail(error,
		            QCoreApplication::translate("AudioRecovery", "Unable to remove the local audio recovery copy."));
	}
	return true;
}

bool removeAudioRecovery(const QString &directory, const QString &id, QString *error)
{
	return removeRecovery(directory, id, error, AudioRecoveryKind::Waveform);
}
bool removeAudioSessionRecovery(const QString &directory, const QString &id, QString *error)
{
	return removeRecovery(directory, id, error, AudioRecoveryKind::Session);
}

QString writeAudioSessionRecovery(const AudioSession &session, const QString &sourcePath, const QString &directory,
                                  const QString &id, QString *error)
{
	if (error) {
		error->clear();
	}
	const QString path = audioSessionRecoveryPath(directory, id);
	if (path.isEmpty() || sourcePath.contains(QChar(0)) || sourcePath.size() > 32768 || !QDir().mkpath(directory) ||
	    !safeDirectory(directory)) {
		fail(error, QCoreApplication::translate("AudioRecovery",
		                                        "The session recovery folder or provenance is invalid or unsafe."));
		return {};
	}
	const auto metadata = QJsonDocument(QJsonObject{{QStringLiteral("sourcePath"), sourcePath},
	                                                {QStringLiteral("writtenUtc"),
	                                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}})
	                          .toJson(QJsonDocument::Compact);
	if (metadata.size() > SessionRecoveryMetadataLimit) {
		fail(error, QCoreApplication::translate("AudioRecovery", "Session recovery provenance exceeds 64 KiB."));
		return {};
	}
	QByteArray bytes = QByteArrayLiteral("VSRMX\r\n\x1a");
	bytes.resize(16);
	qToLittleEndian<quint32>(1, bytes.data() + 8);
	qToLittleEndian<quint32>(quint32(metadata.size()), bytes.data() + 12);
	bytes += metadata;
	{
		const auto native = encodeAudioSession(session, error);
		if (native.isEmpty()) {
			return {};
		}
		bytes += native;
	}
	bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	QStringList protectedPaths{sourcePath};
	for (const auto &source : session.sources) {
		protectedPaths.append(source.audio.sourcePath);
	}
	const auto safeTarget = [&] {
		const QFileInfo info(path);
		if (!safeDirectory(directory) || info.isSymLink() || info.isJunction() || (info.exists() && !info.isFile())) {
			return false;
		}
		return std::none_of(protectedPaths.cbegin(), protectedPaths.cend(), [&](const auto &source) {
			return !source.isEmpty() && audioPathsReferToSameFile(path, source);
		});
	};
	const QString folderPath = QDir(directory).filePath(QStringLiteral(".inventory.lock"));
	QLockFile folderLock(folderPath), fileLock(path + QStringLiteral(".lock"));
	if (!lockSafe(folderLock, folderPath, error, 5000) || !lockSafe(fileLock, path + QStringLiteral(".lock"), error) ||
	    !fitsInventory(directory, id, AudioRecoveryKind::Session, bytes.size(), error)) {
		return {};
	}
	if (!safeTarget()) {
		fail(error, QCoreApplication::translate("AudioRecovery",
		                                        "The recovery destination is unsafe or refers to source media."));
		return {};
	}
	QSaveFile output(path);
	output.setDirectWriteFallback(false);
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) {
		fail(error, output.errorString());
		return {};
	}
	if (!safeTarget()) {
		fail(error,
		     QCoreApplication::translate("AudioRecovery", "The recovery destination became unsafe during writing."));
		return {};
	}
	if (!output.commit()) {
		fail(error, output.errorString());
		return {};
	}
	return path;
}

bool readAudioSessionRecovery(const QString &path, const QByteArray &expectedSha256, AudioSessionRecovery *recovery,
                              QString *error, const AudioWorkControl &control)
{
	if (error) {
		error->clear();
	}
	const QFileInfo info(path);
	if (!recovery || expectedSha256.size() != 32 || !safeDirectory(info.absolutePath()) || !info.isFile() ||
	    info.isSymLink() || info.isJunction() || info.size() > SessionRecoveryByteLimit) {
		return fail(error, QCoreApplication::translate(
		                       "AudioRecovery", "Choose a regular session recovery and its reviewed SHA-256 digest."));
	}
	QFile input(path);
	if (!input.open(QIODevice::ReadOnly)) {
		return fail(error, input.errorString());
	}
	const auto bytes = input.read(SessionRecoveryByteLimit + 1);
	if (input.error() != QFileDevice::NoError || bytes.size() > SessionRecoveryByteLimit ||
	    QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != expectedSha256) {
		return fail(error,
		            QCoreApplication::translate(
		                "AudioRecovery", "Recovery changed since review. Refresh Recoveries before restoring it."));
	}
	return decodeSessionRecovery(bytes, recovery, error, control);
}

bool discardAudioRecovery(const QString &directory, const QString &id, const QByteArray &expectedSha256, bool dryRun,
                          QString *error, AudioRecoveryKind kind)
{
	if (error) {
		error->clear();
	}
	const QString path = recordPath(directory, id, kind);
	if (path.isEmpty() || expectedSha256.size() != 32) {
		return fail(error, QCoreApplication::translate(
		                       "AudioRecovery", "Discard requires a recovery ID and the reviewed SHA-256 digest."));
	}
	// Dry runs are read-only, including lock files. They do not promise that a
	// later commit can acquire the required session and writer locks.
	std::unique_ptr<QLockFile> session, folderLock, fileLock;
	if (!dryRun) {
		session = acquireAudioRecoverySession(directory, id, error, kind);
		if (!session) {
			return false;
		}
		const QString folderPath = QDir(directory).filePath(QStringLiteral(".inventory.lock"));
		folderLock = std::make_unique<QLockFile>(folderPath);
		fileLock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
		if (!lockSafe(*folderLock, folderPath, error) || !lockSafe(*fileLock, path + QStringLiteral(".lock"), error)) {
			return false;
		}
	}
	QString readError;
	const auto bytes = readRecord(directory, id, &readError, kind, recordLimit(kind));
	if (!readError.isEmpty()) {
		return fail(error, readError);
	}
	// Also handles invalid/empty records: their exact bytes were reviewable.
	if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != expectedSha256) {
		return fail(error,
		            QCoreApplication::translate(
		                "AudioRecovery", "Recovery changed since review. Refresh the list before discarding it."));
	}
	if (!dryRun && !QFile::remove(path)) {
		return fail(error, QCoreApplication::translate("AudioRecovery", "Unable to discard the recovery copy."));
	}
	return true;
}

QJsonObject audioRecoveryInventoryJson(const AudioRecoveryInventory &inventory)
{
	QJsonArray records;
	for (const auto &item : inventory.records) {
		records.append(QJsonObject{{QStringLiteral("id"), item.id},
		                           {QStringLiteral("kind"), audioRecoveryKindId(item.kind)},
		                           {QStringLiteral("path"), item.path},
		                           {QStringLiteral("sourceName"), item.sourceName},
		                           {QStringLiteral("sourcePath"), item.sourcePath},
		                           {QStringLiteral("bytes"), item.bytes},
		                           {QStringLiteral("frames"), item.frames},
		                           {QStringLiteral("channels"), item.channels},
		                           {QStringLiteral("sampleRate"), item.sampleRate},
		                           {QStringLiteral("tracks"), item.tracks},
		                           {QStringLiteral("clips"), item.clips},
		                           {QStringLiteral("writtenUtc"), item.writtenUtc.toString(Qt::ISODateWithMs)},
		                           {QStringLiteral("sha256"), QString::fromLatin1(item.sha256.toHex())},
		                           {QStringLiteral("verified"), item.verified()},
		                           {QStringLiteral("error"), item.error},
		                           {QStringLiteral("sessionFilePresent"), item.sessionFilePresent}});
	}
	return {{QStringLiteral("records"), records},
	        {QStringLiteral("totalBytes"), inventory.totalBytes},
	        {QStringLiteral("countLimit"), AudioRecoveryCountLimit},
	        {QStringLiteral("byteLimit"), AudioRecoveryStorageLimit},
	        {QStringLiteral("truncated"), inventory.truncated},
	        {QStringLiteral("cancelled"), inventory.cancelled},
	        {QStringLiteral("error"), inventory.error}};
}
} // namespace vibestudio
