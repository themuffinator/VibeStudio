#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "app/studio_sidebar.h"
#include "core/level_component_selection.h"
#include "core/map_preview_mesh.h"
#include "tests/level_geometry_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLayout>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTranslator>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label) {
	if (!value) { ok = false; std::cerr << "FAIL: " << label << '\n'; } return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 20000) { QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int n) const override { return QStringLiteral("[%1 expanded wording]").arg(QString::fromUtf8(source).replace("%n",QString::number(n))); }
};
void coreChecks() {
	const QVector<LevelComponentCycleHit> hits{{2,10,1,"near"},{1,10,1,"near"},{4,-10,0,"far"},{3,0,0,"crossing"}};
	auto cycle = cycleLevelComponentHits(hits,{99},1);
	expect(cycle.picked == QSet<int>({1,2,99}) && cycle.ordinal == 1 && cycle.count == 3 && cycle.handle == 1,"near geometry groups first, with outside picks retained");
	cycle = cycleLevelComponentHits(hits,cycle.picked,1);
	expect(cycle.picked == QSet<int>({3,99}) && cycle.ordinal == 2,"crossing geometry remains separate");
	cycle = cycleLevelComponentHits(hits,cycle.picked,1);
	expect(cycle.picked == QSet<int>({4,99}) && cycle.ordinal == 3,"farther depth replaces only the overlap");
	expect(cycleLevelComponentHits(hits,cycle.picked,1).picked == QSet<int>({1,2,99}),"forward wraps");
	expect(cycleLevelComponentHits(hits,{1,2},-1).picked == QSet<int>{4},"reverse wraps");
	expect(cycleLevelComponentHits(hits,{},-1).picked == QSet<int>{4},"fresh reverse starts at farthest");
	expect(cycleLevelComponentHits(hits,{1,4,99},1).picked == QSet<int>({1,2,99}),"multiple groups restart rather than silently choosing an arbitrary current depth");
	expect(cycleLevelComponentHits(hits,{},2).picked == QSet<int>{3},"multi-notch forward");
	expect(cycleLevelComponentHits(hits,{},-2).picked == QSet<int>{3},"multi-notch reverse");
	auto shuffled = hits; std::reverse(shuffled.begin(),shuffled.end());
	expect(cycleLevelComponentHits(shuffled,{},1).picked == QSet<int>({1,2}),"input order cannot affect geometry group order");
	expect(cycleLevelComponentHits({{9,1,1,{}},{7,1,1,{}},{8,1,0,{}}},{},1).picked == QSet<int>{8},"equal-depth screen-distance ordering");
	expect(cycleLevelComponentHits({{9,1,1,{}},{7,1,1,{}}},{},1).picked == QSet<int>{7},"stable handle-ID tie break without geometry keys");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	expect(cycleLevelComponentHits({{-1,0,0,{}},{1,nan,0,{}},{2,0,nan,{}},{3,0,-1,{}}},{42},1).picked == QSet<int>{42},"invalid hit data cannot change picks");
	expect(cycleLevelComponentHits(hits,{42},0).picked == QSet<int>{42},"zero step is inert");
	QVector<LevelComponentCycleHit> dense; for (int i = 0; i < 16384; ++i) { dense << LevelComponentCycleHit{i,double(i),0,QString::number(i)}; }
	QElapsedTimer timer; timer.start(); cycle = cycleLevelComponentHits(dense,{},1);
	expect(cycle.picked == QSet<int>{16383} && cycle.count == 16384,"bounded dense ring");
	std::cout << "16384-handle cycle query: " << timer.nsecsElapsed()/1e6 << " ms\n";
	dense << LevelComponentCycleHit{16384,0,0,{}};
	expect(cycleLevelComponentHits(dense,{42},1).picked == QSet<int>{42},"oversized context refuses atomically");
	LevelComponentCycleCursor cursor; cursor.aim({10,10});
	expect(cursor.wheelSteps(-40) == 0 && cursor.wheelSteps(-40) == 0 && cursor.wheelSteps(-40) == 1,"high-resolution wheel accumulates a notch");
	cursor.wheelSteps(-60); cursor.aim({30,10}); expect(cursor.wheelSteps(-60) == 0,"moving to another overlap resets fractional wheel input");
	cursor.aim({nan,0}); expect(!cursor.point && cursor.remainder == 0,"invalid cursor resets safely");
}
int handleAt(const MapViewport& view,double x,double y,double z) {
	for (int i = 0; i < view.componentHandleCount(); ++i) { const auto p = view.componentHandles()[i].position; if (p.x == x && p.y == y && p.z == z) { return i; } } return -1;
}
void planChecks(QApplication& app,const LevelMapDocument& map,const QString& evidence) {
	for (int scale : {100,200}) { for (bool light : {false,true}) {
		const auto theme = studioThemeTokens(light ? StudioTheme::HighContrastLight : StudioTheme::HighContrastDark,UiDensity::Standard,scale);
		applyStudioTheme(app,theme); Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); }
		MapViewport plan; plan.resize(1000,800); plan.setDocument(map); plan.setSelectionSet({{LevelMapSelectionKind::QuakeBrush,0}}); plan.setHighContrast(true);
		if (scale == 200) { plan.setLayoutDirection(Qt::RightToLeft); }
		for (auto projection : {MapViewportProjection::TopXY,MapViewportProjection::FrontXZ,MapViewportProjection::SideZY}) {
			plan.setProjection(projection); plan.zoomToSelection();
			for (int mode : {1,2,3}) {
				plan.setComponentMode(static_cast<MapViewport::ComponentMode>(mode));
				const double a = mode == 1 ? -16 : 0, b = mode == 3 ? 0 : -16;
				const int near = projection == MapViewportProjection::TopXY ? handleAt(plan,a,b,16)
					: projection == MapViewportProjection::FrontXZ ? handleAt(plan,a,-16,b) : handleAt(plan,16,a,b);
				const int far = projection == MapViewportProjection::TopXY ? handleAt(plan,a,b,-16)
					: projection == MapViewportProjection::FrontXZ ? handleAt(plan,a,16,b) : handleAt(plan,-16,a,b);
				if (!expect(near >= 0 && far >= 0,"known projected component pair")) { continue; }
				const auto point = plan.componentHandlePosition(near);
				expect(plan.pickComponentAt(point) && plan.pickedComponentCount() == 2,"ordinary plan click keeps projected stack");
				expect(plan.cycleComponentAt(point) && plan.pickedComponentHandles() == QSet<int>{near},"first cycle resolves a selected stack to the near depth");
				expect(plan.cycleComponentSelection() && plan.pickedComponentHandles() == QSet<int>{far},"keyboard API advances to far depth");
				expect(plan.pickComponentAt(point,LevelComponentPick::Replace,true) && plan.pickedComponentHandles() == QSet<int>{far},"drag preserves the single cycled depth");
				expect(plan.cycleComponentSelection(-1) && plan.pickedComponentHandles() == QSet<int>{near},"keyboard API returns to near depth");
				expect(plan.accessibleDescription().contains("XYZ") && plan.accessibleDescription().contains("Overlap"),"cycle status includes ordinal and world coordinates");
				const auto selected = plan.pickedComponentHandles();
				expect(plan.componentCycleWheel(point,-60,Qt::AltModifier) && plan.pickedComponentHandles() == selected,"partial wheel does not cycle early");
				expect(plan.componentCycleWheel(point,-60,Qt::AltModifier) && plan.pickedComponentHandles() == QSet<int>{far},"full wheel notch cycles without zoom");
				expect(!plan.componentCycleWheel(point,-120,Qt::NoModifier),"plain wheel remains navigation");
				auto controls = plan.controls(); controls.componentCycleWheelModifiers = Qt::NoModifier; plan.setControls(controls);
				expect(!plan.componentCycleWheel(point,-120,Qt::AltModifier),"None disables wheel cycling");
				controls.componentCycleWheelModifiers = Qt::AltModifier; plan.setControls(controls);
				plan.setPickedComponentHandles({near}); expect(plan.cycleComponentSelection() && plan.pickedComponentHandles() == QSet<int>{far},"exact-ID selection resets stale cycle anchor");
				QImage image(plan.size(),QImage::Format_ARGB32_Premultiplied); plan.render(&image);
				expect(image.pixelColor(point.toPoint()).rgb() == theme.colors.selection.rgb(),"selected depth paints above unselected overlap");
				int other = -1;
				for (int i = 0; i < plan.componentHandleCount(); ++i) { if (plan.componentHandlePosition(i) != point) { other = i; break; } }
				expect(other >= 0 && image.pixelColor(plan.componentHandlePosition(other).toPoint()).rgb() == theme.colors.input.rgb(),"object resize handles cannot fill unselected component handles");
				int background = 0; for (int y = 0; y < image.height(); y += 8) { for (int x = 0; x < image.width(); x += 8) { if (image.pixelColor(x,y).rgb() == theme.colors.input.rgb()) { ++background; } } }
				expect(background > image.width()*image.height()/128,"plan background follows high-contrast light or dark theme");
				if (!evidence.isEmpty() && mode == 1 && projection == MapViewportProjection::TopXY) {
					expect(image.save(QDir(evidence).filePath(QStringLiteral("cycle-plan-%1-%2.png").arg(light ? "light" : "dark").arg(scale))),"owned plan render");
				}
			}
		}
		plan.clearDocument(); expect(!plan.cycleComponentSelection() && !plan.cycleComponentAt({0,0}),"closed map has no stale cycle");
		if (scale == 200) { app.removeTranslator(&expanded); }
	} }
}
void cameraChecks(const LevelMapDocument& map) {
	ModelViewport camera; camera.resize(1000,800); camera.setMesh(buildLevelMapPreviewMesh(map).mesh);
	camera.setCameraControls(trenchBroomLevelControls().camera); camera.setCameraView({-128,0,0},0,0);
	expect(tests::settleModelViewport(camera),"camera renderer ready");
	const QVector<ModelVec3> positions{{-16,0,0},{-16,0,0},{16,0,0},{-16,24,0},{-140,0,0},{0,9999,0}};
	const QVector<QString> keys{"near","near","far","outside","behind-camera","offscreen"};
	for (int mode : {1,2,3}) {
		camera.setComponentHandles(mode,positions,{3},false,keys); camera.setComponentXray(false);
		const auto point = camera.componentScreenPosition(0);
		expect(camera.cycleComponentAt(point) && camera.pickedComponents() == QSet<int>({0,1,3}),"visible cycle selects coincident near group and preserves unrelated picks");
		expect(camera.cycleComponentSelection() && camera.pickedComponents() == QSet<int>({0,1,3}),"occluded components excluded");
		camera.setComponentXray(true);
		expect(camera.cycleComponentAt(point) && camera.pickedComponents() == QSet<int>({2,3}),"X-ray cycle reaches far component");
		expect(camera.cycleComponentSelection() && camera.pickedComponents() == QSet<int>({0,1,3}),"X-ray still excludes behind-camera and offscreen handles");
		expect(camera.componentCycleWheel(point,-120,Qt::AltModifier) && camera.pickedComponents() == QSet<int>({2,3}),"camera wheel cycles");
		camera.setComponentHandles(mode,positions,{0},false,{"crossing-a","crossing-b","far","outside","behind-camera","offscreen"});
		expect(camera.cycleComponentAt(point) && camera.pickedComponents() == QSet<int>{1},"equal-centre different geometry cycles separately");
		const auto before = camera.pickedComponents();
		expect(!camera.cycleComponentAt({std::numeric_limits<double>::infinity(),0}) && camera.pickedComponents() == before,"nonfinite pointer is inert");
		camera.setComponentHandles(mode,positions,before,true,keys);
		expect(!camera.cycleComponentAt(point) && camera.pickedComponents() == before,"truncated handle context refuses cycling");
	}
	camera.setComponentHandles(1,positions,{0},false,keys); camera.setMoveGrid(1);
	const auto handles = camera.componentMoveHandles();
	if (expect(camera.beginComponentMove(handles[2]),"component move begins")) {
		expect(!camera.cycleComponentAt(camera.componentScreenPosition(0)) && camera.pickedComponents() == QSet<int>{0},"active component edit blocks cycling"); camera.finishComponentMove(false);
	}
	camera.setComponentHandles(0,{},{}); expect(!camera.cycleComponentSelection(),"leaving component mode clears camera cycle");
}
void shellChecks(const QString& path,const QString& evidence,int scale) {
	std::cerr << "Shell scale " << scale << '\n';
	ApplicationShell shell; shell.resize(scale == 100 ? 1700 : 2600,scale == 100 ? 1100 : 1800); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* id) { std::cerr << "Action " << id << '\n'; auto* action = shell.findChild<QAction*>(id); if (expect(action && action->isEnabled(),id)) { action->trigger(); } };
	trigger("shell.mode.levels"); auto* plan = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(plan && camera && until([&] { return shell.levelDocument().brushes.size() == 1 && camera->hasMesh() && camera->isEnabled(); }),"shell ready")) { return; }
	trigger("map.selectAll"); trigger("map.vertexTool"); plan->zoomToSelection(); shell.activateWindow(); plan->setFocus(Qt::OtherFocusReason);
	expect(until([&] { return plan->hasFocus(); }),"owned shell plan focus established");
	const auto bytes = serializeLevelMap(shell.levelDocument()).bytes; const auto objects = shell.levelDocument().selection;
	// TrenchBroom records Select All as a non-content selection step; component
	// cycling and cancelled edits must add nothing on top of that history.
	const auto history = shell.levelDocument().undoStack.size();
	const int near = handleAt(*plan,-16,-16,16), far = handleAt(*plan,-16,-16,-16);
	if (!expect(near >= 0 && far >= 0,"shell corner pair")) { return; }
	plan->pickComponentAt(plan->componentHandlePosition(near)); trigger("map.nextComponent");
	const auto agree = [&](const QSet<int>& picked) {
		if (plan->pickedComponentHandles() != picked || camera->pickedComponents() != picked) {
			const auto ids = [](const QSet<int>& values) { QStringList parts; for (int id : values) { parts << QString::number(id); } return parts.join(',').toStdString(); };
			std::cerr << "Expected " << ids(picked) << "; plan " << ids(plan->pickedComponentHandles()) << "; camera " << ids(camera->pickedComponents())
				<< "; focus " << (camera->hasFocus() ? "camera" : plan->hasFocus() ? "plan" : "other") << '\n';
		}
		expect(plan->pickedComponentHandles() == picked && camera->pickedComponents() == picked,"plan and camera cycle agree");
		for (auto* view : shell.findChildren<MapViewport*>()) { if (view->componentMode() != MapViewport::ComponentMode::Off) { expect(view->pickedComponentHandles() == picked,"all plans share exact cycled IDs"); } }
	};
	agree({near}); trigger("map.nextComponent"); agree({far}); trigger("map.previousComponent"); agree({near});
	expect(serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().selection == objects && shell.levelDocument().undoStack.size() == history,"cycling preserves geometry, object context and undo history");
	for (const auto& id : {QStringLiteral("map.nextComponent"),QStringLiteral("map.previousComponent")}) {
		auto* button = shell.findChild<QPushButton*>("levelToolsRow-"+id); auto* accessible = button ? QAccessible::queryAccessibleInterface(button) : nullptr;
		expect(button && button->focusPolicy() != Qt::NoFocus && accessible && accessible->role() == QAccessible::Button && !accessible->text(QAccessible::Name).isEmpty(),"cycle commands accessible and keyboard focusable");
	}
	if (!evidence.isEmpty()) {
		auto* panel = shell.findChild<QWidget*>("levelTools-component-selection");
		if (expect(panel != nullptr,"selection tools panel")) {
			for (auto* section : shell.findChildren<SidebarSection*>()) { if (section->body() == panel) { section->setExpanded(true); } }
			panel->show(); panel->ensurePolished(); panel->resize(std::max(360*scale/100,panel->minimumSizeHint().width()),panel->sizeHint().height()); panel->layout()->setGeometry(panel->rect());
			QImage image(panel->size(),QImage::Format_ARGB32_Premultiplied); panel->render(&image);
			for (auto* button : panel->findChildren<QPushButton*>()) { expect(!button->isHidden() && panel->rect().contains(button->geometry()),"cycle tools fit expanded text at scale"); }
			expect(image.save(QDir(evidence).filePath(QStringLiteral("cycle-tools-%1.png").arg(scale))),"owned tools render");
		}
	}
	camera->setComponentXray(true); camera->setCameraView({-128,-16,16},0,0); shell.activateWindow(); camera->setFocus(Qt::OtherFocusReason);
	expect(until([&] { return camera->hasFocus(); }),"owned shell camera focus established");
	expect(tests::settleModelViewport(*camera),"shell camera settles");
	const int rear = handleAt(*plan,16,-16,16);
	camera->pickComponentAt(camera->componentScreenPosition(near)); trigger("map.nextComponent"); agree({rear});
	// A selection command must retire a pending preview before it changes IDs.
	camera->setCameraView({-160,-180,120},50,-28); expect(tests::settleModelViewport(*camera),"move camera settles"); camera->setMoveGrid(8);
	auto handles = camera->componentMoveHandles();
	if (expect(camera->beginComponentMove(handles[2]) && camera->updateComponentMove(handles[2]+(handles[2]-handles[3])*.5),"pending cycled-component move begins")) {
		camera->finishComponentMove(true); trigger("map.previousComponent");
		expect(until([&] { return !camera->isMovingComponents(); }) && serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().undoStack.size() == history,"cycle command cancels pending commit atomically");
	}
	handles = camera->componentMoveHandles();
	if (expect(camera->beginComponentMove(handles[2]) && camera->updateComponentMove(handles[2]+(handles[2]-handles[3])*.5),"cycled component feeds checked edit")) {
		camera->finishComponentMove(true); expect(until([&] { return !camera->isMovingComponents(); }) && shell.levelDocument().undoStack.size() == history + 1
			&& shell.levelDocument().undoStack.last().commandKind != QStringLiteral("selection") && serializeLevelMap(shell.levelDocument()).bytes != bytes,"cycled edit is one geometry transaction");
		trigger("map.undo"); expect(serializeLevelMap(shell.levelDocument()).bytes == bytes,"exact undo of cycled edit");
	}
}
}
int main(int argc,char** argv) {
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	QTimer watchdog; watchdog.setSingleShot(true); QObject::connect(&watchdog,&QTimer::timeout,&app,[] { std::cerr << "Cycle test watchdog\n"; std::exit(1); }); watchdog.start(120000);
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	LevelMapDocument map; QString error; if (!expect(tests::createGeometryFixture(1,&map,&error),"geometry fixture")) { return 1; }
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE");
	coreChecks(); planChecks(app,map,evidence); cameraChecks(map);
	const auto path = temp.filePath("cycle.map"); if (!expect(saveLevelMapAs(map,path).succeeded(),"saved fixture")) { return 1; }
	for (int scale : {100,200}) {
		settings.setSelectedEditorProfileId(scale == 100 ? "trenchbroom" : "netradiant-custom");
		settings.setTheme(scale == 100 ? StudioTheme::HighContrastDark : StudioTheme::HighContrastLight); settings.setTextScalePercent(scale); settings.sync();
		Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
		shellChecks(path,evidence,scale);
		if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
	}
	return ok ? 0 : 1;
}
