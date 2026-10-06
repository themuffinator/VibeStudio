#pragma once

#include "core/package_storage.h"
#include <QJsonObject>

namespace vibestudio {

struct PackageDraftSaveLimits {
	qint64 maximumBytes = 32LL * 1024 * 1024 * 1024;
	int maximumFiles = 200000;
};

struct PackageDraftStorageReview {
	PackageStorageSnapshot storage;
	QVector<PackageStorageFile> reclaimable;
	QString error;
	qint64 retainedBytes = 0, reclaimableBytes = 0;
	int referencedObjects = 0;
	[[nodiscard]] bool complete() const { return storage.safe() && error.isEmpty(); }
};

// Verifies the complete document, undo/redo history and referenced payloads,
// then reviews unreferenced objects/recognized interrupted writes. Read-only:
// even its process-shared reader lease creates no lock file.
PackageDraftStorageReview reviewPackageDraftStorage(const QString& path, const PackageReadControl& control = {});
QJsonObject packageDraftStorageReviewJson(const PackageDraftStorageReview& review);

struct PackageDraftCompaction {
	bool succeeded = false, dryRun = true, readerExclusion = false;
	qint64 reclaimedBytes = 0;
	int reclaimedFiles = 0;
	QString error;
};

// Refuses active document/history/worker readers before removing anything.
// Partial failure/cancellation only removes already reviewed unreachable files;
// the document and all referenced bytes remain unchanged. A dry run reports
// potential reclamation without proving reader exclusion or creating locks.
PackageDraftCompaction compactPackageDraftStorage(const QString& path, const QByteArray& expectedStorageSha256,
	bool dryRun = true, const PackageReadControl& control = {});

} // namespace vibestudio
