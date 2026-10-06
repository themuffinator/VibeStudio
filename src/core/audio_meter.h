#pragma once

#include "core/audio_clip.h"
#include <array>
#include <span>

namespace vibestudio
{
struct AudioSession;
inline constexpr int AudioMeterStripLimit = 65; // 64 tracks/buses and master.
inline constexpr double AudioMeterRmsSeconds = 0.3;
inline constexpr double AudioMeterDecayDbPerSecond = 24;

struct AudioMeterWindow {
	bool enabled = false;
	qint64 first = 0, end = 0;
};
struct AudioMeterReading {
	quint64 frames = 0;
	qint64 lastFrame = -1;
	bool valid = true;
	std::array<double, 2> peak{}, maximum{}, rms{}, integratedRms{};
	std::array<quint64, 2> samplesAboveFullScale{};
	double correlation = 0, integratedCorrelation = 0;
	bool correlationValid = false, integratedCorrelationValid = false;
};
struct AudioStripMeters {
	AudioMeterReading pre, post;
};
struct AudioMeterSnapshot {
	int count = 0; // Session order, then master. No IDs or allocations on the render path.
	std::array<AudioStripMeters, AudioMeterStripLimit> strips{};
};

// Sample-clock envelopes; silence decays only when rendered. Pause retains the
// reading. Integrated statistics and maxima persist until an explicit reset.
class AudioMeterProcessor {
  public:
	void prepare(int sampleRate);
	void reset();
	void tick(double left, double right, qint64 frame);
	[[nodiscard]] AudioMeterReading reading() const;

  private:
	AudioMeterReading m_reading;
	double m_release = 0, m_memory = 0, m_scale = 0;
	std::array<double, 2> m_energy{}, m_sum{};
	double m_cross = 0, m_crossSum = 0;
};
class AudioMeterBank {
  public:
	void prepare(int count, int sampleRate, AudioMeterWindow window);
	void reset();
	void setOrigin(qint64 first) { m_origin = first; }
	void setEnd(qint64 end) { m_window.end = end; }
	void process(int strip, bool post, qint64 first, std::span<const double> samples);
	void process(int strip, bool post, qint64 first, std::span<const float> samples);
	[[nodiscard]] bool enabled() const { return m_window.enabled; }
	[[nodiscard]] AudioMeterSnapshot snapshot() const;

  private:
	template <typename T> void accumulate(int strip, bool post, qint64 first, std::span<const T> samples);
	int m_count = 0;
	AudioMeterWindow m_window;
	qint64 m_origin = 0;
	std::array<std::array<AudioMeterProcessor, 2>, AudioMeterStripLimit> m_strips;
};

struct AudioMeterReport {
	AudioMeterSnapshot meters;
	qint64 first = 0, end = 0;
	int sampleRate = 0;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && meters.count > 0; }
};
AudioMeterReport measureAudioSessionMeters(const AudioSession &session, qint64 first, qint64 end,
                                           int blockFrames = 4096, const AudioWorkControl &control = {},
                                           const std::function<void(qint64, qint64)> &progress = {});
QJsonObject audioMeterReportToJson(const AudioSession &session, const AudioMeterReport &report);
} // namespace vibestudio
