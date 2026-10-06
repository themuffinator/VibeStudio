#pragma once

#include <QJsonObject>
#include <QString>
#include <memory>

namespace vibestudio {

inline constexpr int PackageCopyMaximumMiB = 131072;
inline constexpr qsizetype PackageCopyMaximumFiles = 100000;
inline constexpr qsizetype PackageCopyMaximumEntries = 250000;
inline constexpr int PackageCopyMaximumBatches = 1024;

struct PackageCopyLimits {
	quint64 maximumBytes = 2048ull * 1024 * 1024;
	qsizetype maximumFiles = 8000;
	qsizetype maximumEntries = 40000;
	int maximumBatches = 64;
};

struct PackageCopyUsage {
	quint64 bytes = 0;
	qsizetype files = 0, entries = 0;
	int batches = 0, pendingBatches = 0, cleanupFailedBatches = 0;
};

QJsonObject packageCopyLimitsJson(const PackageCopyLimits& limits);
QJsonObject packageCopyUsageJson(const PackageCopyUsage& usage);
class PackageCopyReservation;

// One studio window shares this budget across documents and copy workers.
// It accounts initial logical payload/entry reservations, not filesystem blocks
// or later edits by a consumer. Successful batches remain charged for its life:
// a native drag target may still be using a path after a document/tab closes.
class PackageCopyBudget final {
public:
	PackageCopyBudget();
	[[nodiscard]] PackageCopyUsage usage() const;
	std::unique_ptr<PackageCopyReservation> reserve(quint64 bytes, qsizetype files, qsizetype entries,
		const PackageCopyLimits& limits, QString* error = nullptr);
private:
	struct State;
	std::shared_ptr<State> m_state;
	friend class PackageCopyReservation;
};

class PackageCopyReservation final {
public:
	~PackageCopyReservation();
	PackageCopyReservation(const PackageCopyReservation&) = delete;
	PackageCopyReservation& operator=(const PackageCopyReservation&) = delete;
	// Commit successful handoffs, or conservatively retain a failed cleanup.
	// Destroy an uncommitted reservation only after its output has been removed.
	void retain(bool cleanupFailed = false);
private:
	PackageCopyReservation(std::shared_ptr<PackageCopyBudget::State> state, PackageCopyUsage usage);
	std::shared_ptr<PackageCopyBudget::State> m_state;
	PackageCopyUsage m_usage;
	bool m_pending = true;
	friend class PackageCopyBudget;
};

} // namespace vibestudio
