#include "core/model_weight_paint.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include "core/model_skinning_work.h"
#include <QCoreApplication>
#include <QMap>
#include <algorithm>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelWeightPaint)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool near(ModelVec3 a, ModelVec3 b)
{
	return std::abs(double(a.x) - b.x) <= .0001 && std::abs(double(a.y) - b.y) <= .0001 && std::abs(double(a.z) - b.z) <= .0001;
}
bool normalised(const ModelSurface &surface, int v)
{
	double sum = 0;
	for (int i = surface.skinning.first[v]; i < surface.skinning.first[v] + surface.skinning.count[v]; ++i)
	{
		const double w = surface.skinning.influences[i].weight;
		if (!std::isfinite(w) || w < 0)
			return false;
		sum += w;
	}
	return std::abs(sum - 1) <= .00001;
}
bool stroke(ModelMesh *mesh, const ModelWeightStroke &s, int bind, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	auto &surface = mesh->surfaces[s.surface];
	const int joint = modelJointIndex(mesh->skeleton, s.joint, Qt::CaseSensitive);
	const int remainder = modelJointIndex(mesh->skeleton, s.remainderJoint, Qt::CaseSensitive);
	QSet<int> locks;
	for (const auto &name : s.lockedJoints)
		locks.insert(modelJointIndex(mesh->skeleton, name, Qt::CaseSensitive));
	QVector<QSet<int>> neighbours;
	if (s.mode == ModelWeightPaintMode::Smooth)
	{
		neighbours.resize(surface.vertexCount);
		for (const auto &t : surface.triangles)
		{
			if (!work.step())
				return false;
			for (auto edge : {qMakePair(t.a, t.b), qMakePair(t.b, t.c), qMakePair(t.c, t.a)})
				if (edge.first != edge.second)
				{
					neighbours[edge.first].insert(edge.second);
					neighbours[edge.second].insert(edge.first);
				}
		}
	}
	QMap<int, double> coverage;
	for (const auto &sample : s.samples)
		if (sample.opacity > 0)
			coverage.insert(sample.vertex, sample.opacity);
	ModelSurfaceSkinning output;
	bool changed = false;
	for (int v = 0; v < surface.vertexCount; ++v)
	{
		if (!work.step())
			return false;
		output.first.append(output.influences.size());
		const int first = surface.skinning.first[v], end = first + surface.skinning.count[v];
		if (!coverage.contains(v))
		{
			for (int i = first; i < end; ++i)
				output.influences.append(surface.skinning.influences[i]);
		}
		else
		{
			if (!normalised(surface, v))
				return fail(error, Text::tr("Vertex %1 has negative or unnormalised weights. Repair its binding before painting.").arg(v));
			double locked = 0, others = 0, oldWeight = 0;
			bool normalOffsets = false;
			for (int i = first; i < end; ++i)
			{
				const auto &w = surface.skinning.influences[i];
				if (w.weight > 0 &&
					!near(modelJointTransformPoint(mesh->skeleton.joints[w.joint].bind, w.offset), surface.frames[bind].positions[v]))
					return fail(error, Text::tr("Vertex %1 has distinct imported bind offsets. Rebind it before painting.").arg(v));
				if (locks.contains(w.joint))
					locked += w.weight;
				else if (w.joint == joint)
					oldWeight += w.weight;
				else
					others += w.weight;
				if (w.weight > 0 && (w.normalOffset.x != 0 || w.normalOffset.y != 0 || w.normalOffset.z != 0))
					normalOffsets = true;
			}
			if (locked > 1)
				return fail(error, Text::tr("Locked weights exceed one at vertex %1.").arg(v));
			const double available = 1 - locked, alpha = s.strength * coverage[v];
			double wanted = s.value;
			if (s.mode == ModelWeightPaintMode::Add)
				wanted = oldWeight + s.value;
			else if (s.mode == ModelWeightPaintMode::Subtract)
				wanted = oldWeight - s.value;
			else if (s.mode == ModelWeightPaintMode::Smooth)
			{
				wanted = 0;
				// Sort for reproducible floating-point sums across processes.
				auto adjacent = neighbours[v].values();
				std::sort(adjacent.begin(), adjacent.end());
				for (int other : adjacent)
				{
					if (!work.step())
						return false;
					if (!normalised(surface, other))
						return fail(error, Text::tr("Smoothing touches an invalid neighbouring binding. Repair it first."));
					wanted += modelVertexJointWeight(surface, other, joint);
				}
				wanted = adjacent.isEmpty() ? oldWeight : wanted / adjacent.size();
			}
			// Clamp after blending: strength scales the requested add/subtract.
			const double weight = std::clamp(oldWeight + alpha * (wanted - oldWeight), 0., available);
			// A representable no-op must not renormalise float round-off or add
			// history. Explicit binding repair owns that operation.
			if (float(weight) == float(oldWeight))
			{
				for (int i = first; i < end; ++i)
					output.influences.append(surface.skinning.influences[i]);
				output.count.append(end - first);
				continue;
			}
			changed = true;
			const double rest = available - weight;
			if (rest > 1e-12 && others <= 0 && remainder < 0)
				return fail(error, Text::tr("Reducing the sole unlocked influence at vertex %1 needs an explicit remainder joint.").arg(v));
			for (int i = first; i < end; ++i)
			{
				auto w = surface.skinning.influences[i];
				if (locks.contains(w.joint))
					output.influences.append(w);
				else if (w.weight > 0)
				{
					w.weight = float(w.weight * (w.joint == joint ? weight / oldWeight : others > 0 ? rest / others : 0));
					if (w.weight > 0)
						output.influences.append(w);
				}
			}
			const auto add = [&](int j, double amount)
			{
				bool invertible = false;
				const auto inverse = modelJointInverse(mesh->skeleton.joints[j].bind, &invertible);
				if (!invertible || !modelJointMatrixIsFinite(inverse))
					return fail(error, Text::tr("A painted joint has a singular bind transform."));
				output.influences.append(
					{j, float(amount), modelJointTransformPoint(inverse, surface.frames[bind].positions[v]),
					 normalOffsets ? modelJointTransformVector(inverse, surface.frames[bind].normals[v]) : ModelVec3{}});
				return true;
			};
			if (oldWeight <= 0 && weight > 0 && !add(joint, weight))
				return false;
			if (others <= 0 && rest > 1e-12 && !add(remainder, rest))
				return false;
		}
		const int count = output.influences.size() - output.first.last();
		if (count < 1 || count > 64)
			return fail(error, Text::tr("Painting would exceed 64 influences at a vertex. Reduce its weights explicitly first."));
		output.count.append(count);
	}
	if (changed)
		surface.skinning = std::move(output);
	return work.check();
}
} // namespace
double modelVertexJointWeight(const ModelSurface &surface, int vertex, int joint)
{
	if (vertex < 0 || vertex >= surface.vertexCount || surface.skinning.isEmpty())
		return 0;
	double weight = 0;
	for (int i = surface.skinning.first[vertex]; i < surface.skinning.first[vertex] + surface.skinning.count[vertex]; ++i)
		if (surface.skinning.influences[i].joint == joint)
			weight += surface.skinning.influences[i].weight;
	return weight;
}
bool applyModelWeightPaint(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	ModelSelection context;
	context.surface = edit.selection.surface;
	if (edit.kind != ModelEditKind::PaintSkinWeights || edit.frame != -1 || edit.selection != context || edit.weightStrokes.isEmpty() ||
		edit.weightStrokes.size() > modelWeightMaxStrokes)
		return fail(error, Text::tr("Weight painting needs 1–1024 strokes with explicit vertex targets and no competing selection."));
	QSet<int> surfaces;
	qint64 samples = 0;
	for (const auto &s : edit.weightStrokes)
	{
		if (!work.check())
			return false;
		if (s.surface < 0 || s.surface >= mesh->surfaces.size() || mesh->surfaces[s.surface].skinning.isEmpty() || int(s.mode) < 0 ||
			int(s.mode) > int(ModelWeightPaintMode::Smooth) || !std::isfinite(s.value) || s.value < 0 || s.value > 1 ||
			!std::isfinite(s.strength) || s.strength <= 0 || s.strength > 1 || (s.mode == ModelWeightPaintMode::Smooth && s.value != 1) ||
			s.samples.isEmpty())
			return fail(error,
						Text::tr("Choose a bound surface, a brush mode, a value from zero to one and a positive strength up to one."));
		const int joint = modelJointIndex(mesh->skeleton, s.joint, Qt::CaseSensitive);
		QSet<QString> locks;
		for (const auto &name : s.lockedJoints)
		{
			if (name == s.joint || locks.contains(name) || modelJointIndex(mesh->skeleton, name, Qt::CaseSensitive) < 0)
				return fail(error, Text::tr("Locked joints must be distinct existing names and cannot include the painted joint."));
			locks.insert(name);
		}
		if (joint < 0 || (!s.remainderJoint.isEmpty() && (s.remainderJoint == s.joint || locks.contains(s.remainderJoint) ||
														  modelJointIndex(mesh->skeleton, s.remainderJoint, Qt::CaseSensitive) < 0)))
			return fail(error, Text::tr("Choose an existing painted joint and, if needed, a different unlocked remainder joint."));
		QSet<int> seen;
		bool covered = false;
		for (const auto &sample : s.samples)
		{
			if (!work.step())
				return false;
			if (++samples > modelWeightMaxSamples || sample.vertex < 0 || sample.vertex >= mesh->surfaces[s.surface].vertexCount ||
				seen.contains(sample.vertex) || !std::isfinite(sample.opacity) || sample.opacity < 0 || sample.opacity > 1)
				return fail(error, Text::tr("Stroke samples need distinct existing vertices, opacity from zero to one and at most "
											"1,048,576 samples in total."));
			seen.insert(sample.vertex);
			covered |= sample.opacity > 0;
		}
		if (!covered)
			return fail(error, Text::tr("The brush stroke has no covered vertices."));
		surfaces.insert(s.surface);
	}
	ModelSkinningWork skin;
	if (!skin.prepare(*mesh, error))
		return false;
	auto ordered = surfaces.values();
	std::sort(ordered.begin(), ordered.end());
	QMap<int, ModelSurfaceSkinning> original;
	for (int s : ordered)
	{
		if (!skin.validateSurface(*mesh, s, error, control))
			return false;
		original.insert(s, mesh->surfaces[s].skinning);
	}
	for (const auto &s : edit.weightStrokes)
		if (!stroke(mesh, s, skin.bindFrame, error, control))
			return false;
	bool changed = false;
	for (int s : ordered)
	{
		if (original[s].influences.constData() == mesh->surfaces[s].skinning.influences.constData())
			continue;
		changed = true;
		if (!skin.bakeSurface(mesh, s, error, control))
			return false;
	}
	if (changed)
		for (auto &clip : mesh->skeleton.clips)
		{
			clip.frameMins.clear();
			clip.frameMaxs.clear();
		}
	return work.check();
}
} // namespace vibestudio
