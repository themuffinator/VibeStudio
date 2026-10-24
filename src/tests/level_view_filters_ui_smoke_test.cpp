// View > Filters in the real shell, offscreen, in the NetRadiant Custom,
// TrenchBroom and studio profiles at 100% (dark) and 200% (high contrast,
// expanded translation, right to left):
// - every kind is a checkable command with a palette entry, checked while
//   shown, also in the view bar's Select > Filters menu, and NetRadiant
//   Custom's Filter keys (Alt+1 World, Alt+2 Entities and the rest) are
//   installed there only, Ctrl+P staying Go to File;
// - a command, its View tab check box and the saved list always agree, and
//   every plan view hides the kind's objects (a func_group's brushes with
//   it) without changing the map, the selection or undo;
// - Invert Filters, from the command and the View tab's button, flips every
//   kind; Show Every Kind brings them all back; Show All Hidden leaves
//   filters alone; with Qt Test, Alt+1 reaches the command from a plan view;
// - the saved list comes back in a new shell, kinds saved for other formats
//   survive an invert, and a Quake map without patches disables Patches.
// Owned offscreen widgets, settings in a temporary folder and synthetic key
// events only.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_view_filters.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"
#include "tests/level_view_filter_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QSet>
#include <QStatusBar>
#include <QTemporaryDir>
#ifdef VIBESTUDIO_HAVE_QTEST
#include <QTest>
#endif
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

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

bool until(const std::function<bool()>& ready, int milliseconds = 30000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < milliseconds) {
		QEventLoop loop;
		QTimer::singleShot(15, &loop, &QEventLoop::quit);
		loop.exec(QEventLoop::ExcludeUserInputEvents);
	}
	return ready();
}

