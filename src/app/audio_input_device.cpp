#include "app/audio_capture.h"
#include "vibestudio_config.h"
#include <QCoreApplication>
#include <algorithm>
#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#endif
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#if QT_CONFIG(permissions)
#include <QPermissions>
#define VIBESTUDIO_CAPTURE_PERMISSIONS 1
#endif
#endif
#ifndef VIBESTUDIO_CAPTURE_PERMISSIONS
#define VIBESTUDIO_CAPTURE_PERMISSIONS 0
#endif

namespace vibestudio
{
namespace
{
QString problem(const char *text) { return QCoreApplication::translate("AudioInputDevice", text); }
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
// Original adapter to the public Qt QAudioSource byte-stream API. PCM uses
// native byte order. No resampling or input fallback occurs here. See CREDITS.
class QtAudioCaptureDevice final : public AudioCaptureDevice {
  public:
	QtAudioCaptureDevice()
	{
		QObject::connect(&m_devices, &QMediaDevices::audioInputsChanged, &m_devices, [this] {
			if (!m_source)
				return;
			const auto inputs = QMediaDevices::audioInputs();
			m_missing =
			    std::none_of(inputs.cbegin(), inputs.cend(), [this](const auto &input) { return input.id() == m_id; });
		});
	}
	~QtAudioCaptureDevice() override { close(); }
	bool available() const override
	{
#if defined(Q_OS_MACOS) && !VIBESTUDIO_CAPTURE_PERMISSIONS
		return false;
#else
		return true;
#endif
	}
	QVector<AudioInputDevice> inputs() override
	{
		QVector<AudioInputDevice> result;
		for (const auto &device : QMediaDevices::audioInputs())
			result.append(
			    {device.id(), device.description(), device.minimumChannelCount(), device.maximumChannelCount()});
		return result;
	}
	bool open(const AudioCaptureRequest &request, AudioInputEncoding *encoding, QString *error) override
	{
		close();
		QAudioDevice selected;
		for (const auto &device : QMediaDevices::audioInputs())
			if (!request.deviceId.isEmpty() && device.id() == request.deviceId) {
				selected = device;
				break;
			}
		if (selected.isNull()) {
			*error = problem(
			    QT_TRANSLATE_NOOP("AudioInputDevice",
			                      "The selected input is unavailable. Refresh and explicitly select an input device."));
			return false;
		}
		QAudioFormat format;
		format.setSampleRate(request.metadata.sampleRate);
		format.setChannelCount(request.metadata.inputChannels);
		for (auto candidate : {QAudioFormat::Float, QAudioFormat::Int32, QAudioFormat::Int16, QAudioFormat::UInt8}) {
			format.setSampleFormat(candidate);
			if (selected.isFormatSupported(format))
				break;
		}
		if (!selected.isFormatSupported(format)) {
			*error = problem(QT_TRANSLATE_NOOP("AudioInputDevice",
			                                   "The input does not support the requested sample rate and channel "
			                                   "count. Select a supported configuration; capture does not resample."));
			return false;
		}
		*encoding = format.sampleFormat() == QAudioFormat::Float   ? AudioInputEncoding::Float32
		            : format.sampleFormat() == QAudioFormat::Int32 ? AudioInputEncoding::Int32
		            : format.sampleFormat() == QAudioFormat::Int16 ? AudioInputEncoding::Int16
		                                                           : AudioInputEncoding::UInt8;
		m_frameBytes = format.bytesPerFrame();
		m_id = selected.id();
		m_missing = false;
		m_source = std::make_unique<QAudioSource>(selected, format);
		m_source->setBufferSize(request.bufferFrames * m_frameBytes);
		m_source->setVolume(1);
		m_stream = m_source->start();
		if (!m_stream || m_source->error() != QAudio::NoError) {
			*error = problem(QT_TRANSLATE_NOOP("AudioInputDevice",
			                                   "The input could not start. Check the selected device, OS microphone "
			                                   "permission and exclusive use by other applications."));
			close();
			return false;
		}
		return true;
	}
	qint64 read(char *bytes, qint64 count) override { return m_stream ? m_stream->read(bytes, count) : -1; }
	AudioInputStatus status() const override
	{
		AudioInputStatus result;
		if (!m_source || m_missing || m_source->error() != QAudio::NoError ||
		    m_source->state() == QAudio::StoppedState || m_source->state() == QAudio::SuspendedState) {
			result.error =
			    problem(QT_TRANSLATE_NOOP("AudioInputDevice", "The audio input stopped, disconnected or reported a "
			                                                  "dropout. Capture stopped; inspect the retained take."));
			return result;
		}
		result.availableBytes = std::max<qint64>(0, m_source->bytesAvailable());
		result.bufferFrames = int(m_source->bufferSize() / m_frameBytes);
		return result;
	}
	void close() override
	{
		if (m_source)
			m_source->stop();
		m_stream = nullptr;
		m_source.reset();
	}

