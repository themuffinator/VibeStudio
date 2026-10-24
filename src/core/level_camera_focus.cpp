// Levelling and focusing the level camera (core/level_camera_focus.h).
//
// Workflow reference: NRC camwindow.cpp GlobalCamera_ResetAngles,
// Camera_getFocusPos and GlobalCamera_FocusOnSelected at 449778b3
// (GPL-2.0-or-later); Q3Radiant MainFrm.cpp OnViewCenter at dbe4ddb1
// (GPL-2.0-or-later). Original Qt implementation; see docs/CREDITS.md.

#include "core/level_camera_focus.h"
#include "core/level_visibility.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio {
namespace {

constexpr double kStepDegrees = 22.5;
constexpr double kCoordinateLimit = 1.0e7;

bool finiteCoordinate(double value)
{
	return std::isfinite(value) && std::abs(value) <= kCoordinateLimit;
}

LevelCameraFocus failed(const QString& error)
{
	LevelCameraFocus focus;
	focus.error = error;
	return focus;
}

} // namespace

double levelCameraLevelledYaw(double yawDegrees)
{
	if (!std::isfinite(yawDegrees)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double heading = std::fmod(yawDegrees, 360.0);
	if (heading < 0.0) {
		heading += 360.0;
	}
	double levelled = std::round(heading / kStepDegrees) * kStepDegrees;
	if (levelled >= 360.0) {
		levelled -= 360.0;
	}
	return levelled;
}

double levelCameraFocusOffsetScale(int offsetSteps)
{
	const int steps = std::clamp(offsetSteps, -kLevelCameraFocusOffsetLimit, kLevelCameraFocusOffsetLimit);
	return std::exp2(-static_cast<double>(steps) / 8.0);
}

LevelCameraFocus levelCameraFocusPosition(const LevelCameraFocusRequest& request)
{
	const LevelMapVec3& low = request.mins;
	const LevelMapVec3& high = request.maxs;
	if (!finiteCoordinate(low.x) || !finiteCoordinate(low.y) || !finiteCoordinate(low.z) || !finiteCoordinate(high.x)
		|| !finiteCoordinate(high.y) || !finiteCoordinate(high.z) || low.x > high.x || low.y > high.y || low.z > high.z) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The box to focus on has no valid size or position."));
	}
	if (!std::isfinite(request.yawDegrees) || !std::isfinite(request.pitchDegrees) || std::abs(request.pitchDegrees) > 90.0) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The camera heading must be finite and its pitch between -90 and 90 degrees."));
	}
	if (!std::isfinite(request.fieldOfViewDegrees) || request.fieldOfViewDegrees < 15.0 || request.fieldOfViewDegrees > 150.0) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The field of view must be between 15 and 150 degrees."));
	}
	if (!std::isfinite(request.aspect) || request.aspect < 0.01 || request.aspect > 100.0) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The view's width to height ratio must be between 0.01 and 100."));
	}
	// The camera's own axes: forward along the view, right level with the
	// ground (Quake-family cameras never roll), up completing the basis.
	const double yaw = request.yawDegrees * std::numbers::pi / 180.0;
	const double pitch = request.pitchDegrees * std::numbers::pi / 180.0;
	const std::array<double, 3> forward {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
	const std::array<double, 3> right {std::sin(yaw), -std::cos(yaw), 0.0};
	const std::array<double, 3> up {-std::cos(yaw) * std::sin(pitch), -std::sin(yaw) * std::sin(pitch), std::cos(pitch)};
	const double tanVertical = std::tan(request.fieldOfViewDegrees * std::numbers::pi / 360.0);
	const double tanHorizontal = tanVertical * request.aspect;
	const std::array<double, 3> centre {(low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (low.z + high.z) * 0.5};
	const std::array<double, 3> half {(high.x - low.x) * 0.5, (high.y - low.y) * 0.5, (high.z - low.z) * 0.5};
	// Standing `d` back from the centre along the view, a corner `q` from the
	// centre is `d + q.forward` deep, and inside the view while its sideways
	// and upward offsets stay within that depth times the half-angle tangents.
	// The fit is the smallest `d` that holds for all eight corners.
	double fit = kLevelCameraFocusMinimumDistance;
	for (int corner = 0; corner < 8; ++corner) {
		const std::array<double, 3> offset {(corner & 1) ? half[0] : -half[0], (corner & 2) ? half[1] : -half[1], (corner & 4) ? half[2] : -half[2]};
		const auto dot = [&offset](const std::array<double, 3>& axis) { return offset[0] * axis[0] + offset[1] * axis[1] + offset[2] * axis[2]; };
		const double along = dot(forward);
		fit = std::max({fit, std::abs(dot(right)) / tanHorizontal - along, std::abs(dot(up)) / tanVertical - along,
			kLevelCameraFocusNearClearance - along});
	}
	if (!std::isfinite(fit)) {
		return failed(QCoreApplication::translate("LevelCameraFocus", "The box to focus on has no valid size or position."));
	}
	LevelCameraFocus focus;
	focus.valid = true;
	focus.fitDistance = fit;
	focus.distance = fit * levelCameraFocusOffsetScale(request.offsetSteps);
	focus.centre = {centre[0], centre[1], centre[2], true};
	focus.position = {centre[0] - forward[0] * focus.distance, centre[1] - forward[1] * focus.distance, centre[2] - forward[2] * focus.distance, true};
	return focus;
}

bool levelCameraFocusObjectsBounds(const LevelMapDocument& document, const QVector<LevelMapSelectionRef>& objects,
	LevelMapVec3* mins, LevelMapVec3* maxs)
{
	const QSet<QString> hidden = levelMapHiddenObjects(document);
	QVector<LevelMapSelectionRef> measured;
	measured.reserve(objects.size());
	for (const LevelMapSelectionRef& ref : objects) {
		if (levelMapObjectExists(document, ref) && !hidden.contains(levelMapSelectionRefId(ref))) {
			measured.push_back(ref);
		}
	}
	LevelMapVec3 low;
	LevelMapVec3 high;
	if (measured.isEmpty() || !levelMapObjectsBounds(document, measured, &low, &high)) {
		return false;
	}
	low.valid = high.valid = true;
	if (mins) {
		*mins = low;
	}
	if (maxs) {
		*maxs = high;
	}
	return true;
}

} // namespace vibestudio
