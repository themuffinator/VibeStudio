#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/editor_profiles.h"
#include "core/level_gestures.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTranslator>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& detail = {}) {
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 20000) { QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override { return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source)); }
};
void policyChecks() {
	using Pick = LevelComponentPick;
	const QSet<int> current{0,1,4}, hits{1,2};
	expect(selectLevelComponentHandles(current,hits,Pick::Replace) == hits,"replace picks");
	expect(selectLevelComponentHandles(current,hits,Pick::Add) == QSet<int>({0,1,2,4}),"add preserves others");
	expect(selectLevelComponentHandles(current,hits,Pick::Subtract) == QSet<int>({0,4}),"subtract leaves outside picks");
	expect(selectLevelComponentHandles(current,hits,Pick::Toggle) == QSet<int>({0,2,4}),"lasso toggles each hit");
	expect(selectLevelComponentHandles(current,hits,Pick::Toggle,true) == QSet<int>({0,1,2,4}),"partial coincident group fills on toggle");
	expect(selectLevelComponentHandles({0,1,2,4},hits,Pick::Toggle,true) == QSet<int>({0,4}),"whole coincident group toggles off");
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("gtkradiant"),QStringLiteral("quark")}) {
		EditorProfileDescriptor descriptor; expect(editorProfileForId(profile,&descriptor),"profile exists");
		const auto& p = descriptor.controls.plan; const auto& c = descriptor.controls.camera;
		for (bool band : {false,true}) {
			expect(levelComponentPickOperation(Qt::NoModifier,p.toggleModifiers,p.addModifiers,band,p.componentBandToggles)
				== (band && profile == "trenchbroom" ? Pick::Toggle : Pick::Replace),"profile plan plain selection",profile);
			expect(levelComponentPickOperation(Qt::NoModifier,c.toggleModifiers,Qt::NoModifier,band,c.componentBandToggles)
				== (band && profile == "trenchbroom" ? Pick::Toggle : Pick::Replace),"profile camera plain selection",profile);
		}
		expect(levelComponentPickOperation(Qt::ControlModifier|Qt::ShiftModifier,p.toggleModifiers,p.addModifiers,true,p.componentBandToggles) == Pick::Subtract,"both selection keys subtract",profile);
	}
	expect(levelComponentPickOperation(Qt::ControlModifier,Qt::ControlModifier,Qt::NoModifier,true,true) == Pick::Add,"TB Ctrl lasso adds");
	expect(levelComponentPickOperation(Qt::ControlModifier,Qt::ControlModifier,Qt::NoModifier,false,true) == Pick::Toggle,"TB Ctrl click toggles");
	expect(levelComponentPickOperation(Qt::ControlModifier|Qt::ShiftModifier,Qt::ControlModifier|Qt::ShiftModifier,Qt::NoModifier,false,false) == Pick::Toggle,"custom toggle chord takes precedence over subtraction");
	expect(levelComponentPickOperation(Qt::ControlModifier|Qt::ShiftModifier,Qt::ControlModifier|Qt::ShiftModifier,Qt::NoModifier,true,true) == Pick::Add,"custom toggle chord retains additive TB lasso role");
	expect(!levelComponentPickOperation(Qt::AltModifier,Qt::ControlModifier,Qt::ShiftModifier,true,false),"navigation modifier remains available");
	const auto a = levelComponentGeometryKey({{-16,0,0,true},{16,0,0,true}});
	expect(a == levelComponentGeometryKey({{16,0,0,true},{-16,0,0,true}}),"edge orientation irrelevant");
	expect(a != levelComponentGeometryKey({{0,-16,0,true},{0,16,0,true}}),"crossing edges have distinct identity");
	expect(levelComponentGeometryKey({{0,0,0,true}}) != levelComponentGeometryKey({{.001,0,0,true}}),"nearby corners remain separate");
	expect(levelComponentGeometryKey({{std::numeric_limits<double>::infinity(),0,0,true}}).isEmpty(),"invalid geometry has no coincidence identity");
	LevelEditorControls changed; QJsonObject normalized; QString error;
	expect(applyLevelGestureOverrides(trenchBroomLevelControls(),{{"plan.componentBandToggles","off"},{"camera.componentBandToggles","off"}},&changed,&normalized,&error)
		&& !changed.plan.componentBandToggles && !changed.camera.componentBandToggles,"lasso profile override",error);
}
void widgetChecks(QApplication& app,const LevelMapDocument& map,const QString& evidence) {
	const auto mesh = buildLevelMapPreviewMesh(map).mesh;
	for (int scale : {100,200}) {
		applyStudioTheme(app,studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark,UiDensity::Standard,scale));
		Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); }
		MapViewport plan; plan.resize(1000,800); plan.setDocument(map); plan.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
		ModelViewport camera; camera.resize(1000,800); camera.setMesh(mesh); camera.setCameraControls(trenchBroomLevelControls().camera);
		camera.setCameraView({-160,-180,120},50,-28); camera.setComponentXray(true); camera.setHighContrast(scale == 200);
		if (scale == 200) { plan.setLayoutDirection(Qt::RightToLeft); camera.setLayoutDirection(Qt::RightToLeft); }
		expect(tests::settleModelViewport(camera),"camera settles");
		for (int mode : {1,2,3}) {
			plan.setComponentMode(static_cast<MapViewport::ComponentMode>(mode));
			QVector<ModelVec3> positions; QVector<QString> keys;
			for (const auto& handle : plan.componentHandles()) { positions << ModelVec3{float(handle.position.x),float(handle.position.y),float(handle.position.z)}; keys << handle.geometryKey; }
			camera.setComponentHandles(mode,positions,{},false,keys);
			int shared = -1;
			for (int i = 0; i < keys.size(); ++i) { if (keys.count(keys[i]) == 2) { shared = i; break; } }
			if (!expect(shared >= 0,"shared component fixture",QString::number(mode))) { continue; }
			const auto point = camera.componentScreenPosition(shared);
			expect(camera.pickComponentAt(point,LevelComponentPick::Replace) && camera.pickedComponents().size() == 2,"camera selects full coincident component",QString::number(mode));
			expect(camera.pickComponentAt(point,LevelComponentPick::Toggle) && camera.pickedComponents().isEmpty(),"toggle shared component off");
			expect(camera.pickComponentAt(point,LevelComponentPick::Add) && camera.pickedComponents().size() == 2,"add shared component");
			expect(camera.pickComponentAt(point,LevelComponentPick::Subtract) && camera.pickedComponents().isEmpty(),"subtract shared component");
			const QRectF all(-10000,-10000,20000,20000);
			expect(camera.pickComponentBand(all,LevelComponentPick::Add) && camera.pickedComponents().size() == positions.size(),"camera additive lasso");
			expect(camera.pickComponentBand(all,LevelComponentPick::Subtract) && camera.pickedComponents().isEmpty(),"camera subtractive lasso");
			expect(plan.pickComponentAt(plan.componentHandlePosition(shared),LevelComponentPick::Replace) && plan.pickedComponentCount() >= 2,"plan stacked component picks");
			expect(plan.pickComponentBand(all,LevelComponentPick::Replace) && plan.pickedComponentCount() == keys.size(),"plan replace lasso");
			expect(plan.pickComponentBand(all,LevelComponentPick::Toggle) && plan.pickedComponentCount() == 0,"plan toggle lasso");
			camera.pickComponentAt(point,LevelComponentPick::Replace);
			if (!evidence.isEmpty() && mode == 2) {
				QImage image(camera.size(),QImage::Format_ARGB32_Premultiplied); camera.render(&image);
				expect(image.save(QDir(evidence).filePath(QStringLiteral("selection-%1.png").arg(scale))),"owned camera render");
			}
		}
		const auto sharedEdge = levelComponentGeometryKey({{-16,0,0,true},{16,0,0,true}});
		const auto crossingEdge = levelComponentGeometryKey({{0,-16,0,true},{0,16,0,true}});
		camera.setComponentHandles(2,{{0,0,0},{0,0,0},{0,0,0}}, {},false,{crossingEdge,sharedEdge,sharedEdge});
		const auto centre = camera.componentScreenPosition(1);
		expect(camera.pickComponentAt(centre) && camera.pickedComponents() == QSet<int>({1,2}),"same centre does not clump crossing edges");
		camera.setComponentHandles(2,{{0,0,0},{0,0,0},{0,0,0}}, {1},false,{crossingEdge,sharedEdge,sharedEdge});
		expect(camera.pickComponentAt(centre,LevelComponentPick::Toggle) && camera.pickedComponents() == QSet<int>({1,2}),"camera partial coincident toggle fills group");
		if (scale == 200) { app.removeTranslator(&expanded); }
	}
}
void shellChecks(const LevelMapDocument& map,const QString& path) {
	QFile file(path); const auto source = serializeLevelMap(map).bytes;
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(),"write shell fixture")) { return; } file.close();
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* id) { auto* action = shell.findChild<QAction*>(id); if (expect(action && action->isEnabled(),id)) { action->trigger(); } };
	trigger("shell.mode.levels");
	auto* plan = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(plan && camera && until([&] { return shell.levelDocument().brushes.size() == 2 && camera->hasMesh() && camera->isEnabled(); }),"shell map ready")) { return; }
	trigger("map.selectAll"); trigger("map.vertexTool"); const auto objects = shell.levelDocument().selection;
	trigger("map.selectAll"); expect(plan->pickedComponentCount() == 16 && camera->pickedComponents().size() == 16,"Select All shared components");
	trigger("map.invertSelection"); expect(plan->pickedComponentCount() == 0 && camera->pickedComponents().isEmpty(),"Invert all component picks");
	trigger("map.selectAll"); trigger("map.selectNone"); expect(plan->pickedComponentCount() == 0 && camera->pickedComponents().isEmpty(),"Select None clears only picks");
	expect(shell.levelDocument().selection == objects && plan->componentMode() == MapViewport::ComponentMode::Vertices
		&& serializeLevelMap(shell.levelDocument()).bytes == source && shell.levelDocument().undoStack.isEmpty(),"selection commands retain mode, objects and clean geometry");
	camera->setComponentXray(true); camera->setCameraView({-160,-180,120},50,-28); expect(tests::settleModelViewport(*camera),"shell camera ready");
	int shared = -1;
	for (int i = 0; i < plan->componentHandleCount(); ++i) { const auto p = plan->componentHandles()[i].position; if (p.x == 16 && p.y == -16 && p.z == 16) { shared = i; break; } }
	if (!expect(shared >= 0,"terrain seam corner")) { return; }
	expect(camera->pickComponentAt(camera->componentScreenPosition(shared)) && plan->pickedComponentCount() == 2,"camera clump shares both brush picks with plan");
	const auto sets = plan->pickedComponents(); expect(sets.size() == 2,"two brush component sets");
	camera->setMoveGrid(8); const auto handles = camera->componentMoveHandles();
	expect(camera->beginComponentMove(handles[2]) && camera->updateComponentMove(handles[2]+(handles[2]-handles[3])*.5),"shared corner constrained gesture");
	const auto delta = camera->componentMoveDelta(); expect(delta.z > 0 && delta.x == 0 && delta.y == 0,"shared corner Z motion");
	camera->finishComponentMove(true);
	expect(until([&] { return !camera->isMovingComponents(); }) && plan->pickedComponentCount() == 2 && shell.levelDocument().undoStack.size() == 1,"shared corner edit is one transaction");
	for (const auto& pick : plan->pickedComponentPositions()) { expect(pick.second.x == 16 && pick.second.y == -16 && pick.second.z == 16+delta.z,"both terrain corners move together"); }
	trigger("map.undo"); expect(serializeLevelMap(shell.levelDocument()).bytes == source,"shared corner exact undo");
	trigger("map.selectAll"); trigger("map.componentScaleTool");
	auto* gizmo = plan->componentGizmo(); const auto frame = plan->componentGizmoFrame();
	QPointF press; for (const auto& handle : gizmo->handles(frame)) { if (handle.id == 3) { press = handle.label; } }
	expect(plan->beginComponentTransform(press) && plan->updateComponentTransform(press+QPointF(96,0)),"pending transform fixture");
	plan->finishComponentTransform(true); trigger("map.selectNone");
	expect(!gizmo->active() && plan->pickedComponentCount() == 0 && serializeLevelMap(shell.levelDocument()).bytes == source,"selection command cancels pending preview");
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
	LevelMapDocument map; QString error; if (!expect(tests::createGeometryFixture(2,&map,&error),"geometry fixture",error)) { return 1; }
	// Adjacent boxes share four corners, four edges and one face.
	for (auto& face : map.brushes[1].faces) { for (auto* p : {&face.p0,&face.p1,&face.p2}) { p->x -= 16; } }
	map.brushes[1].mins.x -= 16; map.brushes[1].maxs.x -= 16;
	policyChecks(); widgetChecks(app,map,qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE"));
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100)); shellChecks(map,temp.filePath("terrain.map"));
	return ok ? 0 : 1;
}
