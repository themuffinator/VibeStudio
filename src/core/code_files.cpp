#include "core/code_files.h"
#include "core/asset_formats.h"
#include "core/asset_tools.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace vibestudio {
namespace {

const QHash<QString, QString>& languageExtensions()
{
	static const QHash<QString, QString> values = []() {
		QHash<QString, QString> map;
		const auto add = [&](const char* language, const char* extensions) {
			for (const auto& extension : QString::fromLatin1(extensions).split(QLatin1Char(' '))) { map.insert(extension, QString::fromLatin1(language)); }
		};
		add("config", "cfg rc scr skin");
		add("quakec", "qc qh");
		add("shader", "shader");
		add("map-source", "map");
		add("entity-def", "def ent fgd");
		add("ini", "ini arena menu bot conf");
		add("json", "json vsproj vsmanifest");
		add("cpp", "c cc cpp cxx h hh hpp hxx inl");
		add("python", "py");
		add("meson", "meson");
		add("plain-text", "txt md log csv tsv yaml yml toml xml cmake sh bat ps1 lua js ts acs dec zsc");
		return map;
	}();
	return values;
}

} // namespace

QString codeFileLanguageId(const QString& path)
{
	const QFileInfo info(path);
	const QString name = info.fileName().toLower();
	if (name == QStringLiteral("progs.src")) { return QStringLiteral("quakec"); }
	if (name == QStringLiteral("meson.build") || name == QStringLiteral("meson.options")) { return QStringLiteral("meson"); }
	return languageExtensions().value(info.suffix().toLower(), QStringLiteral("plain-text"));
}

bool isCodeFileCandidate(const QString& path)
{
	const QFileInfo info(path);
	const QString name = info.fileName().toLower();
	return !name.isEmpty() && (info.suffix().isEmpty() || languageExtensions().contains(info.suffix().toLower())
		|| name == QStringLiteral("progs.src") || name == QStringLiteral("meson.build") || name == QStringLiteral("meson.options")
		|| name == QStringLiteral("cmakelists.txt") || name == QStringLiteral(".gitignore") || name == QStringLiteral(".editorconfig"));
}

QString projectFileKindId(const QString& path)
{
	if (const auto* format = assetFormatForPath(path)) {
		if (format->id == QStringLiteral("workspace")) { return QStringLiteral("workspace"); }
		if (format->module == QStringLiteral("models") && format->readCapability == QStringLiteral("document")) { return QStringLiteral("model-project"); }
	}
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QStringLiteral("vsaudio")) { return QStringLiteral("audio-project"); }
	if (suffix == QStringLiteral("vtexture")) { return QStringLiteral("texture-project"); }
	if (suffix == QStringLiteral("map")) { return QStringLiteral("map"); }
	if (packageArchiveFormatFromFileName(QFileInfo(path).fileName()) != PackageArchiveFormat::Unknown) { return QStringLiteral("package"); }
	switch (assetPreviewKindForPath(path)) {
	case AssetPreviewKind::Image: return QStringLiteral("image");
	case AssetPreviewKind::Model: return QStringLiteral("model");
	case AssetPreviewKind::Audio: return QStringLiteral("audio");
	default: break;
	}
	return isCodeFileCandidate(path) ? QStringLiteral("code") : QString();
}

bool isExcludedCodeDirectory(const QString& path)
{
	static const QSet<QString> excluded {
		QStringLiteral(".git"), QStringLiteral(".hg"), QStringLiteral(".svn"), QStringLiteral(".agents"),
		QStringLiteral(".vibestudio"), QStringLiteral("node_modules"), QStringLiteral("__pycache__"),
		QStringLiteral("build"), QStringLiteral("dist"), QStringLiteral("external")
	};
	const QString name = QFileInfo(path).fileName().toLower();
	return excluded.contains(name) || name.startsWith(QStringLiteral("builddir"));
}

