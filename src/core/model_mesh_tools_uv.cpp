#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// Blender-style UV projections for idTech skins: cube (world-aligned, six
// sides), view, cylinder and sphere. Faces only split UV corners where a
// neighbour keeps a different mapping, so seams appear where the texture
// actually changes and vertex counts stay low for MD3/MD2/MDL budgets.

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
// Dominant axis and sign of a face normal: 0..5 for +X, -X, +Y, -Y, +Z, -Z.
int cubeSide(P3 normal)
{
	const double ax = std::abs(normal.x), ay = std::abs(normal.y), az = std::abs(normal.z);
	if (ax >= ay && ax >= az)
		return normal.x >= 0 ? 0 : 1;
	if (ay >= az)
		return normal.y >= 0 ? 2 : 3;
	return normal.z >= 0 ? 4 : 5;
}
// Each side reads the right way round from outside, with +Z up on the walls
// and +Y up on the caps; V grows downwards like image rows.
ModelTexCoord cubeUv(int side, P3 p, double scale)
{
	switch (side)
	{
	case 0:
		return {float(p.y * scale), float(-p.z * scale)};
	case 1:
		return {float(-p.y * scale), float(-p.z * scale)};
	case 2:
		return {float(-p.x * scale), float(-p.z * scale)};
	case 3:
		return {float(p.x * scale), float(-p.z * scale)};
	case 4:
		return {float(p.x * scale), float(-p.y * scale)};
	default:
		return {float(-p.x * scale), float(-p.y * scale)};
	}
}
} // namespace

