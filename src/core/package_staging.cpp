#include "core/package_staging.h"

#include <QCryptographicHash>
#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTime>

#include <algorithm>
#include <limits>

namespace vibestudio {

namespace {

constexpr quint32 kZipLocalFileSignature = 0x04034b50;
constexpr quint32 kZipCentralDirectorySignature = 0x02014b50;
constexpr quint32 kZipEndOfCentralDirectorySignature = 0x06054b50;

QString normalizedId(QString value)
{
	return value.trimmed().toLower().replace('_', '-');
}

QString entryKey(const QString& virtualPath)
{
	return virtualPath.toCaseFolded();
}

bool stagedEntryLess(const PackageStagedEntry& left, const PackageStagedEntry& right)
{
	return left.virtualPath.compare(right.virtualPath, Qt::CaseInsensitive) < 0;
}

int findEntryIndex(const QVector<PackageStagedEntry>& entries, const QString& virtualPath)
{
	const QString key = entryKey(virtualPath);
	for (int index = 0; index < entries.size(); ++index) {
		if (entryKey(entries[index].virtualPath) == key) {
			return index;
		}
	}
	return -1;
}

void addConflict(QVector<PackageStageConflict>* conflicts, const QString& operationId, const QString& virtualPath, const QString& message, bool blocking = true)
{
	if (!conflicts || message.trimmed().isEmpty()) {
		return;
	}
	conflicts->push_back({operationId.trimmed(), virtualPath.trimmed(), message.trimmed(), blocking});
}

bool readSourceFile(const QString& sourceFilePath, QByteArray* bytes, QString* error)
{
	if (error) {
		error->clear();
	}
	if (bytes) {
		bytes->clear();
	}
	const QFileInfo info(sourceFilePath);
	if (!info.exists() || !info.isFile()) {
		if (error) {
			*error = QStringLiteral("Source file does not exist.");
		}
		return false;
	}
	QFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QStringLiteral("Unable to read source file.");
		}
		return false;
	}
	if (bytes) {
		*bytes = file.readAll();
	}
	return true;
}

QByteArray sha256Bytes(const QByteArray& bytes)
{
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

QString topLevelLocation(const QString& virtualPath)
{
	const QString parent = packageVirtualPathParent(virtualPath);
	if (parent.isEmpty()) {
		return QStringLiteral("/");
	}
	const QString top = parent.section('/', 0, 0);
	return top.isEmpty() ? QStringLiteral("/") : top;
}

QString typeBucket(const QString& virtualPath)
{
	const QString suffix = QFileInfo(virtualPath).suffix().toLower();
	if (suffix.isEmpty()) {
		return QStringLiteral("binary");
	}
	if (QStringList {QStringLiteral("txt"), QStringLiteral("cfg"), QStringLiteral("shader"), QStringLiteral("map"), QStringLiteral("def"), QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("log")}.contains(suffix)) {
		return QStringLiteral("text");
	}
	if (QStringList {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("tga"), QStringLiteral("pcx"), QStringLiteral("wal"), QStringLiteral("mip"), QStringLiteral("lmp")}.contains(suffix)) {
		return QStringLiteral("image");
	}
	if (QStringList {QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("mp3")}.contains(suffix)) {
		return QStringLiteral("audio");
	}
	if (QStringList {QStringLiteral("mdl"), QStringLiteral("md2"), QStringLiteral("md3"), QStringLiteral("mdc"), QStringLiteral("mdr"), QStringLiteral("iqm")}.contains(suffix)) {
		return QStringLiteral("model");
	}
	return suffix;
}

QVector<PackageCompositionBucket> compositionBuckets(const QVector<PackageStagedEntry>& entries)
{
	QVector<PackageCompositionBucket> buckets;
	for (const PackageStagedEntry& entry : entries) {
		const QString id = QStringLiteral("%1:%2").arg(topLevelLocation(entry.virtualPath), typeBucket(entry.virtualPath));
		auto existing = std::find_if(buckets.begin(), buckets.end(), [&id](const PackageCompositionBucket& bucket) {
			return bucket.id == id;
		});
		if (existing == buckets.end()) {
			PackageCompositionBucket bucket;
			bucket.id = id;
			bucket.label = QStringLiteral("%1 / %2").arg(topLevelLocation(entry.virtualPath), typeBucket(entry.virtualPath));
			buckets.push_back(bucket);
			existing = buckets.end() - 1;
		}
		++existing->fileCount;
		existing->sizeBytes += static_cast<quint64>(entry.bytes.size());
	}
	std::sort(buckets.begin(), buckets.end(), [](const PackageCompositionBucket& left, const PackageCompositionBucket& right) {
		if (left.sizeBytes != right.sizeBytes) {
			return left.sizeBytes > right.sizeBytes;
		}
		return left.label.compare(right.label, Qt::CaseInsensitive) < 0;
	});
	return buckets;
}

void appendLe16(QByteArray* data, quint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* data, quint32 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
	data->append(static_cast<char>((value >> 16) & 0xff));
	data->append(static_cast<char>((value >> 24) & 0xff));
}

quint32 crc32(const QByteArray& bytes)
{
	quint32 crc = 0xffffffffu;
	for (uchar byte : bytes) {
		crc ^= byte;
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc & 1u) ? ((crc >> 1) ^ 0xedb88320u) : (crc >> 1);
		}
	}
	return crc ^ 0xffffffffu;
}

