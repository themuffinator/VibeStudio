#include "app/application_shell.h"
#include "app/level_surface_tools.h"
#include "app/level_surface_worker.h"
#include "app/level_texture_reset_dialog.h"
#include "app/model_viewport.h"
#include "app/map_viewport.h"
#include "app/studio_actions.h"
#include "core/editor_profiles.h"
#include "core/level_patch_tools.h"
#include "core/level_face_clipboard.h"
#include "core/level_nudge.h"
#include "core/level_scene.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>

namespace vibestudio {
namespace {
// The plan view's projection, for its screen and depth axes (core/level_nudge.h).
MapRenderProjection planRenderProjection(MapViewportProjection projection)
{
	switch (projection) {
	case MapViewportProjection::FrontXZ:
		return MapRenderProjection::FrontXZ;
	case MapViewportProjection::SideZY:
		return MapRenderProjection::SideZY;
	case MapViewportProjection::TopXY:
		break;
	}
	return MapRenderProjection::TopXY;
}
} // namespace

bool ApplicationShell::levelSurfaceGeometryBusy() const
{
	return m_levelQuickClip || std::any_of(m_levelPlanViews.cbegin(),m_levelPlanViews.cend(),[](const auto* view) {
		return view && (view->clipMode() || view->isDrawingBrush() || view->isDragging() || view->isMovingComponents());
	}) || (m_levelMap3D && (m_levelMap3D->isDrawingBrush() || m_levelMap3D->isResizingSelection()
		|| m_levelMap3D->isMovingSelection() || m_levelMap3D->isMovingComponents()));
}
QWidget* ApplicationShell::buildLevelSurfaceTools()
{
	m_levelSurfaceTools = new LevelSurfaceTools;
	m_levelSurfaceTools->bindCommands(*m_commands);
	// The profile's texture rotation step and anchor; a later profile change
	// brings its own (applyLevelEditorProfile).
	const auto controls = m_levelControlsApplied ? m_levelControls : levelEditorControlsForProfile(m_settings.selectedEditorProfileId());
	m_levelSurfaceTools->setProfileSteps(controls.surfaceRotationStep, controls.surfaceRotateAboutOrigin);
	m_levelSurfaceTools->targetChanged = [this] { cancelLevelSurfaceEdits(); refreshLevelSurfaceTools(); };
	m_levelSurfaceTools->cancelRequested = [this] { cancelLevelSurfaceEdits(); };
	// The Flags section's boxes and Value field (level_face_flags_actions.cpp).
	m_levelSurfaceTools->flagsEdited = [this](const LevelFaceFlagsEdit& edit) {
		QString error;
		if (!applyLevelFaceFlags(edit, &error)) {
			m_levelSurfaceTools->setStatus(error, levelSurfaceEditsPending());
			statusBar()->showMessage(error, 6000);
		}
	};
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
		m_levelSurfaceResetting = false;
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
					: result.changedPatches > 0 ? tr("Adjusted %n surface(s), patches included. The batch is one undo step.", nullptr, changedSurfaces)
				: tr("Adjusted %n brush face(s). The batch is one undo step.", nullptr, result.changedFaces))
				: tr("Surface mapping is unchanged; no undo step was added.");
			if (result.convertedFaces > 0) {
				message = tr("Pasted surfaces and converted %n classic face(s) throughout the map to Valve 220 in one undo step. Unpasted surfaces retain their materials and UVs.", nullptr, result.convertedFaces);
			}
			if (result.reset) {
				message = changedSurfaces > 0 ? tr("Reset textures on %n surface(s). The batch is one undo step.", nullptr, changedSurfaces)
					: tr("Reset Textures found nothing to change; no undo step was added.");
				if (result.renamed > 0) { message += QLatin1Char(' ') + tr("%n surface(s) took the new texture name.", nullptr, result.renamed); }
			}
			if (advancedClipboard) { message += QLatin1Char(' ') + tr("The wrapped face is now the source for the next paste."); }
			if (result.edgeOnFaces > 0) { message += QLatin1Char(' ') + tr("Radiant projection left %n face(s) edge-on to the copied mapping. Use Radiant values for face-relative alignment.", nullptr, result.edgeOnFaces); }
			if (result.skippedEdgeOn > 0) {
				message += QLatin1Char(' ') + tr("%n surface(s) run along the projection and kept their mapping.", nullptr, result.skippedEdgeOn);
			}
			if (result.skippedClassic > 0) {
				message += QLatin1Char(' ') + tr("%n classic face(s) cannot hold the projection and kept their mapping.", nullptr, result.skippedClassic);
			}
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
		tr("Shrink Texture V"), tr("Grow Texture V"), tr("Fit Texture 1 × 1"), tr("Centre Texture"),
		tr("Flip Texture U"), tr("Flip Texture V"), tr("Fit Texture Width Only"), tr("Fit Texture Height Only"),
		tr("Justify Texture Left"), tr("Justify Texture Right"), tr("Justify Texture Top"), tr("Justify Texture Bottom"),
		tr("Natural Texture Mapping"), tr("Align Texture to Face"),
		tr("Texture Scale U −"), tr("Texture Scale U +"), tr("Texture Scale V −"), tr("Texture Scale V +"),
		tr("Reset Texture Alignment"), tr("Radiant Texture Reset"), tr("Fit Texture Width"), tr("Fit Texture Height"),
		tr("Project Texture Axially"), tr("Project Texture from 2D View"), tr("Project Texture from Camera")};
	const auto ids = LevelSurfaceTools::commandIds();
	for (int i = 0; i < ids.size(); ++i) {
		StudioCommandRegistration command;
		command.commandId = ids[i]; command.label = labels[i]; command.group = StudioCommandGroup::Edit;
		command.menuSection = tr("Surface Adjustments"); command.iconName = QStringLiteral("image");
		command.statusTip = tr("Adjust the texture mapping of the targeted brush faces and patches using the step values in Levels > Surfaces.");
		// The resets and projections act on the picked faces when there are
		// any, as TrenchBroom's and Radiant's act on their face selection.
		const bool reset = ids[i] == QLatin1String("map.surfaceReset") || ids[i] == QLatin1String("map.surfaceRadiantReset");
		const bool projection = ids[i].startsWith(QLatin1String("map.surfaceProject"));
		if (ids[i] == QLatin1String("map.surfaceReset")) {
			command.statusTip = tr("Reset the picked faces, or the Surfaces target, to no shift or rotation at the default scale, each in its own format: classic faces natural, Valve 220 and brush-primitive faces in their plane, patches their natural spread.");
		} else if (ids[i] == QLatin1String("map.surfaceRadiantReset")) {
			command.statusTip = tr("Reset the picked faces, or the Surfaces target, as Radiant does: classic faces natural, Valve 220 and brush-primitive faces in their plane's own basis, patches capped.");
		} else if (ids[i] == QLatin1String("map.surfaceFitU")) {
			command.statusTip = tr("Fit one texture repeat across the width (U) of each targeted face and patch and leave its height (V) as it is, as NetRadiant Custom's Fit Width Only does.");
		} else if (ids[i] == QLatin1String("map.surfaceFitV")) {
			command.statusTip = tr("Fit one texture repeat down the height (V) of each targeted face and patch and leave its width (U) as it is, as NetRadiant Custom's Fit Height Only does.");
		} else if (ids[i] == QLatin1String("map.surfaceFitWidth")) {
			command.statusTip = tr("Fit one texture repeat across the width (U) of each targeted face and patch and scale its height (V) to match, so the texture keeps its proportions, as NetRadiant Custom's Fit Width does.");
		} else if (ids[i] == QLatin1String("map.surfaceFitHeight")) {
			command.statusTip = tr("Fit one texture repeat down the height (V) of each targeted face and patch and scale its width (U) to match, so the texture keeps its proportions, as NetRadiant Custom's Fit Height does.");
		} else if (ids[i] == QLatin1String("map.surfaceProjectAxial")) {
			command.statusTip = tr("Project the default texture onto the picked faces, or the Surfaces target, along the world axis each face looks down most, and onto patches along the axis nearest their facing, as NetRadiant Custom's Project Axial does.");
		} else if (ids[i] == QLatin1String("map.surfaceProjectView")) {
			command.statusTip = tr("Project the default texture onto the picked faces, or the Surfaces target, straight from the active 2D view, so they share one flat projection, as NetRadiant Custom's Project Ortho does. Faces the view sees edge-on keep their mapping.");
		} else if (ids[i] == QLatin1String("map.surfaceProjectCamera")) {
			command.statusTip = tr("Project the default texture onto the picked faces, or the Surfaces target, along the 3D camera's view, so they share one flat projection, as NetRadiant Custom's Project Camera does. Faces the camera sees edge-on keep their mapping.");
		}
		command.handler = [this, id = ids[i], picked = reset || projection] {
			if (!m_levelSurfaceTools) { return; }
			setMode(StudioMode::Levels);
			showLevelSidebarPage(QStringLiteral("surfaces"));
			QString error;
			LevelSurfaceRequest request = m_levelSurfaceTools->request(id);
			if (id == QLatin1String("map.surfaceProjectView")) {
				// Towards the viewer of the active 2D view (Top +Z, Front -Y,
				// Side +X), as Radiant's Project Ortho takes the current view's axis.
				if (m_levelMapViewport) {
					request.projectionDirection = levelPlanNudgeAxis(planRenderProjection(m_levelMapViewport->projection()), LevelNudgeDirection::DepthUp);
				} else {
					error = tr("Show a 2D view to project a texture from it.");
				}
			} else if (id == QLatin1String("map.surfaceProjectCamera")) {
				// Back towards the camera, as Radiant projects along the negated view vector.
				if (m_levelMap3D) {
					const ModelVec3 forward = m_levelMap3D->viewForward();
					request.projectionDirection = {-forward.x, -forward.y, -forward.z, true};
				} else {
					error = tr("The 3D camera is not available for texture projection.");
				}
			}
			if (!error.isEmpty() || !queueLevelSurfaceEdit(request, &error, picked)) {
				m_levelSurfaceTools->setStatus(error, levelSurfaceEditsPending()); statusBar()->showMessage(error, 6000);
			}
		};
		m_commands->registerCommand(command);
	}
	{
		// bobToolz's Reset Textures window (app/level_texture_reset_dialog.h).
		StudioCommandRegistration command;
		command.commandId = QStringLiteral("map.resetTextures");
		command.label = tr("Reset Textures…");
		command.group = StudioCommandGroup::Edit; command.menuSection = tr("Surface Adjustments"); command.iconName = QStringLiteral("image");
		command.statusTip = tr("Set absolute texture scale, shift and rotation, or a new texture name, on the selection or the whole map, optionally only where one texture is used.");
		command.handler = [this] { showLevelTextureResetDialog(); };
		m_commands->registerCommand(command);
		m_commands->setEnabled(command.commandId, false);
	}
	registerLevelFaceFlagCommands();
	registerLevelUvEditorCommands();
	for (bool paste : {false, true}) {
		StudioCommandRegistration command;
		command.commandId = paste ? QStringLiteral("map.pasteSurface") : QStringLiteral("map.copySurface");
		command.label = paste ? tr("Paste Surface") : tr("Copy Surface");
		command.group = StudioCommandGroup::Edit; command.menuSection = tr("Surface Clipboard"); command.iconName = QStringLiteral("image");
		command.statusTip = paste ? tr("Paste the copied material, mapping and flags onto the Surfaces target.") : tr("Copy selected face components, or the inspected brush face, with material, mapping and flags.");
		command.handler = [this, paste] {
			if (!m_levelSurfaceTools) { return; }
			setMode(StudioMode::Levels); showLevelSidebarPage(QStringLiteral("surfaces"));
			QString error; bool done = false;
			if (paste) {
				auto targets = levelSurfaceSelectionTargets();
				if (m_levelSurfaceTools->singleFace()) {
					QVector<LevelMaterialTarget> faces;
					for (const auto& face : levelSurfaceFaceTargets()) {
						const LevelMaterialTarget target{LevelMaterialKind::BrushFace, face.brushId, face.faceIndex};
						if (targets.contains(target)) { faces.push_back(target); }
					}
					targets = faces;
				}
				done = pasteLevelSurfaceTargets(targets, m_levelSurfaceTools->pasteOptions(), &error);
			} else {
				const auto picked = levelPickedSurfaceFaces();
				const auto faces = levelComponentsActive() || !picked.isEmpty() ? picked : QVector<LevelSurfaceFace>{inspectedLevelMapSurface()};
				const auto text = levelFaceSelectionText(m_levelMapDocument, faces, &error);
				done = !text.isEmpty() && copyLevelSurfaceSettings(faces.last(), &error);
				if (done) { QGuiApplication::clipboard()->setText(text); }
			}
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
	const auto history = m_levelMapDocument.historyRevision;
	const auto package = m_packageStaging.revision(), reload = m_levelPreviewReload;
	const auto palette = activePaletteId();
	const bool single = m_levelSurfaceTools->singleFace();
	const auto inspected = levelSurfaceFaceTargets();
	const auto picked = levelPickedSurfaceFaces();
	const auto targets = levelSurfaceSelectionTargets();
	const auto components = levelComponentSelectionToken();
	return [this, load, revision, path, output, hash, selection, scene, active, saved, history, package, reload, palette, single, inspected, picked, targets, components] {
		return load == m_levelMapLoadSerial && revision == m_levelMapDocument.revision && path == m_levelMapDocument.sourcePath
			&& output == m_levelMapDocument.outputPath && hash == m_levelMapDocument.sourceContentHash
			&& selection == m_levelMapDocument.selection && scene == m_levelMapDocument.scene && active == m_levelMapDocument.activeSceneNode
			&& saved == m_levelMapDocument.savedUndoDepth && history == m_levelMapDocument.historyRevision && package == m_packageStaging.revision()
			&& reload == m_levelPreviewReload && palette == activePaletteId() && single == m_levelSurfaceTools->singleFace()
			&& (!single || inspected == levelSurfaceFaceTargets()) && picked == levelPickedSurfaceFaces()
			&& targets == levelSurfaceSelectionTargets() && components == levelComponentSelectionToken() && !levelSurfaceGeometryBusy();
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
	const auto pasteTargets = levelSurfaceSelectionTargets();
	int faces = 0, patches = 0;
	for (const auto& target : pasteTargets) {
		patches += target.kind == LevelMaterialKind::Patch;
		if (target.kind == LevelMaterialKind::BrushFace) {
			++faces;
		}
	}
	m_commands->setEnabled(QStringLiteral("map.alignSurfaces"), faces > 0 && faces <= 16384 && !levelSurfaceGeometryBusy());
	if (m_levelSurfaceTools->singleFace()) { faces = static_cast<int>(levelSurfaceFaceTargets().size()); }
	const auto faceTargets = levelSurfaceFaceTargets();
	m_levelSurfaceTools->setTargetSummary(faces, faceTargets.size() == 1 ? faceTargets.first() : inspected, patches, levelComponentsActive());
	m_levelSurfaceTools->setDefaultScale(levelSurfaceDefaultScale(m_levelMapDocument));
	// Quick adjustments reach the selected patches too, unless the target is
	// the one inspected face.
	const int quickPatches = m_levelSurfaceTools->singleFace() ? 0 : patches;
	const bool editable = (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map)
		&& (faces > 0 || quickPatches > 0) && faces <= 16384 && quickPatches <= 256 && !levelSurfaceGeometryBusy();
	for (const auto& id : LevelSurfaceTools::commandIds()) { m_commands->setEnabled(id, editable); }
	if (!editable && (m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map)
		&& !levelSurfaceGeometryBusy()) {
		// The resets and projections act on picked faces whatever the
		// Surfaces tab's target says.
		const qsizetype picked = levelPickedSurfaceFaces().size();
		for (const char* id : {"map.surfaceReset", "map.surfaceRadiantReset", "map.surfaceProjectAxial", "map.surfaceProjectView",
				 "map.surfaceProjectCamera"}) {
			m_commands->setEnabled(QString::fromLatin1(id), picked > 0 && picked <= 16384);
		}
	}
	// Projecting from the camera reads its view direction.
	if (!m_levelMap3D) { m_commands->setEnabled(QStringLiteral("map.surfaceProjectCamera"), false); }
	// Reset Textures can always reach the whole map.
	m_commands->setEnabled(QStringLiteral("map.resetTextures"), (m_levelMapDocument.format == LevelMapFormat::QuakeMap
		|| m_levelMapDocument.format == LevelMapFormat::Quake3Map) && !levelSurfaceEditsPending() && !levelSurfaceGeometryBusy());
	refreshLevelCameraTextureCommands(editable);
	const bool copyReady = levelComponentsActive() || !levelPickedSurfaceFaces().isEmpty() ? !faceTargets.isEmpty() && faceTargets.size() <= 16384
		: inspected.brushId >= 0 && inspected.faceIndex >= 0;
	m_commands->setEnabled(QStringLiteral("map.copySurface"), copyReady && !levelSurfaceEditsPending() && !levelSurfaceGeometryBusy());
	const bool pasteEditable = m_levelSurfaceTools->singleFace() ? editable : !pasteTargets.isEmpty() && pasteTargets.size() <= 16384;
	m_commands->setEnabled(QStringLiteral("map.pasteSurface"), pasteEditable && m_levelSurfaceClipboard.ready() && !levelSurfaceEditsPending() && !levelSurfaceGeometryBusy());
	m_levelSurfaceTools->setClipboardSummary(m_levelSurfaceClipboard);
	refreshLevelFaceFlags();
	refreshLevelUvEditorCommand();
	// Hide and Show All wait while surface edits are pending; a batch that
	// changes nothing publishes no map, so they come back here.
	refreshLevelVisibilityCommands();
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
	m_levelSurfaceResetting = false;
	if (m_levelSurfaceDebounce) { m_levelSurfaceDebounce->stop(); }
	if (m_levelSurfaceWorker) { m_levelSurfaceWorker->cancel(); }
	const auto message = wasStroke ? tr("The complete surface stroke was discarded. Earlier edits remain in undo history.")
		: tr("Pending surface adjustments cancelled. Completed batches remain in undo history.");
	if (m_levelSurfaceTools) { m_levelSurfaceTools->setStatus(message, false); }
	if (wasPasting) { showLevelMaterialPaintMessage(message); }
	refreshLevelSurfaceTools();
}

void ApplicationShell::levelSurfaceQueueTargets(bool cameraTargets, QVector<LevelSurfaceFace>* faces, QVector<int>* patches) const
{
	faces->clear(); patches->clear();
	const QVector<LevelSurfaceFace> picked = cameraTargets ? levelPickedSurfaceFaces() : QVector<LevelSurfaceFace>{};
	if (!picked.isEmpty()) {
		// TrenchBroom's camera texture keys act on its face selection: here the
		// picked faces, with any whole patches Select Textured picked.
		*faces = picked;
		for (const auto& ref : m_levelMapDocument.selection) {
			if (ref.kind == LevelMapSelectionKind::QuakePatch && m_levelTexturedPatches.contains(ref.objectId)) { patches->append(ref.objectId); }
		}
		return;
	}
	for (const auto& target : levelSurfaceSelectionTargets()) {
		if (target.kind == LevelMaterialKind::BrushFace) { faces->append({target.objectId,target.faceIndex}); }
		else if (target.kind == LevelMaterialKind::Patch) { patches->append(target.objectId); }
	}
	if (m_levelSurfaceTools->singleFace()) {
		*faces = levelSurfaceFaceTargets();
		patches->clear();
	}
}

bool ApplicationShell::queueLevelSurfaceEdit(const LevelSurfaceRequest& request, QString* error, bool cameraTargets)
{
	if (error) { error->clear(); }
	if (!m_levelSurfaceTools || !m_levelSurfaceWorker) { return false; }
	if (levelSurfaceGeometryBusy()) { if (error) { *error = tr("Finish the active geometry tool before adjusting surfaces."); } return false; }
	// Repeated commands in the same burst reuse the captured targets. A
	// large map must not be rescanned for every auto-repeat event.
	if (!m_levelSurfaceCurrent || !m_levelSurfaceCurrent()) { refreshLevelSurfaceTools(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (m_levelSurfacePasting) { return fail(tr("Wait for the surface paste to finish or cancel it before queuing adjustments.")); }
	if (m_levelSurfaceResetting) { return fail(tr("Wait for Reset Textures to finish or cancel it before queuing adjustments.")); }
	if (m_levelSurfacePending.size() >= 64) { return fail(tr("The surface queue is full. Wait for the pending batch or cancel it.")); }
	if (m_levelSurfaceCurrent && cameraTargets != m_levelSurfaceCameraTargets) {
		// A camera move joins a burst of quick adjustments, or the other way
		// round, only where both would adjust the same surfaces.
		QVector<LevelSurfaceFace> faces; QVector<int> patches;
		levelSurfaceQueueTargets(cameraTargets, &faces, &patches);
		if (faces != m_levelSurfaceFaces || patches != m_levelSurfacePatches) {
			return fail(tr("Wait for the pending surface adjustments to finish before adjusting other surfaces."));
		}
	}
	if (!m_levelSurfaceCurrent) {
		levelSurfaceQueueTargets(cameraTargets, &m_levelSurfaceFaces, &m_levelSurfacePatches);
		m_levelSurfaceCameraTargets = cameraTargets;
		if ((m_levelSurfaceFaces.isEmpty() && m_levelSurfacePatches.isEmpty()) || m_levelSurfaceFaces.size() > 16384 || m_levelSurfacePatches.size() > 256) {
			return fail(tr("Select between 1 and 16,384 brush faces, or up to 256 patches, for surface alignment."));
		}
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
	work.document = m_levelMapDocument; work.faces = m_levelSurfaceFaces; work.patches = m_levelSurfacePatches;
	work.adjustments = std::move(m_levelSurfacePending);
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
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy() || (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->isDrawingBrush()
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
	return startLevelSurfacePaste(targets, options, {}, error);
}
bool ApplicationShell::startLevelSurfacePaste(const QVector<LevelMaterialTarget>& targets, const LevelSurfacePasteOptions& options,
	const QString& faceText, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!m_levelSurfaceTools || !m_levelSurfaceWorker) { return fail(tr("Surface tools are unavailable.")); }
	if (faceText.isEmpty() && !m_levelSurfaceClipboard.ready()) { return fail(tr("Copy a brush surface before pasting.")); }
	if (targets.isEmpty() || targets.size() > 16384) { return fail(tr("Select between 1 and 16,384 surfaces for paste.")); }
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy() || (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->isDrawingBrush()
		|| m_levelMap3D->isResizingSelection() || m_levelMap3D->isMovingSelection()))) {
		return fail(tr("Finish or cancel the pending level edit before pasting a surface."));
	}
	auto archive = m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) { archive = std::make_shared<const PackageArchive>(m_packageArchive); }
	LevelSurfaceWork work; work.document = m_levelMapDocument; work.pasteTargets = targets;
	work.clipboard = m_levelSurfaceClipboard; work.paste = options; work.archive = std::move(archive); work.palette = activePaletteId();
	if (options.includeSelection) { work.paste.selectionTargets = levelSurfaceSelectionTargets(); }
	work.clipboardArchive = m_levelSurfaceClipboardArchive; work.clipboardPalette = m_levelSurfaceClipboardPalette; work.clipboardContextCaptured = true;
	work.clipboardStaging = m_levelSurfaceClipboardStaging;
	if (m_packageStaging.isLoaded()) { work.staging = std::make_shared<const PackageStagingModel>(m_packageStaging); }
	work.clipboardEngineFamily = m_levelSurfaceClipboardEngineFamily;
	work.clipboardFormat = m_levelSurfaceClipboardFormat;
	if (!faceText.isEmpty()) { work.faceClipboardText = faceText; work.clipboard = {}; work.clipboardContextCaptured = false; }
	work.token = ++m_levelSurfaceToken; m_levelSurfaceCurrent = levelSurfaceContextGuard(); m_levelSurfacePasting = true;
	if (!m_levelSurfaceWorker->start(std::move(work))) { cancelLevelSurfaceEdits(); return fail(tr("The surface worker is still finishing a previous operation.")); }
	showLevelSidebarPage(QStringLiteral("surfaces"));
	refreshLevelSurfaceTools();
	const auto message = options.includeSelection ? tr("Preparing surface paste for the hit and selected objects…")
		: tr("Preparing paste for %n surface(s)…", nullptr, static_cast<int>(targets.size()));
	m_levelSurfaceTools->setStatus(message, true); statusBar()->showMessage(message, 6000); return true;
}

bool ApplicationShell::startLevelTextureReset(LevelTextureResetRequest request, QString* error)
{
	if (error) { error->clear(); }
	const auto fail = [error](const QString& message) { if (error) { *error = message; } return false; };
	if (!m_levelSurfaceTools || !m_levelSurfaceWorker) { return fail(tr("Surface tools are unavailable.")); }
	if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		return fail(tr("Open a Quake-family map to reset its textures."));
	}
	if (levelSurfaceEditsPending() || levelSurfaceGeometryBusy() || (m_levelMap3D && (m_levelMap3D->surfaceStrokeActive() || m_levelMap3D->isDrawingBrush()
		|| m_levelMap3D->isResizingSelection() || m_levelMap3D->isMovingSelection()))) {
		return fail(tr("Finish or cancel the pending level edit before resetting textures."));
	}
	if (!request.changesSomething()) { return fail(tr("Tick a value to set, or give a new texture name, before resetting textures.")); }
	request.excludedObjects.clear();
	if (request.wholeMap) {
		// What the views hide with filters or Hide stays as it is, as Find and
		// Replace Textures and Select All leave it.
		request.faces.clear(); request.patches.clear();
		for (const QString& key : levelSceneObjects(m_levelMapDocument)) {
			const qsizetype colon = key.indexOf(QLatin1Char(':'));
			if (m_levelMapViewport && m_levelMapViewport->isHidden(levelMapSelectionKindFromId(key.left(colon)), key.mid(colon + 1).toInt())) {
				request.excludedObjects.insert(key);
			}
		}
	} else {
		levelSurfaceQueueTargets(false, &request.faces, &request.patches);
		if (request.faces.isEmpty() && request.patches.isEmpty()) {
			return fail(tr("Select brushes or patches, or pick faces, to reset their textures; or reset the whole map."));
		}
	}
	auto archive = m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) { archive = std::make_shared<const PackageArchive>(m_packageArchive); }
	LevelSurfaceWork work;
	work.document = m_levelMapDocument; work.resetTextures = true; work.reset = std::move(request);
	work.archive = std::move(archive); work.palette = activePaletteId();
	if (m_packageStaging.isLoaded()) { work.staging = std::make_shared<const PackageStagingModel>(m_packageStaging); }
	work.token = ++m_levelSurfaceToken; m_levelSurfaceCurrent = levelSurfaceContextGuard(); m_levelSurfaceResetting = true;
	if (!m_levelSurfaceWorker->start(std::move(work))) { cancelLevelSurfaceEdits(); return fail(tr("The surface worker is still finishing a previous operation.")); }
	setMode(StudioMode::Levels);
	showLevelSidebarPage(QStringLiteral("surfaces"));
	refreshLevelSurfaceTools();
	const auto message = tr("Resetting textures…");
	m_levelSurfaceTools->setStatus(message, true); statusBar()->showMessage(message, 6000);
	return true;
}

void ApplicationShell::showLevelTextureResetDialog()
{
	if (!m_levelSurfaceTools) { return; }
	setMode(StudioMode::Levels);
	if (m_levelMapDocument.format != LevelMapFormat::QuakeMap && m_levelMapDocument.format != LevelMapFormat::Quake3Map) {
		statusBar()->showMessage(tr("Open a Quake-family map to reset its textures."), 6000);
		return;
	}
	QVector<LevelSurfaceFace> faces;
	QVector<int> patches;
	levelSurfaceQueueTargets(false, &faces, &patches);
	LevelTextureResetDialog dialog(static_cast<int>(faces.size()), static_cast<int>(patches.size()), m_levelPaintMaterial,
		levelSurfaceDefaultScale(m_levelMapDocument), this);
	if (dialog.exec() != QDialog::Accepted) { return; }
	QString error;
	if (!startLevelTextureReset(dialog.request(), &error)) {
		m_levelSurfaceTools->setStatus(error, levelSurfaceEditsPending()); statusBar()->showMessage(error, 6000);
	}
}
} // namespace vibestudio
