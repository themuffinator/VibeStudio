#include "core/model_recovery.h"
#include "core/model_surface_selection.h"
#include "core/model_collision.h"
#include "core/model_tags.h"
#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
constexpr qint64 headerLimit = 64 * 1024;
constexpr qint64 chunkSize = 256 * 1024;
const QByteArray magic = QByteArrayLiteral("VSMREC1\n");
const QString suffix = QStringLiteral(".vsmeshrecovery");

bool fail(QString *error, const QString &text)
{
	if (error)
	{
		*error = text;
	}
	return false;
}
bool validId(const QString &id)
{
	const QUuid value(id);
	return !value.isNull() && value.toString(QUuid::WithoutBraces) == id;
}
bool safeDirectory(const QString &path)
{
	const QFileInfo info(path);
	return !path.isEmpty() && info.isDir() && !info.isSymLink() && !info.isJunction();
}
QByteArray hash(const QByteArray &value) { return QCryptographicHash::hash(value, QCryptographicHash::Sha256); }
bool integer(const QJsonValue &value, qint64 low, qint64 high, qint64 *output)
{
	if (!value.isDouble())
	{
		return false;
	}
	const double number = value.toDouble();
	if (!std::isfinite(number) || number < low || number > high || std::floor(number) != number)
	{
		return false;
	}
	*output = qint64(number);
	return true;
}
bool interrupted(const std::function<bool()> &cancelled, QString *error)
{
	if (!cancelled || !cancelled())
	{
		return false;
	}
	fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "Model recovery cancelled."));
	return true;
}
QJsonArray indices(const QSet<int> &selection)
{
	auto values = selection.values();
	std::sort(values.begin(), values.end());
	QJsonArray result;
	for (int index : values)
	{
		result.append(index);
	}
	return result;
}
bool readIndices(const QJsonValue &value, int limit, QSet<int> *output)
{
	if (!value.isArray() || value.toArray().size() > limit)
	{
		return false;
	}
	QSet<int> result;
	for (const auto &item : value.toArray())
	{
		qint64 index = 0;
		if (!integer(item, 0, limit - 1, &index) || result.contains(int(index)))
		{
			return false;
		}
		result.insert(int(index));
	}
	*output = std::move(result);
	return true;
}
QJsonArray edgeIndices(const QSet<ModelEdge> &edges)
{
	auto values = edges.values();
	std::sort(values.begin(), values.end());
	QJsonArray result;
	for (auto edge : values)
	{
		result.append(QJsonArray{edge.first, edge.second});
	}
	return result;
}
bool readEdges(const QJsonValue &value, const ModelSurface &surface, QSet<ModelEdge> *output)
{
	if (value.isUndefined())
	{
		output->clear();
		return true;
	} // Older v1 recovery copies.
	if (!value.isArray() || value.toArray().size() > surface.triangles.size() * 3)
	{
		return false;
	}
	const auto available = modelSurfaceEdges(surface);
	QSet<ModelEdge> result;
	for (const auto &item : value.toArray())
	{
		if (!item.isArray() || item.toArray().size() != 2)
		{
			return false;
		}
		const auto pair = item.toArray();
		qint64 a = 0, b = 0;
		if (!integer(pair[0], 0, surface.vertexCount - 1, &a) || !integer(pair[1], 0, surface.vertexCount - 1, &b))
		{
			return false;
		}
		const ModelEdge edge{int(a), int(b)};
		if (!std::binary_search(available.cbegin(), available.cend(), edge) || result.contains(edge))
		{
			return false;
		}
		result.insert(edge);
	}
	*output = std::move(result);
	return true;
}
bool validSelection(const ModelRecoverySnapshot &snapshot)
{
	if (!validModelSurfaceSelection(snapshot.mesh, snapshot.selection) || !validModelTagSelection(snapshot.mesh, snapshot.selection) || !validModelCollisionSelection(snapshot.mesh, snapshot.selection))
	{
		return false;
	}
	if (snapshot.selection.surface < 0 || snapshot.selection.surface >= snapshot.mesh.surfaces.size() || snapshot.frame < 0 ||
		snapshot.frame >= snapshot.mesh.frames.size())
	{
		return false;
	}
	const auto &surface = snapshot.mesh.surfaces[snapshot.selection.surface];
	for (int index : snapshot.selection.vertices)
	{
		if (index < 0 || index >= surface.texCoords.size())
		{
			return false;
		}
	}
	for (int index : snapshot.selection.faces)
	{
		if (index < 0 || index >= surface.triangles.size())
		{
			return false;
		}
	}
	if (!snapshot.selection.edges.isEmpty())
	{
		const auto available = modelSurfaceEdges(surface);
		for (auto edge : snapshot.selection.edges)
		{
			if (!std::binary_search(available.cbegin(), available.cend(), edge))
			{
				return false;
			}
		}
	}
	return true;
}
bool readPayload(QFile &file, qint64 size, QByteArray *result, QString *error, const std::function<bool()> &cancelled)
{
	QByteArray bytes;
	bytes.reserve(size);
	while (bytes.size() < size)
	{
		if (interrupted(cancelled, error))
		{
			return false;
		}
		const auto part = file.read(std::min(chunkSize, size - bytes.size()));
		if (part.isEmpty())
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery copy is incomplete or unreadable."));
		}
		bytes.append(part);
	}
	if (file.error() != QFileDevice::NoError)
	{
		return fail(error, file.errorString());
	}
	*result = std::move(bytes);
	return true;
}
} // namespace

