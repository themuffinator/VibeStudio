#pragma once

#include "core/package_staging.h"

namespace vibestudio {

// A .vibepackage directory is a portable editing document: document.json is
// atomically replaced, while objects/<sha256> contains independent immutable
// payload copies. Version 3 can explicitly retain missing original content as
// history metadata after a repair. Keep the complete directory when sharing.
// Saving never rewrites an existing object and never reads payload paths from
// the manifest. Opening validates every referenced object and history delta.
class PackageDraft final {
public:
	static bool save(const QString& directory, PackageStagingModel* staging, bool overwrite,
		QString* error = nullptr, const PackageReadControl& control = {}, bool dryRun = false);
	static bool load(const QString& directory, PackageStagingModel* staging,
		QString* error = nullptr, const PackageReadControl& control = {});
	static PackageArchive baseArchive(const PackageStagingModel& staging, QString* error = nullptr,
		const PackageReadControl& control = {}, const PackageIndexLimits& limits = {});

private:
	friend class PackageRecoverySession;
	static bool admitMetadata(const QString& root, const PackageStagingModel& staging, QString* error, const PackageReadControl& control);
	static bool saveWithRecovery(const QString& directory, PackageStagingModel* staging, bool overwrite,
		QString* error, const PackageReadControl& control, bool dryRun, const QJsonObject& recovery,
		const QByteArray& expectedManifestSha256 = {}, qint64 maximumAdditionalBytes = -1, bool preflightForWrite = false);
};

} // namespace vibestudio
