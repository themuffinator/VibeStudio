#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "core/map_geometry_cache.h"
#include "core/level_placement_control_p.h"
#include <QCoreApplication>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QThread>
#include <optional>
#include <atomic>

namespace vibestudio {
namespace {
struct ClipDraft {
	LevelMapDocument document;
	LevelClipState state;
	LevelMapClipOptions options;
	LevelClipPreview preview;
	LevelMapClipReport report;
};
class ClipWorker final : public QObject {
public:
	explicit ClipWorker(QObject* parent) : QObject(parent) {}
	~ClipWorker() override { reset(); if (m_thread) { m_thread->disconnect(this); m_thread->wait(); delete m_thread; } }
	void reset() { ++m_serial; m_pending.reset(); if (m_cancelled) { m_cancelled->store(true); } }
	void request(ClipDraft draft) { ++m_serial; m_pending = std::move(draft); start(); }
	std::function<void(ClipDraft)> completed;
private:
	void start() {
		if (m_thread || !m_pending) { return; }
		auto draft = std::make_shared<ClipDraft>(std::move(*m_pending)); m_pending.reset(); const auto serial = m_serial;
		m_cancelled = std::make_shared<std::atomic_bool>(false);
		m_thread = QThread::create([draft,cancelled=m_cancelled] {
			const LevelPlacementControl control{[cancelled] { return cancelled->load(); },{}};
			const detail::PlacementControlScope scope(control);
			try {
			QString error;
			if (!clipLevelMapPoints(&draft->document,draft->state.points,draft->state.axis,draft->state.direction,draft->state.keep,draft->options,&draft->report,&error)) {
				draft->preview.status = LevelClipPreview::Status::Refused;
				draft->preview.message = QCoreApplication::translate("LevelClipper","Cut refused: %1").arg(error); return;
			}
			draft->preview.status = LevelClipPreview::Status::Ready;
			draft->preview.wrapped = draft->report.wrapped;
			draft->preview.message = draft->report.wrapped
				? QCoreApplication::translate("LevelClipper","Checked wrap/merge: %n brush(es) become one convex hull, filling gaps. Dashed wire shows the result. Enter applies; Escape leaves.",nullptr,draft->report.changed)
				: QCoreApplication::translate("LevelClipper","Checked cut: %n brush(es) changed; dashed wire shows the kept result. Enter applies; Escape leaves.",nullptr,draft->report.changed);
			draft->preview.message += draft->report.wrapped ? QCoreApplication::translate("LevelClipper"," New hull material: %1.").arg(draft->options.wrapTexture)
				: draft->options.cutTexture.isEmpty() ? QCoreApplication::translate("LevelClipper"," Cut faces inherit each brush's majority material.")
				: QCoreApplication::translate("LevelClipper"," Cut material: %1.").arg(draft->options.cutTexture);
			QSet<int> brushes,entities;
			for (const auto& ref : draft->document.selection) {
				if (ref.kind == LevelMapSelectionKind::QuakeBrush) { brushes.insert(ref.objectId); }
				if (ref.kind == LevelMapSelectionKind::Entity) { entities.insert(ref.objectId); }
			}
			for (const auto& brush : std::as_const(draft->document.brushes)) {
				detail::placementCancellationCheckpoint();
				if (!brushes.contains(brush.id) && !entities.contains(brush.entityId)) { continue; }
				const auto geometry = solveBrushGeometry(brush.faces,brush.id,brush.entityId);
				for (const auto& polygon : geometry.faces) {
					if (draft->preview.edges.size()+polygon.points.size() > 65536) {
						draft->preview.message += QCoreApplication::translate("LevelClipper"," Preview wire limit reached; use a smaller selection to inspect every edge."); return;
					}
					for (int i = 0; i < polygon.points.size(); ++i) { draft->preview.edges.append({polygon.points[i],polygon.points[(i+1)%polygon.points.size()]}); }
				}
			}
			} catch (const detail::PlacementCancelled&) {
				draft->preview = {LevelClipPreview::Status::Refused,{},QCoreApplication::translate("LevelClipper","Clipper preview cancelled.")};
			}
		});
		connect(m_thread,&QThread::finished,this,[this,draft,serial] {
			m_thread->deleteLater(); m_thread = nullptr;
			if (serial == m_serial && completed) { completed(std::move(*draft)); } start();
		});
		m_thread->start();
	}
	quint64 m_serial = 0;
	QThread* m_thread = nullptr;
	std::optional<ClipDraft> m_pending;
	std::shared_ptr<std::atomic_bool> m_cancelled;
};
}

void ApplicationShell::connectLevelClipper()
{
	m_levelClipPreferences = m_settings.levelClipperPreferences();
	for (auto* view : std::as_const(m_levelPlanViews)) { view->setClipPreferences(m_levelClipPreferences); }
	m_levelMap3D->setClipPreferences(m_levelClipPreferences);
	m_levelMap3D->setClipBoundsResolver([this](auto* low,auto* high) { return levelMapSelectionBounds(m_levelMapDocument,low,high); });
	struct Source {
		LevelMapDocument before;
		quint64 loadSerial = 0;
		LevelClipState state;
		LevelMapClipOptions options;
		bool broadcasting = false, applyWhenReady = false;
		std::optional<ClipDraft> prepared;
		MapBrushGeometryCache cache;
	};
	const auto source = std::make_shared<Source>(); auto* worker = new ClipWorker(this);
	const auto active = [this] { return m_levelMapViewport && m_levelMapViewport->clipMode(); };
	const auto sameDocument = [this,source,active] {
		const auto& old = source->before; const auto& now = m_levelMapDocument;
		return active() && source->loadSerial == m_levelMapLoadSerial && old.revision == now.revision && old.historyRevision == now.historyRevision && old.selection == now.selection
			&& old.sourcePath == now.sourcePath && old.outputPath == now.outputPath && old.sourceContentHash == now.sourceContentHash
			&& old.savedUndoDepth == now.savedUndoDepth && old.editState == now.editState && old.activeSceneNode == now.activeSceneNode && old.undoLimit == now.undoLimit;
	};
	const auto current = [this,source,sameDocument] { return sameDocument() && source->options == levelClipOptions(); };
	const auto show = [this](LevelClipPreview preview) {
		if (m_levelQuickClip && !preview.message.isEmpty()) { preview.message = tr("Quick clip — %1").arg(preview.message); }
		for (auto* view : std::as_const(m_levelPlanViews)) { view->setClipPreview(preview); }
		m_levelMap3D->setClipPreview(preview);
	};
	const auto propagate = [this,source](const LevelClipState& state) {
		const QScopedValueRollback guard(source->broadcasting,true);
		for (auto* view : std::as_const(m_levelPlanViews)) { view->setClipState(state); }
		m_levelMap3D->setClipState(state);
	};
	const auto changed = [this,source,worker,propagate,show,active](const LevelClipState& state) {
		if (source->broadcasting || !active()) { return; }
		source->state = state; source->applyWhenReady = false; source->prepared.reset(); worker->reset(); propagate(state);
		refreshLevelDeletionCommands();
		source->before = m_levelMapDocument; source->loadSerial = m_levelMapLoadSerial;
		source->options = levelClipOptions();
		if (!state.actionable(source->options.wrapMerge)) {
			show({LevelClipPreview::Status::Empty,{},state.points.size() >= 2 ? tr("The clip points do not define a plane. Move a point or add a third point.") : QString{}}); return;
		}
		show({LevelClipPreview::Status::Pending,{},source->options.wrapMerge ? tr("Checking cut or wrap/merge… Enter waits for the result; Escape cancels.") : tr("Checking cut… Enter waits for the result; Escape cancels.")});
		ClipDraft draft; draft.document = source->before; draft.state = state; draft.options = source->options; worker->request(std::move(draft));
	};
	for (auto* view : std::as_const(m_levelPlanViews)) {
		connect(view,&MapViewport::clipStateChanged,this,[view,changed] { changed(view->clipState()); });
		connect(view,&MapViewport::quickClipRequested,this,&ApplicationShell::startLevelQuickClip);
	}
	connect(m_levelMap3D,&ModelViewport::clipStateChanged,this,[this,changed] { changed(m_levelMap3D->clipState()); });
	connect(m_levelMap3D,&ModelViewport::clipModeChanged,this,&ApplicationShell::setLevelMapClipMode);
	m_levelClipPreferencesChanged = [this,changed] {
		for (auto* view : std::as_const(m_levelPlanViews)) { view->finishClipPoint(false); view->setClipPreferences(m_levelClipPreferences); }
		m_levelMap3D->finishClipPoint(false); m_levelMap3D->setClipPreferences(m_levelClipPreferences);
		changed(m_levelMapViewport->clipState());
	};
	m_levelClipRefresh = [this,source,worker,sameDocument,current,changed,show,propagate,active] {
		if (!active()) { m_levelQuickClip = false; }
		if (active() && source->state.points.isEmpty()) { return; }
		if (active() && current()) { return; }
		if (sameDocument()) { changed(source->state); return; }
		worker->reset(); source->prepared.reset(); source->applyWhenReady = false; source->state.points.clear();
		if (m_levelClipPreferences.resetSide) { source->state.keep = LevelMapClipKeep::Back; }
		propagate(source->state); show({});
	};
	m_levelClipCommit = [this,source,worker,current,show,propagate,changed] {
		if (!current()) { m_levelClipRefresh(); statusBar()->showMessage(tr("The map, selection or cut material changed; check the new preview before applying.")); return; }
		if (!source->state.actionable(source->options.wrapMerge)) { return; }
		if (!source->prepared) { source->applyWhenReady = true; return; }
		if (source->prepared->preview.status != LevelClipPreview::Status::Ready) { statusBar()->showMessage(source->prepared->preview.message); return; }
		const int count = source->prepared->report.changed; const bool wrapped = source->prepared->report.wrapped;
		m_levelMapDocument = std::move(source->prepared->document);
		source->prepared.reset(); source->applyWhenReady = false; worker->reset();
		if (m_levelClipPreferences.resetPoints) { source->state.points.clear(); }
		if (m_levelClipPreferences.resetSide) { source->state.keep = LevelMapClipKeep::Back; }
		// Rebase retained points onto the committed document before the ordinary
		// refresh checks bindings. Undo or a later selection change still cancels.
		source->before = m_levelMapDocument; source->loadSerial = m_levelMapLoadSerial;
		propagate(source->state); show({});
		recordActivity(wrapped ? tr("Level map brushes wrapped/merged") : tr("Level map brushes clipped"),tr("%n brush(es)",nullptr,count),QStringLiteral("level-map"),OperationState::Warning,tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		if (m_levelQuickClip) { setLevelMapClipMode(false); }
		else if (!source->state.points.isEmpty()) { changed(source->state); }
		statusBar()->showMessage(wrapped ? tr("Wrapped/merged %n brush(es) into the checked hull. Undo restores the geometry and selection.",nullptr,count)
			: tr("Applied the checked cut to %n brush(es). Undo restores the geometry and selection.",nullptr,count));
	};
	connect(m_levelMap3D,&ModelViewport::clipRequested,this,[this] { m_levelClipCommit(); });
	worker->completed = [this,source,current,show](ClipDraft draft) {
		if (!current() || !(draft.state == source->state)) { m_levelClipRefresh(); return; }
		source->prepared = std::move(draft); show(source->prepared->preview);
		if (source->applyWhenReady) { source->applyWhenReady = false; m_levelClipCommit(); }
	};
	m_levelMap3D->setClipSurfaceResolver([this,source](int triangle,LevelMapVec3* point,LevelClipState* match) {
		if (triangle < 0 || triangle >= m_levelMap3DOwners.size()) { return false; }
		const auto owner = m_levelMap3DOwners[triangle]; const int face = m_levelMap3DFaces.value(triangle,-1);
		if (owner.kind != LevelMapSelectionKind::QuakeBrush || face < 0) { return false; }
		const auto it = std::find_if(m_levelMapDocument.brushes.cbegin(),m_levelMapDocument.brushes.cend(),[&](const auto& brush) { return brush.id == owner.objectId; });
		if (it == m_levelMapDocument.brushes.cend() || face >= it->faces.size()) { return false; }
		if (match) {
			const auto& original = it->faces[face]; match->points = {original.p1,original.p0,original.p2};
			return match->plane(nullptr,nullptr,nullptr);
		}
		if (!point) { return false; }
		const auto geometry = source->cache.resolve(*it);
		if (!geometry.solved || face >= geometry.faces.size()) { return false; }
		const auto plane = geometry.faces[face].plane;
		const double normal[]{plane.normalX,plane.normalY,plane.normalZ};
		const int axis = int(std::max_element(std::begin(normal),std::end(normal),[](double a,double b) { return std::abs(a) < std::abs(b); })-std::begin(normal));
		if (std::abs(normal[axis]) < 1e-6) { return false; }
		double values[]{point->x,point->y,point->z};
		const double grid = m_levelMapViewport && m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0;
		double distance = plane.distance;
		for (int i = 0; i < 3; ++i) { if (i != axis) { if (grid > 0) { values[i] = snapLevelMapCoordinate(values[i],grid); } distance -= normal[i]*values[i]; } }
		values[axis] = distance/normal[axis]; const LevelMapVec3 snapped{values[0],values[1],values[2],true};
		// At face boundaries a coarse grid may lie outside the brush. Preserve
		// the actual hit in that case instead of jumping onto a different face.
		const bool inside = std::all_of(geometry.faces.cbegin(),geometry.faces.cend(),[&](const auto& f) { return planeDistanceToPoint(f.plane,snapped) <= 0.01; });
		if (inside) { *point = snapped; }
		else { double* axes[]{&point->x,&point->y,&point->z}; *axes[axis] -= planeDistanceToPoint(plane,*point)/normal[axis]; }
		return LevelClipState::finite(*point);
	});
}
} // namespace vibestudio
