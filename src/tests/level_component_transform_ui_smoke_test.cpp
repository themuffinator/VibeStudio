#include "app/application_shell.h"
#include "app/level_component_transform_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>
using namespace vibestudio;
namespace {
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context,const char* text,const char*,int) const override {
		if (QByteArray(context) != "vibestudio::LevelComponentTransformDialog" && QByteArray(context) != "LevelComponentTransformDialog") { return {}; }
		const auto source = QString::fromUtf8(text); return QStringLiteral("[%1%2]").arg(source,QString(source.size()/3,'~'));
	}
};
}
int main(int argc,char** argv)
{
	// Semantic controls and QWidget::render on owned offscreen widgets only.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true);
	bool ok = true; QString error;
	const auto expect = [&](bool value,const char* label,const QString& detail = {}) { if (!value) { ok = false; std::cerr << label << ": " << detail.toStdString() << '\n'; } return value; };
	expect(!settings.levelComponentUvLock(),"component UV lock default"); settings.setLevelComponentUvLock(true);
	expect(StudioSettings().levelComponentUvLock(),"component UV setting persists"); settings.setLevelComponentUvLock(false);
	expect(settings.levelComponentUvFit(),"fitted UV lock fallback is on by default");
	for (int scale : {100,200}) {
		applyStudioTheme(app,studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark,UiDensity::Standard,scale));
		Expansion expansion; if (scale == 200) { app.installTranslator(&expansion); }
		LevelComponentTransformRequest initial; initial.operation = LevelComponentTransformKind::Rotate; initial.amount = 15;
		LevelComponentTransformDialog dialog(initial,{12,24,36,true});
		if (scale == 200) { dialog.setLayoutDirection(Qt::RightToLeft); dialog.resize(1550,1100); }
		dialog.show(); app.processEvents();
		auto* operation = dialog.findChild<QComboBox*>("componentOperation"); auto* apply = dialog.findChild<QPushButton*>("componentTransformApply");
		expect(operation && apply && apply->isVisible() && dialog.rect().contains(apply->mapTo(&dialog,apply->rect().center())),"visible action at both scales");
		for (auto* field : dialog.findChildren<QDoubleSpinBox*>()) {
			auto* accessible = QAccessible::queryAccessibleInterface(field);
			expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && field->focusPolicy() != Qt::NoFocus && field->layoutDirection() == Qt::LeftToRight,"accessible numeric control",field->objectName());
		}
		for (int mode = 0; mode < 4; ++mode) { operation->setCurrentIndex(mode); expect(static_cast<int>(dialog.request().operation) == mode,"all transform pages select correct request"); }
		auto* uvLock = dialog.findChild<QCheckBox*>("componentUvLock"); auto* uvFit = dialog.findChild<QCheckBox*>("componentUvFit");
		if (expect(uvLock && uvFit,"UV lock and fit options")) {
			expect(!uvFit->isEnabled() && !dialog.request().fitUvLock,"fit option waits for the lock");
			uvLock->setChecked(true); uvFit->setChecked(true);
			expect(uvFit->isEnabled() && dialog.request().textureLock && dialog.request().fitUvLock && !uvFit->accessibleName().isEmpty(),"fit option follows the lock");
			uvLock->setChecked(false); expect(!dialog.request().fitUvLock,"no fit without the lock");
		}
		operation->setCurrentIndex(0);
		dialog.findChild<QDoubleSpinBox*>("componentDeltaX")->setValue(64000);
		expect(dialog.request().delta.x == 64000 && dialog.findChild<QDoubleSpinBox*>("componentPivotX")->maximum() == 32768,"movement spans valid world endpoints without widening pivot bounds");
		operation->setCurrentIndex(1);
		dialog.findChild<QCheckBox*>("componentCustomPivot")->setChecked(true);
		dialog.findChild<QDoubleSpinBox*>("componentPivotX")->setValue(7);
		expect(dialog.request().pivot.valid && dialog.request().pivot.x == 7 && dialog.request().amount == 15,"custom pivot and angle");
		dialog.setApplyHandler([](const auto&,QString* problem) { *problem = QStringLiteral("Geometry refused; adjust the transform."); return false; });
		apply->click(); expect(dialog.isVisible() && !dialog.findChild<QLabel*>("componentTransformError")->text().isEmpty(),"failed transform keeps editable dialog");
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_COMPONENT_UI_EVIDENCE");
		if (!evidence.isEmpty()) { QImage rendered(dialog.size(),QImage::Format_ARGB32_Premultiplied); rendered.fill(Qt::transparent); dialog.render(&rendered); expect(rendered.save(QDir(evidence).filePath(QStringLiteral("components-%1.png").arg(scale))),"owned widget render"); }
		dialog.setApplyHandler({}); apply->click(); expect(dialog.result() == QDialog::Accepted,"successful dialog closes");
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	applyStudioTheme(app,studioThemeTokens(StudioTheme::Dark,UiDensity::Standard,100));
	ApplicationShell shell; LevelMapCreateRequest create; create.starterRoom = true;
	if (!expect(shell.createLevelDocument(create,&error),"shell fixture",error)) { return 1; }
	const auto trigger = [&](const char* name) { auto* action = shell.findChild<QAction*>(QString::fromLatin1(name)); if (expect(action && action->isEnabled(),name)) { action->trigger(); } };
	trigger("map.selectAll"); trigger("map.vertexTool");
	auto* view = shell.findChild<MapViewport*>("mapViewport"); if (!expect(view,"plan viewport")) { return 1; }
	QVector<QPair<LevelMapSelectionRef,LevelMapVec3>> picks;
	const int brushId = shell.levelDocument().brushes.first().id;
	for (const auto& handle : view->componentHandles()) { if (handle.object.objectId == brushId) { picks.append({handle.object,handle.position}); } }
	expect(picks.size() == 8,"first brush vertices"); view->setPickedComponentPositions(picks);
	const auto original = serializeLevelMap(shell.levelDocument()).bytes;
	LevelMapRotationRequest rotate; rotate.axis = 2; rotate.degrees = 15;
	QTimer::singleShot(0,&shell,[&] { view->setPickedComponentPositions({}); });
	expect(!shell.applyLevelRotation(rotate,&error) && serializeLevelMap(shell.levelDocument()).bytes == original,"stale worker component selection refused",error);
	expect(!shell.applyLevelRotation(rotate,&error) && serializeLevelMap(shell.levelDocument()).bytes == original,"empty component picks never rotate objects",error);
	view->setPickedComponentPositions(picks);
	expect(shell.applyLevelRotation(rotate,&error) && view->pickedComponentCount() == 8,"component rotation and selection continuity",error);
	expect(shell.levelDocument().undoStack.size() == 1,"one component undo step");
	trigger("map.undo"); expect(serializeLevelMap(shell.levelDocument()).bytes == original,"shell exact undo");
	view->setPickedComponentPositions(picks);
	bool stale = false; QTimer drive; drive.setInterval(20);
	QObject::connect(&drive,&QTimer::timeout,&shell,[&] {
		auto* dialog = qobject_cast<LevelComponentTransformDialog*>(QApplication::activeModalWidget()); if (!dialog) { return; }
		drive.stop(); view->setPickedComponentPositions({}); dialog->findChild<QPushButton*>("componentTransformApply")->click();
		stale = dialog->isVisible() && dialog->findChild<QLabel*>("componentTransformError")->text().contains("changed"); dialog->reject();
	});
	QTimer watchdog; watchdog.setSingleShot(true); QObject::connect(&watchdog,&QTimer::timeout,&shell,[] { if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) { modal->reject(); } });
	// Transform Components… (Rotate Selection… now opens the modeless Euler
	// window) is enabled once the shell has seen the picks.
	view->componentPicksChanged(static_cast<int>(picks.size()));
	drive.start(); watchdog.start(15000); trigger("map.transformComponents"); drive.stop(); watchdog.stop();
	expect(stale && serializeLevelMap(shell.levelDocument()).bytes == original,"numeric component dialog rejects changed picks");
	expect(shell.saveLevelDocument(temp.filePath("saved.map"),false,&error),"save tested shell",error); expect(shell.close(),"clean shell close");
	return ok ? 0 : 1;
}
