#include "core/project_manifest.h"
#include "core/game_installation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace vibestudio {

namespace {

const QStringList& knownManifestKeys()
{
	static const QStringList keys {
		QStringLiteral("schemaVersion"), QStringLiteral("projectId"), QStringLiteral("displayName"), QStringLiteral("game"),
		QStringLiteral("sourceFolders"), QStringLiteral("packageFolders"), QStringLiteral("outputFolder"), QStringLiteral("tempFolder"),
		QStringLiteral("selectedInstallationId"), QStringLiteral("compilerSearchPaths"), QStringLiteral("compilerToolOverrides"),
		QStringLiteral("registeredOutputPaths"), QStringLiteral("settingsOverrides"), QStringLiteral("release"),
		QStringLiteral("createdUtc"), QStringLiteral("updatedUtc"),
	};
	return keys;
}

const QStringList& knownReleaseKeys()
{
	static const QStringList keys {
		QStringLiteral("title"), QStringLiteral("version"), QStringLiteral("authors"), QStringLiteral("description"),
		QStringLiteral("website"), QStringLiteral("license"), QStringLiteral("packageName"), QStringLiteral("packageFormat"),
		QStringLiteral("gameFolder"), QStringLiteral("stockSources"), QStringLiteral("requires"), QStringLiteral("include"),
		QStringLiteral("exclude"), QStringLiteral("outputFolder"), QStringLiteral("changelog"), QStringLiteral("includeSources"),
	};
	return keys;
}

QStringList releaseStrings(const QJsonValue& value)
{
	QStringList result;
	for (const QJsonValue& item : value.toArray()) {
		const QString text = item.toString().trimmed();
		if (!text.isEmpty() && !result.contains(text)) {
			result.push_back(text);
		}
	}
	return result;
}

QJsonArray releaseStringArray(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		if (!value.trimmed().isEmpty()) {
			array.append(value.trimmed());
		}
	}
	return array;
}

ProjectReleaseSettings releaseSettingsFromJson(const QJsonValue& value)
{
	ProjectReleaseSettings release;
	if (!value.isObject()) {
		return release;
	}
	const QJsonObject object = value.toObject();
	release.title = object.value(QStringLiteral("title")).toString().trimmed();
	release.version = object.value(QStringLiteral("version")).toString().trimmed();
	release.authors = releaseStrings(object.value(QStringLiteral("authors")));
	release.description = object.value(QStringLiteral("description")).toString().trimmed();
	release.website = object.value(QStringLiteral("website")).toString().trimmed();
	release.license = object.value(QStringLiteral("license")).toString().trimmed();
	release.packageName = object.value(QStringLiteral("packageName")).toString().trimmed();
	release.packageFormat = object.value(QStringLiteral("packageFormat")).toString().trimmed().toLower();
	release.gameFolder = object.value(QStringLiteral("gameFolder")).toString().trimmed();
	release.stockSources = releaseStrings(object.value(QStringLiteral("stockSources")));
	for (const QJsonValue& item : object.value(QStringLiteral("requires")).toArray()) {
		const QJsonObject requirement = item.toObject();
		ProjectReleaseRequirement entry;
		entry.name = requirement.value(QStringLiteral("name")).toString().trimmed();
		entry.path = requirement.value(QStringLiteral("path")).toString().trimmed();
		entry.url = requirement.value(QStringLiteral("url")).toString().trimmed();
		if (!entry.name.isEmpty() || !entry.path.isEmpty()) {
			release.requirements.push_back(entry);
		}
	}
	release.include = releaseStrings(object.value(QStringLiteral("include")));
	release.exclude = releaseStrings(object.value(QStringLiteral("exclude")));
	release.outputFolder = object.value(QStringLiteral("outputFolder")).toString().trimmed();
	release.changelogFile = object.value(QStringLiteral("changelog")).toString().trimmed();
	release.includeSources = object.value(QStringLiteral("includeSources")).toBool(false);
	for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
		if (!knownReleaseKeys().contains(it.key())) {
			release.extraFields.insert(it.key(), it.value());
		}
	}
	return release;
}

