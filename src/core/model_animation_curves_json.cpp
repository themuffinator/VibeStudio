#include "core/model_animation_curves.h"
#include <QCoreApplication>
#include <cmath>
namespace vibestudio
{
namespace
{
QStringList modes()
{
	return {"step", "linear", "cubic", "step-next"};
}
QStringList tangents()
{
	return {"auto", "clamped", "free", "flat"};
}
bool fail(QString *error)
{
	if (error)
		*error =
			QCoreApplication::translate("ModelAnimationCurves", "The stored animation curves are malformed or exceed their key budget.");
	return false;
}
} // namespace
QJsonObject modelPoseCurvesJson(const ModelPoseCurves &group)
{
	QJsonObject channels;
	for (int c = 0; c < 9; ++c)
	{
		QJsonArray keys;
		for (const auto &k : group.channels[c])
			keys.append(
				QJsonArray{k.frame, k.value, modes().value(int(k.interpolation)), tangents().value(int(k.tangent)), k.inSlope, k.outSlope});
		channels.insert(modelCurveChannelId(c), keys);
	}
	return {{"joint", group.joint}, {"channels", channels}};
}
bool modelPoseCurvesFromJson(const QJsonObject &object, int frameCount, ModelPoseCurves *group, QString *error)
{
	if (!group || object.size() != 2 || !object["joint"].isString() || !object["channels"].isObject())
		return fail(error);
	const auto channels = object["channels"].toObject();
	if (channels.size() != 9)
		return fail(error);
	ModelPoseCurves result;
	result.joint = object["joint"].toString();
	qint64 count = 0;
	for (int c = 0; c < 9; ++c)
	{
		const auto value = channels[modelCurveChannelId(c)];
		if (!value.isArray())
			return fail(error);
		const auto keys = value.toArray();
		count += keys.size();
		if (keys.isEmpty() || count > modelCurveMaxKeys || keys.size() > frameCount)
			return fail(error);
		for (const auto &valueKey : keys)
		{
			if (!valueKey.isArray())
				return fail(error);
			const auto key = valueKey.toArray();
			if (key.size() != 6 || !key[0].isDouble() || key[0].toDouble() < 0 || key[0].toDouble() >= frameCount ||
				std::floor(key[0].toDouble()) != key[0].toDouble() || !key[1].isDouble() || !key[2].isString() || !key[3].isString() ||
				!key[4].isDouble() || !key[5].isDouble())
				return fail(error);
			const int mode = modes().indexOf(key[2].toString()), tangent = tangents().indexOf(key[3].toString());
			if (mode < 0 || tangent < 0)
				return fail(error);
			result.channels[c].append({key[0].toInt(), key[1].toDouble(), key[4].toDouble(), key[5].toDouble(),
									   ModelCurveInterpolation(mode), ModelCurveTangent(tangent)});
		}
	}
	if (!validateModelCurveGroup(result, frameCount, error))
		return false;
	*group = std::move(result);
	return true;
}
QJsonArray modelClipCurvesJson(const QVector<ModelPoseCurves> &groups)
{
	QJsonArray result;
	for (const auto &group : groups)
		result.append(modelPoseCurvesJson(group));
	return result;
}
bool modelClipCurvesFromJson(const QJsonValue &value, int frameCount, QVector<ModelPoseCurves> *groups, QString *error)
{
	if (!groups || !value.isArray())
		return fail(error);
	const auto array = value.toArray();
	if (array.size() > 4096)
		return fail(error);
	QVector<ModelPoseCurves> result;
	QSet<QString> names;
	qint64 count = 0;
	for (const auto &entry : array)
	{
		ModelPoseCurves group;
		if (!entry.isObject())
			return fail(error);
		if (!modelPoseCurvesFromJson(entry.toObject(), frameCount, &group, error))
			return false;
		if (names.contains(group.joint))
			return fail(error);
		names.insert(group.joint);
		for (const auto &channel : group.channels)
			count += channel.size();
		if (count > modelCurveMaxKeys)
			return fail(error);
		result.append(std::move(group));
	}
	*groups = std::move(result);
	return true;
}
} // namespace vibestudio
