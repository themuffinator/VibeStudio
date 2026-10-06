#include "core/model_topology_health.h"

#include <QCoreApplication>
#include <QMap>

#include <algorithm>
#include <array>
#include <map>
#include <numeric>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const char *message)
{
	if (error)
	{
		*error = QCoreApplication::translate("VibeStudioModelDocument", message);
	}
	return false;
}
class Groups
{
  public:
	explicit Groups(int size) : m_parent(size), m_rank(size, 0)
	{
		std::iota(m_parent.begin(), m_parent.end(), 0);
	}
	int root(int i)
	{
		while (m_parent[i] != i)
		{
			m_parent[i] = m_parent[m_parent[i]];
			i = m_parent[i];
		}
		return i;
	}
	void join(int a, int b)
	{
		a = root(a);
		b = root(b);
		if (a == b)
		{
			return;
		}
		if (m_rank[a] < m_rank[b])
		{
			std::swap(a, b);
		}
		m_parent[b] = a;
		m_rank[a] += m_rank[a] == m_rank[b];
	}

  private:
	QVector<int> m_parent, m_rank;
};
std::array<int, 3> corners(ModelTriangle t)
{
	return {t.a, t.b, t.c};
}
int corner(ModelTriangle t, int vertex)
{
	return t.a == vertex ? 0 : t.b == vertex ? 1 : 2;
}
void replace(ModelTriangle &t, int old, int replacement)
{
	if (t.a == old)
	{
		t.a = replacement;
	}
	if (t.b == old)
	{
		t.b = replacement;
	}
	if (t.c == old)
	{
		t.c = replacement;
	}
}

bool nonmanifoldCuts(const ModelSurface &surface, const ModelTopologyHealth &health, QVector<ModelVertexFans> *splits,
					 ModelWorkProgress &work)
{
	QVector<bool> affected(surface.vertexCount, false);
	for (const auto &edge : health.nonmanifoldEdges)
	{
		if (!work.step())
		{
			return false;
		}
		affected[edge.first] = affected[edge.second] = true;
	}
	// A triangle corner has at most two incident manifold-edge connections.
	// Removing overfull-edge connections leaves paths/cycles in its vertex link.
	// Each path has at most two ends, so a resulting indexed edge cannot retain
	// more than two incident faces. No geometric pairing or pose-dependent choice
	// is needed; existing two-face connections remain intact in every frame.
	Groups groups(int(surface.triangles.size()) * 3);
	for (const auto &edge : health.edges)
	{
		if (!work.step())
		{
			return false;
		}
		if (edge.faces.size() != 2)
		{
			continue;
		}
		for (int vertex : {edge.vertices.first, edge.vertices.second})
		{
			if (affected[vertex])
			{
				const int a = edge.faces[0], b = edge.faces[1];
				groups.join(a * 3 + corner(surface.triangles[a], vertex), b * 3 + corner(surface.triangles[b], vertex));
			}
		}
	}
	QVector<QVector<int>> incident(surface.vertexCount);
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		const auto indices = corners(surface.triangles[face]);
		for (int c = 0; c < 3; ++c)
		{
			if (affected[indices[c]])
			{
				incident[indices[c]] << face * 3 + c;
			}
		}
	}
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
	{
		if (!work.step())
		{
			return false;
		}
		if (!affected[vertex])
		{
			continue;
		}
		QMap<int, QVector<int>> fans;
		for (int c : incident[vertex])
		{
			if (!work.step())
			{
				return false;
			}
			fans[groups.root(c)] << c / 3;
		}
		if (fans.size() > 1)
		{
			ModelVertexFans split{vertex, fans.values().toVector()};
			std::sort(split.faces.begin(), split.faces.end(), [](const auto &a, const auto &b) { return a.first() < b.first(); });
			splits->append(std::move(split));
		}
	}
	return work.check();
}
} // namespace

