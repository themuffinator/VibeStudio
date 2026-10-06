#include "core/audio_take.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <array>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value)
		std::cerr << text << '\n';
	return value;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("take-cli-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QLatin1String(name)); };
	AudioTakeMetadata metadata;
	metadata.name = "CLI take";
	metadata.inputChannels = 2;
	metadata.channelMap = {0, 1};
	AudioTakeWriter writer;
	QString error;
	bool ok = expect(writer.open(path("prefix.vstake"), metadata, &error) &&
	                     writer.append(std::array<float, 6>{.25f, .5f, -.5f, -1, .75f, 1.5f}, &error),
	                 "create retained prefix for CLI");
	writer.close();
	QJsonObject last;
	const auto run = [&](const QStringList &options, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]),
		              QStringList{"--cli", "--json", "--settings-file", path("settings.ini"), "asset", "audio-take"} +
		                  options);
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
			std::cerr << options.join(' ').toStdString() << " exit " << process.exitCode() << '\n'
			          << output.constData() << process.readAllStandardError().constData();
		return passed;
	};
	ok &= expect(run({"inspect", path("prefix.vstake")}) && last["take"].toObject()["frames"].toInteger() == 3 &&
	                 !last["take"].toObject()["complete"].toBool(),
	             "CLI inspection exposes recoverable verified frames");
	const auto digest = last["take"].toObject()["prefixSha256"].toString();
	QStringList options{"export",
	                    path("prefix.vstake"),
	                    "--expected-prefix-sha256",
	                    digest,
	                    "--start-frame",
	                    "1",
	                    "--end-frame",
	                    "3",
	                    "--channels",
	                    "2,1",
	                    "--output",
	                    path("selected.vsaudio")};
	ok &= expect(run(options, 3) && !QFile::exists(path("selected.vsaudio")),
	             "incomplete export requires explicit recovery flag");
	options << "--allow-incomplete";
	ok &= expect(run(options + QStringList{"--dry-run"}) && !QFile::exists(path("selected.vsaudio")),
	             "take dry run verifies but never writes");
	ok &= expect(run(options), "CLI exports reviewed prefix to separate project");
	AudioProject project;
	ok &= expect(readAudioProject(path("selected.vsaudio"), &project) &&
	                 project.clip.samples == QVector<float>{-1, -.5f, 1.5f, .75f} &&
	                 project.sourcePath == path("prefix.vstake"),
	             "CLI exact range/channel selection and provenance match core");
	ok &= expect(run(options, 3), "CLI refuses existing destination without overwrite");
	auto changed = options;
	changed[changed.indexOf("--expected-prefix-sha256") + 1] = QString(64, '0');
	ok &= expect(run(changed, 3), "changed reviewed digest cannot export");
	ok &= expect(run({"inspect", path("prefix.vstake"), "--allow-incomplete"}, 2) &&
	                 run({"inspect", path("prefix.vstake"), "--channels=1"}, 2),
	             "inspection rejects irrelevant export flags");
	changed = options;
	changed[changed.indexOf("--channels") + 1] = "-9223372036854775808";
	ok &= expect(run(changed, 2), "extreme signed channel value is rejected without arithmetic overflow");
	changed = options;
	changed[changed.indexOf("--output") + 1] = path("selected.wav");
	changed << "--format" << "wav";
	ok &= expect(run(changed), "CLI exports float WAV for existing game delivery workflow");
	QFile wav(path("selected.wav"));
	ok &= expect(wav.open(QIODevice::ReadOnly), "read exported WAV");
	const auto decoded = decodeAudioClip("selected.wav", wav.readAll());
	ok &= expect(decoded.succeeded() && decoded.clip.samples == project.clip.samples,
	             "float WAV retains recorded headroom");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
