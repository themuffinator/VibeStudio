#include "core/model_intersections.h"
#include "core/model_document.h"

#include <QCoreApplication>
#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(VibeStudioModelIntersections)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
struct Bounds
{
	std::array<double, 3> lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
	void add(const Bounds &other)
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			lo[axis] = std::min(lo[axis], other.lo[axis]);
			hi[axis] = std::max(hi[axis], other.hi[axis]);
		}
	}
	void add(ModelVec3 point)
	{
		const std::array<double, 3> coordinates{point.x, point.y, point.z};
		add({coordinates, coordinates});
	}
	bool touches(const Bounds &other) const
	{
		for (int axis = 0; axis < 3; ++axis)
			if (hi[axis] < other.lo[axis] || other.hi[axis] < lo[axis])
				return false;
		return true;
	}
};
struct Face
{
	int surface, face;
	std::array<ModelVec3, 3> vertices;
	Bounds bounds;
};
struct Node
{
	Bounds bounds;
	int first, count, left = -1, right = -1;
};
struct Tree
{
	const QVector<Face> &faces;
	ModelWorkProgress &work;
	QVector<int> order;
	QVector<Node> nodes;
	bool stopped = false;
	int build(int first, int count)
	{
		Node node{{}, first, count};
		Bounds centres;
		for (int at = first; at < first + count; ++at)
		{
			if (!work.step())
			{
				stopped = true;
				return -1;
			}
			const auto &bounds = faces[order[at]].bounds;
			node.bounds.add(bounds);
			std::array<double, 3> centre;
			for (int axis = 0; axis < 3; ++axis)
				centre[axis] = (bounds.lo[axis] + bounds.hi[axis]) * .5;
			centres.add({centre, centre});
		}
		const int index = int(nodes.size());
		nodes.append(node);
		if (count <= 8)
			return index;
		int axis = 0;
		for (int candidate = 1; candidate < 3; ++candidate)
			if (centres.hi[candidate] - centres.lo[candidate] > centres.hi[axis] - centres.lo[axis])
				axis = candidate;
		const int middle = first + count / 2;
		std::nth_element(order.begin() + first, order.begin() + middle, order.begin() + first + count, [&](int a, int b) {
			const auto &x = faces[a].bounds, &y = faces[b].bounds;
			const double cx = x.lo[axis] + x.hi[axis], cy = y.lo[axis] + y.hi[axis];
			return cx == cy ? a < b : cx < cy;
		});
		const int left = build(first, middle - first);
		if (stopped)
			return -1;
		const int right = build(middle, first + count - middle);
		if (stopped)
			return -1;
		nodes[index].left = left;
		nodes[index].right = right;
		return index;
	}
};
} // namespace

QString modelIntersectionKindId(ModelTriangleContact kind)
{
	switch (kind)
	{
	case ModelTriangleContact::Crossing:
		return QStringLiteral("crossing");
	case ModelTriangleContact::CoplanarOverlap:
		return QStringLiteral("coplanar-overlap");
	case ModelTriangleContact::Degenerate:
		return QStringLiteral("degenerate");
	default:
		return QStringLiteral("none");
	}
}

