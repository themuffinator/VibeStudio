#include "core/audio_effects.h"
#include "core/audio_session_io.h"
#include "core/audio_transport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value)
		std::cerr << text << '\n';
	return value;
}
bool close(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }
std::vector<double> signal(int frames, double frequency, int rate = 48000)
{
	std::vector<double> result(size_t(frames) * 2);
	for (int i = 0; i < frames; ++i) {
		result[size_t(i) * 2] = .25 * std::sin(2 * std::numbers::pi * frequency * i / rate);
		result[size_t(i) * 2 + 1] = result[size_t(i) * 2] / 3;
	}
	return result;
}
double rms(const std::vector<double> &values, int first)
{
	double sum = 0;
	for (size_t i = size_t(first) * 2; i < values.size(); i += 2)
		sum += values[i] * values[i];
	return std::sqrt(sum / (values.size() / 2 - size_t(first)));
}
AudioSession sessionFixture()
{
	AudioProject source;
	source.clip = {2, 48000, {}};
	const auto samples = signal(5000, 421);
	for (double sample : samples)
		source.clip.samples.append(float(sample));
	return importAudioSessionSource({}, source).session;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	for (int rate : {1, 8000, 48000, 384000})
		for (const auto &type : audioEffectTypes()) {
			auto effect = makeAudioEffect(type, rate);
			ok &= expect(validateAudioEffects({effect}, rate).isEmpty(),
			             "every default effect is valid across supported rates");
			effect.enabled = false;
			AudioEffectsProcessor processor;
			auto dry = signal(127, 1000), before = dry;
			ok &= expect(processor.prepare({effect}, rate, &error) && processor.process(dry) && dry == before,
			             "bypassed processors retain exact dry samples");
		}
	for (const auto &type : {QString("low-pass"), QString("high-pass"), QString("notch"), QString("peak-eq")})
		for (double frequency : {100.0, 1000.0, 8000.0}) {
			auto effect = makeAudioEffect(type, 48000);
			effect.parameters["frequencyHz"] = 1000;
			effect.parameters["q"] = 1 / std::sqrt(2.0);
			if (type == "peak-eq")
				effect.parameters["gainDb"] = 12;
			AudioEffectsProcessor processor;
			ok &= expect(processor.prepare({effect}, 48000, &error), "prepare frequency-response fixture");
			auto samples = signal(96000, frequency), dry = samples;
			ok &= expect(processor.process(samples), "process sustained sine");
			const double r = std::tan(std::numbers::pi * frequency / 48000) / std::tan(std::numbers::pi * 1000 / 48000);
			const double damping = std::sqrt(2.0) * r, square = (1 - r * r) * (1 - r * r);
			double expected = 1 / std::sqrt(square + damping * damping);
			if (type == "high-pass")
				expected *= r * r;
			if (type == "notch")
				expected *= std::abs(1 - r * r);
			if (type == "peak-eq") {
				const double a = std::pow(10.0, 12.0 / 40);
				expected = std::sqrt((square + damping * damping * a * a) / (square + damping * damping / (a * a)));
			}
			ok &= expect(close(rms(samples, 48000) / rms(dry, 48000), expected, 1e-8),
			             "measured response matches independent warped analog transfer function");
		}
	for (const auto &type : {QString("low-shelf"), QString("high-shelf")})
		for (bool alternating : {false, true}) {
			auto effect = makeAudioEffect(type, 48000);
			effect.parameters["gainDb"] = 12;
			AudioEffectsProcessor processor;
			ok &= processor.prepare({effect}, 48000, &error);
			std::vector<double> samples(96000, .25);
			if (alternating)
				for (size_t i = 0; i < samples.size(); ++i)
					if ((i / 2) % 2)
						samples[i] *= -1;
			ok &= processor.process(samples);
			const double gain = (type == "high-shelf") == alternating ? std::pow(10.0, 12.0 / 20) : 1;
			ok &= expect(close(std::abs(samples.back()), .25 * gain, 1e-10),
			             "shelf endpoints independently verify DC and Nyquist gain");
		}
	auto delay = makeAudioEffect("delay", 1000);
	delay.parameters = {{"leftMs", 2.5}, {"rightMs", 4}, {"feedback", .5}, {"mix", 1}};
	AudioEffectsProcessor delayed;
	ok &= delayed.prepare({delay}, 1000, &error);
	std::vector<double> impulse(64, 0), expected(64, 0);
	impulse[0] = 1;
	impulse[1] = 2;
	for (int frame = 0; frame < 32; ++frame) {
		if (frame >= 2)
			expected[size_t(frame) * 2] += .5 * impulse[size_t(frame - 2) * 2] + .25 * expected[size_t(frame - 2) * 2];
		if (frame >= 3)
			expected[size_t(frame) * 2] += .5 * impulse[size_t(frame - 3) * 2] + .25 * expected[size_t(frame - 3) * 2];
		if (frame >= 4)
			expected[size_t(frame) * 2 + 1] =
			    impulse[size_t(frame - 4) * 2 + 1] + .5 * expected[size_t(frame - 4) * 2 + 1];
	}
	ok &= expect(delayed.process(impulse) && impulse == expected,
	             "fractional stereo delay agrees with independent recurrence");
	delayed.reset();
	std::fill(impulse.begin(), impulse.end(), 0);
	ok &=
	    expect(delayed.process(impulse) && std::all_of(impulse.begin(), impulse.end(), [](double v) { return v == 0; }),
	           "constant-time delay reset masks every stale sample");
	auto compressor = makeAudioEffect("compressor", 1000);
	compressor.parameters["thresholdDb"] = -24;
	compressor.parameters["ratio"] = 4;
	compressor.parameters["kneeDb"] = 0;
	compressor.parameters["attackMs"] = 1;
	AudioEffectsProcessor compressed;
	ok &= compressed.prepare({compressor}, 1000, &error);
	std::vector<double> steady(2000, 1);
	for (size_t i = 1; i < steady.size(); i += 2)
		steady[i] = .25;
	ok &= compressed.process(steady);
	ok &= expect(close(steady[0], std::pow(10.0, -18 * (1 - std::exp(-1.0)) / 20)) &&
	                 close(steady[1998], std::pow(10.0, -18.0 / 20)) && close(steady[1999], steady[1998] / 4),
	             "compressor attack, static curve and stereo link have independent expectations");
	auto gate = makeAudioEffect("gate", 1000);
	gate.parameters["holdMs"] = 2;
	gate.parameters["attackMs"] = .1;
	gate.parameters["thresholdDb"] = -20;
	gate.parameters["hysteresisDb"] = 0;
	AudioEffectsProcessor gated;
	ok &= gated.prepare({gate}, 1000, &error);
	std::vector<double> quiet(8, .01);
	ok &= gated.process(quiet);
	ok &= expect(close(quiet[0], 1e-6), "gate starts at its configured closed attenuation");
	std::array<double, 2> loud{1, .5};
	ok &= gated.process(loud);
	quiet.assign(8, .01);
	ok &= gated.process(quiet);
	ok &= expect(quiet[2] >= quiet[0] && quiet[4] < quiet[2], "gate holds open before release starts");
	auto limiter = makeAudioEffect("limiter", 48000);
	limiter.parameters["ceilingDb"] = -6;
	AudioEffectsProcessor limited;
	ok &= limited.prepare({limiter}, 48000, &error);
	std::array<double, 4> peaks{4, .5, .1, -.2};
	ok &= limited.process(peaks);
	const double ceiling = std::pow(10.0, -6.0 / 20);
	ok &=
	    expect(close(peaks[0], ceiling) && close(peaks[1], ceiling / 8) && peaks[2] > .1 * ceiling / 4 && peaks[2] < .1,
	           "limiter uses instantaneous linked attack and releases toward unity");
	auto saturation = makeAudioEffect("saturation", 48000);
	AudioEffectsProcessor saturated;
	ok &= saturated.prepare({saturation}, 48000, &error);
	std::array<double, 4> drive{.1, -.4, 1, -1};
	ok &= saturated.process(drive);
	const double boost = std::pow(10.0, 6.0 / 20);
	ok &= expect(close(drive[0], std::tanh(.1 * boost) / std::tanh(boost)) && drive[2] == 1 && drive[3] == -1,
	             "saturation follows its explicit transfer curve");
	for (int malformed = 0; malformed < 4; ++malformed) {
		auto invalid = delay;
		if (malformed == 0)
			invalid.parameters["unknown"] = 1;
		if (malformed == 1)
			invalid.parameters.remove("mix");
		if (malformed == 2)
			invalid.parameters["mix"] = std::numeric_limits<double>::infinity();
		if (malformed == 3)
			invalid.type = "future";
		ok &= expect(!validateAudioEffects({invalid}, 1000).isEmpty() &&
		                 audioEffectMemoryBytes({invalid}, 1000) > AudioEffectMemoryLimit,
		             "malformed parameters are rejected before memory calculation");
	}
	auto session = sessionFixture();
	session.effectTailSeconds = .01;
	session.tracks[0].effects = {makeAudioEffect("low-pass", 48000), makeAudioEffect("compressor", 48000),
	                             makeAudioEffect("delay", 48000)};
	session.tracks[0].effects[2].parameters["leftMs"] = 1;
	session.tracks[0].effects[2].parameters["rightMs"] = 2.5;
	AudioSessionTrack bus;
	bus.id = bus.name = "bus";
	bus.routing.bus = true;
	bus.effects = {makeAudioEffect("peak-eq", 48000)};
	bus.effects[0].parameters["gainDb"] = 6;
	session.tracks[0].routing.outputId = bus.id;
	session.tracks.append(bus);
	session.masterEffects = {limiter};
	ok &= expect(audioSessionFrames(session) == 5480, "saved tail extends the default render range once");
	AudioSessionRenderer renderer;
	if (!expect(renderer.prepare(session, &error), "prepare session effects"))
		return EXIT_FAILURE;
	const auto reference = renderer.renderBlock(0, 5480);
	if (!expect(reference.succeeded(), "render effect reference"))
		return EXIT_FAILURE;
	for (int frames : {1, 7, 256, 4096}) {
		QVector<float> joined;
		for (int at = 0; at < 5480; at += frames)
			joined += renderer.renderBlock(at, std::min(frames, 5480 - at)).clip.samples;
		ok &= expect(joined == reference.clip.samples, "contiguous stateful effect rendering is block-size invariant");
	}
	AudioSessionRenderer restarted;
	ok &= restarted.prepare(session, &error);
	const auto restartRange = restarted.renderBlock(128, 256).clip.samples;
	int cancelledBlocks = 0;
	for (int cancelAt = 1; cancelAt <= 64; ++cancelAt) {
		ok &= renderer.renderBlock(0, 128).succeeded();
		std::vector<float> cancelledOutput(512, 999);
		std::vector<double> scratch(renderer.scratchSamples(256));
		int probes = 0;
		AudioWorkControl control;
		control.cancelled = [&] { return ++probes == cancelAt; };
		const auto status = renderer.renderInto(128, cancelledOutput, scratch, control);
		if (status != AudioSessionRenderer::BlockStatus::Cancelled)
			continue;
		++cancelledBlocks;
		ok &= expect(
		    std::all_of(cancelledOutput.begin(), cancelledOutput.end(), [](float value) { return value == 0; }) &&
		        renderer.renderBlock(128, 256).clip.samples == restartRange,
		    "cancellation at each observed boundary clears complete output and processor history");
	}
	ok &= expect(cancelledBlocks > 3, "cancellation sweep reaches processing boundaries after preparation");
	ok &= renderer.renderBlock(0, 128).succeeded();
	ok &=
	    expect(!renderer.renderBlock(-1, 10).succeeded() && renderer.renderBlock(128, 256).clip.samples == restartRange,
	           "invalid block requests reset state consistently with caller-owned rendering");
	auto overflowing = sessionFixture();
	overflowing.tracks[0].effects = {makeAudioEffect("delay", 48000)};
	overflowing.tracks[0].gainDb = 24;
	overflowing.sources[0].audio.clip.samples[8000] = std::numeric_limits<float>::max();
	AudioSessionRenderer overflowRenderer;
	ok &= overflowRenderer.prepare(overflowing, &error);
	std::vector<float> overflowOutput(10000, 999);
	std::vector<double> overflowScratch(overflowRenderer.scratchSamples(5000));
	const auto overflowStatus = overflowRenderer.renderInto(0, overflowOutput, overflowScratch);
	const auto clearedTail = overflowRenderer.renderBlock(5000, 256);
	ok &=
	    expect(overflowStatus == AudioSessionRenderer::BlockStatus::Overflow &&
	               std::all_of(overflowOutput.begin(), overflowOutput.end(), [](float value) { return value == 0; }) &&
	               clearedTail.succeeded() &&
	               std::all_of(clearedTail.clip.samples.cbegin(), clearedTail.clip.samples.cend(),
	                           [](float value) { return value == 0; }),
	           "late floating-point overflow clears the written prefix and pending delay samples");
	AudioTransport transport;
	ok &= expect(transport.prepare(session, {0, 100, true}, 7, &error) && transport.play(),
	             "prepare effects transport loop");
	std::array<float, 600> loop{};
	const auto looped = transport.process(loop);
	auto repeated = session;
	for (qsizetype i = 0; i < repeated.sources[0].audio.clip.samples.size(); ++i)
		repeated.sources[0].audio.clip.samples[i] = session.sources[0].audio.clip.samples[i % 200];
	const auto continuous = renderAudioSession(repeated, 0, 300);
	bool loopExact = looped.frames == 300 && continuous.succeeded();
	for (size_t i = 0; i < loop.size(); ++i)
		loopExact &= loop[i] == continuous.clip.samples[qsizetype(i)];
	ok &= expect(loopExact, "looping equals repeated source audio with continuous filter, dynamics and delay history");
	ok &= transport.seek(10);
	std::array<float, 20> sought{};
	transport.process(sought);
	AudioSessionRenderer fresh;
	ok &= fresh.prepare(session, &error);
	const auto freshRange = fresh.renderBlock(10, 10);
	ok &= expect(std::equal(sought.begin(), sought.end(), freshRange.clip.samples.cbegin()),
	             "seek starts the same fresh-state range as export");
	ok &= transport.prepare(session, {0, 5480, false}, 7, &error) && transport.play();
	std::array<float, 200> paused{};
	transport.process(paused);
	transport.pause();
	const auto silence = transport.process(paused);
	ok &= expect(silence.frames == 0 &&
	                 std::all_of(paused.begin(), paused.end(), [](float sample) { return sample == 0; }),
	             "paused transport emits no frames");
	ok &= transport.play();
	transport.process(paused);
	ok &= expect(std::equal(paused.begin(), paused.end(), reference.clip.samples.cbegin() + 200),
	             "pause/resume retains filter, dynamics and delay history");
	transport.stop();
	ok &= transport.play();
	transport.process(paused);
	ok &= expect(std::equal(paused.begin(), paused.end(), reference.clip.samples.cbegin()),
	             "stop/restart discards all processor history");
	const auto bytes = encodeAudioSession(session, &error);
	AudioSession read;
	ok &= expect(!bytes.isEmpty() && decodeAudioSession(bytes, &read, &error) &&
	                 audioSessionSummary(read) == audioSessionSummary(session),
	             "native session persists exact chain order, parameters, bypass and tail");
	const auto metadataSize = qFromLittleEndian<quint32>(bytes.constData() + 12);
	const auto metadata = QJsonDocument::fromJson(bytes.mid(16, metadataSize)).object();
	const auto payload = bytes.mid(16 + metadataSize, bytes.size() - 48 - metadataSize);
	for (int bad = 0; bad < 6; ++bad) {
		auto altered = metadata;
		auto rootEffects = altered.value("effects").toObject();
		if (bad == 0)
			altered.remove("effects");
		if (bad == 1) {
			rootEffects["tailSeconds"] = -1;
			altered["effects"] = rootEffects;
		}
		if (bad >= 2) {
			auto tracks = altered.value("tracks").toArray();
			auto track = tracks[0].toObject();
			auto chain = track.value("effects").toArray();
			if (bad == 2)
				track.remove("effects");
			if (bad == 3) {
				chain.append(chain[0]);
				track["effects"] = chain;
			}
			if (bad == 4) {
				auto effect = chain[0].toObject();
				effect["future"] = true;
				chain[0] = effect;
				track["effects"] = chain;
			}
			if (bad == 5) {
				auto effect = chain[0].toObject();
				effect["parameters"] = QJsonObject{};
				chain[0] = effect;
				track["effects"] = chain;
			}
			tracks[0] = track;
			altered["tracks"] = tracks;
		}
		const auto json = QJsonDocument(altered).toJson(QJsonDocument::Compact);
		auto wire = bytes.first(16);
		qToLittleEndian<quint32>(quint32(json.size()), wire.data() + 12);
		wire += json + payload;
		wire += QCryptographicHash::hash(wire, QCryptographicHash::Sha256);
		const auto previous = audioSessionSummary(read);
		ok &= expect(!decodeAudioSession(wire, &read, &error) && audioSessionSummary(read) == previous,
		             "checksummed malformed v3 effects are rejected without mutating the destination");
	}
	auto atLimit = session;
	atLimit.tracks[0].regions[0].position = AudioSessionFrameLimit - atLimit.tracks[0].regions[0].length;
	ok &= expect(!validateAudioSessionStructure(atLimit).isEmpty(),
	             "tail beyond timeline capacity is rejected, not truncated");
	atLimit.effectTailSeconds = 0;
	ok &= expect(validateAudioSessionStructure(atLimit).isEmpty() &&
	                 audioSessionFrames(atLimit) == AudioSessionFrameLimit,
	             "explicit zero tail permits a clip ending at the timeline limit");
	AudioEffectChain excessive;
	for (int i = 0; i < 9; ++i)
		excessive.append(makeAudioEffect("gain", 48000));
	ok &= expect(!validateAudioEffects(excessive, 48000).isEmpty(),
	             "chain length limit applies before processor preparation");
	auto nonlinear = sessionFixture();
	nonlinear.effectTailSeconds = 0;
	nonlinear.sources[0].audio.clip.samples.fill(.1f);
	AudioSessionTrack second = nonlinear.tracks[0];
	second.id = second.name = "second";
	second.regions[0].id = "second-clip";
	second.gainDb = 20 * std::log10(3.0);
	nonlinear.tracks[0].solo = true;
	nonlinear.tracks[0].routing.outputId = second.routing.outputId = "x";
	nonlinear.tracks.append(second);
	AudioSessionTrack x, y;
	x.id = x.name = "x";
	y.id = y.name = "y";
	x.routing.bus = y.routing.bus = true;
	y.solo = true;
	x.routing.sends.append({"y"});
	x.effects = {saturation};
	nonlinear.tracks.append(x);
	nonlinear.tracks.append(y);
	const auto mixed = renderAudioSession(nonlinear, 0, 4);
	const double a = double(.1f), wanted = (std::tanh(a * boost) + std::tanh(4 * a * boost)) / std::tanh(boost);
	ok &= expect(mixed.succeeded() && close(mixed.clip.samples[0], wanted, 1e-7),
	             "nonlinear shared bus preserves combined processing without solo bypass leakage");
	auto memory = nonlinear;
	memory.sampleRate = 384000;
	memory.sources[0].audio.clip.sampleRate = 384000;
	for (auto &track : memory.tracks) {
		track.effects.clear();
		for (int i = 0; i < 8; ++i) {
			auto longDelay = makeAudioEffect("delay", 384000);
			longDelay.parameters["leftMs"] = longDelay.parameters["rightMs"] = 2000;
			track.effects.append(longDelay);
		}
	}
	ok &= expect(!validateAudioSessionStructure(memory).isEmpty(),
	             "session rejects delay allocation beyond its shared memory budget");
	auto capacity = sessionFixture();
	capacity.tracks.clear();
	capacity.effectTailSeconds = .05;
	const QStringList capacityTypes{"gain",       "low-pass", "peak-eq", "low-shelf",
	                                "compressor", "gate",     "delay",   "saturation"};
	const auto busId = [](int i) { return QString("bus-%1").arg(i); };
	for (int i = 0; i < 64; ++i) {
		AudioSessionTrack strip;
		strip.id = strip.name = i < 32 ? QString("track-%1").arg(i) : busId(i - 32);
		strip.routing.bus = i >= 32;
		strip.routing.outputId = i < 32 ? busId(i) : i < 63 ? busId(i - 31) : QString();
		strip.solo = i < 32 || i == 63;
		if (i < 32)
			strip.regions.append({QString("clip-%1").arg(i), "Signal", capacity.sources[0].id, 0, 0, 5000});
		for (const auto &type : capacityTypes) {
			auto effect = makeAudioEffect(type, capacity.sampleRate);
			if (type == "peak-eq")
				effect.parameters["gainDb"] = 3;
			if (type == "low-shelf")
				effect.parameters["gainDb"] = -3;
			if (type == "delay") {
				effect.parameters["leftMs"] = 1;
				effect.parameters["rightMs"] = 1.5;
			}
			strip.effects.append(effect);
		}
		capacity.tracks.append(strip);
	}
	for (int i = 0; i < 8; ++i)
		capacity.masterEffects.append(makeAudioEffect("limiter", capacity.sampleRate));
	AudioSessionRenderer capacityRenderer;
	if (!expect(capacityRenderer.prepare(capacity, &error), "prepare maximum strip and effect counts"))
		return EXIT_FAILURE;
	QElapsedTimer elapsed;
	elapsed.start();
	const auto large = capacityRenderer.renderBlock(0, 65536);
	QVector<float> small;
	for (int at = 0; at < 65536; at += 256)
		small += capacityRenderer.renderBlock(at, 256).clip.samples;
	ok &= expect(large.succeeded() && small == large.clip.samples &&
	                 std::all_of(small.cbegin(), small.cend(), [](float value) { return std::isfinite(value); }) &&
	                 std::any_of(small.cbegin(), small.cend(), [](float value) { return value != 0; }) &&
	                 capacityRenderer.scratchSamples(65536) * sizeof(double) == 69ULL * 1024 * 1024,
	             "maximum 64-strip/520-effect solo graph is finite and exact at both block limits");
	std::cout << "effects-capacity strips=64 effects=520 frames=65536 scratchBytes="
	          << capacityRenderer.scratchSamples(65536) * sizeof(double) << " pairedRenderMs=" << elapsed.elapsed()
	          << '\n';
	std::cout << (ok ? "Audio effects verification passed\n" : "Audio effects verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
