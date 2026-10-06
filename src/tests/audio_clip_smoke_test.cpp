#include "core/audio_clip.h"
#include "core/audio_waveform.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
QByteArray read(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-core-XXXXXX")));
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	const AudioClip stereo{2, 8000, {-1.0f, 0.5f, -0.5f, 0.25f, 0.0f, -0.25f, 0.5f, -0.5f}};
	bool ok = true;
	QString error;
	const QByteArray wav = encodeAudioWav(stereo, &error);
	const auto decoded = decodeAudioClip(QStringLiteral("fixture.wav"), wav);
	ok &= expect(decoded.succeeded() && decoded.clip.samples == stereo.samples,
	             "PCM16 round trip must preserve representable signed samples exactly");
	ok &= expect(decoded.clip.frameCount() == 4 && decoded.clip.sampleRate == 8000, "format round trip");
	struct SampleFixture {
		quint16 tag, bits;
		QByteArray data;
	};
	for (const auto& fixture :
	     QVector<SampleFixture>{{1, 8, QByteArray::fromHex("40c0")},
	                            {1, 24, QByteArray::fromHex("0000c0000040")},
	                            {1, 32, QByteArray::fromHex("000000c000000040")},
	                            {3, 32, QByteArray::fromHex("000000bf0000003f")},
	                            {3, 64, QByteArray::fromHex("000000000000e0bf000000000000e03f")}}) {
		QByteArray stream = wav.first(44) + fixture.data;
		qToLittleEndian<quint32>(quint32(stream.size() - 8), stream.data() + 4);
		qToLittleEndian(fixture.tag, stream.data() + 20);
		qToLittleEndian<quint16>(1, stream.data() + 22);
		qToLittleEndian<quint32>(8000 * fixture.bits / 8, stream.data() + 28);
		qToLittleEndian<quint16>(fixture.bits / 8, stream.data() + 32);
		qToLittleEndian(fixture.bits, stream.data() + 34);
		qToLittleEndian<quint32>(quint32(fixture.data.size()), stream.data() + 40);
		const auto samples = decodeAudioClip(QStringLiteral("format.wav"), stream);
		ok &= expect(samples.succeeded() && samples.clip.samples == QVector<float>({-0.5f, 0.5f}),
		             "native PCM and float editing formats preserve sign and scale");
	}
	QByteArray extensible = wav;
	extensible.insert(36, QByteArray::fromHex("16001000030000000100000000001000800000aa00389b71"));
	qToLittleEndian<quint32>(quint32(extensible.size() - 8), extensible.data() + 4);
	qToLittleEndian<quint32>(40, extensible.data() + 16);
	qToLittleEndian<quint16>(0xfffe, extensible.data() + 20);
	ok &= expect(decodeAudioClip(QStringLiteral("extensible.wav"), extensible).clip.samples == stereo.samples,
	             "extensible PCM GUID decoding");
	extensible[59] = char(0xff);
	ok &= expect(!decodeAudioClip(QStringLiteral("bad-guid.wav"), extensible).succeeded(),
	             "an unrelated extensible subtype cannot be treated as PCM");
	const auto trimmed = applyAudioEdit(stereo, {QStringLiteral("trim"), 1, 3});
	ok &= expect(trimmed.succeeded() && trimmed.clip.samples == QVector<float>({-0.5f, 0.25f, 0.0f, -0.25f}),
	             "trim uses exclusive, frame-aligned boundaries");
	const auto removed = applyAudioEdit(stereo, {QStringLiteral("delete"), 1, 3});
	ok &= expect(removed.succeeded() && removed.clip.samples == QVector<float>({-1.0f, 0.5f, 0.5f, -0.5f}),
	             "delete preserves samples outside the selection");
	const auto emptied = applyAudioEdit(stereo, {QStringLiteral("delete"), 0, 4});
	ok &= expect(emptied.succeeded() && emptied.clip.samples.isEmpty() && emptied.clip.channels == 2 &&
	                 validateAudioClip(emptied.clip, true).isEmpty() && !validateAudioClip(emptied.clip).isEmpty(),
	             "delete all yields a valid empty working document, not playable delivery audio");
	const auto blank = createAudioClip(8000, 2);
	ok &= expect(blank.succeeded() && blank.clip.frameCount() == 0, "new audio can start empty");
	ok &= expect(!createAudioClip(0, 2).succeeded() && !createAudioClip(8000, 0).succeeded() &&
	                 !createAudioClip(8000, 2, AudioSampleLimit).succeeded(),
	             "new audio validates format and allocation bounds");
	const auto pasted = replaceAudioRange(stereo, 1, 2, trimmed.clip);
	ok &= expect(pasted.succeeded() &&
	                 pasted.clip.samples == QVector<float>({-1, .5f, -.5f, .25f, 0, -.25f, 0, -.25f, .5f, -.5f}),
	             "paste replaces exactly the selection and retains the suffix in frame order");
	const auto restoredEmpty = replaceAudioRange(blank.clip, 0, 0, stereo);
	ok &= expect(restoredEmpty.succeeded() && restoredEmpty.clip.samples == stereo.samples,
	             "paste can populate an empty document");
	const auto inserted = insertAudioSilence(stereo, 2, 2);
	ok &= expect(inserted.succeeded() &&
	                 inserted.clip.samples == QVector<float>({-1, .5f, -.5f, .25f, 0, 0, 0, 0, 0, -.25f, .5f, -.5f}),
	             "insert silence moves the tail without replacing existing samples");
	const auto mixed = mixAudioClip(stereo, 3, trimmed.clip);
	ok &= expect(mixed.succeeded() &&
	                 mixed.clip.samples == QVector<float>({-1, .5f, -.5f, .25f, 0, -.25f, 0, -.25f, 0, -.25f}),
	             "mix adds matching channels and zero-extends the destination");
	ok &= expect(mixAudioClip(stereo, 0, stereo).clip.samples.first() == -2.0f, "mix retains float headroom");
	AudioClip mismatch = stereo;
	mismatch.sampleRate = 11025;
	ok &=
	    expect(!replaceAudioRange(stereo, 0, 0, mismatch).succeeded() && !mixAudioClip(stereo, 0, mismatch).succeeded(),
	           "paste and mix refuse implicit sample-rate reinterpretation");
	ok &= expect(!replaceAudioRange(stereo, -1, 0, stereo).succeeded() &&
	                 !mixAudioClip(stereo, 5, stereo).succeeded() && !insertAudioSilence(stereo, 1, -1).succeeded(),
	             "editing rejects invalid insertion ranges");
	ok &= expect(replaceAudioRange(stereo, 0, 0, stereo, {[]() { return true; }}).cancelled &&
	                 mixAudioClip(stereo, 0, stereo, {[]() { return true; }}).cancelled,
	             "paste and mix cancellation never publish partial clips");
	const auto dc = applyAudioEdit({2, 8000, {0.5f, -0.5f, 1.0f, 0.0f}}, {QStringLiteral("remove-dc")});
	ok &= expect(dc.succeeded() && dc.clip.samples == QVector<float>({-.25f, -.25f, .25f, .25f}),
	             "DC removal subtracts an independent mean per channel");
	const auto polarity = applyAudioEdit(stereo, {QStringLiteral("invert"), 1, 2});
	ok &= expect(polarity.clip.samples == QVector<float>({-1, .5f, .5f, -.25f, 0, -.25f, .5f, -.5f}),
	             "polarity inversion preserves unselected frames");
	const auto dual = applyAudioEdit({1, 8000, {.25f, -.5f}}, {QStringLiteral("stereo")});
	ok &= expect(dual.succeeded() && dual.clip.channels == 2 &&
	                 dual.clip.samples == QVector<float>({.25f, .25f, -.5f, -.5f}),
	             "stereo conversion duplicates mono without attenuation");
	const auto dcEnvelope = audioClipPeaks({1, 8000, {.25f, .5f, .75f}}, 1);
	ok &= expect(dcEnvelope.peaks == QVector<float>({.25f, .75f}),
	             "waveform extrema preserve a real DC offset instead of forcing a zero baseline");
	AudioClip envelopeFixture{3, 48000, {}};
	for (int frame = 0; frame < 1031; ++frame) {
		envelopeFixture.samples << float((frame * 17) % 101 - 20) / 31.0f << float((frame * 13) % 103 - 90) / 47.0f
		                        << float(frame % 7 + 1) / 8.0f;
	}
	const auto cached = AudioWaveformData::build(envelopeFixture);
	ok &= expect(cached.valid() && cached.cacheBytes() < 1500, "bounded multilevel waveform cache");
	bool exactRanges = true;
	for (int first = 0; first < 1031; first += 17) {
		for (int end = first + 1; end <= 1031; end += 31) {
			for (int channel = 0; channel < 3; ++channel) {
				float low = envelopeFixture.samples[first * 3 + channel], high = low;
				for (int frame = first; frame < end; ++frame) {
					low = std::min(low, envelopeFixture.samples[frame * 3 + channel]);
					high = std::max(high, envelopeFixture.samples[frame * 3 + channel]);
				}
				const auto value = cached.range(channel, first, end);
				exactRanges &= low == value.minimum && high == value.maximum;
			}
		}
	}
	ok &= expect(exactRanges, "cached waveform queries equal brute-force extrema across unaligned ranges and channels");
	ok &= expect(AudioWaveformData::build(blank.clip).valid() &&
	                 !AudioWaveformData::build(envelopeFixture, {[]() { return true; }}).valid(),
	             "empty waveform is valid and cancellation publishes no cache");
	ok &= expect(cached.range(3, 0, 10).minimum == 0 && cached.range(0, -1, 10).maximum == 0,
	             "waveform rejects invalid ranges and channels without reading samples");
	const auto reversed = applyAudioEdit(stereo, {QStringLiteral("reverse"), 1, 4});
	ok &= expect(reversed.clip.samples == QVector<float>({-1.0f, 0.5f, 0.5f, -0.5f, 0.0f, -0.25f, -0.5f, 0.25f}),
	             "reverse frames must not exchange channels");
	const auto silent = applyAudioEdit(stereo, {QStringLiteral("silence"), 1, 3});
	ok &= expect(silent.clip.samples == QVector<float>({-1.0f, 0.5f, 0, 0, 0, 0, 0.5f, -0.5f}),
	             "silence must preserve time and unselected samples");
	const AudioClip constant{1, 1000, {1, 1, 1, 1, 1}};
	const auto fade = applyAudioEdit(constant, {QStringLiteral("fade-in"), 1, 4});
	ok &= expect(fade.clip.samples == QVector<float>({1, 0, 0.5f, 1, 1}), "fade-in endpoints and unaffected edges");
	const auto fadeOut = applyAudioEdit(constant, {QStringLiteral("fade-out"), 1, 4});
	ok &= expect(fadeOut.clip.samples == QVector<float>({1, 1, 0.5f, 0, 1}), "fade-out endpoints");
	ok &= expect(applyAudioEdit(constant, {QStringLiteral("fade-out"), 2, 3}).clip.samples[2] == 0,
	             "one-frame fade must be finite silence");
	const auto gain = applyAudioEdit(stereo, {QStringLiteral("gain"), 1, 4, 6.020599913});
	ok &= expect(std::abs(gain.clip.samples[2] + 1.0f) < 0.00001f && gain.clip.samples[0] == -1.0f,
	             "gain in dB affects only the selection");
	const auto normalized = applyAudioEdit(stereo, {QStringLiteral("normalize"), 1, 4, -6.020599913});
	ok &= expect(std::abs(normalized.clip.samples[2] + 0.5f) < 0.00001f,
	             "normalization uses a linked peak across channels");
	const auto mono = applyAudioEdit(stereo, {QStringLiteral("mono")});
	ok &= expect(mono.clip.channels == 1 && mono.clip.samples == QVector<float>({-0.25f, -0.125f, -0.125f, 0.0f}),
	             "mono averages channels without changing frames or rate");
	ok &= expect(!applyAudioEdit(stereo, {QStringLiteral("mono"), 1, 4}).succeeded(),
	             "partial channel conversion must be rejected");
	for (const AudioEdit& edit :
	     QVector<AudioEdit>{{QStringLiteral("trim"), -1, 3},
	                        {QStringLiteral("reverse"), 2, 2},
	                        {QStringLiteral("gain"), 0, 99},
	                        {QStringLiteral("gain"), 0, -1, 25},
	                        {QStringLiteral("normalize"), 0, -1, 1},
	                        {QStringLiteral("gain"), 0, -1, std::numeric_limits<double>::quiet_NaN()},
	                        {QStringLiteral("bogus")}}) {
		ok &= expect(!applyAudioEdit(stereo, edit).succeeded(), "invalid edits must fail without a document mutation");
	}
	ok &= expect(stereo.samples[0] == -1.0f && stereo.channels == 2, "all edits leave their source unchanged");
	ok &= expect(applyAudioEdit(stereo, {QStringLiteral("gain")}, {[] { return true; }}).cancelled,
	             "processing cancellation");
	ok &= expect(decodeAudioClip(QStringLiteral("fixture.wav"), wav, {[] { return true; }}).cancelled,
	             "decode cancellation");
	const auto peaks = audioClipPeaks(stereo, 2);
	ok &= expect(peaks.valid && peaks.bucketCount == 2 && peaks.peaks[0] == -1.0f && peaks.peaks[5] == 0.5f,
	             "per-channel waveform peaks");
	AudioClip invalid = stereo;
	invalid.samples[0] = std::numeric_limits<float>::infinity();
	ok &= expect(!validateAudioClip(invalid).isEmpty() && encodeAudioWav(invalid).isEmpty(),
	             "non-finite edited audio cannot be written");
	for (int length : {0, 4, 11, 20, 43, 45}) {
		ok &= expect(!decodeAudioClip(QStringLiteral("bad.wav"), wav.left(length)).succeeded(),
		             "truncated inputs must not become editable sounds");
	}
	QByteArray wrong = wav;
	qToLittleEndian<quint16>(1, wrong.data() + 32);
	ok &= expect(!decodeAudioClip(QStringLiteral("bad.wav"), wrong).succeeded(), "invalid block alignment");
	wrong = wav;
	qToLittleEndian<quint32>(15, wrong.data() + 40);
	ok &= expect(!decodeAudioClip(QStringLiteral("bad.wav"), wrong).succeeded(), "partial sample frame");
	wrong = wav;
	wrong.replace(12, 4, "JUNK");
	ok &= expect(!decodeAudioClip(QStringLiteral("bad.wav"), wrong).succeeded(), "missing format chunk");
	ok &= expect(!decodeAudioClip(QStringLiteral("compressed.ogg"), "OggS").succeeded(),
	             "compressed audio has an explicit editing failure");
	QByteArray dmx(8 + 32 + 64, char(128));
	qToLittleEndian<quint16>(3, dmx.data());
	qToLittleEndian<quint16>(11025, dmx.data() + 2);
	qToLittleEndian<quint32>(96, dmx.data() + 4);
	dmx[24] = char(0);
	dmx[87] = char(255);
	const auto doom = decodeAudioClip(QStringLiteral("DSFIXTURE"), dmx);
	ok &= expect(doom.succeeded() && doom.clip.frameCount() == 64 && doom.clip.samples.first() == -1.0f &&
	                 doom.clip.samples.last() == 127.0f / 128,
	             "DMX padding is removed and unsigned samples decoded");
	const QString sourcePath = QDir(temporary.path()).filePath(QStringLiteral("source.wav"));
	const QString outputPath = QDir(temporary.path()).filePath(QStringLiteral("result.wav"));
	ok &= expect(saveAudioWav(stereo, sourcePath, false, {}, &error), "write independent generated source fixture");
	ok &= expect(!saveAudioWav(mono.clip, sourcePath, true, sourcePath, &error) && read(sourcePath) == wav,
	             "protect input even with overwrite");
	ok &= expect(saveAudioWav(mono.clip, outputPath, false, sourcePath, &error, true) && !QFileInfo::exists(outputPath),
	             "dry run has no writes");
	ok &= expect(saveAudioWav(mono.clip, outputPath, false, sourcePath, &error), "atomic WAV export");
	ok &= expect(!saveAudioWav(stereo, outputPath, false, sourcePath, &error),
	             "existing output requires explicit overwrite");
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(temporary.path(), &error) && staging.loadBaseArchive(archive, &error),
	             "create package fixture");
	ok &=
	    expect(stageAudioWav(wav, QStringLiteral("sound/test.wav"), &staging, false, &error), "stage generated bytes");
	const int count = staging.operations().size();
	ok &= expect(!stageAudioWav(wav, QStringLiteral("sound/test.wav"), &staging, false, &error) &&
	                 staging.operations().size() == count,
	             "collision failure is transactional");
	ok &= expect(!stageAudioWav(wav, QStringLiteral("../escape.wav"), &staging, false, &error) &&
	                 staging.operations().size() == count,
	             "unsafe path failure is transactional");
	ok &= expect(stageAudioWav(encodeAudioWav(mono.clip), QStringLiteral("sound/test.wav"), &staging, true, &error),
	             "explicit replacement stages new samples");
	QByteArray staged;
	ok &= expect(PackageStagingArchive(staging).readEntryBytes(QStringLiteral("sound/test.wav"), &staged, &error) &&
	                 decodeAudioClip(QStringLiteral("test.wav"), staged).clip.channels == 1,
	             "normal package snapshot reads the edit");
	if (argc > 1) {
		const auto cli = [&](const QStringList& args, int exitCode) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(),
			              QStringList{QStringLiteral("--cli"), QStringLiteral("--json"),
			                          QStringLiteral("--settings-file"),
			                          QDir(temporary.path()).filePath(QStringLiteral("cli-settings.ini"))} +
			                  args);
			const bool done = process.waitForFinished(30000);
			const auto output = process.readAllStandardOutput();
			if (!done || process.exitCode() != exitCode) {
				std::cerr << output.constData() << process.readAllStandardError().constData();
			}
			return expect(done && process.exitCode() == exitCode && QJsonDocument::fromJson(output).isObject(),
			              "CLI JSON and exit contract");
		};
		const QString cliOutput = QDir(temporary.path()).filePath(QStringLiteral("cli.wav"));
		const QStringList args{QStringLiteral("asset"),
		                       QStringLiteral("audio-edit"),
		                       sourcePath,
		                       QStringLiteral("--operation"),
		                       QStringLiteral("trim"),
		                       QStringLiteral("--start-frame"),
		                       QStringLiteral("1"),
		                       QStringLiteral("--end-frame"),
		                       QStringLiteral("3"),
		                       QStringLiteral("--output"),
		                       cliOutput};
		ok &= cli(args + QStringList{QStringLiteral("--dry-run")}, 0);
		ok &= expect(!QFileInfo::exists(cliOutput), "CLI dry-run must create no output");
		ok &= cli(args, 0);
		ok &=
		    expect(read(cliOutput) == encodeAudioWav(trimmed.clip), "CLI and editor share exactly the same processing");
		ok &= cli(args, 1);
		QStringList invalidRange = args;
		invalidRange[invalidRange.indexOf(QStringLiteral("--end-frame")) + 1] = QStringLiteral("999");
		ok &= cli(invalidRange + QStringList{QStringLiteral("--overwrite")}, 4);
		ok &= cli({QStringLiteral("asset"), QStringLiteral("audio-edit"), sourcePath, QStringLiteral("--operation"),
		           QStringLiteral("gain"), QStringLiteral("--db"), QStringLiteral("nope"), QStringLiteral("--output"),
		           cliOutput},
		          2);
		ok &= cli({QStringLiteral("asset"), QStringLiteral("audio-edit"), temporary.path(), QStringLiteral("--entry"),
		           QStringLiteral("source.wav"), QStringLiteral("--operation"), QStringLiteral("mono"),
		           QStringLiteral("--output"), cliOutput, QStringLiteral("--overwrite")},
		          0);
		ok &= expect(read(cliOutput) == encodeAudioWav(mono.clip), "package-entry CLI editing matches local editing");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
