#include "core/audio_effect_preset.h"
#include "core/audio_publication.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
QString tr(const char *text) { return QCoreApplication::translate("AudioEffectPreset", text); }
bool fail(QString *error, const QString &message)
{
	if (error)
		*error = message;
	return false;
}
bool stopped(const AudioWorkControl &control, QString *error)
{
	if (!control.cancelled || !control.cancelled())
		return false;
	fail(error, tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Preset operation cancelled.")));
	return true;
}
} // namespace
QVector<AudioEffectFactoryPreset> audioEffectFactoryPresets()
{
	return {{"dialogue-clean", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Dialogue cleanup")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Low-cut filtering and gentle compression."))},
	        {"radio", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Radio voice")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Narrow band, saturation and sample peak control."))},
	        {"small-room", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Small room")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Short stereo room reflections."))},
	        {"large-hall", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Large hall")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Long diffuse decay with pre-delay."))},
	        {"wide-chorus", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Wide chorus")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Slow stereo delay modulation."))},
	        {"jet-flange", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Jet flange")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Short swept delays with resonant feedback."))},
	        {"phase-sweep", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Phase sweep")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Four swept allpass stages per channel."))},
	        {"rhythmic-tremolo", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Rhythmic tremolo")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Linked amplitude modulation."))},
	        {"master-control", tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Master peak control")),
	         tr(QT_TRANSLATE_NOOP("AudioEffectPreset",
	                              "Gentle compression and sample limiting; no loudness or true-peak target."))}};
}
QString validateAudioEffectPreset(const AudioEffectPreset &preset)
{
	if (preset.name.trimmed().isEmpty() || preset.name.size() > 128 || !preset.name.isValidUtf16() ||
	    preset.name.contains(QChar(0)) || !std::isfinite(preset.tailSeconds) || preset.tailSeconds < 0 ||
	    preset.tailSeconds > 60)
		return tr(
		    QT_TRANSLATE_NOOP("AudioEffectPreset",
		                      "Preset requires a name of at most 128 characters and a tail between 0 and 60 seconds."));
	const auto error = validateAudioEffects(preset.effects, preset.sampleRate);
	if (!error.isEmpty())
		return error;
	if (audioEffectMemoryBytes(preset.effects, preset.sampleRate) > AudioEffectMemoryLimit)
		return tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Preset effect state exceeds 128 MiB."));
	return {};
}
AudioEffectPreset makeAudioEffectPreset(const QString &id, int rate, QString *error)
{
	AudioEffectPreset result;
	result.sampleRate = rate;
	for (const auto &descriptor : audioEffectFactoryPresets())
		if (descriptor.id == id)
			result.name = descriptor.name;
	if (result.name.isEmpty() || rate < 1 || rate > 384000) {
		fail(error,
		     tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Choose a known factory preset and a supported sample rate.")));
		return {};
	}
	const auto add = [&](const QString &type, const QMap<QString, double> &values = {}) {
		auto effect = makeAudioEffect(type, rate);
		for (const auto &parameter : audioEffectParameters(type, rate))
			if (values.contains(parameter.key))
				effect.parameters[parameter.key] =
				    std::clamp(values[parameter.key], parameter.minimum, parameter.maximum);
		result.effects.append(effect);
	};
	if (id == "dialogue-clean") {
		add("high-pass", {{"frequencyHz", 90}});
		add("compressor", {{"thresholdDb", -20}, {"ratio", 3}, {"attackMs", 5}, {"releaseMs", 120}, {"makeupDb", 2}});
		result.tailSeconds = .5;
	} else if (id == "radio") {
		add("high-pass", {{"frequencyHz", 350}});
		add("low-pass", {{"frequencyHz", 3500}});
		add("saturation", {{"driveDb", 12}, {"mix", .35}});
		add("limiter", {{"ceilingDb", -2}});
		result.tailSeconds = .5;
	} else if (id == "small-room") {
		add("reverb", {{"roomSize", .55}, {"decaySeconds", .65}, {"preDelayMs", 7}, {"dampingHz", 6500}, {"mix", .18}});
		result.tailSeconds = 2.5;
	} else if (id == "large-hall") {
		add("reverb", {{"roomSize", 1.8}, {"decaySeconds", 4}, {"preDelayMs", 30}, {"dampingHz", 4500}, {"mix", .28}});
		result.tailSeconds = 12;
	} else if (id == "wide-chorus") {
		add("chorus", {{"minDelayMs", 14}, {"depthMs", 8}, {"rateHz", .65}, {"feedback", .08}, {"mix", .4}});
		result.tailSeconds = 1;
	} else if (id == "jet-flange") {
		add("flanger", {{"minDelayMs", .2}, {"depthMs", 3.5}, {"rateHz", .15}, {"feedback", .7}});
		result.tailSeconds = 2;
	} else if (id == "phase-sweep") {
		add("phaser", {{"frequencyHz", 700}, {"rateHz", .3}, {"feedback", .4}});
		result.tailSeconds = 1;
	} else if (id == "rhythmic-tremolo") {
		add("tremolo", {{"depth", .8}});
		result.tailSeconds = 0;
	} else {
		add("compressor", {{"thresholdDb", -12}, {"ratio", 2}, {"attackMs", 25}, {"releaseMs", 150}});
		add("limiter");
		result.tailSeconds = 0;
	}
	if (error)
		*error = validateAudioEffectPreset(result);
	return result;
}
bool instantiateAudioEffectPreset(const AudioEffectPreset &preset, int rate, AudioEffectChain *effects, QString *error)
{
	const auto invalid = validateAudioEffectPreset(preset);
	if (!invalid.isEmpty())
		return fail(error, invalid);
	const auto incompatible = validateAudioEffects(preset.effects, rate);
	if (!effects || !incompatible.isEmpty())
		return fail(error, incompatible.isEmpty()
		                       ? tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "No destination chain was supplied."))
		                       : incompatible);
	if (audioEffectMemoryBytes(preset.effects, rate) > AudioEffectMemoryLimit)
		return fail(error, tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Preset effect state exceeds 128 MiB.")));
	auto next = preset.effects;
	for (auto &effect : next)
		effect.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	*effects = std::move(next);
	if (error)
		error->clear();
	return true;
}
QByteArray encodeAudioEffectPreset(const AudioEffectPreset &preset, QString *error)
{
	const auto invalid = validateAudioEffectPreset(preset);
	if (!invalid.isEmpty()) {
		fail(error, invalid);
		return {};
	}
	if (error)
		error->clear();
	return QJsonDocument(QJsonObject{{"format", "vibestudio.audio-effects"},
	                                 {"version", 1},
	                                 {"name", preset.name},
	                                 {"sampleRate", preset.sampleRate},
	                                 {"tailSeconds", preset.tailSeconds},
	                                 {"effects", audioEffectsToJson(preset.effects)}})
	    .toJson();
}
bool decodeAudioEffectPreset(const QByteArray &bytes, AudioEffectPreset *preset, QString *error)
{
	QJsonParseError parse{};
	const auto json =
	    bytes.size() <= AudioEffectPresetByteLimit ? QJsonDocument::fromJson(bytes, &parse) : QJsonDocument();
	const auto object = json.object();
	const double rate = object.value("sampleRate").toDouble(std::numeric_limits<double>::quiet_NaN());
	AudioEffectPreset next;
	if (!preset || parse.error != QJsonParseError::NoError || !json.isObject() || object.size() != 6 ||
	    object.value("format") != QJsonValue("vibestudio.audio-effects") || object.value("version") != QJsonValue(1) ||
	    !object.value("name").isString() || !std::isfinite(rate) || rate != std::floor(rate) || rate < 1 ||
	    rate > 384000 || !audioEffectsFromJson(object.value("effects"), &next.effects))
		return fail(error,
		            tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Preset is malformed, unsupported or exceeds 64 KiB.")));
	next.name = object.value("name").toString();
	next.sampleRate = int(rate);
	next.tailSeconds = object.value("tailSeconds").toDouble(std::numeric_limits<double>::quiet_NaN());
	const auto invalid = validateAudioEffectPreset(next);
	if (!invalid.isEmpty())
		return fail(error, invalid);
	*preset = std::move(next);
	if (error)
		error->clear();
	return true;
}
bool readAudioEffectPreset(const QString &path, AudioEffectPreset *preset, AudioProjectIdentity *identity,
                           QString *error, const AudioWorkControl &control)
{
	if (stopped(control, error))
		return false;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > AudioEffectPresetByteLimit)
		return fail(error, tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "Preset is unreadable or exceeds 64 KiB.")));
	const auto bytes = file.read(AudioEffectPresetByteLimit + 1);
	if (file.error() != QFileDevice::NoError)
		return fail(error, file.errorString());
	AudioEffectPreset next;
	if (stopped(control, error) || !decodeAudioEffectPreset(bytes, &next, error) || stopped(control, error))
		return false;
	if (!preset)
		return fail(error, tr(QT_TRANSLATE_NOOP("AudioEffectPreset", "No destination preset was supplied.")));
	*preset = std::move(next);
	if (identity)
		*identity = {QFileInfo(path).absoluteFilePath(), QFileInfo(path).canonicalFilePath(),
		             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
	return true;
}
AudioProjectSaveReport writeAudioEffectPreset(const AudioEffectPreset &preset, const AudioProjectSaveRequest &request,
                                              const QStringList &protectedPaths, const AudioWorkControl &control)
{
	return commitAudioOutput(request, "vsfx", protectedPaths, AudioEffectPresetByteLimit, control,
	                         [&](const AudioOutputEmit &write, QString *error) {
		                         const auto bytes = encodeAudioEffectPreset(preset, error);
		                         return !bytes.isEmpty() && write(bytes);
	                         });
}
} // namespace vibestudio
