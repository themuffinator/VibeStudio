#include "core/model_uv_obstacles.h"
#include "core/model_uv_mapping.h"
#include "core/model_uv.h"
#include "core/package_archive.h"
#include <QCoreApplication>
#include <QMap>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelUvObstacles)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
struct Work
{
	const ModelWorkControl &control;
	QString *error;
	quint64 limit, units = 0, checked = 0;
	bool failed = false;
	bool step(quint64 count = 1)
	{
		if (failed)
			return false;
		units += count;
		if (units > limit)
		{
			failed = true;
			return fail(error,
						Text::tr("UV obstacle packing reached its raster/search work limit. Reduce the selection or atlas dimensions."));
		}
		if (units - checked < 256)
			return true;
		checked = units;
		return check();
	}
	bool check()
	{
		failed = failed || !modelWorkCheckpoint(control, ModelWorkPhase::Editing, units, 0, error);
		return !failed;
	}
};
struct Bitmap
{
	int width, height, stride;
	quint64 count = 0;
	QVector<quint64> words;
	Bitmap(int w, int h) : width(w), height(h), stride((w + 63) / 64), words(qsizetype(stride) * h, 0)
	{
	}
	bool range(int y, int lo, int hi, Work &work)
	{
		lo = std::max(0, lo);
		hi = std::min(width - 1, hi);
		if (lo > hi || y < 0 || y >= height)
			return true;
		for (int x = lo / 64; x <= hi / 64; ++x)
		{
			if (!work.step())
				return false;
			const auto mask = (~quint64(0) << (x == lo / 64 ? lo % 64 : 0)) & (~quint64(0) >> (x == hi / 64 ? 63 - hi % 64 : 0));
			auto &word = words[y * stride + x];
			count += std::popcount(mask & ~word);
			word |= mask;
		}
		return true;
	}
};
struct Point
{
	double x, y;
};
using Triangle = std::array<Point, 3>;
std::array<int, 3> corners(ModelTriangle t)
{
	return {t.a, t.b, t.c};
}

// Intersect a convex triangle with each closed texel-row strip, then mark its
// horizontal projection. This conservatively includes boundary/degenerate
// contacts without testing every texel against every face.
bool raster(const Triangle &t, Bitmap &mask, Work &work)
{
	const double low = std::min({t[0].y, t[1].y, t[2].y}), high = std::max({t[0].y, t[1].y, t[2].y});
	const int first = std::max(0, int(std::ceil(low)) - 1), last = std::min(mask.height - 1, int(std::floor(high)));
	for (int y = first; y <= last; ++y)
	{
		if (!work.step())
			return false;
		double lo = std::numeric_limits<double>::infinity(), hi = -lo;
		const auto add = [&](double x) {
			lo = std::min(lo, x);
			hi = std::max(hi, x);
		};
		for (int i = 0; i < 3; ++i)
		{
			const auto a = t[i], b = t[(i + 1) % 3];
			if (a.y >= y && a.y <= y + 1)
				add(a.x);
			if (a.y != b.y)
				for (double boundary : {double(y), double(y + 1)})
					if (boundary >= std::min(a.y, b.y) && boundary <= std::max(a.y, b.y))
						add(a.x + (b.x - a.x) * ((boundary - a.y) / (b.y - a.y)));
		}
		if (std::isfinite(lo) && !mask.range(y, int(std::ceil(lo)) - 1, int(std::floor(hi)), work))
			return false;
	}
	return true;
}
bool stamp(const Bitmap &mask, Bitmap &board, int x, int y, int padding, Work &work)
{
	for (int row = 0; row < mask.height; ++row)
		for (int column = 0; column < mask.stride; ++column)
		{
			if (!work.step())
				return false;
			auto bits = mask.words[row * mask.stride + column];
			while (bits)
			{
				const int start = std::countr_zero(bits), length = std::countr_one(bits >> start);
				for (int yy = std::max(0, y + row - padding); yy <= std::min(board.height - 1, y + row + padding); ++yy)
					if (!board.range(yy, x + column * 64 + start - padding, x + column * 64 + start + length - 1 + padding, work))
						return false;
				bits &= ~(~quint64(0) >> (64 - length) << start);
			}
		}
	return true;
}
bool clearAt(const Bitmap &mask, const Bitmap &board, int x, int y, Work &work)
{
	const int shift = x % 64;
	for (int row = 0; row < mask.height; ++row)
		for (int column = 0; column < mask.stride; ++column)
		{
			if (!work.step())
				return false;
			const auto bits = mask.words[row * mask.stride + column];
			if (!bits)
				continue;
			const int index = (y + row) * board.stride + x / 64 + column;
			if (board.words[index] & (bits << shift))
				return false;
			if (shift && bits >> (64 - shift) && board.words[index + 1] & (bits >> (64 - shift)))
				return false;
		}
	return true;
}
struct Chart
{
	QVector<int> faces, vertices;
	QVector<Triangle> triangles;
	double x = 0, y = 0, width = 0, height = 0;
};
struct Placement
{
	int x = 0, y = 0;
};
QString materialId(const QString &path)
{
	const auto normalized = normalizePackageVirtualPath(path, false);
	return normalized.isSafe() ? normalized.normalizedPath.toCaseFolded() : QString{};
}