  private:
	QMediaDevices m_devices;
	std::unique_ptr<QAudioSource> m_source;
	QIODevice *m_stream = nullptr;
	QByteArray m_id;
	int m_frameBytes = 4;
	bool m_missing = false;
};
#else
class UnavailableCaptureDevice final : public AudioCaptureDevice {
  public:
	bool available() const override { return false; }
	QVector<AudioInputDevice> inputs() override { return {}; }
	bool open(const AudioCaptureRequest &, AudioInputEncoding *, QString *error) override
	{
		*error = problem(QT_TRANSLATE_NOOP("AudioInputDevice", "Audio device capture is unavailable in this build."));
		return false;
	}
	qint64 read(char *, qint64) override { return -1; }
	AudioInputStatus status() const override { return {}; }
	void close() override {}
};
#endif
} // namespace
std::unique_ptr<AudioCaptureDevice> createAudioCaptureDevice()
{
#if VIBESTUDIO_HAVE_AUDIO_PLAYBACK
	return std::make_unique<QtAudioCaptureDevice>();
#else
	return std::make_unique<UnavailableCaptureDevice>();
#endif
}
void requestAudioCapturePermission(QObject *context, std::function<void(QString)> complete)
{
#ifdef Q_OS_MACOS
	// Guard the OS usage-description contract before Qt can request access.
	// Handles binary/XML plists and localized InfoPlist.strings through the
	// system API, without parsing a guessed bundle path. See docs/CREDITS.md.
	const auto bundle = CFBundleGetMainBundle();
	const auto description =
	    bundle ? CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("NSMicrophoneUsageDescription")) : nullptr;
	if (!description || CFGetTypeID(description) != CFStringGetTypeID() ||
	    CFStringGetLength(static_cast<CFStringRef>(description)) == 0) {
		complete(problem(QT_TRANSLATE_NOOP("AudioInputDevice",
		                                   "This macOS application has no microphone usage description. Install a "
		                                   "recording-enabled application bundle; take review remains available.")));
		return;
	}
#endif
#if defined(Q_OS_MACOS) && !VIBESTUDIO_CAPTURE_PERMISSIONS
	Q_UNUSED(context);
	complete(problem(QT_TRANSLATE_NOOP(
	    "AudioInputDevice", "Recording on macOS requires Qt 6.5 or newer with microphone permission support.")));
#elif VIBESTUDIO_CAPTURE_PERMISSIONS
	const QMicrophonePermission permission;
	const auto report = [complete](Qt::PermissionStatus status) {
		complete(status == Qt::PermissionStatus::Granted
		             ? QString{}
		             : problem(QT_TRANSLATE_NOOP("AudioInputDevice",
		                                         "Microphone permission was denied. Enable access in OS privacy "
		                                         "settings, then explicitly start recording again.")));
	};
	const auto status = qApp->checkPermission(permission);
	if (status == Qt::PermissionStatus::Undetermined)
		qApp->requestPermission(permission, context, [report](const QPermission &result) { report(result.status()); });
	else
		report(status);
#else
	Q_UNUSED(context);
	complete({}); // Older Windows/Linux backends enforce OS access on open.
#endif
}
} // namespace vibestudio
