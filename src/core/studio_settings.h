#pragma once

#include "core/ai_connectors.h"
#include "core/compiler_registry.h"
#include "core/game_installation.h"
#include "core/operation_state.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <memory>

namespace vibestudio {

struct PackageDraftSaveLimits;
struct PackageCopyLimits;
struct LevelViewLinks;
struct LevelEditorControls;

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

// What was open when the studio last closed: the package, the map (and which
// map, for a WAD), and the code editor's tabs with the one in front.
struct StudioSession {
	QString packagePath;
	QString mapPath;
	QString mapName;
	QStringList codeFiles;
	QString currentCodeFile;
	// The process of the studio that recorded it, so a second studio started
	// while the first runs can tell the session is not its own.
	qint64 ownerProcessId = 0;

	[[nodiscard]] bool isEmpty() const;
	friend bool operator==(const StudioSession&, const StudioSession&) = default;
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
	static constexpr int kMaximumRecentFiles = 10;
	static constexpr int kMaximumRecentCommands = 6;
	static constexpr int kMaximumGameInstallationProfiles = 32;
	static constexpr int kMaximumCompilerToolPathOverrides = 32;
	static constexpr int kMinimumTextScalePercent = 100;
	static constexpr int kMaximumTextScalePercent = 200;
	// Bounds for the persisted tail of a task log.
	static constexpr int kMaximumActivityLogEntries = 400;
	static constexpr int kMaximumActivityLogBytes = 64 * 1024;
	static constexpr int kMaximumActivityTransitions = 64;

	// Inspection reads existing values without creating or migrating a store.
	enum class AccessMode { ReadWrite, ReadOnly };
	explicit StudioSettings(AccessMode access = AccessMode::ReadWrite);
	explicit StudioSettings(const QString& filePath, AccessMode access = AccessMode::ReadWrite);
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
	// Recently opened documents of one kind ("package", "map", "code"), newest
	// first and at most kMaximumRecentFiles per kind. Paths are stored as given
	// after cleaning; ones that no longer exist are left for the reader to
	// skip, so a removable drive coming back restores them.
	QStringList recentFiles(const QString& kind) const;
	void recordRecentFile(const QString& kind, const QString& path);
	void removeRecentFile(const QString& kind, const QString& path);
	void clearRecentFiles();
	QVector<RecentActivityTask> recentActivityTasks() const;
	void recordRecentActivityTask(const RecentActivityTask& task);
	void clearRecentActivityTasks();

	QVector<GameInstallationProfile> gameInstallations() const;
	void upsertGameInstallation(GameInstallationProfile profile);
	void removeGameInstallation(const QString& id);
	void clearGameInstallations();
	QString selectedGameInstallationId() const;
	void setSelectedGameInstallation(const QString& id);
	// The game folder the Build page last launched an installation into: a
	// mod's folder, or empty for the base game.
	QString launchGameDirectory(const QString& installationId) const;
	void setLaunchGameDirectory(const QString& installationId, const QString& folder);
	// Commands last run from the command palette, newest first, at most
	// kMaximumRecentCommands.
	QStringList recentCommands() const;
	void recordRecentCommand(const QString& commandId);
	// The queries typed into one filter (by filter id), most recent first,
	// each once whatever its case, at most twelve.
	QStringList recentFilterQueries(const QString& filterId) const;
	void recordFilterQuery(const QString& filterId, const QString& query);

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
	QJsonObject editorGestureOverrides(const QString& profile, QString* error = nullptr) const;
	LevelEditorControls effectiveLevelEditorControls(const QString& profile, QString* error = nullptr) const;
	bool setEditorGestureOverrides(const QString& profile, const QJsonObject& overrides, QString* error = nullptr);
	QVector<CompilerToolPathOverride> compilerToolPathOverrides() const;
	void upsertCompilerToolPathOverride(const CompilerToolPathOverride& override);
	void removeCompilerToolPathOverride(const QString& toolId);
	void clearCompilerToolPathOverrides();
	AiAutomationPreferences aiAutomationPreferences() const;
	void setAiAutomationPreferences(const AiAutomationPreferences& preferences);
	// Sending project context off this machine is agreed once per project and
	// destination (such as "claude@api.anthropic.com"), after the user has
	// seen what goes. An empty project path stands for work outside a project.
	bool aiContextConsentGiven(const QString& projectPath, const QString& destination) const;
	void setAiContextConsentGiven(const QString& projectPath, const QString& destination, bool given);
	void clearAiContextConsent();

	SetupProgress setupProgress() const;
	SetupSummary setupSummary() const;
	void startOrResumeSetup(SetupStep step);
	void advanceSetup();
	void skipSetup();
	void completeSetup();
	void resetSetup();

