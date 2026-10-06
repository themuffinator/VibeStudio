#include "core/model_assembly_recovery.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
bool safeDirectory(const QString &directory)
{
	if (directory.trimmed().isEmpty())
	{
		return false;
	}
	QFileInfo current(QDir(directory).absolutePath());
	for (;;)
	{
		if (current.isSymLink() || current.isJunction() || (current.exists() && !current.isDir()))
		{
			return false;
		}
		const auto parent = current.dir().absolutePath();
		if (parent == current.absoluteFilePath())
		{
			return true;
		}
		current.setFile(parent);
	}
}
bool lock(QLockFile &lockFile, const QString &path, QString *error, const ModelWorkControl &control = {}, int waitMs = 0)
{
	const QFileInfo file(path);
	lockFile.setStaleLockTime(0);
	if (!file.isSymLink() && !file.isJunction() && !file.isDir())
	{
		QElapsedTimer elapsed;
		elapsed.start();
		for (;;)
		{
			if (lockFile.tryLock(0))
			{
				return true;
			}
			if (lockFile.error() != QLockFile::LockFailedError || elapsed.elapsed() >= waitMs)
			{
				break;
			}
			if (!modelWorkCheckpoint(control, ModelWorkPhase::Writing, 0, 0, error))
			{
				return false;
			}
			QThread::msleep(10);
		}
	}
	return fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "Recovery is in use by another editor or operation."));
}
bool textField(const QJsonObject &object, const QString &key, int limit, bool required = false)
{
	const auto value = object.value(key);
	const auto text = value.toString();
	return value.isString() && text.size() <= limit && !text.contains(QChar::Null) && (!required || !text.isEmpty());
}
bool keys(const QJsonObject &object, const QStringList &names)
{
	return object.size() == names.size() &&
		   std::all_of(names.cbegin(), names.cend(), [&](const auto &name) { return object.contains(name); });
}
bool digest(const QString &text, QByteArray *bytes, bool optional = false)
{
	*bytes = QByteArray::fromHex(text.toLatin1());
	return (optional && text.isEmpty()) || (text.size() == 64 && bytes->size() == 32 && QString::fromLatin1(bytes->toHex()) == text);
}
bool validateSnapshot(const ModelAssemblyRecoverySnapshot &snapshot, QString *error)
{
	if (snapshot.directory.isEmpty() || !QDir::isAbsolutePath(snapshot.directory) || snapshot.directory.size() > 4096 ||
		snapshot.directory.contains(QChar::Null) || snapshot.sourcePath.size() > 4096 || snapshot.sourcePath.contains(QChar::Null) ||
		(!snapshot.sourcePath.isEmpty() && !QDir::isAbsolutePath(snapshot.sourcePath)) ||
		(!snapshot.sourceSha256.isEmpty() && (snapshot.sourceSha256.size() != 32 || snapshot.sourcePath.isEmpty())) ||
		!std::isfinite(snapshot.seconds) || snapshot.seconds < 0 || snapshot.seconds > 1000000 ||
		(!snapshot.selectedPart.isEmpty() && std::none_of(snapshot.assembly.parts.cbegin(), snapshot.assembly.parts.cend(),
														  [&](const auto &part) { return part.id == snapshot.selectedPart; })))
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "The assembly recovery context is invalid."));
	}
	return validateModelAssembly(snapshot.assembly, error);
}
QJsonObject payloadJson(const ModelAssemblyRecoverySnapshot &snapshot)
{
	return {{"assembly", modelAssemblyJson(snapshot.assembly)},
			{"directory", snapshot.directory},
			{"selectedPart", snapshot.selectedPart},
			{"seconds", snapshot.seconds},
			{"sourcePath", snapshot.sourcePath},
			{"sourceSha256", QString::fromLatin1(snapshot.sourceSha256.toHex())}};
}
bool readRecord(const QString &path, QByteArray *bytes, QString *error, const ModelWorkControl &control)
{
	const QFileInfo file(path);
	if (modelAssemblyRecoveryPath(file.absolutePath(), file.completeBaseName()) != file.absoluteFilePath() || !file.isFile() ||
		file.isSymLink() || file.isJunction() || file.size() > modelAssemblyRecoveryByteLimit)
	{
		return fail(error,
					QCoreApplication::translate("ModelAssemblyRecovery", "Recovery is missing, unsafe, or exceeds the 2 MiB limit."));
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, 0, file.size(), error))
	{
		return false;
	}
	QFile input(path);
	if (!input.open(QIODevice::ReadOnly))
	{
		return fail(error, input.errorString());
	}
	*bytes = input.read(modelAssemblyRecoveryByteLimit + 1);
	if (input.error() != QFileDevice::NoError || bytes->size() > modelAssemblyRecoveryByteLimit || bytes->size() != file.size() ||
		!safeDirectory(file.absolutePath()))
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "Recovery changed or could not be read safely."));
	}
	return modelWorkCheckpoint(control, ModelWorkPhase::Reading, bytes->size(), bytes->size(), error);
}
ModelAssemblyRecoveryScan scan(const QString &directory, bool verify, const ModelWorkControl &control)
{
	ModelAssemblyRecoveryScan result;
	if (!safeDirectory(directory) || (QFileInfo::exists(directory) && !QFileInfo(directory).isReadable()))
	{
		result.error = QCoreApplication::translate("ModelAssemblyRecovery", "The assembly recovery folder is unavailable or unsafe.");
		return result;
	}
	QDirIterator entries(directory, {QStringLiteral("*.vsassemblyrecovery")},
						 QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
	qint64 readBytes = 0;
	while (entries.hasNext())
	{
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.records.size(), modelAssemblyRecoveryScanLimit, &result.error))
		{
			break;
		}
		if (result.records.size() >= modelAssemblyRecoveryScanLimit)
		{
			result.limited = true;
			break;
		}
		entries.next();
		const auto file = entries.fileInfo();
		ModelAssemblyRecoveryRecord record;
		record.id = file.completeBaseName();
		record.path = file.absoluteFilePath();
		record.bytes = std::max(qint64(0), file.size());
		result.totalBytes += std::min(record.bytes, std::numeric_limits<qint64>::max() - result.totalBytes);
		if (verify)
		{
			if (record.bytes > modelAssemblyRecoveryStorageLimit - readBytes)
			{
				record.error =
					QCoreApplication::translate("ModelAssemblyRecovery", "Not verified: the recovery scan reached its 32 MiB read budget.");
				result.limited = true;
			}
			else
			{
				readBytes += record.bytes;
				record = inspectModelAssemblyRecovery(record.path, nullptr, control);
			}
		}
		result.records.append(std::move(record));
	}
	if (result.error.isEmpty())
	{
		modelWorkCheckpoint(control, ModelWorkPhase::Reading, result.records.size(), result.records.size(), &result.error);
	}
	std::sort(result.records.begin(), result.records.end(),
			  [](const auto &a, const auto &b) { return a.writtenUtc == b.writtenUtc ? a.path < b.path : a.writtenUtc > b.writtenUtc; });
	return result;
}
bool removeReviewed(const QString &directory, const QString &id, const QByteArray &expected, bool dryRun, QString *error,
					const ModelWorkControl &control)
{
	const auto path = modelAssemblyRecoveryPath(directory, id);
	if (path.isEmpty() || expected.size() != 32)
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery",
													   "Discard requires a valid recovery ID and its reviewed SHA-256 digest."));
	}
	QByteArray bytes;
	if (!readRecord(path, &bytes, error, control))
	{
		return false;
	}
	if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != expected)
	{
		return fail(error,
					QCoreApplication::translate("ModelAssemblyRecovery", "Recovery changed since review. Refresh before discarding it."));
	}
	if (!modelWorkCheckpoint(control, ModelWorkPhase::Writing, 0, 1, error))
	{
		return false;
	}
	if (!dryRun && (!safeDirectory(directory) || !QFile::remove(path)))
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "Unable to discard the assembly recovery copy."));
	}
	return true;
}
} // namespace