QString modelRecoveryDirectory()
{
	const auto override = StudioSettings::overrideFilePath();
	const auto root =
		override.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) : QFileInfo(override).absolutePath();
	return QDir(root).absoluteFilePath(QStringLiteral("model-recovery"));
}

QString modelRecoveryPath(const QString &directory, const QString &id)
{
	return directory.isEmpty() || !validId(id) ? QString() : QDir(directory).absoluteFilePath(id + suffix);
}

QString writeModelRecovery(const ModelRecoverySnapshot &snapshot, const QString &directory, const QString &id, QString *error,
						   const std::function<bool()> &cancelled)
{
	if (error)
	{
		error->clear();
	}
	const auto reject = [&](const QString &text)
	{
		fail(error, text);
		return QString();
	};
	if (!validId(id) || directory.isEmpty())
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "Invalid model recovery destination."));
	}
	if (interrupted(cancelled, error))
	{
		return {};
	}
	const ModelWorkControl control{cancelled, {}};
	const auto validation = validateEditableModel(snapshot.mesh, control);
	if (!validation.isEmpty())
	{
		return reject(validation.join(QLatin1Char('\n')));
	}
	if (!validSelection(snapshot) || (!snapshot.sourceSha256.isEmpty() && snapshot.sourceSha256.size() != 32))
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery context is invalid."));
	}
	auto object = editableModelJson(snapshot.mesh, error, control);
	if (object.isEmpty())
	{
		return {};
	}
	object.insert(QStringLiteral("recoverySelection"), QJsonObject{{QStringLiteral("surface"), snapshot.selection.surface},
		{QStringLiteral("surfaces"), indices(snapshot.selection.surfaces)},
																   {QStringLiteral("vertices"), indices(snapshot.selection.vertices)},
																   {QStringLiteral("faces"), indices(snapshot.selection.faces)},
																   {QStringLiteral("edges"), edgeIndices(snapshot.selection.edges)},
																   {QStringLiteral("tag"), snapshot.selection.tag},
																   {QStringLiteral("collision"), snapshot.selection.collision}});
	const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
	if (payload.size() > modelDocumentMaxSourceBytes)
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery copy exceeds 64 MiB."));
	}
	if (interrupted(cancelled, error))
	{
		return {};
	}
	const QJsonObject metadata{{QStringLiteral("version"), 1},
							   {QStringLiteral("id"), id},
							   {QStringLiteral("title"), snapshot.title.left(255)},
							   {QStringLiteral("sourcePath"), snapshot.sourcePath.left(4096)},
							   {QStringLiteral("sourceSha256"), QString::fromLatin1(snapshot.sourceSha256.toHex())},
							   {QStringLiteral("payloadSha256"), QString::fromLatin1(hash(payload).toHex())},
							   {QStringLiteral("payloadBytes"), payload.size()},
							   {QStringLiteral("writtenUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
							   {QStringLiteral("ownerProcessId"), QCoreApplication::applicationPid()},
							   {QStringLiteral("frame"), snapshot.frame}};
	const auto header = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	if (header.size() > headerLimit)
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery header exceeds its limit."));
	}
	const QFileInfo before(directory);
	if (before.isSymLink() || before.isJunction() || !QDir().mkpath(directory) || !safeDirectory(directory))
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery folder is unavailable or is a link."));
	}
	const auto root = QFileInfo(directory).canonicalFilePath();
	const auto path = modelRecoveryPath(root, id);
	const auto safeTarget = [&]()
	{
		const QFileInfo info(path);
		return safeDirectory(directory) && QFileInfo(directory).canonicalFilePath() == root && !info.isSymLink() && !info.isJunction() &&
			   (!info.exists() || info.isFile());
	};
	if (!safeTarget())
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery target is not a regular file."));
	}
	QSaveFile file(path);
	const quint32 length = qToLittleEndian(quint32(header.size()));
	if (!file.open(QIODevice::WriteOnly) || file.write(magic) != magic.size() ||
		file.write(reinterpret_cast<const char *>(&length), sizeof(length)) != sizeof(length) || file.write(header) != header.size())
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery header could not be written."));
	}
	for (qsizetype offset = 0; offset < payload.size(); offset += chunkSize)
	{
		if (interrupted(cancelled, error))
		{
			file.cancelWriting();
			return {};
		}
		const auto size = std::min<qsizetype>(chunkSize, payload.size() - offset);
		if (file.write(payload.constData() + offset, size) != size)
		{
			return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery copy could not be written."));
		}
	}
	if (interrupted(cancelled, error) || !safeTarget())
	{
		file.cancelWriting();
		return reject(
			QCoreApplication::translate("VibeStudioModelRecovery", "The recovery write was cancelled or its destination changed."));
	}
	if (!file.commit())
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery copy could not be committed."));
	}
	return path;
}

