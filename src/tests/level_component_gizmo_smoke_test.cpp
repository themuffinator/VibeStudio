#include "app/application_shell.h"
#include "app/level_component_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTranslator>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& detail = {}) {
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool near(double a,double b) { return std::abs(a-b) < .002; }
bool until(const std::function<bool()>& ready) {
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 20000) {
		QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}
LevelComponentGizmoFrame orthographic(bool oblique = false) {
	const ModelVec3 right = oblique ? ModelVec3{float(1/std::sqrt(2.)),float(-1/std::sqrt(2.)),0} : ModelVec3{1,0,0};
	const ModelVec3 up = oblique ? ModelVec3{float(1/std::sqrt(6.)),float(1/std::sqrt(6.)),float(-2/std::sqrt(6.))} : ModelVec3{0,1,0};
	const ModelVec3 normal = oblique ? ModelVec3{float(1/std::sqrt(3.)),float(1/std::sqrt(3.)),float(1/std::sqrt(3.))} : ModelVec3{0,0,1};
	LevelComponentGizmoFrame frame; frame.radius = 36; frame.viewDirection = {-normal.x,-normal.y,-normal.z};
	frame.project = [right,up](const LevelMapVec3& p) { return QPointF(400+2*(p.x*right.x+p.y*right.y+p.z*right.z),300-2*(p.x*up.x+p.y*up.y+p.z*up.z)); };
	frame.ray = [right,up,normal](const QPointF& p) {
		const double x = (p.x()-400)/2,y = (300-p.y())/2;
		return ModelPickRay{{float(x*right.x+y*up.x+100*normal.x),float(x*right.y+y*up.y+100*normal.y),float(x*right.z+y*up.z+100*normal.z)}, {-normal.x,-normal.y,-normal.z},false};
	}; return frame;
}
QPointF label(LevelComponentGizmo& gizmo,const LevelComponentGizmoFrame& frame,int id) {
	for (const auto& handle : gizmo.handles(frame)) { if (handle.id == id) { return handle.label; } }
	expect(false,"requested handle exists",QString::number(id)); return {};
}
void controllerChecks() {
	LevelComponentGizmo gizmo; const auto frame = orthographic(); int commits = 0,pivots = 0; LevelMapVec3 movedPivot;
	QObject::connect(&gizmo,&LevelComponentGizmo::commitRequested,&gizmo,[&](quint64) { ++commits; });
	QObject::connect(&gizmo,&LevelComponentGizmo::pivotChanged,&gizmo,[&](LevelMapVec3 p) { ++pivots; movedPivot = p; });
	gizmo.setContext(LevelComponentTool::Rotate,{0,0,0,true},true,16);
	const auto start = label(gizmo,frame,2);
	if (!expect(gizmo.begin(start,frame),"begin Z rotation")) { return; }
	const double phase = std::atan2(300-start.y(),start.x()-400);
	for (int quarter = 1; quarter <= 5; ++quarter) {
		const double angle = phase+quarter*std::numbers::pi/2;
		expect(gizmo.updateDrag({400+72*std::cos(angle),300-72*std::sin(angle)}) && near(gizmo.request().amount,quarter*90),"rotation unwraps through full turns");
	}
	const auto serial = gizmo.serial(); gizmo.finish(true);
	expect(gizmo.active() && gizmo.releasePending() && commits == 0,"release waits for validation");
	gizmo.setPreview(serial-1,{},{}); expect(gizmo.pending(),"older preview cannot satisfy release");
	gizmo.setPreview(serial,{},{}); expect(!gizmo.active() && commits == 1,"latest preview commits once");
	gizmo.setPreview(serial,{},{}); expect(commits == 1,"duplicate callback ignored");
	for (double factor : {2.,-1.,0.}) {
		gizmo.setContext(LevelComponentTool::Scale,{0,0,0,true},true,0);
		const auto press = label(gizmo,frame,0); expect(gizmo.begin(press,frame),"begin axis scale");
		expect(gizmo.updateDrag(press+QPointF(72*(factor-1),0)) && near(gizmo.request().scale.x,factor) && near(gizmo.request().scale.y,1),"axis factor and reflection");
		gizmo.setPreview(gizmo.serial(),{},factor == 0 ? QStringLiteral("Degenerate brush") : QString()); gizmo.finish(true);
	}
	expect(commits == 3,"invalid collapse does not commit");
	const auto all = label(gizmo,frame,3); expect(gizmo.begin(all,frame),"begin uniform scale");
	expect(gizmo.updateDrag(all+QPointF(96,0)) && near(gizmo.request().scale.x,2) && near(gizmo.request().scale.z,2),"uniform scale");
	const auto cancelled = gizmo.serial(); gizmo.finish(true); gizmo.finish(false); gizmo.setPreview(cancelled,{},{});
	expect(!gizmo.active() && commits == 3,"cancel pending release");
	const auto oblique = orthographic(true);
	constexpr int axes[6][2]{{0,1},{0,2},{1,0},{1,2},{2,0},{2,1}};
	for (int id = 0; id < 6; ++id) {
		gizmo.setContext(LevelComponentTool::Shear,{0,0,0,true},true,0);
		const auto press = label(gizmo,oblique,id); LevelMapVec3 direction{0,0,0,true};
		if (axes[id][0] == 0) { direction.x = 18; } else if (axes[id][0] == 1) { direction.y = 18; } else { direction.z = 18; }
		expect(gizmo.begin(press,oblique),"begin shear handle",QString::number(id));
		expect(gizmo.updateDrag(press+oblique.project(direction)-oblique.project({0,0,0,true})) && near(gizmo.request().amount,.5)
			&& gizmo.request().axis == axes[id][0] && gizmo.request().along == axes[id][1],"six shear directions",QString::number(id)); gizmo.finish(false);
	}
	gizmo.setContext(LevelComponentTool::Pivot,{0,0,0,true},true,16);
	expect(gizmo.begin(label(gizmo,frame,3),frame) && gizmo.updateDrag({420,284}),"begin view-plane pivot"); gizmo.finish(true);
	expect(pivots == 1 && near(movedPivot.x,16) && near(movedPivot.y,16) && near(movedPivot.z,0) && commits == 3,"pivot snaps without geometry commit");
	gizmo.setContext(LevelComponentTool::Scale,{0,0,0,true},true,0);
	expect(gizmo.begin(label(gizmo,frame,0),frame) && gizmo.updateDrag({544,300}),"prepare invalid pointer check");
	expect(!gizmo.updateDrag({std::numeric_limits<double>::infinity(),0}),"nonfinite pointer refused"); gizmo.finish(true);
	expect(commits == 3,"invalid pointer cannot apply preceding valid draft");
	expect(gizmo.begin(label(gizmo,frame,0),frame) && gizmo.updateDrag({544,300}),"prepare identity check");
	expect(gizmo.updateDrag({472,300}),"return to identity"); gizmo.setPreview(gizmo.serial(),{},{}); gizmo.finish(true);
	expect(commits == 3,"identity does not commit");
	expect(gizmo.begin(label(gizmo,frame,0),frame) && gizmo.updateDrag({544,300}),"prepare context cancellation");
	gizmo.setContext(LevelComponentTool::Scale,{5,0,0,true},true,0); gizmo.finish(true);
	expect(!gizmo.active() && commits == 3,"changed pivot cancels old frame");
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override {
		const auto value = QString::fromUtf8(source); return QStringLiteral("[%1%2]").arg(value,QString(value.size()/3,'~'));
	}
};
void shellChecks(QApplication& app,const QString& evidence) {
	QTemporaryDir exportDirectory;
	ApplicationShell shell; shell.resize(1700,1100); shell.show();
	LevelMapCreateRequest create; create.starterRoom = true; QString error;
	if (!expect(shell.createLevelDocument(create,&error),"shell fixture",error)) { return; }
	const auto trigger = [&](const char* name) { auto* action = shell.findChild<QAction*>(QString::fromLatin1(name)); if (expect(action && action->isEnabled(),name)) { action->trigger(); } };
	trigger("shell.mode.levels"); trigger("map.selectAll"); trigger("map.vertexTool");
	auto* view = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(view && camera,"shared plan and camera")) { return; }
	if (!expect(until([&] { return camera->hasMesh() && camera->isEnabled(); }),"camera preview mesh ready")) { return; }
	view->resize(960,720); camera->resize(960,720);
	QVector<QPair<LevelMapSelectionRef,LevelMapVec3>> picks; const int brush = shell.levelDocument().brushes[0].id;
	for (const auto& handle : view->componentHandles()) { if (handle.object.objectId == brush) { picks.append({handle.object,handle.position}); } }
	if (!expect(picks.size() == 8,"eight first-brush corners")) { return; }
	const auto choose = [&] { view->setPickedComponentPositions(picks); view->componentPicksChanged(picks.size()); };
	choose(); const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	for (auto tool : {LevelComponentTool::Rotate,LevelComponentTool::Scale,LevelComponentTool::Shear}) {
		choose(); trigger(tool == LevelComponentTool::Rotate ? "map.componentRotateTool" : tool == LevelComponentTool::Scale ? "map.componentScaleTool" : "map.componentShearTool");
		auto* gizmo = view->componentGizmo(); const auto frame = view->componentGizmoFrame();
		expect(camera->componentGizmo()->tool() == tool,"tool shared with camera");
		const auto press = label(*gizmo,frame,tool == LevelComponentTool::Rotate ? 2 : 0); QPointF end;
		if (tool == LevelComponentTool::Rotate) { const auto centre = frame.project(gizmo->pivot()); const auto offset = press-centre; end = centre+QPointF(offset.y(),-offset.x()); }
		else { end = press+QPointF(tool == LevelComponentTool::Scale ? 72 : 36,0); }
		if (!expect(view->beginComponentTransform(press) && view->updateComponentTransform(end),"plan semantic transform")) { continue; }
		expect(serializeLevelMap(shell.levelDocument()).bytes == original,"preview leaves source untouched");
		const auto request = gizmo->request(); auto expected = shell.levelDocument(); LevelMapComponentReport report;
		expect(transformLevelMapComponents(&expected,view->pickedComponents(),request,&report,&error),"shared-service oracle",error);
		view->finishComponentTransform(true); expect(until([&] { return !gizmo->active(); }),"worker completes release");
		expect(serializeLevelMap(shell.levelDocument()).bytes == serializeLevelMap(expected).bytes && shell.levelDocument().undoStack.size() == 1,"gesture equals shared service in one undo");
		expect(view->pickedComponentCount() == 8 && camera->pickedComponents().size() == 8,"picked corners survive publication");
		if (tool == LevelComponentTool::Shear) {
			expect(shell.saveLevelDocument(QDir(evidence.isEmpty() ? exportDirectory.path() : evidence).filePath("gesture-shear.map"),true,&error),"save gesture map",error);
			expect(view->componentMode() == MapViewport::ComponentMode::Vertices && view->pickedComponentCount() == 8 && gizmo->tool() == tool,"first save preserves component context");
		}
		trigger("map.undo"); expect(serializeLevelMap(shell.levelDocument()).bytes == original,"exact gesture undo");
	}
	choose(); trigger("map.componentScaleTool");
	auto* gizmo = view->componentGizmo(); auto frame = view->componentGizmoFrame(); auto press = label(*gizmo,frame,0);
	expect(view->beginComponentTransform(press) && view->updateComponentTransform(press-QPointF(72,0)),"zero scale draft");
	view->finishComponentTransform(true); expect(until([&] { return !gizmo->active(); }),"invalid preview completes");
	expect(serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().undoStack.isEmpty(),"zero-volume brush is atomic");
	press = label(*gizmo,view->componentGizmoFrame(),0);
	expect(view->beginComponentTransform(press) && view->updateComponentTransform(press+QPointF(36,0)),"pending cancellation draft");
	view->finishComponentTransform(true); view->cancelInteraction();
	expect(!gizmo->active() && serializeLevelMap(shell.levelDocument()).bytes == original,"cancelInteraction cancels pending gesture");
	expect(view->beginComponentTransform(press) && view->updateComponentTransform(press+QPointF(36,0)),"selection cancellation draft");
	view->setPickedComponentPositions({}); view->componentPicksChanged(0); view->finishComponentTransform(true);
	expect(!gizmo->active() && serializeLevelMap(shell.levelDocument()).bytes == original,"changed component picks cancel preview");
	choose(); trigger("map.componentPivotTool"); frame = view->componentGizmoFrame(); press = label(*gizmo,frame,3);
	const auto beforePivot = gizmo->pivot();
	expect(view->beginComponentTransform(press) && view->updateComponentTransform(press+QPointF(72,0)),"pivot gesture"); view->finishComponentTransform(true);
	expect(gizmo->pivot().x != beforePivot.x && near(camera->componentGizmo()->pivot().x,gizmo->pivot().x) && serializeLevelMap(shell.levelDocument()).bytes == original,"pivot shared without map edits");
	trigger("map.componentResetPivot"); expect(near(gizmo->pivot().x,beforePivot.x),"reset pivot to picked bounds");
	trigger("map.componentScaleTool"); expect(tests::settleModelViewport(*camera),"settle camera");
	camera->setCameraView({-185,-220,160},50,-30);
	expect(until([&] { return camera->isEnabled(); }) && tests::settleModelViewport(*camera),"camera ready for gesture");
	const auto cameraFrame = camera->componentGizmoFrame(); auto* cameraGizmo = camera->componentGizmo();
	press = label(*cameraGizmo,cameraFrame,3);
	const bool cameraBegan = camera->beginComponentTransform(press);
	if (expect(cameraBegan,"camera uniform scale begin",QStringLiteral("point=%1,%2 hit=%3 enabled=%4 rendering=%5 mode=%6 looking=%7 tool=%8")
		.arg(press.x()).arg(press.y()).arg(cameraGizmo->hitTest(press,cameraFrame,camera->font())).arg(camera->isEnabled()).arg(camera->isRendering())
		.arg(camera->componentMode()).arg(camera->isLooking()).arg(int(cameraGizmo->tool())))
		&& expect(camera->updateComponentTransform(press+QPointF(96,0)),"camera uniform scale update")) {
		camera->finishComponentTransform(true); expect(until([&] { return !cameraGizmo->active(); }),"camera release finishes");
		expect(shell.levelDocument().undoStack.size() == 1 && view->pickedComponentCount() == 8,"camera commit and shared picks"); trigger("map.undo");
		expect(serializeLevelMap(shell.levelDocument()).bytes == original,"camera exact undo");
	}
	choose();
	for (int scale : {100,200}) {
		applyStudioTheme(app,studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark,UiDensity::Standard,scale));
		Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
		QTimer drive; drive.setInterval(10);
		QObject::connect(&drive,&QTimer::timeout,&shell,[&] {
			auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); if (!dialog || dialog->objectName() != "levelComponentGizmoOptions") { return; } drive.stop();
			if (scale == 200) { dialog->setLayoutDirection(Qt::RightToLeft); dialog->resize(1100,900); }
			for (auto* field : dialog->findChildren<QDoubleSpinBox*>()) {
				auto* accessible = QAccessible::queryAccessibleInterface(field);
				expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && field->focusPolicy() != Qt::NoFocus && field->layoutDirection() == Qt::LeftToRight,"accessible options field",field->objectName());
			}
			dialog->findChild<QCheckBox*>("componentGizmoCustomPivot")->setChecked(true);
			dialog->findChild<QDoubleSpinBox*>("componentGizmoPivotX")->setValue(13);
			dialog->findChild<QDoubleSpinBox*>("componentAngleStep")->setValue(30);
			dialog->findChild<QDoubleSpinBox*>("componentScaleStep")->setValue(.25);
			dialog->findChild<QDoubleSpinBox*>("componentShearStep")->setValue(0);
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			auto* apply = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
			expect(apply->isVisible() && dialog->rect().contains(apply->mapTo(dialog,apply->rect().center())),"visible apply at both scales");
			if (!evidence.isEmpty()) { QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); dialog->render(&image); expect(image.save(QDir(evidence).filePath(QStringLiteral("gizmo-options-%1.png").arg(scale))),"render owned options dialog"); }
			apply->click();
		});
		QTimer watchdog; watchdog.setSingleShot(true); QObject::connect(&watchdog,&QTimer::timeout,&shell,[] { expect(false,"options watchdog"); if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); } });
		drive.start(); watchdog.start(10000); trigger("map.componentGizmoOptions"); drive.stop(); watchdog.stop();
		expect(StudioSettings().levelComponentTransformSteps() == std::array<double,3>{30,.25,0} && near(gizmo->pivot().x,13) && near(cameraGizmo->pivot().x,13),"persistent steps and shared custom pivot");
		if (scale == 200) { app.removeTranslator(&expansion); }
		if (!evidence.isEmpty()) {
			view->setHighContrast(scale == 200); trigger("map.componentRotateTool");
			QImage image(view->size(),QImage::Format_ARGB32_Premultiplied); view->render(&image);
			expect(image.save(QDir(evidence).filePath(QStringLiteral("gizmo-plan-%1.png").arg(scale))),"render owned plan manipulator");
		}
	}
	expect(shell.levelDocument().undoStack.isEmpty() && serializeLevelMap(shell.levelDocument()).bytes == original,"preferences never edit map");
}
}
int main(int argc,char** argv) {
	// Semantic APIs, actions and owned widget renders; no injected input or OS capture.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference(QStringLiteral("four-views")); settings.sync();
	expect(settings.levelComponentTransformSteps() == std::array<double,3>{15,.125,.125},"default transform steps");
	expect(!settings.setLevelComponentTransformSteps({15,std::numeric_limits<double>::quiet_NaN(),0}) && settings.levelComponentTransformSteps()[1] == .125,"invalid steps refused atomically");
	controllerChecks(); shellChecks(app,qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE"));
	return ok ? 0 : 1;
}
