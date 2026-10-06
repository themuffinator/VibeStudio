#pragma once
#include <array>
#include <cstddef>
#include <vector>

namespace vibestudio
{
// Prepared stereo Schroeder/Moorer reverberator. The caller validates parameters.
// Each delay masks unwritten history so resetting never clears large buffers.
class AudioReverb {
  public:
	static size_t memoryBytes(int sampleRate, double roomSize, double preDelayMs);
	void prepare(int sampleRate, double roomSize, double decaySeconds, double dampingHz, double preDelayMs,
	             double maximumRoomSize = -1, double maximumPreDelayMs = -1);
	// Physical values change without allocation; prepare reserves their declared maxima.
	void setParameters(double roomSize, double decaySeconds, double dampingHz, double preDelayMs);
	void reset();
	bool process(double &left, double &right, double width, double mix);

  private:
	struct Line {
		std::vector<double> samples;
		size_t cursor = 0, valid = 0;
		double filtered = 0, feedback = 0;
		double delay = 1;
		double read(double current = 0) const;
		void write(double sample);
	};
	std::array<Line, 26> m_lines;
	double m_damping = 0;
	int m_rate = 48000;
	double m_room = -1, m_decay = -1, m_cutoff = -1, m_predelay = -1;
};
} // namespace vibestudio
