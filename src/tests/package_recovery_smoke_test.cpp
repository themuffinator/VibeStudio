#include "core/package_recovery.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include <filesystem>
#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; }
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
	return std::filesystem::path(path.toStdWString());
#else
	return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}
QString objectPath(const QString& path, const QByteArray& bytes)
{
	return QDir(path).filePath(QStringLiteral("objects/") + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
}
QByteArray payload(const PackageStagingModel& plan, const QString& path)
{
	QByteArray bytes; PackageStagingArchive(plan).readEntryBytes(path, &bytes, nullptr); return bytes;
}
}

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path());
	const QString store = root.filePath(QStringLiteral("recovery"));
	const QString firstId = id(), copyPath = packageRecoveryPath(store, firstId);
	QString error;
	bool ok = expect(!PackageRecoverySession::acquire(store, QStringLiteral("../escape"), &error)
		&& !QFileInfo::exists(store), "reject traversal before creating a directory");
	ok &= expect(listPackageRecoveries(store).records.isEmpty() && !QFileInfo::exists(store), "inventory does not create its store");
	PackageStagingModel plan;
	ok &= plan.createEmpty(PackageArchiveFormat::Pk3);
	ok &= plan.createDirectory(QStringLiteral("empty"));
	ok &= plan.addBytes("retained", QStringLiteral("one.txt"));
	ok &= plan.addBytes("old redo", QStringLiteral("two.txt"));
	const quint64 revision = plan.revision();
	auto session = PackageRecoverySession::acquire(store, firstId, &error);
	if (!expect(bool(session), "acquire recovery session", error)) { return 1; }
	auto written = session->checkpoint(plan, QStringLiteral("Untitled PK3"));
	ok &= expect(written.succeeded() && written.maintenanceError.isEmpty(), "write checkpoint", written.error + written.maintenanceError);
	ok &= expect(plan.isModified() && plan.draftPath().isEmpty() && plan.revision() == revision, "checkpoint never marks the live document saved");
	const QString manifest = QDir(copyPath).filePath(QStringLiteral("document.json"));
	const QByteArray initial = read(manifest);
	auto inventory = listPackageRecoveries(store);
	ok &= expect(inventory.records.size() == 1 && inventory.records.first().readable()
		&& inventory.records.first().sessionFilePresent && inventory.records.first().historyCount == 3, "inventory preserves history details and session presence");
	ok &= expect(!PackageRecoverySession::acquire(store, firstId, &error)
		&& !discardPackageRecovery(store, firstId, written.manifestSha256, false, &error), "live editor lease blocks another owner and discard", error);
	PackageReadControl cancelled; cancelled.isCancelled = [] { return true; };
	ok &= plan.addBytes("cancelled", QStringLiteral("later.txt"));
	const auto interrupted = session->checkpoint(plan, QStringLiteral("cancelled"), cancelled);
	ok &= expect(!interrupted.succeeded() && read(manifest) == initial, "cancelled write retains the previous manifest");
	ok &= plan.undo();
	const auto withRedo = session->checkpoint(plan, QStringLiteral("With redo"));
	ok &= expect(withRedo.succeeded(), "checkpoint includes redo payload", withRedo.error);
	PackageStagingModel recovered;
	const QString restore = root.filePath(QStringLiteral("restored.vibepackage"));
	ok &= expect(!restorePackageRecovery(store, firstId, written.manifestSha256, restore, &recovered, &error)
		&& !QFileInfo::exists(restore), "stale selected digest cannot restore a changed copy");
	ok &= expect(restorePackageRecovery(store, firstId, withRedo.manifestSha256, restore, &recovered, &error, {}, true)
		&& !QFileInfo::exists(restore) && !recovered.isLoaded(), "restore dry run performs validation without output", error);
	ok &= expect(restorePackageRecovery(store, firstId, withRedo.manifestSha256, restore, &recovered, &error)
		&& recovered.canRedo() && !recovered.isModified(), "restore to an independent saved draft", error);
	ok &= expect(recovered.redo() && payload(recovered, QStringLiteral("later.txt")) == "cancelled", "restored redo preserves bytes");
	ok &= expect(!QJsonDocument::fromJson(read(QDir(restore).filePath(QStringLiteral("document.json")))).object().contains(QStringLiteral("recovery")), "normal save drops the recovery envelope");
	ok &= expect(!restorePackageRecovery(store, firstId, withRedo.manifestSha256, restore, nullptr, &error), "restore never overwrites an existing draft");
	ok &= expect(!restorePackageRecovery(store, firstId, withRedo.manifestSha256, QDir(store).filePath(QStringLiteral("nested.vibepackage")), nullptr, &error), "restore cannot target its recovery store");
	// Drop the redo branch and the last accepted operation; its blobs must be
	// reclaimed only after a new manifest commits. The prior restore is separate.
	PackageStagingModel olderCheckpoint;
	ok &= expect(PackageDraft::load(copyPath, &olderCheckpoint, &error), "retain an older checkpoint reader", error);
	ok &= plan.undo();
	ok &= plan.addBytes("replacement", QStringLiteral("new.txt"));
	written = session->checkpoint(plan, QStringLiteral("New branch"));
	ok &= expect(written.succeeded() && !written.maintenanceError.isEmpty()
		&& QFileInfo::exists(objectPath(copyPath, "old redo")) && QFileInfo::exists(objectPath(copyPath, "cancelled")),
		"checkpoint commits while older readers defer cleanup and retain its storage cost", written.error);
	ok &= expect(payload(olderCheckpoint, QStringLiteral("two.txt")) == "old redo"
		&& olderCheckpoint.redo() && payload(olderCheckpoint, QStringLiteral("later.txt")) == "cancelled", "older checkpoint retains readable content and redo after a newer commit");
	const auto retainedUsage = inspectPackageRecovery(copyPath);
	const auto limitedWithReader = session->checkpoint(plan, QStringLiteral("Retained reader quota"), {}, {retainedUsage.storageBytes, 32});
	ok &= expect(!limitedWithReader.succeeded() && inspectPackageRecovery(copyPath).manifestSha256 == written.manifestSha256,
		"deferred cleanup still counts retained unused bytes against checkpoint quota", limitedWithReader.error);
	olderCheckpoint.clear();
	written = session->checkpoint(plan, QStringLiteral("New branch after readers finish"));
	ok &= expect(written.succeeded() && written.maintenanceError.isEmpty()
		&& !QFileInfo::exists(objectPath(copyPath, "old redo")) && !QFileInfo::exists(objectPath(copyPath, "cancelled")), "unreachable objects are reclaimed after commit", written.error + written.maintenanceError);
	ok &= expect(payload(recovered, QStringLiteral("two.txt")) == "old redo"
		&& payload(recovered, QStringLiteral("later.txt")) == "cancelled", "compacting recovery cannot invalidate a restored draft");
	const QString foreign = QDir(copyPath).filePath(QStringLiteral("keep-user-file.txt"));
	ok &= write(foreign, "unrelated");
	ok &= expect(!session->retire(&error) && QFileInfo::exists(manifest) && QFileInfo::exists(objectPath(copyPath, "retained"))
		&& read(foreign) == "unrelated", "unrelated files block retirement before any deletion");
	ok &= QFile::remove(foreign);
	const QString disguised = QDir(copyPath).filePath(QStringLiteral("objects/.writing-user-notes.txt"));
	ok &= write(disguised, "keep");
	ok &= expect(!session->retire(&error) && read(disguised) == "keep" && QFileInfo::exists(manifest), "only exact temporary names belong to recovery storage");
	ok &= QFile::remove(disguised);
	ok &= expect(session->retire(&error) && !QFileInfo::exists(copyPath), "approved retirement removes only the owned copy", error);
	ok &= expect(!session->checkpoint(plan, QStringLiteral("retired")).succeeded(), "retired session cannot resurrect its copy");
	ok &= expect(payload(recovered, QStringLiteral("one.txt")) == "retained", "restore remains readable after checkpoint retirement");
	// Independent source bytes and all undone input bytes survive source removal.
	const QString source = root.filePath(QStringLiteral("source")); QDir().mkpath(source);
	const QString sourceFile = QDir(source).filePath(QStringLiteral("base.txt"));
	const QString imported = root.filePath(QStringLiteral("imported.txt"));
	ok &= write(sourceFile, "source bytes") && write(imported, "import bytes");
	PackageArchive archive; PackageStagingModel disk;
	ok &= archive.load(source, &error) && disk.loadBaseArchive(archive, &error) && disk.addFile(imported, QStringLiteral("added.txt"), &error) && disk.undo();
	const QString diskId = id(), diskPath = packageRecoveryPath(store, diskId);
	auto diskSession = PackageRecoverySession::acquire(store, diskId, &error);
	if (!expect(bool(diskSession), "disk session", error)) { return 1; }
	auto diskWrite = diskSession->checkpoint(disk, QStringLiteral("Disk sources"));
	ok &= expect(diskWrite.succeeded(), "checkpoint undone imports", diskWrite.error);
	ok &= QFile::remove(sourceFile) && QFile::remove(imported);
	ok &= expect(restorePackageRecovery(store, diskId, diskWrite.manifestSha256, root.filePath(QStringLiteral("independent.vibepackage")), &disk, &error)
		&& payload(disk, QStringLiteral("base.txt")) == "source bytes" && disk.redo() && payload(disk, QStringLiteral("added.txt")) == "import bytes", "recovery never reopens recorded source paths", error);
	// Restore verifies every history object, even if the effective view omits it.
	ok &= write(objectPath(diskPath, "import bytes"), "corrupt");
	const QString corruptOutput = root.filePath(QStringLiteral("corrupt.vibepackage"));
	ok &= expect(inspectPackageRecovery(diskPath).readable(), "inventory honestly reports metadata checks only");
	ok &= expect(!restorePackageRecovery(store, diskId, diskWrite.manifestSha256, corruptOutput, nullptr, &error)
		&& !QFileInfo::exists(corruptOutput), "corrupt redo object prevents restore before creating output");
	diskSession.reset();
	ok &= expect(discardPackageRecovery(store, diskId, diskWrite.manifestSha256, true, &error)
		&& QFileInfo::exists(diskPath), "discard dry run preserves a damaged copy", error);
	ok &= expect(!discardPackageRecovery(store, diskId, QByteArray(32, 'x'), false, &error) && QFileInfo::exists(diskPath), "wrong discard digest preserves the copy");
	ok &= expect(discardPackageRecovery(store, diskId, diskWrite.manifestSha256, false, &error) && !QFileInfo::exists(diskPath), "explicit discard can remove corrupt payloads", error);
	// No checkpoint was committed: interrupted object publication is safely
	// retired too, with neither a bogus manifest read error nor recursive deletion.
	const QString emptyId = id(); auto emptySession = PackageRecoverySession::acquire(store, emptyId, &error);
	ok &= expect(emptySession && emptySession->retire(&error) && error.isEmpty(), "retire a session before its first checkpoint", error);
	const QString linkId = id(); auto linkSession = PackageRecoverySession::acquire(store, linkId, &error);
	if (!expect(bool(linkSession), "link session", error)) { return 1; }
	auto linkWrite = linkSession->checkpoint(plan, QStringLiteral("Links"));
	const QString linkCopy = packageRecoveryPath(store, linkId);
	const QString external = root.filePath(QStringLiteral("outside.txt")); ok &= write(external, "outside");
	const QString link = objectPath(linkCopy, "never an object");
	std::error_code linkError;
	std::filesystem::create_symlink(nativePath(external), nativePath(link), linkError);
	if (!linkError) {
		ok &= expect(!linkSession->retire(&error) && read(external) == "outside" && QFileInfo::exists(QDir(linkCopy).filePath(QStringLiteral("document.json"))), "linked object prevents all retirement mutations");
		std::filesystem::remove(nativePath(link), linkError);
	} else { std::cout << "Symbolic link fixture unavailable: " << linkError.message() << '\n'; }
