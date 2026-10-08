#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/level_surface_worker.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>

namespace vibestudio {
QWidget* ApplicationShell::buildLevelSurfaceTools()
{
	m_levelSurfaceTools = new LevelSurfaceTools;
	m_levelSurfaceTools->bindCommands(*m_commands);
	m_levelSurfaceTools->targetChanged = [this] { cancelLevelSurfaceEdits(); refreshLevelSurfaceTools(); };
	m_levelSurfaceTools->cancelRequested = [this] { cancelLevelSurfaceEdits(); };
	connect(m_entityInspector, &QTreeWidget::currentItemChanged, this, [this] {
		// The inspector clears and restores its current row during a refresh.
		QTimer::singleShot(0, this, [this] { refreshLevelSurfaceTools(); });
	});
	m_levelSurfaceDebounce = new QTimer(this);
	m_levelSurfaceDebounce->setSingleShot(true); m_levelSurfaceDebounce->setInterval(20);
	connect(m_levelSurfaceDebounce, &QTimer::timeout, this, &ApplicationShell::startLevelSurfaceBatch);
	m_levelSurfaceWorker = new LevelSurfaceWorker(this);
	m_levelSurfaceWorker->completed = [this](LevelSurfaceResult result) {
		if (result.strokeResult) { completeLevelSurfaceStroke(std::move(result)); return; }
		if (result.token != m_levelSurfaceToken) { startLevelSurfaceBatch(); refreshLevelSurfaceTools(); return; }
		m_levelSurfacePasting = false;
		if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) {
			cancelLevelSurfaceEdits();
			m_levelSurfaceTools->setStatus(tr("The map, selection, target or package changed. Pending surface adjustments were cancelled."), false);
			if (result.pasted) { showLevelMaterialPaintMessage(tr("The map context changed. Pending surface paste was cancelled.")); }
			return;
		}
		if (result.cancelled || !result.error.isEmpty()) {
			cancelLevelSurfaceEdits();
			m_levelSurfaceTools->setStatus(result.cancelled ? tr("Pending surface adjustments cancelled.") : result.error, false);
			if (result.pasted) { showLevelMaterialPaintMessage(result.cancelled ? tr("Pending surface paste cancelled.") : result.error); }
			statusBar()->showMessage(result.error, 6000); return;
		}
		m_levelSurfaceSizes = std::move(result.textureSizes);
		const bool advancedClipboard = result.nextClipboard.ready();
		if (advancedClipboard) {
			m_levelSurfaceClipboard = std::move(result.nextClipboard);
			m_levelSurfaceClipboardArchive = std::move(result.nextClipboardArchive);
			m_levelSurfaceClipboardStaging = std::move(result.nextClipboardStaging);
			m_levelSurfaceClipboardPalette = std::move(result.nextClipboardPalette);
			m_levelSurfaceClipboardEngineFamily = std::move(result.nextClipboardEngineFamily);
			m_levelSurfaceClipboardFormat = result.nextClipboardFormat;
		}
		const int changedSurfaces = result.changedFaces + result.changedPatches;
		if (changedSurfaces > 0) {
			m_levelSurfacePublishing = true;
			m_levelMapDocument = std::move(result.document);
			recordActivity(result.pasted ? tr("Level surfaces pasted") : tr("Level surfaces adjusted"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"),
				OperationState::Warning, tr("Unsaved map edit"));
			refreshLevelMapWorkbench();
			m_levelSurfacePublishing = false;
		}
		if (!m_levelSurfacePending.isEmpty()) {
			m_levelSurfaceCurrent = levelSurfaceContextGuard(); startLevelSurfaceBatch();
		} else {
			m_levelSurfaceCurrent = {};
			refreshLevelSurfaceTools();
			auto message = changedSurfaces > 0
				? (result.pasted ? tr("Pasted %n surface(s). The batch is one undo step.", nullptr, changedSurfaces)
					: tr("Adjusted %n brush face(s). The batch is one undo step.", nullptr, result.changedFaces))
				: tr("Surface mapping is unchanged; no undo step was added.");
			if (result.convertedFaces > 0) {
				message = tr("Pasted surfaces and converted %n classic face(s) throughout the map to Valve 220 in one undo step. Unpasted surfaces retain their materials and UVs.", nullptr, result.convertedFaces);
			}
			if (advancedClipboard) { message += QLatin1Char(' ') + tr("The wrapped face is now the source for the next paste."); }
			if (result.edgeOnFaces > 0) { message += QLatin1Char(' ') + tr("Radiant projection left %n face(s) edge-on to the copied mapping. Use Radiant values for face-relative alignment.", nullptr, result.edgeOnFaces); }
			m_levelSurfaceTools->setStatus(message, false); statusBar()->showMessage(message, 6000);
			if (result.pasted) { showLevelMaterialPaintMessage(message); }
		}
	};
	return m_levelSurfaceTools;
}

void ApplicationShell::registerLevelSurfaceCommands()
{
	const QStringList labels{tr("Shift Texture U −"), tr("Shift Texture U +"), tr("Shift Texture V −"), tr("Shift Texture V +"),
		tr("Rotate Texture −"), tr("Rotate Texture +"), tr("Shrink Texture U"), tr("Grow Texture U"),
		tr("Shrink Texture V"), tr("Grow Texture V"), tr("Fit Texture 1 × 1"), tr("Centre Texture")};
	const auto ids = LevelSurfaceTools::commandIds();
	for (int i = 0; i < ids.size(); ++i) {
		StudioCommandRegistration command;
		command.commandId = ids[i]; command.label = labels[i]; command.group = StudioCommandGroup::Edit;
		command.menuSection = tr("Surface Adjustments"); command.iconName = QStringLiteral("image");
		command.statusTip = tr("Adjust brush texture mapping using the target and step values in Levels > Surfaces.");
		command.handler = [this, id = ids[i]] {
			if (!m_levelSurfaceTools) { return; }
			setMode(StudioMode::Levels);
			showLevelSidebarPage(QStringLiteral("surfaces"));
			QString error;
			if (!queueLevelSurfaceEdit(m_levelSurfaceTools->request(id), &error)) {
				m_levelSurfaceTools->setStatus(error, levelSurfaceEditsPending()); statusBar()->showMessage(error, 6000);
			}
		};
		m_commands->registerCommand(command);
	}
	for (bool paste : {false, true}) {
		StudioCommandRegistration command;
		command.commandId = paste ? QStringLiteral("map.pasteSurface") : QStringLiteral("map.copySurface");
		command.label = paste ? tr("Paste Surface") : tr("Copy Surface");
		command.group = StudioCommandGroup::Edit; command.menuSection = tr("Surface Clipboard"); command.iconName = QStringLiteral("image");
		command.statusTip = paste ? tr("Paste the copied material, mapping and flags onto the Surfaces target.") : tr("Copy the inspected brush face's material, mapping and flags.");
		command.handler = [this, paste] {
			if (!m_levelSurfaceTools) { return; }
			setMode(StudioMode::Levels); showLevelSidebarPage(QStringLiteral("surfaces"));
			QString error; bool done = false;
			if (paste) {
				auto targets = levelSurfacePasteSelectionTargets(m_levelMapDocument);
				if (m_levelSurfaceTools->singleFace()) {
					const auto face = inspectedLevelMapSurface(); const LevelMaterialTarget target{LevelMaterialKind::BrushFace, face.brushId, face.faceIndex};
					targets = targets.contains(target) ? QVector<LevelMaterialTarget>{target} : QVector<LevelMaterialTarget>{};
				}
				done = pasteLevelSurfaceTargets(targets, m_levelSurfaceTools->pasteOptions(), &error);
			} else { done = copyLevelSurfaceSettings(inspectedLevelMapSurface(), &error); }
			if (!done) { m_levelSurfaceTools->setStatus(error, levelSurfaceEditsPending()); statusBar()->showMessage(error, 6000); }
		};
		m_commands->registerCommand(command);
	}
}

std::function<bool()> ApplicationShell::levelSurfaceContextGuard() const
{
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto path = m_levelMapDocument.sourcePath, output = m_levelMapDocument.outputPath;
	const auto hash = m_levelMapDocument.sourceContentHash;
	const auto selection = m_levelMapDocument.selection;
	const auto scene = m_levelMapDocument.scene;
	const auto active = m_levelMapDocument.activeSceneNode;
	const auto saved = m_levelMapDocument.savedUndoDepth;
	const auto package = m_packageStaging.revision(), reload = m_levelPreviewReload;
	const auto palette = activePaletteId();
	const bool single = m_levelSurfaceTools->singleFace();
	const auto inspected = inspectedLevelMapSurface();
	return [this, load, revision, path, output, hash, selection, scene, active, saved, package, reload, palette, single, inspected] {
		return load == m_levelMapLoadSerial && revision == m_levelMapDocument.revision && path == m_levelMapDocument.sourcePath
			&& output == m_levelMapDocument.outputPath && hash == m_levelMapDocument.sourceContentHash
			&& selection == m_levelMapDocument.selection && scene == m_levelMapDocument.scene && active == m_levelMapDocument.activeSceneNode
			&& saved == m_levelMapDocument.savedUndoDepth && package == m_packageStaging.revision()
			&& reload == m_levelPreviewReload && palette == activePaletteId() && single == m_levelSurfaceTools->singleFace()
			&& (!single || inspected == inspectedLevelMapSurface());
	};
}

void ApplicationShell::refreshLevelSurfaceTools()
{
	if (!m_levelSurfaceTools || !m_commands) { return; }
	if (!m_levelSurfacePublishing && m_levelSurfaceCurrent && !m_levelSurfaceCurrent()) {
		cancelLevelSurfaceEdits();
		m_levelSurfaceTools->setStatus(tr("The map, selection, target or package changed. Pending surface adjustments were cancelled."), false);
	}
	const auto inspected = inspectedLevelMapSurface();
	const auto pasteTargets = levelSurfacePasteSelectionTargets(m_levelMapDocument);
	int faces = 0, patches = 0; bool includesInspected = false;
	for (const auto& target : pasteTargets) {
		patches += target.kind == LevelMaterialKind::Patch;
		if (target.kind == LevelMaterialKind::BrushFace) {
			++faces; includesInspected |= target.objectId == inspected.brushId && target.faceIndex == inspected.faceIndex;
		}
	}
	if (m_levelSurfaceTools->singleFace()) { faces = includesInspected ? 1 : 0; }
	m_levelSurfaceTools->setTargetSummary(faces, inspected, patches);
	const bool editable = (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map)
		&& faces > 0 && faces <= 16384;
	for (const auto& id : LevelSurfaceTools::commandIds()) { m_commands->setEnabled(id, editable); }
	m_commands->setEnabled(QStringLiteral("map.copySurface"), inspected.brushId >= 0 && inspected.faceIndex >= 0 && !levelSurfaceEditsPending());
	const bool pasteEditable = m_levelSurfaceTools->singleFace() ? editable : !pasteTargets.isEmpty() && pasteTargets.size() <= 16384;
	m_commands->setEnabled(QStringLiteral("map.pasteSurface"), pasteEditable && m_levelSurfaceClipboard.ready() && !levelSurfaceEditsPending());
	m_levelSurfaceTools->setClipboardSummary(m_levelSurfaceClipboard);
}

bool ApplicationShell::levelSurfaceEditsPending() const
{
	return m_levelSurfaceCurrent || (m_levelSurfaceWorker && m_levelSurfaceWorker->busy());
}

void ApplicationShell::cancelLevelSurfaceEdits()
{
	const bool wasStroke = bool(m_levelSurfaceStroke);
	cancelLevelSurfaceStroke();
	const bool wasPasting = m_levelSurfacePasting;
	++m_levelSurfaceToken; m_levelSurfacePending.clear(); m_levelSurfaceCurrent = {}; m_levelSurfaceSizes.clear();
	m_levelSurfacePasting = false;
	if (m_levelSurfaceDebounce) { m_levelSurfaceDebounce->stop(); }
	if (m_levelSurfaceWorker) { m_levelSurfaceWorker->cancel(); }
	const auto message = wasStroke ? tr("The complete surface stroke was discarded. Earlier edits remain in undo history.")
		: tr("Pending surface adjustments cancelled. Completed batches remain in undo history.");
	if (m_levelSurfaceTools) { m_levelSurfaceTools->setStatus(message, false); }
	if (wasPasting) { showLevelMaterialPaintMessage(message); }
	refreshLevelSurfaceTools();
}

bool ApplicationShell::queueLevelSurfaceEdit(const LevelSurfaceRequest& request, QString* error)
{
	if (error) { error->clear(); }
	if (!m_levelSurfaceTools || !m_levelSurfaceWorker) { return false; }
	// Repeated commands in the same burst reuse the captured targets. A
	// large map must not be rescanned for every auto-repeat event.
	if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) { refreshLevelSurfaceTools(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (m_levelSurfacePasting) { return fail(tr("Wait for the surface paste to finish or cancel it before queuing adjustments.")); }
	if (m_levelSurfacePending.size() >= 64) { return fail(tr("The surface queue is full. Wait for the pending batch or cancel it.")); }
	if (!m_levelSurfaceCurrent) {
		m_levelSurfaceFaces = levelMapSelectedSurfaces(m_levelMapDocument);
		if (m_levelSurfaceTools->singleFace()) {
			const auto inspected = inspectedLevelMapSurface();
			m_levelSurfaceFaces = m_levelSurfaceFaces.contains(inspected) ? QVector<LevelSurfaceFace>{inspected} : QVector<LevelSurfaceFace>{};
		}
		if (m_levelSurfaceFaces.isEmpty() || m_levelSurfaceFaces.size() > 16384) { return fail(tr("Select between 1 and 16,384 brush faces for surface alignment.")); }
		m_levelSurfaceCurrent = levelSurfaceContextGuard(); m_levelSurfaceSizes.clear(); ++m_levelSurfaceToken;
	}
	m_levelSurfacePending << request;
	m_levelSurfaceTools->setStatus(tr("%n adjustment(s) queued · Preparing surfaces…", nullptr, static_cast<int>(m_levelSurfacePending.size())), true);
	if (!m_levelSurfaceDebounce->isActive()) { m_levelSurfaceDebounce->start(); }
	return true;
}

void ApplicationShell::startLevelSurfaceBatch()
{
	if (m_levelSurfacePending.isEmpty() || m_levelSurfaceWorker->busy()) { return; }
	if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) { refreshLevelSurfaceTools(); return; }
	auto archive = m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) { archive = std::make_shared<const PackageArchive>(m_packageArchive); }
	LevelSurfaceWork work;
	work.document = m_levelMapDocument; work.faces = m_levelSurfaceFaces; work.adjustments = std::move(m_levelSurfacePending);
	work.archive = std::move(archive); work.palette = activePaletteId(); work.textureSizes = m_levelSurfaceSizes;
	if (m_packageStaging.isLoaded()) { work.staging = std::make_shared<const PackageStagingModel>(m_packageStaging); }
	work.token = m_levelSurfaceToken;
	m_levelSurfacePending.clear();
	m_levelSurfaceTools->setStatus(tr("Preparing %n surface adjustment(s)…", nullptr, static_cast<int>(work.adjustments.size())), true);
	m_levelSurfaceWorker->start(std::move(work));
}

