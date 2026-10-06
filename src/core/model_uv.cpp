#include "core/model_uv.h"

#include "core/model_document.h"

#include <QCoreApplication>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace vibestudio
{
namespace
{
bool invalid(QString *error)
{
	if (error)
	{
		*error = QCoreApplication::translate("VibeStudioModelUv", "UV topology contains invalid coordinates, edges, or faces.");
	}
	return false;
}
int rootOf(QVector<int> &parents, int face)
{
	while (parents[face] != face)
	{
		parents[face] = parents[parents[face]];
		face = parents[face];
	}
	return face;
}
} // namespace

bool buildModelUvTopology(const ModelSurface &surface, ModelUvTopology *result, QString *error, const ModelWorkControl &control)
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
	if (!result || surface.vertexCount < 3 || surface.vertexCount > modelDocumentMaxVertices ||
		surface.texCoords.size() != surface.vertexCount || surface.triangles.isEmpty() ||
		surface.triangles.size() > modelDocumentMaxTriangles || surface.uvSeams.size() > surface.triangles.size() * 3)
	{
		return invalid(error);
	}
	for (const auto uv : surface.texCoords)
	{
		if (!work.step())
		{
			return false;
		}
		if (!std::isfinite(uv.u) || !std::isfinite(uv.v) || std::abs(uv.u) > 1000000 || std::abs(uv.v) > 1000000)
		{
			return invalid(error);
		}
	}
	QMap<ModelEdge, QVector<int>> incidence;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		const auto t = surface.triangles[face];
		if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= surface.vertexCount || t.b >= surface.vertexCount || t.c >= surface.vertexCount ||
			t.a == t.b || t.a == t.c || t.b == t.c)
		{
			return invalid(error);
		}
		for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
		{
			incidence[edge].append(face);
		}
	}
	for (auto seam : surface.uvSeams)
	{
		if (!work.step())
		{
			return false;
		}
		if (seam.first >= seam.second || !incidence.contains(seam))
		{
			return invalid(error);
		}
	}
	ModelUvTopology candidate;
	QVector<int> parents(surface.triangles.size());
	std::iota(parents.begin(), parents.end(), 0);
	for (auto it = incidence.cbegin(); it != incidence.cend(); ++it)
	{
		if (!work.step())
		{
			return false;
		}
		const bool seam = surface.uvSeams.contains(it.key());
		candidate.edges.append({it.key(), it.value(), seam});
		if (!seam && it.value().size() == 2)
		{
			const int a = rootOf(parents, it.value()[0]), b = rootOf(parents, it.value()[1]);
			parents[std::max(a, b)] = std::min(a, b);
		}
	}
	QMap<int, int> groups;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		const int root = rootOf(parents, face);
		if (!groups.contains(root))
		{
			groups.insert(root, candidate.islands.size());
			candidate.islands.append(ModelUvIsland{});
		}
		const int index = groups.value(root);
		candidate.faceIsland.append(index);
		auto &island = candidate.islands[index];
		const auto t = surface.triangles[face];
		if (island.faces.isEmpty())
		{
			island.mins = island.maxs = surface.texCoords[t.a];
		}
		island.faces.append(face);
		for (int vertex : {t.a, t.b, t.c})
		{
			island.vertices.append(vertex);
			const auto uv = surface.texCoords[vertex];
			island.mins.u = std::min(island.mins.u, uv.u);
			island.mins.v = std::min(island.mins.v, uv.v);
			island.maxs.u = std::max(island.maxs.u, uv.u);
			island.maxs.v = std::max(island.maxs.v, uv.v);
		}
	}
	for (auto &island : candidate.islands)
	{
		if (!work.step())
		{
			return false;
		}
		std::sort(island.vertices.begin(), island.vertices.end());
		island.vertices.erase(std::unique(island.vertices.begin(), island.vertices.end()), island.vertices.end());
	}
	if (!work.check())
	{
		return false;
	}
	*result = std::move(candidate);
	return true;
}

bool expandModelUvIslands(const ModelSurface &surface, const ModelUvTopology &topology, const QSet<int> &faces, const QSet<int> &vertices,
						  const QSet<ModelEdge> &edges, QSet<int> *result, QString *error, const ModelWorkControl &control)
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
	if (!result || topology.faceIsland.size() != surface.triangles.size())
	{
		return invalid(error);
	}
	QSet<int> touched;
	for (int face : faces)
	{
		if (!work.step())
		{
			return false;
		}
		if (face < 0 || face >= topology.faceIsland.size())
		{
			return invalid(error);
		}
		touched.insert(topology.faceIsland[face]);
	}
	for (int vertex : vertices)
	{
		if (!work.step())
		{
			return false;
		}
		if (vertex < 0 || vertex >= surface.vertexCount)
		{
			return invalid(error);
		}
	}
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		const auto t = surface.triangles[face];
		if (vertices.contains(t.a) || vertices.contains(t.b) || vertices.contains(t.c))
		{
			touched.insert(topology.faceIsland[face]);
		}
	}
	QSet<ModelEdge> found;
	for (const auto &edge : topology.edges)
	{
		if (!work.step())
		{
			return false;
		}
		if (edges.contains(edge.vertices))
		{
			found.insert(edge.vertices);
			for (int face : edge.faces)
			{
				if (face < 0 || face >= topology.faceIsland.size())
				{
					return invalid(error);
				}
				touched.insert(topology.faceIsland[face]);
			}
		}
	}
	if (found != edges)
	{
		return invalid(error);
	}
	QSet<int> selected;
	for (int island : touched)
	{
		if (island < 0 || island >= topology.islands.size())
		{
			return invalid(error);
		}
		for (int face : topology.islands[island].faces)
		{
			if (!work.step())
			{
				return false;
			}
			if (face < 0 || face >= surface.triangles.size() || topology.faceIsland[face] != island)
			{
				return invalid(error);
			}
			selected.insert(face);
		}
	}
	if (!work.check())
	{
		return false;
	}
	*result = std::move(selected);
	return true;
}
} // namespace vibestudio
