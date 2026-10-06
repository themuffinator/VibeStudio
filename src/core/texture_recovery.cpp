#include "core/texture_recovery.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {
const QByteArray magic = QByteArrayLiteral("VSTREC\x1a\n");
constexpr int headerSize = 24;
constexpr quint32 metadataLimit = 64 * 1024;
constexpr int recordLimit = 256;
constexpr int enumerationLimit = 8192;

bool fail(QString* error, const QString& message) { if (error) { *error = message; } return false; }
bool validId(const QString& id)
{
	static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
	return pattern.match(id).hasMatch();
}
bool checkpoint(const TextureProgress& progress, qint64 done, qint64 total, QString* error)
{
	return !progress || progress(done, total) || fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Texture recovery operation cancelled."));
}
bool jsonInteger(const QJsonObject& object, const QString& key, int minimum, int maximum, int* result)
{
	const auto field = object.value(key); const double value = field.toDouble(-1);
	if (!field.isDouble() || !std::isfinite(value) || std::floor(value) != value || value < minimum || value > maximum) { return false; }
	*result = int(value); return true;
}

TextureRecoveryInfo readRecord(const QString& path, QByteArray* payload, const TextureProgress& progress)
{
	TextureRecoveryInfo record; const QFileInfo fileInfo(path); record.path = fileInfo.absoluteFilePath();
	QFile file(record.path);
	const auto invalid = [&](const QString& text) { record.error = text; return record; };
	if (!fileInfo.isFile() || fileInfo.isSymLink() || !file.open(QIODevice::ReadOnly) ||
		file.size() < headerSize + 32 || file.size() > textureProjectFileLimit + metadataLimit + headerSize + 32) {
		return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "Not a bounded regular texture recovery file."));
	}
	const QByteArray header = file.read(headerSize);
	if (header.size() != headerSize || !header.startsWith(magic) || qFromLittleEndian<quint32>(header.constData() + 8) != 1) {
		return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "The texture recovery header or version is unsupported."));
	}
	const auto jsonBytes = qFromLittleEndian<quint32>(header.constData() + 12);
	const auto payloadBytes = qFromLittleEndian<quint64>(header.constData() + 16);
	if (jsonBytes == 0 || jsonBytes > metadataLimit || payloadBytes < 48 || payloadBytes > quint64(textureProjectFileLimit) ||
		quint64(file.size()) != headerSize + quint64(jsonBytes) + payloadBytes + 32) {
		return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "Texture recovery lengths exceed their bounds or do not match the file."));
	}
	const QByteArray json = file.read(jsonBytes); QJsonParseError parseError;
	const auto parsed = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) { return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "Texture recovery metadata is malformed.")); }
	const auto metadata = parsed.object();
	record.id = metadata.value(QStringLiteral("id")).toString(); record.displayName = metadata.value(QStringLiteral("displayName")).toString();
	record.sourcePath = metadata.value(QStringLiteral("sourcePath")).toString(); record.payloadBytes = qint64(payloadBytes);
	const auto hash = metadata.value(QStringLiteral("sourceSha256")).toString();
	const auto revision = metadata.value(QStringLiteral("revision")).toString(); bool revisionOk = false;
	record.revision = revision.toULongLong(&revisionOk);
	const auto written = metadata.value(QStringLiteral("writtenUtc")).toString(); record.writtenUtc = QDateTime::fromString(written, Qt::ISODateWithMs);
	int width = 0, height = 0;
	static const QRegularExpression hashPattern(QStringLiteral("^(?:[0-9a-f]{64})?$"));
	static const QRegularExpression revisionPattern(QStringLiteral("^[0-9]{1,20}$"));
	if (!validId(record.id) || !metadata.value(QStringLiteral("displayName")).isString() || record.displayName.size() > 4096 || record.displayName.contains(QChar::Null) ||
		!metadata.value(QStringLiteral("sourcePath")).isString() || record.sourcePath.size() > 32768 || record.sourcePath.contains(QChar::Null) ||
		!metadata.value(QStringLiteral("sourceSha256")).isString() || !hashPattern.match(hash).hasMatch() ||
		!revisionOk || !revisionPattern.match(revision).hasMatch() || !record.writtenUtc.isValid() || !written.endsWith(QLatin1Char('Z')) ||
		!jsonInteger(metadata, QStringLiteral("width"), 1, TextureDocument::MaximumDimension, &width) ||
		!jsonInteger(metadata, QStringLiteral("height"), 1, TextureDocument::MaximumDimension, &height) ||
		!jsonInteger(metadata, QStringLiteral("layerCount"), 1, TextureDocument::MaximumLayers, &record.layerCount) ||
		!validTextureSize({width, height}) || qint64(width) * height * record.layerCount > TextureDocument::MaximumLayerPixels) {
		return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "Texture recovery identity, dimensions, timestamp, or source metadata is invalid."));
	}
	record.size = {width, height}; record.sourceSha256 = QByteArray::fromHex(hash.toLatin1());
	QCryptographicHash digest(QCryptographicHash::Sha256); digest.addData(header); digest.addData(json);
	if (payload) { payload->clear(); payload->reserve(record.payloadBytes); }
	qint64 remaining = record.payloadBytes;
	while (remaining > 0) {
		if (!checkpoint(progress, record.payloadBytes - remaining, record.payloadBytes, &record.error)) { return record; }
		const auto chunk = file.read(std::min<qint64>(remaining, 64 * 1024));
		if (chunk.isEmpty() || file.error() != QFile::NoError) { return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "The recovery payload is truncated or unreadable.")); }
		if (remaining == record.payloadBytes && (chunk.size() < 16 || !chunk.startsWith(QByteArrayLiteral("VSTEX\r\n\x1a")) || qFromLittleEndian<quint32>(chunk.constData() + 8) != 1)) {
			return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "The embedded texture project version is unsupported."));
		}
		digest.addData(chunk); if (payload) { payload->append(chunk); } remaining -= chunk.size();
	}
	record.recordSha256 = digest.result();
	if (file.read(32) != record.recordSha256 || !file.atEnd()) { return invalid(QCoreApplication::translate("VibeStudioTextureRecovery", "Texture recovery checksum verification failed.")); }
	if (!checkpoint(progress, record.payloadBytes, record.payloadBytes, &record.error)) { return record; }
	return record;
}
}

