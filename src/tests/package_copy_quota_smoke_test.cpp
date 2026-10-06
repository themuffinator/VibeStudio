#include "package_copy_test_helpers.h"
#include "core/package_copy_store.h"

#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <memory>
#include <vector>
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
bool configure(const QString& store, const PackageCopyLimits& limits, QString* error)
{
	return configurePackageCopyQuota(store, limits, inspectPackageCopyQuota(store).policyFingerprint, false, error);
}
}
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv); const auto args = app.arguments();
	if (args.size() == 5 && args[1] == QStringLiteral("--compete")) {
		QString error; auto session = PackageCopySession::create(args[2], &error);
		if (!session || !write(args[4], "waiting")) { std::cerr << error.toStdString(); return 2; }
		while (!QFileInfo::exists(args[3])) { QThread::msleep(5); }
		auto reservation = session->reserve(8, 1, 2, &error);
		if (!write(args[4], reservation ? "accepted" : "refused:" + error.toUtf8())) { return 2; }
		for (;;) { QThread::msleep(10); }
	}
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	const auto cleanup = qScopeGuard([] { waitForPackageCopyCleanup(); });
	const QString root = QDir(temporary.path()).canonicalPath(), store = QDir(root).filePath(QStringLiteral("shared"));
	bool ok = true; QString error;
	const auto absent = inspectPackageCopyQuota(store); const PackageCopyLimits narrow{12, 4, 10, 2};
	ok &= expect(absent.complete() && absent.policyFingerprint.size() == 32 && !absent.configured && absent.reserved.bytes == 0
		&& !QFileInfo::exists(store), "missing shared quota inspection creates nothing");
	ok &= expect(configurePackageCopyQuota(store, narrow, absent.policyFingerprint, true, &error) && !QFileInfo::exists(store), "shared limit preview creates neither policy nor coordination directory");
	ok &= expect(configure(store, narrow, &error), "persist an isolated shared policy");
	const auto policy = inspectPackageCopyQuota(store);
	ok &= expect(policy.complete() && policy.configured && policy.limits.maximumBytes == 12
		&& policy.policyFingerprint != absent.policyFingerprint, "committed shared limits have a new review token");
	ok &= expect(!configurePackageCopyQuota(store, {20, 4, 10, 2}, absent.policyFingerprint, false, &error)
		&& inspectPackageCopyQuota(store).limits.maximumBytes == 12, "stale policy review cannot overwrite current limits");
	const QString gate = QDir(root).filePath(QStringLiteral("go")); std::vector<std::unique_ptr<QProcess>> children; QStringList ready;
	const auto stopChildren = qScopeGuard([&] { for (auto& child : children) { if (child->state() != QProcess::NotRunning) { child->kill(); child->waitForFinished(5000); } } });
	for (int i = 0; i < 4; ++i) {
		ready << QDir(root).filePath(QStringLiteral("ready-%1").arg(i)); auto child = std::make_unique<QProcess>(); child->setWorkingDirectory(root);
		child->start(app.applicationFilePath(), {QStringLiteral("--compete"), store, gate, ready.last()}); children.push_back(std::move(child));
	}
	const auto waitFor = [&](bool waiting) {
		QElapsedTimer timer; timer.start();
		while (timer.elapsed() < 15000) {
			bool complete = true;
			for (const auto& path : ready) { const auto value = read(path); complete &= waiting ? value == "waiting" : value == "accepted" || value.startsWith("refused:"); }
			if (complete) { return true; }
			QThread::msleep(10);
		}
		for (auto& child : children) { std::cerr << child->readAllStandardError().toStdString(); } return false;
	};
	if (!expect(waitFor(true) && write(gate, "go") && waitFor(false), "four native processes reach concurrent reservation admission")) { return 1; }
	int accepted = 0; for (const auto& path : ready) { accepted += read(path) == "accepted"; }
	const auto pending = inspectPackageCopyQuota(store);
	ok &= expect(accepted == 1 && pending.complete() && pending.reserved.bytes == 8 && pending.reserved.files == 1
		&& pending.reserved.entries == 2 && pending.reserved.batches == 1 && pending.reserved.pendingBatches == 1,
		"serialized durable reservations prevent cross-process overbooking before payload creation");
	for (auto& child : children) { child->kill(); ok &= expect(child->waitForFinished(5000), "interrupt only the owned fixture processes"); }
	const auto crashed = inspectPackageCopyQuota(store);
	ok &= expect(crashed.complete() && crashed.reserved.bytes == 8 && crashed.reserved.pendingBatches == 1,
		"abrupt process exit retains the pending charge without a stale coordination lock");
	auto session = PackageCopySession::create(store, &error); if (!expect(session != nullptr, "create another window after crash")) { return 1; }
	ok &= expect(!session->reserve(5, 1, 1, &error), "new work cannot reuse a crashed reservation even with no payload on disk");
	const auto inventory = listPackageCopies(store);
	for (const auto& info : inventory.sessions) {
		if (info.reserved.bytes) { ok &= expect(discardPackageCopies(store, info.id, info.fingerprint, false, &error), "reviewed orphan removal reclaims its shared reservation"); }
	}
	ok &= expect(inspectPackageCopyQuota(store).reserved.bytes == 0, "discard releases only the removed session's quota");
	{
		auto exact = session->reserve(12, 4, 10, &error);
		ok &= expect(exact != nullptr && !session->reserve(0, 0, 1, &error), "exact byte/file/entry limits include pending work");
	}
	ok &= expect(inspectPackageCopyQuota(store).reserved.bytes == 0 && inspectPackageCopyQuota(store).reserved.batches == 0,
		"verified no-output failure releases a pending reservation durably");
	Reader reader; reader.add(QStringLiteral("nested/a.txt"), "AAAA"); reader.add(QStringLiteral("nested/b.txt"), "BBBB");
	PackageCopyRequest request; request.session = session; request.entryIndexes = {0, 1}; request.maximumEntries = 2;
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && reader.streamed == 0, "implicit parent directories count toward entry admission before reads");
	request.maximumEntries = 10; reader.failAtEnd = 1;
	ok &= expect(!copyPackageEntries(reader, request).succeeded() && inspectPackageCopyQuota(store).reserved.batches == 0,
		"stream failure and verified batch cleanup release shared reservations");
	reader.failAtEnd = -1;
	bool cancelled = false; request.control.isCancelled = [&] { return cancelled; };
	request.control.progress = [&](const QString& path, qint64, qint64) { if (path == QStringLiteral("nested/b.txt")) { cancelled = true; } };
	ok &= expect(copyPackageEntries(reader, request).cancelled && inspectPackageCopyQuota(store).reserved.bytes == 0, "cancellation with successful cleanup releases the persistent reservation");
	request.control = {};
	auto copy = copyPackageEntries(reader, request); if (!expect(copy.succeeded(), "prepare a fully retained shared copy")) { return 1; }
	const auto retained = inspectPackageCopyQuota(store);
	ok &= expect(retained.reserved.bytes == 8 && retained.reserved.files == 2 && retained.reserved.entries == 3
		&& retained.reserved.pendingBatches == 0, "completed copies retain exact initial payload and implicit-folder reservations");
	const QString recordPath = QDir(session->path()).filePath(QStringLiteral("session.json")); const auto recordBytes = read(recordPath);
	auto damaged = QJsonDocument::fromJson(recordBytes).object(); auto counter = damaged.value(QStringLiteral("reservations")).toObject();
	counter.insert(QStringLiteral("bytes"), 0); damaged.insert(QStringLiteral("reservations"), counter);
	ok &= expect(write(recordPath, QJsonDocument(damaged).toJson()) && !inspectPackageCopyQuota(store).complete()
		&& !session->reserve(0, 0, 0, &error) && read(copy.paths.first()) == "AAAA", "a counter checksum mismatch blocks new reservations and preserves payloads");
	ok &= expect(write(recordPath, recordBytes) && inspectPackageCopyQuota(store).complete(), "restore the owned counter corruption fixture");
	ok &= expect(configure(store, {4, 1, 1, 1}, &error) && read(copy.paths.first()) == "AAAA"
		&& !session->reserve(0, 0, 0, &error), "lower shared limits preserve copies and block further admission");
	copy.storage.reset(); copy.session.reset(); request.session.reset(); session.reset(); waitForPackageCopyCleanup();
	ok &= expect(inspectPackageCopyQuota(store).reserved.bytes == 0, "normal session removal frees persistent shared charges");
	for (const PackageCopyLimits cap : {PackageCopyLimits{100, 1, 100, 10}, {100, 100, 2, 10}, {100, 100, 100, 1}}) {
		ok &= expect(configure(store, cap, &error), "configure each independent shared resource cap");
		{
			auto first = PackageCopySession::create(store, &error), second = PackageCopySession::create(store, &error);
			if (!expect(first && second, "create two independent window owners")) { return 1; }
			auto heldReservation = first->reserve(1, 1, 2, &error); if (heldReservation) { heldReservation->retain(); }
			ok &= expect(heldReservation && !second->reserve(1, 1, 2, &error), "file, entry and batch caps reject overbooking by another owner");
		}
		waitForPackageCopyCleanup();
	}
