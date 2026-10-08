#include "core/model_mesh_tools_p.h"

#include "core/model_transform.h"
#include "core/model_transform_axes.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <queue>

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
// Displacement in a triangle's plane expressed as a UV displacement, so new
// vertices placed inside a face take the texture coordinates found there.
ModelTexCoord uvShift(const ModelSurface &surface, int frame, const ModelTriangle &t, P3 displacement)
{
	const P3 a = position(surface, frame, t.a), e1 = position(surface, frame, t.b) - a, e2 = position(surface, frame, t.c) - a;
	const double d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2), denominator = d11 * d22 - d12 * d12;
	if (std::abs(denominator) < 1e-20)
		return {0, 0};
	const double r1 = dot(displacement, e1), r2 = dot(displacement, e2);
	const double s = (r1 * d22 - r2 * d12) / denominator, w = (r2 * d11 - r1 * d12) / denominator;
	const auto ua = surface.texCoords[t.a], ub = surface.texCoords[t.b], uc = surface.texCoords[t.c];
	return {float(s * (ub.u - ua.u) + w * (uc.u - ua.u)), float(s * (ub.v - ua.v) + w * (uc.v - ua.v))};
}
// Direction and length that move a border corner inward by `thickness` from
// both border edges (even thickness) or along their averaged direction.
P3 insetOffset(P3 inwardIn, P3 inwardOut, double thickness, bool even)
{
	P3 direction = unit(inwardIn + inwardOut);
	if (length(direction) <= 1e-9)
		direction = inwardIn;
	if (!even)
		return direction * thickness;
	const double cosine = dot(direction, inwardIn);
	return direction * (thickness / std::max(cosine, 0.2));
}
// Angle-weighted normal of each geometric vertex group touched by `groups`.
QHash<int, P3> groupNormals(const ModelSurface &surface, const ModelGeometricTopology &topology, const QSet<int> &groups, int frame)
{
	QHash<int, P3> normals;
	for (int group : groups)
	{
		P3 sum{0, 0, 0};
		for (int face : topology.groupFaces[group])
			sum = sum + matchingCornerNormal(surface, frame, surface.triangles[face], [&](int v) { return topology.group(v) == group; });
		normals.insert(group, unit(sum));
	}
	return normals;
}
} // namespace

