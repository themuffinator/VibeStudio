#include "core/model_skinning_tools.h"
#include "core/model_document.h"
#include "core/model_joint_tools.h"
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
	Q_DECLARE_TR_FUNCTIONS(ModelSkinningTools)
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
bool targets(const ModelMesh &mesh, const ModelSelection &selection, QMap<int, QSet<int>> *out, QString *error)
{
	if (selection.surface < 0 || selection.surface >= mesh.surfaces.size() || !selection.tag.isEmpty() || !selection.joint.isEmpty() ||
		!selection.collision.isEmpty() ||
		(!selection.surfaces.isEmpty() && (!selection.vertices.isEmpty() || !selection.faces.isEmpty() || !selection.edges.isEmpty())))
		return fail(error, Text::tr("Choose mesh components or whole surfaces for skinning."));
	QSet<int> surfaces = selection.surfaces;
	if (surfaces.isEmpty())
		surfaces.insert(selection.surface);
	for (int index : surfaces)
	{
		if (index < 0 || index >= mesh.surfaces.size())
			return fail(error, Text::tr("A selected skinning surface no longer exists."));
		const auto &surface = mesh.surfaces[index];
		QSet<int> vertices = selection.vertices;
		for (int face : selection.faces)
		{
			if (face < 0 || face >= surface.triangles.size())
				return fail(error, Text::tr("A selected skinning face no longer exists."));
			const auto &triangle = surface.triangles[face];
			vertices.unite({triangle.a, triangle.b, triangle.c});
		}
		const auto edges = selection.edges.isEmpty() ? QVector<QPair<int, int>>{} : modelSurfaceEdges(surface);
		for (auto edge : selection.edges)
		{
			if (!std::binary_search(edges.cbegin(), edges.cend(), edge))
				return fail(error, Text::tr("A selected skinning edge no longer exists."));
			vertices.unite({edge.first, edge.second});
		}
		if (vertices.isEmpty())
			for (int v = 0; v < surface.vertexCount; ++v)
				vertices.insert(v);
		for (int vertex : vertices)
			if (vertex < 0 || vertex >= surface.vertexCount)
				return fail(error, Text::tr("A selected skinning vertex no longer exists."));
		out->insert(index, std::move(vertices));
	}
	return true;
}
double distanceSquared(ModelVec3 p, ModelVec3 a, ModelVec3 b)
{
	const double dx = double(b.x) - a.x, dy = double(b.y) - a.y, dz = double(b.z) - a.z;
	const double length = dx * dx + dy * dy + dz * dz;
	const double t =
		length > 0 ? std::clamp(((double(p.x) - a.x) * dx + (double(p.y) - a.y) * dy + (double(p.z) - a.z) * dz) / length, 0., 1.) : 0.;
	const double x = p.x - (a.x + t * dx), y = p.y - (a.y + t * dy), z = p.z - (a.z + t * dz);
	return x * x + y * y + z * z;
}
QMap<int, double> weights(const ModelSurfaceSkinning &skinning, int vertex)
{
	QMap<int, double> result;
	for (int i = skinning.first[vertex]; i < skinning.first[vertex] + skinning.count[vertex]; ++i)
		result[skinning.influences[i].joint] += skinning.influences[i].weight;
	return result;
}
bool canonical(const ModelMesh &mesh, const ModelSurface &surface, int vertex, int bind)
{
	if (bind < 0 || surface.skinning.isEmpty())
		return false;
	for (int i = surface.skinning.first[vertex]; i < surface.skinning.first[vertex] + surface.skinning.count[vertex]; ++i)
	{
		const auto &influence = surface.skinning.influences[i];
		if (influence.weight > 0 && !near(modelJointTransformPoint(mesh.skeleton.joints[influence.joint].bind, influence.offset),
										  surface.frames[bind].positions[vertex]))
			return false;
	}
	return true;
}
} // namespace

