#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_brush.h"
#include "core/level_patch.h"
#include "core/editor_profiles.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QStatusBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { std::cerr << "FAIL: " << message << ": " << detail.toStdString() << '\n'; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(20,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
bool settle(ModelViewport& view)
{
	return until([&] { QImage image(view.size(),QImage::Format_ARGB32_Premultiplied); view.render(&image); return view.isEnabled() && !view.isRendering(); });
}
bool writeMap(const QString& path, const LevelMapDocument& document)
{
	QFile file(path); const auto bytes = serializeLevelMap(document).bytes;
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool startMove(ModelViewport& view, int axis = 0)
{
	const auto handles = view.componentMoveHandles();
	return expect(view.beginComponentMove(handles[axis]),"begin constrained component move")
		&& expect(view.updateComponentMove(handles[axis]+(handles[axis]-handles[3])*0.8),"update constrained component move");
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* text, const char*, int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(text)); }
};
bool planSplitChecks(QApplication& app, const LevelMapDocument& map, const QString& output)
{
	bool ok = true;
	for (int scale : {100,200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,scale));
		MapViewport plan; plan.resize(1100,800); plan.setHighContrast(true); plan.setReducedMotion(true);
		plan.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		plan.setDocument(map); plan.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,0}});
		plan.setComponentMode(MapViewport::ComponentMode::Faces); plan.setComponentSplit(true); plan.setGridSize(8); plan.setSnapToGrid(true); plan.zoomToSelection();
		int right = -1,left = -1;
		for (int i = 0; i < plan.componentHandles().size(); ++i) {
			const auto p = plan.componentHandles()[i].position;
			if (std::abs(p.y) < 0.01 && std::abs(p.z) < 0.01) { if (p.x > 0) { right = i; } else { left = i; } }
		}
		if (right < 0 || left < 0) { return false; }
		const auto picked = plan.componentHandles()[right]; plan.setPickedComponentPositions({{picked.object,picked.position}});
		const auto start = plan.componentHandlePosition(right), step = (start-plan.componentHandlePosition(left))*0.5;
		int commits = 0; QObject::connect(&plan,&MapViewport::componentMoveRequested,&plan,[&] { ++commits; });
		ok &= expect(plan.beginComponentMove(start) && plan.updateComponentMove(start+step) && plan.componentPreviewPending(),"plan split exposes pending geometry");
		auto draft = map; QString error;
		ok &= splitLevelMapComponents(&draft,plan.pickedComponents(),plan.componentMoveDelta(),false,nullptr,&error);
		LevelBrushTopology topology; levelBrushTopology(draft.brushes[0],&topology);
		QVector<QPair<LevelMapVec3,LevelMapVec3>> edges;
		for (const auto& edge : topology.edges) { edges.append({topology.vertices[edge[0]],topology.vertices[edge[1]]}); }
		plan.setComponentPreview(plan.componentMoveDelta(),edges);
		ok &= expect(plan.accessibleDescription().contains(QStringLiteral("Valid preview")),"plan split exposes checked state");
		if (!output.isEmpty()) { QImage image(plan.size(),QImage::Format_ARGB32_Premultiplied); plan.render(&image); ok &= image.save(QDir(output).filePath(QStringLiteral("split-plan-%1.png").arg(scale))); }
		plan.finishComponentMove(true); ok &= expect(commits == 1,"checked plan split can commit");
		ok &= plan.beginComponentMove(start) && plan.updateComponentMove(start-step);
		draft = map; ok &= expect(!splitLevelMapComponents(&draft,plan.pickedComponents(),plan.componentMoveDelta(),false,nullptr,&error),"inward plan split refuses geometry");
		plan.setComponentPreview(plan.componentMoveDelta(),{},error); plan.finishComponentMove(true);
		ok &= expect(commits == 1 && !plan.isMovingComponents(),"refused plan preview cannot commit");
		ok &= plan.beginComponentMove(start) && plan.updateComponentMove(start+step); plan.finishComponentMove(true);
		plan.setEnabled(false); plan.setComponentPreview({16,0,0,true},edges);
		ok &= expect(commits == 1 && !plan.isMovingComponents(),"disabling plan cancels pending split release");
		plan.setEnabled(true); ok &= plan.beginComponentMove(start) && plan.updateComponentMove(start+step);
		plan.setGridSize(16); plan.finishComponentMove(true);
		ok &= expect(commits == 1 && !plan.isMovingComponents(),"grid change cancels plan split");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	return ok;
}

