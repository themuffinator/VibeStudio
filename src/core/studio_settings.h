#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

enum class StudioTheme {
	System,
	Dark,
	Light,
	HighContrastDark,
	HighContrastLight,
};

enum class UiDensity {
	Comfortable,
	Standard,
	Compact,
};

enum class SetupStep {
	WelcomeAccess,
	WorkspaceProfile,
	ProjectsPackages,
	Toolchains,
	AiAutomation,
	CliIntegration,
	ReviewFinish,
};

struct AccessibilityPreferences {
	QString localeName = QStringLiteral("en");
	int textScalePercent = 100;
	StudioTheme theme = StudioTheme::Dark;
	UiDensity density = UiDensity::Standard;
	bool reducedMotion = false;
	bool textToSpeechEnabled = false;
};

struct SetupProgress {
	SetupStep currentStep = SetupStep::WelcomeAccess;
	bool started = false;
	bool skipped = false;
	bool completed = false;
	QDateTime lastUpdatedUtc;
};

struct SetupSummary {
	QString status;
	QString currentStepId;
	QString currentStepName;
	QString currentStepDescription;
	QString nextAction;
	QStringList completedItems;
	QStringList pendingItems;
	QStringList warnings;
};

struct RecentProject {
	QString path;
	QString displayName;
	QDateTime lastOpenedUtc;
	bool exists = false;
};

class StudioSettings final {
public:
	static constexpr int kSchemaVersion = 1;
	static constexpr int kMaximumRecentProjects = 12;
	static constexpr int kMinimumTextScalePercent = 100;
	static constexpr int kMaximumTextScalePercent = 200;

	StudioSettings();
	explicit StudioSettings(const QString& filePath);
	~StudioSettings();

	StudioSettings(const StudioSettings&) = delete;
	StudioSettings& operator=(const StudioSettings&) = delete;

	QString storageLocation() const;
	QSettings::Status status() const;
	void sync();

	int schemaVersion() const;

	QVector<RecentProject> recentProjects() const;
	void recordRecentProject(const QString& path, const QString& displayName = QString(), const QDateTime& openedUtc = QDateTime::currentDateTimeUtc());
	void removeRecentProject(const QString& path);
	void clearRecentProjects();

	AccessibilityPreferences accessibilityPreferences() const;
	void setAccessibilityPreferences(const AccessibilityPreferences& preferences);
	void setLocaleName(const QString& localeName);
	void setTextScalePercent(int textScalePercent);
	void setTheme(StudioTheme theme);
	void setDensity(UiDensity density);
	void setReducedMotion(bool reducedMotion);
	void setTextToSpeechEnabled(bool enabled);

	SetupProgress setupProgress() const;
	SetupSummary setupSummary() const;
	void startOrResumeSetup(SetupStep step);
	void advanceSetup();
	void skipSetup();
	void completeSetup();
	void resetSetup();

	int selectedMode() const;
	void setSelectedMode(int modeIndex);

	QByteArray shellGeometry() const;
	void setShellGeometry(const QByteArray& geometry);
	QByteArray shellWindowState() const;
	void setShellWindowState(const QByteArray& windowState);

private:
	void ensureSchema();
	void writeRecentProjects(const QVector<RecentProject>& projects);

	mutable QSettings m_settings;
};

QString normalizedProjectPath(const QString& path);
QString recentProjectDisplayName(const QString& path, const QString& preferredName = QString());
QStringList supportedLocaleNames();
QString normalizedLocaleName(const QString& localeName);
QString themeId(StudioTheme theme);
QString themeDisplayName(StudioTheme theme);
StudioTheme themeFromId(const QString& id);
QStringList themeIds();
QString densityId(UiDensity density);
QString densityDisplayName(UiDensity density);
UiDensity densityFromId(const QString& id);
QStringList densityIds();
int normalizedTextScalePercent(int textScalePercent);
QString setupStepId(SetupStep step);
QString setupStepDisplayName(SetupStep step);
QString setupStepDescription(SetupStep step);
SetupStep setupStepFromId(const QString& id);
QVector<SetupStep> setupSteps();

} // namespace vibestudio
