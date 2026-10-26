#pragma once

#include "core/level_editor_controls.h"
#include "core/level_map.h"
#include "core/map_render.h"

#include <QString>
#include <QVector>

// Directional nudges: the one-grid-step moves that the arrow keys, Page Up and
// Page Down, and the keypad's plus and minus make from a Levels view.
//
// Left, Right, Forward and Back follow the view. In a plan view they move
// along its screen axes, Forward being up the screen. In the camera they move
// along the horizontal world axis nearest the camera's view and along its
// right axis snapped the same way, as TrenchBroom's MapView3D::moveDirection
// does: looking straight down or up, Forward follows the camera's up axis.
// Depth Up and Depth Down move along a plan view's depth axis, towards and
// away from the viewer (Top +Z, Front -Y, Side +X), and along world Z in the
// camera. Z Up and Z Down always move along world Z, as NetRadiant Custom's
// MoveSelectionUP and MoveSelectionDOWN do from any view.
//
// The edits themselves are the ordinary move and component transform services
// (moveLevelMapSelection(), transformLevelMapComponents()), so texture lock,
// scene locks, open-group context and undo behave as for every other move.
//
// Workflow reference: TrenchBroom lib/TbUiLib/src/ActionManager.cpp "Move
// objects" actions, MapView2D::moveDirection and MapView3D::moveDirection at
// 4c756be1 (GPL-3.0-or-later); NetRadiant Custom radiant/select.cpp
// NudgeSelection, AxisBase_forViewType and Selection_MoveUp/MoveDown at
// 449778b3 (GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.

namespace vibestudio {

enum class LevelNudgeDirection { Left, Right, Forward, Back, DepthUp, DepthDown, ZUp, ZDown };

struct LevelNudgeCommand {
	// The shell's action id, such as map.nudgeLeft.
	QString commandId;
	// The CLI's --direction name, such as left.
	QString name;
	LevelNudgeDirection direction = LevelNudgeDirection::Left;
};

// Left, Right, Forward, Back, Depth Up, Depth Down, Z Up and Z Down, in that order.
[[nodiscard]] const QVector<LevelNudgeCommand>& levelNudgeCommands();
bool levelNudgeCommandForId(const QString& commandId, LevelNudgeCommand* command = nullptr);
bool levelNudgeCommandForName(const QString& name, LevelNudgeCommand* command = nullptr);
// Every --direction name, comma separated, for usage messages.
[[nodiscard]] QString levelNudgeDirectionNames();

// The unit world axis a nudge moves along in a plan view.
[[nodiscard]] LevelMapVec3 levelPlanNudgeAxis(MapRenderProjection projection, LevelNudgeDirection direction);
// The unit world axis a nudge moves along in the camera, from the camera's
// forward, right and up directions. Invalid when the basis is degenerate.
[[nodiscard]] LevelMapVec3 levelCameraNudgeAxis(const LevelMapVec3& forward, const LevelMapVec3& right, const LevelMapVec3& up,
	LevelNudgeDirection direction);
// The same for a camera that looks along `yawDegrees` (0 along +X, counter-
// clockwise) and `pitchDegrees` (up positive), as the CLI names a view.
[[nodiscard]] LevelMapVec3 levelCameraNudgeAxis(double yawDegrees, double pitchDegrees, LevelNudgeDirection direction);
// That camera's unit forward, screen-right and screen-up directions, with no
// roll; pitch is clamped to -90..90. False for a non-finite angle.
bool levelCameraBasis(double yawDegrees, double pitchDegrees, LevelMapVec3* forward, LevelMapVec3* right, LevelMapVec3* up);
// Doom-format geometry lies in the plane, so those maps nudge along X and Y
// only; every Quake-family map takes all three axes. False for no map.
[[nodiscard]] bool levelNudgeAxisSupported(const LevelMapDocument& document, const LevelMapVec3& axis);
// `steps` grid steps of `grid` units along `axis`; invalid for a non-finite or
// non-positive grid, an invalid axis or a step count outside 1 to 64.
[[nodiscard]] LevelMapVec3 levelNudgeDelta(const LevelMapVec3& axis, double grid, int steps = 1);
// `delta` in a plan view's screen coordinates (right, up). False when it has a
// component along the view's depth axis, which no in-plane move can make.
bool levelPlanNudgePlaneDelta(MapRenderProjection projection, const LevelMapVec3& delta, double* right, double* up);
// Radiant-family profiles drive the camera with the arrow keys, so their
// nudges use the last active plan view's axes from every view, the camera
// included, as NetRadiant Custom's do; the others nudge camera-relative.
[[nodiscard]] bool levelNudgeCameraFollowsPlan(const LevelEditorControls& controls);

// Duplicate and Move: TrenchBroom's Ctrl+arrow and Ctrl+Page Up/Down. Each
// copies the selection and moves the copies one grid step along the same
// axis the matching nudge uses (Left, Right, Forward, Back, Depth Up, Depth
// Down), in one duplicateLevelMapSelection() call: one undo step and one
// Repeat entry, the copies selected. Doom maps copy things only, in the plane.
//
// Workflow reference: TrenchBroom lib/TbUiLib/src/ActionManager.cpp
// "Duplicate and move objects" actions and MapViewBase::duplicateAndMoveObjects
// at 4c756be1 (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.

// Left, Right, Forward, Back, Depth Up and Depth Down, in that order. The
// command ids are map.duplicateMoveLeft and so on; the names are the nudge
// --direction names.
[[nodiscard]] const QVector<LevelNudgeCommand>& levelDuplicateMoveCommands();
// Also accepts the palette's hyphenated spelling, such as map.duplicate-move-left.
bool levelDuplicateMoveCommandForId(const QString& commandId, LevelNudgeCommand* command = nullptr);

} // namespace vibestudio