quint16 fixedDosTime()
{
	return 0;
}

quint16 fixedDosDate()
{
	return static_cast<quint16>(((1980 - 1980) << 9) | (1 << 5) | 1);
}

bool validateArchiveSize(qint64 value, QString* error, const QString& label)
{
	if (value < 0 || value > std::numeric_limits<quint32>::max()) {
		if (error) {
			*error = QStringLiteral("%1 exceeds the ZIP/PAK MVP writer size limit.").arg(label);
		}
		return false;
	}
	return true;
}

bool writePakBytes(const QVector<PackageStagedEntry>& inputEntries, QByteArray* out, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = QStringLiteral("Missing output buffer.");
		}
		return false;
	}

	QVector<PackageStagedEntry> entries = inputEntries;
	std::sort(entries.begin(), entries.end(), stagedEntryLess);
	QByteArray data;
	data.append("PACK");
	appendLe32(&data, 0);
	appendLe32(&data, 0);

	struct Record {
		QString virtualPath;
		quint32 offset = 0;
		quint32 size = 0;
	};
	QVector<Record> records;
	for (const PackageStagedEntry& entry : entries) {
		const QByteArray name = entry.virtualPath.toLatin1();
		if (name.isEmpty() || name.size() > 56 || QString::fromLatin1(name) != entry.virtualPath) {
			if (error) {
				*error = QStringLiteral("PAK entry path must be Latin-1 and at most 56 bytes: %1").arg(entry.virtualPath);
			}
			return false;
		}
		if (!validateArchiveSize(data.size(), error, QStringLiteral("PAK data offset")) || !validateArchiveSize(entry.bytes.size(), error, QStringLiteral("PAK entry size"))) {
			return false;
		}
		records.push_back({entry.virtualPath, static_cast<quint32>(data.size()), static_cast<quint32>(entry.bytes.size())});
		data.append(entry.bytes);
	}

	if (!validateArchiveSize(data.size(), error, QStringLiteral("PAK directory offset")) || !validateArchiveSize(records.size() * 64, error, QStringLiteral("PAK directory"))) {
		return false;
	}
	const quint32 directoryOffset = static_cast<quint32>(data.size());
	const quint32 directoryLength = static_cast<quint32>(records.size() * 64);
	for (const Record& record : records) {
		const QByteArray name = record.virtualPath.toLatin1();
		const int start = data.size();
		data.append(name);
		while (data.size() - start < 56) {
			data.append('\0');
		}
		appendLe32(&data, record.offset);
		appendLe32(&data, record.size);
	}
	for (int byte = 0; byte < 4; ++byte) {
		data[4 + byte] = static_cast<char>((directoryOffset >> (byte * 8)) & 0xff);
		data[8 + byte] = static_cast<char>((directoryLength >> (byte * 8)) & 0xff);
	}
	*out = data;
	return true;
}

bool writeZipBytes(const QVector<PackageStagedEntry>& inputEntries, QByteArray* out, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = QStringLiteral("Missing output buffer.");
		}
		return false;
	}

	QVector<PackageStagedEntry> entries = inputEntries;
	std::sort(entries.begin(), entries.end(), stagedEntryLess);
	QByteArray data;
	QByteArray central;
	for (const PackageStagedEntry& entry : entries) {
		const QByteArray name = entry.virtualPath.toUtf8();
		if (name.isEmpty() || name.size() > std::numeric_limits<quint16>::max()) {
			if (error) {
				*error = QStringLiteral("ZIP entry path is empty or too long: %1").arg(entry.virtualPath);
			}
			return false;
		}
		if (!validateArchiveSize(data.size(), error, QStringLiteral("ZIP local header offset")) || !validateArchiveSize(entry.bytes.size(), error, QStringLiteral("ZIP entry size"))) {
			return false;
		}
		const quint32 localOffset = static_cast<quint32>(data.size());
		const quint32 size = static_cast<quint32>(entry.bytes.size());
		const quint32 crc = crc32(entry.bytes);

		appendLe32(&data, kZipLocalFileSignature);
		appendLe16(&data, 20);
		appendLe16(&data, 0x0800);
		appendLe16(&data, 0);
		appendLe16(&data, fixedDosTime());
		appendLe16(&data, fixedDosDate());
		appendLe32(&data, crc);
		appendLe32(&data, size);
		appendLe32(&data, size);
		appendLe16(&data, static_cast<quint16>(name.size()));
		appendLe16(&data, 0);
		data.append(name);
		data.append(entry.bytes);

		appendLe32(&central, kZipCentralDirectorySignature);
		appendLe16(&central, 20);
		appendLe16(&central, 20);
		appendLe16(&central, 0x0800);
		appendLe16(&central, 0);
		appendLe16(&central, fixedDosTime());
		appendLe16(&central, fixedDosDate());
		appendLe32(&central, crc);
		appendLe32(&central, size);
		appendLe32(&central, size);
		appendLe16(&central, static_cast<quint16>(name.size()));
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe32(&central, 0);
		appendLe32(&central, localOffset);
		central.append(name);
	}

	if (!validateArchiveSize(data.size(), error, QStringLiteral("ZIP central directory offset")) || !validateArchiveSize(central.size(), error, QStringLiteral("ZIP central directory size")) || entries.size() > std::numeric_limits<quint16>::max()) {
		return false;
	}
	const quint32 centralOffset = static_cast<quint32>(data.size());
	data.append(central);
	appendLe32(&data, kZipEndOfCentralDirectorySignature);
	appendLe16(&data, 0);
	appendLe16(&data, 0);
	appendLe16(&data, static_cast<quint16>(entries.size()));
	appendLe16(&data, static_cast<quint16>(entries.size()));
	appendLe32(&data, static_cast<quint32>(central.size()));
	appendLe32(&data, centralOffset);
	appendLe16(&data, 0);
	*out = data;
	return true;
}

