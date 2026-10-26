// Core and command-line checks of the camera-relative texture moves and flips
// and the fixed-step texture turns (core/level_camera_texture.h, CameraShift,
// CameraFlip and ClockwiseRotate in core/level_surface.h): the twenty commands
// and their grid, coarse and fine distances and 15°, 90° and 1° angles; which
// texture axis runs across the screen for walls, floors, turned textures and a
// camera behind the face; the picture moving towards screen right and up on
// classic, Valve 220 and brush-primitive faces and on a patch; flips mirroring
// the axis across or up the view about the middle; the picture turning
// clockwise or counter-clockwise as seen from every face's front, on each face
// format and on a patch, the right way round and mirrored, keeping the
// offsets; one exact undo step; refusals for a locked layer, unusable camera
// directions and a brush-primitive face with no image size; and map
// align-textures --camera-shift, --camera-flip and --clockwise writing what the
// shared service writes.

#include "core/level_camera_texture.h"
#include "core/level_document.h"
#include "core/level_nudge.h"
#include "core/level_patch.h"
#include "core/level_scene.h"
#include "core/level_scene_locks.h"
#include "core/level_surface.h"
#include "core/level_texture_mapping.h"
#include "core/map_geometry.h"
#include "tests/level_material_test_helpers.h"
#include "tests/level_surface_test_helpers.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;
QString phase;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << qPrintable(phase) << ": " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
// The first line where two maps differ, for a failure's detail.
QString firstDifference(const QByteArray& a, const QByteArray& b)
{
	const auto left = a.split('\n'), right = b.split('\n');
	for (int i = 0; i < std::max(left.size(), right.size()); ++i) {
		if (left.value(i) != right.value(i)) {
			return QStringLiteral("line %1: %2 | %3").arg(i + 1).arg(QString::fromUtf8(left.value(i)).trimmed(), QString::fromUtf8(right.value(i)).trimmed());
		}
	}
	return {};
}
double dot(const LevelMapVec3& a, const LevelMapVec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 scaled(const LevelMapVec3& a, double s) { return {a.x * s, a.y * s, a.z * s, true}; }
LevelMapVec3 plus(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z, true}; }
LevelMapVec3 minus(const LevelMapVec3& a, const LevelMapVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
LevelMapVec3 unit(const LevelMapVec3& a)
{
	const double length = std::sqrt(dot(a, a));
	return length > 0 ? scaled(a, 1 / length) : LevelMapVec3 {};
}
LevelMapVec3 normalOf(const MapFacePolygon& polygon) { return unit({polygon.plane.normalX, polygon.plane.normalY, polygon.plane.normalZ, true}); }
LevelMapVec3 centreOf(const MapFacePolygon& polygon)
{
	LevelMapVec3 centre {0, 0, 0, true};
	for (const auto& point : polygon.points) {
		centre = plus(centre, scaled(point, 1.0 / polygon.points.size()));
	}
	return centre;
}

struct Camera {
	LevelMapVec3 right;
	LevelMapVec3 up;
};
Camera camera(double yaw, double pitch)
{
	Camera result;
	LevelMapVec3 forward;
	levelCameraBasis(yaw, pitch, &forward, &result.right, &result.up);
	return result;
}

LevelSurfaceRequest shift(const Camera& view, double right, double up)
{
	LevelSurfaceRequest request;
	request.operation = LevelSurfaceOperation::CameraShift;
	request.x = right;
	request.y = up;
	request.cameraRight = view.right;
	request.cameraUp = view.up;
	return request;
}

LevelSurfaceRequest flip(const Camera& view, bool horizontal)
{
	LevelSurfaceRequest request;
	request.operation = LevelSurfaceOperation::CameraFlip;
	request.x = horizontal ? 1 : 0;
	request.y = horizontal ? 0 : 1;
	request.cameraRight = view.right;
	request.cameraUp = view.up;
	return request;
}

bool load(const QByteArray& bytes, LevelMapDocument* document)
{
	QString error;
	return expect(loadLevelMapBytes({QStringLiteral("camera.map"), {}, QStringLiteral("idtech3")}, bytes, document, &error), "load fixture", error);
}

// The face of brush 0 whose outward normal is nearest `direction`.
int faceFacing(const LevelMapDocument& map, const LevelMapVec3& direction)
{
	const auto geometry = solveBrushGeometry(map.brushes.first().faces);
	int best = -1;
	double bestDot = -2;
	for (int i = 0; i < geometry.faces.size(); ++i) {
		const double alignment = dot(normalOf(geometry.faces[i]), direction);
		if (geometry.faces[i].isValid() && alignment > bestDot) {
			bestDot = alignment;
			best = i;
		}
	}
	return best;
}

// A face's texture coordinates at `point`, in texels.
QPointF texels(const LevelMapBrushFace& face, const LevelMapVec3& point, QSize size)
{
	const auto projection = levelTextureProjection(face);
	const QPointF uv = projection.at(point);
	return projection.normalizedCoordinates ? QPointF(uv.x() * size.width(), uv.y() * size.height()) : uv;
}

// Where the picture that was at a face's centre has gone after the edit, as
// a world displacement within the face's plane.
LevelMapVec3 pictureMotion(const LevelMapBrushFace& before, const LevelMapBrushFace& after, const MapFacePolygon& polygon, QSize size)
{
	const LevelMapVec3 normal = normalOf(polygon);
	const LevelMapVec3 e1 = unit(minus(polygon.points[1], polygon.points[0]));
	const LevelMapVec3 e2 = unit(cross(normal, e1));
	const LevelMapVec3 centre = centreOf(polygon);
	const QPointF at = texels(after, centre, size), along1 = texels(after, plus(centre, e1), size) - at, along2 = texels(after, plus(centre, e2), size) - at;
	// Solve after(centre + a e1 + b e2) = before(centre).
	const QPointF wanted = texels(before, centre, size) - at;
	const double determinant = along1.x() * along2.y() - along2.x() * along1.y();
	const double a = (wanted.x() * along2.y() - along2.x() * wanted.y()) / determinant;
	const double b = (along1.x() * wanted.y() - wanted.x() * along1.y()) / determinant;
	return plus(scaled(e1, a), scaled(e2, b));
}

bool apply(LevelMapDocument* map, const QVector<LevelSurfaceFace>& faces, const QVector<int>& patches, const LevelSurfaceRequest& request,
	const QHash<QString, QSize>& sizes, LevelSurfaceEditPlan* plan, QString* error)
{
	return prepareLevelSurfaceEdits(*map, faces, patches, {request}, sizes, plan, error) && commitLevelSurfaceEdit(map, *plan, error);
}

LevelSurfaceRequest clockwise(double degrees)
{
	LevelSurfaceRequest request;
	request.operation = LevelSurfaceOperation::ClockwiseRotate;
	request.degrees = degrees;
	return request;
}

// A surface's texel coordinates at a world point.
using Mapping = std::function<QPointF(const LevelMapVec3&)>;

// How far the picture turned, in degrees anticlockwise on screen, for a viewer
// in front of a plane with outward normal `normal` and screen right `right`
// (up is then normal x right). The picture that was at `centre` and 16 units
// to its right is found again after the edit, at `centre + a right + b up`;
// both mappings are affine on the plane.
double pictureTurn(const Mapping& before, const Mapping& after, const LevelMapVec3& centre, const LevelMapVec3& normal, const LevelMapVec3& right)
{
	const LevelMapVec3 up = unit(cross(normal, right));
	const auto locate = [&](const LevelMapVec3& point) {
		const QPointF at = after(centre), along1 = after(plus(centre, right)) - at, along2 = after(plus(centre, up)) - at;
		const QPointF wanted = before(point) - at;
		const double determinant = along1.x() * along2.y() - along2.x() * along1.y();
		return QPointF((wanted.x() * along2.y() - along2.x() * wanted.y()) / determinant, (along1.x() * wanted.y() - wanted.x() * along1.y()) / determinant);
	};
	const QPointF from = locate(centre), to = locate(plus(centre, scaled(right, 16)));
	const QPointF arm = to - from;
	return std::atan2(arm.y(), arm.x()) * 180.0 / std::acos(-1.0);
}

// The face's own offsets, which a turn about the texture origin keeps.
QVector<double> offsetsOf(const LevelMapBrushFace& face)
{
	if (face.explicitTextureMatrix) {
		return {face.textureMatrix[2], face.textureMatrix[5]};
	}
	return face.explicitTextureAxes ? QVector<double> {face.uOffset, face.vOffset} : QVector<double> {face.shiftX, face.shiftY};
}

// Mirrors a face's texture along U, so it reads the other way round.
void mirrorU(LevelMapBrushFace* face)
{
	if (face->explicitTextureMatrix) {
		for (int i = 0; i < 3; ++i) {
			face->textureMatrix[i] = -face->textureMatrix[i];
		}
	} else {
		face->scaleX = -face->scaleX;
	}
	face->textureParametersDirty = true;
}

// A wall-facing box with plain classic mapping: rotation 0, scale 1.
QByteArray plainBox()
{
	LevelMapDocument document;
	LevelMapCreateRequest create;
	create.starterRoom = false;
	createLevelMap(create, &document);
	addLevelMapBoxBrush(&document, {0, 0, 0, true}, {64, 64, 64, true}, QStringLiteral("studio/grid"));
	for (auto& face : document.brushes.first().faces) {
		face.shiftX = face.shiftY = face.rotation = 0;
		face.scaleX = face.scaleY = 1;
		face.textureParametersDirty = true;
	}
	document.brushes.first().textureParametersDirty = true;
	return serializeLevelMap(document).bytes;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	expect(temp.isValid(), "temporary directory");
	QString error;
	const QSize imageSize(128, 64);
	const QHash<QString, QSize> sizes {{QStringLiteral("studio/grid"), imageSize}};

	phase = QStringLiteral("commands");
	const auto& commands = levelCameraTextureCommands();
	expect(commands.size() == 20, "twenty camera texture commands");
	{
		// The fixed-step turns: 15°, 90° and 1°, clockwise positive.
		int turns = 0;
		for (const auto& candidate : commands) {
			turns += levelCameraTextureRotates(candidate) ? 1 : 0;
		}
		expect(turns == 6 && levelCameraTextureAngle(LevelCameraTextureStep::Grid) == 15 && levelCameraTextureAngle(LevelCameraTextureStep::Coarse) == 90
				&& levelCameraTextureAngle(LevelCameraTextureStep::Fine) == 1,
			"six turns of 15°, 90° and 1°");
		LevelCameraTextureCommand turn;
		expect(levelCameraTextureCommandForId(QStringLiteral("map.rotate-textures-counterclockwise-coarse"), &turn)
				&& turn.commandId == QStringLiteral("map.rotateTexturesCounterclockwiseCoarse") && turn.direction == LevelCameraTextureDirection::Counterclockwise
				&& turn.step == LevelCameraTextureStep::Coarse && levelCameraTextureRotates(turn),
			"the palette's hyphenated id finds Rotate Textures Counter-clockwise (Coarse)");
		auto request = levelCameraTextureRequest(turn, {}, {}, 16);
		expect(request.operation == LevelSurfaceOperation::ClockwiseRotate && request.degrees == -90, "Counter-clockwise (Coarse) turns -90° and needs no camera");
		levelCameraTextureCommandForId(QStringLiteral("map.rotateTexturesClockwise"), &turn);
		request = levelCameraTextureRequest(turn, {}, {}, 64);
		expect(request.operation == LevelSurfaceOperation::ClockwiseRotate && request.degrees == 15, "Clockwise turns 15° whatever the grid");
		levelCameraTextureCommandForId(QStringLiteral("map.rotateTexturesClockwiseFine"), &turn);
		expect(levelCameraTextureRequest(turn, {}, {}, 8).degrees == 1, "Clockwise (Fine) turns 1°");
		// Seen from -Y, right is +X and up is +Z: right to up is anticlockwise.
		const LevelMapVec3 towardsViewer {0, -1, 0, true};
		expect(levelTextureTurnSense({1, 0, 0, true}, {0, 0, 1, true}, towardsViewer) == LevelTextureTurn::Counterclockwise
				&& levelTextureTurnSense({0, 0, 1, true}, {1, 0, 0, true}, towardsViewer) == LevelTextureTurn::Clockwise
				&& levelTextureTurnSense({1, 0, 0, true}, {0, 0, 1, true}, {0, 1, 0, true}) == LevelTextureTurn::Clockwise,
			"the turn's sense follows the side it is seen from");
		expect(levelTextureTurnSense({1, 0, 0, true}, {2, 0, 0, true}, towardsViewer) == LevelTextureTurn::None
				&& levelTextureTurnSense({0, 1, 0, true}, {1, 0, 0, true}, towardsViewer) == LevelTextureTurn::None
				&& levelTextureTurnSense({1, 0, 0, true}, {0, 0, 1, true}, {}) == LevelTextureTurn::None,
			"no sense for parallel directions, a direction along the normal or no normal");
	}
	LevelCameraTextureCommand found;
	expect(levelCameraTextureCommandForId(QStringLiteral("map.move-textures-up-coarse"), &found) && found.commandId == QStringLiteral("map.moveTexturesUpCoarse")
			&& found.step == LevelCameraTextureStep::Coarse && found.direction == LevelCameraTextureDirection::Up && !found.flip,
		"the palette's hyphenated id finds Move Textures Up (Coarse)");
	expect(levelCameraTextureCommandForId(QStringLiteral("map.flipTexturesVertically"), &found) && found.flip
			&& found.direction == LevelCameraTextureDirection::Up && !levelCameraTextureCommandForId(QStringLiteral("map.nudgeLeft")),
		"flips are commands; nudges are not");
	expect(levelCameraTextureDistance(LevelCameraTextureStep::Grid, 16) == 16 && levelCameraTextureDistance(LevelCameraTextureStep::Coarse, 16) == 32
			&& levelCameraTextureDistance(LevelCameraTextureStep::Fine, 16) == 1 && levelCameraTextureDistance(LevelCameraTextureStep::Fine, 0.125) == 1
			&& levelCameraTextureDistance(LevelCameraTextureStep::Grid, 0.5) == 0.5 && levelCameraTextureDistance(LevelCameraTextureStep::Grid, -1) == 0,
		"grid, twice the grid and one texel, whatever the grid");
	const Camera front = camera(90, 0); // Looking along +Y: right is +X, up is +Z.
	expect(near(front.right.x, 1) && near(front.right.y, 0) && near(front.up.z, 1), "the camera basis for yaw 90, pitch 0");
	LevelCameraTextureCommand command;
	levelCameraTextureCommandForId(QStringLiteral("map.moveTexturesLeft"), &command);
	auto request = levelCameraTextureRequest(command, front.right, front.up, 16);
	expect(request.operation == LevelSurfaceOperation::CameraShift && request.x == -16 && request.y == 0 && request.cameraRight.valid, "Left moves -grid across");
	levelCameraTextureCommandForId(QStringLiteral("map.moveTexturesUpFine"), &command);
	request = levelCameraTextureRequest(command, front.right, front.up, 64);
	expect(request.x == 0 && request.y == 1, "Up (Fine) moves one texel up");
	levelCameraTextureCommandForId(QStringLiteral("map.moveTexturesDownCoarse"), &command);
	request = levelCameraTextureRequest(command, front.right, front.up, 8);
	expect(request.x == 0 && request.y == -16, "Down (Coarse) moves twice the grid down");
	levelCameraTextureCommandForId(QStringLiteral("map.flipTexturesHorizontally"), &command);
	request = levelCameraTextureRequest(command, front.right, front.up, 8);
	expect(request.operation == LevelSurfaceOperation::CameraFlip && request.x == 1 && request.y == 0, "Flip Horizontally mirrors the axis across");
	expect(!levelCameraTextureBasisValid(front.right, front.right) && !levelCameraTextureBasisValid({}, front.up)
			&& !levelCameraTextureBasisValid({std::nan(""), 0, 0, true}, front.up) && levelCameraTextureBasisValid(front.right, front.up),
		"parallel, missing or non-finite camera directions are unusable");

	phase = QStringLiteral("axes");
	{
		// A Quake wall facing -Y: U along +X, V down the wall.
		const LevelMapVec3 u {1, 0, 0, true}, v {0, 0, -1, true}, wall {0, -1, 0, true};
		auto axes = levelCameraTextureAxes(u, v, wall, front.right, front.up);
		expect(axes.valid && axes.uHorizontal && axes.horizontalSign == 1 && axes.verticalSign == -1, "a wall seen from the front: U across, V down");
		auto moved = levelCameraTextureShift(axes, 16, 0);
		expect(near(moved.x(), -16) && near(moved.y(), 0), "moving right lowers the U offset");
		moved = levelCameraTextureShift(axes, 0, 16);
		expect(near(moved.x(), 0) && near(moved.y(), 16), "moving up raises the V offset, since V runs down");
		// From behind, right is -X: U still runs across, the other way.
		const Camera behind = camera(-90, 0);
		axes = levelCameraTextureAxes(u, v, wall, behind.right, behind.up);
		expect(axes.valid && axes.uHorizontal && axes.horizontalSign == -1, "from behind U runs right to left");
		// A texture turned a quarter: V runs across.
		axes = levelCameraTextureAxes({0, 0, 1, true}, {1, 0, 0, true}, wall, front.right, front.up);
		expect(axes.valid && !axes.uHorizontal && axes.horizontalSign == 1 && axes.verticalSign == 1, "a turned texture swaps the axes");
		moved = levelCameraTextureShift(axes, 16, 0);
		expect(near(moved.x(), 0) && near(moved.y(), -16), "moving right then shifts V");
		// A floor seen from straight above with yaw 0: right is -Y, up is +X.
		const Camera above = camera(0, -90);
		axes = levelCameraTextureAxes({1, 0, 0, true}, {0, -1, 0, true}, {0, 0, 1, true}, above.right, above.up);
		expect(axes.valid && !axes.uHorizontal && axes.horizontalSign == 1 && axes.verticalSign == 1, "a floor from above follows the camera's heading");
		// A floor seen from 45°: a tie, broken towards U.
		const Camera diagonal = camera(45, -90);
		axes = levelCameraTextureAxes({1, 0, 0, true}, {0, -1, 0, true}, {0, 0, 1, true}, diagonal.right, diagonal.up);
		expect(axes.valid && axes.uHorizontal, "a 45° tie on a floor puts U across");
		// The tie prefers the axis nearer the horizontal plane.
		const LevelMapVec3 slopeU = unit({1, 0, 1, true}), slopeV = unit({1, 0, -0.2, true});
		const Camera tilted {unit({1, 0, 0.4, true}), unit({-0.4, 0, 1, true})};
		axes = levelCameraTextureAxes(slopeU, slopeV, {0, -1, 0, true}, tilted.right, tilted.up);
		expect(axes.valid, "a skewed mapping still has axes");
		// Collapsed mappings and unusable cameras have no axes.
		expect(!levelCameraTextureAxes({0, 1, 0, true}, v, wall, front.right, front.up).valid, "an axis along the normal has no direction on the face");
		expect(!levelCameraTextureAxes(u, v, wall, front.right, front.right).valid, "a parallel camera has no axes");
	}

	phase = QStringLiteral("plain wall");
	{
		LevelMapDocument map;
		if (!load(plainBox(), &map)) {
			return 1;
		}
		const int wall = faceFacing(map, {0, -1, 0, true});
		const auto geometry = solveBrushGeometry(map.brushes.first().faces);
		const QVector<LevelSurfaceFace> target {{map.brushes.first().id, wall}};
		const auto source = serializeLevelMap(map).bytes;
		const auto before = map.brushes.first().faces[wall];
		LevelSurfaceEditPlan plan;
		expect(apply(&map, target, {}, shift(front, 16, 0), {}, &plan, &error) && plan.faceCount() == 1 && map.undoStack.size() == 1, "move right", error);
		auto motion = pictureMotion(before, map.brushes.first().faces[wall], geometry.faces[wall], imageSize);
		expect(near(motion.x, 16) && near(motion.y, 0) && near(motion.z, 0), "the picture moves 16 units to the right",
			QStringLiteral("%1 %2 %3").arg(motion.x).arg(motion.y).arg(motion.z));
		expect(near(map.brushes.first().faces[wall].shiftX, -16) && near(map.brushes.first().faces[wall].shiftY, 0), "through the classic U shift");
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "undo is exact", error);
		expect(apply(&map, target, {}, shift(front, 0, 8), {}, &plan, &error), "move up", error);
		motion = pictureMotion(before, map.brushes.first().faces[wall], geometry.faces[wall], imageSize);
		expect(near(motion.x, 0) && near(motion.z, 8), "the picture moves 8 units up", QStringLiteral("%1 %2 %3").arg(motion.x).arg(motion.y).arg(motion.z));
		undoLevelMapEdit(&map, &error);
		// The same keys from behind the wall move the picture the other way in
		// the world, which is still right on screen.
		const Camera behind = camera(-90, 0);
		expect(apply(&map, target, {}, shift(behind, 16, 0), {}, &plan, &error), "move right from behind", error);
		motion = pictureMotion(before, map.brushes.first().faces[wall], geometry.faces[wall], imageSize);
		expect(near(motion.x, -16), "from behind the picture moves towards -X", QString::number(motion.x));
		undoLevelMapEdit(&map, &error);
		// A turned texture: moving right shifts V.
		map.brushes.first().faces[wall].rotation = 90;
		map.brushes.first().faces[wall].textureParametersDirty = true;
		const auto turned = map.brushes.first().faces[wall];
		expect(apply(&map, target, {}, shift(front, 16, 0), {}, &plan, &error), "move a turned texture right", error);
		motion = pictureMotion(turned, map.brushes.first().faces[wall], geometry.faces[wall], imageSize);
		expect(near(motion.x, 16) && near(motion.z, 0) && near(map.brushes.first().faces[wall].shiftX, 0) && !near(map.brushes.first().faces[wall].shiftY, 0),
			"a turned texture moves right through its V shift", QStringLiteral("%1 %2").arg(motion.x).arg(motion.z));
		map.brushes.first().faces[wall] = before;
	}

	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		phase = QString::fromLatin1(dialect);
		LevelMapDocument map;
		if (!load(tests::surfaceFixture(phase), &map)) {
			return 1;
		}
		setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
		const auto faces = levelMapSelectedSurfaces(map);
		const auto source = serializeLevelMap(map).bytes;
		const auto before = map.brushes.first().faces;
		const auto geometry = solveBrushGeometry(before);
		const bool matrix = before.first().explicitTextureMatrix;
		// Brush-primitive terms are kept at the map writer's six decimals, which
		// moves a coordinate by up to a few thousandths of a texel.
		const double tolerance = matrix ? 0.02 : 1e-4;
		// A camera looking down at 30° from the south-west, so every face sees
		// the view at a slant.
		const Camera view = camera(40, -30);
		LevelSurfaceEditPlan plan;
		if (matrix) {
			expect(!prepareLevelSurfaceEdits(map, faces, {}, {shift(view, 16, 0)}, {}, &plan, &error) && error.contains(QStringLiteral("image size")),
				"a brush-primitive move needs the image size", error);
		}
		expect(apply(&map, faces, {}, shift(view, 16, 0), sizes, &plan, &error) && plan.faceCount() == 6 && map.undoStack.size() == 1,
			"move every face right in one undo step", error);
		for (int i = 0; i < before.size(); ++i) {
			const auto& after = map.brushes.first().faces[i];
			// The texture moved 16 texels along the axis across the view.
			const auto projection = levelTextureProjection(before[i]);
			const auto axes = levelCameraTextureAxes(projection.u, projection.v, normalOf(geometry.faces[i]), view.right, view.up);
			const auto centre = centreOf(geometry.faces[i]);
			const QPointF change = texels(after, centre, imageSize) - texels(before[i], centre, imageSize);
			const double across = axes.uHorizontal ? change.x() : change.y(), upward = axes.uHorizontal ? change.y() : change.x();
			expect(axes.valid && near(std::abs(across), 16, tolerance) && near(upward, 0, tolerance), "16 texels along the axis across the view",
				QStringLiteral("face %1: %2, %3").arg(i).arg(change.x()).arg(change.y()));
			// Facing the camera or not, the picture went towards screen right.
			const auto motion = pictureMotion(before[i], after, geometry.faces[i], imageSize);
			const auto towards = unit({view.right.x - normalOf(geometry.faces[i]).x * dot(view.right, normalOf(geometry.faces[i])),
				view.right.y - normalOf(geometry.faces[i]).y * dot(view.right, normalOf(geometry.faces[i])),
				view.right.z - normalOf(geometry.faces[i]).z * dot(view.right, normalOf(geometry.faces[i])), true});
			if (towards.valid && std::abs(dot(view.right, normalOf(geometry.faces[i]))) < 0.95) {
				expect(dot(motion, view.right) > 0, "the picture moves towards screen right", QStringLiteral("face %1: %2").arg(i).arg(dot(motion, view.right)));
			}
		}
		const auto moved = serializeLevelMap(map).bytes;
		expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "undo restores the exact source", error);
		expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == moved, "redo repeats the move", error);
		undoLevelMapEdit(&map, &error);

		// Flip Horizontally mirrors the axis across the view about each face's middle.
		expect(apply(&map, faces, {}, flip(view, true), sizes, &plan, &error) && plan.faceCount() == 6, "flip horizontally", error);
		for (int i = 0; i < before.size(); ++i) {
			const auto& after = map.brushes.first().faces[i];
			const auto projection = levelTextureProjection(before[i]);
			const auto axes = levelCameraTextureAxes(projection.u, projection.v, normalOf(geometry.faces[i]), view.right, view.up);
			const auto centre = centreOf(geometry.faces[i]);
			const auto corner = geometry.faces[i].points.first();
			const QPointF middle = texels(before[i], centre, imageSize), middleAfter = texels(after, centre, imageSize);
			const QPointF offset = texels(before[i], corner, imageSize) - middle, offsetAfter = texels(after, corner, imageSize) - middleAfter;
			const bool flippedU = axes.uHorizontal;
			expect(near(middle.x(), middleAfter.x(), tolerance) && near(middle.y(), middleAfter.y(), tolerance)
					&& near(offsetAfter.x(), flippedU ? -offset.x() : offset.x(), tolerance)
					&& near(offsetAfter.y(), flippedU ? offset.y() : -offset.y(), tolerance),
				"the axis across the view is mirrored about the middle",
				QStringLiteral("face %1: %2,%3 -> %4,%5").arg(i).arg(offset.x()).arg(offset.y()).arg(offsetAfter.x()).arg(offsetAfter.y()));
		}
		undoLevelMapEdit(&map, &error);
		expect(apply(&map, faces, {}, flip(view, false), {}, &plan, &error) && plan.faceCount() == 6, "a flip needs no image size", error);
		undoLevelMapEdit(&map, &error);
		expect(serializeLevelMap(map).bytes == source, "flips undo exactly");

		// Unusable cameras and zero moves.
		auto parallel = shift(view, 16, 0);
		parallel.cameraUp = parallel.cameraRight;
		expect(!prepareLevelSurfaceEdits(map, faces, {}, {parallel}, sizes, &plan, &error) && error.contains(QStringLiteral("camera")),
			"a parallel camera is refused", error);
		const auto revision = map.revision;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {shift(view, 0, 0)}, sizes, &plan, &error) && plan.faceCount() == 0
				&& commitLevelSurfaceEdit(&map, plan, &error) && map.revision == revision,
			"a zero move is a no-op without an undo step", error);

		// A locked layer refuses the move.
		QString layer;
		expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
				&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&map, layer, true, &error),
			"lock the brush", error);
		const auto locked = serializeLevelMap(map).bytes;
		const auto lockedRevision = map.revision;
		expect(prepareLevelSurfaceEdits(map, faces, {}, {shift(view, 16, 0)}, sizes, &plan, &error) && !commitLevelSurfaceEdit(&map, plan, &error)
				&& map.revision == lockedRevision && serializeLevelMap(map).bytes == locked,
			"a locked brush refuses the move", error);
	}

	// A flat patch: the move follows the direction its coordinates grow in.
	phase = QStringLiteral("patch");
	{
		LevelMapDocument created;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake3");
		create.starterRoom = false;
		createLevelMap(create, &created, &error);
		LevelPatchCreateRequest patchRequest;
		patchRequest.shape = QStringLiteral("plane");
		patchRequest.texture = QStringLiteral("studio/grid");
		patchRequest.size = {256, 128, 128, true};
		LevelMapPatch plane;
		int patchId = -1;
		expect(createLevelPatch(patchRequest, &plane, &error) && addLevelMapPatch(&created, plane, &patchId, &error), "patch fixture", error);
		LevelMapDocument document;
		load(serializeLevelMap(created).bytes, &document);
		if (!expect(document.patches.size() == 1 && document.patches.first().width >= 2 && document.patches.first().height >= 2, "patch reloaded")) {
			return 1;
		}
		const auto start = document.patches.first();
		patchId = start.id;
		const auto source = serializeLevelMap(document).bytes;
		// The patch's own directions, from its corners.
		const int last = start.width * start.height - 1;
		const auto corner = start.controlPoints[0];
		const auto alongRow = minus(start.controlPoints[start.width - 1], corner), downColumn = minus(start.controlPoints[last - (start.width - 1)], corner);
		const LevelMapVec3 normal = unit(cross(alongRow, downColumn));
		// Look at its front, with the row running across.
		const LevelMapVec3 forward = scaled(normal, -1), right = unit(alongRow);
		const Camera view {right, unit(cross(right, forward))};
		LevelSurfaceEditPlan plan;
		expect(!prepareLevelSurfaceEdits(document, {}, {patchId}, {shift(view, 0, 16)}, {}, &plan, &error), "a patch move needs the image size");
		expect(apply(&document, {}, {patchId}, shift(view, 0, 16), sizes, &plan, &error) && plan.patchCount() == 1 && document.undoStack.size() == 1,
			"move the patch up", error);
		const auto& moved = document.patches.first();
		// The coordinate that grows up the view changed by 16 texels against it.
		const double rowU = start.controlU[start.width - 1] - start.controlU[0], rowV = start.controlV[start.width - 1] - start.controlV[0];
		const double columnU = start.controlU[last - (start.width - 1)] - start.controlU[0], columnV = start.controlV[last - (start.width - 1)] - start.controlV[0];
		const double upU = dot(downColumn, view.up) * columnU + dot(alongRow, view.up) * rowU;
		const double upV = dot(downColumn, view.up) * columnV + dot(alongRow, view.up) * rowV;
		const bool vUp = std::abs(upV) * imageSize.height() > std::abs(upU) * imageSize.width();
		bool same = true;
		for (int i = 0; i < start.controlU.size(); ++i) {
			const double du = (moved.controlU[i] - start.controlU[i]) * imageSize.width(), dv = (moved.controlV[i] - start.controlV[i]) * imageSize.height();
			const double expected = (vUp ? upV : upU) > 0 ? -16 : 16;
			same &= vUp ? near(dv, expected, 1e-6) && near(du, 0, 1e-6) : near(du, expected, 1e-6) && near(dv, 0, 1e-6);
		}
		expect(same, "16 texels against the coordinate growing up the view");
		expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == source, "patch undo is exact", error);
		expect(apply(&document, {}, {patchId}, flip(view, true), {}, &plan, &error) && plan.patchCount() == 1, "flip the patch horizontally without an image", error);
		same = true;
		const auto& flipped = document.patches.first();
		const auto [minU, maxU] = std::minmax_element(start.controlU.cbegin(), start.controlU.cend());
		const auto [minV, maxV] = std::minmax_element(start.controlV.cbegin(), start.controlV.cend());
		for (int i = 0; i < start.controlU.size(); ++i) {
			// The axis across the view mirrors about its middle; the other stays.
			const double mirroredU = *minU + *maxU - start.controlU[i], mirroredV = *minV + *maxV - start.controlV[i];
			same &= vUp ? near(flipped.controlV[i], start.controlV[i]) && near(flipped.controlU[i], mirroredU)
						: near(flipped.controlU[i], start.controlU[i]) && near(flipped.controlV[i], mirroredV);
		}
		expect(same, "a horizontal flip mirrors the patch's axis across the view and keeps the other");
		undoLevelMapEdit(&document, &error);
	}

	// The fixed-step turns, as seen from each face's front, on every face
	// format, with the fixture's texture and with it mirrored along U.
	for (const auto* dialect : {"classic", "valve220", "brushDef", "brushDef3"}) {
		for (const bool mirrored : {false, true}) {
			phase = QStringLiteral("rotate %1%2").arg(QLatin1String(dialect), mirrored ? QStringLiteral(" mirrored") : QString());
			LevelMapDocument map;
			if (!load(tests::surfaceFixture(QString::fromLatin1(dialect)), &map)) {
				return 1;
			}
			if (mirrored) {
				for (auto& face : map.brushes.first().faces) {
					mirrorU(&face);
				}
				map.brushes.first().textureParametersDirty = true;
				LevelMapDocument reloaded;
				if (!load(serializeLevelMap(map).bytes, &reloaded)) {
					return 1;
				}
				map = reloaded;
			}
			setLevelMapSelection(&map, {{LevelMapSelectionKind::QuakeBrush, 0}});
			const auto faces = levelMapSelectedSurfaces(map);
			const auto source = serializeLevelMap(map).bytes;
			const auto before = map.brushes.first().faces;
			const auto geometry = solveBrushGeometry(before);
			// Brush-primitive terms keep six decimals, which can move an angle by
			// a few hundredths of a degree.
			const double tolerance = before.first().explicitTextureMatrix ? 0.05 : 1e-3;
			for (const double degrees : {15.0, 90.0, 1.0, -15.0}) {
				LevelSurfaceEditPlan plan;
				// Brush-primitive faces turn their plane coordinates, so no image size is needed.
				if (!expect(apply(&map, faces, {}, clockwise(degrees), {}, &plan, &error) && plan.faceCount() == 6 && map.undoStack.size() == 1,
						"turn every face in one undo step", QStringLiteral("%1°: %2").arg(degrees).arg(error))) {
					break;
				}
				for (int i = 0; i < before.size(); ++i) {
					const auto& after = map.brushes.first().faces[i];
					const auto& polygon = geometry.faces[i];
					const LevelMapVec3 normal = normalOf(polygon), right = unit(minus(polygon.points[1], polygon.points[0]));
					const Mapping was = [&, i](const LevelMapVec3& point) { return texels(before[i], point, imageSize); };
					const Mapping now = [&](const LevelMapVec3& point) { return texels(after, point, imageSize); };
					const double turned = pictureTurn(was, now, centreOf(polygon), normal, right);
					// Clockwise on screen is a negative anticlockwise angle.
					expect(near(std::remainder(turned + degrees, 360.0), 0, tolerance), "the picture turns clockwise as seen from the face's front",
						QStringLiteral("face %1, %2° asked: %3° anticlockwise").arg(i).arg(degrees).arg(turned));
					expect(offsetsOf(after) == offsetsOf(before[i]), "the offsets stay, so the turn is about the texture origin", QString::number(i));
				}
				const auto turnedBytes = serializeLevelMap(map).bytes;
				expect(undoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == source, "undo restores the exact source", error);
				expect(redoLevelMapEdit(&map, &error) && serializeLevelMap(map).bytes == turnedBytes, "redo repeats the turn", error);
				undoLevelMapEdit(&map, &error);
			}
			const auto revision = map.revision;
			LevelSurfaceEditPlan plan;
			expect(prepareLevelSurfaceEdits(map, faces, {}, {clockwise(360)}, {}, &plan, &error) && plan.faceCount() == 0
					&& commitLevelSurfaceEdit(&map, plan, &error) && map.revision == revision,
				"a whole turn is a no-op without an undo step", error);
			if (!mirrored) {
				QString layer;
				expect(createLevelSceneNode(&map, LevelSceneNodeKind::Layer, QStringLiteral("Locked"), {}, &layer, &error)
						&& assignLevelSceneObjects(&map, layer, {QStringLiteral("brush:0")}, &error) && setLevelSceneLocked(&map, layer, true, &error),
					"lock the brush", error);
				const auto locked = serializeLevelMap(map).bytes;
				expect(prepareLevelSurfaceEdits(map, faces, {}, {clockwise(15)}, {}, &plan, &error) && !commitLevelSurfaceEdit(&map, plan, &error)
						&& serializeLevelMap(map).bytes == locked,
					"a locked brush refuses the turn", error);
			}
		}
	}

	// A flat patch turns its coordinates about 0,0, clockwise from the side its
	// normal faces, mirrored or not.
	for (const bool mirrored : {false, true}) {
		phase = mirrored ? QStringLiteral("rotate patch mirrored") : QStringLiteral("rotate patch");
		LevelMapDocument created;
		LevelMapCreateRequest create;
		create.game = QStringLiteral("quake3");
		create.starterRoom = false;
		createLevelMap(create, &created, &error);
		LevelPatchCreateRequest patchRequest;
		patchRequest.shape = QStringLiteral("plane");
		patchRequest.texture = QStringLiteral("studio/grid");
		patchRequest.size = {256, 128, 128, true};
		LevelMapPatch plane;
		int patchId = -1;
		expect(createLevelPatch(patchRequest, &plane, &error) && addLevelMapPatch(&created, plane, &patchId, &error), "patch fixture", error);
		if (mirrored) {
			for (double& u : created.patches.first().controlU) {
				u = -u;
			}
		}
		LevelMapDocument document;
		load(serializeLevelMap(created).bytes, &document);
		if (!expect(document.patches.size() == 1 && document.patches.first().width >= 2 && document.patches.first().height >= 2, "patch reloaded")) {
			return 1;
		}
		const auto start = document.patches.first();
		patchId = start.id;
		const auto source = serializeLevelMap(document).bytes;
		const int last = start.width * start.height - 1;
		const auto corner = start.controlPoints[0];
		const auto alongRow = minus(start.controlPoints[start.width - 1], corner), downColumn = minus(start.controlPoints[last - (start.width - 1)], corner);
		const LevelMapVec3 normal = unit(cross(alongRow, downColumn));
		// The patch's texels at a point on its plane, from three corners.
		const auto mapping = [&](const LevelMapPatch& patch) {
			return Mapping([&, patch](const LevelMapVec3& point) {
				const auto offset = minus(point, corner);
				const double a = dot(offset, alongRow) / dot(alongRow, alongRow), b = dot(offset, downColumn) / dot(downColumn, downColumn);
				const auto texel = [&](int index) { return QPointF(patch.controlU[index] * imageSize.width(), patch.controlV[index] * imageSize.height()); };
				return texel(0) + a * (texel(start.width - 1) - texel(0)) + b * (texel(last - (start.width - 1)) - texel(0));
			});
		};
		const LevelMapVec3 middle = plus(corner, plus(scaled(alongRow, 0.5), scaled(downColumn, 0.5)));
		LevelSurfaceEditPlan plan;
		expect(!prepareLevelSurfaceEdits(document, {}, {patchId}, {clockwise(15)}, {}, &plan, &error), "a patch turn needs the image size");
		for (const double degrees : {15.0, -90.0}) {
			if (!expect(apply(&document, {}, {patchId}, clockwise(degrees), sizes, &plan, &error) && plan.patchCount() == 1 && document.undoStack.size() == 1,
					"turn the patch", error)) {
				break;
			}
			const auto& turnedPatch = document.patches.first();
			const double turned = pictureTurn(mapping(start), mapping(turnedPatch), middle, normal, unit(alongRow));
			expect(near(std::remainder(turned + degrees, 360.0), 0, 1e-3), "the patch's picture turns clockwise from its front",
				QStringLiteral("%1° asked: %2° anticlockwise").arg(degrees).arg(turned));
			bool aboutOrigin = true;
			for (int i = 0; i < start.controlU.size(); ++i) {
				aboutOrigin &= near(std::hypot(start.controlU[i] * imageSize.width(), start.controlV[i] * imageSize.height()),
					std::hypot(turnedPatch.controlU[i] * imageSize.width(), turnedPatch.controlV[i] * imageSize.height()), 1e-3);
			}
			expect(aboutOrigin, "every coordinate keeps its distance from 0,0 in texels");
			expect(undoLevelMapEdit(&document, &error) && serializeLevelMap(document).bytes == source, "patch undo is exact", error);
		}
	}

	if (argc > 1) {
		phase = QStringLiteral("cli");
		const QString binary = QString::fromLocal8Bit(argv[1]);
		const auto run = [&](const QString& map, const QStringList& arguments, int* exitCode) {
			QProcess process;
			process.start(binary, QStringList {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("align-textures"), map} + arguments
				+ QStringList {QStringLiteral("--json")});
			const bool ended = process.waitForFinished(60000);
			*exitCode = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
			const auto bytes = process.readAllStandardOutput();
			if (*exitCode != 0) {
				std::cerr << bytes.constData() << process.readAllStandardError().constData() << std::endl;
			}
			return QJsonDocument::fromJson(bytes).object();
		};
		const auto savedBytes = [](const QString& path) {
			LevelMapDocument saved;
			QString problem;
			expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &saved, &problem), "saved map loads", problem);
			return serializeLevelMap(saved).bytes;
		};
		const auto classic = QDir(temp.path()).filePath(QStringLiteral("classic.map"));
		const auto primitive = QDir(temp.path()).filePath(QStringLiteral("primitive.map"));
		tests::putMaterialFile(classic, tests::surfaceFixture(QStringLiteral("classic")));
		tests::putMaterialFile(primitive, tests::surfaceFixture(QStringLiteral("brushDef")));
		const auto out = [&](const char* name) { return QDir(temp.path()).filePath(QString::fromLatin1(name)); };
		const auto reference = [&](const QString& path, const LevelSurfaceRequest& surface, const QHash<QString, QSize>& known) {
			LevelMapDocument expected;
			QString problem;
			LevelSurfaceEditPlan plan;
			expect(loadLevelMap({path, {}, QStringLiteral("idtech3")}, &expected, &problem)
					&& setLevelMapSelection(&expected, {{LevelMapSelectionKind::QuakeBrush, 0}}, &problem)
					&& apply(&expected, levelMapSelectedSurfaces(expected), {}, surface, known, &plan, &problem),
				"reference edit", problem);
			return serializeLevelMap(expected).bytes;
		};
		const QStringList brush {QStringLiteral("--engine"), QStringLiteral("idTech3"), QStringLiteral("--object"), QStringLiteral("brush:0")};
		int code = -1;
		auto json = run(classic, brush + QStringList {QStringLiteral("--camera-shift"), QStringLiteral("16,-8"), QStringLiteral("--camera-yaw"), QStringLiteral("40"),
			QStringLiteral("--camera-pitch"), QStringLiteral("-30"), QStringLiteral("--output"), out("moved.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("operation")).toString() == QStringLiteral("camera-shift") && json.value(QStringLiteral("facesChanged")).toInt() == 6
				&& near(json.value(QStringLiteral("cameraYaw")).toDouble(), 40) && near(json.value(QStringLiteral("cameraPitch")).toDouble(), -30),
			"map align-textures --camera-shift reports the camera", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		const Camera view = camera(40, -30);
		expect(savedBytes(out("moved.map")) == reference(classic, shift(view, 16, -8), {}), "the CLI writes what the shared service writes");
		json = run(classic, brush + QStringList {QStringLiteral("--camera-flip"), QStringLiteral("vertical"), QStringLiteral("--camera-yaw"), QStringLiteral("40"),
			QStringLiteral("--camera-pitch"), QStringLiteral("-30"), QStringLiteral("--output"), out("flipped.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("operation")).toString() == QStringLiteral("camera-flip")
				&& savedBytes(out("flipped.map")) == reference(classic, flip(view, false), {}),
			"--camera-flip vertical matches the shared flip", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		json = run(classic, brush + QStringList {QStringLiteral("--camera-shift"), QStringLiteral("16,0"), QStringLiteral("--output"), out("default.map")}, &code);
		expect(code == 0 && savedBytes(out("default.map")) == reference(classic, shift(camera(0, 0), 16, 0), {}), "the camera defaults to yaw 0, pitch 0");
		run(primitive, brush + QStringList {QStringLiteral("--camera-shift"), QStringLiteral("16,0"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "a brush-primitive move needs a texture size");
		run(primitive, brush + QStringList {QStringLiteral("--camera-shift"), QStringLiteral("16,0"), QStringLiteral("--texture-size"), QStringLiteral("128,64"),
			QStringLiteral("--output"), out("primitive-moved.map")}, &code);
		expect(code == 0, "a brush-primitive move with --texture-size");
		expect(savedBytes(out("primitive-moved.map")) == reference(primitive, shift(camera(0, 0), 16, 0), sizes),
			"the CLI's brush-primitive move matches the shared service",
			firstDifference(savedBytes(out("primitive-moved.map")), reference(primitive, shift(camera(0, 0), 16, 0), sizes)));
		run(classic, brush + QStringList {QStringLiteral("--camera-flip"), QStringLiteral("sideways"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--camera-flip takes horizontal or vertical");
		run(classic, brush + QStringList {QStringLiteral("--shift"), QStringLiteral("16,0"), QStringLiteral("--camera-yaw"), QStringLiteral("40"),
			QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--camera-yaw goes only with the camera operations");
		run(classic, brush + QStringList {QStringLiteral("--camera-shift"), QStringLiteral("16,0"), QStringLiteral("--camera-pitch"), QStringLiteral("120"),
			QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--camera-pitch stays within -90 to 90");
		run(classic, brush + QStringList {QStringLiteral("--camera-shift"), QStringLiteral("16"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--camera-shift takes two values");
		run(classic, brush + QStringList {QStringLiteral("--camera-shift"), QStringLiteral("16,0"), QStringLiteral("--shift"), QStringLiteral("1,1"),
			QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "one operation at a time");

		// --clockwise is the Rotate Textures turn: degrees clockwise from each
		// face's front, about the texture origin.
		json = run(classic, brush + QStringList {QStringLiteral("--clockwise"), QStringLiteral("15"), QStringLiteral("--output"), out("turned.map")}, &code);
		expect(code == 0 && json.value(QStringLiteral("operation")).toString() == QStringLiteral("clockwise") && json.value(QStringLiteral("facesChanged")).toInt() == 6
				&& json.value(QStringLiteral("anchor")).toString() == QStringLiteral("origin") && near(json.value(QStringLiteral("clockwiseDegrees")).toDouble(), 15),
			"map align-textures --clockwise reports the turn", QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
		expect(savedBytes(out("turned.map")) == reference(classic, clockwise(15), {}), "--clockwise writes what the shared turn writes",
			firstDifference(savedBytes(out("turned.map")), reference(classic, clockwise(15), {})));
		run(primitive, brush + QStringList {QStringLiteral("--clockwise"), QStringLiteral("-90"), QStringLiteral("--output"), out("primitive-turned.map")}, &code);
		expect(code == 0 && savedBytes(out("primitive-turned.map")) == reference(primitive, clockwise(-90), {}),
			"a counter-clockwise brush-primitive turn needs no texture size and matches the shared turn");
		run(classic, brush + QStringList {QStringLiteral("--clockwise"), QStringLiteral("quarter"), QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--clockwise takes a number of degrees");
		run(classic, brush + QStringList {QStringLiteral("--clockwise"), QStringLiteral("15"), QStringLiteral("--anchor"), QStringLiteral("centre"),
			QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "--anchor goes only with --degrees");
		run(classic, brush + QStringList {QStringLiteral("--clockwise"), QStringLiteral("15"), QStringLiteral("--camera-yaw"), QStringLiteral("40"),
			QStringLiteral("--output"), out("refused.map")}, &code);
		expect(code != 0 && !QFile::exists(out("refused.map")), "a turn takes no camera");
	}
	std::cout << (ok ? "Camera texture checks passed" : "Camera texture checks failed") << std::endl;
	return ok ? 0 : 1;
}
