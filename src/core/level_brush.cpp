#include "core/level_brush.h"
#include "core/level_placement_control_p.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
constexpr double epsilon = 0.02;
constexpr double coordinateLimit = 32768.0;
bool fail(QString *error, const char *message)
{
	if (error) {
		*error = QCoreApplication::translate("LevelBrush", message);
	}
	return false;
}
LevelMapVec3 sub(const LevelMapVec3 &a, const LevelMapVec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z, true}; }
double dot(const LevelMapVec3 &a, const LevelMapVec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
LevelMapVec3 cross(const LevelMapVec3 &a, const LevelMapVec3 &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x, true};
}
bool close(const LevelMapVec3 &a, const LevelMapVec3 &b) { return dot(sub(a, b), sub(a, b)) <= epsilon * epsilon; }
bool finite(const LevelMapVec3 &p)
{
	return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) <= coordinateLimit &&
		   std::abs(p.y) <= coordinateLimit && std::abs(p.z) <= coordinateLimit;
}
bool planeEqual(const MapPlane &a, const MapPlane &b)
{
	return std::abs(a.normalX - b.normalX) < 1e-6 && std::abs(a.normalY - b.normalY) < 1e-6 && std::abs(a.normalZ - b.normalZ) < 1e-6 &&
		   std::abs(a.distance - b.distance) < 0.001;
}
struct Triangle {
	int a, b, c;
	MapPlane plane;
};

// Incremental 3D hull. A seed tetrahedron provides an interior reference;
// every new point replaces the visible triangles with its horizon fan. This
// is original implementation; plane conventions use our shared map solver.
bool hull(const QVector<LevelMapVec3> &points, QVector<Triangle> *result, QString *error)
{
	if (points.size() < 4) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "A brush needs at least four distinct vertices."));
	}
	const int a = 0;
	int b = -1, c = -1, d = -1;
	double best = epsilon * epsilon;
	for (int i = 1; i < points.size(); ++i) {
		const double distance = dot(sub(points[i], points[a]), sub(points[i], points[a]));
		if (distance > best) {
			best = distance;
			b = i;
		}
	}
	if (b < 0) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The edit collapses the brush to a point."));
	}
	const double lineLengthSquared = best;
	best = epsilon * epsilon;
	for (int i = 0; i < points.size(); ++i) {
		const auto normal = cross(sub(points[i], points[a]), sub(points[b], points[a]));
		const double distance = dot(normal, normal) / lineLengthSquared;
		if (distance > best) {
			best = distance;
			c = i;
		}
	}
	if (c < 0) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The edit collapses the brush to a line."));
	}
	const auto seedPlane = planeFromPoints(points[a], points[b], points[c]);
	best = epsilon;
	for (int i = 0; i < points.size(); ++i) {
		const double distance = std::abs(planeDistanceToPoint(seedPlane, points[i]));
		if (distance > best) {
			best = distance;
			d = i;
		}
	}
	if (d < 0) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The edit flattens the brush; a brush must enclose a volume."));
	}
	const LevelMapVec3 inside{(points[a].x + points[b].x + points[c].x + points[d].x) / 4,
							  (points[a].y + points[b].y + points[c].y + points[d].y) / 4,
							  (points[a].z + points[b].z + points[c].z + points[d].z) / 4, true};
	const auto triangle = [&](int x, int y, int z) {
		auto plane = planeFromPoints(points[x], points[y], points[z]);
		if (planeDistanceToPoint(plane, inside) > 0) {
			std::swap(x, z);
			plane = planeFromPoints(points[x], points[y], points[z]);
		}
		return Triangle{x, y, z, plane};
	};
	QVector<Triangle> triangles{triangle(a, b, c), triangle(a, d, b), triangle(a, c, d), triangle(b, d, c)};
	for (int i = 0; i < points.size(); ++i) {
		if (i == a || i == b || i == c || i == d) {
			continue;
		}
		detail::placementCancellationCheckpoint();
		QVector<Triangle> kept;
		QMap<QPair<int, int>, int> horizon;
		for (const auto &t : triangles) {
			if (planeDistanceToPoint(t.plane, points[i]) <= 0.0001) {
				kept << t;
				continue;
			}
			for (const auto &e : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)}) {
				++horizon[qMakePair(std::min(e.first, e.second), std::max(e.first, e.second))];
			}
		}
		if (horizon.isEmpty()) {
			continue;
		}
		for (auto it = horizon.cbegin(); it != horizon.cend(); ++it) {
			if (it.value() == 1) {
				const auto added = triangle(it.key().first, it.key().second, i);
				if (!added.plane.valid) {
					return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The edit produces an unstable edge. "
																	   "Adjust the movement or grid."));
				}
				kept << added;
			}
		}
		triangles = kept;
	}
	QVector<Triangle> planes;
	for (const auto &t : triangles) {
		if (std::none_of(planes.cbegin(), planes.cend(), [&](const auto &p) { return planeEqual(t.plane, p.plane); })) {
			planes << t;
		}
	}
	if (planes.size() > kLevelBrushComponentMaxFaces) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The result exceeds the component editor's 128-face limit."));
	}
	*result = planes;
	return true;
}
} // namespace

