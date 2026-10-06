#include "core/package_index_p.h"

#include <QCoreApplication>
#include <QSet>
#include <limits>

namespace vibestudio {

void accumulatePackageBytes(quint64 bytes, quint64* total, bool* overflow)
{
	if (*overflow || bytes > std::numeric_limits<quint64>::max() - *total) {
		*total = std::numeric_limits<quint64>::max(); *overflow = true; return;
	}
	*total += bytes;
}

QString packageIndexDuplicateWarning()
{
	return QCoreApplication::translate("VibeStudioPackageArchive", "Duplicate package entry path; first match will be used for byte reads.");
}

bool validPackageIndexLimits(const PackageIndexLimits& limits, QString* error)
{
	if (limits.maximumEntries < 0 || limits.maximumEntries > PackageIndexLimits::entryCeiling
		|| limits.maximumMetadataBytes < 0 || limits.maximumMetadataBytes > PackageIndexLimits::metadataCeiling
		|| limits.maximumFingerprintBytes < 0 || limits.maximumFingerprintBytes > PackageIndexLimits::fingerprintCeiling
		|| limits.maximumPathDepth < 1 || limits.maximumPathDepth > PackageIndexLimits::depthCeiling) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package indexing limits are outside their supported range."); }
		return false;
	}
	return true;
}

bool admitPackageIndex(PackageIndexUsage* usage, const PackageIndexLimits& limits,
	qsizetype entries, qint64 metadataBytes, QString* error)
{
	if (entries < 0 || entries > limits.maximumEntries - usage->entries
		|| metadataBytes < 0 || metadataBytes > limits.maximumMetadataBytes - usage->metadataBytes) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package index exceeds the limits of %1 entries or %2 metadata bytes. Use a smaller package or folder.").arg(limits.maximumEntries).arg(limits.maximumMetadataBytes); }
		return false;
	}
	usage->entries += entries; usage->metadataBytes += metadataBytes;
	return true;
}

bool admitPackageIndexPath(PackageIndexUsage* usage, const PackageIndexLimits& limits,
	const QString& path, QString* error)
{
	const qsizetype components = 1 + path.count(QLatin1Char('/')) + path.count(QLatin1Char('\\'))
		- (path.endsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('\\')));
	if (components > limits.maximumPathDepth) {
		if (error) { *error = QCoreApplication::translate("VibeStudioPackageArchive", "Package entry paths exceed the maximum indexing depth of %1.").arg(limits.maximumPathDepth); }
		return false;
	}
	return admitPackageIndex(usage, limits, 0, path.size() * qint64(sizeof(QChar)), error);
}

qint64 packageIndexMaximumFileBytes(const PackageIndexLimits& limits)
{
	return (limits.maximumFingerprintBytes / 32) * PackageFileIdentity::chunkBytes;
}

bool addPackageIndexDirectories(QVector<PackageEntry>* entries, const QString& sourceArchiveId,
	const std::function<bool(const QString&)>& admit, const std::function<bool()>& isCancelled)
{
	if (!entries) { return false; }

	QSet<QString> seen;
	for (const PackageEntry& entry : *entries) {
		if (isCancelled && isCancelled()) { return false; }
		seen.insert(entry.virtualPath.toCaseFolded());
	}

	QVector<PackageEntry> directories;
	for (const PackageEntry& entry : *entries) {
		if (isCancelled && isCancelled()) { return false; }
		QString parent = packageVirtualPathParent(entry.virtualPath);
		while (!parent.isEmpty()) {
			if (isCancelled && isCancelled()) { return false; }
			const QString key = parent.toCaseFolded();
			if (!seen.contains(key)) {
				if (admit && !admit(parent)) { return false; }
				seen.insert(key);
				PackageEntry directory;
				directory.virtualPath = parent;
				directory.kind = PackageEntryKind::Directory;
				directory.typeHint = QStringLiteral("directory");
				directory.sourceArchiveId = sourceArchiveId;
				directory.storageMethod = QStringLiteral("synthetic");
				directory.readable = false;
				directories.push_back(directory);
			}
			parent = packageVirtualPathParent(parent);
		}
	}

	*entries += directories;
	return true;
}

} // namespace vibestudio
