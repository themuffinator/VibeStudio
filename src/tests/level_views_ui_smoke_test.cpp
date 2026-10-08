#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QListWidget>
#include <QProcess>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *label, const QString &detail = {})
{
	if (!condition) {
		std::cerr << label << ": " << detail.toStdString() << '\n';
	}
	return condition;
}
bool until(const std::function<bool()> &ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop;
		QTimer::singleShot(20, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return ready();
}
void settle()
{
	QEventLoop loop;
	QTimer::singleShot(60, &loop, &QEventLoop::quit);
	loop.exec();
}
class Expansion final : public QTranslator
{
  public:
	QString translate(const char *, const char *source, const char *, int) const override
	{
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 3, QLatin1Char('~')));
	}
};
} // namespace

int main(int argc, char **argv)
{
	// Direct Qt services, signals and QWidget::render only. No injected input,
	// native screen capture, real settings or game installation writes.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	const auto settingsPath = temp.filePath(QStringLiteral("settings.ini"));
	StudioSettings::setOverrideFilePath(settingsPath);
	StudioSettings settings;
	settings.setReducedMotion(true);
	settings.setRestoreSession(false);
	settings.setSelectedEditorProfileId(QStringLiteral("netradiant-custom"));
	bool ok = expect(settings.levelViewLayoutPreference() == QStringLiteral("profile"), "default follows profile");
	ok &= expect(!settings.setLevelViewLayoutPreference(QStringLiteral("invalid")), "invalid preference refused");
	LevelMapDocument source;
	QString error;
	ok &= expect(tests::createMaterialFixture(temp.path(), &source, &error), "original material/model fixture", error);
	const auto mapPath = temp.filePath(QStringLiteral("fixture.map"));
	ok &= expect(tests::putMaterialFile(mapPath, serializeLevelMap(source).bytes), "fixture map written");
	MapViewport a, b;
	a.resize(600, 400);
	b.resize(500, 350);
	a.setDocument(source);
	b.setProjection(MapViewportProjection::FrontXZ);
	b.synchronizeSceneFrom(a, true);
	b.zoomIn();
	const double originalZoom = b.zoom();
	const QPointF originalCenter = b.viewPointFor(0, 0);
	a.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}});
	a.hideSelection();
	b.synchronizeSceneFrom(a);
	ok &= expect(b.isHidden(LevelMapSelectionKind::QuakeBrush, 0) && b.displayDocument().brushes.size() == 1,
				 "visibility shared with projection");
	ok &= expect(b.zoom() == originalZoom && b.viewPointFor(0, 0) == originalCenter, "scene synchronization keeps pan and zoom");
	a.setSelectionSet({{LevelMapSelectionKind::QuakeBrush, 0}});
	b.synchronizeSceneFrom(a);
	ok &= expect(b.hiddenCount() == 0 && b.selectionSet() == a.selectionSet(), "selection reveal shared");
	a.setLeakTrail({{0, 0, 0, true}, {32, 64, 96, true}});
	b.synchronizeSceneFrom(a);
	ok &= expect(b.hasLeakTrail(), "leak trail shared");
	a.clearDocument();
	b.synchronizeSceneFrom(a);
	ok &= expect(!b.hasDocument() && !b.hasLeakTrail() && b.hiddenCount() == 0 && b.targetLinkCount() == 0,
				 "clear drops previous scene state");

	settings.sync();
	if (argc > 1) {
		const auto binary = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
		auto cli = [&](const QStringList &arguments, int expected, const QString &store = {}) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(binary, QStringList{QStringLiteral("--cli"), QStringLiteral("--settings-file"),
											  store.isEmpty() ? settingsPath : store, QStringLiteral("editor"), QStringLiteral("layout")} +
									  arguments + QStringList{QStringLiteral("--json")});
			const bool finished = process.waitForFinished(30000);
			const auto bytes = process.readAllStandardOutput();
			ok &= expect(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected, "CLI layout result",
						 QString::fromUtf8(bytes + process.readAllStandardError()));
			return QJsonDocument::fromJson(bytes).object();
		};
		ok &= expect(cli({QStringLiteral("four-views")}, 0).value(QStringLiteral("effectiveLayout")) == QStringLiteral("four-views"),
					 "CLI selects four views");
		ok &= expect(cli({}, 0).value(QStringLiteral("preference")) == QStringLiteral("four-views"), "CLI reads saved preference");
		cli({QStringLiteral("unknown")}, 2);
		cli({QStringLiteral("single-2d"), QStringLiteral("extra")}, 2);
		cli({QStringLiteral("--unknown")}, 2);
		cli({QStringLiteral("--dry-run")}, 2);
		ok &= expect(cli({}, 0).value(QStringLiteral("preference")) == QStringLiteral("four-views"), "invalid CLI calls preserve settings");
		ok &= expect(cli({QStringLiteral("camera-above-plans")}, 0).value(QStringLiteral("effectiveLayout")) == QStringLiteral("camera-above-plans"),
			"CLI selects camera above plans");
		ok &= expect(cli({}, 0).value(QStringLiteral("preference")) == QStringLiteral("camera-above-plans"), "CLI persists camera workspace preference");
		ok &= expect(cli({QStringLiteral("camera-beside-plans")}, 0).value(QStringLiteral("effectiveLayout")) == QStringLiteral("camera-beside-plans"),
			"CLI selects camera beside plans");
		ok &= expect(cli({}, 0).value(QStringLiteral("preference")) == QStringLiteral("camera-beside-plans"), "CLI persists wide camera workspace preference");
		const auto newerPath = temp.filePath(QStringLiteral("newer.ini"));
		{
			QSettings newer(newerPath, QSettings::IniFormat);
			newer.setValue(QStringLiteral("app/settingsSchemaVersion"), 999);
			newer.sync();
		}
		cli({QStringLiteral("four-views")}, 1, newerPath);
		ok &= expect(cli({QStringLiteral("profile")}, 0).value(QStringLiteral("effectiveLayout")) == QStringLiteral("camera-and-plan"),
					 "CLI returns to profile");
	}

	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	settings.sync();
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	auto captureWorkspace = [&](ApplicationShell &shell, int scale) {
		auto *views = shell.findChild<QSplitter *>(QStringLiteral("levelMapViews"));
		auto *camera = shell.findChild<ModelViewport *>(QStringLiteral("mapPreview3D"));
		ok &= expect(until([&] { return camera->isEnabled() && camera->hasMesh() && camera->hasSkin(); }),
					 "current document material preview ready");
		ok &= expect(camera->isVisible(), "four-view camera remains visible after queued refreshes");
		ok &= expect(camera->reducedMotion(), "level camera follows shared accessibility preferences");
		shell.findChild<QAction *>(QStringLiteral("map.frameSelection"))->trigger();
		settle();
		ok &= expect(tests::settleModelViewport(*camera), "camera render settled");
		for (auto *view : views->findChildren<MapViewport *>()) {
			ok &= expect(view->isVisible() && view->width() >= 240 && view->height() >= 180,
						 "each expanded pane remains visible with usable extent");
		}
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
		if (!captures.isEmpty()) {
			QDir().mkpath(captures);
			for (auto *widget : {static_cast<QWidget *>(views), static_cast<QWidget *>(&shell)}) {
				QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
				image.fill(Qt::transparent);
				widget->render(&image);
				const auto name = widget == views ? QStringLiteral("level-views") : QStringLiteral("level-shell");
				ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("%1-%2.png").arg(name).arg(scale))),
							 "four-view widget render saved");
			}
		}
	};
	{
		ApplicationShell shell;
		shell.resize(1700, 1100);
		shell.show();
		shell.openPathFromCommandLine(mapPath);
		ok &= expect(until([&] { return shell.levelDocument().brushes.size() == 2; }), "shell opens fixture");
		shell.openPathFromCommandLine(temp.filePath(QStringLiteral("assets")));
		shell.findChild<QAction *>(QStringLiteral("shell.mode.levels"))->trigger();
		auto *top = shell.findChild<MapViewport *>(QStringLiteral("mapViewport"));
		auto *front = shell.findChild<MapViewport *>(QStringLiteral("mapViewport1"));
		auto *side = shell.findChild<MapViewport *>(QStringLiteral("mapViewport2"));
		auto *camera = shell.findChild<ModelViewport *>(QStringLiteral("mapPreview3D"));
		auto *views = shell.findChild<QSplitter *>(QStringLiteral("levelMapViews"));
		auto *upper = shell.findChild<QSplitter *>(QStringLiteral("levelMapUpperViews"));
		auto *lower = shell.findChild<QSplitter *>(QStringLiteral("levelMapLowerViews"));
		auto command = [&](const char *id) {
			auto *action = shell.findChild<QAction *>(QString::fromLatin1(id));
			ok &= expect(action && action->isEnabled(), id);
			if (action) {
				action->trigger();
			}
		};
		ok &= expect(top && front && side && camera && views && upper && lower, "workspace widgets exist");
		if (!top || !front || !side || !camera || !views || !upper || !lower) {
			return EXIT_FAILURE;
		}
		command("map.layout.four-views");
		ok &= expect(until([&] { return camera->hasMesh() && camera->hasSkin(); }), "shared package material/model preview");
		for (auto *view : {top, front, side}) {
			ok &= expect(view->isVisible() && view->hasDocument() && view->cameraMarkerVisible(),
						 "plan pane visible with scene and camera marker");
			ok &=
				expect(view->displayDocument().brushes.size() == 2 && view->displayDocument().patches.size() == 1, "panes share full map");
			ok &= expect(view->controls().middleButtonDrivesCamera && view->focusPolicy() == Qt::StrongFocus,
						 "profile and keyboard focus available in each pane");
			ok &= expect(QAccessible::queryAccessibleInterface(view) != nullptr && !view->accessibleDescription().isEmpty(),
						 "pane accessibility metadata");
		}
		ok &= expect(camera->isVisible() && top->projection() == MapViewportProjection::TopXY &&
						 front->projection() == MapViewportProjection::FrontXZ && side->projection() == MapViewportProjection::SideZY,
					 "four distinct views");
		for (auto *view : {top, front, side}) {
			ok &= expect(view->gridSize() == 16 && view->snapToGrid(), "fresh profile grid applies to every pane");
		}
		command("map.grid8");
		auto *snap = shell.findChild<QCheckBox *>(QStringLiteral("levelMapSnap"));
		snap->setChecked(false);
		for (auto *view : {top, front, side}) {
			ok &= expect(view->gridSize() == 8 && !view->snapToGrid(), "grid and snap changes shared across panes");
		}
		command("map.grid16");
		snap->setChecked(true);
		const auto original = serializeLevelMap(shell.levelDocument()).bytes;
		const QVector<LevelMapSelectionRef> chosen{{LevelMapSelectionKind::QuakeBrush, 0}};
		front->setSelectionSet(chosen);
		front->selectionSetChanged(chosen);
		ok &= expect(front->isActivePane() && !top->isActivePane() && top->selectionSet() == chosen && side->selectionSet() == chosen,
					 "front pane drives shared selection");
		front->zoomIn();
		const auto frontZoom = front->zoom();
		const auto sideZoom = side->zoom();
		front->moveRequested(0, 0, 16);
		const auto moved = serializeLevelMap(shell.levelDocument()).bytes;
		ok &= expect(moved != original && shell.levelDocument().undoStack.size() == 1, "front-pane move is one document edit",
					 shell.statusBar()->currentMessage());
		for (auto *view : {top, front, side}) {
			ok &= expect(serializeLevelMap(view->displayDocument()).bytes == moved, "edit appears in every pane");
		}
		ok &= expect(front->zoom() == frontZoom && side->zoom() == sideZoom, "editing retains independent view scales");
		command("map.undo");
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == original, "shared exact undo");
		command("map.redo");
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == moved, "shared exact redo");
		command("map.hideSelection");
		for (auto *view : {top, front, side}) {
			ok &= expect(view->isHidden(LevelMapSelectionKind::QuakeBrush, 0) && view->selectionSet().isEmpty(),
						 "hidden object omitted everywhere");
		}
		command("map.viewSide");
		ok &= expect(side->isActivePane() && !front->isActivePane(), "projection action activates existing pane");
		command("map.showAll");
		for (auto *view : {top, front, side}) {
			ok &= expect(view->hiddenCount() == 0 && view->displayDocument().brushes.size() == 2, "show all shared from another pane");
		}
		side->setSelectionSet(chosen);
		side->selectionSetChanged(chosen);
		command("map.isolateSelection");
		for (auto *view : {top, front, side}) {
			ok &= expect(view->isHidden(LevelMapSelectionKind::QuakeBrush, 1) && !view->isHidden(LevelMapSelectionKind::QuakeBrush, 0),
						 "isolation synchronized");
		}
		command("map.showAll");
		upper->setSizes({340, 570});
		lower->setSizes({550, 360});
		views->setSizes({300, 480});
		settle();
		const auto upperState = upper->saveState(), lowerState = lower->saveState(), rootState = views->saveState();
		command("map.layout.single-2d");
		ok &=
			expect(side->isVisible() && !top->isVisible() && !front->isVisible() && !camera->isVisible(), "single plan keeps active pane");
		command("map.layout.four-views");
		settle();
		ok &= expect(upper->saveState() == upperState && lower->saveState() == lowerState && views->saveState() == rootState,
					 "splitter arrangement restored across layouts");
		ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == moved, "visibility and layout do not edit map");
		captureWorkspace(shell, 100);
		ok &= expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("edited.map")), false, &error), "shared document saves", error);
		LevelMapCreateRequest next;
		next.game = QStringLiteral("doom");
		ok &= expect(shell.createLevelDocument(next, &error), "switch to Doom document", error);
		for (auto *view : {top, front, side}) {
			ok &= expect(view->displayDocument().format == LevelMapFormat::DoomWad && view->displayDocument().brushes.isEmpty() &&
							 !view->hasLeakTrail(),
						 "new document replaces every scene");
		}
		ok &= expect(shell.saveLevelDocument(temp.filePath(QStringLiteral("doom.wad")), false, &error), "Doom saves after view change",
					 error);
		shell.close();
	}
	settings.setTextScalePercent(200);
	settings.setTheme(StudioTheme::HighContrastDark);
	settings.sync();
	// Apply settings and expansion before creating the second shell. This
	// exercises startup scaling and translated labels at construction time.
	applyStudioTheme(app, studioThemeTokens(StudioTheme::HighContrastDark, UiDensity::Standard, 200));
	Expansion expansion;
	app.installTranslator(&expansion);
	{
		ApplicationShell restored;
		restored.setLayoutDirection(Qt::RightToLeft);
		restored.resize(2600, 1700);
		restored.show();
		restored.openPathFromCommandLine(mapPath);
		ok &= expect(until([&] { return restored.levelDocument().brushes.size() == 2; }), "restored shell opens map");
		restored.openPathFromCommandLine(temp.filePath(QStringLiteral("assets")));
		restored.findChild<QAction *>(QStringLiteral("shell.mode.levels"))->trigger();
		auto *lower = restored.findChild<QSplitter *>(QStringLiteral("levelMapLowerViews"));
		ok &= expect(lower && !lower->isHidden(), "four-view preference survives a new shell");
		auto *camera = restored.findChild<ModelViewport *>(QStringLiteral("mapPreview3D"));
		ok &= expect(until([&] { return camera->hasMesh() && camera->hasSkin(); }), "restored shared assets ready");
		captureWorkspace(restored, 200);
		restored.close();
	}
	app.removeTranslator(&expansion);
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
