#include "core/release_notes.h"

#include "core/deflate.h"
#include "core/game_asset_register.h"
#include "core/game_installation.h"
#include "core/studio_manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QMap>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <limits>

namespace vibestudio {

namespace {

constexpr qint64 kMaximumChangelogBytes = 8ll * 1024 * 1024;
constexpr qint64 kMaximumRecordBytes = 32ll * 1024 * 1024;
constexpr qsizetype kMaximumRecordFiles = 250000;

const QRegularExpression& sectionHeading()
{
	static const QRegularExpression expression(QStringLiteral("^##(?!#)\\s*(.*?)\\s*$"));
	return expression;
}

const QRegularExpression& subsectionHeading()
{
	static const QRegularExpression expression(QStringLiteral("^###(?!#)\\s*(.*?)\\s*$"));
	return expression;
}

const QRegularExpression& bulletLine()
{
	static const QRegularExpression expression(QStringLiteral("^\\s{0,3}[-*+]\\s+(.*)$"));
	return expression;
}

// "[1.2.0] - 2026-10-08", "1.2.0 – 2026-10-08", "[Unreleased]".
void parseHeading(const QString& title, QString* version, QString* date)
{
	static const QRegularExpression expression(QStringLiteral("^\\[?([^\\]\\s]+(?:\\s+[^\\]\\s-][^\\]]*?)?)\\]?(?:\\([^)]*\\))?(?:\\s+[-\\x{2013}\\x{2014}]\\s+(.+))?$"));
	const QRegularExpressionMatch match = expression.match(title.trimmed());
	if (match.hasMatch()) {
		*version = match.captured(1).trimmed();
		*date = match.captured(2).trimmed();
	} else {
		*version = title.trimmed();
		date->clear();
	}
}

int categoryOrder(const QString& category)
{
	const qsizetype index = changelogCategories().indexOf(category);
	return index < 0 ? int(changelogCategories().size()) : int(index);
}

QString singleLine(const QString& text)
{
	QString line = text.trimmed();
	line.replace(QRegularExpression(QStringLiteral("\\s*\\n\\s*")), QStringLiteral(" "));
	static const QRegularExpression leadingBullet(QStringLiteral("^[-*+]\\s+"));
	line.remove(leadingBullet);
	return line.trimmed();
}

QJsonArray jsonStrings(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QString humanSize(quint64 bytes)
{
	return QLocale().formattedDataSize(qint64(std::min<quint64>(bytes, quint64(std::numeric_limits<qint64>::max()))));
}

QString roleNoun(const QString& role)
{
	if (role == QStringLiteral("map")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Maps"); }
	if (role == QStringLiteral("map-companion")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Map extras"); }
	if (role == QStringLiteral("texture")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Textures"); }
	if (role == QStringLiteral("shader")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Shader scripts"); }
	if (role == QStringLiteral("model")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Models"); }
	if (role == QStringLiteral("skin")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Skins"); }
	if (role == QStringLiteral("sound")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Sounds"); }
	if (role == QStringLiteral("music")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Music"); }
	if (role == QStringLiteral("script")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Scripts"); }
	if (role == QStringLiteral("code")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Game code"); }
	if (role == QStringLiteral("map-source")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Sources"); }
	return QCoreApplication::translate("VibeStudioReleaseNotes", "Other files");
}

QString mapLabel(const ReleaseRecordFile& file)
{
	return QFileInfo(file.path).completeBaseName();
}

QString listPreview(const QStringList& names, int limit = 6)
{
	if (names.size() <= limit) {
		return names.join(QStringLiteral(", "));
	}
	return QCoreApplication::translate("VibeStudioReleaseNotes", "%1 and %2 more").arg(names.mid(0, limit).join(QStringLiteral(", "))).arg(names.size() - limit);
}

bool doomFamily(const QString& gameKey)
{
	const QString key = normalizedGameKey(gameKey);
	return key == QStringLiteral("doom") || key == QStringLiteral("heretic-hexen");
}

QString defaultIwad(const QString& gameKey, const ReleaseNotesInput& input)
{
	if (normalizedGameKey(gameKey) == QStringLiteral("heretic-hexen")) {
		return QStringLiteral("heretic.wad");
	}
	for (const ReleaseMapInfo& map : input.maps) {
		for (const QString& name : map.doomMaps) {
			if (QRegularExpression(QStringLiteral("^E\\dM\\d$"), QRegularExpression::CaseInsensitiveOption).match(name).hasMatch()) {
				return QStringLiteral("doom.wad");
			}
		}
	}
	return QStringLiteral("doom2.wad");
}

QStringList wrapped(const QString& text, int width)
{
	QStringList lines;
	for (const QString& paragraph : text.split(QLatin1Char('\n'))) {
		QString line;
		for (const QString& word : paragraph.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
			if (!line.isEmpty() && line.size() + 1 + word.size() > width) {
				lines << line;
				line.clear();
			}
			line += line.isEmpty() ? word : QLatin1Char(' ') + word;
		}
		lines << line;
	}
	return lines;
}

} // namespace

QString releasePackageHashToken()
{
	return QStringLiteral("{{package-sha256}}");
}

QString releasePackageSizeToken()
{
	return QStringLiteral("{{package-size}}");
}

QString fillReleasePackageTokens(QString text, const QString& sha256, quint64 bytes)
{
	text.replace(releasePackageHashToken(), sha256.isEmpty() ? QStringLiteral("-") : sha256);
	text.replace(releasePackageSizeToken(), bytes > 0 ? humanSize(bytes) : QStringLiteral("-"));
	return text;
}

QStringList changelogCategories()
{
	return {QStringLiteral("Added"), QStringLiteral("Changed"), QStringLiteral("Deprecated"), QStringLiteral("Removed"), QStringLiteral("Fixed"), QStringLiteral("Security")};
}

QString normalizedChangelogCategory(const QString& category)
{
	const QString key = category.trimmed().toLower();
	if (key == QStringLiteral("added") || key == QStringLiteral("add") || key == QStringLiteral("new")) {
		return QStringLiteral("Added");
	}
	if (key == QStringLiteral("changed") || key == QStringLiteral("change") || key == QStringLiteral("updated") || key == QStringLiteral("update")
		|| key == QStringLiteral("improved")) {
		return QStringLiteral("Changed");
	}
	if (key == QStringLiteral("deprecated") || key == QStringLiteral("deprecate")) {
		return QStringLiteral("Deprecated");
	}
	if (key == QStringLiteral("removed") || key == QStringLiteral("remove") || key == QStringLiteral("deleted") || key == QStringLiteral("delete")) {
		return QStringLiteral("Removed");
	}
	if (key == QStringLiteral("fixed") || key == QStringLiteral("fix") || key == QStringLiteral("fixes") || key == QStringLiteral("bugfix")) {
		return QStringLiteral("Fixed");
	}
	if (key == QStringLiteral("security")) {
		return QStringLiteral("Security");
	}
	return {};
}

QString changelogCategoryDisplayName(const QString& category)
{
	const QString id = normalizedChangelogCategory(category);
	if (id == QStringLiteral("Added")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Added"); }
	if (id == QStringLiteral("Changed")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Changed"); }
	if (id == QStringLiteral("Deprecated")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Deprecated"); }
	if (id == QStringLiteral("Removed")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Removed"); }
	if (id == QStringLiteral("Fixed")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Fixed"); }
	if (id == QStringLiteral("Security")) { return QCoreApplication::translate("VibeStudioReleaseNotes", "Security"); }
	return category;
}

bool ChangelogSection::isUnreleased() const
{
	return version.compare(QStringLiteral("Unreleased"), Qt::CaseInsensitive) == 0;
}

QVector<ChangelogEntry> ChangelogSection::entries() const
{
	QVector<ChangelogEntry> result;
	QString category;
	bool open = false;
	for (const QString& line : lines) {
		const QRegularExpressionMatch heading = subsectionHeading().match(line);
		if (heading.hasMatch()) {
			const QString normalized = normalizedChangelogCategory(heading.captured(1));
			category = normalized.isEmpty() ? heading.captured(1).trimmed() : normalized;
			open = false;
			continue;
		}
		const QRegularExpressionMatch bullet = bulletLine().match(line);
		if (bullet.hasMatch()) {
			result.push_back({category.isEmpty() ? QStringLiteral("Changed") : category, bullet.captured(1).trimmed()});
			open = true;
			continue;
		}
		if (line.trimmed().isEmpty()) {
			open = false;
			continue;
		}
		if (open && (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t')))) {
			result.last().text += QLatin1Char(' ') + line.trimmed();
		}
	}
	return result;
}

int ProjectChangelog::unreleasedIndex() const
{
	for (int i = 0; i < sections.size(); ++i) {
		if (sections[i].isUnreleased()) {
			return i;
		}
	}
	return -1;
}

const ChangelogSection* ProjectChangelog::section(const QString& version) const
{
	for (const ChangelogSection& candidate : sections) {
		if (candidate.version.compare(version.trimmed(), Qt::CaseInsensitive) == 0) {
			return &candidate;
		}
	}
	return nullptr;
}

QVector<ChangelogEntry> ProjectChangelog::unreleasedEntries() const
{
	const int index = unreleasedIndex();
	return index < 0 ? QVector<ChangelogEntry> {} : sections[index].entries();
}

QStringList ProjectChangelog::versions() const
{
	QStringList result;
	for (const ChangelogSection& candidate : sections) {
		if (!candidate.isUnreleased()) {
			result << candidate.version;
		}
	}
	return result;
}

ProjectChangelog defaultProjectChangelog(const QString& title)
{
	ProjectChangelog changelog;
	changelog.preamble << QCoreApplication::translate("VibeStudioReleaseNotes", "# Changelog") << QString()
					   << QCoreApplication::translate("VibeStudioReleaseNotes", "All notable changes to %1 are recorded here.").arg(title.isEmpty() ? QCoreApplication::translate("VibeStudioReleaseNotes", "this project") : title)
					   << QCoreApplication::translate("VibeStudioReleaseNotes", "The format follows Keep a Changelog (https://keepachangelog.com/en/1.1.0/).") << QString();
	ChangelogSection unreleased;
	unreleased.heading = QStringLiteral("## [Unreleased]");
	unreleased.version = QStringLiteral("Unreleased");
	unreleased.lines << QString();
	changelog.sections << unreleased;
	return changelog;
}

bool parseProjectChangelog(const QString& text, ProjectChangelog* changelog, QString* error)
{
	if (text.size() > kMaximumChangelogBytes) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "The changelog is too large to read.");
		}
		return false;
	}
	ProjectChangelog parsed;
	parsed.lineEnding = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
	QString normalized = text;
	normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	QStringList lines = normalized.split(QLatin1Char('\n'));
	if (!lines.isEmpty() && lines.last().isEmpty()) {
		lines.removeLast();
	}
	bool fenced = false;
	for (const QString& line : std::as_const(lines)) {
		if (line.trimmed().startsWith(QStringLiteral("```"))) {
			fenced = !fenced;
		}
		const QRegularExpressionMatch heading = fenced ? QRegularExpressionMatch() : sectionHeading().match(line);
		if (heading.hasMatch()) {
			ChangelogSection section;
			section.heading = line;
			parseHeading(heading.captured(1), &section.version, &section.date);
			parsed.sections << section;
			continue;
		}
		if (parsed.sections.isEmpty()) {
			parsed.preamble << line;
		} else {
			parsed.sections.last().lines << line;
		}
	}
	if (changelog) {
		*changelog = std::move(parsed);
	}
	return true;
}

QString projectChangelogText(const ProjectChangelog& changelog)
{
	QStringList lines = changelog.preamble;
	for (const ChangelogSection& section : changelog.sections) {
		lines << section.heading;
		lines += section.lines;
	}
	while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
		lines.removeLast();
	}
	return lines.join(changelog.lineEnding) + changelog.lineEnding;
}

bool loadProjectChangelog(const QString& path, const QString& title, ProjectChangelog* changelog, QString* error)
{
	QFile file(path);
	if (!file.exists()) {
		ProjectChangelog fresh = defaultProjectChangelog(title);
		fresh.path = path;
		if (changelog) {
			*changelog = fresh;
		}
		return true;
	}
	if (file.size() > kMaximumChangelogBytes) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "The changelog is too large to read.");
		}
		return false;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Unable to read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	ProjectChangelog parsed;
	if (!parseProjectChangelog(QString::fromUtf8(file.readAll()), &parsed, error)) {
		return false;
	}
	parsed.path = path;
	if (changelog) {
		*changelog = std::move(parsed);
	}
	return true;
}

bool saveProjectChangelog(const ProjectChangelog& changelog, QString* error)
{
	if (changelog.path.trimmed().isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "The changelog has no file path.");
		}
		return false;
	}
	QSaveFile file(changelog.path);
	const QByteArray bytes = projectChangelogText(changelog).toUtf8();
	if (!QDir().mkpath(QFileInfo(changelog.path).absolutePath()) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Unable to write %1: %2").arg(QDir::toNativeSeparators(changelog.path), file.errorString());
		}
		return false;
	}
	return true;
}

bool addChangelogEntry(ProjectChangelog* changelog, const QString& category, const QString& text, QString* error)
{
	const QString id = normalizedChangelogCategory(category);
	const QString line = singleLine(text);
	if (id.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Unknown changelog category %1. Use %2.").arg(category, changelogCategories().join(QStringLiteral(", ")));
		}
		return false;
	}
	if (line.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "The changelog entry is empty.");
		}
		return false;
	}
	int index = changelog->unreleasedIndex();
	if (index < 0) {
		ChangelogSection unreleased;
		unreleased.heading = QStringLiteral("## [Unreleased]");
		unreleased.version = QStringLiteral("Unreleased");
		unreleased.lines << QString();
		changelog->sections.prepend(unreleased);
		index = 0;
	}
	QStringList& lines = changelog->sections[index].lines;
	// Find this category's subsection, or the first one that sorts after it.
	int subsection = -1;
	int insertBefore = -1;
	for (int i = 0; i < lines.size(); ++i) {
		const QRegularExpressionMatch heading = subsectionHeading().match(lines[i]);
		if (!heading.hasMatch()) {
			continue;
		}
		const QString existing = normalizedChangelogCategory(heading.captured(1));
		if (existing == id) {
			subsection = i;
			break;
		}
		if (insertBefore < 0 && categoryOrder(existing) > categoryOrder(id)) {
			insertBefore = i;
		}
	}
	const QString bullet = QStringLiteral("- ") + line;
	if (subsection >= 0) {
		int end = subsection + 1;
		while (end < lines.size() && !subsectionHeading().match(lines[end]).hasMatch()) {
			++end;
		}
		int last = end - 1;
		while (last > subsection && lines[last].trimmed().isEmpty()) {
			--last;
		}
		lines.insert(last + 1, bullet);
		return true;
	}
	QStringList block {QStringLiteral("### ") + id, bullet, QString()};
	if (insertBefore >= 0) {
		for (int i = int(block.size()) - 1; i >= 0; --i) {
			lines.insert(insertBefore, block[i]);
		}
		return true;
	}
	while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) {
		lines.removeLast();
	}
	lines << QString();
	lines += block;
	return true;
}

bool releaseProjectChangelog(ProjectChangelog* changelog, const QString& version, const QDate& date, QString* error)
{
	if (!isValidReleaseVersion(version)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "%1 is not a usable version. Use letters, digits, '.', '-', '+' or '_', such as 1.2.0.").arg(version);
		}
		return false;
	}
	if (changelog->section(version)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "The changelog already has a section for %1.").arg(version);
		}
		return false;
	}
	const QString isoDate = (date.isValid() ? date : QDate::currentDate()).toString(Qt::ISODate);
	int index = changelog->unreleasedIndex();
	ChangelogSection released;
	if (index >= 0) {
		released = changelog->sections[index];
	} else {
		index = 0;
		changelog->sections.prepend(ChangelogSection {});
	}
	released.heading = QStringLiteral("## [%1] - %2").arg(version, isoDate);
	released.version = version;
	released.date = isoDate;
	while (!released.lines.isEmpty() && released.lines.last().trimmed().isEmpty()) {
		released.lines.removeLast();
	}
	if (released.lines.isEmpty() || !released.lines.first().trimmed().isEmpty()) {
		released.lines.prepend(QString());
	}
	released.lines << QString();
	changelog->sections[index] = released;
	ChangelogSection unreleased;
	unreleased.heading = QStringLiteral("## [Unreleased]");
	unreleased.version = QStringLiteral("Unreleased");
	unreleased.lines << QString();
	changelog->sections.insert(index, unreleased);
	return true;
}

bool isSemanticVersion(const QString& version)
{
	static const QRegularExpression expression(QStringLiteral("^(0|[1-9]\\d*)\\.(0|[1-9]\\d*)\\.(0|[1-9]\\d*)(?:-[0-9A-Za-z.-]+)?(?:\\+[0-9A-Za-z.-]+)?$"));
	return expression.match(version.trimmed()).hasMatch();
}

bool isValidReleaseVersion(const QString& version)
{
	static const QRegularExpression expression(QStringLiteral("^[0-9A-Za-z][0-9A-Za-z._+-]{0,31}$"));
	return expression.match(version).hasMatch();
}

QString bumpReleaseVersion(const QString& version, const QString& part)
{
	QString text = version.trimmed();
	QString prefix;
	if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) && text.size() > 1 && text[1].isDigit()) {
		prefix = text.left(1);
		text = text.mid(1);
	}
	static const QRegularExpression numbers(QStringLiteral("^(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?(-[0-9A-Za-z.-]+)?(?:\\+[0-9A-Za-z.-]+)?$"));
	const QRegularExpressionMatch match = numbers.match(text);
	if (!match.hasMatch()) {
		return text.isEmpty() ? QStringLiteral("1.0.0") : prefix + text + QStringLiteral(".1");
	}
	qulonglong major = match.captured(1).toULongLong();
	qulonglong minor = match.captured(2).isEmpty() ? 0 : match.captured(2).toULongLong();
	qulonglong patch = match.captured(3).isEmpty() ? 0 : match.captured(3).toULongLong();
	const bool prerelease = !match.captured(4).isEmpty();
	if (part == QStringLiteral("major")) {
		if (!(prerelease && minor == 0 && patch == 0)) {
			++major;
		}
		minor = 0;
		patch = 0;
	} else if (part == QStringLiteral("minor")) {
		if (!(prerelease && patch == 0)) {
			++minor;
		}
		patch = 0;
	} else if (!prerelease) {
		++patch;
	}
	return prefix + QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString releaseRecordDirectory(const QString& projectRoot)
{
	return projectRoot.trimmed().isEmpty() ? QString() : QDir(projectRoot).absoluteFilePath(QStringLiteral(".vibestudio/releases"));
}

QString releaseRecordPath(const QString& projectRoot, const QString& version)
{
	if (projectRoot.trimmed().isEmpty() || !isValidReleaseVersion(version)) {
		return {};
	}
	return QDir(releaseRecordDirectory(projectRoot)).absoluteFilePath(version + QStringLiteral(".json"));
}

QJsonObject releaseRecordJson(const ReleaseRecord& record)
{
	QJsonArray files;
	for (const ReleaseRecordFile& file : record.files) {
		files.append(QJsonArray {file.path, file.role, double(file.sizeBytes), QStringLiteral("%1").arg(file.crc32, 8, 16, QLatin1Char('0'))});
	}
	return {
		{QStringLiteral("format"), QStringLiteral("vibestudio-release-record")},
		{QStringLiteral("version"), ReleaseRecord::kFormatVersion},
		{QStringLiteral("release"), record.version},
		{QStringLiteral("title"), record.title},
		{QStringLiteral("game"), record.gameKey},
		{QStringLiteral("scope"), record.scope},
		{QStringLiteral("packageFormat"), record.formatId},
		{QStringLiteral("package"), record.packageFileName},
		{QStringLiteral("packageSha256"), record.packageSha256},
		{QStringLiteral("packageBytes"), double(record.packageBytes)},
		{QStringLiteral("publishedUtc"), record.publishedUtc.toUTC().toString(Qt::ISODate)},
		{QStringLiteral("maps"), jsonStrings(record.maps)},
		{QStringLiteral("files"), files},
		{QStringLiteral("notes"), record.notes},
		{QStringLiteral("studioVersion"), record.studioVersion},
		{QStringLiteral("output"), QDir::fromNativeSeparators(record.outputDirectory)},
	};
}

bool parseReleaseRecord(const QJsonObject& object, ReleaseRecord* record, QString* error)
{
	const auto fail = [error](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (object.value(QStringLiteral("format")).toString() != QStringLiteral("vibestudio-release-record")) {
		return fail(QCoreApplication::translate("VibeStudioReleaseNotes", "The file is not a VibeStudio release record."));
	}
	if (object.value(QStringLiteral("version")).toInt(-1) != ReleaseRecord::kFormatVersion) {
		return fail(QCoreApplication::translate("VibeStudioReleaseNotes", "This release record version is not supported."));
	}
	ReleaseRecord parsed;
	parsed.version = object.value(QStringLiteral("release")).toString();
	if (!isValidReleaseVersion(parsed.version)) {
		return fail(QCoreApplication::translate("VibeStudioReleaseNotes", "The release record has no usable version."));
	}
	parsed.title = object.value(QStringLiteral("title")).toString();
	parsed.gameKey = object.value(QStringLiteral("game")).toString();
	parsed.scope = object.value(QStringLiteral("scope")).toString();
	parsed.formatId = object.value(QStringLiteral("packageFormat")).toString();
	parsed.packageFileName = object.value(QStringLiteral("package")).toString();
	parsed.packageSha256 = object.value(QStringLiteral("packageSha256")).toString();
	parsed.packageBytes = quint64(object.value(QStringLiteral("packageBytes")).toDouble());
	parsed.publishedUtc = QDateTime::fromString(object.value(QStringLiteral("publishedUtc")).toString(), Qt::ISODate).toUTC();
	for (const QJsonValue& value : object.value(QStringLiteral("maps")).toArray()) {
		parsed.maps << value.toString();
	}
	const QJsonArray files = object.value(QStringLiteral("files")).toArray();
	if (files.size() > kMaximumRecordFiles) {
		return fail(QCoreApplication::translate("VibeStudioReleaseNotes", "The release record lists too many files."));
	}
	for (const QJsonValue& value : files) {
		const QJsonArray row = value.toArray();
		ReleaseRecordFile file;
		file.path = row.at(0).toString();
		file.role = row.at(1).toString();
		file.sizeBytes = quint64(row.at(2).toDouble());
		bool ok = false;
		file.crc32 = row.at(3).toString().toUInt(&ok, 16);
		if (file.path.isEmpty() || !ok) {
			return fail(QCoreApplication::translate("VibeStudioReleaseNotes", "The release record has a malformed file row."));
		}
		parsed.files << file;
	}
	parsed.notes = object.value(QStringLiteral("notes")).toString();
	parsed.studioVersion = object.value(QStringLiteral("studioVersion")).toString();
	parsed.outputDirectory = object.value(QStringLiteral("output")).toString();
	if (record) {
		*record = std::move(parsed);
	}
	return true;
}

bool saveReleaseRecord(const QString& projectRoot, const ReleaseRecord& record, bool overwrite, QString* savedPath, QString* error)
{
	const QString path = releaseRecordPath(projectRoot, record.version);
	if (path.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Release records need a project folder and a usable version.");
		}
		return false;
	}
	if (QFileInfo::exists(path) && !overwrite) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Version %1 has already been released. Choose a new version, or allow replacing the release.").arg(record.version);
		}
		return false;
	}
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Unable to create %1.").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
		}
		return false;
	}
	QSaveFile file(path);
	const QByteArray bytes = QJsonDocument(releaseRecordJson(record)).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Unable to write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	if (savedPath) {
		*savedPath = path;
	}
	return true;
}

