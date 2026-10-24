#include "core/model_skinning_work.h"
#include "core/model_joint_tools.h"
#include "core/model_skeleton.h"
#include <QCoreApplication>
#include <cmath>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkinningTools)
};
bool fail(QString *error, const QString &text)
{
	if (error)
		*error = text;
	return false;
}
bool same(const ModelFrameGeometry &a, const ModelFrameGeometry &b)
{
	if (a.positions.size() != b.positions.size() || a.normals.size() != b.normals.size())
		return false;
	const auto near = [](ModelVec3 p, ModelVec3 q)
	{ return std::abs(double(p.x) - q.x) <= .0001 && std::abs(double(p.y) - q.y) <= .0001 && std::abs(double(p.z) - q.z) <= .0001; };
	for (int v = 0; v < a.positions.size(); ++v)
		if (!near(a.positions[v], b.positions[v]) || !near(a.normals[v], b.normals[v]))
			return false;
	return true;
}
} // namespace
bool ModelSkinningWork::prepare(const ModelMesh &mesh, QString *error)
{
	bindFrame = modelBindPoseFrame(mesh);
	if (bindFrame < 0 || mesh.skeleton.isEmpty())
		return fail(error, Text::tr("Skin binding needs joints and a retained bind frame."));
	ModelJointFrame first;
	if (!modelJointFrame(mesh, 0, &first, error))
		return false;
	localFrames.fill(0, mesh.frames.size());
	QVector<int> counts(mesh.skeleton.clips.size(), 0);
	for (int f = 0; f < localFrames.size(); ++f)
		if (const int clip = mesh.skeleton.bakedClipForFrame[f]; clip >= 0)
			localFrames[f] = counts[clip]++;
	bindPose = modelSkeletonBindPose(mesh.skeleton);
	return true;
}
const QVector<ModelJointMatrix> &ModelSkinningWork::pose(const ModelMesh &mesh, int frame) const
{
	const int clip = mesh.skeleton.bakedClipForFrame[frame];
	return clip < 0 ? bindPose : mesh.skeleton.clips[clip].frames[localFrames[frame]];
}
bool ModelSkinningWork::validateSurface(const ModelMesh &mesh, int index, QString *error, const ModelWorkControl &control) const
{
	const auto &surface = mesh.surfaces[index];
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	for (int f = 0; f < surface.frames.size(); ++f)
	{
		if (!work.check())
			return false;
		ModelFrameGeometry expected = surface.frames[bindFrame];
		if (!surface.skinning.isEmpty() && !skinModelSurface(surface, pose(mesh, f), &expected, error))
			return false;
		if (!same(expected, surface.frames[f]))
			return fail(error,
						Text::tr("Surface %1 has independent geometry or normals. Skinning would replace those edits.").arg(surface.name));
	}
	return work.check();
}
bool ModelSkinningWork::bakeSurface(ModelMesh *mesh, int index, QString *error, const ModelWorkControl &control) const
{
	auto &surface = mesh->surfaces[index];
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	for (int f = 0; f < surface.frames.size(); ++f)
	{
		if (!work.check())
			return false;
		ModelFrameGeometry updated;
		if (!skinModelSurface(surface, pose(*mesh, f), &updated, error))
			return false;
		if (f == bindFrame)
		{
			if (!same(surface.frames[f], updated))
				return fail(error, Text::tr("The binding cannot preserve this surface's bind geometry and normals."));
		}
		else
			surface.frames[f] = std::move(updated);
	}
	return work.check();
}
} // namespace vibestudio
