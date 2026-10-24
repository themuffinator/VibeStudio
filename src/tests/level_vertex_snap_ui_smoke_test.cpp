#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/level_component_trace.h"
#include "app/viewport_hud.h"
#include "app/studio_theme.h"
#include "core/level_editor_controls.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value,const char* label,const QString& detail = {}) { if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; }
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start(); while (!ready() && timer.elapsed() < 20000) { QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); } return ready();
}
QByteArray bytes(const LevelMapDocument& map) { return serializeLevelMap(map).bytes; }
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override { const auto text = QString::fromUtf8(source); return '['+text+QString(text.size()/3,'~')+']'; }
};
void traceChecks(QApplication& app,const QString& evidence) {
	for (int scale : {100,200}) {
		applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastLight,UiDensity::Standard,scale));
		Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
		QImage image(700,350,QImage::Format_ARGB32_Premultiplied); image.fill(app.palette().color(QPalette::Window)); QPainter painter(&image);
		painter.setFont(app.font()); LevelComponentGizmoFrame frame; frame.project = [](const auto& p) { return QPointF(120+p.x*4,270-p.y*4); };
		const auto labels = paintLevelComponentTrace(painter,frame,{0,0,0,true},{70,30,20,true},0,true,true,app.palette());
		expect(labels.size() == 3,"trace includes hidden-axis displacement");
		for (const auto& label : labels) { expect(QRectF(image.rect()).contains(label.bounds) && label.text.contains(QLatin1Char("XYZ"[label.axis])),"bounded non-colour axis label"); }
		for (int i = 0; i < labels.size(); ++i) { for (int j = i+1; j < labels.size(); ++j) { expect(!labels[i].bounds.intersects(labels[j].bounds),"trace labels do not overlap"); } }
		expect(paintLevelComponentTrace(painter,frame,{0,0,0,true},{0,0,0,true},-1,false,false,app.palette()).isEmpty(),"zero movement has no trace");
		painter.end(); if (!evidence.isEmpty()) { expect(image.save(QDir(evidence).filePath(QStringLiteral("vertex-trace-%1.png").arg(scale))),"save owned trace render"); }
		QImage nearHud(500,260,QImage::Format_ARGB32_Premultiplied); nearHud.fill(Qt::white); QPainter nearPainter(&nearHud); nearPainter.setFont(app.font());
		frame.project = [](const auto& p) { return QPointF(80+p.x,10-p.y); };
		const auto bounded = paintLevelComponentTrace(nearPainter,frame,{0,0,0,true},{70,30,20,true},-1,false,true,app.palette());
		const double belowHud = 2*std::ceil(QFontMetricsF(viewportHudFont(app.font()),&nearHud).height())+18;
		expect(bounded.size() == 3,"near-HUD trace can retain three distances");
		for (const auto& label : bounded) { expect(label.bounds.top() >= belowHud && QRectF(nearHud.rect()).contains(label.bounds),"distance labels avoid both possible HUD rows"); }
		nearPainter.end();
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
}
void shellChecks(QApplication& app,const QString& directory,const QString& evidence) {
	QString error; LevelMapDocument map; tests::createGeometryFixture(1,&map,&error);
	// A sheared, off-grid box separates all eight corners in every plan view.
	for (auto& face : map.brushes[0].faces) { for (auto* p : {&face.p0,&face.p1,&face.p2}) {
		const auto v = *p; *p = {v.x+.25*v.y+.125*v.z+1.25,v.y+.125*v.x+.25*v.z+2.5,v.z+.125*v.x+.25*v.y+3.75,true};
	} }
	map.brushes[0].geometryDirty = true;
	const auto path = QDir(directory).filePath("sheared.map"); expect(saveLevelMapAs(map,path).succeeded(),"save original fixture");
	ApplicationShell shell; shell.resize(1700,1100); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* name) { auto* action = shell.findChild<QAction*>(QString::fromLatin1(name)); if (expect(action && action->isEnabled(),name)) { action->trigger(); } };
	trigger("shell.mode.levels"); auto* top = shell.findChild<MapViewport*>("mapViewport"); auto* front = shell.findChild<MapViewport*>("mapViewport1");
	auto* side = shell.findChild<MapViewport*>("mapViewport2"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(top && front && side && camera && until([&] { return shell.levelDocument().brushes.size() == 1 && camera->hasMesh() && camera->isEnabled(); }),"views ready")) { return; }
	trigger("map.selectAll"); trigger("map.vertexTool"); trigger("map.grid64");
	const auto original = bytes(shell.levelDocument()); LevelBrushTopology topology; levelBrushTopology(shell.levelDocument().brushes[0],&topology); const auto edge = topology.edges[0];
	const auto source = edge[0],target = edge[1]; const auto targetPoint = topology.vertices[target];
	const auto ready = [&] {
		QElapsedTimer stable; stable.start(); return until([&] {
			QImage image(camera->size(),QImage::Format_ARGB32_Premultiplied); camera->render(&image);
			const bool settled = top->isEnabled() && front->isEnabled() && side->isEnabled() && camera->isEnabled() && !camera->isRendering();
			if (!settled) { stable.restart(); } return settled && stable.elapsed() >= 50;
		});
	};
	const auto pick = [&](MapViewport* view) { view->setPickedComponentHandles({source}); view->componentPicksChanged(1); };
	const auto expected = [&] {
		auto result = shell.levelDocument(); LevelComponentTransformRequest request; request.allowCollapse = true;
		expect(levelMapVertexSnapDelta(result,top->pickedComponents(),{LevelMapSelectionKind::QuakeBrush,0},target,&request.delta,&error)
			&& transformLevelMapComponents(&result,top->pickedComponents(),request,nullptr,&error),"shared core oracle",error); return bytes(result);
	};
	const auto checkUndo = [&](const QByteArray& changed) {
		expect(bytes(shell.levelDocument()) == changed && shell.levelDocument().undoStack.size() == 1,"GUI exactly matches target merge with one undo");
		expect(top->pickedComponentCount() == 1 && camera->pickedComponents().size() == 1,"merged target remains selected across views");
		trigger("map.undo"); expect(bytes(shell.levelDocument()) == original,"snap exact undo");
	};
	for (auto* view : {top,front,side}) {
		if (!expect(ready(),"settle before plan snap")) { return; }
		view->setControls(trenchBroomLevelControls().plan); view->zoomToSelection(); pick(view);
		const auto changed = expected(); const auto point = view->componentGizmoFrame().project(targetPoint);
		expect(view->armComponentMove(point,Qt::AltModifier|Qt::ShiftModifier),"arm cancellable snap"); view->finishComponentMove(false);
		expect(bytes(shell.levelDocument()) == original && view->pickedComponentCount() == 1 && view->pickedComponentHandles().contains(source),"cancelled click preserves source and history");
		expect(view->canSnapPickedVertex() && view->armComponentMove(point,Qt::AltModifier|Qt::ShiftModifier),"arm native quick snap",view->objectName());
		expect(bytes(shell.levelDocument()) == original && view->pickedComponentCount() == 1,"press preserves picked source and document");
		view->finishComponentMove(true); checkUndo(changed);
	}
	if (!expect(ready(),"settle remapped plan snap")) { return; } pick(top);
	auto customPlan = trenchBroomLevelControls().plan; customPlan.componentAxisDrag = false; customPlan.componentQuickSnapModifiers = Qt::MetaModifier; top->setControls(customPlan);
	auto changedCustom = expected(); expect(top->armComponentMove(top->componentGizmoFrame().project(targetPoint),Qt::MetaModifier),"custom quick snap without native dragging");
	top->finishComponentMove(true); checkUndo(changedCustom);
	if (!expect(ready(),"settle click-versus-drag check")) { return; } pick(top); top->setControls(trenchBroomLevelControls().plan);
	const auto dragPress = top->componentGizmoFrame().project(targetPoint);
	expect(top->armComponentMove(dragPress,Qt::AltModifier|Qt::ShiftModifier)
		&& top->updateComponentMove(dragPress+QPointF(80,40),Qt::AltModifier|Qt::ShiftModifier) && top->isMovingComponents(),"quick snap chord can begin a real drag");
	expect(top->pickedComponentHandles().contains(target) && !top->pickedComponentHandles().contains(source),"drag threshold selects the grabbed target instead of snapping the source");
	top->finishComponentMove(false); expect(bytes(shell.levelDocument()) == original && shell.levelDocument().undoStack.isEmpty(),"cancelled drag leaves map untouched");
	if (!expect(ready(),"settle camera snap")) { return; }
	camera->setCameraControls(trenchBroomLevelControls().camera); camera->setCameraView({-150,-200,140},50,-30); camera->setComponentXray(true);
	if (!expect(ready(),"camera projection ready")) { return; } pick(top);
	const auto changed = expected(); const auto point = camera->componentGizmoFrame().project(targetPoint);
	expect(camera->armComponentMove(point,Qt::AltModifier|Qt::ShiftModifier),"arm camera quick snap"); camera->finishComponentMove(true); checkUndo(changed);
	if (!expect(ready(),"settle remapped camera snap")) { return; } pick(top);
	auto customCamera = trenchBroomLevelControls().camera; customCamera.componentAxisDrag = false; customCamera.componentQuickSnapModifiers = Qt::MetaModifier; camera->setCameraControls(customCamera);
	if (!expect(ready(),"remapped camera projection ready")) { return; }
	expect(camera->armComponentMove(camera->componentGizmoFrame().project(targetPoint),Qt::MetaModifier),"custom camera quick snap without native dragging");
	camera->finishComponentMove(true); checkUndo(changed);
	if (!expect(ready(),"settle keyboard route")) { return; } pick(top);
	for (int scale : {100,200}) {
		applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastDark,UiDensity::Standard,scale)); Expansion expansion;
		if (scale == 200) { app.installTranslator(&expansion); shell.setLayoutDirection(Qt::RightToLeft); }
		QTimer::singleShot(0,&shell,[&] {
			auto* dialog = shell.findChild<QDialog*>("levelVertexSnapDialog"); auto* combo = dialog ? dialog->findChild<QComboBox*>("vertexSnapTarget") : nullptr;
			if (!expect(dialog && combo,"accessible target dialog")) { if (dialog) { dialog->reject(); } return; }
			expect(dialog->layoutDirection() == shell.layoutDirection(),"target dialog inherits requested layout direction");
			combo->setCurrentIndex(combo->findData(target));
			expect(combo->toolTip() == combo->currentText() && combo->accessibleDescription() == combo->currentText(),"full target coordinates remain available when the label is elided");
			auto* accessible = QAccessible::queryAccessibleInterface(combo); expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && combo->focusPolicy() != Qt::NoFocus,"target picker name and focus");
			if (!evidence.isEmpty()) { QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); dialog->render(&image); expect(image.save(QDir(evidence).filePath(QStringLiteral("vertex-snap-dialog-%1.png").arg(scale))),"owned dialog render"); }
			QMetaObject::invokeMethod(dialog->findChild<QDialogButtonBox*>(),"accepted",Qt::DirectConnection);
		});
		trigger("map.snapPickedVertex"); checkUndo(changed);
		if (scale == 200) { app.removeTranslator(&expansion); }
		expect(ready(),"undo settles after target dialog"); pick(top);
	}
	// A target dialog never applies its cached choice to a changed selection.
	QTimer::singleShot(0,&shell,[&] {
		auto* dialog = shell.findChild<QDialog*>("levelVertexSnapDialog"); if (!expect(dialog != nullptr,"stale target dialog")) { return; }
		top->setPickedComponentHandles({target}); top->componentPicksChanged(1);
		QMetaObject::invokeMethod(dialog->findChild<QDialogButtonBox*>(),"accepted",Qt::DirectConnection);
		expect(dialog->isVisible() && !dialog->findChild<QLabel*>("vertexSnapError")->text().isEmpty(),"changed source refused with visible error"); dialog->reject();
	});
	trigger("map.snapPickedVertex"); expect(bytes(shell.levelDocument()) == original && shell.levelDocument().undoStack.isEmpty(),"stale dialog leaves history untouched");
	top->pickAllComponents(); expect(!top->canSnapPickedVertex() && !top->requestVertexSnapAt(top->componentGizmoFrame().project(targetPoint)),"multiple geometric vertices cannot quick snap");
}
}
int main(int argc,char** argv) {
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelComponentUvLock(false); settings.setLevelViewLayoutPreference("four-views"); settings.sync();
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE"); traceChecks(app,evidence); shellChecks(app,temp.path(),evidence); return ok ? 0 : 1;
}
