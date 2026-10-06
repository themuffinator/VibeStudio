#include "core/audio_effects.h"
#include "core/audio_latency.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
QString tr(const char *text) { return QCoreApplication::translate("AudioEffects", text); }
double amplitude(double db) { return std::pow(10.0, db / 20); }
double pole(double milliseconds, int rate) { return std::exp(-1000.0 / (milliseconds * rate)); }
bool filterType(const QString &type)
{
	return type == "low-pass" || type == "high-pass" || type == "peak-eq" || type == "notch" || type == "low-shelf" ||
	       type == "high-shelf";
}
size_t delayFrames(const AudioEffect &effect, int rate)
{
	if (effect.type == "chorus" || effect.type == "flanger")
		return size_t(std::ceil(std::max(
		           1.0, (effect.parameters.value("minDelayMs") + effect.parameters.value("depthMs")) * rate / 1000))) +
		       1;
	return size_t(std::ceil(std::max({1.0, effect.parameters.value("leftMs") * rate / 1000,
	                                  effect.parameters.value("rightMs") * rate / 1000}))) +
	       1;
}
AudioEffect maximumParameters(const AudioEffect &effect, const AudioEffectAutomation &lanes)
{
	auto maximum = effect;
	for (const auto &lane : lanes)
		if (lane.effectId == effect.id && lane.enabled)
			for (const auto &point : lane.points)
				maximum.parameters[lane.parameter] = std::max(maximum.parameters.value(lane.parameter), point.value);
	return maximum;
}
// Original C++ implementation of the mathematical biquad coefficients in
// Robert Bristow-Johnson / W3C Audio EQ Cookbook, 2021-06-08, section 2.
// https://www.w3.org/TR/2021/NOTE-audio-eq-cookbook-20210608/
// W3C Software and Document License 2015; attribution/notices in docs/CREDITS.md.
std::array<double, 5> coefficients(int type, const std::array<double, 12> &values, int rate)
{
	const double omega = 2 * std::numbers::pi * values[0] / rate;
	const double c = std::cos(omega), alpha = std::sin(omega) / (2 * values[1]);
	double a0 = 1 + alpha, a1 = -2 * c, a2 = 1 - alpha, b0 = 1, b1 = -2 * c, b2 = 1;
	if (type == 0) {
		b0 = b2 = (1 - c) / 2;
		b1 = 1 - c;
	} else if (type == 1) {
		b0 = b2 = (1 + c) / 2;
		b1 = -(1 + c);
	} else if (type == 2) {
		const double a = std::pow(10.0, values[2] / 40);
		b0 = 1 + alpha * a;
		b2 = 1 - alpha * a;
		a0 = 1 + alpha / a;
		a2 = 1 - alpha / a;
	} else if (type == 4 || type == 5) {
		const double a = std::pow(10.0, values[2] / 40), r = 2 * std::sqrt(a) * alpha;
		const double sign = type == 4 ? 1 : -1;
		b0 = a * ((a + 1) - sign * (a - 1) * c + r);
		b1 = 2 * a * (sign * (a - 1) - (a + 1) * c);
		b2 = a * ((a + 1) - sign * (a - 1) * c - r);
		a0 = (a + 1) + sign * (a - 1) * c + r;
		a1 = -2 * (sign * (a - 1) + (a + 1) * c);
		a2 = (a + 1) + sign * (a - 1) * c - r;
	}
	return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}
} // namespace
QStringList audioEffectTypes()
{
	return {"gain",   "low-pass",   "high-pass", "peak-eq", "low-shelf", "high-shelf",
	        "notch",  "compressor", "gate",      "limiter", "delay",     "saturation",
	        "reverb", "chorus",     "flanger",   "tremolo", "phaser",    "lookahead-limiter"};
}
QString audioEffectName(const QString &type)
{
	if (type == "gain")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Gain"));
	if (type == "low-pass")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Low-pass filter"));
	if (type == "high-pass")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "High-pass filter"));
	if (type == "peak-eq")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Parametric EQ"));
	if (type == "low-shelf")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Low-shelf EQ"));
	if (type == "high-shelf")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "High-shelf EQ"));
	if (type == "notch")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Notch filter"));
	if (type == "compressor")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Compressor"));
	if (type == "gate")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Noise gate"));
	if (type == "limiter")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Sample peak limiter"));
	if (type == "lookahead-limiter")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Lookahead sample peak limiter"));
	if (type == "delay")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Stereo delay"));
	if (type == "saturation")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Soft saturation"));
	if (type == "reverb")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Stereo reverb"));
	if (type == "chorus")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Chorus"));
	if (type == "flanger")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Flanger"));
	if (type == "tremolo")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Tremolo"));
	if (type == "phaser")
		return tr(QT_TRANSLATE_NOOP("AudioEffects", "Phaser"));
	return type;
}
QVector<AudioEffectParameter> audioEffectParameters(const QString &type, int rate)
{
	QVector<AudioEffectParameter> result;
	const auto add = [&](const char *key, const char *label, double low, double high, double initial,
	                     int decimals = 2) {
		result.append({QLatin1String(key), tr(label), low, high, initial, decimals});
	};
	if (filterType(type)) {
		add("frequencyHz", QT_TRANSLATE_NOOP("AudioEffects", "Frequency (Hz)"), std::min(10.0, rate * .01),
		    std::min(20000.0, rate * .49), std::min(1000.0, rate * .1));
		add("q", QT_TRANSLATE_NOOP("AudioEffects", "Resonance (Q)"), .1, 18, 1 / std::sqrt(2.0), 4);
		if (type.endsWith("shelf") || type == "peak-eq")
			add("gainDb", QT_TRANSLATE_NOOP("AudioEffects", "EQ gain (dB)"), -24, 24, 0);
	} else if (type == "gain")
		add("gainDb", QT_TRANSLATE_NOOP("AudioEffects", "Gain (dB)"), -96, 24, 0);
	else if (type == "compressor" || type == "gate") {
		add("thresholdDb", QT_TRANSLATE_NOOP("AudioEffects", "Threshold (dBFS)"), -96, 0, type == "gate" ? -48 : -18);
		add("attackMs", QT_TRANSLATE_NOOP("AudioEffects", "Attack (ms)"), .1, 200, type == "gate" ? 1 : 10);
		add("releaseMs", QT_TRANSLATE_NOOP("AudioEffects", "Release (ms)"), 1, 5000, 100);
		if (type == "compressor") {
			add("ratio", QT_TRANSLATE_NOOP("AudioEffects", "Ratio"), 1, 40, 4);
			add("kneeDb", QT_TRANSLATE_NOOP("AudioEffects", "Knee (dB)"), 0, 24, 6);
			add("makeupDb", QT_TRANSLATE_NOOP("AudioEffects", "Makeup gain (dB)"), -24, 24, 0);
			add("mix", QT_TRANSLATE_NOOP("AudioEffects", "Wet mix (0–1)"), 0, 1, 1, 3);
		} else {
			add("rangeDb", QT_TRANSLATE_NOOP("AudioEffects", "Closed attenuation (dB)"), 0, 96, 80);
			add("holdMs", QT_TRANSLATE_NOOP("AudioEffects", "Hold (ms)"), 0, 2000, 20);
			add("hysteresisDb", QT_TRANSLATE_NOOP("AudioEffects", "Hysteresis (dB)"), 0, 24, 3);
		}
	} else if (type == "limiter" || type == "lookahead-limiter") {
		add("ceilingDb", QT_TRANSLATE_NOOP("AudioEffects", "Ceiling (dBFS)"), -24, 0, -1);
		add("releaseMs", QT_TRANSLATE_NOOP("AudioEffects", "Release (ms)"), 1, 5000, 100);
		if (type == "lookahead-limiter") {
			add("lookaheadMs", QT_TRANSLATE_NOOP("AudioEffects", "Lookahead (ms)"), 0, 20, 5, 3);
			result.last().automatable = false;
			add("attackMs", QT_TRANSLATE_NOOP("AudioEffects", "Attack (ms)"), .01, 20, 1, 3);
		}
	} else if (type == "delay") {
		add("leftMs", QT_TRANSLATE_NOOP("AudioEffects", "Left delay (ms)"), 1, 2000, 250);
		add("rightMs", QT_TRANSLATE_NOOP("AudioEffects", "Right delay (ms)"), 1, 2000, 375);
		add("feedback", QT_TRANSLATE_NOOP("AudioEffects", "Feedback (0–0.95)"), 0, .95, .35, 3);
		add("mix", QT_TRANSLATE_NOOP("AudioEffects", "Wet mix (0–1)"), 0, 1, .25, 3);
	} else if (type == "saturation") {
		add("driveDb", QT_TRANSLATE_NOOP("AudioEffects", "Drive (dB)"), 0, 36, 6);
		add("outputDb", QT_TRANSLATE_NOOP("AudioEffects", "Output gain (dB)"), -24, 12, 0);
		add("mix", QT_TRANSLATE_NOOP("AudioEffects", "Wet mix (0–1)"), 0, 1, 1, 3);
	} else if (type == "reverb") {
		add("roomSize", QT_TRANSLATE_NOOP("AudioEffects", "Room size multiplier"), .25, 2, 1, 3);
		add("decaySeconds", QT_TRANSLATE_NOOP("AudioEffects", "Low-frequency decay (s)"), .1, 30, 1.5, 3);
		add("dampingHz", QT_TRANSLATE_NOOP("AudioEffects", "Damping cutoff (Hz)"), std::min(20.0, rate * .01),
		    std::min(20000.0, rate * .49), std::min(6000.0, rate * .25));
		add("preDelayMs", QT_TRANSLATE_NOOP("AudioEffects", "Pre-delay (ms)"), 0, 250, 10);
		add("width", QT_TRANSLATE_NOOP("AudioEffects", "Stereo width (0–1)"), 0, 1, 1, 3);
		add("mix", QT_TRANSLATE_NOOP("AudioEffects", "Wet mix (0–1)"), 0, 1, .2, 3);
	} else if (type == "chorus" || type == "flanger" || type == "tremolo" || type == "phaser") {
		const double speed = type == "tremolo" ? 4 : type == "flanger" ? .2 : .6;
		add("rateHz", QT_TRANSLATE_NOOP("AudioEffects", "Modulation rate (Hz)"), .01, std::min(20.0, rate * .25),
		    std::min(speed, rate * .25), 3);
		add("stereoPhaseDeg", QT_TRANSLATE_NOOP("AudioEffects", "Stereo phase offset (degrees)"), 0, 360,
		    type == "tremolo" ? 0 : 90);
		if (type == "chorus" || type == "flanger") {
			add("minDelayMs", QT_TRANSLATE_NOOP("AudioEffects", "Minimum delay (ms)"), type == "chorus" ? 1 : .01,
			    type == "chorus" ? 50 : 10, type == "chorus" ? 10 : .1, 3);
			add("depthMs", QT_TRANSLATE_NOOP("AudioEffects", "Delay sweep (ms)"), 0, type == "chorus" ? 20 : 10,
			    type == "chorus" ? 5 : 2, 3);
			add("feedback", QT_TRANSLATE_NOOP("AudioEffects", "Feedback (−0.95–0.95)"), -.95, .95,
			    type == "chorus" ? 0 : .5, 3);
		} else if (type == "phaser") {
			add("frequencyHz", QT_TRANSLATE_NOOP("AudioEffects", "Center frequency (Hz)"), std::min(20.0, rate * .01),
			    std::min(5000.0, rate * .25), std::min(1000.0, rate * .1));
			add("depthOctaves", QT_TRANSLATE_NOOP("AudioEffects", "Sweep depth (octaves)"), 0, 4, 2, 3);
			add("feedback", QT_TRANSLATE_NOOP("AudioEffects", "Feedback (−0.9–0.9)"), -.9, .9, .3, 3);
		} else
			add("depth", QT_TRANSLATE_NOOP("AudioEffects", "Modulation depth (0–1)"), 0, 1, .5, 3);
		if (type != "tremolo")
			add("mix", QT_TRANSLATE_NOOP("AudioEffects", "Wet mix (0–1)"), 0, 1, .5, 3);
	}
	return result;
}
AudioEffect makeAudioEffect(const QString &type, int sampleRate)
{
	AudioEffect effect;
	effect.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	effect.type = type;
	for (const auto &parameter : audioEffectParameters(type, sampleRate))
		effect.parameters.insert(parameter.key, parameter.initial);
	return effect;
}
QString validateAudioEffects(const AudioEffectChain &chain, int rate)
{
	if (rate < 1 || rate > 384000 || chain.size() > AudioEffectChainLimit)
		return tr(QT_TRANSLATE_NOOP("AudioEffects",
		                            "Effects require a valid sample rate and at most eight inserts per chain."));
	QSet<QString> ids;
	for (const auto &effect : chain) {
		if (effect.id.trimmed().isEmpty() || effect.id.size() > 64 || !effect.id.isValidUtf16() ||
		    effect.id.contains(QChar(0)) || ids.contains(effect.id) || !audioEffectTypes().contains(effect.type))
			return tr(
			    QT_TRANSLATE_NOOP("AudioEffects", "Effects require distinct valid IDs and supported processor types."));
		ids.insert(effect.id);
		const auto parameters = audioEffectParameters(effect.type, rate);
		if (effect.parameters.size() != parameters.size())
			return tr(QT_TRANSLATE_NOOP("AudioEffects", "An effect has missing or unknown parameters."));
		for (const auto &parameter : parameters) {
			const double value = effect.parameters.value(parameter.key);
			if (!effect.parameters.contains(parameter.key) || !std::isfinite(value) || value < parameter.minimum ||
			    value > parameter.maximum)
				return tr(QT_TRANSLATE_NOOP("AudioEffects", "Effect parameter %1 must be between %2 and %3."))
				    .arg(parameter.key)
				    .arg(parameter.minimum)
				    .arg(parameter.maximum);
		}
	}
	return {};
}
quint64 audioEffectMemoryBytes(const AudioEffectChain &chain, int rate, const AudioEffectAutomation &lanes)
{
	if (!validateAudioEffectAutomation(chain, rate, lanes).isEmpty())
		return AudioEffectMemoryLimit + 1;
	quint64 bytes = quint64(chain.size()) * 512;
	for (const auto &lane : lanes)
		bytes += 64 + quint64(lane.points.size()) * sizeof(AudioAutomationPoint);
	for (const auto &effect : chain) {
		const auto maximum = maximumParameters(effect, lanes);
		if (effect.enabled && (effect.type == "delay" || effect.type == "chorus" || effect.type == "flanger"))
			bytes += quint64(delayFrames(maximum, rate)) * 2 * sizeof(double);
		if (effect.enabled && effect.type == "reverb")
			bytes += AudioReverb::memoryBytes(rate, maximum.parameters.value("roomSize"),
			                                  maximum.parameters.value("preDelayMs"));
		if (effect.enabled && effect.type == "lookahead-limiter")
			bytes += sizeof(AudioLookaheadLimiter) + AudioLookaheadLimiter::memoryBytes(int(std::ceil(
			                                             effect.parameters.value("lookaheadMs") * rate / 1000)));
	}
	return bytes;
}
int audioEffectLatencyFrames(const AudioEffectChain &chain, int rate)
{
	if (!validateAudioEffects(chain, rate).isEmpty())
		return -1;
	int frames = 0;
	for (const auto &effect : chain)
		if (effect.enabled && effect.type == "lookahead-limiter")
			frames += int(std::ceil(effect.parameters.value("lookaheadMs") * rate / 1000));
	return frames;
}
bool audioEffectsEnabled(const AudioEffectChain &chain)
{
	return std::any_of(chain.cbegin(), chain.cend(), [](const auto &effect) { return effect.enabled; });
}
bool audioEffectsHaveTail(const AudioEffectChain &chain, const AudioEffectAutomation &lanes)
{
	return std::any_of(chain.cbegin(), chain.cend(), [&](const auto &effect) {
		const bool automatedGain = std::any_of(lanes.cbegin(), lanes.cend(), [&](const auto &lane) {
			return lane.enabled && lane.effectId == effect.id && lane.parameter == "gainDb" &&
			       std::any_of(lane.points.cbegin(), lane.points.cend(),
			                   [](const auto &point) { return point.value != 0; });
		});
		return effect.enabled &&
		       (effect.type == "delay" || effect.type == "reverb" || effect.type == "chorus" ||
		        effect.type == "flanger" || effect.type == "phaser" ||
		        (filterType(effect.type) &&
		         (!effect.parameters.contains("gainDb") || effect.parameters.value("gainDb") != 0 || automatedGain)));
	});
}
QJsonArray audioEffectsToJson(const AudioEffectChain &chain)
{
	QJsonArray result;
	for (const auto &effect : chain) {
		QJsonObject parameters;
		for (auto it = effect.parameters.cbegin(); it != effect.parameters.cend(); ++it)
			parameters.insert(it.key(), it.value());
		result.append(QJsonObject{
		    {"id", effect.id}, {"type", effect.type}, {"enabled", effect.enabled}, {"parameters", parameters}});
	}
	return result;
}
bool audioEffectsFromJson(const QJsonValue &value, AudioEffectChain *chain)
{
	if (!chain || !value.isArray() || value.toArray().size() > AudioEffectChainLimit)
		return false;
	AudioEffectChain result;
	for (const auto &item : value.toArray()) {
		const auto object = item.toObject();
		if (!item.isObject() || object.size() != 4 || !object.value("id").isString() ||
		    !object.value("type").isString() || !object.value("enabled").isBool() ||
		    !object.value("parameters").isObject() || object.value("parameters").toObject().size() > 12)
			return false;
		AudioEffect effect{
		    object.value("id").toString(), object.value("type").toString(), object.value("enabled").toBool(), {}};
		const auto parameters = object.value("parameters").toObject();
		for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
			if (!it.value().isDouble() || !std::isfinite(it.value().toDouble()))
				return false;
			effect.parameters.insert(it.key(), it.value().toDouble());
		}
		result.append(effect);
	}
	*chain = std::move(result);
	return true;
}
bool AudioEffectsProcessor::prepare(const AudioEffectChain &chain, int rate, QString *error,
                                    const AudioEffectAutomation &lanes, int inputLatency, AudioTimelineLoop loop)
{
	static_assert(sizeof(Unit) <= 512, "Update the conservative descriptor memory budget when adding state.");
	static_assert(sizeof(Unit::Lane) <= 64);
	m_units = std::vector<Unit>{};
	m_latencyFrames = 0;
	m_loop = loop;
	const auto issue = validateAudioEffectAutomation(chain, rate, lanes);
	if (error)
		*error = issue;
	if (!issue.isEmpty())
		return false;
	const int latency = audioEffectLatencyFrames(chain, rate);
	if (!loop.valid() || inputLatency < 0 || inputLatency > AudioProcessingLatencyLimit - latency) {
		if (error)
			*error = tr(QT_TRANSLATE_NOOP("AudioEffects", "Effect processing latency exceeds the supported bounds."));
		return false;
	}
	if (audioEffectMemoryBytes(chain, rate, lanes) > AudioEffectMemoryLimit) {
		if (error)
			*error = tr(QT_TRANSLATE_NOOP("AudioEffects", "Effect state and automation exceed 128 MiB."));
		return false;
	}
	m_sampleRate = rate;
	m_units.reserve(size_t(chain.size()));
	const QStringList filters{"low-pass", "high-pass", "peak-eq", "notch", "low-shelf", "high-shelf"};
	for (const auto &effect : chain) {
		if (!effect.enabled)
			continue;
		Unit unit;
		unit.inputLatency = inputLatency + m_latencyFrames;
		const auto schema = audioEffectParameters(effect.type, rate);
		unit.lanes.reserve(size_t(std::count_if(lanes.cbegin(), lanes.cend(), [&](const auto &lane) {
			return lane.effectId == effect.id && lane.enabled && !lane.points.isEmpty();
		})));
		for (qsizetype i = 0; i < schema.size(); ++i) {
			unit.physical[size_t(i)] = effect.parameters.value(schema[i].key);
			for (const auto &lane : lanes)
				if (lane.effectId == effect.id && lane.parameter == schema[i].key && lane.enabled &&
				    !lane.points.isEmpty())
					unit.lanes.push_back({size_t(i), lane.points, 0});
		}
		if (filterType(effect.type)) {
			if (effect.parameters.contains("gainDb") && effect.parameters.value("gainDb") == 0 && unit.lanes.empty())
				continue;
			unit.kind = Kind::Filter;
			unit.filter = int(filters.indexOf(effect.type));
		} else if (effect.type == "gain")
			unit.kind = Kind::Gain;
		else if (effect.type == "compressor")
			unit.kind = Kind::Compressor;
		else if (effect.type == "gate")
			unit.kind = Kind::Gate;
		else if (effect.type == "limiter")
			unit.kind = Kind::Limiter;
		else if (effect.type == "lookahead-limiter") {
			unit.kind = Kind::LookaheadLimiter;
			unit.latency = int(std::ceil(effect.parameters.value("lookaheadMs") * rate / 1000));
			unit.lookahead = std::make_unique<AudioLookaheadLimiter>();
			if (!unit.lookahead->prepare(unit.latency))
				return false;
			m_latencyFrames += unit.latency;
		} else if (effect.type == "saturation")
			unit.kind = Kind::Saturation;
		else if (effect.type == "tremolo")
			unit.kind = Kind::Tremolo;
		else if (effect.type == "phaser")
			unit.kind = Kind::Phaser;
		else if (effect.type == "delay" || effect.type == "chorus" || effect.type == "flanger") {
			unit.kind = effect.type == "delay" ? Kind::Delay : Kind::ModulatedDelay;
			unit.delay.resize(delayFrames(maximumParameters(effect, lanes), rate) * 2);
		} else if (effect.type == "reverb") {
			unit.kind = Kind::Reverb;
			unit.reverb = std::make_unique<AudioReverb>();
			const auto maximum = maximumParameters(effect, lanes);
			const auto &v = unit.physical;
			unit.reverb->prepare(rate, v[0], v[1], v[2], v[3], maximum.parameters.value("roomSize"),
			                     maximum.parameters.value("preDelayMs"));
		}
		configure(unit);
		m_units.push_back(std::move(unit));
	}
	reset();
	return true;
}
void AudioEffectsProcessor::configure(Unit &unit)
{
	const auto &v = unit.physical;
	const auto rate = m_sampleRate;
	if (unit.kind == Kind::Filter)
		unit.coefficients = coefficients(unit.filter, v, rate);
	else if (unit.kind == Kind::Gain)
		unit.p[0] = amplitude(v[0]);
	else if (unit.kind == Kind::Compressor)
		unit.p = {v[0], 1 - 1 / v[3], v[4], pole(v[1], rate), pole(v[2], rate), amplitude(v[5]), v[6]};
	else if (unit.kind == Kind::Gate)
		unit.p = {v[0], v[5], v[3], pole(v[1], rate), pole(v[2], rate), std::ceil(v[4] * rate / 1000)};
	else if (unit.kind == Kind::Limiter)
		unit.p = {amplitude(v[0]), pole(v[1], rate)};
	else if (unit.kind == Kind::LookaheadLimiter)
		unit.lookahead->setParameters(amplitude(v[0]), pole(v[3], rate), pole(v[1], rate));
	else if (unit.kind == Kind::Delay)
		unit.p = {std::max(1.0, v[0] * rate / 1000), std::max(1.0, v[1] * rate / 1000), v[2], v[3]};
	else if (unit.kind == Kind::Saturation)
		unit.p = {amplitude(v[0]), amplitude(v[1]), v[2]};
	else if (unit.kind == Kind::Reverb) {
		unit.reverb->setParameters(v[0], v[1], v[2], v[3]);
		unit.p = {v[4], v[5]};
	} else if (unit.kind == Kind::ModulatedDelay)
		unit.p = {v[2] * rate / 1000, v[3] * rate / 1000, v[4], v[5], v[0] / rate, v[1] * std::numbers::pi / 180};
	else if (unit.kind == Kind::Tremolo)
		unit.p = {v[0] / rate, v[2], v[1] * std::numbers::pi / 180};
	else if (unit.kind == Kind::Phaser)
		unit.p = {v[2], v[3], v[0] / rate, v[1] * std::numbers::pi / 180, double(rate), v[4], v[5]};
}
void AudioEffectsProcessor::automate(Unit &unit, qint64 frame)
{
	bool changed = false;
	for (auto &lane : unit.lanes) {
		const auto &points = std::as_const(lane.points);
		if (frame < points[lane.segment].frame) {
			const auto after = std::upper_bound(points.cbegin(), points.cend(), frame,
			                                    [](qint64 time, const auto &point) { return time < point.frame; });
			lane.segment = after == points.cbegin() ? 0 : after - points.cbegin() - 1;
		}
		while (lane.segment + 1 < points.size() && frame >= points[lane.segment + 1].frame)
			++lane.segment;
		const double value = lane.segment + 1 < points.size()
		                         ? audioAutomationSegment(points[lane.segment], points[lane.segment + 1], frame)
		                         : points.last().value;
		changed |= unit.physical[lane.parameter] != value;
		unit.physical[lane.parameter] = value;
	}
	if (changed)
		configure(unit);
}
void AudioEffectsProcessor::reset(qint64 firstFrame)
{
	m_nextFrame = firstFrame;
	m_resetFrame = firstFrame;
	for (auto &unit : m_units) {
		const auto authoredFirst = audioLoopFrame(firstFrame, m_loop);
		for (auto &lane : unit.lanes) {
			const auto after = std::upper_bound(lane.points.cbegin(), lane.points.cend(), authoredFirst,
			                                    [](qint64 time, const auto &point) { return time < point.frame; });
			lane.segment = after == lane.points.cbegin() ? 0 : (after - lane.points.cbegin() - 1);
		}
		automate(unit, authoredFirst);
		unit.z1 = unit.z2 = {};
		unit.allpass = {};
		unit.phase = 0;
		if (unit.reverb)
			unit.reverb->reset();
		if (unit.lookahead)
			unit.lookahead->reset();
		unit.envelope = unit.kind == Kind::Limiter ? 1 : unit.kind == Kind::Gate ? unit.p[2] : 0;
		unit.cursor = unit.validFrames = 0;
		unit.hold = 0;
		unit.open = false;
	}
}
bool AudioEffectsProcessor::process(std::span<double> stereo, qint64 firstFrame)
{
	if (firstFrame < -1 || stereo.size() % 2)
		return false;
	const qint64 first = firstFrame < 0 ? m_nextFrame : firstFrame;
	const qint64 processingLimit =
	    (m_loop.enabled ? AudioLoopClockLimit : AudioAutomationFrameLimit) + AudioProcessingLatencyLimit;
	if (first < 0 || first > processingLimit || stereo.size() / 2 > size_t(processingLimit - first))
		return false;
	if (first != m_nextFrame)
		reset(first);
	for (auto &unit : m_units) {
		const auto &p = unit.p;
		for (size_t at = 0; at < stereo.size(); at += 2) {
			const auto physicalFrame = first + qint64(at / 2);
			if (physicalFrame - m_resetFrame < unit.inputLatency) {
				stereo[at] = stereo[at + 1] = 0;
				continue;
			}
			automate(unit,
			         audioLoopFrame(std::max(m_resetFrame, physicalFrame - unit.inputLatency - unit.latency), m_loop));
			double left = stereo[at], right = stereo[at + 1];
			if (!std::isfinite(left) || !std::isfinite(right))
				return false;
			const double peak = std::max(std::abs(left), std::abs(right));
			if (unit.kind == Kind::Gain) {
				left *= p[0];
				right *= p[0];
			} else if (unit.kind == Kind::Filter) {
				const auto &c = unit.coefficients;
				const auto sample = [&](double input, size_t channel) {
					const double output = c[0] * input + unit.z1[channel];
					unit.z1[channel] = c[1] * input - c[3] * output + unit.z2[channel];
					unit.z2[channel] = c[2] * input - c[4] * output;
					if (std::abs(unit.z1[channel]) < 1e-300)
						unit.z1[channel] = 0;
					if (std::abs(unit.z2[channel]) < 1e-300)
						unit.z2[channel] = 0;
					return output;
				};
				left = sample(left, 0);
				right = sample(right, 1);
			} else if (unit.kind == Kind::Compressor) {
				const double over = 20 * std::log10(std::max(peak, 1e-30)) - p[0];
				const double target = over <= -p[2] / 2  ? 0
				                      : over >= p[2] / 2 ? over * p[1]
				                                         : p[1] * (over + p[2] / 2) * (over + p[2] / 2) / (2 * p[2]);
				const double smoothing = target > unit.envelope ? p[3] : p[4];
				unit.envelope = smoothing * unit.envelope + (1 - smoothing) * target;
				const double gain = (1 - p[6]) + p[6] * amplitude(-unit.envelope) * p[5];
				left *= gain;
				right *= gain;
			} else if (unit.kind == Kind::Gate) {
				const double level = 20 * std::log10(std::max(peak, 1e-30));
				if (level >= p[0]) {
					unit.open = true;
					unit.hold = int(p[5]);
				} else if (unit.open && level < p[0] - p[1]) {
					if (unit.hold)
						--unit.hold;
					else
						unit.open = false;
				} else if (unit.open)
					unit.hold = int(p[5]);
				const double target = unit.open ? 0 : p[2], smoothing = target < unit.envelope ? p[3] : p[4];
				unit.envelope = smoothing * unit.envelope + (1 - smoothing) * target;
				const double gain = amplitude(-unit.envelope);
				left *= gain;
				right *= gain;
			} else if (unit.kind == Kind::Limiter) {
				const double desired = peak > p[0] ? p[0] / peak : 1;
				unit.envelope = desired < unit.envelope ? desired : p[1] * unit.envelope + (1 - p[1]) * desired;
				left = std::clamp(left * unit.envelope, -p[0], p[0]);
				right = std::clamp(right * unit.envelope, -p[0], p[0]);
			} else if (unit.kind == Kind::LookaheadLimiter) {
				const auto limited = unit.lookahead->tick(left, right);
				left = limited[0];
				right = limited[1];
			} else if (unit.kind == Kind::Delay || unit.kind == Kind::ModulatedDelay) {
				const size_t frames = unit.delay.size() / 2;
				const bool modulated = unit.kind == Kind::ModulatedDelay;
				const double angle = 2 * std::numbers::pi * unit.phase;
				const auto delayed = [&](size_t channel) {
					const double tap =
					    modulated ? std::max(1.0, p[0] + p[1] * (1 + std::sin(angle + (channel ? p[5] : 0))) / 2)
					              : p[channel];
					const size_t whole = size_t(tap);
					const double part = tap - double(whole);
					const size_t recent = (unit.cursor + frames - whole) % frames,
					             older = (recent + frames - 1) % frames;
					return (whole <= unit.validFrames ? unit.delay[recent * 2 + channel] * (1 - part) : 0) +
					       (whole + 1 <= unit.validFrames ? unit.delay[older * 2 + channel] * part : 0);
				};
				const double dl = delayed(0), dr = delayed(1);
				unit.delay[unit.cursor * 2] = left + dl * p[2];
				unit.delay[unit.cursor * 2 + 1] = right + dr * p[2];
				if (!std::isfinite(unit.delay[unit.cursor * 2]) || !std::isfinite(unit.delay[unit.cursor * 2 + 1]))
					return false;
				unit.cursor = (unit.cursor + 1) % frames;
				unit.validFrames = std::min(frames, unit.validFrames + 1);
				left = left * (1 - p[3]) + dl * p[3];
				right = right * (1 - p[3]) + dr * p[3];
				if (modulated) {
					unit.phase += p[4];
					if (unit.phase >= 1)
						unit.phase -= 1;
				}
			} else if (unit.kind == Kind::Reverb) {
				if (!unit.reverb->process(left, right, p[0], p[1]))
					return false;
			} else if (unit.kind == Kind::Tremolo) {
				const double angle = 2 * std::numbers::pi * unit.phase;
				left *= 1 - p[1] * (1 + std::sin(angle)) / 2;
				right *= 1 - p[1] * (1 + std::sin(angle + p[2])) / 2;
				unit.phase += p[0];
				if (unit.phase >= 1)
					unit.phase -= 1;
			} else if (unit.kind == Kind::Phaser) {
				const double angle = 2 * std::numbers::pi * unit.phase;
				const auto phased = [&](double dry, size_t channel) {
					double wet = dry + p[5] * unit.z1[channel];
					const double center = p[0] * std::pow(2.0, p[1] * std::sin(angle + (channel ? p[3] : 0)));
					for (size_t stage = 0; stage < 4; ++stage) {
						const double frequency = std::clamp(center * (.5 + .5 * stage), p[4] * 1e-5, p[4] * .49);
						const double tangent = std::tan(std::numbers::pi * frequency / p[4]);
						const double coefficient = (tangent - 1) / (tangent + 1);
						auto &state = unit.allpass[channel * 4 + stage];
						const double out = coefficient * wet + state;
						state = wet - coefficient * out;
						if (std::abs(state) < 1e-300)
							state = 0;
						wet = out;
					}
					unit.z1[channel] = wet;
					return (1 - p[6]) * dry + p[6] * wet;
				};
				left = phased(left, 0);
				right = phased(right, 1);
				unit.phase += p[2];
				if (unit.phase >= 1)
					unit.phase -= 1;
			} else if (unit.kind == Kind::Saturation) {
				const double normal = std::tanh(p[0]);
				left = ((1 - p[2]) * left + p[2] * std::tanh(left * p[0]) / normal) * p[1];
				right = ((1 - p[2]) * right + p[2] * std::tanh(right * p[0]) / normal) * p[1];
			}
			if (!std::isfinite(left) || !std::isfinite(right))
				return false;
			stereo[at] = left;
			stereo[at + 1] = right;
		}
	}
	m_nextFrame = first + qint64(stereo.size() / 2);
	return true;
}
} // namespace vibestudio