QString textureRecoveryPath(const QString& directory, const QString& id)
{
	return !directory.isEmpty() && validId(id) ? QDir(QFileInfo(directory).absoluteFilePath()).filePath(id + QStringLiteral(".vtrecovery")) : QString();
}

QString writeTextureRecovery(const TextureRecoverySnapshot& snapshot, const QString& directory, const QString& id, QString* error, const TextureProgress& progress)
{
	if (error) { error->clear(); }
	const QString target = textureRecoveryPath(directory, id); const QFileInfo folder(directory);
	if (snapshot.document.strokeActive()) { fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Finish the current texture stroke before taking a checkpoint.")); return {}; }
	if (target.isEmpty() || folder.isSymLink() || (folder.exists() && !folder.isDir()) || snapshot.displayName.size() > 4096 ||
		snapshot.source.path.size() > 32768 || snapshot.source.path.contains(QChar::Null) || snapshot.displayName.contains(QChar::Null) ||
		(!snapshot.source.sha256.isEmpty() && snapshot.source.sha256.size() != 32)) {
		fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Choose a regular recovery folder, a UUID, and bounded source metadata.")); return {};
	}
	const auto payload = encodeTextureProject(snapshot.document, snapshot.metadata, error, [&](qint64 done, qint64 total) {
		return checkpoint(progress, total > 0 ? done * 500 / total : 0, 1000, error);
	});
	if (payload.isEmpty()) { return {}; }
	QJsonObject metadata{{QStringLiteral("id"), id}, {QStringLiteral("displayName"), snapshot.displayName},
		{QStringLiteral("sourcePath"), snapshot.source.path}, {QStringLiteral("sourceSha256"), QString::fromLatin1(snapshot.source.sha256.toHex())},
		{QStringLiteral("writtenUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
		{QStringLiteral("revision"), QString::number(snapshot.document.revision())}, {QStringLiteral("width"), snapshot.document.size().width()},
		{QStringLiteral("height"), snapshot.document.size().height()}, {QStringLiteral("layerCount"), snapshot.document.layers().size()}};
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	if (json.size() > metadataLimit) { fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Texture recovery metadata exceeds 64 KiB.")); return {}; }
	if (!checkpoint(progress, 500, 1000, error)) { return {}; }
	if (!QDir().mkpath(folder.absoluteFilePath()) || QFileInfo(directory).isSymLink()) { fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Unable to create a regular texture recovery directory.")); return {}; }
	const QString canonicalDirectory = QFileInfo(directory).canonicalFilePath();
	const QString path = QDir(canonicalDirectory).filePath(QFileInfo(target).fileName()); const QFileInfo before(path);
	if (canonicalDirectory.isEmpty() || before.isSymLink() || (before.exists() && !before.isFile())) {
		fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "The recovery checkpoint is not a regular file.")); return {};
	}
	QByteArray header(headerSize, '\0'); header.replace(0, magic.size(), magic);
	qToLittleEndian<quint32>(1, header.data() + 8); qToLittleEndian<quint32>(quint32(json.size()), header.data() + 12);
	qToLittleEndian<quint64>(quint64(payload.size()), header.data() + 16);
	QSaveFile output(path); QCryptographicHash digest(QCryptographicHash::Sha256);
	if (!output.open(QIODevice::WriteOnly) || output.write(header) != header.size() || output.write(json) != json.size()) { fail(error, output.errorString()); return {}; }
	digest.addData(header); digest.addData(json);
	for (qsizetype offset = 0; offset < payload.size();) {
		if (!checkpoint(progress, 500 + offset * 500 / payload.size(), 1000, error)) { output.cancelWriting(); return {}; }
		const auto count = std::min<qsizetype>(64 * 1024, payload.size() - offset); const QByteArrayView chunk(payload.constData() + offset, count);
		if (output.write(chunk.data(), chunk.size()) != chunk.size()) { fail(error, output.errorString()); return {}; }
		digest.addData(chunk); offset += count;
	}
	if (!checkpoint(progress, 1000, 1000, error)) { output.cancelWriting(); return {}; }
	if (QFileInfo(directory).canonicalFilePath() != canonicalDirectory || QFileInfo(path).isSymLink()) { output.cancelWriting(); fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "The recovery destination changed during writing.")); return {}; }
	if (output.write(digest.result()) != 32 || !output.commit()) { fail(error, output.errorString()); return {}; }
	return path;
}

TextureRecoveryInfo inspectTextureRecovery(const QString& path, const TextureProgress& progress) { return readRecord(path, nullptr, progress); }

TextureRecoveryList listTextureRecoveries(const QString& directory, const TextureProgress& progress)
{
	TextureRecoveryList result;
	if (!QFileInfo::exists(directory)) { return result; }
	if (!QFileInfo(directory).isDir() || QFileInfo(directory).isSymLink()) { result.error = QCoreApplication::translate("VibeStudioTextureRecovery", "Choose a regular texture recovery directory."); return result; }
	QVector<QFileInfo> files;
	QDirIterator iterator(directory, {QStringLiteral("*.vtrecovery")}, QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot);
	while (iterator.hasNext()) {
		if (files.size() >= enumerationLimit) { result.truncated = true; break; }
		if (!checkpoint(progress, 0, 1, &result.error)) { result.cancelled = true; return result; }
		iterator.next(); files.append(iterator.fileInfo());
	}
	std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() > b.lastModified(); });
	if (files.size() > recordLimit) { result.truncated = true; files.resize(recordLimit); }
	for (int i = 0; i < files.size(); ++i) {
		bool cancelled = false;
		auto info = inspectTextureRecovery(files[i].absoluteFilePath(), [&](qint64 done, qint64 total) {
			cancelled = !checkpoint(progress, i * 1000 + (total > 0 ? done * 1000 / total : 0), files.size() * 1000, &result.error); return !cancelled;
		});
		if (cancelled) { result.cancelled = true; return result; }
		result.records.append(std::move(info));
	}
	return result;
}