QVector<ReleaseRecord> listReleaseRecords(const QString& projectRoot, QStringList* warnings)
{
	QVector<ReleaseRecord> records;
	const QString directory = releaseRecordDirectory(projectRoot);
	if (directory.isEmpty()) {
		return records;
	}
	for (const QFileInfo& info : QDir(directory).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
		if (info.size() > kMaximumRecordBytes) {
			if (warnings) {
				*warnings << QCoreApplication::translate("VibeStudioReleaseNotes", "%1 is too large to read.").arg(info.fileName());
			}
			continue;
		}
		QFile file(info.absoluteFilePath());
		if (!file.open(QIODevice::ReadOnly)) {
			continue;
		}
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
		ReleaseRecord record;
		QString error;
		if (!document.isObject() || !parseReleaseRecord(document.object(), &record, &error)) {
			if (warnings) {
				*warnings << QCoreApplication::translate("VibeStudioReleaseNotes", "%1 is not a readable release record: %2").arg(info.fileName(), error);
			}
			continue;
		}
		record.recordPath = info.absoluteFilePath();
		records << record;
	}
	std::sort(records.begin(), records.end(), [](const ReleaseRecord& left, const ReleaseRecord& right) {
		if (left.publishedUtc != right.publishedUtc) {
			return left.publishedUtc < right.publishedUtc;
		}
		return left.version < right.version;
	});
	return records;
}

