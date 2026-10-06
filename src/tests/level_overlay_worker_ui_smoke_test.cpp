#include "app/map_viewport_overlay_worker.h"
#include "tests/map_viewport_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QThread>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool waitFor(MapViewportOverlayWorker& worker)
{
	QEventLoop loop; QTimer pulse, deadline; deadline.setSingleShot(true);
	QObject::connect(&pulse,&QTimer::timeout,&loop,[&] { if (!worker.busy()) { loop.quit(); } });
	QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
	pulse.start(1); deadline.start(30000);
	if (worker.busy()) { loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return !worker.busy();
}
QImage imageFor(QSize size, double ratio)
{
	QImage image(QSize(int(std::ceil(size.width() * ratio)),int(std::ceil(size.height() * ratio))),QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio); image.fill(Qt::transparent); return image;
}
MapViewportOverlayRequest fixture()
{
	MapViewportOverlayRequest request; request.document.format = LevelMapFormat::QuakeMap;
	for (int i = 0; i < 81; ++i) {
		LevelMapEntity entity; entity.id = 101 + i * 7; entity.className = QStringLiteral("info_null");
		entity.origin = {-160.0 + (i % 9) * 40,-140.0 + (i / 9) * 35,-120.0 + (i / 9) * 30 + (i % 9) * 3,true};
		request.document.entities.append(entity); request.selection.append({LevelMapSelectionKind::Entity,entity.id});
	}
	request.index.rebuild(request.document); request.primary = request.selection.last();
	request.key.sceneRevision = 1; request.key.selectionRevision = 1;
	request.key.showGrid = true; request.key.showMembers = true; request.key.selectionColor = qRgb(255,0,255); request.key.markerWidth = 2;
	request.key.view.viewport = {801,603}; request.key.view.center = {0,0}; request.key.view.zoom = 1;
	request.key.view.minor = qRgb(70,70,70); request.key.view.major = qRgb(120,120,120); request.key.view.axis = qRgb(255,255,255);
	return request;
}
// Reference positions are derived directly from the known entity fixture, with
// no scene index, production projection helper, marker cache or image renderer.
QImage expectedMembers(const MapViewportOverlayRequest& request)
{
	const auto& view = request.key.view;
	auto image = imageFor(view.viewport,view.pixelRatio);
	QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(QColor::fromRgba(request.key.selectionColor),request.key.markerWidth));
	for (int i = 0; i < 80; ++i) {
		const auto& point = request.document.entities.at(i).origin;
		const double x = request.key.projection == MapViewportProjection::SideZY ? point.y : point.x;
		const double y = request.key.projection == MapViewportProjection::TopXY ? point.y : point.z;
		painter.drawEllipse(QPointF(view.viewport.width() * 0.5 + (x - view.center.x()) * view.zoom,
			view.viewport.height() * 0.5 - (y - view.center.y()) * view.zoom),8,8);
	}
	return image;
}
QImage memberReference(const MapViewportOverlayRequest& request)
{
	auto image = imageFor(request.key.view.viewport,request.key.view.pixelRatio); image.fill(Qt::black);
	QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(Qt::NoBrush);
	painter.setPen(QPen(Qt::cyan,1.5));
	// Quake point-entity diamonds are base geometry; Show Things controls Doom
	// things. Include the diamond beneath the independently drawn member ring.
	for (const auto& entity : request.document.entities) {
		const QPointF center(request.key.view.viewport.width() * 0.5 + entity.origin.x,
			request.key.view.viewport.height() * 0.5 - entity.origin.y);
		painter.drawPolygon(QPolygonF{center + QPointF(0,-5),center + QPointF(5,0),center + QPointF(0,5),center + QPointF(-5,0)});
	}
	painter.drawImage(QPointF(0,0),expectedMembers(request)); return image;
}
QRect memberRegion(double ratio, QSize viewport = {801,603})
{
	// Member 30 is inside the selection bounds, away from primary, HUD, handles,
	// border and neighboring rings. Its TopXY center is (360.5,336.5).
	return QRect(int(std::ceil((viewport.width() * 0.5 - 51) * ratio)),int(std::ceil((viewport.height() * 0.5 + 24) * ratio)),int(22 * ratio),int(22 * ratio));
}
int channelDifference(const QImage& a, const QImage& b)
{
	if (a.size() != b.size()) { return 255; }
	int maximum = 0;
	for (int y = 0; y < a.height(); ++y) { for (int x = 0; x < a.width(); ++x) {
		const auto ca = a.pixelColor(x,y), cb = b.pixelColor(x,y);
		maximum = std::max({maximum,std::abs(ca.red() - cb.red()),std::abs(ca.green() - cb.green()),std::abs(ca.blue() - cb.blue()),std::abs(ca.alpha() - cb.alpha())});
	} }
	return maximum;
}
bool sameAsFresh(MapViewport& view, bool contrast = true, double ratio = 1)
{
	MapViewport fresh; fresh.resize(view.size()); fresh.setHighContrast(contrast); fresh.setReducedMotion(true);
	fresh.setFont(view.font()); fresh.setLayoutDirection(view.layoutDirection());
	fresh.setShowGrid(view.showGrid()); fresh.setGridSize(view.gridSize()); fresh.setShowLabels(false); fresh.setShowThings(false);
	fresh.setDocument(view.displayDocument()); fresh.setSelectionSet(view.selectionSet()); fresh.restoreNavigationState(view.navigationState());
	auto actual = imageFor(view.size(),ratio), expected = imageFor(view.size(),ratio);
	return expect(tests::settleMapViewport(view,&actual) && tests::settleMapViewport(fresh,&expected) && actual == expected,
		"current overlay result matches a freshly constructed viewport");
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
}

