// View > Filters: one checkable command for each kind of object the Levels
// views can stop drawing, Invert Filters and Show Every Kind. They share the
// View tab's Filters check boxes and their saved list, so a key, the menu,
// command search and the check boxes always agree, and a change applies to
// every plan view and the camera at once. Filters change what is drawn and
// picked, never the map, so nothing here touches undo.
//
// Workflow reference: NRC filters.cpp ToggleFilterFlag, InvertFilters and
// ResetFilters at 449778b3 (GPL-2.0-or-later) / TB ActionManager "View
// Filter > Toggle show brushes" at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/studio_actions.h"
#include "core/level_view_filters.h"

#include <QAction>
#include <QCheckBox>
#include <QIcon>
#include <QSet>
#include <QSignalBlocker>
#include <QStatusBar>

#include <functional>

namespace vibestudio {

void ApplicationShell::registerLevelViewFilterCommands()
{
	if (!m_commands) {
		return;
	}
	// The saved list is read here as well as by the View tab, so a key pressed
	// before that tab is built never saves over the kinds switched off last time.
	m_levelFiltersOff = levelViewFiltersFromSaved(m_settings.shellLayoutState(levelViewFiltersSettingsKey()));
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, bool checkable, bool separatorBefore,
						 std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.menuSection = tr("Filters");
		command.checkable = checkable;
		command.separatorBefore = separatorBefore;
		command.handler = std::move(handler);
		if (QAction* action = m_commands->registerCommand(command); action && checkable) {
			// Leave the menu's indicator column to the check mark.
			action->setIcon(QIcon());
		}
	};
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		const QString id = levelViewFilterCommandId(kind.id);
		add(id, kind.title, tr("%1 Checked while the Levels views draw them; the map is unchanged.").arg(kind.description), QStringLiteral("filter"), true,
			false, [this, id, filterId = kind.id]() {
				const QAction* action = m_commands->action(id);
				setLevelViewFilter(filterId, action && action->isChecked());
			});
	}
	add(QStringLiteral("map.invertFilters"), tr("Invert Filters"),
		tr("Hide every kind the filters show and show every kind they hide, as Radiant's Invert filters does. The map is unchanged."),
		QStringLiteral("refresh"), false, true, [this]() { invertLevelViewFilters(); });
	add(QStringLiteral("map.resetFilters"), tr("Show Every Kind"),
		tr("Show every kind of object in the Levels views again, as Radiant's Reset filters does. The map is unchanged."), QStringLiteral("eye"),
		false, false, [this]() { resetLevelViewFilters(); });
	refreshLevelViewFilterCommands();
}

void ApplicationShell::refreshLevelViewFilterCommands()
{
	if (!m_commands) {
		return;
	}
	QSet<QString> offered;
	for (const LevelViewFilter& filter : levelViewFilters(m_levelMapDocument)) {
		offered.insert(filter.id);
	}
	for (const LevelViewFilter& kind : levelViewFilterKinds()) {
		const QString id = levelViewFilterCommandId(kind.id);
		if (QAction* action = m_commands->action(id)) {
			// Checked while shown, like the View tab's check boxes. Setting the
			// state emits no trigger, so this never runs the command.
			action->setChecked(!m_levelFiltersOff.contains(kind.id));
			m_commands->setEnabled(id, offered.contains(kind.id));
		}
	}
	m_commands->setEnabled(QStringLiteral("map.invertFilters"), !offered.isEmpty());
	m_commands->setEnabled(QStringLiteral("map.resetFilters"), !offered.isEmpty());
}

void ApplicationShell::applyLevelViewFilterState(const QStringList& hiddenFilterIds)
{
	m_levelFiltersOff = levelViewFiltersFromSaved(levelViewFiltersSavedValue(hiddenFilterIds));
	m_settings.setShellLayoutState(levelViewFiltersSettingsKey(), levelViewFiltersSavedValue(m_levelFiltersOff));
	for (auto it = m_levelFilterBoxes.cbegin(); it != m_levelFilterBoxes.cend(); ++it) {
		if (QCheckBox* box = it.value()) {
			const QSignalBlocker blocker(box);
			box->setChecked(!m_levelFiltersOff.contains(it.key()));
		}
	}
	refreshLevelViewFilterCommands();
	applyLevelViewFilters();
	synchronizeLevelPlanViews();
	refreshLevelMapObjectHiddenMarks();
	if (levelMap3DShowing()) {
		refreshLevelMap3D();
	}
}

void ApplicationShell::invertLevelViewFilters()
{
	const QVector<LevelViewFilter> offered = levelViewFilters(m_levelMapDocument);
	if (offered.isEmpty()) {
		statusBar()->showMessage(tr("Open a map to invert its view filters."), 4000);
		return;
	}
	applyLevelViewFilterState(levelViewFiltersInverted(m_levelMapDocument, m_levelFiltersOff));
	int hidden = 0;
	for (const LevelViewFilter& filter : offered) {
		hidden += m_levelFiltersOff.contains(filter.id) ? 1 : 0;
	}
	statusBar()->showMessage(tr("Filters inverted: %n kind(s) now hidden from the views; the map is unchanged.", nullptr, hidden), 4000);
}

void ApplicationShell::resetLevelViewFilters()
{
	applyLevelViewFilterState({});
	statusBar()->showMessage(tr("Every kind of object is shown again; the map is unchanged."), 4000);
}

} // namespace vibestudio
