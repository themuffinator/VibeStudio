#include "app/application_shell.h"
#include "app/studio_runtime.h"
#include "app/studio_theme.h"
#include "cli/cli.h"
#include "core/studio_manifest.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
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
QString flagValue(const QStringList& args, const QString& flag)
{
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

QString settingsFileOverride(const QStringList& args)
{
	return flagValue(args, QStringLiteral("--settings-file"));
}

// Every `--open <path>` in order. Each is handled exactly like a drop onto the
// window, so a map opens in Levels, an archive in Packages, and so on.
QStringList pathsToOpen(const QStringList& args)
{
	const QString flag = QStringLiteral("--open");
	QStringList paths;
	for (int index = 0; index < args.size(); ++index) {
		const QString& argument = args.at(index);
		if (argument == flag && index + 1 < args.size()) {
			paths.push_back(args.at(++index));
		} else if (argument.startsWith(flag + QLatin1Char('='))) {
			paths.push_back(argument.mid(flag.size() + 1));
		}
	}
	return paths;
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

	// The offscreen platform (CI self-test, documentation snapshots) has no
	// system UI font, so pick a real one when it is installed.
	if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
		for (const QString& family : {QStringLiteral("Segoe UI"), QStringLiteral("Helvetica Neue"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")}) {
			if (QFontDatabase::hasFamily(family)) {
				QFont font = QApplication::font();
				font.setFamily(family);
				QApplication::setFont(font);
				break;
			}
		}
	}

	{
		vibestudio::StudioSettings settings;
		const vibestudio::AccessibilityPreferences preferences = settings.accessibilityPreferences();
		vibestudio::installStudioTranslations(app, preferences.localeName);
		vibestudio::applyLayoutDirectionForLocale(preferences.localeName);
		// Theme before the first widget exists, so nothing is built with the
		// platform style and then repolished.
		vibestudio::applyStudioTheme(app, vibestudio::studioThemeTokens(preferences.theme, preferences.density, preferences.textScalePercent));
	}

	vibestudio::ApplicationShell shell;
	shell.show();

	for (const QString& path : pathsToOpen(args)) {
		shell.openPathFromCommandLine(path);
	}

	// `--ui-snapshot <dir>` renders every work surface to PNG and exits. Run it
	// under the offscreen platform plugin with an isolated --settings-file to
	// produce documentation screenshots without touching the user's session.
	const QString snapshotDirectory = flagValue(args, QStringLiteral("--ui-snapshot"));
	if (!snapshotDirectory.isEmpty()) {
		const QString sizeText = flagValue(args, QStringLiteral("--ui-snapshot-size"));
		const QStringList sizeParts = sizeText.split(QLatin1Char('x'));
		if (sizeParts.size() == 2 && sizeParts.at(0).toInt() > 0 && sizeParts.at(1).toInt() > 0) {
			shell.resize(sizeParts.at(0).toInt(), sizeParts.at(1).toInt());
		}
		QTimer::singleShot(0, &app, [&shell, snapshotDirectory]() {
			const QStringList written = shell.captureUiSnapshots(snapshotDirectory);
			for (const QString& path : written) {
				qInfo("%s", qPrintable(path));
			}
			QCoreApplication::exit(written.isEmpty() ? 1 : 0);
		});
		return app.exec();
	}

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
