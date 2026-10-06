#pragma once

#include "core/package_recovery.h"

#include <QObject>
#include <QSet>
#include <atomic>
#include <optional>

class QThread;

namespace vibestudio {

// One worker and one replaceable pending snapshot. Retirement cancels a write
// and is processed on the same worker before any newer document is checkpointed.
class PackageRecoveryWriter final : public QObject {
public:
	explicit PackageRecoveryWriter(QString directory, QObject* parent = nullptr);
	~PackageRecoveryWriter() override;
	bool checkpoint(const QString& id, const PackageStagingModel& staging, const QString& title, const PackageRecoveryLimits& limits = {});
	void retire(const QString& id);
	[[nodiscard]] bool busy() const;
	[[nodiscard]] int progress() const;
	std::function<void(const QString& id, quint64 revision, const PackageRecoveryWriteResult&)> finished;

private:
	struct Pending { QString id, title; PackageStagingModel staging; PackageRecoveryLimits limits; };
	struct Storage;
	struct Result {
		QString id;
		quint64 revision = 0;
		bool retiring = false;
		std::atomic_bool cancelled {false};
		std::atomic_int progress {0};
		PackageRecoveryWriteResult output;
	};
	void startNext();
	static void run(const std::shared_ptr<Storage>& storage, const std::shared_ptr<Result>& result,
		const std::optional<Pending>& pending);
	QString m_directory, m_savedId;
	quint64 m_savedRevision = 0;
	std::shared_ptr<Storage> m_storage;
	std::shared_ptr<Result> m_result;
	QThread* m_thread = nullptr;
	std::optional<Pending> m_pending;
	QSet<QString> m_retired, m_cleanup;
};

} // namespace vibestudio