bool levelBrushTopology(const LevelMapBrush &brush, LevelBrushTopology *topology, QString *error)
{
	if (error) {
		error->clear();
	}
	if (!topology || brush.faces.size() < 4 || brush.faces.size() > kLevelBrushComponentMaxFaces) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Component editing requires a closed brush with 4 to 128 faces."));
	}
	for (const auto &face : brush.faces) {
		if (!finite(face.p0) || !finite(face.p1) || !finite(face.p2)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Brush plane points must be finite and within ±32768 units."));
		}
	}
	LevelBrushTopology made;
	made.geometry = solveBrushGeometry(brush.faces, brush.id, brush.entityId, MapGeometryPrecision::CompilerCompatible, [] {
		detail::placementCancellationCheckpoint();
		return false;
	});
	if (!made.geometry.solved || !made.geometry.warnings.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Repair the brush's open, duplicate or degenerate "
														   "planes before editing components."));
	}
	for (const auto &face : made.geometry.faces) {
		if (!face.isValid()) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Every brush face must bound a polygon "
															   "before editing components."));
		}
		for (const auto &p : face.points) {
			if (!finite(p)) {
				return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Brush vertices must be finite and within ±32768 units."));
			}
			if (std::none_of(made.vertices.cbegin(), made.vertices.cend(), [&](const auto &v) { return close(p, v); })) {
				made.vertices << p;
			}
		}
	}
	if (made.vertices.size() > kLevelBrushComponentMaxVertices) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The brush exceeds the component editor's 256-vertex limit."));
	}
	std::sort(made.vertices.begin(), made.vertices.end(), [](const auto &a, const auto &b) {
		if (a.x != b.x) {
			return a.x < b.x;
		}
		if (a.y != b.y) {
			return a.y < b.y;
		}
		return a.z < b.z;
	});
	QMap<QPair<int, int>, int> edges;
	for (const auto &face : made.geometry.faces) {
		QVector<int> ids;
		for (const auto &p : face.points) {
			const auto it = std::find_if(made.vertices.cbegin(), made.vertices.cend(), [&](const auto &v) { return close(p, v); });
			ids << static_cast<int>(std::distance(made.vertices.cbegin(), it));
		}
		for (int i = 0; i < ids.size(); ++i) {
			const int a = ids[i], b = ids[(i + 1) % ids.size()];
			++edges[qMakePair(std::min(a, b), std::max(a, b))];
		}
		made.faces << ids;
	}
	for (auto it = edges.cbegin(); it != edges.cend(); ++it) {
		if (it.value() != 2 || it.key().first == it.key().second) {
			if (error) {
				const auto a = made.vertices[it.key().first], b = made.vertices[it.key().second];
				*error = QCoreApplication::translate("LevelBrush", "Brush edge (%1, %2, %3) to (%4, %5, %6) has %7 "
																   "adjacent faces; a closed solid needs two.")
							 .arg(a.x)
							 .arg(a.y)
							 .arg(a.z)
							 .arg(b.x)
							 .arg(b.y)
							 .arg(b.z)
							 .arg(it.value());
			}
			return false;
		}
		made.edges << std::array<int, 2>{it.key().first, it.key().second};
	}
	*topology = made;
	return true;
}

