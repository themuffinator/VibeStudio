// Large grid sizes in the offscreen shell: Grid 512 and Grid 1024 in the view
// bar, the View tab and the palette; ] stepping past 256 and stopping at 1024;
// every plan pane following; nudges landing on 1024-unit steps as one undo
// step; NetRadiant Custom turning Snap on with each grid change (and the
// gesture preference that turns that off); Eureka's 9 and 0 keys; and the size
// kept for the next session. Runs the NRC, TrenchBroom and default profiles at
// 100% and at 200% with high contrast, expanded strings and right to left.
// Workflow reference: NRC radiant/grid.cpp at 449778b3; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_theme.h"
#include "core/level_document.h"
#include "core/level_editor_controls.h"
#include "core/level_map.h"
#include "core/studio_semantics.h"
#include "core/studio_settings.h"
#include "tests/level_geometry_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QJsonObject>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <cmath>
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
	while (!ready() && timer.elapsed() < 30000) {
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
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};

QByteArray mapBytes(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

} // namespace

int main(int argc, char** argv)
{
	// Owned offscreen Qt widgets and semantic key events only.
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

	// One 32-unit brush around the origin; original synthetic geometry.
	LevelMapDocument fixture;
	QString error;
	const QString path = temp.filePath(QStringLiteral("grid.map"));
	if (!expect(tests::createGeometryFixture(1, &fixture, &error) && saveLevelMapAs(fixture, path).succeeded(), "fixture", error)) {
		return 1;
	}
	const QVector<double> sizes = levelEditorGridSizes();

	struct Run {
		const char* profile;
		int scale;
	};
	const Run runs[] = {{"netradiant-custom", 100}, {"netradiant-custom", 200}, {"trenchbroom", 100}, {"trenchbroom", 200},
		{"vibestudio-default", 100}, {"vibestudio-default", 200}, {"eureka", 100}};
	bool first = true;
	for (const Run& run : runs) {
		const QString profileId = QString::fromLatin1(run.profile);
		const bool radiant = profileId == QLatin1String("netradiant-custom");
		phase = QStringLiteral("%1 %2%").arg(profileId).arg(run.scale);
		std::cout << "Large grid UI " << phase.toStdString() << std::endl;
		settings.setSelectedEditorProfileId(profileId);
		settings.setTextScalePercent(run.scale);
		settings.setTheme(run.scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, run.scale));
		Expanded expanded;
		if (run.scale == 200) {
			app.installTranslator(&expanded);
			app.setLayoutDirection(Qt::RightToLeft);
		}
		{
			ApplicationShell shell;
			shell.resize(1600, 1000);
			shell.show();
			shell.openPathFromCommandLine(path);
			const auto action = [&](const char* id) { return shell.findChild<QAction*>(QLatin1String(id)); };
			action("shell.mode.levels")->trigger();
			if (QAction* layout = action("map.layout.four-views")) {
				layout->trigger();
			}
			auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
			auto* combo = shell.findChild<QComboBox*>(QStringLiteral("levelMapGrid"));
			auto* sidebarGrid = shell.findChild<QComboBox*>(QStringLiteral("levelViewGrid"));
			auto* snap = shell.findChild<QCheckBox*>(QStringLiteral("levelMapSnap"));
			if (!expect(view && combo && sidebarGrid && snap, "grid controls exist")) {
				return 1;
			}
			if (!expect(until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 1 && view->isEnabled(); }),
					"map ready")) {
				return 1;
			}
			// The size chosen in the previous run comes back with the next session.
			if (!first) {
				expect(combo->currentData().toInt() == 1024 && view->gridSize() == 1024, "the 1024 grid is kept for the next session",
					QString::number(combo->currentData().toInt()));
			}
			first = false;

			// Both lists offer every size, labelled and accessible.
			expect(combo->count() == sizes.size() && sidebarGrid->count() == sizes.size(), "both lists offer 0.125 to 1024",
				QStringLiteral("%1 / %2").arg(combo->count()).arg(sidebarGrid->count()));
			for (int index = 0; index < std::min(combo->count(), sidebarGrid->count()); ++index) {
				const QString units = QString::number(sizes.value(index));
				expect(combo->itemData(index).toDouble() == sizes.value(index) && sidebarGrid->itemData(index).toDouble() == sizes.value(index)
						&& combo->itemText(index).contains(units) && sidebarGrid->itemText(index).contains(units),
					"list item carries its size", combo->itemText(index) + QStringLiteral(" / ") + sidebarGrid->itemText(index));
				expect((run.scale == 200) == sidebarGrid->itemText(index).startsWith(QLatin1Char('[')), "View tab sizes follow the translation",
					sidebarGrid->itemText(index));
			}
			expect(!combo->accessibleName().isEmpty() && !sidebarGrid->accessibleName().isEmpty() && QAccessible::queryAccessibleInterface(combo)
					&& QAccessible::queryAccessibleInterface(sidebarGrid) && combo->focusPolicy() != Qt::NoFocus && sidebarGrid->focusPolicy() != Qt::NoFocus,
				"grid lists are named and keyboard reachable");

			// The two new commands are registered, described and in the palette.
			for (const int units : {512, 1024}) {
				const QString id = QStringLiteral("map.grid%1").arg(units);
				QAction* grid = shell.findChild<QAction*>(id);
				CommandPaletteEntry entry;
				expect(grid && grid->text().contains(QString::number(units)) && grid->toolTip().contains(QString::number(units))
						&& commandPaletteEntryForCommandId(id, &entry) && entry.label.contains(QString::number(units))
						&& (run.scale == 200) == entry.label.startsWith(QLatin1Char('[')),
					"large grid command is labelled, described and translated", id + (grid ? QStringLiteral(" ") + grid->toolTip() : QString()));
			}

			// The focus helper presses real keys on the active plan view.
			const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
				if (QApplication::focusWidget() != view) {
					shell.activateWindow();
					until([&] { return QApplication::activeWindow() == &shell; });
					view->setFocus(Qt::OtherFocusReason);
					until([&] { return view->hasFocus(); });
				}
				QTest::keyClick(view, key, modifiers);
				settle();
			};
			const auto panesAt = [&](int units) {
				const auto panes = shell.findChildren<MapViewport*>();
				return !panes.isEmpty() && std::all_of(panes.cbegin(), panes.cend(), [units](const MapViewport* pane) { return pane->gridSize() == units; });
			};

			// A grid command: NRC turns Snap back on, the others leave it.
			snap->setChecked(false);
			action("map.grid256")->trigger();
			expect(combo->currentData().toInt() == 256 && view->gridSize() == 256 && snap->isChecked() == radiant,
				"Grid 256 sets the grid and applies the profile's snap policy", shell.statusBar()->currentMessage());
			expect(shell.statusBar()->currentMessage().contains(QStringLiteral("256")), "status bar names the size", shell.statusBar()->currentMessage());
			expect(action("map.gridLarger")->isEnabled(), "Larger Grid is available past 256");

			// ] steps past 256 to 512 and 1024, where Larger Grid stops.
			snap->setChecked(false);
			press(Qt::Key_BracketRight);
			expect(combo->currentData().toInt() == 512 && until([&] { return panesAt(512); }) && snap->isChecked() == radiant,
				"] steps to 512 in every pane", shell.statusBar()->currentMessage());
			expect(sidebarGrid->currentData().toInt() == 512, "the View tab follows");
			press(Qt::Key_BracketRight);
			expect(combo->currentData().toInt() == 1024 && until([&] { return panesAt(1024); }), "] steps to 1024", shell.statusBar()->currentMessage());
			expect(!action("map.gridLarger")->isEnabled() && action("map.gridSmaller")->isEnabled(), "Larger Grid stops at 1024");
			expect(shell.statusBar()->currentMessage().contains(QStringLiteral("1024")), "status bar names 1024", shell.statusBar()->currentMessage());
			expect(StudioSettings().shellLayoutState(QStringLiteral("levelMapGrid")) == QByteArrayLiteral("1024"), "1024 is saved for the next session");
			QAccessibleInterface* comboAccess = QAccessible::queryAccessibleInterface(combo);
			expect(comboAccess && comboAccess->text(QAccessible::Value).contains(QStringLiteral("1024")), "screen readers hear the new size",
				comboAccess ? comboAccess->text(QAccessible::Value) : QString());
			press(Qt::Key_BracketRight);
			expect(combo->currentData().toInt() == 1024, "] at 1024 changes nothing");

			// Keyboard choices in either list are grid commands too.
			snap->setChecked(false);
			QTest::keyClick(sidebarGrid, Qt::Key_Up);
			settle();
			expect(combo->currentData().toInt() == 512 && snap->isChecked() == radiant, "the View tab list steps down and follows the snap policy",
				QString::number(combo->currentData().toInt()));
			snap->setChecked(false);
			QTest::keyClick(combo, Qt::Key_Down);
			settle();
			expect(combo->currentData().toInt() == 1024 && sidebarGrid->currentData().toInt() == 1024 && snap->isChecked() == radiant,
				"the view-bar list steps up and follows the snap policy", QString::number(combo->currentData().toInt()));

			// Eureka keeps 512 and 1024 on 9 and 0.
			if (profileId == QLatin1String("eureka")) {
				expect(action("map.grid512")->shortcuts() == QList<QKeySequence>{QKeySequence(QStringLiteral("9"))}
						&& action("map.grid1024")->shortcuts() == QList<QKeySequence>{QKeySequence(QStringLiteral("0"))},
					"Eureka's digit keys are installed");
				action("map.grid16")->trigger();
				press(Qt::Key_9);
				expect(combo->currentData().toInt() == 512, "9 chooses Grid 512", QString::number(combo->currentData().toInt()));
				press(Qt::Key_0);
				expect(combo->currentData().toInt() == 1024, "0 chooses Grid 1024", QString::number(combo->currentData().toInt()));
			} else {
				expect(action("map.grid512")->shortcuts().isEmpty() && action("map.grid1024")->shortcuts().isEmpty(),
					"Radiant and TrenchBroom give the large grids no key");
			}

			// A nudge with Snap on moves the selection one 1024-unit step, as
			// one undo step that undoes exactly.
			snap->setChecked(true);
			const QVector<LevelMapSelectionRef> brush{{LevelMapSelectionKind::QuakeBrush, 0}};
			view->setSelectionSet(brush);
			view->selectionSetChanged(brush);
			settle();
			const QByteArray baseline = mapBytes(shell.levelDocument());
			const auto pointX = [&] { return shell.levelDocument().brushes.first().faces.first().p0.x; };
			const double startX = pointX();
			const int undo = shell.levelDocument().undoStack.size();
			// NRC's arrows steer the camera; Alt+arrow nudges, as in Radiant.
			press(Qt::Key_Right, radiant ? Qt::AltModifier : Qt::NoModifier);
			expect(until([&] { return pointX() != startX; }) && qFuzzyCompare(std::abs(pointX() - startX), 1024.0)
					&& shell.levelDocument().undoStack.size() == undo + 1,
				"a nudge moves one 1024-unit step as one undo step", QStringLiteral("%1 -> %2").arg(startX).arg(pointX()));
			action("map.undo")->trigger();
			expect(until([&] { return mapBytes(shell.levelDocument()) == baseline; }), "the nudge undoes exactly");
			expect(combo->currentData().toInt() == 1024, "the run ends on 1024 for the next session");
		}
		if (run.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}

	// The snap policy is a gesture preference: with it off, NRC leaves Snap
	// as it was, and the kept 1024 grid still comes back.
	{
		phase = QStringLiteral("netradiant-custom policy off");
		settings.setSelectedEditorProfileId(QStringLiteral("netradiant-custom"));
		settings.setTextScalePercent(100);
		settings.setTheme(StudioTheme::Dark);
		expect(settings.setEditorGestureOverrides(QStringLiteral("netradiant-custom"), {{QStringLiteral("editing.gridChangeEnablesSnap"), QStringLiteral("off")}}, &error),
			"policy override saved", error);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, 100));
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		shell.openPathFromCommandLine(path);
		shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
		auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* combo = shell.findChild<QComboBox*>(QStringLiteral("levelMapGrid"));
		auto* snap = shell.findChild<QCheckBox*>(QStringLiteral("levelMapSnap"));
		if (expect(view && combo && snap && until([&] { return shell.levelDocument().brushes.size() == 1 && view->isEnabled(); }), "map ready")) {
			expect(combo->currentData().toInt() == 1024, "1024 restored under the overridden profile");
			snap->setChecked(false);
			shell.findChild<QAction*>(QStringLiteral("map.grid512"))->trigger();
			expect(combo->currentData().toInt() == 512 && !snap->isChecked(), "with the policy off, Snap stays off");
		}
	}

	if (!ok) {
		std::cerr << "level large grid UI smoke test failed" << std::endl;
		return 1;
	}
	std::cout << "level large grid UI smoke test passed" << std::endl;
	return 0;
}
