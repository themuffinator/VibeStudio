#pragma once
#include "app/audio_session_playback.h"
#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <limits>

namespace vibestudio
{
// All state is mutex-protected because the real controller uses its worker.
// Fixture clients change/inspect it only inside locked(). No device access.
struct FakeAudioStreamState {
	QMutex mutex;
	QVector<AudioOutputDevice> devices{{"fixture", "Fixture output", true}};
	AudioStreamConfiguration configuration;
	QByteArray bytes;
	AudioStreamDeviceStatus::State state = AudioStreamDeviceStatus::State::Closed;
	qint64 processed = 0, capacity = 16384, maxWrite = std::numeric_limits<qint64>::max();
	int opens = 0, resets = 0, suspends = 0, resumes = 0;
	bool rejectOpen = false, rejectWrite = false, stall = false;
	template <typename F> auto locked(F operation)
	{
		QMutexLocker guard(&mutex);
		return operation(*this);
	}
};
class FakeAudioStreamDevice final : public AudioStreamDevice {
  public:
	explicit FakeAudioStreamDevice(std::shared_ptr<FakeAudioStreamState> value) : m_data(std::move(value)) {}
	bool available() const override { return true; }
	QVector<AudioOutputDevice> outputs() override
	{
		return m_data->locked([](auto &v) { return v.devices; });
	}
	bool open(const AudioStreamConfiguration &config, QString *error) override
	{
		return m_data->locked([&](auto &v) {
			++v.opens;
			v.configuration = config;
			v.bytes.clear();
			v.processed = 0;
			if (v.rejectOpen) {
				*error = "Fixture open failure";
				return false;
			}
			v.state = AudioStreamDeviceStatus::State::Idle;
			return true;
		});
	}
	void reset() override
	{
		m_data->locked([](auto &v) {
			++v.resets;
			v.state = AudioStreamDeviceStatus::State::Closed;
		});
	}
	void suspend() override
	{
		m_data->locked([](auto &v) {
			++v.suspends;
			v.state = AudioStreamDeviceStatus::State::Paused;
		});
	}
	void resume() override
	{
		m_data->locked([](auto &v) {
			++v.resumes;
			v.state = AudioStreamDeviceStatus::State::Active;
		});
	}
	qint64 write(const char *bytes, qint64 count) override
	{
		return m_data->locked([&](auto &v) -> qint64 {
			if (v.rejectWrite)
				return -1;
			if (v.stall)
				return 0;
			const qint64 accepted =
			    std::max<qint64>(0, std::min({count, v.maxWrite, v.capacity - (v.bytes.size() - v.processed * 8)}));
			v.bytes.append(bytes, accepted);
			if (accepted)
				v.state = AudioStreamDeviceStatus::State::Active;
			return accepted;
		});
	}
	AudioStreamDeviceStatus status() const override
	{
		return m_data->locked([](auto &v) {
			AudioStreamDeviceStatus result;
			result.state = v.state;
			result.processedFrames = v.processed;
			result.writableBytes = std::max<qint64>(0, v.capacity - (v.bytes.size() - v.processed * 8));
			result.bufferFrames = int(v.capacity / 8);
			if (v.state == AudioStreamDeviceStatus::State::Error)
				result.error = "Fixture disconnected";
			return result;
		});
	}

  private:
	std::shared_ptr<FakeAudioStreamState> m_data;
};
inline AudioStreamDeviceFactory fakeAudioStreamFactory(std::shared_ptr<FakeAudioStreamState> state = {})
{
	if (!state)
		state = std::make_shared<FakeAudioStreamState>();
	return [state] { return std::make_unique<FakeAudioStreamDevice>(state); };
}
} // namespace vibestudio
