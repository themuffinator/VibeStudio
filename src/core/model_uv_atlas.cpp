#include "core/model_uv_atlas.h"
#include "core/model_uv_mapping.h"
#include "core/model_uv.h"
#include "core/model_uv_atlas_memory.h"

#include <QCoreApplication>
#include <QMap>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <numeric>
#include <xatlas.h>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
std::array<int, 3> corners(ModelTriangle t)
{
	return {t.a, t.b, t.c};
}
struct Sets
{
	QVector<int> parent;
	explicit Sets(int count) : parent(count)
	{
		std::iota(parent.begin(), parent.end(), 0);
	}
	int root(int index)
	{
		while (parent[index] != index)
		{
			parent[index] = parent[parent[index]];
			index = parent[index];
		}
		return index;
	}
	void join(int a, int b)
	{
		a = root(a);
		b = root(b);
		parent[std::max(a, b)] = std::min(a, b);
	}
};
struct Input
{
	QVector<int> faces, sourceVertices;
	QVector<uint32_t> indices, materials;
	QVector<ModelVec3> positions, normals;
	QVector<ModelTexCoord> uv;
};

bool prepare(const ModelSurface &source, const QSet<int> &selected, int frame, bool unwrap, int width, int height, Input *input,
			 QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	ModelUvTopology topology;
	if (!buildModelUvTopology(source, &topology, error, control))
	{
		return false;
	}
	input->faces = selected.values();
	std::sort(input->faces.begin(), input->faces.end());
	QVector<int> local(source.triangles.size(), -1);
	for (int i = 0; i < input->faces.size(); ++i)
	{
		local[input->faces[i]] = i;
	}
	Sets fans(input->faces.size() * 3), charts(input->faces.size());
	for (const auto &edge : topology.edges)
	{
		if (!work.step())
		{
			return false;
		}
		if (edge.seam || edge.faces.size() != 2)
		{
			continue;
		}
		const int a = local[edge.faces[0]], b = local[edge.faces[1]];
		if (a < 0 || b < 0)
		{
			continue;
		}
		charts.join(a, b);
		const auto ca = corners(source.triangles[edge.faces[0]]), cb = corners(source.triangles[edge.faces[1]]);
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				if (ca[i] == cb[j] && (ca[i] == edge.vertices.first || ca[i] == edge.vertices.second))
				{
					fans.join(a * 3 + i, b * 3 + j);
				}
			}
		}
	}
	QMap<int, int> roots;
	for (int face = 0; face < input->faces.size(); ++face)
	{
		if (!work.step())
		{
			return false;
		}
		input->materials.append(charts.root(face));
		const auto c = corners(source.triangles[input->faces[face]]);
		for (int i = 0; i < 3; ++i)
		{
			const int root = fans.root(face * 3 + i);
			if (!roots.contains(root))
			{
				roots.insert(root, input->sourceVertices.size());
				input->sourceVertices.append(c[i]);
			}
			input->indices.append(roots[root]);
		}
	}
	// An isolated interior seam can reconnect around both endpoint fans. Give
	// one incident face its own edge corners; automatic unwrap may add cuts.
	if (unwrap)
	{
		for (const auto &edge : topology.edges)
		{
			if (!work.step())
			{
				return false;
			}
			if (!edge.seam || edge.faces.size() != 2 || local[edge.faces[0]] < 0 || local[edge.faces[1]] < 0)
			{
				continue;
			}
			const int a = local[edge.faces[0]], b = local[edge.faces[1]];
			const auto ca = corners(source.triangles[edge.faces[0]]), cb = corners(source.triangles[edge.faces[1]]);
			for (int i = 0; i < 3; ++i)
			{
				if (ca[i] != edge.vertices.first && ca[i] != edge.vertices.second)
				{
					continue;
				}
				for (int j = 0; j < 3; ++j)
				{
					if (ca[i] == cb[j] && input->indices[a * 3 + i] == input->indices[b * 3 + j])
					{
						input->indices[b * 3 + j] = input->sourceVertices.size();
						input->sourceVertices.append(cb[j]);
					}
				}
			}
		}
	}
	// Normalize library input, not document geometry. A stable magnitude avoids
	// xatlas's absolute area tolerance rejecting otherwise usable small meshes.
	ModelVec3 lo{1e6f, 1e6f, 1e6f}, hi{-1e6f, -1e6f, -1e6f};
	ModelTexCoord uvLo{1e6f, 1e6f}, uvHi{-1e6f, -1e6f};
	for (int v : input->sourceVertices)
	{
		if (!work.step())
		{
			return false;
		}
		const auto p = source.frames[frame].positions[v];
		lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
		hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
		const auto uv = source.texCoords[v];
		uvLo = {std::min(uvLo.u, uv.u), std::min(uvLo.v, uv.v)};
		uvHi = {std::max(uvHi.u, uv.u), std::max(uvHi.v, uv.v)};
	}
	// Existing UVs describe this texture's normalized axes. Pack in pixel space
	// so rectangular textures retain chart shape and relative density.
	const double uAspect = double(width) / height;
	const double extent = unwrap ? std::max({double(hi.x) - lo.x, double(hi.y) - lo.y, double(hi.z) - lo.z})
								 : std::max((double(uvHi.u) - uvLo.u) * uAspect, double(uvHi.v) - uvLo.v);
	if (extent <= 0 || !std::isfinite(extent))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
													   "Selected faces have no usable area. Repair geometry or project UVs first."));
	}
	const double scale = 1024.0 / extent;
	for (int v : input->sourceVertices)
	{
		if (!work.step())
		{
			return false;
		}
		const auto p = source.frames[frame].positions[v];
		input->positions.append(
			{float((double(p.x) - lo.x) * scale), float((double(p.y) - lo.y) * scale), float((double(p.z) - lo.z) * scale)});
		input->normals.append(source.frames[frame].normals[v]);
		const auto uv = source.texCoords[v];
		input->uv.append({float((double(uv.u) - uvLo.u) * uAspect * scale), float((double(uv.v) - uvLo.v) * scale)});
	}
	return work.check();
}

