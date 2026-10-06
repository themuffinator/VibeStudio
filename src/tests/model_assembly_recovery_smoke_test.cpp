#include "core/model_assembly_recovery.h"
#include "core/studio_settings.h"
#include "tests/model_assembly_test_helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QByteArray hash(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
ModelAssemblyRecoverySnapshot fixture(const QString &directory)
{
	ModelAssemblyRecoverySnapshot snapshot;
	snapshot.assembly = tests::assemblyRecipe("missing.mesh.json");
	snapshot.assembly.parts[1].sourceKind = ModelAssemblySource::Package;
	snapshot.assembly.parts[1].source = "models/child.md3";
	snapshot.assembly.parts[1].phase = .75;
	snapshot.directory = directory;
	snapshot.selectedPart = "child";
	snapshot.seconds = 1.25;
	return snapshot;
}
int interruptibleChild(const QStringList &args)
{
	if (args.size() != 4)
	{
		return EXIT_FAILURE;
	}
	auto snapshot = fixture(args[2]);
	auto session = ModelAssemblyRecoverySession::acquire(args[2], args[3]);
	if (!session || !session->write(snapshot))
	{
		return EXIT_FAILURE;
	}
	snapshot.seconds = 9;
	ModelWorkControl control;
	control.progress = [](ModelWorkPhase phase, qint64 completed, qint64)
	{
		if (phase == ModelWorkPhase::Committing && completed == 0)
		{
			std::cout << "staged-before-commit" << std::endl;
			QThread::sleep(60);
		}
	};
	return session->write(snapshot, nullptr, control) ? EXIT_SUCCESS : EXIT_FAILURE;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().value(1) == "--interruptible-child")
	{
		return interruptibleChild(app.arguments());
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("assembly-recovery-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	const auto directory = modelAssemblyRecoveryDirectory(), id = uuid();
	QString error;
	auto snapshot = fixture(temporary.path());
	ModelAssemblyDocument original;
	bool ok = expect(original.setAssembly(snapshot.assembly, snapshot.directory, &error) &&
						 original.save(path("source.assembly.json"), false, &error),
					 "original recipe fixture");
	snapshot.sourcePath = original.path();
	snapshot.sourceSha256 = original.sourceFingerprint();
	const auto originalBytes = read(snapshot.sourcePath);
	auto session = ModelAssemblyRecoverySession::acquire(directory, id, &error);
	ok &= expect(session && session->write(snapshot, &error), "atomic recovery publication");
	if (!session || !ok)
	{
		std::cerr << error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	const auto recoveryPath = session->path();
	bool scanCancelled = false;
	ModelWorkControl scanControl;
	scanControl.cancelled = [&] { return scanCancelled; };
	scanControl.progress = [&](ModelWorkPhase phase, qint64 complete, qint64 total)
	{
		if (phase == ModelWorkPhase::Reading && complete == total && total > modelAssemblyRecoveryScanLimit)
		{
			scanCancelled = true;
		}
	};
	ok &= expect(!listModelAssemblyRecoveries(directory, scanControl).error.isEmpty(),
				 "cancellation while reading the final entry cancels the complete inventory");
	ok &= expect(!listModelAssemblyRecoveries(path("empty-inventory"), scanControl).error.isEmpty(),
				 "empty inventory still observes cancellation");
	ModelAssemblyRecoverySnapshot restored;
	auto record = inspectModelAssemblyRecovery(recoveryPath, &restored);
	ok &= expect(record.isValid() && record.parts == 2 && record.sessionFilePresent && record.sourceSha256 == hash(originalBytes) &&
					 modelAssemblyRecoveryFingerprint(restored) == modelAssemblyRecoveryFingerprint(snapshot),
				 "full linked recipe, provenance, selection and time round trip");
	ok &= expect(!ModelAssemblyRecoverySession::acquire(directory, id, &error) &&
					 !discardModelAssemblyRecovery(directory, id, record.sha256, false, &error),
				 "active session prevents a second writer and discard");
	const auto entriesBefore = QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
	ok &= expect(discardModelAssemblyRecovery(directory, id, record.sha256, true, &error) && read(recoveryPath) == read(record.path) &&
					 QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) == entriesBefore,
				 "discard dry run performs no lock or data writes");
	ModelAssemblyDocument draft;
	ok &= expect(restoreModelAssemblyRecovery(recoveryPath, record.sha256, &draft, &restored, &error) && draft.path().isEmpty() &&
					 draft.isModified() && draft.selectedPart() == "child" && !draft.canUndo() &&
					 QDir::isAbsolutePath(draft.assembly().parts[0].source) &&
					 draft.assembly().parts[1].sourceKind == ModelAssemblySource::Package,
				 "source-free dirty restoration with rebased file and retained package links");
	ok &= expect(!draft.save(snapshot.sourcePath, true, &error) && !draft.save(snapshot.sourcePath, true, &error, {}, true) &&
					 read(snapshot.sourcePath) == originalBytes,
				 "original source protected even with overwrite and dry run");
	ok &= expect(draft.save(path("recovered.assembly.json"), false, &error) && !draft.isModified() && read(recoveryPath).size() > 0,
				 "new recovered source writes without resolving absent inputs or deleting recovery");
	const auto preserved = read(recoveryPath);
	bool cancel = false;
	ModelWorkControl control;
	control.cancelled = [&] { return cancel; };
	control.progress = [&](ModelWorkPhase phase, qint64 complete, qint64)
	{
		if (phase == ModelWorkPhase::Committing && complete == 0)
		{
			cancel = true;
		}
	};
	snapshot.seconds = 2.5;
	ok &= expect(!session->write(snapshot, &error, control) && read(recoveryPath) == preserved,
				 "cancellation at commit retains exact preceding copy");
	cancel = false;
	control.progress = [&](ModelWorkPhase phase, qint64 complete, qint64)
	{
		if (phase == ModelWorkPhase::Committing && complete == 1)
		{
			cancel = true;
		}
	};
	ok &= expect(session->write(snapshot, &error, control) && inspectModelAssemblyRecovery(recoveryPath).seconds == 2.5,
				 "late cancellation reports a successfully published checkpoint");
	ok &= expect(!restoreModelAssemblyRecovery(recoveryPath, record.sha256, &draft, &restored, &error) &&
					 draft.path() == path("recovered.assembly.json"),
				 "stale reviewed digest refuses restoration without changing destination document");
	// Retiring an earlier dirty document and checkpointing its replacement share
	// this short-lived storage lock. A normal overlap must not lose the new copy.
	QLockFile inventoryLock(QDir(directory).filePath(".inventory.lock"));
	ok &= expect(inventoryLock.tryLock(0), "hold recovery storage during checkpoint");
	std::atomic_bool waiting = false, cancelWait = false;
	bool wroteAfterWait = false;
	ModelWorkControl waitingControl;
	waitingControl.cancelled = [&] { return cancelWait.load(); };
	waitingControl.progress = [&](ModelWorkPhase phase, qint64, qint64 total)
	{
		if (phase == ModelWorkPhase::Writing && total == 0)
		{
			waiting = true;
		}
	};
	snapshot.seconds = 4;
	auto *waitingWriter = QThread::create([&] { wroteAfterWait = session->write(snapshot, &error, waitingControl); });
	waitingWriter->start();
	for (int attempt = 0; attempt < 100 && !waiting; ++attempt)
	{
		QThread::msleep(5);
	}
	const bool waited = waiting.load();
	inventoryLock.unlock();
	waitingWriter->wait();
	delete waitingWriter;
	ok &= expect(waited && wroteAfterWait && inspectModelAssemblyRecovery(recoveryPath).seconds == 4,
				 "checkpoint waits through normal retirement contention and then publishes");
	const auto beforeCancelledWait = read(recoveryPath);
	ok &= expect(inventoryLock.tryLock(0), "hold storage to cancel a waiting checkpoint");
	waiting = false;
	snapshot.seconds = 5;
	waitingWriter = QThread::create([&] { wroteAfterWait = session->write(snapshot, &error, waitingControl); });
	waitingWriter->start();
	for (int attempt = 0; attempt < 100 && !waiting; ++attempt)
	{
		QThread::msleep(5);
	}
	cancelWait = true;
	waitingWriter->wait();
	delete waitingWriter;
	inventoryLock.unlock();
	ok &= expect(waiting && !wroteAfterWait && read(recoveryPath) == beforeCancelledWait,
				 "cancelling a storage wait preserves the preceding atomic copy");
	const auto validBytes = read(recoveryPath);
	for (int mode = 0; mode < 5; ++mode)
	{
		auto object = QJsonDocument::fromJson(validBytes).object();
		if (mode == 0)
		{
			object.insert("schema", 999);
		}
		if (mode == 1)
		{
			object.insert("id", uuid());
		}
		if (mode >= 2)
		{
			auto payload = object.value("payload").toObject();
			if (mode == 2)
			{
				payload.insert("seconds", 99);
			}
			if (mode == 3)
			{
				payload.insert("selectedPart", "absent");
			}
			if (mode == 4)
			{
				payload.insert("seconds", -1);
			}
			object.insert("payload", payload);
			if (mode != 2)
			{
				object.insert("payloadSha256", QString::fromLatin1(hash(QJsonDocument(payload).toJson(QJsonDocument::Compact)).toHex()));
			}
		}
		ok &= expect(write(recoveryPath, QJsonDocument(object).toJson()) &&
						 !inspectModelAssemblyRecovery(recoveryPath, &restored).isValid() && restored.seconds == 1.25,
					 "unsupported schema, identity, checksum and invalid context refused without mutating snapshot");
	}
	ok &= expect(!session->write(snapshot, &error) && !session->retire(&error) && QFileInfo::exists(recoveryPath),
				 "external replacement survives subsequent checkpoint and automatic retirement");
	session.reset();
	record = inspectModelAssemblyRecovery(recoveryPath);
	ok &= expect(!record.isValid() && record.sha256.size() == 32 &&
					 discardModelAssemblyRecovery(directory, id, record.sha256, false, &error) && !QFileInfo::exists(recoveryPath),
				 "reviewed invalid inactive copy can be discarded");
	ModelAssembly empty;
	ok &= expect(draft.restoreDraft(empty, temporary.path(), {}, original.path(), &error) && draft.isModified() && draft.path().isEmpty() &&
					 draft.save(path("empty.assembly.json"), false, &error) && !draft.isModified(),
				 "empty recovered recipe remains an explicitly saveable dirty draft");
	snapshot.seconds = std::numeric_limits<double>::infinity();
	session = ModelAssemblyRecoverySession::acquire(directory, uuid(), &error);
	ok &= expect(session && !session->write(snapshot, &error) && !QFileInfo::exists(session->path()),
				 "nonfinite timeline refused before publication");
	session.reset();
	const auto fullDirectory = path("full");
	QDir().mkpath(fullDirectory);
	for (int i = 0; i < modelAssemblyRecoveryCountLimit; ++i)
	{
		ok &= write(modelAssemblyRecoveryPath(fullDirectory, uuid()), "invalid");
	}
	session = ModelAssemblyRecoverySession::acquire(fullDirectory, uuid(), &error);
	snapshot.seconds = 3;
	ok &= expect(session && !session->write(snapshot, &error) &&
					 listModelAssemblyRecoveries(fullDirectory).records.size() == modelAssemblyRecoveryCountLimit,
				 "storage count includes invalid copies and never prunes them automatically");
	session.reset();
	const auto largeDirectory = path("large");
	QDir().mkpath(largeDirectory);
	QFile oversized(modelAssemblyRecoveryPath(largeDirectory, uuid()));
	ok &= expect(oversized.open(QIODevice::WriteOnly) && oversized.resize(modelAssemblyRecoveryStorageLimit + 1),
				 "oversized inventory fixture");
	oversized.close();
	session = ModelAssemblyRecoverySession::acquire(largeDirectory, uuid(), &error);
	ok &= expect(session && !session->write(snapshot, &error) && listModelAssemblyRecoveries(largeDirectory).limited,
				 "storage and read budgets account for oversized copies");
	session.reset();
	for (int i = modelAssemblyRecoveryCountLimit; i <= modelAssemblyRecoveryScanLimit; ++i)
	{
		ok &= write(modelAssemblyRecoveryPath(fullDirectory, uuid()), "invalid");
	}
	const auto bounded = listModelAssemblyRecoveries(fullDirectory);
	ok &= expect(bounded.limited && bounded.records.size() == modelAssemblyRecoveryScanLimit,
				 "bounded inventory enumerates at most 128 records");
	control.cancelled = [] { return true; };
	control.progress = {};
	ok &= expect(!listModelAssemblyRecoveries(fullDirectory, control).error.isEmpty() &&
					 modelAssemblyRecoveryPath(directory, "../bad").isEmpty(),
				 "cancelled scan and unsafe identifiers refused");
	const auto crashDirectory = path("interrupted");
	const auto crashId = uuid();
	QProcess child;
	child.setWorkingDirectory(temporary.path());
	child.start(app.applicationFilePath(), {"--interruptible-child", crashDirectory, crashId});
	const bool staged = child.waitForReadyRead(15000) && child.readAllStandardOutput().contains("staged-before-commit");
	child.kill();
	const bool killed = child.waitForFinished(15000);
	record = inspectModelAssemblyRecovery(modelAssemblyRecoveryPath(crashDirectory, crashId));
	ok &= expect(staged && killed && record.isValid() && record.seconds == 1.25 && record.sessionFilePresent,
				 "process termination during replacement retains preceding valid recovery");
	ok &= expect(discardModelAssemblyRecovery(crashDirectory, crashId, record.sha256, false, &error),
				 "Qt reclaims dead-process session, inventory and writer locks");
	std::cout << "Interrupted checkpoint verified before commit; prior recipe retained.\n";

	snapshot = fixture(temporary.path());
	snapshot.sourcePath = original.path();
	snapshot.sourceSha256 = original.sourceFingerprint();
	session = ModelAssemblyRecoverySession::acquire(directory, uuid(), &error);
	ok &= expect(session && session->write(snapshot, &error), "CLI recovery fixture");
	if (!session)
	{
		return EXIT_FAILURE;
	}
	record = inspectModelAssemblyRecovery(session->path());
	QJsonObject output;
	const auto run = [&](QStringList args, int code = 0)
	{
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
					  QStringList{"--cli", "--settings-file", path("settings.ini"), "model", "assembly"} + args + QStringList{"--json"});
		if (!process.waitForFinished(30000))
		{
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		output = QJsonDocument::fromJson(bytes).object();
		if (process.exitCode() != code || output.isEmpty())
		{
			std::cerr << bytes.toStdString() << process.readAllStandardError().toStdString();
			return false;
		}
		return process.exitStatus() == QProcess::NormalExit;
	};
	ok &= expect(run({"--operation", "recoveries"}) && output.value("records").toArray().size() == 1,
				 "CLI inventory shares verified recovery store and isolated profile");
	const QStringList selected{"--recovery", record.id, "--sha256", QString::fromLatin1(record.sha256.toHex())};
	ok &= expect(run(QStringList{"--operation", "recover", "--output", "cli.assembly.json", "--dry-run"} + selected) &&
					 !QFileInfo::exists(path("cli.assembly.json")),
				 "CLI recovery dry run");
	ok &= expect(run(QStringList{"--operation", "recover", "--output", "cli.assembly.json"} + selected) &&
					 output.value("selectedPart") == "child" && output.value("timeSeconds").toDouble() == 1.25 &&
					 QFileInfo::exists(record.path),
				 "CLI recovers absent-input recipe and reports editor context without deleting copy");
	ok &= expect(run(QStringList{"--operation", "recover", "--output", original.path(), "--overwrite"} + selected, 4) &&
					 read(original.path()) == originalBytes,
				 "CLI cannot replace original source with a recovered draft");
	ok &= expect(run({"--operation", "recover", "--recovery", record.id, "--output", "no-digest.assembly.json"}, 2) &&
					 run({"--operation", "recoveries", "--overwrite"}, 2) && run({"--operation", "recoveries", "--time", "0"}, 2),
				 "CLI rejects missing digest and irrelevant flags");
	ok &= expect(run(QStringList{"--operation", "discard"} + selected, 4) &&
					 run(QStringList{"--operation", "discard", "--dry-run"} + selected),
				 "CLI enforces live lease on commit and makes dry run read-only");
	session.reset();
	ok &= expect(run(QStringList{"--operation", "discard"} + selected) && !QFileInfo::exists(record.path), "CLI reviewed inactive discard");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
