#include "app/application_shell.h"
#include "app/level_preview_worker.h"
#include "app/level_surface_tools.h"
#include "app/level_surface_worker.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>

namespace vibestudio {
struct LevelSurfaceStrokeState {
	struct Hit { LevelMaterialTarget target; CameraMaterialGesture action; };
	LevelSurfaceWork work;
	QVector<Hit> pending;
	QVector<LevelMaterialTarget> bindings;
	QSet<LevelMaterialTarget> touched;
	QSet<int> visibleBrushes, visiblePatches, visibleEntities;
	LevelMaterialTarget lastTarget;
	CameraMaterialGesture lastAction = CameraMaterialGesture::None;
	QSize sourceSize;
	QString assetKey;
	int hits = 0;
	bool released = false, filtered = false;
};

void ApplicationShell::beginLevelSurfaceStroke()
{
	if (!m_levelSurfaceTools || !m_levelSurfaceWorker || levelSurfaceEditsPending() || !m_levelSurfaceClipboard.ready()) {
		m_levelMap3D->finishMaterialStroke(false);
		showLevelMaterialPaintMessage(m_levelSurfaceClipboard.ready() ? tr("Finish or cancel the pending level edit before pasting a surface.")
			: tr("Copy a brush surface before pasting.")); return;
	}
	auto state = std::make_shared<LevelSurfaceStrokeState>();
	state->bindings = m_levelPreviewMaterialTargets;
	auto& work = state->work;
	work.document = m_levelMapDocument; work.clipboard = m_levelSurfaceClipboard;
	work.archive = m_packageArchive.snapshotReader();
	if (!work.archive && m_packageArchive.isOpen()) { work.archive = std::make_shared<const PackageArchive>(m_packageArchive); }
	if (m_packageStaging.isLoaded()) { work.staging = std::make_shared<const PackageStagingModel>(m_packageStaging); }
	work.palette = activePaletteId(); work.clipboardArchive = m_levelSurfaceClipboardArchive; work.clipboardStaging = m_levelSurfaceClipboardStaging;
	work.clipboardPalette = m_levelSurfaceClipboardPalette; work.clipboardEngineFamily = m_levelSurfaceClipboardEngineFamily;
	work.clipboardFormat = m_levelSurfaceClipboardFormat; work.clipboardContextCaptured = true;
	work.token = ++m_levelSurfaceToken;
	state->assetKey = m_packageArchive.sourcePath() + QLatin1Char('|') + QString::number(m_packageStaging.revision()) + QLatin1Char('|') + QString::number(m_levelPreviewReload);
	state->filtered = m_levelMapViewport && m_levelMapViewport->hiddenCount() > 0;
	if (state->filtered) {
		const auto& visible = m_levelMapViewport->displayDocument();
		for (const auto& brush : visible.brushes) { state->visibleBrushes.insert(brush.id); }
		for (const auto& patch : visible.patches) { state->visiblePatches.insert(patch.id); }
		for (const auto& entity : visible.entities) { state->visibleEntities.insert(entity.id); }
	}
	m_levelSurfaceStroke = std::move(state); m_levelSurfaceCurrent = levelSurfaceContextGuard(); m_levelSurfacePasting = true;
	if (!m_levelSurfaceStrokePreview) {
		m_levelSurfaceStrokePreview = new LevelPreviewWorker(this);
		m_levelSurfaceStrokePreview->completed = [this](const LevelPreviewResult& result) {
			const auto state = m_levelSurfaceStroke;
			if (!state || result.loadSerial != state->work.token) { return; }
			if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) { cancelLevelSurfaceEdits(); return; }
			if (!result.error.isEmpty() || result.assets.cancelled || result.preview.cancelled) {
				showLevelMaterialPaintMessage(result.error.isEmpty() ? tr("Surface preview cancelled. Release to apply or Escape to discard the stroke.") : result.error); return;
			}
			QVector<int> triangles;
			for (int i = 0; i < result.preview.materialTargets.size(); ++i) {
				if (state->touched.contains(result.preview.materialTargets[i])) { triangles << i; }
			}
			m_levelMap3D->setMaterialStrokePreview(result.preview.mesh, levelPreviewSurfaceImages(result.preview.mesh, result.assets), triangles,
				state->work.stroke ? state->work.stroke->plan().faceCount() + state->work.stroke->plan().patchCount() : 0);
		};
	}
	m_levelMaterialStrokeCancel->setEnabled(true);
	m_levelSurfaceTools->setStatus(tr("Surface stroke active · Release to apply; Escape cancels."), true);
	showLevelMaterialPaintMessage(tr("Surface stroke active · Drag across faces; change modifiers to switch transfer modes. Release to apply; Escape cancels."));
	refreshLevelSurfaceTools();
}

