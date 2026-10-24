#include "app/application_shell.h"
#include "app/level_gestures_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << std::endl; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override {
		const auto text = QString::fromUtf8(source); return QStringLiteral("[%1%2]").arg(text,QString(text.size()/3,'~'));
	}
};
}
int main(int argc, char** argv)
{
	// Owned offscreen Qt methods and render targets; no native input or capture.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	LevelMapDocument source; QString error;
	if (!expect(tests::createGeometryFixture(2,&source,&error),"geometry fixture",error)) { return 1; }
	setLevelMapSelection(&source,{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}});
	if (!expect(tieLevelMapSelectionToEntity(&source,"func_door",{{"targetname","duplicate"}},nullptr,&error),"named brush owner",error)) { return 1; }
	const auto original = serializeLevelMap(source).bytes;
	const auto path = temp.filePath("duplicate.map");
	QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) { return 1; } file.close();
	const QVector<LevelMapSelectionRef> selection{{LevelMapSelectionKind::QuakeBrush,0}};
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("vibestudio-default")}) {
		for (int scale : {100,200}) {
			std::cout << "Duplicate UI " << profile.toStdString() << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profile); settings.setEditorGestureOverrides(profile,{});
			settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
				auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				const auto views = shell.findChildren<MapViewport*>();
				if (!expect(views.size() == 3 && camera && until([&] { return shell.levelDocument().sourcePath == path && camera->hasMesh(); }),"map and views ready")) { return 1; }
				auto* duplicate = action("map.duplicateSelection"); auto* offset = action("map.duplicateWithOffset");
				auto* unique = action("map.duplicateUnique");
				expect(unique && !unique->toolTip().isEmpty() && !unique->text().isEmpty(),"discoverable unique command");
				if (profile == "netradiant-custom") {
					expect(unique->shortcuts() == QList<QKeySequence>{QKeySequence("Shift+Space")}
						&& duplicate->shortcuts() == QList<QKeySequence>{QKeySequence("Space")},"NRC has distinct unique shortcut");
				}
				const auto pick = [&](MapViewport* view) { view->setSelectionSet(selection); view->selectionSetChanged(selection); view->setFocus(Qt::OtherFocusReason); view->setGridSize(32); };
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto nudgePreference = [&](const QString& choice) {
					bool visited = false;
					QTimer::singleShot(0,&shell,[&] {
						auto* dialog = shell.findChild<LevelGesturesDialog*>("levelGesturesDialog");
						if (!expect(dialog != nullptr,"editing preferences dialog")) { return; }
						auto* field = dialog->findChild<QComboBox*>("gesture.editing.nudgeAfterDuplicate");
						auto* apply = dialog->findChild<QPushButton*>("gestureApply");
						if (!expect(field && apply && field->findData(choice) >= 0,"duplicate placement choice")) { dialog->reject(); return; }
						visited = true;
						expect(field->focusPolicy() != Qt::NoFocus && !field->accessibleName().isEmpty()
							&& !field->accessibleDescription().isEmpty() && QAccessible::queryAccessibleInterface(field),"accessible duplicate preference");
						field->setCurrentIndex(field->findData(choice));
						expect(apply->isEnabled(),"valid placement preference can apply"); apply->click();
						auto* tabs = dialog->findChild<QTabWidget*>("gestureTabs"); tabs->setCurrentIndex(tabs->count()-1);
						const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
						if (!captures.isEmpty()) {
							QDir().mkpath(captures); dialog->resize(scale == 100 ? 740 : 1300,scale == 100 ? 690 : 1050);
							app.processEvents(QEventLoop::ExcludeUserInputEvents);
							QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
							expect(image.save(QDir(captures).filePath(QStringLiteral("editing-%1-%2.png").arg(profile).arg(scale))),"render editing preference");
						}
						dialog->accept();
					});
					action("editor.gestures")->trigger(); expect(visited,"editing preference applied through shell");
				};
				const bool sourceNudge = profile == "vibestudio-default";
				for (int policy : {0,1}) {
					if (policy) { nudgePreference(sourceNudge ? "off" : "on"); }
					const bool nudged = policy ? !sourceNudge : sourceNudge;
					for (auto* view : views) {
						pick(view); action("map.clearRepeat")->trigger();
						if (!expect(view->isActivePane() && duplicate->isEnabled(),"active plan can duplicate")) { return 1; }
						const auto plane = view->projection();
						if (plane == MapViewportProjection::FrontXZ) { camera->setFocus(Qt::OtherFocusReason); }
						const double step = nudged ? 32 : 0;
						const LevelMapVec3 delta{plane == MapViewportProjection::SideZY ? 0 : step,
							plane == MapViewportProjection::TopXY ? -step : plane == MapViewportProjection::SideZY ? step : 0,
							plane == MapViewportProjection::TopXY ? 0 : -step,true};
						const auto matches = [&](int count, double factor) {
							LevelMapVec3 lo,hi;
							return shell.levelDocument().brushes.size() == count && shell.levelDocument().selection.size() == 1
								&& levelMapSelectionBounds(shell.levelDocument(),&lo,&hi)
								&& std::abs(lo.x-(-16+factor*delta.x)) < 1e-6 && std::abs(lo.y-(-16+factor*delta.y)) < 1e-6 && std::abs(lo.z-(-16+factor*delta.z)) < 1e-6;
						};
						if (view == views.first()) {
							const auto uniqueDepth = shell.levelDocument().undoStack.size(); unique->trigger();
							const auto& document = shell.levelDocument();
							if (!expect(matches(3,1) && document.entities.size() == 3 && document.brushes.last().entityId == 2
								&& document.selection.first().kind == LevelMapSelectionKind::Entity && document.undoStack.size() == uniqueDepth+1,
								"unique UI copies partial owner with profile placement",shell.statusBar()->currentMessage())) { return 1; }
							expect(std::any_of(document.entities.last().properties.cbegin(),document.entities.last().properties.cend(),[](const auto& p) {
								return p.key == "targetname" && p.value == "duplicate1";
							}),"unique UI renames target");
							const auto uniqueBytes = bytes(); action("map.repeat")->trigger();
							expect(matches(4,2) && shell.levelDocument().entities.size() == 4,"unique UI Repeat keeps owner semantics");
							action("map.undo")->trigger(); expect(bytes() == uniqueBytes,"unique UI Repeat undo");
							action("map.undo")->trigger(); expect(bytes() == original,"unique UI exact undo");
							action("map.redo")->trigger(); expect(bytes() == uniqueBytes,"unique UI exact redo");
							action("map.undo")->trigger(); pick(view); action("map.clearRepeat")->trigger();
						}
						const auto depth = shell.levelDocument().undoStack.size(); duplicate->trigger();
						if (!expect(matches(3,1) && shell.levelDocument().undoStack.size() == depth+1,"duplicate placement and one content step",shell.statusBar()->currentMessage())) { return 1; }
						const auto duplicated = bytes();
						action("map.repeat")->trigger();
						if (!expect(matches(4,2),"repeat preserves recorded duplicate displacement",shell.statusBar()->currentMessage())) { return 1; }
						action("map.undo")->trigger(); expect(bytes() == duplicated,"repeat undo exact bytes");
						action("map.undo")->trigger(); expect(bytes() == original,"duplicate undo exact bytes");
						action("map.redo")->trigger(); expect(bytes() == duplicated && matches(3,1),"duplicate redo exact placement");
						action("map.undo")->trigger();
					}
				}
				pick(views.first());
				for (const char* mode : {"map.vertexTool","map.edgeTool","map.faceTool"}) {
					action(mode)->trigger();
					const bool allowed = profile != "netradiant-custom";
					expect(duplicate->isEnabled() == allowed && offset->isEnabled() == allowed,"profile component-mode eligibility");
					expect(!unique->isEnabled(),"unique command requires objects in every profile");
					unique->trigger(); expect(bytes() == original,"disabled unique command is inert");
					if (!allowed) { duplicate->trigger(); expect(bytes() == original,"disabled component clone is inert"); }
					action(mode)->trigger();
					expect(duplicate->isEnabled() && offset->isEnabled(),"returning to objects restores duplicate actions");
					expect(unique->isEnabled(),"object mode restores unique command");
				}
				if (profile == "netradiant-custom" && scale == 100) {
					const auto depth = shell.levelDocument().undoStack.size(); bool changed = false;
					QTimer::singleShot(0,&shell,[&] { nudgePreference("off"); changed = true; });
					duplicate->trigger();
					expect(changed && bytes() == original && shell.levelDocument().undoStack.size() == depth
						&& shell.statusBar()->currentMessage().contains("changed"),"changing placement preference rejects pending duplicate",shell.statusBar()->currentMessage());
				}
				expect(bytes() == original,"duplicate workflow leaves saved source unchanged after undo");
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	return ok ? 0 : 1;
}
