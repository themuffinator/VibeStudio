#include "core/level_document.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QtEndian>

#include <algorithm>

namespace vibestudio
{
namespace
{

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

bool samePath(const QString& left, const QString& right)
{
	if (left.isEmpty() || right.isEmpty()) {
		return false;
	}
	const QFileInfo a(left), b(right);
	const auto normalized = [](const QFileInfo& info) {
		return QDir::cleanPath(info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath());
	};
#ifdef Q_OS_WIN
	return normalized(a).compare(normalized(b), Qt::CaseInsensitive) == 0;
#else
	return normalized(a) == normalized(b);
#endif
}

QByteArray fileHash(const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		fail(error, file.errorString());
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		fail(error, file.errorString());
		return {};
	}
	return hash.result();
}

bool cancelled(const std::function<bool()>& check, QString* error)
{
	if (!check || !check()) {
		return false;
	}
	fail(error, QCoreApplication::translate("VibeStudioLevelDocument", "The operation was cancelled; the destination was left unchanged."));
	return true;
}

bool writeChunks(QSaveFile* file, const QByteArray& bytes, QString* error, const std::function<bool()>& check)
{
	for (qsizetype offset = 0; offset < bytes.size();) {
		if (cancelled(check, error)) {
			return false;
		}
		const auto size = std::min<qsizetype>(1024 * 1024, bytes.size() - offset);
		if (file->write(bytes.constData() + offset, size) != size) {
			return fail(error, file->errorString());
		}
		offset += size;
	}
	return !cancelled(check, error);
}

bool recoveryIdValid(const QString& id)
{
	return QRegularExpression(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"))
		.match(id)
		.hasMatch();
}

QString recoveryPath(const QString& directory, const QString& id)
{
	return QDir(directory).absoluteFilePath(id + QStringLiteral(".vsrecovery"));
}

bool safeDirectory(const QString& path, QString* error)
{
	QFileInfo info(path);
	// The managed directory itself cannot redirect. Its parent may use a
	// normal platform alias (for example /var on macOS) or a user-chosen link.
	if (info.isSymLink() || info.isJunction()) {
		return fail(error, QCoreApplication::translate("VibeStudioLevelDocument",
													   "A managed backup or recovery directory cannot be a symbolic link."));
	}
	if (!QDir().mkpath(path)) {
		return fail(error, QCoreApplication::translate("VibeStudioLevelDocument", "Unable to create the backup or recovery directory."));
	}
	return true;
}

bool readRecoveryMetadata(QFile* file, LevelMapRecovery* record)
{
	const auto bad = [&]() {
		return fail(&record->error,
					QCoreApplication::translate("VibeStudioLevelDocument",
												"The recovery record is invalid, incomplete, or from an unsupported version."));
	};
	if (!file->open(QIODevice::ReadOnly) || file->size() > kLevelMapMaxDocumentBytes + 65544) {
		return bad();
	}
	const auto header = file->read(8);
	if (header.size() != 8 || !header.startsWith("VSR1")) {
		return bad();
	}
	const quint32 length = qFromLittleEndian<quint32>(header.constData() + 4);
	if (length == 0 || length > 65536) {
		return bad();
	}
	const auto metadata = QJsonDocument::fromJson(file->read(length));
	if (!metadata.isObject()) {
		return bad();
	}
	const auto object = metadata.object();
	if (object.value(QStringLiteral("schemaVersion")) != QJsonValue(1)) {
		return bad();
	}
	record->sourcePath = object.value(QStringLiteral("sourcePath")).toString();
	record->mapName = object.value(QStringLiteral("mapName")).toString();
	record->engineFamily = object.value(QStringLiteral("engineFamily")).toString();
	record->format = object.value(QStringLiteral("format")).toString();
	record->writtenUtc = QDateTime::fromString(object.value(QStringLiteral("writtenUtc")).toString(), Qt::ISODateWithMs);
	record->revision = object.value(QStringLiteral("revision")).toString().toULongLong();
	record->payloadBytes = object.value(QStringLiteral("bytes")).toInteger();
	record->contentHash = QByteArray::fromHex(object.value(QStringLiteral("sha256")).toString().toLatin1());
	record->sourceContentHash = QByteArray::fromHex(object.value(QStringLiteral("sourceSha256")).toString().toLatin1());
	record->doomGeometryChanged = object.value(QStringLiteral("doomGeometryChanged")).toBool();
	if (!record->writtenUtc.isValid() || record->payloadBytes <= 0 || record->payloadBytes > kLevelMapMaxDocumentBytes ||
		record->payloadBytes != file->size() - file->pos() || record->contentHash.size() != 32 ||
		(!record->sourceContentHash.isEmpty() && record->sourceContentHash.size() != 32) ||
		(!record->sourcePath.isEmpty() && record->sourceContentHash.size() != 32) ||
		(record->format != QStringLiteral("doom-wad") && record->format != QStringLiteral("quake-map") &&
		 record->format != QStringLiteral("quake3-map"))) {
		return bad();
	}
	return true;
}

} // namespace

bool createLevelMap(const LevelMapCreateRequest& request, LevelMapDocument* document, QString* error)
{
	if (error) {
		error->clear();
	}
	const QString game = request.game.toLower();
	const bool doom = game == QStringLiteral("doom") || game == QStringLiteral("hexen");
	const bool q3 = game == QStringLiteral("quake3");
	if (!document || (!doom && !q3 && game != QStringLiteral("quake") && game != QStringLiteral("quake2"))) {
		return fail(error, QCoreApplication::translate("VibeStudioLevelDocument",
													   "Choose Quake, Quake II, Quake III, Doom, or Hexen for the new map."));
	}
	if (request.name.trimmed().isEmpty() || request.name.size() > 128 ||
		request.name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f/\\\\]")))) {
		return fail(error,
					QCoreApplication::translate("VibeStudioLevelDocument",
												"Choose a map name of 1 to 128 characters without path separators or control characters."));
	}
	LevelMapDocument result;
	if (doom) {
		for (const auto& texture : {request.wallTexture, request.floorTexture, request.ceilingTexture}) {
			if (!texture.isEmpty() && !QRegularExpression(QStringLiteral("^[!-~]{1,8}$")).match(texture).hasMatch()) {
				return fail(error, QCoreApplication::translate(
									   "VibeStudioLevelDocument",
									   "Doom texture names must contain one to eight printable ASCII characters without spaces."));
			}
		}
		if (!QRegularExpression(QStringLiteral("^(E[0-9]+M[0-9]+|MAP[0-9]+)$")).match(request.mapName).hasMatch() ||
			request.mapName.size() > 8) {
			return fail(error, QCoreApplication::translate("VibeStudioLevelDocument",
														   "Doom map markers must use MAP01 or E1M1 form and fit eight characters."));
		}
		result.format = LevelMapFormat::DoomWad;
		result.engineFamily = QStringLiteral("idTech1");
		result.mapName = request.mapName;
		result.doomFormat = game == QStringLiteral("hexen") ? LevelMapDoomFormat::Hexen : LevelMapDoomFormat::Doom;
		result.doomArchiveLumps << LevelMapWadSourceLump{result.mapName, {}};
		for (const QString& name : {QStringLiteral("THINGS"), QStringLiteral("LINEDEFS"), QStringLiteral("SIDEDEFS"),
									QStringLiteral("VERTEXES"), QStringLiteral("SEGS"), QStringLiteral("SSECTORS"), QStringLiteral("NODES"),
									QStringLiteral("SECTORS"), QStringLiteral("REJECT"), QStringLiteral("BLOCKMAP")}) {
			result.doomArchiveLumps << LevelMapWadSourceLump{name, {}};
		}
		if (result.doomFormat == LevelMapDoomFormat::Hexen) {
			// Empty classic ACS object: directory at byte 8, no scripts or strings.
			// Layout verified against Chocolate Doom 3.1.0, src/hexen/p_acs.c
			// (GPL-2.0-or-later), P_LoadACScripts. No upstream code copied.
			// https://github.com/chocolate-doom/chocolate-doom/blob/chocolate-doom-3.1.0/src/hexen/p_acs.c
			result.doomArchiveLumps << LevelMapWadSourceLump{QStringLiteral("BEHAVIOR"),
															 QByteArray::fromHex("41435300080000000000000000000000")};
		}
		for (const auto& lump : result.doomArchiveLumps) {
			result.doomLumpOrder << lump.name;
			result.doomLumps.insert(lump.name, lump.bytes);
		}
		if (request.starterRoom) {
			if (!drawLevelMapDoomSector(&result, {{-256, -256, 0, true}, {-256, 256, 0, true}, {256, 256, 0, true}, {256, -256, 0, true}},
										nullptr, error) ||
				!addLevelMapDoomThing(&result, 1, 0, 0, 90, nullptr, error)) {
				return false;
			}
			const QString wall = request.wallTexture.isEmpty() ? QStringLiteral("WALL") : request.wallTexture;
			for (int side = 0; side < result.doomSidedefs.size(); ++side) {
				if (!setLevelMapSidedefProperty(&result, result.doomSidedefs.at(side).id, QStringLiteral("middle"), wall, error)) {
					return false;
				}
			}
			if (!setLevelMapSectorProperty(&result, 0, QStringLiteral("floorTexture"),
										   request.floorTexture.isEmpty() ? QStringLiteral("FLOOR") : request.floorTexture, error) ||
				!setLevelMapSectorProperty(&result, 0, QStringLiteral("ceilingTexture"),
										   request.ceilingTexture.isEmpty() ? QStringLiteral("CEILING") : request.ceilingTexture, error)) {
				return false;
			}
		}
	} else {
		if (!loadLevelMapBytes({QStringLiteral("Untitled.map"), {}, q3 ? QStringLiteral("idTech3") : QStringLiteral("idTech2")},
							   q3 ? QByteArray("// Q3Radiant\n{\n\"classname\" \"worldspawn\"\n}\n")
								  : (game == QStringLiteral("quake2") ? QByteArray(kQuake2MapTargetHeader) : QByteArray()) +
										QByteArray("{\n\"classname\" \"worldspawn\"\n}\n"),
							   &result, error)) {
			return false;
		}
		result.mapName = request.name.trimmed();
		if (request.starterRoom) {
			const QString wall =
				request.wallTexture.isEmpty() ? (q3 ? QStringLiteral("common/caulk") : QStringLiteral("WALL")) : request.wallTexture;
			const QString floor = request.floorTexture.isEmpty() ? wall : request.floorTexture;
			const QString ceiling = request.ceilingTexture.isEmpty() ? wall : request.ceilingTexture;
			const auto box = [&](LevelMapVec3 mins, LevelMapVec3 maxs, const QString& texture) {
				return addLevelMapBoxBrush(&result, mins, maxs, texture, nullptr, error);
			};
			if (!box({-272, -272, -16, true}, {272, 272, 0, true}, floor) ||
				!box({-272, -272, 256, true}, {272, 272, 272, true}, ceiling) ||
				!box({-272, -272, 0, true}, {-256, 272, 256, true}, wall) || !box({256, -272, 0, true}, {272, 272, 256, true}, wall) ||
				!box({-256, -272, 0, true}, {256, -256, 256, true}, wall) || !box({-256, 256, 0, true}, {256, 272, 256, true}, wall) ||
				!addLevelMapEntity(&result, q3 ? QStringLiteral("info_player_deathmatch") : QStringLiteral("info_player_start"),
								   {0, 0, 32, true}, {}, nullptr, error) ||
				!addLevelMapEntity(&result, QStringLiteral("light"), {0, 0, 192, true},
								   {{QStringLiteral("light"), QStringLiteral("300"), 0}}, nullptr, error)) {
				return false;
			}
		}
	}
	result.sourcePath.clear();
	result.sourceContentHash.clear();
	result.outputPath.clear();
	result.undoStack.clear();
	result.redoStack.clear();
	result.savedUndoDepth = -1;
	result.editState = QStringLiteral("modified");
	result.revision = 1;
	clearLevelMapSelection(&result);
	*document = std::move(result);
	return true;
}

