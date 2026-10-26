// TrenchBroom's Brush Tool as Assemble Brush Tool and Create Assembled Brush.
// The plan views and the camera share one list of points (each keeps a copy,
// as the clip tool's views do); every change is propagated, and a worker
// checks the convex hull against a copy of the map with the same service
// Create Brush from Points uses (addLevelMapBrushFromPoints), so Enter
// publishes a checked result as one undo step in the current material, then
// clears the points. A changed map, selection, creation layer or material
// sends the points for a fresh check first.
// Workflow reference: TB AssembleBrushTool, MapViewToolBox (exclusive tools)
// and ActionManager "Menu/Edit/Tools/Brush Tool" (B) and "Controls/Map
// view/Create brush" (Return) at 4c756be1 (GPL-3.0-or-later). Original Qt
// implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_brush_assemble.h"
#include "core/level_placement_control_p.h"
#include "core/map_geometry_cache.h"

#include <QAction>
#include <QCoreApplication>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>

namespace vibestudio {
namespace {
struct AssembleDraft {
	LevelMapDocument document;
	QVector<LevelMapVec3> points;
	QString material;
	LevelBrushAssemblePreview preview;
	LevelBrushPointsReport report;
};

// One check at a time; a newer request replaces a waiting one and cancels a
// running one, and only the newest result is delivered.
class AssembleWorker final : public QObject {
public:
	explicit AssembleWorker(QObject* parent)
		: QObject(parent)
	{
	}
	~AssembleWorker() override
	{
		reset();
		if (m_thread) {
			m_thread->disconnect(this);
			m_thread->wait();
			delete m_thread;
		}
	}
	void reset()
	{
		++m_serial;
		m_pending.reset();
		if (m_cancelled) {
			m_cancelled->store(true);
		}
	}
	void request(AssembleDraft draft)
	{
		++m_serial;
		m_pending = std::move(draft);
		start();
	}
	std::function<void(AssembleDraft)> completed;

private:
	void start()
	{
		if (m_thread || !m_pending) {
			return;
		}
		auto draft = std::make_shared<AssembleDraft>(std::move(*m_pending));
		m_pending.reset();
		const auto serial = m_serial;
		m_cancelled = std::make_shared<std::atomic_bool>(false);
		m_thread = QThread::create([draft, cancelled = m_cancelled] {
			const LevelPlacementControl control {[cancelled] { return cancelled->load(); }, {}};
			const detail::PlacementControlScope scope(control);
			auto& preview = draft->preview;
			try {
				LevelBrushAssembleOutline outline;
				QString error;
				if (!levelBrushAssembleOutline(draft->points, &outline, &error)) {
					preview.status = LevelBrushAssemblePreview::Status::Refused;
					preview.shape = outline.shape;
					preview.message = QCoreApplication::translate("LevelBrushAssemble", "No brush: %1").arg(error);
					return;
				}
				preview.shape = outline.shape;
				if (outline.shape != LevelBrushAssembleShape::Volume) {
					preview.status = LevelBrushAssemblePreview::Status::Empty;
					preview.message = outline.shape == LevelBrushAssembleShape::Polygon
						? QCoreApplication::translate("LevelBrushAssemble",
							  "The points lie on one plane. Shift-drag the outline to extrude it, or add a point off the plane.")
						: QCoreApplication::translate("LevelBrushAssemble",
							  "The points lie on one line. Add points around it until they enclose a volume.");
					return;
				}
				detail::placementCancellationCheckpoint();
				if (!addLevelMapBrushFromPoints(&draft->document, {draft->points, draft->material}, &draft->report, &error)) {
					preview.status = LevelBrushAssemblePreview::Status::Refused;
					preview.message = QCoreApplication::translate("LevelBrushAssemble", "No brush: %1").arg(error);
					return;
				}
				preview.status = LevelBrushAssemblePreview::Status::Ready;
				preview.edges = outline.edges;
				preview.corners = draft->report.corners;
				preview.faces = draft->report.faces;
				preview.message = QCoreApplication::translate("LevelBrushAssemble",
					"Checked brush in %3. Corners: %1. Faces: %2. Enter creates it; Escape clears the points.")
									  .arg(draft->report.corners)
									  .arg(draft->report.faces)
									  .arg(draft->material);
			} catch (const detail::PlacementCancelled&) {
				preview = {LevelBrushAssemblePreview::Status::Refused, preview.shape, {}, 0, 0,
					QCoreApplication::translate("LevelBrushAssemble", "Brush check cancelled.")};
			}
		});
		connect(m_thread, &QThread::finished, this, [this, draft, serial] {
			m_thread->deleteLater();
			m_thread = nullptr;
			if (serial == m_serial && completed) {
				completed(std::move(*draft));
			}
			start();
		});
		m_thread->start();
	}
	quint64 m_serial = 0;
	QThread* m_thread = nullptr;
	std::optional<AssembleDraft> m_pending;
	std::shared_ptr<std::atomic_bool> m_cancelled;
};
} // namespace

void ApplicationShell::registerLevelBrushAssembleCommands()
{
	StudioCommandRegistration tool;
	tool.commandId = QStringLiteral("map.assembleBrushTool");
	tool.group = StudioCommandGroup::Edit;
	tool.label = tr("Assemble Brush Tool");
	tool.statusTip = tr("Click surfaces or the grid to collect points, drag faces, Shift-drag a flat outline to extrude it, and press Enter for "
						"their convex brush.");
	tool.iconName = QStringLiteral("vertex-mode");
	tool.checkable = true;
	tool.menuSection = tr("Brush Shapes");
	tool.handler = [this]() {
		setMode(StudioMode::Levels);
		setLevelBrushAssembleTool(!levelBrushAssembleActive());
	};
	m_commands->registerCommand(tool);

	StudioCommandRegistration create;
	create.commandId = QStringLiteral("map.createAssembledBrush");
	create.group = StudioCommandGroup::Edit;
	create.label = tr("Create Assembled Brush");
	create.statusTip = tr("Add the convex brush enclosing the Assemble Brush tool's points, in the current material, as one undo step.");
	create.iconName = QStringLiteral("cube");
	create.menuSection = tr("Brush Shapes");
	create.handler = [this]() {
		if (m_levelBrushAssembleCommit) {
			m_levelBrushAssembleCommit();
		}
	};
	m_commands->registerCommand(create);
}

bool ApplicationShell::levelBrushAssembleActive() const
{
	return m_levelMapViewport && m_levelMapViewport->brushAssembleTool();
}

void ApplicationShell::refreshLevelBrushAssembleCommands()
{
	if (!m_commands) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	const bool active = levelBrushAssembleActive();
	m_commands->setEnabled(QStringLiteral("map.assembleBrushTool"), quake);
	if (QAction* tool = m_commands->action(QStringLiteral("map.assembleBrushTool"))) {
		const QSignalBlocker blocker(tool);
		tool->setChecked(active);
	}
	m_commands->setEnabled(QStringLiteral("map.createAssembledBrush"),
		quake && active && m_levelMapViewport->brushAssembleState().points.size() >= kLevelBrushPointsMinimum);
}

void ApplicationShell::setLevelBrushAssembleTool(bool enabled)
{
	if (!m_levelMapViewport || !m_levelMap3D) {
		return;
	}
	const bool quake = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if (enabled && !quake) {
		statusBar()->showMessage(tr("Open a Quake-family .map to assemble brushes."));
		refreshLevelBrushAssembleCommands();
		return;
	}
	const bool was = levelBrushAssembleActive();
	if (enabled) {
		// One tool at a time, as TrenchBroom's exclusive tool groups have it.
		if (m_levelMapViewport->clipMode()) {
			setLevelMapClipMode(false);
		}
		setLevelComponentMode(0);
		setLevelShearMode(false);
		setLevelRegionDrawMode(false);
		setLevelMaterialTool(0);
		for (auto* view : std::as_const(m_levelPlanViews)) {
			view->setDrawMode(false);
			view->setShapeDrawMode(false);
		}
	}
	for (auto* view : std::as_const(m_levelPlanViews)) {
		const QSignalBlocker blocker(view);
		view->setBrushAssembleTool(enabled);
	}
	{
		const QSignalBlocker blocker(m_levelMap3D);
		m_levelMap3D->setBrushAssembleTool(enabled);
	}
	if (m_levelBrushAssembleRefresh) {
		m_levelBrushAssembleRefresh();
	}
	refreshLevelBrushAssembleCommands();
	refreshLevelToolShelf();
	if (was != enabled) {
		statusBar()->showMessage(enabled ? tr("Assemble Brush: click surfaces or the grid to add points in any view; Enter creates the brush.")
										 : tr("Assemble Brush tool off."));
	}
}

void ApplicationShell::connectLevelBrushAssemble()
{
	struct Source {
		LevelBrushAssembleState state;
		bool broadcasting = false;
		bool applyWhenReady = false;
		std::optional<AssembleDraft> prepared;
		// What the check was made against; the committer refuses a stale map.
		std::function<bool(const LevelMapDocument&, QString*)> publish;
		quint64 loadSerial = 0;
		quint64 revision = 0;
		quint64 history = 0;
		int savedDepth = 0;
		QVector<LevelMapSelectionRef> selection;
		QString activeNode;
		QString material;
		MapBrushGeometryCache cache;
	};
	const auto source = std::make_shared<Source>();
	auto* worker = new AssembleWorker(this);
	const auto active = [this] { return levelBrushAssembleActive(); };
	const auto show = [this](const LevelBrushAssemblePreview& preview) {
		for (auto* view : std::as_const(m_levelPlanViews)) {
			view->setBrushAssemblePreview(preview);
		}
		m_levelMap3D->setBrushAssemblePreview(preview);
		refreshLevelBrushAssembleCommands();
	};
	const auto propagate = [this, source](const LevelBrushAssembleState& state) {
		const QScopedValueRollback guard(source->broadcasting, true);
		for (auto* view : std::as_const(m_levelPlanViews)) {
			view->setBrushAssembleState(state);
		}
		m_levelMap3D->setBrushAssembleState(state);
	};
	const auto current = [this, source] {
		const auto& now = m_levelMapDocument;
		return source->loadSerial == m_levelMapLoadSerial && source->revision == now.revision && source->history == now.historyRevision
			&& source->savedDepth == now.savedUndoDepth && source->selection == now.selection && source->activeNode == now.activeSceneNode
			&& source->material == levelBrushMaterial();
	};
	const auto check = [this, source, worker, show] {
		source->prepared.reset();
		worker->reset();
		const auto& now = m_levelMapDocument;
		source->loadSerial = m_levelMapLoadSerial;
		source->revision = now.revision;
		source->history = now.historyRevision;
		source->savedDepth = now.savedUndoDepth;
		source->selection = now.selection;
		source->activeNode = now.activeSceneNode;
		source->material = levelBrushMaterial();
		source->publish = levelPlacementCommitter();
		const int count = static_cast<int>(source->state.points.size());
		if (count < kLevelBrushPointsMinimum) {
			LevelBrushAssemblePreview preview;
			if (count > 0) {
				preview.message = tr("%n point(s). A brush needs at least four points that enclose a volume.", nullptr, count);
			}
			show(preview);
			return;
		}
		LevelBrushAssemblePreview pending;
		pending.status = LevelBrushAssemblePreview::Status::Pending;
		pending.message = tr("Checking the brush… Enter waits for the result; Escape clears the points.");
		show(pending);
		AssembleDraft draft;
		draft.document = m_levelMapDocument;
		draft.points = source->state.points;
		draft.material = source->material;
		worker->request(std::move(draft));
	};
	const auto changed = [source, propagate, check, active](const LevelBrushAssembleState& state) {
		if (source->broadcasting || !active()) {
			return;
		}
		source->state = state;
		source->applyWhenReady = false;
		propagate(state);
		check();
	};
	for (auto* view : std::as_const(m_levelPlanViews)) {
		connect(view, &MapViewport::brushAssembleStateChanged, this, [view, changed] { changed(view->brushAssembleState()); });
		connect(view, &MapViewport::brushAssembleToolChanged, this, &ApplicationShell::setLevelBrushAssembleTool);
		connect(view, &MapViewport::brushAssembleRequested, this, [this] { m_levelBrushAssembleCommit(); });
	}
	connect(m_levelMap3D, &ModelViewport::brushAssembleStateChanged, this, [this, changed] { changed(m_levelMap3D->brushAssembleState()); });
	connect(m_levelMap3D, &ModelViewport::brushAssembleToolChanged, this, &ApplicationShell::setLevelBrushAssembleTool);
	connect(m_levelMap3D, &ModelViewport::brushAssembleRequested, this, [this] { m_levelBrushAssembleCommit(); });
	m_levelBrushAssembleRefresh = [source, worker, check, current, active, propagate, show] {
		if (!active() || source->state.points.isEmpty()) {
			// Turning the tool on or off starts from no points in every view.
			const bool idle = source->state.points.isEmpty() && !source->prepared && !source->applyWhenReady;
			worker->reset();
			source->prepared.reset();
			source->applyWhenReady = false;
			if (!idle) {
				source->state = {};
				propagate(source->state);
				show({});
			}
			return;
		}
		if (!current()) {
			// The points stay; the map they would join changed.
			check();
		}
	};
	m_levelBrushAssembleCommit = [this, source, worker, check, current, active, propagate, show] {
		if (!active()) {
			return;
		}
		const int count = static_cast<int>(source->state.points.size());
		if (count < kLevelBrushPointsMinimum) {
			statusBar()->showMessage(tr("Add at least four points that enclose a volume, then press Enter."), 6000);
			return;
		}
		if (!current()) {
			check();
			source->applyWhenReady = true;
			return;
		}
		if (!source->prepared) {
			source->applyWhenReady = true;
			statusBar()->showMessage(tr("Checking the brush; it is created when the check finishes."), 4000);
			return;
		}
		if (source->prepared->preview.status != LevelBrushAssemblePreview::Status::Ready) {
			statusBar()->showMessage(source->prepared->preview.message, 6000);
			return;
		}
		QString error;
		if (!source->publish || !source->publish(source->prepared->document, &error)) {
			// Something the check depended on changed; check again, then create.
			check();
			source->applyWhenReady = true;
			return;
		}
		const LevelBrushPointsReport report = source->prepared->report;
		source->prepared.reset();
		source->applyWhenReady = false;
		worker->reset();
		// TrenchBroom clears the tool's points once the brush exists.
		source->state = {};
		propagate(source->state);
		show({});
		recordActivity(tr("Level map brush assembled"), QStringLiteral("brush:%1").arg(report.brushId), QStringLiteral("level-map"),
			OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(
			tr("Created brush:%1. Corners: %2. Faces: %3. Undo removes it.").arg(report.brushId).arg(report.corners).arg(report.faces), 6000);
	};
	worker->completed = [this, source, current, show](AssembleDraft draft) {
		if (!(LevelBrushAssembleState {draft.points} == source->state) || !current()) {
			m_levelBrushAssembleRefresh();
			return;
		}
		source->prepared = std::move(draft);
		show(source->prepared->preview);
		if (source->applyWhenReady) {
			source->applyWhenReady = false;
			m_levelBrushAssembleCommit();
		}
	};
	m_levelMap3D->setBrushAssembleSurfaceResolver([this, source](int triangle, LevelBrushAssembleSurface* surface) {
		if (!surface || triangle < 0 || triangle >= m_levelMap3DOwners.size()) {
			return false;
		}
		const auto owner = m_levelMap3DOwners[triangle];
		const int face = m_levelMap3DFaces.value(triangle, -1);
		if (owner.kind != LevelMapSelectionKind::QuakeBrush || face < 0) {
			return false;
		}
		const auto brush = std::find_if(m_levelMapDocument.brushes.cbegin(), m_levelMapDocument.brushes.cend(),
			[&owner](const LevelMapBrush& candidate) { return candidate.id == owner.objectId; });
		if (brush == m_levelMapDocument.brushes.cend()) {
			return false;
		}
		const auto geometry = source->cache.resolve(*brush);
		if (!geometry.solved) {
			return false;
		}
		for (const auto& polygon : geometry.faces) {
			if (polygon.faceIndex == face && polygon.plane.valid && polygon.points.size() >= 3) {
				*surface = {polygon.plane, polygon.points};
				return true;
			}
		}
		return false;
	});
}
} // namespace vibestudio
