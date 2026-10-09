// The `render` CLI family end to end, run as a separate process: backends,
// test and set, the --renderer and VIBESTUDIO_RENDER_BACKEND overrides, and
// their usage errors. A machine where no renderer starts still checks the
// reports and errors; VIBESTUDIO_RENDER_REQUIRE=1 makes that a failure.

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <iostream>

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toStdString();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid() || argc != 2) {
		std::cerr << "usage: render_cli_smoke_test <vibestudio>\n";
		return 1;
	}
	const QString cli = QString::fromLocal8Bit(argv[1]);
	const QString settings = QDir(temp.path()).filePath(QStringLiteral("cli.ini"));
	int lastExit = 0;
	QString lastOutput;
	const auto run = [&](const QStringList& arguments, const QString& environmentChoice = QString()) {
		QProcess process;
		QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
		environment.remove(QStringLiteral("VIBESTUDIO_RENDER_BACKEND"));
		if (!environmentChoice.isEmpty()) {
			environment.insert(QStringLiteral("VIBESTUDIO_RENDER_BACKEND"), environmentChoice);
		}
		process.setProcessEnvironment(environment);
		process.setWorkingDirectory(temp.path());
		process.start(cli, QStringList {QStringLiteral("--cli"), QStringLiteral("--json"), QStringLiteral("--settings-file"), settings,
							   QStringLiteral("render")}
				+ arguments);
		const bool ended = process.waitForStarted() && process.waitForFinished(120000);
		if (!ended) {
			process.kill();
			process.waitForFinished();
		}
		const QByteArray out = process.readAllStandardOutput();
		lastOutput = QString::fromUtf8(out + process.readAllStandardError());
		lastExit = ended && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
		return QJsonDocument::fromJson(out).object();
	};

	// backends: both renderers reported, the saved choice applied.
	QJsonObject result = run({QStringLiteral("backends")});
	const bool available = lastExit == 0;
	expect(lastExit == 0 || lastExit == 5, "render backends succeeds, or reports that no renderer starts", lastOutput);
	const QJsonArray backends = result.value(QStringLiteral("backends")).toArray();
	expect(backends.size() == 2, "render backends reports OpenGL and Vulkan", lastOutput);
	QStringList ids;
	for (const QJsonValue& backend : backends) {
		const QJsonObject object = backend.toObject();
		ids << object.value(QStringLiteral("id")).toString();
		expect(object.value(QStringLiteral("started")).toBool(), "render backends starts every renderer", lastOutput);
		expect(object.value(QStringLiteral("available")).toBool() || !object.value(QStringLiteral("error")).toString().isEmpty(),
			"an unavailable renderer says why", lastOutput);
	}
	ids.sort();
	expect(ids == QStringList({QStringLiteral("opengl"), QStringLiteral("vulkan")}), "the renderers are opengl and vulkan", ids.join(QLatin1Char(',')));
	expect(result.value(QStringLiteral("choice")).toString() == QStringLiteral("automatic")
			&& result.value(QStringLiteral("choiceSource")).toString() == QStringLiteral("preference"),
		"the default choice is the automatic preference", lastOutput);
	if (available) {
		expect(ids.contains(result.value(QStringLiteral("active")).toString()), "render backends names the renderer in use", lastOutput);
	} else {
		expect(result.value(QStringLiteral("active")).isNull(), "with no renderer, none is in use", lastOutput);
		if (qEnvironmentVariableIntValue("VIBESTUDIO_RENDER_REQUIRE") != 0) {
			expect(false, "VIBESTUDIO_RENDER_REQUIRE is set, but no renderer starts", lastOutput);
		} else {
			std::cout << "No renderer starts on this machine: drawing checks skipped.\n";
		}
	}

	// test: every available renderer draws the test image correctly.
	result = run({QStringLiteral("test")});
	expect(lastExit == (available ? 0 : 5), "render test passes where a renderer starts", lastOutput);
	const QJsonArray tests = result.value(QStringLiteral("tests")).toArray();
	expect(tests.size() == 2, "render test covers both renderers", lastOutput);
	for (const QJsonValue& test : tests) {
		const QJsonObject object = test.toObject();
		if (object.value(QStringLiteral("available")).toBool()) {
			expect(object.value(QStringLiteral("tested")).toBool() && object.value(QStringLiteral("passed")).toBool(),
				"an available renderer draws the test image correctly", lastOutput);
		}
	}
	result = run({QStringLiteral("test"), QStringLiteral("--renderer"), QStringLiteral("vulkan")});
	expect(result.value(QStringLiteral("tests")).toArray().size() == 1, "--renderer limits render test to one renderer", lastOutput);

	// Overrides: --renderer for the run, ahead of the environment variable.
	result = run({QStringLiteral("backends"), QStringLiteral("--renderer"), QStringLiteral("opengl")});
	expect(result.value(QStringLiteral("choice")).toString() == QStringLiteral("opengl")
			&& result.value(QStringLiteral("choiceSource")).toString() == QStringLiteral("command-line"),
		"--renderer overrides the choice for one run", lastOutput);
	result = run({QStringLiteral("backends")}, QStringLiteral("vulkan"));
	expect(result.value(QStringLiteral("choice")).toString() == QStringLiteral("vulkan")
			&& result.value(QStringLiteral("choiceSource")).toString() == QStringLiteral("environment"),
		"VIBESTUDIO_RENDER_BACKEND overrides the preference", lastOutput);
	result = run({QStringLiteral("backends"), QStringLiteral("--renderer=opengl")}, QStringLiteral("vulkan"));
	expect(result.value(QStringLiteral("choiceSource")).toString() == QStringLiteral("command-line"), "--renderer wins over the environment", lastOutput);
	run({QStringLiteral("backends"), QStringLiteral("--renderer"), QStringLiteral("software")});
	expect(lastExit == 2, "an unknown --renderer is a usage error", lastOutput);

	// set: saved, read back, and validated.
	result = run({QStringLiteral("set"), QStringLiteral("vulkan")});
	expect(lastExit == 0 && result.value(QStringLiteral("preference")).toString() == QStringLiteral("vulkan"), "render set saves the preference",
		lastOutput);
	result = run({QStringLiteral("backends")});
	expect(result.value(QStringLiteral("preference")).toString() == QStringLiteral("vulkan")
			&& result.value(QStringLiteral("choice")).toString() == QStringLiteral("vulkan"),
		"the saved preference is the next run's choice", lastOutput);
	run({QStringLiteral("set"), QStringLiteral("metal")});
	expect(lastExit == 2, "render set refuses an unknown renderer", lastOutput);
	run({QStringLiteral("set")});
	expect(lastExit == 2, "render set without a renderer is a usage error", lastOutput);
	run({QStringLiteral("set"), QStringLiteral("automatic")});
	expect(lastExit == 0, "render set automatic restores the default", lastOutput);
	run({QStringLiteral("draw")});
	expect(lastExit == 2, "an unknown render action is a usage error", lastOutput);

	if (failures > 0) {
		std::cerr << failures << " failure(s)\n";
		return 1;
	}
	std::cout << "render CLI smoke test passed\n";
	return 0;
}
