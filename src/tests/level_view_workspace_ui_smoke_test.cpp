#include "app/application_shell.h"
#include "app/editor_profile_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTextBrowser>
#include <QMenu>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QToolBar>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer; timer.start();
	while (!ready() && timer.elapsed() < 30000) {
		QEventLoop loop; QTimer::singleShot(20, &loop, &QEventLoop::quit); loop.exec();
	}
	return ready();
}
void settle()
{
	QEventLoop loop; QTimer::singleShot(60, &loop, &QEventLoop::quit); loop.exec();
}
QByteArray navigation(const LevelViewState& state)
{
	return QJsonDocument(levelBookmarksJson({{QStringLiteral("test"), QStringLiteral("Test"), state}})).toJson();
}
class Expansion final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		return QStringLiteral("[%1 expanded text]").arg(QString::fromUtf8(source));
	}
};
}

int main(int argc, char** argv)
{
	// Direct services, focus setters, Qt signals and widget rendering only.
	// No input injection, OS capture, real settings or game launch.
	QTemporaryDir temp;
	if (!temp.isValid()) { return EXIT_FAILURE; }
	// Qt's offscreen plugin defaults to an 800px screen, which clips native
	// menus even when the rendered shell is larger. Use a realistic test screen.
	// Configuration contract: qtbase/src/plugins/platforms/offscreen/qoffscreenintegration.cpp.
	QFile screen(temp.filePath(QStringLiteral("offscreen.json")));
	const QByteArray screenConfig = R"({"screens":[{"name":"workspace-test","width":4096,"height":3072,"logicalDpi":96,"logicalBaseDpi":96,"dpr":1}]})";
	if (!screen.open(QIODevice::WriteOnly) || screen.write(screenConfig) != screenConfig.size()) { return EXIT_FAILURE; }
	screen.close();
	const auto workingDirectory = QDir::currentPath();
	if (!QDir::setCurrent(temp.path())) { return EXIT_FAILURE; }
	// A relative config path avoids drive-letter colons in Qt's platform options.
	qputenv("QT_QPA_PLATFORM", "offscreen:configfile=offscreen.json");
	qputenv("QT_SCALE_FACTOR", "1");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (!QDir::setCurrent(workingDirectory)) { return EXIT_FAILURE; }
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true);
	LevelMapDocument fixture;
	QString error;
	bool ok = expect(tests::createMaterialFixture(temp.path(), &fixture, &error), "material fixture created");
	const auto mapPath = temp.filePath(QStringLiteral("workspace.map"));
	const auto otherPath = temp.filePath(QStringLiteral("other.map"));
	ok &= expect(tests::putMaterialFile(mapPath, serializeLevelMap(fixture).bytes)
		&& tests::putMaterialFile(otherPath, serializeLevelMap(fixture).bytes), "map fixtures written");
	for (int scale : {100, 200}) {
		settings.setSelectedEditorProfileId(QStringLiteral("hammer"));
		settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
		settings.setTextScalePercent(scale); settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		Expansion expansion;
		if (scale == 200) { app.installTranslator(&expansion); }
		{
			ApplicationShell shell;
			shell.resize(scale == 100 ? 1800 : 2900, scale == 100 ? 1200 : 2000); shell.show();
			auto focus = [&](QWidget* pane) {
				// The offscreen platform does not activate a shell again when a
				// worker dialog closes. Set only this process's Qt focus state.
				shell.activateWindow(); settle();
				pane->setFocus(Qt::OtherFocusReason); settle();
			};
			auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
			auto* maximize = action("map.maximizeView");
			auto* equalize = action("map.equalizeViews");
			if (!expect(maximize && equalize, "workspace commands exist")) { return EXIT_FAILURE; }
			ok &= expect(!maximize->isEnabled() && !equalize->isEnabled(), "no-map workspace actions disabled");
			shell.openPathFromCommandLine(mapPath);
			ok &= expect(until([&] { return shell.levelDocument().brushes.size() == fixture.brushes.size(); }), "map loaded");
			shell.openPathFromCommandLine(temp.filePath(QStringLiteral("assets")));
			action("shell.mode.levels")->trigger();
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
			auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
			auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
			auto* root = shell.findChild<QSplitter*>(QStringLiteral("levelMapViews"));
			auto* upper = shell.findChild<QSplitter*>(QStringLiteral("levelMapUpperViews"));
			auto* lower = shell.findChild<QSplitter*>(QStringLiteral("levelMapLowerViews"));
			auto* layoutButton = shell.findChild<QToolButton*>(QStringLiteral("levelViewLayoutButton"));
			if (!expect(top && front && side && camera && root && upper && lower && layoutButton, "workspace widgets exist")) { return EXIT_FAILURE; }
			ok &= expect(until([&] { return camera->isEnabled() && camera->hasMesh() && camera->hasSkin(); }), "package-backed camera ready");
			ok &= expect(layoutButton->menu()->actions().contains(maximize) && layoutButton->menu()->actions().contains(equalize), "layout menu shares commands");
			ok &= expect(maximize->shortcuts().contains(QKeySequence(QStringLiteral("Shift+Z")))
				&& equalize->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+A"))), "Hammer workspace bindings installed");
			for (auto* owner : maximize->associatedObjects()) {
				auto* widget = qobject_cast<QWidget*>(owner);
				ok &= expect(!widget || widget == top || !widget->isAncestorOf(top), "workspace shortcuts exclude inspector focus");
			}
			action("map.selectAll")->trigger();
			const auto bytes = serializeLevelMap(shell.levelDocument()).bytes;
			const auto selection = shell.levelDocument().selection;
			ok &= expect(!selection.isEmpty(), "nonempty selection for preservation checks");
			root->setSizes({650, 410}); upper->setSizes({640, 420}); lower->setSizes({430, 630}); settle();
			const QList<QWidget*> panes {camera, top, front, side};
			const QList<QSplitter*> splitters {root, upper, lower};
			auto capture = [&](QWidget* widget, const QString& name) {
				const auto directory = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (directory.isEmpty()) { return; }
				QDir().mkpath(directory);
				QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); widget->render(&image);
				ok &= expect(image.save(QDir(directory).filePath(QStringLiteral("%1-%2.png").arg(name).arg(scale))), "widget evidence saved");
			};
			for (auto* target : panes) {
				focus(target);
				ok &= expect(target->hasFocus(), "offscreen pane accepts focus");
				LevelViewState before, expanded, restored;
				ok &= expect(shell.captureLevelViewState(&before, &error), "view snapshot before maximize");
				QList<QList<int>> sizes;
				for (auto* splitter : splitters) { sizes << splitter->sizes(); }
				maximize->trigger(); settle();
				ok &= expect(maximize->isChecked() && target->isVisible(), "focused pane maximized");
				for (auto* pane : panes) { ok &= expect(pane->isVisible() == (pane == target), "only selected pane visible"); }
				ok &= expect(target->width() == root->width() && target->height() == root->height(), "maximized pane fills the viewport area");
				ok &= expect(shell.captureLevelViewState(&expanded, &error) && navigation(expanded) == navigation(before), "bookmark captures underlying layout and unchanged navigation");
				ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().selection == selection, "maximize preserves geometry and selection");
				ok &= expect(settings.levelViewLayoutPreference() == QStringLiteral("four-views"), "maximize preserves persistent layout");
				const auto saved = settings.shellLayoutState(QStringLiteral("levelViews/four-views/upper"));
				upper->splitterMoved(1, 1);
				ok &= expect(settings.shellLayoutState(QStringLiteral("levelViews/four-views/upper")) == saved, "expanded splitter cannot overwrite saved proportions");
				if (target == front) { capture(root, QStringLiteral("maximized-front")); }
				maximize->trigger(); settle();
				ok &= expect(!maximize->isChecked() && target->hasFocus(), "restore returns focus to the same pane");
				for (auto* pane : panes) { ok &= expect(pane->isVisible(), "four-pane visibility restored"); }
				for (int i = 0; i < splitters.size(); ++i) { ok &= expect(splitters[i]->sizes() == sizes[i], "exact unequal pane sizes restored"); }
				ok &= expect(shell.captureLevelViewState(&restored, &error) && navigation(restored) == navigation(before), "restore preserves navigation and bookmark state");
			}
			focus(front); maximize->trigger();
			front->moveRequested(16, 0, 0);
			ok &= expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes != bytes; }), "edit applies through shared placement service");
			ok &= expect(maximize->isChecked() && front->isVisible() && !camera->isVisible(), "editing retains maximized plan");
			action("map.undo")->trigger();
			ok &= expect(until([&] { return serializeLevelMap(shell.levelDocument()).bytes == bytes; }), "undo restores exact map");
			LevelViewState bookmark;
			ok &= expect(until([&] { return shell.captureLevelViewState(&bookmark, &error); }) && shell.restoreLevelViewState(bookmark, &error), "bookmark round trip while maximized");
			ok &= expect(!maximize->isChecked() && camera->isVisible() && side->isVisible(), "bookmark restore clears temporary expansion");
			action("map.toggle3D")->trigger(); focus(front); maximize->trigger(); maximize->trigger(); settle();
			ok &= expect(!camera->isVisible() && top->isVisible() && front->isVisible() && side->isVisible(), "restore preserves intentionally hidden camera");
			action("map.toggle3D")->trigger(); focus(front); maximize->trigger();
			equalize->trigger(); settle();
			ok &= expect(!maximize->isChecked(), "equalize restores workspace first");
			for (auto* splitter : splitters) {
				const auto values = splitter->sizes();
				ok &= expect(values.size() == 2 && std::abs(values[0] - values[1]) <= 1, "equalized panes share available space");
			}
			ok &= expect(tests::settleModelViewport(*camera), "restored camera completes its render");
			capture(root, QStringLiteral("restored-four-views"));
			// A broad camera above all three plans retains the same editing,
			// saved-view and focus behaviour as the traditional four-view grid.
			action("map.layout.camera-above-plans")->trigger(); settle();
			ok &= expect(upper->count() == 1 && upper->widget(0) == camera && lower->count() == 3,
				"camera spans the upper row above three plans");
			ok &= expect(lower->widget(0) == top && lower->widget(1) == front && lower->widget(2) == side,
				"camera workspace retains ordered top front and side panes");
			ok &= expect(camera->width() == root->width() && camera->height() > top->height(), "camera receives primary workspace area");
			LevelViewState cameraWorkspace;
			ok &= expect(shell.captureLevelViewState(&cameraWorkspace, &error)
				&& cameraWorkspace.layout == LevelViewLayout::CameraAbovePlans, "new layout captures valid bookmark");
			for (auto* target : panes) {
				focus(target); maximize->trigger(); settle();
				ok &= expect(target->width() == root->width() && target->height() == root->height(), "new layout maximizes each pane");
				if (target == camera) {
					ok &= expect(shell.findChild<QComboBox*>(QStringLiteral("levelMapGrid"))->isEnabled()
						&& shell.findChild<QCheckBox*>(QStringLiteral("levelMapSnap"))->isEnabled(), "camera authoring keeps grid and snap available");
				}
				maximize->trigger(); settle();
			}
			action("map.toggle3D")->trigger(); settle();
			ok &= expect(!upper->isVisible() && lower->height() == root->height(), "hidden camera leaves no empty workspace row");
			action("map.layout.four-views")->trigger(); settle();
			ok &= expect(upper->isVisible() && top->isVisible(), "layout switch restores hidden upper splitter");
			ok &= expect(shell.restoreLevelViewState(cameraWorkspace, &error), "camera workspace bookmark restores");
			settle();
			ok &= expect(camera->isVisible() && upper->count() == 1 && lower->count() == 3, "bookmark restores new pane arrangement");
			for (const auto* name : {"levelCreateTools", "levelSelectTools", "levelTransformTools", "levelGeometryTools", "levelSurfaceToolsMenu"}) {
				auto* button = shell.findChild<QToolButton*>(QString::fromLatin1(name));
				auto* menu = button && button->defaultAction() ? button->defaultAction()->menu() : nullptr;
				ok &= expect(button && menu && !menu->actions().isEmpty()
					&& button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button)
					&& !button->accessibleDescription().isEmpty(), "authoring groups expose accessible keyboard menus");
			}
			auto* selectTools = shell.findChild<QToolButton*>(QStringLiteral("levelSelectTools"));
			auto* selectionMenu = selectTools && selectTools->defaultAction() ? selectTools->defaultAction()->menu() : nullptr;
			ok &= expect(selectionMenu && selectionMenu->actions().contains(action("map.selectNone")), "authoring shelf shares live commands");
			if (selectionMenu) {
				bool opened = false;
				const auto connection = QObject::connect(selectionMenu, &QMenu::aboutToShow, &shell, [&] { opened = true; });
				QTimer::singleShot(20, selectionMenu, &QMenu::close);
				selectTools->showMenu();
				QObject::disconnect(connection);
				ok &= expect(opened, "authoring button opens its default action menu");
			}
			action("map.selectNone")->trigger();
			ok &= expect(shell.levelDocument().selection.isEmpty()
				&& !action("map.duplicateWithOffset")->isEnabled(), "shelf command selection updates transform enablement");
			action("map.selectAll")->trigger();
			ok &= expect(serializeLevelMap(shell.levelDocument()).bytes == bytes, "workspace and shelf preserve map contents");
			auto* authoringBar = shell.findChild<QToolBar*>(QStringLiteral("levelAuthoringBar"));
			authoringBar->setFixedWidth(180); settle();
			auto* overflow = authoringBar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
			ok &= expect(overflow && overflow->isVisible() && overflow->menu(), "narrow authoring shelf exposes overflow");
			for (auto* groupedAction : authoringBar->actions()) {
				auto* control = authoringBar->widgetForAction(groupedAction);
				ok &= expect((control && control->isVisible()) || (overflow && overflow->menu() && overflow->menu()->actions().contains(groupedAction)),
					"every authoring menu remains reachable at narrow widths");
			}
			authoringBar->setMinimumWidth(0); authoringBar->setMaximumWidth(QWIDGETSIZE_MAX); settle();
			ok &= expect(tests::settleModelViewport(*camera), "camera authoring workspace render completes");
			capture(&shell, QStringLiteral("camera-authoring-workspace"));
			action("map.layout.camera-beside-plans")->trigger(); settle();
			ok &= expect(root->orientation() == Qt::Horizontal && lower->orientation() == Qt::Vertical
				&& upper->count() == 1 && lower->count() == 3, "wide workspace stacks three plans beside the camera");
			ok &= expect(camera->height() == root->height() && camera->width() > top->width(), "wide camera occupies primary column");
			LevelViewState wideWorkspace;
			ok &= expect(shell.captureLevelViewState(&wideWorkspace, &error)
				&& wideWorkspace.layout == LevelViewLayout::CameraBesidePlans, "wide workspace captures valid bookmark");
			root->setSizes({1000, 600}); lower->setSizes({300, 250, 200}); settle();
			const auto wideSizes = root->sizes();
			const auto planSizes = lower->sizes();
			for (auto* target : panes) {
				focus(target); maximize->trigger(); settle();
				ok &= expect(target->width() == root->width() && target->height() == root->height(), "wide layout maximizes every pane");
				maximize->trigger(); settle();
				ok &= expect(root->sizes() == wideSizes && lower->sizes() == planSizes, "wide layout restores exact pane proportions");
			}
			action("map.toggle3D")->trigger(); settle();
			ok &= expect(!upper->isVisible() && lower->width() == root->width(), "hidden camera leaves no empty column");
			action("map.toggle3D")->trigger(); settle();
			ok &= expect(tests::settleModelViewport(*camera), "wide camera render completes");
			capture(&shell, QStringLiteral("camera-beside-plans"));
			action("map.layout.four-views")->trigger(); settle();
			ok &= expect(root->orientation() == Qt::Vertical && lower->orientation() == Qt::Horizontal, "classic layout resets splitter orientation");
			ok &= expect(shell.restoreLevelViewState(wideWorkspace, &error), "wide bookmark restores");
			settle();
			ok &= expect(root->orientation() == Qt::Horizontal && camera->isVisible(), "wide bookmark restores orientation and visibility");
			action("map.layout.camera-beside-plans")->trigger(); settle();
			const auto priorProfile = settings.selectedEditorProfileId();
			QTimer::singleShot(0, &shell, [&] {
				auto* browser = shell.findChild<EditorProfileDialog*>();
				if (!expect(browser != nullptr, "profile browser opens from controls")) { ok = false; return; }
				auto* search = browser->findChild<QLineEdit*>(QStringLiteral("editorProfileSearch"));
				auto* choices = browser->findChild<QListWidget*>(QStringLiteral("editorProfileChoices"));
				auto* preview = browser->findChild<QTextBrowser*>(QStringLiteral("editorProfilePreview"));
				auto* use = browser->findChild<QPushButton*>(QStringLiteral("editorProfileApply"));
				ok &= expect(browser->layoutDirection() == shell.layoutDirection(), "profile browser follows workspace reading direction");
				ok &= expect(choices->count() == editorProfileDescriptors().size(), "browser includes every configured profile");
				search->setText(QStringLiteral("no such editor xyz"));
				ok &= expect(!use->isEnabled() && browser->selectedProfileId().isEmpty(), "empty search cannot apply stale selection");
				search->setText(QStringLiteral("TB"));
				ok &= expect(browser->selectedProfileId() == QStringLiteral("trenchbroom") && use->isEnabled(), "profile aliases are searchable");
				ok &= expect(preview->toPlainText().contains(QStringLiteral("TrenchBroom"))
					&& !preview->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(choices), "profile preview exposes accessible controls");
				ok &= expect(settings.selectedEditorProfileId() == priorProfile, "browsing does not mutate settings");
				settle(); capture(browser, QStringLiteral("profile-browser"));
				use->click();
			});
			action("levelBrowseProfiles")->trigger(); settle();
			ok &= expect(settings.selectedEditorProfileId() == QStringLiteral("trenchbroom"), "profile browser applies through shared settings");
			ok &= expect(settings.levelViewLayoutPreference() == QStringLiteral("camera-beside-plans"), "profile choice preserves explicit layout override");
			action("map.layout.four-views")->trigger(); settle();
			focus(front); maximize->trigger(); action("map.viewSide")->trigger(); settle();
			ok &= expect(!maximize->isChecked() && side->isVisible() && side->hasFocus(), "projection change restores and focuses destination pane");
			focus(camera); maximize->trigger(); action("map.clipTool")->trigger(); settle();
			ok &= expect(!maximize->isChecked() && side->isVisible() && side->clipMode(), "plan tool restores visible editing surface");
			action("map.clipTool")->trigger(); action("map.layout.four-views")->trigger();
			focus(front); maximize->trigger(); action("map.layout.single-2d")->trigger(); settle();
			ok &= expect(!maximize->isChecked() && !maximize->isEnabled() && !equalize->isEnabled(), "single-view layout leaves no stale restore state");
			action("map.layout.four-views")->trigger(); focus(front); maximize->trigger();
			auto* profile = shell.findChild<QComboBox*>(QStringLiteral("editorProfileCombo"));
			profile->setCurrentIndex(profile->findData(QStringLiteral("netradiant-custom"))); settle();
			ok &= expect(!maximize->isChecked() && side->isVisible() && maximize->shortcuts().contains(QKeySequence(QStringLiteral("F12"))), "profile switch restores layout and installs audited binding");
			ok &= expect(action("code.goToDefinition") && action("code.goToDefinition")->shortcuts().contains(QKeySequence(QStringLiteral("F12"))),
				"NetRadiant F12 leaves Code Go to Definition available in its own surface");
			if (scale == 200) { shell.setLayoutDirection(Qt::RightToLeft); }
			focus(front); maximize->trigger();
			auto* menu = layoutButton->menu();
			menu->setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			menu->show(); settle();
			ok &= expect(menu->rect().contains(menu->actionGeometry(equalize)) && menu->width() >= menu->sizeHint().width(), "expanded workspace menu fits all actions");
			capture(menu, QStringLiteral("workspace-menu")); menu->hide();
			ok &= expect(QAccessible::queryAccessibleInterface(layoutButton) && !layoutButton->accessibleName().isEmpty(), "layout control exposes accessibility metadata");
			shell.openPathFromCommandLine(otherPath);
			ok &= expect(until([&] { return shell.levelDocument().sourcePath == otherPath; }), "second map opens");
			ok &= expect(!maximize->isChecked() && camera->isVisible() && side->isVisible(), "new map clears temporary expansion");
			focus(front); maximize->trigger(); action("map.close")->trigger();
			ok &= expect(shell.levelDocument().format == LevelMapFormat::Unknown && !maximize->isChecked() && !maximize->isEnabled(), "map close clears expansion and disables commands");
			shell.close();
		}
		if (scale == 200) { app.removeTranslator(&expansion); }
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
