#pragma once

#include "core/audio_project.h"
#include "core/audio_recovery_store.h"

#include <QMap>
#include <QObject>
#include <QSet>
#include <atomic>
#include <memory>
#include <optional>
#include <variant>

class QThread;
class QLockFile;

namespace vibestudio
{

// Startup reads only bounded directory metadata in a worker. Full validation
// remains in the recovery manager after the user opens it.
class AudioRecoveryDiscovery final : public QObject {
  public:
	explicit AudioRecoveryDiscovery(QObject *parent = nullptr);
	~AudioRecoveryDiscovery() override;
	void start(const QString &directory);
	void cancel();
	[[nodiscard]] bool busy() const { return m_thread != nullptr; }
	std::function<void(const AudioRecoveryInventory &)> finished;

  private:
	QThread *m_thread = nullptr;
	std::shared_ptr<std::atomic_bool> m_cancel;
};

// One background writer with one replaceable pending snapshot. Retiring a
// document also retires an in-flight write, so it cannot resurrect a draft.
class AudioRecoveryWriter final : public QObject {
  public:
	explicit AudioRecoveryWriter(QString directory, QObject *parent = nullptr,
	                             AudioRecoveryKind kind = AudioRecoveryKind::Waveform);
	~AudioRecoveryWriter() override;
	void checkpoint(const QString &id, quint64 revision, AudioProject project);
	void checkpoint(const QString &id, quint64 revision, AudioSessionRecovery session);
	void retire(const QString &id);
	// Detach a reviewed idle copy without deleting it. In-flight writes must
	// finish before the caller may treat those bytes as a retained origin.
	bool retain(const QString &id);
	[[nodiscard]] bool busy() const;
	std::function<void(const QString &, const QString &)> finished;

  private:
	struct Snapshot {
		QString id;
		quint64 revision = 0;
		std::variant<AudioProject, AudioSessionRecovery> document;
	};
	struct Result {
		QString id, path, error;
		quint64 revision = 0;
	};
	void startNext();
	void queue(Snapshot snapshot);
	static QString writeSnapshot(const Snapshot &snapshot, const QString &directory, QString *error = nullptr);
	void removeRetired();
	AudioRecoveryKind m_kind;
	QString m_directory, m_savedId;
	quint64 m_savedRevision = 0;
	std::optional<Snapshot> m_pending;
	QSet<QString> m_retired;
	QSet<QString> m_closedIds;
	QMap<QString, std::shared_ptr<QLockFile>> m_sessions;
	QThread *m_thread = nullptr;
	std::shared_ptr<Result> m_result;
};

} // namespace vibestudio
