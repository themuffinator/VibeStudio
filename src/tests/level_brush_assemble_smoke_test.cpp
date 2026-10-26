// The Assemble Brush tool's geometry (core/level_brush_assemble.h): the shape
// a point set makes (none, a point, a segment, a flat outline in order with
// its plane, or a hull with its corners, edges, faces and absorbed points),
// bad and oversized input, grid points on a face that stay on it, the grid
// rectangle on a sloped plane, rays meeting planes and outlines, the
// extrusion distance along a normal by grid steps, sweeping points by an
// offset and appending without duplicates; the TrenchBroom workflow end to
// end (a face rectangle, its extrusion and Create Brush as one exact undo
// step); and, with the vibestudio binary as the first argument,
// `map add-brush --points --extrude`. Synthetic maps and owned temporary files
// only.
#include "core/level_brush_assemble.h"
#include "core/level_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool ok = true;

bool expect(bool value, const char* label, const QString& detail = {})
{
	if (!value) {
		ok = false;
		std::cerr << "FAIL: " << label << ": " << qPrintable(detail) << std::endl;
	}
	return value;
}

bool near(const LevelMapVec3& a, const LevelMapVec3& b, double tolerance = 1e-9)
{
	return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

bool contains(const QVector<LevelMapVec3>& points, const LevelMapVec3& point)
{
	return std::any_of(points.cbegin(), points.cend(), [&](const LevelMapVec3& p) { return near(p, point); });
}

QString text(const QVector<LevelMapVec3>& points)
{
	QStringList parts;
	for (const auto& p : points) {
		parts << levelMapPointText(p);
	}
	return parts.join(QLatin1Char(' '));
}

QByteArray bytes(const LevelMapDocument& document)
{
	return serializeLevelMap(document).bytes;
}

MapPlane axisPlane(int axis, double distance)
{
	MapPlane plane;
	plane.valid = true;
	plane.distance = distance;
	(axis == 0 ? plane.normalX : axis == 1 ? plane.normalY : plane.normalZ) = 1.0;
	return plane;
}

BoxResizeRay ray(LevelMapVec3 origin, LevelMapVec3 direction, bool forwardOnly = true)
{
	return {{origin.x, origin.y, origin.z}, {direction.x, direction.y, direction.z}, forwardOnly};
}

QByteArray fixture()
{
	return QByteArrayLiteral("{\n\"classname\" \"worldspawn\"\n{\n"
							 "( 0 0 64 ) ( 1 0 64 ) ( 0 1 64 ) base/box 0 0 0 1 1\n"
							 "( 0 0 0 ) ( 0 1 0 ) ( 1 0 0 ) base/box 0 0 0 1 1\n"
							 "( 64 0 0 ) ( 64 1 0 ) ( 64 0 1 ) base/box 0 0 0 1 1\n"
							 "( 0 0 0 ) ( 0 0 1 ) ( 0 1 0 ) base/box 0 0 0 1 1\n"
							 "( 0 64 0 ) ( 0 64 1 ) ( 1 64 0 ) base/box 0 0 0 1 1\n"
							 "( 0 0 0 ) ( 1 0 0 ) ( 0 0 1 ) base/box 0 0 0 1 1\n"
							 "}\n}\n");
}

QJsonObject runCli(const QString& binary, const QStringList& args, int expected, QByteArray* out = nullptr)
{
	QProcess process;
	process.start(binary, args);
	const bool finished = process.waitForFinished(60000);
	const QByteArray stdoutBytes = process.readAllStandardOutput();
	expect(finished && process.exitCode() == expected, "CLI exit code",
		QStringLiteral("%1 for %2: %3 %4").arg(process.exitCode()).arg(args.join(QLatin1Char(' ')), QString::fromUtf8(stdoutBytes),
			QString::fromUtf8(process.readAllStandardError())));
	if (out) {
		*out = stdoutBytes;
	}
	return QJsonDocument::fromJson(stdoutBytes).object();
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;

	// Shapes: nothing, one point (duplicates absorbed), a segment's ends.
	LevelBrushAssembleOutline outline;
	expect(levelBrushAssembleOutline({}, &outline, &error) && outline.shape == LevelBrushAssembleShape::Empty && outline.corners.isEmpty(),
		"no points", error);
	expect(levelBrushAssembleOutline({{8, 8, 8, true}, {8, 8, 8, true}}, &outline, &error) && outline.shape == LevelBrushAssembleShape::Point
			&& outline.corners.size() == 1 && outline.unusedPoints == 1,
		"one point", error);
	expect(levelBrushAssembleOutline({{0, 0, 0, true}, {32, 0, 0, true}, {64, 0, 0, true}, {16, 0, 0, true}}, &outline, &error)
			&& outline.shape == LevelBrushAssembleShape::Segment && outline.corners.size() == 2 && contains(outline.corners, {0, 0, 0, true})
			&& contains(outline.corners, {64, 0, 0, true}) && outline.edges.size() == 1 && outline.unusedPoints == 2,
		"a segment's ends", error);

	// A flat outline: the corners in order, interior and edge points absorbed,
	// its plane through them, its sides as the wire.
	const QVector<LevelMapVec3> square {{0, 0, 32, true}, {64, 64, 32, true}, {64, 0, 32, true}, {32, 32, 32, true}, {0, 64, 32, true}, {32, 0, 32, true}};
	expect(levelBrushAssembleOutline(square, &outline, &error) && outline.shape == LevelBrushAssembleShape::Polygon && outline.corners.size() == 4
			&& outline.edges.size() == 4 && outline.unusedPoints == 2 && outline.plane.valid && std::abs(std::abs(outline.plane.normalZ) - 1) < 1e-12
			&& std::abs(outline.plane.distance - 32 * outline.plane.normalZ) < 1e-9,
		"a flat outline", text(outline.corners) + error);
	bool adjacent = true;
	for (int i = 0; i < outline.corners.size(); ++i) {
		const auto& a = outline.corners[i];
		const auto& b = outline.corners[(i + 1) % outline.corners.size()];
		adjacent = adjacent && (a.x == b.x || a.y == b.y);
	}
	expect(adjacent, "outline corners go round the square in order", text(outline.corners));

	// A volume: the hull's corners, edges and faces; the costly part optional.
	const QVector<LevelMapVec3> prism = levelBrushPointsExtruded(square, {0, 0, 32, true});
	expect(prism.size() == 12 && prism[6].z == 64 && prism[0].z == 32, "points swept by an offset", text(prism));
	expect(levelBrushAssembleOutline(prism, &outline, &error, false) && outline.shape == LevelBrushAssembleShape::Volume && outline.corners.isEmpty(),
		"a volume without its hull", error);
	expect(levelBrushAssembleOutline(prism, &outline, &error) && outline.shape == LevelBrushAssembleShape::Volume && outline.corners.size() == 8
			&& outline.edges.size() == 12 && outline.faces == 6 && outline.unusedPoints == 4,
		"a volume's hull", error);

	// Bad input.
	expect(!levelBrushAssembleOutline({{0, 0, 40000, true}}, &outline, &error) && !error.isEmpty(), "out of range refused", error);
	expect(!levelBrushAssembleOutline({{0, 0, std::nan(""), true}}, &outline, &error), "not finite refused", error);
	expect(!levelBrushAssembleOutline(QVector<LevelMapVec3>(kLevelBrushPointsMaximum + 1, {1, 2, 3, true}), &outline, &error)
			&& error.contains(QString::number(kLevelBrushPointsMaximum)),
		"too many refused", error);

	// Grid points on a face stay on the face; a coarse grid keeps the hit.
	LevelBrushAssembleSurface top {axisPlane(2, 64), {{0, 0, 64, true}, {64, 0, 64, true}, {64, 64, 64, true}, {0, 64, 64, true}}};
	expect(near(levelBrushAssembleSurfacePoint({13, 21, 64, true}, top, 16), {16, 16, 64, true}), "grid point on a face");
	expect(near(levelBrushAssembleSurfacePoint({13, 21, 64, true}, top, 0), {13, 21, 64, true}), "no grid keeps the hit");
	expect(near(levelBrushAssembleSurfacePoint({60, 21, 64, true}, top, 96), {60, 21, 64, true}), "rounding off the face keeps the hit");
	// A slope: x across, z follows the plane z = x / 2.
	MapPlane slope;
	slope.valid = true;
	slope.normalX = -1 / std::sqrt(5.0);
	slope.normalZ = 2 / std::sqrt(5.0);
	slope.distance = 0;
	const LevelBrushAssembleSurface sloped {slope, {}};
	const auto onSlope = levelBrushAssembleSurfacePoint({30, 9, 15, true}, sloped, 8);
	expect(near(onSlope, {32, 8, 16, true}, 1e-9) && std::abs(planeDistanceToPoint(slope, onSlope)) < 1e-9, "grid point on a slope",
		levelMapPointText(onSlope));

	// The grid rectangle: low corner down, high corner up, on the plane.
	const auto rectangle = levelBrushAssembleRectangle(top.plane, {37, 3, 64, true}, {9, 50, 64, true}, 16);
	expect(rectangle.size() == 4 && contains(rectangle, {0, 0, 64, true}) && contains(rectangle, {48, 0, 64, true}) && contains(rectangle, {48, 64, 64, true})
			&& contains(rectangle, {0, 64, 64, true}),
		"grid rectangle on a face", text(rectangle));
	const auto tilted = levelBrushAssembleRectangle(slope, {3, 0, 1.5, true}, {29, 30, 14.5, true}, 8);
	expect(tilted.size() == 4 && contains(tilted, {0, 0, 0, true}) && contains(tilted, {32, 32, 16, true})
			&& std::all_of(tilted.cbegin(), tilted.cend(), [&](const LevelMapVec3& p) { return std::abs(planeDistanceToPoint(slope, p)) < 1e-9; }),
		"rectangle on a sloped plane", text(tilted));

	// Rays: a plane ahead, never behind; an outline from either side.
	LevelMapVec3 hit;
	expect(levelBrushAssembleRayPlane(ray({16, 16, 200, true}, {0, 0, -1, true}), top.plane, &hit) && near(hit, {16, 16, 64, true}), "ray meets a plane");
	expect(!levelBrushAssembleRayPlane(ray({16, 16, 200, true}, {0, 0, 1, true}), top.plane, &hit), "a plane behind a forward ray");
	expect(!levelBrushAssembleRayPlane(ray({16, 16, 200, true}, {1, 0, 0, true}), top.plane, &hit), "a ray along the plane");
	expect(levelBrushAssembleOutline(square, &outline, &error), "outline again", error);
	expect(levelBrushAssembleRayPolygon(ray({10, 20, 100, true}, {0, 0, -1, true}), outline, &hit) && near(hit, {10, 20, 32, true}),
		"ray meets the outline from above");
	expect(levelBrushAssembleRayPolygon(ray({10, 20, -100, true}, {0, 0, 1, true}), outline, &hit), "and from below");
	expect(!levelBrushAssembleRayPolygon(ray({90, 20, 100, true}, {0, 0, -1, true}), outline, &hit), "a ray beside the outline");

	// Extrusion: the distance along the normal to the ray, by grid steps.
	double distance = 0;
	const LevelMapVec3 up {0, 0, 1, true};
	expect(levelBrushAssembleExtrusionDistance(ray({-200, 32, 75, true}, {1, 0, 0, true}), {32, 32, 32, true}, up, 16, &distance) && distance == 48,
		"extrusion snaps to the grid", QString::number(distance));
	expect(levelBrushAssembleExtrusionDistance(ray({-200, 32, 75, true}, {1, 0, 0, true}), {32, 32, 32, true}, up, 0, &distance) && distance == 43,
		"extrusion without a grid", QString::number(distance));
	expect(levelBrushAssembleExtrusionDistance(ray({-200, 32, 0, true}, {1, 0, 0, true}), {32, 32, 32, true}, up, 16, &distance) && distance == -32,
		"extrusion below the outline", QString::number(distance));
	expect(!levelBrushAssembleExtrusionDistance(ray({32, 32, 200, true}, {0, 0, -1, true}), {32, 32, 32, true}, up, 16, &distance),
		"a ray along the extrusion");

	// Appending skips points already present.
	QVector<LevelMapVec3> collected {{0, 0, 0, true}};
	int appended = 0;
	expect(levelBrushAssembleAppend(&collected, {{0, 0, 0, true}, {1, 0, 0, true}, {1, 0, 0, true}}, &appended) && appended == 1 && collected.size() == 2,
		"append without duplicates");
	QVector<LevelMapVec3> full(kLevelBrushPointsMaximum);
	for (int i = 0; i < full.size(); ++i) {
		full[i] = {double(i % 64), double(i / 64), 0, true};
	}
	expect(!levelBrushAssembleAppend(&full, {{900, 0, 0, true}}) && full.size() == kLevelBrushPointsMaximum, "append stops at the maximum");

	// The workflow: a rectangle on the cube's top face, extruded 32 units
	// up, becomes one brush in the current material as one exact undo step.
	{
		LevelMapDocument document;
		expect(loadLevelMapBytes({QStringLiteral("assemble.map"), {}, QStringLiteral("idtech2")}, fixture(), &document, &error), "fixture", error);
		const QByteArray before = bytes(document);
		const auto depth = document.undoStack.size();
		auto points = levelBrushAssembleRectangle(top.plane, {5, 7, 64, true}, {40, 30, 64, true}, 16);
		LevelBrushAssembleOutline face;
		expect(levelBrushAssembleOutline(points, &face, &error) && face.shape == LevelBrushAssembleShape::Polygon, "face drawn", error);
		double rise = 0;
		LevelMapVec3 anchor;
		expect(levelBrushAssembleRayPolygon(ray({20, 20, 300, true}, {0, 0, -1, true}), face, &anchor), "Shift-drag starts on the face");
		const LevelMapVec3 normal {face.plane.normalX, face.plane.normalY, face.plane.normalZ, true};
		expect(levelBrushAssembleExtrusionDistance(ray({-300, 20, 93, true}, {1, 0, 0, true}), anchor, normal, 16, &rise)
				&& std::abs(rise) == 32,
			"Shift-drag to 32 units", QString::number(rise));
		expect(levelBrushAssembleAppend(&points,
				   levelBrushPointsExtruded(face.corners, {normal.x * rise, normal.y * rise, normal.z * rise, true}).mid(face.corners.size())),
			"extruded corners join the points");
		LevelBrushPointsReport report;
		expect(addLevelMapBrushFromPoints(&document, {points, QStringLiteral("studio/assembled")}, &report, &error) && report.corners == 8
				&& report.faces == 6 && document.undoStack.size() == depth + 1 && document.selection.size() == 1
				&& document.selection.first().objectId == report.brushId,
			"Create Brush adds one selected brush in one step", error);
		const auto added = std::find_if(document.brushes.cbegin(), document.brushes.cend(), [&](const LevelMapBrush& b) { return b.id == report.brushId; });
		expect(added != document.brushes.cend() && added->mins.x == 0 && added->maxs.x == 48 && added->mins.y == 0 && added->maxs.y == 32
				&& added->mins.z == 64 && added->maxs.z == 96
				&& std::all_of(added->faces.cbegin(), added->faces.cend(), [](const LevelMapBrushFace& f) { return f.textureName == QStringLiteral("studio/assembled"); }),
			"the brush sits on the face in the current material");
		expect(undoLevelMapEdit(&document, &error) && bytes(document) == before, "exact undo", error);
	}

	// The CLI: map add-brush --points --extrude.
	if (argc > 1) {
		const QString binary = QString::fromLocal8Bit(argv[1]);
		QTemporaryDir temporary;
		if (!expect(temporary.isValid(), "temporary folder")) {
			return 1;
		}
		const QString input = QDir(temporary.path()).filePath(QStringLiteral("input.map"));
		const QString output = QDir(temporary.path()).filePath(QStringLiteral("output.map"));
		QFile file(input);
		if (!expect(file.open(QIODevice::WriteOnly) && file.write(fixture()) == fixture().size(), "CLI fixture")) {
			return 1;
		}
		file.close();
		const QStringList base {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-brush"), input, QStringLiteral("--points"),
			QStringLiteral("0,0,64 48,0,64 48,32,64 0,32,64"), QStringLiteral("--extrude"), QStringLiteral("0,0,32"), QStringLiteral("--texture"),
			QStringLiteral("studio/assembled"), QStringLiteral("--output"), output};
		const QJsonObject result = runCli(binary, base + QStringList {QStringLiteral("--json")}, 0);
		const QJsonObject points = result.value(QStringLiteral("points")).toObject();
		expect(result.value(QStringLiteral("faceCount")).toInt() == 6 && points.value(QStringLiteral("inputPoints")).toInt() == 8
				&& points.value(QStringLiteral("corners")).toInt() == 8,
			"structured --extrude result", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
		LevelMapDocument direct;
		expect(loadLevelMap({input, {}, QStringLiteral("idtech2")}, &direct, &error), "reload CLI input", error);
		const QVector<LevelMapVec3> flat {{0, 0, 64, true}, {48, 0, 64, true}, {48, 32, 64, true}, {0, 32, 64, true}};
		QFile written(output);
		expect(addLevelMapBrushFromPoints(&direct, {levelBrushPointsExtruded(flat, {0, 0, 32, true}), QStringLiteral("studio/assembled")}, nullptr, &error)
				&& written.open(QIODevice::ReadOnly) && written.readAll() == bytes(direct),
			"the CLI writes what the core service builds", error);
		written.close();
		const auto variant = [&](const QStringList& extra) {
			return QStringList {QStringLiteral("--cli"), QStringLiteral("map"), QStringLiteral("add-brush"), input, QStringLiteral("--texture"),
					   QStringLiteral("studio/assembled"), QStringLiteral("--output"), output, QStringLiteral("--overwrite"), QStringLiteral("--json")}
				+ extra;
		};
		runCli(binary, variant({QStringLiteral("--points"), QStringLiteral("0,0,64 48,0,64 48,32,64 0,32,64")}), 2);
		runCli(binary, variant({QStringLiteral("--points"), QStringLiteral("0,0,64 48,0,64 48,32,64 0,32,64"), QStringLiteral("--extrude"), QStringLiteral("0,0")}), 2);
		runCli(binary, variant({QStringLiteral("--points"), QStringLiteral("0,0,64 48,0,64 48,32,64 0,32,64"), QStringLiteral("--extrude"), QStringLiteral("16,0,0")}), 2);
		runCli(binary, variant({QStringLiteral("--mins"), QStringLiteral("0,0,0"), QStringLiteral("--maxs"), QStringLiteral("8,8,8"), QStringLiteral("--extrude"),
						   QStringLiteral("0,0,8")}),
			2);
	}

	std::cout << (ok ? "level brush assemble smoke passed" : "level brush assemble smoke FAILED") << std::endl;
	return ok ? 0 : 1;
}
