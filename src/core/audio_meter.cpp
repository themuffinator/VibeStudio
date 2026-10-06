#include "core/audio_meter.h"
#include "core/audio_session.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
static_assert(AudioMeterStripLimit == AudioSessionTrackLimit + 1);
namespace
{
QString problem(const char *text) { return QCoreApplication::translate("AudioMeters", text); }
void increment(quint64 &value)
{
	if (value != std::numeric_limits<quint64>::max())
		++value;
}
double correlation(const std::array<double, 2> &energy, double cross)
{
	return std::clamp(cross / std::sqrt(energy[0]) / std::sqrt(energy[1]), -1.0, 1.0);
}
QJsonValue db(double value) { return value > 0 ? QJsonValue(20 * std::log10(value)) : QJsonValue(QJsonValue::Null); }
QJsonObject readingJson(const AudioMeterReading &reading)
{
	QJsonArray channels;
	for (size_t i = 0; i < 2; ++i)
		channels.append(QJsonObject{{"peak", reading.peak[i]},
		                            {"peakDbfs", db(reading.peak[i])},
		                            {"maximum", reading.maximum[i]},
		                            {"maximumDbfs", db(reading.maximum[i])},
		                            {"rms", reading.rms[i]},
		                            {"rmsDbfs", db(reading.rms[i])},
		                            {"integratedRms", reading.integratedRms[i]},
		                            {"integratedRmsDbfs", db(reading.integratedRms[i])},
		                            {"samplesAboveFullScale", qint64(reading.samplesAboveFullScale[i])}});
	return {{"frames", qint64(reading.frames)},
	        {"lastFrame", reading.lastFrame},
	        {"valid", reading.valid},
	        {"channels", channels},
	        {"correlation", reading.correlationValid ? QJsonValue(reading.correlation) : QJsonValue(QJsonValue::Null)},
	        {"integratedCorrelation", reading.integratedCorrelationValid ? QJsonValue(reading.integratedCorrelation)
	                                                                     : QJsonValue(QJsonValue::Null)}};
}
} // namespace
void AudioMeterProcessor::prepare(int sampleRate)
{
	m_release = std::pow(10.0, -AudioMeterDecayDbPerSecond / (20 * std::max(1, sampleRate)));
	m_memory = std::exp(-1.0 / (AudioMeterRmsSeconds * std::max(1, sampleRate)));
	reset();
}
void AudioMeterProcessor::reset()
{
	m_reading = {};
	m_scale = m_cross = m_crossSum = 0;
	m_energy = m_sum = {};
}
void AudioMeterProcessor::tick(double left, double right, qint64 frame)
{
	m_reading.lastFrame = frame;
	if (!std::isfinite(left) || !std::isfinite(right)) {
		m_reading.valid = false;
		return;
	}
	// Normalize energies by the largest amplitude encountered. Squaring a
	// finite internal bus sample must not overflow the meter's accumulators.
	const double scale = std::max(std::abs(left), std::abs(right));
	if (scale > m_scale) {
		const double ratio = m_scale / scale, factor = ratio * ratio;
		for (size_t i = 0; i < 2; ++i) {
			m_energy[i] *= factor;
			m_sum[i] *= factor;
		}
		m_cross *= factor;
		m_crossSum *= factor;
		m_scale = scale;
	}
	const std::array<double, 2> samples{left, right};
	std::array<double, 2> normalized{};
	const bool accumulate = m_reading.frames < std::numeric_limits<quint64>::max();
	increment(m_reading.frames);
	for (size_t i = 0; i < 2; ++i) {
		const double amplitude = std::abs(samples[i]);
		m_reading.peak[i] = std::max(amplitude, m_reading.peak[i] * m_release);
		m_reading.maximum[i] = std::max(amplitude, m_reading.maximum[i]);
		if (amplitude > 1)
			increment(m_reading.samplesAboveFullScale[i]);
		normalized[i] = m_scale ? samples[i] / m_scale : 0;
		const double square = normalized[i] * normalized[i];
		m_energy[i] = m_memory * m_energy[i] + (1 - m_memory) * square;
		if (accumulate)
			m_sum[i] += square;
	}
	const double cross = normalized[0] * normalized[1];
	m_cross = m_memory * m_cross + (1 - m_memory) * cross;
	if (accumulate)
		m_crossSum += cross;
}
AudioMeterReading AudioMeterProcessor::reading() const
{
	auto result = m_reading;
	for (size_t i = 0; i < 2; ++i) {
		result.rms[i] = m_scale * std::sqrt(std::clamp(m_energy[i], 0.0, 1.0));
		result.integratedRms[i] =
		    result.frames ? m_scale * std::sqrt(std::clamp(m_sum[i] / double(result.frames), 0.0, 1.0)) : 0;
	}
	result.correlationValid = result.valid && m_energy[0] > 0 && m_energy[1] > 0;
	result.integratedCorrelationValid = result.valid && m_sum[0] > 0 && m_sum[1] > 0;
	if (result.correlationValid)
		result.correlation = correlation(m_energy, m_cross);
	if (result.integratedCorrelationValid)
		result.integratedCorrelation = correlation(m_sum, m_crossSum);
	return result;
}
void AudioMeterBank::prepare(int count, int sampleRate, AudioMeterWindow window)
{
	m_count = std::clamp(count, 0, AudioMeterStripLimit);
	m_window = window;
	m_origin = window.first;
	for (auto &strip : m_strips)
		for (auto &tap : strip)
			tap.prepare(sampleRate);
}
void AudioMeterBank::reset()
{
	for (auto &strip : m_strips)
		for (auto &tap : strip)
			tap.reset();
}
template <typename T> void AudioMeterBank::accumulate(int strip, bool post, qint64 first, std::span<const T> samples)
{
	if (!enabled() || strip < 0 || strip >= m_count)
		return;
	const qint64 begin = std::max<qint64>(0, std::max(m_window.first, m_origin) - first);
	const qint64 end = std::min<qint64>(qint64(samples.size() / 2), m_window.end - first);
	for (qint64 frame = begin; frame < end; ++frame)
		m_strips[size_t(strip)][post].tick(samples[size_t(frame) * 2], samples[size_t(frame) * 2 + 1], first + frame);
}
void AudioMeterBank::process(int strip, bool post, qint64 first, std::span<const double> samples)
{
	accumulate(strip, post, first, samples);
}
void AudioMeterBank::process(int strip, bool post, qint64 first, std::span<const float> samples)
{
	accumulate(strip, post, first, samples);
}
AudioMeterSnapshot AudioMeterBank::snapshot() const
{
	AudioMeterSnapshot result;
	result.count = enabled() ? m_count : 0;
	for (int i = 0; i < result.count; ++i) {
		result.strips[size_t(i)].pre = m_strips[size_t(i)][0].reading();
		result.strips[size_t(i)].post = m_strips[size_t(i)][1].reading();
	}
	return result;
}
AudioMeterReport measureAudioSessionMeters(const AudioSession &session, qint64 first, qint64 end, int blockFrames,
                                           const AudioWorkControl &control,
                                           const std::function<void(qint64, qint64)> &progress)
{
	AudioMeterReport result;
	result.first = first;
	result.end = end;
	result.sampleRate = session.sampleRate;
	const auto cancelled = [&] { return control.cancelled && control.cancelled(); };
	if (first < 0 || end <= first || end > AudioSessionFrameLimit || blockFrames < 1 || blockFrames > 65536) {
		result.error = problem(
		    QT_TRANSLATE_NOOP("AudioMeters", "Choose a nonempty meter range and a block size of 1–65536 frames."));
		return result;
	}
	AudioSessionRenderer renderer;
	if (!renderer.prepare(session, &result.error, control, {}, {true, first, end})) {
		result.cancelled = cancelled();
		return result;
	}
	std::vector<float> output(size_t(blockFrames) * 2);
	std::vector<double> scratch(renderer.scratchSamples(blockFrames));
	auto status = AudioSessionRenderer::BlockStatus::Ready;
	for (qint64 at = first; at < end;) {
		const int frames = int(std::min<qint64>(blockFrames, end - at));
		status = renderer.renderInto(at, std::span(output).first(size_t(frames) * 2), scratch, control);
		if (status != AudioSessionRenderer::BlockStatus::Ready)
			break;
		at += frames;
		if (progress)
			progress(at - first, end - first);
	}
	if (status == AudioSessionRenderer::BlockStatus::Ready)
		status = renderer.finishMetering(end, control);
	result.cancelled = status == AudioSessionRenderer::BlockStatus::Cancelled || cancelled();
	if (result.cancelled)
		return result;
	if (status != AudioSessionRenderer::BlockStatus::Ready) {
		result.error = problem(
		    QT_TRANSLATE_NOOP("AudioMeters", "Meter analysis failed because the mix exceeds floating-point headroom."));
		return result;
	}
	const auto snapshot = renderer.meters();
	for (int i = 0; i < snapshot.count; ++i)
		for (const auto *tap : {&snapshot.strips[size_t(i)].pre, &snapshot.strips[size_t(i)].post})
			if (!tap->valid || tap->frames != quint64(end - first)) {
				result.error = problem(QT_TRANSLATE_NOOP(
				    "AudioMeters", "Meter analysis did not produce a complete finite reading for every signal point."));
				return result;
			}
	result.meters = snapshot;
	return result;
}
QJsonObject audioMeterReportToJson(const AudioSession &session, const AudioMeterReport &report)
{
	QJsonArray strips;
	if (report.succeeded() && report.meters.count == session.tracks.size() + 1)
		for (int i = 0; i < report.meters.count; ++i) {
			const bool master = i == session.tracks.size();
			const auto &meter = report.meters.strips[size_t(i)];
			strips.append(QJsonObject{
			    {"id", master ? QString() : session.tracks[i].id},
			    {"name", master ? problem(QT_TRANSLATE_NOOP("AudioMeters", "Master")) : session.tracks[i].name},
			    {"type", master                          ? "master"
			             : session.tracks[i].routing.bus ? "bus"
			                                             : "track"},
			    {"pre", readingJson(meter.pre)},
			    {"post", readingJson(meter.post)}});
		}
	return {{"firstFrame", report.first},
	        {"endFrame", report.end},
	        {"sampleRate", report.sampleRate},
	        {"peakType", "sample"},
	        {"peakDecayDbPerSecond", AudioMeterDecayDbPerSecond},
	        {"rmsTimeConstantSeconds", AudioMeterRmsSeconds},
	        {"strips", strips}};
}
} // namespace vibestudio
