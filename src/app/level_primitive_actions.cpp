#include "app/application_shell.h"
#include "app/level_primitive_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "core/level_placement.h"
#include <QStatusBar>
#include <algorithm>

namespace vibestudio
{
bool ApplicationShell::applyLevelBrushPrimitive(const LevelBrushPrimitiveRequest &request, QString *error)
{
	if (error) { error->clear(); }
	LevelPlacementRequest placement;
	placement.operation = LevelPlacementOperation::AddBrush;
	placement.primitive = request;
	if (!runLevelPlacementFromUi(placement, error)) {
		return false;
	}
	levelBrushPrimitiveAdded(request);
	return true;
}

void ApplicationShell::levelBrushPrimitiveAdded(const LevelBrushPrimitiveRequest &request)
{
	const int id = m_levelMapDocument.selectedObjectId;
	recordActivity(tr("Level map brush added"), QStringLiteral("brush:%1").arg(id), QStringLiteral("level-map"), OperationState::Warning,
				   tr("Unsaved map edit"));
	rememberLevelMaterial(request.texture.trimmed());
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Added brush:%1. Save the map to include it in the next build.").arg(id));
}

void ApplicationShell::addLevelMapBrushFromUi(const QPointF &viewPoint)
{
	if ((m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) ||
		!m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a Quake-family .map to add brushes."));
		return;
	}
	LevelBrushPrimitiveRequest request;
	const auto projection = m_levelMapViewport->projection();
	request.axis = projection == MapViewportProjection::FrontXZ ? 1 : projection == MapViewportProjection::SideZY ? 0 : 2;
	const auto component = [&](const LevelMapVec3 &p) { return request.axis == 0 ? p.x : request.axis == 1 ? p.y : p.z; };
	const auto statistics = levelMapStatistics(m_levelMapDocument);
	const double hidden =
		statistics.mins.valid && statistics.maxs.valid ? (component(statistics.mins) + component(statistics.maxs)) / 2 : 0;
	const auto point = viewPoint.x() < 0 ? QPointF(m_levelMapViewport->rect().center()) : viewPoint;
	const auto center = m_levelMapViewport->worldPositionAt(point, hidden);
	const int grid = std::max(1, m_levelMapViewport->gridSize());
	const double size = std::max(64, grid * 2);
	request.mins = {snapLevelMapCoordinate(center.x - size / 2, grid), snapLevelMapCoordinate(center.y - size / 2, grid),
					snapLevelMapCoordinate(center.z - size / 2, grid), true};
	request.maxs = {request.mins.x + size, request.mins.y + size, request.mins.z + size, true};
	if (m_levelMap3D && m_levelMap3D->brushDrawTool()) {
		request.axis = m_levelMap3D->brushDrawAxis();
		const double plane = m_levelMap3D->brushDrawBase(), end = plane + m_levelMap3D->brushDrawDirection()*m_levelMap3D->brushDrawDepth();
		const double base = std::min(plane,end), top = std::max(plane,end);
		if (request.axis == 0) { request.mins.x = base; request.maxs.x = top; }
		else if (request.axis == 1) { request.mins.y = base; request.maxs.y = top; }
		else { request.mins.z = base; request.maxs.z = top; }
	}
	request.texture = levelBrushMaterial();
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	LevelPrimitiveDialog dialog(m_levelMapDocument, request, archive, activePaletteId(), this);
	const auto publish = levelPlacementCommitter();
	dialog.setApplyHandler([this, &dialog, publish](const LevelMapDocument &candidate, QString *error) {
		if (!publish(candidate, error)) {
			return false;
		}
		levelBrushPrimitiveAdded(dialog.request());
		return true;
	});
	dialog.exec();
}
} // namespace vibestudio