void ApplicationShell::touchLevelSurfaceStroke(int triangle, CameraMaterialGesture action)
{
	const auto state = m_levelSurfaceStroke;
	if (!state || state->released) { return; }
	if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) { cancelLevelSurfaceEdits(); return; }
	if (triangle < 0 || triangle >= state->bindings.size()) { return; }
	const auto target = state->bindings[triangle];
	const bool wraps = action == CameraMaterialGesture::WrapFace || action == CameraMaterialGesture::WrapFaceOnly;
	if (target.kind != LevelMaterialKind::BrushFace && (wraps || target.kind != LevelMaterialKind::Patch)) {
		if (state->hits == 0) {
			cancelLevelSurfaceEdits();
			showLevelMaterialPaintMessage(target.kind == LevelMaterialKind::None ? tr("Placed model materials are edited in Models.")
				: tr("This paste needs a brush face or a patch for Radiant values/projection. Use material painting for Doom surfaces."));
		}
		return;
	}
	if (target == state->lastTarget && action == state->lastAction) { return; }
	if (state->pending.size() >= 256 || state->hits >= 4096) {
		cancelLevelSurfaceEdits(); showLevelMaterialPaintMessage(tr("The surface stroke exceeded its pending-hit limit and was discarded. Use shorter strokes.")); return;
	}
	state->lastTarget = target; state->lastAction = action;
	state->pending.append({target, action}); state->touched.insert(target); ++state->hits;
	startLevelSurfaceStrokeStep();
}

void ApplicationShell::finishLevelSurfaceStroke(bool commit)
{
	if (!m_levelSurfaceStroke) { return; }
	if (!commit) { cancelLevelSurfaceEdits(); return; }
	m_levelSurfaceStroke->released = true;
	m_levelSurfaceTools->setStatus(tr("Finishing surface stroke…"), true);
	showLevelMaterialPaintMessage(tr("Finishing surface stroke…")); startLevelSurfaceStrokeStep();
}

void ApplicationShell::cancelLevelSurfaceStroke()
{
	if (!m_levelSurfaceStroke) { return; }
	m_levelSurfaceStroke.reset();
	if (m_levelSurfaceStrokePreview) { m_levelSurfaceStrokePreview->reset(); }
	if (m_levelMap3D) { m_levelMap3D->finishMaterialStroke(false); m_levelMap3D->clearMaterialStrokePreview(); }
	if (m_levelMaterialStrokeCancel) { m_levelMaterialStrokeCancel->setEnabled(false); }
	// Restore the current document after a package/filter/context change too.
	m_levelPreviewRequestKey.clear();
	QTimer::singleShot(0, this, [this] { if (!m_levelSurfaceStroke) { refreshLevelMap3D(); } });
}

void ApplicationShell::startLevelSurfaceStrokeStep()
{
	const auto state = m_levelSurfaceStroke;
	if (!state || m_levelSurfaceWorker->busy()) { return; }
	if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) { cancelLevelSurfaceEdits(); return; }
	auto work = state->work;
	if (state->pending.isEmpty()) {
		if (!state->released) { return; }
		if (!work.stroke) { cancelLevelSurfaceEdits(); return; }
		work.finishStroke = true;
	} else {
		const auto hit = state->pending.takeFirst(); work.pasteTargets = {hit.target};
		work.paste = {}; work.paste.textureSize = state->sourceSize;
		work.paste.mappingOnly = hit.action == CameraMaterialGesture::ValuesSelectionOnly || hit.action == CameraMaterialGesture::ProjectSelectionOnly || hit.action == CameraMaterialGesture::WrapFaceOnly;
		if (hit.action == CameraMaterialGesture::WrapFace || hit.action == CameraMaterialGesture::WrapFaceOnly) {
			work.paste.mode = LevelSurfacePasteMode::Seamless; work.paste.allowValve220 = m_levelSurfaceTools->pasteOptions().allowValve220;
		} else {
			work.paste.mode = hit.action == CameraMaterialGesture::ProjectSelection || hit.action == CameraMaterialGesture::ProjectSelectionOnly
				? LevelSurfacePasteMode::RadiantProject : LevelSurfacePasteMode::RadiantValues;
			work.paste.includeSelection = true;
		}
		work.startStroke = !work.stroke;
	}
	if (!m_levelSurfaceWorker->start(std::move(work))) { cancelLevelSurfaceEdits(); }
}

