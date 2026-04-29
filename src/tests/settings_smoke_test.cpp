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

	settings.recordRecentProject(alphaProject, QStringLiteral("Alpha"), QDateTime::fromString(QStringLiteral("2026-04-29T08:00:00Z"), Qt::ISODate));
	settings.recordRecentProject(betaProject, QString(), QDateTime::fromString(QStringLiteral("2026-04-29T09:00:00Z"), Qt::ISODate));
	settings.recordRecentProject(alphaProject, QStringLiteral("Alpha Renamed"), QDateTime::fromString(QStringLiteral("2026-04-29T10:00:00Z"), Qt::ISODate));
	settings.setSelectedMode(5);
	settings.setShellGeometry(QByteArray("geometry-bytes"));
	settings.setShellWindowState(QByteArray("state-bytes"));
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
