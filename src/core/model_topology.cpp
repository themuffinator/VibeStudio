#include "core/model_topology.h"

#include <QCoreApplication>
#include <QMap>

#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>

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
using Cell = std::array<qint64, 3>;
Cell cellAt(ModelVec3 p, double distance)
{
	const auto part = [distance](float v) -> qint64
	{ return distance == 0 ? (v == 0 ? 0 : std::bit_cast<quint32>(v)) : qint64(std::floor(double(v) / distance)); };
	return {part(p.x), part(p.y), part(p.z)};
}
bool same(ModelVec3 a, ModelVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
struct Incidence
{
	int faces = 0, direction = 0;
};
struct Topology
{
	QMap<ModelEdge, Incidence> edges;
	std::map<std::array<int, 3>, int> faces;
};
bool topology(const QVector<ModelTriangle> &triangles, Topology *out, ModelWorkProgress &work)
{
	for (const auto &t : triangles)
	{
		if (!work.step())
		{
			return false;
		}
		std::array<int, 3> face{t.a, t.b, t.c};
		std::sort(face.begin(), face.end());
		++out->faces[face];
		for (auto e : {ModelEdge{t.a, t.b}, ModelEdge{t.b, t.c}, ModelEdge{t.c, t.a}})
		{
			auto &count = out->edges[modelEdge(e.first, e.second)];
			++count.faces;
			count.direction += e.first < e.second ? 1 : -1;
		}
	}
	return work.check();
}
} // namespace

QVector<ModelEdge> modelSurfaceEdges(const ModelSurface &surface, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, nullptr);
	QSet<ModelEdge> edges;
	for (const auto &t : surface.triangles)
	{
		if (!work.step())
		{
			return {};
		}
		for (auto e : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
		{
			if (e.first >= 0 && e.first < e.second && e.second < surface.vertexCount)
			{
				edges.insert(e);
			}
		}
	}
	auto result = edges.values().toVector();
	std::sort(result.begin(), result.end());
	return work.check() ? result : QVector<ModelEdge>();
}

qint64 ModelSurfaceTopology::storageBytes() const
{
	// Triangles share the mesh allocation already counted by document history.
	return edges.size() * qint64(sizeof(ModelEdge)) + faceUses.size() * qint64(sizeof(ModelEdge) + sizeof(int) + 3 * sizeof(void *)) +
		   allEdges.size() * qint64(sizeof(ModelEdge) + 3 * sizeof(void *)) +
		   edgeVertices.size() * qint64(sizeof(int) + 3 * sizeof(void *));
}

bool prepareModelSurfaceTopology(const ModelSurface &surface, ModelSurfaceTopology *index, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!index || !work.check())
	{
		return false;
	}
	if (index->vertexCount == surface.vertexCount && index->triangles.constData() == surface.triangles.constData())
	{
		return true;
	}
	ModelSurfaceTopology candidate;
	candidate.triangles = surface.triangles;
	candidate.vertexCount = surface.vertexCount;
	candidate.faceUses.reserve(surface.triangles.size() * 3);
	for (const auto &triangle : surface.triangles)
	{
		if (!work.step())
		{
			return false;
		}
		for (const auto &edge : {modelEdge(triangle.a, triangle.b), modelEdge(triangle.b, triangle.c), modelEdge(triangle.c, triangle.a)})
		{
			if (edge.first >= 0 && edge.first < edge.second && edge.second < surface.vertexCount)
			{
				++candidate.faceUses[edge];
			}
		}
	}
	candidate.edges = candidate.faceUses.keys();
	std::sort(candidate.edges.begin(), candidate.edges.end());
	candidate.allEdges.reserve(candidate.edges.size());
	candidate.edgeVertices.reserve(std::max(qsizetype(0), std::min(qsizetype(surface.vertexCount), candidate.edges.size() * 2)));
	for (const auto &edge : std::as_const(candidate.edges))
	{
		if (!work.step())
		{
			return false;
		}
		candidate.allEdges.insert(edge);
		candidate.edgeVertices.insert(edge.first);
		candidate.edgeVertices.insert(edge.second);
	}
	if (!work.check())
	{
		return false;
	}
	*index = std::move(candidate);
	return true;
}

