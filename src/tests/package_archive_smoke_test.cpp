#include "core/package_archive.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

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

bool hasFormat(PackageArchiveFormat format)
{
	for (const PackageArchiveFormatDescriptor& descriptor : packageArchiveFormatDescriptors()) {
		if (descriptor.format == format) {
			return true;
		}
	}
	return false;
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

bool writeFile(const QString& path, const QByteArray& data)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return file.write(data) == data.size();
}

QByteArray pakFixture()
{
	const QByteArray first = "echo hello\n";
	const QByteArray second = "duplicate\n";
	const quint32 firstOffset = 12;
	const quint32 secondOffset = firstOffset + first.size();
	const quint32 directoryOffset = secondOffset + second.size();

	QByteArray data;
	data.append("PACK");
	appendLe32(&data, directoryOffset);
	appendLe32(&data, 64 * 3);
	data.append(first);
	data.append(second);

	auto appendRecord = [&data](const QByteArray& name, quint32 offset, quint32 size) {
		const int before = data.size();
		data.append(name.left(56));
		while (data.size() - before < 56) {
			data.append('\0');
		}
		appendLe32(&data, offset);
		appendLe32(&data, size);
	};
	appendRecord("scripts/autoexec.cfg", firstOffset, first.size());
	appendRecord("scripts/autoexec.cfg", secondOffset, second.size());
	appendRecord("../escape.cfg", firstOffset, first.size());
	return data;
}

QByteArray wadFixture()
{
	const QByteArray first = "MAPDATA";
	const QByteArray second = "PLAYPAL";
	const quint32 firstOffset = 12;
	const quint32 secondOffset = firstOffset + first.size();
	const quint32 directoryOffset = secondOffset + second.size();

	QByteArray data;
	data.append("PWAD");
	appendLe32(&data, 2);
	appendLe32(&data, directoryOffset);
	data.append(first);
	data.append(second);

	auto appendRecord = [&data](const QByteArray& name, quint32 offset, quint32 size) {
		appendLe32(&data, offset);
		appendLe32(&data, size);
		const int before = data.size();
		data.append(name.left(8));
		while (data.size() - before < 8) {
			data.append('\0');
		}
	};
	appendRecord("MAP01", firstOffset, first.size());
	appendRecord("PLAYPAL", secondOffset, second.size());
	return data;
}

QByteArray zipFixture()
{
	struct ZipEntry {
		QByteArray name;
		QByteArray bytes;
		quint32 localOffset = 0;
	};

	QVector<ZipEntry> entries = {
		{QByteArray("maps/test.map"), QByteArray("{\n}")},
		{QByteArray("textures/wall.tga"), QByteArray("TGA")},
		{QByteArray("../escape.cfg"), QByteArray("bad")},
	};

	QByteArray data;
	for (ZipEntry& entry : entries) {
		entry.localOffset = static_cast<quint32>(data.size());
		appendLe32(&data, 0x04034b50);
		appendLe16(&data, 20);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, entry.bytes.size());
		appendLe32(&data, entry.bytes.size());
		appendLe16(&data, entry.name.size());
		appendLe16(&data, 0);
		data.append(entry.name);
		data.append(entry.bytes);
	}

	const quint32 centralOffset = static_cast<quint32>(data.size());
	for (const ZipEntry& entry : entries) {
		appendLe32(&data, 0x02014b50);
		appendLe16(&data, 20);
		appendLe16(&data, 20);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, entry.bytes.size());
		appendLe32(&data, entry.bytes.size());
		appendLe16(&data, entry.name.size());
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe16(&data, 0);
		appendLe32(&data, 0);
		appendLe32(&data, entry.localOffset);
		data.append(entry.name);
	}
	const quint32 centralSize = static_cast<quint32>(data.size()) - centralOffset;

	appendLe32(&data, 0x06054b50);
	appendLe16(&data, 0);
	appendLe16(&data, 0);
	appendLe16(&data, entries.size());
	appendLe16(&data, entries.size());
	appendLe32(&data, centralSize);
	appendLe32(&data, centralOffset);
	appendLe16(&data, 0);
	return data;
}

} // namespace

