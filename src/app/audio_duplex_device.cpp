#include "app/audio_duplex_device.h"
#include "app/audio_portaudio_api.h"
#include "vibestudio_config.h"
#include <QCoreApplication>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>
#include <vibestudio_portaudio.h>
#ifdef Q_OS_WIN
#include <pa_win_wasapi.h>
#elif defined(Q_OS_MACOS)
#include <pa_mac_core.h>
#endif

namespace vibestudio
{
namespace
{
QString deviceText(const char *message) { return QCoreApplication::translate("Audio", message); }
enum class CallbackEnd { None, Complete, Abort, Stop, Invalid };
struct CallbackState {
	AudioDuplexDeviceCallback *m_callback = nullptr;
	int m_channels = 0;
	std::atomic<bool> m_stop{false};
	std::atomic<CallbackEnd> m_end{CallbackEnd::None};
	std::atomic<quint64> m_callbacks{0};
	std::atomic<unsigned> m_inFlight{0};
};
class PortAudioDevice final : public AudioDuplexDevice {
  public:
	explicit PortAudioDevice(std::shared_ptr<AudioPortAudioApi> api) : m_api(std::move(api)) {}
	~PortAudioDevice() override
	{
		close();
		if (m_initialized && !m_quarantined)
			m_api->terminate();
		if (!m_quarantined)
			delete m_callbackState;
	}
	bool available() const override { return bool(m_api) && !m_quarantined; }
	QVector<AudioDuplexEndpoint> enumerate(QString *error) override
	{
		if (m_quarantined) {
			if (error)
				*error = m_error;
			return {};
		}
		if (m_stream) {
			fail(error,
			     deviceText(QT_TRANSLATE_NOOP("Audio", "Close the recording device before refreshing its list.")));
			return {};
		}
		m_devices.clear();
		m_indices.clear();
		// PortAudio caches its device inventory for an initialization lifetime.
		// Refresh only while closed, and release/reacquire on this same owner.
		if (m_initialized) {
			const auto result = m_api->terminate();
			m_initialized = false;
			if (result != paNoError) {
				fail(error, nativeError(result));
				return {};
			}
		}
		if (!initialize(error))
			return {};
		const int count = m_api->deviceCount();
		if (count < 0) {
			fail(error, nativeError(count));
			return {};
		}
		const QByteArray generation = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
		for (int index = 0; index < count; ++index) {
			const auto *device = m_api->deviceInfo(index);
			const auto *host = device ? m_api->hostInfo(device->hostApi) : nullptr;
			if (!device || !host || !device->name || !host->name || !supportedHost(host->type) ||
			    (device->maxInputChannels <= 0 && device->maxOutputChannels < 2))
				continue;
			m_devices.push_back({generation + ':' + QByteArray::number(index), QString::fromUtf8(device->name),
			                     QString::fromUtf8(host->name), device->maxInputChannels, device->maxOutputChannels,
			                     device->defaultSampleRate});
			m_indices.push_back(index);
		}
		if (error)
			error->clear();
		return m_devices;
	}
	bool open(const AudioDuplexDeviceRequest &request, AudioDuplexDeviceCallback &callback, QString *error) override
	{
		if (m_quarantined) {
			if (error)
				*error = m_error;
			return false;
		}
		if (m_stream)
			return fail(error, deviceText(QT_TRANSLATE_NOOP("Audio", "A recording device is already open.")));
		m_error.clear();
		m_info = {};
		m_started = false;
		m_callbackState->m_stop.store(false);
		m_callbackState->m_end.store(CallbackEnd::None);
		m_callbackState->m_callbacks.store(0);
		if (!m_api)
			return fail(error, unavailableText());
		if (request.sampleRate < 8000 || request.sampleRate > 384000 || request.inputChannels < 1 ||
		    request.inputChannels > AudioDuplexInputLimit || request.bufferFrames < 64 || request.bufferFrames > 16384)
			return fail(error, deviceText(QT_TRANSLATE_NOOP("Audio",
			                                                "The recording device format or buffer size is invalid.")));
		const int inputIndex = selected(request.inputId), outputIndex = selected(request.outputId);
		if (inputIndex < 0 || outputIndex < 0 || !m_initialized)
			return fail(error, deviceText(QT_TRANSLATE_NOOP(
			                       "Audio", "Select input and output devices from the current device list.")));
		const auto *input = m_api->deviceInfo(inputIndex), *output = m_api->deviceInfo(outputIndex);
		if (!input || !output || input->hostApi != output->hostApi || input->maxInputChannels < request.inputChannels ||
		    output->maxOutputChannels < 2)
			return fail(error,
			            deviceText(QT_TRANSLATE_NOOP(
			                "Audio", "The selected devices cannot provide this input and stereo output format.")));
		const auto *host = m_api->hostInfo(input->hostApi);
		if (!host || !supportedHost(host->type))
			return fail(error, unavailableText());
		if (host->type == paCoreAudio && (inputIndex != outputIndex || input->defaultSampleRate != request.sampleRate))
			return fail(error,
			            deviceText(QT_TRANSLATE_NOOP(
			                "Audio",
			                "CoreAudio recording requires one duplex device already set to the session sample rate.")));
		const double latency = double(request.bufferFrames) / request.sampleRate;
		PaStreamParameters in{inputIndex, request.inputChannels, paFloat32, latency, nullptr};
		PaStreamParameters out{outputIndex, 2, paFloat32, latency, nullptr};
#ifdef Q_OS_WIN
		PaWasapiStreamInfo wasapi{};
		wasapi.size = sizeof(wasapi);
		wasapi.hostApiType = paWASAPI;
		wasapi.version = 1;
		wasapi.flags = paWinWasapiPolling | paWinWasapiExplicitSampleFormat;
		if (host->type == paWASAPI)
			in.hostApiSpecificStreamInfo = out.hostApiSpecificStreamInfo = &wasapi;
#elif defined(Q_OS_MACOS)
		PaMacCoreStreamInfo mac{};
		mac.size = sizeof(mac);
		mac.hostApiType = paCoreAudio;
		mac.version = 1;
		mac.flags = paMacCoreFailIfConversionRequired;
		if (host->type == paCoreAudio)
			in.hostApiSpecificStreamInfo = out.hostApiSpecificStreamInfo = &mac;
#endif
		m_callbackState->m_channels = request.inputChannels;
		m_callbackState->m_callback = &callback;
		PaError result = m_api->open(&m_stream, &in, &out, request.sampleRate, paFramesPerBufferUnspecified,
		                             paClipOff | paDitherOff, &PortAudioDevice::dispatch, m_callbackState);
		if (result != paNoError) {
			close();
			return fail(error, nativeError(result));
		}
		const auto *actual = m_stream ? m_api->streamInfo(m_stream) : nullptr;
		if (!actual || actual->sampleRate != request.sampleRate || !std::isfinite(actual->inputLatency) ||
		    !std::isfinite(actual->outputLatency) || actual->inputLatency < 0 || actual->outputLatency < 0 ||
		    actual->inputLatency > 10 || actual->outputLatency > 10) {
			close();
			return fail(error, deviceText(QT_TRANSLATE_NOOP(
			                       "Audio", "The device did not report the requested sample rate and valid latency.")));
		}
		m_info = {QString::fromUtf8(input->name), QString::fromUtf8(output->name),
		          QString::fromUtf8(host->name),  request.sampleRate,
		          request.inputChannels,          actual->inputLatency,
		          actual->outputLatency,          host->type == paWASAPI};
		if (error)
			error->clear();
		return true;
	}
	bool start(QString *error) override
	{
		if (!m_stream || m_started || m_callbackState->m_stop.load(std::memory_order_acquire))
			return fail(error,
			            deviceText(QT_TRANSLATE_NOOP("Audio", "Open a fresh recording device before starting.")));
		m_started = true; // The host can invoke its callback before start returns.
		const auto result = m_api->start(m_stream);
		if (result != paNoError) {
			close();
			return fail(error, nativeError(result));
		}
		if (error)
			error->clear();
		return true;
	}
	void requestStop() noexcept override { m_callbackState->m_stop.store(true, std::memory_order_seq_cst); }
	AudioDuplexDeviceStatus status() override
	{
		using State = AudioDuplexDeviceStatus::State;
		State state = State::Closed;
		if (m_stream && !m_started)
			state = State::Open;
		else if (m_stream) {
			const auto active = m_api->active(m_stream);
			const auto end = m_callbackState->m_end.load(std::memory_order_acquire);
			if (active < 0)
				m_error = nativeError(active);
			else if (active > 0)
				state = end == CallbackEnd::Complete ? State::Draining : State::Running;
			else if (m_api->stoppedFlags(m_stream))
				m_error = deviceText(
				    QT_TRANSLATE_NOOP("Audio", "The audio driver stopped with an error or could not finish playback."));
			else if (end == CallbackEnd::Complete)
				state = State::Complete;
			else if (end == CallbackEnd::Stop || m_callbackState->m_stop.load(std::memory_order_acquire))
				state = State::Stopped;
			else if (end == CallbackEnd::Invalid)
				m_error =
				    deviceText(QT_TRANSLATE_NOOP("Audio", "The audio driver supplied an invalid callback buffer."));
			else if (end == CallbackEnd::Abort)
				state = State::Stopped; // The processor owner retains its specific fault.
			else
				m_error = deviceText(
				    QT_TRANSLATE_NOOP("Audio", "The audio driver stopped before the recording pass finished."));
		}
		if (!m_error.isEmpty())
			state = State::Error;
		return {state, m_callbackState ? m_callbackState->m_callbacks.load(std::memory_order_acquire) : 0, m_error};
	}
	AudioDuplexDeviceInfo info() const override { return m_info; }
	void close() override
	{
		requestStop();
		if (m_stream) {
			// Abort/close join native callbacks; never destroy their target earlier.
			const auto aborted = m_api->abort(m_stream);
			const auto closed = m_api->close(m_stream);
			if (closed != paNoError) {
				m_error = deviceText(QT_TRANSLATE_NOOP(
				    "Audio",
				    "The audio driver could not close. Recording is unavailable until the application restarts."));
				// A failed Pa_CloseStream may leave native callbacks alive. Stop
				// admitting work, wait for the bounded current callback, then
				// quarantine its tiny context and native runtime for the rest
				// of the process. A late callback sees Stop without accessing
				// this object or the former processor. Do not retry an opaque
				// stream pointer: some hosts free it even when Close fails.
				while (m_callbackState->m_inFlight.load(std::memory_order_seq_cst) != 0)
					std::this_thread::yield();
				m_callbackState->m_callback = nullptr;
				m_quarantined = true;
			} else if (aborted != paNoError && aborted != paStreamIsStopped)
				m_error = nativeError(aborted);
		}
		m_stream = nullptr;
		if (m_callbackState)
			m_callbackState->m_callback = nullptr;
		m_started = false;
	}