bool wadNameIsValid(const QString& name)
{
	if (name.isEmpty() || name.size() > 8 || name.contains('/')) {
		return false;
	}
	return !name.toLatin1().contains('\0') && QString::fromLatin1(name.toLatin1()) == name;
}

bool isDoomMapMarker(const QString& name)
{
	if (name.size() == 4 && name[0] == QLatin1Char('E') && name[2] == QLatin1Char('M') && name[1].isDigit() && name[3].isDigit()) {
		return true;
	}
	return name.size() == 5 && name.startsWith(QStringLiteral("MAP")) && name[3].isDigit() && name[4].isDigit();
}

int doomMapLumpRank(const QString& name)
{
	if (isDoomMapMarker(name)) {
		return 0;
	}
	const QStringList order = {
		QStringLiteral("THINGS"),
		QStringLiteral("LINEDEFS"),
		QStringLiteral("SIDEDEFS"),
		QStringLiteral("VERTEXES"),
		QStringLiteral("SEGS"),
		QStringLiteral("SSECTORS"),
		QStringLiteral("NODES"),
		QStringLiteral("SECTORS"),
		QStringLiteral("REJECT"),
		QStringLiteral("BLOCKMAP"),
		QStringLiteral("BEHAVIOR"),
		QStringLiteral("SCRIPTS"),
	};
	const int index = order.indexOf(name);
	return index >= 0 ? index + 1 : 1000;
}

QVector<PackageStagedEntry> wadOrderedEntries(QVector<PackageStagedEntry> entries)
{
	std::stable_sort(entries.begin(), entries.end(), [](const PackageStagedEntry& left, const PackageStagedEntry& right) {
		const QString leftName = left.virtualPath.toUpper();
		const QString rightName = right.virtualPath.toUpper();
		const int leftRank = doomMapLumpRank(leftName);
		const int rightRank = doomMapLumpRank(rightName);
		if (leftRank != rightRank) {
			return leftRank < rightRank;
		}
		if (leftRank >= 1000) {
			return leftName.compare(rightName, Qt::CaseInsensitive) < 0;
		}
		return false;
	});
	return entries;
}

bool writeWadBytes(const QVector<PackageStagedEntry>& inputEntries, QByteArray* out, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!out) {
		if (error) {
			*error = QStringLiteral("Missing output buffer.");
		}
		return false;
	}
	QVector<PackageStagedEntry> entries = wadOrderedEntries(inputEntries);
	if (entries.size() > std::numeric_limits<qint32>::max()) {
		if (error) {
			*error = QStringLiteral("WAD entry count exceeds the MVP writer limit.");
		}
		return false;
	}

	QByteArray data;
	data.append("PWAD");
	appendLe32(&data, static_cast<quint32>(entries.size()));
	appendLe32(&data, 0);
	struct Record {
		QString name;
		quint32 offset = 0;
		quint32 size = 0;
	};
	QVector<Record> records;
	for (const PackageStagedEntry& entry : entries) {
		const QString name = entry.virtualPath.toUpper();
		if (!wadNameIsValid(name)) {
			if (error) {
				*error = QStringLiteral("WAD lump names must be Latin-1, contain no folders, and be at most 8 characters: %1").arg(entry.virtualPath);
			}
			return false;
		}
		if (!validateArchiveSize(data.size(), error, QStringLiteral("WAD lump offset")) || !validateArchiveSize(entry.bytes.size(), error, QStringLiteral("WAD lump size"))) {
			return false;
		}
		records.push_back({name, static_cast<quint32>(data.size()), static_cast<quint32>(entry.bytes.size())});
		data.append(entry.bytes);
	}
	if (!validateArchiveSize(data.size(), error, QStringLiteral("WAD directory offset"))) {
		return false;
	}
	const quint32 directoryOffset = static_cast<quint32>(data.size());
	for (const Record& record : records) {
		appendLe32(&data, record.offset);
		appendLe32(&data, record.size);
		const QByteArray name = record.name.toLatin1();
		const int start = data.size();
		data.append(name);
		while (data.size() - start < 8) {
			data.append('\0');
		}
	}
	for (int byte = 0; byte < 4; ++byte) {
		data[8 + byte] = static_cast<char>((directoryOffset >> (byte * 8)) & 0xff);
	}
	*out = data;
	return true;
}

