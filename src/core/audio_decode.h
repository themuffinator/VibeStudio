#pragma once

#include "core/audio_clip.h"
#include <cstddef>

namespace vibestudio
{

// Header sniffing only; success does not establish stream validity. Stable IDs:
// mp3, flac, vorbis, ogg (unsupported Ogg codec), or empty for other inputs.
QString compressedAudioFormat(const QByteArray& bytes);
struct AudioDecodeBudget {
	qint64 samples = AudioSampleLimit;
	size_t decoderBytes = 16 * 1024 * 1024;
};
// Device-independent import with bounded input, decoder allocation and output.
// Keeps the source rate and channel count; never implicitly resamples or clips.
// Callers can lower (never raise) the standard limits for constrained
// workflows.
AudioClipResult decodeCompressedAudio(const QByteArray& bytes, const AudioWorkControl& control = {},
                                      const AudioDecodeBudget& budget = {});

} // namespace vibestudio
