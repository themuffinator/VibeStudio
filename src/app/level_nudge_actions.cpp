// Directional and depth nudge commands (core/level_nudge.h): Nudge Left,
// Right, Forward and Back, Nudge Depth Up and Down, and Nudge Up and Down
// Along Z. Each moves, one grid step, the first of: the sweep destination
// while the sweep tool is on; the shared component pivot while Rotate
// Components or Move Component Pivot is chosen; the picked vertices, edges,
// faces and Select Textured faces; or the selected objects. The axes come
// from the plan view that has focus or was used last, or from the camera.
// Object and component moves are the same placement services the arrow keys
// and Transform Components use, so each nudge is one undo step that keeps
// scene locks, texture lock and UV lock.
//
// The keys belong to the editor profile (core/level_editor_controls.cpp) and
// to the user's own shortcuts. The Levels views claim them ahead of window-
// wide commands, such as Go Back's Alt+Left, while a nudge can act; otherwise
// the key goes on to its other owner or to the view, which pans with the
// arrows when nothing is selected. The view-relative turn and flip commands
// (app/level_axis_transform_actions.cpp) claim their keys the same way.
//
// Workflow reference: TrenchBroom ActionManager "Move objects" actions and
// MapViewBase::move (rotation centre, node handles, sweep centre, objects) at
// 4c756be1 (GPL-3.0-or-later); NetRadiant Custom radiant/select.cpp
// SelectNudgeLeft/Right/Up/Down and MoveSelectionUP/DOWN at 449778b3
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "app/application_shell.h"
#include "app/level_sweep_gizmo.h"
#include "app/map_viewport.h"
#include "app/model_viewport.h"
#include "app/studio_actions.h"
#include "core/level_camera_keys.h"
#include "core/level_camera_texture.h"
#include "core/level_nudge.h"
#include "core/level_placement.h"
#include "core/level_quick_transform.h"

#include <QAction>
#include <QKeyEvent>
#include <QStatusBar>

#include <algorithm>

