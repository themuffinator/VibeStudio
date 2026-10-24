#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_scene.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
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
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* text,const char*,int) const override {
		const auto source = QString::fromUtf8(text); return QStringLiteral("[%1%2]").arg(source,QString(source.size()/3,'~'));
	}
};
}
int main(int argc, char** argv) {
	// Owned offscreen widgets and semantic calls. No OS input or screen capture.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	LevelMapDocument map; QString group, error; tests::createGeometryFixture(3,&map,&error);
	createLevelSceneNode(&map,LevelSceneNodeKind::Group,"Pair",{},&group,&error);
	assignLevelSceneObjects(&map,group,{"brush:0","brush:1"},&error);
	const auto path = temp.filePath("objects.map"); const auto original = serializeLevelMap(map).bytes;
	QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) { return 1; } file.close();
	const QVector<LevelMapSelectionRef> pair{{LevelMapSelectionKind::QuakeBrush,0},{LevelMapSelectionKind::QuakeBrush,1}};
	const QVector<LevelMapSelectionRef> outside{{LevelMapSelectionKind::QuakeBrush,2}};
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("vibestudio-default")}) {
		if (QCoreApplication::arguments().contains(QStringLiteral("--dense-only"))) { break; }
		for (int scale : {100,200}) {
			std::cout << "Object selection UI " << profile.toStdString() << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
			const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
			action("shell.mode.levels")->trigger(); action("map.layout.four-views")->trigger();
			auto* top = shell.findChild<MapViewport*>("mapViewport");
			if (!expect(top && until([&] { return shell.levelDocument().sourcePath == path && top->isEnabled(); }),"map ready")) { return 1; }
			auto* siblings = action("map.selectSiblings"); auto* lines = action("map.selectSourceLines");
			if (!expect(siblings && lines,"registered selection commands")) { return 1; }
			const auto bytes = [&] { return serializeLevelMap(shell.levelDocument()).bytes; };
			const auto select = [&](const auto& refs) { top->setSelectionSet(refs); top->selectionSetChanged(refs); top->setFocus(Qt::OtherFocusReason); };
			const bool tb = profile == "trenchbroom";
			if (tb) {
				expect(siblings->shortcuts().contains(QKeySequence("Ctrl+B")) && siblings->shortcutContext() == Qt::WidgetWithChildrenShortcut,"TB sibling shortcut is scoped to Levels");
				expect(action("compiler.run")->toolTip().contains("Ctrl+Alt+B") && !action("compiler.run")->shortcuts().contains(QKeySequence("Ctrl+B")),"compiler avoids TB sibling conflict");
			}
			select(outside); const auto depth = shell.levelDocument().undoStack.size(); siblings->trigger();
			expect(shell.levelDocument().selection.size() == 3 && bytes() == original && levelMapHistoryMatchesSaved(shell.levelDocument(),static_cast<int>(shell.levelDocument().undoStack.size())),"siblings match core without dirtying map",shell.statusBar()->currentMessage());
			expect(shell.levelDocument().undoStack.size() == depth + (tb ? 1 : 0),"profile selection history policy");
			if (tb) { action("map.undo")->trigger(); expect(shell.levelDocument().selection == outside,"undo siblings restores previous pick"); action("map.redo")->trigger(); }
			select(pair);
			for (const auto* tool : {"map.vertexTool","map.edgeTool","map.faceTool","map.clipTool"}) {
				action(tool)->trigger(); expect(!siblings->isEnabled() && !lines->isEnabled(),"selection commands disabled during modal tool",QLatin1String(tool));
				action(tool)->trigger();
			}
			select(outside);
			const auto sourceLine = shell.levelDocument().brushes[0].startLine;
			bool inspected = false;
			QTimer::singleShot(0,&shell,[&] {
				auto* dialog = shell.findChild<QDialog*>("levelSourceLineDialog");
				if (!expect(dialog != nullptr,"source line dialog")) { return; }
				auto* field = dialog->findChild<QLineEdit*>("levelSourceLines");
				auto* buttons = dialog->findChild<QDialogButtonBox*>(); auto* accept = buttons->button(QDialogButtonBox::Ok);
				auto* problem = dialog->findChild<QLabel*>("levelSourceLineProblem");
				expect(field && accept && problem && !accept->isEnabled(),"empty line input cannot submit");
				field->setText("0, invalid"); expect(!accept->isEnabled() && !problem->text().isEmpty(),"invalid line input shows inline error");
				auto number = QString::number(sourceLine);
				if (scale == 200) { for (auto& digit : number) { digit = QChar(0x0660 + digit.digitValue()); } }
				field->setText(number);
				expect(accept->isEnabled() && problem->text().isEmpty() && field->focusPolicy() != Qt::NoFocus
					&& field->accessibleDescription().contains("last open") && QAccessible::queryAccessibleInterface(field)->role() == QAccessible::EditableText,
					"source field exposes focus, role, valid state and snapshot semantics");
				bool buddy = false; for (auto* label : dialog->findChildren<QLabel*>()) { buddy |= label->buddy() == field; }
				expect(buddy && dialog->accessibleName() == dialog->windowTitle() && accept->isDefault(),"keyboard mnemonic and default action");
				dialog->adjustSize();
				expect(dialog->rect().contains(field->geometry()) && field->width() > 80,"line input remains visible with scaling and expansion");
				const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (tb && !capture.isEmpty()) {
					QDir().mkpath(capture); QImage image(dialog->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog->render(&image);
					expect(image.save(QDir(capture).filePath(QStringLiteral("source-lines-%1.png").arg(scale))),"render owned source line dialog");
				}
				inspected = true; accept->click();
			});
			lines->trigger(); expect(inspected && shell.levelDocument().selection == pair && bytes() == original,"source lines select closed group atomically",shell.statusBar()->currentMessage());
			select(outside); const auto before = shell.levelDocument().undoStack.size(); bool cancelled = false;
			QTimer::singleShot(0,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelSourceLineDialog")) { cancelled = true; dialog->reject(); } });
			lines->trigger(); expect(cancelled && shell.levelDocument().selection == outside && shell.levelDocument().undoStack.size() == before,"cancel input has no effect");
			QTimer::singleShot(0,&shell,[&] {
				if (auto* dialog = shell.findChild<QDialog*>("levelSourceLineDialog")) {
					select(pair); dialog->findChild<QLineEdit*>("levelSourceLines")->setText(QString::number(sourceLine));
					dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
				}
			});
			lines->trigger(); expect(shell.levelDocument().selection == pair && shell.statusBar()->currentMessage().contains("changed"),"changed selection invalidates input dialog");
			select(outside); cancelled = false;
			QTimer::singleShot(0,&shell,[&] { if (auto* dialog = shell.findChild<QDialog*>("levelPlacementTaskDialog")) { cancelled = true; dialog->reject(); } });
			siblings->trigger(); expect(cancelled && shell.levelDocument().selection == outside,"cancel worker does not publish selection");
			QTimer::singleShot(0,&shell,[&] { select(pair); if (tb) { select(outside); } }); siblings->trigger();
			expect(shell.levelDocument().selection == (tb ? outside : pair) && shell.statusBar()->currentMessage().contains("changed"),"worker rejects stale picks including TB roundtrip");
			select(outside); QTimer::singleShot(0,&shell,[&] { top->setFilteredObjects(pair); }); siblings->trigger();
			expect(shell.levelDocument().selection == outside && shell.statusBar()->currentMessage().contains("changed"),"filter change invalidates worker result");
			top->setFilteredObjects({});
			select(pair); action("map.openGroup")->trigger(); select(QVector<LevelMapSelectionRef>{pair[0]}); siblings->trigger();
			expect(shell.levelDocument().selection == pair,"siblings obey open group");
			action("map.closeAllGroups")->trigger(); select(QVector<LevelMapSelectionRef>{});
			expect(!siblings->isEnabled() && lines->isEnabled(),"empty selection disables siblings but permits source query");
			expect(bytes() == original && levelMapHistoryMatchesSaved(shell.levelDocument(),static_cast<int>(shell.levelDocument().undoStack.size())),"selection workflow preserves native bytes and saved state");
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	LevelMapDocument dense; tests::createGeometryFixture(100000,&dense,&error); setLevelMapSelection(&dense,outside);
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectObjects; request.objectSelection.undoSelection = true;
	for (int scale : {100,200}) {
		settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
		Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
		QElapsedTimer elapsed; elapsed.start(); qint64 previous = 0, gap = 0; int beats = 0; bool inspected = false;
		QTimer pulse; pulse.setInterval(5); QObject::connect(&pulse,&QTimer::timeout,&app,[&] {
			const auto now = elapsed.elapsed(); gap = std::max(gap,now-previous); previous = now; ++beats;
		}); pulse.start();
		QTimer inspect; inspect.setInterval(10); QObject::connect(&inspect,&QTimer::timeout,&app,[&] {
			for (auto* widget : QApplication::topLevelWidgets()) {
				auto* dialog = dynamic_cast<LevelPlacementTaskDialog*>(widget); if (!dialog || inspected) { continue; }
				auto* phase = dialog->findChild<QLabel*>("levelPlacementTaskPhase");
				auto* bar = dialog->findChild<QProgressBar*>("levelPlacementTaskProgress");
				if (!phase || !bar || bar->maximum() != 1000 || !phase->text().contains(levelPlacementPhaseName(LevelPlacementPhase::SelectingObjects))) { continue; }
				auto* cancel = dialog->findChild<QPushButton*>("levelPlacementTaskCancel"); inspected = true;
				expect(cancel && cancel->focusPolicy() != Qt::NoFocus && !cancel->accessibleDescription().isEmpty()
					&& QAccessible::queryAccessibleInterface(bar)->role() == QAccessible::ProgressBar && dialog->accessibleName() == dialog->windowTitle(),"dense object selection exposes accessible progress and cancel");
				cancel->click(); break;
			}
		}); inspect.start();
		const auto result = LevelPlacementTaskDialog::prepare(nullptr,dense,request); inspect.stop(); pulse.stop();
		expect(inspected && beats > 0 && gap < 1000 && result.cancelled && result.objectSelectionReport.selection.isEmpty()
			&& dense.selection == outside && dense.undoStack.isEmpty(),"100,000-brush query stays responsive and cancels atomically",result.error);
		std::cout << scale << "% dense selection: " << beats << " heartbeats, maximum gap " << gap << " ms" << std::endl;
		const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!capture.isEmpty()) {
			QDir().mkpath(capture); QFile evidence(QDir(capture).filePath(QStringLiteral("dense-selection-%1.json").arg(scale)));
			const QJsonObject metrics{{"brushes",100000},{"scale",scale},{"heartbeats",beats},{"maximumGapMs",gap},
				{"progressInspected",inspected},{"cancelled",result.cancelled},{"selectionUnchanged",dense.selection == outside}};
			expect(evidence.open(QIODevice::WriteOnly) && evidence.write(QJsonDocument(metrics).toJson()) > 0,"record dense selection responsiveness");
		}
		if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
	}
	return ok ? 0 : 1;
}
