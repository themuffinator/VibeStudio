#include "app/audio_capture.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace vibestudio
{
enum class AudioCaptureStop { None, Finish, Abort };
namespace
{
using State = AudioCaptureSnapshot::State;
constexpr size_t QueueBlocks = 16;
QString problem(const char *text) { return QCoreApplication::translate("AudioCapture", text); }
class TakeStorage final : public AudioCaptureStorage {
  public:
	bool open(const AudioCaptureRequest &request, QString *error) override
	{
		return writer.open(request.path, request.metadata, error);
	}
	bool append(std::span<const float> samples, QString *error) override { return writer.append(samples, error); }
	bool finish(QString *error) override { return writer.finish(error); }
	void close() override { writer.close(); }

  private:
	AudioTakeWriter writer;
};
struct CapturePipe {
	struct Block {
		std::vector<float> samples;
		size_t count = 0;
	};
	std::array<Block, QueueBlocks> blocks;
	std::atomic<quint64> produced{0}, consumed{0};
	std::atomic<qint64> stored{0};
	std::atomic_bool ready{false}, done{false}, ending{false}, clean{true}, failed{false};
	bool opened = false;
	QString error;      // Published only by ready (open failure) or done.
	AudioTakeInfo take; // Published by done.
	std::mutex wakeMutex;
	std::condition_variable wake;
	std::thread thread;
	CapturePipe(const AudioCaptureRequest &request, AudioCaptureStorageFactory factory)
	{
		for (auto &block : blocks)
			block.samples.resize(size_t(AudioTakeBlockFrames * request.metadata.channelMap.size()));
		thread = std::thread([this, request, factory] {
			try {
				auto storage = factory();
				opened = storage && storage->open(request, &error);
				if (!opened && error.isEmpty())
					error = problem(QT_TRANSLATE_NOOP("AudioCapture", "Take storage could not start."));
				ready.store(true, std::memory_order_release);
				if (opened) {
					for (;;) {
						const auto index = consumed.load(std::memory_order_relaxed);
						if (index != produced.load(std::memory_order_acquire)) {
							const auto &block = blocks[index % QueueBlocks];
							if (!storage->append({block.samples.data(), block.count}, &error)) {
								failed.store(true, std::memory_order_release);
								if (error.isEmpty())
									error = problem(QT_TRANSLATE_NOOP("AudioCapture",
									                                  "Take storage could not write the next block."));
								break;
							}
							stored.fetch_add(qint64(block.count / request.metadata.channelMap.size()));
							consumed.store(index + 1, std::memory_order_release);
							continue;
						}
						if (ending.load(std::memory_order_acquire) && index == produced.load(std::memory_order_acquire))
							break;
						std::unique_lock lock(wakeMutex);
						// Timed wait also covers a notification racing the empty check.
						wake.wait_for(lock, std::chrono::milliseconds(20));
					}
					if (error.isEmpty() && clean.load() && !storage->finish(&error) && error.isEmpty())
						error = problem(
						    QT_TRANSLATE_NOOP("AudioCapture", "The take completion record could not be stored."));
					storage->close();
					take = inspectAudioTake(request.path);
				}
			} catch (const std::exception &) {
				error = problem(QT_TRANSLATE_NOOP(
				    "AudioCapture", "Take storage failed. Inspect the saved file to recover verified blocks."));
				ready.store(true, std::memory_order_release);
			}
			done.store(true, std::memory_order_release);
		});
	}
	~CapturePipe()
	{
		end(false);
		if (thread.joinable())
			thread.join();
	}
	void end(bool complete)
	{
		if (!complete)
			clean = false;
		ending.store(true, std::memory_order_release);
		wake.notify_one();
	}
	bool push(std::span<const float> samples)
	{
		const auto index = produced.load(std::memory_order_relaxed);
		if (done.load(std::memory_order_acquire) || index - consumed.load(std::memory_order_acquire) >= QueueBlocks)
			return false;
		auto &block = blocks[index % QueueBlocks];
		std::copy(samples.begin(), samples.end(), block.samples.begin());
		block.count = samples.size();
		produced.store(index + 1, std::memory_order_release);
		wake.notify_one();
		return true;
	}
};
template <typename T> T sampleAt(const char *source)
{
	T value;
	std::memcpy(&value, source, sizeof(value));
	return value;
}
} // namespace
class AudioCaptureWorker final : public QObject {
  public:
	AudioCaptureWorker(AudioCapture *owner, AudioCaptureDeviceFactory device, AudioCaptureStorageFactory storage)
	    : m_owner(owner), m_factory(std::move(device)), m_storage(std::move(storage))
	{
	}
	void initialize()
	{
		m_device = m_factory();
		m_timer = new QTimer(this);
		m_timer->setTimerType(Qt::PreciseTimer);
		m_timer->setInterval(5);
		connect(m_timer, &QTimer::timeout, this, [this] { pump(); });
		refreshInputs();
	}
	void refreshInputs()
	{
		const bool available = m_device && m_device->available();
		const auto inputs = m_device ? m_device->inputs() : QVector<AudioInputDevice>{};
		QMetaObject::invokeMethod(
		    m_owner,
		    [owner = m_owner, available, inputs] {
			    owner->m_available = available;
			    owner->m_inputs = inputs;
			    if (!available && !owner->busy())
				    owner->m_snapshot.state = State::Unavailable;
			    Q_EMIT owner->inputsChanged();
			    Q_EMIT owner->changed();
		    },
		    Qt::QueuedConnection);
	}
	void start(AudioCaptureRequest request, std::shared_ptr<std::atomic<AudioCaptureStop>> stop, quint64 epoch)
	{
		m_epoch = epoch;
		m_stop = std::move(stop);
		m_request = std::move(request);
		// Stamp preparation after permission, not the earlier Record click.
		// This is provenance, not a sample-exact hardware timestamp.
		m_request.metadata.startedUtc = QDateTime::currentDateTimeUtc();
		m_snapshot = {};
		m_snapshot.state = State::Preparing;
		m_carry = 0;
		m_frames = 0;
		if (m_stop->load() != AudioCaptureStop::None) {
			m_snapshot.state = State::Finished;
			publish();
			return;
		}
		try {
			m_bytes.resize(AudioTakeBlockFrames * m_request.metadata.inputChannels * 4);
			m_samples.resize(size_t(AudioTakeBlockFrames * m_request.metadata.channelMap.size()));
			m_pipe = std::make_unique<CapturePipe>(m_request, m_storage);
			m_notifications.start();
			m_timer->start();
			publish();
		} catch (const std::exception &) {
			m_snapshot.state = State::Error;
			m_snapshot.error = problem(QT_TRANSLATE_NOOP(
			    "AudioCapture", "There is not enough memory or a worker thread could not start recording."));
			publish();
		}
	}
	void shutdown()
	{
		m_timer->stop();
		if (m_device)
			m_device->close();
		if (m_pipe) {
			flush();
			m_pipe->end(false);
			m_pipe.reset(); // Join storage before destroying the UI owner.
		}
	}

  private:
	bool flush()
	{
		if (!m_frames)
			return true;
		const bool ok = m_pipe->push({m_samples.data(), size_t(m_frames * m_request.metadata.channelMap.size())});
		if (ok)
			m_snapshot.queuedFrames += m_frames;
		m_frames = 0;
		return ok;
	}
	void stopInput(QString error = {})
	{
		if (m_snapshot.state == State::Stopping)
			return;
		m_device->close();
		if (!flush() && error.isEmpty())
			error = problem(QT_TRANSLATE_NOOP(
			    "AudioCapture",
			    "The recording queue is full. Capture stopped; earlier queued blocks are being saved."));
		if (m_carry && error.isEmpty())
			error = problem(QT_TRANSLATE_NOOP(
			    "AudioCapture", "Input ended in a partial sample frame. Only complete frames are retained."));
		m_snapshot.error = error;
		m_snapshot.state = State::Stopping;
		// A timer may observe destruction before the queued shutdown runs. It
		// must retain an incomplete prefix, never publish a normal Stop footer.
		m_pipe->end(error.isEmpty() && m_stop->load() != AudioCaptureStop::Abort);
		publish();
	}
	float decode(const char *source) const
	{
		switch (m_encoding) {
		case AudioInputEncoding::Float32:
			return sampleAt<float>(source);
		case AudioInputEncoding::Int32:
			return float(double(sampleAt<qint32>(source)) / 2147483648.0);
		case AudioInputEncoding::Int16:
			return float(sampleAt<qint16>(source)) / 32768.0f;
		case AudioInputEncoding::UInt8:
			return (float(static_cast<unsigned char>(*source)) - 128) / 128;
		}
		return 0;
	}
	void pump()
	{
		if (!m_pipe)
			return;
		if (m_pipe->done.load(std::memory_order_acquire)) {
			m_device->close();
			m_timer->stop();
			m_snapshot.storedFrames = m_pipe->stored.load();
			m_snapshot.queuedBlocks = 0;
			m_snapshot.take = m_pipe->take;
			if (!m_pipe->error.isEmpty())
				m_snapshot.error = m_pipe->error;
			if (m_snapshot.error.isEmpty() && !m_snapshot.take.complete)
				m_snapshot.error = m_snapshot.take.error;
			m_snapshot.state = m_snapshot.error.isEmpty() ? State::Finished : State::Error;
			m_pipe.reset();
			publish();
			return;
		}
		if (!m_pipe->ready.load(std::memory_order_acquire))
			return;
		if (!m_pipe->opened)
			return; // done publishes its open failure next.
		if (m_pipe->failed.load(std::memory_order_acquire) && m_snapshot.state != State::Stopping) {
			stopInput(problem(QT_TRANSLATE_NOOP(
			    "AudioCapture", "Take storage failed. Capture stopped while the retained file is verified.")));
		}
		if (m_stop->load() != AudioCaptureStop::None && m_snapshot.state != State::Stopping)
			stopInput();
		if (m_snapshot.state == State::Preparing) {
			QString error;
			if (!m_device || !m_device->available() || !m_device->open(m_request, &m_encoding, &error)) {
				if (error.isEmpty())
					error = problem(QT_TRANSLATE_NOOP("AudioCapture", "The selected input could not start."));
				stopInput(error);
				return;
			}
			m_sampleBytes = m_encoding == AudioInputEncoding::UInt8   ? 1
			                : m_encoding == AudioInputEncoding::Int16 ? 2
			                                                          : 4;
			m_frameBytes = m_sampleBytes * m_request.metadata.inputChannels;
			m_snapshot.state = State::Recording;
			m_progress.start();
			publish();
		}
		if (m_snapshot.state == State::Recording) {
			const auto status = m_device->status();
			m_snapshot.bufferFrames = status.bufferFrames;
			if (!status.error.isEmpty()) {
				stopInput(status.error);
				return;
			}
			qint64 available = std::clamp<qint64>(status.availableBytes, 0, 16384LL * m_frameBytes);
			for (int batch = 0; available > 0 && batch < 16; ++batch) {
				if (m_stop->load() != AudioCaptureStop::None) {
					stopInput();
					return;
				}
				const auto request = std::min<qint64>(available, AudioTakeBlockFrames * m_frameBytes - m_carry);
				const auto got = m_device->read(m_bytes.data() + m_carry, request);
				if (got < 0 || got > request) {
					stopInput(problem(
					    QT_TRANSLATE_NOOP("AudioCapture", "The audio input could not deliver the next block.")));
					return;
				}
				if (!got)
					break;
				available -= got;
				m_progress.restart();
				const qint64 bytes = got + m_carry;
				const int frames = int(bytes / m_frameBytes);
				const auto channels = m_request.metadata.channelMap.size();
				for (int frame = 0; frame < frames; ++frame) {
					for (qsizetype channel = 0; channel < channels; ++channel) {
						const float sample = decode(m_bytes.constData() + frame * m_frameBytes +
						                            m_request.metadata.channelMap[channel] * m_sampleBytes);
						if (!std::isfinite(sample)) {
							m_carry = 0;
							stopInput(problem(QT_TRANSLATE_NOOP(
							    "AudioCapture",
							    "The input delivered a non-finite sample. Capture stopped before that frame.")));
							return;
						}
						m_samples[size_t(m_frames * channels + channel)] = sample;
						m_snapshot.peak[size_t(channel)] = std::max(m_snapshot.peak[size_t(channel)], std::abs(sample));
						if (std::abs(sample) > 1)
							++m_snapshot.samplesAboveFullScale;
					}
					++m_frames;
					++m_snapshot.receivedFrames;
					if (m_frames == AudioTakeBlockFrames && !flush()) {
						m_carry = 0;
						stopInput(problem(QT_TRANSLATE_NOOP(
						    "AudioCapture",
						    "The recording queue is full. Capture stopped; earlier queued blocks are being saved.")));
						return;
					}
				}
				m_carry = int(bytes % m_frameBytes);
				if (m_carry)
					std::memmove(m_bytes.data(), m_bytes.constData() + frames * m_frameBytes, size_t(m_carry));
			}
			if (m_progress.elapsed() > 5000) {
				stopInput(problem(QT_TRANSLATE_NOOP("AudioCapture",
				                                    "The input delivered no data for five seconds. Capture stopped.")));
				return;
			}
		}
		if (m_notifications.elapsed() >= 50)
			publish();
	}
	void publish()
	{
		if (m_pipe) {
			m_snapshot.storedFrames = m_pipe->stored.load();
			const auto read = m_pipe->consumed.load();
			m_snapshot.queuedBlocks = int(m_pipe->produced.load() - read);
		}
		const auto snapshot = m_snapshot;
		QMetaObject::invokeMethod(
		    m_owner,
		    [owner = m_owner, snapshot, epoch = m_epoch] {
			    if (owner->m_epoch != epoch)
				    return;
			    owner->m_snapshot = snapshot;
			    Q_EMIT owner->changed();
		    },
		    Qt::QueuedConnection);
		m_notifications.restart();
	}
	AudioCapture *m_owner;
	AudioCaptureDeviceFactory m_factory;
	AudioCaptureStorageFactory m_storage;
	std::unique_ptr<AudioCaptureDevice> m_device;
	std::unique_ptr<CapturePipe> m_pipe;
	std::shared_ptr<std::atomic<AudioCaptureStop>> m_stop;
	QTimer *m_timer = nullptr;
	QElapsedTimer m_progress, m_notifications;
	AudioCaptureRequest m_request;
	AudioCaptureSnapshot m_snapshot;
	AudioInputEncoding m_encoding = AudioInputEncoding::Float32;
	QByteArray m_bytes;
	std::vector<float> m_samples;
	quint64 m_epoch = 0;
	int m_carry = 0, m_frames = 0, m_sampleBytes = 4, m_frameBytes = 4;
};
AudioCapture::AudioCapture(QObject *parent, AudioCaptureDeviceFactory device, AudioCaptureStorageFactory storage)
    : QObject(parent), m_native(!device)
{
	if (!device)
		device = createAudioCaptureDevice;
	if (!storage)
		storage = [] { return std::make_unique<TakeStorage>(); };
	m_thread = new QThread(this);
	m_thread->setObjectName("audioInputCapture");
	m_worker = new AudioCaptureWorker(this, std::move(device), std::move(storage));
	m_worker->moveToThread(m_thread);
	connect(m_thread, &QThread::started, m_worker, [worker = m_worker] { worker->initialize(); });
	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	m_thread->start();
}
AudioCapture::~AudioCapture()
{
	++m_epoch;
	if (m_stop)
		*m_stop = AudioCaptureStop::Abort;
	QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->shutdown(); }, Qt::BlockingQueuedConnection);
	m_thread->quit();
	m_thread->wait();
}
bool AudioCapture::busy() const
{
	return m_snapshot.state == State::Permission || m_snapshot.state == State::Preparing ||
	       m_snapshot.state == State::Recording || m_snapshot.state == State::Stopping;
}
bool AudioCapture::start(const AudioCaptureRequest &request)
{
	if (busy())
		return false;
	m_snapshot = {};
	m_snapshot.error = validateAudioTakeMetadata(request.metadata);
	if (m_snapshot.error.isEmpty() && (!m_available || request.deviceId.isEmpty() || request.path.isEmpty() ||
	                                   request.bufferFrames < 256 || request.bufferFrames > 16384))
		m_snapshot.error = problem(QT_TRANSLATE_NOOP(
		    "AudioCapture", "Choose an available input, a new take path and a buffer of 256–16384 frames."));
	if (!m_snapshot.error.isEmpty()) {
		m_snapshot.state = State::Error;
		Q_EMIT changed();
		return false;
	}
	const auto epoch = ++m_epoch;
	m_stop = std::make_shared<std::atomic<AudioCaptureStop>>(AudioCaptureStop::None);
	m_snapshot.state = State::Permission;
	Q_EMIT changed();
	const auto granted = [this, request, epoch](QString error) {
		if (m_epoch != epoch || m_stop->load() != AudioCaptureStop::None)
			return;
		if (!error.isEmpty()) {
			m_snapshot.state = State::Error;
			m_snapshot.error = error;
			Q_EMIT changed();
			return;
		}
		m_snapshot.state = State::Preparing;
		Q_EMIT changed();
		QMetaObject::invokeMethod(
		    m_worker, [worker = m_worker, request, stop = m_stop, epoch] { worker->start(request, stop, epoch); },
		    Qt::QueuedConnection);
	};
	if (m_native)
		requestAudioCapturePermission(this, granted);
	else
		granted({});
	return true;
}
void AudioCapture::stop()
{
	if (!busy())
		return;
	if (m_stop) {
		auto expected = AudioCaptureStop::None;
		m_stop->compare_exchange_strong(expected, AudioCaptureStop::Finish);
	}
	if (m_snapshot.state == State::Permission) {
		++m_epoch;
		m_snapshot.state = State::Finished;
	} else
		m_snapshot.state = State::Stopping;
	Q_EMIT changed();
}
void AudioCapture::refreshInputs()
{
	QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->refreshInputs(); }, Qt::QueuedConnection);
}
} // namespace vibestudio
