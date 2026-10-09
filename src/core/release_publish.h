#pragma once

// Publishing a planned release.
//
// One release folder holds what an author uploads: the package (PK3, PAK,
// merged Doom WAD, or a ZIP of loose game files), a plain-text readme for
// players, RELEASE_NOTES.md for release pages, any files that must sit beside
// the package, and a distribution ZIP of all of it in the layout players
// extract. The project keeps a release record for the next "what changed",
// and its changelog's Unreleased section becomes the new version.
//
// Every file goes through the shared atomic publisher: nothing appears half
// written, and replacing an earlier attempt keeps a verified .bak. Project
// files are verified when staged and again while the package is written, so
// a file changed mid-release fails the release instead of shipping a mix.

#include "core/deflate.h"
#include "core/project_manifest.h"
#include "core/release_notes.h"
#include "core/release_plan.h"

#include <QDate>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <functional>

namespace vibestudio {

struct ReleasePublishRequest {
	ReleasePlan plan;
	// The project folder, for its release record and changelog. May be empty.
	QString projectRoot;
	// Absolute folder for this release's files.
	QString outputDirectory;
	// Written as RELEASE_NOTES.md; empty skips it.
	QString notesMarkdown;
	// Written as <package name>.txt and placed in the distribution; empty skips it.
	QString readmeText;
	// <package name>-<version>.zip: the package, readme and loose files in the
	// layout players extract. A ZIP-format release is already that archive.
	bool writeArchive = true;
	bool writeRecord = true;
	// Adds these entries to the changelog's Unreleased section, then releases
	// it as the plan's version and saves the changelog.
	bool updateChangelog = false;
	ProjectChangelog changelog;
	QVector<ChangelogEntry> changelogAdditions;
	QDate releaseDate;
	DeflateLevel compression = DeflateLevel::Default;
	// Replace an earlier release of the same version, keeping .bak copies.
	bool overwrite = false;
	bool dryRun = false;
	std::function<bool()> isCancelled;
	// Phase text and a completed/total count (bytes when known).
	std::function<void(const QString& phase, qint64 completed, qint64 total)> progress;
};

struct ReleasePublishResult {
	bool succeeded = false;
	bool cancelled = false;
	bool dryRun = false;
	// True once the package itself was written, even if a later step failed.
	bool packageCommitted = false;
	QString outputDirectory;
	QString packagePath;
	QString packageSha256;
	quint64 packageBytes = 0;
	QString readmePath;
	QString notesPath;
	QString archivePath;
	QString archiveSha256;
	quint64 archiveBytes = 0;
	QString recordPath;
	QString changelogPath;
	QStringList loosePaths;
	QStringList backupPaths;
	QVector<ReleaseRecordFile> inventory;
	QStringList warnings;
	QString error;
};

// <project>/<release output folder>/<package name>-<version>
QString defaultReleaseOutputDirectory(const ProjectManifest& manifest, const ProjectReleaseSettings& release);
// The distribution archive's file name: <package name>-<version>.zip.
QString releaseArchiveFileName(const ReleasePlan& plan);
// Why an output folder cannot hold this release, or empty when it can.
QString releaseOutputProblem(const ReleasePlan& plan, const QString& outputDirectory, bool overwrite);

ReleasePublishResult publishRelease(const ReleasePublishRequest& request);
QJsonObject releasePublishResultJson(const ReleasePublishResult& result);
QString releasePublishResultText(const ReleasePublishResult& result);

} // namespace vibestudio
