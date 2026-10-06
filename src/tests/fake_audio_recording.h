#pragma once
#include "app/audio_recording.h"
#include <QThread>
#include <array>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>

// Shared deterministic duplex device and durable take-writer fault injection.
// No native device enumeration or physical input/output is used.
namespace vibestudio::test
{
inline thread_local bool recordingCountAllocations = false;
inline thread_local size_t recordingAllocations = 0;
using RecordingDeviceState = AudioDuplexDeviceStatus::State;
struct RecordingFixture {
	std::atomic<RecordingDeviceState> state{RecordingDeviceState::Closed};
	std::atomic<int> opens{0}, starts{0}, closes{0}, calls{0};
	std::atomic<bool> validThreads{true}, allJournalsReady{true}, draining{false};
	std::atomic<int> journals{0};
	std::atomic<size_t> callbackAllocations{0};
	std::atomic<bool> releaseDrain{false};
	std::atomic<bool> pausedCallbacks{false}, callbacksPaused{false};
	bool stall = false, priming = false, xrun = false, failOpen = false, failStart = false, holdDrain = false;
	bool failClose = false;
	bool fast = false;
	int packets = 1000000;
};
class RecordingDevice final : public AudioDuplexDevice {
  public:
	explicit RecordingDevice(std::shared_ptr<RecordingFixture> fixture)
	    : f(std::move(fixture)), owner(std::this_thread::get_id())
	{
	}
	~RecordingDevice() override
	{
		close();
		check();
	}
	bool available() const override
	{
		check();
		return !(f->failClose && f->closes > 0);
	}
	QVector<AudioDuplexEndpoint> enumerate(QString *) override
	{
		check();
		return {{"input", "RecordingFixture input", "RecordingFixture", 4, 0, 48000},
		        {"output", "RecordingFixture output", "RecordingFixture", 0, 2, 48000}};
	}
	bool open(const AudioDuplexDeviceRequest &, AudioDuplexDeviceCallback &callback, QString *error) override
	{
		check();
		++f->opens;
		if (f->journals.load() != 2)
			f->allJournalsReady = false;
		if (f->failOpen) {
			*error = "Injected open failure";
			return false;
		}
		target = &callback;
		f->state = RecordingDeviceState::Open;
		return true;
	}
	bool start(QString *error) override
	{
		check();
		++f->starts;
		if (f->failStart) {
			*error = "Injected start failure";
			return false;
		}
		stop = false;
		f->state = RecordingDeviceState::Running;
		thread = std::thread([this] {
			qint64 frame = 0;
			std::array<float, 17 * 4> input;
			std::array<float, 17 * 2> output;
			while (!stop.load()) {
				if (f->stall || f->pausedCallbacks.load() || f->calls.load() >= f->packets) {
					f->callbacksPaused = true;
					QThread::msleep(1);
					continue;
				}
				f->callbacksPaused = false;
				for (size_t i = 0; i < input.size(); ++i)
					input[i] = float((frame + qint64(i / 4)) * 10 + qint64(i % 4));
				AudioDuplexTime time{10.0 + double(frame) / 48000, 10.0 + double(frame + 29) / 48000};
				time.priming = f->priming;
				time.inputOverflow = f->xrun && frame >= 85;
				recordingAllocations = 0;
				recordingCountAllocations = true;
				const auto result = target->process(input, output, time);
				recordingCountAllocations = false;
				f->callbackAllocations.fetch_add(recordingAllocations);
				++f->calls;
				frame += 17;
				if (result == AudioDuplexCallbackResult::Complete) {
					f->state = RecordingDeviceState::Draining;
					f->draining = true;
					while (f->holdDrain && !f->releaseDrain.load() && !stop.load())
						QThread::msleep(1);
					f->state = stop.load() ? RecordingDeviceState::Stopped : RecordingDeviceState::Complete;
					return;
				}
				if (result == AudioDuplexCallbackResult::Abort) {
					f->state = RecordingDeviceState::Stopped;
					return;
				}
				if (!f->fast)
					QThread::msleep(2);
			}
			f->state = RecordingDeviceState::Stopped;
		});
		return true;
	}
	void requestStop() noexcept override { stop = true; }
	AudioDuplexDeviceStatus status() override
	{
		check();
		const auto state = f->state.load();
		return {state, quint64(f->calls.load()),
		        state == RecordingDeviceState::Error ? QStringLiteral("Injected close failure") : QString{}};
	}
	AudioDuplexDeviceInfo info() const override
	{
		check();
		return {"RecordingFixture input", "RecordingFixture output", "RecordingFixture", 48000, 4, .001, .001, true};
	}
	void close() override
	{
		check();
		stop = true;
		if (thread.joinable())
			thread.join();
		if (target)
			++f->closes;
		target = nullptr;
		f->state = f->failClose && f->closes > 0 ? RecordingDeviceState::Error : RecordingDeviceState::Closed;
	}

