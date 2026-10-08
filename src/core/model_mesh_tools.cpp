#include "core/model_mesh_tools.h"

#include "core/model_mesh_tools_p.h"
#include "core/model_transform_axes.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <numeric>

namespace vibestudio
{
namespace model_tools
{
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}

P3 faceCross(const ModelSurface &surface, int frame, const ModelTriangle &t)
{
	const auto a = position(surface, frame, t.a), b = position(surface, frame, t.b), c = position(surface, frame, t.c);
	return cross(b - a, c - a);
}

P3 cornerNormal(const ModelSurface &surface, int frame, const ModelTriangle &t, int corner)
{
	const int v[3]{t.a, t.b, t.c};
	const P3 p = position(surface, frame, v[corner]);
	const P3 next = position(surface, frame, v[(corner + 1) % 3]) - p, previous = position(surface, frame, v[(corner + 2) % 3]) - p;
	const double ln = length(next), lp = length(previous);
	if (ln <= 0 || lp <= 0)
		return {0, 0, 0};
	const double angle = std::acos(std::clamp(dot(next, previous) / (ln * lp), -1.0, 1.0));
	return unit(cross(next, previous)) * angle;
}

double minimumAngle(P3 a, P3 b, P3 c)
{
	const auto angle = [](P3 at, P3 p, P3 q)
	{
		const P3 u = p - at, v = q - at;
		const double lu = length(u), lv = length(v);
		if (lu <= 0 || lv <= 0)
			return 0.0;
		return std::acos(std::clamp(dot(u, v) / (lu * lv), -1.0, 1.0));
	};
	return std::min({angle(a, b, c), angle(b, c, a), angle(c, a, b)});
}

bool collapsedInAnyFrame(const ModelSurface &surface, const ModelTriangle &t)
{
	for (const auto &frame : surface.frames)
	{
		if (collapsedTriangle(frame.positions[t.a], frame.positions[t.b], frame.positions[t.c]))
			return true;
	}
	return false;
}

bool flipsInAnyFrame(const ModelSurface &surface, const ModelTriangle &before, const ModelTriangle &after)
{
	for (int frame = 0; frame < surface.frames.size(); ++frame)
	{
		if (dot(faceCross(surface, frame, before), faceCross(surface, frame, after)) <= 0)
			return true;
	}
	return false;
}

int appendCopy(ModelSurface *surface, int source)
{
	const int index = surface->texCoords.size();
	const auto uv = surface->texCoords[source];
	surface->texCoords.append(uv);
	for (auto &frame : surface->frames)
	{
		const auto p = frame.positions[source], n = frame.normals[source];
		frame.positions.append(p);
		frame.normals.append(n);
	}
	surface->vertexCount = surface->texCoords.size();
	return index;
}

int appendBlend(ModelSurface *surface, const QVector<QPair<int, double>> &weights)
{
	double u = 0, v = 0;
	for (const auto &[vertex, weight] : weights)
	{
		u += weight * surface->texCoords[vertex].u;
		v += weight * surface->texCoords[vertex].v;
	}
	const int index = surface->texCoords.size();
	surface->texCoords.append({float(u), float(v)});
	for (auto &frame : surface->frames)
	{
		P3 p{0, 0, 0}, n{0, 0, 0};
		for (const auto &[vertex, weight] : weights)
		{
			p = p + point(frame.positions[vertex]) * weight;
			n = n + unit(point(frame.normals[vertex])) * weight;
		}
		const P3 normal = length(n) > 1e-9 ? unit(n) : unit(point(frame.normals[weights.first().first]));
		frame.positions.append(vec(p));
		frame.normals.append(vec(normal));
	}
	surface->vertexCount = surface->texCoords.size();
	return index;
}

int appendLerp(ModelSurface *surface, int a, int b, double t)
{
	return appendBlend(surface, {{a, 1.0 - t}, {b, t}});
}

bool reserveCapacity(const ModelMesh &mesh, int surface, qint64 addedVertices, qint64 addedTriangles, QString *error)
{
	qint64 vertices = addedVertices, triangles = addedTriangles;
	for (const auto &s : mesh.surfaces)
	{
		vertices += s.vertexCount;
		triangles += s.triangles.size();
	}
	const qint64 frames = std::max<qint64>(1, mesh.frames.size());
	if (surface < 0 || surface >= mesh.surfaces.size() || mesh.surfaces[surface].vertexCount + addedVertices > modelDocumentMaxVertices ||
		mesh.surfaces[surface].triangles.size() + addedTriangles > modelDocumentMaxTriangles || vertices > modelDocumentMaxVertices ||
		vertices * frames > modelDocumentMaxFrameVertices || triangles > modelDocumentMaxTriangles)
	{
		return fail(error, Text::tr("The edit would exceed the model's vertex, animation storage or triangle limit."));
	}
	return true;
}

QSet<int> usedVertices(const ModelSurface &surface)
{
	QSet<int> used;
	used.reserve(surface.vertexCount);
	for (const auto &t : surface.triangles)
	{
		used.insert(t.a);
		used.insert(t.b);
		used.insert(t.c);
	}
	return used;
}

QSet<ModelEdge> surfaceEdgeSet(const ModelSurface &surface)
{
	QSet<ModelEdge> edges;
	edges.reserve(surface.triangles.size() * 2);
	for (const auto &t : surface.triangles)
	{
		edges.insert(modelEdge(t.a, t.b));
		edges.insert(modelEdge(t.b, t.c));
		edges.insert(modelEdge(t.c, t.a));
	}
	return edges;
}

bool refreshNormals(ModelSurface *surface, const QSet<int> &vertices, ModelWorkProgress &work, int onlyFrame)
{
	if (vertices.isEmpty())
		return work.check();
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(*surface, &topology))
		return false;
	QHash<int, QVector<int>> indexFaces;
	QSet<int> groups;
	for (int vertex : vertices)
	{
		if (vertex >= 0 && vertex < surface->vertexCount)
			groups.insert(topology.group(vertex));
	}
	for (int face = 0; face < surface->triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface->triangles[face];
		for (int corner : {t.a, t.b, t.c})
		{
			if (vertices.contains(corner))
				indexFaces[corner].append(face);
		}
	}
	QVector<int> ordered = groups.values().toVector();
	std::sort(ordered.begin(), ordered.end());
	for (int frame = 0; frame < surface->frames.size(); ++frame)
	{
		if (onlyFrame >= 0 && frame != onlyFrame)
			continue;
		auto &normals = surface->frames[frame].normals;
		for (int group : std::as_const(ordered))
		{
			if (!work.step())
				return false;
			const auto &copies = topology.copies[group];
			if (copies.isEmpty())
				continue;
			const P3 first = unit(point(normals[copies.first()]));
			bool smooth = true;
			for (int copy : copies)
			{
				if (dot(unit(point(normals[copy])), first) < 0.9999)
					smooth = false;
			}
			if (smooth)
			{
				P3 sum{0, 0, 0};
				for (int face : topology.groupFaces[group])
					sum = sum + matchingCornerNormal(*surface, frame, surface->triangles[face],
													 [&](int v) { return topology.group(v) == group; });
				if (length(sum) > 1e-12)
				{
					const auto n = vec(unit(sum));
					for (int copy : copies)
						normals[copy] = n;
				}
				continue;
			}
			for (int copy : copies)
			{
				if (!vertices.contains(copy))
					continue;
				P3 sum{0, 0, 0};
				for (int face : indexFaces.value(copy))
					sum = sum + matchingCornerNormal(*surface, frame, surface->triangles[face], [&](int v) { return v == copy; });
				if (length(sum) > 1e-12)
					normals[copy] = vec(unit(sum));
			}
		}
	}
	return work.check();
}

