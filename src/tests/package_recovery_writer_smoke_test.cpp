#include "app/package_recovery_writer.h"
#include "core/package_draft.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { std::cerr << label << ": " << error.toStdString() << '\n'; }
	return value;
}
bool waitFor(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	QCoreApplication::processEvents(); return ready();
}
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

int main(int argc, char** argv)
{
	QCoreApplication application(argc, argv); QTemporaryDir temporary;
	if (!temporary.isValid()) { return 1; }
	const QString directory = QDir(temporary.path()).filePath(QStringLiteral("recoveries"));
	QString error; bool ok = true; const QString first = id(), second = id();
	PackageStagingModel plan; plan.createEmpty(PackageArchiveFormat::Pk3); plan.addBytes(QByteArray(1000003, 'a'), QStringLiteral("first.bin"));
	{
		PackageRecoveryWriter writer(directory);
		int completions = 0; quint64 finalRevision = 0;
		writer.finished = [&](const QString&, quint64 revision, const PackageRecoveryWriteResult& result) {
			++completions; finalRevision = revision; ok &= expect(result.succeeded(), "worker result", result.error + result.maintenanceError);
		};
		ok &= expect(writer.checkpoint(first, plan, QStringLiteral("First")) && !writer.checkpoint(first, plan, QStringLiteral("First")), "duplicate active revision is coalesced");
		for (int index = 0; index < 8; ++index) {
			plan.addBytes(QByteArray::number(index), QStringLiteral("file-%1.txt").arg(index));
			ok &= writer.checkpoint(first, plan, QStringLiteral("Latest"));
		}
		ok &= expect(waitFor([&] { return !writer.busy(); }) && finalRevision == plan.revision() && completions <= 2, "one pending snapshot coalesces rapid edits");
		PackageStagingModel checkpoint;
		ok &= expect(PackageDraft::load(packageRecoveryPath(directory, first), &checkpoint, &error)
			&& checkpoint.operations().size() == plan.operations().size(), "newest pending revision is durable", error);
		ok &= expect(!writer.checkpoint(first, plan, QStringLiteral("Latest")), "saved revision is not reread every timer tick");
		const auto info = inspectPackageRecovery(packageRecoveryPath(directory, first));
		ok &= expect(!discardPackageRecovery(directory, first, info.manifestSha256, false, &error), "idle editor continues to own the recovery lease");
		checkpoint.clear(); // Verification snapshots also keep recovery payloads alive.
		plan.addBytes(QByteArray(8000003, 'b'), QStringLiteral("large.bin"));
		writer.checkpoint(first, plan, QStringLiteral("Retiring"));
		plan.addBytes("pending", QStringLiteral("pending.txt")); writer.checkpoint(first, plan, QStringLiteral("Pending"));
		writer.retire(first);
		ok &= expect(!writer.checkpoint(first, plan, QStringLiteral("Resurrection")), "retired ID rejects new work");
		PackageStagingModel next; next.createEmpty(PackageArchiveFormat::Wad, QStringLiteral("WAD3"));
		ok &= writer.checkpoint(second, next, QStringLiteral("New document"));
		ok &= expect(waitFor([&] { return !writer.busy(); }) && !QFileInfo::exists(packageRecoveryPath(directory, first))
			&& inspectPackageRecovery(packageRecoveryPath(directory, second)).readable(), "retirement cancels old work before checkpointing the next document");
		writer.retire(second);
		ok &= expect(waitFor([&] { return !writer.busy(); }) && !QFileInfo::exists(packageRecoveryPath(directory, second)), "retirement finishes without leaving a copy");
	}
	const QString unexpected = id();
	{
		PackageRecoveryWriter writer(directory);
		writer.checkpoint(unexpected, plan, QStringLiteral("First"));
		plan.addBytes("newest", QStringLiteral("shutdown.txt"));
		writer.checkpoint(unexpected, plan, QStringLiteral("Newest"));
		// No explicit close approval: the destructor joins the worker and flushes
		// the accepted pending snapshot instead of dropping recent work.
	}
	PackageStagingModel newest;
	ok &= expect(PackageDraft::load(packageRecoveryPath(directory, unexpected), &newest, &error)
		&& newest.revision() == plan.revision(), "unexpected destruction preserves accepted pending work", error);
	const auto copy = inspectPackageRecovery(packageRecoveryPath(directory, unexpected));
	ok &= expect(!discardPackageRecovery(directory, unexpected, copy.manifestSha256, false, &error), "loaded verification snapshot still blocks discard after writer destruction", error);
	newest.clear();
	ok &= expect(discardPackageRecovery(directory, unexpected, copy.manifestSha256, false, &error), "destruction releases the session lease", error);
	{
		PackageRecoveryWriter writer(directory); const QString limited = id(); PackageRecoveryWriteResult result;
		writer.finished = [&](const QString&, quint64, const PackageRecoveryWriteResult& output) { result = output; };
		ok &= writer.checkpoint(limited, plan, QStringLiteral("Limited"), {1024, 1});
		ok &= expect(waitFor([&] { return !writer.busy(); }) && !result.succeeded(), "worker forwards quota and reports a failed checkpoint", result.error);
		ok &= expect(writer.checkpoint(limited, plan, QStringLiteral("Retry"), {32 * 1024 * 1024, 1})
			&& waitFor([&] { return !writer.busy(); }) && result.succeeded(), "same revision retries after raising a failed quota", result.error);
		writer.retire(limited); ok &= waitFor([&] { return !writer.busy(); });
	}
	std::cout << (ok ? "Package recovery writer smoke passed\n" : "Package recovery writer smoke failed\n");
	return ok ? 0 : 1;
}
