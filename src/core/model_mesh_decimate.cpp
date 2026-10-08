#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <set>

// Quadric error metric simplification after Garland and Heckbert, "Surface
// Simplification Using Quadric Error Metrics" (SIGGRAPH 1997). Collapses are
// half-edge collapses (u moves onto v) so every animation pose keeps stored
// positions, and quadrics sum sampled poses so animated shape is respected.

namespace vibestudio::model_tools
{
namespace
{
struct Quadric
{
	// xx, xy, xz, xw, yy, yz, yw, zz, zw, ww
	std::array<double, 10> a{};
	void addPlane(P3 n, double d, double weight)
	{
		a[0] += weight * n.x * n.x;
		a[1] += weight * n.x * n.y;
		a[2] += weight * n.x * n.z;
		a[3] += weight * n.x * d;
		a[4] += weight * n.y * n.y;
		a[5] += weight * n.y * n.z;
		a[6] += weight * n.y * d;
		a[7] += weight * n.z * n.z;
		a[8] += weight * n.z * d;
		a[9] += weight * d * d;
	}
	[[nodiscard]] double evaluate(P3 p) const
	{
		const double x = p.x, y = p.y, z = p.z;
		return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x + a[4] * y * y + 2 * a[5] * y * z + 2 * a[6] * y +
			   a[7] * z * z + 2 * a[8] * z + a[9];
	}
	Quadric &operator+=(const Quadric &other)
	{
		for (int i = 0; i < 10; ++i)
			a[i] += other.a[i];
		return *this;
	}
};
struct Candidate
{
	double cost;
	int from, to;
	quint64 fromStamp, toStamp;
	bool operator>(const Candidate &other) const
	{
		if (cost != other.cost)
			return cost > other.cost;
		return std::pair(from, to) > std::pair(other.from, other.to);
	}
};
std::array<int, 3> sortedCorners(const ModelTriangle &t)
{
	std::array<int, 3> key{t.a, t.b, t.c};
	std::sort(key.begin(), key.end());
	return key;
}
} // namespace

bool decimate(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto faces = selection->faces.values().toVector();
	std::sort(faces.begin(), faces.end());
	if (faces.isEmpty())
		return fail(error, Text::tr("Select the faces to decimate (Select All for the whole surface)."));
	const auto &options = edit.tool;
	const int regionCount = faces.size();
	const int target = options.targetTriangles > 0 ? options.targetTriangles : int(std::ceil(regionCount * options.ratio));
	if (target >= regionCount)
		return fail(error, Text::tr("The selection already has no more than %1 triangles.").arg(target));
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const int vertexCount = surface.vertexCount, frameCount = surface.frames.size();
	QVector<bool> inRegion(surface.triangles.size(), false);
	for (int face : std::as_const(faces))
		inRegion[face] = true;
	// Live adjacency over vertex indices.
	QVector<QVector<int>> vertexFaces(vertexCount);
	QVector<QSet<int>> neighbours(vertexCount);
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto &t = surface.triangles[face];
		for (int v : {t.a, t.b, t.c})
			vertexFaces[v].append(face);
		neighbours[t.a] << t.b << t.c;
		neighbours[t.b] << t.a << t.c;
		neighbours[t.c] << t.a << t.b;
	}
	QSet<int> boundaryGroups, branchingGroups;
	for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
	{
		if (it.value().size() == 1)
			boundaryGroups << it.key().first << it.key().second;
		else if (it.value().size() > 2)
			branchingGroups << it.key().first << it.key().second;
	}
	QVector<bool> eligible(vertexCount, false);
	for (int v = 0; v < vertexCount; ++v)
	{
		if (vertexFaces[v].isEmpty() || topology.copies[topology.group(v)].size() != 1 || branchingGroups.contains(topology.group(v)) ||
			(options.preserveBoundary && boundaryGroups.contains(topology.group(v))))
			continue;
		eligible[v] = std::all_of(vertexFaces[v].cbegin(), vertexFaces[v].cend(), [&](int face) { return inRegion[face]; });
	}
	// Quadrics over up to eight evenly spaced poses, always including the reference.
	QVector<int> samples;
	const int sampleCount = std::min(frameCount, 8);
	for (int i = 0; i < sampleCount; ++i)
		samples.append(sampleCount == 1 ? options.referenceFrame : int(std::round(double(i) * (frameCount - 1) / (sampleCount - 1))));
	if (!samples.contains(options.referenceFrame))
		samples.last() = options.referenceFrame;
	const int k = samples.size();
	QVector<Quadric> quadrics(qsizetype(vertexCount) * k);
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface.triangles[face];
		for (int s = 0; s < k; ++s)
		{
			const P3 c = faceCross(surface, samples[s], t);
			const double area = length(c) / 2;
			if (area <= 0)
				continue;
			const P3 n = unit(c);
			const double d = -dot(n, position(surface, samples[s], t.a));
			Quadric q;
			q.addPlane(n, d, area);
			for (int v : {t.a, t.b, t.c})
				quadrics[qsizetype(v) * k + s] += q;
		}
	}
	const auto cost = [&](int from, int to)
	{
		double total = 0;
		for (int s = 0; s < k; ++s)
		{
			Quadric q = quadrics[qsizetype(from) * k + s];
			q += quadrics[qsizetype(to) * k + s];
			total += std::max(0.0, q.evaluate(position(surface, samples[s], to)));
		}
		return total;
	};
	QVector<quint64> stamps(vertexCount, 0);
	QVector<bool> alive(vertexCount, true), dead(surface.triangles.size(), false);
	std::priority_queue<Candidate, std::vector<Candidate>, std::greater<>> queue;
	const auto push = [&](int from, int to)
	{
		if (eligible[from] && alive[from] && alive[to] && from != to)
			queue.push({cost(from, to), from, to, stamps[from], stamps[to]});
	};
	for (int v = 0; v < vertexCount; ++v)
	{
		if (!work.step())
			return false;
		if (eligible[v])
		{
			for (int other : std::as_const(neighbours[v]))
				push(v, other);
		}
	}
	std::set<std::array<int, 3>> live;
	for (const auto &t : std::as_const(surface.triangles))
		live.insert(sortedCorners(t));
	QVector<int> collapsedTo(vertexCount, -1);
	int remaining = regionCount, collapses = 0;
	while (remaining > target && !queue.empty())
	{
		if (!work.step())
			return false;
		const auto candidate = queue.top();
		queue.pop();
		const int u = candidate.from, v = candidate.to;
		if (!alive[u] || !alive[v] || candidate.fromStamp != stamps[u] || candidate.toStamp != stamps[v] || !neighbours[u].contains(v))
			continue;
		// Link condition: shared neighbours must be exactly the corners opposite
		// the shared edge, or the collapse would pinch the surface.
		QSet<int> opposite;
		QVector<int> removedFaces, movedFaces;
		for (int face : std::as_const(vertexFaces[u]))
		{
			if (dead[face])
				continue;
			const auto &t = surface.triangles[face];
			if (t.a == v || t.b == v || t.c == v)
			{
				removedFaces.append(face);
				for (int corner : {t.a, t.b, t.c})
				{
					if (corner != u && corner != v)
						opposite.insert(corner);
				}
			}
			else
				movedFaces.append(face);
		}
		if (removedFaces.isEmpty() || opposite.size() != removedFaces.size())
			continue;
		const auto shared = neighbours[u] & neighbours[v];
		if (shared != opposite)
			continue;
		bool valid = true;
		QVector<ModelTriangle> replacements;
		for (int face : std::as_const(movedFaces))
		{
			const auto before = surface.triangles[face];
			auto after = before;
			if (after.a == u)
				after.a = v;
			else if (after.b == u)
				after.b = v;
			else
				after.c = v;
			if (live.count(sortedCorners(after)) || collapsedInAnyFrame(surface, after))
			{
				valid = false;
				break;
			}
			for (int frame = 0; frame < frameCount && valid; ++frame)
			{
				const P3 oldNormal = unit(faceCross(surface, frame, before)), newNormal = unit(faceCross(surface, frame, after));
				if (dot(oldNormal, newNormal) <= 0.05)
					valid = false;
			}
			replacements.append(after);
		}
		if (!valid)
			continue;
		for (int face : std::as_const(removedFaces))
		{
			dead[face] = true;
			live.erase(sortedCorners(surface.triangles[face]));
			remaining -= inRegion[face];
		}
		for (int i = 0; i < movedFaces.size(); ++i)
		{
			const int face = movedFaces[i];
			live.erase(sortedCorners(surface.triangles[face]));
			surface.triangles[face] = replacements[i];
			live.insert(sortedCorners(replacements[i]));
			vertexFaces[v].append(face);
		}
		for (int other : std::as_const(neighbours[u]))
		{
			neighbours[other].remove(u);
			if (other != v)
			{
				neighbours[other].insert(v);
				neighbours[v].insert(other);
			}
		}
		neighbours[v].remove(u);
		neighbours[u].clear();
		vertexFaces[u].clear();
		for (int s = 0; s < k; ++s)
			quadrics[qsizetype(v) * k + s] += quadrics[qsizetype(u) * k + s];
		alive[u] = false;
		collapsedTo[u] = v;
		++collapses;
		++stamps[v];
		for (int other : std::as_const(neighbours[v]))
		{
			push(other, v);
			push(v, other);
		}
	}
	if (collapses == 0)
		return fail(error, Text::tr("Nothing could be collapsed without changing UV seams, borders, or the shape of an animation pose."));
	// Rebuild faces, carry seam marks through the collapses and compact.
	const auto resolve = [&](int v)
	{
		while (collapsedTo[v] >= 0)
			v = collapsedTo[v];
		return v;
	};
	QVector<ModelTriangle> triangles;
	QSet<int> result;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (dead[face])
			continue;
		if (inRegion[face])
			result.insert(triangles.size());
		triangles.append(surface.triangles[face]);
	}
	surface.triangles = std::move(triangles);
	QSet<ModelEdge> seams;
	for (auto seam : std::as_const(surface.uvSeams))
	{
		const int a = resolve(seam.first), b = resolve(seam.second);
		if (a != b)
			seams.insert(modelEdge(a, b));
	}
	surface.uvSeams = seams;
	pruneSeams(&surface);
	QSet<int> removed;
	for (int v = 0; v < vertexCount; ++v)
	{
		if (!alive[v])
			removed.insert(v);
	}
	selection->faces = result;
	selection->vertices.clear();
	selection->edges.clear();
	if (!compactUnused(&surface, removed, selection, work))
		return false;
	QSet<int> refresh;
	for (int face : std::as_const(selection->faces))
	{
		const auto &t = surface.triangles[face];
		refresh << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, refresh, work);
}
} // namespace vibestudio::model_tools
