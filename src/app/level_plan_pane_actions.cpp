// View > Show Top View, Show Front View and Show Side View: which plan panes
// the three-plan layouts (Four Views, Camera Above Plans, Camera Beside
// Plans) show. The choice is a persistent view preference shared with
// `editor layout --panes`; it never edits the map, its selection or its undo
// history, so scene locks and the open group do not apply. One plan pane
// always stays visible, and the active plan moves to a shown pane before its
// own pane hides. Each pane combination keeps its own splitter proportions.
//
// Workflow reference: NRC xywindow.cpp ToggleView, ToggleFrontView and
// ToggleSideView (XYVIS, XZVIS and YZVIS) at 449778b3 (GPL-2.0-or-later).
// Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"

#include <QAction>
#include <QComboBox>
#include <QMenu>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace vibestudio {

namespace {

constexpr std::array<const char*, 3> kPlanPaneCommands {"map.showTopView", "map.showFrontView", "map.showSideView"};

bool splitterHoldsShownPane(const QSplitter* splitter)
{
	for (int i = 0; i < splitter->count(); ++i) {
		if (const QWidget* pane = splitter->widget(i); pane && !pane->isHidden()) {
			return true;
		}
	}
	return false;
}

} // namespace

void ApplicationShell::registerLevelPlanPaneCommands()
{
	if (!m_commands) {
		return;
	}
	const auto add = [this](const char* id, const QString& label, const QString& tip, int index) {
		StudioCommandRegistration command;
		command.commandId = QString::fromLatin1(id);
		command.group = StudioCommandGroup::View;
		command.label = label;
		command.statusTip = tip;
		command.iconName = QStringLiteral("map");
		command.checkable = true;
		command.separatorBefore = index == 0;
		// The model decides, not the action's toggled state: a refused
		// change (the last shown pane) leaves every control as it was.
		command.handler = [this, index]() { showLevelPlanPane(index, !m_levelPlanPanes.shown[static_cast<std::size_t>(index)]); };
		m_commands->registerCommand(command);
	};
	add(kPlanPaneCommands[0], tr("Show Top View"),
		tr("Show or hide the Top plan pane in the three-plan layouts. One plan pane always stays visible; the map is unchanged."), 0);
	add(kPlanPaneCommands[1], tr("Show Front View"),
		tr("Show or hide the Front plan pane in the three-plan layouts. One plan pane always stays visible; the map is unchanged."), 1);
	add(kPlanPaneCommands[2], tr("Show Side View"),
		tr("Show or hide the Side plan pane in the three-plan layouts. One plan pane always stays visible; the map is unchanged."), 2);
	refreshLevelPlanPaneActions();
}

bool ApplicationShell::showLevelPlanPane(int index, bool shown)
{
	if (index < 0 || index >= static_cast<int>(m_levelPlanPanes.shown.size())) {
		return false;
	}
	LevelPlanPanes panes = m_levelPlanPanes;
	if (!setLevelPlanPaneShown(&panes, index, shown)) {
		refreshLevelPlanPaneActions();
		statusBar()->showMessage(tr("One plan view always stays visible. Show another plan view before hiding this one."), 5000);
		return false;
	}
	if (panes == m_levelPlanPanes) {
		refreshLevelPlanPaneActions();
		return true;
	}
	if (!m_settings.setLevelPlanPanes(panes)) {
		refreshLevelPlanPaneActions();
		statusBar()->showMessage(tr("The plan view choice could not be saved to these settings."), 5000);
		return false;
	}
	// A temporary expansion ends first, and the proportions of the
	// combination being left are kept before the panes change.
	restoreLevelViewWorkspace();
	saveLevelViewSplitters();
	m_levelPlanPanes = panes;
	if (levelViewLayoutHasThreePlans(m_levelViewLayout) && m_levelPlanViews.size() == 3 && m_levelMapViews) {
		MapViewport* active = m_levelMapViewport;
		const bool hadFocus = !shown && m_levelPlanViews[index]->hasFocus();
		moveLevelActivePlanToShownPane();
		applyLevelPlanPaneVisibility();
		if (hadFocus) {
			// Keyboard work carries on in the pane that became active.
			m_levelMapViewport->setFocus(Qt::OtherFocusReason);
		}
		const QString prefix = levelViewSplitterStatePrefix(m_levelViewLayout, m_levelPlanPanes);
		QSplitter* holder = qobject_cast<QSplitter*>(m_levelPlanViews[index]->parentWidget());
		{
			const QScopedValueRollback<bool> applying(m_applyingLevelViewLayout, true);
			for (const auto& [name, splitter] : {std::pair {QStringLiteral("root"), m_levelMapViews},
				     std::pair {QStringLiteral("upper"), m_levelMapUpperViews}, std::pair {QStringLiteral("lower"), m_levelMapLowerViews}}) {
				const QByteArray state = m_settings.shellLayoutState(prefix + name);
				if (!state.isEmpty()) {
					splitter->restoreState(state);
				} else if (splitter == holder) {
					// A combination seen for the first time shares its row fairly.
					QList<int> sizes;
					const int extent = std::max(splitter->width(), splitter->height());
					for (int i = 0; i < splitter->count(); ++i) { sizes << extent; }
					splitter->setSizes(sizes);
				}
			}
		}
		if (active != m_levelMapViewport) {
			saveLevelMapViewState();
		}
		synchronizeLevelViewNavigation(m_levelMapViewport);
		refreshLevelCameraMarker();
	}
	refreshCommandEnablement();
	const QString names[] = {tr("Top"), tr("Front"), tr("Side")};
	QStringList remaining;
	for (int i = 0; i < 3; ++i) {
		if (m_levelPlanPanes.shown[static_cast<std::size_t>(i)]) { remaining << names[i]; }
	}
	const QString message = shown ? tr("%1 view shown. Plan views: %2.").arg(names[index], remaining.join(tr(", ")))
		: tr("%1 view hidden. Plan views: %2.").arg(names[index], remaining.join(tr(", ")));
	statusBar()->showMessage(levelViewLayoutHasThreePlans(m_levelViewLayout) ? message
		: tr("%1 The choice applies to the three-plan layouts.").arg(message), 5000);
	return true;
}

