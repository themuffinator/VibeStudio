#include "core/audio_latency.h"
#include "core/audio_lookahead.h"
#include "core/audio_session.h"
#include "core/audio_session_io.h"
#include "core/audio_transport.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool limiter()
{
	bool ok = true;
	for (int frames : {0, 1, 240, 7680}) {
		AudioLookaheadLimiter processor;
		ok &= expect(processor.prepare(frames) && processor.latencyFrames() == frames,
		             "lookahead preparation reports exact latency");
		processor.setParameters(.5, .9, .99);
		for (int i = 0; i < frames + 300; ++i) {
			const auto value = processor.tick(i < 300 ? .2 : 0, i < 300 ? -.1 : 0);
			const bool present = i >= frames && i < frames + 300;
			ok &= expect(value == (present ? std::array<double, 2>{.2, -.1} : std::array<double, 2>{}),
			             "below-ceiling signal passes unchanged with exact delay");
		}
		processor.reset();
		for (int i = 0; i < 2 * frames + 1200; ++i) {
			const double sample = std::sin(i * .2) * 8;
			if (i == 250)
				processor.setParameters(.1, .9999, .9);
			const auto output = processor.tick(sample, sample * -.25);
			const double ceiling = i < 250 ? .5 : .1;
			ok &= expect(
			    std::isfinite(output[0]) && std::abs(output[0]) <= ceiling + 1e-14 && output[1] == output[0] * -.25,
			    "linked safety gain bounds both channels and preserves stereo ratio under abrupt ceiling changes");
		}
		processor.reset();
		processor.setParameters(.5, 0, .99);
		for (int i = 0; i <= frames; ++i) {
			const auto output = processor.tick(0, 0);
			ok &=
			    expect(output == std::array<double, 2>{}, "constant-time reset hides every retained audio/peak value");
		}
	}
	AudioLookaheadLimiter look, immediate;
	ok &= look.prepare(100) && immediate.prepare(0);
	look.setParameters(.5, .9, .99);
	immediate.setParameters(.5, .9, .99);
	std::array<double, 500> ahead{}, now{};
	for (int i = 0; i < 600; ++i) {
		const double input = i == 300 ? 4 : .1;
		const auto value = look.tick(input, input)[0];
		if (i >= 100)
			ahead[size_t(i - 100)] = value;
		if (i < 500)
			now[size_t(i)] = immediate.tick(input, input)[0];
	}
	ok &= expect(ahead[250] < .02 && now[250] == .1 && std::abs(ahead[300]) <= .5,
	             "lookahead begins controlled gain reduction before the transient reaches output");
	ok &= expect(ahead[499] > ahead[301] && ahead[499] <= .1, "release recovers smoothly after the transient");
	for (int frames : {0, 1}) {
		AudioLookaheadLimiter safety;
		ok &= safety.prepare(frames);
		safety.setParameters(.5, .999, .99);
		for (int i = 0; i <= frames; ++i)
			safety.tick(i == 0 ? 4 : .1, i == 0 ? 2 : .05);
		const auto recovered = safety.tick(.1, .05);
		ok &= expect(std::abs(recovered[0] - .1 * (.125 * .99 + .01)) < 1e-14 && recovered[1] == recovered[0] * .5,
		             "release starts from the actual safety reduction even with zero/short lookahead and slow attack");
	}
	ok &= expect(!look.prepare(-1) && !look.prepare(7681) &&
	                 AudioLookaheadLimiter::memoryBytes(7681) == std::numeric_limits<quint64>::max(),
	             "unsupported storage bounds rejected");
	AudioLatencyLine line;
	ok &= line.prepare(3);
	for (int i = 0; i < 12; ++i) {
		const auto value = line.tick(i + 1, -i - 1);
		ok &= expect(value == (i < 3 ? std::array<double, 2>{} : std::array<double, 2>{double(i - 2), double(2 - i)}),
		             "route compensation line has exact integer stereo delay");
	}
	line.reset();
	ok &= expect(line.tick(1, 2) == std::array<double, 2>{}, "route reset masks retained contents");
	ok &= expect(line.prepare(0) && line.tick(.25, -.5) == std::array<double, 2>{.25, -.5},
	             "zero compensation bypasses exactly");
	return ok;
}
bool planning()
{
	AudioSession s;
	for (int i = 0; i < 4; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		track.routing.bus = i >= 2;
		track.routing.outputId = i < 2 ? "2" : i == 2 ? "3" : QString();
		s.tracks.append(track);
	}
	s.tracks[1].routing.sends = {{QString(), 0, 0, true, true}};
	s.tracks[2].routing.sends = {{QString(), 0, 0, true, true}};
	AudioRoutingPlan routing;
	bool ok = expect(prepareAudioRouting(s, &routing).isEmpty(), "latency graph fixture validates");
	AudioLatencyPlan plan;
	const std::array<int, 4> latencies{3, 7, 5, 11};
	ok &= expect(prepareAudioLatency(s, routing, latencies, 2, &plan).isEmpty(), "latency graph prepares");
	ok &= expect(plan.nodes[2].input == 7 && plan.nodes[2].output == 12 && plan.nodes[3].input == 12 &&
	                 plan.nodes[3].output == 23 && plan.masterInput == 23 && plan.total == 25,
	             "serial chains and bus merges compute the longest arrival path");
	ok &=
	    expect(plan.nodes[0].outputDelay == 4 && plan.nodes[1].outputDelay == 0 && plan.nodes[1].sendDelays[0] == 23 &&
	               plan.nodes[2].sendDelays[0] == 16 && plan.bytes == 43 * 2 * sizeof(double),
	           "parallel pre/post paths receive exact alignment storage");
	s.tracks[0].muted = s.tracks[0].solo = true;
	ok &= prepareAudioRouting(s, &routing).isEmpty();
	ok &= expect(prepareAudioLatency(s, routing, latencies, 2, &plan).isEmpty() && plan.total == 25 &&
	                 plan.bytes == 43 * 4 * sizeof(double),
	             "mute and solo preserve timing while accounting independent solo histories");
	const std::array<bool, 4> included{true, true, true, false};
	ok &= expect(prepareAudioLatency(s, routing, latencies, 2, &plan, included, 2).isEmpty() && plan.total == 12 &&
	                 plan.bytes == 4 * 4 * sizeof(double),
	             "tap graph excludes downstream and master compensation");
	ok &= expect(prepareAudioLatency(s, routing, latencies, 2, &plan, included, 2, true).isEmpty() && plan.total == 7,
	             "pre-fader tap precedes selected strip latency");
	ok &= expect(!prepareAudioLatency(s, routing, latencies, 2, &plan, included, 3).isEmpty() && plan.nodes.isEmpty(),
	             "excluded target rejects without a partial plan");
	const std::array<int, 4> over{AudioProcessingLatencyLimit, 7, 5, 11};
	ok &= expect(!prepareAudioLatency(s, routing, over, 2, &plan).isEmpty(),
	             "serial latency overflow is bounded before allocation");
	s.tracks[0].routing.outputEnabled = false;
	ok &= prepareAudioRouting(s, &routing).isEmpty();
	ok &= expect(prepareAudioLatency(s, routing, over, 2, &plan).isEmpty() && plan.total == 25,
	             "disabled output cannot extend active route latency");
	routing.order[0] = routing.order[1];
	ok &= expect(!prepareAudioLatency(s, routing, latencies, 2, &plan).isEmpty(),
	             "malformed prepared graph is rejected safely");
	s = {};
	std::array<int, 16> memory{};
	memory[0] = AudioProcessingLatencyLimit;
	for (int i = 0; i < 16; ++i) {
		AudioSessionTrack t;
		t.id = t.name = QString::number(i);
		s.tracks.append(t);
	}
	ok &= prepareAudioRouting(s, &routing).isEmpty();
	ok &= expect(!prepareAudioLatency(s, routing, memory, 0, &plan).isEmpty(),
	             "aggregate compensation memory rejected before allocating many long lines");
	return ok;
}
AudioEffect lookahead(int frames, int rate = 1000)
{
	auto effect = makeAudioEffect("lookahead-limiter", rate);
	effect.parameters["lookaheadMs"] = double(frames) * 1000 / rate;
	effect.parameters["ceilingDb"] = 0;
	return effect;
}
AudioSession session(int frames = 513)
{
	AudioSession s;
	s.sampleRate = 1000;
	s.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "source";
	source.audio.clip = {2, 1000, QVector<float>(frames * 2)};
	source.audio.endFrame = frames;
	for (int i = 0; i < frames; ++i) {
		source.audio.clip.samples[i * 2] = float(.0625 * std::cos(i * .17));
		source.audio.clip.samples[i * 2 + 1] = float(.03125 * std::sin(i * .07));
	}
	s.sources = {source};
	AudioSessionTrack track;
	track.id = track.name = "a";
	track.regions = {{"clip", "clip", "source", 0, 0, frames}};
	s.tracks = {track};
	return s;
}
bool near(const QVector<float> &a, const QVector<float> &b, double tolerance = 1e-7)
{
	return a.size() == b.size() && std::equal(a.cbegin(), a.cend(), b.cbegin(),
	                                          [&](float x, float y) { return std::abs(double(x) - y) < tolerance; });
}
bool rendering()
{
	bool ok = true;
	QString error;
	for (bool effects : {false, true}) {
		auto shared = session(8);
		if (effects)
			shared.tracks[0].effects = {lookahead(7)};
		AudioSessionRenderer prepared;
		ok &= prepared.prepare(shared, &error);
		std::array<float, 16> output{};
		std::vector<double> storage(prepared.scratchSamples(8));
		ok &= expect(!shared.tracks.isDetached() && !shared.sources.isDetached() &&
		                 prepared.renderInto(0, output, storage) == AudioSessionRenderer::BlockStatus::Ready &&
		                 !shared.tracks.isDetached() && !shared.sources.isDetached(),
		             "direct and compensated render reads do not detach shared Qt session containers");
	}
	auto s = session();
	const auto dry = renderAudioSession(s);
	s.tracks[0].effects = {lookahead(7), lookahead(11)};
	s.masterEffects = {lookahead(13)};
	AudioSessionRenderer renderer;
	ok &=
	    expect(renderer.prepare(s, &error) && renderer.processingLatencyFrames() == 31 && renderer.frameCount() == 513,
	           "serial/master latency reports without extending timeline");
	const auto rendered = renderAudioSession(s);
	ok &= expect(dry.succeeded() && rendered.succeeded() && dry.clip.samples == rendered.clip.samples,
	             "aligned output preserves first and last source samples with no tail or leading silence");
	for (int block : {1, 7, 128, 512}) {
		QVector<float> actual;
		renderer.resetProcessing();
		for (int first = 0; first < 513; first += block) {
			const auto part = renderer.renderBlock(first, std::min(block, 513 - first));
			ok &= part.succeeded();
			actual += part.clip.samples;
		}
		ok &= expect(actual == rendered.clip.samples, "priming and flush are identical across caller block sizes");
	}
	for (int first : {19, 250, 0, 400}) {
		const auto seek = renderer.renderBlock(first, 40);
		ok &= expect(seek.succeeded() && seek.clip.samples == dry.clip.samples.mid(first * 2, 80),
		             "discontinuous ranges reset every delay and align to requested first frame");
	}
	std::vector<float> cancelled(40, 1);
	std::vector<double> scratch(renderer.scratchSamples(20));
	renderer.resetProcessing();
	int calls = 0;
	AudioWorkControl control;
	control.cancelled = [&] { return ++calls == 5; };
	ok &= expect(renderer.renderInto(0, cancelled, scratch, control) == AudioSessionRenderer::BlockStatus::Cancelled &&
	                 std::all_of(cancelled.begin(), cancelled.end(), [](float value) { return value == 0; }) &&
	                 renderer.renderBlock(0, 20).clip.samples == dry.clip.samples.first(40),
	             "cancellation during priming clears output and subsequent playback starts cleanly");
	// Independent polarity-null oracle: one delayed route, one dry route, and a
	// pre-insert send must arrive at exactly the same authored sample.
	s = session();
	auto inverse = s.tracks[0];
	inverse.id = inverse.name = "inverse";
	inverse.regions[0].id = "inverse-clip";
	inverse.routing.invertLeft = inverse.routing.invertRight = true;
	s.tracks.append(inverse);
	s.tracks[0].effects = {lookahead(17)};
	auto null = renderAudioSession(s);
	ok &= expect(null.succeeded() && std::all_of(null.clip.samples.cbegin(), null.clip.samples.cend(),
	                                             [](float value) { return value == 0; }),
	             "parallel latent and polarity-inverted dry tracks null exactly");
	s.tracks[0].routing.sends = {{QString(), 0, 0, true, true}};
	s.tracks[1].routing.sends = {{QString(), 0, 0, false, true}};
	null = renderAudioSession(s);
	ok &= expect(null.succeeded() && std::all_of(null.clip.samples.cbegin(), null.clip.samples.cend(),
	                                             [](float value) { return value == 0; }),
	             "pre/post sends align with latent main outputs");
	// Near the authored limit, physical processing is allowed to flush the
	// lookahead pipeline without extending the requested or serialized range.
	s = session(20);
	s.tracks[0].regions[0].position = AudioSessionFrameLimit - 20;
	s.tracks[0].effects = {lookahead(20)};
	s.masterEffects = {lookahead(20)};
	ok &= expect(renderer.prepare(s, &error) && renderer.renderBlock(AudioSessionFrameLimit - 20, 20).clip.samples ==
	                                                s.sources[0].audio.clip.samples,
	             "maximum authored timeline frame flushes latent processors without overflow");
	s.tracks[0].effects[0].enabled = false;
	ok &= expect(renderer.prepare(s, &error) && renderer.processingLatencyFrames() == 20,
	             "bypass removes insert latency when preparing a new snapshot");
	s = session(80);
	std::fill(s.sources[0].audio.clip.samples.begin(), s.sources[0].audio.clip.samples.end(), 2.f);
	s.masterEffects = {lookahead(9)};
	s.masterEffectAutomation = {
	    {s.masterEffects[0].id, "ceilingDb", true, {{0, -1, AudioAutomationCurve::Step}, {17, -12}}}};
	const auto ceiling = renderAudioSession(s);
	ok &= ceiling.succeeded();
	for (int i = 0; i < 80 && ceiling.succeeded(); ++i)
		ok &= expect(std::abs(ceiling.clip.samples[i * 2] - std::pow(10., (i < 17 ? -1. : -12.) / 20)) < 1e-7,
		             "limiter ceiling automation changes at the authored output frame");
	return ok;
}
bool transportAndBudget()
{
	bool ok = true;
	QString error;
	auto s = session(23);
	s.tracks[0].effects = {lookahead(17)};
	s.masterEffects = {lookahead(13)};
	for (int block : {1, 7, 256}) {
		AudioTransport transport;
		ok &= transport.prepare(s, {3, 20, true}, block, &error) && transport.play();
		std::array<float, 126> output{};
		const auto rendered = transport.process(output);
		ok &= expect(rendered.frames == 63 && transport.processingLatencyFrames() == 30 && transport.loops() == 3 &&
		                 transport.position() == 15,
		             "loop and consumed sample clocks exclude priming frames");
		for (int i = 0; i < 63; ++i)
			ok &= expect(output[size_t(i) * 2] == s.sources[0].audio.clip.samples[(3 + i % 17) * 2],
			             "short loops retain latency histories with no silence or missing boundary samples");
		transport.pause();
		ok &= expect(transport.process(output).frames == 0 && transport.position() == 15, "pause retains latent clock");
		transport.play();
		std::array<float, 2> one{};
		ok &= expect(transport.process(one).frames == 1 && one[0] == s.sources[0].audio.clip.samples[30],
		             "resume retains queued compensated signal");
		ok &= transport.seek(7);
		ok &= expect(transport.process(one).frames == 1 && one[0] == s.sources[0].audio.clip.samples[14],
		             "transport seek rebuilds processing from requested position");
		transport.stop();
		transport.play();
		ok &= expect(transport.process(one).frames == 1 && one[0] == s.sources[0].audio.clip.samples[6],
		             "stop/play resets every processing delay");
	}
	// Each budget separately fits. Their sum must reject before allocations.
	s = {};
	s.sampleRate = 384000;
	std::array<int, 37> latency{};
	for (int i = 0; i < 37; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		track.routing.bus = i >= 33;
		if (i == 0 || (i >= 33 && i < 36))
			track.routing.outputId = QString::number(i == 0 ? 33 : i + 1);
		if (track.routing.bus) {
			for (int j = 0; j < 8; ++j)
				track.effects.append(lookahead(7680, s.sampleRate));
			latency[size_t(i)] = 61440;
		}
		s.tracks.append(track);
	}
	for (int j = 0; j < 8; ++j)
		s.masterEffects.append(lookahead(7680, s.sampleRate));
	AudioRoutingPlan routing;
	AudioLatencyPlan plan;
	ok &= prepareAudioRouting(s, &routing).isEmpty();
	ok &= expect(prepareAudioLatency(s, routing, latency, 61440, &plan).isEmpty() &&
	                 plan.bytes < AudioEffectMemoryLimit && !validateAudioSessionStructure(s).isEmpty(),
	             "aggregate insert and compensation state is bounded before allocating either");
	s.tracks[1].routing.outputEnabled = false;
	ok &= expect(validateAudioSessionStructure(s).isEmpty(), "removing one long parallel route returns below budget");
	s.tracks[2].solo = true;
	ok &= expect(!validateAudioSessionStructure(s).isEmpty(),
	             "independent solo compensation histories count toward budget");
	return ok;
}
bool nested()
{
	bool ok = true;
	QString error;
	auto reference = session(601);
	auto second = reference.tracks[0];
	second.id = second.name = "b";
	second.regions[0].id = "clip-b";
	second.gainDb = -6;
	second.routing.outputId = "x";
	reference.tracks[0].routing.outputId = "x";
	reference.tracks[0].routing.sends = {{"y", -9, -.2, true, true}};
	reference.tracks.append(second);
	for (const auto &id : {QString("x"), QString("y")}) {
		AudioSessionTrack bus;
		bus.id = bus.name = id;
		bus.routing.bus = true;
		if (id == "x") {
			bus.routing.outputId = "y";
			bus.routing.sends = {{QString(), -12, .1, true, true}};
		}
		bus.gainAutomation = {{0, -9}, {101, 0}, {211, -3, AudioAutomationCurve::Step}, {350, -6}};
		bus.panAutomation = {{0, -.3}, {170, .4}, {500, 0}};
		bus.effects = {makeAudioEffect("tremolo", 1000), makeAudioEffect("gain", 1000)};
		bus.effectAutomation = {{bus.effects.last().id, "gainDb", true, {{0, -3}, {301, 0}, {600, -6}}}};
		reference.tracks.append(bus);
	}
	reference.masterEffects = {makeAudioEffect("tremolo", 1000)};
	auto latent = reference;
	latent.tracks[0].effects = {lookahead(3)};
	latent.tracks[1].effects = {lookahead(7)};
	latent.tracks[2].effects.prepend(lookahead(5));
	latent.tracks[3].effects.prepend(lookahead(11));
	latent.masterEffects.prepend(lookahead(2));
	AudioSessionRenderer a, b;
	ok &= expect(a.prepare(reference, &error) && b.prepare(latent, &error) && b.processingLatencyFrames() == 25,
	             "nested bus fixture and master latency prepare");
	const auto dry = a.renderBlock(0, 601), delayed = b.renderBlock(0, 601);
	ok &= expect(dry.succeeded() && delayed.succeeded() && near(dry.clip.samples, delayed.clip.samples),
	             "bus faders, pan and effect automation plus LFO phases stay on authored time through nested delays");
	for (const auto &id : {QString("a"), QString("x"), QString("y")})
		for (auto tap : {AudioSessionRenderTarget::Tap::PreFader, AudioSessionRenderTarget::Tap::PostFader}) {
			ok &= a.prepare(reference, &error, {}, {id, tap}) && b.prepare(latent, &error, {}, {id, tap});
			const auto expected = a.renderBlock(17, 500), actual = b.renderBlock(17, 500);
			ok &= expect(expected.succeeded() && actual.succeeded() && near(expected.clip.samples, actual.clip.samples),
			             "pre/post strip taps prune downstream latency and remain aligned at arbitrary ranges");
		}
	// Independent audible and residual processors must advance through the
	// same compensation paths when solo crosses a nonlinear bus.
	reference.tracks[0].solo = latent.tracks[0].solo = true;
	reference.tracks[3].solo = latent.tracks[3].solo = true;
	for (auto *s : {&reference, &latent})
		s->tracks[2].effects.append(makeAudioEffect("saturation", 1000));
	ok &= a.prepare(reference, &error) && b.prepare(latent, &error);
	ok &= expect(near(a.renderBlock(0, 601).clip.samples, b.renderBlock(0, 601).clip.samples),
	             "nonlinear solo residual and audible domains retain compensated alignment");
	auto driven = latent;
	for (auto &sample : driven.sources[0].audio.clip.samples)
		sample *= 100;
	driven.tracks[2].effects[0].parameters["ceilingDb"] = -24;
	auto combined = driven;
	for (auto &track : combined.tracks)
		track.solo = false;
	ok &= a.prepare(combined, &error, {}, {"x"}) && b.prepare(driven, &error, {}, {"x"});
	const auto fullBus = a.renderBlock(0, 601), residualBus = b.renderBlock(0, 601);
	ok &= expect(fullBus.succeeded() && residualBus.succeeded() && near(fullBus.clip.samples, residualBus.clip.samples),
	             "active lookahead gain reduction recombines independent nonlinear solo histories into the full bus");
	// Native storage preserves structural values and lanes alongside tempo maps.
	const auto encoded = encodeAudioSession(latent, &error);
	AudioSession decoded;
	ok &= expect(!encoded.isEmpty() && decodeAudioSession(encoded, &decoded, &error) && b.prepare(decoded, &error) &&
	                 b.processingLatencyFrames() == 25,
	             "native session round trip preserves latent insert graph");
	const auto report = audioSessionLatencyReport(latent);
	ok &= expect(report["frames"].toInt() == 25 && report["masterInputFrames"].toInt() == 23 &&
	                 !audioSessionSummary(latent).contains("processingLatency"),
	             "latency diagnostics remain separate from native schema");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const bool ok = limiter() && planning() && rendering() && nested() && transportAndBudget();
	std::cout << (ok ? "Audio latency and lookahead rendering passed\n" : "Audio latency failed\n");
	return ok ? 0 : 1;
}