bool projectUvs(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, ModelWorkProgress &work)
{
	const auto faces = sortedValues(selection->faces);
	if (faces.isEmpty())
		return fail(error, Text::tr("Select the faces to project UVs onto."));
	const auto &options = edit.tool;
	auto &surface = mesh->surfaces[selection->surface];
	const int frame = options.referenceFrame;
	// Cube and view projections repeat the texture every uvTileSize units;
	// round projections wrap it once around the axis.
	const double scale = 1.0 / options.uvTileSize;
	const bool round = options.uvProjection == ModelUvProjection::Cylinder || options.uvProjection == ModelUvProjection::Sphere;
	const P3 uAxis = unit(arrayPoint(options.uvAxisU)), vAxis = unit(arrayPoint(options.uvAxisV));
	if (options.uvProjection == ModelUvProjection::View && (length(uAxis) <= 0 || length(vAxis) <= 0 || length(cross(uAxis, vAxis)) < 1e-6))
		return fail(error, Text::tr("Project From View needs the view's right and up directions."));
	const QSet<int> chosen(faces.cbegin(), faces.cend());
	// Mapping group of each selected face; unselected faces form group -1.
	QHash<int, int> group;
	P3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		group.insert(face, options.uvProjection == ModelUvProjection::Cube ? cubeSide(faceCross(surface, frame, t)) : 0);
		for (int v : {t.a, t.b, t.c})
		{
			const P3 p = position(surface, frame, v);
			low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
			high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
		}
	}
	// Round projections wrap around the selection's centre along the axis.
	const P3 centre = (low + high) * 0.5;
	const P3 up = options.axis == 0 ? P3{1, 0, 0} : options.axis == 1 ? P3{0, 1, 0} : P3{0, 0, 1};
	const P3 side = options.axis == 2 ? P3{1, 0, 0} : options.axis == 1 ? P3{0, 0, 1} : P3{0, 1, 0};
	const P3 front = cross(up, side);
	const double polarTolerance = std::max(1e-6, length(high - low) * 1e-6);
	const auto radial = [&](P3 p)
	{
		const P3 r = p - centre;
		return length(r - up * dot(r, up));
	};
	// Cylinder V keeps square texels: one U repeat spans the mean circumference.
	double top = -INFINITY, meanRadius = 0;
	{
		QSet<int> corners;
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			corners << t.a << t.b << t.c;
		}
		for (int v : std::as_const(corners))
		{
			const P3 p = position(surface, frame, v);
			top = std::max(top, dot(p - centre, up));
			meanRadius += radial(p) / corners.size();
		}
	}
	const double circumference = 2 * std::numbers::pi * meanRadius;
	const auto project = [&](int mapping, P3 p) -> ModelTexCoord
	{
		const P3 r = p - centre;
		const double angle = std::atan2(dot(r, front), dot(r, side));
		switch (options.uvProjection)
		{
		case ModelUvProjection::Cube:
			return cubeUv(mapping, p, scale);
		case ModelUvProjection::View:
			return {float(dot(p, uAxis) * scale), float(-dot(p, vAxis) * scale)};
		case ModelUvProjection::Cylinder:
			return {float(0.5 + angle / (2 * std::numbers::pi)), float(circumference > 0 ? (top - dot(r, up)) / circumference : 0)};
		case ModelUvProjection::Sphere:
		{
			const double l = length(r);
			const double polar = l > 0 ? std::acos(std::clamp(dot(r, up) / l, -1.0, 1.0)) : 0;
			return {float(0.5 + angle / (2 * std::numbers::pi)), float(polar / std::numbers::pi)};
		}
		}
		return {};
	};
	// Corners shared with a face that keeps a different mapping get a copy.
	QHash<int, QSet<int>> users;
	for (int face = 0; face < surface.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto &t = surface.triangles[face];
		const int mapping = chosen.contains(face) ? group.value(face) : -1;
		for (int v : {t.a, t.b, t.c})
			users[v].insert(mapping);
	}
	qint64 copies = 0, projected = 0, polarCorners = 0;
	for (auto it = users.cbegin(); it != users.cend(); ++it)
	{
		copies += it.value().size() - 1;
		projected += it.value().size() - (it.value().contains(-1) ? 1 : 0);
	}
	if (round)
	{
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			for (int v : {t.a, t.b, t.c})
				polarCorners += radial(position(surface, frame, v)) <= polarTolerance ? 1 : 0;
		}
	}
	// Round projections may copy each projected corner once more at the wrap,
	// and give every face its own corner on the axis.
	if (!reserveCapacity(*mesh, selection->surface, copies + (round ? projected + polarCorners : 0), 0, error))
		return false;
	// The unselected side keeps each original index; otherwise the lowest mapping does.
	QHash<QPair<int, int>, int> corner;
	for (auto it = users.cbegin(); it != users.cend(); ++it)
	{
		const auto mappings = sortedValues(it.value());
		for (int i = 0; i < mappings.size(); ++i)
			corner.insert({it.key(), mappings[i]}, i == 0 ? it.key() : -1);
	}
	for (int face : faces)
	{
		if (!work.step())
			return false;
		auto t = surface.triangles[face];
		const int mapping = group.value(face);
		for (int *v : {&t.a, &t.b, &t.c})
		{
			auto &index = corner[{*v, mapping}];
			if (index < 0)
				index = appendCopy(&surface, *v);
			*v = index;
		}
		surface.triangles[face] = t;
	}
	// Seam marks follow each side's copies so a later unwrap keeps them.
	const auto seams = surface.uvSeams;
	for (const auto &seam : seams)
	{
		for (int mapping : users.value(seam.first))
		{
			const int a = corner.value({seam.first, mapping}, -1), b = corner.value({seam.second, mapping}, -1);
			if (a >= 0 && b >= 0 && users.value(seam.second).contains(mapping))
				surface.uvSeams.insert(modelEdge(a, b));
		}
	}
	QHash<int, int> mappingOf;
	for (int face : faces)
	{
		const auto &t = surface.triangles[face];
		for (int v : {t.a, t.b, t.c})
			mappingOf.insert(v, group.value(face));
	}
	for (auto it = mappingOf.cbegin(); it != mappingOf.cend(); ++it)
		surface.texCoords[it.key()] = project(it.value(), position(surface, frame, it.key()));
	if (round)
	{
		// Faces across the wrap use copies shifted one repeat to the right.
		QHash<int, int> wrapped;
		const auto onAxis = [&](int v) { return radial(position(surface, frame, v)) <= polarTolerance; };
		for (int face : faces)
		{
			if (!work.step())
				return false;
			auto t = surface.triangles[face];
			float lowU = INFINITY, highU = -INFINITY;
			for (int v : {t.a, t.b, t.c})
			{
				if (onAxis(v))
					continue;
				lowU = std::min(lowU, surface.texCoords[v].u);
				highU = std::max(highU, surface.texCoords[v].u);
			}
			if (highU - lowU > 0.5f)
			{
				for (int *v : {&t.a, &t.b, &t.c})
				{
					if (onAxis(*v) || surface.texCoords[*v].u >= 0.5f)
						continue;
					int copy = wrapped.value(*v, -1);
					if (copy < 0)
					{
						copy = appendCopy(&surface, *v);
						surface.texCoords[copy].u += 1;
						wrapped.insert(*v, copy);
					}
					*v = copy;
				}
			}
			// A corner on the axis has no direction of its own; it takes the
			// mean U of the face's other corners, as Blender does at the poles.
			double sum = 0;
			int count = 0;
			for (int v : {t.a, t.b, t.c})
			{
				if (!onAxis(v))
				{
					sum += surface.texCoords[v].u;
					++count;
				}
			}
			if (count > 0)
			{
				for (int *v : {&t.a, &t.b, &t.c})
				{
					if (!onAxis(*v))
						continue;
					*v = appendCopy(&surface, *v);
					surface.texCoords[*v].u = float(sum / count);
				}
			}
			surface.triangles[face] = t;
		}
	}
	if (options.uvFit)
	{
		// Scale to Bounds: the projected faces fill the 0-1 skin, keeping aspect.
		QSet<int> used;
		for (int face : faces)
		{
			const auto &t = surface.triangles[face];
			used << t.a << t.b << t.c;
		}
		float minU = INFINITY, minV = INFINITY, maxU = -INFINITY, maxV = -INFINITY;
		for (int v : std::as_const(used))
		{
			minU = std::min(minU, surface.texCoords[v].u);
			maxU = std::max(maxU, surface.texCoords[v].u);
			minV = std::min(minV, surface.texCoords[v].v);
			maxV = std::max(maxV, surface.texCoords[v].v);
		}
		const float extent = std::max(maxU - minU, maxV - minV);
		if (extent > 0)
		{
			for (int v : std::as_const(used))
				surface.texCoords[v] = {(surface.texCoords[v].u - minU) / extent, (surface.texCoords[v].v - minV) / extent};
		}
	}
	// Axis corners left unused once every face has its own copy are dropped.
	QSet<int> spare;
	for (auto it = users.cbegin(); it != users.cend(); ++it)
		spare.insert(it.key());
	pruneSeams(&surface);
	return compactUnused(&surface, spare, selection, work);
}
} // namespace vibestudio::model_tools
