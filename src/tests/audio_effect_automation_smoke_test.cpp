#include "core/audio_effects.h"
#include "core/audio_session_io.h"
#include "core/audio_transport.h"
#include "tests/audio_native_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
std::vector<double> signal(int frames)
{
	std::vector<double> result(size_t(frames) * 2);
	for (int i = 0; i < frames; ++i) {
		result[size_t(i) * 2] = .2 * std::sin(i * .11);
		result[size_t(i) * 2 + 1] = .15 * std::cos(i * .03);
	}
	return result;
}
bool close(const std::vector<double> &a, const std::vector<double> &b)
{
	return std::equal(a.begin(), a.end(), b.begin(), b.end(),
	                  [](double x, double y) { return std::abs(x - y) < 1e-12; });
}
QByteArray wire(const QByteArray &bytes, QJsonObject metadata, quint32 version)
{
	const auto size = qFromLittleEndian<quint32>(bytes.constData() + 12);
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	auto next = bytes.first(16);
	qToLittleEndian(version, next.data() + 8);
	qToLittleEndian<quint32>(quint32(json.size()), next.data() + 12);
	next += json + bytes.mid(16 + size, bytes.size() - 48 - size);
	next += QCryptographicHash::hash(next, QCryptographicHash::Sha256);
	return next;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	using Curve = AudioAutomationCurve;
	for (auto curve : {Curve::Linear, Curve::Step, Curve::Smooth}) {
		QVector<AudioAutomationPoint> points{{10, 0, curve}, {18, 1}};
		const double expected = curve == Curve::Step ? 0 : curve == Curve::Smooth ? .15625 : .25;
		ok &= expect(audioAutomationValue(points, 12, 99) == expected && audioAutomationValue(points, 9, 99) == 0 &&
		                 audioAutomationValue(points, 18, 99) == 1 && audioAutomationValue(points, 100, 99) == 1,
		             "curve shape, exact boundary and endpoint hold oracle");
	}
	ok &= expect(audioAutomationValue({}, 12, 7) == 7, "empty lane uses static value");
	int parameters = 0;
	for (const auto &type : audioEffectTypes()) {
		auto effect = makeAudioEffect(type, 48000);
		// Keep EQ active when varying frequency or Q, so constant-lane parity tests its actual transfer function.
		if (effect.parameters.contains("gainDb"))
			effect.parameters["gainDb"] = 6;
		AudioEffectAutomation moving;
		for (const auto &parameter : audioEffectParameters(type, 48000)) {
			if (!parameter.automatable) {
				AudioEffectAutomation structural{{effect.id, parameter.key, false, {{0, parameter.initial}}}};
				ok &= expect(!validateAudioEffectAutomation({effect}, 48000, structural).isEmpty(),
				             "structural latency parameters reject automation even when disabled");
				continue;
			}
			++parameters;
			const double alternative = parameter.minimum + (parameter.maximum - parameter.minimum) * .37;
			auto manual = effect;
			manual.parameters[parameter.key] = alternative;
			AudioEffectAutomation lane{{effect.id, parameter.key, true, {{19, alternative}}}};
			AudioEffectsProcessor a, b;
			auto x = signal(20000), y = x;
			const bool matched = a.prepare({effect}, 48000, &error, lane) && b.prepare({manual}, 48000, &error) &&
			                     a.process(x) && b.process(y) && close(x, y);
			if (!matched)
				std::cerr << type.toStdString() << '/' << parameter.key.toStdString() << ' ' << error.toStdString()
				          << '\n';
			ok &= expect(matched, "constant effect lane matches manually set parameter");
			lane[0].enabled = false;
			x = signal(400);
			y = x;
			ok &= expect(a.prepare({effect}, 48000, &error, lane) && b.prepare({effect}, 48000, &error) &&
			                 a.process(x) && b.process(y) && x == y,
			             "disabled lane preserves exact static processing");
			moving.append(
			    {effect.id,
			     parameter.key,
			     true,
			     {{0, parameter.initial, Curve::Smooth}, {900, alternative, Curve::Step}, {2300, parameter.initial}}});
		}
		AudioEffectsProcessor full;
		auto expected = signal(20000);
		ok &= expect(full.prepare({effect}, 48000, &error, moving) && full.process(expected),
		             "dynamic parameter combination remains finite");
		for (int block : {1, 7, 256, 4096}) {
			AudioEffectsProcessor parts;
			auto actual = signal(20000);
			ok &= parts.prepare({effect}, 48000, &error, moving);
			for (size_t at = 0; at < actual.size(); at += size_t(block) * 2)
				ok &= parts.process(
				    std::span<double>(actual).subspan(at, std::min(size_t(block) * 2, actual.size() - at)),
				    qint64(at / 2));
			ok &= expect(actual == expected, "dynamic curves are bit-identical across block sizes");
			auto seek = signal(200);
			auto fresh = seek;
			AudioEffectsProcessor reference;
			ok &= reference.prepare({effect}, 48000, &error, moving);
			ok &= expect(parts.process(seek, 1000) && reference.process(fresh, 1000) && seek == fresh,
			             "discontinuous range resets history and reads absolute automation time");
		}
	}
	auto gain = makeAudioEffect("gain", 1000);
	AudioEffectAutomation lanes{{gain.id, "gainDb", true, {{0, -20, Curve::Smooth}, {100, 0, Curve::Step}, {200, 6}}}};
	AudioEffectsProcessor processor;
	std::vector<double> values(402, .25);
	ok &= processor.prepare({gain}, 1000, &error, lanes) && processor.process(values);
	for (int frame = 0; frame <= 200; ++frame) {
		const double t = double(std::min(frame, 100)) / 100;
		const double db = frame == 200 ? 6 : frame >= 100 ? 0 : -20 + 20 * t * t * (3 - 2 * t);
		ok &= expect(std::abs(values[size_t(frame) * 2] - .25 * std::pow(10.0, db / 20)) < 1e-14,
		             "independent smooth/step gain amplitude oracle");
	}
	auto delay = makeAudioEffect("delay", 1000);
	delay.parameters = {{"leftMs", 1}, {"rightMs", 1}, {"feedback", 0}, {"mix", 1}};
	AudioEffectAutomation delayLane{{delay.id, "leftMs", true, {{0, 1}, {20, 7}}}};
	auto ramp = signal(50), dry = ramp;
	ok &= processor.prepare({delay}, 1000, &error, delayLane) && processor.process(ramp);
	for (int i = 0; i < 50; ++i) {
		const double time = 1 + 6 * double(std::min(i, 20)) / 20;
		const int whole = int(time);
		const double fraction = time - whole;
		const double a = i >= whole ? dry[size_t(i - whole) * 2] : 0,
		             b = i > whole ? dry[size_t(i - whole - 1) * 2] : 0;
		ok &= expect(std::abs(ramp[size_t(i) * 2] - (a * (1 - fraction) + b * fraction)) < 1e-14,
		             "independent variable fractional delay oracle");
	}
	ok &= expect(audioEffectMemoryBytes({delay}, 1000, delayLane) > audioEffectMemoryBytes({delay}, 1000),
	             "delay reserves largest automated endpoint before processing");
	auto invalid = lanes;
	invalid.append(invalid.first());
	ok &= expect(!validateAudioEffectAutomation({gain}, 1000, invalid).isEmpty(), "duplicate lane rejected");
	invalid = lanes;
	invalid[0].effectId = "missing";
	ok &= expect(!validateAudioEffectAutomation({gain}, 1000, invalid).isEmpty(), "missing effect rejected");
	invalid = lanes;
	invalid[0].parameter = "missing";
	ok &= expect(!validateAudioEffectAutomation({gain}, 1000, invalid).isEmpty(), "unknown parameter rejected");
	invalid = lanes;
	invalid[0].points[1].value = 25;
	ok &= expect(!validateAudioEffectAutomation({gain}, 1000, invalid).isEmpty(), "parameter range enforced");
	invalid = lanes;
	invalid[0].points[1].frame = 0;
	ok &= expect(!validateAudioEffectAutomation({gain}, 1000, invalid).isEmpty(), "duplicate time rejected");
	AudioProject source;
	source.clip = {1, 1000, QVector<float>(250, .125f)};
	AudioSession empty;
	empty.sampleRate = 1000;
	const auto imported = importAudioSessionSource(empty, source);
	if (!expect(imported.succeeded(), "create session fixture"))
		return 1;
	auto session = imported.session;
	session.effectTailSeconds = 0;
	session.tracks[0].effects = {gain};
	session.tracks[0].effectAutomation = lanes;
	session.tracks[0].gainAutomation = {{0, 0, Curve::Smooth}, {100, -3}};
	session.masterEffects = {makeAudioEffect("gain", 1000)};
	session.masterEffectAutomation = {{session.masterEffects[0].id, "gainDb", true, {{0, -1, Curve::Step}, {100, -2}}}};
	const auto bytes = encodeAudioSession(session, &error);
	AudioSession decoded;
	ok &= expect(!bytes.isEmpty() && qFromLittleEndian<quint32>(bytes.constData() + 8) == 7 &&
	                 decodeAudioSession(bytes, &decoded, &error) &&
	                 audioSessionSummary(decoded) == audioSessionSummary(session),
	             "native v7 preserves curves and master/track effect lanes exactly");
	const auto rendered = renderAudioSession(session);
	AudioTransport transport;
	ok &= transport.prepare(session, {0, 250, false}, 256, &error) && transport.play();
	std::array<float, 100> block{};
	transport.process(block);
	transport.pause();
	transport.process(block);
	ok &= transport.play();
	transport.process(block);
	ok &= expect(rendered.succeeded() && std::equal(block.begin(), block.end(), rendered.clip.samples.cbegin() + 100),
	             "pause/resume uses session clock for all automation");
	AudioSessionEdit remove;
	remove.operation = "effects";
	remove.trackId = session.tracks[0].id;
	const auto removed = editAudioSession(session, remove);
	ok &= expect(removed.succeeded() && removed.session.tracks[0].effectAutomation.isEmpty(),
	             "removing an effect prunes its automation");
	remove.effectAutomation = lanes;
	ok &= expect(!editAudioSession(session, remove).succeeded(),
	             "explicit dangling edit is rejected instead of silently pruned");
	auto metadata = QJsonDocument::fromJson(bytes.mid(16, qFromLittleEndian<quint32>(bytes.constData() + 12))).object();
	auto bad = metadata;
	auto effects = bad["effects"].toObject();
	auto saved = effects["automation"].toArray();
	auto lane = saved[0].toObject();
	lane["future"] = true;
	saved[0] = lane;
	effects["automation"] = saved;
	bad["effects"] = effects;
	const auto before = audioSessionSummary(decoded);
	ok &= expect(!decodeAudioSession(wire(bytes, bad, 7), &decoded, &error) && before == audioSessionSummary(decoded),
	             "strict lane schema is transactional");
	// Independently downgrade the envelope, including pre-v4 point layouts.
	metadata = test::withoutAutomationSegments(metadata).toObject();
	metadata.remove("timing");
	metadata.remove("groups");
	auto oldTracks = metadata["tracks"].toArray();
	for (qsizetype i = 0; i < oldTracks.size(); ++i) {
		auto oldTrack = oldTracks[i].toObject();
		auto regions = oldTrack["regions"].toArray();
		for (qsizetype j = 0; j < regions.size(); ++j) {
			auto region = regions[j].toObject();
			for (const auto key : {"groupId", "fadeStart", "fadeSpan"})
				region.remove(key);
			regions[j] = region;
		}
		oldTrack["regions"] = regions;
		oldTracks[i] = oldTrack;
	}
	metadata["tracks"] = oldTracks;
	ok &= expect(decodeAudioSession(wire(bytes, metadata, 4), &decoded, &error) &&
	                 audioSessionSummary(decoded) == audioSessionSummary(session),
	             "v4 automation and routing migrate without losing curves or lanes");
	auto tracks = metadata["tracks"].toArray();
	auto track = tracks[0].toObject();
	track.remove("effectAutomation");
	for (const char *key : {"gainAutomation", "panAutomation"}) {
		auto points = track[key].toArray();
		for (qsizetype i = 0; i < points.size(); ++i) {
			auto point = points[i].toObject();
			point.remove("curve");
			points[i] = point;
		}
		track[key] = points;
	}
	tracks[0] = track;
	metadata["tracks"] = tracks;
	effects = metadata["effects"].toObject();
	effects.remove("automation");
	metadata["effects"] = effects;
	for (quint32 version : {3U, 2U, 1U}) {
		if (version == 2) {
			metadata.remove("effects");
			track.remove("effects");
		}
		if (version == 1)
			track.remove("routing");
		tracks[0] = track;
		metadata["tracks"] = tracks;
		ok &= expect(decodeAudioSession(wire(bytes, metadata, version), &decoded, &error) &&
		                 decoded.tracks[0].gainAutomation[0].curve == Curve::Linear &&
		                 decoded.tracks[0].effectAutomation.isEmpty() && decoded.masterEffectAutomation.isEmpty(),
		             "v1/v2/v3 sessions migrate linear curves and empty effect lanes");
	}
	ok &= transport.prepare(session, {40, 110, true}, 256, &error) && transport.play();
	std::array<float, 400> looped{};
	transport.process(looped);
	for (size_t i = 0; i < looped.size(); ++i)
		ok &= expect(looped[i] == rendered.clip.samples[qsizetype((40 + (i / 2) % 70) * 2 + i % 2)],
		             "loop reads the absolute automation clock at each restart");
	auto routed = session;
	routed.masterEffects.clear();
	routed.masterEffectAutomation.clear();
	routed.tracks[0].effects.clear();
	routed.tracks[0].effectAutomation.clear();
	routed.tracks[0].gainAutomation.clear();
	auto second = routed.tracks[0];
	second.id = "second";
	second.regions[0].id = "second-region";
	second.routing.outputId = "bus";
	routed.tracks.append(second);
	routed.tracks[0].routing.sends = {{"bus", 0, 0, false, true}};
	AudioSessionTrack bus;
	bus.id = bus.name = "bus";
	bus.routing.bus = true;
	bus.solo = true;
	bus.effects = {makeAudioEffect("saturation", 1000)};
	bus.effectAutomation = {{bus.effects[0].id, "driveDb", true, {{0, 0, Curve::Smooth}, {249, 18}}}};
	routed.tracks.append(bus);
	const auto soloed = renderAudioSession(routed);
	routed.tracks[2].solo = false;
	routed.tracks[0].routing.outputEnabled = false;
	const auto withoutBypass = renderAudioSession(routed);
	ok &= expect(soloed.succeeded() && withoutBypass.succeeded() && soloed.clip.samples == withoutBypass.clip.samples,
	             "automated nonlinear solo bus excludes the parallel dry route");
	for (int rate : {1, 8000, 384000}) {
		auto reverb = makeAudioEffect("reverb", rate);
		reverb.parameters["roomSize"] = .25;
		reverb.parameters["preDelayMs"] = 0;
		AudioEffectAutomation wide{{reverb.id, "roomSize", true, {{0, .25}, {37, 2}}},
		                           {reverb.id, "preDelayMs", true, {{0, 0}, {37, 250}}}};
		auto maximum = reverb;
		maximum.parameters["roomSize"] = 2;
		maximum.parameters["preDelayMs"] = 250;
		ok &= expect(audioEffectMemoryBytes({reverb}, rate, wide) >= audioEffectMemoryBytes({maximum}, rate),
		             "reverb reserves maximum automated room and predelay taps");
		auto samples = signal(1000);
		ok &= expect(processor.prepare({reverb}, rate, &error, wide) && processor.process(samples),
		             "extreme variable reverb taps remain bounded at minimum and maximum sample rates");
	}
	auto capacity = session;
	capacity.masterEffects.clear();
	capacity.masterEffectAutomation.clear();
	capacity.tracks[0].effects.clear();
	capacity.tracks[0].effectAutomation.clear();
	QVector<AudioAutomationPoint> dense;
	for (int i = 0; i < 4096; ++i)
		dense.append({i, 0, Curve::Smooth});
	for (int i = 0; i < 8; ++i) {
		auto fx = makeAudioEffect("gain", 1000);
		capacity.masterEffects.append(fx);
		capacity.masterEffectAutomation.append({fx.id, "gainDb", false, dense});
		fx = makeAudioEffect("gain", 1000);
		capacity.tracks[0].effects.append(fx);
		capacity.tracks[0].effectAutomation.append({fx.id, "gainDb", true, dense});
	}
	const auto capacityBytes = encodeAudioSession(capacity, &error);
	ok &= expect(!capacityBytes.isEmpty() && decodeAudioSession(capacityBytes, &decoded, &error) &&
	                 audioSessionSummary(capacity) == audioSessionSummary(decoded),
	             "all 65536 effect points round-trip at the session limit");
	AudioSessionTrack extra;
	extra.id = extra.name = "over-limit";
	extra.effects = {makeAudioEffect("gain", 1000)};
	extra.effectAutomation = {{extra.effects[0].id, "gainDb", false, {{0, 0}}}};
	capacity.tracks.append(extra);
	ok &= expect(!validateAudioSessionStructure(capacity).isEmpty(),
	             "aggregate point bound includes disabled lanes on other strips");
	std::cout << "Verified " << parameters << " numeric parameters across " << audioEffectTypes().size()
	          << " processors.\n";
	return ok ? 0 : 1;
}