LevelMapSaveReport writeLevelDocument(const LevelMapDocument& document, const LevelDocumentSaveRequest& request)
{
	LevelMapSaveReport report;
	report.sourcePath = document.sourcePath;
	report.mapName = document.mapName;
	report.format = document.format;
	report.outputPath = QFileInfo(request.path).absoluteFilePath();
	report.dryRun = request.dryRun;
	report.editState = document.editState;
	report.summaryLines = levelMapStatisticsLines(document);
	QString error;
	const auto stop = [&report, &error](const QString& message) {
		report.errors << (message.isEmpty() ? error : message);
		return report;
	};
	if (request.path.trimmed().isEmpty()) {
		return stop(QCoreApplication::translate("VibeStudioLevelDocument", "Choose an output file for the map."));
	}
	const QFileInfo target(report.outputPath);
	const bool existed = target.exists();
	if (target.isSymLink() || target.isDir()) {
		return stop(QCoreApplication::translate("VibeStudioLevelDocument", "A map output cannot be a directory or symbolic link."));
	}
	if (cancelled(request.isCancelled, &error)) {
		return stop(error);
	}
	const auto serialized = serializeLevelMap(document);
	report.warnings = serialized.warnings;
	report.staleLumps = serialized.staleLumps;
	if (!serialized.succeeded()) {
		report.errors = serialized.errors;
		return report;
	}
	report.contentHash = QCryptographicHash::hash(serialized.bytes, QCryptographicHash::Sha256);
	const bool replacesSource = samePath(report.outputPath, document.sourcePath);
	const QByteArray before = existed ? fileHash(report.outputPath, &error) : QByteArray();
	if (existed && before.isEmpty()) {
		return stop(error);
	}
	if (replacesSource && !document.sourceContentHash.isEmpty() && before != document.sourceContentHash) {
		return stop(QCoreApplication::translate(
			"VibeStudioLevelDocument",
			"The map changed or was removed outside the editor. Save As to preserve these edits, or reload the changed file."));
	}
	if (existed && !request.overwrite && !request.dryRun) {
		return stop(QCoreApplication::translate("VibeStudioLevelDocument", "The output exists. Explicit overwrite is required."));
	}
	if (cancelled(request.isCancelled, &error)) {
		return stop(error);
	}
	if (request.dryRun) {
		report.warnings << (target.exists() ? QCoreApplication::translate("VibeStudioLevelDocument", "Would replace the map output.")
											: QCoreApplication::translate("VibeStudioLevelDocument", "Would write a new map output."));
		return report;
	}
	if (!QDir().mkpath(target.absolutePath())) {
		return stop(QCoreApplication::translate("VibeStudioLevelDocument", "Unable to create the map output directory."));
	}
	QSaveFile output(report.outputPath);
	output.setDirectWriteFallback(false);
	if (!output.open(QIODevice::WriteOnly) || !writeChunks(&output, serialized.bytes, &error, request.isCancelled)) {
		return stop(error.isEmpty() ? output.errorString() : error);
	}
	if (existed && request.keepBackup) {
		const QString directory = request.backupDirectory.isEmpty()
									  ? QDir(target.absolutePath()).filePath(QStringLiteral(".vibestudio/map-backups"))
									  : request.backupDirectory;
		if (!safeDirectory(directory, &error)) {
			return stop(error);
		}
		const QString name = target.fileName() + QLatin1Char('.') +
							 QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmsszzz")) + QLatin1Char('-') +
							 QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".bak");
		const QString backupPath = QDir(directory).absoluteFilePath(name);
		QFile input(report.outputPath);
		QSaveFile backup(backupPath);
		backup.setDirectWriteFallback(false);
		if (!input.open(QIODevice::ReadOnly) || !backup.open(QIODevice::WriteOnly)) {
			return stop(QCoreApplication::translate("VibeStudioLevelDocument", "Unable to create a backup; the map was not replaced."));
		}
		QCryptographicHash copied(QCryptographicHash::Sha256);
		while (!input.atEnd()) {
			if (cancelled(request.isCancelled, &error)) {
				return stop(error);
			}
			const auto chunk = input.read(1024 * 1024);
			if (input.error() != QFileDevice::NoError || backup.write(chunk) != chunk.size()) {
				return stop(
					QCoreApplication::translate("VibeStudioLevelDocument", "Unable to finish the backup; the map was not replaced."));
			}
			copied.addData(chunk);
		}
		if (copied.result() != before || !backup.commit()) {
			return stop(QCoreApplication::translate("VibeStudioLevelDocument",
													"The destination changed during backup, or its backup could not be committed."));
		}
		report.backupPath = backupPath;
	}
	if (cancelled(request.isCancelled, &error)) {
		return stop(error);
	}
	const bool existsNow = QFileInfo::exists(report.outputPath);
	if (existsNow != existed || (existsNow && fileHash(report.outputPath, &error) != before)) {
		return stop(QCoreApplication::translate("VibeStudioLevelDocument", "The destination changed while saving; it was left unchanged."));
	}
	if (!output.commit()) {
		return stop(output.errorString());
	}
	report.written = true;
	report.editState = QStringLiteral("saved");
	return report;
}