bool weldModelSurface(ModelSurface *surface, const QSet<int> &vertices, double distance, bool preserveSeams, QVector<int> *vertexMap,
					  QVector<int> *faceMap, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
	{
		return false;
	}
	if (!surface || !vertexMap || !faceMap || surface->frames.isEmpty() || vertices.size() < 2 || !std::isfinite(distance) ||
		distance < 0 || distance > 1000000 || (distance > 0 && distance < 0.000001))
	{
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument",
											 "Welding requires at least two vertices and a distance of zero or 0.000001–1,000,000 units."));
	}
	auto chosen = vertices.values().toVector();
	std::sort(chosen.begin(), chosen.end());
	if (chosen.first() < 0 || chosen.last() >= surface->vertexCount)
	{
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument", "A selected weld vertex no longer exists."));
	}
	QVector<int> mapping(surface->vertexCount);
	std::iota(mapping.begin(), mapping.end(), 0);
	std::map<Cell, QVector<int>> cells;
	qint64 comparisons = 0;
	bool exhausted = false;
	const auto compatible = [&](int a, int b)
	{
		if (++comparisons > 4194304 || !work.step())
		{
			exhausted = true;
			return false;
		}
		const auto u = surface->texCoords[a], v = surface->texCoords[b];
		if (preserveSeams && (u.u != v.u || u.v != v.v))
		{
			return false;
		}
		for (const auto &frame : surface->frames)
		{
			if (++comparisons > 4194304 || !work.step())
			{
				exhausted = true;
				return false;
			}
			const auto p = frame.positions[a], q = frame.positions[b];
			const double x = double(p.x) - q.x, y = double(p.y) - q.y, z = double(p.z) - q.z;
			if (x * x + y * y + z * z > distance * distance || (preserveSeams && !same(frame.normals[a], frame.normals[b])))
			{
				return false;
			}
		}
		return true;
	};
	int merged = 0;
	for (int vertex : chosen)
	{
		if (!work.step())
		{
			return false;
		}
		const auto cell = cellAt(surface->frames.first().positions[vertex], distance);
		int anchor = vertex;
		const int radius = distance == 0 ? 0 : 1;
		for (int x = -radius; x <= radius; ++x)
		{
			for (int y = -radius; y <= radius; ++y)
			{
				for (int z = -radius; z <= radius; ++z)
				{
					const auto found = cells.find({cell[0] + x, cell[1] + y, cell[2] + z});
					if (found == cells.end())
					{
						continue;
					}
					for (int candidate : found->second)
					{
						if (candidate >= anchor)
						{
							break;
						}
						if (compatible(candidate, vertex))
						{
							anchor = candidate;
							break;
						}
						if (exhausted)
						{
							if (!work.check())
							{
								return false;
							}
							return fail(error, QT_TRANSLATE_NOOP(
												   "VibeStudioModelDocument",
												   "Weld search exceeded its comparison budget. Use a smaller selection or distance."));
						}
					}
				}
			}
		}
		mapping[vertex] = anchor;
		if (anchor == vertex)
		{
			cells[cell].append(vertex);
		}
		else
		{
			++merged;
		}
	}
	if (!merged)
	{
		return fail(error,
					QT_TRANSLATE_NOOP("VibeStudioModelDocument",
									  "No vertices can weld within this distance in every frame while respecting the seam setting."));
	}
	QVector<ModelTriangle> triangles;
	QVector<int> faces(surface->triangles.size(), -1);
	for (int i = 0; i < surface->triangles.size(); ++i)
	{
		if (!work.step())
		{
			return false;
		}
		const auto source = surface->triangles[i];
		const ModelTriangle t{mapping[source.a], mapping[source.b], mapping[source.c]};
		if (t.a == t.b || t.b == t.c || t.c == t.a)
		{
			continue;
		}
		faces[i] = triangles.size();
		triangles << t;
	}
	if (triangles.isEmpty())
	{
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument", "Welding would remove every face of the surface."));
	}
	Topology before, after;
	if (!topology(surface->triangles, &before, work) || !topology(triangles, &after, work))
	{
		return false;
	}
	for (const auto &[face, count] : after.faces)
	{
		if (!work.step())
		{
			return false;
		}
		if (count > 1 && count > before.faces[face])
		{
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioModelDocument",
												 "Welding would introduce duplicate faces. Reduce the selection or distance."));
		}
	}
	for (auto it = after.edges.cbegin(); it != after.edges.cend(); ++it)
	{
		if (!work.step())
		{
			return false;
		}
		const auto old = before.edges.value(it.key()), now = it.value();
		if ((now.faces > 2 && now.faces > old.faces) ||
			(now.faces == 2 && std::abs(now.direction) == 2 && !(old.faces == 2 && std::abs(old.direction) == 2)))
		{
			return fail(
				error,
				QT_TRANSLATE_NOOP(
					"VibeStudioModelDocument",
					"Welding would introduce a nonmanifold edge or inconsistent winding. Reduce the selection or repair the faces first."));
		}
	}
	if (!work.check())
	{
		return false;
	}
	QSet<ModelEdge> seams;
	for (auto edge : surface->uvSeams)
	{
		if (!work.step())
		{
			return false;
		}
		const auto mapped = modelEdge(mapping[edge.first], mapping[edge.second]);
		if (mapped.first != mapped.second && after.edges.contains(mapped))
		{
			seams.insert(mapped);
		}
	}
	if (!work.check())
	{
		return false;
	}
	surface->triangles = std::move(triangles);
	surface->uvSeams = std::move(seams);
	*vertexMap = std::move(mapping);
	*faceMap = std::move(faces);
	return true;
}
} // namespace vibestudio
