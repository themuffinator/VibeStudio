#pragma once
#include "app/audio_capture.h"
#include "app/audio_duplex_device.h"
#include "core/audio_recording.h"
#include <QObject>

class QThread;
namespace vibestudio
{
struct AudioRecordingRequest {
	QString directory;
	AudioRecordingPlan plan;
	AudioDuplexDeviceRequest device;
	int queueFrames = 65536, stallTimeoutMs = 5000;
};
struct AudioRecordingSnapshot {
	enum class State {
		Unavailable,
		Ready,
		Permission,
		WaitingForPlayback,
		Preparing,
		Recording,
		Draining,
		Saving,
		Finished,
		Error
	};
	State state = State::Ready;
	QString error;
	AudioDuplexProgress progress;
	AudioMeterSnapshot meters;
	AudioDuplexMeters levels;
	quint64 meterEpoch = 0;
	AudioDuplexDeviceInfo device;
	std::array<qint64, AudioDuplexArmLimit> storedFrames{};
	qint64 queuedFrames = 0;
	AudioRecordingInfo result;
};
// Gates run on the facade's thread; completion must return to that thread.
// Permission is native by default. An optional preparation gate acknowledges
// existing playback/audition shutdown before the recording worker can open input.
using AudioRecordingGate = std::function<void(QObject *, std::function<void(QString)>)>;
enum class AudioRecordingStop;
class AudioRecordingWorker;
class AudioRecording final : public QObject {
	Q_OBJECT
  public:
	explicit AudioRecording(QObject *parent = nullptr, AudioDuplexDeviceFactory device = {},
	                        AudioCaptureStorageFactory storage = {}, AudioRecordingGate permission = {},
	                        AudioRecordingGate beforeStart = {});
	~AudioRecording() override;
	bool start(const AudioSession &session, const AudioRecordingRequest &request);
	void stop();
	void refreshDevices();
	void resetMeters();
	[[nodiscard]] bool meterResetPending() const;
	[[nodiscard]] bool busy() const;
	[[nodiscard]] bool available() const { return m_available; }
	[[nodiscard]] const QVector<AudioDuplexEndpoint> &devices() const { return m_devices; }
	[[nodiscard]] const AudioRecordingSnapshot &snapshot() const { return m_snapshot; }
  Q_SIGNALS:
	void changed();
	void devicesChanged();

  private:
	friend class AudioRecordingWorker;
	void prepare(const AudioSession &, const AudioRecordingRequest &, quint64 epoch);
	QThread *m_thread = nullptr;
	AudioRecordingWorker *m_worker = nullptr;
	AudioRecordingGate m_permission, m_beforeStart;
	std::shared_ptr<std::atomic<AudioRecordingStop>> m_stop;
	std::shared_ptr<std::atomic<quint64>> m_meterReset;
	AudioRecordingSnapshot m_snapshot;
	QVector<AudioDuplexEndpoint> m_devices;
	quint64 m_epoch = 0;
	bool m_available = false;
};
} // namespace vibestudio