int main()
{
	bool ok = true;

	ok &= expect(hasFormat(PackageArchiveFormat::Folder), "missing folder package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Pak), "missing PAK package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Wad), "missing WAD package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Zip), "missing ZIP package descriptor");
	ok &= expect(hasFormat(PackageArchiveFormat::Pk3), "missing PK3 package descriptor");
	ok &= expect(packageArchiveFormatFromId(QStringLiteral("directory")) == PackageArchiveFormat::Folder, "directory id should map to folder");
	ok &= expect(packageArchiveFormatFromFileName(QStringLiteral("pak0.PAK")) == PackageArchiveFormat::Pak, "PAK extension not detected");
	ok &= expect(packageArchiveFormatFromFileName(QStringLiteral("textures.wad3")) == PackageArchiveFormat::Wad, "WAD3 extension not detected");
	ok &= expect(packageArchiveFormatFromFileName(QStringLiteral("baseq3/pak0.pk3")) == PackageArchiveFormat::Pk3, "PK3 extension not detected");

	PackageVirtualPath safe = normalizePackageVirtualPath(QStringLiteral("textures\\stone//wall01.tga"));
	ok &= expect(safe.isSafe(), "backslash path should normalize safely");
	ok &= expect(safe.normalizedPath == QStringLiteral("textures/stone/wall01.tga"), "normalized path mismatch");
	ok &= expect(packageVirtualPathFileName(safe.normalizedPath) == QStringLiteral("wall01.tga"), "file name extraction failed");
	ok &= expect(packageVirtualPathParent(safe.normalizedPath) == QStringLiteral("textures/stone"), "parent extraction failed");

	PackageVirtualPath trailing = normalizePackageVirtualPath(QStringLiteral("maps/start/"));
	ok &= expect(trailing.isSafe(), "trailing slash path should be safe");
	ok &= expect(trailing.normalizedPath == QStringLiteral("maps/start/"), "trailing slash should be preserved");

	ok &= expect(normalizePackageVirtualPath(QStringLiteral("")).issue == PackagePathIssue::Empty, "empty path should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("/absolute/path")).issue == PackagePathIssue::AbsolutePath, "absolute path should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("C:/games/pak0.pak")).issue == PackagePathIssue::DriveQualifiedPath, "drive path should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("../autoexec.cfg")).issue == PackagePathIssue::TraversalSegment, "leading traversal should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("textures/../autoexec.cfg")).issue == PackagePathIssue::TraversalSegment, "inner traversal should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("./textures/wall.tga")).issue == PackagePathIssue::CurrentDirectorySegment, "current-dir segment should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("pak0.pak:textures/wall.tga")).issue == PackagePathIssue::Colon, "colon should be rejected");
	ok &= expect(normalizePackageVirtualPath(QStringLiteral("textures/\x01wall.tga")).issue == PackagePathIssue::ControlCharacter, "control character should be rejected");

	ok &= expect(packageEntryLooksNestedArchive(QStringLiteral("maps/detail.pk3")), "PK3 should look like nested archive");
	ok &= expect(packageEntryLooksNestedArchive(QStringLiteral("id1/pak1.pak")), "PAK should look like nested archive");
	ok &= expect(!packageEntryLooksNestedArchive(QStringLiteral("textures/wall.tga")), "TGA should not look like nested archive");

	QTemporaryDir root;
	ok &= expect(root.isValid(), "temporary root should be valid");
	QString error;
	const QString outputPath = safePackageOutputPath(root.path(), QStringLiteral("textures/stone/wall01.tga"), &error);
	ok &= expect(error.isEmpty(), "safe output path should not report error");
	ok &= expect(!outputPath.isEmpty(), "safe output path should be created");
	ok &= expect(packagePathIsInsideDirectory(root.path(), outputPath), "safe output path should stay inside root");
	const QString blocked = safePackageOutputPath(root.path(), QStringLiteral("../escape.cfg"), &error);
	ok &= expect(blocked.isEmpty(), "unsafe output path should be empty");
	ok &= expect(!error.isEmpty(), "unsafe output path should report error");

	PackageArchiveSession session;
	PackageMountLayer primary;
	primary.id = QStringLiteral("pak0");
	primary.displayName = QStringLiteral("pak0.pak");
	primary.sourcePath = QStringLiteral("C:/Games/Quake/id1/pak0.pak");
	primary.format = PackageArchiveFormat::Pak;
	primary.entryCount = 42;
	ok &= expect(session.setPrimaryLayer(primary, &error), "primary layer should be accepted");
	ok &= expect(session.depth() == 1, "primary layer depth mismatch");
	ok &= expect(session.currentLayer().id == QStringLiteral("pak0"), "primary layer should be current");

	PackageMountLayer nested;
	nested.id = QStringLiteral("nested");
	nested.displayName = QStringLiteral("nested.pk3");
	nested.sourcePath = QStringLiteral("maps/nested.pk3");
	nested.mountPath = QStringLiteral("maps");
	nested.format = PackageArchiveFormat::Pk3;
	nested.entryCount = 3;
	ok &= expect(session.pushMountedLayer(nested, &error), "nested mount should be accepted");
	ok &= expect(session.depth() == 2, "nested mount depth mismatch");
	ok &= expect(session.currentLayer().mountPath == QStringLiteral("maps"), "nested mount path should normalize");
	ok &= expect(session.popMountedLayer(), "nested mount should pop");
	ok &= expect(session.depth() == 1, "depth after pop mismatch");

	nested.mountPath = QStringLiteral("../escape");
	ok &= expect(!session.pushMountedLayer(nested, &error), "unsafe nested mount should be rejected");
	ok &= expect(!error.isEmpty(), "unsafe nested mount should report error");

	QTemporaryDir packageRoot;
	ok &= expect(packageRoot.isValid(), "package fixture root should be valid");
	QDir packageDir(packageRoot.path());
	ok &= expect(packageDir.mkpath(QStringLiteral("folder/textures")), "folder fixture directory should be created");
	ok &= expect(writeFile(packageDir.filePath(QStringLiteral("folder/textures/wall.txt")), QByteArray("wall")), "folder fixture file should be written");

	PackageArchive archive;
	ok &= expect(archive.load(packageDir.filePath(QStringLiteral("folder")), &error), "folder package should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Folder, "folder package format mismatch");
	ok &= expect(archive.summary().fileCount == 1, "folder package file count mismatch");
	ok &= expect(archive.summary().directoryCount >= 1, "folder package should include synthetic directory");
	QByteArray bytes;
	ok &= expect(archive.readEntryBytes(QStringLiteral("textures/wall.txt"), &bytes, &error), "folder entry bytes should read");
	ok &= expect(bytes == QByteArray("wall"), "folder entry bytes mismatch");

	QTemporaryDir extractionRoot;
	ok &= expect(extractionRoot.isValid(), "extraction output root should be valid");
	PackageExtractionRequest dryRunRequest;
	dryRunRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("dry-run"));
	dryRunRequest.virtualPaths = {QStringLiteral("textures/wall.txt")};
	dryRunRequest.dryRun = true;
	PackageExtractionReport dryRunReport = extractPackageEntries(archive, dryRunRequest);
	ok &= expect(dryRunReport.succeeded(), "dry-run extraction should succeed");
	ok &= expect(dryRunReport.entries.size() == 1, "dry-run extraction should report one entry");
	ok &= expect(!dryRunReport.entries.front().outputPath.isEmpty(), "dry-run extraction should report output path");
	ok &= expect(!QFileInfo::exists(dryRunReport.entries.front().outputPath), "dry-run extraction should not write output file");
	ok &= expect(packageExtractionReportText(dryRunReport).contains(QStringLiteral("would write")), "dry-run report should describe staged write");

	PackageExtractionRequest extractRequest;
	extractRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("actual"));
	extractRequest.virtualPaths = {QStringLiteral("textures/wall.txt")};
	PackageExtractionReport extractReport = extractPackageEntries(archive, extractRequest);
	ok &= expect(extractReport.succeeded(), "selected extraction should succeed");
	ok &= expect(extractReport.writtenCount == 1, "selected extraction should write one entry");
	ok &= expect(extractReport.entries.size() == 1, "selected extraction should report one output entry");
	ok &= expect(packagePathIsInsideDirectory(extractRequest.targetDirectory, extractReport.entries.front().outputPath), "reported output should stay under extraction root");
	ok &= expect(QFileInfo::exists(extractReport.entries.front().outputPath), "selected extraction should create output file");
	QFile extractedFile(extractReport.entries.front().outputPath);
	ok &= expect(extractedFile.open(QIODevice::ReadOnly), "extracted file should open");
	ok &= expect(extractedFile.readAll() == QByteArray("wall"), "extracted file bytes mismatch");

	PackageExtractionReport noOverwriteReport = extractPackageEntries(archive, extractRequest);
	ok &= expect(noOverwriteReport.succeeded(), "no-overwrite extraction should not fail");
	ok &= expect(noOverwriteReport.skippedCount == 1, "no-overwrite extraction should skip existing output");
	ok &= expect(packageExtractionReportText(noOverwriteReport).contains(QStringLiteral("skipped")), "no-overwrite report should include skip state");

	PackageExtractionRequest treeRequest;
	treeRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("tree"));
	treeRequest.virtualPaths = {QStringLiteral("textures")};
	treeRequest.dryRun = true;
	PackageExtractionReport treeReport = extractPackageEntries(archive, treeRequest);
	ok &= expect(treeReport.succeeded(), "directory extraction dry-run should succeed");
	ok &= expect(treeReport.requestedCount >= 2, "directory extraction should include subtree entries");

	PackageExtractionRequest cancelRequest;
	cancelRequest.targetDirectory = QDir(extractionRoot.path()).filePath(QStringLiteral("cancelled"));
	cancelRequest.extractAll = true;
	cancelRequest.dryRun = true;
	PackageExtractionReport cancelledReport = extractPackageEntries(archive, cancelRequest, [](const PackageExtractionEntryResult&, const PackageExtractionReport&) {
		return false;
	});
	ok &= expect(cancelledReport.cancelled, "extraction callback should be able to cancel");
	ok &= expect(!cancelledReport.succeeded(), "cancelled extraction should not report success");

	const QString pakPath = packageDir.filePath(QStringLiteral("tiny.pak"));
	ok &= expect(writeFile(pakPath, pakFixture()), "PAK fixture should be written");
	ok &= expect(archive.load(pakPath, &error), "PAK fixture should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Pak, "PAK format mismatch");
	ok &= expect(archive.summary().fileCount == 2, "PAK duplicate entries should remain visible");
	ok &= expect(!archive.warnings().isEmpty(), "PAK unsafe/duplicate warnings should be reported");
	ok &= expect(archive.readEntryBytes(QStringLiteral("scripts/autoexec.cfg"), &bytes, &error, 4), "PAK entry bytes should read");
	ok &= expect(bytes == QByteArray("echo"), "PAK entry byte prefix mismatch");

	const QString wadPath = packageDir.filePath(QStringLiteral("tiny.wad"));
	ok &= expect(writeFile(wadPath, wadFixture()), "WAD fixture should be written");
	ok &= expect(archive.load(wadPath, &error), "WAD fixture should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Wad, "WAD format mismatch");
	ok &= expect(archive.summary().fileCount == 2, "WAD file count mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("MAP01"), &bytes, &error), "WAD lump bytes should read");
	ok &= expect(bytes == QByteArray("MAPDATA"), "WAD lump bytes mismatch");

	const QString pk3Path = packageDir.filePath(QStringLiteral("tiny.pk3"));
	ok &= expect(writeFile(pk3Path, zipFixture()), "PK3 fixture should be written");
	ok &= expect(archive.load(pk3Path, &error), "PK3 fixture should load");
	ok &= expect(archive.format() == PackageArchiveFormat::Pk3, "PK3 format mismatch");
	ok &= expect(archive.summary().fileCount == 2, "PK3 unsafe entry should be skipped");
	ok &= expect(archive.summary().directoryCount >= 2, "PK3 synthetic directories should be visible");
	ok &= expect(!archive.warnings().isEmpty(), "PK3 unsafe warning should be reported");
	ok &= expect(archive.readEntryBytes(QStringLiteral("maps/test.map"), &bytes, &error), "PK3 stored entry bytes should read");
	ok &= expect(bytes == QByteArray("{\n}"), "PK3 stored entry bytes mismatch");

	return ok ? 0 : 1;
}
