#include "core/model_mesh_pose_binding_p.h"
#include "core/model_mesh_tools_p.h"
#include "core/model_rip.h"
#include "core/model_skeleton.h"
#include "core/model_transform_axes.h"
#include <algorithm>
#include <cmath>

namespace vibestudio::model_tools
{
namespace
{
struct RipText
{
	Q_DECLARE_TR_FUNCTIONS(ModelRip)
};
template <typename T> QVector<T> ordered(const QSet<T> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
int corner(const ModelGeometricTopology &topology, ModelTriangle t, int group)
{
	for (int v : {t.a, t.b, t.c})
		if (topology.group(v) == group)
			return v;
	return -1;
}
bool follows(ModelTriangle t, int a, int b) { return (t.a == a && t.b == b) || (t.b == a && t.c == b) || (t.c == a && t.a == b); }
class RipBuilder
{
  public:
	RipBuilder(ModelMesh &mesh, const ModelEdit &edit, ModelSelection &selection, QString *error, const ModelWorkControl &control,
			   ModelEditReport *report)
		: m_mesh(mesh), m_edit(edit), m_selection(selection), m_error(error), m_control(control), m_report(report),
		  m_work(control, ModelWorkPhase::Editing, error), m_surface(mesh.surfaces[selection.surface]), m_before(m_surface)
	{
	}
	bool run()
	{
		if (!planModelRip(m_before, m_selection, m_edit.tool, &m_plan, m_error, m_control))
			return false;
		ModelTransformBasis axes;
		if (!resolveModelTransformAxes(m_mesh, m_edit, &axes, m_error, m_control))
			return false;
		m_delta = point(modelBasisToWorld(m_edit.translation, axes));
		if (m_edit.tool.ripFill && length(m_delta) < 1e-9)
			return refuse(RipText::tr("Rip and Fill needs a nonzero movement to make faces. Use Rip to split without moving."));
		QSet<int> moving, stationary;
		for (int f = 0; f < m_before.triangles.size(); ++f)
		{
			if (!m_work.step())
				return false;
			const auto t = m_before.triangles[f];
			for (int v : {t.a, t.b, t.c})
				if (m_plan.groups.contains(m_plan.geometry.group(v)))
					(m_plan.movingFaces.contains(f) ? moving : stationary).insert(v);
		}
		if (!reserveCapacity(m_mesh, m_selection.surface, (moving & stationary).size() + (m_edit.tool.ripFill ? m_plan.cuts.size() * 4 : 0),
							 m_edit.tool.ripFill ? m_plan.cuts.size() * 2 : 0, m_error))
			return false;
		m_skinned = !m_before.skinning.isEmpty();
		if (m_skinned)
		{
			if (!m_binding.prepare(m_mesh, m_edit.tool.referenceFrame, m_error, m_control))
				return false;
			// Editable sources may share influence ranges. Isolate them before
			// moving a vertex or rebinding its normal, retaining the same values.
			m_surface.skinning = {};
			for (int v = 0; v < m_before.vertexCount; ++v)
			{
				if (!m_work.step())
					return false;
				m_surface.skinning.first.append(m_surface.skinning.influences.size());
				m_surface.skinning.count.append(m_before.skinning.count[v]);
				for (int i = 0; i < m_before.skinning.count[v]; ++i)
				{
					if (!m_work.step())
						return false;
					m_surface.skinning.influences.append(m_before.skinning.influences[m_before.skinning.first[v] + i]);
				}
			}
		}
		for (int v = 0; v < m_before.vertexCount; ++v)
			m_origins.append(v);
		QHash<int, int> movedIdentities;
		int nextIdentity = *std::max_element(m_before.topologyVertices.cbegin(), m_before.topologyVertices.cend()) + 1;
		for (int v : ordered(moving))
		{
			if (!m_work.step())
				return false;
			const int target = stationary.contains(v) ? copyVertex(v) : v;
			if (target < 0 || !displace(target))
				return false;
			const int group = m_plan.geometry.group(v);
			if (!movedIdentities.contains(group)) movedIdentities.insert(group, nextIdentity++);
			m_surface.topologyVertices[target] = movedIdentities.value(group);
			m_mapping.insert(v, target);
		}
		// Do not silently weld or reassign weights when seam copies diverge.
		for (int group : ordered(m_plan.groups))
		{
			int first = -1;
			for (int source : m_plan.geometry.copies[group])
			{
				if (!m_mapping.contains(source))
					continue;
				const int v = m_mapping[source];
				if (first < 0)
					first = v;
				else if (!samePoint(first, v))
					return refuse(
						RipText::tr("Skin bindings separate copies of a rip seam in another pose. Repair the binding before ripping."));
			}
		}
		for (int f = 0; f < m_surface.triangles.size(); ++f)
		{
			if (!m_work.step())
				return false;
			m_faceSources.append(f);
			if (!m_plan.movingFaces.contains(f))
				continue;
			auto &t = m_surface.triangles[f];
			t = {m_mapping.value(t.a, t.a), m_mapping.value(t.b, t.b), m_mapping.value(t.c, t.c)};
			m_affected << t.a << t.b << t.c;
			for (int frame = 0; frame < m_surface.frames.size(); ++frame)
			{
				if (!m_work.step())
					return false;
				const auto &positions = m_surface.frames[frame].positions;
				if (collapsedTriangle(positions[t.a], positions[t.b], positions[t.c]) ||
					dot(faceCross(m_before, frame, m_before.triangles[f]), faceCross(m_surface, frame, t)) <= 0)
					return refuse(
						RipText::tr("Rip collapses or reverses a neighbouring face in a retained pose. Reduce or change the movement."));
			}
		}
		for (auto edge : ordered(m_before.uvSeams))
		{
			if (!m_work.step())
				return false;
			m_surface.uvSeams.insert(modelEdge(m_mapping.value(edge.first, edge.first), m_mapping.value(edge.second, edge.second)));
		}
		m_selection.vertices.clear();
		m_selection.edges.clear();
		m_selection.faces.clear();
		if (m_plan.vertexSelection)
			for (int v : ordered(moving))
				m_selection.vertices.insert(m_mapping[v]);
		for (auto edge : ordered(m_plan.cuts))
		{
			if (!m_work.step())
				return false;
			const auto faces = m_plan.geometry.edgeFaces.value(edge);
			const int movedFace = m_plan.movingFaces.contains(faces[0]) ? faces[0] : faces[1];
			const int fixedFace = faces[0] == movedFace ? faces[1] : faces[0];
			const auto fixed = m_before.triangles[fixedFace], moved = m_before.triangles[movedFace];
			int a = corner(m_plan.geometry, fixed, edge.first), b = corner(m_plan.geometry, fixed, edge.second);
			int ma = corner(m_plan.geometry, moved, edge.first), mb = corner(m_plan.geometry, moved, edge.second);
			ma = m_mapping.value(ma, ma);
			mb = m_mapping.value(mb, mb);
			if (!follows(fixed, a, b))
			{
				std::swap(a, b);
				std::swap(ma, mb);
			}
			m_surface.uvSeams.insert(modelEdge(a, b));
			m_surface.uvSeams.insert(modelEdge(ma, mb));
			if (!m_plan.vertexSelection && m_plan.selectedEdges.contains(edge))
				m_selection.edges.insert(modelEdge(ma, mb));
			if (m_edit.tool.ripFill && !fillGap(a, b, ma, mb, movedFace))
				return false;
		}
		if (!normalise())
			return false;
		pruneSeams(&m_surface);
		if (m_report)
			m_report->sourceFaces.insert(m_selection.surface, m_faceSources);
		return m_work.check();
	}

  private:
	bool refuse(const QString &text) { return fail(m_error, text); }
	bool weightSum(int v, double *sum)
	{
		const int first = m_surface.skinning.first[v], count = m_surface.skinning.count[v];
		if (count < 1 || count > 64)
			return refuse(RipText::tr("Rip vertices need 1–64 skin influences. Repair the binding first."));
		*sum = 0;
		for (int i = first; i < first + count; ++i)
		{
			if (!m_work.step())
				return false;
			const auto weight = m_surface.skinning.influences[i].weight;
			if (weight < 0)
				return refuse(RipText::tr("Repair negative skin weights before ripping their vertices."));
			*sum += weight;
		}
		return std::isfinite(*sum) && *sum >= 1e-8 ? true : refuse(RipText::tr("Rip vertices need a nonzero skin-weight sum."));
	}
	int copyVertex(int source)
	{
		if (!m_work.step())
			return -1;
		const int result = m_surface.vertexCount++;
		const auto uv = m_surface.texCoords[source];
		m_surface.texCoords.append(uv);
		m_surface.topologyVertices.append(m_surface.topologyVertices[source]);
		m_origins.append(m_origins[source]);
		for (auto &frame : m_surface.frames)
		{
			if (!m_work.step())
				return -1;
			const auto p = frame.positions[source], n = frame.normals[source];
			frame.positions.append(p);
			frame.normals.append(n);
		}
		if (m_skinned)
		{
			const int first = m_surface.skinning.first[source], count = m_surface.skinning.count[source];
			m_surface.skinning.first.append(m_surface.skinning.influences.size());
			m_surface.skinning.count.append(count);
			for (int i = first; i < first + count; ++i)
			{
				if (!m_work.step())
					return -1;
				const auto influence = m_surface.skinning.influences[i];
				m_surface.skinning.influences.append(influence);
			}
		}
		return result;
	}
	bool displace(int vertex)
	{
		QVector<ModelVec3> local;
		if (m_skinned)
		{
			double sum = 0;
			if (!weightSum(vertex, &sum))
				return false;
			const int first = m_surface.skinning.first[vertex], count = m_surface.skinning.count[vertex];
			for (int i = first; i < first + count; ++i)
			{
				if (!m_work.step())
					return false;
				auto &influence = m_surface.skinning.influences[i];
				const auto delta = modelJointTransformVector(m_binding.inverseReference[influence.joint], vec(m_delta * (1 / sum)));
				local.append(delta);
				influence.offset = vec(point(influence.offset) + point(delta));
			}
		}
		for (int frame = 0; frame < m_surface.frames.size(); ++frame)
		{
			if (!m_work.step())
				return false;
			auto delta = m_delta;
			if (m_skinned && m_mesh.skeleton.bakedClipForFrame[frame] >= -1)
			{
				delta = {};
				const auto &pose = m_binding.pose(m_mesh, frame);
				for (int i = 0; i < local.size(); ++i)
				{
					if (!m_work.step())
						return false;
					const auto &influence = m_surface.skinning.influences[m_surface.skinning.first[vertex] + i];
					delta = delta + point(modelJointTransformVector(pose[influence.joint], local[i])) * influence.weight;
				}
			}
			m_surface.frames[frame].positions[vertex] = vec(point(m_surface.frames[frame].positions[vertex]) + delta);
		}
		return m_work.check();
	}
	bool samePoint(int a, int b)
	{
		for (const auto &frame : m_surface.frames)
		{
			if (!m_work.step())
				return false;
			const auto x = frame.positions[a], y = frame.positions[b];
			if (x.x != y.x || x.y != y.y || x.z != y.z)
				return false;
		}
		return true;
	}
	bool fillGap(int a, int b, int ma, int mb, int face)
	{
		const int sources[]{a, b, ma, mb};
		int copies[]{-1, -1, -1, -1};
		const int frame = m_edit.tool.referenceFrame;
		const double tile = m_edit.tool.uvTileSize, u = length(position(m_surface, frame, b) - position(m_surface, frame, a)) / tile;
		const double va = length(position(m_surface, frame, ma) - position(m_surface, frame, a)) / tile;
		const double vb = length(position(m_surface, frame, mb) - position(m_surface, frame, b)) / tile;
		const ModelTexCoord uvs[]{{0, 0}, {float(u), 0}, {0, float(va)}, {float(u), float(vb)}};
		const int firstFace = m_surface.triangles.size();
		for (const auto corners : {std::array<int, 3>{1, 0, 2}, std::array<int, 3>{1, 2, 3}})
		{
			if (!m_work.step())
				return false;
			const int x = sources[corners[0]], y = sources[corners[1]], z = sources[corners[2]];
			if (samePoint(x, y) || samePoint(x, z) || samePoint(y, z))
			{
				if (!m_work.check())
					return false;
				continue;
			}
			for (const auto &pose : m_surface.frames)
			{
				if (!m_work.step())
					return false;
				if (collapsedTriangle(pose.positions[x], pose.positions[y], pose.positions[z]))
					return refuse(RipText::tr("Rip fill collapses in a retained pose. Change the movement or leave the gap open."));
			}
			for (int c : corners)
				if (copies[c] < 0)
				{
					copies[c] = copyVertex(sources[c]);
					if (copies[c] < 0)
						return false;
					m_surface.texCoords[copies[c]] = uvs[c];
					m_strip.insert(copies[c]);
					m_affected.insert(copies[c]);
				}
			m_surface.triangles.append({copies[corners[0]], copies[corners[1]], copies[corners[2]]});
			m_faceSources.append(face);
		}
		QHash<ModelEdge, int> stripEdges;
		for (int f = firstFace; f < m_surface.triangles.size(); ++f)
		{
			const auto t = m_surface.triangles[f];
			for (auto edge : {modelEdge(t.a, t.b), modelEdge(t.b, t.c), modelEdge(t.c, t.a)})
				++stripEdges[edge];
		}
		for (auto i = stripEdges.cbegin(); i != stripEdges.cend(); ++i)
			if (i.value() == 1)
				m_surface.uvSeams.insert(i.key());
		if (m_surface.triangles.size() == firstFace + 2)
			for (int f = 0; f < m_surface.frames.size(); ++f)
			{
				if (!m_work.step())
					return false;
				if (dot(faceCross(m_surface, f, m_surface.triangles[firstFace]),
						faceCross(m_surface, f, m_surface.triangles[firstFace + 1])) <= 0)
					return refuse(RipText::tr("Rip fill folds in a retained pose. Change the movement or leave the gap open."));
			}
		return m_work.check();
	}
	bool normalise()
	{
		ModelGeometricTopology geometry;
		if (!buildModelGeometricTopology(m_surface, &geometry, m_error, m_control))
			return false;
		QSet<int> groups, changed;
		for (int v : m_affected)
			groups.insert(geometry.group(v));
		const auto used = usedVertices(m_surface);
		for (int frame = 0; frame < m_surface.frames.size(); ++frame)
			for (int group : ordered(groups))
			{
				QHash<int, QByteArray> keys;
				QHash<QByteArray, P3> sums;
				for (int v : geometry.copies[group])
				{
					if (!m_work.step())
						return false;
					if (!used.contains(v))
						continue;
					const auto normal = m_before.frames[frame].normals[m_origins[v]];
					// Canonicalise signed zero, and avoid structure padding in the key.
					const float components[]{normal.x == 0 ? 0.f : normal.x, normal.y == 0 ? 0.f : normal.y,
											 normal.z == 0 ? 0.f : normal.z};
					const auto key = m_strip.contains(v) ? QByteArray("fill")
														 : QByteArray(reinterpret_cast<const char *>(components), sizeof(components));
					keys.insert(v, key);
					changed.insert(v);
				}
				for (int f : geometry.groupFaces[group])
				{
					if (!m_work.step())
						return false;
					const auto t = m_surface.triangles[f];
					const int corners[]{t.a, t.b, t.c};
					for (int c = 0; c < 3; ++c)
						if (geometry.group(corners[c]) == group)
						{
							const auto key = keys.value(corners[c]);
							sums[key] = sums.value(key, P3{}) + cornerNormal(m_surface, frame, t, c);
						}
				}
				for (auto i = keys.cbegin(); i != keys.cend(); ++i)
				{
					if (!m_work.step())
						return false;
					const auto normal = sums.value(i.value());
					if (length(normal) < 1e-12)
						return refuse(
							RipText::tr("Rip produces an ambiguous vertex normal. Repair the neighbourhood or change the movement."));
					m_surface.frames[frame].normals[i.key()] = vec(unit(normal));
				}
			}
		if (m_skinned)
			for (int v : ordered(changed))
			{
				double sum = 0;
				if (!weightSum(v, &sum))
					return false;
				const int first = m_surface.skinning.first[v], count = m_surface.skinning.count[v];
				const auto normal = point(m_surface.frames[m_edit.tool.referenceFrame].normals[v]) * (1 / sum);
				for (int i = first; i < first + count; ++i)
				{
					if (!m_work.step())
						return false;
					auto &influence = m_surface.skinning.influences[i];
					influence.normalOffset = modelJointTransformVector(m_binding.inverseReference[influence.joint], vec(normal));
				}
				for (int f = 0; f < m_surface.frames.size(); ++f)
				{
					if (!m_work.step())
						return false;
					if (m_mesh.skeleton.bakedClipForFrame[f] < -1)
						continue;
					P3 result{};
					const auto &pose = m_binding.pose(m_mesh, f);
					for (int i = first; i < first + count; ++i)
					{
						if (!m_work.step())
							return false;
						const auto &influence = m_surface.skinning.influences[i];
						result =
							result + point(modelJointTransformVector(pose[influence.joint], influence.normalOffset)) * influence.weight;
					}
					m_surface.frames[f].normals[v] = vec(unit(result));
				}
			}
		return m_work.check();
	}
	ModelMesh &m_mesh;
	const ModelEdit &m_edit;
	ModelSelection &m_selection;
	QString *m_error;
	const ModelWorkControl &m_control;
	ModelEditReport *m_report;
	ModelWorkProgress m_work;
	ModelSurface &m_surface;
	const ModelSurface m_before;
	ModelRipPlan m_plan;
	ModelMeshPoseBinding m_binding;
	P3 m_delta{};
	bool m_skinned = false;
	QHash<int, int> m_mapping;
	QVector<int> m_origins, m_faceSources;
	QSet<int> m_affected, m_strip;
};
} // namespace
bool ripGeometry(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
				 ModelEditReport *report)
{
	bool cancelled = false;
	auto work = control;
	work.cancelled = [&] { return cancelled = cancelled || (control.cancelled && control.cancelled()); };
	if (!materialiseModelVertexIdentities(&mesh->surfaces[selection->surface], error, work)) return false;
	compactModelVertexIdentities(&mesh->surfaces[selection->surface]);
	return RipBuilder(*mesh, edit, *selection, error, work, report).run();
}
} // namespace vibestudio::model_tools
