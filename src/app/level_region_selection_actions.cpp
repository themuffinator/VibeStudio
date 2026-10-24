#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_scene.h"
#include <QAction>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <algorithm>

namespace vibestudio {
bool ApplicationShell::canSelectLevelMapRegion(LevelMapRegionSelection mode) const
{
	if (m_levelRegionSelectionBusy || m_levelMapDocument.format == LevelMapFormat::Unknown || m_levelMapDocument.selection.isEmpty()
		|| levelComponentsActive() || m_levelQuickClip || levelSurfaceEditsPending()) { return false; }
	if (std::any_of(m_levelPlanViews.cbegin(),m_levelPlanViews.cend(),[](const auto* view) {
		return view && (view->clipMode() || view->isDrawingBrush() || view->isDragging());
	})) { return false; }
	if (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->materialStrokeActive() || m_levelMap3D->isDrawingBrush()
		|| m_levelMap3D->isMovingSelection() || m_levelMap3D->isResizingSelection())) { return false; }
	const auto policy = m_levelControls.regionSelection;
	if (policy == LevelRegionSelectionPolicy::Bounds) { return true; }
	if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) { return false; }
	const auto brush = [](const auto& ref) { return ref.kind == LevelMapSelectionKind::QuakeBrush; };
	if (!std::any_of(m_levelMapDocument.selection.cbegin(),m_levelMapDocument.selection.cend(),brush)) { return false; }
	if (policy == LevelRegionSelectionPolicy::TrenchBroom) {
		if (!std::all_of(m_levelMapDocument.selection.cbegin(),m_levelMapDocument.selection.cend(),brush) || !levelPickedSurfaceFaces().isEmpty()) { return false; }
		if ((mode == LevelMapRegionSelection::CompleteTall || mode == LevelMapRegionSelection::PartialTall)
			&& (!levelMap2DShowing() || m_levelLastFocusedView == m_levelMap3D)) { return false; }
	}
	return true;
}

void ApplicationShell::refreshLevelRegionSelectionCommands()
{
	const auto policy = m_levelControls.regionSelection;
	const bool tb = policy == LevelRegionSelectionPolicy::TrenchBroom, nrc = policy == LevelRegionSelectionPolicy::Radiant;
	const std::array<std::pair<const char*,LevelMapRegionSelection>,4> actions{{
		{"map.selectInside",LevelMapRegionSelection::Inside}, {"map.selectTouching",LevelMapRegionSelection::Touching},
		{"map.selectCompleteTall",LevelMapRegionSelection::CompleteTall}, {"map.selectPartialTall",LevelMapRegionSelection::PartialTall}}};
	for (const auto& [id,mode] : actions) {
		m_commands->setEnabled(QLatin1String(id),canSelectLevelMapRegion(mode));
		auto* action = m_commands->action(QLatin1String(id)); if (!action) { continue; }
		const bool whole = mode == LevelMapRegionSelection::Inside || mode == LevelMapRegionSelection::CompleteTall;
		const bool tall = mode == LevelMapRegionSelection::CompleteTall || mode == LevelMapRegionSelection::PartialTall;
		QString description;
		if (tb) {
			description = tall
				? (whole ? tr("Select objects wholly within each brush's projected footprint in the active 2D view. Removes the selection brushes in one undo step.")
					: tr("Select objects overlapping each brush's projected footprint in the active 2D view. Removes the selection brushes in one undo step."))
				: (whole ? tr("Select objects wholly inside any selected convex brush. Removes the selection brushes in one undo step.")
					: tr("Select objects touching any selected convex brush. Removes the selection brushes in one undo step."));
		} else if (nrc) {
			description = whole ? tr("Select objects inside any selected brush's bounds. Removes all selected objects in one undo step; Tall ignores the active 2D view's depth.")
				: tr("Add objects touching any selected brush's bounds to the selection. Tall ignores the active 2D view's depth.");
		} else {
			description = whole ? tr("Select objects inside the combined selection bounds. Tall ignores the active 2D view's depth; geometry is retained.")
				: tr("Select objects touching the combined selection bounds. Tall ignores the active 2D view's depth; geometry is retained.");
		}
		action->setToolTip(description); action->setStatusTip(description);
		if (mode == LevelMapRegionSelection::CompleteTall) { action->setText(tb ? tr("Select Tall") : tr("Select Complete Tall")); }
	}
}

void ApplicationShell::selectLevelMapRegionFromUi(LevelMapRegionSelection mode)
{
	if (!canSelectLevelMapRegion(mode)) { statusBar()->showMessage(tr("Finish the active tool and select objects suitable for this profile's region command.")); return; }
	const auto exclusions = [this] {
		QSet<QString> result;
		for (const auto& key : levelSceneObjects(m_levelMapDocument)) {
			const auto colon = key.indexOf(':');
			if (m_levelMapViewport && m_levelMapViewport->isHidden(levelMapSelectionKindFromId(key.left(colon)),key.mid(colon+1).toInt())) { result.insert(key); }
		}
		return result;
	};
	const auto definitionBounds = [this] {
		QHash<QString,ResizeBox> result;
		for (const auto& definition : levelEntityCatalogue().classes) {
			if (definition.kind == EntityClassKind::Point && definition.hasSize) {
				result.insert(definition.className.toCaseFolded(),{{definition.mins[0],definition.mins[1],definition.mins[2]},
					{definition.maxs[0],definition.maxs[1],definition.maxs[2]}});
			}
		}
		return result;
	};
	const auto projection = [this] { return m_levelMapViewport ? static_cast<int>(m_levelMapViewport->projection()) : 0; };
	const int view = projection();
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectRegion;
	request.region.policy = m_levelControls.regionSelection; request.region.mode = mode;
	request.region.axis = view == 0 ? 2 : (view == 1 ? 1 : 0);
	request.region.excludedObjects = exclusions(); request.region.entityBounds = definitionBounds();
	const auto publish = levelPlacementCommitter();
	LevelPlacementResult result;
	{
		QScopedValueRollback<bool> busy(m_levelRegionSelectionBusy,true);
		refreshLevelRegionSelectionCommands();
		result = LevelPlacementTaskDialog::prepare(this,m_levelMapDocument,request);
	}
	refreshLevelRegionSelectionCommands();
	QString error;
	if (!result.succeeded) { statusBar()->showMessage(result.error,6000); return; }
	if (!canSelectLevelMapRegion(mode) || m_levelControls.regionSelection != request.region.policy || projection() != view
		|| exclusions() != request.region.excludedObjects || definitionBounds() != request.region.entityBounds) {
		statusBar()->showMessage(tr("The view, profile, filters or entity definitions changed. Select by region again from the current state."),6000); return;
	}
	if (!publish(result.document,&error)) { statusBar()->showMessage(error,6000); return; }
	if (result.regionReport.removedObjects > 0) {
		recordActivity(tr("Selected by brush volume"),tr("Removed %n selected object(s)",nullptr,result.regionReport.removedObjects),
			QStringLiteral("level-map"),OperationState::Warning,tr("Unsaved map edit"));
	}
	refreshLevelMapWorkbench();
	statusBar()->showMessage(result.regionReport.removedObjects > 0
		? tr("Selected %n object(s). Selection volumes removed; Undo restores them. Save before building.",nullptr,static_cast<int>(m_levelMapDocument.selection.size()))
		: tr("Selected %n object(s).",nullptr,static_cast<int>(m_levelMapDocument.selection.size())));
}
} // namespace vibestudio
