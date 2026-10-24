// World-axis quick transforms and the shared quick-turn pivot.
// Workflow reference: NetRadiant Custom radiant/select.cpp Selection_Flipx to
// Selection_Rotatez and radiant/selection.cpp rotateSelected/scaleSelected
// (snapOrigin) at 449778b3 (GPL-2.0-or-later); TrenchBroom MapViewBase::rotate
// at 4c756be1 (GPL-3.0-or-later). Original implementation; see docs/CREDITS.md.

#include "core/level_quick_transform.h"
#include "core/level_nudge.h"

#include <QCoreApplication>
#include <QStringList>

#include <cmath>

namespace vibestudio {

const QVector<LevelAxisTransform>& levelAxisTransforms()
{
	// NetRadiant Custom turns X and Z clockwise as their positive axes look
	// at the viewer (-90) and Y the other way (+90), so each matches its
	// Rotate Clockwise in the side, top and front views respectively.
	static const QVector<LevelAxisTransform> transforms {
		{QStringLiteral("map.flipX"), true, 0, 0},
		{QStringLiteral("map.flipY"), true, 1, 0},
		{QStringLiteral("map.flipZ"), true, 2, 0},
		{QStringLiteral("map.rotateX"), false, 0, -1},
		{QStringLiteral("map.rotateY"), false, 1, 1},
		{QStringLiteral("map.rotateZ"), false, 2, -1},
	};
	return transforms;
}

bool levelAxisTransformForCommand(const QString& commandId, LevelAxisTransform* transform)
{
	for (const LevelAxisTransform& candidate : levelAxisTransforms()) {
		if (candidate.commandId == commandId) {
			if (transform) {
				*transform = candidate;
			}
			return true;
		}
	}
	return false;
}

bool levelAxisTransformSupported(const LevelMapDocument& document, const LevelAxisTransform& transform)
{
	if (document.format != LevelMapFormat::DoomWad) {
		return document.format != LevelMapFormat::Unknown;
	}
	return transform.mirror ? transform.axis != 2 : transform.axis == 2;
}

LevelMapVec3 levelQuickTurnPivot(const LevelMapVec3& mins, const LevelMapVec3& maxs, double grid)
{
	const LevelMapVec3 centre {(mins.x + maxs.x) * 0.5, (mins.y + maxs.y) * 0.5, (mins.z + maxs.z) * 0.5, mins.valid && maxs.valid};
	return std::isfinite(grid) && grid > 0.0 ? snapLevelMapPosition(centre, grid) : centre;
}

bool levelMapQuickTurnPivot(const LevelMapDocument& document, double grid, LevelMapVec3* pivot, QString* error)
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!levelMapSelectionBounds(document, &mins, &maxs)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Nothing selected has a position to transform about.");
		}
		return false;
	}
	if (pivot) {
		*pivot = levelQuickTurnPivot(mins, maxs, grid);
	}
	if (error) {
		error->clear();
	}
	return true;
}

// View-relative turns and flips. Workflow reference: TrenchBroom
// MapViewBase::rotate, rotationAxis and flip, MapView2D/MapView3D::
// moveDirection and flipAxis at 4c756be1 (GPL-3.0-or-later). Original
// implementation; see docs/CREDITS.md.

namespace {

// The view frame's three axes as the nudge service resolves them: forward
// (a plan view's screen up), right, and up (a plan view's depth axis towards
// the viewer, world Z from the camera). Vertical is what Flip Vertical mirrors
// along: screen up in a plan view, world Z from the camera.
struct ViewFrame {
	LevelMapVec3 forward;
	LevelMapVec3 right;
	LevelMapVec3 up;
	LevelMapVec3 vertical;
};

// One world axis and its sign from a unit axis vector.
bool axisOf(const LevelMapVec3& vector, int* axis, int* sign)
{
	if (!vector.valid) {
		return false;
	}
	const double components[3] {vector.x, vector.y, vector.z};
	for (int index = 0; index < 3; ++index) {
		if (components[index] != 0.0) {
			*axis = index;
			*sign = components[index] < 0 ? -1 : 1;
			return true;
		}
	}
	return false;
}

bool resolveInFrame(const ViewFrame& frame, const LevelViewTransform& transform, LevelAxisTransform* resolved)
{
	// TrenchBroom turns +90° about the roll axis (-forward), the pitch axis
	// (right) or the yaw axis (up), each reversed for the clockwise command.
	LevelMapVec3 along;
	bool reversed = false;
	switch (transform.kind) {
	case LevelViewTransformKind::Roll:
		along = frame.forward;
		reversed = !transform.clockwise;
		break;
	case LevelViewTransformKind::Pitch:
		along = frame.right;
		reversed = transform.clockwise;
		break;
	case LevelViewTransformKind::Yaw:
		along = frame.up;
		reversed = transform.clockwise;
		break;
	case LevelViewTransformKind::FlipHorizontal:
		along = frame.right;
		break;
	case LevelViewTransformKind::FlipVertical:
		along = frame.vertical;
		break;
	}
	int axis = 0;
	int sign = 1;
	if (!axisOf(along, &axis, &sign)) {
		return false;
	}
	LevelAxisTransform result;
	result.commandId = transform.commandId;
	result.axis = axis;
	result.mirror = levelViewTransformMirrors(transform);
	result.quarterTurns = result.mirror ? 0 : (reversed ? -sign : sign);
	if (resolved) {
		*resolved = result;
	}
	return true;
}

} // namespace