QJsonObject entryJson(const PackageStagedEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("virtualPath"), entry.virtualPath);
	object.insert(QStringLiteral("bytes"), static_cast<double>(entry.bytes.size()));
	object.insert(QStringLiteral("sha256"), QString::fromLatin1(sha256Bytes(entry.bytes)));
	object.insert(QStringLiteral("source"), entry.source);
	object.insert(QStringLiteral("operationId"), entry.operationId);
	return object;
}

QJsonArray entriesJson(const QVector<PackageStagedEntry>& entries)
{
	QJsonArray array;
	for (const PackageStagedEntry& entry : entries) {
		array.append(entryJson(entry));
	}
	return array;
}

QJsonObject operationJson(const PackageStageOperation& operation)
{
	QJsonObject object;
	object.insert(QStringLiteral("id"), operation.id);
	object.insert(QStringLiteral("type"), packageStageOperationTypeId(operation.type));
	object.insert(QStringLiteral("virtualPath"), operation.virtualPath);
	object.insert(QStringLiteral("targetVirtualPath"), operation.targetVirtualPath);
	object.insert(QStringLiteral("sourceFilePath"), operation.sourceFilePath);
	object.insert(QStringLiteral("conflictResolution"), packageStageConflictResolutionId(operation.conflictResolution));
	return object;
}

QJsonArray operationsJson(const QVector<PackageStageOperation>& operations)
{
	QJsonArray array;
	for (const PackageStageOperation& operation : operations) {
		array.append(operationJson(operation));
	}
	return array;
}

QJsonArray conflictsJson(const QVector<PackageStageConflict>& conflicts)
{
	QJsonArray array;
	for (const PackageStageConflict& conflict : conflicts) {
		QJsonObject object;
		object.insert(QStringLiteral("operationId"), conflict.operationId);
		object.insert(QStringLiteral("virtualPath"), conflict.virtualPath);
		object.insert(QStringLiteral("message"), conflict.message);
		object.insert(QStringLiteral("blocking"), conflict.blocking);
		array.append(object);
	}
	return array;
}

QJsonArray bucketsJson(const QVector<PackageCompositionBucket>& buckets)
{
	QJsonArray array;
	for (const PackageCompositionBucket& bucket : buckets) {
		QJsonObject object;
		object.insert(QStringLiteral("id"), bucket.id);
		object.insert(QStringLiteral("label"), bucket.label);
		object.insert(QStringLiteral("fileCount"), bucket.fileCount);
		object.insert(QStringLiteral("sizeBytes"), static_cast<double>(bucket.sizeBytes));
		array.append(object);
	}
	return array;
}

