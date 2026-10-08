#include "core/studio_settings.h"

#include "core/ai_connectors.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimeZone>

#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message)
{
	std::cerr << message << "\n";
	return EXIT_FAILURE;
}

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool samePath(const QString& left, const QString& right)
{
	return QFileInfo(left).absoluteFilePath() == QFileInfo(right).absoluteFilePath();
}

bool runPreferenceAndHistorySmoke(const QDir& root)
{
	bool ok = true;
	const QString settingsPath = root.filePath(QStringLiteral("settings.ini"));
	const QString alphaProject = root.filePath(QStringLiteral("Alpha Project"));
	const QString betaProject = root.filePath(QStringLiteral("Beta Project"));
	const QString quakeInstall = root.filePath(QStringLiteral("Quake Install"));
	if (!QDir().mkpath(alphaProject) || !QDir().mkpath(betaProject) || !QDir().mkpath(quakeInstall)) {
		return expect(false, "Expected test project directories to be created.");
	}

	vibestudio::StudioSettings settings(settingsPath);
	ok &= expect(settings.schemaVersion() == vibestudio::StudioSettings::kSchemaVersion, "Expected default settings schema version.");
	ok &= expect(!settings.storedSchemaIsNewer() && !settings.isReadOnly(), "Expected a fresh store to be writable.");
	ok &= expect(settings.recentProjects().isEmpty(), "Expected no recent projects in a fresh settings file.");
	const vibestudio::AccessibilityPreferences defaultPreferences = settings.accessibilityPreferences();
	ok &= expect(defaultPreferences.localeName == QStringLiteral("system") && defaultPreferences.textScalePercent == 100 && defaultPreferences.theme == vibestudio::StudioTheme::Dark && defaultPreferences.density == vibestudio::UiDensity::Standard,
		"Expected default accessibility and language preferences.");
	ok &= expect(defaultPreferences.formatLocaleName == QStringLiteral("system") && defaultPreferences.colorVision == vibestudio::ColorVision::Typical
			&& !defaultPreferences.reducedSaturation && !defaultPreferences.thickFocusIndicator && !defaultPreferences.thickTextCursor
			&& !defaultPreferences.steadyTextCursor && defaultPreferences.uiFontFamily.isEmpty() && !defaultPreferences.wideTextSpacing
			&& defaultPreferences.messageDuration == vibestudio::MessageDuration::Standard && defaultPreferences.visualAlerts
			&& !defaultPreferences.soundCues && defaultPreferences.soundCueVolume == 60
			&& defaultPreferences.screenReaderAnnouncements && !defaultPreferences.textToSpeechEnabled
			&& defaultPreferences.speechEvents == QStringList({QStringLiteral("task-results"), QStringLiteral("task-problems")})
			&& defaultPreferences.speechVoice.isEmpty() && defaultPreferences.speechRate == 0 && defaultPreferences.speechPitch == 0 && defaultPreferences.speechVolume == 100,
		"Expected default region, vision, timing, alert, and speech preferences.");
	ok &= expect(settings.selectedEditorProfileId() == QStringLiteral("vibestudio-default"), "Expected default editor profile selection.");
	const vibestudio::AiAutomationPreferences defaultAiPreferences = settings.aiAutomationPreferences();
	ok &= expect(defaultAiPreferences.aiFreeMode && !defaultAiPreferences.cloudConnectorsEnabled && !defaultAiPreferences.agenticWorkflowsEnabled,
		"Expected AI automation to be disabled by default.");
	const vibestudio::SetupProgress defaultSetup = settings.setupProgress();
	ok &= expect(!defaultSetup.started && !defaultSetup.skipped && !defaultSetup.completed && defaultSetup.currentStep == vibestudio::SetupStep::WelcomeAccess,
		"Expected default setup state.");
	ok &= expect(settings.setupSummary().status == QStringLiteral("not-started"), "Expected default setup summary status.");

	settings.recordRecentProject(alphaProject, QStringLiteral("Alpha"), QDateTime::fromString(QStringLiteral("2026-04-29T08:00:00Z"), Qt::ISODate));
	settings.recordRecentProject(betaProject, QString(), QDateTime::fromString(QStringLiteral("2026-04-29T09:00:00Z"), Qt::ISODate));
	settings.recordRecentProject(alphaProject, QStringLiteral("Alpha Renamed"), QDateTime::fromString(QStringLiteral("2026-04-29T10:00:00Z"), Qt::ISODate));
	settings.setCurrentProjectPath(alphaProject);
	settings.setSelectedMode(5);
	settings.setShellGeometry(QByteArray("geometry-bytes"));
	settings.setShellWindowState(QByteArray("state-bytes"));
	ok &= expect(settings.shellModeRailBehaviour() == QStringLiteral("automatic"), "Expected the mode rail to fold to icons by default.");
	settings.setShellModeRailBehaviour(QStringLiteral("Expanded"));
	ok &= expect(settings.restoreSession() && settings.lastSession().isEmpty(), "Expected the session to reopen by default, with nothing to reopen yet.");
	ok &= expect(settings.crashReports(), "Expected crash reports to be kept by default.");
	settings.setCrashReports(false);
	ok &= expect(!settings.crashReports(), "Expected crash reports to turn off.");
	settings.setCrashReports(true);
	ok &= expect(settings.codeRecoveryEnabled(), "Expected local Code recovery to be enabled by default.");
	settings.setCodeRecoveryEnabled(false);
	ok &= expect(settings.audioRecoveryEnabled() && settings.audioRecoveryNotifyAtStartup(),
		"Audio checkpoints and startup discovery are enabled by default.");
	settings.setAudioRecoveryEnabled(false);
	settings.setAudioRecoveryNotifyAtStartup(false);
	settings.sync();
	vibestudio::StudioSettings audioReloaded(settingsPath);
	ok &= expect(!audioReloaded.audioRecoveryEnabled() && !audioReloaded.audioRecoveryNotifyAtStartup(),
		"Audio checkpoint and notification preferences persist independently.");
	settings.setAudioRecoveryNotifyAtStartup(true);
	ok &= expect(!settings.audioRecoveryEnabled() && settings.audioRecoveryNotifyAtStartup(),
		"Existing audio copies can still be offered when checkpointing is disabled.");
	settings.setAudioRecoveryEnabled(true);
	ok &= expect(settings.textureRecoveryEnabled() && settings.textureRecoveryIntervalSeconds() == 30, "Expected local texture recovery every 30 seconds by default.");
	settings.setTextureRecoveryEnabled(false); settings.setTextureRecoveryIntervalSeconds(1);
	ok &= expect(!settings.textureRecoveryEnabled() && settings.textureRecoveryIntervalSeconds() == 5, "Texture recovery can be disabled and its interval has a lower bound.");
	settings.setTextureRecoveryIntervalSeconds(9999);
	ok &= expect(settings.textureRecoveryIntervalSeconds() == 600, "Texture recovery interval has an upper bound.");
	settings.setTextureRecoveryEnabled(true); settings.setTextureRecoveryIntervalSeconds(30);
	settings.sync();
	ok &= expect(!vibestudio::StudioSettings(settingsPath).codeRecoveryEnabled(), "Expected the Code recovery preference to persist across settings instances.");
	settings.setCodeRecoveryEnabled(true);
	ok &= expect(settings.codeZoomPercent() == 100, "Expected the code editor to start at 100% zoom.");
	ok &= expect(settings.userShortcuts().isEmpty(), "Expected no keys of the user's own at first.");
	settings.setUserShortcuts({{QStringLiteral("code.zoomReset"), {QStringLiteral("Ctrl+Shift+G"), QStringLiteral("Ctrl+K, Ctrl+Z")}},
		{QStringLiteral("map.undo"), {}}});
	const QHash<QString, QStringList> keys = settings.userShortcuts();
	ok &= expect(keys.size() == 2 && keys.value(QStringLiteral("code.zoomReset")) == QStringList {QStringLiteral("Ctrl+Shift+G"), QStringLiteral("Ctrl+K, Ctrl+Z")}
			&& keys.contains(QStringLiteral("map.undo")) && keys.value(QStringLiteral("map.undo")).isEmpty(),
		"Expected the user's keys to come back as set, a chord kept whole and a command left without keys kept.");
	settings.setUserShortcuts({});
	ok &= expect(settings.userShortcuts().isEmpty(), "Expected setting no keys to forget them all.");
	settings.setCodeZoomPercent(900);
	ok &= expect(settings.codeZoomPercent() == 300, "Expected the code editor zoom to stop at 300%.");
	settings.setCodeZoomPercent(100);
	ok &= expect(settings.recentFilterQueries(QStringLiteral("levelObjects")).isEmpty(), "Expected no recent filter queries at first.");
	settings.recordFilterQuery(QStringLiteral("levelObjects"), QStringLiteral("tag=3"));
	settings.recordFilterQuery(QStringLiteral("levelObjects"), QStringLiteral("class=light"));
	settings.recordFilterQuery(QStringLiteral("levelObjects"), QStringLiteral("TAG=3"));
	ok &= expect(settings.recentFilterQueries(QStringLiteral("levelObjects")) == QStringList {QStringLiteral("TAG=3"), QStringLiteral("class=light")},
		"Expected recent queries most recent first, each once whatever its case.");
	ok &= expect(settings.recentFilterQueries(QStringLiteral("packageEntries")).isEmpty(), "Expected each filter to keep its own queries.");
	ok &= expect(settings.codeStickyHeaders(), "Expected the code editor's sticky headers to start on.");
	settings.setCodeStickyHeaders(false);
	ok &= expect(!settings.codeStickyHeaders(), "Expected sticky headers to turn off.");
	settings.setCodeStickyHeaders(true);
	vibestudio::StudioSession left;
	left.codeFiles = {QStringLiteral("a.cfg")};
	vibestudio::StudioSession right = left;
	ok &= expect(left == right, "Expected equal sessions to compare equal.");
	right.currentCodeFile = QStringLiteral("a.cfg");
	ok &= expect(!(left == right), "Expected sessions differing in the file in front to differ.");
	vibestudio::StudioSession session;
	session.packagePath = QStringLiteral("C:/games/quake/id1/pak0.pak");
	session.mapPath = QStringLiteral("C:/maps/rooms.wad");
	session.mapName = QStringLiteral("MAP02");
	session.codeFiles = {QStringLiteral("C:/src/a.qc"), QStringLiteral("C:/src/b.cfg")};
	session.currentCodeFile = QStringLiteral("C:/src/b.cfg");
	settings.setLastSession(session);
	settings.setRestoreSession(false);
	settings.setShellLayoutState(QStringLiteral("levelsWorkbench"), QByteArray("splitter-bytes"));
	settings.setShellLayoutState(QStringLiteral("  "), QByteArray("ignored"));
	for (int index = 0; index < vibestudio::StudioSettings::kMaximumRecentFiles + 3; ++index) {
		settings.recordRecentFile(QStringLiteral("map"), QStringLiteral("C:/maps/map%1.map").arg(index));
	}
	settings.recordRecentFile(QStringLiteral("map"), QStringLiteral("C:/maps/map5.map"));
	settings.recordRecentFile(QStringLiteral("package"), QStringLiteral("C:/paks/pak0.pk3"));
	settings.recordRecentFile(QStringLiteral("code"), QStringLiteral("C:/scripts/autoexec.cfg"));
	settings.removeRecentFile(QStringLiteral("code"), QStringLiteral("C:/scripts/autoexec.cfg"));
	settings.recordRecentFile(QStringLiteral("relative"), QStringLiteral("sources/texture.cfg"));
	settings.removeRecentFile(QStringLiteral("relative"), QStringLiteral("./sources/texture.cfg"));
	ok &= expect(settings.recentFiles(QStringLiteral("relative")).isEmpty(), "Expected equivalent relative paths to remove the recorded absolute recent file.");
	settings.recordRecentFile(QStringLiteral("  "), QStringLiteral("C:/ignored.txt"));
	settings.setLaunchGameDirectory(QStringLiteral("quake-1234"), QStringLiteral(" mymod "));
	settings.setLaunchGameDirectory(QStringLiteral("quake3-5678"), QStringLiteral("baseq3"));
	settings.setLaunchGameDirectory(QStringLiteral("quake3-5678"), QString());
	settings.setLaunchGameDirectory(QStringLiteral("  "), QStringLiteral("ignored"));
	for (int index = 0; index < vibestudio::StudioSettings::kMaximumRecentCommands + 2; ++index) {
		settings.recordRecentCommand(QStringLiteral("command.%1").arg(index));
	}
	settings.recordRecentCommand(QStringLiteral("command.3"));
	settings.recordRecentCommand(QStringLiteral("  "));

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
	compilerActivity.cancellable = true;
	compilerActivity.durationMs = 4242;
	compilerActivity.progress = {3, 7};
	compilerActivity.transitions.push_back({compilerActivity.createdUtc, vibestudio::OperationState::Running, 0, QStringLiteral("started")});
	compilerActivity.transitions.push_back({compilerActivity.updatedUtc, vibestudio::OperationState::Completed, 4242, QStringLiteral("finished")});
	compilerActivity.log.push_back({compilerActivity.createdUtc, vibestudio::OperationState::Running, QStringLiteral("qbsp: reading maps/start.map")});
	compilerActivity.log.push_back({compilerActivity.updatedUtc, vibestudio::OperationState::Completed, QStringLiteral("qbsp: wrote maps/start.bsp")});
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
	preferences.formatLocaleName = QStringLiteral("de_CH");
	preferences.colorVision = vibestudio::colorVisionFromId(QStringLiteral("deuteranopia"));
	preferences.reducedSaturation = true;
	preferences.thickFocusIndicator = true;
	preferences.thickTextCursor = true;
	preferences.steadyTextCursor = true;
	preferences.uiFontFamily = QStringLiteral("  Atkinson Hyperlegible ");
	preferences.wideTextSpacing = true;
	preferences.messageDuration = vibestudio::messageDurationFromId(QStringLiteral("until_replaced"));
	preferences.visualAlerts = false;
	preferences.soundCues = true;
	preferences.soundCueVolume = 140;
	preferences.screenReaderAnnouncements = false;
	preferences.speechEvents = {QStringLiteral("status-messages"), QStringLiteral("unknown-event"), QStringLiteral("TASK_PROBLEMS")};
	preferences.speechVoice = QStringLiteral("voice-id");
	preferences.speechRate = 25;
	preferences.speechPitch = -4;
	preferences.speechVolume = 60;
	settings.setAccessibilityPreferences(preferences);
	settings.setSelectedEditorProfileId(QStringLiteral("TrenchBroom"));
	settings.upsertCompilerToolPathOverride({QStringLiteral("ericw-qbsp"), root.filePath(QStringLiteral("qbsp-test"))});
	vibestudio::AiAutomationPreferences aiPreferences;
	aiPreferences.aiFreeMode = false;
	aiPreferences.cloudConnectorsEnabled = true;
	aiPreferences.agenticWorkflowsEnabled = true;
	aiPreferences.preferredReasoningConnectorId = QStringLiteral("openai");
	aiPreferences.preferredLocalConnectorId = QStringLiteral("local-offline");
	aiPreferences.preferredTextModelId = QStringLiteral("openai-text-default");
	aiPreferences.meshyCredentialEnvironmentVariable = QStringLiteral("VIBESTUDIO_TEST_MESHY_KEY");
	aiPreferences.connectorModels.insert(QStringLiteral("Local-Offline"), QStringLiteral(" llama-test "));
	aiPreferences.connectorEndpoints.insert(QStringLiteral("local-offline"), QStringLiteral("http://localhost:1234/v1"));
	aiPreferences.connectorModels.insert(QStringLiteral("no-such-connector"), QStringLiteral("x"));
	settings.setAiAutomationPreferences(aiPreferences);
	settings.setAiContextConsentGiven(alphaProject, QStringLiteral("Claude@API.anthropic.com"), true);
	settings.setAiContextConsentGiven(alphaProject, QStringLiteral("gemini@generativelanguage.googleapis.com"), true);
	settings.setAiContextConsentGiven(alphaProject, QStringLiteral("gemini@generativelanguage.googleapis.com"), false);
	settings.sync();

	vibestudio::StudioSettings reloaded(settingsPath);
	const QVector<vibestudio::RecentProject> projects = reloaded.recentProjects();
	ok &= expect(projects.size() == 2, "Expected duplicate recent project records to collapse.");
	if (projects.size() == 2) {
		ok &= expect(projects[0].displayName == QStringLiteral("Alpha Renamed"), "Expected most recently opened project first.");
		ok &= expect(projects[0].exists && projects[1].exists, "Expected existing project directories to be marked ready.");
	}
	ok &= expect(reloaded.currentProjectPath() == QDir::cleanPath(alphaProject), "Expected current project path to persist.");
	ok &= expect(reloaded.selectedMode() == 5, "Expected selected shell mode to persist.");
	ok &= expect(reloaded.shellGeometry() == QByteArray("geometry-bytes") && reloaded.shellWindowState() == QByteArray("state-bytes"),
		"Expected shell geometry and state to persist.");
	ok &= expect(reloaded.shellModeRailBehaviour() == QStringLiteral("expanded"), "Expected the pinned mode rail preference to persist.");
	reloaded.setShellModeRailBehaviour(QStringLiteral("sideways"));
	ok &= expect(reloaded.shellModeRailBehaviour() == QStringLiteral("automatic"), "Expected an unknown rail behaviour to read back as automatic.");
	reloaded.setShellModeRailBehaviour(QStringLiteral("expanded"));
	{
		const vibestudio::StudioSession restored = reloaded.lastSession();
		ok &= expect(!reloaded.restoreSession() && restored.packagePath == QStringLiteral("C:/games/quake/id1/pak0.pak")
				&& restored.mapPath == QStringLiteral("C:/maps/rooms.wad") && restored.mapName == QStringLiteral("MAP02")
				&& restored.codeFiles == QStringList {QStringLiteral("C:/src/a.qc"), QStringLiteral("C:/src/b.cfg")}
				&& restored.currentCodeFile == QStringLiteral("C:/src/b.cfg"),
			"Expected the last session and the reopen preference to persist.");
	}
	ok &= expect(reloaded.shellLayoutState(QStringLiteral("levelsWorkbench")) == QByteArray("splitter-bytes"),
		"Expected a named work-surface layout to persist.");
	ok &= expect(reloaded.shellLayoutState(QStringLiteral("packagesWorkbench")).isEmpty(),
		"Expected an unknown layout key to read back empty.");
	ok &= expect(reloaded.shellLayoutState(QStringLiteral("  ")).isEmpty(), "Expected a blank layout key to be ignored.");
	const QStringList recentMaps = reloaded.recentFiles(QStringLiteral("map"));
	ok &= expect(recentMaps.size() == vibestudio::StudioSettings::kMaximumRecentFiles, "Expected recent maps to stop at the limit.");
	ok &= expect(!recentMaps.isEmpty() && recentMaps.first().endsWith(QStringLiteral("map5.map")), "Expected a reopened map to move to the top.");
	ok &= expect(recentMaps.filter(QStringLiteral("map5.map")).size() == 1, "Expected a reopened map to appear once.");
	ok &= expect(reloaded.recentFiles(QStringLiteral("package")).size() == 1, "Expected recent packages to persist separately.");
	ok &= expect(reloaded.recentFiles(QStringLiteral("code")).isEmpty(), "Expected a removed recent file to stay removed.");
	ok &= expect(reloaded.recentFiles(QStringLiteral("  ")).isEmpty(), "Expected a blank recent-file kind to be ignored.");
	ok &= expect(reloaded.launchGameDirectory(QStringLiteral("quake-1234")) == QStringLiteral("mymod"), "Expected the launch game folder to persist per installation.");
	ok &= expect(reloaded.launchGameDirectory(QStringLiteral("quake3-5678")).isEmpty(), "Expected clearing a launch game folder to forget it.");
	ok &= expect(reloaded.launchGameDirectory(QStringLiteral("  ")).isEmpty(), "Expected a blank installation id to be ignored.");
	const QStringList recentCommands = reloaded.recentCommands();
	ok &= expect(recentCommands.size() == vibestudio::StudioSettings::kMaximumRecentCommands, "Expected recent commands to stop at the limit.");
	ok &= expect(recentCommands.value(0) == QStringLiteral("command.3") && recentCommands.count(QStringLiteral("command.3")) == 1,
		"Expected a command run again to move to the front once.");
	reloaded.clearRecentFiles();
	ok &= expect(reloaded.recentFiles(QStringLiteral("map")).isEmpty() && reloaded.recentFiles(QStringLiteral("package")).isEmpty(),
		"Expected clearing recent files to clear every kind.");

	const QVector<vibestudio::RecentActivityTask> activities = reloaded.recentActivityTasks();
	ok &= expect(activities.size() == 1, "Expected recent activity task history to persist.");
	if (!activities.isEmpty()) {
		const vibestudio::RecentActivityTask& task = activities.front();
		ok &= expect(task.id == QStringLiteral("compiler-ericw-qbsp") && task.source == QStringLiteral("compiler") && task.state == vibestudio::OperationState::Completed && task.warnings.size() == 1,
			"Expected recent activity task headline fields to persist.");
		// The detail behind the headline must survive too.
		ok &= expect(task.durationMs == 4242, "Expected task duration to persist.");
		ok &= expect(task.cancellable, "Expected task cancellability to persist.");
		ok &= expect(task.progress.current == 3 && task.progress.total == 7, "Expected task progress to persist.");
		ok &= expect(task.transitions.size() == 2 && task.transitions.last().state == vibestudio::OperationState::Completed && task.transitions.last().elapsedMs == 4242,
			"Expected the task transition timeline to persist.");
		ok &= expect(task.log.size() == 2 && task.log.last().message.contains(QStringLiteral("start.bsp")),
			"Expected captured compiler output to persist.");
		ok &= expect(!task.logTruncated && task.droppedLogEntryCount == 0, "Expected a short log to persist untruncated.");
	}

	QVector<vibestudio::GameInstallationProfile> installations = reloaded.gameInstallations();
	ok &= expect(installations.size() == 1 && installations.front().displayName == QStringLiteral("Quake Test") && installations.front().engineFamily == vibestudio::GameEngineFamily::IdTech2,
		"Expected manual game installation to persist.");
	if (installations.isEmpty()) {
		return ok;
	}
	ok &= expect(reloaded.selectedGameInstallationId() == installations.front().id, "Expected first installation to become selected.");
	ok &= expect(reloaded.selectedEditorProfileId() == QStringLiteral("trenchbroom"), "Expected selected editor profile to persist with normalized id.");
	ok &= expect(reloaded.compilerToolPathOverrides().size() == 1 && reloaded.compilerToolPathOverrides().front().toolId == QStringLiteral("ericw-qbsp"),
		"Expected compiler executable override to persist.");

	const vibestudio::AiAutomationPreferences reloadedAiPreferences = reloaded.aiAutomationPreferences();
	ok &= expect(!reloadedAiPreferences.aiFreeMode && reloadedAiPreferences.cloudConnectorsEnabled && reloadedAiPreferences.agenticWorkflowsEnabled
			&& reloadedAiPreferences.preferredReasoningConnectorId == QStringLiteral("openai")
			&& reloadedAiPreferences.preferredLocalConnectorId == QStringLiteral("local-offline")
			&& reloadedAiPreferences.preferredTextModelId == QStringLiteral("openai-text-default")
			&& reloadedAiPreferences.meshyCredentialEnvironmentVariable == QStringLiteral("VIBESTUDIO_TEST_MESHY_KEY"),
		"Expected AI opt-in preferences to persist.");
	ok &= expect(reloadedAiPreferences.connectorModels.value(QStringLiteral("local-offline")) == QStringLiteral("llama-test")
			&& reloadedAiPreferences.connectorEndpoints.value(QStringLiteral("local-offline")) == QStringLiteral("http://localhost:1234/v1")
			&& !reloadedAiPreferences.connectorModels.contains(QStringLiteral("no-such-connector")),
		"Expected per-connector models and endpoints to persist, trimmed, for known connectors only.");
	ok &= expect(reloaded.aiContextConsentGiven(alphaProject, QStringLiteral("claude@api.anthropic.com"))
			&& !reloaded.aiContextConsentGiven(alphaProject, QStringLiteral("gemini@generativelanguage.googleapis.com"))
			&& !reloaded.aiContextConsentGiven(QString(), QStringLiteral("claude@api.anthropic.com")),
		"Expected consent to send context to persist per project and destination.");
	{
		vibestudio::AiAutomationPreferences cleared = reloaded.aiAutomationPreferences();
		cleared.connectorEndpoints.clear();
		reloaded.setAiAutomationPreferences(cleared);
		ok &= expect(reloaded.aiAutomationPreferences().connectorEndpoints.isEmpty() && !reloaded.aiAutomationPreferences().connectorModels.isEmpty(),
			"Expected clearing a connector's endpoint to forget it and keep its model.");
		reloaded.clearAiContextConsent();
		ok &= expect(!reloaded.aiContextConsentGiven(alphaProject, QStringLiteral("claude@api.anthropic.com")), "Expected consent to clear.");
	}

	vibestudio::SetupProgress reloadedSetup = reloaded.setupProgress();
	ok &= expect(reloadedSetup.started && reloadedSetup.skipped && !reloadedSetup.completed && reloadedSetup.currentStep == vibestudio::SetupStep::ProjectsPackages,
		"Expected setup skip/resume state to persist.");
	ok &= expect(reloaded.setupSummary().status == QStringLiteral("skipped"), "Expected skipped setup summary.");
	reloaded.startOrResumeSetup(reloadedSetup.currentStep);
	reloaded.advanceSetup();
	reloaded.completeSetup();
	reloadedSetup = reloaded.setupProgress();
	ok &= expect(reloadedSetup.started && !reloadedSetup.skipped && reloadedSetup.completed && reloadedSetup.currentStep == vibestudio::SetupStep::ReviewFinish,
		"Expected setup completion state.");
	ok &= expect(reloaded.setupSummary().status == QStringLiteral("complete"), "Expected complete setup summary.");
	reloaded.resetSetup();
	ok &= expect(!reloaded.setupProgress().started && reloaded.setupSummary().status == QStringLiteral("not-started"), "Expected reset setup state.");
	ok &= expect(vibestudio::setupStepFromId(QStringLiteral("AI_AUTOMATION")) == vibestudio::SetupStep::AiAutomation, "Expected setup step id normalization.");
	// Step names are title case as the Settings pages write it: joining words
	// stay lower case ("Welcome and Access", not "Welcome And Access").
	for (vibestudio::SetupStep step : vibestudio::setupSteps()) {
		const QString name = vibestudio::setupStepDisplayName(step);
		ok &= expect(!name.isEmpty() && !name.contains(QStringLiteral(" And ")) && !name.contains(QStringLiteral(" Or ")),
			"Expected setup step names to keep joining words lower case.");
	}
	ok &= expect(vibestudio::setupStepDisplayName(vibestudio::SetupStep::AiAutomation) == QStringLiteral("AI and Automation"),
		"Expected the AI step to read \"AI and Automation\".");

	const vibestudio::AccessibilityPreferences reloadedPreferences = reloaded.accessibilityPreferences();
	ok &= expect(reloadedPreferences.localeName == QStringLiteral("pt-BR") && reloadedPreferences.textScalePercent == 175 && reloadedPreferences.theme == vibestudio::StudioTheme::HighContrastLight && reloadedPreferences.density == vibestudio::UiDensity::Compact && reloadedPreferences.reducedMotion && reloadedPreferences.textToSpeechEnabled,
		"Expected accessibility and language preferences to persist.");
	ok &= expect(reloadedPreferences.formatLocaleName == QStringLiteral("de-CH") && reloadedPreferences.colorVision == vibestudio::ColorVision::RedGreen
			&& reloadedPreferences.reducedSaturation && reloadedPreferences.thickFocusIndicator && reloadedPreferences.thickTextCursor
			&& reloadedPreferences.steadyTextCursor && reloadedPreferences.uiFontFamily == QStringLiteral("Atkinson Hyperlegible") && reloadedPreferences.wideTextSpacing
			&& reloadedPreferences.messageDuration == vibestudio::MessageDuration::UntilReplaced && !reloadedPreferences.visualAlerts
			&& reloadedPreferences.soundCues && reloadedPreferences.soundCueVolume == 100
			&& !reloadedPreferences.screenReaderAnnouncements,
		"Expected region, vision, timing, and alert preferences to persist normalized, the cue volume clamped.");
	ok &= expect(reloadedPreferences.speechEvents == QStringList({QStringLiteral("task-problems"), QStringLiteral("status-messages")})
			&& reloadedPreferences.speechVoice == QStringLiteral("voice-id") && reloadedPreferences.speechRate == 10
			&& reloadedPreferences.speechPitch == -4 && reloadedPreferences.speechVolume == 60,
		"Expected speech preferences to persist with known events only and the rate clamped.");
	{
		// Every speech event switched off stays off; it is not the default.
		vibestudio::AccessibilityPreferences silent = reloadedPreferences;
		silent.speechEvents.clear();
		reloaded.setAccessibilityPreferences(silent);
		ok &= expect(reloaded.accessibilityPreferences().speechEvents.isEmpty(), "Expected an empty speech event list to persist as empty.");
		reloaded.setAccessibilityPreferences(reloadedPreferences);
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
	ok &= expect(normalizedPreferences.localeName == QStringLiteral("en") && normalizedPreferences.textScalePercent == 200 && normalizedPreferences.theme == vibestudio::StudioTheme::HighContrastDark && normalizedPreferences.density == vibestudio::UiDensity::Comfortable && !normalizedPreferences.reducedMotion && !normalizedPreferences.textToSpeechEnabled,
		"Expected preference normalization and individual setters.");
	ok &= expect(reloaded.selectedEditorProfileId() == QStringLiteral("vibestudio-default"), "Expected invalid editor profile selection to fall back to default.");
	const vibestudio::AiAutomationPreferences normalizedAiPreferences = reloaded.aiAutomationPreferences();
	ok &= expect(normalizedAiPreferences.aiFreeMode && !normalizedAiPreferences.cloudConnectorsEnabled && !normalizedAiPreferences.agenticWorkflowsEnabled && normalizedAiPreferences.preferredReasoningConnectorId.isEmpty(),
		"Expected AI-free mode and invalid connector preferences to normalize.");

	for (int index = 0; index < vibestudio::StudioSettings::kMaximumRecentProjects + 4; ++index) {
		reloaded.recordRecentProject(root.filePath(QStringLiteral("Project %1").arg(index)));
	}
	ok &= expect(reloaded.recentProjects().size() == vibestudio::StudioSettings::kMaximumRecentProjects, "Expected recent project list to stay bounded.");
	for (int index = 0; index < vibestudio::StudioSettings::kMaximumRecentActivityTasks + 4; ++index) {
		vibestudio::RecentActivityTask activity;
		activity.id = QStringLiteral("package-task-%1").arg(index);
		activity.title = QStringLiteral("Package Task %1").arg(index);
		activity.detail = root.filePath(QStringLiteral("package-%1.pak").arg(index));
		activity.source = QStringLiteral("package");
		activity.state = vibestudio::OperationState::Completed;
		activity.resultSummary = QStringLiteral("Task %1 complete.").arg(index);
		activity.updatedUtc = QDateTime::fromString(QStringLiteral("2026-04-30T10:%1:00Z").arg(index % 60, 2, 10, QLatin1Char('0')), Qt::ISODate);
		reloaded.recordRecentActivityTask(activity);
	}
	ok &= expect(reloaded.recentActivityTasks().size() == vibestudio::StudioSettings::kMaximumRecentActivityTasks, "Expected recent activity task list to stay bounded.");

	reloaded.removeRecentProject(alphaProject);
	for (const vibestudio::RecentProject& project : reloaded.recentProjects()) {
		ok &= expect(project.displayName != QStringLiteral("Alpha Renamed"), "Expected selected recent project removal.");
	}

	reloaded.clearRecentProjects();
	ok &= expect(reloaded.recentProjects().isEmpty(), "Expected recent projects to clear.");
	reloaded.clearRecentActivityTasks();
	ok &= expect(reloaded.recentActivityTasks().isEmpty(), "Expected recent activity task history to clear.");

	reloaded.removeGameInstallation(installations.front().id);
	ok &= expect(reloaded.gameInstallations().isEmpty() && reloaded.selectedGameInstallationId().isEmpty(), "Expected game installation removal to clear selection.");
	reloaded.removeCompilerToolPathOverride(QStringLiteral("ericw-qbsp"));
	ok &= expect(reloaded.compilerToolPathOverrides().isEmpty(), "Expected compiler executable override removal.");
	return ok;
}

// A version 1 store is upgraded in place: ids are normalized, the legacy
// high-contrast flag folds into the theme, and retired keys are dropped.
bool runMigrationSmoke(const QDir& root)
{
	bool ok = true;
	const QString path = root.filePath(QStringLiteral("legacy-v1.ini"));
	{
		QSettings legacy(path, QSettings::IniFormat);
		legacy.setValue(QStringLiteral("app/settingsSchemaVersion"), 1);
		legacy.setValue(QStringLiteral("preferences/localeName"), QStringLiteral("pt_BR"));
		legacy.setValue(QStringLiteral("preferences/theme"), QStringLiteral("DARK"));
		legacy.setValue(QStringLiteral("preferences/density"), QStringLiteral("COMPACT"));
		legacy.setValue(QStringLiteral("preferences/textScalePercent"), 999);
		legacy.setValue(QStringLiteral("preferences/highContrast"), true);
		legacy.setValue(QStringLiteral("shell/lastMode"), QStringLiteral("packages"));
		legacy.setValue(QStringLiteral("ai/experimentalConnectorsEnabled"), true);
		legacy.sync();
	}

	{
		vibestudio::StudioSettings migrated(path);
		ok &= expect(migrated.schemaVersion() == vibestudio::StudioSettings::kSchemaVersion, "Expected a version 1 store to be migrated to the current schema.");
		ok &= expect(!migrated.storedSchemaIsNewer() && !migrated.isReadOnly(), "Expected a migrated store to stay writable.");
		ok &= expect(!migrated.migrationNotes().isEmpty(), "Expected the migration to be reported.");
		const vibestudio::AccessibilityPreferences preferences = migrated.accessibilityPreferences();
		ok &= expect(preferences.localeName == QStringLiteral("pt-BR"), "Expected the stored locale id to be normalized by the migration.");
		ok &= expect(preferences.theme == vibestudio::StudioTheme::HighContrastDark, "Expected the retired high-contrast flag to fold into the theme.");
		ok &= expect(preferences.density == vibestudio::UiDensity::Compact, "Expected the stored density id to be normalized.");
		ok &= expect(preferences.textScalePercent == 200, "Expected the stored text scale to be clamped by the migration.");
		migrated.sync();
	}

	QSettings verify(path, QSettings::IniFormat);
	ok &= expect(verify.value(QStringLiteral("app/settingsSchemaVersion")).toInt() == vibestudio::StudioSettings::kSchemaVersion,
		"Expected the migrated schema version to be written back.");
	ok &= expect(verify.value(QStringLiteral("preferences/theme")).toString() == QStringLiteral("high-contrast-dark"),
		"Expected the normalized theme id to be stored.");
	ok &= expect(verify.value(QStringLiteral("preferences/localeName")).toString() == QStringLiteral("pt-BR"),
		"Expected the normalized locale id to be stored.");
	for (const QString& retiredKey : {QStringLiteral("preferences/highContrast"), QStringLiteral("shell/lastMode"), QStringLiteral("ai/experimentalConnectorsEnabled")}) {
		ok &= expect(!verify.contains(retiredKey), "Expected retired version 1 keys to be dropped.");
	}

	// Re-opening an already migrated store must be a no-op.
	vibestudio::StudioSettings again(path);
	ok &= expect(again.schemaVersion() == vibestudio::StudioSettings::kSchemaVersion && again.migrationNotes().isEmpty(),
		"Expected a second open of a current store to migrate nothing.");
	return ok;
}

// A store written by a newer build is never reinterpreted or rewritten.
bool runNewerStoreGuardSmoke(const QDir& root)
{
	bool ok = true;
	const QString path = root.filePath(QStringLiteral("future.ini"));
	{
		QSettings future(path, QSettings::IniFormat);
		future.setValue(QStringLiteral("app/settingsSchemaVersion"), vibestudio::StudioSettings::kSchemaVersion + 97);
		future.setValue(QStringLiteral("preferences/localeName"), QStringLiteral("fr"));
		future.sync();
	}

	{
		vibestudio::StudioSettings guarded(path);
		ok &= expect(guarded.schemaVersion() == vibestudio::StudioSettings::kSchemaVersion + 97, "Expected the newer stored schema version to be reported as-is.");
		ok &= expect(guarded.storedSchemaIsNewer() && guarded.isReadOnly(), "Expected a newer store to be flagged and opened read-only.");
		ok &= expect(!guarded.migrationNotes().isEmpty(), "Expected the newer-store refusal to be reported.");
		guarded.setSelectedMode(7);
		guarded.setLocaleName(QStringLiteral("de"));
		guarded.recordRecentProject(root.filePath(QStringLiteral("Alpha Project")));
		guarded.clearRecentProjects();
		guarded.sync();
	}

	QSettings verify(path, QSettings::IniFormat);
	ok &= expect(verify.value(QStringLiteral("app/settingsSchemaVersion")).toInt() == vibestudio::StudioSettings::kSchemaVersion + 97,
		"Expected the newer schema version to survive untouched.");
	ok &= expect(verify.value(QStringLiteral("preferences/localeName")).toString() == QStringLiteral("fr"),
		"Expected a newer store never to be rewritten.");
	ok &= expect(!verify.contains(QStringLiteral("shell/selectedMode")), "Expected no writes into a newer store.");
	return ok;
}

// An oversized stored array must yield the newest entries, not the first ones.
bool runSortThenTruncateSmoke(const QDir& root)
{
	bool ok = true;
	const QString path = root.filePath(QStringLiteral("oversized.ini"));
	const int projectCount = vibestudio::StudioSettings::kMaximumRecentProjects + 6;
	const int taskCount = vibestudio::StudioSettings::kMaximumRecentActivityTasks + 6;
	{
		QSettings oversized(path, QSettings::IniFormat);
		oversized.setValue(QStringLiteral("app/settingsSchemaVersion"), vibestudio::StudioSettings::kSchemaVersion);

		// Oldest first in array order, so a truncate-before-sort read keeps
		// exactly the wrong entries.
		oversized.beginWriteArray(QStringLiteral("recentProjects"), projectCount);
		for (int index = 0; index < projectCount; ++index) {
			oversized.setArrayIndex(index);
			oversized.setValue(QStringLiteral("path"), root.filePath(QStringLiteral("Ordered Project %1").arg(index)));
			oversized.setValue(QStringLiteral("displayName"), QStringLiteral("Ordered Project %1").arg(index));
			oversized.setValue(QStringLiteral("lastOpenedUtc"), QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::utc()).addDays(index));
		}
		oversized.endArray();

		oversized.beginWriteArray(QStringLiteral("recentActivityTasks"), taskCount);
		for (int index = 0; index < taskCount; ++index) {
			oversized.setArrayIndex(index);
			oversized.setValue(QStringLiteral("id"), QStringLiteral("ordered-task-%1").arg(index));
			oversized.setValue(QStringLiteral("title"), QStringLiteral("Ordered Task %1").arg(index));
			oversized.setValue(QStringLiteral("source"), QStringLiteral("package"));
			oversized.setValue(QStringLiteral("state"), QStringLiteral("completed"));
			oversized.setValue(QStringLiteral("updatedUtc"), QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::utc()).addDays(index));
		}
		oversized.endArray();
		oversized.sync();
	}

	vibestudio::StudioSettings settings(path);
	const QVector<vibestudio::RecentProject> projects = settings.recentProjects();
	ok &= expect(projects.size() == vibestudio::StudioSettings::kMaximumRecentProjects, "Expected an oversized project array to be truncated to the cap.");
	if (!projects.isEmpty()) {
		ok &= expect(projects.front().displayName == QStringLiteral("Ordered Project %1").arg(projectCount - 1),
			"Expected the most recently opened project to survive truncation.");
		ok &= expect(projects.last().displayName == QStringLiteral("Ordered Project %1").arg(projectCount - vibestudio::StudioSettings::kMaximumRecentProjects),
			"Expected the oldest projects to be dropped, not the newest.");
	}

	const QVector<vibestudio::RecentActivityTask> tasks = settings.recentActivityTasks();
	ok &= expect(tasks.size() == vibestudio::StudioSettings::kMaximumRecentActivityTasks, "Expected an oversized activity array to be truncated to the cap.");
	if (!tasks.isEmpty()) {
		ok &= expect(tasks.front().id == QStringLiteral("ordered-task-%1").arg(taskCount - 1),
			"Expected the most recently updated task to survive truncation.");
		ok &= expect(tasks.last().id == QStringLiteral("ordered-task-%1").arg(taskCount - vibestudio::StudioSettings::kMaximumRecentActivityTasks),
			"Expected the oldest tasks to be dropped, not the newest.");
	}

	// Installations use the same read path: newest first.
	const QString firstInstall = root.filePath(QStringLiteral("Install One"));
	const QString secondInstall = root.filePath(QStringLiteral("Install Two"));
	if (!QDir().mkpath(firstInstall) || !QDir().mkpath(secondInstall)) {
		return expect(false, "Expected installation fixtures to be created.");
	}
	vibestudio::GameInstallationProfile first;
	first.rootPath = firstInstall;
	first.gameKey = QStringLiteral("quake");
	first.displayName = QStringLiteral("Install One");
	settings.upsertGameInstallation(first);
	vibestudio::GameInstallationProfile second;
	second.rootPath = secondInstall;
	second.gameKey = QStringLiteral("quake2");
	second.displayName = QStringLiteral("Install Two");
	settings.upsertGameInstallation(second);
	const QVector<vibestudio::GameInstallationProfile> installations = settings.gameInstallations();
	ok &= expect(installations.size() == 2 && installations.front().displayName == QStringLiteral("Install Two"),
		"Expected installations to be ordered by update time.");
	return ok;
}

