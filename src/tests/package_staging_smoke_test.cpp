#include "core/deflate.h"
#include "core/package_archive.h"
#include "core/package_staging.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>
#include <limits>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return file.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

quint16 readLe16(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 2 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<quint16>(bytes[0] | (bytes[1] << 8));
}

quint32 readLe32(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 4 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<quint32>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
}

quint64 readLe64(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 8 > data.size()) {
		return 0;
	}
	return static_cast<quint64>(readLe32(data, offset)) | (static_cast<quint64>(readLe32(data, offset + 4)) << 32);
}

void appendLe16(QByteArray* data, quint16 value)
{
	data->append(static_cast<char>(value & 0xff));
	data->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* data, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) {
		data->append(static_cast<char>((value >> shift) & 0xff));
	}
}

QString fixedLatin1String(const char* data, qsizetype maxSize)
{
	qsizetype size = 0;
	while (size < maxSize && data[size] != '\0') {
		++size;
	}
	return QString::fromLatin1(data, size).trimmed();
}

struct WadRecord {
	QString name;
	quint8 type = 0;
	quint32 size = 0;
};

// Reads a written WAD directory back. Doom records are 16 bytes
// (offset, size, name[8]); WAD2/WAD3 records are 32 bytes
// (offset, diskSize, size, type, compression, pad, name[16]).
QVector<WadRecord> wadDirectory(const QString& path, QString* magicOut)
{
	const QByteArray bytes = readFile(path);
	if (bytes.size() < 12) {
		return {};
	}
	const QString magic = QString::fromLatin1(bytes.constData(), 4);
	if (magicOut) {
		*magicOut = magic;
	}
	const bool textureWad = magic == QStringLiteral("WAD2") || magic == QStringLiteral("WAD3");
	const int recordSize = textureWad ? 32 : 16;
	const int nameOffset = textureWad ? 16 : 8;
	const int nameLimit = textureWad ? 16 : 8;
	const int count = static_cast<int>(readLe32(bytes, 4));
	const qsizetype directoryOffset = static_cast<qsizetype>(readLe32(bytes, 8));
	QVector<WadRecord> records;
	for (int index = 0; index < count; ++index) {
		const qsizetype record = directoryOffset + (static_cast<qsizetype>(index) * recordSize);
		if (record + recordSize > bytes.size()) {
			return {};
		}
		WadRecord entry;
		entry.name = fixedLatin1String(bytes.constData() + record + nameOffset, nameLimit);
		entry.size = readLe32(bytes, record + 4);
		entry.type = textureWad ? static_cast<quint8>(bytes[record + 12]) : 0;
		records.push_back(entry);
	}
	return records;
}

QStringList wadDirectoryNames(const QString& path)
{
	QStringList names;
	for (const WadRecord& record : wadDirectory(path, nullptr)) {
		names.push_back(record.name);
	}
	return names;
}

struct PakInput {
	QString name;
	QByteArray data;
};

// Minimal Quake PAK fixture writer: "PACK", directory offset/length, payload,
// then 64-byte records of name[56] + offset + size.
bool buildPak(const QString& path, const QVector<PakInput>& inputs)
{
	QByteArray payload;
	QByteArray directory;
	for (const PakInput& input : inputs) {
		const quint32 offset = static_cast<quint32>(12 + payload.size());
		payload.append(input.data);
		const QByteArray name = input.name.toLatin1();
		const qsizetype start = directory.size();
		directory.append(name);
		while (directory.size() - start < 56) {
			directory.append('\0');
		}
		appendLe32(&directory, offset);
		appendLe32(&directory, static_cast<quint32>(input.data.size()));
	}
	QByteArray bytes;
	bytes.append("PACK");
	appendLe32(&bytes, static_cast<quint32>(12 + payload.size()));
	appendLe32(&bytes, static_cast<quint32>(directory.size()));
	bytes.append(payload);
	bytes.append(directory);
	return writeFile(path, bytes);
}

QStringList pakDirectoryNames(const QString& path)
{
	const QByteArray bytes = readFile(path);
	if (bytes.size() < 12) {
		return {};
	}
	const qsizetype directoryOffset = static_cast<qsizetype>(readLe32(bytes, 4));
	const qsizetype directoryLength = static_cast<qsizetype>(readLe32(bytes, 8));
	QStringList names;
	for (qsizetype cursor = 0; cursor + 64 <= directoryLength; cursor += 64) {
		const qsizetype record = directoryOffset + cursor;
		if (record + 64 > bytes.size()) {
			return {};
		}
		names.push_back(fixedLatin1String(bytes.constData() + record, 56));
	}
	return names;
}

struct ZipInput {
	QString name;
	QByteArray data;
	bool directory = false;
};

// Minimal stored-only ZIP fixture writer (PKWARE APPNOTE.TXT sections 4.3.7,
// 4.3.12 and 4.3.16), used to build base archives that contain a real empty
// directory record.
bool buildStoredZip(const QString& path, const QVector<ZipInput>& inputs)
{
	QByteArray data;
	QByteArray central;
	for (const ZipInput& input : inputs) {
		QByteArray name = input.name.toUtf8();
		if (input.directory && !name.endsWith('/')) {
			name.append('/');
		}
		const quint32 localOffset = static_cast<quint32>(data.size());
		const quint32 size = input.directory ? 0u : static_cast<quint32>(input.data.size());
		const quint32 crc = input.directory ? 0u : crc32Bytes(input.data);
		const quint16 fixedDate = static_cast<quint16>((1 << 5) | 1);

		appendLe32(&data, 0x04034b50);
		appendLe16(&data, 20);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, fixedDate);
		appendLe32(&data, crc);
		appendLe32(&data, size);
		appendLe32(&data, size);
		appendLe16(&data, static_cast<quint16>(name.size()));
		appendLe16(&data, 0);
		data.append(name);
		if (!input.directory) {
			data.append(input.data);
		}

		appendLe32(&central, 0x02014b50);
		appendLe16(&central, 20);
		appendLe16(&central, 20);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, fixedDate);
		appendLe32(&central, crc);
		appendLe32(&central, size);
		appendLe32(&central, size);
		appendLe16(&central, static_cast<quint16>(name.size()));
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe16(&central, 0);
		appendLe32(&central, input.directory ? 0x10u : 0u);
		appendLe32(&central, localOffset);
		central.append(name);
	}

	const quint32 centralOffset = static_cast<quint32>(data.size());
	data.append(central);
	appendLe32(&data, 0x06054b50);
	appendLe16(&data, 0);
	appendLe16(&data, 0);
	appendLe16(&data, static_cast<quint16>(inputs.size()));
	appendLe16(&data, static_cast<quint16>(inputs.size()));
	appendLe32(&data, static_cast<quint32>(central.size()));
	appendLe32(&data, centralOffset);
	appendLe16(&data, 0);
	return writeFile(path, data);
}

