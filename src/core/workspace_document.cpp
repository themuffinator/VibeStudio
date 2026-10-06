#include "core/workspace_document.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>

namespace vibestudio {
namespace {
constexpr qint64 maximumBytes = 1024 * 1024;
bool fail(QString* error, const char* message)
{
	if (error) { *error = QCoreApplication::translate("VibeStudioWorkspace", message); } return false;
}
QString portable(const QString& path, const QString& workspacePath)
{
	return path.isEmpty() ? QString() : QDir(QFileInfo(workspacePath).absolutePath()).relativeFilePath(QFileInfo(path).absoluteFilePath());
}
bool validText(const QJsonValue& value, int maximum = 4096)
{
	if (!value.isString() || value.toString().size() > maximum) { return false; }
	for (const QChar ch : value.toString()) { if (ch.unicode() < 32) { return false; } }
	return true;
}
bool localReference(const QString& path)
{
	// No URIs, drive-relative paths or device namespaces. External local paths
	// and ../ references are intentional: opening never executes their contents.
	if (path.contains(QStringLiteral("://")) || path.startsWith(QStringLiteral("\\\\?\\")) || path.startsWith(QStringLiteral("\\\\.\\"))) { return false; }
	const auto colon = path.indexOf(':');
	return colon < 0 || (colon == 1 && path[0].isLetter() && path.size() > 2 && (path[2] == '/' || path[2] == '\\') && path.lastIndexOf(':') == 1);
}
QByteArray digest(const QByteArray& bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
}

QStringList workspaceModuleIds()
{
	return {QStringLiteral("workspace"), QStringLiteral("levels"), QStringLiteral("models"), QStringLiteral("textures"), QStringLiteral("audio"),
		QStringLiteral("packages"), QStringLiteral("code"), QStringLiteral("shaders"), QStringLiteral("build"), QStringLiteral("settings")};
}

QJsonObject workspaceDocumentJson(const WorkspaceDocument& document, const QString& path)
{
	QJsonArray code;
	for (const auto& file : document.codeFiles) { code.append(portable(file, path)); }
	return {{"format", QStringLiteral("vibestudio-workspace")}, {"version", 1},
		{"project", portable(document.projectPath, path)}, {"package", portable(document.packagePath, path)},
		{"map", portable(document.mapPath, path)}, {"mapName", document.mapName}, {"codeFiles", code},
		{"currentCodeFile", portable(document.currentCodeFile, path)}, {"activeModule", document.activeModule},
		{"assetSelections", document.assetSelections}, {"extensions", document.extensions}};
}

bool parseWorkspaceDocument(const QByteArray& bytes, const QString& path, WorkspaceDocument* document, QString* error)
{
	if (error) { error->clear(); }
	if (!document || path.isEmpty() || bytes.isEmpty() || bytes.size() > maximumBytes) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Workspace files must contain 1–1,048,576 bytes."));
	}
	QJsonParseError parseError;
	const auto json = QJsonDocument::fromJson(bytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "The workspace is not a valid JSON object."));
	}
	const auto object = json.object();
	if (object.value("format") != QStringLiteral("vibestudio-workspace") || !object.value("version").isDouble() || object.value("version").toDouble() != 1) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unsupported workspace format or version. This reader supports version 1."));
	}
	const QSet<QString> keys {"format", "version", "project", "package", "map", "mapName", "codeFiles", "currentCodeFile", "activeModule", "assetSelections", "extensions"};
	for (auto it = object.begin(); it != object.end(); ++it) {
		if (!keys.contains(it.key())) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unknown workspace field. Custom metadata belongs in extensions.")); }
	}
	WorkspaceDocument parsed;
	const auto resolve = [&](const char* key, QString* target) {
		const auto value = object.value(QLatin1String(key));
		if (value.isUndefined()) { return true; }
		if (!validText(value) || !localReference(value.toString())) { return false; }
		const auto relative = QDir::fromNativeSeparators(value.toString());
		*target = relative.isEmpty() ? QString() : QDir::cleanPath(QDir(QFileInfo(path).absolutePath()).absoluteFilePath(relative));
		return true;
	};
	if (!resolve("project", &parsed.projectPath) || !resolve("package", &parsed.packagePath) || !resolve("map", &parsed.mapPath) || !resolve("currentCodeFile", &parsed.currentCodeFile)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Workspace references must be bounded local filesystem paths."));
	}
	if (object.contains("mapName")) {
		if (!validText(object.value("mapName"), 64)) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Invalid workspace map name.")); }
		parsed.mapName = object.value("mapName").toString();
		if (!parsed.mapName.isEmpty() && parsed.mapPath.isEmpty()) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "A workspace map name requires a map path.")); }
	}
	if (object.contains("activeModule")) {
		if (!object.value("activeModule").isString() || !workspaceModuleIds().contains(object.value("activeModule").toString())) {
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unknown workspace module."));
		}
		parsed.activeModule = object.value("activeModule").toString();
	}
	if (object.contains("codeFiles")) {
		const auto value = object.value("codeFiles");
		if (!value.isArray() || value.toArray().size() > 128) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "A workspace supports at most 128 code files.")); }
		for (const auto& entry : value.toArray()) {
			if (!validText(entry) || entry.toString().isEmpty() || !localReference(entry.toString())) {
				return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Invalid workspace code file reference."));
			}
			const auto file = QDir::cleanPath(QDir(QFileInfo(path).absolutePath()).absoluteFilePath(QDir::fromNativeSeparators(entry.toString())));
			if (!parsed.codeFiles.contains(file)) { parsed.codeFiles << file; }
		}
	}
	if (!parsed.currentCodeFile.isEmpty() && !parsed.codeFiles.contains(parsed.currentCodeFile)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "The active code file must belong to the workspace code files."));
	}
	if (object.contains("assetSelections")) {
		if (!object.value("assetSelections").isObject()) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Invalid workspace asset selections.")); }
		parsed.assetSelections = object.value("assetSelections").toObject();
		for (auto it = parsed.assetSelections.begin(); it != parsed.assetSelections.end(); ++it) {
			if (!QStringList{"textures", "models", "audio"}.contains(it.key()) || !validText(it.value()) ||
				!normalizePackageVirtualPath(it.value().toString(), false).isSafe() || parsed.packagePath.isEmpty()) {
				return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Asset selections require a package and safe virtual paths for textures, models or audio."));
			}
		}
	}
	if (object.contains("extensions")) {
		if (!object.value("extensions").isObject()) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Workspace extensions must be an object.")); }
		parsed.extensions = object.value("extensions").toObject();
	}
	*document = std::move(parsed); return true;
}

