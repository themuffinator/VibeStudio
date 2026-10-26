#pragma once

#include "core/level_surface.h"

#include <QPointF>
#include <QString>
#include <QVector>

// Camera-relative texture moves and flips: TrenchBroom's Move Textures Up,
// Down, Left and Right in the 3D view (one grid size of texels), their Coarse
// (twice the grid size) and Fine (one texel) forms, and Flip Textures
// Horizontally and Vertically. Each face's texture axes are compared with the
// camera's screen-right and screen-up directions within the face's plane: the
// axis running most nearly across the screen is horizontal and the other
// vertical, whichever way a texture is turned or mirrored, on classic, Valve
// 220 and brush-primitive faces and on patches alike. The edits themselves are
// the surface service's CameraShift and CameraFlip (core/level_surface.h), so
// they share its worker, limits, scene locks and single undo step.
//
// The same table holds TrenchBroom's Rotate Textures Clockwise and
// Counter-clockwise, in the 3D view with faces selected too: 15° (its fixed
// grid angle), 90° for Coarse and 1° for Fine. They need no camera. Each face
// turns the named way as seen from its own front, about the texture origin,
// through the surface service's ClockwiseRotate.
//
// Workflow reference: TrenchBroom lib/TbUiLib/src/ActionManager.cpp "Move
// textures ...", "Rotate textures ..." and "Flip textures ..." actions, and
// MapViewBase::moveUV, moveUVDistance, rotateUV, rotateUVAngle and flipUV in
// lib/TbUiLib/src/MapViewBase.cpp at 4c756be1 (GPL-3.0-or-later). Original
// implementation; see docs/CREDITS.md.

namespace vibestudio {

enum class LevelCameraTextureDirection { Up, Down, Left, Right, Clockwise, Counterclockwise };
// Grid moves one grid size of texels, Coarse twice that and Fine one texel,
// whatever the grid, as TrenchBroom's moveUVDistance does. A turn takes 15°,
// 90° or 1°, as its rotateUVAngle does.
enum class LevelCameraTextureStep { Grid, Coarse, Fine };

struct LevelCameraTextureCommand {
	// The shell's action id, such as map.moveTexturesUp.
	QString commandId;
	// A flip mirrors the axis across the screen for Left and Right, and the
	// axis up the screen for Up and Down; a move goes that way. Clockwise and
	// Counterclockwise are turns.
	bool flip = false;
	LevelCameraTextureDirection direction = LevelCameraTextureDirection::Up;
	LevelCameraTextureStep step = LevelCameraTextureStep::Grid;
};

// Move Textures Up, Down, Left and Right, then their Coarse forms, then their
// Fine forms, then Flip Textures Horizontally and Vertically, then Rotate
// Textures Clockwise, its Coarse and Fine forms, and Rotate Textures
// Counter-clockwise and its Coarse and Fine forms.
[[nodiscard]] const QVector<LevelCameraTextureCommand>& levelCameraTextureCommands();
// Also accepts the palette's hyphenated spelling, such as map.move-textures-up.
bool levelCameraTextureCommandForId(const QString& commandId, LevelCameraTextureCommand* command = nullptr);
// Whether `command` turns textures, which needs no camera directions.
[[nodiscard]] bool levelCameraTextureRotates(const LevelCameraTextureCommand& command);
// The texels a move covers: `gridUnits`, twice that, or 1. Zero for a
// non-finite or non-positive grid.
[[nodiscard]] double levelCameraTextureDistance(LevelCameraTextureStep step, double gridUnits);
// The degrees a turn covers: 15 (TrenchBroom's fixed grid angle), 90 for
// Coarse and 1 for Fine.
[[nodiscard]] double levelCameraTextureAngle(LevelCameraTextureStep step);
// Whether `right` and `up` are finite, non-zero and not parallel.
[[nodiscard]] bool levelCameraTextureBasisValid(const LevelMapVec3& right, const LevelMapVec3& up);
// The surface request `command` makes from a camera with these screen-right
// and screen-up directions, at this grid size. A turn ignores the camera: it
// is a ClockwiseRotate of levelCameraTextureAngle() degrees, negative for
// Counter-clockwise.
[[nodiscard]] LevelSurfaceRequest levelCameraTextureRequest(const LevelCameraTextureCommand& command, const LevelMapVec3& right,
	const LevelMapVec3& up, double gridUnits);

// Which way a texture turned, as seen from the front of a plane whose outward
// normal is `normal` (the side a camera looking against the normal sees).
// `before` and `after` are the directions in which one of its coordinates
// grows before and after the turn, as for levelCameraTextureAxes(); both are
// first laid into the plane. A picture turns the way its coordinate directions
// do. None when either has no direction within the plane, or when they run
// together or exactly apart, which a quarter turn never does.
enum class LevelTextureTurn { None, Counterclockwise, Clockwise };
[[nodiscard]] LevelTextureTurn levelTextureTurnSense(const LevelMapVec3& before, const LevelMapVec3& after, const LevelMapVec3& normal);

// How a face's texture axes lie on screen.
struct LevelCameraTextureAxes {
	bool valid = false;
	// U runs across the screen and V up it; false when V runs across.
	bool uHorizontal = true;
	// +1 where the coordinate on the horizontal axis grows towards screen
	// right, and the one on the vertical axis towards screen up; -1 otherwise.
	int horizontalSign = 1;
	int verticalSign = 1;
};
// `gradientU` and `gradientV` are the directions in which the texture
// coordinates grow (core/level_texture_mapping.h's projection rows); both are
// first laid into the plane with this `normal`. The assignment with the larger
// summed alignment against `right` and `up` wins; on a tie the axis nearer the
// world's horizontal plane runs across, and then U.
// Invalid when an axis has no direction within the plane or the camera
// directions are unusable.
[[nodiscard]] LevelCameraTextureAxes levelCameraTextureAxes(const LevelMapVec3& gradientU, const LevelMapVec3& gradientV, const LevelMapVec3& normal,
	const LevelMapVec3& right, const LevelMapVec3& up);
// The U and V offset changes, in texels, that move the texture `rightTexels`
// towards screen right and `upTexels` towards screen up: an offset grows
// against the direction the picture moves.
[[nodiscard]] QPointF levelCameraTextureShift(const LevelCameraTextureAxes& axes, double rightTexels, double upTexels);

} // namespace vibestudio
