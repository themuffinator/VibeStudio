#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>

namespace vibestudio::model_tools
{
namespace
{
QVector<int> sortedValues(const QSet<int> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
double modelScale(const ModelSurface &surface, int frame)
{
	double scale = 1;
	for (const auto &p : surface.frames[frame].positions)
		scale = std::max({scale, double(std::abs(p.x)), double(std::abs(p.y)), double(std::abs(p.z))});
	return scale;
}
// Removes faces flagged in `drop`, compacting vertices only they used, and
// returns the new index of every kept face (or -1).
QVector<int> removeFaces(ModelSurface *surface, const QVector<bool> &drop, ModelSelection *selection, ModelWorkProgress &work, bool *ok)
{
	QSet<int> candidates;
	QVector<int> mapping(surface->triangles.size(), -1);
	QVector<ModelTriangle> kept;
	for (int face = 0; face < surface->triangles.size(); ++face)
	{
		const auto &t = surface->triangles[face];
		if (drop.value(face))
		{
			candidates << t.a << t.b << t.c;
			continue;
		}
		mapping[face] = kept.size();
		kept.append(t);
	}
	surface->triangles = std::move(kept);
	pruneSeams(surface);
	*ok = compactUnused(surface, candidates, selection, work);
	return mapping;
}
} // namespace

bool bisectFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select the faces to bisect."));
	const auto &options = edit.tool;
	const P3 normal = unit(arrayPoint(options.normal)), origin = arrayPoint(options.point);
	if (length(normal) <= 0)
		return fail(error, Text::tr("The bisect plane needs a nonzero normal."));
	auto &surface = mesh->surfaces[selection->surface];
	const int frame = options.referenceFrame;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const double epsilon = modelScale(surface, frame) * 1e-6;
	const auto distance = [&](int v)
	{
		const double d = dot(position(surface, frame, v) - origin, normal);
		return std::abs(d) <= epsilon ? 0.0 : d;
	};
	QHash<ModelEdge, double> cuts;
	QSet<int> onPlane;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const double da = distance(a), db = distance(b);
			if (da == 0)
				onPlane.insert(a);
			if (da * db >= 0)
				continue;
			const int low = topology.group(a) < topology.group(b) ? a : b;
			const double dl = low == a ? da : db, dh = low == a ? db : da;
			cuts.insert(modelEdge(a, b), dl / (dl - dh));
		}
	}
	if (cuts.isEmpty() && onPlane.isEmpty())
		return fail(error, Text::tr("The plane does not cross the selected faces."));
	if (!reserveCapacity(*mesh, selection->surface, cuts.size(), qint64(cuts.size()) * 2, error))
		return false;
	QHash<ModelEdge, int> created;
	QVector<int> sources;
	if (!splitSurfaceEdges(&surface, topology, cuts, frame, &created, &sources, work))
		return false;
	for (int v : created)
		onPlane.insert(v);
	const QSet<int> chosen(faces.cbegin(), faces.cend());
	QVector<bool> drop(surface.triangles.size(), false);
	QSet<int> derived;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!chosen.contains(sources[face]))
			continue;
		derived.insert(face);
		if (options.keep == ModelBisectKeep::Both)
			continue;
		const auto &t = surface.triangles[face];
		const P3 centre = (position(surface, frame, t.a) + position(surface, frame, t.b) + position(surface, frame, t.c)) * (1.0 / 3);
		const double side = dot(centre - origin, normal);
		drop[face] = options.keep == ModelBisectKeep::Front ? side < 0 : side > 0;
	}
	QSet<ModelEdge> cutEdges;
	for (int face : std::as_const(derived))
	{
		if (drop[face])
			continue;
		const auto &t = surface.triangles[face];
		for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			if (onPlane.contains(a) && onPlane.contains(b))
				cutEdges.insert(modelEdge(a, b));
		}
	}
	selection->faces.clear();
	selection->vertices.clear();
	selection->edges = cutEdges;
	bool ok = true;
	removeFaces(&surface, drop, selection, work, &ok);
	if (!ok)
		return false;
	if (surface.triangles.isEmpty())
		return fail(error, Text::tr("Bisecting would remove every face of the surface."));
	return work.check();
}