  private:
	static bool supportedHost(PaHostApiTypeId host)
	{
		return host == paWASAPI || host == paCoreAudio || host == paALSA;
	}
	QString unavailableText() const
	{
		return deviceText(QT_TRANSLATE_NOOP("Audio", "Native synchronized recording is unavailable in this build."));
	}
	QString nativeError(PaError code) const
	{
		return deviceText(QT_TRANSLATE_NOOP("Audio", "Recording device error: %1"))
		    .arg(QString::fromUtf8(m_api->errorText(code)));
	}
	bool fail(QString *error, const QString &message)
	{
		m_error = message;
		if (error)
			*error = message;
		return false;
	}
	bool initialize(QString *error)
	{
		if (!m_api)
			return fail(error, unavailableText());
		if (!m_initialized) {
			const auto result = m_api->initialize();
			if (result != paNoError)
				return fail(error, nativeError(result));
			m_initialized = true;
		}
		m_error.clear();
		return true;
	}
	int selected(const QByteArray &id) const
	{
		for (qsizetype i = 0; i < m_devices.size(); ++i)
			if (m_devices[i].id == id)
				return m_indices[i];
		return -1;
	}
	static int dispatch(const void *input, void *output, unsigned long frames, const PaStreamCallbackTimeInfo *time,
	                    PaStreamCallbackFlags flags, void *context) noexcept
	{
		auto &self = *static_cast<CallbackState *>(context);
		self.m_inFlight.fetch_add(1, std::memory_order_seq_cst);
		struct Leave {
			CallbackState &state;
			~Leave() { state.m_inFlight.fetch_sub(1, std::memory_order_seq_cst); }
		} leave{self};
		self.m_callbacks.fetch_add(1, std::memory_order_release);
		// The host owns these spans. Never infer a size from an invalid count.
		if (!output || !frames || frames > 65536 || !time || (!input && !(flags & paPrimingOutput))) {
			if (output && frames <= 65536)
				std::fill_n(static_cast<float *>(output), size_t(frames) * 2, 0.0f);
			self.m_end.store(CallbackEnd::Invalid, std::memory_order_release);
			return paAbort;
		}
		std::span<float> out(static_cast<float *>(output), size_t(frames) * 2);
		if (self.m_end.load(std::memory_order_acquire) != CallbackEnd::None) {
			std::fill(out.begin(), out.end(), 0.0f);
			return paAbort;
		}
		if (self.m_stop.load(std::memory_order_seq_cst)) {
			std::fill(out.begin(), out.end(), 0.0f);
			self.m_end.store(CallbackEnd::Stop, std::memory_order_release);
			return paAbort;
		}
		AudioDuplexTime timing{time->inputBufferAdcTime,       time->outputBufferDacTime,
		                       bool(flags & paPrimingOutput),  bool(flags & paInputOverflow),
		                       bool(flags & paInputUnderflow), bool(flags & paOutputOverflow),
		                       bool(flags & paOutputUnderflow)};
		if (flags & (VIBE_PA_TIMESTAMP_ERROR | VIBE_PA_HOST_ERROR))
			timing.inputAdc = std::numeric_limits<double>::quiet_NaN();
		const std::span<const float> in(static_cast<const float *>(input),
		                                input ? size_t(frames) * self.m_channels : 0);
		const auto result = self.m_callback->process(in, out, timing);
		if (result == AudioDuplexCallbackResult::Continue)
			return paContinue;
		if (result == AudioDuplexCallbackResult::Complete) {
			self.m_end.store(CallbackEnd::Complete, std::memory_order_release);
			return paComplete;
		}
		std::fill(out.begin(), out.end(), 0.0f);
		self.m_end.store(CallbackEnd::Abort, std::memory_order_release);
		return paAbort;
	}
	std::shared_ptr<AudioPortAudioApi> m_api;
	QVector<AudioDuplexEndpoint> m_devices;
	QVector<int> m_indices;
	PaStream *m_stream = nullptr;
	AudioDuplexDeviceInfo m_info;
	QString m_error;
	bool m_initialized = false, m_started = false;
	CallbackState *const m_callbackState = new CallbackState;
	bool m_quarantined = false;
};
static_assert(std::atomic<bool>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free &&
              std::atomic<CallbackEnd>::is_always_lock_free && std::atomic<quint64>::is_always_lock_free);

#if VIBESTUDIO_HAVE_AUDIO_DUPLEX
std::shared_ptr<AudioPortAudioApi> nativeApi()
{
	// One native owner at a time keeps initialization/COM termination on the
	// same worker and avoids global PortAudio lifecycle races. Never locked by
	// the callback. Standalone Qt playback/capture use their own coordinators.
	static std::mutex lifecycle;
	static bool leased = false;
	auto api = std::make_shared<AudioPortAudioApi>();
	api->initialize = []() -> PaError {
		const std::lock_guard lock(lifecycle);
		if (leased)
			return paDeviceUnavailable;
		const auto result = Pa_Initialize();
		leased = result == paNoError;
		return result;
	};
	api->terminate = [] {
		const std::lock_guard lock(lifecycle);
		const auto result = Pa_Terminate();
		leased = result != paNoError;
		return result;
	};
	api->deviceCount = Pa_GetDeviceCount;
	api->deviceInfo = Pa_GetDeviceInfo;
	api->hostInfo = Pa_GetHostApiInfo;
	api->open = Pa_OpenStream;
	api->start = Pa_StartStream;
	api->abort = Pa_AbortStream;
	api->close = Pa_CloseStream;
	api->active = Pa_IsStreamActive;
	api->streamInfo = Pa_GetStreamInfo;
	api->errorText = Pa_GetErrorText;
#ifdef Q_OS_WIN
	api->stoppedFlags = VibePaWasapi_GetStoppedFlags;
#else
	api->stoppedFlags = [](PaStream *) { return 0UL; };
#endif
	return api;
}
#endif
} // namespace
std::unique_ptr<AudioDuplexDevice> createAudioDuplexDevice()
{
#if VIBESTUDIO_HAVE_AUDIO_DUPLEX
	return std::make_unique<PortAudioDevice>(nativeApi());
#else
	return std::make_unique<PortAudioDevice>(nullptr);
#endif
}
std::unique_ptr<AudioDuplexDevice> createAudioDuplexDeviceForTest(std::shared_ptr<AudioPortAudioApi> api)
{
	return std::make_unique<PortAudioDevice>(std::move(api));
}
} // namespace vibestudio