std::optional<ReleaseRecord> latestReleaseRecord(const QString& projectRoot, const QString& excludingVersion)
{
	const QVector<ReleaseRecord> records = listReleaseRecords(projectRoot);
	for (auto it = records.crbegin(); it != records.crend(); ++it) {
		if (excludingVersion.isEmpty() || it->version.compare(excludingVersion, Qt::CaseInsensitive) != 0) {
			return *it;
		}
	}
	return std::nullopt;
}

ReleaseInventoryDiff diffReleaseInventories(const ReleaseRecord& previous, const QVector<ReleaseRecordFile>& current)
{
	ReleaseInventoryDiff diff;
	diff.hasPrevious = true;
	diff.previousVersion = previous.version;
	QHash<QString, const ReleaseRecordFile*> before;
	for (const ReleaseRecordFile& file : previous.files) {
		before.insert(file.path.toCaseFolded(), &file);
	}
	QSet<QString> seen;
	for (const ReleaseRecordFile& file : current) {
		const QString key = file.path.toCaseFolded();
		seen.insert(key);
		const ReleaseRecordFile* old = before.value(key);
		if (!old) {
			diff.added << file;
		} else if (old->sizeBytes != file.sizeBytes || old->crc32 != file.crc32) {
			diff.changed << file;
		} else {
			++diff.unchanged;
		}
	}
	for (const ReleaseRecordFile& file : previous.files) {
		if (!seen.contains(file.path.toCaseFolded())) {
			diff.removed << file;
		}
	}
	return diff;
}

