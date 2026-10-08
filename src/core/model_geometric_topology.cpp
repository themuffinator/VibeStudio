#include "core/model_geometric_topology.h"

#include "core/model_geometry_helpers.h"

#include <QCoreApplication>

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMeshTools)
};
using namespace model_geometry;

bool samePositions(const ModelSurface &surface, int a, int b)
{
	for (const auto &frame : surface.frames)
	{
		const auto p = frame.positions[a], q = frame.positions[b];
		if (p.x != q.x || p.y != q.y || p.z != q.z)
			return false;
	}
	return true;
}
quint64 positionHash(const ModelSurface &surface, int vertex)
{
	quint64 hash = 1469598103934665603ull;
	for (const auto &frame : surface.frames)
	{
		const auto p = frame.positions[vertex];
		// Adding zero folds -0 into +0 so equal coordinates share a bucket.
		for (float value : {p.x + 0.0f, p.y + 0.0f, p.z + 0.0f})
		{
			hash ^= std::bit_cast<quint32>(value);
			hash *= 1099511628211ull;
		}
	}
	return hash;
}
P3 faceNormal(const ModelFrameGeometry &frame, const std::array<int, 3> &corners)
{
	const auto a = point(frame.positions[corners[0]]), b = point(frame.positions[corners[1]]), c = point(frame.positions[corners[2]]);
	return cross(b - a, c - a);
}
double angleBetween(P3 a, P3 b)
{
	const double la = length(a), lb = length(b);
	if (la <= 0 || lb <= 0)
		return std::numbers::pi;
	return std::acos(std::clamp(dot(a, b) / (la * lb), -1.0, 1.0));
}
struct Candidate
{
	double score = 0;
	int first = -1, second = -1;
	// First face rotated so the shared edge runs corners[0] -> corners[1];
	// the second face's corner opposite that edge.
	std::array<int, 3> rotated{};
	int opposite = -1;
};
std::array<int, 3> rotateToEdge(const ModelTriangle &t, int a, int b)
{
	if (t.a == a && t.b == b)
		return {t.a, t.b, t.c};
	if (t.b == a && t.c == b)
		return {t.b, t.c, t.a};
	return {t.c, t.a, t.b};
}
} // namespace

bool buildModelGeometricTopology(const ModelSurface &surface, ModelGeometricTopology *result, QString *error,
								 const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	const int count = surface.vertexCount;
	if (!result || count < 0 || surface.texCoords.size() != count || surface.frames.isEmpty())
	{
		if (error)
			*error = Text::tr("The surface has no complete vertex data.");
		return false;
	}
	for (const auto &frame : surface.frames)
	{
		if (frame.positions.size() != count)
		{
			if (error)
				*error = Text::tr("Every animation pose must contain every vertex position.");
			return false;
		}
	}
	ModelGeometricTopology topology;
	topology.vertexGroup.resize(count);
	topology.copies.resize(count);
	topology.groupFaces.resize(count);
	QHash<quint64, QVector<int>> buckets;
	buckets.reserve(count);
	for (int vertex = 0; vertex < count; ++vertex)
	{
		if (!work.step())
			return false;
		auto &bucket = buckets[positionHash(surface, vertex)];
		int group = -1;
		for (int candidate : std::as_const(bucket))
		{
			if (samePositions(surface, candidate, vertex))
			{
				group = candidate;
				break;
			}
		}
		if (group < 0)
		{
			group = vertex;
			bucket.append(vertex);
		}
		topology.vertexGroup[vertex] = group;
		topology.copies[group].append(vertex);
	}
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface.triangles[face];
		if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= count || t.b >= count || t.c >= count)
		{
			if (error)
				*error = Text::tr("The surface contains an invalid triangle index.");
			return false;
		}
		const std::array<int, 3> groups{topology.vertexGroup[t.a], topology.vertexGroup[t.b], topology.vertexGroup[t.c]};
		for (int group : groups)
		{
			auto &faces = topology.groupFaces[group];
			if (faces.isEmpty() || faces.last() != face)
				faces.append(face);
		}
		for (int corner = 0; corner < 3; ++corner)
		{
			const int a = groups[corner], b = groups[(corner + 1) % 3];
			if (a != b)
				topology.edgeFaces[modelEdge(a, b)].append(face);
		}
	}
	*result = std::move(topology);
	return work.check();
}

