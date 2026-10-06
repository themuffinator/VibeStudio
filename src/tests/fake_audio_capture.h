#pragma once
#include "app/audio_capture.h"
#include <algorithm>
#include <cstring>
#include <mutex>

namespace vibestudio::test
{
struct CaptureFixture {
	mutable std::mutex mutex;
	QByteArray bytes;
	AudioInputEncoding encoding = AudioInputEncoding::Float32;
	qint64 offset = 0, maxRead = 16384;
	int opens = 0, closes = 0;
	bool failOpen = false, failRead = false;
	QString error;
	AudioCaptureRequest opened;
	bool consumed() const
	{
		std::lock_guard lock(mutex);
		return offset == bytes.size();
	}
	int openCount() const
	{
		std::lock_guard lock(mutex);
		return opens;
	}
	int closeCount() const
	{
		std::lock_guard lock(mutex);
		return closes;
	}
};
class FakeCaptureDevice final : public AudioCaptureDevice {
  public:
	explicit FakeCaptureDevice(std::shared_ptr<CaptureFixture> state) : s(std::move(state)) {}
	bool available() const override { return true; }
	QVector<AudioInputDevice> inputs() override { return {{"fixture", "Simulated input", 1, 32}}; }
	bool open(const AudioCaptureRequest &request, AudioInputEncoding *encoding, QString *error) override
	{
		std::lock_guard lock(s->mutex);
		++s->opens;
		s->opened = request;
		*encoding = s->encoding;
		if (s->failOpen || request.deviceId != "fixture") {
			*error = "fixture unavailable";
			return false;
		}
		active = true;
		return true;
	}
	qint64 read(char *bytes, qint64 count) override
	{
		std::lock_guard lock(s->mutex);
		if (!active || s->failRead)
			return -1;
		const auto size = std::min({count, s->maxRead, s->bytes.size() - s->offset});
		std::memcpy(bytes, s->bytes.constData() + s->offset, size_t(size));
		s->offset += size;
		return size;
	}
	AudioInputStatus status() const override
	{
		std::lock_guard lock(s->mutex);
		return {s->bytes.size() - s->offset, 2048, s->error};
	}
	void close() override
	{
		std::lock_guard lock(s->mutex);
		if (active)
			++s->closes;
		active = false;
	}

  private:
	std::shared_ptr<CaptureFixture> s;
	bool active = false;
};
inline AudioCaptureDeviceFactory captureFactory(const std::shared_ptr<CaptureFixture> &state)
{
	return [state] { return std::make_unique<FakeCaptureDevice>(state); };
}
} // namespace vibestudio::test
