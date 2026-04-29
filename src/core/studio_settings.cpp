#include "core/studio_settings.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace vibestudio {

namespace {

constexpr auto kSchemaVersionKey = "app/settingsSchemaVersion";
constexpr auto kSelectedModeKey = "shell/selectedMode";
constexpr auto kShellGeometryKey = "shell/geometry";
constexpr auto kShellWindowStateKey = "shell/windowState";
constexpr auto kLocaleNameKey = "preferences/localeName";
constexpr auto kTextScalePercentKey = "preferences/textScalePercent";
constexpr auto kThemeKey = "preferences/theme";
constexpr auto kDensityKey = "preferences/density";
constexpr auto kReducedMotionKey = "preferences/reducedMotion";
constexpr auto kTextToSpeechEnabledKey = "preferences/textToSpeechEnabled";
constexpr auto kSetupStartedKey = "setup/started";
constexpr auto kSetupSkippedKey = "setup/skipped";
constexpr auto kSetupCompletedKey = "setup/completed";
constexpr auto kSetupCurrentStepKey = "setup/currentStep";
constexpr auto kSetupLastUpdatedUtcKey = "setup/lastUpdatedUtc";
constexpr auto kRecentProjectsArray = "recentProjects";
constexpr auto kRecentProjectPathKey = "path";
constexpr auto kRecentProjectDisplayNameKey = "displayName";
constexpr auto kRecentProjectLastOpenedKey = "lastOpenedUtc";

bool sameProjectPath(const QString& left, const QString& right)
{
#if defined(Q_OS_WIN)
	constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
	constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
	return QString::compare(left, right, sensitivity) == 0;
}

QDateTime normalizedTimestamp(const QDateTime& openedUtc)
{
	if (openedUtc.isValid()) {
		return openedUtc.toUTC();
	}
	return QDateTime::currentDateTimeUtc();
}

QString normalizedId(const QString& id)
{
	return id.trimmed().toLower().replace('_', '-');
}

QDateTime setupTimestamp()
{
	return QDateTime::currentDateTimeUtc();
}

int setupStepIndex(SetupStep step)
{
	const QVector<SetupStep> steps = setupSteps();
	for (int index = 0; index < steps.size(); ++index) {
		if (steps[index] == step) {
			return index;
		}
	}
	return 0;
}

} // namespace

StudioSettings::StudioSettings()
{
	ensureSchema();
}

StudioSettings::StudioSettings(const QString& filePath)
	: m_settings(filePath, QSettings::IniFormat)
{
	ensureSchema();
}

StudioSettings::~StudioSettings() = default;

QString StudioSettings::storageLocation() const
{
	return m_settings.fileName();
}

QSettings::Status StudioSettings::status() const
{
	return m_settings.status();
}

void StudioSettings::sync()
{
	m_settings.sync();
}

int StudioSettings::schemaVersion() const
{
	bool ok = false;
	const int version = m_settings.value(kSchemaVersionKey, kSchemaVersion).toInt(&ok);
	return ok ? version : kSchemaVersion;
}

QVector<RecentProject> StudioSettings::recentProjects() const
{
	QVector<RecentProject> projects;
	QStringList seenPaths;

	const int size = m_settings.beginReadArray(kRecentProjectsArray);
	for (int index = 0; index < size; ++index) {
		m_settings.setArrayIndex(index);
		const QString path = normalizedProjectPath(m_settings.value(kRecentProjectPathKey).toString());
		if (path.isEmpty()) {
			continue;
		}

		bool alreadySeen = false;
		for (const QString& seenPath : seenPaths) {
			if (sameProjectPath(seenPath, path)) {
				alreadySeen = true;
				break;
			}
		}
		if (alreadySeen) {
			continue;
		}

		RecentProject project;
		project.path = path;
		project.displayName = recentProjectDisplayName(path, m_settings.value(kRecentProjectDisplayNameKey).toString());
		project.lastOpenedUtc = normalizedTimestamp(m_settings.value(kRecentProjectLastOpenedKey).toDateTime());
		project.exists = QFileInfo::exists(path);
		projects.push_back(project);
		seenPaths.push_back(path);

		if (projects.size() >= kMaximumRecentProjects) {
			break;
		}
	}
	m_settings.endArray();

	std::sort(projects.begin(), projects.end(), [](const RecentProject& left, const RecentProject& right) {
		return left.lastOpenedUtc > right.lastOpenedUtc;
	});
	return projects;
}

