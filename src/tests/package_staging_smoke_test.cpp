#include "core/package_archive.h"
#include "core/package_staging.h"

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

quint32 readLe32(const QByteArray& data, qsizetype offset)
{
	if (offset < 0 || offset + 4 > data.size()) {
		return 0;
	}
	const auto* bytes = reinterpret_cast<const uchar*>(data.constData() + offset);
	return static_cast<quint32>(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24));
}

QString fixedLatin1String(const char* data, qsizetype maxSize)
{
	qsizetype size = 0;
	while (size < maxSize && data[size] != '\0') {
		++size;
	}
	return QString::fromLatin1(data, size).trimmed();
}

QStringList wadDirectoryNames(const QString& path)
{
	const QByteArray bytes = readFile(path);
	if (bytes.size() < 12) {
		return {};
	}
	const int count = static_cast<int>(readLe32(bytes, 4));
	const int directoryOffset = static_cast<int>(readLe32(bytes, 8));
	QStringList names;
	for (int index = 0; index < count; ++index) {
		const qsizetype record = directoryOffset + (index * 16);
		if (record + 16 > bytes.size()) {
			return {};
		}
		names.push_back(fixedLatin1String(bytes.constData() + record + 8, 8));
	}
	return names;
}

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
	ok &= expect(QFileInfo::exists(pakReportA.manifestPath), "PAK manifest should be written");
	ok &= expect(archive.load(pakPathA, &error), "written PAK should load");
	QByteArray bytes;
	ok &= expect(archive.readEntryBytes(QStringLiteral("scripts/autoexec.cfg"), &bytes, &error), "PAK added entry should read");
	ok &= expect(bytes == QByteArray("exec config\n"), "PAK added bytes mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("textures/wall.txt"), &bytes, &error), "PAK replaced entry should read");
	ok &= expect(bytes == QByteArray("stone"), "PAK replaced bytes mismatch");
	ok &= expect(archive.readEntryBytes(QStringLiteral("maps/e1m1.map"), &bytes, &error), "PAK renamed entry should read");

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

	const QString pk3PathA = root.filePath(QStringLiteral("out-a.pk3"));
	const QString pk3PathB = root.filePath(QStringLiteral("out-b.pk3"));
	PackageWriteReport pk3ReportA = writePackage(staging, pk3PathA, PackageArchiveFormat::Pk3);
	PackageWriteReport pk3ReportB = writePackage(staging, pk3PathB, PackageArchiveFormat::Pk3);
	ok &= expect(pk3ReportA.succeeded() && pk3ReportB.succeeded(), "PK3 save-as should succeed");
	ok &= expect(readFile(pk3PathA) == readFile(pk3PathB), "PK3 writer should be deterministic");
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
	ok &= expect(wadDirectoryNames(wadPath) == QStringList({QStringLiteral("MAP01"), QStringLiteral("THINGS"), QStringLiteral("LINEDEFS")}), "WAD map lump order should be preserved");
	ok &= expect(archive.load(wadPath, &error), "written WAD should load");
	ok &= expect(archive.readEntryBytes(QStringLiteral("THINGS"), &bytes, &error), "written WAD lump should read");
	ok &= expect(bytes == QByteArray("things"), "written WAD lump bytes mismatch");

	return ok ? 0 : 1;
}
