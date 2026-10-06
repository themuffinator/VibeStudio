#include "core/level_map.h"
#include "core/map_geometry.h"
#include "core/map_preview_mesh.h"

#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

LevelMapVec3 vec(double x, double y, double z)
{
	LevelMapVec3 value;
	value.x = x;
	value.y = y;
	value.z = z;
	value.valid = true;
	return value;
}

LevelMapVec3 cross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return vec((a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x));
}

LevelMapVec3 scaled(const LevelMapVec3& a, double factor)
{
	return vec(a.x * factor, a.y * factor, a.z * factor);
}

LevelMapVec3 added(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return vec(a.x + b.x, a.y + b.y, a.z + b.z);
}

LevelMapVec3 normalized(const LevelMapVec3& a)
{
	const double length = std::sqrt((a.x * a.x) + (a.y * a.y) + (a.z * a.z));
	if (length <= 0.0) {
		return vec(0.0, 0.0, 0.0);
	}
	return scaled(a, 1.0 / length);
}

bool nearly(double a, double b, double epsilon = 1.0e-3)
{
	return std::abs(a - b) <= epsilon;
}

// Builds a face whose plane has the requested outward normal and passes through
// `origin`, using the idTech `normal = cross(a - b, c - b)` convention.
LevelMapBrushFace faceFromPlane(const LevelMapVec3& normalIn, const LevelMapVec3& origin, const char* texture)
{
	const LevelMapVec3 normal = normalized(normalIn);
	const LevelMapVec3 seed = std::abs(normal.z) > 0.9 ? vec(1.0, 0.0, 0.0) : vec(0.0, 0.0, 1.0);
	const LevelMapVec3 u = normalized(cross(seed, normal));
	const LevelMapVec3 v = cross(normal, u);

	LevelMapBrushFace face;
	face.p1 = origin;
	face.p0 = added(origin, scaled(u, 8.0));
	face.p2 = added(origin, scaled(v, 8.0));
	face.textureName = QString::fromLatin1(texture);
	return face;
}

LevelMapBrushFace boxFace(double nx, double ny, double nz, double px, double py, double pz)
{
	return faceFromPlane(vec(nx, ny, nz), vec(px, py, pz), "e1u1/metal1_1");
}

QVector<LevelMapBrushFace> boxFaces(double minX, double minY, double minZ, double maxX, double maxY, double maxZ)
{
	QVector<LevelMapBrushFace> faces;
	faces.append(boxFace(-1.0, 0.0, 0.0, minX, minY, minZ));
	faces.append(boxFace(0.0, -1.0, 0.0, minX, minY, minZ));
	faces.append(boxFace(0.0, 0.0, -1.0, minX, minY, minZ));
	faces.append(boxFace(1.0, 0.0, 0.0, maxX, maxY, maxZ));
	faces.append(boxFace(0.0, 1.0, 0.0, maxX, maxY, maxZ));
	faces.append(boxFace(0.0, 0.0, 1.0, maxX, maxY, maxZ));
	return faces;
}

int distinctVertexCount(const MapBrushGeometry& geometry)
{
	QVector<LevelMapVec3> unique;
	for (const MapFacePolygon& face : geometry.faces) {
		for (const LevelMapVec3& point : face.points) {
			bool found = false;
			for (const LevelMapVec3& existing : unique) {
				if (nearly(existing.x, point.x, 0.05) && nearly(existing.y, point.y, 0.05) && nearly(existing.z, point.z, 0.05)) {
					found = true;
					break;
				}
			}
			if (!found) {
				unique.append(point);
			}
		}
	}
	return static_cast<int>(unique.size());
}