int main(int argc, char** argv)
{
	// Semantic Qt calls and QWidget::render targets only: no input or OS capture.
	qputenv("QT_QPA_PLATFORM","offscreen"); qputenv("QT_SCALE_FACTOR","1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!capture.isEmpty()) { QDir().mkpath(capture); }
	bool ok = true; auto request = fixture(); const auto original = request.document;
	MapViewportOverlayWorker worker; MapViewportOverlayResult last; int callbacks = 0; bool guiCallbacks = true;
	worker.completed = [&](const MapViewportOverlayResult& result) {
		last = result; ++callbacks; guiCallbacks &= QThread::currentThread() == app.thread();
	};
	for (const auto projection : {MapViewportProjection::TopXY,MapViewportProjection::FrontXZ,MapViewportProjection::SideZY}) {
		for (const double ratio : {1.0,1.25,1.5,1.75,2.0}) {
			request.key.projection = projection; request.key.view.pixelRatio = ratio;
			request.key.view.center = {13.25,-7.75}; request.key.view.zoom = 0.87;
			request.key.markerWidth = ratio == 1 ? 1.4 : 2;
			MapGridFrame expectedGrid; ok &= renderMapGrid(request.key.view,&expectedGrid);
			worker.request(request); ok &= expect(waitFor(worker),"background overlay completes");
			ok &= expect(last.ready && !last.failed && last.markerCount == 80 && last.members == expectedMembers(request)
				&& last.grid.image == expectedGrid.image,"worker preserves native marker and grid pixels in all projections and fractional scales");
		}
	}
	ok &= expect(guiCallbacks,"overlay callbacks remain on the GUI thread");
	request.previousGrid = last.grid; const auto gridStorage = last.grid.image.cacheKey();
	request.key.selectionRevision++; request.primary = request.selection.first();
	worker.request(request); ok &= waitFor(worker);
	ok &= expect(last.grid.image.cacheKey() == gridStorage,"selection-only replacement retains unchanged grid storage");

	// Thousands of offscreen and coincident references cannot consume the visible
	// marker budget. A late distinct marker remains visible; primary stays separate.
	auto dense = fixture(); dense.document.entities.clear(); dense.selection.clear();
	const auto add = [&](int id, double x, double y) {
		LevelMapEntity entity; entity.id = id; entity.origin = {x,y,0,true};
		dense.document.entities.append(entity); dense.selection.append({LevelMapSelectionKind::Entity,id});
	};
	for (int i = 0; i < 2048; ++i) { add(10000 + i * 3,100000 + i,100000); }
	for (int i = 0; i < 1024; ++i) { add(50000 + i * 3,-100,0); }
	add(70000,100,0); add(70003,0,100); dense.primary = dense.selection.last(); dense.index.rebuild(dense.document);
	QVector<QPointF> centers;
	ok &= mapViewportMemberCenters(dense,&centers);
	ok &= expect(centers == QVector<QPointF>{{300.5,301.5},{500.5,301.5}},"offscreen and coincident members preserve late visible identities");
	for (int i = 0; i < 600; ++i) { add(80000 + i * 3,-300 + (i % 30) * 20,-200 + (i / 30) * 20); }
	dense.index.rebuild(dense.document); ok &= mapViewportMemberCenters(dense,&centers);
	ok &= expect(centers.size() == 512,"visible member count remains bounded independently of full selection");
	std::atomic_bool cancelled = true; const auto retainedMembers = last.members.cacheKey();
	ok &= expect(!renderMapViewportOverlays(request,&last,&cancelled) && last.members.cacheKey() == retainedMembers,
		"cancelled overlay never publishes partial images");
	const auto retainedCenters = centers;
	ok &= expect(!mapViewportMemberCenters(dense,&centers,&cancelled) && centers == retainedCenters,"cancelled traversal preserves caller output");
	auto oversized = request; oversized.key.view.viewport = {2000,2000}; oversized.key.view.pixelRatio = 2;
	ok &= expect(!renderMapViewportOverlays(oversized,&last) && last.members.cacheKey() == retainedMembers,"oversized physical image keeps previous result intact");
	worker.request(oversized); ok &= waitFor(worker);
	ok &= expect(last.failed && !last.ready,"worker reports rejected image allocation for ordinary fallback");
	// Shared sparse-ID projection covers every object family, including a Doom
	// back-sidedef fallback, cached brush bounds and unsolved patch point averages.
	LevelMapDocument mixed;
	LevelMapDoomVertex va; va.id = 91; va.x = -10; va.y = 20;
	LevelMapDoomVertex vb; vb.id = 207; vb.x = 30; vb.y = 40; mixed.doomVertices = {va,vb};
	LevelMapDoomLinedef line; line.id = 503; line.startVertex = 91; line.endVertex = 207; line.backSidedef = 803; mixed.doomLinedefs = {line};
	LevelMapDoomSidedef side; side.id = 803; side.sector = 1201; mixed.doomSidedefs = {side};
	LevelMapDoomSector sector; sector.id = 1201; sector.floorHeight = 64; mixed.doomSectors = {sector};
	LevelMapDoomThing thing; thing.id = 1301; thing.x = 13; thing.y = -27; mixed.doomThings = {thing};
	LevelMapBrush brush; brush.id = 2201; brush.boundsSolved = true; brush.mins = {-8,-4,10,true}; brush.maxs = {12,16,50,true}; mixed.brushes = {brush};
	MapBrushGeometry geometry; geometry.brushId = 2201; geometry.solved = true; geometry.mins = {20,40,80,true}; geometry.maxs = {40,80,160,true};
	QVector<MapBrushGeometry> geometryCache {geometry};
	LevelMapPatch patch; patch.id = 3301; patch.controlPoints = {{2,4,6,true},{8,10,18,true}}; mixed.patches = {patch};
	DoomSectorOutline outline; outline.sectorId = 1201; outline.bounds = {-10,20,40,20}; QVector<DoomSectorOutline> outlines {outline};
	MapViewportSceneIndex mixedIndex; mixedIndex.rebuild(mixed); mixedIndex.rebuildGeometry(geometryCache,outlines);
	QPointF point;
	const auto projected = [&](LevelMapSelectionKind kind, int id, MapViewportProjection projection, QPointF expected) {
		return mapViewportObjectPoint(mixed,outlines,geometryCache,mixedIndex,projection,kind,id,&point) && point == expected;
	};
	ok &= expect(projected(LevelMapSelectionKind::DoomVertex,91,MapViewportProjection::FrontXZ,{-10,0})
		&& projected(LevelMapSelectionKind::DoomLinedef,503,MapViewportProjection::TopXY,{10,30})
		&& projected(LevelMapSelectionKind::DoomLinedef,503,MapViewportProjection::FrontXZ,{10,64})
		&& projected(LevelMapSelectionKind::DoomLinedef,503,MapViewportProjection::SideZY,{30,64})
		&& projected(LevelMapSelectionKind::DoomThing,1301,MapViewportProjection::TopXY,{13,-27})
		&& projected(LevelMapSelectionKind::DoomSector,1201,MapViewportProjection::TopXY,{10,30})
		&& projected(LevelMapSelectionKind::QuakeBrush,2201,MapViewportProjection::SideZY,{60,120})
		&& projected(LevelMapSelectionKind::QuakePatch,3301,MapViewportProjection::SideZY,{7,12}),"shared marker projection preserves known sparse Doom/brush/patch positions");
	geometryCache.clear(); mixed.patches.first().mins = {-20,-40,10,true}; mixed.patches.first().maxs = {20,40,50,true};
	ok &= expect(projected(LevelMapSelectionKind::QuakeBrush,2201,MapViewportProjection::FrontXZ,{2,30})
		&& projected(LevelMapSelectionKind::QuakePatch,3301,MapViewportProjection::FrontXZ,{0,30})
		&& !mapViewportObjectPoint(mixed,outlines,geometryCache,mixedIndex,MapViewportProjection::TopXY,LevelMapSelectionKind::Entity,91,&point),
		"marker projection retains source bounds fallback and rejects missing identities");

	request = fixture(); request.key.view.viewport = {1801,1203}; request.key.view.units = 8;
	callbacks = 0; worker.request(request); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	for (int i = 0; i < 40; ++i) {
		request.key.view.center += QPointF(3.25,1.75); request.key.view.zoom += 0.01;
		if (i == 20) { request.key.selectionRevision++; request.primary = request.selection.first(); }
		if (i == 30) { request.key.sceneRevision++; request.key.projection = MapViewportProjection::SideZY; }
		worker.request(request);
	}
	ok &= expect(waitFor(worker) && callbacks <= 2 && last.ready && sameMapViewportOverlayKey(last.key,request.key),
		"navigation/selection/source bursts coalesce to the latest complete overlay");
	callbacks = 0; worker.request(request); worker.cancel(); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	ok &= expect(!worker.busy() && callbacks == 0,"cancel before dispatch drops pending overlay work");
	worker.request(request); app.processEvents(QEventLoop::ExcludeUserInputEvents); callbacks = 0; worker.cancel();
	ok &= expect(waitFor(worker) && callbacks == 0,"cancel during work cannot publish a retired result");

	// Actual widget: entity-only scenes exercise this worker independently of
	// the brush worker, including live primary/handles and combined busy status.
	request = fixture(); request.key.showGrid = false;
	MapViewport view; view.resize(request.key.view.viewport); view.setHighContrast(true); view.setReducedMotion(true);
	view.setShowGrid(false); view.setShowLabels(false); view.setShowThings(false); view.setDocument(request.document); view.setSelectionSet(request.selection);
	ok &= view.restoreNavigationState({0,{0,0},1});
	auto image = imageFor(view.size(),1); QElapsedTimer elapsed; elapsed.start(); int beats = 0;
	QTimer heartbeat; heartbeat.setTimerType(Qt::PreciseTimer);
	QObject::connect(&heartbeat,&QTimer::timeout,&app,[&] { ++beats; }); heartbeat.start(1);
	view.render(&image); const double firstPaintMs = elapsed.nsecsElapsed() / 1e6;
	ok &= expect(view.isOverlayRendering() && view.isRendering() && view.accessibleDescription().contains(QStringLiteral("Updating view")),
		"member-only background work exposes the accessible pending state");
	const auto initial = image;
	ok &= tests::settleMapViewport(view,&image); heartbeat.stop();
	ok &= expect(beats > 0 && !view.isRendering() && !view.accessibleDescription().contains(QStringLiteral("Updating view")),
		"event loop stays live until overlay completion clears the pending state");
	// Primary crosshair at (560.5,161.5) is already present in the first paint.
	ok &= expect(initial.copy(QRect(543,145,36,36)) == image.copy(QRect(543,145,36,36)),"primary marker remains current while member image is pending");
	for (const double ratio : {1.0,1.25,1.5,1.75,2.0}) {
		request.key.view.pixelRatio = ratio; image = imageFor(view.size(),ratio);
		ok &= tests::settleMapViewport(view,&image);
		const auto reference = memberReference(request);
		const bool matches = image.copy(memberRegion(ratio)) == reference.copy(memberRegion(ratio));
		if (!matches && !capture.isEmpty()) {
			image.save(QDir(capture).filePath(QStringLiteral("overlay-member-actual-%1.png").arg(ratio)));
			reference.save(QDir(capture).filePath(QStringLiteral("overlay-member-reference-%1.png").arg(ratio)));
		}
		ok &= expect(matches,
			"widget member images retain physical alignment at odd pane sizes and fractional DPR");
	}
	view.setShowGrid(true); request.key.view.pixelRatio = 1.25; image = imageFor(view.size(),1.25);
	ok &= tests::settleMapViewport(view,&image);
	MapGridFrame grid; ok &= renderMapGrid(request.key.view,&grid);
	auto gridReference = imageFor(view.size(),1.25); gridReference.fill(Qt::black);
	{ QPainter painter(&gridReference); painter.drawImage(QPointF(0,0),grid.image); }
	ok &= expect(image.copy(QRect(45,100,100,100)) == gridReference.copy(QRect(45,100,100,100)),
		"widget background grid preserves fractional physical placement outside the scene");
	view.setShowGrid(false); view.resize(1601,1403); view.restoreNavigationState({0,{0,0},1}); image = imageFor(view.size(),2);
	view.render(&image); auto fallback = fixture(); fallback.key.view.viewport = view.size(); fallback.key.view.pixelRatio = 2;
	ok &= expect(!view.isOverlayRendering() && channelDifference(image.copy(memberRegion(2,view.size())),memberReference(fallback).copy(memberRegion(2,view.size()))) <= 4,
		"physical targets above the image ceiling retain complete ordinary member rendering");
	view.resize(request.key.view.viewport); view.restoreNavigationState({0,{0,0},1});
	image = imageFor(view.size(),1); ok &= tests::settleMapViewport(view,&image);
	if (!capture.isEmpty()) { ok &= image.save(QDir(capture).filePath("overlay-worker-selected.png")); }
	// Remove a member while enough remain to keep the async path active. The old
	// member ring must disappear before any event-loop delivery can replace it.
	auto changedSelection = request.selection; changedSelection.removeAt(30); view.setSelectionSet(changedSelection); view.render(&image);
	const auto retired = image.copy(memberRegion(1));
	bool retiredInk = false;
	for (int y = 0; y < retired.height(); ++y) { for (int x = 0; x < retired.width(); ++x) {
		const auto color = retired.pixelColor(x,y); retiredInk |= color.red() > 80 && color.blue() > 80 && color.green() < 40;
	} }
	ok &= expect(view.isOverlayRendering() && !retiredInk,"selection mutation immediately clears retired member highlights");
	ok &= sameAsFresh(view);
	const auto start = view.navigationState();
	for (int i = 0; i < 12; ++i) {
		auto state = start; state.center += QPointF(i * 7.5,i * -3.25); state.zoom *= 1 + i * 0.04;
		view.applyLinkedNavigation(state); view.render(&image);
		if (i == 4) { app.processEvents(QEventLoop::ExcludeUserInputEvents); }
	}
	ok &= sameAsFresh(view);
	view.setShowGrid(true); view.setGridSize(8); view.render(&image); view.setGridSize(32); ok &= sameAsFresh(view);
	view.setShowGrid(false); view.render(&image); ok &= sameAsFresh(view);
	const auto retainedState = view.navigationState();
	auto temporaryState = retainedState; temporaryState.center += QPointF(35.25,-21.75);
	view.applyLinkedNavigation(temporaryState); view.render(&image); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	view.applyLinkedNavigation(retainedState); view.render(&image);
	ok &= expect(!view.isOverlayRendering(),"returning to the presented view cancels its obsolete replacement");
	ok &= sameAsFresh(view);
	for (const auto projection : {MapViewportProjection::SideZY,MapViewportProjection::FrontXZ,MapViewportProjection::TopXY}) {
		view.setProjection(projection); view.render(&image);
	}
	ok &= sameAsFresh(view);
	// Pending old source with the same path must never overwrite its replacement.
	view.setSelectionSet(request.selection); view.render(&image); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	auto replacement = request.document; replacement.entities.remove(0,10);
	view.updateDocument(replacement); ok &= sameAsFresh(view);
	view.setDocument(request.document); view.setSelectionSet(request.selection); view.render(&image);
	ok &= expect(view.hideSelection() == 81,"visibility change hides all selected member identities");
	view.render(&image); ok &= expect(!view.isRendering(),"hidden/cleared member-only scene has no pending overlay");
	view.showAllHidden(); view.setSelectionSet(request.selection);
	MapViewport sibling; sibling.resize(view.size()); sibling.setHighContrast(true); sibling.setShowThings(false); sibling.setReducedMotion(true);
	sibling.synchronizeSceneFrom(view,true); sibling.setProjection(MapViewportProjection::SideZY);
	auto siblingImage = imageFor(sibling.size(),1); sibling.render(&siblingImage);
	view.setSelectionSet({}); ok &= sameAsFresh(sibling);
	view.setDocument(request.document); view.setSelectionSet(request.selection); view.render(&image); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	view.clearDocument(); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	ok &= expect(!view.hasDocument() && !view.isRendering(),"closing a scene retires pending member work");

	// Doom members use the same worker without relying on Quake geometry weight.
	LevelMapDocument doom; doom.format = LevelMapFormat::DoomWad; QVector<LevelMapSelectionRef> things;
	for (const auto& entity : request.document.entities) {
		LevelMapDoomThing thing; thing.id = entity.id; thing.x = entity.origin.x; thing.y = entity.origin.y;
		doom.doomThings.append(thing); things.append({LevelMapSelectionKind::DoomThing,thing.id});
	}
	view.setDocument(doom); view.setSelectionSet(things); view.render(&image);
	ok &= expect(view.isOverlayRendering(),"large Doom selections use the shared member worker");
	ok &= sameAsFresh(view);
	Expanded translator; app.installTranslator(&translator);
	QFont expandedFont = view.font(); expandedFont.setPointSizeF(18); view.setFont(expandedFont); view.setLayoutDirection(Qt::RightToLeft);
	view.setShowGrid(true); view.resize(701,503); image = imageFor(view.size(),1.25); view.render(&image);
	ok &= expect(view.isRendering() && view.accessibleDescription().contains(QStringLiteral("Updating view… expanded")),
		"combined pending state remains translatable at enlarged RTL scale");
	ok &= tests::settleMapViewport(view,&image);
	if (!capture.isEmpty()) { ok &= image.save(QDir(capture).filePath("overlay-worker-doom-rtl.png")); }
	app.removeTranslator(&translator);
	QElapsedTimer closing; closing.start();
	{
		auto pending = std::make_unique<MapViewport>(); pending->resize(1801,1203); pending->setDocument(request.document);
		pending->setSelectionSet(request.selection); auto target = imageFor(pending->size(),1); pending->render(&target);
		app.processEvents(QEventLoop::ExcludeUserInputEvents); closing.restart(); pending.reset();
	}
	const double closeMs = closing.nsecsElapsed() / 1e6;
	const bool unchanged = std::equal(original.entities.cbegin(),original.entities.cend(),request.document.entities.cbegin(),request.document.entities.cend(),
		[](const auto& a, const auto& b) { return a.id == b.id && a.className == b.className && a.selected == b.selected
			&& a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.origin.z == b.origin.z && a.origin.valid == b.origin.valid; });
	ok &= expect(unchanged && original.selection == request.document.selection && request.document.undoStack.isEmpty(),
		"overlay work preserves source objects, selection and undo state");
	std::cout << QJsonDocument(QJsonObject{{"ok",ok},{"firstPaintMs",firstPaintMs},{"heartbeatCount",beats},{"closeMs",closeMs}})
		.toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
