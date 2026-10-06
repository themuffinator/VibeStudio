#pragma once
#include "core/audio_take.h"
#include <QObject>
#include <array>
#include <atomic>

class QThread;
namespace vibestudio
{
struct AudioInputDevice {
	QByteArray id;
	QString name;
	int minimumChannels = 1, maximumChannels = 1;
};
enum class AudioInputEncoding { Float32, Int32, Int16, UInt8 };
struct AudioCaptureRequest {
	QString path;
	AudioTakeMetadata metadata;
	QByteArray deviceId; // An explicit device is required; no implicit default.
	int bufferFrames = 2048;
};
struct AudioInputStatus {
	qint64 availableBytes = 0;
	int bufferFrames = 0;
	QString error;
};
// Worker-owned native-endian PCM byte stream. Short reads may split a sample.
// Capture retains partial frames, converts explicitly and stores selected input
// channels without resampling or clipping. Test devices never access hardware.
class AudioCaptureDevice {
  public:
	virtual ~AudioCaptureDevice() = default;
	virtual bool available() const = 0;
	virtual QVector<AudioInputDevice> inputs() = 0;
	virtual bool open(const AudioCaptureRequest &, AudioInputEncoding *, QString *error) = 0;
	virtual qint64 read(char *bytes, qint64 count) = 0;
	virtual AudioInputStatus status() const = 0;
	virtual void close() = 0;
};
using AudioCaptureDeviceFactory = std::function<std::unique_ptr<AudioCaptureDevice>()>;
std::unique_ptr<AudioCaptureDevice> createAudioCaptureDevice();
void requestAudioCapturePermission(QObject *context, std::function<void(QString)> complete);

// Storage seam for failure/slow-disk fixtures; production uses AudioTakeWriter.
// Construct and use only on the disk thread. No device or GUI access here.
class AudioCaptureStorage {
  public:
	virtual ~AudioCaptureStorage() = default;
	virtual bool open(const AudioCaptureRequest &, QString *error) = 0;
	virtual bool append(std::span<const float>, QString *error) = 0;
	virtual bool finish(QString *error) = 0;
	virtual void close() = 0;
};
using AudioCaptureStorageFactory = std::function<std::unique_ptr<AudioCaptureStorage>()>;
struct AudioCaptureSnapshot {
	enum class State { Unavailable, Ready, Permission, Preparing, Recording, Stopping, Finished, Error };
	State state = State::Ready;
	qint64 receivedFrames = 0, queuedFrames = 0, storedFrames = 0;
	quint64 samplesAboveFullScale = 0;
	std::array<float, 8> peak{};
	int bufferFrames = 0, queuedBlocks = 0;
	QString error;
	AudioTakeInfo take;
};
class AudioCaptureWorker;
enum class AudioCaptureStop;
// An explicit start is the only operation that may request permission/open an
// input. Enumeration and arming do not record. Stop also cancels pending access.
class AudioCapture final : public QObject {
	Q_OBJECT
  public:
	explicit AudioCapture(QObject *parent = nullptr, AudioCaptureDeviceFactory device = {},
	                      AudioCaptureStorageFactory storage = {});
	~AudioCapture() override;
	bool start(const AudioCaptureRequest &request);
	void stop();
	void refreshInputs();
	[[nodiscard]] bool busy() const;
	[[nodiscard]] bool available() const { return m_available; }
	[[nodiscard]] const QVector<AudioInputDevice> &inputs() const { return m_inputs; }
	[[nodiscard]] const AudioCaptureSnapshot &snapshot() const { return m_snapshot; }
  Q_SIGNALS:
	void changed();
	void inputsChanged();

  private:
	friend class AudioCaptureWorker;
	QThread *m_thread = nullptr;
	AudioCaptureWorker *m_worker = nullptr;
	std::shared_ptr<std::atomic<AudioCaptureStop>> m_stop;
	quint64 m_epoch = 0;
	bool m_available = false, m_native = true;
	QVector<AudioInputDevice> m_inputs;
	AudioCaptureSnapshot m_snapshot;
};
} // namespace vibestudio
