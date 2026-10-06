#include "core/package_staging.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace vibestudio;

// Opt-in diagnostic workload. The harness samples process memory externally;
// this executable records atomic batch admission and cancellation phases.
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const int count = argc > 1 ? QByteArray(argv[1]).toInt() : 8000;
	const int depth = argc > 2 ? QByteArray(argv[2]).toInt() : 32;
	const int cancelAfterMs = argc > 3 ? QByteArray(argv[3]).toInt() : 20;
	if (count < 1 || count > 100000 || depth < 1 || depth > 128 || cancelAfterMs < 0 || cancelAfterMs > 60000) { return 2; }
	PackageStagingModel model; QString error;
	if (!model.createEmpty(PackageArchiveFormat::Zip, {}, &error) || !model.beginOperationGroup("Generated plan probe", &error)) { return 1; }
	QElapsedTimer setup; setup.start();
	int accepted = 0;
	for (int index = 0; index < count; ++index) {
		QString path = QStringLiteral("branch-%1").arg(index, 6, 10, QLatin1Char('0'));
		for (int component = 1; component < depth; ++component) { path += "/a"; }
		if (!model.addBytes({}, path, &error)) { break; }
		++accepted;
	}
	const qint64 setupMs = setup.elapsed();
	const auto revision = model.revision();
	std::atomic_bool stop{false}; std::atomic_bool finished{false}; std::atomic_bool started{false};
	std::atomic<qint64> cancelledAt{-1};
	QElapsedTimer preparation; preparation.start();
	std::thread cancellation;
	if (cancelAfterMs > 0) {
		cancellation = std::thread([&] {
			while (!started.load() && !finished.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
			if (finished.load()) { return; }
			std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfterMs));
			cancelledAt = preparation.elapsed(); stop = true;
		});
	}
	QJsonArray phases; qsizetype callbacks = 0; qsizetype cancellationChecks = 0; QString lastPhase;
	PackageReadControl control;
	control.isCancelled = [&] { ++cancellationChecks; return stop.load(); };
	control.progress = [&](const QString& phase, qint64 completed, qint64 total) {
		++callbacks; started = true;
		if (phase != lastPhase) {
			phases.append(QJsonObject{{"phase", phase}, {"atMs", preparation.elapsed()}, {"completed", completed}, {"total", total}});
			lastPhase = phase;
		}
	};
	const bool committed = model.endOperationGroup(true, &error, control);
	const qint64 preparedMs = preparation.elapsed(); finished = true;
	if (cancellation.joinable()) { cancellation.join(); }
	const PackageStagingArchive view(model, PackageStagingReadMode::InspectPlan);
	QJsonObject report{{"requestedEntries", count}, {"acceptedEntries", accepted}, {"depth", depth},
		{"setupMs", setupMs}, {"preparationMs", preparedMs}, {"cancelAfterMs", cancelAfterMs},
		{"cancelledAtMs", cancelledAt.load()}, {"cancellationChecks", cancellationChecks}, {"progressCallbacks", callbacks},
		{"committed", committed}, {"committedOperations", model.operations().size()}, {"viewOpen", view.isOpen()}, {"viewEntries", view.entries().size()}, {"viewError", view.errorString()},
		{"stagingError", error}, {"revisionUnchanged", model.revision() == revision}, {"canUndo", model.canUndo()}, {"phases", phases}};
	if (cancelledAt >= 0) { report.insert("cancellationDelayMs", qMax<qint64>(0, preparedMs - cancelledAt.load())); }
	std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
	return accepted == count && (committed ? model.revision() != revision : model.revision() == revision && model.operations().isEmpty()) ? 0 : 1;
}
