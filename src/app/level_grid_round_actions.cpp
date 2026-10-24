// Round Selection to Grid, Radiant's Snap To Grid: every brush vertex, patch
// control point and point-entity origin of the selection rounds onto the grid
// on its own, or only the picked components in a component mode. Workflow
// reference: NetRadiant Custom Selection_SnapToGrid in radiant/select.cpp at
// 449778b3 (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.
// Also the component UV lock settings every component edit shares.

#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "core/level_grid_round.h"
#include "core/level_placement.h"

#include <QStatusBar>

#include <algorithm>

namespace vibestudio {

void ApplicationShell::applyLevelComponentUvSettings(LevelComponentTransformRequest* request) const
{
	if (!request) {
		return;
	}
	request->textureLock = m_settings.levelComponentUvLock();
	request->fitUvLock = m_settings.levelComponentUvFit();
	request->allowValve220 = m_settings.levelAllowValve220();
}

QString ApplicationShell::levelComponentUvFitNote(int fittedFaces) const
{
	if (fittedFaces <= 0) {
		return {};
	}
	return QLatin1Char(' ')
		+ tr("UVs on %n face(s) could not stay exact, so they were fitted to three corners and the other corners drifted.", nullptr, fittedFaces);
}

void ApplicationShell::roundLevelMapSelectionToGridFromUi()
{
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (!textMap) {
		// Doom-format maps have no brush vertices or patches; their objects snap
		// by their own reference points, as Snap Selection to Grid does.
		snapLevelMapSelectionFromUi();
		return;
	}
	const double grid = m_levelMapViewport ? m_levelMapViewport->gridSize() : 8.0;
	QString error;
	if (levelComponentsActive()) {
		const QVector<LevelMapComponentSet> sets = levelComponentTransformSets(&error);
		if (!error.isEmpty() || sets.isEmpty()) {
			statusBar()->showMessage(error.isEmpty() ? tr("Pick vertices, edges, faces or patch control points to round them to the grid.") : error, 6000);
			return;
		}
		for (auto* view : std::as_const(m_levelPlanViews)) {
			view->finishComponentMove(false);
		}
		if (m_levelMap3D) {
			m_levelMap3D->finishComponentMove(false);
		}
		LevelComponentTransformRequest request;
		request.grid = grid;
		request.allowCollapse = true;
		applyLevelComponentUvSettings(&request);
		LevelMapComponentReport report;
		if (!applyLevelComponentTransform(request, &error, &report)) {
			statusBar()->showMessage(tr("Could not round the picked components: %1").arg(error), 6000);
			return;
		}
		const int objects = report.brushes + report.patches;
		if (objects == 0) {
			statusBar()->showMessage(tr("The picked components are already on grid %1.").arg(grid), 5000);
			return;
		}
		QString merged;
		if (report.collapsedVertices > 0) {
			merged = QLatin1Char(' ') + tr("%n vertex(es) merged.", nullptr, report.collapsedVertices);
		}
		statusBar()->showMessage(tr("Rounded the picked components of %n object(s) to grid %1. Undo restores them.", nullptr, objects).arg(grid)
				+ merged + levelComponentUvFitNote(report.fittedFaces),
			6000);
		return;
	}
	if (m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(tr("Select brushes, patches or entities to round them to the grid."), 5000);
		return;
	}
	LevelPlacementRequest work;
	work.operation = LevelPlacementOperation::RoundToGrid;
	work.grid = grid;
	work.gridRound.grid = grid;
	work.gridRound.textureLock = m_settings.levelComponentUvLock();
	work.gridRound.fitUvLock = m_settings.levelComponentUvFit();
	work.gridRound.allowValve220 = m_settings.levelAllowValve220();
	const auto publish = levelPlacementCommitter();
	const LevelPlacementResult result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, work);
	if (result.cancelled) {
		statusBar()->showMessage(tr("Rounding to the grid was cancelled; the map is unchanged."), 5000);
		return;
	}
	if (!result.succeeded) {
		statusBar()->showMessage(tr("Could not round the selection: %1").arg(result.error), 6000);
		return;
	}
	const LevelMapGridRoundReport& report = result.gridRoundReport;
	QString skippedNote;
	if (!report.skipped.isEmpty()) {
		skippedNote = QLatin1Char(' ')
			+ tr("%n object(s) left as they were; the first because: %1", nullptr, static_cast<int>(report.skipped.size())).arg(report.skipped.first());
	}
	if (!report.changed()) {
		statusBar()->showMessage(report.skipped.isEmpty() ? tr("The selection is already on grid %1.").arg(grid)
														  : tr("Nothing changed.") + skippedNote,
			5000);
		return;
	}
	if (!publish(result.document, &error)) {
		statusBar()->showMessage(tr("Could not round the selection: %1").arg(error), 6000);
		return;
	}
	const int objects = report.brushes + report.patches + report.entities;
	recordActivity(tr("Level map selection rounded to the grid"), tr("%n object(s)", nullptr, objects), QStringLiteral("level-map"),
		OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Rounded %n object(s) to grid %1, point by point. Undo restores them.", nullptr, objects).arg(grid)
			+ levelComponentUvFitNote(report.fittedFaces) + skippedNote,
		6000);
}

} // namespace vibestudio
