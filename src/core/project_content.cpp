#include "core/project_content.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace vibestudio {

namespace {

constexpr qint64 kChunkBytes = 64 * 1024;

QString foldedKey(const QString& path)
{
	return path.toCaseFolded();
}

bool isNonContentFileName(const QString& name)
{
	return name.startsWith(QLatin1Char('.')) || name.compare(QStringLiteral("Thumbs.db"), Qt::CaseInsensitive) == 0
		|| name.compare(QStringLiteral("desktop.ini"), Qt::CaseInsensitive) == 0;
}

QString cleanAbsolute(const QString& path)
{
	return path.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool insideOrSame(const QString& folder, const QString& candidate)
{
	if (folder.isEmpty() || candidate.isEmpty()) {
		return false;
	}
	const Qt::CaseSensitivity sensitivity =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
		Qt::CaseInsensitive;
#else
		Qt::CaseSensitive;
#endif
	return candidate.compare(folder, sensitivity) == 0 || candidate.startsWith(folder + QLatin1Char('/'), sensitivity);
}

bool readFileBytes(const QString& path, quint64 expectedSize, QByteArray* out, QString* error, qint64 maxBytes)
{
	if (out) {
		out->clear();
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectContent", "Unable to read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	if (quint64(file.size()) != expectedSize) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectContent", "%1 changed while the project was being read.").arg(QDir::toNativeSeparators(path));
		}
		return false;
	}
	const qint64 limit = maxBytes < 0 ? file.size() : std::min<qint64>(maxBytes, file.size());
	const QByteArray bytes = file.read(limit);
	if (bytes.size() != limit) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioProjectContent", "Unable to read %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	if (out) {
		*out = bytes;
	}
	return true;
}

} // namespace

bool isProjectNonContentFolderName(const QString& name)
{
	static const QSet<QString> names {
		QStringLiteral("__macosx"), QStringLiteral("node_modules"), QStringLiteral("$recycle.bin"), QStringLiteral("system volume information"),
	};
	return name.startsWith(QLatin1Char('.')) || names.contains(name.toLower())
		|| name.endsWith(QStringLiteral(".vibepackage"), Qt::CaseInsensitive);
}

QVector<ProjectContentRoot> projectContentRoots(const ProjectManifest& manifest)
{
	QVector<ProjectContentRoot> roots;
	const QString root = cleanAbsolute(manifest.rootPath);
	if (root.isEmpty()) {
		return roots;
	}
	const auto resolve = [&root](const QString& path) -> QString {
		if (path.trimmed().isEmpty()) {
			return {};
		}
		const QFileInfo info(path.trimmed());
		return QDir::cleanPath(info.isAbsolute() ? info.absoluteFilePath() : QDir(root).absoluteFilePath(path.trimmed()));
	};
	QStringList excluded;
	for (const QString& folder : {manifest.outputFolder, manifest.tempFolder, manifest.release.outputFolder}) {
		const QString path = resolve(folder);
		if (!path.isEmpty() && path != root && insideOrSame(root, path) && !excluded.contains(path)) {
			excluded << path;
		}
	}
	QStringList packages;
	for (const QString& folder : manifest.packageFolders) {
		const QString path = resolve(folder);
		if (!path.isEmpty() && path != root && !packages.contains(path)) {
			packages << path;
			if (insideOrSame(root, path) && !excluded.contains(path)) {
				excluded << path;
			}
		}
	}
	ProjectContentRoot project;
	project.path = root;
	project.layerId = QStringLiteral("project");
	project.label = manifest.displayName.isEmpty() ? QFileInfo(root).fileName() : manifest.displayName;
	project.excludedFolders = excluded;
	roots << project;
	for (int i = 0; i < packages.size(); ++i) {
		ProjectContentRoot package;
		package.path = packages[i];
		package.layerId = QStringLiteral("package-folder:%1").arg(i + 1);
		package.label = QDir(root).relativeFilePath(packages[i]);
		for (const QString& path : std::as_const(excluded)) {
			if (path != packages[i] && insideOrSame(packages[i], path)) {
				package.excludedFolders << path;
			}
		}
		roots << package;
	}
	return roots;
}

