#include "core/text_recovery.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace vibestudio {
namespace {

constexpr qint64 recoveryByteLimit = 6ll * 1024 * 1024;
const QString suffix = QStringLiteral(".vstextrecovery");

bool validId(const QString& id)
{
	const QUuid uuid(id);
	return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

QString recoveryPath(const QString& directory, const QString& id)
{
	return QDir(directory).absoluteFilePath(id + suffix);
}

bool safeDirectory(const QString& directory)
{
	const QFileInfo info(directory);
	return !directory.isEmpty() && info.isDir() && !info.isSymLink() && !info.isJunction();
}

QByteArray sha256(const QByteArray& bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

} // namespace

QString textRecoveryDirectory()
{
	const QString root = StudioSettings::overrideFilePath().isEmpty()
		? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
		: QFileInfo(StudioSettings::overrideFilePath()).absolutePath();
	return QDir(root).absoluteFilePath(QStringLiteral("code-recovery"));
}

QString writeTextRecovery(const TextRecoverySnapshot& snapshot, const QString& directory, const QString& id,
	QString* error, const std::function<bool()>& cancelled)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return QString(); };
	const auto interrupted = [&]() { return cancelled && cancelled(); };
	if (!validId(id) || directory.isEmpty()) { return fail(QCoreApplication::translate("TextRecovery", "Invalid recovery destination.")); }
	if (interrupted()) { return fail(QCoreApplication::translate("TextRecovery", "Recovery write cancelled.")); }
	QByteArray bytes;
	QString encodingError;
	if (!encodeTextFile(snapshot.source, snapshot.text, &bytes, &encodingError)) { return fail(encodingError); }
	const QFileInfo before(directory);
	if (before.isSymLink() || before.isJunction() || !QDir().mkpath(directory) || !safeDirectory(directory)) {
		return fail(QCoreApplication::translate("TextRecovery", "The recovery folder is unavailable or is a link."));
	}
	const QString root = QFileInfo(directory).canonicalFilePath();
	const QString path = recoveryPath(root, id);
	const auto safeTarget = [&]() {
		const QFileInfo target(path);
		return safeDirectory(directory) && QFileInfo(directory).canonicalFilePath() == root
			&& !target.isSymLink() && !target.isJunction() && (!target.exists() || target.isFile());
	};
	if (!safeTarget()) { return fail(QCoreApplication::translate("TextRecovery", "The recovery file is not a regular file.")); }
	const QJsonObject object {
		{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("id"), id},
		{QStringLiteral("title"), snapshot.title.left(255)},
		{QStringLiteral("sourcePath"), snapshot.source.path.left(4096)},
		{QStringLiteral("sourceSha256"), QString::fromLatin1(snapshot.source.sha256.toHex())},
		{QStringLiteral("writtenUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
		{QStringLiteral("ownerProcessId"), QCoreApplication::applicationPid()},
		{QStringLiteral("payload"), QString::fromLatin1(bytes.toBase64())},
		{QStringLiteral("sha256"), QString::fromLatin1(sha256(bytes).toHex())},
		{QStringLiteral("position"), std::clamp(snapshot.position, 0, static_cast<int>(snapshot.text.size()))},
		{QStringLiteral("anchor"), std::clamp(snapshot.anchor, 0, static_cast<int>(snapshot.text.size()))},
		{QStringLiteral("scroll"), std::max(0, snapshot.scroll)},
		{QStringLiteral("horizontalScroll"), std::max(0, snapshot.horizontalScroll)},
	};
	const QByteArray serialized = QJsonDocument(object).toJson(QJsonDocument::Compact);
	if (serialized.size() > recoveryByteLimit) { return fail(QCoreApplication::translate("TextRecovery", "The recovery record exceeds its size limit.")); }
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(serialized) != serialized.size()) {
		return fail(QCoreApplication::translate("TextRecovery", "The recovery copy could not be written: %1").arg(file.errorString()));
	}
	if (interrupted() || !safeTarget()) {
		file.cancelWriting();
		return fail(QCoreApplication::translate("TextRecovery", "The recovery write was cancelled or its destination changed."));
	}
	if (!file.commit()) { return fail(QCoreApplication::translate("TextRecovery", "The recovery copy could not be committed: %1").arg(file.errorString())); }
	return path;
}

TextRecoveryRecord inspectTextRecovery(const QString& path, TextFileDocument* restored)
{
	if (restored) { *restored = {}; }
	TextRecoveryRecord record;
	record.path = QFileInfo(path).absoluteFilePath();
	const auto fail = [&](const QString& error) { record.error = error; return record; };
	const QFileInfo info(path);
	QFile file(path);
	if (info.isSymLink() || info.isJunction() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
		return fail(QCoreApplication::translate("TextRecovery", "The recovery record is not a readable regular file."));
	}
	const QByteArray bytes = file.read(recoveryByteLimit + 1);
	if (file.error() != QFileDevice::NoError || bytes.size() > recoveryByteLimit) {
		return fail(QCoreApplication::translate("TextRecovery", "The recovery record could not be read within its size limit."));
	}
	QJsonParseError parse;
	const auto document = QJsonDocument::fromJson(bytes, &parse);
	const auto object = document.object();
	record.id = object.value(QStringLiteral("id")).toString();
	if (parse.error != QJsonParseError::NoError || !document.isObject()
		|| object.value(QStringLiteral("schemaVersion")).toInt() != 1 || !validId(record.id)) {
		return fail(QCoreApplication::translate("TextRecovery", "The recovery record has an unsupported or damaged header."));
	}
	const QByteArray encoded = object.value(QStringLiteral("payload")).toString().toLatin1();
	const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded || decoded.decoded.toBase64() != encoded || decoded.decoded.size() > textDocumentByteLimit
		|| object.value(QStringLiteral("sha256")).toString().toLatin1() != sha256(decoded.decoded).toHex()) {
		return fail(QCoreApplication::translate("TextRecovery", "The recovery contents failed their checksum or size check."));
	}
	auto text = decodeTextFile(decoded.decoded);
	if (!text.editable()) { return fail(text.error); }
	record.title = object.value(QStringLiteral("title")).toString().left(255);
	record.sourcePath = object.value(QStringLiteral("sourcePath")).toString().left(4096);
	record.sourceSha256 = QByteArray::fromHex(object.value(QStringLiteral("sourceSha256")).toString().toLatin1());
	record.writtenUtc = QDateTime::fromString(object.value(QStringLiteral("writtenUtc")).toString(), Qt::ISODateWithMs).toUTC();
	if (!record.writtenUtc.isValid()) { return fail(QCoreApplication::translate("TextRecovery", "The recovery timestamp is invalid.")); }
	record.ownerProcessId = object.value(QStringLiteral("ownerProcessId")).toInteger();
	record.payloadBytes = decoded.decoded.size();
	record.position = std::clamp(object.value(QStringLiteral("position")).toInt(), 0, static_cast<int>(text.text.size()));
	record.anchor = std::clamp(object.value(QStringLiteral("anchor")).toInt(), 0, static_cast<int>(text.text.size()));
	record.scroll = std::max(0, object.value(QStringLiteral("scroll")).toInt());
	record.horizontalScroll = std::max(0, object.value(QStringLiteral("horizontalScroll")).toInt());
	if (restored) { *restored = std::move(text); }
	return record;
}