bool buildModelQuadTopology(const ModelSurface &surface, int frame, double maxAngleDegrees, ModelQuadTopology *result, QString *error,
							const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!result || frame < 0 || frame >= surface.frames.size() || !std::isfinite(maxAngleDegrees) || maxAngleDegrees < 0 ||
		maxAngleDegrees > 180)
	{
		if (error)
			*error = Text::tr("Choose an existing reference pose and a pairing angle from 0 to 180 degrees.");
		return false;
	}
	ModelQuadTopology topology;
	if (!buildModelGeometricTopology(surface, &topology.geometry, error, control))
		return false;
	const auto &positions = surface.frames[frame];
	// Directed indexed edges, so a partner must traverse the shared edge in the
	// opposite direction (consistent winding) and the edge must not branch.
	QHash<ModelEdge, QVector<int>> indexed;
	QHash<QPair<int, int>, int> directed;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface.triangles[face];
		for (const auto &edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			indexed[modelEdge(edge.first, edge.second)].append(face);
			directed.insert(edge, face);
		}
	}
	const double maxAngle = maxAngleDegrees * std::numbers::pi / 180.0;
	QVector<Candidate> candidates;
	for (auto it = indexed.cbegin(); it != indexed.cend(); ++it)
	{
		if (!work.step())
			return false;
		if (it.value().size() != 2 || it.value()[0] == it.value()[1])
			continue;
		const int first = std::min(it.value()[0], it.value()[1]), second = std::max(it.value()[0], it.value()[1]);
		const auto &f = surface.triangles[first];
		// Find the shared edge's direction in the first face.
		int a = -1, b = -1;
		for (const auto &edge : {qMakePair(f.a, f.b), qMakePair(f.b, f.c), qMakePair(f.c, f.a)})
		{
			if (modelEdge(edge.first, edge.second) == it.key())
			{
				a = edge.first;
				b = edge.second;
			}
		}
		if (directed.value(qMakePair(b, a), -1) != second)
			continue;
		const auto geometricEdge = topology.geometry.geometricEdge(a, b);
		if (topology.geometry.edgeFaces.value(geometricEdge).size() != 2)
			continue;
		const auto rotated = rotateToEdge(f, a, b);
		const auto g = rotateToEdge(surface.triangles[second], b, a);
		const int c = rotated[2], d = g[2];
		if (c == d || topology.geometry.group(c) == topology.geometry.group(d))
			continue;
		const P3 nf = faceNormal(positions, rotated), ng = faceNormal(positions, g);
		const double normalAngle = angleBetween(nf, ng);
		if (normalAngle > maxAngle + 1e-12)
			continue;
		// Quad corners in winding order: a, d, b, c.
		const std::array<P3, 4> q{point(positions.positions[a]), point(positions.positions[d]), point(positions.positions[b]),
								  point(positions.positions[c])};
		P3 axis = nf * (1.0 / std::max(length(nf), 1e-300)) + ng * (1.0 / std::max(length(ng), 1e-300));
		const double axisLength = length(axis);
		if (axisLength <= 1e-12)
			continue;
		axis = axis * (1.0 / axisLength);
		double scale = 0;
		for (int i = 0; i < 4; ++i)
			scale = std::max(scale, length(q[(i + 1) % 4] - q[i]));
		bool convex = scale > 0;
		double shapeError = 0;
		for (int i = 0; i < 4 && convex; ++i)
		{
			const P3 previous = q[(i + 3) % 4] - q[i], next = q[(i + 1) % 4] - q[i];
			// Strictly convex: every corner turns the same way about the quad normal.
			if (dot(cross(next, previous), axis) <= 1e-9 * scale * scale)
				convex = false;
			shapeError += std::abs(angleBetween(previous, next) - std::numbers::pi / 2);
		}
		if (!convex)
			continue;
		const double diagonal = length(q[2] - q[0]);
		const bool longest = diagonal + 1e-9 * scale >= std::max({length(q[3] - q[0]), length(q[3] - q[2]), length(q[1] - q[0]),
																   length(q[1] - q[2])});
		Candidate candidate;
		candidate.first = first;
		candidate.second = second;
		candidate.rotated = rotated;
		candidate.opposite = d;
		candidate.score = (maxAngle > 0 ? normalAngle / maxAngle : 0) + shapeError / (2 * std::numbers::pi) + (longest ? 0 : 0.5);
		candidates.append(candidate);
	}
	std::sort(candidates.begin(), candidates.end(),
			  [](const Candidate &left, const Candidate &right)
			  {
				  if (left.score != right.score)
					  return left.score < right.score;
				  return std::pair(left.first, left.second) < std::pair(right.first, right.second);
			  });
	QVector<int> partner(surface.triangles.size(), -1);
	QHash<int, Candidate> chosen;
	for (const auto &candidate : std::as_const(candidates))
	{
		if (!work.step())
			return false;
		if (partner[candidate.first] >= 0 || partner[candidate.second] >= 0)
			continue;
		partner[candidate.first] = candidate.second;
		partner[candidate.second] = candidate.first;
		chosen.insert(candidate.first, candidate);
	}
	topology.faceElement.fill(-1, surface.triangles.size());
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		if (topology.faceElement[face] >= 0)
			continue;
		ModelQuadElement element;
		if (partner[face] >= 0)
		{
			const auto candidate = chosen.value(face);
			element.corners = {candidate.rotated[0], candidate.opposite, candidate.rotated[1], candidate.rotated[2]};
			element.faces = {face, partner[face]};
			topology.diagonals.insert(topology.geometry.geometricEdge(candidate.rotated[0], candidate.rotated[1]));
		}
		else
		{
			const auto &t = surface.triangles[face];
			element.corners = {t.a, t.b, t.c, -1};
			element.faces = {face, -1};
		}
		const int index = topology.elements.size();
		for (int member : element.faces)
		{
			if (member >= 0)
				topology.faceElement[member] = index;
		}
		const int sides = element.sides();
		for (int side = 0; side < sides; ++side)
		{
			const auto edge = topology.geometry.geometricEdge(element.corners[side], element.corners[(side + 1) % sides]);
			topology.sideUses[edge].append({index, side});
		}
		topology.elements.append(element);
	}
	*result = std::move(topology);
	return work.check();
}

