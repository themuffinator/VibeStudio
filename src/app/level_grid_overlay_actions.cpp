// View > Show Grid Coordinates and Show Block Grid: the Levels view bar's
// Show > Grid Coordinates and Show > Block Grid as bindable, checkable
// commands. The Show menu items own the saved choices (with the rest of the
// Show menu, in the shell layout state); the commands trigger them, follow
// their check marks and say in the status bar what is now drawn. They change
// only the picture: the map, its undo history, the selection, scene locks,
// hidden objects and the open group are untouched.
//
// Workflow reference: NRC ShowCoordinatesToggle and ShowBlocksToggle in
// radiant/xywindow.cpp at 449778b3 (GPL-2.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "core/level_block_grid.h"

#include <QAction>
#include <QStatusBar>

#include <utility>

namespace vibestudio {

void ApplicationShell::registerLevelGridOverlayCommands()
{
	if (!m_commands) {
		return;
	}
	const auto add = [this](const QString& id, const QString& label, const QString& tip, QAction* ApplicationShell::*toggle) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = QStringLiteral("grid");
		command.checkable = true;
		command.handler = [this, toggle]() { toggleLevelGridOverlay(this->*toggle); };
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.toggleGridCoordinates"), tr("Show Grid Coordinates"),
		tr("Label the grid lines with their coordinates along the bottom and leading edges of the 2D views, clear of the status tags."),
		&ApplicationShell::m_levelMapShowCoordinates);
	add(QStringLiteral("map.toggleBlockGrid"), tr("Show Block Grid"),
		tr("Draw dashed lines in the 2D views where the compiler splits the world into blocks, from worldspawn _blocksize (1024 units by default)."),
		&ApplicationShell::m_levelMapShowBlockGrid);
	refreshLevelGridOverlayCommands();
}

void ApplicationShell::refreshLevelGridOverlayCommands()
{
	if (!m_commands) {
		return;
	}
	// Doom maps have no compiler block grid; coordinates suit every map and,
	// like Show Grid, can be chosen before one is open.
	m_commands->setEnabled(QStringLiteral("map.toggleGridCoordinates"), true);
	m_commands->setEnabled(QStringLiteral("map.toggleBlockGrid"), m_levelMapDocument.format != LevelMapFormat::DoomWad);
	const std::pair<const char*, QAction*> checks[] = {
		{"map.toggleGridCoordinates", m_levelMapShowCoordinates},
		{"map.toggleBlockGrid", m_levelMapShowBlockGrid},
	};
	for (const auto& [id, toggle] : checks) {
		QAction* command = m_commands->action(QString::fromLatin1(id));
		if (command && toggle && command->isChecked() != toggle->isChecked()) {
			command->setChecked(toggle->isChecked());
		}
	}
}

void ApplicationShell::toggleLevelGridOverlay(QAction* toggle)
{
	if (!toggle) {
		return;
	}
	// The Show menu item saves the choice, redraws the views and moves the
	// command's check mark (refreshLevelGridOverlayCommands()).
	toggle->trigger();
	refreshLevelGridOverlayCommands();
	QString message;
	if (toggle == m_levelMapShowCoordinates) {
		message = toggle->isChecked() ? tr("Grid coordinates on: the 2D views label their grid lines.") : tr("Grid coordinates off.");
	} else if (!toggle->isChecked()) {
		message = tr("Block grid off.");
	} else if (!levelMapHasBlockGrid(m_levelMapDocument)) {
		message = tr("Block grid on: it is drawn on Quake-family maps.");
	} else {
		const LevelMapBlockSize blocks = levelMapBlockSize(m_levelMapDocument);
		message = blocks.anyAxis()
			? tr("Block grid on: blocks of %1 units, from %2.").arg(levelBlockSizeText(blocks), levelBlockSizeSourceText(blocks))
			: tr("Block grid on, but %1 leaves every axis unsplit, so there are no block lines to draw.").arg(levelBlockSizeSourceText(blocks));
		if (m_levelMapDocument.format == LevelMapFormat::QuakeMap) {
			message += QLatin1Char(' ')
				+ tr("q3map2-family compilers such as VibeMap3 split the world on this grid; Quake and Quake II compilers such as VibeMap2 do not.");
		}
	}
	statusBar()->showMessage(message, 6000);
}

} // namespace vibestudio