#ifdef Q_OS_WIN
	// A junction exercises the Windows path guard without symlink privilege.
	// Both ends are explicit children of this disposable test directory.
	const QString junctionId = id(), junctionPath = packageRecoveryPath(store, junctionId);
	const QString target = root.filePath(QStringLiteral("junction-target"));
	QDir().mkpath(target); const QString sentinel = QDir(target).filePath(QStringLiteral("keep.txt")); ok &= write(sentinel, "preserved");
	const auto quote = [](QString path) { return path.replace(QLatin1Char('\''), QStringLiteral("''")); };
	QProcess junction;
	junction.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
		QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' -ErrorAction Stop | Out-Null").arg(quote(junctionPath), quote(target))});
	const bool created = junction.waitForFinished(10000) && junction.exitCode() == 0 && QFileInfo(junctionPath).isJunction();
	ok &= expect(created, "Windows recovery must exercise a real junction fixture");
	if (created) {
		auto redirected = PackageRecoverySession::acquire(store, junctionId, &error);
		ok &= expect(!inspectPackageRecovery(junctionPath).readable() && redirected
			&& !redirected->checkpoint(plan, QStringLiteral("Unsafe")).succeeded() && !redirected->retire(&error)
			&& read(sentinel) == "preserved", "linked recovery root rejects reads, writes and retirement without touching its target");
		redirected.reset();
		const bool removed = QFileInfo(junctionPath).isJunction() && QDir().rmdir(junctionPath);
		ok &= expect(removed && read(sentinel) == "preserved", "remove only the verified junction fixture");
		if (!removed) { temporary.setAutoRemove(false); }
	}
#endif
	ok &= expect(linkWrite.succeeded() && linkSession->retire(&error), "retire final test copy", error);
	std::cout << (ok ? "Package recovery smoke passed\n" : "Package recovery smoke failed\n");
	return ok ? 0 : 1;
}
