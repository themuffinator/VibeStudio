#include "package_copy_test_helpers.h"
#include "core/package_copy_store.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <filesystem>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

using namespace vibestudio;
using namespace package_copy_test;
namespace {
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
// The parent must never observe an empty, merely-created readiness file.
bool publishReady(const QString& path, const QString& session)
{
	QSaveFile file(path); file.setDirectWriteFallback(false); const auto bytes = session.toUtf8();
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
void populate(Reader& reader)
{
	reader.add(QStringLiteral("scripts/a.txt"), "AAAA"); reader.add(QStringLiteral("scripts/b.txt"), "BBBB"); reader.folder(QStringLiteral("empty"));
}
bool linkDirectory(const QString& target, const QString& link, const QString& working)
{
	std::error_code error;
	std::filesystem::create_directory_symlink(std::filesystem::path(target.toStdU16String()), std::filesystem::path(link.toStdU16String()), error);
	if (!error) { return true; }
#ifdef Q_OS_WIN
	const auto quote = [](QString path) { return path.replace(QLatin1Char('\''), QStringLiteral("''")); };
	QProcess process; process.setWorkingDirectory(working);
	process.start(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"),
		QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' -ErrorAction Stop | Out-Null").arg(quote(link), quote(target))});
	return process.waitForFinished(10000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && QFileInfo(link).isJunction();
#else
	Q_UNUSED(working); return false;
#endif
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const auto arguments = app.arguments();
	if (arguments.size() == 5 && arguments[1] == QStringLiteral("--hold")) {
		Reader reader; populate(reader); PackageCopyRequest request;
		request.storeDirectory = arguments[2]; request.entryIndexes = {0, 1, 2};
		if (arguments[4] == QStringLiteral("creating")) {
			request.control.progress = [&](const QString& path, qint64 completed, qint64 total) {
				if (completed || total || !path.endsWith(QStringLiteral(".copies"))) { return; }
				if (!publishReady(arguments[3], path)) { std::exit(2); }
				for (;;) { QThread::msleep(20); }
			};
		}
		const auto copy = copyPackageEntries(reader, request);
		if (!copy.succeeded() || !copy.session || !publishReady(arguments[3], copy.session->path())) { std::cerr << copy.error.toStdString(); return 2; }
		for (;;) { QThread::msleep(20); }
	}
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const auto cleanup = qScopeGuard([] { waitForPackageCopyCleanup(); });
	const QString root = QDir(temporary.path()).canonicalPath(); bool ok = true; QString error;
	const QString absent = QDir(root).filePath(QStringLiteral("absent"));
	const auto empty = listPackageCopies(absent);
	ok &= expect(empty.complete() && empty.sessions.isEmpty() && !QFileInfo::exists(absent), "listing a missing copy store creates nothing");
	Reader reader; populate(reader); PackageCopyRequest request; request.storeDirectory = QDir(root).filePath(QStringLiteral("normal")); request.entryIndexes = {0, 1, 2};
	QString retainedPath; std::shared_ptr<const QTemporaryDir> retained;
	{
		auto copy = copyPackageEntries(reader, request);
		if (!expect(copy.succeeded() && copy.session && copy.session->isValid(), "prepare a managed copy session")) { std::cerr << copy.error.toStdString(); return 1; }
		retainedPath = copy.session->path(); retained = copy.storage;
		const auto inventory = listPackageCopies(request.storeDirectory);
		ok &= expect(inventory.complete() && inventory.sessions.size() == 1 && inventory.bytes == 8 && inventory.files == 2
			&& inventory.sessions.first().batches == 1 && inventory.sessions.first().reviewable() && !inventory.sessions.first().discardAvailable,
			"managed inventory reports actual payloads and native owner exclusion");
		if (!inventory.sessions.isEmpty()) {
			const auto& session = inventory.sessions.first();
			ok &= expect(!discardPackageCopies(request.storeDirectory, session.id, session.fingerprint, true, &error)
				&& !discardPackageCopies(request.storeDirectory, session.id, session.fingerprint, false, &error), "dry and written discard both refuse a live session");
		}
	}
	waitForPackageCopyCleanup();
	ok &= expect(QFileInfo::exists(retainedPath) && !listPackageCopies(request.storeDirectory).sessions.first().discardAvailable,
		"a directory lease alone retains the session and its native ownership");
	retained.reset(); waitForPackageCopyCleanup();
	ok &= expect(!QFileInfo::exists(retainedPath), "normal last-owner release cleans the session on the dedicated worker");

	for (const QString& kind : {QStringLiteral("ready"), QStringLiteral("creating"), QStringLiteral("locked"), QStringLiteral("cancelled")}) {
		const QString store = QDir(root).filePath(kind), ready = QDir(root).filePath(kind + QStringLiteral(".ready"));
		QProcess child; child.setWorkingDirectory(root);
		child.start(app.applicationFilePath(), {QStringLiteral("--hold"), store, ready, kind == QStringLiteral("creating") ? kind : QStringLiteral("ready")});
		QElapsedTimer timer; timer.start();
		while (!QFileInfo::exists(ready) && child.state() != QProcess::NotRunning && timer.elapsed() < 10000) { child.waitForReadyRead(20); }
		if (!expect(QFileInfo::exists(ready), "child creates an owned session before interruption")) {
			std::cerr << child.readAllStandardError().toStdString(); child.kill(); child.waitForFinished(5000); return 1;
		}
		const QString path = QString::fromUtf8(read(ready));
		if (kind != QStringLiteral("creating")) {
			const auto active = listPackageCopies(store);
			ok &= expect(active.complete() && active.sessions.size() == 1 && !active.sessions.first().discardAvailable,
				"another process's live session is excluded from maintenance");
		}
		child.kill(); ok &= expect(child.waitForFinished(5000), "abruptly stop only the owned test child");
		auto inventory = listPackageCopies(store);
		if (!expect(inventory.complete() && inventory.sessions.size() == 1, "interrupted session has a bounded review")) { std::cerr << inventory.error.toStdString(); return 1; }
		auto info = inventory.sessions.first();
		ok &= expect(info.reviewable() && info.discardAvailable && info.path == path, "native ownership releases on abrupt process exit");
		if (kind == QStringLiteral("creating")) {
			ok &= expect(!info.error.isEmpty() && info.files == 0 && !QFileInfo::exists(QDir(path).filePath(QStringLiteral("session.json"))),
				"interrupted initial creation is reviewable without inventing a completed manifest");
		} else {
			const auto batches = QDir(path).entryList({QStringLiteral("package-copy-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
			if (!expect(batches.size() == 1, "owned child has one batch")) { return 1; }
			const QString payload = QDir(path).filePath(batches.first() + QStringLiteral("/scripts/a.txt"));
			if (kind == QStringLiteral("ready")) {
				ok &= expect(write(payload, "modified by the temporary consumer"), "modify only the owned orphan payload");
				ok &= expect(!discardPackageCopies(store, info.id, info.fingerprint, false, &error) && QFileInfo::exists(payload), "a changed review cannot delete a modified copy");
				bool cancelled = false; PackageReadControl control;
				control.isCancelled = [&] { return cancelled; }; control.progress = [&](const QString&, qint64, qint64) { cancelled = true; };
				const auto partial = listPackageCopies(store, control);
				ok &= expect(partial.cancelled && !partial.complete() && QFileInfo::exists(payload), "cancelled inventory is explicitly incomplete and preserves payloads");
			}
#ifdef Q_OS_WIN
			if (kind == QStringLiteral("locked")) {
				const auto native = QDir::toNativeSeparators(payload);
				HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
				ok &= expect(handle != INVALID_HANDLE_VALUE, "hold a real copy file without delete sharing");
				if (handle != INVALID_HANDLE_VALUE) {
					ok &= expect(!discardPackageCopies(store, info.id, info.fingerprint, false, &error) && QFileInfo::exists(payload)
						&& QFileInfo::exists(QDir(path).filePath(QStringLiteral("session.json"))), "failed native deletion retains payload and ownership record for review");
					CloseHandle(handle);
				}
			}
#endif
			if (kind == QStringLiteral("cancelled")) {
				bool cancelled = false; PackageReadControl control;
				control.isCancelled = [&] { return cancelled; }; control.progress = [&](const QString&, qint64 done, qint64 total) { if (total > 0 && done == 1) { cancelled = true; } };
				ok &= expect(!discardPackageCopies(store, info.id, info.fingerprint, false, &error, control) && cancelled
					&& QFileInfo::exists(QDir(path).filePath(QStringLiteral("session.json"))), "cancel after one removal retains a reviewable incomplete cleanup");
			}
		}
		inventory = listPackageCopies(store); if (!expect(inventory.complete() && inventory.sessions.size() == 1, "refresh remaining copy session")) { return 1; }
		info = inventory.sessions.first(); const auto before = QJsonDocument(packageCopyInventoryJson(inventory)).toJson();
		ok &= expect(discardPackageCopies(store, info.id, info.fingerprint, true, &error)
			&& QJsonDocument(packageCopyInventoryJson(listPackageCopies(store))).toJson() == before, "reviewed dry run preserves the complete inventory without lock files");
		ok &= expect(discardPackageCopies(store, info.id, info.fingerprint, false, &error) && !QFileInfo::exists(path), "reviewed orphan cleanup removes only the selected session");
	}

	request.storeDirectory = QDir(root).filePath(QStringLiteral("unsafe"));
	auto copy = copyPackageEntries(reader, request); if (!expect(copy.succeeded(), "prepare link-refusal fixture")) { return 1; }
	const QString path = copy.session->path(); const QString outside = QDir(root).filePath(QStringLiteral("outside")); QDir().mkpath(outside);
	const QString sentinel = QDir(outside).filePath(QStringLiteral("keep.txt")); ok &= expect(write(sentinel, "preserve"), "write outside sentinel");
	const QString link = copy.storage->filePath(QStringLiteral("redirect"));
	const bool linked = linkDirectory(outside, link, root); ok &= expect(linked, "create a real filesystem redirect in copy storage");
	if (linked) {
		const auto inventory = listPackageCopies(request.storeDirectory);
		ok &= expect(!inventory.complete() && !inventory.sessions.isEmpty() && !inventory.sessions.first().reviewable(), "linked payload trees cannot be reviewed for recursive cleanup");
		copy.storage.reset(); copy.session.reset(); waitForPackageCopyCleanup();
		ok &= expect(QFileInfo::exists(path) && read(sentinel) == "preserve", "normal cleanup retains an unsafe copy tree without following its link");
		const QFileInfo info(link);
		ok &= expect(info.isJunction() ? QDir().rmdir(link) : info.isSymLink() && QFile::remove(link), "remove only the verified fixture link");
		const auto refreshed = listPackageCopies(request.storeDirectory);
		if (!refreshed.sessions.isEmpty()) {
			const auto& value = refreshed.sessions.first();
			ok &= expect(discardPackageCopies(request.storeDirectory, value.id, value.fingerprint, false, &error) && read(sentinel) == "preserve", "safe reviewed cleanup after removing the fixture link preserves the outside file");
		}
	}
	const QString capped = QDir(root).filePath(QStringLiteral("session-cap")); QDir().mkpath(capped);
	for (int i = 0; i < PackageCopyStoreSessionLimit; ++i) {
		ok &= expect(QDir().mkdir(QDir(capped).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".copies"))), "create owned empty interrupted session");
	}
	const auto full = listPackageCopies(capped);
	ok &= expect(full.complete() && full.sessions.size() == PackageCopyStoreSessionLimit
		&& !PackageCopySession::create(capped, &error) && QDir(capped).entryList({QStringLiteral("*.copies")}, QDir::Dirs | QDir::NoDotAndDotDot).size() == PackageCopyStoreSessionLimit,
		"session admission refuses the full store before creating another session");
	const QString extra = QDir(capped).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".copies"));
	ok &= expect(QDir().mkdir(extra) && !listPackageCopies(capped).complete() && QDir().rmdir(extra), "over-limit session inspection reports incomplete storage");
	const QString deepStore = QDir(root).filePath(QStringLiteral("depth"));
	const QString deepRoot = QDir(deepStore).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".copies"));
	QString leaf = QDir(deepRoot).filePath(QStringLiteral("package-copy-deep12"));
	for (int i = 0; i < PackageCopyStoreDepthLimit; ++i) { leaf += QStringLiteral("/a"); }
	ok &= expect(QDir().mkpath(leaf), "create owned depth-limit fixture");
	const auto deep = listPackageCopies(deepStore);
	ok &= expect(!deep.complete() && deep.sessions.size() == 1 && !deep.sessions.first().reviewable(), "over-depth trees refuse a discard token");
	ok &= expect(QDir().rmdir(leaf), "remove only the final owned fixture directory");
	const auto bounded = listPackageCopies(deepStore);
	ok &= expect(bounded.complete() && bounded.sessions.size() == 1 && bounded.sessions.first().reviewable(), "exactly bounded depth remains reviewable");
	if (!bounded.sessions.isEmpty()) {
		const auto& info = bounded.sessions.first();
		ok &= expect(discardPackageCopies(deepStore, info.id, info.fingerprint, false, &error), "explicit cleanup handles bounded deep trees without recursion");
	}
	PackageReadControl alreadyCancelled; alreadyCancelled.isCancelled = [] { return true; };
	ok &= expect(listPackageCopies(absent, alreadyCancelled).cancelled && !QFileInfo::exists(absent), "cancelled missing-store review stays incomplete and creates nothing");
	return ok ? 0 : 1;
}
