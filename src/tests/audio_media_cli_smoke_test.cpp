#include "core/audio_media.h"
#include "core/audio_session_io.h"
#include "tests/audio_media_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
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
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("media-cli-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return temporary.filePath(QLatin1String(name)); };
	QJsonObject last;
	const auto run = [&](const QStringList &arguments, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file",
		                                                           path("settings.ini"), "asset", "audio-session"} +
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
	auto source = test::mediaFixture();
	source.sources[0].audio.sourcePath = path("missing.wav");
	source.sources[1].audio.sourcePath = path("unused.vssession");
	const auto original = encodeAudioSession(source);
	const auto native = path("song.vssession"), destination = path("changed.vssession");
	AudioWavOptions options;
	options.format = AudioWavFormat::Float32;
	const auto wav = encodeAudioWav(source.sources[0].audio.clip, options);
	auto replacement = source.sources[0].audio;
	for (auto &sample : replacement.clip.samples)
		sample *= 0.25f;
	const auto replacementBytes = encodeAudioProject(replacement);
	bool ok = expect(test::writeMediaFixture(native, original) &&
	                     test::writeMediaFixture(path("unused.vssession"), "protected unused file") &&
	                     test::writeMediaFixture(path("identical.wav"), wav) &&
	                     test::writeMediaFixture(path("replacement.vssession"), replacementBytes),
	                 "write private CLI media fixtures");
	if (!ok)
		return EXIT_FAILURE;
	ok &= expect(run({"media", native, "--operation", "inspect"}) &&
	                 last["media"].toObject()["sources"].toArray().size() == 2 &&
	                 last["media"].toObject()["unusedSampleBytes"].toInteger() == 1024,
	             "CLI read-only media inventory exposes usage and unused memory");
	const auto edit = [&](const QStringList &args, int expected = 0) {
		return run(QStringList{"media", native} + args + QStringList{"--output", destination, "--overwrite"}, expected);
	};
	ok &= expect(
	    edit({"--operation", "replace", "--source", "media", "--input", path("replacement.vssession"), "--dry-run"}) &&
	        !QFile::exists(destination) && last["mediaEdit"].toObject()["affectedClipIds"].toArray().size() == 3,
	    "replacement dry-run reports all affected clips and writes nothing");
	const auto digest = last["mediaEdit"].toObject()["inputSha256"].toString();
	ok &= expect(digest.size() == 64 && edit({"--operation", "replace", "--source", "media", "--input",
	                                          path("replacement.vssession"), "--expected-sha256", digest}),
	             "reviewed digest authorizes exactly that input revision");
	AudioSession changed;
	QString error;
	ok &= expect(readAudioSession(destination, &changed, nullptr, &error) && changed.sources[0].id != "media" &&
	                 changed.sources[0].audio.clip.samples == replacement.clip.samples,
	             "CLI persists replacement under a new immutable source ID");
	auto reference = source;
	reference.sources[0].audio.clip = replacement.clip;
	const auto rendered = renderAudioSession(reference, 0, 300);
	ok &= expect(run({"mixdown", destination, "--output", path("mix.wav"), "--start-frame", "0", "--end-frame", "300",
	                  "--wav-format", "float32"}) &&
	                 test::readMediaFixture(path("mix.wav")) == encodeAudioWav(rendered.clip, options),
	             "actual exported WAV matches core rendering after replacement");
	const auto published = test::readMediaFixture(destination);
	ok &= expect(edit({"--operation", "replace", "--source", "media", "--input", path("replacement.vssession"),
	                   "--expected-sha256", QString(64, '0')},
	                  4) &&
	                 test::readMediaFixture(destination) == published,
	             "stale reviewed digest cannot replace an existing output");
	for (const auto &args :
	     {QStringList{"--operation", "prune", "--source", "unused"}, QStringList{"--operation", "inspect"},
	      QStringList{"--operation", "remove", "--source", "unused", "--resample"},
	      QStringList{"--operation", "rename", "--source", "media"},
	      QStringList{"--operation", "rename", "--source", "media", "--name", "x", "--input", "x"},
	      QStringList{"--operation", "relink", "--source", "media", "--input", path("identical.wav"), "--resample"},
	      QStringList{"--operation", "replace", "--source", "media", "--input", path("identical.wav"),
	                  "--expected-sha256", "broken"},
	      QStringList{"--operation", "future"}})
		ok &= expect(edit(args, 2) && test::readMediaFixture(destination) == published,
		             "strict CLI rejects missing or inapplicable media options before output");
	for (const auto &args :
	     {QStringList{"--operation", "remove", "--source", "media"},
	      QStringList{"--operation", "remove", "--source", "unused", "--source", "unused"},
	      QStringList{"--operation", "remove", "--source", "unused,media"},
	      QStringList{"--operation", "relink", "--source", "media", "--input", path("replacement.vssession")}})
		ok &= expect(edit(args, 4),
		             "shared validation rejects used/duplicate/literal unknown sources and differing relink audio");
	ok &=
	    expect(run({"media", native, "--operation", "prune", "--output", path("unused.vssession"), "--overwrite"}, 4) &&
	               test::readMediaFixture(path("unused.vssession")) == "protected unused file",
	           "prune retains source-file protection for removed provenance");
	ok &= expect(run({"media", native, "--operation", "replace", "--source", "media", "--input",
	                  path("replacement.vssession"), "--output", path("replacement.vssession"), "--overwrite"},
	                 4) &&
	                 test::readMediaFixture(path("replacement.vssession")) == replacementBytes,
	             "replacement cannot overwrite its own reviewed input, even with a native output suffix");
	ok &= expect(edit({"--operation", "relink", "--source", "media", "--input", path("identical.wav")}) &&
	                 last["mediaEdit"].toObject()["identicalSamples"].toBool(),
	             "CLI supports safe identical relink");
	ok &= expect(edit({"--operation", "rename", "--source", "media", "--name", "CLI source"}) &&
	                 readAudioSession(destination, &changed, nullptr, &error) &&
	                 changed.sources[0].audio.sourceName == "CLI source",
	             "CLI renames shared media");
	ok &= expect(edit({"--operation", "remove", "--source", "unused"}) &&
	                 readAudioSession(destination, &changed, nullptr, &error) && changed.sources.size() == 1,
	             "CLI removes an unused source");
	ok &= expect(run({"media", native, "--operation", "prune", "--output", native, "--overwrite"}) &&
	                 readAudioSession(native, &changed, nullptr, &error) && changed.sources.size() == 1,
	             "native in-place edit retains revision-checked save behavior");
	std::cout << (ok ? "Media CLI verification passed\n" : "Media CLI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
