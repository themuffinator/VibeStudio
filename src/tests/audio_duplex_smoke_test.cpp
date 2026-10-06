#include "core/audio_duplex.h"
#include <QCoreApplication>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

// Instrument C++ allocations in the actual callback path. Qt containers are
// additionally checked for detachment; source review covers their C allocator.
namespace
{
thread_local bool countAllocations = false;
thread_local size_t allocationCount = 0;
} // namespace
void *operator new(std::size_t size)
{
	if (countAllocations)
		++allocationCount;
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
using State = AudioDuplexProgress::State;
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool silent(std::span<const float> samples)
{
	return std::all_of(samples.begin(), samples.end(), [](float value) { return value == 0; });
}
AudioEffect lookahead(int frames)
{
	auto effect = makeAudioEffect("lookahead-limiter", 1000);
	effect.parameters["lookaheadMs"] = frames;
	effect.parameters["ceilingDb"] = 0;
	return effect;
}
AudioSession fixture()
{
	AudioSession s;
	s.sampleRate = 1000;
	s.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "source";
	source.audio.clip = {2, 1000, QVector<float>(1000, .125f)};
	source.audio.endFrame = 500;
	s.sources = {source};
	for (int i = 0; i < 2; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		if (!i)
			track.regions = {{"backing", "Backing", "source", 0, 0, 500}};
		track.effects = {lookahead(i ? 3 : 7)};
		s.tracks.append(track);
	}
	s.masterEffects = {lookahead(5)};
	return s;
}
AudioDuplexPass pass()
{
	AudioDuplexPass p;
	p.playbackFirst = 5;
	p.punchFirst = 37;
	p.punchEnd = 94;
	p.calibrationFrames = 3;
	p.inputChannels = 4;
	p.blockFrames = 17;
	p.arms = {{"0", 2, {3, 1}}, {"1", 1, {2, 0}}};
	return p;
}
class Sink final : public AudioDuplexCaptureSink {
  public:
	Sink()
	{
		for (auto &arm : samples)
			arm.resize(32768);
	}
	std::array<std::vector<float>, AudioDuplexArmLimit> samples;
	std::array<int, AudioDuplexArmLimit> channels{2, 1};
	qint64 first = -1, end = -1, frames = 0;
	int calls = 0, failAfter = -1;
	bool valid = true;
	bool push(const AudioDuplexCaptureBlock &block) noexcept override
	{
		if (failAfter >= 0 && calls == failAfter)
			return false;
		if (!calls)
			first = end = block.first;
		valid &= block.first == end && block.frames > 0 && block.frames <= 4096;
		for (int arm = 0; arm < block.arms; ++arm) {
			valid &= block.samples[size_t(arm)].size() == size_t(block.frames * channels[size_t(arm)]);
			if ((frames + block.frames) * channels[size_t(arm)] > qint64(samples[size_t(arm)].size()))
				return false;
			std::copy(block.samples[size_t(arm)].begin(), block.samples[size_t(arm)].end(),
			          samples[size_t(arm)].begin() + frames * channels[size_t(arm)]);
		}
		end += block.frames;
		frames += block.frames;
		++calls;
		return true;
	}
};
void fill(std::span<float> input, qint64 first, int channels)
{
	for (size_t i = 0; i < input.size(); ++i)
		input[i] = float((first + qint64(i / size_t(channels))) * 10 + qint64(i % size_t(channels)));
}
AudioDuplexTime timestamp(qint64 frame, double roundTrip = .017)
{
	return {10.0 + frame / 1000., 10.0 + roundTrip + frame / 1000.};
}
bool alignment()
{
	bool ok = true;
	for (int block : {1, 3, 17, 64, 4096}) {
		const auto s = fixture();
		const auto p = pass();
		QString error;
		AudioDuplexProcessor processor;
		if (!expect(processor.prepare(s, p, &error), error.toUtf8().constData()))
			return false;
		Sink sink;
		std::vector<float> input(size_t(block * p.inputChannels)), output(size_t(block * 2));
		AudioDuplexTime priming;
		priming.priming = true;
		ok &= expect(processor.process({}, output, priming, sink) == State::Ready && silent(output) && sink.calls == 0,
		             "priming produces silence without moving the session or capturing fabricated input");
		qint64 at = 0;
		allocationCount = 0;
		for (int callbacks = 0; callbacks < 1000 && processor.progress().state != State::Complete; ++callbacks) {
			fill(input, at, p.inputChannels);
			const auto time = timestamp(at);
			countAllocations = true;
			const auto state = processor.process(input, output, time, sink);
			countAllocations = false;
			if (!expect(state == State::Running || state == State::Complete,
			            "a continuous duplex pass runs to completion"))
				return false;
			at += block;
		}
		const auto progress = processor.progress();
		const qint64 origin = p.playbackFirst - 17 - 12 - p.calibrationFrames;
		ok &= expect(progress.state == State::Complete && progress.roundTripFrames == 17 &&
		                 progress.processingLatencyFrames == 12 && progress.inputTimelineOrigin == origin &&
		                 progress.capturedFrames == p.punchEnd - p.punchFirst && sink.first == p.punchFirst &&
		                 sink.end == p.punchEnd && sink.valid && allocationCount == 0,
		             "one integer clock applies driver delay, graph delay and calibration exactly once");
		for (qint64 frame = 0; frame < sink.frames; ++frame) {
			const auto ordinal = p.punchFirst - origin + frame;
			ok &= expect(sink.samples[0][size_t(frame * 2)] == float(ordinal * 10 + 3) &&
			                 sink.samples[0][size_t(frame * 2 + 1)] == float(ordinal * 10 + 1) &&
			                 sink.samples[1][size_t(frame)] == float(ordinal * 10 + 2),
			             "dry stereo ordering and mono selection preserve every punch sample without clipping");
		}
		ok &= expect(!s.sources.isDetached() && !s.tracks.isDetached() && !p.arms.isDetached(),
		             "callback reads do not detach shared model or arm storage");
		output.assign(output.size(), 1);
		ok &= expect(processor.process(input, output, timestamp(at), sink) == State::Complete && silent(output),
		             "completed pass cannot capture or replay on a later callback");
	}
	return ok;
}
bool monitoring()
{
	auto s = fixture();
	auto p = pass();
	p.playbackFirst = 0;
	p.punchFirst = 20;
	p.punchEnd = 40;
	p.calibrationFrames = 0;
	p.blockFrames = 7;
	p.arms[0].monitor = p.arms[0].replacePlayback = true;
	p.arms[0].monitorGain = .5;
	p.outputGain = .5;
	QString error;
	AudioDuplexProcessor processor;
	bool ok = processor.prepare(s, p, &error);
	Sink sink;
	std::array<float, 400> input{};
	input.fill(.25f);
	std::array<float, 200> output{};
	ok &= expect(processor.process(input, output, timestamp(0, .01), sink) == State::Complete,
	             "one large callback splits punch and monitoring drain boundaries");
	// Graph latency 12, round trip 10. Last captured sample arrives at callback
	// frame 61; monitoring then drains through frame 73. Backing ends at 40.
	for (int physical = 0; physical < 100; ++physical) {
		const int source = physical - 12;
		const double backing = source >= 0 && source < 20 ? .125 : 0;
		const double monitor = source >= 0 && source < 62 ? .125 : 0;
		const auto expected = float((backing + monitor) * .5);
		ok &= expect(output[size_t(physical * 2)] == expected && output[size_t(physical * 2 + 1)] == expected,
		             "monitor gain, tape-style punch, audible latency and final drain share the session graph");
	}
	ok &= expect(sink.frames == 20 && sink.samples[0][0] == .25f && sink.samples[0][38] == .25f,
	             "recorded input stays dry despite monitoring and audition gain");
	// Software monitoring through a nonlinear master must react to the sum.
	s.tracks[0].effects.clear();
	s.tracks[1].effects.clear();
	s.masterEffects = {makeAudioEffect("saturation", 1000)};
	p.arms[0].replacePlayback = false;
	p.outputGain = 1;
	ok &= processor.prepare(s, p, &error);
	Sink nonlinearSink;
	ok &= processor.process(input, output, timestamp(0, .01), nonlinearSink) == State::Complete;
	auto combined = s;
	std::fill(combined.sources[0].audio.clip.samples.begin(), combined.sources[0].audio.clip.samples.end(), .25f);
	const auto reference = renderAudioSession(combined, 0, 20);
	ok &= expect(reference.succeeded() &&
	                 std::equal(reference.clip.samples.cbegin(), reference.clip.samples.cend(), output.begin()),
	             "backing and monitored input enter nonlinear master processing together");
	return ok;
}
bool failure()
{
	const auto s = fixture();
	const auto p = pass();
	QString error;
	bool ok = true;
	for (int issue = 0; issue < 10; ++issue) {
		AudioDuplexProcessor processor;
		ok &= processor.prepare(s, p, &error);
		Sink sink;
		std::array<float, 320> input{};
		std::array<float, 160> output{};
		ok &= processor.process(input, output, timestamp(0), sink) == State::Running;
		const auto retained = sink.frames;
		ok &= expect(retained > 0, "fault fixture already holds an acknowledged punch prefix");
		auto time = timestamp(80);
		AudioDuplexFault expected = AudioDuplexFault::None;
		switch (issue) {
		case 0:
			time.inputAdc += .1;
			expected = AudioDuplexFault::TimingDiscontinuity;
			break;
		case 1:
			time.outputDac -= .1;
			expected = AudioDuplexFault::TimingDiscontinuity;
			break;
		case 2:
			time.inputAdc = std::numeric_limits<double>::quiet_NaN();
			expected = AudioDuplexFault::TimingUnavailable;
			break;
		case 3:
			time.inputOverflow = true;
			expected = AudioDuplexFault::InputOverflow;
			break;
		case 4:
			time.inputUnderflow = true;
			expected = AudioDuplexFault::InputUnderflow;
			break;
		case 5:
			time.outputOverflow = true;
			expected = AudioDuplexFault::OutputOverflow;
			break;
		case 6:
			time.outputUnderflow = true;
			expected = AudioDuplexFault::OutputUnderflow;
			break;
		case 7:
			input[0] = std::numeric_limits<float>::infinity();
			expected = AudioDuplexFault::InputNotFinite;
			break;
		case 8:
			sink.failAfter = sink.calls;
			expected = AudioDuplexFault::CaptureQueueFull;
			break;
		case 9:
			time.priming = true;
			expected = AudioDuplexFault::TimingDiscontinuity;
			break;
		}
		allocationCount = 0;
		countAllocations = true;
		const auto state = processor.process(input, output, time, sink);
		countAllocations = false;
		ok &= expect(
		    state == State::Error && processor.progress().fault == expected && silent(output) &&
		        sink.frames == retained && processor.progress().capturedFrames == retained && allocationCount == 0,
		    "fault returns silence, retains the accepted prefix and does not allocate or silently realign input");
		ok &= expect(!audioDuplexFaultText(expected).isEmpty(),
		             "callback fault has a translated presentation outside the callback");
	}
	AudioDuplexProcessor processor;
	ok &= processor.prepare(s, p, &error);
	Sink sink;
	std::array<float, 320> input{};
	std::array<float, 160> output{};
	ok &= processor.process(input, output, timestamp(0), sink) == State::Running;
	const auto frames = sink.frames;
	processor.stop();
	ok &= expect(processor.process(input, output, timestamp(80), sink) == State::Stopped && silent(output) &&
	                 sink.frames == frames,
	             "explicit stop retains the prefix without capturing or draining more data");
	return ok;
}
bool validation()
{
	auto s = fixture();
	const auto original = pass();
	QString error;
	bool ok = true;
	for (int issue = 0; issue < 20; ++issue) {
		auto p = original;
		switch (issue) {
		case 0:
			p.playbackFirst = -1;
			break;
		case 1:
			p.punchFirst = p.playbackFirst - 1;
			break;
		case 2:
			p.punchEnd = p.punchFirst;
			break;
		case 3:
			p.punchEnd = AudioSessionFrameLimit + 1;
			break;
		case 4:
			p.inputChannels = 0;
			break;
		case 5:
			p.inputChannels = 33;
			break;
		case 6:
			p.blockFrames = 0;
			break;
		case 7:
			p.blockFrames = 4097;
			break;
		case 8:
			p.clockToleranceFrames = -1;
			break;
		case 9:
			p.clockToleranceFrames = 1001;
			break;
		case 10:
			p.arms.clear();
			break;
		case 11:
			p.arms[1].trackId = p.arms[0].trackId;
			break;
		case 12:
			p.arms[0].trackId = "missing";
			break;
		case 13:
			p.arms[0].channelMap = {1, 1};
			break;
		case 14:
			p.arms[0].channelMap[0] = 4;
			break;
		case 15:
			p.outputGain = std::numeric_limits<double>::quiet_NaN();
			break;
		case 16:
			p.arms[0].monitorGain = 2;
			break;
		case 17:
			p.arms[0].replacePlayback = true;
			break;
		case 18:
			p.calibrationFrames = -10001;
			break;
		case 19:
			p.calibrationFrames = 10001;
			break;
		}
		AudioDuplexProcessor processor;
		ok &= expect(!processor.prepare(s, p, &error) && !error.isEmpty() && processor.progress().state == State::Empty,
		             "invalid pass cannot leave a prepared stream");
	}
	AudioDuplexProcessor processor;
	ok &= processor.prepare(s, original, &error);
	Sink sink;
	std::array<float, 320> input{};
	std::array<float, 160> output{};
	ok &= expect(processor.process(std::span(input).first(319), output, timestamp(0), sink) == State::Error &&
	                 processor.progress().fault == AudioDuplexFault::InvalidBlock && silent(output),
	             "partial device frames are rejected");
	ok &= processor.prepare(s, original, &error);
	ok &= expect(processor.process(input, std::span(input).first(160), timestamp(0), sink) == State::Error &&
	                 processor.progress().fault == AudioDuplexFault::InvalidBlock,
	             "input/output buffer aliasing cannot silently erase capture");
	for (auto time : {AudioDuplexTime{}, AudioDuplexTime{20, 10}, AudioDuplexTime{10, 21}, AudioDuplexTime{1e16, 1e16},
	                  AudioDuplexTime{std::numeric_limits<double>::infinity(), 1}}) {
		ok &= processor.prepare(s, original, &error);
		ok &= expect(processor.process(input, output, time, sink) == State::Error &&
		                 processor.progress().fault == AudioDuplexFault::TimingUnavailable,
		             "unsupported initial timestamps cannot invent a capture origin");
	}
	auto p = original;
	p.calibrationFrames = -100;
	ok &= processor.prepare(s, p, &error);
	ok &= expect(processor.process(input, output, timestamp(0), sink) == State::Error &&
	                 processor.progress().fault == AudioDuplexFault::InsufficientPreroll,
	             "calibration cannot silently omit the beginning of a punch");
	p = original;
	p.clockToleranceFrames = 2;
	ok &= processor.prepare(s, p, &error);
	Sink tolerant;
	ok &= processor.process(input, output, timestamp(0), tolerant) == State::Running;
	auto time = timestamp(80);
	time.inputAdc += .002;
	ok &= expect(processor.process(input, output, time, tolerant) == State::Complete &&
	                 processor.progress().maximumClockDeviationFrames == 2 &&
	                 tolerant.frames == p.punchEnd - p.punchFirst,
	             "bounded timestamp jitter is reported without dropping or duplicating sample frames");
	return ok;
}
bool limits()
{
	AudioSession session;
	session.sampleRate = 1000;
	AudioDuplexPass p;
	p.inputChannels = 32;
	p.blockFrames = 4096;
	p.punchEnd = 8192;
	for (int arm = 0; arm < AudioDuplexArmLimit; ++arm) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(arm);
		session.tracks.append(track);
		p.arms.append({track.id, 2, {arm * 4 + 3, arm * 4 + 1}, true, false, .125});
	}
	AudioDuplexProcessor processor;
	QString error;
	bool ok = processor.prepare(session, p, &error);
	Sink sink;
	sink.channels.fill(2);
	std::vector<float> input(65536 * 32, .25f), output(65536 * 2);
	allocationCount = 0;
	countAllocations = true;
	const auto state = processor.process(input, output, timestamp(0, .01), sink);
	countAllocations = false;
	ok &= expect(state == State::Complete && sink.valid && sink.frames == 8192 && allocationCount == 0,
	             "maximum callback/channel/arm counts use bounded prepared storage");
	ok &= expect(output[0] == .25f && output[8192 * 2] == .25f && output[8202 * 2] == 0,
	             "eight monitoring inputs sum once and unused callback output is silent");
	// At the authored endpoint, capture may finish later than output. Without
	// monitoring it can continue safely; monitoring needs explicit headroom.
	p.arms.resize(1);
	p.arms[0].monitor = false;
	p.playbackFirst = AudioSessionFrameLimit - 20;
	p.punchFirst = p.playbackFirst;
	p.punchEnd = AudioSessionFrameLimit;
	ok &= processor.prepare(session, p, &error);
	Sink edge;
	ok &= expect(processor.process(input, output, timestamp(0, .01), edge) == State::Complete && edge.frames == 20,
	             "capture completes at the timeline edge while delayed device input arrives");
	p.arms[0].monitor = true;
	ok &= processor.prepare(session, p, &error);
	ok &= expect(processor.process(input, output, timestamp(0, .01), edge) == State::Error &&
	                 processor.progress().fault == AudioDuplexFault::MonitorTimelineLimit && silent(output),
	             "monitoring cannot silently overflow its authored timeline");
	p.playbackFirst = p.punchFirst = 0;
	p.punchEnd = 100;
	p.calibrationFrames = -5;
	ok &= processor.prepare(session, p, &error);
	Sink calibrated;
	ok &= expect(processor.process(input, output, timestamp(0, .01), calibrated) == State::Complete &&
	                 processor.progress().inputTimelineOrigin == -5 && calibrated.frames == 100,
	             "negative measured calibration offsets reduce the automatic correction without changing punch length");
	session.masterGainDb = 24;
	p.arms[0].monitorGain = 1;
	std::fill(input.begin(), input.end(), std::numeric_limits<float>::max());
	ok &= processor.prepare(session, p, &error);
	Sink overflow;
	ok &=
	    expect(processor.process(input, output, timestamp(0, .01), overflow) == State::Error &&
	               processor.progress().fault == AudioDuplexFault::RenderFailure && silent(output) && !overflow.frames,
	           "an overflowing mix fails silently before publishing the affected capture block");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = alignment();
	ok &= monitoring();
	ok &= failure();
	ok &= validation();
	ok &= limits();
	return ok ? 0 : 1;
}