bool symmetrize(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select the faces to symmetrize (Select All for the whole surface)."));
	const auto &options = edit.tool;
	const int axis = options.axis, frame = options.referenceFrame;
	const double sign = options.positiveToNegative ? 1 : -1;
	auto &surface = mesh->surfaces[selection->surface];
	const double threshold = std::max(options.mergeThreshold, modelScale(surface, frame) * 1e-9);
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto coordinate = [&](int v)
	{
		const auto p = surface.frames[frame].positions[v];
		return double(axis == 0 ? p.x : axis == 1 ? p.y : p.z);
	};
	// Cut faces that cross the mirror plane.
	QHash<ModelEdge, double> cuts;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const double da = coordinate(a), db = coordinate(b);
			if (std::abs(da) <= threshold || std::abs(db) <= threshold || da * db >= 0)
				continue;
			const int low = topology.group(a) < topology.group(b) ? a : b;
			const double dl = low == a ? da : db, dh = low == a ? db : da;
			cuts.insert(modelEdge(a, b), dl / (dl - dh));
		}
	}
	QSet<int> chosen(faces.cbegin(), faces.cend());
	if (!cuts.isEmpty())
	{
		if (!reserveCapacity(*mesh, selection->surface, cuts.size(), qint64(cuts.size()) * 2, error))
			return false;
		QHash<ModelEdge, int> created;
		QVector<int> sources;
		if (!splitSurfaceEdges(&surface, topology, cuts, frame, &created, &sources, work))
			return false;
		QSet<int> derived;
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			if (chosen.contains(sources[face]))
				derived.insert(face);
		}
		chosen = derived;
		// Cut points lie exactly on the plane in every pose.
		for (int v : created)
		{
			for (auto &pose : surface.frames)
				(axis == 0 ? pose.positions[v].x : axis == 1 ? pose.positions[v].y : pose.positions[v].z) = 0.0f;
		}
	}
	QVector<bool> drop(surface.triangles.size(), false);
	QVector<int> source;
	QSet<int> centre;
	for (int face : sortedValues(chosen))
	{
		const auto &t = surface.triangles[face];
		bool planar = true, target = false;
		for (int v : {t.a, t.b, t.c})
		{
			const double c = coordinate(v) * sign;
			planar &= std::abs(c) <= threshold;
			target |= c < -threshold;
		}
		if (target)
		{
			drop[face] = true;
			continue;
		}
		if (!planar)
			source.append(face);
	}
	if (source.isEmpty())
		return fail(error, Text::tr("Nothing lies on the side being mirrored. Choose the other direction."));
	for (int face : std::as_const(source))
	{
		const auto &t = surface.triangles[face];
		for (int v : {t.a, t.b, t.c})
		{
			if (std::abs(coordinate(v)) <= threshold)
				centre.insert(v);
		}
	}
	QHash<int, int> mirror;
	qint64 added = 0;
	{
		QSet<int> corners;
		for (int face : std::as_const(source))
		{
			const auto &t = surface.triangles[face];
			corners << t.a << t.b << t.c;
		}
		corners -= centre;
		added = corners.size();
	}
	if (!reserveCapacity(*mesh, selection->surface, added, source.size(), error))
		return false;
	for (int v : std::as_const(centre))
	{
		for (auto &pose : surface.frames)
			(axis == 0 ? pose.positions[v].x : axis == 1 ? pose.positions[v].y : pose.positions[v].z) = 0.0f;
	}
	const auto reflected = [&](int v)
	{
		if (centre.contains(v))
			return v;
		if (mirror.contains(v))
			return mirror.value(v);
		const int copy = appendCopy(&surface, v);
		for (auto &pose : surface.frames)
		{
			auto &p = pose.positions[copy];
			auto &n = pose.normals[copy];
			(axis == 0 ? p.x : axis == 1 ? p.y : p.z) *= -1.0f;
			(axis == 0 ? n.x : axis == 1 ? n.y : n.z) *= -1.0f;
		}
		mirror.insert(v, copy);
		return copy;
	};
	QSet<int> created;
	for (int face : std::as_const(source))
	{
		if (!work.step())
			return false;
		const auto t = surface.triangles[face];
		const ModelTriangle copy{reflected(t.a), reflected(t.c), reflected(t.b)};
		created.insert(surface.triangles.size());
		surface.triangles.append(copy);
	}
	drop.resize(surface.triangles.size());
	const auto seams = surface.uvSeams;
	for (auto seam : seams)
	{
		const bool a = mirror.contains(seam.first) || centre.contains(seam.first), b = mirror.contains(seam.second) || centre.contains(seam.second);
		if (a && b)
			surface.uvSeams.insert(modelEdge(mirror.value(seam.first, seam.first), mirror.value(seam.second, seam.second)));
	}
	ModelSelection result{selection->surface, {}, {}};
	for (int face : std::as_const(source))
		result.faces.insert(face);
	result.faces += created;
	bool ok = true;
	const auto mapping = removeFaces(&surface, drop, nullptr, work, &ok);
	if (!ok)
		return false;
	selection->faces.clear();
	for (int face : std::as_const(result.faces))
	{
		if (face < mapping.size() && mapping[face] >= 0)
			selection->faces.insert(mapping[face]);
	}
	selection->vertices.clear();
	selection->edges.clear();
	// Centre vertices are compacted only when unused, so their indices may
	// have shifted; refresh normals around every face of the result.
	QSet<int> refresh;
	for (int face : std::as_const(selection->faces))
	{
		const auto &t = surface.triangles[face];
		refresh << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, refresh, work);
}

