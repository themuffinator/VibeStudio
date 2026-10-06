#include "package_copy_test_helpers.h"
#include "core/studio_settings.h"
#include "core/package_copy_store.h"

#include <QDir>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <barrier>
#include <limits>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

using namespace vibestudio;
using namespace package_copy_test;

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid() || argc < 2) { return 1; }
	bool ok = true; QString error;
	PackageCopyBudget parallel; PackageCopyLimits narrow{12, 4, 4, 4};
	std::barrier gate(17); std::atomic_int admitted = 0; std::vector<std::thread> threads;
	for (int i = 0; i < 16; ++i) {
		threads.emplace_back([&] { gate.arrive_and_wait(); auto reservation = parallel.reserve(3, 1, 1, narrow);
			if (reservation) { ++admitted; } gate.arrive_and_wait(); gate.arrive_and_wait(); });
	}
	gate.arrive_and_wait(); gate.arrive_and_wait();
	const auto pending = parallel.usage();
	ok &= expect(admitted == 4 && pending.bytes == 12 && pending.files == 4 && pending.entries == 4
		&& pending.batches == 4 && pending.pendingBatches == 4, "concurrent reservations cannot overbook any session limit");
	gate.arrive_and_wait(); for (auto& thread : threads) { thread.join(); }
	ok &= expect(parallel.usage().bytes == 0 && parallel.usage().batches == 0, "uncommitted reservations release all counters");
	ok &= expect(!parallel.reserve(std::numeric_limits<quint64>::max(), 1, 1, narrow), "overflow-sized reservations cannot wrap admission");
	for (const auto limits : {PackageCopyLimits{100, 1, 10, 10}, PackageCopyLimits{100, 10, 1, 10}, PackageCopyLimits{100, 10, 10, 1}}) {
		PackageCopyBudget budget; auto first = budget.reserve(1, 1, 1, limits); if (first) { first->retain(); }
		ok &= expect(first && !budget.reserve(1, 1, 1, limits), "file, entry and batch caps independently reject aggregate overflow");
	}
	Reader reader; reader.add(QStringLiteral("a.txt"), "AAAA"); reader.add(QStringLiteral("b.txt"), "BBBB"); reader.folder(QStringLiteral("empty"));
	PackageCopyRequest request; request.parentDirectory = QDir(temporary.path()).canonicalPath(); request.entryIndexes = {0};
	request.budget = std::make_shared<PackageCopyBudget>(); request.sessionLimits = {8, 2, 2, 2};
	auto first = copyPackageEntries(reader, request); auto second = copyPackageEntries(reader, request);
	const int reads = reader.streamed;
	const auto blocked = copyPackageEntries(reader, request);
	ok &= expect(first.succeeded() && second.succeeded() && !blocked.succeeded() && blocked.error.contains("session")
		&& reader.streamed == reads && read(first.paths.first()) == "AAAA" && request.budget->usage().bytes == 8,
		"completed copies retain their reservations and a full session refuses reads before output");
	first.storage.reset(); second.storage.reset();
	ok &= expect(request.budget->usage().batches == 2, "dropping a caller's lease does not invalidate session-lifetime accounting");
	request.sessionLimits.maximumBytes = 3; request.entryIndexes = {2};
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && request.budget->usage().bytes == 8, "lowering limits preserves reservations and blocks new empty batches too");
	request.sessionLimits = {8, 2, 3, 4}; request.budget = std::make_shared<PackageCopyBudget>(); request.entryIndexes = {0, 1};
	reader.failAtEnd = 1;
	const auto failed = copyPackageEntries(reader, request);
	ok &= expect(!failed.succeeded() && failed.paths.isEmpty() && request.budget->usage().batches == 0, "verified cleanup releases a failed batch reservation");
	reader.failAtEnd = -1; bool cancelled = false;
	request.control.isCancelled = [&] { return cancelled; };
	request.control.progress = [&](const QString& path, qint64, qint64) { if (path == QStringLiteral("b.txt")) { cancelled = true; } };
	const auto stopped = copyPackageEntries(reader, request);
	ok &= expect(stopped.cancelled && stopped.extraction.writtenCount == 1 && request.budget->usage().bytes == 0
		&& request.budget->usage().pendingBatches == 0, "cancellation removes earlier committed output before releasing its reservation");
	request.control = {};
