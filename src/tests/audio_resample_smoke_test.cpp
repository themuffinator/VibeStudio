#include "core/audio_resample.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
double sine(qint64 frame, int rate, double frequency)
{
	return std::sin(2 * std::numbers::pi * frequency * double(frame) / rate);
}
AudioClip tone(int rate, double frequency, float amplitude = 0.75f)
{
	AudioClip clip{1, rate, QVector<float>(rate)};
	for (int frame = 0; frame < rate; ++frame) {
		clip.samples[frame] = float(amplitude * sine(frame, rate, frequency));
	}
	return clip;
}
double residual(const AudioClip& clip, double frequency, double amplitude, int channel = 0)
{
	// Exclude the finite signal's zero-extended boundaries from steady-state
	// spectral measurements. Compare against an analytic destination-rate tone,
	// not the converter or its filter coefficients.
	const qint64 margin = std::min<qint64>(2048, clip.frameCount() / 4);
	double energy = 0;
	for (qint64 frame = margin; frame < clip.frameCount() - margin; ++frame) {
		const double difference =
		    clip.samples[frame * clip.channels + channel] - amplitude * sine(frame, clip.sampleRate, frequency);
		energy += difference * difference;
	}
	return std::sqrt(energy / (clip.frameCount() - 2 * margin));
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	for (const auto& [from, to] :
	     {std::pair{44100, 48000}, {48000, 44100}, {44100, 11025}, {11025, 48000}, {48000, 8000}, {96000, 192000}}) {
		const AudioClip source = tone(from, 997);
		const auto output = resampleAudioClip(source, to);
		ok &= expect(output.succeeded() && output.clip.frameCount() == to && output.clip.sampleRate == to,
		             "sample rate conversion preserves one-second duration and requested format");
		if (!output.succeeded()) {
			std::cerr << output.error.toStdString() << '\n';
			continue;
		}
		const double error = residual(output.clip, 997, 0.75);
		std::cout << from << " -> " << to << " passband residual RMS " << error << '\n';
		ok &= expect(error < 0.000002, "analytic tone must retain pitch, phase, and passband amplitude");
	}
	for (const double frequency : {7000.0, 19000.0, 31000.0}) {
		const AudioClip source = tone(96000, frequency);
		const auto output = resampleAudioClip(source, 11025);
		ok &= expect(output.succeeded(), "anti-alias fixture conversion succeeds");
		if (!output.succeeded()) {
			continue;
		}
		const double rms = residual(output.clip, 0, 0);
		std::cout << frequency << " Hz rejected at 11025 Hz: RMS " << rms << '\n';
		ok &= expect(rms < 0.0000075, "out-of-band tones must be attenuated by at least 100 dB in RMS");
	}
	AudioClip channels{8, 48000, QVector<float>(48000 * 8)};
	for (int frame = 0; frame < 48000; ++frame) {
		for (int channel = 0; channel < 8; ++channel) {
			channels.samples[frame * 8 + channel] = float(1.5 * sine(frame, 48000, 200 + channel * 177));
		}
	}
	const auto multi = resampleAudioClip(channels, 22050);
	ok &= expect(multi.succeeded() && multi.clip.channels == 8, "eight-channel conversion retains channel layout");
	if (multi.succeeded()) {
		for (int channel = 0; channel < 8; ++channel) {
			ok &= expect(residual(multi.clip, 200 + channel * 177, 1.5, channel) < 0.000004,
			             "independent channels retain alignment and samples above full scale");
		}
	}
	AudioClip impulse{2, 22050, QVector<float>(22050 * 2, 0)};
	impulse.samples[5000 * 2] = 1;
	const auto doubled = resampleAudioClip(impulse, 44100);
	ok &= expect(doubled.succeeded(), "impulse resampling succeeds");
	if (doubled.succeeded()) {
		qint64 maximum = 0;
		for (qint64 frame = 0; frame < doubled.clip.frameCount(); ++frame) {
			if (std::abs(doubled.clip.samples[frame * 2]) > std::abs(doubled.clip.samples[maximum * 2])) {
				maximum = frame;
			}
			ok &= expect(doubled.clip.samples[frame * 2 + 1] == 0, "silent channel has no leakage");
		}
		ok &= expect(maximum == 10000, "linear-phase conversion compensates filter delay at the impulse position");
		for (int offset = 1; offset < 100; ++offset) {
			ok &= expect(std::abs(doubled.clip.samples[(10000 - offset) * 2] -
			                      doubled.clip.samples[(10000 + offset) * 2]) < 0.000001,
			             "impulse response is symmetric around its time position");
		}
	}
	AudioClip exact{2, 8000, {-0.0f, 2.5f, -2.0f, std::numeric_limits<float>::denorm_min()}};
	const auto unchanged = resampleAudioClip(exact, 8000);
	ok &= expect(unchanged.succeeded() && unchanged.clip.samples == exact.samples &&
	                 std::signbit(unchanged.clip.samples[0]),
	             "same-rate conversion preserves exact samples including negative zero and headroom");
	const auto empty = resampleAudioClip({2, 8000, {}}, 44100);
	ok &= expect(empty.succeeded() && empty.clip.sampleRate == 44100 && empty.clip.frameCount() == 0,
	             "empty documents can change sample rate without inventing samples");
	const auto single = resampleAudioClip({1, 96000, {0.5f}}, 8000);
	ok &= expect(single.succeeded() && single.clip.frameCount() == 1, "sub-frame durations retain one filtered sample");
	ok &= expect(audioFrameAtSampleRate(3, 8000, 11025) == 4 && audioFrameAtSampleRate(1, 2, 1) == 1 &&
	                 audioFrameAtSampleRate(1, 0, 1) == -1 && audioFrameAtSampleRate(-1, 8000, 11025) == -1 &&
	                 audioFrameAtSampleRate(AudioSampleLimit + 1, 8000, 11025) == -1,
	             "frame mapping uses nearest integer timing and validates its bounded domain");
	for (const int rate : {-1, 0, 384001, std::numeric_limits<int>::max()}) {
		ok &= expect(!resampleAudioClip(exact, rate).succeeded(), "invalid target rates are rejected");
	}
	ok &= expect(!resampleAudioClip({1, 8000, {std::numeric_limits<float>::quiet_NaN()}}, 44100).succeeded(),
	             "non-finite input is rejected before the converter");
	ok &= expect(!resampleAudioClip({8, 1, QVector<float>(64, 0)}, 384000).succeeded(),
	             "oversized output is rejected before allocating it");
	ok &= expect(!resampleAudioClip({8, 384000, QVector<float>(64, 0)}, 1).succeeded(),
	             "extreme downsampling cannot exceed the bounded processing budget");
	int polls = 0;
	const auto cancelled = resampleAudioClip(channels, 96000, {[&] { return ++polls == 6; }});
	ok &= expect(cancelled.cancelled && cancelled.clip.samples.isEmpty() && !cancelled.succeeded() && polls == 6,
	             "mid-stream cancellation returns no partial output");
	ok &= expect(channels.samples[8] == float(1.5 * sine(1, 48000, 200)), "conversion leaves source samples untouched");
	// Real simultaneous workers exercise upstream global filter-cache locking.
	auto concurrent = std::async(std::launch::async, [] { return resampleAudioClip(tone(48000, 997), 32000); });
	const auto foreground = resampleAudioClip(tone(44100, 997), 32000);
	const auto background = concurrent.get();
	ok &= expect(foreground.succeeded() && background.succeeded() && residual(foreground.clip, 997, .75) < .000002 &&
	                 residual(background.clip, 997, .75) < .000002,
	             "concurrent conversions safely share filter caches");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