TextRecoveryScan listTextRecoveries(const QString& directory, const std::function<bool()>& cancelled)
{
	TextRecoveryScan scan;
	if (!QFileInfo::exists(directory)) { return scan; }
	if (!safeDirectory(directory)) { scan.error = QCoreApplication::translate("TextRecovery", "The recovery folder is not a regular directory."); return scan; }
	QDirIterator iterator(directory, QStringList {QStringLiteral("*") + suffix}, QDir::Files | QDir::System | QDir::NoDotAndDotDot);
	qint64 totalBytes = 0;
	while (iterator.hasNext()) {
		if ((cancelled && cancelled()) || scan.records.size() >= 128 || totalBytes >= 64ll * 1024 * 1024) { scan.limited = true; break; }
		const QString path = iterator.next();
		const qint64 bytes = std::min(recoveryByteLimit + 1, std::max<qint64>(0, iterator.fileInfo().size()));
		if (totalBytes + bytes > 64ll * 1024 * 1024) { scan.limited = true; break; }
		totalBytes += bytes;
		scan.records.push_back(inspectTextRecovery(path));
	}
	std::sort(scan.records.begin(), scan.records.end(), [](const auto& first, const auto& second) { return first.writtenUtc > second.writtenUtc; });
	return scan;
}

bool removeTextRecovery(const QString& directory, const QString& id, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [&](const QString& message) { if (error) { *error = message; } return false; };
	if (!validId(id) || directory.isEmpty()) { return fail(QCoreApplication::translate("TextRecovery", "Invalid recovery identifier.")); }
	if (!QFileInfo::exists(directory)) { return true; }
	if (!safeDirectory(directory)) { return fail(QCoreApplication::translate("TextRecovery", "The recovery folder is not a regular directory.")); }
	const QString path = recoveryPath(directory, id);
	const QFileInfo info(path);
	if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isFile())) {
		return fail(QCoreApplication::translate("TextRecovery", "Only regular recovery files may be discarded."));
	}
	if (!info.exists() || QFile::remove(path)) { return true; }
	return fail(QCoreApplication::translate("TextRecovery", "The recovery copy could not be discarded."));
}

} // namespace vibestudio
