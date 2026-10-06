#include "core/model_boundary_fill.h"
#include "core/model_boundary_helpers.h"
#include "core/model_geometry_helpers.h"
#include "core/model_triangle_contact.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

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
using namespace model_geometry;
bool onSegment(P2 a, P2 b, P2 p, double epsilon, double distance)
{
	return std::abs(orient(a, b, p)) <= epsilon && p.x >= std::min(a.x, b.x) - distance && p.x <= std::max(a.x, b.x) + distance &&
		   p.y >= std::min(a.y, b.y) - distance && p.y <= std::max(a.y, b.y) + distance;
}
bool crosses(P2 a, P2 b, P2 c, P2 d, double epsilon, double distance)
{
	const double abC = orient(a, b, c), abD = orient(a, b, d), cdA = orient(c, d, a), cdB = orient(c, d, b);
	return (((abC > epsilon && abD < -epsilon) || (abC < -epsilon && abD > epsilon)) &&
			((cdA > epsilon && cdB < -epsilon) || (cdA < -epsilon && cdB > epsilon))) ||
		   onSegment(a, b, c, epsilon, distance) || onSegment(a, b, d, epsilon, distance) || onSegment(c, d, a, epsilon, distance) ||
		   onSegment(c, d, b, epsilon, distance);
}
struct Projection
{
	QVector<P2> points;
	double signedArea = 0, epsilon = 0, distance = 0;
};
std::array<Projection, 3> projections(const QVector<int> &loop, const ModelFrameGeometry &frame)
{
	std::array<Projection, 3> result;
	const auto origin = point(frame.positions[loop[0]]);
	for (int axis = 0; axis < 3; ++axis)
	{
		auto &p = result[axis];
		double scale = 0;
		for (int vertex : loop)
		{
			const auto value = project(point(frame.positions[vertex]) - origin, axis);
			p.points << value;
			scale = std::max({scale, std::abs(value.x), std::abs(value.y)});
		}
		p.signedArea = area(p.points);
		p.epsilon = scale * scale * 1e-12;
		p.distance = scale * 1e-10;
	}
	std::stable_sort(result.begin(), result.end(),
					 [](const auto &a, const auto &b) { return std::abs(a.signedArea) > std::abs(b.signedArea); });
	return result;
}
bool simple(const Projection &p, ModelBoundaryWork &work)
{
	if (std::abs(p.signedArea) <= p.epsilon)
	{
		return false;
	}
	const int size = p.points.size();
	for (int i = 0; i < size; ++i)
	{
		const auto a = p.points[i], b = p.points[(i + 1) % size];
		if (std::hypot(a.x - b.x, a.y - b.y) <= p.distance)
		{
			return false;
		}
		for (int j = i + 1; j < size; ++j)
		{
			if (!work.step())
			{
				return false;
			}
			if (j != i + 1 && !(i == 0 && j == size - 1) && crosses(a, b, p.points[j], p.points[(j + 1) % size], p.epsilon, p.distance))
			{
				return false;
			}
		}
	}
	return true;
}
bool triangulate(const QVector<int> &loop, const Projection &p, const QSet<ModelEdge> &existing, QVector<ModelTriangle> *result,
				 ModelBoundaryWork &work)
{
	const double sign = p.signedArea > 0 ? 1 : -1;
	QVector<int> remaining(loop.size());
	std::iota(remaining.begin(), remaining.end(), 0);
	QSet<ModelEdge> boundary;
	for (int i = 0; i < loop.size(); ++i)
	{
		boundary.insert(modelEdge(loop[i], loop[(i + 1) % loop.size()]));
	}
	QVector<ModelTriangle> triangles;
	while (remaining.size() > 3)
	{
		bool found = false;
		for (int i = 0; i < remaining.size(); ++i)
		{
			if (!work.step())
			{
				return false;
			}
			const int a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i],
					  c = remaining[(i + 1) % remaining.size()];
			if (sign * orient(p.points[a], p.points[b], p.points[c]) <= p.epsilon)
			{
				continue;
			}
			const auto diagonal = modelEdge(loop[a], loop[c]);
			if (existing.contains(diagonal) && !boundary.contains(diagonal))
			{
				continue;
			}
			bool blocked = false;
			for (int v : remaining)
			{
				if (!work.step())
				{
					return false;
				}
				if (v != a && v != b && v != c && sign * orient(p.points[a], p.points[b], p.points[v]) >= -p.epsilon &&
					sign * orient(p.points[b], p.points[c], p.points[v]) >= -p.epsilon &&
					sign * orient(p.points[c], p.points[a], p.points[v]) >= -p.epsilon)
				{
					blocked = true;
					break;
				}
			}
			if (!blocked)
			{
				triangles << ModelTriangle{loop[a], loop[b], loop[c]};
				remaining.removeAt(i);
				found = true;
				break;
			}
		}
		if (!found)
		{
			return false;
		}
	}
	if (sign * orient(p.points[remaining[0]], p.points[remaining[1]], p.points[remaining[2]]) <= p.epsilon)
	{
		return false;
	}
	triangles << ModelTriangle{loop[remaining[0]], loop[remaining[1]], loop[remaining[2]]};
	*result = std::move(triangles);
	return true;
}
bool supportsCap(const QVector<int> &loop, const ModelFrameGeometry &frame, const QVector<ModelTriangle> &triangles,
				 ModelBoundaryWork &work)
{
	for (const auto &t : triangles)
	{
		if (!work.step())
		{
			return false;
		}
		const auto a = point(frame.positions[t.a]), b = point(frame.positions[t.b]), c = point(frame.positions[t.c]);
		// Match the editable document's minimum geometric face area before a
		// projection-relative tolerance can accept an extremely small cap.
		if (length(cross(b - a, c - a)) < 1e-10)
		{
			return false;
		}
	}
	QHash<int, int> indices;
	for (int i = 0; i < loop.size(); ++i)
	{
		indices.insert(loop[i], i);
	}
	for (const auto &p : projections(loop, frame))
	{
		if (!simple(p, work))
		{
			if (work.stopped)
			{
				return false;
			}
			continue;
		}
		bool valid = true;
		const double sign = p.signedArea > 0 ? 1 : -1;
		// A simple boundary and strictly positive triangles in this triangulated
		// disk give a non-overlapping projected cap. Requiring one such projection
		// is conservative for highly folded 3D loops; no pose is flattened.
		for (const auto &t : triangles)
		{
			if (!work.step())
			{
				return false;
			}
			if (sign * orient(p.points[indices[t.a]], p.points[indices[t.b]], p.points[indices[t.c]]) <= p.epsilon)
			{
				valid = false;
				break;
			}
		}
		if (valid)
		{
			return true;
		}
	}
	return false;
}
using Triangle = std::array<P3, 3>;
Triangle triangle(const ModelFrameGeometry &frame, const ModelTriangle &t)
{
	return {point(frame.positions[t.a]), point(frame.positions[t.b]), point(frame.positions[t.c])};
}
struct EdgeUse
{
	int count = 0;
	int from = -1, to = -1;
};
} // namespace

