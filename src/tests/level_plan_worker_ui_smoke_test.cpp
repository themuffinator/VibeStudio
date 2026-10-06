#include "app/map_plan_render_worker.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_patch.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/map_viewport_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool waitFor(MapPlanRenderWorker& worker)
{
	QEventLoop loop; QTimer pulse, deadline; deadline.setSingleShot(true);
	QObject::connect(&pulse, &QTimer::timeout, &loop, [&] { if (!worker.busy()) { loop.quit(); } });
	QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
	pulse.start(1); deadline.start(30000);
	if (worker.busy()) { loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return !worker.busy();
}
QImage imageFor(const MapViewport& view, int scale = 1)
{
	QImage image(view.size() * scale, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(scale); image.fill(Qt::transparent); return image;
}
bool matchesFresh(MapViewport& view, bool highContrast = false, int scale = 1,
	const LevelMapDocument* fullSource = nullptr, const QVector<LevelMapSelectionRef>& hidden = {})
{
	MapViewport fresh; fresh.resize(view.size()); fresh.setFont(view.font()); fresh.setLayoutDirection(view.layoutDirection());
	fresh.setHighContrast(highContrast); fresh.setShowGrid(view.showGrid()); fresh.setShowLabels(view.showLabels());
	fresh.setDocument(fullSource ? *fullSource : view.displayDocument());
	if (!hidden.isEmpty()) { fresh.setSelectionSet(hidden); fresh.hideSelection(); }
	fresh.setSelectionSet(view.selectionSet()); fresh.restoreNavigationState(view.navigationState());
	auto actual = imageFor(view,scale), expected = imageFor(fresh,scale);
	if (!tests::settleMapViewport(view,&actual) || !tests::settleMapViewport(fresh,&expected)) { return expect(false,"viewport render timed out"); }
	if (actual != expected && !qEnvironmentVariableIsEmpty("VIBESTUDIO_TEST_CAPTURE_DIR")) {
		const QDir output(qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR"));
		actual.save(output.filePath("worker-mismatch-actual.png")); expected.save(output.filePath("worker-mismatch-fresh.png"));
	}
	return expect(actual == expected,"current background frame must match a freshly constructed viewport");
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
}

int main(int argc, char** argv)
{
	// Direct semantic calls and Qt render targets only. No input or OS capture.
	qputenv("QT_QPA_PLATFORM","offscreen"); qputenv("QT_SCALE_FACTOR","1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!capture.isEmpty()) { QDir().mkpath(capture); }
	bool ok = true; QString error;
	LevelMapDocument document;
	if (!tests::createGeometryFixture(625,&document,&error)) { std::cerr << error.toStdString(); return 1; }
	document.sourcePath = QStringLiteral("synthetic-worker.map");
	LevelMapEntity owner; owner.id = 71; owner.className = QStringLiteral("func_group"); document.entities.append(owner);
	for (auto& brush : document.brushes) {
		const double z = (brush.id % 17) * 24;
		brush.id = 101 + brush.id * 3;
		if (brush.id < 111) { brush.entityId = owner.id; }
		for (auto& face : brush.faces) { for (auto* point : {&face.p0,&face.p1,&face.p2}) { point->z += z; } }
		brush.mins.z += z; brush.maxs.z += z;
	}
	LevelMapPatch patch; LevelPatchCreateRequest patchRequest;
	patchRequest.center = {300,300,240,true}; patchRequest.size = {256,256,128,true};
	if (!createLevelPatch(patchRequest,&patch,&error)) { return 1; }
	patch.id = 907; patch.entityId = owner.id;
	for (int row = 0; row < 3; ++row) { patch.controlPoints[row * 3 + 1].z += 128; }
	refreshLevelPatchBounds(&patch); document.patches.append(patch);
	// A diagnosed source object must survive both cached and fallback rendering.
	document.brushes.last().faces.clear(); document.brushes.last().boundsSolved = true;
	const auto sourceBytes = serializeLevelMap(document).bytes;
	MapPlanRenderRequest request; request.sceneRevision = 1; request.selectionRevision = 1;
	request.worldspawnId = document.entities.first().id; request.document = document;
	request.brushes = buildLevelMapBrushGeometry(document); request.index.rebuild(document); request.index.rebuildGeometry(request.brushes,{});
	request.selection = {{LevelMapSelectionKind::Entity,owner.id},{LevelMapSelectionKind::QuakeBrush,document.brushes.last().id}};
	request.view.viewport = {900,600}; request.view.center = {576,300}; request.view.zoom = 0.6; request.view.pixelRatio = 1;
	request.view.world = qRgb(100,200,230); request.view.entity = qRgb(80,240,150); request.view.invalid = qRgb(240,90,80);
	request.view.selection = qRgb(255,205,70); request.view.patch = qRgb(180,140,240);

	// Worker and direct rendering agree across projected brush, patch and warning
	// geometry, pixel scales and contrast. Callbacks must remain on the GUI thread.
	MapPlanRenderWorker worker;
	MapPlanRenderResult last; int callbacks = 0; bool guiCallbacks = true;
	worker.completed = [&](const MapPlanRenderResult& result) {
		last = result; ++callbacks; guiCallbacks &= QThread::currentThread() == app.thread();
	};
	for (const auto projection : {MapViewportProjection::TopXY,MapViewportProjection::FrontXZ,MapViewportProjection::SideZY}) {
		for (int scale : {1,2}) {
			request.projection = projection; request.view.pixelRatio = scale; request.view.highContrast = scale == 2;
			const auto wires = buildMapPlanWires(document,request.brushes,request.index,projection,request.worldspawnId);
			const auto selected = buildMapPlanSelectionWires(document,request.brushes,request.index,projection,request.selection);
			MapPlanWireFrame expected, expectedSelection;
			ok &= renderMapPlanFrame(request,false,wires,&expected) && renderMapPlanFrame(request,true,selected,&expectedSelection);
			const auto imageKey = expected.image.cacheKey();
			ok &= expect(renderMapPlanFrame(request,false,wires,&expected) && expected.image.cacheKey() == imageKey,"unchanged frames reuse image storage");
			worker.request(request); ok &= expect(waitFor(worker),"background rendering completes");
			ok &= expect(!last.failed && last.baseFrame.image == expected.image && last.selectionFrame.image == expectedSelection.image,
				"background output exactly matches complete direct output");
		}
	}
	ok &= expect(guiCallbacks,"render results arrive on the GUI thread");
	std::atomic_bool cancelled = true;
	ok &= expect(!buildMapPlanWires(document,request.brushes,request.index,request.projection,request.worldspawnId,{},&cancelled).statistics.ready,
		"cancelled base projection is discarded");
	ok &= expect(!buildMapPlanSelectionWires(document,request.brushes,request.index,request.projection,request.selection,{},&cancelled).statistics.ready,
		"cancelled selection projection is discarded");
	auto retained = last.baseFrame; const auto retainedKey = retained.image.cacheKey();
	ok &= expect(!renderMapPlanFrame(request,false,last.wires,&retained,&cancelled) && retained.image.cacheKey() == retainedKey,
		"cancelled painting never publishes a partial replacement");
	cancelled = false; int outlines = 0;
	ok &= expect(!visitMapPlanSelectionOutlines(document,request.brushes,request.index,request.projection,request.selection,
		[&](const QPolygonF&) { ++outlines; cancelled = true; return true; },&cancelled) && outlines == 1,
		"cancellation is observed during selected geometry traversal");

	// Exhausted wire budgets retain a complete fallback frame, including patches.
	request.sceneRevision++; request.limits.uniqueEdges = 1; request.view.pixelRatio = 1;
	worker.request(request); ok &= waitFor(worker);
	ok &= expect(last.wiresComputed && !last.wires.statistics.ready && last.selectionWiresComputed && !last.selectionWires.statistics.ready
		&& !last.failed,"wire exhaustion uses complete background painter fallback");
	for (const bool selection : {false,true}) {
		QImage expected(request.view.viewport,QImage::Format_ARGB32_Premultiplied); expected.fill(Qt::transparent);
		{ QPainter painter(&expected); painter.setRenderHint(QPainter::Antialiasing); ok &= paintMapPlanFallback(painter,request,selection); }
		ok &= expect(expected == (selection ? last.selectionFrame.image : last.baseFrame.image),"fallback retains every source and selected outline");
	}

	// Start work, then replace its pending target repeatedly without dispatching
	// input. Only the running request and latest pending target may complete.
	request.sceneRevision++; request.limits = {}; request.view.viewport = {1400,900};
	request.selection.clear(); for (const auto& brush : document.brushes) { request.selection.append({LevelMapSelectionKind::QuakeBrush,brush.id}); }
	callbacks = 0; worker.request(request); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	for (int i = 0; i < 40; ++i) {
		request.view.center += QPointF(3.25,1.75); request.view.zoom += 0.01;
		if (i == 20) { request.selectionRevision++; request.selection = {{LevelMapSelectionKind::Entity,owner.id}}; }
		worker.request(request);
	}
	ok &= expect(waitFor(worker) && callbacks <= 2 && last.selectionRevision == request.selectionRevision && sameMapPlanView(last.view,request.view),
		"navigation and selection bursts coalesce to the final target");
	ok &= expect(!last.failed && sameMapPlanView(last.baseFrame,request.view) && sameMapPlanView(last.selectionFrame,request.view),"coalesced frames finish at the latest view");
	worker.request(request); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	request.sceneRevision++; request.projection = MapViewportProjection::TopXY; request.document.patches.clear();
	request.selectionRevision++; request.selection.clear();
	worker.request(request); ok &= expect(waitFor(worker) && last.sceneRevision == request.sceneRevision && last.projection == request.projection,
		"scene and projection replacement retires the old work");
	callbacks = 0; worker.request(request); worker.cancel(); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	ok &= expect(!worker.busy() && callbacks == 0,"cancel before dispatch discards pending work");
	auto oversized = request.view; oversized.viewport = {1600,1400}; oversized.pixelRatio = 2;
	ok &= expect(!supportedMapPlanFrame(oversized),"physical targets above the image budget use the complete ordinary path");

	MapViewport view; view.resize(1100,720); view.setShowLabels(false); view.setDocument(document);
	view.setSelectionSet({{LevelMapSelectionKind::Entity,owner.id}});
	auto image = imageFor(view); QElapsedTimer elapsed; elapsed.start(); int beats = 0; qint64 previous = 0, maximumGap = 0;
	QTimer heartbeat; heartbeat.setTimerType(Qt::PreciseTimer);
	QObject::connect(&heartbeat,&QTimer::timeout,&app,[&] {
		const auto now = elapsed.nsecsElapsed(); maximumGap = std::max(maximumGap,now - previous); previous = now; ++beats;
	});
	heartbeat.start(1); view.render(&image); const double firstPaintMs = elapsed.nsecsElapsed() / 1e6;
	ok &= expect(view.isRendering() && view.accessibleDescription().contains(QStringLiteral("Updating view")),"large plan paints queue work and expose updating status");
	const QImage initialImage = image;
	ok &= tests::settleMapViewport(view,&image); heartbeat.stop();
	maximumGap = std::max(maximumGap,elapsed.nsecsElapsed() - previous);
	ok &= expect(beats > 0 && !view.isRendering() && !view.accessibleDescription().contains(QStringLiteral("Updating view")),
		"GUI heartbeat runs during rendering and completion clears the accessible status");
	const double settledMs = elapsed.nsecsElapsed() / 1e6;
	if (!capture.isEmpty()) { ok &= initialImage.save(QDir(capture).filePath("worker-updating.png")); }
	ok &= matchesFresh(view);
	const auto original = view.navigationState();
	for (int i = 0; i < 12; ++i) {
		auto state = original; state.center += QPointF(i * 17.5,i * -8.25); state.zoom *= 1.0 + i * 0.05;
		ok &= view.applyLinkedNavigation(state); view.render(&image);
		if (i == 4) { app.processEvents(QEventLoop::ExcludeUserInputEvents); }
	}
	ok &= expect(view.isRendering(),"changed navigation uses an interim view while work is pending");
	if (!capture.isEmpty()) { ok &= image.save(QDir(capture).filePath("worker-navigation-updating.png")); }
	ok &= matchesFresh(view);
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,101}}); view.render(&image); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,104}}); view.render(&image);
	view.setSelectionSet({}); ok &= matchesFresh(view);
	view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,104}}); view.render(&image);
	ok &= expect(view.hideSelection() == 1,"hide current object while its selected image is pending");
	ok &= matchesFresh(view,false,1,&document,{{LevelMapSelectionKind::QuakeBrush,104}});
	view.showAllHidden(); view.render(&image);
	for (const auto projection : {MapViewportProjection::SideZY,MapViewportProjection::FrontXZ,MapViewportProjection::TopXY}) {
		view.setProjection(projection); view.render(&image);
	}
	ok &= matchesFresh(view);
	// Same path, changed source; the immutable old snapshot must not be adopted.
	view.setSelectionSet({{LevelMapSelectionKind::Entity,owner.id}}); view.render(&image); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	auto replacement = document; replacement.brushes.remove(0,400); replacement.patches.clear();
	view.setDocument(replacement); ok &= matchesFresh(view);
	view.setDocument(document); view.render(&image); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	view.clearDocument(); app.processEvents(QEventLoop::ExcludeUserInputEvents);
	ok &= expect(!view.hasDocument() && !view.isRendering() && !view.planWireStatistics().ready,"closed scene cannot regain retired rendering data");
	view.setDocument(document); view.setSelectionSet({{LevelMapSelectionKind::Entity,owner.id}});
	MapViewport sibling; sibling.resize(view.size()); sibling.setShowLabels(false); sibling.synchronizeSceneFrom(view,true);
	sibling.setProjection(MapViewportProjection::SideZY); auto siblingImage = imageFor(sibling); sibling.render(&siblingImage);
	view.render(&image); view.setSelectionSet({}); ok &= matchesFresh(view) && matchesFresh(sibling);
	Expanded translator; app.installTranslator(&translator);
	applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,150));
	view.setFont(app.font()); view.setHighContrast(true); view.setLayoutDirection(Qt::RightToLeft);
	view.setSelectionSet({{LevelMapSelectionKind::Entity,owner.id}}); view.resize(900,660); image = imageFor(view,2); view.render(&image);
	ok &= expect(view.isRendering() && view.accessibleDescription().contains(QStringLiteral("Updating view… expanded")),"busy text is translatable at enlarged RTL scale");
	ok &= matchesFresh(view,true,2); ok &= tests::settleMapViewport(view,&image);
	if (!capture.isEmpty()) { ok &= image.save(QDir(capture).filePath("worker-rtl-hidpi.png")); }
	app.removeTranslator(&translator);
	QElapsedTimer closing; closing.start();
	{
		auto closingView = std::make_unique<MapViewport>(); closingView->resize(1200,800); closingView->setDocument(document);
		auto pendingImage = imageFor(*closingView); closingView->render(&pendingImage); app.processEvents(QEventLoop::ExcludeUserInputEvents);
		closing.restart(); closingView.reset();
	}
	const double closeMs = closing.nsecsElapsed() / 1e6;
	ok &= expect(sourceBytes == serializeLevelMap(document).bytes && document.undoStack.isEmpty() && document.redoStack.isEmpty(),"background rendering preserves source and undo state");
	std::cout << QJsonDocument(QJsonObject{{"ok",ok},{"brushes",625},{"firstPaintMs",firstPaintMs},{"settledMs",settledMs},
		{"heartbeatCount",beats},{"maxEventGapMs",maximumGap / 1e6},{"closeMs",closeMs}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