QJsonObject releaseSettingsToJson(const ProjectReleaseSettings& release)
{
	QJsonObject object = release.extraFields;
	const auto put = [&object](const QString& key, const QString& value) {
		if (!value.trimmed().isEmpty()) {
			object.insert(key, value.trimmed());
		} else {
			object.remove(key);
		}
	};
	const auto putList = [&object](const QString& key, const QStringList& values) {
		const QJsonArray array = releaseStringArray(values);
		if (!array.isEmpty()) {
			object.insert(key, array);
		} else {
			object.remove(key);
		}
	};
	put(QStringLiteral("title"), release.title);
	put(QStringLiteral("version"), release.version);
	putList(QStringLiteral("authors"), release.authors);
	put(QStringLiteral("description"), release.description);
	put(QStringLiteral("website"), release.website);
	put(QStringLiteral("license"), release.license);
	put(QStringLiteral("packageName"), release.packageName);
	put(QStringLiteral("packageFormat"), release.packageFormat);
	put(QStringLiteral("gameFolder"), release.gameFolder);
	putList(QStringLiteral("stockSources"), release.stockSources);
	QJsonArray requirements;
	for (const ProjectReleaseRequirement& requirement : release.requirements) {
		QJsonObject entry;
		if (!requirement.name.trimmed().isEmpty()) { entry.insert(QStringLiteral("name"), requirement.name.trimmed()); }
		if (!requirement.path.trimmed().isEmpty()) { entry.insert(QStringLiteral("path"), requirement.path.trimmed()); }
		if (!requirement.url.trimmed().isEmpty()) { entry.insert(QStringLiteral("url"), requirement.url.trimmed()); }
		if (!entry.isEmpty()) { requirements.append(entry); }
	}
	if (!requirements.isEmpty()) {
		object.insert(QStringLiteral("requires"), requirements);
	} else {
		object.remove(QStringLiteral("requires"));
	}
	putList(QStringLiteral("include"), release.include);
	putList(QStringLiteral("exclude"), release.exclude);
	put(QStringLiteral("outputFolder"), release.outputFolder);
	put(QStringLiteral("changelog"), release.changelogFile);
	if (release.includeSources) {
		object.insert(QStringLiteral("includeSources"), true);
	} else {
		object.remove(QStringLiteral("includeSources"));
	}
	return object;
}

QString shortPathHash(const QString& path)
{
#if defined(Q_OS_WIN)
	const QString normalized = path.toLower();
#else
	const QString normalized = path;
#endif
	return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
}

QJsonArray stringListToJson(const QStringList& values)
{
	QJsonArray array;
	for (const QString& value : values) {
		array.append(value);
	}
	return array;
}

QStringList jsonToStringList(const QJsonValue& value)
{
	QStringList result;
	const QJsonArray array = value.toArray();
	for (const QJsonValue& item : array) {
		const QString text = item.toString().trimmed();
		if (!text.isEmpty() && !result.contains(text)) {
			result.push_back(text);
		}
	}
	return result;
}

QJsonArray compilerToolOverridesToJson(const QVector<CompilerToolPathOverride>& overrides)
{
	QJsonArray array;
	for (const CompilerToolPathOverride& override : overrides) {
		if (override.toolId.trimmed().isEmpty() || override.executablePath.trimmed().isEmpty()) {
			continue;
		}
		QJsonObject object;
		object.insert(QStringLiteral("toolId"), override.toolId.trimmed());
		object.insert(QStringLiteral("executablePath"), override.executablePath.trimmed());
		array.append(object);
	}
	return array;
}

QVector<CompilerToolPathOverride> compilerToolOverridesFromJson(const QJsonValue& value)
{
	QVector<CompilerToolPathOverride> overrides;
	QStringList seen;
	for (const QJsonValue& item : value.toArray()) {
		const QJsonObject object = item.toObject();
		CompilerToolPathOverride override;
		override.toolId = object.value(QStringLiteral("toolId")).toString().trimmed();
		override.executablePath = object.value(QStringLiteral("executablePath")).toString().trimmed();
		if (override.toolId.isEmpty() || override.executablePath.isEmpty() || seen.contains(override.toolId)) {
			continue;
		}
		overrides.push_back(override);
		seen.push_back(override.toolId);
	}
	return overrides;
}

QString trimmedString(const QJsonObject& object, const QString& key)
{
	return object.value(key).toString().trimmed();
}

QJsonObject projectSettingsOverridesToJson(const ProjectSettingsOverride& overrides)
{
	QJsonObject object;
	if (!overrides.selectedInstallationId.trimmed().isEmpty()) {
		object.insert(QStringLiteral("selectedInstallationId"), overrides.selectedInstallationId.trimmed());
	}
	if (!overrides.editorProfileId.trimmed().isEmpty()) {
		object.insert(QStringLiteral("editorProfileId"), overrides.editorProfileId.trimmed());
	}
	if (!overrides.paletteId.trimmed().isEmpty()) {
		object.insert(QStringLiteral("paletteId"), overrides.paletteId.trimmed());
	}
	if (!overrides.compilerProfileId.trimmed().isEmpty()) {
		object.insert(QStringLiteral("compilerProfileId"), overrides.compilerProfileId.trimmed());
	}
	if (overrides.aiFreeModeSet) {
		object.insert(QStringLiteral("aiFreeMode"), overrides.aiFreeMode);
	}
	return object;
}

ProjectSettingsOverride projectSettingsOverridesFromJson(const QJsonValue& value)
{
	ProjectSettingsOverride overrides;
	if (!value.isObject()) {
		return overrides;
	}

	const QJsonObject object = value.toObject();
	overrides.selectedInstallationId = trimmedString(object, QStringLiteral("selectedInstallationId"));
	overrides.editorProfileId = trimmedString(object, QStringLiteral("editorProfileId"));
	overrides.paletteId = trimmedString(object, QStringLiteral("paletteId"));
	overrides.compilerProfileId = trimmedString(object, QStringLiteral("compilerProfileId"));
	if (object.value(QStringLiteral("aiFreeMode")).isBool()) {
		overrides.aiFreeModeSet = true;
		overrides.aiFreeMode = object.value(QStringLiteral("aiFreeMode")).toBool();
	}
	return overrides;
}