bool ModelBoundaryWork::step()
{
	if (!stopped && ++checks > modelBoundaryMaxChecks)
	{
		fail(error, QCoreApplication::translate(
						"VibeStudioModelBoundaryFill",
						"The boundary operation exceeded its geometry-check limit. Reduce the loop, surface or animation complexity."));
		stopped = true;
	}
	if (!stopped && !progress.step())
	{
		stopped = true;
	}
	return !stopped;
}

bool findModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected, ModelBoundaryLoops *result,
							ModelBoundaryWork &work)
{
	auto *error = work.error;
	if (!result || selected.isEmpty() || source.frames.isEmpty() || source.vertexCount < 3 || source.texCoords.size() != source.vertexCount)
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill", "Select boundary edges on a valid surface."));
	}
	for (const auto &frame : source.frames)
	{
		if (frame.positions.size() != source.vertexCount || frame.normals.size() != source.vertexCount)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill",
														   "Every pose must contain all boundary positions and normals."));
		}
		for (auto p : frame.positions)
		{
			if (!work.step())
			{
				return false;
			}
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill", "Boundary positions must be finite."));
			}
		}
	}
	QHash<ModelEdge, EdgeUse> edges;
	QSet<ModelEdge> existing;
	for (const auto &t : source.triangles)
	{
		if (!work.step())
		{
			return false;
		}
		if (t.a < 0 || t.b < 0 || t.c < 0 || t.a >= source.vertexCount || t.b >= source.vertexCount || t.c >= source.vertexCount ||
			t.a == t.b || t.b == t.c || t.c == t.a)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill", "The surface contains an invalid triangle."));
		}
		for (auto directed : {ModelEdge{t.a, t.b}, ModelEdge{t.b, t.c}, ModelEdge{t.c, t.a}})
		{
			const auto key = modelEdge(directed.first, directed.second);
			auto &use = edges[key];
			++use.count;
			use.from = directed.first;
			use.to = directed.second;
			existing.insert(key);
		}
	}
	QHash<int, QVector<int>> outgoing, incoming;
	for (auto it = edges.cbegin(); it != edges.cend(); ++it)
	{
		if (!work.step())
		{
			return false;
		}
		if (it->count == 1)
		{
			// Reverse the existing face boundary so the cap closes its winding.
			outgoing[it->to] << it->from;
			incoming[it->from] << it->to;
		}
	}
	auto seeds = selected.values();
	std::sort(seeds.begin(), seeds.end());
	QSet<ModelEdge> visited;
	QVector<QVector<int>> loops;
	for (auto seed : seeds)
	{
		if (!work.step())
		{
			return false;
		}
		const auto use = edges.constFind(seed);
		if (use == edges.cend() || use->count != 1)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill",
														   "Edge %1:%2 is not a boundary edge. Select edges used by exactly one face.")
								   .arg(seed.first)
								   .arg(seed.second));
		}
		if (visited.contains(seed))
		{
			continue;
		}
		QVector<int> loop;
		const int start = use->to;
		int vertex = start;
		do
		{
			if (!work.step())
			{
				return false;
			}
			if (outgoing.value(vertex).size() != 1 || incoming.value(vertex).size() != 1)
			{
				return fail(error,
							QCoreApplication::translate(
								"VibeStudioModelBoundaryFill",
								"The selected boundary branches or has inconsistent winding at vertex %1. Repair its topology first.")
								.arg(vertex));
			}
			const int next = outgoing.value(vertex)[0];
			const auto edge = modelEdge(vertex, next);
			if (visited.contains(edge))
			{
				return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill",
															   "The selected edges do not form separate closed boundary loops."));
			}
			visited.insert(edge);
			loop << vertex;
			if (loop.size() > modelBoundaryMaxLoopVertices)
			{
				return fail(error, QCoreApplication::translate(
									   "VibeStudioModelBoundaryFill",
									   "A boundary loop can contain at most %1 vertices. Divide this boundary before editing it.")
									   .arg(modelBoundaryMaxLoopVertices));
			}
			vertex = next;
		} while (vertex != start);
		if (loop.size() < 3)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill",
														   "A boundary loop needs at least three distinct vertices."));
		}
		std::rotate(loop.begin(), std::min_element(loop.begin(), loop.end()), loop.end());
		loops << std::move(loop);
	}
	std::sort(loops.begin(), loops.end(), [](const auto &a, const auto &b) { return a[0] < b[0]; });
	ModelBoundaryLoops candidate;
	candidate.loops = std::move(loops);
	candidate.existing = std::move(existing);
	candidate.edges = visited.values();
	std::sort(candidate.edges.begin(), candidate.edges.end());
	*result = std::move(candidate);
	return true;
}

