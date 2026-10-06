#pragma once

#include "core/model_file_io.h"
#include "core/package_archive.h"

namespace vibestudio
{
// Read-only adapter for model geometry, palettes and material images. Every
// read verifies the entire exact entry before publishing bytes, never a prefix
// or a fallback after checksum failure. Names must identify one physical row.
class ModelArchiveReader final : public PackageArchiveReader
{
  public:
	ModelArchiveReader(const PackageArchiveReader &source, ModelWorkControl control = {});
	PackageArchiveFormat format() const override;
	QString sourcePath() const override;
	bool isOpen() const override;
	QVector<PackageEntry> entries() const override;
	bool visitProtectedInputPaths(const std::function<bool(const QString &)> &visitor,
		QString *error = nullptr, const PackageReadControl &control = {}) const override;
	bool readEntryBytes(const QString &path, QByteArray *out, QString *error, qint64 maxBytes = -1) const override;
	bool readEntryAt(qsizetype index, QByteArray *out, QString *error, qint64 maxBytes = -1) const override;

  private:
	const PackageArchiveReader &m_source;
	ModelWorkControl m_control;
	QVector<PackageEntry> m_entries;
};
} // namespace vibestudio
