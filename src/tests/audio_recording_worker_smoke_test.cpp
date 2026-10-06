#include "tests/fake_audio_recording.h"
#include "tests/fake_audio_stream.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>

using namespace vibestudio::test;
void *operator new(std::size_t size)
{
	if (recordingCountAllocations)
		++recordingAllocations;
	if (auto *memory = std::malloc(std::max(size, size_t(1))))
		return memory;
	throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept { ::operator delete(memory); }
using namespace vibestudio;
namespace
{
using State = AudioRecordingSnapshot::State;
using DeviceState = AudioDuplexDeviceStatus::State;
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool wait(const std::function<bool()> &condition, int milliseconds = 10000)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < milliseconds) {
		QCoreApplication::processEvents();
		QThread::msleep(2);
	}
	QCoreApplication::processEvents();
	return condition();
}
AudioSession session()
{
	AudioSession s;
	s.sampleRate = 48000;
	s.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "source";
	source.audio.clip = {2, 48000, QVector<float>(1000, .125f)};
	source.audio.endFrame = 500;
	s.sources = {source};
	for (int i = 0; i < 2; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		if (!i)
			track.regions = {{"backing", "Backing", "source", 0, 0, 500}};
		s.tracks.append(track);
	}
	return s;
}
AudioDuplexDeviceFactory factory(const std::shared_ptr<RecordingFixture> &f)
{
	return [f] { return std::make_unique<RecordingDevice>(f); };
}
AudioCaptureStorageFactory storage(const std::shared_ptr<RecordingFixture> &f,
                                   const std::shared_ptr<RecordingDiskFixture> &d)
{
	return [f, d] { return std::make_unique<RecordingStorage>(f, d); };
}
} // namespace

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath("recording-worker-XXXXXX"));
	if (!temp.isValid())
		return EXIT_FAILURE;
	bool ok = true;
	int serial = 0, heartbeat = 0;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&] { ++heartbeat; });
	timer.start(2);
	const auto s = session();
	const auto request = [&] {
		AudioRecordingRequest r;
		r.directory = QDir(temp.path()).filePath(QString::number(++serial) + ".vsrecord");
		r.plan.name = "Worker take";
		r.plan.inputDeviceName = "RecordingFixture input";
		r.plan.pass = {5, 37, 94, 3, 4, 17, 0, .125, {{"0", 2, {3, 1}}, {"1", 1, {2, 0}}}};
		r.device = {"input", "output", 48000, 4, 256};
		r.stallTimeoutMs = 150;
		return r;
	};
	{
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		f->holdDrain = true;
		AudioRecording recording(nullptr, factory(f), storage(f, d));
		ok &= expect(wait([&] { return recording.available(); }) && f->opens == 0, "enumeration does not open input");
		auto r = request();
		ok &= expect(recording.start(s, r) && !recording.start(s, r), "one recording pass at a time");
		ok &= expect(wait([&] { return recording.snapshot().state == State::Draining; }) && recording.busy(),
		             "callback completion waits for device output drain");
		f->releaseDrain = true;
		ok &= expect(wait([&] { return !recording.busy(); }) && recording.snapshot().state == State::Finished,
		             "completed pass finalizes asynchronously");
		const auto info = inspectAudioRecording(r.directory);
		ok &= expect(info.receiptMatches && info.receipt.outcome == AudioRecordingReceipt::Outcome::Complete &&
		                 info.takes.size() == 2 && info.takes[0].frames == 57 && info.takes[1].frames == 57,
		             "both punched arms finish with verified receipt");
		if (info.takes.size() == 2) {
			const auto stereo = readAudioTakeRange(info.takes[0].path, info.takes[0].prefixSha256, 0, 57, {0, 1});
			const auto mono = readAudioTakeRange(info.takes[1].path, info.takes[1].prefixSha256, 0, 57, {0});
			bool exact = stereo.succeeded() && mono.succeeded();
			for (int i = 0; exact && i < 57; ++i)
				exact = stereo.audio.clip.samples[i * 2] == float((64 + i) * 10 + 3) &&
				        stereo.audio.clip.samples[i * 2 + 1] == float((64 + i) * 10 + 1) &&
				        mono.audio.clip.samples[i] == float((64 + i) * 10 + 2);
			ok &=
			    expect(exact && info.takes[0].metadata.position == 37 && info.takes[0].metadata.latencyFrames == 0,
			           "dry samples preserve channel order, punch and single timing correction without audition gain");
		}
		ok &= expect(f->allJournalsReady && f->validThreads && f->callbackAllocations == 0 && f->closes > 0,
		             "journals precede input, callback allocates nothing, devices and files keep owner threads");
	}
	for (int failure = 0; failure < 11; ++failure) {
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		auto r = request();
		switch (failure) {
		case 0:
			d->failOpenArm = 1;
			break;
		case 1:
			d->failWriteArm = 1;
			break;
		case 2:
			d->failFinishArm = 1;
			break;
		case 3:
			f->stall = true;
			break;
		case 4:
			f->priming = true;
			break;
		case 5:
			f->xrun = true;
			break;
		case 6:
			f->failOpen = true;
			break;
		case 7:
			f->failStart = true;
			break;
		case 8:
			f->fast = true;
			r.queueFrames = 17;
			d->slowAppendMs = 75;
			break;
		case 9:
			d->throwOpen = true;
			break;
		case 10:
			f->failClose = true;
			break;
		}
		AudioRecording recording(nullptr, factory(f), storage(f, d));
		wait([&] { return recording.available(); });
		const int before = heartbeat;
		recording.start(s, r);
		ok &= expect(wait([&] { return !recording.busy(); }) && recording.snapshot().state == State::Error &&
		                 !recording.snapshot().error.isEmpty(),
		             "device, disk, priming/stall, queue and exception failures are explicit");
		const auto info = inspectAudioRecording(r.directory);
		ok &= expect(info.receiptMatches && info.receipt.outcome == AudioRecordingReceipt::Outcome::Interrupted &&
		                 heartbeat > before,
		             "error receipt remains inspectable while UI keeps running");
		if (failure == 0 || failure == 9)
			ok &= expect(f->opens == 0, "journal preparation failure never opens input");
		if (failure == 1)
			ok &=
			    expect(info.takes.size() == 2 && info.takes[0].frames > info.takes[1].frames && !info.takes[0].complete,
			           "disk failure retains actual unequal per-arm prefixes");
		if (failure == 2)
			ok &= expect(info.takes.size() == 2 && info.takes[0].complete && !info.takes[1].complete,
			             "partial footer failure does not claim grouped completion");
		if (failure == 4)
			ok &= expect(info.receipt.progress.primingFrames > 0 && info.receipt.progress.capturedFrames == 0,
			             "endless priming cannot evade progress watchdog");
		if (failure == 8)
			ok &= expect(info.receipt.progress.fault == AudioDuplexFault::CaptureQueueFull,
			             "bounded queue overflow stops without silently dropping captured audio");
		if (failure == 10)
			ok &= expect(!recording.available() && info.takes.size() == 2 && !info.takes[0].complete &&
			                 info.receipt.error.contains("Injected close failure"),
			             "failed driver close disables recording and prevents a clean completion receipt");
	}
	{
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		f->fast = true;
		AudioRecording recording(nullptr, factory(f), storage(f, d));
		wait([&] { return recording.available(); });
		recording.start(s, request());
		ok &=
		    expect(wait([&] { return !recording.busy(); }) && recording.snapshot().state == State::Finished &&
		               recording.snapshot().progress.capturedFrames == 57 && recording.snapshot().result.receiptMatches,
		           "final completion remains exact when callback burst fills the telemetry queue");
	}
	{
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		AudioRecording recording(nullptr, factory(f), storage(f, d),
		                         [](QObject *, auto complete) { complete("Permission denied fixture"); });
		wait([&] { return recording.available(); });
		const auto r = request();
		recording.start(s, r);
		ok &= expect(recording.snapshot().state == State::Error && f->opens == 0 && !QFileInfo::exists(r.directory),
		             "permission denial never opens a device or creates take files");
	}
	{
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		d->holdOpen = true;
		AudioRecording recording(nullptr, factory(f), storage(f, d));
		wait([&] { return recording.available(); });
		const auto r = request();
		recording.start(s, r);
		wait([&] { return d->opening.load(); });
		QFile occupied(QDir(r.directory).filePath("result.json"));
		ok &= expect(occupied.open(QIODevice::WriteOnly | QIODevice::NewOnly) && occupied.write("retained") == 8,
		             "create conflicting receipt fixture");
		occupied.close();
		d->holdOpen = false;
		ok &= expect(wait([&] { return !recording.busy(); }) && recording.snapshot().state == State::Error &&
		                 recording.snapshot().result.planValid && !recording.snapshot().result.receiptValid &&
		                 recording.snapshot().result.takes.size() == 2 &&
		                 recording.snapshot().result.takes[0].frames == 57 && occupied.open(QIODevice::ReadOnly) &&
		                 occupied.readAll() == "retained",
		             "receipt creation failure preserves existing file and independently verified full takes");
	}
	for (int cancel = 0; cancel < 3; ++cancel) {
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		std::function<void(QString)> completion;
		const AudioRecordingGate gate = [&](QObject *, auto done) { completion = std::move(done); };
		auto recording = std::make_unique<AudioRecording>(nullptr, factory(f), storage(f, d),
		                                                  cancel == 0 ? gate : AudioRecordingGate{},
		                                                  cancel != 0 ? gate : AudioRecordingGate{});
		wait([&] { return recording->available(); });
		const auto r = request();
		recording->start(s, r);
		ok &= expect(bool(completion) && f->opens == 0, "permission and playback acknowledgement gate device opening");
		if (cancel == 2)
			recording.reset();
		else
			recording->stop();
		completion({});
		completion({});
		QCoreApplication::processEvents();
		ok &= expect(f->opens == 0 && !QFileInfo::exists(r.directory) && (!recording || !recording->busy()),
		             "late duplicate gate completion after Stop/destruction cannot start a recording");
	}
	{
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		d->holdOpen = true;
		AudioRecording recording(nullptr, factory(f), storage(f, d));
		wait([&] { return recording.available(); });
		const auto r = request();
		recording.start(s, r);
		wait([&] { return d->opening.load(); });
		recording.stop();
		d->holdOpen = false;
		ok &= expect(wait([&] { return !recording.busy(); }) && f->opens == 0 &&
		                 recording.snapshot().state == State::Finished &&
		                 inspectAudioRecording(r.directory).receiptMatches,
		             "Stop during journal preparation finalizes empty takes without opening input");
	}
	for (bool destroy : {false, true}) {
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		f->packets = 5;
		auto recording = std::make_unique<AudioRecording>(nullptr, factory(f), storage(f, d));
		wait([&] { return recording->available(); });
		const auto r = request();
		recording->start(s, r);
		wait([&] { return f->calls >= 5; });
		if (destroy)
			recording.reset();
		else {
			recording->stop();
			wait([&] { return !recording->busy(); });
		}
		const auto info = inspectAudioRecording(r.directory);
		ok &= expect(info.receiptMatches && info.takes.size() == 2 && info.takes[0].frames == 21 &&
		                 info.takes[1].frames == 21 && info.takes[0].complete == !destroy,
		             "Stop drains a clean prefix; destruction retains interrupted prefix");
		ok &= expect(f->closes > 0 && f->validThreads, "destruction joins the device and disk on correct threads");
	}
	{
		auto playbackState = std::make_shared<FakeAudioStreamState>();
		AudioSessionPlayback playback(nullptr, fakeAudioStreamFactory(playbackState));
		wait([&] { return playback.available(); });
		playback.start(s, {0, 500, false}, {"fixture", 48000, 256});
		wait([&] { return playback.snapshot().state == AudioSessionPlaybackSnapshot::State::Playing; });
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		bool acknowledged = false;
		AudioRecording recording(nullptr, factory(f), storage(f, d), {}, [&](QObject *context, auto complete) {
			playback.stopAndWait(context, [&, complete](bool stopped) {
				acknowledged =
				    playbackState->locked([](auto &v) { return v.state == AudioStreamDeviceStatus::State::Closed; });
				complete(stopped && acknowledged ? QString{} : QStringLiteral("Playback changed"));
			});
		});
		wait([&] { return recording.available(); });
		recording.start(s, request());
		ok &= expect(wait([&] { return !recording.busy(); }) && acknowledged &&
		                 recording.snapshot().state == State::Finished,
		             "input opens after acknowledged playback worker shutdown");
		bool delivered = false, accepted = true;
		playback.stopAndWait(&recording, [&](bool value) {
			delivered = true;
			accepted = value;
		});
		playback.start(s, {0, 500, false}, {"fixture", 48000, 256});
		ok &= expect(wait([&] { return delivered; }) && !accepted,
		             "new playback generation invalidates an older shutdown acknowledgement");
	}
	{
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		AudioRecording recording(nullptr, factory(f), storage(f, d));
		wait([&] { return recording.available(); });
		auto r = request();
		r.plan.pass.punchEnd = 100000;
		r.stallTimeoutMs = 5000;
		recording.start(s, r);
		ok &= expect(wait([&] { return recording.snapshot().levels.input[0].frames > 0; }),
		             "recording telemetry publishes actual raw input levels");
		f->pausedCallbacks = true;
		ok &=
		    expect(wait([&] { return f->callbacksPaused.load(); }), "fixture pauses outside the callback before reset");
		const auto oldFrames = recording.snapshot().progress.capturedFrames;
		recording.resetMeters();
		ok &= expect(recording.meterResetPending() && recording.snapshot().levels.count == 0,
		             "reset clears visible levels immediately while callback acknowledgement is pending");
		bool elapsed = false;
		QTimer::singleShot(100, &recording, [&] { elapsed = true; });
		wait([&] { return elapsed; });
		ok &= expect(recording.meterResetPending() && recording.snapshot().levels.count == 0,
		             "older queued telemetry cannot resurrect reset meter history");
		f->pausedCallbacks = false;
		ok &= expect(
		    wait([&] { return !recording.meterResetPending() && recording.snapshot().levels.input[0].frames > 0; }) &&
		        recording.snapshot().levels.input[0].frames < quint64(recording.snapshot().progress.processedFrames) &&
		        recording.snapshot().progress.capturedFrames >= oldFrames,
		    "callback acknowledges reset without resetting capture timing or sample count");
		recording.stop();
		ok &= expect(wait([&] { return !recording.busy(); }) && recording.snapshot().result.receiptMatches &&
		                 f->callbackAllocations == 0,
		             "metering and reset preserve durable capture without callback allocations");
		if (recording.snapshot().result.takes.isEmpty())
			return EXIT_FAILURE;
		const auto stored = recording.snapshot().result.takes[0].frames;
		recording.resetMeters();
		QCoreApplication::processEvents();
		ok &= expect(!recording.meterResetPending() && recording.snapshot().levels.count == 0 &&
		                 recording.snapshot().result.takes[0].frames == stored,
		             "finished meter reset changes no receipt, take data or worker lifetime");
	}
	for (bool partialLoop : {false, true}) {
		auto f = std::make_shared<RecordingFixture>();
		auto d = std::make_shared<RecordingDiskFixture>();
		AudioRecording recording(nullptr, factory(f), storage(f, d));
		wait([&] { return recording.available(); });
		auto r = request();
		r.plan.pass.loopPasses = 3;
		if (partialLoop)
			f->packets = 8;
		recording.start(s, r);
		if (partialLoop) {
			ok &= wait([&] { return f->calls >= 8; });
			recording.stop();
		}
		const auto frames = partialLoop ? 72 : 3 * (r.plan.pass.punchEnd - r.plan.pass.punchFirst);
		ok &= expect(wait([&] { return !recording.busy(); }) && recording.snapshot().result.receiptMatches &&
		                 recording.snapshot().progress.capturedFrames == frames && f->opens == 1 && f->starts == 1 &&
		                 f->callbackAllocations == 0,
		             "loop recording keeps one native stream and allocation-free callbacks across all passes");
		const auto info = inspectAudioRecording(r.directory);
		ok &= expect(info.planValid && info.receiptMatches && info.plan.pass.loopPasses == 3,
		             "disk worker finalizes and independently verifies a complete multi-pass journal");
		if (info.takes.size() == 2) {
			const auto length = r.plan.pass.punchEnd - r.plan.pass.punchFirst;
			const auto later =
			    readAudioTakeRange(info.takes[0].path, info.takes[0].prefixSha256, length, frames, {0, 1});
			ok &= expect(later.succeeded() && later.audio.clip.samples.first() == 1213 &&
			                 later.audio.clip.samples.size() == (frames - length) * 2,
			             "completed and stopped loops retain the exact beginning and available length of later passes");
		}
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
