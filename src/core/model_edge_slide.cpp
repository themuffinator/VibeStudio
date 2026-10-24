#include "core/model_edge_slide.h"
#include "core/model_mesh_tools_p.h"

#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct SlideText
{
	Q_DECLARE_TR_FUNCTIONS(ModelEdgeSlide)
};
using namespace model_tools;
} // namespace
bool planModelEdgeSlide(const ModelSurface &surface, const QSet<ModelEdge> &edges, int frame, double maxAngle, ModelEdgeSlidePlan *result,
						QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!result || !work.check())
		return false;
	if (edges.isEmpty())
		return fail(error, SlideText::tr("Select edge chains or loops to slide."));
	ModelQuadTopology topology;
	if (!buildModelQuadTopology(surface, frame, maxAngle, &topology, error, control))
		return false;
	const auto &geometry = topology.geometry;
	const auto indexed = surfaceEdgeSet(surface);
	QSet<ModelEdge> chosen;
	QHash<int, QVector<int>> neighbours;
	for (auto edge : edges)
	{
		if (!work.step())
			return false;
		if (!indexed.contains(modelEdge(edge.first, edge.second)))
			return fail(error, SlideText::tr("Every slide selector must name an existing edge."));
		const auto geometric = geometry.geometricEdge(edge.first, edge.second);
		const auto uses = topology.sideUses.value(geometric);
		if (geometric.first == geometric.second || uses.isEmpty() || uses.size() > 2 || topology.diagonals.contains(geometric))
			return fail(error, SlideText::tr("Slide edges must be unbranched sides of quads, not paired diagonals or nonmanifold edges."));
		for (auto use : uses)
			if (!topology.elements[use.first].quad())
				return fail(
					error,
					SlideText::tr("Every selected edge needs quad faces on its available sides. Adjust the pairing angle or selection."));
		if (chosen.contains(geometric))
			continue;
		chosen.insert(geometric);
		neighbours[geometric.first].append(geometric.second);
		neighbours[geometric.second].append(geometric.first);
	}
	for (auto &adjacent : neighbours)
	{
		if (adjacent.size() > 2)
			return fail(error, SlideText::tr("Edge sliding needs separate chains or loops without selected branches."));
		std::sort(adjacent.begin(), adjacent.end());
	}
	QVector<ModelEdge> ordered(chosen.cbegin(), chosen.cend());
	std::sort(ordered.begin(), ordered.end());
	QSet<int> visited;
	QHash<int, std::array<int, 2>> rails;
	ModelEdgeSlidePlan plan;
	for (auto seed : ordered)
	{
		if (visited.contains(seed.first))
			continue;
		QVector<int> component, pending{seed.first};
		visited.insert(seed.first);
		while (!pending.isEmpty())
		{
			if (!work.step())
				return false;
			const int v = pending.takeLast();
			component.append(v);
			for (int next : neighbours.value(v))
				if (!visited.contains(next))
				{
					visited.insert(next);
					pending.append(next);
				}
		}
		std::sort(component.begin(), component.end());
		int start = component.first();
		for (int v : component)
			if (neighbours.value(v).size() == 1)
			{
				start = v;
				break;
			}
		int previous = -1, current = start;
		bool first = true, flip = false;
		do
		{
			if (!work.step())
				return false;
			int next = -1;
			for (int v : neighbours.value(current))
				if (v != previous)
				{
					next = v;
					break;
				}
			if (next < 0)
				break;
			const auto uses = topology.sideUses.value(modelEdge(current, next));
			std::array<bool, 2> seen{false, false};
			for (auto [element, side] : uses)
			{
				const auto &c = topology.elements[element].corners;
				const bool forward = geometry.group(c[side]) == current;
				if (first && uses.size() == 1)
					flip = !forward;
				const int direction = int(!forward) ^ int(flip);
				if (seen[direction])
					return fail(error, SlideText::tr("Neighbouring slide faces must have consistent winding."));
				seen[direction] = true;
				const int near = geometry.group(c[(side + 3) % 4]), far = geometry.group(c[(side + 2) % 4]);
				for (const auto &[v, rail] : {qMakePair(current, forward ? near : far), qMakePair(next, forward ? far : near)})
				{
					if (!rails.contains(v))
						rails.insert(v, {-1, -1});
					int &slot = rails[v][direction];
					if (chosen.contains(modelEdge(v, rail)) || (slot >= 0 && slot != rail))
						return fail(error,
									SlideText::tr(
										"The selected chain turns across a face or meets ambiguous rails. Select a continuous edge loop."));
					slot = rail;
				}
			}
			first = false;
			previous = current;
			current = next;
		} while (current != start);
		++plan.chains;
	}
	auto vertices = rails.keys();
	std::sort(vertices.begin(), vertices.end());
	for (int v : vertices)
		plan.vertices.append({v, rails.value(v), geometry.copies[v]});
	if (!work.check())
		return false;
	*result = std::move(plan);
	return true;
}