void StudioSettings::recordRecentProject(const QString& path, const QString& displayName, const QDateTime& openedUtc)
{
	const QString normalizedPath = normalizedProjectPath(path);
	if (normalizedPath.isEmpty()) {
		return;
	}

	QVector<RecentProject> projects = recentProjects();
	projects.erase(
		std::remove_if(projects.begin(), projects.end(), [&normalizedPath](const RecentProject& project) {
			return sameProjectPath(project.path, normalizedPath);
		}),
		projects.end());

	RecentProject project;
	project.path = normalizedPath;
	project.displayName = recentProjectDisplayName(normalizedPath, displayName);
	project.lastOpenedUtc = normalizedTimestamp(openedUtc);
	project.exists = QFileInfo::exists(normalizedPath);
	projects.prepend(project);

	if (projects.size() > kMaximumRecentProjects) {
		projects.resize(kMaximumRecentProjects);
	}

	writeRecentProjects(projects);
}

void StudioSettings::removeRecentProject(const QString& path)
{
	const QString normalizedPath = normalizedProjectPath(path);
	if (normalizedPath.isEmpty()) {
		return;
	}

	QVector<RecentProject> projects = recentProjects();
	projects.erase(
		std::remove_if(projects.begin(), projects.end(), [&normalizedPath](const RecentProject& project) {
			return sameProjectPath(project.path, normalizedPath);
		}),
		projects.end());
	writeRecentProjects(projects);
}

void StudioSettings::clearRecentProjects()
{
	m_settings.remove(kRecentProjectsArray);
}

AccessibilityPreferences StudioSettings::accessibilityPreferences() const
{
	AccessibilityPreferences preferences;
	preferences.localeName = normalizedLocaleName(m_settings.value(kLocaleNameKey, preferences.localeName).toString());
	preferences.textScalePercent = normalizedTextScalePercent(m_settings.value(kTextScalePercentKey, preferences.textScalePercent).toInt());
	preferences.theme = themeFromId(m_settings.value(kThemeKey, themeId(preferences.theme)).toString());
	preferences.density = densityFromId(m_settings.value(kDensityKey, densityId(preferences.density)).toString());
	preferences.reducedMotion = m_settings.value(kReducedMotionKey, preferences.reducedMotion).toBool();
	preferences.textToSpeechEnabled = m_settings.value(kTextToSpeechEnabledKey, preferences.textToSpeechEnabled).toBool();
	return preferences;
}

void StudioSettings::setAccessibilityPreferences(const AccessibilityPreferences& preferences)
{
	m_settings.setValue(kLocaleNameKey, normalizedLocaleName(preferences.localeName));
	m_settings.setValue(kTextScalePercentKey, normalizedTextScalePercent(preferences.textScalePercent));
	m_settings.setValue(kThemeKey, themeId(preferences.theme));
	m_settings.setValue(kDensityKey, densityId(preferences.density));
	m_settings.setValue(kReducedMotionKey, preferences.reducedMotion);
	m_settings.setValue(kTextToSpeechEnabledKey, preferences.textToSpeechEnabled);
}

void StudioSettings::setLocaleName(const QString& localeName)
{
	m_settings.setValue(kLocaleNameKey, normalizedLocaleName(localeName));
}

void StudioSettings::setTextScalePercent(int textScalePercent)
{
	m_settings.setValue(kTextScalePercentKey, normalizedTextScalePercent(textScalePercent));
}

