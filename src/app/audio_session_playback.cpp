#include "app/audio_session_playback.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <new>

namespace vibestudio
{
namespace
{
using State = AudioSessionPlaybackSnapshot::State;
using DeviceState = AudioStreamDeviceStatus::State;
constexpr qint64 FrameBytes = 2 * sizeof(float);
QString problem(const char *text) { return QCoreApplication::translate("AudioSessionPlayback", text); }
} // namespace
class AudioSessionPlaybackWorker final : public QObject {
  public:
	AudioSessionPlaybackWorker(AudioSessionPlayback *owner, AudioStreamDeviceFactory factory,
	                           std::shared_ptr<std::atomic<quint64>> generation)
	    : m_owner(owner), m_factory(std::move(factory)), m_generation(std::move(generation))
	{
	}
	void initialize()
	{
		m_device = m_factory();
		m_timer = new QTimer(this);
		m_timer->setTimerType(Qt::PreciseTimer);
		m_timer->setInterval(5);
		connect(m_timer, &QTimer::timeout, this, [this] { pump(); });
		refreshOutputs();
	}
	void refreshOutputs()
	{
		const bool available = m_device && m_device->available();
		const auto outputs = m_device ? m_device->outputs() : QVector<AudioOutputDevice>{};
		QMetaObject::invokeMethod(
		    m_owner,
		    [owner = m_owner, available, outputs] {
			    owner->m_available = available;
			    owner->m_outputs = outputs;
			    if (!available)
				    owner->m_snapshot.state = State::Unavailable;
			    else if (owner->m_snapshot.state == State::Unavailable)
				    owner->m_snapshot.state = State::Stopped;
			    Q_EMIT owner->outputsChanged();
			    Q_EMIT owner->changed();
		    },
		    Qt::QueuedConnection);
	}
	void shutdown()
	{
		if (m_timer)
			m_timer->stop();
		if (m_device)
			m_device->reset();
		m_engine = {};
		m_pending = m_offset = 0;
	}
	void start(AudioSession session, AudioTransportRange range, AudioStreamConfiguration config, quint64 token)
	{
		shutdown();
		m_token = token;
		m_snapshot = {};
		m_snapshot.position = range.first;
		m_config = config;
		m_config.sampleRate = session.sampleRate;
		if (!current())
			return;
		if (!m_device || !m_device->available()) {
			fail(problem(QT_TRANSLATE_NOOP("AudioSessionPlayback", "Audio playback is unavailable in this build.")));
			return;
		}
		// Resolve a default only once per explicit Play. Seeks and loop changes
		// must retain that output even if the OS default changes in the meantime.
		if (m_config.deviceId.isEmpty()) {
			for (const auto &device : m_device->outputs()) {
				if (device.systemDefault) {
					m_config.deviceId = device.id;
					break;
				}
			}
		}
		if (config.bufferFrames < 256 || config.bufferFrames > 16384) {
			fail(problem(
			    QT_TRANSLATE_NOOP("AudioSessionPlayback", "Choose an output buffer between 256 and 16384 frames.")));
			return;
		}
		try {
			QString error;
			const int block = std::min(4096, config.bufferFrames);
			if (!m_engine.prepare(session, range, block, &error, control(), true)) {
				if (current())
					fail(error);
				return;
			}
			m_samples.resize(size_t(block) * 2);
			if (!current())
				return;
			m_engine.play();
			m_paused = false;
			openAt(range.first);
		} catch (const std::bad_alloc &) {
			fail(problem(
			    QT_TRANSLATE_NOOP("AudioSessionPlayback", "There is not enough memory to prepare session playback.")));
		}
	}
	void pause(quint64 token)
	{
		if (!accepts(token) || m_snapshot.state != State::Playing)
			return;
		const auto device = m_device->status();
		if (device.state == DeviceState::Error) {
			fail(device.error);
			return;
		}
		updateClock(device);
		m_device->suspend();
		m_engine.pause();
		m_paused = true;
		m_timer->stop();
		m_snapshot.state = State::Paused;
		m_snapshot.peak = {};
		publish();
	}
	void resume(quint64 token)
	{
		if (!accepts(token) || m_snapshot.state != State::Paused)
			return;
		m_device->resume();
		if (m_engine.state() == AudioTransport::State::Paused)
			m_engine.play();
		m_paused = false;
		m_snapshot.state = State::Playing;
		m_progress.restart();
		m_timer->start();
		pump();
		publish();
	}
	void seek(qint64 frame, quint64 token)
	{
		if (!accepts(token) || (m_snapshot.state != State::Playing && m_snapshot.state != State::Paused) ||
		    !m_engine.seek(frame))
			return;
		m_timer->stop();
		m_device->reset();
		m_pending = m_offset = 0;
		m_snapshot.position = frame;
		if (frame == m_engine.range().end) {
			m_snapshot.state = State::Ended;
			m_snapshot.peak = {};
			publish();
			return;
		}
		m_engine.play();
		openAt(frame);
	}
	void loop(bool loop, quint64 token)
	{
		if (!accepts(token) || m_engine.range().loop == loop)
			return;
		updateClock(m_device->status());
		const auto position = m_snapshot.position;
		m_engine.setLoop(loop);
		// Discard lookahead produced under the old loop policy before resuming.
		seek(position, token);
	}
	void volume(float value) { m_volume = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0; }
	void resetMeters(quint64 token)
	{
		if (!accepts(token))
			return;
		m_engine.resetMetering();
		m_snapshot.peak = {};
		m_snapshot.samplesAboveFullScale = 0;
		publish();
	}