ModelRecoveryRecord inspectModelRecovery(const QString &path, ModelRecoverySnapshot *restored, const std::function<bool()> &cancelled)
{
	ModelRecoveryRecord record;
	record.path = QFileInfo(path).absoluteFilePath();
	const auto reject = [&](const QString &text)
	{
		record.error = text;
		return record;
	};
	if (interrupted(cancelled, &record.error))
	{
		return record;
	}
	const QFileInfo info(path);
	QFile file(path);
	if (info.isSymLink() || info.isJunction() || !info.isFile() || !file.open(QIODevice::ReadOnly))
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery copy is not a readable regular file."));
	}
	if (file.read(magic.size()) != magic)
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery signature is invalid."));
	}
	const auto lengthBytes = file.read(sizeof(quint32));
	if (lengthBytes.size() != sizeof(quint32))
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery header is incomplete."));
	}
	const quint32 length = qFromLittleEndian<quint32>(lengthBytes.constData());
	if (length == 0 || length > headerLimit)
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery header exceeds its limit."));
	}
	const auto bytes = file.read(length);
	QJsonParseError parse;
	const auto json = QJsonDocument::fromJson(bytes, &parse);
	const auto header = json.object();
	record.id = header.value(QStringLiteral("id")).toString();
	qint64 version = 0, frame = 0;
	if (bytes.size() != length || parse.error != QJsonParseError::NoError || !json.isObject() ||
		!integer(header.value(QStringLiteral("version")), 1, 1, &version) || !validId(record.id) || info.fileName() != record.id + suffix ||
		!integer(header.value(QStringLiteral("frame")), 0, modelDocumentMaxFrames - 1, &frame) ||
		!integer(header.value(QStringLiteral("payloadBytes")), 1, modelDocumentMaxSourceBytes, &record.payloadBytes) ||
		!integer(header.value(QStringLiteral("ownerProcessId")), 1, 9007199254740991LL, &record.ownerProcessId) ||
		file.size() != magic.size() + qint64(sizeof(quint32)) + length + record.payloadBytes)
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery header is damaged or unsupported."));
	}
	record.frame = int(frame);
	record.title = header.value(QStringLiteral("title")).toString().left(255);
	record.sourcePath = header.value(QStringLiteral("sourcePath")).toString().left(4096);
	const auto sourceHash = header.value(QStringLiteral("sourceSha256")).toString().toLatin1();
	const auto payloadHash = header.value(QStringLiteral("payloadSha256")).toString().toLatin1();
	record.sourceSha256 = QByteArray::fromHex(sourceHash);
	record.payloadSha256 = QByteArray::fromHex(payloadHash);
	record.writtenUtc = QDateTime::fromString(header.value(QStringLiteral("writtenUtc")).toString(), Qt::ISODateWithMs).toUTC();
	if (record.sourceSha256.toHex() != sourceHash || (!sourceHash.isEmpty() && sourceHash.size() != 64) ||
		record.payloadSha256.toHex() != payloadHash || payloadHash.size() != 64 || !record.writtenUtc.isValid())
	{
		return reject(
			QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery timestamp or checksum header is invalid."));
	}
	if (!restored)
	{
		return record;
	}
	QByteArray payload;
	if (!readPayload(file, record.payloadBytes, &payload, &record.error, cancelled))
	{
		return record;
	}
	if (hash(payload) != record.payloadSha256)
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery contents failed their checksum."));
	}
	if (interrupted(cancelled, &record.error))
	{
		return record;
	}
	ModelRecoverySnapshot candidate;
	if (!parseEditableModel(payload, &candidate.mesh, &record.error, {cancelled, {}}))
	{
		return record;
	}
	const auto selection = QJsonDocument::fromJson(payload).object().value(QStringLiteral("recoverySelection")).toObject();
	qint64 surface = 0;
	if (!integer(selection.value(QStringLiteral("surface")), 0, candidate.mesh.surfaces.size() - 1, &surface))
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The recovered component selection is invalid."));
	}
	candidate.selection.surface = int(surface);
	if (selection.contains(QStringLiteral("tag")) && !selection.value(QStringLiteral("tag")).isString())
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The recovered attachment selection is invalid."));
	}
	candidate.selection.tag = selection.value(QStringLiteral("tag")).toString();
	if (selection.contains(QStringLiteral("collision")) && !selection.value(QStringLiteral("collision")).isString())
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The recovered collision selection is invalid."));
	}
	candidate.selection.collision = selection.value(QStringLiteral("collision")).toString();
	const auto &geometry = candidate.mesh.surfaces.at(surface);
	if ((selection.contains(QStringLiteral("surfaces")) &&
		 !readIndices(selection.value(QStringLiteral("surfaces")), candidate.mesh.surfaces.size(), &candidate.selection.surfaces)) ||
		!validModelSurfaceSelection(candidate.mesh, candidate.selection) ||
		!readIndices(selection.value(QStringLiteral("vertices")), geometry.texCoords.size(), &candidate.selection.vertices) ||
		!readIndices(selection.value(QStringLiteral("faces")), geometry.triangles.size(), &candidate.selection.faces) ||
		!readEdges(selection.value(QStringLiteral("edges")), geometry, &candidate.selection.edges) ||
		record.frame >= candidate.mesh.frames.size() || !validModelSurfaceSelection(candidate.mesh, candidate.selection) || !validModelTagSelection(candidate.mesh, candidate.selection) ||
		!validModelCollisionSelection(candidate.mesh, candidate.selection))
	{
		return reject(QCoreApplication::translate("VibeStudioModelRecovery", "The recovered component selection is invalid."));
	}
	candidate.frame = record.frame;
	candidate.title = record.title;
	candidate.sourcePath = record.sourcePath;
	candidate.sourceSha256 = record.sourceSha256;
	if (interrupted(cancelled, &record.error))
	{
		return record;
	}
	*restored = std::move(candidate);
	return record;
}