StudioQueryProperties codeFileQueryProperties(const CodeFileEntry& file)
{
	const QFileInfo info(file.relativePath);
	return {{QStringLiteral("path"), file.relativePath}, {QStringLiteral("name"), info.fileName()},
		{QStringLiteral("folder"), info.path()}, {QStringLiteral("ext"), info.suffix().toLower()},
		{QStringLiteral("size"), QString::number(file.sizeBytes)}, {QStringLiteral("language"), file.languageId}, {QStringLiteral("kind"), file.kind}};
}

CodeFilesResult listCodeFiles(const CodeFilesRequest& request)
{
	CodeFilesResult result;
	result.rootPath = request.rootPath.isEmpty() ? QString() : QDir::cleanPath(QFileInfo(request.rootPath).absoluteFilePath());
	const QFileInfo root(result.rootPath);
	const QString canonical = root.canonicalFilePath();
	if (request.rootPath.trimmed().isEmpty() || !root.isDir() || !root.isReadable() || root.isSymLink() || root.isJunction() || canonical.isEmpty()) {
		result.complete = false;
		result.state = OperationState::Failed;
		result.error = QCoreApplication::translate("VibeStudioCodeFiles", "The project root must be a readable directory, not a link.");
		return result;
	}
	const int maxFiles = std::clamp(request.maxFiles, 1, 20000);
	const int maxEntries = std::clamp(request.maxEntries, 1, 100000);
	const auto cancel = [&]() {
		if (request.isCancelled && request.isCancelled()) { result.cancelled = true; result.complete = false; }
		return result.cancelled;
	};
	const auto warn = [&](const QString& message) {
		result.complete = false;
		if (result.warnings.size() < 32 && !result.warnings.contains(message)) { result.warnings << message; }
	};
	const auto inside = [&](const QString& path) {
		if (path.isEmpty()) { return false; }
		const QString relative = QDir(canonical).relativeFilePath(path);
		return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"));
	};
	bool stopped = false;
	QStringList pending {result.rootPath};
	while (!pending.isEmpty() && !stopped && !cancel()) {
		const QString directory = pending.takeLast();
		const QFileInfo info(directory);
		if (info.isSymLink() || info.isJunction()) { ++result.linksExcluded; continue; }
		if (!info.isDir() || !info.isReadable() || !inside(info.canonicalFilePath())) {
			warn(QCoreApplication::translate("VibeStudioCodeFiles", "Some folders were unavailable while listing project files. Refresh to try again."));
			continue;
		}
		QDirIterator iterator(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
		while (iterator.hasNext() && !cancel()) {
			if (result.entriesVisited >= maxEntries) {
				warn(QCoreApplication::translate("VibeStudioCodeFiles", "The project file list reached its %1 directory-entry limit.").arg(maxEntries));
				stopped = true; break;
			}
			iterator.next();
			++result.entriesVisited;
			const QFileInfo entry = iterator.fileInfo();
			if (entry.isSymLink() || entry.isJunction()) { ++result.linksExcluded; }
			else if (entry.isDir()) {
				if (isExcludedCodeDirectory(entry.filePath())) { ++result.directoriesExcluded; }
				else { pending << entry.filePath(); }
			} else if (entry.isFile() && (request.includeAssets ? !projectFileKindId(entry.fileName()).isEmpty() : isCodeFileCandidate(entry.fileName()))) {
				if (!inside(entry.canonicalFilePath())) {
					warn(QCoreApplication::translate("VibeStudioCodeFiles", "A project file changed location during the scan and was skipped."));
				} else if (result.files.size() >= maxFiles) {
					warn(QCoreApplication::translate("VibeStudioCodeFiles", "The project file list reached its %1 file limit. More files may exist.").arg(maxFiles));
					stopped = true; break;
				} else {
					const QString path = entry.absoluteFilePath();
					result.files.push_back({path, QDir(result.rootPath).relativeFilePath(path), isCodeFileCandidate(path) ? codeFileLanguageId(path) : QString(), entry.size(), projectFileKindId(path)});
				}
			}
			if (request.progress) { request.progress(int(result.files.size()), result.entriesVisited); }
		}
	}
	cancel();
	std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.relativePath < b.relativePath; });
	result.state = result.cancelled ? OperationState::Cancelled : result.complete ? OperationState::Completed : OperationState::Warning;
	return result;
}

} // namespace vibestudio