void pruneSeams(ModelSurface *surface)
{
	if (surface->uvSeams.isEmpty())
		return;
	const auto edges = surfaceEdgeSet(*surface);
	for (auto it = surface->uvSeams.begin(); it != surface->uvSeams.end();)
	{
		if (!edges.contains(*it))
			it = surface->uvSeams.erase(it);
		else
			++it;
	}
}

bool compactUnused(ModelSurface *surface, const QSet<int> &candidates, ModelSelection *selection, ModelWorkProgress &work)
{
	const auto used = usedVertices(*surface);
	QVector<int> mapping(surface->vertexCount, -1);
	int next = 0;
	bool removed = false;
	for (int i = 0; i < surface->vertexCount; ++i)
	{
		if (candidates.contains(i) && !used.contains(i))
		{
			removed = true;
			continue;
		}
		mapping[i] = next++;
	}
	if (!removed)
		return work.check();
	QVector<ModelTexCoord> coords;
	coords.reserve(next);
	for (int i = 0; i < surface->vertexCount; ++i)
	{
		if (mapping[i] >= 0)
			coords.append(surface->texCoords[i]);
	}
	for (auto &frame : surface->frames)
	{
		if (!work.step())
			return false;
		QVector<ModelVec3> positions, normals;
		positions.reserve(next);
		normals.reserve(next);
		for (int i = 0; i < mapping.size(); ++i)
		{
			if (mapping[i] >= 0)
			{
				positions.append(frame.positions[i]);
				normals.append(frame.normals[i]);
			}
		}
		frame.positions = std::move(positions);
		frame.normals = std::move(normals);
	}
	for (auto &t : surface->triangles)
		t = {mapping[t.a], mapping[t.b], mapping[t.c]};
	QSet<ModelEdge> seams;
	for (auto seam : std::as_const(surface->uvSeams))
	{
		if (mapping[seam.first] >= 0 && mapping[seam.second] >= 0)
			seams.insert(modelEdge(mapping[seam.first], mapping[seam.second]));
	}
	surface->uvSeams = std::move(seams);
	surface->texCoords = std::move(coords);
	surface->vertexCount = next;
	if (selection)
	{
		QSet<int> vertices;
		for (int v : std::as_const(selection->vertices))
		{
			if (v >= 0 && v < mapping.size() && mapping[v] >= 0)
				vertices.insert(mapping[v]);
		}
		QSet<ModelEdge> edges;
		for (auto e : std::as_const(selection->edges))
		{
			if (e.first < mapping.size() && e.second < mapping.size() && mapping[e.first] >= 0 && mapping[e.second] >= 0)
				edges.insert(modelEdge(mapping[e.first], mapping[e.second]));
		}
		selection->vertices = std::move(vertices);
		selection->edges = std::move(edges);
	}
	return work.check();
}

QSet<int> selectedVertexSet(const ModelSurface &surface, const ModelSelection &selection)
{
	auto vertices = selection.vertices;
	for (auto edge : selection.edges)
	{
		vertices.insert(edge.first);
		vertices.insert(edge.second);
	}
	for (int face : selection.faces)
	{
		if (face < 0 || face >= surface.triangles.size())
			continue;
		const auto &t = surface.triangles[face];
		vertices.insert(t.a);
		vertices.insert(t.b);
		vertices.insert(t.c);
	}
	return vertices;
}

QSet<int> expandToCopies(const ModelGeometricTopology &topology, const QSet<int> &vertices)
{
	QSet<int> result;
	for (int vertex : vertices)
	{
		const int group = topology.group(vertex);
		if (group < 0)
			continue;
		for (int copy : topology.copies[group])
			result.insert(copy);
	}
	return result;
}

void removeDuplicateFaces(ModelSurface *surface, const QSet<int> &touchedVertices)
{
	std::map<std::array<int, 3>, int> seen;
	QVector<ModelTriangle> kept;
	kept.reserve(surface->triangles.size());
	for (const auto &t : std::as_const(surface->triangles))
	{
		if (touchedVertices.contains(t.a) || touchedVertices.contains(t.b) || touchedVertices.contains(t.c))
		{
			std::array<int, 3> key{t.a, t.b, t.c};
			std::sort(key.begin(), key.end());
			if (!seen.emplace(key, kept.size()).second)
				continue;
		}
		kept.append(t);
	}
	surface->triangles = std::move(kept);
}

bool triangulatePolygon(const ModelSurface &surface, int frame, const QVector<int> &polygon, P3 normal, QVector<ModelTriangle> *result)
{
	result->clear();
	const int count = polygon.size();
	if (count < 3 || count > 4096)
		return false;
	if (count == 3)
	{
		result->append({polygon[0], polygon[1], polygon[2]});
		return true;
	}
	normal = unit(normal);
	if (length(normal) <= 0)
		return false;
	// An orthonormal basis of the projection plane, right-handed about normal.
	const P3 helper = std::abs(normal.x) < 0.9 ? P3{1, 0, 0} : P3{0, 1, 0};
	const P3 u = unit(cross(helper, normal)), v = cross(normal, u);
	QVector<P2> points(count);
	QVector<P3> spatial(count);
	for (int i = 0; i < count; ++i)
	{
		spatial[i] = position(surface, frame, polygon[i]);
		points[i] = {dot(spatial[i], u), dot(spatial[i], v)};
	}
	if (area(points) <= 0)
		return false;
	double scale = 0;
	for (int i = 0; i < count; ++i)
		scale = std::max({scale, std::abs(points[i].x), std::abs(points[i].y)});
	const double epsilon = std::max(1e-12, scale * scale * 1e-12);
	QVector<int> remaining(count);
	std::iota(remaining.begin(), remaining.end(), 0);
	const auto inside = [&](int a, int b, int c, int p)
	{
		// Strictly inside or on the ear's open edges; coincident corners are ignored.
		const P2 pa = points[a], pb = points[b], pc = points[c], pp = points[p];
		const auto same = [](P2 x, P2 y) { return x.x == y.x && x.y == y.y; };
		if (same(pp, pa) || same(pp, pb) || same(pp, pc))
			return false;
		return orient(pa, pb, pp) >= -epsilon && orient(pb, pc, pp) >= -epsilon && orient(pc, pa, pp) >= -epsilon;
	};
	while (remaining.size() > 3)
	{
		int best = -1;
		double bestQuality = -1;
		const int size = remaining.size();
		for (int i = 0; i < size; ++i)
		{
			const int a = remaining[(i + size - 1) % size], b = remaining[i], c = remaining[(i + 1) % size];
			if (orient(points[a], points[b], points[c]) <= epsilon)
				continue;
			bool ear = true;
			for (int j = 0; j < size && ear; ++j)
			{
				const int p = remaining[j];
				if (p != a && p != b && p != c && inside(a, b, c, p))
					ear = false;
			}
			if (!ear)
				continue;
			const double quality = minimumAngle(spatial[a], spatial[b], spatial[c]);
			if (quality > bestQuality + 1e-12)
			{
				best = i;
				bestQuality = quality;
			}
		}
		if (best < 0)
			return false;
		const int a = remaining[(best + size - 1) % size], b = remaining[best], c = remaining[(best + 1) % size];
		result->append({polygon[a], polygon[b], polygon[c]});
		remaining.removeAt(best);
	}
	if (orient(points[remaining[0]], points[remaining[1]], points[remaining[2]]) <= epsilon)
		return false;
	result->append({polygon[remaining[0]], polygon[remaining[1]], polygon[remaining[2]]});
	return true;
}