void StudioSettings::setTheme(StudioTheme theme)
{
	m_settings.setValue(kThemeKey, themeId(theme));
}

void StudioSettings::setDensity(UiDensity density)
{
	m_settings.setValue(kDensityKey, densityId(density));
}

void StudioSettings::setReducedMotion(bool reducedMotion)
{
	m_settings.setValue(kReducedMotionKey, reducedMotion);
}

void StudioSettings::setTextToSpeechEnabled(bool enabled)
{
	m_settings.setValue(kTextToSpeechEnabledKey, enabled);
}

SetupProgress StudioSettings::setupProgress() const
{
	SetupProgress progress;
	progress.currentStep = setupStepFromId(m_settings.value(kSetupCurrentStepKey, setupStepId(progress.currentStep)).toString());
	progress.started = m_settings.value(kSetupStartedKey, false).toBool();
	progress.skipped = m_settings.value(kSetupSkippedKey, false).toBool();
	progress.completed = m_settings.value(kSetupCompletedKey, false).toBool();
	progress.lastUpdatedUtc = m_settings.value(kSetupLastUpdatedUtcKey).toDateTime().toUTC();
	return progress;
}

SetupSummary StudioSettings::setupSummary() const
{
	const SetupProgress progress = setupProgress();
	const AccessibilityPreferences preferences = accessibilityPreferences();
	const QVector<RecentProject> projects = recentProjects();
	const QVector<SetupStep> steps = setupSteps();
	const int currentIndex = setupStepIndex(progress.currentStep);

	SetupSummary summary;
	summary.currentStepId = setupStepId(progress.currentStep);
	summary.currentStepName = setupStepDisplayName(progress.currentStep);
	summary.currentStepDescription = setupStepDescription(progress.currentStep);

	if (progress.completed) {
		summary.status = QStringLiteral("complete");
		summary.nextAction = QStringLiteral("Open the workspace dashboard and continue normal project work.");
	} else if (progress.skipped) {
		summary.status = QStringLiteral("skipped");
		summary.nextAction = QStringLiteral("Resume setup when you want to finish tailoring VibeStudio.");
	} else if (progress.started) {
		summary.status = QStringLiteral("in-progress");
		summary.nextAction = QStringLiteral("Continue the current setup step or skip setup for now.");
	} else {
		summary.status = QStringLiteral("not-started");
		summary.nextAction = QStringLiteral("Start setup to review access, workspace, project, toolchain, AI, and CLI choices.");
	}

	for (int index = 0; index < steps.size(); ++index) {
		const QString item = QStringLiteral("%1: %2").arg(setupStepDisplayName(steps[index]), setupStepDescription(steps[index]));
		if (progress.completed || (progress.started && !progress.skipped && index < currentIndex)) {
			summary.completedItems.push_back(item);
		} else {
			summary.pendingItems.push_back(item);
		}
	}

	if (preferences.localeName == QStringLiteral("en")) {
		summary.warnings.push_back(QStringLiteral("Language is still the default English fallback."));
	}
	if (preferences.theme == StudioTheme::Dark && preferences.textScalePercent == 100 && preferences.density == UiDensity::Standard && !preferences.reducedMotion && !preferences.textToSpeechEnabled) {
		summary.warnings.push_back(QStringLiteral("Accessibility preferences are still at defaults."));
	}
	if (projects.isEmpty()) {
		summary.warnings.push_back(QStringLiteral("No recent project folder has been selected yet."));
	}

	return summary;
}

void StudioSettings::startOrResumeSetup(SetupStep step)
{
	m_settings.setValue(kSetupStartedKey, true);
	m_settings.setValue(kSetupSkippedKey, false);
	m_settings.setValue(kSetupCompletedKey, false);
	m_settings.setValue(kSetupCurrentStepKey, setupStepId(step));
	m_settings.setValue(kSetupLastUpdatedUtcKey, setupTimestamp());
}

