#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_scene.h"
#include <QScopedValueRollback>
#include <QStatusBar>
#include <algorithm>

namespace vibestudio {
bool ApplicationShell::canChangeLevelVisibility(LevelVisibilityOperation operation) const
{
	if (m_levelVisibilityBusy || m_levelObjectSelectionBusy || m_levelRegionSelectionBusy || !m_levelMapViewport
		|| m_levelMapDocument.format == LevelMapFormat::Unknown || levelComponentsActive() || !levelPickedSurfaceFaces().isEmpty()
		|| m_levelQuickClip || levelSurfaceEditsPending()) { return false; }
	if (operation == LevelVisibilityOperation::ShowAll ? (m_levelMapDocument.hiddenObjects.isEmpty() && m_levelMapDocument.shownObjects.isEmpty()) : m_levelMapDocument.selection.isEmpty()) { return false; }
	if (std::any_of(m_levelPlanViews.cbegin(),m_levelPlanViews.cend(),[](const auto* view) {
		return view && (view->clipMode() || view->isDrawingBrush() || view->isDragging());
	})) { return false; }
	return !m_levelMap3D || (!m_levelMap3D->surfaceStrokeActive() && !m_levelMap3D->materialStrokeActive() && !m_levelMap3D->isDrawingBrush()
		&& !m_levelMap3D->isMovingSelection() && !m_levelMap3D->isResizingSelection());
}
void ApplicationShell::refreshLevelVisibilityCommands()
{
	m_commands->setEnabled(QStringLiteral("map.hideSelection"),canChangeLevelVisibility(LevelVisibilityOperation::HideSelected));
	m_commands->setEnabled(QStringLiteral("map.showAll"),canChangeLevelVisibility(LevelVisibilityOperation::ShowAll));
	m_commands->setEnabled(QStringLiteral("map.isolateSelection"),canChangeLevelVisibility(LevelVisibilityOperation::Isolate));
}
void ApplicationShell::changeLevelVisibility(LevelVisibilityOperation operation)
{
	if (!canChangeLevelVisibility(operation)) { statusBar()->showMessage(tr("Finish the active tool and select objects before changing visibility."),6000); return; }
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::Visibility;
	request.visibility = {operation,m_levelControls.undoObjectSelection,m_levelControls.inheritGroupVisibility};
	const auto publish = levelPlacementCommitter();
	LevelPlacementResult result;
	{
		QScopedValueRollback<bool> busy(m_levelVisibilityBusy,true); refreshLevelVisibilityCommands();
		result = LevelPlacementTaskDialog::prepare(this,m_levelMapDocument,request);
	}
	refreshLevelVisibilityCommands();
	if (!result.succeeded) { statusBar()->showMessage(result.error,6000); return; }
	if (!canChangeLevelVisibility(operation) || request.visibility.undoable != m_levelControls.undoObjectSelection
		|| request.visibility.inheritGroups != m_levelControls.inheritGroupVisibility) {
		statusBar()->showMessage(tr("The active tool or editor profile changed. Change visibility again from the current state."),6000); return;
	}
	QString error;
	if (!publish(result.document,&error)) { statusBar()->showMessage(error,6000); return; }
	refreshLevelMapWorkbench();
	const int hidden = static_cast<int>((levelMapHiddenObjects(m_levelMapDocument) - levelSceneHiddenObjects(m_levelMapDocument)).size());
	statusBar()->showMessage(operation == LevelVisibilityOperation::ShowAll
		? tr("Temporary hiding cleared. Scene visibility and filters still apply.")
		: tr("%n object(s) temporarily hidden. Show All Hidden restores temporary visibility.",nullptr,hidden));
}
} // namespace vibestudio