	int selectedMode() const;
	void setSelectedMode(int modeIndex);

	// Whether the studio reopens the previous session at start; on unless the
	// user turns it off.
	bool restoreSession() const;
	void setRestoreSession(bool enabled);
	StudioSession lastSession() const;
	void setLastSession(const StudioSession& session);
	// Whether a crash leaves a report on this machine for the next start to
	// offer; on unless the user turns it off. Reports are never sent anywhere.
	bool crashReports() const;
	void setCrashReports(bool enabled);
	// Local, asynchronous checkpoints of modified maps; separate from crash logs.
	bool levelRecoveryEnabled() const;
	// "profile" follows the interaction profile; other values are
	// levelViewLayoutId() identifiers. Layout and interaction are independent.
	QString levelViewLayoutPreference() const;
	bool setLevelViewLayoutPreference(const QString& id);
	LevelViewLinks levelViewLinks() const;
	bool setLevelViewLinks(const LevelViewLinks& links);
	bool levelTextureLock() const;
	void setLevelTextureLock(bool enabled);
	bool levelTextureScaleLock() const;
	void setLevelTextureScaleLock(bool enabled);
	bool levelAllowValve220() const;
	void setLevelAllowValve220(bool enabled);
	void setLevelRecoveryEnabled(bool enabled);
	bool codeRecoveryEnabled() const;
	void setCodeRecoveryEnabled(bool enabled);
	// Inert local-tool preferences; opening a project never launches a server.
	QJsonObject languageServerPreferences() const;
	void setLanguageServerPreferences(const QJsonObject& preferences);
	bool audioRecoveryEnabled() const;
	void setAudioRecoveryEnabled(bool enabled);
	bool audioRecoveryNotifyAtStartup() const;
	void setAudioRecoveryNotifyAtStartup(bool enabled);
	bool modelRecoveryEnabled() const;
	void setModelRecoveryEnabled(bool enabled);
	QStringList packagePublicationDirectories() const;
	void rememberPackagePublicationDirectory(const QString& path);
	bool packageRecoveryEnabled() const;
	void setPackageRecoveryEnabled(bool enabled);
	int packageRecoveryIntervalSeconds() const;
	void setPackageRecoveryIntervalSeconds(int seconds);
	int packageRecoveryMaximumMiB() const;
	void setPackageRecoveryMaximumMiB(int mib);
	int packageRecoveryMaximumCopies() const;
	void setPackageRecoveryMaximumCopies(int copies);
	int packageImportMaximumMiB() const;
	void setPackageImportMaximumMiB(int mib);
	int packageImportMaximumFiles() const;
	void setPackageImportMaximumFiles(int files);
	int packageDraftMaximumMiB() const;
	void setPackageDraftMaximumMiB(int mib);
	int packageDraftMaximumFiles() const;
	void setPackageDraftMaximumFiles(int files);
	PackageDraftSaveLimits packageDraftSaveLimits() const;
	PackageCopyLimits packageCopyLimits() const;
	void setPackageCopyLimits(const PackageCopyLimits& limits);
	bool textureRecoveryEnabled() const;
	void setTextureRecoveryEnabled(bool enabled);
	int textureRecoveryIntervalSeconds() const;
	void setTextureRecoveryIntervalSeconds(int seconds);
	// The code editor's zoom, in percent of the studio's monospace text size:
	// 100 until the user zooms, and always within 50 to 300.
	int codeZoomPercent() const;
	void setCodeZoomPercent(int percent);
	// Whether the code editor pins the opening lines of the blocks the top of
	// its view is inside; on unless the user turns it off.
	bool codeStickyHeaders() const;
	void setCodeStickyHeaders(bool enabled);
	// Keys the user gave commands in place of their defaults, by command id,
	// in portable key-sequence text; an empty list is a command the user left
	// without keys. Setting the map replaces every earlier one.
	QHash<QString, QStringList> userShortcuts() const;
	void setUserShortcuts(const QHash<QString, QStringList>& shortcuts);

	QByteArray shellGeometry() const;
	void setShellGeometry(const QByteArray& geometry);
	QByteArray shellWindowState() const;
	void setShellWindowState(const QByteArray& windowState);
	// Sizes of the studio's main splitter. QMainWindow::saveState() only covers
	// toolbars and dock widgets, so the splitter has to be persisted separately.
	QByteArray shellSplitterState() const;
	void setShellSplitterState(const QByteArray& splitterState);
	// How the mode rail spends its width: "automatic" (icons, opening over
	// the page on hover or focus; the default), "expanded" (labels pinned
	// open), or "compact" (icons only).
	QString shellModeRailBehaviour() const;
	void setShellModeRailBehaviour(const QString& behaviour);
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