bool warningsContain(const QStringList& warnings, const char* needle)
{
	for (const QString& warning : warnings) {
		if (warning.contains(QString::fromLatin1(needle), Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

int validFaceCount(const MapBrushGeometry& geometry)
{
	int count = 0;
	for (const MapFacePolygon& face : geometry.faces) {
		if (face.isValid()) {
			++count;
		}
	}
	return count;
}

bool runPlaneSmoke()
{
	bool ok = true;
	// normal = cross(a - b, c - b): (1,0,0) x (0,1,0) = (0,0,1).
	const MapPlane top = planeFromPoints(vec(1.0, 0.0, 64.0), vec(0.0, 0.0, 64.0), vec(0.0, 1.0, 64.0));
	ok &= expect(top.valid, "Plane from three non-collinear points should be valid.");
	ok &= expect(nearly(top.normalX, 0.0) && nearly(top.normalY, 0.0) && nearly(top.normalZ, 1.0), "Plane normal should follow the idTech cross convention.");
	ok &= expect(nearly(top.distance, 64.0), "Plane distance should be dot(a, normal).");
	ok &= expect(nearly(planeDistanceToPoint(top, vec(0.0, 0.0, 96.0)), 32.0), "Point in front of the plane should measure positive.");
	ok &= expect(nearly(planeDistanceToPoint(top, vec(0.0, 0.0, 0.0)), -64.0), "Point behind the plane should measure negative.");

	const MapPlane degenerate = planeFromPoints(vec(0.0, 0.0, 0.0), vec(8.0, 0.0, 0.0), vec(16.0, 0.0, 0.0));
	ok &= expect(!degenerate.valid, "Collinear points should not produce a plane.");
	return ok;
}

bool runCubeSmoke()
{
	bool ok = true;
	const MapBrushGeometry cube = solveBrushGeometry(boxFaces(0.0, 0.0, 0.0, 64.0, 64.0, 64.0), 7, 0);
	ok &= expect(cube.solved, "Unit cube should solve.");
	ok &= expect(cube.brushId == 7 && cube.entityId == 0, "Brush geometry should keep its brush and entity id.");
	ok &= expect(cube.warnings.isEmpty(), "Unit cube should not warn.");
	ok &= expect(validFaceCount(cube) == 6, "Unit cube should produce six face polygons.");
	ok &= expect(distinctVertexCount(cube) == 8, "Unit cube should produce eight distinct vertices.");
	for (const MapFacePolygon& face : cube.faces) {
		ok &= expect(face.points.size() == 4, "Each cube face should be a quad.");
		ok &= expect(!face.textureName.isEmpty(), "Face polygons should carry the source texture name.");
	}
	ok &= expect(nearly(cube.mins.x, 0.0) && nearly(cube.mins.y, 0.0) && nearly(cube.mins.z, 0.0), "Cube mins should be exact.");
	ok &= expect(nearly(cube.maxs.x, 64.0) && nearly(cube.maxs.y, 64.0) && nearly(cube.maxs.z, 64.0), "Cube maxs should be exact.");

	const QRectF footprint = cube.footprint();
	ok &= expect(nearly(footprint.left(), 0.0) && nearly(footprint.top(), 0.0) && nearly(footprint.width(), 64.0) && nearly(footprint.height(), 64.0), "Cube footprint should be the 64x64 square.");

	const QVector<QPolygonF> polygons = cube.footprintPolygons();
	ok &= expect(!polygons.isEmpty(), "Cube should produce at least one footprint polygon.");
	if (!polygons.isEmpty()) {
		ok &= expect(polygons.first().size() == 4, "Cube footprint polygon should be a quad.");
		const QRectF bounds = polygons.first().boundingRect();
		ok &= expect(nearly(bounds.width(), 64.0) && nearly(bounds.height(), 64.0), "Cube footprint polygon should span the brush.");
	}
	return ok;
}

bool runSlantedBrushSmoke()
{
	// A box rotated 45 degrees about Z. Every plane-defining point stays within
	// ~41 units of the origin, but the real brush reaches 64 units on X and Y.
	bool ok = true;
	const double diagonal = 64.0;
	QVector<LevelMapBrushFace> faces;
	faces.append(faceFromPlane(vec(1.0, 1.0, 0.0), vec(32.0, 32.0, 0.0), "slant0"));
	faces.append(faceFromPlane(vec(-1.0, 1.0, 0.0), vec(-32.0, 32.0, 0.0), "slant1"));
	faces.append(faceFromPlane(vec(-1.0, -1.0, 0.0), vec(-32.0, -32.0, 0.0), "slant2"));
	faces.append(faceFromPlane(vec(1.0, -1.0, 0.0), vec(32.0, -32.0, 0.0), "slant3"));
	faces.append(faceFromPlane(vec(0.0, 0.0, 1.0), vec(0.0, 0.0, 32.0), "slant4"));
	faces.append(faceFromPlane(vec(0.0, 0.0, -1.0), vec(0.0, 0.0, -32.0), "slant5"));

	double naiveMax = 0.0;
	for (const LevelMapBrushFace& face : faces) {
		const LevelMapVec3 points[3] = {face.p0, face.p1, face.p2};
		for (const LevelMapVec3& point : points) {
			naiveMax = std::max(naiveMax, std::max(std::abs(point.x), std::abs(point.y)));
		}
	}

	const MapBrushGeometry brush = solveBrushGeometry(faces, 11, 0);
	ok &= expect(brush.solved, "Rotated brush should solve.");
	ok &= expect(validFaceCount(brush) == 6, "Rotated brush should produce six face polygons.");
	ok &= expect(distinctVertexCount(brush) == 8, "Rotated brush should produce eight distinct vertices.");
	ok &= expect(nearly(brush.maxs.x, diagonal, 0.01) && nearly(brush.mins.x, -diagonal, 0.01), "Rotated brush X bounds should reach the diagonal.");
	ok &= expect(nearly(brush.maxs.y, diagonal, 0.01) && nearly(brush.mins.y, -diagonal, 0.01), "Rotated brush Y bounds should reach the diagonal.");
	ok &= expect(nearly(brush.maxs.z, 32.0) && nearly(brush.mins.z, -32.0), "Rotated brush Z bounds should match the caps.");
	ok &= expect(naiveMax + 1.0 < brush.maxs.x, "Plane-point bounding box must be provably smaller than the solved bounds.");

	const QVector<QPolygonF> polygons = brush.footprintPolygons();
	ok &= expect(polygons.size() == 1 && polygons.first().size() == 4, "Rotated brush footprint should be one rotated quad.");
	return ok;
}

bool runWedgeSmoke()
{
	bool ok = true;
	QVector<LevelMapBrushFace> faces;
	faces.append(faceFromPlane(vec(-1.0, 0.0, 0.0), vec(0.0, 0.0, 0.0), "wedge0"));
	faces.append(faceFromPlane(vec(0.0, -1.0, 0.0), vec(0.0, 0.0, 0.0), "wedge1"));
	faces.append(faceFromPlane(vec(0.0, 0.0, -1.0), vec(0.0, 0.0, 0.0), "wedge2"));
	faces.append(faceFromPlane(vec(0.0, 0.0, 1.0), vec(0.0, 0.0, 64.0), "wedge3"));
	faces.append(faceFromPlane(vec(1.0, 1.0, 0.0), vec(64.0, 0.0, 0.0), "wedge4"));

	const MapBrushGeometry wedge = solveBrushGeometry(faces, 3, 0);
	ok &= expect(wedge.solved, "Five-plane wedge should solve.");
	ok &= expect(validFaceCount(wedge) == 5, "Wedge should produce five face polygons.");
	ok &= expect(distinctVertexCount(wedge) == 6, "Wedge should produce six distinct vertices.");
	ok &= expect(nearly(wedge.mins.x, 0.0) && nearly(wedge.mins.y, 0.0) && nearly(wedge.mins.z, 0.0), "Wedge mins should be at the origin.");
	ok &= expect(nearly(wedge.maxs.x, 64.0) && nearly(wedge.maxs.y, 64.0) && nearly(wedge.maxs.z, 64.0), "Wedge maxs should be at the far corner.");
	return ok;
}

bool runDegenerateBrushSmoke()
{
	bool ok = true;

	QVector<LevelMapBrushFace> threePlanes = boxFaces(0.0, 0.0, 0.0, 64.0, 64.0, 64.0);
	threePlanes.resize(3);
	const MapBrushGeometry sparse = solveBrushGeometry(threePlanes, 1, 0);
	ok &= expect(!sparse.solved, "A three-plane brush cannot be solved.");
	ok &= expect(warningsContain(sparse.warnings, "fewer than four planes"), "A three-plane brush should warn about the plane count.");
	ok &= expect(sparse.faces.size() == 3, "Face entries should stay aligned with the source brush.");
	ok &= expect(validFaceCount(sparse) == 0, "A three-plane brush should produce no polygons.");

	QVector<LevelMapBrushFace> duplicated = boxFaces(0.0, 0.0, 0.0, 64.0, 64.0, 64.0);
	duplicated.append(boxFace(0.0, 0.0, 1.0, 0.0, 0.0, 64.0));
	const MapBrushGeometry duplicate = solveBrushGeometry(duplicated, 2, 0);
	ok &= expect(warningsContain(duplicate.warnings, "duplicate planes"), "Two identical planes should be reported.");
	ok &= expect(duplicate.solved, "A duplicated plane should not stop the brush from solving.");
	ok &= expect(nearly(duplicate.maxs.z, 64.0), "A duplicated plane should not change the bounds.");

	QVector<LevelMapBrushFace> empty = boxFaces(0.0, 0.0, 0.0, 64.0, 64.0, 64.0);
	empty[3] = boxFace(1.0, 0.0, 0.0, -32.0, 0.0, 0.0);
	const MapBrushGeometry hollow = solveBrushGeometry(empty, 4, 0);
	ok &= expect(!hollow.solved, "Planes that exclude each other should not solve.");
	ok &= expect(warningsContain(hollow.warnings, "encloses no volume"), "An empty intersection should warn about the missing volume.");
	ok &= expect(validFaceCount(hollow) == 0, "An empty intersection should produce no polygons.");

	QVector<LevelMapBrushFace> collinear = boxFaces(0.0, 0.0, 0.0, 64.0, 64.0, 64.0);
	collinear[5].p0 = vec(0.0, 0.0, 64.0);
	collinear[5].p1 = vec(8.0, 0.0, 64.0);
	collinear[5].p2 = vec(16.0, 0.0, 64.0);
	const MapBrushGeometry broken = solveBrushGeometry(collinear, 5, 0);
	ok &= expect(!broken.solved, "A brush left open by a degenerate plane should not solve.");
	ok &= expect(warningsContain(broken.warnings, "degenerate plane"), "A collinear face should be reported as a degenerate plane.");
	ok &= expect(warningsContain(broken.warnings, "encloses no volume"), "An open brush should be reported as enclosing no volume.");
	ok &= expect(validFaceCount(broken) == 0, "An open brush should not leak oversized polygons.");
	return ok;
}

LevelMapDoomVertex doomVertex(int id, double x, double y)
{
	LevelMapDoomVertex vertex;
	vertex.id = id;
	vertex.x = x;
	vertex.y = y;
	return vertex;
}

LevelMapDoomLinedef doomLinedef(int id, int start, int end, int frontSidedef)
{
	LevelMapDoomLinedef linedef;
	linedef.id = id;
	linedef.startVertex = start;
	linedef.endVertex = end;
	linedef.frontSidedef = frontSidedef;
	return linedef;
}

LevelMapDoomSidedef doomSidedef(int id, int sector)
{
	LevelMapDoomSidedef sidedef;
	sidedef.id = id;
	sidedef.sector = sector;
	return sidedef;
}

LevelMapDoomSector doomSector(int id)
{
	LevelMapDoomSector sector;
	sector.id = id;
	sector.floorHeight = 0;
	sector.ceilingHeight = 128;
	return sector;
}

bool runDoomOutlineSmoke()
{
	bool ok = true;

	LevelMapDocument square;
	square.format = LevelMapFormat::DoomWad;
	square.doomVertices = {doomVertex(0, 0.0, 0.0), doomVertex(1, 64.0, 0.0), doomVertex(2, 64.0, 64.0), doomVertex(3, 0.0, 64.0)};
	square.doomSidedefs = {doomSidedef(0, 0), doomSidedef(1, 0), doomSidedef(2, 0), doomSidedef(3, 0)};
	square.doomSectors = {doomSector(0)};
	square.doomLinedefs = {doomLinedef(0, 0, 1, 0), doomLinedef(1, 1, 2, 1), doomLinedef(2, 2, 3, 2), doomLinedef(3, 3, 0, 3)};

	QVector<DoomSectorOutline> outlines = buildDoomSectorOutlines(square);
	ok &= expect(outlines.size() == 1, "One sector should produce one outline.");
	if (!outlines.isEmpty()) {
		ok &= expect(outlines.first().loops.size() == 1, "A square sector should trace exactly one loop.");
		ok &= expect(outlines.first().openEdgeCount == 0, "A closed square sector should have no open edges.");
		if (!outlines.first().loops.isEmpty()) {
			ok &= expect(outlines.first().loops.first().size() == 4, "A square sector loop should have four points.");
		}
		const QRectF bounds = outlines.first().bounds;
		ok &= expect(nearly(bounds.left(), 0.0) && nearly(bounds.top(), 0.0) && nearly(bounds.width(), 64.0) && nearly(bounds.height(), 64.0), "Sector bounds should cover the square.");
	}

	LevelMapDocument broken = square;
	broken.doomLinedefs.removeLast();
	outlines = buildDoomSectorOutlines(broken);
	ok &= expect(outlines.size() == 1, "A sector with a missing linedef should still be reported.");
	if (!outlines.isEmpty()) {
		ok &= expect(outlines.first().loops.isEmpty(), "An unclosed sector should not emit a loop.");
		ok &= expect(outlines.first().openEdgeCount == 3, "An unclosed sector should count its three open edges.");
		ok &= expect(!outlines.first().bounds.isNull(), "An unclosed sector should still report bounds.");
	}

	LevelMapDocument donut;
	donut.format = LevelMapFormat::DoomWad;
	donut.doomVertices = {doomVertex(0, 0.0, 0.0), doomVertex(1, 128.0, 0.0), doomVertex(2, 128.0, 128.0), doomVertex(3, 0.0, 128.0),
		doomVertex(4, 32.0, 32.0), doomVertex(5, 32.0, 96.0), doomVertex(6, 96.0, 96.0), doomVertex(7, 96.0, 32.0)};
	donut.doomSectors = {doomSector(0)};
	for (int index = 0; index < 8; ++index) {
		donut.doomSidedefs.append(doomSidedef(index, 0));
	}
	donut.doomLinedefs = {doomLinedef(0, 0, 1, 0), doomLinedef(1, 1, 2, 1), doomLinedef(2, 2, 3, 2), doomLinedef(3, 3, 0, 3),
		doomLinedef(4, 4, 5, 4), doomLinedef(5, 5, 6, 5), doomLinedef(6, 6, 7, 6), doomLinedef(7, 7, 4, 7)};
	outlines = buildDoomSectorOutlines(donut);
	ok &= expect(outlines.size() == 1, "The donut document has a single sector.");
	if (!outlines.isEmpty()) {
		ok &= expect(outlines.first().loops.size() == 2, "A donut sector should trace two loops.");
		ok &= expect(outlines.first().openEdgeCount == 0, "A donut sector should have no open edges.");
	}

	// A two-sided linedef whose back sidedef faces the same sector contributes a
	// second, reversed edge at the same vertices. A greedy walk takes that
	// reversed edge at the junction and strands the whole loop, so the tracer has
	// to pick the tightest turn instead.
	LevelMapDocument selfReferencing = square;
	selfReferencing.doomSidedefs.append(doomSidedef(4, 0));
	selfReferencing.doomLinedefs[2].backSidedef = 4;
	outlines = buildDoomSectorOutlines(selfReferencing);
	ok &= expect(outlines.size() == 1, "A self-referencing two-sided linedef should still produce one outline.");
	if (!outlines.isEmpty()) {
		ok &= expect(outlines.first().loops.size() == 1, "A square with a self-referencing two-sided linedef should still close one loop.");
		ok &= expect(outlines.first().openEdgeCount == 1, "The reversed edge of a self-referencing linedef should be reported as open.");
		if (!outlines.first().loops.isEmpty()) {
			ok &= expect(outlines.first().loops.first().size() == 4, "The closed loop should still have four points.");
		}
	}

	// Two sectors that share a wall: each side of the shared linedef belongs to a
	// different sector, so both must close.
	LevelMapDocument sharedWall;
	sharedWall.format = LevelMapFormat::DoomWad;
	sharedWall.doomVertices = {doomVertex(0, 0.0, 0.0), doomVertex(1, 64.0, 0.0), doomVertex(2, 64.0, 64.0),
		doomVertex(3, 0.0, 64.0), doomVertex(4, 128.0, 0.0), doomVertex(5, 128.0, 64.0)};
	sharedWall.doomSectors = {doomSector(0), doomSector(1)};
	sharedWall.doomSidedefs = {doomSidedef(0, 0), doomSidedef(1, 0), doomSidedef(2, 0), doomSidedef(3, 0),
		doomSidedef(4, 1), doomSidedef(5, 1), doomSidedef(6, 1), doomSidedef(7, 1)};
	sharedWall.doomLinedefs = {doomLinedef(0, 0, 1, 0), doomLinedef(1, 1, 2, 1), doomLinedef(2, 2, 3, 2), doomLinedef(3, 3, 0, 3),
		doomLinedef(4, 1, 4, 4), doomLinedef(5, 4, 5, 5), doomLinedef(6, 5, 2, 6)};
	sharedWall.doomLinedefs[1].backSidedef = 7;
	outlines = buildDoomSectorOutlines(sharedWall);
	ok &= expect(outlines.size() == 2, "Two sectors should produce two outlines.");
	if (outlines.size() == 2) {
		ok &= expect(outlines.at(0).loops.size() == 1, "The left sector should close.");
		ok &= expect(outlines.at(1).loops.size() == 1, "The right sector sharing a wall should close.");
		ok &= expect(outlines.at(1).openEdgeCount == 0, "The shared-wall sector should have no open edges.");
	}

	// Real Doom data winds a sector's boundary with the interior on the RIGHT of
	// each edge (front sidedef start -> end), so an outer boundary runs clockwise
	// and the boundary of a hole inside the same sector runs counter-clockwise.
	// Both loops of a properly wound ring must still close.
	LevelMapDocument ring;
	ring.format = LevelMapFormat::DoomWad;
	ring.doomVertices = {doomVertex(0, 0.0, 0.0), doomVertex(1, 128.0, 0.0), doomVertex(2, 128.0, 128.0), doomVertex(3, 0.0, 128.0),
		doomVertex(4, 32.0, 32.0), doomVertex(5, 32.0, 96.0), doomVertex(6, 96.0, 96.0), doomVertex(7, 96.0, 32.0)};
	ring.doomSectors = {doomSector(0)};
	for (int index = 0; index < 8; ++index) {
		ring.doomSidedefs.append(doomSidedef(index, 0));
	}
	// Outer clockwise, inner counter-clockwise; the sector lies right of both.
	ring.doomLinedefs = {doomLinedef(0, 0, 3, 0), doomLinedef(1, 3, 2, 1), doomLinedef(2, 2, 1, 2), doomLinedef(3, 1, 0, 3),
		doomLinedef(4, 4, 7, 4), doomLinedef(5, 7, 6, 5), doomLinedef(6, 6, 5, 6), doomLinedef(7, 5, 4, 7)};
	outlines = buildDoomSectorOutlines(ring);
	ok &= expect(outlines.size() == 1, "The Doom-wound ring has a single sector.");
	if (!outlines.isEmpty()) {
		ok &= expect(outlines.first().loops.size() == 2, "A ring wound the Doom way should trace two loops.");
		ok &= expect(outlines.first().openEdgeCount == 0, "A ring wound the Doom way should have no open edges.");
	}

	// Two squares of one sector meeting at a single vertex, both wound the Doom
	// way. At the shared vertex the walk has two continuations and only the
	// tightest counter-clockwise turn from the reverse direction stays inside the
	// square it arrived in. Turning the other way merges both squares into one
	// self-touching eight-edge loop, so this fixture pins the turn direction.
	LevelMapDocument bowtie;
	bowtie.format = LevelMapFormat::DoomWad;
	bowtie.doomVertices = {doomVertex(0, 0.0, 0.0), doomVertex(1, 0.0, 16.0), doomVertex(2, 16.0, 16.0), doomVertex(3, 16.0, 0.0),
		doomVertex(4, 0.0, -16.0), doomVertex(5, -16.0, -16.0), doomVertex(6, -16.0, 0.0)};
	bowtie.doomSectors = {doomSector(0)};
	for (int index = 0; index < 8; ++index) {
		bowtie.doomSidedefs.append(doomSidedef(index, 0));
	}
	// Seeded away from the shared vertex so the walk has to reach it mid-loop.
	bowtie.doomLinedefs = {doomLinedef(0, 1, 2, 0), doomLinedef(1, 2, 3, 1), doomLinedef(2, 3, 0, 2), doomLinedef(3, 0, 1, 3),
		doomLinedef(4, 4, 5, 4), doomLinedef(5, 5, 6, 5), doomLinedef(6, 6, 0, 6), doomLinedef(7, 0, 4, 7)};
	outlines = buildDoomSectorOutlines(bowtie);
	ok &= expect(outlines.size() == 1, "The bowtie document has a single sector.");
	if (!outlines.isEmpty()) {
		ok &= expect(outlines.first().loops.size() == 2, "Two squares touching at a vertex should trace two loops, not one merged loop.");
		ok &= expect(outlines.first().openEdgeCount == 0, "A touching-island sector should have no open edges.");
		bool allQuads = true;
		for (const QPolygonF& loop : outlines.first().loops) {
			allQuads = allQuads && loop.size() == 4;
		}
		ok &= expect(allQuads, "Each square of a touching-island sector should keep four points.");
	}

	LevelMapDocument corrupt = square;
	corrupt.doomLinedefs.append(doomLinedef(4, 99, 100, 42));
	corrupt.doomLinedefs.append(doomLinedef(5, 0, 0, 0));
	outlines = buildDoomSectorOutlines(corrupt);
	ok &= expect(outlines.size() == 1, "Out-of-range indices must not add sectors.");
	if (!outlines.isEmpty()) {
		ok &= expect(outlines.first().loops.size() == 1, "Out-of-range indices must not break loop tracing.");
		ok &= expect(outlines.first().openEdgeCount == 1, "A zero-length edge should be counted as open.");
	}
	return ok;
}

bool runPatchSmoke()
{
	bool ok = true;
	LevelMapPatch patch;
	patch.id = 0;
	patch.width = 3;
	patch.height = 3;
	patch.textureName = QStringLiteral("textures/base_wall/patch");
	patch.controlPoints = {
		vec(0.0, 0.0, 0.0), vec(32.0, 0.0, 0.0), vec(64.0, 0.0, 0.0),
		vec(0.0, 32.0, 0.0), vec(32.0, 32.0, 64.0), vec(64.0, 32.0, 0.0),
		vec(0.0, 64.0, 0.0), vec(32.0, 64.0, 0.0), vec(64.0, 64.0, 0.0),
	};

	const QVector<QVector<LevelMapVec3>> mesh = tessellatePatchMesh(patch, 4);
	ok &= expect(mesh.size() == 5, "A 3x3 patch at four subdivisions should produce five rows.");
	if (mesh.size() == 5) {
		ok &= expect(mesh.first().size() == 5, "A 3x3 patch at four subdivisions should produce five columns.");
		const LevelMapVec3 topLeft = mesh.at(0).at(0);
		const LevelMapVec3 topRight = mesh.at(0).at(4);
		const LevelMapVec3 bottomLeft = mesh.at(4).at(0);
		const LevelMapVec3 bottomRight = mesh.at(4).at(4);
		ok &= expect(nearly(topLeft.x, 0.0) && nearly(topLeft.y, 0.0) && nearly(topLeft.z, 0.0), "Patch corner should match the first control point.");
		ok &= expect(nearly(topRight.x, 64.0) && nearly(topRight.y, 0.0), "Patch corner should match the last control point of the first row.");
		ok &= expect(nearly(bottomLeft.x, 0.0) && nearly(bottomLeft.y, 64.0), "Patch corner should match the first control point of the last row.");
		ok &= expect(nearly(bottomRight.x, 64.0) && nearly(bottomRight.y, 64.0), "Patch corner should match the last control point.");
		const LevelMapVec3 centre = mesh.at(2).at(2);
		ok &= expect(nearly(centre.x, 32.0) && nearly(centre.y, 32.0) && nearly(centre.z, 16.0), "Patch centre should follow the quadratic Bezier basis.");
	}

	const QVector<QVector<LevelMapVec3>> single = tessellatePatchMesh(patch, 1);
	ok &= expect(single.size() == 2 && single.first().size() == 2, "One subdivision should return the sub-patch corners.");

	// A square grid hides a row/column mix-up, because transposing it yields the
	// same surface. A 5 x 3 grid does not: control points are `height` rows of
	// `width` columns, normalised from the file's width-major order by
	// parsePatchBody, so row and column are not interchangeable here.
	LevelMapPatch wide;
	wide.id = 1;
	wide.width = 5;
	wide.height = 3;
	wide.textureName = QStringLiteral("textures/base_wall/wide");
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 5; ++column) {
			wide.controlPoints.push_back(vec(column * 32.0, row * 32.0, row * 8.0));
		}
	}

	const QVector<QVector<LevelMapVec3>> wideMesh = tessellatePatchMesh(wide, 4);
	ok &= expect(wideMesh.size() == 5, "A 5x3 patch at four subdivisions should produce five rows.");
	if (wideMesh.size() == 5) {
		ok &= expect(wideMesh.first().size() == 9, "A 5x3 patch at four subdivisions should produce nine columns.");
		const LevelMapVec3 topLeft = wideMesh.at(0).at(0);
		const LevelMapVec3 topRight = wideMesh.at(0).at(8);
		const LevelMapVec3 bottomLeft = wideMesh.at(4).at(0);
		const LevelMapVec3 bottomRight = wideMesh.at(4).at(8);
		ok &= expect(nearly(topLeft.x, 0.0) && nearly(topLeft.y, 0.0) && nearly(topLeft.z, 0.0), "The 5x3 mesh should start at the first control point.");
		ok &= expect(nearly(topRight.x, 128.0) && nearly(topRight.y, 0.0) && nearly(topRight.z, 0.0), "The 5x3 mesh should reach the last column of the first row.");
		ok &= expect(nearly(bottomLeft.x, 0.0) && nearly(bottomLeft.y, 64.0) && nearly(bottomLeft.z, 16.0), "The 5x3 mesh should reach the first column of the last row.");
		ok &= expect(nearly(bottomRight.x, 128.0) && nearly(bottomRight.y, 64.0) && nearly(bottomRight.z, 16.0), "The 5x3 mesh should reach the last control point.");
	}

	LevelMapPatch malformed = patch;
	malformed.width = 4;
	ok &= expect(tessellatePatchMesh(malformed, 4).isEmpty(), "An even control grid width should be rejected.");
	malformed = patch;
	malformed.controlPoints.removeLast();
	ok &= expect(tessellatePatchMesh(malformed, 4).isEmpty(), "A short control point list should be rejected.");
	ok &= expect(tessellatePatchMesh(LevelMapPatch(), 4).isEmpty(), "An empty patch should be rejected.");
	return ok;
}

bool runSummarySmoke()
{
	bool ok = true;
	LevelMapDocument document;
	document.format = LevelMapFormat::QuakeMap;

	LevelMapBrush good;
	good.id = 0;
	good.entityId = 0;
	good.faces = boxFaces(0.0, 0.0, 0.0, 64.0, 64.0, 64.0);
	good.faceCount = static_cast<int>(good.faces.size());

	LevelMapBrush far;
	far.id = 1;
	far.entityId = 0;
	far.faces = boxFaces(-128.0, -64.0, -32.0, -64.0, 0.0, 0.0);
	far.faceCount = static_cast<int>(far.faces.size());

	LevelMapBrush bad;
	bad.id = 2;
	bad.entityId = 0;
	bad.faces = boxFaces(0.0, 0.0, 0.0, 64.0, 64.0, 64.0);
	bad.faces.resize(3);
	bad.faceCount = 3;

	document.brushes = {good, far, bad};

	const QVector<MapBrushGeometry> solved = buildLevelMapBrushGeometry(document);
	ok &= expect(solved.size() == 3, "Every brush should be solved in order.");
	ok &= expect(solved.at(0).brushId == 0 && solved.at(1).brushId == 1 && solved.at(2).brushId == 2, "Brush ids should be preserved.");

	const MapGeometrySummary summary = summarizeLevelMapGeometry(document);
	ok &= expect(summary.brushCount == 3, "Summary should count every brush.");
	ok &= expect(summary.solvedBrushCount == 2, "Summary should count the solved brushes.");
	ok &= expect(summary.degenerateBrushCount == 1, "Summary should count the unsolved brushes.");
	ok &= expect(summary.faceCount == 12, "Summary should count the solved face polygons.");
	ok &= expect(summary.polygonPointCount == 48, "Summary should count the face polygon points.");
	ok &= expect(summary.mins.valid && nearly(summary.mins.x, -128.0) && nearly(summary.mins.y, -64.0) && nearly(summary.mins.z, -32.0), "Summary bounds should cover both solved brushes.");
	ok &= expect(summary.maxs.valid && nearly(summary.maxs.x, 64.0) && nearly(summary.maxs.y, 64.0) && nearly(summary.maxs.z, 64.0), "Summary bounds should cover the far corner.");
	ok &= expect(!summary.warnings.isEmpty(), "Summary should surface brush warnings.");

	const QStringList lines = mapGeometrySummaryLines(summary);
	ok &= expect(lines.size() >= 8, "Summary lines should describe the geometry.");
	ok &= expect(lines.first().contains(QStringLiteral("2")), "Summary lines should report the solved brush count.");
	return ok;
}

} // namespace

// The 3D preview mesh: one surface per texture, each face a fan of
// triangles, Doom walls from sector heights, and a triangle limit.
bool runPreviewMeshSmoke()
{
	bool ok = true;
	LevelMapDocument box;
	box.format = LevelMapFormat::QuakeMap;
	LevelMapBrush brush;
	brush.id = 0;
	brush.faces = boxFaces(0.0, 0.0, 0.0, 64.0, 32.0, 16.0);
	brush.faces[5].textureName = QStringLiteral("e1u1/floor1_3");
	box.brushes = {brush};
	const LevelMapPreviewMesh preview = buildLevelMapPreviewMesh(box);
	ok &= expect(preview.brushFaces == 6 && preview.triangles == 12 && preview.mesh.triangleCount == 12 && !preview.truncated,
		"A box brush should give six faces of two triangles each.");
	ok &= expect(preview.mesh.surfaces.size() == 2 && preview.mesh.frameCount == 1 && preview.mesh.geometryAvailable,
		"The box's two textures should make two surfaces with one frame.");
	bool outward = true;
	for (const ModelSurface& surface : preview.mesh.surfaces) {
		const ModelFrameGeometry& frame = surface.frames.first();
		outward = outward && frame.normals.size() == frame.positions.size();
		for (int index = 0; outward && index < frame.positions.size(); ++index) {
			// Every corner's normal points away from the box's middle.
			const ModelVec3& position = frame.positions.at(index);
			const ModelVec3& normal = frame.normals.at(index);
			outward = (position.x - 32.0f) * normal.x + (position.y - 16.0f) * normal.y + (position.z - 8.0f) * normal.z > 0.0f;
		}
	}
	ok &= expect(outward, "Every face's normal should point out of the box.");
	ok &= expect(preview.owners.size() == 12
			&& std::all_of(preview.owners.cbegin(), preview.owners.cend(),
				[](const LevelMapSelectionRef& owner) { return owner.kind == LevelMapSelectionKind::QuakeBrush && owner.objectId == 0; }),
		"Every triangle of the box should know it came from brush 0.");
	QSet<int> facesSeen;
	for (const int face : preview.ownerFaces) {
		facesSeen.insert(face);
	}
	ok &= expect(preview.ownerFaces.size() == preview.owners.size() && facesSeen.size() == 6 && !facesSeen.contains(-1),
		"Every triangle should know which of the box's six faces it lies on.");
	ok &= expect(nearly(preview.mesh.mins.x, 0.0) && nearly(preview.mesh.maxs.x, 64.0) && nearly(preview.mesh.maxs.y, 32.0)
			&& nearly(preview.mesh.maxs.z, 16.0),
		"The preview's bounds should be the box's.");

	const LevelMapPreviewMesh limited = buildLevelMapPreviewMesh(box, {5});
	ok &= expect(limited.truncated && limited.triangles <= 5, "A triangle limit should stop the mesh short and say so.");

	LevelMapDocument square;
	square.format = LevelMapFormat::DoomWad;
	square.doomVertices = {doomVertex(0, 0.0, 0.0), doomVertex(1, 64.0, 0.0), doomVertex(2, 64.0, 64.0), doomVertex(3, 0.0, 64.0)};
	square.doomSidedefs = {doomSidedef(0, 0), doomSidedef(1, 0), doomSidedef(2, 0), doomSidedef(3, 0)};
	square.doomSectors = {doomSector(0)};
	square.doomLinedefs = {doomLinedef(0, 0, 1, 0), doomLinedef(1, 1, 2, 1), doomLinedef(2, 2, 3, 2), doomLinedef(3, 3, 0, 3)};
	const LevelMapPreviewMesh room = buildLevelMapPreviewMesh(square);
	ok &= expect(room.walls == 4 && room.floors == 1 && room.ceilings == 1 && room.triangles == 12 && nearly(room.mesh.mins.z, 0.0) && nearly(room.mesh.maxs.z, 128.0),
		"A one-sector Doom room should have four walls, a floor and a ceiling.");
	return ok;
}

int main()
{
	bool ok = true;
	ok &= runPlaneSmoke();
	ok &= runCubeSmoke();
	ok &= runSlantedBrushSmoke();
	ok &= runWedgeSmoke();
	ok &= runDegenerateBrushSmoke();
	ok &= runDoomOutlineSmoke();
	ok &= runPatchSmoke();
	ok &= runSummarySmoke();
	ok &= runPreviewMeshSmoke();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
