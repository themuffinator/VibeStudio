#include "core/model_skeletal_edit.h"
#include "core/model_collision.h"
#include "core/model_document.h"
#include "core/model_joint_tools.h"
#include "core/model_pose.h"
#include "core/model_skeleton.h"
#include "core/model_tags.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelSkeletalEdit)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool same(ModelVec3 a, ModelVec3 b, double tolerance = 0.0001)
{
	return std::abs(double(a.x) - b.x) <= tolerance && std::abs(double(a.y) - b.y) <= tolerance && std::abs(double(a.z) - b.z) <= tolerance;
}
bool same(const ModelFrameGeometry &a, const ModelFrameGeometry &b, double tolerance = 0.0001)
{
	if (a.positions.size() != b.positions.size() || a.normals.size() != b.normals.size())
		return false;
	for (int i = 0; i < a.positions.size(); ++i)
		if (!same(a.positions[i], b.positions[i], tolerance) || !same(a.normals[i], b.normals[i], tolerance))
			return false;
	return true;
}
struct Frame
{
	int clip = -1, local = 0;
	// original identifies surviving frames; inherited supplies auxiliary data
	// for a new sample without claiming that it is the original frame.
	int original = -1, inherited = 0;
};
bool layout(const ModelMesh &mesh, QVector<Frame> *frames, QVector<QVector<Frame>> *clips, QString *error)
{
	const auto &skeleton = mesh.skeleton;
	if (skeleton.isEmpty() || skeleton.bakedClipForFrame.size() != mesh.frames.size())
		return fail(error, Text::tr("Skeletal authoring needs joints and retained skeletal frame ranges."));
	clips->resize(skeleton.clips.size());
	int previous = -1;
	for (int i = 0; i < mesh.frames.size(); ++i)
	{
		const int clip = skeleton.bakedClipForFrame[i];
		if ((clip == -1 && i != 0) || clip < -1 || clip >= clips->size() || clip < previous)
			return fail(error, Text::tr("Skeletal authoring cannot replace independently added or rearranged frames."));
		Frame item{clip, clip < 0 ? 0 : int((*clips)[clip].size()), i, i};
		frames->append(item);
		if (clip >= 0)
			(*clips)[clip].append(item);
		previous = clip;
	}
	for (int i = 0; i < clips->size(); ++i)
		if (!(*clips)[i].isEmpty() && (*clips)[i].size() != skeleton.clips[i].frames.size())
			return fail(error, Text::tr("Skeletal authoring needs complete retained clip ranges."));
	return true;
}
const QVector<ModelJointMatrix> &pose(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &bind, const Frame &frame)
{
	return frame.clip < 0 ? bind : skeleton.clips[frame.clip].frames[frame.local];
}
ModelTag tagPose(const ModelSkeletalTag &source, const QVector<ModelJointMatrix> &matrices, int frame)
{
	const auto matrix = modelJointMultiply(matrices[source.joint], source.offset);
	ModelTag result;
	result.name = source.name;
	result.frameIndex = frame;
	result.origin = modelJointTranslation(matrix);
	for (int axis = 0; axis < 3; ++axis)
	{
		const double length = std::sqrt(double(matrix.m[axis]) * matrix.m[axis] + double(matrix.m[4 + axis]) * matrix.m[4 + axis] +
										double(matrix.m[8 + axis]) * matrix.m[8 + axis]);
		for (int row = 0; row < 3; ++row)
			result.axis[3 * axis + row] = length > 0 ? float(matrix.m[row * 4 + axis] / length) : 0;
	}
	return result;
}
bool same(const ModelTag &a, const ModelTag &b, double tolerance = 0.0001)
{
	if (!same(a.origin, b.origin, tolerance))
		return false;
	for (int i = 0; i < 9; ++i)
		if (std::abs(a.axis[i] - b.axis[i]) > tolerance)
			return false;
	return true;
}
bool validName(const ModelMesh &mesh, const QString &name, int except, QString *error)
{
	static const QRegularExpression pattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,62}$"));
	if (!pattern.match(name).hasMatch())
		return fail(
			error,
			Text::tr("Use a clip name of 1–63 ASCII letters, digits, underscores, dots or hyphens, starting with a letter or underscore."));
	for (int i = 0; i < mesh.skeleton.clips.size(); ++i)
		if (i != except && mesh.skeleton.clips[i].name.compare(name, Qt::CaseInsensitive) == 0)
			return fail(error, Text::tr("That skeletal clip name is already used."));
	return true;
}
bool transform(const ModelJointMatrix &before, const ModelSkeletalEditOptions &options, ModelJointMatrix *after, QString *error)
{
	for (float value :
		 {options.translation.x, options.translation.y, options.translation.z, options.rotation.x, options.rotation.y, options.rotation.z})
		if (!std::isfinite(value) || std::abs(double(value)) > 1000000)
			return fail(error, Text::tr("Joint transforms require finite coordinates within one million units or degrees."));
	for (float value : {options.scale.x, options.scale.y, options.scale.z})
		if (!std::isfinite(value) || value < 0.000001f || value > 1000)
			return fail(error, Text::tr("Joint scale factors must be from 0.000001 to 1000."));
	const double radians = std::numbers::pi / 360.0;
	const auto axis = [radians](float degrees, int index) {
		ModelQuat result{0, 0, 0, float(std::cos(degrees * radians))};
		if (index == 0)
			result.x = float(std::sin(degrees * radians));
		if (index == 1)
			result.y = float(std::sin(degrees * radians));
		if (index == 2)
			result.z = float(std::sin(degrees * radians));
		return result;
	};
	const auto rotation =
		modelQuatMultiply(axis(options.rotation.z, 2), modelQuatMultiply(axis(options.rotation.y, 1), axis(options.rotation.x, 0)));
	const auto delta = modelJointMatrix(rotation, options.translation, options.scale);
	if (options.local)
		*after = modelJointMultiply(before, delta);
	else
	{
		auto linear = before;
		linear.m[3] = linear.m[7] = linear.m[11] = 0;
		*after = modelJointMultiply(delta, linear);
		after->m[3] += before.m[3];
		after->m[7] += before.m[7];
		after->m[11] += before.m[11];
	}
	return true;
}
bool transformPose(QVector<ModelJointMatrix> *matrices, const ModelSkeleton &skeleton, int joint, const ModelSkeletalEditOptions &options,
				   QString *error)
{
	if (options.matrixTransform)
		return applyModelJointDelta(matrices, skeleton, joint, options.delta, options.descendants, error);
	const auto old = (*matrices)[joint];
	ModelJointMatrix updated;
	if (!transform(old, options, &updated, error))
		return false;
	bool invertible = false;
	const auto inverse = modelJointInverse(old, &invertible);
	if (!invertible)
		return fail(error, Text::tr("The selected joint has a singular transform."));
	const auto delta = modelJointMultiply(updated, inverse);
	QVector<bool> affected(skeleton.joints.size(), false);
	affected[joint] = true;
	for (int i = joint + 1; options.descendants && i < affected.size(); ++i)
		if (skeleton.joints[i].parent >= 0)
			affected[i] = affected[skeleton.joints[i].parent];
	for (int i = 0; i < affected.size(); ++i)
		if (affected[i])
			(*matrices)[i] = modelJointMultiply(delta, (*matrices)[i]);
	return true;
}
bool localPose(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &world, QVector<ModelJointMatrix> *local, QString *error)
{
	for (int i = 0; i < world.size(); ++i)
	{
		bool ok = true;
		const int parent = skeleton.joints[i].parent;
		local->append(parent < 0 ? world[i] : modelJointMultiply(modelJointInverse(world[parent], &ok), world[i]));
		if (!ok)
			return fail(error, Text::tr("A parent pose is singular and cannot be interpolated."));
		const auto &value = local->last();
		const auto rebuilt = modelJointMatrix(modelJointRotation(value), modelJointTranslation(value), modelJointScale(value));
		if (modelJointMatrixDistance(value, rebuilt) > 0.0001)
			return fail(error, Text::tr("Pose interpolation cannot discard shear or reflected scale. Use compatible endpoint poses."));
	}
	return true;
}
// All original frame references are remapped once. Additional clips can be
// retained outside the editable frame budget; edits never silently drop them.
bool rebuild(ModelMesh *mesh, const ModelMesh &before, const QVector<Frame> &oldPlan, const QVector<QVector<Frame>> &oldClips,
			 const QVector<Frame> &plan, const QVector<int> &oldClipForNew, bool preserveDeformation, QString *error,
			 ModelWorkProgress &work)
{
	qint64 vertices = 0;
	for (const auto &surface : mesh->surfaces)
		vertices += surface.vertexCount;
	if (plan.isEmpty() || plan.size() > modelDocumentMaxFrames || vertices * plan.size() > modelDocumentMaxFrameVertices)
		return fail(error, Text::tr("The authored clips exceed 1024 editable frames or 1,048,576 frame vertices. Take clips out of the "
									"skeletal working set to make room; they stay in the skeleton."));
	const auto oldBind = modelSkeletonBindPose(before.skeleton), newBind = modelSkeletonBindPose(mesh->skeleton);
	QVector<int> remap(before.frames.size(), -1), clipFirst(mesh->skeleton.clips.size(), -1), clipCount(mesh->skeleton.clips.size(), 0);
	mesh->frames.clear();
	mesh->tags.clear();
	mesh->skeleton.bakedClipForFrame.clear();
	for (auto &surface : mesh->surfaces)
		surface.frames.clear();
	QHash<QString, int> skeletalTags;
	for (int i = 0; i < before.skeleton.tags.size(); ++i)
		skeletalTags.insert(before.skeleton.tags[i].name, i);
	for (int i = 0; i < plan.size(); ++i)
	{
		if (!work.check())
			return false;
		const auto &item = plan[i];
		if (item.original >= 0)
			remap[item.original] = i;
		if (item.clip >= 0)
		{
			if (clipFirst[item.clip] < 0)
				clipFirst[item.clip] = i;
			++clipCount[item.clip];
		}
		const auto &oldPose = pose(before.skeleton, oldBind, oldPlan[item.inherited]);
		const auto &newPose = pose(mesh->skeleton, newBind, item);
		auto info = before.frames[item.inherited];
		info.index = i;
		if (item.original < 0)
			info.name = item.clip < 0 ? QStringLiteral("bindpose") : modelBakedFrameName(mesh->skeleton.clips[item.clip].name, item.local);
		mesh->frames.append(info);
		mesh->skeleton.bakedClipForFrame.append(item.clip);
		for (int surface = 0; surface < mesh->surfaces.size(); ++surface)
		{
			if (!work.step())
				return false;
			const auto &oldSurface = before.surfaces[surface];
			auto &newSurface = mesh->surfaces[surface];
			const auto &original = oldSurface.frames[item.inherited];
			if (newSurface.skinning.isEmpty())
			{
				newSurface.frames.append(original);
				continue;
			}
			ModelFrameGeometry expected, updated;
			if (!skinModelSurface(oldSurface, oldPose, &expected, error) || !skinModelSurface(newSurface, newPose, &updated, error))
				return false;
			if (preserveDeformation && !same(expected, updated))
				return fail(error, Text::tr("The bind transform cannot preserve skin deformation within the editing precision."));
			if (preserveDeformation || same(expected, updated, 0))
				newSurface.frames.append(original);
			else
			{
				if (!same(expected, original))
					return fail(
						error,
						Text::tr("Surface %1 has independent geometry or normal edits in frame %2. This pose edit would overwrite them.")
							.arg(oldSurface.name)
							.arg(item.inherited));
				newSurface.frames.append(std::move(updated));
			}
		}
		for (const auto &original : before.tags)
		{
			if (!work.step())
				return false;
			if (original.frameIndex != item.inherited)
				continue;
			auto tag = original;
			const int index = skeletalTags.value(tag.name, -1);
			if (index >= 0)
			{
				const auto expected = tagPose(before.skeleton.tags[index], oldPose, i);
				const auto updated = tagPose(mesh->skeleton.tags[index], newPose, i);
				if (preserveDeformation && !same(expected, updated))
					return fail(error, Text::tr("The bind transform cannot preserve attachment poses within the editing precision."));
				if (!preserveDeformation && !same(expected, updated, 0))
				{
					if (!same(expected, original))
						return fail(error,
									Text::tr("Attachment %1 has an independent pose edit in frame %2. This joint edit would overwrite it.")
										.arg(tag.name)
										.arg(item.inherited));
					tag = updated;
				}
			}
			tag.frameIndex = i;
			mesh->tags.append(tag);
		}
	}
	for (int i = 0; i < mesh->collisionBoxes.size(); ++i)
	{
		if (before.collisionBoxes[i].framePoses.isEmpty())
			continue;
		QVector<ModelCollisionPose> frames;
		for (const auto &item : plan)
			frames.append(before.collisionBoxes[i].framePoses[item.inherited]);
		setModelCollisionFrames(&mesh->collisionBoxes[i], std::move(frames));
	}
	mesh->animations.clear();
	QSet<int> linked;
	for (const auto &animation : before.animations)
	{
		int oldClip = -1;
		for (int i = 0; i < oldClips.size(); ++i)
			if (!oldClips[i].isEmpty() && animation.name == before.skeleton.clips[i].name &&
				animation.firstFrame == oldClips[i].first().original && animation.frameCount == oldClips[i].size())
			{
				oldClip = i;
				break;
			}
		if (oldClip >= 0)
		{
			const int newClip = oldClipForNew.indexOf(oldClip);
			if (newClip >= 0 && clipFirst[newClip] >= 0)
			{
				const auto &clip = mesh->skeleton.clips[newClip];
				mesh->animations.append({clip.name, clipFirst[newClip], clipCount[newClip], clip.framesPerSecond});
				linked.insert(newClip);
			}
			continue;
		}
		int first = -1, last = -1;
		for (int frame = animation.firstFrame; frame < animation.firstFrame + animation.frameCount; ++frame)
			if (remap[frame] >= 0)
			{
				if (first < 0)
					first = remap[frame];
				last = remap[frame];
			}
		if (first < 0)
			return fail(error,
						Text::tr("Deleting these frames would empty the custom animation range %1. Remove that range explicitly first.")
							.arg(animation.name));
		mesh->animations.append({animation.name, first, last - first + 1, animation.framesPerSecond});
	}
	for (int i = 0; i < clipFirst.size(); ++i)
		if (clipFirst[i] >= 0 && !linked.contains(i))
			mesh->animations.append({mesh->skeleton.clips[i].name, clipFirst[i], clipCount[i], mesh->skeleton.clips[i].framesPerSecond});
	if (mesh->mdl.enabled)
	{
		QVector<int> groupForFrame(before.frames.size(), -1);
		QVector<float> duration(before.frames.size(), 0);
		for (int group = 0; group < before.mdl.frameGroups.size(); ++group)
		{
			const auto &value = before.mdl.frameGroups[group];
			for (int n = 0; n < value.frameCount(); ++n)
			{
				groupForFrame[value.firstFrame + n] = group;
				if (!value.intervals.isEmpty())
					duration[value.firstFrame + n] = value.intervals[n] - (n == 0 ? 0 : value.intervals[n - 1]);
			}
		}
		mesh->mdl.frameGroups.clear();
		int lastGroup = -1;
		for (int i = 0; i < plan.size(); ++i)
		{
			const int source = plan[i].inherited, group = plan[i].original < 0 ? -1 : groupForFrame[source];
			if (i == 0 || group < 0 || group != lastGroup || duration[source] <= 0)
				mesh->mdl.frameGroups.append({i, duration[source] > 0 ? QVector<float>{duration[source]} : QVector<float>{}});
			else
				mesh->mdl.frameGroups.last().intervals.append(mesh->mdl.frameGroups.last().intervals.last() + duration[source]);
			lastGroup = group;
		}
	}
	return work.check();
}
// Why taking baked frame `item` out of the working set would lose data that
// baking its clip again cannot restore; empty when nothing would be lost. A
// clip joining the working set takes its geometry and skeletal attachments
// from the skeleton and everything else from the bind frame `inherited`, as
// Add skeletal clip does, so a dropped frame must match exactly that.
QString droppedFrameLoss(const ModelMesh &mesh, const Frame &item, int inherited, const QVector<int> &groupSize,
						 const QVector<float> &duration)
{
	const auto &skeleton = mesh.skeleton;
	const int frame = item.original;
	const auto &clip = skeleton.clips[item.clip];
	const auto &pose = clip.frames[item.local];
	for (const auto &surface : mesh.surfaces)
	{
		ModelFrameGeometry expected;
		if (surface.skinning.isEmpty())
			expected = surface.frames[inherited];
		else if (!skinModelSurface(surface, pose, &expected))
			expected = {};
		if (!same(expected, surface.frames[frame]))
			return Text::tr("Surface %1 has independent geometry or normal edits in frame %2 of clip %3. Keep the clip in the working "
							"set to keep them.")
				.arg(surface.name)
				.arg(frame)
				.arg(clip.name);
	}
	for (const auto &tag : mesh.tags)
	{
		if (tag.frameIndex != frame)
			continue;
		const auto skeletal = std::find_if(skeleton.tags.cbegin(), skeleton.tags.cend(), [&](const auto &t) { return t.name == tag.name; });
		const auto manual = std::find_if(mesh.tags.cbegin(), mesh.tags.cend(),
										 [&](const auto &t) { return t.frameIndex == inherited && t.name == tag.name; });
		const bool restored = skeletal != skeleton.tags.cend() ? same(tagPose(*skeletal, pose, frame), tag)
							  : manual != mesh.tags.cend()	   ? same(*manual, tag)
															   : false;
		if (!restored)
			return Text::tr("Attachment %1 has its own pose in frame %2 of clip %3. Keep the clip in the working set to keep it.")
				.arg(tag.name)
				.arg(frame)
				.arg(clip.name);
	}
	for (const auto &box : mesh.collisionBoxes)
	{
		if (box.framePoses.size() != mesh.frames.size())
			continue;
		const auto &own = box.framePoses[frame], &bind = box.framePoses[inherited];
		if (!same(own.centre, bind.centre) || !same(own.size, bind.size) || !same(own.rotation, bind.rotation))
			return Text::tr("Collision box %1 has its own pose in frame %2 of clip %3. Keep the clip in the working set to keep it.")
				.arg(box.name)
				.arg(frame)
				.arg(clip.name);
	}
	if (!same(mesh.frames[frame].origin, mesh.frames[inherited].origin))
		return Text::tr("Frame %1 of clip %2 has its own origin. Keep the clip in the working set to keep it.").arg(frame).arg(clip.name);
	if (mesh.mdl.enabled && (groupSize[frame] > 1 || duration[frame] != duration[inherited]))
		return Text::tr("Frame %1 of clip %2 has its own Quake MDL group timing. Keep the clip in the working set to keep it.")
			.arg(frame)
			.arg(clip.name);
	return {};
}
// Chooses the clips baked into editable frames. Clips that stay keep their
// frames exactly; clips that join are baked from the skeleton with the bind
// frame's auxiliary tracks; clips that leave drop their frames only when
// baking them again restores those frames. Custom ranges over dropped frames
// must be changed first, so no range shrinks silently.
bool chooseWorkingSet(const ModelMesh &mesh, const QVector<int> &chosen, QVector<QVector<Frame>> *clips, QString *error)
{
	const auto &skeleton = mesh.skeleton;
	QVector<bool> wanted(clips->size(), false);
	for (int clip : chosen)
	{
		if (clip < 0 || clip >= clips->size() || wanted[clip])
			return fail(error, Text::tr("Choose each clip of the working set once, by an existing clip index."));
		wanted[clip] = true;
	}
	const auto planned = planModelSkeletalWorkingSet(mesh, &chosen, modelSkeletalEditableBudget());
	if (!planned.fits)
		return fail(error, modelSkeletalWorkingSetLines(planned).join(QLatin1Char('\n')));
	const int inherited = std::max(0, modelBindPoseFrame(mesh));
	QVector<int> groupSize(mesh.frames.size(), 1);
	QVector<float> duration(mesh.frames.size(), 0);
	for (const auto &group : mesh.mdl.frameGroups)
		for (int n = 0; n < group.frameCount() && group.firstFrame + n < mesh.frames.size(); ++n)
		{
			groupSize[group.firstFrame + n] = group.frameCount();
			if (!group.intervals.isEmpty())
				duration[group.firstFrame + n] = group.intervals[n] - (n == 0 ? 0 : group.intervals[n - 1]);
		}
	QSet<int> dropped;
	for (int clip = 0; clip < clips->size(); ++clip)
	{
		if (wanted[clip] || (*clips)[clip].isEmpty())
			continue;
		for (const auto &item : (*clips)[clip])
		{
			const auto loss = droppedFrameLoss(mesh, item, inherited, groupSize, duration);
			if (!loss.isEmpty())
				return fail(error, loss);
			dropped.insert(item.original);
		}
	}
	for (const auto &animation : mesh.animations)
	{
		bool clipRange = false;
		for (int clip = 0; clip < clips->size() && !clipRange; ++clip)
			clipRange = !(*clips)[clip].isEmpty() && animation.name == skeleton.clips[clip].name &&
						animation.firstFrame == (*clips)[clip].first().original && animation.frameCount == (*clips)[clip].size();
		for (int frame = animation.firstFrame; !clipRange && frame < animation.firstFrame + animation.frameCount; ++frame)
			if (dropped.contains(frame))
				return fail(error, Text::tr("The animation range %1 covers frame %2, which would leave the working set. Change or remove "
											"that range first.")
									   .arg(animation.name)
									   .arg(frame));
	}
	for (int clip = 0; clip < clips->size(); ++clip)
	{
		auto &frames = (*clips)[clip];
		if (!wanted[clip])
			frames.clear();
		else if (frames.isEmpty())
			for (int local = 0; local < skeleton.clips[clip].frames.size(); ++local)
				frames.append({clip, local, -1, inherited});
	}
	// Joining clips are baked in clip order, between the frames of clips that
	// stay. A custom range keeps its first and last frames, so a clip joining
	// between them would silently grow it.
	QVector<int> moved(mesh.frames.size(), -1), joinedAt;
	QVector<int> joinedClip;
	int next = !mesh.frames.isEmpty() && skeleton.bakedClipForFrame.value(0) == -1 ? 1 : 0;
	if (next)
		moved[0] = 0;
	for (int clip = 0; clip < clips->size(); ++clip)
		for (const auto &item : (*clips)[clip])
		{
			if (item.original >= 0)
				moved[item.original] = next;
			else
			{
				joinedAt.append(next);
				joinedClip.append(clip);
			}
			++next;
		}
	for (const auto &animation : mesh.animations)
	{
		bool clipRange = false;
		for (int clip = 0; clip < clips->size() && !clipRange; ++clip)
			clipRange = animation.name == skeleton.clips[clip].name && skeleton.bakedClipForFrame.indexOf(clip) == animation.firstFrame &&
						animation.frameCount == skeleton.clips[clip].frames.size();
		if (clipRange || animation.frameCount <= 0)
			continue;
		const int first = moved.value(animation.firstFrame, -1), last = moved.value(animation.firstFrame + animation.frameCount - 1, -1);
		if (first < 0 || last < 0 || last - first + 1 == animation.frameCount)
			continue;
		const auto inside = std::find_if(joinedAt.cbegin(), joinedAt.cend(), [&](int frame) { return frame > first && frame < last; });
		const auto clip = inside == joinedAt.cend() ? -1 : joinedClip[int(inside - joinedAt.cbegin())];
		return fail(error, Text::tr("Clip %1 would join the working set inside the animation range %2 and change its length from %3 to "
									"%4. Change or remove that range first, or keep together the clips it spans.")
							   .arg(clip >= 0 ? skeleton.clips[clip].name : QString())
							   .arg(animation.name)
							   .arg(animation.frameCount)
							   .arg(last - first + 1));
	}
	return true;
}
} // namespace

