#include "core/package_draft.h"
#include "core/package_draft_access.h"
#include "core/package_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; } return value;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QString error;
	if (app.arguments().size() == 3 && app.arguments().at(1) == QStringLiteral("--hold-writer")) {
		QLockFile writer(QDir(app.arguments().at(2)).filePath(QStringLiteral(".write.lock")));
		if (!writer.tryLock()) { return 2; }
		std::cout << "READY\n" << std::flush; for (;;) { QThread::msleep(50); }
	}
	if (app.arguments().size() == 3 && app.arguments().at(1) == QStringLiteral("--hold-reader")) {
		PackageStagingModel reader;
		if (!PackageDraft::load(app.arguments().at(2), &reader, &error)) { std::cerr << error.toStdString(); return 2; }
		std::cout << "READY\n" << std::flush; for (;;) { QThread::msleep(50); }
	}
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); const QString path = root.filePath(QStringLiteral("draft.vibepackage"));
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); bool ok = true;
	ok &= expect(plan.addBytes(QByteArray("first"), QStringLiteral("asset.bin"), &error), "stage source", error);
	if (!expect(PackageDraft::save(path, &plan, false, &error), "create leased draft", error)) { return 1; }
	const auto initial = inspectPackageStorage(path);
	ok &= expect(initial.safe() && plan.plannedEntries().first().sourceIdentity->draftAccess
		&& plan.plannedEntries().first().sourceIdentity->draftAccess->protectsReaders(), "saved payloads retain native directory protection", error);
	ok &= expect(!PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &error), "an open document blocks maintenance");
	auto oldReader = std::make_unique<PackageStagingArchive>(plan);
	ok &= expect(plan.renameEntry(QStringLiteral("asset.bin"), QStringLiteral("renamed.bin"), &error)
		&& PackageDraft::save(path, &plan, true, &error), "save can replace metadata while older readers stay open", error);
	plan.clear();
	ok &= expect(!PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &error), "older snapshots keep maintenance excluded after document close");
	QByteArray bytes;
	ok &= expect(oldReader->readEntryAt(0, &bytes, &error) && bytes == "first", "older reader keeps its original content", error);
	PackageStagingModel flattened; ok &= expect(flattened.loadBaseArchive(*oldReader, &error), "flatten planned snapshot", error);
	oldReader.reset();
	ok &= expect(!PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &error), "flattened readers retain the lease through object identities");
	PackageWriteRequest request; request.destinationPath = QDir(path).filePath(QStringLiteral("document.json")); request.allowOverwrite = true;
	ok &= expect(!flattened.writeArchive(request).succeeded(), "flattened snapshots protect the complete draft storage path");
	flattened.clear();
	const auto maintenanceReview = inspectPackageStorage(path);
	{
		const auto access = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &error);
		ok &= expect(access && access->matchesDirectory(), "last reader releases the directory for maintenance", error);
		ok &= expect(!PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Read, &error), "maintenance excludes new readers");
		ok &= expect(!PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &error), "maintenance excludes another maintainer");
		ok &= expect(packageStorageLayoutUnchanged(maintenanceReview), "maintenance rechecks the reviewed layout without enumerating the locked directory");
		const QString probe = QDir(path).filePath(QStringLiteral(".probe")); QFile file(probe);
		ok &= expect(file.open(QIODevice::WriteOnly) && file.write("probe") == 5, "maintenance permits child file operations"); file.close();
		ok &= QFile::remove(probe);
	}
	const auto beforeRead = inspectPackageStorage(path);
	{
		PackageStagingModel first, second;
		ok &= expect(PackageDraft::load(path, &first, &error) && PackageDraft::load(path, &second, &error), "independent readers coexist", error);
		ok &= expect(inspectPackageStorage(path).fingerprint == beforeRead.fingerprint
			&& !QFileInfo::exists(QDir(path).filePath(QStringLiteral(".write.lock"))), "reader leases write no metadata or lock files");
	}
	QProcess child; child.setWorkingDirectory(root.path());
	child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hold-reader"), path});
	QByteArray output;
	for (int i = 0; i < 100 && !output.contains("READY"); ++i) {
		child.waitForReadyRead(100); output += child.readAllStandardOutput(); if (child.state() == QProcess::NotRunning) { break; }
	}
	const bool ready = output.contains("READY");
	ok &= expect(ready, "child holds a draft snapshot", QString::fromUtf8(child.readAllStandardError()));
	if (ready) { ok &= expect(!PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &error), "read protection works across processes"); }
	child.kill(); child.waitForFinished(10000);
	ok &= expect(bool(PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Maintain, &error)), "the OS releases reader protection after a crash", error);
	ok &= expect(inspectPackageStorage(path).fingerprint == beforeRead.fingerprint, "reader crash leaves no storage changes or stale lock files");
	child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hold-writer"), path}); output.clear();
	for (int i = 0; i < 100 && !output.contains("READY"); ++i) {
		child.waitForReadyRead(100); output += child.readAllStandardOutput(); if (child.state() == QProcess::NotRunning) { break; }
	}
	ok &= expect(output.contains("READY"), "child owns the draft writer lock before its crash");
	child.kill(); child.waitForFinished(10000);
	{
		PackageStagingModel resumed; const QString lockPath = QDir(path).filePath(QStringLiteral(".write.lock"));
		ok &= expect(QFileInfo::exists(lockPath) && PackageDraft::load(path, &resumed, &error), "writer crash leaves a readable draft and a stale lock", error);
		ok &= resumed.renameEntry(QStringLiteral("renamed.bin"), QStringLiteral("resumed.bin"), &error);
		ok &= expect(!PackageDraft::save(path, &resumed, true, &error, {}, true) && QFileInfo::exists(lockPath), "read-only preview does not clear a stale writer lock");
		ok &= expect(PackageDraft::save(path, &resumed, true, &error) && !QFileInfo::exists(lockPath), "a real save recovers a stale writer lock after no-write quota preflight", error);
	}
	{
		PackageStagingModel snapshot;
		ok &= expect(PackageDraft::load(path, &snapshot, &error), "load snapshot before root replacement", error);
		if (!snapshot.isLoaded() || snapshot.plannedEntries().isEmpty()) { return 1; }
		const auto identity = snapshot.plannedEntries().first().sourceIdentity;
		const auto access = PackageDraftAccess::acquire(path, PackageDraftAccess::Mode::Read, &error);
		const QString moved = root.filePath(QStringLiteral("moved.vibepackage"));
		ok &= expect(access && QDir().rename(path, moved) && QDir().mkpath(path), "replace a synthetic draft root while its native handle survives", error);
		if (access) { ok &= expect(!access->matchesDirectory(), "native identity rejects a replacement directory at the same path"); }
		ok &= expect(QFileInfo::exists(QDir(moved).filePath(QStringLiteral("document.json"))), "root replacement leaves the original fixture intact");
		ok &= QDir().mkpath(QDir(path).filePath(QStringLiteral("objects")));
		const QString movedObject = QDir(moved).filePath(QStringLiteral("objects/") + QFileInfo(identity->path).fileName());
		ok &= QFile::copy(movedObject, identity->path);
		QFile replacement(identity->path);
		ok &= replacement.open(QIODevice::ReadWrite) && replacement.setFileTime(identity->modifiedUtc, QFileDevice::FileModificationTime); replacement.close();
		ok &= expect(identity->matchesMetadata(), "replacement object preserves the old metadata and content");
		PackageContentDevice stale(identity);
		ok &= expect(!stale.open() && !verifyPackageFileIdentity(identity, &error), "older snapshots cannot adopt a replacement root with identical file metadata");
	}
	std::cout << (ok ? "Package draft access smoke passed\n" : "Package draft access smoke failed\n"); return ok ? 0 : 1;
}
