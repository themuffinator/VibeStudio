#include "core/studio_settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected a writable temporary directory.");
	}

	const QString settingsPath = tempDir.filePath(QStringLiteral("settings.ini"));
	const QString alphaProject = tempDir.filePath(QStringLiteral("Alpha Project"));
	const QString betaProject = tempDir.filePath(QStringLiteral("Beta Project"));
	if (!QDir().mkpath(alphaProject) || !QDir().mkpath(betaProject)) {
		return fail("Expected test project directories to be created.");
	}

	vibestudio::StudioSettings settings(settingsPath);
	if (settings.schemaVersion() != vibestudio::StudioSettings::kSchemaVersion) {
		return fail("Expected default settings schema version.");
	}
	if (!settings.recentProjects().isEmpty()) {
		return fail("Expected no recent projects in a fresh settings file.");
	}
	const vibestudio::AccessibilityPreferences defaultPreferences = settings.accessibilityPreferences();
	if (defaultPreferences.localeName != QStringLiteral("en") || defaultPreferences.textScalePercent != 100 || defaultPreferences.theme != vibestudio::StudioTheme::Dark || defaultPreferences.density != vibestudio::UiDensity::Standard) {
		return fail("Expected default accessibility and language preferences.");
	}
	const vibestudio::SetupProgress defaultSetup = settings.setupProgress();
	if (defaultSetup.started || defaultSetup.skipped || defaultSetup.completed || defaultSetup.currentStep != vibestudio::SetupStep::WelcomeAccess) {
		return fail("Expected default setup state.");
	}
	if (settings.setupSummary().status != QStringLiteral("not-started")) {
		return fail("Expected default setup summary status.");
	}

	settings.recordRecentProject(alphaProject, QStringLiteral("Alpha"), QDateTime::fromString(QStringLiteral("2026-04-29T08:00:00Z"), Qt::ISODate));
	settings.recordRecentProject(betaProject, QString(), QDateTime::fromString(QStringLiteral("2026-04-29T09:00:00Z"), Qt::ISODate));
	settings.recordRecentProject(alphaProject, QStringLiteral("Alpha Renamed"), QDateTime::fromString(QStringLiteral("2026-04-29T10:00:00Z"), Qt::ISODate));
	settings.setSelectedMode(5);
	settings.setShellGeometry(QByteArray("geometry-bytes"));
	settings.setShellWindowState(QByteArray("state-bytes"));
	settings.startOrResumeSetup(vibestudio::SetupStep::WorkspaceProfile);
	settings.advanceSetup();
	settings.skipSetup();
	vibestudio::AccessibilityPreferences preferences;
	preferences.localeName = QStringLiteral("pt_BR");
	preferences.textScalePercent = 175;
	preferences.theme = vibestudio::StudioTheme::HighContrastLight;
	preferences.density = vibestudio::UiDensity::Compact;
	preferences.reducedMotion = true;
	preferences.textToSpeechEnabled = true;
	settings.setAccessibilityPreferences(preferences);
	settings.sync();

	vibestudio::StudioSettings reloaded(settingsPath);
	const QVector<vibestudio::RecentProject> projects = reloaded.recentProjects();
	if (projects.size() != 2) {
		return fail("Expected duplicate recent project records to collapse.");
	}
	if (projects[0].displayName != QStringLiteral("Alpha Renamed")) {
		return fail("Expected most recently opened project first.");
	}
	if (!projects[0].exists || !projects[1].exists) {
		return fail("Expected existing project directories to be marked ready.");
	}
	if (reloaded.selectedMode() != 5) {
		return fail("Expected selected shell mode to persist.");
	}
	if (reloaded.shellGeometry() != QByteArray("geometry-bytes") || reloaded.shellWindowState() != QByteArray("state-bytes")) {
		return fail("Expected shell geometry and state to persist.");
	}
	vibestudio::SetupProgress reloadedSetup = reloaded.setupProgress();
	if (!reloadedSetup.started || !reloadedSetup.skipped || reloadedSetup.completed || reloadedSetup.currentStep != vibestudio::SetupStep::ProjectsPackages) {
		return fail("Expected setup skip/resume state to persist.");
	}
	if (reloaded.setupSummary().status != QStringLiteral("skipped")) {
		return fail("Expected skipped setup summary.");
	}
	reloaded.startOrResumeSetup(reloadedSetup.currentStep);
	reloaded.advanceSetup();
	reloaded.completeSetup();
	reloadedSetup = reloaded.setupProgress();
	if (!reloadedSetup.started || reloadedSetup.skipped || !reloadedSetup.completed || reloadedSetup.currentStep != vibestudio::SetupStep::ReviewFinish) {
		return fail("Expected setup completion state.");
	}
	if (reloaded.setupSummary().status != QStringLiteral("complete")) {
		return fail("Expected complete setup summary.");
	}
	reloaded.resetSetup();
	if (reloaded.setupProgress().started || reloaded.setupSummary().status != QStringLiteral("not-started")) {
		return fail("Expected reset setup state.");
	}
	if (vibestudio::setupStepFromId(QStringLiteral("AI_AUTOMATION")) != vibestudio::SetupStep::AiAutomation) {
		return fail("Expected setup step id normalization.");
	}
	const vibestudio::AccessibilityPreferences reloadedPreferences = reloaded.accessibilityPreferences();
	if (reloadedPreferences.localeName != QStringLiteral("pt-BR") || reloadedPreferences.textScalePercent != 175 || reloadedPreferences.theme != vibestudio::StudioTheme::HighContrastLight || reloadedPreferences.density != vibestudio::UiDensity::Compact || !reloadedPreferences.reducedMotion || !reloadedPreferences.textToSpeechEnabled) {
		return fail("Expected accessibility and language preferences to persist.");
	}

	reloaded.setLocaleName(QStringLiteral("zz"));
	reloaded.setTextScalePercent(999);
	reloaded.setTheme(vibestudio::themeFromId(QStringLiteral("high_contrast_dark")));
	reloaded.setDensity(vibestudio::densityFromId(QStringLiteral("comfortable")));
	reloaded.setReducedMotion(false);
	reloaded.setTextToSpeechEnabled(false);
	const vibestudio::AccessibilityPreferences normalizedPreferences = reloaded.accessibilityPreferences();
	if (normalizedPreferences.localeName != QStringLiteral("en") || normalizedPreferences.textScalePercent != 200 || normalizedPreferences.theme != vibestudio::StudioTheme::HighContrastDark || normalizedPreferences.density != vibestudio::UiDensity::Comfortable || normalizedPreferences.reducedMotion || normalizedPreferences.textToSpeechEnabled) {
		return fail("Expected preference normalization and individual setters.");
	}

	for (int index = 0; index < vibestudio::StudioSettings::kMaximumRecentProjects + 4; ++index) {
		reloaded.recordRecentProject(tempDir.filePath(QStringLiteral("Project %1").arg(index)));
	}
	if (reloaded.recentProjects().size() != vibestudio::StudioSettings::kMaximumRecentProjects) {
		return fail("Expected recent project list to stay bounded.");
	}

	reloaded.removeRecentProject(alphaProject);
	for (const vibestudio::RecentProject& project : reloaded.recentProjects()) {
		if (project.displayName == QStringLiteral("Alpha Renamed")) {
			return fail("Expected selected recent project removal.");
		}
	}

	reloaded.clearRecentProjects();
	if (!reloaded.recentProjects().isEmpty()) {
		return fail("Expected recent projects to clear.");
	}

	return EXIT_SUCCESS;
}