struct Progress
{
	const ModelWorkControl &control;
	QString *error;
	bool stopped = false;
	static bool callback(xatlas::ProgressCategory category, int percent, void *context)
	{
		auto &self = *static_cast<Progress *>(context);
		self.stopped =
			self.stopped || !modelWorkCheckpoint(self.control, ModelWorkPhase::Editing, int(category) * 100 + percent, 400, self.error);
		return !self.stopped;
	}
};
struct AtlasOwner
{
	xatlas::Atlas *value = xatlas::Create();
	~AtlasOwner()
	{
		// An allocator exception may leave the upstream graph incomplete. The
		// enclosing allocation region releases all its blocks in that case.
		if (std::uncaught_exceptions() == 0)
		{
			xatlas::Destroy(value);
		}
	}
};
struct UvTriangle
{
	std::array<ModelTexCoord, 3> p;
	double minX, maxX, minY, maxY;
};
bool overlaps(const UvTriangle &a, const UvTriangle &b)
{
	for (const auto *triangle : {&a, &b})
	{
		for (int i = 0; i < 3; ++i)
		{
			const auto p = triangle->p[i], q = triangle->p[(i + 1) % 3];
			const double x = double(q.v) - p.v, y = double(p.u) - q.u;
			double loA = 1e20, hiA = -1e20, loB = 1e20, hiB = -1e20;
			for (int j = 0; j < 3; ++j)
			{
				const double pa = x * a.p[j].u + y * a.p[j].v, pb = x * b.p[j].u + y * b.p[j].v;
				loA = std::min(loA, pa);
				hiA = std::max(hiA, pa);
				loB = std::min(loB, pb);
				hiB = std::max(hiB, pb);
			}
			if (std::min(hiA, hiB) - std::max(loA, loB) <= 1e-10 * std::hypot(x, y))
			{
				return false;
			}
		}
	}
	return true;
}
bool validateAtlas(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	QVector<UvTriangle> triangles;
	for (int i = 0; i < indices.size(); i += 3)
	{
		if (!work.step())
		{
			return false;
		}
		UvTriangle t;
		for (int j = 0; j < 3; ++j)
		{
			t.p[j] = uv[indices[i + j]];
		}
		const auto a = t.p[0], b = t.p[1], c = t.p[2];
		const double area = (double(b.u) - a.u) * (double(c.v) - a.v) - (double(b.v) - a.v) * (double(c.u) - a.u);
		if (std::abs(area) < 1e-14)
		{
			return fail(error, QCoreApplication::translate(
								   "VibeStudioModelUvAtlas",
								   "A selected face has collapsed UVs. Repair geometry, unwrap again, or increase atlas resolution."));
		}
		t.minX = std::min({a.u, b.u, c.u});
		t.maxX = std::max({a.u, b.u, c.u});
		t.minY = std::min({a.v, b.v, c.v});
		t.maxY = std::max({a.v, b.v, c.v});
		triangles.append(t);
	}
	std::stable_sort(triangles.begin(), triangles.end(), [](const auto &a, const auto &b) { return a.minX < b.minX; });
	qint64 comparisons = 0;
	for (int i = 0; i < triangles.size(); ++i)
	{
		const auto &a = triangles[i];
		for (int j = i + 1; j < triangles.size() && triangles[j].minX < a.maxX; ++j)
		{
			if (!work.step())
			{
				return false;
			}
			if (++comparisons > 8000000)
			{
				return fail(error,
							QCoreApplication::translate("VibeStudioModelUvAtlas",
														"UV overlap validation reached its work limit. Process fewer faces at a time."));
			}
			const auto &b = triangles[j];
			if (a.maxY <= b.minY || b.maxY <= a.minY)
			{
				continue;
			}
			if (overlaps(a, b))
			{
				return fail(
					error, QCoreApplication::translate("VibeStudioModelUvAtlas",
													   "Selected UV faces overlap inside an island. Add seams and unwrap before packing."));
			}
		}
	}
	return work.check();
}
} // namespace

