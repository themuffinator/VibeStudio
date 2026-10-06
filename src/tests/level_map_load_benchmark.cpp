#include "app/level_load_dialog.h"
#include "app/map_viewport.h"
#include "tests/level_geometry_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <array>
#include <atomic>
#include <iostream>

using namespace vibestudio;
int main(int argc, char** argv)
{
	// A developer measurement, not a CI timing threshold. No input injection.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp;
	bool valid = true;
	const int count = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt(&valid) : 1000;
	if (!temp.isValid() || !valid || count < 1 || count > 10000) { return 2; }
	LevelMapDocument map; QString error;
	if (!tests::createGeometryFixture(count, &map, &error)) { return 1; }
	const auto bytes = serializeLevelMap(map).bytes;
	const auto path = temp.filePath(QStringLiteral("benchmark.map"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) { return 1; }
	file.close(); map = {};
	QJsonObject report {{QStringLiteral("brushes"), count}, {QStringLiteral("sourceBytes"), bytes.size()}};
	std::array<qint64, 8> phaseNanos {};
	QElapsedTimer workClock;
	int previousPhase = -1; qint64 previousStamp = 0;
	LevelMapLoadRequest request {path, {}, {}};
	request.progress = [&](LevelMapLoadPhase phase, qint64, qint64) {
		if (!workClock.isValid()) { workClock.start(); }
		const auto now = workClock.nsecsElapsed();
		if (previousPhase >= 0) { phaseNanos[previousPhase] += now - previousStamp; }
		previousPhase = int(phase); previousStamp = now;
	};
	QElapsedTimer wallClock; wallClock.start();
	qint64 previousPulse = 0, maxGap = 0; int pulses = 0;
	QTimer pulse; pulse.setInterval(16);
	QObject::connect(&pulse, &QTimer::timeout, &app, [&] {
		const auto now = wallClock.elapsed(); maxGap = std::max(maxGap, now - previousPulse); previousPulse = now; ++pulses;
	});
	pulse.start(); auto result = LevelMapLoadDialog::openMap(nullptr, request); pulse.stop();
	report.insert(QStringLiteral("openMs"), wallClock.nsecsElapsed() / 1e6);
	report.insert(QStringLiteral("guiHeartbeats"), pulses); report.insert(QStringLiteral("largestHeartbeatGapMs"), maxGap);
	const QStringList names {QStringLiteral("reading"), QStringLiteral("indexing"), QStringLiteral("tokenizing"), QStringLiteral("parsing"),
		QStringLiteral("solving"), QStringLiteral("validating"), QStringLiteral("hashing"), QStringLiteral("complete")};
	QJsonObject phases;
	for (int i = 0; i < 8; ++i) { phases.insert(names[i], phaseNanos[i] / 1e6); }
	report.insert(QStringLiteral("phaseMs"), phases);
	bool ok = result.succeeded && result.geometry && result.document.brushes.size() == count;
	if (ok) {
		MapViewport view; wallClock.restart(); view.setDocument(result.document, result.geometry.get());
		report.insert(QStringLiteral("viewportAdoptionMs"), wallClock.nsecsElapsed() / 1e6);
		report.insert(QStringLiteral("viewportSolved"), view.geometryCacheStatistics().solved);
		report.insert(QStringLiteral("viewportReused"), view.geometryCacheStatistics().reused);
		report.insert(QStringLiteral("cachePayloadBytes"), view.geometryCacheStatistics().retainedBytes);
	}
	std::atomic_bool solving{false};
	request.progress = [&](LevelMapLoadPhase phase, qint64, qint64) { if (phase == LevelMapLoadPhase::Solving) { solving = true; } };
	QElapsedTimer cancelClock; QTimer cancellation; cancellation.setInterval(1); bool requested = false;
	QObject::connect(&cancellation, &QTimer::timeout, &app, [&] {
		if (!solving || requested) { return; }
		if (auto* dialog = qobject_cast<LevelMapLoadDialog*>(QApplication::activeModalWidget())) {
			requested = true; cancelClock.start(); dialog->reject();
		}
	});
	cancellation.start(); const auto cancelled = LevelMapLoadDialog::openMap(nullptr, request); cancellation.stop();
	ok &= requested && cancelled.cancelled && !cancelled.succeeded;
	if (requested) { report.insert(QStringLiteral("cancelAcknowledgementMs"), cancelClock.nsecsElapsed() / 1e6); }
	report.insert(QStringLiteral("ok"), ok); report.insert(QStringLiteral("error"), result.error);
	std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
	return ok ? 0 : 1;
}
