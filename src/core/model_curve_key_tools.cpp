#include "core/model_curve_key_tools.h"
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
	Q_DECLARE_TR_FUNCTIONS(ModelCurveKeyTools)
};
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool reduce(QVector<ModelPoseCurveKey> *keys, const QVector<int> &selected, double tolerance, QString *error,
			const ModelWorkControl &control)
{
	const auto original = *keys;
	QVector<bool> keep(original.size(), true), linear(original.size(), false);
	QVector<QPair<int, int>> spans;
	for (int i = 0; i < selected.size();)
	{
		const int start = selected[i];
		int end = start;
		while (++i < selected.size() && selected[i] == end + 1)
			end = selected[i];
		QVector<int> anchors{start, end};
		for (int k = start; k < end; ++k)
			if (original[k].interpolation == ModelCurveInterpolation::Step ||
				original[k].interpolation == ModelCurveInterpolation::StepNext)
			{
				anchors << k << k + 1;
			}
		std::sort(anchors.begin(), anchors.end());
		anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());
		for (int k = 1; k < anchors.size(); ++k)
			spans.append({anchors[k - 1], anchors[k]});
	}
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	while (!spans.isEmpty())
	{
		if (!work.check())
			return false;
		const auto [first, last] = spans.takeLast();
		if (last - first < 2)
			continue;
		QVector<ModelPoseCurveKey> line{original[first], original[last]};
		line[0].interpolation = ModelCurveInterpolation::Linear;
		double maximum = 0;
		if (!modelCurveDifferenceBound(original, line, original[first].frame, original[last].frame, &maximum, error, control))
			return false;
		if (maximum <= tolerance)
		{
			linear[first] = true;
			for (int k = first + 1; k < last; ++k)
				keep[k] = false;
		}
		else
		{
			// Balanced splitting bounds work to O(n log n), including oscillatory
			// clips. Every accepted replacement is checked against the original.
			const int middle = first + (last - first) / 2;
			spans.append({first, middle});
			spans.append({middle, last});
		}
	}
	QVector<int> retained;
	for (int i = 0; i < keep.size(); ++i)
		if (keep[i])
			retained.append(i);
	QVector<ModelPoseCurveKey> result;
	for (int i = 0; i < retained.size(); ++i)
	{
		if (!work.step())
			return false;
		const int old = retained[i];
		auto key = original[old];
		const bool changedNeighbours = (i && retained[i - 1] != old - 1) || (i + 1 < retained.size() && retained[i + 1] != old + 1);
		if (changedNeighbours && (key.tangent == ModelCurveTangent::Auto || key.tangent == ModelCurveTangent::Clamped))
		{
			const auto slopes = modelCurveSlopes(original, old);
			key.tangent = ModelCurveTangent::Free;
			key.inSlope = slopes.first;
			key.outSlope = slopes.second;
		}
		if (linear[old])
			key.interpolation = ModelCurveInterpolation::Linear;
		result.append(key);
	}
	if (!work.check())
		return false;
	*keys = std::move(result);
	return true;
}
} // namespace
bool editModelCurveKeys(QVector<ModelPoseCurves> *groups, int frameCount, const ModelCurveKeyOptions &o, ModelCurveKeyReport *report,
						QString *error, const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!groups || !work.check())
		return false;
	const ModelCurveKeyOptions defaults;
	const bool timing = o.action == ModelCurveKeyAction::Retime || o.action == ModelCurveKeyAction::Duplicate;
	if (int(o.action) < 0 || int(o.action) > int(ModelCurveKeyAction::Reduce) || o.selection.isEmpty() ||
		(!timing && (o.pivot != 0 || o.factor != 1 || o.offset != 0 || o.snap)) ||
		(o.action != ModelCurveKeyAction::Reduce && o.tolerance != defaults.tolerance))
		return fail(error, Text::tr("Choose keys and settings that apply to the selected key operation."));
	for (double number : {o.pivot, o.factor, o.offset, o.tolerance[0], o.tolerance[1], o.tolerance[2]})
		if (!std::isfinite(number) || std::abs(number) > 1000000)
			return fail(error, Text::tr("Key timing and error limits must be finite and within one million units."));
	if (o.factor <= 0 || o.pivot < 0 || o.pivot >= frameCount ||
		std::any_of(o.tolerance.begin(), o.tolerance.end(), [](double n) { return n < 0; }))
		return fail(error, Text::tr("Time scale must be positive, pivot inside the clip and error limits nonnegative."));
	auto candidate = *groups;
	QSet<QString> names;
	for (const auto &group : candidate)
	{
		if (!work.check() || !validateModelCurveGroup(group, frameCount, error))
			return false;
		if (names.contains(group.joint))
			return fail(error, Text::tr("Curve groups must have unique joint names."));
		names.insert(group.joint);
	}
	QSet<QPair<QString, int>> seen;
	ModelCurveKeyReport result;
	qint64 selectionCount = 0;
	for (const auto &selection : o.selection)
	{
		if (!work.check())
			return false;
		const auto it = std::find_if(candidate.begin(), candidate.end(), [&](const auto &g) { return g.joint == selection.joint; });
		selectionCount += selection.samples.size();
		if (it == candidate.end() || selection.channel < 0 || selection.channel >= 9 || selection.samples.isEmpty() ||
			seen.contains({selection.joint, selection.channel}) || selectionCount > modelCurveMaxKeys)
			return fail(error, Text::tr("Select distinct existing joint channels and at least one key, within the key budget."));
		seen.insert({selection.joint, selection.channel});
		auto &keys = it->channels[selection.channel];
		const auto original = keys;
		QVector<int> indices;
		int previous = -1;
		for (int sample : selection.samples)
		{
			if (!work.step())
				return false;
			const auto key = std::lower_bound(original.cbegin(), original.cend(), sample, [](const auto &k, int f) { return k.frame < f; });
			if (sample <= previous || key == original.cend() || key->frame != sample)
				return fail(error, Text::tr("Selected sample indices must be increasing, distinct existing keys."));
			indices.append(int(key - original.cbegin()));
			previous = sample;
		}
		if (o.action == ModelCurveKeyAction::Reduce)
		{
			if (!reduce(&keys, indices, o.tolerance[selection.channel / 3], error, control))
				return false;
		}
		else
		{
			QSet<int> selected(indices.begin(), indices.end());
			QVector<ModelPoseCurveKey> changed;
			for (int i = 0; i < original.size(); ++i)
			{
				if (!work.step())
					return false;
				if (!selected.contains(i) || o.action == ModelCurveKeyAction::Duplicate)
					changed.append(original[i]);
				if (!selected.contains(i) || !timing)
					continue;
				auto key = original[i];
				const double time = o.pivot + (key.frame - o.pivot) * o.factor + o.offset, rounded = std::round(time);
				if (!std::isfinite(time) || rounded < 0 || rounded >= frameCount || (!o.snap && std::abs(time - rounded) > 1e-7))
					return fail(
						error,
						Text::tr("Retimed keys must land inside the clip on integer samples. Enable rounding for fractional results."));
				key.frame = int(rounded);
				if (key.tangent == ModelCurveTangent::Free)
				{
					key.inSlope /= o.factor;
					key.outSlope /= o.factor;
				}
				changed.append(key);
			}
			std::sort(changed.begin(), changed.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
			if (changed.isEmpty())
				return fail(error, Text::tr("Keep at least one key in each channel; bake the joint to remove its curves."));
			for (int i = 1; i < changed.size(); ++i)
				if (changed[i - 1].frame == changed[i].frame)
					return fail(error, Text::tr("The operation would place two keys at the same sample. No keys were replaced."));
			keys = std::move(changed);
		}
		if (!validateModelCurveGroup(*it, frameCount, error))
			return false;
		double maximum = 0;
		if (!modelCurveDifferenceBound(original, keys, 0, frameCount - 1, &maximum, error, control))
			return false;
		if (o.action == ModelCurveKeyAction::Reduce && maximum > o.tolerance[selection.channel / 3])
			return fail(error, Text::tr("Reduction could not preserve the continuous curve within the chosen error limit."));
		result.channels.append({selection.joint, selection.channel, int(indices.size()), int(original.size()), int(keys.size()), maximum});
	}
	qint64 total = 0;
	for (const auto &group : candidate)
		for (const auto &channel : group.channels)
			total += channel.size();
	if (total > modelCurveMaxKeys)
		return fail(error, Text::tr("The result exceeds the 1,048,576 animation key budget."));
	if (!work.check())
		return false;
	*groups = std::move(candidate);
	if (report)
		*report = std::move(result);
	return true;
}
bool applyModelCurveKeys(ModelMesh *mesh, const ModelEdit &edit, ModelCurveKeyReport *report, QString *error,
						 const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || !work.check() || !validateModelSkeleton(*mesh, error, control))
		return false;
	const auto &o = edit.curveKeys;
	if (edit.kind != ModelEditKind::EditCurveKeys || !defaultModelSkeletalOptions(edit.skeletal) || o.clip < 0 ||
		o.clip >= mesh->skeleton.clips.size())
		return fail(error, Text::tr("Choose an existing skeletal clip for key editing without sampled-pose options."));
	const auto original = mesh->skeleton.clips[o.clip].curves;
	auto groups = original;
	ModelCurveKeyReport result;
	if (!editModelCurveKeys(&groups, int(mesh->skeleton.clips[o.clip].frames.size()), o, &result, error, control))
		return false;
	if (!replaceModelCurveGroups(mesh, o.clip, groups, error, control))
		return false;
	if (!work.check())
		return false;
	if (report)
		*report = std::move(result);
	return true;
}
bool replaceModelCurveGroups(ModelMesh *mesh, int clip, const QVector<ModelPoseCurves> &groups, QString *error,
							 const ModelWorkControl &control)
{
	ModelWorkProgress work(control, ModelWorkPhase::Editing, error);
	if (!mesh || clip < 0 || clip >= mesh->skeleton.clips.size() || !validateModelSkeleton(*mesh, error, control))
		return false;
	const auto original = mesh->skeleton.clips[clip].curves;
	if (groups.size() != original.size())
		return fail(error, Text::tr("Curve replacement requires matching captured joint groups."));
	for (int i = 0; i < groups.size(); ++i)
		if (groups[i].joint != original[i].joint ||
			!validateModelCurveGroup(groups[i], int(mesh->skeleton.clips[clip].frames.size()), error))
			return fail(error, Text::tr("Replacement curves must preserve valid joint groups and sample bounds."));
	if (groups != original)
	{
		QVector<int> groupForJoint(mesh->skeleton.joints.size(), -1);
		for (int i = 0; i < groups.size(); ++i)
			if (groups[i] != original[i])
				groupForJoint[modelJointIndex(mesh->skeleton, groups[i].joint, Qt::CaseSensitive)] = i;
		auto samples = mesh->skeleton.clips[clip].frames;
		for (int frame = 0; frame < samples.size(); ++frame)
		{
			const auto before = samples[frame];
			QVector<bool> changed(before.size(), false);
			QVector<ModelJointMatrix> deltas(before.size());
			for (int joint = 0; joint < before.size(); ++joint)
			{
				if (!work.step())
					return false;
				const int parent = mesh->skeleton.joints[joint].parent, group = groupForJoint[joint];
				const bool localChanged = group >= 0 && modelJointMatrixDistance(evaluateModelPoseCurves(groups[group], frame),
																				 evaluateModelPoseCurves(original[group], frame)) != 0;
				if (localChanged)
				{
					const auto local = evaluateModelPoseCurves(groups[group], frame);
					samples[frame][joint] = parent < 0 ? local : modelJointMultiply(samples[frame][parent], local);
				}
				else if (parent >= 0 && changed[parent])
					samples[frame][joint] = modelJointMultiply(deltas[parent], before[joint]);
				changed[joint] = modelJointMatrixDistance(samples[frame][joint], before[joint]) != 0;
				if (changed[joint])
				{
					bool ok = false;
					const auto inverse = modelJointInverse(before[joint], &ok);
					if (!ok)
						return fail(error, Text::tr("An existing joint pose is singular and cannot move its descendants."));
					deltas[joint] = modelJointMultiply(samples[frame][joint], inverse);
				}
			}
		}
		if (!replaceModelSkeletalSamples(mesh, clip, 0, samples, error, control, &groups))
			return false;
	}
	return work.check();
}
QJsonArray modelCurveKeyReportJson(const ModelCurveKeyReport &report)
{
	QJsonArray result;
	for (const auto &c : report.channels)
		result.append(QJsonObject{{"joint", c.joint},
								  {"channel", modelCurveChannelId(c.channel)},
								  {"selectedKeys", c.selected},
								  {"beforeKeys", c.before},
								  {"afterKeys", c.after},
								  {"maximumChange", c.maximumChange}});
	return result;
}
bool describeModelCurveKeyChanges(const QVector<ModelPoseCurves> &before, const QVector<ModelPoseCurves> &after, int frameCount,
								  const QVector<ModelCurveKeySelection> &selection, ModelCurveKeyReport *report, QString *error,
								  const ModelWorkControl &control)
{
	ModelCurveKeyReport result;
	for (const auto &s : selection)
	{
		const auto a = std::find_if(before.cbegin(), before.cend(), [&](const auto &g) { return g.joint == s.joint; });
		const auto b = std::find_if(after.cbegin(), after.cend(), [&](const auto &g) { return g.joint == s.joint; });
		if (!report || a == before.cend() || b == after.cend() || s.channel < 0 || s.channel >= 9)
			return fail(error, Text::tr("Curve change reports require matching valid joint channels."));
		double maximum = 0;
		if (!modelCurveDifferenceBound(a->channels[s.channel], b->channels[s.channel], 0, frameCount - 1, &maximum, error, control))
			return false;
		result.channels.append(
			{s.joint, s.channel, int(s.samples.size()), int(a->channels[s.channel].size()), int(b->channels[s.channel].size()), maximum});
	}
	if (!report)
		return false;
	*report = std::move(result);
	return true;
}
} // namespace vibestudio
