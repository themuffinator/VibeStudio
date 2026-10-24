#include "app/application_shell.h"
#include "app/level_wrap_dialog.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QStatusBar>

namespace vibestudio {
void ApplicationShell::wrapLevelMapFromUi(bool withPoints)
{
	if (!m_levelMapViewport) { return; }
	LevelWrapRequest request; request.componentsOnly = levelComponentsActive();
	request.components = request.componentsOnly ? m_levelMapViewport->pickedComponents() : QVector<LevelMapComponentSet>{};
	request.texture = levelBrushMaterial();
	if (request.componentsOnly && request.components.isEmpty()) { statusBar()->showMessage(tr("Pick components before Wrap/Merge.")); return; }
	for (auto* view : std::as_const(m_levelPlanViews)) { view->finishComponentMove(false); }
	if (m_levelMap3D) { m_levelMap3D->finishComponentMove(false); }
	const bool clipping = m_levelMapViewport->clipMode(); const auto plane = m_levelMapViewport->clipState();
	if (clipping) { request.points = plane.points; if (m_levelClipPreferencesChanged) { m_levelClipPreferencesChanged(); } }
	const auto publish = levelPlacementCommitter(); const auto material = request.texture;
	const auto apply = [this,publish,clipping,plane,material,withPoints,picks=request.components](const LevelMapDocument& candidate,QString* error) {
		if ((clipping && (!m_levelMapViewport->clipMode() || !(plane == m_levelMapViewport->clipState())))
			|| (!withPoints && material != levelBrushMaterial())) {
			if (error) { *error = tr("The clipper points or current material changed. Prepare Wrap/Merge again."); } return false;
		}
		if (!publish(candidate,error)) { return false; }
		refreshLevelMapWorkbench();
		if (!picks.isEmpty()) { m_levelMapViewport->setPickedComponents(picks); shareLevelComponentPicks(m_levelMapViewport); }
		recordActivity(tr("Level geometry wrapped/merged"),m_levelMapDocument.sourcePath,QStringLiteral("level-map"),OperationState::Warning,tr("Unsaved map edit"));
		statusBar()->showMessage(picks.isEmpty() ? tr("Selected brushes wrapped into one hull. Undo restores the originals.")
			: tr("Picked components wrapped into a new brush. Original objects and picks remain."),6000);
		return true;
	};
	if (withPoints) {
		auto archive = m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
		if (!archive && m_packageArchive.isOpen()) { archive = std::make_shared<const PackageArchive>(m_packageArchive); }
		LevelWrapDialog dialog(m_levelMapDocument,request,archive,activePaletteId(),this); dialog.setApplyHandler(apply); dialog.exec();
	} else {
		LevelPlacementRequest work; work.operation = LevelPlacementOperation::WrapMerge; work.wrap = request;
		const auto result = LevelPlacementTaskDialog::prepare(this,m_levelMapDocument,work); QString error = result.error;
		if (!result.succeeded || !apply(result.document,&error)) { statusBar()->showMessage(error,6000); }
	}
}
} // namespace vibestudio