// The process-wide override keeps automation and CI out of the real store.
bool runOverrideSmoke(const QDir& root)
{
	bool ok = true;
	const QString overridePath = root.filePath(QStringLiteral("override.ini"));
	ok &= expect(vibestudio::StudioSettings::overrideFilePath().isEmpty(), "Expected no settings override by default.");
	vibestudio::StudioSettings::setOverrideFilePath(overridePath);
	ok &= expect(samePath(vibestudio::StudioSettings::overrideFilePath(), overridePath), "Expected the settings override to be readable back.");
	{
		vibestudio::StudioSettings overridden;
		ok &= expect(samePath(overridden.storageLocation(), overridePath), "Expected the default constructor to honour the override path.");
		overridden.setSelectedMode(4);
		overridden.sync();
	}
	vibestudio::StudioSettings::setOverrideFilePath(QString());
	ok &= expect(vibestudio::StudioSettings::overrideFilePath().isEmpty(), "Expected the settings override to be clearable.");
	ok &= expect(QFileInfo::exists(overridePath), "Expected the override store to be the file that was written.");

	vibestudio::StudioSettings reopened(overridePath);
	ok &= expect(reopened.selectedMode() == 4, "Expected the override store to hold the written value.");
	return ok;
}

// Long compiler logs are kept as a bounded, explicitly truncated tail.
bool runActivityLogTruncationSmoke(const QDir& root)
{
	bool ok = true;
	const QString path = root.filePath(QStringLiteral("activity.ini"));
	const int logEntryCount = vibestudio::StudioSettings::kMaximumActivityLogEntries + 120;
	const int transitionCount = vibestudio::StudioSettings::kMaximumActivityTransitions + 16;

	vibestudio::RecentActivityTask task;
	task.id = QStringLiteral("compiler-q3map2-bsp");
	task.title = QStringLiteral("q3map2 BSP");
	task.source = QStringLiteral("compiler");
	task.state = vibestudio::OperationState::Completed;
	task.createdUtc = QDateTime::fromString(QStringLiteral("2026-05-01T09:00:00Z"), Qt::ISODate);
	task.updatedUtc = QDateTime::fromString(QStringLiteral("2026-05-01T09:00:30Z"), Qt::ISODate);
	task.finishedUtc = task.updatedUtc;
	for (int index = 0; index < logEntryCount; ++index) {
		task.log.push_back({task.createdUtc, vibestudio::OperationState::Running, QStringLiteral("q3map2 line %1").arg(index)});
	}
	for (int index = 0; index < transitionCount; ++index) {
		task.transitions.push_back({task.createdUtc, vibestudio::OperationState::Running, index, QStringLiteral("step %1").arg(index)});
	}

	vibestudio::StudioSettings settings(path);
	settings.recordRecentActivityTask(task);
	settings.sync();

	vibestudio::StudioSettings reloaded(path);
	const QVector<vibestudio::RecentActivityTask> tasks = reloaded.recentActivityTasks();
	ok &= expect(tasks.size() == 1, "Expected the long-running task to persist.");
	if (tasks.isEmpty()) {
		return ok;
	}
	const vibestudio::RecentActivityTask& stored = tasks.front();
	ok &= expect(stored.log.size() == vibestudio::StudioSettings::kMaximumActivityLogEntries, "Expected the persisted log to be capped.");
	ok &= expect(stored.logTruncated, "Expected truncation to be recorded, not hidden.");
	ok &= expect(stored.droppedLogEntryCount == logEntryCount - vibestudio::StudioSettings::kMaximumActivityLogEntries,
		"Expected the number of dropped log entries to be recorded.");
	ok &= expect(!stored.log.isEmpty() && stored.log.last().message == QStringLiteral("q3map2 line %1").arg(logEntryCount - 1),
		"Expected the newest log lines to be the ones kept.");
	ok &= expect(stored.transitions.size() == vibestudio::StudioSettings::kMaximumActivityTransitions, "Expected the transition timeline to be capped.");
	ok &= expect(stored.durationMs == 30000, "Expected a missing duration to be derived from the task timestamps.");
	return ok;
}