bool inspectModelIntersections(const ModelMesh &mesh, const ModelIntersectionOptions &options, ModelIntersectionReport *result,
							   QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check())
		return false;
	if (!result || options.frame < -1 || options.frame >= mesh.frames.size() || options.surface < -1 ||
		options.surface >= mesh.surfaces.size() || options.pairLimit < 1 || options.pairLimit > modelIntersectionMaxPairs ||
		options.nodeLimit < 1 || options.nodeLimit > modelIntersectionMaxNodeChecks || options.findingLimit < 1 ||
		options.findingLimit > modelIntersectionMaxFindings)
		return fail(error, Text::tr("Choose existing surfaces and poses within the intersection scan limits."));
	qint64 triangles = 0;
	for (const auto &surface : mesh.surfaces)
		triangles += surface.triangles.size();
	const int frames = options.frame < 0 ? int(mesh.frames.size()) : 1;
	if (triangles > modelDocumentMaxTriangles || triangles * frames > modelIntersectionMaxFacePoses)
		return fail(error, Text::tr("The intersection scan exceeds %1 face poses. Scan one pose or reduce the model complexity.")
							   .arg(modelIntersectionMaxFacePoses));
	const auto problems = validateEditableModel(mesh, control);
	if (!problems.isEmpty())
		return fail(error, problems.join(QLatin1Char('\n')));
	ModelIntersectionReport report;
	const int firstFrame = options.frame < 0 ? 0 : options.frame;
	for (int frame = firstFrame; frame < firstFrame + frames; ++frame)
	{
		if (!work.check())
			return false;
		QVector<Face> faces;
		faces.reserve(int(triangles));
		for (int surface = 0; surface < mesh.surfaces.size(); ++surface)
		{
			const auto &source = mesh.surfaces[surface];
			const auto &positions = source.frames[frame].positions;
			for (int face = 0; face < source.triangles.size(); ++face)
			{
				if (!work.step())
					return false;
				const auto t = source.triangles[face];
				Face item{surface, face, {positions[t.a], positions[t.b], positions[t.c]}, {}};
				for (auto vertex : item.vertices)
					item.bounds.add(vertex);
				faces.append(item);
			}
		}
		Tree tree{faces, work, {}, {}, false};
		tree.order.resize(faces.size());
		std::iota(tree.order.begin(), tree.order.end(), 0);
		tree.nodes.reserve(faces.size() / 2 + 1);
		if (tree.build(0, int(faces.size())) < 0)
			return false;
		QVector<int> stack;
		stack.reserve(32);
		for (int first = 0; first < faces.size(); ++first)
		{
			const auto &a = faces[first];
			if (options.surface >= 0 && a.surface != options.surface)
				continue;
			stack = {0};
			while (!stack.isEmpty())
			{
				if (!work.step())
					return false;
				if (++report.nodeChecks > options.nodeLimit)
					return fail(error, Text::tr("The intersection scan exceeded its spatial-search limit. Scan one pose or one surface."));
				const auto &node = tree.nodes[stack.takeLast()];
				if (!node.bounds.touches(a.bounds))
					continue;
				if (node.left >= 0)
				{
					stack.append(node.right);
					stack.append(node.left);
					continue;
				}
				for (int at = node.first; at < node.first + node.count; ++at)
				{
					if (!work.step())
						return false;
					const int second = tree.order[at];
					if (second == first || (options.surface < 0 && second < first))
						continue;
					const auto &b = faces[second];
					if ((options.surface >= 0 && b.surface == options.surface && second < first) || !a.bounds.touches(b.bounds))
						continue;
					if (++report.candidatePairs > options.pairLimit)
						return fail(error, Text::tr("The intersection scan exceeded its face-pair limit. Scan one pose or one surface."));
					const auto kind = modelTriangleContact(a.vertices, b.vertices);
					if (kind == ModelTriangleContact::Degenerate)
						return fail(error,
									Text::tr("The intersection scan encountered degenerate geometry. Repair the source before scanning."));
					if (kind == ModelTriangleContact::None)
						continue;
					if (report.findings.size() >= options.findingLimit)
						return fail(
							error,
							Text::tr(
								"The intersection scan exceeded %1 findings. Scan one pose or one surface; no partial report was adopted.")
								.arg(options.findingLimit));
					const auto &low = first < second ? a : b, &high = first < second ? b : a;
					report.findings.append({frame, low.surface, low.face, high.surface, high.face, kind});
				}
			}
		}
		++report.framesScanned;
		report.facePoses += faces.size();
		if (!modelWorkCheckpoint(control, ModelWorkPhase::Validating, report.framesScanned, frames, error))
			return false;
	}
	std::sort(report.findings.begin(), report.findings.end(), [](const auto &a, const auto &b) {
		return std::tie(a.frame, a.firstSurface, a.firstFace, a.secondSurface, a.secondFace) <
			   std::tie(b.frame, b.firstSurface, b.firstFace, b.secondSurface, b.secondFace);
	});
	if (!work.check())
		return false;
	*result = std::move(report);
	return true;
}
} // namespace vibestudio