void StudioSettings::advanceSetup()
{
	SetupProgress progress = setupProgress();
	if (!progress.started || progress.skipped || progress.completed) {
		startOrResumeSetup(progress.currentStep);
		return;
	}

	const QVector<SetupStep> steps = setupSteps();
	const int index = setupStepIndex(progress.currentStep);
	if (index + 1 >= steps.size()) {
		completeSetup();
		return;
	}

	startOrResumeSetup(steps[index + 1]);
}

void StudioSettings::skipSetup()
{
	const SetupProgress progress = setupProgress();
	m_settings.setValue(kSetupStartedKey, progress.started);
	m_settings.setValue(kSetupSkippedKey, true);
	m_settings.setValue(kSetupCompletedKey, false);
	m_settings.setValue(kSetupCurrentStepKey, setupStepId(progress.currentStep));
	m_settings.setValue(kSetupLastUpdatedUtcKey, setupTimestamp());
}

void StudioSettings::completeSetup()
{
	m_settings.setValue(kSetupStartedKey, true);
	m_settings.setValue(kSetupSkippedKey, false);
	m_settings.setValue(kSetupCompletedKey, true);
	m_settings.setValue(kSetupCurrentStepKey, setupStepId(SetupStep::ReviewFinish));
	m_settings.setValue(kSetupLastUpdatedUtcKey, setupTimestamp());
}

void StudioSettings::resetSetup()
{
	m_settings.remove(kSetupStartedKey);
	m_settings.remove(kSetupSkippedKey);
	m_settings.remove(kSetupCompletedKey);
	m_settings.remove(kSetupCurrentStepKey);
	m_settings.remove(kSetupLastUpdatedUtcKey);
}

int StudioSettings::selectedMode() const
{
	bool ok = false;
	const int modeIndex = m_settings.value(kSelectedModeKey, 0).toInt(&ok);
	return ok ? std::max(0, modeIndex) : 0;
}

void StudioSettings::setSelectedMode(int modeIndex)
{
	m_settings.setValue(kSelectedModeKey, std::max(0, modeIndex));
}

QByteArray StudioSettings::shellGeometry() const
{
	return m_settings.value(kShellGeometryKey).toByteArray();
}

void StudioSettings::setShellGeometry(const QByteArray& geometry)
{
	m_settings.setValue(kShellGeometryKey, geometry);
}

QByteArray StudioSettings::shellWindowState() const
{
	return m_settings.value(kShellWindowStateKey).toByteArray();
}

void StudioSettings::setShellWindowState(const QByteArray& windowState)
{
	m_settings.setValue(kShellWindowStateKey, windowState);
}

void StudioSettings::ensureSchema()
{
	if (!m_settings.contains(kSchemaVersionKey)) {
		m_settings.setValue(kSchemaVersionKey, kSchemaVersion);
	}
}

void StudioSettings::writeRecentProjects(const QVector<RecentProject>& projects)
{
	const int count = std::min(static_cast<int>(projects.size()), kMaximumRecentProjects);
	m_settings.beginWriteArray(kRecentProjectsArray, count);
	for (int index = 0; index < count; ++index) {
		const RecentProject& project = projects[index];
		m_settings.setArrayIndex(index);
		m_settings.setValue(kRecentProjectPathKey, project.path);
		m_settings.setValue(kRecentProjectDisplayNameKey, recentProjectDisplayName(project.path, project.displayName));
		m_settings.setValue(kRecentProjectLastOpenedKey, normalizedTimestamp(project.lastOpenedUtc));
	}
	m_settings.endArray();
}

QString normalizedProjectPath(const QString& path)
{
	const QString trimmed = path.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}

	const QFileInfo info(trimmed);
	const QString absolutePath = info.isAbsolute()
		? info.absoluteFilePath()
		: QDir::current().absoluteFilePath(trimmed);
	return QDir::cleanPath(absolutePath);
}

