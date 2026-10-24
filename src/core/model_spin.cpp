#include "core/model_mesh_tools_p.h"
#include "core/model_skeleton.h"

#include <QMap>
#include <algorithm>
#include <cmath>
#include <numbers>

// Spin's centre/axis/steps and duplication workflow follows Blender's documented
// operator (manual 4.0, reviewed 2026-10-21); this implementation is original.
// https://docs.blender.org/manual/en/4.0/modeling/meshes/tools/spin.html
namespace vibestudio::model_tools
{
namespace
{
QVector<int> sortedValues(const QSet<int> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
QVector<ModelEdge> sortedEdges(const QSet<ModelEdge> &values)
{
	auto result = values.values().toVector();
	std::sort(result.begin(), result.end());
	return result;
}
struct SpinText
{
	Q_DECLARE_TR_FUNCTIONS(ModelSpin)
};
struct Border
{
	int a = -1, b = -1, face = -1;
};
struct EdgeUse
{
	Border border;
	int count = 0, balance = 0;
};

class SpinBuilder
{
  public:
	SpinBuilder(ModelMesh &mesh, const ModelEdit &edit, ModelSelection &selection, QString *error, const ModelWorkControl &control,
				ModelEditReport *report)
		: m_mesh(mesh), m_selection(selection), m_error(error), m_control(control), m_report(report),
		  m_work(control, ModelWorkPhase::Editing, error), m_surface(mesh.surfaces[selection.surface]), m_before(m_surface),
		  m_options(edit.tool), m_centre{m_options.point[0], m_options.point[1], m_options.point[2]},
		  m_axis{m_options.normal[0], m_options.normal[1], m_options.normal[2]}
	{
	}
	bool run()
	{
		if (!prepare())
			return false;
		if (m_options.spinAngle == 0)
			return m_work.check();
		m_normalGroups.fill(-1, m_before.vertexCount);
		m_surface.triangles.clear();
		for (int face = 0; face < m_before.triangles.size(); ++face)
		{
			if (!m_work.step())
				return false;
			if (m_options.spinDuplicate || !m_selectedFaces.contains(face))
			{
				m_surface.triangles.append(m_before.triangles[face]);
				m_faceSources.append(face);
			}
		}
		m_selection.vertices.clear();
		m_selection.edges.clear();
		m_selection.faces.clear();
		if (m_options.spinDuplicate)
		{
			for (int step = 1; step <= m_options.spinSteps - int(m_closed); ++step)
				if (!copyFaces(step))
					return false;
		}
		else
		{
			for (const auto &border : m_borders)
				if (!sweepBorder(border))
					return false;
			if (!m_closed && !m_selectedFaces.isEmpty())
			{
				m_selection.faces.clear();
				if (!copyFaces(m_options.spinSteps))
					return false;
			}
			if (!normaliseStrips())
				return false;
		}
		if (!compact())
			return false;
		pruneSeams(&m_surface);
		if (m_report)
			m_report->sourceFaces.insert(m_selection.surface, m_faceSources);
		return m_work.check();
	}

  private:
	bool refuse(const QString &message) { return fail(m_error, message); }
	bool prepare()
	{
		if (!m_work.check())
			return false;
		if (!std::isfinite(m_options.spinAngle) || std::abs(m_options.spinAngle) > 360 || m_options.spinSteps < 1 ||
			m_options.spinSteps > 256 || length(m_axis) < 1e-9)
			return refuse(SpinText::tr("Spin needs an angle from −360 to 360 degrees, 1–256 steps and a nonzero axis."));
		m_axis = unit(m_axis);
		m_closed = m_options.spinClose && std::abs(m_options.spinAngle) == 360;
		if ((m_closed && m_options.spinSteps < 3) ||
			(!m_options.spinDuplicate && std::abs(m_options.spinAngle) / m_options.spinSteps >= 180))
			return refuse(SpinText::tr("Use at least three steps for a closed turn, and less than 180 degrees per extrusion step."));
		m_selectedFaces = m_selection.faces;
		if ((!m_selection.edges.isEmpty() && !m_selectedFaces.isEmpty()) || (m_selection.edges.isEmpty() && m_selectedFaces.isEmpty()) ||
			(!m_selection.vertices.isEmpty() && m_selectedFaces.isEmpty()))
			return refuse(SpinText::tr("Select a face region or open boundary edges to spin."));
		if (m_options.spinDuplicate && (m_selectedFaces.isEmpty() || m_options.spinAngle == 0))
			return refuse(SpinText::tr("Spin duplicates need selected faces and a nonzero angle."));
		ModelGeometricTopology topology;
		if (!buildModelGeometricTopology(m_before, &topology, m_error, m_control))
			return false;
		QMap<ModelEdge, EdgeUse> edges;
		for (int face : sortedValues(m_selectedFaces))
		{
			if (!m_work.step())
				return false;
			const auto t = m_before.triangles[face];
			m_sourceVertices << t.a << t.b << t.c;
			for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
			{
				const auto key = topology.geometricEdge(a, b);
				auto &use = edges[key];
				use.border = {a, b, face};
				++use.count;
				use.balance += topology.group(a) == key.first ? 1 : -1;
			}
		}
		if (!m_options.spinDuplicate)
		{
			for (auto i = edges.cbegin(); i != edges.cend(); ++i)
			{
				if (!m_work.step())
					return false;
				if (i->count > 2 || (i->count == 2 && i->balance != 0))
					return refuse(SpinText::tr("Spin extrusion needs a consistently wound face region with at most two faces per edge."));
				if (i->count == 1)
				{
					if (m_closed && !topology.boundary(i.key()))
						return refuse(
							SpinText::tr("A closed spin needs a detached profile. Separate its faces before closing a complete turn."));
					m_borders.append(i->border);
				}
			}
			QSet<ModelEdge> seen;
			for (auto edge : sortedEdges(m_selection.edges))
			{
				if (!m_work.step())
					return false;
				const auto key = topology.geometricEdge(edge.first, edge.second);
				if (!topology.boundary(key) || m_closed)
					return refuse(
						SpinText::tr("Edge spin needs open borders. To close a full turn, select a detached face profile instead."));
				if (seen.contains(key))
					continue;
				seen.insert(key);
				const int face = topology.edgeFaces.value(key).first();
				const auto t = m_before.triangles[face];
				for (const auto &[a, b] : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
					if (topology.geometricEdge(a, b) == key)
					{
						m_borders.append({b, a, face});
						m_sourceVertices << a << b;
					}
			}
			if (m_borders.isEmpty())
				return refuse(SpinText::tr("The selected region has no boundary to extrude. Use Spin Duplicates for a closed object."));
		}
		const qint64 steps = m_options.spinSteps;
		const qint64 capCopies = m_options.spinDuplicate ? steps - int(m_closed) : int(!m_closed && !m_selectedFaces.isEmpty());
		const qint64 addedVertices =
			m_sourceVertices.size() * capCopies + (m_options.spinDuplicate ? 0 : 2 * qint64(m_borders.size()) * (steps + 1));
		const qint64 addedTriangles =
			m_selectedFaces.size() * capCopies + (m_options.spinDuplicate ? 0 : 2 * qint64(m_borders.size()) * steps);
		if (!reserveCapacity(m_mesh, m_selection.surface, addedVertices, addedTriangles, m_error))
			return false;
		if (!m_before.skinning.isEmpty())
		{
			if (!prepareSkinPoses())
				return false;
			if (m_mesh.skeleton.bakedClipForFrame[m_options.referenceFrame] < -1)
				return refuse(SpinText::tr("Spin skinned geometry from its bind pose or a retained skeletal pose."));
			for (const auto &matrix : skinPose(m_options.referenceFrame))
			{
				if (!m_work.step())
					return false;
				bool valid = false;
				m_inverse.append(modelJointInverse(matrix, &valid));
				if (!valid)
					return refuse(SpinText::tr("The reference skeleton pose has a singular transform."));
			}
			for (int v : sortedValues(m_sourceVertices))
			{
				if (!m_work.step())
					return false;
				const int first = m_before.skinning.first[v], count = m_before.skinning.count[v];
				double sum = 0;
				if (count < 1 || count > 64)
					return refuse(SpinText::tr("Spin vertices need 1–64 skin influences. Repair the binding first."));
				for (int i = first; i < first + count; ++i)
				{
					const auto weight = m_before.skinning.influences[i].weight;
					if (weight < 0)
						return refuse(SpinText::tr("Repair negative skin weights before spinning their vertices."));
					sum += weight;
				}
				if (!std::isfinite(sum) || sum < 1e-8)
					return refuse(SpinText::tr("Spin vertices need a nonzero skin-weight sum."));
				m_weightSums.insert(v, sum);
			}
		}
		return m_work.check();
	}
	bool prepareSkinPoses()
	{
		// Weight-authoring helpers intentionally refuse independent frames. Spin
		// keeps those frames and rotates their own stored geometry, while complete
		// contiguous clip ranges still identify their exact retained joint poses.
		const auto &skeleton = m_mesh.skeleton;
		const auto invalid = [&]
		{
			return refuse(
				SpinText::tr("Spin needs complete, contiguous retained skeletal clip ranges. Repair ambiguous frame mappings first."));
		};
		if (skeleton.bakedClipForFrame.size() != m_mesh.frames.size())
			return invalid();
		m_bindPose = modelSkeletonBindPose(skeleton);
		m_localFrames.fill(-1, m_mesh.frames.size());
		QVector<int> counts(skeleton.clips.size(), 0);
		int previous = -3;
		for (int frame = 0; frame < m_mesh.frames.size(); ++frame)
		{
			if (!m_work.step())
				return false;
			const int clip = skeleton.bakedClipForFrame[frame];
			if (clip < -2 || clip >= counts.size())
				return invalid();
			if (clip >= 0)
			{
				if ((previous != clip && counts[clip] != 0) || counts[clip] >= skeleton.clips[clip].frames.size())
					return invalid();
				m_localFrames[frame] = counts[clip]++;
			}
			previous = clip;
		}
		for (int clip = 0; clip < counts.size(); ++clip)
		{
			if (!m_work.step())
				return false;
			if (counts[clip] != 0 && counts[clip] != skeleton.clips[clip].frames.size())
				return invalid();
		}
		return m_work.check();
	}
	const QVector<ModelJointMatrix> &skinPose(int frame) const
	{
		const int clip = m_mesh.skeleton.bakedClipForFrame[frame];
		return clip < 0 ? m_bindPose : m_mesh.skeleton.clips[clip].frames[m_localFrames[frame]];
	}
	P3 rotate(P3 value, int step) const
	{
		if (step == 0 || (m_closed && step == m_options.spinSteps))
			return value;
		const double angle = m_options.spinAngle * (std::numbers::pi / 180) * step / m_options.spinSteps;
		return value * std::cos(angle) + cross(m_axis, value) * std::sin(angle) + m_axis * (dot(m_axis, value) * (1 - std::cos(angle)));
	}
	P3 transformed(P3 value, int step) const
	{
		if (step == 0 || (m_closed && step == m_options.spinSteps) || length(cross(m_axis, value - m_centre)) == 0)
			return value;
		return m_centre + rotate(value - m_centre, step);
	}
	int copyVertex(int source, int step, bool strip)
	{
		if (!m_work.step())
			return -1;
		const int result = m_surface.vertexCount++;
		m_surface.texCoords.append(m_before.texCoords[source]);
		if (!m_surface.topologyVertices.isEmpty()) m_surface.topologyVertices.append(result);
		m_normalGroups.append(-1);
		const bool skinned = !m_before.skinning.isEmpty();
		const auto reference = position(m_before, m_options.referenceFrame, source);
		const auto delta = transformed(reference, step) - reference;
		QVector<ModelVec3> localDeltas;
		if (skinned)
		{
			const int first = m_before.skinning.first[source], count = m_before.skinning.count[source];
			m_surface.skinning.first.append(m_surface.skinning.influences.size());
			m_surface.skinning.count.append(count);
			const auto normal = rotate(point(m_before.frames[m_options.referenceFrame].normals[source]), step);
			for (int i = first; i < first + count; ++i)
			{
				if (!m_work.step())
					return -1;
				auto influence = m_before.skinning.influences[i];
				const auto offset = modelJointTransformVector(m_inverse[influence.joint], vec(delta * (1 / m_weightSums[source])));
				localDeltas.append(offset);
				influence.offset = vec(point(influence.offset) + point(offset));
				influence.normalOffset =
					strip ? ModelVec3{} : modelJointTransformVector(m_inverse[influence.joint], vec(normal * (1 / m_weightSums[source])));
				m_surface.skinning.influences.append(influence);
			}
		}
		for (int frame = 0; frame < m_surface.frames.size(); ++frame)
		{
			if (!m_work.step())
				return -1;
			auto p = transformed(position(m_before, frame, source), step);
			auto n = rotate(point(m_before.frames[frame].normals[source]), step);
			if (skinned && m_mesh.skeleton.bakedClipForFrame[frame] >= -1)
			{
				p = position(m_before, frame, source);
				n = {0, 0, 0};
				const auto &pose = skinPose(frame);
				const int first = m_surface.skinning.first[result];
				for (int i = 0; i < localDeltas.size(); ++i)
				{
					if (!m_work.step())
						return -1;
					const auto &influence = m_surface.skinning.influences[first + i];
					p = p + point(modelJointTransformVector(pose[influence.joint], localDeltas[i])) * influence.weight;
					n = n + point(modelJointTransformVector(pose[influence.joint], influence.normalOffset)) * influence.weight;
				}
			}
			m_surface.frames[frame].positions.append(vec(p));
			m_surface.frames[frame].normals.append(strip ? ModelVec3{} : vec(unit(n)));
		}
		if (!m_surface.topologyVertices.isEmpty())
		{
			const int original = m_before.topologyVertices[source];
			if (!m_options.spinDuplicate && (step == 0 || (m_closed && step == m_options.spinSteps) || samePoint(source, result)))
				m_surface.topologyVertices[result] = original;
			else
			{
				const auto key = qMakePair(step, original);
				if (!m_copyIdentities.contains(key)) m_copyIdentities.insert(key, result);
				m_surface.topologyVertices[result] = m_copyIdentities.value(key);
			}
		}
		return result;
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
	bool addTriangle(ModelTriangle triangle, int source, bool poleAllowed = false)
	{
		if (!m_work.step())
			return false;
		if (poleAllowed && (samePoint(triangle.a, triangle.b) || samePoint(triangle.a, triangle.c) || samePoint(triangle.b, triangle.c)))
			return m_work.check();
		for (const auto &frame : m_surface.frames)
		{
			if (!m_work.step())
				return false;
			if (collapsedTriangle(frame.positions[triangle.a], frame.positions[triangle.b], frame.positions[triangle.c]))
				return refuse(SpinText::tr("Spin collapses a face in a retained pose. Change the axis, angle, profile or step count."));
		}
		m_selection.faces.insert(m_surface.triangles.size());
		m_surface.triangles.append(triangle);
		m_faceSources.append(source);
		return true;
	}
	bool copyFaces(int step)
	{
		QHash<int, int> mapping;
		for (int source : sortedValues(m_sourceVertices))
		{
			const int copy = copyVertex(source, step, false);
			if (copy < 0)
				return false;
			mapping.insert(source, copy);
		}
		for (int face : sortedValues(m_selectedFaces))
		{
			const auto t = m_before.triangles[face];
			if (!addTriangle({mapping[t.a], mapping[t.b], mapping[t.c]}, face))
				return false;
		}
		for (auto edge : m_before.uvSeams)
		{
			if (!m_work.step())
				return false;
			if (mapping.contains(edge.first) && mapping.contains(edge.second))
				m_surface.uvSeams.insert(modelEdge(mapping[edge.first], mapping[edge.second]));
		}
		return m_work.check();
	}
	bool sweepBorder(const Border &border)
	{
		QVector<std::array<int, 2>> columns;
		const auto a = position(m_before, m_options.referenceFrame, border.a), b = position(m_before, m_options.referenceFrame, border.b);
		const double span = length(b - a) / m_options.uvTileSize;
		const double arc = std::abs(m_options.spinAngle) * (std::numbers::pi / 180) / m_options.uvTileSize;
		const double radius[2]{length(cross(m_axis, a - m_centre)), length(cross(m_axis, b - m_centre))};
		for (int step = 0; step <= m_options.spinSteps; ++step)
		{
			const int x = copyVertex(border.a, step, true), y = copyVertex(border.b, step, true);
			if (x < 0 || y < 0)
				return false;
			columns.append({x, y});
			for (int side = 0; side < 2; ++side)
			{
				const int vertex = columns.last()[side], start = columns.first()[side];
				m_surface.texCoords[vertex] = {float(arc * radius[side] * step / m_options.spinSteps), float(side * span)};
				m_normalGroups[vertex] = samePoint(start, vertex) ? start : vertex;
			}
			if (step > 0)
			{
				const auto previous = columns[step - 1];
				const auto firstFace = m_surface.triangles.size();
				if (!addTriangle({previous[0], previous[1], y}, border.face, true) || !addTriangle({previous[0], y, x}, border.face, true))
					return false;
				if (m_surface.triangles.size() == firstFace + 2)
					for (int frame = 0; frame < m_surface.frames.size(); ++frame)
					{
						if (!m_work.step())
							return false;
						if (dot(faceCross(m_surface, frame, m_surface.triangles[firstFace]),
								faceCross(m_surface, frame, m_surface.triangles[firstFace + 1])) <= 0)
							return refuse(SpinText::tr("Spin folds a band in a retained pose. Change the profile or add smaller steps."));
					}
				m_surface.uvSeams.insert(modelEdge(previous[0], x));
				m_surface.uvSeams.insert(modelEdge(previous[1], y));
			}
		}
		m_surface.uvSeams.insert(modelEdge(columns.first()[0], columns.first()[1]));
		m_surface.uvSeams.insert(modelEdge(columns.last()[0], columns.last()[1]));
		if (m_selectedFaces.isEmpty() && !m_closed)
		{
			m_selection.faces.clear();
			m_selection.edges.insert(modelEdge(columns.last()[0], columns.last()[1]));
		}
		return m_work.check();
	}
	bool normaliseStrips()
	{
		const auto used = usedVertices(m_surface);
		for (int frame = 0; frame < m_surface.frames.size(); ++frame)
		{
			QVector<P3> sums(m_surface.vertexCount, P3{0, 0, 0});
			for (const auto &t : m_surface.triangles)
			{
				if (!m_work.step())
					return false;
				const auto normal = faceCross(m_surface, frame, t);
				for (int v : {t.a, t.b, t.c})
					if (m_normalGroups[v] >= 0)
						sums[m_normalGroups[v]] = sums[m_normalGroups[v]] + normal;
			}
			for (int v = m_before.vertexCount; v < m_surface.vertexCount; ++v)
			{
				if (!m_work.step())
					return false;
				if (m_normalGroups[v] < 0 || !used.contains(v))
					continue;
				const auto normal = sums[m_normalGroups[v]];
				if (length(normal) < 1e-12)
					return refuse(SpinText::tr("Spin creates an ambiguous folded band. Change the profile or step count."));
				m_surface.frames[frame].normals[v] = vec(unit(normal));
			}
		}
		if (!m_before.skinning.isEmpty())
		{
			for (int v = m_before.vertexCount; v < m_surface.vertexCount; ++v)
			{
				if (!m_work.step())
					return false;
				if (m_normalGroups[v] < 0 || !used.contains(v))
					continue;
				const int first = m_surface.skinning.first[v], count = m_surface.skinning.count[v];
				double sum = 0;
				for (int i = first; i < first + count; ++i)
					sum += m_surface.skinning.influences[i].weight;
				const auto normal = point(m_surface.frames[m_options.referenceFrame].normals[v]) * (1 / sum);
				for (int i = first; i < first + count; ++i)
				{
					auto &influence = m_surface.skinning.influences[i];
					influence.normalOffset = modelJointTransformVector(m_inverse[influence.joint], vec(normal));
				}
				for (int frame = 0; frame < m_surface.frames.size(); ++frame)
				{
					if (!m_work.step())
						return false;
					if (m_mesh.skeleton.bakedClipForFrame[frame] < -1)
						continue;
					P3 result{0, 0, 0};
					const auto &pose = skinPose(frame);
					for (int i = first; i < first + count; ++i)
					{
						const auto &influence = m_surface.skinning.influences[i];
						result =
							result + point(modelJointTransformVector(pose[influence.joint], influence.normalOffset)) * influence.weight;
					}
					m_surface.frames[frame].normals[v] = vec(unit(result));
				}
			}
		}
		return m_work.check();
	}
	bool compact()
	{
		const auto used = usedVertices(m_surface), oldUsed = usedVertices(m_before);
		QVector<int> mapping(m_surface.vertexCount, -1);
		ModelSurface result = m_surface;
		result.texCoords.clear();
		result.topologyVertices.clear();
		result.skinning = {};
		for (auto &frame : result.frames)
		{
			frame.positions.clear();
			frame.normals.clear();
		}
		for (int v = 0; v < m_surface.vertexCount; ++v)
		{
			if (!m_work.step())
				return false;
			if (!used.contains(v) && (v >= m_before.vertexCount || oldUsed.contains(v)))
				continue;
			mapping[v] = result.texCoords.size();
			result.texCoords.append(m_surface.texCoords[v]);
			if (!m_surface.topologyVertices.isEmpty()) result.topologyVertices.append(m_surface.topologyVertices[v]);
			for (int frame = 0; frame < result.frames.size(); ++frame)
			{
				if (!m_work.step())
					return false;
				result.frames[frame].positions.append(m_surface.frames[frame].positions[v]);
				result.frames[frame].normals.append(m_surface.frames[frame].normals[v]);
			}
			if (!m_surface.skinning.isEmpty())
			{
				const int first = m_surface.skinning.first[v], count = m_surface.skinning.count[v];
				result.skinning.first.append(result.skinning.influences.size());
				result.skinning.count.append(count);
				for (int i = first; i < first + count; ++i)
				{
					if (!m_work.step())
						return false;
					result.skinning.influences.append(m_surface.skinning.influences[i]);
				}
			}
		}
		for (auto &t : result.triangles)
		{
			if (!m_work.step())
				return false;
			t = {mapping[t.a], mapping[t.b], mapping[t.c]};
		}
		const auto remap = [&](const QSet<ModelEdge> &edges, QSet<ModelEdge> *resultEdges)
		{
			for (auto edge : edges)
			{
				if (!m_work.step())
					return false;
				if (mapping[edge.first] >= 0 && mapping[edge.second] >= 0)
					resultEdges->insert(modelEdge(mapping[edge.first], mapping[edge.second]));
			}
			return true;
		};
		QSet<ModelEdge> seams, selectedEdges;
		if (!remap(m_surface.uvSeams, &seams) || !remap(m_selection.edges, &selectedEdges))
			return false;
		result.uvSeams = std::move(seams);
		m_selection.edges = std::move(selectedEdges);
		result.vertexCount = result.texCoords.size();
		compactModelVertexIdentities(&result);
		m_surface = std::move(result);
		return m_work.check();
	}
	ModelMesh &m_mesh;
	ModelSelection &m_selection;
	QString *m_error;
	const ModelWorkControl &m_control;
	ModelEditReport *m_report;
	ModelWorkProgress m_work;
	ModelSurface &m_surface;
	const ModelSurface m_before;
	const ModelMeshToolOptions &m_options;
	P3 m_centre, m_axis;
	bool m_closed = false;
	QSet<int> m_selectedFaces, m_sourceVertices;
	QVector<Border> m_borders;
	QVector<int> m_faceSources, m_normalGroups;
	QVector<ModelJointMatrix> m_bindPose;
	QVector<int> m_localFrames;
	QVector<ModelJointMatrix> m_inverse;
	QHash<int, double> m_weightSums;
	QHash<QPair<int, int>, int> m_copyIdentities;
};
} // namespace

bool spinGeometry(ModelMesh *mesh, const ModelEdit &edit, ModelSelection *selection, QString *error, const ModelWorkControl &control,
				  ModelEditReport *report)
{
	bool cancelled = false;
	ModelWorkControl work = control;
	work.cancelled = [&] { return cancelled = cancelled || (control.cancelled && control.cancelled()); };
	if (edit.tool.spinDuplicate && !materialiseModelVertexIdentities(&mesh->surfaces[selection->surface], error, work)) return false;
	return SpinBuilder(*mesh, edit, *selection, error, work, report).run();
}
} // namespace vibestudio::model_tools
