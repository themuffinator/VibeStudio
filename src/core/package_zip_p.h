#pragma once

#include <QByteArrayView>
#include <QString>

class QIODevice;

namespace vibestudio {

// Bounded wire-format helpers. Views borrow the caller's current central record;
// no helper retains a device, record buffer, or payload.
struct ZipDirectoryMetadata {
	quint64 entries = 0;
	quint64 offset = 0;
	quint64 size = 0;
	quint64 endRecordsOffset = 0;
	qint64 additionalMetadataBytes = 0;
};

struct ZipEntryMetadata {
	QString name;
	quint16 flags = 0;
	quint16 method = 0;
	quint32 crc = 0;
	quint64 compressedSize = 0;
	quint64 size = 0;
	quint64 localOffset = 0;
	quint32 disk = 0;
	bool zip64Sizes = false;
	bool hasZip64 = false;
};

bool readZipDirectoryMetadata(QIODevice& file, qint64 maximumMetadataBytes,
	ZipDirectoryMetadata* directory, QString* error);
bool finishZipDirectory(QIODevice& file, const ZipDirectoryMetadata& directory,
	quint64 consumed, QString* error);
bool readZipEntryMetadata(QByteArrayView record, ZipEntryMetadata* entry, QString* error);
bool readZipLocalMetadata(QIODevice& file, quint64 directoryOffset, const ZipEntryMetadata& entry,
	QByteArrayView wireName, qint64* dataOffset, QString* error);

} // namespace vibestudio