bool validateModelBoundaryFaces(const ModelSurface &source, const QVector<ModelTriangle> &added, ModelBoundaryWork &work)
{
	auto *error = work.error;
	for (int frame = 0; frame < source.frames.size(); ++frame)
	{
		const auto &pose = source.frames[frame];
		for (int face = 0; face < added.size(); ++face)
		{
			const auto cap = triangle(pose, added[face]);
			if (length(cross(cap[1] - cap[0], cap[2] - cap[0])) < 1e-10)
			{
				return fail(work.error,
							QCoreApplication::translate(
								"VibeStudioModelBoundaryFill",
								"Proposed face %1 collapses in frame %2. Adjust the boundaries or choose another reference pose.")
								.arg(source.triangles.size() + face)
								.arg(frame));
			}
			for (int other = 0; other < source.triangles.size() + face; ++other)
			{
				if (!work.step())
				{
					return false;
				}
				const auto &t = other < source.triangles.size() ? source.triangles[other] : added[other - source.triangles.size()];
				const auto addedFace = added[face];
				if (modelTriangleContact({pose.positions[addedFace.a], pose.positions[addedFace.b], pose.positions[addedFace.c]},
										 {pose.positions[t.a], pose.positions[t.b], pose.positions[t.c]}) != ModelTriangleContact::None)
				{
					return fail(
						error,
						QCoreApplication::translate(
							"VibeStudioModelBoundaryFill",
							"Proposed face %1 intersects face %2 in frame %3. Adjust the selected boundaries or obstructing geometry.")
							.arg(source.triangles.size() + face)
							.arg(other)
							.arg(frame));
				}
			}
		}
	}
	return work.progress.check();
}

