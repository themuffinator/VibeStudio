#include "core/audio_lookahead.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
quint64 AudioLookaheadLimiter::memoryBytes(int frames)
{
	return frames < 0 || frames > 7680 ? std::numeric_limits<quint64>::max()
	                                   : quint64(frames) * 2 * sizeof(double) + quint64(frames + 2) * sizeof(Peak);
}
bool AudioLookaheadLimiter::prepare(int frames)
{
	m_audio = std::vector<double>{};
	m_peaks = std::vector<Peak>{};
	if (frames < 0 || frames > 7680)
		return false;
	m_audio.resize(size_t(frames) * 2);
	m_peaks.resize(size_t(frames) + 2);
	reset();
	return true;
}
void AudioLookaheadLimiter::reset()
{
	m_cursor = m_valid = m_head = m_count = 0;
	m_frame = 0;
	m_envelope = m_gain = 1;
}
void AudioLookaheadLimiter::setParameters(double ceiling, double attackPole, double releasePole)
{
	m_ceiling = ceiling;
	m_attack = attackPole;
	m_release = releasePole;
}
std::array<double, 2> AudioLookaheadLimiter::tick(double left, double right)
{
	if (m_peaks.empty() || !std::isfinite(left) || !std::isfinite(right) || !std::isfinite(m_ceiling) ||
	    m_ceiling <= 0 || !std::isfinite(m_attack) || m_attack < 0 || m_attack > 1 || !std::isfinite(m_release) ||
	    m_release < 0 || m_release > 1)
		return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
	const auto frames = size_t(latencyFrames());
	const auto first = m_frame >= frames ? m_frame - frames : 0;
	while (m_count && m_peaks[m_head].frame < first) {
		m_head = (m_head + 1) % m_peaks.size();
		--m_count;
	}
	const double peak = std::max(std::abs(left), std::abs(right));
	while (m_count && m_peaks[(m_head + m_count - 1) % m_peaks.size()].value <= peak)
		--m_count;
	m_peaks[(m_head + m_count) % m_peaks.size()] = {m_frame++, peak};
	++m_count;
	std::array<double, 2> output{left, right};
	if (frames) {
		output = m_valid == frames ? std::array<double, 2>{m_audio[m_cursor * 2], m_audio[m_cursor * 2 + 1]}
		                           : std::array<double, 2>{};
		m_audio[m_cursor * 2] = left;
		m_audio[m_cursor * 2 + 1] = right;
		m_cursor = (m_cursor + 1) % frames;
		m_valid = std::min(m_valid + 1, frames);
	}
	const double future = m_peaks[m_head].value;
	const double desired = future > m_ceiling ? m_ceiling / future : 1;
	const double pole = desired < m_envelope ? m_attack : m_release;
	m_envelope = desired + pole * (m_envelope - desired);
	const double outputPeak = std::max(std::abs(output[0]), std::abs(output[1]));
	// Stereo-linked safety gain catches any residual from the attack smoother,
	// including abrupt ceiling automation. It never clips channels independently.
	m_gain = std::min(m_envelope, outputPeak > m_ceiling ? m_ceiling / outputPeak : 1);
	// A safety reduction is real gain reduction. Release from that level rather
	// than jumping back to the slower attack envelope on the following sample.
	m_envelope = m_gain;
	output[0] *= m_gain;
	output[1] *= m_gain;
	return output;
}
} // namespace vibestudio