class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*, const char* source, const char*, int) const override
	{
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray savedFilters()
{
	StudioSettings fresh;
	return fresh.shellLayoutState(levelViewFiltersSettingsKey());
}

QSet<QString> asSet(const QStringList& list)
{
	return QSet<QString>(list.cbegin(), list.cend());
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
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
	settings.setLevelViewLayoutPreference(QStringLiteral("four-views"));
	const QString path = temp.filePath(QStringLiteral("filters.map"));
	if (!expect(writeFile(path, tests::viewFilterFixture()), "fixture")) {
		return 1;
	}
	const QStringList kinds = [] {
		QStringList ids;
		for (const LevelViewFilter& kind : levelViewFilterKinds()) {
			ids << kind.id;
		}
		return ids;
	}();
	const QHash<QString, QString> nrcKeys {{QStringLiteral("world"), QStringLiteral("Alt+1")}, {QStringLiteral("entities"), QStringLiteral("Alt+2")},
		{QStringLiteral("liquids"), QStringLiteral("Alt+5")}, {QStringLiteral("caulk"), QStringLiteral("Alt+6")}, {QStringLiteral("clip"), QStringLiteral("Alt+7")},
		{QStringLiteral("paths"), QStringLiteral("Alt+8")}, {QStringLiteral("lights"), QStringLiteral("Alt+0")}, {QStringLiteral("detail"), QStringLiteral("Ctrl+D")},
		{QStringLiteral("hint"), QStringLiteral("Ctrl+H")}, {QStringLiteral("models"), QStringLiteral("Shift+M")},
		{QStringLiteral("triggers"), QStringLiteral("Ctrl+Shift+T")}};

	for (const char* profile : {"netradiant-custom", "trenchbroom", "vibestudio-default"}) {
		for (int scale : {100, 200}) {
			const QString profileId = QString::fromLatin1(profile);
			phase = QStringLiteral("%1 %2%").arg(profileId).arg(scale);
			std::cout << "View filters UI " << profile << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profileId);
			settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
			settings.setShellLayoutState(levelViewFiltersSettingsKey(), QByteArray());
			settings.sync();
			applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, scale));
			Expanded expanded;
			if (scale == 200) {
				app.installTranslator(&expanded);
				app.setLayoutDirection(Qt::RightToLeft);
			}
			{
				ApplicationShell shell;
				shell.resize(1600, 1000);
				shell.show();
				shell.openPathFromCommandLine(path);
				const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
				const auto command = [&](const QString& kind) { return action(levelViewFilterCommandId(kind)); };
				action(QStringLiteral("shell.mode.levels"))->trigger();
				action(QStringLiteral("map.layout.four-views"))->trigger();
				auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 10; }), "map ready")) {
					return 1;
				}
				expect(shell.levelDocument().format == LevelMapFormat::Quake3Map, "fixture opens as a Quake III map");
				if (QAction* sidebar = action(QStringLiteral("map.sidebar.view"))) {
					sidebar->trigger();
				}
				const bool nrc = profileId == QLatin1String("netradiant-custom");

				// Every kind is a checkable, described command, checked while shown.
				for (const QString& kind : kinds) {
					QAction* toggle = command(kind);
					CommandPaletteEntry entry;
					if (!expect(toggle, "kind command exists", kind)) {
						continue;
					}
					expect(toggle->isCheckable() && toggle->isChecked() && toggle->isEnabled() && !toggle->text().isEmpty() && !toggle->statusTip().isEmpty(),
						"kind command is checked, enabled and described", kind);
					expect(commandPaletteEntryForCommandId(toggle->objectName(), &entry) && !entry.label.isEmpty() && !entry.summary.isEmpty(),
						"kind command is in the shared registry", kind);
					const QList<QKeySequence> wanted = nrc && nrcKeys.contains(kind) ? QList<QKeySequence> {QKeySequence(nrcKeys.value(kind))} : QList<QKeySequence> {};
					expect(toggle->shortcuts() == wanted, "profile key", kind + QLatin1String(": ") + toggle->shortcut().toString());
				}
				for (const char* id : {"map.invertFilters", "map.resetFilters"}) {
					QAction* filterAction = action(QString::fromLatin1(id));
					CommandPaletteEntry entry;
					expect(filterAction && filterAction->isEnabled() && !filterAction->isCheckable() && !filterAction->statusTip().isEmpty()
							&& commandPaletteEntryForCommandId(QString::fromLatin1(id), &entry),
						"invert and reset commands", QString::fromLatin1(id));
				}
				QAction* goToFile = action(QStringLiteral("shell.goToFile"));
				expect(!goToFile || goToFile->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+P"))), "Ctrl+P stays Go to File");
				// The view bar's Select menu carries the same commands in a Filters submenu.
				auto* selectTools = shell.findChild<QToolButton*>(QStringLiteral("levelSelectTools"));
				auto* filterMenu = shell.findChild<QMenu*>(QStringLiteral("levelFilterToolsMenu"));
				bool filterMenuComplete = selectTools && selectTools->defaultAction() && selectTools->defaultAction()->menu() && filterMenu
					&& !filterMenu->title().isEmpty() && selectTools->defaultAction()->menu()->actions().contains(filterMenu->menuAction());
				for (const QString& kind : kinds) {
					filterMenuComplete = filterMenuComplete && filterMenu->actions().contains(command(kind));
				}
				expect(filterMenuComplete && filterMenu->actions().contains(action(QStringLiteral("map.invertFilters")))
						&& filterMenu->actions().contains(action(QStringLiteral("map.resetFilters"))),
					"the view bar's Select menu offers every filter, Invert Filters and Show Every Kind");

				auto* worldBox = shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-world"));
				auto* lightsBox = shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-lights"));
				auto* invertButton = shell.findChild<QToolButton*>(QStringLiteral("levelFiltersInvert"));
				auto* showAllButton = shell.findChild<QToolButton*>(QStringLiteral("levelFiltersShowAll"));
				if (!expect(worldBox && lightsBox && invertButton && showAllButton, "View tab filter controls")) {
					return 1;
				}
				const int worldCount = static_cast<int>(levelViewFilterMatches(shell.levelDocument(), QStringLiteral("world")).size());
				expect(until([&] { return worldBox->accessibleName().contains(QString::number(worldCount)); }), "check box names its count",
					worldBox->accessibleName());
				expect(!invertButton->accessibleName().isEmpty() && !invertButton->accessibleDescription().isEmpty() && invertButton->focusPolicy() == Qt::TabFocus
						&& invertButton->isEnabled() && QAccessible::queryAccessibleInterface(invertButton),
					"Invert button is named, described and reachable by keyboard");
				expect(!showAllButton->accessibleName().isEmpty() && showAllButton->focusPolicy() == Qt::TabFocus, "Show Every Kind button is named");

				const QVector<LevelMapSelectionRef> picked {{LevelMapSelectionKind::QuakeBrush, 0}};
				view->setSelectionSet(picked);
				view->selectionSetChanged(picked);
				const QByteArray baseline = serializeLevelMap(shell.levelDocument()).bytes;
				const auto revision = shell.levelDocument().revision;
				const auto undo = shell.levelDocument().undoStack.size();
				const auto selection = shell.levelDocument().selection;
				const auto unchanged = [&] {
					return serializeLevelMap(shell.levelDocument()).bytes == baseline && shell.levelDocument().revision == revision
						&& shell.levelDocument().undoStack.size() == undo && shell.levelDocument().selection == selection;
				};
				const auto everyPane = [&](int count) {
					for (MapViewport* pane : shell.findChildren<MapViewport*>()) {
						if (pane->hasDocument() && pane->filteredCount() != count) {
							return false;
						}
					}
					return true;
				};

				// The command, its check box, the saved list and every view agree.
				command(QStringLiteral("world"))->trigger();
				LevelViewFilter world;
				expect(levelViewFilterForId(shell.levelDocument(), QStringLiteral("world"), &world), "World Brushes is offered");
				expect(!command(QStringLiteral("world"))->isChecked() && !worldBox->isChecked() && savedFilters() == QByteArrayLiteral("world"),
					"World Brushes command switches the kind off everywhere", QString::fromUtf8(savedFilters()));
				expect(view->filteredCount() == worldCount && everyPane(worldCount), "every plan view hides the world", QString::number(view->filteredCount()));
				expect(view->isHidden(LevelMapSelectionKind::QuakeBrush, 0) && view->isHidden(LevelMapSelectionKind::QuakeBrush, 6)
						&& view->isHidden(LevelMapSelectionKind::QuakePatch, 1) && !view->isHidden(LevelMapSelectionKind::QuakeBrush, 8),
					"func_group brushes and patches count as world; door brushes do not");
				expect(shell.statusBar()->currentMessage().contains(world.title), "status bar names the kind", shell.statusBar()->currentMessage());
				expect(unchanged(), "filters never change the map, the selection or undo");

				lightsBox->click();
				expect(!command(QStringLiteral("lights"))->isChecked() && view->filteredCount() == worldCount + 1
						&& asSet(levelViewFiltersFromSaved(savedFilters())) == asSet({QStringLiteral("world"), QStringLiteral("lights")}),
					"a check box drives its command");

				// Invert Filters from the command, then from the View tab's button.
				action(QStringLiteral("map.invertFilters"))->trigger();
				QStringList invertedOff = kinds;
				invertedOff.removeAll(QStringLiteral("world"));
				invertedOff.removeAll(QStringLiteral("lights"));
				const int invertedCount = static_cast<int>(levelViewFilteredObjects(shell.levelDocument(), invertedOff).size());
				expect(asSet(levelViewFiltersFromSaved(savedFilters())) == asSet(invertedOff) && command(QStringLiteral("world"))->isChecked()
						&& command(QStringLiteral("lights"))->isChecked() && !command(QStringLiteral("clip"))->isChecked() && worldBox->isChecked()
						&& !shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-clip"))->isChecked(),
					"Invert Filters flips every kind", QString::fromUtf8(savedFilters()));
				expect(view->filteredCount() == invertedCount && everyPane(invertedCount) && !shell.statusBar()->currentMessage().isEmpty(),
					"inverted filters reach every view", QString::number(view->filteredCount()));
				invertButton->click();
				expect(asSet(levelViewFiltersFromSaved(savedFilters())) == asSet({QStringLiteral("world"), QStringLiteral("lights")})
						&& view->filteredCount() == worldCount + 1,
					"the Invert button inverts back", QString::fromUtf8(savedFilters()));

				// Show Every Kind; Show All Hidden leaves filters on.
				showAllButton->click();
				expect(savedFilters().isEmpty() && view->filteredCount() == 0 && command(QStringLiteral("world"))->isChecked() && worldBox->isChecked()
						&& lightsBox->isChecked(),
					"Show Every Kind shows every kind");
				command(QStringLiteral("clip"))->trigger();
				action(QStringLiteral("map.showAll"))->trigger();
				expect(view->filteredCount() == 1 && !command(QStringLiteral("clip"))->isChecked(), "Show All Hidden leaves filters on");
				action(QStringLiteral("map.resetFilters"))->trigger();
				expect(view->filteredCount() == 0 && savedFilters().isEmpty() && command(QStringLiteral("clip"))->isChecked(), "Show Every Kind command");

				// NetRadiant Custom's Alt+1 reaches the command from a plan view
				// through the shortcut map, when Qt Test is there to press it.
#ifdef VIBESTUDIO_HAVE_QTEST
				if (nrc) {
					shell.activateWindow();
					expect(until([&] { return QApplication::activeWindow() == &shell; }), "owned offscreen window is active");
					view->setFocus(Qt::OtherFocusReason);
					expect(until([&] { return view->hasFocus(); }), "plan view takes focus");
					QTest::keyClick(view, Qt::Key_1, Qt::AltModifier);
					expect(until([&] { return !command(QStringLiteral("world"))->isChecked(); }, 3000) && view->filteredCount() == worldCount,
						"Alt+1 hides the world");
					QTest::keyClick(view, Qt::Key_1, Qt::AltModifier);
					expect(until([&] { return command(QStringLiteral("world"))->isChecked(); }, 3000) && view->filteredCount() == 0, "Alt+1 again shows it");
				}
#endif
				expect(unchanged(), "every filter workflow leaves the map, the selection and undo alone");
			}
			if (scale == 200) {
				app.removeTranslator(&expanded);
				app.setLayoutDirection(Qt::LeftToRight);
			}
		}
	}

	// The saved list comes back in a new shell; kinds saved for other formats
	// survive an invert; a Quake map without patches disables Patches.
	{
		phase = QStringLiteral("persistence");
		settings.setSelectedEditorProfileId(QStringLiteral("vibestudio-default"));
		settings.setTextScalePercent(100);
		settings.setTheme(StudioTheme::Dark);
		settings.setShellLayoutState(levelViewFiltersSettingsKey(), QByteArrayLiteral("world,things:1"));
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, 100));
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		shell.openPathFromCommandLine(path);
		const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
		action(QStringLiteral("shell.mode.levels"))->trigger();
		auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 10; }), "map ready")) {
			return 1;
		}
		const int worldCount = static_cast<int>(levelViewFilterMatches(shell.levelDocument(), QStringLiteral("world")).size());
		auto* worldBox = shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-world"));
		expect(!action(QStringLiteral("map.filterWorld"))->isChecked() && worldBox && !worldBox->isChecked()
				&& until([&] { return view->filteredCount() == worldCount; }),
			"the saved list comes back", QString::number(view->filteredCount()));
		action(QStringLiteral("map.invertFilters"))->trigger();
		const QStringList off = levelViewFiltersFromSaved(savedFilters());
		expect(off.value(0) == QLatin1String("things:1") && !off.contains(QStringLiteral("world")) && off.size() == kinds.size(),
			"Invert keeps kinds saved for other formats", off.join(QLatin1Char(',')));
		action(QStringLiteral("map.resetFilters"))->trigger();

		const QString quakePath = temp.filePath(QStringLiteral("quake.map"));
		const QByteArray quake = "{\n\"classname\" \"worldspawn\"\n\"wad\" \"gfx/base.wad\"\n{\n"
								 "( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) ground1_6 0 0 0 1 1\n"
								 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) ground1_6 0 0 0 1 1\n"
								 "( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) ground1_6 0 0 0 1 1\n"
								 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) ground1_6 0 0 0 1 1\n"
								 "( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) ground1_6 0 0 0 1 1\n"
								 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) ground1_6 0 0 0 1 1\n"
								 "}\n}\n{\n\"classname\" \"light\"\n\"origin\" \"32 32 32\"\n}\n";
		if (expect(writeFile(quakePath, quake), "Quake fixture")) {
			shell.openPathFromCommandLine(quakePath);
			action(QStringLiteral("shell.mode.levels"))->trigger();
			if (expect(until([&] { return shell.levelDocument().sourcePath == quakePath && shell.levelDocument().brushes.size() == 1; }), "Quake map ready")
				&& expect(shell.levelDocument().format == LevelMapFormat::QuakeMap, "opens as a Quake map")) {
				expect(!action(QStringLiteral("map.filterPatches"))->isEnabled() && action(QStringLiteral("map.filterWorld"))->isEnabled()
						&& action(QStringLiteral("map.invertFilters"))->isEnabled(),
					"a map without patches disables Patches only");
				expect(!shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-patches")) || !shell.findChild<QCheckBox*>(QStringLiteral("levelFilter-patches"))->isVisible(),
					"no Patches check box for a Quake map");
			}
		}
	}
	if (ok) {
		std::cout << "View filters UI: commands, keys, invert, reset and persistence passed." << std::endl;
	}
	return ok ? 0 : 1;
}
