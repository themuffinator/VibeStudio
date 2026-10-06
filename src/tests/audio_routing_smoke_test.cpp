#include "core/audio_session_io.h"
#include "core/audio_transport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>
#include <algorithm>
#include <array>
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
	AudioSession s;
	for (int i = 0; i < 2; ++i) {
		AudioSessionSource source;
		source.id = i ? "b-source" : "a-source";
		source.audio.clip = {2, 48000,
		                     i ? QVector<float>{1, -.5f, .5f, 2, -.25f, 1, .25f, .75f}
		                       : QVector<float>{.25f, .5f, .5f, -.25f, -.5f, .75f, 1, -1}};
		source.audio.endFrame = 4;
		s.sources.append(source);
		AudioSessionTrack t;
		t.id = t.name = i ? "b" : "a";
		t.regions.append({t.id + "-clip", "Clip", source.id, 0, 0, 4});
		t.routing.outputId = "x";
		s.tracks.append(t);
	}
	for (const auto &id : {QStringLiteral("x"), QStringLiteral("y")}) {
		AudioSessionTrack bus;
		bus.id = bus.name = id;
		bus.routing.bus = true;
		s.tracks.append(bus);
	}
	return s;
}
bool samplesNear(const AudioClipResult &actual, const QVector<float> &expected)
{
	if (!actual.succeeded() || actual.clip.samples.size() != expected.size())
		return false;
	for (qsizetype i = 0; i < expected.size(); ++i)
		if (std::abs(actual.clip.samples[i] - expected[i]) > 1e-6f)
			return false;
	return true;
}
QByteArray wire(QJsonObject metadata, const QByteArray &payload, quint32 version)
{
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	QByteArray bytes = QByteArray::fromHex("56534d49580d0a1a0000000000000000");
	qToLittleEndian(version, bytes.data() + 8);
	qToLittleEndian(quint32(json.size()), bytes.data() + 12);
	bytes += json;
	bytes += payload;
	bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return bytes;
}
bool capacity()
{
	constexpr int frames = 65536, audioTracks = 32, buses = 32;
	AudioSession s;
	AudioSessionSource source;
	source.id = "capacity-source";
	source.audio.clip = {2, s.sampleRate, QVector<float>(frames * 2)};
	source.audio.endFrame = frames;
	for (int frame = 0; frame < frames; ++frame) {
		source.audio.clip.samples[frame * 2] = float(.0001 * (1 + frame % 13));
		source.audio.clip.samples[frame * 2 + 1] = float(-.0001 * (1 + frame % 7));
	}
	s.sources.append(source);
	const auto busId = [](int index) { return QStringLiteral("bus-%1").arg(index); };
	const double sendDb = 20 * std::log10(1.0 / 8), busDb = 20 * std::log10(.5);
	for (int i = 0; i < audioTracks + buses; ++i) {
		AudioSessionTrack track;
		track.id = track.name = i < audioTracks ? QStringLiteral("track-%1").arg(i) : busId(i - audioTracks);
		for (int point = 0; point < 256; ++point)
			track.gainAutomation.append({point * 256, 0});
		track.routing.bus = i >= audioTracks;
		if (!track.routing.bus) {
			track.routing.outputId = busId(0);
			for (int clip = 0; clip < 128; ++clip)
				track.regions.append(
				    {QStringLiteral("clip-%1-%2").arg(i).arg(clip), "Clip", source.id, clip * 512, clip * 512, 512});
			for (int target = 1; target <= 8; ++target)
				track.routing.sends.append({busId(target), sendDb});
		} else {
			const int index = i - audioTracks;
			track.gainDb = busDb;
			track.routing.outputId = index + 1 < buses ? busId(index + 1) : QString();
			for (int step = 2; step <= 9; ++step) {
				track.routing.sends.append({index + step < buses ? busId(index + step) : QString(), sendDb});
				if (index + step >= buses)
					break;
			}
		}
		s.tracks.append(track);
	}
	// Independent scalar recurrence: each bus receives the preceding bus at
	// one half and buses two through nine places back at one sixteenth.
	// The first nine buses also receive the known 32 identical audio tracks.
	std::array<double, buses> coefficient{};
	for (int i = 0; i < buses; ++i) {
		coefficient[i] = i == 0 ? audioTracks : i <= 8 ? audioTracks / 8.0 : 0;
		if (i)
			coefficient[i] += coefficient[i - 1] / 2;
		for (int step = 2; step <= 9 && step <= i; ++step)
			coefficient[i] += coefficient[i - step] / 16;
	}
	double master = coefficient.back() / 2;
	for (int i = buses - 9; i < buses; ++i)
		master += coefficient[i] / 16;
	bool ok = true;
	QString error;
	for (bool solo : {false, true}) {
		s.tracks.last().solo = solo;
		AudioSessionRenderer renderer;
		if (!expect(renderer.prepare(s, &error), "maximum routing graph prepares"))
			return false;
		const size_t expectedScratch = size_t(frames) * 2 * (3 + buses * (solo ? 2 : 1));
		ok &= expect(renderer.scratchSamples(frames) == expectedScratch && renderer.scratchSamples(65537) == 0,
		             "maximum routing graph has a bounded documented scratch requirement");
		std::vector<double> scratch(expectedScratch);
		QVector<float> reference(frames * 2);
		QElapsedTimer elapsed;
		elapsed.start();
		if (!expect(renderer.renderInto(0, {reference.data(), size_t(reference.size())}, scratch) ==
		                AudioSessionRenderer::BlockStatus::Ready,
		            "maximum graph renders in one bounded block"))
			return false;
		const double factor = solo ? coefficient.back() * 9 / 16 : master;
		double maximumError = 0;
		for (int i = 0; i < reference.size(); ++i)
			maximumError =
			    std::max(maximumError, std::abs(double(reference[i]) - source.audio.clip.samples[i] * factor));
		ok &= expect(maximumError < 1e-7, "maximum graph agrees with independent scalar recurrence");
		std::array<float, 512> block{};
		bool exact = true;
		for (int first = 0; first < frames; first += 256) {
			exact &= renderer.renderInto(first, block, scratch) == AudioSessionRenderer::BlockStatus::Ready;
			for (int i = 0; i < 512; ++i)
				exact &= block[i] == reference[first * 2 + i];
		}
		ok &= expect(exact, "maximum routed graph is sample-exact at 256 and 65536 frame block sizes");
		std::cout << "routing-capacity solo=" << solo
		          << " tracks=64 buses=32 clips=4096 gainPoints=16384 frames=" << frames
		          << " scratchBytes=" << scratch.size() * sizeof(double) << " maximumError=" << maximumError
		          << " twoRendersMs=" << elapsed.elapsed() << '\n';
	}
	auto oversized = s;
	oversized.tracks[0].routing.sends.append({QString()});
	ok &= expect(!validateAudioSessionStructure(oversized).isEmpty(), "ninth send is rejected");
	oversized = s;
	oversized.tracks[0].regions.clear();
	oversized.tracks[0].routing.bus = true;
	ok &= expect(!validateAudioSessionStructure(oversized).isEmpty(), "thirty-third bus is rejected");
	oversized = s;
	AudioSessionTrack extra;
	extra.id = extra.name = "extra";
	oversized.tracks.append(extra);
	ok &= expect(!validateAudioSessionStructure(oversized).isEmpty(), "sixty-fifth strip is rejected");
	// Finite, valid gain stages can still overflow float output after a long
	// bus chain. Report overflow and clear every sample, including earlier ones.
	for (auto &track : s.tracks) {
		track.routing.sends.clear();
		if (track.routing.bus)
			track.gainDb = 24;
	}
	s.sources[0].audio.clip.samples.fill(1);
	std::fill_n(s.sources[0].audio.clip.samples.begin(), 4, 1e-20f);
	AudioSessionRenderer overflowing;
	ok &= expect(overflowing.prepare(s, &error), "finite high-gain chain is structurally valid");
	std::array<float, 8> output{42, 42, 42, 42, 42, 42, 42, 42};
	std::vector<double> scratch(overflowing.scratchSamples(4));
	ok &= expect(overflowing.renderInto(0, output, scratch) == AudioSessionRenderer::BlockStatus::Overflow &&
	                 std::all_of(output.begin(), output.end(), [](float value) { return value == 0; }),
	             "long-chain overflow clears the complete output instead of exposing partial audio");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	auto s = fixture();
	s.tracks[0].gainDb = 20 * std::log10(.5);
	s.tracks[1].gainDb = 20 * std::log10(.25);
	s.tracks[2].gainDb = 20 * std::log10(2.0);
	s.tracks[0].routing.sends.append({"y", 20 * std::log10(.25), 0, true, true});
	s.tracks[3].routing.invertRight = true;
	const auto a = s.sources[0].audio.clip.samples, b = s.sources[1].audio.clip.samples;
	QVector<float> expected(8);
	for (int i = 0; i < 8; ++i)
		expected[i] = float((i % 2 ? .75 : 1.25) * a[i] + .5 * b[i]);
	ok &= expect(samplesNear(renderAudioSession(s), expected),
	             "independent subgroup, pre-fader send and polarity oracle");
	s.tracks[0].routing.sends[0].preFader = false;
	for (int i = 0; i < 8; ++i)
		expected[i] = float((i % 2 ? .875 : 1.125) * a[i] + .5 * b[i]);
	ok &= expect(samplesNear(renderAudioSession(s), expected), "post-fader sends include source trim");
	s.tracks[0].routing.outputEnabled = false;
	s.tracks[0].routing.sends[0].enabled = false;
	for (int i = 0; i < 8; ++i)
		expected[i] = .5f * b[i];
	ok &= expect(samplesNear(renderAudioSession(s), expected), "disabled routes contribute no samples");
	s = fixture();
	s.tracks[0].routing.swapChannels = true;
	s.tracks[0].routing.invertLeft = true;
	s.tracks[1].muted = true;
	for (int i = 0; i < 8; i += 2) {
		expected[i] = -a[i + 1];
		expected[i + 1] = a[i];
	}
	ok &= expect(samplesNear(renderAudioSession(s), expected), "swap precedes output channel polarity");
	s = fixture();
	s.tracks[1].muted = true;
	s.tracks[2].gainAutomation = {{0, 0}, {3, 20 * std::log10(.5)}};
	s.tracks[2].panAutomation = {{0, -1}, {3, 1}};
	for (int frame = 0; frame < 4; ++frame) {
		const double pan = -1 + 2.0 * frame / 3;
		const double gain = std::pow(.5, double(frame) / 3);
		expected[frame * 2] = float(a[frame * 2] * gain *
		                            (pan >= 1   ? 0
		                             : pan <= 0 ? 1
		                                        : std::cos(pan * std::acos(-1.0) / 2)));
		expected[frame * 2 + 1] = float(a[frame * 2 + 1] * gain *
		                                (pan <= -1  ? 0
		                                 : pan >= 0 ? 1
		                                            : std::cos(pan * std::acos(-1.0) / 2)));
	}
	ok &= expect(samplesNear(renderAudioSession(s), expected),
	             "bus gain and balance automation follow an independent per-frame oracle");
	s = fixture();
	s.tracks[1].muted = true;
	s.sources[0].audio.clip = {1, 48000, {1, .5f, -.25f, -1}};
	s.tracks[0].gainDb = 20 * std::log10(.5);
	s.tracks[0].pan = -1;
	s.tracks[0].routing.outputEnabled = false;
	s.tracks[0].routing.sends = {{QString(), 0, 0, true, true}};
	for (int frame = 0; frame < 4; ++frame)
		expected[frame * 2] = expected[frame * 2 + 1] = float(s.sources[0].audio.clip.samples[frame] / std::sqrt(2.0));
	ok &= expect(samplesNear(renderAudioSession(s), expected),
	             "mono pre-fader sends retain centre law and bypass strip gain and pan");
	s.tracks[0].routing.sends[0].preFader = false;
	for (int frame = 0; frame < 4; ++frame) {
		expected[frame * 2] = s.sources[0].audio.clip.samples[frame] * .5f;
		expected[frame * 2 + 1] = 0;
	}
	ok &= expect(samplesNear(renderAudioSession(s), expected), "mono post-fader sends retain equal-power strip pan");

	// X is downstream of solo A and upstream of solo Y. B may reach Y but
	// cannot leak through X's parallel master output before passing solo Y.
	s = fixture();
	s.tracks[0].solo = true;
	s.tracks[3].solo = true;
	s.tracks[2].routing.sends.append({"y"});
	for (int i = 0; i < 8; ++i)
		expected[i] = 2 * a[i] + b[i];
	ok &= expect(samplesNear(renderAudioSession(s), expected), "solo isolates parallel paths through a shared bus");
	s.tracks[0].solo = false;
	for (int i = 0; i < 8; ++i)
		expected[i] = a[i] + b[i];
	ok &=
	    expect(samplesNear(renderAudioSession(s), expected), "bus solo retains ancestors without their bypass outputs");
	s.tracks[3].muted = true;
	ok &=
	    expect(samplesNear(renderAudioSession(s), QVector<float>(8, 0)), "mute wins over bus solo and pre-fader sends");
	s.tracks[3].muted = false;
	s.tracks[0].solo = true;
	s.tracks[3].solo = false;
	for (int i = 0; i < 8; ++i)
		expected[i] = 2 * a[i];
	ok &= expect(samplesNear(renderAudioSession(s), expected),
	             "track solo retains downstream buses while excluding unrelated sources");
	std::swap(s.tracks[0], s.tracks[3]);
	ok &= expect(samplesNear(renderAudioSession(s), expected), "routing is independent of strip display order");

	AudioSessionRenderer renderer;
	ok &= expect(renderer.prepare(s, &error), "prepare routed renderer");
	const auto reference = renderer.renderBlock(0, 4);
	for (int block : {1, 3, 4, 256}) {
		AudioTransport clock;
		ok &= expect(clock.prepare(s, {0, 4, true}, block, &error) && clock.blockFrames() == block && clock.play(),
		             "transport allocates routing scratch without changing block frames");
		std::array<float, 26> out{};
		const auto rendered = clock.process(out);
		bool exact = rendered.frames == 13;
		for (size_t i = 0; i < out.size(); ++i)
			exact &= out[i] == reference.clip.samples[qsizetype(i % 8)];
		ok &= expect(exact, "routed playback matches offline samples across loops and block sizes");
	}
	std::array<float, 8> out;
	out.fill(42);
	std::vector<double> scratch(renderer.scratchSamples(4));
	ok &= expect(renderer.renderInto(0, out, std::span<double>(scratch).first(scratch.size() - 1)) ==
	                     AudioSessionRenderer::BlockStatus::InvalidRange &&
	                 out[0] == 0,
	             "routed scratch bound is checked");
	int checks = 0;
	ok &= expect(renderer.renderInto(0, out, scratch, {[&] { return ++checks > 3; }}) ==
	                     AudioSessionRenderer::BlockStatus::Cancelled &&
	                 std::all_of(out.begin(), out.end(), [](float v) { return v == 0; }),
	             "cancelled route processing clears the whole output");
	for (int malformed = 0; malformed < 6; ++malformed) {
		auto bad = fixture();
		if (malformed == 0)
			bad.tracks[2].routing.outputId = "x";
		if (malformed == 1) {
			bad.tracks[2].routing.outputId = "y";
			bad.tracks[3].routing.sends.append({"x", 0, 0, false, false});
		}
		if (malformed == 2)
			bad.tracks[0].routing.outputId = "b";
		if (malformed == 3)
			bad.tracks[0].routing.sends = {{"y"}, {"y"}};
		if (malformed == 4)
			bad.tracks[2].regions = bad.tracks[0].regions;
		if (malformed == 5)
			bad.tracks[0].routing.sends = {{"y", std::numeric_limits<double>::infinity()}};
		ok &= expect(!validateAudioSessionStructure(bad).isEmpty(),
		             "invalid, cyclic, duplicate or non-finite routing is rejected");
	}
	AudioSessionEdit edit;
	edit.operation = "remove-track";
	edit.trackId = "x";
	ok &=
	    expect(!editAudioSession(fixture(), edit).succeeded(), "referenced bus removal cannot silently reroute audio");
	edit.operation = "add-bus";
	edit.name = "New bus";
	const auto added = editAudioSession(fixture(), edit);
	ok &=
	    expect(added.succeeded() && added.session.tracks.last().routing.bus, "bus edit uses shared session validation");
	ok &= expect(!importAudioSessionSource(fixture(), fixture().sources[0].audio, "x").succeeded(),
	             "buses reject clip import");
	const auto bytes = encodeAudioSession(s, &error);
	AudioSession decoded;
	ok &= expect(!bytes.isEmpty() && qFromLittleEndian<quint32>(bytes.constData() + 8) == 7 &&
	                 decodeAudioSession(bytes, &decoded, &error) &&
	                 audioSessionSummary(decoded) == audioSessionSummary(s),
	             "version seven preserves complete routing and automation state");
	const auto metadataBytes = qFromLittleEndian<quint32>(bytes.constData() + 12);
	auto metadata = QJsonDocument::fromJson(bytes.mid(16, metadataBytes)).object();
	const auto payload = bytes.mid(16 + metadataBytes, bytes.size() - 48 - metadataBytes);
	auto tracks = metadata.value("tracks").toArray();
	metadata.remove("effects");
	metadata.remove("timing");
	metadata.remove("groups");
	for (qsizetype i = 0; i < tracks.size(); ++i) {
		auto track = tracks[i].toObject();
		auto regions = track["regions"].toArray();
		for (qsizetype j = 0; j < regions.size(); ++j) {
			auto region = regions[j].toObject();
			for (const auto key : {"groupId", "fadeStart", "fadeSpan"})
				region.remove(key);
			regions[j] = region;
		}
		track["regions"] = regions;
		track.remove("effects");
		track.remove("effectAutomation");
		for (const char *key : {"gainAutomation", "panAutomation"}) {
			auto points = track[key].toArray();
			for (qsizetype j = 0; j < points.size(); ++j) {
				auto point = points[j].toObject();
				point.remove("curve");
				point.remove("segment");
				points[j] = point;
			}
			track[key] = points;
		}
		tracks[i] = track;
	}
	metadata.insert("tracks", tracks);
	ok &= expect(decodeAudioSession(wire(metadata, payload, 2), &decoded, &error) &&
	                 audioSessionSummary(decoded) == audioSessionSummary(s),
	             "version two routing migrates with empty effects");
	for (qsizetype i = 0; i < tracks.size(); ++i) {
		auto track = tracks[i].toObject();
		track.remove("routing");
		tracks[i] = track;
	}
	metadata.insert("tracks", tracks);
	ok &= expect(decodeAudioSession(wire(metadata, payload, 1), &decoded, &error) &&
	                 std::all_of(decoded.tracks.cbegin(), decoded.tracks.cend(),
	                             [](const auto &track) { return track.routing == AudioTrackRouting{}; }),
	             "legacy version one migrates to direct-master audio tracks");
	ok &= expect(!decodeAudioSession(wire(metadata, payload, 2), &decoded, &error),
	             "version two cannot silently default missing routing");
	ok &= capacity();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
