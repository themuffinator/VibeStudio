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
	const QString quakeInstall = tempDir.filePath(QStringLiteral("Quake Install"));
	if (!QDir().mkpath(alphaProject) || !QDir().mkpath(betaProject) || !QDir().mkpath(quakeInstall)) {
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
	if (settings.selectedEditorProfileId() != QStringLiteral("vibestudio-default")) {
		return fail("Expected default editor profile selection.");
	}
	const vibestudio::AiAutomationPreferences defaultAiPreferences = settings.aiAutomationPreferences();
	if (!defaultAiPreferences.aiFreeMode || defaultAiPreferences.cloudConnectorsEnabled || defaultAiPreferences.agenticWorkflowsEnabled) {
		return fail("Expected AI automation to be disabled by default.");
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
	settings.setCurrentProjectPath(alphaProject);
	settings.setSelectedMode(5);
	settings.setShellGeometry(QByteArray("geometry-bytes"));
	settings.setShellWindowState(QByteArray("state-bytes"));
	vibestudio::RecentActivityTask compilerActivity;
	compilerActivity.id = QStringLiteral("compiler-ericw-qbsp");
	compilerActivity.title = QStringLiteral("Compiler Run");
	compilerActivity.detail = QStringLiteral("maps/start.map");
	compilerActivity.source = QStringLiteral("compiler");
	compilerActivity.state = vibestudio::OperationState::Completed;
	compilerActivity.resultSummary = QStringLiteral("Completed in 42 ms / outputs: 1");
	compilerActivity.warnings = {QStringLiteral("Fixture compiler emitted a benign note.")};
	compilerActivity.createdUtc = QDateTime::fromString(QStringLiteral("2026-04-29T10:05:00Z"), Qt::ISODate);
	compilerActivity.updatedUtc = QDateTime::fromString(QStringLiteral("2026-04-29T10:06:00Z"), Qt::ISODate);
	compilerActivity.finishedUtc = compilerActivity.updatedUtc;
	settings.recordRecentActivityTask(compilerActivity);
	vibestudio::GameInstallationProfile quakeProfile;
	quakeProfile.rootPath = quakeInstall;
	quakeProfile.gameKey = QStringLiteral("quake");
	quakeProfile.displayName = QStringLiteral("Quake Test");
	settings.upsertGameInstallation(quakeProfile);
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
	settings.setSelectedEditorProfileId(QStringLiteral("TrenchBroom"));
	settings.upsertCompilerToolPathOverride({QStringLiteral("ericw-qbsp"), tempDir.filePath(QStringLiteral("qbsp-test"))});
	vibestudio::AiAutomationPreferences aiPreferences;
	aiPreferences.aiFreeMode = false;
	aiPreferences.cloudConnectorsEnabled = true;
	aiPreferences.agenticWorkflowsEnabled = true;
	aiPreferences.preferredReasoningConnectorId = QStringLiteral("openai");
	aiPreferences.preferredLocalConnectorId = QStringLiteral("local-offline");
	aiPreferences.preferredTextModelId = QStringLiteral("openai-text-default");
	aiPreferences.meshyCredentialEnvironmentVariable = QStringLiteral("VIBESTUDIO_TEST_MESHY_KEY");
	settings.setAiAutomationPreferences(aiPreferences);
	settings.sync();

	vibestudio::StudioSettings reloaded(settingsPath);
	const QVector<vibestudio::RecentProject> projects = reloaded.recentProjects();
	if (projects.size() != 2) {
		return fail("Expected duplicate recent project records to collapse.");
	}
	if (projects[0].displayName != QStringLiteral("Alpha Renamed")) {
		return fail("Expected most recently opened project first.");
	}
	if (reloaded.currentProjectPath() != QDir::cleanPath(alphaProject)) {
		return fail("Expected current project path to persist.");
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
	const QVector<vibestudio::RecentActivityTask> activities = reloaded.recentActivityTasks();
	if (activities.size() != 1 || activities.front().id != QStringLiteral("compiler-ericw-qbsp") || activities.front().source != QStringLiteral("compiler") || activities.front().state != vibestudio::OperationState::Completed || activities.front().warnings.size() != 1) {
		return fail("Expected recent activity task history to persist.");
	}
	QVector<vibestudio::GameInstallationProfile> installations = reloaded.gameInstallations();
	if (installations.size() != 1 || installations.front().displayName != QStringLiteral("Quake Test") || installations.front().engineFamily != vibestudio::GameEngineFamily::IdTech2) {
		return fail("Expected manual game installation to persist.");
	}
	if (reloaded.selectedGameInstallationId() != installations.front().id) {
		return fail("Expected first installation to become selected.");
	}
	if (reloaded.selectedEditorProfileId() != QStringLiteral("trenchbroom")) {
		return fail("Expected selected editor profile to persist with normalized id.");
	}
	if (reloaded.compilerToolPathOverrides().size() != 1 || reloaded.compilerToolPathOverrides().front().toolId != QStringLiteral("ericw-qbsp")) {
		return fail("Expected compiler executable override to persist.");
	}
	const vibestudio::AiAutomationPreferences reloadedAiPreferences = reloaded.aiAutomationPreferences();
	if (reloadedAiPreferences.aiFreeMode || !reloadedAiPreferences.cloudConnectorsEnabled || !reloadedAiPreferences.agenticWorkflowsEnabled || reloadedAiPreferences.preferredReasoningConnectorId != QStringLiteral("openai") || reloadedAiPreferences.preferredLocalConnectorId != QStringLiteral("local-offline") || reloadedAiPreferences.preferredTextModelId != QStringLiteral("openai-text-default") || reloadedAiPreferences.meshyCredentialEnvironmentVariable != QStringLiteral("VIBESTUDIO_TEST_MESHY_KEY")) {
		return fail("Expected AI opt-in preferences to persist.");
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
	reloaded.setSelectedEditorProfileId(QStringLiteral("missing-profile"));
	vibestudio::AiAutomationPreferences disabledAiPreferences = reloaded.aiAutomationPreferences();
	disabledAiPreferences.aiFreeMode = true;
	disabledAiPreferences.cloudConnectorsEnabled = true;
	disabledAiPreferences.agenticWorkflowsEnabled = true;
	disabledAiPreferences.preferredReasoningConnectorId = QStringLiteral("missing");
	reloaded.setAiAutomationPreferences(disabledAiPreferences);
	const vibestudio::AccessibilityPreferences normalizedPreferences = reloaded.accessibilityPreferences();
	if (normalizedPreferences.localeName != QStringLiteral("en") || normalizedPreferences.textScalePercent != 200 || normalizedPreferences.theme != vibestudio::StudioTheme::HighContrastDark || normalizedPreferences.density != vibestudio::UiDensity::Comfortable || normalizedPreferences.reducedMotion || normalizedPreferences.textToSpeechEnabled) {
		return fail("Expected preference normalization and individual setters.");
	}
	if (reloaded.selectedEditorProfileId() != QStringLiteral("vibestudio-default")) {
		return fail("Expected invalid editor profile selection to fall back to default.");
	}
	const vibestudio::AiAutomationPreferences normalizedAiPreferences = reloaded.aiAutomationPreferences();
	if (!normalizedAiPreferences.aiFreeMode || normalizedAiPreferences.cloudConnectorsEnabled || normalizedAiPreferences.agenticWorkflowsEnabled || !normalizedAiPreferences.preferredReasoningConnectorId.isEmpty()) {
		return fail("Expected AI-free mode and invalid connector preferences to normalize.");
	}

	for (int index = 0; index < vibestudio::StudioSettings::kMaximumRecentProjects + 4; ++index) {
		reloaded.recordRecentProject(tempDir.filePath(QStringLiteral("Project %1").arg(index)));
	}
	if (reloaded.recentProjects().size() != vibestudio::StudioSettings::kMaximumRecentProjects) {
		return fail("Expected recent project list to stay bounded.");
	}
	for (int index = 0; index < vibestudio::StudioSettings::kMaximumRecentActivityTasks + 4; ++index) {
		vibestudio::RecentActivityTask activity;
		activity.id = QStringLiteral("package-task-%1").arg(index);
		activity.title = QStringLiteral("Package Task %1").arg(index);
		activity.detail = tempDir.filePath(QStringLiteral("package-%1.pak").arg(index));
		activity.source = QStringLiteral("package");
		activity.state = vibestudio::OperationState::Completed;
		activity.resultSummary = QStringLiteral("Task %1 complete.").arg(index);
		activity.updatedUtc = QDateTime::fromString(QStringLiteral("2026-04-30T10:%1:00Z").arg(index % 60, 2, 10, QLatin1Char('0')), Qt::ISODate);
		reloaded.recordRecentActivityTask(activity);
	}
	if (reloaded.recentActivityTasks().size() != vibestudio::StudioSettings::kMaximumRecentActivityTasks) {
		return fail("Expected recent activity task list to stay bounded.");
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
	reloaded.clearRecentActivityTasks();
	if (!reloaded.recentActivityTasks().isEmpty()) {
		return fail("Expected recent activity task history to clear.");
	}

	reloaded.removeGameInstallation(installations.front().id);
	if (!reloaded.gameInstallations().isEmpty() || !reloaded.selectedGameInstallationId().isEmpty()) {
		return fail("Expected game installation removal to clear selection.");
	}
	reloaded.removeCompilerToolPathOverride(QStringLiteral("ericw-qbsp"));
	if (!reloaded.compilerToolPathOverrides().isEmpty()) {
		return fail("Expected compiler executable override removal.");
	}

	return EXIT_SUCCESS;
}
