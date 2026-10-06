#include "app/map_plan_renderer.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/map_viewport_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QPainter>
#include <iostream>

using namespace vibestudio;
int main(int argc, char** argv)
{
	// Original geometry, direct semantic calls and widget render targets only.
	qputenv("QT_QPA_PLATFORM","offscreen"); qputenv("QT_SCALE_FACTOR","1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	bool ok = true; QString error;
	for (const int count : {1,625}) {
		LevelMapDocument document;
		if (!tests::createGeometryFixture(count,&document,&error)) { return 1; }
		MapViewport view; view.resize(901,663); view.setHighContrast(true); view.setShowGrid(false); view.setShowLabels(false);
		view.setShowThings(false); view.setShowVertices(false); view.setDocument(document);
		MapPlanRenderRequest request; request.document = document; request.brushes = buildLevelMapBrushGeometry(document);
		request.index.rebuild(document); request.index.rebuildGeometry(request.brushes,{}); request.worldspawnId = document.entities.first().id;
		request.view.highContrast = true; request.view.world = qRgb(255,255,255); request.view.entity = qRgb(255,255,0);
		request.view.invalid = qRgb(255,96,0); request.view.selection = qRgb(255,0,255); request.view.patch = qRgb(0,255,0);
		for (const auto projection : {MapViewportProjection::TopXY,MapViewportProjection::FrontXZ,MapViewportProjection::SideZY}) {
			view.setProjection(projection); view.zoomToFit(); request.projection = projection;
			const auto state = view.navigationState(); request.view.viewport = view.size(); request.view.center = state.center; request.view.zoom = state.zoom;
			const auto wires = buildMapPlanWires(document,request.brushes,request.index,projection,request.worldspawnId);
			for (const qreal ratio : {1.25,1.5,1.75}) {
				request.view.pixelRatio = ratio; MapPlanWireFrame frame;
				if (!renderMapPlanFrame(request,false,wires,&frame)) { return 1; }
				QImage actual(frame.image.size(),QImage::Format_ARGB32_Premultiplied); actual.setDevicePixelRatio(ratio);
				if (!tests::settleMapViewport(view,&actual)) { return 1; }
				QImage expected(actual.size(),actual.format()); expected.setDevicePixelRatio(ratio); expected.fill(Qt::black);
				// This independent presentation places physical pixels directly. It
				// must not squeeze a ceil-sized image into the logical widget extent.
				{ QPainter painter(&expected); painter.drawImage(QPointF(0,0),frame.image); }
				// Exclude the HUD and widget border; there are no editing overlays.
				const QRect geometry(8,int(std::ceil(48 * ratio)),actual.width() - 16,actual.height() - int(std::ceil(56 * ratio)));
				if (actual.copy(geometry) != expected.copy(geometry)) {
					std::cerr << "Physical geometry shifted: brushes=" << count << " projection=" << int(projection) << " ratio=" << ratio << '\n';
					ok = false;
					const QDir output(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR"));
					if (!qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_DIR")) {
						QDir().mkpath(output.path()); actual.save(output.filePath("fractional-actual.png")); expected.save(output.filePath("fractional-reference.png"));
					}
				}
			}
		}
	}
	return ok ? 0 : 1;
}
