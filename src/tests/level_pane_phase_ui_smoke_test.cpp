#include "tests/map_viewport_test_helpers.h"
#include "tests/level_geometry_test_helpers.h"
#include "app/model_rasterizer.h"
#include "core/level_patch.h"
#include <QApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
int difference(const QImage& a, const QImage& b)
{
	if (a.size() != b.size()) { return 256; }
	int maximum = 0;
	for (int y = 0; y < a.height(); ++y) { for (int x = 0; x < a.width(); ++x) {
		const auto p = a.pixel(x,y), q = b.pixel(x,y);
		maximum = std::max({maximum,std::abs(qRed(p)-qRed(q)),std::abs(qGreen(p)-qGreen(q)),std::abs(qBlue(p)-qBlue(q))});
	} }
	return maximum;
}
QImage imageFor(const QWidget& widget, double ratio)
{
	QImage image(QSize(int(std::ceil(widget.width() * ratio)),int(std::ceil(widget.height() * ratio))),QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio); image.fill(Qt::transparent); return image;
}
QImage overlayReference(const QWidget& host, const MapViewport& view, double ratio, const LevelMapDocument& document)
{
	auto image = imageFor(host,ratio); image.fill(Qt::black);
	QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
	painter.translate(view.mapTo(&host,QPoint()));
	const auto nav = view.navigationState();
	MapGridView grid; grid.viewport = view.size(); grid.center = nav.center; grid.zoom = nav.zoom; grid.units = view.gridSize();
	const auto lines = mapGridLines(grid); const QColor colors[] = {QColor(70,70,70),QColor(120,120,120),Qt::white};
	for (int i = 0; i < 3; ++i) { painter.setPen(QPen(colors[i],i == 2 ? 2.0 : 1.0)); painter.drawLines(lines[i]); }
	for (int i = 0; i < document.entities.size(); ++i) {
		const auto& point = document.entities.at(i).origin;
		const QPointF center(view.width() * 0.5 + (point.x-nav.center.x())*nav.zoom,view.height() * 0.5 - (point.y-nav.center.y())*nav.zoom);
		painter.setPen(QPen(Qt::cyan,1.5));
		painter.drawPolygon(QPolygonF{center+QPointF(0,-5),center+QPointF(5,0),center+QPointF(0,5),center+QPointF(-5,0)});
		if (i < document.entities.size()-1) { painter.setPen(QPen(Qt::magenta,2)); painter.drawEllipse(center,8,8); }
	}
	return image;
}
int overlayDifference(const QImage& actual, const QImage& reference, const QWidget& host, const MapViewport& view, double ratio)
{
	const auto offset = view.mapTo(&host,QPoint());
	const auto physical = [&](const QRectF& area) {
		return QRect(int(std::ceil((offset.x()+area.x())*ratio)),int(std::ceil((offset.y()+area.y())*ratio)),int(area.width()*ratio),int(area.height()*ratio));
	};
	// Native grid outside the scene and an interior member away from HUD,
	// primary labels and resize handles. These references do not use an image cache.
	const auto grid = physical({35,80,100,100});
	const auto member = physical({view.width() * 0.5-51,view.height() * 0.5+24,22,22});
	return std::max(difference(actual.copy(grid),reference.copy(grid)),difference(actual.copy(member),reference.copy(member)));
}
bool settle(QWidget& host, MapViewport& view, QImage* image)
{
	QElapsedTimer elapsed; elapsed.start();
	while (elapsed.elapsed() < 30000) {
		image->fill(Qt::transparent); host.render(image);
		if (!view.isRendering()) { return true; }
		QEventLoop events; QTimer timeout; timeout.setSingleShot(true);
		QObject::connect(&view,&MapViewport::renderCompleted,&events,&QEventLoop::quit);
		QObject::connect(&timeout,&QTimer::timeout,&events,&QEventLoop::quit); timeout.start(30000 - int(elapsed.elapsed()));
		events.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return false;
}
bool rasterPaths(QJsonArray* samples)
{
	LevelMapDocument document; QString error;
	if (!tests::createGeometryFixture(4,&document,&error)) { return false; }
	document.brushes.last().faces.clear(); document.brushes.last().boundsSolved = true;
	LevelMapPatch patch; LevelPatchCreateRequest create;
	create.center = {80,70,40,true}; create.size = {80,70,60,true};
	if (!createLevelPatch(create,&patch,&error)) { return false; }
	patch.id = 907; patch.entityId = document.entities.first().id;
	for (int row = 0; row < 3; ++row) { patch.controlPoints[row * 3 + 1].z += 60; }
	refreshLevelPatchBounds(&patch); document.patches.append(patch);
	MapPlanRenderRequest request; request.document = document; request.worldspawnId = document.entities.first().id;
	request.brushes = buildLevelMapBrushGeometry(document); request.index.rebuild(document); request.index.rebuildGeometry(request.brushes,{});
	request.selection = {{LevelMapSelectionKind::Entity,request.worldspawnId}};
	request.view.viewport = {381,283}; request.view.center = {40,40}; request.view.zoom = 1.4;
	request.view.world = qRgb(100,200,230); request.view.entity = qRgb(80,240,150); request.view.invalid = qRgb(240,90,80);
	request.view.selection = qRgb(255,205,70); request.view.patch = qRgb(180,140,240);
	QWidget host; host.resize(431,333); const QPointF offset(17,13); bool ok = true;
	for (const auto projection : {MapViewportProjection::TopXY,MapViewportProjection::FrontXZ,MapViewportProjection::SideZY}) {
		request.projection = projection;
		const auto selectedWires = buildMapPlanSelectionWires(document,request.brushes,request.index,projection,request.selection);
		for (const double ratio : {1.0,1.25,1.5,1.75,2.0}) {
			request.view.pixelRatio = ratio; request.view.highContrast = ratio >= 1.5;
			request.view.pixelPhase = {offset.x()*ratio-std::floor(offset.x()*ratio),offset.y()*ratio-std::floor(offset.y()*ratio)};
			for (int path = 0; path < 4; ++path) {
				auto source = request; MapPlanWires wires;
				const bool selected = path == 1 || path == 2;
				if (path == 2) { wires = selectedWires; }
				// Isolate native diagnostic/patch painting from CPU brush wires.
				if (path == 3) {
					source.brushes = {request.brushes.last()};
					wires = buildMapPlanWires(document,source.brushes,source.index,projection,source.worldspawnId);
				}
				MapPlanWireFrame frame; ok &= renderMapPlanFrame(source,selected,wires,&frame);
				auto actual = imageFor(host,ratio), reference = imageFor(host,ratio); actual.fill(Qt::black); reference.fill(Qt::black);
				{ QPainter painter(&actual); painter.drawImage(offset-source.view.pixelPhase/ratio,frame.image); }
				if (path != 2) {
					QPainter painter(&reference); painter.setRenderHint(QPainter::Antialiasing); painter.translate(offset);
					ok &= paintMapPlanFallback(painter,source,selected);
				} else {
					QVector<ModelWireSegment> lines;
					const auto at = [&](const QPointF& point) {
						return offset + QPointF(source.view.viewport.width()*0.5+(point.x()-source.view.center.x())*source.view.zoom,
							source.view.viewport.height()*0.5-(point.y()-source.view.center.y())*source.view.zoom);
					};
					for (const auto& batch : wires.batches) { for (const auto& line : batch.lines) { lines.append({at(line.p1()),at(line.p2()),true}); } }
					ModelWireStyle style; style.pixelRatio = ratio; style.selection = source.view.selection;
					style.selectionWidth = source.view.highContrast ? 3.6 : 2.8;
					ok &= paintModelWireframe(&reference,lines,style);
				}
				const QRect region(int(std::ceil((offset.x()+8)*ratio)),int(std::ceil((offset.y()+8)*ratio)),
					int((source.view.viewport.width()-16)*ratio),int((source.view.viewport.height()-16)*ratio));
				const int delta = difference(actual.copy(region),reference.copy(region)); ok &= delta <= (path == 2 ? 1 : 4);
				samples->append(QJsonObject{{"projection",int(projection)},{"ratio",ratio},{"path",path},{"maxChannelDifference",delta}});
				if ((delta > 4 || (projection == MapViewportProjection::FrontXZ && ratio == 1.5 && path != 1))
					&& !qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_DIR")) {
					const QDir output(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR"));
					actual.save(output.filePath(QStringLiteral("path-actual-%1-%2-%3.png").arg(path).arg(int(projection)).arg(ratio)));
					reference.save(output.filePath(QStringLiteral("path-reference-%1-%2-%3.png").arg(path).arg(int(projection)).arg(ratio)));
				}
			}
		}
	}
	return ok;
}
}
int main(int argc, char** argv)
{
	// Exercise child-widget paint offsets through Qt render targets, with no
	// input injection, game launch, native window activation or OS capture.
	qputenv("QT_QPA_PLATFORM","offscreen"); qputenv("QT_SCALE_FACTOR","1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	QWidget host; host.resize(851,653);
	auto* view = new MapViewport(&host); view->setGeometry(17,13,801,603); view->setHighContrast(true); view->setReducedMotion(true);
	view->setShowLabels(false); view->setGridSize(8);
	LevelMapDocument document; document.format = LevelMapFormat::QuakeMap;
	QVector<LevelMapSelectionRef> selected;
	for (int i = 0; i < 81; ++i) {
		LevelMapEntity entity; entity.id = 101 + i * 7; entity.className = QStringLiteral("info_null");
		entity.origin = {-160.0 + (i % 9) * 40,-140.0 + (i / 9) * 35,0,true};
		document.entities.append(entity); selected.append({LevelMapSelectionKind::Entity,entity.id});
	}
	view->setDocument(document); view->setSelectionSet(selected); view->restoreNavigationState({0,{0,0},1});
	bool ok = true; QJsonArray samples;
	for (const double ratio : {1.0,1.25,1.5,1.75,2.0}) {
		auto image = imageFor(host,ratio); QElapsedTimer elapsed; elapsed.start(); host.render(&image);
		const double paintMs = elapsed.nsecsElapsed() / 1e6; const bool queued = view->isOverlayRendering();
		const bool complete = settle(host,*view,&image);
		const int delta = overlayDifference(image,overlayReference(host,*view,ratio,document),host,*view,ratio);
		ok &= queued && complete && delta <= 4;
		samples.append(QJsonObject{{"ratio",ratio},{"overlayQueued",queued},{"complete",complete},{"firstPaintMs",paintMs},{"maxOverlayChannelDifference",delta}});
		const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!output.isEmpty()) { QDir().mkpath(output); ok &= image.save(QDir(output).filePath(QStringLiteral("child-pane-%1.png").arg(ratio))); }
	}
	QJsonArray moves;
	for (const auto position : {QPoint(17,13),QPoint(18,14),QPoint(20,16),QPoint(-7,9)}) {
		view->move(position); auto image = imageFor(host,1.5); host.render(&image);
		const bool pending = view->isOverlayRendering(); ok &= settle(host,*view,&image);
		const int delta = overlayDifference(image,overlayReference(host,*view,1.5,document),host,*view,1.5);
		ok &= delta <= 4;
		if (position == QPoint(18,14)) { ok &= pending; }
		if (position == QPoint(20,16)) { ok &= !pending; }
		moves.append(QJsonObject{{"x",position.x()},{"y",position.y()},{"overlayQueued",pending},{"maxOverlayChannelDifference",delta}});
	}
	view->move(17,13);
	QJsonArray geometrySamples;
	for (const int count : {1,625}) {
		LevelMapDocument brushes; QString error;
		if (!tests::createGeometryFixture(count,&brushes,&error)) { return 1; }
		view->setDocument(brushes); view->setShowGrid(false);
		const auto geometry = buildLevelMapBrushGeometry(brushes);
		MapViewportSceneIndex index; index.rebuild(brushes); index.rebuildGeometry(geometry,{});
		for (const auto projection : {MapViewportProjection::TopXY,MapViewportProjection::FrontXZ,MapViewportProjection::SideZY}) {
			view->setProjection(projection); view->zoomToFit(); const auto nav = view->navigationState();
			const auto wires = buildMapPlanWires(brushes,geometry,index,projection,brushes.entities.first().id);
			for (const double ratio : {1.0,1.25,1.5,1.75,2.0}) {
				auto actual = imageFor(host,ratio), reference = imageFor(host,ratio); reference.fill(Qt::black);
				ok &= settle(host,*view,&actual);
				// Rasterize directly into the physical parent target. No cached
				// image placement or production device-phase helper participates.
				QVector<ModelWireSegment> lines;
				const auto at = [&](const QPointF& point) {
					return QPointF(view->x() + view->width() * 0.5 + (point.x()-nav.center.x())*nav.zoom,
						view->y() + view->height() * 0.5 - (point.y()-nav.center.y())*nav.zoom);
				};
				for (const auto& batch : wires.batches) { for (const auto& line : batch.lines) { lines.append({at(line.p1()),at(line.p2()),false}); } }
				ModelWireStyle style; style.pixelRatio = ratio; style.width = 1.6; style.wire = qRgb(255,255,255);
				ok &= paintModelWireframe(&reference,lines,style);
				const QRect region(int(std::ceil((view->x()+8)*ratio)),int(std::ceil((view->y()+60)*ratio)),
					int((view->width()-16)*ratio),int((view->height()-68)*ratio));
				const int delta = difference(actual.copy(region),reference.copy(region)); ok &= delta <= 1;
				geometrySamples.append(QJsonObject{{"brushes",count},{"projection",int(projection)},{"ratio",ratio},{"maxChannelDifference",delta}});
				if (delta > 1 && !qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_DIR")) {
					const QDir output(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR"));
					actual.save(output.filePath(QStringLiteral("geometry-actual-%1-%2-%3.png").arg(count).arg(int(projection)).arg(ratio)));
					reference.save(output.filePath(QStringLiteral("geometry-reference-%1-%2-%3.png").arg(count).arg(int(projection)).arg(ratio)));
				}
			}
		}
	}
	QJsonArray paths; ok &= rasterPaths(&paths);
	std::cout << QJsonDocument(QJsonObject{{"ok",ok},{"samples",samples},{"paneMoves",moves},{"geometry",geometrySamples},{"rasterPaths",paths}}).toJson(QJsonDocument::Compact).constData() << '\n';
	if (!ok) { std::cerr << "Child-pane drawing must match direct physical-target pixels and retain background overlay rendering.\n"; }
	return ok ? 0 : 1;
}
