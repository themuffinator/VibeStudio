#pragma once

#include "core/audio_clip.h"

namespace vibestudio
{

struct AudioExtrema {
	float minimum = 0, maximum = 0;
};

// Immutable, implicitly shared display data. Construction is worker-side;
// queries use exact edge samples and cached power-of-two blocks. The cache
// adds about 1/16 of the PCM size, independent of the current viewport.
class AudioWaveformData
{
public:
	[[nodiscard]] const AudioClip& clip() const { return m_clip; }
	[[nodiscard]] bool valid() const { return m_valid; }
	[[nodiscard]] qint64 cacheBytes() const;
	[[nodiscard]] AudioExtrema range(int channel, qint64 first, qint64 end) const;
	static AudioWaveformData build(const AudioClip& clip, const AudioWorkControl& control = {});

private:
	static constexpr qint64 BlockFrames = 64;
	AudioClip m_clip;
	QVector<QVector<AudioExtrema>> m_levels;
	bool m_valid = false;
};

} // namespace vibestudio
