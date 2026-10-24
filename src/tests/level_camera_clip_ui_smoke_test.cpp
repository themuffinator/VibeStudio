#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_clip_camera.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& detail = {}) { if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; }
bool until(const std::function<bool()>& ready) {
	QElapsedTimer elapsed; elapsed.start(); while (!ready() && elapsed.elapsed() < 20000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); } return ready();
}
bool settle(ModelViewport* view) { return until([&] { QImage image(view->size(),QImage::Format_ARGB32_Premultiplied); view->render(&image); return view->isEnabled() && !view->isRendering(); }); }
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
bool near(LevelMapVec3 a,LevelMapVec3 b) { return a.valid && b.valid && std::abs(a.x-b.x) < .001 && std::abs(a.y-b.y) < .001 && std::abs(a.z-b.z) < .001; }
}
int main(int argc,char** argv)
{
	// Owned offscreen surfaces, direct semantic methods, no injected input.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; } QString error;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setSelectedEditorProfileId("netradiant-custom"); settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	LevelMapDocument fixture; tests::createGeometryFixture(1,&fixture,&error); const auto path = temp.filePath("source.map"); saveLevelMapAs(fixture,path);
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* id) { auto* action = shell.findChild<QAction*>(QString::fromLatin1(id)); if (expect(action && action->isEnabled(),id)) { action->trigger(); } };
	trigger("shell.mode.levels"); auto* top = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(top && camera && until([&] { return camera->hasMesh() && camera->isEnabled(); }),"map views ready")) { return 1; }
	trigger("map.selectAll"); shell.findChild<QCheckBox*>("levelMapSnap")->setChecked(false);
	const auto original = bytes(shell.levelDocument());
	auto controls = netRadiantCustomLevelControls().camera; camera->setCameraControls(controls); camera->setCameraView({-128,0,64},0,-20);
	trigger("map.clipTool"); if (!expect(settle(camera),"camera ready")) { return 1; }
	const auto place = [&](LevelMapVec3 point) {
		const auto position = camera->componentGizmoFrame().project(point);
		const bool accepted = camera->beginClipPoint(position); camera->finishClipPoint(true);
		return expect(accepted,"place actual brush-face clip point");
	};
	// On the front face, a camera-derived extrusion cuts horizontally instead
	// of following the vertical extrusion onto the existing outer face.
	place({-16,-8,0,true}); place({-16,8,0,true});
	if (!expect(camera->clipState().points.size() == 2 && camera->clipState().axis == 0 && camera->clipState().direction == -1,"NRC camera default uses viewing axis")) { return 1; }
	const auto state = camera->clipState();
	expect(top->clipState() == state,"plan and camera share exact extrusion and points");
	auto expected = shell.levelDocument(); expect(clipLevelMapPoints(&expected,state.points,state.axis,state.direction,state.keep,{},nullptr,&error),"shared core camera cut",error);
	camera->setCameraView({-128,-96,64},35,-20); expect(camera->clipState() == state && top->clipState() == state,"camera navigation never rotates an established plane");
	camera->requestClip(); expect(until([&] { return shell.levelDocument().undoStack.size() == 1; }),"publish checked camera cut");
	expect(bytes(shell.levelDocument()) == bytes(expected),"camera GUI and shared core cut agree exactly");
	trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"camera plane exact undo");
	camera->setCameraView({-128,0,64},0,-20); camera->setClipState({}); if (!expect(settle(camera),"camera settles after undo")) { return 1; }
	place({0,-8,16,true}); place({0,8,16,true});
	expect(camera->clipState().axis == 2 && camera->clipState().direction == 1,"outer top face redirects extrusion through selection");
	const auto corrected = camera->clipState(); expect(until([&] { return camera->clipPreview().status == LevelClipPreview::Status::Ready; }),"corrected outer-face cut is checked");
	// Existing-point cancellation restores both its location and any repaired axis.
	expect(camera->beginClipPoint(camera->clipPointPosition(0)),"grab existing clip point");
	camera->moveClipPoint(camera->componentGizmoFrame().project({-16,-8,0,true})); camera->finishClipPoint(false);
	expect(camera->clipState() == corrected && top->clipState() == corrected,"cancel restores the entire shared plane");
	expect(camera->beginClipPoint(camera->clipPointPosition(0)),"grab before policy change");
	camera->moveClipPoint(camera->componentGizmoFrame().project({-16,-8,0,true}));
	controls.clipViewAxis = false; camera->setCameraControls(controls,true);
	expect(camera->clipState() == corrected,"profile/gesture change cancels uncommitted point movement");
	camera->setClipState({}); expect(settle(camera),"legacy policy ready");
	place({-16,-8,0,true}); place({-16,8,0,true});
	expect(camera->clipState().axis == 2 && camera->clipState().direction == 1,"explicit policy off retains original vertical-plane behaviour");
	// Three real points are independent of the two-point camera policy.
	LevelClipState arbitrary; arbitrary.points = {{0,0,0,true},{16,0,8,true},{0,16,0,true}}; arbitrary.axis = 1; arbitrary.direction = -1;
	camera->setClipState(arbitrary); controls.clipViewAxis = true; camera->setCameraControls(controls,true);
	expect(camera->clipState() == arbitrary && top->clipState() == arbitrary,"three-point world plane survives policy changes");
	// Semantic point movement never injects mouse/key events. Fixed press
	// anchors, modifiers and cancellation feed the same shared clip state.
	LevelClipState dragBase; dragBase.points = {{-16,-12,3,true},{-16,12,3,true}}; dragBase.axis = 0; dragBase.direction = -1;
	controls = netRadiantCustomLevelControls().camera; camera->setCameraControls(controls,true); camera->setMoveGrid(8); camera->setClipState(dragBase);
	expect(settle(camera),"world point drag ready");
	const auto horizontal = camera->componentGizmoFrame().project({-6,6,3,true});
	expect(camera->beginClipPoint(camera->clipPointPosition(0)) && camera->moveClipPoint(horizontal,Qt::ShiftModifier),"Shift moves point without scene snapping");
	expect(near(camera->clipState().points[0],{-16,4,3,true}) && top->clipState() == camera->clipState(),"Shift uses dominant XY delta and relative grid across views");
	expect(camera->accessibleSummary().contains("Clip point 1"),"movement mode is available to accessibility and HUD");
	CameraSurfacePoint sampled;
	expect(camera->surfacePointAt(horizontal,&sampled),"surface oracle at the same pointer");
	expect(camera->updateClipPointModifiers(Qt::Key_Control,Qt::ShiftModifier,true),"live Ctrl press is handled");
	expect(near(camera->clipState().points[0],{sampled.position[0],sampled.position[1],sampled.position[2],true}),"Ctrl takes precedence and returns to brush surface picking");
	expect(camera->updateClipPointModifiers(Qt::Key_Control,Qt::ControlModifier|Qt::ShiftModifier,false)
		&& near(camera->clipState().points[0],{-16,4,3,true}),"Ctrl release reuses fixed press anchor");
	camera->finishClipPoint(false); expect(camera->clipState() == dragBase && top->clipState() == dragBase,"modified movement cancellation restores exact shared state");
	const auto vertical = camera->componentGizmoFrame().project({-16,-12,29,true});
	expect(camera->beginClipPoint(camera->clipPointPosition(0)) && camera->moveClipPoint(vertical,Qt::AltModifier|Qt::ShiftModifier),"Alt moves point beyond the source face");
	expect(near(camera->clipState().points[0],{-16,-12,27,true}),"Alt constrains Z and retains off-grid offset");
	expect(bytes(shell.levelDocument()) == original && shell.levelDocument().undoStack.isEmpty(),"point movement stages no document changes");
	camera->finishClipPoint(true); const auto moved = camera->clipState(); auto draggedResult = shell.levelDocument();
	expect(clipLevelMapPoints(&draggedResult,moved.points,moved.axis,moved.direction,moved.keep,{},nullptr,&error),"core oracle for freely moved camera plane",error);
	camera->requestClip(); expect(until([&] { return shell.levelDocument().undoStack.size() == 1; }),"freely moved point applies one checked cut");
	expect(bytes(shell.levelDocument()) == bytes(draggedResult),"modified camera cut exactly matches shared service");
	trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"modified camera cut exact undo");
	controls.clipDragStyle = CameraClipDragStyle::ViewAxes; camera->setCameraControls(controls,true); camera->setMoveGrid(0); camera->setClipState(dragBase);
	expect(settle(camera),"view-relative point drag ready");
	expect(camera->beginClipPoint(camera->clipPointPosition(0))
		&& camera->moveClipPoint(camera->componentGizmoFrame().project({-16,10,13,true}),Qt::ShiftModifier)
		&& near(camera->clipState().points[0],{-16,10,3,true}),"view-relative Shift works in dominant-axis YZ plane");
	expect(camera->moveClipPoint(camera->componentGizmoFrame().project({16,-12,3,true}),Qt::AltModifier)
		&& near(camera->clipState().points[0],{16,-12,3,true}),"view-relative Alt moves along camera-dominant X axis");
	controls.clipDragStyle = CameraClipDragStyle::Surface; camera->setCameraControls(controls,true);
	expect(camera->clipState() == dragBase,"changing movement style cancels point transaction");
	expect(settle(camera) && camera->beginClipPoint(camera->clipPointPosition(0)) && camera->moveClipPoint(horizontal,Qt::AltModifier),"surface-only policy preserves legacy movement");
	expect(near(camera->clipState().points[0],{sampled.position[0],sampled.position[1],sampled.position[2],true}),"disabled free movement still samples the surface with modifiers");
	camera->finishClipPoint(false);
	controls.clipDragStyle = CameraClipDragStyle::WorldAxes; camera->setCameraControls(controls,true); camera->setMoveGrid(8);
	expect(settle(camera) && camera->beginClipPoint(camera->clipPointPosition(0)) && camera->moveClipPoint(vertical,Qt::AltModifier),"begin movement before navigation");
	camera->setCameraView({-128,0,64},0,-20);
	expect(camera->clipState() == dragBase && !camera->moveClipPoint(vertical,Qt::AltModifier),"camera change cancels fixed-anchor movement");
	expect(settle(camera) && camera->beginClipPoint(camera->clipPointPosition(0)) && camera->moveClipPoint(vertical,Qt::AltModifier),"begin movement before external state change");
	camera->setClipState(arbitrary);
	expect(!camera->moveClipPoint(vertical,Qt::AltModifier) && camera->clipState() == arbitrary,"external plane replaces and retires old movement");
	camera->setClipState({}); expect(settle(camera),"new point movement ready");
	expect(camera->beginClipPoint(camera->componentGizmoFrame().project({-16,-12,3,true}))
		&& camera->moveClipPoint(vertical,Qt::AltModifier) && camera->clipState().points.size() == 1,"modified initial drag moves its one actual point");
	camera->finishClipPoint(false); expect(camera->clipState().points.isEmpty(),"cancel removes the newly placed draft point");
	// A camera parallel to the movement plane must retain its last accepted
	// points and present a diagnostic, without falling back to arbitrary depth.
	auto levelBase = dragBase; levelBase.points[0].z = levelBase.points[1].z = 0;
	camera->setCameraView({-128,0,0},0,0); camera->setClipState(levelBase); expect(settle(camera),"parallel-plane movement ready");
	expect(camera->beginClipPoint(camera->clipPointPosition(0))
		&& !camera->moveClipPoint(camera->componentGizmoFrame().project({-16,-4,0,true}),Qt::ShiftModifier)
		&& camera->clipState() == levelBase,"unresolvable horizontal movement retains exact plane");
	expect(camera->accessibleSummary().contains("cannot resolve"),"unresolvable movement explains the next action");
	camera->finishClipPoint(false); camera->setCameraView({-128,0,64},0,-20); expect(settle(camera),"restore camera after refused movement");
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
	if (!evidence.isEmpty()) {
		camera->setClipState(corrected); expect(until([&] { return camera->clipPreview().status == LevelClipPreview::Status::Ready; }),"capture checked corrected cut");
		for (int scale : {100,200}) {
			applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,scale)); camera->setHighContrast(true);
			camera->setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight); expect(settle(camera),"owned camera render settles");
			QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied); camera->render(&image);
			expect(image.save(QDir(evidence).filePath(QString("camera-clip-plane-%1.png").arg(scale))),"save owned camera rendering");
			camera->setClipState(dragBase);
			expect(camera->beginClipPoint(camera->clipPointPosition(0))
				&& camera->moveClipPoint(camera->componentGizmoFrame().project({-16,-12,29,true}),Qt::AltModifier),"render active modified point movement");
			expect(until([&] { return camera->clipPreview().status == LevelClipPreview::Status::Ready; }),"modified point preview settles");
			camera->render(&image);
			expect(image.save(QDir(evidence).filePath(QString("camera-clip-drag-%1.png").arg(scale))),"save owned movement rendering");
			camera->finishClipPoint(false); camera->setClipState(corrected);
		}
	}
	expect(bytes(shell.levelDocument()) == original && shell.levelDocument().undoStack.isEmpty(),"all cancelled and preview-only operations retain source");
	return ok ? 0 : 1;
}