bool ProjectContentReader::load(const QVector<ProjectContentRoot>& roots, QString* error, const ProjectContentOptions& options)
{
	m_roots.clear();
	m_entries.clear();
	m_paths.clear();
	m_index.clear();
	m_warnings.clear();
	m_error.clear();
	m_open = false;
	const auto fail = [this, error](const QString& message) {
		m_error = message;
		if (error) {
			*error = message;
		}
		m_entries.clear();
		m_paths.clear();
		m_index.clear();
		return false;
	};
	for (const ProjectContentRoot& input : roots) {
		ProjectContentRoot root = input;
		root.path = cleanAbsolute(root.path);
		for (QString& folder : root.excludedFolders) {
			folder = cleanAbsolute(folder);
		}
		const QFileInfo rootInfo(root.path);
		if (root.path.isEmpty() || !rootInfo.isDir()) {
			m_warnings << QCoreApplication::translate("VibeStudioProjectContent", "Content folder not found: %1").arg(QDir::toNativeSeparators(root.path));
			continue;
		}
		m_roots << root;
		struct Pending { QString path; int depth; };
		QVector<Pending> stack {{root.path, 0}};
		QSet<QString> rootKeys;
		while (!stack.isEmpty()) {
			if (options.isCancelled && options.isCancelled()) {
				return fail(QCoreApplication::translate("VibeStudioProjectContent", "Reading the project was cancelled."));
			}
			const Pending current = stack.takeLast();
			const QFileInfoList children = QDir(current.path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
				QDir::Name | QDir::DirsFirst);
			// Reverse so the stack pops folders in name order.
			for (auto it = children.crbegin(); it != children.crend(); ++it) {
				const QFileInfo& child = *it;
				const QString path = QDir::cleanPath(child.absoluteFilePath());
				if (child.isSymLink() || child.isJunction()) {
					m_warnings << QCoreApplication::translate("VibeStudioProjectContent", "Skipped link %1; links are never packaged.").arg(QDir::toNativeSeparators(path));
					continue;
				}
				if (child.isDir()) {
					if (isProjectNonContentFolderName(child.fileName())) {
						continue;
					}
					bool skip = false;
					for (const QString& excluded : std::as_const(root.excludedFolders)) {
						skip = skip || insideOrSame(excluded, path);
					}
					if (skip) {
						continue;
					}
					if (current.depth + 1 > options.maximumDepth) {
						m_warnings << QCoreApplication::translate("VibeStudioProjectContent", "Skipped %1: folders are nested too deeply.").arg(QDir::toNativeSeparators(path));
						continue;
					}
					stack.push_back({path, current.depth + 1});
					continue;
				}
				if (!child.isFile() || isNonContentFileName(child.fileName())) {
					continue;
				}
				const QString relative = QDir(root.path).relativeFilePath(path);
				const PackageVirtualPath virtualPath = normalizePackageVirtualPath(relative, false);
				if (!virtualPath.isSafe()) {
					m_warnings << QCoreApplication::translate("VibeStudioProjectContent", "Skipped %1: %2").arg(QDir::toNativeSeparators(path), packagePathIssueDisplayName(virtualPath.issue));
					continue;
				}
				const QString key = foldedKey(virtualPath.normalizedPath);
				if (rootKeys.contains(key)) {
					m_warnings << QCoreApplication::translate("VibeStudioProjectContent", "Skipped %1: another file differs from it only by letter case.").arg(QDir::toNativeSeparators(path));
					continue;
				}
				rootKeys.insert(key);
				PackageEntry entry;
				entry.virtualPath = virtualPath.normalizedPath;
				entry.kind = PackageEntryKind::File;
				entry.sizeBytes = quint64(child.size());
				entry.compressedSizeBytes = entry.sizeBytes;
				entry.modifiedUtc = child.lastModified().toUTC();
				entry.storageMethod = QStringLiteral("stored");
				entry.sourceArchiveId = root.label;
				entry.layerId = root.layerId;
				const auto existing = m_index.constFind(key);
				if (existing != m_index.cend()) {
					const qsizetype index = *existing;
					m_warnings << QCoreApplication::translate("VibeStudioProjectContent", "%1 is in more than one content folder; the copy in %2 is used.")
									  .arg(entry.virtualPath, QDir::toNativeSeparators(root.path));
					entry.sourceOrdinal = index;
					m_entries[index] = entry;
					m_paths[index] = path;
					continue;
				}
				if (m_entries.size() >= options.maximumFiles) {
					return fail(QCoreApplication::translate("VibeStudioProjectContent", "The project holds more than %1 files. Narrow its content folders before packaging.").arg(options.maximumFiles));
				}
				entry.sourceOrdinal = m_entries.size();
				m_index.insert(key, m_entries.size());
				m_entries << entry;
				m_paths << path;
			}
		}
	}
	m_open = true;
	return true;
}

