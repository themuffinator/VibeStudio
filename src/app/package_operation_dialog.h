#pragma once

#include "core/package_compare.h"
#include "core/package_copy.h"
#include "core/package_validation.h"

#include <functional>

class QDialog;
class QWidget;

namespace vibestudio {

struct IdTechPaletteResolution;

// A successful write is reopened on the worker before the shell adopts it.
// This prevents a second edit from using the replaced archive's old offsets.
struct PackageSaveResult {
	PackageWriteReport report;
	PackageArchive archive;
	PackageStagingModel staging;
	QString reloadError;
	[[nodiscard]] bool ready() const { return report.succeeded() && archive.isOpen() && staging.isLoaded() && reloadError.isEmpty(); }
};

// Reopening must match the committed output hash before the shell adopts it.
// The write report remains authoritative when reload fails or is cancelled.
PackageSaveResult reopenSavedPackage(const PackageWriteReport& report, const PackageReadControl& control = {});

struct PackageLoadResult {
	PackageArchive archive;
	PackageStagingModel staging;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool ready() const { return !cancelled && error.isEmpty() && archive.isOpen() && staging.isLoaded(); }
};

// The edit runs on a private worker snapshot. Capture values only: callbacks
// must not access widgets or the live document. A closed prepared view is valid
// for history recovery; cancellation/failure never exposes a partial edit.
using PackageEditOperation = std::function<bool(PackageStagingModel&, QString*, const PackageReadControl&)>;
struct PackageEditResult {
	PackageStagingModel staging;
	PackageArchive view;
	QString error;
	bool applied = false;
	bool cancelled = false;
	[[nodiscard]] bool ready() const { return applied && !cancelled && error.isEmpty(); }
};
PackageEditResult runPackageEditDialog(QWidget* parent, const PackageStagingModel& plan, const QString& label,
	const PackageEditOperation& edit, const PackageReadControl& control = {});

struct PackageStageFileRequest {
	QString sourcePath;
	QString virtualPath;
	PackageStageConflictResolution resolution = PackageStageConflictResolution::Block;
	bool replace = false;
	int sourceOrdinal = -1;
};

struct PackageStageFilesResult {
	PackageStagingModel staging;
	QStringList errors;
	QStringList acceptedPaths;
	int accepted = 0;
	bool cancelled = false;
};

// Modal event loops keep existing shell handoffs sequential while all source
// reads run on a worker. Cancellation before UI adoption rejects unpublished
// load/staging/palette results, including after worker completion. Committed
// draft saves/restores retain their actual output result.
// Optional control callbacks run on the worker and must be thread-safe.
PackageLoadResult runPackageLoadDialog(QWidget* parent, const QString& path, const PackageReadControl& control = {});
PackageLoadResult runPackageDraftSaveDialog(QWidget* parent, const PackageStagingModel& plan, const QString& path,
	bool overwrite, const PackageReadControl& control = {});
PackageLoadResult runPackageRecoveryRestoreDialog(QWidget* parent, const QString& directory, const QString& id,
	const QByteArray& manifestSha256, const QString& destination, const PackageReadControl& control = {});
PackageStageFilesResult runPackageStagingDialog(QWidget* parent, const PackageStagingModel& plan,
	const QVector<PackageStageFileRequest>& files, const PackageReadControl& control = {});
IdTechPaletteResolution runPackagePaletteDialog(QWidget* parent, const QStringList& sources, const QString& paletteId,
	bool* cancelled = nullptr, const PackageReadControl& control = {});
// The caller passes an immutable archive/plan snapshot. Completed files remain
// on cancellation; each uncommitted file is discarded by the shared service.
PackageExtractionReport runPackageExtractionDialog(QWidget* parent, std::shared_ptr<const PackageArchiveReader> source,
	const PackageExtractionRequest& request);

// Temporary copies use the same extraction checks but expose paths only when
// the whole batch succeeds and the UI accepts it. Late cancellation disposes
// prepared output and pending reservations on a worker. Accepted finalization
// disables Cancel; Close waits. Retain result.storage for the handoff lifetime.
PackageCopyResult runPackageCopyDialog(QWidget* parent, std::shared_ptr<const PackageArchiveReader> source,
	const PackageCopyRequest& request);

// The dialogs own value snapshots and cancel on close. Saving is window-modal
// and keeps the dialog alive until the writer acknowledges cancellation or
// completes its commit. Completion callbacks run on the UI thread while the
// dialog lives; request cancellation/progress callbacks run on the worker.
QDialog* showPackageComparisonDialog(QWidget* parent, const PackageArchive& source, const QString& otherPath,
	const PackageStagingModel* plan = nullptr, std::function<void(const PackageCompareResult&)> finished = {},
	PackageCompareRequest request = {});
QDialog* showPackageSaveDialog(QWidget* parent, const PackageStagingModel& plan, const PackageWriteRequest& request,
	std::function<void(const PackageSaveResult&)> finished = {});
QDialog* showPackageValidationDialog(QWidget* parent, const PackageArchive& source,
	std::function<void(const PackageValidationReport&)> finished = {}, const PackageValidationRequest& request = {});

} // namespace vibestudio
