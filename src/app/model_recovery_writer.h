#pragma once

#include "core/model_recovery.h"

#include <QObject>
#include <QSet>
#include <memory>
#include <optional>

class QThread;

namespace vibestudio
{

// One active checkpoint and one replaceable pending snapshot. Retirement is
// permanent for an ID: a late completion cannot recreate a discarded draft.
class ModelRecoveryWriter final : public QObject
{
  public:
	explicit ModelRecoveryWriter(QString directory, QObject *parent = nullptr);
	~ModelRecoveryWriter() override;
	void checkpoint(const QString &id, quint64 revision, ModelRecoverySnapshot snapshot);
	void retire(const QString &id);
	// Disables queued/active writing while keeping existing committed copies.
	void cancelPending();
	bool busy() const;
	std::function<void(const QString &path, const QString &error)> finished;

  private:
	struct Pending
	{
		QString id;
		quint64 revision = 0;
		ModelRecoverySnapshot snapshot;
	};
	struct Result
	{
		QString id, path, error;
		quint64 revision = 0;
	};
	QString m_directory, m_savedId;
	quint64 m_savedRevision = 0;
	// m_reported: retired copies whose removal failed and was reported. A
	// removal is retried after every checkpoint, but its failure is reported
	// once, not every few seconds.
	QSet<QString> m_retired, m_removals, m_reported;
	std::optional<Pending> m_pending;
	std::shared_ptr<Result> m_result;
	QThread *m_thread = nullptr;
	void startNext();
	void removeRetired();
};

} // namespace vibestudio
