#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
bool settle(ModelViewport& view)
{
	QElapsedTimer elapsed; elapsed.start();
	QImage image(view.size(),QImage::Format_ARGB32_Premultiplied);
	while (elapsed.elapsed() < 30000) {
		view.render(&image);
		if (!view.isRendering()) { return true; }
		QEventLoop events; QTimer timeout; timeout.setSingleShot(true);
		QObject::connect(&view,&ModelViewport::renderCompleted,&events,&QEventLoop::quit);
		QObject::connect(&timeout,&QTimer::timeout,&events,&QEventLoop::quit); timeout.start(30000-int(elapsed.elapsed()));
		events.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return false;
}
QImage capture(ModelViewport& view)
{
	QImage image(view.size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); view.render(&image); return image;
}
int difference(const QImage& a, const QImage& b)
{
	int delta = 0;
	for (int y = 80; y < a.height()-20; ++y) { for (int x = 10; x < a.width()-10; ++x) {
		const auto p = a.pixel(x,y),q = b.pixel(x,y);
		delta = std::max({delta,std::abs(qRed(p)-qRed(q)),std::abs(qGreen(p)-qGreen(q)),std::abs(qBlue(p)-qBlue(q))});
	} }
	return delta;
}
void setup(ModelViewport& view, const LevelMapPreviewMesh& mesh)
{
	view.resize(1000,720); view.setMesh(mesh.mesh); view.setShowGrid(false); view.setShowAxes(false);
	view.setCameraControls(trenchBroomLevelControls().camera); view.setCameraView({-180,-220,160},50,-30); view.setMoveGrid(16);
	QVector<int> selected;
	for (int i = 0; i < mesh.owners.size(); ++i) { if (mesh.owners[i] == LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,0}) { selected.append(i); } }
	view.setHighlightedTriangles(selected);
}
QPointF targetPoint(ModelViewport& view, const ResizeBox& from, const ResizeBox& to, int handle)
{
	view.setSelectionResizeBox(to); const auto point = view.selectionResizeHandles()[handle]; view.setSelectionResizeBox(from); return point;
}
bool checkLabels(ModelViewport& view)
{
	const auto labels = view.selectionResizeLabels(); const auto handles = view.selectionResizeHandles();
	bool ok = true;
	for (int i = 0; i < 6; ++i) {
		if (!std::isfinite(handles[i].x())) { continue; }
		ok &= expect(!labels[i].isEmpty() && view.rect().contains(labels[i].toAlignedRect()),"axis label fits within the camera pane");
		ok &= expect(view.selectionResizeHandleAt(labels[i].center()) == i,"label is a usable target for its own face handle");
		for (int j = 0; j < i; ++j) { ok &= expect(!labels[i].intersects(labels[j]),"axis labels never overlap"); }
	}
	return ok;
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
QJsonObject measureResizeScene(int count)
{
	LevelMapDocument source; QString error;
	bool ok = tests::createGeometryFixture(count,&source,&error);
	QVector<LevelMapSelectionRef> selected;
	for (int id = 0; id < count; ++id) { selected.append({LevelMapSelectionKind::QuakeBrush,id}); }
	ok &= setLevelMapSelection(&source,selected,&error);
	LevelMapVec3 low,high; ok &= levelMapSelectionBounds(source,&low,&high);
	const ResizeBox from{{low.x,low.y,low.z},{high.x,high.y,high.z}};
	const auto mesh = buildLevelMapPreviewMesh(source);
	ok &= expect(!mesh.truncated && mesh.triangles == count*12,"large selection retains all source geometry");
	ModelViewport view; view.resize(1000,720); view.setMesh(mesh.mesh); view.setShowGrid(false); view.setShowAxes(false);
	auto controls = trenchBroomLevelControls().camera; controls.perspective = false; view.setCameraControls(controls);
	view.setOrbit(35,25); view.setMoveGrid(16);
	QVector<int> triangles(mesh.owners.size()); std::iota(triangles.begin(),triangles.end(),0); view.setHighlightedTriangles(triangles);
	std::array<QPointF,8> targets;
	for (int i = 0; i < 8; ++i) {
		auto to = from; to.maxs[0] += (i+1)*16; view.setSelectionResizeBox(to); targets[i] = view.selectionResizeHandles()[1];
	}
	view.setSelectionResizeBox(from); ok &= settle(view);
	if (!expect(ok && view.beginSelectionResize(view.selectionResizeHandles()[1]),"large selection resize begins")) { return {{"brushes",count},{"ok",false}}; }
	QImage frame(view.size(),QImage::Format_ARGB32_Premultiplied);
	QElapsedTimer elapsed; elapsed.start(); qint64 previous = 0,maximumGap = 0; int beats = 0,completed = 0;
	const auto beat = [&] { const auto now = elapsed.nsecsElapsed(); maximumGap = std::max(maximumGap,now-previous); previous = now; ++beats; };
	QTimer heartbeat; heartbeat.setTimerType(Qt::PreciseTimer); heartbeat.setInterval(5);
	QObject::connect(&heartbeat,&QTimer::timeout,&view,beat); heartbeat.start();
	QObject::connect(&view,&ModelViewport::renderCompleted,&view,[&] { ++completed; });
	QVector<double> calls; calls.reserve(64);
	for (int i = 0; i < 64; ++i) {
		QElapsedTimer call; call.start(); ok &= view.updateSelectionResize(targets[i%8]); view.render(&frame);
		calls.append(call.nsecsElapsed()/1e6); QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	}
	ok &= expect(settle(view),"coalesced resize worker reaches the final proposal"); beat(); heartbeat.stop();
	auto finalBox = from; finalBox.maxs[0] += 128;
	ok &= expect(view.isResizingSelection() && view.selectionResizeBox() == finalBox && source.undoStack.isEmpty(),
		"rapid proposals retain the latest bounds without source edits");
	std::sort(calls.begin(),calls.end());
	const double totalMs = elapsed.nsecsElapsed()/1e6;
	const int budget = qEnvironmentVariableIntValue("VIBESTUDIO_LEVEL_RESIZE_MAX_CALL_MS");
	if (budget > 0) { ok &= expect(calls.last() <= budget,"configured resize GUI-call latency budget"); }
	view.finishSelectionResize(false);
	return {{"ok",ok},{"brushes",count},{"triangles",mesh.triangles},{"proposals",64},{"medianCallMs",calls[calls.size()/2]},
		{"maxCallMs",calls.last()},{"maxEventGapMs",maximumGap/1e6},{"heartbeats",beats},{"completedPictures",completed},{"totalMs",totalMs}};
}
}
int main(int argc, char** argv)
{
	// Semantic tool APIs and Qt render targets only. No injected input or OS capture.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); app.setFont(QFont(QStringLiteral("Segoe UI"),10));
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	if (argc > 1 && QByteArray(argv[1]) == "--latency") {
		QJsonArray records; bool ok = true;
		for (int count : {1000,10000}) { const auto result = measureResizeScene(count); ok &= result.value("ok").toBool(); records.append(result); }
		std::cout << QJsonDocument(records).toJson(QJsonDocument::Compact).constData() << '\n'; return ok ? 0 : 1;
	}
	LevelMapDocument source; QString error;
	if (!tests::createGeometryFixture(2,&source,&error)) { return 1; }
	setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0}},&error);
	const auto bytes = serializeLevelMap(source).bytes; const auto preview = buildLevelMapPreviewMesh(source);
	const ResizeBox from{{-16,-16,-16},{16,16,16}},to{{-16,-16,-16},{48,16,16}};
	ModelViewport view; setup(view,preview); view.setSelectionResizeBox(from);
	bool ok = expect(settle(view),"initial camera image settles");
	ok &= checkLabels(view);
	const auto original = capture(view);
	int requests = 0; ResizeBox committed;
	QObject::connect(&view,&ModelViewport::selectionResizeRequested,&app,[&](const ResizeBox& box) { ++requests; committed = box; });
	const auto end = targetPoint(view,from,to,1); const auto start = view.selectionResizeHandles()[1];
	ok &= expect(view.beginSelectionResize(start) && view.updateSelectionResize(end) && view.selectionResizeBox() == to,
		"camera face handle produces the requested snapped world bounds");
	QVector<int> unchangedSelection;
	for (int i = 0; i < preview.owners.size(); ++i) {
		if (preview.owners[i] == LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,0}) { unchangedSelection.append(i); }
	}
	view.setHighlightedTriangles(unchangedSelection);
	ok &= expect(view.isResizingSelection(),"an unchanged selection refresh preserves the in-progress gesture");
	ok &= expect(view.accessibleDescription().contains(QStringLiteral("64 × 32 × 32")) && settle(view),"preview exposes dimensions and completes in background");
	const auto actual = capture(view);
	auto expectedSource = source;
	ok &= resizeLevelMapSelection(&expectedSource,{to.mins[0],to.mins[1],to.mins[2],true},{to.maxs[0],to.maxs[1],to.maxs[2],true},&error);
	ModelViewport reference; setup(reference,buildLevelMapPreviewMesh(expectedSource)); reference.setSelectionResizeBox(to);
	ok &= settle(reference); const auto expected = capture(reference); const int delta = difference(actual,expected);
	ok &= expect(delta <= 2,"live geometry preview agrees with separately committed and rebuilt map geometry");
	ok &= expect(serializeLevelMap(source).bytes == bytes && source.undoStack.isEmpty(),"preview preserves source bytes and undo history");
	view.finishSelectionResize(false); ok &= settle(view);
	ok &= expect(requests == 0 && difference(capture(view),original) == 0,"cancelling restores the source picture without a commit");
	ok &= view.beginSelectionResize(start); view.finishSelectionResize(true);
	ok &= expect(requests == 0,"unmoved handle does not request an edit");
	ok &= view.beginSelectionResize(start); ok &= view.updateSelectionResize(end);
	const double nan = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(!view.updateSelectionResize({nan,0}),"invalid ray marks preview invalid"); view.finishSelectionResize(true);
	ok &= expect(requests == 0,"invalid release cannot commit the preceding valid preview");
	ok &= view.beginSelectionResize(start); ok &= view.updateSelectionResize(end); view.finishSelectionResize(true);
	ok &= expect(requests == 1 && committed == to,"valid release emits exactly one bounds request");
	const auto labelStart = view.selectionResizeLabels()[1].center();
	ok &= expect(view.beginSelectionResize(labelStart) && view.updateSelectionResize(labelStart+end-start) && view.selectionResizeBox() == to,
		"dragging a displaced label preserves the original face offset");
	view.finishSelectionResize(false);
	// A model/thing marker follows its source origin; its visual dimensions
	// must not stretch when the enclosing map selection changes its spacing.
	QHash<int,BoxResizePoint> origins;
	for (int i = 0; i < preview.owners.size(); ++i) {
		if (preview.owners[i] == LevelMapSelectionRef{LevelMapSelectionKind::QuakeBrush,0}) { origins.insert(i,{0,0,0}); }
	}
	view.setSelectionResizeBox(from,origins);
	ok &= view.beginSelectionResize(start); ok &= view.updateSelectionResize(end); ok &= settle(view);
	const auto pointActual = capture(view);
	auto translated = source; ok &= moveLevelMapSelection(&translated,16,0,0,&error);
	setup(reference,buildLevelMapPreviewMesh(translated)); reference.setSelectionResizeBox(to); ok &= settle(reference);
	const int pointDelta = difference(pointActual,capture(reference));
	ok &= expect(pointDelta <= 2,"point-owned visuals translate with their origins and retain their size");
	view.finishSelectionResize(false);
	int profiles = 0;
	for (const auto& profile : editorProfileDescriptors()) {
		view.setCameraControls(profile.controls.camera); view.setOrbit(35,25); view.setCameraView({-180,-220,160},50,-30);
		view.setSelectionResizeBox(from); const auto handles = view.selectionResizeHandles();
		int handle = -1;
		for (int i = 0; i < 6; ++i) { if (std::isfinite(handles[i].x())) { handle = i; break; } }
		ok &= expect(handle >= 0 && view.beginSelectionResize(handles[handle]),"every profile exposes an explicit camera resize handle");
		view.finishSelectionResize(false); ++profiles;
	}
	setup(view,preview); view.setSelectionResizeBox(from);
	ok &= view.beginSelectionResize(view.selectionResizeHandles()[1]); view.setMoveGrid(8);
	ok &= expect(!view.isResizingSelection(),"grid changes cancel pending resize");
	ok &= view.beginSelectionResize(view.selectionResizeHandles()[1]); view.setCameraView({-180,-220,160},55,-30);
	ok &= expect(!view.isResizingSelection(),"camera changes cancel pending resize");
	ok &= view.beginSelectionResize(view.selectionResizeHandles()[1]); view.setHighlightedTriangles({});
	ok &= expect(!view.isResizingSelection(),"selection replacement cancels pending resize");
	ok &= view.beginSelectionResize(view.selectionResizeHandles()[1]); view.setEnabled(false);
	ok &= expect(!view.isResizingSelection(),"disabled/loading views cancel pending resize"); view.setEnabled(true);
	view.setSelectionResizeBox(from,{},3);
	ok &= expect(!std::isfinite(view.selectionResizeHandles()[4].x()) && !std::isfinite(view.selectionResizeHandles()[5].x()),"Doom axis mask leaves elevation to its property editor");
	Expanded translator; app.installTranslator(&translator);
	applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,200));
	view.setHighContrast(true); view.setLayoutDirection(Qt::RightToLeft); view.setFont(app.font()); view.setSelectionResizeBox(from);
	ok &= checkLabels(view);
	ok &= view.beginSelectionResize(view.selectionResizeHandles()[1]);
	ok &= expect(view.accessibleDescription().contains(QStringLiteral("expanded")),"resize status remains translatable with enlarged RTL text");
	ok &= expect(settle(view),"enlarged high-contrast camera picture completes before visual review");
	const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!output.isEmpty()) {
		QDir().mkpath(output); const QDir folder(output);
		ok &= original.save(folder.filePath("camera-resize-before.png")); ok &= actual.save(folder.filePath("camera-resize-preview.png"));
		ok &= expected.save(folder.filePath("camera-resize-reference.png")); ok &= capture(view).save(folder.filePath("camera-resize-rtl.png"));
		ok &= pointActual.save(folder.filePath("camera-resize-point.png"));
	}
	view.finishSelectionResize(false); app.removeTranslator(&translator); view.clearMesh();
	ok &= expect(!view.isResizingSelection() && !std::isfinite(view.selectionResizeHandles()[1].x()),"closed document releases resize state");
	std::cout << QJsonDocument(QJsonObject{{"ok",ok},{"profiles",profiles},{"previewChannelDifference",delta},{"pointChannelDifference",pointDelta},{"commitRequests",requests}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
