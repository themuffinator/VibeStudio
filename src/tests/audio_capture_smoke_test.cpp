#include "tests/fake_audio_capture.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstring>
#include <iostream>
#include <limits>

using namespace vibestudio;
using namespace vibestudio::test;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value)
		std::cerr << text << '\n';
	return value;
}
bool wait(const std::function<bool()> &condition, int ms = 10000)
{
	QElapsedTimer clock;
	clock.start();
	while (!condition() && clock.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	return condition();
}
template <typename T> QByteArray pcm(std::initializer_list<T> values)
{
	return {reinterpret_cast<const char *>(values.begin()), qsizetype(values.size() * sizeof(T))};
}
class FaultStorage final : public AudioCaptureStorage {
  public:
	explicit FaultStorage(bool stall, bool fail) : stall(stall), fail(fail) {}
	bool open(const AudioCaptureRequest &request, QString *error) override
	{
		return writer.open(request.path, request.metadata, error);
	}
	bool append(std::span<const float> samples, QString *error) override
	{
		if (stall)
			QThread::msleep(100);
		if (fail && writes++ == 1) {
			*error = "injected disk failure";
			return false;
		}
		return writer.append(samples, error);
	}
	bool finish(QString *error) override { return writer.finish(error); }
	void close() override { writer.close(); }

  private:
	AudioTakeWriter writer;
	bool stall, fail;
	int writes = 0;
};
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("capture-worker-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	bool ok = true;
	int heartbeat = 0;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] { ++heartbeat; });
	timer.start(2);
	int number = 0;
	const auto request = [&] {
		AudioCaptureRequest value;
		value.path = QDir(temporary.path()).filePath(QString::number(++number) + ".vstake");
		value.deviceId = "fixture";
		value.metadata.name = "Fixture take";
		return value;
	};
	for (auto encoding : {AudioInputEncoding::Float32, AudioInputEncoding::Int32, AudioInputEncoding::Int16,
	                      AudioInputEncoding::UInt8}) {
		auto fake = std::make_shared<CaptureFixture>();
		fake->encoding = encoding;
		fake->maxRead = 3;
		// Two three-channel frames; save input 3 then 1. No input 2 in output.
		switch (encoding) {
		case AudioInputEncoding::Float32:
			fake->bytes = pcm<float>({.25f, .7f, -.5f, .75f, .9f, -1});
			break;
		case AudioInputEncoding::Int32:
			fake->bytes = pcm<qint32>({536870912, 3, -1073741824, 1610612736, 7, -2147483647 - 1});
			break;
		case AudioInputEncoding::Int16:
			fake->bytes = pcm<qint16>({8192, 3, -16384, 24576, 7, -32768});
			break;
		case AudioInputEncoding::UInt8:
			fake->bytes = pcm<quint8>({160, 3, 64, 224, 7, 0});
			break;
		}
		AudioCapture capture(nullptr, captureFactory(fake));
		ok &= expect(wait([&] { return capture.available(); }) && fake->openCount() == 0,
		             "enumeration never opens the input");
		auto config = request();
		config.metadata.inputChannels = 3;
		config.metadata.channelMap = {2, 0};
		ok &=
		    expect(capture.start(config) && !capture.start(config), "one explicit recording owns a capture controller");
		ok &= expect(wait([&] { return fake->consumed(); }), "all partial sample bytes consumed");
		capture.stop();
		ok &= expect(wait([&] { return !capture.busy(); }), "stop drains queued disk blocks without blocking GUI");
		const auto take = capture.snapshot().take;
		const auto audio = readAudioTakeRange(config.path, take.prefixSha256, 0, 2, {0, 1});
		ok &= expect(capture.snapshot().state == AudioCaptureSnapshot::State::Finished && take.complete &&
		                 take.frames == 2 && audio.succeeded() &&
		                 audio.audio.clip.samples == QVector<float>{-.5f, .25f, -1, .75f},
		             "all native PCM formats and short reads preserve exact selected channel samples");
	}
	{
		auto fake = std::make_shared<CaptureFixture>();
		AudioCapture capture(nullptr, captureFactory(fake));
		wait([&] { return capture.available(); });
		auto config = request();
		capture.start(config);
		capture.stop();
		ok &= expect(wait([&] { return !capture.busy(); }) && fake->openCount() == 0,
		             "stop cancels queued preparation before opening a device");
	}
	for (int failure = 0; failure < 5; ++failure) {
		auto fake = std::make_shared<CaptureFixture>();
		const bool overflow = failure == 0, disk = failure == 1;
		fake->bytes = QByteArray((overflow ? 40 : 3) * AudioTakeBlockFrames * 4, '\0');
		if (failure == 2)
			fake->bytes = pcm<float>({.25f, std::numeric_limits<float>::infinity()});
		if (failure == 3)
			fake->bytes = pcm<float>({.25f}) + QByteArray(1, '\0');
		if (failure == 4)
			fake->error = "injected input disconnect";
		AudioCapture capture(nullptr, captureFactory(fake),
		                     [=] { return std::make_unique<FaultStorage>(overflow, disk); });
		wait([&] { return capture.available(); });
		auto config = request();
		capture.start(config);
		if (failure == 3) {
			wait([&] { return fake->consumed(); });
			capture.stop();
		}
		ok &= expect(wait([&] { return !capture.busy(); }) &&
		                 capture.snapshot().state == AudioCaptureSnapshot::State::Error &&
		                 !capture.snapshot().error.isEmpty(),
		             "queue, disk, invalid sample, partial frame and disconnect failures stop explicitly");
		const auto take = inspectAudioTake(config.path);
		ok &= expect(!take.complete && take.frames == capture.snapshot().storedFrames,
		             "failed capture retains exactly the committed prefix with no clean footer");
		if (overflow)
			ok &= expect(take.frames > 0 && take.frames <= 17 * AudioTakeBlockFrames &&
			                 capture.snapshot().receivedFrames > take.frames,
			             "bounded queue failure reports loss without inventing samples");
		if (disk)
			ok &= expect(take.frames == AudioTakeBlockFrames, "injected disk fault retains the prior durable block");
		if (failure == 2 || failure == 3)
			ok &= expect(take.frames == 1, "invalid tail preserves preceding complete frame");
	}
	{
		auto fake = std::make_shared<CaptureFixture>();
		AudioCapture capture(nullptr, captureFactory(fake));
		wait([&] { return capture.available(); });
		capture.start(request());
		const auto before = heartbeat;
		ok &= expect(wait([&] { return !capture.busy(); }, 8000) &&
		                 capture.snapshot().state == AudioCaptureSnapshot::State::Error && heartbeat > before + 200,
		             "no-data input timeout leaves the main event loop responsive");
	}
	for (int attempt = 0; attempt < 48; ++attempt) {
		auto fake = std::make_shared<CaptureFixture>();
		QVector<float> samples(4096 + attempt % 2, .25f);
		fake->bytes = QByteArray(reinterpret_cast<const char *>(samples.constData()), samples.size() * 4);
		auto capture = std::make_unique<AudioCapture>(nullptr, captureFactory(fake));
		ok &= expect(wait([&] { return capture->available(); }), "destruction fixture input becomes available");
		auto config = request();
		ok &= expect(capture->start(config) && wait([&] { return fake->consumed(); }),
		             "destruction fixture delivers full blocks and optional partial tails");
		if (attempt % 2 == 0)
			ok &= expect(wait([&] { return capture->snapshot().storedFrames == samples.size(); }),
			             "destruction also covers an empty disk queue after durable storage");
		capture.reset();
		const auto take = inspectAudioTake(config.path);
		const auto audio = readAudioTakeRange(config.path, take.prefixSha256, 0, samples.size(), {0}, true);
		const bool retained = fake->closeCount() == 1 && take.recoverable() && !take.complete &&
		                      take.frames == samples.size() && audio.succeeded() && audio.audio.clip.samples == samples;
		ok &= expect(retained, "controller destruction closes input and joins storage with a recoverable prefix");
		if (!retained)
			std::cerr << "destruction attempt=" << attempt << " frames=" << take.frames << " complete=" << take.complete
			          << " recoverable=" << take.recoverable() << " error=" << take.error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
