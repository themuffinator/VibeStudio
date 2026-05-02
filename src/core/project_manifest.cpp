#include "core/project_manifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace vibestudio {

namespace {

QString manifestText(const char* source)
{
	return QCoreApplication::translate("VibeStudioProjectManifest", source);
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
	manifest.displayName = displayName.trimmed().isEmpty() ? (rootInfo.fileName().isEmpty() ? manifestText("Untitled Project") : rootInfo.fileName()) : displayName.trimmed();
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
			*error = manifestText("Project root path is empty.");
		}
		return false;
	}

	QFile file(manifestPath);
	if (!file.exists()) {
		if (error) {
			*error = manifestText("Project manifest does not exist.");
		}
		return false;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = manifestText("Unable to open project manifest.");
		}
		return false;
	}

	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		if (error) {
			*error = manifestText("Project manifest JSON is invalid: %1").arg(parseError.errorString());
		}
		return false;
	}

	const QJsonObject object = document.object();
	ProjectManifest loaded;
	loaded.schemaVersion = object.value(QStringLiteral("schemaVersion")).toInt(ProjectManifest::kSchemaVersion);
	loaded.projectId = object.value(QStringLiteral("projectId")).toString(defaultProjectId(projectRootPath)).trimmed();
	loaded.displayName = object.value(QStringLiteral("displayName")).toString(QFileInfo(projectRootPath).fileName()).trimmed();
	loaded.rootPath = normalizedProjectRootPath(projectRootPath);
	loaded.sourceFolders = jsonToStringList(object.value(QStringLiteral("sourceFolders")));
	loaded.packageFolders = jsonToStringList(object.value(QStringLiteral("packageFolders")));
	loaded.outputFolder = object.value(QStringLiteral("outputFolder")).toString(QStringLiteral("build")).trimmed();
	loaded.tempFolder = object.value(QStringLiteral("tempFolder")).toString(QStringLiteral(".vibestudio/tmp")).trimmed();
	loaded.selectedInstallationId = object.value(QStringLiteral("selectedInstallationId")).toString().trimmed();
	loaded.compilerSearchPaths = jsonToStringList(object.value(QStringLiteral("compilerSearchPaths")));
	loaded.compilerToolOverrides = compilerToolOverridesFromJson(object.value(QStringLiteral("compilerToolOverrides")));
	loaded.registeredOutputPaths = jsonToStringList(object.value(QStringLiteral("registeredOutputPaths")));
	loaded.settingsOverrides = projectSettingsOverridesFromJson(object.value(QStringLiteral("settingsOverrides")));
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
			*error = manifestText("Project root path is empty.");
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
			*error = manifestText("Project root does not exist.");
		}
		return false;
	}
	if (!rootDir.mkpath(projectManifestDirectoryName())) {
		if (error) {
			*error = manifestText("Unable to create project metadata directory.");
		}
		return false;
	}

	QJsonObject object;
	object.insert(QStringLiteral("schemaVersion"), normalized.schemaVersion);
	object.insert(QStringLiteral("projectId"), normalized.projectId);
	object.insert(QStringLiteral("displayName"), normalized.displayName);
	object.insert(QStringLiteral("sourceFolders"), stringListToJson(normalized.sourceFolders));
	object.insert(QStringLiteral("packageFolders"), stringListToJson(normalized.packageFolders));
	object.insert(QStringLiteral("outputFolder"), normalized.outputFolder);
	object.insert(QStringLiteral("tempFolder"), normalized.tempFolder);
	object.insert(QStringLiteral("selectedInstallationId"), normalized.selectedInstallationId);
	object.insert(QStringLiteral("compilerSearchPaths"), stringListToJson(normalized.compilerSearchPaths));
	object.insert(QStringLiteral("compilerToolOverrides"), compilerToolOverridesToJson(normalized.compilerToolOverrides));
	object.insert(QStringLiteral("registeredOutputPaths"), stringListToJson(normalized.registeredOutputPaths));
	object.insert(QStringLiteral("settingsOverrides"), projectSettingsOverridesToJson(normalized.settingsOverrides));
	object.insert(QStringLiteral("createdUtc"), normalized.createdUtc.toUTC().toString(Qt::ISODate));
	object.insert(QStringLiteral("updatedUtc"), normalized.updatedUtc.toUTC().toString(Qt::ISODate));

	QFile file(projectManifestPath(normalized.rootPath));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (error) {
			*error = manifestText("Unable to write project manifest.");
		}
		return false;
	}
	file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
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
	summary.title = manifest.displayName.isEmpty() ? manifestText("Project") : manifest.displayName;
	summary.detail = manifest.rootPath;

	const QFileInfo rootInfo(manifest.rootPath);
	addCheck(&summary, QStringLiteral("root"), manifestText("Project Root"), manifest.rootPath, rootInfo.exists() && rootInfo.isDir() ? OperationState::Completed : OperationState::Failed);
	addCheck(&summary, QStringLiteral("manifest"), manifestText("Project Manifest"), projectManifestPath(manifest.rootPath), QFileInfo::exists(projectManifestPath(manifest.rootPath)) ? OperationState::Completed : OperationState::Warning);
	addCheck(&summary, QStringLiteral("schema-version"), manifestText("Project Schema"), manifestText("Version %1").arg(manifest.schemaVersion), manifest.schemaVersion <= ProjectManifest::kSchemaVersion ? OperationState::Completed : OperationState::Warning);

	for (const QString& sourceFolder : manifest.sourceFolders) {
		const QString path = normalizedChildPath(sourceFolder, manifest.rootPath);
		addCheck(&summary, QStringLiteral("source-folder"), manifestText("Source Folder"), relativeOrNativePath(path, manifest.rootPath), QFileInfo::exists(path) ? OperationState::Completed : OperationState::Warning);
	}
	if (manifest.packageFolders.isEmpty()) {
		addCheck(&summary, QStringLiteral("package-folders"), manifestText("Package Folders"), manifestText("No package folders configured yet."), OperationState::Warning);
	} else {
		for (const QString& packageFolder : manifest.packageFolders) {
			const QString path = normalizedChildPath(packageFolder, manifest.rootPath);
			addCheck(&summary, QStringLiteral("package-folder"), manifestText("Package Folder"), relativeOrNativePath(path, manifest.rootPath), QFileInfo::exists(path) ? OperationState::Completed : OperationState::Warning);
		}
	}

	const QString outputPath = normalizedChildPath(manifest.outputFolder, manifest.rootPath);
	addCheck(&summary, QStringLiteral("output-folder"), manifestText("Output Folder"), relativeOrNativePath(outputPath, manifest.rootPath), QFileInfo::exists(outputPath) ? OperationState::Completed : OperationState::Warning);
	const QString tempPath = normalizedChildPath(manifest.tempFolder, manifest.rootPath);
	addCheck(&summary, QStringLiteral("temp-folder"), manifestText("Temp Folder"), relativeOrNativePath(tempPath, manifest.rootPath), QFileInfo::exists(tempPath) ? OperationState::Completed : OperationState::Warning);

	const QString effectiveInstallation = effectiveProjectInstallationId(manifest, fallbackInstallationId);
	addCheck(&summary, QStringLiteral("installation"), manifestText("Game Installation"), effectiveInstallation.isEmpty() ? manifestText("No installation profile linked yet.") : effectiveInstallation, effectiveInstallation.isEmpty() ? OperationState::Warning : OperationState::Completed);
	addCheck(&summary, QStringLiteral("compiler-overrides"), manifestText("Compiler Overrides"), manifest.compilerToolOverrides.isEmpty() ? manifestText("No project-local compiler executable overrides configured.") : manifestText("Compiler executable overrides configured: %1").arg(manifest.compilerToolOverrides.size()), manifest.compilerToolOverrides.isEmpty() ? OperationState::Idle : OperationState::Completed);
	addCheck(&summary, QStringLiteral("registered-outputs"), manifestText("Registered Outputs"), manifest.registeredOutputPaths.isEmpty() ? manifestText("No compiler outputs registered yet.") : manifestText("Compiler outputs registered: %1").arg(manifest.registeredOutputPaths.size()), manifest.registeredOutputPaths.isEmpty() ? OperationState::Idle : OperationState::Completed);
	addCheck(&summary, QStringLiteral("settings-overrides"), manifestText("Project Settings Overrides"), manifest.settingsOverrides.isEmpty() ? manifestText("No project-local overrides configured.") : manifestText("Project-local overrides are active."), manifest.settingsOverrides.isEmpty() ? OperationState::Idle : OperationState::Completed);
	return summary;
}