bool inspectModelTopology(const ModelSurface &surface, ModelTopologyHealth *result, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check())
	{
		return false;
	}
	if (!result || surface.vertexCount < 3 || surface.vertexCount > 65536 || surface.triangles.isEmpty() ||
		surface.triangles.size() > 131072)
	{
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument", "Topology inspection requires a bounded, indexed surface."));
	}
	ModelTopologyHealth health;
	QMap<ModelEdge, ModelHealthEdge> edges;
	QMap<ModelEdge, int> directions;
	std::map<std::array<int, 3>, QVector<int>> duplicates;
	QVector<QVector<int>> incident(surface.vertexCount);
	Groups fans(int(surface.triangles.size()) * 3), components(int(surface.triangles.size()));
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		const auto indices = corners(surface.triangles[face]);
		if (indices[0] == indices[1] || indices[1] == indices[2] || indices[2] == indices[0] ||
			std::any_of(indices.begin(), indices.end(), [&](int i) { return i < 0 || i >= surface.vertexCount; }))
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument", "Topology inspection found an invalid triangle index."));
		}
		auto key = indices;
		std::sort(key.begin(), key.end());
		duplicates[key] << face;
		for (int c = 0; c < 3; ++c)
		{
			const int a = indices[c], b = indices[(c + 1) % 3];
			incident[a] << face * 3 + c;
			const auto edge = modelEdge(a, b);
			edges[edge].faces << face;
			directions[edge] += a < b ? 1 : -1;
		}
	}
	for (auto it = edges.begin(); it != edges.end(); ++it)
	{
		if (!work.step())
		{
			return false;
		}
		auto &edge = it.value();
		edge.vertices = it.key();
		edge.inconsistentWinding = edge.faces.size() == 2 && std::abs(directions[it.key()]) == 2;
		if (edge.faces.size() == 1)
		{
			health.boundaryEdges << it.key();
		}
		if (edge.faces.size() > 2)
		{
			health.nonmanifoldEdges << it.key();
		}
		if (edge.inconsistentWinding)
		{
			health.windingEdges << it.key();
		}
		const int first = edge.faces.first();
		for (int f = 1; f < edge.faces.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			const int face = edge.faces[f];
			components.join(first, face);
			for (int vertex : {it.key().first, it.key().second})
			{
				fans.join(first * 3 + corner(surface.triangles[first], vertex), face * 3 + corner(surface.triangles[face], vertex));
			}
		}
		health.edges << std::move(edge);
	}
	for (int vertex = 0; vertex < surface.vertexCount; ++vertex)
	{
		if (!work.step())
		{
			return false;
		}
		if (incident[vertex].isEmpty())
		{
			health.unusedVertices << vertex;
			continue;
		}
		QMap<int, QVector<int>> groups;
		for (int c : incident[vertex])
		{
			if (!work.step())
			{
				return false;
			}
			groups[fans.root(c)] << c / 3;
		}
		if (groups.size() > 1)
		{
			ModelVertexFans found{vertex, groups.values().toVector()};
			std::sort(found.faces.begin(), found.faces.end(), [](const auto &a, const auto &b) { return a.first() < b.first(); });
			health.disconnectedFans << std::move(found);
		}
	}
	for (auto &[key, faces] : duplicates)
	{
		Q_UNUSED(key);
		if (!work.step())
		{
			return false;
		}
		if (faces.size() > 1)
		{
			health.duplicateFaces << std::move(faces);
		}
	}
	std::sort(health.duplicateFaces.begin(), health.duplicateFaces.end(),
			  [](const auto &a, const auto &b) { return a.first() < b.first(); });
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		health.faceComponents += components.root(face) == face;
	}
	if (!work.check())
	{
		return false;
	}
	*result = std::move(health);
	return true;
}