#ifdef Q_OS_WIN
	ok &= expect(configure(store, narrow, &error), "restore the fixture policy");
	request.storeDirectory = store; reader.failAtEnd = 1; HANDLE held = INVALID_HANDLE_VALUE;
	request.control.progress = [&](const QString& path, qint64, qint64) {
		if (path != QStringLiteral("nested/b.txt") || held != INVALID_HANDLE_VALUE) { return; }
		const auto current = listPackageCopies(store);
		for (const auto& info : current.sessions) {
			const auto batches = QDir(info.path).entryList({QStringLiteral("package-copy-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
			if (batches.isEmpty()) { continue; }
			const auto file = QDir::toNativeSeparators(QDir(info.path).filePath(batches.first() + QStringLiteral("/nested/a.txt")));
			held = CreateFileW(reinterpret_cast<LPCWSTR>(file.utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		}
	};
	auto failed = copyPackageEntries(reader, request);
	const auto charged = inspectPackageCopyQuota(store);
	ok &= expect(held != INVALID_HANDLE_VALUE && !failed.succeeded() && charged.reserved.bytes == 8
		&& charged.reserved.cleanupFailedBatches == 1 && charged.reserved.pendingBatches == 0,
		"native cleanup failure keeps the full durable charge and its explicit failed state");
	if (held != INVALID_HANDLE_VALUE) { CloseHandle(held); }
	failed.session.reset(); waitForPackageCopyCleanup();
#endif
	return ok ? 0 : 1;
}
