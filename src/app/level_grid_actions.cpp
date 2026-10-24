// The Levels working grid: Grid 0.125 to Grid 1024, Smaller Grid and Larger
// Grid. Every route (view bar, View tab, palette, profile keys) shares the
// sizes in core/level_editor_controls.h. Binary Doom and Hexen maps store
// whole-unit coordinates, so while one is open the grids finer than 1 unit are
// greyed out and stepping stops at 1. Under a profile whose controls ask for
// it (NetRadiant Custom and NetRadiant), choosing or stepping the grid turns
// Snap back on. Workflow reference: NRC radiant/grid.cpp GridPrev, GridNext
// and setGridPower at 449778b3 (GPL-2.0-or-later) and TB MapWindow
// decGridSize/setGridSize at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"

#include <QCheckBox>
#include <QComboBox>
#include <QStandardItemModel>
#include <QStatusBar>

#include <algorithm>
#include <cmath>

namespace vibestudio {

namespace {

// True when the profile's grid commands turned Snap on just now.
bool turnSnapOnForGridChange(const LevelEditorControls& controls, QCheckBox* snap)
{
	if (!controls.gridChangeEnablesSnap || !snap || snap->isChecked()) {
		return false;
	}
	snap->setChecked(true);
	return true;
}

// Greys out the sizes finer than `finest` in one grid list. QComboBox skips
// disabled items when the keyboard or the wheel steps through the list.
void enableGridSizesFrom(QComboBox* list, double finest)
{
	auto* model = list ? qobject_cast<QStandardItemModel*>(list->model()) : nullptr;
	if (!model) {
		return;
	}
	for (int row = 0; row < model->rowCount(); ++row) {
		if (QStandardItem* item = model->item(row)) {
			const bool allowed = item->data(Qt::UserRole).toDouble() >= finest;
			if (item->isEnabled() != allowed) {
				item->setEnabled(allowed);
			}
		}
	}
}

} // namespace

double ApplicationShell::levelMapGridUnits() const
{
	const double chosen = m_levelMapGrid ? m_levelMapGrid->currentData().toDouble() : 16.0;
	const double units = std::isfinite(chosen) && chosen > 0.0 ? chosen : 16.0;
	return std::max(units, levelEditorFinestGridUnits(m_levelMapDocument));
}

void ApplicationShell::refreshLevelMapGridChoices()
{
	if (!m_levelMapGrid) {
		return;
	}
	const double finest = levelEditorFinestGridUnits(m_levelMapDocument);
	if (finest == m_levelMapGridFinest) {
		return;
	}
	// Recorded first: moving the choice below refreshes the views, which
	// calls back in here.
	m_levelMapGridFinest = finest;
	enableGridSizesFrom(m_levelMapGrid, finest);
	enableGridSizesFrom(findChild<QComboBox*>(QStringLiteral("levelViewGrid")), finest);
	if (m_levelMapGrid->currentData().toDouble() < finest) {
		if (const int index = m_levelMapGrid->findData(finest); index >= 0) {
			m_levelMapGrid->setCurrentIndex(index);
			statusBar()->showMessage(tr("Doom and Hexen maps store whole units, so the grid is now 1 unit."), 4000);
		}
	}
}

void ApplicationShell::stepLevelMapGrid(int direction)
{
	if (!m_levelMapGrid) {
		return;
	}
	// Radiant turns snapping on before it checks the limit, so a step at the
	// finest or coarsest grid still turns Snap on.
	const bool snapped = turnSnapOnForGridChange(m_levelControls, m_levelMapSnap);
	const double finest = levelEditorFinestGridUnits(m_levelMapDocument);
	const double current = levelMapGridUnits();
	const double next = levelEditorSteppedGridUnits(current, direction, finest);
	const int index = m_levelMapGrid->findData(next);
	const QString size = levelEditorGridSizeText(current);
	if (next == current || index < 0) {
		if (direction >= 0) {
			statusBar()->showMessage(snapped ? tr("The grid is already at its coarsest, %1 units. Snap to grid turned on.").arg(size)
							 : tr("The grid is already at its coarsest, %1 units.").arg(size));
		} else if (finest >= 1.0) {
			statusBar()->showMessage(snapped ? tr("The grid is already at 1 unit, the finest a Doom or Hexen map can store. Snap to grid turned on.")
							 : tr("The grid is already at 1 unit, the finest a Doom or Hexen map can store."));
		} else {
			statusBar()->showMessage(snapped ? tr("The grid is already at its finest, %1 units. Snap to grid turned on.").arg(size)
							 : tr("The grid is already at its finest, %1 units.").arg(size));
		}
		return;
	}
	// The list's change handler refreshes the views and Smaller/Larger Grid.
	m_levelMapGrid->setCurrentIndex(index);
	const QString units = levelEditorGridSizeText(next);
	statusBar()->showMessage(snapped ? tr("Grid: %1 units. Snap to grid turned on.").arg(units) : tr("Grid: %1 units.").arg(units));
}

void ApplicationShell::setLevelMapGridUnits(double units)
{
	if (!m_levelMapGrid) {
		return;
	}
	const int index = m_levelMapGrid->findData(units);
	if (index < 0) {
		return;
	}
	if (units < levelEditorFinestGridUnits(m_levelMapDocument)) {
		statusBar()->showMessage(tr("Doom and Hexen maps store whole units, so the finest grid is 1 unit."), 4000);
		return;
	}
	const bool snapped = turnSnapOnForGridChange(m_levelControls, m_levelMapSnap);
	// The list's change handler refreshes the views and Smaller/Larger Grid.
	m_levelMapGrid->setCurrentIndex(index);
	const QString size = levelEditorGridSizeText(units);
	statusBar()->showMessage(snapped ? tr("Grid: %1 units. Snap to grid turned on.").arg(size) : tr("Grid: %1 units.").arg(size), 2000);
}

} // namespace vibestudio
