#include "core/audio_session.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace vibestudio;
namespace
{
using Status = AudioSessionRenderer::BlockStatus;
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool near(std::span<const float> a, std::span<const float> b, double epsilon = 1e-6)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
	                                          [&](float x, float y) { return std::abs(double(x) - y) <= epsilon; });
}
bool silent(std::span<const float> values)
{
	return std::all_of(values.begin(), values.end(), [](float x) { return x == 0; });
}
AudioEffect lookahead(int frames)
{
	auto effect = makeAudioEffect("lookahead-limiter", 1000);
	effect.parameters["lookaheadMs"] = frames;
	effect.parameters["ceilingDb"] = 0;
	return effect;
}
AudioSession fixture(int channels = 2, int frames = 157)
{
	AudioSession s;
	s.sampleRate = 1000;
	s.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "media";
	source.audio.clip = {channels, 1000, QVector<float>(frames * channels)};
	source.audio.endFrame = frames;
	for (int i = 0; i < frames; ++i)
		for (int c = 0; c < channels; ++c)
			source.audio.clip.samples[i * channels + c] = float(.0625 * std::cos(i * .17 + c * .9));
	s.sources = {source};
	AudioSessionTrack track;
	track.id = track.name = "input";
	track.regions = {{"clip", "Clip", "media", 0, 0, frames}};
	s.tracks = {track};
	return s;
}
// Independent input storage, with caller-defined callback boundaries. The
// reference uses ordinary immutable media through the compensated renderer.
bool compare(AudioSession reference, bool replace, int block)
{
	const auto media = reference.sources[0].audio.clip;
	const int frames = int(media.frameCount());
	const auto offline = renderAudioSession(reference);
	if (!expect(offline.succeeded(), offline.error.toUtf8().constData()))
		return false;
	AudioSession live = reference;
	if (!replace)
		live.tracks[0].regions.clear();
	AudioSessionRenderer renderer;
	QString error;
	if (!expect(renderer.prepare(live, &error, {}, {}, {true, 0, frames}, AudioSessionRenderClock::Live),
	            error.toUtf8().constData()))
		return false;
	const int delay = renderer.processingLatencyFrames();
	std::vector<float> result(size_t(frames + delay) * 2);
	std::vector<double> scratch(renderer.scratchSamples(block));
	const auto samples = std::span<const float>(media.samples.constData(), size_t(media.samples.size()));
	bool ok = true;
	for (int at = 0; at < frames + delay;) {
		const int count = std::min(block, frames + delay - at);
		AudioSessionLiveInput input{0, media.channels};
		input.replacePlayback = replace;
		if (at < frames)
			input.samples =
			    samples.subspan(size_t(at * media.channels), size_t(std::min(count, frames - at) * media.channels));
		ok &= expect(renderer.renderLiveInto(at, std::span(result).subspan(size_t(at * 2), size_t(count * 2)), scratch,
		                                     input.samples.empty() ? std::span<const AudioSessionLiveInput>()
		                                                           : std::span(&input, 1)) == Status::Ready,
		             "live input callback renders");
		at += count;
	}
	ok &= expect(silent(std::span(result).first(size_t(delay * 2))), "live processing exposes initial graph latency");
	ok &= expect(near(std::span(result).subspan(size_t(delay * 2)),
	                  {offline.clip.samples.constData(), size_t(offline.clip.samples.size())}),
	             "live input and offline media agree after exactly the reported latency");
	const auto meters = renderer.meters();
	ok &= expect(meters.strips[size_t(live.tracks.size())].post.frames == quint64(frames),
	             "live master meter excludes physical warm-up and includes the final drained sample");
	return ok;
}
bool equivalence()
{
	bool ok = true;
	for (int channels : {1, 2}) {
		for (int kind = 0; kind < 5; ++kind) {
			auto s = fixture(channels);
			s.tracks[0].pan = -.3;
			s.tracks[0].gainAutomation = {{0, -3}, {79, 2}, {100, -12}};
			s.tracks[0].panAutomation = {{0, -.6}, {120, .8}};
			s.tracks[0].routing.swapChannels = true;
			s.tracks[0].routing.invertRight = true;
			if (kind >= 1) {
				s.tracks[0].effects = {lookahead(13)};
				s.masterEffects = {lookahead(7), makeAudioEffect("saturation", 1000)};
			}
			if (kind >= 2) {
				AudioSessionTrack bus;
				bus.id = bus.name = "bus";
				bus.routing.bus = true;
				bus.effects = {makeAudioEffect("saturation", 1000), lookahead(3)};
				s.tracks[0].routing.outputId = bus.id;
				s.tracks[0].routing.sends = {{QString(), -6, .5, true, true}, {bus.id, -9, -.2, false, true}};
				s.tracks.append(bus);
				// A second source branch meets live input in nonlinear bus/master
				// processing, including pending/audible solo domains.
				auto backing = s.tracks[0];
				backing.id = backing.name = "backing";
				backing.regions[0].id = "backing-clip";
				backing.effects = {lookahead(19)};
				backing.solo = kind == 3;
				s.tracks.append(backing);
				s.tracks[1].solo = kind >= 3;
				s.tracks[0].muted = kind == 4;
			}
			for (bool replace : {false, true})
				for (int block : {1, 7, 64, 157, 4096})
					ok &= compare(s, replace, block);
		}
	}
	return ok;
}
bool windows()
{
	auto s = fixture(2, 12);
	s.tracks[0].pan = 0;
	std::fill(s.sources[0].audio.clip.samples.begin(), s.sources[0].audio.clip.samples.end(), .125f);
	const auto original = s;
	std::array<float, 8> input{};
	input.fill(.25f);
	AudioSessionLiveInput feed{0, 2, input, 3, .5, true};
	QString error;
	AudioSessionRenderer renderer;
	bool ok = renderer.prepare(s, &error, {}, {}, {}, AudioSessionRenderClock::Live);
	std::array<float, 24> output{};
	std::vector<double> scratch(renderer.scratchSamples(12));
	ok &= expect(renderer.renderLiveInto(0, output, scratch, std::span(&feed, 1)) == Status::Ready &&
	                 std::all_of(output.begin(), output.end(), [](float value) { return value == .125f; }),
	             "replacement and monitor gain apply only in the selected input window");
	feed.replacePlayback = false;
	ok &= renderer.renderLiveInto(0, output, scratch, std::span(&feed, 1)) == Status::Ready;
	for (int frame = 0; frame < 12; ++frame)
		ok &= expect(output[size_t(frame * 2)] == (frame >= 3 && frame < 7 ? .25f : .125f),
		             "additive live input meets accompaniment before inserts at exact boundaries");
	const AudioSession &shared = s;
	ok &= expect(shared.sources[0].audio.clip.samples == original.sources[0].audio.clip.samples &&
	                 !shared.tracks.isDetached() && !shared.sources.isDetached(),
	             "live render retains shared immutable session storage");
	return ok;
}
bool validation()
{
	auto s = fixture(2, 8);
	AudioSessionTrack bus;
	bus.id = bus.name = "bus";
	bus.routing.bus = true;
	s.tracks.append(bus);
	QString error;
	AudioSessionRenderer renderer;
	bool ok = renderer.prepare(s, &error, {}, {}, {}, AudioSessionRenderClock::Live);
	std::array<float, 16> output{}, input{};
	std::vector<double> scratch(renderer.scratchSamples(8));
	const AudioSessionLiveInput valid{0, 2, input};
	for (int issue = 0; issue < 13; ++issue) {
		auto feed = valid;
		switch (issue) {
		case 0:
			feed.trackIndex = -1;
			break;
		case 1:
			feed.trackIndex = 2;
			break;
		case 2:
			feed.trackIndex = 1;
			break;
		case 3:
			feed.channels = 0;
			break;
		case 4:
			feed.channels = 3;
			break;
		case 5:
			feed.samples = {};
			break;
		case 6:
			feed.samples = std::span(input).first(15);
			break;
		case 7:
			feed.offsetFrames = -1;
			break;
		case 8:
			feed.offsetFrames = 1;
			break;
		case 9:
			feed.gain = -1;
			break;
		case 10:
			feed.gain = 17;
			break;
		case 11:
			feed.gain = std::numeric_limits<double>::quiet_NaN();
			break;
		case 12:
			feed.gain = std::numeric_limits<double>::infinity();
			break;
		}
		output.fill(1);
		ok &= expect(renderer.renderLiveInto(0, output, scratch, std::span(&feed, 1)) == Status::InvalidRange &&
		                 silent(output),
		             "invalid live input clears the whole output block");
	}
	const std::array<AudioSessionLiveInput, 2> duplicate{valid, valid};
	ok &= expect(renderer.renderLiveInto(0, output, scratch, duplicate) == Status::InvalidRange,
	             "duplicate inputs cannot silently double an armed track");
	for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
		input[7] = invalid;
		ok &= expect(renderer.renderLiveInto(0, output, scratch, std::span(&valid, 1)) == Status::Overflow &&
		                 silent(output),
		             "nonfinite capture samples fail before processing");
	}
	input[7] = 0;
	ok &= expect(renderer.renderInto(0, output, scratch) == Status::InvalidRange,
	             "compensated API cannot accidentally prime live input");
	ok &= renderer.prepare(s, &error);
	ok &= expect(renderer.renderLiveInto(0, output, scratch, std::span(&valid, 1)) == Status::InvalidRange,
	             "live API requires explicit preparation");
	ok &= expect(!renderer.prepare(s, &error, {}, {"input"}, {}, AudioSessionRenderClock::Live),
	             "live strip taps require explicit future design, not hidden clock changes");
	ok &= expect(!renderer.prepare(s, &error, {}, {}, {}, AudioSessionRenderClock(42)), "unknown clock is rejected");
	ok &= renderer.prepare(s, &error, {}, {}, {}, AudioSessionRenderClock::Live);
	AudioWorkControl cancel;
	cancel.cancelled = [] { return true; };
	ok &= expect(renderer.renderLiveInto(0, output, scratch, std::span(&valid, 1), cancel) == Status::Cancelled &&
	                 silent(output),
	             "cancelled live block is silent");
	return ok;
}
bool boundaries()
{
	auto s = fixture(2, 8);
	s.tracks[0].regions.clear();
	s.tracks[0].effects = {lookahead(7)};
	s.masterEffects = {lookahead(3)};
	const auto samples = s.sources[0].audio.clip.samples;
	QString error;
	AudioSessionRenderer renderer;
	bool ok = renderer.prepare(s, &error, {}, {}, {}, AudioSessionRenderClock::Live);
	std::vector<double> scratch(renderer.scratchSamples(18));
	std::array<float, 36> output{};
	const AudioSessionLiveInput input{0, 2, {samples.constData(), size_t(samples.size())}};
	ok &= expect(renderer.renderLiveInto(AudioSessionFrameLimit - 8, output, scratch, std::span(&input, 1)) ==
	                     Status::Ready &&
	                 silent(std::span(output).first(20)) && near(std::span(output).subspan(20), input.samples),
	             "physical drain extends beyond the final authored frame without losing live samples");
	ok &= expect(renderer.renderLiveInto(AudioSessionFrameLimit - 7, output, scratch, std::span(&input, 1)) ==
	                     Status::InvalidRange &&
	                 silent(output),
	             "live samples cannot extend the authored timeline");
	std::array<float, 2> one{1, 1};
	std::array<float, 2> impulse{.25f, -.125f};
	const AudioSessionLiveInput pulse{0, 2, impulse};
	ok &= renderer.renderLiveInto(0, one, scratch, std::span(&pulse, 1)) == Status::Ready && silent(one);
	// A discontinuity discards the old physical delay history, with no input
	// from the earlier callback read or replayed after it has returned.
	ok &= expect(renderer.renderLiveInto(100, output, scratch) == Status::Ready && silent(output),
	             "seek clears live history and cannot replay a prior input span");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = equivalence();
	ok &= windows();
	ok &= validation();
	ok &= boundaries();
	return ok ? 0 : 1;
}
