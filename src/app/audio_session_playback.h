#pragma once

#include "core/audio_transport.h"
#include <QObject>
#include <atomic>
#include <memory>

class QThread;
namespace vibestudio
{
struct AudioOutputDevice {
	QByteArray id;
	QString name;
	bool systemDefault = false;
};
struct AudioStreamConfiguration {
	QByteArray deviceId; // Empty explicitly selects the current system default.
	int sampleRate = 48000;
	int bufferFrames = 2048;
};
struct AudioStreamDeviceStatus {
	enum class State { Active, Idle, Paused, Closed, Error };
	State state = State::Closed;
	qint64 writableBytes = 0;
	qint64 processedFrames = 0;
	int bufferFrames = 0;
	QString error;
};
// Construct and use on the playback worker. Writes can be short, including a
// partial sample/frame; the adapter retains every unwritten byte before mixing
// another block. Test implementations never open a physical audio device.
class AudioStreamDevice {
  public:
	virtual ~AudioStreamDevice() = default;
	virtual bool available() const = 0;
	virtual QVector<AudioOutputDevice> outputs() = 0;
	virtual bool open(const AudioStreamConfiguration &configuration, QString *error) = 0;
	virtual void reset() = 0;
	virtual void suspend() = 0;
	virtual void resume() = 0;
	virtual qint64 write(const char *bytes, qint64 count) = 0;
	virtual AudioStreamDeviceStatus status() const = 0;
};
using AudioStreamDeviceFactory = std::function<std::unique_ptr<AudioStreamDevice>()>;
std::unique_ptr<AudioStreamDevice> createAudioStreamDevice();

struct AudioSessionPlaybackSnapshot {
	enum class State { Unavailable, Stopped, Preparing, Playing, Paused, Ended, Error };
	State state = State::Stopped;
	qint64 position = 0;
	quint64 renderedFrames = 0, submittedFrames = 0, processedFrames = 0;
	quint64 underruns = 0, samplesAboveFullScale = 0;
	std::array<float, 2> peak{};
	AudioMeterSnapshot meters;
	int bufferFrames = 0;
	QString error;
};
class AudioSessionPlaybackWorker;
// UI facade. Preparation, mixing and all device work run on one dedicated Qt
// worker. Generation checks discard stale completion/error/position events.
class AudioSessionPlayback final : public QObject {
	Q_OBJECT
  public:
	explicit AudioSessionPlayback(QObject *parent = nullptr, AudioStreamDeviceFactory factory = {});
	~AudioSessionPlayback() override;
	void start(const AudioSession &session, AudioTransportRange range, AudioStreamConfiguration configuration);
	void stop();
	// Acknowledge actual worker/device shutdown on the facade thread. A newer
	// playback generation returns false; destroying context cancels completion.
	void stopAndWait(QObject *context, std::function<void(bool)> completion);
	void pause();
	void resume();
	void seek(qint64 frame);
	void setLoop(bool loop);
	void setVolume(float volume);
	void refreshOutputs();
	void resetMeters();
	[[nodiscard]] const AudioSessionPlaybackSnapshot &snapshot() const { return m_snapshot; }
	[[nodiscard]] const QVector<AudioOutputDevice> &outputs() const { return m_outputs; }
	[[nodiscard]] bool available() const { return m_available; }
  Q_SIGNALS:
	void changed();
	void outputsChanged();

  private:
	friend class AudioSessionPlaybackWorker;
	QThread *m_thread = nullptr;
	AudioSessionPlaybackWorker *m_worker = nullptr;
	std::shared_ptr<std::atomic<quint64>> m_generation;
	AudioSessionPlaybackSnapshot m_snapshot;
	QVector<AudioOutputDevice> m_outputs;
	AudioTransportRange m_range;
	bool m_available = false;
};
} // namespace vibestudio
