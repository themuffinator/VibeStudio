#pragma once

#include "core/package_archive.h"
#include <QFile>
#include <memory>

namespace vibestudio::tests {

inline void summaryU16(QByteArray& bytes, quint16 value)
{
	bytes.append(static_cast<char>(value)); bytes.append(static_cast<char>(value >> 8));
}
inline void summaryU32(QByteArray& bytes, quint32 value)
{
	summaryU16(bytes, value & 0xffff); summaryU16(bytes, value >> 16);
}
inline void summaryU64(QByteArray& bytes, quint64 value)
{
	summaryU32(bytes, static_cast<quint32>(value)); summaryU32(bytes, static_cast<quint32>(value >> 32));
}
// Independent, tiny ZIP64 metadata fixtures. Declared payload sizes intentionally
// exceed the one stored byte: summary inspection must never try to allocate them.
inline bool summaryZip(const QString& path, const QVector<QPair<QByteArray, quint64>>& rows)
{
	QByteArray bytes, directory;
	for (const auto& [name, size] : rows) {
		const auto offset = static_cast<quint32>(bytes.size());
		QByteArray extra; summaryU16(extra, 1); summaryU16(extra, 8); summaryU64(extra, size);
		QByteArray localExtra; summaryU16(localExtra, 1); summaryU16(localExtra, 16); summaryU64(localExtra, size); summaryU64(localExtra, 1);
		summaryU32(bytes, 0x04034b50); summaryU16(bytes, 45); summaryU16(bytes, 0); summaryU16(bytes, 0);
		summaryU16(bytes, 0); summaryU16(bytes, 0); summaryU32(bytes, 0x8cdc1683); summaryU32(bytes, 1); summaryU32(bytes, 0xffffffff);
		summaryU16(bytes, static_cast<quint16>(name.size())); summaryU16(bytes, static_cast<quint16>(localExtra.size())); bytes += name; bytes += localExtra; bytes += 'x';
		summaryU32(directory, 0x02014b50); summaryU16(directory, 45); summaryU16(directory, 45); summaryU16(directory, 0); summaryU16(directory, 0);
		summaryU16(directory, 0); summaryU16(directory, 0); summaryU32(directory, 0x8cdc1683); summaryU32(directory, 1); summaryU32(directory, 0xffffffff);
		summaryU16(directory, static_cast<quint16>(name.size())); summaryU16(directory, static_cast<quint16>(extra.size()));
		summaryU16(directory, 0); summaryU16(directory, 0); summaryU16(directory, 0); summaryU32(directory, 0); summaryU32(directory, offset);
		directory += name; directory += extra;
	}
	const auto offset = static_cast<quint32>(bytes.size()); bytes += directory;
	summaryU32(bytes, 0x06054b50); summaryU16(bytes, 0); summaryU16(bytes, 0);
	summaryU16(bytes, static_cast<quint16>(rows.size())); summaryU16(bytes, static_cast<quint16>(rows.size()));
	summaryU32(bytes, static_cast<quint32>(directory.size())); summaryU32(bytes, offset); summaryU16(bytes, 0);
	QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

class SummaryReader final : public PackageArchiveReader {
public:
	QVector<PackageEntry> rows;
	mutable int payloadReads = 0, listings = 0;
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Zip; }
	QString sourcePath() const override { return QStringLiteral("summary-metadata"); }
	bool isOpen() const override { return true; }
	QVector<PackageEntry> entries() const override { ++listings; return rows; }
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { ++payloadReads; return false; }
	void add(const QString& path, quint64 bytes, bool folder = false)
	{
		PackageEntry entry; entry.virtualPath = path; entry.sizeBytes = bytes;
		entry.kind = folder ? PackageEntryKind::Directory : PackageEntryKind::File; rows.append(entry);
	}
};

} // namespace vibestudio::tests
