#pragma once

#include "core/compiler_registry.h"
#include "core/operation_state.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct GameInstallationProfile;

// Content a release needs but does not ship, such as a mission pack or another
// mod. Files under `path` count as provided, like the game's own packages.
struct ProjectReleaseRequirement {
	QString name;
	// A folder or package, absolute or relative to the installation root.
	QString path;
	// Where players get it, for the release notes.
	QString url;
};

// How the project is packaged and described when released. Every field is
// optional; empty values fall back to the project and game defaults.
struct ProjectReleaseSettings {
	QString title;
	QString version;
	QStringList authors;
	QString description;
	QString website;
	QString license;
	// Package file name without extension; defaults to a slug of the title.
	QString packageName;
	// pk3, pak, wad or zip; defaults to the game's usual format.
	QString packageFormat;
	// The folder players install into (baseq3, id1, or a mod folder).
	QString gameFolder;
	// Asset register source ids counted as the game's own files. Empty uses
	// the register's base sources.
	QStringList stockSources;
	QVector<ProjectReleaseRequirement> requirements;
	// Glob patterns (project-relative, '/' separators) always shipped, and never shipped.
	QStringList include;
	QStringList exclude;
	QString outputFolder;
	QString changelogFile;
	bool includeSources = false;
	// Unknown keys of the release object, written back unchanged.
	QJsonObject extraFields;

	[[nodiscard]] bool isEmpty() const;
};

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
	// Schema history:
	//   1  folders, installation, compilers and settings overrides
	//   2  adds `game`, the `release` object, and keeps unknown top-level keys
	static constexpr int kSchemaVersion = 2;

	int schemaVersion = kSchemaVersion;
	QString projectId;
	QString displayName;
	QString rootPath;
	// The game the project targets (quake, quake2, quake3, doom...). Empty
	// means "the game of the linked installation".
	QString gameKey;
	QStringList sourceFolders;
	QStringList packageFolders;
	QString outputFolder;
	QString tempFolder;
	QString selectedInstallationId;
	QStringList compilerSearchPaths;
	QVector<CompilerToolPathOverride> compilerToolOverrides;
	QStringList registeredOutputPaths;
	ProjectSettingsOverride settingsOverrides;
	ProjectReleaseSettings release;
	QDateTime createdUtc;
	QDateTime updatedUtc;
	// Top-level keys this version does not know, written back unchanged so a
	// newer studio's settings survive an older one saving the manifest.
	QJsonObject extraFields;
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
// The manifest's game, else the given installation's, else "custom".
QString effectiveProjectGameKey(const ProjectManifest& manifest, const GameInstallationProfile* installation = nullptr);
// Release settings with the defaults filled in: title, version, package name,
// game folder, output folder and changelog file. The package format stays
// empty unless the project chose one, so the planner can pick per scope.
ProjectReleaseSettings effectiveProjectReleaseSettings(const ProjectManifest& manifest, const QString& gameKey);
// The default package format id for a game: pk3 for Quake III, pak for Quake
// and Quake II, wad for the Doom family, zip otherwise.
QString defaultReleasePackageFormat(const QString& gameKey);
// A file-name-safe slug: lower case letters, digits, '-' and '_'.
QString releaseSlug(const QString& text);
QStringList effectiveProjectCompilerSearchPaths(const ProjectManifest& manifest, const QStringList& fallbackSearchPaths = {});
QVector<CompilerToolPathOverride> effectiveProjectCompilerToolOverrides(const ProjectManifest& manifest, const QVector<CompilerToolPathOverride>& fallbackOverrides = {});
bool registerProjectOutputPath(ProjectManifest* manifest, const QString& outputPath);
int registerProjectOutputPaths(ProjectManifest* manifest, const QStringList& outputPaths);
ProjectHealthSummary buildProjectHealthSummary(const ProjectManifest& manifest, const QString& fallbackInstallationId = QString());
QString projectManifestToText(const ProjectManifest& manifest);

} // namespace vibestudio