namespace vibestudio {
namespace {

MapRenderProjection renderProjection(MapViewportProjection projection)
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

LevelMapVec3 mapVector(const ModelVec3& value)
{
	return {value.x, value.y, value.z, true};
}

bool arrowKey(int key)
{
	return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
}

// Arrows and paging keys can arrive with the keypad flag (macOS sets it on
// every arrow); they match their plain binding. The keypad's own plus and
// minus keep the flag, so they stay apart from the main keyboard's.
QKeyCombination normalizedNudgeKey(QKeyCombination combination)
{
	const int key = combination.key();
	if (arrowKey(key) || key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Home || key == Qt::Key_End) {
		return QKeyCombination(combination.keyboardModifiers() & ~Qt::KeypadModifier, combination.key());
	}
	return combination;
}

bool verticalNudge(LevelNudgeDirection direction)
{
	return direction == LevelNudgeDirection::DepthUp || direction == LevelNudgeDirection::DepthDown
		|| direction == LevelNudgeDirection::ZUp || direction == LevelNudgeDirection::ZDown;
}

} // namespace

void ApplicationShell::registerLevelNudgeCommands()
{
	struct Entry {
		QString label;
		QString tip;
		QString icon;
	};
	// Same order as levelNudgeCommands().
	const QVector<Entry> entries {
		{tr("Nudge Left"), tr("Move the selection one grid step left in the plan view, or along the camera's nearest horizontal axis."),
			QStringLiteral("chevron-left")},
		{tr("Nudge Right"), tr("Move the selection one grid step right in the plan view, or along the camera's nearest horizontal axis."),
			QStringLiteral("chevron-right")},
		{tr("Nudge Forward"), tr("Move the selection one grid step up the plan view, or forward along the camera's nearest horizontal axis."),
			QStringLiteral("chevron-up")},
		{tr("Nudge Back"), tr("Move the selection one grid step down the plan view, or back along the camera's nearest horizontal axis."),
			QStringLiteral("chevron-down")},
		{tr("Nudge Depth Up"), tr("Move the selection one grid step towards you along the plan view's depth axis, or up world Z from the camera."),
			QStringLiteral("chevron-up")},
		{tr("Nudge Depth Down"), tr("Move the selection one grid step away from you along the plan view's depth axis, or down world Z from the camera."),
			QStringLiteral("chevron-down")},
		{tr("Nudge Up Along Z"), tr("Move the selection one grid step up world Z, whichever view is active."), QStringLiteral("chevron-up")},
		{tr("Nudge Down Along Z"), tr("Move the selection one grid step down world Z, whichever view is active."), QStringLiteral("chevron-down")},
	};
	const QVector<LevelNudgeCommand>& commands = levelNudgeCommands();
	for (int index = 0; index < commands.size() && index < entries.size(); ++index) {
		StudioCommandRegistration command;
		command.commandId = commands.at(index).commandId;
		command.group = StudioCommandGroup::Edit;
		command.label = entries.at(index).label;
		command.statusTip = entries.at(index).tip;
		command.iconName = entries.at(index).icon;
		command.menuSection = tr("Nudge");
		command.handler = [this, id = commands.at(index).commandId]() {
			setMode(StudioMode::Levels);
			nudgeLevelSelectionFromUi(id);
		};
		m_commands->registerCommand(command);
	}
	registerLevelDuplicateMoveCommands();
}

void ApplicationShell::refreshLevelNudgeCommands()
{
	if (!m_commands) {
		return;
	}
	const bool open = m_levelMapDocument.format != LevelMapFormat::Unknown && m_levelMapViewport;
	const bool doom = m_levelMapDocument.format == LevelMapFormat::DoomWad;
	bool target = open && levelSweepToolActive();
	if (open && !target) {
		target = levelComponentsActive()
			? !m_levelMapViewport->pickedComponents().isEmpty() || !levelComponentFacePicks().faces.isEmpty()
			: !m_levelMapDocument.selection.isEmpty();
	}
	for (const LevelNudgeCommand& command : levelNudgeCommands()) {
		// Doom geometry stays in the plane; Page Up and Page Down go on to
		// Raise Floor and Lower Floor there.
		m_commands->setEnabled(command.commandId, target && !(doom && verticalNudge(command.direction)));
	}
	refreshLevelDuplicateMoveCommands();
}

QString ApplicationShell::levelNudgeCommandForKey(QKeyCombination combination, int* steps) const
{
	if (!m_commands) {
		return {};
	}
	combination = normalizedNudgeKey(combination);
	const auto bound = [this](const QString& id, QKeyCombination key) {
		const QAction* action = m_commands->action(id);
		if (!action || !action->isEnabled()) {
			return false;
		}
		for (const QKeySequence& sequence : m_commands->shortcutSequences(id)) {
			if (sequence.count() == 1 && normalizedNudgeKey(sequence[0]) == key) {
				return true;
			}
		}
		return false;
	};
	for (const LevelNudgeCommand& command : levelNudgeCommands()) {
		if (bound(command.commandId, combination)) {
			if (steps) {
				*steps = 1;
			}
			return command.commandId;
		}
	}
	// Duplicate and Move (TrenchBroom's Ctrl+arrows and Ctrl+Page Up/Down,
	// app/level_duplicate_move_actions.cpp) and the view-relative turns and
	// flips (its Alt+arrows, Alt+Page Up/Down, Ctrl+F and Ctrl+Alt+F,
	// app/level_axis_transform_actions.cpp) act in the views the same way.
	for (const LevelNudgeCommand& command : levelDuplicateMoveCommands()) {
		if (bound(command.commandId, combination)) {
			if (steps) {
				*steps = 1;
			}
			return command.commandId;
		}
	}
	for (const LevelViewTransform& transform : levelViewTransforms()) {
		if (bound(transform.commandId, combination)) {
			if (steps) {
				*steps = 1;
			}
			return transform.commandId;
		}
	}
	// Shift moves eight grid steps, where no command answers the Shift chord
	// in the views (Radiant's Shift+arrows shift textures instead). The
	// camera's texture moves take their Shift chords only while faces are
	// picked (handleLevelNudgeKey), so they leave the chord to the nudge.
	if (combination.keyboardModifiers().testFlag(Qt::ShiftModifier)) {
		const QKeyCombination plain(combination.keyboardModifiers() & ~Qt::ShiftModifier, combination.key());
		const QString chord = QKeySequence(combination).toString(QKeySequence::PortableText);
		for (const LevelNudgeCommand& command : levelNudgeCommands()) {
			QStringList users = m_commands->commandsUsingShortcut(chord, command.commandId);
			users.removeIf([](const QString& id) { return levelCameraTextureCommandForId(id); });
			if (bound(command.commandId, plain) && users.isEmpty()) {
				if (steps) {
					*steps = 8;
				}
				return command.commandId;
			}
		}
	}
	return {};
}

bool ApplicationShell::handleLevelNudgeKey(QObject* watched, QEvent* event)
{
	if ((event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) || !m_commands) {
		return false;
	}
	MapViewport* plan = nullptr;
	for (auto* view : std::as_const(m_levelPlanViews)) {
		if (view == watched) {
			plan = view;
			break;
		}
	}
	const bool camera = m_levelMap3D && watched == m_levelMap3D;
	if (!plan && !camera) {
		return false;
	}
	auto* key = static_cast<QKeyEvent*>(event);
	if (event->type() == QEvent::ShortcutOverride) {
		// Availability is read fresh, so the claim never lags a selection change.
		refreshLevelNudgeCommands();
		refreshLevelAxisTransformCommands();
		if (camera) {
			// The camera texture moves follow the Surfaces target and the
			// picked faces; refresh them only for a key one of them uses.
			const QKeyCombination pressed = normalizedNudgeKey(key->keyCombination());
			bool textureKey = false;
			for (const LevelCameraTextureCommand& command : levelCameraTextureCommands()) {
				for (const QKeySequence& sequence : m_commands->shortcutSequences(command.commandId)) {
					textureKey |= sequence.count() == 1 && normalizedNudgeKey(sequence[0]) == pressed;
				}
			}
			if (textureKey) {
				refreshLevelSurfaceTools();
			}
		}
	}
	int steps = 1;
	// In the camera, TrenchBroom's texture moves and flips come first while
	// faces are picked (its face selection), then the nudges, turns and flips
	// of objects; a texture command still takes a key nothing else wants.
	QString commandId = camera ? levelCameraTextureCommandForKey(key->keyCombination(), true) : QString();
	if (commandId.isEmpty()) {
		commandId = levelNudgeCommandForKey(key->keyCombination(), &steps);
	}
	if (commandId.isEmpty() && camera) {
		commandId = levelCameraTextureCommandForKey(key->keyCombination(), false);
	}
	if (commandId.isEmpty()) {
		return false;
	}
	// Claiming the key keeps a window-wide owner such as Go Back from firing
	// in the view; the press then decides who acts on it.
	event->accept();
	if (event->type() == QEvent::ShortcutOverride) {
		return true;
	}
	bool keeps = false;
	if (plan) {
		keeps = plan->keepsKey(key->key(), key->modifiers());
	} else if (m_levelMap3D->isMovingComponents()) {
		keeps = true;
	} else if (!m_levelMap3D->isPerspective()) {
		// The orbiting camera turns and pans with the arrow keys.
		keeps = arrowKey(key->key());
	} else {
		// The camera's own flying and driving keys stay its own; a chord the
		// profile gives a nudge (NRC's Alt+arrows) wins over the same key
		// driven slower or faster, except while flying in free look.
		const CameraViewControls& controls = m_levelMap3D->cameraControls();
		const bool looking = m_levelMap3D->isLooking();
		const bool plain = (key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
		keeps = ((plain || looking) && cameraKeyMotions(controls, looking, Qt::NoButton, Qt::NoModifier, key->modifiers()).contains(key->key()))
			|| cameraLookToggleMatches(controls, key->key(), key->modifiers())
			|| navigationHoldKeyMatches(controls.lookHoldKey, key->key(), key->modifiers());
	}
	if (keeps) {
		// The view's own keys and interactions come first.
		return false;
	}
	QWidget* view = plan ? static_cast<QWidget*>(plan) : static_cast<QWidget*>(m_levelMap3D);
	if (levelCameraTextureCommandForId(commandId)) {
		QString error;
		if (!runLevelCameraTextureCommand(commandId, &error) && !error.isEmpty()) {
			statusBar()->showMessage(error, 6000);
		}
	} else if (levelNudgeCommandForId(commandId)) {
		nudgeLevelSelectionFromUi(commandId, steps, view);
	} else if (levelDuplicateMoveCommandForId(commandId)) {
		duplicateMoveLevelSelectionFromUi(commandId, view);
	} else {
		applyLevelViewTransformFromUi(commandId, view);
	}
	return true;
}

LevelMapVec3 ApplicationShell::levelNudgeAxisFromUi(LevelNudgeDirection direction, QWidget* source, MapViewport** planOut, bool* cameraOut)
{
	QWidget* from = source ? source : m_levelLastFocusedView;
	MapViewport* plan = nullptr;
	for (auto* view : std::as_const(m_levelPlanViews)) {
		if (view == from) {
			plan = view;
		}
	}
	if (plan) {
		activateLevelPlanViewport(plan);
	}
	const bool camera = !plan && m_levelMap3D && from == m_levelMap3D && !levelNudgeCameraFollowsPlan(m_levelControls);
	if (!plan) {
		plan = m_levelMapViewport;
	}
	if (planOut) {
		*planOut = plan;
	}
	if (cameraOut) {
		*cameraOut = camera;
	}
	if (camera) {
		ModelVec3 right;
		ModelVec3 up;
		m_levelMap3D->viewAxes(&right, &up);
		return levelCameraNudgeAxis(mapVector(m_levelMap3D->viewForward()), mapVector(right), mapVector(up), direction);
	}
	return plan ? levelPlanNudgeAxis(renderProjection(plan->projection()), direction) : LevelMapVec3 {};
}

void ApplicationShell::nudgeLevelSelectionFromUi(const QString& commandId, int steps, QWidget* source)
{
	LevelNudgeCommand command;
	if (!levelNudgeCommandForId(commandId, &command)) {
		return;
	}
	if (m_levelMapDocument.format == LevelMapFormat::Unknown || !m_levelMapViewport) {
		statusBar()->showMessage(tr("Open a map before nudging."));
		return;
	}
	MapViewport* plan = nullptr;
	bool camera = false;
	const LevelMapVec3 axis = levelNudgeAxisFromUi(command.direction, source, &plan, &camera);
	if (!axis.valid) {
		statusBar()->showMessage(tr("The view has no direction to nudge along."));
		return;
	}
	if (!levelNudgeAxisSupported(m_levelMapDocument, axis)) {
		statusBar()->showMessage(tr("Doom maps nudge along X and Y only; Raise Floor and Lower Floor change heights."));
		return;
	}
	const LevelMapVec3 delta = levelNudgeDelta(axis, levelMapGridUnits(), std::clamp(steps, 1, 64));
	if (!delta.valid) {
		return;
	}
	const QString offset = tr("%1, %2, %3").arg(delta.x).arg(delta.y).arg(delta.z);

	// TrenchBroom's order: a tool's handle before the geometry it edits.
	if (m_levelSweepGizmo) {
		if (m_levelSweepGizmo->translateDestination(delta)) {
			statusBar()->showMessage(tr("Moved the sweep destination by %1.").arg(offset));
		} else {
			statusBar()->showMessage(tr("Finish dragging the sweep destination before nudging it."));
		}
		return;
	}
	if (levelComponentsActive()) {
		const QVector<LevelMapComponentSet> picked = m_levelMapViewport->pickedComponents();
		if (picked.isEmpty() && levelComponentFacePicks().faces.isEmpty()) {
			statusBar()->showMessage(tr("Pick vertices, edges or faces before nudging them."));
			return;
		}
		const auto tool = static_cast<LevelComponentTool>(m_levelComponentTool);
		if (tool == LevelComponentTool::Rotate || tool == LevelComponentTool::Pivot) {
			// Rotate Components and Move Component Pivot nudge the shared pivot,
			// which is a view setting, not a map edit.
			const LevelMapVec3 pivot = m_levelComponentPivot.valid ? m_levelComponentPivot : m_levelMapViewport->componentGizmo()->pivot();
			if (!pivot.valid) {
				statusBar()->showMessage(tr("The picked components have no pivot to nudge."));
				return;
			}
			m_levelComponentPivot = {pivot.x + delta.x, pivot.y + delta.y, pivot.z + delta.z, true};
			refreshLevelComponentGizmos();
			refreshLevelGeometryCommands();
			statusBar()->showMessage(tr("Moved the component pivot to %1, %2, %3. Reset Component Pivot restores the selection centre.")
				.arg(m_levelComponentPivot.x).arg(m_levelComponentPivot.y).arg(m_levelComponentPivot.z));
			return;
		}
		double right = 0;
		double up = 0;
		if (!camera && !picked.isEmpty() && levelPlanNudgePlaneDelta(renderProjection(plan->projection()), delta, &right, &up)
			&& plan->nudgeComponents(QPointF(right, up))) {
			// The arrow keys' own path: split and extrusion apply.
			return;
		}
		if (m_levelMapViewport->componentSplit() || m_levelComponentExtrusion != 0) {
			statusBar()->showMessage(tr("Split and extrusion nudges move in a plan view's plane; nudge from a plan view that shows the move."));
			return;
		}
		LevelComponentTransformRequest move;
		move.delta = delta;
		applyLevelComponentUvSettings(&move);
		QString error;
		if (!applyLevelComponentTransform(move, &error)) {
			statusBar()->showMessage(tr("Picked components unchanged: %1").arg(error));
		}
		return;
	}
	if (m_levelMapDocument.selection.isEmpty()) {
		statusBar()->showMessage(tr("Select map objects before nudging them."));
		return;
	}
	moveLevelMapSelectionFromViewport(delta.x, delta.y, delta.z);
}

} // namespace vibestudio
