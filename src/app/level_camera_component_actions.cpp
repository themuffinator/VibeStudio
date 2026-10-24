#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "core/level_brush.h"
#include "core/level_textured_selection.h"
#include <QStatusBar>
#include <QThread>
#include <QPointer>
#include <optional>

namespace vibestudio {
namespace {
ModelVec3 modelPoint(LevelMapVec3 p) { return {float(p.x),float(p.y),float(p.z)}; }
bool same(LevelMapVec3 a, LevelMapVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
LevelMapVec3 mapPoint(ModelVec3 p) { return {p.x,p.y,p.z,true}; }
struct ComponentDraft {
	LevelMapDocument document;
	QVector<LevelMapComponentSet> sets;
	QVector<LevelMapVertexInsertion> insertions;
	LevelMapVec3 delta;
	bool affine = false;
	quint64 gizmoSerial = 0;
	LevelComponentTransformRequest transform;
	bool split = false;
	int extrusion = 0;
	double extrusionDistance = 0;
	bool textureLock = true;
	bool fitUvLock = false;
	bool allowValve220 = false;
	LevelMapExtrusionReport extrusionReport;
	QVector<QPair<LevelMapSelectionRef,LevelMapVec3>> resultPicks;
	LevelMapComponentReport report;
	// Face-mode moves and manipulator transforms include Select Textured picks.
	LevelSurfaceFacePicks faces;
	LevelSurfaceFaceTransformReport faceReport;
	QVector<QPair<ModelVec3,ModelVec3>> edges;
	QString error;
};

// One worker and one replaceable pending request. No event-loop geometry
// rebuilds and no unbounded thread queue during a dense component drag.
class ComponentWorker final : public QObject {
public:
	explicit ComponentWorker(QObject* parent) : QObject(parent) {}
	~ComponentWorker() override {
		reset();
		if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; }
	}
	void reset() { ++m_serial; m_pending.reset(); }
	void request(ComponentDraft draft) { ++m_serial; m_pending = std::move(draft); start(); }
	std::function<void(ComponentDraft)> completed;
private:
	void start() {
		if (m_thread || !m_pending) { return; }
		auto draft = std::make_shared<ComponentDraft>(std::move(*m_pending)); m_pending.reset();
		const auto serial = m_serial;
		m_thread = QThread::create([draft] {
			const auto delta = draft->delta;
			LevelComponentTransformRequest transform = draft->affine ? draft->transform : LevelComponentTransformRequest{};
			if (!draft->affine) { transform.delta = delta; transform.allowCollapse = true; }
			transform.textureLock = draft->textureLock; transform.fitUvLock = draft->fitUvLock; transform.allowValve220 = draft->allowValve220;
			if (draft->affine) {
				LevelTextureAffine matrix;
				if (!levelComponentTransformMatrix(transform,{},&matrix,&draft->error)) { return; }
				for (auto& pick : draft->resultPicks) { pick.second = transformedLevelComponentPoint(pick.second,matrix); }
			}
			const bool valid = !draft->insertions.isEmpty()
				? insertLevelMapVertices(&draft->document,draft->insertions,0,false,&draft->report,&draft->error)
				: draft->extrusion
				? extrudeLevelMapFaces(&draft->document,draft->sets,draft->extrusionDistance,draft->extrusion == 2,draft->textureLock,&draft->extrusionReport,&draft->error)
				: draft->split
				? splitLevelMapComponents(&draft->document,draft->sets,delta,false,&draft->report,&draft->error)
				: !draft->faces.faces.isEmpty()
				? transformLevelMapSurfaceFaces(&draft->document,draft->faces,draft->sets,transform,&draft->faceReport,&draft->error)
				: transformLevelMapComponents(&draft->document,draft->sets,transform,&draft->report,&draft->error);
			if (!valid) { return; }
			if (draft->extrusion) { draft->report.brushes = draft->extrusionReport.changedBrushes; }
			if (!draft->faces.faces.isEmpty()) { draft->report = draft->faceReport.components; }
			QSet<int> brushes,patches;
			for (const auto& face : std::as_const(draft->faces.faces)) { brushes.insert(face.brushId); }
			for (const auto& insertion : std::as_const(draft->insertions)) {
				brushes.insert(insertion.brushId);
				for (const auto& p : insertion.positions) { draft->resultPicks.append({{LevelMapSelectionKind::QuakeBrush,insertion.brushId},p}); }
			}
			for (int id : draft->extrusionReport.brushIds) { brushes.insert(id); }
			for (const auto& set : std::as_const(draft->sets)) {
				if (set.object.kind == LevelMapSelectionKind::QuakeBrush) { brushes.insert(set.object.objectId); }
				else if (set.object.kind == LevelMapSelectionKind::QuakePatch) { patches.insert(set.object.objectId); }
			}
			for (const auto& brush : std::as_const(draft->document.brushes)) {
				if (!brushes.contains(brush.id)) { continue; }
				LevelBrushTopology topology;
				if (!levelBrushTopology(brush,&topology)) { continue; }
				for (const auto& set : std::as_const(draft->extrusionReport.faces)) {
					if (set.object.objectId != brush.id || set.components.isEmpty()) { continue; }
					const int face = set.components[0];
					if (face < 0 || face >= topology.geometry.faces.size()) { continue; }
					const auto& polygon = topology.geometry.faces[face].points;
					LevelMapVec3 centre{0,0,0,true};
					for (const auto& p : polygon) { centre.x += p.x/polygon.size(); centre.y += p.y/polygon.size(); centre.z += p.z/polygon.size(); }
					draft->resultPicks.append({set.object,centre});
				}
				for (const auto& edge : std::as_const(topology.edges)) { draft->edges.append({modelPoint(topology.vertices[edge[0]]),modelPoint(topology.vertices[edge[1]])}); }
			}
			for (const auto& patch : std::as_const(draft->document.patches)) {
				if (!patches.contains(patch.id)) { continue; }
				for (int i = 0; i < patch.controlPoints.size(); ++i) {
					const auto p = modelPoint(patch.controlPoints[i]);
					if (patch.width > 0 && i%patch.width+1 < patch.width && i+1 < patch.controlPoints.size()) { draft->edges.append({p,modelPoint(patch.controlPoints[i+1])}); }
					if (patch.width > 0 && i+patch.width < patch.controlPoints.size()) { draft->edges.append({p,modelPoint(patch.controlPoints[i+patch.width])}); }
				}
			}
		});
		connect(m_thread,&QThread::finished,this,[this,draft,serial] {
			m_thread->deleteLater(); m_thread = nullptr;
			if (serial == m_serial && completed) { completed(std::move(*draft)); }
			start();
		});
		m_thread->start();
	}
	quint64 m_serial = 0;
	QThread* m_thread = nullptr;
	std::optional<ComponentDraft> m_pending;
};
}

void ApplicationShell::connectLevelCameraComponents()
{
	struct Source {
		QPointer<MapViewport> plan;
		QPointer<LevelComponentGizmo> gizmo;
		QPointer<LevelVertexInsertion> insertion;
		bool active = false, split = false;
		int extrusion = 0;
		bool textureLock = true;
		bool fitUvLock = false;
		bool allowValve220 = false;
		QString componentToken;
		quint64 revision = 0, loadSerial = 0;
		QVector<LevelMapSelectionRef> selection;
		QVector<LevelMapComponentSet> sets;
		LevelSurfaceFacePicks faces;
		QVector<QPair<LevelMapSelectionRef,LevelMapVec3>> picks;
		LevelMapDocument document;
		std::optional<ComponentDraft> prepared;
	};
	const auto source = std::make_shared<Source>();
	auto* worker = new ComponentWorker(this);
	const auto current = [this,source] {
		const auto& before = source->document; const auto& now = m_levelMapDocument;
		// Saving or changing the creation destination can change document state
		// without a geometry revision. A draft must not restore old file bindings,
		// clean/dirty history or the user's current creation context.
		return source->revision == now.revision && source->loadSerial == m_levelMapLoadSerial && source->selection == now.selection
			&& before.sourcePath == now.sourcePath && before.outputPath == now.outputPath && before.sourceContentHash == now.sourceContentHash
			&& before.savedUndoDepth == now.savedUndoDepth && before.editState == now.editState && before.historyRevision == now.historyRevision
			&& before.activeSceneNode == now.activeSceneNode && before.undoLimit == now.undoLimit
			&& source->componentToken == levelComponentSelectionToken()
			&& source->textureLock == (source->extrusion ? m_settings.levelTextureLock() : m_settings.levelComponentUvLock())
			&& source->fitUvLock == m_settings.levelComponentUvFit()
			&& source->allowValve220 == m_settings.levelAllowValve220();
	};
	const auto cancel = [this,source] {
		if (source->insertion) { source->insertion->finish(false); }
		else if (source->gizmo) { source->gizmo->finish(false); }
		else if (source->plan) { source->plan->finishComponentMove(false); }
		else { m_levelMap3D->finishComponentMove(false); }
	};
	m_levelComponentsRefresh = [source,current,cancel] { if (source->active && !current()) { cancel(); } };
	connect(m_levelMap3D,&ModelViewport::componentModeExitRequested,this,[this] { setLevelComponentMode(0); });
	connect(m_levelMap3D,&ModelViewport::texturedSelectionClearRequested,this,&ApplicationShell::selectNoLevelMapObjects);
	connect(m_levelMap3D,&ModelViewport::componentPicksChanged,this,[this] {
		if (!m_levelMapViewport) { return; }
		m_levelMapViewport->setPickedComponentHandles(m_levelMap3D->pickedComponents()); shareLevelComponentPicks(m_levelMapViewport);
	});
	const auto activeChanged = [this,source,worker,cancel](MapViewport* plan, bool active, LevelComponentGizmo* gizmo = nullptr, LevelVertexInsertion* insertion = nullptr) {
		if (!active) {
			if (source->plan == plan && source->gizmo == gizmo && source->insertion == insertion) { source->active = false; worker->reset(); source->prepared.reset(); source->gizmo = nullptr; source->insertion = nullptr; }
			return;
		}
		if (source->active && (source->plan != plan || source->gizmo != gizmo || source->insertion != insertion)) { cancel(); }
		worker->reset(); source->prepared.reset(); source->plan = plan; source->gizmo = gizmo; source->insertion = insertion; source->active = false;
		if (plan) { activateLevelPlanViewport(plan); }
		source->revision = m_levelMapDocument.revision; source->loadSerial = m_levelMapLoadSerial;
		source->selection = m_levelMapDocument.selection; source->sets = m_levelMapViewport->pickedComponents();
		source->picks = m_levelMapViewport->pickedComponentPositions(); source->document = m_levelMapDocument;
		source->split = plan ? plan->componentSplit() : m_levelMap3D->componentSplit();
		source->extrusion = plan ? plan->componentExtrusion() : m_levelMap3D->componentExtrusion();
		if (insertion) { source->split = false; source->extrusion = 0; }
		// Split and extrusion stay handle-only; moves and manipulator transforms
		// carry the Select Textured face picks with the handles.
		source->faces = insertion || source->split || source->extrusion ? LevelSurfaceFacePicks{} : levelComponentFacePicks();
		source->textureLock = source->extrusion ? m_settings.levelTextureLock() : m_settings.levelComponentUvLock();
		source->fitUvLock = m_settings.levelComponentUvFit();
		source->allowValve220 = m_settings.levelAllowValve220(); source->componentToken = levelComponentSelectionToken();
		source->active = plan ? plan->isMovingComponents() : m_levelMap3D->isMovingComponents();
	};
	const auto request = [this,source,worker](LevelMapVec3 delta) {
		source->prepared.reset(); ComponentDraft request;
		request.document = source->document; request.sets = source->sets; request.faces = source->faces; request.delta = delta; request.split = source->split;
		request.extrusion = source->extrusion; request.textureLock = source->textureLock; request.fitUvLock = source->fitUvLock;
		request.allowValve220 = source->allowValve220;
		request.extrusionDistance = source->plan ? source->plan->componentExtrusionDistance() : m_levelMap3D->componentExtrusionDistance();
		worker->request(std::move(request));
	};
	worker->completed = [this,source,current,cancel](ComponentDraft draft) {
		if (!source->active) { return; }
		if (!current()) { cancel(); return; }
		if (!draft.insertions.isEmpty()) {
			if (!source->insertion || source->insertion->serial() != draft.gizmoSerial) { return; }
			source->prepared = std::move(draft); QVector<QPair<LevelMapVec3,LevelMapVec3>> lines;
			for (const auto& edge : source->prepared->edges) { lines.append({mapPoint(edge.first),mapPoint(edge.second)}); }
			const auto error = source->prepared->error; const auto serial = source->prepared->gizmoSerial;
			source->insertion->setPreview(serial,lines,error);
			if (!error.isEmpty()) { statusBar()->showMessage(tr("Vertex insertion refused: %1").arg(error),6000); }
			return;
		}
		if (draft.affine) {
			if (!source->gizmo || source->gizmo->serial() != draft.gizmoSerial) { return; }
			source->prepared = std::move(draft); QVector<QPair<LevelMapVec3,LevelMapVec3>> lines;
			for (const auto& edge : source->prepared->edges) { lines.append({mapPoint(edge.first),mapPoint(edge.second)}); }
			const auto error = source->prepared->error; const auto serial = source->prepared->gizmoSerial;
			source->gizmo->setPreview(serial,lines,error);
			if (!error.isEmpty()) { statusBar()->showMessage(tr("Components unchanged: %1").arg(error),6000); }
			return;
		}
		const auto now = source->plan ? source->plan->componentMoveDelta() : mapPoint(m_levelMap3D->componentMoveDelta());
		if (!same(draft.delta,now)) { return; }
		if (draft.extrusion && draft.extrusionDistance != (source->plan ? source->plan->componentExtrusionDistance() : m_levelMap3D->componentExtrusionDistance())) { return; }
		source->prepared = std::move(draft);
		const auto delta = source->prepared->delta;
		// Completing a pending release can synchronously consume prepared.
		const auto edges = source->prepared->edges; const auto error = source->prepared->error;
		if (source->plan) {
			QVector<QPair<LevelMapVec3,LevelMapVec3>> lines;
			for (const auto& edge : edges) { lines.append({mapPoint(edge.first),mapPoint(edge.second)}); }
			source->plan->setComponentPreview(delta,lines,error);
		} else { m_levelMap3D->setComponentPreview(modelPoint(delta),edges,error); }
		if (!error.isEmpty()) { statusBar()->showMessage(tr("Components unchanged: %1").arg(error),6000); }
	};
	const auto commit = [this,source,current](LevelMapVec3 delta, quint64 gizmoSerial = 0, bool inserting = false) {
		if (!source->active || !current() || !source->prepared || !source->prepared->error.isEmpty()
			|| (inserting ? !source->insertion || source->prepared->insertions.isEmpty() || source->prepared->gizmoSerial != gizmoSerial
				: gizmoSerial ? !source->gizmo || !source->prepared->affine || source->prepared->gizmoSerial != gizmoSerial : !same(source->prepared->delta,delta))
			|| (source->extrusion && source->prepared->extrusionDistance != (source->plan ? source->plan->componentExtrusionDistance() : m_levelMap3D->componentExtrusionDistance()))) {
			statusBar()->showMessage(tr("The map or selection changed. The component move was cancelled.")); return;
		}
		const auto report = source->prepared->report;
		if (!report.brushes && !report.patches) { return; }
		const bool split = source->split || inserting;
		const bool extrusion = source->extrusion != 0;
		const auto created = source->prepared->extrusionReport.createdBrushes;
		const bool faces = !source->prepared->faces.faces.isEmpty();
		const auto faceReport = source->prepared->faceReport;
		source->active = false;
		m_levelMapDocument = std::move(source->prepared->document);
		if (faces) { adoptLevelTexturedFaces(m_levelTexturedFaces,m_levelTexturedGeometry,faceReport); }
		auto picks = extrusion || gizmoSerial ? source->prepared->resultPicks : source->picks;
		if (!extrusion && !gizmoSerial) { for (auto& pick : picks) { pick.second.x += delta.x; pick.second.y += delta.y; pick.second.z += delta.z; } }
		recordActivity(inserting ? tr("Level map vertices inserted") : extrusion ? tr("Level map faces extruded") : split ? tr("Level map components split") : tr("Level map components transformed"),tr("%n object(s)",nullptr,report.brushes+report.patches),QStringLiteral("level-map"),OperationState::Warning,tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		if (split) { setLevelComponentMode(1); }
		for (auto* view : std::as_const(m_levelPlanViews)) { view->setPickedComponentPositions(picks); }
		shareLevelComponentPicks(m_levelMapViewport);
		if (faces) { refreshLevelMap3DHighlight(); refreshLevelSurfaceTools(); }
		statusBar()->showMessage(extrusion ? tr("Extruded %1 brush(es); %2 new brush(es). The moved faces are selected; Undo restores the edit.").arg(report.brushes).arg(created)
			: gizmoSerial && !inserting ? tr("Transformed the picked components. Undo restores the complete edit.")+levelComponentUvFitNote(report.fittedFaces)
			: split ? tr("Inserted %n corner(s). The new vertices are selected; Undo restores the edit.",nullptr,report.insertedVertices)
			: tr("Moved the picked components. %n vertex(es) merged; Undo restores the edit.",nullptr,report.collapsedVertices)+levelComponentUvFitNote(report.fittedFaces),5000);
	};
	const auto connectGizmo = [this,source,worker,activeChanged,commit](MapViewport* plan,LevelComponentGizmo* gizmo) {
		connect(gizmo,&LevelComponentGizmo::activeChanged,this,[activeChanged,plan,gizmo](bool active) { activeChanged(plan,active,gizmo); });
		connect(gizmo,&LevelComponentGizmo::previewRequested,this,[source,worker,gizmo](quint64 serial) {
			if (!source->active || source->gizmo != gizmo) { return; }
			source->prepared.reset(); ComponentDraft request; request.affine = true; request.gizmoSerial = serial;
			request.document = source->document; request.sets = source->sets; request.faces = source->faces; request.transform = gizmo->request(); request.resultPicks = source->picks;
			request.textureLock = source->textureLock; request.fitUvLock = source->fitUvLock; request.allowValve220 = source->allowValve220;
			worker->request(std::move(request));
		});
		connect(gizmo,&LevelComponentGizmo::commitRequested,this,[commit](quint64 serial) { commit({0,0,0,true},serial); });
		connect(gizmo,&LevelComponentGizmo::pivotChanged,this,[this](const LevelMapVec3& pivot) {
			m_levelComponentPivot = pivot; refreshLevelComponentGizmos(); refreshLevelGeometryCommands();
			statusBar()->showMessage(tr("Component pivot set. Reset Component Pivot restores the selection centre."),5000);
		});
	};
	const auto connectInsertion = [this,source,worker,activeChanged,commit](MapViewport* plan,LevelVertexInsertion* insertion) {
		connect(insertion,&LevelVertexInsertion::activeChanged,this,[activeChanged,plan,insertion](bool active) { activeChanged(plan,active,nullptr,insertion); });
		connect(insertion,&LevelVertexInsertion::previewRequested,this,[source,worker,insertion](quint64 serial) {
			if (!source->active || source->insertion != insertion) { return; }
			source->prepared.reset(); ComponentDraft request;
			request.document = source->document; request.insertions = insertion->request(); request.gizmoSerial = serial;
			worker->request(std::move(request));
		});
		connect(insertion,&LevelVertexInsertion::commitRequested,this,[commit](quint64 serial) { commit({0,0,0,true},serial,true); });
	};
	connectGizmo(nullptr,m_levelMap3D->componentGizmo());
	connectInsertion(nullptr,m_levelMap3D->vertexInsertion());
	connect(m_levelMap3D,&ModelViewport::componentMoveActiveChanged,this,[activeChanged](bool active) { activeChanged(nullptr,active); });
	connect(m_levelMap3D,&ModelViewport::componentMovePreviewChanged,this,[request](ModelVec3 delta) { request(mapPoint(delta)); });
	connect(m_levelMap3D,&ModelViewport::componentMoveRequested,this,[commit](ModelVec3 delta) { commit(mapPoint(delta)); });
	connect(m_levelMap3D,&ModelViewport::vertexSnapRequested,this,[this](int id) {
		if (!m_levelMapViewport || id < 0 || id >= m_levelMapViewport->componentHandles().size()) { return; }
		const auto target = m_levelMapViewport->componentHandles()[id]; QString error;
		if (!snapLevelVertex(target.object,target.component,&error)) { statusBar()->showMessage(error,6000); }
	});
	for (auto* view : std::as_const(m_levelPlanViews)) {
		connectGizmo(view,view->componentGizmo());
		connectInsertion(view,view->vertexInsertion());
		connect(view,&MapViewport::componentMoveActiveChanged,this,[activeChanged,view](bool active) { activeChanged(view,active); });
		connect(view,&MapViewport::componentMovePreviewChanged,this,request);
		connect(view,&MapViewport::componentMoveRequested,this,[commit](const QVector<LevelMapComponentSet>&,const LevelMapVec3& delta) { commit(delta); });
		connect(view,&MapViewport::texturedFacesMoveRequested,this,[this,view](const LevelMapVec3& delta) {
			activateLevelPlanViewport(view);
			LevelComponentTransformRequest move; move.delta = delta;
			applyLevelComponentUvSettings(&move);
			QString error;
			if (!applyLevelComponentTransform(move,&error)) { statusBar()->showMessage(tr("Picked faces unchanged: %1").arg(error),6000); }
		});
		connect(view,&MapViewport::vertexSnapRequested,this,[this,view](const LevelMapSelectionRef& object,int vertex) {
			activateLevelPlanViewport(view); QString error;
			if (!snapLevelVertex(object,vertex,&error)) { statusBar()->showMessage(error,6000); }
		});
	}
}

void ApplicationShell::refreshLevelCameraComponents()
{
	if (!m_levelMap3D || !m_levelMapViewport) { return; }
	const int mode = static_cast<int>(m_levelMapViewport->componentMode());
	if (mode != 1) { m_levelVertexInsertion = false; }
	const auto insertionData = m_levelMapViewport->vertexInsertionData();
	const bool insertionAvailable = mode == 1 && !m_levelMapViewport->componentHandlesLimited() && m_levelComponentTool == 0;
	for (auto* view : std::as_const(m_levelPlanViews)) {
		view->vertexInsertion()->setContext(insertionData,insertionAvailable,m_levelVertexInsertion,view->snapToGrid() ? view->gridSize() : 0);
	}
	m_levelMap3D->vertexInsertion()->setContext(insertionData,insertionAvailable,m_levelVertexInsertion,m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0);
	QVector<ModelVec3> positions; QVector<QString> geometryKeys;
	const auto& handles = m_levelMapViewport->componentHandles();
	for (int index = 0; index < handles.size(); ++index) {
		positions.append(modelPoint(handles[index].position));
		geometryKeys.append(handles[index].geometryKey);
	}
	m_levelMap3D->setComponentHandles(mode,positions,m_levelMapViewport->pickedComponentHandles(),m_levelMapViewport->componentHandlesLimited(),geometryKeys);
	m_levelMap3D->setComponentSplit(m_levelMapViewport->componentSplit());
	if (mode != 3) { m_levelComponentExtrusion = 0; }
	LevelMapVec3 normal; QVector<LevelMapVec3> vertices;
	const auto sets = m_levelMapViewport->pickedComponents();
	if (m_levelComponentExtrusion && !sets.isEmpty() && sets[0].kind == LevelBrushComponent::Face && sets[0].object.kind == LevelMapSelectionKind::QuakeBrush && !sets[0].components.isEmpty()) {
		const auto found = std::find_if(m_levelMapDocument.brushes.cbegin(),m_levelMapDocument.brushes.cend(),[&](const auto& brush) { return brush.id == sets[0].object.objectId; });
		if (found != m_levelMapDocument.brushes.cend()) {
			auto cache = m_levelPreparedBrushGeometry ? *m_levelPreparedBrushGeometry : MapBrushGeometryCache{};
			const auto geometry = cache.resolve(*found); const int face = sets[0].components[0];
			if (face >= 0 && face < geometry.faces.size() && geometry.faces[face].isValid()) {
				const auto& polygon = geometry.faces[face];
				normal = {polygon.plane.normalX,polygon.plane.normalY,polygon.plane.normalZ,true}; vertices = polygon.points;
			}
		}
	}
	for (auto* view : std::as_const(m_levelPlanViews)) { view->setComponentExtrusion(m_levelComponentExtrusion,normal,vertices); }
	m_levelMap3D->setComponentExtrusion(m_levelComponentExtrusion,normal,vertices);
	refreshLevelComponentGizmos();
}
} // namespace vibestudio
