#pragma once

#include "core/package_archive.h"
#include "core/package_copy_budget.h"

#include <QTemporaryDir>
#include <memory>

namespace vibestudio {

class PackageCopySession;
class PackageCopyPreparation;

struct PackageCopyRequest {
	// An existing, caller-owned session directory. Each batch gets a fresh child.
	QString parentDirectory;
	// GUI requests reuse an owned managed session, or create one on the worker
	// in this store. Standalone callers may still provide parentDirectory only.
	QString storeDirectory;
	std::shared_ptr<PackageCopySession> session;
	// Exact positions in one immutable planned reader, including folder rows.
	QVector<qsizetype> entryIndexes;
	quint64 maximumBytes = 512ull * 1024 * 1024;
	qsizetype maximumFiles = 2000;
	qsizetype maximumEntries = 10000;
	PackageReadControl control;
	std::shared_ptr<PackageCopyBudget> budget; // Shared by every retained copy in one window.
	PackageCopyLimits sessionLimits;
};

struct PackageCopyResult {
	// Keep this lease while a drag target or an authoring surface uses the paths.
	// Only a succeeded result may be handed off; prepared paths remain private.
	// Discard clears the cancelled result's lease and paths.
	std::shared_ptr<const QTemporaryDir> storage;
	std::shared_ptr<PackageCopySession> session;
	QStringList paths;
	PackageExtractionReport extraction;
	QString error;
	quint64 totalBytes = 0;
	qsizetype fileCount = 0;
	bool cancelled = false;
	[[nodiscard]] bool prepared() const;
	[[nodiscard]] bool succeeded() const;
private:
	std::shared_ptr<PackageCopyPreparation> m_preparation;
	friend PackageCopyResult preparePackageCopyEntries(const PackageArchiveReader&, const PackageCopyRequest&);
	friend bool publishPreparedPackageCopies(PackageCopyResult*);
	friend bool discardPreparedPackageCopies(PackageCopyResult*);
};

// True for paths lexically inside caller-owned copy storage or resolving
// there through links/junctions, including output descendants not created yet.
// Editors use this guard before adopting a supposedly permanent save location.
bool packageCopyStorageContainsPath(const QString& directory, const QString& path);

// Preparation owns a private batch and pending reservations. Paths must not be
// handed to another surface until publication succeeds. Prepare/publish/discard
// run serially on operation threads; discarding may perform filesystem cleanup.
// Dropping the final unpublished result also discards its batch on that thread.
PackageCopyResult preparePackageCopyEntries(const PackageArchiveReader& source, const PackageCopyRequest& request);
bool publishPreparedPackageCopies(PackageCopyResult* result);
bool discardPreparedPackageCopies(PackageCopyResult* result);

// Planning, filesystem checks and verified extraction all run on the caller's
// operation thread. Shared extraction preflight refuses portable-name collisions,
// unsafe paths and links; partial batches are removed before returning failure.
PackageCopyResult copyPackageEntries(const PackageArchiveReader& source, const PackageCopyRequest& request);

} // namespace vibestudio