bool fillModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected, int referenceFrame, int triangleLimit,
							ModelSurface *result, ModelBoundaryFillReport *report, QString *error, const ModelWorkControl &control)
{
	ModelBoundaryWork work{{control, ModelWorkPhase::Editing, error}, error};
	if (!work.progress.check())
	{
		return false;
	}
	if (!result || selected.isEmpty() || referenceFrame < 0 || referenceFrame >= source.frames.size() || source.vertexCount < 3 ||
		source.texCoords.size() != source.vertexCount || triangleLimit < source.triangles.size())
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill",
													   "Select boundary edges on a valid surface and choose an existing reference frame."));
	}
	ModelBoundaryLoops boundary;
	if (!findModelBoundaryLoops(source, selected, &boundary, work))
	{
		return false;
	}
	const auto &loops = boundary.loops;
	const auto &existing = boundary.existing;
	qint64 newFaces = 0;
	for (const auto &loop : loops)
	{
		newFaces += loop.size() - 2;
		if (source.triangles.size() + newFaces > triangleLimit)
		{
			return fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill",
														   "Filling these loops would exceed the document's triangle limit."));
		}
	}
	QVector<ModelTriangle> caps;
	for (const auto &loop : loops)
	{
		QVector<ModelTriangle> triangles;
		for (const auto &p : projections(loop, source.frames[referenceFrame]))
		{
			if (simple(p, work) && triangulate(loop, p, existing, &triangles, work))
			{
				break;
			}
			if (work.stopped)
			{
				return false;
			}
		}
		if (triangles.isEmpty())
		{
			return fail(error,
						QCoreApplication::translate("VibeStudioModelBoundaryFill",
													"Boundary at vertex %1 cannot be triangulated in frame %2 without crossings, collapsed "
													"faces or an existing interior edge. Adjust the loop or reference frame.")
							.arg(loop[0])
							.arg(referenceFrame));
		}
		for (int frame = 0; frame < source.frames.size(); ++frame)
		{
			if (!supportsCap(loop, source.frames[frame], triangles, work))
			{
				return work.stopped
						   ? false
						   : fail(error, QCoreApplication::translate("VibeStudioModelBoundaryFill",
																	 "The proposed cap at vertex %1 folds, crosses or collapses in frame "
																	 "%2. Adjust that pose or choose another reference frame.")
											 .arg(loop[0])
											 .arg(frame));
			}
		}
		caps += triangles;
	}
	if (!validateModelBoundaryFaces(source, caps, work))
	{
		return false;
	}
	ModelSurface candidate = source;
	ModelBoundaryFillReport receipt;
	receipt.loopCount = loops.size();
	receipt.boundaryEdges = boundary.edges;
	for (const auto &cap : caps)
	{
		if (!work.step())
		{
			return false;
		}
		receipt.faces << int(candidate.triangles.size());
		candidate.triangles << cap;
	}
	if (!work.progress.check())
	{
		return false;
	}
	*result = std::move(candidate);
	if (report)
	{
		*report = std::move(receipt);
	}
	return true;
}
} // namespace vibestudio