void adoptLevelDocumentSave(LevelMapDocument* document, const LevelMapSaveReport& report)
{
	if (!document || !report.written || !report.succeeded()) {
		return;
	}
	document->sourcePath = report.outputPath;
	document->outputPath = report.outputPath;
	document->sourceContentHash = report.contentHash;
	if (document->format != LevelMapFormat::DoomWad) {
		document->mapName = QFileInfo(report.outputPath).fileName();
	}
	markLevelMapSaved(document);
}

QString writeLevelMapRecovery(const LevelMapDocument& document, const QString& directory, const QString& id, QString* error,
							  std::function<bool()> isCancelled)
{
	if (error) {
		error->clear();
	}
	if (directory.trimmed().isEmpty() || !recoveryIdValid(id)) {
		fail(error, QCoreApplication::translate("VibeStudioLevelDocument", "A recovery directory and document UUID are required."));
		return {};
	}
	if (!document.sourcePath.isEmpty() && document.sourceContentHash.size() != 32) {
		fail(error,
			 QCoreApplication::translate("VibeStudioLevelDocument",
										 "Recovery requires the loaded source fingerprint. Save this document to a new file first."));
		return {};
	}
	const auto serialized = serializeLevelMap(document);
	if (!serialized.succeeded()) {
		fail(error, serialized.errors.join(QLatin1Char('\n')));
		return {};
	}
	const QJsonObject metadata{
		{QStringLiteral("schemaVersion"), 1},
		{QStringLiteral("sourcePath"), document.sourcePath},
		{QStringLiteral("mapName"), document.mapName},
		{QStringLiteral("engineFamily"), document.engineFamily},
		{QStringLiteral("format"), levelMapFormatId(document.format)},
		{QStringLiteral("writtenUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
		{QStringLiteral("revision"), QString::number(document.revision)},
		{QStringLiteral("bytes"), serialized.bytes.size()},
		{QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(serialized.bytes, QCryptographicHash::Sha256).toHex())},
		{QStringLiteral("sourceSha256"), QString::fromLatin1(document.sourceContentHash.toHex())},
		{QStringLiteral("doomGeometryChanged"), document.doomGeometryChanged}};
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	if (json.size() > 65536 || cancelled(isCancelled, error) || !safeDirectory(directory, error)) {
		return {};
	}
	const QString path = recoveryPath(directory, id);
	if (QFileInfo(path).isSymLink()) {
		fail(error, QCoreApplication::translate("VibeStudioLevelDocument", "A recovery record cannot be a symbolic link."));
		return {};
	}
	QByteArray header("VSR1\0\0\0\0", 8);
	qToLittleEndian<quint32>(json.size(), header.data() + 4);
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size() || file.write(json) != json.size() ||
		!writeChunks(&file, serialized.bytes, error, isCancelled) || !file.commit()) {
		if (error && error->isEmpty()) {
			*error = file.errorString();
		}
		return {};
	}
	return path;
}

