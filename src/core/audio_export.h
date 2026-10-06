#pragma once

#include "core/audio_clip.h"
#include <QStringList>
#include <random>

namespace vibestudio
{

enum class AudioWavFormat { Pcm8, Pcm16, Pcm24, Pcm32, Float32 };

struct AudioWavOptions {
	AudioWavFormat format = AudioWavFormat::Pcm16;
	bool dither = false;
	quint64 ditherSeed = 0; // Deterministic TPDF; applied once, at integer delivery.
	AudioWavMarkers markers = AudioWavMarkers::Standard;
};

QString audioWavFormatId(AudioWavFormat format);
bool parseAudioWavFormat(const QString &id, AudioWavFormat *format);
int audioWavBits(AudioWavFormat format);

// Each call produces a complete WAV block. Dither continues across successful
// blocks, so concatenating their data chunks matches a single whole-file encode.
// A failed/cancelled block does not advance the deterministic noise sequence.
class AudioWavEncoder {
  public:
	explicit AudioWavEncoder(const AudioWavOptions &options);
	QByteArray encode(const AudioClip &clip, QString *error = nullptr, const AudioWorkControl &control = {});

  private:
	AudioWavOptions m_options;
	std::mt19937_64 m_random;
};

// Float32 retains every finite sample bit. Integer formats saturate to their
// signed/unsigned full-scale range. Invalid options and cancellation return no
// partial bytes. This overload is also used by the default PCM16 convenience API.
QByteArray encodeAudioWav(const AudioClip &clip, const AudioWavOptions &options, QString *error = nullptr,
                          const AudioWorkControl &control = {});
bool saveAudioWav(const AudioClip &clip, const AudioWavOptions &options, const QString &path, bool overwrite,
                  const QString &protectedPath, QString *error = nullptr, bool dryRun = false);

// Internal delivery boundary for bytes produced by the audio encoders. Shared
// atomic write, cooperating-writer lock, suffix, and source-protection checks.
bool writeAudioExportBytes(const QByteArray &bytes, const QString &path, const QStringList &suffixes, bool overwrite,
                           const QStringList &protectedPaths, QString *error = nullptr, bool dryRun = false);

// Header and bounded marker validation of canonical integer WAV layouts,
// without scanning PCM samples. Used at the UI-thread package handoff boundary.
bool isGeneratedIntegerAudioWav(const QByteArray &bytes);

} // namespace vibestudio
