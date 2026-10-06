#pragma once

#include "core/package_content.h"
#include "core/package_copy_budget.h"
#include <QJsonObject>
#include <memory>

namespace vibestudio {

inline constexpr int PackageCopyStoreSessionLimit = 128;
inline constexpr int PackageCopyStoreEntryLimit = 250000;
inline constexpr int PackageCopyStoreDepthLimit = 128;
inline constexpr int PackageCopyStorePayloadEntryLimit = PackageCopyStoreEntryLimit - PackageCopyStoreSessionLimit - PackageCopyMaximumBatches - 16;

// Limits apply to this physical store across all cooperating studio processes.
// Initial payload reservations include pending work and retained crash sessions.
struct PackageCopyQuota {
	QString directory, error;
	PackageCopyLimits limits{8192ull * 1024 * 1024, 32000, 160000, 256};
	PackageCopyUsage reserved;
	QByteArray policyFingerprint;
	int sessions = 0;
	bool configured = false, cancelled = false;
	[[nodiscard]] bool complete() const { return error.isEmpty() && !cancelled; }
};
PackageCopyQuota inspectPackageCopyQuota(const QString& directory, const PackageReadControl& control = {});
QJsonObject packageCopyQuotaJson(const PackageCopyQuota& quota);
bool configurePackageCopyQuota(const QString& directory, const PackageCopyLimits& limits,
	const QByteArray& expectedPolicyFingerprint, bool dryRun, QString* error = nullptr, const PackageReadControl& control = {});

struct PackageCopySessionInfo {
	QString id, path, error, storageError, leaseError;
	QDateTime createdUtc;
	qint64 bytes = 0;
	int files = 0, entries = 0, batches = 0;
	bool discardAvailable = false;
	QByteArray fingerprint;
	PackageCopyUsage reserved;
	bool reservationKnown = false;
	[[nodiscard]] bool reviewable() const { return storageError.isEmpty() && fingerprint.size() == 32; }
};

struct PackageCopyInventory {
	QString directory, error;
	QVector<PackageCopySessionInfo> sessions;
	qint64 bytes = 0;
	int files = 0, entries = 0;
	bool cancelled = false;
	PackageCopyQuota quota;
	[[nodiscard]] bool complete() const { return error.isEmpty() && !cancelled; }
};

// Resolves the OS temporary root without creating the store. Old unregistered
// vibestudio-copies-* directories are outside this managed store.
QString packageCopyDirectory();
PackageCopyInventory listPackageCopies(const QString& directory, const PackageReadControl& control = {});
QJsonObject packageCopyInventoryJson(const PackageCopyInventory& inventory);

// Both dry runs and deletion require the current metadata review fingerprint
// and native exclusion of live session owners. Inspection/dry runs create no
// files. Deletion is per file, never recursive; cancellation may leave a partial
// cleanup, requiring a fresh review. Tokens do not hash payload contents.
bool discardPackageCopies(const QString& directory, const QString& id, const QByteArray& expectedFingerprint,
	bool dryRun, QString* error = nullptr, const PackageReadControl& control = {});

class PackageCopyStoreReservation;

class PackageCopySession final : public std::enable_shared_from_this<PackageCopySession> {
public:
	static std::shared_ptr<PackageCopySession> create(const QString& directory, QString* error = nullptr,
		const PackageReadControl& control = {});
	~PackageCopySession();
	[[nodiscard]] QString path() const;
	[[nodiscard]] bool isValid() const;
	std::unique_ptr<PackageCopyStoreReservation> reserve(quint64 bytes, qsizetype files, qsizetype entries,
		QString* error = nullptr, const PackageReadControl& control = {});
private:
	struct State;
	explicit PackageCopySession(std::unique_ptr<State> state);
	std::unique_ptr<State> m_state;
};

class PackageCopyStoreReservation final {
public:
	~PackageCopyStoreReservation();
	PackageCopyStoreReservation(const PackageCopyStoreReservation&) = delete;
	PackageCopyStoreReservation& operator=(const PackageCopyStoreReservation&) = delete;
	// Reservations remain until the owning session is removed. A failed metadata
	// update conservatively keeps the pending charge for subsequent review.
	void retain(bool cleanupFailed = false);
private:
	PackageCopyStoreReservation(std::shared_ptr<PackageCopySession> session, PackageCopyUsage usage);
	std::shared_ptr<PackageCopySession> m_session;
	PackageCopyUsage m_usage;
	bool m_pending = true;
	friend class PackageCopySession;
};

// Call after windows/workers/copy leases release their sessions, before Qt
// teardown. Normal cleanup keeps the native owner lease until it finishes.
void waitForPackageCopyCleanup();

} // namespace vibestudio
