#include "core/model_boundary_bridge.h"
#include "core/model_boundary_helpers.h"

#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
double distanceSquared(ModelVec3 a, ModelVec3 b)
{
	const double x = double(a.x) - b.x, y = double(a.y) - b.y, z = double(a.z) - b.z;
	return x * x + y * y + z * z;
}
bool hasArea(const ModelFrameGeometry &pose, ModelTriangle triangle)
{
	const auto a = pose.positions[triangle.a], b = pose.positions[triangle.b], c = pose.positions[triangle.c];
	const double ux = double(b.x) - a.x, uy = double(b.y) - a.y, uz = double(b.z) - a.z,
		vx = double(c.x) - a.x, vy = double(c.y) - a.y, vz = double(c.z) - a.z;
	return std::hypot(uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx) >= 1e-10;
}
struct Strip
{
	QVector<ModelTriangle> triangles;
	double cost = std::numeric_limits<double>::infinity();
};

Strip triangulate(const QVector<int> &a, const QVector<int> &b, const ModelFrameGeometry &pose,
	const QSet<ModelEdge> &existing, bool startWithA, ModelBoundaryWork &work)
{
	const int n = a.size(), m = b.size(), width = m + 1;
	QVector<double> costs((n + 1) * width, std::numeric_limits<double>::infinity());
	QVector<char> steps(costs.size(), 0);
	const auto at = [width](int i, int j) { return i * width + j; };
	// Each lattice point is one edge across the gap. A step consumes exactly
	// one boundary edge and adds its triangle. The two seam orientations keep
	// the initial cross edge unique until closure; allowing an early full lap
	// would reuse cross edges and create nonmanifold fans.
	for (int i = 0; i <= n; ++i)
	{
		for (int j = 0; j <= m; ++j)
		{
			if (!work.step())
				return {};
			if ((i == n && j == 0) || (i == 0 && j == m) ||
				(startWithA && ((i == 0 && j > 0) || (j == m && i < n))) ||
				(!startWithA && ((j == 0 && i > 0) || (i == n && j < m))))
				continue;
			const int ai = a[i % n], bj = b[j % m], cell = at(i, j);
			if (existing.contains(modelEdge(ai, bj)))
				continue;
			if (i == 0 && j == 0)
			{
				costs[cell] = 0;
				continue;
			}
			const double cost = distanceSquared(pose.positions[ai], pose.positions[bj]);
			if (i > 0 && std::isfinite(costs[at(i - 1, j)]) && hasArea(pose, {a[i - 1], ai, bj}))
			{
				costs[cell] = costs[at(i - 1, j)] + cost;
				steps[cell] = 'a';
			}
			if (j > 0 && std::isfinite(costs[at(i, j - 1)]) && costs[at(i, j - 1)] + cost < costs[cell] &&
				hasArea(pose, {ai, bj, b[j - 1]}))
			{
				costs[cell] = costs[at(i, j - 1)] + cost;
				steps[cell] = 'b';
			}
		}
	}
	Strip result;
	result.cost = costs[at(n, m)];
	if (!std::isfinite(result.cost))
		return result;
	int i = n, j = m;
	while (i || j)
	{
		if (!work.step())
			return {};
		if (steps[at(i, j)] == 'a')
		{
			result.triangles << ModelTriangle{a[i - 1], a[i % n], b[j % m]};
			--i;
		}
		else
		{
			result.triangles << ModelTriangle{a[i % n], b[j % m], b[j - 1]};
			--j;
		}
	}
	std::reverse(result.triangles.begin(), result.triangles.end());
	return result;
}
} // namespace

bool bridgeModelBoundaryLoops(const ModelSurface &source, const QSet<ModelEdge> &selected, int referenceFrame, int twist,
	int triangleLimit, ModelSurface *result, ModelBoundaryBridgeReport *report, QString *error, const ModelWorkControl &control)
{
	if (error)
		error->clear();
	ModelBoundaryWork work{{control, ModelWorkPhase::Editing, error}, error};
	if (!work.progress.check())
		return false;
	if (!result || referenceFrame < 0 || referenceFrame >= source.frames.size() || twist < -1023 || twist > 1023 ||
		triangleLimit < source.triangles.size())
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
			"Choose an existing reference frame, a bridge twist from -1023 to 1023 and enough triangle capacity."));
	ModelBoundaryLoops boundary;
	if (!findModelBoundaryLoops(source, selected, &boundary, work))
		return false;
	if (boundary.loops.size() != 2)
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
			"Select boundary edges from exactly two closed loops on one surface. Join surfaces first when needed."));
	auto a = boundary.loops[0], b = boundary.loops[1];
	const QSet<int> firstVertices(a.cbegin(), a.cend());
	for (int vertex : b)
		if (firstVertices.contains(vertex))
			return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Bridge boundaries must not share vertices."));
	if (qint64(source.triangles.size()) + a.size() + b.size() > triangleLimit)
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge", "Bridging these loops would exceed the document's triangle limit."));
	// Opposite strip sides follow opposite directions. Preserve the canonical
	// first vertex while reversing the second loop, then find a stable seam.
	std::reverse(b.begin() + 1, b.end());
	const auto &pose = source.frames[referenceFrame];
	double closest = std::numeric_limits<double>::infinity();
	int startA = 0, startB = 0;
	for (int i = 0; i < a.size(); ++i)
	{
		for (int j = 0; j < b.size(); ++j)
		{
			if (!work.step())
				return false;
			const double distance = distanceSquared(pose.positions[a[i]], pose.positions[b[j]]);
			if (!boundary.existing.contains(modelEdge(a[i], b[j])) && distance < closest)
			{
				closest = distance;
				startA = i;
				startB = j;
			}
		}
	}
	const int secondStart = (startB + twist % b.size() + b.size()) % b.size();
	std::rotate(a.begin(), a.begin() + startA, a.end());
	std::rotate(b.begin(), b.begin() + secondStart, b.end());
	std::array<Strip, 2> strips;
	for (int orientation = 0; orientation < 2; ++orientation)
	{
		strips[orientation] = triangulate(a, b, pose, boundary.existing, orientation == 0, work);
		if (work.stopped)
			return false;
	}
	if (strips[1].cost < strips[0].cost)
		std::swap(strips[0], strips[1]);
	const Strip *accepted = nullptr;
	for (const auto &strip : strips)
	{
		if (strip.triangles.isEmpty())
			continue;
		if (validateModelBoundaryFaces(source, strip.triangles, work))
		{
			accepted = &strip;
			break;
		}
		if (work.stopped || !work.progress.check())
			return false;
	}
	if (!accepted)
	{
		if (error && !error->isEmpty())
			return false;
		return fail(error, QCoreApplication::translate("ModelBoundaryBridge",
			"These boundaries cannot form a valid bridge with this alignment. Adjust the twist, reference pose or boundary geometry."));
	}
	ModelSurface candidate = source;
	ModelBoundaryBridgeReport receipt;
	receipt.firstAnchor = a[0];
	receipt.secondAnchor = b[0];
	receipt.boundaryEdges = std::move(boundary.edges);
	for (auto face : accepted->triangles)
	{
		if (!work.step())
			return false;
		receipt.faces << int(candidate.triangles.size());
		candidate.triangles << face;
	}
	if (!work.progress.check())
		return false;
	*result = std::move(candidate);
	if (report)
		*report = std::move(receipt);
	if (error)
		error->clear();
	return true;
}
} // namespace vibestudio