bool readWorkspaceDocument(const QString& path, WorkspaceDocument* document, QByteArray* revision, QString* error)
{
	QFile file(path);
	if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.size() > maximumBytes) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unable to read the workspace, or it exceeds 1 MiB."));
	}
	const auto bytes = file.read(maximumBytes + 1);
	if (file.error() != QFile::NoError) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unable to read the workspace, or it exceeds 1 MiB.")); }
	if (!parseWorkspaceDocument(bytes, path, document, error)) { return false; }
	if (revision) { *revision = digest(bytes); } return true;
}

bool writeWorkspaceDocument(const QString& path, const WorkspaceDocument& document, const QByteArray& expected, QByteArray* revision, QString* error)
{
	if (error) { error->clear(); }
	const QFileInfo info(path);
	const auto directory = QDir(info.absolutePath()).canonicalPath();
	if (path.isEmpty() || info.suffix().compare(QStringLiteral("vibeworkspace"), Qt::CaseInsensitive) != 0 || directory.isEmpty() || info.isSymLink() ||
		(!expected.isEmpty() && expected.size() != 32)) {
		return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Choose a .vibeworkspace file in an existing folder, with no symbolic-link destination."));
	}
	const auto bytes = QJsonDocument(workspaceDocumentJson(document, info.absoluteFilePath())).toJson();
	WorkspaceDocument checked;
	if (!parseWorkspaceDocument(bytes, info.absoluteFilePath(), &checked, error)) { return false; }
	const auto destination = QDir(directory).filePath(info.fileName());
	QLockFile lock(destination + QStringLiteral(".lock"));
	if (!lock.tryLock(0)) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Another writer is saving this workspace.")); }
	const auto unchanged = [&] {
		QFileInfo current(path);
		if (current.isSymLink() || QDir(current.absolutePath()).canonicalPath() != directory || current.exists() != !expected.isEmpty()) { return false; }
		if (expected.isEmpty()) { return true; }
		WorkspaceDocument existing; QByteArray actual;
		return readWorkspaceDocument(destination, &existing, &actual, error) && actual == expected;
	};
	if (!unchanged()) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "The workspace destination changed or already exists. Review it before replacing it.")); }
	if (expected.isEmpty()) {
		QTemporaryFile file(QDir(directory).filePath(QStringLiteral(".vibeworkspace-XXXXXX.tmp")));
		if (!file.open() || file.write(bytes) != bytes.size() || !file.flush()) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unable to write the workspace.")); }
		file.close();
		if (!unchanged() || !file.rename(destination)) { return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unable to publish the workspace without replacing an existing file.")); }
		file.setAutoRemove(false);
	} else {
		QSaveFile file(destination); file.setDirectWriteFallback(false);
		if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !unchanged() || !file.commit()) {
			return fail(error, QT_TRANSLATE_NOOP("VibeStudioWorkspace", "Unable to commit the workspace; the saved version was retained."));
		}
	}
	if (revision) { *revision = digest(bytes); } return true;
}

QStringList workspaceMissingReferences(const WorkspaceDocument& document)
{
	QStringList paths = document.codeFiles;
	paths << document.projectPath << document.packagePath << document.mapPath;
	QStringList missing;
	for (const auto& path : paths) { if (!path.isEmpty() && !QFileInfo::exists(path) && !missing.contains(path)) { missing << path; } }
	return missing;
}
} // namespace vibestudio
