#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool condition, const char* message)
{
	if (!condition) { std::cerr << message << '\n'; }
	return condition;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
	QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QStringList listing(const QDir& root)
{
	return root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
}
bool refused(const QDir& root, const PackageStagingModel& model, PackageWriteRequest request, const QString& reason)
{
	bool ok = true;
	request.destinationPath = root.filePath("preserved-output"); request.allowOverwrite = true;
	request.backupPath = root.filePath("preserved-backup"); request.writeManifest = true;
	request.manifestPath = root.filePath("preserved-manifest");
	ok &= expect(write(request.destinationPath, "original") && write(request.backupPath, "backup")
		&& write(request.manifestPath, "manifest"), "Create independent output, backup and manifest sentinels.");
	const auto before = listing(root); const auto revision = model.revision();
	for (const bool dryRun : {true, false}) {
		request.dryRun = dryRun;
		const auto result = model.writeArchive(request);
		ok &= expect(!result.succeeded() && !result.outputCommitted && !result.blockedMessages.isEmpty()
			&& result.blockedMessages.join(' ').contains(reason, Qt::CaseInsensitive), "Refusal identifies the index policy in real and no-write modes.");
		if (result.succeeded() || !result.blockedMessages.join(' ').contains(reason, Qt::CaseInsensitive)) {
			std::cerr << result.blockedMessages.join(' ').toStdString() << '\n';
		}
		ok &= expect(read(request.destinationPath) == "original" && read(request.backupPath) == "backup"
			&& read(request.manifestPath) == "manifest" && listing(root) == before
			&& model.revision() == revision, "Refused writes preserve all files, history and the directory inventory.");
	}
	return ok;
}
bool boundary(const QDir& root, PackageStagingModel& model, PackageWriteRequest request,
	qsizetype records, qint64 metadataBytes)
{
	bool ok = true; QString error;
	request.indexLimits.maximumEntries = records;
	request.indexLimits.maximumMetadataBytes = metadataBytes;
	request.indexLimits.maximumFingerprintBytes = 32;
	request.verifyDeterminism = true; request.dryRun = true;
	const auto before = listing(root);
	const auto dry = model.writeArchive(request);
	ok &= expect(dry.succeeded() && dry.determinismVerified && !dry.outputCommitted && listing(root) == before,
		"Exact index boundaries admit a deterministic dry run without files or locks.");
	request.dryRun = false;
	const auto saved = model.writeArchive(request);
	PackageArchive archive;
	ok &= expect(saved.succeeded() && saved.outputCommitted && saved.bytesWritten == dry.bytesWritten
		&& saved.sha256 == dry.sha256 && archive.load(request.destinationPath, &error, {}, request.indexLimits),
		"The dry-run bytes match the committed archive, which reopens under the same limits.");
	if (!saved.succeeded()) { std::cerr << saved.blockedMessages.join(' ').toStdString() << '\n'; }
	const auto usage = archive.indexUsage();
	ok &= expect(usage.entries == records && usage.metadataBytes == metadataBytes && usage.fingerprintBytes == 32,
		"Reader accounting matches independently calculated record, encoded-directory and decoded-text costs.");
	for (const auto& entry : model.plannedEntries()) {
		if (entry.kind != PackageEntryKind::File) { continue; }
		QByteArray actual, expected;
		const QString name = request.format == PackageArchiveFormat::Wad && (request.wadMagic == "PWAD" || request.wadMagic == "IWAD")
			? entry.virtualPath.toUpper() : entry.virtualPath;
		ok &= expect(model.entryBytes(entry, &expected, &error) && archive.readEntryBytes(name, &actual, &error) && actual == expected,
			"Bounded output preserves payload bytes and expected format names.");
	}
	// Canonical ZIP directory identities also have to survive snapshot admission.
	PackageArchive snapshot;
	ok &= expect(snapshot.loadSnapshot(std::make_shared<PackageArchive>(archive), &error), "A reopened archive remains usable as an immutable document snapshot.");
	if (records) { auto reduced = request; --reduced.indexLimits.maximumEntries; ok &= refused(root, model, reduced, "index"); }
	if (metadataBytes) { auto reduced = request; --reduced.indexLimits.maximumMetadataBytes; ok &= refused(root, model, reduced, "index"); }
	auto reduced = request; reduced.indexLimits.maximumFingerprintBytes = 31;
	ok &= refused(root, model, reduced, "fingerprint");
	return ok;
}
bool formats(const QDir& root)
{
	bool ok = true;
	for (const auto format : {PackageArchiveFormat::Pak, PackageArchiveFormat::Zip, PackageArchiveFormat::Pk3}) {
		PackageStagingModel model; model.createEmpty(format);
		const QString name = format == PackageArchiveFormat::Pk3 ? QString::fromUtf8("é/漢.txt") : QStringLiteral("a/b");
		ok &= expect(model.addBytes("payload", name), "Stage nested path fixture.");
		PackageWriteRequest request; request.format = format;
		request.destinationPath = root.filePath(QStringLiteral("nested-%1").arg(static_cast<int>(format)));
		const qint64 bytes = format == PackageArchiveFormat::Zip ? 57 : 72;
		ok &= boundary(root, model, request, 2, bytes);
		request.indexLimits.maximumPathDepth = 1;
		ok &= refused(root, model, request, "depth");
	}
	for (const QString& magic : {QStringLiteral("PWAD"), QStringLiteral("IWAD"), QStringLiteral("WAD2"), QStringLiteral("WAD3")}) {
		PackageStagingModel model; model.createEmpty(PackageArchiveFormat::Wad, magic);
		ok &= expect(model.addBytes("payload", "asset"), "Stage WAD fixture.");
		PackageWriteRequest request; request.format = PackageArchiveFormat::Wad; request.wadMagic = magic;
		request.destinationPath = root.filePath(magic + ".wad");
		ok &= boundary(root, model, request, 1, magic.startsWith("WAD") ? 42 : 26);
	}
	PackageStagingModel folders; folders.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(folders.createDirectory("a") && folders.createDirectory("empty") && folders.addBytes("payload", "a/b"), "Stage explicit populated and empty folders.");
	PackageWriteRequest request; request.format = PackageArchiveFormat::Zip; request.destinationPath = root.filePath("folders.zip");
	ok &= boundary(root, folders, request, 3, 171);
	PackageArchive reopened; QString error;
	ok &= expect(reopened.load(request.destinationPath, &error) && reopened.entries().size() == 3, "Explicit folders are not duplicated by implied folders.");
	for (const auto& entry : reopened.entries()) {
		ok &= expect(!entry.virtualPath.endsWith('/'), "Directory identities have no wire-format trailing slash.");
	}
	PackageStagingModel empty; empty.createEmpty(PackageArchiveFormat::Zip);
	request.destinationPath = root.filePath("empty.zip");
	ok &= boundary(root, empty, request, 0, 0);
	return ok;
}
bool duplicateDiagnostics(const QDir& root)
{
	// Independently encode two case-differing physical records. Saving must
	// retain both and budget the reader's advisory diagnostic as well as paths.
	QByteArray fixture("PACK");
	const auto u32 = [&](quint32 value) { for (int shift = 0; shift < 32; shift += 8) { fixture.append(static_cast<char>(value >> shift)); } };
	u32(14); u32(128); fixture += "pp";
	int offset = 12;
	for (const QByteArray& path : {QByteArray("SS/A"), QByteArray("ss/a")}) {
		fixture += path + QByteArray(56 - path.size(), '\0'); u32(offset++); u32(1);
	}
	const QString source = root.filePath("duplicate-source.pak");
	PackageArchive archive; PackageStagingModel model; QString error;
	bool ok = expect(write(source, fixture) && archive.load(source, &error) && archive.warnings().size() == 1
		&& model.loadBaseArchive(archive, &error), "Read independently encoded duplicate-case records.");
	const QString warning = QStringLiteral("Duplicate package entry path; first match will be used for byte reads.");
	for (const auto format : {PackageArchiveFormat::Pak, PackageArchiveFormat::Zip}) {
		PackageWriteRequest request; request.format = format;
		request.destinationPath = root.filePath(QStringLiteral("duplicates-%1").arg(static_cast<int>(format)));
		const qint64 directory = format == PackageArchiveFormat::Pak ? 128 : 100;
		ok &= boundary(root, model, request, 3, directory + 20 + 2 * (4 + warning.size()));
	}
	return ok;
}

bool fingerprintBoundary(const QDir& root)
{
	bool ok = true;
	for (const auto format : {PackageArchiveFormat::Pak, PackageArchiveFormat::Zip, PackageArchiveFormat::Wad}) {
		const int overhead = format == PackageArchiveFormat::Pak ? 76 : format == PackageArchiveFormat::Zip ? 100 : 28;
		PackageWriteRequest request; request.format = format; request.wadMagic = format == PackageArchiveFormat::Wad ? "PWAD" : "";
		request.compression = DeflateLevel::Store; request.indexLimits.maximumFingerprintBytes = 32;
		request.destinationPath = root.filePath(QStringLiteral("chunk-%1").arg(static_cast<int>(format)));
		PackageStagingModel model; model.createEmpty(format);
		ok &= expect(model.addBytes(QByteArray(65536 - overhead, 'x'), "x"), "Stage an exact one-chunk output.");
		request.dryRun = true; const auto dry = model.writeArchive(request);
		request.dryRun = false; const auto actual = model.writeArchive(request);
		PackageArchive archive; QString error;
		ok &= expect(dry.succeeded() && actual.succeeded() && actual.bytesWritten == 65536 && dry.sha256 == actual.sha256
			&& archive.load(request.destinationPath, &error, {}, request.indexLimits), "Archive headers, payload and directory exactly fill one fingerprint chunk.");
		PackageStagingModel larger; larger.createEmpty(format);
		ok &= expect(larger.addBytes(QByteArray(65537 - overhead, 'x'), "x"), "Stage one additional output byte.");
		ok &= refused(root, larger, request, "fingerprint");
	}
	return ok;
}
bool depthAndCancellation(const QDir& root)
{
	bool ok = true; QString error;
	QString prefix; for (int index = 0; index < 127; ++index) { prefix += "a/"; }
	PackageWriteRequest request; request.format = PackageArchiveFormat::Zip; request.destinationPath = root.filePath("depth.zip");
	PackageStagingModel model; model.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(model.addBytes("payload", prefix + "file"), "Stage the 128-component boundary.");
	const auto report = model.writeArchive(request); PackageArchive archive;
	ok &= expect(report.succeeded() && archive.load(request.destinationPath, &error) && archive.entries().size() == 128,
		"The default depth boundary writes and reopens with all 127 implied folders.");
	PackageStagingModel deep; deep.createEmpty(PackageArchiveFormat::Zip);
	const QString source = root.filePath("verified-input");
	ok &= expect(write(source, "verified") && !deep.addFile(source, "a/" + prefix + "file", &error,
		PackageStageConflictResolution::Block, {}, PackageFileImportMode::VerifyOnly) && error.contains("depth") && deep.operations().isEmpty(),
		"New staging refuses an over-depth verified source before history adoption.");
	ok &= expect(deep.addFile(source, prefix + "file", &error, PackageStageConflictResolution::Block, {}, PackageFileImportMode::VerifyOnly),
		"Stage a verified source for a stricter writer policy.");
	ok &= expect(write(source, "changed"), "Invalidate the synthetic source after staging.");
	int payloadCallbacks = 0;
	request.byteProgress = [&](PackageWritePhase phase, const QString&, quint64, quint64) { if (phase != PackageWritePhase::CheckIndex) { ++payloadCallbacks; } };
	request.indexLimits.maximumPathDepth = 127;
	ok &= refused(root, deep, request, "depth");
	ok &= expect(payloadCallbacks == 0, "Index admission refuses before source verification or payload reads.");
	request.indexLimits.maximumPathDepth = 128;
	PackageStagingModel folders; folders.createEmpty(PackageArchiveFormat::Zip);
	for (const QString& name : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
		ok &= expect(folders.addBytes("x", name + prefix.mid(1) + "file"), "Stage enough unique implied folders for progress cancellation.");
	}
	for (const bool dryRun : {true, false}) {
		bool stop = false; bool folderProgress = false;
		request.destinationPath = root.filePath("cancelled.zip"); request.dryRun = dryRun;
		request.isCancelled = [&] { return stop; };
		request.byteProgress = [&](PackageWritePhase phase, const QString&, quint64 done, quint64 total) {
			if (phase == PackageWritePhase::CheckIndex && total == 0 && done > 3) { folderProgress = true; stop = true; }
		};
		const auto before = listing(root); const auto result = folders.writeArchive(request);
		ok &= expect(folderProgress && result.cancelled && !result.outputCommitted && listing(root) == before,
			"Cancellation within implied-folder admission leaves no publication files in either mode.");
	}
	return ok;
}
bool invalidPoliciesAndNames(const QDir& root)
{
	bool ok = true; PackageStagingModel model; model.createEmpty(PackageArchiveFormat::Zip); model.addBytes("x", "file");
	PackageWriteRequest request; request.format = PackageArchiveFormat::Zip;
	for (int index = 0; index < 8; ++index) {
		request.indexLimits = {};
		if (index == 0) { request.indexLimits.maximumEntries = -1; }
		if (index == 1) { request.indexLimits.maximumEntries = PackageIndexLimits::entryCeiling + 1; }
		if (index == 2) { request.indexLimits.maximumMetadataBytes = -1; }
		if (index == 3) { request.indexLimits.maximumMetadataBytes = PackageIndexLimits::metadataCeiling + 1; }
		if (index == 4) { request.indexLimits.maximumFingerprintBytes = -1; }
		if (index == 5) { request.indexLimits.maximumFingerprintBytes = PackageIndexLimits::fingerprintCeiling + 1; }
		if (index == 6) { request.indexLimits.maximumPathDepth = 0; }
		if (index == 7) { request.indexLimits.maximumPathDepth = PackageIndexLimits::depthCeiling + 1; }
		ok &= refused(root, model, request, "range");
	}
	request.indexLimits = {};
	PackageStagingModel invalidUtf8; invalidUtf8.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(invalidUtf8.addBytes("x", QString("file") + QChar(0xd800)), "Stage a QString path which cannot round-trip through UTF-8.");
	ok &= refused(root, invalidUtf8, request, "name");
	PackageStagingModel longFolder; longFolder.createEmpty(PackageArchiveFormat::Zip);
	ok &= expect(longFolder.createDirectory(QString(4096, 'a')), "Stage the canonical path-length boundary.");
	ok &= refused(root, longFolder, request, "name");
	return ok;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); bool ok = true;
	ok &= formats(root);
	ok &= duplicateDiagnostics(root);
	ok &= fingerprintBoundary(root);
	ok &= depthAndCancellation(root);
	ok &= invalidPoliciesAndNames(root);
	return ok ? 0 : 1;
}
