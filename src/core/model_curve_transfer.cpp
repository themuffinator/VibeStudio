#include "core/model_curve_transfer.h"
#include "core/model_document.h"
#include "core/model_skeleton.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelCurveTransfer)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
int groupIndex(const QVector<ModelPoseCurves> &groups, const QString &joint)
{
	for (int i = 0; i < groups.size(); ++i)
		if (groups[i].joint == joint)
			return i;
	return -1;
}
bool options(const ModelMesh &mesh, const ModelCurveTransferOptions &o, QStringList *joints, QVector<int> *channels, QString *error)
{
	if (o.sourceClip < 0 || o.targetClip < 0 || o.sourceClip >= mesh.skeleton.clips.size() || o.targetClip >= mesh.skeleton.clips.size() ||
		o.first < 0 || o.last < o.first || o.last >= mesh.skeleton.clips[o.sourceClip].frames.size() || o.targetFirst < 0 ||
		!std::isfinite(o.factor) || o.factor <= 0 || o.factor > 1000000)
		return fail(error, Text::tr("Choose existing clips, an ordered source range and a positive finite time scale."));
	const double end = o.targetFirst + (o.last - o.first) * o.factor;
	if (!std::isfinite(end) || end >= mesh.skeleton.clips[o.targetClip].frames.size() || std::abs(end - std::round(end)) > 1e-7)
		return fail(error, Text::tr("The destination range must end on an integer sample inside its clip."));
	QSet<QString> seen;
	for (const auto &joint : o.joints)
	{
		if (seen.contains(joint) || modelJointIndex(mesh.skeleton, joint, Qt::CaseSensitive) < 0)
			return fail(error, Text::tr("Joint selectors must be distinct exact names from this rig."));
		seen.insert(joint);
	}
	joints->clear();
	for (const auto &joint : mesh.skeleton.joints)
		if (o.joints.isEmpty() || seen.contains(joint.name))
			joints->append(joint.name);
	QSet<int> selected;
	for (int channel : o.channels)
	{
		if (channel < 0 || channel >= 9 || selected.contains(channel))
			return fail(error, Text::tr("Channel selectors must be distinct translation, rotation or scale channels."));
		selected.insert(channel);
	}
	channels->clear();
	for (int c = 0; c < 9; ++c)
		if (o.channels.isEmpty() || selected.contains(c))
			channels->append(c);
	return !joints->isEmpty();
}
bool curves(const ModelMesh &mesh, int clip, const QString &joint, ModelPoseCurves *result, QString *error, const ModelWorkControl &control)
{
	const auto &groups = mesh.skeleton.clips[clip].curves;
	const int index = groupIndex(groups, joint);
	if (index >= 0)
	{
		*result = groups[index];
		return true;
	}
	return captureModelPoseCurves(mesh, clip, joint, result, error, control);
}
bool identity(const ModelCurveTransferOptions &o)
{
	return o.sourceClip == o.targetClip && o.first == o.targetFirst && o.factor == 1 && !o.reverse;
}
} // namespace
bool applyModelCurveTransfer(ModelMesh *mesh, const ModelEdit &edit, QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check() || !validateModelSkeleton(*mesh, error, control))
		return false;
	if (edit.kind != ModelEditKind::TransferAnimationCurves || !defaultModelSkeletalOptions(edit.skeletal))
		return fail(error, Text::tr("Curve transfer requires its own document operation without sampled-pose settings."));
	const auto &o = edit.curveTransfer;
	QStringList joints;
	QVector<int> channels;
	if (!options(*mesh, o, &joints, &channels, error))
		return false;
	if (identity(o))
		return work.check();
	const auto source = *mesh;
	auto baseline = mesh->skeleton.clips[o.targetClip].curves;
	auto result = baseline;
	qint64 total = 0;
	for (const auto &clip : mesh->skeleton.clips)
		for (const auto &group : clip.curves)
			for (const auto &channel : group.channels)
				total += channel.size();
	const int targetFrames = int(mesh->skeleton.clips[o.targetClip].frames.size());
	const int end = int(std::round(o.targetFirst + (o.last - o.first) * o.factor));
	for (const auto &joint : joints)
	{
		if (!work.check())
			return false;
		ModelPoseCurves from, target;
		if (!curves(source, o.sourceClip, joint, &from, error, control) || !curves(source, o.targetClip, joint, &target, error, control))
			return false;
		int index = groupIndex(result, joint);
		if (index < 0)
		{
			index = int(result.size());
			baseline.append(target);
			result.append(target);
			for (const auto &channel : target.channels)
				total += channel.size();
		}
		for (int c : channels)
		{
			QVector<ModelPoseCurveKey> copied;
			if (!sliceModelCurve(from.channels[c], o.first, o.last, o.targetFirst, o.factor, o.reverse, targetFrames, &copied, error,
								 control))
				return false;
			const auto &old = target.channels[c];
			// Preserve the destination's outward tangent sides and interpolation.
			// Different pasted boundary values necessarily change the adjoining spans.
			copied.first().inSlope = sampleModelCurveKey(old, o.targetFirst).inSlope;
			const auto right = sampleModelCurveKey(old, end);
			copied.last().outSlope = right.outSlope;
			copied.last().interpolation = right.interpolation;
			QVector<ModelPoseCurveKey> merged;
			for (int i = 0; i < old.size(); ++i)
			{
				if (!work.step())
					return false;
				if (old[i].frame < o.targetFirst || old[i].frame > end)
					merged.append(sampleModelCurveKey(old, old[i].frame));
			}
			merged += copied;
			std::sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
			total += merged.size() - old.size();
			if (total > modelCurveMaxKeys)
				return fail(
					error,
					Text::tr(
						"The transferred curves exceed the model's 1,048,576 key budget. Select fewer joints or reduce curves first."));
			result[index].channels[c] = std::move(merged);
		}
		if (!validateModelCurveGroup(result[index], targetFrames, error))
			return false;
	}
	if (!work.check())
		return false;
	// This function edits an isolated document candidate. The shared rebake
	// sees captured baseline curves so unchanged sample matrices stay exact.
	mesh->skeleton.clips[o.targetClip].curves = std::move(baseline);
	return replaceModelCurveGroups(mesh, o.targetClip, result, error, control);
}
bool describeModelCurveTransfer(const ModelMesh &before, const ModelMesh &after, const ModelCurveTransferOptions &o,
								ModelCurveKeyReport *report, QString *error, const ModelWorkControl &control)
{
	QStringList joints;
	QVector<int> channels;
	if (!report || !options(before, o, &joints, &channels, error) || o.targetClip >= after.skeleton.clips.size())
		return false;
	ModelWorkProgress work(control, ModelWorkPhase::Validating, error);
	ModelCurveKeyReport result;
	for (const auto &joint : joints)
	{
		ModelPoseCurves source, old, changed;
		if (!work.check() || !curves(before, o.sourceClip, joint, &source, error, control) ||
			!curves(before, o.targetClip, joint, &old, error, control) || !curves(after, o.targetClip, joint, &changed, error, control))
			return false;
		for (int c : channels)
		{
			QVector<ModelPoseCurveKey> selected;
			if (!sliceModelCurve(source.channels[c], o.first, o.last, 0, 1, false, o.last - o.first + 1, &selected, error, control))
				return false;
			double maximum = 0;
			if (!modelCurveDifferenceBound(old.channels[c], changed.channels[c], 0, before.skeleton.clips[o.targetClip].frames.size() - 1,
										   &maximum, error, control))
				return false;
			result.channels.append({joint, c, int(selected.size()), int(old.channels[c].size()), int(changed.channels[c].size()), maximum});
		}
	}
	if (!work.check())
		return false;
	*report = std::move(result);
	return true;
}
} // namespace vibestudio
