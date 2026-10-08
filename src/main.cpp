#include "app/application_shell.h"
#include "app/studio_runtime.h"
#include "app/studio_theme.h"
#include "cli/cli.h"
#include "core/package_import_store.h"
#include "core/localization.h"
#include "core/package_copy_store.h"
#include "core/studio_manifest.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QScopeGuard>
#include <QThreadPool>

// The brand icon resource lives in the app library (assets/branding/vibestudio.qrc).
// Q_INIT_RESOURCE must run from the global namespace, and calling it here makes
// the linker keep the resource in the executable.
static void initBrandingResources()
{
	Q_INIT_RESOURCE(vibestudio_branding);
}

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
		const auto imports = qScopeGuard([] {
			QThreadPool::globalInstance()->waitForDone();
			vibestudio::waitForPackageImportCleanup();
			vibestudio::waitForPackageCopyCleanup();
		});
		configureApplicationMetadata(app);
		return vibestudio::cli::run(app.arguments());
	}

	vibestudio::configureHighDpiBehavior();

	QApplication app(argc, argv);
	// Declared before the shell: first release document/worker ownership, then
	// finish queued import deletion and unlock, while Qt is still available.
	const auto imports = qScopeGuard([] {
		QThreadPool::globalInstance()->waitForDone();
		vibestudio::waitForPackageImportCleanup();
		vibestudio::waitForPackageCopyCleanup();
	});
	configureApplicationMetadata(app);
	// Matches packaging/linux/*.desktop, so Wayland and freedesktop launchers
	// pair the window with the installed icon.
	QGuiApplication::setDesktopFileName(QStringLiteral("io.github.themuffinator.VibeStudio"));
	initBrandingResources();
	QApplication::setWindowIcon(vibestudio::studioApplicationIcon());
	vibestudio::installSessionLogging();

	// The offscreen platform (CI self-test, documentation snapshots) has no
	// system UI font, so pick a real one when it is installed.
	if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
		// Its font database reads only .ttf, .otf, and Type 1 files from
		// QT_QPA_FONTDIR, leaving out TrueType collections, which is where
		// Windows keeps its Chinese, Japanese, and Indic interface fonts.
		const QString fontDirectory = qEnvironmentVariable("QT_QPA_FONTDIR");
		if (!fontDirectory.isEmpty()) {
			const QFileInfoList collections = QDir(fontDirectory).entryInfoList({QStringLiteral("*.ttc")}, QDir::Files);
			for (const QFileInfo& collection : collections) {
				QFontDatabase::addApplicationFont(collection.absoluteFilePath());
			}
		}
		for (const QString& family : {QStringLiteral("Segoe UI"), QStringLiteral("Helvetica Neue"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")}) {
			if (QFontDatabase::hasFamily(family)) {
				QFont font = QApplication::font();
				font.setFamily(family);
				QApplication::setFont(font);
				break;
			}
		}
	}

	// A self-test or a snapshot run is not a working session: it starts clean,
	// records nothing, and neither arms crash capture nor consumes the marker a
	// real session that crashed left behind.
	const bool interactive = !args.contains(QStringLiteral("--self-test")) && flagValue(args, QStringLiteral("--ui-snapshot")).isEmpty();

	{
		vibestudio::StudioSettings settings;
		const vibestudio::AccessibilityPreferences preferences = settings.accessibilityPreferences();
		vibestudio::installStudioTranslations(app, preferences.localeName);
		vibestudio::applyLayoutDirectionForLocale(preferences.localeName);
		// Numbers, dates, and sizes follow the region format preference, which
		// is the operating system's regional settings unless the user chose.
		QLocale::setDefault(vibestudio::regionFormatLocale(preferences.formatLocaleName, preferences.localeName));
		// Theme before the first widget exists, so nothing is built with the
		// platform style and then repolished.
		vibestudio::applyStudioTheme(app, vibestudio::studioThemeTokens(preferences));
		// Before the shell exists, so it can tell whether the last session
		// crashed. Turned off in Preferences, nothing is written, though a
		// crash from before is still offered once.
		if (interactive) {
			vibestudio::CrashHandlerOptions crashOptions;
			crashOptions.enabled = settings.crashReports();
			// A run with its own settings file is a scripted or isolated one: its
			// markers and reports stay beside that file, never the user's.
			if (!settingsOverride.isEmpty()) {
				crashOptions.directory = QFileInfo(settingsOverride).absoluteDir().filePath(QStringLiteral("crash-reports"));
			}
			vibestudio::installCrashHandling(crashOptions);
		}
	}

	vibestudio::ApplicationShell shell;
	shell.show();

	const QStringList paths = pathsToOpen(args);
	for (const QString& path : paths) {
		shell.openPathFromCommandLine(path);
	}
	// Once the window is up the session begins: a crash last time is offered
	// back; otherwise the last session reopens, unless a path given on the
	// command line already says what the user wants.
	if (interactive) {
		const bool reopenLast = paths.isEmpty();
		QTimer::singleShot(0, &shell, [&shell, reopenLast]() {
			shell.beginSession(reopenLast);
		});
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

	const int exitCode = app.exec();
	// A language change asked for a restart, and the shell has closed through
	// its unsaved-work guards: the new studio starts in the new language.
	vibestudio::startRequestedStudioRestart();
	return exitCode;
}
