#include "core/level_brush.h"
#include "core/level_patch.h"
#include "core/level_placement_control_p.h"
#include "core/level_scene_locks.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <numeric>

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

namespace {
bool createHull(const QVector<LevelMapVec3> &points, const QString &texture, LevelMapBrush *brush, bool envelope, QString *error)
{
	if (error) {
		error->clear();
	}
	if (!brush || points.size() < 4 || points.size() > (envelope ? 4096 : kLevelBrushComponentMaxVertices) ||
		std::any_of(points.cbegin(), points.cend(), [](const auto &p) { return !finite(p); })) {
		return fail(error, envelope ? QT_TRANSLATE_NOOP("LevelBrush", "A convex envelope needs 4 to 4096 finite points within ±32768 units.")
			: QT_TRANSLATE_NOOP("LevelBrush", "A brush hull needs 4 to 256 finite vertices within ±32768 units."));
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
	// Primitive controls must survive as corners; envelopes may absorb interior
	// points. Neither may create corners unrelated to the supplied coordinates.
	for (const auto &p : points) {
		if (envelope) {
			if (std::any_of(topology.geometry.faces.cbegin(),topology.geometry.faces.cend(),[&](const auto& f) { return planeDistanceToPoint(f.plane,p) > epsilon; })) {
				return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The convex envelope could not contain every input point accurately."));
			}
			continue;
		}
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
} // namespace

bool createLevelBrushHull(const QVector<LevelMapVec3> &points, const QString &texture, LevelMapBrush *brush, QString *error)
{
	return createHull(points,texture,brush,false,error);
}

bool createLevelBrushEnvelope(const QVector<LevelMapVec3> &points, const QString &texture, LevelMapBrush *brush, QString *error)
{
	return createHull(points,texture,brush,true,error);
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

namespace
{
struct UvLockPolicy {
	bool enabled = false, fit = false, allowValve220 = false;
};
struct UvSample {
	LevelMapVec3 point;
	QPointF uv;
	bool moved = false;
};

// The three corners a fitted UV lock keeps exactly: corners the edit left in
// place where they span a stable triangle (at least a quarter of the best
// squared extent), so seams with unchanged neighbours hold; otherwise the
// widest triangle. Empty when the face is too narrow to fit.
QVector<int> fittedUvCorners(const QVector<UvSample>& samples)
{
	const auto pick = [&samples](const auto& value, const QVector<int>& taken) {
		int best = -1, still = -1;
		double bestValue = 0, stillValue = 0;
		for (int i = 0; i < samples.size(); ++i) {
			if (taken.contains(i)) { continue; }
			const double v = value(i);
			if (v > bestValue) { bestValue = v; best = i; }
			if (!samples[i].moved && v > stillValue) { stillValue = v; still = i; }
		}
		return still >= 0 && stillValue >= 0.25 * bestValue ? still : best;
	};
	int a = 0;
	for (int i = 0; i < samples.size(); ++i) {
		if (!samples[i].moved) { a = i; break; }
	}
	const int b = pick([&](int i) { const auto d = sub(samples[i].point, samples[a].point); return dot(d, d); }, {a});
	if (b < 0) { return {}; }
	const auto e = sub(samples[b].point, samples[a].point);
	const int c = pick([&](int i) { const auto n = cross(e, sub(samples[i].point, samples[a].point)); return dot(n, n); }, {a, b});
	if (c < 0) { return {}; }
	return {a, b, c};
}

// Exact UV lock solves one planar projection from three corners and checks
// every other corner of the face against it. With the fit policy, a
// deformation no single projection can follow keeps three corners exactly and
// lets the rest drift, and a classic face that cannot hold the result takes
// the nearest classic mapping. Workflow reference: NRC Brush::vertexModeBuildHull
// with TextureVertexLock (Texdef_from_ST) at 449778b3 (GPL-2.0-or-later) and
// TB's UV Lock at 4c756be1 (GPL-3.0-or-later). Original implementation; see
// docs/CREDITS.md.
bool lockComponentFace(const LevelMapBrushFace& source, LevelMapBrushFace* target, const LevelBrushTopology& before,
	const QVector<LevelMapVec3>& points, const QVector<int>& sourceIndices, const MapPlane& plane, const UvLockPolicy& uv,
	bool* fitted, QString* error)
{
	const auto projection = levelTextureProjection(source);
	if (!projection.valid) { return fail(error,QT_TRANSLATE_NOOP("LevelBrush","The source face has no valid UV projection to lock.")); }
	QVector<UvSample> samples;
	for (qsizetype i = 0; i < before.vertices.size(); ++i) {
		if (sourceIndices[i] < 0) { continue; }
		const auto& p = points[sourceIndices[i]];
		if (std::abs(planeDistanceToPoint(plane,p)) < 0.001) {
			const auto moved = sub(p,before.vertices[i]);
			samples.append({p,projection.at(before.vertices[i]),dot(moved,moved) > 1e-12});
		}
	}
	if (samples.size() < 3) { return fail(error,QT_TRANSLATE_NOOP("LevelBrush","The deformed face has too few corresponding corners for UV lock.")); }
	const auto solve = [&](int ia, int ib, int ic, LevelTextureProjection* out) {
		const auto a = samples[ia].point;
		const auto e = sub(samples[ib].point,a), f = sub(samples[ic].point,a), n = cross(e,f);
		const double area = dot(n,n);
		if (area < 1e-16) { return false; }
		const auto u = cross(f,n), v = cross(n,e);
		const auto axis = [&](double origin, double alongE, double alongF) {
			return LevelMapVec3{(u.x*(alongE-origin)+v.x*(alongF-origin))/area,
				(u.y*(alongE-origin)+v.y*(alongF-origin))/area,(u.z*(alongE-origin)+v.z*(alongF-origin))/area,true};
		};
		LevelTextureProjection locked;
		locked.valid = true; locked.normalizedCoordinates = projection.normalizedCoordinates;
		locked.u = axis(samples[ia].uv.x(),samples[ib].uv.x(),samples[ic].uv.x());
		locked.v = axis(samples[ia].uv.y(),samples[ib].uv.y(),samples[ic].uv.y());
		locked.offsetU = samples[ia].uv.x()-dot(locked.u,a);
		locked.offsetV = samples[ia].uv.y()-dot(locked.v,a);
		*out = locked;
		return true;
	};
	int b = 1, c = 2; double longest = 0, area = 0;
	const auto a = samples[0].point;
	for (int i = 1; i < samples.size(); ++i) {
		const auto e = sub(samples[i].point,a); const double length = dot(e,e);
		if (length > longest) { longest = length; b = i; }
	}
	const auto e = sub(samples[b].point,a);
	for (int i = 1; i < samples.size(); ++i) {
		const auto n = cross(e,sub(samples[i].point,a)); const double value = dot(n,n);
		if (value > area) { area = value; c = i; }
	}
	LevelTextureProjection locked;
	if (area < 1e-16 || !solve(0,b,c,&locked)) { return fail(error,QT_TRANSLATE_NOOP("LevelBrush","The deformed face is too narrow for stable UV lock.")); }
	QVector<int> anchors{0,b,c};
	const bool exact = std::all_of(samples.cbegin(),samples.cend(),[&locked](const auto& sample) {
		const auto uv = locked.at(sample.point);
		return std::abs(uv.x()-sample.uv.x()) <= 1e-5+1e-9*std::abs(sample.uv.x())
			&& std::abs(uv.y()-sample.uv.y()) <= 1e-5+1e-9*std::abs(sample.uv.y());
	});
	if (!exact) {
		if (!uv.fit) {
			return fail(error,QT_TRANSLATE_NOOP("LevelBrush","This deformation cannot keep every corner's UV coordinates in one planar projection. Disable component UV lock or use a different transform."));
		}
		anchors = fittedUvCorners(samples);
		if (anchors.size() != 3 || !solve(anchors[0],anchors[1],anchors[2],&locked)) {
			return fail(error,QT_TRANSLATE_NOOP("LevelBrush","The deformed face is too narrow for stable UV lock."));
		}
		*fitted = true;
	}
	QString problem;
	if (!setLevelTextureProjection(target,locked,uv.allowValve220,&problem)) {
		if (!uv.fit || target->explicitTextureAxes || target->explicitTextureMatrix) {
			if (error) { *error = problem; }
			return false;
		}
		QVector<QPair<LevelMapVec3,QPointF>> pins;
		for (int index : std::as_const(anchors)) { pins.append({samples[index].point,samples[index].uv}); }
		const auto nearest = nearestClassicLevelTextureProjection(*target,locked,pins);
		if (!nearest.valid || !setLevelTextureProjection(target,nearest,false,nullptr)) {
			if (error) { *error = problem; }
			return false;
		}
		*fitted = true;
	}
	target->textureParametersDirty = true;
	return true;
}

// Rebuild all topology edits through the same validation and face inheritance.
// sourceIndices maps each original vertex to its candidate point, or -1 when
// explicitly removed. New points have no source vertex. This implementation
// is original; insertion/removal behaviour follows TrenchBroom's vertex tool
// (2026.2 manual). See docs/CREDITS.md, Brush topology authoring.
bool rebuildBrush(LevelMapBrush *brush, const LevelBrushTopology &before, const QVector<LevelMapVec3> &points,
				  const QVector<int> &sourceIndices, int firstInserted, bool allowCollapse,
				  LevelBrushEditReport result, LevelBrushEditReport *report, QString *error, const UvLockPolicy &uv = {})
{
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
				if (sourceIndices[v] >= 0 && std::abs(planeDistanceToPoint(plane.plane, points[sourceIndices[v]])) < 0.001) {
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
		bool fitted = false;
		if (uv.enabled && !lockComponentFace(brush->faces[donor],&face,before,points,sourceIndices,plane.plane,uv,&fitted,error)) { return false; }
		result.fittedFaces += fitted ? 1 : 0;
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
	// Every inserted point must become a real corner. A redundant point on
	// an existing edge/face would disappear when saved as supporting planes.
	for (int i = firstInserted; i >= 0 && i < points.size(); ++i) {
		if (std::none_of(after.vertices.cbegin(), after.vertices.cend(), [&](const auto &v) { return close(points[i], v); })) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "An inserted vertex must become a distinct corner of the convex brush. "
															   "Move it outside the existing face or edge."));
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
} // namespace

bool moveLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components, const LevelMapVec3 &delta,
							  double grid, bool allowCollapse, LevelBrushEditReport *report, QString *error)
{
	if (!finite(delta)) { return fail(error,QT_TRANSLATE_NOOP("LevelBrush","Use finite movement within ±32768 units.")); }
	LevelTextureAffine matrix; matrix.translation = delta;
	return transformLevelBrushComponents(brush,kind,components,matrix,grid,allowCollapse,false,false,report,error);
}

bool transformLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components,
	const LevelTextureAffine& matrix, double grid, bool allowCollapse, bool textureLock, bool allowValve220,
	LevelBrushEditReport* report, QString* error, bool fitUvLock)
{
	if (error) {
		error->clear();
	}
	if (report) {
		*report = {};
	}
	const auto validVector = [](const auto& p) { return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
	if (!brush || !validVector(matrix.translation) || !std::all_of(matrix.columns.cbegin(),matrix.columns.cend(),validVector) ||
		!std::isfinite(grid) || grid < 0 || grid > coordinateLimit) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Use a finite component transform and a grid from 0 to 32768."));
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
		points[i] = transformedLevelComponentPoint(points[i],matrix,grid);
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
	QVector<int> sourceIndices(before.vertices.size());
	std::iota(sourceIndices.begin(), sourceIndices.end(), 0);
	return rebuildBrush(brush, before, points, sourceIndices, -1, allowCollapse, result, report, error, {textureLock, fitUvLock, allowValve220});
}

bool removeLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components,
								LevelBrushEditReport *report, QString *error)
{
	if (error) {
		error->clear();
	}
	if (report) {
		*report = {};
	}
	if (!brush) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Choose a brush to edit."));
	}
	LevelBrushTopology before;
	if (!levelBrushTopology(*brush, &before, error)) {
		return false;
	}
	const auto selected = levelBrushComponentVertices(before, kind, components);
	if (selected.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Select existing brush components to remove."));
	}
	QVector<LevelMapVec3> points;
	QVector<int> sourceIndices(before.vertices.size(), -1);
	for (int i = 0; i < before.vertices.size(); ++i) {
		if (!selected.contains(i)) {
			sourceIndices[i] = static_cast<int>(points.size());
			points << before.vertices[i];
		}
	}
	LevelBrushEditReport result;
	result.verticesBefore = static_cast<int>(before.vertices.size());
	result.facesBefore = static_cast<int>(brush->faces.size());
	result.removedVertices = static_cast<int>(selected.size());
	return rebuildBrush(brush, before, points, sourceIndices, -1, false, result, report, error);
}

bool insertLevelBrushVertices(LevelMapBrush *brush, const QVector<LevelMapVec3> &positions, double grid,
							  bool allowCollapse, LevelBrushEditReport *report, QString *error)
{
	if (error) {
		error->clear();
	}
	if (report) {
		*report = {};
	}
	if (!brush || !std::isfinite(grid) || grid < 0 || grid > coordinateLimit
		|| positions.isEmpty() || positions.size() > kLevelBrushComponentMaxVertices) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Choose 1 to 256 new vertices and a finite grid from 0 to 32768."));
	}
	LevelBrushTopology before;
	if (!levelBrushTopology(*brush, &before, error)) {
		return false;
	}
	if (before.vertices.size() + positions.size() > kLevelBrushComponentMaxVertices) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The insertion exceeds the component editor's 256-vertex limit."));
	}
	QVector<LevelMapVec3> points = before.vertices;
	for (const auto &position : positions) {
		if (!finite(position)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "New vertices must be finite and within ±32768 units."));
		}
		const auto snapped = snapLevelMapPosition(position, grid);
		if (!finite(snapped)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "The snapped vertex would lie outside ±32768 units."));
		}
		if (std::any_of(points.cbegin(), points.cend(), [&](const auto &p) { return close(p, snapped); })) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "A new vertex coincides with an existing or another new vertex."));
		}
		points << snapped;
	}
	QVector<int> sourceIndices(before.vertices.size());
	std::iota(sourceIndices.begin(), sourceIndices.end(), 0);
	LevelBrushEditReport result;
	result.verticesBefore = static_cast<int>(before.vertices.size());
	result.facesBefore = static_cast<int>(brush->faces.size());
	result.insertedVertices = static_cast<int>(positions.size());
	return rebuildBrush(brush, before, points, sourceIndices, result.verticesBefore, allowCollapse, result, report, error);
}

