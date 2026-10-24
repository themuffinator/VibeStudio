#include "app/application_shell.h"
#include "app/level_component_transform_dialog.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "core/level_placement.h"
#include "core/level_textured_selection.h"
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::levelComponentsActive() const
{
	return m_levelMapViewport && m_levelMapViewport->componentMode() != MapViewport::ComponentMode::Off;
}
LevelSurfaceFacePicks ApplicationShell::levelComponentFacePicks() const
{
	// Workflow reference: NRC Scene_Translate_Component_Selected at 449778b3
	// (GPL-2.0-or-later) transforms faces picked by Select Textured in face
	// mode. Original Qt implementation; see docs/CREDITS.md.
	LevelSurfaceFacePicks picks;
	if (!levelComponentsActive() || m_levelMapViewport->componentMode() != MapViewport::ComponentMode::Faces
		|| m_levelTexturedLoadSerial != m_levelMapLoadSerial || m_levelTexturedFaces.isEmpty()) { return picks; }
	picks.faces = m_levelTexturedFaces; picks.brushGeometry = m_levelTexturedGeometry;
	for (const auto& face : m_levelTexturedFaces) {
		if (m_levelMapViewport->isHidden(LevelMapSelectionKind::QuakeBrush,face.brushId)) {
			picks.excludedObjects.insert(levelMapSelectionRefId({LevelMapSelectionKind::QuakeBrush,face.brushId}));
		}
	}
	return picks;
}
QVector<LevelMapComponentSet> ApplicationShell::levelComponentTransformSets(QString* error) const
{
	if (error) { error->clear(); }
	if (!levelComponentsActive()) { return {}; }
	const auto picked = m_levelMapViewport->pickedComponents();
	const auto faces = levelComponentFacePicks();
	if (faces.faces.isEmpty()) { return picked; }
	QVector<LevelMapComponentSet> sets;
	if (!levelSurfaceFaceComponentSets(m_levelMapDocument,faces,picked,&sets,error)) { return {}; }
	return sets;
}
void ApplicationShell::adoptLevelTexturedFaces(const QVector<LevelSurfaceFace>& before, const QHash<int,QByteArray>& beforeGeometry,
	const LevelSurfaceFaceTransformReport& report)
{
	m_levelTexturedHistory.append({before,beforeGeometry});
	m_levelTexturedHistory.append({report.faces,report.brushGeometry});
	while (m_levelTexturedHistory.size() > 64) { m_levelTexturedHistory.removeFirst(); }
	m_levelTexturedFaces = report.faces; m_levelTexturedGeometry = report.brushGeometry;
	m_levelTexturedLoadSerial = m_levelMapLoadSerial; m_levelTexturedRevision = m_levelMapDocument.revision;
}
QString ApplicationShell::levelComponentSelectionToken() const
{
	if (!m_levelMapViewport) { return {}; }
	QStringList parts{QString::number(static_cast<int>(m_levelMapViewport->componentMode()))};
	for (const auto& set : m_levelMapViewport->pickedComponents()) {
		auto indices = set.components; std::sort(indices.begin(),indices.end()); QStringList ids;
		for (int id : indices) { ids << QString::number(id); }
		parts << QStringLiteral("%1/%2/%3").arg(levelMapSelectionRefId(set.object)).arg(static_cast<int>(set.kind)).arg(ids.join(','));
	}
	for (const auto& face : levelComponentFacePicks().faces) { parts << QStringLiteral("face:%1:%2").arg(face.brushId).arg(face.faceIndex); }
	return parts.join(';');
}
bool ApplicationShell::applyLevelComponentTransform(const LevelComponentTransformRequest& input, QString* error, LevelMapComponentReport* report)
{
	auto request = input;
	if (!request.pivot.valid && m_levelComponentPivot.valid) { request.pivot = m_levelComponentPivot; }
	if (!levelComponentsActive()) { if (error) { *error = tr("Enter a component mode and pick components to transform."); } return false; }
	const auto picked = m_levelMapViewport->pickedComponents();
	const auto faces = levelComponentFacePicks();
	QString problem; const auto sets = levelComponentTransformSets(&problem);
	if (!problem.isEmpty()) { if (error) { *error = problem; } return false; }
	LevelMapVec3 lo,hi; LevelTextureAffine matrix;
	if (!levelMapComponentBounds(m_levelMapDocument,sets,&lo,&hi,error) ||
		!levelComponentTransformMatrix(request,{(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2,true},&matrix,error)) { return false; }
	// Handles follow their transformed centres; independent faces have none.
	QVector<QPair<LevelMapSelectionRef,LevelMapVec3>> picks;
	for (const auto& set : picked) {
		LevelBrushTopology topology;
		if (set.object.kind == LevelMapSelectionKind::QuakeBrush) {
			const auto brush = std::find_if(m_levelMapDocument.brushes.cbegin(),m_levelMapDocument.brushes.cend(),[&](const auto& b) { return b.id == set.object.objectId; });
			if (brush == m_levelMapDocument.brushes.cend() || !levelBrushTopology(*brush,&topology,error)) { return false; }
		}
		for (int id : set.components) {
			QVector<LevelMapVec3> vertices;
			if (set.object.kind == LevelMapSelectionKind::QuakePatch) {
				const auto patch = std::find_if(m_levelMapDocument.patches.cbegin(),m_levelMapDocument.patches.cend(),[&](const auto& p) { return p.id == set.object.objectId; });
				vertices << patch->controlPoints[id];
			} else if (set.kind == LevelBrushComponent::Vertex) { vertices << topology.vertices[id]; }
			else if (set.kind == LevelBrushComponent::Edge) { for (int vertex : topology.edges[id]) { vertices << topology.vertices[vertex]; } }
			else { for (int vertex : topology.faces[id]) { vertices << topology.vertices[vertex]; } }
			LevelMapVec3 centre{0,0,0,true};
			for (const auto& vertex : vertices) {
				const auto moved = transformedLevelComponentPoint(vertex,matrix,request.grid);
				centre.x += moved.x/vertices.size(); centre.y += moved.y/vertices.size(); centre.z += moved.z/vertices.size();
			}
			picks.append({set.object,centre});
		}
	}
	LevelPlacementRequest work; work.operation = LevelPlacementOperation::TransformComponents;
	work.components = picked; work.componentTransform = request; work.surfaceFaces = faces;
	const auto publish = levelPlacementCommitter();
	const auto result = LevelPlacementTaskDialog::prepare(this,m_levelMapDocument,work);
	if (!result.succeeded) { if (error) { *error = result.error; } return false; }
	const auto beforeFaces = m_levelTexturedFaces; const auto beforeGeometry = m_levelTexturedGeometry;
	if (!publish(result.document,error)) { return false; }
	if (report) { *report = result.componentReport; }
	if (!faces.faces.isEmpty()) { adoptLevelTexturedFaces(beforeFaces,beforeGeometry,result.surfaceFaceReport); }
	refreshLevelMapWorkbench();
	for (auto* view : std::as_const(m_levelPlanViews)) { view->setPickedComponentPositions(picks); }
	shareLevelComponentPicks(m_levelMapViewport);
	if (!faces.faces.isEmpty()) { refreshLevelMap3DHighlight(); refreshLevelSurfaceTools(); }
	recordActivity(tr("Level components transformed"),m_levelMapDocument.sourcePath,QStringLiteral("level-map"),OperationState::Warning,tr("Unsaved map edit"));
	const QString fitted = levelComponentUvFitNote(result.componentReport.fittedFaces);
	if (faces.faces.isEmpty()) { statusBar()->showMessage(tr("Transformed the picked components. Undo restores the complete edit.")+fitted,5000); }
	else {
		const auto kept = static_cast<int>(result.surfaceFaceReport.faces.size());
		statusBar()->showMessage(tr("Transformed the picked components and faces; %n face pick(s) kept. Undo restores the complete edit.",nullptr,kept)+fitted,5000);
	}
	return true;
}
void ApplicationShell::transformLevelComponentsFromUi(int operation)
{
	QString error; LevelMapVec3 lo,hi;
	const auto sets = levelComponentTransformSets(&error);
	if (!levelComponentsActive() || !error.isEmpty() || !levelMapComponentBounds(m_levelMapDocument,sets,&lo,&hi,&error)) {
		statusBar()->showMessage(error.isEmpty() ? tr("Pick vertices, edges, faces or patch control points to transform.") : error,6000); return;
	}
	LevelComponentTransformRequest initial; initial.operation = static_cast<LevelComponentTransformKind>(operation);
	initial.pivot = m_levelComponentPivot;
	applyLevelComponentUvSettings(&initial);
	initial.amount = operation == 1 ? 90 : operation == 3 ? .25 : 0;
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto token = levelComponentSelectionToken();
	LevelComponentTransformDialog dialog(initial,{(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2,true},this);
	const auto independent = static_cast<int>(levelComponentFacePicks().faces.size());
	if (independent > 0) { dialog.setScopeSummary(tr("Includes %n face(s) picked by Select Textured. Their brushes stay unselected.",nullptr,independent)); }
	dialog.setApplyHandler([this,load,revision,token](const auto& request, QString* error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || token != levelComponentSelectionToken()) {
			if (error) { *error = tr("The map or picked components changed. Reopen Transform Components from the current selection."); } return false;
		}
		return applyLevelComponentTransform(request,error);
	});
	dialog.exec();
}
} // namespace vibestudio
