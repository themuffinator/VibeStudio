#include "core/audio_stems.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>
using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value)
		std::cerr << text << '\n';
	return value;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray data(const QByteArray &bytes)
{
	for (int at = 12; at + 8 <= bytes.size();) {
		const auto size = qFromLittleEndian<quint32>(bytes.constData() + at + 4);
		if (bytes.mid(at, 4) == "data")
			return bytes.mid(at + 8, size);
		at += 8 + int(size) + int(size & 1);
	}
	return {};
}
AudioSession fixture()
{
	AudioSession s;
	s.name = "Delivery";
	s.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "source";
	source.audio.clip = {2, 48000, {.1f, -.2f, .2f, .4f, -.3f, .1f, .4f, -.4f}};
	source.audio.endFrame = 4;
	s.sources.append(source);
	for (int i = 0; i < 2; ++i) {
		AudioSessionTrack track;
		track.id = track.name = i ? "b" : "a";
		track.regions.append({track.id + "-clip", "Clip", "source", 0, 0, 4});
		track.gainDb = 20 * std::log10(i ? .25 : .5);
		track.routing.outputId = "bus";
		track.effects = {makeAudioEffect("lookahead-limiter", 48000)};
		track.effects[0].parameters["lookaheadMs"] = i ? 7 : 3;
		s.tracks.append(track);
	}
	AudioSessionTrack bus;
	bus.id = bus.name = "bus";
	bus.routing.bus = true;
	bus.effects = {makeAudioEffect("saturation", 48000)};
	s.tracks.append(bus);
	s.masterGainDb = -12;
	s.masterEffects = {makeAudioEffect("gain", 48000)};
	s.masterEffects[0].parameters["gainDb"] = -6;
	s.masterEffects.append(makeAudioEffect("lookahead-limiter", 48000));
	return s;
}
bool taps()
{
	bool ok = true;
	QString error;
	const auto s = fixture();
	const auto transfer = [](double value) {
		const auto boost = std::pow(10.0, .3);
		return std::tanh(value * boost) / std::tanh(boost);
	};
	for (const auto &id : {QString("a"), QString("b"), QString("bus"), QString()})
		for (const auto tap : {AudioSessionRenderTarget::Tap::PreFader, AudioSessionRenderTarget::Tap::PostFader}) {
			if (id.isEmpty() && tap == AudioSessionRenderTarget::Tap::PreFader)
				continue;
			AudioSessionRenderer renderer;
			ok &= expect(renderer.prepare(s, &error, {}, {id, tap}), "tap prepares");
			const auto output = renderer.renderBlock(0, 4);
			bool exact = output.succeeded();
			for (int i = 0; exact && i < 8; ++i) {
				const auto value = s.sources[0].audio.clip.samples[i];
				const auto expected =
				    id.isEmpty() ? transfer(value * .75) * std::pow(10.0, -.9)
				    : id == "bus"
				        ? (tap == AudioSessionRenderTarget::Tap::PreFader ? value * .75 : transfer(value * .75))
				        : value * (tap == AudioSessionRenderTarget::Tap::PreFader ? 1
				                   : id == "a"                                    ? .5
				                                                                  : .25);
				exact &= std::abs(output.clip.samples[i] - expected) < 1e-7;
			}
			ok &= expect(exact, "pre/post/master taps agree with independent gain and nonlinear bus oracle");
		}
	auto varied = s;
	varied.tracks[0].routing.outputEnabled = false;
	varied.tracks[0].gainAutomation = {{0, -6}, {3, 6}};
	varied.tracks[0].routing.swapChannels = varied.tracks[0].routing.invertLeft = true;
	AudioSessionRenderer renderer;
	ok &= renderer.prepare(varied, &error, {}, {"a"});
	const auto automated = renderer.renderBlock(1, 2);
	ok &= expect(automated.succeeded() && std::abs(automated.clip.samples[0] + .4 * .5 * std::pow(10.0, -.1)) < 1e-7,
	             "disconnected strip retains local automation, polarity and swap at absolute range time");
	varied.tracks[1].solo = true;
	ok &= renderer.prepare(varied, &error, {}, {"a"});
	ok &= expect(renderer.renderBlock(0, 4).clip.samples == QVector<float>(8, 0),
	             "respect-solo tap is silent for excluded strip");
	varied = s;
	AudioSessionTrack selectedBus;
	selectedBus.id = selectedBus.name = "selected";
	selectedBus.routing.bus = selectedBus.solo = true;
	varied.tracks[2].routing.outputId = selectedBus.id;
	varied.tracks.append(selectedBus);
	varied.tracks[0].solo = true;
	ok &= renderer.prepare(varied, &error, {}, {"bus"});
	const auto combined = renderer.renderBlock(0, 4);
	bool combinedCorrect = combined.succeeded();
	for (int i = 0; combinedCorrect && i < 8; ++i)
		combinedCorrect &=
		    std::abs(combined.clip.samples[i] - transfer(s.sources[0].audio.clip.samples[i] * .75)) < 1e-7;
	ok &= expect(combinedCorrect, "nonlinear tap recombines audible and downstream-solo residual domains");
	ok &= renderer.prepare(varied, &error, {}, {"b"});
	ok &= expect(renderer.renderBlock(0, 4).clip.samples[0] > 0,
	             "upstream track remains audible through a downstream solo bus");
	varied.tracks[0].solo = true;
	varied.tracks[0].muted = true;
	ok &= renderer.prepare(varied, &error, {}, {"a"});
	ok &=
	    expect(renderer.renderBlock(0, 4).clip.samples == QVector<float>(8, 0), "mute wins over solo for tapped strip");
	ok &= expect(!renderer.prepare(s, &error, {}, {"missing"}) &&
	                 !renderer.prepare(s, &error, {}, {{}, AudioSessionRenderTarget::Tap::PreFader}),
	             "invalid render targets rejected");
	varied = s;
	varied.tracks[0].routing.outputEnabled = false;
	varied.tracks[1].muted = true;
	varied.tracks[0].routing.sends = {{"bus", -6, 1, true, true}};
	ok &= renderer.prepare(varied, &error, {}, {"bus", AudioSessionRenderTarget::Tap::PreFader});
	const auto preSend = renderer.renderBlock(0, 4);
	ok &= expect(
	    preSend.succeeded() && preSend.clip.samples[0] == 0 &&
	        std::abs(preSend.clip.samples[1] + .2 * std::pow(10.0, -.3)) < 1e-7,
	    "bus pre tap contains upstream pre-fader send gain and balance, excluding fader and selected bus inserts");
	varied.sources[0].audio.clip = {1, 48000, {.2f, .3f, .1f, -.2f}};
	ok &= renderer.prepare(varied, &error, {}, {"a", AudioSessionRenderTarget::Tap::PreFader});
	ok &= expect(std::abs(renderer.renderBlock(0, 4).clip.samples[0] - .2 * std::numbers::sqrt2 / 2) < 1e-7,
	             "mono pre tap follows the established centered equal-power send law");
	std::array<float, 8> cleared;
	cleared.fill(42);
	std::vector<double> scratch(renderer.scratchSamples(4) + 8, 42);
	ok &= expect(renderer.renderInto(0, cleared, scratch, {[] { return true; }}) ==
	                     AudioSessionRenderer::BlockStatus::Cancelled &&
	                 std::all_of(cleared.begin(), cleared.end(), [](float v) { return v == 0; }),
	             "cancelled tap clears every output sample with oversized scratch");
	varied = s;
	varied.tracks[0].routing.outputEnabled = false;
	for (int i = 0; i < 30; ++i) {
		AudioSessionTrack next;
		next.id = next.name = QString::number(i);
		next.routing.bus = true;
		next.gainDb = 24;
		varied.tracks.last().routing.outputId = next.id;
		varied.tracks.append(next);
	}
	ok &= renderer.prepare(varied, &error, {}, {"a"});
	ok &= expect(renderer.renderBlock(0, 4).succeeded(), "unrelated downstream overflow cannot invalidate a strip tap");
	return ok;
}
bool dither()
{
	bool ok = true;
	AudioClip clip{2, 48000, QVector<float>(40002)};
	for (int i = 0; i < clip.samples.size(); ++i)
		clip.samples[i] = float(std::sin(i * .17) * .3);
	for (auto format : {AudioWavFormat::Pcm8, AudioWavFormat::Pcm16, AudioWavFormat::Pcm24, AudioWavFormat::Pcm32}) {
		AudioWavOptions options;
		options.format = format;
		options.dither = true;
		options.ditherSeed = 123456789;
		const auto whole = data(encodeAudioWav(clip, options));
		AudioWavEncoder encoder(options);
		QByteArray blocked;
		for (int at = 0; at < clip.samples.size(); at += 514) {
			AudioClip block{2, 48000, clip.samples.mid(at, 514)};
			// Failed calls cannot consume any samples of the deterministic sequence.
			int polls = 0;
			ok &= expect(encoder.encode(block, nullptr, {[&] { return ++polls > 2; }}).isEmpty(),
			             "cancelled block has no bytes after consuming random samples");
			blocked += data(encoder.encode(block));
		}
		ok &= expect(whole == blocked && !whole.isEmpty(),
		             "arbitrary blocks preserve whole-file seeded integer dither bytes");
	}
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = taps() && dither();
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir folder(QDir(root).filePath("audio-stems-XXXXXX"));
	if (!folder.isValid())
		return 1;
	auto s = fixture();
	AudioStemExportRequest request;
	request.directory = folder.path();
	request.stripIds = {"bus", "a"};
	request.includeMaster = true;
	request.end = 20001;
	request.dither = true;
	request.format = AudioWavFormat::Pcm24;
	const auto plan = planAudioSessionStems(s, request);
	ok &= expect(plan.error.isEmpty() && plan.files.size() == 3 && plan.files[1].target.stripId == "a",
	             "stable session order ignores selection order");
	auto result = writeAudioSessionStems(s, request);
	ok &= expect(result.succeeded && result.completed == 3 && result.manifest["status"] == "complete",
	             "aligned stem delivery succeeds");
	for (int i = 0; i < 3; ++i) {
		AudioSessionRenderer renderer;
		QString error;
		ok &= renderer.prepare(s, &error, {}, plan.files[i].target);
		AudioWavOptions options;
		options.format = request.format;
		options.dither = true;
		options.ditherSeed = plan.files[i].ditherSeed;
		const auto expected = encodeAudioWav(renderer.renderBlock(0, 20001).clip, options);
		const auto actual = read(plan.files[i].path);
		ok &= expect(actual == expected && result.files[i].frames == 20001 &&
		                 result.files[i].saved.identity.sha256 ==
		                     QCryptographicHash::hash(actual, QCryptographicHash::Sha256),
		             "streamed per-strip WAV bytes, uniform lengths, seeded dither and manifest hash agree");
	}
	ok &= expect(QJsonDocument::fromJson(read(plan.manifestPath)).object() == result.manifest,
	             "on-disk complete manifest matches report");
	const auto offset = data(read(plan.files[1].path)).right(600);
	ok &= expect(offset != data(read(plan.files[2].path)).right(600),
	             "quiet stem tails use distinct deterministic dither streams");
	{
		auto subset = request;
		subset.includeMaster = false;
		subset.stripIds = {"a"};
		ok &= expect(planAudioSessionStems(s, subset).files[0].ditherSeed == plan.files[1].ditherSeed,
		             "dither stream is stable across selection order and master inclusion");
		subset.ditherSeed = std::numeric_limits<quint64>::max();
		ok &= expect(planAudioSessionStems(s, subset).files[0].ditherSeed == 0,
		             "unsigned seed offsets wrap reproducibly");
	}
	const auto old = read(plan.files[0].path);
	ok &= expect(!writeAudioSessionStems(s, request).succeeded && read(plan.files[0].path) == old,
	             "overwrite requires explicit consent for every delivery");
	request.overwrite = true;
	request.progress = [&](int index, int, qint64 complete, qint64) {
		if (!index && complete == 16384)
			write(plan.files[2].path, "external revision");
	};
	result = writeAudioSessionStems(s, request);
	ok &= expect(!result.succeeded && result.completed == 2 && result.manifest["status"] == "failed" &&
	                 read(plan.files[2].path) == "external revision",
	             "preflight digests protect later files from intervening edits and report partial delivery");
	request.progress = {};
	request.prefix = "cancel";
	request.overwrite = false;
	bool cancel = false;
	request.progress = [&](int index, int, qint64, qint64) {
		if (index == 1)
			cancel = true;
	};
	result = writeAudioSessionStems(s, request, {[&] { return cancel; }});
	ok &= expect(result.cancelled && result.completed == 1 && result.manifest["status"] == "cancelled" &&
	                 QFile::exists(result.plan.files[0].path) && !QFile::exists(result.plan.files[1].path) &&
	                 !QFile::exists(result.plan.files[2].path),
	             "cancellation keeps exactly committed files and finalizes bounded status manifest");
	request.progress = {};
	request.prefix = "new-conflict";
	const auto conflictPlan = planAudioSessionStems(s, request);
	request.overwrite = true;
	request.progress = [&](int index, int, qint64, qint64) {
		if (!index)
			write(conflictPlan.files[1].path, "arrived");
	};
	result = writeAudioSessionStems(s, request);
	ok &= expect(!result.succeeded && result.completed == 1 && read(conflictPlan.files[1].path) == "arrived",
	             "new paths do not inherit permission to overwrite files appearing after preflight");
	request.progress = {};
	request.prefix = "dry";
	const auto conflictManifest = planAudioSessionStems(s, request).manifestPath;
	request.progress = [&](int index, int, qint64, qint64) {
		if (!index)
			write(conflictManifest, "external manifest");
	};
	result = writeAudioSessionStems(s, request);
	ok &= expect(!result.succeeded && result.completed == 1 && result.manifest["status"] == "manifest-failed" &&
	                 read(conflictManifest) == "external manifest",
	             "manifest conflict preserves external revision, stops next file and reports committed WAV");
	request.prefix = "dry-render";
	request.progress = {};
	request.dryRun = true;
	result = writeAudioSessionStems(s, request);
	ok &= expect(result.succeeded && result.completed == 3 && !QFile::exists(result.plan.manifestPath) &&
	                 !QFile::exists(result.plan.files[0].path),
	             "dry run renders every stem and writes nothing");
	request.dryRun = false;
	request.prefix = "protected";
	const auto protectedPlan = planAudioSessionStems(s, request);
	s.sources[0].audio.sourcePath = protectedPlan.files.last().path;
	result = writeAudioSessionStems(s, request);
	ok &= expect(!result.succeeded && !QFile::exists(protectedPlan.files[0].path) &&
	                 !QFile::exists(protectedPlan.manifestPath),
	             "all destinations preflight before any output, including absent protected sources");
	s.sources[0].audio.sourcePath = folder.filePath("source.wav");
	write(s.sources[0].audio.sourcePath, "source");
	std::error_code ec;
	std::filesystem::create_hard_link(std::filesystem::path(s.sources[0].audio.sourcePath.toStdWString()),
	                                  std::filesystem::path(protectedPlan.files.last().path.toStdWString()), ec);
	ok &=
	    expect(!ec && !writeAudioSessionStems(s, request).succeeded && read(s.sources[0].audio.sourcePath) == "source",
	           "hard-linked source cannot be replaced");
	request.prefix = "solo";
	request.end = 4;
	request.format = AudioWavFormat::Float32;
	request.dither = false;
	s.tracks[1].solo = true;
	result = writeAudioSessionStems(s, request);
	ok &= expect(result.succeeded && result.files[1].peak > 0, "temporary solo ignored by default");
	request.respectSolo = true;
	request.prefix = "respected";
	result = writeAudioSessionStems(s, request);
	ok &= expect(result.succeeded && result.files[1].peak == 0, "explicit respect-solo matches mixer exclusions");
	request.prefix = "../../CON:*?";
	s.tracks[0].name = "aux:/\\?*...";
	const auto names = planAudioSessionStems(s, request);
	ok &= expect(names.error.isEmpty() &&
	                 QFileInfo(names.files[1].path).absolutePath() == QDir(folder.path()).absolutePath() &&
	                 !QFileInfo(names.files[1].path).fileName().contains(':'),
	             "portable components cannot escape output directory");
	request.stripIds << "a";
	ok &= expect(!planAudioSessionStems(s, request).error.isEmpty(), "duplicate stem selection rejected");
	request.stripIds.removeLast();
	request.end = AudioSessionFrameLimit;
	ok &= expect(!planAudioSessionStems(s, request).error.isEmpty(),
	             "RIFF limit rejected before rendering or publishing any file");
	request.end = -1;
	s.effectTailSeconds = .1;
	s.tracks[0].effects = {makeAudioEffect("delay", s.sampleRate)};
	ok &= expect(planAudioSessionStems(s, request).end == 4804,
	             "default shared end includes original session effects tail");
	request.prefix = "locked";
	const auto lockPlan = planAudioSessionStems(s, request);
	QLockFile lock(lockPlan.manifestPath + ".batch.lock");
	ok &= expect(lock.tryLock(0) && !writeAudioSessionStems(s, request).succeeded &&
	                 !QFile::exists(lockPlan.files[0].path),
	             "same-delivery batch lock rejects another writer before publishing");
	std::cout << (ok ? "Audio stem delivery verification passed\n" : "Audio stem delivery verification failed\n");
	return ok ? 0 : 1;
}
