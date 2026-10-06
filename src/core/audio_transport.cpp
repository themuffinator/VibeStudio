#include "core/audio_transport.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
bool AudioTransport::prepare(const AudioSession &session, AudioTransportRange range, int blockFrames, QString *error,
                             const AudioWorkControl &control, bool metering)
{
	m_state = State::Empty;
	m_renderer = {};
	m_range = {};
	m_scratch.clear();
	m_blockFrames = 0;
	m_position = m_renderPosition = 0;
	m_rendered = m_loops = 0;
	if (range.first < 0 || range.end <= range.first || range.end > AudioSessionFrameLimit || blockFrames < 1 ||
	    blockFrames > 65536) {
		if (error) {
			*error = QCoreApplication::translate(
			    "AudioTransport", "Choose a nonempty transport range and a block size of 1–65536 frames.");
		}
		return false;
	}
	if (!m_renderer.prepare(session, error, control, {}, {metering, range.first, range.end},
	                        AudioSessionRenderClock::Compensated, {range.loop, range.first, range.end})) {
		return false;
	}
	m_scratch.resize(m_renderer.scratchSamples(blockFrames));
	m_blockFrames = blockFrames;
	m_range = range;
	m_position = m_renderPosition = range.first;
	m_state = State::Stopped;
	return true;
}
bool AudioTransport::play()
{
	if (m_state == State::Empty || m_state == State::Error) {
		return false;
	}
	if (m_position == m_range.end) {
		m_position = m_renderPosition = m_range.first;
		m_renderer.resetProcessing();
		resetMetering();
	}
	m_state = State::Playing;
	return true;
}
void AudioTransport::pause()
{
	if (m_state == State::Playing) {
		m_state = State::Paused;
	}
}
void AudioTransport::stop()
{
	if (m_state != State::Empty) {
		m_renderer.resetProcessing();
		resetMetering();
		m_position = m_renderPosition = m_range.first;
		m_rendered = m_loops = 0;
		m_state = State::Stopped;
	}
}
bool AudioTransport::seek(qint64 frame)
{
	if (m_state == State::Empty || m_state == State::Error || frame < m_range.first || frame > m_range.end) {
		return false;
	}
	m_position = m_renderPosition = frame;
	m_renderer.resetProcessing();
	resetMetering();
	m_rendered = m_loops = 0;
	if (frame == m_range.end) {
		m_state = State::Ended;
	} else if (m_state == State::Ended) {
		m_state = State::Stopped;
	}
	return true;
}
void AudioTransport::setLoop(bool loop)
{
	if (loop == m_range.loop || !m_renderer.setLoopEnabled(loop))
		return;
	m_range.loop = loop;
	m_renderPosition = m_position;
}
qint64 AudioTransport::positionAfter(qint64 origin, quint64 frames) const
{
	if (m_state == State::Empty || origin < m_range.first || origin > m_range.end) {
		return m_range.first;
	}
	const auto length = quint64(m_range.end - m_range.first);
	if (!m_range.loop) {
		return origin + qint64(std::min(frames, quint64(m_range.end - origin)));
	}
	return m_range.first + qint64((quint64(origin - m_range.first) + frames % length) % length);
}
AudioTransport::Block AudioTransport::process(std::span<float> stereo, const AudioWorkControl &control)
{
	Block result;
	std::fill(stereo.begin(), stereo.end(), 0.0f);
	if (stereo.empty() || stereo.size() % 2 || stereo.size() > 131072 || m_state == State::Empty) {
		result.status = AudioSessionRenderer::BlockStatus::InvalidRange;
		return result;
	}
	if (m_state != State::Playing) {
		return result;
	}
	const auto originalPosition = m_position;
	const auto originalRenderPosition = m_renderPosition;
	const auto originalLoops = m_loops;
	while (size_t(result.frames) * 2 < stereo.size() && m_state == State::Playing) {
		const int frames =
		    int(std::min({qint64(stereo.size() / 2) - result.frames, qint64(blockFrames()), m_range.end - m_position}));
		const auto output = stereo.subspan(size_t(result.frames) * 2, size_t(frames) * 2);
		result.status = m_renderer.renderInto(m_renderPosition, output, m_scratch, control);
		if (result.status != AudioSessionRenderer::BlockStatus::Ready) {
			std::fill(stereo.begin(), stereo.end(), 0.0f);
			m_position = originalPosition;
			m_renderPosition = originalRenderPosition;
			m_loops = originalLoops;
			m_state = State::Error;
			result.frames = 0;
			return result;
		}
		result.frames += frames;
		m_position += frames;
		m_renderPosition += frames;
		if (m_position == m_range.end) {
			if (m_range.loop) {
				m_position = m_range.first;
				if (m_loops != std::numeric_limits<quint64>::max())
					++m_loops;
			} else {
				m_state = State::Ended;
			}
		}
	}
	// Lifetime counters saturate instead of wrapping, including short loop ranges.
	m_rendered += std::min(quint64(result.frames), std::numeric_limits<quint64>::max() - m_rendered);
	for (int i = 0; i < result.frames * 2; ++i) {
		const float value = std::abs(stereo[size_t(i)]);
		result.peak[size_t(i % 2)] = std::max(result.peak[size_t(i % 2)], value);
		result.samplesAboveFullScale += value > 1;
	}
	return result;
}
} // namespace vibestudio