bool isModelSkeletalEdit(ModelEditKind kind)
{
	return (kind >= ModelEditKind::TransformBindJoint && kind <= ModelEditKind::InterpolateSkeletalPoses) || kind == ModelEditKind::SolveTwoBoneIk || kind == ModelEditKind::EditAnimationCurves || kind == ModelEditKind::EditCurveKeys || kind == ModelEditKind::TransferAnimationCurves || kind == ModelEditKind::SetSkeletalWorkingSet;
}
bool defaultModelSkeletalOptions(const ModelSkeletalEditOptions &o)
{
	return o.clip == -1 && o.sourceClip == -1 && o.first == 0 && o.last == 0 && o.sourceFrame == 0 && o.frameCount == 1 &&
		   o.joint.isEmpty() && o.name.isEmpty() && o.rate == 24 && o.loops && o.descendants && !o.local && same(o.translation, {}, 0) &&
		   same(o.rotation, {}, 0) && same(o.scale, {1, 1, 1}, 0) && !o.matrixTransform &&
		   modelJointMatrixIsFinite(o.delta) && modelJointMatrixDistance(o.delta, modelJointIdentity()) == 0 && o.workingSet.isEmpty();
}
bool applyModelSkeletalEdit(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	if (edit.kind == ModelEditKind::SolveTwoBoneIk)
		return applyModelIk(mesh, edit, error, control);
	if (edit.kind == ModelEditKind::EditAnimationCurves)
		return applyModelAnimationCurves(mesh, edit, error, control);
	if (edit.kind == ModelEditKind::TransferAnimationCurves)
        return applyModelCurveTransfer(mesh, edit, error, control);
	if (edit.kind == ModelEditKind::EditCurveKeys)
		return applyModelCurveKeys(mesh, edit, nullptr, error, control);
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !isModelSkeletalEdit(edit.kind) || !work.check())
		return false;
	if (!validateModelSkeleton(*mesh, error, control))
		return false;
	const auto &o = edit.skeletal;
	const auto kind = edit.kind;
	const bool bind = kind == ModelEditKind::TransformBindJoint, add = kind == ModelEditKind::AddSkeletalClip;
	const bool duplicate = kind == ModelEditKind::DuplicateSkeletalClip, rename = kind == ModelEditKind::RenameSkeletalClip;
	const bool resize = kind == ModelEditKind::ResizeSkeletalClip, timing = kind == ModelEditKind::SetSkeletalClipTiming;
	const bool transformJoint = kind == ModelEditKind::TransformSkeletalPose;
	const bool copy = kind == ModelEditKind::CopySkeletalPose, interpolate = kind == ModelEditKind::InterpolateSkeletalPoses;
	const bool range = transformJoint || copy || interpolate;
	const bool workingSet = kind == ModelEditKind::SetSkeletalWorkingSet;
	if ((o.matrixTransform && (!(bind || transformJoint) || o.local || !same(o.translation, {}, 0) ||
		!same(o.rotation, {}, 0) || !same(o.scale, {1, 1, 1}, 0))) ||
		(!o.matrixTransform && (!modelJointMatrixIsFinite(o.delta) || modelJointMatrixDistance(o.delta, modelJointIdentity()) != 0)))
		return fail(error, Text::tr("A joint matrix delta applies only to bind or pose transforms and cannot be mixed with numeric transforms."));
	if (((bind || add || workingSet) && o.clip != -1) || (!workingSet && !o.workingSet.isEmpty()) ||
		(!(bind || transformJoint) && (!o.joint.isEmpty() || !o.descendants || o.local || !same(o.translation, {}, 0) ||
									   !same(o.rotation, {}, 0) || !same(o.scale, {1, 1, 1}, 0))) ||
		(!(add || duplicate || rename) && !o.name.isEmpty()) || (!(add || timing) && (o.rate != 24 || !o.loops)) ||
		(!(add || resize) && o.frameCount != 1) || (!range && (o.first != 0 || o.last != 0)) ||
		(!copy && (o.sourceClip != -1 || o.sourceFrame != 0)) || edit.frame != -1)
		return fail(error, Text::tr("The supplied skeletal settings do not apply to this operation."));
	const ModelMesh before = *mesh;
	QVector<Frame> oldPlan;
	QVector<QVector<Frame>> clips;
	if (!layout(before, &oldPlan, &clips, error))
		return false;
	const auto oldClips = clips;
	QVector<int> oldClipForNew;
	for (int i = 0; i < clips.size(); ++i)
		oldClipForNew.append(i);
	if (!bind && !add && !workingSet && (o.clip < 0 || o.clip >= clips.size()))
		return fail(error, Text::tr("Select an existing skeletal clip index."));
	if ((add || duplicate || rename) && !validName(*mesh, o.name, rename ? o.clip : -1, error))
		return false;
	if ((add || timing) && (!std::isfinite(o.rate) || o.rate < .001 || o.rate > 1000))
		return fail(error, Text::tr("Authored clip FPS must be from 0.001 to 1000."));
	if ((add || resize) && (o.frameCount < 1 || o.frameCount > modelDocumentMaxFrames))
		return fail(error, Text::tr("A skeletal clip needs between 1 and 1024 frames."));
	if (range && (o.first < 0 || o.last < o.first || o.last >= mesh->skeleton.clips[o.clip].frames.size()))
		return fail(error, Text::tr("Choose an inclusive range within the skeletal clip."));
	if ((add || duplicate) && mesh->skeleton.clips.size() >= modelDocumentMaxAnimations)
		return fail(error, Text::tr("A skeleton supports at most 1024 authored clips."));
	if (bind || transformJoint)
	{
		const int joint = modelJointIndex(mesh->skeleton, o.joint, Qt::CaseSensitive);
		if (joint < 0)
			return fail(error, Text::tr("Choose an existing joint by its exact name."));
		if (bind)
		{
			const auto oldBind = modelSkeletonBindPose(mesh->skeleton);
			auto newBind = oldBind;
			if (!transformPose(&newBind, mesh->skeleton, joint, o, error))
				return false;
			QVector<ModelJointMatrix> toNew(newBind.size()), toOld(newBind.size());
			QVector<bool> affected(newBind.size(), false);
			for (int j = 0; j < newBind.size(); ++j)
			{
				if (!work.step())
					return false;
				if (modelJointMatrixDistance(oldBind[j], newBind[j]) == 0)
					continue;
				bool ok = false;
				const auto inverseNew = modelJointInverse(newBind[j], &ok);
				if (!ok)
					return fail(error, Text::tr("The new bind transform is singular."));
				const auto inverseOld = modelJointInverse(oldBind[j], &ok);
				if (!ok)
					return fail(error, Text::tr("The existing bind transform is singular."));
				toNew[j] = modelJointMultiply(inverseNew, oldBind[j]);
				toOld[j] = modelJointMultiply(inverseOld, newBind[j]);
				affected[j] = true;
				mesh->skeleton.joints[j].bind = newBind[j];
			}
			for (auto &clip : mesh->skeleton.clips)
				for (auto &frame : clip.frames)
					for (int j = 0; j < frame.size(); ++j)
					{
						if (!work.step())
							return false;
						if (affected[j])
							frame[j] = modelJointMultiply(frame[j], toOld[j]);
					}
			for (auto &surface : mesh->surfaces)
				for (auto &influence : surface.skinning.influences)
				{
					if (!work.step())
						return false;
					if (!affected[influence.joint])
						continue;
					influence.offset = modelJointTransformPoint(toNew[influence.joint], influence.offset);
					// Preserve the exact vector blended by skinModelSurface, including
					// its magnitude under a non-uniform bind scale.
					influence.normalOffset = modelJointTransformVector(toNew[influence.joint], influence.normalOffset);
				}
			for (auto &tag : mesh->skeleton.tags)
			{
				if (!work.step())
					return false;
				if (affected[tag.joint])
					tag.offset = modelJointMultiply(toNew[tag.joint], tag.offset);
			}
		}
		else
			for (int i = o.first; i <= o.last; ++i)
			{
				if (!work.step() || !transformPose(&mesh->skeleton.clips[o.clip].frames[i], mesh->skeleton, joint, o, error))
					return false;
			}
	}
	else if (add || duplicate)
	{
		ModelSkeletalClip clip = duplicate ? mesh->skeleton.clips[o.clip] : ModelSkeletalClip{};
		clip.name = o.name;
		clip.sourcePath.clear();
		QVector<Frame> frames;
		const int next = clips.size();
		if (add)
		{
			clip.framesPerSecond = o.rate;
			clip.loops = o.loops;
			clip.frames.fill(modelSkeletonBindPose(mesh->skeleton), o.frameCount);
			const int inherited = std::max(0, modelBindPoseFrame(before));
			for (int i = 0; i < o.frameCount; ++i)
				frames.append({next, i, -1, inherited});
		}
		else
		{
			for (int i = 0; i < clip.frames.size(); ++i)
				frames.append({next, i, -1, clips[o.clip].isEmpty() ? 0 : clips[o.clip][i].inherited});
		}
		mesh->skeleton.clips.append(clip);
		clips.append(frames);
		oldClipForNew.append(-1);
	}
	else if (workingSet)
	{
		if (!chooseWorkingSet(before, o.workingSet, &clips, error))
			return false;
	}
	else if (rename)
		mesh->skeleton.clips[o.clip].name = o.name;
	else if (timing)
	{
		mesh->skeleton.clips[o.clip].framesPerSecond = o.rate;
		mesh->skeleton.clips[o.clip].loops = o.loops;
	}
	else if (kind == ModelEditKind::DeleteSkeletalClip)
	{
		mesh->skeleton.clips.removeAt(o.clip);
		clips.removeAt(o.clip);
		oldClipForNew.removeAt(o.clip);
	}
	else if (resize)
	{
		auto &frames = mesh->skeleton.clips[o.clip].frames;
		if (frames.isEmpty())
			return fail(error, Text::tr("An empty imported clip cannot supply poses for resizing."));
		const auto last = frames.last();
		frames.resize(o.frameCount, last);
		if (!clips[o.clip].isEmpty())
		{
			const auto source = clips[o.clip].last();
			while (clips[o.clip].size() < o.frameCount)
				clips[o.clip].append({o.clip, int(clips[o.clip].size()), -1, source.inherited});
			clips[o.clip].resize(o.frameCount);
		}
	}
	else if (copy)
	{
		if (o.sourceClip < -1 || o.sourceClip >= before.skeleton.clips.size() || o.sourceFrame < 0 ||
			(o.sourceClip < 0 ? o.sourceFrame != 0 : o.sourceFrame >= before.skeleton.clips[o.sourceClip].frames.size()))
			return fail(error, Text::tr("Choose an existing source clip/frame, or the bind pose."));
		const auto source =
			o.sourceClip < 0 ? modelSkeletonBindPose(before.skeleton) : before.skeleton.clips[o.sourceClip].frames[o.sourceFrame];
		for (int i = o.first; i <= o.last; ++i)
			mesh->skeleton.clips[o.clip].frames[i] = source;
	}
	else if (interpolate)
	{
		if (o.last - o.first < 2)
			return fail(error, Text::tr("Interpolation needs two endpoint poses with at least one frame between them."));
		auto &frames = mesh->skeleton.clips[o.clip].frames;
		QVector<ModelJointMatrix> first, last;
		if (!localPose(mesh->skeleton, frames[o.first], &first, error) || !localPose(mesh->skeleton, frames[o.last], &last, error))
			return false;
		for (int frame = o.first + 1; frame < o.last; ++frame)
		{
			if (!work.step())
				return false;
			const double amount = double(frame - o.first) / (o.last - o.first);
			QVector<ModelJointMatrix> local;
			for (int joint = 0; joint < first.size(); ++joint)
				local.append(modelJointMatrix(
					modelQuatSlerp(modelJointRotation(first[joint]), modelJointRotation(last[joint]), float(amount)),
					interpolateModelPosition(modelJointTranslation(first[joint]), modelJointTranslation(last[joint]), amount),
					interpolateModelPosition(modelJointScale(first[joint]), modelJointScale(last[joint]), amount)));
			frames[frame] = modelJointsToModelSpace(modelJointParents(mesh->skeleton), local);
		}
	}
	if (range || resize || bind)
		for (auto &clip : mesh->skeleton.clips)
		{
			clip.frameMins.clear();
			clip.frameMaxs.clear();
		}
	qint64 matrices = 0;
	for (const auto &clip : mesh->skeleton.clips)
		matrices += qint64(clip.frames.size()) * mesh->skeleton.joints.size();
	if (matrices > 16LL * 1024 * 1024)
		return fail(error, Text::tr("The authored animation exceeds the source format's joint-pose storage limit."));
	QVector<Frame> plan;
	if (oldPlan.first().clip < 0)
		plan.append(oldPlan.first());
	for (int clip = 0; clip < clips.size(); ++clip)
		for (auto item : clips[clip])
		{
			item.clip = clip;
			plan.append(item);
		}
	// Deleting the only displayed clip still leaves an editable bind frame.
	if (plan.isEmpty())
		plan.append({-1, 0, -1, 0});
	return rebuild(mesh, before, oldPlan, oldClips, plan, oldClipForNew, bind, error, work);
}
bool replaceModelSkeletalSamples(ModelMesh *mesh, int clip, int first, const QVector<QVector<ModelJointMatrix>> &samples,
	QString *error, const ModelWorkControl &control, const QVector<ModelPoseCurves> *curves)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check() || !validateModelSkeleton(*mesh, error, control)) return false;
	if (clip < 0 || clip >= mesh->skeleton.clips.size() || first < 0 || samples.isEmpty() ||
		qint64(first) + samples.size() > mesh->skeleton.clips[clip].frames.size())
		return fail(error, Text::tr("Choose an existing clip and a complete consecutive pose range."));
	const auto before = *mesh;
	QVector<Frame> plan;
	QVector<QVector<Frame>> clips;
	if (!layout(before, &plan, &clips, error) || plan.isEmpty()) return false;
	if (curves) mesh->skeleton.clips[clip].curves = *curves;
	for (int i = 0; i < samples.size(); ++i)
	{
		if (!work.step()) return false;
		if (samples[i].size() != mesh->skeleton.joints.size())
			return fail(error, Text::tr("Every replacement pose must contain every joint."));
		mesh->skeleton.clips[clip].frames[first+i] = samples[i];
	}
	if (!validateModelSkeleton(*mesh, error, control)) return false;
	mesh->skeleton.clips[clip].frameMins.clear();
	mesh->skeleton.clips[clip].frameMaxs.clear();
	QVector<int> identity;
	for (int i=0; i<clips.size(); ++i) identity.append(i);
	return rebuild(mesh, before, plan, clips, plan, identity, false, error, work);
}
} // namespace vibestudio