QString normalizedChildPath(const QString& path, const QString& rootPath)
{
	const QString trimmed = path.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	const QFileInfo info(trimmed);
	const QString absolutePath = info.isAbsolute() ? info.absoluteFilePath() : QDir(rootPath).absoluteFilePath(trimmed);
	return QDir::cleanPath(absolutePath);
}

QString relativeOrNativePath(const QString& path, const QString& rootPath)
{
	const QString normalized = normalizedChildPath(path, rootPath);
	if (normalized.isEmpty()) {
		return {};
	}
	const QString relative = QDir(rootPath).relativeFilePath(normalized);
	if (!relative.startsWith(QStringLiteral(".."))) {
		return QDir::toNativeSeparators(relative);
	}
	return QDir::toNativeSeparators(normalized);
}

void addCheck(ProjectHealthSummary* summary, const QString& id, const QString& title, const QString& detail, OperationState state)
{
	if (!summary) {
		return;
	}
	summary->checks.push_back({id, title, detail, state});
	if (state == OperationState::Completed) {
		++summary->readyCount;
	} else if (state == OperationState::Failed) {
		++summary->failedCount;
	} else if (state == OperationState::Warning) {
		++summary->warningCount;
	}
}

} // namespace

bool ProjectReleaseSettings::isEmpty() const
{
	return title.trimmed().isEmpty() && version.trimmed().isEmpty() && authors.isEmpty() && description.trimmed().isEmpty()
		&& website.trimmed().isEmpty() && license.trimmed().isEmpty() && packageName.trimmed().isEmpty() && packageFormat.trimmed().isEmpty()
		&& gameFolder.trimmed().isEmpty() && stockSources.isEmpty() && requirements.isEmpty() && include.isEmpty() && exclude.isEmpty()
		&& outputFolder.trimmed().isEmpty() && changelogFile.trimmed().isEmpty() && !includeSources && extraFields.isEmpty();
}

bool ProjectSettingsOverride::isEmpty() const
{
	return selectedInstallationId.trimmed().isEmpty()
		&& editorProfileId.trimmed().isEmpty()
		&& paletteId.trimmed().isEmpty()
		&& compilerProfileId.trimmed().isEmpty()
		&& !aiFreeModeSet;
}

OperationState ProjectHealthSummary::overallState() const
{
	if (failedCount > 0) {
		return OperationState::Failed;
	}
	if (warningCount > 0) {
		return OperationState::Warning;
	}
	if (readyCount > 0) {
		return OperationState::Completed;
	}
	return OperationState::Idle;
}

QString projectManifestDirectoryName()
{
	return QStringLiteral(".vibestudio");
}

QString projectManifestFileName()
{
	return QStringLiteral("project.json");
}

QString projectManifestPath(const QString& projectRootPath)
{
	const QString rootPath = normalizedProjectRootPath(projectRootPath);
	if (rootPath.isEmpty()) {
		return {};
	}
	return QDir(rootPath).filePath(QStringLiteral("%1/%2").arg(projectManifestDirectoryName(), projectManifestFileName()));
}

QString normalizedProjectRootPath(const QString& path)
{
	const QString trimmed = path.trimmed();
	if (trimmed.isEmpty()) {
		return {};
	}
	const QFileInfo info(trimmed);
	const QString absolutePath = info.isAbsolute() ? info.absoluteFilePath() : QDir::current().absoluteFilePath(trimmed);
	return QDir::cleanPath(absolutePath);
}

QString defaultProjectId(const QString& projectRootPath)
{
	const QString rootPath = normalizedProjectRootPath(projectRootPath);
	const QString baseName = QFileInfo(rootPath).fileName().trimmed().toLower().replace(' ', '-').replace('_', '-');
	const QString prefix = baseName.isEmpty() ? QStringLiteral("project") : baseName;
	return QStringLiteral("%1-%2").arg(prefix, shortPathHash(rootPath));
}

ProjectManifest defaultProjectManifest(const QString& projectRootPath, const QString& displayName)
{
	const QString rootPath = normalizedProjectRootPath(projectRootPath);
	const QFileInfo rootInfo(rootPath);

	ProjectManifest manifest;
	manifest.projectId = defaultProjectId(rootPath);
	manifest.displayName = displayName.trimmed().isEmpty() ? (rootInfo.fileName().isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "Untitled Project") : rootInfo.fileName()) : displayName.trimmed();
	manifest.rootPath = rootPath;
	manifest.sourceFolders = {QStringLiteral(".")};
	manifest.packageFolders = {};
	manifest.outputFolder = QStringLiteral("build");
	manifest.tempFolder = QStringLiteral(".vibestudio/tmp");
	manifest.createdUtc = QDateTime::currentDateTimeUtc();
	manifest.updatedUtc = manifest.createdUtc;
	return manifest;
}

