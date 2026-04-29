#include "cli/cli.h"

#include "core/studio_manifest.h"
#include "core/studio_settings.h"
#include "vibestudio_config.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QSettings>
#include <QSysInfo>

#include <cstddef>
#include <iostream>
#include <string>

namespace vibestudio::cli {

namespace {

std::string text(const QString& value)
{
	const QByteArray bytes = value.toUtf8();
	return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString nativePath(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

QString settingsStatusText(QSettings::Status status)
{
	switch (status) {
	case QSettings::NoError:
		return QStringLiteral("ready");
	case QSettings::AccessError:
		return QStringLiteral("access-error");
	case QSettings::FormatError:
		return QStringLiteral("format-error");
	}
	return QStringLiteral("unknown");
}

QString optionValue(const QStringList& args, const QString& option)
{
	const int index = args.indexOf(option);
	if (index < 0 || index + 1 >= args.size()) {
		return {};
	}
	return args[index + 1];
}

bool hasOption(const QStringList& args, const QString& option)
{
	return args.contains(option);
}

void printHelp()
{
	std::cout << "VibeStudio " << text(versionString()) << "\n";
	std::cout << "Usage: vibestudio --cli [options]\n\n";
	std::cout << "Options:\n";
	std::cout << "  --version           Print the application version.\n";
	std::cout << "  --help              Print this help text.\n";
	std::cout << "  --studio-report     Print planned studio modules.\n";
	std::cout << "  --compiler-report   Print imported compiler integrations.\n";
	std::cout << "  --platform-report   Print platform and Qt runtime details.\n";
	std::cout << "  --settings-report   Print settings storage and recent projects.\n";
	std::cout << "  --recent-projects   Print recent projects only.\n";
	std::cout << "  --add-recent-project <path>\n";
	std::cout << "                      Remember a project folder in persistent settings.\n";
	std::cout << "  --remove-recent-project <path>\n";
	std::cout << "                      Forget a project folder without touching files.\n";
	std::cout << "  --clear-recent-projects\n";
	std::cout << "                      Clear remembered project folders without touching files.\n";
}

void printStudioReport()
{
	std::cout << "VibeStudio planned modules\n";
	for (const StudioModule& module : plannedModules()) {
		std::cout << "- " << text(module.name) << " [" << text(module.maturity) << "]\n";
		std::cout << "  Category: " << text(module.category) << "\n";
		std::cout << "  Engines: " << text(module.engines.join(", ")) << "\n";
		std::cout << "  " << text(module.description) << "\n";
	}
}

void printCompilerReport()
{
	std::cout << "VibeStudio compiler integrations\n";
	for (const CompilerIntegration& compiler : compilerIntegrations()) {
		std::cout << "- " << text(compiler.displayName) << "\n";
		std::cout << "  Engines: " << text(compiler.engines) << "\n";
		std::cout << "  Role: " << text(compiler.role) << "\n";
		std::cout << "  Source: " << text(compiler.sourcePath) << "\n";
		std::cout << "  Upstream: " << text(compiler.upstreamUrl) << "\n";
		std::cout << "  Revision: " << text(compiler.pinnedRevision) << "\n";
		std::cout << "  License: " << text(compiler.license) << "\n";
	}
}

void printPlatformReport()
{
	std::cout << "VibeStudio platform report\n";
	std::cout << "Version: " << text(versionString()) << "\n";
	std::cout << "GitHub repo: " << VIBESTUDIO_GITHUB_REPO << "\n";
	std::cout << "Update channel: " << VIBESTUDIO_UPDATE_CHANNEL << "\n";
	std::cout << "Qt runtime: " << qVersion() << "\n";
	std::cout << "Kernel: " << text(QSysInfo::kernelType()) << " " << text(QSysInfo::kernelVersion()) << "\n";
	std::cout << "CPU architecture: " << text(QSysInfo::currentCpuArchitecture()) << "\n";
	std::cout << "Product: " << text(QSysInfo::prettyProductName()) << "\n";
}

void printRecentProjects(const StudioSettings& settings)
{
	const QVector<RecentProject> projects = settings.recentProjects();
	if (projects.isEmpty()) {
		std::cout << "Recent projects: none\n";
		return;
	}

	std::cout << "Recent projects\n";
	for (const RecentProject& project : projects) {
		std::cout << "- " << text(project.displayName) << "\n";
		std::cout << "  Path: " << text(nativePath(project.path)) << "\n";
		std::cout << "  State: " << (project.exists ? "ready" : "missing") << "\n";
		std::cout << "  Last opened UTC: " << text(project.lastOpenedUtc.toUTC().toString(Qt::ISODate)) << "\n";
	}
}

void printSettingsReport()
{
	const StudioSettings settings;
	std::cout << "VibeStudio settings\n";
	std::cout << "Storage: " << text(nativePath(settings.storageLocation())) << "\n";
	std::cout << "Status: " << text(settingsStatusText(settings.status())) << "\n";
	std::cout << "Schema: " << settings.schemaVersion() << "\n";
	std::cout << "Selected mode index: " << settings.selectedMode() << "\n";
	printRecentProjects(settings);
}

} // namespace

int run(const QStringList& args)
{
	QStringList requestedOptions = args.mid(1);
	requestedOptions.removeAll(QStringLiteral("--cli"));

	if (hasOption(args, "--version")) {
		std::cout << "VibeStudio " << text(versionString()) << "\n";
		return 0;
	}
	if (hasOption(args, "--help") || requestedOptions.isEmpty()) {
		printHelp();
		return 0;
	}
	if (hasOption(args, "--studio-report")) {
		printStudioReport();
		return 0;
	}
	if (hasOption(args, "--compiler-report")) {
		printCompilerReport();
		return 0;
	}
	if (hasOption(args, "--platform-report")) {
		printPlatformReport();
		return 0;
	}
	if (hasOption(args, "--settings-report")) {
		printSettingsReport();
		return 0;
	}
	if (hasOption(args, "--recent-projects")) {
		const StudioSettings settings;
		printRecentProjects(settings);
		return 0;
	}
	if (hasOption(args, "--add-recent-project")) {
		const QString path = optionValue(args, "--add-recent-project");
		if (path.isEmpty()) {
			std::cerr << "--add-recent-project requires a path.\n";
			return 2;
		}

		StudioSettings settings;
		settings.recordRecentProject(path);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Remembered recent project: " << text(nativePath(normalizedProjectPath(path))) << "\n";
		return 0;
	}
	if (hasOption(args, "--remove-recent-project")) {
		const QString path = optionValue(args, "--remove-recent-project");
		if (path.isEmpty()) {
			std::cerr << "--remove-recent-project requires a path.\n";
			return 2;
		}

		StudioSettings settings;
		settings.removeRecentProject(path);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Forgot recent project: " << text(nativePath(normalizedProjectPath(path))) << "\n";
		return 0;
	}
	if (hasOption(args, "--clear-recent-projects")) {
		StudioSettings settings;
		settings.clearRecentProjects();
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save settings: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Recent projects cleared.\n";
		return 0;
	}

	std::cerr << "Unknown VibeStudio CLI option. Run --cli --help.\n";
	return 2;
}

} // namespace vibestudio::cli
