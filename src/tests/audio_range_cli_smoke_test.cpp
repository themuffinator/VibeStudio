#include "core/audio_session_io.h"
#include "tests/audio_range_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
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
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("range-cli-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto file = [&](const char *name) { return temporary.filePath(QLatin1String(name)); };
	QJsonObject last;
	const auto run = [&](const QStringList &arguments, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file",
		                                                           file("settings.ini"), "asset", "audio-session"} +
		                                                   arguments);
		if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto output = process.readAllStandardOutput();
		last = QJsonDocument::fromJson(output).object();
		const bool passed =
		    process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && !last.isEmpty();
		if (!passed)
			std::cerr << arguments.join(' ').toStdString() << '\n'
			          << output.constData() << process.readAllStandardError().constData();
		return passed;
	};
	auto source = test::rangeFixture(true);
	source.sources[0].audio.sourcePath = file("protected.wav");
	const auto native = file("session.vssession"), destination = file("edited.vssession");
	const auto bytes = encodeAudioSession(source);
	bool ok =
	    expect(write(native, bytes) && write(file("protected.wav"), "protected source"), "write private range fixture");
	if (!ok)
		return EXIT_FAILURE;
	const auto range = [&](const QStringList &args, int expected = 0) {
		return run(QStringList{"range", native, "--start-frame", "60", "--end-frame", "83"} + args +
		               QStringList{"--output", destination, "--overwrite"},
		           expected);
	};
	ok &= expect(range({"--operation", "repeat", "--all-tracks", "--dry-run"}) && !QFile::exists(destination) &&
	                 read(native) == bytes,
	             "range dry-run returns a proposed edit without publication");
	ok &= expect(last["rangeEdit"].toObject()["tracks"].toArray().size() == 4 &&
	                 last["rangeEdit"].toObject()["first"].toInteger() == 83 &&
	                 last["rangeEdit"].toObject()["end"].toInteger() == 106,
	             "JSON reports canonical track scope and repeated section bounds");
	for (const auto &operation : {"clear", "ripple-delete", "insert-silence", "repeat"}) {
		ok &= expect(range({"--operation", operation, "--all-tracks"}), "CLI exposes every range operation");
		AudioSession edited;
		QString error;
		if (!expect(readAudioSession(destination, &edited, nullptr, &error), "read CLI range result"))
			return EXIT_FAILURE;
		const auto expected = editAudioRange(source, test::rangeEdit(QLatin1String(operation)));
		const auto expectedAudio = renderAudioSession(expected.session, 0, 280);
		ok &= expect(expected.succeeded() && expectedAudio.succeeded() &&
		                 edited.masterEffectAutomation == expected.session.masterEffectAutomation &&
		                 edited.musicalTime == source.musicalTime &&
		                 edited.tracks[0].gainAutomation == expected.session.tracks[0].gainAutomation &&
		                 renderAudioSession(edited, 0, 280).clip.samples == expectedAudio.clip.samples,
		             "CLI/core parity for curves and audible range mapping");
		const auto output = temporary.filePath(QString::fromLatin1(operation) + ".wav");
		AudioWavOptions encoding;
		encoding.format = AudioWavFormat::Float32;
		ok &= expect(run({"mixdown", destination, "--start-frame", "0", "--end-frame", "280", "--output", output,
		                  "--wav-format", "float32"}) &&
		                 read(output) == encodeAudioWav(expectedAudio.clip, encoding),
		             "actual CLI WAV matches exact shared range render");
	}
	ok &= expect(range({"--operation", "repeat", "--select-track", "ta", "--select-track", "tc", "--follow-automation",
	                    "false"}),
	             "CLI supports a repeated, explicit track scope and sample-anchored envelopes");
	AudioSession scoped;
	QString error;
	ok &= expect(readAudioSession(destination, &scoped, nullptr, &error) &&
	                 scoped.tracks[0].gainAutomation == source.tracks[0].gainAutomation &&
	                 scoped.masterEffectAutomation == source.masterEffectAutomation &&
	                 scoped.tracks[1].regions[0].position == source.tracks[1].regions[0].position,
	             "group links cannot broaden a CLI range scope");
	const auto keep = read(destination);
	for (const auto &args : {QStringList{"--operation", "repeat"},
	                         QStringList{"--operation", "repeat", "--all-tracks", "--select-track", "ta"},
	                         QStringList{"--operation", "clear", "--all-tracks", "--follow-automation", "true"},
	                         QStringList{"--operation", "repeat", "--all-tracks", "--db", "-3"},
	                         QStringList{"--operation", "repeat", "--all-tracks=true"},
	                         QStringList{"--operation", "repeat", "--all-tracks", "--follow-automation", "maybe"},
	                         QStringList{"--operation", "future", "--all-tracks"}})
		ok &= expect(range(args, 2) && read(destination) == keep,
		             "strict range CLI rejects ambiguous or inapplicable options before writes");
	for (const auto &args :
	     {QStringList{"--operation", "repeat", "--select-track", "missing"},
	      QStringList{"--operation", "repeat", "--select-track", "ta", "--select-track", "ta"},
	      QStringList{"--operation", "repeat", "--select-track", "ta", "--master-automation", "true"},
	      QStringList{"--operation", "repeat", "--all-tracks", "--follow-automation", "false", "--master-automation",
	                  "true"}})
		ok &= expect(range(args, 4) && read(destination) == keep, "invalid range state is transactional");
	ok &= expect(run({"range", native, "--operation", "repeat", "--all-tracks", "--start-frame", "60", "--end-frame",
	                  "83", "--output", destination},
	                 4) &&
	                 read(destination) == keep,
	             "existing range output requires overwrite authorization");
	ok &= expect(run({"range", native, "--operation", "repeat", "--all-tracks", "--start-frame", "60", "--end-frame",
	                  "83", "--output", file("protected.wav"), "--overwrite"},
	                 4) &&
	                 read(file("protected.wav")) == "protected source",
	             "range edits retain embedded media provenance protection");
	ok &= expect(run({"range", native, "--operation", "ripple-delete", "--all-tracks", "--start-frame", "60",
	                  "--end-frame", "83", "--master-automation", "false", "--output", native, "--overwrite"}) &&
	                 readAudioSession(native, &scoped, nullptr, &error) &&
	                 scoped.masterEffectAutomation == source.masterEffectAutomation,
	             "guarded in-place save can explicitly retain master curves");
	std::cout << (ok ? "Range CLI verification passed\n" : "Range CLI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
