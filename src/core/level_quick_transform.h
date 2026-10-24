#pragma once

#include "core/level_map.h"
#include "core/map_render.h"

#include <QString>
#include <QVector>

// World-axis quick transforms and the pivot every quick turn shares.
//
// Flip X, Flip Y and Flip Z mirror the selection along a world axis, and
// Rotate X, Rotate Y and Rotate Z turn it a quarter turn about one, whatever
// view is active, with NetRadiant Custom's signs (X -90, Y +90, Z -90). The
// edits themselves are flipLevelMapSelection() and rotateLevelMapSelection(),
// so texture lock, entity angles, Repeat recording, scene locks and open-group
// context behave as they do for every other transform.
//
// A quick turn (these three and Rotate 90° Left and Right) pivots on the
// centre of the selection's bounds. Under the profiles whose controls ask for
// it (LevelEditorControls::quickTurnSnapsPivot) that centre is rounded to the
// grid first: a quarter turn about a grid point keeps every on-grid point on
// the grid, so a brush whose two cross sizes differ by an odd number of grid
// steps stays on the grid, at the cost of moving by up to half a grid step.
//
// Workflow reference: NetRadiant Custom radiant/select.cpp Selection_Flipx to
// Selection_Rotatez and radiant/selection.cpp rotateSelected/scaleSelected
// (snapOrigin) at 449778b3 (GPL-2.0-or-later); TrenchBroom MapViewBase::rotate
// (grid reference point) at 4c756be1 (GPL-3.0-or-later). Original
// implementation; see docs/CREDITS.md.

namespace vibestudio {

struct LevelAxisTransform {
	// The shell's action id, such as map.flipX.
	QString commandId;
	bool mirror = false;
	int axis = 0;
	// Signed right-handed quarter turns about +axis; zero for a mirror.
	int quarterTurns = 0;
};

// Flip X, Flip Y, Flip Z, Rotate X, Rotate Y and Rotate Z, in that order.
[[nodiscard]] const QVector<LevelAxisTransform>& levelAxisTransforms();
bool levelAxisTransformForCommand(const QString& commandId, LevelAxisTransform* transform = nullptr);
// Doom-format geometry lies in the plane: it mirrors along x or y and turns
// about z only. Every Quake-family map takes all six.
[[nodiscard]] bool levelAxisTransformSupported(const LevelMapDocument& document, const LevelAxisTransform& transform);

// The centre of the box from `mins` to `maxs`, rounded to the nearest multiple
// of `grid` on each axis with snapLevelMapPosition() (ties away from zero, as
// every VibeStudio grid snap rounds). A non-positive or non-finite grid keeps
// the exact centre.
[[nodiscard]] LevelMapVec3 levelQuickTurnPivot(const LevelMapVec3& mins, const LevelMapVec3& maxs, double grid);
// The same pivot for the document's selection, from levelMapSelectionBounds():
// everything a transform of the selection moves. False, with `error` set, when
// nothing selected has a position.
bool levelMapQuickTurnPivot(const LevelMapDocument& document, double grid, LevelMapVec3* pivot, QString* error = nullptr);

// View-relative quick transforms: TrenchBroom's Roll, Yaw and Pitch
// Clockwise and Counter-clockwise, and its Flip Horizontally and Vertically.
// Each resolves, for the view it is used from, to one world axis, so the edit
// is again flipLevelMapSelection() or rotateLevelMapSelection().
//
// A plan view's frame is its screen right, screen up and the depth axis that
// points at the viewer (core/level_nudge.h); the camera's is the horizontal
// world axis nearest its view (forward), its right axis snapped to a world
// axis, and world Z (up), as the camera's nudges use them.
//
// - Roll turns 90° about the forward axis (a plan view's screen up), clockwise
//   as seen looking along it.
// - Yaw turns about the up axis (a plan view's depth axis, world Z from the
//   camera), clockwise as the view shows it. Rotate 90° Right is Yaw Clockwise
//   and Rotate 90° Left is Yaw Counter-clockwise.
// - Pitch turns about the right axis, clockwise as seen looking along it.
// - Flip Horizontal mirrors along the right axis; Flip Vertical along a plan
//   view's screen up, or world Z from the camera.
//
// Workflow reference: TrenchBroom MapViewBase::rotate, rotationAxis and flip,
// MapView2D/MapView3D::moveDirection and flipAxis at 4c756be1
// (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.

enum class LevelViewTransformKind { Roll, Yaw, Pitch, FlipHorizontal, FlipVertical };

struct LevelViewTransform {
	// The shell's action id, such as map.rollClockwise.
	QString commandId;
	// The CLI's --transform name, such as roll-clockwise.
	QString name;
	LevelViewTransformKind kind = LevelViewTransformKind::Yaw;
	// Rotations only.
	bool clockwise = false;
};

// Roll Clockwise and Counter-clockwise, Yaw Clockwise (map.rotateRight) and
// Counter-clockwise (map.rotateLeft), Pitch Clockwise and Counter-clockwise,
// Flip Horizontal and Flip Vertical, in that order.
[[nodiscard]] const QVector<LevelViewTransform>& levelViewTransforms();
// Also accepts the palette's hyphenated spelling, such as map.roll-clockwise.
bool levelViewTransformForCommand(const QString& commandId, LevelViewTransform* transform = nullptr);
bool levelViewTransformForName(const QString& name, LevelViewTransform* transform = nullptr);
// Every --transform name, comma separated, for usage messages.
[[nodiscard]] QString levelViewTransformNames();
[[nodiscard]] inline bool levelViewTransformMirrors(const LevelViewTransform& transform)
{
	return transform.kind == LevelViewTransformKind::FlipHorizontal || transform.kind == LevelViewTransformKind::FlipVertical;
}

// The world-axis transform `transform` makes in a plan view. The result's
// commandId is the view-relative command's.
bool levelPlanViewTransform(MapRenderProjection projection, const LevelViewTransform& transform, LevelAxisTransform* resolved);
// The same for the camera, from its forward, right and up directions; false
// when the basis is degenerate.
bool levelCameraViewTransform(const LevelMapVec3& forward, const LevelMapVec3& right, const LevelMapVec3& up,
	const LevelViewTransform& transform, LevelAxisTransform* resolved);
// The same for a camera looking along `yawDegrees` (0 along +X, counter-
// clockwise) and `pitchDegrees` (up positive), as the CLI names a view.
bool levelCameraViewTransform(double yawDegrees, double pitchDegrees, const LevelViewTransform& transform, LevelAxisTransform* resolved);
// The signed unit vector a resolved turn is right-handed about (its axis, or
// the axis reversed for a negative turn); invalid for a mirror.
[[nodiscard]] LevelMapVec3 levelAxisTransformTurnVector(const LevelAxisTransform& transform);

// The pivot of a flip under TrenchBroom's policy: the centre of the box,
// rounded to half of `grid`. Mirroring about a multiple of half the grid keeps
// on-grid points on the grid, so on-grid geometry never moves off it and an
// on-grid selection flips in place. A non-positive or non-finite grid keeps
// the exact centre.
[[nodiscard]] LevelMapVec3 levelFlipPivot(const LevelMapVec3& mins, const LevelMapVec3& maxs, double grid);
// The same pivot for the document's selection; false, with `error` set, when
// nothing selected has a position.
bool levelMapFlipPivot(const LevelMapDocument& document, double grid, LevelMapVec3* pivot, QString* error = nullptr);

} // namespace vibestudio