bool restoreTextureRecovery(const QString& path, TextureDocument* document, QJsonObject* metadata, TextureRecoveryInfo* info, QString* error,
	const QByteArray& expectedRecordSha256, const TextureProgress& progress)
{
	if (error) { error->clear(); }
	QByteArray payload; const auto record = readRecord(path, &payload, [&](qint64 done, qint64 total) { return checkpoint(progress, total > 0 ? done * 500 / total : 0, 1000, error); });
	if (!document || !record.isValid()) { return fail(error, record.error.isEmpty() ? QCoreApplication::translate("VibeStudioTextureRecovery", "A texture document is required for restoration.") : record.error); }
	if (!expectedRecordSha256.isEmpty() && expectedRecordSha256 != record.recordSha256) { return fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "The selected checkpoint changed. Inspect it again before restoring.")); }
	TextureDocument restored; QJsonObject values;
	if (!decodeTextureProject(payload, &restored, &values, error, [&](qint64 done, qint64 total) { return checkpoint(progress, 500 + (total > 0 ? done * 500 / total : 0), 1000, error); })) { return false; }
	if (restored.size() != record.size || restored.layers().size() != record.layerCount) { return fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Recovery summary and project dimensions do not agree.")); }
	if (!checkpoint(progress, 1, 1, error)) { return false; }
	restored.markUnsaved(); *document = std::move(restored); if (metadata) { *metadata = values; } if (info) { *info = record; }
	return true;
}

bool removeTextureRecovery(const QString& directory, const QString& id, QString* error)
{
	const auto path = textureRecoveryPath(directory, id); const QFileInfo folder(directory), file(path);
	if (path.isEmpty() || folder.isSymLink() || file.isSymLink() || (file.exists() && !file.isFile())) { return fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Only an owned regular texture checkpoint can be removed.")); }
	if (!file.exists()) { return true; }
	if (file.canonicalPath() != folder.canonicalFilePath()) { return fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "The texture checkpoint is outside its recovery directory.")); }
	return QFile::remove(file.absoluteFilePath()) || fail(error, QCoreApplication::translate("VibeStudioTextureRecovery", "Unable to remove the retired texture checkpoint."));
}

} // namespace vibestudio