#ifdef Q_OS_WIN
	for (const bool cancelCase : {false, true}) {
		QTemporaryDir nativeRoot(temporary.filePath(QStringLiteral("native-cleanup-XXXXXX")));
		if (!nativeRoot.isValid()) { return 1; }
		request.parentDirectory = QDir(nativeRoot.path()).canonicalPath();
		request.budget = std::make_shared<PackageCopyBudget>();
		bool stop = false;
		request.control.isCancelled = [&] { return stop; };
		HANDLE locked = INVALID_HANDLE_VALUE;
		request.control.progress = [&](const QString& path, qint64, qint64) {
			if (path != QStringLiteral("b.txt") || locked != INVALID_HANDLE_VALUE) { return; }
			const auto batches = QDir(request.parentDirectory).entryList({QStringLiteral("package-copy-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
			if (batches.size() != 1) { return; }
			const QString file = QDir(request.parentDirectory).filePath(batches.first() + QStringLiteral("/a.txt"));
			locked = CreateFileW(reinterpret_cast<LPCWSTR>(file.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (cancelCase) { stop = true; }
		};
		reader.failAtEnd = cancelCase ? -1 : 1;
		const auto unclean = copyPackageEntries(reader, request);
		ok &= expect(locked != INVALID_HANDLE_VALUE && !unclean.succeeded() && unclean.paths.isEmpty()
			&& unclean.cancelled == cancelCase
			&& unclean.error.contains("cleanup failed") && request.budget->usage().bytes == 8
			&& request.budget->usage().cleanupFailedBatches == 1 && request.budget->usage().pendingBatches == 0,
			"failed and cancelled batches retain full reservations when native deletion fails");
		if (locked != INVALID_HANDLE_VALUE) { CloseHandle(locked); }
	}
	request.control = {};
#endif
#ifdef Q_OS_WIN
	{
		QTemporaryDir nativeRoot(temporary.filePath(QStringLiteral("prepared-cleanup-XXXXXX")));
		if (!nativeRoot.isValid()) { return 1; }
		PackageCopyRequest pendingRequest;
		const auto store = nativeRoot.filePath(QStringLiteral("store"));
		pendingRequest.session = PackageCopySession::create(store, &error);
		if (!expect(pendingRequest.session && pendingRequest.session->isValid(), "create managed native rollback session")) { return 1; }
		pendingRequest.entryIndexes = {0}; pendingRequest.budget = std::make_shared<PackageCopyBudget>();
		reader.failAtEnd = -1;
		auto prepared = preparePackageCopyEntries(reader, pendingRequest);
		if (!expect(prepared.prepared(), "prepare native rollback fixture")) { return 1; }
		const auto path = prepared.paths.first();
		HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		ok &= expect(locked != INVALID_HANDLE_VALUE && !discardPreparedPackageCopies(&prepared) && prepared.cancelled
			&& !prepared.storage && prepared.paths.isEmpty() && prepared.error.contains("cleanup failed")
			&& pendingRequest.budget->usage().bytes == 4 && pendingRequest.budget->usage().cleanupFailedBatches == 1
			&& pendingRequest.budget->usage().pendingBatches == 0 && QFileInfo::exists(path),
			"failed prepared-batch deletion exposes no handoff and retains its full charge for review");
		const auto quota = inspectPackageCopyQuota(store);
		ok &= expect(quota.complete() && quota.reserved.bytes == 4 && quota.reserved.cleanupFailedBatches == 1
			&& quota.reserved.pendingBatches == 0, "failed prepared deletion retains the shared durable reservation too");
		if (locked != INVALID_HANDLE_VALUE) { CloseHandle(locked); }
		pendingRequest.session.reset(); waitForPackageCopyCleanup();
	}
#endif
	const QString settingsPath = temporary.filePath(QStringLiteral("limits.ini"));
	const auto cli = [&](const QStringList& arguments, int expected = 0) {
		QProcess process; process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--settings-file", settingsPath, "package", "copy-limits", "--json"} + arguments);
		const bool ended = process.waitForFinished(15000);
		ok &= expect(ended && process.exitCode() == expected, "copy limit CLI returns the documented exit code");
		if (!ended) { process.kill(); process.waitForFinished(); }
		const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
		ok &= expect(!result.isEmpty(), "copy limit CLI returns structured JSON"); return result;
	};
	auto result = cli({});
	ok &= expect(!QFileInfo::exists(settingsPath) && result.value("scope").toString() == QStringLiteral("studio-window")
		&& !result.value("usageAvailable").toBool(true) && result.value("limits").toObject().value("maximumBatches").toInt() == 64,
		"inspection reports scope and defaults without inventing live usage or creating settings");
	result = cli({"--max-mib=3", "--max-files", "7", "--max-entries", "8", "--max-batches", "9"});
	ok &= expect(!QFileInfo::exists(settingsPath) && result.value("dryRun").toBool() && result.value("limits").toObject().value("maximumFiles").toInt() == 7,
		"proposed limits are a no-write dry run");
	cli({"--max-mib", "3", "--max-files", "7", "--max-entries", "8", "--max-batches", "9", "--write"});
	const QByteArray saved = read(settingsPath);
	for (const QStringList& invalid : {QStringList{"--max-files", "0"}, {"--max-mib", "18446744073709551615"}, {"--max-batches", "1.5"},
		{"--max-entries", "2", "--max-entries=3"}, {"--max-files", "2", "--write", "--dry-run"}, {"--write"}, {"--unknown"}, {"--write=false"}}) {
		cli(invalid, 2); ok &= expect(read(settingsPath) == saved, "invalid limit commands leave settings byte-for-byte unchanged");
	}
	cli({"--max-files", "2", "--dry-run"}); ok &= expect(read(settingsPath) == saved, "existing preferences survive proposed changes");
	result = cli({});
	ok &= expect(result.value("limits").toObject().value("maximumBytes").toInteger() == 3 * 1024 * 1024
		&& result.value("limits").toObject().value("maximumEntries").toInt() == 8, "written values round-trip through the actual CLI");
	const auto replaceSettings = [&](const QByteArray& data) { QFile file(settingsPath); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); };
	const QByteArray legacy("[custom]\nvalue=preserved\n");
	ok &= expect(replaceSettings(legacy), "create legacy settings fixture"); cli({}); cli({"--max-files", "4"});
	ok &= expect(read(settingsPath) == legacy, "inspection and dry run never migrate legacy settings");
	const QByteArray future("[app]\nsettingsSchemaVersion=9999\n[custom]\nvalue=preserved\n");
	ok &= expect(replaceSettings(future), "create future settings fixture"); cli({"--max-files", "4", "--write"}, 1);
	ok &= expect(read(settingsPath) == future, "future-schema settings reject writes without changing bytes");
	return ok ? 0 : 1;
}