QString modelAssemblyRecoveryDirectory()
{
	const auto profile = StudioSettings::overrideFilePath();
	const auto root =
		profile.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : QFileInfo(profile).absolutePath();
	return QDir(root).filePath(QStringLiteral("assembly-recovery"));
}
QString modelAssemblyRecoveryPath(const QString &directory, const QString &id)
{
	const QUuid uuid(id);
	return uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != id || !safeDirectory(directory)
			   ? QString()
			   : QDir(directory).absoluteFilePath(id + QStringLiteral(".vsassemblyrecovery"));
}
QByteArray modelAssemblyRecoveryFingerprint(const ModelAssemblyRecoverySnapshot &snapshot)
{
	return QCryptographicHash::hash(QJsonDocument(payloadJson(snapshot)).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}
ModelAssemblyRecoveryRecord inspectModelAssemblyRecovery(const QString &path, ModelAssemblyRecoverySnapshot *snapshot,
														 const ModelWorkControl &control)
{
	ModelAssemblyRecoveryRecord record;
	const QFileInfo file(path);
	record.path = file.absoluteFilePath();
	record.id = file.completeBaseName();
	record.bytes = std::max(qint64(0), file.size());
	record.sessionFilePresent = QFileInfo::exists(record.path + QStringLiteral(".active"));
	QByteArray bytes;
	if (!readRecord(path, &bytes, &record.error, control))
	{
		return record;
	}
	record.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	QJsonParseError parseError;
	const auto json = QJsonDocument::fromJson(bytes, &parseError);
	const auto object = json.object();
	QByteArray expected;
	if (parseError.error != QJsonParseError::NoError || !json.isObject() ||
		!keys(object, {"format", "schema", "id", "writtenUtc", "payloadSha256", "payload"}) ||
		object.value("format") != QJsonValue(QStringLiteral("vibestudio-assembly-recovery")) || object.value("schema") != QJsonValue(1) ||
		object.value("id") != QJsonValue(record.id) || !textField(object, "writtenUtc", 64, true) ||
		!textField(object, "payloadSha256", 64, true) || !digest(object.value("payloadSha256").toString(), &expected) ||
		!object.value("payload").isObject())
	{
		record.error = QCoreApplication::translate("ModelAssemblyRecovery", "This is not a supported assembly recovery record.");
		return record;
	}
	record.writtenUtc = QDateTime::fromString(object.value("writtenUtc").toString(), Qt::ISODateWithMs).toUTC();
	const auto payload = object.value("payload").toObject();
	if (!record.writtenUtc.isValid() ||
		QCryptographicHash::hash(QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256) != expected ||
		!keys(payload, {"assembly", "directory", "selectedPart", "seconds", "sourcePath", "sourceSha256"}) ||
		!payload.value("assembly").isObject() || !payload.value("seconds").isDouble() || !textField(payload, "directory", 4096, true) ||
		!textField(payload, "selectedPart", 32) || !textField(payload, "sourcePath", 4096) || !textField(payload, "sourceSha256", 64))
	{
		record.error = QCoreApplication::translate("ModelAssemblyRecovery", "The assembly recovery checksum or context is invalid.");
		return record;
	}
	ModelAssemblyRecoverySnapshot candidate;
	candidate.directory = payload.value("directory").toString();
	candidate.selectedPart = payload.value("selectedPart").toString();
	candidate.seconds = payload.value("seconds").toDouble();
	candidate.sourcePath = payload.value("sourcePath").toString();
	if (!digest(payload.value("sourceSha256").toString(), &candidate.sourceSha256, true))
	{
		record.error = QCoreApplication::translate("ModelAssemblyRecovery", "The original source fingerprint is invalid.");
		return record;
	}
	if (!parseModelAssembly(QJsonDocument(payload.value("assembly").toObject()).toJson(QJsonDocument::Compact), &candidate.assembly,
							&record.error) ||
		!validateSnapshot(candidate, &record.error) ||
		!modelWorkCheckpoint(control, ModelWorkPhase::Reading, bytes.size(), bytes.size(), &record.error))
	{
		return record;
	}
	record.title = candidate.assembly.name;
	record.sourcePath = candidate.sourcePath;
	record.sourceSha256 = candidate.sourceSha256;
	record.parts = candidate.assembly.parts.size();
	record.seconds = candidate.seconds;
	if (snapshot)
	{
		*snapshot = std::move(candidate);
	}
	return record;
}
ModelAssemblyRecoveryScan listModelAssemblyRecoveries(const QString &directory, const ModelWorkControl &control)
{
	return scan(directory, true, control);
}
QJsonObject modelAssemblyRecoveryJson(const ModelAssemblyRecoveryScan &scan)
{
	QJsonArray records;
	for (const auto &record : scan.records)
	{
		records.append(QJsonObject{{"id", record.id},
								   {"path", record.path},
								   {"name", record.title},
								   {"sourcePath", record.sourcePath},
								   {"sha256", QString::fromLatin1(record.sha256.toHex())},
								   {"sourceSha256", QString::fromLatin1(record.sourceSha256.toHex())},
								   {"writtenUtc", record.writtenUtc.toString(Qt::ISODateWithMs)},
								   {"bytes", record.bytes},
								   {"parts", record.parts},
								   {"time", record.seconds},
								   {"valid", record.isValid()},
								   {"sessionFilePresent", record.sessionFilePresent},
								   {"error", record.error}});
	}
	return {{"records", records},
			{"totalBytes", scan.totalBytes},
			{"limited", scan.limited},
			{"error", scan.error},
			{"countLimit", modelAssemblyRecoveryCountLimit},
			{"byteLimit", modelAssemblyRecoveryStorageLimit}};
}
bool restoreModelAssemblyRecovery(const QString &path, const QByteArray &expectedSha256, ModelAssemblyDocument *document,
								  ModelAssemblyRecoverySnapshot *snapshot, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelAssemblyRecoverySnapshot restored;
	const auto record = inspectModelAssemblyRecovery(path, &restored, control);
	if (!record.isValid())
	{
		return fail(error, record.error);
	}
	if (expectedSha256.size() != 32 || record.sha256 != expectedSha256)
	{
		return fail(error,
					QCoreApplication::translate("ModelAssemblyRecovery", "Recovery changed since review. Refresh before restoring it."));
	}
	ModelAssemblyDocument candidate;
	if (!document || !candidate.restoreDraft(restored.assembly, restored.directory, restored.selectedPart, restored.sourcePath, error) ||
		!modelWorkCheckpoint(control, ModelWorkPhase::Reading, record.bytes, record.bytes, error))
	{
		return false;
	}
	*document = std::move(candidate);
	if (snapshot)
	{
		*snapshot = std::move(restored);
	}
	return true;
}
std::shared_ptr<ModelAssemblyRecoverySession> ModelAssemblyRecoverySession::acquire(const QString &directory, const QString &id,
																					QString *error)
{
	if (error)
	{
		error->clear();
	}
	const auto path = modelAssemblyRecoveryPath(directory, id);
	if (path.isEmpty() || !QDir().mkpath(directory) || !safeDirectory(directory))
	{
		fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "The assembly recovery folder is unavailable or unsafe."));
		return {};
	}
	auto session = std::shared_ptr<ModelAssemblyRecoverySession>(new ModelAssemblyRecoverySession);
	session->m_directory = QFileInfo(path).absolutePath();
	session->m_id = id;
	session->m_lease = std::make_unique<QLockFile>(path + QStringLiteral(".active"));
	session->m_lease->setStaleLockTime(0);
	if (!lock(*session->m_lease, path + QStringLiteral(".active"), error))
	{
		return {};
	}
	return session;
}
ModelAssemblyRecoverySession::~ModelAssemblyRecoverySession() = default;
QString ModelAssemblyRecoverySession::path() const { return modelAssemblyRecoveryPath(m_directory, m_id); }
bool ModelAssemblyRecoverySession::write(const ModelAssemblyRecoverySnapshot &snapshot, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	if (!validateSnapshot(snapshot, error) || !modelWorkCheckpoint(control, ModelWorkPhase::Writing, 0, 1, error))
	{
		return false;
	}
	const auto output = path();
	const QFileInfo outputInfo(output);
	if (output.isEmpty() || outputInfo.isSymLink() || outputInfo.isJunction() || (outputInfo.exists() && !outputInfo.isFile()) ||
		modelPathsReferToSameFile(output, snapshot.sourcePath) ||
		std::any_of(snapshot.assembly.parts.cbegin(), snapshot.assembly.parts.cend(),
					[&](const auto &part)
					{
						return part.sourceKind == ModelAssemblySource::File &&
							   modelPathsReferToSameFile(output, QDir(snapshot.directory).absoluteFilePath(part.source));
					}))
	{
		return fail(error,
					QCoreApplication::translate("ModelAssemblyRecovery", "Recovery cannot replace an assembly input or an unsafe path."));
	}
	const auto payload = payloadJson(snapshot);
	const auto payloadBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	const auto bytes =
		QJsonDocument(
			QJsonObject{{"format", QStringLiteral("vibestudio-assembly-recovery")},
						{"schema", 1},
						{"id", m_id},
						{"writtenUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
						{"payload", payload},
						{"payloadSha256", QString::fromLatin1(QCryptographicHash::hash(payloadBytes, QCryptographicHash::Sha256).toHex())}})
			.toJson(QJsonDocument::Compact);
	if (bytes.size() > modelAssemblyRecoveryByteLimit)
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "The assembly recovery record exceeds 2 MiB."));
	}
	const auto lockPath = QDir(m_directory).filePath(QStringLiteral(".inventory.lock"));
	QLockFile folderLock(lockPath);
	// A previous document may still be retiring its copy. This short wait is on
	// the worker, observes cancellation, and never waits on a live editor lease.
	if (!lock(folderLock, lockPath, error, control, 1000))
	{
		return false;
	}
	const auto inventory = scan(m_directory, false, control);
	const auto old =
		std::find_if(inventory.records.cbegin(), inventory.records.cend(), [&](const auto &record) { return record.id == m_id; });
	const auto previousBytes = old == inventory.records.cend() ? 0 : old->bytes;
	if (!inventory.error.isEmpty())
	{
		return fail(error, inventory.error);
	}
	if (inventory.limited || inventory.records.size() + (old == inventory.records.cend() ? 1 : 0) > modelAssemblyRecoveryCountLimit ||
		inventory.totalBytes - previousBytes > modelAssemblyRecoveryStorageLimit - bytes.size())
	{
		return fail(error,
					QCoreApplication::translate(
						"ModelAssemblyRecovery",
						"Recovery storage is full. Save the assembly or review Recoveries to free space. Existing copies were preserved."));
	}
	const auto target = inspectModelWriteTarget(output, control);
	if (!target.isValid())
	{
		return fail(error, target.error);
	}
	if (target.existed && (m_writtenSha256.isEmpty() || target.sha256 != m_writtenSha256))
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery",
													   "The recovery copy changed outside this editor. It was preserved."));
	}
	if (!safeDirectory(m_directory) || !writeModelFile(target, bytes, error, control))
	{
		return false;
	}
	m_writtenSha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return true;
}
bool ModelAssemblyRecoverySession::retire(QString *error)
{
	if (error)
	{
		error->clear();
	}
	const auto output = path();
	if (output.isEmpty())
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "The recovery path is unsafe."));
	}
	if (!QFileInfo::exists(output))
	{
		return true;
	}
	const auto folderPath = QDir(m_directory).filePath(QStringLiteral(".inventory.lock"));
	QLockFile folderLock(folderPath), fileLock(output + QStringLiteral(".vibestudio-model.lock"));
	return lock(folderLock, folderPath, error, {}, 1000) &&
		   lock(fileLock, output + QStringLiteral(".vibestudio-model.lock"), error, {}, 1000) &&
		   removeReviewed(m_directory, m_id, m_writtenSha256, false, error, {});
}
bool discardModelAssemblyRecovery(const QString &directory, const QString &id, const QByteArray &expectedSha256, bool dryRun,
								  QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	const auto path = modelAssemblyRecoveryPath(directory, id);
	if (dryRun)
	{
		return removeReviewed(directory, id, expectedSha256, true, error, control);
	}
	if (path.isEmpty() || !QFileInfo::exists(path))
	{
		return fail(error, QCoreApplication::translate("ModelAssemblyRecovery", "The requested recovery copy is missing or unsafe."));
	}
	const auto session = ModelAssemblyRecoverySession::acquire(directory, id, error);
	if (!session)
	{
		return false;
	}
	const auto folderPath = QDir(directory).filePath(QStringLiteral(".inventory.lock"));
	QLockFile folderLock(folderPath), fileLock(path + QStringLiteral(".vibestudio-model.lock"));
	return lock(folderLock, folderPath, error, control, 1000) &&
		   lock(fileLock, path + QStringLiteral(".vibestudio-model.lock"), error, control, 1000) &&
		   removeReviewed(directory, id, expectedSha256, false, error, control);
}
} // namespace vibestudio