QVector<PackageStagedEntry> computePlan(const QVector<PackageStagedEntry>& baseEntries, const QVector<PackageStageOperation>& operations, const QVector<PackageStageConflict>& baseConflicts, QVector<PackageStageConflict>* conflicts)
{
	QVector<PackageStagedEntry> entries = baseEntries;
	if (conflicts) {
		*conflicts = baseConflicts;
	}

	for (const PackageStageOperation& operation : operations) {
		PackageVirtualPath normalized = normalizePackageVirtualPath(operation.virtualPath, false);
		if (!normalized.isSafe()) {
			addConflict(conflicts, operation.id, operation.virtualPath, QStringLiteral("Unsafe package path: %1").arg(packagePathIssueDisplayName(normalized.issue)));
			continue;
		}

		const int existingIndex = findEntryIndex(entries, normalized.normalizedPath);
		if (operation.type == PackageStageOperationType::Add || operation.type == PackageStageOperationType::Replace) {
			QByteArray bytes;
			QString error;
			if (!readSourceFile(operation.sourceFilePath, &bytes, &error)) {
				addConflict(conflicts, operation.id, normalized.normalizedPath, error);
				continue;
			}
			if (operation.type == PackageStageOperationType::Add) {
				if (existingIndex >= 0) {
					if (operation.conflictResolution == PackageStageConflictResolution::ReplaceExisting) {
						entries[existingIndex] = {normalized.normalizedPath, bytes, QStringLiteral("staged-add-replace"), operation.id};
					} else if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
						addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Skipped add because an entry already exists."), false);
					} else {
						addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Cannot add because an entry already exists. Use replace or replace-existing conflict resolution."));
					}
					continue;
				}
				entries.push_back({normalized.normalizedPath, bytes, QStringLiteral("staged-add"), operation.id});
				continue;
			}
			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Skipped replace because the entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Cannot replace because the entry is missing."));
				}
				continue;
			}
			entries[existingIndex] = {normalized.normalizedPath, bytes, QStringLiteral("staged-replace"), operation.id};
			continue;
		}

		if (operation.type == PackageStageOperationType::Rename) {
			PackageVirtualPath target = normalizePackageVirtualPath(operation.targetVirtualPath, false);
			if (!target.isSafe()) {
				addConflict(conflicts, operation.id, operation.targetVirtualPath, QStringLiteral("Unsafe target package path: %1").arg(packagePathIssueDisplayName(target.issue)));
				continue;
			}
			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Skipped rename because the source entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Cannot rename because the source entry is missing."));
				}
				continue;
			}
			const int targetIndex = findEntryIndex(entries, target.normalizedPath);
			if (targetIndex >= 0 && targetIndex != existingIndex) {
				if (operation.conflictResolution == PackageStageConflictResolution::ReplaceExisting) {
					entries.removeAt(targetIndex);
				} else if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, target.normalizedPath, QStringLiteral("Skipped rename because the target entry exists."), false);
					continue;
				} else {
					addConflict(conflicts, operation.id, target.normalizedPath, QStringLiteral("Cannot rename because the target entry already exists."));
					continue;
				}
			}
			const int sourceIndex = findEntryIndex(entries, normalized.normalizedPath);
			if (sourceIndex >= 0) {
				entries[sourceIndex].virtualPath = target.normalizedPath;
				entries[sourceIndex].source = QStringLiteral("staged-rename");
				entries[sourceIndex].operationId = operation.id;
			}
			continue;
		}

		if (operation.type == PackageStageOperationType::Delete) {
			if (existingIndex < 0) {
				if (operation.conflictResolution == PackageStageConflictResolution::Skip) {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Skipped delete because the entry is missing."), false);
				} else {
					addConflict(conflicts, operation.id, normalized.normalizedPath, QStringLiteral("Cannot delete because the entry is missing."));
				}
				continue;
			}
			entries.removeAt(existingIndex);
		}
	}
	return entries;
}

quint64 totalBytes(const QVector<PackageStagedEntry>& entries)
{
	quint64 total = 0;
	for (const PackageStagedEntry& entry : entries) {
		total += static_cast<quint64>(entry.bytes.size());
	}
	return total;
}

} // namespace

bool PackageWriteReport::succeeded() const
{
	return blockedMessages.isEmpty() && !outputPath.isEmpty() && bytesWritten > 0;
}

bool PackageStagingModel::loadBaseArchive(const PackageArchiveReader& archive, QString* error)
{
	if (error) {
		error->clear();
	}
	clear();
	if (!archive.isOpen()) {
		if (error) {
			*error = QStringLiteral("No package is open.");
		}
		return false;
	}

	m_sourcePath = archive.sourcePath();
	m_sourceFormat = archive.format();
	m_loaded = true;
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.kind != PackageEntryKind::File) {
			continue;
		}
		if (!entry.readable) {
			m_baseConflicts.push_back({QString(), entry.virtualPath, entry.note.isEmpty() ? QStringLiteral("Base entry is not readable and cannot be preserved by the MVP writer.") : entry.note, true});
			continue;
		}
		QByteArray bytes;
		QString readError;
		if (!archive.readEntryBytes(entry.virtualPath, &bytes, &readError)) {
			m_baseConflicts.push_back({QString(), entry.virtualPath, readError.isEmpty() ? QStringLiteral("Unable to read base entry bytes.") : readError, true});
			continue;
		}
		m_baseEntries.push_back({entry.virtualPath, bytes, QStringLiteral("base"), QString()});
	}
	std::sort(m_baseEntries.begin(), m_baseEntries.end(), stagedEntryLess);
	return true;
}

void PackageStagingModel::clear()
{
	m_sourcePath.clear();
	m_sourceFormat = PackageArchiveFormat::Unknown;
	m_loaded = false;
	m_baseEntries.clear();
	m_operations.clear();
	m_baseConflicts.clear();
}

bool PackageStagingModel::isLoaded() const
{
	return m_loaded;
}

QString PackageStagingModel::sourcePath() const
{
	return m_sourcePath;
}

PackageArchiveFormat PackageStagingModel::sourceFormat() const
{
	return m_sourceFormat;
}

QVector<PackageStageOperation> PackageStagingModel::operations() const
{
	return m_operations;
}

QVector<PackageStageConflict> PackageStagingModel::conflicts() const
{
	QVector<PackageStageConflict> conflicts;
	computePlan(m_baseEntries, m_operations, m_baseConflicts, &conflicts);
	return conflicts;
}

QVector<PackageStagedEntry> PackageStagingModel::beforeEntries() const
{
	return m_baseEntries;
}

