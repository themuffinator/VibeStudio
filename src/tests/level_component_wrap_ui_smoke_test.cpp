#include "app/application_shell.h"
#include "app/level_wrap_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/level_editor_controls.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
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
bool samePicks(const QVector<LevelMapComponentSet>& a,const QVector<LevelMapComponentSet>& b)
{
	return a.size() == b.size() && std::equal(a.cbegin(),a.cend(),b.cbegin(),[](const auto& x,const auto& y) {
		return x.object == y.object && x.kind == y.kind && x.components == y.components;
	});
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override { const auto text = QString::fromUtf8(source); return '['+text+QString(text.size()/3,'~')+']'; }
};
void dialogChecks(QApplication& app,const QString& evidence)
{
	QString error; LevelMapDocument source; tests::createGeometryFixture(1,&source,&error);
	setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0}}); const auto before = bytes(source);
	LevelWrapRequest request; request.componentsOnly = true; request.texture = "studio/wrap";
	request.components = {{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Face,{0}}};
	for (const int scale : {100,200}) {
		StudioSettings settings; settings.setTheme(StudioTheme::HighContrastLight); settings.setReducedMotion(true); settings.sync();
		applyStudioTheme(app,studioThemeTokens(StudioTheme::HighContrastLight,UiDensity::Standard,scale)); Expansion expansion;
		if (scale == 200) { app.installTranslator(&expansion); }
		QWidget owner; owner.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		LevelWrapDialog dialog(source,request,{}, {}, &owner); dialog.show();
		auto* count = dialog.findChild<QSpinBox*>("wrapPointCount"); auto* x = dialog.findChild<QDoubleSpinBox*>("wrapPoint1X");
		auto* y = dialog.findChild<QDoubleSpinBox*>("wrapPoint1Y"); auto* z = dialog.findChild<QDoubleSpinBox*>("wrapPoint1Z");
		auto* material = dialog.findChild<QLineEdit*>("wrapMaterial"); auto* apply = dialog.findChild<QPushButton*>("wrapApply");
		auto* status = dialog.findChild<QLabel*>("wrapStatus"); auto* view = dialog.findChild<QComboBox*>("wrapView"); auto* preview = dialog.findChild<ModelViewport*>("wrapPreview");
		if (!expect(count && x && y && z && material && apply && status && view && preview,"wrap controls exist")) { return; }
		expect(!apply->isEnabled() && until([&] { return !status->text().contains("Checking") && !status->text().isEmpty(); }) && !dialog.isReady(),"flat picks are visibly refused");
		count->setValue(1); x->setValue(64); y->setValue(48); z->setValue(80);
		if (!expect(until([&] { return dialog.isReady() && preview->hasMesh(); }),"face plus point preview ready",status->text())) { continue; }
		expect(dialog.layoutDirection() == owner.layoutDirection() && apply->isEnabled(),"RTL and apply readiness");
		for (auto* field : QVector<QWidget*>{count,x,y,z,material,view,apply}) {
			auto* a = QAccessible::queryAccessibleInterface(field);
			expect(a && !a->text(QAccessible::Name).isEmpty() && field->focusPolicy() != Qt::NoFocus,"named keyboard-accessible control",field->objectName());
		}
		view->setCurrentIndex(1); expect(preview->hasMesh() && preview->accessibleName().contains("Source"),"source view available without changing candidate");
		view->setCurrentIndex(0); expect(preview->hasMesh() && preview->accessibleName().contains("Wrapped"),"hull view restored");
		material->setText("invalid material"); expect(!apply->isEnabled() && !preview->hasMesh(),"invalidated preview cannot look current or apply");
		expect(until([&] { return !status->text().contains("Checking") && !status->text().isEmpty(); }) && !dialog.isReady(),"unsafe material visibly refused");
		material->setText("studio/wrap");
		for (int i = 0; i < 20; ++i) { z->setValue(81+i); }
		expect(until([&] { return dialog.isReady(); }),"rapid point changes settle on newest candidate",status->text());
		auto expected = source; auto latest = request; latest.points = {{64,48,100,true}}; wrapLevelMap(&expected,latest,nullptr,&error);
		int applications = 0; dialog.setApplyHandler([&](const auto& candidate,QString*) { ++applications; expect(bytes(candidate) == bytes(expected),"preview publishes exact latest prepared hull"); return true; });
		if (!evidence.isEmpty()) {
			// Rendering is a separate worker from geometry validation. Exercise
			// owned-widget paint events until its current mesh has settled.
			expect(until([&] {
				QImage frame(dialog.size(),QImage::Format_ARGB32_Premultiplied); dialog.render(&frame);
				return preview->hasMesh() && !preview->isRendering();
			}),"owned preview render settles");
			QImage image(dialog.size(),QImage::Format_ARGB32_Premultiplied); dialog.render(&image);
			expect(image.save(QDir(evidence).filePath(QString("component-wrap-dialog-%1.png").arg(scale))),"save owned dialog render");
		}
		const auto* scroll = dialog.findChild<QScrollArea*>();
		expect(scroll && scroll->horizontalScrollBar()->maximum() == 0,"coordinate controls do not require horizontal scrolling");
		if (scroll) { for (auto* label : scroll->widget()->findChildren<QLabel*>()) { if (label->isVisible()) {
			const auto position = label->mapTo(scroll->viewport(),QPoint{});
			expect(label->width() > 0 && label->height() > 0 && position.x() >= 0 && position.x()+label->width() <= scroll->viewport()->width(),
				"visible form label fits horizontally",QString("%1: x=%2 width=%3 viewport=%4").arg(label->text()).arg(position.x()).arg(label->width()).arg(scroll->viewport()->width()));
		} } }
		expect(status->parentWidget() == &dialog && dialog.rect().contains(status->geometry())
			&& status->height() >= status->heightForWidth(status->width()),"validation is fully visible outside the scrolling controls");
		expect(preview->width() >= 240 && preview->height() >= 240,"useful preview survives scaling");
		dialog.accept(); expect(applications == 1 && dialog.result() == QDialog::Accepted && bytes(source) == before,"one accepted candidate, source untouched until publication");
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	request.points = {{64,48,80,true}};
	for (int i = 0; i < 3; ++i) {
		LevelWrapDialog cancelled(source,request); cancelled.show();
		if (i == 1) { until([&] { return cancelled.isReady(); }); }
		cancelled.reject();
	}
	expect(bytes(source) == before && source.undoStack.isEmpty(),"closing pending and ready dialogs leaves source and undo untouched");
}
void shellChecks(const QString& directory)
{
	QString error; LevelMapDocument source; tests::createGeometryFixture(2,&source,&error);
	const auto path = QDir(directory).filePath("source.map"); expect(saveLevelMapAs(source,path).succeeded(),"save shell fixture");
	ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
	const auto trigger = [&](const char* name) { auto* action = shell.findChild<QAction*>(QString::fromLatin1(name)); if (expect(action && action->isEnabled(),name)) { action->trigger(); } };
	trigger("shell.mode.levels"); auto* top = shell.findChild<MapViewport*>("mapViewport"); auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
	if (!expect(top && camera && until([&] { return shell.levelDocument().brushes.size() == 2 && camera->hasMesh(); }),"shell views ready")) { return; }
	trigger("map.selectAll"); const auto before = bytes(shell.levelDocument());
	trigger("map.wrapMerge"); expect(shell.levelDocument().brushes.size() == 1 && shell.levelDocument().undoStack.size() == 1,"immediate whole-object command wraps gap");
	trigger("map.undo"); expect(bytes(shell.levelDocument()) == before,"immediate whole wrap exact undo");
	trigger("map.clipTool"); LevelClipState plane; plane.points = {{128,0,0,true}}; top->setClipState(plane);
	trigger("map.wrapMerge");
	expect(shell.levelDocument().brushes.size() == 1 && shell.levelDocument().brushes[0].maxs.x == 128,"standalone wrap includes actual clipper points with fallback disabled");
	trigger("map.undo"); expect(bytes(shell.levelDocument()) == before,"standalone clipper-point wrap exact undo"); top->setClipMode(false);
	trigger("map.vertexTool"); top->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Vertex,{0,1,2,4}}}); top->componentPicksChanged(4);
	const auto picks = top->pickedComponents(); trigger("map.wrapMerge");
	expect(shell.levelDocument().brushes.size() == 3 && shell.levelDocument().undoStack.size() == 1 && shell.levelDocument().selection.size() == 3,"immediate component command adds one hull and preserves parents");
	expect(samePicks(top->pickedComponents(),picks) && camera->pickedComponents() == top->pickedComponentHandles(),"component picks retained across views");
	trigger("map.undo"); expect(bytes(shell.levelDocument()) == before,"component command exact undo");
	top->setPickedComponents(picks); top->componentPicksChanged(4);
	// Change the component selection while its numeric dialog is preparing. The cached hull must not publish.
	QTimer::singleShot(0,&shell,[&] {
		auto* dialog = static_cast<LevelWrapDialog*>(shell.findChild<QDialog*>("levelWrapDialog"));
		if (!expect(dialog && until([&] { return dialog->isReady(); }),"shell numeric dialog ready")) { if (dialog) { dialog->reject(); } return; }
		top->setPickedComponents({{{LevelMapSelectionKind::QuakeBrush,0},LevelBrushComponent::Vertex,{0,1,2,7}}}); top->componentPicksChanged(4);
		dialog->accept(); expect(dialog->isVisible() && !dialog->findChild<QLabel*>("wrapStatus")->text().isEmpty(),"stale component snapshot refused visibly"); dialog->reject();
	}); trigger("map.wrapMergePoints");
	expect(bytes(shell.levelDocument()) == before && shell.levelDocument().undoStack.isEmpty(),"stale dialog cannot modify document or history");
	top->setPickedComponents(picks); top->componentPicksChanged(4);
	QTimer::singleShot(0,&shell,[&] {
		auto* dialog = static_cast<LevelWrapDialog*>(shell.findChild<QDialog*>("levelWrapDialog"));
		if (expect(dialog && until([&] { return dialog->isReady(); }),"fresh numeric dialog ready")) { dialog->accept(); } else if (dialog) { dialog->reject(); }
	}); trigger("map.wrapMergePoints");
	expect(shell.levelDocument().brushes.size() == 3 && shell.levelDocument().undoStack.size() == 1 && samePicks(top->pickedComponents(),picks),"fresh numeric dialog publishes one undo and retains picks");
	bool shortcut = false; for (const auto& key : netRadiantCustomLevelControls().keys) { shortcut |= key.commandId == "map.wrapMerge" && key.keys.contains("Ctrl+U"); }
	expect(shortcut,"NRC Ctrl+U maps to standalone wrap");
	for (const auto& key : familiarLevelControls("netradiant").keys) {
		expect(key.commandId != "map.wrapMerge" || key.keys.isEmpty(),"unaudited NetRadiant baseline does not inherit NRC shortcut");
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
	dialogChecks(app,qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE")); shellChecks(temp.path()); return ok ? 0 : 1;
}