namespace model_tools
{
bool slideEdges(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = mesh->surfaces[selection->surface];
	ModelEdgeSlidePlan plan;
	if (!planModelEdgeSlide(surface, selection->edges, edit.tool.referenceFrame, edit.tool.maxAngle, &plan, error, control))
		return false;
	const double amount = edit.tool.slideByDistance ? edit.tool.offset : edit.tool.slide;
	if (amount == 0)
		return work.check();
	const int direction = amount < 0 ? 1 : 0;
	const auto original = surface;
	QSet<int> moved;
	for (const auto &entry : plan.vertices)
	{
		if (!work.step())
			return false;
		const int rail = entry.rails[direction];
		if (rail < 0)
			return fail(error, SlideText::tr("The chain has no rail on the chosen side. Reverse the slide direction."));
		const double railLength =
			length(position(original, edit.tool.referenceFrame, rail) - position(original, edit.tool.referenceFrame, entry.vertex));
		if (railLength <= 1e-12)
			return fail(error, SlideText::tr("A slide rail has zero length in the reference pose."));
		const double fraction = edit.tool.slideByDistance ? std::abs(amount) / railLength : std::abs(amount);
		if (!std::isfinite(fraction) || fraction >= 1)
			return fail(error, SlideText::tr("The slide reaches or passes a neighbouring rail vertex. Reduce its amount."));
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			if (!work.step())
				return false;
			if (edit.frame >= 0 && frame != edit.frame)
				continue;
			const auto a = position(original, frame, entry.vertex), b = position(original, frame, rail);
			const auto p = vec(a + (b - a) * fraction);
			for (int copy : entry.copies)
			{
				if (!work.step())
					return false;
				surface.frames[frame].positions[copy] = p;
				moved.insert(copy);
			}
		}
	}
	QSet<int> normals = moved;
	for (const auto &t : original.triangles)
	{
		if (!work.step())
			return false;
		if (!moved.contains(t.a) && !moved.contains(t.b) && !moved.contains(t.c))
			continue;
		normals << t.a << t.b << t.c;
		for (int frame = 0; frame < surface.frames.size(); ++frame)
		{
			if (!work.step())
				return false;
			if (edit.frame >= 0 && frame != edit.frame)
				continue;
			const auto &p = surface.frames[frame].positions;
			if (collapsedTriangle(p[t.a], p[t.b], p[t.c]) || dot(faceCross(original, frame, t), faceCross(surface, frame, t)) <= 0)
				return fail(error, SlideText::tr("The slide would collapse or reverse a face in sample %1. Reduce its amount.").arg(frame));
		}
	}
	if (!refreshNormals(&surface, normals, work, edit.frame))
		return false;
	selection->vertices.clear();
	selection->faces.clear();
	return work.check();
}
} // namespace model_tools
} // namespace vibestudio
