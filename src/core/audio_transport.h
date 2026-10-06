#pragma once

#include "core/audio_session.h"
#include <array>
#include <vector>

namespace vibestudio
{
struct AudioTransportRange {
	qint64 first = 0;
	qint64 end = 0;
	bool loop = false;
};

// One owner thread. All sample storage and scratch are prepared off the device
// callback. process() performs no allocations or I/O. Device buffering and UI
// notifications belong to the adapter, not this deterministic sample clock.
class AudioTransport {
  public:
	enum class State { Empty, Stopped, Playing, Paused, Ended, Error };
	struct Block {
		int frames = 0;
		std::array<float, 2> peak{};
		quint64 samplesAboveFullScale = 0;
		AudioSessionRenderer::BlockStatus status = AudioSessionRenderer::BlockStatus::Ready;
	};
	bool prepare(const AudioSession &session, AudioTransportRange range, int blockFrames, QString *error,
	             const AudioWorkControl &control = {}, bool metering = false);
	void resetMetering() { m_renderer.resetMetering(); }
	[[nodiscard]] AudioMeterSnapshot meters() const { return m_renderer.meters(); }
	bool play();
	void pause();
	void stop();
	bool seek(qint64 frame);
	void setLoop(bool loop);
	// Clears unused output at a finite endpoint or while stopped/paused. Output
	// may span several prepared blocks and loop boundaries, up to 65536 frames.
	Block process(std::span<float> stereo, const AudioWorkControl &control = {});
	[[nodiscard]] State state() const { return m_state; }
	[[nodiscard]] qint64 position() const { return m_position; }
	[[nodiscard]] quint64 framesRendered() const { return m_rendered; }
	[[nodiscard]] quint64 loops() const { return m_loops; }
	[[nodiscard]] int sampleRate() const { return m_renderer.sampleRate(); }
	[[nodiscard]] int processingLatencyFrames() const { return m_renderer.processingLatencyFrames(); }
	[[nodiscard]] AudioTransportRange range() const { return m_range; }
	[[nodiscard]] int blockFrames() const { return m_blockFrames; }
	// Maps a device's consumed frame count to this range after a start/seek.
	[[nodiscard]] qint64 positionAfter(qint64 origin, quint64 frames) const;

  private:
	AudioSessionRenderer m_renderer;
	std::vector<double> m_scratch;
	AudioTransportRange m_range;
	State m_state = State::Empty;
	int m_blockFrames = 0;
	qint64 m_position = 0;
	qint64 m_renderPosition = 0;
	quint64 m_rendered = 0, m_loops = 0;
};
} // namespace vibestudio