bool splitSurfaceEdges(ModelSurface *surface, const ModelGeometricTopology &topology, const QHash<ModelEdge, double> &cuts, int frame,
					   QHash<ModelEdge, int> *created, QVector<int> *faceSource, ModelWorkProgress &work)
{
	QHash<ModelEdge, int> points;
	auto keys = cuts.keys();
	std::sort(keys.begin(), keys.end());
	for (auto edge : std::as_const(keys))
	{
		if (!work.step())
			return false;
		int a = edge.first, b = edge.second;
		if (topology.group(a) > topology.group(b))
			std::swap(a, b);
		points.insert(edge, appendLerp(surface, a, b, cuts.value(edge)));
	}
	const auto at = [&](int a, int b) { return points.value(modelEdge(a, b), -1); };
	QVector<ModelTriangle> triangles;
	QVector<int> sources;
	triangles.reserve(surface->triangles.size() + points.size() * 2);
	const auto addFace = [&](int source, int a, int b, int c)
	{
		triangles.append({a, b, c});
		sources.append(source);
	};
	for (int face = 0; face < surface->triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto t = surface->triangles[face];
		const int ab = at(t.a, t.b), bc = at(t.b, t.c), ca = at(t.c, t.a);
		const int count = (ab >= 0) + (bc >= 0) + (ca >= 0);
		if (count == 0)
		{
			addFace(face, t.a, t.b, t.c);
		}
		else if (count == 1)
		{
			// Rotate so the split edge runs x -> y.
			const int x = ab >= 0 ? t.a : bc >= 0 ? t.b : t.c, y = ab >= 0 ? t.b : bc >= 0 ? t.c : t.a,
					  z = ab >= 0 ? t.c : bc >= 0 ? t.a : t.b, m = ab >= 0 ? ab : bc >= 0 ? bc : ca;
			addFace(face, x, m, z);
			addFace(face, m, y, z);
		}
		else if (count == 2)
		{
			// Rotate so the split edges are x -> y and y -> z (tip y).
			int x, y, z, m1, m2;
			if (ab >= 0 && bc >= 0)
			{
				x = t.a, y = t.b, z = t.c, m1 = ab, m2 = bc;
			}
			else if (bc >= 0 && ca >= 0)
			{
				x = t.b, y = t.c, z = t.a, m1 = bc, m2 = ca;
			}
			else
			{
				x = t.c, y = t.a, z = t.b, m1 = ca, m2 = ab;
			}
			addFace(face, m1, y, m2);
			const P3 px = position(*surface, frame, x), pz = position(*surface, frame, z), p1 = position(*surface, frame, m1),
					 p2 = position(*surface, frame, m2);
			const double first = std::min(minimumAngle(px, p1, p2), minimumAngle(px, p2, pz));
			const double second = std::min(minimumAngle(px, p1, pz), minimumAngle(p1, p2, pz));
			if (first >= second)
			{
				addFace(face, x, m1, m2);
				addFace(face, x, m2, z);
			}
			else
			{
				addFace(face, x, m1, z);
				addFace(face, m1, m2, z);
			}
		}
		else
		{
			addFace(face, t.a, ab, ca);
			addFace(face, ab, t.b, bc);
			addFace(face, ca, bc, t.c);
			addFace(face, ab, bc, ca);
		}
	}
	for (auto it = points.cbegin(); it != points.cend(); ++it)
	{
		if (surface->uvSeams.remove(it.key()))
		{
			surface->uvSeams.insert(modelEdge(it.key().first, it.value()));
			surface->uvSeams.insert(modelEdge(it.value(), it.key().second));
		}
	}
	surface->triangles = std::move(triangles);
	if (created)
		*created = points;
	if (faceSource)
		*faceSource = std::move(sources);
	return work.check();
}

