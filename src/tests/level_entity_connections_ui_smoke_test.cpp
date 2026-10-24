#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_prefab_test_helpers.h"
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
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& error = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << error.toStdString() << std::endl; }
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
	// Only owned offscreen Qt methods and widget render targets. No native input.
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
	settings.setCurrentProjectPath(temp.path());
	auto project = defaultProjectManifest(temp.path()); project.gameKey = "quake"; QString error;
	if (!expect(saveProjectManifest(project,&error),"project fixture",error)) { return 1; }
	auto original = tests::prefabFixture(); original.replace("\"target\" \"gate\"","\"target\" \"fired\"");
	const auto path = temp.filePath("connections.map"); QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) { return 1; } file.close();
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("vibestudio-default")}) {
		for (int scale : {100,200}) {
			std::cout << "Connection UI " << profile.toStdString() << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
				action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
				auto* camera = shell.findChild<ModelViewport*>("mapPreview3D");
				if (!expect(camera && until([&] { return shell.levelDocument().sourcePath == path && camera->hasMesh(); }),"map ready")) { return 1; }
				auto* view = shell.findChild<MapViewport*>(); auto* connect = action("map.connectEntities"); auto* kill = action("map.killConnectEntities");
				auto* tool = shell.findChild<QPushButton*>("levelToolsRow-map.killConnectEntities");
				if (!expect(view && connect && kill && tool,"registered action and Tools route")) { return 1; }
				expect(!kill->text().isEmpty() && !kill->toolTip().isEmpty() && !tool->accessibleName().isEmpty()
					&& tool->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(tool),"accessible keyboard-focusable connection control");
				const bool native = profile == "netradiant-custom";
				if (native) { expect(connect->shortcuts() == QList<QKeySequence>{QKeySequence("Ctrl+K")} && kill->shortcuts() == QList<QKeySequence>{QKeySequence("Shift+K")},"NRC connection keys"); }
				else { expect(kill->shortcuts().isEmpty(),"other profiles do not inherit NRC kill key"); }
				const auto pick = [&](const QVector<LevelMapSelectionRef>& refs) { view->setSelectionSet(refs); view->selectionSetChanged(refs); view->setFocus(Qt::OtherFocusReason); };
				const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
				const auto property = [&](int id, const char* key) { return tests::prefabProperty(shell.levelDocument().entities[id],QLatin1String(key)); };
				pick({{LevelMapSelectionKind::Entity,2}}); expect(!connect->isEnabled() && !kill->isEnabled(),"single selection disables connection");
				pick({{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}}); expect(!connect->isEnabled() && !kill->isEnabled(),"two children of one entity are ineligible");
				pick({{LevelMapSelectionKind::Entity,2},{LevelMapSelectionKind::Entity,3},{LevelMapSelectionKind::Entity,1}});
				expect(connect->isEnabled() == !native && !kill->isEnabled(),"profile-specific multi-source eligibility");
				pick({{LevelMapSelectionKind::Entity,2},{LevelMapSelectionKind::QuakeBrush,0}});
				const auto selection = shell.levelDocument().selection; const auto depth = shell.levelDocument().undoStack.size();
				connect->trigger();
				if (!expect(property(1,"targetname") == (native ? "fired" : "gate") && property(2,"target") == (native ? "fired" : "gate")
					&& shell.levelDocument().selection == selection && shell.levelDocument().undoStack.size() == depth+1,"UI connection precedence and single edit",shell.statusBar()->currentMessage())) { return 1; }
				const auto connected = bytes();
				if (native) { connect->trigger(); expect(bytes() == connected && shell.levelDocument().undoStack.size() == depth+1,"native redundant action has no history"); }
				action("map.undo")->trigger(); expect(bytes() == original,"UI exact undo");
				action("map.redo")->trigger(); expect(bytes() == connected,"UI exact redo"); action("map.undo")->trigger();
				pick(selection); expect(kill->isEnabled() && tool->isEnabled(),"project Quake enables KillConnect on a Q3-format map");
				tool->click();
				expect(property(1,"targetname") == "outside" && property(2,"target") == "fired" && property(2,"killtarget") == "outside","Tools route invokes real KillConnect",shell.statusBar()->currentMessage());
				action("map.undo")->trigger(); expect(bytes() == original,"KillConnect exact undo");
				project.gameKey = "quake2"; expect(saveProjectManifest(project,&error),"switch project game",error); pick(selection);
				expect(!kill->isEnabled() && !tool->isEnabled() && connect->isEnabled(),"Quake II project disables only KillConnect");
				kill->trigger(); expect(bytes() == original,"disabled KillConnect is inert");
				project.gameKey = "quake"; expect(saveProjectManifest(project,&error),"restore project game",error); pick(selection);
				if (native && scale == 100) {
					bool changed = false;
					QTimer::singleShot(0,&shell,[&] { project.gameKey = "quake2"; changed = saveProjectManifest(project); });
					kill->trigger(); expect(changed && bytes() == original && shell.statusBar()->currentMessage().contains("changed"),"pending result rejects changed game context",shell.statusBar()->currentMessage());
					project.gameKey = "quake"; saveProjectManifest(project); pick(selection);
					auto* profiles = shell.findChild<QComboBox*>("editorProfileCombo");
					if (!expect(profiles != nullptr,"profile selector")) { return 1; }
					QTimer::singleShot(0,&shell,[&] { profiles->setCurrentIndex(profiles->findData("trenchbroom")); });
					connect->trigger(); expect(bytes() == original && shell.statusBar()->currentMessage().contains("changed"),"pending result rejects changed profile",shell.statusBar()->currentMessage());
					profiles->setCurrentIndex(profiles->findData(profile)); pick(selection);
					QTimer::singleShot(0,&shell,[&] { pick({{LevelMapSelectionKind::Entity,3}}); });
					connect->trigger(); expect(bytes() == original && shell.statusBar()->currentMessage().contains("changed"),"pending result rejects changed selection",shell.statusBar()->currentMessage());
				}
				if (native && scale == 200) {
					const auto capture = qEnvironmentVariable("VIBESTUDIO_CONNECTION_CAPTURE");
					if (!capture.isEmpty()) { QImage image(shell.size(),QImage::Format_ARGB32_Premultiplied); shell.render(&image); expect(image.save(capture),"owned Qt render evidence"); }
				}
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	return ok ? 0 : 1;
}
