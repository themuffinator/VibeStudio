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

} // namespace vibestudio