namespace
{
QVector<int> sortedValues(const QSet<int> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
QVector<ModelEdge> sortedEdges(const QSet<ModelEdge> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
using Directed = QHash<QPair<int, int>, int>;
Directed directedEdges(const ModelSurface &surface)
{
	Directed directed;
	directed.reserve(surface.triangles.size() * 3);
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto &t = surface.triangles[face];
		directed.insert({t.a, t.b}, face);
		directed.insert({t.b, t.c}, face);
		directed.insert({t.c, t.a}, face);
	}
	return directed;
}
void setFace(ModelSurface *surface, Directed *directed, int face, const ModelTriangle &t)
{
	const auto old = surface->triangles[face];
	for (const auto &edge : {qMakePair(old.a, old.b), qMakePair(old.b, old.c), qMakePair(old.c, old.a)})
	{
		if (directed->value(edge, -1) == face)
			directed->remove(edge);
	}
	surface->triangles[face] = t;
	directed->insert({t.a, t.b}, face);
	directed->insert({t.b, t.c}, face);
	directed->insert({t.c, t.a}, face);
}
// Corners of the two faces across an indexed edge as quad x, d, y, c, where
// the first face runs x -> y -> c and the second y -> x -> d.
struct EdgeQuad
{
	int first = -1, second = -1;
	int x = -1, y = -1, c = -1, d = -1;
};
bool edgeQuad(const ModelSurface &surface, const Directed &directed, ModelEdge edge, EdgeQuad *quad)
{
	int first = directed.value({edge.first, edge.second}, -1), second = directed.value({edge.second, edge.first}, -1);
	if (first < 0 || second < 0 || first == second)
		return false;
	quad->first = first;
	quad->second = second;
	quad->x = edge.first;
	quad->y = edge.second;
	const auto &f = surface.triangles[first], &g = surface.triangles[second];
	for (int v : {f.a, f.b, f.c})
	{
		if (v != edge.first && v != edge.second)
			quad->c = v;
	}
	for (int v : {g.a, g.b, g.c})
	{
		if (v != edge.first && v != edge.second)
			quad->d = v;
	}
	// Exactly two faces may use the edge.
	int uses = 0;
	for (const auto &t : surface.triangles)
	{
		const bool hasFirst = t.a == edge.first || t.b == edge.first || t.c == edge.first;
		const bool hasSecond = t.a == edge.second || t.b == edge.second || t.c == edge.second;
		uses += hasFirst && hasSecond;
		if (uses > 2)
			return false;
	}
	return uses == 2 && quad->c >= 0 && quad->d >= 0;
}
bool strictlyConvex(const std::array<P3, 4> &q)
{
	P3 axis{0, 0, 0};
	for (int i = 0; i < 4; ++i)
		axis = axis + cross(q[(i + 1) % 4] - q[i], q[(i + 2) % 4] - q[(i + 1) % 4]);
	axis = unit(axis);
	double scale = 0;
	for (int i = 0; i < 4; ++i)
		scale = std::max(scale, length(q[(i + 1) % 4] - q[i]));
	if (length(axis) <= 0 || scale <= 0)
		return false;
	for (int i = 0; i < 4; ++i)
	{
		const P3 previous = q[(i + 3) % 4] - q[i], next = q[(i + 1) % 4] - q[i];
		if (dot(cross(next, previous), axis) <= 1e-9 * scale * scale)
			return false;
	}
	return true;
}
} // namespace

bool rotateEdges(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	if (selection->edges.isEmpty() || !selection->faces.isEmpty() || !selection->vertices.isEmpty())
		return fail(error, Text::tr("Select one or more edges between two faces to rotate."));
	const int frame = edit.tool.referenceFrame;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	auto directed = directedEdges(surface);
	QSet<ModelEdge> rotated;
	QSet<int> touched;
	for (auto edge : sortedEdges(selection->edges))
	{
		if (!work.step())
			return false;
		EdgeQuad quad;
		if (!edgeQuad(surface, directed, edge, &quad) && !edgeQuad(surface, directed, {edge.second, edge.first}, &quad))
		{
			return fail(error, Text::tr("Edge %1–%2 cannot rotate: it needs exactly two consistently wound faces on one UV island.")
								   .arg(edge.first)
								   .arg(edge.second));
		}
		if (quad.c == quad.d || topology.group(quad.c) == topology.group(quad.d) || directed.contains({quad.c, quad.d}) ||
			directed.contains({quad.d, quad.c}) || topology.edgeFaces.contains(topology.geometricEdge(quad.c, quad.d)))
		{
			return fail(error, Text::tr("Edge %1–%2 cannot rotate: its opposite corners are already connected.").arg(edge.first).arg(edge.second));
		}
		const std::array<P3, 4> corners{position(surface, frame, quad.x), position(surface, frame, quad.d),
										position(surface, frame, quad.y), position(surface, frame, quad.c)};
		if (!strictlyConvex(corners))
		{
			return fail(error, Text::tr("Edge %1–%2 cannot rotate: its two faces do not form a convex quad in the reference pose.")
								   .arg(edge.first)
								   .arg(edge.second));
		}
		const ModelTriangle first{quad.x, quad.d, quad.c}, second{quad.d, quad.y, quad.c};
		if (collapsedInAnyFrame(surface, first) || collapsedInAnyFrame(surface, second))
		{
			return fail(error, Text::tr("Edge %1–%2 cannot rotate: a new face would collapse in an animation pose.").arg(edge.first).arg(edge.second));
		}
		setFace(&surface, &directed, quad.first, first);
		setFace(&surface, &directed, quad.second, second);
		surface.uvSeams.remove(modelEdge(quad.x, quad.y));
		rotated.remove(modelEdge(quad.x, quad.y));
		rotated.insert(modelEdge(quad.c, quad.d));
		touched << quad.x << quad.y << quad.c << quad.d;
	}
	if (!refreshNormals(&surface, touched, work))
		return false;
	selection->edges = rotated;
	return work.check();
}

bool pokeFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select at least one face to poke."));
	if (!reserveCapacity(*mesh, selection->surface, faces.size(), qint64(faces.size()) * 2, error))
		return false;
	auto &surface = mesh->surfaces[selection->surface];
	const double offset = edit.tool.offset;
	QSet<int> result, touched;
	for (int face : faces)
	{
		if (!work.step())
			return false;
		const auto t = surface.triangles[face];
		const int centre = appendBlend(&surface, {{t.a, 1.0 / 3}, {t.b, 1.0 / 3}, {t.c, 1.0 / 3}});
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			const P3 n = unit(faceCross(surface, frame, t));
			if (length(n) <= 0)
				continue;
			surface.frames[frame].normals[centre] = vec(n);
			if (offset != 0)
				surface.frames[frame].positions[centre] = vec(position(surface, frame, centre) + n * offset);
		}
		surface.triangles[face] = {t.a, t.b, centre};
		result << face << surface.triangles.size() << surface.triangles.size() + 1;
		surface.triangles.append({t.b, t.c, centre});
		surface.triangles.append({t.c, t.a, centre});
		if (offset != 0)
			touched << t.a << t.b << t.c << centre;
	}
	if (!refreshNormals(&surface, touched, work))
		return false;
	selection->faces = result;
	selection->vertices.clear();
	selection->edges.clear();
	return work.check();
}

bool beautifyFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	if (selection->faces.size() < 2)
		return fail(error, Text::tr("Select at least two neighbouring faces to beautify."));
	auto &surface = mesh->surfaces[selection->surface];
	const int frame = edit.tool.referenceFrame;
	const double maxAngle = edit.tool.maxAngle * std::numbers::pi / 180.0;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	auto directed = directedEdges(surface);
	int flips = 0;
	QSet<int> touched;
	for (int pass = 0; pass < 64; ++pass)
	{
		QSet<ModelEdge> candidates;
		for (int face : std::as_const(selection->faces))
		{
			const auto &t = surface.triangles[face];
			for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
				candidates.insert(edge);
		}
		QSet<int> changed;
		for (auto edge : sortedEdges(candidates))
		{
			if (!work.step())
				return false;
			if (surface.uvSeams.contains(edge))
				continue;
			EdgeQuad quad;
			if (!edgeQuad(surface, directed, edge, &quad) && !edgeQuad(surface, directed, {edge.second, edge.first}, &quad))
				continue;
			if (!selection->faces.contains(quad.first) || !selection->faces.contains(quad.second) || changed.contains(quad.first) ||
				changed.contains(quad.second))
				continue;
			if (topology.group(quad.c) == topology.group(quad.d) || directed.contains({quad.c, quad.d}) || directed.contains({quad.d, quad.c}) ||
				topology.edgeFaces.contains(topology.geometricEdge(quad.c, quad.d)))
				continue;
			const P3 x = position(surface, frame, quad.x), y = position(surface, frame, quad.y), c = position(surface, frame, quad.c),
					 d = position(surface, frame, quad.d);
			const P3 n1 = cross(y - x, c - x), n2 = cross(x - y, d - y);
			if (length(n1) <= 0 || length(n2) <= 0 ||
				std::acos(std::clamp(dot(unit(n1), unit(n2)), -1.0, 1.0)) > maxAngle + 1e-12 || !strictlyConvex({x, d, y, c}))
				continue;
			const double before = std::min(minimumAngle(x, y, c), minimumAngle(y, x, d));
			const double after = std::min(minimumAngle(x, d, c), minimumAngle(d, y, c));
			if (after <= before + 1e-9)
				continue;
			const ModelTriangle first{quad.x, quad.d, quad.c}, second{quad.d, quad.y, quad.c};
			if (collapsedInAnyFrame(surface, first) || collapsedInAnyFrame(surface, second))
				continue;
			setFace(&surface, &directed, quad.first, first);
			setFace(&surface, &directed, quad.second, second);
			changed << quad.first << quad.second;
			touched << quad.x << quad.y << quad.c << quad.d;
			++flips;
		}
		if (changed.isEmpty())
			break;
	}
	if (flips == 0)
		return fail(error, Text::tr("The selected faces are already as even as their shape allows."));
	if (!refreshNormals(&surface, touched, work))
		return false;
	return work.check();
}