QString recentProjectDisplayName(const QString& path, const QString& preferredName)
{
	const QString trimmedName = preferredName.trimmed();
	if (!trimmedName.isEmpty()) {
		return trimmedName;
	}

	const QFileInfo info(path);
	const QString fileName = info.fileName();
	if (!fileName.isEmpty()) {
		return fileName;
	}
	return QDir::toNativeSeparators(path);
}

QStringList supportedLocaleNames()
{
	return {
		QStringLiteral("en"),
		QStringLiteral("zh-Hans"),
		QStringLiteral("hi"),
		QStringLiteral("es"),
		QStringLiteral("fr"),
		QStringLiteral("ar"),
		QStringLiteral("bn"),
		QStringLiteral("pt-BR"),
		QStringLiteral("ru"),
		QStringLiteral("ur"),
		QStringLiteral("id"),
		QStringLiteral("de"),
		QStringLiteral("ja"),
		QStringLiteral("pcm"),
		QStringLiteral("mr"),
		QStringLiteral("te"),
		QStringLiteral("tr"),
		QStringLiteral("ta"),
		QStringLiteral("vi"),
		QStringLiteral("ko"),
	};
}

QString normalizedLocaleName(const QString& localeName)
{
	const QString requested = localeName.trimmed().replace('_', '-');
	if (requested.isEmpty()) {
		return QStringLiteral("en");
	}

	for (const QString& supportedLocale : supportedLocaleNames()) {
		if (QString::compare(supportedLocale, requested, Qt::CaseInsensitive) == 0) {
			return supportedLocale;
		}
	}

	const QString languageOnly = requested.section('-', 0, 0);
	for (const QString& supportedLocale : supportedLocaleNames()) {
		if (QString::compare(supportedLocale.section('-', 0, 0), languageOnly, Qt::CaseInsensitive) == 0) {
			return supportedLocale;
		}
	}

	return QStringLiteral("en");
}

QString themeId(StudioTheme theme)
{
	switch (theme) {
	case StudioTheme::System:
		return QStringLiteral("system");
	case StudioTheme::Dark:
		return QStringLiteral("dark");
	case StudioTheme::Light:
		return QStringLiteral("light");
	case StudioTheme::HighContrastDark:
		return QStringLiteral("high-contrast-dark");
	case StudioTheme::HighContrastLight:
		return QStringLiteral("high-contrast-light");
	}
	return QStringLiteral("dark");
}

QString themeDisplayName(StudioTheme theme)
{
	switch (theme) {
	case StudioTheme::System:
		return QStringLiteral("System");
	case StudioTheme::Dark:
		return QStringLiteral("Dark");
	case StudioTheme::Light:
		return QStringLiteral("Light");
	case StudioTheme::HighContrastDark:
		return QStringLiteral("High Contrast Dark");
	case StudioTheme::HighContrastLight:
		return QStringLiteral("High Contrast Light");
	}
	return QStringLiteral("Dark");
}

StudioTheme themeFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("system")) {
		return StudioTheme::System;
	}
	if (normalized == QStringLiteral("light")) {
		return StudioTheme::Light;
	}
	if (normalized == QStringLiteral("high-contrast-dark")) {
		return StudioTheme::HighContrastDark;
	}
	if (normalized == QStringLiteral("high-contrast-light")) {
		return StudioTheme::HighContrastLight;
	}
	return StudioTheme::Dark;
}

QStringList themeIds()
{
	return {
		themeId(StudioTheme::System),
		themeId(StudioTheme::Dark),
		themeId(StudioTheme::Light),
		themeId(StudioTheme::HighContrastDark),
		themeId(StudioTheme::HighContrastLight),
	};
}

QString densityId(UiDensity density)
{
	switch (density) {
	case UiDensity::Comfortable:
		return QStringLiteral("comfortable");
	case UiDensity::Standard:
		return QStringLiteral("standard");
	case UiDensity::Compact:
		return QStringLiteral("compact");
	}
	return QStringLiteral("standard");
}

