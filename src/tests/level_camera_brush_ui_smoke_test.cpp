#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; }
	return value;
}
QImage capture(ModelViewport& view)
{
	QImage image(view.size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); view.render(&image); return image;
}
bool settle(ModelViewport& view)
{
	QElapsedTimer elapsed; elapsed.start();
	do {
		capture(view);
		QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
		if (!view.isRendering()) { return true; }
	} while (elapsed.elapsed() < 30000);
	return false;
}
// Independent projection for a known empty construction scene centred at zero.
QPointF screen(const ModelViewport& view, BoxResizePoint point)
{
	const double yaw = (view.isPerspective() ? view.cameraYaw() : view.yaw())*std::numbers::pi/180;
	const double pitch = (view.isPerspective() ? view.cameraPitch() : view.pitch())*std::numbers::pi/180;
	const BoxResizePoint forward{std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch)};
	const BoxResizePoint right = view.isPerspective() ? BoxResizePoint{std::sin(yaw),-std::cos(yaw),0} : BoxResizePoint{-std::sin(yaw),std::cos(yaw),0};
	const BoxResizePoint up{-std::sin(pitch)*std::cos(yaw),-std::sin(pitch)*std::sin(yaw),std::cos(pitch)};
	if (view.isPerspective()) {
		const auto eye = view.cameraPosition(); point[0] -= eye.x; point[1] -= eye.y; point[2] -= eye.z;
	}
	const auto dot = [&](BoxResizePoint direction) { return point[0]*direction[0]+point[1]*direction[1]+point[2]*direction[2]; };
	const double scale = view.isPerspective() ? view.height()*0.5/std::tan(view.fieldOfView()*std::numbers::pi/360)/dot(forward) : view.zoom();
	return {view.width()*0.5+dot(right)*scale,view.height()*0.5-dot(up)*scale};
}
void setup(ModelViewport& view, const LevelEditorControls& controls)
{
	view.resize(1000,720);
	ModelMesh empty; empty.mins = {-128,-128,-128}; empty.maxs = {128,128,128};
	view.setMesh(empty); view.setCameraControls(controls.camera); view.setShowGrid(false); view.setShowAxes(false); view.setMoveGrid(16);
	if (view.isPerspective()) { view.setCameraView({-220,-280,240},50,-32); }
	else { view.setOrbit(45,30); }
	view.setBrushDrawTool(true);
}
bool draw(ModelViewport& view, int axis, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
	BoxResizePoint from{},to{}; from[axis] = to[axis] = view.brushDrawBase();
	from[(axis+1)%3] = -32; from[(axis+2)%3] = -16;
	to[(axis+1)%3] = 48; to[(axis+2)%3] = 80;
	return view.beginBrushDraw(screen(view,from),modifiers) && view.updateBrushDraw(screen(view,to),modifiers);
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
QJsonObject latency(int count)
{
	LevelMapDocument source; QString error; bool ok = tests::createGeometryFixture(count,&source,&error);
	const auto mesh = buildLevelMapPreviewMesh(source);
	ModelViewport view; setup(view,trenchBroomLevelControls()); view.setMesh(mesh.mesh);
	view.setCameraView({-220,-280,240},50,-32); view.setBrushDrawTool(true); view.setBrushDrawPlane(2,0,64);
	ok &= settle(view); const QPointF start(view.width()*0.4,view.height()*0.6),end(view.width()*0.6,view.height()*0.7);
	ok &= view.beginBrushDraw(start);
	QVector<double> calls; int completed = 0;
	QObject::connect(&view,&ModelViewport::renderCompleted,&view,[&] { ++completed; });
	for (int i = 0; i < 64; ++i) {
		QElapsedTimer timer; timer.start(); ok &= view.updateBrushDraw(end+QPointF(i%8,0)); capture(view);
		calls.append(timer.nsecsElapsed()/1e6); QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
	}
	std::sort(calls.begin(),calls.end());
	ok &= expect(mesh.triangles == count*12 && completed == 0 && view.isDrawingBrush() && source.undoStack.isEmpty(),
		"construction proposals do not rebuild scene geometry or edit the source");
	const int budget = qEnvironmentVariableIntValue("VIBESTUDIO_LEVEL_BRUSH_MAX_CALL_MS");
	if (budget > 0) { ok &= expect(calls.last() <= budget,"configured camera brush GUI-call latency budget"); }
	view.finishBrushDraw(false);
	return {{"ok",ok},{"brushes",count},{"triangles",mesh.triangles},{"proposals",calls.size()},
		{"medianCallMs",calls[calls.size()/2]},{"maxCallMs",calls.last()},{"sceneRebuilds",completed}};
}
}
int main(int argc, char** argv)
{
	// Semantic widget calls and QWidget render targets; no input injection or
	// OS capture, game processes, user settings or commercial assets.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	if (argc > 1 && QByteArray(argv[1]) == "--latency") {
		QJsonArray results; bool ok = true;
		for (int count : {1000,10000}) { const auto result = latency(count); results.append(result); ok &= result.value("ok").toBool(); }
		std::cout << QJsonDocument(results).toJson(QJsonDocument::Compact).constData() << '\n'; return ok ? 0 : 1;
	}
	ModelViewport view; bool ok = true; int cases = 0,requests = 0;
	ResizeBox committed;
	QObject::connect(&view,&ModelViewport::brushDrawRequested,&app,[&](const auto& box) { ++requests; committed = box; });
	for (const auto& id : editorProfileIds()) {
		const auto controls = levelEditorControlsForProfile(id); setup(view,controls);
		for (int axis = 0; axis < 3; ++axis) {
			ok &= view.setBrushDrawPlane(axis,8,64,controls.plan.squareModifiers,controls.plan.cubeModifiers);
			if (!expect(draw(view,axis),"every profile draws on each construction plane")) { std::cerr << id.toStdString() << ':' << axis << '\n'; return 1; }
			const auto box = view.brushDrawBox();
			ok &= expect(box.mins[axis] == 8 && box.maxs[axis] == 72 && box.maxs[(axis+1)%3]-box.mins[(axis+1)%3] == 80
				&& box.maxs[(axis+2)%3]-box.mins[(axis+2)%3] == 96,"independently projected points recover exact snapped bounds");
			ok &= expect(view.adjustBrushDrawDepth(1) && view.brushDrawBox().maxs[axis] == 96,"wheel adjusts the hidden axis on the world grid");
			const auto expected = view.brushDrawBox(); view.finishBrushDraw(true);
			ok &= expect(committed == expected && requests == cases+1,"one complete brush request per release"); ++cases;
			if (controls.plan.cubeModifiers != Qt::NoModifier) {
				ok &= draw(view,axis,controls.plan.cubeModifiers);
				const auto cube = view.brushDrawBox();
				for (int a = 0; a < 3; ++a) { ok &= expect(cube.maxs[a]-cube.mins[a] == 96,"profile cube modifier constrains every axis"); }
				view.finishBrushDraw(false);
			}
		}
	}
	setup(view,trenchBroomLevelControls()); view.setBrushDrawPlane(2,0,64);
	const auto initial = capture(view);
	ok &= draw(view,2); const auto draft = capture(view);
	ok &= expect(initial != draft && view.accessibleDescription().contains("80 × 96 × 64"),"visible and accessible draft dimensions");
	const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!output.isEmpty()) { QDir().mkpath(output); ok &= draft.save(QDir(output).filePath("camera-brush-draft.png")); }
	view.finishBrushDraw(false); ok &= expect(capture(view) == initial,"cancel restores the construction picture exactly");
	ok &= draw(view,2); view.setMoveGrid(8); ok &= expect(!view.isDrawingBrush(),"grid changes cancel");
	ok &= draw(view,2); view.setCameraView({-240,-280,240},50,-32); ok &= expect(!view.isDrawingBrush(),"camera changes cancel");
	ok &= draw(view,2); view.setEnabled(false); ok &= expect(!view.isDrawingBrush(),"disabled view cancels"); view.setEnabled(true);
	ok &= draw(view,2); view.setSurfaceTool(ModelViewportSurfaceTool::Paint); ok &= expect(!view.isDrawingBrush() && !view.brushDrawTool(),"painting replaces brush mode");
	view.setBrushDrawTool(true); ok &= expect(view.surfaceTool() == ModelViewportSurfaceTool::None,"drawing replaces painting");
	ok &= draw(view,2); ok &= expect(!view.updateBrushDraw({std::numeric_limits<double>::quiet_NaN(),0}),"invalid projection rejected");
	view.finishBrushDraw(true); ok &= expect(requests == cases,"cancelled or invalid drafts never commit");
	Expanded expanded; app.installTranslator(&expanded);
	applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,200));
	view.setHighContrast(true); view.setLayoutDirection(Qt::RightToLeft); view.resize(760,650);
	ok &= settle(view);
	ok &= draw(view,2);
	if (!output.isEmpty()) { ok &= capture(view).save(QDir(output).filePath("camera-brush-expanded-rtl.png")); }
	ok &= expect(view.isDrawingBrush() && view.brushDrawValid(),"enlarged RTL render retains the live draft");
	view.finishBrushDraw(false); app.removeTranslator(&expanded);
	std::cout << QJsonDocument(QJsonObject{{"ok",ok},{"profiles",editorProfileIds().size()},{"planeCases",cases},{"requests",requests}}).toJson(QJsonDocument::Compact).constData() << '\n';
	return ok ? 0 : 1;
}