bool insetFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select at least one face to inset."));
	const auto &options = edit.tool;
	if (options.insetThickness <= 0 && options.insetDepth == 0)
		return fail(error, Text::tr("Inset needs a thickness or a depth."));
	auto &surface = mesh->surfaces[selection->surface];
	const int reference = options.referenceFrame;
	const bool individual = options.inset == ModelInsetMode::Individual;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const QSet<int> region(faces.cbegin(), faces.cend());
	// Border edges in face winding, with the face that owns each one.
	struct Border
	{
		int from = -1, to = -1, face = -1;
	};
	QVector<Border> borders;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		for (const auto &[from, to] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			if (individual)
			{
				borders.append({from, to, face});
				continue;
			}
			int inside = 0;
			for (int other : topology.edgeFaces.value(topology.geometricEdge(from, to)))
				inside += region.contains(other);
			if (topology.edgeFaces.value(topology.geometricEdge(from, to)).size() > 2)
				return fail(error, Text::tr("Inset needs manifold faces; edge %1–%2 has more than two faces.").arg(from).arg(to));
			if (inside == 1)
				borders.append({from, to, face});
		}
	}
	if (borders.isEmpty())
		return fail(error, Text::tr("The selection is a closed shell without a border. Inset Individual faces instead."));
	// Each border corner gets an inner copy. In region mode one copy serves
	// every region face at that index; individually inset faces get private copies.
	struct Corner
	{
		int vertex = -1, face = -1;
		int incoming = -1, outgoing = -1; // border indices
	};
	QHash<QPair<int, int>, Corner> corners; // (vertex, face or -1) -> corner
	const auto cornerKey = [&](int vertex, int face) { return qMakePair(vertex, individual ? face : -1); };
	// In region mode, border continuity is geometric: a seam may split a border
	// corner into two indices, one carrying the incoming edge, the other the outgoing.
	QHash<int, int> incomingByGroup, outgoingByGroup;
	for (int i = 0; i < borders.size(); ++i)
	{
		const auto &b = borders[i];
		auto &start = corners[cornerKey(b.from, b.face)];
		start.vertex = b.from;
		start.face = b.face;
		start.outgoing = i;
		auto &end = corners[cornerKey(b.to, b.face)];
		end.vertex = b.to;
		end.face = b.face;
		end.incoming = i;
		if (!individual)
		{
			const int fromGroup = topology.group(b.from), toGroup = topology.group(b.to);
			if (outgoingByGroup.contains(fromGroup) || incomingByGroup.contains(toGroup))
				return fail(error, Text::tr("Inset needs regions whose borders do not touch at a single corner."));
			outgoingByGroup.insert(fromGroup, i);
			incomingByGroup.insert(toGroup, i);
		}
	}
	if (!reserveCapacity(*mesh, selection->surface, corners.size(), qint64(borders.size()) * 2, error))
		return false;
	if (!individual)
	{
		for (auto it = outgoingByGroup.cbegin(); it != outgoingByGroup.cend(); ++it)
		{
			if (!incomingByGroup.contains(it.key()))
				return fail(error, Text::tr("Inset could not follow the selection border."));
		}
		// Vertices inside the region must not touch faces outside it.
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			for (int v : {t.a, t.b, t.c})
			{
				if (incomingByGroup.contains(topology.group(v)))
					continue;
				for (int other : topology.groupFaces[topology.group(v)])
				{
					if (!region.contains(other))
						return fail(error, Text::tr("Inset needs regions that meet the rest of the surface along edges, not at a single vertex."));
				}
			}
		}
	}
	const auto borderInward = [&](int index, int frame)
	{
		const auto &b = borders[index];
		const P3 normal = unit(faceCross(surface, frame, surface.triangles[b.face]));
		return unit(cross(normal, position(surface, frame, b.to) - position(surface, frame, b.from)));
	};
	const auto regionNormal = [&](int frame)
	{
		P3 sum{0, 0, 0};
		for (int face : faces)
			sum = sum + faceCross(surface, frame, surface.triangles[face]);
		return unit(sum);
	};
	// Offsets per corner key and pose, computed from the unmodified surface.
	QHash<QPair<int, int>, QVector<P3>> offsets, flatOffsets;
	QHash<QPair<int, int>, ModelTexCoord> uvs;
	for (auto it = corners.cbegin(); it != corners.cend(); ++it)
	{
		const auto &corner = it.value();
		int incoming = corner.incoming, outgoing = corner.outgoing;
		if (!individual)
		{
			incoming = incomingByGroup.value(topology.group(corner.vertex));
			outgoing = outgoingByGroup.value(topology.group(corner.vertex));
		}
		if (incoming < 0 || outgoing < 0)
			return fail(error, Text::tr("Inset could not follow the selection border."));
		QVector<P3> perFrame(surface.frames.size()), inPlane(surface.frames.size());
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			const P3 lift = individual ? unit(faceCross(surface, frame, surface.triangles[corner.face])) : regionNormal(frame);
			inPlane[frame] = insetOffset(borderInward(incoming, frame), borderInward(outgoing, frame), options.insetThickness, options.evenThickness);
			perFrame[frame] = inPlane[frame] + lift * options.insetDepth;
		}
		flatOffsets.insert(it.key(), inPlane);
		const P3 flat = insetOffset(borderInward(incoming, reference), borderInward(outgoing, reference), options.insetThickness,
									options.evenThickness);
		const int ownerFace = borders[corner.outgoing >= 0 ? corner.outgoing : corner.incoming].face;
		const auto shift = uvShift(surface, reference, surface.triangles[ownerFace], flat);
		const auto uv = surface.texCoords[corner.vertex];
		uvs.insert(it.key(), {uv.u + shift.u, uv.v + shift.v});
		offsets.insert(it.key(), perFrame);
	}
	// A rim that folds over in its face's plane means the thickness reaches
	// past the opposite border; refuse rather than build overlapping faces.
	if (options.insetThickness > 0)
	{
		for (const auto &b : std::as_const(borders))
		{
			const auto &from = flatOffsets[cornerKey(b.from, b.face)], &to = flatOffsets[cornerKey(b.to, b.face)];
			for (int frame = 0; frame < surface.frames.size(); ++frame)
			{
				const P3 n = unit(faceCross(surface, frame, surface.triangles[b.face]));
				const P3 a = position(surface, frame, b.from), c = position(surface, frame, b.to);
				const P3 cInner = c + to[frame], aInner = a + from[frame];
				if (dot(cross(c - a, cInner - a), n) <= 0 || dot(cross(cInner - a, aInner - a), n) <= 0)
					return fail(error, Text::tr("The inset thickness is too large for face %1; its border would fold over.").arg(b.face));
			}
		}
	}
	// Create inner copies.
	QHash<QPair<int, int>, int> inner;
	QSet<int> touched;
	auto keys = corners.keys();
	std::sort(keys.begin(), keys.end());
	for (const auto &key : std::as_const(keys))
	{
		if (!work.step())
			return false;
		const int vertex = corners.value(key).vertex;
		const int copy = appendCopy(&surface, vertex);
		const auto perFrame = offsets.value(key);
		for (int frame = 0; frame < surface.frames.size(); ++frame)
			surface.frames[frame].positions[copy] = vec(position(surface, frame, vertex) + perFrame[frame]);
		surface.texCoords[copy] = uvs.value(key);
		inner.insert(key, copy);
		touched << vertex << copy;
	}
	// Interior region vertices rise with the depth.
	if (!individual && options.insetDepth != 0)
	{
		QSet<int> interior;
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			for (int v : {t.a, t.b, t.c})
			{
				if (!incomingByGroup.contains(topology.group(v)))
					interior.insert(v);
			}
		}
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			const P3 lift = regionNormal(frame) * options.insetDepth;
			for (int v : std::as_const(interior))
				surface.frames[frame].positions[v] = vec(position(surface, frame, v) + lift);
		}
		touched += interior;
	}
	// Region faces use the inner copies; rims join each border to its copy.
	const auto originalTriangles = surface.triangles;
	for (int face : faces)
	{
		auto t = surface.triangles[face];
		for (int *corner : {&t.a, &t.b, &t.c})
		{
			const auto key = cornerKey(*corner, face);
			if (inner.contains(key))
				*corner = inner.value(key);
		}
		surface.triangles[face] = t;
	}
	for (const auto &b : std::as_const(borders))
	{
		const int from = inner.value(cornerKey(b.from, b.face)), to = inner.value(cornerKey(b.to, b.face));
		surface.triangles.append({b.from, b.to, to});
		surface.triangles.append({b.from, to, from});
	}
	// Inner faces must keep their orientation; too much thickness turns them over.
	for (int face : faces)
	{
		if (flipsInAnyFrame(surface, originalTriangles[face], surface.triangles[face]) || collapsedInAnyFrame(surface, surface.triangles[face]))
			return fail(error, Text::tr("The inset thickness is too large for face %1; its inner face would turn over.").arg(face));
	}
	// Seam marks inside the region follow the inner copies.
	QSet<ModelEdge> seams = surface.uvSeams;
	for (int face : faces)
	{
		const auto &before = originalTriangles[face], &after = surface.triangles[face];
		const std::array<int, 3> b{before.a, before.b, before.c}, a{after.a, after.b, after.c};
		for (int i = 0; i < 3; ++i)
		{
			const auto old = modelEdge(b[i], b[(i + 1) % 3]);
			if (surface.uvSeams.contains(old) && old != modelEdge(a[i], a[(i + 1) % 3]))
				seams.insert(modelEdge(a[i], a[(i + 1) % 3]));
		}
	}
	surface.uvSeams = seams;
	pruneSeams(&surface);
	if (!refreshNormals(&surface, touched, work))
		return false;
	selection->vertices.clear();
	selection->edges.clear();
	return work.check();
}

