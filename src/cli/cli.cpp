#include "cli/cli.h"

#include "app/ui_primitives.h"
#include "core/operation_state.h"
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

QString normalizedOptionId(const QString& id)
{
	return id.trimmed().toLower().replace('_', '-');
}

bool hasOption(const QStringList& args, const QString& option)
{
	return args.contains(option);
}

bool boolOptionValue(const QString& value, bool* parsed)
{
	const QString normalized = normalizedOptionId(value);
	if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true") || normalized == QStringLiteral("yes") || normalized == QStringLiteral("on") || normalized == QStringLiteral("enabled")) {
		*parsed = true;
		return true;
	}
	if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false") || normalized == QStringLiteral("no") || normalized == QStringLiteral("off") || normalized == QStringLiteral("disabled")) {
		*parsed = false;
		return true;
	}
	return false;
}

bool localeOptionIsSupported(const QString& value)
{
	const QString requested = value.trimmed().replace('_', '-');
	if (requested.isEmpty()) {
		return false;
	}
	const QString normalized = normalizedLocaleName(requested);
	return normalized != QStringLiteral("en") || requested.startsWith(QStringLiteral("en"), Qt::CaseInsensitive);
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
	std::cout << "  --operation-states  Print reusable operation state identifiers.\n";
	std::cout << "  --ui-primitives     Print reusable UI primitive identifiers.\n";
	std::cout << "  --settings-report   Print settings storage and recent projects.\n";
	std::cout << "  --setup-report      Print first-run setup status and summary.\n";
	std::cout << "  --setup-start       Start or resume first-run setup.\n";
	std::cout << "  --setup-step <id>   Resume setup at a specific step.\n";
	std::cout << "  --setup-next        Advance setup to the next step.\n";
	std::cout << "  --setup-skip        Skip setup for now without completing it.\n";
	std::cout << "  --setup-complete    Mark setup complete.\n";
	std::cout << "  --setup-reset       Reset setup progress.\n";
	std::cout << "  --preferences-report\n";
	std::cout << "                      Print accessibility and language preferences.\n";
	std::cout << "  --set-locale <code> Set the preferred UI locale. Supported: " << text(supportedLocaleNames().join(", ")) << "\n";
	std::cout << "  --set-theme <id>    Set theme: " << text(themeIds().join(", ")) << "\n";
	std::cout << "  --set-text-scale <percent>\n";
	std::cout << "                      Set text scale from 100 to 200.\n";
	std::cout << "  --set-density <id>  Set density: " << text(densityIds().join(", ")) << "\n";
	std::cout << "  --set-reduced-motion <on|off>\n";
	std::cout << "                      Store the reduced-motion preference.\n";
	std::cout << "  --set-tts <on|off>  Store the OS-backed text-to-speech preference.\n";
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

void printOperationStates()
{
	std::cout << "Operation states\n";
	for (const QString& id : operationStateIds()) {
		const OperationState state = operationStateFromId(id);
		std::cout << "- " << text(id) << "\n";
		std::cout << "  Label: " << text(operationStateDisplayName(state)) << "\n";
		std::cout << "  Terminal: " << (operationStateIsTerminal(state) ? "yes" : "no") << "\n";
		std::cout << "  Cancellable: " << (operationStateAllowsCancellation(state) ? "yes" : "no") << "\n";
	}
}

void printUiPrimitives()
{
	std::cout << "UI primitives\n";
	for (const UiPrimitiveDescriptor& primitive : uiPrimitiveDescriptors()) {
		std::cout << "- " << text(primitive.id) << "\n";
		std::cout << "  Title: " << text(primitive.title) << "\n";
		std::cout << "  Description: " << text(primitive.description) << "\n";
		std::cout << "  Use cases: " << text(primitive.useCases.join(", ")) << "\n";
	}
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

void printPreferences(const AccessibilityPreferences& preferences)
{
	std::cout << "Accessibility and language preferences\n";
	std::cout << "Locale: " << text(preferences.localeName) << "\n";
	std::cout << "Theme: " << text(themeId(preferences.theme)) << " (" << text(themeDisplayName(preferences.theme)) << ")\n";
	std::cout << "Text scale: " << preferences.textScalePercent << "%\n";
	std::cout << "Density: " << text(densityId(preferences.density)) << " (" << text(densityDisplayName(preferences.density)) << ")\n";
	std::cout << "Reduced motion: " << (preferences.reducedMotion ? "enabled" : "disabled") << "\n";
	std::cout << "Text to speech: " << (preferences.textToSpeechEnabled ? "enabled" : "disabled") << "\n";
}

void printSetupSummary(const SetupSummary& summary)
{
	std::cout << "First-run setup\n";
	std::cout << "Status: " << text(summary.status) << "\n";
	std::cout << "Current step: " << text(summary.currentStepName) << " [" << text(summary.currentStepId) << "]\n";
	std::cout << "Description: " << text(summary.currentStepDescription) << "\n";
	std::cout << "Next action: " << text(summary.nextAction) << "\n";

	if (!summary.completedItems.isEmpty()) {
		std::cout << "Completed items\n";
		for (const QString& item : summary.completedItems) {
			std::cout << "- " << text(item) << "\n";
		}
	}
	if (!summary.pendingItems.isEmpty()) {
		std::cout << "Pending items\n";
		for (const QString& item : summary.pendingItems) {
			std::cout << "- " << text(item) << "\n";
		}
	}
	if (!summary.warnings.isEmpty()) {
		std::cout << "Warnings\n";
		for (const QString& item : summary.warnings) {
			std::cout << "- " << text(item) << "\n";
		}
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
	printPreferences(settings.accessibilityPreferences());
	printSetupSummary(settings.setupSummary());
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
	if (hasOption(args, "--operation-states")) {
		printOperationStates();
		return 0;
	}
	if (hasOption(args, "--ui-primitives")) {
		printUiPrimitives();
		return 0;
	}
	if (hasOption(args, "--setup-start") || hasOption(args, "--setup-step") || hasOption(args, "--setup-next") || hasOption(args, "--setup-skip") || hasOption(args, "--setup-complete") || hasOption(args, "--setup-reset")) {
		StudioSettings settings;
		if (hasOption(args, "--setup-reset")) {
			settings.resetSetup();
		}
		if (hasOption(args, "--setup-start")) {
			settings.startOrResumeSetup(settings.setupProgress().currentStep);
		}
		if (hasOption(args, "--setup-step")) {
			const QString stepId = optionValue(args, "--setup-step");
			if (stepId.isEmpty() || setupStepId(setupStepFromId(stepId)) != normalizedOptionId(stepId)) {
				QStringList ids;
				for (SetupStep step : setupSteps()) {
					ids.push_back(setupStepId(step));
				}
				std::cerr << "--setup-step requires one of: " << text(ids.join(", ")) << "\n";
				return 2;
			}
			settings.startOrResumeSetup(setupStepFromId(stepId));
		}
		if (hasOption(args, "--setup-next")) {
			settings.advanceSetup();
		}
		if (hasOption(args, "--setup-skip")) {
			settings.skipSetup();
		}
		if (hasOption(args, "--setup-complete")) {
			settings.completeSetup();
		}
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save setup progress: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		printSetupSummary(settings.setupSummary());
		return 0;
	}
	if (hasOption(args, "--set-locale") || hasOption(args, "--set-theme") || hasOption(args, "--set-text-scale") || hasOption(args, "--set-density") || hasOption(args, "--set-reduced-motion") || hasOption(args, "--set-tts")) {
		StudioSettings settings;
		AccessibilityPreferences preferences = settings.accessibilityPreferences();

		if (hasOption(args, "--set-locale")) {
			const QString value = optionValue(args, "--set-locale");
			if (!localeOptionIsSupported(value)) {
				std::cerr << "--set-locale requires one of: " << text(supportedLocaleNames().join(", ")) << "\n";
				return 2;
			}
			preferences.localeName = normalizedLocaleName(value);
		}
		if (hasOption(args, "--set-theme")) {
			const QString value = normalizedOptionId(optionValue(args, "--set-theme"));
			if (!themeIds().contains(value)) {
				std::cerr << "--set-theme requires one of: " << text(themeIds().join(", ")) << "\n";
				return 2;
			}
			preferences.theme = themeFromId(value);
		}
		if (hasOption(args, "--set-text-scale")) {
			bool ok = false;
			const int value = optionValue(args, "--set-text-scale").toInt(&ok);
			if (!ok || value < StudioSettings::kMinimumTextScalePercent || value > StudioSettings::kMaximumTextScalePercent) {
				std::cerr << "--set-text-scale requires a value from 100 to 200.\n";
				return 2;
			}
			preferences.textScalePercent = value;
		}
		if (hasOption(args, "--set-density")) {
			const QString value = normalizedOptionId(optionValue(args, "--set-density"));
			if (!densityIds().contains(value)) {
				std::cerr << "--set-density requires one of: " << text(densityIds().join(", ")) << "\n";
				return 2;
			}
			preferences.density = densityFromId(value);
		}
		if (hasOption(args, "--set-reduced-motion")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-reduced-motion"), &value)) {
				std::cerr << "--set-reduced-motion requires on or off.\n";
				return 2;
			}
			preferences.reducedMotion = value;
		}
		if (hasOption(args, "--set-tts")) {
			bool value = false;
			if (!boolOptionValue(optionValue(args, "--set-tts"), &value)) {
				std::cerr << "--set-tts requires on or off.\n";
				return 2;
			}
			preferences.textToSpeechEnabled = value;
		}

		settings.setAccessibilityPreferences(preferences);
		settings.sync();
		if (settings.status() != QSettings::NoError) {
			std::cerr << "Failed to save preferences: " << text(settingsStatusText(settings.status())) << "\n";
			return 1;
		}
		std::cout << "Preferences saved.\n";
		printPreferences(settings.accessibilityPreferences());
		return 0;
	}
	if (hasOption(args, "--settings-report")) {
		printSettingsReport();
		return 0;
	}
	if (hasOption(args, "--setup-report")) {
		const StudioSettings settings;
		printSetupSummary(settings.setupSummary());
		return 0;
	}
	if (hasOption(args, "--preferences-report")) {
		const StudioSettings settings;
		printPreferences(settings.accessibilityPreferences());
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
