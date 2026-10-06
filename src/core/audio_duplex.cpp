#include "core/audio_duplex.h"
#include <QCoreApplication>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vibestudio
{
namespace
{
QString duplexText(const char *text) { return QCoreApplication::translate("AudioDuplex", text); }
bool preciseTime(double value, int rate)
{
	return std::isfinite(value) &&
	       std::abs(std::nextafter(value, std::numeric_limits<double>::infinity()) - value) * rate < .25;
}
} // namespace
qint64 audioDuplexCaptureEnd(const AudioDuplexPass &pass)
{
	if (pass.punchFirst < 0 || pass.punchEnd <= pass.punchFirst || pass.punchEnd > AudioSessionFrameLimit ||
	    pass.loopPasses < 1 || pass.loopPasses > AudioDuplexLoopPassLimit ||
	    pass.punchEnd - pass.punchFirst > (AudioSessionFrameLimit - pass.punchFirst) / pass.loopPasses)
		return -1;
	return pass.punchFirst + (pass.punchEnd - pass.punchFirst) * pass.loopPasses;
}
QString audioDuplexFaultText(AudioDuplexFault fault)
{
	switch (fault) {
	case AudioDuplexFault::None:
		return {};
	case AudioDuplexFault::InvalidBlock:
		return duplexText(QT_TRANSLATE_NOOP("AudioDuplex", "The duplex device supplied an invalid audio block."));
	case AudioDuplexFault::InputNotFinite:
		return duplexText(QT_TRANSLATE_NOOP(
		    "AudioDuplex", "The input contains nonfinite samples. The verified take prefix is retained."));
	case AudioDuplexFault::TimingUnavailable:
		return duplexText(
		    QT_TRANSLATE_NOOP("AudioDuplex", "The duplex device did not supply usable input and output timestamps."));
	case AudioDuplexFault::TimingDiscontinuity:
		return duplexText(QT_TRANSLATE_NOOP(
		    "AudioDuplex",
		    "The device clock changed beyond the configured tolerance. Recording stopped to preserve alignment."));
	case AudioDuplexFault::InsufficientPreroll:
		return duplexText(QT_TRANSLATE_NOOP(
		    "AudioDuplex", "Start playback earlier so the calibrated input covers the beginning of the punch."));
	case AudioDuplexFault::MonitorTimelineLimit:
		return duplexText(
		    QT_TRANSLATE_NOOP("AudioDuplex", "Leave room before the timeline limit for the input monitoring delay."));
	case AudioDuplexFault::InputOverflow:
		return duplexText(QT_TRANSLATE_NOOP(
		    "AudioDuplex", "Input samples were lost. Recording stopped and retains the verified prefix."));
	case AudioDuplexFault::InputUnderflow:
		return duplexText(QT_TRANSLATE_NOOP(
		    "AudioDuplex", "Input samples were unavailable. Recording stopped and retains the verified prefix."));
	case AudioDuplexFault::OutputOverflow:
		return duplexText(
		    QT_TRANSLATE_NOOP("AudioDuplex", "The output stream overflowed. Recording stopped to preserve alignment."));
	case AudioDuplexFault::OutputUnderflow:
		return duplexText(
		    QT_TRANSLATE_NOOP("AudioDuplex", "Playback ran out of samples. Recording stopped to preserve alignment."));
	case AudioDuplexFault::RenderFailure:
		return duplexText(QT_TRANSLATE_NOOP(
		    "AudioDuplex",
		    "The session mix could not be rendered. Recording stopped and retains the verified prefix."));
	case AudioDuplexFault::CaptureQueueFull:
		return duplexText(QT_TRANSLATE_NOOP(
		    "AudioDuplex", "The recording queue could not accept more audio. The queued prefix is retained."));
	}
	return duplexText(QT_TRANSLATE_NOOP("AudioDuplex", "The duplex recording stopped with an unknown error."));
}
bool AudioDuplexProcessor::prepare(const AudioSession &session, const AudioDuplexPass &pass, QString *error,
                                   const AudioWorkControl &control)
{
	m_progress = {};
	m_renderer.resetProcessing();
	m_renderer.resetMetering();
	m_rate = 0;
	m_monitoring = false;
	m_scratch = {};
	m_selected = {};
	m_pass = {};
	m_captureEnd = m_processEnd = 0;
	m_playbackEnd = audioDuplexCaptureEnd(pass);
	resetMetering();
	const auto reject = [&](const char *text) {
		if (error)
			*error = duplexText(text);
		return false;
	};
	if (error)
		error->clear();
	if (pass.playbackFirst < 0 || pass.punchFirst < pass.playbackFirst || pass.punchEnd <= pass.punchFirst ||
	    m_playbackEnd < 0 || pass.inputChannels < 1 || pass.inputChannels > AudioDuplexInputLimit ||
	    pass.blockFrames < 1 || pass.blockFrames > 4096 || pass.clockToleranceFrames < 0 ||
	    pass.clockToleranceFrames > session.sampleRate || pass.arms.isEmpty() ||
	    pass.arms.size() > AudioDuplexArmLimit || !std::isfinite(pass.outputGain) || pass.outputGain < 0 ||
	    pass.outputGain > 1 || pass.calibrationFrames < -qint64(session.sampleRate) * 10 ||
	    pass.calibrationFrames > qint64(session.sampleRate) * 10)
		return reject(QT_TRANSLATE_NOOP(
		    "AudioDuplex",
		    "Choose a valid playback start, punch range, input format, buffer, timing tolerance and armed tracks."));
	QSet<QString> armed;
	for (qsizetype i = 0; i < pass.arms.size(); ++i) {
		const auto &arm = pass.arms[i];
		int index = -1;
		for (qsizetype j = 0; j < session.tracks.size(); ++j)
			if (session.tracks[j].id == arm.trackId && !session.tracks[j].routing.bus)
				index = int(j);
		if (index < 0 || armed.contains(arm.trackId) || (arm.channels != 1 && arm.channels != 2) ||
		    arm.channelMap[0] < 0 || arm.channelMap[0] >= pass.inputChannels ||
		    (arm.channels == 2 && (arm.channelMap[1] < 0 || arm.channelMap[1] >= pass.inputChannels ||
		                           arm.channelMap[0] == arm.channelMap[1])) ||
		    !std::isfinite(arm.monitorGain) || arm.monitorGain < 0 || arm.monitorGain > 1 ||
		    (arm.replacePlayback && !arm.monitor))
			return reject(QT_TRANSLATE_NOOP(
			    "AudioDuplex",
			    "Arm each existing audio track once with a mono or stereo input map and a valid monitoring level."));
		armed.insert(arm.trackId);
		m_trackIndices[size_t(i)] = index;
		m_monitoring |= arm.monitor;
	}
	if (!m_renderer.prepare(session, error, control, {}, {true, pass.playbackFirst, m_playbackEnd},
	                        AudioSessionRenderClock::Live, {pass.loopPasses > 1, pass.punchFirst, pass.punchEnd}))
		return false;
	m_scratch.resize(m_renderer.scratchSamples(pass.blockFrames));
	m_selected.resize(size_t(pass.arms.size()) * size_t(pass.blockFrames) * 2);
	m_pass = pass;
	m_rate = session.sampleRate;
	for (auto &meter : m_inputMeters)
		meter.prepare(m_rate);
	m_outputMeter.prepare(m_rate);
	m_progress.processingLatencyFrames = m_renderer.processingLatencyFrames();
	m_progress.state = AudioDuplexProgress::State::Ready;
	return true;
}
AudioDuplexProgress::State AudioDuplexProcessor::fail(AudioDuplexFault fault, std::span<float> output) noexcept
{
	std::fill(output.begin(), output.end(), 0);
	m_progress.fault = fault;
	m_progress.state = AudioDuplexProgress::State::Error;
	return m_progress.state;
}
bool AudioDuplexProcessor::timing(const AudioDuplexTime &time) noexcept
{
	if (!preciseTime(time.inputAdc, m_rate) || !preciseTime(time.outputDac, m_rate)) {
		m_progress.fault = AudioDuplexFault::TimingUnavailable;
		return false;
	}
	if (m_progress.state == AudioDuplexProgress::State::Ready) {
		const long double roundTrip = (static_cast<long double>(time.outputDac) - time.inputAdc) * m_rate;
		if (roundTrip < -.5 || roundTrip > m_rate * 10.L || (time.inputAdc == 0 && time.outputDac == 0)) {
			m_progress.fault = AudioDuplexFault::TimingUnavailable;
			return false;
		}
		m_firstInputTime = time.inputAdc;
		m_firstOutputTime = time.outputDac;
		m_progress.roundTripFrames = std::llround(roundTrip);
		m_progress.inputTimelineOrigin = m_pass.playbackFirst - m_progress.roundTripFrames -
		                                 m_progress.processingLatencyFrames - m_pass.calibrationFrames;
		if (m_progress.inputTimelineOrigin > m_pass.punchFirst) {
			m_progress.fault = AudioDuplexFault::InsufficientPreroll;
			return false;
		}
		m_captureEnd = m_playbackEnd - m_progress.inputTimelineOrigin;
		if (m_monitoring && m_pass.playbackFirst + m_captureEnd > AudioSessionFrameLimit) {
			m_progress.fault = AudioDuplexFault::MonitorTimelineLimit;
			return false;
		}
		m_processEnd = std::max(m_captureEnd + (m_monitoring ? m_progress.processingLatencyFrames : 0),
		                        m_playbackEnd - m_pass.playbackFirst + m_progress.processingLatencyFrames);
		m_progress.state = AudioDuplexProgress::State::Running;
		return true;
	}
	const long double inputDeviation =
	    (static_cast<long double>(time.inputAdc) - m_firstInputTime) * m_rate - m_progress.processedFrames;
	const long double outputDeviation =
	    (static_cast<long double>(time.outputDac) - m_firstOutputTime) * m_rate - m_progress.processedFrames;
	const auto deviation = std::max(std::abs(inputDeviation), std::abs(outputDeviation));
	m_progress.maximumClockDeviationFrames =
	    std::max(m_progress.maximumClockDeviationFrames,
	             std::llround(std::min(deviation, static_cast<long double>(AudioSessionFrameLimit))));
	// Check before conversion; malformed finite timestamps must not overflow an
	// integer or shift/reorder samples to hide a discontinuity.
	if (deviation > static_cast<long double>(m_pass.clockToleranceFrames) + .5L) {
		m_progress.fault = AudioDuplexFault::TimingDiscontinuity;
		return false;
	}
	return true;
}
AudioDuplexProgress::State AudioDuplexProcessor::process(std::span<const float> input, std::span<float> stereo,
                                                         const AudioDuplexTime &time,
                                                         AudioDuplexCaptureSink &capture) noexcept
{
	std::fill(stereo.begin(), stereo.end(), 0);
	if (m_progress.state != AudioDuplexProgress::State::Ready &&
	    m_progress.state != AudioDuplexProgress::State::Running)
		return m_progress.state;
	if (stereo.empty() || stereo.size() % 2 || stereo.size() > 131072 ||
	    (!(time.priming && input.empty()) && input.size() != stereo.size() / 2 * size_t(m_pass.inputChannels)))
		return fail(AudioDuplexFault::InvalidBlock, stereo);
	const auto inputAddress = reinterpret_cast<std::uintptr_t>(input.data());
	const auto outputAddress = reinterpret_cast<std::uintptr_t>(stereo.data());
	if (!input.empty() && (inputAddress <= outputAddress ? outputAddress - inputAddress < input.size_bytes()
	                                                     : inputAddress - outputAddress < stereo.size_bytes()))
		return fail(AudioDuplexFault::InvalidBlock, stereo);
	if (time.priming) {
		if (m_progress.state != AudioDuplexProgress::State::Ready)
			return fail(AudioDuplexFault::TimingDiscontinuity, stereo);
		m_progress.primingFrames =
		    std::min(AudioSessionFrameLimit, m_progress.primingFrames + qint64(stereo.size() / 2));
		return m_progress.state;
	}
	if (time.inputOverflow)
		return fail(AudioDuplexFault::InputOverflow, stereo);
	if (time.inputUnderflow)
		return fail(AudioDuplexFault::InputUnderflow, stereo);
	if (time.outputOverflow)
		return fail(AudioDuplexFault::OutputOverflow, stereo);
	if (time.outputUnderflow)
		return fail(AudioDuplexFault::OutputUnderflow, stereo);
	if (!timing(time))
		return fail(m_progress.fault, stereo);
	for (float value : input)
		if (!std::isfinite(value))
			return fail(AudioDuplexFault::InputNotFinite, stereo);
	const AudioDuplexPass &pass = m_pass;
	const int callbackFrames = int(stereo.size() / 2);
	for (int at = 0; at < callbackFrames && m_progress.processedFrames < m_processEnd;) {
		const auto physicalFirst = pass.playbackFirst + m_progress.processedFrames;
		int frames =
		    int(std::min<qint64>({pass.blockFrames, callbackFrames - at, m_processEnd - m_progress.processedFrames}));
		// Split changes in tape-style monitoring and the final live-input drain.
		for (qint64 boundary : {pass.punchFirst - pass.playbackFirst, m_playbackEnd - pass.playbackFirst, m_captureEnd})
			if (boundary > m_progress.processedFrames)
				frames = int(std::min<qint64>(frames, boundary - m_progress.processedFrames));
		std::array<AudioSessionLiveInput, AudioDuplexArmLimit> live{};
		std::array<std::span<const float>, AudioDuplexArmLimit> selected{};
		int liveCount = 0;
		for (qsizetype armIndex = 0; armIndex < pass.arms.size(); ++armIndex) {
			const auto &arm = pass.arms[armIndex];
			auto samples = std::span(m_selected)
			                   .subspan(size_t(armIndex) * size_t(pass.blockFrames) * 2, size_t(frames * arm.channels));
			for (int f = 0; f < frames; ++f)
				for (int channel = 0; channel < arm.channels; ++channel)
					samples[size_t(f * arm.channels + channel)] =
					    input[size_t((at + f) * pass.inputChannels + arm.channelMap[size_t(channel)])];
			selected[size_t(armIndex)] = samples;
			const int measured = int(std::clamp<qint64>(m_captureEnd - m_progress.processedFrames, 0, frames));
			for (int frame = 0; frame < measured; ++frame)
				m_inputMeters[size_t(armIndex)].tick(
				    samples[size_t(frame * arm.channels)], arm.channels == 2 ? samples[size_t(frame * 2 + 1)] : 0,
				    m_progress.inputTimelineOrigin + m_progress.processedFrames + frame);
			if (arm.monitor && m_progress.processedFrames < m_captureEnd)
				live[size_t(liveCount++)] = {m_trackIndices[size_t(armIndex)],
				                             arm.channels,
				                             samples,
				                             0,
				                             arm.monitorGain,
				                             arm.replacePlayback && physicalFirst >= pass.punchFirst &&
				                                 physicalFirst < m_playbackEnd};
		}
		auto output = stereo.subspan(size_t(at * 2), size_t(frames * 2));
		// At the absolute timeline edge, finish receiving delayed input while
		// output remains silent. Monitoring was rejected earlier if it needs this.
		const auto renderEnd = AudioSessionFrameLimit + m_progress.processingLatencyFrames;
		const int renderFrames = int(std::clamp<qint64>(renderEnd - physicalFirst, 0, frames));
		if (renderFrames && m_renderer.renderLiveInto(physicalFirst, output.first(size_t(renderFrames * 2)), m_scratch,
		                                              std::span(live).first(size_t(liveCount)), {},
		                                              m_playbackEnd) != AudioSessionRenderer::BlockStatus::Ready)
			return fail(AudioDuplexFault::RenderFailure, stereo);
		const auto inputFirst = m_progress.inputTimelineOrigin + m_progress.processedFrames;
		const auto captureFirst = std::max(inputFirst, pass.punchFirst);
		const auto captureEnd = std::min(inputFirst + frames, m_playbackEnd);
		if (captureFirst < captureEnd) {
			AudioDuplexCaptureBlock block;
			block.first = captureFirst;
			block.frames = int(captureEnd - captureFirst);
			block.arms = int(pass.arms.size());
			for (int arm = 0; arm < block.arms; ++arm) {
				const int channels = pass.arms[arm].channels;
				block.samples[size_t(arm)] = selected[size_t(arm)].subspan(
				    size_t(captureFirst - inputFirst) * size_t(channels), size_t(block.frames * channels));
			}
			if (!capture.push(block))
				return fail(AudioDuplexFault::CaptureQueueFull, stereo);
			m_progress.capturedFrames += block.frames;
		}
		for (int frame = 0; frame < frames; ++frame) {
			const std::array<float, 2> values{float(output[size_t(frame * 2)] * pass.outputGain),
			                                  float(output[size_t(frame * 2 + 1)] * pass.outputGain)};
			m_outputMeter.tick(values[0], values[1], physicalFirst + frame);
			for (size_t channel = 0; channel < 2; ++channel) {
				const float value = values[channel];
				m_progress.outputPeak[channel] = std::max(m_progress.outputPeak[channel], std::abs(value));
				if (std::abs(value) > 1 && m_progress.outputSamplesAboveFullScale < std::numeric_limits<quint64>::max())
					++m_progress.outputSamplesAboveFullScale;
				output[size_t(frame * 2) + channel] = std::clamp(value, -1.f, 1.f);
			}
		}
		m_progress.processedFrames += frames;
		at += frames;
	}
	if (m_progress.processedFrames == m_processEnd)
		m_progress.state = AudioDuplexProgress::State::Complete;
	return m_progress.state;
}
void AudioDuplexProcessor::stop() noexcept
{
	if (m_progress.state == AudioDuplexProgress::State::Ready ||
	    m_progress.state == AudioDuplexProgress::State::Running)
		m_progress.state = AudioDuplexProgress::State::Stopped;
}
void AudioDuplexProcessor::resetMetering() noexcept
{
	for (auto &meter : m_inputMeters)
		meter.reset();
	m_outputMeter.reset();
	m_renderer.resetMetering();
}
AudioDuplexMeters AudioDuplexProcessor::levels() const
{
	AudioDuplexMeters result;
	result.count = int(m_pass.arms.size());
	for (int arm = 0; arm < result.count; ++arm) {
		result.channels[size_t(arm)] = m_pass.arms[arm].channels;
		result.input[size_t(arm)] = m_inputMeters[size_t(arm)].reading();
	}
	result.output = m_outputMeter.reading();
	return result;
}
} // namespace vibestudio
