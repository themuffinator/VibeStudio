#include "tests/audio_loop_test_fixture.h"
#include "tests/fake_audio_stream.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace vibestudio;
namespace
{
using State = AudioSessionPlaybackSnapshot::State;
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}
bool waitFor(const std::function<bool()> &condition, int timeout = 10000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < timeout) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	QCoreApplication::processEvents();
	return condition();
}
AudioSession fixture()
{
	AudioProject source;
	source.clip = {2, 48000, {0.25f, -0.5f, 0.75f, -1, 1.5f, -2, 0, 0.125f}};
	return importAudioSessionSource({}, source).session;
}
float sample(const QByteArray &bytes, int index)
{
	float value = 0;
	if (bytes.size() >= (index + 1) * 4)
		std::memcpy(&value, bytes.constData() + index * 4, 4);
	return value;
}
bool continuousLoops()
{
	const auto session = test::playbackLoopFixture(true);
	const auto expected = renderAudioSession(test::expandPlaybackLoop(session, 23, 70, 1200), 23, 1044);
	auto device = std::make_shared<FakeAudioStreamState>();
	device->capacity = 1021 * 8;
	device->maxWrite = 197; // Split samples and frames across worker events.
	AudioSessionPlayback playback(nullptr, fakeAudioStreamFactory(device));
	if (!expect(expected.succeeded() && waitFor([&] { return playback.available(); }),
	            "prepare independent loop worker reference"))
		return false;
	playback.setVolume(1);
	playback.start(session, {23, 70, true}, {{}, 1000, 256});
	bool ok = expect(waitFor([&] {
		                 return device->locked([](auto &v) { return v.bytes.size() == v.capacity; }) &&
		                        playback.snapshot().meters.strips[4].post.frames == 1021;
	                 }),
	                 "worker continuously fills the requested loop buffer and publishes all-cycle meters");
	const auto bytes = device->locked([](auto &v) { return v.bytes; });
	bool exact = bytes.size() == 1021 * 8;
	for (int i = 0; exact && i < 2042; ++i)
		exact &= std::abs(sample(bytes, i) - expected.clip.samples[i]) < 2e-6;
	ok &= expect(exact && device->locked([](auto &v) { return v.opens == 1; }) && playback.snapshot().position == 23,
	             "one device stream carries continuous effects across all loops without advancing the unheard cursor");
	playback.setLoop(true);
	playback.pause();
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Paused; }) &&
	                 device->locked([](auto &v) { return v.opens == 1; }),
	             "an unchanged loop policy preserves the stream and pause acknowledgement");
	device->locked([](auto &v) { v.processed = 51; });
	playback.setLoop(false);
	ok &= expect(waitFor([&] {
		             return device->locked([](auto &v) {
			             return v.opens == 2 && v.state == AudioStreamDeviceStatus::State::Paused && v.bytes.isEmpty();
		             });
	             }) &&
	                 waitFor([&] { return playback.snapshot().position == 27; }),
	             "explicit loop-policy change discards queued cycles and retains pause at the consumed cursor");
	playback.resume();
	ok &= expect(waitFor([&] { return device->locked([](auto &v) { return v.bytes.size() == 43 * 8; }); }),
	             "finite output after loop-policy change ends at the selected range endpoint");
	const auto finite = renderAudioSession(session, 27, 70);
	const auto finiteBytes = device->locked([](auto &v) { return v.bytes; });
	exact = finite.succeeded() && finiteBytes.size() == 43 * 8;
	for (int i = 0; exact && i < 86; ++i)
		exact &= sample(finiteBytes, i) == finite.clip.samples[i];
	ok &= expect(exact, "loop-policy change preserves ordinary finite render compensation and effect initialization");
	playback.stop();
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	auto device = std::make_shared<FakeAudioStreamState>();
	device->maxWrite = 3; // Intentionally splits both samples and frames.
	AudioSessionPlayback playback(nullptr, fakeAudioStreamFactory(device));
	playback.stop(); // A review can cancel before asynchronous device discovery completes.
	bool ok = expect(waitFor([&] { return playback.available(); }) && playback.snapshot().state == State::Stopped,
	                 "device discovery restores stopped readiness after an early Stop");
	const auto session = fixture();
	playback.setVolume(1);
	playback.start(session, {0, 4, false}, {"fixture", 48000, 256});
	ok &= expect(waitFor([&] { return device->locked([](auto &v) { return v.bytes.size() == 32; }); }),
	             "all short writes are eventually delivered");
	const auto bytes = device->locked([](auto &v) { return v.bytes; });
	ok &= expect(bytes.size() == 32 && sample(bytes, 0) == .25f && sample(bytes, 1) == -.5f && sample(bytes, 4) == 1 &&
	                 sample(bytes, 5) == -1 && sample(bytes, 7) == .125f,
	             "partial writes preserve exact sample order and audition clamps headroom");
	ok &= expect(playback.snapshot().state == State::Playing && playback.snapshot().position == 0,
	             "rendered lookahead does not falsely move the audible cursor or end before drain");
	device->locked([](auto &v) {
		v.processed = 4;
		v.state = AudioStreamDeviceStatus::State::Idle;
	});
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Ended; }) &&
	                 playback.snapshot().position == 4 && playback.snapshot().samplesAboveFullScale == 2 &&
	                 playback.snapshot().underruns == 0,
	             "finite drain reaches exact endpoint without reporting a false dropout");
	const auto &masterMeters = playback.snapshot().meters.strips[1].post;
	ok &= expect(playback.snapshot().meters.count == 2 && masterMeters.frames == 4 &&
	                 masterMeters.maximum == std::array<double, 2>{1.5, 2} &&
	                 masterMeters.samplesAboveFullScale == std::array<quint64, 2>{1, 1},
	             "live meters count each frame once despite short writes and precede audition clipping");
	auto routed = session;
	AudioSessionTrack bus;
	bus.id = bus.name = "routed-bus";
	bus.routing.bus = true;
	bus.routing.invertLeft = true;
	bus.pan = -.25;
	bus.gainAutomation = {{0, -6}, {3, -12}};
	bus.effects = {makeAudioEffect("compressor", 48000)};
	routed.tracks[0].gainDb = -12;
	routed.tracks[0].routing.outputId = bus.id;
	routed.tracks[0].routing.sends.append({QString(), -18, 0, true, true});
	routed.tracks.append(bus);
	routed.tracks[0].effects = {makeAudioEffect("delay", 48000)};
	routed.tracks[0].effects[0].parameters["leftMs"] = 1;
	routed.tracks[0].effects[0].parameters["rightMs"] = 1.5;
	routed.effectTailSeconds = .002;
	routed.masterEffects = {makeAudioEffect("limiter", 48000)};
	const auto routedReference = renderAudioSession(routed);
	const int routedFrames = int(audioSessionFrames(routed));
	device->locked([](auto &v) { v.capacity = 2048; });
	const int beforeRouting = device->locked([](auto &v) { return v.opens; });
	playback.start(routed, {0, routedFrames, false}, {"fixture", 48000, 256});
	ok &= expect(waitFor([&] {
		             return device->locked(
		                 [&](auto &v) { return v.opens > beforeRouting && v.bytes.size() == routedFrames * 8; });
	             }),
	             "routed worker stream delivers all split samples");
	const auto routedBytes = device->locked([](auto &v) { return v.bytes; });
	bool routedExact = routedReference.succeeded() && routedFrames == 100 && routedBytes.size() == routedFrames * 8;
	for (int i = 0; routedExact && i < routedFrames * 2; ++i)
		routedExact &= sample(routedBytes, i) == std::clamp(routedReference.clip.samples[i], -1.0f, 1.0f);
	ok &= expect(routedExact && sample(routedBytes, 96) != 0,
	             "worker effects, delay tail, bus automation and dry pre-fader sends match offline rendering exactly");
	device->locked([&](auto &v) {
		v.processed = routedFrames;
		v.state = AudioStreamDeviceStatus::State::Idle;
	});
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Ended; }), "routed stream drains normally");
	auto modulated = routed;
	modulated.effectTailSeconds = .1;
	for (const auto &type : {"reverb", "chorus", "flanger", "phaser", "tremolo"})
		modulated.tracks[0].effects.append(makeAudioEffect(type, 48000));
	modulated.tracks[0].effects.append(makeAudioEffect("lookahead-limiter", 48000));
	modulated.tracks[1].effects.append(makeAudioEffect("lookahead-limiter", 48000));
	modulated.masterEffects = {makeAudioEffect("lookahead-limiter", 48000)};
	for (const auto &effect : modulated.tracks[0].effects) {
		const auto parameter = audioEffectParameters(effect.type, 48000).first();
		modulated.tracks[0].effectAutomation.append({effect.id,
		                                             parameter.key,
		                                             true,
		                                             {{0, parameter.initial, AudioAutomationCurve::Smooth},
		                                              {2500, parameter.minimum},
		                                              {4800, parameter.initial}}});
	}
	modulated.masterEffectAutomation = {
	    {modulated.masterEffects[0].id, "ceilingDb", true, {{0, -3}, {4800, -9, AudioAutomationCurve::Step}}}};
	modulated.tracks[1].effectAutomation = {{modulated.tracks[1].effects[0].id,
	                                         "thresholdDb",
	                                         true,
	                                         {{0, -18, AudioAutomationCurve::Smooth}, {4800, -24}}}};
	const auto modulatedReference = renderAudioSession(modulated);
	const int modulatedFrames = int(audioSessionFrames(modulated));
	const int beforeModulation = device->locked([](auto &v) {
		v.maxWrite = 197;
		v.capacity = 65536;
		return v.opens;
	});
	playback.start(modulated, {0, modulatedFrames, false}, {"fixture", 48000, 256});
	ok &= expect(waitFor([&] {
		             return device->locked(
		                 [&](auto &v) { return v.opens > beforeModulation && v.bytes.size() == modulatedFrames * 8; });
	             }),
	             "modulation and reverb stream through split-sample worker writes");
	const auto modulatedBytes = device->locked([](auto &v) { return v.bytes; });
	bool modulatedExact = modulatedReference.succeeded() && modulatedBytes.size() == modulatedFrames * 8;
	bool nonzeroTail = false;
	for (int i = 0; modulatedExact && i < modulatedFrames * 2; ++i) {
		modulatedExact &= sample(modulatedBytes, i) == std::clamp(modulatedReference.clip.samples[i], -1.0f, 1.0f);
		if (i > 2400 && modulatedReference.clip.samples[i] != 0)
			nonzeroTail = true;
	}
	ok &= expect(modulatedExact && nonzeroTail,
	             "every modulation/reverb worker sample and late tail equals offline output");
	device->locked([&](auto &v) {
		v.processed = modulatedFrames;
		v.state = AudioStreamDeviceStatus::State::Idle;
	});
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Ended; }), "new effect tail drains normally");
	device->locked([](auto &v) {
		v.maxWrite = 32768;
		v.capacity = 2048;
	});
	playback.start(session, {0, 4, true}, {{}, 48000, 256});
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Playing; }), "start looping stream");
	device->locked([](auto &v) { v.processed = 3; });
	ok &= expect(waitFor([&] { return playback.snapshot().position == 3; }),
	             "device consumed-frame clock maps into loop");
	playback.pause();
	ok &=
	    expect(waitFor([&] { return playback.snapshot().state == State::Paused; }) && playback.snapshot().position == 3,
	           "pause retains buffered position");
	const auto pausedPosition = playback.snapshot().position;
	const auto pausedBytes = device->locked([](auto &v) { return v.bytes; });
	ok &= expect(playback.snapshot().meters.strips[0].post.frames > 0, "paused meter holds its measured history");
	playback.resetMeters();
	ok &= expect(waitFor([&] { return playback.snapshot().meters.strips[0].post.frames == 0; }) &&
	                 playback.snapshot().position == pausedPosition && playback.snapshot().state == State::Paused &&
	                 device->locked([&](auto &v) { return v.bytes == pausedBytes; }),
	             "meter reset is worker-acknowledged without seeking or changing queued audio");
	const int opens = device->locked([](auto &v) { return v.opens; });
	playback.resume();
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Playing; }) &&
	                 device->locked([&](auto &v) { return v.opens == opens && v.resumes == 1; }),
	             "resume retains the same device stream");
	device->locked(
	    [](auto &v) { v.devices = {{"fixture", "Former default", false}, {"second", "New default", true}}; });
	playback.seek(1);
	ok &= expect(
	    waitFor([&] { return device->locked([&](auto &v) { return v.opens == opens + 1 && !v.bytes.isEmpty(); }); }) &&
	        waitFor([&] { return playback.snapshot().position == 1; }),
	    "seek resets queued lookahead and reopens at exact frame");
	ok &= expect(device->locked([](auto &v) { return v.configuration.deviceId == "fixture"; }),
	             "seeking retains the resolved output after a system-default change");
	const auto sought = device->locked([](auto &v) { return v.bytes; });
	ok &= expect(sample(sought, 0) == .75f && sample(sought, 1) == -1,
	             "no stale pre-seek samples reach restarted output");
	playback.setLoop(false);
	ok &= expect(
	    waitFor([&] { return device->locked([&](auto &v) { return v.opens == opens + 2 && v.bytes.size() == 24; }); }),
	    "disabling loop discards queued repetitions and renders only the remaining finite tail");
	playback.pause();
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Paused; }),
	             "pause can retain a finite draining tail");
	playback.setLoop(true);
	ok &= expect(waitFor([&] {
		             return device->locked([&](auto &v) {
			             return v.opens == opens + 3 && v.state == AudioStreamDeviceStatus::State::Paused;
		             });
	             }),
	             "changing loop while paused reopens without autoplay");
	playback.resume();
	ok &= expect(waitFor([&] {
		             return playback.snapshot().state == State::Playing &&
		                    device->locked([](auto &v) { return !v.bytes.isEmpty(); });
	             }),
	             "resume after paused loop change fills a new stream");
	device->locked([](auto &v) {
		v.state = AudioStreamDeviceStatus::State::Idle;
		v.stall = true;
	});
	ok &= expect(waitFor([&] { return playback.snapshot().underruns == 1; }), "unexpected starvation is visible");
	int heartbeat = 0;
	QTimer timer;
	timer.setInterval(2);
	QObject::connect(&timer, &QTimer::timeout, &app, [&] { ++heartbeat; });
	timer.start();
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Error; }, 7000) && heartbeat > 100 &&
	                 playback.snapshot().underruns == 1,
	             "persistent device stall fails after deadline without blocking GUI or flooding dropout count");
	device->locked([](auto &v) {
		v.stall = false;
		v.rejectOpen = true;
	});
	playback.start(session, {0, 4, true}, {});
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Error; }) &&
	                 playback.snapshot().error == "Fixture open failure",
	             "device-open errors propagate explicitly");
	device->locked([](auto &v) { v.rejectOpen = false; });
	playback.start(session, {0, 4, true}, {});
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Playing; }), "retry after open error");
	device->locked([](auto &v) { v.state = AudioStreamDeviceStatus::State::Error; });
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Error; }) &&
	                 playback.snapshot().error == "Fixture disconnected",
	             "device removal fails without switching outputs");
	playback.start(session, {0, AudioSessionFrameLimit, true}, {});
	playback.stop();
	const int before = heartbeat;
	ok &= expect(waitFor([&] { return heartbeat > before + 60; }) && playback.snapshot().state == State::Stopped &&
	                 device->locked([](auto &v) { return v.state == AudioStreamDeviceStatus::State::Closed; }),
	             "stop cancels preparation and stale worker events cannot restart playback");
	ok &= expect(playback.snapshot().meters.count == 0, "generation-guarded stop cannot revive stale meter rows");
	playback.start(session, {10000000000LL, AudioSessionFrameLimit, true}, {});
	ok &= expect(waitFor([&] { return playback.snapshot().state == State::Playing; }) &&
	                 device->locked([](auto &v) { return v.bytes.size() <= v.capacity; }),
	             "long sparse ranges stream with bounded buffers instead of whole-range materialization");
	int notifications = 0;
	const auto connection = QObject::connect(&playback, &AudioSessionPlayback::changed, &app, [&] { ++notifications; });
	QThread::msleep(300); // Deliberately block this fixture's UI, never the user's UI.
	QCoreApplication::processEvents();
	QObject::disconnect(connection);
	ok &= expect(notifications <= 2, "blocked UI coalesces live meter packets into a bounded pending notification");
	playback.setLoop(false);
	playback.stop();
	ok &= continuousLoops();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