bool loadProjectManifest(const QString& projectRootPath, ProjectManifest* manifest, QString* error)
{
	if (error) {
		error->clear();
	}
	if (manifest) {
		*manifest = {};
	}

	const QString manifestPath = projectManifestPath(projectRootPath);
	if (manifestPath.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Project root path is empty.");
		}
		return false;
	}

	QFile file(manifestPath);
	if (!file.exists()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Project manifest does not exist.");
		}
		return false;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Unable to open project manifest.");
		}
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Project manifest JSON is invalid: %1").arg(parseError.errorString());
		}
		return false;
	}

	const QJsonObject object = document.object();
	ProjectManifest loaded;
	loaded.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(ProjectManifest::kSchemaVersion);
	loaded.projectId = object.value(QStringLiteral("projectId")).toString(defaultProjectId(projectRootPath)).trimmed();
	loaded.displayName = object.value(QStringLiteral("displayName")).toString(QFileInfo(projectRootPath).fileName()).trimmed();
	loaded.rootPath = normalizedProjectRootPath(projectRootPath);
	loaded.gameKey = object.value(QStringLiteral("game")).toString().trimmed().toLower();
	loaded.sourceFolders = jsonToStringList(object.value(QStringLiteral("sourceFolders")));
	loaded.packageFolders = jsonToStringList(object.value(QStringLiteral("packageFolders")));
	loaded.outputFolder = object.value(QStringLiteral("outputFolder")).toString(QStringLiteral("build")).trimmed();
	loaded.tempFolder = object.value(QStringLiteral("tempFolder")).toString(QStringLiteral(".vibestudio/tmp")).trimmed();
	loaded.selectedInstallationId = object.value(QStringLiteral("selectedInstallationId")).toString().trimmed();
	loaded.compilerSearchPaths = jsonToStringList(object.value(QStringLiteral("compilerSearchPaths")));
	loaded.compilerToolOverrides = compilerToolOverridesFromJson(object.value(QStringLiteral("compilerToolOverrides")));
	loaded.registeredOutputPaths = jsonToStringList(object.value(QStringLiteral("registeredOutputPaths")));
	loaded.settingsOverrides = projectSettingsOverridesFromJson(object.value(QStringLiteral("settingsOverrides")));
	loaded.release = releaseSettingsFromJson(object.value(QStringLiteral("release")));
	for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
		if (!knownManifestKeys().contains(it.key())) {
			loaded.extraFields.insert(it.key(), it.value());
		}
	}
	if (loaded.settingsOverrides.selectedInstallationId.isEmpty()) {
		loaded.settingsOverrides.selectedInstallationId = loaded.selectedInstallationId;
	}
	if (loaded.selectedInstallationId.isEmpty()) {
		loaded.selectedInstallationId = loaded.settingsOverrides.selectedInstallationId;
	}
	loaded.createdUtc = QDateTime::fromString(object.value(QStringLiteral("createdUtc")).toString(), Qt::ISODate).toUTC();
	loaded.updatedUtc = QDateTime::fromString(object.value(QStringLiteral("updatedUtc")).toString(), Qt::ISODate).toUTC();
	if (loaded.projectId.isEmpty()) {
		loaded.projectId = defaultProjectId(projectRootPath);
	}
	if (loaded.displayName.isEmpty()) {
		loaded.displayName = QFileInfo(loaded.rootPath).fileName();
	}
	if (loaded.sourceFolders.isEmpty()) {
		loaded.sourceFolders = {QStringLiteral(".")};
	}
	if (!loaded.createdUtc.isValid()) {
		loaded.createdUtc = QDateTime::currentDateTimeUtc();
	}
	if (!loaded.updatedUtc.isValid()) {
		loaded.updatedUtc = loaded.createdUtc;
	}

	if (manifest) {
		*manifest = loaded;
	}
	return true;
}

