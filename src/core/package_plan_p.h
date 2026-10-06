#pragma once

#include "core/package_staging.h"

#include <QCoreApplication>
#include <QVector>

#include <algorithm>
#include <utility>

namespace vibestudio {

// Private preparation control. Progress counts metadata work, not payload bytes.
// Latch cancellation across nested folder/WAD helpers and callback boundaries.
class PackagePlanWork final {
public:
	PackagePlanWork(const PackageReadControl& control, QString* error, QString phase)
		: m_control(control), m_error(error), m_phase(std::move(phase)) {}
	bool cancelled()
	{
		m_cancelled = m_cancelled || (m_control.isCancelled && m_control.isCancelled());
		if (m_cancelled && m_error) {
			*m_error = QCoreApplication::translate("VibeStudioPackageStaging", "Package plan preparation cancelled.");
		}
		return m_cancelled;
	}
	bool failed() const { return m_failed; }
	bool refuse(const QString& message)
	{
		if (!m_failed && m_error) { *m_error = message; }
		m_failed = true;
		return false;
	}
	bool checkpoint()
	{
		if (cancelled() || m_failed) { return false; }
		if (m_control.progress && m_steps % 256 == 0) { m_control.progress(m_phase, m_steps, 0); }
		++m_steps;
		return !cancelled();
	}
	bool finish()
	{
		if (cancelled() || m_failed) { return false; }
		if (m_control.progress) { m_control.progress(m_phase, m_steps, m_steps); }
		return !cancelled();
	}
	PackageReadControl nestedControl()
	{
		auto result = m_control;
		result.isCancelled = [this] { return cancelled(); };
		return result;
	}

private:
	PackageReadControl m_control;
	QString* m_error;
	QString m_phase;
	qint64 m_steps = 0;
	bool m_cancelled = false;
	bool m_failed = false;
};

// Logical admission for one row/conflict representation and one key index.
// Container overhead is bounded by counts; this is not a heap-size estimate.
class PackagePlanBudget final {
public:
	PackagePlanBudget(const PackageStagingPlanLimits& limits, PackagePlanWork& work) : m_limits(limits), m_work(work) {}
	bool validate()
	{
		if (m_limits.maximumRecords < 1 || m_limits.maximumRecords > PackageStagingPlanLimits::recordCeiling
			|| m_limits.maximumIndexKeys < 1 || m_limits.maximumIndexKeys > PackageStagingPlanLimits::indexKeyCeiling
			|| m_limits.maximumMetadataBytes < 1 || m_limits.maximumMetadataBytes > PackageStagingPlanLimits::metadataCeiling) {
			return m_work.refuse(QCoreApplication::translate("VibeStudioPackageStaging", "Invalid package plan limits."));
		}
		return true;
	}
	bool checkRecords(qsizetype count)
	{
		return count <= m_limits.maximumRecords || m_work.refuse(QCoreApplication::translate("VibeStudioPackageStaging",
			"Package plan exceeds the %1-record limit. Undo or unstage edits, or work with a smaller package.").arg(m_limits.maximumRecords));
	}
	bool addRecord(qint64 bytes)
	{
		if (!checkRecords(m_records + 1) || !checkText(m_bytes, bytes)) { return false; }
		++m_records; m_bytes += bytes; return true;
	}
	void removeRecord(qint64 bytes) { --m_records; m_bytes -= bytes; }
	bool replaceRecord(qint64 before, qint64 after)
	{
		if (!checkText(m_bytes - before, after)) { return false; }
		m_bytes += after - before; return true;
	}
	void resetRows() { m_records = 0; m_bytes = 0; }
	bool addKey(const QString& key)
	{
		if (m_keys >= m_limits.maximumIndexKeys) {
			return m_work.refuse(QCoreApplication::translate("VibeStudioPackageStaging",
				"Package plan exceeds the %1-index-key limit. Use fewer entries or shallower folder paths.").arg(m_limits.maximumIndexKeys));
		}
		const qint64 bytes = textBytes(key);
		if (!checkText(m_keyBytes, bytes)) { return false; }
		++m_keys; m_keyBytes += bytes; return true;
	}
	void removeKey(const QString& key) { --m_keys; m_keyBytes -= textBytes(key); }
	void resetKeys() { m_keys = 0; m_keyBytes = 0; }
	bool checkText(qint64 used, qint64 addition)
	{
		return (addition >= 0 && used <= m_limits.maximumMetadataBytes && addition <= m_limits.maximumMetadataBytes - used)
			|| m_work.refuse(QCoreApplication::translate("VibeStudioPackageStaging",
				"Package plan exceeds the %1-byte text limit. Undo or unstage edits, or use shorter paths.").arg(m_limits.maximumMetadataBytes));
	}
	static qint64 textBytes(const QString& value) { return static_cast<qint64>(value.size()) * 2; }
	static qint64 entryBytes(const PackageStagedEntry& entry)
	{
		return textBytes(entry.virtualPath) + textBytes(entry.source) + textBytes(entry.operationId)
			+ textBytes(entry.sourceFilePath) + textBytes(entry.baseVirtualPath) + textBytes(entry.wadInsertBefore) + textBytes(entry.wadNamespace) + textBytes(entry.unavailableReason);
	}
	static qint64 conflictBytes(const PackageStageConflict& conflict)
	{
		return textBytes(conflict.operationId) + textBytes(conflict.virtualPath) + textBytes(conflict.message);
	}
private:
	PackageStagingPlanLimits m_limits;
	PackagePlanWork& m_work;
	qsizetype m_records = 0, m_keys = 0;
	qint64 m_bytes = 0, m_keyBytes = 0;
};

// Only for already canonical internal paths/keys. Re-normalizing every prefix
// makes deep-tree indexing unnecessarily quadratic in the path length.
inline QString packagePlanParent(const QString& path)
{
	const auto slash = path.lastIndexOf(QLatin1Char('/'));
	return slash < 0 ? QString() : path.left(slash);
}

// Sort a private candidate in bounded runs, then merge with checkpoints. A
// cancelled comparator must never change its ordering or leave a partial cache.
template <typename T, typename Less>
bool sortPackagePlan(QVector<T>* values, Less less, PackagePlanWork& work)
{
	constexpr qsizetype runSize = 256;
	const auto size = values->size();
	for (qsizetype start = 0; start < size; start += runSize) {
		if (!work.checkpoint()) { return false; }
		std::stable_sort(values->begin() + start, values->begin() + qMin(start + runSize, size), less);
	}
	if (!work.checkpoint()) { return false; }
	if (size <= runSize) { return true; }
	QVector<T> merged;
	merged.reserve(size);
	for (qsizetype width = runSize; width < size; width = qMin(width * 2, size)) {
		merged.clear();
		for (qsizetype start = 0; start < size; start += qMin(width * 2, size - start)) {
			qsizetype left = start, right = qMin(start + width, size);
			const auto middle = right, end = qMin(right + width, size);
			while (left < middle || right < end) {
				if (!work.checkpoint()) { return false; }
				const bool takeRight = right < end && (left == middle || less(values->at(right), values->at(left)));
				merged.append(std::move((*values)[takeRight ? right++ : left++]));
			}
		}
		values->swap(merged);
	}
	return !work.cancelled();
}

} // namespace vibestudio
