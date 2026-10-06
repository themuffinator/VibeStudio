#include "app/audio_recording.h"
#include "core/audio_duplex_queue.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <limits>
#include <mutex>
#include <thread>

namespace vibestudio
{
enum class AudioRecordingStop { None, Finish, Abort };
namespace
{
using State = AudioRecordingSnapshot::State;
using ProgressState = AudioDuplexProgress::State;
using DeviceState = AudioDuplexDeviceStatus::State;
using Outcome = AudioRecordingReceipt::Outcome;
QString recordingText(const char *source) { return QCoreApplication::translate("AudioRecording", source); }
QString storageProblem()
{
	return recordingText(
	    QT_TRANSLATE_NOOP("AudioRecording", "Recording storage failed. Review each verified take prefix."));
}
class TakeStorage final : public AudioCaptureStorage {
  public:
	bool open(const AudioCaptureRequest &request, QString *error) override
	{
		return m_writer.open(request.path, request.metadata, error);
	}
	bool append(std::span<const float> samples, QString *error) override { return m_writer.append(samples, error); }
	bool finish(QString *error) override { return m_writer.finish(error); }
	void close() override { m_writer.close(); }

  private:
	AudioTakeWriter m_writer;
};

// One producer and one consumer. Full telemetry drops a display update only;
// it cannot affect the separate, loss-intolerant audio queue.
struct Telemetry {
	AudioDuplexProgress progress;
	AudioMeterSnapshot meters;
	AudioDuplexMeters levels;
	quint64 meterEpoch = 0;
};
class TelemetryQueue {
  public:
	bool lockFree() const { return m_write.is_lock_free() && m_read.is_lock_free(); }
	void push(const Telemetry &value) noexcept
	{
		const auto write = m_write.load(std::memory_order_relaxed);
		const auto next = (write + 1) % m_slots.size();
		if (next == m_read.load(std::memory_order_acquire))
			return;
		m_slots[write] = value;
		m_write.store(next, std::memory_order_release);
	}
	void drain(AudioRecordingSnapshot &snapshot)
	{
		auto read = m_read.load(std::memory_order_relaxed);
		// Bound each poll even if a synthetic producer runs faster than hardware.
		const auto end = m_write.load(std::memory_order_acquire);
		while (read != end) {
			snapshot.progress = m_slots[read].progress;
			snapshot.meters = m_slots[read].meters;
			snapshot.levels = m_slots[read].levels;
			snapshot.meterEpoch = m_slots[read].meterEpoch;
			read = (read + 1) % m_slots.size();
			m_read.store(read, std::memory_order_release);
		}
	}

