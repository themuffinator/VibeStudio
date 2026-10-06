#include "core/audio_session_io.h"
#include "tests/audio_arrangement_fixture.h"
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
	QTemporaryDir temporary(QDir(root).filePath("arrangement-cli-XXXXXX"));
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
	auto source = test::arrangementFixture(false);
	source.sources[0].audio.sourcePath = file("protected.wav");
	const auto native = file("session.vssession");
	bool ok = expect(write(native, encodeAudioSession(source)) && write(file("protected.wav"), "protected source"),
	                 "create private native fixture");
	if (!ok)
		return EXIT_FAILURE;
	const auto original = read(native);
	const QStringList destination{"--output", native, "--overwrite"};
	const auto arrange = [&](const QStringList &arguments, int expected = 0) {
		return run(QStringList{"arrange", native} + arguments + destination, expected);
	};
	ok &= expect(run({"arrange", native, "--operation", "group", "--select-clip", "a", "--select-clip", "b", "--name",
	                  "Rhythm pair", "--output", file("dry.vssession"), "--dry-run"}) &&
	                 read(native) == original && !QFile::exists(file("dry.vssession")),
	             "dry-run previews group edits without writes");
	ok &= expect(arrange({"--operation", "group", "--select-clip", "b", "--select-clip", "a", "--name", "Rhythm pair"}),
	             "CLI creates a named cross-track group");
	const auto groupId = last["groupId"].toString();
	ok &= expect(!groupId.isEmpty() && last["selection"].toArray() == QJsonArray({"a", "b"}),
	             "CLI reports canonical effective selection and group identity");
	ok &= expect(arrange({"--operation", "move", "--select-clip", "b", "--offset-frames", "20"}),
	             "CLI expands a linked move from one member");
	AudioSession moved;
	QString error;
	if (!expect(readAudioSession(native, &moved, nullptr, &error), "reopen moved session"))
		return EXIT_FAILURE;
	ok &= expect(moved.tracks[0].regions[0].position == 40 && moved.tracks[1].regions[0].position == 60 &&
	                 moved.tracks[2].regions[0].position == 140,
	             "CLI move preserves relative timing and other clips");
	const auto beforeSplit = renderAudioSession(moved);
	ok &= expect(arrange({"--operation", "split", "--select-clip", "a", "--at-frame", "80"}) &&
	                 last["addedClipIds"].toArray().size() == 2,
	             "CLI grouped split returns right-side clip identities");
	const auto rightId = last["addedClipIds"].toArray()[0].toString();
	AudioSession split;
	ok &= expect(readAudioSession(native, &split, nullptr, &error) && split.groups.size() == 2 &&
	                 renderAudioSession(split).clip.samples == beforeSplit.clip.samples,
	             "CLI grouped split retains every audible sample");
	AudioWavOptions encoding;
	encoding.format = AudioWavFormat::Float32;
	ok &= expect(run({"mixdown", native, "--output", file("split.wav"), "--wav-format", "float32"}) &&
	                 read(file("split.wav")) == encodeAudioWav(beforeSplit.clip, encoding),
	             "actual CLI mixdown matches the original unsplit independent render");
	const auto keep = read(native);
	for (const auto &arguments :
	     {QStringList{"--operation", "move", "--select-clip", "a", "--offset-frames", "-41"},
	      QStringList{"--operation", "move", "--select-clip", "a", "--offset-frames",
	                  QString::number(AudioSessionFrameLimit)},
	      QStringList{"--operation", "mute", "--select-clip", "a", "--select-clip", "a", "--mute", "true"},
	      QStringList{"--operation", "remove", "--select-clip", "missing"},
	      QStringList{"--operation", "group", "--select-clip", "c", "--name", "Only one"},
	      QStringList{"--operation", "fades", "--select-clip", "a", "--fade-in", "1000", "--fade-out", "0"}})
		ok &= expect(arrange(arguments, 4) && read(native) == keep,
		             "invalid batch edit leaves the guarded source bytes intact");
	for (const auto &arguments : {QStringList{"--operation", "move", "--offset-frames", "1"},
	                              QStringList{"--operation", "move", "--select-clip", "a", "--offset-frames", "1.5"},
	                              QStringList{"--operation", "remove", "--select-clip", "a", "--db", "0"},
	                              QStringList{"--operation", "mute", "--select-clip", "a", "--mute", "yes"},
	                              QStringList{"--operation", "gain", "--select-clip", "a", "--db", "0", "--db", "0"},
	                              QStringList{"--operation", "group", "--select-clip", "a"}})
		ok &= expect(arrange(arguments, 2) && read(native) == keep,
		             "invalid or inapplicable CLI options fail before writing");
	ok &= expect(run({"arrange", native, "--operation", "move", "--select-clip", "a", "--offset-frames", "1",
	                  "--output", native},
	                 2) &&
	                 read(native) == keep,
	             "in-place edits still require explicit overwrite");
	ok &= expect(run({"arrange", native, "--operation", "move", "--select-clip", "a", "--offset-frames", "1",
	                  "--output", file("protected.wav"), "--overwrite"},
	                 4) &&
	                 read(file("protected.wav")) == "protected source",
	             "arrangement export cannot overwrite embedded media provenance");
	ok &= expect(arrange({"--operation", "duplicate", "--select-clip", rightId, "--offset-frames", "200"}) &&
	                 last["addedClipIds"].toArray().size() == 2,
	             "CLI duplicates an inherited fade group with a new group identity");
	const auto copy = last["addedClipIds"].toArray()[0].toString();
	ok &= expect(arrange({"--operation", "gain", "--select-clip", copy, "--db", "-6"}) &&
	                 arrange({"--operation", "mute", "--select-clip", copy, "--mute", "true"}) &&
	                 arrange({"--operation", "fades", "--select-clip", copy, "--fade-in", "4", "--fade-out", "3"}),
	             "CLI exposes gain, mute and fade batch authoring");
	ok &= expect(run(QStringList{"edit", native, "--operation", "region", "--track", "ta", "--clip", rightId,
	                             "--fade-in", "0", "--fade-out", "0", "--reset-fades", "true"} +
	                 destination),
	             "precise single-clip editing exposes an explicit inherited-envelope reset");
	ok &= expect(arrange({"--operation", "ungroup", "--select-clip", copy, "--linked-groups", "false"}),
	             "CLI can release a single group member");
	ok &= expect(arrange({"--operation", "remove", "--select-clip", "a"}),
	             "CLI removes a linked selection transactionally");
	std::cout << (ok ? "Arrangement CLI verification passed\n" : "Arrangement CLI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
