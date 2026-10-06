#include "core/package_storage.h"
#include "core/package_recovery.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; }
	return value;
}
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QString objectPath(const QString& root, const QByteArray& bytes)
{
	return QDir(root).filePath(QStringLiteral("objects/") + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); const QString store = root.filePath(QStringLiteral("recovery"));
	bool ok = true; QString error;
	const QString interruptedId = id(), interrupted = packageRecoveryPath(store, interruptedId);
	QDir().mkpath(QDir(interrupted).filePath(QStringLiteral("objects")));
	const QString blob = objectPath(interrupted, "published"), pending = QDir(interrupted).filePath(QStringLiteral("objects/.writing-AbC123"));
	ok &= write(blob, "published") && write(pending, "partial");
	auto snapshot = inspectPackageStorage(interrupted);
	ok &= expect(snapshot.safe() && !snapshot.manifestPresent && snapshot.bytes == 16 && snapshot.temporaryBytes == 7
		&& snapshot.objectBytes == 9, "scan accounts for incomplete data and temporary files", snapshot.error);
	ok &= expect(!inspectPackageStorage(interrupted, {}, 1).safe(), "file scan is bounded");
	ok &= expect(!inspectPackageStorage(interrupted, {[] { return true; }}).safe(), "storage scan can be cancelled");
	auto inventory = listPackageRecoveries(store);
	ok &= expect(inventory.storageComplete && inventory.storageBytes == 16 && inventory.records.size() == 1
		&& !inventory.records.first().readable() && inventory.records.first().storageSha256 == snapshot.fingerprint,
		"incomplete copies expose a review token without claiming a readable manifest");
	const QString addition = QDir(interrupted).filePath(QStringLiteral(".document-XyZ123"));
	ok &= write(addition, "metadata");
	ok &= expect(!discardPackageRecoveryStorage(store, interruptedId, snapshot.fingerprint, false, &error)
		&& read(blob) == "published" && read(addition) == "metadata", "stale membership review removes no files", error);
	snapshot = inspectPackageStorage(interrupted);
	const QString foreign = QDir(interrupted).filePath(QStringLiteral("keep-notes.txt"));
	ok &= write(foreign, "keep");
	ok &= expect(!discardPackageRecoveryStorage(store, interruptedId, snapshot.fingerprint, false, &error)
		&& read(foreign) == "keep" && read(pending) == "partial", "unknown files block the entire discard");
	ok &= QFile::remove(foreign);
	auto session = PackageRecoverySession::acquire(store, interruptedId, &error);
	ok &= expect(session && !discardPackageRecoveryStorage(store, interruptedId, snapshot.fingerprint, false, &error)
		&& !discardPackageRecoveryStorage(store, interruptedId, snapshot.fingerprint, true, &error), "incomplete copy in an active session is protected");
	session.reset();
	const auto beforePreview = QDir(store).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
	ok &= expect(discardPackageRecoveryStorage(store, interruptedId, snapshot.fingerprint, true, &error)
		&& QDir(store).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == beforePreview
		&& inspectPackageStorage(interrupted).fingerprint == snapshot.fingerprint, "discard preview creates no lock or data files", error);
	ok &= expect(discardPackageRecoveryStorage(store, interruptedId, snapshot.fingerprint, false, &error)
		&& !QFileInfo::exists(interrupted), "explicit discard removes reviewed incomplete storage", error);

	// An oversized damaged manifest must remain bounded to inspect, yet still
	// permit an explicit reviewed discard through its storage statistics token.
	const QString oversizedId = id(), oversized = packageRecoveryPath(store, oversizedId);
	QDir().mkpath(oversized); QFile huge(QDir(oversized).filePath(QStringLiteral("document.json")));
	ok &= huge.open(QIODevice::WriteOnly) && huge.resize(32 * 1024 * 1024 + 1); huge.close();
	const auto damaged = inspectPackageRecovery(oversized);
	ok &= expect(!damaged.readable() && damaged.manifestSha256.isEmpty() && damaged.storageSha256.size() == 32
		&& discardPackageRecoveryStorage(store, oversizedId, damaged.storageSha256, false, &error), "oversized metadata can be reviewed and discarded safely", error);

	PackageStagingModel plan; ok &= plan.createEmpty(PackageArchiveFormat::Pk3);
	const QByteArray retained(256 * 1024, 'a'), rejected(512 * 1024, 'b');
	ok &= plan.addBytes(retained, QStringLiteral("retained.bin"));
	const QString firstId = id(), firstPath = packageRecoveryPath(store, firstId);
	auto first = PackageRecoverySession::acquire(store, firstId, &error);
	if (!expect(bool(first), "quota session", error)) { return 1; }
	auto result = first->checkpoint(plan, QStringLiteral("Quota"), {}, {1024 * 1024, 1});
	if (!expect(result.succeeded(), "initial bounded checkpoint", result.error)) { return 1; }
	const QString manifest = QDir(firstPath).filePath(QStringLiteral("document.json"));
	const QByteArray committed = read(manifest); const auto initialStorage = inspectPackageStorage(firstPath);
	const QString secondId = id(); auto second = PackageRecoverySession::acquire(store, secondId, &error);
	ok &= expect(second && !second->checkpoint(plan, QStringLiteral("Second"), {}, {1024 * 1024, 1}).succeeded()
		&& !QFileInfo::exists(packageRecoveryPath(store, secondId)) && read(manifest) == committed, "copy count limit preserves existing copies and creates no new copy");
	second.reset();
	ok &= plan.addBytes(rejected, QStringLiteral("large.bin"));
	result = first->checkpoint(plan, QStringLiteral("Over limit"), {}, {initialStorage.bytes + 1024, 1});
	ok &= expect(!result.succeeded() && read(manifest) == committed && !QFileInfo::exists(objectPath(firstPath, rejected))
		&& inspectPackageStorage(firstPath).bytes == initialStorage.bytes, "payload quota failure leaves last checkpoint and storage unchanged", result.error);
	ok &= plan.undo(); // The redo payload is also required and cannot bypass the quota.
	ok &= expect(!first->checkpoint(plan, QStringLiteral("Redo budget"), {}, {initialStorage.bytes + 1024, 1}).succeeded()
		&& read(manifest) == committed, "quota includes undone history content");
	ok &= plan.addBytes("small", QStringLiteral("branch.txt"));
	result = first->checkpoint(plan, QStringLiteral("Manifest budget"), {}, {initialStorage.bytes + 100, 1});
	ok &= expect(!result.succeeded() && read(manifest) == committed && inspectPackageStorage(firstPath).bytes <= initialStorage.bytes + 100,
		"atomic manifest replacement also needs budget", result.error);
	QLockFile storeLock(QDir(store).filePath(QStringLiteral(".store.lock"))); storeLock.setStaleLockTime(0);
	ok &= expect(storeLock.tryLock() && !first->checkpoint(plan, QStringLiteral("Locked")).succeeded()
		&& !first->retire(&error) && read(manifest) == committed, "store lock serializes checkpoint and cleanup"); storeLock.unlock();
	result = first->checkpoint(plan, QStringLiteral("Retry"), {}, {initialStorage.bytes + 16384, 1});
	ok &= expect(result.succeeded() && result.maintenanceError.isEmpty(), "retry reclaims unreachable failed-write objects", result.error + result.maintenanceError);
	ok &= first->retire(&error);

	// A base reader does not supply individual object digests. Hash-only reuse
	// allows an unchanged large base to checkpoint with only manifest headroom.
	const QString source = root.filePath(QStringLiteral("source")); QDir().mkpath(source);
	ok &= write(QDir(source).filePath(QStringLiteral("base.bin")), retained);
	PackageArchive archive; PackageStagingModel disk;
	ok &= archive.load(source, &error) && disk.loadBaseArchive(archive, &error);
	const QString diskId = id(), diskPath = packageRecoveryPath(store, diskId);
	auto diskSession = PackageRecoverySession::acquire(store, diskId, &error);
	if (!expect(bool(diskSession), "base reader session", error)) { return 1; }
	result = diskSession->checkpoint(disk, QStringLiteral("Base"));
	if (!expect(result.succeeded(), "base reader checkpoint", result.error)) { return 1; }
	const auto baseline = inspectPackageStorage(diskPath); const qint64 maximum = baseline.bytes + 16384;
	ok &= disk.createDirectory(QStringLiteral("empty")); bool exceeded = false;
	PackageReadControl observe;
	observe.progress = [&](const QString&, qint64, qint64) {
		const auto current = inspectPackageStorage(diskPath);
		if (current.safe()) { exceeded |= current.bytes > maximum; }
	};
	result = diskSession->checkpoint(disk, QStringLiteral("Reuse"), observe, {maximum, 1});
	ok &= expect(result.succeeded() && !exceeded && inspectPackageStorage(diskPath).bytes <= maximum,
		"large duplicate base reuses content without exceeding transient budget", result.error);
	const QByteArray good = read(QDir(diskPath).filePath(QStringLiteral("document.json")));
	ok &= disk.addBytes(rejected, QStringLiteral("cancel.bin")); bool stop = false;
	PackageReadControl cancelDuringWrite;
	cancelDuringWrite.isCancelled = [&] { return stop; };
	cancelDuringWrite.progress = [&](const QString& label, qint64 done, qint64 total) {
		if (label == QStringLiteral("cancel.bin") && total == rejected.size() && done > 0) { stop = true; }
	};
	result = diskSession->checkpoint(disk, QStringLiteral("Cancelled"), cancelDuringWrite);
	ok &= expect(stop && !result.succeeded() && read(QDir(diskPath).filePath(QStringLiteral("document.json"))) == good
		&& inspectPackageStorage(diskPath).temporaryBytes == 0, "mid-write cancellation preserves committed copy and removes its temporary file", result.error);
	ok &= diskSession->retire(&error);
	std::cout << (ok ? "Package storage smoke passed\n" : "Package storage smoke failed\n");
	return ok ? 0 : 1;
}