QStringList releaseDiffSummaryLines(const ReleaseInventoryDiff& diff)
{
	QStringList lines;
	if (!diff.hasPrevious) {
		return lines;
	}
	// Maps are named; other roles are counted.
	const auto describe = [&lines](const QVector<ReleaseRecordFile>& files, const QString& text) {
		QMap<QString, QStringList> byRole;
		for (const ReleaseRecordFile& file : files) {
			byRole[file.role] << (file.role == QStringLiteral("map") ? mapLabel(file) : file.path);
		}
		for (auto it = byRole.cbegin(); it != byRole.cend(); ++it) {
			lines << text.arg(roleNoun(it.key()), it.key() == QStringLiteral("map") ? listPreview(it.value()) : QString::number(it.value().size()));
		}
	};
	describe(diff.added, QCoreApplication::translate("VibeStudioReleaseNotes", "%1 added: %2"));
	describe(diff.changed, QCoreApplication::translate("VibeStudioReleaseNotes", "%1 updated: %2"));
	describe(diff.removed, QCoreApplication::translate("VibeStudioReleaseNotes", "%1 removed: %2"));
	if (lines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioReleaseNotes", "No file changed since %1.").arg(diff.previousVersion);
	}
	return lines;
}

QVector<ChangelogEntry> suggestedChangelogEntries(const ReleaseInventoryDiff& diff)
{
	QVector<ChangelogEntry> entries;
	if (!diff.hasPrevious) {
		entries.push_back({QStringLiteral("Added"), QCoreApplication::translate("VibeStudioReleaseNotes", "First release.")});
		return entries;
	}
	const auto add = [&entries](const QString& category, const QVector<ReleaseRecordFile>& files, const QString& mapText, const QString& otherText) {
		QStringList maps;
		QMap<QString, int> others;
		for (const ReleaseRecordFile& file : files) {
			if (file.role == QStringLiteral("map")) {
				maps << mapLabel(file);
			} else if (file.role != QStringLiteral("map-companion")) {
				++others[file.role];
			}
		}
		if (!maps.isEmpty()) {
			entries.push_back({category, mapText.arg(listPreview(maps))});
		}
		for (auto it = others.cbegin(); it != others.cend(); ++it) {
			entries.push_back({category, otherText.arg(roleNoun(it.key()).toLower()).arg(it.value())});
		}
	};
	add(QStringLiteral("Added"), diff.added, QCoreApplication::translate("VibeStudioReleaseNotes", "New maps: %1."), QCoreApplication::translate("VibeStudioReleaseNotes", "New %1: %2."));
	add(QStringLiteral("Changed"), diff.changed, QCoreApplication::translate("VibeStudioReleaseNotes", "Updated maps: %1."), QCoreApplication::translate("VibeStudioReleaseNotes", "Updated %1: %2."));
	add(QStringLiteral("Removed"), diff.removed, QCoreApplication::translate("VibeStudioReleaseNotes", "Removed maps: %1."), QCoreApplication::translate("VibeStudioReleaseNotes", "Removed %1: %2."));
	return entries;
}