QVector<PackageStagedEntry> PackageStagingModel::plannedEntries() const
{
	return computePlan(m_baseEntries, m_operations, m_baseConflicts, nullptr);
}

PackageStagingSummary PackageStagingModel::summary() const
{
	const QVector<PackageStagedEntry> planned = plannedEntries();
	const QVector<PackageStageConflict> currentConflicts = conflicts();
	PackageStagingSummary result;
	result.sourcePath = m_sourcePath;
	result.sourceFormat = m_sourceFormat;
	result.baseFileCount = m_baseEntries.size();
	result.stagedFileCount = planned.size();
	result.operationCount = m_operations.size();
	result.beforeBytes = totalBytes(m_baseEntries);
	result.afterBytes = totalBytes(planned);
	for (const PackageStageOperation& operation : m_operations) {
		switch (operation.type) {
		case PackageStageOperationType::Add:
			++result.addedCount;
			break;
		case PackageStageOperationType::Replace:
			++result.replacedCount;
			break;
		case PackageStageOperationType::Rename:
			++result.renamedCount;
			break;
		case PackageStageOperationType::Delete:
			++result.deletedCount;
			break;
		}
	}
	result.conflictCount = currentConflicts.size();
	for (const PackageStageConflict& conflict : currentConflicts) {
		if (conflict.blocking) {
			++result.blockingCount;
			result.blockedMessages.push_back(conflict.virtualPath.isEmpty() ? conflict.message : QStringLiteral("%1: %2").arg(conflict.virtualPath, conflict.message));
		}
	}
	result.canSave = m_loaded && result.blockingCount == 0;
	return result;
}

QVector<PackageCompositionBucket> PackageStagingModel::beforeComposition() const
{
	return compositionBuckets(m_baseEntries);
}

QVector<PackageCompositionBucket> PackageStagingModel::afterComposition() const
{
	return compositionBuckets(plannedEntries());
}

QByteArray PackageStagingModel::manifestJson() const
{
	const PackageStagingSummary stagingSummary = summary();
	QJsonObject summaryObject;
	summaryObject.insert(QStringLiteral("sourcePath"), stagingSummary.sourcePath);
	summaryObject.insert(QStringLiteral("sourceFormat"), packageArchiveFormatId(stagingSummary.sourceFormat));
	summaryObject.insert(QStringLiteral("baseFileCount"), stagingSummary.baseFileCount);
	summaryObject.insert(QStringLiteral("stagedFileCount"), stagingSummary.stagedFileCount);
	summaryObject.insert(QStringLiteral("operationCount"), stagingSummary.operationCount);
	summaryObject.insert(QStringLiteral("addedCount"), stagingSummary.addedCount);
	summaryObject.insert(QStringLiteral("replacedCount"), stagingSummary.replacedCount);
	summaryObject.insert(QStringLiteral("renamedCount"), stagingSummary.renamedCount);
	summaryObject.insert(QStringLiteral("deletedCount"), stagingSummary.deletedCount);
	summaryObject.insert(QStringLiteral("conflictCount"), stagingSummary.conflictCount);
	summaryObject.insert(QStringLiteral("blockingCount"), stagingSummary.blockingCount);
	summaryObject.insert(QStringLiteral("beforeBytes"), static_cast<double>(stagingSummary.beforeBytes));
	summaryObject.insert(QStringLiteral("afterBytes"), static_cast<double>(stagingSummary.afterBytes));
	summaryObject.insert(QStringLiteral("canSave"), stagingSummary.canSave);

	QJsonObject root;
	root.insert(QStringLiteral("schemaVersion"), 1);
	root.insert(QStringLiteral("summary"), summaryObject);
	root.insert(QStringLiteral("operations"), operationsJson(m_operations));
	root.insert(QStringLiteral("conflicts"), conflictsJson(conflicts()));
	root.insert(QStringLiteral("beforeEntries"), entriesJson(m_baseEntries));
	root.insert(QStringLiteral("afterEntries"), entriesJson(plannedEntries()));
	root.insert(QStringLiteral("beforeComposition"), bucketsJson(beforeComposition()));
	root.insert(QStringLiteral("afterComposition"), bucketsJson(afterComposition()));
	return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool PackageStagingModel::addFile(const QString& sourceFilePath, const QString& virtualPath, QString* error, PackageStageConflictResolution resolution)
{
	PackageStageOperation operation;
	operation.id = nextOperationId();
	operation.type = PackageStageOperationType::Add;
	operation.sourceFilePath = QFileInfo(sourceFilePath).absoluteFilePath();
	operation.virtualPath = virtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error);
}

bool PackageStagingModel::replaceFile(const QString& virtualPath, const QString& sourceFilePath, QString* error)
{
	PackageStageOperation operation;
	operation.id = nextOperationId();
	operation.type = PackageStageOperationType::Replace;
	operation.virtualPath = virtualPath;
	operation.sourceFilePath = QFileInfo(sourceFilePath).absoluteFilePath();
	return appendOperation(operation, error);
}