bool runReadOnlyInspectionSmoke(const QDir& root)
{
	using Settings = vibestudio::StudioSettings;
	const auto path = root.filePath(QStringLiteral("read-only.ini")); bool ok = true;
	{
		Settings inspected(path, Settings::AccessMode::ReadOnly);
		ok &= expect(inspected.isReadOnly() && inspected.packageImportMaximumMiB() == 8192
			&& inspected.discardedWriteCount() == 0, "Read-only inspection returns defaults without attempting schema writes.");
		inspected.setPackageImportMaximumMiB(128); inspected.sync();
		ok &= expect(inspected.discardedWriteCount() == 1, "Explicit read-only mode rejects mutation.");
	}
	ok &= expect(!QFileInfo::exists(path), "Reading defaults and refusing a write creates no settings file.");
	{
		QSettings legacy(path, QSettings::IniFormat);
		legacy.setValue(QStringLiteral("packages/importMaximumMiB"), 256);
		legacy.setValue(QStringLiteral("packages/importMaximumFiles"), 12);
		legacy.setValue(QStringLiteral("preferences/highContrast"), true);
	}
	QFile file(path); if (!file.open(QIODevice::ReadOnly)) { return false; }
	const auto before = file.readAll(); file.close(); const auto modified = QFileInfo(path).lastModified();
	{
		Settings inspected(path, Settings::AccessMode::ReadOnly);
		ok &= expect(inspected.packageImportMaximumMiB() == 256 && inspected.packageImportMaximumFiles() == 12
			&& !inspected.migrationNotes().isEmpty(), "Read-only legacy inspection respects limits and reports skipped migration.");
		inspected.sync();
	}
	if (!file.open(QIODevice::ReadOnly)) { return false; }
	ok &= expect(file.readAll() == before && QFileInfo(path).lastModified() == modified, "Read-only inspection preserves legacy settings bytes and timestamp.");
	return ok;
}

