#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_scene_locks.h"
#include "core/level_textured_selection.h"
#include "core/level_visibility.h"
#include <QScopedValueRollback>
#include <QStatusBar>
#include <algorithm>

// NRC Select_FacesAndPatchesByShader / Selection_Deselect, revision
// 449778b3840f8f2062daab15c87cb012f2ac945c, GPL-2.0-or-later: workflow
// reference only. Original Qt state and publication; see docs/CREDITS.md.
namespace vibestudio {
bool ApplicationShell::canSelectLevelMapTextured() const
{
	return canSelectLevelMapObjects(true, true) && !m_levelPaintMaterial.isEmpty()
		&& std::none_of(m_levelPlanViews.cbegin(), m_levelPlanViews.cend(), [](const auto* view) {
			return view && view->componentMode() != MapViewport::ComponentMode::Off && view->componentHandlesLimited();
		});
}

void ApplicationShell::reconcileLevelTexturedSelection(bool checkFilters)
{
	if (m_levelTexturedLoadSerial != m_levelMapLoadSerial) {
		m_levelTexturedFaces.clear(); m_levelTexturedGeometry.clear(); m_levelTexturedPatches.clear(); m_levelTexturedHistory.clear();
		m_levelTexturedLoadSerial = m_levelMapLoadSerial;
	}
	if (m_levelTexturedFaces.isEmpty() && m_levelTexturedPatches.isEmpty()) { return; }
	const auto context = levelSceneSelectionContext(m_levelMapDocument);
	const auto excluded = levelMapHiddenObjects(m_levelMapDocument) | levelSceneLockedObjects(m_levelMapDocument);
	const auto allowed = [&](LevelMapSelectionKind kind, int id, int owner) {
		const auto key = levelMapSelectionRefId({kind,id});
		return context.allowedObjects.contains(key) && !context.closedGroups.contains(key) && !excluded.contains(key)
			&& !excluded.contains(levelMapSelectionRefId({LevelMapSelectionKind::Entity,owner}))
			&& (!checkFilters || !m_levelMapViewport || !m_levelMapViewport->isHidden(kind,id));
	};
	QSet<int> validBrushes;
	for (const auto& brush : m_levelMapDocument.brushes) {
		if (!m_levelTexturedGeometry.contains(brush.id) || !allowed(LevelMapSelectionKind::QuakeBrush,brush.id,brush.entityId)) { continue; }
		if (m_levelTexturedRevision == m_levelMapDocument.revision
			|| m_levelTexturedGeometry.value(brush.id) == levelSurfaceSelectionGeometryKey(brush)) { validBrushes.insert(brush.id); }
	}
	if (!m_levelTexturedHistory.isEmpty()
		&& std::any_of(m_levelTexturedFaces.cbegin(),m_levelTexturedFaces.cend(),[&](const auto& face) { return !validBrushes.contains(face.brushId); })) {
		// Undo or redo of a picked-face transform: restore the newest recorded
		// picks whose every brush has exactly that geometry again.
		QHash<int,const LevelMapBrush*> brushes;
		for (const auto& brush : m_levelMapDocument.brushes) { brushes.insert(brush.id,&brush); }
		for (auto state = m_levelTexturedHistory.crbegin(); state != m_levelTexturedHistory.crend(); ++state) {
			const auto& [faces,geometry] = *state;
			const bool matches = !faces.isEmpty() && std::all_of(faces.cbegin(),faces.cend(),[&](const auto& face) { return geometry.contains(face.brushId); })
				&& std::all_of(geometry.keyValueBegin(),geometry.keyValueEnd(),[&](const auto& entry) {
					const auto* brush = brushes.value(entry.first);
					return brush && allowed(LevelMapSelectionKind::QuakeBrush,brush->id,brush->entityId) && levelSurfaceSelectionGeometryKey(*brush) == entry.second;
				});
			if (!matches) { continue; }
			m_levelTexturedFaces.removeIf([&](const auto& face) { return !validBrushes.contains(face.brushId); });
			for (const auto& face : faces) { if (!m_levelTexturedFaces.contains(face)) { m_levelTexturedFaces.append(face); } }
			for (auto entry = geometry.cbegin(); entry != geometry.cend(); ++entry) { m_levelTexturedGeometry.insert(entry.key(),entry.value()); validBrushes.insert(entry.key()); }
			break;
		}
	}
	m_levelTexturedFaces.removeIf([&](const auto& face) { return !validBrushes.contains(face.brushId); });
	for (auto it = m_levelTexturedGeometry.begin(); it != m_levelTexturedGeometry.end();) {
		if (!validBrushes.contains(it.key())) { it = m_levelTexturedGeometry.erase(it); } else { ++it; }
	}
	QSet<int> patches;
	for (const auto& patch : m_levelMapDocument.patches) {
		if (m_levelTexturedPatches.contains(patch.id) && m_levelMapDocument.selection.contains({LevelMapSelectionKind::QuakePatch,patch.id})
			&& allowed(LevelMapSelectionKind::QuakePatch,patch.id,patch.entityId)) { patches.insert(patch.id); }
	}
	m_levelTexturedPatches = std::move(patches);
	m_levelTexturedRevision = m_levelMapDocument.revision;
}

void ApplicationShell::refreshLevelTexturedSelectionViews()
{
	reconcileLevelTexturedSelection();
	for (auto* view : std::as_const(m_levelPlanViews)) { view->setTexturedSurfaceFaces(m_levelTexturedFaces); }
	if (m_levelMap3D) { m_levelMap3D->setTexturedSurfaceCount(static_cast<int>(m_levelTexturedFaces.size())); }
	// In Face mode the picks share the component manipulator and its pivot.
	refreshLevelComponentGizmos();
}

void ApplicationShell::selectLevelMapTexturedFromUi(const QString& material, bool withinSelection)
{
	// Find and Replace Textures names its own material; Select Textured
	// follows the current paint material.
	const bool followPaint = material.isEmpty();
	const auto chosen = [&] { return followPaint ? m_levelPaintMaterial : material; };
	const auto eligible = [&] {
		return canSelectLevelMapObjects(true, true) && !chosen().isEmpty()
			&& std::none_of(m_levelPlanViews.cbegin(), m_levelPlanViews.cend(), [](const auto* view) {
				   return view && view->componentMode() != MapViewport::ComponentMode::Off && view->componentHandlesLimited();
			   });
	};
	if (!eligible()) { statusBar()->showMessage(tr("Finish the active tool and choose a material before selecting textured surfaces.")); return; }
	reconcileLevelTexturedSelection();
	const auto exclusions = [this] {
		QSet<QString> result;
		for (const auto& key : levelSceneObjects(m_levelMapDocument)) {
			const auto colon = key.indexOf(':');
			if (m_levelMapViewport && m_levelMapViewport->isHidden(levelMapSelectionKindFromId(key.left(colon)),key.mid(colon+1).toInt())) { result.insert(key); }
		}
		return result;
	};
	LevelPlacementRequest request; request.operation = LevelPlacementOperation::SelectTextured;
	request.texturedSelection.material = chosen();
	request.texturedSelection.withinSelection = withinSelection;
	request.texturedSelection.faces = m_levelTexturedFaces;
	request.texturedSelection.excludedObjects = exclusions();
	request.texturedSelection.undoSelection = m_levelControls.undoObjectSelection;
	const auto publish = levelPlacementCommitter();
	const auto cameraPicks = m_levelPickedSelection == m_levelMapDocument.selection ? m_levelPickedFaces : QVector<LevelSurfaceFace>{};
	const auto picks = m_levelMapViewport->pickedComponents();
	const auto mode = m_levelMapViewport->componentMode();
	LevelPlacementResult result;
	{
		QScopedValueRollback<bool> busy(m_levelObjectSelectionBusy,true); refreshLevelObjectSelectionCommands();
		result = LevelPlacementTaskDialog::prepare(this,m_levelMapDocument,request);
	}
	refreshLevelObjectSelectionCommands();
	if (!result.succeeded) { statusBar()->showMessage(result.error,6000); return; }
	if (!eligible() || request.texturedSelection.material != chosen()
		|| request.texturedSelection.faces != m_levelTexturedFaces || request.texturedSelection.excludedObjects != exclusions()
		|| request.texturedSelection.undoSelection != m_levelControls.undoObjectSelection) {
		statusBar()->showMessage(tr("The view, material, profile or filters changed. Select textured surfaces again from the current state."),6000); return;
	}
	for (const auto* view : std::as_const(m_levelPlanViews)) {
		if (!view->canAddComponentPatches(result.texturedSelectionReport.matchedPatches)) {
			statusBar()->showMessage(tr("The matching patches exceed the component handle limit. Leave component mode or select fewer objects and try again."),6000); return;
		}
	}
	QString error;
	if (!publish(result.document,&error)) { statusBar()->showMessage(error,6000); return; }
	m_levelTexturedFaces = result.texturedSelectionReport.faces;
	m_levelTexturedGeometry = result.texturedSelectionReport.brushGeometry;
	for (int id : result.texturedSelectionReport.matchedPatches) { m_levelTexturedPatches.insert(id); }
	m_levelTexturedLoadSerial = m_levelMapLoadSerial; m_levelTexturedRevision = m_levelMapDocument.revision;
	// Adding patches must not invalidate the separate camera-picked faces.
	m_levelPickedFaces = cameraPicks;
	m_levelPickedSelection = m_levelMapDocument.selection;
	refreshLevelMapWorkbench();
	if (mode != MapViewport::ComponentMode::Off) {
		// The preflight proved capacity. Geometry and mode were validated before
		// publication; restore exact IDs rather than approximate handle centres.
		m_levelMapViewport->setPickedComponents(picks); shareLevelComponentPicks(m_levelMapViewport);
	}
	refreshLevelTexturedSelectionViews(); refreshLevelMap3DHighlight(); refreshLevelSurfaceTools(); refreshCommandEnablement();
	statusBar()->showMessage(tr("Added %n textured face(s).",nullptr,result.texturedSelectionReport.addedFaces) + QLatin1Char(' ')
		+ tr("Added %n whole patch(es).",nullptr,result.texturedSelectionReport.addedPatches));
}
} // namespace vibestudio
