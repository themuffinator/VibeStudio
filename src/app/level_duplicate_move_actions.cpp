// Duplicate and Move, Move to Camera and Repeat Transforms on the Levels page.
//
// Duplicate and Move Left, Right, Forward, Back, Depth Up and Depth Down
// (core/level_nudge.h) copy the selection and move the copies one grid step
// along the axis the matching nudge takes from the plan view or the camera,
// as one undo step and one Repeat entry. The views claim their keys
// (TrenchBroom's Ctrl+arrows and Ctrl+Page Up/Down) while a copy can be made,
// as they do for the nudges (app/level_nudge_actions.cpp), so with nothing
// selected Ctrl+arrows still pan a plan view, and on a Doom map Ctrl+Page Up
// and Ctrl+Page Down still raise and lower ceilings.
//
// Move to Camera (core/level_paste.h) centres the selection, or the picked
// components, on the camera by Paste to Camera's grid-snapped translation.
// Repeat Transforms (core/level_repeat.h) copies the selection and applies the
// last recorded translation, rotation, scale and shear to the copies, or to
// the picked components in a component mode.
//
// Workflow reference: TrenchBroom ActionManager "Duplicate and move objects"
// actions at 4c756be1 (GPL-3.0-or-later); NetRadiant Custom radiant/select.cpp
// MoveToCamera, TranslateToCamera and "RepeatTransforms", and radiant/
// selection.cpp RadiantSelectionSystem::repeatTransforms at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_placement_task_dialog.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_brush.h"
#include "core/level_nudge.h"
#include "core/level_paste.h"
#include "core/level_placement.h"
#include "core/level_repeat.h"

#include <QAction>
#include <QStatusBar>

