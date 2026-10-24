// Directional nudge axes (core/level_nudge.h). Workflow reference:
// TrenchBroom MapView2D::moveDirection and MapView3D::moveDirection at
// 4c756be1 (GPL-3.0-or-later); NetRadiant Custom radiant/select.cpp
// NudgeSelection and Selection_MoveUp/MoveDown at 449778b3
// (GPL-2.0-or-later). Original implementation; see docs/CREDITS.md.

#include "core/level_nudge.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace vibestudio {
namespace {

constexpr double kDegenerate = 1e-9;

LevelMapVec3 axisVector(int axis, double sign)
{
	LevelMapVec3 result {0, 0, 0, true};
	(axis == 0 ? result.x : axis == 1 ? result.y : result.z) = sign < 0 ? -1.0 : 1.0;
	return result;
}

LevelMapVec3 negated(const LevelMapVec3& value)
{
	return {-value.x, -value.y, -value.z, value.valid};
}

bool finite(const LevelMapVec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double lengthOf(const LevelMapVec3& value)
{
	return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

// The world axis carrying the largest component of `value`, with its sign;
// ties go to the earlier axis. Invalid for a zero or non-finite vector.
LevelMapVec3 dominantAxis(const LevelMapVec3& value)
{
	if (!finite(value) || lengthOf(value) < kDegenerate) {
		return {};
	}
	const double ax = std::abs(value.x);
	const double ay = std::abs(value.y);
	const double az = std::abs(value.z);
	if (ax >= ay && ax >= az) {
		return axisVector(0, value.x);
	}
	if (ay >= az) {
		return axisVector(1, value.y);
	}
	return axisVector(2, value.z);
}

bool sameAxis(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.valid && b.valid && a.x == b.x && a.y == b.y && a.z == b.z;
}

LevelMapVec3 cameraForwardAxis(const LevelMapVec3& forward, const LevelMapVec3& up)
{
	// The view's direction projected onto the ground plane; looking straight
	// down Forward is up the picture, looking straight up it is down it.
	const LevelMapVec3 ground {forward.x, forward.y, 0, true};
	if (lengthOf(ground) < 1e-6) {
		const LevelMapVec3 screenUp = dominantAxis(up);
		return forward.z < 0 ? screenUp : negated(screenUp);
	}
	return dominantAxis(ground);
}

LevelMapVec3 cameraRightAxis(const LevelMapVec3& forward, const LevelMapVec3& right, const LevelMapVec3& up)
{
	LevelMapVec3 axis = dominantAxis(right);
	const LevelMapVec3 ahead = cameraForwardAxis(forward, up);
	if (axis.valid && sameAxis(axis, ahead)) {
		// Rolled far enough that right and forward snap together: turn it a
		// quarter about Z (cross with +Z), as TrenchBroom does.
		axis = {axis.y, -axis.x, 0, true};
	}
	return axis;
}

} // namespace

const QVector<LevelNudgeCommand>& levelNudgeCommands()
{
	static const QVector<LevelNudgeCommand> commands {
		{QStringLiteral("map.nudgeLeft"), QStringLiteral("left"), LevelNudgeDirection::Left},
		{QStringLiteral("map.nudgeRight"), QStringLiteral("right"), LevelNudgeDirection::Right},
		{QStringLiteral("map.nudgeForward"), QStringLiteral("forward"), LevelNudgeDirection::Forward},
		{QStringLiteral("map.nudgeBack"), QStringLiteral("back"), LevelNudgeDirection::Back},
		{QStringLiteral("map.nudgeDepthUp"), QStringLiteral("depth-up"), LevelNudgeDirection::DepthUp},
		{QStringLiteral("map.nudgeDepthDown"), QStringLiteral("depth-down"), LevelNudgeDirection::DepthDown},
		{QStringLiteral("map.nudgeWorldUp"), QStringLiteral("z-up"), LevelNudgeDirection::ZUp},
		{QStringLiteral("map.nudgeWorldDown"), QStringLiteral("z-down"), LevelNudgeDirection::ZDown},
	};
	return commands;
}

namespace {

// The palette's hyphenated form (map.nudge-depth-up) names the same command.
bool findCommand(const QVector<LevelNudgeCommand>& commands, const QString& commandId, LevelNudgeCommand* command)
{
	const auto key = [](QString id) { return id.remove(QLatin1Char('-')).toLower(); };
	const QString wanted = key(commandId.trimmed());
	for (const LevelNudgeCommand& candidate : commands) {
		if (key(candidate.commandId) == wanted) {
			if (command) {
				*command = candidate;
			}
			return true;
		}
	}
	return false;
}

} // namespace

bool levelNudgeCommandForId(const QString& commandId, LevelNudgeCommand* command)
{
	return findCommand(levelNudgeCommands(), commandId, command);
}

const QVector<LevelNudgeCommand>& levelDuplicateMoveCommands()
{
	static const QVector<LevelNudgeCommand> commands {
		{QStringLiteral("map.duplicateMoveLeft"), QStringLiteral("left"), LevelNudgeDirection::Left},
		{QStringLiteral("map.duplicateMoveRight"), QStringLiteral("right"), LevelNudgeDirection::Right},
		{QStringLiteral("map.duplicateMoveForward"), QStringLiteral("forward"), LevelNudgeDirection::Forward},
		{QStringLiteral("map.duplicateMoveBack"), QStringLiteral("back"), LevelNudgeDirection::Back},
		{QStringLiteral("map.duplicateMoveDepthUp"), QStringLiteral("depth-up"), LevelNudgeDirection::DepthUp},
		{QStringLiteral("map.duplicateMoveDepthDown"), QStringLiteral("depth-down"), LevelNudgeDirection::DepthDown},
	};
	return commands;
}

bool levelDuplicateMoveCommandForId(const QString& commandId, LevelNudgeCommand* command)
{
	return findCommand(levelDuplicateMoveCommands(), commandId, command);
}

bool levelNudgeCommandForName(const QString& name, LevelNudgeCommand* command)
{
	const QString wanted = name.trimmed().toLower();
	for (const LevelNudgeCommand& candidate : levelNudgeCommands()) {
		if (candidate.name == wanted) {
			if (command) {
				*command = candidate;
			}
			return true;
		}
	}
	return false;
}

QString levelNudgeDirectionNames()
{
	QStringList names;
	for (const LevelNudgeCommand& command : levelNudgeCommands()) {
		names << command.name;
	}
	return names.join(QStringLiteral(", "));
}

LevelMapVec3 levelPlanNudgeAxis(MapRenderProjection projection, LevelNudgeDirection direction)
{
	// Screen right, screen up and towards the viewer, per projection: Top
	// looks down -Z, Front along +Y and Side along -X, so each view's right,
	// up and towards-viewer axes form a right-handed set.
	int rightAxis = 0;
	int upAxis = 1;
	int depthAxis = 2;
	double depthSign = 1.0;
	switch (projection) {
	case MapRenderProjection::TopXY:
		break;
	case MapRenderProjection::FrontXZ:
		upAxis = 2;
		depthAxis = 1;
		depthSign = -1.0;
		break;
	case MapRenderProjection::SideZY:
		rightAxis = 1;
		upAxis = 2;
		depthAxis = 0;
		break;
	}
	switch (direction) {
	case LevelNudgeDirection::Left:
		return axisVector(rightAxis, -1.0);
	case LevelNudgeDirection::Right:
		return axisVector(rightAxis, 1.0);
	case LevelNudgeDirection::Forward:
		return axisVector(upAxis, 1.0);
	case LevelNudgeDirection::Back:
		return axisVector(upAxis, -1.0);
	case LevelNudgeDirection::DepthUp:
		return axisVector(depthAxis, depthSign);
	case LevelNudgeDirection::DepthDown:
		return axisVector(depthAxis, -depthSign);
	case LevelNudgeDirection::ZUp:
		return axisVector(2, 1.0);
	case LevelNudgeDirection::ZDown:
		return axisVector(2, -1.0);
	}
	return {};
}

LevelMapVec3 levelCameraNudgeAxis(const LevelMapVec3& forward, const LevelMapVec3& right, const LevelMapVec3& up,
	LevelNudgeDirection direction)
{
	if (!finite(forward) || !finite(right) || !finite(up) || lengthOf(forward) < kDegenerate) {
		return {};
	}
	switch (direction) {
	case LevelNudgeDirection::Forward:
		return cameraForwardAxis(forward, up);
	case LevelNudgeDirection::Back: {
		const LevelMapVec3 axis = cameraForwardAxis(forward, up);
		return axis.valid ? negated(axis) : axis;
	}
	case LevelNudgeDirection::Right:
		return cameraRightAxis(forward, right, up);
	case LevelNudgeDirection::Left: {
		const LevelMapVec3 axis = cameraRightAxis(forward, right, up);
		return axis.valid ? negated(axis) : axis;
	}
	case LevelNudgeDirection::DepthUp:
	case LevelNudgeDirection::ZUp:
		return axisVector(2, 1.0);
	case LevelNudgeDirection::DepthDown:
	case LevelNudgeDirection::ZDown:
		return axisVector(2, -1.0);
	}
	return {};
}

LevelMapVec3 levelCameraNudgeAxis(double yawDegrees, double pitchDegrees, LevelNudgeDirection direction)
{
	if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) {
		return {};
	}
	constexpr double kRadians = 3.14159265358979323846 / 180.0;
	const double yaw = yawDegrees * kRadians;
	const double pitch = std::clamp(pitchDegrees, -90.0, 90.0) * kRadians;
	const LevelMapVec3 forward {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch), true};
	// Right is the heading turned a quarter clockwise; up completes the set.
	const LevelMapVec3 right {std::sin(yaw), -std::cos(yaw), 0, true};
	const LevelMapVec3 up {right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z,
		right.x * forward.y - right.y * forward.x, true};
	return levelCameraNudgeAxis(forward, right, up, direction);
}

