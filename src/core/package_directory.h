#pragma once

#include "core/package_staging.h"

namespace vibestudio {

// Metadata identity of the exact live subtree, including empty directories,
// source occurrences and generated-operation identities. No payload reads.
QString packageDirectoryIdentity(const QVector<PackageStagedEntry>& entries, const QString& directory,
	QString* error = nullptr, const PackageReadControl& control = {}, const PackageStagingPlanLimits& limits = {});
bool isPackageDirectoryOperation(PackageStageOperationType type);
// Applies one whole-tree edit to a candidate. Failure leaves entries unchanged.
bool applyPackageDirectoryOperation(QVector<PackageStagedEntry>* entries, const PackageStageOperation& operation, QString* error, const PackageReadControl& control = {}, const PackageStagingPlanLimits& limits = {});

} // namespace vibestudio