namespace
{
// The side of element `element` that touches group `vertex` and is not `excluded`.
int otherSideAt(const ModelQuadTopology &topology, int element, int vertex, ModelEdge excluded, ModelEdge *edge)
{
	const auto &e = topology.elements[element];
	const int sides = e.sides();
	for (int side = 0; side < sides; ++side)
	{
		const int a = topology.geometry.group(e.corners[side]), b = topology.geometry.group(e.corners[(side + 1) % sides]);
		const auto candidate = modelEdge(a, b);
		if ((a == vertex || b == vertex) && candidate != excluded)
		{
			*edge = candidate;
			return side;
		}
	}
	return -1;
}
// The single other element using `edge`, or -1 at a boundary or branching edge.
int across(const ModelQuadTopology &topology, ModelEdge edge, int element)
{
	const auto uses = topology.sideUses.value(edge);
	if (uses.size() != 2)
		return -1;
	return uses[0].first == element ? uses[1].first : uses[1].first == element ? uses[0].first : -1;
}
// The continuation of a loop arriving at `vertex` along `incoming`, or an
// invalid edge where the loop ends.
ModelEdge loopStep(const ModelQuadTopology &topology, ModelEdge incoming, int vertex,
				   const QHash<int, QVector<ModelEdge>> &boundaryEdges)
{
	const ModelEdge none{-1, -1};
	const auto uses = topology.sideUses.value(incoming);
	if (uses.size() == 1)
	{
		const auto edges = boundaryEdges.value(vertex);
		if (edges.size() != 2)
			return none;
		return edges[0] == incoming ? edges[1] : edges[1] == incoming ? edges[0] : none;
	}
	if (uses.size() != 2)
		return none;
	int element = uses[0].first;
	ModelEdge current = incoming, continuation = none;
	for (int step = 0; step < 4; ++step)
	{
		if (element < 0 || !topology.elements[element].quad())
			return none;
		ModelEdge next;
		if (otherSideAt(topology, element, vertex, current, &next) < 0)
			return none;
		if (step == 1)
			continuation = next;
		if (step == 3)
			return next == incoming && element == uses[1].first ? continuation : none;
		element = across(topology, next, element);
		current = next;
	}
	return none;
}
} // namespace

