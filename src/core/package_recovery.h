#pragma once

#include "core/package_staging.h"

#include <QJsonObject>
#include <memory>

class QLockFile;

namespace vibestudio {

inline constexpr int PackageRecoveryScanLimit = 128;
inline constexpr qint64 PackageRecoveryMetadataLimit = 64 * 1024 * 1024;
inline constexpr int PackageRecoveryStorageFileLimit = 500000;

struct PackageRecoveryLimits {
	qint64 maximumBytes = 8LL * 1024 * 1024 * 1024;
	int maximumCopies = 32;
};

struct PackageRecoveryInfo {
	QString id, path, title, sourcePath, originalDraftPath;
	PackageArchiveFormat format = PackageArchiveFormat::Unknown;
	QDateTime writtenUtc;
	quint64 revision = 0;
	qint64 metadataBytes = 0;
	int operationCount = 0, historyCount = 0, unavailableBaseCount = 0;
	QByteArray manifestSha256, storageSha256;
	qint64 storageBytes = 0, temporaryBytes = 0;
	int storageFiles = 0;
	bool manifestPresent = false;
	QString error, storageError;
	bool sessionFilePresent = false;
	// Inventory checks metadata only. Restore verifies all referenced payloads and
	// history deltas, including explicit missing-original records, before writing
	// an independent draft; it never opens sourcePath.
	[[nodiscard]] bool readable() const { return error.isEmpty() && manifestSha256.size() == 32 && !id.isEmpty(); }
};

struct PackageRecoveryInventory {
	QVector<PackageRecoveryInfo> records;
	qint64 metadataBytes = 0, storageBytes = 0;
	int storageFiles = 0;
	bool storageComplete = true;
	bool truncated = false, cancelled = false;
	QString error;
};

struct PackageRecoveryWriteResult {
	QString path, error, maintenanceError;
	int unavailableBaseCount = 0;
	QByteArray manifestSha256;
	[[nodiscard]] bool succeeded() const { return !path.isEmpty() && error.isEmpty(); }
};

QString packageRecoveryDirectory();
QString packageRecoveryPath(const QString& directory, const QString& id);
PackageRecoveryInfo inspectPackageRecovery(const QString& path, const PackageReadControl& control = {}, int maximumStorageFiles = 250000);
PackageRecoveryInventory listPackageRecoveries(const QString& directory, const PackageReadControl& control = {});
QJsonObject packageRecoveryInventoryJson(const PackageRecoveryInventory& inventory);

// The editor owns a lease for the entire document lifetime, including intervals
// between writes. Qt's stale-process lock handling permits recovery after a crash.
// A session is used serially; its worker never shares mutable staging state.
class PackageRecoverySession final {
public:
	static std::unique_ptr<PackageRecoverySession> acquire(const QString& directory, const QString& id, QString* error = nullptr);
	~PackageRecoverySession();
	PackageRecoveryWriteResult checkpoint(const PackageStagingModel& staging, const QString& title,
		const PackageReadControl& control = {}, const PackageRecoveryLimits& limits = {});
	bool retire(QString* error = nullptr);

private:
	PackageRecoverySession(QString directory, QString id, std::unique_ptr<QLockFile> lease);
	QString m_directory, m_id;
	std::unique_ptr<QLockFile> m_lease;
	QByteArray m_manifestSha256;
	bool m_retired = false;
};

// Restoration is always Save As to a new .vibepackage directory, outside the
// recovery store and recorded source/draft. The selected digest prevents stale
// selections. The original checkpoint remains available until explicit discard.
bool restorePackageRecovery(const QString& directory, const QString& id, const QByteArray& expectedManifestSha256,
	const QString& destination, PackageStagingModel* restored = nullptr, QString* error = nullptr,
	const PackageReadControl& control = {}, bool dryRun = false);
bool discardPackageRecovery(const QString& directory, const QString& id, const QByteArray& expectedManifestSha256,
	bool dryRun = false, QString* error = nullptr);

// A storage review token also permits explicit cleanup of interrupted copies
// without a committed manifest. Unknown files/links block the entire discard.
bool discardPackageRecoveryStorage(const QString& directory, const QString& id, const QByteArray& expectedStorageSha256,
	bool dryRun = false, QString* error = nullptr);

} // namespace vibestudio