bool widgetChecks(QApplication& app, const LevelMapDocument& map, const QString& output)
{
	bool ok = true; LevelBrushTopology topology;
	if (!levelBrushTopology(map.brushes[0],&topology)) { return false; }
	QVector<ModelVec3> points;
	for (const auto& p : topology.vertices) { points.append({float(p.x),float(p.y),float(p.z)}); }
	const auto mesh = buildLevelMapPreviewMesh(map).mesh;
	int profileIndex = 0;
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("gtkradiant"),QStringLiteral("quark")}) {
		for (int scale : {100,200}) {
			Expanded translator;
			if (scale == 200) { app.installTranslator(&translator); }
			applyStudioTheme(app,studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::HighContrastLight,UiDensity::Standard,scale));
			ModelViewport view; view.resize(1100,800); view.setHighContrast(true); view.setReducedMotion(true);
			view.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			EditorProfileDescriptor descriptor;
			if (!editorProfileForId(profile,&descriptor)) { return false; }
			view.setMesh(mesh); view.setCameraControls(descriptor.controls.camera);
			view.setCameraView({-180,-220,160},50,-30); view.setMoveGrid(16); view.setComponentHandles(1,points,{});
			if (!expect(settle(view),"standalone component camera settles")) { return false; }
			int visible = 0,hidden = -1;
			for (int i = 0; i < points.size(); ++i) {
				if (view.componentAt(view.componentScreenPosition(i)) == i) { ++visible; } else { hidden = i; }
			}
			ok &= expect(visible > 0 && visible < points.size() && hidden >= 0,"depth-tested handles exclude rear corners");
			ok &= view.pickComponentBand(view.rect());
			ok &= expect(view.pickedComponents().size() == visible,"rectangle selection respects depth visibility");
			view.setComponentXray(true);
			ok &= expect(hidden >= 0 && view.pickComponentAt(view.componentScreenPosition(hidden)),"X-ray can select an occluded corner");
			ok &= view.pickComponentBand(view.rect());
			ok &= expect(view.pickedComponents().size() == 8,"X-ray rectangle reaches all corners");
			ok &= view.pickComponentBand(view.rect(),true);
			ok &= expect(view.pickedComponents().isEmpty(),"toggle rectangle removes the same corners");
			view.selectAllComponents();
			int requests = 0;
			QObject::connect(&view,&ModelViewport::componentMoveRequested,&view,[&](ModelVec3) { ++requests; });
			for (int axis = 0; axis < 3; ++axis) {
				if (!startMove(view,axis)) { return false; }
				const auto delta = view.componentMoveDelta(); const float coordinates[]{delta.x,delta.y,delta.z};
				ok &= expect(coordinates[axis] != 0 && std::fmod(coordinates[axis],16) == 0
					&& coordinates[(axis+1)%3] == 0 && coordinates[(axis+2)%3] == 0,"XYZ gizmo constrains and snaps world delta",
					QStringLiteral("profile=%1 scale=%2 axis=%3 delta=%4,%5,%6 pending=%7 active=%8").arg(profile).arg(scale).arg(axis).arg(delta.x).arg(delta.y).arg(delta.z).arg(view.componentPreviewPending()).arg(view.isMovingComponents()));
				view.finishComponentMove(true);
				ok &= expect(view.isMovingComponents() && view.componentPreviewPending() && requests == axis,"release waits for current geometry validation");
				view.setComponentPreview({delta.x+1,delta.y,delta.z},{},{});
				ok &= expect(view.componentPreviewPending(),"stale preview response is ignored");
				view.setComponentPreview(delta,{},{});
				ok &= expect(!view.isMovingComponents() && requests == axis+1,"validated release emits exactly one commit");
			}
			ok &= startMove(view); const auto validDelta = view.componentMoveDelta();
			view.setComponentPreview(validDelta,{},QStringLiteral("Invalid convex hull")); view.finishComponentMove(true);
			ok &= expect(requests == 3 && !view.isMovingComponents(),"invalid convex hull never requests a commit");
			ok &= startMove(view); view.finishComponentMove(true); view.escapeComponents(); view.setComponentPreview(view.componentMoveDelta(),{},{});
			ok &= expect(requests == 3 && !view.isMovingComponents(),"Escape cancels a pending release");
			ok &= startMove(view);
			ok &= expect(!view.updateComponentMove({std::numeric_limits<double>::quiet_NaN(),0}),"non-finite drag is rejected");
			view.finishComponentMove(true); ok &= expect(requests == 3,"invalid pointer cannot commit preceding valid geometry");
			ok &= startMove(view); view.setMoveGrid(8); view.finishComponentMove(true);
			ok &= expect(requests == 3 && !view.isMovingComponents(),"grid changes cancel in-progress movement");
			ok &= startMove(view); view.setCameraView({-185,-220,160},50,-30); view.finishComponentMove(true);
			ok &= expect(requests == 3 && !view.isMovingComponents(),"navigation changes cancel movement against an old projection");
			ok &= settle(view); ok &= startMove(view); view.setEnabled(false); view.finishComponentMove(true);
			ok &= expect(requests == 3 && !view.isMovingComponents(),"disabled stale camera cannot finish a gesture"); view.setEnabled(true);
			ok &= settle(view);
			if (!output.isEmpty() && profileIndex == 0) {
				QImage image(view.size(),QImage::Format_ARGB32_Premultiplied); view.render(&image);
				ok &= image.save(QDir(output).filePath(QStringLiteral("components-%1.png").arg(scale)));
			}
			view.escapeComponents(); ok &= expect(view.pickedComponents().isEmpty(),"Escape clears picks first");
			int exits = 0; QObject::connect(&view,&ModelViewport::componentModeExitRequested,&view,[&] { ++exits; });
			view.escapeComponents(); ok &= expect(exits == 1,"second Escape asks the owner to exit component mode");
			if (scale == 200) { app.removeTranslator(&translator); }
		}
		++profileIndex;
	}
	return ok;
}
}

