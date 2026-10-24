#include "core/model_mesh_tools_p.h"

#include <QMap>
#include <algorithm>
#include <deque>

namespace vibestudio::model_tools
{
namespace
{
struct KnifeText
{
	Q_DECLARE_TR_FUNCTIONS(ModelKnife)
};
constexpr double epsilon = 1e-10, areaEpsilon = 1e-14;
using Segment = QPair<P2, P2>;
P2 interpolate(P2 a, P2 b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
double distance(P2 a, P2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
double parameter(P2 p, P2 a, P2 b)
{
	const double x = b.x - a.x, y = b.y - a.y;
	return ((p.x - a.x) * x + (p.y - a.y) * y) / (x * x + y * y);
}
bool onSegment(P2 p, P2 a, P2 b)
{
	const double t = parameter(p, a, b);
	return t >= -epsilon && t <= 1 + epsilon && std::abs(orient(a, b, p)) <= epsilon * distance(a, b);
}
bool crosses(P2 a, P2 b, P2 c, P2 d)
{
	const double x = orient(a, b, c), y = orient(a, b, d), u = orient(c, d, a), v = orient(c, d, b);
	return ((x > areaEpsilon && y < -areaEpsilon) || (x < -areaEpsilon && y > areaEpsilon)) &&
		   ((u > areaEpsilon && v < -areaEpsilon) || (u < -areaEpsilon && v > areaEpsilon));
}
P2 canonical(P2 p)
{
	double a = 1 - p.x - p.y, b = p.x, c = p.y;
	if (a < epsilon)
		a = 0;
	if (b < epsilon)
		b = 0;
	if (c < epsilon)
		c = 0;
	const double sum = a + b + c;
	return {b / sum, c / sum};
}
int node(QVector<P2> &points, P2 p)
{
	p = canonical(p);
	for (int i = 0; i < points.size(); ++i)
		if (distance(points[i], p) <= epsilon)
			return i;
	points.append(p);
	return points.size() - 1;
}
QVector<int> corners(ModelTriangle t) { return {t.a, t.b, t.c}; }

class FaceTriangulation
{
  public:
	QVector<P2> points{{0, 0}, {1, 0}, {0, 1}};
	QVector<ModelTriangle> triangles{{0, 1, 2}};
	QSet<ModelEdge> constraints;
	bool build(const QVector<Segment> &segments, ModelWorkProgress &work, QString *error)
	{
		const auto refuse = [&]
		{
			return fail(
				error,
				KnifeText::tr(
					"The knife intersections are too close or complex to triangulate reliably. Move the path or use fewer segments."));
		};
		if (segments.size() > 512 || points.size() > 1024)
			return refuse();
		for (const auto &s : segments)
		{
			node(points, s.first);
			node(points, s.second);
		}
		for (int i = 0; i < segments.size(); ++i)
			for (int j = i + 1; j < segments.size(); ++j)
			{
				if (!work.step())
					return false;
				const auto &[a, b] = segments[i];
				const auto &[c, d] = segments[j];
				const double da = orient(c, d, a), db = orient(c, d, b);
				if (crosses(a, b, c, d))
					node(points, interpolate(a, b, da / (da - db)));
				if (points.size() > 1024)
					return refuse();
			}
		if (points.size() > 1024)
			return refuse();
		// Stable insertion order independent of hash iteration and face winding.
		std::sort(points.begin() + 3, points.end(), [](P2 a, P2 b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
		for (int p = 3; p < points.size(); ++p)
		{
			if (!work.step())
				return false;
			int containing = -1;
			ModelEdge edge{-1, -1};
			for (int f = 0; f < triangles.size(); ++f)
			{
				if (!work.step())
					return false;
				const auto c = corners(triangles[f]);
				bool inside = true;
				for (int k = 0; k < 3; ++k)
					inside &= orient(points[c[k]], points[c[(k + 1) % 3]], points[p]) >= -areaEpsilon;
				if (!inside)
					continue;
				containing = f;
				for (int k = 0; k < 3; ++k)
					if (onSegment(points[p], points[c[k]], points[c[(k + 1) % 3]]))
						edge = modelEdge(c[k], c[(k + 1) % 3]);
				break;
			}
			if (containing < 0)
				return refuse();
			QVector<ModelTriangle> next;
			for (int f = 0; f < triangles.size(); ++f)
			{
				const auto c = corners(triangles[f]);
				if (edge.first >= 0)
				{
					int side = -1;
					for (int k = 0; k < 3; ++k)
						if (modelEdge(c[k], c[(k + 1) % 3]) == edge)
							side = k;
					if (side < 0)
						next.append(triangles[f]);
					else
					{
						next.append({c[side], p, c[(side + 2) % 3]});
						next.append({p, c[(side + 1) % 3], c[(side + 2) % 3]});
					}
				}
				else if (f != containing)
					next.append(triangles[f]);
				else
					for (int k = 0; k < 3; ++k)
						next.append({c[k], c[(k + 1) % 3], p});
			}
			triangles = std::move(next);
		}
		for (const auto &s : segments)
		{
			QVector<QPair<double, int>> along;
			for (int i = 0; i < points.size(); ++i)
				if (onSegment(points[i], s.first, s.second))
					along.append({parameter(points[i], s.first, s.second), i});
			std::sort(along.begin(), along.end());
			for (int i = 0; i + 1 < along.size(); ++i)
				constraints.insert(modelEdge(along[i].second, along[i + 1].second));
		}
		auto ordered = constraints.values();
		std::sort(ordered.begin(), ordered.end());
		QSet<ModelEdge> locked;
		for (auto wanted : ordered)
		{
			if (!work.step())
				return false;
			const auto adjacency = [&]
			{
				QMap<ModelEdge, QVector<int>> result;
				for (int f = 0; f < triangles.size(); ++f)
				{
					const auto c = corners(triangles[f]);
					for (int k = 0; k < 3; ++k)
						result[modelEdge(c[k], c[(k + 1) % 3])].append(f);
				}
				return result;
			};
			auto uses = adjacency();
			std::deque<ModelEdge> queue;
			for (auto i = uses.cbegin(); i != uses.cend(); ++i)
				if (crosses(points[wanted.first], points[wanted.second], points[i.key().first], points[i.key().second]))
					queue.push_back(i.key());
			qint64 iterations = 0;
			int stalled = 0;
			while (!uses.contains(wanted) && !queue.empty())
			{
				if (!work.step())
					return false;
				if (++iterations > qint64(triangles.size()) * triangles.size() * 16)
					return refuse();
				const auto edge = queue.front();
				queue.pop_front();
				if (!uses.contains(edge) || !crosses(points[wanted.first], points[wanted.second], points[edge.first], points[edge.second]))
					continue;
				const auto incident = uses.value(edge);
				if (locked.contains(edge) || incident.size() != 2)
					return refuse();
				int a = -1, b = -1;
				for (int v : corners(triangles[incident[0]]))
					if (v != edge.first && v != edge.second)
						a = v;
				for (int v : corners(triangles[incident[1]]))
					if (v != edge.first && v != edge.second)
						b = v;
				if (a < 0 || b < 0 || a == b)
					return refuse();
				if (!crosses(points[a], points[b], points[edge.first], points[edge.second]))
				{
					queue.push_back(edge);
					if (++stalled > int(queue.size()))
						return refuse();
					continue;
				}
				stalled = 0;
				ModelTriangle ta{a, b, edge.first}, tb{b, a, edge.second};
				if (orient(points[ta.a], points[ta.b], points[ta.c]) < 0)
					std::swap(ta.a, ta.b);
				if (orient(points[tb.a], points[tb.b], points[tb.c]) < 0)
					std::swap(tb.a, tb.b);
				triangles[incident[0]] = ta;
				triangles[incident[1]] = tb;
				uses = adjacency();
				if (crosses(points[wanted.first], points[wanted.second], points[a], points[b]))
					queue.push_back(modelEdge(a, b));
			}
			if (!uses.contains(wanted))
				return refuse();
			locked.insert(wanted);
		}
		for (const auto &t : triangles)
			if (orient(points[t.a], points[t.b], points[t.c]) <= areaEpsilon)
				return refuse();
		return work.check();
	}
};

struct EdgeCut
{
	double t = 0;
	QHash<ModelEdge, int> indices;
	QVector<ModelVec3> positions;
	int identity = -1;
};

class KnifeBuilder
{
  public:
	KnifeBuilder(ModelMesh &mesh, const ModelEdit &edit, ModelSelection &selection, QString *error, const ModelWorkControl &control)
		: mesh(mesh), edit(edit), selection(selection), error(error), control(control), work(control, ModelWorkPhase::Editing, error),
		  surface(mesh.surfaces[selection.surface]), before(surface)
	{
	}
	bool run(ModelEditReport *report)
	{
		QVector<ModelKnifeCut> plan;
		if (!planModelKnife(mesh, selection.surface, edit.tool.referenceFrame, selection.faces, edit.tool.knife, &plan, error, control) ||
			!buildModelGeometricTopology(before, &topology, error, control))
			return false;
		QMap<int, QVector<Segment>> faceCuts;
		for (const auto &cut : plan)
		{
			if (!work.step())
				return false;
			const P2 a = canonical({cut.a[0], cut.a[1]}), b = canonical({cut.b[0], cut.b[1]});
			faceCuts[cut.face].append({a, b});
			for (auto p : {a, b})
			{
				ModelEdge edge;
				double t = 0;
				if (boundary(before.triangles[cut.face], p, &edge, &t))
				{
					auto &splits = edgeCuts[modelEdge(topology.group(edge.first), topology.group(edge.second))];
					if (std::none_of(splits.cbegin(), splits.cend(),
									 [&](const auto &cutPoint) { return std::abs(cutPoint.t - t) <= epsilon; }))
						splits.append(EdgeCut{t, {}, {}});
				}
			}
		}
		for (auto &splits : edgeCuts)
			std::sort(splits.begin(), splits.end(), [](const auto &a, const auto &b) { return a.t < b.t; });
		QVector<ModelTriangle> output;
		QVector<int> sourceFaces;
		QSet<ModelEdge> selected;
		for (int f = 0; f < before.triangles.size(); ++f)
		{
			if (!work.step())
				return false;
			const auto original = before.triangles[f];
			const auto originalCorners = corners(original);
			FaceTriangulation local;
			for (int side = 0; side < 3; ++side)
			{
				const int a = originalCorners[side], b = originalCorners[(side + 1) % 3];
				const auto key = modelEdge(topology.group(a), topology.group(b));
				for (const auto &cut : edgeCuts.value(key))
				{
					const double t = topology.group(a) < topology.group(b) ? cut.t : 1 - cut.t;
					node(local.points, interpolate(local.points[side], local.points[(side + 1) % 3], t));
				}
			}
			if (local.points.size() == 3 && !faceCuts.contains(f))
			{
				output.append(original);
				if (report)
					sourceFaces.append(f);
				continue;
			}
			if (!local.build(faceCuts.value(f), work, error))
				return false;
			if (!reserveCapacity(mesh, selection.surface, 0, output.size() + local.triangles.size() - f - 1, error))
				return false;
			QVector<int> indices = originalCorners;
			for (int i = 3; i < local.points.size(); ++i)
			{
				const int index = append(original, local.points[i]);
				if (index < 0)
					return false;
				indices.append(index);
			}
			for (const auto &t : local.triangles)
			{
				if (!work.step())
					return false;
				const ModelTriangle triangle{indices[t.a], indices[t.b], indices[t.c]};
				if (collapsedInAnyFrame(surface, triangle) || flipsInAnyFrame(surface, original, triangle))
					return fail(error, KnifeText::tr("A knife cut collapses or reverses a face in a retained pose. Move the cut away from "
													 "nearby vertices or edges."));
				output.append(triangle);
				if (report)
					sourceFaces.append(f);
			}
			for (auto edge : local.constraints)
				selected.insert(modelEdge(indices[edge.first], indices[edge.second]));
		}
		// Keep explicit seam marks on each replacement boundary edge.
		QSet<ModelEdge> seams;
		for (auto original : before.uvSeams)
		{
			if (!work.step())
				return false;
			const auto key = modelEdge(topology.group(original.first), topology.group(original.second));
			QVector<QPair<double, int>> chain{
				{0, topology.group(original.first) < topology.group(original.second) ? original.first : original.second},
				{1, topology.group(original.first) < topology.group(original.second) ? original.second : original.first}};
			for (const auto &cut : edgeCuts.value(key))
				if (cut.indices.contains(original))
					chain.append({cut.t, cut.indices.value(original)});
			std::sort(chain.begin(), chain.end());
			for (int i = 0; i + 1 < chain.size(); ++i)
				seams.insert(modelEdge(chain[i].second, chain[i + 1].second));
		}
		surface.triangles = std::move(output);
		surface.uvSeams = std::move(seams);
		selection.faces.clear();
		selection.vertices.clear();
		selection.edges = std::move(selected);
		if (report)
			report->sourceFaces.insert(selection.surface, std::move(sourceFaces));
		return work.check();
	}

  private:
	bool boundary(ModelTriangle face, P2 p, ModelEdge *edge, double *fraction) const
	{
		const double w[3]{1 - p.x - p.y, p.x, p.y};
		const int c[3]{face.a, face.b, face.c};
		for (int i = 0; i < 3; ++i)
		{
			if (w[i] > epsilon)
				continue;
			const int a = (i + 1) % 3, b = (i + 2) % 3;
			if (w[a] <= epsilon || w[b] <= epsilon)
				return false;
			*edge = modelEdge(c[a], c[b]);
			*fraction = topology.group(c[a]) < topology.group(c[b]) ? w[b] / (w[a] + w[b]) : w[a] / (w[a] + w[b]);
			return true;
		}
		return false;
	}
	int append(ModelTriangle face, P2 p)
	{
		if (!work.step())
			return -1;
		ModelEdge edge;
		double t = 0;
		EdgeCut *boundaryCut = nullptr;
		ModelEdge geometry;
		if (boundary(face, p, &edge, &t))
		{
			geometry = modelEdge(topology.group(edge.first), topology.group(edge.second));
			auto &splits = edgeCuts[geometry];
			for (auto &cut : splits)
				if (std::abs(cut.t - t) <= epsilon)
				{
					boundaryCut = &cut;
					break;
				}
			if (!boundaryCut)
			{
				fail(error, KnifeText::tr("A knife boundary intersection could not be matched across its adjacent faces."));
				return -1;
			}
			if (boundaryCut->indices.contains(edge))
				return boundaryCut->indices.value(edge);
		}
		QMap<int, double> weights;
		if (boundaryCut)
		{
			const int a = topology.group(edge.first) < topology.group(edge.second) ? edge.first : edge.second;
			const int b = a == edge.first ? edge.second : edge.first;
			weights[a] = 1 - boundaryCut->t;
			weights[b] = boundaryCut->t;
		}
		else
		{
			weights[face.a] = 1 - p.x - p.y;
			weights[face.b] = p.x;
			weights[face.c] = p.y;
		}
		QVector<QPair<int, double>> blend;
		for (auto i = weights.cbegin(); i != weights.cend(); ++i)
			if (i.value() > 0)
				blend.append({i.key(), i.value()});
		if (!reserveCapacity(mesh, selection.surface, 1, 0, error))
			return -1;
		const int index = appendBlend(&surface, blend);
		if (boundaryCut)
		{
			if (!surface.topologyVertices.isEmpty())
			{
				if (boundaryCut->identity < 0) boundaryCut->identity = index;
				surface.topologyVertices[index] = boundaryCut->identity;
			}
			if (boundaryCut->positions.isEmpty())
				for (const auto &frame : before.frames)
				{
					const P3 a = point(frame.positions[geometry.first]), b = point(frame.positions[geometry.second]);
					boundaryCut->positions.append(vec(a * (1 - boundaryCut->t) + b * boundaryCut->t));
				}
			for (int f = 0; f < surface.frames.size(); ++f)
				surface.frames[f].positions[index] = boundaryCut->positions[f];
			boundaryCut->indices.insert(edge, index);
		}
		if (!before.skinning.isEmpty())
		{
			QVector<ModelSkinBlendSource> sources;
			for (const auto &[vertex, part] : blend)
				sources.append({&before.skinning, vertex, part});
			QVector<ModelJointInfluence> influences;
			switch (blendModelSkinInfluences(sources, &influences, &work))
			{
			case ModelSkinBlendStatus::Blended:
				break;
			case ModelSkinBlendStatus::Cancelled:
				return -1;
			case ModelSkinBlendStatus::NegativeWeight:
				fail(error, KnifeText::tr("Repair negative skin weights before cutting their vertices."));
				return -1;
			case ModelSkinBlendStatus::InfluenceCount:
				fail(error, KnifeText::tr("A knife vertex needs 1–64 skin influences. Repair or reduce the source influences first."));
				return -1;
			}
			surface.skinning.first.append(surface.skinning.influences.size());
			surface.skinning.influences += influences;
			surface.skinning.count.append(influences.size());
		}
		return index;
	}
	ModelMesh &mesh;
	const ModelEdit &edit;
	ModelSelection &selection;
	QString *error;
	const ModelWorkControl &control;
	ModelWorkProgress work;
	ModelSurface &surface;
	const ModelSurface before;
	ModelGeometricTopology topology;
	QMap<ModelEdge, QVector<EdgeCut>> edgeCuts;
};
} // namespace

bool knifeFaces(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
				ModelEditReport *report)
{
	return KnifeBuilder(*mesh, edit, *selection, error, control).run(report);
}
} // namespace vibestudio::model_tools
