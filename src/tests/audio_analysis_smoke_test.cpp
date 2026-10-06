#include "core/audio_analysis.h"
#include "core/audio_export.h"
#include "core/audio_project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>

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
bool near(double actual, double expected, double tolerance = 1e-12) { return std::abs(actual - expected) <= tolerance; }
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	const AudioClip fixture{1, 1000, {.25f, -.25f, 1, -1, 2, -2, 1.25f, 0, 3, 0}};
	const auto measured = analyzeAudioClip(fixture);
	ok &= expect(measured.succeeded() && measured.analysis.channels.size() == 1, "bounded valid audio can be measured");
	if (!measured.succeeded()) {
		return EXIT_FAILURE;
	}
	const auto& channel = measured.analysis.channels.first();
	ok &= expect(channel.peak == 3 && channel.peakFrame == 8 && channel.minimum == -2 && channel.maximum == 3 &&
	                 near(channel.dc, .425) && near(channel.rms, std::sqrt(20.6875 / 10)),
	             "peak, signed extrema, mean, and RMS match hand-calculated sample statistics");
	ok &= expect(channel.samplesAboveFullScale == 4 && channel.samplesAtFullScale == 2 &&
	                 channel.firstAboveFullScaleFrame == 4 && channel.longestAboveFullScaleRun == 3,
	             "strict over-range counts, exact endpoints, and consecutive runs remain distinct");
	const auto selected = analyzeAudioClip(fixture, 4, 7);
	ok &= expect(selected.succeeded() && selected.analysis.firstFrame == 4 && selected.analysis.endFrame == 7 &&
	                 selected.analysis.channels.first().peakFrame == 4 &&
	                 selected.analysis.channels.first().firstAboveFullScaleFrame == 4 &&
	                 selected.analysis.channels.first().longestAboveFullScaleRun == 3 &&
	                 near(selected.analysis.channels.first().dc, 1.25 / 3) &&
	                 near(selected.analysis.rms, std::sqrt(9.5625 / 3)),
	             "selection boundaries exclude surrounding frames and peak ties retain the earliest absolute position");
	const AudioClip stereo{2, 48000, {.25f, .5f, -.25f, .5f, .25f, .5f, -.25f, .5f}};
	const auto independent = analyzeAudioClip(stereo);
	ok &= expect(independent.succeeded() && independent.analysis.channels[0].dc == 0 &&
	                 independent.analysis.channels[0].rms == .25 && independent.analysis.channels[1].dc == .5 &&
	                 independent.analysis.channels[1].rms == .5 && near(independent.analysis.rms, std::sqrt(.15625)),
	             "RMS includes DC, keeps independent channels, and combines energy rather than signed samples");
	AudioClip eight{8, 48000, {}};
	for (int frame = 0; frame < 100; ++frame) {
		for (int index = 0; index < 8; ++index) {
			eight.samples << float(index + 1) / 16;
		}
	}
	const auto multichannel = analyzeAudioClip(eight, 9, 50);
	ok &= expect(multichannel.succeeded() && multichannel.analysis.channels.size() == 8,
	             "all supported channels are analyzed");
	for (int index = 0; index < multichannel.analysis.channels.size(); ++index) {
		const auto& stats = multichannel.analysis.channels[index];
		ok &= expect(stats.dc == double(index + 1) / 16 && stats.rms == stats.dc && stats.peak == stats.dc &&
		                 stats.peakFrame == 9 && stats.samplesAboveFullScale == 0,
		             "channel order and independently calculated constant-channel statistics are preserved");
	}
	AudioClip sine{1, 48000, {}};
	for (int frame = 0; frame < 48000; ++frame) {
		sine.samples << float(.75 * std::sin(2 * std::numbers::pi * 1000 * frame / 48000));
	}
	const auto tone = analyzeAudioClip(sine);
	ok &= expect(tone.succeeded() && near(tone.analysis.channels.first().rms, .75 / std::sqrt(2.0), 2e-8) &&
	                 std::abs(tone.analysis.channels.first().dc) < 1e-10 && tone.analysis.peak == .75,
	             "a periodic sine has independently predicted RMS, peak, and zero DC");
	const float largest = std::numeric_limits<float>::max(), tiny = std::numeric_limits<float>::denorm_min();
	for (const float amplitude : {largest, tiny}) {
		const AudioClip extreme{1, 1, {amplitude, -amplitude, amplitude, -amplitude}};
		const auto stats = analyzeAudioClip(extreme);
		ok &= expect(stats.succeeded() && std::isfinite(stats.analysis.rms) &&
		                 std::abs(stats.analysis.rms / double(amplitude) - 1) < 1e-14 &&
		                 stats.analysis.channels.first().dc == 0,
		             "finite float extremes neither overflow the RMS nor underflow away tiny signal energy");
	}
	const AudioClip silence{1, 1, {0.0f, -0.0f}};
	const auto silent = analyzeAudioClip(silence);
	const auto silentJson = audioAnalysisJson(silent.analysis);
	ok &= expect(silent.succeeded() && silent.analysis.rms == 0 && silent.analysis.channels.first().peakFrame == 0 &&
	                 silentJson.value(QStringLiteral("peakDbfs")).isNull() &&
	                 silentJson.value(QStringLiteral("rmsDbfs")).isNull(),
	             "silence reports zero linear levels and null JSON decibels rather than non-finite numbers");
	const auto empty = analyzeAudioClip({2, 44100, {}});
	const auto cursor = analyzeAudioClip(fixture, 3, 3);
	ok &= expect(empty.succeeded() && empty.analysis.channels.size() == 2 && cursor.succeeded() &&
	                 cursor.analysis.channels.first().peakFrame == -1 && cursor.analysis.peak == 0 &&
	                 audioAnalysisJson(empty.analysis)
	                     .value(QStringLiteral("channels"))
	                     .toArray()
	                     .first()
	                     .toObject()
	                     .value(QStringLiteral("peakFrame"))
	                     .isNull(),
	             "empty documents and empty ranges have no peak position or over-range event");
	for (const auto& range : QVector<QPair<qint64, qint64>>{{-1, 4}, {1, -2}, {5, 4}, {0, 11}, {11, 11}}) {
		ok &= expect(!analyzeAudioClip(fixture, range.first, range.second).succeeded(),
		             "out-of-bounds or inverted analysis ranges fail explicitly");
	}
	ok &= expect(!analyzeAudioClip({0, 44100, {}}).succeeded() && !analyzeAudioClip({2, 44100, {0}}).succeeded() &&
	                 !analyzeAudioClip({1, 0, {0}}).succeeded() &&
	                 !analyzeAudioClip({1, 44100, {0, std::numeric_limits<float>::infinity()}}, 0, 1).succeeded(),
	             "invalid format, incomplete frames, and non-finite samples outside the range are rejected");
	int polls = 0;
	const auto cancelled = analyzeAudioClip(sine, 0, -1, {[&]() { return ++polls >= 17; }});
	ok &= expect(cancelled.cancelled && !cancelled.succeeded() && cancelled.analysis.channels.isEmpty(),
	             "cancellation during measurement cannot expose a partial report");
	ok &= expect(analyzeAudioClip(fixture, 0, -1, {[]() { return true; }}).cancelled,
	             "already-cancelled work returns without starting analysis");
	const auto json = audioAnalysisJson(measured.analysis);
	ok &= expect(json.value(QStringLiteral("schemaVersion")).toInt() == 1 &&
	                 json.value(QStringLiteral("samplesAboveFullScale")).toInteger() == 4 &&
	                 near(json.value(QStringLiteral("peakDbfs")).toDouble(), 20 * std::log10(3.0)) &&
	                 QJsonDocument::fromJson(QJsonDocument(json).toJson()).object() == json,
	             "the stable JSON report round-trips all finite statistics and positions");
	if (argc > 1) {
		const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
		QDir().mkpath(root);
		QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-analysis-XXXXXX")));
		if (!temporary.isValid()) {
			return EXIT_FAILURE;
		}
		const QString native = QDir(temporary.path()).filePath(QStringLiteral("source.vsaudio"));
		ok &= expect(writeAudioProject({fixture, 2, 7, QStringLiteral("levels"), {}, {}},
		                               {native, false, false, {}, {}}).succeeded,
		             "write independent native analysis fixture");
		const QByteArray original = read(native);
		QByteArray output;
		const auto cli = [&](const QStringList& args, int expected) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), QStringList{QStringLiteral("--cli"), QStringLiteral("asset"),
			                                                           QStringLiteral("audio-analyze")} +
			                                                   args + QStringList{QStringLiteral("--json")});
			const bool finished = process.waitForFinished(30000);
			if (!finished) {
				process.kill();
				process.waitForFinished();
			}
			output = process.readAllStandardOutput();
			if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != expected) {
				std::cerr << output.constData() << process.readAllStandardError().constData();
				return false;
			}
			return true;
		};
		ok &= expect(cli({native}, 0), "CLI measures a native project without writing");
		auto report = QJsonDocument::fromJson(output).object().value(QStringLiteral("audioAnalysis")).toObject();
		report.remove(QStringLiteral("input"));
		report.remove(QStringLiteral("source"));
		ok &= expect(report.take(QStringLiteral("importWarnings")) == QJsonArray{},
		             "analysis of a native document without imports has no import warnings");
		ok &= expect(report == json, "CLI defaults to the whole sound and shares every core report field");
		ok &= expect(cli({native, QStringLiteral("--start-frame"), QStringLiteral("4"), QStringLiteral("--end-frame"),
		                  QStringLiteral("7")},
		                 0) &&
		                 QJsonDocument::fromJson(output)
		                         .object()
		                         .value(QStringLiteral("audioAnalysis"))
		                         .toObject()
		                         .value(QStringLiteral("samplesAboveFullScale"))
		                         .toInt() == 3,
		             "CLI explicit frame selection uses the exact core range");
		const QString wav = QDir(temporary.path()).filePath(QStringLiteral("float.wav"));
		QString error;
		ok &= expect(saveAudioWav(fixture, {AudioWavFormat::Float32}, wav, false, {}, &error) &&
		                 cli({temporary.path(), QStringLiteral("--entry"), QStringLiteral("float.wav")}, 0),
		             "CLI analyzes package entries through the shared native decoder");
		ok &= expect(cli({native, QStringLiteral("--start-frame"), QStringLiteral("no")}, 2) &&
		                 cli({native, QStringLiteral("--end-frame"), QStringLiteral("11")}, 4) &&
		                 cli({native, QStringLiteral("--output"), wav, QStringLiteral("--overwrite")}, 2) &&
		                 cli({QDir(temporary.path()).filePath(QStringLiteral("missing.wav"))}, 1),
		             "usage, invalid ranges, write-option mistakes, and unreadable files have distinct exit codes");
		ok &= expect(read(native) == original && !QFileInfo::exists(native + QStringLiteral(".lock")),
		             "analysis leaves native samples, selection, metadata, and file bytes unchanged without locking");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
