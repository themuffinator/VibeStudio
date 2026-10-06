#include "core/audio_media.h"
#include "core/audio_recovery_store.h"
#include "core/audio_resample.h"
#include "tests/audio_media_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QTemporaryDir>
#include <QUuid>
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
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("media-core-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return temporary.filePath(QLatin1String(name)); };
	auto original = test::mediaFixture();
	original.sources[0].audio.sourcePath = path("missing.wav");
	const auto originalBytes = encodeAudioSession(original);
	const auto before = renderAudioSession(original, 0, 300);
	bool ok = expect(!originalBytes.isEmpty() && before.succeeded(),
	                 "valid embedded fixture plays without its external file");
	const auto inventory = inspectAudioMedia(original);
	ok &= expect(inventory.succeeded() && inventory.sources.size() == 2 &&
	                 inventory.sources[0].availability == "missing" && inventory.sources[0].clips == 3 &&
	                 inventory.sources[0].tracks.size() == 3 && inventory.sources[0].requiredFrames == 100 &&
	                 inventory.sources[1].availability == "embedded-only" && inventory.sources[1].clips == 0 &&
	                 audioMediaInventoryJson(inventory)["unusedSampleBytes"].toInteger() == 1024,
	             "inventory records exact usage and embedded memory without requiring file presence");
	auto changed = editAudioMedia(original, {"rename", {"media"}, "Shared Foley"});
	ok &= expect(changed.succeeded() && changed.session.sources[0].audio.sourceName == "Shared Foley" &&
	                 changed.session.sources[0].id == "media" &&
	                 changed.session.sources[0].audio.clip.samples.constData() ==
	                     original.sources[0].audio.clip.samples.constData() &&
	                 renderAudioSession(changed.session, 0, 300).clip.samples == before.clip.samples,
	             "rename preserves shared sample allocation and render");
	changed = editAudioMedia(original, {"prune"});
	ok &= expect(changed.succeeded() && changed.removedSourceIds == QStringList{"unused"} &&
	                 changed.session.sources.size() == 1 &&
	                 renderAudioSession(changed.session, 0, 300).clip.samples == before.clip.samples,
	             "pruning removes unused embedded snapshots only");
	const auto removed = editAudioMedia(original, {"remove", {"unused"}});
	ok &= expect(removed.succeeded() && encodeAudioSession(removed.session) == encodeAudioSession(changed.session),
	             "explicit unused removal matches pruning");
	AudioWavOptions options;
	options.format = AudioWavFormat::Float32;
	const auto wav = encodeAudioWav(original.sources[0].audio.clip, options);
	ok &= expect(test::writeMediaFixture(path("identical.wav"), wav), "write independently reviewed float source");
	auto candidate = readAudioMediaCandidate(path("identical.wav"));
	ok &= expect(candidate.succeeded() &&
	                 candidate.identity.sha256 == QCryptographicHash::hash(wav, QCryptographicHash::Sha256),
	             "candidate digest covers exact file bytes");
	changed = editAudioMedia(original, {"relink", {"media"}}, candidate);
	ok &= expect(changed.succeeded() && changed.identicalSamples && changed.session.sources[0].id == "media" &&
	                 changed.session.sources[0].audio.sourcePath == candidate.identity.canonicalPath &&
	                 changed.session.sources[0].audio.metadata == original.sources[0].audio.metadata &&
	                 changed.session.sources[0].audio.clip.markers == original.sources[0].audio.clip.markers &&
	                 renderAudioSession(changed.session, 0, 300).clip.samples == before.clip.samples,
	             "identical relink changes provenance only and preserves native metadata");
	auto replacement = original.sources[0].audio;
	for (auto &value : replacement.clip.samples)
		value *= 0.5f;
	replacement.metadata = {{"fixture", "replacement"}};
	replacement.sourcePath = path("nested-old-provenance.wav");
	replacement.clip.markers.cues[0].frame = 19;
	replacement.firstFrame = 10;
	replacement.endFrame = 90;
	const auto replacementBytes = encodeAudioProject(replacement);
	ok &= test::writeMediaFixture(path("replacement.vsaudio"), replacementBytes);
	candidate = readAudioMediaCandidate(path("replacement.vsaudio"));
	ok &= expect(candidate.succeeded() && candidate.audio.sourcePath == candidate.identity.canonicalPath,
	             "native review uses the selected file's provenance");
	ok &= expect(!editAudioMedia(original, {"relink", {"media"}}, candidate).succeeded(),
	             "relink rejects different audio");
	changed = editAudioMedia(original, {"replace", {"media"}}, candidate);
	if (!expect(changed.succeeded(), "reviewed replacement applies transactionally")) {
		std::cerr << changed.error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	ok &= expect(!changed.identicalSamples && !changed.resampled && changed.addedSourceId != "media" &&
	                 changed.removedSourceIds == QStringList{"media"} && changed.affectedRegionIds.size() == 3 &&
	                 changed.session.sources[0].audio.firstFrame == 0 &&
	                 changed.session.sources[0].audio.endFrame == 128 &&
	                 changed.session.sources[0].audio.sourceName == original.sources[0].audio.sourceName &&
	                 changed.session.sources[0].audio.metadata == replacement.metadata &&
	                 changed.session.sources[0].audio.clip.markers == replacement.clip.markers,
	             "replacement adopts reviewed metadata but retains the source label and full-source selection");
	auto compare = changed.session;
	compare.sources = original.sources;
	for (auto &track : compare.tracks)
		for (auto &region : track.regions)
			region.sourceId = "media";
	ok &= expect(
	    encodeAudioSession(compare) == originalBytes,
	    "replacement retains all clip IDs, positions, offsets, fades, groups, routing, automation and musical timing");
	const auto rendered = renderAudioSession(changed.session, 0, 300);
	bool half = rendered.succeeded();
	for (qsizetype i = 0; i < before.clip.samples.size(); ++i)
		half &= rendered.clip.samples[i] == before.clip.samples[i] * 0.5f;
	ok &= expect(half && encodeAudioSession(original) == originalBytes,
	             "all referencing clips render replacement samples; input remains untouched");
	const auto replacedBytes = encodeAudioSession(changed.session);
	AudioSession restored;
	QString error;
	ok &= expect(decodeAudioSession(replacedBytes, &restored, &error) && encodeAudioSession(restored) == replacedBytes,
	             "media change round-trips in native v7 without a format bump");
	const auto recoveryPath = writeAudioSessionRecovery(changed.session, path("song.vssession"), path("recovery"),
	                                                    QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	AudioSessionRecovery recovered;
	ok &= expect(!recoveryPath.isEmpty() &&
	                 readAudioSessionRecovery(
	                     recoveryPath,
	                     QCryptographicHash::hash(test::readMediaFixture(recoveryPath), QCryptographicHash::Sha256),
	                     &recovered, &error) &&
	                 encodeAudioSession(recovered.session) == replacedBytes,
	             "recovery retains new immutable media identity and edited samples");
	for (const auto &edit :
	     {AudioMediaEdit{"unknown"}, AudioMediaEdit{"rename", {"media"}, " "},
	      AudioMediaEdit{"rename", {"media"}, QString(257, 'x')}, AudioMediaEdit{"remove", {"media"}},
	      AudioMediaEdit{"remove", {"unused", "missing"}}, AudioMediaEdit{"remove", {"unused", "unused"}},
	      AudioMediaEdit{"prune", {"unused"}}, AudioMediaEdit{"rename", {"media", "unused"}, "both"},
	      AudioMediaEdit{"prune", {}, "name"}, AudioMediaEdit{"relink", {"media"}, {}, true}})
		ok &= expect(!editAudioMedia(original, edit, candidate).succeeded() &&
		                 encodeAudioSession(original) == originalBytes,
		             "invalid media request leaves the input transaction intact");
	for (int variant = 0; variant < 4; ++variant) {
		auto bad = candidate;
		if (variant == 0)
			bad.audio.clip.samples.resize(100);
		if (variant == 1)
			bad.audio.clip.channels = 1;
		if (variant == 2)
			bad.audio.clip.sampleRate = 2000;
		if (variant == 3)
			bad.audio.clip.samples[0] = std::numeric_limits<float>::quiet_NaN();
		ok &= expect(!editAudioMedia(original, {"replace", {"media"}}, bad, false).succeeded(),
		             "short, channel-changing, implicit-rate and nonfinite replacements are rejected");
	}
	auto higher = replacement;
	higher.clip = resampleAudioClip(replacement.clip, 2000).clip;
	higher.firstFrame = 0;
	higher.endFrame = higher.clip.frameCount();
	ok &= test::writeMediaFixture(path("higher.vsaudio"), encodeAudioProject(higher));
	const auto rateCandidate = readAudioMediaCandidate(path("higher.vsaudio"));
	const auto resampled = editAudioMedia(original, {"replace", {"media"}, {}, true}, rateCandidate);
	ok &= expect(
	    resampled.succeeded() && resampled.resampled && resampled.session.sources[0].audio.clip.sampleRate == 1000 &&
	        resampled.session.sources[0].audio.clip.samples == resampleAudioClip(higher.clip, 1000).clip.samples,
	    "explicit conversion uses shared antialias resampler");
	ok &= test::writeMediaFixture(path("replacement.vsaudio"), encodeAudioProject(original.sources[0].audio));
	ok &= expect(!editAudioMedia(original, {"replace", {"media"}}, candidate).succeeded(),
	             "digest guard rejects a file changed after review");
	ok &= expect(!readAudioMediaCandidate(path("absent.wav")).succeeded() &&
	                 !readAudioMediaCandidate(temporary.path()).succeeded(),
	             "missing files and directories cannot become candidates");
	const AudioWorkControl cancel{[] { return true; }};
	ok &= expect(inspectAudioMedia(original, cancel).cancelled &&
	                 readAudioMediaCandidate(path("identical.wav"), cancel).cancelled &&
	                 editAudioMedia(original, {"replace", {"media"}}, rateCandidate, true, cancel).cancelled &&
	                 !editAudioMedia(original, {"prune"}, {}, true, cancel).succeeded(),
	             "cancellation returns no adopted changes");
	std::cout << (ok ? "Media core verification passed\n" : "Media core verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
