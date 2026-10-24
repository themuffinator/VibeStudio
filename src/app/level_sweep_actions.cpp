#include "app/application_shell.h"
#include "app/level_sweep_canvas.h"
#include "app/level_sweep_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "app/studio_layout.h"
#include <QStatusBar>
#include <QTimer>
namespace vibestudio
{
void ApplicationShell::sweepLevelFacesFromUi(bool inViews)
{
	if (m_levelSweepDialog) {
		m_levelSweepDialog->show();
		m_levelSweepDialog->raise();
		return;
	}
	if (!m_levelMapViewport) {
		return;
	}
	LevelSweepRequest request;
	request.faces = m_levelMapViewport->pickedComponents();
	request.texture = levelBrushMaterial();
	if (request.faces.isEmpty() || std::any_of(request.faces.cbegin(), request.faces.cend(), [](const auto& set) {
		    return set.object.kind != LevelMapSelectionKind::QuakeBrush || set.kind != LevelBrushComponent::Face;
	    })) {
		statusBar()->showMessage(tr("Pick brush faces before sweeping."), 6000);
		return;
	}
	for (auto* view : std::as_const(m_levelPlanViews)) {
		view->finishComponentMove(false);
	}
	if (m_levelMap3D) {
		m_levelMap3D->finishComponentMove(false);
	}
	auto archive =
	    m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	auto dialog = std::make_unique<LevelSweepDialog>(m_levelMapDocument, request, archive, activePaletteId(), this);
	const auto publish = levelPlacementCommitter();
	dialog->setApplyHandler([this, publish](const LevelMapDocument& candidate, QString* error) {
		if (m_levelSweepGizmo && m_levelSweepGizmo->dragging()) {
			if (error) {
				*error = tr("Finish the destination drag before creating the sweep.");
			}
			return false;
		}
		if (!publish(candidate, error)) {
			return false;
		}
		setLevelComponentMode(0);
		refreshLevelMapWorkbench();
		recordActivity(tr("Swept level faces"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
		               tr("Unsaved map edit"));
		statusBar()->showMessage(tr("Sweep created. Source faces remain; Undo removes the sweep."), 6000);
		return true;
	});
	if (!inViews) {
		dialog->exec();
		return;
	}
	LevelSweepSource source;
	QString error;
	if (!levelSweepSource(m_levelMapDocument, request.faces, &source, &error)) {
		statusBar()->showMessage(error, 6000);
		return;
	}
	m_levelSweepDialog = dialog.release();
	m_levelSweepDialog->setAttribute(Qt::WA_DeleteOnClose);
	m_levelSweepDialog->setViewportMode();
	m_levelSweepGizmo = new LevelSweepGizmo(source, request, m_levelSweepDialog);
	auto* tool = m_levelSweepGizmo.data();
	tool->setSteps(m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0, m_settings.levelComponentTransformSteps()[0]);
	connect(tool, &LevelSweepGizmo::requestChanged, m_levelSweepDialog.data(), &LevelSweepDialog::setDestination);
	connect(m_levelSweepDialog.data(), &LevelSweepDialog::requestChanged, tool, &LevelSweepGizmo::setRequest);
	connect(m_levelSweepDialog.data(), &LevelSweepDialog::previewChanged, tool, &LevelSweepGizmo::setPreview);
	connect(m_levelSweepDialog.data(), &LevelSweepDialog::scaleRequested, this, &ApplicationShell::stepLevelSweepToolScale);
	connect(tool, &LevelSweepGizmo::changed, this, [this] {
		refreshLevelGeometryCommands();
		if (m_levelMapHover) {
			m_levelMapHover->setText(levelSweepReadout());
		}
	});
	const auto preferences = m_settings.accessibilityPreferences();
	const bool highContrast = preferences.theme == StudioTheme::HighContrastDark || preferences.theme == StudioTheme::HighContrastLight;
	const auto bind = [&](QWidget* view, std::function<LevelComponentGizmoFrame()> frame) {
		auto* canvas = new LevelSweepCanvas(view, tool, std::move(frame), highContrast);
		const auto overrides = m_commands->userShortcuts();
		for (const auto& entry : QList<QPair<QString, LevelSweepCanvas::Command>>{
		         {QStringLiteral("map.sweepApply"), LevelSweepCanvas::Command::Apply},
		         {QStringLiteral("map.sweepDecreaseScale"), LevelSweepCanvas::Command::DecreaseScale},
		         {QStringLiteral("map.sweepIncreaseScale"), LevelSweepCanvas::Command::IncreaseScale}}) {
			auto keys = canvas->commandKeys(entry.second);
			if (overrides.contains(entry.first)) {
				keys.clear();
				for (const auto& key : overrides.value(entry.first)) {
					keys << QKeySequence(key, QKeySequence::PortableText);
				}
			} else {
				// An explicitly assigned user key also wins over a local default.
				for (const auto& assigned : overrides) {
					for (const auto& key : assigned) {
						keys.removeAll(QKeySequence(key, QKeySequence::PortableText));
					}
				}
			}
			canvas->setCommandKeys(entry.second, keys);
		}
		connect(canvas, &LevelSweepCanvas::applyRequested, this, &ApplicationShell::commitLevelSweepTool);
		connect(canvas, &LevelSweepCanvas::leaveRequested, m_levelSweepDialog.data(), &QDialog::reject);
		connect(m_levelSweepDialog.data(), &QDialog::finished, canvas, &LevelSweepCanvas::stop);
	};
	for (auto* view : std::as_const(m_levelPlanViews)) {
		bind(view, [view] {
			auto frame = view->componentGizmoFrame();
			const auto direction = frame.viewDirection;
			frame.rotationAxis = std::abs(direction.x) > .5 ? 0 : std::abs(direction.y) > .5 ? 1 : 2;
			return frame;
		});
	}
	if (m_levelMap3D) {
		bind(m_levelMap3D, [this, weak = QPointer<LevelSweepGizmo>(tool)] {
			return m_levelMap3D->componentGizmoFrame(weak ? weak->centre() : LevelMapVec3{});
		});
	}
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto selection = levelComponentSelectionToken();
	const auto package = m_packageStaging.revision(), reload = m_levelPreviewReload;
	const auto objectSelection = m_levelMapDocument.selection;
	const auto activeNode = m_levelMapDocument.activeSceneNode;
	const auto palette = activePaletteId();
	const auto path = m_levelMapDocument.sourcePath;
	const auto hash = m_levelMapDocument.sourceContentHash;
	const auto savedDepth = m_levelMapDocument.savedUndoDepth;
	const auto history = m_levelMapDocument.historyRevision;
	const auto shortcuts = m_settings.userShortcuts();
	const auto profile = m_settings.selectedEditorProfileId();
	auto* contextTimer = new QTimer(m_levelSweepDialog);
	contextTimer->setInterval(100);
	connect(contextTimer, &QTimer::timeout, m_levelSweepDialog,
	        [this, load, revision, selection, package, reload, objectSelection, activeNode, palette, path, hash, savedDepth, history, shortcuts,
	         profile] {
		        if (!m_levelSweepDialog) {
			        return;
		        }
		        if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || history != m_levelMapDocument.historyRevision ||
		            selection != levelComponentSelectionToken() || package != m_packageStaging.revision() ||
		            reload != m_levelPreviewReload || objectSelection != m_levelMapDocument.selection ||
		            activeNode != m_levelMapDocument.activeSceneNode || palette != activePaletteId() ||
		            path != m_levelMapDocument.sourcePath || hash != m_levelMapDocument.sourceContentHash ||
		            savedDepth != m_levelMapDocument.savedUndoDepth || shortcuts != m_settings.userShortcuts() ||
		            profile != m_settings.selectedEditorProfileId()) {
			        m_levelSweepDialog->reject();
			        statusBar()->showMessage(tr("Sweep cancelled because the map, selection, package or editor controls changed."), 6000);
			        return;
		        }
		        if (m_levelSweepGizmo && m_levelMapViewport) {
			        m_levelSweepGizmo->setSteps(m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0,
			                                    m_settings.levelComponentTransformSteps()[0]);
		        }
	        });
	contextTimer->start();
	connect(m_levelSweepDialog.data(), &QDialog::finished, this, [this, contextTimer](int result) {
		contextTimer->stop();
		m_levelSweepDialog.clear();
		m_levelSweepGizmo.clear();
		refreshLevelMapWorkbench();
		refreshLevelGeometryCommands();
		if(result==QDialog::Rejected) { statusBar()->showMessage(tr("Sweep cancelled."),6000); }
	});
	m_levelSweepDialog->show();
	refreshLevelComponentGizmos();
	refreshLevelGeometryCommands();
	statusBar()->showMessage(tr("Sweep destination controls are active. Create Sweep applies the checked preview."),6000);
}
} // namespace vibestudio

namespace vibestudio
{
bool ApplicationShell::levelSweepToolActive() const
{
	return !m_levelSweepDialog.isNull();
}
QString ApplicationShell::levelSweepReadout(const QString& fallback) const
{
	return m_levelSweepGizmo ? m_levelSweepGizmo->summary() : fallback;
}
bool ApplicationShell::levelSweepToolReady() const
{
	return m_levelSweepDialog && m_levelSweepDialog->isReady() && m_levelSweepGizmo && !m_levelSweepGizmo->dragging();
}
void ApplicationShell::toggleLevelSweepTool()
{
	if (m_levelSweepDialog) {
		m_levelSweepDialog->reject();
	} else {
		sweepLevelFacesFromUi(true);
	}
}
void ApplicationShell::stepLevelSweepToolScale(int direction)
{
	if (m_levelSweepGizmo) {
		m_levelSweepGizmo->scaleStep(direction);
	}
}
void ApplicationShell::commitLevelSweepTool()
{
	if (levelSweepToolReady()) {
		m_levelSweepDialog->accept();
	}
}
} // namespace vibestudio
