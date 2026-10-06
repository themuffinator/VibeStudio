#include "core/audio_waveform.h"

#include <algorithm>
#include <limits>

namespace vibestudio
{
namespace
{
void include(AudioExtrema& result, const AudioExtrema& value)
{
	result.minimum = std::min(result.minimum, value.minimum);
	result.maximum = std::max(result.maximum, value.maximum);
}
} // namespace

AudioWaveformData AudioWaveformData::build(const AudioClip& clip, const AudioWorkControl& control)
{
	AudioWaveformData result;
	if (!validateAudioClip(clip, true).isEmpty()) {
		return result;
	}
	const auto cancelled = [&]() { return control.cancelled && control.cancelled(); };
	if (cancelled()) {
		return result;
	}
	result.m_clip = clip;
	const qint64 blocks = (clip.frameCount() + BlockFrames - 1) / BlockFrames;
	if (blocks > 0) {
		QVector<AudioExtrema> leaves(blocks * clip.channels);
		for (qint64 block = 0; block < blocks; ++block) {
			if (block % 64 == 0 && cancelled()) {
				return {};
			}
			const qint64 first = block * BlockFrames, end = std::min(first + BlockFrames, clip.frameCount());
			for (int channel = 0; channel < clip.channels; ++channel) {
				const float start = clip.samples[first * clip.channels + channel];
				AudioExtrema value{start, start};
				for (qint64 frame = first + 1; frame < end; ++frame) {
					const float sample = clip.samples[frame * clip.channels + channel];
					include(value, {sample, sample});
				}
				leaves[block * clip.channels + channel] = value;
			}
		}
		result.m_levels << std::move(leaves);
		while (result.m_levels.last().size() > clip.channels) {
			const auto& previous = result.m_levels.last();
			const qint64 previousBlocks = previous.size() / clip.channels;
			QVector<AudioExtrema> next(((previousBlocks + 1) / 2) * clip.channels);
			for (qint64 block = 0; block < (previousBlocks + 1) / 2; ++block) {
				if (block % 4096 == 0 && cancelled()) {
					return {};
				}
				for (int channel = 0; channel < clip.channels; ++channel) {
					auto value = previous[block * 2 * clip.channels + channel];
					if (block * 2 + 1 < previousBlocks) {
						include(value, previous[(block * 2 + 1) * clip.channels + channel]);
					}
					next[block * clip.channels + channel] = value;
				}
			}
			result.m_levels << std::move(next);
		}
	}
	if (cancelled()) {
		return {};
	}
	result.m_valid = true;
	return result;
}

AudioExtrema AudioWaveformData::range(int channel, qint64 first, qint64 end) const
{
	if (!m_valid || channel < 0 || channel >= m_clip.channels || first < 0 || end > m_clip.frameCount() ||
	    first >= end) {
		return {};
	}
	AudioExtrema result{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
	const auto sample = [&](qint64 frame) {
		const float value = m_clip.samples[frame * m_clip.channels + channel];
		include(result, {value, value});
	};
	// Peel only the partial leaves. The middle is covered by aligned blocks,
	// choosing the largest one that fits instead of revisiting every sample.
	while (first < end && first % BlockFrames != 0) {
		sample(first++);
	}
	while (end > first && end % BlockFrames != 0) {
		sample(--end);
	}
	qint64 block = first / BlockFrames;
	const qint64 last = end / BlockFrames;
	while (block < last) {
		int level = 0;
		qint64 count = 1;
		while (level + 1 < m_levels.size() && block % (count * 2) == 0 && block + count * 2 <= last) {
			++level;
			count *= 2;
		}
		include(result, m_levels[level][(block / count) * m_clip.channels + channel]);
		block += count;
	}
	return result;
}

qint64 AudioWaveformData::cacheBytes() const
{
	qint64 bytes = 0;
	for (const auto& level : m_levels) {
		bytes += level.size() * qint64(sizeof(AudioExtrema));
	}
	return bytes;
}

} // namespace vibestudio
