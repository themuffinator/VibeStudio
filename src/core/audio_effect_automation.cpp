#include "core/audio_effects.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
#include <limits>

namespace vibestudio
{
QString validateAudioEffectAutomation(const AudioEffectChain &chain, int rate, const AudioEffectAutomation &lanes)
{
	const auto chainError = validateAudioEffects(chain, rate);
	if (!chainError.isEmpty())
		return chainError;
	if (lanes.size() > AudioEffectChainLimit * 12)
		return QCoreApplication::translate("AudioEffects", "Effect automation exceeds 96 parameter lanes per chain.");
	QSet<QString> targets;
	qsizetype total = 0;
	for (const auto &lane : lanes) {
		const auto effect =
		    std::find_if(chain.cbegin(), chain.cend(), [&](const auto &item) { return item.id == lane.effectId; });
		const auto key = lane.effectId + QChar(0) + lane.parameter;
		if (effect == chain.cend() || targets.contains(key))
			return QCoreApplication::translate(
			    "AudioEffects", "Automation must target an existing effect and each parameter only once.");
		targets.insert(key);
		bool valid = false;
		for (const auto &parameter : audioEffectParameters(effect->type, rate))
			if (parameter.key == lane.parameter && parameter.automatable)
				valid = validAudioAutomation(lane.points, parameter.minimum, parameter.maximum);
		total += lane.points.size();
		if (!valid || total > AudioEffectAutomationPointLimit)
			return QCoreApplication::translate("AudioEffects",
			                                   "Effect automation requires supported parameters, ordered frames, "
			                                   "bounded values and at most 4096 points per lane / 65536 per session.");
	}
	return {};
}
void retainAudioEffectAutomation(const AudioEffectChain &chain, AudioEffectAutomation *lanes)
{
	if (!lanes)
		return;
	lanes->erase(std::remove_if(lanes->begin(), lanes->end(),
	                            [&](const auto &lane) {
		                            return std::none_of(chain.cbegin(), chain.cend(), [&](const auto &effect) {
			                            return effect.id == lane.effectId && effect.parameters.contains(lane.parameter);
		                            });
	                            }),
	             lanes->end());
}
QJsonArray audioEffectAutomationToJson(const AudioEffectAutomation &lanes)
{
	QJsonArray result;
	for (const auto &lane : lanes)
		result.append(QJsonObject{{"effect", lane.effectId},
		                          {"parameter", lane.parameter},
		                          {"enabled", lane.enabled},
		                          {"points", audioAutomationToJson(lane.points)}});
	return result;
}
bool audioEffectAutomationFromJson(const QJsonValue &value, AudioEffectAutomation *lanes, bool legacyShape)
{
	if (!lanes || !value.isArray() || value.toArray().size() > AudioEffectChainLimit * 12)
		return false;
	AudioEffectAutomation next;
	qsizetype total = 0;
	for (const auto &item : value.toArray()) {
		const auto object = item.toObject();
		AudioEffectAutomationLane lane;
		if (!item.isObject() || object.size() != 4 || !object.value("effect").isString() ||
		    !object.value("parameter").isString() || !object.value("enabled").isBool() ||
		    !audioAutomationFromJson(object.value("points"), &lane.points, -std::numeric_limits<double>::max(),
		                             std::numeric_limits<double>::max(), false, legacyShape))
			return false;
		lane.effectId = object.value("effect").toString();
		lane.parameter = object.value("parameter").toString();
		lane.enabled = object.value("enabled").toBool();
		total += lane.points.size();
		if (total > AudioEffectAutomationPointLimit)
			return false;
		next.append(std::move(lane));
	}
	*lanes = std::move(next);
	return true;
}
} // namespace vibestudio
