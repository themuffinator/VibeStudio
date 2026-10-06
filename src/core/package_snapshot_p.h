#pragma once

#include "core/package_archive.h"

namespace vibestudio {

// Admit an immutable listing without reordering its physical entry indexes.
// Missing parent folders count even when the adapter does not expose them.
// Payload backing retains its own content/storage policy; this does not read it.
bool admitPackageSnapshot(const QString& source, const QVector<PackageEntry>& entries,
	const QVector<PackageLoadWarning>& warnings, const PackageIndexLimits& limits,
	const PackageReadControl& control, PackageIndexUsage* usage,
	QVector<PackageEntry>* impliedDirectories, QString* error);

} // namespace vibestudio