  private:
	void check() const
	{
		if (owner != std::this_thread::get_id())
			f->validThreads = false;
	}
	std::shared_ptr<RecordingFixture> f;
	std::thread::id owner;
	AudioDuplexDeviceCallback *target = nullptr;
	std::thread thread;
	std::atomic<bool> stop{false};
};
struct RecordingDiskFixture {
	int failOpenArm = -1, failWriteArm = -1, failFinishArm = -1, slowAppendMs = 0;
	bool throwOpen = false;
	std::atomic<bool> holdOpen{false}, opening{false};
	std::atomic<int> constructed{0}, writes{0};
};
class RecordingStorage final : public AudioCaptureStorage {
  public:
	RecordingStorage(std::shared_ptr<RecordingFixture> f, std::shared_ptr<RecordingDiskFixture> disk)
	    : f(std::move(f)), disk(std::move(disk)), owner(std::this_thread::get_id()), arm(this->disk->constructed++)
	{
	}
	bool open(const AudioCaptureRequest &request, QString *error) override
	{
		check();
		disk->opening = true;
		while (disk->holdOpen.load())
			QThread::msleep(1);
		if (disk->throwOpen)
			throw std::runtime_error("Injected exception");
		if (arm == disk->failOpenArm) {
			*error = "Injected journal open failure";
			return false;
		}
		const bool opened = writer.open(request.path, request.metadata, error);
		if (opened)
			++f->journals;
		return opened;
	}
	bool append(std::span<const float> samples, QString *error) override
	{
		check();
		++disk->writes;
		if (disk->slowAppendMs)
			QThread::msleep(disk->slowAppendMs);
		if (arm == disk->failWriteArm) {
			*error = "Injected append failure";
			return false;
		}
		return writer.append(samples, error);
	}
	bool finish(QString *error) override
	{
		check();
		if (arm == disk->failFinishArm) {
			*error = "Injected footer failure";
			return false;
		}
		return writer.finish(error);
	}
	void close() override
	{
		check();
		writer.close();
	}

  private:
	void check()
	{
		if (owner != std::this_thread::get_id())
			f->validThreads = false;
	}
	std::shared_ptr<RecordingFixture> f;
	std::shared_ptr<RecordingDiskFixture> disk;
	std::thread::id owner;
	int arm;
	AudioTakeWriter writer;
};

inline AudioDuplexDeviceFactory recordingDeviceFactory(std::shared_ptr<RecordingFixture> fixture)
{
	return [fixture] { return std::make_unique<RecordingDevice>(fixture); };
}
inline AudioCaptureStorageFactory recordingStorageFactory(std::shared_ptr<RecordingFixture> fixture,
                                                          std::shared_ptr<RecordingDiskFixture> disk)
{
	return [fixture, disk] { return std::make_unique<RecordingStorage>(fixture, disk); };
}
} // namespace vibestudio::test