bool releaseInventory(const ReleasePlan& plan, QVector<ReleaseRecordFile>* files, QString* error, const std::function<bool()>& isCancelled)
{
	QVector<ReleaseRecordFile> result;
	result.reserve(plan.entries.size());
	for (const ReleaseEntry& entry : plan.entries) {
		if (isCancelled && isCancelled()) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Cancelled.");
			}
			return false;
		}
		ReleaseRecordFile file;
		file.path = entry.virtualPath;
		file.role = entry.role;
		file.sizeBytes = entry.sizeBytes;
		quint32 crc = 0;
		QString readError;
		bool read = false;
		const auto sink = [&crc](QByteArrayView chunk) {
			crc = crc32View(chunk, crc);
			return true;
		};
		if (entry.lumpSource >= 0 && entry.lumpSource < plan.lumpSources.size()) {
			read = plan.lumpSources[entry.lumpSource]->streamEntryAt(entry.layerIndex, sink, &readError, isCancelled);
			// Lump names repeat; keep each occurrence distinct in the inventory.
			file.path = QStringLiteral("%1#%2").arg(entry.virtualPath).arg(entry.layerIndex);
		} else if (!entry.sourcePath.isEmpty()) {
			read = fileCrc32(entry.sourcePath, &crc, &readError, isCancelled);
		} else if (plan.reader && entry.layer >= 0) {
			const auto* layer = plan.reader->layer(entry.layer);
			read = layer && layer->reader && layer->reader->streamEntryAt(entry.layerIndex, sink, &readError, isCancelled);
		}
		if (!read) {
			if (error) {
				*error = QCoreApplication::translate("VibeStudioReleaseNotes", "Unable to read %1: %2").arg(entry.virtualPath, readError);
			}
			return false;
		}
		file.crc32 = crc;
		result << file;
	}
	if (files) {
		*files = std::move(result);
	}
	return true;
}

