#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_editor_controls.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& error = {}) { if (!value) { ok = false; std::cerr << label << ": " << error.toStdString() << '\n'; } return value; }
bool near(double a,double b) { return std::abs(a-b) < .002; }
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 20000) { QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
QByteArray bytes(const LevelMapDocument& document) { return serializeLevelMap(document).bytes; }
LevelComponentGizmoFrame planFrame() {
	LevelComponentGizmoFrame frame; frame.viewDirection = {0,0,-1};
	frame.project = [](const LevelMapVec3& p) { return QPointF(400+2*p.x,300-2*p.y); };
	frame.ray = [](const QPointF& p) { return ModelPickRay{{float((p.x()-400)/2),float((300-p.y())/2),100},{0,0,-1},false}; };
	return frame;
}
std::shared_ptr<LevelVertexInsertionData> dataFor(const LevelMapDocument& document) {
	auto data = std::make_shared<LevelVertexInsertionData>();
	for (const auto& brush : document.brushes) { LevelBrushTopology topology; if (levelBrushTopology(brush,&topology)) { data->append({brush.id,topology}); } }
	return data;
}
void controllerChecks() {
	LevelMapDocument fixture; QString error; expect(tests::createGeometryFixture(1,&fixture,&error),"controller fixture",error);
	auto data = dataFor(fixture); data->append({1,data->first().topology});
	LevelVertexInsertion tool; tool.setContext(data,true,true,4); auto frame = planFrame();
	const auto point = frame.project({-16,8,16,true}),outside = frame.project({-32,8,16,true});
	auto hit = tool.pick(point,frame);
	expect(hit.position.valid && hit.kind == LevelBrushComponent::Edge && hit.brushes == QVector<int>{0,1} && near(hit.position.y,8) && near(hit.position.z,16),"arbitrary edge grid point and coincident brushes");
	hit = tool.pick(frame.project({4,8,16,true}),frame);
	expect(hit.position.valid && hit.kind == LevelBrushComponent::Face && near(hit.position.x,4) && near(hit.position.y,8),"arbitrary bounded face point");
	expect(!tool.pick(frame.project({80,0,16,true}),frame).position.valid && !tool.pick(frame.project({16,16,16,true}),frame).position.valid,"empty space and existing corner refused");
	frame.visible = [](const LevelMapVec3&) { return false; }; expect(!tool.pick(point,frame).position.valid,"camera visibility callback rejects hidden geometry"); frame.visible = {};
	int commits = 0,previews = 0;
	QObject::connect(&tool,&LevelVertexInsertion::commitRequested,&tool,[&](quint64) { ++commits; });
	QObject::connect(&tool,&LevelVertexInsertion::previewRequested,&tool,[&](quint64) { ++previews; });
	expect(tool.begin(point,frame) && tool.updateDrag(outside),"begin edge insertion");
	expect(tool.request().size() == 2 && near(tool.request()[0].positions[0].x,-32),"batch request from moved candidate");
	const auto serial = tool.serial(); tool.finish(true); expect(tool.active() && tool.releasePending() && commits == 0,"release waits");
	tool.setPreview(serial-1,{},{}); expect(tool.pending(),"stale completion ignored"); tool.setPreview(serial,{},{});
	expect(!tool.active() && commits == 1,"checked release commits exactly once"); tool.setPreview(serial,{},{}); expect(commits == 1,"duplicate ignored");
	expect(tool.begin(point,frame) && tool.updateDrag(outside),"cancel pending insertion"); const auto cancelled = tool.serial(); tool.finish(true); tool.finish(false); tool.setPreview(cancelled,{},{});
	expect(commits == 1 && !tool.active(),"cancel cannot later publish");
	expect(tool.begin(point,frame) && tool.updateDrag(outside),"invalid pointer draft"); const auto validSerial = tool.serial();
	expect(!tool.updateDrag({std::numeric_limits<double>::infinity(),0}),"invalid pointer rejected"); tool.setPreview(validSerial,{},{}); tool.finish(true); expect(commits == 1,"invalid pointer blocks earlier valid draft");
	expect(tool.begin(point,frame) && tool.updateDrag(outside) && tool.updateDrag(point),"return to origin"); tool.finish(true); expect(commits == 1 && !tool.pending(),"zero insertion no-op");
	expect(tool.begin(point,frame) && tool.updateDrag(outside),"refused hull draft"); tool.setPreview(tool.serial(),{},"Concave corner"); tool.finish(true); expect(commits == 1,"refused hull cannot publish");
	expect(tool.begin(point,frame) && tool.updateDrag(outside),"context change draft"); tool.setContext(data,true,true,8); expect(!tool.active() && commits == 1 && previews >= 5,"grid change cancels");
	// Sloped edges remain on their source line when snapping: the dominant
	// coordinate crosses the grid, the other coordinates follow the line.
	auto slope = std::make_shared<LevelVertexInsertionData>(*data);
	for (auto& brush : *slope) { for (auto& p : brush.topology.vertices) { p.x += p.z*.5; } }
	tool.setContext(slope,true,true,4); hit = tool.pick(frame.project({-8,8,16,true}),frame);
	expect(hit.position.valid && near(hit.position.x,-8) && near(hit.position.z,16),"sloped source geometry retained");
	tool.setContext(data,true,true,16); hit = tool.pick(frame.project({-12,0,16,true}),frame);
	expect(hit.position.valid && hit.kind == LevelBrushComponent::Edge && hit.brushes.size() == 2,"snapped boundary keeps edge incidence");
	tool.setContext(data,false,true,4); expect(!tool.pick(point,frame).position.valid,"unavailable or limited selection cannot insert");
	LevelMapDocument dense; tests::createGeometryFixture(128,&dense,&error); tool.setContext(dataFor(dense),true,true,4);
	QElapsedTimer timer; timer.start(); for (int i = 0; i < 20; ++i) { (void)tool.pick(point,frame); }
	std::cout << "128-brush cached insertion query: " << timer.elapsed()/20.0 << " ms per pick\n";
	expect(timer.elapsed() < 5000,"bounded dense picker");
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override { const auto text = QString::fromUtf8(source); return '['+text+QString(text.size()/3,'~')+']'; }
};
void shellChecks(QApplication& app,const QString& directory,const QString& evidence) {
	LevelMapDocument fixture; QString error; tests::createGeometryFixture(1,&fixture,&error);
	const auto path = QDir(directory).filePath("fixture.map"); QFile file(path); expect(file.open(QIODevice::WriteOnly) && file.write(bytes(fixture)) > 0,"shell fixture write"); file.close();
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* name) { auto* action = shell.findChild<QAction*>(QString::fromLatin1(name)); if (expect(action && action->isEnabled(),name)) { action->trigger(); } };
	trigger("shell.mode.levels");
	auto* top = shell.findChild<MapViewport*>("mapViewport"); auto* front = shell.findChild<MapViewport*>("mapViewport1"); auto* side = shell.findChild<MapViewport*>("mapViewport2"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(top && front && side && camera && until([&] { return shell.levelDocument().brushes.size() == 1 && camera->hasMesh() && camera->isEnabled(); }),"views ready")) { return; }
	trigger("map.selectAll"); trigger("map.grid4"); trigger("map.insertVertexTool");
	const auto original = bytes(shell.levelDocument());
	const auto ready = [&] {
		QElapsedTimer stable; stable.start();
		return until([&] {
			QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied); camera->render(&image);
			const bool settled = top->isEnabled() && front->isEnabled() && side->isEnabled() && camera->isEnabled() && !camera->isRendering();
			if (!settled) { stable.restart(); }
			return settled && stable.elapsed() >= 50;
		});
	};
	const auto capture = [&](QWidget* widget,const QString& name) {
		if (evidence.isEmpty()) { return; } app.processEvents(QEventLoop::ExcludeUserInputEvents);
		QImage image(widget->size(),QImage::Format_ARGB32_Premultiplied); widget->render(&image); expect(image.save(QDir(evidence).filePath(name)),"owned widget render",name);
	};
	for (auto* view : {top,front,side}) {
		if (!expect(ready(),"document preview settles before plan insertion")) { return; }
		view->zoomToSelection();
		const auto p = view == top ? LevelMapVec3{-16,8,16,true} : view == front ? LevelMapVec3{-16,16,8,true} : LevelMapVec3{16,-16,8,true};
		const auto q = view == top ? LevelMapVec3{-32,8,16,true} : view == front ? LevelMapVec3{-32,16,8,true} : LevelMapVec3{16,-32,8,true};
		const auto frame = view->componentGizmoFrame(); const auto start = frame.project(p),end = frame.project(q); auto* insertion = view->vertexInsertion();
		if (!expect(view->beginVertexInsertion(start) && insertion->updateDrag(end),"plan insertion",view->objectName())) { continue; }
		expect(bytes(shell.levelDocument()) == original,"pending preview does not mutate map");
		auto expected = shell.levelDocument(); const auto request = insertion->request();
		if (!expect(insertLevelMapVertices(&expected,request,0,false,nullptr,&error),"plan numeric oracle",error)) { insertion->finish(false); continue; }
		expect(until([&] { return !insertion->pending(); }),"plan validation");
		if (view == top) { capture(view,"insertion-plan-100.png"); }
		insertion->finish(true); expect(until([&] { return !insertion->active(); }),"plan release completes");
		expect(bytes(shell.levelDocument()) == bytes(expected) && shell.levelDocument().undoStack.size() == 1,"plan equals core, one undo",view->objectName());
		expect(top->pickedComponentCount() == 1 && camera->pickedComponents().size() == 1,"new vertex selected in every view");
		trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"plan exact undo");
	}
	// Named tool and TB shortcut share the same preview. Clicking an existing
	// vertex remains ordinary selection/movement, even with the insert chord.
	trigger("map.componentMoveTool"); top->setControls(trenchBroomLevelControls().plan); top->zoomToSelection();
	if (!expect(ready(),"document settles after undo")) { return; }
	auto frame = top->componentGizmoFrame(); auto press = frame.project({-16,8,16,true}),end = frame.project({-32,8,16,true});
	expect(!top->beginVertexInsertion(press) && top->beginVertexInsertion(press,Qt::ShiftModifier),"profile insertion chord"); top->vertexInsertion()->finish(false);
	expect(!top->beginVertexInsertion(frame.project({16,16,16,true}),Qt::ShiftModifier),"existing corner takes precedence");
	trigger("map.insertVertexTool");
	expect(top->beginVertexInsertion(press) && top->vertexInsertion()->updateDrag(end),"pending release cancellation"); top->vertexInsertion()->finish(true); top->cancelInteraction();
	expect(!top->vertexInsertion()->active() && bytes(shell.levelDocument()) == original,"cancelInteraction discards pending insertion");
	expect(top->beginVertexInsertion(press) && top->vertexInsertion()->updateDrag(end),"selection change cancellation");
	top->setPickedComponentHandles({0}); top->componentPicksChanged(1); top->vertexInsertion()->finish(true);
	expect(!top->vertexInsertion()->active() && bytes(shell.levelDocument()) == original,"selection change invalidates insertion");
	expect(top->beginVertexInsertion(press) && top->vertexInsertion()->updateDrag(frame.project({0,8,16,true})),"interior insertion preview"); top->vertexInsertion()->finish(true);
	expect(until([&] { return !top->vertexInsertion()->active(); }) && bytes(shell.levelDocument()) == original,"interior corner rejected atomically");
	// Render the actual editor controls at large text, high visibility and RTL.
	applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,200)); Expansion expansion; app.installTranslator(&expansion);
	top->setLayoutDirection(Qt::RightToLeft); top->setHighContrast(true); top->zoomToSelection(); frame = top->componentGizmoFrame();
	press = frame.project({-16,8,16,true}); end = frame.project({-32,8,16,true});
	expect(top->beginVertexInsertion(press) && top->vertexInsertion()->updateDrag(end) && until([&] { return !top->vertexInsertion()->pending(); }),"large text preview");
	auto* accessible = QAccessible::queryAccessibleInterface(top); expect(accessible && !accessible->text(QAccessible::Description).isEmpty() && top->focusPolicy() != Qt::NoFocus,"accessible insertion state");
	capture(top,"insertion-plan-200.png"); top->vertexInsertion()->finish(false); app.removeTranslator(&expansion);
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	camera->setCameraControls(trenchBroomLevelControls().camera); camera->setCameraView({-96,-120,88},50,-30); camera->setComponentXray(false);
	if (!expect(ready(),"camera renderer ready")) { return; }
	frame = camera->componentGizmoFrame(); press = frame.project({-16,4,4,true}); end = frame.project({-32,4,4,true}); auto* insertion = camera->vertexInsertion();
	if (expect(camera->beginVertexInsertion(press) && insertion->updateDrag(end),"camera face insertion")) {
		expect(insertion->candidate().kind == LevelBrushComponent::Face,"camera arbitrary face hit");
		auto expected = shell.levelDocument(); expect(insertLevelMapVertices(&expected,insertion->request(),0,false,nullptr,&error),"camera numeric oracle",error);
		expect(until([&] { return !insertion->pending(); }),"camera validated preview"); capture(camera,"insertion-camera.png");
		insertion->finish(true); expect(until([&] { return !insertion->active(); }) && bytes(shell.levelDocument()) == bytes(expected) && shell.levelDocument().undoStack.size() == 1,"camera equals core atomically");
		trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"camera undo exact");
	}
	if (expect(ready(),"camera settles after undo")) {
		frame = camera->componentGizmoFrame(); press = frame.project({-16,4,4,true}); end = press+QPointF(-30,0);
		const bool started = camera->beginVertexInsertion(press);
		expect(started && insertion->updateDrag(end),"camera pending cancel",QStringLiteral("mode %1 tool %2 enabled %3 rendering %4 point %5,%6 candidate %7 %8")
			.arg(camera->componentMode()).arg(insertion->tool()).arg(camera->isEnabled()).arg(camera->isRendering()).arg(press.x()).arg(press.y()).arg(insertion->candidate().position.valid).arg(insertion->summary()));
		insertion->finish(true); camera->escapeComponents();
		expect(!insertion->active() && bytes(shell.levelDocument()) == original,"camera Escape cancels pending insertion");
	}
	if (expect(ready(),"camera ready for Z constraint")) {
		frame = camera->componentGizmoFrame(); press = frame.project({-16,4,4,true});
		if (expect(camera->beginVertexInsertion(press,Qt::ShiftModifier|Qt::AltModifier),"TB shifted Z insertion")) {
			const auto start = insertion->candidate().position; auto target = start; target.z += 32;
			expect(insertion->updateDrag(frame.project(target)),"update Z insertion");
			const auto p = insertion->position(); expect(near(p.x,start.x) && near(p.y,start.y) && near(p.z,start.z+32),"Z constraint retains XY");
			insertion->finish(true); expect(until([&] { return !insertion->active(); }) && shell.levelDocument().undoStack.size() == 1,"Z insertion commits");
			trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"Z insertion undo");
		}
	}
}
}
int main(int argc,char** argv)
{
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	controllerChecks(); shellChecks(app,temp.path(),qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE")); return ok ? 0 : 1;
}
