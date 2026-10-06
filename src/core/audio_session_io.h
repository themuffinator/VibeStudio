#pragma once

#include "core/audio_export.h"
#include "core/audio_session.h"

namespace vibestudio
{

inline constexpr qint64 AudioSessionByteLimit = AudioSessionSampleLimit * 4 + 64 * 1024 * 1024;
QByteArray encodeAudioSession(const AudioSession &session, QString *error = nullptr,
                              const AudioWorkControl &control = {});
bool decodeAudioSession(const QByteArray &bytes, AudioSession *session, QString *error = nullptr,
                        const AudioWorkControl &control = {});
bool readAudioSession(const QString &path, AudioSession *session, AudioProjectIdentity *identity = nullptr,
                      QString *error = nullptr, const AudioWorkControl &control = {});
AudioProjectSaveReport writeAudioSession(const AudioSession &session, const AudioProjectSaveRequest &request,
                                         const AudioWorkControl &control = {}, const QStringList &protectedPaths = {});

struct AudioSessionMixdown {
	AudioProjectSaveRequest output;
	QStringList protectedPaths = {};
	qint64 first = 0;
	qint64 end = -1;
	AudioWavFormat format = AudioWavFormat::Float32;
	bool dither = false;
	quint64 ditherSeed = 0;
	AudioSessionRenderTarget target;
	// Called on the rendering worker; callers marshal UI updates themselves.
	std::function<void(qint64, qint64)> progress;
};
struct AudioSessionMixdownReport {
	AudioProjectSaveReport saved;
	qint64 frames = 0;
	int processingLatencyFrames = 0;
	double peak = 0;
	qint64 samplesAboveFullScale = 0;
};
// Streams bounded blocks through the existing WAV precision encoder. No
// whole-mix sample allocation. RIFF size limits are checked before any write.
AudioSessionMixdownReport writeAudioSessionMixdown(const AudioSession &session, const AudioSessionMixdown &request,
                                                   const AudioWorkControl &control = {});

} // namespace vibestudio
