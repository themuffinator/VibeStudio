#include "core/studio_settings.h"

#include "core/editor_profiles.h"
#include "core/level_navigation.h"
#include "core/localization.h"
#include "core/package_draft_storage.h"
#include "core/package_copy_budget.h"
#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>
#include <memory>
#include <utility>

namespace vibestudio {

namespace {

constexpr auto kSchemaVersionKey = "app/settingsSchemaVersion";
constexpr auto kSelectedModeKey = "shell/selectedMode";
constexpr auto kShellGeometryKey = "shell/geometry";
constexpr auto kShellWindowStateKey = "shell/windowState";
constexpr auto kShellSplitterStateKey = "shell/splitterState";
constexpr auto kShellModeRailBehaviourKey = "shell/modeRailBehaviour";
constexpr auto kShellLayoutPrefix = "shell/layout/";
constexpr auto kRestoreSessionKey = "session/restore";
constexpr auto kCrashReportsKey = "diagnostics/crashReports";
constexpr auto kCodeZoomPercentKey = "code/zoomPercent";
constexpr auto kCodeStickyHeadersKey = "code/stickyHeaders";
constexpr auto kUserShortcutsGroup = "shortcuts";
constexpr auto kSessionPackageKey = "session/package";
constexpr auto kSessionMapKey = "session/map";
constexpr auto kSessionMapNameKey = "session/mapName";
constexpr auto kSessionCodeFilesKey = "session/codeFiles";
constexpr auto kSessionCurrentCodeFileKey = "session/currentCodeFile";
constexpr auto kSessionOwnerKey = "session/owner";
constexpr auto kRecentFilesPrefix = "recentFiles/";
constexpr auto kLaunchGameDirectoryPrefix = "launchGameDirectory/";
constexpr auto kRecentCommandsKey = "recentCommands";
constexpr auto kLocaleNameKey = "preferences/localeName";
constexpr auto kTextScalePercentKey = "preferences/textScalePercent";
constexpr auto kThemeKey = "preferences/theme";
constexpr auto kDensityKey = "preferences/density";
constexpr auto kReducedMotionKey = "preferences/reducedMotion";
constexpr auto kTextToSpeechEnabledKey = "preferences/textToSpeechEnabled";
constexpr auto kFormatLocaleKey = "preferences/formatLocale";
constexpr auto kColorVisionKey = "preferences/colorVision";
constexpr auto kReducedSaturationKey = "preferences/reducedSaturation";
constexpr auto kThickFocusIndicatorKey = "preferences/thickFocusIndicator";
constexpr auto kThickTextCursorKey = "preferences/thickTextCursor";
constexpr auto kSteadyTextCursorKey = "preferences/steadyTextCursor";
constexpr auto kUiFontFamilyKey = "preferences/uiFontFamily";
constexpr auto kWideTextSpacingKey = "preferences/wideTextSpacing";
constexpr auto kMessageDurationKey = "preferences/messageDuration";
constexpr auto kVisualAlertsKey = "preferences/visualAlerts";
constexpr auto kSoundCuesKey = "preferences/soundCues";
constexpr auto kSoundCueVolumeKey = "preferences/soundCueVolume";
constexpr auto kScreenReaderAnnouncementsKey = "preferences/screenReaderAnnouncements";
constexpr auto kSpeechEventsKey = "preferences/speechEvents";
constexpr auto kSpeechVoiceKey = "preferences/speechVoice";
constexpr auto kSpeechRateKey = "preferences/speechRate";
constexpr auto kSpeechPitchKey = "preferences/speechPitch";
constexpr auto kSpeechVolumeKey = "preferences/speechVolume";
constexpr auto kSelectedEditorProfileKey = "editor/selectedProfileId";
constexpr auto kAiFreeModeKey = "ai/freeMode";
constexpr auto kAiCloudConnectorsEnabledKey = "ai/cloudConnectorsEnabled";
constexpr auto kAiAgenticWorkflowsEnabledKey = "ai/agenticWorkflowsEnabled";
constexpr auto kAiPreferredReasoningConnectorKey = "ai/preferredReasoningConnector";
constexpr auto kAiPreferredCodingConnectorKey = "ai/preferredCodingConnector";
constexpr auto kAiPreferredVisionConnectorKey = "ai/preferredVisionConnector";
constexpr auto kAiPreferredImageConnectorKey = "ai/preferredImageConnector";
constexpr auto kAiPreferredAudioConnectorKey = "ai/preferredAudioConnector";
constexpr auto kAiPreferredVoiceConnectorKey = "ai/preferredVoiceConnector";
constexpr auto kAiPreferredThreeDConnectorKey = "ai/preferredThreeDConnector";
constexpr auto kAiPreferredEmbeddingsConnectorKey = "ai/preferredEmbeddingsConnector";
constexpr auto kAiPreferredLocalConnectorKey = "ai/preferredLocalConnector";
constexpr auto kAiPreferredTextModelKey = "ai/preferredTextModel";
constexpr auto kAiPreferredCodingModelKey = "ai/preferredCodingModel";
constexpr auto kAiPreferredVisionModelKey = "ai/preferredVisionModel";
constexpr auto kAiPreferredImageModelKey = "ai/preferredImageModel";
constexpr auto kAiPreferredAudioModelKey = "ai/preferredAudioModel";
constexpr auto kAiPreferredVoiceModelKey = "ai/preferredVoiceModel";
constexpr auto kAiPreferredThreeDModelKey = "ai/preferredThreeDModel";
constexpr auto kAiPreferredEmbeddingsModelKey = "ai/preferredEmbeddingsModel";
constexpr auto kAiOpenAiCredentialEnvironmentKey = "ai/openAiCredentialEnvironment";
constexpr auto kAiElevenLabsCredentialEnvironmentKey = "ai/elevenLabsCredentialEnvironment";
constexpr auto kAiMeshyCredentialEnvironmentKey = "ai/meshyCredentialEnvironment";
constexpr auto kAiCustomHttpCredentialEnvironmentKey = "ai/customHttpCredentialEnvironment";
// ai/connectors/<connector id>/model and .../endpoint
constexpr auto kAiConnectorsGroup = "ai/connectors";
constexpr auto kAiConsentGroup = "ai/consent";
constexpr auto kSetupStartedKey = "setup/started";
constexpr auto kSetupSkippedKey = "setup/skipped";
constexpr auto kSetupCompletedKey = "setup/completed";
constexpr auto kSetupCurrentStepKey = "setup/currentStep";
constexpr auto kSetupLastUpdatedUtcKey = "setup/lastUpdatedUtc";
constexpr auto kCurrentProjectPathKey = "projects/currentPath";
constexpr auto kRecentProjectsArray = "recentProjects";
constexpr auto kRecentProjectPathKey = "path";
constexpr auto kRecentProjectDisplayNameKey = "displayName";
constexpr auto kRecentProjectLastOpenedKey = "lastOpenedUtc";
constexpr auto kRecentActivityTasksArray = "recentActivityTasks";
constexpr auto kRecentActivityTaskIdKey = "id";
constexpr auto kRecentActivityTaskTitleKey = "title";
constexpr auto kRecentActivityTaskDetailKey = "detail";
constexpr auto kRecentActivityTaskSourceKey = "source";
constexpr auto kRecentActivityTaskStateKey = "state";
constexpr auto kRecentActivityTaskResultKey = "resultSummary";
constexpr auto kRecentActivityTaskWarningsKey = "warnings";
constexpr auto kRecentActivityTaskCreatedKey = "createdUtc";
constexpr auto kRecentActivityTaskUpdatedKey = "updatedUtc";
constexpr auto kRecentActivityTaskFinishedKey = "finishedUtc";
constexpr auto kRecentActivityTaskProgressCurrentKey = "progressCurrent";
constexpr auto kRecentActivityTaskProgressTotalKey = "progressTotal";
constexpr auto kRecentActivityTaskCancellableKey = "cancellable";
constexpr auto kRecentActivityTaskDurationMsKey = "durationMs";
constexpr auto kRecentActivityTaskLogKey = "log";
constexpr auto kRecentActivityTaskLogTruncatedKey = "logTruncated";
constexpr auto kRecentActivityTaskLogDroppedKey = "logDroppedEntries";
constexpr auto kRecentActivityTaskTransitionsKey = "transitions";
constexpr auto kGameInstallationsArray = "gameInstallations";
constexpr auto kSelectedGameInstallationKey = "gameInstallations/selectedId";
constexpr auto kGameInstallationIdKey = "id";
constexpr auto kGameInstallationGameKey = "gameKey";
constexpr auto kGameInstallationEngineFamilyKey = "engineFamily";
constexpr auto kGameInstallationDisplayNameKey = "displayName";
constexpr auto kGameInstallationRootPathKey = "rootPath";
constexpr auto kGameInstallationExecutablePathKey = "executablePath";
constexpr auto kGameInstallationBasePackagePathsKey = "basePackagePaths";
constexpr auto kGameInstallationModPackagePathsKey = "modPackagePaths";
constexpr auto kGameInstallationPaletteIdKey = "paletteId";
constexpr auto kGameInstallationCompilerProfileIdKey = "compilerProfileId";
constexpr auto kGameInstallationReadOnlyKey = "readOnly";
constexpr auto kGameInstallationActiveKey = "active";
constexpr auto kGameInstallationHiddenKey = "hidden";
constexpr auto kGameInstallationManualKey = "manual";
constexpr auto kGameInstallationCreatedUtcKey = "createdUtc";
constexpr auto kGameInstallationUpdatedUtcKey = "updatedUtc";
constexpr auto kCompilerToolPathOverridesArray = "compilerToolPathOverrides";
constexpr auto kCompilerToolPathOverrideToolIdKey = "toolId";
constexpr auto kCompilerToolPathOverrideExecutablePathKey = "executablePath";

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

// Process-wide override honoured by the default constructor. It is a plain
// global on purpose: it is set once during start-up (CLI --settings-file, or a
// test fixture) before any StudioSettings instance exists.
QString& storeOverrideFilePath()
{
	static QString overridePath;
	return overridePath;
}

// Keys written by schema version 1 that nothing reads any more.
QStringList retiredV1Keys()
{
	return {
		QStringLiteral("preferences/highContrast"),
		QStringLiteral("preferences/highVisibility"),
		QStringLiteral("shell/lastMode"),
		QStringLiteral("shell/lastSelectedModeName"),
		QStringLiteral("ai/experimentalConnectorsEnabled"),
	};
}

QJsonObject operationLogEntryToJson(const OperationLogEntry& entry)
{
	QJsonObject object;
	object.insert(QStringLiteral("t"), normalizedTimestamp(entry.timestampUtc).toString(Qt::ISODate));
	object.insert(QStringLiteral("s"), operationStateId(entry.state));
	object.insert(QStringLiteral("m"), entry.message);
	return object;
}

OperationLogEntry operationLogEntryFromJson(const QJsonObject& object)
{
	OperationLogEntry entry;
	entry.timestampUtc = QDateTime::fromString(object.value(QStringLiteral("t")).toString(), Qt::ISODate).toUTC();
	entry.state = operationStateFromId(object.value(QStringLiteral("s")).toString());
	entry.message = object.value(QStringLiteral("m")).toString();
	return entry;
}

QJsonObject operationTransitionToJson(const OperationStateTransition& transition)
{
	QJsonObject object;
	object.insert(QStringLiteral("t"), normalizedTimestamp(transition.timestampUtc).toString(Qt::ISODate));
	object.insert(QStringLiteral("s"), operationStateId(transition.state));
	object.insert(QStringLiteral("e"), static_cast<double>(transition.elapsedMs));
	object.insert(QStringLiteral("m"), transition.message);
	return object;
}

OperationStateTransition operationTransitionFromJson(const QJsonObject& object)
{
	OperationStateTransition transition;
	transition.timestampUtc = QDateTime::fromString(object.value(QStringLiteral("t")).toString(), Qt::ISODate).toUTC();
	transition.state = operationStateFromId(object.value(QStringLiteral("s")).toString());
	transition.elapsedMs = static_cast<qint64>(object.value(QStringLiteral("e")).toDouble());
	transition.message = object.value(QStringLiteral("m")).toString();
	return transition;
}

// Keeps the persisted tail of a task log inside the documented bounds: at most
// kMaximumActivityLogEntries entries and kMaximumActivityLogBytes of encoded
// text, newest entries kept. Truncation is recorded, never hidden.
RecentActivityTask boundedActivityTask(RecentActivityTask task)
{
	const int transitionOverflow = static_cast<int>(task.transitions.size()) - StudioSettings::kMaximumActivityTransitions;
	if (transitionOverflow > 0) {
		task.transitions.remove(0, transitionOverflow);
	}

	int dropped = task.droppedLogEntryCount;
	const int entryOverflow = static_cast<int>(task.log.size()) - StudioSettings::kMaximumActivityLogEntries;
	if (entryOverflow > 0) {
		task.log.remove(0, entryOverflow);
		dropped += entryOverflow;
	}

	const auto encodedSize = [](const QVector<OperationLogEntry>& log) {
		qsizetype bytes = 0;
		for (const OperationLogEntry& entry : log) {
			bytes += entry.message.toUtf8().size() + 48;
		}
		return bytes;
	};
	while (!task.log.isEmpty() && encodedSize(task.log) > StudioSettings::kMaximumActivityLogBytes) {
		task.log.remove(0, 1);
		++dropped;
	}

	task.droppedLogEntryCount = dropped;
	task.logTruncated = task.logTruncated || dropped > 0;
	return task;
}

QString encodeJsonArray(const QJsonArray& array)
{
	if (array.isEmpty()) {
		return {};
	}
	return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QJsonArray decodeJsonArray(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8());
	return document.isArray() ? document.array() : QJsonArray();
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

StudioSettings::StudioSettings(AccessMode access)
	: m_settings(overrideFilePath().isEmpty()
		? std::make_unique<QSettings>()
		: std::make_unique<QSettings>(overrideFilePath(), QSettings::IniFormat)), m_readOnly(access == AccessMode::ReadOnly)
{
	ensureSchema();
}

StudioSettings::StudioSettings(const QString& filePath, AccessMode access)
	: m_settings(std::make_unique<QSettings>(filePath, QSettings::IniFormat)), m_readOnly(access == AccessMode::ReadOnly)
{
	ensureSchema();
}

StudioSettings::~StudioSettings() = default;

void StudioSettings::setOverrideFilePath(const QString& filePath)
{
	storeOverrideFilePath() = filePath.trimmed();
}

QString StudioSettings::overrideFilePath()
{
	return storeOverrideFilePath();
}

QString StudioSettings::storageLocation() const
{
	return m_settings->fileName();
}

QSettings::Status StudioSettings::status() const
{
	// A refused write is an access failure from the caller's point of view, even
	// though QSettings itself is healthy.
	if (m_discardedWrites > 0) {
		return QSettings::AccessError;
	}
	return m_settings->status();
}

int StudioSettings::discardedWriteCount() const
{
	return m_discardedWrites;
}

void StudioSettings::sync()
{
	m_settings->sync();
}

int StudioSettings::schemaVersion() const
{
	bool ok = false;
	const int version = m_settings->value(kSchemaVersionKey, kSchemaVersion).toInt(&ok);
	return ok ? version : kSchemaVersion;
}

QVector<RecentProject> StudioSettings::recentProjects() const
{
	QVector<RecentProject> projects;

	const int size = m_settings->beginReadArray(kRecentProjectsArray);
	for (int index = 0; index < size; ++index) {
		m_settings->setArrayIndex(index);
		const QString path = normalizedProjectPath(m_settings->value(kRecentProjectPathKey).toString());
		if (path.isEmpty()) {
			continue;
		}

		RecentProject project;
		project.path = path;
		project.displayName = recentProjectDisplayName(path, m_settings->value(kRecentProjectDisplayNameKey).toString());
		project.lastOpenedUtc = normalizedTimestamp(m_settings->value(kRecentProjectLastOpenedKey).toDateTime());
		project.exists = QFileInfo::exists(path);
		projects.push_back(project);
	}
	m_settings->endArray();

	// Sort before de-duplicating and truncating: an oversized stored array must
	// yield the most recent entries, not the first ones in array order.
	std::stable_sort(projects.begin(), projects.end(), [](const RecentProject& left, const RecentProject& right) {
		return left.lastOpenedUtc > right.lastOpenedUtc;
	});

	QVector<RecentProject> unique;
	QStringList seenPaths;
	for (const RecentProject& project : std::as_const(projects)) {
		bool alreadySeen = false;
		for (const QString& seenPath : std::as_const(seenPaths)) {
			if (sameProjectPath(seenPath, project.path)) {
				alreadySeen = true;
				break;
			}
		}
		if (alreadySeen) {
			continue;
		}
		unique.push_back(project);
		seenPaths.push_back(project.path);
		if (unique.size() >= kMaximumRecentProjects) {
			break;
		}
	}
	return unique;
}

QString StudioSettings::currentProjectPath() const
{
	return normalizedProjectPath(m_settings->value(kCurrentProjectPathKey).toString());
}

void StudioSettings::setCurrentProjectPath(const QString& path)
{
	const QString normalizedPath = normalizedProjectPath(path);
	if (normalizedPath.isEmpty()) {
		removeKey(kCurrentProjectPathKey);
		return;
	}
	writeValue(kCurrentProjectPathKey, normalizedPath);
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
	removeKey(kRecentProjectsArray);
}

QStringList StudioSettings::recentFiles(const QString& kind) const
{
	const QString trimmed = kind.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	QStringList unique;
	for (const QString& stored : m_settings->value(QString::fromLatin1(kRecentFilesPrefix) + trimmed).toStringList()) {
		const QString path = QDir::cleanPath(stored.trimmed());
		if (path.isEmpty() || path == QStringLiteral(".")) {
			continue;
		}
		bool seen = false;
		for (const QString& existing : std::as_const(unique)) {
			seen = seen || sameProjectPath(existing, path);
		}
		if (!seen) {
			unique.push_back(path);
		}
		if (unique.size() >= kMaximumRecentFiles) {
			break;
		}
	}
	return unique;
}

QString StudioSettings::launchGameDirectory(const QString& installationId) const
{
	const QString id = installationId.trimmed();
	if (id.isEmpty()) {
		return {};
	}
	return m_settings->value(QString::fromLatin1(kLaunchGameDirectoryPrefix) + id).toString().trimmed();
}

void StudioSettings::setLaunchGameDirectory(const QString& installationId, const QString& folder)
{
	const QString id = installationId.trimmed();
	if (id.isEmpty()) {
		return;
	}
	const QString key = QString::fromLatin1(kLaunchGameDirectoryPrefix) + id;
	if (folder.trimmed().isEmpty()) {
		removeKey(key);
	} else {
		writeValue(key, folder.trimmed());
	}
}

QStringList StudioSettings::recentCommands() const
{
	QStringList commands;
	for (const QString& stored : m_settings->value(QString::fromLatin1(kRecentCommandsKey)).toStringList()) {
		const QString id = stored.trimmed();
		if (!id.isEmpty() && !commands.contains(id) && commands.size() < kMaximumRecentCommands) {
			commands.push_back(id);
		}
	}
	return commands;
}

void StudioSettings::recordRecentCommand(const QString& commandId)
{
	const QString id = commandId.trimmed();
	if (id.isEmpty()) {
		return;
	}
	QStringList commands = recentCommands();
	commands.removeAll(id);
	commands.prepend(id);
	while (commands.size() > kMaximumRecentCommands) {
		commands.removeLast();
	}
	writeValue(QString::fromLatin1(kRecentCommandsKey), commands);
}

QStringList StudioSettings::recentFilterQueries(const QString& filterId) const
{
	const QString id = filterId.trimmed();
	if (id.isEmpty()) {
		return {};
	}
	return m_settings->value(QStringLiteral("filterQueries/%1").arg(id)).toStringList();
}

void StudioSettings::recordFilterQuery(const QString& filterId, const QString& query)
{
	constexpr int kMaximumRecentQueries = 12;
	const QString id = filterId.trimmed();
	const QString text = query.trimmed();
	if (id.isEmpty() || text.isEmpty()) {
		return;
	}
	QStringList queries = recentFilterQueries(id);
	queries.erase(std::remove_if(queries.begin(), queries.end(), [&text](const QString& stored) {
		return stored.compare(text, Qt::CaseInsensitive) == 0;
	}), queries.end());
	queries.prepend(text);
	while (queries.size() > kMaximumRecentQueries) {
		queries.removeLast();
	}
	writeValue(QStringLiteral("filterQueries/%1").arg(id), queries);
}

void StudioSettings::recordRecentFile(const QString& kind, const QString& path)
{
	const QString trimmed = kind.trimmed();
	const QString cleaned = QDir::cleanPath(QFileInfo(path.trimmed()).absoluteFilePath());
	if (trimmed.isEmpty() || path.trimmed().isEmpty()) {
		return;
	}
	QStringList files = recentFiles(trimmed);
	files.erase(std::remove_if(files.begin(), files.end(), [&cleaned](const QString& existing) {
		return sameProjectPath(existing, cleaned);
	}), files.end());
	files.prepend(cleaned);
	while (files.size() > kMaximumRecentFiles) {
		files.removeLast();
	}
	writeValue(QString::fromLatin1(kRecentFilesPrefix) + trimmed, files);
}

void StudioSettings::removeRecentFile(const QString& kind, const QString& path)
{
	const QString trimmed = kind.trimmed();
	if (trimmed.isEmpty() || path.trimmed().isEmpty()) {
		return;
	}
	QStringList files = recentFiles(trimmed);
	const QString cleaned = QDir::cleanPath(QFileInfo(path.trimmed()).absoluteFilePath());
	files.erase(std::remove_if(files.begin(), files.end(), [&cleaned](const QString& existing) {
		return sameProjectPath(existing, cleaned);
	}), files.end());
	writeValue(QString::fromLatin1(kRecentFilesPrefix) + trimmed, files);
}

void StudioSettings::clearRecentFiles()
{
	removeKey(QString::fromLatin1(kRecentFilesPrefix).chopped(1));
}

QVector<RecentActivityTask> StudioSettings::recentActivityTasks() const
{
	QVector<RecentActivityTask> tasks;

	const int size = m_settings->beginReadArray(kRecentActivityTasksArray);
	for (int index = 0; index < size; ++index) {
		m_settings->setArrayIndex(index);

		RecentActivityTask task;
		task.id = m_settings->value(kRecentActivityTaskIdKey).toString().trimmed();
		task.title = m_settings->value(kRecentActivityTaskTitleKey).toString().trimmed();
		task.detail = m_settings->value(kRecentActivityTaskDetailKey).toString().trimmed();
		task.source = m_settings->value(kRecentActivityTaskSourceKey).toString().trimmed();
		task.state = operationStateFromId(m_settings->value(kRecentActivityTaskStateKey, operationStateId(OperationState::Idle)).toString());
		task.resultSummary = m_settings->value(kRecentActivityTaskResultKey).toString().trimmed();
		task.warnings = m_settings->value(kRecentActivityTaskWarningsKey).toStringList();
		task.createdUtc = m_settings->value(kRecentActivityTaskCreatedKey).toDateTime().toUTC();
		task.updatedUtc = normalizedTimestamp(m_settings->value(kRecentActivityTaskUpdatedKey).toDateTime());
		task.finishedUtc = m_settings->value(kRecentActivityTaskFinishedKey).toDateTime().toUTC();
		task.progress.current = m_settings->value(kRecentActivityTaskProgressCurrentKey, 0).toInt();
		task.progress.total = m_settings->value(kRecentActivityTaskProgressTotalKey, 0).toInt();
		task.cancellable = m_settings->value(kRecentActivityTaskCancellableKey, false).toBool();
		task.durationMs = m_settings->value(kRecentActivityTaskDurationMsKey, 0).toLongLong();
		task.logTruncated = m_settings->value(kRecentActivityTaskLogTruncatedKey, false).toBool();
		task.droppedLogEntryCount = m_settings->value(kRecentActivityTaskLogDroppedKey, 0).toInt();
		for (const QJsonValue& value : decodeJsonArray(m_settings->value(kRecentActivityTaskLogKey).toString())) {
			if (value.isObject()) {
				task.log.push_back(operationLogEntryFromJson(value.toObject()));
			}
		}
		for (const QJsonValue& value : decodeJsonArray(m_settings->value(kRecentActivityTaskTransitionsKey).toString())) {
			if (value.isObject()) {
				task.transitions.push_back(operationTransitionFromJson(value.toObject()));
			}
		}

		if (task.id.isEmpty()) {
			task.id = QStringLiteral("%1-%2").arg(task.source.isEmpty() ? QStringLiteral("activity") : normalizedId(task.source), QString::number(task.updatedUtc.toSecsSinceEpoch()));
		}
		if (task.title.isEmpty()) {
			continue;
		}
		if (!task.createdUtc.isValid()) {
			task.createdUtc = task.updatedUtc;
		}
		if (!task.finishedUtc.isValid() && operationStateIsTerminal(task.state)) {
			task.finishedUtc = task.updatedUtc;
		}
		tasks.push_back(task);
	}
	m_settings->endArray();

	// Sort before de-duplicating and truncating so an oversized stored array
	// keeps the most recently updated tasks.
	std::stable_sort(tasks.begin(), tasks.end(), [](const RecentActivityTask& left, const RecentActivityTask& right) {
		return left.updatedUtc > right.updatedUtc;
	});

	QVector<RecentActivityTask> unique;
	QStringList seenIds;
	for (const RecentActivityTask& task : std::as_const(tasks)) {
		if (seenIds.contains(task.id)) {
			continue;
		}
		unique.push_back(task);
		seenIds.push_back(task.id);
		if (unique.size() >= kMaximumRecentActivityTasks) {
			break;
		}
	}
	return unique;
}

void StudioSettings::recordRecentActivityTask(const RecentActivityTask& task)
{
	if (task.title.trimmed().isEmpty()) {
		return;
	}

	RecentActivityTask normalized = task;
	normalized.id = normalized.id.trimmed();
	normalized.title = normalized.title.trimmed();
	normalized.detail = normalized.detail.trimmed();
	normalized.source = normalized.source.trimmed();
	normalized.resultSummary = normalized.resultSummary.trimmed();
	if (normalized.id.isEmpty()) {
		normalized.id = QStringLiteral("%1-%2").arg(normalized.source.isEmpty() ? QStringLiteral("activity") : normalizedId(normalized.source), QString::number(QDateTime::currentSecsSinceEpoch()));
	}
	if (!normalized.createdUtc.isValid()) {
		normalized.createdUtc = QDateTime::currentDateTimeUtc();
	}
	normalized.updatedUtc = normalizedTimestamp(normalized.updatedUtc);
	if (!normalized.finishedUtc.isValid() && operationStateIsTerminal(normalized.state)) {
		normalized.finishedUtc = normalized.updatedUtc;
	}
	if (normalized.durationMs <= 0 && normalized.createdUtc.isValid() && normalized.finishedUtc.isValid()) {
		normalized.durationMs = std::max<qint64>(0, normalized.createdUtc.msecsTo(normalized.finishedUtc));
	}
	normalized = boundedActivityTask(normalized);

	QVector<RecentActivityTask> tasks = recentActivityTasks();
	tasks.erase(
		std::remove_if(tasks.begin(), tasks.end(), [&normalized](const RecentActivityTask& existing) {
			return existing.id == normalized.id;
		}),
		tasks.end());
	tasks.prepend(normalized);
	if (tasks.size() > kMaximumRecentActivityTasks) {
		tasks.resize(kMaximumRecentActivityTasks);
	}
	writeRecentActivityTasks(tasks);
}

void StudioSettings::clearRecentActivityTasks()
{
	removeKey(kRecentActivityTasksArray);
}

QVector<GameInstallationProfile> StudioSettings::gameInstallations() const
{
	QVector<GameInstallationProfile> profiles;

	const int size = m_settings->beginReadArray(kGameInstallationsArray);
	for (int index = 0; index < size; ++index) {
		m_settings->setArrayIndex(index);

		GameInstallationProfile profile;
		profile.id = m_settings->value(kGameInstallationIdKey).toString();
		profile.gameKey = m_settings->value(kGameInstallationGameKey, QStringLiteral("custom")).toString();
		profile.engineFamily = gameEngineFamilyFromId(m_settings->value(kGameInstallationEngineFamilyKey, gameEngineFamilyId(GameEngineFamily::Unknown)).toString());
		profile.displayName = m_settings->value(kGameInstallationDisplayNameKey).toString();
		profile.rootPath = m_settings->value(kGameInstallationRootPathKey).toString();
		profile.executablePath = m_settings->value(kGameInstallationExecutablePathKey).toString();
		profile.basePackagePaths = m_settings->value(kGameInstallationBasePackagePathsKey).toStringList();
		profile.modPackagePaths = m_settings->value(kGameInstallationModPackagePathsKey).toStringList();
		profile.paletteId = m_settings->value(kGameInstallationPaletteIdKey).toString();
		profile.compilerProfileId = m_settings->value(kGameInstallationCompilerProfileIdKey).toString();
		profile.readOnly = m_settings->value(kGameInstallationReadOnlyKey, true).toBool();
		profile.active = m_settings->value(kGameInstallationActiveKey, true).toBool();
		profile.hidden = m_settings->value(kGameInstallationHiddenKey, false).toBool();
		profile.manual = m_settings->value(kGameInstallationManualKey, true).toBool();
		profile.createdUtc = m_settings->value(kGameInstallationCreatedUtcKey).toDateTime().toUTC();
		profile.updatedUtc = normalizedTimestamp(m_settings->value(kGameInstallationUpdatedUtcKey).toDateTime());
		profile = normalizedGameInstallationProfile(profile);

		if (profile.rootPath.isEmpty()) {
			continue;
		}

		profiles.push_back(profile);
	}
	m_settings->endArray();

	// Sort before de-duplicating and truncating so the most recently updated
	// installation profiles survive an oversized stored array.
	std::stable_sort(profiles.begin(), profiles.end(), [](const GameInstallationProfile& left, const GameInstallationProfile& right) {
		return left.updatedUtc > right.updatedUtc;
	});

	QVector<GameInstallationProfile> unique;
	QStringList seenIds;
	for (const GameInstallationProfile& profile : std::as_const(profiles)) {
		if (seenIds.contains(profile.id)) {
			continue;
		}
		unique.push_back(profile);
		seenIds.push_back(profile.id);
		if (unique.size() >= kMaximumGameInstallationProfiles) {
			break;
		}
	}
	return unique;
}

void StudioSettings::upsertGameInstallation(GameInstallationProfile profile)
{
	profile = normalizedGameInstallationProfile(profile);
	if (profile.rootPath.isEmpty()) {
		return;
	}

	const QDateTime now = QDateTime::currentDateTimeUtc();
	QVector<GameInstallationProfile> profiles = gameInstallations();
	for (const GameInstallationProfile& existing : profiles) {
		if (sameGameInstallationId(existing.id, profile.id)) {
			if (!profile.createdUtc.isValid()) {
				profile.createdUtc = existing.createdUtc;
			}
			break;
		}
	}
	if (!profile.createdUtc.isValid()) {
		profile.createdUtc = now;
	}
	profile.updatedUtc = now;

	profiles.erase(
		std::remove_if(profiles.begin(), profiles.end(), [&profile](const GameInstallationProfile& existing) {
			return sameGameInstallationId(existing.id, profile.id);
		}),
		profiles.end());
	profiles.prepend(profile);

	if (profiles.size() > kMaximumGameInstallationProfiles) {
		profiles.resize(kMaximumGameInstallationProfiles);
	}

	writeGameInstallations(profiles);
	if (selectedGameInstallationId().isEmpty()) {
		setSelectedGameInstallation(profile.id);
	}
}

void StudioSettings::removeGameInstallation(const QString& id)
{
	QVector<GameInstallationProfile> profiles = gameInstallations();
	profiles.erase(
		std::remove_if(profiles.begin(), profiles.end(), [&id](const GameInstallationProfile& profile) {
			return sameGameInstallationId(profile.id, id);
		}),
		profiles.end());
	writeGameInstallations(profiles);

	if (sameGameInstallationId(selectedGameInstallationId(), id)) {
		if (profiles.isEmpty()) {
			removeKey(kSelectedGameInstallationKey);
		} else {
			writeValue(kSelectedGameInstallationKey, profiles.front().id);
		}
	}
}

void StudioSettings::clearGameInstallations()
{
	removeKey(kGameInstallationsArray);
	removeKey(kSelectedGameInstallationKey);
}

QString StudioSettings::selectedGameInstallationId() const
{
	const QString selectedId = m_settings->value(kSelectedGameInstallationKey).toString();
	if (selectedId.isEmpty()) {
		return {};
	}
	for (const GameInstallationProfile& profile : gameInstallations()) {
		if (sameGameInstallationId(profile.id, selectedId)) {
			return profile.id;
		}
	}
	return {};
}

void StudioSettings::setSelectedGameInstallation(const QString& id)
{
	if (id.trimmed().isEmpty()) {
		removeKey(kSelectedGameInstallationKey);
		return;
	}

	GameInstallationProfile requested;
	requested.id = id;
	const QString normalized = stableGameInstallationId(requested);
	if (normalized.isEmpty()) {
		removeKey(kSelectedGameInstallationKey);
		return;
	}
	for (const GameInstallationProfile& profile : gameInstallations()) {
		if (sameGameInstallationId(profile.id, normalized)) {
			writeValue(kSelectedGameInstallationKey, profile.id);
			return;
		}
	}
}

AccessibilityPreferences StudioSettings::accessibilityPreferences() const
{
	AccessibilityPreferences preferences;
	preferences.localeName = normalizedLocaleName(m_settings->value(kLocaleNameKey, preferences.localeName).toString());
	preferences.textScalePercent = normalizedTextScalePercent(m_settings->value(kTextScalePercentKey, preferences.textScalePercent).toInt());
	preferences.theme = themeFromId(m_settings->value(kThemeKey, themeId(preferences.theme)).toString());
	preferences.density = densityFromId(m_settings->value(kDensityKey, densityId(preferences.density)).toString());
	preferences.reducedMotion = m_settings->value(kReducedMotionKey, preferences.reducedMotion).toBool();
	preferences.textToSpeechEnabled = m_settings->value(kTextToSpeechEnabledKey, preferences.textToSpeechEnabled).toBool();
	preferences.formatLocaleName = normalizedFormatLocaleName(m_settings->value(kFormatLocaleKey, preferences.formatLocaleName).toString());
	preferences.colorVision = colorVisionFromId(m_settings->value(kColorVisionKey, colorVisionId(preferences.colorVision)).toString());
	preferences.reducedSaturation = m_settings->value(kReducedSaturationKey, preferences.reducedSaturation).toBool();
	preferences.thickFocusIndicator = m_settings->value(kThickFocusIndicatorKey, preferences.thickFocusIndicator).toBool();
	preferences.thickTextCursor = m_settings->value(kThickTextCursorKey, preferences.thickTextCursor).toBool();
	preferences.steadyTextCursor = m_settings->value(kSteadyTextCursorKey, preferences.steadyTextCursor).toBool();
	preferences.uiFontFamily = m_settings->value(kUiFontFamilyKey, preferences.uiFontFamily).toString().trimmed();
	preferences.wideTextSpacing = m_settings->value(kWideTextSpacingKey, preferences.wideTextSpacing).toBool();
	preferences.messageDuration = messageDurationFromId(m_settings->value(kMessageDurationKey, messageDurationId(preferences.messageDuration)).toString());
	preferences.visualAlerts = m_settings->value(kVisualAlertsKey, preferences.visualAlerts).toBool();
	preferences.soundCues = m_settings->value(kSoundCuesKey, preferences.soundCues).toBool();
	preferences.soundCueVolume = std::clamp(m_settings->value(kSoundCueVolumeKey, preferences.soundCueVolume).toInt(), 0, 100);
	preferences.screenReaderAnnouncements = m_settings->value(kScreenReaderAnnouncementsKey, preferences.screenReaderAnnouncements).toBool();
	// A stored empty list means every event was switched off; a missing key
	// keeps the defaults.
	if (m_settings->contains(kSpeechEventsKey)) {
		preferences.speechEvents = normalizedSpeechEvents(m_settings->value(kSpeechEventsKey).toStringList());
	}
	preferences.speechVoice = m_settings->value(kSpeechVoiceKey, preferences.speechVoice).toString().trimmed();
	preferences.speechRate = std::clamp(m_settings->value(kSpeechRateKey, preferences.speechRate).toInt(), -10, 10);
	preferences.speechPitch = std::clamp(m_settings->value(kSpeechPitchKey, preferences.speechPitch).toInt(), -10, 10);
	preferences.speechVolume = std::clamp(m_settings->value(kSpeechVolumeKey, preferences.speechVolume).toInt(), 0, 100);
	return preferences;
}

void StudioSettings::setAccessibilityPreferences(const AccessibilityPreferences& preferences)
{
	writeValue(kLocaleNameKey, normalizedLocaleName(preferences.localeName));
	writeValue(kTextScalePercentKey, normalizedTextScalePercent(preferences.textScalePercent));
	writeValue(kThemeKey, themeId(preferences.theme));
	writeValue(kDensityKey, densityId(preferences.density));
	writeValue(kReducedMotionKey, preferences.reducedMotion);
	writeValue(kTextToSpeechEnabledKey, preferences.textToSpeechEnabled);
	writeValue(kFormatLocaleKey, normalizedFormatLocaleName(preferences.formatLocaleName));
	writeValue(kColorVisionKey, colorVisionId(preferences.colorVision));
	writeValue(kReducedSaturationKey, preferences.reducedSaturation);
	writeValue(kThickFocusIndicatorKey, preferences.thickFocusIndicator);
	writeValue(kThickTextCursorKey, preferences.thickTextCursor);
	writeValue(kSteadyTextCursorKey, preferences.steadyTextCursor);
	writeValue(kUiFontFamilyKey, preferences.uiFontFamily.trimmed());
	writeValue(kWideTextSpacingKey, preferences.wideTextSpacing);
	writeValue(kMessageDurationKey, messageDurationId(preferences.messageDuration));
	writeValue(kVisualAlertsKey, preferences.visualAlerts);
	writeValue(kSoundCuesKey, preferences.soundCues);
	writeValue(kSoundCueVolumeKey, std::clamp(preferences.soundCueVolume, 0, 100));
	writeValue(kScreenReaderAnnouncementsKey, preferences.screenReaderAnnouncements);
	writeValue(kSpeechEventsKey, normalizedSpeechEvents(preferences.speechEvents));
	writeValue(kSpeechVoiceKey, preferences.speechVoice.trimmed());
	writeValue(kSpeechRateKey, std::clamp(preferences.speechRate, -10, 10));
	writeValue(kSpeechPitchKey, std::clamp(preferences.speechPitch, -10, 10));
	writeValue(kSpeechVolumeKey, std::clamp(preferences.speechVolume, 0, 100));
}

void StudioSettings::setLocaleName(const QString& localeName)
{
	writeValue(kLocaleNameKey, normalizedLocaleName(localeName));
}

void StudioSettings::setTextScalePercent(int textScalePercent)
{
	writeValue(kTextScalePercentKey, normalizedTextScalePercent(textScalePercent));
}

void StudioSettings::setTheme(StudioTheme theme)
{
	writeValue(kThemeKey, themeId(theme));
}

void StudioSettings::setDensity(UiDensity density)
{
	writeValue(kDensityKey, densityId(density));
}

void StudioSettings::setReducedMotion(bool reducedMotion)
{
	writeValue(kReducedMotionKey, reducedMotion);
}

void StudioSettings::setTextToSpeechEnabled(bool enabled)
{
	writeValue(kTextToSpeechEnabledKey, enabled);
}

QString StudioSettings::selectedEditorProfileId() const
{
	const QString requested = m_settings->value(kSelectedEditorProfileKey, defaultEditorProfileId()).toString();
	EditorProfileDescriptor descriptor;
	if (editorProfileForId(requested, &descriptor)) {
		return descriptor.id;
	}
	return defaultEditorProfileId();
}

void StudioSettings::setSelectedEditorProfileId(const QString& id)
{
	EditorProfileDescriptor descriptor;
	if (editorProfileForId(id, &descriptor)) {
		writeValue(kSelectedEditorProfileKey, descriptor.id);
		return;
	}
	writeValue(kSelectedEditorProfileKey, defaultEditorProfileId());
}

QVector<CompilerToolPathOverride> StudioSettings::compilerToolPathOverrides() const
{
	QVector<CompilerToolPathOverride> overrides;
	QStringList seen;
	const int size = m_settings->beginReadArray(kCompilerToolPathOverridesArray);
	for (int index = 0; index < size; ++index) {
		m_settings->setArrayIndex(index);
		CompilerToolPathOverride override;
		override.toolId = normalizedId(m_settings->value(kCompilerToolPathOverrideToolIdKey).toString());
		override.executablePath = normalizedProjectPath(m_settings->value(kCompilerToolPathOverrideExecutablePathKey).toString());
		if (override.toolId.isEmpty() || override.executablePath.isEmpty() || seen.contains(override.toolId)) {
			continue;
		}
		overrides.push_back(override);
		seen.push_back(override.toolId);
		if (overrides.size() >= kMaximumCompilerToolPathOverrides) {
			break;
		}
	}
	m_settings->endArray();
	return overrides;
}

void StudioSettings::upsertCompilerToolPathOverride(const CompilerToolPathOverride& override)
{
	CompilerToolDescriptor descriptor;
	if (!compilerToolDescriptorForId(override.toolId, &descriptor)) {
		return;
	}
	const QString executablePath = normalizedProjectPath(override.executablePath);
	if (executablePath.isEmpty()) {
		return;
	}

	QVector<CompilerToolPathOverride> overrides = compilerToolPathOverrides();
	overrides.erase(
		std::remove_if(overrides.begin(), overrides.end(), [&descriptor](const CompilerToolPathOverride& existing) {
			return normalizedId(existing.toolId) == normalizedId(descriptor.id);
		}),
		overrides.end());
	overrides.prepend({descriptor.id, executablePath});
	if (overrides.size() > kMaximumCompilerToolPathOverrides) {
		overrides.resize(kMaximumCompilerToolPathOverrides);
	}
	writeCompilerToolPathOverrides(overrides);
}

void StudioSettings::removeCompilerToolPathOverride(const QString& toolId)
{
	const QString normalizedToolId = normalizedId(toolId);
	QVector<CompilerToolPathOverride> overrides = compilerToolPathOverrides();
	overrides.erase(
		std::remove_if(overrides.begin(), overrides.end(), [&normalizedToolId](const CompilerToolPathOverride& existing) {
			return normalizedId(existing.toolId) == normalizedToolId;
		}),
		overrides.end());
	writeCompilerToolPathOverrides(overrides);
}

void StudioSettings::clearCompilerToolPathOverrides()
{
	removeKey(kCompilerToolPathOverridesArray);
}

AiAutomationPreferences StudioSettings::aiAutomationPreferences() const
{
	AiAutomationPreferences preferences = defaultAiAutomationPreferences();
	preferences.aiFreeMode = m_settings->value(kAiFreeModeKey, preferences.aiFreeMode).toBool();
	preferences.cloudConnectorsEnabled = m_settings->value(kAiCloudConnectorsEnabledKey, preferences.cloudConnectorsEnabled).toBool();
	preferences.agenticWorkflowsEnabled = m_settings->value(kAiAgenticWorkflowsEnabledKey, preferences.agenticWorkflowsEnabled).toBool();
	preferences.preferredReasoningConnectorId = m_settings->value(kAiPreferredReasoningConnectorKey).toString();
	preferences.preferredCodingConnectorId = m_settings->value(kAiPreferredCodingConnectorKey).toString();
	preferences.preferredVisionConnectorId = m_settings->value(kAiPreferredVisionConnectorKey).toString();
	preferences.preferredImageConnectorId = m_settings->value(kAiPreferredImageConnectorKey).toString();
	preferences.preferredAudioConnectorId = m_settings->value(kAiPreferredAudioConnectorKey).toString();
	preferences.preferredVoiceConnectorId = m_settings->value(kAiPreferredVoiceConnectorKey).toString();
	preferences.preferredThreeDConnectorId = m_settings->value(kAiPreferredThreeDConnectorKey).toString();
	preferences.preferredEmbeddingsConnectorId = m_settings->value(kAiPreferredEmbeddingsConnectorKey).toString();
	preferences.preferredLocalConnectorId = m_settings->value(kAiPreferredLocalConnectorKey).toString();
	preferences.preferredTextModelId = m_settings->value(kAiPreferredTextModelKey).toString();
	preferences.preferredCodingModelId = m_settings->value(kAiPreferredCodingModelKey).toString();
	preferences.preferredVisionModelId = m_settings->value(kAiPreferredVisionModelKey).toString();
	preferences.preferredImageModelId = m_settings->value(kAiPreferredImageModelKey).toString();
	preferences.preferredAudioModelId = m_settings->value(kAiPreferredAudioModelKey).toString();
	preferences.preferredVoiceModelId = m_settings->value(kAiPreferredVoiceModelKey).toString();
	preferences.preferredThreeDModelId = m_settings->value(kAiPreferredThreeDModelKey).toString();
	preferences.preferredEmbeddingsModelId = m_settings->value(kAiPreferredEmbeddingsModelKey).toString();
	preferences.openAiCredentialEnvironmentVariable = m_settings->value(kAiOpenAiCredentialEnvironmentKey, preferences.openAiCredentialEnvironmentVariable).toString();
	preferences.elevenLabsCredentialEnvironmentVariable = m_settings->value(kAiElevenLabsCredentialEnvironmentKey, preferences.elevenLabsCredentialEnvironmentVariable).toString();
	preferences.meshyCredentialEnvironmentVariable = m_settings->value(kAiMeshyCredentialEnvironmentKey, preferences.meshyCredentialEnvironmentVariable).toString();
	preferences.customHttpCredentialEnvironmentVariable = m_settings->value(kAiCustomHttpCredentialEnvironmentKey, preferences.customHttpCredentialEnvironmentVariable).toString();
	for (const QString& connectorId : aiConnectorIds()) {
		const QString base = QStringLiteral("%1/%2/").arg(QLatin1String(kAiConnectorsGroup), connectorId);
		const QString model = m_settings->value(base + QStringLiteral("model")).toString();
		const QString endpoint = m_settings->value(base + QStringLiteral("endpoint")).toString();
		if (!model.trimmed().isEmpty()) {
			preferences.connectorModels.insert(connectorId, model);
		}
		if (!endpoint.trimmed().isEmpty()) {
			preferences.connectorEndpoints.insert(connectorId, endpoint);
		}
		const QString imageModel = m_settings->value(base + QStringLiteral("imageModel")).toString();
		const QString imageEndpoint = m_settings->value(base + QStringLiteral("imageEndpoint")).toString();
		if (!imageModel.trimmed().isEmpty()) {
			preferences.connectorImageModels.insert(connectorId, imageModel);
		}
		if (!imageEndpoint.trimmed().isEmpty()) {
			preferences.connectorImageEndpoints.insert(connectorId, imageEndpoint);
		}
		const QString audioModel = m_settings->value(base + QStringLiteral("audioModel")).toString();
		const QString audioEndpoint = m_settings->value(base + QStringLiteral("audioEndpoint")).toString();
		if (!audioModel.trimmed().isEmpty()) {
			preferences.connectorAudioModels.insert(connectorId, audioModel);
		}
		if (!audioEndpoint.trimmed().isEmpty()) {
			preferences.connectorAudioEndpoints.insert(connectorId, audioEndpoint);
		}
	}
	// The open project may turn AI off for itself; it cannot turn it on.
	const QString project = currentProjectPath();
	ProjectManifest manifest;
	if (!project.isEmpty() && QFileInfo::exists(projectManifestPath(project)) && loadProjectManifest(project, &manifest)) {
		preferences.projectAiFree = manifest.settingsOverrides.aiFreeModeSet && manifest.settingsOverrides.aiFreeMode;
	}
	return normalizedAiAutomationPreferences(preferences);
}

void StudioSettings::setAiAutomationPreferences(const AiAutomationPreferences& preferences)
{
	const AiAutomationPreferences normalized = normalizedAiAutomationPreferences(preferences);
	writeValue(kAiFreeModeKey, normalized.aiFreeMode);
	writeValue(kAiCloudConnectorsEnabledKey, normalized.cloudConnectorsEnabled);
	writeValue(kAiAgenticWorkflowsEnabledKey, normalized.agenticWorkflowsEnabled);
	writeValue(kAiPreferredReasoningConnectorKey, normalized.preferredReasoningConnectorId);
	writeValue(kAiPreferredCodingConnectorKey, normalized.preferredCodingConnectorId);
	writeValue(kAiPreferredVisionConnectorKey, normalized.preferredVisionConnectorId);
	writeValue(kAiPreferredImageConnectorKey, normalized.preferredImageConnectorId);
	writeValue(kAiPreferredAudioConnectorKey, normalized.preferredAudioConnectorId);
	writeValue(kAiPreferredVoiceConnectorKey, normalized.preferredVoiceConnectorId);
	writeValue(kAiPreferredThreeDConnectorKey, normalized.preferredThreeDConnectorId);
	writeValue(kAiPreferredEmbeddingsConnectorKey, normalized.preferredEmbeddingsConnectorId);
	writeValue(kAiPreferredLocalConnectorKey, normalized.preferredLocalConnectorId);
	writeValue(kAiPreferredTextModelKey, normalized.preferredTextModelId);
	writeValue(kAiPreferredCodingModelKey, normalized.preferredCodingModelId);
	writeValue(kAiPreferredVisionModelKey, normalized.preferredVisionModelId);
	writeValue(kAiPreferredImageModelKey, normalized.preferredImageModelId);
	writeValue(kAiPreferredAudioModelKey, normalized.preferredAudioModelId);
	writeValue(kAiPreferredVoiceModelKey, normalized.preferredVoiceModelId);
	writeValue(kAiPreferredThreeDModelKey, normalized.preferredThreeDModelId);
	writeValue(kAiPreferredEmbeddingsModelKey, normalized.preferredEmbeddingsModelId);
	writeValue(kAiOpenAiCredentialEnvironmentKey, normalized.openAiCredentialEnvironmentVariable);
	writeValue(kAiElevenLabsCredentialEnvironmentKey, normalized.elevenLabsCredentialEnvironmentVariable);
	writeValue(kAiMeshyCredentialEnvironmentKey, normalized.meshyCredentialEnvironmentVariable);
	writeValue(kAiCustomHttpCredentialEnvironmentKey, normalized.customHttpCredentialEnvironmentVariable);
	for (const QString& connectorId : aiConnectorIds()) {
		const QString base = QStringLiteral("%1/%2/").arg(QLatin1String(kAiConnectorsGroup), connectorId);
		const QString model = normalized.connectorModels.value(connectorId);
		const QString endpoint = normalized.connectorEndpoints.value(connectorId);
		model.isEmpty() ? removeKey(base + QStringLiteral("model")) : writeValue(base + QStringLiteral("model"), model);
		endpoint.isEmpty() ? removeKey(base + QStringLiteral("endpoint")) : writeValue(base + QStringLiteral("endpoint"), endpoint);
		const QString imageModel = normalized.connectorImageModels.value(connectorId);
		const QString imageEndpoint = normalized.connectorImageEndpoints.value(connectorId);
		imageModel.isEmpty() ? removeKey(base + QStringLiteral("imageModel")) : writeValue(base + QStringLiteral("imageModel"), imageModel);
		imageEndpoint.isEmpty() ? removeKey(base + QStringLiteral("imageEndpoint")) : writeValue(base + QStringLiteral("imageEndpoint"), imageEndpoint);
		const QString audioModel = normalized.connectorAudioModels.value(connectorId);
		const QString audioEndpoint = normalized.connectorAudioEndpoints.value(connectorId);
		audioModel.isEmpty() ? removeKey(base + QStringLiteral("audioModel")) : writeValue(base + QStringLiteral("audioModel"), audioModel);
		audioEndpoint.isEmpty() ? removeKey(base + QStringLiteral("audioEndpoint")) : writeValue(base + QStringLiteral("audioEndpoint"), audioEndpoint);
	}
}

namespace {

// Consent is filed under a digest of the project folder: a path is no key.
QString aiConsentKey(const QString& projectPath)
{
	QString folder = projectPath.trimmed();
	if (!folder.isEmpty()) {
		folder = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
#ifdef Q_OS_WIN
		folder = folder.toLower();
#endif
	}
	const QByteArray digest = QCryptographicHash::hash(folder.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
	return QStringLiteral("%1/%2").arg(QLatin1String(kAiConsentGroup), QString::fromLatin1(digest));
}

} // namespace

bool StudioSettings::aiContextConsentGiven(const QString& projectPath, const QString& destination) const
{
	return m_settings->value(aiConsentKey(projectPath)).toStringList().contains(destination.trimmed().toLower());
}

void StudioSettings::setAiContextConsentGiven(const QString& projectPath, const QString& destination, bool given)
{
	const QString key = aiConsentKey(projectPath);
	QStringList destinations = m_settings->value(key).toStringList();
	const QString entry = destination.trimmed().toLower();
	destinations.removeAll(entry);
	if (given && !entry.isEmpty()) {
		destinations << entry;
	}
	destinations.isEmpty() ? removeKey(key) : writeValue(key, destinations);
}

void StudioSettings::clearAiContextConsent()
{
	removeKey(QLatin1String(kAiConsentGroup));
}

SetupProgress StudioSettings::setupProgress() const
{
	SetupProgress progress;
	progress.currentStep = setupStepFromId(m_settings->value(kSetupCurrentStepKey, setupStepId(progress.currentStep)).toString());
	progress.started = m_settings->value(kSetupStartedKey, false).toBool();
	progress.skipped = m_settings->value(kSetupSkippedKey, false).toBool();
	progress.completed = m_settings->value(kSetupCompletedKey, false).toBool();
	progress.lastUpdatedUtc = m_settings->value(kSetupLastUpdatedUtcKey).toDateTime().toUTC();
	return progress;
}

SetupSummary StudioSettings::setupSummary() const
{
	const SetupProgress progress = setupProgress();
	const AccessibilityPreferences preferences = accessibilityPreferences();
	const AiAutomationPreferences aiPreferences = aiAutomationPreferences();
	const QVector<RecentProject> projects = recentProjects();
	const QVector<GameInstallationProfile> installations = gameInstallations();
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
		summary.nextAction = QStringLiteral("Start setup to review access, workspace, projects, game installations, toolchains, AI, and CLI choices.");
	}

	for (int index = 0; index < steps.size(); ++index) {
		const QString item = QStringLiteral("%1: %2").arg(setupStepDisplayName(steps[index]), setupStepDescription(steps[index]));
		if (progress.completed || (progress.started && !progress.skipped && index < currentIndex)) {
			summary.completedItems.push_back(item);
		} else {
			summary.pendingItems.push_back(item);
		}
	}

	if (isSystemLocalizationPreference(preferences.localeName)) {
		// Following the system is right unless the system speaks a language
		// with no catalog, which quietly leaves the interface in English.
		const QStringList systemLanguages = systemLanguageTags();
		const QString firstLanguage = systemLanguages.isEmpty() ? QString() : systemLanguages.first().section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
		if (systemLocalizationTargetId() == QStringLiteral("en") && !firstLanguage.isEmpty() && firstLanguage != QStringLiteral("en")) {
			summary.warnings.push_back(QStringLiteral("The system language (%1) has no VibeStudio translation, so the interface is in English.").arg(systemLanguages.first()));
		}
	}
	const AccessibilityPreferences defaults;
	if (preferences.theme == defaults.theme && preferences.textScalePercent == defaults.textScalePercent && preferences.density == defaults.density
		&& preferences.reducedMotion == defaults.reducedMotion && preferences.textToSpeechEnabled == defaults.textToSpeechEnabled
		&& preferences.colorVision == defaults.colorVision && !preferences.reducedSaturation && !preferences.thickFocusIndicator
		&& !preferences.thickTextCursor && !preferences.steadyTextCursor && preferences.uiFontFamily.isEmpty() && !preferences.wideTextSpacing
		&& preferences.messageDuration == defaults.messageDuration && preferences.soundCues == defaults.soundCues) {
		summary.warnings.push_back(QStringLiteral("Accessibility preferences are still at defaults."));
	}
	if (projects.isEmpty()) {
		summary.warnings.push_back(QStringLiteral("No recent project folder has been selected yet."));
	}
	if (installations.isEmpty()) {
		summary.warnings.push_back(QStringLiteral("No game installation profile has been added yet."));
	}
	if (!aiPreferences.aiFreeMode && aiPreferences.cloudConnectorsEnabled) {
		summary.warnings.push_back(QStringLiteral("AI connectors are enabled as experimental design stubs; no provider credentials are stored yet."));
	}

	return summary;
}

void StudioSettings::startOrResumeSetup(SetupStep step)
{
	writeValue(kSetupStartedKey, true);
	writeValue(kSetupSkippedKey, false);
	writeValue(kSetupCompletedKey, false);
	writeValue(kSetupCurrentStepKey, setupStepId(step));
	writeValue(kSetupLastUpdatedUtcKey, setupTimestamp());
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
	writeValue(kSetupStartedKey, progress.started);
	writeValue(kSetupSkippedKey, true);
	writeValue(kSetupCompletedKey, false);
	writeValue(kSetupCurrentStepKey, setupStepId(progress.currentStep));
	writeValue(kSetupLastUpdatedUtcKey, setupTimestamp());
}

void StudioSettings::completeSetup()
{
	writeValue(kSetupStartedKey, true);
	writeValue(kSetupSkippedKey, false);
	writeValue(kSetupCompletedKey, true);
	writeValue(kSetupCurrentStepKey, setupStepId(SetupStep::ReviewFinish));
	writeValue(kSetupLastUpdatedUtcKey, setupTimestamp());
}

void StudioSettings::resetSetup()
{
	removeKey(kSetupStartedKey);
	removeKey(kSetupSkippedKey);
	removeKey(kSetupCompletedKey);
	removeKey(kSetupCurrentStepKey);
	removeKey(kSetupLastUpdatedUtcKey);
}

int StudioSettings::selectedMode() const
{
	bool ok = false;
	const int modeIndex = m_settings->value(kSelectedModeKey, 0).toInt(&ok);
	return ok ? std::max(0, modeIndex) : 0;
}

void StudioSettings::setSelectedMode(int modeIndex)
{
	writeValue(kSelectedModeKey, std::max(0, modeIndex));
}

QByteArray StudioSettings::shellGeometry() const
{
	return m_settings->value(kShellGeometryKey).toByteArray();
}

void StudioSettings::setShellGeometry(const QByteArray& geometry)
{
	writeValue(kShellGeometryKey, geometry);
}

QByteArray StudioSettings::shellWindowState() const
{
	return m_settings->value(kShellWindowStateKey).toByteArray();
}

void StudioSettings::setShellWindowState(const QByteArray& windowState)
{
	writeValue(kShellWindowStateKey, windowState);
}

QByteArray StudioSettings::shellSplitterState() const
{
	return m_settings->value(kShellSplitterStateKey).toByteArray();
}

void StudioSettings::setShellSplitterState(const QByteArray& splitterState)
{
	writeValue(kShellSplitterStateKey, splitterState);
}

bool StudioSession::isEmpty() const
{
	return packagePath.isEmpty() && mapPath.isEmpty() && codeFiles.isEmpty();
}

bool StudioSettings::restoreSession() const
{
	return m_settings->value(kRestoreSessionKey, true).toBool();
}

void StudioSettings::setRestoreSession(bool enabled)
{
	writeValue(kRestoreSessionKey, enabled);
}

bool StudioSettings::crashReports() const
{
	return m_settings->value(kCrashReportsKey, true).toBool();
}

bool StudioSettings::levelRecoveryEnabled() const
{
	return m_settings->value(QStringLiteral("levels/recoveryEnabled"), true).toBool();
}

QString StudioSettings::levelViewLayoutPreference() const
{
	const QString value = m_settings->value(QStringLiteral("levels/viewLayout"), QStringLiteral("profile")).toString();
	return levelViewLayoutForId(value, nullptr) ? value : QStringLiteral("profile");
}

bool StudioSettings::setLevelViewLayoutPreference(const QString& id)
{
	if (id != QStringLiteral("profile") && !levelViewLayoutForId(id, nullptr)) { return false; }
	writeValue(QStringLiteral("levels/viewLayout"), id);
	return !isReadOnly();
}

LevelViewLinks StudioSettings::levelViewLinks() const
{
	const auto values = m_settings->value(QStringLiteral("levels/viewLinks")).toMap();
	return {values.value(QStringLiteral("centers"), false).toBool(), values.value(QStringLiteral("zoom"), false).toBool(),
		values.value(QStringLiteral("followCamera"), false).toBool()};
}

bool StudioSettings::setLevelViewLinks(const LevelViewLinks& links)
{
	if (isReadOnly()) { return false; }
	writeValue(QStringLiteral("levels/viewLinks"), QVariantMap {{QStringLiteral("centers"), links.centers},
		{QStringLiteral("zoom"), links.zoom}, {QStringLiteral("followCamera"), links.followCamera}});
	sync();
	return status() == QSettings::NoError;
}

bool StudioSettings::levelTextureLock() const
{
	return m_settings->value(QStringLiteral("levels/textureLock"), true).toBool();
}

void StudioSettings::setLevelTextureLock(bool enabled)
{
	writeValue(QStringLiteral("levels/textureLock"), enabled);
}

bool StudioSettings::levelTextureScaleLock() const
{
	return m_settings->value(QStringLiteral("levels/textureScaleLock"), false).toBool();
}

void StudioSettings::setLevelTextureScaleLock(bool enabled)
{
	writeValue(QStringLiteral("levels/textureScaleLock"), enabled);
}

bool StudioSettings::levelAllowValve220() const
{
	return m_settings->value(QStringLiteral("levels/allowValve220"), false).toBool();
}

void StudioSettings::setLevelAllowValve220(bool enabled)
{
	writeValue(QStringLiteral("levels/allowValve220"), enabled);
}

void StudioSettings::setLevelRecoveryEnabled(bool enabled)
{
	writeValue(QStringLiteral("levels/recoveryEnabled"), enabled);
}

bool StudioSettings::codeRecoveryEnabled() const
{
	return m_settings->value(QStringLiteral("code/recoveryEnabled"), true).toBool();
}

QJsonObject StudioSettings::languageServerPreferences() const
{
	return QJsonDocument::fromJson(m_settings->value(QStringLiteral("code/languageServer")).toByteArray()).object();
}

void StudioSettings::setLanguageServerPreferences(const QJsonObject& preferences)
{
	writeValue(QStringLiteral("code/languageServer"), QJsonDocument(preferences).toJson(QJsonDocument::Compact));
}

bool StudioSettings::audioRecoveryEnabled() const
{
	return m_settings->value(QStringLiteral("audio/recoveryEnabled"), true).toBool();
}

void StudioSettings::setAudioRecoveryEnabled(bool enabled)
{
	writeValue(QStringLiteral("audio/recoveryEnabled"), enabled);
}

bool StudioSettings::audioRecoveryNotifyAtStartup() const
{
	return m_settings->value(QStringLiteral("audio/recoveryNotifyAtStartup"), true).toBool();
}

void StudioSettings::setAudioRecoveryNotifyAtStartup(bool enabled)
{
	writeValue(QStringLiteral("audio/recoveryNotifyAtStartup"), enabled);
}

void StudioSettings::setCodeRecoveryEnabled(bool enabled)
{
	writeValue(QStringLiteral("code/recoveryEnabled"), enabled);
}

bool StudioSettings::modelRecoveryEnabled() const
{
	return m_settings->value(QStringLiteral("model/recoveryEnabled"), true).toBool();
}

void StudioSettings::setModelRecoveryEnabled(bool enabled)
{
	writeValue(QStringLiteral("model/recoveryEnabled"), enabled);
}

QStringList StudioSettings::packagePublicationDirectories() const
{
	QStringList paths;
	for (const auto& path : m_settings->value(QStringLiteral("packages/publicationDirectories")).toStringList()) {
		if (!QDir::isAbsolutePath(path)) { continue; }
		const QString cleaned = QDir::cleanPath(path);
		if (std::none_of(paths.begin(), paths.end(), [&](const auto& existing) { return sameProjectPath(existing, cleaned); })) { paths << cleaned; }
		if (paths.size() == 16) { break; }
	}
	return paths;
}

void StudioSettings::rememberPackagePublicationDirectory(const QString& path)
{
	if (path.trimmed().isEmpty()) { return; }
	const QString cleaned = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	auto paths = packagePublicationDirectories();
	paths.erase(std::remove_if(paths.begin(), paths.end(), [&](const auto& existing) { return sameProjectPath(existing, cleaned); }), paths.end());
	paths.prepend(cleaned); while (paths.size() > 16) { paths.removeLast(); }
	writeValue(QStringLiteral("packages/publicationDirectories"), paths);
	// Flush before starting a save so crash recovery can find a new output folder.
	sync();
}

bool StudioSettings::packageRecoveryEnabled() const
{
	return m_settings->value(QStringLiteral("packages/recoveryEnabled"), true).toBool();
}

void StudioSettings::setPackageRecoveryEnabled(bool enabled)
{
	writeValue(QStringLiteral("packages/recoveryEnabled"), enabled);
}

int StudioSettings::packageRecoveryIntervalSeconds() const
{
	return std::clamp(m_settings->value(QStringLiteral("packages/recoveryIntervalSeconds"), 30).toInt(), 5, 600);
}

void StudioSettings::setPackageRecoveryIntervalSeconds(int seconds)
{
	writeValue(QStringLiteral("packages/recoveryIntervalSeconds"), std::clamp(seconds, 5, 600));
}

int StudioSettings::packageRecoveryMaximumMiB() const
{
	return std::clamp(m_settings->value(QStringLiteral("packages/recoveryMaximumMiB"), 8192).toInt(), 128, 131072);
}

void StudioSettings::setPackageRecoveryMaximumMiB(int mib)
{
	writeValue(QStringLiteral("packages/recoveryMaximumMiB"), std::clamp(mib, 128, 131072));
}

int StudioSettings::packageRecoveryMaximumCopies() const
{
	return std::clamp(m_settings->value(QStringLiteral("packages/recoveryMaximumCopies"), 32).toInt(), 1, 128);
}

void StudioSettings::setPackageRecoveryMaximumCopies(int copies)
{
	writeValue(QStringLiteral("packages/recoveryMaximumCopies"), std::clamp(copies, 1, 128));
}

int StudioSettings::packageImportMaximumMiB() const
{
	return std::clamp(m_settings->value(QStringLiteral("packages/importMaximumMiB"), 8192).toInt(), 128, 131072);
}

void StudioSettings::setPackageImportMaximumMiB(int mib)
{
	writeValue(QStringLiteral("packages/importMaximumMiB"), std::clamp(mib, 128, 131072));
}

int StudioSettings::packageImportMaximumFiles() const
{
	return std::clamp(m_settings->value(QStringLiteral("packages/importMaximumFiles"), 50000).toInt(), 1, 100000);
}

void StudioSettings::setPackageImportMaximumFiles(int files)
{
	writeValue(QStringLiteral("packages/importMaximumFiles"), std::clamp(files, 1, 100000));
}

int StudioSettings::packageDraftMaximumMiB() const
{
	return std::clamp(m_settings->value(QStringLiteral("packages/draftMaximumMiB"), 32768).toInt(), 128, 131072);
}

void StudioSettings::setPackageDraftMaximumMiB(int mib)
{
	writeValue(QStringLiteral("packages/draftMaximumMiB"), std::clamp(mib, 128, 131072));
}

int StudioSettings::packageDraftMaximumFiles() const
{
	return std::clamp(m_settings->value(QStringLiteral("packages/draftMaximumFiles"), 200000).toInt(), 1, PackageStorageEntryLimit);
}

void StudioSettings::setPackageDraftMaximumFiles(int files)
{
	writeValue(QStringLiteral("packages/draftMaximumFiles"), std::clamp(files, 1, PackageStorageEntryLimit));
}

PackageDraftSaveLimits StudioSettings::packageDraftSaveLimits() const
{
	return {static_cast<qint64>(packageDraftMaximumMiB()) * 1024 * 1024, packageDraftMaximumFiles()};
}

PackageCopyLimits StudioSettings::packageCopyLimits() const
{
	return {static_cast<quint64>(std::clamp(m_settings->value(QStringLiteral("packages/copyMaximumMiB"), 2048).toInt(), 1, PackageCopyMaximumMiB)) * 1024 * 1024,
		std::clamp<qsizetype>(m_settings->value(QStringLiteral("packages/copyMaximumFiles"), 8000).toLongLong(), 1, PackageCopyMaximumFiles),
		std::clamp<qsizetype>(m_settings->value(QStringLiteral("packages/copyMaximumEntries"), 40000).toLongLong(), 1, PackageCopyMaximumEntries),
		std::clamp(m_settings->value(QStringLiteral("packages/copyMaximumBatches"), 64).toInt(), 1, PackageCopyMaximumBatches)};
}

void StudioSettings::setPackageCopyLimits(const PackageCopyLimits& limits)
{
	writeValue(QStringLiteral("packages/copyMaximumMiB"), static_cast<int>(std::clamp<quint64>(limits.maximumBytes / (1024 * 1024), 1, PackageCopyMaximumMiB)));
	writeValue(QStringLiteral("packages/copyMaximumFiles"), static_cast<qint64>(std::clamp<qsizetype>(limits.maximumFiles, 1, PackageCopyMaximumFiles)));
	writeValue(QStringLiteral("packages/copyMaximumEntries"), static_cast<qint64>(std::clamp<qsizetype>(limits.maximumEntries, 1, PackageCopyMaximumEntries)));
	writeValue(QStringLiteral("packages/copyMaximumBatches"), std::clamp(limits.maximumBatches, 1, PackageCopyMaximumBatches));
}

bool StudioSettings::textureRecoveryEnabled() const
{
	return m_settings->value(QStringLiteral("textures/recoveryEnabled"), true).toBool();
}

void StudioSettings::setTextureRecoveryEnabled(bool enabled)
{
	writeValue(QStringLiteral("textures/recoveryEnabled"), enabled);
}

int StudioSettings::textureRecoveryIntervalSeconds() const
{
	return std::clamp(m_settings->value(QStringLiteral("textures/recoveryIntervalSeconds"), 30).toInt(), 5, 600);
}

void StudioSettings::setTextureRecoveryIntervalSeconds(int seconds)
{
	writeValue(QStringLiteral("textures/recoveryIntervalSeconds"), std::clamp(seconds, 5, 600));
}

void StudioSettings::setCrashReports(bool enabled)
{
	writeValue(kCrashReportsKey, enabled);
}

int StudioSettings::codeZoomPercent() const
{
	return std::clamp(m_settings->value(kCodeZoomPercentKey, 100).toInt(), 50, 300);
}

void StudioSettings::setCodeZoomPercent(int percent)
{
	writeValue(kCodeZoomPercentKey, std::clamp(percent, 50, 300));
}

bool StudioSettings::codeStickyHeaders() const
{
	return m_settings->value(kCodeStickyHeadersKey, true).toBool();
}

void StudioSettings::setCodeStickyHeaders(bool enabled)
{
	writeValue(kCodeStickyHeadersKey, enabled);
}

QHash<QString, QStringList> StudioSettings::userShortcuts() const
{
	QHash<QString, QStringList> shortcuts;
	m_settings->beginGroup(QString::fromLatin1(kUserShortcutsGroup));
	const QStringList commandIds = m_settings->childKeys();
	for (const QString& commandId : commandIds) {
		// One sequence per line: portable key text never holds a line break,
		// and an empty value is a command left without keys.
		shortcuts.insert(commandId, m_settings->value(commandId).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts));
	}
	m_settings->endGroup();
	return shortcuts;
}

void StudioSettings::setUserShortcuts(const QHash<QString, QStringList>& shortcuts)
{
	removeKey(QString::fromLatin1(kUserShortcutsGroup));
	for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it) {
		if (!it.key().trimmed().isEmpty()) {
			writeValue(QStringLiteral("%1/%2").arg(QString::fromLatin1(kUserShortcutsGroup), it.key()), it.value().join(QLatin1Char('\n')));
		}
	}
}

StudioSession StudioSettings::lastSession() const
{
	StudioSession session;
	session.packagePath = m_settings->value(kSessionPackageKey).toString();
	session.mapPath = m_settings->value(kSessionMapKey).toString();
	session.mapName = m_settings->value(kSessionMapNameKey).toString();
	session.codeFiles = m_settings->value(kSessionCodeFilesKey).toStringList();
	session.currentCodeFile = m_settings->value(kSessionCurrentCodeFileKey).toString();
	session.ownerProcessId = m_settings->value(kSessionOwnerKey, 0).toLongLong();
	return session;
}

void StudioSettings::setLastSession(const StudioSession& session)
{
	writeValue(kSessionPackageKey, session.packagePath);
	writeValue(kSessionMapKey, session.mapPath);
	writeValue(kSessionMapNameKey, session.mapName);
	writeValue(kSessionCodeFilesKey, session.codeFiles);
	writeValue(kSessionCurrentCodeFileKey, session.currentCodeFile);
	writeValue(kSessionOwnerKey, session.ownerProcessId);
}

QString StudioSettings::shellModeRailBehaviour() const
{
	// The rail folds to icons unless the user pinned it open or chose icons
	// only. The old show-or-fold switch (shell/modeRailCompact) is left
	// behind: either of its answers meant "not pinned open".
	const QString stored = m_settings->value(kShellModeRailBehaviourKey).toString().trimmed().toLower();
	if (stored == QLatin1String("expanded") || stored == QLatin1String("compact")) {
		return stored;
	}
	return QStringLiteral("automatic");
}

void StudioSettings::setShellModeRailBehaviour(const QString& behaviour)
{
	const QString normalized = behaviour.trimmed().toLower();
	writeValue(kShellModeRailBehaviourKey,
		normalized == QLatin1String("expanded") || normalized == QLatin1String("compact") ? normalized : QStringLiteral("automatic"));
}

QByteArray StudioSettings::shellLayoutState(const QString& key) const
{
	const QString trimmed = key.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	return m_settings->value(QString::fromLatin1(kShellLayoutPrefix) + trimmed).toByteArray();
}

void StudioSettings::setShellLayoutState(const QString& key, const QByteArray& state)
{
	const QString trimmed = key.trimmed();
	if (trimmed.isEmpty()) {
		return;
	}
	writeValue(QString::fromLatin1(kShellLayoutPrefix) + trimmed, state);
}

void StudioSettings::ensureSchema()
{
	// A store with no version key is either brand new (nothing to migrate) or a
	// version 1 store written before the key existed.
	int storedVersion = kSchemaVersion;
	if (m_settings->contains(kSchemaVersionKey)) {
		bool ok = false;
		const int value = m_settings->value(kSchemaVersionKey).toInt(&ok);
		storedVersion = ok ? value : 1;
		if (!ok) {
			m_migrationNotes.push_back(QStringLiteral("Unreadable schema version; assuming version 1."));
		}
	} else if (!m_settings->allKeys().isEmpty()) {
		storedVersion = 1;
		m_migrationNotes.push_back(QStringLiteral("No schema version stored; assuming version 1."));
	} else {
		if (!m_readOnly) { writeValue(kSchemaVersionKey, kSchemaVersion); }
		return;
	}

	if (storedVersion > kSchemaVersion) {
		// Never reinterpret a newer store: unknown keys would be silently
		// dropped or misread. Report it and stop writing instead.
		m_storedSchemaIsNewer = true;
		m_readOnly = true;
		m_migrationNotes.push_back(QStringLiteral("Settings schema version %1 is newer than this build's %2; the store is opened read-only.")
			.arg(storedVersion)
			.arg(kSchemaVersion));
		qWarning("VibeStudio settings at %s use schema version %d, newer than the supported version %d; no settings will be written.",
			qUtf8Printable(m_settings->fileName()),
			storedVersion,
			kSchemaVersion);
		return;
	}

	if (storedVersion < kSchemaVersion) {
		if (!m_readOnly) { runMigrations(storedVersion); }
		else { m_migrationNotes.push_back(QCoreApplication::translate("StudioSettings", "Settings schema %1 inspected read-only; migration to %2 was skipped.").arg(storedVersion).arg(kSchemaVersion)); }
	}
}

void StudioSettings::runMigrations(int storedVersion)
{
	// Ordered, idempotent steps. Each one ends by stamping the version it
	// produced so an interrupted upgrade resumes from the right place.
	for (int version = storedVersion; version < kSchemaVersion; ++version) {
		switch (version) {
		case 1: {
			// 1 -> 2: normalise the stored preference ids so lookups no longer
			// depend on the exact casing/separator a previous build wrote, then
			// drop keys nothing reads any more.
			if (m_settings->contains(kLocaleNameKey)) {
				writeValue(kLocaleNameKey, normalizedLocaleName(m_settings->value(kLocaleNameKey).toString()));
			}
			const bool legacyHighContrast = m_settings->value(QStringLiteral("preferences/highContrast"), false).toBool();
			if (m_settings->contains(kThemeKey)) {
				const QString storedTheme = m_settings->value(kThemeKey).toString();
				StudioTheme theme = themeFromId(storedTheme);
				if (legacyHighContrast && theme == StudioTheme::Dark) {
					theme = StudioTheme::HighContrastDark;
				} else if (legacyHighContrast && theme == StudioTheme::Light) {
					theme = StudioTheme::HighContrastLight;
				}
				writeValue(kThemeKey, themeId(theme));
			} else if (legacyHighContrast) {
				writeValue(kThemeKey, themeId(StudioTheme::HighContrastDark));
			}
			if (m_settings->contains(kDensityKey)) {
				writeValue(kDensityKey, densityId(densityFromId(m_settings->value(kDensityKey).toString())));
			}
			if (m_settings->contains(kTextScalePercentKey)) {
				writeValue(kTextScalePercentKey, normalizedTextScalePercent(m_settings->value(kTextScalePercentKey).toInt()));
			}
			int droppedKeys = 0;
			for (const QString& key : retiredV1Keys()) {
				if (m_settings->contains(key)) {
					removeKey(key);
					++droppedKeys;
				}
			}
			m_migrationNotes.push_back(QStringLiteral("Migrated settings schema 1 -> 2: normalized theme/density/locale ids, dropped %1 unused key(s).")
				.arg(droppedKeys));
			break;
		}
		default:
			m_migrationNotes.push_back(QStringLiteral("No migration step for schema version %1.").arg(version));
			break;
		}
		writeValue(kSchemaVersionKey, version + 1);
	}
}

bool StudioSettings::storedSchemaIsNewer() const
{
	return m_storedSchemaIsNewer;
}

bool StudioSettings::isReadOnly() const
{
	return m_readOnly;
}

QStringList StudioSettings::migrationNotes() const
{
	return m_migrationNotes;
}

void StudioSettings::writeValue(const QString& key, const QVariant& value)
{
	if (m_readOnly) {
		++m_discardedWrites;
		return;
	}
	m_settings->setValue(key, value);
}

void StudioSettings::removeKey(const QString& key)
{
	if (m_readOnly) {
		++m_discardedWrites;
		return;
	}
	m_settings->remove(key);
}

void StudioSettings::writeRecentProjects(const QVector<RecentProject>& projects)
{
	if (m_readOnly) {
		++m_discardedWrites;
		return;
	}

	const int count = std::min(static_cast<int>(projects.size()), kMaximumRecentProjects);
	m_settings->beginWriteArray(kRecentProjectsArray, count);
	for (int index = 0; index < count; ++index) {
		const RecentProject& project = projects[index];
		m_settings->setArrayIndex(index);
		writeValue(kRecentProjectPathKey, project.path);
		writeValue(kRecentProjectDisplayNameKey, recentProjectDisplayName(project.path, project.displayName));
		writeValue(kRecentProjectLastOpenedKey, normalizedTimestamp(project.lastOpenedUtc));
	}
	m_settings->endArray();
}

void StudioSettings::writeRecentActivityTasks(const QVector<RecentActivityTask>& tasks)
{
	if (m_readOnly) {
		++m_discardedWrites;
		return;
	}

	const int count = std::min(static_cast<int>(tasks.size()), kMaximumRecentActivityTasks);
	m_settings->beginWriteArray(kRecentActivityTasksArray, count);
	for (int index = 0; index < count; ++index) {
		RecentActivityTask task = boundedActivityTask(tasks[index]);
		task.id = task.id.trimmed();
		task.title = task.title.trimmed();
		task.detail = task.detail.trimmed();
		task.source = task.source.trimmed();
		task.resultSummary = task.resultSummary.trimmed();
		task.createdUtc = normalizedTimestamp(task.createdUtc);
		task.updatedUtc = normalizedTimestamp(task.updatedUtc);
		if (!task.finishedUtc.isValid() && operationStateIsTerminal(task.state)) {
			task.finishedUtc = task.updatedUtc;
		}

		m_settings->setArrayIndex(index);
		writeValue(kRecentActivityTaskIdKey, task.id);
		writeValue(kRecentActivityTaskTitleKey, task.title);
		writeValue(kRecentActivityTaskDetailKey, task.detail);
		writeValue(kRecentActivityTaskSourceKey, task.source);
		writeValue(kRecentActivityTaskStateKey, operationStateId(task.state));
		writeValue(kRecentActivityTaskResultKey, task.resultSummary);
		writeValue(kRecentActivityTaskWarningsKey, task.warnings);
		writeValue(kRecentActivityTaskCreatedKey, task.createdUtc);
		writeValue(kRecentActivityTaskUpdatedKey, task.updatedUtc);
		writeValue(kRecentActivityTaskFinishedKey, task.finishedUtc);
		writeValue(kRecentActivityTaskProgressCurrentKey, task.progress.current);
		writeValue(kRecentActivityTaskProgressTotalKey, task.progress.total);
		writeValue(kRecentActivityTaskCancellableKey, task.cancellable);
		writeValue(kRecentActivityTaskDurationMsKey, task.durationMs);
		writeValue(kRecentActivityTaskLogTruncatedKey, task.logTruncated);
		writeValue(kRecentActivityTaskLogDroppedKey, task.droppedLogEntryCount);

		QJsonArray logJson;
		for (const OperationLogEntry& entry : std::as_const(task.log)) {
			logJson.append(operationLogEntryToJson(entry));
		}
		writeValue(kRecentActivityTaskLogKey, encodeJsonArray(logJson));

		QJsonArray transitionsJson;
		for (const OperationStateTransition& transition : std::as_const(task.transitions)) {
			transitionsJson.append(operationTransitionToJson(transition));
		}
		writeValue(kRecentActivityTaskTransitionsKey, encodeJsonArray(transitionsJson));
	}
	m_settings->endArray();
}

void StudioSettings::writeGameInstallations(const QVector<GameInstallationProfile>& profiles)
{
	if (m_readOnly) {
		++m_discardedWrites;
		return;
	}

	const int count = std::min(static_cast<int>(profiles.size()), kMaximumGameInstallationProfiles);
	m_settings->beginWriteArray(kGameInstallationsArray, count);
	for (int index = 0; index < count; ++index) {
		const GameInstallationProfile profile = normalizedGameInstallationProfile(profiles[index]);
		m_settings->setArrayIndex(index);
		writeValue(kGameInstallationIdKey, profile.id);
		writeValue(kGameInstallationGameKey, profile.gameKey);
		writeValue(kGameInstallationEngineFamilyKey, gameEngineFamilyId(profile.engineFamily));
		writeValue(kGameInstallationDisplayNameKey, profile.displayName);
		writeValue(kGameInstallationRootPathKey, profile.rootPath);
		writeValue(kGameInstallationExecutablePathKey, profile.executablePath);
		writeValue(kGameInstallationBasePackagePathsKey, profile.basePackagePaths);
		writeValue(kGameInstallationModPackagePathsKey, profile.modPackagePaths);
		writeValue(kGameInstallationPaletteIdKey, profile.paletteId);
		writeValue(kGameInstallationCompilerProfileIdKey, profile.compilerProfileId);
		writeValue(kGameInstallationReadOnlyKey, profile.readOnly);
		writeValue(kGameInstallationActiveKey, profile.active);
		writeValue(kGameInstallationHiddenKey, profile.hidden);
		writeValue(kGameInstallationManualKey, profile.manual);
		writeValue(kGameInstallationCreatedUtcKey, normalizedTimestamp(profile.createdUtc));
		writeValue(kGameInstallationUpdatedUtcKey, normalizedTimestamp(profile.updatedUtc));
	}
	m_settings->endArray();
}

void StudioSettings::writeCompilerToolPathOverrides(const QVector<CompilerToolPathOverride>& overrides)
{
	if (m_readOnly) {
		++m_discardedWrites;
		return;
	}

	const int count = std::min(static_cast<int>(overrides.size()), kMaximumCompilerToolPathOverrides);
	m_settings->beginWriteArray(kCompilerToolPathOverridesArray, count);
	for (int index = 0; index < count; ++index) {
		m_settings->setArrayIndex(index);
		writeValue(kCompilerToolPathOverrideToolIdKey, normalizedId(overrides[index].toolId));
		writeValue(kCompilerToolPathOverrideExecutablePathKey, normalizedProjectPath(overrides[index].executablePath));
	}
	m_settings->endArray();
}

RecentActivityTask recentActivityTaskFromOperationTask(const OperationTask& task)
{
	RecentActivityTask record;
	record.id = task.id;
	record.title = task.title;
	record.detail = task.detail;
	record.source = task.source;
	record.state = task.state;
	record.progress = task.progress;
	record.resultSummary = task.resultSummary;
	record.warnings = task.warnings;
	record.log = task.log;
	record.transitions = task.transitions;
	record.cancellable = task.cancellable;
	record.createdUtc = task.createdUtc;
	record.updatedUtc = task.updatedUtc;
	record.finishedUtc = task.finishedUtc;
	record.durationMs = task.durationMs > 0 ? task.durationMs : operationTaskElapsedMs(task);
	return boundedActivityTask(record);
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
	return localizationTargetIds();
}

QString normalizedLocaleName(const QString& localeName)
{
	const QString requested = localeName.trimmed().replace('_', '-');
	// Nothing stored, or "system", follows the operating system's language.
	if (requested.isEmpty() || isSystemLocalizationPreference(requested)) {
		return systemLocalizationPreferenceId();
	}
	// Regional and legacy ids resolve to the written standard they use:
	// zh-TW to zh-Hant, es-MX to es-419, iw to he.
	LocalizationTarget target;
	if (localizationTargetForId(requested, &target)) {
		return target.localeName;
	}
	return QStringLiteral("en");
}

QString normalizedFormatLocaleName(const QString& formatLocaleName)
{
	return normalizedRegionFormatId(formatLocaleName);
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

QString colorVisionId(ColorVision vision)
{
	switch (vision) {
	case ColorVision::Typical:
		return QStringLiteral("typical");
	case ColorVision::RedGreen:
		return QStringLiteral("red-green");
	case ColorVision::BlueYellow:
		return QStringLiteral("blue-yellow");
	case ColorVision::Monochrome:
		return QStringLiteral("monochrome");
	}
	return QStringLiteral("typical");
}

ColorVision colorVisionFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	// The deficiency names are accepted too, since that is what people search.
	if (normalized == QStringLiteral("red-green") || normalized == QStringLiteral("protanopia") || normalized == QStringLiteral("deuteranopia")) {
		return ColorVision::RedGreen;
	}
	if (normalized == QStringLiteral("blue-yellow") || normalized == QStringLiteral("tritanopia")) {
		return ColorVision::BlueYellow;
	}
	if (normalized == QStringLiteral("monochrome") || normalized == QStringLiteral("achromatopsia")) {
		return ColorVision::Monochrome;
	}
	return ColorVision::Typical;
}

QStringList colorVisionIds()
{
	return {
		colorVisionId(ColorVision::Typical),
		colorVisionId(ColorVision::RedGreen),
		colorVisionId(ColorVision::BlueYellow),
		colorVisionId(ColorVision::Monochrome),
	};
}

QString messageDurationId(MessageDuration duration)
{
	switch (duration) {
	case MessageDuration::Standard:
		return QStringLiteral("standard");
	case MessageDuration::Longer:
		return QStringLiteral("longer");
	case MessageDuration::UntilReplaced:
		return QStringLiteral("until-replaced");
	}
	return QStringLiteral("standard");
}

MessageDuration messageDurationFromId(const QString& id)
{
	const QString normalized = normalizedId(id);
	if (normalized == QStringLiteral("longer") || normalized == QStringLiteral("long")) {
		return MessageDuration::Longer;
	}
	if (normalized == QStringLiteral("until-replaced")) {
		return MessageDuration::UntilReplaced;
	}
	return MessageDuration::Standard;
}

QStringList messageDurationIds()
{
	return {
		messageDurationId(MessageDuration::Standard),
		messageDurationId(MessageDuration::Longer),
		messageDurationId(MessageDuration::UntilReplaced),
	};
}

QStringList speechEventIds()
{
	return {QStringLiteral("task-results"), QStringLiteral("task-problems"), QStringLiteral("status-messages")};
}

QStringList normalizedSpeechEvents(const QStringList& events)
{
	QStringList wanted;
	for (const QString& event : events) {
		wanted << normalizedId(event);
	}
	QStringList normalized;
	for (const QString& id : speechEventIds()) {
		if (wanted.contains(id)) {
			normalized << id;
		}
	}
	return normalized;
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
	case SetupStep::GameInstallations:
		return QStringLiteral("game-installations");
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
	// Title case as the Settings pages write it: short words stay lower case.
	switch (step) {
	case SetupStep::WelcomeAccess:
		return QCoreApplication::translate("VibeStudioSetup", "Welcome and Access");
	case SetupStep::WorkspaceProfile:
		return QCoreApplication::translate("VibeStudioSetup", "Workspace and Editor Profile");
	case SetupStep::ProjectsPackages:
		return QCoreApplication::translate("VibeStudioSetup", "Projects and Packages");
	case SetupStep::GameInstallations:
		return QCoreApplication::translate("VibeStudioSetup", "Game Installations");
	case SetupStep::Toolchains:
		return QCoreApplication::translate("VibeStudioSetup", "Toolchains");
	case SetupStep::AiAutomation:
		return QCoreApplication::translate("VibeStudioSetup", "AI and Automation");
	case SetupStep::CliIntegration:
		return QCoreApplication::translate("VibeStudioSetup", "CLI Integration");
	case SetupStep::ReviewFinish:
		return QCoreApplication::translate("VibeStudioSetup", "Review and Finish");
	}
	return QCoreApplication::translate("VibeStudioSetup", "Welcome and Access");
}

QString setupStepDescription(SetupStep step)
{
	switch (step) {
	case SetupStep::WelcomeAccess:
		return QCoreApplication::translate("VibeStudioSetup", "Review language, region formats, theme, colour vision, text scale, focus, motion, alerts, and TTS preferences.");
	case SetupStep::WorkspaceProfile:
		return QCoreApplication::translate("VibeStudioSetup", "Choose your role and the level editor profile whose keys and mouse controls feel familiar.");
	case SetupStep::ProjectsPackages:
		return QCoreApplication::translate("VibeStudioSetup", "Open a project folder, initialize a manifest, or defer project/package setup.");
	case SetupStep::GameInstallations:
		return QCoreApplication::translate("VibeStudioSetup", "Add, detect, select, skip, or revisit game installation profiles without modifying game files.");
	case SetupStep::Toolchains:
		return QCoreApplication::translate("VibeStudioSetup", "Prepare for compiler detection and command manifests.");
	case SetupStep::AiAutomation:
		return QCoreApplication::translate("VibeStudioSetup", "Keep AI disabled by default or plan connector setup later.");
	case SetupStep::CliIntegration:
		return QCoreApplication::translate("VibeStudioSetup", "Review CLI availability and scriptable settings reports.");
	case SetupStep::ReviewFinish:
		return QCoreApplication::translate("VibeStudioSetup", "Inspect skipped, complete, pending, and warning states before entering the workspace.");
	}
	return QCoreApplication::translate("VibeStudioSetup", "Review language, region formats, theme, colour vision, text scale, focus, motion, alerts, and TTS preferences.");
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
		SetupStep::GameInstallations,
		SetupStep::Toolchains,
		SetupStep::AiAutomation,
		SetupStep::CliIntegration,
		SetupStep::ReviewFinish,
	};
}

} // namespace vibestudio
