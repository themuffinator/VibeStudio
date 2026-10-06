#pragma once
#include "core/audio_effects.h"
#include "core/audio_project.h"

namespace vibestudio
{
inline constexpr qint64 AudioEffectPresetByteLimit = 64 * 1024;
struct AudioEffectPreset {
	QString name;
	int sampleRate = 48000;
	double tailSeconds = 2;
	AudioEffectChain effects;
};
struct AudioEffectFactoryPreset {
	QString id, name, description;
};
QVector<AudioEffectFactoryPreset> audioEffectFactoryPresets();
AudioEffectPreset makeAudioEffectPreset(const QString &id, int sampleRate, QString *error = nullptr);
QString validateAudioEffectPreset(const AudioEffectPreset &preset);
// Preserve physical parameter values and reject incompatible destination rates.
// Fresh IDs make applying a reusable recipe a distinct chain instance.
bool instantiateAudioEffectPreset(const AudioEffectPreset &preset, int sampleRate, AudioEffectChain *effects,
                                  QString *error = nullptr);
QByteArray encodeAudioEffectPreset(const AudioEffectPreset &preset, QString *error = nullptr);
bool decodeAudioEffectPreset(const QByteArray &bytes, AudioEffectPreset *preset, QString *error = nullptr);
bool readAudioEffectPreset(const QString &path, AudioEffectPreset *preset, AudioProjectIdentity *identity = nullptr,
                           QString *error = nullptr, const AudioWorkControl &control = {});
AudioProjectSaveReport writeAudioEffectPreset(const AudioEffectPreset &preset, const AudioProjectSaveRequest &request,
                                              const QStringList &protectedPaths = {},
                                              const AudioWorkControl &control = {});
} // namespace vibestudio
