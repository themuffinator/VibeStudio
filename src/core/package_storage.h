#pragma once

#include "core/package_content.h"

#include <QVector>

namespace vibestudio {

inline constexpr int PackageStorageEntryLimit = 250000;

struct PackageStorageFile {
	QString relativePath;
	qint64 bytes = 0, modifiedMs = 0, createdMs = 0;
	QByteArray metadataSha256;
	bool temporary = false;
};

// A bounded, nonrecursive inventory of the draft's two-level storage layout.
// The fingerprint covers names, file sizes/times and bounded manifest content;
// it is a review token, not a claim that payload checksums are valid. Locks are
// excluded. Unknown names, directories, links and junctions make it unsafe.
struct PackageStorageSnapshot {
	QString path, error, canonicalPath, canonicalObjects;
	QDateTime rootModified, objectsModified;
	qint64 rootCreated = 0, objectsCreated = 0;
	bool objectsPresent = false;
	QVector<PackageStorageFile> files;
	QByteArray fingerprint, manifestSha256;
	qint64 bytes = 0, objectBytes = 0, temporaryBytes = 0;
	bool manifestPresent = false;
	[[nodiscard]] bool safe() const { return error.isEmpty() && fingerprint.size() == 32; }
};

bool safePackageStoragePath(const QString& path, QString* error = nullptr);
bool packageStorageTemporaryName(const QString& name, const QString& prefix);
PackageStorageSnapshot inspectPackageStorage(const QString& path, const PackageReadControl& control = {},
	int maximumEntries = PackageStorageEntryLimit);
bool packageStorageFileUnchanged(const QString& root, const PackageStorageFile& file);
// Attribute-only check usable while a Windows maintenance handle denies directory
// enumeration. Capture with inspectPackageStorage after taking the writer lock,
// acquire maintenance access, then verify this state before deleting any file.
bool packageStorageLayoutUnchanged(const PackageStorageSnapshot& snapshot);

} // namespace vibestudio
