#include "app/application_shell.h"
#include "app/studio_runtime.h"
#include "cli/cli.h"
#include "core/studio_manifest.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace {

QStringList rawArguments(int argc, char** argv)
{
	QStringList args;
	for (int index = 0; index < argc; ++index) {
		args.push_back(QString::fromLocal8Bit(argv[index]));
	}
	return args;
}

void configureApplicationMetadata(QCoreApplication& app)
{
	app.setOrganizationName(QStringLiteral("DarkMatterProductions"));
	app.setApplicationName(QStringLiteral("VibeStudio"));
	app.setApplicationVersion(vibestudio::versionString());
}

// `--settings-file <path>` keeps automation, CI, and scripted runs away from
// the user's real preference store. It is resolved before any QSettings access
// so every later reader sees the override.
QString settingsFileOverride(const QStringList& args)
{
	const QString flag = QStringLiteral("--settings-file");
	for (int index = 0; index < args.size(); ++index) {
		const QString& argument = args.at(index);
		if (argument == flag && index + 1 < args.size()) {
			return args.at(index + 1);
		}
		if (argument.startsWith(flag + QLatin1Char('='))) {
			return argument.mid(flag.size() + 1);
		}
	}
	return QString();
}

} // namespace

int main(int argc, char** argv)
{
	const QStringList args = rawArguments(argc, argv);

	const QString settingsOverride = settingsFileOverride(args);
	if (!settingsOverride.isEmpty()) {
		vibestudio::StudioSettings::setOverrideFilePath(settingsOverride);
	}

	if (args.contains(QStringLiteral("--cli"))) {
		QCoreApplication app(argc, argv);
		configureApplicationMetadata(app);
		return vibestudio::cli::run(app.arguments());
	}

	vibestudio::configureHighDpiBehavior();

	QApplication app(argc, argv);
	configureApplicationMetadata(app);
	QApplication::setWindowIcon(vibestudio::studioApplicationIcon());
	vibestudio::installSessionLogging();

	{
		vibestudio::StudioSettings settings;
		const QString locale = settings.accessibilityPreferences().localeName;
		vibestudio::installStudioTranslations(app, locale);
		vibestudio::applyLayoutDirectionForLocale(locale);
	}

	vibestudio::ApplicationShell shell;
	shell.show();

	// `--self-test` builds the whole shell, pumps the event loop briefly, then
	// exits. It gives CI a real GUI smoke check (under an offscreen platform
	// plugin) instead of only ever exercising the CLI.
	if (args.contains(QStringLiteral("--self-test"))) {
		QTimer::singleShot(0, &app, [&shell]() {
			shell.runSelfTest();
		});
		QTimer::singleShot(2500, &app, []() {
			QCoreApplication::exit(0);
		});
	}

	return app.exec();
}