bool PackageStagingModel::renameEntry(const QString& virtualPath, const QString& targetVirtualPath, QString* error, PackageStageConflictResolution resolution)
{
	PackageStageOperation operation;
	operation.id = nextOperationId();
	operation.type = PackageStageOperationType::Rename;
	operation.virtualPath = virtualPath;
	operation.targetVirtualPath = targetVirtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error);
}

bool PackageStagingModel::deleteEntry(const QString& virtualPath, QString* error, PackageStageConflictResolution resolution)
{
	PackageStageOperation operation;
	operation.id = nextOperationId();
	operation.type = PackageStageOperationType::Delete;
	operation.virtualPath = virtualPath;
	operation.conflictResolution = resolution;
	return appendOperation(operation, error);
}

bool PackageStagingModel::clearOperation(const QString& operationId)
{
	const qsizetype oldSize = m_operations.size();
	m_operations.erase(
		std::remove_if(m_operations.begin(), m_operations.end(), [&operationId](const PackageStageOperation& operation) {
			return operation.id == operationId;
		}),
		m_operations.end());
	return m_operations.size() != oldSize;
}

bool PackageStagingModel::exportManifest(const QString& outputPath, QString* error) const
{
	if (error) {
		error->clear();
	}
	if (outputPath.trimmed().isEmpty()) {
		if (error) {
			*error = QStringLiteral("Manifest output path is required.");
		}
		return false;
	}
	QSaveFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = QStringLiteral("Unable to open package manifest for writing.");
		}
		return false;
	}
	const QByteArray json = manifestJson();
	if (file.write(json) != json.size()) {
		if (error) {
			*error = QStringLiteral("Unable to write package manifest.");
		}
		return false;
	}
	if (!file.commit()) {
		if (error) {
			*error = QStringLiteral("Unable to commit package manifest.");
		}
		return false;
	}
	return true;
}

PackageWriteReport PackageStagingModel::writeArchive(const PackageWriteRequest& request) const
{
	PackageWriteReport report;
	report.sourcePath = m_sourcePath;
	report.outputPath = QFileInfo(request.destinationPath).absoluteFilePath();
	report.format = request.format == PackageArchiveFormat::Unknown ? packageArchiveFormatFromFileName(request.destinationPath) : request.format;
	report.dryRun = request.dryRun;

	const PackageStagingSummary stagingSummary = summary();
	if (!stagingSummary.canSave) {
		report.blockedMessages = stagingSummary.blockedMessages;
		if (report.blockedMessages.isEmpty()) {
			report.blockedMessages.push_back(QStringLiteral("Package staging model is not saveable."));
		}
		return report;
	}
	if (request.destinationPath.trimmed().isEmpty()) {
		report.blockedMessages.push_back(QStringLiteral("Save-as destination path is required."));
		return report;
	}
	if (QFileInfo(request.destinationPath).absoluteFilePath().compare(QFileInfo(m_sourcePath).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
		report.blockedMessages.push_back(QStringLiteral("Save-as destination must be different from the source package path."));
		return report;
	}
	if (QFileInfo::exists(request.destinationPath) && !request.allowOverwrite) {
		report.blockedMessages.push_back(QStringLiteral("Destination already exists. Choose a new save-as path or enable overwrite explicitly."));
		return report;
	}
	if (!(report.format == PackageArchiveFormat::Pak || report.format == PackageArchiveFormat::Zip || report.format == PackageArchiveFormat::Pk3 || report.format == PackageArchiveFormat::Wad)) {
		report.blockedMessages.push_back(QStringLiteral("MVP write-back supports PAK, ZIP, PK3, and tested PWAD outputs."));
		return report;
	}

	QByteArray bytes;
	QString writerError;
	const QVector<PackageStagedEntry> entries = plannedEntries();
	bool wroteBytes = false;
	if (report.format == PackageArchiveFormat::Pak) {
		wroteBytes = writePakBytes(entries, &bytes, &writerError);
	} else if (report.format == PackageArchiveFormat::Zip || report.format == PackageArchiveFormat::Pk3) {
		wroteBytes = writeZipBytes(entries, &bytes, &writerError);
	} else if (report.format == PackageArchiveFormat::Wad) {
		wroteBytes = writeWadBytes(entries, &bytes, &writerError);
	}
	if (!wroteBytes) {
		report.blockedMessages.push_back(writerError.isEmpty() ? QStringLiteral("Unable to write package bytes.") : writerError);
		return report;
	}

	report.entryCount = entries.size();
	report.bytesWritten = static_cast<quint64>(bytes.size());
	report.sha256 = QString::fromLatin1(sha256Bytes(bytes));
	if (request.dryRun) {
		return report;
	}

	QSaveFile outputFile(request.destinationPath);
	if (!outputFile.open(QIODevice::WriteOnly)) {
		report.blockedMessages.push_back(QStringLiteral("Unable to open save-as package path."));
		return report;
	}
	if (outputFile.write(bytes) != bytes.size()) {
		report.blockedMessages.push_back(QStringLiteral("Unable to write save-as package bytes."));
		return report;
	}
	if (!outputFile.commit()) {
		report.blockedMessages.push_back(QStringLiteral("Unable to commit save-as package file."));
		return report;
	}

	if (request.writeManifest) {
		report.manifestPath = request.manifestPath.trimmed().isEmpty()
			? QStringLiteral("%1.manifest.json").arg(report.outputPath)
			: QFileInfo(request.manifestPath).absoluteFilePath();
		QString manifestError;
		if (exportManifest(report.manifestPath, &manifestError)) {
			report.wroteManifest = true;
		} else {
			report.warnings.push_back(manifestError.isEmpty() ? QStringLiteral("Unable to write package manifest.") : manifestError);
		}
	}
	return report;
}

QString PackageStagingModel::nextOperationId() const
{
	return QStringLiteral("stage-%1").arg(m_operations.size() + 1);
}

bool PackageStagingModel::appendOperation(PackageStageOperation operation, QString* error)
{
	if (error) {
		error->clear();
	}
	if (!m_loaded) {
		if (error) {
			*error = QStringLiteral("Load a package before staging changes.");
		}
		return false;
	}
	if (operation.id.trimmed().isEmpty()) {
		operation.id = nextOperationId();
	}
	m_operations.push_back(operation);
	return true;
}

QString packageStageOperationTypeId(PackageStageOperationType type)
{
	switch (type) {
	case PackageStageOperationType::Add:
		return QStringLiteral("add");
	case PackageStageOperationType::Replace:
		return QStringLiteral("replace");
	case PackageStageOperationType::Rename:
		return QStringLiteral("rename");
	case PackageStageOperationType::Delete:
		return QStringLiteral("delete");
	}
	return QStringLiteral("add");
}

QString packageStageOperationTypeDisplayName(PackageStageOperationType type)
{
	switch (type) {
	case PackageStageOperationType::Add:
		return QStringLiteral("Add");
	case PackageStageOperationType::Replace:
		return QStringLiteral("Replace");
	case PackageStageOperationType::Rename:
		return QStringLiteral("Rename");
	case PackageStageOperationType::Delete:
		return QStringLiteral("Delete");
	}
	return QStringLiteral("Add");
}

PackageStageOperationType packageStageOperationTypeFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("replace")) {
		return PackageStageOperationType::Replace;
	}
	if (normalized == QStringLiteral("rename")) {
		return PackageStageOperationType::Rename;
	}
	if (normalized == QStringLiteral("delete") || normalized == QStringLiteral("remove")) {
		return PackageStageOperationType::Delete;
	}
	return PackageStageOperationType::Add;
}