namespace vibestudio {
namespace {

bool textMapFormat(const LevelMapDocument& document)
{
	return document.format == LevelMapFormat::QuakeMap || document.format == LevelMapFormat::Quake3Map;
}

bool depthDirection(LevelNudgeDirection direction)
{
	return direction == LevelNudgeDirection::DepthUp || direction == LevelNudgeDirection::DepthDown;
}

} // namespace

void ApplicationShell::registerLevelDuplicateMoveCommands()
{
	struct Entry {
		QString label;
		QString tip;
	};
	// Same order as levelDuplicateMoveCommands().
	const QVector<Entry> entries {
		{tr("Duplicate and Move Left"),
			tr("Copy the selection and move the copies one grid step left in the plan view, or along the camera's nearest horizontal axis, as one undo step.")},
		{tr("Duplicate and Move Right"),
			tr("Copy the selection and move the copies one grid step right in the plan view, or along the camera's nearest horizontal axis, as one undo step.")},
		{tr("Duplicate and Move Forward"),
			tr("Copy the selection and move the copies one grid step up the plan view, or forward along the camera's nearest horizontal axis, as one undo step.")},
		{tr("Duplicate and Move Back"),
			tr("Copy the selection and move the copies one grid step down the plan view, or back along the camera's nearest horizontal axis, as one undo step.")},
		{tr("Duplicate and Move Depth Up"),
			tr("Copy the selection and move the copies one grid step towards you along the plan view's depth axis, or up world Z from the camera, as one undo step.")},
		{tr("Duplicate and Move Depth Down"),
			tr("Copy the selection and move the copies one grid step away from you along the plan view's depth axis, or down world Z from the camera, as one undo step.")},
	};
	const QVector<LevelNudgeCommand>& commands = levelDuplicateMoveCommands();
	const QString section = tr("Duplicate and Move");
	for (int index = 0; index < commands.size() && index < entries.size(); ++index) {
		StudioCommandRegistration command;
		command.commandId = commands.at(index).commandId;
		command.group = StudioCommandGroup::Edit;
		command.label = entries.at(index).label;
		command.statusTip = entries.at(index).tip;
		command.iconName = QStringLiteral("copy");
		command.menuSection = section;
		command.handler = [this, id = commands.at(index).commandId]() {
			setMode(StudioMode::Levels);
			duplicateMoveLevelSelectionFromUi(id);
		};
		m_commands->registerCommand(command);
	}
	const auto add = [this](const QString& id, const QString& label, const QString& tip, const QString& icon, const QString& menu,
						 std::function<void()> handler) {
		StudioCommandRegistration command;
		command.commandId = id;
		command.group = StudioCommandGroup::Edit;
		command.label = label;
		command.statusTip = tip;
		command.iconName = icon;
		command.menuSection = menu;
		command.handler = [this, handler = std::move(handler)]() {
			setMode(StudioMode::Levels);
			handler();
		};
		m_commands->registerCommand(command);
	};
	add(QStringLiteral("map.moveToCamera"), tr("Move to Camera"),
		tr("Move the selection, or the picked components, so its centre goes to the camera or the orbit view's focus centre, by a "
		   "grid-snapped translation, as one undo step."),
		QStringLiteral("move"), section, [this]() { moveLevelSelectionToCameraFromUi(); });
	const QString repeat = tr("Repeat");
	add(QStringLiteral("map.repeatTransforms"), tr("Repeat Transforms"),
		tr("Copy the selection where it is and apply the last recorded translation, rotation, scale and shear to the copies about their "
		   "centre, as one undo step; in a component mode, transform the picked components instead."),
		QStringLiteral("refresh"), repeat, [this]() { repeatLevelMapTransformsFromUi(); });
	add(QStringLiteral("map.resetRepeatTransforms"), tr("Reset Repeat Transforms"),
		tr("Forget the recorded translation, rotation, scale and shear without changing the map or its undo history."),
		QStringLiteral("close"), repeat, [this]() {
			resetLevelMapRepeatTransforms(&m_levelMapDocument);
			refreshLevelMapWorkbench();
			statusBar()->showMessage(tr("Repeat transforms reset."), 5000);
		});
}

void ApplicationShell::refreshLevelDuplicateMoveCommands()
{
	if (!m_commands) {
		return;
	}
	const LevelMapDocument& document = m_levelMapDocument;
	const bool open = document.format != LevelMapFormat::Unknown && m_levelMapViewport;
	const bool doom = document.format == LevelMapFormat::DoomWad;
	const bool components = open && levelComponentsActive();
	// Copies are whole objects: a component mode or the sweep tool gives the
	// keys back to the view (Ctrl+arrows pan a plan view).
	const bool duplicable = open && !components && !levelSweepToolActive() && canDuplicateLevelMapSelection();
	for (const LevelNudgeCommand& command : levelDuplicateMoveCommands()) {
		// Doom geometry stays in the plane; Ctrl+Page Up and Down go on to
		// Raise Ceiling and Lower Ceiling there.
		m_commands->setEnabled(command.commandId, duplicable && !(doom && depthDirection(command.direction)));
	}
	const bool picked = components && (!m_levelMapViewport->pickedComponents().isEmpty() || !levelComponentFacePicks().faces.isEmpty());
	const bool target = components ? picked : open && !document.selection.isEmpty();
	const bool text = open && textMapFormat(document);
	m_commands->setEnabled(QStringLiteral("map.moveToCamera"), text && m_levelMap3D && target);
	m_commands->setEnabled(QStringLiteral("map.repeatTransforms"), text && target && !document.repeat.transforms.isIdentity());
	m_commands->setEnabled(QStringLiteral("map.resetRepeatTransforms"), !document.repeat.transforms.isIdentity());
}

void ApplicationShell::duplicateMoveLevelSelectionFromUi(const QString& commandId, QWidget* source)
{
	LevelNudgeCommand command;
	if (!levelDuplicateMoveCommandForId(commandId, &command)) {
		return;
	}
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a map before duplicating."));
		return;
	}
	if (levelComponentsActive()) {
		statusBar()->showMessage(tr("Duplicate and Move copies whole objects; leave vertex, edge or face mode first."));
		return;
	}
	if (!canDuplicateLevelMapSelection()) {
		statusBar()->showMessage(tr("Select duplicable map objects and use the selection mode required by your profile."));
		return;
	}
	const LevelMapVec3 axis = levelNudgeAxisFromUi(command.direction, source);
	if (!axis.valid) {
		statusBar()->showMessage(tr("The view has no direction to move the copies along."));
		return;
	}
	if (!levelNudgeAxisSupported(m_levelMapDocument, axis)) {
		statusBar()->showMessage(tr("Doom maps copy things along X and Y only."));
		return;
	}
	const LevelMapVec3 delta = levelNudgeDelta(axis, levelMapGridUnits());
	if (!delta.valid) {
		return;
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::Duplicate;
	request.offset = delta;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	QString error;
	if (!runLevelPlacementFromUi(request, &error)) {
		statusBar()->showMessage(tr("Could not duplicate: %1").arg(error));
		return;
	}
	const int count = static_cast<int>(m_levelMapDocument.selection.size());
	const QString offset = tr("%1, %2, %3").arg(delta.x).arg(delta.y).arg(delta.z);
	recordActivity(tr("Level map objects duplicated and moved"), offset, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Copied %n object(s) and moved the copies by %1; the copies are selected.", nullptr, count).arg(offset));
}

void ApplicationShell::moveLevelSelectionToCameraFromUi()
{
	if (!textMapFormat(m_levelMapDocument) || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Move to Camera works on Quake-family .map files."));
		return;
	}
	LevelPastePlacement placement;
	QString error;
	if (!levelCameraAnchor(&placement.point, &error)) {
		statusBar()->showMessage(error);
		return;
	}
	placement.mode = LevelPasteMode::Camera;
	placement.grid = m_levelMapViewport->snapToGrid() ? m_levelMapViewport->gridSize() : 0;
	if (levelComponentsActive()) {
		// NetRadiant Custom moves the picked components in a component mode.
		const QVector<LevelMapComponentSet> sets = levelComponentTransformSets(&error);
		LevelMapVec3 low;
		LevelMapVec3 high;
		if (!error.isEmpty() || sets.isEmpty() || !levelMapComponentBounds(m_levelMapDocument, sets, &low, &high, &error)) {
			statusBar()->showMessage(error.isEmpty() ? tr("Pick vertices, edges or faces to move to the camera.") : error);
			return;
		}
		const LevelMapVec3 centre {(low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5, true};
		LevelComponentTransformRequest move;
		move.delta = {placement.point.x - centre.x, placement.point.y - centre.y, placement.point.z - centre.z, true};
		if (placement.grid > 0) {
			move.delta = {snapLevelMapCoordinate(move.delta.x, placement.grid), snapLevelMapCoordinate(move.delta.y, placement.grid),
				snapLevelMapCoordinate(move.delta.z, placement.grid), true};
		}
		if (move.delta.x == 0 && move.delta.y == 0 && move.delta.z == 0) {
			statusBar()->showMessage(tr("The picked components are already centred on the camera at this grid size."));
			return;
		}
		applyLevelComponentUvSettings(&move);
		if (!applyLevelComponentTransform(move, &error)) {
			statusBar()->showMessage(tr("Picked components unchanged: %1").arg(error));
			return;
		}
		statusBar()->showMessage(tr("Moved the picked components by %1, %2, %3 to centre them on the camera.")
			.arg(move.delta.x).arg(move.delta.y).arg(move.delta.z));
		return;
	}
	if (m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(tr("Select objects to move to the camera."));
		return;
	}
	fillLevelPlacementEntityBounds(&placement);
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::MoveToCamera;
	request.paste = placement;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	const auto publish = levelPlacementCommitter();
	const LevelPlacementResult result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, request);
	if (!result.succeeded || !publish(result.document, &error)) {
		statusBar()->showMessage(tr("Could not move to the camera: %1").arg(result.succeeded ? error : result.error));
		return;
	}
	const LevelMapVec3 offset = result.pasteOffset;
	const int count = static_cast<int>(m_levelMapDocument.selection.size());
	recordActivity(tr("Level map selection moved to the camera"), tr("%1, %2, %3").arg(offset.x).arg(offset.y).arg(offset.z),
		QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Moved %n object(s) by %1, %2, %3 to centre them on the camera.", nullptr, count)
		.arg(offset.x).arg(offset.y).arg(offset.z));
}

void ApplicationShell::repeatLevelMapTransformsFromUi()
{
	const LevelRepeatTransforms transforms = m_levelMapDocument.repeat.transforms;
	if (transforms.isIdentity()) {
		statusBar()->showMessage(tr("There is no transform to repeat yet. Move, rotate, scale or shear something first."));
		return;
	}
	const QString applied = levelRepeatTransformsDescription(transforms).join(QStringLiteral("; "));
	QString error;
	if (levelComponentsActive()) {
		LevelComponentTransformRequest request;
		applyLevelComponentUvSettings(&request);
		if (!levelRepeatTransformsComponentRequest(transforms, &request, &error) || !applyLevelComponentTransform(request, &error)) {
			statusBar()->showMessage(tr("Picked components unchanged: %1").arg(error));
			return;
		}
		statusBar()->showMessage(tr("Repeated %1 on the picked components as one undo step.").arg(applied), 7000);
		return;
	}
	if (!canRepeatLevelMapTransforms(m_levelMapDocument)) {
		statusBar()->showMessage(tr("Select objects in a Quake-family map to repeat the transforms on."));
		return;
	}
	LevelPlacementRequest request;
	request.operation = LevelPlacementOperation::RepeatTransforms;
	request.textures = {m_settings.levelTextureLock(), m_settings.levelAllowValve220()};
	const auto publish = levelPlacementCommitter();
	const LevelPlacementResult result = LevelPlacementTaskDialog::prepare(this, m_levelMapDocument, request);
	if (!result.succeeded || !publish(result.document, &error)) {
		statusBar()->showMessage(tr("Could not repeat the transforms: %1").arg(result.succeeded ? error : result.error), 8000);
		return;
	}
	const int count = static_cast<int>(m_levelMapDocument.selection.size());
	const LevelMapVec3 pivot = result.repeatPivot;
	recordActivity(tr("Level map transforms repeated"), applied, QStringLiteral("level-map"), OperationState::Warning, tr("Unsaved map edit"));
	refreshLevelMapWorkbench();
	statusBar()->showMessage(tr("Copied %n object(s) and repeated %1 about %2, %3, %4 as one undo step.", nullptr, count)
		.arg(applied).arg(pivot.x).arg(pivot.y).arg(pivot.z), 7000);
}

} // namespace vibestudio