bool saveProjectManifest(const ProjectManifest& manifest, QString* error)
{
	if (error) {
		error->clear();
	}

	ProjectManifest normalized = manifest;
	normalized.rootPath = normalizedProjectRootPath(manifest.rootPath);
	if (normalized.rootPath.isEmpty()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Project root path is empty.");
		}
		return false;
	}
	if (normalized.projectId.trimmed().isEmpty()) {
		normalized.projectId = defaultProjectId(normalized.rootPath);
	}
	if (normalized.displayName.trimmed().isEmpty()) {
		normalized.displayName = QFileInfo(normalized.rootPath).fileName();
	}
	if (normalized.sourceFolders.isEmpty()) {
		normalized.sourceFolders = {QStringLiteral(".")};
	}
	if (normalized.outputFolder.trimmed().isEmpty()) {
		normalized.outputFolder = QStringLiteral("build");
	}
	if (normalized.tempFolder.trimmed().isEmpty()) {
		normalized.tempFolder = QStringLiteral(".vibestudio/tmp");
	}
	normalized.selectedInstallationId = normalized.selectedInstallationId.trimmed();
	normalized.settingsOverrides.selectedInstallationId = normalized.settingsOverrides.selectedInstallationId.trimmed();
	normalized.settingsOverrides.editorProfileId = normalized.settingsOverrides.editorProfileId.trimmed();
	normalized.settingsOverrides.paletteId = normalized.settingsOverrides.paletteId.trimmed();
	normalized.settingsOverrides.compilerProfileId = normalized.settingsOverrides.compilerProfileId.trimmed();
	for (QString& searchPath : normalized.compilerSearchPaths) {
		searchPath = searchPath.trimmed();
	}
	normalized.compilerSearchPaths.removeAll(QString());
	for (CompilerToolPathOverride& override : normalized.compilerToolOverrides) {
		override.toolId = override.toolId.trimmed();
		override.executablePath = override.executablePath.trimmed();
	}
	normalized.compilerToolOverrides.erase(
		std::remove_if(normalized.compilerToolOverrides.begin(), normalized.compilerToolOverrides.end(), [](const CompilerToolPathOverride& override) {
			return override.toolId.isEmpty() || override.executablePath.isEmpty();
		}),
		normalized.compilerToolOverrides.end());
	for (QString& outputPath : normalized.registeredOutputPaths) {
		outputPath = outputPath.trimmed();
	}
	normalized.registeredOutputPaths.removeAll(QString());
	if (normalized.settingsOverrides.selectedInstallationId.isEmpty() && !normalized.selectedInstallationId.isEmpty()) {
		normalized.settingsOverrides.selectedInstallationId = normalized.selectedInstallationId;
	}
	if (normalized.selectedInstallationId.isEmpty()) {
		normalized.selectedInstallationId = normalized.settingsOverrides.selectedInstallationId;
	}
	if (!normalized.createdUtc.isValid()) {
		normalized.createdUtc = QDateTime::currentDateTimeUtc();
	}
	normalized.updatedUtc = QDateTime::currentDateTimeUtc();

	QDir rootDir(normalized.rootPath);
	if (!rootDir.exists()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Project root does not exist.");
		}
		return false;
	}
	if (!rootDir.mkpath(projectManifestDirectoryName())) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Unable to create project metadata directory.");
		}
		return false;
	}

	// Unknown keys first, so the known ones below always win.
	QJsonObject object = normalized.extraFields;
	// Saving writes the current schema; older manifests are upgraded in place.
	object.insert(QStringLiteral("schemaVersion"), std::max(normalized.schemaVersion, ProjectManifest::kSchemaVersion));
	object.insert(QStringLiteral("projectId"), normalized.projectId);
	object.insert(QStringLiteral("displayName"), normalized.displayName);
	if (!normalized.gameKey.trimmed().isEmpty()) {
		object.insert(QStringLiteral("game"), normalized.gameKey.trimmed().toLower());
	} else {
		object.remove(QStringLiteral("game"));
	}
	object.insert(QStringLiteral("sourceFolders"), stringListToJson(normalized.sourceFolders));
	object.insert(QStringLiteral("packageFolders"), stringListToJson(normalized.packageFolders));
	object.insert(QStringLiteral("outputFolder"), normalized.outputFolder);
	object.insert(QStringLiteral("tempFolder"), normalized.tempFolder);
	object.insert(QStringLiteral("selectedInstallationId"), normalized.selectedInstallationId);
	object.insert(QStringLiteral("compilerSearchPaths"), stringListToJson(normalized.compilerSearchPaths));
	object.insert(QStringLiteral("compilerToolOverrides"), compilerToolOverridesToJson(normalized.compilerToolOverrides));
	object.insert(QStringLiteral("registeredOutputPaths"), stringListToJson(normalized.registeredOutputPaths));
	object.insert(QStringLiteral("settingsOverrides"), projectSettingsOverridesToJson(normalized.settingsOverrides));
	const QJsonObject release = releaseSettingsToJson(normalized.release);
	if (!release.isEmpty()) {
		object.insert(QStringLiteral("release"), release);
	} else {
		object.remove(QStringLiteral("release"));
	}
	object.insert(QStringLiteral("createdUtc"), normalized.createdUtc.toUTC().toString(Qt::ISODate));
	object.insert(QStringLiteral("updatedUtc"), normalized.updatedUtc.toUTC().toString(Qt::ISODate));

	// Atomic replacement: an interrupted save leaves the previous manifest.
	QSaveFile file(projectManifestPath(normalized.rootPath));
	const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectManifest", "Unable to write project manifest.");
		}
		return false;
	}
	return true;
}

QString effectiveProjectInstallationId(const ProjectManifest& manifest, const QString& fallbackInstallationId)
{
	const QString overrideId = manifest.settingsOverrides.selectedInstallationId.trimmed();
	if (!overrideId.isEmpty()) {
		return overrideId;
	}
	const QString manifestId = manifest.selectedInstallationId.trimmed();
	return manifestId.isEmpty() ? fallbackInstallationId.trimmed() : manifestId;
}

QString effectiveProjectEditorProfileId(const ProjectManifest& manifest, const QString& fallbackEditorProfileId)
{
	const QString overrideId = manifest.settingsOverrides.editorProfileId.trimmed();
	return overrideId.isEmpty() ? fallbackEditorProfileId.trimmed() : overrideId;
}

QString effectiveProjectPaletteId(const ProjectManifest& manifest, const QString& fallbackPaletteId)
{
	const QString overrideId = manifest.settingsOverrides.paletteId.trimmed();
	return overrideId.isEmpty() ? fallbackPaletteId.trimmed() : overrideId;
}

QString effectiveProjectCompilerProfileId(const ProjectManifest& manifest, const QString& fallbackCompilerProfileId)
{
	const QString overrideId = manifest.settingsOverrides.compilerProfileId.trimmed();
	return overrideId.isEmpty() ? fallbackCompilerProfileId.trimmed() : overrideId;
}