int main(int argc, char** argv)
{
	// Only semantic APIs and owned Qt render targets. No injected input,
	// desktop capture, installed game data or persistent user settings.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp;
	if (!temp.isValid()) { return 1; }
	const auto output = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR"); if (!output.isEmpty()) { QDir().mkpath(output); }
	LevelMapDocument fixture; QString error;
	if (!tests::createGeometryFixture(2,&fixture,&error)) { return 1; }
	bool ok = widgetChecks(app,fixture,output);
	ok &= planSplitChecks(app,fixture,output);
	{
		LevelMapDocument dense; dense.format = LevelMapFormat::Quake3Map;
		LevelPatchCreateRequest request; request.columns = request.rows = 31; LevelMapPatch grid;
		if (!createLevelPatch(request,&grid,&error)) { return 1; }
		QVector<LevelMapSelectionRef> selected;
		for (int id = 0; id < 18; ++id) { grid.id = id; dense.patches.append(grid); selected.append({LevelMapSelectionKind::QuakePatch,id}); }
		MapViewport plan; plan.setDocument(dense); plan.setSelectionSet(selected); plan.setComponentMode(MapViewport::ComponentMode::Vertices);
		ok &= expect(plan.componentHandleCount() == 17*31*31 && plan.componentHandlesLimited(),"large patch selection enforces the handle limit at whole-object boundaries");
	}
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	StudioSettings settings; settings.setReducedMotion(true); settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom")); settings.setLevelViewLayoutPreference(QStringLiteral("four-views")); settings.sync();
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	if (!writeMap(temp.filePath("fixture.map"),fixture)) { return 1; }
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(temp.filePath("fixture.map"));
	shell.findChild<QAction*>("shell.mode.levels")->trigger();
	auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	auto* top = shell.findChild<MapViewport*>("mapViewport");
	auto* front = shell.findChild<MapViewport*>("mapViewport1"); auto* side = shell.findChild<MapViewport*>("mapViewport2");
	if (!camera || !top || !front || !side || !until([&] { return shell.levelDocument().brushes.size() == 2 && camera->hasMesh() && camera->isEnabled(); })) { return 1; }
	const auto choose = [&](QVector<LevelMapSelectionRef> selected) { top->setSelectionSet(selected); top->selectionSetChanged(selected); };
	shell.findChild<QAction*>("map.grid16")->trigger(); shell.findChild<QCheckBox*>("levelMapSnap")->setChecked(true);
	// Splitting uses the same checked transaction in a plan view and camera.
	// Call semantic gesture methods directly; never send mouse/key events.
	const auto splitSession = [&](bool useCamera, bool cancel) {
		choose({{LevelMapSelectionKind::QuakeBrush,0}});
		if (camera->componentMode() != 3) { shell.findChild<QAction*>("map.faceTool")->trigger(); }
		int right = -1,left = -1;
		for (int i = 0; i < top->componentHandles().size(); ++i) {
			const auto p = top->componentHandles()[i].position;
			if (std::abs(p.y) < 0.01 && std::abs(p.z) < 0.01) { if (p.x > 0) { right = i; } else { left = i; } }
		}
		if (!expect(right >= 0 && left >= 0,"face split handles exist")) { return false; }
		const auto handle = top->componentHandles()[right];
		top->setPickedComponentPositions({{handle.object,handle.position}}); top->componentPicksChanged(1);
		auto* split = shell.findChild<QAction*>("map.splitComponents");
		if (!expect(split && split->isEnabled(),"split command available for face mode")) { return false; }
		if (!split->isChecked()) { split->trigger(); }
		camera->setCameraView({-96,-120,88},50,-30); camera->setComponentXray(true);
		if (!settle(*camera)) { return false; }
		const auto before = serializeLevelMap(shell.levelDocument()).bytes;
		const auto depth = shell.levelDocument().undoStack.size();
		const auto sets = top->pickedComponents();
		if (useCamera) { if (!startMove(*camera)) { return false; } }
		else {
			const auto from = top->componentHandlePosition(right), opposite = top->componentHandlePosition(left);
			if (!expect(top->beginComponentMove(from) && top->updateComponentMove(from+(from-opposite)*0.5),"plan split gesture starts")) { return false; }
		}
		const auto cameraDelta = camera->componentMoveDelta();
		const LevelMapVec3 delta = useCamera ? LevelMapVec3{cameraDelta.x,cameraDelta.y,cameraDelta.z,true} : top->componentMoveDelta();
		auto expected = shell.levelDocument();
		if (!expect(splitLevelMapComponents(&expected,sets,delta,false,nullptr,&error),"independent split candidate",error)) { return false; }
		if (useCamera) { camera->finishComponentMove(!cancel); } else { top->finishComponentMove(!cancel); }
		if (!until([&] { return !camera->isMovingComponents() && !top->isMovingComponents(); })) { return false; }
		if (cancel) { return expect(serializeLevelMap(shell.levelDocument()).bytes == before && shell.levelDocument().undoStack.size() == depth,"pending split cancellation leaves map and history untouched"); }
		bool passed = expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes && shell.levelDocument().undoStack.size() == depth+1,"split commits exactly its checked candidate as one undo step");
		passed &= expect(camera->componentMode() == 1 && !camera->componentSplit() && top->pickedComponentCount() == 1 && front->pickedComponentCount() == 1 && side->pickedComponentCount() == 1,"new corner selected in all views and split mode retired");
		shell.findChild<QAction*>("map.undo")->trigger();
		passed &= expect(serializeLevelMap(shell.levelDocument()).bytes == before,"shared split undo restores source");
		shell.findChild<QAction*>("map.redo")->trigger();
		passed &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes,"shared split redo restores candidate");
		shell.findChild<QAction*>("map.undo")->trigger(); return passed;
	};
	ok &= splitSession(false,true);
	ok &= splitSession(false,false);
	ok &= splitSession(true,true);
	ok &= splitSession(true,false);
	const auto pickExtrusionFace = [&] {
		choose({{LevelMapSelectionKind::QuakeBrush,0}});
		if (camera->componentMode() != 3) { shell.findChild<QAction*>("map.faceTool")->trigger(); }
		for (const auto& handle : top->componentHandles()) {
			if (handle.position.x > 0 && std::abs(handle.position.y) < 0.01 && std::abs(handle.position.z) < 0.01) {
				top->setPickedComponentPositions({{handle.object,handle.position}}); top->componentPicksChanged(1); return true;
			}
		}
		return false;
	};
	const auto extrusionSession = [&](bool useCamera, bool split, bool cancel) {
		if (!pickExtrusionFace()) { return false; }
		auto* extrude = shell.findChild<QAction*>("map.extrudeFaces");
		if (!extrude->isChecked()) { extrude->trigger(); }
		auto* separate = shell.findChild<QAction*>("map.extrudeSplit");
		if (separate->isChecked() != split) { separate->trigger(); }
		camera->setCameraView({-96,-120,88},50,-30); camera->setComponentXray(true);
		if (!settle(*camera)) { return false; }
		const auto before = serializeLevelMap(shell.levelDocument()).bytes;
		const auto depth = shell.levelDocument().undoStack.size(); const auto sets = top->pickedComponents();
		if (useCamera) { if (!startMove(*camera)) { return false; } }
		else {
			int index = 0;
			while (index < top->componentHandles().size() && top->componentHandles()[index].position.x <= 0) { ++index; }
			const auto from = top->componentHandlePosition(index);
			if (!expect(top->beginComponentMove(from) && top->updateComponentMove(from+QPointF(36,0)),"plan normal extrusion starts")) { return false; }
		}
		const auto distance = useCamera ? camera->componentExtrusionDistance() : top->componentExtrusionDistance();
		auto expected = shell.levelDocument();
		if (!expect(distance > 0 && extrudeLevelMapFaces(&expected,sets,distance,split,settings.levelTextureLock(),nullptr,&error),"independent extrusion candidate",error)) { return false; }
		if (!output.isEmpty() && !cancel) {
			if (!until([&] { return useCamera ? !camera->componentPreviewPending() : !top->componentPreviewPending(); })) { return false; }
			QWidget* view = useCamera ? static_cast<QWidget*>(camera) : static_cast<QWidget*>(top);
			QImage image(view->size(),QImage::Format_ARGB32_Premultiplied); view->render(&image);
			if (!image.save(QDir(output).filePath(QStringLiteral("extrusion-%1-%2.png").arg(useCamera ? "camera" : "plan").arg(split ? "split" : "move")))) { return false; }
		}
		if (useCamera) { camera->finishComponentMove(!cancel); } else { top->finishComponentMove(!cancel); }
		if (!until([&] { return !camera->isMovingComponents() && !top->isMovingComponents(); })) { return false; }
		if (cancel) { return expect(serializeLevelMap(shell.levelDocument()).bytes == before && shell.levelDocument().undoStack.size() == depth,"extrusion cancellation preserves map and history"); }
		bool passed = expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes && shell.levelDocument().undoStack.size() == depth+1,"extrusion commits checked geometry as one undo step");
		passed &= expect(camera->componentMode() == 3 && camera->componentExtrusion() == (split ? 2 : 1)
			&& top->pickedComponentCount() == 1 && front->pickedComponentCount() == 1 && side->pickedComponentCount() == 1,"moved face remains selected with extrusion active in all panes");
		shell.findChild<QAction*>("map.undo")->trigger(); passed &= expect(serializeLevelMap(shell.levelDocument()).bytes == before,"extrusion undo restores source");
		shell.findChild<QAction*>("map.redo")->trigger(); passed &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes,"extrusion redo restores candidate");
		shell.findChild<QAction*>("map.undo")->trigger(); return passed;
	};
	ok &= extrusionSession(false,false,true);
	ok &= extrusionSession(false,false,false);
	ok &= extrusionSession(true,false,false);
	ok &= extrusionSession(false,true,false);
	ok &= extrusionSession(true,true,true);
	ok &= extrusionSession(true,true,false);
	ok &= settle(*camera);
	if (pickExtrusionFace()) {
		const auto before = serializeLevelMap(shell.levelDocument()).bytes;
		auto expected = shell.levelDocument();
		ok &= extrudeLevelMapFaces(&expected,top->pickedComponents(),-7.125,true,settings.levelTextureLock(),nullptr,&error);
		Expanded translator; app.installTranslator(&translator);
		applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastLight,UiDensity::Standard,200));
		shell.setLayoutDirection(Qt::RightToLeft);
		ok &= settle(*camera);
		QTimer::singleShot(100,&shell,[&] {
			auto* dialog = shell.findChild<QDialog*>("extrudeFacesDialog");
			if (!expect(dialog != nullptr,"precise extrusion dialog opens")) { ok = false; return; }
			auto* distance = dialog->findChild<QDoubleSpinBox*>("extrudeDistance");
			auto* separate = dialog->findChild<QCheckBox*>("extrudeSeparate");
			auto* apply = dialog->findChild<QPushButton*>("extrudeApply");
			ok &= expect(distance && separate && apply && !distance->accessibleName().isEmpty(),"precise extrusion exposes accessible controls");
			if (!distance || !separate || !apply) { dialog->reject(); return; }
			ok &= expect(camera->isEnabled() && !camera->pickedComponents().isEmpty() && camera->componentExtrusion() == 2,"numeric source selection is ready before acceptance");
			distance->setValue(-7.125); separate->setChecked(true);
			ok &= expect(apply->text().contains(QStringLiteral("expanded")) && apply->width() >= apply->fontMetrics().horizontalAdvance(apply->text()),"expanded action label fits at 200 percent");
			if (!output.isEmpty()) { QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); dialog->render(&image); ok &= image.save(QDir(output).filePath("extrusion-precise-200-rtl.png")); }
			apply->click();
			if (dialog->isVisible()) { ok = false; dialog->reject(); }
		});
		shell.findChild<QAction*>("map.extrudePrecisely")->trigger();
		ok &= until([&] { return !camera->isMovingComponents(); });
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes,"numeric split uses exact unsnapped distance through shared worker",shell.statusBar()->currentMessage());
		app.removeTranslator(&translator); shell.setLayoutDirection(Qt::LeftToRight);
		applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
		shell.findChild<QAction*>("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == before,"numeric extrusion undo restores source");
	} else { ok = false; }
	camera->escapeComponents(); camera->escapeComponents(); camera->setComponentXray(false);
	choose({{LevelMapSelectionKind::QuakeBrush,0}}); shell.findChild<QAction*>("map.vertexTool")->trigger();
	camera->setCameraView({-96,-120,88},50,-30);
	QEventLoop initial; QTimer::singleShot(100,&initial,&QEventLoop::quit); initial.exec(QEventLoop::ExcludeUserInputEvents);
	ok &= settle(*camera);
	ok &= expect(camera->componentMode() == 1 && top->componentHandleCount() == 8,"camera shares the active plan component mode and topology");
	shell.findChild<QAction*>("map.componentXray")->trigger();
	ok &= expect(camera->componentXray() && shell.findChild<QAction*>("map.componentXray")->isChecked(),"X-ray command reflects the camera state");
	int corner = 0;
	while (corner+1 < top->componentHandles().size() && top->componentHandles()[corner].position.x < 0) { ++corner; }
	ok &= camera->pickComponentAt(camera->componentScreenPosition(corner));
	for (auto* plan : {top,front,side}) { ok &= expect(plan->pickedComponentCount() == 1,"camera corner pick propagates to every plan pane"); }
	ok &= expect(shell.findChild<QAction*>("map.removeComponents")->isEnabled(),"camera picks enable shared topology removal");
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	const auto originalUndoDepth = shell.levelDocument().undoStack.size();
	const auto originalRedoDepth = shell.levelDocument().redoStack.size();
	const auto originalHistoryRevision = shell.levelDocument().historyRevision;
	const auto initialHandles = camera->componentMoveHandles();
	if (!startMove(*camera)) { return 1; }
	for (int step = 1; step <= 12; ++step) {
		ok &= camera->updateComponentMove(initialHandles[0]+(initialHandles[0]-initialHandles[3])*(0.3+step*0.12));
	}
	const auto delta = camera->componentMoveDelta(); auto expected = shell.levelDocument();
	ok &= moveLevelMapComponents(&expected,top->pickedComponents(),{delta.x,delta.y,delta.z,true},true,nullptr,&error);
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().undoStack.size() == originalUndoDepth
		&& shell.levelDocument().redoStack.size() == originalRedoDepth && shell.levelDocument().historyRevision == originalHistoryRevision,
		"background preview leaves source and undo history unchanged");
	ok &= expect(until([&] { return !camera->componentPreviewPending(); }),"coalesced preview completes for the newest drag position");
	if (!output.isEmpty()) {
		QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied); camera->render(&image);
		ok &= image.save(QDir(output).filePath("camera-components-preview.png"));
	}
	camera->finishComponentMove(true);
	ok &= expect(until([&] { return !camera->isMovingComponents(); }),"pending release finishes after worker validation");
	const auto committed = serializeLevelMap(expected).bytes;
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == committed && shell.levelDocument().undoStack.size() == originalUndoDepth+1,
		"camera commits byte-identical shared-service geometry in one undo step",shell.statusBar()->currentMessage());
	for (auto* plan : {top,front,side}) { ok &= expect(serializeLevelMap(plan->displayDocument()).bytes == committed && plan->pickedComponentCount() == 1,"committed corner remains picked in all plan panes"); }
	ok &= until([&] { return camera->isEnabled(); }); ok &= settle(*camera);
	ok &= expect(camera->pickedComponents().size() == 1,"rebuilt camera retains the moved corner pick");
	const auto planStart = front->componentHandlePosition(*front->pickedComponentHandles().cbegin());
	ok &= expect(front->beginComponentMove(planStart) && front->updateComponentMove(planStart+QPointF(0,-36)),"front plan move follows the semantic preview path");
	front->finishComponentMove(true);
	ok &= until([&] { return !front->isMovingComponents(); });
	for (auto* plan : {top,front,side}) { ok &= expect(plan->pickedComponentCount() == 1,"plan component move keeps the moved pick in every pane"); }
	ok &= until([&] { return camera->isEnabled(); }); ok &= settle(*camera);
	ok &= expect(camera->pickedComponents().size() == 1,"plan component move shares its new pick with the camera");
	shell.findChild<QAction*>("map.undo")->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == committed,"plan component undo restores the camera edit");
	shell.findChild<QAction*>("map.undo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original,"camera edit supports exact undo");
	shell.findChild<QAction*>("map.redo")->trigger(); ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == committed,"camera edit supports exact redo");
	ok &= shell.saveLevelDocument(temp.filePath("saved.map"),false,&error);
	LevelMapDocument saved; ok &= expect(loadLevelMap({temp.filePath("saved.map"),{},{}},&saved,&error) && serializeLevelMap(saved).bytes == committed,"camera component edit saves and reloads exactly",error);
	// Multi-brush edge and face moves use the same native component IDs.
	for (const char* tool : {"map.edgeTool","map.faceTool"}) {
		shell.findChild<QAction*>(tool)->trigger(); choose({{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
		ok &= until([&] { return camera->isEnabled(); }); ok &= settle(*camera); camera->selectAllComponents();
		ok &= expect(top->pickedComponents().size() == 2,"camera selects components across both brushes");
		auto moved = shell.levelDocument(); const auto count = moved.undoStack.size();
		if (!startMove(*camera,2)) { return 1; }
		const auto offset = camera->componentMoveDelta();
		ok &= moveLevelMapComponents(&moved,top->pickedComponents(),{offset.x,offset.y,offset.z,true},true,nullptr,&error);
		camera->finishComponentMove(true); ok &= until([&] { return !camera->isMovingComponents(); });
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(moved).bytes && shell.levelDocument().undoStack.size() == count+1,"multi-brush component move is one atomic edit");
	}
	ok &= until([&] { return camera->isEnabled(); }); ok &= settle(*camera);
	camera->selectAllComponents(); const auto beforeCancel = serializeLevelMap(shell.levelDocument()).bytes;
	ok &= startMove(*camera); choose({{LevelMapSelectionKind::QuakeBrush,1}}); camera->finishComponentMove(true);
	ok &= expect(!camera->isMovingComponents() && serializeLevelMap(shell.levelDocument()).bytes == beforeCancel,"selection change cancels an in-flight worker draft");
	camera->selectAllComponents(); ok &= settle(*camera); ok &= startMove(*camera); const auto undoDepth = shell.levelDocument().undoStack.size();
	top->moveRequested(0,16,0); camera->finishComponentMove(true);
	ok &= expect(!camera->isMovingComponents() && shell.levelDocument().undoStack.size() == undoDepth+1,"a concurrent plan edit owns its single undo step and cancels the camera draft");
	ok &= shell.saveLevelDocument(temp.filePath("saved.map"),true,&error);
	ok &= until([&] { return camera->isEnabled(); }); ok &= settle(*camera); camera->selectAllComponents();
	const auto beforeSave = serializeLevelMap(shell.levelDocument()).bytes; const auto saveDepth = shell.levelDocument().undoStack.size();
	ok &= startMove(*camera); ok &= shell.saveLevelDocument(temp.filePath("saved-copy.map"),false,&error); camera->finishComponentMove(true);
	ok &= expect(!camera->isMovingComponents() && shell.levelDocument().sourcePath == temp.filePath("saved-copy.map")
		&& serializeLevelMap(shell.levelDocument()).bytes == beforeSave && shell.levelDocument().undoStack.size() == saveDepth,
		"saving during a pending preview preserves the new source bindings and cancels the draft");
	// Reload a locked document, then exercise the same camera request.
	QString layer; ok &= createLevelSceneNode(&fixture,LevelSceneNodeKind::Layer,QStringLiteral("Locked"),{},&layer,&error);
	ok &= assignLevelSceneObjects(&fixture,layer,{QStringLiteral("brush:0")},&error); ok &= setLevelSceneLocked(&fixture,layer,true,&error);
	ok &= writeMap(temp.filePath("locked.map"),fixture); shell.openPathFromCommandLine(temp.filePath("locked.map"));
	ok &= until([&] { return shell.levelDocument().sourcePath == temp.filePath("locked.map") && camera->isEnabled(); });
	choose({{LevelMapSelectionKind::QuakeBrush,0}}); if (!camera->componentMode()) { shell.findChild<QAction*>("map.vertexTool")->trigger(); }
	ok &= settle(*camera); camera->selectAllComponents();
	const auto locked = serializeLevelMap(shell.levelDocument()).bytes;
	const auto lockedUndoDepth = shell.levelDocument().undoStack.size();
	const auto lockedRedoDepth = shell.levelDocument().redoStack.size();
	const auto lockedHistoryRevision = shell.levelDocument().historyRevision;
	if (!camera->pickedComponents().isEmpty()) {
		ok &= startMove(*camera); camera->finishComponentMove(true); ok &= until([&] { return !camera->isMovingComponents(); });
	}
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == locked && shell.levelDocument().undoStack.size() == lockedUndoDepth
		&& shell.levelDocument().redoStack.size() == lockedRedoDepth && shell.levelDocument().historyRevision == lockedHistoryRevision,
		"locked scene geometry cannot be changed by the camera worker");
	// Patch points share the same camera path even in edge/face mode, and a
	// single camera layout remains in 3D when a component tool is chosen.
	LevelMapDocument patchMap; LevelMapCreateRequest create; create.game = QStringLiteral("quake3"); create.starterRoom = false;
	LevelMapPatch patch; LevelPatchCreateRequest patchRequest; patchRequest.size = {64,64,64,true};
	ok &= createLevelMap(create,&patchMap,&error) && createLevelPatch(patchRequest,&patch,&error) && addLevelMapPatch(&patchMap,patch,nullptr,&error);
	patchMap.undoStack.clear(); ok &= writeMap(temp.filePath("patch.map"),patchMap); shell.openPathFromCommandLine(temp.filePath("patch.map"));
	ok &= until([&] { return shell.levelDocument().patches.size() == 1 && camera->isEnabled(); });
	shell.findChild<QAction*>("map.layout.single-3d")->trigger();
	choose({{LevelMapSelectionKind::QuakePatch,shell.levelDocument().patches[0].id}});
	if (camera->componentMode()) { camera->escapeComponents(); camera->escapeComponents(); }
	shell.findChild<QAction*>("map.edgeTool")->trigger();
	ok &= expect(!camera->isHidden() && top->QWidget::isHidden() && camera->componentMode() == 2,"activating components preserves the camera-only layout");
	camera->setCameraView({-96,-120,88},50,-30);
	QEventLoop layout; QTimer::singleShot(100,&layout,&QEventLoop::quit); layout.exec(QEventLoop::ExcludeUserInputEvents);
	ok &= settle(*camera); camera->setComponentXray(true); camera->selectAllComponents();
	ok &= expect(top->pickedComponentCount() == 9 && !top->pickedComponents().isEmpty()
		&& top->pickedComponents()[0].kind == LevelBrushComponent::Vertex,"edge mode exposes all native patch points");
	auto expectedPatch = shell.levelDocument();
	const auto patchUndoDepth = expectedPatch.undoStack.size();
	if (!startMove(*camera,2)) { return 1; }
	const auto patchDelta = camera->componentMoveDelta();
	ok &= moveLevelMapComponents(&expectedPatch,top->pickedComponents(),{patchDelta.x,patchDelta.y,patchDelta.z,true},true,nullptr,&error);
	camera->finishComponentMove(true); ok &= until([&] { return !camera->isMovingComponents(); });
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expectedPatch).bytes && shell.levelDocument().undoStack.size() == patchUndoDepth+1,"camera moves a native patch as one undoable shared-service edit");
	shell.findChild<QAction*>("map.undo")->trigger();
	ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(patchMap).bytes,"patch camera undo restores exact control points and UVs");
	ok &= shell.saveLevelDocument(temp.filePath("patch.map"),true,&error);
	camera->setSurfaceTool(ModelViewportSurfaceTool::Paint);
	ok &= expect(camera->componentMode() == 0 && top->componentMode() == MapViewport::ComponentMode::Off,"material tool exits component mode in every view");
	shell.findChild<QAction*>("map.vertexTool")->trigger(); camera->setBrushDrawTool(true);
	ok &= expect(camera->componentMode() == 0 && top->componentMode() == MapViewport::ComponentMode::Off,"brush construction exits shared component mode");
	camera->setBrushDrawTool(false); shell.findChild<QAction*>("map.vertexTool")->trigger(); shell.findChild<QAction*>("map.clipTool")->trigger();
	ok &= expect(camera->componentMode() == 0 && top->componentMode() == MapViewport::ComponentMode::Off && top->clipMode(),"clipper activation gives component tools a consistent exit");
	ok &= shell.close();
	return ok ? 0 : 1;
}
