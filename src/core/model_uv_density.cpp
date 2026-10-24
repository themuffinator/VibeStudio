#include "core/model_uv_density.h"
#include "core/model_document.h"
#include "core/model_geometry_helpers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvDensity)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool validSize(QSize size) { return size.width() >= 1 && size.height() >= 1 && size.width() <= 65536 && size.height() <= 65536; }
} // namespace

bool inspectModelUvDensity(const ModelSurface &surface, const QSet<int> &faces, QSize textureSize, int referenceFrame,
						   ModelUvDensityReport *report, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	if (!report || !validSize(textureSize) || referenceFrame < 0 || referenceFrame >= surface.frames.size() ||
		surface.frames[referenceFrame].positions.size() != surface.vertexCount)
		return fail(error, Text::tr("Choose an existing reference pose and texture dimensions from 1 to 65,536 pixels."));
	ModelUvTopology topology;
	if (!buildModelUvTopology(surface, &topology, error, control))
		return false;
	QSet<int> chosen;
	for (int face : faces)
	{
		if (!work.step())
			return false;
		if (face < 0 || face >= topology.faceIsland.size())
			return fail(error, Text::tr("A selected UV face no longer exists."));
		chosen.insert(topology.faceIsland[face]);
	}
	ModelUvDensityReport result;
	result.textureSize = textureSize;
	result.referenceFrame = referenceFrame;
	const auto &positions = surface.frames[referenceFrame].positions;
	const double pixels = double(textureSize.width()) * textureSize.height();
	using namespace model_geometry;
	for (int island = 0; island < topology.islands.size(); ++island)
	{
		if (!work.step())
			return false;
		if (!faces.isEmpty() && !chosen.contains(island))
			continue;
		ModelUvDensityIsland row;
		row.island = island;
		row.faces = topology.islands[island].faces;
		row.minimum = std::numeric_limits<double>::infinity();
		for (int face : row.faces)
		{
			if (!work.step())
				return false;
			result.faces.insert(face);
			const auto t = surface.triangles[face];
			const auto a = point(positions[t.a]), b = point(positions[t.b]), c = point(positions[t.c]);
			const auto u = surface.texCoords[t.a], v = surface.texCoords[t.b], w = surface.texCoords[t.c];
			const double area = .5 * length(cross(b - a, c - a));
			const double x = double(v.u) - u.u, y = double(v.v) - u.v, z = double(w.u) - u.u, h = double(w.v) - u.v;
			const double signedUv = x * h - y * z;
			const double uvArea = .5 * std::abs(signedUv) * pixels;
			if (!std::isfinite(area) || !std::isfinite(uvArea))
				return fail(error, Text::tr("UV density requires finite geometry and UV coordinates."));
			const bool collapsed = collapsedTriangle(positions[t.a], positions[t.b], positions[t.c]);
			const bool collapsedUv = std::abs(signedUv) <= 1e-12 * std::max({x * x + y * y, z * z + h * h, 1e-24});
			row.collapsedGeometry += collapsed;
			row.collapsedUvs += collapsedUv;
			row.mirroredFaces += signedUv < 0;
			row.modelArea += area;
			row.pixelArea += uvArea;
			if (!collapsed)
			{
				const double density = std::sqrt(uvArea / area);
				row.minimum = std::min(row.minimum, density);
				row.maximum = std::max(row.maximum, density);
			}
		}
		if (!std::isfinite(row.minimum))
			row.minimum = 0;
		row.density = row.modelArea > 0 ? std::sqrt(row.pixelArea / row.modelArea) : 0;
		result.modelArea += row.modelArea;
		result.pixelArea += row.pixelArea;
		result.islands.append(row);
	}
	if (result.islands.isEmpty())
		return fail(error, Text::tr("This surface has no UV islands to measure."));
	result.density = result.modelArea > 0 ? std::sqrt(result.pixelArea / result.modelArea) : 0;
	if (!work.check())
		return false;
	*report = std::move(result);
	return true;
}