bool splitLevelBrushComponents(LevelMapBrush *brush, LevelBrushComponent kind, const QVector<int> &components,
	const LevelMapVec3 &delta, double grid, bool allowCollapse, LevelBrushEditReport *report, QString *error)
{
	if (error) { error->clear(); }
	if (report) { *report = {}; }
	if (!brush || !finite(delta) || !std::isfinite(grid) || grid < 0 || grid > coordinateLimit
		|| (kind != LevelBrushComponent::Edge && kind != LevelBrushComponent::Face) || components.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Choose edges or faces to split, a finite movement and a grid from 0 to 32768."));
	}
	LevelBrushTopology topology;
	if (!levelBrushTopology(*brush, &topology, error)) { return false; }
	const auto count = kind == LevelBrushComponent::Edge ? topology.edges.size() : topology.faces.size();
	QSet<int> seen;
	QVector<LevelMapVec3> positions;
	for (int id : components) {
		if (id < 0 || id >= count || seen.contains(id)) {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "A component to split is missing or named twice."));
		}
		seen.insert(id);
		const auto vertices = levelBrushComponentVertices(topology, kind, {id});
		LevelMapVec3 center{0, 0, 0, true};
		for (int index : vertices) {
			center.x += topology.vertices[index].x;
			center.y += topology.vertices[index].y;
			center.z += topology.vertices[index].z;
		}
		const double size = static_cast<double>(vertices.size());
		positions << LevelMapVec3{center.x / size + delta.x, center.y / size + delta.y, center.z / size + delta.z, true};
	}
	if (grid == 0 && delta.x == 0 && delta.y == 0 && delta.z == 0) {
		if (report) {
			report->verticesBefore = report->verticesAfter = static_cast<int>(topology.vertices.size());
			report->facesBefore = report->facesAfter = static_cast<int>(brush->faces.size());
		}
		return true;
	}
	return insertLevelBrushVertices(brush, positions, grid, allowCollapse, report, error);
}

