#pragma once

// Release notes for projects.
//
// - The project changelog is a Keep a Changelog Markdown file the author keeps
//   (https://keepachangelog.com/en/1.1.0/): an "Unreleased" section collects
//   changes as they happen, and publishing a release moves them under the new
//   version. Text the parser does not understand is kept exactly as written.
// - Generated notes describe one release: what is new, what it contains, what
//   it needs, and how to install and play it. A Markdown file suits stores and
//   repositories; the plain-text readme follows the field layout players know
//   from the idgames archive and the Quake map archives.
// - Release records (.vibestudio/releases/<version>.json) keep each published
//   inventory, so the next release can say what changed since.

#include "core/project_manifest.h"
#include "core/release_plan.h"

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <optional>

namespace vibestudio {

// Keep a Changelog's categories, in its order.
QStringList changelogCategories();
// Placeholders generated notes use until the package exists; publishRelease
// replaces them with the written package's SHA-256 and size.
QString releasePackageHashToken();
QString releasePackageSizeToken();
QString fillReleasePackageTokens(QString text, const QString& sha256, quint64 bytes);
// The category id for a word such as "fix" or "added"; empty when unknown.
QString normalizedChangelogCategory(const QString& category);
QString changelogCategoryDisplayName(const QString& category);

struct ChangelogEntry {
	QString category;
	QString text;
};

struct ChangelogSection {
	QString heading;
	QString version;
	QString date;
	// Body lines exactly as written, without the heading.
	QStringList lines;

	[[nodiscard]] bool isUnreleased() const;
	[[nodiscard]] QVector<ChangelogEntry> entries() const;
};

struct ProjectChangelog {
	QString path;
	// Lines before the first version section.
	QStringList preamble;
	QVector<ChangelogSection> sections;
	QString lineEnding = QStringLiteral("\n");

	[[nodiscard]] int unreleasedIndex() const;
	[[nodiscard]] const ChangelogSection* section(const QString& version) const;
	[[nodiscard]] QVector<ChangelogEntry> unreleasedEntries() const;
	[[nodiscard]] QStringList versions() const;
};

ProjectChangelog defaultProjectChangelog(const QString& title);
bool parseProjectChangelog(const QString& text, ProjectChangelog* changelog, QString* error = nullptr);
QString projectChangelogText(const ProjectChangelog& changelog);
// A missing file loads as a fresh changelog with an empty Unreleased section.
bool loadProjectChangelog(const QString& path, const QString& title, ProjectChangelog* changelog, QString* error = nullptr);
bool saveProjectChangelog(const ProjectChangelog& changelog, QString* error = nullptr);
// Appends one bullet under the Unreleased section's category heading,
// creating either when missing. Multi-line text becomes one bullet.
bool addChangelogEntry(ProjectChangelog* changelog, const QString& category, const QString& text, QString* error = nullptr);
// Turns the Unreleased section into `## [version] - date` and starts a new,
// empty Unreleased section above it. Refuses a version that already exists.
bool releaseProjectChangelog(ProjectChangelog* changelog, const QString& version, const QDate& date, QString* error = nullptr);

// Versions: Semantic Versioning when the text is X.Y.Z, otherwise free text.
bool isSemanticVersion(const QString& version);
// "patch", "minor" or "major"; a non-SemVer version gains ".1".
QString bumpReleaseVersion(const QString& version, const QString& part);
// Characters a version may hold: letters, digits, '.', '-', '+' and '_'.
bool isValidReleaseVersion(const QString& version);

struct ReleaseRecordFile {
	QString path;
	QString role;
	quint64 sizeBytes = 0;
	quint32 crc32 = 0;
};

struct ReleaseRecord {
	static constexpr int kFormatVersion = 1;
	QString version;
	QString title;
	QString gameKey;
	QString scope;
	QString formatId;
	QString packageFileName;
	QString packageSha256;
	quint64 packageBytes = 0;
	QDateTime publishedUtc;
	QStringList maps;
	QVector<ReleaseRecordFile> files;
	QString notes;
	QString studioVersion;
	// The release folder: relative to the project when inside it.
	QString outputDirectory;
	// Where the record lives; not serialized.
	QString recordPath;
};

QString releaseRecordDirectory(const QString& projectRoot);
QString releaseRecordPath(const QString& projectRoot, const QString& version);
QJsonObject releaseRecordJson(const ReleaseRecord& record);
bool parseReleaseRecord(const QJsonObject& object, ReleaseRecord* record, QString* error = nullptr);
bool saveReleaseRecord(const QString& projectRoot, const ReleaseRecord& record, bool overwrite, QString* savedPath = nullptr, QString* error = nullptr);
// Every readable record, oldest first; unreadable ones become warnings.
QVector<ReleaseRecord> listReleaseRecords(const QString& projectRoot, QStringList* warnings = nullptr);
std::optional<ReleaseRecord> latestReleaseRecord(const QString& projectRoot, const QString& excludingVersion = {});

struct ReleaseInventoryDiff {
	bool hasPrevious = false;
	QString previousVersion;
	QVector<ReleaseRecordFile> added;
	QVector<ReleaseRecordFile> removed;
	QVector<ReleaseRecordFile> changed;
	int unchanged = 0;
	[[nodiscard]] bool isEmpty() const { return added.isEmpty() && removed.isEmpty() && changed.isEmpty(); }
};
ReleaseInventoryDiff diffReleaseInventories(const ReleaseRecord& previous, const QVector<ReleaseRecordFile>& current);
// One line per role and kind of change, e.g. "Added 2 maps: arena2, arena3".
QStringList releaseDiffSummaryLines(const ReleaseInventoryDiff& diff);
// Changelog bullets proposed from a diff, for an empty Unreleased section.
QVector<ChangelogEntry> suggestedChangelogEntries(const ReleaseInventoryDiff& diff);

// The inventory a plan will publish: path, role, size and CRC-32 of each file.
// Reads every planned file once; cancellation returns false.
bool releaseInventory(const ReleasePlan& plan, QVector<ReleaseRecordFile>* files, QString* error = nullptr, const std::function<bool()>& isCancelled = {});

struct ReleaseNotesInput {
	ProjectReleaseSettings release;
	QString gameKey;
	QString releaseDate;
	QString packageFileName;
	QString packageFolder;
	QString formatId;
	QString packageSha256;
	quint64 packageBytes = 0;
	ReleaseScope scope = ReleaseScope::Project;
	QVector<ChangelogEntry> changes;
	QVector<ReleaseMapInfo> maps;
	QVector<ReleaseCompositionRow> composition;
	int fileCount = 0;
	quint64 totalBytes = 0;
	ReleaseInventoryDiff diff;
	QString stockDescription;
	QString studioVersion;
};

// Builds the notes input from a plan, its changelog entries and a diff.
ReleaseNotesInput releaseNotesInput(const ReleasePlan& plan, const QVector<ChangelogEntry>& changes, const ReleaseInventoryDiff& diff, const QDate& date);
QString releaseNotesMarkdown(const ReleaseNotesInput& input);
QString releaseReadmeText(const ReleaseNotesInput& input);
// Numbered steps for installing and starting the release.
QStringList releaseInstallSteps(const ReleaseNotesInput& input);

} // namespace vibestudio
