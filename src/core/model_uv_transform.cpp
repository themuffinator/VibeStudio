#include "core/model_uv.h"

#include "core/model_document.h"

#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
bool transformModelUvIslands(const ModelSurface &source, const QSet<int> &faces, const ModelUvIslandTransform &transform,
							 ModelSurface *result, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	const auto fail = [&](const QString &message) {
		if (error)
			*error = message;
		return false;
	};
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	const auto bounded = [](double value) { return std::isfinite(value) && std::abs(value) <= 1000000; };
	if (!result || !bounded(transform.scale.u) || !bounded(transform.scale.v) || std::abs(transform.scale.u) < 1e-6f ||
		std::abs(transform.scale.v) < 1e-6f || !bounded(transform.offset.u) || !bounded(transform.offset.v) ||
		!std::isfinite(transform.rotation) || std::abs(transform.rotation) > 36000 || transform.projection < -1 ||
		transform.projection > 2 || transform.referenceFrame < 0 || transform.referenceFrame >= source.frames.size() ||
		transform.vertexLimit < source.vertexCount || transform.vertexLimit > modelDocumentMaxVertices ||
		source.frames.size() > modelDocumentMaxFrames)
		return fail(QCoreApplication::translate("ModelUvTransform",
												"Choose valid island transform values, a reference pose and sufficient vertex capacity."));
	for (const auto &frame : source.frames)
	{
		if (!work.step())
			return false;
		if (frame.positions.size() != source.vertexCount || frame.normals.size() != source.vertexCount)
			return fail(QCoreApplication::translate("ModelUvTransform",
													"Every animation pose must retain the surface's complete vertex and normal arrays."));
	}
	ModelUvTopology topology;
	if (!buildModelUvTopology(source, &topology, error, control))
		return false;
	QVector<bool> selected(topology.islands.size(), false);
	for (int face : faces)
	{
		if (!work.step())
			return false;
		if (face < 0 || face >= topology.faceIsland.size())
			return fail(QCoreApplication::translate("ModelUvTransform", "A selected UV face no longer exists."));
		selected[topology.faceIsland[face]] = true;
	}
	if (faces.isEmpty())
		return fail(QCoreApplication::translate("ModelUvTransform",
												"Select complete UV islands. Use Select Islands to expand the current components first."));
	for (int island = 0; island < topology.islands.size(); ++island)
	{
		if (!work.step())
			return false;
		if (!selected[island])
			continue;
		for (int face : topology.islands[island].faces)
		{
			if (!work.step())
				return false;
			if (!faces.contains(face))
				return fail(QCoreApplication::translate(
					"ModelUvTransform", "Select complete UV islands. Use Select Islands to expand the current components first."));
		}
	}
	// Unselected faces retain their original indices. Otherwise the first selected
	// island owns the original; later islands sharing that index receive copies.
	QVector<int> owners(source.vertexCount, -2);
	for (int face = 0; face < source.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		if (!faces.contains(face))
		{
			const auto triangle = source.triangles[face];
			for (int vertex : {triangle.a, triangle.b, triangle.c})
				owners[vertex] = -1;
		}
	}
	qint64 count = source.vertexCount;
	for (int island = 0; island < topology.islands.size(); ++island)
	{
		if (!work.step())
			return false;
		if (!selected[island])
			continue;
		for (int vertex : topology.islands[island].vertices)
		{
			if (!work.step())
				return false;
			if (owners[vertex] == -2)
				owners[vertex] = island;
			else
				++count;
		}
	}
	if (count > transform.vertexLimit || count * source.frames.size() > modelDocumentMaxFrameVertices)
		return fail(
			QCoreApplication::translate("ModelUvTransform", "Splitting UV islands would exceed the vertex or animation storage limit."));
	ModelSurface candidate = source;
	candidate.texCoords.reserve(count);
	for (auto &frame : candidate.frames)
	{
		if (!work.step())
			return false;
		frame.positions.reserve(count);
		frame.normals.reserve(count);
	}
	const auto coordinate = [&](int vertex) {
		if (transform.projection < 0)
			return source.texCoords[vertex];
		const auto p = source.frames[transform.referenceFrame].positions[vertex];
		return transform.projection == 0   ? ModelTexCoord{p.x, -p.y}
			   : transform.projection == 1 ? ModelTexCoord{p.x, -p.z}
										   : ModelTexCoord{p.y, -p.z};
	};
	QVector<int> mapped(source.vertexCount, -1);
	const double radians = transform.rotation * std::numbers::pi / 180;
	const double cosine = std::cos(radians), sine = std::sin(radians);
	for (int island = 0; island < topology.islands.size(); ++island)
	{
		if (!work.step())
			return false;
		if (!selected[island])
			continue;
		const auto &chart = topology.islands[island];
		auto lo = coordinate(chart.vertices.first()), hi = lo;
		for (int vertex : chart.vertices)
		{
			if (!work.step())
				return false;
			const auto uv = coordinate(vertex);
			lo.u = std::min(lo.u, uv.u);
			lo.v = std::min(lo.v, uv.v);
			hi.u = std::max(hi.u, uv.u);
			hi.v = std::max(hi.v, uv.v);
		}
		const double pivotU = (double(lo.u) + hi.u) / 2, pivotV = (double(lo.v) + hi.v) / 2;
		for (int vertex : chart.vertices)
		{
			if (!work.step())
				return false;
			int target = vertex;
			if (owners[vertex] != island)
			{
				target = candidate.texCoords.size();
				candidate.texCoords.append(source.texCoords[vertex]);
				for (int frame = 0; frame < source.frames.size(); ++frame)
				{
					if (!work.step())
						return false;
					candidate.frames[frame].positions.append(source.frames[frame].positions[vertex]);
					candidate.frames[frame].normals.append(source.frames[frame].normals[vertex]);
				}
			}
			mapped[vertex] = target;
			const auto uv = coordinate(vertex);
			const double u = (uv.u - pivotU) * transform.scale.u, v = (uv.v - pivotV) * transform.scale.v;
			const double newU = u * cosine - v * sine + pivotU + transform.offset.u;
			const double newV = u * sine + v * cosine + pivotV + transform.offset.v;
			if (!bounded(newU) || !bounded(newV))
				return fail(QCoreApplication::translate("ModelUvTransform", "Transformed UVs exceed the supported coordinate range."));
			candidate.texCoords[target] = {float(newU), float(newV)};
		}
		for (int face : chart.faces)
		{
			if (!work.step())
				return false;
			const auto triangle = source.triangles[face];
			candidate.triangles[face] = {mapped[triangle.a], mapped[triangle.b], mapped[triangle.c]};
		}
	}
	// Rebuild marked edges from each original face so both sides of every split
	// retain their marks, including point contacts and nonmanifold boundaries.
	candidate.uvSeams.clear();
	for (int face = 0; face < source.triangles.size(); ++face)
	{
		if (!work.step())
			return false;
		const auto old = source.triangles[face], updated = candidate.triangles[face];
		const int from[]{old.a, old.b, old.c}, to[]{updated.a, updated.b, updated.c};
		for (int corner = 0; corner < 3; ++corner)
		{
			const int next = (corner + 1) % 3;
			if (source.uvSeams.contains(modelEdge(from[corner], from[next])))
				candidate.uvSeams.insert(modelEdge(to[corner], to[next]));
		}
	}
	candidate.vertexCount = candidate.texCoords.size();
	if (!work.check())
		return false;
	*result = std::move(candidate);
	return true;
}
} // namespace vibestudio