QString ProjectContentReader::sourcePath() const
{
	return m_roots.isEmpty() ? QString() : m_roots.first().path;
}

qsizetype ProjectContentReader::indexOf(const QString& virtualPath) const
{
	QString path = virtualPath.trimmed();
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return m_index.value(foldedKey(path), -1);
}

QString ProjectContentReader::absolutePath(qsizetype index) const
{
	return index >= 0 && index < m_paths.size() ? m_paths[index] : QString();
}

QString ProjectContentReader::absolutePathFor(const QString& virtualPath) const
{
	return absolutePath(indexOf(virtualPath));
}

bool ProjectContentReader::readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes) const
{
	const qsizetype index = indexOf(virtualPath);
	if (index < 0) {
		if (out) { out->clear(); }
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "No project file %1.").arg(virtualPath); }
		return false;
	}
	return readEntryAt(index, out, error, maxBytes);
}

bool ProjectContentReader::readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes) const
{
	if (index < 0 || index >= m_entries.size()) {
		if (out) { out->clear(); }
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "The project entry is out of range."); }
		return false;
	}
	return readFileBytes(m_paths[index], m_entries[index].sizeBytes, out, error, maxBytes);
}

bool ProjectContentReader::streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
	const std::function<bool()>& isCancelled) const
{
	if (index < 0 || index >= m_entries.size()) {
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "The project entry is out of range."); }
		return false;
	}
	QFile file(m_paths[index]);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "Unable to read %1: %2").arg(QDir::toNativeSeparators(m_paths[index]), file.errorString()); }
		return false;
	}
	if (quint64(file.size()) != m_entries[index].sizeBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "%1 changed while the project was being read.").arg(QDir::toNativeSeparators(m_paths[index])); }
		return false;
	}
	qint64 remaining = file.size();
	while (remaining > 0) {
		if (isCancelled && isCancelled()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "Reading was cancelled."); }
			return false;
		}
		const QByteArray chunk = file.read(std::min(remaining, kChunkBytes));
		if (chunk.isEmpty()) {
			if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "Unable to read %1: %2").arg(QDir::toNativeSeparators(m_paths[index]), file.errorString()); }
			return false;
		}
		remaining -= chunk.size();
		if (!sink(QByteArrayView(chunk))) {
			if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "Reading was cancelled."); }
			return false;
		}
	}
	return true;
}

bool ProjectContentReader::visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor, QString* error, const PackageReadControl& control) const
{
	Q_UNUSED(error);
	for (const ProjectContentRoot& root : m_roots) {
		if (control.isCancelled && control.isCancelled()) {
			return false;
		}
		if (!visitor(root.path)) {
			return false;
		}
	}
	return true;
}

