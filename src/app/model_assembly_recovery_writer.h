#pragma once

#include "core/model_assembly_recovery.h"

#include <QObject>
#include <optional>

class QThread;

namespace vibestudio
{
// A dirty document owns one writer/lease. One active and one replaceable
// snapshot; retirement waits for in-flight publication before removing it.
class ModelAssemblyRecoveryWriter final : public QObject
{
  public:
	explicit ModelAssemblyRecoveryWriter(QString directory, QObject *parent = nullptr);
	~ModelAssemblyRecoveryWriter() override;
	bool checkpoint(ModelAssemblyRecoverySnapshot snapshot);
	void retire();
	void preserve();
	void pause();
	bool busy() const { return m_thread || m_pending.has_value(); }
	QString path() const { return modelAssemblyRecoveryPath(m_directory, m_id); }
	std::function<void(const QString &path, const QString &error)> finished;
	std::function<void()> retired;

  private:
	struct Result;
	QString m_directory, m_id;
	QByteArray m_savedKey;
	std::optional<ModelAssemblyRecoverySnapshot> m_pending;
	std::shared_ptr<ModelAssemblyRecoverySession> m_session;
	std::shared_ptr<Result> m_result;
	QThread *m_thread = nullptr;
	bool m_retiring = false;
	bool m_preserving = false;
	void startNext();
};
} // namespace vibestudio
