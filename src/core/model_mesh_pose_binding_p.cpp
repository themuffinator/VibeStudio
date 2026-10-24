#include "core/model_mesh_pose_binding_p.h"
#include "core/model_skeleton.h"
#include <QCoreApplication>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelMeshPoseBinding)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
} // namespace
bool ModelMeshPoseBinding::prepare(const ModelMesh &mesh, int reference, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!work.check())
		return false;
	const auto &skeleton = mesh.skeleton;
	const auto invalid = [&]
	{
		return fail(
			error,
			Text::tr("Topology editing needs complete, contiguous retained skeletal clip ranges. Repair ambiguous frame mappings first."));
	};
	if (skeleton.isEmpty() || skeleton.bakedClipForFrame.size() != mesh.frames.size() || reference < 0 || reference >= mesh.frames.size())
		return invalid();
	if (skeleton.bakedClipForFrame[reference] < -1)
		return fail(error, Text::tr("Edit skinned topology from its bind pose or a retained skeletal pose."));
	referenceFrame = reference;
	bindPose = modelSkeletonBindPose(skeleton);
	localFrames.fill(-1, mesh.frames.size());
	QVector<int> counts(skeleton.clips.size(), 0);
	int previous = -3;
	for (int frame = 0; frame < mesh.frames.size(); ++frame)
	{
		if (!work.step())
			return false;
		const int clip = skeleton.bakedClipForFrame[frame];
		if (clip < -2 || clip >= counts.size())
			return invalid();
		if (clip >= 0)
		{
			if ((previous != clip && counts[clip] != 0) || counts[clip] >= skeleton.clips[clip].frames.size())
				return invalid();
			localFrames[frame] = counts[clip]++;
		}
		previous = clip;
	}
	for (int clip = 0; clip < counts.size(); ++clip)
	{
		if (!work.step())
			return false;
		if (counts[clip] != 0 && counts[clip] != skeleton.clips[clip].frames.size())
			return invalid();
	}
	inverseReference.clear();
	for (const auto &matrix : pose(mesh, reference))
	{
		if (!work.step())
			return false;
		bool valid = false;
		inverseReference.append(modelJointInverse(matrix, &valid));
		if (!valid)
			return fail(error, Text::tr("The reference skeleton pose has a singular transform."));
	}
	return work.check();
}
const QVector<ModelJointMatrix> &ModelMeshPoseBinding::pose(const ModelMesh &mesh, int frame) const
{
	const int clip = mesh.skeleton.bakedClipForFrame[frame];
	return clip < 0 ? bindPose : mesh.skeleton.clips[clip].frames[localFrames[frame]];
}
} // namespace vibestudio
