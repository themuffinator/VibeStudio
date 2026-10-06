#include "core/audio_meter.h"
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
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("meter-cli-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto s = test::mediaFixture();
	const auto native = temporary.filePath("session.vssession");
	const auto original = encodeAudioSession(s);
	if (!test::writeMediaFixture(native, original))
		return EXIT_FAILURE;
	QJsonObject last;
	const auto run = [&](const QStringList &options, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
		              QStringList{"--cli", "--json", "--settings-file", temporary.filePath("settings.ini"), "asset",
		                          "audio-session", "meters", native} +
		                  options);
		if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		last = QJsonDocument::fromJson(bytes).object();
		const bool ok =
		    process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && !last.isEmpty();
		if (!ok)
			std::cerr << options.join(' ').toStdString() << '\n'
			          << bytes.constData() << process.readAllStandardError().constData();
		return ok;
	};
	bool ok = run({});
	ok &= last["meters"].toObject() ==
	          audioMeterReportToJson(s, measureAudioSessionMeters(s, 0, audioSessionFrames(s))) &&
	      last["deviceOpened"].isBool() && !last["deviceOpened"].toBool();
	for (int block : {1, 7, 65536}) {
		ok &= run({"--start-frame", "17", "--end-frame", "199", "--block-frames", QString::number(block)});
		ok &= last["meters"].toObject() == audioMeterReportToJson(s, measureAudioSessionMeters(s, 17, 199, 13));
	}
	for (const auto &options :
	     {QStringList{"--output", temporary.filePath("unexpected.json")}, QStringList{"--overwrite"},
	      QStringList{"--dry-run"}, QStringList{"--block-frames", "65537"}, QStringList{"--block-frames", "0"},
	      QStringList{"--end-frame", "0"}, QStringList{"--start-frame", "-1"}, QStringList{"--end-frame", "no"},
	      QStringList{"--frames", "100"}, QStringList{"--loop", "true"},
	      QStringList{"--start-frame", "1", "--start-frame", "2"}, QStringList{"--unknown"}})
		ok &= run(options, 2);
	ok &= test::readMediaFixture(native) == original && !QFile::exists(temporary.filePath("unexpected.json"));
	std::cout << (ok ? "Read-only meter CLI parity and strict options passed\n" : "Meter CLI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