void ApplicationShell::completeLevelSurfaceStroke(LevelSurfaceResult result)
{
	const auto state = m_levelSurfaceStroke;
	if (!state || result.token != m_levelSurfaceToken || result.token != state->work.token) { startLevelSurfaceBatch(); refreshLevelSurfaceTools(); return; }
	if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) {
		cancelLevelSurfaceEdits(); showLevelMaterialPaintMessage(tr("The map context changed. The complete surface stroke was discarded.")); return;
	}
	if (result.cancelled || !result.error.isEmpty()) {
		cancelLevelSurfaceEdits();
		const auto message = result.cancelled ? tr("Surface stroke cancelled. The map and copied source are unchanged.") : result.error;
		m_levelSurfaceTools->setStatus(message, false); showLevelMaterialPaintMessage(message); return;
	}
	if (!result.finishedStroke) {
		state->work.stroke = std::move(result.stroke); state->work.archive = std::move(result.resolvedArchive);
		state->work.stagingIndexed = result.stagingIndexed; state->work.textureSizes = std::move(result.textureSizes);
		if (result.sourceTextureSize.isValid()) { state->sourceSize = result.sourceTextureSize; }
		LevelPreviewRequest preview; preview.document = state->work.stroke->document();
		if (state->filtered) {
			preview.document.brushes.removeIf([&](const auto& brush) { return !state->visibleBrushes.contains(brush.id); });
			preview.document.patches.removeIf([&](const auto& patch) { return !state->visiblePatches.contains(patch.id); });
			preview.document.entities.removeIf([&](const auto& entity) { return !state->visibleEntities.contains(entity.id); });
		}
		preview.archive = state->work.archive; preview.staging = state->work.stagingIndexed ? nullptr : state->work.staging;
		preview.options.paletteId = state->work.palette; preview.assetKey = state->assetKey;
		preview.loadSerial = state->work.token; preview.sourceKey = QString::number(state->work.stroke->stepCount());
		m_levelSurfaceStrokePreview->request(std::move(preview));
		const auto message = tr("%n surface(s) in stroke preview · Release to apply; Escape cancels.", nullptr, result.changedFaces + result.changedPatches);
		m_levelSurfaceTools->setStatus(message, true); showLevelMaterialPaintMessage(message);
		startLevelSurfaceStrokeStep(); return;
	}
	m_levelSurfaceStroke.reset(); m_levelSurfaceStrokePreview->reset(); m_levelSurfaceCurrent = {}; m_levelSurfacePasting = false;
	m_levelMaterialStrokeCancel->setEnabled(false); m_levelMap3D->clearMaterialStrokePreview();
	if (result.nextClipboard.ready()) {
		m_levelSurfaceClipboard = std::move(result.nextClipboard); m_levelSurfaceClipboardArchive = std::move(result.nextClipboardArchive);
		m_levelSurfaceClipboardStaging = std::move(result.nextClipboardStaging); m_levelSurfaceClipboardPalette = std::move(result.nextClipboardPalette);
		m_levelSurfaceClipboardEngineFamily = std::move(result.nextClipboardEngineFamily); m_levelSurfaceClipboardFormat = result.nextClipboardFormat;
	}
	const int changed = result.changedFaces + result.changedPatches;
	if (changed > 0) {
		m_levelSurfacePublishing = true; m_levelMapDocument = std::move(result.document);
		recordActivity(tr("Level surface stroke applied"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
		refreshLevelMapWorkbench(); m_levelSurfacePublishing = false;
	}
	refreshLevelSurfaceTools();
	auto message = changed > 0 ? tr("Pasted %n surface(s). The stroke is one undo step.", nullptr, changed) : tr("Surface mapping is unchanged; no undo step was added.");
	if (result.convertedFaces > 0) { message += QLatin1Char(' ') + tr("Converted %n classic face(s) throughout the map to Valve 220.", nullptr, result.convertedFaces); }
	if (result.edgeOnFaces > 0) { message += QLatin1Char(' ') + tr("Radiant projection left %n face(s) edge-on to the copied mapping.", nullptr, result.edgeOnFaces); }
	m_levelSurfaceTools->setStatus(message, false); showLevelMaterialPaintMessage(message);
}
} // namespace vibestudio
