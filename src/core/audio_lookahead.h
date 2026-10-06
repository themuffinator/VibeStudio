#pragma once
#include <QtGlobal>
#include <array>
#include <vector>

namespace vibestudio
{
// Original stereo-linked sample-peak lookahead limiter. This is not a true-peak
// reconstruction guarantee. Preparation owns all storage; tick/reset allocate
// nothing. A monotone peak queue keeps total work linear in processed frames.
class AudioLookaheadLimiter {
  public:
	bool prepare(int frames);
	void reset();
	void setParameters(double ceiling, double attackPole, double releasePole);
	std::array<double, 2> tick(double left, double right);
	[[nodiscard]] int latencyFrames() const { return int(m_audio.size() / 2); }
	[[nodiscard]] static quint64 memoryBytes(int frames);
	[[nodiscard]] double gain() const { return m_gain; }

  private:
	struct Peak {
		quint64 frame = 0;
		double value = 0;
	};
	std::vector<double> m_audio;
	std::vector<Peak> m_peaks;
	size_t m_cursor = 0, m_valid = 0, m_head = 0, m_count = 0;
	quint64 m_frame = 0;
	double m_ceiling = 1, m_attack = 0, m_release = 0, m_envelope = 1, m_gain = 1;
};
} // namespace vibestudio
