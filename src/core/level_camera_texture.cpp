// Camera-relative texture moves and flips, and fixed-step texture turns
// (core/level_camera_texture.h).
// Workflow reference: TrenchBroom ActionManager "Move textures ...", "Rotate
// textures ..." and "Flip textures ..." actions and MapViewBase::moveUV,
// moveUVDistance, rotateUV, rotateUVAngle and flipUV at 4c756be1
// (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.

#include "core/level_camera_texture.h"

#include <cmath>

namespace vibestudio {
namespace {

constexpr double kDegenerate = 1e-9;

double dot(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

bool finite(const LevelMapVec3& value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// `value` at unit length, or invalid when it has none.
LevelMapVec3 unit(const LevelMapVec3& value)
{
	const double length = std::sqrt(dot(value, value));
	if (!finite(value) || !std::isfinite(length) || length < kDegenerate) {
		return {};
	}
	return {value.x / length, value.y / length, value.z / length, true};
}

// `value` with its part along the unit `normal` removed, at unit length.
LevelMapVec3 inPlane(const LevelMapVec3& value, const LevelMapVec3& normal)
{
	if (!normal.valid) {
		return unit(value);
	}
	const double along = dot(value, normal);
	return unit({value.x - normal.x * along, value.y - normal.y * along, value.z - normal.z * along, true});
}

QString hyphenated(const QString& camelCase)
{
	QString result;
	for (const QChar character : camelCase) {
		if (character.isUpper()) {
			result += QLatin1Char('-');
			result += character.toLower();
		} else {
			result += character;
		}
	}
	return result;
}

} // namespace

const QVector<LevelCameraTextureCommand>& levelCameraTextureCommands()
{
	using Direction = LevelCameraTextureDirection;
	using Step = LevelCameraTextureStep;
	static const QVector<LevelCameraTextureCommand> commands {
		{QStringLiteral("map.moveTexturesUp"), false, Direction::Up, Step::Grid},
		{QStringLiteral("map.moveTexturesDown"), false, Direction::Down, Step::Grid},
		{QStringLiteral("map.moveTexturesLeft"), false, Direction::Left, Step::Grid},
		{QStringLiteral("map.moveTexturesRight"), false, Direction::Right, Step::Grid},
		{QStringLiteral("map.moveTexturesUpCoarse"), false, Direction::Up, Step::Coarse},
		{QStringLiteral("map.moveTexturesDownCoarse"), false, Direction::Down, Step::Coarse},
		{QStringLiteral("map.moveTexturesLeftCoarse"), false, Direction::Left, Step::Coarse},
		{QStringLiteral("map.moveTexturesRightCoarse"), false, Direction::Right, Step::Coarse},
		{QStringLiteral("map.moveTexturesUpFine"), false, Direction::Up, Step::Fine},
		{QStringLiteral("map.moveTexturesDownFine"), false, Direction::Down, Step::Fine},
		{QStringLiteral("map.moveTexturesLeftFine"), false, Direction::Left, Step::Fine},
		{QStringLiteral("map.moveTexturesRightFine"), false, Direction::Right, Step::Fine},
		{QStringLiteral("map.flipTexturesHorizontally"), true, Direction::Right, Step::Grid},
		{QStringLiteral("map.flipTexturesVertically"), true, Direction::Up, Step::Grid},
		{QStringLiteral("map.rotateTexturesClockwise"), false, Direction::Clockwise, Step::Grid},
		{QStringLiteral("map.rotateTexturesClockwiseCoarse"), false, Direction::Clockwise, Step::Coarse},
		{QStringLiteral("map.rotateTexturesClockwiseFine"), false, Direction::Clockwise, Step::Fine},
		{QStringLiteral("map.rotateTexturesCounterclockwise"), false, Direction::Counterclockwise, Step::Grid},
		{QStringLiteral("map.rotateTexturesCounterclockwiseCoarse"), false, Direction::Counterclockwise, Step::Coarse},
		{QStringLiteral("map.rotateTexturesCounterclockwiseFine"), false, Direction::Counterclockwise, Step::Fine},
	};
	return commands;
}

bool levelCameraTextureRotates(const LevelCameraTextureCommand& command)
{
	return command.direction == LevelCameraTextureDirection::Clockwise || command.direction == LevelCameraTextureDirection::Counterclockwise;
}

bool levelCameraTextureCommandForId(const QString& commandId, LevelCameraTextureCommand* command)
{
	const QString wanted = commandId.trimmed();
	for (const LevelCameraTextureCommand& candidate : levelCameraTextureCommands()) {
		if (candidate.commandId == wanted || hyphenated(candidate.commandId) == wanted) {
			if (command) {
				*command = candidate;
			}
			return true;
		}
	}
	return false;
}

double levelCameraTextureDistance(LevelCameraTextureStep step, double gridUnits)
{
	if (!std::isfinite(gridUnits) || gridUnits <= 0) {
		return 0;
	}
	switch (step) {
	case LevelCameraTextureStep::Fine:
		return 1;
	case LevelCameraTextureStep::Coarse:
		return 2 * gridUnits;
	case LevelCameraTextureStep::Grid:
		break;
	}
	return gridUnits;
}

double levelCameraTextureAngle(LevelCameraTextureStep step)
{
	switch (step) {
	case LevelCameraTextureStep::Fine:
		return 1;
	case LevelCameraTextureStep::Coarse:
		return 90;
	case LevelCameraTextureStep::Grid:
		break;
	}
	// TrenchBroom's Grid::angle(), which no setting changes.
	return 15;
}

bool levelCameraTextureBasisValid(const LevelMapVec3& right, const LevelMapVec3& up)
{
	const LevelMapVec3 r = unit(right);
	const LevelMapVec3 u = unit(up);
	if (!right.valid || !up.valid || !r.valid || !u.valid) {
		return false;
	}
	const LevelMapVec3 across {r.y * u.z - r.z * u.y, r.z * u.x - r.x * u.z, r.x * u.y - r.y * u.x, true};
	return std::sqrt(dot(across, across)) > 1e-6;
}

LevelSurfaceRequest levelCameraTextureRequest(const LevelCameraTextureCommand& command, const LevelMapVec3& right, const LevelMapVec3& up,
	double gridUnits)
{
	LevelSurfaceRequest request;
	request.cameraRight = right;
	request.cameraUp = up;
	if (levelCameraTextureRotates(command)) {
		// Clockwise is positive, as TrenchBroom's rotateUVAngle makes it.
		const double angle = levelCameraTextureAngle(command.step);
		request.operation = LevelSurfaceOperation::ClockwiseRotate;
		request.degrees = command.direction == LevelCameraTextureDirection::Clockwise ? angle : -angle;
		return request;
	}
	const bool horizontal = command.direction == LevelCameraTextureDirection::Left || command.direction == LevelCameraTextureDirection::Right;
	if (command.flip) {
		request.operation = LevelSurfaceOperation::CameraFlip;
		request.x = horizontal ? 1 : 0;
		request.y = horizontal ? 0 : 1;
		return request;
	}
	request.operation = LevelSurfaceOperation::CameraShift;
	const double distance = levelCameraTextureDistance(command.step, gridUnits);
	const bool negative = command.direction == LevelCameraTextureDirection::Left || command.direction == LevelCameraTextureDirection::Down;
	request.x = horizontal ? (negative ? -distance : distance) : 0;
	request.y = horizontal ? 0 : (negative ? -distance : distance);
	return request;
}

LevelCameraTextureAxes levelCameraTextureAxes(const LevelMapVec3& gradientU, const LevelMapVec3& gradientV, const LevelMapVec3& normal,
	const LevelMapVec3& right, const LevelMapVec3& up)
{
	LevelCameraTextureAxes result;
	if (!levelCameraTextureBasisValid(right, up)) {
		return result;
	}
	const LevelMapVec3 n = normal.valid ? unit(normal) : LevelMapVec3 {};
	const LevelMapVec3 u = inPlane(gradientU, n);
	const LevelMapVec3 v = inPlane(gradientV, n);
	const LevelMapVec3 screenRight = unit(right);
	const LevelMapVec3 screenUp = unit(up);
	if (!u.valid || !v.valid) {
		return result;
	}
	// Which pairing lines the two texture axes up with the screen's best.
	const double uAcross = std::abs(dot(screenRight, u)) + std::abs(dot(screenUp, v));
	const double vAcross = std::abs(dot(screenRight, v)) + std::abs(dot(screenUp, u));
	if (std::abs(uAcross - vAcross) > kDegenerate) {
		result.uHorizontal = uAcross > vAcross;
	} else if (std::abs(std::abs(u.z) - std::abs(v.z)) > kDegenerate) {
		// A texture turned 45° to the view: the axis nearer the world's
		// horizontal plane runs across.
		result.uHorizontal = std::abs(u.z) < std::abs(v.z);
	} else {
		result.uHorizontal = true;
	}
	const LevelMapVec3& across = result.uHorizontal ? u : v;
	const LevelMapVec3& upward = result.uHorizontal ? v : u;
	// A face seen exactly edge-on counts as facing the same way.
	result.horizontalSign = dot(screenRight, across) < -kDegenerate ? -1 : 1;
	result.verticalSign = dot(screenUp, upward) < -kDegenerate ? -1 : 1;
	result.valid = true;
	return result;
}

QPointF levelCameraTextureShift(const LevelCameraTextureAxes& axes, double rightTexels, double upTexels)
{
	if (!axes.valid) {
		return {};
	}
	// Adding to an offset slides the picture towards smaller coordinates.
	const double across = -axes.horizontalSign * rightTexels;
	const double upward = -axes.verticalSign * upTexels;
	return axes.uHorizontal ? QPointF(across, upward) : QPointF(upward, across);
}

LevelTextureTurn levelTextureTurnSense(const LevelMapVec3& before, const LevelMapVec3& after, const LevelMapVec3& normal)
{
	const LevelMapVec3 n = normal.valid ? unit(normal) : LevelMapVec3 {};
	if (!n.valid) {
		return LevelTextureTurn::None;
	}
	const LevelMapVec3 from = inPlane(before, n);
	const LevelMapVec3 to = inPlane(after, n);
	if (!from.valid || !to.valid) {
		return LevelTextureTurn::None;
	}
	// The sine of the turn about the normal: positive turns anticlockwise for
	// a viewer the normal points at.
	const double sine = n.x * (from.y * to.z - from.z * to.y) + n.y * (from.z * to.x - from.x * to.z) + n.z * (from.x * to.y - from.y * to.x);
	if (!std::isfinite(sine) || std::abs(sine) < 1e-6) {
		return LevelTextureTurn::None;
	}
	return sine > 0 ? LevelTextureTurn::Counterclockwise : LevelTextureTurn::Clockwise;
}

} // namespace vibestudio