QJsonObject modelUvDensityJson(const ModelUvDensityReport &report)
{
	QJsonArray islands;
	for (const auto &row : report.islands)
	{
		QJsonArray faces;
		for (int face : row.faces)
			faces.append(face);
		islands.append(QJsonObject{{"island", row.island},
								   {"faces", faces},
								   {"modelArea", row.modelArea},
								   {"pixelArea", row.pixelArea},
								   {"pixelsPerUnit", row.density},
								   {"minimumFaceDensity", row.minimum},
								   {"maximumFaceDensity", row.maximum},
								   {"collapsedGeometry", row.collapsedGeometry},
								   {"collapsedUvs", row.collapsedUvs},
								   {"mirroredFaces", row.mirroredFaces},
								   {"scalable", row.scalable()}});
	}
	return {{"textureWidth", report.textureSize.width()},
			{"textureHeight", report.textureSize.height()},
			{"referenceFrame", report.referenceFrame},
			{"modelArea", report.modelArea},
			{"pixelArea", report.pixelArea},
			{"pixelsPerUnit", report.density},
			{"islands", islands}};
}

bool applyModelUvDensity(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	const auto &o = edit.uvDensity;
	if (!mesh || !selection || selection->surface < 0 || selection->surface >= mesh->surfaces.size() || edit.frame != -1 ||
		!std::isfinite(o.target) || o.target < .000001 || o.target > 1000000)
		return fail(error, Text::tr("UV density spans every pose and needs a target from 0.000001 to 1,000,000 pixels per model unit."));
	const auto &source = mesh->surfaces[selection->surface];
	ModelUvTopology topology;
	QSet<int> faces;
	if (!buildModelUvTopology(source, &topology, error, control) ||
		!expandModelUvIslands(source, topology, selection->faces, selection->vertices, selection->edges, &faces, error, control))
		return false;
	if (faces.isEmpty())
		return fail(error, Text::tr("Select mesh components or complete UV islands before setting density."));
	ModelUvDensityReport before;
	if (!inspectModelUvDensity(source, faces, o.textureSize, o.referenceFrame, &before, error, control))
		return false;
	ModelUvIslandTransform transform;
	QSet<int> changedFaces;
	transform.referenceFrame = o.referenceFrame;
	qint64 other = 0;
	for (int s = 0; s < mesh->surfaces.size(); ++s)
		if (s != selection->surface)
			other += mesh->surfaces[s].vertexCount;
	transform.vertexLimit = int(
		std::min<qint64>(modelDocumentMaxVertices, modelDocumentMaxFrameVertices / std::max<qsizetype>(1, mesh->frames.size())) - other);
	for (const auto &row : before.islands)
	{
		if (!work.step())
			return false;
		if (!row.scalable())
			return fail(
				error,
				Text::tr("Island %1 has collapsed geometry or UV faces. Repair or unwrap it before setting density.").arg(row.island));
		const double factor = o.target / (o.individual ? row.density : before.density);
		if (std::abs(factor - 1) <= 1e-12)
			continue;
		transform.islandScales.insert(row.island, factor);
		for (int face : row.faces)
		{
			if (!work.step())
				return false;
			changedFaces.insert(face);
		}
	}
	if (changedFaces.isEmpty())
	{
		if (!work.check())
			return false;
		*selection = {selection->surface, {}, faces};
		return true;
	}
	ModelSurface result;
	if (!transformModelUvIslands(source, changedFaces, transform, &result, error, control))
		return false;
	ModelUvDensityReport after;
	if (!inspectModelUvDensity(result, faces, o.textureSize, o.referenceFrame, &after, error, control))
		return false;
	// Float UV storage can lose very small changes far outside the base tile.
	// Verify the stored result instead of claiming an unachievable target.
	for (int i = 0; i < after.islands.size(); ++i)
	{
		if (!work.step())
			return false;
		const double expected = o.individual ? o.target : before.islands[i].density * o.target / before.density;
		if (!after.islands[i].scalable() || std::abs(after.islands[i].density - expected) > expected * .0001)
			return fail(
				error,
				Text::tr(
					"Stored UV precision cannot represent this density here. Move islands nearer the origin or choose a larger target."));
	}
	if (!work.check())
		return false;
	mesh->surfaces[selection->surface] = std::move(result);
	*selection = {selection->surface, {}, faces};
	return true;
}
} // namespace vibestudio