bool effectiveProjectAiFreeMode(const ProjectManifest& manifest, bool fallbackAiFreeMode)
{
	return manifest.settingsOverrides.aiFreeModeSet ? manifest.settingsOverrides.aiFreeMode : fallbackAiFreeMode;
}

QString effectiveProjectGameKey(const ProjectManifest& manifest, const GameInstallationProfile* installation)
{
	if (!manifest.gameKey.trimmed().isEmpty()) {
		return normalizedGameKey(manifest.gameKey);
	}
	if (installation && !installation->gameKey.trimmed().isEmpty()) {
		return normalizedGameKey(installation->gameKey);
	}
	return QStringLiteral("custom");
}

QString defaultReleasePackageFormat(const QString& gameKey)
{
	const QString key = normalizedGameKey(gameKey);
	if (key == QStringLiteral("quake3")) {
		return QStringLiteral("pk3");
	}
	if (key == QStringLiteral("quake") || key == QStringLiteral("quake2")) {
		return QStringLiteral("pak");
	}
	if (key == QStringLiteral("doom") || key == QStringLiteral("heretic-hexen")) {
		return QStringLiteral("wad");
	}
	return QStringLiteral("zip");
}

QString releaseSlug(const QString& text)
{
	QString slug = text.normalized(QString::NormalizationForm_KD).toLower();
	QString result;
	result.reserve(slug.size());
	for (const QChar ch : std::as_const(slug)) {
		if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || ch == QLatin1Char('_')) {
			result.append(ch);
		} else if (ch.isSpace() || ch == QLatin1Char('-') || ch == QLatin1Char('.')) {
			if (!result.isEmpty() && !result.endsWith(QLatin1Char('-'))) {
				result.append(QLatin1Char('-'));
			}
		}
	}
	while (result.endsWith(QLatin1Char('-'))) {
		result.chop(1);
	}
	return result.left(64);
}

ProjectReleaseSettings effectiveProjectReleaseSettings(const ProjectManifest& manifest, const QString& gameKey)
{
	ProjectReleaseSettings release = manifest.release;
	if (release.title.isEmpty()) {
		release.title = manifest.displayName.trimmed().isEmpty() ? QFileInfo(manifest.rootPath).fileName() : manifest.displayName.trimmed();
	}
	if (release.version.isEmpty()) {
		release.version = QStringLiteral("1.0.0");
	}
	if (release.packageName.isEmpty()) {
		release.packageName = releaseSlug(release.title);
		if (release.packageName.isEmpty()) {
			release.packageName = QStringLiteral("release");
		}
	}
	// An empty format stays Automatic: the release planner picks one per
	// game and scope (a single Quake map ships loose, a whole mod as a PAK).
	if (release.gameFolder.isEmpty()) {
		release.gameFolder = gameDefinitionForKey(gameKey).baseGameDirectory;
	}
	if (release.outputFolder.isEmpty()) {
		const QString output = manifest.outputFolder.trimmed().isEmpty() ? QStringLiteral("build") : manifest.outputFolder.trimmed();
		release.outputFolder = QDir::fromNativeSeparators(output) + QStringLiteral("/releases");
	}
	if (release.changelogFile.isEmpty()) {
		release.changelogFile = QStringLiteral("CHANGELOG.md");
	}
	return release;
}

QStringList effectiveProjectCompilerSearchPaths(const ProjectManifest& manifest, const QStringList& fallbackSearchPaths)
{
	QStringList paths = fallbackSearchPaths;
	for (const QString& path : manifest.compilerSearchPaths) {
		const QString normalized = normalizedChildPath(path, manifest.rootPath);
		if (!normalized.isEmpty() && !paths.contains(normalized)) {
			paths.push_back(normalized);
		}
	}
	return paths;
}

QVector<CompilerToolPathOverride> effectiveProjectCompilerToolOverrides(const ProjectManifest& manifest, const QVector<CompilerToolPathOverride>& fallbackOverrides)
{
	QVector<CompilerToolPathOverride> overrides = fallbackOverrides;
	for (const CompilerToolPathOverride& projectOverride : manifest.compilerToolOverrides) {
		if (projectOverride.toolId.trimmed().isEmpty() || projectOverride.executablePath.trimmed().isEmpty()) {
			continue;
		}
		CompilerToolPathOverride normalized = projectOverride;
		normalized.executablePath = normalizedChildPath(projectOverride.executablePath, manifest.rootPath);
		bool replaced = false;
		for (CompilerToolPathOverride& existing : overrides) {
			if (QString::compare(existing.toolId, normalized.toolId, Qt::CaseInsensitive) == 0) {
				existing = normalized;
				replaced = true;
				break;
			}
		}
		if (!replaced) {
			overrides.push_back(normalized);
		}
	}
	return overrides;
}

bool registerProjectOutputPath(ProjectManifest* manifest, const QString& outputPath)
{
	if (!manifest) {
		return false;
	}
	const QString normalized = normalizedChildPath(outputPath, manifest->rootPath);
	if (normalized.isEmpty()) {
		return false;
	}
	const QString relative = QDir(manifest->rootPath).relativeFilePath(normalized);
	const QString stored = relative.startsWith(QStringLiteral("..")) ? normalized : relative;
	for (const QString& existing : manifest->registeredOutputPaths) {
		if (QString::compare(existing, stored, Qt::CaseInsensitive) == 0) {
			return false;
		}
	}
	manifest->registeredOutputPaths.push_back(stored);
	return true;
}

