#pragma once

// The level camera's levelling and focusing, shared by the Levels View >
// Camera commands, the held free-focus key and `map camera-focus`.
//
// Level Camera keeps the camera where it stands, drops its pitch to zero and
// turns its heading to the nearest 22.5-degree step, as Radiant's Center
// command (End) does. Focus Camera on Selection keeps the camera's heading
// and pitch and moves it along that view until every corner of the
// selection's box is inside the view, at least 64 units from the box's
// centre; the plan views stay where they are. While the free-focus key is
// held during mouse look, each wheel notch scales that distance by a
// constant step, nearer or farther.
//
// Workflow reference: NRC camwindow.cpp GlobalCamera_ResetAngles,
// Camera_getFocusPos and GlobalCamera_FocusOnSelected at 449778b3
// (GPL-2.0-or-later); Q3Radiant MainFrm.cpp OnViewCenter at dbe4ddb1
// (GPL-2.0-or-later). Original Qt implementation with its own frustum fit and
// offset curve; see docs/CREDITS.md.

#include "core/level_map.h"

#include <QString>
#include <QVector>

namespace vibestudio {

// The closest Focus Camera on Selection places the camera to the box's
// centre before any wheel offset, in map units.
inline constexpr double kLevelCameraFocusMinimumDistance = 64.0;
// The box keeps at least this far in front of the camera, in map units, so
// no corner reaches the camera's near plane.
inline constexpr double kLevelCameraFocusNearClearance = 8.0;
// Wheel notches the free focus accepts each way; eight notches halve or
// double the distance.
inline constexpr int kLevelCameraFocusOffsetLimit = 48;

// The nearest 22.5-degree heading to `yawDegrees`, from 0 up to but not
// including 360; NaN when the input is not finite.
[[nodiscard]] double levelCameraLevelledYaw(double yawDegrees);

// Scale a wheel offset applies to the fitted distance: 2^(-steps/8), with
// `steps` clamped to the offset limit. Positive steps come nearer.
[[nodiscard]] double levelCameraFocusOffsetScale(int offsetSteps);

struct LevelCameraFocusRequest {
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	// Where the camera looks: yaw 0 along +X, counter-clockwise; pitch up
	// positive, both in degrees.
	double yawDegrees = 0.0;
	double pitchDegrees = 0.0;
	// The camera's vertical field of view in degrees and its width / height.
	double fieldOfViewDegrees = 90.0;
	double aspect = 4.0 / 3.0;
	// Wheel notches while the free-focus key is held; 0 fits exactly.
	int offsetSteps = 0;
};

struct LevelCameraFocus {
	bool valid = false;
	QString error;
	// Where the camera goes; it keeps the request's yaw and pitch.
	LevelMapVec3 position;
	LevelMapVec3 centre;
	// Distance from the box centre that fits the box (never below the
	// minimum), and the distance after the wheel offset.
	double fitDistance = 0.0;
	double distance = 0.0;
};

// The camera position that fits the box in the view. Fails, with a
// translated reason, for an invalid box, angles, field of view or aspect.
[[nodiscard]] LevelCameraFocus levelCameraFocusPosition(const LevelCameraFocusRequest& request);

// The box the focus fits around `objects`: those that still exist and are not
// hidden. False when none of them can be measured.
bool levelCameraFocusObjectsBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects,
	LevelMapVec3* mins, LevelMapVec3* maxs);

} // namespace vibestudio
