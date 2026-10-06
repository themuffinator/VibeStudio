#include "core/audio_duplex.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <type_traits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool near(double actual, double expected)
{
	return std::abs(actual - expected) < 2e-10 * std::max(1.0, std::abs(expected));
}
float input(qint64 frame, int channel)
{
	if (channel == 0)
		return frame < 100 ? .25f : 0;
	if (channel == 1)
		return frame == 200 ? 1.25f : (frame % 2 ? -.5f : .5f);
	return frame < 10 ? -2.f : .125f;
}
AudioSession session()
{
	AudioSession value;
	value.sampleRate = 1000;
	value.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "source";
	source.audio.clip = {2, 1000, QVector<float>(2000, .25f)};
	source.audio.endFrame = 1000;
	value.sources = {source};
	for (int index = 0; index < 2; ++index) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(index);
		if (!index)
			track.regions = {{"backing", "Backing", source.id, 0, 0, 1000}};
		value.tracks << track;
	}
	return value;
}
struct Sink final : AudioDuplexCaptureSink {
	std::array<float, 1800> stereo{};
	std::array<float, 900> mono{};
	int frames = 0;
	bool push(const AudioDuplexCaptureBlock &block) noexcept override
	{
		if (block.arms != 2 || frames + block.frames > 900)
			return false;
		std::copy(block.samples[0].begin(), block.samples[0].end(), stereo.begin() + frames * 2);
		std::copy(block.samples[1].begin(), block.samples[1].end(), mono.begin() + frames);
		frames += block.frames;
		return true;
	}
};
bool verify(const AudioMeterReading &reading, int channel, int first, int end)
{
	long double energy = 0, sum = 0, peak = 0, maximum = 0;
	quint64 overs = 0;
	const long double memory = std::exp(-1.L / 300), release = std::pow(10.L, -24.L / 20000);
	for (int frame = first; frame < end; ++frame) {
		const long double sample = input(frame, channel), magnitude = std::abs(sample);
		sum += sample * sample;
		energy = memory * energy + (1 - memory) * sample * sample;
		maximum = std::max(maximum, magnitude);
		peak = std::max(magnitude, peak * release);
		overs += magnitude > 1;
	}
	return reading.valid && reading.frames == quint64(end - first) && reading.lastFrame == end - 26 &&
	       near(reading.maximum[0], double(maximum)) && near(reading.peak[0], double(peak)) &&
	       near(reading.rms[0], double(std::sqrt(energy))) &&
	       near(reading.integratedRms[0], double(std::sqrt(sum / (end - first)))) &&
	       reading.samplesAboveFullScale[0] == overs;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	static_assert(std::is_trivially_copyable_v<AudioDuplexMeters>);
	bool ok = true;
	AudioDuplexPass pass;
	pass.punchFirst = 100;
	pass.punchEnd = 1000;
	pass.inputChannels = 3;
	pass.blockFrames = 17;
	pass.outputGain = .5;
	pass.arms = {{"0", 2, {2, 0}}, {"1", 1, {1, 0}}};
	Sink reference;
	for (int block : {1, 7, 256, 4096}) {
		for (bool reset : {false, true}) {
			AudioDuplexProcessor processor;
			QString error;
			if (!expect(processor.prepare(session(), pass, &error), error.toUtf8().constData()))
				return EXIT_FAILURE;
			Sink sink;
			std::vector<float> samples(size_t(block * 3)), output(size_t(block * 2));
			AudioDuplexTime priming;
			priming.priming = true;
			processor.process({}, output, priming, sink);
			ok &= expect(processor.levels().count == 2 && processor.levels().input[0].frames == 0,
			             "priming does not meter fabricated input");
			qint64 frame = 0;
			bool cleared = false;
			while (processor.progress().state != AudioDuplexProgress::State::Complete && frame < 5000) {
				int frames = block;
				if (reset && !cleared)
					frames = int(std::min<qint64>(frames, 300 - frame));
				if (!frames) {
					const auto captured = processor.progress().capturedFrames;
					processor.resetMetering();
					cleared = true;
					ok &= expect(processor.levels().input[0].frames == 0 && processor.levels().output.frames == 0 &&
					                 processor.progress().capturedFrames == captured,
					             "meter reset never rewinds capture or clock");
					continue;
				}
				for (int f = 0; f < frames; ++f)
					for (int c = 0; c < 3; ++c)
						samples[size_t(f * 3 + c)] = input(frame + f, c);
				const auto status = processor.process(
				    std::span(samples).first(size_t(frames * 3)), std::span(output).first(size_t(frames * 2)),
				    {10. + double(frame) / 1000, 10.025 + double(frame) / 1000}, sink);
				if (status == AudioDuplexProgress::State::Error) {
					std::cerr << audioDuplexFaultText(processor.progress().fault).toStdString() << '\n';
					return EXIT_FAILURE;
				}
				for (int f = 0; f < frames; ++f) {
					const float expected = frame + f < 1000 ? .125f : 0;
					ok &= expect(output[size_t(f * 2)] == expected && output[size_t(f * 2 + 1)] == expected,
					             "metering and reset leave output samples unchanged");
				}
				frame += frames;
			}
			const auto levels = processor.levels();
			const int first = reset ? 300 : 0;
			ok &=
			    expect(levels.count == 2 && levels.channels[0] == 2 && levels.channels[1] == 1 &&
			               verify(levels.input[0], 2, first, 1025) && verify(levels.input[1], 1, first, 1025),
			           "dry reordered channels match independent peak/RMS/maximum/count oracle across callback sizes");
			auto right = levels.input[0];
			right.peak[0] = right.peak[1];
			right.maximum[0] = right.maximum[1];
			right.rms[0] = right.rms[1];
			right.integratedRms[0] = right.integratedRms[1];
			right.samplesAboveFullScale[0] = right.samplesAboveFullScale[1];
			ok &= expect(verify(right, 0, first, 1025) && levels.input[1].peak[1] == 0 &&
			                 !levels.input[1].correlationValid,
			             "stereo order is retained and mono does not invent a second signal");
			const auto &device = levels.output;
			const double memory = std::exp(-1.0 / 300);
			ok &= expect(device.frames == quint64(1025 - first) && device.maximum[0] == .125 &&
			                 near(device.rms[0],
			                      .125 * std::sqrt((1 - std::pow(memory, 1000 - first)) * std::pow(memory, 25))) &&
			                 device.samplesAboveFullScale[0] == 0,
			             "output meter follows audition gain and counts latency drain silence");
			if (block == 1 && !reset)
				reference = sink;
			ok &= expect(sink.frames == 900 && sink.stereo == reference.stereo && sink.mono == reference.mono,
			             "meter block size and reset never change dry capture data");
		}
	}
	AudioDuplexProcessor clipping;
	QString error;
	pass.punchFirst = 0;
	pass.punchEnd = 2;
	pass.outputGain = 1;
	pass.arms[0].monitor = true;
	pass.arms[0].monitorGain = 1;
	ok &= expect(clipping.prepare(session(), pass, &error), "prepare clipping meter fixture");
	Sink sink;
	std::array<float, 6> loud{2, 3, 4, 2, 3, 4};
	std::array<float, 4> output;
	clipping.process(loud, output, {10, 10}, sink);
	ok &= expect(clipping.levels().output.maximum[0] == 4.25 && clipping.levels().output.maximum[1] == 2.25 &&
	                 clipping.levels().output.samplesAboveFullScale[0] == 2 && output[0] == 1 && output[1] == 1 &&
	                 clipping.levels().input[0].maximum[0] == 4 && sink.stereo[0] == 4,
	             "device meter exposes actual output clipping while dry meter and capture retain input headroom");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
