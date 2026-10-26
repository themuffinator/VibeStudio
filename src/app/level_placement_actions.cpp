#include "app/application_shell.h"
#include "app/level_placement_dialog.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "core/level_texture_mapping.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QStatusBar>

namespace vibestudio {
bool ApplicationShell::canDuplicateLevelMapSelection(LevelDuplicateMode mode) const {
	return levelMapSelectionIsDuplicable(m_levelMapDocument)
		&& (!(m_levelControls.duplicateRequiresObjectMode || mode == LevelDuplicateMode::MakeUnique) || !levelComponentsActive())
		&& (mode != LevelDuplicateMode::MakeUnique || m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map);
}
std::function<bool(const LevelMapDocument&, QString*)> ApplicationShell::levelPlacementCommitter(bool readOnly) {
	const auto load = m_levelMapLoadSerial, revision = m_levelMapDocument.revision;
	const auto repeatRevision = m_levelMapDocument.repeat.revision;
	const auto history = m_levelMapDocument.historyRevision;
	const auto insertionVisibility = m_levelMapDocument.insertionVisibility;
	const auto nudgeAfterDuplicate = m_levelControls.nudgeAfterDuplicate;
	const auto duplicateRequiresObjectMode = m_levelControls.duplicateRequiresObjectMode;
	const auto selection = m_levelMapDocument.selection;
	const auto activeNode = m_levelMapDocument.activeSceneNode;
	const auto sourcePath = m_levelMapDocument.sourcePath;
	const auto sourceHash = m_levelMapDocument.sourceContentHash;
	const auto savedDepth = m_levelMapDocument.savedUndoDepth;
	const auto packageRevision = m_packageStaging.revision(), reload = m_levelPreviewReload;
	const auto palette = activePaletteId();
	const auto components = levelComponentSelectionToken();
	const auto picked = levelPickedSurfaceFaces();
	return [this, load, revision, repeatRevision, history, selection, activeNode, sourcePath, sourceHash, savedDepth, packageRevision, reload,
			palette, components, picked, readOnly, insertionVisibility, nudgeAfterDuplicate, duplicateRequiresObjectMode](const LevelMapDocument& candidate, QString* error) {
		if (load != m_levelMapLoadSerial || revision != m_levelMapDocument.revision || history != m_levelMapDocument.historyRevision || repeatRevision != m_levelMapDocument.repeat.revision || selection != m_levelMapDocument.selection ||
			activeNode != m_levelMapDocument.activeSceneNode || sourcePath != m_levelMapDocument.sourcePath ||
			sourceHash != m_levelMapDocument.sourceContentHash || savedDepth != m_levelMapDocument.savedUndoDepth ||
			packageRevision != m_packageStaging.revision() || reload != m_levelPreviewReload || palette != activePaletteId() ||
			components != levelComponentSelectionToken() || picked != levelPickedSurfaceFaces()
			|| insertionVisibility != m_levelMapDocument.insertionVisibility
			|| nudgeAfterDuplicate != m_levelControls.nudgeAfterDuplicate || duplicateRequiresObjectMode != m_levelControls.duplicateRequiresObjectMode) {
			if (error) {
				*error = tr("The map, selection, repeat commands, scene destination, placement policy or package changed. Reopen placement from the current state.");
			}
			return false;
		}
		if (!readOnly) { m_levelMapDocument = candidate; }
		return true;
	};
}
bool ApplicationShell::runLevelPlacementFromUi(const LevelPlacementRequest& request, QString* error) {
	if (levelComponentsActive()) {
		LevelComponentTransformRequest transform;
		applyLevelComponentUvSettings(&transform);
		switch (request.operation) {
		case LevelPlacementOperation::Move:
			transform.delta = request.snapMoveDelta ? snapLevelMapPosition(request.offset, request.grid) : request.offset;
			return applyLevelComponentTransform(transform, error);
		case LevelPlacementOperation::Rotate:
			if (request.rotation.euler) {
				// Arbitrary Rotation's X, then Y, then Z turn as one affine map.
				const auto steps = levelMapRotationSteps(request.rotation);
				if (steps.isEmpty()) { if (error) { *error = tr("A whole number of full turns changes nothing."); } return false; }
				transform.operation = LevelComponentTransformKind::Affine;
				for (auto& column : transform.linear) {
					for (const auto& step : steps) { column = rotateLevelVector(column, step.first, step.second); }
				}
				transform.delta = {0, 0, 0, true};
				transform.pivot = request.rotation.pivot;
				return applyLevelComponentTransform(transform, error);
			}
			transform.operation = LevelComponentTransformKind::Rotate;
			transform.axis = request.rotation.axis; transform.amount = request.rotation.degrees; transform.pivot = request.rotation.pivot;
			return applyLevelComponentTransform(transform, error);
		case LevelPlacementOperation::Scale:
			transform.operation = LevelComponentTransformKind::Scale;
			transform.scale = {request.scale.factors[0], request.scale.factors[1], request.scale.factors[2], true};
			transform.pivot = request.scale.pivot;
			return applyLevelComponentTransform(transform, error);
		case LevelPlacementOperation::Mirror:
			transform.operation = LevelComponentTransformKind::Scale;
			transform.pivot = request.mirrorPivot;
			if (request.axis == 0) { transform.scale.x = -1; }
			else if (request.axis == 1) { transform.scale.y = -1; }
			else if (request.axis == 2) { transform.scale.z = -1; }
			else { if (error) { *error = tr("Choose a valid mirror axis."); } return false; }
			return applyLevelComponentTransform(transform, error);
		case LevelPlacementOperation::Snap:
			transform.grid = request.grid;
			return applyLevelComponentTransform(transform, error);
		case LevelPlacementOperation::Resize:
			if (error) { *error = tr("Use Transform Components to scale the picked components."); }
			return false;
		default: break;
		}
	}
	const auto publish = levelPlacementCommitter();
	const auto result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, request);
	if (!result.succeeded) {
		if (error) {
			*error = result.error;
		}
		return false;
	}
	return publish(result.document, error);
}
void ApplicationShell::placeLevelMapObjectsFromUi(bool paste) {
	const bool textMap = m_levelMapDocument.format == LevelMapFormat::QuakeMap || m_levelMapDocument.format == LevelMapFormat::Quake3Map;
	if ((paste && !textMap) || (!paste && !canDuplicateLevelMapSelection())) {
		return;
	}
	auto archive =
		m_packageStaging.isLoaded() ? std::make_shared<PackageStagingArchive>(m_packageStaging) : m_packageArchive.snapshotReader();
	if (!archive && m_packageArchive.isOpen()) {
		archive = std::make_shared<const PackageArchive>(m_packageArchive);
	}
	LevelPlacementDialog dialog(m_levelMapDocument, paste ? LevelPlacementMode::Paste : LevelPlacementMode::Duplicate,
								paste ? QGuiApplication::clipboard()->text() : QString(), archive, activePaletteId(), this);
	dialog.setTextureLock(m_settings.levelTextureLock());
	const double step = m_levelMapViewport ? m_levelMapViewport->gridSize() : 16.0;
	dialog.setOffset({step, 0, 0, true});
	const auto publish = levelPlacementCommitter();
	dialog.setApplyHandler([this, publish](const LevelMapDocument& candidate, QString* error) {
		if (!publish(candidate, error)) {
			return false;
		}
		recordActivity(tr("Level objects placed"), m_levelMapDocument.sourcePath, QStringLiteral("level-map"), OperationState::Warning,
					   tr("Unsaved map edit"));
		refreshLevelMapWorkbench();
		statusBar()->showMessage(tr("Objects placed as one undo step. Save the map before building."));
		return true;
	});
	dialog.exec();
}
} // namespace vibestudio
