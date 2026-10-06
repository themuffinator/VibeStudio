#include "core/audio_meter.h"
#include "core/audio_session.h"
#include "core/audio_transport.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool near(double a, double b, double tolerance = 1e-10) { return std::abs(a - b) <= tolerance; }
AudioSession fixture()
{
	AudioSession s;
	s.sampleRate = 1000;
	s.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "source";
	source.audio.clip = {2, 1000, QVector<float>(1034)};
	source.audio.endFrame = 517;
	for (int i = 0; i < 517; ++i) {
		source.audio.clip.samples[i * 2] = i == 3 ? 1.25f : float(.125 * std::sin(i * .1));
		source.audio.clip.samples[i * 2 + 1] = -source.audio.clip.samples[i * 2] * .5f;
	}
	s.sources = {source};
	AudioSessionTrack track;
	track.id = track.name = "a";
	track.regions = {{"clip", "clip", "source", 0, 0, 517}};
	s.tracks = {track};
	return s;
}
AudioEffect lookahead(int frames)
{
	auto effect = makeAudioEffect("lookahead-limiter", 1000);
	effect.parameters["lookaheadMs"] = frames;
	effect.parameters["ceilingDb"] = 0;
	return effect;
}
bool scalar()
{
	AudioMeterProcessor meter;
	meter.prepare(1000);
	for (int i = 0; i < 300; ++i)
		meter.tick(.5, -.25, i);
	auto r = meter.reading();
	bool ok = expect(r.frames == 300 && r.lastFrame == 299 && near(r.integratedRms[0], .5) &&
	                     near(r.rms[0], .5 * std::sqrt(1 - std::exp(-1.0))) && near(r.correlation, -1) &&
	                     near(r.integratedCorrelation, -1),
	                 "analytical RMS and anti-phase correlation");
	for (int i = 0; i < 1000; ++i)
		meter.tick(0, 0, i + 300);
	r = meter.reading();
	ok &= expect(near(r.peak[0], .5 * std::pow(10.0, -24.0 / 20)) && r.maximum[0] == .5 &&
	                 near(r.integratedRms[0], .5 * std::sqrt(300.0 / 1300)),
	             "sample-clock decay and persistent held peak");
	meter.reset();
	meter.tick(1, -1, 0);
	meter.tick(1.25, -2, 1);
	ok &= expect(meter.reading().samplesAboveFullScale == std::array<quint64, 2>{1, 1},
	             "strict over-full-scale boundary");
	meter.reset();
	meter.tick(1e300, -5e299, 17);
	r = meter.reading();
	ok &= expect(std::isfinite(r.rms[0]) && r.integratedRms[0] == 1e300 && near(r.correlation, -1),
	             "large finite bus amplitudes do not overflow squared-energy meters");
	meter.tick(std::numeric_limits<double>::infinity(), 0, 18);
	ok &= expect(!meter.reading().valid, "nonfinite taps cannot become valid readings");
	meter.reset();
	meter.tick(1, 0, 0);
	ok &= expect(!meter.reading().correlationValid && !meter.reading().integratedCorrelationValid,
	             "one silent channel has undefined correlation");
	return ok;
}
bool compareOracle(const AudioSession &s, const AudioMeterReport &report)
{
	bool ok = expect(report.succeeded(), report.error.toUtf8().constData());
	if (!report.succeeded())
		return false;
	QString error;
	for (int index = 0; index <= s.tracks.size(); ++index)
		for (bool post : {false, true}) {
			if (index == s.tracks.size() && !post)
				continue;
			AudioSessionRenderer oracle;
			AudioSessionRenderTarget target;
			if (index < s.tracks.size()) {
				target.stripId = s.tracks[index].id;
				target.tap = post ? AudioSessionRenderTarget::Tap::PostFader : AudioSessionRenderTarget::Tap::PreFader;
			}
			if (!oracle.prepare(s, &error, {}, target))
				return expect(false, error.toUtf8().constData());
			const auto clip = oracle.renderBlock(report.first, int(report.end - report.first));
			ok &= expect(clip.succeeded(), "independent strip render succeeds");
			if (!clip.succeeded())
				continue;
			std::array<double, 2> sum{}, peak{};
			std::array<quint64, 2> overs{};
			double cross = 0;
			for (qsizetype i = 0; i < clip.clip.samples.size(); i += 2) {
				for (size_t c = 0; c < 2; ++c) {
					const double value = clip.clip.samples[i + qsizetype(c)];
					sum[c] += value * value;
					peak[c] = std::max(peak[c], std::abs(value));
					overs[c] += std::abs(value) > 1;
				}
				cross += double(clip.clip.samples[i]) * clip.clip.samples[i + 1];
			}
			const auto &tap = post ? report.meters.strips[size_t(index)].post : report.meters.strips[size_t(index)].pre;
			for (size_t c = 0; c < 2; ++c)
				ok &= expect(
				    near(tap.maximum[c], peak[c], 1e-6) &&
				        near(tap.integratedRms[c], std::sqrt(sum[c] / double(report.end - report.first)), 1e-6) &&
				        tap.samplesAboveFullScale[c] == overs[c],
				    "meter equals independently rendered tap peak, energy and overs");
			if (sum[0] > 0 && sum[1] > 0)
				ok &= expect(near(tap.integratedCorrelation, cross / std::sqrt(sum[0] * sum[1]), 1e-6),
				             "integrated phase matches scalar dot product");
		}
	return ok;
}
bool rendering()
{
	bool ok = true;
	for (int variant = 0; variant < 5; ++variant) {
		auto s = fixture();
		s.tracks[0].gainDb = -3;
		s.masterGainDb = -2;
		if (variant == 1)
			s.tracks[0].muted = true;
		if (variant >= 2) {
			AudioSessionTrack bus;
			bus.id = bus.name = "bus";
			bus.routing.bus = true;
			bus.gainDb = 2;
			s.tracks[0].routing.outputId = bus.id;
			s.tracks[0].routing.sends = {{QString(), -8, .25, true, true}};
			s.tracks.append(bus);
		}
		if (variant >= 3) {
			s.tracks[0].effects = {lookahead(7)};
			s.tracks[1].effects = {lookahead(11)};
			s.masterEffects = {lookahead(13)};
		}
		if (variant == 4) {
			s.tracks[0].routing.outputEnabled = false;
			s.tracks[0].routing.sends.clear();
			s.tracks[0].effects = {lookahead(20), lookahead(20), lookahead(20)};
		}
		QJsonObject previous;
		for (int block : {1, 7, 128, 4096}) {
			const auto report = measureAudioSessionMeters(s, 3, 517, block);
			ok &= compareOracle(s, report);
			const auto json = audioMeterReportToJson(s, report);
			ok &= expect(previous.isEmpty() || previous == json, "meter results independent of render block partition");
			previous = json;
		}
		AudioSessionRenderer plain, metered;
		QString error;
		ok &= plain.prepare(s, &error) && metered.prepare(s, &error, {}, {}, {true, 0, 517});
		for (int at = 0; at < 517; at += 47)
			ok &= expect(plain.renderBlock(at, std::min(47, 517 - at)).clip.samples ==
			                 metered.renderBlock(at, std::min(47, 517 - at)).clip.samples,
			             "enabling meters leaves every rendered float unchanged");
	}
	// A solo-selected downstream bus receives F(A+B), including the nonlinear
	// residual of its upstream bus. Meter the combined signal once per frame.
	auto s = fixture();
	auto second = s.tracks[0];
	second.id = "b";
	second.regions[0].id = "second";
	second.solo = true;
	s.tracks[0].routing.outputId = second.routing.outputId = "upstream";
	s.tracks.append(second);
	AudioSessionTrack up, down;
	up.id = up.name = "upstream";
	up.routing.bus = true;
	up.routing.outputId = "downstream";
	up.effects = {makeAudioEffect("limiter", 1000)};
	down.id = down.name = "downstream";
	down.routing.bus = down.solo = true;
	s.tracks.append(up);
	s.tracks.append(down);
	ok &= compareOracle(s, measureAudioSessionMeters(s, 3, 517, 7));
	auto report = measureAudioSessionMeters(s, 0, 517, 7, {[] { return true; }});
	ok &= expect(report.cancelled && !report.succeeded() && report.meters.count == 0,
	             "cancellation returns no partial meters");
	report = measureAudioSessionMeters(s, 0, 0);
	ok &= expect(!report.succeeded(), "empty analysis rejected");
	return ok;
}
bool transport()
{
	auto s = fixture();
	s.tracks[0].effects = {lookahead(7)};
	AudioTransport engine;
	QString error;
	bool ok = engine.prepare(s, {3, 20, true}, 7, &error, {}, true) && engine.play();
	std::array<float, 102> samples{};
	ok &= expect(engine.process(samples).frames == 51 && engine.meters().strips[0].pre.frames == 58 &&
	                 engine.meters().strips[0].post.frames == 51,
	             "continuous loops count each signal frame once; input tap leads compensated output by seven frames");
	engine.pause();
	const auto held = engine.meters().strips[0].post.frames;
	engine.process(samples);
	ok &= expect(engine.meters().strips[0].post.frames == held, "pause retains meter history");
	engine.seek(10);
	engine.play();
	engine.setLoop(false);
	engine.process(samples);
	ok &= expect(engine.meters().strips[0].pre.frames == 10 && engine.meters().strips[0].post.frames == 10,
	             "seek resets history and excludes warmup before the seek origin");
	engine.resetMetering();
	ok &= expect(engine.position() == 20 && engine.meters().strips[0].post.frames == 0,
	             "meter reset does not seek transport");
	engine.stop();
	ok &= expect(engine.meters().strips[0].pre.frames == 0, "stop resets meters");
	return ok;
}
bool boundaries()
{
	auto s = fixture();
	s.sources[0].audio.clip = {1, 1000, {1, -1, 0, .5f}};
	s.sources[0].audio.endFrame = 4;
	s.tracks[0].regions[0].length = 4;
	s.tracks[0].pan = 1;
	s.tracks[0].gainDb = -6;
	s.masterGainDb = -6;
	auto report = measureAudioSessionMeters(s, 0, 4);
	const double trim = std::pow(10.0, -6.0 / 20);
	bool ok = expect(report.succeeded() && near(report.meters.strips[0].pre.maximum[0], std::sqrt(.5)) &&
	                     report.meters.strips[0].post.maximum[0] == 0 &&
	                     near(report.meters.strips[0].post.maximum[1], trim) &&
	                     near(report.meters.strips[1].pre.maximum[1], trim) &&
	                     near(report.meters.strips[1].post.maximum[1], trim * trim, 1e-7),
	                 "mono centre pre tap, hard pan and separate strip/master gains follow signal flow");
	s = {};
	s.effectTailSeconds = 0;
	for (int i = 0; i < AudioSessionTrackLimit; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		track.routing.bus = i >= 32;
		s.tracks.append(track);
	}
	report = measureAudioSessionMeters(s, AudioSessionFrameLimit - 17, AudioSessionFrameLimit, 3);
	ok &= expect(report.succeeded() && report.meters.count == AudioMeterStripLimit &&
	                 report.meters.strips.back().pre.frames == 17 && !report.meters.strips.back().post.correlationValid,
	             "all 64 strips plus master have bounded silent readings at the timeline limit");
	const auto channel = audioMeterReportToJson(s, report)["strips"]
	                         .toArray()
	                         .last()
	                         .toObject()["post"]
	                         .toObject()["channels"]
	                         .toArray()
	                         .first()
	                         .toObject();
	ok &= expect(channel["rmsDbfs"].isNull() && channel["integratedRms"].toDouble() == 0,
	             "JSON represents silence without nonfinite numbers");
	s = fixture();
	bool cancel = false;
	report = measureAudioSessionMeters(s, 0, 517, 7, {[&] { return cancel; }}, [&](qint64, qint64) { cancel = true; });
	ok &= expect(report.cancelled && report.meters.count == 0,
	             "cancellation after an actual rendered block discards partial stats");
	s.sources[0].audio.clip.samples[4] = std::numeric_limits<float>::max();
	s.tracks[0].gainDb = 24;
	QString error;
	AudioSessionRenderer renderer;
	ok &= renderer.prepare(s, &error, {}, {}, {true, 0, 517});
	ok &= expect(!renderer.renderBlock(0, 7).succeeded() && renderer.meters().strips[0].pre.frames == 0,
	             "render overflow clears partially accumulated meter history");
	report = measureAudioSessionMeters(s, 0, 517);
	ok &= expect(!report.succeeded() && report.meters.count == 0, "overflow analysis cannot publish partial stats");
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	return scalar() && rendering() && transport() && boundaries() ? EXIT_SUCCESS : EXIT_FAILURE;
}
