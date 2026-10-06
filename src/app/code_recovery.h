#pragma once

#include "core/text_recovery.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <memory>

class QThread;
class QWidget;

namespace vibestudio {

// One worker for all tabs. Newer queued snapshots replace older ones, and
// retired documents cannot be recreated by a write already in flight.
class CodeRecoveryWriter final : public QObject {
public:
	explicit CodeRecoveryWriter(QString directory, QObject* parent = nullptr);
	~CodeRecoveryWriter() override;
	bool needsCheckpoint(const QString& id, int revision) const;
	void checkpoint(const QString& id, int revision, TextRecoverySnapshot snapshot);
	void retire(const QString& id);
	bool busy() const { return m_thread != nullptr || !m_pending.isEmpty(); }
	std::function<void(const QString& id, const QString& path, const QString& error)> finished;

private:
	struct Pending { int revision = 0; TextRecoverySnapshot snapshot; };
	struct Result;
	void startNext();
	void removeRetired();
	QString m_directory;
	QHash<QString, Pending> m_pending;
	QHash<QString, int> m_saved;
	QSet<QString> m_retired;
	QThread* m_thread = nullptr;
	std::shared_ptr<Result> m_result;
};

QString chooseTextRecovery(QWidget* parent, const QString& directory);

} // namespace vibestudio
