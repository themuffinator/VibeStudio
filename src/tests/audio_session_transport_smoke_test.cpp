#include "core/audio_transport.h"
#include <QCoreApplication>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}
AudioSession fixture()
{
	AudioSession session;
	AudioSessionSource source;
	source.id = "source";
	source.audio.clip = {2, 48000, {0.25f, -0.5f, 0.75f, -1, 1.5f, -2, 0, 0.125f}};
	session.sources.append(source);
	AudioSessionTrack track;
	track.id = "track";
	track.name = "Independent stereo fixture";
	AudioSessionRegion region;
	region.id = "clip";
	region.sourceId = source.id;
	region.position = 10000000000LL;
	region.length = 4;
	track.regions.append(region);
	session.tracks.append(track);
	return session;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	bool ok = true;
	const auto session = fixture();
	constexpr qint64 first = 10000000000LL;
	QString error;
	AudioSessionRenderer renderer;
	ok &= expect(renderer.prepare(session, &error), "prepare independently specified stereo fixture");
	if (!ok) {
		std::cerr << error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	std::array<float, 8> samples{};
	std::array<double, 8> scratch{};
	ok &= expect(renderer.renderInto(first, samples, scratch) == AudioSessionRenderer::BlockStatus::Ready &&
	                 std::equal(samples.begin(), samples.end(), session.sources[0].audio.clip.samples.cbegin()),
	             "caller-buffer renderer matches original unity stereo samples, including float headroom");
	ok &= expect(renderer.renderInto(first, samples, std::span<double>(scratch).first(7)) ==
	                     AudioSessionRenderer::BlockStatus::InvalidRange &&
	                 std::all_of(samples.begin(), samples.end(), [](float s) { return s == 0; }),
	             "short accumulation storage cannot expose partial or previous samples");
	for (const int internal : {1, 3, 256, 4096}) {
		AudioTransport transport;
		ok &= expect(transport.prepare(session, {first, first + 4, true}, internal, &error) && transport.play(),
		             "prepare loop transport");
		std::vector<float> streamed;
		for (const int count : {1, 7, 5, 128, 8193}) {
			std::vector<float> output(size_t(count) * 2);
			const auto block = transport.process(output);
			ok &= expect(block.frames == count && block.status == AudioSessionRenderer::BlockStatus::Ready,
			             "loop output spans arbitrary callback and internal boundaries");
			streamed.insert(streamed.end(), output.begin(), output.end());
		}
		bool exact = true;
		for (size_t i = 0; i < streamed.size(); ++i)
			exact &= streamed[i] == session.sources[0].audio.clip.samples[qsizetype(i % 8)];
		const auto frames = streamed.size() / 2;
		ok &= expect(exact && transport.position() == first + qint64(frames % 4) && transport.loops() == frames / 4 &&
		                 transport.framesRendered() == frames,
		             "far-frame loop clock and every sample match a modulo oracle for all block sizes");
		ok &= expect(transport.positionAfter(first + 1, std::numeric_limits<quint64>::max()) == first,
		             "device cursor mapping cannot overflow on a long-running loop");
		const auto pausedAt = transport.position();
		transport.pause();
		const auto paused = transport.process(samples);
		ok &= expect(paused.frames == 0 && transport.position() == pausedAt && samples[0] == 0,
		             "pause emits silence without advancing time");
		ok &= expect(transport.seek(first + 2) && transport.state() == AudioTransport::State::Paused &&
		                 !transport.seek(first - 1),
		             "seek preserves pause and rejects out-of-range frames");
		transport.setLoop(false);
		transport.play();
		const auto tail = transport.process(samples);
		ok &= expect(tail.frames == 2 && tail.samplesAboveFullScale == 2 && tail.peak[1] == 2 && samples[0] == 1.5f &&
		                 samples[4] == 0 && transport.position() == first + 4 &&
		                 transport.state() == AudioTransport::State::Ended,
		             "finite tail retains headroom, meters exact samples and zeroes unused device space");
		ok &= expect(transport.process(samples).frames == 0 && samples[0] == 0,
		             "drained transport does not restart itself");
		transport.play();
		ok &= expect(transport.position() == first, "explicit play at endpoint restarts the range");
		int calls = 0;
		const auto before = transport.position();
		const auto cancelled = transport.process(samples, {[&] { return ++calls >= 2; }});
		ok &= expect(cancelled.status == AudioSessionRenderer::BlockStatus::Cancelled && cancelled.frames == 0 &&
		                 transport.position() == before && samples[0] == 0,
		             "cancelled block cannot advance time or expose partial mix");
		transport.stop();
		ok &= expect(transport.position() == first && transport.framesRendered() == 0, "stop resets the clock");
	}
	AudioSession varied = session;
	varied.tracks[0].regions[0].fadeIn = 3;
	varied.tracks[0].gainAutomation = {{first, 0}, {first + 3, -6}};
	varied.tracks[0].panAutomation = {{first, -1}, {first + 3, 1}};
	AudioTransport transport;
	ok &= expect(transport.prepare(varied, {first, first + 4, false}, 2, &error) && transport.play(),
	             "prepare fades and automation");
	transport.process(samples);
	for (int frame = 0; frame < 4; ++frame) {
		const double fade = frame < 3 ? double(frame) / 2 : 1;
		const double gain = std::pow(10., (-2. * frame) / 20.);
		const double pan = -1. + 2. * frame / 3.;
		const double left = pan >= 1 ? 0 : pan <= 0 ? 1 : std::cos(pan * std::acos(-1.) / 2.);
		const double right = pan <= -1 ? 0 : pan >= 0 ? 1 : std::cos(pan * std::acos(-1.) / 2.);
		ok &= expect(std::abs(samples[size_t(frame * 2)] -
		                      session.sources[0].audio.clip.samples[frame * 2] * fade * gain * left) < 1e-7 &&
		                 std::abs(samples[size_t(frame * 2 + 1)] -
		                          session.sources[0].audio.clip.samples[frame * 2 + 1] * fade * gain * right) < 1e-7,
		             "sample automation and fades match independent analytic values across internal blocks");
	}
	ok &= expect(!transport.prepare(session, {first, first, false}, 1024, &error) &&
	                 transport.state() == AudioTransport::State::Empty,
	             "failed preparation cannot retain a playable old range");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