bool makeFace(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	if (!selection->faces.isEmpty())
		return fail(error, Text::tr("Make Face uses selected vertices or edges, not faces."));
	auto vertices = selection->vertices;
	for (auto edge : std::as_const(selection->edges))
		vertices << edge.first << edge.second;
	if (vertices.size() != 3 && vertices.size() != 4)
		return fail(error, Text::tr("Select three or four vertices (or two edges) to make a face. Use Fill Boundary Loops for larger holes."));
	if (!reserveCapacity(*mesh, selection->surface, 0, 2, error))
		return false;
	const int frame = edit.tool.referenceFrame;
	auto corners = sortedValues(vertices);
	const auto at = [&](int v) { return position(surface, frame, v); };
	if (corners.size() == 4)
	{
		// The cyclic order with the largest enclosed area does not cross itself.
		const std::array<std::array<int, 4>, 3> orders{{{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}}};
		double best = -1;
		QVector<int> chosen;
		for (const auto &order : orders)
		{
			P3 newell{0, 0, 0};
			for (int i = 0; i < 4; ++i)
				newell = newell + cross(at(corners[order[i]]), at(corners[order[(i + 1) % 4]]));
			if (length(newell) > best + 1e-12)
			{
				best = length(newell);
				chosen = {corners[order[0]], corners[order[1]], corners[order[2]], corners[order[3]]};
			}
		}
		corners = chosen;
	}
	QVector<ModelTriangle> faces;
	if (corners.size() == 3)
	{
		faces.append({corners[0], corners[1], corners[2]});
	}
	else
	{
		// Split along the shorter diagonal.
		if (length(at(corners[2]) - at(corners[0])) <= length(at(corners[3]) - at(corners[1])))
			faces << ModelTriangle{corners[0], corners[1], corners[2]} << ModelTriangle{corners[0], corners[2], corners[3]};
		else
			faces << ModelTriangle{corners[0], corners[1], corners[3]} << ModelTriangle{corners[1], corners[2], corners[3]};
	}
	const auto directed = directedEdges(surface);
	const auto conflicts = [&](const QVector<ModelTriangle> &candidate)
	{
		int count = 0;
		for (const auto &t : candidate)
		{
			for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
				count += directed.contains(edge);
		}
		return count;
	};
	auto reversed = faces;
	for (auto &t : reversed)
		std::swap(t.b, t.c);
	const int forward = conflicts(faces), backward = conflicts(reversed);
	bool adjacent = false;
	for (const auto &t : faces)
	{
		for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			adjacent |= directed.contains(edge) || directed.contains({edge.second, edge.first});
	}
	if (forward > 0 && backward > 0)
		return fail(error, Text::tr("The new face would share an edge with two faces. Check the selection for existing faces."));
	if (forward > 0)
	{
		faces = reversed;
	}
	else if (backward == 0 && !adjacent)
	{
		// No neighbour fixes the winding: face the selected vertices' normals.
		P3 normals{0, 0, 0}, face{0, 0, 0};
		for (int v : std::as_const(corners))
			normals = normals + unit(point(surface.frames[frame].normals[v]));
		for (const auto &t : std::as_const(faces))
			face = face + faceCross(surface, frame, t);
		if (dot(normals, face) < 0)
			faces = reversed;
	}
	std::map<std::array<int, 3>, int> existing;
	for (const auto &t : std::as_const(surface.triangles))
	{
		std::array<int, 3> key{t.a, t.b, t.c};
		std::sort(key.begin(), key.end());
		existing.emplace(key, 0);
	}
	QSet<int> created;
	for (const auto &t : std::as_const(faces))
	{
		std::array<int, 3> key{t.a, t.b, t.c};
		std::sort(key.begin(), key.end());
		if (existing.count(key))
			return fail(error, Text::tr("A face already uses these vertices."));
		if (collapsedInAnyFrame(surface, t))
			return fail(error, Text::tr("The selected vertices are collinear in an animation pose; the face would collapse."));
		created.insert(surface.triangles.size());
		surface.triangles.append(t);
	}
	if (!refreshNormals(&surface, vertices, work))
		return false;
	selection->vertices.clear();
	selection->edges.clear();
	selection->faces = created;
	return work.check();
}

bool mergeVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto selected = expandToCopies(topology, selectedVertexSet(surface, *selection));
	QSet<int> selectedGroups;
	for (int v : selected)
		selectedGroups.insert(topology.group(v));
	if (selectedGroups.size() < 2)
		return fail(error, Text::tr("Select at least two separate vertices to merge."));
	const auto mode = edit.tool.merge;
	const int frame = edit.tool.referenceFrame;
	// Union-find helpers.
	QHash<int, int> parent;
	std::function<int(int)> find = [&](int v)
	{
		int root = v;
		while (parent.value(root, root) != root)
			root = parent.value(root, root);
		while (v != root)
		{
			const int next = parent.value(v, v);
			parent.insert(v, root);
			v = next;
		}
		return root;
	};
	const auto unite = [&](int a, int b)
	{
		a = find(a);
		b = find(b);
		if (a != b)
			parent.insert(std::max(a, b), std::min(a, b));
	};
	// Merge groups: one group, or connected groups for Collapse.
	QHash<int, int> mergeGroup;
	if (mode == ModelMergeMode::Collapse)
	{
		parent.clear();
		for (int v : selected)
			unite(v, topology.group(v));
		for (const auto &t : std::as_const(surface.triangles))
		{
			for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				if (selected.contains(edge.first) && selected.contains(edge.second))
					unite(edge.first, edge.second);
			}
		}
		for (int v : selected)
			mergeGroup.insert(v, find(v));
		parent.clear();
	}
	else
	{
		const int root = *std::min_element(selected.cbegin(), selected.cend());
		for (int v : selected)
			mergeGroup.insert(v, root);
	}
	QHash<int, QVector<int>> members;
	for (int v : sortedValues(selected))
		members[mergeGroup.value(v)].append(v);
	// Move every member to its group's target in each pose.
	int merged = 0;
	for (auto it = members.cbegin(); it != members.cend(); ++it)
	{
		QSet<int> groups;
		for (int v : it.value())
			groups.insert(topology.group(v));
		if (groups.size() < 2)
			continue;
		++merged;
		const auto centreIn = [&](int f)
		{
			P3 sum{0, 0, 0};
			for (int group : groups)
				sum = sum + position(surface, f, group);
			return sum * (1.0 / groups.size());
		};
		const P3 reference = centreIn(frame);
		for (int f = 0; f < surface.frames.size(); ++f)
		{
			if (!work.step())
				return false;
			P3 target = centreIn(f);
			if (mode == ModelMergeMode::Point)
				target = arrayPoint(edit.tool.point) + (target - reference);
			for (int v : it.value())
				surface.frames[f].positions[v] = vec(target);
		}
	}
	if (merged == 0)
		return fail(error, Text::tr("The selected vertices already coincide. Select vertices at different positions to merge."));
	// Members connected by an indexed edge share one UV and become one index;
	// seam copies that are not connected stay separate, keeping their UVs.
	parent.clear();
	for (const auto &t : std::as_const(surface.triangles))
	{
		for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			if (selected.contains(edge.first) && selected.contains(edge.second) &&
				mergeGroup.value(edge.first) == mergeGroup.value(edge.second))
				unite(edge.first, edge.second);
		}
	}
	QHash<int, int> mapping;
	QHash<int, QVector<int>> uvComponents;
	for (int v : sortedValues(selected))
		uvComponents[find(v)].append(v);
	QSet<int> representatives;
	for (auto it = uvComponents.cbegin(); it != uvComponents.cend(); ++it)
	{
		const int representative = it.value().first();
		representatives.insert(representative);
		double u = 0, v = 0;
		for (int member : it.value())
		{
			mapping.insert(member, representative);
			u += surface.texCoords[member].u;
			v += surface.texCoords[member].v;
		}
		surface.texCoords[representative] = {float(u / it.value().size()), float(v / it.value().size())};
	}
	QVector<ModelTriangle> triangles;
	triangles.reserve(surface.triangles.size());
	for (const auto &t : std::as_const(surface.triangles))
	{
		if (!work.step())
			return false;
		const ModelTriangle mapped{mapping.value(t.a, t.a), mapping.value(t.b, t.b), mapping.value(t.c, t.c)};
		// Two corners in one merge group now coincide; the face collapses.
		const auto groupOf = [&](int v) { return selected.contains(v) ? mergeGroup.value(v) : -1 - v; };
		if (groupOf(t.a) == groupOf(t.b) || groupOf(t.b) == groupOf(t.c) || groupOf(t.c) == groupOf(t.a))
			continue;
		triangles.append(mapped);
	}
	surface.triangles = std::move(triangles);
	removeDuplicateFaces(&surface, representatives);
	if (surface.triangles.isEmpty())
		return fail(error, Text::tr("Merging would remove every face of the surface."));
	QSet<ModelEdge> seams;
	for (auto seam : std::as_const(surface.uvSeams))
	{
		const int a = mapping.value(seam.first, seam.first), b = mapping.value(seam.second, seam.second);
		if (a != b)
			seams.insert(modelEdge(a, b));
	}
	surface.uvSeams = std::move(seams);
	pruneSeams(&surface);
	selection->faces.clear();
	selection->edges.clear();
	selection->vertices = representatives;
	if (!compactUnused(&surface, selected, selection, work))
		return false;
	QSet<int> around = selection->vertices;
	for (const auto &t : std::as_const(surface.triangles))
	{
		if (selection->vertices.contains(t.a) || selection->vertices.contains(t.b) || selection->vertices.contains(t.c))
			around << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, around, work);
}