bool prepare(const ModelMesh &mesh, int surfaceIndex, const QSet<int> &faces, int width, int height, QVector<Chart> *charts, Bitmap &fixed,
			 ModelUvObstacleReport *report, Work &work)
{
	const auto &surface = mesh.surfaces[surfaceIndex];
	ModelUvTopology topology;
	if (!buildModelUvTopology(surface, &topology, work.error, work.control))
		return false;
	for (const auto &island : topology.islands)
	{
		int selected = 0;
		for (int face : island.faces)
			selected += faces.contains(face);
		if (!selected)
			continue;
		if (selected != island.faces.size())
			return fail(work.error, Text::tr("Pack Around Unselected requires complete UV islands. Use Select Islands first."));
		Chart chart;
		chart.faces = island.faces;
		chart.vertices = island.vertices;
		chart.x = double(island.mins.u) * width;
		chart.y = double(island.mins.v) * height;
		chart.width = (double(island.maxs.u) - island.mins.u) * width;
		chart.height = (double(island.maxs.v) - island.mins.v) * height;
		QVector<uint32_t> indices;
		for (int face : island.faces)
		{
			if (!work.step())
				return false;
			Triangle triangle;
			const auto c = corners(surface.triangles[face]);
			for (int j = 0; j < 3; ++j)
			{
				indices.append(c[j]);
				triangle[j] = {double(surface.texCoords[c[j]].u) * width - chart.x, double(surface.texCoords[c[j]].v) * height - chart.y};
			}
			chart.triangles.append(triangle);
		}
		if (!validateModelUvMapping(surface.texCoords, indices, work.error, work.control))
			return false;
		charts->append(std::move(chart));
	}
	QSet<QString> materials;
	for (const auto &path : surface.skinPaths)
		if (!path.isEmpty())
			materials.insert(materialId(path));
	for (int s = 0; s < mesh.surfaces.size(); ++s)
	{
		const auto &other = mesh.surfaces[s];
		if (s != surfaceIndex && std::none_of(other.skinPaths.cbegin(), other.skinPaths.cend(),
											  [&](const QString &path) { return !path.isEmpty() && materials.contains(materialId(path)); }))
			continue;
		if (s != surfaceIndex)
			++report->relatedSurfaces;
		for (int face = 0; face < other.triangles.size(); ++face)
		{
			if (!work.step())
				return false;
			if (s == surfaceIndex && faces.contains(face))
				continue;
			Triangle triangle;
			const auto c = corners(other.triangles[face]);
			for (int j = 0; j < 3; ++j)
			{
				const auto uv = other.texCoords[c[j]];
				if (uv.u < 0 || uv.v < 0 || uv.u > 1 || uv.v > 1)
					return fail(work.error, Text::tr("Fixed UVs on surface %1, face %2 extend outside the atlas tile. Repeating or "
													 "multi-tile regions require separate authoring.")
												.arg(s)
												.arg(face));
				triangle[j] = {double(uv.u) * width, double(uv.v) * height};
			}
			++report->fixedFaces;
			if (!raster(triangle, fixed, work))
				return false;
		}
	}
	return work.check();
}
bool arrange(const QVector<Chart> &charts, const QVector<int> &order, const Bitmap &fixed, double scale, int padding,
			 QVector<Placement> *placements, Work &work)
{
	auto board = fixed;
	placements->fill({}, charts.size());
	for (int index : order)
	{
		const auto &chart = charts[index];
		const double w = std::ceil(chart.width * scale) + 3, h = std::ceil(chart.height * scale) + 3;
		if (w > board.width - 2 * padding || h > board.height - 2 * padding)
			return false;
		Bitmap mask{int(w), int(h)};
		for (auto triangle : chart.triangles)
		{
			for (auto &point : triangle)
			{
				point.x = point.x * scale + 1;
				point.y = point.y * scale + 1;
			}
			if (!raster(triangle, mask, work))
				return false;
		}
		if (!mask.count || mask.count > quint64(board.width) * board.height - board.count)
			return false;
		bool placed = false;
		for (int y = padding; !placed && y <= board.height - padding - mask.height; ++y)
			for (int x = padding; x <= board.width - padding - mask.width; ++x)
			{
				if (!work.step())
					return false;
				if (clearAt(mask, board, x, y, work))
				{
					(*placements)[index] = {x, y};
					if (!stamp(mask, board, x, y, padding, work))
						return false;
					placed = true;
					break;
				}
				if (work.failed)
					return false;
			}
		if (!placed)
			return false;
	}
	return work.check();
}
} // namespace

