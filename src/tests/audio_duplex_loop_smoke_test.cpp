#include "core/audio_duplex.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
thread_local bool measuring = false;
thread_local size_t allocations = 0;
} // namespace
void *operator new(std::size_t size)
{
	if (measuring)
		++allocations;
	if (auto *memory = std::malloc(std::max(size, size_t(1))))
		return memory;
	throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept { ::operator delete(memory); }

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
float sample(qint64 frame, int channel) { return float(.001 + .0002 * (frame % 31) + .0003 * channel); }
AudioEffect ahead(int frames)
{
	auto result = makeAudioEffect("lookahead-limiter", 1000);
	result.parameters["lookaheadMs"] = frames;
	return result;
}
AudioSession session()
{
	AudioSession result;
	result.sampleRate = 1000;
	result.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "backing";
	source.audio.clip = {2, 1000, QVector<float>(200)};
	source.audio.endFrame = 100;
	for (int frame = 0; frame < 100; ++frame) {
		source.audio.clip.samples[frame * 2] = float(.003 + frame * .0001);
		source.audio.clip.samples[frame * 2 + 1] = float(-.003 - frame * .0002);
	}
	result.sources = {source};
	auto delay = makeAudioEffect("delay", 1000);
	delay.parameters["leftMs"] = 11;
	delay.parameters["rightMs"] = 13;
	for (int i = 0; i < 3; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		track.routing.bus = i == 2;
		if (!i)
			track.regions = {{"clip", "Clip", "backing", 0, 0, 100}};
		if (i < 2)
			track.routing.outputId = "2";
		track.gainAutomation = {{0, -3}, {23, 2}, {68, -9}};
		track.panAutomation = {{0, -.5}, {23, .7}, {68, -.2}};
		auto gain = makeAudioEffect("gain", 1000);
		track.effects = {ahead(i == 2 ? 3 : 7), delay, gain};
		track.effectAutomation = {{gain.id, "gainDb", true, {{0, -6}, {23, 3}, {50, -1}, {68, -12}}}};
		result.tracks << track;
	}
	auto masterGain = makeAudioEffect("gain", 1000);
	result.masterEffects = {ahead(5), masterGain};
	result.masterEffectAutomation = {{masterGain.id, "gainDb", true, {{0, -3}, {23, 1}, {68, -8}}}};
	return result;
}
// Construct a separate linear arrangement. This does not use audioLoopFrame:
// it repeats source samples and samples authored envelopes into step points.
AudioSession expanded(const AudioSession &original, qint64 first, qint64 end)
{
	AudioSession result = original;
	constexpr int size = 800;
	const auto authored = [&](qint64 physical) {
		if (physical < end)
			return physical;
		return first + (physical - end) % (end - first);
	};
	const auto lane = [&](const QVector<AudioAutomationPoint> &points) {
		QVector<AudioAutomationPoint> samples;
		for (int frame = 0; frame < size; ++frame)
			samples << AudioAutomationPoint{frame, audioAutomationValue(points, authored(frame), 0),
			                                AudioAutomationCurve::Step};
		return samples;
	};
	result.sources[0].audio.clip.samples.resize(size * 2);
	result.sources[0].audio.endFrame = size;
	for (int frame = 0; frame < size; ++frame)
		for (int channel = 0; channel < 2; ++channel)
			result.sources[0].audio.clip.samples[frame * 2 + channel] =
			    original.sources[0].audio.clip.samples[authored(frame) * 2 + channel];
	result.tracks[0].regions[0].length = size;
	for (auto &track : result.tracks) {
		track.gainAutomation = lane(track.gainAutomation);
		track.panAutomation = lane(track.panAutomation);
		for (auto &automation : track.effectAutomation)
			automation.points = lane(automation.points);
	}
	for (auto &automation : result.masterEffectAutomation)
		automation.points = lane(automation.points);
	return result;
}
struct Sink final : AudioDuplexCaptureSink {
	qint64 first = 0, count = 0;
	std::array<float, 2048> stereo{}, mono{};
	bool push(const AudioDuplexCaptureBlock &block) noexcept override
	{
		if (block.arms != 2 || block.first != first + count || count + block.frames > 1024)
			return false;
		std::copy(block.samples[0].begin(), block.samples[0].end(), stereo.begin() + count * 2);
		std::copy(block.samples[1].begin(), block.samples[1].end(), mono.begin() + count);
		count += block.frames;
		return true;
	}
};
bool compare(int block, bool replacement, bool tiny)
{
	auto source = session();
	AudioDuplexPass pass;
	pass.punchFirst = tiny ? 0 : 23;
	pass.punchEnd = tiny ? 1 : 70;
	pass.loopPasses = tiny ? 257 : 4;
	pass.inputChannels = 3;
	pass.blockFrames = 17;
	pass.calibrationFrames = 2;
	pass.outputGain = .7;
	pass.arms = {{"0", 2, {2, 0}, true, replacement, .25}, {"1", 1, {1, 0}, true, false, .4}};
	AudioDuplexProcessor loop, linear;
	QString error;
	if (!expect(loop.prepare(source, pass, &error), error.toUtf8().constData()))
		return false;
	auto reference = pass;
	reference.punchEnd = pass.punchFirst + (pass.punchEnd - pass.punchFirst) * pass.loopPasses;
	reference.loopPasses = 1;
	if (!expect(linear.prepare(expanded(source, pass.punchFirst, pass.punchEnd), reference, &error),
	            error.toUtf8().constData()))
		return false;
	std::vector<float> input(size_t(block * 3)), actual(size_t(block * 2)), expected(size_t(block * 2));
	Sink captured, baseline;
	captured.first = baseline.first = pass.punchFirst;
	qint64 frame = 0;
	bool ok = true;
	while (loop.progress().state != AudioDuplexProgress::State::Complete && frame < 800) {
		for (int i = 0; i < block; ++i)
			for (int c = 0; c < 3; ++c)
				input[size_t(i * 3 + c)] = sample(frame + i, c);
		AudioDuplexTime time{10 + double(frame) / 1000, 10.013 + double(frame) / 1000};
		allocations = 0;
		measuring = true;
		const auto state = loop.process(input, actual, time, captured);
		measuring = false;
		ok &= expect(allocations == 0, "loop callbacks perform no C++ allocation");
		if (!expect(state != AudioDuplexProgress::State::Error,
		            audioDuplexFaultText(loop.progress().fault).toUtf8().constData()))
			return false;
		ok &= expect(linear.process(input, expected, time, baseline) == state,
		             "loop and continuous reference have the same physical state");
		for (size_t i = 0; i < actual.size(); ++i)
			if (!expect(std::abs(actual[i] - expected[i]) < 2e-6,
			            "looped audio equals an independently expanded arrangement with continuous effects/automation"))
				return false;
		frame += block;
	}
	const auto progress = loop.progress();
	ok &= expect(progress.state == AudioDuplexProgress::State::Complete &&
	                 captured.count == (pass.punchEnd - pass.punchFirst) * pass.loopPasses &&
	                 captured.count == baseline.count && captured.stereo == baseline.stereo &&
	                 captured.mono == baseline.mono,
	             "one continuous clock captures every pass without duplicated or skipped input");
	for (qint64 i = 0; i < captured.count; ++i) {
		const auto ordinal = i + pass.punchFirst - progress.inputTimelineOrigin;
		ok &= expect(captured.stereo[size_t(i * 2)] == sample(ordinal, 2) &&
		                 captured.stereo[size_t(i * 2 + 1)] == sample(ordinal, 0) &&
		                 captured.mono[size_t(i)] == sample(ordinal, 1),
		             "calibrated dry channel ordinals are exact across loop boundaries");
	}
	ok &= expect(loop.levels().input[0].frames == quint64(reference.punchEnd - progress.inputTimelineOrigin),
	             "input meters include every pass and real preroll");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	for (int block : {1, 7, 64, 256, 4096})
		for (bool replacement : {false, true})
			ok &= compare(block, replacement, false);
	ok &= compare(256, false, true);
	{
		AudioDuplexPass pass;
		pass.punchFirst = 10;
		pass.punchEnd = 20;
		pass.loopPasses = 3;
		pass.inputChannels = 3;
		pass.arms = {{"0", 2, {2, 0}}, {"1", 1, {1, 0}}};
		AudioDuplexProcessor clock;
		QString error;
		ok &= expect(clock.prepare(session(), pass, &error), "prepare loop clock fault fixture");
		std::array<float, 120> input{};
		std::array<float, 80> output{};
		Sink sink;
		sink.first = 10;
		clock.process(input, output, {10, 10}, sink);
		const auto captured = clock.progress().capturedFrames;
		clock.process(input, output, {10.05, 10.04}, sink);
		ok &= expect(captured == 15 && clock.progress().fault == AudioDuplexFault::TimingDiscontinuity &&
		                 clock.progress().capturedFrames == captured &&
		                 std::all_of(output.begin(), output.end(), [](float v) { return v == 0; }),
		             "a later-pass clock jump stops without relabeling or replaying captured input");
	}
	AudioDuplexPass pass;
	pass.punchEnd = 70;
	pass.arms = {{"0", 1, {0, 0}}};
	AudioDuplexProcessor processor;
	QString error;
	for (int count : {0, AudioDuplexLoopPassLimit + 1}) {
		pass.loopPasses = count;
		ok &= expect(audioDuplexCaptureEnd(pass) == -1 && !processor.prepare(session(), pass, &error),
		             "invalid loop counts are rejected before device work");
	}
	pass.loopPasses = 2;
	pass.punchEnd = AudioSessionFrameLimit;
	ok &= expect(audioDuplexCaptureEnd(pass) == -1 && !processor.prepare(session(), pass, &error),
	             "unwrapped loop duration cannot overflow the timeline");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