bool dissolveVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto targets = expandToCopies(topology, selection->vertices.isEmpty() ? selectedVertexSet(surface, *selection) : selection->vertices);
	if (targets.isEmpty())
		return fail(error, Text::tr("Select at least one vertex to dissolve."));
	const int frame = edit.tool.referenceFrame;
	QVector<bool> removed(surface.triangles.size(), false);
	QVector<ModelTriangle> added;
	std::map<std::array<int, 3>, int> existing;
	const auto key = [](const ModelTriangle &t)
	{
		std::array<int, 3> k{t.a, t.b, t.c};
		std::sort(k.begin(), k.end());
		return k;
	};
	for (const auto &t : std::as_const(surface.triangles))
		existing[key(t)]++;
	for (int vertex : sortedValues(targets))
	{
		if (!work.step())
			return false;
		QVector<int> fan;
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			if (!removed[face])
			{
				const auto &t = surface.triangles[face];
				if (t.a == vertex || t.b == vertex || t.c == vertex)
					fan.append(face);
			}
		}
		QVector<int> addedFan;
		for (int i = 0; i < added.size(); ++i)
		{
			const auto &t = added[i];
			if (t.a == vertex || t.b == vertex || t.c == vertex)
				addedFan.append(i);
		}
		if (fan.isEmpty() && addedFan.isEmpty())
			continue;
		QHash<int, int> next;
		QSet<int> hasPrevious;
		P3 normal{0, 0, 0};
		const auto visit = [&](const ModelTriangle &t)
		{
			const int x = t.a == vertex ? t.b : t.b == vertex ? t.c : t.a;
			const int y = t.a == vertex ? t.c : t.b == vertex ? t.a : t.b;
			if (next.contains(x) || hasPrevious.contains(y))
				return false;
			next.insert(x, y);
			hasPrevious.insert(y);
			normal = normal + faceCross(surface, frame, t);
			return true;
		};
		for (int face : std::as_const(fan))
		{
			if (!visit(surface.triangles[face]))
				return fail(error, Text::tr("Vertex %1 is not manifold and cannot be dissolved.").arg(vertex));
		}
		for (int index : std::as_const(addedFan))
		{
			if (!visit(added[index]))
				return fail(error, Text::tr("Vertex %1 is not manifold and cannot be dissolved.").arg(vertex));
		}
		int start = -1;
		for (auto it = next.cbegin(); it != next.cend(); ++it)
		{
			if (!hasPrevious.contains(it.key()) && (start < 0 || it.key() < start))
				start = it.key();
		}
		const bool closed = start < 0;
		if (closed)
		{
			auto keys = next.keys();
			start = *std::min_element(keys.cbegin(), keys.cend());
		}
		QVector<int> polygon{start};
		int current = start;
		while (next.contains(current))
		{
			current = next.value(current);
			if (current == start)
				break;
			polygon.append(current);
			if (polygon.size() > next.size() + 1)
				break;
		}
		if ((closed && polygon.size() != next.size()) || (!closed && polygon.size() != next.size() + 1))
			return fail(error, Text::tr("Vertex %1 has more than one fan of faces and cannot be dissolved.").arg(vertex));
		QVector<ModelTriangle> fill;
		if (polygon.size() >= 3 && !triangulatePolygon(surface, frame, polygon, normal, &fill))
			return fail(error, Text::tr("Vertex %1 cannot be dissolved without folding its neighbouring faces.").arg(vertex));
		for (const auto &t : std::as_const(fill))
		{
			if (existing.count(key(t)) && existing[key(t)] > 0)
				return fail(error, Text::tr("Dissolving vertex %1 would duplicate an existing face.").arg(vertex));
			if (collapsedInAnyFrame(surface, t))
				return fail(error, Text::tr("Dissolving vertex %1 would collapse a face in an animation pose.").arg(vertex));
		}
		for (int face : std::as_const(fan))
		{
			removed[face] = true;
			existing[key(surface.triangles[face])]--;
		}
		for (int i = addedFan.size() - 1; i >= 0; --i)
		{
			existing[key(added[addedFan[i]])]--;
			added.removeAt(addedFan[i]);
		}
		for (const auto &t : std::as_const(fill))
		{
			added.append(t);
			existing[key(t)]++;
		}
	}
	QVector<ModelTriangle> triangles;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!removed[face])
			triangles.append(surface.triangles[face]);
	}
	const int firstAdded = triangles.size();
	triangles += added;
	if (triangles.isEmpty())
		return fail(error, Text::tr("Dissolving would remove every face of the surface."));
	surface.triangles = std::move(triangles);
	pruneSeams(&surface);
	selection->vertices.clear();
	selection->edges.clear();
	selection->faces.clear();
	for (int face = firstAdded; face < surface.triangles.size(); ++face)
		selection->faces.insert(face);
	if (!compactUnused(&surface, targets, selection, work))
		return false;
	// Compaction renumbers vertices; refresh around the new faces.
	QSet<int> remaining;
	for (int face : std::as_const(selection->faces))
	{
		const auto &t = surface.triangles[face];
		remaining << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, remaining, work);
}