bool inspectModelSkinning(const ModelMesh &mesh, const ModelSelection &selection, int influenceLimit, ModelSkinReport *report,
						  QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	if (!work.check())
		return false;
	if (!report || influenceLimit < 1 || influenceLimit > 64)
		return fail(error, Text::tr("Choose an influence limit from 1 to 64."));
	const auto errors = validateEditableModel(mesh, control);
	if (!errors.isEmpty())
		return fail(error, errors.join(QLatin1Char('\n')));
	QMap<int, QSet<int>> selected;
	if (!targets(mesh, selection, &selected, error))
		return false;
	ModelSkinReport candidate;
	candidate.source = mesh;
	const int bind = modelBindPoseFrame(mesh);
	candidate.hasBindFrame = bind >= 0;
	for (auto it = selected.cbegin(); it != selected.cend(); ++it)
	{
		const auto &surface = mesh.surfaces[it.key()];
		auto indices = it.value().values();
		std::sort(indices.begin(), indices.end());
		for (int vertex : indices)
		{
			if (!work.step())
				return false;
			ModelSkinVertex item;
			item.surface = it.key();
			item.vertex = vertex;
			if (!surface.skinning.isEmpty())
			{
				QSet<int> seen;
				for (int i = surface.skinning.first[vertex]; i < surface.skinning.first[vertex] + surface.skinning.count[vertex]; ++i)
				{
					const auto &influence = surface.skinning.influences[i];
					item.negative |= influence.weight < 0;
					item.duplicate |= seen.contains(influence.joint);
					seen.insert(influence.joint);
					item.sum += influence.weight;
				}
				const auto combined = weights(surface.skinning, vertex);
				for (auto w = combined.cbegin(); w != combined.cend(); ++w)
				{
					item.weights.append({w.key(), w.value()});
					item.positiveInfluences += w.value() > 0;
				}
				item.noncanonical = bind >= 0 && !canonical(mesh, surface, vertex, bind);
			}
			item.unbound = item.positiveInfluences == 0;
			item.unnormalised = std::abs(item.sum - 1) > .00001;
			item.overLimit = item.positiveInfluences > influenceLimit;
			candidate.unbound += item.unbound;
			candidate.negative += item.negative;
			candidate.unnormalised += item.unnormalised;
			candidate.duplicate += item.duplicate;
			candidate.noncanonical += item.noncanonical;
			candidate.overLimit += item.overLimit;
			candidate.vertices.append(std::move(item));
		}
	}
	if (!work.check())
		return false;
	*report = std::move(candidate);
	return true;
}
bool selectModelSkinVertices(const ModelMesh &mesh, const ModelSkinReport &report, int surface, ModelSkinFilter filter,
							 const QString &joint, double minimum, double maximum, ModelSelection *selection, QString *error)
{
	if (!selection || surface < 0 || surface >= mesh.surfaces.size() || int(filter) < 0 || int(filter) > int(ModelSkinFilter::OverLimit) ||
		!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0 || maximum > 1 || maximum < minimum)
		return fail(error, Text::tr("Choose a surface and a weight range from zero to one."));
	const int index = modelJointIndex(mesh.skeleton, joint, Qt::CaseSensitive);
	if (report.source.surfaces.constData() != mesh.surfaces.constData() || report.source.frames.constData() != mesh.frames.constData() ||
		report.source.skeleton.joints.constData() != mesh.skeleton.joints.constData())
		return fail(error, Text::tr("The model changed after skinning inspection. Inspect it again before selecting vertices."));
	if (filter == ModelSkinFilter::Weighted && index < 0)
		return fail(error, Text::tr("Choose an existing joint to select its influenced vertices."));
	ModelSelection result;
	result.surface = surface;
	for (const auto &vertex : report.vertices)
	{
		if (vertex.surface != surface)
			continue;
		if (vertex.vertex < 0 || vertex.vertex >= mesh.surfaces[surface].vertexCount)
			return fail(error, Text::tr("The skinning report no longer matches this surface."));
		double weight = 0;
		for (const auto &value : vertex.weights)
			if (value.joint == index)
				weight += value.weight;
		const bool matches = filter == ModelSkinFilter::Weighted	   ? weight >= minimum && weight <= maximum
							 : filter == ModelSkinFilter::Unbound	   ? vertex.unbound
							 : filter == ModelSkinFilter::Unnormalised ? vertex.unnormalised
							 : filter == ModelSkinFilter::Negative	   ? vertex.negative
							 : filter == ModelSkinFilter::Duplicate	   ? vertex.duplicate
							 : filter == ModelSkinFilter::Noncanonical ? vertex.noncanonical
																	   : vertex.overLimit;
		if (matches)
			result.vertices.insert(vertex.vertex);
	}
	*selection = std::move(result);
	return true;
}
bool modelSkinBindingWeights(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &bindPose, const QVector<int> &joints,
	ModelSkinBindingMethod method, int maxInfluences, double falloff, ModelVec3 position, QMap<int, double> *weights, ModelWorkProgress *work)
{
	weights->clear();
	if (joints.isEmpty())
		return true;
	if (method == ModelSkinBindingMethod::Rigid)
	{
		weights->insert(joints[0], 1);
		return true;
	}
	QVector<QPair<double, int>> distances;
	for (int j : joints)
	{
		if (work && !work->step())
			return false;
		const auto origin = modelJointTranslation(bindPose[j]);
		const int parent = skeleton.joints[j].parent;
		const auto start = method == ModelSkinBindingMethod::NearestBones && parent >= 0 ? modelJointTranslation(bindPose[parent]) : origin;
		distances.append({distanceSquared(position, start, origin), j});
	}
	const int keep = std::min(maxInfluences, int(distances.size()));
	std::partial_sort(distances.begin(), distances.begin() + keep, distances.end());
	const double closest = std::max(1e-12, distances[0].first);
	for (int i = 0; i < keep; ++i)
	{
		// Exact hits share their weight; positive distances use a ratio so very
		// small/large coordinates cannot overflow.
		if (distances[0].first <= 1e-12 && distances[i].first > 1e-12)
			break;
		weights->insert(distances[i].second, std::pow(closest / std::max(1e-12, distances[i].first), falloff * .5));
	}
	return true;
}
bool isModelSkinEdit(ModelEditKind kind)
{
	return kind == ModelEditKind::BindSkin || kind == ModelEditKind::RebindSkin || kind == ModelEditKind::UnbindSkin ||
		   kind == ModelEditKind::PruneSkinWeights;
}
bool applyModelSkinEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	const auto &o = edit.skin;
	const bool bind = edit.kind == ModelEditKind::BindSkin, rebind = edit.kind == ModelEditKind::RebindSkin,
			   unbind = edit.kind == ModelEditKind::UnbindSkin, prune = edit.kind == ModelEditKind::PruneSkinWeights;
	if (!isModelSkinEdit(edit.kind) || edit.frame != -1 || int(o.method) < 0 || int(o.method) > int(ModelSkinBindingMethod::NearestBones) ||
		o.maxInfluences < 1 || o.maxInfluences > 64 || !std::isfinite(o.falloff) || o.falloff < .1 || o.falloff > 16 ||
		!std::isfinite(o.minimumWeight) || o.minimumWeight < 0 || o.minimumWeight > 1 ||
		(!bind && (o.method != ModelSkinBindingMethod::NearestBones || !o.joints.isEmpty() || o.replaceExisting || o.falloff != 2)) ||
		(!prune && o.minimumWeight != .001) || ((!bind && !prune) && o.maxInfluences != 4) ||
		(bind && o.method == ModelSkinBindingMethod::Rigid && (o.maxInfluences != 4 || o.falloff != 2)))
		return fail(error, Text::tr("The skinning options do not apply to this operation or are outside their limits."));
	if (!validateModelSkeleton(*mesh, error))
		return false;
	QMap<int, QSet<int>> selected;
	if (!targets(*mesh, edit.selection, &selected, error))
		return false;
	if (unbind)
	{
		for (auto it = selected.cbegin(); it != selected.cend(); ++it)
		{
			if (!work.check())
				return false;
			if (it.value().size() != mesh->surfaces[it.key()].vertexCount)
				return fail(error, Text::tr("Unbinding requires complete surfaces; every baked frame is retained."));
			mesh->surfaces[it.key()].skinning = {};
		}
		return work.check();
	}
	ModelSkinningWork skin;
	if (!skin.prepare(*mesh, error)) return false;
	const int bindFrame = skin.bindFrame;
	const auto &bindPose = skin.bindPose;
	QVector<int> joints;
	for (const auto &name : o.joints)
	{
		const int joint = modelJointIndex(mesh->skeleton, name, Qt::CaseSensitive);
		if (joint < 0 || joints.contains(joint))
			return fail(error, Text::tr("Binding joints must be distinct existing names."));
		joints.append(joint);
	}
	if (bind && o.method == ModelSkinBindingMethod::Rigid && joints.size() != 1)
		return fail(error, Text::tr("Rigid binding requires exactly one joint."));
	if (joints.isEmpty())
		for (int j = 0; j < bindPose.size(); ++j)
			joints.append(j);
	std::sort(joints.begin(), joints.end());
	QVector<ModelJointMatrix> inverses(bindPose.size());
	QVector<bool> invertible(bindPose.size(), false);
	for (int j = 0; j < bindPose.size(); ++j)
	{
		bool ok = false;
		inverses[j] = modelJointInverse(bindPose[j], &ok);
		invertible[j] = ok && modelJointMatrixIsFinite(inverses[j]);
	}
	for (auto it = selected.cbegin(); it != selected.cend(); ++it)
	{
		auto &surface = mesh->surfaces[it.key()];
		const bool skinned = !surface.skinning.isEmpty();
		if (bind && skinned && !o.replaceExisting)
			return fail(error, Text::tr("Surface %1 is already skinned. Enable replacement to assign new weights.").arg(surface.name));
		if ((rebind || prune) && !skinned)
			return fail(error, Text::tr("Surface %1 has no weights to repair. Bind it first.").arg(surface.name));
		if (!skinned && it.value().size() != surface.vertexCount)
			return fail(error, Text::tr("First binding must cover a complete surface."));
		const auto geometry = surface.frames[bindFrame];
		if (!skin.validateSurface(*mesh, it.key(), error, control)) return false;
		ModelSurfaceSkinning skinning;
		for (int v = 0; v < surface.vertexCount; ++v)
		{
			if (!work.step())
				return false;
			skinning.first.append(skinning.influences.size());
			if (!it.value().contains(v))
			{
				for (int i = surface.skinning.first[v]; i < surface.skinning.first[v] + surface.skinning.count[v]; ++i)
					skinning.influences.append(surface.skinning.influences[i]);
			}
			else
			{
				QMap<int, double> assigned;
				if (bind)
				{
					if (!modelSkinBindingWeights(mesh->skeleton, bindPose, joints, o.method, o.maxInfluences, o.falloff, geometry.positions[v],
							&assigned, &work))
						return false;
				}
				else
				{
					for (int i = surface.skinning.first[v]; i < surface.skinning.first[v] + surface.skinning.count[v]; ++i)
						if (surface.skinning.influences[i].weight < 0)
							return fail(error, Text::tr("Negative weights need explicit replacement binding."));
					assigned = weights(surface.skinning, v);
					if (prune)
					{
						if (!canonical(*mesh, surface, v, bindFrame))
							return fail(error,
										Text::tr("Distinct imported bind offsets require explicit rebinding before reducing weights."));
						QVector<QPair<double, int>> ranked;
						double total = 0;
						for (auto w = assigned.cbegin(); w != assigned.cend(); ++w)
							if (w.value() > 0)
							{
								total += w.value();
								ranked.append({-w.value(), w.key()});
							}
						std::sort(ranked.begin(), ranked.end());
						assigned.clear();
						for (int i = 0; i < std::min(o.maxInfluences, int(ranked.size())); ++i)
							if (i == 0 || -ranked[i].first / total >= o.minimumWeight)
								assigned[ranked[i].second] = -ranked[i].first;
					}
				}
				double total = 0;
				for (double w : assigned)
					if (w > 0)
						total += w;
				if (!(total > 0) || !std::isfinite(total))
					return fail(error, Text::tr("An unweighted vertex needs a binding joint."));
				for (auto w = assigned.cbegin(); w != assigned.cend(); ++w)
				{
					if (!(w.value() > 0))
						continue;
					if (!invertible[w.key()])
						return fail(error, Text::tr("Joint %1 has a singular bind transform.").arg(mesh->skeleton.joints[w.key()].name));
					const float weight = float(w.value() / total);
					if (weight > 0)
					{
						ModelJointInfluence influence{w.key(), weight, modelJointTransformPoint(inverses[w.key()], geometry.positions[v]),
													  modelJointTransformVector(inverses[w.key()], geometry.normals[v])};
						if (prune)
						{
							// Keep the imported normal policy, including zero offsets
							// that request geometric normals. Merge duplicate entries
							// linearly within a joint without changing its deformation.
							double x = 0, y = 0, z = 0, nx = 0, ny = 0, nz = 0;
							for (int i = surface.skinning.first[v]; i < surface.skinning.first[v] + surface.skinning.count[v]; ++i)
							{
								const auto &old = surface.skinning.influences[i];
								if (old.joint != w.key() || old.weight <= 0)
									continue;
								const double ratio = old.weight / w.value();
								x += old.offset.x * ratio;
								y += old.offset.y * ratio;
								z += old.offset.z * ratio;
								nx += old.normalOffset.x * ratio;
								ny += old.normalOffset.y * ratio;
								nz += old.normalOffset.z * ratio;
							}
							influence.offset = {float(x), float(y), float(z)};
							influence.normalOffset = {float(nx), float(ny), float(nz)};
						}
						skinning.influences.append(influence);
					}
				}
			}
			skinning.count.append(skinning.influences.size() - skinning.first.last());
		}
		surface.skinning = std::move(skinning);
		if (!skin.bakeSurface(mesh, it.key(), error, control)) return false;
	}
	for (auto &clip : mesh->skeleton.clips)
	{
		clip.frameMins.clear();
		clip.frameMaxs.clear();
	}
	return work.check();
}
} // namespace vibestudio
