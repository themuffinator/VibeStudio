#pragma once
#include "core/audio_duplex.h"
#include <memory>

namespace vibestudio
{
struct AudioDuplexEndpoint {
	QByteArray id; // Enumeration-scoped token, never a persistent device index.
	QString name, host;
	int inputChannels = 0, outputChannels = 0;
	double defaultSampleRate = 0;
};
struct AudioDuplexDeviceRequest {
	QByteArray inputId, outputId; // Explicit current enumeration selections.
	int sampleRate = 48000, inputChannels = 1, bufferFrames = 2048;
};
struct AudioDuplexDeviceInfo {
	QString inputName, outputName, host;
	int sampleRate = 0, inputChannels = 0;
	double inputLatencySeconds = 0, outputLatencySeconds = 0;
	// WASAPI input uses packet QPC, while output is a padding estimate. Other
	// hosts provide driver estimates. No hardware-calibrated accuracy is claimed.
	bool packetInputTimestamp = false;
};
enum class AudioDuplexCallbackResult { Continue, Complete, Abort };
class AudioDuplexDeviceCallback {
  public:
	virtual ~AudioDuplexDeviceCallback() = default;
	virtual AudioDuplexCallbackResult process(std::span<const float> input, std::span<float> output,
	                                          const AudioDuplexTime &time) noexcept = 0;
};
struct AudioDuplexDeviceStatus {
	enum class State { Closed, Open, Running, Draining, Complete, Stopped, Error };
	State state = State::Closed;
	quint64 callbackCount = 0;
	QString error;
};
// One control-thread owner for enumeration, open/start/status/close/destruction.
// Constructing/available never initializes devices. enumerate does not capture;
// open/start require the caller's explicit Record action and permission check.
// The callback remains alive until close returns. A successful close joins
// native callbacks. Failed close detaches the target after its current bounded
// callback, then quarantines the native runtime/context until process exit.
// Only requestStop may be called from another thread, including the callback.
class AudioDuplexDevice {
  public:
	virtual ~AudioDuplexDevice() = default;
	virtual bool available() const = 0;
	virtual QVector<AudioDuplexEndpoint> enumerate(QString *error) = 0;
	virtual bool open(const AudioDuplexDeviceRequest &, AudioDuplexDeviceCallback &, QString *error) = 0;
	virtual bool start(QString *error) = 0;
	virtual void requestStop() noexcept = 0;
	virtual AudioDuplexDeviceStatus status() = 0;
	virtual AudioDuplexDeviceInfo info() const = 0;
	virtual void close() = 0;
};
using AudioDuplexDeviceFactory = std::function<std::unique_ptr<AudioDuplexDevice>()>;
std::unique_ptr<AudioDuplexDevice> createAudioDuplexDevice();
} // namespace vibestudio