bool dissolveFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select the faces to dissolve into one polygon."));
	const int frame = edit.tool.referenceFrame;
	QHash<ModelEdge, QVector<int>> uses;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto &t = surface.triangles[face];
		for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			uses[edge].append(face);
	}
	// Regions connect through indexed edges, so each keeps one UV island.
	QHash<int, int> region;
	int regions = 0;
	for (int seed : faces)
	{
		if (region.contains(seed))
			continue;
		QVector<int> stack{seed};
		region.insert(seed, regions);
		while (!stack.isEmpty())
		{
			const int face = stack.takeLast();
			const auto &t = surface.triangles[face];
			for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
			{
				for (int other : uses.value(edge))
				{
					if (selection->faces.contains(other) && !region.contains(other))
					{
						region.insert(other, regions);
						stack.append(other);
					}
				}
			}
		}
		++regions;
	}
	QVector<bool> removed(surface.triangles.size(), false);
	QVector<ModelTriangle> added;
	QSet<int> interior, border;
	for (int r = 0; r < regions; ++r)
	{
		if (!work.step())
			return false;
		QVector<int> members;
		for (int face : faces)
		{
			if (region.value(face) == r)
				members.append(face);
		}
		QHash<int, int> next;
		QSet<int> regionVertices;
		P3 normal{0, 0, 0};
		for (int face : std::as_const(members))
		{
			const auto &t = surface.triangles[face];
			normal = normal + faceCross(surface, frame, t);
			regionVertices << t.a << t.b << t.c;
			for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				const auto edgeUses = uses.value(modelEdge(edge.first, edge.second));
				if (edgeUses.size() > 2)
					return fail(error, Text::tr("Dissolve Faces needs manifold regions; edge %1–%2 has more than two faces.")
										   .arg(edge.first)
										   .arg(edge.second));
				int inRegion = 0;
				for (int other : edgeUses)
					inRegion += region.value(other, -1) == r;
				if (inRegion == 1)
				{
					if (next.contains(edge.first))
						return fail(error, Text::tr("Dissolve Faces needs regions without pinched corners."));
					next.insert(edge.first, edge.second);
				}
			}
		}
		if (next.isEmpty())
			return fail(error, Text::tr("A selected region is closed; dissolving needs an open border."));
		auto keys = next.keys();
		const int start = *std::min_element(keys.cbegin(), keys.cend());
		QVector<int> polygon{start};
		int current = next.value(start);
		while (current != start && polygon.size() <= next.size())
		{
			polygon.append(current);
			current = next.value(current, start);
		}
		if (polygon.size() != next.size())
			return fail(error, Text::tr("Dissolve Faces needs each selected region to have one border without holes."));
		QSet<int> borderSet(polygon.cbegin(), polygon.cend());
		for (int v : std::as_const(regionVertices))
		{
			if (borderSet.contains(v))
				continue;
			for (int face = 0; face < surface.triangles.size(); ++face)
			{
				const auto &t = surface.triangles[face];
				if ((t.a == v || t.b == v || t.c == v) && region.value(face, -1) != r)
					return fail(error, Text::tr("Vertex %1 inside the region is shared with other faces.").arg(v));
			}
			interior.insert(v);
		}
		QVector<ModelTriangle> fill;
		if (!triangulatePolygon(surface, frame, polygon, normal, &fill))
			return fail(error, Text::tr("The region's border folds over itself in the reference pose and cannot become one polygon."));
		for (const auto &t : std::as_const(fill))
		{
			if (collapsedInAnyFrame(surface, t))
				return fail(error, Text::tr("A dissolved face would collapse in an animation pose."));
		}
		for (int face : std::as_const(members))
			removed[face] = true;
		added += fill;
		border += borderSet;
	}
	QVector<ModelTriangle> triangles;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!removed[face])
			triangles.append(surface.triangles[face]);
	}
	const int firstAdded = triangles.size();
	triangles += added;
	surface.triangles = std::move(triangles);
	pruneSeams(&surface);
	selection->vertices.clear();
	selection->edges.clear();
	selection->faces.clear();
	for (int face = firstAdded; face < surface.triangles.size(); ++face)
		selection->faces.insert(face);
	if (!compactUnused(&surface, interior, selection, work))
		return false;
	QSet<int> refresh;
	for (int face : std::as_const(selection->faces))
	{
		const auto &t = surface.triangles[face];
		refresh << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, refresh, work);
}

bool extrudeEdges(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	if (selection->edges.isEmpty() || !selection->faces.isEmpty())
		return fail(error, Text::tr("Select open boundary edges to extrude."));
	ModelTransformBasis axes;
	if (!resolveModelTransformAxes(*mesh, edit, &axes, error))
		return false;
	const auto offset = point(modelBasisToWorld(edit.translation, axes));
	if (length(offset) < 1e-6)
		return fail(error, Text::tr("Extrusion requires a nonzero offset."));
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto directed = directedEdges(surface);
	QVector<QPair<int, int>> borders;
	QSet<int> endpoints;
	for (auto edge : sortedEdges(selection->edges))
	{
		const bool forward = directed.contains({edge.first, edge.second}), backward = directed.contains({edge.second, edge.first});
		if (forward == backward || !topology.boundary(topology.geometricEdge(edge.first, edge.second)))
			return fail(error, Text::tr("Edge %1–%2 is not an open boundary. Extrude Edges works on borders; use Extrude Faces for closed surfaces.")
								   .arg(edge.first)
								   .arg(edge.second));
		borders.append(forward ? qMakePair(edge.first, edge.second) : qMakePair(edge.second, edge.first));
		endpoints << edge.first << edge.second;
	}
	if (!reserveCapacity(*mesh, selection->surface, endpoints.size(), qint64(borders.size()) * 2, error))
		return false;
	QHash<int, int> copies;
	for (int v : sortedValues(endpoints))
	{
		const int copy = appendCopy(&surface, v);
		for (auto &frame : surface.frames)
			frame.positions[copy] = vec(point(frame.positions[copy]) + offset);
		copies.insert(v, copy);
	}
	QSet<ModelEdge> result;
	QSet<int> touched = endpoints;
	for (const auto &[x, y] : std::as_const(borders))
	{
		if (!work.step())
			return false;
		const int xc = copies.value(x), yc = copies.value(y);
		surface.triangles.append({y, x, xc});
		surface.triangles.append({y, xc, yc});
		result.insert(modelEdge(xc, yc));
		touched << xc << yc;
	}
	if (!refreshNormals(&surface, touched, work))
		return false;
	selection->edges = result;
	selection->vertices.clear();
	return work.check();
}
} // namespace model_tools

double modelFalloffWeight(ModelFalloff falloff, double distance)
{
	if (!std::isfinite(distance) || distance >= 1)
		return 0;
	const double f = 1 - std::max(0.0, distance);
	switch (falloff)
	{
	case ModelFalloff::Smooth:
		return 3 * f * f - 2 * f * f * f;
	case ModelFalloff::Sphere:
		return std::sqrt(std::max(0.0, 2 * f - f * f));
	case ModelFalloff::Root:
		return std::sqrt(f);
	case ModelFalloff::InverseSquare:
		return f * (2 - f);
	case ModelFalloff::Sharp:
		return f * f;
	case ModelFalloff::Linear:
		return f;
	case ModelFalloff::Constant:
		return 1;
	}
	return f;
}

