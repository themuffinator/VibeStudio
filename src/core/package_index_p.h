#pragma once

#include "core/package_archive.h"

namespace vibestudio {

// Shared disk-index admission. Writers charge the records/text a reopen will
// retain, including missing parent directories and any later ZIP64 fields.
// Saturates on overflow, latching the flag; never wraps a declared byte total.
void accumulatePackageBytes(quint64 bytes, quint64* total, bool* overflow);

QString packageIndexDuplicateWarning();
bool validPackageIndexLimits(const PackageIndexLimits& limits, QString* error);
bool admitPackageIndex(PackageIndexUsage* usage, const PackageIndexLimits& limits,
	qsizetype entries, qint64 metadataBytes, QString* error);
bool admitPackageIndexPath(PackageIndexUsage* usage, const PackageIndexLimits& limits,
	const QString& path, QString* error);
bool addPackageIndexDirectories(QVector<PackageEntry>* entries, const QString& sourceArchiveId,
	const std::function<bool(const QString&)>& admit, const std::function<bool()>& isCancelled);

// Validated limits only. A partial 64 KiB source chunk still costs a complete
// SHA-256 fingerprint. Rounding the allowance down avoids admitting that chunk.
qint64 packageIndexMaximumFileBytes(const PackageIndexLimits& limits);

} // namespace vibestudio
