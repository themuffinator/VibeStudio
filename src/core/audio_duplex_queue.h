#pragma once
#include "core/audio_duplex.h"
#include <atomic>

namespace vibestudio
{
// Single callback producer, single disk consumer. Capacity is measured in
// frames rather than callback blocks, so small device buffers do not shorten
// the disk-stall reserve. Neither side allocates, locks or wakes the other.
class AudioDuplexCaptureQueue final : public AudioDuplexCaptureSink {
  public:
	bool prepare(qint64 first, std::span<const int> channels, int capacityFrames);
	bool push(const AudioDuplexCaptureBlock &block) noexcept override;
	// Consumer-only, up to 4096 contiguous frames. Span lifetime ends when those
	// frames are consumed; keep them owned until every arm has been written.
	[[nodiscard]] AudioDuplexCaptureBlock read(int maximumFrames = 4096) noexcept;
	bool consume(int frames) noexcept;
	[[nodiscard]] qint64 producedFrames() const { return m_produced.load(std::memory_order_acquire); }
	[[nodiscard]] qint64 consumedFrames() const { return m_consumed.load(std::memory_order_acquire); }
	[[nodiscard]] int capacityFrames() const { return m_capacity; }

  private:
	// Lock-free 64-bit atomics are required on the callback path. An unsupported
	// target rejects preparation rather than silently calling a runtime mutex.
	alignas(64) std::atomic<qint64> m_produced{0};
	alignas(64) std::atomic<qint64> m_consumed{0};
	qint64 m_first = 0;
	int m_arms = 0, m_capacity = 0, m_readable = 0;
	std::array<int, AudioDuplexArmLimit> m_channels{};
	std::array<size_t, AudioDuplexArmLimit> m_offsets{};
	std::vector<float> m_audio;
};
} // namespace vibestudio
