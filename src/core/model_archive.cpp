#include "core/model_archive.h"

#include <QCoreApplication>
#include <algorithm>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}
} // namespace
ModelArchiveReader::ModelArchiveReader(const PackageArchiveReader &source, ModelWorkControl control)
	: m_source(source), m_control(std::move(control)), m_entries(source.entries())
{
}
PackageArchiveFormat ModelArchiveReader::format() const { return m_source.format(); }
QString ModelArchiveReader::sourcePath() const { return m_source.sourcePath(); }
bool ModelArchiveReader::isOpen() const { return m_source.isOpen(); }
QVector<PackageEntry> ModelArchiveReader::entries() const { return m_entries; }
bool ModelArchiveReader::visitProtectedInputPaths(const std::function<bool(const QString &)> &visitor,
	QString *error, const PackageReadControl &control) const
{
	return m_source.visitProtectedInputPaths(visitor, error, control);
}
bool ModelArchiveReader::readEntryBytes(const QString &path, QByteArray *out, QString *error, qint64 maxBytes) const
{
	if (error)
	{
		error->clear();
	}
	ModelWorkProgress lookup(m_control, ModelWorkPhase::Reading, error);
	if (!lookup.check())
	{
		return false;
	}
	qsizetype index = -1;
	for (qsizetype i = 0; i < m_entries.size(); ++i)
	{
		if (!lookup.step())
		{
			return false;
		}
		if (m_entries[i].kind != PackageEntryKind::File || m_entries[i].virtualPath.compare(path, Qt::CaseInsensitive) != 0)
		{
			continue;
		}
		if (index >= 0)
		{
			return fail(error, QCoreApplication::translate(
								   "VibeStudioModelMesh",
								   "The package contains repeated asset paths. Rename or remove the ambiguity before loading this model."));
		}
		index = i;
	}
	return readEntryAt(index, out, error, maxBytes);
}
bool ModelArchiveReader::readEntryAt(qsizetype index, QByteArray *out, QString *error, qint64 maxBytes) const
{
	if (error)
	{
		error->clear();
	}
	if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Reading, 0, 0, error))
	{
		return false;
	}
	if (!out || !isOpen() || index < 0 || index >= m_entries.size() || m_entries[index].kind != PackageEntryKind::File ||
		!m_entries[index].readable || !isSafePackageVirtualPath(m_entries[index].virtualPath))
	{
		return fail(error,
					QCoreApplication::translate("VibeStudioModelMesh", "The package asset is missing, unreadable or has an unsafe path."));
	}
	const auto &entry = m_entries[index];
	const auto limit = maxBytes < 0 ? modelFileByteLimit : std::min(maxBytes, modelFileByteLimit);
	if (!entry.sizeBytes || entry.sizeBytes > quint64(limit) || entry.compressedSizeBytes > quint64(modelFileByteLimit))
	{
		return fail(error, QCoreApplication::translate("VibeStudioModelMesh",
													   "The package asset is empty or exceeds the model read limit (at most 64 MiB)."));
	}
	const auto size = qint64(entry.sizeBytes);
	QByteArray bytes;
	QString sinkError;
	const bool read = m_source.streamEntryAt(
		index,
		[&](QByteArrayView chunk)
		{
			if (chunk.size() > size - bytes.size())
			{
				return fail(&sinkError, QCoreApplication::translate("VibeStudioModelMesh", "The package asset exceeds its recorded size."));
			}
			if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Reading, bytes.size(), size, &sinkError))
			{
				return false;
			}
			bytes.append(chunk.data(), chunk.size());
			return true;
		},
		error, m_control.cancelled);
	if (!sinkError.isEmpty())
	{
		return fail(error, sinkError);
	}
	if (!modelWorkCheckpoint(m_control, ModelWorkPhase::Reading, bytes.size(), size, error))
	{
		return false;
	}
	if (!read || bytes.size() != size)
	{
		return error && !error->isEmpty()
				   ? false
				   : fail(error,
						  QCoreApplication::translate("VibeStudioModelMesh", "Unable to verify the complete package asset payload."));
	}
	*out = std::move(bytes);
	return true;
}
} // namespace vibestudio