  private:
	std::array<Telemetry, 4> m_slots;
	alignas(64) std::atomic<size_t> m_write{0};
	alignas(64) std::atomic<size_t> m_read{0};
};

class RecordingDisk {
  public:
	RecordingDisk(AudioDuplexCaptureQueue &queue, AudioRecordingRequest request, AudioCaptureStorageFactory factory)
	    : m_queue(queue), m_request(std::move(request)), m_factory(std::move(factory))
	{
	}
	~RecordingDisk()
	{
		if (!m_ending.load(std::memory_order_acquire))
			end({});
		join();
	}
	void start()
	{
		m_thread = std::thread([this] { run(); });
	}
	void end(AudioRecordingReceipt receipt)
	{
		m_receipt = std::move(receipt);
		m_ending.store(true, std::memory_order_release);
	}
	void join()
	{
		if (m_thread.joinable())
			m_thread.join();
	}
	std::atomic<bool> ready{false}, failed{false}, done{false};
	std::array<std::atomic<qint64>, AudioDuplexArmLimit> stored{};
	bool opened = false;       // Published by ready; immutable thereafter.
	QString error;             // Read only after done.
	AudioRecordingInfo result; // Read only after done.
  private:
	void problem(QString detail)
	{
		if (error.isEmpty())
			error = detail.isEmpty() ? storageProblem() : std::move(detail);
		failed.store(true, std::memory_order_release);
	}
	void pause() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
	void run()
	{
		std::array<std::unique_ptr<AudioCaptureStorage>, AudioDuplexArmLimit> writers;
		QByteArray planDigest;
		QString detail;
		bool folder = false;
		try {
			folder = createAudioRecordingFolder(m_request.directory, m_request.plan, &planDigest, &detail);
			if (!folder)
				problem(detail);
			else {
				for (int i = 0; i < m_request.plan.pass.arms.size(); ++i) {
					writers[size_t(i)] = m_factory();
					AudioCaptureRequest request;
					request.path = audioRecordingArmPath(m_request.directory, i);
					request.metadata = audioRecordingArmMetadata(m_request.plan, i);
					if (!writers[size_t(i)] || !writers[size_t(i)]->open(request, &detail)) {
						problem(detail);
						break;
					}
				}
			}
			opened = !failed.load(std::memory_order_acquire);
		} catch (...) {
			problem(storageProblem());
		}
		ready.store(true, std::memory_order_release);
		try {
			while (!failed.load(std::memory_order_acquire)) {
				// Check ending before reading: once observed, callback production has
				// joined, so an empty read proves there is no unconsumed final block.
				const bool ending = m_ending.load(std::memory_order_acquire);
				const auto block = m_queue.read();
				if (!block.frames) {
					if (ending)
						break;
					pause();
					continue;
				}
				for (int i = 0; i < block.arms; ++i) {
					if (!writers[size_t(i)]->append(block.samples[size_t(i)], &detail)) {
						problem(detail);
						break;
					}
					stored[size_t(i)].fetch_add(block.frames, std::memory_order_release);
				}
				if (!failed.load(std::memory_order_acquire) && !m_queue.consume(block.frames))
					problem(storageProblem());
			}
		} catch (...) {
			problem(storageProblem());
		}
		// Even on disk failure, only the control thread closes the device. It
		// publishes final timing after joining callbacks, before setting ending.
		while (!m_ending.load(std::memory_order_acquire))
			pause();
		if (!failed.load(std::memory_order_acquire) && m_receipt.outcome != Outcome::Interrupted) {
			for (auto &writer : writers) {
				if (!writer)
					continue;
				try {
					if (!writer->finish(&detail))
						problem(detail);
				} catch (...) {
					problem(storageProblem());
				}
				if (failed.load(std::memory_order_acquire))
					break;
			}
		}
		for (auto &writer : writers) {
			if (!writer)
				continue;
			try {
				writer->close();
			} catch (...) {
				problem(storageProblem());
			}
		}
		if (failed.load(std::memory_order_acquire)) {
			m_receipt.outcome = Outcome::Interrupted;
			if (!m_receipt.error.isEmpty())
				m_receipt.error += QLatin1Char('\n');
			m_receipt.error += error;
		}
		try {
			if (folder && !finishAudioRecordingFolder(m_request.directory, planDigest, m_receipt, &detail, &result))
				problem(detail);
		} catch (...) {
			problem(storageProblem());
		}
		done.store(true, std::memory_order_release);
	}
	AudioDuplexCaptureQueue &m_queue;
	AudioRecordingRequest m_request;
	AudioCaptureStorageFactory m_factory;
	AudioRecordingReceipt m_receipt;
	std::atomic<bool> m_ending{false};
	std::thread m_thread;
};

class RecordingCallback final : public AudioDuplexDeviceCallback {
  public:
	AudioDuplexProcessor processor;
	AudioDuplexCaptureQueue queue;
	TelemetryQueue telemetry;
	std::shared_ptr<std::atomic<AudioRecordingStop>> stop;
	std::shared_ptr<std::atomic<quint64>> meterReset;
	quint64 meterEpoch = 0;
	void applyMeterReset() noexcept
	{
		const auto requested = meterReset->load(std::memory_order_relaxed);
		if (requested != meterEpoch) {
			processor.resetMetering();
			meterEpoch = requested;
		}
	}
	const std::atomic<bool> *diskFailed = nullptr;
	AudioDuplexCallbackResult process(std::span<const float> input, std::span<float> output,
	                                  const AudioDuplexTime &time) noexcept override
	{
		ProgressState state;
		applyMeterReset();
		if (stop->load(std::memory_order_acquire) != AudioRecordingStop::None ||
		    diskFailed->load(std::memory_order_acquire)) {
			processor.stop();
			std::fill(output.begin(), output.end(), 0);
			state = ProgressState::Stopped;
		} else
			state = processor.process(input, output, time, queue);
		telemetry.push({processor.progress(), processor.meters(), processor.levels(), meterEpoch});
		if (state == ProgressState::Complete)
			return AudioDuplexCallbackResult::Complete;
		if (state == ProgressState::Error || state == ProgressState::Stopped)
			return AudioDuplexCallbackResult::Abort;
		return AudioDuplexCallbackResult::Continue;
	}
};
} // namespace

class AudioRecordingWorker final : public QObject {
  public:
	AudioRecordingWorker(AudioRecording *owner, AudioDuplexDeviceFactory factory, AudioCaptureStorageFactory storage)
	    : m_owner(owner), m_factory(std::move(factory)), m_storage(std::move(storage))
	{
	}
	void initialize()
	{
		m_device = m_factory();
		m_timer = new QTimer(this);
		m_timer->setTimerType(Qt::PreciseTimer);
		m_timer->setInterval(5);
		connect(m_timer, &QTimer::timeout, this, [this] { pump(); });
		refresh();
	}
	void refresh()
	{
		if (m_callback)
			return;
		const bool available = m_device && m_device->available();
		QString error;
		const auto devices = available ? m_device->enumerate(&error) : QVector<AudioDuplexEndpoint>{};
		QMetaObject::invokeMethod(
		    m_owner,
		    [owner = m_owner, available, devices, error] {
			    owner->m_available = available;
			    owner->m_devices = devices;
			    if (!owner->busy()) {
				    if (!available)
					    owner->m_snapshot.state = State::Unavailable;
				    else if (!error.isEmpty()) {
					    owner->m_snapshot.state = State::Error;
					    owner->m_snapshot.error = error;
				    }
			    }
			    Q_EMIT owner->devicesChanged();
			    Q_EMIT owner->changed();
		    },
		    Qt::QueuedConnection);
	}
	void start(AudioSession session, AudioRecordingRequest request,
	           std::shared_ptr<std::atomic<AudioRecordingStop>> stop, std::shared_ptr<std::atomic<quint64>> meterReset,
	           quint64 epoch)
	{
		m_epoch = epoch;
		m_snapshot = {};
		m_snapshot.state = State::Preparing;
		m_request = std::move(request);
		m_stop = std::move(stop);
		m_notifications.start();
		try {
			if (m_stop->load() != AudioRecordingStop::None) {
				m_snapshot.state = State::Finished;
				publish();
				return;
			}
			m_callback = std::make_unique<RecordingCallback>();
			m_callback->stop = m_stop;
			m_callback->meterReset = std::move(meterReset);
			QString error;
			if (!m_callback->processor.prepare(session, m_request.plan.pass, &error, {[stop = m_stop] {
				                                   return stop->load() != AudioRecordingStop::None;
			                                   }})) {
				m_snapshot.state = m_stop->load() == AudioRecordingStop::None ? State::Error : State::Finished;
				if (m_snapshot.state == State::Error)
					m_snapshot.error = error;
				m_callback.reset();
				publish();
				return;
			}
			std::array<int, AudioDuplexArmLimit> channels{};
			for (int i = 0; i < m_request.plan.pass.arms.size(); ++i)
				channels[size_t(i)] = m_request.plan.pass.arms[i].channels;
			if (!m_callback->telemetry.lockFree() || !m_stop->is_lock_free() ||
			    !m_callback->meterReset->is_lock_free() ||
			    !m_callback->queue.prepare(m_request.plan.pass.punchFirst,
			                               {channels.data(), size_t(m_request.plan.pass.arms.size())},
			                               m_request.queueFrames)) {
				failPreparation(recordingText(
				    QT_TRANSLATE_NOOP("AudioRecording", "The bounded recording queues could not be prepared.")));
				return;
			}
			m_request.plan.startedUtc = QDateTime::currentDateTimeUtc();
			m_disk = std::make_unique<RecordingDisk>(m_callback->queue, m_request, m_storage);
			m_callback->diskFailed = &m_disk->failed;
			if (!m_disk->failed.is_lock_free()) {
				failPreparation(recordingText(
				    QT_TRANSLATE_NOOP("AudioRecording", "The bounded recording queues could not be prepared.")));
				return;
			}
			m_disk->start();
			m_timer->start();
			publish();
		} catch (const std::exception &) {
			failPreparation(recordingText(
			    QT_TRANSLATE_NOOP("AudioRecording", "There are not enough resources to prepare recording.")));
		}
	}
	void shutdown()
	{
		if (m_timer)
			m_timer->stop();
		if (m_callback && m_disk && !m_saving)
			finish(Outcome::Interrupted,
			       recordingText(QT_TRANSLATE_NOOP("AudioRecording", "Recording was interrupted during shutdown.")));
		if (m_device)
			m_device->close();
		m_disk.reset(); // Joins disk work before its queue or callback is destroyed.
		m_callback.reset();
		m_device.reset(); // Native destruction must remain on its control thread.
	}