// Reads the ZIP64 end-of-central-directory record that sits directly in front
// of the ZIP64 locator and the classic end record (APPNOTE.TXT 4.3.14-4.3.16).
bool readZip64TotalEntries(const QString& path, quint64* totalEntries, quint16* legacyEntries)
{
	const QByteArray bytes = readFile(path);
	if (bytes.size() < 22 + 20 + 56) {
		return false;
	}
	const qsizetype eocd = bytes.size() - 22;
	if (readLe32(bytes, eocd) != 0x06054b50) {
		return false;
	}
	if (legacyEntries) {
		*legacyEntries = readLe16(bytes, eocd + 10);
	}
	const qsizetype locator = eocd - 20;
	if (readLe32(bytes, locator) != 0x07064b50) {
		return false;
	}
	const qsizetype zip64 = static_cast<qsizetype>(readLe64(bytes, locator + 8));
	if (zip64 < 0 || zip64 + 56 > bytes.size() || readLe32(bytes, zip64) != 0x06064b50) {
		return false;
	}
	if (totalEntries) {
		*totalEntries = readLe64(bytes, zip64 + 32);
	}
	return true;
}

QByteArray incompressibleBytes(int size)
{
	QByteArray bytes;
	bytes.reserve(size);
	quint32 state = 0x13572468u;
	for (int index = 0; index < size; ++index) {
		state = (state * 1103515245u) + 12345u;
		bytes.append(static_cast<char>((state >> 16) & 0xff));
	}
	return bytes;
}

QString storageMethodFor(const PackageArchive& archive, const QString& virtualPath)
{
	for (const PackageEntry& entry : archive.entries()) {
		if (entry.virtualPath.compare(virtualPath, Qt::CaseSensitive) == 0) {
			return entry.storageMethod;
		}
	}
	return {};
}

bool hasRealDirectoryEntry(const PackageArchive& archive, const QString& virtualPath)
{
	for (const PackageEntry& entry : archive.entries()) {
		QString path = entry.virtualPath;
		while (path.endsWith('/')) {
			path.chop(1);
		}
		if (entry.kind == PackageEntryKind::Directory && path == virtualPath && entry.storageMethod != QStringLiteral("synthetic")) {
			return true;
		}
	}
	return false;
}

// A reader that reports one real file plus a large number of real directory
// records, so the ZIP64 entry-count path can be exercised without creating tens
// of thousands of files on disk.
class BulkDirectoryReader final : public PackageArchiveReader
{
public:
	BulkDirectoryReader(const QString& folderPath, int directoryCount)
		: m_sourcePath(folderPath)
	{
		PackageEntry file;
		file.virtualPath = QStringLiteral("seed.bin");
		file.kind = PackageEntryKind::File;
		file.sizeBytes = 4;
		file.storageMethod = QStringLiteral("file");
		file.readable = true;
		m_entries.push_back(file);
		for (int index = 0; index < directoryCount; ++index) {
			PackageEntry directory;
			directory.virtualPath = QStringLiteral("bulk/%1/").arg(index, 6, 10, QLatin1Char('0'));
			directory.kind = PackageEntryKind::Directory;
			directory.storageMethod = QStringLiteral("stored");
			directory.readable = false;
			m_entries.push_back(directory);
		}
	}

	[[nodiscard]] PackageArchiveFormat format() const override
	{
		return PackageArchiveFormat::Folder;
	}

	[[nodiscard]] QString sourcePath() const override
	{
		return m_sourcePath;
	}

	[[nodiscard]] bool isOpen() const override
	{
		return true;
	}

	[[nodiscard]] QVector<PackageEntry> entries() const override
	{
		return m_entries;
	}

	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override
	{
		Q_UNUSED(maxBytes);
		if (virtualPath != QStringLiteral("seed.bin")) {
			if (error) {
				*error = QStringLiteral("unknown entry");
			}
			return false;
		}
		if (out) {
			*out = QByteArray("seed");
		}
		return true;
	}

private:
	QString m_sourcePath;
	QVector<PackageEntry> m_entries;
};

// A reader that claims one huge file without ever producing its bytes, so the
// writers' 32-bit directory bounds can be exercised without staging gigabytes.
class OversizedEntryReader final : public PackageArchiveReader
{
public:
	OversizedEntryReader(const QString& folderPath, const QString& entryName, quint64 sizeBytes)
		: m_sourcePath(folderPath)
	{
		PackageEntry file;
		file.virtualPath = entryName;
		file.kind = PackageEntryKind::File;
		file.sizeBytes = sizeBytes;
		file.storageMethod = QStringLiteral("file");
		file.readable = true;
		m_entries.push_back(file);
	}

	[[nodiscard]] PackageArchiveFormat format() const override
	{
		return PackageArchiveFormat::Folder;
	}

