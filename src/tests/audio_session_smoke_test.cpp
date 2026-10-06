#include "core/asset_formats.h"
#include "core/audio_session.h"
#include "core/audio_session_io.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool result, const char *name)
{
	if (!result) {
		std::cerr << name << '\n';
	}
	return result;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-6; }
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
QByteArray wire(const QJsonObject &metadata, const QByteArray &media)
{
	const auto json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	QByteArray bytes = QByteArray::fromHex("56534d49580d0a1a0100000000000000");
	qToLittleEndian<quint32>(quint32(json.size()), bytes.data() + 12);
	bytes += json;
	bytes += media;
	bytes += QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
	return bytes;
}
AudioSession fixture()
{
	AudioSession session;
	session.name = QString::fromUtf8("声音 — session");
	AudioProject source;
	source.clip = {1, 48000, {1, 0.5f, -0.5f, -1}};
	source.endFrame = 4;
	source.sourceName = "original";
	session.sources.append({"source", source});
	AudioSessionTrack track;
	track.id = "track";
	track.name = "Track";
	track.pan = -1;
	track.regions.append({"clip", "Clip", "source", 2, 0, 4});
	session.tracks.append(track);
	return session;
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	const QString temporaryRoot = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (temporaryRoot.isEmpty() || !QDir().mkpath(temporaryRoot)) {
		return EXIT_FAILURE;
	}
	QTemporaryDir temp(QDir(temporaryRoot).filePath("session-core-XXXXXX"));
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	auto original = fixture();
	const auto sourceBefore = encodeAudioProject(original.sources[0].audio);
	auto mix = renderAudioSession(original);
	ok &= expect(mix.succeeded() && mix.clip.frameCount() == 6 && mix.clip.channels == 2,
	             "session renders timeline duration and stereo master");
	const QVector<float> expected{0, 0, 0, 0, 1, 0, 0.5f, 0, -0.5f, 0, -1, 0};
	for (int i = 0; i < expected.size() && i < mix.clip.samples.size(); ++i) {
		ok &= expect(near(expected[i], mix.clip.samples[i]), "known timeline samples and hard-left routing");
	}
	auto stereo = original;
	stereo.sources[0].audio.clip = {2, 48000, {1, -1, 0.5f, -0.5f, 0.25f, -0.25f, 2, -2}};
	stereo.tracks[0].pan = 0;
	const auto stereoMix = renderAudioSession(stereo, 2, 6);
	ok &= expect(stereoMix.succeeded() && stereoMix.clip.samples == stereo.sources[0].audio.clip.samples,
	             "stereo center preserves channels, unity and float headroom");
	auto automated = original;
	automated.tracks[0].pan = 0;
	automated.tracks[0].gainAutomation = {{2, 0}, {4, -12}};
	automated.tracks[0].panAutomation = {{2, -1}, {4, 1}};
	const auto automatedMix = renderAudioSession(automated);
	ok &= expect(near(automatedMix.clip.samples[4], 1) &&
	                 near(automatedMix.clip.samples[6], 0.5 * std::pow(10.0, -6.0 / 20) / std::sqrt(2.0)) &&
	                 near(automatedMix.clip.samples[9], -0.5 * std::pow(10.0, -12.0 / 20)),
	             "sample-accurate independent analytic automation oracle");
	AudioSessionRenderer renderer;
	ok &= expect(renderer.prepare(automated, &error), "prepare automated render plan");
	for (int blockSize : {1, 2, 3, 5}) {
		QVector<float> joined;
		for (int frame = 0; frame < 6; frame += blockSize) {
			joined += renderer.renderBlock(frame, std::min(blockSize, 6 - frame)).clip.samples;
		}
		ok &= expect(joined == automatedMix.clip.samples, "render bits are independent of block boundaries");
	}
	auto doubled = original;
	auto second = doubled.tracks[0];
	second.id = "second";
	second.regions[0].id = "second-clip";
	doubled.tracks.append(second);
	ok &= expect(near(renderAudioSession(doubled).clip.samples[4], 2), "overlapping tracks sum without clipping");
	doubled.tracks[1].solo = true;
	ok &= expect(near(renderAudioSession(doubled).clip.samples[4], 1), "solo excludes nonsolo tracks");
	doubled.tracks[1].muted = true;
	ok &= expect(near(renderAudioSession(doubled).clip.samples[4], 0),
	             "mute wins over solo without admitting other tracks");
	auto faded = original;
	faded.tracks[0].regions[0].fadeIn = 2;
	faded.tracks[0].regions[0].fadeOut = 2;
	const auto fadeMix = renderAudioSession(faded);
	ok &= expect(near(fadeMix.clip.samples[4], 0) && near(fadeMix.clip.samples[6], 0.5) &&
	                 near(fadeMix.clip.samples[8], -0.5) && near(fadeMix.clip.samples[10], 0),
	             "fade endpoints and unmodified center samples");
	AudioSessionEdit split;
	split.operation = "split";
	split.trackId = "track";
	split.regionId = "clip";
	split.position = 4;
	const auto divided = editAudioSession(original, split);
	ok &= expect(divided.succeeded() && divided.session.tracks[0].regions.size() == 2 &&
	                 renderAudioSession(divided.session).clip.samples == mix.clip.samples,
	             "nondestructive split preserves every rendered sample");
	ok &= expect(encodeAudioProject(original.sources[0].audio) == sourceBefore &&
	                 divided.session.sources[0].audio.clip.samples.constData() ==
	                     original.sources[0].audio.clip.samples.constData(),
	             "timeline edits share unchanged sample storage");
	split.position = 2;
	ok &= expect(!editAudioSession(original, split).succeeded(), "split at edge rejected");
	faded.tracks[0].regions[0].fadeIn = 3;
	split.position = 3;
	const auto throughFade = editAudioSession(faded, split);
	ok &= expect(throughFade.succeeded() &&
	                 renderAudioSession(throughFade.session).clip.samples == renderAudioSession(faded).clip.samples,
	             "split inside fade preserves the audible envelope");
	auto bad = original;
	bad.tracks[0].regions[0].sourceOffset = 2;
	ok &= expect(!validateAudioSessionStructure(bad).isEmpty(), "source-range overflow rejected");
	bad = original;
	bad.tracks[0].gainAutomation = {{1, 0}, {1, 2}};
	ok &= expect(!validateAudioSessionStructure(bad).isEmpty(), "duplicate automation frames rejected");
	bad = original;
	bad.sources[0].audio.clip.samples[0] = std::numeric_limits<float>::infinity();
	ok &= expect(!renderAudioSession(bad).succeeded(), "nonfinite media rejected before mixing");
	bad = original;
	bad.sources[0].audio.clip.samples[0] = std::numeric_limits<float>::max();
	bad.masterGainDb = 24;
	ok &= expect(!renderAudioSession(bad).succeeded(), "float overflow rejected instead of emitting infinity");
	ok &= expect(renderAudioSession(original, 0, -1, {[] { return true; }}).cancelled,
	             "render cancellation returns no successful partial output");
	bad = original;
	bad.sources[0].audio.metadata.insert("oversized", QString(256 * 1024, 'x'));
	ok &= expect(!validateAudioSession(bad).isEmpty(), "oversized imported metadata rejected");
	const auto *format = assetFormatForPath(QStringLiteral("arrangement.VSSESSION"));
	ok &= expect(format && format->module == QStringLiteral("audio"), "shared format catalog routes session documents");
	auto far = original;
	far.tracks[0].regions[0].position = 10000000000LL;
	ok &= expect(renderer.prepare(far, &error) && near(renderer.renderBlock(10000000000LL, 1).clip.samples[0], 1),
	             "64-bit timeline seeks do not allocate intervening silence");
	const auto savedBytes = encodeAudioSession(automated, &error);
	AudioSession decoded;
	// Hand-authored native metadata exercises the reader without its own writer.
	auto independent = QJsonDocument::fromJson(R"({"name":"wire","sampleRate":48000,"tempo":120,
	    "beatsPerBar":4,"masterGainDb":0,"frames":6,
	    "sources":[{"id":"source","name":"original","path":"","channels":1,"frames":4,"bytes":0}],
	    "tracks":[{"id":"track","name":"Track","gainDb":0,"pan":-1,"muted":false,"solo":false,
	    "gainAutomation":[],"panAutomation":[],"regions":[{"id":"clip","name":"Clip","sourceId":"source",
	    "position":2,"sourceOffset":0,"length":4,"gainDb":0,"fadeIn":0,"fadeOut":0,"muted":false}]}]})")
	                       .object();
	auto independentSources = independent["sources"].toArray();
	auto sourceRecord = independentSources[0].toObject();
	sourceRecord["bytes"] = sourceBefore.size();
	independentSources[0] = sourceRecord;
	independent["sources"] = independentSources;
	ok &= expect(decodeAudioSession(wire(independent, sourceBefore), &decoded, &error) &&
	                 renderAudioSession(decoded).clip.samples == expected,
	             "independent native wire fixture and known mix samples");
	const auto independentSaved = encodeAudioSession(decoded);
	for (int mutation = 0; mutation < 6; ++mutation) {
		auto hostile = independent;
		if (mutation == 0)
			hostile["sampleRate"] = 48000.5;
		if (mutation == 1)
			hostile["newProcessingState"] = true;
		if (mutation == 2)
			hostile["frames"] = 7;
		if (mutation >= 3) {
			auto tracks = hostile["tracks"].toArray();
			auto track = tracks[0].toObject();
			if (mutation == 3)
				track["newProcessingState"] = true;
			if (mutation == 4)
				track["gainAutomation"] = QJsonArray{QJsonObject{{"frame", 0}, {"value", 0}, {"curve", "unknown"}}};
			if (mutation == 5) {
				auto regions = track["regions"].toArray();
				auto region = regions[0].toObject();
				region["sourceOffset"] = 1;
				regions[0] = region;
				track["regions"] = regions;
			}
			tracks[0] = track;
			hostile["tracks"] = tracks;
		}
		ok &= expect(!decodeAudioSession(wire(hostile, sourceBefore), &decoded, &error) &&
		                 encodeAudioSession(decoded) == independentSaved,
		             "valid checksums cannot bypass schema validation or silently discard unknown processing state");
	}
	ok &= expect(!savedBytes.isEmpty() && decodeAudioSession(savedBytes, &decoded, &error) &&
	                 encodeAudioSession(decoded) == savedBytes,
	             "lossless session, media and automation round trip");
	auto corrupted = savedBytes;
	corrupted[20] ^= 1;
	ok &= expect(!decodeAudioSession(corrupted, &decoded, &error) && encodeAudioSession(decoded) == savedBytes,
	             "corrupt input preserves caller state");
	ok &= expect(!decodeAudioSession(savedBytes.first(savedBytes.size() - 1), &decoded, &error),
	             "truncated container rejected");
	corrupted = savedBytes;
	qToLittleEndian<quint32>(999, corrupted.data() + 8);
	corrupted.chop(32);
	corrupted += QCryptographicHash::hash(corrupted, QCryptographicHash::Sha256);
	ok &= expect(!decodeAudioSession(corrupted, &decoded, &error),
	             "unknown native version rejected even with valid checksum");
	AudioProjectSaveRequest save;
	save.path = QDir(temp.path()).filePath("test.vssession");
	const auto first = writeAudioSession(original, save);
	ok &= expect(first.succeeded && first.written && read(save.path) == encodeAudioSession(original),
	             "atomic native save binds exact bytes");
	save.expected = first.identity;
	ok &= expect(writeAudioSession(automated, save).succeeded && !writeAudioSession(original, save).succeeded,
	             "guarded save rejects a stale document revision");
	const auto preserve = read(save.path);
	save.expected = {};
	save.overwrite = true;
	ok &= expect(!writeAudioSession(original, save, {[] { return true; }}).succeeded && read(save.path) == preserve,
	             "cancelled save preserves existing bytes");
	{
		QLockFile lock(save.path + ".lock");
		ok &= expect(lock.tryLock(0), "fixture owns output lock");
		ok &= expect(!writeAudioSession(original, save).succeeded && read(save.path) == preserve,
		             "cooperating writer lock preserves destination");
	}
	for (const auto format : {AudioWavFormat::Pcm8, AudioWavFormat::Pcm16, AudioWavFormat::Pcm24, AudioWavFormat::Pcm32,
	                          AudioWavFormat::Float32}) {
		AudioSessionMixdown output;
		output.output.path = QDir(temp.path()).filePath(audioWavFormatId(format) + ".wav");
		output.format = format;
		const auto rendered = writeAudioSessionMixdown(stereo, output);
		AudioWavOptions options;
		options.format = format;
		ok &= expect(rendered.saved.succeeded && rendered.frames == 6 &&
		                 read(output.output.path) == encodeAudioWav(renderAudioSession(stereo).clip, options),
		             "streamed WAV agrees byte-for-byte with shared precision encoder");
		const auto keep = read(output.output.path);
		output.output.overwrite = true;
		output.output.expected = rendered.saved.identity;
		output.progress = [&](qint64, qint64) { write(output.output.path, "external change"); };
		ok &= expect(!writeAudioSessionMixdown(stereo, output).saved.succeeded &&
		                 read(output.output.path) == "external change",
		             "an external edit during streaming commit is preserved");
		ok &= expect(write(output.output.path, keep), "restore independent fixture for cancellation check");
		output.progress = {};
		ok &= expect(!writeAudioSessionMixdown(stereo, output, {[] { return true; }}).saved.succeeded &&
		                 read(output.output.path) == keep,
		             "cancelled streaming export preserves prior output");
	}
	AudioSessionMixdown dry;
	dry.output.path = QDir(temp.path()).filePath("dry.wav");
	dry.output.dryRun = true;
	dry.end = 20000;
	const auto streamed = writeAudioSessionMixdown(original, dry);
	ok &= expect(streamed.saved.succeeded && !streamed.saved.written && !QFile::exists(dry.output.path) &&
	                 streamed.frames == 20000,
	             "multi-block dry run renders without creating output");
	int progress = 0;
	dry.progress = [&](qint64, qint64) { ++progress; };
	ok &= expect(!writeAudioSessionMixdown(original, dry, {[&] { return progress > 0; }}).saved.succeeded &&
	                 progress == 1,
	             "mid-render cancellation stops after observed block progress");
	original.sources[0].audio.sourcePath = dry.output.path;
	dry.output.dryRun = false;
	ok &= expect(!writeAudioSessionMixdown(original, dry).saved.succeeded && !QFile::exists(dry.output.path),
	             "source path remains protected even when absent");
	std::cout << (ok ? "Audio session core verification passed\n" : "Audio session core verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