int registerProjectOutputPaths(ProjectManifest* manifest, const QStringList& outputPaths)
{
	int count = 0;
	for (const QString& outputPath : outputPaths) {
		if (registerProjectOutputPath(manifest, outputPath)) {
			++count;
		}
	}
	return count;
}

ProjectHealthSummary buildProjectHealthSummary(const ProjectManifest& manifest, const QString& fallbackInstallationId)
{
	ProjectHealthSummary summary;
	summary.title = manifest.displayName.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "Project") : manifest.displayName;
	summary.detail = manifest.rootPath;

	const QFileInfo rootInfo(manifest.rootPath);
	addCheck(&summary, QStringLiteral("root"), QCoreApplication::translate("VibeStudioProjectManifest", "Project Root"), manifest.rootPath, rootInfo.exists() && rootInfo.isDir() ? OperationState::Completed : OperationState::Failed);
	addCheck(&summary, QStringLiteral("manifest"), QCoreApplication::translate("VibeStudioProjectManifest", "Project Manifest"), projectManifestPath(manifest.rootPath), QFileInfo::exists(projectManifestPath(manifest.rootPath)) ? OperationState::Completed : OperationState::Warning);
	addCheck(&summary, QStringLiteral("schema-version"), QCoreApplication::translate("VibeStudioProjectManifest", "Project Schema"), QCoreApplication::translate("VibeStudioProjectManifest", "Version %1").arg(manifest.schemaVersion), manifest.schemaVersion <= ProjectManifest::kSchemaVersion ? OperationState::Completed : OperationState::Warning);

	for (const QString& sourceFolder : manifest.sourceFolders) {
		const QString path = normalizedChildPath(sourceFolder, manifest.rootPath);
		addCheck(&summary, QStringLiteral("source-folder"), QCoreApplication::translate("VibeStudioProjectManifest", "Source Folder"), relativeOrNativePath(path, manifest.rootPath), QFileInfo::exists(path) ? OperationState::Completed : OperationState::Warning);
	}
	// The project folder is laid out the way the game reads it, so extra
	// package folders are optional, and output and temporary folders appear
	// when something first writes to them: none of these is a problem.
	if (manifest.packageFolders.isEmpty()) {
		addCheck(&summary, QStringLiteral("package-folders"), QCoreApplication::translate("VibeStudioProjectManifest", "Package Folders"), QCoreApplication::translate("VibeStudioProjectManifest", "None: the project folder holds the game files."), OperationState::Idle);
	} else {
		for (const QString& packageFolder : manifest.packageFolders) {
			const QString path = normalizedChildPath(packageFolder, manifest.rootPath);
			addCheck(&summary, QStringLiteral("package-folder"), QCoreApplication::translate("VibeStudioProjectManifest", "Package Folder"), relativeOrNativePath(path, manifest.rootPath), QFileInfo::exists(path) ? OperationState::Completed : OperationState::Warning);
		}
	}

	const QString outputPath = normalizedChildPath(manifest.outputFolder, manifest.rootPath);
	const bool outputExists = QFileInfo::exists(outputPath);
	addCheck(&summary, QStringLiteral("output-folder"), QCoreApplication::translate("VibeStudioProjectManifest", "Output Folder"),
		outputExists ? relativeOrNativePath(outputPath, manifest.rootPath)
					 : QCoreApplication::translate("VibeStudioProjectManifest", "%1 (created when first needed)").arg(relativeOrNativePath(outputPath, manifest.rootPath)),
		outputExists ? OperationState::Completed : OperationState::Idle);
	const QString tempPath = normalizedChildPath(manifest.tempFolder, manifest.rootPath);
	const bool tempExists = QFileInfo::exists(tempPath);
	addCheck(&summary, QStringLiteral("temp-folder"), QCoreApplication::translate("VibeStudioProjectManifest", "Temp Folder"),
		tempExists ? relativeOrNativePath(tempPath, manifest.rootPath)
				   : QCoreApplication::translate("VibeStudioProjectManifest", "%1 (created when first needed)").arg(relativeOrNativePath(tempPath, manifest.rootPath)),
		tempExists ? OperationState::Completed : OperationState::Idle);

	const QString effectiveInstallation = effectiveProjectInstallationId(manifest, fallbackInstallationId);
	addCheck(&summary, QStringLiteral("installation"), QCoreApplication::translate("VibeStudioProjectManifest", "Game Installation"), effectiveInstallation.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "No installation profile linked yet.") : effectiveInstallation, effectiveInstallation.isEmpty() ? OperationState::Warning : OperationState::Completed);
	const QString game = manifest.gameKey.trimmed();
	const bool knownGame = !game.isEmpty() && knownGameKeys().contains(normalizedGameKey(game));
	addCheck(&summary, QStringLiteral("game"), QCoreApplication::translate("VibeStudioProjectManifest", "Target Game"),
		game.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "Not set; the linked installation's game is used.")
			: knownGame ? gameDefinitionForKey(game).displayName : QCoreApplication::translate("VibeStudioProjectManifest", "Unknown game %1.").arg(game),
		game.isEmpty() ? OperationState::Idle : knownGame ? OperationState::Completed : OperationState::Warning);
	addCheck(&summary, QStringLiteral("release"), QCoreApplication::translate("VibeStudioProjectManifest", "Release Settings"),
		manifest.release.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "Not set yet; Package and Release fills in defaults.")
			: QCoreApplication::translate("VibeStudioProjectManifest", "Version %1").arg(manifest.release.version.isEmpty() ? QStringLiteral("1.0.0") : manifest.release.version),
		manifest.release.isEmpty() ? OperationState::Idle : OperationState::Completed);
	addCheck(&summary, QStringLiteral("compiler-overrides"), QCoreApplication::translate("VibeStudioProjectManifest", "Compiler Overrides"), manifest.compilerToolOverrides.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "No project-local compiler executable overrides configured.") : QCoreApplication::translate("VibeStudioProjectManifest", "Compiler executable overrides configured: %1").arg(manifest.compilerToolOverrides.size()), manifest.compilerToolOverrides.isEmpty() ? OperationState::Idle : OperationState::Completed);
	addCheck(&summary, QStringLiteral("registered-outputs"), QCoreApplication::translate("VibeStudioProjectManifest", "Registered Outputs"), manifest.registeredOutputPaths.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "No compiler outputs registered yet.") : QCoreApplication::translate("VibeStudioProjectManifest", "Compiler outputs registered: %1").arg(manifest.registeredOutputPaths.size()), manifest.registeredOutputPaths.isEmpty() ? OperationState::Idle : OperationState::Completed);
	addCheck(&summary, QStringLiteral("settings-overrides"), QCoreApplication::translate("VibeStudioProjectManifest", "Project Settings Overrides"), manifest.settingsOverrides.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "No project-local overrides configured.") : QCoreApplication::translate("VibeStudioProjectManifest", "Project-local overrides are active."), manifest.settingsOverrides.isEmpty() ? OperationState::Idle : OperationState::Completed);
	return summary;
}