LayeredPackageReader::LayeredPackageReader(QVector<Layer> layers) : m_layers(std::move(layers))
{
	for (int layer = 0; layer < m_layers.size(); ++layer) {
		const auto& reader = m_layers[layer].reader;
		if (!reader || !reader->isOpen()) {
			continue;
		}
		m_open = true;
		const QVector<PackageEntry> entries = reader->entries();
		for (qsizetype index = 0; index < entries.size(); ++index) {
			PackageEntry entry = entries[index];
			if (entry.kind != PackageEntryKind::File) {
				continue;
			}
			if (entry.layerId.isEmpty()) {
				entry.layerId = m_layers[layer].id;
			}
			const QString key = foldedKey(entry.virtualPath);
			const auto existing = m_index.constFind(key);
			if (existing != m_index.cend()) {
				// A later layer shadows the earlier file at the same path.
				m_entries[*existing] = entry;
				m_origin[*existing] = {layer, index};
				continue;
			}
			m_index.insert(key, m_entries.size());
			m_entries << entry;
			m_origin << QPair<int, qsizetype> {layer, index};
		}
	}
}

QString LayeredPackageReader::sourcePath() const
{
	QStringList paths;
	for (const Layer& layer : m_layers) {
		if (layer.reader && layer.reader->isOpen()) {
			paths << layer.reader->sourcePath();
		}
	}
	return paths.join(QStringLiteral("; "));
}

qsizetype LayeredPackageReader::indexOf(const QString& virtualPath) const
{
	QString path = virtualPath.trimmed();
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
	return m_index.value(foldedKey(path), -1);
}

int LayeredPackageReader::layerOf(qsizetype index) const
{
	return index >= 0 && index < m_origin.size() ? m_origin[index].first : -1;
}

qsizetype LayeredPackageReader::layerIndexOf(qsizetype index) const
{
	return index >= 0 && index < m_origin.size() ? m_origin[index].second : -1;
}

const LayeredPackageReader::Layer* LayeredPackageReader::layer(int index) const
{
	return index >= 0 && index < m_layers.size() ? &m_layers[index] : nullptr;
}

bool LayeredPackageReader::readEntryBytes(const QString& virtualPath, QByteArray* out, QString* error, qint64 maxBytes) const
{
	const qsizetype index = indexOf(virtualPath);
	if (index < 0) {
		if (out) { out->clear(); }
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "No entry %1.").arg(virtualPath); }
		return false;
	}
	return readEntryAt(index, out, error, maxBytes);
}

bool LayeredPackageReader::readEntryAt(qsizetype index, QByteArray* out, QString* error, qint64 maxBytes) const
{
	const int layerIndex = layerOf(index);
	if (layerIndex < 0) {
		if (out) { out->clear(); }
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "The entry is out of range."); }
		return false;
	}
	return m_layers[layerIndex].reader->readEntryAt(layerIndexOf(index), out, error, maxBytes);
}

bool LayeredPackageReader::streamEntryAt(qsizetype index, const std::function<bool(QByteArrayView)>& sink, QString* error,
	const std::function<bool()>& isCancelled) const
{
	const int layerIndex = layerOf(index);
	if (layerIndex < 0) {
		if (error) { *error = QCoreApplication::translate("VibeStudioProjectContent", "The entry is out of range."); }
		return false;
	}
	return m_layers[layerIndex].reader->streamEntryAt(layerIndexOf(index), sink, error, isCancelled);
}

bool LayeredPackageReader::visitProtectedInputPaths(const std::function<bool(const QString&)>& visitor, QString* error, const PackageReadControl& control) const
{
	for (const Layer& layer : m_layers) {
		if (layer.reader && !layer.reader->visitProtectedInputPaths(visitor, error, control)) {
			return false;
		}
	}
	return true;
}

} // namespace vibestudio