bool shrinkFatten(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto vertices = expandToCopies(topology, selectedVertexSet(surface, *selection));
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select the vertices, edges or faces to shrink or fatten."));
	if (edit.tool.offset == 0)
		return fail(error, Text::tr("Shrink/Fatten needs a nonzero distance."));
	QSet<int> groups;
	for (int v : vertices)
		groups.insert(topology.group(v));
	for (int frame = 0; frame < surface.frames.size(); ++frame)
	{
		if (edit.frame >= 0 && frame != edit.frame)
			continue;
		const auto normals = groupNormals(surface, topology, groups, frame);
		QHash<int, P3> moved;
		for (int group : groups)
		{
			if (!work.step())
				return false;
			const P3 normal = normals.value(group);
			if (length(normal) <= 0)
				continue;
			double factor = 1;
			if (edit.tool.evenThickness)
			{
				// Shell factor: the angle-weighted mean cosine to each face.
				double weight = 0, cosine = 0;
				for (int face : topology.groupFaces[group])
				{
					const P3 c = matchingCornerNormal(surface, frame, surface.triangles[face],
													  [&](int v) { return topology.group(v) == group; });
					weight += length(c);
					cosine += length(c) * dot(unit(c), normal);
				}
				if (weight > 0)
					factor = 1 / std::max(cosine / weight, 0.25);
			}
			moved.insert(group, position(surface, frame, group) + normal * (edit.tool.offset * factor));
		}
		for (auto it = moved.cbegin(); it != moved.cend(); ++it)
		{
			for (int copy : topology.copies[it.key()])
				surface.frames[frame].positions[copy] = vec(it.value());
		}
	}
	QSet<int> around = vertices;
	for (const auto &t : std::as_const(surface.triangles))
	{
		if (vertices.contains(t.a) || vertices.contains(t.b) || vertices.contains(t.c))
			around << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, around, work, edit.frame);
}

