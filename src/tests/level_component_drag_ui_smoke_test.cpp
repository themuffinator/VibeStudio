#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_editor_controls.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& error = {}) { if (!value) { ok = false; std::cerr << label << ": " << error.toStdString() << '\n'; } return value; }
bool near(double a,double b) { return std::abs(a-b) < .005; }
bool near(LevelMapVec3 p,LevelMapVec3 q) { return near(p.x,q.x) && near(p.y,q.y) && near(p.z,q.z); }
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 20000) { QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
QByteArray bytes(const LevelMapDocument& document) { return serializeLevelMap(document).bytes; }
LevelMapDocument fixture() {
	LevelMapDocument map; QString error; expect(tests::createGeometryFixture(1,&map,&error),"fixture",error);
	// All corners deliberately miss the grid, including the hidden coordinate.
	for (auto& brush : map.brushes) {
		for (auto& face : brush.faces) { for (auto* p : {&face.p0,&face.p1,&face.p2}) { p->x += 1; p->y += 2; p->z += 3; } }
		brush.mins = {-15,-14,-13,true}; brush.maxs = {17,18,19,true}; brush.geometryDirty = true;
	}
	return map;
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override { const auto text = QString::fromUtf8(source); return '['+text+QString(text.size()/3,'~')+']'; }
};
void planChecks(const LevelMapDocument& map) {
	for (auto projection : {MapViewportProjection::TopXY,MapViewportProjection::FrontXZ,MapViewportProjection::SideZY}) {
		MapViewport view; view.resize(1000,800); view.setDocument(map); view.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,0}});
		view.setProjection(projection); view.setControls(trenchBroomLevelControls().plan); view.setGridSize(8); view.setSnapToGrid(true);
		view.setComponentMode(MapViewport::ComponentMode::Vertices); view.pickAllComponents(); view.zoomToSelection();
		const auto frame = view.componentGizmoFrame(); const LevelMapVec3 p{17,18,19,true}; auto q = p;
		view.vertexInsertion()->setContext(view.vertexInsertionData(),true,false,8);
		const auto edgePoint = frame.project(projection == MapViewportProjection::TopXY ? LevelMapVec3{17,2,19,true} : LevelMapVec3{17,18,3,true});
		const auto insertionKeys = Qt::ShiftModifier|Qt::ControlModifier|Qt::AltModifier;
		if (expect(view.beginVertexInsertion(edgePoint,insertionKeys),"combined insertion chord in plan")) {
			const auto start = view.vertexInsertion()->candidate().position; auto target = start;
			if (projection == MapViewportProjection::SideZY) { target.y += 24; } else { target.x += 24; }
			expect(view.vertexInsertion()->updateDrag(frame.project(target),insertionKeys),"combined insertion moves on visible plane");
			const auto result = view.vertexInsertion()->position();
			expect(near(result.x/8,std::round(result.x/8)) && near(result.y/8,std::round(result.y/8)) && near(result.z/8,std::round(result.z/8)),"insertion absolute snapping includes all axes");
			expect(view.vertexInsertion()->summary().contains("Absolute grid") && view.vertexInsertion()->summary().contains("plane"),"insertion modifier state exposed");
			view.vertexInsertion()->finish(false);
		}
		expect(!view.beginVertexInsertion(frame.project(p),insertionKeys),"existing vertex keeps priority for combined insertion chord");
		if (projection == MapViewportProjection::TopXY) { q.x += 10; q.y += 18; }
		else if (projection == MapViewportProjection::FrontXZ) { q.x += 10; q.z += 18; }
		else { q.y += 10; q.z += 18; }
		const auto press = frame.project(p),end = frame.project(q); int commits = 0;
		QObject::connect(&view,&MapViewport::componentMoveRequested,&view,[&] { ++commits; });
		expect(view.armComponentMove(press,Qt::ControlModifier) && view.pickedComponentCount() == 8 && !view.isMovingComponents(),"Ctrl press defers selection");
		view.finishComponentMove(true); expect(view.pickedComponentCount() == 6 && commits == 0,"Ctrl click toggles projected pair without moving");
		view.pickAllComponents(); expect(view.armComponentMove(press,Qt::ControlModifier) && view.updateComponentMove(end,Qt::ControlModifier),"Ctrl drag starts absolute move");
		expect(view.pickedComponentCount() == 8 && view.componentPreviewPending(),"drag preserves the existing selected set");
		const auto absolute = view.componentMoveDelta();
		const LevelMapVec3 wanted = projection == MapViewportProjection::TopXY ? LevelMapVec3{7,22,-3,true}
			: projection == MapViewportProjection::FrontXZ ? LevelMapVec3{7,-2,21,true} : LevelMapVec3{-1,14,21,true};
		expect(near(absolute,wanted),"absolute grid includes hidden coordinate");
		view.setComponentPreview(absolute,{}); expect(view.updateComponentModifiers(Qt::Key_Control,Qt::ControlModifier,false),"stationary modifier release handled");
		const auto relative = view.componentMoveDelta();
		// Absolute snapping changed the plane's hidden coordinate; releasing Ctrl
		// keeps that plane, then relative snapping restores its off-grid offset.
		expect(view.accessibleDescription().contains("Relative grid") && !near(relative,absolute),"live Ctrl release updates preview and accessible mode");
		view.setComponentPreview(relative,{}); view.finishComponentMove(true); expect(commits == 1,"checked move publishes once");
		expect(view.armComponentMove(press,Qt::AltModifier) && view.updateComponentMove(end,Qt::AltModifier),"Alt drag available in plan");
		expect(view.accessibleDescription().contains("plane") && view.componentMoveDelta().valid,"2D Alt stays on projection plane");
		view.finishComponentMove(true); view.cancelInteraction(); view.setComponentPreview(view.componentMoveDelta(),{});
		expect(!view.isMovingComponents() && commits == 1,"cancel pending release prevents late commit");
		expect(view.armComponentMove(press,Qt::NoModifier),"arm before profile change"); view.setControls(netRadiantCustomLevelControls().plan);
		expect(!view.componentMoveArmed() && !view.armComponentMove(press,Qt::ControlModifier),"profile change cancels and disables native chords");
		view.setControls(trenchBroomLevelControls().plan);
		for (auto mode : {MapViewport::ComponentMode::Edges,MapViewport::ComponentMode::Faces}) {
			view.setComponentMode(mode); view.pickAllComponents(); const auto handle = view.componentHandlePosition(0);
			expect(!view.armComponentMove(handle,Qt::ControlModifier),"edge/face Ctrl remains selection");
			expect(view.armComponentMove(handle,Qt::NoModifier) && view.updateComponentMove(handle+QPointF(80,-40),Qt::ControlModifier),"Ctrl can change during edge/face move");
			expect(view.accessibleDescription().contains("Relative grid"),"edges/faces retain relative grid under Ctrl"); view.finishComponentMove(false);
		}
	}
}
void shellChecks(QApplication& app,const QString& directory,const QString& evidence) {
	const auto map = fixture(); const auto path = QDir(directory).filePath("off-grid.map"); QFile file(path);
	expect(file.open(QIODevice::WriteOnly) && file.write(bytes(map)) > 0,"write fixture"); file.close();
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* name) { auto* action = shell.findChild<QAction*>(QString::fromLatin1(name)); if (expect(action && action->isEnabled(),name)) { action->trigger(); } };
	trigger("shell.mode.levels");
	auto* top = shell.findChild<MapViewport*>("mapViewport"); auto* front = shell.findChild<MapViewport*>("mapViewport1");
	auto* side = shell.findChild<MapViewport*>("mapViewport2"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(top && front && side && camera && until([&] { return shell.levelDocument().brushes.size() == 1 && camera->hasMesh() && camera->isEnabled(); }),"shell ready")) { return; }
	trigger("map.selectAll"); trigger("map.grid8"); trigger("map.vertexTool");
	const auto original = bytes(shell.levelDocument()); StudioSettings settings;
	const auto ready = [&] {
		QElapsedTimer stable; stable.start();
		return until([&] {
			QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied); camera->render(&image);
			const bool settled = top->isEnabled() && front->isEnabled() && side->isEnabled() && camera->isEnabled() && !camera->isRendering();
			if (!settled) { stable.restart(); } return settled && stable.elapsed() >= 50;
		});
	};
	const auto capture = [&](QWidget* widget,const QString& name) {
		if (evidence.isEmpty()) { return; } app.processEvents(QEventLoop::ExcludeUserInputEvents);
		QImage image(widget->size(),QImage::Format_ARGB32_Premultiplied); widget->render(&image); expect(image.save(QDir(evidence).filePath(name)),"owned widget render",name);
	};
	const auto oracle = [&](const QVector<LevelMapComponentSet>& sets,LevelMapVec3 delta) {
		auto expected = shell.levelDocument(); QString error; LevelComponentTransformRequest request;
		request.delta = delta; request.allowCollapse = true; request.textureLock = settings.levelComponentUvLock(); request.allowValve220 = settings.levelAllowValve220();
		expect(transformLevelMapComponents(&expected,sets,request,nullptr,&error),"core movement oracle",error); return expected;
	};
	for (auto* view : {top,front,side}) {
		if (!expect(ready(),"settle before plan movement")) { return; }
		view->setControls(trenchBroomLevelControls().plan); view->selectComponentHandles(LevelComponentPick::Replace); view->zoomToSelection();
		const auto press = view->componentHandlePosition(*view->pickedComponentHandles().cbegin());
		// An arbitrary pointer displacement exercises actual view rays and grid.
		if (!expect(view->armComponentMove(press,Qt::ControlModifier) && view->updateComponentMove(press+QPointF(80,-40),Qt::ControlModifier),"shell plan absolute move")) { continue; }
		const auto expected = oracle(view->pickedComponents(),view->componentMoveDelta());
		expect(bytes(shell.levelDocument()) == original,"pending move leaves document unchanged");
		expect(until([&] { return !view->componentPreviewPending(); }),"plan geometry validated");
		if (view == top) { capture(view,"component-drag-plan-100.png"); }
		view->finishComponentMove(true); expect(until([&] { return !view->isMovingComponents(); }),"plan release completes");
		expect(bytes(shell.levelDocument()) == bytes(expected) && shell.levelDocument().undoStack.size() == 1,"plan exactly matches core with one undo");
		trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"plan exact undo");
	}
	if (!expect(ready(),"settle camera")) { return; }
	camera->setCameraControls(trenchBroomLevelControls().camera); camera->setCameraView({-120,-160,110},50,-30); camera->setComponentXray(true);
	if (!expect(ready(),"perspective render")) { return; } camera->selectAllComponents();
	const auto frame = camera->componentGizmoFrame(); const LevelMapVec3 p{17,18,19,true}; const auto press = frame.project(p);
	expect(camera->armComponentMove(press,Qt::ControlModifier) && camera->pickedComponents().size() == 8,"camera Ctrl press defers"); camera->finishComponentMove(true);
	expect(camera->pickedComponents().size() == 7,"camera Ctrl click toggles only hit geometry"); camera->selectAllComponents();
	if (!expect(camera->armComponentMove(press,Qt::NoModifier) && camera->updateComponentMove(frame.project({81,42,19,true}),Qt::NoModifier),"camera horizontal drag")) { return; }
	if (!expect(until([&] { return !camera->componentPreviewPending(); }),"horizontal valid")) { return; }
	auto d = camera->componentMoveDelta(); expect(near(d.x,64) && near(d.y,24) && near(d.z,0) && camera->pickedComponents().size() == 8,"perspective XY movement retains multiselection",camera->accessibleDescription());
	expect(camera->updateComponentModifiers(Qt::Key_Shift,Qt::NoModifier,true),"stationary Shift activates constraint");
	expect(until([&] { return !camera->componentPreviewPending(); }) && camera->accessibleDescription().contains("X axis"),"accepted dominant axis exposed",camera->accessibleDescription());
	d = camera->componentMoveDelta(); expect(near(d.y,0) && near(d.z,0),"Shift projects to X axis");
	// Alt changes to vertical through the accepted position, then pointer motion
	// uses the closest world-Z line rather than the camera's tilted view plane.
	expect(camera->updateComponentModifiers(Qt::Key_Alt,Qt::ShiftModifier,true),"live Alt activation");
	expect(until([&] { return !camera->componentPreviewPending(); }),"vertical transition valid");
	d = camera->componentMoveDelta(); const auto target = LevelMapVec3{p.x+d.x,p.y+d.y,p.z+d.z+24,true};
	expect(camera->updateComponentMove(frame.project(target),Qt::AltModifier|Qt::ShiftModifier),"camera vertical update");
	expect(until([&] { return !camera->componentPreviewPending(); }),"vertical valid"); d = camera->componentMoveDelta();
	expect(near(p.x+d.x,target.x) && near(p.y+d.y,target.y) && near(p.z+d.z,target.z),"vertical movement preserves accepted XY");
	const auto expected = oracle(top->pickedComponents(),{d.x,d.y,d.z,true}); capture(camera,"component-drag-camera-100.png");
	applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,200)); Expansion expansion; app.installTranslator(&expansion);
	camera->setLayoutDirection(Qt::RightToLeft); camera->setHighContrast(true);
	// Theme changes may cancel the active preview. Render a fresh checked drag.
	camera->finishComponentMove(false); expect(ready(),"large text render settles"); camera->selectAllComponents();
	const auto large = camera->componentGizmoFrame(); const auto largePress = large.project(p);
	expect(camera->armComponentMove(largePress,Qt::ControlModifier) && camera->updateComponentMove(large.project({81,42,19,true}),Qt::ControlModifier),"large-text absolute drag");
	expect(until([&] { return !camera->componentPreviewPending(); }),"large-text valid");
	auto* accessible = QAccessible::queryAccessibleInterface(camera); expect(accessible && accessible->text(QAccessible::Description).contains("Absolute grid") && camera->focusPolicy() != Qt::NoFocus,"accessible mode and focus");
	capture(camera,"component-drag-camera-200.png"); camera->finishComponentMove(false); app.removeTranslator(&expansion);
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	expect(ready(),"normal theme settles"); camera->selectAllComponents();
	const auto finalFrame = camera->componentGizmoFrame(); expect(camera->armComponentMove(finalFrame.project(p),Qt::NoModifier)
		&& camera->updateComponentMove(finalFrame.project({p.x+d.x,p.y+d.y,p.z,true}),Qt::NoModifier),"rebuild horizontal segment");
	expect(until([&] { return !camera->componentPreviewPending(); }),"horizontal segment checked");
	expect(camera->updateComponentModifiers(Qt::Key_Alt,Qt::NoModifier,true),"vertical segment start");
	expect(until([&] { return !camera->componentPreviewPending(); }),"vertical segment anchor checked");
	expect(camera->updateComponentMove(finalFrame.project({p.x+d.x,p.y+d.y,p.z+d.z,true}),Qt::AltModifier),"rebuild final displacement");
	camera->finishComponentMove(true); expect(until([&] { return !camera->isMovingComponents(); }),"camera release waits for latest geometry");
	expect(bytes(shell.levelDocument()) == bytes(expected) && shell.levelDocument().undoStack.size() == 1,"camera equals core with one undo");
	trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"camera exact undo");
	expect(ready(),"undo settles"); camera->selectAllComponents();
	const auto cancelFrame = camera->componentGizmoFrame(); expect(camera->armComponentMove(cancelFrame.project(p),Qt::AltModifier),"arm camera cancellation"); camera->escapeComponents();
	expect(!camera->componentMoveArmed() && camera->pickedComponents().size() == 8,"Escape cancels armed gesture before deselecting");
	expect(camera->armComponentMove(cancelFrame.project(p),Qt::NoModifier),"arm profile cancellation"); camera->setCameraControls(netRadiantCustomLevelControls().camera,true);
	expect(!camera->componentMoveArmed() && !camera->isMovingComponents() && bytes(shell.levelDocument()) == original,"profile change cancels camera gesture");
}
}
int main(int argc,char** argv) {
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	planChecks(fixture()); shellChecks(app,temp.path(),qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE")); return ok ? 0 : 1;
}
