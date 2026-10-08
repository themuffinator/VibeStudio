#include "core/model_selection_tools.h"

#include "core/model_geometric_topology.h"
#include "core/model_geometry_helpers.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <queue>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSelectionTools)
};
using namespace model_geometry;
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
P3 unitVector(P3 p)
{
	const double l = length(p);
	return l > 1e-300 ? p * (1.0 / l) : P3{0, 0, 0};
}
double angleDegrees(P3 a, P3 b)
{
	a = unitVector(a);
	b = unitVector(b);
	if (length(a) <= 0 || length(b) <= 0)
		return 180;
	return std::acos(std::clamp(dot(a, b), -1.0, 1.0)) * 180.0 / std::numbers::pi;
}
// Shared lookups for one surface in one pose.
struct Context
{
	const ModelSurface &surface;
	const ModelGeometricTopology &topology;
	int frame;
	QHash<ModelEdge, QVector<ModelEdge>> indexEdges; // geometric edge -> indexed edges
	QVector<ModelEdge> allIndexEdges;
	Context(const ModelSurface &s, const ModelGeometricTopology &t, int f) : surface(s), topology(t), frame(f)
	{
		QSet<ModelEdge> seen;
		for (const auto &tri : surface.triangles)
		{
			for (auto e : {modelEdge(tri.a, tri.b), modelEdge(tri.b, tri.c), modelEdge(tri.c, tri.a)})
			{
				if (seen.contains(e))
					continue;
				seen.insert(e);
				allIndexEdges.append(e);
				indexEdges[topology.geometricEdge(e.first, e.second)].append(e);
			}
		}
		std::sort(allIndexEdges.begin(), allIndexEdges.end());
	}
	P3 position(int v) const { return point(surface.frames[frame].positions[v]); }
	P3 faceCross(int face) const
	{
		const auto &t = surface.triangles[face];
		const P3 a = position(t.a);
		return cross(position(t.b) - a, position(t.c) - a);
	}
	P3 centroid(int face) const
	{
		const auto &t = surface.triangles[face];
		return (position(t.a) + position(t.b) + position(t.c)) * (1.0 / 3);
	}
	std::array<int, 3> groups(int face) const
	{
		const auto &t = surface.triangles[face];
		return {topology.group(t.a), topology.group(t.b), topology.group(t.c)};
	}
	P3 groupNormal(int group) const
	{
		P3 sum{0, 0, 0};
		for (int face : topology.groupFaces[group])
			sum = sum + faceCross(face);
		return unitVector(sum);
	}
	double scale() const
	{
		double result = 1;
		for (const auto &p : surface.frames[frame].positions)
			result = std::max({result, double(std::abs(p.x)), double(std::abs(p.y)), double(std::abs(p.z))});
		return result;
	}
};