bool ApplicationShell::copyLevelSurfaceSettings(LevelSurfaceFace face, QString* error)
{
	if (error) { error->clear(); }
	if (levelSurfaceEditsPending() || (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->isDrawingBrush()
		|| m_levelMap3D->isResizingSelection() || m_levelMap3D->isMovingSelection()))) {
		if (error) { *error = tr("Finish or cancel the pending level edit before copying a surface."); } return false;
	}
	const bool copied = copyLevelSurface(m_levelMapDocument, face, &m_levelSurfaceClipboard, error);
	m_levelSurfaceClipboardArchive.reset();
	m_levelSurfaceClipboardStaging.reset();
	if (copied) {
		// Copy implicitly shared state now; build its package index only when a
		// surface worker needs dimensions. Sampling must not index a large package.
		if (m_packageStaging.isLoaded()) { m_levelSurfaceClipboardStaging = std::make_shared<const PackageStagingModel>(m_packageStaging); }
		else {
			m_levelSurfaceClipboardArchive = m_packageArchive.snapshotReader();
			if (!m_levelSurfaceClipboardArchive && m_packageArchive.isOpen()) { m_levelSurfaceClipboardArchive = std::make_shared<const PackageArchive>(m_packageArchive); }
		}
		m_levelSurfaceClipboardPalette = activePaletteId();
		m_levelSurfaceClipboardEngineFamily = m_levelMapDocument.engineFamily;
		m_levelSurfaceClipboardFormat = m_levelMapDocument.format;
	}
	refreshLevelSurfaceTools();
	if (!copied) { return false; }
	chooseLevelPaintMaterial(m_levelSurfaceClipboard.material());
	const auto message = tr("Copied %1 · material, mapping and flags.").arg(m_levelSurfaceClipboard.material());
	if (m_levelSurfaceTools) { m_levelSurfaceTools->setStatus(message, false); }
	statusBar()->showMessage(message, 6000); return true;
}
bool ApplicationShell::pasteLevelSurfaceSettings(const QVector<LevelSurfaceFace>& faces, const LevelSurfacePasteOptions& options, QString* error)
{
	QVector<LevelMaterialTarget> targets; targets.reserve(faces.size());
	for (const auto& face : faces) { targets.append({LevelMaterialKind::BrushFace, face.brushId, face.faceIndex}); }
	return pasteLevelSurfaceTargets(targets, options, error);
}
bool ApplicationShell::pasteLevelSurfaceTargets(const QVector<LevelMaterialTarget>& targets, const LevelSurfacePasteOptions& options, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!m_levelSurfaceTools || !m_levelSurfaceWorker) { return fail(tr("Surface tools are unavailable.")); }
	if (!m_levelSurfaceClipboard.ready()) { return fail(tr("Copy a brush surface before pasting.")); }
	if (targets.isEmpty() || targets.size() > 16384) { return fail(tr("Select between 1 and 16,384 surfaces for paste.")); }
	if (levelSurfaceEditsPending() || (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->isDrawingBrush()
		|| m_levelMap3D->isResizingSelection() || m_levelMap3D->isMovingSelection()))) {
		return fail(tr("Finish or cancel the pending level edit before pasting a surface."));
	}
	auto archive = m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) { archive = std::make_shared<const PackageArchive>(m_packageArchive); }
	LevelSurfaceWork work; work.document = m_levelMapDocument; work.pasteTargets = targets;
	work.clipboard = m_levelSurfaceClipboard; work.paste = options; work.archive = std::move(archive); work.palette = activePaletteId();
	work.clipboardArchive = m_levelSurfaceClipboardArchive; work.clipboardPalette = m_levelSurfaceClipboardPalette; work.clipboardContextCaptured = true;
	work.clipboardStaging = m_levelSurfaceClipboardStaging;
	if (m_packageStaging.isLoaded()) { work.staging = std::make_shared<const PackageStagingModel>(m_packageStaging); }
	work.clipboardEngineFamily = m_levelSurfaceClipboardEngineFamily;
	work.clipboardFormat = m_levelSurfaceClipboardFormat;
	work.token = ++m_levelSurfaceToken; m_levelSurfaceCurrent = levelSurfaceContextGuard(); m_levelSurfacePasting = true;
	if (!m_levelSurfaceWorker->start(std::move(work))) { cancelLevelSurfaceEdits(); return fail(tr("The surface worker is still finishing a previous operation.")); }
	showLevelSidebarPage(QStringLiteral("surfaces"));
	refreshLevelSurfaceTools();
	const auto message = options.includeSelection ? tr("Preparing surface paste for the hit and selected objects…")
		: tr("Preparing paste for %n surface(s)…", nullptr, static_cast<int>(targets.size()));
	m_levelSurfaceTools->setStatus(message, true); statusBar()->showMessage(message, 6000); return true;
}
} // namespace vibestudio
