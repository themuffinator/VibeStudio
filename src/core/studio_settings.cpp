#include "core/studio_settings.h"

#include "core/editor_profiles.h"
#include "core/localization.h"

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
constexpr auto kLocaleNameKey = "preferences/localeName";
constexpr auto kTextScalePercentKey = "preferences/textScalePercent";
constexpr auto kThemeKey = "preferences/theme";
constexpr auto kDensityKey = "preferences/density";
constexpr auto kReducedMotionKey = "preferences/reducedMotion";
constexpr auto kTextToSpeechEnabledKey = "preferences/textToSpeechEnabled";
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

StudioSettings::StudioSettings()
	: m_settings(overrideFilePath().isEmpty()
		? std::make_unique<QSettings>()
		: std::make_unique<QSettings>(overrideFilePath(), QSettings::IniFormat))
{
	ensureSchema();
}

StudioSettings::StudioSettings(const QString& filePath)
	: m_settings(std::make_unique<QSettings>(filePath, QSettings::IniFormat))
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

	if (preferences.localeName == QStringLiteral("en")) {
		summary.warnings.push_back(QStringLiteral("Language is still the default English fallback."));
	}
	if (preferences.theme == StudioTheme::Dark && preferences.textScalePercent == 100 && preferences.density == UiDensity::Standard && !preferences.reducedMotion && !preferences.textToSpeechEnabled) {
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
		writeValue(kSchemaVersionKey, kSchemaVersion);
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
		runMigrations(storedVersion);
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
	switch (step) {
	case SetupStep::WelcomeAccess:
		return QStringLiteral("Welcome And Access");
	case SetupStep::WorkspaceProfile:
		return QStringLiteral("Workspace Profile");
	case SetupStep::ProjectsPackages:
		return QStringLiteral("Projects And Packages");
	case SetupStep::GameInstallations:
		return QStringLiteral("Game Installations");
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
		return QStringLiteral("Open a project folder, initialize a manifest, or defer project/package setup.");
	case SetupStep::GameInstallations:
		return QStringLiteral("Add, detect, select, skip, or revisit game installation profiles without modifying game files.");
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
		SetupStep::GameInstallations,
		SetupStep::Toolchains,
		SetupStep::AiAutomation,
		SetupStep::CliIntegration,
		SetupStep::ReviewFinish,
	};
}

} // namespace vibestudio
