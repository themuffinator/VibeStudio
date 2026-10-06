#pragma once
#include "core/audio_routing.h"
#include <QJsonObject>
#include <array>
#include <span>
#include <vector>

namespace vibestudio
{
inline constexpr int AudioProcessingLatencyLimit = 16 * 384000;
struct AudioLatencyNode {
	int input = 0, output = 0;
	int outputDelay = 0;
	QVector<int> sendDelays;
};
struct AudioLatencyPlan {
	QVector<AudioLatencyNode> nodes;
	int masterInput = 0, total = 0;
	quint64 bytes = 0;
};
// Graph/strip vectors must describe the same validated routing snapshot. Only
// included nodes/routes contribute. Empty inclusion means the complete graph.
QString prepareAudioLatency(const AudioSession &session, const AudioRoutingPlan &routing,
                            std::span<const int> stripLatency, int masterLatency, AudioLatencyPlan *result,
                            std::span<const bool> included = {}, int target = -1, bool preFader = false);
// Diagnostic metadata only; never part of the native session's serialized root.
QJsonObject audioSessionLatencyReport(const AudioSession &session);
class AudioLatencyLine {
  public:
	bool prepare(int frames);
	void reset();
	std::array<double, 2> tick(double left, double right);

  private:
	std::vector<double> m_audio;
	size_t m_cursor = 0, m_valid = 0;
};
} // namespace vibestudio