namespace
{
enum class ComponentEdit { Remove, Split };
bool editMapComponents(LevelMapDocument *document, const QVector<LevelMapComponentSet> &sets, const LevelMapVec3 &delta, bool allowCollapse, ComponentEdit operation,
							LevelMapComponentReport *report, QString *error)
{
	if (report) { *report = {}; }
	if (error) { error->clear(); }
	LevelMapComponentReport staged;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument *candidate) {
			return editMapComponents(candidate, sets, delta, allowCollapse, operation, report ? &staged : nullptr, error);
		});
		guarded.has_value()) {
		if (*guarded && report) {
			*report = staged;
		}
		return *guarded;
	}
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Open a Quake-family map to edit brush and patch components."));
	}
	if (sets.isEmpty() || sets.size() > 256 || !finite(delta)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Choose components of 1 to 256 brushes and patches, and a finite movement."));
	}
	LevelMapComponentReport result;
	QMap<int, LevelMapBrush> brushes;
	QSet<int> seenBrushes;
	for (const auto &set : sets) {
		detail::placementCancellationCheckpoint();
		if (operation == ComponentEdit::Remove) { detail::placementCheckpoint(LevelPlacementPhase::Deleting, seenBrushes.size(), sets.size()); }
		if (set.object.kind == LevelMapSelectionKind::QuakeBrush) {
			const auto found = std::find_if(document->brushes.cbegin(), document->brushes.cend(), [&set](const auto &b) { return b.id == set.object.objectId; });
			if (found == document->brushes.cend() || seenBrushes.contains(set.object.objectId)) {
				return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "A brush to edit is missing or named twice."));
			}
			seenBrushes.insert(set.object.objectId);
			LevelMapBrush moved = *found;
			LevelBrushEditReport edit;
			QString problem;
			const bool edited = operation == ComponentEdit::Split ? splitLevelBrushComponents(&moved, set.kind, set.components, delta, 0, allowCollapse, &edit, &problem)
				: removeLevelBrushComponents(&moved, set.kind, set.components, &edit, &problem);
			if (!edited) {
				if (error) {
					*error = QCoreApplication::translate("LevelBrush", "Brush %1: %2").arg(set.object.objectId).arg(problem);
				}
				return false;
			}
			if (!edit.changed) {
				continue;
			}
			brushes.insert(set.object.objectId,moved);
			++result.brushes;
			result.collapsedVertices += edit.collapsedVertices;
			result.removedVertices += edit.removedVertices;
			result.insertedVertices += edit.insertedVertices;
		} else if (set.object.kind == LevelMapSelectionKind::QuakePatch) {
			if (operation == ComponentEdit::Split) {
				return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Split brush edges or faces; use the patch matrix tools to insert control points."));
			}
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Remove patch rows or columns with the patch matrix tools; individual control points cannot be removed."));
		} else {
			return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Only brushes and patches have components to move."));
		}
	}
	if (!replaceLevelMapComponentGeometry(document,brushes,{},
		operation == ComponentEdit::Split ? QCoreApplication::translate("LevelBrush", "Split components of %n brush(es)", nullptr, result.brushes)
			: QCoreApplication::translate("LevelBrush", "Remove components of %n brush(es)", nullptr, result.brushes),error)) {
		return false;
	}
	if (report) {
		*report = result;
	}
	return true;
}
} // namespace

