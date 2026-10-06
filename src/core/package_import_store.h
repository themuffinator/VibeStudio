#pragma once

#include "core/package_content.h"

#include <QJsonObject>
#include <QVector>

namespace vibestudio {

inline constexpr int PackageImportSessionLimit = 64;
inline constexpr int PackageImportFileLimit = 100000;

// Payload reservations cover in-progress writes as well as completed imports.
// Metadata and filesystem overhead are bounded separately, not counted as payload.
struct PackageImportOptions {
	QString directory;
	qint64 maximumBytes = 8LL * 1024 * 1024 * 1024;
	int maximumFiles = 50000;
};

struct PackageImportInfo {
	QString id, path, error, storageError;
	QDateTime createdUtc;
	qint64 bytes = 0, reservedBytes = 0;
	int files = 0, reservedFiles = 0;
	bool leasePresent = false;
	QByteArray fingerprint;
	[[nodiscard]] bool reviewable() const { return storageError.isEmpty() && fingerprint.size() == 32; }
};

struct PackageImportLockInfo {
	QString relativePath, error;
	QByteArray fingerprint;
	qint64 bytes = 0;
	bool exists = false;
	[[nodiscard]] bool reviewable() const { return exists && error.isEmpty() && fingerprint.size() == 32; }
};

struct PackageImportInventory {
	QString directory, error;
	QVector<PackageImportInfo> sessions;
	QVector<PackageImportLockInfo> locks;
	qint64 bytes = 0, reservedBytes = 0;
	int files = 0, reservedFiles = 0;
	bool cancelled = false, truncated = false;
	[[nodiscard]] bool complete() const { return error.isEmpty() && !cancelled && !truncated; }
};

// Default store uses the resolved OS temporary directory. It is not created by
// inspection, CLI diagnostics, dry runs or this path accessor.
QString packageImportDirectory();
PackageImportInventory listPackageImports(const QString& directory, const PackageReadControl& control = {});
QJsonObject packageImportInventoryJson(const PackageImportInventory& inventory);

// Only the store/session QLockFile names and their stale-removal guards are
// accepted. Inspection creates nothing. Release requires the same native file
// identity/content and OS exclusion of any active owner, including for dry runs.
PackageImportLockInfo inspectPackageImportLock(const QString& directory, const QString& relativePath,
	const PackageReadControl& control = {});
bool releasePackageImportLock(const QString& directory, const QString& relativePath, const QByteArray& expectedFingerprint,
	bool dryRun, QString* error = nullptr, const PackageReadControl& control = {});

// Call after documents/readers/workers release their owned imports and before
// destroying the application. This drains the dedicated cleanup queue only.
void waitForPackageImportCleanup();

// A dry run verifies the reviewed storage without creating locks. Lease presence
// is reported by inventory; only a write can prove the session is no longer live.
bool discardPackageImports(const QString& directory, const QString& id, const QByteArray& expectedFingerprint,
	bool dryRun, QString* error = nullptr, const PackageReadControl& control = {});

class PackageImportReservation final {
public:
	~PackageImportReservation();
	[[nodiscard]] QString path() const;
	[[nodiscard]] QString directory() const;
	std::shared_ptr<const PackageContentStorage> retain(const PackageFileIdentity& identity);
private:
	struct State;
	explicit PackageImportReservation(std::unique_ptr<State> state);
	std::unique_ptr<State> m_state;
	friend std::unique_ptr<PackageImportReservation> reservePackageImport(const PackageFileIdentityPtr&,
		QString*, const PackageReadControl&, const QString&);
};

// Reservation is committed before payload creation. A failed/interrupted copy
// cannot cause another cooperating session to reuse its reserved disk budget.
std::unique_ptr<PackageImportReservation> reservePackageImport(const PackageFileIdentityPtr& source,
	QString* error, const PackageReadControl& control = {}, const QString& directory = {});

} // namespace vibestudio
