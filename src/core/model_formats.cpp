// Shared plumbing for the model format decoders: path helpers, the
// companion-file budget, and the package and file-system companion sources.
#include "core/model_formats_p.h"

#include "core/model_archive.h"
#include "core/package_archive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <memory>

namespace vibestudio {

namespace model_formats {

QString pathDirectory(const QString& path)
{
	QString normalized = path;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int slash = normalized.lastIndexOf(QLatin1Char('/'));
	return slash < 0 ? QString() : normalized.left(slash);
}

QString pathStem(const QString& path)
{
	QString normalized = path;
	normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
	QString name = normalized.mid(normalized.lastIndexOf(QLatin1Char('/')) + 1);
	const int dot = name.lastIndexOf(QLatin1Char('.'));
	return dot > 0 ? name.left(dot) : name;
}

QString joinPath(const QString& directory, const QString& name)
{
	if (directory.isEmpty()) {
		return name;
	}
	return directory.endsWith(QLatin1Char('/')) ? directory + name : directory + QLatin1Char('/') + name;
}

Companions::Companions(const ModelCompanionSource* source, ModelMesh* mesh)
	: m_source(source)
	, m_mesh(mesh)
{
}

bool Companions::canRead() const
{
	return m_source && static_cast<bool>(m_source->read) && !m_exhausted;
}

bool Companions::canList() const
{
	return m_source && static_cast<bool>(m_source->list);
}

bool Companions::exhausted() const
{
	return m_exhausted;
}

bool Companions::read(const QString& path, QByteArray* bytes, QString* error)
{
	if (!m_source || !m_source->read) {
		if (error) { *error = QCoreApplication::translate("VibeStudioModelMesh", "Companion files cannot be read here."); }
		return false;
	}
	if (m_exhausted || m_files >= m_source->maxFiles) {
		m_exhausted = true;
		if (error) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "The model reads more than %1 companion file(s); the rest were left out.")
				.arg(m_source->maxFiles);
		}
		return false;
	}
	QByteArray data;
	QString problem;
	if (!m_source->read(path, &data, &problem)) {
		if (error) { *error = problem; }
		return false;
	}
	if (qint64(data.size()) > m_source->maxBytes - m_bytes) {
		m_exhausted = true;
		if (error) {
			*error = QCoreApplication::translate("VibeStudioModelMesh", "The model's companion files pass %1 MiB; the rest were left out.")
				.arg(m_source->maxBytes / (1024 * 1024));
		}
		return false;
	}
	++m_files;
	m_bytes += data.size();
	if (m_mesh) {
		m_mesh->companionPaths.append(path);
	}
	*bytes = data;
	return true;
}

QStringList Companions::list(const QString& directory, const QStringList& suffixes) const
{
	if (!canList()) {
		return {};
	}
	return m_source->list(directory, suffixes);
}

} // namespace model_formats

ModelCompanionSource modelCompanionsFromArchive(const PackageArchiveReader& archive, const ModelWorkControl& control)
{
	auto reader = std::make_shared<ModelArchiveReader>(archive, control);
	ModelCompanionSource source;
	source.read = [reader](const QString& path, QByteArray* bytes, QString* error) {
		QString normalized = path;
		normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
		while (normalized.startsWith(QLatin1Char('/'))) {
			normalized.remove(0, 1);
		}
		if (reader->readEntryBytes(normalized, bytes, error)) {
			return true;
		}
		// Game file systems match names case-insensitively.
		for (const PackageEntry& entry : reader->entries()) {
			if (entry.kind == PackageEntryKind::File && entry.virtualPath.compare(normalized, Qt::CaseInsensitive) == 0) {
				return reader->readEntryBytes(entry.virtualPath, bytes, error);
			}
		}
		return false;
	};
	source.list = [reader](const QString& directory, const QStringList& suffixes) {
		QString folder = directory;
		folder.replace(QLatin1Char('\\'), QLatin1Char('/'));
		while (folder.endsWith(QLatin1Char('/'))) {
			folder.chop(1);
		}
		QStringList paths;
		for (const PackageEntry& entry : reader->entries()) {
			if (entry.kind != PackageEntryKind::File) {
				continue;
			}
			if (model_formats::pathDirectory(entry.virtualPath).compare(folder, Qt::CaseInsensitive) != 0) {
				continue;
			}
			const QString suffix = QFileInfo(entry.virtualPath).suffix().toLower();
			if (suffixes.contains(suffix)) {
				paths.append(entry.virtualPath);
			}
		}
		std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
		return paths;
	};
	return source;
}

ModelCompanionSource modelCompanionsFromFileSystem(const QString& modelPath)
{
	const QString modelDirectory = QFileInfo(modelPath).absolutePath();
	ModelCompanionSource source;
	source.read = [modelDirectory](const QString& path, QByteArray* bytes, QString* error) {
		const auto readFile = [bytes, error](const QString& file) {
			QFile handle(file);
			if (!handle.exists() || !handle.open(QIODevice::ReadOnly)) {
				return false;
			}
			*bytes = handle.readAll();
			if (error) { error->clear(); }
			return true;
		};
		if (QFileInfo(path).isAbsolute()) {
			if (readFile(path)) {
				return true;
			}
		} else {
			// A game-relative path: try it against the model's folder and every
			// folder above it, so "models/x.gla" resolves from the game folder.
			QDir folder(modelDirectory);
			for (int depth = 0; depth < 32; ++depth) {
				if (readFile(folder.filePath(path))) {
					return true;
				}
				if (!folder.cdUp()) {
					break;
				}
			}
		}
		if (error) { *error = QCoreApplication::translate("VibeStudioModelMesh", "Companion file not found: %1").arg(path); }
		return false;
	};
	source.list = [modelDirectory](const QString& directory, const QStringList& suffixes) {
		// A game-relative folder ("def") is looked for beside the model and in
		// every folder above it, as reads are.
		QDir folder(QFileInfo(directory).isAbsolute() ? directory : QDir(modelDirectory).filePath(directory));
		if (!QFileInfo(directory).isAbsolute()) {
			QDir ancestor(modelDirectory);
			for (int depth = 0; depth < 32; ++depth) {
				if (QDir(ancestor.filePath(directory)).exists()) {
					folder = QDir(ancestor.filePath(directory));
					break;
				}
				if (!ancestor.cdUp()) {
					break;
				}
			}
		}
		QStringList filters;
		for (const QString& suffix : suffixes) {
			// Name filters are case-sensitive on some file systems.
			filters << QStringLiteral("*.") + suffix << QStringLiteral("*.") + suffix.toUpper();
		}
		QStringList paths;
		const QFileInfoList files = folder.entryInfoList(filters, QDir::Files, QDir::Name | QDir::IgnoreCase);
		for (const QFileInfo& file : files) {
			if (suffixes.contains(file.suffix().toLower()) && !paths.contains(file.absoluteFilePath().replace(QLatin1Char('\\'), QLatin1Char('/')))) {
				QString path = file.absoluteFilePath();
				path.replace(QLatin1Char('\\'), QLatin1Char('/'));
				paths.append(path);
			}
		}
		return paths;
	};
	return source;
}

} // namespace vibestudio
