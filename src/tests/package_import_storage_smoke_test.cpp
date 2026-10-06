#include "core/package_import_store.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSettings>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

#include <array>
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
void drain() { QThreadPool::globalInstance()->waitForDone(); waitForPackageImportCleanup(); }
PackageReadControl optionsFor(const QString& directory, qint64 bytes = 100, int files = 10)
{
	PackageReadControl control; auto options = std::make_shared<PackageImportOptions>();
	options->directory = directory; options->maximumBytes = bytes; options->maximumFiles = files;
	control.importOptions = options; return control;
}
void hold() { std::cout << "READY\n" << std::flush; for (;;) { QThread::msleep(50); } }
}

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const auto arguments = app.arguments();
	if (arguments.size() == 4 && arguments.at(1).startsWith(QStringLiteral("--hold-"))) {
		QString error; auto control = optionsFor(arguments.at(2), 1000000);
		const auto identity = capturePackageFileIdentity(arguments.at(3), &error);
		if (arguments.at(1) == QStringLiteral("--hold-reservation")) {
			const auto reservation = reservePackageImport(identity, &error, control);
			if (reservation) { hold(); } return 2;
		}
		control.progress = [&](const QString&, qint64 done, qint64) { if (done >= PackageFileIdentity::chunkBytes) { hold(); } };
		const auto retained = retainPackageFileContent(identity, &error, control); return retained ? 0 : 2;
	}
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	const QDir root(temporary.path()); const QString source = root.filePath(QStringLiteral("source.bin"));
	const QString store = root.filePath(QStringLiteral("working")); QString error; bool ok = true;
	const auto empty = listPackageImports(store);
	ok &= expect(empty.complete() && empty.sessions.isEmpty() && !QFileInfo::exists(store), "inventory never creates an absent store");
	ok &= write(source, QByteArray(40, 'a'));
	{
		PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); const auto control = optionsFor(store, 80, 2);
		ok &= expect(plan.addFile(source, QStringLiteral("first.bin"), &error, PackageStageConflictResolution::Block, control)
			&& plan.addFile(source, QStringLiteral("second.bin"), &error, PackageStageConflictResolution::Block, control), "reserve two complete imports", error);
		const auto inventory = listPackageImports(store);
		ok &= expect(inventory.complete() && inventory.sessions.size() == 1 && inventory.bytes == 80 && inventory.reservedBytes == 80
			&& inventory.files == 2 && inventory.reservedFiles == 2 && inventory.sessions.first().leasePresent, "inventory reports actual and reserved working payloads");
		const auto revision = plan.revision();
		ok &= expect(!plan.addFile(source, QStringLiteral("over.bin"), &error, PackageStageConflictResolution::Block, control)
			&& plan.revision() == revision && plan.operations().size() == 2 && listPackageImports(store).reservedBytes == 80,
			"quota refusal preserves edits and reservations", error);
		const auto info = inventory.sessions.first();
		ok &= expect(discardPackageImports(store, info.id, info.fingerprint, true, &error)
			&& !QFileInfo::exists(QDir(store).filePath(QStringLiteral(".store.lock"))), "dry-run storage review creates no lock", error);
		ok &= expect(!discardPackageImports(store, info.id, info.fingerprint, false, &error)
			&& listPackageImports(store).files == 2, "maintenance cannot delete a live session", error);
		PackageWriteRequest request; request.destinationPath = QDir(info.path).filePath(QStringLiteral("working.json")); request.allowOverwrite = true;
		ok &= expect(!plan.writeArchive(request).succeeded(), "package publication cannot overwrite working-store accounting");
		PackageStagingModel flattened; ok &= flattened.loadBaseArchive(PackageStagingArchive(plan), &error);
		ok &= expect(!flattened.writeArchive(request).succeeded(), "flattened planned views retain working-store protection");
		flattened.clear();
		auto reader = std::make_unique<PackageStagingArchive>(plan);
		const QString draft = root.filePath(QStringLiteral("saved.vibepackage"));
		ok &= expect(PackageDraft::save(draft, &plan, false, &error), "persist owned working content", error);
		plan.clear(); drain();
		ok &= expect(listPackageImports(store).files == 2, "older readers keep their file reservations after save and close");
		reader.reset(); drain();
		ok &= expect(listPackageImports(store).sessions.isEmpty(), "last reader reclaims data, reservations and empty session");
	}
	{
		const auto control = optionsFor(store, 0, 1); ok &= write(source, {});
		auto first = retainPackageFileContent(capturePackageFileIdentity(source, &error), &error, control);
		ok &= expect(bool(first) && listPackageImports(store).reservedFiles == 1, "empty imports still reserve a file slot", error);
		ok &= expect(!retainPackageFileContent(capturePackageFileIdentity(source, &error), &error, control), "file count bounds empty imports");
		first.reset(); drain(); ok &= expect(listPackageImports(store).sessions.isEmpty(), "empty import cleanup releases its slot");
	}
	{
		ok &= write(source, QByteArray(40, 'r')); const auto original = capturePackageFileIdentity(source, &error);
		const auto control = optionsFor(store, 40, 10);
		std::array<PackageFileIdentityPtr, 2> copies; std::array<QString, 2> errors;
		std::array<QThread*, 2> workers;
		for (int i = 0; i < 2; ++i) { workers[i] = QThread::create([&, i]() { copies[i] = retainPackageFileContent(original, &errors[i], control); }); workers[i]->start(); }
		for (auto* worker : workers) { worker->wait(); delete worker; }
		ok &= expect(bool(copies[0]) != bool(copies[1]), "concurrent imports cannot both consume the same byte budget");
		ok &= expect(listPackageImports(store).reservedBytes == 40, "concurrent admission accounts exactly one copy");
		for (auto& copy : copies) { copy.reset(); } drain();
	}
	for (const QString& mode : {QStringLiteral("--hold-reservation"), QStringLiteral("--hold-copy")}) {
		ok &= write(source, QByteArray(200000, 'c'));
		QProcess child; child.setWorkingDirectory(root.path());
		child.start(QCoreApplication::applicationFilePath(), {mode, store, source});
		QByteArray output;
		for (int attempt = 0; attempt < 100 && !output.contains("READY"); ++attempt) {
			child.waitForReadyRead(100); output += child.readAllStandardOutput();
			if (child.state() == QProcess::NotRunning) { break; }
		}
		const bool ready = output.contains("READY");
		ok &= expect(ready, "child holds a live reservation", QString::fromUtf8(child.readAllStandardError()));
		if (ready) {
			const auto inventory = listPackageImports(store);
			ok &= expect(inventory.complete() && inventory.sessions.size() == 1 && inventory.reservedBytes == 200000
				&& inventory.files == (mode == QStringLiteral("--hold-copy") ? 1 : 0), "in-progress and not-yet-created bytes are reserved across processes");
			const auto info = inventory.sessions.first();
			ok &= expect(!discardPackageImports(store, info.id, info.fingerprint, false, &error), "a different process cannot discard a live reservation");
			ok &= expect(!retainPackageFileContent(capturePackageFileIdentity(source, &error), &error, optionsFor(store, 200000)),
				"cross-process reservations prevent quota oversubscription");
			child.kill(); child.waitForFinished(10000);
			const auto orphan = listPackageImports(store);
			ok &= expect(orphan.complete() && orphan.sessions.size() == 1 && orphan.reservedBytes == 200000, "abrupt termination leaves a reviewable conservative reservation");
			const auto abandoned = orphan.sessions.first();
			ok &= expect(discardPackageImports(store, abandoned.id, abandoned.fingerprint, true, &error)
				&& QFileInfo::exists(abandoned.path), "orphan dry run preserves all storage", error);
			ok &= expect(discardPackageImports(store, abandoned.id, abandoned.fingerprint, false, &error)
				&& listPackageImports(store).sessions.isEmpty(), "reviewed cleanup reclaims a crashed session after proving its lease is stale", error);
		} else { child.kill(); child.waitForFinished(10000); }
	}
	{
		const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), path = QDir(store).filePath(id + QStringLiteral(".working"));
		ok &= QDir().mkpath(QDir(path).filePath(QStringLiteral("objects")));
		const QString payload = QDir(path).filePath(QStringLiteral("objects/") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".blob"));
		ok &= write(QDir(path).filePath(QStringLiteral("working.json")), "{") && write(payload, "partial");
		auto inventory = listPackageImports(store); auto info = inventory.sessions.first();
		ok &= expect(info.reviewable() && !info.error.isEmpty(), "incomplete metadata can still be explicitly reviewed");
		ok &= expect(!retainPackageFileContent(capturePackageFileIdentity(source, &error), &error, optionsFor(store, 1000000)), "unaccountable sessions block new imports");
		ok &= write(payload, "changed partial");
		ok &= expect(!discardPackageImports(store, id, info.fingerprint, false, &error) && QFileInfo::exists(payload), "stale review checksum protects changed files");
		const QString unknown = QDir(path).filePath(QStringLiteral("user-notes.txt")); ok &= write(unknown, "preserve");
		inventory = listPackageImports(store);
		ok &= expect(!inventory.complete() && !inventory.sessions.first().reviewable(), "unrecognized paths prevent cleanup");
		ok &= QFile::remove(unknown); info = listPackageImports(store).sessions.first();
		ok &= expect(discardPackageImports(store, id, info.fingerprint, false, &error), "explicit cleanup handles a recognized incomplete session", error);
	}
	{
		ok &= write(source, QByteArray(40, 'c')); const auto control = optionsFor(store, 80, 2);
		auto retained = retainPackageFileContent(capturePackageFileIdentity(source, &error), &error, control);
		const auto inventory = listPackageImports(store);
		if (!expect(retained && inventory.sessions.size() == 1, "prepare reservation checksum regression", error)) { return 1; }
		const auto info = inventory.sessions.first(); const QString manifest = QDir(info.path).filePath(QStringLiteral("working.json"));
		QFile original(manifest); ok &= original.open(QIODevice::ReadOnly); const auto originalBytes = original.readAll(); original.close();
		auto changed = QJsonDocument::fromJson(originalBytes).object(); changed.insert(QStringLiteral("reservedBytes"), QStringLiteral("0"));
		ok &= write(manifest, QJsonDocument(changed).toJson(QJsonDocument::Compact));
		const auto damaged = listPackageImports(store);
		ok &= expect(!damaged.complete() && damaged.sessions.size() == 1 && damaged.sessions.first().reviewable()
			&& !damaged.sessions.first().error.isEmpty(), "changed reservation counters fail checksum validation but remain reviewable");
		ok &= expect(!retainPackageFileContent(capturePackageFileIdentity(source, &error), &error, control)
			&& QFileInfo::exists(retained->path), "counter tampering cannot admit an import or remove live content");
		ok &= write(manifest, originalBytes); retained.reset(); drain();
		ok &= expect(listPackageImports(store).sessions.isEmpty(), "restored counters allow normal owner cleanup");
	}
	{
		ok &= write(source, QByteArray(200000, 'x')); auto control = optionsFor(store, 200000);
		bool cancel = false; control.isCancelled = [&]() { return cancel; };
		control.progress = [&](const QString&, qint64 done, qint64) { if (done >= 65536) { cancel = true; } };
		ok &= expect(!retainPackageFileContent(capturePackageFileIdentity(source, &error), &error, control) && cancel, "copy cancellation fails before adoption");
		drain(); ok &= expect(listPackageImports(store).sessions.isEmpty(), "cancelled copies release both partial payload and reservation");
	}
	{
		ok &= write(source, "one owner"); const auto control = optionsFor(store);
		auto reservation = reservePackageImport(capturePackageFileIdentity(source, &error), &error, control);
		if (!expect(bool(reservation), "prepare one-time ownership transfer", error)) { return 1; }
		const QString path = reservation->path(); ok &= write(path, "one owner");
		const auto identity = capturePackageFileIdentity(path, &error);
		if (!expect(bool(identity), "verify reserved payload", error)) { return 1; }
		auto owner = reservation->retain(*identity);
		ok &= expect(owner && !reservation->retain(*identity), "a reservation transfers content ownership only once");
		reservation.reset(); drain();
		ok &= expect(QFileInfo::exists(path) && listPackageImports(store).reservedFiles == 1, "retained owner survives reservation destruction");
		owner.reset(); drain();
		ok &= expect(listPackageImports(store).sessions.isEmpty(), "one retained owner releases exactly one reservation");
	}
	{
		const QString crowded = root.filePath(QStringLiteral("crowded"));
		for (int i = 0; i <= PackageImportSessionLimit; ++i) {
			ok &= QDir().mkpath(QDir(crowded).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".working")));
		}
		const auto inventory = listPackageImports(crowded);
		ok &= expect(!inventory.complete() && inventory.sessions.isEmpty()
			&& !QFileInfo::exists(QDir(crowded).filePath(QStringLiteral(".store.lock"))), "root scan enforces the session bound even without a store lock file");
	}
	{
		const auto control = optionsFor(store, 1000000); PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3);
		ok &= write(source, "bounded cleanup");
		for (int index = 0; index < 8; ++index) {
			ok &= plan.addFile(source, QStringLiteral("cleanup-%1.bin").arg(index), &error, PackageStageConflictResolution::Block, control);
		}
		const QString rootLock = QDir(store).filePath(QStringLiteral(".store.lock")); ok &= write(rootLock, {});
		QElapsedTimer timer; timer.start(); plan.clear(); drain();
		const auto retained = listPackageImports(store);
		ok &= expect(timer.elapsed() < 8000 && retained.sessions.size() == 1 && retained.files == 8 && retained.reservedFiles == 8
			&& !retained.sessions.first().leasePresent, "one blocked cleanup retains its session without repeating the lock timeout per queued file");
		const auto lock = inspectPackageImportLock(store, QStringLiteral(".store.lock"));
		ok &= expect(releasePackageImportLock(store, lock.relativePath, lock.fingerprint, false, &error), "blocked cleanup retains an explicitly recoverable lock", error);
		if (!retained.sessions.isEmpty()) {
			const auto info = retained.sessions.first();
			ok &= expect(discardPackageImports(store, info.id, info.fingerprint, false, &error), "retained blocked-cleanup files remain reviewable for explicit discard", error);
		}
	}
	if (arguments.size() != 2) { std::cerr << "Pass the VibeStudio CLI binary for route tests\n"; return 1; }
	{
		const QString profile = root.filePath(QStringLiteral("cli.ini")), cliTemporary = root.filePath(QStringLiteral("cli-temporary"));
		ok &= QDir().mkpath(cliTemporary);
		{ QSettings settings(profile, QSettings::IniFormat); settings.setValue(QStringLiteral("packages/importMaximumFiles"), 1); }
		const auto cli = [&](const QStringList& command, int expected) {
			QProcess process; process.setWorkingDirectory(root.path()); auto environment = QProcessEnvironment::systemEnvironment();
			for (const auto& key : {QStringLiteral("TEMP"), QStringLiteral("TMP"), QStringLiteral("TMPDIR")}) { environment.insert(key, cliTemporary); }
			process.setProcessEnvironment(environment);
			process.start(arguments.at(1), QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"), profile, QStringLiteral("package")} + command + QStringList{QStringLiteral("--json")});
			const bool ended = process.waitForFinished(15000);
			if (!ended) { process.kill(); process.waitForFinished(10000); }
			const auto output = process.readAllStandardOutput();
			ok &= expect(ended && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected,
				"working storage CLI exit status", QString::fromUtf8(output + process.readAllStandardError()));
			QJsonParseError parse; const auto json = QJsonDocument::fromJson(output, &parse);
			ok &= expect(parse.error == QJsonParseError::NoError && json.isObject(), "working storage CLI returns structured output"); return json.object();
		};
		const QString absent = root.filePath(QStringLiteral("cli-absent"));
		const auto clean = cli({QStringLiteral("working-imports"), QStringLiteral("--directory"), absent}, 0);
		ok &= expect(!QFileInfo::exists(absent) && clean.value(QStringLiteral("limits")).toObject().value(QStringLiteral("maximumFiles")).toInt() == 1,
			"CLI inventory is read-only and reports configured limits");
		cli({QStringLiteral("working-imports"), QStringLiteral("--write")}, 2);
		cli({QStringLiteral("working-imports"), QStringLiteral("--directory"), absent, QStringLiteral("--directory"), absent}, 2);
		const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), path = QDir(store).filePath(id + QStringLiteral(".working"));
		ok &= QDir().mkpath(QDir(path).filePath(QStringLiteral("objects")));
		ok &= write(QDir(path).filePath(QStringLiteral("working.json")), "{");
		const auto listed = cli({QStringLiteral("working-imports"), QStringLiteral("--directory"), store}, 4);
		const auto rows = listed.value(QStringLiteral("inventory")).toObject().value(QStringLiteral("sessions")).toArray();
		if (!expect(rows.size() == 1, "CLI exposes incomplete session review")) { return 1; }
		const QString hash = rows.first().toObject().value(QStringLiteral("storageSha256")).toString();
		const QStringList discard{QStringLiteral("working-discard"), id, QStringLiteral("--directory"), store, QStringLiteral("--expected-storage-sha256"), hash};
		const auto preview = cli(discard, 0);
		ok &= expect(preview.value(QStringLiteral("dryRun")).toBool() && !preview.value(QStringLiteral("discarded")).toBool()
			&& !preview.value(QStringLiteral("leaseChecked")).toBool() && QFileInfo::exists(path)
			&& !QFileInfo::exists(QDir(path).filePath(QStringLiteral(".lease"))), "CLI dry run explicitly leaves lease liveness unproven without creating locks");
		cli(discard + QStringList{QStringLiteral("--write"), QStringLiteral("--dry-run")}, 2);
		cli(discard + QStringList{QStringLiteral("--write=no")}, 2);
		cli(discard + QStringList{QStringLiteral("--unknown")}, 2);
		cli(discard + QStringList{QStringLiteral("extra")}, 2);
		const auto removed = cli(discard + QStringList{QStringLiteral("--write")}, 0);
		ok &= expect(removed.value(QStringLiteral("discarded")).toBool() && removed.value(QStringLiteral("leaseChecked")).toBool()
			&& !QFileInfo::exists(path), "CLI explicit discard proves the lease and removes the reviewed session");
		const QString draft = root.filePath(QStringLiteral("limited.vibepackage"));
		const QStringList create{QStringLiteral("create"), draft, QStringLiteral("--format"), QStringLiteral("pk3"),
			QStringLiteral("--import-file"), source, QStringLiteral("--as"), QStringLiteral("first.bin"),
			QStringLiteral("--import-file"), source, QStringLiteral("--as"), QStringLiteral("second.bin")};
		cli(create + QStringList{QStringLiteral("--dry-run")}, 0);
		ok &= expect(!QFileInfo::exists(QDir(cliTemporary).filePath(QStringLiteral("vibestudio-package-imports"))), "dry-run imports allocate no working store");
		const auto rejected = cli(create, 2);
		ok &= expect(!QFileInfo::exists(draft) && !rejected.value(QStringLiteral("ok")).toBool(), "configured file quota blocks the actual CLI import before draft commit");
		const QString cliStore = QDir(cliTemporary).filePath(QStringLiteral("vibestudio-package-imports"));
		ok &= expect(listPackageImports(cliStore).sessions.isEmpty() && listPackageImports(cliStore).locks.isEmpty(), "failed CLI operation drains working payload and lock cleanup before process exit");
		for (int attempt = 0; attempt < 3; ++attempt) {
			const QString output = root.filePath(QStringLiteral("normal-exit-%1.vibepackage").arg(attempt));
			cli({QStringLiteral("create"), output, QStringLiteral("--format"), QStringLiteral("pk3"),
				QStringLiteral("--import-file"), source, QStringLiteral("--as"), QStringLiteral("first.bin")}, 0);
			const auto afterExit = listPackageImports(cliStore);
			ok &= expect(QFileInfo::exists(output) && afterExit.complete() && afterExit.sessions.isEmpty() && afterExit.locks.isEmpty(),
				"normal CLI exit drains payload, session and lock teardown before the next process starts");
		}
	}
	drain();
	std::cout << (ok ? "Package import storage smoke passed\n" : "Package import storage smoke failed\n");
	return ok ? 0 : 1;
}