LevelMapRecovery inspectLevelMapRecovery(const QString& path)
{
	LevelMapRecovery result;
	result.path = QFileInfo(path).absoluteFilePath();
	QFile file(result.path);
	readRecoveryMetadata(&file, &result);
	return result;
}

QVector<LevelMapRecovery> listLevelMapRecoveries(const QString& directory)
{
	QVector<LevelMapRecovery> result;
	if (directory.trimmed().isEmpty()) {
		return result;
	}
	for (const auto& info : QDir(directory).entryInfoList({QStringLiteral("*.vsrecovery")}, QDir::Files | QDir::NoSymLinks, QDir::Time)) {
		result << inspectLevelMapRecovery(info.absoluteFilePath());
	}
	return result;
}

bool restoreLevelMapRecovery(const QString& path, LevelMapDocument* document, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!document) {
		return fail(error, QCoreApplication::translate("VibeStudioLevelDocument", "No recovery document output was provided."));
	}
	LevelMapRecovery record;
	record.path = path;
	QFile file(path);
	if (!readRecoveryMetadata(&file, &record)) {
		return fail(error, record.error);
	}
	const auto bytes = file.read(kLevelMapMaxDocumentBytes + 1);
	if (bytes.size() != record.payloadBytes || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != record.contentHash) {
		return fail(error, QCoreApplication::translate("VibeStudioLevelDocument",
													   "Recovery content failed its integrity check. The current map was not changed."));
	}
	const bool wad = record.format == QStringLiteral("doom-wad");
	const QString parsingPath = wad ? QStringLiteral("recovery.wad") : QStringLiteral("recovery.map");
	LevelMapDocument restored;
	if (!loadLevelMapBytes({parsingPath, wad ? record.mapName : QString(), record.engineFamily}, bytes, &restored, error)) {
		return false;
	}
	if (levelMapFormatId(restored.format) != record.format) {
		return fail(error,
					QCoreApplication::translate("VibeStudioLevelDocument", "Recovery metadata does not match the stored map format."));
	}
	restored.sourcePath = record.sourcePath;
	restored.sourceContentHash = record.sourceContentHash;
	restored.mapName = record.mapName;
	restored.editState = QStringLiteral("modified");
	restored.savedUndoDepth = -1;
	restored.revision = record.revision;
	restored.doomGeometryChanged = record.doomGeometryChanged;
	restored.doomGeometryEdits = record.doomGeometryChanged ? 1 : 0;
	*document = std::move(restored);
	return true;
}

bool removeLevelMapRecovery(const QString& directory, const QString& id, QString* error)
{
	if (error) {
		error->clear();
	}
	if (directory.trimmed().isEmpty() || !recoveryIdValid(id)) {
		return fail(error, QCoreApplication::translate("VibeStudioLevelDocument", "Invalid recovery directory or document UUID."));
	}
	const QString path = recoveryPath(directory, id);
	if (!QFileInfo::exists(path)) {
		return true;
	}
	if (QFileInfo(path).isSymLink() || !QFile::remove(path)) {
		return fail(error, QCoreApplication::translate("VibeStudioLevelDocument", "Unable to remove the recovery record."));
	}
	return true;
}

} // namespace vibestudio
