#include "core/model_animation_curves.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAnimationCurves)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool near(const ModelJointMatrix &a, const ModelJointMatrix &b)
{
	for (int n = 0; n < 12; ++n)
		if (std::abs(double(a.m[n]) - b.m[n]) > std::max(.0001, std::abs(double(b.m[n])) * .000002))
			return false;
	return true;
}
bool localPose(const ModelSkeleton &skeleton, const QVector<ModelJointMatrix> &pose, int joint, ModelJointMatrix *result, QString *error)
{
	if (joint < 0 || joint >= skeleton.joints.size() || pose.size() != skeleton.joints.size())
		return fail(error, Text::tr("Choose an existing joint with complete retained pose samples."));
	const int parent = skeleton.joints[joint].parent;
	if (parent < -1 || parent >= joint)
		return fail(error, Text::tr("Curve authoring requires a valid ordered joint hierarchy."));
	*result = pose[joint];
	if (parent >= 0)
	{
		bool ok = false;
		const auto inverse = modelJointInverse(pose[parent], &ok);
		if (!ok)
			return fail(error, Text::tr("Curve authoring requires invertible parent poses."));
		*result = modelJointMultiply(inverse, pose[joint]);
	}
	return true;
}
int groupIndex(const QVector<ModelPoseCurves> &groups, const QString &joint)
{
	for (int i = 0; i < groups.size(); ++i)
		if (groups[i].joint == joint)
			return i;
	return -1;
}
bool clipJoint(const ModelMesh &mesh, int clip, const QString &name, int *joint, QString *error)
{
	*joint = modelJointIndex(mesh.skeleton, name, Qt::CaseSensitive);
	if (clip < 0 || clip >= mesh.skeleton.clips.size() || mesh.skeleton.clips[clip].frames.isEmpty() || *joint < 0)
		return fail(error, Text::tr("Choose an existing skeletal clip and an exact joint name."));
	return true;
}
std::array<double, 3> euler(const ModelJointMatrix &m, const ModelVec3 &scale, const std::array<double, 3> &previous, bool first)
{
	const double radians = 180 / std::numbers::pi, sine = -double(m.m[8]) / scale.x;
	const double cosine = std::hypot(double(m.m[0]) / scale.x, double(m.m[4]) / scale.x);
	std::array<double, 3> a{0, std::atan2(sine, cosine) * radians, 0};
	if (cosine > 1e-6)
	{
		const double z = std::atan2(double(m.m[4]) / scale.x, double(m.m[0]) / scale.x);
		// Remove Z before extracting X: unlike dividing the last row by cos(Y),
		// this remains stable for float matrices close to either gimbal pole.
		a[0] = std::atan2(std::sin(z) * m.m[2] / scale.z - std::cos(z) * m.m[6] / scale.z,
						  std::cos(z) * m.m[5] / scale.y - std::sin(z) * m.m[1] / scale.y) *
			   radians;
		a[2] = z * radians;
	}
	else
	{
		a[0] = std::atan2(-double(m.m[6]) / scale.z, double(m.m[5]) / scale.y) * radians;
		if (!first)
		{
			a[2] = previous[2];
			a[0] += sine > 0 ? a[2] : -a[2];
		}
	}
	if (first)
		return a;
	auto b = a;
	b[0] += 180;
	b[1] = 180 - b[1];
	b[2] += 180;
	double da = 0, db = 0;
	for (int i = 0; i < 3; ++i)
	{
		a[i] = previous[i] + std::remainder(a[i] - previous[i], 360.0);
		b[i] = previous[i] + std::remainder(b[i] - previous[i], 360.0);
		da += std::pow(a[i] - previous[i], 2);
		db += std::pow(b[i] - previous[i], 2);
	}
	return da <= db ? a : b;
}
} // namespace
bool hasModelAnimationCurves(const ModelMesh &mesh)
{
	for (const auto &clip : mesh.skeleton.clips)
		if (!clip.curves.isEmpty())
			return true;
	return false;
}
int modelAnimationCurveVersion(const ModelMesh &mesh)
{
	int version = 0;
	for (const auto &clip : mesh.skeleton.clips)
		for (const auto &group : clip.curves)
		{
			version = 9;
			for (const auto &channel : group.channels)
				for (const auto &key : channel)
					if (key.interpolation == ModelCurveInterpolation::StepNext)
						return 10;
		}
	return version;
}
bool validateModelAnimationCurves(const ModelMesh &mesh, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	qint64 count = 0;
	for (const auto &clip : mesh.skeleton.clips)
	{
		QSet<QString> names;
		for (const auto &group : clip.curves)
		{
			if (!work.check() || !validateModelCurveGroup(group, int(clip.frames.size()), error))
				return false;
			const int joint = modelJointIndex(mesh.skeleton, group.joint, Qt::CaseSensitive);
			if (joint < 0 || names.contains(group.joint))
				return fail(error, Text::tr("Stored curves must name distinct existing joints exactly."));
			names.insert(group.joint);
			for (const auto &channel : group.channels)
				count += channel.size();
			if (count > modelCurveMaxKeys)
				return fail(error, Text::tr("The model exceeds the 1,048,576 animation key budget."));
			for (int frame = 0; frame < clip.frames.size(); ++frame)
			{
				if (!work.step())
					return false;
				ModelJointMatrix local;
				if (!localPose(mesh.skeleton, clip.frames[frame], joint, &local, error))
					return false;
				if (!near(local, evaluateModelPoseCurves(group, frame)))
					return fail(error, Text::tr("Stored curves no longer match joint %1 in clip %2, sample %3. Bake that joint's curves "
												"before changing its sampled poses.")
										   .arg(group.joint, clip.name)
										   .arg(frame));
			}
		}
	}
	return work.check();
}
bool captureModelPoseCurves(const ModelMesh &mesh, int clip, const QString &name, ModelPoseCurves *result, QString *error,
							const ModelWorkControl &control)
{
	int joint = -1;
	if (!result || !clipJoint(mesh, clip, name, &joint, error))
		return false;
	const auto &frames = mesh.skeleton.clips[clip].frames;
	if (qint64(frames.size()) * 9 > modelCurveMaxKeys)
		return fail(error, Text::tr("Capturing this joint would exceed the animation key budget."));
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	ModelPoseCurves group;
	group.joint = name;
	std::array<double, 3> previous{};
	for (int f = 0; f < frames.size(); ++f)
	{
		if (!work.step())
			return false;
		ModelJointMatrix local;
		if (!localPose(mesh.skeleton, frames[f], joint, &local, error))
			return false;
		const auto t = modelJointTranslation(local), s = modelJointScale(local);
		if (s.x <= .000001f || s.y <= .000001f || s.z <= .000001f)
			return fail(error, Text::tr("Curve capture requires positive, nonzero local scale."));
		const auto r = euler(local, s, previous, f == 0);
		previous = r;
		const double values[9] = {t.x, t.y, t.z, r[0], r[1], r[2], s.x, s.y, s.z};
		for (int c = 0; c < 9; ++c)
		{
			ModelPoseCurveKey key;
			key.frame = f;
			key.value = values[c];
			group.channels[c].append(key);
		}
		if (!near(local, evaluateModelPoseCurves(group, f)))
			return fail(error, Text::tr("Joint %1 has local shear or reflection, or cannot be captured at this precision. Curves require "
										"translation, rotation and positive scale.")
								   .arg(name));
	}
	if (!validateModelCurveGroup(group, int(frames.size()), error) || !work.check())
		return false;
	*result = std::move(group);
	return true;
}
bool editModelCurveGroup(ModelPoseCurves *group, int frameCount, const ModelCurveEditOptions &o, QString *error)
{
	if (!group || o.channel < 0 || o.channel >= 9 || o.frame < 0 || o.frame >= frameCount || group->joint != o.joint)
		return fail(error, Text::tr("Choose a valid curve channel and sample on the selected joint."));
	ModelPoseCurves candidate = *group;
	auto &keys = candidate.channels[o.channel];
	auto it = std::lower_bound(keys.begin(), keys.end(), o.frame, [](const ModelPoseCurveKey &k, int f) { return k.frame < f; });
	const bool found = it != keys.end() && it->frame == o.frame;
	if (o.action == ModelCurveAction::SetKey)
	{
		if (o.key.frame != o.frame)
			return fail(error, Text::tr("The key sample must match the requested sample."));
		if (found)
			*it = o.key;
		else
			keys.insert(it, o.key);
	}
	else if (o.action == ModelCurveAction::RemoveKey || o.action == ModelCurveAction::MoveKey)
	{
		if (!found)
			return fail(error, Text::tr("There is no key at that sample."));
		if (o.action == ModelCurveAction::RemoveKey)
		{
			if (keys.size() == 1)
				return fail(
					error,
					Text::tr(
						"Keep at least one key in every channel. Use Bake Joint Curves to remove editable curves and retain the motion."));
			keys.erase(it);
		}
		else
		{
			if (o.newFrame < 0 || o.newFrame >= frameCount)
				return fail(error, Text::tr("The moved key must remain inside the clip."));
			if (o.newFrame != o.frame &&
				std::any_of(keys.cbegin(), keys.cend(), [&](const ModelPoseCurveKey &k) { return k.frame == o.newFrame; }))
				return fail(error, Text::tr("Another key already occupies the destination sample."));
			it->frame = o.newFrame;
			std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
		}
	}
	else
		return fail(error, Text::tr("Choose a key insertion, removal or move operation."));
	if (!validateModelCurveGroup(candidate, frameCount, error))
		return false;
	*group = std::move(candidate);
	return true;
}
bool applyModelAnimationCurves(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check())
		return false;
	if (!validateModelSkeleton(*mesh, error, control))
		return false;
	const auto &o = edit.curves;
	int joint = -1;
	if (edit.kind != ModelEditKind::EditAnimationCurves || !clipJoint(*mesh, o.clip, o.joint, &joint, error))
		return false;
	if (!defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Curve operations cannot be combined with sampled-pose settings."));
	const bool keyAction =
		o.action == ModelCurveAction::SetKey || o.action == ModelCurveAction::RemoveKey || o.action == ModelCurveAction::MoveKey;
	if (int(o.action) < 0 || int(o.action) > int(ModelCurveAction::Bake) || (!keyAction && (o.channel != -1 || o.frame != 0)) ||
		(o.action != ModelCurveAction::SetKey && !(o.key == ModelPoseCurveKey{})) ||
		(o.action != ModelCurveAction::MoveKey && o.newFrame != 0) ||
		(o.action != ModelCurveAction::Replace && !(o.group == ModelPoseCurves{})))
		return fail(error, Text::tr("Curve settings must match the selected capture, key, replacement or bake operation."));
	auto groups = mesh->skeleton.clips[o.clip].curves;
	const int index = groupIndex(groups, o.joint);
	if (o.action == ModelCurveAction::Bake)
	{
		if (index < 0)
			return fail(error, Text::tr("That joint has no stored animation curves."));
		groups.removeAt(index);
		mesh->skeleton.clips[o.clip].curves = groups;
		return work.check();
	}
	ModelPoseCurves baseline;
	if (index >= 0)
		baseline = groups[index];
	else if (!captureModelPoseCurves(*mesh, o.clip, o.joint, &baseline, error, control))
		return false;
	ModelPoseCurves next = baseline;
	if (o.action == ModelCurveAction::Capture)
	{
		if (index >= 0)
			return fail(error, Text::tr("That joint already has editable curves. Edit or bake them before capturing again."));
	}
	else if (o.action == ModelCurveAction::Replace)
	{
		if (o.group.joint != o.joint)
			return fail(error, Text::tr("Replacement curves must name the selected joint exactly."));
		next = o.group;
	}
	else
	{
		if (index < 0)
			return fail(error, Text::tr("Capture the joint's sampled animation before editing its keys."));
		if (!editModelCurveGroup(&next, int(mesh->skeleton.clips[o.clip].frames.size()), o, error))
			return false;
	}
	if (!validateModelCurveGroup(next, int(mesh->skeleton.clips[o.clip].frames.size()), error))
		return false;
	if (index >= 0 && next == baseline)
		return work.check();
	if (index >= 0)
		groups[index] = next;
	else
		groups.append(next);
	auto samples = mesh->skeleton.clips[o.clip].frames;
	QVector<bool> descendants(mesh->skeleton.joints.size(), false);
	for (int j = 0; j < descendants.size(); ++j)
		descendants[j] = j == joint || (mesh->skeleton.joints[j].parent >= 0 && descendants[mesh->skeleton.joints[j].parent]);
	for (int frame = 0; frame < samples.size(); ++frame)
	{
		if (!work.check())
			return false;
		const auto local = evaluateModelPoseCurves(next, frame);
		if (modelJointMatrixDistance(local, evaluateModelPoseCurves(baseline, frame)) == 0)
			continue;
		const auto original = samples[frame];
		const int parent = mesh->skeleton.joints[joint].parent;
		const auto target = parent < 0 ? local : modelJointMultiply(original[parent], local);
		bool ok = false;
		const auto inverse = modelJointInverse(original[joint], &ok);
		if (!ok)
			return fail(error, Text::tr("The existing joint pose is singular and cannot move its descendants."));
		const auto delta = modelJointMultiply(target, inverse);
		for (int j = 0; j < original.size(); ++j)
		{
			if (!work.step())
				return false;
			if (descendants[j])
				samples[frame][j] = j == joint ? target : modelJointMultiply(delta, original[j]);
		}
	}
	return replaceModelSkeletalSamples(mesh, o.clip, 0, samples, error, control, &groups);
}
} // namespace vibestudio