bool moveLevelMapComponents(LevelMapDocument *document, const QVector<LevelMapComponentSet> &sets, const LevelMapVec3 &delta, bool allowCollapse,
							LevelMapComponentReport *report, QString *error)
{
	if (report) { *report = {}; }
	LevelComponentTransformRequest request; request.delta = delta; request.allowCollapse = allowCollapse;
	return transformLevelMapComponents(document,sets,request,report,error);
}

bool removeLevelMapComponents(LevelMapDocument *document, const QVector<LevelMapComponentSet> &sets,
							  LevelMapComponentReport *report, QString *error)
{
	return editMapComponents(document, sets, {0, 0, 0, true}, false, ComponentEdit::Remove, report, error);
}

bool splitLevelMapComponents(LevelMapDocument *document, const QVector<LevelMapComponentSet> &sets, const LevelMapVec3 &delta,
	bool allowCollapse, LevelMapComponentReport *report, QString *error)
{
	return editMapComponents(document, sets, delta, allowCollapse, ComponentEdit::Split, report, error);
}

bool snapLevelMapBrushVertices(LevelMapDocument *document, const QVector<int> &brushIds, double grid, int *snapped, QStringList *skipped,
							   QString *error)
{
	int stagedSnapped = 0;
	QStringList stagedSkipped;
	if (const auto guarded = guardLevelSceneEdit(document, error, [&](LevelMapDocument *candidate) {
			return snapLevelMapBrushVertices(candidate, brushIds, grid, &stagedSnapped, &stagedSkipped, error);
		});
		guarded.has_value()) {
		if (*guarded) {
			if (snapped) {
				*snapped = stagedSnapped;
			}
			if (skipped) {
				*skipped = stagedSkipped;
			}
		}
		return *guarded;
	}
	if (error) {
		error->clear();
	}
	if (snapped) {
		*snapped = 0;
	}
	if (skipped) {
		skipped->clear();
	}
	if (!document || (document->format != LevelMapFormat::QuakeMap && document->format != LevelMapFormat::Quake3Map)) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Open a Quake-family map to snap brush vertices."));
	}
	if (!std::isfinite(grid) || grid <= 0 || grid > 4096) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Snap vertices to a grid from more than 0 to 4096 units."));
	}
	if (brushIds.isEmpty()) {
		return fail(error, QT_TRANSLATE_NOOP("LevelBrush", "Select brushes to snap their vertices."));
	}
	LevelMapDocument working = *document;
	const quint64 revision = working.revision;
	QStringList refusals;
	int changed = 0;
	for (const int id : brushIds) {
		const auto found = std::find_if(working.brushes.cbegin(), working.brushes.cend(), [id](const auto &b) { return b.id == id; });
		if (found == working.brushes.cend()) {
			refusals << QCoreApplication::translate("LevelBrush", "Brush %1 is no longer in the map.").arg(id);
			continue;
		}
		LevelBrushTopology topology;
		QString problem;
		if (!levelBrushTopology(*found, &topology, &problem)) {
			refusals << QCoreApplication::translate("LevelBrush", "Brush %1: %2").arg(id).arg(problem);
			continue;
		}
		QVector<int> all(topology.vertices.size());
		std::iota(all.begin(), all.end(), 0);
		LevelMapBrush moved = *found;
		LevelBrushEditReport edit;
		if (!moveLevelBrushComponents(&moved, LevelBrushComponent::Vertex, all, {0, 0, 0, true}, grid, false, &edit, &problem)
			|| (edit.changed && !replaceLevelMapBrushGeometry(&working, id, moved, &problem))) {
			refusals << QCoreApplication::translate("LevelBrush", "Brush %1: %2").arg(id).arg(problem);
			continue;
		}
		changed += edit.changed ? 1 : 0;
	}
	const int steps = static_cast<int>(working.revision - revision);
	if (steps == 0) {
		if (changed == 0 && !refusals.isEmpty() && refusals.size() == brushIds.size()) {
			if (error) {
				*error = refusals.first();
			}
			return false;
		}
		if (skipped) {
			*skipped = refusals;
		}
		return true;
	}
	if (steps > 1
		&& !collapseLevelMapUndoSteps(&working, steps,
			QCoreApplication::translate("LevelBrush", "Snap the vertices of %n brush(es) to the grid", nullptr, changed),
			QCoreApplication::translate("LevelBrush", "Put the brushes' vertices back"), error)) {
		return false;
	}
	*document = std::move(working);
	if (snapped) {
		*snapped = changed;
	}
	if (skipped) {
		*skipped = refusals;
	}
	return true;
}
} // namespace vibestudio