// A project's manifest can turn AI off for itself, never on, and its override
// is read with the settings without ever being saved into them.
bool runProjectAiPolicySmoke(const QDir& root)
{
	bool ok = true;
	const QString projectPath = root.filePath(QStringLiteral("ai-free-project"));
	QDir().mkpath(projectPath);
	vibestudio::ProjectManifest manifest = vibestudio::defaultProjectManifest(projectPath);
	manifest.settingsOverrides.aiFreeModeSet = true;
	manifest.settingsOverrides.aiFreeMode = true;
	QString error;
	ok &= expect(vibestudio::saveProjectManifest(manifest, &error), "Expected the AI-free project manifest to save.");
	vibestudio::StudioSettings settings(root.filePath(QStringLiteral("project-ai.ini")));
	vibestudio::AiAutomationPreferences preferences = settings.aiAutomationPreferences();
	preferences.aiFreeMode = false;
	preferences.cloudConnectorsEnabled = true;
	settings.setAiAutomationPreferences(preferences);
	ok &= expect(!settings.aiAutomationPreferences().projectAiFree, "Expected no project override with no project open.");
	settings.setCurrentProjectPath(projectPath);
	const vibestudio::AiAutomationPreferences effective = settings.aiAutomationPreferences();
	ok &= expect(effective.projectAiFree && !effective.aiFreeMode && effective.cloudConnectorsEnabled,
		"Expected the project's manifest to turn AI off without changing the studio's own AI settings.");
	settings.setAiAutomationPreferences(effective);
	settings.setCurrentProjectPath(QString());
	const vibestudio::AiAutomationPreferences after = settings.aiAutomationPreferences();
	ok &= expect(!after.aiFreeMode && after.cloudConnectorsEnabled && !after.projectAiFree,
		"Expected saving while the project is open to leave its override out of the studio settings.");

	// A project that asks for AI does not get it while the studio is AI-free.
	manifest.settingsOverrides.aiFreeMode = false;
	ok &= expect(vibestudio::saveProjectManifest(manifest, &error), "Expected the AI-allowing manifest to save.");
	preferences.aiFreeMode = true;
	settings.setAiAutomationPreferences(preferences);
	settings.setCurrentProjectPath(projectPath);
	const vibestudio::AiAutomationPreferences strict = settings.aiAutomationPreferences();
	ok &= expect(strict.aiFreeMode && !strict.projectAiFree, "Expected a project never to switch AI on against AI-free mode.");
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir tempDir;
	if (!tempDir.isValid()) {
		return fail("Expected a writable temporary directory.");
	}
	const QDir root(tempDir.path());

	bool ok = true;
	ok &= runPreferenceAndHistorySmoke(root);
	ok &= runMigrationSmoke(root);
	ok &= runNewerStoreGuardSmoke(root);
	ok &= runSortThenTruncateSmoke(root);
	ok &= runOverrideSmoke(root);
	ok &= runActivityLogTruncationSmoke(root);
	ok &= runReadOnlyInspectionSmoke(root);
	ok &= runProjectAiPolicySmoke(root);
	if (!ok) {
		return fail("studio_settings smoke test failed.");
	}
	return EXIT_SUCCESS;
}
