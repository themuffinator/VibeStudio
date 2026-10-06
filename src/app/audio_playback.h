#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <memory>
#include <functional>

namespace vibestudio
{

// Backends report the session supplied to open(), even in delayed callbacks.
// This boundary lets lifecycle tests run without a real audio device.
class AudioPlaybackBackend : public QObject
{
	Q_OBJECT
public:
	enum class State { Stopped, Playing, Paused };
	using QObject::QObject;
	virtual void open(const QByteArray& bytes, const QString& fileName, quint64 session, float volume, bool loop) = 0;
	virtual void clear() = 0;
	// Completion runs on this thread after output ownership has been released.
	// Asynchronous backends override this; the default clear is synchronous.
	virtual void clearAndWait(QObject* context, std::function<void()> complete);
	virtual void play() = 0;
	virtual void pause() = 0;
	virtual void seek(qint64 milliseconds) = 0;
	virtual void setVolume(float volume) = 0;
	virtual void setLoop(bool enabled) = 0;

Q_SIGNALS:
	void ready(quint64 session, bool seekable);
	void stateChanged(quint64 session, State state);
	void positionChanged(quint64 session, qint64 milliseconds);
	void durationChanged(quint64 session, qint64 milliseconds);
	void seekableChanged(quint64 session, bool seekable);
	void stalledChanged(quint64 session, bool stalled);
	void ended(quint64 session);
	void failed(quint64 session, const QString& message);
};

// Owns a prepared, bounded selection. Initial boundaries are exact frames;
// seeking within it follows the backend's millisecond position contract.
class AudioPlayback final : public QObject
{
	Q_OBJECT
public:
	enum class State { Unavailable, Stopped, Loading, Playing, Paused, Error };
	Q_ENUM(State)
	explicit AudioPlayback(QObject* parent = nullptr,
	                       std::unique_ptr<AudioPlaybackBackend> backend = {}, int timeoutMs = 30000);
	~AudioPlayback() override;
	bool start(const QByteArray& wav, qint64 first, qint64 end, int sampleRate);
	// Browser media retains its native codec. Duration is a header hint until
	// the backend reports it; zero means unknown. Its position unit is ms.
	bool startMedia(const QByteArray& bytes, const QString& fileName, qint64 durationMs = 0, qint64 startMs = 0);
	bool seekToMilliseconds(qint64 milliseconds);
	[[nodiscard]] qint64 positionMilliseconds() const;
	[[nodiscard]] qint64 durationMilliseconds() const;
	void pause();
	void resume();
	void stop();
	void stopAndWait(QObject* context, std::function<void(bool)> complete);
	bool seekToFrame(qint64 frame);
	void setVolume(float volume);
	void setLoop(bool enabled);
	[[nodiscard]] bool available() const { return bool(m_backend); }
	[[nodiscard]] State state() const { return m_state; }
	[[nodiscard]] bool active() const;
	[[nodiscard]] bool canSeek() const;
	[[nodiscard]] bool stalled() const { return m_stalled; }
	[[nodiscard]] qint64 firstFrame() const { return m_first; }
	[[nodiscard]] qint64 endFrame() const { return m_end; }
	[[nodiscard]] qint64 positionFrame() const { return m_position; }
	[[nodiscard]] QString errorString() const { return m_error; }

Q_SIGNALS:
	void changed();
	void positionChanged(qint64 frame);
	void failed(const QString& message);

private:
	bool current(quint64 session) const;
	void fail(const QString& message);
	void detach();
	void setPosition(qint64 frame);
	void finishAtEnd(quint64 session);
	void restartLoop(quint64 session);
	bool open(const QByteArray& bytes, const QString& fileName, qint64 initialPosition);
	std::unique_ptr<AudioPlaybackBackend> m_backend;
	QTimer m_timeout;
	State m_state = State::Unavailable;
	AudioPlaybackBackend::State m_backendState = AudioPlaybackBackend::State::Stopped;
	quint64 m_session = 0;
	qint64 m_first = 0, m_end = 0, m_position = 0;
	int m_sampleRate = 0;
	float m_volume = 0.7f;
	bool m_loop = false, m_seekable = false, m_stalled = false, m_pendingStart = false;
	bool m_pendingLoopRestart = false;
	bool m_media = false;
	qint64 m_initialMs = 0;
	QString m_error;
};

} // namespace vibestudio
