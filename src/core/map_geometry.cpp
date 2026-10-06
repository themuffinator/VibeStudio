#include "core/map_geometry.h"
#include "core/level_patch.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio {

namespace {

// Tolerances follow the conventions used by the released idTech qbsp sources
// (see `ON_EPSILON` / `DIST_EPSILON` in ericw-tools,
// https://github.com/ericwa/ericw-tools), which VibeStudio imports under
// external/compilers/ericw-tools. They are reimplemented here, not copied.
constexpr double kOnPlaneEpsilon = 0.01;
constexpr double kNormalEpsilon = 1.0e-6;
constexpr double kDistanceEpsilon = 1.0e-4;
constexpr double kPointSnapEpsilon = 1.0e-3;
constexpr double kVertexMergeEpsilon = 0.05;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kDuplicatePlaneNormalEpsilon = 1.0e-4;
constexpr double kDuplicatePlaneDistanceEpsilon = 0.01;
constexpr double kUpwardFaceThreshold = 0.25;

// Quake III clamps map coordinates to +/-65536 world units (MAX_WORLD_COORD in
// the released Quake III / q3map2 sources), so a base polygon of 2^16 units
// around the plane origin always covers any legal brush face.
constexpr double kBaseWindingExtent = 65536.0;

LevelMapVec3 makeVec3(double x, double y, double z)
{
	LevelMapVec3 value;
	value.x = x;
	value.y = y;
	value.z = z;
	value.valid = true;
	return value;
}

LevelMapVec3 vecAdd(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return makeVec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

LevelMapVec3 vecSub(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return makeVec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

LevelMapVec3 vecScale(const LevelMapVec3& a, double scale)
{
	return makeVec3(a.x * scale, a.y * scale, a.z * scale);
}

double vecDot(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

LevelMapVec3 vecCross(const LevelMapVec3& a, const LevelMapVec3& b)
{
	return makeVec3((a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x));
}

double vecLength(const LevelMapVec3& a)
{
	return std::sqrt(vecDot(a, a));
}

bool vecIsFinite(const LevelMapVec3& a)
{
	return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

double snapToInteger(double value, double epsilon)
{
	const double rounded = std::round(value);
	return (std::abs(value - rounded) < epsilon) ? rounded : value;
}

LevelMapVec3 planeNormal(const MapPlane& plane)
{
	return makeVec3(plane.normalX, plane.normalY, plane.normalZ);
}

// Classic `BaseWindingForPlane`: build an orthonormal basis on the plane and
// emit a large quad centred on the plane origin. Reimplemented from the method
// described by the released qbsp sources (ericw-tools).
QVector<LevelMapVec3> baseWindingForPlane(const MapPlane& plane)
{
	const LevelMapVec3 normal = planeNormal(plane);
	const double ax = std::abs(normal.x);
	const double ay = std::abs(normal.y);
	const double az = std::abs(normal.z);

	LevelMapVec3 up = makeVec3(0.0, 0.0, 1.0);
	if (az >= ax && az >= ay) {
		up = makeVec3(1.0, 0.0, 0.0);
	}

	// Project the seed axis into the plane and normalise it.
	up = vecSub(up, vecScale(normal, vecDot(up, normal)));
	const double upLength = vecLength(up);
	if (!std::isfinite(upLength) || upLength < kNormalEpsilon) {
		return {};
	}
	up = vecScale(up, 1.0 / upLength);

	const LevelMapVec3 right = vecCross(up, normal);
	const LevelMapVec3 origin = vecScale(normal, plane.distance);
	const LevelMapVec3 upEdge = vecScale(up, kBaseWindingExtent);
	const LevelMapVec3 rightEdge = vecScale(right, kBaseWindingExtent);

	QVector<LevelMapVec3> winding;
	winding.reserve(4);
	winding.append(vecSub(vecAdd(origin, upEdge), rightEdge));
	winding.append(vecAdd(vecAdd(origin, upEdge), rightEdge));
	winding.append(vecAdd(vecSub(origin, upEdge), rightEdge));
	winding.append(vecSub(vecSub(origin, upEdge), rightEdge));
	return winding;
}

// Sutherland-Hodgman clip keeping the half-space behind `plane` (the brush
// interior, because map brush face normals point outward). Use numerical
// precision here, not the larger diagnostic tolerance: keeping a point 0.01
// off a shallow plane can separate adjacent face windings by several units.
QVector<LevelMapVec3> clipWinding(const QVector<LevelMapVec3>& winding, const MapPlane& plane)
{
	constexpr double clipEpsilon = 1e-7;
	if (winding.size() < 3) {
		return {};
	}
	QVector<double> distances;
	distances.reserve(winding.size());
	bool anyOutside = false;
	for (const LevelMapVec3& point : winding) {
		const double distance = planeDistanceToPoint(plane, point);
		distances.append(distance);
		if (distance > clipEpsilon) {
			anyOutside = true;
		}
	}
	if (!anyOutside) {
		return winding;
	}

	QVector<LevelMapVec3> result;
	result.reserve(winding.size() + 4);
	for (int index = 0; index < winding.size(); ++index) {
		const int nextIndex = (index + 1) % winding.size();
		const LevelMapVec3& current = winding.at(index);
		const LevelMapVec3& next = winding.at(nextIndex);
		const double currentDistance = distances.at(index);
		const double nextDistance = distances.at(nextIndex);
		const bool currentInside = currentDistance <= clipEpsilon;
		const bool nextInside = nextDistance <= clipEpsilon;
		if (currentInside) {
			result.append(current);
		}
		if (currentInside == nextInside) {
			continue;
		}
		const double denominator = currentDistance - nextDistance;
		if (std::abs(denominator) < 1e-12) {
			continue;
		}
		const double fraction = currentDistance / denominator;
		result.append(vecAdd(current, vecScale(vecSub(next, current), fraction)));
	}
	return result;
}

bool pointsAreClose(const LevelMapVec3& a, const LevelMapVec3& b, double epsilon)
{
	return std::abs(a.x - b.x) <= epsilon && std::abs(a.y - b.y) <= epsilon && std::abs(a.z - b.z) <= epsilon;
}

QVector<LevelMapVec3> tidyWinding(const QVector<LevelMapVec3>& winding, MapGeometryPrecision precision)
{
	QVector<LevelMapVec3> result;
	const bool preserve = precision == MapGeometryPrecision::PreserveCoordinates;
	const double duplicateEpsilon = preserve ? 1e-7 : kOnPlaneEpsilon;
	result.reserve(winding.size());
	for (const LevelMapVec3& point : winding) {
		if (!vecIsFinite(point)) {
			return {};
		}
		const LevelMapVec3 snapped = preserve ? point : makeVec3(snapToInteger(point.x, kPointSnapEpsilon),
			snapToInteger(point.y, kPointSnapEpsilon),
			snapToInteger(point.z, kPointSnapEpsilon));
		if (!result.isEmpty() && pointsAreClose(result.last(), snapped, duplicateEpsilon)) {
			continue;
		}
		result.append(snapped);
	}
	while (result.size() >= 2 && pointsAreClose(result.first(), result.last(), duplicateEpsilon)) {
		result.removeLast();
	}
	if (result.size() < 3) {
		return {};
	}
	return result;
}

double polygonArea(const QPolygonF& polygon)
{
	if (polygon.size() < 3) {
		return 0.0;
	}
	double area = 0.0;
	for (int index = 0; index < polygon.size(); ++index) {
		const QPointF& current = polygon.at(index);
		const QPointF& next = polygon.at((index + 1) % polygon.size());
		area += (current.x() * next.y()) - (next.x() * current.y());
	}
	return std::abs(area) * 0.5;
}

double cross2d(const QPointF& origin, const QPointF& a, const QPointF& b)
{
	return ((a.x() - origin.x()) * (b.y() - origin.y())) - ((a.y() - origin.y()) * (b.x() - origin.x()));
}

// Andrew's monotone chain convex hull, used as the footprint fallback.
QPolygonF convexHull(QVector<QPointF> points)
{
	std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) {
		if (a.x() < b.x()) {
			return true;
		}
		if (b.x() < a.x()) {
			return false;
		}
		return a.y() < b.y();
	});
	points.erase(std::unique(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) {
		return std::abs(a.x() - b.x()) <= kVertexMergeEpsilon && std::abs(a.y() - b.y()) <= kVertexMergeEpsilon;
	}), points.end());
	if (points.size() < 3) {
		return {};
	}

	QVector<QPointF> hull(2 * points.size());
	int count = 0;
	for (const QPointF& point : points) {
		while (count >= 2 && cross2d(hull.at(count - 2), hull.at(count - 1), point) <= 0.0) {
			--count;
		}
		hull[count++] = point;
	}
	const int lowerCount = count + 1;
	for (int index = static_cast<int>(points.size()) - 2; index >= 0; --index) {
		const QPointF& point = points.at(index);
		while (count >= lowerCount && cross2d(hull.at(count - 2), hull.at(count - 1), point) <= 0.0) {
			--count;
		}
		hull[count++] = point;
	}
	hull.resize(std::max(count - 1, 0));

	QPolygonF polygon;
	polygon.reserve(hull.size());
	for (const QPointF& point : hull) {
		polygon.append(point);
	}
	if (polygon.size() < 3) {
		return {};
	}
	return polygon;
}

QRectF rectFromPoints(const QVector<QPointF>& points)
{
	if (points.isEmpty()) {
		return {};
	}
	double minX = std::numeric_limits<double>::max();
	double minY = std::numeric_limits<double>::max();
	double maxX = std::numeric_limits<double>::lowest();
	double maxY = std::numeric_limits<double>::lowest();
	for (const QPointF& point : points) {
		minX = std::min(minX, point.x());
		minY = std::min(minY, point.y());
		maxX = std::max(maxX, point.x());
		maxY = std::max(maxY, point.y());
	}
	return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

QString vectorText(const LevelMapVec3& value)
{
	if (!value.valid) {
		return QCoreApplication::translate("VibeStudioMapGeometry", "unknown");
	}
	return QStringLiteral("%1, %2, %3").arg(value.x, 0, 'f', 1).arg(value.y, 0, 'f', 1).arg(value.z, 0, 'f', 1);
}

LevelMapVec3 evaluateQuadraticBezier(const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c, double t)
{
	// Quadratic Bezier basis: (1-t)^2 * a + 2t(1-t) * b + t^2 * c. Quake III
	// patchDef2/patchDef3 control meshes are grids of these, as described by the
	// Quake III shader/map documentation and the released q3map2 sources.
	const double inverse = 1.0 - t;
	const double wa = inverse * inverse;
	const double wb = 2.0 * t * inverse;
	const double wc = t * t;
	return makeVec3((a.x * wa) + (b.x * wb) + (c.x * wc),
		(a.y * wa) + (b.y * wb) + (c.y * wc),
		(a.z * wa) + (b.z * wb) + (c.z * wc));
}

} // namespace

bool MapFacePolygon::isValid() const
{
	return plane.valid && points.size() >= 3;
}

QRectF MapBrushGeometry::footprint() const
{
	QVector<QPointF> projected;
	for (const MapFacePolygon& face : faces) {
		for (const LevelMapVec3& point : face.points) {
			projected.append(QPointF(point.x, point.y));
		}
	}
	if (!projected.isEmpty()) {
		return rectFromPoints(projected);
	}
	if (mins.valid && maxs.valid) {
		return QRectF(QPointF(mins.x, mins.y), QPointF(maxs.x, maxs.y)).normalized();
	}
	return {};
}

QVector<QPolygonF> MapBrushGeometry::footprintPolygons() const
{
	QVector<QPolygonF> polygons;
	QVector<QPointF> projected;
	for (const MapFacePolygon& face : faces) {
		for (const LevelMapVec3& point : face.points) {
			projected.append(QPointF(point.x, point.y));
		}
		if (!face.isValid() || face.plane.normalZ <= kUpwardFaceThreshold) {
			continue;
		}
		// For a closed convex brush the upward-facing faces already project onto
		// the complete XY footprint, so the downward ones are redundant.
		QPolygonF polygon;
		polygon.reserve(face.points.size());
		for (const LevelMapVec3& point : face.points) {
			polygon.append(QPointF(point.x, point.y));
		}
		if (polygonArea(polygon) <= kOnPlaneEpsilon) {
			continue;
		}
		polygons.append(polygon);
	}
	if (!polygons.isEmpty()) {
		return polygons;
	}
	const QPolygonF hull = convexHull(projected);
	if (!hull.isEmpty()) {
		polygons.append(hull);
	}
	return polygons;
}

MapPlane planeFromPoints(const LevelMapVec3& a, const LevelMapVec3& b, const LevelMapVec3& c, MapGeometryPrecision precision)
{
	// idTech convention, matching `PlaneFromPoints` in the released qbsp sources
	// (ericw-tools, https://github.com/ericwa/ericw-tools, imported under
	// external/compilers/ericw-tools): normal = cross(a - b, c - b), normalised,
	// and distance = dot(a, normal). Map brush face normals therefore point away
	// from the brush interior.
	MapPlane plane;
	if (!vecIsFinite(a) || !vecIsFinite(b) || !vecIsFinite(c)) {
		return plane;
	}
	const LevelMapVec3 normal = vecCross(vecSub(a, b), vecSub(c, b));
	const double length = vecLength(normal);
	if (!std::isfinite(length) || length < kNormalEpsilon) {
		return plane;
	}

	double nx = normal.x / length;
	double ny = normal.y / length;
	double nz = normal.z / length;
	if (precision == MapGeometryPrecision::PreserveCoordinates) {
		return {nx, ny, nz, (a.x * nx) + (a.y * ny) + (a.z * nz), true};
	}
	// Axis snapping, as qbsp does, so axis-aligned brushes stay exact.
	if (std::abs(nx) < kNormalEpsilon) {
		nx = 0.0;
	}
	if (std::abs(ny) < kNormalEpsilon) {
		ny = 0.0;
	}
	if (std::abs(nz) < kNormalEpsilon) {
		nz = 0.0;
	}
	if (std::abs(std::abs(nx) - 1.0) < kNormalEpsilon) {
		nx = nx > 0.0 ? 1.0 : -1.0;
		ny = 0.0;
		nz = 0.0;
	} else if (std::abs(std::abs(ny) - 1.0) < kNormalEpsilon) {
		ny = ny > 0.0 ? 1.0 : -1.0;
		nx = 0.0;
		nz = 0.0;
	} else if (std::abs(std::abs(nz) - 1.0) < kNormalEpsilon) {
		nz = nz > 0.0 ? 1.0 : -1.0;
		nx = 0.0;
		ny = 0.0;
	}

	plane.normalX = nx;
	plane.normalY = ny;
	plane.normalZ = nz;
	plane.distance = snapToInteger((a.x * nx) + (a.y * ny) + (a.z * nz), kDistanceEpsilon);
	plane.valid = true;
	return plane;
}

double planeDistanceToPoint(const MapPlane& plane, const LevelMapVec3& point)
{
	if (!plane.valid) {
		return 0.0;
	}
	return (plane.normalX * point.x) + (plane.normalY * point.y) + (plane.normalZ * point.z) - plane.distance;
}

MapBrushGeometry solveBrushGeometry(const QVector<LevelMapBrushFace>& faces, int brushId, int entityId, MapGeometryPrecision precision,
	const std::function<bool()>& isCancelled)
{
	MapBrushGeometry geometry;
	geometry.brushId = brushId;
	geometry.entityId = entityId;
	const auto cancelled = [&] {
		if (!isCancelled || !isCancelled()) { return false; }
		geometry.cancelled = true; geometry.solved = false; geometry.faces.clear();
		geometry.mins = {}; geometry.maxs = {}; geometry.warnings.clear();
		return true;
	};
	if (cancelled()) { return geometry; }
	geometry.faces.reserve(faces.size());

	QVector<MapPlane> planes;
	planes.reserve(faces.size());
	int validPlaneCount = 0;
	for (int index = 0; index < faces.size(); ++index) {
		if ((index & 63) == 0 && cancelled()) { return geometry; }
		const LevelMapBrushFace& face = faces.at(index);
		const MapPlane plane = planeFromPoints(face.p0, face.p1, face.p2, precision);
		if (plane.valid) {
			++validPlaneCount;
		} else {
			geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Face %1 has a degenerate plane; its three points are collinear or identical.").arg(index);
		}
		planes.append(plane);

		MapFacePolygon polygon;
		polygon.faceIndex = index;
		polygon.textureName = face.textureName;
		polygon.plane = plane;
		geometry.faces.append(polygon);
	}

	bool duplicateReported = false;
	const bool preserve = precision == MapGeometryPrecision::PreserveCoordinates;
	const double duplicateNormal = preserve ? 1e-9 : kDuplicatePlaneNormalEpsilon;
	const double duplicateDistance = preserve ? 1e-7 : kDuplicatePlaneDistanceEpsilon;
	for (int i = 0; i < planes.size() && !duplicateReported; ++i) {
		if (cancelled()) { return geometry; }
		if (!planes.at(i).valid) {
			continue;
		}
		for (int j = i + 1; j < planes.size(); ++j) {
			if ((j & 63) == 0 && cancelled()) { return geometry; }
			const MapPlane& first = planes.at(i);
			const MapPlane& second = planes.at(j);
			if (!second.valid) {
				continue;
			}
			if (std::abs(first.normalX - second.normalX) > duplicateNormal
				|| std::abs(first.normalY - second.normalY) > duplicateNormal
				|| std::abs(first.normalZ - second.normalZ) > duplicateNormal) {
				continue;
			}
			if (std::abs(first.distance - second.distance) > duplicateDistance) {
				continue;
			}
			geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Faces %1 and %2 are duplicate planes.").arg(i).arg(j);
			duplicateReported = true;
			break;
		}
	}

	if (faces.size() < 4) {
		geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Brush has fewer than four planes and cannot enclose a volume.");
	}

	int solvedFaceCount = 0;
	bool unbounded = false;
	double minX = std::numeric_limits<double>::max();
	double minY = std::numeric_limits<double>::max();
	double minZ = std::numeric_limits<double>::max();
	double maxX = std::numeric_limits<double>::lowest();
	double maxY = std::numeric_limits<double>::lowest();
	double maxZ = std::numeric_limits<double>::lowest();

	if (validPlaneCount >= 4) {
		for (int index = 0; index < planes.size(); ++index) {
			if (cancelled()) { return geometry; }
			if (!planes.at(index).valid) {
				continue;
			}
			QVector<LevelMapVec3> winding = baseWindingForPlane(planes.at(index));
			for (int other = 0; other < planes.size() && !winding.isEmpty(); ++other) {
				if ((other & 31) == 0 && cancelled()) { return geometry; }
				if (other == index || !planes.at(other).valid) {
					continue;
				}
				winding = clipWinding(winding, planes.at(other));
			}
			winding = tidyWinding(winding, precision);
			if (winding.isEmpty()) {
				continue;
			}
			geometry.faces[index].points = winding;
			++solvedFaceCount;
			for (const LevelMapVec3& point : winding) {
				// A vertex still sitting on the base polygon edge means the face
				// was never clipped on that side: the planes do not close.
				if (std::abs(point.x) >= kBaseWindingExtent - 1.0
					|| std::abs(point.y) >= kBaseWindingExtent - 1.0
					|| std::abs(point.z) >= kBaseWindingExtent - 1.0) {
					unbounded = true;
				}
				minX = std::min(minX, point.x);
				minY = std::min(minY, point.y);
				minZ = std::min(minZ, point.z);
				maxX = std::max(maxX, point.x);
				maxY = std::max(maxY, point.y);
				maxZ = std::max(maxZ, point.z);
			}
		}
	}

	if (unbounded) {
		// Drop the runaway polygons so callers never paint a 2^16 unit face.
		for (MapFacePolygon& face : geometry.faces) {
			face.points.clear();
		}
		geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Brush is open on at least one side and encloses no volume.");
	} else if (solvedFaceCount >= 4) {
		const bool finite = std::isfinite(minX) && std::isfinite(minY) && std::isfinite(minZ)
			&& std::isfinite(maxX) && std::isfinite(maxY) && std::isfinite(maxZ);
		const bool nonDegenerate = finite
			&& (maxX - minX) > kOnPlaneEpsilon
			&& (maxY - minY) > kOnPlaneEpsilon
			&& (maxZ - minZ) > kOnPlaneEpsilon;
		if (!finite) {
			geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Brush bounds are not finite; the brush encloses no volume.");
		} else if (!nonDegenerate) {
			geometry.mins = makeVec3(minX, minY, minZ);
			geometry.maxs = makeVec3(maxX, maxY, maxZ);
			geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Brush is flat on at least one axis and encloses no volume.");
		} else {
			geometry.mins = makeVec3(minX, minY, minZ);
			geometry.maxs = makeVec3(maxX, maxY, maxZ);
			geometry.solved = true;
		}
	} else if (validPlaneCount >= 4) {
		geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Brush encloses no volume; only %1 of %2 planes produced a face.")
			.arg(solvedFaceCount)
			.arg(validPlaneCount);
	} else if (faces.size() >= 4) {
		geometry.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Brush encloses no volume; fewer than four usable planes remain.");
	}

	if (cancelled()) { return geometry; }
	return geometry;
}

QVector<MapBrushGeometry> buildLevelMapBrushGeometry(const LevelMapDocument& document)
{
	QVector<MapBrushGeometry> result;
	result.reserve(document.brushes.size());
	for (const LevelMapBrush& brush : document.brushes) {
		result.append(solveBrushGeometry(brush.faces, brush.id, brush.entityId));
	}
	return result;
}

QVector<DoomSectorOutline> buildDoomSectorOutlines(const LevelMapDocument& document)
{
	// Doom linedefs carry a front and an optional back sidedef, and each sidedef
	// names the sector it faces. The boundary of a sector is therefore the set of
	// linedef edges whose front sidedef belongs to it, taken start -> end, plus
	// the edges whose back sidedef belongs to it, taken end -> start. See
	// https://doomwiki.org/wiki/Linedef and https://doomwiki.org/wiki/Sidedef.
	struct SectorEdge {
		int from = -1;
		int to = -1;
	};

	QVector<DoomSectorOutline> outlines;
	if (document.doomSectors.isEmpty()) {
		return outlines;
	}

	QHash<int, QPointF> vertexById;
	for (int index = 0; index < document.doomVertices.size(); ++index) {
		const LevelMapDoomVertex& vertex = document.doomVertices.at(index);
		if (vertex.id >= 0 && !vertexById.contains(vertex.id)) {
			vertexById.insert(vertex.id, QPointF(vertex.x, vertex.y));
		}
	}
	const auto vertexPoint = [&](int index, QPointF* out) -> bool {
		if (index < 0) {
			return false;
		}
		if (index < document.doomVertices.size()) {
			const LevelMapDoomVertex& vertex = document.doomVertices.at(index);
			if (vertex.id < 0 || vertex.id == index) {
				*out = QPointF(vertex.x, vertex.y);
				return true;
			}
		}
		const auto it = vertexById.constFind(index);
		if (it != vertexById.constEnd()) {
			*out = it.value();
			return true;
		}
		if (index < document.doomVertices.size()) {
			const LevelMapDoomVertex& vertex = document.doomVertices.at(index);
			*out = QPointF(vertex.x, vertex.y);
			return true;
		}
		return false;
	};

	QHash<int, int> sectorBySidedefId;
	for (int index = 0; index < document.doomSidedefs.size(); ++index) {
		const LevelMapDoomSidedef& sidedef = document.doomSidedefs.at(index);
		if (sidedef.id >= 0 && !sectorBySidedefId.contains(sidedef.id)) {
			sectorBySidedefId.insert(sidedef.id, sidedef.sector);
		}
	}
	const auto sectorForSidedef = [&](int index) -> int {
		if (index < 0) {
			return -1;
		}
		if (index < document.doomSidedefs.size()) {
			const LevelMapDoomSidedef& sidedef = document.doomSidedefs.at(index);
			if (sidedef.id < 0 || sidedef.id == index) {
				return sidedef.sector;
			}
		}
		const auto it = sectorBySidedefId.constFind(index);
		if (it != sectorBySidedefId.constEnd()) {
			return it.value();
		}
		if (index < document.doomSidedefs.size()) {
			return document.doomSidedefs.at(index).sector;
		}
		return -1;
	};

	QHash<int, int> outlineBySectorIndex;
	outlines.reserve(document.doomSectors.size());
	for (int index = 0; index < document.doomSectors.size(); ++index) {
		const LevelMapDoomSector& sector = document.doomSectors.at(index);
		DoomSectorOutline outline;
		outline.sectorId = sector.id >= 0 ? sector.id : index;
		outlines.append(outline);
		outlineBySectorIndex.insert(index, static_cast<int>(outlines.size()) - 1);
	}

	QVector<QVector<SectorEdge>> edgesPerSector(outlines.size());
	QVector<int> invalidEdgesPerSector(outlines.size(), 0);
	for (const LevelMapDoomLinedef& linedef : document.doomLinedefs) {
		const int frontSector = sectorForSidedef(linedef.frontSidedef);
		const int backSector = sectorForSidedef(linedef.backSidedef);
		for (int pass = 0; pass < 2; ++pass) {
			const int sectorIndex = pass == 0 ? frontSector : backSector;
			const auto it = outlineBySectorIndex.constFind(sectorIndex);
			if (it == outlineBySectorIndex.constEnd()) {
				continue;
			}
			SectorEdge edge;
			edge.from = pass == 0 ? linedef.startVertex : linedef.endVertex;
			edge.to = pass == 0 ? linedef.endVertex : linedef.startVertex;
			QPointF ignored;
			if (edge.from == edge.to || !vertexPoint(edge.from, &ignored) || !vertexPoint(edge.to, &ignored)) {
				invalidEdgesPerSector[it.value()] += 1;
				continue;
			}
			edgesPerSector[it.value()].append(edge);
		}
	}

	for (int outlineIndex = 0; outlineIndex < outlines.size(); ++outlineIndex) {
		DoomSectorOutline& outline = outlines[outlineIndex];
		const QVector<SectorEdge>& edges = edgesPerSector.at(outlineIndex);
		outline.openEdgeCount = invalidEdgesPerSector.at(outlineIndex);

		QVector<QPointF> boundsPoints;
		QHash<int, QVector<int>> edgesByStart;
		for (int index = 0; index < edges.size(); ++index) {
			edgesByStart[edges.at(index).from].append(index);
			QPointF point;
			if (vertexPoint(edges.at(index).from, &point)) {
				boundsPoints.append(point);
			}
			if (vertexPoint(edges.at(index).to, &point)) {
				boundsPoints.append(point);
			}
		}
		outline.bounds = rectFromPoints(boundsPoints);

		// Walking the boundary needs more than "take any unused outgoing edge":
		// a vertex shared by several linedefs of the same sector has more than
		// one continuation, and picking the wrong one strands the whole loop.
		// This is the standard planar face traversal. Every edge above is
		// oriented with its sector on the right (front sidedefs start -> end,
		// back sidedefs end -> start), so the wedge of sector interior at a
		// vertex is swept counter-clockwise from the reverse of the edge just
		// travelled. Taking the tightest counter-clockwise turn therefore
		// follows the smallest face on the sector side, and the reverse edge
		// itself is only taken as a last resort.
		const auto selectNextEdge = [&](int cursor, const QPointF& incomingFrom, const QPointF& cursorPoint, const QVector<bool>& usedEdges) {
			const QPointF reverse = incomingFrom - cursorPoint;
			const double reverseAngle = std::atan2(reverse.y(), reverse.x());
			int best = -1;
			double bestTurn = 0.0;
			for (int candidate : edgesByStart.value(cursor)) {
				if (usedEdges.at(candidate)) {
					continue;
				}
				QPointF target;
				if (!vertexPoint(edges.at(candidate).to, &target)) {
					continue;
				}
				const QPointF direction = target - cursorPoint;
				if (std::abs(direction.x()) < kNormalEpsilon && std::abs(direction.y()) < kNormalEpsilon) {
					continue;
				}
				// Counter-clockwise turn from the reverse direction, in
				// (0, 2*pi]. The reverse edge itself lands on 2*pi and is
				// therefore chosen only when nothing else remains.
				double turn = std::atan2(direction.y(), direction.x()) - reverseAngle;
				while (turn <= kNormalEpsilon) {
					turn += kTwoPi;
				}
				while (turn > kTwoPi + kNormalEpsilon) {
					turn -= kTwoPi;
				}
				if (best < 0 || turn < bestTurn) {
					best = candidate;
					bestTurn = turn;
				}
			}
			return best;
		};

		QVector<bool> used(edges.size(), false);
		for (int index = 0; index < edges.size(); ++index) {
			if (used.at(index)) {
				continue;
			}
			QVector<int> chain;
			used[index] = true;
			chain.append(index);
			const int startVertex = edges.at(index).from;
			int cursor = edges.at(index).to;
			int previousVertex = startVertex;
			while (cursor != startVertex) {
				QPointF cursorPoint;
				QPointF previousPoint;
				if (!vertexPoint(cursor, &cursorPoint) || !vertexPoint(previousVertex, &previousPoint)) {
					break;
				}
				const int nextEdge = selectNextEdge(cursor, previousPoint, cursorPoint, used);
				if (nextEdge < 0) {
					break;
				}
				used[nextEdge] = true;
				chain.append(nextEdge);
				previousVertex = cursor;
				cursor = edges.at(nextEdge).to;
			}

			QPolygonF loop;
			bool complete = cursor == startVertex && chain.size() >= 3;
			if (complete) {
				loop.reserve(chain.size());
				for (int edgeIndex : chain) {
					QPointF point;
					if (!vertexPoint(edges.at(edgeIndex).from, &point)) {
						complete = false;
						break;
					}
					loop.append(point);
				}
			}
			if (complete && loop.size() >= 3) {
				outline.loops.append(loop);
			} else {
				outline.openEdgeCount += chain.size();
			}
		}
	}

	return outlines;
}

QVector<QVector<LevelMapVec3>> tessellatePatchMesh(const LevelMapPatch& patch, int subdivisions)
{
	// A Quake III patch mesh is a (2n+1) x (2m+1) control grid that decomposes
	// into n * m 3x3 quadratic Bezier sub-patches. Control points are stored row
	// major: `controlPoints[row * width + column]`, `height` rows of `width`
	// columns. The `.map` file itself lays a patch out width-major (`width`
	// parenthesised groups of `height` points); parsePatchBody normalises that
	// into this layout, which is also q3map2's own internal `mesh_t` order.
	QVector<QVector<LevelMapVec3>> result;
	const int width = patch.width;
	const int height = patch.height;
	if (width < 3 || height < 3 || width > kLevelPatchMaxDimension || height > kLevelPatchMaxDimension || (width % 2) == 0 || (height % 2) == 0) {
		return result;
	}
	if (patch.controlPoints.size() != static_cast<qsizetype>(width) * static_cast<qsizetype>(height)) {
		return result;
	}

	const int stepsX = patch.fixedSubdivisions ? std::clamp(patch.subdivisionsX, 1, 64) : std::clamp(subdivisions, 1, 32);
	const int stepsY = patch.fixedSubdivisions ? std::clamp(patch.subdivisionsY, 1, 64) : std::clamp(subdivisions, 1, 32);
	const int columnPatches = (width - 1) / 2;
	const int rowPatches = (height - 1) / 2;
	const int outputColumns = (columnPatches * stepsX) + 1;
	const int outputRows = (rowPatches * stepsY) + 1;

	result.resize(outputRows);
	for (int row = 0; row < outputRows; ++row) {
		result[row].resize(outputColumns);
	}

	const auto control = [&](int row, int column) -> LevelMapVec3 {
		return patch.controlPoints.at((static_cast<qsizetype>(row) * width) + column);
	};

	for (int patchRow = 0; patchRow < rowPatches; ++patchRow) {
		for (int patchColumn = 0; patchColumn < columnPatches; ++patchColumn) {
			const int baseRow = patchRow * 2;
			const int baseColumn = patchColumn * 2;
			for (int stepV = 0; stepV <= stepsY; ++stepV) {
				const double v = static_cast<double>(stepV) / static_cast<double>(stepsY);
				for (int stepU = 0; stepU <= stepsX; ++stepU) {
					const double u = static_cast<double>(stepU) / static_cast<double>(stepsX);
					LevelMapVec3 rowPoints[3];
					for (int offset = 0; offset < 3; ++offset) {
						rowPoints[offset] = evaluateQuadraticBezier(control(baseRow + offset, baseColumn),
							control(baseRow + offset, baseColumn + 1),
							control(baseRow + offset, baseColumn + 2),
							u);
					}
					const LevelMapVec3 point = evaluateQuadraticBezier(rowPoints[0], rowPoints[1], rowPoints[2], v);
					const int outRow = (patchRow * stepsY) + stepV;
					const int outColumn = (patchColumn * stepsX) + stepU;
					result[outRow][outColumn] = point;
				}
			}
		}
	}

	return result;
}

QVector<QVector<QPointF>> tessellatePatchTexCoords(const LevelMapPatch& patch, int subdivisions)
{
	if (patch.controlU.size() != patch.controlPoints.size() || patch.controlV.size() != patch.controlPoints.size()) { return {}; }
	LevelMapPatch uv = patch;
	for (int i = 0; i < uv.controlPoints.size(); ++i) {
		uv.controlPoints[i] = {patch.controlU.at(i), patch.controlV.at(i), 0.0, true};
	}
	const auto grid = tessellatePatchMesh(uv, subdivisions);
	QVector<QVector<QPointF>> result;
	result.reserve(grid.size());
	for (const auto& row : grid) {
		QVector<QPointF> points;
		points.reserve(row.size());
		for (const auto& p : row) { points << QPointF(p.x, p.y); }
		result << points;
	}
	return result;
}

MapGeometrySummary summarizeLevelMapGeometry(const LevelMapDocument& document)
{
	MapGeometrySummary summary;
	summary.brushCount = static_cast<int>(document.brushes.size());
	summary.patchCount = static_cast<int>(document.patches.size());

	double minX = std::numeric_limits<double>::max();
	double minY = std::numeric_limits<double>::max();
	double minZ = std::numeric_limits<double>::max();
	double maxX = std::numeric_limits<double>::lowest();
	double maxY = std::numeric_limits<double>::lowest();
	double maxZ = std::numeric_limits<double>::lowest();
	bool haveBounds = false;

	const QVector<MapBrushGeometry> brushes = buildLevelMapBrushGeometry(document);
	int reportedWarnings = 0;
	for (const MapBrushGeometry& brush : brushes) {
		if (brush.solved) {
			++summary.solvedBrushCount;
		} else {
			++summary.degenerateBrushCount;
		}
		for (const MapFacePolygon& face : brush.faces) {
			if (!face.isValid()) {
				continue;
			}
			++summary.faceCount;
			summary.polygonPointCount += static_cast<int>(face.points.size());
		}
		if (brush.solved && brush.mins.valid && brush.maxs.valid) {
			minX = std::min(minX, brush.mins.x);
			minY = std::min(minY, brush.mins.y);
			minZ = std::min(minZ, brush.mins.z);
			maxX = std::max(maxX, brush.maxs.x);
			maxY = std::max(maxY, brush.maxs.y);
			maxZ = std::max(maxZ, brush.maxs.z);
			haveBounds = true;
		}
		for (const QString& warning : brush.warnings) {
			if (reportedWarnings >= 16) {
				break;
			}
			++reportedWarnings;
			summary.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Brush %1: %2").arg(brush.brushId).arg(warning);
		}
	}

	const QVector<DoomSectorOutline> outlines = buildDoomSectorOutlines(document);
	for (int index = 0; index < outlines.size(); ++index) {
		const DoomSectorOutline& outline = outlines.at(index);
		if (!outline.loops.isEmpty()) {
			++summary.sectorOutlineCount;
		}
		if (outline.openEdgeCount > 0) {
			++summary.openSectorCount;
			if (reportedWarnings < 16) {
				++reportedWarnings;
				summary.warnings << QCoreApplication::translate("VibeStudioMapGeometry", "Sector %1 has %2 unclosed boundary edges.")
					.arg(outline.sectorId)
					.arg(outline.openEdgeCount);
			}
		}
		if (outline.bounds.isNull()) {
			continue;
		}
		minX = std::min(minX, outline.bounds.left());
		minY = std::min(minY, outline.bounds.top());
		maxX = std::max(maxX, outline.bounds.right());
		maxY = std::max(maxY, outline.bounds.bottom());
		if (index < document.doomSectors.size()) {
			const LevelMapDoomSector& sector = document.doomSectors.at(index);
			minZ = std::min(minZ, static_cast<double>(sector.floorHeight));
			maxZ = std::max(maxZ, static_cast<double>(sector.ceilingHeight));
		} else {
			minZ = std::min(minZ, 0.0);
			maxZ = std::max(maxZ, 0.0);
		}
		haveBounds = true;
	}

	if (haveBounds && minX <= maxX && minY <= maxY && minZ <= maxZ) {
		summary.mins = makeVec3(minX, minY, minZ);
		summary.maxs = makeVec3(maxX, maxY, maxZ);
	}
	return summary;
}

QStringList mapGeometrySummaryLines(const MapGeometrySummary& summary)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioMapGeometry", "Brushes solved: %1 / %2").arg(summary.solvedBrushCount).arg(summary.brushCount);
	lines << QCoreApplication::translate("VibeStudioMapGeometry", "Unsolved brushes: %1").arg(summary.degenerateBrushCount);
	lines << QCoreApplication::translate("VibeStudioMapGeometry", "Face polygons: %1 (%2 points)").arg(summary.faceCount).arg(summary.polygonPointCount);
	lines << QCoreApplication::translate("VibeStudioMapGeometry", "Patch meshes: %1").arg(summary.patchCount);
	lines << QCoreApplication::translate("VibeStudioMapGeometry", "Sector outlines: %1 (%2 with open edges)").arg(summary.sectorOutlineCount).arg(summary.openSectorCount);
	lines << QCoreApplication::translate("VibeStudioMapGeometry", "Geometry bounds min: %1").arg(vectorText(summary.mins));
	lines << QCoreApplication::translate("VibeStudioMapGeometry", "Geometry bounds max: %1").arg(vectorText(summary.maxs));
	if (summary.warnings.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioMapGeometry", "Geometry warnings: none");
	} else {
		lines << QCoreApplication::translate("VibeStudioMapGeometry", "Geometry warnings: %1").arg(summary.warnings.size());
		for (const QString& warning : summary.warnings) {
			lines << warning;
		}
	}
	return lines;
}

} // namespace vibestudio
