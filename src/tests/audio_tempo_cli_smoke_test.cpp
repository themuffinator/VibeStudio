#include "core/audio_session_io.h"
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
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("tempo-cli-XXXXXX"));
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
	bool ok = expect(run({"new", "--tempo", "120", "--meter", "4/4", "--tempo-points", "3840:60,9600:180",
	                      "--meter-points", "3:7/8,5:3/4", "--output", file("map.vssession")}),
	                 "CLI creates complete tempo and meter map");
	const auto initial = read(file("map.vssession"));
	if (!ok)
		return EXIT_FAILURE;
	ok &= expect(run({"position", file("map.vssession"), "--at-position", "4.1.0"}) &&
	                 last["musicalPosition"].toObject()["frame"].toInteger() == 408000 &&
	                 last["musicalPosition"].toObject()["tick"].toInteger() == 11040,
	             "CLI musical conversion matches independent expected frame and tick");
	ok &= expect(run({"position", file("map.vssession"), "--at-tick", "14400"}) &&
	                 last["musicalPosition"].toObject()["position"] == "5.1.0" &&
	                 last["musicalPosition"].toObject()["frame"].toInteger() == 464000,
	             "CLI tick conversion crosses meter and tempo changes");
	ok &= expect(run({"position", file("map.vssession"), "--at-frame", "120000", "--snap", "beat"}) &&
	                 last["musicalPosition"].toObject()["snappedFrame"].toInteger() == 144000 &&
	                 last["musicalPosition"].toObject()["previousFrame"].toInteger() == 96000 &&
	                 last["musicalPosition"].toObject()["nextFrame"].toInteger() == 144000 &&
	                 read(file("map.vssession")) == initial,
	             "CLI snapping is read-only and ties to the later actual grid frame");
	for (const auto &arguments : {QStringList{"--at-position", "4.8.0"},
	                              {"--at-position", "4.7.480"},
	                              {"--at-tick", "-1"},
	                              {"--at-frame", "12.5"},
	                              {"--at-frame", "0", "--at-tick", "0"},
	                              {"--at-position", "1.1.0", "--snap", "unknown"},
	                              {"--at-tick", QString::number(AudioMusicalTickLimit)},
	                              {"--at-frame", "0", "--output", file("unwanted.vssession")}})
		ok &= expect(run(QStringList{"position", file("map.vssession")} + arguments, 2),
		             "invalid or conflicting position arguments are rejected");
	ok &= expect(
	    run({"tempo-map", file("map.vssession"), "--tempo", "100", "--output", file("dry.vssession"), "--dry-run"}) &&
	        !QFile::exists(file("dry.vssession")) && read(file("map.vssession")) == initial,
	    "tempo edit dry-run preserves source and destination");
	for (const auto &arguments : {QStringList{"--tempo", "0"},
	                              {"--tempo", "401"},
	                              {"--meter", "7/3"},
	                              {"--tempo-points", "1:120,1:90"},
	                              {"--meter-points", "2:3/4,1:4/4"},
	                              {"--tempo-points", QString::number(AudioMusicalTickLimit) + ":120"}})
		ok &= expect(
		    run(QStringList{"tempo-map", file("map.vssession"), "--output", file("map.vssession"), "--overwrite"} +
		            arguments,
		        4) &&
		        read(file("map.vssession")) == initial,
		    "invalid tempo edits retain guarded destination bytes");
	ok &= expect(run({"tempo-map", file("map.vssession"), "--tempo", "90", "--meter", "3/8", "--tempo-points", "none",
	                  "--meter-points", "none", "--output", file("map.vssession"), "--overwrite"}),
	             "CLI replaces origin and clears both change lists");
	AudioSession saved;
	QString error;
	ok &= expect(readAudioSession(file("map.vssession"), &saved, nullptr, &error) &&
	                 saved.musicalTime == AudioTempoMap{90, 3, 8, {}, {}},
	             "GUI/core reader accepts exact CLI-authored map");
	ok &= expect(run({"position", file("map.vssession"), "--at-position", "2.1.0"}) &&
	                 last["musicalPosition"].toObject()["frame"].toInteger() == 48000,
	             "3/8 bars use three eighth notes at quarter-note BPM");
	std::cout << (ok ? "Audio tempo CLI verification passed\n" : "Audio tempo CLI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