const QVector<LevelViewTransform>& levelViewTransforms()
{
	static const QVector<LevelViewTransform> transforms {
		{QStringLiteral("map.rollClockwise"), QStringLiteral("roll-clockwise"), LevelViewTransformKind::Roll, true},
		{QStringLiteral("map.rollCounterclockwise"), QStringLiteral("roll-counterclockwise"), LevelViewTransformKind::Roll, false},
		{QStringLiteral("map.rotateRight"), QStringLiteral("yaw-clockwise"), LevelViewTransformKind::Yaw, true},
		{QStringLiteral("map.rotateLeft"), QStringLiteral("yaw-counterclockwise"), LevelViewTransformKind::Yaw, false},
		{QStringLiteral("map.pitchClockwise"), QStringLiteral("pitch-clockwise"), LevelViewTransformKind::Pitch, true},
		{QStringLiteral("map.pitchCounterclockwise"), QStringLiteral("pitch-counterclockwise"), LevelViewTransformKind::Pitch, false},
		{QStringLiteral("map.flipHorizontal"), QStringLiteral("flip-horizontal"), LevelViewTransformKind::FlipHorizontal, false},
		{QStringLiteral("map.flipVertical"), QStringLiteral("flip-vertical"), LevelViewTransformKind::FlipVertical, false},
	};
	return transforms;
}

bool levelViewTransformForCommand(const QString& commandId, LevelViewTransform* transform)
{
	// The palette's hyphenated form (map.roll-clockwise) names the same command.
	const auto key = [](QString id) { return id.remove(QLatin1Char('-')).toLower(); };
	const QString wanted = key(commandId.trimmed());
	for (const LevelViewTransform& candidate : levelViewTransforms()) {
		if (key(candidate.commandId) == wanted) {
			if (transform) {
				*transform = candidate;
			}
			return true;
		}
	}
	return false;
}

bool levelViewTransformForName(const QString& name, LevelViewTransform* transform)
{
	const QString wanted = name.trimmed().toLower();
	for (const LevelViewTransform& candidate : levelViewTransforms()) {
		if (candidate.name == wanted) {
			if (transform) {
				*transform = candidate;
			}
			return true;
		}
	}
	return false;
}

QString levelViewTransformNames()
{
	QStringList names;
	for (const LevelViewTransform& transform : levelViewTransforms()) {
		names << transform.name;
	}
	return names.join(QStringLiteral(", "));
}

bool levelPlanViewTransform(MapRenderProjection projection, const LevelViewTransform& transform, LevelAxisTransform* resolved)
{
	ViewFrame frame;
	frame.forward = levelPlanNudgeAxis(projection, LevelNudgeDirection::Forward);
	frame.right = levelPlanNudgeAxis(projection, LevelNudgeDirection::Right);
	frame.up = levelPlanNudgeAxis(projection, LevelNudgeDirection::DepthUp);
	frame.vertical = frame.forward;
	return resolveInFrame(frame, transform, resolved);
}

bool levelCameraViewTransform(const LevelMapVec3& forward, const LevelMapVec3& right, const LevelMapVec3& up,
	const LevelViewTransform& transform, LevelAxisTransform* resolved)
{
	ViewFrame frame;
	frame.forward = levelCameraNudgeAxis(forward, right, up, LevelNudgeDirection::Forward);
	frame.right = levelCameraNudgeAxis(forward, right, up, LevelNudgeDirection::Right);
	frame.up = levelCameraNudgeAxis(forward, right, up, LevelNudgeDirection::ZUp);
	frame.vertical = frame.up;
	if (!frame.forward.valid || !frame.right.valid || !frame.up.valid) {
		return false;
	}
	return resolveInFrame(frame, transform, resolved);
}

bool levelCameraViewTransform(double yawDegrees, double pitchDegrees, const LevelViewTransform& transform, LevelAxisTransform* resolved)
{
	ViewFrame frame;
	frame.forward = levelCameraNudgeAxis(yawDegrees, pitchDegrees, LevelNudgeDirection::Forward);
	frame.right = levelCameraNudgeAxis(yawDegrees, pitchDegrees, LevelNudgeDirection::Right);
	frame.up = levelCameraNudgeAxis(yawDegrees, pitchDegrees, LevelNudgeDirection::ZUp);
	frame.vertical = frame.up;
	if (!frame.forward.valid || !frame.right.valid || !frame.up.valid) {
		return false;
	}
	return resolveInFrame(frame, transform, resolved);
}

LevelMapVec3 levelAxisTransformTurnVector(const LevelAxisTransform& transform)
{
	if (transform.mirror || transform.quarterTurns == 0 || transform.axis < 0 || transform.axis > 2) {
		return {};
	}
	LevelMapVec3 vector {0, 0, 0, true};
	const double sign = transform.quarterTurns < 0 ? -1.0 : 1.0;
	(transform.axis == 0 ? vector.x : transform.axis == 1 ? vector.y : vector.z) = sign;
	return vector;
}

LevelMapVec3 levelFlipPivot(const LevelMapVec3& mins, const LevelMapVec3& maxs, double grid)
{
	// TrenchBroom MapViewBase::flip snaps to half the grid so that a selection
	// an odd number of grid steps wide is not translated (its issue 1495).
	return levelQuickTurnPivot(mins, maxs, std::isfinite(grid) && grid > 0.0 ? grid * 0.5 : 0.0);
}

bool levelMapFlipPivot(const LevelMapDocument& document, double grid, LevelMapVec3* pivot, QString* error)
{
	LevelMapVec3 mins;
	LevelMapVec3 maxs;
	if (!levelMapSelectionBounds(document, &mins, &maxs)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioLevelMap", "Nothing selected has a position to transform about.");
		}
		return false;
	}
	if (pivot) {
		*pivot = levelFlipPivot(mins, maxs, grid);
	}
	if (error) {
		error->clear();
	}
	return true;
}

} // namespace vibestudio