ModelRecoveryScan listModelRecoveries(const QString &directory, const std::function<bool()> &cancelled)
{
	ModelRecoveryScan result;
	if (!QFileInfo::exists(directory))
	{
		return result;
	}
	if (!safeDirectory(directory))
	{
		result.error = QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery folder is unavailable or is a link.");
		return result;
	}
	QDirIterator entries(directory, {QStringLiteral("*") + suffix}, QDir::Files | QDir::System | QDir::NoDotAndDotDot);
	while (entries.hasNext())
	{
		if (result.records.size() >= 128 || interrupted(cancelled, &result.error))
		{
			result.limited = true;
			break;
		}
		result.records.append(inspectModelRecovery(entries.next(), nullptr, cancelled));
	}
	std::sort(result.records.begin(), result.records.end(),
			  [](const auto &a, const auto &b) { return a.writtenUtc == b.writtenUtc ? a.path < b.path : a.writtenUtc > b.writtenUtc; });
	return result;
}

bool removeModelRecovery(const QString &directory, const QString &id, QString *error)
{
	if (error)
	{
		error->clear();
	}
	if (!validId(id) || directory.isEmpty())
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "Invalid model recovery identifier."));
	}
	if (!QFileInfo::exists(directory))
	{
		return true;
	}
	if (!safeDirectory(directory))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery folder is unavailable or is a link."));
	}
	const auto root = QFileInfo(directory).canonicalFilePath();
	const auto path = modelRecoveryPath(root, id);
	const QFileInfo info(path);
	if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isFile()))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "Only regular model recovery files may be discarded."));
	}
	if (!info.exists())
	{
		return true;
	}
	if (!safeDirectory(directory) || QFileInfo(directory).canonicalFilePath() != root || !QFile::remove(path))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelRecovery", "The model recovery copy could not be discarded."));
	}
	return true;
}

} // namespace vibestudio