bool loopCut(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	if (selection->edges.size() != 1 || !selection->faces.isEmpty() || !selection->vertices.isEmpty())
		return fail(error, Text::tr("Select one edge that the new loop should cross."));
	const auto &options = edit.tool;
	auto &surface = mesh->surfaces[selection->surface];
	const int frame = options.referenceFrame;
	ModelQuadTopology quads;
	if (!buildModelQuadTopology(surface, frame, options.maxAngle, &quads, error))
		return false;
	const auto &geometry = quads.geometry;
	const auto seed = *selection->edges.cbegin();
	const auto geometric = geometry.geometricEdge(seed.first, seed.second);
	if (quads.diagonals.contains(geometric))
		return fail(error, Text::tr("The selected edge is a quad diagonal. Choose an edge on the side of a quad."));
	bool closed = false;
	QVector<int> elements;
	const auto ring = modelEdgeRing(quads, geometric, &closed, &elements);
	if (elements.isEmpty())
		return fail(error, Text::tr("The edge has no quads on either side. Loop cuts run across pairs of triangles that form quads."));
	if (QSet<int>(elements.cbegin(), elements.cend()).size() != elements.size())
		return fail(error, Text::tr("The quad ring crosses itself. Cut a different edge."));
	const int m = ring.size();
	if (elements.size() != (closed ? m : m - 1))
		return fail(error, Text::tr("The quad ring could not be followed."));
	const int cuts = options.cuts;
	QVector<double> fractions;
	if (cuts == 1)
		fractions.append(std::clamp(0.5 + options.slide / 2, 0.01, 0.99));
	else
	{
		for (int k = 1; k <= cuts; ++k)
			fractions.append(double(k) / (cuts + 1));
	}
	// Orient every ring edge: its "bottom" endpoint lies on the same side of the
	// ring throughout, so cut k on one quad side meets cut k on the next.
	QVector<int> bottom(m, -1);
	bottom[0] = ring[0].first;
	struct Placement
	{
		int entry = -1;
		bool ccw = true;
	};
	QVector<Placement> placements(elements.size());
	for (int i = 0; i < elements.size(); ++i)
	{
		const auto &element = quads.elements[elements[i]];
		int entry = -1;
		for (int side = 0; side < 4; ++side)
		{
			if (geometry.geometricEdge(element.corners[side], element.corners[(side + 1) % 4]) == ring[i])
				entry = side;
		}
		const int exit = (entry + 2) % 4;
		if (entry < 0 || geometry.geometricEdge(element.corners[exit], element.corners[(exit + 1) % 4]) != ring[(i + 1) % m])
			return fail(error, Text::tr("The quad ring could not be followed."));
		const auto &c = element.corners;
		const bool ccw = geometry.group(c[entry]) == bottom[i];
		if (!ccw && geometry.group(c[(entry + 1) % 4]) != bottom[i])
			return fail(error, Text::tr("The quad ring could not be followed."));
		placements[i] = {entry, ccw};
		const int nextBottom = geometry.group(ccw ? c[(entry + 3) % 4] : c[(entry + 2) % 4]);
		if (i + 1 == m)
		{
			if (nextBottom != bottom[0])
				return fail(error, Text::tr("The quad ring is twisted like a Möbius strip and cannot be cut."));
		}
		else
			bottom[i + 1] = nextBottom;
	}
	qint64 newVertices = qint64(cuts) * m * 2, newTriangles = qint64(elements.size()) * 2 * (cuts + 1) + 4 * qint64(cuts + 1);
	if (!reserveCapacity(*mesh, selection->surface, newVertices, newTriangles, error))
		return false;
	QHash<QPair<ModelEdge, int>, int> points;
	const auto pointAt = [&](int u, int v, int ringIndex, int k)
	{
		const auto key = qMakePair(modelEdge(u, v), k);
		if (points.contains(key))
			return points.value(key);
		const int low = geometry.group(u) < geometry.group(v) ? u : v, high = low == u ? v : u;
		const double s = fractions[k - 1];
		const double t = bottom[ringIndex] == ring[ringIndex].first ? s : 1 - s;
		const int created = appendLerp(&surface, low, high, t);
		points.insert(key, created);
		return created;
	};
	const auto at = [&](int v) { return position(surface, frame, v); };
	QVector<bool> removed(surface.triangles.size(), false);
	QVector<ModelTriangle> added;
	QSet<ModelEdge> loopEdges;
	QHash<ModelEdge, QVector<int>> sideChains; // index ring edge -> ordered vertices bottom to top
	for (int i = 0; i < elements.size(); ++i)
	{
		if (!work.step())
			return false;
		const auto &element = quads.elements[elements[i]];
		const auto &c = element.corners;
		const auto [entry, ccw] = placements[i];
		const int eb = ccw ? c[entry] : c[(entry + 1) % 4], et = ccw ? c[(entry + 1) % 4] : c[entry];
		const int xb = ccw ? c[(entry + 3) % 4] : c[(entry + 2) % 4], xt = ccw ? c[(entry + 2) % 4] : c[(entry + 3) % 4];
		QVector<int> left{eb}, right{xb};
		for (int k = 1; k <= cuts; ++k)
		{
			left.append(pointAt(eb, et, i, k));
			right.append(pointAt(xb, xt, (i + 1) % m, k));
		}
		left.append(et);
		right.append(xt);
		sideChains.insert(modelEdge(eb, et), left);
		sideChains.insert(modelEdge(xb, xt), right);
		for (int k = 0; k <= cuts; ++k)
		{
			std::array<int, 4> q = ccw ? std::array<int, 4>{left[k], left[k + 1], right[k + 1], right[k]}
									   : std::array<int, 4>{left[k], right[k], right[k + 1], left[k + 1]};
			if (length(at(q[0]) - at(q[2])) <= length(at(q[1]) - at(q[3])))
				added << ModelTriangle{q[0], q[1], q[2]} << ModelTriangle{q[0], q[2], q[3]};
			else
				added << ModelTriangle{q[0], q[1], q[3]} << ModelTriangle{q[1], q[2], q[3]};
			if (k > 0)
				loopEdges.insert(modelEdge(left[k], right[k]));
		}
		for (int face : element.faces)
			removed[face] = true;
	}
	// Faces beyond an open ring's ends take the new points too (no T-junctions).
	if (!closed)
	{
		const QSet<int> ringElements(elements.cbegin(), elements.cend());
		for (int end : {0, m - 1})
		{
			for (const auto &[element, side] : quads.sideUses.value(ring[end]))
			{
				if (ringElements.contains(element))
					continue;
				const auto &e = quads.elements[element];
				if (e.quad())
					return fail(error, Text::tr("The loop cut would end at a branching edge and leave a gap."));
				const int u = e.corners[side], v = e.corners[(side + 1) % 3], w = e.corners[(side + 2) % 3];
				// Points on this index edge from u to v.
				QVector<int> chain;
				const bool upward = geometry.group(u) == bottom[end];
				for (int k = 1; k <= cuts; ++k)
					chain.append(pointAt(u, v, end, upward ? k : cuts + 1 - k));
				QVector<int> fan{u};
				fan += chain;
				fan.append(v);
				for (int k = 0; k + 1 < fan.size(); ++k)
					added.append({fan[k], fan[k + 1], w});
				removed[e.faces[0]] = true;
			}
		}
	}
	for (auto it = sideChains.cbegin(); it != sideChains.cend(); ++it)
	{
		if (surface.uvSeams.contains(it.key()))
		{
			for (int k = 0; k + 1 < it.value().size(); ++k)
				surface.uvSeams.insert(modelEdge(it.value()[k], it.value()[k + 1]));
		}
	}
	QVector<ModelTriangle> triangles;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!removed[face])
			triangles.append(surface.triangles[face]);
	}
	triangles += added;
	surface.triangles = std::move(triangles);
	pruneSeams(&surface);
	selection->edges = loopEdges;
	selection->faces.clear();
	selection->vertices.clear();
	return work.check();
}
bool bevelVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	const double width = edit.tool.bevelWidth;
	if (!(width > 0))
		return fail(error, Text::tr("Bevel needs a positive width."));
	const int frame = edit.tool.referenceFrame;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto targets =
		expandToCopies(topology, selection->vertices.isEmpty() ? selectedVertexSet(surface, *selection) : selection->vertices);
	if (targets.isEmpty())
		return fail(error, Text::tr("Select the vertices to bevel."));
	// An edge between two bevelled vertices first gets a midpoint, so each end
	// can be cut separately.
	QHash<ModelEdge, double> middles;
	for (const auto &t : std::as_const(surface.triangles))
	{
		for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			if (targets.contains(a) && targets.contains(b))
				middles.insert(modelEdge(a, b), 0.5);
		}
	}
	QSet<int> midpoints;
	if (!middles.isEmpty())
	{
		if (!reserveCapacity(*mesh, selection->surface, middles.size(), qint64(middles.size()) * 2, error))
			return false;
		QHash<ModelEdge, int> created;
		QVector<int> sources;
		if (!splitSurfaceEdges(&surface, topology, middles, frame, &created, &sources, work))
			return false;
		for (int v : std::as_const(created))
			midpoints.insert(v);
		if (!buildModelGeometricTopology(surface, &topology, error))
			return false;
	}
	// Cut every edge at a bevelled vertex at the bevel width from that vertex.
	QHash<ModelEdge, double> cuts;
	for (const auto &t : std::as_const(surface.triangles))
	{
		for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			const bool ta = targets.contains(a), tb = targets.contains(b);
			if (ta == tb || cuts.contains(modelEdge(a, b)))
				continue;
			const int target = ta ? a : b, other = ta ? b : a;
			const double edgeLength = length(position(surface, frame, other) - position(surface, frame, target));
			if (edgeLength <= width * 1.0001)
				return fail(error, Text::tr("The bevel width reaches past a neighbouring vertex. Use a width below %1.")
									   .arg(edgeLength, 0, 'g', 4));
			const double fromTarget = width / edgeLength;
			cuts.insert(modelEdge(a, b), topology.group(target) < topology.group(other) ? fromTarget : 1 - fromTarget);
		}
	}
	if (!reserveCapacity(*mesh, selection->surface, cuts.size(), qint64(cuts.size()) * 2, error))
		return false;
	QHash<ModelEdge, int> created;
	QVector<int> sources;
	if (!splitSurfaceEdges(&surface, topology, cuts, frame, &created, &sources, work))
		return false;
	// Helper midpoints that sit in a flat area are dissolved again; on a fold
	// they stay, so no triangle cuts across the edge.
	QSet<int> remove = targets;
	for (int midpoint : std::as_const(midpoints))
	{
		P3 first{0, 0, 0};
		bool flat = true, seeded = false;
		for (const auto &t : std::as_const(surface.triangles))
		{
			if (t.a != midpoint && t.b != midpoint && t.c != midpoint)
				continue;
			const P3 n = unit(faceCross(surface, frame, t));
			if (!seeded)
			{
				first = n;
				seeded = true;
			}
			else if (dot(first, n) < 0.99985)
				flat = false;
		}
		if (flat)
			remove.insert(midpoint);
	}
	ModelSelection dissolve{selection->surface, remove, {}};
	ModelEdit dissolveEdit = edit;
	dissolveEdit.kind = ModelEditKind::DissolveVertices;
	if (!dissolveVertices(mesh, dissolveEdit, &dissolve, error, work))
		return false;
	selection->faces = dissolve.faces;
	selection->vertices.clear();
	selection->edges.clear();
	return work.check();
}

} // namespace vibestudio::model_tools
