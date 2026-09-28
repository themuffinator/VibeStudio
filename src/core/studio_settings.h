#pragma once

#include "core/ai_connectors.h"
#include "core/compiler_registry.h"
#include "core/game_installation.h"
#include "core/operation_state.h"

#include <QByteArray>
#include <QDateTime>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <memory>

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
	GameInstallations,
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

// A persisted activity task. The log is stored as a bounded tail: if it did not
// fit, `logTruncated` is set and `droppedLogEntryCount` says how many older
// entries were dropped, so "task history" never silently pretends to be whole.
struct RecentActivityTask {
	QString id;
	QString title;
	QString detail;
	QString source;
	OperationState state = OperationState::Idle;
	OperationProgress progress;
	QString resultSummary;
	QStringList warnings;
	QVector<OperationLogEntry> log;
	QVector<OperationStateTransition> transitions;
	bool cancellable = false;
	bool logTruncated = false;
	int droppedLogEntryCount = 0;
	QDateTime createdUtc;
	QDateTime updatedUtc;
	QDateTime finishedUtc;
	qint64 durationMs = 0;
};

class StudioSettings final {
public:
	// Schema history:
	//   1  initial layout
	//   2  normalised theme/density/locale ids, dropped unused v1 keys,
	//      richer recent-activity records
	static constexpr int kSchemaVersion = 2;
	static constexpr int kMaximumRecentProjects = 12;
	static constexpr int kMaximumRecentActivityTasks = 24;
	static constexpr int kMaximumGameInstallationProfiles = 32;
	static constexpr int kMaximumCompilerToolPathOverrides = 32;
	static constexpr int kMinimumTextScalePercent = 100;
	static constexpr int kMaximumTextScalePercent = 200;
	// Bounds for the persisted tail of a task log.
	static constexpr int kMaximumActivityLogEntries = 400;
	static constexpr int kMaximumActivityLogBytes = 64 * 1024;
	static constexpr int kMaximumActivityTransitions = 64;

	StudioSettings();
	explicit StudioSettings(const QString& filePath);
	~StudioSettings();

	StudioSettings(const StudioSettings&) = delete;
	StudioSettings& operator=(const StudioSettings&) = delete;

	// Store selection precedence, highest first:
	//   1. the explicit StudioSettings(filePath) INI constructor
	//   2. the process-wide override file path (also an INI file), set by the
	//      CLI's --settings-file flag and by tests
	//   3. the platform default store (registry/plist/ini) for the running
	//      organization and application name
	// The override is a plain process-global: set it once during start-up,
	// before any StudioSettings instance is constructed.
	static void setOverrideFilePath(const QString& filePath);
	static QString overrideFilePath();

	QString storageLocation() const;
	QSettings::Status status() const;
	// Number of writes that were refused because the store is read-only. A caller
	// that reports success on a refused write would be lying to the user, so
	// mutating commands check this.
	int discardedWriteCount() const;
	void sync();

	// The schema version stored in the file. After a successful migration this
	// equals kSchemaVersion; for a store written by a newer build it is that
	// newer number and the instance is read-only.
	int schemaVersion() const;
	// True when the store was written by a newer build than this binary. The
	// store is then never rewritten, because its unknown keys cannot be
	// migrated backwards safely.
	bool storedSchemaIsNewer() const;
	bool isReadOnly() const;
	// Human-readable notes about what ensureSchema() did (or refused to do).
	QStringList migrationNotes() const;

	QVector<RecentProject> recentProjects() const;
	QString currentProjectPath() const;
	void setCurrentProjectPath(const QString& path);
	void recordRecentProject(const QString& path, const QString& displayName = QString(), const QDateTime& openedUtc = QDateTime::currentDateTimeUtc());
	void removeRecentProject(const QString& path);
	void clearRecentProjects();
	QVector<RecentActivityTask> recentActivityTasks() const;
	void recordRecentActivityTask(const RecentActivityTask& task);
	void clearRecentActivityTasks();

	QVector<GameInstallationProfile> gameInstallations() const;
	void upsertGameInstallation(GameInstallationProfile profile);
	void removeGameInstallation(const QString& id);
	void clearGameInstallations();
	QString selectedGameInstallationId() const;
	void setSelectedGameInstallation(const QString& id);

	AccessibilityPreferences accessibilityPreferences() const;
	void setAccessibilityPreferences(const AccessibilityPreferences& preferences);
	void setLocaleName(const QString& localeName);
	void setTextScalePercent(int textScalePercent);
	void setTheme(StudioTheme theme);
	void setDensity(UiDensity density);
	void setReducedMotion(bool reducedMotion);
	void setTextToSpeechEnabled(bool enabled);
	QString selectedEditorProfileId() const;
	void setSelectedEditorProfileId(const QString& id);
	QVector<CompilerToolPathOverride> compilerToolPathOverrides() const;
	void upsertCompilerToolPathOverride(const CompilerToolPathOverride& override);
	void removeCompilerToolPathOverride(const QString& toolId);
	void clearCompilerToolPathOverrides();
	AiAutomationPreferences aiAutomationPreferences() const;
	void setAiAutomationPreferences(const AiAutomationPreferences& preferences);

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
	// Sizes of the studio's main splitter. QMainWindow::saveState() only covers
	// toolbars and dock widgets, so the splitter has to be persisted separately.
	QByteArray shellSplitterState() const;
	void setShellSplitterState(const QByteArray& splitterState);
	// Whether the mode rail shows icons only.
	bool shellModeRailCompact() const;
	void setShellModeRailCompact(bool compact);
	// Saved sizes for one named work-surface splitter, so each page keeps the
	// panel widths the user chose. Keys are stable object names such as
	// "levelsWorkbench"; unknown keys read back empty.
	QByteArray shellLayoutState(const QString& key) const;
	void setShellLayoutState(const QString& key, const QByteArray& state);

private:
	void ensureSchema();
	void runMigrations(int storedVersion);
	void writeValue(const QString& key, const QVariant& value);
	void removeKey(const QString& key);
	void writeRecentProjects(const QVector<RecentProject>& projects);
	void writeRecentActivityTasks(const QVector<RecentActivityTask>& tasks);
	void writeGameInstallations(const QVector<GameInstallationProfile>& profiles);
	void writeCompilerToolPathOverrides(const QVector<CompilerToolPathOverride>& overrides);

	std::unique_ptr<QSettings> m_settings;
	QStringList m_migrationNotes;
	bool m_storedSchemaIsNewer = false;
	bool m_readOnly = false;
	int m_discardedWrites = 0;
};

// Bridges the in-memory task model to the persisted history record, keeping the
// log/transition tails inside the documented bounds.
RecentActivityTask recentActivityTaskFromOperationTask(const OperationTask& task);

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