QString packageStageConflictResolutionId(PackageStageConflictResolution resolution)
{
	switch (resolution) {
	case PackageStageConflictResolution::Block:
		return QStringLiteral("block");
	case PackageStageConflictResolution::ReplaceExisting:
		return QStringLiteral("replace-existing");
	case PackageStageConflictResolution::Skip:
		return QStringLiteral("skip");
	}
	return QStringLiteral("block");
}

PackageStageConflictResolution packageStageConflictResolutionFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("replace") || normalized == QStringLiteral("replace-existing") || normalized == QStringLiteral("staged-wins")) {
		return PackageStageConflictResolution::ReplaceExisting;
	}
	if (normalized == QStringLiteral("skip") || normalized == QStringLiteral("keep-existing")) {
		return PackageStageConflictResolution::Skip;
	}
	return PackageStageConflictResolution::Block;
}

QString packageWriteReportText(const PackageWriteReport& report)
{
	QStringList lines;
	lines << QStringLiteral("Package save-as");
	lines << QStringLiteral("Mode: %1").arg(report.dryRun ? QStringLiteral("dry run") : QStringLiteral("write"));
	lines << QStringLiteral("Output: %1").arg(report.outputPath.isEmpty() ? QStringLiteral("not written") : report.outputPath);
	lines << QStringLiteral("Format: %1").arg(packageArchiveFormatId(report.format));
	lines << QStringLiteral("Entries: %1").arg(report.entryCount);
	lines << QStringLiteral("Bytes: %1").arg(report.bytesWritten);
	lines << QStringLiteral("SHA-256: %1").arg(report.sha256.isEmpty() ? QStringLiteral("not available") : report.sha256);
	lines << QStringLiteral("Manifest: %1").arg(report.wroteManifest ? report.manifestPath : QStringLiteral("not written"));
	if (!report.blockedMessages.isEmpty()) {
		lines << QStringLiteral("Blocked:");
		for (const QString& blocked : report.blockedMessages) {
			lines << QStringLiteral("- %1").arg(blocked);
		}
	}
	if (!report.warnings.isEmpty()) {
		lines << QStringLiteral("Warnings:");
		for (const QString& warning : report.warnings) {
			lines << QStringLiteral("- %1").arg(warning);
		}
	}
	return lines.join('\n');
}

} // namespace vibestudio