QString modelPrimitiveName(ModelPrimitive primitive)
{
	switch (primitive)
	{
	case ModelPrimitive::Plane:
		return model_tools::Text::tr("Plane");
	case ModelPrimitive::Cube:
		return model_tools::Text::tr("Cube");
	case ModelPrimitive::Circle:
		return model_tools::Text::tr("Circle");
	case ModelPrimitive::Grid:
		return model_tools::Text::tr("Grid");
	case ModelPrimitive::Cylinder:
		return model_tools::Text::tr("Cylinder");
	case ModelPrimitive::Cone:
		return model_tools::Text::tr("Cone");
	case ModelPrimitive::UvSphere:
		return model_tools::Text::tr("UV Sphere");
	case ModelPrimitive::IcoSphere:
		return model_tools::Text::tr("Ico Sphere");
	case ModelPrimitive::Torus:
		return model_tools::Text::tr("Torus");
	}
	return {};
}

bool isModelMeshToolEdit(ModelEditKind kind)
{
	switch (kind)
	{
	case ModelEditKind::RotateEdges:
	case ModelEditKind::MergeVertices:
	case ModelEditKind::DissolveVertices:
	case ModelEditKind::DissolveFaces:
	case ModelEditKind::PokeFaces:
	case ModelEditKind::BeautifyFaces:
	case ModelEditKind::MakeFace:
	case ModelEditKind::ExtrudeEdges:
	case ModelEditKind::InsetFaces:
	case ModelEditKind::ShrinkFatten:
	case ModelEditKind::SmoothVertices:
	case ModelEditKind::WeightedTransform:
	case ModelEditKind::ShadeFlat:
	case ModelEditKind::ShadeSmooth:
	case ModelEditKind::ShadeAutoSmooth:
	case ModelEditKind::Bisect:
	case ModelEditKind::Symmetrize:
	case ModelEditKind::LoopCut:
	case ModelEditKind::AddPrimitive:
	case ModelEditKind::Decimate:
	case ModelEditKind::BevelVertices:
	case ModelEditKind::Solidify:
	case ModelEditKind::ProjectUvMapping:
		return true;
	default:
		return false;
	}
}

bool isModelMeshShapeEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::ShrinkFatten || kind == ModelEditKind::SmoothVertices || kind == ModelEditKind::WeightedTransform;
}

bool applyModelMeshToolEdit(ModelMesh *candidate, const ModelEdit &edit, ModelSelection *selection, QString *error,
							const ModelWorkControl &control)
{
	using namespace model_tools;
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!candidate || !selection || selection->surface < 0 || selection->surface >= candidate->surfaces.size())
		return fail(error, Text::tr("Select a valid surface."));
	const auto &options = edit.tool;
	if (options.referenceFrame < 0 || options.referenceFrame >= candidate->frames.size())
		return fail(error, Text::tr("Choose an existing reference pose."));
	if (!isModelMeshShapeEdit(edit.kind) && edit.kind != ModelEditKind::ShadeFlat && edit.kind != ModelEditKind::ShadeSmooth &&
		edit.kind != ModelEditKind::ShadeAutoSmooth && edit.frame >= 0)
		return fail(error, Text::tr("This tool changes topology in every animation pose. Use the All frames scope."));
	const auto finiteIn = [](double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; };
	for (const auto &values : {options.point, options.normal, options.size, options.uvAxisU, options.uvAxisV})
	{
		for (double value : values)
		{
			if (!finiteIn(value, -1000000, 1000000))
				return fail(error, Text::tr("Tool coordinates must be finite and within ±1,000,000 units."));
		}
	}
	if (!finiteIn(options.insetThickness, 0, 1000000) || !finiteIn(options.insetDepth, -1000000, 1000000) ||
		!finiteIn(options.offset, -1000000, 1000000) || !finiteIn(options.smoothFactor, 0, 1) || options.iterations < 1 ||
		options.iterations > 100 || options.cuts < 1 || options.cuts > 64 || !finiteIn(options.slide, -1, 1) || options.axis < 0 ||
		options.axis > 2 || !finiteIn(options.mergeThreshold, 0, 1000) || options.segments < 3 || options.segments > 256 ||
		options.rings < 1 || options.rings > 256 || !finiteIn(options.maxAngle, 0, 180) || !finiteIn(options.smoothAngle, 0, 180) ||
		!finiteIn(options.proportionalRadius, 0, 1000000) || options.mirrorAxes < 0 || options.mirrorAxes > 7 ||
		!finiteIn(options.ratio, 0, 1) || options.targetTriangles < 0 || !finiteIn(options.bevelWidth, 0, 1000000) ||
		!finiteIn(options.solidifyThickness, -1000000, 1000000) || !finiteIn(options.uvTileSize, 0.001, 1000000))
		return fail(error, Text::tr("A tool setting is outside its supported range."));
	switch (edit.kind)
	{
	case ModelEditKind::RotateEdges:
		return rotateEdges(candidate, edit, selection, error, work);
	case ModelEditKind::MergeVertices:
		return mergeVertices(candidate, edit, selection, error, work);
	case ModelEditKind::DissolveVertices:
		return dissolveVertices(candidate, edit, selection, error, work);
	case ModelEditKind::DissolveFaces:
		return dissolveFaces(candidate, edit, selection, error, work);
	case ModelEditKind::PokeFaces:
		return pokeFaces(candidate, edit, selection, error, work);
	case ModelEditKind::BeautifyFaces:
		return beautifyFaces(candidate, edit, selection, error, work);
	case ModelEditKind::MakeFace:
		return makeFace(candidate, edit, selection, error, work);
	case ModelEditKind::ExtrudeEdges:
		return extrudeEdges(candidate, edit, selection, error, work);
	case ModelEditKind::InsetFaces:
		return insetFaces(candidate, edit, selection, error, work);
	case ModelEditKind::ShrinkFatten:
		return shrinkFatten(candidate, edit, selection, error, work);
	case ModelEditKind::SmoothVertices:
		return smoothVertices(candidate, edit, selection, error, work);
	case ModelEditKind::WeightedTransform:
		return weightedTransform(candidate, edit, selection, error, work, control);
	case ModelEditKind::ShadeFlat:
	case ModelEditKind::ShadeSmooth:
	case ModelEditKind::ShadeAutoSmooth:
		return shadeFaces(candidate, edit, selection, error, work);
	case ModelEditKind::Bisect:
		return bisectFaces(candidate, edit, selection, error, work);
	case ModelEditKind::Symmetrize:
		return symmetrize(candidate, edit, selection, error, work);
	case ModelEditKind::LoopCut:
		return loopCut(candidate, edit, selection, error, work);
	case ModelEditKind::AddPrimitive:
		return addPrimitive(candidate, edit, selection, error, work);
	case ModelEditKind::Decimate:
		return decimate(candidate, edit, selection, error, work);
	case ModelEditKind::BevelVertices:
		return bevelVertices(candidate, edit, selection, error, work);
	case ModelEditKind::Solidify:
		return solidify(candidate, edit, selection, error, work);
	case ModelEditKind::ProjectUvMapping:
		return projectUvs(candidate, edit, selection, error, work);
	default:
		return fail(error, Text::tr("Unknown mesh tool."));
	}
}
} // namespace vibestudio
