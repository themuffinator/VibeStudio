#pragma once

#include "core/audio_export.h"

#include <QStringList>

namespace vibestudio
{

enum class AudioDeliveryPreset { Wav, Doom, Quake, Quake2, Quake3 };

struct AudioDeliveryOptions {
	AudioDeliveryPreset preset = AudioDeliveryPreset::Wav;
	AudioWavOptions wav;
};

struct AudioDeliveryPlan {
	int sampleRate = 0;
	int channels = 0;
	qint64 frames = 0;
	bool dmx = false;
	AudioWavOptions wav;
	QString error;
	int outputCues = 0;
	bool outputLoop = false;
	QString markerSummary;
};

struct AudioDeliveryResult {
	AudioDeliveryPlan plan;
	QByteArray bytes;
	double peak = 0;
	qint64 samplesAboveFullScale = 0;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && !bytes.isEmpty(); }
};

QString audioDeliveryPresetId(AudioDeliveryPreset preset);
QString audioDeliveryPresetLabel(AudioDeliveryPreset preset);
bool parseAudioDeliveryPreset(const QString& id, AudioDeliveryPreset* preset);
// Constant-time preview; sample validation and conversion run in renderAudioDelivery.
AudioDeliveryPlan planAudioDelivery(const AudioClip& clip, const AudioDeliveryOptions& options);
QString audioDeliveryDescription(const AudioDeliveryPlan& plan);
// Delivery works on a separate copy: equal-weight mono mix, then band-limited
// resampling, then one quantization/dither step. It never edits the working clip.
AudioDeliveryResult renderAudioDelivery(const AudioClip& clip, const AudioDeliveryOptions& options,
                                        const AudioWorkControl& control = {});
// Commit an already prepared result atomically. Encoding/conversion may be
// cancelled beforehand; this short commit is deliberately not cancellable.
bool writeAudioDelivery(const AudioDeliveryResult& result, const QString& path, bool overwrite,
                        const QStringList& protectedPaths, QString* error = nullptr, bool dryRun = false);
bool isGeneratedAudioDmx(const QByteArray& bytes);
bool stageAudioDelivery(const QByteArray& bytes, const QString& virtualPath, PackageStagingModel* staging, bool replace,
                        QString* error = nullptr);

} // namespace vibestudio