bool validateModelUvMapping(const QVector<ModelTexCoord> &uv, const QVector<uint32_t> &indices, QString *error,
							const ModelWorkControl &control)
{
	return validateAtlas(uv, indices, error, control);
}

bool atlasModelUv(const ModelSurface &source, const QSet<int> &faces, int frame, bool unwrap, const ModelUvAtlasOptions &options,
				  ModelSurface *result, QString *error, const ModelWorkControl &control, ModelUvAtlasReport *report)
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
	const int height = options.height == 0 ? options.resolution : options.height;
	if (!result || faces.isEmpty() || frame < 0 || frame >= source.frames.size() || options.resolution < 32 || options.resolution > 4096 ||
		height < 32 || height > 4096 || options.padding < 0 || options.padding > 64 ||
		options.padding * 8 >= std::min(options.resolution, height) || options.memoryLimit < 65536 ||
		options.memoryLimit > 256 * 1024 * 1024 || options.vertexLimit < 1 || options.vertexLimit > 65536)
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelUvAtlas",
												"Select faces and a valid reference frame. Atlas width and height must be 32–4096 pixels; "
												"padding must be 0–64 and less than one eighth of the smaller dimension."));
	}
	for (int face : faces)
	{
		if (face < 0 || face >= source.triangles.size())
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "A selected atlas face no longer exists."));
		}
	}
	Input input;
	if (!prepare(source, faces, frame, unwrap, options.resolution, height, &input, error, control))
	{
		return false;
	}
	model_atlas_internal::AllocationRegion region(size_t(options.memoryLimit), control);
	try
	{
		// Uses xatlas's reviewed MIT/BSD implementation; see external/modelling/xatlas/VIBESTUDIO.md.
		Progress progress{control, error};
		AtlasOwner atlas;
		xatlas::SetProgressCallback(atlas.value, Progress::callback, &progress);
		xatlas::AddMeshError added;
		if (unwrap)
		{
			xatlas::MeshDecl decl;
			decl.vertexPositionData = input.positions.constData();
			decl.vertexPositionStride = sizeof(ModelVec3);
			decl.vertexNormalData = input.normals.constData();
			decl.vertexNormalStride = sizeof(ModelVec3);
			decl.vertexCount = input.sourceVertices.size();
			decl.indexData = input.indices.constData();
			decl.indexCount = input.indices.size();
			decl.indexFormat = xatlas::IndexFormat::UInt32;
			decl.faceMaterialData = input.materials.constData();
			added = xatlas::AddMesh(atlas.value, decl, 1);
		}
		else
		{
			xatlas::UvMeshDecl decl;
			decl.vertexUvData = input.uv.constData();
			decl.vertexStride = sizeof(ModelTexCoord);
			decl.vertexCount = input.uv.size();
			decl.indexData = input.indices.constData();
			decl.indexCount = input.indices.size();
			decl.indexFormat = xatlas::IndexFormat::UInt32;
			decl.faceMaterialData = input.materials.constData();
			added = xatlas::AddUvMesh(atlas.value, decl);
		}
		if (progress.stopped || !work.check())
		{
			return false;
		}
		if (added != xatlas::AddMeshError::Success)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "The atlas input could not be prepared: %1.")
								   .arg(QString::fromLatin1(xatlas::StringForEnum(added))));
		}
		xatlas::ChartOptions charts;
		charts.maxIterations = 2;
		xatlas::ComputeCharts(atlas.value, charts);
		if (progress.stopped || !work.check())
		{
			return false;
		}
		xatlas::PackOptions pack;
		pack.resolution = options.resolution - 2 * options.padding;
		pack.resolutionHeight = height - 2 * options.padding;
		pack.padding = options.padding;
		// Existing islands retain orientation, shape and relative scale. No
		// lightmap-style per-axis rounding or reflected quarter-turn placement.
		pack.rotateCharts = false;
		pack.rotateChartsToAxis = unwrap;
		xatlas::PackCharts(atlas.value, pack);
		if (progress.stopped || !work.check())
		{
			return false;
		}
		const uint32_t maximum = std::max(atlas.value->width, atlas.value->height);
		if (maximum == 0 || atlas.value->chartCount == 0)
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelUvAtlas",
													"No usable UV charts were produced. Repair degenerate faces or project UVs first."));
		}
		pack.texelsPerUnit =
			atlas.value->texelsPerUnit *
			std::min({1.0f, float(pack.resolution - 1) / atlas.value->width, float(pack.resolutionHeight - 1) / atlas.value->height}) *
			0.9f;
		const bool rectangular = options.resolution != height;
		if (rectangular && atlas.value->meshCount == 1 && atlas.value->meshes)
		{
			// Bound uniform density by each chart, rather than shrinking an entire
			// provisional square arrangement to the short axis. This lets charts
			// use the long axis without per-chart resizing or aspect distortion.
			QMap<int, std::array<float, 4>> bounds;
			const auto &provisional = atlas.value->meshes[0];
			for (uint32_t v = 0; v < provisional.vertexCount; ++v)
			{
				if (!work.step())
				{
					return false;
				}
				const auto &vertex = provisional.vertexArray[v];
				if (vertex.chartIndex < 0)
				{
					continue;
				}
				if (!bounds.contains(vertex.chartIndex))
				{
					bounds.insert(vertex.chartIndex, {vertex.uv[0], vertex.uv[0], vertex.uv[1], vertex.uv[1]});
				}
				auto &box = bounds[vertex.chartIndex];
				box = {std::min(box[0], vertex.uv[0]), std::max(box[1], vertex.uv[0]), std::min(box[2], vertex.uv[1]),
					   std::max(box[3], vertex.uv[1])};
			}
			float ratio = std::numeric_limits<float>::max();
			for (const auto &box : bounds)
			{
				if (box[1] > box[0])
				{
					ratio = std::min(ratio, (pack.resolution - 2.f) / (box[1] - box[0]));
				}
				if (box[3] > box[2])
				{
					ratio = std::min(ratio, (pack.resolutionHeight - 2.f) / (box[3] - box[2]));
				}
			}
			if (std::isfinite(ratio) && ratio > 0 && !bounds.isEmpty())
				pack.texelsPerUnit = atlas.value->texelsPerUnit * ratio * 0.99f;
		}
		float failedDensity = 0;
		for (int attempt = 0; attempt < 12; ++attempt)
		{
			xatlas::PackCharts(atlas.value, pack);
			if (progress.stopped || !work.check())
			{
				return false;
			}
			if (atlas.value->atlasCount == 1)
			{
				break;
			}
			failedDensity = pack.texelsPerUnit;
			pack.texelsPerUnit *= 0.7f;
		}
		if (rectangular && atlas.value->atlasCount == 1 && failedDensity > pack.texelsPerUnit)
		{
			float fitted = pack.texelsPerUnit;
			// Four bounded refinements recover useful texel density after finding
			// a fit. No exhaustive optimum is claimed by this heuristic packer.
			for (int attempt = 0; attempt < 4; ++attempt)
			{
				pack.texelsPerUnit = (fitted + failedDensity) * 0.5f;
				xatlas::PackCharts(atlas.value, pack);
				if (progress.stopped || !work.check())
				{
					return false;
				}
				if (atlas.value->atlasCount == 1)
				{
					fitted = pack.texelsPerUnit;
				}
				else
				{
					failedDensity = pack.texelsPerUnit;
				}
			}
			pack.texelsPerUnit = fitted;
			xatlas::PackCharts(atlas.value, pack);
			if (progress.stopped || !work.check())
			{
				return false;
			}
		}
		if (atlas.value->atlasCount != 1 || atlas.value->meshCount != 1 || !atlas.value->meshes)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
														   "The charts do not fit one atlas. Increase its size or reduce padding."));
		}
		const auto &output = atlas.value->meshes[0];
		if (output.indexCount != uint32_t(input.indices.size()))
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
														   "Atlas generation changed the face count. No changes were applied."));
		}
		QVector<ModelTexCoord> uv;
		QVector<uint32_t> indices;
		for (uint32_t v = 0; v < output.vertexCount; ++v)
		{
			if (!work.step())
			{
				return false;
			}
			const auto &vertex = output.vertexArray[v];
			const ModelTexCoord point{(vertex.uv[0] + options.padding) / options.resolution, (vertex.uv[1] + options.padding) / height};
			if (vertex.atlasIndex != 0 || vertex.chartIndex < 0 || vertex.xref >= uint32_t(input.sourceVertices.size()) ||
				!std::isfinite(point.u) || !std::isfinite(point.v) || point.u < 0 || point.v < 0 || point.u > 1 || point.v > 1)
			{
				return fail(error, QCoreApplication::translate(
									   "VibeStudioModelUvAtlas",
									   "A selected face could not be mapped into the atlas. Repair degenerate geometry or UVs first."));
			}
			uv.append(point);
		}
		for (uint32_t i = 0; i < output.indexCount; ++i)
		{
			if (!work.step())
			{
				return false;
			}
			const auto index = output.indexArray[i];
			if (index >= uint32_t(uv.size()) ||
				input.sourceVertices[output.vertexArray[index].xref] != input.sourceVertices[input.indices[i]])
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas",
															   "Atlas generation changed a face corner. No changes were applied."));
			}
			indices.append(index);
		}
		if (!validateAtlas(uv, indices, error, control))
		{
			return false;
		}
		QVector<int> sourceVertices;
		for (uint32_t v = 0; v < output.vertexCount; ++v)
		{
			if (!work.step())
				return false;
			sourceVertices.append(input.sourceVertices[output.vertexArray[v].xref]);
		}
		if (!applyModelUvMapping(source, input.faces, sourceVertices, uv, indices, options.vertexLimit, result, error, control))
			return false;
		if (report)
		{
			*report = {int(atlas.value->chartCount), qsizetype(region.peakBytes())};
		}
		return true;
	}
	catch (const model_atlas_internal::Cancelled &)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelUvAtlas", "UV atlas operation cancelled. No changes were applied."));
	}
	catch (const std::bad_alloc &)
	{
		return fail(
			error, QCoreApplication::translate("VibeStudioModelUvAtlas",
											   "UV atlas generation exceeded its memory budget. Reduce the face selection or atlas size."));
	}
}
} // namespace vibestudio