ReleaseNotesInput releaseNotesInput(const ReleasePlan& plan, const QVector<ChangelogEntry>& changes, const ReleaseInventoryDiff& diff, const QDate& date)
{
	ReleaseNotesInput input;
	input.release = plan.release;
	input.gameKey = plan.gameKey;
	input.releaseDate = (date.isValid() ? date : QDate::currentDate()).toString(Qt::ISODate);
	input.packageFileName = plan.packageFileName;
	input.packageFolder = plan.packageFolder;
	input.formatId = plan.formatId;
	input.scope = plan.scope;
	input.changes = changes;
	input.maps = plan.maps;
	input.composition = plan.composition();
	input.fileCount = int(plan.entries.size());
	input.totalBytes = plan.totalBytes;
	input.diff = diff;
	input.stockDescription = plan.stockDescription;
	input.studioVersion = versionString();
	return input;
}

QStringList releaseInstallSteps(const ReleaseNotesInput& input)
{
	QStringList steps;
	const QString game = normalizedGameKey(input.gameKey);
	const GameDefinition definition = gameDefinitionForKey(game);
	const QString base = definition.baseGameDirectory;
	const QString folder = input.packageFolder.isEmpty() ? base : input.packageFolder;
	const bool modFolder = !folder.isEmpty() && folder.compare(base, Qt::CaseInsensitive) != 0;
	QString firstMap;
	if (!input.maps.isEmpty()) {
		firstMap = input.maps.first().doomMaps.isEmpty() ? input.maps.first().name : input.maps.first().doomMaps.first();
	}
	const QString package = input.packageFileName;
	if (doomFamily(game)) {
		const QString iwad = defaultIwad(game, input);
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Put %1 where your source port can find it, for example next to %2.").arg(package, iwad);
		QString command = QStringLiteral("gzdoom -iwad %1 -file %2").arg(iwad, package);
		const QRegularExpressionMatch numbered = QRegularExpression(QStringLiteral("^MAP(\\d\\d)$"), QRegularExpression::CaseInsensitiveOption).match(firstMap);
		const QRegularExpressionMatch episode = QRegularExpression(QStringLiteral("^E(\\d)M(\\d)$"), QRegularExpression::CaseInsensitiveOption).match(firstMap);
		if (numbered.hasMatch()) {
			command += QStringLiteral(" -warp %1").arg(numbered.captured(1).toInt());
		} else if (episode.hasMatch()) {
			command += QStringLiteral(" -warp %1 %2").arg(episode.captured(1), episode.captured(2));
		}
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Start it from a command line, for example: %1").arg(command);
		return steps;
	}
	const QString gameName = definition.displayName;
	if (input.formatId == QStringLiteral("zip")) {
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Extract the archive into your %1 installation's %2 folder, keeping its folders.").arg(gameName, folder);
	} else if (modFolder) {
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Extract the archive into your %1 installation folder, so the %2 folder sits beside %3.").arg(gameName, folder, base);
	} else if (input.formatId == QStringLiteral("pak")) {
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Copy %1 into your %2 installation's %3 folder, renaming it to the next free number (for example pak2.pak) if that name is taken.").arg(package, gameName, folder);
	} else {
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Copy %1 into your %2 installation's %3 folder.").arg(package, gameName, folder);
	}
	if (modFolder) {
		const QString option = game == QStringLiteral("quake") ? QStringLiteral("-game %1").arg(folder)
			: game == QStringLiteral("quake2") ? QStringLiteral("+set game %1").arg(folder) : QStringLiteral("+set fs_game %1").arg(folder);
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Start the game with %1.").arg(option);
	}
	if (!firstMap.isEmpty()) {
		steps << QCoreApplication::translate("VibeStudioReleaseNotes", "Open the console and type: map %1").arg(firstMap);
	}
	return steps;
}

