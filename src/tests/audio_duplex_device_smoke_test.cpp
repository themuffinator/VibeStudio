#include "app/audio_portaudio_api.h"
#include "core/audio_duplex_queue.h"
#include "core/audio_take.h"
#include "vibestudio_config.h"
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <thread>
#include <vibestudio_wasapi_packet.h>
#ifdef Q_OS_WIN
#include <pa_win_wasapi.h>
#endif
#if VIBESTUDIO_HAVE_AUDIO_DUPLEX
extern "C" {
#include <pa_process.h>
}
#endif

namespace
{
thread_local bool countAllocations = false;
thread_local size_t allocations = 0;
} // namespace
void *operator new(std::size_t size)
{
	if (countAllocations)
		++allocations;
	if (void *p = std::malloc(std::max(size, size_t(1))))
		return p;
	throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *p) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete(p); }

using namespace vibestudio;
namespace
{
using State = AudioDuplexDeviceStatus::State;
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}
bool silent(std::span<const float> values)
{
	return std::all_of(values.begin(), values.end(), [](float v) { return v == 0; });
}
struct Host {
	PaHostApiInfo host{1, paWASAPI, "Synthetic host", 2, 0, 1};
	std::array<PaDeviceInfo, 2> devices{{{2, "Synthetic duplex A", 0, 4, 2, .005, .006, .05, .06, 48000},
	                                     {2, "Synthetic duplex B", 0, 2, 2, .005, .006, .05, .06, 48000}}};
	PaStreamInfo streamInfo{1, .01, .02, 48000};
	PaStreamParameters in{}, out{};
	PaStreamCallback *callback = nullptr;
	void *context = nullptr;
	unsigned long flags = 0, frames = 99, stopped = 0;
	int initialized = 0, terminated = 0, opened = 0, started = 0, aborted = 0, closed = 0, active = 0;
	PaError initializeResult = paNoError, openResult = paNoError, startResult = paNoError;
	PaError closeResult = paNoError, abortResult = paNoError;
	bool onStart = false, onAbort = false, abortSilent = false, wasapiCorrect = true;
	std::shared_ptr<AudioPortAudioApi> api = std::make_shared<AudioPortAudioApi>();
	Host()
	{
		api->initialize = [&] {
			++initialized;
			return initializeResult;
		};
		api->terminate = [&] {
			++terminated;
			return paNoError;
		};
		api->deviceCount = [] { return 2; };
		api->deviceInfo = [&](int i) { return i >= 0 && i < 2 ? &devices[size_t(i)] : nullptr; };
		api->hostInfo = [&](int i) { return i == 0 ? &host : nullptr; };
		api->open = [&](PaStream **stream, const PaStreamParameters *input, const PaStreamParameters *output,
		                double rate, unsigned long block, PaStreamFlags streamFlags, PaStreamCallback *cb, void *data) {
			++opened;
			in = *input;
			out = *output;
			flags = streamFlags;
			frames = block;
			callback = cb;
			context = data;
#ifdef Q_OS_WIN
			if (host.type == paWASAPI) {
				const auto *info = static_cast<const PaWasapiStreamInfo *>(input->hostApiSpecificStreamInfo);
				wasapiCorrect = info && info->flags == (paWinWasapiPolling | paWinWasapiExplicitSampleFormat) &&
				                input->hostApiSpecificStreamInfo == output->hostApiSpecificStreamInfo;
			}
#endif
			if (rate != 48000)
				return PaError(paInvalidSampleRate);
			if (openResult == paNoError)
				*stream = reinterpret_cast<PaStream *>(this);
			return openResult;
		};
		api->streamInfo = [&](PaStream *) { return &streamInfo; };
		api->start = [&](PaStream *) {
			++started;
			active = 1;
			if (onStart)
				invoke(0, 8);
			return startResult;
		};
		api->abort = [&](PaStream *) {
			++aborted;
			if (onAbort) {
				std::array<float, 16> output;
				output.fill(9);
				std::array<float, 32> input{};
				PaStreamCallbackTimeInfo time{10, 10, 10.001};
				abortSilent = callback(input.data(), output.data(), 8, &time, 0, context) == paAbort && silent(output);
			}
			active = 0;
			return abortResult;
		};
		api->close = [&](PaStream *) {
			++closed;
			if (closeResult == paNoError)
				callback = nullptr;
			return closeResult;
		};
		api->active = [&](PaStream *) { return active; };
		api->stoppedFlags = [&](PaStream *) { return stopped; };
		api->errorText = [](PaError) { return "Synthetic failure"; };
	}
	int invoke(int first, int count, unsigned long callbackFlags = 0)
	{
		std::array<float, 1024> input{}, output{};
		for (int f = 0; f < count; ++f)
			for (int c = 0; c < in.channelCount; ++c)
				input[size_t(f * in.channelCount + c)] = float((first + f) * 4 + c);
		PaStreamCallbackTimeInfo time{10 + double(first) / 48000, 10 + double(first + 16) / 48000,
		                              10 + double(first + 32) / 48000};
		countAllocations = true;
		const int result = callback(input.data(), output.data(), count, &time, callbackFlags, context);
		countAllocations = false;
		return result;
	}
};
struct Probe : AudioDuplexDeviceCallback {
	int calls = 0;
	AudioDuplexTime time;
	size_t inputSize = 0, outputSize = 0;
	AudioDuplexCallbackResult result = AudioDuplexCallbackResult::Continue;
	AudioDuplexCallbackResult process(std::span<const float> in, std::span<float> out,
	                                  const AudioDuplexTime &timing) noexcept override
	{
		++calls;
		time = timing;
		inputSize = in.size();
		outputSize = out.size();
		std::fill(out.begin(), out.end(), .25f);
		return result;
	}
};
AudioDuplexDeviceRequest request(const QVector<AudioDuplexEndpoint> &devices)
{
	return {devices[0].id, devices[1].id, 48000, 4, 1024};
}
bool lifecycle()
{
	Host host;
	Probe probe;
	QString error;
	{
		auto device = createAudioDuplexDeviceForTest(host.api);
		if (!expect(device->available() && host.initialized == 0, "construct/available have no device side effects"))
			return false;
		const auto old = device->enumerate(&error);
		const auto devices = device->enumerate(&error);
		if (!expect(devices.size() == 2 && host.initialized == 2 && host.terminated == 1 && host.opened == 0 &&
		                !device->open(request(old), probe, &error) && host.opened == 0,
		            "enumeration is lazy, invalidates stale tokens, and cannot capture"))
			return false;
		auto bad = request(devices);
		bad.inputChannels = 5;
		if (!expect(!device->open(bad, probe, &error) && host.opened == 0,
		            "unsupported channels rejected before host open"))
			return false;
		bad = request(devices);
		bad.inputId.clear();
		if (!expect(!device->open(bad, probe, &error), "no implicit default input"))
			return false;
		if (!expect(device->open(request(devices), probe, &error) && host.frames == 0 && host.wasapiCorrect &&
		                host.in.sampleFormat == paFloat32 && host.out.sampleFormat == paFloat32 &&
		                host.out.channelCount == 2 && host.flags == (paClipOff | paDitherOff) &&
		                device->info().sampleRate == 48000,
		            "host gets explicit float/rate/channels and variable callbacks with conversion disabled"))
			return false;
		if (!expect(device->status().state == State::Open && probe.calls == 0, "opening does not start capture"))
			return false;
		host.onStart = true;
		if (!expect(device->start(&error) && probe.calls == 1 && device->status().state == State::Running,
		            "callback can run during start"))
			return false;
		if (!expect(!device->start(&error) && !device->open(request(devices), probe, &error),
		            "no duplicate open/start"))
			return false;
		host.onAbort = true;
		device->close();
		if (!expect(host.abortSilent && probe.calls == 1 && host.closed == 1,
		            "close stops callbacks before destroying target"))
			return false;
	}
	if (!expect(host.terminated == 2 && host.closed == 1, "owner releases refreshed backend once after stream close"))
		return false;
	Host core;
	core.host.type = paCoreAudio;
	auto device = createAudioDuplexDeviceForTest(core.api);
	const auto devices = device->enumerate(&error);
	if (!expect(!device->open(request(devices), probe, &error) && core.opened == 0,
	            "CoreAudio rejects separate device clock/SRC path"))
		return false;
	auto req = request(devices);
	req.outputId = req.inputId;
	core.devices[0].defaultSampleRate = 44100;
	if (!expect(!device->open(req, probe, &error), "CoreAudio never changes the hardware sample rate implicitly"))
		return false;
	core.devices[0].defaultSampleRate = 48000;
	core.streamInfo.sampleRate = 44100;
	return expect(!device->open(req, probe, &error) && core.closed == 1, "host rate mismatch closes the unopened pass");
}
bool flagsAndFailures()
{
	Host host;
	Probe probe;
	QString error;
	auto device = createAudioDuplexDeviceForTest(host.api);
	const auto devices = device->enumerate(&error);
	const auto req = request(devices);
	if (!device->open(req, probe, &error) || !device->start(&error))
		return false;
	host.invoke(0, 8,
	            paInputOverflow | paInputUnderflow | paOutputOverflow | paOutputUnderflow | VIBE_PA_TIMESTAMP_ERROR);
	if (!expect(probe.time.inputOverflow && probe.time.inputUnderflow && probe.time.outputOverflow &&
	                probe.time.outputUnderflow && std::isnan(probe.time.inputAdc) && probe.inputSize == 32 &&
	                probe.outputSize == 16,
	            "all dropout flags, invalid timestamp and exact spans reach the processor"))
		return false;
	probe.result = AudioDuplexCallbackResult::Complete;
	if (!expect(host.invoke(8, 8) == paComplete && device->status().state == State::Draining,
	            "complete waits for native output drain"))
		return false;
	host.active = 0;
	if (!expect(device->status().state == State::Complete, "inactive after a successful drain completes"))
		return false;
	host.stopped = VIBE_PA_HOST_ERROR;
	if (!expect(device->status().state == State::Error, "drain failure never claims completion"))
		return false;
	device->close();
	host.stopped = 0;
	probe.result = AudioDuplexCallbackResult::Continue;
	if (!device->open(req, probe, &error) || !device->start(&error))
		return false;
	host.active = 0;
	if (!expect(device->status().state == State::Error, "silent native termination is an error"))
		return false;
	device->close();
	for (int fault = 0; fault < 5; ++fault) {
		if (!device->open(req, probe, &error) || !device->start(&error))
			return false;
		std::array<float, 32> in{};
		std::array<float, 16> out;
		out.fill(9);
		PaStreamCallbackTimeInfo time{10, 10, 10.001};
		const int before = probe.calls;
		const auto result = host.callback(fault == 0 ? nullptr : in.data(), fault == 1 ? nullptr : out.data(),
		                                  fault == 2   ? 0
		                                  : fault == 3 ? 65537
		                                               : 8,
		                                  fault == 4 ? nullptr : &time, 0, host.context);
		host.active = 0;
		if (!expect(result == paAbort && probe.calls == before && device->status().state == State::Error,
		            "malformed callbacks abort without touching processor"))
			return false;
		if ((fault == 0 || fault == 4) && !expect(silent(out), "invalid callback clears valid bounded output"))
			return false;
		device->close();
	}
	if (!device->open(req, probe, &error) || !device->start(&error))
		return false;
	device->requestStop();
	const int before = probe.calls;
	const int result = host.invoke(0, 8);
	host.active = 0;
	return expect(result == paAbort && before == probe.calls && device->status().state == State::Stopped,
	              "atomic Stop is acknowledged without another input block");
}
bool failedClose()
{
	struct BusyProbe final : AudioDuplexDeviceCallback {
		std::atomic<bool> entered{false}, release{false};
		std::atomic<int> calls{0};
		AudioDuplexCallbackResult process(std::span<const float>, std::span<float> out,
		                                  const AudioDuplexTime &) noexcept override
		{
			++calls;
			entered.store(true);
			while (!release.load())
				std::this_thread::yield();
			std::fill(out.begin(), out.end(), .25f);
			return AudioDuplexCallbackResult::Continue;
		}
	} probe;
	Host host;
	QString error;
	auto device = createAudioDuplexDeviceForTest(host.api);
	const auto req = request(device->enumerate(&error));
	if (!device->open(req, probe, &error) || !device->start(&error))
		return false;
	host.abortResult = host.closeResult = paUnanticipatedHostError;
	std::thread callback([&] { host.invoke(0, 8); });
	while (!probe.entered.load())
		std::this_thread::yield();
	std::thread release([&] { probe.release.store(true); });
	device->close();
	release.join();
	callback.join();
	if (!expect(!device->available() && device->status().state == State::Error && !device->open(req, probe, &error) &&
	                host.closed == 1,
	            "failed native close quarantines the device and prohibits reopening"))
		return false;
	device.reset();
	// A bad driver may retain the old context after Close returns an error.
	// It must remain safe even after both the device and target are released.
	std::array<float, 32> input{};
	std::array<float, 16> output;
	output.fill(9);
	PaStreamCallbackTimeInfo time{10, 10, 10.001};
	const int result = host.callback(input.data(), output.data(), 8, &time, 0, host.context);
	return expect(result == paAbort && silent(output) && probe.calls == 1 && host.terminated == 0,
	              "late failed-close callback reaches only quarantined Stop state, never the former target");
}
bool packets()
{
	VibeWasapiPacketState packet{};
	VibeWasapiPadding(&packet, 0);
	VibeWasapiPacket(&packet, 100, 100000000, 16, 1, 0);
	if (!expect(packet.flags == 0 && packet.inputTime == 10 && packet.nextInput == 116,
	            "first packet establishes QPC origin"))
		return false;
	VibeWasapiPacket(&packet, 116, 100003333, 16, 0, 0);
	if (!expect(packet.flags == 0, "contiguous packets preserve valid clock"))
		return false;
	VibeWasapiPacket(&packet, 133, 100006666, 16, 0, 0);
	if (!expect(packet.flags & paInputOverflow, "device position gap catches unflagged loss"))
		return false;
	packet.flags = 0;
	VibeWasapiPacket(&packet, 149, 100010000, 16, 1, 0);
	if (!expect(packet.flags & paInputOverflow, "subsequent discontinuity flags are retained"))
		return false;
	packet.flags = 0;
	VibeWasapiPacket(&packet, 165, 0, 16, 0, 1);
	VibeWasapiPadding(&packet, 0);
	if (!expect(packet.flags == VIBE_PA_TIMESTAMP_ERROR, "uncertain clock fails, startup padding does not signal loss"))
		return false;
	packet.outputWritten = 1;
	VibeWasapiPadding(&packet, 1);
	if (!expect(!(packet.flags & paOutputUnderflow), "queued output is not an underrun"))
		return false;
	VibeWasapiPadding(&packet, 0);
	return expect(packet.flags & paOutputUnderflow, "empty output after real audio conservatively signals loss");
}
struct Engine : AudioDuplexDeviceCallback {
	AudioDuplexProcessor processor;
	AudioDuplexCaptureQueue queue;
	AudioDuplexCallbackResult process(std::span<const float> in, std::span<float> out,
	                                  const AudioDuplexTime &time) noexcept override
	{
		const auto state = processor.process(in, out, time, queue);
		if (state == AudioDuplexProgress::State::Complete)
			return AudioDuplexCallbackResult::Complete;
		return state == AudioDuplexProgress::State::Error ? AudioDuplexCallbackResult::Abort
		                                                  : AudioDuplexCallbackResult::Continue;
	}
};
bool endToEnd()
{
	AudioSession session;
	session.sampleRate = 48000;
	session.effectTailSeconds = 0;
	AudioSessionTrack left, right;
	left.id = "left";
	left.name = "Left";
	right.id = "right";
	right.name = "Right";
	session.tracks = {left, right};
	AudioDuplexPass pass;
	pass.punchFirst = 64;
	pass.punchEnd = 137;
	pass.inputChannels = 4;
	pass.blockFrames = 17;
	pass.arms = {{"left", 2, {3, 1}}, {"right", 1, {2, 0}}};
	Engine engine;
	QString error;
	const std::array<int, 2> channels{2, 1};
	if (!expect(engine.processor.prepare(session, pass, &error) && engine.queue.prepare(64, channels, 128),
	            "real engine/queue prepare"))
		return false;
	Host host;
	auto device = createAudioDuplexDeviceForTest(host.api);
	if (!device->open(request(device->enumerate(&error)), engine, &error) || !device->start(&error))
		return false;
	int first = 0, result = paContinue;
	for (const int count : {13, 27, 61, 31, 75}) {
		result = host.invoke(first, count);
		first += count;
	}
	if (!expect(result == paComplete && engine.processor.progress().capturedFrames == 73 &&
	                engine.processor.progress().roundTripFrames == 32 && device->status().state == State::Draining,
	            "production adapter, processor and queue agree on punch and clock placement"))
		return false;
	host.active = 0;
	if (!expect(device->status().state == State::Complete, "full pass completes after drain"))
		return false;
	device->close();
	QTemporaryDir temp(QDir::tempPath() + "/audio-duplex-device-XXXXXX");
	if (!temp.isValid())
		return false;
	std::array<AudioTakeWriter, 2> writers;
	for (int arm = 0; arm < 2; ++arm) {
		AudioTakeMetadata metadata;
		metadata.name = QString::number(arm);
		metadata.sampleRate = 48000;
		metadata.inputChannels = 4;
		metadata.channelMap = arm == 0 ? QVector<int>{3, 1} : QVector<int>{2};
		metadata.position = 64;
		metadata.trackId = pass.arms[arm].trackId;
		if (!writers[size_t(arm)].open(temp.filePath(QString::number(arm) + ".vstake"), metadata, &error))
			return false;
	}
	AudioDuplexCaptureBlock block;
	qint64 read = 0;
	while ((block = engine.queue.read(29)).frames) {
		for (int arm = 0; arm < 2; ++arm) {
			for (int f = 0; f < block.frames; ++f)
				for (int c = 0; c < channels[size_t(arm)]; ++c)
					if (!expect(block.samples[size_t(arm)][size_t(f * channels[size_t(arm)] + c)] ==
					                float((96 + read + f) * 4 + pass.arms[arm].channelMap[size_t(c)]),
					            "dry sample ordinals and explicit channel mapping survive native adapter"))
						return false;
			if (!writers[size_t(arm)].append(block.samples[size_t(arm)], &error))
				return false;
		}
		read += block.frames;
		engine.queue.consume(block.frames);
	}
	for (int arm = 0; arm < 2; ++arm) {
		if (!writers[size_t(arm)].finish(&error))
			return false;
		writers[size_t(arm)].close();
		const auto info = inspectAudioTake(temp.filePath(QString::number(arm) + ".vstake"));
		if (!expect(info.complete && info.frames == 73 && info.metadata.position == 64 &&
		                info.metadata.latencyFrames == 0,
		            "each durable take records already-corrected position once"))
			return false;
	}
	return expect(read == 73, "entire capture prefix reaches normal journal storage");
}
#if VIBESTUDIO_HAVE_AUDIO_DUPLEX
bool silentConversion()
{
	// Exercise the actual pinned buffer processor. No Pa_Initialize or devices.
	Probe probe;
	auto callback = [](const void *input, void *output, unsigned long frames, const PaStreamCallbackTimeInfo *,
	                   PaStreamCallbackFlags flags, void *context) -> int {
		auto &p = *static_cast<Probe *>(context);
		++p.calls;
		if (!silent({static_cast<const float *>(input), size_t(frames) * 2}) || !(flags & paInputOverflow))
			p.inputSize = 1;
		std::fill_n(static_cast<float *>(output), size_t(frames) * 2, .5f);
		return paContinue;
	};
	PaUtilBufferProcessor processor{};
	if (!expect(PaUtil_InitializeBufferProcessor(&processor, 2, paFloat32, paInt16, 2, paFloat32, paFloat32, 48000,
	                                             paClipOff | paDitherOff, 0, 16, paUtilBoundedHostBufferSize, callback,
	                                             &probe) == paNoError,
	            "native buffer converter prepares"))
		return false;
	std::array<float, 74> output{};
	PaStreamCallbackTimeInfo time{10, 10.01, 10.02};
	PaUtil_BeginBufferProcessing(&processor, &time, paInputOverflow);
	PaUtil_SetInputFrameCount(&processor, 37);
	PaUtil_SetNoInput(&processor);
	PaUtil_SetOutputFrameCount(&processor, 37);
	PaUtil_SetInterleavedOutputChannels(&processor, 0, output.data(), 0);
	int result = paContinue;
	const auto frames = PaUtil_EndBufferProcessing(&processor, &result);
	PaUtil_TerminateBufferProcessor(&processor);
	return expect(frames == 37 && probe.calls == 3 && probe.inputSize == 0 &&
	                  std::all_of(output.begin(), output.end(), [](float v) { return v == .5f; }),
	              "silent host packet becomes zeros across real variable-size conversion chunks with flags");
}
#endif
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	if (!lifecycle() || !flagsAndFailures() || !failedClose() || !packets() || !endToEnd())
		return 1;
#if VIBESTUDIO_HAVE_AUDIO_DUPLEX
	if (!silentConversion())
		return 1;
#else
	const auto native = createAudioDuplexDevice();
	if (!expect(!native->available(), "disabled build exposes no native backend"))
		return 1;
#endif
	if (!expect(allocations == 0, "application device callbacks do not allocate C++ objects"))
		return 1;
	std::cout
	    << "Duplex device policy, packet clocks, lifecycle and journal handoff passed without native device access.\n";
	return 0;
}