  private:
	void failPreparation(QString error)
	{
		m_disk.reset();
		m_callback.reset();
		m_snapshot.state = State::Error;
		m_snapshot.error = std::move(error);
		publish();
	}
	void finish(Outcome outcome, QString error = {})
	{
		m_device->close(); // Joins/detaches callbacks before touching processor state.
		const auto closed = m_device->status();
		if (closed.state == DeviceState::Error) {
			outcome = Outcome::Interrupted;
			const auto detail = closed.error.isEmpty()
			                        ? recordingText(QT_TRANSLATE_NOOP("AudioRecording", "The recording device failed."))
			                        : closed.error;
			if (error.isEmpty())
				error = detail;
			else if (error != detail)
				error += QLatin1Char('\n') + detail;
		}
		m_active = false;
		m_callback->processor.stop();
		m_callback->applyMeterReset();
		m_snapshot.progress = m_callback->processor.progress();
		m_snapshot.meters = m_callback->processor.meters();
		m_snapshot.levels = m_callback->processor.levels();
		m_snapshot.meterEpoch = m_callback->meterEpoch;
		if (outcome == Outcome::Complete && m_snapshot.progress.state != ProgressState::Complete) {
			outcome = Outcome::Interrupted;
			error = recordingText(
			    QT_TRANSLATE_NOOP("AudioRecording", "The recording device ended before the pass completed."));
		}
		if (m_snapshot.progress.fault != AudioDuplexFault::None) {
			outcome = Outcome::Interrupted;
			if (error.isEmpty())
				error = audioDuplexFaultText(m_snapshot.progress.fault);
		}
		m_snapshot.error = std::move(error);
		AudioRecordingReceipt receipt;
		receipt.outcome = outcome;
		receipt.error = m_snapshot.error;
		receipt.progress = m_snapshot.progress;
		receipt.inputName = m_snapshot.device.inputName;
		receipt.outputName = m_snapshot.device.outputName;
		receipt.host = m_snapshot.device.host;
		receipt.inputLatencySeconds = m_snapshot.device.inputLatencySeconds;
		receipt.outputLatencySeconds = m_snapshot.device.outputLatencySeconds;
		receipt.packetInputTimestamp = m_snapshot.device.packetInputTimestamp;
		m_disk->end(std::move(receipt));
		m_saving = true;
		m_snapshot.state = State::Saving;
		publish();
	}
	void pump()
	{
		if (!m_disk)
			return;
		if (m_saving) {
			// Native close has joined callbacks. Resets during disk finalization
			// can now be applied by this owner without touching captured samples.
			m_callback->applyMeterReset();
			m_snapshot.meters = m_callback->processor.meters();
			m_snapshot.levels = m_callback->processor.levels();
			m_snapshot.meterEpoch = m_callback->meterEpoch;
			if (m_disk->done.load(std::memory_order_acquire)) {
				m_disk->join();
				updateStored();
				m_snapshot.result = m_disk->result;
				// Final inspection, including a partial append, is authoritative.
				for (int i = 0; i < m_snapshot.result.takes.size(); ++i)
					m_snapshot.storedFrames[size_t(i)] = m_snapshot.result.takes[i].frames;
				if (!m_disk->error.isEmpty()) {
					if (!m_snapshot.error.isEmpty())
						m_snapshot.error += QLatin1Char('\n');
					m_snapshot.error += m_disk->error;
				}
				m_disk.reset();
				m_callback.reset();
				m_saving = false;
				m_timer->stop();
				m_snapshot.state = m_snapshot.error.isEmpty() ? State::Finished : State::Error;
				publish();
			} else if (m_notifications.elapsed() >= 50)
				publish();
			return;
		}
		if (!m_disk->ready.load(std::memory_order_acquire))
			return;
		if (m_disk->failed.load(std::memory_order_acquire)) {
			finish(Outcome::Interrupted); // Disk publishes its detailed error after joining.
			return;
		}
		const auto stop = m_stop->load(std::memory_order_acquire);
		if (stop != AudioRecordingStop::None) {
			finish(stop == AudioRecordingStop::Finish ? Outcome::Stopped : Outcome::Interrupted);
			return;
		}
		if (!m_active) {
			QString error;
			if (!m_device->open(m_request.device, *m_callback, &error)) {
				finish(Outcome::Interrupted,
				       error.isEmpty() ? recordingText(QT_TRANSLATE_NOOP("AudioRecording",
				                                                         "The recording device could not be opened."))
				                       : error);
				return;
			}
			m_snapshot.device = m_device->info();
			if (m_stop->load() != AudioRecordingStop::None) {
				finish(Outcome::Stopped);
				return;
			}
			if (!m_device->start(&error)) {
				finish(Outcome::Interrupted,
				       error.isEmpty() ? recordingText(QT_TRANSLATE_NOOP("AudioRecording",
				                                                         "The recording device could not be started."))
				                       : error);
				return;
			}
			m_active = true;
			m_snapshot.state = State::Recording;
			m_watchdog.start();
			m_lastProcessed = 0;
			publish();
		}
		m_callback->telemetry.drain(m_snapshot);
		const auto status = m_device->status();
		if (m_snapshot.progress.processedFrames != m_lastProcessed) {
			m_lastProcessed = m_snapshot.progress.processedFrames;
			m_watchdog.restart();
		}
		if (status.state == DeviceState::Error) {
			finish(Outcome::Interrupted,
			       status.error.isEmpty()
			           ? recordingText(QT_TRANSLATE_NOOP("AudioRecording", "The recording device failed."))
			           : status.error);
			return;
		}
		if (status.state == DeviceState::Complete) {
			// A terminal status joins the callback only when close returns. Inspect
			// the final processor directly then, not a possibly dropped UI update.
			finish(Outcome::Complete);
			return;
		}
		if (status.state == DeviceState::Stopped || status.state == DeviceState::Closed) {
			finish(Outcome::Interrupted,
			       recordingText(QT_TRANSLATE_NOOP("AudioRecording", "The recording device stopped unexpectedly.")));
			return;
		}
		if (m_snapshot.progress.state == ProgressState::Error) {
			finish(Outcome::Interrupted, audioDuplexFaultText(m_snapshot.progress.fault));
			return;
		}
		if (status.state == DeviceState::Draining && m_snapshot.state != State::Draining) {
			m_snapshot.state = State::Draining;
			m_watchdog.restart();
			publish();
		}
		const auto timeout =
		    m_snapshot.state == State::Draining
		        ? std::max(m_request.stallTimeoutMs, int(m_snapshot.device.outputLatencySeconds * 1000) + 1000)
		        : m_request.stallTimeoutMs;
		if (m_watchdog.elapsed() > timeout) {
			finish(Outcome::Interrupted,
			       recordingText(QT_TRANSLATE_NOOP(
			           "AudioRecording",
			           "The recording device stopped making progress. Verified take prefixes were retained.")));
			return;
		}
		if (m_notifications.elapsed() >= 50)
			publish();
	}
	void updateStored()
	{
		if (!m_disk)
			return;
		for (size_t i = 0; i < m_snapshot.storedFrames.size(); ++i)
			m_snapshot.storedFrames[i] = m_disk->stored[i].load(std::memory_order_acquire);
		m_snapshot.queuedFrames =
		    std::max<qint64>(0, m_callback->queue.producedFrames() - m_callback->queue.consumedFrames());
	}
	void publish()
	{
		updateStored();
		bool enqueue;
		{
			std::lock_guard lock(m_mailbox->mutex);
			m_mailbox->snapshot = m_snapshot;
			m_mailbox->epoch = m_epoch;
			m_mailbox->available = m_device && m_device->available();
			enqueue = !m_mailbox->queued;
			m_mailbox->queued = true;
		}
		if (enqueue)
			QMetaObject::invokeMethod(
			    m_owner,
			    [owner = m_owner, box = m_mailbox] {
				    AudioRecordingSnapshot snapshot;
				    quint64 epoch;
				    bool available;
				    {
					    std::lock_guard lock(box->mutex);
					    snapshot = box->snapshot;
					    epoch = box->epoch;
					    available = box->available;
					    box->queued = false;
				    }
				    if (owner->m_epoch != epoch)
					    return;
				    owner->m_snapshot = std::move(snapshot);
				    if (owner->m_meterReset && owner->m_snapshot.meterEpoch < owner->m_meterReset->load()) {
					    // Old queued telemetry must never resurrect reset meter history.
					    owner->m_snapshot.levels = {};
					    owner->m_snapshot.meters = {};
					    if (!owner->busy())
						    owner->m_snapshot.meterEpoch = owner->m_meterReset->load();
				    }
				    if (owner->m_available != available) {
					    owner->m_available = available;
					    Q_EMIT owner->devicesChanged();
				    }
				    Q_EMIT owner->changed();
			    },
			    Qt::QueuedConnection);
		m_notifications.restart();
	}
	struct Mailbox {
		std::mutex mutex;
		AudioRecordingSnapshot snapshot;
		quint64 epoch = 0;
		bool queued = false;
		bool available = false;
	};
	AudioRecording *m_owner;
	AudioDuplexDeviceFactory m_factory;
	AudioCaptureStorageFactory m_storage;
	std::unique_ptr<AudioDuplexDevice> m_device;
	std::unique_ptr<RecordingCallback> m_callback;
	std::unique_ptr<RecordingDisk> m_disk;
	std::shared_ptr<std::atomic<AudioRecordingStop>> m_stop;
	std::shared_ptr<Mailbox> m_mailbox = std::make_shared<Mailbox>();
	AudioRecordingSnapshot m_snapshot;
	AudioRecordingRequest m_request;
	QTimer *m_timer = nullptr;
	QElapsedTimer m_notifications, m_watchdog;
	quint64 m_epoch = 0;
	qint64 m_lastProcessed = 0;
	bool m_active = false, m_saving = false;
};

AudioRecording::AudioRecording(QObject *parent, AudioDuplexDeviceFactory device, AudioCaptureStorageFactory storage,
                               AudioRecordingGate permission, AudioRecordingGate beforeStart)
    : QObject(parent), m_permission(std::move(permission)), m_beforeStart(std::move(beforeStart))
{
	if (!device) {
		device = createAudioDuplexDevice;
		if (!m_permission)
			m_permission = requestAudioCapturePermission;
	}
	if (!storage)
		storage = [] { return std::make_unique<TakeStorage>(); };
	m_thread = new QThread(this);
	m_thread->setObjectName(QStringLiteral("audioDuplexRecording"));
	m_worker = new AudioRecordingWorker(this, std::move(device), std::move(storage));
	m_worker->moveToThread(m_thread);
	connect(m_thread, &QThread::started, m_worker, [worker = m_worker] { worker->initialize(); });
	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	m_thread->start();
}
AudioRecording::~AudioRecording()
{
	++m_epoch;
	if (m_stop)
		m_stop->store(AudioRecordingStop::Abort);
	QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->shutdown(); }, Qt::BlockingQueuedConnection);
	m_thread->quit();
	m_thread->wait();
}
bool AudioRecording::busy() const
{
	return m_snapshot.state == State::Permission || m_snapshot.state == State::WaitingForPlayback ||
	       m_snapshot.state == State::Preparing || m_snapshot.state == State::Recording ||
	       m_snapshot.state == State::Draining || m_snapshot.state == State::Saving;
}
bool AudioRecording::start(const AudioSession &session, const AudioRecordingRequest &request)
{
	if (busy())
		return false;
	m_snapshot = {};
	m_snapshot.error = validateAudioRecordingPlan(request.plan);
	if (m_snapshot.error.isEmpty() &&
	    (!m_available || request.directory.isEmpty() || request.device.inputId.isEmpty() ||
	     request.device.outputId.isEmpty() || request.device.sampleRate != session.sampleRate ||
	     request.plan.sampleRate != session.sampleRate ||
	     request.device.inputChannels != request.plan.pass.inputChannels || request.device.bufferFrames < 64 ||
	     request.device.bufferFrames > 16384 || request.queueFrames < 1 || request.queueFrames > 262144 ||
	     request.stallTimeoutMs < 100 || request.stallTimeoutMs > 60000))
		m_snapshot.error = recordingText(QT_TRANSLATE_NOOP(
		    "AudioRecording",
		    "Choose explicit input and output devices, matching session settings and a valid recording buffer."));
	if (!m_snapshot.error.isEmpty()) {
		m_snapshot.state = State::Error;
		Q_EMIT changed();
		return false;
	}
	const auto epoch = ++m_epoch;
	m_stop = std::make_shared<std::atomic<AudioRecordingStop>>(AudioRecordingStop::None);
	m_meterReset = std::make_shared<std::atomic<quint64>>(0);
	m_snapshot.state = State::Permission;
	Q_EMIT changed();
	const auto granted = [guard = QPointer<AudioRecording>(this), session, request, epoch](QString error) {
		if (!guard || guard->m_epoch != epoch || guard->m_snapshot.state != State::Permission)
			return;
		if (!error.isEmpty()) {
			guard->m_snapshot.state = State::Error;
			guard->m_snapshot.error = std::move(error);
			Q_EMIT guard->changed();
			return;
		}
		guard->prepare(session, request, epoch);
	};
	if (m_epoch != epoch || m_snapshot.state != State::Permission)
		return true;
	if (m_permission)
		m_permission(this, granted);
	else
		granted({});
	return true;
}
void AudioRecording::prepare(const AudioSession &session, const AudioRecordingRequest &request, quint64 epoch)
{
	m_snapshot.state = State::WaitingForPlayback;
	Q_EMIT changed();
	const auto prepared = [guard = QPointer<AudioRecording>(this), session, request, epoch](QString error) {
		if (!guard || guard->m_epoch != epoch || guard->m_snapshot.state != State::WaitingForPlayback)
			return;
		if (!error.isEmpty()) {
			guard->m_snapshot.state = State::Error;
			guard->m_snapshot.error = std::move(error);
			Q_EMIT guard->changed();
			return;
		}
		guard->m_snapshot.state = State::Preparing;
		Q_EMIT guard->changed();
		QMetaObject::invokeMethod(
		    guard->m_worker,
		    [worker = guard->m_worker, session, request, stop = guard->m_stop, reset = guard->m_meterReset, epoch] {
			    worker->start(session, request, stop, reset, epoch);
		    },
		    Qt::QueuedConnection);
	};
	if (m_epoch != epoch || m_snapshot.state != State::WaitingForPlayback)
		return;
	if (m_beforeStart)
		m_beforeStart(this, prepared);
	else
		prepared({});
}
void AudioRecording::stop()
{
	if (!busy())
		return;
	if (m_stop) {
		auto expected = AudioRecordingStop::None;
		m_stop->compare_exchange_strong(expected, AudioRecordingStop::Finish);
	}
	if (m_snapshot.state == State::Permission || m_snapshot.state == State::WaitingForPlayback) {
		++m_epoch;
		m_snapshot.state = State::Finished;
	} else
		m_snapshot.state = State::Saving;
	Q_EMIT changed();
}
void AudioRecording::refreshDevices()
{
	if (!busy())
		QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->refresh(); }, Qt::QueuedConnection);
}
void AudioRecording::resetMeters()
{
	if (!m_meterReset || m_meterReset->load() == std::numeric_limits<quint64>::max())
		return;
	const auto epoch = m_meterReset->fetch_add(1) + 1;
	m_snapshot.levels = {};
	m_snapshot.meters = {};
	if (!busy())
		m_snapshot.meterEpoch = epoch;
	Q_EMIT changed();
}
bool AudioRecording::meterResetPending() const
{
	return busy() && m_meterReset && m_snapshot.meterEpoch < m_meterReset->load();
}
} // namespace vibestudio