QVector<ModelEdge> modelEdgeLoop(const ModelQuadTopology &topology, ModelEdge geometric, bool *closed)
{
	if (closed)
		*closed = false;
	geometric = modelEdge(geometric.first, geometric.second);
	if (!topology.sideUses.contains(geometric) || topology.diagonals.contains(geometric))
		return {};
	QHash<int, QVector<ModelEdge>> boundaryEdges;
	for (auto it = topology.sideUses.cbegin(); it != topology.sideUses.cend(); ++it)
	{
		if (it.value().size() == 1)
		{
			boundaryEdges[it.key().first].append(it.key());
			boundaryEdges[it.key().second].append(it.key());
		}
	}
	QSet<ModelEdge> seen{geometric};
	const auto walk = [&](int towards, QVector<ModelEdge> *edges)
	{
		ModelEdge current = geometric;
		int vertex = towards;
		while (true)
		{
			const auto next = loopStep(topology, current, vertex, boundaryEdges);
			if (next.first < 0)
				return false;
			if (next == geometric)
				return true;
			if (seen.contains(next))
				return false;
			seen.insert(next);
			edges->append(next);
			vertex = next.first == vertex ? next.second : next.first;
			current = next;
		}
	};
	QVector<ModelEdge> forward, backward;
	const bool isClosed = walk(geometric.second, &forward);
	if (!isClosed)
		walk(geometric.first, &backward);
	if (closed)
		*closed = isClosed;
	QVector<ModelEdge> result;
	for (auto it = backward.crbegin(); it != backward.crend(); ++it)
		result.append(*it);
	result.append(geometric);
	result += forward;
	return result;
}

QVector<ModelEdge> modelEdgeRing(const ModelQuadTopology &topology, ModelEdge geometric, bool *closed, QVector<int> *elements)
{
	if (closed)
		*closed = false;
	if (elements)
		elements->clear();
	geometric = modelEdge(geometric.first, geometric.second);
	const auto seedUses = topology.sideUses.value(geometric);
	if (seedUses.isEmpty() || seedUses.size() > 2 || topology.diagonals.contains(geometric))
		return {};
	QSet<int> visited;
	const auto walk = [&](QPair<int, int> use, QVector<ModelEdge> *edges, QVector<int> *walked)
	{
		while (true)
		{
			const auto &element = topology.elements[use.first];
			if (!element.quad() || visited.contains(use.first))
				return false;
			visited.insert(use.first);
			walked->append(use.first);
			const int side = (use.second + 2) % 4;
			const auto opposite =
				topology.geometry.geometricEdge(element.corners[side], element.corners[(side + 1) % 4]);
			if (opposite == geometric)
				return true;
			edges->append(opposite);
			const auto uses = topology.sideUses.value(opposite);
			if (uses.size() != 2)
				return false;
			use = uses[0].first == use.first && uses[0].second == side ? uses[1] : uses[0];
		}
	};
	QVector<ModelEdge> forward, backward;
	QVector<int> forwardElements, backwardElements;
	const bool isClosed = walk(seedUses[0], &forward, &forwardElements);
	if (!isClosed && seedUses.size() == 2)
		walk(seedUses[1], &backward, &backwardElements);
	if (closed)
		*closed = isClosed;
	QVector<ModelEdge> result;
	for (auto it = backward.crbegin(); it != backward.crend(); ++it)
		result.append(*it);
	result.append(geometric);
	result += forward;
	if (elements)
	{
		for (auto it = backwardElements.crbegin(); it != backwardElements.crend(); ++it)
			elements->append(*it);
		*elements += forwardElements;
	}
	return result;
}
} // namespace vibestudio
