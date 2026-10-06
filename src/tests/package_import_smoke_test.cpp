#include "core/package_content.h"
#include "core/package_draft.h"
#include "core/package_recovery.h"
#include "core/package_import_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimeZone>
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
QByteArray payload(const PackageStagingModel& plan, const QString& path)
{
	QByteArray bytes; PackageStagingArchive(plan).readEntryBytes(path, &bytes, nullptr); return bytes;
}
bool removed(const QString& path)
{
	QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); return !QFileInfo::exists(path);
}
QStringList temporaryImports()
{
	QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup();
	const auto inventory = listPackageImports(packageImportDirectory()); QStringList result;
	if (!inventory.complete()) { result << inventory.error; }
	for (const auto& info : inventory.sessions) { result << info.id + QString::fromLatin1(info.fingerprint.toHex()); }
	return result;
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); QString error; bool ok = true;
	const QString importDirectory = root.filePath(QStringLiteral("working-imports"));
	if (!QDir().mkpath(importDirectory)) { return 1; }
	for (const char* name : {"TEMP", "TMP", "TMPDIR"}) { qputenv(name, importDirectory.toLocal8Bit()); }
	if (!expect(QDir::cleanPath(QDir::tempPath()) == importDirectory, "snapshot test has an isolated temporary directory")) { return 1; }
	const QString source = root.filePath(QStringLiteral("import.bin"));
	const QByteArray original(4 * 1024 * 1024 + 7, 'a');
	ok &= write(source, original);
	const QDateTime logicalTime = QDateTime::fromSecsSinceEpoch(946684800, QTimeZone::utc());
	{ QFile file(source); ok &= file.open(QIODevice::ReadWrite) && file.setFileTime(logicalTime, QFileDevice::FileModificationTime); }
	QString retainedPath;
	{
		PackageStagingModel plan; ok &= plan.createEmpty(PackageArchiveFormat::Pk3);
		ok &= expect(plan.addFile(source, QStringLiteral("data.bin"), &error), "retain an independent import", error);
		retainedPath = plan.operations().first().sourceIdentity->path;
		ok &= expect(retainedPath != source && QFileInfo::exists(retainedPath) && plan.operations().first().sourceIdentity->storage
			&& plan.operations().first().sourceFilePath == source && plan.plannedEntries().first().modifiedUtc == logicalTime,
			"retention owns bytes while preserving source provenance and logical time");
		ok &= write(source, "changed original");
		ok &= expect(payload(plan, QStringLiteral("data.bin")) == original && plan.verifySources(&error)
			&& plan.undo() && QFileInfo::exists(retainedPath) && plan.redo() && payload(plan, QStringLiteral("data.bin")) == original,
			"original changes never invalidate live content or undo history", error);
		ok &= QFile::remove(source);
		const auto manifest = QJsonDocument::fromJson(plan.manifestJson()).object();
		ok &= expect(manifest.value(QStringLiteral("operations")).toArray().first().toObject().value(QStringLiteral("contentStorage")) == QStringLiteral("owned-temporary"),
			"manifest explains retained import storage");
		ok &= expect(PackageStagingArchive(plan).protectsInputPath(source) && PackageStagingArchive(plan).protectsInputPath(retainedPath), "both original and backing paths remain protected from output");
		auto reader = std::make_unique<PackageStagingArchive>(plan);
		const QString draft = root.filePath(QStringLiteral("retained.vibepackage"));
		ok &= expect(PackageDraft::save(draft, &plan, false, &error) && !plan.operations().first().sourceIdentity->storage
			&& QFileInfo::exists(retainedPath), "draft adoption replaces its temporary backing without invalidating an older reader", error);
		plan.clear(); QByteArray bytes;
		ok &= expect(reader->readEntryBytes(QStringLiteral("data.bin"), &bytes, &error) && bytes == original,
			"background snapshot survives document save and close", error);
		reader.reset();
		ok &= expect(removed(retainedPath) && PackageDraft::load(draft, &plan, &error)
			&& payload(plan, QStringLiteral("data.bin")) == original, "last reader releases temporary bytes; persisted draft remains independent", error);
	}
	{
		ok &= write(source, "history"); PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
		ok &= plan.addFile(source, QStringLiteral("history.txt"), &error);
		const QString path = plan.operations().first().sourceIdentity->path, operation = plan.operations().first().id;
		ok &= plan.clearOperation(operation); ok &= QFile::remove(source);
		ok &= expect(QFileInfo::exists(path) && plan.undo() && payload(plan, QStringLiteral("history.txt")) == "history",
			"unstaging preserves its payload for undo");
		ok &= plan.undo(); // Undo the original insertion, leaving it on the redo branch.
		ok &= plan.addBytes("new branch", QStringLiteral("new.txt"));
		ok &= expect(removed(path), "discarding an unreachable redo branch releases its temporary content");
		ok &= write(source, "cancel group"); plan.beginOperationGroup(QStringLiteral("Group"));
		ok &= plan.addFile(source, QStringLiteral("group.txt"), &error);
		const QString grouped = plan.operations().last().sourceIdentity->path;
		plan.endOperationGroup(false);
		ok &= expect(removed(grouped) && plan.operations().size() == 1, "abandoned edit group releases new imports");
	}
	{
		ok &= write(source, original); const auto files = temporaryImports();
		PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); plan.addBytes("kept", QStringLiteral("kept.txt"));
		const quint64 revision = plan.revision(); bool cancelled = false; int passes = 0;
		PackageReadControl control;
		control.isCancelled = [&] { return cancelled; };
		control.progress = [&](const QString&, qint64 done, qint64) {
			if (done == 0) { ++passes; }
			if (passes == 2 && done >= PackageFileIdentity::chunkBytes) { cancelled = true; }
		};
		ok &= expect(!plan.addFile(source, QStringLiteral("cancel.bin"), &error, PackageStageConflictResolution::Block, control)
			&& cancelled && passes == 2 && plan.revision() == revision && plan.operations().size() == 1 && temporaryImports() == files,
			"within-copy cancellation adopts neither an edit nor a temporary file", error);
		const auto identity = capturePackageFileIdentity(source, &error);
		const QString unusable = root.filePath(QStringLiteral("not-a-directory")); ok &= write(unusable, "keep");
		ok &= expect(!retainPackageFileContent(identity, &error, {}, unusable) && verifyPackageFileIdentity(identity, &error),
			"failed snapshot destination leaves original content intact", error);
		bool changed = false;
		PackageReadControl mutate;
		mutate.progress = [&](const QString&, qint64 done, qint64) {
			if (done == PackageFileIdentity::chunkBytes && !changed) {
				QFile file(source); changed = file.open(QIODevice::ReadWrite) && file.seek(2 * PackageFileIdentity::chunkBytes)
					&& file.write("b") == 1 && file.flush() && file.setFileTime(identity->modifiedUtc, QFileDevice::FileModificationTime);
			}
		};
		ok &= expect(!retainPackageFileContent(identity, &error, mutate) && changed && temporaryImports() == files,
			"a changed unread chunk rejects retention even when timestamps and size match", error);
	}
	{
		ok &= write(source, "borrowed"); const auto files = temporaryImports();
		PackageStagingModel diagnostic; diagnostic.createEmpty(PackageArchiveFormat::Pk3);
		ok &= expect(diagnostic.addFile(source, QStringLiteral("check.txt"), &error, PackageStageConflictResolution::Block, {}, PackageFileImportMode::VerifyOnly)
			&& diagnostic.operations().first().sourceIdentity->path == source && !diagnostic.operations().first().sourceIdentity->storage
			&& temporaryImports() == files, "read-only diagnostic staging creates no retained file", error);
		ok &= write(source, "changed");
		ok &= expect(!diagnostic.verifySources(&error), "read-only plans still reject changed live sources");
	}
	{
		// Platform temporary directories may use a symlink/junction alias. Only
		// the OS default is resolved; an explicit linked storage path is refused.
		const QString alias = root.filePath(QStringLiteral("temporary-alias"));
		std::error_code linkError;
		std::filesystem::create_directory_symlink(std::filesystem::path(importDirectory.toStdU16String()),
			std::filesystem::path(alias.toStdU16String()), linkError);
		bool linkAvailable = !linkError;
#ifdef Q_OS_WIN
		if (!linkAvailable) {
			const auto quoted = [](QString path) { return path.replace(QLatin1Char('\''), QStringLiteral("''")); };
			QProcess junction; junction.setWorkingDirectory(root.path());
			junction.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
				QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' -ErrorAction Stop | Out-Null")
					.arg(quoted(alias), quoted(importDirectory))});
			linkAvailable = junction.waitForFinished(15000) && junction.exitStatus() == QProcess::NormalExit && junction.exitCode() == 0;
		}