bool levelNudgeAxisSupported(const LevelMapDocument& document, const LevelMapVec3& axis)
{
	if (!axis.valid || document.format == LevelMapFormat::Unknown) {
		return false;
	}
	return document.format != LevelMapFormat::DoomWad || axis.z == 0.0;
}

LevelMapVec3 levelNudgeDelta(const LevelMapVec3& axis, double grid, int steps)
{
	if (!axis.valid || !finite(axis) || !std::isfinite(grid) || grid <= 0 || steps < 1 || steps > 64) {
		return {};
	}
	const double distance = grid * steps;
	return {axis.x * distance, axis.y * distance, axis.z * distance, true};
}

bool levelPlanNudgePlaneDelta(MapRenderProjection projection, const LevelMapVec3& delta, double* right, double* up)
{
	double r = 0;
	double u = 0;
	double depth = 0;
	switch (projection) {
	case MapRenderProjection::TopXY:
		r = delta.x;
		u = delta.y;
		depth = delta.z;
		break;
	case MapRenderProjection::FrontXZ:
		r = delta.x;
		u = delta.z;
		depth = delta.y;
		break;
	case MapRenderProjection::SideZY:
		r = delta.y;
		u = delta.z;
		depth = delta.x;
		break;
	}
	if (right) {
		*right = r;
	}
	if (up) {
		*up = u;
	}
	return delta.valid && depth == 0.0;
}

bool levelNudgeCameraFollowsPlan(const LevelEditorControls& controls)
{
	return controls.plan.arrowsDriveCamera;
}

} // namespace vibestudio
