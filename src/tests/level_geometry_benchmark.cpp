#include "app/level_preview_worker.h"
#include "app/map_viewport.h"
#include "tests/level_geometry_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

using namespace vibestudio;
int main(int argc, char** argv)
{
	// Offscreen semantic operations and render targets only; no injected input.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); QTemporaryDir temp;
	bool validCount = true;
	const int count = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt(&validCount) : 1000;
	if (!temp.isValid() || !validCount || count < 1 || count > 10000) { std::cerr << "Choose 1-10000 brushes.\n"; return 2; }
	QString error; LevelMapDocument generated, map;
	if (!tests::createGeometryFixture(count, &generated, &error)) { std::cerr << error.toStdString(); return 1; }
	const auto bytes = serializeLevelMap(generated).bytes;
	QJsonObject report {{"brushes", count}, {"mapBytes", bytes.size()}};
	auto measure = [&](const char* key, const auto& action) {
		QElapsedTimer timer; timer.start(); action(); report.insert(QString::fromLatin1(key), timer.nsecsElapsed() / 1.0e6);
	};
	bool ok = true;
	LevelMapLoadRequest load; load.path = temp.filePath(QStringLiteral("large.map")); load.engineHint = QStringLiteral("idtech3");
	measure("parseMs", [&] { ok &= loadLevelMapBytes(load, bytes, &map, &error); });
	if (!ok || map.brushes.size() != count) { std::cerr << "Fixture parse failed: " << error.toStdString(); return 1; }
	MapViewport plan, front, side;
	plan.resize(1200, 800); front.resize(1200, 800); side.resize(1200, 800);
	front.setProjection(MapViewportProjection::FrontXZ); side.setProjection(MapViewportProjection::SideZY);
	measure("planInitialMs", [&] { plan.setDocument(map); });
	measure("siblingInitialMs", [&] { front.synchronizeSceneFrom(plan, true); side.synchronizeSceneFrom(plan, true); });
	QImage render(plan.size(), QImage::Format_ARGB32_Premultiplied); render.fill(Qt::black);
	measure("renderUnselectedMs", [&] { plan.render(&render); });
	const auto assetPath = temp.filePath(QStringLiteral("assets/textures/studio")); QDir().mkpath(assetPath);
	QImage texture(64, 64, QImage::Format_RGB32); texture.fill(QColor(70, 140, 190));
	ok &= texture.save(QDir(assetPath).filePath(QStringLiteral("cache.png")));
	auto archive = std::make_shared<PackageArchive>(); ok &= archive->load(temp.filePath(QStringLiteral("assets")), &error);
	LevelPreviewWorker worker;
	const auto preview = [&](const char* key) {
		QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); bool completed = false;
		QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
		worker.completed = [&](const LevelPreviewResult& result) {
			completed = true; ok &= result.error.isEmpty() && !result.preview.cancelled && !result.preview.truncated
				&& result.preview.triangles == count * 12 && result.assets.readyCount() == 1;
			report.insert(QString::fromLatin1(key) + QStringLiteral("Solved"), result.geometryCache.solved);
			report.insert(QString::fromLatin1(key) + QStringLiteral("Reused"), result.geometryCache.reused);
			report.insert(QStringLiteral("cameraCachePayloadBytes"), result.geometryCache.retainedBytes);
			loop.quit();
		};
		LevelPreviewRequest request; request.document = map; request.archive = archive; request.sourceKey = QStringLiteral("benchmark");
		request.assetKey = QStringLiteral("fixture"); request.loadSerial = 1;
		measure(key, [&] { timeout.start(120000); worker.request(std::move(request)); loop.exec(); });
		ok &= completed; worker.completed = {};
	};
	preview("cameraInitialMs");
	ok &= setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, count / 2}}, &error);
	measure("moveOneMs", [&] { ok &= moveLevelMapSelection(&map, 8, 0, 0, &error); });
	measure("planEditMs", [&] { plan.updateDocument(map); });
	report.insert(QStringLiteral("planEditSolved"), plan.geometryCacheStatistics().solved);
	report.insert(QStringLiteral("planEditReused"), plan.geometryCacheStatistics().reused);
	report.insert(QStringLiteral("planCachePayloadBytes"), plan.geometryCacheStatistics().retainedBytes);
	measure("siblingEditMs", [&] { front.synchronizeSceneFrom(plan); side.synchronizeSceneFrom(plan); });
	preview("cameraEditMs");
	measure("undoOneMs", [&] { ok &= undoLevelMapEdit(&map, &error); });
	measure("planUndoMs", [&] { plan.updateDocument(map); });
	QVector<LevelMapSelectionRef> all;
	for (const auto& brush : map.brushes) { all.append({LevelMapSelectionKind::QuakeBrush, brush.id}); }
	measure("selectAllMs", [&] { ok &= setLevelMapSelection(&map, all, &error); plan.setSelectionSet(all); });
	measure("renderSelectedMs", [&] { plan.render(&render); });
	report.insert(QStringLiteral("ok"), ok); report.insert(QStringLiteral("error"), error);
	std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
	return ok ? 0 : 1;
}