QString releaseNotesMarkdown(const ReleaseNotesInput& input)
{
	QStringList lines;
	const QString gameName = gameDefinitionForKey(input.gameKey).displayName;
	lines << QStringLiteral("# %1 %2").arg(input.release.title, input.release.version) << QString();
	if (!input.release.description.isEmpty()) {
		lines << input.release.description << QString();
	}
	lines << QStringLiteral("| | |") << QStringLiteral("| --- | --- |");
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "| Game | %1 |").arg(gameName);
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "| Version | %1 |").arg(input.release.version);
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "| Released | %1 |").arg(input.releaseDate);
	if (!input.release.authors.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioReleaseNotes", "| Authors | %1 |").arg(input.release.authors.join(QStringLiteral(", ")));
	}
	// Before the package exists, tokens stand in for its size and hash;
	// publishRelease fills them in once the package is written.
	const QString size = input.packageBytes > 0 ? humanSize(input.packageBytes) : releasePackageSizeToken();
	const QString hash = input.packageSha256.isEmpty() ? releasePackageHashToken() : input.packageSha256;
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "| Package | %1 |").arg(QStringLiteral("`%1` (%2)").arg(input.packageFileName, size));
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "| SHA-256 | `%1` |").arg(hash);
	if (!input.release.website.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioReleaseNotes", "| Website | %1 |").arg(input.release.website);
	}
	lines << QString();

	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "## What's new") << QString();
	if (input.changes.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioReleaseNotes", "No changes were recorded for this release.") << QString();
	} else {
		for (const QString& category : changelogCategories()) {
			QStringList bullets;
			for (const ChangelogEntry& entry : input.changes) {
				if (normalizedChangelogCategory(entry.category) == category) {
					bullets << QStringLiteral("- ") + entry.text;
				}
			}
			if (!bullets.isEmpty()) {
				lines << QStringLiteral("### ") + changelogCategoryDisplayName(category) << QString() << bullets << QString();
			}
		}
	}
	const QStringList diffLines = releaseDiffSummaryLines(input.diff);
	if (!diffLines.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioReleaseNotes", "Compared with %1:").arg(input.diff.previousVersion) << QString();
		for (const QString& line : diffLines) {
			lines << QStringLiteral("- ") + line;
		}
		lines << QString();
	}

	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "## Contents") << QString();
	for (const ReleaseMapInfo& map : input.maps) {
		const QString names = map.doomMaps.isEmpty() ? map.name : map.doomMaps.join(QStringLiteral(", "));
		lines << (map.title.isEmpty() ? QCoreApplication::translate("VibeStudioReleaseNotes", "- Map: %1").arg(names)
									  : QCoreApplication::translate("VibeStudioReleaseNotes", "- Map: %1, \"%2\"").arg(names, map.title));
	}
	for (const ReleaseCompositionRow& row : input.composition) {
		if (row.role == QStringLiteral("map")) {
			continue;
		}
		const int files = row.count;
		lines << QCoreApplication::translate("VibeStudioReleaseNotes", "- %1: %2, %3").arg(roleNoun(row.role),
			QCoreApplication::translate("VibeStudioReleaseNotes", "%n file(s)", nullptr, files), humanSize(row.bytes));
	}
	const int total = input.fileCount;
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "- In total: %1, %2").arg(
		QCoreApplication::translate("VibeStudioReleaseNotes", "%n file(s)", nullptr, total), humanSize(input.totalBytes)) << QString();

	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "## Requirements") << QString();
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "- %1").arg(gameName);
	for (const ProjectReleaseRequirement& requirement : input.release.requirements) {
		const QString name = requirement.name.isEmpty() ? requirement.path : requirement.name;
		lines << (requirement.url.isEmpty() ? QStringLiteral("- ") + name : QStringLiteral("- [%1](%2)").arg(name, requirement.url));
	}
	lines << QString();

	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "## Installation") << QString();
	const QStringList steps = releaseInstallSteps(input);
	for (int i = 0; i < steps.size(); ++i) {
		lines << QStringLiteral("%1. %2").arg(i + 1).arg(steps[i]);
	}
	lines << QString();
	if (!input.release.license.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioReleaseNotes", "## Licence and permissions") << QString() << input.release.license << QString();
	}
	lines << QCoreApplication::translate("VibeStudioReleaseNotes", "Packaged with VibeStudio %1.").arg(input.studioVersion);
	return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString releaseReadmeText(const ReleaseNotesInput& input)
{
	const QString game = normalizedGameKey(input.gameKey);
	const GameDefinition definition = gameDefinitionForKey(game);
	constexpr int labelWidth = 24;
	const QString rule(75, QLatin1Char('='));
	QStringList lines;
	const auto field = [&lines](const QString& label, const QString& value) {
		const QStringList valueLines = wrapped(value.isEmpty() ? QStringLiteral("-") : value, 75 - labelWidth - 2);
		// An empty label continues the field above.
		lines << (label.isEmpty() ? QString(labelWidth + 2, QLatin1Char(' ')) + valueLines.value(0)
								  : QStringLiteral("%1: %2").arg(label.leftJustified(labelWidth), valueLines.value(0)));
		for (int i = 1; i < valueLines.size(); ++i) {
			lines << QString(labelWidth + 2, QLatin1Char(' ')) + valueLines[i];
		}
	};
	const auto section = [&lines](const QString& title) {
		lines << QString() << QStringLiteral("* %1 *").arg(title) << QString();
	};
	QStringList emails;
	QStringList authors;
	for (const QString& author : input.release.authors) {
		static const QRegularExpression email(QStringLiteral("^(.*?)\\s*<([^>]+)>\\s*$"));
		const QRegularExpressionMatch match = email.match(author);
		if (match.hasMatch()) {
			authors << match.captured(1);
			emails << match.captured(2);
		} else {
			authors << author;
		}
	}
	lines << rule;
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Title"), input.release.title);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Version"), input.release.version);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Filename"), input.packageFileName);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Release date"), input.releaseDate);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Author"), authors.join(QStringLiteral(", ")));
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Email Address"), emails.join(QStringLiteral(", ")));
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Website"), input.release.website);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Description"), input.release.description);
	lines << rule;

	section(QCoreApplication::translate("VibeStudioReleaseNotes", "What is included"));
	QStringList mapNames;
	for (const ReleaseMapInfo& map : input.maps) {
		const QString names = map.doomMaps.isEmpty() ? map.name : map.doomMaps.join(QStringLiteral(", "));
		mapNames << (map.title.isEmpty() ? names : QStringLiteral("%1 (%2)").arg(names, map.title));
	}
	const auto has = [&input](const QString& role) {
		for (const ReleaseCompositionRow& row : input.composition) {
			if (row.role == role) {
				return true;
			}
		}
		return false;
	};
	const QString yes = QCoreApplication::translate("VibeStudioReleaseNotes", "Yes");
	const QString no = QCoreApplication::translate("VibeStudioReleaseNotes", "No");
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "New levels"), mapNames.isEmpty() ? no : mapNames.join(QStringLiteral("; ")));
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Sounds"), has(QStringLiteral("sound")) ? yes : no);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Music"), has(QStringLiteral("music")) ? yes : no);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Graphics"), has(QStringLiteral("texture")) || has(QStringLiteral("shader")) ? yes : no);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Models"), has(QStringLiteral("model")) ? yes : no);
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Game code"), has(QStringLiteral("code")) ? yes : no);
	QStringList required;
	for (const ProjectReleaseRequirement& requirement : input.release.requirements) {
		required << (requirement.url.isEmpty() ? (requirement.name.isEmpty() ? requirement.path : requirement.name)
											   : QStringLiteral("%1 (%2)").arg(requirement.name.isEmpty() ? requirement.path : requirement.name, requirement.url));
	}
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Other files required"), required.isEmpty() ? QCoreApplication::translate("VibeStudioReleaseNotes", "None") : required.join(QStringLiteral("; ")));

	section(QCoreApplication::translate("VibeStudioReleaseNotes", "Play Information"));
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Game"), doomFamily(game) ? defaultIwad(game, input) : definition.displayName);
	if (!mapNames.isEmpty()) {
		QStringList mapSlots;
		for (const ReleaseMapInfo& map : input.maps) {
			mapSlots += map.doomMaps.isEmpty() ? QStringList {map.name} : map.doomMaps;
		}
		field(QCoreApplication::translate("VibeStudioReleaseNotes", "Map #"), mapSlots.join(QStringLiteral(", ")));
	}
	const QStringList steps = releaseInstallSteps(input);
	for (int i = 0; i < steps.size(); ++i) {
		field(i == 0 ? QCoreApplication::translate("VibeStudioReleaseNotes", "How to play") : QString(), QStringLiteral("%1. %2").arg(i + 1).arg(steps[i]));
	}

	if (!input.changes.isEmpty() || input.diff.hasPrevious) {
		section(QCoreApplication::translate("VibeStudioReleaseNotes", "What is new"));
		for (const ChangelogEntry& entry : input.changes) {
			for (const QString& line : wrapped(QStringLiteral("- %1: %2").arg(changelogCategoryDisplayName(entry.category), entry.text), 73)) {
				lines << line;
			}
		}
		for (const QString& line : releaseDiffSummaryLines(input.diff)) {
			lines << QStringLiteral("- ") + line;
		}
	}

	section(QCoreApplication::translate("VibeStudioReleaseNotes", "Construction"));
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Editor(s) used"), QStringLiteral("VibeStudio %1").arg(input.studioVersion));
	field(QCoreApplication::translate("VibeStudioReleaseNotes", "Package"), QStringLiteral("%1, %2").arg(input.packageFileName,
		input.packageBytes > 0 ? humanSize(input.packageBytes) : releasePackageSizeToken()));
	field(QStringLiteral("SHA-256"), input.packageSha256.isEmpty() ? releasePackageHashToken() : input.packageSha256);

	section(QCoreApplication::translate("VibeStudioReleaseNotes", "Copyright / Permissions"));
	for (const QString& line : wrapped(input.release.license.isEmpty()
			? QCoreApplication::translate("VibeStudioReleaseNotes", "No permissions were stated. Ask the author before reusing this work.") : input.release.license, 75)) {
		lines << line;
	}
	if (!input.release.website.isEmpty()) {
		section(QCoreApplication::translate("VibeStudioReleaseNotes", "Where to get the file that this text file describes"));
		lines << input.release.website;
	}
	return lines.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n");
}

} // namespace vibestudio