  private:
	bool current() const { return m_generation->load(std::memory_order_relaxed) == m_token; }
	bool accepts(quint64 token) const { return token == m_token && current(); }
	AudioWorkControl control()
	{
		return {[this] { return !current(); }};
	}
	void fail(const QString &error)
	{
		if (m_timer)
			m_timer->stop();
		if (m_device)
			m_device->reset();
		m_snapshot.state = State::Error;
		m_snapshot.error = error;
		m_snapshot.peak = {};
		m_engine.resetMetering();
		publish();
	}
	void publish()
	{
		m_snapshot.meters = m_engine.meters();
		bool enqueue = false;
		{
			std::lock_guard lock(m_mailbox->mutex);
			m_mailbox->snapshot = m_snapshot;
			m_mailbox->token = m_token;
			enqueue = !m_mailbox->queued;
			m_mailbox->queued = true;
		}
		// One pending notification bounds memory even when the UI is blocked.
		// This mailbox is outside the allocation/lock-free rendering path.
		if (enqueue)
			QMetaObject::invokeMethod(
			    m_owner,
			    [owner = m_owner, mailbox = m_mailbox] {
				    AudioSessionPlaybackSnapshot snapshot;
				    quint64 token;
				    {
					    std::lock_guard lock(mailbox->mutex);
					    snapshot = mailbox->snapshot;
					    token = mailbox->token;
					    mailbox->queued = false;
				    }
				    if (owner->m_generation->load(std::memory_order_relaxed) != token)
					    return;
				    owner->m_snapshot = std::move(snapshot);
				    Q_EMIT owner->changed();
			    },
			    Qt::QueuedConnection);
		m_notifications.restart();
		m_snapshot.peak = {};
	}
	void updateClock(const AudioStreamDeviceStatus &device)
	{
		const quint64 consumed =
		    std::min(quint64(std::max<qint64>(0, device.processedFrames)), m_snapshot.submittedFrames);
		if (consumed > m_snapshot.processedFrames)
			m_progress.restart();
		m_snapshot.processedFrames = std::max(m_snapshot.processedFrames, consumed);
		m_snapshot.position = m_engine.positionAfter(m_origin, m_snapshot.processedFrames);
		m_snapshot.bufferFrames = device.bufferFrames;
	}
	void openAt(qint64 frame)
	{
		m_origin = frame;
		m_bytesWritten = 0;
		m_pending = m_offset = 0;
		m_idleReported = false;
		m_snapshot.submittedFrames = m_snapshot.processedFrames = m_snapshot.renderedFrames = 0;
		m_snapshot.peak = {};
		QString error;
		if (!current())
			return;
		if (!m_device->open(m_config, &error)) {
			fail(error);
			return;
		}
		if (!current()) {
			m_device->reset();
			return;
		}
		m_snapshot.state = State::Playing;
		m_progress.restart();
		m_notifications.start();
		if (m_paused) {
			m_device->suspend();
			m_engine.pause();
			m_snapshot.state = State::Paused;
		} else {
			m_timer->start();
			pump();
		}
		publish();
	}
	void pump()
	{
		if (!current()) {
			shutdown();
			return;
		}
		if (m_snapshot.state != State::Playing)
			return;
		const auto device = m_device->status();
		if (device.state == DeviceState::Error || device.state == DeviceState::Closed ||
		    device.state == DeviceState::Paused) {
			fail(device.error.isEmpty()
			         ? problem(QT_TRANSLATE_NOOP(
			               "AudioSessionPlayback",
			               "The output stopped unexpectedly. Restart playback after checking the device."))
			         : device.error);
			return;
		}
		updateClock(device);
		const bool finished = m_engine.state() == AudioTransport::State::Ended && m_pending == m_offset;
		if (finished &&
		    (device.state == DeviceState::Idle || m_snapshot.processedFrames >= m_snapshot.submittedFrames)) {
			m_snapshot.position = m_engine.range().end;
			m_snapshot.processedFrames = m_snapshot.submittedFrames;
			m_snapshot.state = State::Ended;
			m_timer->stop();
			m_device->reset();
			publish();
			return;
		}
		if (device.state == DeviceState::Idle && m_bytesWritten > 0 && !finished) {
			if (!m_idleReported)
				++m_snapshot.underruns;
			m_idleReported = true;
		} else if (device.state == DeviceState::Active) {
			m_idleReported = false;
		}
		qint64 free = std::clamp<qint64>(device.writableBytes, 0, 16384 * FrameBytes);
		// Bound each worker event so control messages cannot starve behind a large
		// or misreported device buffer. Unwritten bytes survive until the next tick.
		for (int blocks = 0; free > 0 && blocks < 8 && current(); ++blocks) {
			if (m_offset == m_pending) {
				if (m_engine.state() == AudioTransport::State::Ended || free < FrameBytes)
					break;
				const int frames = int(std::min<qint64>(free / FrameBytes, m_engine.blockFrames()));
				const auto block = m_engine.process({m_samples.data(), size_t(frames) * 2}, control());
				if (block.status != AudioSessionRenderer::BlockStatus::Ready) {
					if (current())
						fail(problem(
						    QT_TRANSLATE_NOOP("AudioSessionPlayback",
						                      "Session mixing failed. Reduce excessive gain and restart playback.")));
					return;
				}
				m_snapshot.renderedFrames = m_engine.framesRendered();
				m_snapshot.samplesAboveFullScale += block.samplesAboveFullScale;
				for (size_t channel = 0; channel < 2; ++channel)
					m_snapshot.peak[channel] = std::max(m_snapshot.peak[channel], block.peak[channel]);
				for (int i = 0; i < block.frames * 2; ++i)
					m_samples[size_t(i)] = std::clamp(m_samples[size_t(i)] * m_volume, -1.0f, 1.0f);
				m_pending = block.frames * FrameBytes;
				m_offset = 0;
				if (!m_pending)
					break;
			}
			const qint64 request = std::min(free, m_pending - m_offset);
			const auto written = m_device->write(reinterpret_cast<const char *>(m_samples.data()) + m_offset, request);
			if (written < 0 || written > request) {
				fail(problem(QT_TRANSLATE_NOOP("AudioSessionPlayback",
				                               "The audio output could not accept the next block. Playback stopped.")));
				return;
			}
			if (written == 0)
				break;
			m_offset += written;
			m_bytesWritten += quint64(written);
			m_snapshot.submittedFrames = m_bytesWritten / FrameBytes;
			free -= written;
			m_progress.restart();
		}
		if (m_progress.elapsed() > 5000) {
			fail(problem(QT_TRANSLATE_NOOP("AudioSessionPlayback",
			                               "The audio output made no progress for five seconds. Playback stopped.")));
			return;
		}
		if (m_notifications.elapsed() >= 50)
			publish();
	}
	AudioSessionPlayback *m_owner;
	struct Mailbox {
		std::mutex mutex;
		AudioSessionPlaybackSnapshot snapshot;
		quint64 token = 0;
		bool queued = false;
	};
	std::shared_ptr<Mailbox> m_mailbox = std::make_shared<Mailbox>();
	AudioStreamDeviceFactory m_factory;
	std::shared_ptr<std::atomic<quint64>> m_generation;
	std::unique_ptr<AudioStreamDevice> m_device;
	QTimer *m_timer = nullptr;
	AudioTransport m_engine;
	AudioStreamConfiguration m_config;
	AudioSessionPlaybackSnapshot m_snapshot;
	std::vector<float> m_samples;
	QElapsedTimer m_progress, m_notifications;
	quint64 m_token = 0, m_bytesWritten = 0;
	qint64 m_origin = 0, m_pending = 0, m_offset = 0;
	float m_volume = 0.7f;
	bool m_paused = false, m_idleReported = false;
};

AudioSessionPlayback::AudioSessionPlayback(QObject *parent, AudioStreamDeviceFactory factory)
    : QObject(parent), m_generation(std::make_shared<std::atomic<quint64>>(0))
{
	if (!factory)
		factory = createAudioStreamDevice;
	m_thread = new QThread(this);
	m_thread->setObjectName(QStringLiteral("audioSessionOutput"));
	m_worker = new AudioSessionPlaybackWorker(this, std::move(factory), m_generation);
	m_worker->moveToThread(m_thread);
	connect(m_thread, &QThread::started, m_worker, [worker = m_worker] { worker->initialize(); });
	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	m_thread->start();
}
AudioSessionPlayback::~AudioSessionPlayback()
{
	++*m_generation;
	QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->shutdown(); }, Qt::BlockingQueuedConnection);
	m_thread->quit();
	m_thread->wait();
}
void AudioSessionPlayback::start(const AudioSession &session, AudioTransportRange range,
                                 AudioStreamConfiguration config)
{
	const auto token = ++*m_generation;
	m_range = range;
	m_snapshot = {};
	m_snapshot.state = State::Preparing;
	m_snapshot.position = range.first;
	Q_EMIT changed();
	QMetaObject::invokeMethod(
	    m_worker, [worker = m_worker, session, range, config, token] { worker->start(session, range, config, token); },
	    Qt::QueuedConnection);
}
void AudioSessionPlayback::stop()
{
	++*m_generation;
	m_snapshot.state = m_available ? State::Stopped : State::Unavailable;
	m_snapshot.position = m_range.first;
	m_snapshot.peak = {};
	m_snapshot.meters = {};
	m_snapshot.error.clear();
	QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->shutdown(); }, Qt::QueuedConnection);
	Q_EMIT changed();
}
void AudioSessionPlayback::stopAndWait(QObject *context, std::function<void(bool)> completion)
{
	const auto token = m_generation->load() + 1;
	stop();
	QMetaObject::invokeMethod(
	    m_worker,
	    [this, token, guard = QPointer<QObject>(context), completion = std::move(completion)] {
		    // shutdown is ahead of this event on the same worker. Post back through
		    // our owner, whose destructor joins the worker, then check UI lifetime.
		    QMetaObject::invokeMethod(
		        this,
		        [this, token, guard, completion] {
			        if (guard && completion)
				        completion(m_generation->load() == token);
		        },
		        Qt::QueuedConnection);
	    },
	    Qt::QueuedConnection);
}
void AudioSessionPlayback::pause()
{
	const auto token = m_generation->load();
	QMetaObject::invokeMethod(m_worker, [worker = m_worker, token] { worker->pause(token); }, Qt::QueuedConnection);
}
void AudioSessionPlayback::resume()
{
	const auto token = m_generation->load();
	QMetaObject::invokeMethod(m_worker, [worker = m_worker, token] { worker->resume(token); }, Qt::QueuedConnection);
}
void AudioSessionPlayback::seek(qint64 frame)
{
	const auto token = m_generation->load();
	QMetaObject::invokeMethod(
	    m_worker, [worker = m_worker, frame, token] { worker->seek(frame, token); }, Qt::QueuedConnection);
}
void AudioSessionPlayback::setLoop(bool loop)
{
	m_range.loop = loop;
	const auto token = m_generation->load();
	QMetaObject::invokeMethod(
	    m_worker, [worker = m_worker, loop, token] { worker->loop(loop, token); }, Qt::QueuedConnection);
}
void AudioSessionPlayback::setVolume(float volume)
{
	QMetaObject::invokeMethod(m_worker, [worker = m_worker, volume] { worker->volume(volume); }, Qt::QueuedConnection);
}
void AudioSessionPlayback::refreshOutputs()
{
	QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->refreshOutputs(); }, Qt::QueuedConnection);
}
void AudioSessionPlayback::resetMeters()
{
	const auto token = m_generation->load();
	QMetaObject::invokeMethod(
	    m_worker, [worker = m_worker, token] { worker->resetMeters(token); }, Qt::QueuedConnection);
}
} // namespace vibestudio