QString projectManifestToText(const ProjectManifest& manifest)
{
	QStringList lines;
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Project ID: %1").arg(manifest.projectId);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Name: %1").arg(manifest.displayName);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Root: %1").arg(QDir::toNativeSeparators(manifest.rootPath));
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Schema: %1").arg(manifest.schemaVersion);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Game: %1").arg(manifest.gameKey.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "from the installation") : manifest.gameKey);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Source folders: %1").arg(manifest.sourceFolders.join(QStringLiteral("; ")));
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Package folders: %1").arg(manifest.packageFolders.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.packageFolders.join(QStringLiteral("; ")));
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Output folder: %1").arg(manifest.outputFolder);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Temp folder: %1").arg(manifest.tempFolder);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Installation: %1").arg(manifest.selectedInstallationId.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.selectedInstallationId);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Compiler search paths: %1").arg(manifest.compilerSearchPaths.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.compilerSearchPaths.join(QStringLiteral("; ")));
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Compiler executable overrides: %1").arg(manifest.compilerToolOverrides.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : QString::number(manifest.compilerToolOverrides.size()));
	for (const CompilerToolPathOverride& override : manifest.compilerToolOverrides) {
		lines << QCoreApplication::translate("VibeStudioProjectManifest", "  %1: %2").arg(override.toolId, QDir::toNativeSeparators(override.executablePath));
	}
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Registered compiler outputs: %1").arg(manifest.registeredOutputPaths.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.registeredOutputPaths.join(QStringLiteral("; ")));
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Override installation: %1").arg(manifest.settingsOverrides.selectedInstallationId.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.settingsOverrides.selectedInstallationId);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Override editor profile: %1").arg(manifest.settingsOverrides.editorProfileId.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.settingsOverrides.editorProfileId);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Override palette: %1").arg(manifest.settingsOverrides.paletteId.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.settingsOverrides.paletteId);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Override compiler profile: %1").arg(manifest.settingsOverrides.compilerProfileId.isEmpty() ? QCoreApplication::translate("VibeStudioProjectManifest", "none") : manifest.settingsOverrides.compilerProfileId);
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Override AI-free mode: %1").arg(manifest.settingsOverrides.aiFreeModeSet ? (manifest.settingsOverrides.aiFreeMode ? QCoreApplication::translate("VibeStudioProjectManifest", "enabled") : QCoreApplication::translate("VibeStudioProjectManifest", "disabled")) : QCoreApplication::translate("VibeStudioProjectManifest", "global default"));
	if (!manifest.release.isEmpty()) {
		lines << QCoreApplication::translate("VibeStudioProjectManifest", "Release: %1 %2").arg(manifest.release.title.isEmpty() ? manifest.displayName : manifest.release.title,
			manifest.release.version.isEmpty() ? QStringLiteral("1.0.0") : manifest.release.version);
	}
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Created UTC: %1").arg(manifest.createdUtc.toUTC().toString(Qt::ISODate));
	lines << QCoreApplication::translate("VibeStudioProjectManifest", "Updated UTC: %1").arg(manifest.updatedUtc.toUTC().toString(Qt::ISODate));
	return lines.join('\n');
}

} // namespace vibestudio