bool smoothVertices(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto vertices = expandToCopies(topology, selectedVertexSet(surface, *selection));
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select the vertices, edges or faces to smooth."));
	QSet<int> groups;
	for (int v : vertices)
		groups.insert(topology.group(v));
	QHash<int, QSet<int>> neighbours;
	QSet<int> boundary;
	for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
	{
		const auto edge = it.key();
		if (groups.contains(edge.first))
			neighbours[edge.first].insert(edge.second);
		if (groups.contains(edge.second))
			neighbours[edge.second].insert(edge.first);
		if (it.value().size() == 1)
			boundary << edge.first << edge.second;
	}
	const auto ordered = sortedValues(groups);
	for (int frame = 0; frame < surface.frames.size(); ++frame)
	{
		if (edit.frame >= 0 && frame != edit.frame)
			continue;
		for (int iteration = 0; iteration < edit.tool.iterations; ++iteration)
		{
			QHash<int, P3> moved;
			for (int group : ordered)
			{
				if (!work.step())
					return false;
				const auto around = neighbours.value(group);
				if (around.isEmpty() || (edit.tool.pinBoundary && boundary.contains(group)))
					continue;
				P3 average{0, 0, 0};
				for (int other : around)
					average = average + position(surface, frame, other);
				average = average * (1.0 / around.size());
				const P3 current = position(surface, frame, group);
				moved.insert(group, current + (average - current) * edit.tool.smoothFactor);
			}
			for (auto it = moved.cbegin(); it != moved.cend(); ++it)
			{
				for (int copy : topology.copies[it.key()])
					surface.frames[frame].positions[copy] = vec(it.value());
			}
		}
	}
	QSet<int> around = vertices;
	for (const auto &t : std::as_const(surface.triangles))
	{
		if (vertices.contains(t.a) || vertices.contains(t.b) || vertices.contains(t.c))
			around << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, around, work, edit.frame);
}