QString densityDisplayName(UiDensity density)
{
	switch (density) {
	case UiDensity::Comfortable:
		return QStringLiteral("Comfortable");
	case UiDensity::Standard:
		return QStringLiteral("Standard");
	case UiDensity::Compact:
		return QStringLiteral("Compact");
	}
	return QStringLiteral("Standard");
}

UiDensity densityFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("comfortable")) {
		return UiDensity::Comfortable;
	}
	if (normalized == QStringLiteral("compact")) {
		return UiDensity::Compact;
	}
	return UiDensity::Standard;
}

QStringList densityIds()
{
	return {
		densityId(UiDensity::Comfortable),
		densityId(UiDensity::Standard),
		densityId(UiDensity::Compact),
	};
}

int normalizedTextScalePercent(int textScalePercent)
{
	return std::clamp(textScalePercent, StudioSettings::kMinimumTextScalePercent, StudioSettings::kMaximumTextScalePercent);
}

QString setupStepId(SetupStep step)
{
	switch (step) {
	case SetupStep::WelcomeAccess:
		return QStringLiteral("welcome-access");
	case SetupStep::WorkspaceProfile:
		return QStringLiteral("workspace-profile");
	case SetupStep::ProjectsPackages:
		return QStringLiteral("projects-packages");
	case SetupStep::Toolchains:
		return QStringLiteral("toolchains");
	case SetupStep::AiAutomation:
		return QStringLiteral("ai-automation");
	case SetupStep::CliIntegration:
		return QStringLiteral("cli-integration");
	case SetupStep::ReviewFinish:
		return QStringLiteral("review-finish");
	}
	return QStringLiteral("welcome-access");
}

QString setupStepDisplayName(SetupStep step)
{
	switch (step) {
	case SetupStep::WelcomeAccess:
		return QStringLiteral("Welcome And Access");
	case SetupStep::WorkspaceProfile:
		return QStringLiteral("Workspace Profile");
	case SetupStep::ProjectsPackages:
		return QStringLiteral("Projects And Packages");
	case SetupStep::Toolchains:
		return QStringLiteral("Toolchains");
	case SetupStep::AiAutomation:
		return QStringLiteral("AI And Automation");
	case SetupStep::CliIntegration:
		return QStringLiteral("CLI Integration");
	case SetupStep::ReviewFinish:
		return QStringLiteral("Review And Finish");
	}
	return QStringLiteral("Welcome And Access");
}

QString setupStepDescription(SetupStep step)
{
	switch (step) {
	case SetupStep::WelcomeAccess:
		return QStringLiteral("Review language, visibility, scale, density, motion, and TTS preferences.");
	case SetupStep::WorkspaceProfile:
		return QStringLiteral("Choose role and editor-profile direction before deeper editor surfaces arrive.");
	case SetupStep::ProjectsPackages:
		return QStringLiteral("Open a project folder or defer project/package setup.");
	case SetupStep::Toolchains:
		return QStringLiteral("Prepare for compiler detection and command manifests.");
	case SetupStep::AiAutomation:
		return QStringLiteral("Keep AI disabled by default or plan connector setup later.");
	case SetupStep::CliIntegration:
		return QStringLiteral("Review CLI availability and scriptable settings reports.");
	case SetupStep::ReviewFinish:
		return QStringLiteral("Inspect skipped, complete, pending, and warning states before entering the workspace.");
	}
	return QStringLiteral("Review language, visibility, scale, density, motion, and TTS preferences.");
}

SetupStep setupStepFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	for (SetupStep step : setupSteps()) {
		if (setupStepId(step) == normalized) {
			return step;
		}
	}
	return SetupStep::WelcomeAccess;
}

QVector<SetupStep> setupSteps()
{
	return {
		SetupStep::WelcomeAccess,
		SetupStep::WorkspaceProfile,
		SetupStep::ProjectsPackages,
		SetupStep::Toolchains,
		SetupStep::AiAutomation,
		SetupStep::CliIntegration,
		SetupStep::ReviewFinish,
	};
}

} // namespace vibestudio
