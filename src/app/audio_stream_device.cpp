#include "app/audio_session_playback.h"
#include "vibestudio_config.h"
#include <QCoreApplication>
#include <algorithm>
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#endif

namespace vibestudio
{
namespace
{
QString problem(const char *text) { return QCoreApplication::translate("AudioStreamDevice", text); }
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
// Original adapter to Qt 6.4.2/6.10.1's public QAudioSink QIODevice API. No
// callback API introduced after our minimum Qt version. See docs/CREDITS.md.
class QtAudioStreamDevice final : public AudioStreamDevice {
  public:
	QtAudioStreamDevice()
	{
		QObject::connect(&m_devices, &QMediaDevices::audioOutputsChanged, &m_devices, [this] {
			if (!m_sink)
				return;
			const auto devices = QMediaDevices::audioOutputs();
			m_missing = std::none_of(devices.cbegin(), devices.cend(),
			                         [this](const QAudioDevice &device) { return device.id() == m_id; });
		});
	}
	~QtAudioStreamDevice() override { reset(); }
	bool available() const override { return true; }
	QVector<AudioOutputDevice> outputs() override
	{
		QVector<AudioOutputDevice> result;
		for (const auto &device : QMediaDevices::audioOutputs()) {
			result.append({device.id(), device.description(), device.isDefault()});
		}
		return result;
	}
	bool open(const AudioStreamConfiguration &config, QString *error) override
	{
		reset();
		QAudioDevice selected;
		for (const auto &device : QMediaDevices::audioOutputs()) {
			if ((config.deviceId.isEmpty() && device.isDefault()) ||
			    (!config.deviceId.isEmpty() && config.deviceId == device.id())) {
				selected = device;
				break;
			}
		}
		if (selected.isNull()) {
			*error = problem(QT_TRANSLATE_NOOP(
			    "AudioStreamDevice",
			    "The selected audio output is unavailable. Refresh the output list and select a device."));
			return false;
		}
		QAudioFormat format;
		format.setSampleRate(config.sampleRate);
		format.setChannelCount(2);
		format.setSampleFormat(QAudioFormat::Float);
		if (!selected.isFormatSupported(format)) {
			*error = problem(QT_TRANSLATE_NOOP("AudioStreamDevice",
			                                   "The selected output does not support stereo float audio at the session "
			                                   "rate. Choose another output or explicitly convert the session media."));
			return false;
		}
		m_rate = config.sampleRate;
		m_id = selected.id();
		m_missing = false;
		m_sink = std::make_unique<QAudioSink>(selected, format);
		m_sink->setBufferSize(config.bufferFrames * 2 * int(sizeof(float)));
		m_sink->setVolume(1);
		m_stream = m_sink->start();
		if (!m_stream || m_sink->error() != QAudio::NoError) {
			*error = problem(QT_TRANSLATE_NOOP(
			    "AudioStreamDevice",
			    "The audio output could not start. Check the device and its permissions, then retry."));
			reset();
			return false;
		}
		return true;
	}
	void reset() override
	{
		if (m_sink)
			m_sink->reset(); // Discards queued data without a blocking drain.
		m_stream = nullptr;
		m_sink.reset();
	}
	void suspend() override
	{
		if (m_sink)
			m_sink->suspend();
	}
	void resume() override
	{
		if (m_sink)
			m_sink->resume();
	}
	qint64 write(const char *bytes, qint64 count) override { return m_stream ? m_stream->write(bytes, count) : -1; }
	AudioStreamDeviceStatus status() const override
	{
		AudioStreamDeviceStatus result;
		if (!m_sink)
			return result;
		if (m_missing || (m_sink->error() != QAudio::NoError && m_sink->error() != QAudio::UnderrunError)) {
			result.state = AudioStreamDeviceStatus::State::Error;
			result.error =
			    m_missing ? problem(QT_TRANSLATE_NOOP(
			                    "AudioStreamDevice",
			                    "The selected audio output was disconnected. Select an output and restart playback."))
			              : problem(QT_TRANSLATE_NOOP("AudioStreamDevice",
			                                          "The audio output reported a device error. Playback stopped."));
			return result;
		}
		result.writableBytes = std::max<qint64>(0, m_sink->bytesFree());
		// Qt reports processed time in microseconds. This is a device-reported
		// estimate, not a promise of sample-exact hardware presentation timing.
		const qint64 us = std::max<qint64>(0, m_sink->processedUSecs());
		result.processedFrames = (us / 1000000) * m_rate + (us % 1000000) * m_rate / 1000000;
		result.bufferFrames = int(m_sink->bufferSize() / (2 * sizeof(float)));
		switch (m_sink->state()) {
		case QAudio::ActiveState:
			result.state = AudioStreamDeviceStatus::State::Active;
			break;
		case QAudio::IdleState:
			result.state = AudioStreamDeviceStatus::State::Idle;
			break;
		case QAudio::SuspendedState:
			result.state = AudioStreamDeviceStatus::State::Paused;
			break;
		case QAudio::StoppedState:
			result.state = AudioStreamDeviceStatus::State::Closed;
			break;
		}
		return result;
	}

  private:
	QMediaDevices m_devices;
	std::unique_ptr<QAudioSink> m_sink;
	QIODevice *m_stream = nullptr;
	QByteArray m_id;
	int m_rate = 0;
	bool m_missing = false;
};
#else
class UnavailableAudioStreamDevice final : public AudioStreamDevice {
  public:
	bool available() const override { return false; }
	QVector<AudioOutputDevice> outputs() override { return {}; }
	bool open(const AudioStreamConfiguration &, QString *error) override
	{
		*error = problem(QT_TRANSLATE_NOOP("AudioStreamDevice", "Audio playback is unavailable in this build."));
		return false;
	}
	void reset() override {}
	void suspend() override {}
	void resume() override {}
	qint64 write(const char *, qint64) override { return -1; }
	AudioStreamDeviceStatus status() const override { return {}; }
};
#endif
} // namespace
std::unique_ptr<AudioStreamDevice> createAudioStreamDevice()
{
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
	return std::make_unique<QtAudioStreamDevice>();
#else
	return std::make_unique<UnavailableAudioStreamDevice>();
#endif
}
} // namespace vibestudio
