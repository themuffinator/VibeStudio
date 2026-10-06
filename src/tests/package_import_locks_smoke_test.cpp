#include "core/package_import_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <filesystem>
#include <iostream>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; } return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void hold() { std::cout << "READY\n" << std::flush; for (;;) { QThread::msleep(50); } }
void holdEmpty(const QString& path)
{
#if defined(Q_OS_WIN)
	const auto native = QDir::toNativeSeparators(path);
	const HANDLE file = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file != INVALID_HANDLE_VALUE) { hold(); }
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
	const auto native = QFile::encodeName(path); const int file = ::open(native.constData(), O_RDWR | O_CREAT | O_EXCL, 0600);
	if (file >= 0 && flock(file, LOCK_EX | LOCK_NB) == 0) { hold(); }
#endif
}
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); const auto arguments = app.arguments();
	if (arguments.size() == 4 && arguments.at(1).startsWith(QStringLiteral("--hold-"))) {
		const QString path = QDir(arguments.at(2)).filePath(arguments.at(3));
		if (arguments.at(1) == QStringLiteral("--hold-empty")) { holdEmpty(path); return 2; }
		QLockFile lock(path); lock.setStaleLockTime(0); if (lock.tryLock()) { hold(); } return 2;
	}
	QTemporaryDir temporary; if (!temporary.isValid() || arguments.size() != 2) { return 1; }
	const QDir root(temporary.path()); QString error; bool ok = true;
	const QString store = root.filePath(QStringLiteral("store")), name = QStringLiteral(".store.lock");
	const QString path = QDir(store).filePath(name), id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString session = id + QStringLiteral(".working"), lease = session + QStringLiteral("/.lease");
	ok &= expect(!inspectPackageImportLock(store, name).exists && !QFileInfo::exists(store), "inspection of missing locks creates nothing");
	ok &= expect(!releasePackageImportLock(store, name, QByteArray(32, 'a'), true, &error) && !QFileInfo::exists(store), "missing-lock dry run creates nothing");
	ok &= QDir().mkpath(QDir(store).filePath(session));
	for (const auto& relative : {name, name + QStringLiteral(".rmlock"), lease, lease + QStringLiteral(".rmlock")}) {
		for (const auto& mode : {QStringLiteral("--hold-qt"), QStringLiteral("--hold-empty")}) {
			QProcess child; child.setWorkingDirectory(root.path()); child.start(app.applicationFilePath(), {mode, store, relative});
			QByteArray output;
			for (int attempt = 0; attempt < 100 && !output.contains("READY"); ++attempt) {
				child.waitForReadyRead(100); output += child.readAllStandardOutput(); if (child.state() == QProcess::NotRunning) { break; }
			}
			const bool ready = output.contains("READY"); ok &= expect(ready, "child holds a native lock", QString::fromUtf8(child.readAllStandardError()));
			if (ready) {
				const auto lock = inspectPackageImportLock(store, relative);
				ok &= expect(lock.reviewable(), "live lock can be reviewed without interfering with owner", lock.error);
				ok &= expect(!releasePackageImportLock(store, relative, lock.fingerprint, true, &error)
					&& !releasePackageImportLock(store, relative, lock.fingerprint, false, &error)
					&& QFileInfo::exists(QDir(store).filePath(relative)), "dry run and write refuse live Qt and zero-byte native owners", error);
			}
			child.kill(); child.waitForFinished(10000);
			const auto orphan = inspectPackageImportLock(store, relative);
			ok &= expect(orphan.reviewable() && releasePackageImportLock(store, relative, orphan.fingerprint, true, &error)
				&& QFileInfo::exists(QDir(store).filePath(relative)), "crashed owner can be reviewed without deletion", orphan.error + error);
			ok &= expect(releasePackageImportLock(store, relative, orphan.fingerprint, false, &error)
				&& !QFileInfo::exists(QDir(store).filePath(relative)), "explicit recovery releases crashed lock only", error);
		}
	}
	ok &= write(path, {}); auto reviewed = inspectPackageImportLock(store, name);
	const auto modified = QFileInfo(path).lastModified();
	ok &= expect(reviewed.reviewable() && reviewed.bytes == 0 && releasePackageImportLock(store, name, reviewed.fingerprint, true, &error)
		&& QFileInfo(path).lastModified() == modified && QFileInfo(path).size() == 0, "empty-lock dry run preserves metadata and content", error);
	for (const auto& invalid : {QStringLiteral("../.store.lock"), QStringLiteral("/absolute"), QStringLiteral("working.json"),
		QStringLiteral("other/.lease"), session + QStringLiteral("\\.lease"), session.toUpper() + QStringLiteral("/.lease")}) {
		ok &= expect(!inspectPackageImportLock(store, invalid).reviewable()
			&& !releasePackageImportLock(store, invalid, reviewed.fingerprint, false, &error), "only canonical store and session lock paths are accepted");
	}
	ok &= expect(!releasePackageImportLock(store, name, {}, false, &error) && QFileInfo::exists(path), "missing review checksum refuses deletion");
	ok &= write(path, "changed");
	ok &= expect(!releasePackageImportLock(store, name, reviewed.fingerprint, false, &error) && QFileInfo(path).size() == 7, "changed metadata invalidates the review");
	reviewed = inspectPackageImportLock(store, name);
	const QString moved = root.filePath(QStringLiteral("previous-lock"));
	ok &= QFile::rename(path, moved) && write(path, "changed");
	ok &= expect(!releasePackageImportLock(store, name, reviewed.fingerprint, false, &error) && QFileInfo::exists(path), "replacement with identical bytes invalidates native identity");
	reviewed = inspectPackageImportLock(store, name); bool cancel = false;
	PackageReadControl control; control.isCancelled = [&] { return cancel; }; control.progress = [&](const QString&, qint64, qint64) { cancel = true; };
	ok &= expect(!releasePackageImportLock(store, name, reviewed.fingerprint, false, &error, control) && QFileInfo::exists(path), "cancellation after reading prevents deletion");
	ok &= expect(releasePackageImportLock(store, name, reviewed.fingerprint, false, &error), "cancelled recovery leaves its review reusable", error);
	ok &= write(path, QByteArray(65537, 'x'));
	ok &= expect(!inspectPackageImportLock(store, name).reviewable() && !releasePackageImportLock(store, name, reviewed.fingerprint, false, &error)
		&& QFileInfo(path).size() == 65537, "oversized lock metadata is refused without deletion");
	ok &= QFile::remove(path);
	{
		const auto native = [](const QString& value) { return std::filesystem::path(value.toStdU16String()); };
		std::error_code result; std::filesystem::create_hard_link(native(moved), native(path), result);
		ok &= expect(!result, "create independent hard-link refusal fixture", QString::fromStdString(result.message()));
		if (!result) {
			ok &= expect(!inspectPackageImportLock(store, name).reviewable()
				&& !releasePackageImportLock(store, name, reviewed.fingerprint, false, &error), "hard-linked lock files cannot be released");
			ok &= QFile::remove(path) && QFileInfo::exists(moved);
		}
		std::filesystem::create_symlink(native(moved), native(path), result);
		if (!result) {
			ok &= expect(!inspectPackageImportLock(store, name).reviewable()
				&& !releasePackageImportLock(store, name, reviewed.fingerprint, false, &error), "symbolic lock files cannot be released");
			ok &= QFile::remove(path) && QFileInfo::exists(moved);
		} else { std::cout << "Symlink fixture unavailable: " << result.message() << '\n'; }
	}
	const QString payload = QDir(store).filePath(session + QStringLiteral("/objects/") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".blob"));
	ok &= QDir().mkpath(QFileInfo(payload).absolutePath()) && write(payload, "retained payload")
		&& write(QDir(store).filePath(session + QStringLiteral("/working.json")), "{")
		&& write(QDir(store).filePath(lease), {}) && write(QDir(store).filePath(lease + QStringLiteral(".rmlock")), {});
	const auto inventory = listPackageImports(store);
	ok &= expect(inventory.locks.size() == 2 && inventory.sessions.size() == 1 && inventory.sessions.first().reviewable(), "inventory lists malformed leases and removal guards independently of payload review");
	if (!inventory.sessions.isEmpty()) {
		const auto info = inventory.sessions.first();
		ok &= expect(!discardPackageImports(store, id, info.fingerprint, false, &error) && QFileInfo(payload).size() == 16, "session guard blocks discard before any payload is removed");
		for (const auto& lock : inventory.locks) { ok &= expect(releasePackageImportLock(store, lock.relativePath, lock.fingerprint, false, &error), "release reviewed session locks", error); }
		ok &= expect(QFileInfo::exists(payload) && discardPackageImports(store, id, info.fingerprint, false, &error), "lock release keeps payloads until separate reviewed discard", error);
	}
	{
		QString nested = root.filePath(QStringLiteral("long-path"));
		while (nested.size() < 290) { nested += QStringLiteral("/lock-review-long-component"); }
		ok &= QDir().mkpath(nested) && write(QDir(nested).filePath(name), {});
		const auto lock = inspectPackageImportLock(nested, name);
		ok &= expect(lock.reviewable() && releasePackageImportLock(nested, name, lock.fingerprint, false, &error), "long absolute paths retain the same native review and release contract", lock.error + error);
	}
	{
		const QString profile = root.filePath(QStringLiteral("cli.ini"));
		const auto cli = [&](const QStringList& args, int expected) {
			QProcess process; process.setWorkingDirectory(root.path()); auto env = QProcessEnvironment::systemEnvironment();
			for (const auto& key : {QStringLiteral("TEMP"), QStringLiteral("TMP"), QStringLiteral("TMPDIR")}) { env.insert(key, root.path()); }
			process.setProcessEnvironment(env);
			process.start(arguments.at(1), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), profile, QStringLiteral("package")} + args + QStringList{QStringLiteral("--json")});
			const bool ended = process.waitForFinished(15000); if (!ended) { process.kill(); process.waitForFinished(10000); }
			const auto bytes = process.readAllStandardOutput(); const auto json = QJsonDocument::fromJson(bytes);
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && json.isObject(), "strict lock CLI returns expected structured result", QString::fromUtf8(bytes + process.readAllStandardError()));
			return json.object();
		};
		ok &= write(path, {});
		const auto locks = cli({QStringLiteral("working-imports"), QStringLiteral("--directory"), store}, 0).value(QStringLiteral("inventory")).toObject().value(QStringLiteral("locks")).toArray();
		if (locks.size() != 1) { return 1; }
		const auto hash = locks.first().toObject().value(QStringLiteral("lockSha256")).toString();
		ok &= expect(!QFileInfo::exists(profile), "inventory reads configured limits without creating a settings file");
		cli({QStringLiteral("recoveries"), QStringLiteral("--directory"), root.filePath(QStringLiteral("absent-recoveries"))}, 0);
		ok &= expect(!QFileInfo::exists(profile), "recovery inventory also leaves settings absent");
		const QStringList command{QStringLiteral("working-unlock"), name, QStringLiteral("--directory"), store, QStringLiteral("--expected-lock-sha256"), hash};
		const auto dry = cli(command, 0);
		ok &= expect(dry.value(QStringLiteral("dryRun")).toBool() && !dry.value(QStringLiteral("released")).toBool()
			&& dry.value(QStringLiteral("ownerExclusionChecked")).toBool() && QFileInfo::exists(path), "CLI default dry run proves native exclusion without mutation");
		cli(command + QStringList{QStringLiteral("--write"), QStringLiteral("--dry-run")}, 2);
		cli(command + QStringList{QStringLiteral("--write=false")}, 2);
		cli(command + QStringList{QStringLiteral("--expected-lock-sha256"), hash}, 2);
		cli(command + QStringList{QStringLiteral("--expected-storage-sha256"), hash}, 2);
		cli(command + QStringList{QStringLiteral("--unknown")}, 2);
		cli(command + QStringList{QStringLiteral("extra")}, 2);
		const auto released = cli(command + QStringList{QStringLiteral("--write")}, 0);
		ok &= expect(released.value(QStringLiteral("released")).toBool() && !QFileInfo::exists(path) && !QFileInfo::exists(profile), "CLI explicit release removes only the reviewed lock and writes no settings");
		cli(command, 4);
	}
	std::cout << (ok ? "Package import locks smoke passed\n" : "Package import locks smoke failed\n"); return ok ? 0 : 1;
}
