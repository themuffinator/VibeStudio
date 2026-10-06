#include "core/package_copy_budget.h"

#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>

namespace vibestudio {
namespace {
QString text(const char* value) { return QCoreApplication::translate("PackageCopyBudget", value); }
}

struct PackageCopyBudget::State {
	QMutex mutex;
	PackageCopyUsage usage;
};

QJsonObject packageCopyLimitsJson(const PackageCopyLimits& limits)
{
	return {{QStringLiteral("maximumBytes"), static_cast<qint64>(limits.maximumBytes)},
		{QStringLiteral("maximumFiles"), static_cast<qint64>(limits.maximumFiles)},
		{QStringLiteral("maximumEntries"), static_cast<qint64>(limits.maximumEntries)},
		{QStringLiteral("maximumBatches"), limits.maximumBatches}};
}
QJsonObject packageCopyUsageJson(const PackageCopyUsage& usage)
{
	return {{QStringLiteral("bytes"), static_cast<qint64>(usage.bytes)}, {QStringLiteral("files"), static_cast<qint64>(usage.files)},
		{QStringLiteral("entries"), static_cast<qint64>(usage.entries)}, {QStringLiteral("batches"), usage.batches},
		{QStringLiteral("pendingBatches"), usage.pendingBatches}, {QStringLiteral("cleanupFailedBatches"), usage.cleanupFailedBatches}};
}

PackageCopyBudget::PackageCopyBudget() : m_state(std::make_shared<State>()) {}
PackageCopyUsage PackageCopyBudget::usage() const
{
	const QMutexLocker lock(&m_state->mutex); return m_state->usage;
}
std::unique_ptr<PackageCopyReservation> PackageCopyBudget::reserve(quint64 bytes, qsizetype files, qsizetype entries,
	const PackageCopyLimits& limits, QString* error)
{
	if (error) { error->clear(); }
	if (limits.maximumBytes > static_cast<quint64>(PackageCopyMaximumMiB) * 1024 * 1024
		|| limits.maximumFiles < 1 || limits.maximumFiles > PackageCopyMaximumFiles
		|| limits.maximumEntries < 1 || limits.maximumEntries > PackageCopyMaximumEntries
		|| limits.maximumBatches < 1 || limits.maximumBatches > PackageCopyMaximumBatches || files < 0 || entries < files) {
		if (error) { *error = text(QT_TRANSLATE_NOOP("PackageCopyBudget", "Temporary package copy limits or reservations are invalid.")); }
		return {};
	}
	const QMutexLocker lock(&m_state->mutex);
	auto& usage = m_state->usage;
	QString resource;
	if (usage.bytes > limits.maximumBytes || bytes > limits.maximumBytes - usage.bytes) {
		resource = text(QT_TRANSLATE_NOOP("PackageCopyBudget", "payload bytes"));
	} else if (usage.files > limits.maximumFiles || files > limits.maximumFiles - usage.files) {
		resource = text(QT_TRANSLATE_NOOP("PackageCopyBudget", "files"));
	} else if (usage.entries > limits.maximumEntries || entries > limits.maximumEntries - usage.entries) {
		resource = text(QT_TRANSLATE_NOOP("PackageCopyBudget", "entries"));
	} else if (usage.batches >= limits.maximumBatches) {
		resource = text(QT_TRANSLATE_NOOP("PackageCopyBudget", "batches"));
	}
	if (!resource.isEmpty()) {
		if (error) { *error = text(QT_TRANSLATE_NOOP("PackageCopyBudget", "Temporary package copies exceed this session's limit for %1. Use Extract Selected or adjust Temporary Package Copies limits.")).arg(resource); }
		return {};
	}
	usage.bytes += bytes; usage.files += files; usage.entries += entries; ++usage.batches; ++usage.pendingBatches;
	PackageCopyUsage requested; requested.bytes = bytes; requested.files = files; requested.entries = entries;
	return std::unique_ptr<PackageCopyReservation>(new PackageCopyReservation(m_state, requested));
}

PackageCopyReservation::PackageCopyReservation(std::shared_ptr<PackageCopyBudget::State> state, PackageCopyUsage usage)
	: m_state(std::move(state)), m_usage(usage) {}
PackageCopyReservation::~PackageCopyReservation()
{
	if (!m_pending) { return; }
	const QMutexLocker lock(&m_state->mutex);
	auto& usage = m_state->usage;
	usage.bytes -= m_usage.bytes; usage.files -= m_usage.files; usage.entries -= m_usage.entries;
	--usage.batches; --usage.pendingBatches;
}
void PackageCopyReservation::retain(bool cleanupFailed)
{
	if (!m_pending) { return; }
	const QMutexLocker lock(&m_state->mutex);
	--m_state->usage.pendingBatches;
	if (cleanupFailed) { ++m_state->usage.cleanupFailedBatches; }
	m_pending = false;
}

} // namespace vibestudio