	[[nodiscard]] QString sourcePath() const override
	{
		return m_sourcePath;
	}

	[[nodiscard]] bool isOpen() const override
	{
		return true;
	}

	[[nodiscard]] QVector<PackageEntry> entries() const override
	{
		return m_entries;
	}

	bool readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes = -1) const override
	{
		Q_UNUSED(virtualPath);
		Q_UNUSED(out);
		Q_UNUSED(maxBytes);
		if (error) {
			// The writers must reject the plan before they ever get here.
			*error = QStringLiteral("oversized entry bytes should never be requested");
		}
		return false;
	}

private:
	QString m_sourcePath;
	QVector<PackageEntry> m_entries;
};

PackageWriteReport writePackage(const PackageStagingModel& staging, const QString& outputPath, PackageArchiveFormat format)
{
	PackageWriteRequest request;
	request.destinationPath = outputPath;
	request.format = format;
	request.writeManifest = true;
	return staging.writeArchive(request);
}

} // namespace

int main()
{
	bool ok = true;
	QTemporaryDir tempDir;
	ok &= expect(tempDir.isValid(), "temporary directory should be valid");
	QDir root(tempDir.path());
	ok &= expect(root.mkpath(QStringLiteral("source/maps")), "source maps directory should be created");
	ok &= expect(root.mkpath(QStringLiteral("source/textures")), "source textures directory should be created");
	ok &= expect(writeFile(root.filePath(QStringLiteral("source/maps/start.map")), QByteArray("{\n}\n")), "base map should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("source/textures/wall.txt")), QByteArray("wall")), "base texture should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("new.cfg")), QByteArray("exec config\n")), "add source should be written");
	ok &= expect(writeFile(root.filePath(QStringLiteral("replacement.txt")), QByteArray("stone")), "replace source should be written");

	PackageArchive archive;
	QString error;
	ok &= expect(archive.load(root.filePath(QStringLiteral("source")), &error), "folder package should load");

	PackageStagingModel staging;
	ok &= expect(staging.loadBaseArchive(archive, &error), "staging should load base archive");
	ok &= expect(staging.summary().baseFileCount == 2, "base file count mismatch");
	ok &= expect(staging.addFile(root.filePath(QStringLiteral("new.cfg")), QStringLiteral("scripts/autoexec.cfg"), &error), "add operation should stage");
	ok &= expect(staging.replaceFile(QStringLiteral("textures/wall.txt"), root.filePath(QStringLiteral("replacement.txt")), &error), "replace operation should stage");
	ok &= expect(staging.renameEntry(QStringLiteral("maps/start.map"), QStringLiteral("maps/e1m1.map"), &error), "rename operation should stage");
	ok &= expect(staging.deleteEntry(QStringLiteral("scripts/missing.cfg"), &error, PackageStageConflictResolution::Skip), "delete skip operation should stage");
	const PackageStagingSummary summary = staging.summary();
	ok &= expect(summary.operationCount == 4, "operation count mismatch");
	ok &= expect(summary.stagedFileCount == 3, "planned file count mismatch");
	ok &= expect(summary.conflictCount == 1 && summary.blockingCount == 0, "skip conflict should be non-blocking");
	ok &= expect(summary.canSave, "staging summary should be saveable");
	ok &= expect(!staging.manifestJson().isEmpty(), "staging manifest JSON should be generated");
	ok &= expect(!staging.beforeComposition().isEmpty() && !staging.afterComposition().isEmpty(), "composition buckets should exist");

	ok &= expect(staging.exportManifest(root.filePath(QStringLiteral("stage-manifest.json")), &error), "manifest should export");
	ok &= expect(QFileInfo::exists(root.filePath(QStringLiteral("stage-manifest.json"))), "manifest file should exist");

	PackageWriteReport blockedSamePath = writePackage(staging, archive.sourcePath(), PackageArchiveFormat::Pak);
	ok &= expect(!blockedSamePath.succeeded() && !blockedSamePath.blockedMessages.isEmpty(), "save-as to source path should be blocked");

	const QString pakPathA = root.filePath(QStringLiteral("out-a.pak"));
	const QString pakPathB = root.filePath(QStringLiteral("out-b.pak"));
	PackageWriteReport pakReportA = writePackage(staging, pakPathA, PackageArchiveFormat::Pak);
	PackageWriteReport pakReportB = writePackage(staging, pakPathB, PackageArchiveFormat::Pak);
	ok &= expect(pakReportA.succeeded() && pakReportB.succeeded(), "PAK save-as should succeed");
	ok &= expect(readFile(pakPathA) == readFile(pakPathB), "PAK writer should be deterministic");
	ok &= expect(pakReportA.deterministic, "PAK report should claim reproducible output");
	ok &= expect(QFileInfo::exists(pakReportA.manifestPath), "PAK manifest should be written");
	ok &= expect(archive.load(pakPathA, &error), "written PAK should load");
	QByteArray bytes;
	ok &= expect(archive.readEntryBytes(QStringLiteral("scripts/autoexec.cfg"), &bytes, &error), "PAK added entry should read");
	ok &= expect(bytes == QByteArray("exec config\n"), "PAK added bytes mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("textures/wall.txt"), &bytes, &error), "PAK replaced entry should read");
	ok &= expect(bytes == QByteArray("stone"), "PAK replaced bytes mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("maps/e1m1.map"), &bytes, &error), "PAK renamed entry should read");

	// Operation ids must stay unique across clear/add cycles, and clearing must
	// remove exactly one operation.
	{
		QStringList seenIds;
		for (const PackageStageOperation& operation : staging.operations()) {
			seenIds.push_back(operation.id);
		}
		ok &= expect(seenIds.size() == 4, "four staged operations expected before clearing");
		const QString secondId = seenIds.value(1);
		ok &= expect(staging.clearOperation(secondId), "clearing an existing operation should report success");
		ok &= expect(staging.operations().size() == 3, "clearing should remove exactly one operation");
		ok &= expect(!staging.clearOperation(secondId), "clearing a removed operation should report failure");
		ok &= expect(staging.addFile(root.filePath(QStringLiteral("new.cfg")), QStringLiteral("scripts/extra.cfg"), &error), "post-clear add should stage");
		QStringList idsAfter;
		for (const PackageStageOperation& operation : staging.operations()) {
			ok &= expect(!idsAfter.contains(operation.id), "operation ids must never repeat");
			ok &= expect(!seenIds.contains(operation.id) || operation.id != secondId, "cleared operation id must not come back");
			idsAfter.push_back(operation.id);
		}
		ok &= expect(!idsAfter.contains(secondId), "a reused operation id would collide with the cleared one");
		ok &= expect(staging.clearOperation(idsAfter.last()), "the post-clear operation should clear");
	}

	const QString dryRunPath = root.filePath(QStringLiteral("dry-run.pak"));
	PackageWriteRequest dryRunRequest;
	dryRunRequest.destinationPath = dryRunPath;
	dryRunRequest.format = PackageArchiveFormat::Pak;
	dryRunRequest.writeManifest = true;
	dryRunRequest.dryRun = true;
	const PackageWriteReport dryRunReport = staging.writeArchive(dryRunRequest);
	ok &= expect(dryRunReport.succeeded() && dryRunReport.dryRun, "PAK dry-run report should succeed");
	ok &= expect(!QFileInfo::exists(dryRunPath), "PAK dry-run should not write output");
	ok &= expect(!dryRunReport.wroteManifest, "PAK dry-run should not write manifest");
	ok &= expect(!QFileInfo::exists(QStringLiteral("%1.manifest.json").arg(QFileInfo(dryRunPath).absoluteFilePath())), "PAK dry-run should not leave a manifest behind");
	ok &= expect(!dryRunReport.sha256.isEmpty() && dryRunReport.bytesWritten > 0, "PAK dry-run should still report size and digest");

	const QString pk3PathA = root.filePath(QStringLiteral("out-a.pk3"));
	const QString pk3PathB = root.filePath(QStringLiteral("out-b.pk3"));
	PackageWriteReport pk3ReportA = writePackage(staging, pk3PathA, PackageArchiveFormat::Pk3);
	PackageWriteReport pk3ReportB = writePackage(staging, pk3PathB, PackageArchiveFormat::Pk3);
	ok &= expect(pk3ReportA.succeeded() && pk3ReportB.succeeded(), "PK3 save-as should succeed");
	ok &= expect(readFile(pk3PathA) == readFile(pk3PathB), "PK3 writer should be deterministic");
	ok &= expect(pk3ReportA.sha256 == pk3ReportB.sha256, "PK3 digests should match across writes");
	ok &= expect(archive.load(pk3PathA, &error), "written PK3 should load");
	ok &= expect(archive.readEntryBytes(QStringLiteral("scripts/autoexec.cfg"), &bytes, &error), "PK3 added entry should read");
	ok &= expect(bytes == QByteArray("exec config\n"), "PK3 added bytes mismatch");

	PackageStagingModel conflictStaging;
	ok &= expect(conflictStaging.loadBaseArchive(archive, &error), "conflict staging should load");
	ok &= expect(conflictStaging.addFile(root.filePath(QStringLiteral("new.cfg")), QStringLiteral("scripts/autoexec.cfg"), &error), "conflicting add should stage");
	ok &= expect(!conflictStaging.summary().canSave && conflictStaging.summary().blockingCount == 1, "conflicting add should block save");
	PackageStagingModel resolvedStaging;
	ok &= expect(resolvedStaging.loadBaseArchive(archive, &error), "resolved staging should load");
	ok &= expect(resolvedStaging.addFile(root.filePath(QStringLiteral("new.cfg")), QStringLiteral("scripts/autoexec.cfg"), &error, PackageStageConflictResolution::ReplaceExisting), "resolved add should stage");
	ok &= expect(resolvedStaging.summary().canSave, "replace-existing resolution should be saveable");

	// Deflate round-trip, stored fallback, and byte-identical repeat writes.
	{
		QDir compressSource(root.filePath(QStringLiteral("compress-source")));
		ok &= expect(root.mkpath(QStringLiteral("compress-source")), "compression source directory should be created");
		const QByteArray compressible = QByteArray(6000, 'A') + QByteArray("vibestudio");
		const QByteArray random = incompressibleBytes(4096);
		ok &= expect(writeFile(compressSource.filePath(QStringLiteral("repeat.txt")), compressible), "compressible fixture should be written");
		ok &= expect(writeFile(compressSource.filePath(QStringLiteral("noise.bin")), random), "incompressible fixture should be written");

		PackageArchive compressArchive;
		ok &= expect(compressArchive.load(compressSource.path(), &error), "compression source folder should load");
		PackageStagingModel compressStaging;
		ok &= expect(compressStaging.loadBaseArchive(compressArchive, &error), "compression staging should load");

		const QString deflatedA = root.filePath(QStringLiteral("deflated-a.pk3"));
		const QString deflatedB = root.filePath(QStringLiteral("deflated-b.pk3"));
		PackageWriteRequest deflateRequest;
		deflateRequest.destinationPath = deflatedA;
		deflateRequest.format = PackageArchiveFormat::Pk3;
		deflateRequest.compression = DeflateLevel::Default;
		deflateRequest.verifyDeterminism = true;
		const PackageWriteReport deflateReportA = compressStaging.writeArchive(deflateRequest);
		deflateRequest.destinationPath = deflatedB;
		const PackageWriteReport deflateReportB = compressStaging.writeArchive(deflateRequest);
		ok &= expect(deflateReportA.succeeded() && deflateReportB.succeeded(), "deflate PK3 writes should succeed");
		ok &= expect(readFile(deflatedA) == readFile(deflatedB), "deflate PK3 output should be byte-identical across writes");
		ok &= expect(deflateReportA.determinismVerified && deflateReportA.deterministic, "verified determinism should be reported");
		ok &= expect(deflateReportA.compressionId == deflateLevelId(DeflateLevel::Default), "deflate level should be reported");
		ok &= expect(deflateReportA.compressionRatio < 1.0, "deflate should shrink the payload overall");
		ok &= expect(deflateReportA.uncompressedBytes == static_cast<quint64>(compressible.size() + random.size()), "uncompressed byte total mismatch");

		PackageArchive deflatedArchive;
		ok &= expect(deflatedArchive.load(deflatedA, &error), "deflated PK3 should load");
		ok &= expect(deflatedArchive.readEntryBytes(QStringLiteral("repeat.txt"), &bytes, &error), "deflated entry should read back");
		ok &= expect(bytes == compressible, "deflated round-trip bytes mismatch");
		ok &= expect(deflatedArchive.readEntryBytes(QStringLiteral("noise.bin"), &bytes, &error), "stored fallback entry should read back");
		ok &= expect(bytes == random, "stored fallback round-trip bytes mismatch");
		ok &= expect(storageMethodFor(deflatedArchive, QStringLiteral("repeat.txt")) == QStringLiteral("deflated"), "compressible entry should use deflate");
		ok &= expect(storageMethodFor(deflatedArchive, QStringLiteral("noise.bin")) == QStringLiteral("stored"), "incompressible entry should fall back to stored");

		const QString storedPath = root.filePath(QStringLiteral("stored.pk3"));
		PackageWriteRequest storeRequest;
		storeRequest.destinationPath = storedPath;
		storeRequest.format = PackageArchiveFormat::Pk3;
		storeRequest.compression = DeflateLevel::Store;
		const PackageWriteReport storeReport = compressStaging.writeArchive(storeRequest);
		ok &= expect(storeReport.succeeded(), "stored PK3 write should succeed");
		ok &= expect(storeReport.bytesWritten > deflateReportA.bytesWritten, "stored output should be larger than deflated output");
		PackageArchive storedArchive;
		ok &= expect(storedArchive.load(storedPath, &error), "stored PK3 should load");
		ok &= expect(storageMethodFor(storedArchive, QStringLiteral("repeat.txt")) == QStringLiteral("stored"), "store level should not deflate");

		// Preserving source timestamps changes the bytes and is not claimed as
		// reproducible.
		const QString stampedPath = root.filePath(QStringLiteral("stamped.pk3"));
		PackageWriteRequest stampedRequest;
		stampedRequest.destinationPath = stampedPath;
		stampedRequest.format = PackageArchiveFormat::Pk3;
		stampedRequest.timestampMode = PackageTimestampMode::PreserveSource;
		const PackageWriteReport stampedReport = compressStaging.writeArchive(stampedRequest);
		ok &= expect(stampedReport.succeeded(), "timestamp-preserving PK3 write should succeed");
		ok &= expect(!stampedReport.deterministic, "preserved timestamps must not claim reproducible output");
		ok &= expect(readFile(stampedPath) != readFile(deflatedA), "preserved timestamps should change the written bytes");
	}

	// Case-differing duplicate entries must land in exactly one stable order.
	{
		const QString duplicatePak = root.filePath(QStringLiteral("duplicates.pak"));
		ok &= expect(buildPak(duplicatePak, {
							  {QStringLiteral("a/file.txt"), QByteArray("AAA")},
							  {QStringLiteral("a/FILE.txt"), QByteArray("BBB")},
							  {QStringLiteral("a/File.txt"), QByteArray("CCC")},
						  }),
			"duplicate-case PAK fixture should be written");
		PackageArchive duplicateArchive;
		ok &= expect(duplicateArchive.load(duplicatePak, &error), "duplicate-case PAK should load");
		PackageStagingModel duplicateStaging;
		ok &= expect(duplicateStaging.loadBaseArchive(duplicateArchive, &error), "duplicate-case staging should load");
		ok &= expect(duplicateStaging.summary().baseFileCount == 3, "case-differing duplicates should all be preserved");
		QStringList plannedOrder;
		for (const PackageStagedEntry& entry : duplicateStaging.plannedEntries()) {
			plannedOrder.push_back(entry.virtualPath);
		}
		const QStringList expectedOrder = {QStringLiteral("a/FILE.txt"), QStringLiteral("a/File.txt"), QStringLiteral("a/file.txt")};
		ok &= expect(plannedOrder == expectedOrder, "case-differing duplicates should use a total order");

		const QString duplicateOutA = root.filePath(QStringLiteral("duplicates-out-a.pak"));
		const QString duplicateOutB = root.filePath(QStringLiteral("duplicates-out-b.pak"));
		ok &= expect(writePackage(duplicateStaging, duplicateOutA, PackageArchiveFormat::Pak).succeeded(), "duplicate-case PAK write should succeed");
		ok &= expect(writePackage(duplicateStaging, duplicateOutB, PackageArchiveFormat::Pak).succeeded(), "second duplicate-case PAK write should succeed");
		ok &= expect(pakDirectoryNames(duplicateOutA) == expectedOrder, "written PAK directory order mismatch");
		ok &= expect(readFile(duplicateOutA) == readFile(duplicateOutB), "duplicate-case output should be byte-identical across writes");
	}

	// Real directory records survive a ZIP/PK3 round-trip.
	{
		const QString dirZip = root.filePath(QStringLiteral("with-directory.zip"));
		ok &= expect(buildStoredZip(dirZip, {
							  {QStringLiteral("empty"), QByteArray(), true},
							  {QStringLiteral("data/file.txt"), QByteArray("payload"), false},
						  }),
			"directory ZIP fixture should be written");
		PackageArchive dirArchive;
		ok &= expect(dirArchive.load(dirZip, &error), "directory ZIP fixture should load");
		ok &= expect(hasRealDirectoryEntry(dirArchive, QStringLiteral("empty")), "fixture should contain a real directory record");
		PackageStagingModel dirStaging;
		ok &= expect(dirStaging.loadBaseArchive(dirArchive, &error), "directory staging should load");
		ok &= expect(dirStaging.summary().baseDirectoryCount == 1, "one real directory record should be preserved");
		ok &= expect(dirStaging.summary().baseFileCount == 1, "directory records must not be counted as files");

		const QString dirOut = root.filePath(QStringLiteral("with-directory-out.pk3"));
		const PackageWriteReport dirReport = writePackage(dirStaging, dirOut, PackageArchiveFormat::Pk3);
		ok &= expect(dirReport.succeeded(), "directory-preserving write should succeed");
		ok &= expect(dirReport.directoryCount == 1, "directory record should be written");
		ok &= expect(dirReport.entryCount == 1, "file count should exclude directory records");
		PackageArchive dirRoundTrip;
		ok &= expect(dirRoundTrip.load(dirOut, &error), "directory-preserving output should load");
		ok &= expect(hasRealDirectoryEntry(dirRoundTrip, QStringLiteral("empty")), "empty directory should survive the round-trip");
		ok &= expect(dirRoundTrip.readEntryBytes(QStringLiteral("data/file.txt"), &bytes, &error) && bytes == QByteArray("payload"), "directory fixture file should round-trip");
	}

	// WAD2 texture WADs keep their magic, 16-character names, and lump types.
	{
		const QString wad2Path = root.filePath(QStringLiteral("textures.wad"));
		const QByteArray lumpA = QByteArray("miptex-alpha");
		const QByteArray lumpB = QByteArray("miptex-beta!");
		QByteArray wad2;
		wad2.append("WAD2");
		appendLe32(&wad2, 2);
		appendLe32(&wad2, static_cast<quint32>(12 + lumpA.size() + lumpB.size()));
		wad2.append(lumpA);
		wad2.append(lumpB);
		const QStringList wadNames = {QStringLiteral("texture_16_chars"), QStringLiteral("city2_5")};
		quint32 offset = 12;
		int lumpIndex = 0;
		for (const QByteArray& lump : {lumpA, lumpB}) {
			appendLe32(&wad2, offset);
			appendLe32(&wad2, static_cast<quint32>(lump.size()));
			appendLe32(&wad2, static_cast<quint32>(lump.size()));
			wad2.append(static_cast<char>(0x44));
			wad2.append('\0');
			appendLe16(&wad2, 0);
			const QByteArray name = wadNames.value(lumpIndex).toLatin1();
			const qsizetype start = wad2.size();
			wad2.append(name);
			while (wad2.size() - start < 16) {
				wad2.append('\0');
			}
			offset += static_cast<quint32>(lump.size());
			++lumpIndex;
		}
		ok &= expect(writeFile(wad2Path, wad2), "WAD2 fixture should be written");

		PackageArchive wad2Archive;
		ok &= expect(wad2Archive.load(wad2Path, &error), "WAD2 fixture should load");
		PackageStagingModel wad2Staging;
		ok &= expect(wad2Staging.loadBaseArchive(wad2Archive, &error), "WAD2 staging should load");
		ok &= expect(wad2Staging.sourceWadMagic() == QStringLiteral("WAD2"), "source WAD magic should be detected");
		ok &= expect(wad2Staging.summary().baseFileCount == 2, "WAD2 lumps should be staged");

		const QString wad2Out = root.filePath(QStringLiteral("textures-out.wad"));
		const PackageWriteReport wad2Report = writePackage(wad2Staging, wad2Out, PackageArchiveFormat::Wad);
		ok &= expect(wad2Report.succeeded(), "WAD2 save-as should succeed");
		ok &= expect(wad2Report.wadMagic == QStringLiteral("WAD2"), "WAD2 magic should be preserved in the report");
		QString roundTripMagic;
		const QVector<WadRecord> records = wadDirectory(wad2Out, &roundTripMagic);
		ok &= expect(roundTripMagic == QStringLiteral("WAD2"), "written WAD should keep the source magic");
		ok &= expect(records.size() == 2, "written WAD2 should hold two lumps");
		QStringList roundTripNames;
		for (const WadRecord& record : records) {
			roundTripNames.push_back(record.name);
			ok &= expect(record.type == 0x44, "WAD2 lump type byte should be preserved");
		}
		ok &= expect(roundTripNames.contains(QStringLiteral("texture_16_chars")), "16-character WAD2 name should survive");
		PackageArchive wad2RoundTrip;
		ok &= expect(wad2RoundTrip.load(wad2Out, &error), "written WAD2 should load");
		ok &= expect(wad2RoundTrip.readEntryBytes(QStringLiteral("texture_16_chars"), &bytes, &error), "16-character WAD2 lump should read");
		ok &= expect(bytes == lumpA, "WAD2 lump bytes mismatch");

		// A Doom PWAD source with a 16-character lump name must still be
		// rejected rather than silently truncated.
		PackageWriteRequest doomRequest;
		doomRequest.destinationPath = root.filePath(QStringLiteral("textures-doom.wad"));
		doomRequest.format = PackageArchiveFormat::Wad;
		doomRequest.wadMagic = QStringLiteral("PWAD");
		const PackageWriteReport doomReport = wad2Staging.writeArchive(doomRequest);
		ok &= expect(!doomReport.succeeded() && !doomReport.blockedMessages.isEmpty(), "Doom WAD output should reject 16-character lump names");
	}

	// Doom PWAD output keeps map lump ordering and the requested magic.
	QDir wadSource(root.filePath(QStringLiteral("wad-source")));
	ok &= expect(root.mkpath(QStringLiteral("wad-source")), "WAD source directory should be created");
	ok &= expect(writeFile(wadSource.filePath(QStringLiteral("MAP01")), QByteArray("map")), "MAP01 lump should be written");
	ok &= expect(writeFile(wadSource.filePath(QStringLiteral("THINGS")), QByteArray("things")), "THINGS lump should be written");
	ok &= expect(writeFile(wadSource.filePath(QStringLiteral("LINEDEFS")), QByteArray("linedefs")), "LINEDEFS lump should be written");
	ok &= expect(archive.load(wadSource.path(), &error), "WAD source folder should load");
	PackageStagingModel wadStaging;
	ok &= expect(wadStaging.loadBaseArchive(archive, &error), "WAD staging should load");
	const QString wadPath = root.filePath(QStringLiteral("map.wad"));
	PackageWriteReport wadReport = writePackage(wadStaging, wadPath, PackageArchiveFormat::Wad);
	ok &= expect(wadReport.succeeded(), "tested PWAD save-as should succeed");
	ok &= expect(wadReport.wadMagic == QStringLiteral("PWAD"), "folder sources should default to PWAD");
	ok &= expect(wadDirectoryNames(wadPath) == QStringList({QStringLiteral("MAP01"), QStringLiteral("THINGS"), QStringLiteral("LINEDEFS")}), "WAD map lump order should be preserved");
	ok &= expect(archive.load(wadPath, &error), "written WAD should load");
	ok &= expect(archive.readEntryBytes(QStringLiteral("THINGS"), &bytes, &error), "written WAD lump should read");
	ok &= expect(bytes == QByteArray("things"), "written WAD lump bytes mismatch");

	const QString iwadPath = root.filePath(QStringLiteral("map-iwad.wad"));
	PackageWriteRequest iwadRequest;
	iwadRequest.destinationPath = iwadPath;
	iwadRequest.format = PackageArchiveFormat::Wad;
	iwadRequest.wadMagic = QStringLiteral("IWAD");
	ok &= expect(wadStaging.writeArchive(iwadRequest).succeeded(), "IWAD save-as should succeed");
	ok &= expect(readFile(iwadPath).left(4) == QByteArray("IWAD"), "requested IWAD magic should be written");

	// Doom keeps a map's lumps in the run that follows its marker
	// (https://doomwiki.org/wiki/WAD), and the staging plan addresses lumps by
	// name only, so it cannot express two maps that both hold THINGS/LINEDEFS.
	// Writing one anyway produced an interleaved, unreadable WAD, so the writer
	// must block instead.
	{
		QDir multiMapSource(root.filePath(QStringLiteral("multimap-source")));
		ok &= expect(root.mkpath(QStringLiteral("multimap-source")), "multi-map source directory should be created");
		ok &= expect(writeFile(multiMapSource.filePath(QStringLiteral("MAP01")), QByteArray("one")), "MAP01 marker should be written");
		ok &= expect(writeFile(multiMapSource.filePath(QStringLiteral("MAP02")), QByteArray("two")), "MAP02 marker should be written");
		ok &= expect(writeFile(multiMapSource.filePath(QStringLiteral("THINGS")), QByteArray("things")), "THINGS lump should be written");
		ok &= expect(writeFile(multiMapSource.filePath(QStringLiteral("LINEDEFS")), QByteArray("linedefs")), "LINEDEFS lump should be written");
		PackageArchive multiMapArchive;
		ok &= expect(multiMapArchive.load(multiMapSource.path(), &error), "multi-map source folder should load");
		PackageStagingModel multiMapStaging;
		ok &= expect(multiMapStaging.loadBaseArchive(multiMapArchive, &error), "multi-map staging should load");

		const QString multiMapPath = root.filePath(QStringLiteral("multimap.wad"));
		const PackageWriteReport multiMapReport = writePackage(multiMapStaging, multiMapPath, PackageArchiveFormat::Wad);
		ok &= expect(!multiMapReport.succeeded() && !multiMapReport.blockedMessages.isEmpty(), "multi-map Doom WAD output should be blocked");
		ok &= expect(!QFileInfo::exists(multiMapPath), "a blocked WAD write must not leave an output file behind");
	}

	// PAK and WAD directory offsets and sizes are signed int32 in both formats
	// and in this repo's readers, so anything above INT32_MAX must be refused
	// rather than written as a value with the high bit set.
	{
		QDir oversizedSource(root.filePath(QStringLiteral("oversized-source")));
		ok &= expect(root.mkpath(QStringLiteral("oversized-source")), "oversized source directory should be created");

		const quint64 overSignedLimit = static_cast<quint64>(std::numeric_limits<qint32>::max()) + 1;
		OversizedEntryReader pakReader(oversizedSource.path(), QStringLiteral("huge.bin"), overSignedLimit);
		PackageStagingModel oversizedPakStaging;
		ok &= expect(oversizedPakStaging.loadBaseArchive(pakReader, &error), "oversized PAK staging should load");
		const PackageWriteReport oversizedPakReport = writePackage(oversizedPakStaging, root.filePath(QStringLiteral("oversized.pak")), PackageArchiveFormat::Pak);
		ok &= expect(!oversizedPakReport.succeeded() && !oversizedPakReport.blockedMessages.isEmpty(), "a PAK entry above INT32_MAX should be blocked");
		// The message matters: without the signed bound the write still fails,
		// but only later and for the wrong reason (the bytes cannot be read).
		ok &= expect(oversizedPakReport.blockedMessages.join('\n').contains(QStringLiteral("PAK entry size exceeds the signed 32-bit limit")), "a PAK entry above INT32_MAX should be blocked by the size bound");

		OversizedEntryReader wadReader(oversizedSource.path(), QStringLiteral("HUGELUMP"), overSignedLimit);
		PackageStagingModel oversizedWadStaging;
		ok &= expect(oversizedWadStaging.loadBaseArchive(wadReader, &error), "oversized WAD staging should load");
		const PackageWriteReport oversizedWadReport = writePackage(oversizedWadStaging, root.filePath(QStringLiteral("oversized.wad")), PackageArchiveFormat::Wad);
		ok &= expect(!oversizedWadReport.succeeded() && !oversizedWadReport.blockedMessages.isEmpty(), "a WAD lump above INT32_MAX should be blocked");
		ok &= expect(oversizedWadReport.blockedMessages.join('\n').contains(QStringLiteral("WAD lump size exceeds the signed 32-bit limit")), "a WAD lump above INT32_MAX should be blocked by the size bound");
	}

	// More than 65535 entries must produce ZIP64 records instead of failing.
	// The entries come from a synthetic reader so the case stays fast: staging
	// 65k real files would only measure filesystem syscalls.
	{
		QDir bulkSource(root.filePath(QStringLiteral("zip64-source")));
		ok &= expect(root.mkpath(QStringLiteral("zip64-source")), "ZIP64 source directory should be created");
		ok &= expect(writeFile(bulkSource.filePath(QStringLiteral("seed.bin")), QByteArray("seed")), "ZIP64 seed file should be written");

		const int bulkDirectories = 65600;
		BulkDirectoryReader bulkReader(bulkSource.path(), bulkDirectories);
		PackageStagingModel bulkStaging;
		ok &= expect(bulkStaging.loadBaseArchive(bulkReader, &error), "ZIP64 staging should load");
		ok &= expect(bulkStaging.summary().baseDirectoryCount == bulkDirectories, "bulk directory records should be preserved");
		ok &= expect(bulkStaging.summary().baseFileCount == 1, "bulk fixture should hold one file");

		const QString zip64Path = root.filePath(QStringLiteral("bulk.pk3"));
		PackageWriteRequest zip64Request;
		zip64Request.destinationPath = zip64Path;
		zip64Request.format = PackageArchiveFormat::Pk3;
		zip64Request.compression = DeflateLevel::Store;
		const PackageWriteReport zip64Report = bulkStaging.writeArchive(zip64Request);
		ok &= expect(zip64Report.succeeded(), "ZIP64 write should succeed");
		ok &= expect(zip64Report.entryCount == 1 && zip64Report.directoryCount == bulkDirectories, "ZIP64 report record counts mismatch");
		quint64 totalEntries = 0;
		quint16 legacyEntries = 0;
		ok &= expect(readZip64TotalEntries(zip64Path, &totalEntries, &legacyEntries), "ZIP64 end records should be present");
		ok &= expect(totalEntries == static_cast<quint64>(bulkDirectories + 1), "ZIP64 total entry count mismatch");
		ok &= expect(legacyEntries == 0xffff, "legacy end record should carry the ZIP64 sentinel");
	}

	// APPNOTE.TXT 4.4.1.4 / 4.4.21: 0xffff in the end-of-central-directory
	// entry count is not a count, it is the marker that says "read the ZIP64
	// record". A plan of exactly 65535 records therefore still needs the ZIP64
	// end record and locator; writing the bare sentinel without them produced a
	// file this repo's own reader refuses to open.
	{
		QDir boundarySource(root.filePath(QStringLiteral("zip64-boundary-source")));
		ok &= expect(root.mkpath(QStringLiteral("zip64-boundary-source")), "ZIP64 boundary source directory should be created");
		ok &= expect(writeFile(boundarySource.filePath(QStringLiteral("seed.bin")), QByteArray("seed")), "ZIP64 boundary seed file should be written");

		const int boundaryDirectories = 65534;
		BulkDirectoryReader boundaryReader(boundarySource.path(), boundaryDirectories);
		PackageStagingModel boundaryStaging;
		ok &= expect(boundaryStaging.loadBaseArchive(boundaryReader, &error), "ZIP64 boundary staging should load");

		const QString boundaryPath = root.filePath(QStringLiteral("boundary.pk3"));
		PackageWriteRequest boundaryRequest;
		boundaryRequest.destinationPath = boundaryPath;
		boundaryRequest.format = PackageArchiveFormat::Pk3;
		boundaryRequest.compression = DeflateLevel::Store;
		const PackageWriteReport boundaryReport = boundaryStaging.writeArchive(boundaryRequest);
		ok &= expect(boundaryReport.succeeded(), "ZIP64 boundary write should succeed");
		ok &= expect(boundaryReport.entryCount + boundaryReport.directoryCount == 65535, "boundary plan should hold exactly 65535 records");

		quint64 boundaryTotalEntries = 0;
		quint16 boundaryLegacyEntries = 0;
		ok &= expect(readZip64TotalEntries(boundaryPath, &boundaryTotalEntries, &boundaryLegacyEntries), "exactly 65535 records still needs ZIP64 end records");
		ok &= expect(boundaryTotalEntries == 65535, "ZIP64 boundary total entry count mismatch");
		ok &= expect(boundaryLegacyEntries == 0xffff, "legacy end record should carry the ZIP64 sentinel at the boundary");

		PackageArchive boundaryRoundTrip;
		ok &= expect(boundaryRoundTrip.load(boundaryPath, &error), "a 65535-record archive must load back");
		ok &= expect(boundaryRoundTrip.readEntryBytes(QStringLiteral("seed.bin"), &bytes, &error), "boundary archive seed entry should read");
		ok &= expect(bytes == QByteArray("seed"), "boundary archive seed bytes mismatch");
	}

	return ok ? 0 : 1;
}
