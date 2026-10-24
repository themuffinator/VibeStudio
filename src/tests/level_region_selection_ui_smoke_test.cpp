#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* label, const QString& detail = {}) {
	if (!value) { ok = false; std::cerr << "FAIL: " << label << ": " << detail.toStdString() << '\n'; } return value;
}
bool until(const std::function<bool()>& ready) {
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) { QEventLoop loop; QTimer::singleShot(15,&loop,&QEventLoop::quit); loop.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
class Expansion final : public QTranslator {
	QString translate(const char*, const char* text, const char*, int) const override {
		const auto source = QString::fromUtf8(text); return QStringLiteral("[%1%2]").arg(source,QString(source.size()/3,'~'));
	}
};
}
int main(int argc, char** argv) {
	// Owned offscreen widgets and semantic calls; no desktop input or capture.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	LevelMapDocument map; LevelMapCreateRequest create; create.starterRoom = false; createLevelMap(create,&map);
	addLevelMapBoxBrush(&map,{-32,-32,-32,true},{32,32,32,true},"studio/volume");
	addLevelMapBoxBrush(&map,{-8,-8,-8,true},{8,8,8,true},"studio/target");
	addLevelMapBoxBrush(&map,{-8,-8,100,true},{8,8,200,true},"studio/tall");
	const auto original = serializeLevelMap(map).bytes; const auto path = temp.filePath("region.map");
	QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) { return 1; } file.close();
	const QVector<LevelMapSelectionRef> source{{LevelMapSelectionKind::QuakeBrush,0}}, target{{LevelMapSelectionKind::QuakeBrush,1}};
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("vibestudio-default")}) {
		std::cout << "Region UI " << profile.toStdString() << std::endl;
		settings.setSelectedEditorProfileId(profile); settings.sync(); ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
		const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
		action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
		auto* top = shell.findChild<MapViewport*>("mapViewport");
		if (!expect(top && until([&] { return shell.levelDocument().sourcePath == path && top->isEnabled(); }),"map ready")) { return 1; }
		const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
		const auto select = [&](const QVector<LevelMapSelectionRef>& refs) { top->setSelectionSet(refs); top->selectionSetChanged(refs); top->setFocus(Qt::OtherFocusReason); };
		select(source);
		expect(action("map.selectInside")->isEnabled(),"region command enabled");
		if (profile == "trenchbroom") {
			expect(action("map.selectInside")->shortcuts().contains(QKeySequence("Ctrl+E")) && action("map.selectTouching")->shortcuts().contains(QKeySequence("Ctrl+T"))
				&& action("map.selectCompleteTall")->shortcuts().contains(QKeySequence("Ctrl+Shift+E")),"TB volume shortcuts");
			expect(action("map.selectInside")->shortcutContext() == Qt::WidgetWithChildrenShortcut && action("map.selectInside")->toolTip().contains("Removes"),"scoped shortcut exposes consuming policy");
		}
		for (const auto* tool : {"map.vertexTool","map.edgeTool","map.faceTool","map.clipTool"}) {
			action(tool)->trigger(); expect(!action("map.selectInside")->isEnabled(),"region command disabled during modal tool",QLatin1String(tool));
			action("map.selectInside")->trigger(); expect(bytes() == original,"modal region cannot delete objects"); action(tool)->trigger();
		}
		const auto beforeInside = shell.levelDocument().undoStack.size();
		action("map.selectInside")->trigger();
		expect(shell.levelDocument().selection == target,"GUI inside matches core",shell.statusBar()->currentMessage());
		const bool native = profile != "vibestudio-default";
		if (native) {
			expect(shell.levelDocument().brushes.size() == 2 && shell.levelDocument().undoStack.size() == beforeInside + 1,"inside consumes volumes once",shell.statusBar()->currentMessage());
			const auto changed = bytes(); action("map.undo")->trigger(); expect(bytes() == original && shell.levelDocument().selection == source,"GUI undo restores volumes and selection");
			action("map.redo")->trigger(); expect(bytes() == changed && shell.levelDocument().selection == target,"GUI redo restores matches"); action("map.undo")->trigger();
		} else { expect(bytes() == original && shell.levelDocument().undoStack.isEmpty(),"default profile preserves geometry"); }
		select(source); action("map.selectTouching")->trigger();
		if (profile == "trenchbroom") { expect(shell.levelDocument().brushes.size() == 2,"TB touching consumes volume"); action("map.undo")->trigger(); }
		if (profile == "netradiant-custom") { expect(bytes() == original && shell.levelDocument().selection.size() == 2 && shell.levelDocument().selection.contains(source[0]),"NRC touching retains source"); }
		select(source); action("map.selectCompleteTall")->trigger();
		expect(shell.levelDocument().selection.contains({LevelMapSelectionKind::QuakeBrush,2}),"tall includes depth-separated object",shell.statusBar()->currentMessage());
		if (native) { action("map.undo")->trigger(); }
		select(source); bool cancelled = false;
		QTimer::singleShot(0,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) { cancelled = true; dialog->reject(); } });
		action("map.selectInside")->trigger(); expect(cancelled && bytes() == original && shell.levelDocument().selection == source,"GUI cancellation is atomic",shell.statusBar()->currentMessage());
		select(source); QTimer::singleShot(0,&shell,[&] { select(target); }); action("map.selectInside")->trigger();
		expect(bytes() == original && shell.levelDocument().selection == target,"selection change rejects worker result",shell.statusBar()->currentMessage());
		select(source); QTimer::singleShot(0,&shell,[&] { top->setFilteredObjects(target); }); action("map.selectInside")->trigger();
		expect(bytes() == original,"filter change rejects worker result",shell.statusBar()->currentMessage()); top->setFilteredObjects({});
		select(source); QTimer::singleShot(0,&shell,[&] { action("map.vertexTool")->trigger(); }); action("map.selectInside")->trigger();
		expect(bytes() == original,"tool change rejects worker result",shell.statusBar()->currentMessage()); action("map.vertexTool")->trigger();
	}
	QString error; LevelMapDocument large; tests::createGeometryFixture(16000,&large,&error); setLevelMapSelection(&large,source);
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectRegion; request.region.policy = LevelRegionSelectionPolicy::TrenchBroom;
	for (int scale : {100,200}) {
		settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
		Expansion expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
		bool inspected = false; int beats = 0; QElapsedTimer elapsed; elapsed.start(); qint64 previous = 0, gap = 0;
		QTimer pulse; pulse.setInterval(5); QObject::connect(&pulse,&QTimer::timeout,&app,[&] { const auto now = elapsed.elapsed(); gap = std::max(gap,now-previous); previous = now; ++beats; }); pulse.start();
		QTimer inspect; inspect.setInterval(10); QObject::connect(&inspect,&QTimer::timeout,&app,[&] {
			for (auto* widget : QApplication::topLevelWidgets()) {
				auto* dialog = dynamic_cast<LevelPlacementTaskDialog*>(widget); if (!dialog || inspected) { continue; }
				auto* phase = dialog->findChild<QLabel*>("levelPlacementTaskPhase");
				if (!phase || !phase->text().contains(levelPlacementPhaseName(LevelPlacementPhase::Selecting))) { continue; }
				inspected = true; dialog->show(); dialog->adjustSize();
				auto* bar = dialog->findChild<QProgressBar*>("levelPlacementTaskProgress"); auto* cancel = dialog->findChild<QPushButton*>("levelPlacementTaskCancel");
				expect(dialog->accessibleName() == dialog->windowTitle() && bar && bar->maximum() == 1000 && QAccessible::queryAccessibleInterface(bar)->role() == QAccessible::ProgressBar
					&& cancel && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleDescription().isEmpty(),"region worker accessibility");
				const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!capture.isEmpty()) { QDir().mkpath(capture); QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					expect(image.save(QDir(capture).filePath(QStringLiteral("region-%1.png").arg(scale))),"render owned region progress widget"); }
				cancel->click(); break;
			}
		}); inspect.start();
		const auto result = LevelPlacementTaskDialog::prepare(nullptr,large,request); inspect.stop(); pulse.stop();
		expect(inspected && beats > 0 && gap < 1000 && result.cancelled && result.regionReport.matches.isEmpty(),"responsive cancellable region worker",result.error);
		std::cout << scale << "% region: " << beats << " heartbeats, max gap " << gap << " ms\n";
		if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
	}
	return ok ? 0 : 1;
}
