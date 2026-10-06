#include "core/audio_duplex_queue.h"
#include <algorithm>

namespace vibestudio
{
bool AudioDuplexCaptureQueue::prepare(qint64 first, std::span<const int> channels, int capacityFrames)
{
	m_capacity = m_arms = m_readable = 0;
	m_produced.store(0, std::memory_order_relaxed);
	m_consumed.store(0, std::memory_order_relaxed);
	m_audio = {};
	if (!m_produced.is_lock_free() || !m_consumed.is_lock_free() || first < 0 || first >= AudioSessionFrameLimit ||
	    channels.empty() || channels.size() > AudioDuplexArmLimit || capacityFrames < 1 || capacityFrames > 262144)
		return false;
	size_t samples = 0;
	for (size_t arm = 0; arm < channels.size(); ++arm) {
		if (channels[arm] != 1 && channels[arm] != 2)
			return false;
		m_channels[arm] = channels[arm];
		m_offsets[arm] = samples;
		samples += size_t(capacityFrames) * size_t(channels[arm]);
	}
	m_audio.resize(samples);
	m_arms = int(channels.size());
	m_capacity = capacityFrames;
	m_first = first;
	return true;
}
bool AudioDuplexCaptureQueue::push(const AudioDuplexCaptureBlock &block) noexcept
{
	const auto produced = m_produced.load(std::memory_order_relaxed);
	const auto consumed = m_consumed.load(std::memory_order_acquire);
	if (!m_capacity || block.arms != m_arms || block.frames < 1 || block.frames > 4096 ||
	    block.first != m_first + produced || block.frames > AudioSessionFrameLimit - block.first ||
	    block.frames > m_capacity - (produced - consumed))
		return false;
	for (int arm = 0; arm < m_arms; ++arm)
		if (block.samples[size_t(arm)].size() != size_t(block.frames * m_channels[size_t(arm)]))
			return false;
	const int start = int(produced % m_capacity);
	const int firstFrames = std::min(block.frames, m_capacity - start);
	for (int arm = 0; arm < m_arms; ++arm) {
		const auto index = size_t(arm);
		const auto channels = size_t(m_channels[index]);
		const auto source = block.samples[index];
		auto *destination = m_audio.data() + m_offsets[index];
		std::copy_n(source.data(), size_t(firstFrames) * channels, destination + size_t(start) * channels);
		std::copy(source.begin() + firstFrames * qsizetype(channels), source.end(), destination);
	}
	// Publish the complete set of arms together. A rejected block exposes none
	// of its samples; the consumer cannot observe a partial arm assignment.
	m_produced.store(produced + block.frames, std::memory_order_release);
	return true;
}
AudioDuplexCaptureBlock AudioDuplexCaptureQueue::read(int maximumFrames) noexcept
{
	AudioDuplexCaptureBlock block;
	m_readable = 0;
	if (!m_capacity || maximumFrames < 1 || maximumFrames > 4096)
		return block;
	const auto consumed = m_consumed.load(std::memory_order_relaxed);
	const auto produced = m_produced.load(std::memory_order_acquire);
	const int start = int(consumed % m_capacity);
	const int frames = int(std::min<qint64>({produced - consumed, maximumFrames, m_capacity - start}));
	if (!frames)
		return block;
	block.first = m_first + consumed;
	block.frames = m_readable = frames;
	block.arms = m_arms;
	for (int arm = 0; arm < m_arms; ++arm) {
		const auto index = size_t(arm), channels = size_t(m_channels[index]);
		block.samples[index] = std::span<const float>(m_audio).subspan(m_offsets[index] + size_t(start) * channels,
		                                                               size_t(frames) * channels);
	}
	return block;
}
bool AudioDuplexCaptureQueue::consume(int frames) noexcept
{
	if (frames < 1 || frames > m_readable)
		return false;
	m_readable -= frames;
	m_consumed.store(m_consumed.load(std::memory_order_relaxed) + frames, std::memory_order_release);
	return true;
}
} // namespace vibestudio
