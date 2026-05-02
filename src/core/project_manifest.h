#pragma once

#include "core/compiler_registry.h"
#include "core/operation_state.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct ProjectSettingsOverride {
	QString selectedInstallationId;
	QString editorProfileId;
	QString paletteId;
	QString compilerProfileId;
	bool aiFreeModeSet = false;
	bool aiFreeMode = false;

	[[nodiscard]] bool isEmpty() const;
};

struct ProjectManifest {
	static constexpr int kSchemaVersion = 1;

	int schemaVersion = kSchemaVersion;
	QString projectId;
	QString displayName;
	QString rootPath;
	QStringList sourceFolders;
	QStringList packageFolders;
	QString outputFolder;
	QString tempFolder;
	QString selectedInstallationId;
	QStringList compilerSearchPaths;
	QVector<CompilerToolPathOverride> compilerToolOverrides;
	QStringList registeredOutputPaths;
	ProjectSettingsOverride settingsOverrides;
	QDateTime createdUtc;
	QDateTime updatedUtc;
};

struct ProjectHealthCheck {
	QString id;
	QString title;
	QString detail;
	OperationState state = OperationState::Idle;
};

struct ProjectHealthSummary {
	QString title;
	QString detail;
	QVector<ProjectHealthCheck> checks;
	int readyCount = 0;
	int warningCount = 0;
	int failedCount = 0;

	[[nodiscard]] OperationState overallState() const;
};

QString projectManifestDirectoryName();
QString projectManifestFileName();
QString projectManifestPath(const QString& projectRootPath);
QString normalizedProjectRootPath(const QString& path);
QString defaultProjectId(const QString& projectRootPath);
ProjectManifest defaultProjectManifest(const QString& projectRootPath, const QString& displayName = QString());
bool loadProjectManifest(const QString& projectRootPath, ProjectManifest* manifest, QString* error = nullptr);
bool saveProjectManifest(const ProjectManifest& manifest, QString* error = nullptr);
QString effectiveProjectInstallationId(const ProjectManifest& manifest, const QString& fallbackInstallationId = QString());
QString effectiveProjectEditorProfileId(const ProjectManifest& manifest, const QString& fallbackEditorProfileId = QString());
QString effectiveProjectPaletteId(const ProjectManifest& manifest, const QString& fallbackPaletteId = QString());
QString effectiveProjectCompilerProfileId(const ProjectManifest& manifest, const QString& fallbackCompilerProfileId = QString());
bool effectiveProjectAiFreeMode(const ProjectManifest& manifest, bool fallbackAiFreeMode = true);
QStringList effectiveProjectCompilerSearchPaths(const ProjectManifest& manifest, const QStringList& fallbackSearchPaths = {});
QVector<CompilerToolPathOverride> effectiveProjectCompilerToolOverrides(const ProjectManifest& manifest, const QVector<CompilerToolPathOverride>& fallbackOverrides = {});
bool registerProjectOutputPath(ProjectManifest* manifest, const QString& outputPath);
int registerProjectOutputPaths(ProjectManifest* manifest, const QStringList& outputPaths);
ProjectHealthSummary buildProjectHealthSummary(const ProjectManifest& manifest, const QString& fallbackInstallationId = QString());
QString projectManifestToText(const ProjectManifest& manifest);

} // namespace vibestudio
