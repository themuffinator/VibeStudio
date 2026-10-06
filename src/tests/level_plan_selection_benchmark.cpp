#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/map_viewport_test_helpers.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace {
QJsonObject paintStages(const MapViewport& view)
{
	const auto time = view.paintStatistics();
	return {{"totalMs",time.totalNs / 1e6},{"gridMs",time.gridNs / 1e6},{"geometryMs",time.geometryNs / 1e6},
		{"overlaysMs",time.overlayNs / 1e6},{"selectionMarkersMs",time.selectionMarkersNs / 1e6},
		{"resizeHandlesMs",time.resizeHandlesNs / 1e6},{"hudMs",time.hudNs / 1e6}};
}
}
int main(int argc, char** argv)
{
	// Semantic Qt calls and widget render targets; no input or desktop capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
	qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool valid = true;
	const int count = argc > 1 ? QString::fromLocal8Bit(argv[1]).toInt(&valid) : 1000;
	const bool sparse = app.arguments().contains(QStringLiteral("--sparse"));
	const bool staggered = app.arguments().contains(QStringLiteral("--staggered"));
	if (!valid || count < 1 || count > 10000) { return 2; }
	LevelMapDocument map; QString error;
	if (!tests::createGeometryFixture(count, &map, &error)) { std::cerr << error.toStdString(); return 1; }
	QVector<LevelMapSelectionRef> all;
	for (auto& brush : map.brushes) {
		if (staggered) {
			// Original rotated boxes at varied heights, so front/side views
			// also exercise distinct outlines rather than only stacked copies.
			const double x = (brush.mins.x + brush.maxs.x) * 0.5;
			const double y = (brush.mins.y + brush.maxs.y) * 0.5;
			const double z = (brush.id * 17 % 31) * 24.0;
			const double angle = (brush.id % 6) * 0.2617993877991494;
			const double c = std::cos(angle), s = std::sin(angle);
			for (auto& face : brush.faces) {
				for (auto* point : {&face.p0, &face.p1, &face.p2}) {
					const double dx = point->x - x, dy = point->y - y;
					point->x = x + c * dx - s * dy; point->y = y + s * dx + c * dy; point->z += z;
				}
			}
			const double extent = 16.0 * (std::abs(c) + std::abs(s));
			brush.mins = {x - extent, y - extent, z - 16, true};
			brush.maxs = {x + extent, y + extent, z + 16, true};
		}
		if (sparse) { brush.id = brush.id * 3 + 101; }
		all.append({LevelMapSelectionKind::QuakeBrush, brush.id});
	}
	MapViewport plan, front, side;
	front.setProjection(MapViewportProjection::FrontXZ); side.setProjection(MapViewportProjection::SideZY);
	QJsonObject report {{"brushes", count}, {"sparseIds", sparse}, {"staggered", staggered}, {"repetitions", 3},
		{"timingSemantics", "Paint timings measure GUI work; render timings include waiting for the complete current image."}};
	QElapsedTimer timer; timer.start(); plan.setDocument(map);
	report.insert("initialAdoptionMs", timer.nsecsElapsed() / 1e6);
	timer.restart(); front.synchronizeSceneFrom(plan, true); side.synchronizeSceneFrom(plan, true);
	report.insert("siblingAdoptionMs", timer.nsecsElapsed() / 1e6);
	QJsonArray panes;
	for (auto* view : {&plan, &front, &side}) {
		view->setPaintStatisticsEnabled(true);
		view->resize(1200, 800); view->zoomToFit();
		QImage image(view->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::black);
		timer.restart(); view->render(&image);
		QJsonObject pane {{"projection", int(view->projection())}, {"firstPaintMs", timer.nsecsElapsed() / 1e6}, {"backgroundRender",view->isRendering()}};
		pane.insert("firstPaintStages",paintStages(*view));
		if (!tests::settleMapViewport(*view,&image)) { return 1; }
		pane.insert("firstRenderMs",timer.nsecsElapsed() / 1e6);
		const auto wires = view->planWireStatistics();
		pane.insert("wires", QJsonObject{{"ready", wires.ready}, {"sourceEdges", wires.sourceEdges}, {"uniqueEdges", wires.uniqueEdges},
			{"duplicateEdges", wires.duplicateEdges}, {"batches", wires.batches}, {"retainedBytes", wires.retainedBytes}});
		QJsonArray unselected, unselectedStages;
		for (int repeat = 0; repeat < 3; ++repeat) {
			timer.restart(); view->render(&image); unselected.append(timer.nsecsElapsed() / 1e6);
			unselectedStages.append(paintStages(*view));
		}
		pane.insert("unselectedRenderMs", unselected);
		pane.insert("unselectedPaintStages",unselectedStages);
		timer.restart(); view->setSelectionSet(all); pane.insert("selectAllMs", timer.nsecsElapsed() / 1e6);
		QJsonArray samples;
		for (int repeat = 0; repeat < 3; ++repeat) {
			timer.restart(); const auto extent = view->selectionExtent(); const bool handles = view->hasResizeHandles();
			const auto selectionMs = timer.nsecsElapsed() / 1e6;
			timer.restart(); view->render(&image); const auto paintMs = timer.nsecsElapsed() / 1e6;
			if (!tests::settleMapViewport(*view,&image)) { return 1; }
			const auto renderMs = timer.nsecsElapsed() / 1e6;
			timer.restart(); view->zoomToSelection(); const auto frameMs = timer.nsecsElapsed() / 1e6;
			samples.append(QJsonObject{{"selectionMetricsMs", selectionMs}, {"paintMs",paintMs}, {"renderMs", renderMs}, {"frameMs", frameMs},
				{"extentWidth", extent.width()}, {"extentHeight", extent.height()}, {"resizeHandles", handles}});
		}
		pane.insert("samples", samples);
		const auto selectedWires = view->selectionWireStatistics();
		pane.insert("selectionWires", QJsonObject{{"ready", selectedWires.ready}, {"sourceEdges", selectedWires.sourceEdges},
			{"uniqueEdges", selectedWires.uniqueEdges}, {"retainedBytes", selectedWires.retainedBytes}});
		if (!tests::settleMapViewport(*view,&image)) { return 1; }
		// The preceding samples include first selection and Frame Selection's
		// navigation change. Measure a genuinely unchanged selected view too.
		QJsonArray warmSelected, selectedStages;
		for (int repeat = 0; repeat < 3; ++repeat) {
			timer.restart(); view->render(&image); warmSelected.append(timer.nsecsElapsed() / 1e6);
			selectedStages.append(paintStages(*view));
		}
		pane.insert("warmSelectedRenderMs", warmSelected);
		pane.insert("warmSelectedPaintStages",selectedStages);
		pane.insert("renderSha256", QString::fromLatin1(QCryptographicHash::hash(
			QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()), QCryptographicHash::Sha256).toHex()));
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			if (!image.save(QDir(captures).filePath(QStringLiteral("selection-%1-%2-%3.png").arg(count).arg(sparse).arg(int(view->projection()))))) { return 1; }
		}
		const auto original = view->navigationState();
		QJsonArray navigation, navigationPaint, eventGaps, heartbeatCounts, navigationStages;
		for (int repeat = 0; repeat < 3; ++repeat) {
			auto state = original; state.center += QPointF(13.25 * (repeat + 1), -7.75 * (repeat + 1));
			state.zoom *= 1.0 + 0.125 * (repeat + 1);
			view->applyLinkedNavigation(state);
			qint64 previous = 0, maximumGap = 0; int beats = 0; QTimer heartbeat;
			heartbeat.setTimerType(Qt::PreciseTimer);
			QObject::connect(&heartbeat,&QTimer::timeout,&app,[&] {
				const auto now = timer.nsecsElapsed(); maximumGap = std::max(maximumGap,now - previous); previous = now; ++beats;
			});
			timer.restart(); heartbeat.start(5); view->render(&image); navigationPaint.append(timer.nsecsElapsed() / 1e6);
			navigationStages.append(paintStages(*view));
			if (!tests::settleMapViewport(*view,&image)) { return 1; }
			heartbeat.stop(); navigation.append(timer.nsecsElapsed() / 1e6);
			maximumGap = std::max(maximumGap,timer.nsecsElapsed() - previous);
			eventGaps.append(maximumGap / 1e6); heartbeatCounts.append(beats);
		}
		pane.insert("navigationRenderMs", navigation);
		pane.insert("navigationPaintMs",navigationPaint); pane.insert("navigationMaxEventGapMs",eventGaps); pane.insert("navigationHeartbeatCounts",heartbeatCounts);
		pane.insert("navigationPaintStages",navigationStages);
		panes.append(pane);
	}
	report.insert("panes", panes); report.insert("ok", true);
	std::cout << QJsonDocument(report).toJson(QJsonDocument::Indented).constData();
	return 0;
}
