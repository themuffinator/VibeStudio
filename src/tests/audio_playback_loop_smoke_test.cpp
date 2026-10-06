#include "core/audio_transport.h"
#include "tests/audio_loop_test_fixture.h"
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
bool near(std::span<const float> actual, std::span<const float> expected)
{
	return actual.size() == expected.size() && std::equal(actual.begin(), actual.end(), expected.begin(),
	                                                      [](float a, float b) { return std::abs(a - b) < 2e-6; });
}
bool process(AudioTransport &transport, std::span<float> output)
{
	allocations = 0;
	measuring = true;
	const auto result = transport.process(output);
	measuring = false;
	return expect(allocations == 0 && result.status == AudioSessionRenderer::BlockStatus::Ready &&
	                  result.frames == qint64(output.size() / 2),
	              "loop rendering, including initial compensation, performs no C++ allocation");
}
bool compare(int internal, bool solo, bool tiny)
{
	constexpr int frames = 1021;
	const qint64 first = tiny ? 0 : 23, end = tiny ? 1 : 70;
	const auto session = test::playbackLoopFixture(solo);
	const auto expanded = test::expandPlaybackLoop(session, first, end, 1200);
	AudioSessionRenderer reference;
	AudioTransport transport;
	QString error;
	if (!expect(reference.prepare(expanded, &error, {}, {}, {true, first, 1200}) &&
	                transport.prepare(session, {first, end, true}, internal, &error, {}, true) && transport.play(),
	            error.toUtf8().constData()))
		return false;
	std::vector<float> expected(frames * 2), actual(frames * 2);
	std::vector<double> scratch(reference.scratchSamples(frames));
	if (!expect(reference.renderInto(first, expected, scratch) == AudioSessionRenderer::BlockStatus::Ready,
	            "independently expanded finite reference renders"))
		return false;
	bool ok = true;
	constexpr std::array<int, 5> callbacks{1, 3, 127, 509, 17};
	for (int at = 0, step = 0; at < frames; ++step) {
		const int count = std::min(frames - at, callbacks[size_t(step) % callbacks.size()]);
		ok &= process(transport, std::span(actual).subspan(size_t(at * 2), size_t(count * 2)));
		at += count;
	}
	ok &= expect(near(actual, expected),
	             "looped playback matches linear media with continuous delay/reverb/modulation, routes and automation");
	ok &= expect(transport.position() == first + frames % (end - first) &&
	                 transport.loops() == quint64(frames / (end - first)) && transport.framesRendered() == frames,
	             "wrapped cursor and cycle counts exclude latency priming and remain independent of DSP time");
	bool evolved = false;
	for (int frame = 0; frame < end - first; ++frame)
		evolved |= std::abs(actual[size_t(frame * 2)] - actual[size_t((frame + end - first) * 2)]) > 1e-8;
	ok &=
	    expect(evolved, "successive cycles retain audible processor history rather than replaying a cached first pass");
	const auto a = transport.meters(), b = reference.meters();
	for (int i = 0; i < a.count; ++i) {
		for (bool post : {false, true}) {
			const auto &x = post ? a.strips[size_t(i)].post : a.strips[size_t(i)].pre;
			const auto &y = post ? b.strips[size_t(i)].post : b.strips[size_t(i)].pre;
			ok &= expect(x.frames == y.frames && x.lastFrame == y.lastFrame &&
			                 x.samplesAboveFullScale == y.samplesAboveFullScale,
			             "every strip meter counts unwrapped signal time continuously across repeats");
			for (int c = 0; c < 2; ++c)
				ok &= expect(std::abs(x.maximum[size_t(c)] - y.maximum[size_t(c)]) < 2e-6 &&
				                 std::abs(x.rms[size_t(c)] - y.rms[size_t(c)]) < 2e-6 &&
				                 std::abs(x.integratedRms[size_t(c)] - y.integratedRms[size_t(c)]) < 2e-6,
				             "loop meter envelopes and accumulated energy agree with the linear reference");
		}
	}
	return ok;
}
bool lifecycle()
{
	const auto session = test::playbackLoopFixture();
	QString error;
	AudioTransport actual, reference;
	if (!actual.prepare(session, {23, 70, true}, 17, &error, {}, true) ||
	    !reference.prepare(session, {23, 70, true}, 64, &error) || !actual.play() || !reference.play())
		return false;
	std::array<float, 238> a{}, b{};
	bool ok = process(actual, a) && process(reference, b) && near(a, b);
	const auto position = actual.position();
	const auto meters = actual.meters().strips[4].post.frames;
	actual.pause();
	ok &= expect(actual.process(a).frames == 0 && actual.position() == position &&
	                 actual.meters().strips[4].post.frames == meters &&
	                 std::all_of(a.begin(), a.end(), [](float v) { return v == 0; }),
	             "pause freezes both clocks and meter history while clearing caller output");
	actual.play();
	actual.setLoop(true); // An unchanged policy is not a discontinuity.
	actual.resetMetering();
	ok &= process(actual, a) && process(reference, b);
	ok &= expect(near(a, b) && actual.meters().strips[4].post.frames == 119,
	             "resume, unchanged loop policy and meter reset preserve delay and modulation histories");
	actual.stop();
	reference.stop();
	actual.play();
	reference.play();
	ok &= process(actual, a) && process(reference, b) && near(a, b);
	ok &= expect(actual.framesRendered() == 119, "Stop restarts the physical clock and processing exactly once");
	ok &= actual.seek(66) && reference.seek(66);
	ok &= process(actual, a) && process(reference, b) && near(a, b);
	const auto finiteFirst = actual.position();
	allocations = 0;
	measuring = true;
	actual.setLoop(false);
	measuring = false;
	ok &= expect(allocations == 0, "changing loop policy reuses prepared routing/effect storage");
	AudioTransport finite;
	ok &= finite.prepare(session, {23, 70, false}, 11, &error) && finite.seek(finiteFirst) && finite.play();
	const auto finiteA = actual.process(a), finiteB = finite.process(b);
	ok &= expect(finiteA.frames == 70 - finiteFirst && finiteA.frames == finiteB.frames && near(a, b) &&
	                 actual.state() == AudioTransport::State::Ended,
	             "disabling loop starts fresh at the displayed cursor and preserves finite compensation and endpoint");
	actual.setLoop(true);
	actual.play();
	reference.stop();
	reference.play();
	ok &= process(actual, a) && process(reference, b) && near(a, b);
	const auto before = actual.position();
	const auto rendered = actual.framesRendered(), loops = actual.loops();
	int polls = 0;
	const auto cancelled = actual.process(a, {[&] { return ++polls > 20; }});
	ok &= expect(cancelled.status == AudioSessionRenderer::BlockStatus::Cancelled && cancelled.frames == 0 &&
	                 actual.position() == before && actual.framesRendered() == rendered && actual.loops() == loops &&
	                 actual.state() == AudioTransport::State::Error && actual.meters().strips[4].post.frames == 0 &&
	                 std::all_of(a.begin(), a.end(), [](float v) { return v == 0; }),
	             "cancellation across loop work clears partial output/meters and cannot advance transport counters");
	return ok;
}
bool boundaries()
{
	AudioSession session;
	session.sampleRate = 1000;
	session.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "edge-source";
	source.audio.clip = {2, 1000, {.1f, -.1f, .2f, -.2f, .3f, -.3f, .4f, -.4f}};
	source.audio.endFrame = 4;
	session.sources = {source};
	AudioSessionTrack track;
	track.id = track.name = "edge-track";
	track.regions = {{"edge-clip", "Edge", source.id, AudioSessionFrameLimit - 4, 0, 4}};
	track.effects = {test::loopLookahead(7)};
	session.tracks = {track};
	session.masterEffects = {test::loopLookahead(5)};
	QString error;
	AudioTransport transport;
	if (!expect(transport.prepare(session, {AudioSessionFrameLimit - 4, AudioSessionFrameLimit, true}, 17, &error, {},
	                              true) &&
	                transport.play(),
	            error.toUtf8().constData()))
		return false;
	std::array<float, 206> samples{};
	bool ok = process(transport, samples);
	bool exact = true;
	for (size_t i = 0; i < samples.size(); ++i)
		exact &= samples[i] == source.audio.clip.samples[qsizetype(i % 8)];
	ok &= expect(exact, "looped DSP and source positions continue beyond the authored timeline limit");
	ok &= expect(transport.meters().strips[1].post.frames == 103 &&
	                 transport.meters().strips[1].post.lastFrame == AudioSessionFrameLimit + 98,
	             "meter timestamps retain the unwrapped clock beyond the authored limit");
	AudioSessionRenderer renderer;
	ok &= renderer.prepare(session, &error, {}, {}, {}, AudioSessionRenderClock::Compensated,
	                       {true, AudioSessionFrameLimit - 4, AudioSessionFrameLimit});
	std::vector<double> scratch(renderer.scratchSamples(103));
	ok &= expect(renderer.renderInto(AudioLoopClockLimit - 103, samples, scratch) ==
	                 AudioSessionRenderer::BlockStatus::Ready,
	             "the independent physical clock retains arithmetic headroom for latency");
	ok &= expect(renderer.renderInto(AudioLoopClockLimit - 102, samples, scratch) ==
	                     AudioSessionRenderer::BlockStatus::InvalidRange &&
	                 std::all_of(samples.begin(), samples.end(), [](float v) { return v == 0; }),
	             "physical clock exhaustion fails atomically instead of overflowing");
	ok &= expect(!renderer.prepare(session, &error, {}, {}, {}, AudioSessionRenderClock::Compensated, {false, 4, 3}),
	             "invalid dormant loop bounds cannot later be enabled");
	return ok;
}
bool delayOracle()
{
	AudioSession session;
	session.sampleRate = 1000;
	session.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "pulse";
	source.audio.clip = {2, 1000, {.25f, -.25f, 0, 0, 0, 0, 0, 0}};
	source.audio.endFrame = 4;
	session.sources = {source};
	AudioSessionTrack track;
	track.id = track.name = "echo";
	track.regions = {{"pulse-clip", "Pulse", source.id, 3, 0, 4}};
	auto effect = makeAudioEffect("delay", 1000);
	effect.parameters["leftMs"] = 6;
	effect.parameters["rightMs"] = 9;
	effect.parameters["feedback"] = .5;
	effect.parameters["mix"] = 1;
	track.effects = {effect};
	session.tracks = {track};
	QString error;
	AudioTransport transport;
	if (!transport.prepare(session, {3, 7, true}, 7, &error) || !transport.play())
		return false;
	std::array<float, 258> actual{}, expected{};
	for (int frame = 0; frame < 129; ++frame)
		for (int channel = 0; channel < 2; ++channel) {
			const int previous = frame - (channel ? 9 : 6);
			if (previous >= 0)
				expected[size_t(frame * 2 + channel)] =
				    (previous % 4 == 0 ? (channel ? -.25f : .25f) : 0) + .5f * expected[size_t(previous * 2 + channel)];
		}
	return process(transport, actual) &&
	       expect(actual == expected,
	              "feedback echoes cross short loop boundaries at independently calculated samples");
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	for (int internal : {1, 7, 64, 256, 4096})
		for (bool solo : {false, true})
			for (bool tiny : {false, true})
				ok &= compare(internal, solo, tiny);
	ok &= lifecycle();
	ok &= boundaries();
	ok &= delayOracle();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