QString projectManifestToText(const ProjectManifest& manifest)
{
	QStringList lines;
	lines << manifestText("Project ID: %1").arg(manifest.projectId);
	lines << manifestText("Name: %1").arg(manifest.displayName);
	lines << manifestText("Root: %1").arg(QDir::toNativeSeparators(manifest.rootPath));
	lines << manifestText("Schema: %1").arg(manifest.schemaVersion);
	lines << manifestText("Source folders: %1").arg(manifest.sourceFolders.join(QStringLiteral("; ")));
	lines << manifestText("Package folders: %1").arg(manifest.packageFolders.isEmpty() ? manifestText("none") : manifest.packageFolders.join(QStringLiteral("; ")));
	lines << manifestText("Output folder: %1").arg(manifest.outputFolder);
	lines << manifestText("Temp folder: %1").arg(manifest.tempFolder);
	lines << manifestText("Installation: %1").arg(manifest.selectedInstallationId.isEmpty() ? manifestText("none") : manifest.selectedInstallationId);
	lines << manifestText("Compiler search paths: %1").arg(manifest.compilerSearchPaths.isEmpty() ? manifestText("none") : manifest.compilerSearchPaths.join(QStringLiteral("; ")));
	lines << manifestText("Compiler executable overrides: %1").arg(manifest.compilerToolOverrides.isEmpty() ? manifestText("none") : QString::number(manifest.compilerToolOverrides.size()));
	for (const CompilerToolPathOverride& override : manifest.compilerToolOverrides) {
		lines << manifestText("  %1: %2").arg(override.toolId, QDir::toNativeSeparators(override.executablePath));
	}
	lines << manifestText("Registered compiler outputs: %1").arg(manifest.registeredOutputPaths.isEmpty() ? manifestText("none") : manifest.registeredOutputPaths.join(QStringLiteral("; ")));
	lines << manifestText("Override installation: %1").arg(manifest.settingsOverrides.selectedInstallationId.isEmpty() ? manifestText("none") : manifest.settingsOverrides.selectedInstallationId);
	lines << manifestText("Override editor profile: %1").arg(manifest.settingsOverrides.editorProfileId.isEmpty() ? manifestText("none") : manifest.settingsOverrides.editorProfileId);
	lines << manifestText("Override palette: %1").arg(manifest.settingsOverrides.paletteId.isEmpty() ? manifestText("none") : manifest.settingsOverrides.paletteId);
	lines << manifestText("Override compiler profile: %1").arg(manifest.settingsOverrides.compilerProfileId.isEmpty() ? manifestText("none") : manifest.settingsOverrides.compilerProfileId);
	lines << manifestText("Override AI-free mode: %1").arg(manifest.settingsOverrides.aiFreeModeSet ? (manifest.settingsOverrides.aiFreeMode ? manifestText("enabled") : manifestText("disabled")) : manifestText("global default"));
	lines << manifestText("Created UTC: %1").arg(manifest.createdUtc.toUTC().toString(Qt::ISODate));
	lines << manifestText("Updated UTC: %1").arg(manifest.updatedUtc.toUTC().toString(Qt::ISODate));
	return lines.join('\n');
}

} // namespace vibestudio