void ApplicationShell::moveLevelActivePlanToShownPane()
{
	if (!levelViewLayoutHasThreePlans(m_levelViewLayout) || m_levelPlanViews.size() != 3) {
		return;
	}
	const auto hidden = [this](const QWidget* widget) {
		const int index = static_cast<int>(m_levelPlanViews.indexOf(widget));
		return index < 0 || !m_levelPlanPanes.shown[static_cast<std::size_t>(index)];
	};
	const int active = static_cast<int>(m_levelPlanViews.indexOf(m_levelMapViewport));
	MapViewport* next = hidden(m_levelMapViewport) ? m_levelPlanViews[levelPlanPaneNextShown(m_levelPlanPanes, std::max(0, active))] : m_levelMapViewport;
	// Menus and the palette act on the last focused pane; never a hidden one.
	if (m_levelLastFocusedView && m_levelLastFocusedView != m_levelMap3D && hidden(m_levelLastFocusedView)) {
		m_levelLastFocusedView = next;
	}
	if (next == m_levelMapViewport) {
		return;
	}
	if (m_levelMapViewport) {
		// The hidden pane's unfinished gesture and draw modes end with it.
		m_levelMapViewport->cancelInteraction();
		m_levelMapViewport->setDrawMode(false);
		m_levelMapViewport->setShapeDrawMode(false);
		if (m_levelMapViewport->makeSectorMode()) {
			setLevelMakeSectorMode(false);
		}
	}
	m_levelMapViewport = next;
	for (auto* pane : std::as_const(m_levelPlanViews)) {
		pane->setActivePane(pane == next);
	}
	if (m_levelMapProjection) {
		const QSignalBlocker blocker(m_levelMapProjection);
		m_levelMapProjection->setCurrentIndex(static_cast<int>(next->projection()));
	}
}

void ApplicationShell::applyLevelPlanPaneVisibility()
{
	if (!levelViewLayoutHasThreePlans(m_levelViewLayout) || m_levelPlanViews.size() != 3 || !m_levelMapUpperViews || !m_levelMapLowerViews) {
		return;
	}
	for (int i = 0; i < 3; ++i) {
		MapViewport* view = m_levelPlanViews[i];
		const bool shown = m_levelPlanPanes.shown[static_cast<std::size_t>(i)];
		if (!shown) {
			view->cancelInteraction();
		}
		view->setVisible(shown);
	}
	// A row or column with nothing left in it hides, so it leaves no gap.
	m_levelMapUpperViews->setVisible(splitterHoldsShownPane(m_levelMapUpperViews));
	m_levelMapLowerViews->setVisible(splitterHoldsShownPane(m_levelMapLowerViews));
}

void ApplicationShell::refreshLevelPlanPaneActions()
{
	if (!m_commands) {
		return;
	}
	const bool threePlans = levelViewLayoutHasThreePlans(m_levelViewLayout);
	const int shownCount = levelPlanPanesShownCount(m_levelPlanPanes);
	QMenu* menu = m_levelViewLayoutButton ? m_levelViewLayoutButton->menu() : nullptr;
	QAction* insertBefore = nullptr;
	if (menu) {
		// The pane choices follow the layout choices they refine.
		const auto actions = menu->actions();
		int lastLayout = -1;
		for (int i = 0; i < actions.size(); ++i) {
			const QString id = actions[i]->data().toString();
			if (id == QStringLiteral("profile") || levelViewLayoutForId(id, nullptr)) { lastLayout = i; }
		}
		if (lastLayout >= 0 && lastLayout + 1 < actions.size()) { insertBefore = actions[lastLayout + 1]; }
	}
	bool separated = false;
	for (int i = 0; i < 3; ++i) {
		QAction* action = m_commands->action(QString::fromLatin1(kPlanPaneCommands[static_cast<std::size_t>(i)]));
		if (!action) {
			continue;
		}
		const bool shown = m_levelPlanPanes.shown[static_cast<std::size_t>(i)];
		// Unblocked: the handler runs on triggered only, and toggled keeps the
		// Layout section's check boxes in step.
		action->setChecked(shown);
		// The last shown pane cannot be hidden, so its check box says so by
		// being unavailable rather than refusing later.
		m_commands->setEnabled(QString::fromLatin1(kPlanPaneCommands[static_cast<std::size_t>(i)]), threePlans && !m_settings.isReadOnly() && !(shown && shownCount <= 1));
		if (menu && !menu->actions().contains(action)) {
			if (!separated) { menu->insertSeparator(insertBefore); separated = true; }
			action->setIconVisibleInMenu(false);
			menu->insertAction(insertBefore, action);
		}
	}
}

} // namespace vibestudio