bool createLevelBrushHull(const QVector<LevelMapVec3> &points, const QString &texture, LevelMapBrush *brush, QString *error)
{
	if (error) {
		error->clear();
	}
	if (!brush || points.size() < 4 || points.size() > kLevelBrushComponentMaxVertices ||
		std::any_of(points.cbegin(), points.cend(), [](const auto &p) { return !finite(p); })) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "A brush hull needs 4 to 256 finite vertices within ±32768 units."));
	}
	QVector<Triangle> planes;
	if (!hull(points, &planes, error)) {
		return false;
	}
	LevelMapBrush candidate;
	candidate.primitiveKind = QStringLiteral("classic");
	for (const auto &plane : planes) {
		LevelMapBrushFace face;
		face.id = static_cast<int>(candidate.faces.size());
		face.p0 = points[plane.a];
		face.p1 = points[plane.b];
		face.p2 = points[plane.c];
		face.textureName = texture;
		candidate.faces << face;
	}
	LevelBrushTopology topology;
	if (!levelBrushTopology(candidate, &topology, error)) {
		return false;
	}
	// No input point may disappear or be replaced by an unrelated solver
	// intersection: primitive controls describe the actual solid, not a guess.
	for (const auto &p : points) {
		if (std::none_of(topology.vertices.cbegin(), topology.vertices.cend(), [&](const auto &v) { return close(p, v); })) {
			return fail(error,
						QT_TRANSLATE_NOOP("LevelBrush",
										  "The primitive loses vertices at this size or detail. Increase its size or reduce detail."));
		}
	}
	for (const auto &v : topology.vertices) {
		if (std::none_of(points.cbegin(), points.cend(), [&](const auto &p) { return close(p, v); })) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The primitive cannot be reconstructed accurately at these coordinates."));
		}
	}
	candidate.faceCount = static_cast<int>(candidate.faces.size());
	candidate.textureNames = {texture};
	candidate.mins = topology.geometry.mins;
	candidate.maxs = topology.geometry.maxs;
	candidate.boundsSolved = true;
	*brush = std::move(candidate);
	return true;
}

QVector<int> levelBrushComponentVertices(const LevelBrushTopology &topology, LevelBrushComponent kind, const QVector<int> &components)
{
	QSet<int> selected;
	for (const int id : components) {
		if (id < 0) {
			return {};
		}
		if (kind == LevelBrushComponent::Vertex && id < topology.vertices.size()) {
			selected.insert(id);
		} else if (kind == LevelBrushComponent::Edge && id < topology.edges.size()) {
			selected.insert(topology.edges[id][0]);
			selected.insert(topology.edges[id][1]);
		} else if (kind == LevelBrushComponent::Face && id < topology.faces.size()) {
			for (int v : topology.faces[id]) {
				selected.insert(v);
			}
		} else {
			return {};
		}
	}
	QVector<int> result(selected.cbegin(), selected.cend());
	std::sort(result.begin(), result.end());
	return result;
}