bool weightedTransform(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work,
					   const ModelWorkControl &control)
{
	auto &surface = mesh->surfaces[selection->surface];
	const auto &options = edit.tool;
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const auto vertices = expandToCopies(topology, selectedVertexSet(surface, *selection));
	if (vertices.isEmpty())
		return fail(error, Text::tr("Select at least one vertex, edge, or face."));
	ModelTransform transform;
	if (!snapModelTranslation(edit.translation, edit.translationGrid, &transform.translation, error) ||
		!snapModelRotation(edit.rotation, edit.rotationGrid, &transform.rotation, error) ||
		!snapModelScale(edit.scale, edit.scaleGrid, &transform.scale, error) ||
		!resolveModelTransformAxes(*mesh, edit, &transform.basis, error, control))
		return false;
	const int reference = edit.frame >= 0 ? edit.frame : edit.pivotFrame;
	if (reference < 0 || reference >= surface.frames.size() ||
		!modelTransformPivot(surface.frames[reference].positions, vertices, edit.pivotMode, edit.pivot, &transform.pivot))
		return fail(error, Text::tr("Choose a valid transform pivot and reference frame."));
	if (double(transform.scale.x) * transform.scale.y * transform.scale.z <= 0)
		return fail(error, Text::tr("Weighted transforms keep positive scale; mirror the whole surface with Apply Transform instead."));
	// Weight 1 for the selection; the proportional falloff for the rest.
	QSet<int> selectedGroups;
	for (int v : vertices)
		selectedGroups.insert(topology.group(v));
	QHash<int, double> weights;
	for (int group : selectedGroups)
		weights.insert(group, 1.0);
	const double radius = options.proportionalRadius;
	if (radius > 0)
	{
		QHash<int, double> distance;
		if (options.connectedOnly)
		{
			// Geodesic distance along edges from any selected vertex.
			QHash<int, QVector<int>> adjacency;
			for (auto it = topology.edgeFaces.cbegin(); it != topology.edgeFaces.cend(); ++it)
			{
				adjacency[it.key().first].append(it.key().second);
				adjacency[it.key().second].append(it.key().first);
			}
			using Entry = std::pair<double, int>;
			std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
			for (int group : selectedGroups)
			{
				distance.insert(group, 0);
				queue.push({0, group});
			}
			while (!queue.empty())
			{
				if (!work.step())
					return false;
				const auto [d, group] = queue.top();
				queue.pop();
				if (d > distance.value(group, INFINITY) || d >= radius)
					continue;
				for (int other : adjacency.value(group))
				{
					const double next = d + length(position(surface, reference, other) - position(surface, reference, group));
					if (next < distance.value(other, INFINITY) && next < radius)
					{
						distance.insert(other, next);
						queue.push({next, other});
					}
				}
			}
		}
		else
		{
			QVector<P3> sources;
			for (int group : selectedGroups)
				sources.append(position(surface, reference, group));
			for (int v = 0; v < surface.vertexCount; ++v)
			{
				if (!work.step())
					return false;
				if (topology.group(v) != v || selectedGroups.contains(v))
					continue;
				const P3 p = position(surface, reference, v);
				double nearest = INFINITY;
				for (const auto &source : std::as_const(sources))
					nearest = std::min(nearest, length(p - source));
				if (nearest < radius)
					distance.insert(v, nearest);
			}
		}
		for (auto it = distance.cbegin(); it != distance.cend(); ++it)
		{
			if (!selectedGroups.contains(it.key()))
			{
				const double w = modelFalloffWeight(options.falloff, it.value() / radius);
				if (w > 0)
					weights.insert(it.key(), w);
			}
		}
	}
	// Mirror counterparts take the reflected transform with the same weight.
	QHash<int, std::pair<int, double>> mirrored; // group -> (axes, weight)
	if (options.mirrorAxes != 0)
	{
		double scale = 0;
		for (const auto &p : surface.frames[reference].positions)
			scale = std::max({scale, double(std::abs(p.x)), double(std::abs(p.y)), double(std::abs(p.z))});
		const double tolerance = std::max(options.mergeThreshold, scale * 1e-6);
		// Grid cells twice the tolerance wide; a match lies in a neighbouring cell.
		const double cell = std::max(tolerance * 2, 1e-9);
		const auto cellOf = [&](P3 p)
		{ return std::array<qint64, 3>{qint64(std::floor(p.x / cell)), qint64(std::floor(p.y / cell)), qint64(std::floor(p.z / cell))}; };
		std::map<std::array<qint64, 3>, QVector<int>> grid;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			if (topology.group(v) == v)
				grid[cellOf(position(surface, reference, v))].append(v);
		}
		const auto weighted = weights;
		for (auto it = weighted.cbegin(); it != weighted.cend(); ++it)
		{
			for (int axes = 1; axes <= 7; ++axes)
			{
				if ((axes & ~options.mirrorAxes) != 0)
					continue;
				P3 target = position(surface, reference, it.key());
				if (axes & 1)
					target.x = -target.x;
				if (axes & 2)
					target.y = -target.y;
				if (axes & 4)
					target.z = -target.z;
				int match = -1;
				double best = tolerance;
				const auto centre = cellOf(target);
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
								const double d = length(position(surface, reference, candidate) - target);
								if (d < best || (d == best && match >= 0 && candidate < match) || (d == best && match < 0))
								{
									best = d;
									match = candidate;
								}
							}
						}
					}
				}
				if (match < 0 || weights.contains(match) || mirrored.contains(match))
					continue;
				if (match == it.key())
					continue;
				mirrored.insert(match, {axes, it.value()});
			}
		}
	}
	const auto reflect = [](P3 p, int axes)
	{
		if (axes & 1)
			p.x = -p.x;
		if (axes & 2)
			p.y = -p.y;
		if (axes & 4)
			p.z = -p.z;
		return p;
	};
	const auto scaled = [&](double weight)
	{
		ModelTransform t = transform;
		t.translation = {float(transform.translation.x * weight), float(transform.translation.y * weight),
						 float(transform.translation.z * weight)};
		t.rotation = {float(transform.rotation.x * weight), float(transform.rotation.y * weight), float(transform.rotation.z * weight)};
		t.scale = {float(1 + (transform.scale.x - 1) * weight), float(1 + (transform.scale.y - 1) * weight),
				   float(1 + (transform.scale.z - 1) * weight)};
		return t;
	};
	// Vertices on an active mirror plane stay on it: average both reflections.
	const auto onPlane = [&](P3 p, double tolerance)
	{
		int axes = 0;
		if ((options.mirrorAxes & 1) && std::abs(p.x) <= tolerance)
			axes |= 1;
		if ((options.mirrorAxes & 2) && std::abs(p.y) <= tolerance)
			axes |= 2;
		if ((options.mirrorAxes & 4) && std::abs(p.z) <= tolerance)
			axes |= 4;
		return axes;
	};
	QSet<int> moved;
	for (int frame = 0; frame < surface.frames.size(); ++frame)
	{
		if (edit.frame >= 0 && frame != edit.frame)
			continue;
		QHash<int, P3> results;
		for (auto it = weights.cbegin(); it != weights.cend(); ++it)
		{
			if (!work.step())
				return false;
			const auto t = scaled(it.value());
			const P3 p = position(surface, frame, it.key());
			P3 result = point(transformModelPoint(vec(p), t));
			const int plane = onPlane(position(surface, reference, it.key()), std::max(options.mergeThreshold, 1e-6));
			if (plane != 0)
			{
				const P3 mirror = reflect(point(transformModelPoint(vec(reflect(p, plane)), t)), plane);
				result = (result + mirror) * 0.5;
			}
			results.insert(it.key(), result);
		}
		for (auto it = mirrored.cbegin(); it != mirrored.cend(); ++it)
		{
			const auto t = scaled(it.value().second);
			const int axes = it.value().first;
			const P3 p = position(surface, frame, it.key());
			results.insert(it.key(), reflect(point(transformModelPoint(vec(reflect(p, axes)), t)), axes));
		}
		for (auto it = results.cbegin(); it != results.cend(); ++it)
		{
			for (int copy : topology.copies[it.key()])
			{
				surface.frames[frame].positions[copy] = vec(it.value());
				moved.insert(copy);
			}
		}
	}
	QSet<int> around = moved;
	for (const auto &t : std::as_const(surface.triangles))
	{
		if (moved.contains(t.a) || moved.contains(t.b) || moved.contains(t.c))
			around << t.a << t.b << t.c;
	}
	return refreshNormals(&surface, around, work, edit.frame);
}

