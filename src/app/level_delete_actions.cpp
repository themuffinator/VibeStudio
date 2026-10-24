#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_scene_locks.h"
#include "core/level_visibility.h"
#include <QScopedValueRollback>
#include <QStatusBar>
#include <algorithm>

// Workflow references: NRC deleteSelection / CSG_DeleteComponents at
// 449778b3 (GPL-2.0-or-later), TB MapWindow::deleteSelection at 4c756be1
// (GPL-3.0-or-later). Original Qt implementation; see docs/CREDITS.md.
namespace vibestudio {
bool ApplicationShell::canDeleteLevelMapSelection(bool componentsOnly) const
{
	if (m_levelDeletionBusy || m_levelObjectSelectionBusy || levelSurfaceEditsPending() || !m_levelMapViewport
		|| m_levelMapDocument.format == LevelMapFormat::Unknown) { return false; }
	if (!componentsOnly && m_levelControls.deleteRemovesClipPoint && m_levelMapViewport->clipMode()) {
		return !m_levelMapViewport->clipState().points.isEmpty();
	}
	if (levelSurfaceGeometryBusy() || levelSweepToolActive()) { return false; }
	const auto excluded = levelSceneLockedObjects(m_levelMapDocument) | levelMapHiddenObjects(m_levelMapDocument);
	const auto editable = [&](const LevelMapSelectionRef& ref) {
		return !excluded.contains(levelMapSelectionRefId(ref)) && !m_levelMapViewport->isHidden(ref.kind, ref.objectId);
	};
	if (levelComponentsActive()) {
		if (m_levelMapViewport->componentHandlesLimited()) { return false; }
		const auto picked = m_levelMapViewport->pickedComponents();
		if (!picked.isEmpty()) {
			const auto context = levelSceneSelectionContext(m_levelMapDocument);
			bool brushes = false;
			for (const auto& set : picked) {
				if (set.object.kind == LevelMapSelectionKind::QuakePatch && m_levelControls.deleteIgnoresPatchPoints) { continue; }
				const auto key = levelMapSelectionRefId(set.object);
				if (set.object.kind != LevelMapSelectionKind::QuakeBrush || set.components.isEmpty() || !editable(set.object)
					|| !context.allowedObjects.contains(key) || context.closedGroups.contains(key)) { return false; }
				brushes = true;
			}
			return brushes;
		}
		if (componentsOnly || !m_levelControls.deleteObjectsWithoutComponents) { return false; }
	} else if (componentsOnly) { return false; }
	return levelMapSelectionIsDeletable(m_levelMapDocument)
		&& std::all_of(m_levelMapDocument.selection.cbegin(), m_levelMapDocument.selection.cend(), editable);
}

void ApplicationShell::refreshLevelDeletionCommands()
{
	if (!m_commands) { return; }
	m_commands->setEnabled(QStringLiteral("map.deleteSelection"), canDeleteLevelMapSelection());
	m_commands->setEnabled(QStringLiteral("map.removeComponents"), canDeleteLevelMapSelection(true));
}

void ApplicationShell::deleteLevelMapSelectionFromUi() { runLevelDeletionFromUi(false); }
void ApplicationShell::removeLevelComponentsFromUi() { runLevelDeletionFromUi(true); }

void ApplicationShell::runLevelDeletionFromUi(bool componentsOnly)
{
	if (!canDeleteLevelMapSelection(componentsOnly)) {
		statusBar()->showMessage(tr("Finish the active tool and select editable objects or brush components before deleting."), 6000); return;
	}
	if (!componentsOnly && m_levelControls.deleteRemovesClipPoint && m_levelMapViewport->clipMode()) {
		// End any in-progress handle gesture before removing a shared point.
		if (m_levelMap3D) { m_levelMap3D->finishClipPoint(false); }
		for (auto* view : std::as_const(m_levelPlanViews)) { view->finishClipPoint(false); }
		m_levelMapViewport->removeLastClipPoint(); refreshLevelDeletionCommands(); return;
	}
	const auto picked = levelComponentsActive() ? m_levelMapViewport->pickedComponents() : QVector<LevelMapComponentSet>{};
	const auto selection = m_levelMapDocument.selection;
	const bool componentEdit = !picked.isEmpty();
	LevelPlacementRequest request;
	request.operation = componentEdit ? LevelPlacementOperation::RemoveComponents : LevelPlacementOperation::DeleteObjects;
	QVector<LevelMapComponentSet> retained;
	for (const auto& set : picked) {
		if (set.object.kind == LevelMapSelectionKind::QuakePatch && m_levelControls.deleteIgnoresPatchPoints) { retained << set; }
		else { request.components << set; }
	}
	const bool fallback = m_levelControls.deleteObjectsWithoutComponents;
	const bool patches = m_levelControls.deleteIgnoresPatchPoints;
	const bool clip = m_levelControls.deleteRemovesClipPoint;
	const auto profile = m_levelControlsProfileId;
	const auto publish = levelPlacementCommitter();
	LevelPlacementResult result;
	{
		QScopedValueRollback<bool> busy(m_levelDeletionBusy, true); refreshLevelDeletionCommands();
		result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, request);
	}
	refreshLevelDeletionCommands();
	if (!result.succeeded) { statusBar()->showMessage(result.error, 6000); return; }
	if (fallback != m_levelControls.deleteObjectsWithoutComponents || patches != m_levelControls.deleteIgnoresPatchPoints
		|| clip != m_levelControls.deleteRemovesClipPoint || profile != m_levelControlsProfileId
		|| levelSurfaceGeometryBusy() || levelSweepToolActive() || !canDeleteLevelMapSelection(componentsOnly)) {
		statusBar()->showMessage(tr("The tool, profile or editable selection changed. Delete again from the current state."), 6000); return;
	}
	QString error;
	if (!publish(result.document, &error)) { statusBar()->showMessage(error, 6000); return; }
	const QString what = selection.size() == 1 ? levelMapSelectionRefId(selection.first()) : tr("%n objects", nullptr, static_cast<int>(selection.size()));
	recordActivity(componentEdit ? tr("Level map components removed") : tr("Level map objects deleted"),
		componentEdit ? tr("%n brush(es)", nullptr, result.componentReport.brushes) : what,
		QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	if (componentEdit) {
		// The surviving patches retain their exact point IDs. Removed brush
		// handles must not accidentally select new vertices with recycled IDs.
		m_levelMapViewport->setPickedComponents(retained); shareLevelComponentPicks(m_levelMapViewport);
		statusBar()->showMessage(tr("Removed %n vertex(es); Undo restores them.", nullptr, result.componentReport.removedVertices), 5000);
	} else {
		statusBar()->showMessage(tr("Deleted %1. Undo restores it. Use Save Map to update the file after a change check and backup.").arg(what));
	}
}
} // namespace vibestudio