#endif
		if (linkAvailable) {
			ok &= write(source, "temporary alias"); const auto originalIdentity = capturePackageFileIdentity(source, &error);
			ok &= expect(!retainPackageFileContent(originalIdentity, &error, {}, alias), "explicit linked snapshot directory remains refused");
			for (const char* name : {"TEMP", "TMP", "TMPDIR"}) { qputenv(name, alias.toLocal8Bit()); }
			auto identity = retainPackageFileContent(originalIdentity, &error);
			ok &= expect(identity && identity->path.startsWith(QFileInfo(importDirectory).canonicalFilePath() + QStringLiteral("/vibestudio-package-imports/"))
				&& verifyPackageFileIdentity(identity, &error), "default temporary alias resolves to independent canonical storage", error);
			const QString path = identity ? identity->path : QString(); identity.reset();
			ok &= expect(!path.isEmpty() && removed(path), "resolved default temporary storage releases normally");
			for (const char* name : {"TEMP", "TMP", "TMPDIR"}) { qputenv(name, importDirectory.toLocal8Bit()); }
			linkError.clear();
			ok &= expect(std::filesystem::remove(std::filesystem::path(alias.toStdU16String()), linkError)
				&& !linkError && QFileInfo(importDirectory).isDir(), "remove only the synthetic temporary alias");
		} else {
			std::cout << "Temporary alias fixture unavailable: " << linkError.message() << '\n';
		}
	}
	{
		ok &= write(source, "reader lifetime");
		auto identity = retainPackageFileContent(capturePackageFileIdentity(source, &error), &error);
		if (!expect(bool(identity), "direct retained content", error)) { return 1; }
		const QString path = identity->path;
		auto device = std::make_unique<PackageContentDevice>(identity); ok &= device->open(); identity.reset();
		ok &= QFile::remove(source);
		ok &= expect(device->readAll() == "reader lifetime" && QFileInfo::exists(path), "a content device retains its backing independently");
		device.reset(); ok &= expect(removed(path), "last device releases its backing");
		ok &= write(source, "worker release");
		identity = retainPackageFileContent(capturePackageFileIdentity(source, &error), &error);
		if (!expect(bool(identity), "worker retained content", error)) { return 1; }
		const QString workerPath = identity->path;
		auto* worker = QThread::create([held = std::move(identity)]() mutable { held.reset(); });
		worker->start(); worker->wait(); delete worker;
		ok &= expect(removed(workerPath), "plain storage ownership can end safely on a worker");
	}
	{
		ok &= write(source, "replacement"); PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
		plan.addBytes("base", QStringLiteral("entry.txt"));
		ok &= plan.replaceFile(QStringLiteral("entry.txt"), source, &error); ok &= QFile::remove(source);
		ok &= expect(payload(plan, QStringLiteral("entry.txt")) == "replacement" && plan.undo()
			&& payload(plan, QStringLiteral("entry.txt")) == "base" && plan.redo(), "replacement history owns imported bytes");
		const QString store = root.filePath(QStringLiteral("recovery")); const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		auto recovery = PackageRecoverySession::acquire(store, id, &error);
		if (!expect(bool(recovery), "retained import recovery", error)) { return 1; }
		const auto saved = recovery->checkpoint(plan, QStringLiteral("Retained"));
		ok &= expect(saved.succeeded(), "recovery can checkpoint an import after original deletion", saved.error);
		plan.clear();
		ok &= expect(restorePackageRecovery(store, id, saved.manifestSha256, root.filePath(QStringLiteral("recovered.vibepackage")), &plan, &error)
			&& payload(plan, QStringLiteral("entry.txt")) == "replacement", "recovery remains independent of temporary snapshot lifetime", error);
		ok &= recovery->retire(&error);
	}
	QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup();
	std::cout << (ok ? "Package import smoke passed\n" : "Package import smoke failed\n");
	return ok ? 0 : 1;
}