bool moveLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components, const LevelMapVec3 &delta,
							  double grid, bool allowCollapse, LevelBrushEditReport *report, QString *error)
{
	if (error) {
		error->clear();
	}
	if (report) {
		*report = {};
	}
	if (!brush || !finite(delta) || !std::isfinite(grid) || grid < 0 || grid > coordinateLimit) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Use finite movement within ±32768 "
														   "units and a grid from 0 to 32768."));
	}
	LevelBrushTopology before;
	if (!levelBrushTopology(*brush, &before, error)) {
		return false;
	}
	const auto selected = levelBrushComponentVertices(before, kind, components);
	if (selected.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Select existing brush components to move."));
	}
	auto points = before.vertices;
	bool changed = false;
	for (int i : selected) {
		const auto move = [grid](double value, double amount) {
			return grid > 0 ? std::round((value + amount) / grid) * grid : value + amount;
		};
		points[i] = {move(points[i].x, delta.x), move(points[i].y, delta.y), move(points[i].z, delta.z), true};
		if (!finite(points[i])) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The moved vertex would lie outside ±32768 units."));
		}
		const auto difference = sub(points[i], before.vertices[i]);
		changed |= dot(difference, difference) > 1e-14;
	}
	LevelBrushEditReport result;
	result.verticesBefore = result.verticesAfter = static_cast<int>(points.size());
	result.facesBefore = result.facesAfter = static_cast<int>(brush->faces.size());
	if (!changed) {
		if (report) {
			*report = result;
		}
		return true;
	}
	QVector<Triangle> planes;
	if (!hull(points, &planes, error)) {
		return false;
	}
	LevelMapBrush candidate = *brush;
	candidate.faces.clear();
	for (const auto &plane : planes) {
		int donor = -1, mostShared = -1;
		double mostAligned = -2;
		for (int f = 0; f < before.faces.size(); ++f) {
			int shared = 0;
			for (int v : before.faces[f]) {
				if (std::abs(planeDistanceToPoint(plane.plane, points[v])) < 0.001) {
					++shared;
				}
			}
			const auto &n = before.geometry.faces[f].plane;
			const double alignment = n.normalX * plane.plane.normalX + n.normalY * plane.plane.normalY + n.normalZ * plane.plane.normalZ;
			if (shared > mostShared || (shared == mostShared && alignment > mostAligned)) {
				donor = f;
				mostShared = shared;
				mostAligned = alignment;
			}
		}
		auto face = brush->faces[donor];
		face.p0 = points[plane.a];
		face.p1 = points[plane.b];
		face.p2 = points[plane.c];
		if (face.explicitPlane) {
			face.planeNormal = {plane.plane.normalX, plane.plane.normalY, plane.plane.normalZ, true};
			face.planeDistance = -plane.plane.distance;
		}
		candidate.faces << face;
	}
	// Keep inherited source faces adjacent and ordering deterministic.
	std::stable_sort(candidate.faces.begin(), candidate.faces.end(), [](const auto &a, const auto &b) { return a.line < b.line; });
	LevelBrushTopology after;
	if (!levelBrushTopology(candidate, &after, error)) {
		return false;
	}
	for (const auto &p : points) {
		if (std::none_of(after.vertices.cbegin(), after.vertices.cend(), [&](const auto &v) { return close(p, v); })) {
			++result.collapsedVertices;
		}
	}
	// Distinct source vertices may coalesce onto one surviving corner.
	result.collapsedVertices = std::max(result.collapsedVertices, static_cast<int>(points.size() - after.vertices.size()));
	if (result.collapsedVertices > 0 && !allowCollapse) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The edit would merge or remove vertices. Enable "
														   "Allow Vertex Collapse to apply this change."));
	}
	// Verify the solved result corresponds to the requested hull, rather than
	// trusting bounds alone after plane rounding and winding clipping.
	for (const auto &v : after.vertices) {
		if (std::none_of(points.cbegin(), points.cend(), [&](const auto &p) { return close(p, v); })) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The brush solver cannot represent this edit "
															   "accurately. Adjust the movement or grid."));
		}
	}
	candidate.faceCount = static_cast<int>(candidate.faces.size());
	candidate.mins = after.geometry.mins;
	candidate.maxs = after.geometry.maxs;
	candidate.boundsSolved = true;
	candidate.geometryDirty = true;
	candidate.textureNames.clear();
	for (const auto &face : candidate.faces) {
		if (!candidate.textureNames.contains(face.textureName)) {
			candidate.textureNames << face.textureName;
		}
	}
	result.verticesAfter = static_cast<int>(after.vertices.size());
	result.facesAfter = candidate.faceCount;
	result.changed = true;
	*brush = candidate;
	if (report) {
		*report = result;
	}
	return true;
}
} // namespace vibestudio