bool shadeFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select the faces to shade."));
	auto &surface = mesh->surfaces[selection->surface];
	const int reference = edit.tool.referenceFrame;
	const QSet<int> chosen(faces.cbegin(), faces.cend());
	if (edit.kind == ModelEditKind::ShadeFlat)
	{
		QHash<int, int> uses;
		for (const auto &t : std::as_const(surface.triangles))
		{
			uses[t.a]++;
			uses[t.b]++;
			uses[t.c]++;
		}
		qint64 added = 0;
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			for (int v : {t.a, t.b, t.c})
				added += uses.value(v) > 1;
		}
		if (!reserveCapacity(*mesh, selection->surface, added, 0, error))
			return false;
		for (int face : faces)
		{
			if (!work.step())
				return false;
			auto t = surface.triangles[face];
			for (int *corner : {&t.a, &t.b, &t.c})
			{
				if (uses.value(*corner) > 1)
				{
					uses[*corner]--;
					*corner = appendCopy(&surface, *corner);
					uses[*corner] = 1;
				}
			}
			surface.triangles[face] = t;
			for (int frame = 0; frame < surface.frames.size(); ++frame)
			{
				const P3 n = unit(faceCross(surface, frame, t));
				if (length(n) <= 0)
					continue;
				for (int v : {t.a, t.b, t.c})
					surface.frames[frame].normals[v] = vec(n);
			}
		}
		pruneSeams(&surface);
		return work.check();
	}
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	if (edit.kind == ModelEditKind::ShadeSmooth)
	{
		// Weld split normals: copies with the same position and UV become one.
		QHash<int, int> mapping;
		QSet<int> corners;
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			corners << t.a << t.b << t.c;
		}
		for (int v : sortedValues(corners))
		{
			if (mapping.contains(v))
				continue;
			for (int copy : topology.copies[topology.group(v)])
			{
				if (copy != v && !mapping.contains(copy) && corners.contains(copy) && surface.texCoords[copy].u == surface.texCoords[v].u &&
					surface.texCoords[copy].v == surface.texCoords[v].v)
					mapping.insert(copy, v);
			}
		}
		for (int face : faces)
		{
			auto &t = surface.triangles[face];
			t = {mapping.value(t.a, t.a), mapping.value(t.b, t.b), mapping.value(t.c, t.c)};
		}
		// Copies still used by unselected faces keep their own index.
		QSet<int> keep;
		for (int face = 0; face < surface.triangles.size(); ++face)
		{
			if (!chosen.contains(face))
			{
				const auto &t = surface.triangles[face];
				keep << t.a << t.b << t.c;
			}
		}
		QSet<ModelEdge> seams;
		for (auto seam : std::as_const(surface.uvSeams))
			seams.insert(modelEdge(mapping.value(seam.first, seam.first), mapping.value(seam.second, seam.second)));
		surface.uvSeams = seams;
		pruneSeams(&surface);
		QSet<int> candidates;
		for (auto it = mapping.cbegin(); it != mapping.cend(); ++it)
		{
			if (!keep.contains(it.key()))
				candidates.insert(it.key());
		}
		ModelSelection kept = *selection;
		if (!compactUnused(&surface, candidates, &kept, work))
			return false;
		ModelGeometricTopology after;
		if (!buildModelGeometricTopology(surface, &after, error))
			return false;
		// Every corner of the selection takes the smooth normal of the selected
		// faces around its position, so UV seams inside the selection stay smooth.
		// An unselected face joins in only where it shares a corner index.
		QHash<int, QSet<int>> selectedCorners; // group -> indices used by selected faces
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			for (int v : {t.a, t.b, t.c})
				selectedCorners[after.group(v)].insert(v);
		}
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			for (auto it = selectedCorners.cbegin(); it != selectedCorners.cend(); ++it)
			{
				if (!work.step())
					return false;
				P3 sum{0, 0, 0};
				for (int face : after.groupFaces[it.key()])
				{
					const auto &t = surface.triangles[face];
					if (chosen.contains(face) || it.value().contains(t.a) || it.value().contains(t.b) || it.value().contains(t.c))
						sum = sum + matchingCornerNormal(surface, frame, t, [&](int v) { return after.group(v) == it.key(); });
				}
				if (length(sum) <= 1e-12)
					continue;
				for (int v : it.value())
					surface.frames[frame].normals[v] = vec(unit(sum));
			}
		}
		return work.check();
	}
	// Auto Smooth: corners smooth across edges flatter than the angle and stay
	// hard across sharper edges, splitting indices where one vertex needs two normals.
	const double limit = std::cos(edit.tool.smoothAngle * std::numbers::pi / 180.0);
	QVector<P3> referenceNormals(surface.triangles.size());
	for (int face = 0; face < surface.triangles.size(); ++face)
		referenceNormals[face] = unit(faceCross(surface, reference, surface.triangles[face]));
	const auto smoothAcross = [&](int a, int b) { return dot(referenceNormals[a], referenceNormals[b]) >= limit - 1e-12; };
	QSet<int> affected;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		affected << t.a << t.b << t.c;
	}
	// Corners to assign: every face at an affected index.
	QHash<int, QVector<int>> cornerFaces;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		const auto &t = surface.triangles[face];
		for (int v : {t.a, t.b, t.c})
		{
			if (affected.contains(v))
				cornerFaces[v].append(face);
		}
	}
	// Fan of a corner: faces around the same position reached through smooth geometric edges.
	const auto fanOf = [&](int face, int vertex)
	{
		const int group = topology.group(vertex);
		QSet<int> fan{face};
		QVector<int> stack{face};
		const auto around = topology.groupFaces[group];
		while (!stack.isEmpty())
		{
			const int current = stack.takeLast();
			const auto &t = surface.triangles[current];
			for (int other : around)
			{
				if (fan.contains(other) || !smoothAcross(current, other))
					continue;
				const auto &o = surface.triangles[other];
				// Neighbours share a geometric edge at this position.
				int shared = 0;
				for (int x : {t.a, t.b, t.c})
				{
					for (int y : {o.a, o.b, o.c})
						shared += topology.group(x) == topology.group(y);
				}
				if (shared >= 2)
				{
					fan.insert(other);
					stack.append(other);
				}
			}
		}
		auto list = fan.values().toVector();
		std::sort(list.begin(), list.end());
		return list;
	};
	qint64 splits = 0;
	QHash<QPair<int, int>, QVector<int>> fans; // (vertex, face) -> fan
	for (auto it = cornerFaces.cbegin(); it != cornerFaces.cend(); ++it)
	{
		QSet<QVector<int>> distinct;
		for (int face : it.value())
		{
			const auto fan = fanOf(face, it.key());
			fans.insert({it.key(), face}, fan);
			distinct.insert(fan);
		}
		splits += distinct.size() - 1;
	}
	if (!reserveCapacity(*mesh, selection->surface, splits, 0, error))
		return false;
	auto ordered = cornerFaces.keys();
	std::sort(ordered.begin(), ordered.end());
	for (int vertex : std::as_const(ordered))
	{
		if (!work.step())
			return false;
		QHash<QVector<int>, int> indexForFan;
		for (int face : cornerFaces.value(vertex))
		{
			const auto fan = fans.value({vertex, face});
			int index = indexForFan.value(fan, -1);
			if (index < 0)
			{
				index = indexForFan.isEmpty() ? vertex : appendCopy(&surface, vertex);
				indexForFan.insert(fan, index);
			}
			auto &t = surface.triangles[face];
			if (t.a == vertex)
				t.a = index;
			else if (t.b == vertex)
				t.b = index;
			else if (t.c == vertex)
				t.c = index;
			const int group = topology.group(vertex);
			for (int frame = 0; frame < surface.frames.size(); ++frame)
			{
				P3 sum{0, 0, 0};
				for (int member : fan)
					sum = sum + matchingCornerNormal(surface, frame, surface.triangles[member], [&](int v)
													 { return v < topology.vertexGroup.size() ? topology.group(v) == group : v == index; });
				if (length(sum) > 1e-12)
					surface.frames[frame].normals[index] = vec(unit(sum));
			}
		}
	}
	pruneSeams(&surface);
	return work.check();
}
bool solidify(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select the faces to give thickness."));
	const double thickness = edit.tool.solidifyThickness;
	if (std::abs(thickness) < 1e-6)
		return fail(error, Text::tr("Solidify needs a nonzero thickness."));
	auto &surface = mesh->surfaces[selection->surface];
	ModelGeometricTopology topology;
	if (!buildModelGeometricTopology(surface, &topology, error))
		return false;
	const QSet<int> region(faces.cbegin(), faces.cend());
	QSet<int> vertices;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		vertices << t.a << t.b << t.c;
	}
	// Border edges in face winding, where the shell needs a rim.
	QVector<QPair<int, int>> borders;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		for (const auto &[from, to] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
		{
			int inside = 0;
			for (int other : topology.edgeFaces.value(topology.geometricEdge(from, to)))
				inside += region.contains(other);
			if (inside == 1)
				borders.append({from, to});
		}
	}
	if (!reserveCapacity(*mesh, selection->surface, vertices.size() + qint64(borders.size()) * 4,
						 faces.size() + qint64(borders.size()) * 2, error))
		return false;
	// Each corner moves back along the shell's angle-weighted normal.
	QHash<int, QVector<P3>> offsets;
	for (int v : std::as_const(vertices))
	{
		const int group = topology.group(v);
		QVector<P3> perFrame(surface.frames.size());
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			P3 sum{0, 0, 0};
			double weight = 0, cosine = 0;
			QVector<P3> corners;
			for (int face : topology.groupFaces[group])
			{
				if (!region.contains(face))
					continue;
				const P3 c = matchingCornerNormal(surface, frame, surface.triangles[face], [&](int x) { return topology.group(x) == group; });
				corners.append(c);
				sum = sum + c;
			}
			const P3 normal = unit(sum);
			for (const auto &c : std::as_const(corners))
			{
				weight += length(c);
				cosine += length(c) * dot(unit(c), normal);
			}
			const double factor = edit.tool.evenThickness && weight > 0 ? 1 / std::max(cosine / weight, 0.25) : 1;
			perFrame[frame] = normal * (-thickness * factor);
		}
		offsets.insert(v, perFrame);
	}
	QHash<int, int> back;
	for (int v : sortedValues(vertices))
	{
		if (!work.step())
			return false;
		const int copy = appendCopy(&surface, v);
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			auto &pose = surface.frames[frame];
			pose.positions[copy] = vec(position(surface, frame, v) + offsets[v][frame]);
			pose.normals[copy] = vec(point(pose.normals[v]) * -1.0);
		}
		back.insert(v, copy);
	}
	QSet<int> result(faces.cbegin(), faces.cend());
	for (int face : faces)
	{
		const auto t = surface.triangles[face];
		result.insert(surface.triangles.size());
		surface.triangles.append({back[t.a], back[t.c], back[t.b]});
	}
	// The rim gets its own corners so its flat normals give a crisp edge.
	for (const auto &[from, to] : std::as_const(borders))
	{
		if (!work.step())
			return false;
		const int topFrom = appendCopy(&surface, from), topTo = appendCopy(&surface, to);
		const int backFrom = appendCopy(&surface, back[from]), backTo = appendCopy(&surface, back[to]);
		const ModelTriangle first{topTo, topFrom, backFrom}, second{topTo, backFrom, backTo};
		result.insert(surface.triangles.size());
		surface.triangles.append(first);
		result.insert(surface.triangles.size());
		surface.triangles.append(second);
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			const P3 n = unit(faceCross(surface, frame, first) + faceCross(surface, frame, second));
			if (length(n) <= 0)
				continue;
			for (int v : {topFrom, topTo, backFrom, backTo})
				surface.frames[frame].normals[v] = vec(n);
		}
	}
	selection->faces = result;
	selection->vertices.clear();
	selection->edges.clear();
	return work.check();
}

} // namespace vibestudio::model_tools
