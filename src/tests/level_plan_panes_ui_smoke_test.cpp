// View > Show Top, Front and Side View in the Levels page, under the
// NetRadiant Custom and TrenchBroom profiles at 100% and at 200% with high
// contrast, right-to-left and expanded text: hiding and showing plan panes in
// the three-plan layouts, the last-pane rule, the active pane moving off a
// hidden pane, empty rows hiding, Top/Front/Side View bringing a pane back,
// Next 2D View skipping hidden panes, maximise/restore, per-combination
// splitter proportions, saved views, persistence in a new shell, and that
// the map bytes, revision and undo history never change. Owned offscreen
// widgets and semantic Qt calls only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_semantics.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

#include <array>
#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << phase.toStdString() << ": " << label << ": " << detail.toStdString() << std::endl;
	}
	return value;
}

bool until(const std::function<bool()>& ready)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 45000) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

void settle()
{
	QEventLoop loop;
	QTimer::singleShot(40, &loop, &QEventLoop::quit);
	loop.exec();
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

LevelPlanPanes panesOf(bool top, bool front, bool side)
{
	LevelPlanPanes panes;
	panes.shown = {top, front, side};
	return panes;
}

const QByteArray kMap = R"({
"classname" "worldspawn"
{
( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) base/wall 0 0 0 0.5 0.5 0 0 0
( 64 0 0 ) ( 64 0 1 ) ( 64 1 0 ) base/wall 0 0 0 0.5 0.5 0 0 0
( 0 0 0 ) ( 0 0 1 ) ( 1 0 0 ) base/wall 0 0 0 0.5 0.5 0 0 0
( 0 64 0 ) ( 1 64 0 ) ( 0 64 1 ) base/wall 0 0 0 0.5 0.5 0 0 0
( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) base/wall 0 0 0 0.5 0.5 0 0 0
( 0 0 64 ) ( 0 1 64 ) ( 1 0 64 ) base/wall 0 0 0 0.5 0.5 0 0 0
}
}
)";

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	QApplication::setFont(QFont(QStringLiteral("Segoe UI")));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return 1;
	}
	StudioSettings::setOverrideFilePath(temp.filePath(QStringLiteral("settings.ini")));
	StudioSettings settings;
	settings.setRestoreSession(false);
	settings.setReducedMotion(true);
	const QString path = temp.filePath(QStringLiteral("panes.map"));
	QFile file(path);
	if (!expect(file.open(QIODevice::WriteOnly) && file.write(kMap) == kMap.size(), "fixture")) {
		return 1;
	}
	file.close();
	const std::array<const char*, 3> ids {"map.showTopView", "map.showFrontView", "map.showSideView"};
	for (const auto& id : ids) {
		CommandPaletteEntry entry;
		expect(commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry) && !entry.label.isEmpty(), "palette entry", QString::fromLatin1(id));
	}
	for (const char* profile : {"netradiant-custom", "trenchbroom"}) {
		for (int scale : {100, 200}) {
			phase = QStringLiteral("%1 %2%").arg(QLatin1String(profile)).arg(scale);
			std::cout << "Level plan panes UI " << phase.toStdString() << std::endl;
			settings.setSelectedEditorProfileId(QString::fromLatin1(profile));
			settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
			settings.setLevelPlanPanes(LevelPlanPanes {});
			settings.setTextScalePercent(scale);
			const auto theme = scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark;
			settings.setTheme(theme);
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(scale == 100 ? 1700 : 2600, scale == 100 ? 1050 : 1800);
				shell.show();
				const auto action = [&](const char* id) { return shell.findChild<QAction*>(QString::fromLatin1(id)); };
				auto* showTop = action(ids[0]);
				auto* showFront = action(ids[1]);
				auto* showSide = action(ids[2]);
				if (!expect(showTop && showFront && showSide, "pane commands exist")) {
					return 1;
				}
				shell.openPathFromCommandLine(path);
				action("shell.mode.levels")->trigger();
				action("map.layout.four-views")->trigger();
				auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
				auto* side = shell.findChild<MapViewport*>(QStringLiteral("mapViewport2"));
				auto* camera = shell.findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
				auto* root = shell.findChild<QSplitter*>(QStringLiteral("levelMapViews"));
				auto* upper = shell.findChild<QSplitter*>(QStringLiteral("levelMapUpperViews"));
				auto* lower = shell.findChild<QSplitter*>(QStringLiteral("levelMapLowerViews"));
				auto* projection = shell.findChild<QComboBox*>(QStringLiteral("levelMapProjection"));
				auto* layoutButton = shell.findChild<QToolButton*>(QStringLiteral("levelViewLayoutButton"));
				if (!expect(top && front && side && camera && root && upper && lower && projection && layoutButton, "workspace widgets exist")
					|| !expect(until([&] { return shell.levelDocument().sourcePath == path && camera->isEnabled() && camera->hasMesh(); }), "map and camera ready")) {
					return 1;
				}
				settle();
				const QList<QAction*> commands {showTop, showFront, showSide};
				const auto checked = [&](bool t, bool f, bool s) {
					return showTop->isChecked() == t && showFront->isChecked() == f && showSide->isChecked() == s;
				};
				const auto visible = [&](bool t, bool f, bool s) {
					return top->isVisible() == t && front->isVisible() == f && side->isVisible() == s;
				};
				for (QAction* command : commands) {
					expect(command->isCheckable() && command->isChecked() && command->isEnabled() && !command->statusTip().isEmpty(),
						"pane commands start checked and available in four views", command->objectName());
					expect(command->shortcuts().isEmpty(), "no default key, as in NetRadiant Custom", command->shortcut().toString());
					expect(layoutButton->menu()->actions().contains(command), "the Layout menu offers the command", command->objectName());
					bool inMenu = false;
					for (QObject* owner : command->associatedObjects()) {
						inMenu = inMenu || (qobject_cast<QMenu*>(owner) && owner != layoutButton->menu());
					}
					expect(inMenu, "the View menu offers the command", command->objectName());
					auto* box = shell.findChild<QCheckBox*>(QStringLiteral("levelLayoutPane-") + command->objectName().section(QLatin1Char('.'), 1));
					expect(box && box->isChecked() && box->isEnabled() && !box->text().isEmpty() && !box->accessibleDescription().isEmpty()
							&& box->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(box)
							&& box->layoutDirection() == (scale == 200 ? Qt::RightToLeft : Qt::LeftToRight),
						"the Layout section offers an accessible check box", command->objectName());
				}
				expect(visible(true, true, true) && camera->isVisible(), "four views start with every pane");
				const QByteArray bytes = serializeLevelMap(shell.levelDocument()).bytes;
				const auto revision = shell.levelDocument().revision;
				const auto history = shell.levelDocument().undoStack.size();
				const auto unchanged = [&] {
					return serializeLevelMap(shell.levelDocument()).bytes == bytes && shell.levelDocument().revision == revision
						&& shell.levelDocument().undoStack.size() == history;
				};

				// Hiding the active Top pane moves the active plan to Front.
				shell.activateWindow();
				top->setFocus(Qt::OtherFocusReason);
				settle();
				expect(top->isActivePane(), "Top starts active");
				showTop->trigger();
				settle();
				expect(visible(false, true, true) && camera->isVisible() && upper->isVisible() && checked(false, true, true),
					"Show Top View hides only the Top pane");
				expect(settings.levelPlanPanes() == panesOf(false, true, true), "the choice is saved", levelPlanPanesId(settings.levelPlanPanes()));
				expect(front->isActivePane() && !top->isActivePane() && projection->currentIndex() == 1, "the active plan moves to a shown pane");
				expect(!shell.statusBar()->currentMessage().isEmpty(), "the status bar says what changed");
				expect(unchanged(), "a pane choice is not a map edit");

				// Proportions belong to the pane combination; hiding Top kept the
				// all-pane proportions first.
				const QByteArray allLower = settings.shellLayoutState(QStringLiteral("levelViews/four-views/lower"));
				expect(!allLower.isEmpty(), "the all-pane proportions are kept before a pane hides");
				lower->setSizes({lower->width() / 4, lower->width() * 3 / 4});
				emit lower->splitterMoved(lower->width() / 4, 1);
				expect(!settings.shellLayoutState(QStringLiteral("levelViews/four-views/front-side/lower")).isEmpty()
						&& settings.shellLayoutState(QStringLiteral("levelViews/four-views/lower")) == allLower,
					"the combination keeps its own splitter state");

				// The last shown pane cannot be hidden.
				showFront->trigger();
				settle();
				expect(visible(false, false, true) && checked(false, false, true) && side->isActivePane(), "Side alone remains");
				expect(!showSide->isEnabled() && showTop->isEnabled() && showFront->isEnabled(), "the last shown pane's command is unavailable");
				auto* sideBox = shell.findChild<QCheckBox*>(QStringLiteral("levelLayoutPane-showSideView"));
				expect(sideBox && !sideBox->isEnabled() && sideBox->isChecked(), "and so is its check box");
				showSide->trigger();
				settle();
				expect(visible(false, false, true) && settings.levelPlanPanes() == panesOf(false, false, true), "a refused hide leaves Side shown");

				// Without the camera and Top, the upper row leaves no gap.
				action("map.toggle3D")->trigger();
				settle();
				expect(!camera->isVisible() && !upper->isVisible() && lower->isVisible() && lower->height() == root->height() && side->isVisible(),
					"an empty row hides");
				action("map.nextProjection")->trigger();
				settle();
				expect(side->isActivePane() && visible(false, false, true), "Next 2D View stays on the only shown pane");
				action("map.toggle3D")->trigger();
				settle();
				expect(camera->isVisible() && upper->isVisible(), "the camera brings its row back");

				// Top View brings its pane back; Next 2D View skips Front.
				action("map.viewTop")->trigger();
				settle();
				expect(visible(true, false, true) && top->isActivePane() && checked(true, false, true)
						&& settings.levelPlanPanes() == panesOf(true, false, true),
					"Top View shows and activates the hidden Top pane");
				action("map.nextProjection")->trigger();
				settle();
				expect(side->isActivePane() && projection->currentIndex() == 2 && visible(true, false, true), "Next 2D View skips the hidden Front pane");
				action("map.nextProjection")->trigger();
				settle();
				expect(top->isActivePane(), "and wraps to Top");

				// The choice holds in every three-plan layout and waits in the others.
				action("map.layout.camera-above-plans")->trigger();
				settle();
				expect(lower->count() == 3 && visible(true, false, true) && camera->isVisible() && upper->isVisible(), "Camera Above Plans keeps the choice");
				action("map.layout.camera-beside-plans")->trigger();
				settle();
				expect(visible(true, false, true) && root->orientation() == Qt::Horizontal, "Camera Beside Plans keeps the choice");
				action("map.layout.single-2d")->trigger();
				settle();
				expect(!showTop->isEnabled() && !showFront->isEnabled() && !showSide->isEnabled() && checked(true, false, true),
					"one-plan layouts keep the choice but offer no pane commands");
				action("map.layout.four-views")->trigger();
				settle();
				expect(visible(true, false, true) && showFront->isEnabled() && camera->isVisible(), "back in four views the choice returns");

				// A temporary expansion ends before the panes change.
				auto* maximize = action("map.maximizeView");
				shell.activateWindow();
				side->setFocus(Qt::OtherFocusReason);
				settle();
				maximize->trigger();
				settle();
				expect(maximize->isChecked() && side->isVisible() && !top->isVisible(), "Side maximised");
				showFront->trigger();
				settle();
				expect(!maximize->isChecked() && visible(true, true, true) && camera->isVisible() && checked(true, true, true),
					"showing a pane restores the workspace first");

				// Saved views keep working with a hidden active plan.
				shell.activateWindow();
				front->setFocus(Qt::OtherFocusReason);
				settle();
				LevelViewState view;
				QString error;
				expect(until([&] { return shell.captureLevelViewState(&view, &error); }) && view.activePlan == 1, "a view with Front active is saved", error);
				showFront->trigger();
				settle();
				expect(shell.restoreLevelViewState(view, &error) && !front->isVisible() && !front->isActivePane()
						&& (top->isActivePane() || side->isActivePane()) && projection->currentIndex() != 1,
					"restoring it keeps Front hidden and activates a shown pane", error);
				expect(unchanged(), "no pane change edited the map");
				shell.close();
			}
			if (scale == 100) {
				// A new session shows the panes the last one chose.
				ApplicationShell shell;
				shell.resize(1700, 1050);
				shell.show();
				shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
				settle();
				auto* front = shell.findChild<MapViewport*>(QStringLiteral("mapViewport1"));
				auto* top = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				auto* showFront = shell.findChild<QAction*>(QString::fromLatin1(ids[1]));
				expect(front && top && showFront && front->QWidget::isHidden() && !top->QWidget::isHidden() && !showFront->isChecked(), "the choice survives a restart");
				shell.close();
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}
	if (ok) {
		std::cout << "Level plan panes UI passed." << std::endl;
	}
	return ok ? 0 : 1;
}
