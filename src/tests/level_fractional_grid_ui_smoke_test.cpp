// Fractional grid sizes in the offscreen shell: Grid 0.125, 0.25 and 0.5 in
// the view bar, the View tab and the palette; [ stepping below 1 unit and
// stopping at 0.125; every plan pane following; a nudge landing on a
// quarter-unit step as one undo step; snapped pointer positions on an
// eighth-unit grid; the status lines saying how far apart the thinned lines
// are drawn; the size kept for the next session; and binary Doom maps, which
// grey the fractions out, move a fractional choice to 1 unit and stop [ there.
// Runs the NRC, TrenchBroom and default profiles at 100% and at 200% with
// high contrast, expanded strings and right to left. Workflow reference: NRC
// radiant/grid.cpp at 449778b3 and TB MapWindow at 4c756be1; see
// docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_grid.h"
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
#include <QStandardItemModel>
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

bool itemEnabled(const QComboBox* list, int index)
{
	const auto* model = qobject_cast<const QStandardItemModel*>(list->model());
	return model && model->item(index) && model->item(index)->isEnabled();
}

bool onGrid(double value, double grid)
{
	return std::abs(value / grid - std::round(value / grid)) < 1e-9;
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

	// One 32-unit brush around the origin, and a Doom starter room; original
	// synthetic geometry.
	LevelMapDocument fixture;
	QString error;
	const QString path = temp.filePath(QStringLiteral("fraction.map"));
	if (!expect(tests::createGeometryFixture(1, &fixture, &error) && saveLevelMapAs(fixture, path).succeeded(), "fixture", error)) {
		return 1;
	}
	LevelMapDocument doomFixture;
	LevelMapCreateRequest doomRequest;
	doomRequest.game = QStringLiteral("doom");
	const QString doomPath = temp.filePath(QStringLiteral("fraction.wad"));
	if (!expect(createLevelMap(doomRequest, &doomFixture, &error) && saveLevelMapAs(doomFixture, doomPath).succeeded(), "Doom fixture", error)) {
		return 1;
	}
	const QVector<double> fractions{0.125, 0.25, 0.5};

	// The painter thins a fine grid out at low zoom and draws every line once
	// zoomed in far enough.
	phase = QStringLiteral("grid painter");
	expect(mapGridDrawnSpacing(0.125, 1.0) == 8.0 && mapGridDrawnSpacing(0.125, 64.0) == 0.125 && mapGridDrawnSpacing(0.5, 16.0) == 0.5,
		"fine grids thin out to at least 6 pixels apart", QString::number(mapGridDrawnSpacing(0.125, 1.0)));
	{
		const MapGridView fine{QSize(400, 300), QPointF(0.0625, 0.0625), 64.0, 1, 0.125, 0, 0, 0};
		const auto lines = mapGridLines(fine);
		bool placed = !lines[0].isEmpty();
		for (const QLineF& line : lines[0]) {
			const bool vertical = line.x1() == line.x2();
			const double at = vertical ? line.x1() : line.y1();
			const double world = vertical ? fine.center.x() + (at - 200.0) / 64.0 : fine.center.y() - (at - 150.0) / 64.0;
			placed &= onGrid(world, 0.125);
		}
		expect(placed, "eighth-unit lines land on multiples of 0.125 when zoomed in");
	}

	struct Run {
		const char* profile;
		int scale;
	};
	const Run runs[] = {{"netradiant-custom", 100}, {"trenchbroom", 200}, {"vibestudio-default", 100}, {"netradiant-custom", 200}};
	bool first = true;
	for (const Run& run : runs) {
		const QString profileId = QString::fromLatin1(run.profile);
		const bool radiant = profileId == QLatin1String("netradiant-custom");
		phase = QStringLiteral("%1 %2%").arg(profileId).arg(run.scale);
		std::cout << "Fractional grid UI " << phase.toStdString() << std::endl;
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
			const auto action = [&](const QString& id) { return shell.findChild<QAction*>(id); };
			action(QStringLiteral("shell.mode.levels"))->trigger();
			if (QAction* layout = action(QStringLiteral("map.layout.four-views"))) {
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
			// The quarter-unit grid chosen in the previous run comes back.
			if (!first) {
				expect(combo->currentData().toDouble() == 0.25 && view->gridSize() == 0.25, "the 0.25 grid is kept for the next session",
					QString::number(combo->currentData().toDouble()));
			}
			first = false;

			// Both lists start with the fractions, labelled, enabled on a Quake map.
			for (int index = 0; index < 3; ++index) {
				const QString size = levelEditorGridSizeText(fractions.at(index));
				expect(combo->itemData(index).toDouble() == fractions.at(index) && sidebarGrid->itemData(index).toDouble() == fractions.at(index)
						&& combo->itemText(index).contains(size) && sidebarGrid->itemText(index).contains(size),
					"list item carries its fractional size", combo->itemText(index) + QStringLiteral(" / ") + sidebarGrid->itemText(index));
				expect(itemEnabled(combo, index) && itemEnabled(sidebarGrid, index), "fractions are available on a Quake map", size);
				expect((run.scale == 200) == sidebarGrid->itemText(index).startsWith(QLatin1Char('[')), "View tab sizes follow the translation",
					sidebarGrid->itemText(index));
			}

			// The three commands are registered, described and in the palette.
			for (const double units : fractions) {
				const QString id = levelEditorGridCommandId(units);
				const QString size = levelEditorGridSizeText(units);
				QAction* grid = action(id);
				CommandPaletteEntry entry;
				expect(grid && grid->isEnabled() && grid->text().contains(size) && grid->toolTip().contains(size)
						&& grid->shortcuts().isEmpty() && commandPaletteEntryForCommandId(id, &entry) && entry.label.contains(size)
						&& (run.scale == 200) == entry.label.startsWith(QLatin1Char('[')),
					"fractional grid command is labelled, keyless, described and translated", id + (grid ? QStringLiteral(" ") + grid->toolTip() : QString()));
			}

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
			const auto panesAt = [&](double units) {
				const auto panes = shell.findChildren<MapViewport*>();
				return !panes.isEmpty() && std::all_of(panes.cbegin(), panes.cend(), [units](const MapViewport* pane) { return pane->gridSize() == units; });
			};

			// [ halves below 1 unit to 0.125, where Smaller Grid stops.
			action(QStringLiteral("map.grid1"))->trigger();
			snap->setChecked(false);
			press(Qt::Key_BracketLeft);
			expect(combo->currentData().toDouble() == 0.5 && until([&] { return panesAt(0.5); }) && snap->isChecked() == radiant,
				"[ steps from 1 to 0.5 in every pane and applies the profile's snap policy", shell.statusBar()->currentMessage());
			expect(shell.statusBar()->currentMessage().contains(QStringLiteral("0.5")), "status bar names 0.5", shell.statusBar()->currentMessage());
			expect(sidebarGrid->currentData().toDouble() == 0.5, "the View tab follows");
			press(Qt::Key_BracketLeft);
			press(Qt::Key_BracketLeft);
			expect(combo->currentData().toDouble() == 0.125 && until([&] { return panesAt(0.125); }), "[ reaches 0.125", shell.statusBar()->currentMessage());
			expect(action(QStringLiteral("map.grid0.125"))->isChecked() && !action(QStringLiteral("map.grid1"))->isChecked(), "the check mark follows [");
			expect(!action(QStringLiteral("map.gridSmaller"))->isEnabled() && action(QStringLiteral("map.gridLarger"))->isEnabled(), "Smaller Grid stops at 0.125");
			QAccessibleInterface* comboAccess = QAccessible::queryAccessibleInterface(combo);
			expect(comboAccess && comboAccess->text(QAccessible::Value).contains(QStringLiteral("0.125")), "screen readers hear the new size",
				comboAccess ? comboAccess->text(QAccessible::Value) : QString());
			press(Qt::Key_BracketLeft);
			expect(combo->currentData().toDouble() == 0.125 && shell.statusBar()->currentMessage().contains(QStringLiteral("0.125")),
				"[ at 0.125 changes nothing and says so", shell.statusBar()->currentMessage());

			// Snapped pointer positions land on the eighth-unit grid.
			snap->setChecked(true);
			settle();
			const LevelMapVec3 snappedPoint = view->worldPositionAt(QPointF(view->width() * 0.37, view->height() * 0.61), 0.0, true);
			expect(onGrid(snappedPoint.x, 0.125) && onGrid(snappedPoint.y, 0.125) && onGrid(snappedPoint.z, 0.125), "pointer positions snap to 0.125",
				QStringLiteral("%1 %2 %3").arg(snappedPoint.x).arg(snappedPoint.y).arg(snappedPoint.z));

			// At ordinary zoom the eighth-unit lines are thinned out, and the
			// status lines say how far apart they are drawn.
			const QString gridLine = view->statusLines().filter(QStringLiteral("0.125")).value(0);
			if (mapGridDrawnSpacing(0.125, view->zoom()) > 0.125) {
				expect(gridLine.contains(QString::number(mapGridDrawnSpacing(0.125, view->zoom()))), "status lines give the drawn line spacing", gridLine);
			} else {
				expect(!gridLine.isEmpty(), "status lines give the grid", gridLine);
			}

			// The palette commands choose a size directly.
			snap->setChecked(false);
			action(QStringLiteral("map.grid0.25"))->trigger();
			expect(combo->currentData().toDouble() == 0.25 && until([&] { return panesAt(0.25); }) && snap->isChecked() == radiant,
				"Grid 0.25 sets the grid and applies the profile's snap policy", shell.statusBar()->currentMessage());
			expect(StudioSettings().shellLayoutState(QStringLiteral("levelMapGrid")) == QByteArrayLiteral("0.25"), "0.25 is saved for the next session",
				QString::fromUtf8(StudioSettings().shellLayoutState(QStringLiteral("levelMapGrid"))));
			// The grid commands are radio items, checked while their size is the
			// working grid; choosing the checked one again keeps it.
			expect(action(QStringLiteral("map.grid0.25"))->isCheckable() && action(QStringLiteral("map.grid0.25"))->isChecked()
					&& !action(QStringLiteral("map.grid0.125"))->isChecked() && !action(QStringLiteral("map.grid1"))->isChecked(),
				"only Grid 0.25 is checked");
			action(QStringLiteral("map.grid0.25"))->trigger();
			expect(action(QStringLiteral("map.grid0.25"))->isChecked() && combo->currentData().toDouble() == 0.25, "choosing Grid 0.25 again keeps it checked");

			// Keyboard choices in either list reach the fractions.
			QTest::keyClick(sidebarGrid, Qt::Key_Up);
			settle();
			expect(combo->currentData().toDouble() == 0.125, "the View tab list steps down to 0.125", QString::number(combo->currentData().toDouble()));
			QTest::keyClick(combo, Qt::Key_Down);
			settle();
			expect(combo->currentData().toDouble() == 0.25 && sidebarGrid->currentData().toDouble() == 0.25, "the view-bar list steps up to 0.25");

			// A nudge with Snap on moves the brush one quarter-unit step, as one
			// undo step that undoes exactly.
			snap->setChecked(true);
			const QVector<LevelMapSelectionRef> brush{{LevelMapSelectionKind::QuakeBrush, 0}};
			view->setSelectionSet(brush);
			view->selectionSetChanged(brush);
			settle();
			const QByteArray baseline = mapBytes(shell.levelDocument());
			const auto pointX = [&] { return shell.levelDocument().brushes.first().faces.first().p0.x; };
			const double startX = pointX();
			const int undo = static_cast<int>(shell.levelDocument().undoStack.size());
			// NRC's arrows steer the camera; Alt+arrow nudges, as in Radiant.
			press(Qt::Key_Right, radiant ? Qt::AltModifier : Qt::NoModifier);
			expect(until([&] { return pointX() != startX; }) && std::abs(std::abs(pointX() - startX) - 0.25) < 1e-9
					&& shell.levelDocument().undoStack.size() == undo + 1,
				"a nudge moves one 0.25-unit step as one undo step", QStringLiteral("%1 -> %2").arg(startX).arg(pointX()));
			action(QStringLiteral("map.undo"))->trigger();
			expect(until([&] { return mapBytes(shell.levelDocument()) == baseline; }), "the nudge undoes exactly");
			expect(combo->currentData().toDouble() == 0.25, "the run ends on 0.25 for the next session");
		}
		if (run.scale == 200) {
			app.removeTranslator(&expanded);
			app.setLayoutDirection(Qt::LeftToRight);
		}
	}

	// Binary Doom maps store whole units: opening one moves the kept 0.25 grid
	// to 1, greys the fractions out in both lists and the palette, and [ stops
	// at 1. Back on a Quake map the fractions return.
	{
		phase = QStringLiteral("binary Doom map");
		settings.setSelectedEditorProfileId(QStringLiteral("trenchbroom"));
		settings.setTextScalePercent(100);
		settings.setTheme(StudioTheme::HighContrastDark);
		settings.sync();
		applyStudioTheme(app, studioThemeTokens(settings.accessibilityPreferences().theme, UiDensity::Standard, 100));
		ApplicationShell shell;
		shell.resize(1600, 1000);
		shell.show();
		shell.findChild<QAction*>(QStringLiteral("shell.mode.levels"))->trigger();
		auto* view = shell.findChild<MapViewport*>(QStringLiteral("mapViewport"));
		auto* combo = shell.findChild<QComboBox*>(QStringLiteral("levelMapGrid"));
		auto* sidebarGrid = shell.findChild<QComboBox*>(QStringLiteral("levelViewGrid"));
		if (expect(view && combo && sidebarGrid, "grid controls exist")) {
			expect(combo->currentData().toDouble() == 0.25 && itemEnabled(combo, 1), "the kept 0.25 grid is back before any map opens");
			shell.openPathFromCommandLine(doomPath);
			expect(until([&] { return shell.levelDocument().format == LevelMapFormat::DoomWad && view->isEnabled(); }), "Doom map ready");
			expect(until([&] { return combo->currentData().toDouble() == 1.0; }) && until([&] { return view->gridSize() == 1.0; }),
				"a fractional grid moves to 1 unit on a Doom map", QString::number(combo->currentData().toDouble()));
			for (int index = 0; index < 3; ++index) {
				expect(!itemEnabled(combo, index) && !itemEnabled(sidebarGrid, index), "fractions are greyed out on a Doom map", combo->itemText(index));
				expect(!shell.findChild<QAction*>(levelEditorGridCommandId(fractions.at(index)))->isEnabled(), "fractional commands are disabled",
					levelEditorGridCommandId(fractions.at(index)));
			}
			expect(itemEnabled(combo, 3) && itemEnabled(sidebarGrid, 3), "Grid 1 stays available");
			expect(shell.findChild<QAction*>(QStringLiteral("map.grid1"))->isChecked(), "Grid 1 is the checked grid on a Doom map");
			expect(!shell.findChild<QAction*>(QStringLiteral("map.gridSmaller"))->isEnabled(), "Smaller Grid stops at 1 on a Doom map");
			shell.activateWindow();
			view->setFocus(Qt::OtherFocusReason);
			until([&] { return view->hasFocus(); });
			QTest::keyClick(view, Qt::Key_BracketLeft);
			settle();
			expect(combo->currentData().toDouble() == 1.0, "[ at 1 stays at 1 on a Doom map", shell.statusBar()->currentMessage());
			QTest::keyClick(sidebarGrid, Qt::Key_Up);
			settle();
			expect(combo->currentData().toDouble() == 1.0, "the keyboard skips the greyed-out fractions", QString::number(combo->currentData().toDouble()));

			shell.openPathFromCommandLine(path);
			// The fixture is a Quake III map, which keeps fractions as Quake's does.
			expect(until([&] { return shell.levelDocument().sourcePath == path && shell.levelDocument().brushes.size() == 1; }),
				"Quake-family map ready again", QString::number(static_cast<int>(shell.levelDocument().format)));
			expect(until([&] { return itemEnabled(combo, 0) && itemEnabled(sidebarGrid, 0); })
					&& shell.findChild<QAction*>(QStringLiteral("map.grid0.125"))->isEnabled() && combo->currentData().toDouble() == 1.0,
				"the fractions return on a Quake map, and the grid stays where it was");
			shell.findChild<QAction*>(QStringLiteral("map.grid0.5"))->trigger();
			expect(combo->currentData().toDouble() == 0.5 && until([&] { return view->gridSize() == 0.5; }), "Grid 0.5 works again");
		}
	}

	if (!ok) {
		std::cerr << "level fractional grid UI smoke test failed" << std::endl;
		return 1;
	}
	std::cout << "level fractional grid UI smoke test passed" << std::endl;
	return 0;
}