bool packModelUvAround(const ModelMesh &mesh, int surfaceIndex, const QSet<int> &faces, const ModelUvObstacleOptions &options,
					   ModelSurface *result, QString *error, const ModelWorkControl &control, ModelUvObstacleReport *outputReport)
{
	if (error)
		error->clear();
	const int width = options.atlas.resolution, height = options.atlas.height == 0 ? width : options.atlas.height,
			  padding = options.atlas.padding;
	if (!result || surfaceIndex < 0 || surfaceIndex >= mesh.surfaces.size() || faces.isEmpty() || width < 32 || width > 4096 ||
		height < 32 || height > 4096 || padding < 0 || padding > 64 || padding * 8 >= std::min(width, height) || options.workLimit == 0 ||
		options.workLimit > 250000000 || options.atlas.vertexLimit < 1 || options.atlas.vertexLimit > 65536 ||
		options.atlas.memoryLimit < 65536 || options.atlas.memoryLimit > 256 * 1024 * 1024)
		return fail(
			error,
			Text::tr("Choose selected islands, atlas dimensions of 32–4096 pixels and padding below one eighth of the smaller dimension."));
	const auto &source = mesh.surfaces[surfaceIndex];
	for (int face : faces)
		if (face < 0 || face >= source.triangles.size())
			return fail(error, Text::tr("A selected UV face no longer exists."));
	const qsizetype rasterBytes = qsizetype((width + 63) / 64) * height * sizeof(quint64) * 3;
	if (rasterBytes > options.atlas.memoryLimit)
		return fail(error, Text::tr("UV obstacle masks exceed the raster memory budget. Reduce the atlas dimensions."));
	Work work{control, error, options.workLimit};
	if (!work.check())
		return false;
	try
	{
		ModelUvObstacleReport report;
		report.rasterBytes = rasterBytes;
		Bitmap raw(width, height), fixed(width, height);
		QVector<Chart> charts;
		if (!prepare(mesh, surfaceIndex, faces, width, height, &charts, raw, &report, work) || !stamp(raw, fixed, 0, 0, padding, work))
			return false;
		raw.words.clear();
		raw.words.squeeze();
		QVector<int> order(charts.size());
		std::iota(order.begin(), order.end(), 0);
		std::stable_sort(order.begin(), order.end(),
						 [&](int a, int b) { return charts[a].width * charts[a].height > charts[b].width * charts[b].height; });
		double upper = std::numeric_limits<double>::infinity();
		for (const auto &chart : charts)
			upper = std::min({upper, (width - 2 * padding - 4) / chart.width, (height - 2 * padding - 4) / chart.height});
		double scale = options.preserveScale ? 1 : upper;
		QVector<Placement> placements, best;
		double fitted = 0, failed = 0;
		for (int attempt = 0; attempt < (options.preserveScale ? 1 : 16); ++attempt)
		{
			if (arrange(charts, order, fixed, scale, padding, &placements, work))
			{
				fitted = scale;
				best = placements;
				break;
			}
			if (work.failed)
				return false;
			failed = scale;
			scale *= .7;
		}
		if (!fitted)
			return fail(error, Text::tr("The selected islands did not fit around the fixed UV regions at this scale and padding. Reduce "
										"padding, use Fit uniformly or free atlas space."));
		if (!options.preserveScale)
			for (int attempt = 0; attempt < 4 && failed > fitted; ++attempt)
			{
				scale = (fitted + failed) * .5;
				if (arrange(charts, order, fixed, scale, padding, &placements, work))
				{
					fitted = scale;
					best = placements;
				}
				else
				{
					if (work.failed)
						return false;
					failed = scale;
				}
			}
		QVector<int> mappedFaces, sourceVertices;
		QVector<uint32_t> indices;
		QVector<ModelTexCoord> uv;
		for (int c = 0; c < charts.size(); ++c)
		{
			const auto &chart = charts[c];
			QMap<int, uint32_t> mapping;
			for (int vertex : chart.vertices)
			{
				if (!work.step())
					return false;
				const auto point = source.texCoords[vertex];
				mapping.insert(vertex, uv.size());
				sourceVertices.append(vertex);
				uv.append({float(((double(point.u) * width - chart.x) * fitted + best[c].x + 1) / width),
						   float(((double(point.v) * height - chart.y) * fitted + best[c].y + 1) / height)});
			}
			for (int face : chart.faces)
			{
				mappedFaces.append(face);
				for (int vertex : corners(source.triangles[face]))
					indices.append(mapping[vertex]);
			}
		}
		if (!validateModelUvMapping(uv, indices, error, control) || !work.check())
			return false;
		ModelSurface candidate;
		if (!applyModelUvMapping(source, mappedFaces, sourceVertices, uv, indices, options.atlas.vertexLimit, &candidate, error, control) ||
			!work.check())
			return false;
		report.charts = charts.size();
		report.scale = fitted;
		report.workUnits = work.units;
		*result = std::move(candidate);
		if (outputReport)
			*outputReport = report;
		return true;
	}
	catch (const std::bad_alloc &)
	{
		return fail(error, Text::tr("Not enough memory for UV obstacle packing. No mapping was changed."));
	}
}
} // namespace vibestudio
