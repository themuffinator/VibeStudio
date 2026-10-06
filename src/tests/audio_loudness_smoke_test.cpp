#include "core/audio_analysis.h"
#include "core/audio_project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}
bool near(const std::optional<double>& actual, double expected, double tolerance)
{
	return actual && std::isfinite(*actual) && std::abs(*actual - expected) <= tolerance;
}
AudioClip tone(int channels, int rate, double seconds, double amplitude = .1, double frequency = 997,
               double phase = 0)
{
	AudioClip clip{channels, rate, {}};
	clip.samples.resize(qint64(seconds * rate) * channels);
	for (qint64 frame = 0; frame < clip.frameCount(); ++frame) {
		for (int channel = 0; channel < channels; ++channel) {
			clip.samples[frame * channels + channel] = float(amplitude * std::sin(2 * std::numbers::pi * frequency * frame / rate + phase));
		}
	}
	return clip;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	// Original generated tones; the 997 Hz calibration and 3.01 dB channel-energy
	// relation follow ITU-R BS.1770. No upstream audio fixtures are redistributed.
	const AudioClip mono = tone(1, 48000, 1);
	const auto measured = analyzeAudioClip(mono);
	ok &= expect(measured.succeeded() && near(measured.analysis.integratedLufs, -23.01, .1) &&
	                 near(measured.analysis.truePeak, .1, .003) && measured.analysis.truePeakOversampling == 8 &&
	                 measured.analysis.channelMap == QVector<AudioChannelRole>{AudioChannelRole::Center},
	             "997 Hz mono calibration independently predicts loudness and reconstructed peak");
	if (!measured.succeeded() || !measured.analysis.integratedLufs || !measured.analysis.truePeak) { return EXIT_FAILURE; }
	const auto stereo = analyzeAudioClip(tone(2, 48000, 1));
	ok &= expect(stereo.succeeded() && measured.analysis.integratedLufs &&
	                 near(stereo.analysis.integratedLufs, *measured.analysis.integratedLufs + 10 * std::log10(2.0), 1e-7) &&
	                 near(stereo.analysis.truePeak, *measured.analysis.truePeak, 1e-7),
	             "independent stereo channels add loudness energy without doubling true peak");
	const auto intersample = analyzeAudioClip(tone(1, 48000, 1, 1.2, 12000, std::numbers::pi / 4));
	ok &= expect(intersample.succeeded() && intersample.analysis.peak < .85 && intersample.analysis.truePeak &&
	                 *intersample.analysis.truePeak > 1.15 && *intersample.analysis.truePeak < 1.26,
	             "phase-shifted quarter-rate sine reveals an intersample peak above full scale despite sub-full-scale samples");
	for (const int rate : {8000, 11025, 44100, 96000, 192000, 384000}) {
		const auto result = analyzeAudioClip(tone(1, rate, .8));
		ok &= expect(result.succeeded() && result.analysis.integratedLufs && std::isfinite(*result.analysis.integratedLufs) &&
		                 near(result.analysis.truePeak, .1, .008) &&
		                 result.analysis.truePeakOversampling == (rate < 96000 ? 8 : rate < 192000 ? 4 : rate < 384000 ? 2 : 1),
		             "supported rates retain finite metering and report the actual interpolation factor");
	}
	const auto shortRange = analyzeAudioClip(mono, 0, 19199);
	AudioClip ending = tone(1, 48000, .001, 1.2, 12000, std::numbers::pi / 4);
	AudioClip padded = ending; padded.samples += QVector<float>(64, 0);
	const auto tail = analyzeAudioClip(ending);
	const auto tailReference = analyzeAudioClip(padded);
	ok &= expect(tail.succeeded() && tailReference.succeeded() && tailReference.analysis.truePeak &&
	                 near(tail.analysis.truePeak, *tailReference.analysis.truePeak, 1e-8) &&
	                 *tail.analysis.truePeak > ending.samples.last(),
	             "a short range flushes its complete interpolation tail just like explicit trailing silence");
	AudioClip gated = tone(1, 48000, 9);
	for (qint64 frame = 3 * 48000; frame < 6 * 48000; ++frame) { gated.samples[frame] *= .01F; }
	const auto gate = analyzeAudioClip(gated);
	ok &= expect(gate.succeeded() && near(gate.analysis.integratedLufs, *measured.analysis.integratedLufs, .35) &&
	                 near(gate.analysis.relativeThresholdLufs, *measured.analysis.integratedLufs + 10 * std::log10(57.0 / 87.0) - 10, .12),
	             "the relative gate removes quiet programme blocks while retaining transition energy");
	const auto firstBlock = analyzeAudioClip(mono, 0, 19200);
	ok &= expect(shortRange.succeeded() && !shortRange.analysis.integratedLufs &&
	                 shortRange.analysis.loudnessStatus == QStringLiteral("insufficient-duration") &&
	                 shortRange.analysis.truePeak && firstBlock.analysis.integratedLufs,
	             "integrated loudness requires a complete 400 ms block without padding a short selection");
	const auto legacyRate = tone(1, 11025, .8);
	const auto roundedShort = analyzeAudioClip(legacyRate, 0, 4410);
	const auto roundedBlock = analyzeAudioClip(legacyRate, 0, 4412);
	ok &= expect(roundedShort.succeeded() && roundedShort.analysis.loudnessBlockFrames == 4412 &&
	                 !roundedShort.analysis.integratedLufs && roundedShort.analysis.loudnessMessage.contains(QStringLiteral("4412")) &&
	                 roundedBlock.succeeded() && roundedBlock.analysis.integratedLufs,
	             "legacy sample rates report exact rounded block frames rather than treating nominal 400 ms as sufficient");
	AudioClip surrounded{1, 48000, QVector<float>(4800, 10)};
	surrounded.samples += mono.samples;
	surrounded.samples += QVector<float>(4800, -10);
	const auto selected = analyzeAudioClip(surrounded, 4800, 52800);
	ok &= expect(selected.succeeded() && near(selected.analysis.integratedLufs, *measured.analysis.integratedLufs, 1e-12) &&
	                 near(selected.analysis.truePeak, *measured.analysis.truePeak, 1e-12),
	             "selection metering resets filter state and excludes surrounding loud samples");
	const auto quiet = analyzeAudioClip(tone(1, 48000, 1, 1e-6));
	ok &= expect(quiet.succeeded() && !quiet.analysis.integratedLufs && quiet.analysis.truePeak &&
	                 quiet.analysis.loudnessStatus == QStringLiteral("below-gate") &&
	                 audioAnalysisJson(quiet.analysis).value(QStringLiteral("loudness")).toObject().value(QStringLiteral("integratedLufs")).isNull(),
	             "below-gate loudness is explicit and JSON never substitutes zero LUFS or non-finite values");
	const auto silence = analyzeAudioClip({1, 48000, QVector<float>(48000, 0)});
	ok &= expect(silence.succeeded() && silence.analysis.truePeak == 0 && !silence.analysis.integratedLufs &&
	                 audioAnalysisJson(silence.analysis).value(QStringLiteral("truePeakDbtp")).isNull(),
	             "silence has zero linear true peak, null dBTP and no gated programme loudness");
	const auto lowRate = analyzeAudioClip({1, 1, {1, -1}});
	ok &= expect(lowRate.succeeded() && lowRate.analysis.peak == 1 && !lowRate.analysis.truePeak &&
	                 lowRate.analysis.loudnessStatus == QStringLiteral("unsupported-sample-rate"),
	             "low sample rates retain exact sample statistics with explicit meter availability");
	AudioClip surround = tone(6, 48000, 1);
	const AudioAnalysisOptions map{{AudioChannelRole::Left, AudioChannelRole::Right, AudioChannelRole::Center,
	    AudioChannelRole::LowFrequency, AudioChannelRole::LeftSurround, AudioChannelRole::RightSurround}};
	const auto unknown = analyzeAudioClip(surround);
	const auto skipped = analyzeAudioClip(surround, 0, -1, {}, {{}, false});
	ok &= expect(skipped.succeeded() && skipped.analysis.truePeak && !skipped.analysis.integratedLufs &&
	                 skipped.analysis.loudnessStatus == QStringLiteral("disabled") && !skipped.analysis.loudnessMessage.isEmpty(),
	             "intentionally skipped loudness is distinct from an unknown speaker layout");
	const auto mapped = analyzeAudioClip(surround, 0, -1, {}, map);
	ok &= expect(unknown.succeeded() && unknown.analysis.truePeak && !unknown.analysis.integratedLufs &&
	                 unknown.analysis.loudnessStatus == QStringLiteral("channel-map-required") &&
	                 mapped.succeeded() && near(mapped.analysis.integratedLufs, *measured.analysis.integratedLufs + 10 * std::log10(3 + 2 * 1.41), 1e-7),
	             "surround loudness requires reviewed roles, ignores LFE and applies surround energy weights");
	const auto eight = analyzeAudioClip(tone(8, 48000, 1), 0, -1, {},
	    {{AudioChannelRole::Left, AudioChannelRole::Right, AudioChannelRole::Center, AudioChannelRole::LowFrequency,
	      AudioChannelRole::LeftBack, AudioChannelRole::RightBack, AudioChannelRole::LeftSurround, AudioChannelRole::RightSurround}});
	ok &= expect(eight.succeeded() && near(eight.analysis.integratedLufs, *measured.analysis.integratedLufs + 10 * std::log10(5 + 2 * 1.41), 1e-7),
	             "eight-channel speaker weights distinguish back positions from side surrounds");
	for (qint64 frame = 0; frame < surround.frameCount(); ++frame) {
		for (int channel = 0; channel < 6; ++channel) { if (channel != 3) { surround.samples[frame * 6 + channel] = 0; } }
	}
	const auto lfeOnly = analyzeAudioClip(surround, 0, -1, {}, map);
	ok &= expect(lfeOnly.succeeded() && !lfeOnly.analysis.integratedLufs && near(lfeOnly.analysis.truePeak, .1, .003),
	             "LFE remains visible in true peak even though it contributes no programme loudness");
	ok &= expect(!analyzeAudioClip(mono, 0, -1, {}, map).succeeded() &&
	                 !analyzeAudioClip(tone(2, 48000, .1), 0, -1, {}, {{AudioChannelRole::Left, AudioChannelRole::Left}}).succeeded() &&
	                 !analyzeAudioClip(mono, 0, -1, {}, {{static_cast<AudioChannelRole>(500)}}).succeeded(),
	             "incompatible maps and invalid or repeated speaker roles fail explicitly");
	AudioClip extremes{2, 48000, {}};
	for (int frame = 0; frame < 48; ++frame) {
		extremes.samples << (frame % 2 ? -1 : 1) * std::numeric_limits<float>::max()
		                 << (frame % 2 ? -1 : 1) * std::numeric_limits<float>::denorm_min();
	}
	const auto headroom = analyzeAudioClip(extremes);
	ok &= expect(headroom.succeeded() && headroom.analysis.truePeak && std::isfinite(*headroom.analysis.truePeak) &&
	                 headroom.analysis.channels[1].truePeak && *headroom.analysis.channels[1].truePeak > 0,
	             "double-precision reconstruction preserves finite headroom and tiny-channel true peak without float overflow");
	extremes.samples.clear();
	for (int frame = 0; frame < 19200; ++frame) {
		extremes.samples << (frame % 2 ? -1 : 1) * std::numeric_limits<float>::max()
		                 << (frame % 2 ? -1 : 1) * std::numeric_limits<float>::denorm_min();
	}
	const auto loudHeadroom = analyzeAudioClip(extremes);
	ok &= expect(loudHeadroom.succeeded() && loudHeadroom.analysis.integratedLufs &&
	                 std::isfinite(*loudHeadroom.analysis.integratedLufs) && *loudHeadroom.analysis.integratedLufs > 700,
	             "full-block finite float headroom remains finite through K-weighting and energy summation");
	for (const int cancelPoll : {32, 65}) {
		int polls = 0;
		const auto cancelled = analyzeAudioClip(mono, 0, -1, {[&]() { return ++polls >= cancelPoll; }});
		ok &= expect(cancelled.cancelled && !cancelled.succeeded() && cancelled.analysis.channels.isEmpty(),
		             "cancellation during either metering pass discards the entire partial report");
	}
	std::vector<std::future<AudioAnalysisResult>> concurrent;
	for (int index = 0; index < 4; ++index) {
		concurrent.emplace_back(std::async(std::launch::async, [mono]() { return analyzeAudioClip(mono); }));
	}
	for (auto& work : concurrent) {
		const auto result = work.get();
		ok &= expect(result.succeeded() && near(result.analysis.integratedLufs, *measured.analysis.integratedLufs, 1e-12),
		             "simultaneous state initialization and measurement remain deterministic");
	}
	if (argc > 1) {
		const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
		QDir().mkpath(root);
		QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-loudness-XXXXXX")));
		if (!temporary.isValid()) { return EXIT_FAILURE; }
		const QString source = QDir(temporary.path()).filePath(QStringLiteral("source.vsaudio"));
		ok &= expect(writeAudioProject({surround, 0, surround.frameCount(), QStringLiteral("LFE fixture"), {}, {}},
		                               {source, false, false, {}, {}}).succeeded, "write original multichannel fixture");
		QFile file(source);
		if (!expect(file.open(QIODevice::ReadOnly), "read the original loudness fixture")) { return EXIT_FAILURE; }
		const auto before = file.readAll(); file.close();
		QByteArray output;
		const auto cli = [&](const QStringList& options, int expected) {
			QProcess process; process.setWorkingDirectory(temporary.path());
			QStringList arguments{QStringLiteral("--cli"), QStringLiteral("asset"), QStringLiteral("audio-analyze"), source,
			    QStringLiteral("--json")};
			arguments += options;
			process.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), arguments);
			const bool finished = process.waitForFinished(30000);
			if (!finished) { process.kill(); process.waitForFinished(); }
			output = process.readAllStandardOutput();
			return finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected;
		};
		ok &= expect(cli({QStringLiteral("--channel-map"), QStringLiteral("L,R,C,LFE,Ls,Rs")}, 0), "CLI accepts explicit source-order speaker roles");
		const auto report = QJsonDocument::fromJson(output).object().value(QStringLiteral("audioAnalysis")).toObject();
		ok &= expect(report.value(QStringLiteral("loudness")) == audioAnalysisJson(lfeOnly.analysis).value(QStringLiteral("loudness")),
		             "CLI uses the shared layout-aware loudness report");
		ok &= expect(cli({QStringLiteral("--channel-map"), QStringLiteral("wrong")}, 2) &&
		                 cli({QStringLiteral("--channel-map"), QStringLiteral("L,R")}, 4),
		             "invalid role identifiers and mismatched channel counts have distinct usage/validation exits");
		ok &= expect(cli({QStringLiteral("--no-loudness")}, 0) &&
		                 QJsonDocument::fromJson(output).object().value(QStringLiteral("audioAnalysis")).toObject()
		                     .value(QStringLiteral("loudness")).toObject().value(QStringLiteral("status")) == QStringLiteral("disabled") &&
		                 cli({QStringLiteral("--no-loudness"), QStringLiteral("--channel-map"), QStringLiteral("L,R,C,LFE,Ls,Rs")}, 2),
		             "CLI can explicitly skip loudness and rejects contradictory speaker-map options");
		ok &= expect(file.open(QIODevice::ReadOnly), "reopen the unchanged loudness fixture");
		ok &= expect(file.readAll() == before && !QFileInfo::exists(source + QStringLiteral(".lock")),
		             "loudness analysis never changes or locks the input project");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
