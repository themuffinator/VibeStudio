#pragma once

#include "core/audio_session.h"
#include <array>
#include <span>
#include <vector>

namespace vibestudio
{
inline constexpr int AudioDuplexArmLimit = 8;
inline constexpr int AudioDuplexInputLimit = 32;
inline constexpr int AudioDuplexLoopPassLimit = 10000;
struct AudioDuplexArm {
	QString trackId;
	int channels = 1;
	std::array<int, 2> channelMap{0, 1}; // Zero-based hardware inputs; no implicit summing.
	bool monitor = false;
	bool replacePlayback = false; // Suppress this track's clips during the punch only.
	double monitorGain = .25;
};
struct AudioDuplexPass {
	qint64 playbackFirst = 0, punchFirst = 0, punchEnd = 0;
	// Additional measured correction; positive shifts capture earlier. Device
	// input/output timestamps and graph latency are already accounted for once.
	qint64 calibrationFrames = 0;
	int inputChannels = 1, blockFrames = 1024;
	// Maximum accepted deviation from the initial sample clock, in frames.
	// A backend chooses this explicitly for its timestamp precision/jitter.
	int clockToleranceFrames = 0;
	double outputGain = 1;
	QVector<AudioDuplexArm> arms;
	// Repeat [punchFirst,punchEnd), keeping the device clock and DSP continuous.
	// Journals concatenate passes; playback preroll runs only once.
	int loopPasses = 1;
};
// Exclusive end on the unwrapped capture timeline, or -1 for invalid bounds.
qint64 audioDuplexCaptureEnd(const AudioDuplexPass &pass);
struct AudioDuplexTime {
	// First ADC/DAC samples in the same monotonic seconds timebase. These can
	// be driver estimates; the adapter must report that limitation to the user.
	double inputAdc = 0, outputDac = 0;
	bool priming = false;
	bool inputOverflow = false, inputUnderflow = false;
	bool outputOverflow = false, outputUnderflow = false;
};
enum class AudioDuplexFault {
	None,
	InvalidBlock,
	InputNotFinite,
	TimingUnavailable,
	TimingDiscontinuity,
	InsufficientPreroll,
	MonitorTimelineLimit,
	InputOverflow,
	InputUnderflow,
	OutputOverflow,
	OutputUnderflow,
	RenderFailure,
	CaptureQueueFull
};
QString audioDuplexFaultText(AudioDuplexFault fault);
struct AudioDuplexCaptureBlock {
	qint64 first = 0; // Timeline placement, already corrected; contiguous within the punch.
	int frames = 0, arms = 0;
	std::array<std::span<const float>, AudioDuplexArmLimit> samples{};
};
// push must only copy to preallocated bounded storage. No locks, allocation,
// file I/O, notifications, logging or GUI work may occur here. Span lifetimes
// end on return. The disk worker owns durable journals and their finalization.
class AudioDuplexCaptureSink {
  public:
	virtual ~AudioDuplexCaptureSink() = default;
	virtual bool push(const AudioDuplexCaptureBlock &block) noexcept = 0;
};
struct AudioDuplexProgress {
	enum class State { Empty, Ready, Running, Complete, Stopped, Error };
	State state = State::Empty;
	AudioDuplexFault fault = AudioDuplexFault::None;
	qint64 processedFrames = 0, capturedFrames = 0, primingFrames = 0;
	qint64 inputTimelineOrigin = 0, roundTripFrames = 0, maximumClockDeviationFrames = 0;
	int processingLatencyFrames = 0;
	std::array<float, 2> outputPeak{};
	quint64 outputSamplesAboveFullScale = 0;
};
// Transient fixed-size readings, separate from durable recording receipts.
// Dry input includes preroll until the final captured input frame. Output is
// measured after audition gain and before safety clipping.
struct AudioDuplexMeters {
	int count = 0;
	std::array<int, AudioDuplexArmLimit> channels{};
	std::array<AudioMeterReading, AudioDuplexArmLimit> input{};
	AudioMeterReading output;
};
// Device-neutral, single-owner full-duplex pass. Prepare away from the callback;
// process/stop perform no allocation, I/O, locks or QObject access. The adapter
// supplies one input/output clock and publishes snapshots through a bounded
// queue. Real hardware clock/latency acceptance is separate from these rules.
class AudioDuplexProcessor {
  public:
	bool prepare(const AudioSession &session, const AudioDuplexPass &pass, QString *error,
	             const AudioWorkControl &control = {});
	AudioDuplexProgress::State process(std::span<const float> input, std::span<float> stereo,
	                                   const AudioDuplexTime &time, AudioDuplexCaptureSink &capture) noexcept;
	void stop() noexcept;
	[[nodiscard]] AudioDuplexProgress progress() const { return m_progress; }
	[[nodiscard]] AudioMeterSnapshot meters() const { return m_renderer.meters(); }
	[[nodiscard]] AudioDuplexMeters levels() const;
	// Same owner as process(); changes meter history only, never DSP/capture.
	void resetMetering() noexcept;
	[[nodiscard]] int sampleRate() const { return m_rate; }

  private:
	AudioDuplexProgress::State fail(AudioDuplexFault fault, std::span<float> output) noexcept;
	bool timing(const AudioDuplexTime &time) noexcept;
	AudioSessionRenderer m_renderer;
	AudioDuplexPass m_pass;
	AudioDuplexProgress m_progress;
	std::array<AudioMeterProcessor, AudioDuplexArmLimit> m_inputMeters;
	AudioMeterProcessor m_outputMeter;
	std::array<int, AudioDuplexArmLimit> m_trackIndices{};
	std::vector<double> m_scratch;
	std::vector<float> m_selected;
	int m_rate = 0;
	bool m_monitoring = false;
	double m_firstInputTime = 0, m_firstOutputTime = 0;
	qint64 m_captureEnd = 0, m_processEnd = 0;
	qint64 m_playbackEnd = 0;
};
} // namespace vibestudio