// Selection in mode-neutral form: geometric vertex groups, geometric edges, faces.
QSet<int> groupsOf(const Context &c, const ModelSelection &selection, ModelSelectionMode mode)
{
	QSet<int> groups;
	if (mode == ModelSelectionMode::Vertices)
	{
		for (int v : selection.vertices)
			groups.insert(c.topology.group(v));
	}
	else if (mode == ModelSelectionMode::Edges)
	{
		for (auto e : selection.edges)
			groups << c.topology.group(e.first) << c.topology.group(e.second);
	}
	else
	{
		for (int face : selection.faces)
		{
			const auto g = c.groups(face);
			groups << g[0] << g[1] << g[2];
		}
	}
	groups.remove(-1);
	return groups;
}
QSet<ModelEdge> geometricEdgesOf(const Context &c, const ModelSelection &selection)
{
	QSet<ModelEdge> edges;
	for (auto e : selection.edges)
		edges.insert(c.topology.geometricEdge(e.first, e.second));
	return edges;
}
// Converts groups, geometric edges and faces into the requested mode.
ModelSelection fromGroups(const Context &c, int surface, const QSet<int> &groups, ModelSelectionMode mode)
{
	ModelSelection result{surface, {}, {}};
	if (mode == ModelSelectionMode::Vertices)
	{
		for (int g : groups)
		{
			for (int copy : c.topology.copies.value(g))
				result.vertices.insert(copy);
		}
	}
	else if (mode == ModelSelectionMode::Edges)
	{
		for (auto it = c.indexEdges.cbegin(); it != c.indexEdges.cend(); ++it)
		{
			if (groups.contains(it.key().first) && groups.contains(it.key().second))
			{
				for (auto e : it.value())
					result.edges.insert(e);
			}
		}
	}
	else
	{
		for (int face = 0; face < c.surface.triangles.size(); ++face)
		{
			const auto g = c.groups(face);
			if (groups.contains(g[0]) && groups.contains(g[1]) && groups.contains(g[2]))
				result.faces.insert(face);
		}
	}
	return result;
}
ModelSelection fromEdges(const Context &c, int surface, const QSet<ModelEdge> &edges, ModelSelectionMode mode)
{
	ModelSelection result{surface, {}, {}};
	if (mode == ModelSelectionMode::Edges)
	{
		for (auto edge : edges)
		{
			for (auto e : c.indexEdges.value(edge))
				result.edges.insert(e);
		}
		return result;
	}
	if (mode == ModelSelectionMode::Vertices)
	{
		QSet<int> groups;
		for (auto edge : edges)
			groups << edge.first << edge.second;
		return fromGroups(c, surface, groups, mode);
	}
	for (auto edge : edges)
	{
		for (int face : c.topology.edgeFaces.value(edge))
			result.faces.insert(face);
	}
	return result;
}
ModelSelection fromFaces(const Context &c, int surface, const QSet<int> &faces, ModelSelectionMode mode)
{
	ModelSelection result{surface, {}, {}};
	if (mode == ModelSelectionMode::Faces)
	{
		result.faces = faces;
		return result;
	}
	for (int face : faces)
	{
		const auto &t = c.surface.triangles[face];
		if (mode == ModelSelectionMode::Vertices)
			result.vertices << t.a << t.b << t.c;
		else
			result.edges << modelEdge(t.a, t.b) << modelEdge(t.b, t.c) << modelEdge(t.c, t.a);
	}
	return result;
}
// Faces reached from `seeds` through geometric (or, delimited, unmarked indexed) edges.
QSet<int> linkedFaces(const Context &c, const QSet<int> &seeds, bool delimitSeams)
{
	QHash<ModelEdge, QVector<int>> byIndexEdge;
	if (delimitSeams)
	{
		for (int face = 0; face < c.surface.triangles.size(); ++face)
		{
			const auto &t = c.surface.triangles[face];
			for (auto e : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
				byIndexEdge[e].append(face);
		}
	}
	QSet<int> reached = seeds;
	QVector<int> stack(seeds.cbegin(), seeds.cend());
	while (!stack.isEmpty())
	{
		const int face = stack.takeLast();
		const auto &t = c.surface.triangles[face];
		for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const auto neighbours = delimitSeams ? (c.surface.uvSeams.contains(modelEdge(a, b)) ? QVector<int>{} : byIndexEdge.value(modelEdge(a, b)))
												 : c.topology.edgeFaces.value(c.topology.geometricEdge(a, b));
			for (int other : neighbours)
			{
				if (!reached.contains(other))
				{
					reached.insert(other);
					stack.append(other);
				}
			}
		}
	}
	return reached;
}
QHash<int, QSet<int>> groupAdjacency(const Context &c)
{
	QHash<int, QSet<int>> adjacency;
	for (auto it = c.topology.edgeFaces.cbegin(); it != c.topology.edgeFaces.cend(); ++it)
	{
		adjacency[it.key().first].insert(it.key().second);
		adjacency[it.key().second].insert(it.key().first);
	}
	return adjacency;
}
quint32 mix(quint32 value)
{
	value ^= value >> 16;
	value *= 0x7feb352dU;
	value ^= value >> 15;
	value *= 0x846ca68bU;
	value ^= value >> 16;
	return value;
}
QVector<int> sortedValues(const QSet<int> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
double coordinate(P3 p, int axis)
{
	return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
} // namespace

QString modelSelectOperationName(ModelSelectOperation operation)
{
	switch (operation)
	{
	case ModelSelectOperation::All:
		return Text::tr("All");
	case ModelSelectOperation::None:
		return Text::tr("None");
	case ModelSelectOperation::Invert:
		return Text::tr("Invert");
	case ModelSelectOperation::Linked:
		return Text::tr("Linked");
	case ModelSelectOperation::More:
		return Text::tr("More");
	case ModelSelectOperation::Less:
		return Text::tr("Less");
	case ModelSelectOperation::Loop:
		return Text::tr("Loop");
	case ModelSelectOperation::Ring:
		return Text::tr("Ring");
	case ModelSelectOperation::ShortestPath:
		return Text::tr("Shortest Path");
	case ModelSelectOperation::Similar:
		return Text::tr("Similar");
	case ModelSelectOperation::NonManifold:
		return Text::tr("Non-Manifold");
	case ModelSelectOperation::Loose:
		return Text::tr("Loose Vertices");
	case ModelSelectOperation::Boundary:
		return Text::tr("Boundary Loop");
	case ModelSelectOperation::Sharp:
		return Text::tr("Sharp Edges");
	case ModelSelectOperation::Random:
		return Text::tr("Random");
	case ModelSelectOperation::Checker:
		return Text::tr("Checker Deselect");
	case ModelSelectOperation::Side:
		return Text::tr("Side of Axis");
	case ModelSelectOperation::Facing:
		return Text::tr("Facing Axis");
	case ModelSelectOperation::Mirror:
		return Text::tr("Mirror");
	}
	return {};
}

bool selectModelComponents(const ModelMesh &mesh, const ModelSelection &current, const ModelSelectRequest &request, ModelSelection *result,
						   QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!result || current.surface < 0 || current.surface >= mesh.surfaces.size())
		return fail(error, Text::tr("Select a valid surface."));
	const auto &surface = mesh.surfaces[current.surface];
	if (request.frame < 0 || request.frame >= surface.frames.size())
		return fail(error, Text::tr("Choose an existing animation pose."));
	if (!std::isfinite(request.threshold) || request.threshold < 0 || !std::isfinite(request.ratio) || request.ratio < 0 || request.ratio > 1 ||
		request.nth < 2 || request.nth > 1000 || request.offset < 0 || request.axis < 0 || request.axis > 2 || !std::isfinite(request.maxAngle))
		return fail(error, Text::tr("A selection setting is outside its supported range."));
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error, control))
		return false;
	const Context c(surface, topology, request.frame);
	const auto mode = request.mode;
	const int s = current.surface;
	ModelSelection selected{s, {}, {}};
	const auto currentFaces = [&]()
	{
		QSet<int> faces;
		for (int face : current.faces)
		{
			if (face >= 0 && face < surface.triangles.size())
				faces.insert(face);
		}
		return faces;
	};
	// Faces touched by the current selection in this mode.
	const auto seedFaces = [&]()
	{
		if (mode == ModelSelectionMode::Faces)
			return currentFaces();
		QSet<int> faces;
		const auto groups = groupsOf(c, current, mode);
		const auto edges = geometricEdgesOf(c, current);
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			const auto g = c.groups(face);
			if (mode == ModelSelectionMode::Vertices && (groups.contains(g[0]) || groups.contains(g[1]) || groups.contains(g[2])))
				faces.insert(face);
			if (mode == ModelSelectionMode::Edges &&
				(edges.contains(modelEdge(g[0], g[1])) || edges.contains(modelEdge(g[1], g[2])) || edges.contains(modelEdge(g[2], g[0]))))
				faces.insert(face);
		}
		return faces;
	};
	switch (request.operation)
	{
	case ModelSelectOperation::All:
	case ModelSelectOperation::Invert:
	{
		ModelSelection all{s, {}, {}};
		if (mode == ModelSelectionMode::Faces)
		{
			for (int face = 0; face < surface.triangles.size(); ++face)
				all.faces.insert(face);
		}
		else if (mode == ModelSelectionMode::Vertices)
		{
			for (int v = 0; v < surface.vertexCount; ++v)
				all.vertices.insert(v);
		}
		else
		{
			for (auto e : c.allIndexEdges)
				all.edges.insert(e);
		}
		if (request.operation == ModelSelectOperation::Invert)
		{
			all.faces -= current.faces;
			all.vertices -= current.vertices;
			all.edges -= current.edges;
		}
		selected = all;
		break;
	}
	case ModelSelectOperation::None:
		break;
	case ModelSelectOperation::Linked:
	{
		const auto seeds = seedFaces();
		if (seeds.isEmpty() && current.vertices.isEmpty())
			return fail(error, Text::tr("Select something to extend to its linked geometry."));
		selected = fromFaces(c, s, linkedFaces(c, seeds, request.delimitSeams), mode);
		if (mode == ModelSelectionMode::Vertices)
			selected.vertices += current.vertices;
		break;
	}
	case ModelSelectOperation::More:
	case ModelSelectOperation::Less:
	{
		const bool more = request.operation == ModelSelectOperation::More;
		const auto adjacency = groupAdjacency(c);
		if (mode == ModelSelectionMode::Vertices)
		{
			const auto groups = groupsOf(c, current, mode);
			QSet<int> next = more ? groups : QSet<int>{};
			for (int g : groups)
			{
				const auto around = adjacency.value(g);
				if (more)
					next += around;
				else if (std::all_of(around.cbegin(), around.cend(), [&](int other) { return groups.contains(other); }))
					next.insert(g);
			}
			selected = fromGroups(c, s, next, mode);
		}
		else if (mode == ModelSelectionMode::Edges)
		{
			const auto edges = geometricEdgesOf(c, current);
			QSet<int> touched;
			for (auto e : edges)
				touched << e.first << e.second;
			QSet<ModelEdge> next;
			if (more)
			{
				for (auto it = c.indexEdges.cbegin(); it != c.indexEdges.cend(); ++it)
				{
					if (touched.contains(it.key().first) || touched.contains(it.key().second))
						next.insert(it.key());
				}
			}
			else
			{
				// Keep edges whose endpoints carry no unselected edge.
				QSet<int> frontier;
				for (auto it = c.indexEdges.cbegin(); it != c.indexEdges.cend(); ++it)
				{
					if (!edges.contains(it.key()))
						frontier << it.key().first << it.key().second;
				}
				for (auto e : edges)
				{
					if (!frontier.contains(e.first) && !frontier.contains(e.second))
						next.insert(e);
				}
			}
			selected = fromEdges(c, s, next, mode);
		}
		else
		{
			const auto faces = currentFaces();
			QSet<int> groups;
			for (int face : faces)
			{
				const auto g = c.groups(face);
				groups << g[0] << g[1] << g[2];
			}
			QSet<int> next;
			if (more)
			{
				next = faces;
				for (int g : groups)
				{
					for (int face : topology.groupFaces.value(g))
						next.insert(face);
				}
			}
			else
			{
				QSet<int> frontier;
				for (int g : groups)
				{
					for (int face : topology.groupFaces.value(g))
					{
						if (!faces.contains(face))
							frontier.insert(g);
					}
				}
				for (int face : faces)
				{
					const auto g = c.groups(face);
					if (!frontier.contains(g[0]) && !frontier.contains(g[1]) && !frontier.contains(g[2]))
						next.insert(face);
				}
			}
			selected.faces = next;
		}
		break;
	}
	case ModelSelectOperation::Loop:
	case ModelSelectOperation::Ring:
	{
		if (request.edge.first < 0 || request.edge.second < 0 || request.edge.first >= surface.vertexCount ||
			request.edge.second >= surface.vertexCount)
			return fail(error, Text::tr("Pick an edge to start the loop or ring."));
		const auto geometric = topology.geometricEdge(request.edge.first, request.edge.second);
		if (!topology.edgeFaces.contains(geometric))
			return fail(error, Text::tr("The picked edge is not part of this surface."));
		ModelQuadTopology quads;
		if (!buildModelQuadTopology(surface, request.frame, request.maxAngle, &quads, error, control))
			return false;
		if (quads.diagonals.contains(geometric))
			return fail(error, Text::tr("The picked edge is hidden inside a quad. Pick a quad side instead."));
		const bool faceLoop = mode == ModelSelectionMode::Faces;
		if (request.operation == ModelSelectOperation::Ring || faceLoop)
		{
			QVector<int> elements;
			const auto ring = modelEdgeRing(quads, geometric, nullptr, &elements);
			if (faceLoop)
			{
				for (int element : std::as_const(elements))
				{
					for (int face : quads.elements[element].faces)
					{
						if (face >= 0)
							selected.faces.insert(face);
					}
				}
				if (selected.faces.isEmpty())
					return fail(error, Text::tr("No quads run across the picked edge."));
			}
			else
				selected = fromEdges(c, s, QSet<ModelEdge>(ring.cbegin(), ring.cend()), mode);
		}
		else
		{
			const auto loop = modelEdgeLoop(quads, geometric);
			selected = fromEdges(c, s, QSet<ModelEdge>(loop.cbegin(), loop.cend()), mode);
		}
		break;
	}
	case ModelSelectOperation::ShortestPath:
	{
		if (mode == ModelSelectionMode::Faces)
		{
			if (request.from < 0 || request.to < 0 || request.from >= surface.triangles.size() || request.to >= surface.triangles.size())
				return fail(error, Text::tr("Choose two existing faces for the path."));
			QHash<int, double> distance{{request.from, 0}};
			QHash<int, int> previous;
			using Entry = std::pair<double, int>;
			std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
			queue.push({0, request.from});
			while (!queue.empty())
			{
				if (!work.step())
					return false;
				const auto [d, face] = queue.top();
				queue.pop();
				if (face == request.to)
					break;
				if (d > distance.value(face, INFINITY))
					continue;
				const auto g = c.groups(face);
				for (auto e : {modelEdge(g[0], g[1]), modelEdge(g[1], g[2]), modelEdge(g[2], g[0])})
				{
					for (int other : topology.edgeFaces.value(e))
					{
						const double next = d + length(c.centroid(other) - c.centroid(face));
						if (next < distance.value(other, INFINITY))
						{
							distance.insert(other, next);
							previous.insert(other, face);
							queue.push({next, other});
						}
					}
				}
			}
			if (!distance.contains(request.to))
				return fail(error, Text::tr("The two faces are not connected."));
			for (int face = request.to;; face = previous.value(face))
			{
				selected.faces.insert(face);
				if (face == request.from)
					break;
			}
			break;
		}
		if (request.from < 0 || request.to < 0 || request.from >= surface.vertexCount || request.to >= surface.vertexCount)
			return fail(error, Text::tr("Choose two existing vertices for the path."));
		const int from = topology.group(request.from), to = topology.group(request.to);
		const auto adjacency = groupAdjacency(c);
		QHash<int, double> distance{{from, 0}};
		QHash<int, int> previous;
		using Entry = std::pair<double, int>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
		queue.push({0, from});
		while (!queue.empty())
		{
			if (!work.step())
				return false;
			const auto [d, g] = queue.top();
			queue.pop();
			if (g == to)
				break;
			if (d > distance.value(g, INFINITY))
				continue;
			for (int other : adjacency.value(g))
			{
				const double next = d + length(c.position(other) - c.position(g));
				if (next < distance.value(other, INFINITY))
				{
					distance.insert(other, next);
					previous.insert(other, g);
					queue.push({next, other});
				}
			}
		}
		if (!distance.contains(to))
			return fail(error, Text::tr("The two vertices are not connected."));
		QSet<int> groups;
		QSet<ModelEdge> edges;
		for (int g = to;; g = previous.value(g))
		{
			groups.insert(g);
			if (g == from)
				break;
			edges.insert(modelEdge(g, previous.value(g)));
		}
		selected = mode == ModelSelectionMode::Vertices ? fromGroups(c, s, groups, mode) : fromEdges(c, s, edges, mode);
		break;
	}
	case ModelSelectOperation::Similar:
	{
		const auto similarity = request.similarity;
		const bool faceKind = similarity == ModelSimilarity::Normal || similarity == ModelSimilarity::Area || similarity == ModelSimilarity::Coplanar;
		const bool edgeKind = similarity == ModelSimilarity::Length || similarity == ModelSimilarity::Direction ||
							  similarity == ModelSimilarity::FaceAngle || similarity == ModelSimilarity::Seam;
		const bool vertexKind = similarity == ModelSimilarity::Normal || similarity == ModelSimilarity::Valence;
		if ((mode == ModelSelectionMode::Faces && !faceKind) || (mode == ModelSelectionMode::Edges && !edgeKind) ||
			(mode == ModelSelectionMode::Vertices && !vertexKind))
			return fail(error, Text::tr("That similarity does not apply to the current selection mode."));
		if (mode == ModelSelectionMode::Faces)
		{
			const auto seeds = currentFaces();
			if (seeds.isEmpty())
				return fail(error, Text::tr("Select faces to compare against."));
			const double tolerance = c.scale() * 1e-3;
			for (int face = 0; face < surface.triangles.size(); ++face)
			{
				if (!work.step())
					return false;
				const P3 n = c.faceCross(face);
				for (int seed : seeds)
				{
					const P3 sn = c.faceCross(seed);
					bool match = false;
					if (similarity == ModelSimilarity::Normal)
						match = angleDegrees(n, sn) <= request.threshold;
					else if (similarity == ModelSimilarity::Area)
						match = std::abs(length(n) - length(sn)) <= length(sn) * request.threshold / 100;
					else
						match = angleDegrees(n, sn) <= request.threshold &&
								std::abs(dot(unitVector(sn), c.centroid(face) - c.centroid(seed))) <= tolerance;
					if (match)
					{
						selected.faces.insert(face);
						break;
					}
				}
			}
		}
		else if (mode == ModelSelectionMode::Edges)
		{
			const auto seeds = geometricEdgesOf(c, current);
			if (seeds.isEmpty())
				return fail(error, Text::tr("Select edges to compare against."));
			const auto dihedral = [&](ModelEdge e)
			{
				const auto faces = topology.edgeFaces.value(e);
				return faces.size() == 2 ? angleDegrees(c.faceCross(faces[0]), c.faceCross(faces[1])) : -1.0;
			};
			const auto seamed = [&](ModelEdge e)
			{
				for (auto indexEdge : c.indexEdges.value(e))
				{
					if (surface.uvSeams.contains(indexEdge))
						return true;
				}
				return false;
			};
			QSet<ModelEdge> matched;
			for (auto it = c.indexEdges.cbegin(); it != c.indexEdges.cend(); ++it)
			{
				if (!work.step())
					return false;
				const auto e = it.key();
				const P3 d = c.position(e.second) - c.position(e.first);
				for (auto seed : seeds)
				{
					const P3 sd = c.position(seed.second) - c.position(seed.first);
					bool match = false;
					if (similarity == ModelSimilarity::Length)
						match = std::abs(length(d) - length(sd)) <= length(sd) * request.threshold / 100;
					else if (similarity == ModelSimilarity::Direction)
						match = std::min(angleDegrees(d, sd), angleDegrees(d, sd * -1.0)) <= request.threshold;
					else if (similarity == ModelSimilarity::FaceAngle)
					{
						const double a = dihedral(e), b = dihedral(seed);
						match = (a < 0) == (b < 0) && std::abs(a - b) <= request.threshold;
					}
					else
						match = seamed(e) == seamed(seed);
					if (match)
					{
						matched.insert(e);
						break;
					}
				}
			}
			selected = fromEdges(c, s, matched, mode);
		}
		else
		{
			const auto seeds = groupsOf(c, current, mode);
			if (seeds.isEmpty())
				return fail(error, Text::tr("Select vertices to compare against."));
			const auto adjacency = groupAdjacency(c);
			QSet<int> matched;
			for (int g = 0; g < surface.vertexCount; ++g)
			{
				if (!work.step())
					return false;
				if (topology.group(g) != g)
					continue;
				for (int seed : seeds)
				{
					const bool match = similarity == ModelSimilarity::Normal ? angleDegrees(c.groupNormal(g), c.groupNormal(seed)) <= request.threshold
																			 : adjacency.value(g).size() == adjacency.value(seed).size();
					if (match)
					{
						matched.insert(g);
						break;
					}
				}
			}
			selected = fromGroups(c, s, matched, mode);
		}
		break;
	}
	case ModelSelectOperation::NonManifold:
	case ModelSelectOperation::Sharp:
	{
		QSet<ModelEdge> edges;
		for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
		{
			if (!work.step())
				return false;
			const auto &faces = it.value();
			if (request.operation == ModelSelectOperation::Sharp)
			{
				if (faces.size() == 2 && angleDegrees(c.faceCross(faces[0]), c.faceCross(faces[1])) > request.threshold)
					edges.insert(it.key());
				continue;
			}
			if (faces.size() != 2)
			{
				edges.insert(it.key());
				continue;
			}
			// Consistent winding traverses a shared edge in opposite directions.
			int forward = 0;
			for (int face : faces)
			{
				const auto g = c.groups(face);
				for (int k = 0; k < 3; ++k)
					forward += g[k] == it.key().first && g[(k + 1) % 3] == it.key().second;
			}
			if (forward != 1)
				edges.insert(it.key());
		}
		selected = fromEdges(c, s, edges, mode);
		break;
	}
	case ModelSelectOperation::Loose:
	{
		if (mode != ModelSelectionMode::Vertices)
			return fail(error, Text::tr("Loose selection applies to vertices; triangle surfaces have no loose edges or faces."));
		QSet<int> used;
		for (const auto &t : surface.triangles)
			used << t.a << t.b << t.c;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			if (!used.contains(v))
				selected.vertices.insert(v);
		}
		break;
	}
	case ModelSelectOperation::Boundary:
	{
		const auto faces = seedFaces();
		if (faces.isEmpty())
			return fail(error, Text::tr("Select faces to find the border of."));
		QSet<ModelEdge> edges;
		for (int face : faces)
		{
			const auto g = c.groups(face);
			for (auto e : {modelEdge(g[0], g[1]), modelEdge(g[1], g[2]), modelEdge(g[2], g[0])})
			{
				int inside = 0;
				for (int other : topology.edgeFaces.value(e))
					inside += faces.contains(other);
				if (inside == 1)
					edges.insert(e);
			}
		}
		// The border is a set of edges whatever the current mode.
		selected = fromEdges(c, s, edges, mode == ModelSelectionMode::Faces ? ModelSelectionMode::Edges : mode);
		break;
	}
	case ModelSelectOperation::Random:
	{
		const auto pick = [&](quint32 index) { return double(mix(index * 2654435761U ^ mix(request.seed + 0x9e3779b9U))) / 4294967296.0 < request.ratio; };
		if (mode == ModelSelectionMode::Faces)
		{
			for (int face = 0; face < surface.triangles.size(); ++face)
			{
				if (pick(quint32(face)))
					selected.faces.insert(face);
			}
		}
		else if (mode == ModelSelectionMode::Vertices)
		{
			QSet<int> groups;
			for (int v = 0; v < surface.vertexCount; ++v)
			{
				if (topology.group(v) == v && pick(quint32(v)))
					groups.insert(v);
			}
			selected = fromGroups(c, s, groups, mode);
		}
		else
		{
			QSet<ModelEdge> edges;
			int index = 0;
			auto keys = c.indexEdges.keys();
			std::sort(keys.begin(), keys.end());
			for (auto e : std::as_const(keys))
			{
				if (pick(quint32(index++)))
					edges.insert(e);
			}
			selected = fromEdges(c, s, edges, mode);
		}
		break;
	}
	case ModelSelectOperation::Checker:
	{
		// Walk the selection breadth-first from its lowest element and drop every nth.
		if (mode == ModelSelectionMode::Faces)
		{
			const auto faces = currentFaces();
			if (faces.isEmpty())
				return fail(error, Text::tr("Select faces to thin out."));
			QSet<int> visited;
			int position = 0;
			for (int start : sortedValues(faces))
			{
				if (visited.contains(start))
					continue;
				QVector<int> queue{start};
				visited.insert(start);
				for (int i = 0; i < queue.size(); ++i)
				{
					const int face = queue[i];
					if ((position++ + request.offset) % request.nth != request.nth - 1)
						selected.faces.insert(face);
					const auto g = c.groups(face);
					QVector<int> next;
					for (auto e : {modelEdge(g[0], g[1]), modelEdge(g[1], g[2]), modelEdge(g[2], g[0])})
					{
						for (int other : topology.edgeFaces.value(e))
						{
							if (faces.contains(other) && !visited.contains(other))
								next.append(other);
						}
					}
					std::sort(next.begin(), next.end());
					for (int other : std::as_const(next))
					{
						if (!visited.contains(other))
						{
							visited.insert(other);
							queue.append(other);
						}
					}
				}
			}
		}
		else
		{
			const auto groups = mode == ModelSelectionMode::Vertices ? groupsOf(c, current, mode) : QSet<int>{};
			const auto edges = geometricEdgesOf(c, current);
			if (groups.isEmpty() && edges.isEmpty())
				return fail(error, Text::tr("Select something to thin out."));
			if (mode == ModelSelectionMode::Vertices)
			{
				const auto adjacency = groupAdjacency(c);
				QSet<int> visited, kept;
				int position = 0;
				for (int start : sortedValues(groups))
				{
					if (visited.contains(start))
						continue;
					QVector<int> queue{start};
					visited.insert(start);
					for (int i = 0; i < queue.size(); ++i)
					{
						const int g = queue[i];
						if ((position++ + request.offset) % request.nth != request.nth - 1)
							kept.insert(g);
						auto next = sortedValues(adjacency.value(g) & groups);
						for (int other : std::as_const(next))
						{
							if (!visited.contains(other))
							{
								visited.insert(other);
								queue.append(other);
							}
						}
					}
				}
				selected = fromGroups(c, s, kept, mode);
			}
			else
			{
				auto ordered = edges.values().toVector();
				std::sort(ordered.begin(), ordered.end());
				QSet<ModelEdge> kept;
				for (int i = 0; i < ordered.size(); ++i)
				{
					if ((i + request.offset) % request.nth != request.nth - 1)
						kept.insert(ordered[i]);
				}
				selected = fromEdges(c, s, kept, mode);
			}
		}
		break;
	}
	case ModelSelectOperation::Side:
	{
		const double sign = request.positive ? 1 : -1;
		const auto beyond = [&](int v) { return coordinate(c.position(v), request.axis) * sign > request.threshold; };
		if (mode == ModelSelectionMode::Vertices)
		{
			for (int v = 0; v < surface.vertexCount; ++v)
			{
				if (beyond(v))
					selected.vertices.insert(v);
			}
		}
		else if (mode == ModelSelectionMode::Edges)
		{
			for (auto e : c.allIndexEdges)
			{
				if (beyond(e.first) && beyond(e.second))
					selected.edges.insert(e);
			}
		}
		else
		{
			for (int face = 0; face < surface.triangles.size(); ++face)
			{
				const auto &t = surface.triangles[face];
				if (beyond(t.a) && beyond(t.b) && beyond(t.c))
					selected.faces.insert(face);
			}
		}
		break;
	}
	case ModelSelectOperation::Facing:
	{
		P3 direction{0, 0, 0};
		(request.axis == 0 ? direction.x : request.axis == 1 ? direction.y : direction.z) = request.positive ? 1 : -1;
		QSet<int> faces;
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			if (angleDegrees(c.faceCross(face), direction) <= request.threshold)
				faces.insert(face);
		}
		selected = fromFaces(c, s, faces, mode);
		break;
	}
	case ModelSelectOperation::Mirror:
	{
		const double tolerance = std::max(1e-4, c.scale() * 1e-6);
		std::map<std::array<qint64, 3>, QVector<int>> grid;
		const double cell = tolerance * 2;
		const auto cellOf = [&](P3 p)
		{ return std::array<qint64, 3>{qint64(std::floor(p.x / cell)), qint64(std::floor(p.y / cell)), qint64(std::floor(p.z / cell))}; };
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			if (topology.group(v) == v)
				grid[cellOf(c.position(v))].append(v);
		}
		const auto mirrorOf = [&](int g)
		{
			P3 target = c.position(g);
			(request.axis == 0 ? target.x : request.axis == 1 ? target.y : target.z) *= -1;
			const auto centre = cellOf(target);
			int match = -1;
			double best = tolerance;
			for (qint64 dx = -1; dx <= 1; ++dx)
			{
				for (qint64 dy = -1; dy <= 1; ++dy)
				{
					for (qint64 dz = -1; dz <= 1; ++dz)
					{
						const auto found = grid.find({centre[0] + dx, centre[1] + dy, centre[2] + dz});
						if (found == grid.end())
							continue;
						for (int candidate : found->second)
						{
							const double d = length(c.position(candidate) - target);
							if (d < best || (d == best && (match < 0 || candidate < match)))
							{
								best = d;
								match = candidate;
							}
						}
					}
				}
			}
			return match;
		};
		if (mode == ModelSelectionMode::Faces)
		{
			std::map<std::array<int, 3>, int> byGroups;
			for (int face = 0; face < surface.triangles.size(); ++face)
			{
				auto g = c.groups(face);
				std::sort(g.begin(), g.end());
				byGroups.emplace(g, face);
			}
			for (int face : currentFaces())
			{
				auto g = c.groups(face);
				std::array<int, 3> mirrored{mirrorOf(g[0]), mirrorOf(g[1]), mirrorOf(g[2])};
				std::sort(mirrored.begin(), mirrored.end());
				const auto found = byGroups.find(mirrored);
				if (mirrored[0] >= 0 && found != byGroups.end())
					selected.faces.insert(found->second);
			}
		}
		else if (mode == ModelSelectionMode::Vertices)
		{
			QSet<int> groups;
			for (int g : groupsOf(c, current, mode))
			{
				const int m = mirrorOf(g);
				if (m >= 0)
					groups.insert(m);
			}
			selected = fromGroups(c, s, groups, mode);
		}
		else
		{
			QSet<ModelEdge> edges;
			for (auto e : geometricEdgesOf(c, current))
			{
				const auto m = modelEdge(mirrorOf(e.first), mirrorOf(e.second));
				if (m.first >= 0 && c.indexEdges.contains(m))
					edges.insert(m);
			}
			selected = fromEdges(c, s, edges, mode);
		}
		break;
	}
	}
	if (request.extend)
	{
		selected.faces += current.faces;
		selected.vertices += current.vertices;
		selected.edges += current.edges;
	}
	selected.surface = s;
	*result = std::move(selected);
	return work.check();
}
} // namespace vibestudio
