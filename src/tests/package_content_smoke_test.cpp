#include "core/package_content.h"
#include "core/package_staging.h"
#include "core/package_validation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool replacePreservingTime(const QString& path, const QByteArray& bytes)
{
	const auto time = QFileInfo(path).lastModified();
	if (!write(path, bytes)) { return false; }
	QFile file(path);
	return file.open(QIODevice::ReadWrite) && file.setFileTime(time, QFileDevice::FileModificationTime);
}
void u32(QByteArray& bytes, quint32 value)
{
	for (int shift = 0; shift < 32; shift += 8) { bytes.append(static_cast<char>(value >> shift)); }
}
QByteArray pak(const QByteArray& payload)
{
	QByteArray bytes("PACK");
	u32(bytes, 12 + static_cast<quint32>(payload.size())); u32(bytes, 64);
	bytes.append(payload);
	QByteArray name("data.bin"); name.resize(56, '\0'); bytes.append(name);
	u32(bytes, 12); u32(bytes, static_cast<quint32>(payload.size()));
	return bytes;
}
QByteArray repeatedWad()
{
	QByteArray bytes("PWAD"); u32(bytes, 4); u32(bytes, 20);
	bytes.append("AAAABBBB");
	for (int index = 0; index < 4; ++index) {
		u32(bytes, index < 2 ? 12 : 16); u32(bytes, index % 2 ? 4 : 0);
		QByteArray name = index % 2 ? QByteArray("THINGS") : index ? QByteArray("MAP02") : QByteArray("MAP01");
		name.resize(8, '\0'); bytes.append(name);
	}
	return bytes;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	QDir root(temp.path());
	bool ok = true;
	QString error;
	QByteArray payload;
	for (int index = 0; index < 200003; ++index) { payload.append(static_cast<char>(index * 17)); }
	const QString path = root.filePath(QStringLiteral("source.bin"));
	ok &= expect(write(path, payload), "create content fixture");
	const auto identity = capturePackageFileIdentity(path, &error);
	ok &= expect(identity && identity->sha256 == QCryptographicHash::hash(payload, QCryptographicHash::Sha256)
		&& identity->chunkHashes.size() == 4 * 32, "capture bounded chunk identities and complete SHA-256");
	if (!identity) { return 1; }
	PackageContentDevice input(identity);
	ok &= expect(input.open() && input.read(7) == payload.left(7) && input.pos() == 7, "verified device advances its logical position");
	ok &= expect(input.peek(13) == payload.mid(7, 13) && input.pos() == 7, "peek does not consume verified input");
	ok &= expect(input.read(13) == payload.mid(7, 13) && input.seek(65531) && input.read(31) == payload.mid(65531, 31), "random access crosses chunk boundaries exactly");
	ok &= expect(input.seek(0) && input.readAll() == payload && input.atEnd() && input.bytesAvailable() == 0, "whole-file and EOF device semantics");
	input.close();
	QByteArray changed = payload; changed[70000] ^= 1;
	ok &= expect(replacePreservingTime(path, changed) && identity->matchesMetadata(), "mutate bytes without changing size, path or timestamp");
	ok &= expect(!verifyPackageFileIdentity(identity, &error) && !error.isEmpty(), "whole-source verification detects unchanged-metadata mutation");
	PackageContentDevice capped(identity);
	ok &= expect(capped.open() && capped.seek(69990), "seek changed source range");
	ok &= expect(capped.read(1).isEmpty() && capped.failed(), "even capped reads reject a changed source chunk before returning bytes");
	ok &= expect(write(path, payload), "restore content fixture");
	int updates = 0;
	PackageReadControl cancel;
	cancel.progress = [&](const QString&, qint64 completed, qint64) { if (completed > 0) { ++updates; } };
	cancel.isCancelled = [&] { return updates >= 1; };
	ok &= expect(!capturePackageFileIdentity(path, &error, cancel), "fingerprint capture cancels within a file");
	const QString archivePath = root.filePath(QStringLiteral("source.pak"));
	const QByteArray archiveBytes = pak(payload);
	ok &= expect(write(archivePath, archiveBytes), "create PAK fixture");
	PackageArchive archive;
	ok &= expect(archive.load(archivePath, &error) && !archive.contentId().isEmpty(), "load content-identified archive");
	QByteArray out;
	ok &= expect(archive.readEntryBytes(QStringLiteral("data.bin"), &out, &error) && out == payload, "read intact PAK payload");
	QByteArray damaged = archiveBytes; damaged[70012] ^= 1;
	ok &= expect(replacePreservingTime(archivePath, damaged), "change PAK bytes with unchanged metadata");
	ok &= expect(!archive.readEntryBytes(QStringLiteral("data.bin"), &out, &error) && out.isEmpty(), "stored PAK has content integrity despite lacking format CRCs");
	ok &= expect(!validatePackage(archive).valid(), "validation rejects altered source identity");
	PackageStagingModel stale;
	ok &= expect(stale.loadBaseArchive(archive, &error), "staging retains the original identity");
	PackageWriteRequest save; save.destinationPath = root.filePath(QStringLiteral("stale.pak"));
	ok &= expect(!stale.writeArchive(save).succeeded() && !QFile::exists(save.destinationPath), "changed base input cannot be republished from stale offsets");
	ok &= expect(root.mkpath(QStringLiteral("folder")), "create folder fixture");
	const QString folderPath = root.filePath(QStringLiteral("folder"));
	const QString memberPath = QDir(folderPath).filePath(QStringLiteral("member.txt"));
	ok &= expect(write(memberPath, "original"), "create folder member");
	PackageArchive folder;
	ok &= expect(folder.load(folderPath, &error), "capture folder identities");
	ok &= expect(replacePreservingTime(memberPath, "modified"), "change folder member without metadata changes");
	ok &= expect(!folder.readEntryBytes(QStringLiteral("member.txt"), &out, &error) && out.isEmpty(), "folder previews reject changed content");
	ok &= expect(folder.load(folderPath, &error), "refresh folder baseline");
	ok &= expect(write(QDir(folderPath).filePath(QStringLiteral("added.txt")), "new") && !folder.verifySourceIdentity(&error), "folder membership changes invalidate save input");
	ok &= expect(root.mkpath(QStringLiteral("empty")), "create empty source folder");
	PackageArchive empty; ok &= expect(empty.load(root.filePath(QStringLiteral("empty")), &error), "load empty folder");
	ok &= expect(validatePackage(empty).valid(), "unchanged empty source has a verified identity");
	const QString emptyPakPath = root.filePath(QStringLiteral("empty.pak"));
	QByteArray emptyPak("PACK"); u32(emptyPak, 12); u32(emptyPak, 0);
	ok &= expect(write(emptyPakPath, emptyPak), "create empty PAK");
	PackageArchive emptyPakArchive; ok &= expect(emptyPakArchive.load(emptyPakPath, &error), "load empty PAK");
	emptyPak[0] = 'X';
	ok &= expect(replacePreservingTime(emptyPakPath, emptyPak) && !validatePackage(emptyPakArchive).valid(), "changed empty-container headers must fail validation without any payload reads");
	PackageStagingModel plan; ok &= expect(plan.loadBaseArchive(empty, &error), "create staging fixture");
	const QString stagedPath = root.filePath(QStringLiteral("staged.txt"));
	ok &= expect(write(stagedPath, "first") && plan.addFile(stagedPath, QStringLiteral("text.txt"), &error), "capture staged input identity");
	ok &= expect(replacePreservingTime(stagedPath, "other"), "mutate staged input");
	ok &= expect(plan.addBytes("generated", QStringLiteral("extra.txt"), &error), "invalidate computed plan without restaging the input");
	save.destinationPath = root.filePath(QStringLiteral("changed-staged.pak"));
	ok &= expect(plan.writeArchive(save).succeeded(), "owned imports survive original-file mutation");
	PackageArchive retainedOutput;
	ok &= expect(retainedOutput.load(save.destinationPath, &error) && retainedOutput.readEntryBytes(QStringLiteral("text.txt"), &out, &error)
		&& out == "first", "plan recomputation never adopts changed original bytes");
	{
		PackageStagingModel late;
		ok &= expect(late.loadBaseArchive(empty, &error) && write(stagedPath, "first")
			&& late.addFile(stagedPath, QStringLiteral("late.txt"), &error), "capture late-mutation source");
		PackageWriteRequest request;
		request.destinationPath = root.filePath(QStringLiteral("late-change.pak"));
		request.allowOverwrite = true;
		ok &= expect(write(request.destinationPath, "destination") && write(request.destinationPath + ".bak", "earlier backup"), "create protected destination and existing backup");
		bool mutated = false;
		request.progress = [&](int completed, int total, const QString& path) {
			if (completed == total && path.isEmpty()) { mutated = replacePreservingTime(late.operations().last().sourceIdentity->path, "other"); }
		};
		const auto result = late.writeArchive(request);
		QFile destination(request.destinationPath); QFile backup(request.destinationPath + ".bak");
		ok &= expect(mutated && !result.succeeded() && !result.outputCommitted
			&& destination.open(QIODevice::ReadOnly) && destination.readAll() == "destination"
			&& backup.open(QIODevice::ReadOnly) && backup.readAll() == "earlier backup",
			"pre-publication verification rejects late equal-metadata snapshot changes and preserves output and backup");
	}
	const QString wadPath = root.filePath(QStringLiteral("maps.wad"));
	ok &= expect(write(wadPath, repeatedWad()), "create repeated-lump fixture");
	PackageArchive wad; ok &= expect(wad.load(wadPath, &error), "load repeated-lump WAD");
	for (int operation = 0; operation < 4; ++operation) {
		PackageStagingModel repeated; ok &= expect(repeated.loadBaseArchive(wad, &error), "load repeated-lump plan");
		if (operation == 0) { repeated.deleteEntry(QStringLiteral("THINGS"), &error); }
		if (operation == 1) { repeated.renameEntry(QStringLiteral("THINGS"), QStringLiteral("RENAMED"), &error); }
		if (operation == 2) { repeated.addBytes("new lump", QStringLiteral("THINGS"), &error, PackageStageConflictResolution::ReplaceExisting); }
		if (operation == 3) { repeated.renameEntry(QStringLiteral("MAP01"), QStringLiteral("THINGS"), &error, PackageStageConflictResolution::ReplaceExisting); }
		ok &= expect(!repeated.summary().canSave && repeated.plannedEntries().size() == 4, "ambiguous edits cannot silently choose or drop one repeated lump");
	}
	return ok ? 0 : 1;
}