bool repairModelTopology(const ModelSurface &source, ModelTopologyRepair repair, int vertexLimit, ModelSurface *result,
						 QVector<QVector<int>> *vertexMap, QVector<int> *faceMap, QString *error, const ModelWorkControl &control)
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
	{
		return false;
	}
	if (!result || !vertexMap || !faceMap || source.texCoords.size() != source.vertexCount || source.frames.isEmpty() ||
		std::any_of(source.frames.begin(), source.frames.end(),
					[&](const auto &f) { return f.positions.size() != source.vertexCount || f.normals.size() != source.vertexCount; }))
	{
		return fail(error,
					QT_TRANSLATE_NOOP("VibeStudioModelDocument", "Topology repair requires complete editable geometry and output maps."));
	}
	ModelTopologyHealth health;
	if (!inspectModelTopology(source, &health, error, control))
	{
		return false;
	}
	ModelSurface candidate = source;
	QVector<QVector<int>> vertices(source.vertexCount);
	for (int i = 0; i < vertices.size(); ++i)
	{
		if (!work.step())
		{
			return false;
		}
		vertices[i] = {i};
	}
	QVector<int> faces(source.triangles.size());
	std::iota(faces.begin(), faces.end(), 0);
	bool changed = false;
	switch (repair)
	{
	case ModelTopologyRepair::RemoveDuplicateFaces: {
		for (const auto &group : health.duplicateFaces)
		{
			for (int face : group)
			{
				if (!work.step())
				{
					return false;
				}
				faces[face] = group.first();
			}
		}
		candidate.triangles.clear();
		for (int f = 0; f < source.triangles.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			if (faces[f] == f)
			{
				faces[f] = int(candidate.triangles.size());
				candidate.triangles << source.triangles[f];
			}
			else
			{
				faces[f] = faces[faces[f]];
			}
		}
		changed = !health.duplicateFaces.isEmpty();
		break;
	}
	case ModelTopologyRepair::RemoveUnusedVertices: {
		QSet<int> unused(health.unusedVertices.begin(), health.unusedVertices.end());
		QVector<int> retained;
		for (int old = 0; old < source.vertexCount; ++old)
		{
			if (!work.step())
			{
				return false;
			}
			vertices[old].clear();
			if (!unused.contains(old))
			{
				vertices[old] << int(retained.size());
				retained << old;
			}
		}
		candidate.texCoords.clear();
		for (int old : retained)
		{
			if (!work.step())
			{
				return false;
			}
			candidate.texCoords << source.texCoords[old];
		}
		for (int f = 0; f < source.frames.size(); ++f)
		{
			auto &frame = candidate.frames[f];
			frame.positions.clear();
			frame.normals.clear();
			for (int old : retained)
			{
				if (!work.step())
				{
					return false;
				}
				frame.positions << source.frames[f].positions[old];
				frame.normals << source.frames[f].normals[old];
			}
		}
		for (auto &t : candidate.triangles)
		{
			if (!work.step())
			{
				return false;
			}
			t = {vertices[t.a].first(), vertices[t.b].first(), vertices[t.c].first()};
		}
		changed = !health.unusedVertices.isEmpty();
		break;
	}
	case ModelTopologyRepair::SplitDisconnectedFans:
	case ModelTopologyRepair::SplitNonmanifoldEdges: {
		const bool cutEdges = repair == ModelTopologyRepair::SplitNonmanifoldEdges;
		QVector<ModelVertexFans> splits;
		if (cutEdges)
		{
			if (health.nonmanifoldEdges.isEmpty())
			{
				break;
			}
			if (!nonmanifoldCuts(source, health, &splits, work))
			{
				return false;
			}
		}
		else
		{
			splits = health.disconnectedFans;
		}
		qint64 required = source.vertexCount;
		for (const auto &fan : splits)
		{
			if (!work.step())
			{
				return false;
			}
			required += fan.faces.size() - 1;
		}
		if (required > vertexLimit || required > 65536 || required * source.frames.size() > 1048576)
		{
			return fail(error, cutEdges
								   ? QT_TRANSLATE_NOOP("VibeStudioModelDocument",
													   "Splitting nonmanifold edges would exceed the vertex or animation storage limit.")
								   : QT_TRANSLATE_NOOP("VibeStudioModelDocument",
													   "Splitting disconnected fans would exceed the vertex or animation storage limit."));
		}
		if (!splits.isEmpty())
		{
			candidate.texCoords.reserve(int(required));
			for (auto &frame : candidate.frames)
			{
				if (!work.step())
				{
					return false;
				}
				frame.positions.reserve(int(required));
				frame.normals.reserve(int(required));
			}
		}
		for (const auto &fan : splits)
		{
			for (int group = 1; group < fan.faces.size(); ++group)
			{
				const int copy = int(candidate.texCoords.size());
				vertices[fan.vertex] << copy;
				candidate.texCoords << source.texCoords[fan.vertex];
				for (auto &frame : candidate.frames)
				{
					if (!work.step())
					{
						return false;
					}
					frame.positions << frame.positions[fan.vertex];
					frame.normals << frame.normals[fan.vertex];
				}
				for (int f : fan.faces[group])
				{
					if (!work.step())
					{
						return false;
					}
					replace(candidate.triangles[f], fan.vertex, copy);
				}
			}
		}
		changed = !splits.isEmpty();
		break;
	}
	case ModelTopologyRepair::OrientFaces: {
		if (!health.duplicateFaces.isEmpty() || !health.nonmanifoldEdges.isEmpty())
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument",
												 "Remove duplicate faces and resolve nonmanifold edges before orienting this surface."));
		}
		QVector<QVector<QPair<int, bool>>> neighbours(source.triangles.size());
		for (const auto &edge : health.edges)
		{
			if (!work.step())
			{
				return false;
			}
			if (edge.faces.size() == 2)
			{
				neighbours[edge.faces[0]] << qMakePair(edge.faces[1], edge.inconsistentWinding);
				neighbours[edge.faces[1]] << qMakePair(edge.faces[0], edge.inconsistentWinding);
			}
		}
		QVector<int> flipped(source.triangles.size(), -1), queue;
		for (int seed = 0; seed < source.triangles.size(); ++seed)
		{
			if (!work.step())
			{
				return false;
			}
			if (flipped[seed] >= 0)
			{
				continue;
			}
			flipped[seed] = 0;
			queue = {seed};
			for (int q = 0; q < queue.size(); ++q)
			{
				if (!work.step())
				{
					return false;
				}
				const int f = queue[q];
				for (const auto &[other, opposite] : neighbours[f])
				{
					const int expected = flipped[f] ^ int(opposite);
					if (flipped[other] < 0)
					{
						flipped[other] = expected;
						queue << other;
					}
					else if (flipped[other] != expected)
					{
						return fail(error,
									QT_TRANSLATE_NOOP(
										"VibeStudioModelDocument",
										"This surface cannot be oriented consistently without cutting edges. No faces were changed."));
					}
				}
			}
		}
		for (int f = 0; f < flipped.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			if (flipped[f] == 1)
			{
				std::swap(candidate.triangles[f].b, candidate.triangles[f].c);
				changed = true;
			}
		}
		break;
	}
	default:
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument", "Unknown topology repair."));
	}
	if (!changed)
	{
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument", "This surface has no topology findings for the chosen repair."));
	}
	// Remap marks through each face corner, avoiding a cartesian product of fan
	// copies. Orientation changes preserve endpoint identities directly.
	if (repair != ModelTopologyRepair::OrientFaces)
	{
		candidate.uvSeams.clear();
		for (int f = 0; f < source.triangles.size(); ++f)
		{
			if (!work.step())
			{
				return false;
			}
			const auto old = corners(source.triangles[f]), now = corners(candidate.triangles[faces[f]]);
			for (int c = 0; c < 3; ++c)
			{
				if (source.uvSeams.contains(modelEdge(old[c], old[(c + 1) % 3])))
				{
					// Duplicate representatives can have a different corner order.
					const auto edge = repair == ModelTopologyRepair::RemoveDuplicateFaces ? modelEdge(old[c], old[(c + 1) % 3])
																						  : modelEdge(now[c], now[(c + 1) % 3]);
					candidate.uvSeams.insert(edge);
				}
			}
		}
	}
	candidate.vertexCount = int(candidate.texCoords.size());
	if (repair == ModelTopologyRepair::SplitNonmanifoldEdges)
	{
		ModelTopologyHealth checked;
		if (!inspectModelTopology(candidate, &checked, error, control))
		{
			return false;
		}
		if (!checked.nonmanifoldEdges.isEmpty())
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument",
												 "The proposed split retains a nonmanifold edge. No geometry was changed."));
		}
	}
	if (!work.check())
	{
		return false;
	}
	*result = std::move(candidate);
	*vertexMap = std::move(vertices);
	*faceMap = std::move(faces);
	return true;
}
} // namespace vibestudio
