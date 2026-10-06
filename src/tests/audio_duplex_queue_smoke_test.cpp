#include "core/audio_duplex_queue.h"
#include "core/audio_take.h"
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
float sample(qint64 frame, int arm, int channel) { return float(frame * 32 + arm * 2 + channel); }
bool concurrent(int capacity)
{
	constexpr qint64 total = 100003, first = 29;
	AudioDuplexCaptureQueue queue;
	const std::array<int, 8> channels{2, 1, 2, 1, 2, 1, 2, 1};
	if (!expect(queue.prepare(first, channels, capacity), "concurrent recording queue prepares"))
		return false;
	std::atomic<bool> failed{false};
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	std::thread producer([&] {
		std::array<std::array<float, 8192>, 8> samples{};
		for (qint64 at = 0; at < total && !failed.load();) {
			AudioDuplexCaptureBlock block;
			block.first = first + at;
			block.frames = int(std::min<qint64>({1 + at % 127, total - at, capacity}));
			block.arms = 8;
			for (int arm = 0; arm < 8; ++arm) {
				for (int f = 0; f < block.frames; ++f)
					for (int c = 0; c < channels[size_t(arm)]; ++c)
						samples[size_t(arm)][size_t(f * channels[size_t(arm)] + c)] = sample(at + f, arm, c);
				block.samples[size_t(arm)] =
				    std::span(samples[size_t(arm)]).first(size_t(block.frames * channels[size_t(arm)]));
			}
			// Only this synthetic producer retries. A real callback must stop on
			// a full queue instead of waiting for disk I/O.
			while (!queue.push(block) && !failed.load()) {
				if (std::chrono::steady_clock::now() > deadline)
					failed = true;
				std::this_thread::yield();
			}
			at += block.frames;
		}
	});
	qint64 at = 0;
	while (at < total && !failed.load()) {
		const auto block = queue.read(1 + int(at % 4096));
		if (!block.frames) {
			if (std::chrono::steady_clock::now() > deadline)
				failed = true;
			std::this_thread::yield();
			continue;
		}
		bool valid = block.first == first + at && block.arms == 8;
		for (int arm = 0; arm < 8; ++arm) {
			valid &= block.samples[size_t(arm)].size() == size_t(block.frames * channels[size_t(arm)]);
			for (int f = 0; f < block.frames; ++f)
				for (int c = 0; c < channels[size_t(arm)]; ++c)
					valid &=
					    block.samples[size_t(arm)][size_t(f * channels[size_t(arm)] + c)] == sample(at + f, arm, c);
		}
		valid &= queue.consume(block.frames);
		if (!valid)
			failed = true;
		at += block.frames;
	}
	producer.join();
	return expect(!failed.load() && at == total && queue.producedFrames() == total && queue.consumedFrames() == total &&
	                  !queue.read().frames,
	              "concurrent wraparound preserves every sample and publishes complete arm sets");
}
bool boundary()
{
	AudioDuplexCaptureQueue queue;
	const std::array<int, 2> channels{2, 1};
	bool ok = expect(!queue.prepare(-1, channels, 8) && !queue.prepare(0, {}, 8) && !queue.prepare(0, channels, 0) &&
	                     !queue.prepare(0, channels, 262145),
	                 "unsupported queue bounds fail without a usable ring");
	ok &= queue.prepare(9, channels, 5);
	std::array<float, 8> stereo{1, 2, 3, 4, 5, 6, 7, 8};
	std::array<float, 4> mono{9, 10, 11, 12};
	AudioDuplexCaptureBlock block{9, 4, 2};
	block.samples[0] = stereo;
	block.samples[1] = mono;
	ok &= expect(queue.push(block), "initial arm set accepted");
	block.first = 13;
	ok &= expect(!queue.push(block) && queue.producedFrames() == 4, "queue full does not publish or overwrite any arm");
	auto read = queue.read(2);
	ok &= expect(read.frames == 2 && read.samples[0][0] == 1 && read.samples[1][1] == 10 && !queue.consume(3) &&
	                 queue.consume(1),
	             "consumer cannot release unread frames");
	read = queue.read();
	ok &= expect(read.first == 10 && read.frames == 3 && read.samples[0][0] == 3 && read.samples[1][2] == 12 &&
	                 queue.consume(3),
	             "partial consumption retains the remainder");
	ok &= queue.push(block);
	read = queue.read();
	ok &= expect(read.first == 13 && read.frames == 1 && read.samples[0][0] == 1 && queue.consume(1),
	             "wrapped block exposes its contiguous suffix first");
	read = queue.read();
	ok &= expect(read.first == 14 && read.frames == 3 && read.samples[0][0] == 3 && read.samples[0][5] == 8 &&
	                 queue.consume(3),
	             "wrapped prefix follows without a gap");
	block.first = 17;
	block.samples[1] = std::span(mono).first(3);
	ok &=
	    expect(!queue.push(block) && queue.producedFrames() == 8, "malformed arm rejects the complete multi-arm block");
	block.samples[1] = mono;
	block.first = 18;
	ok &= expect(!queue.push(block), "queue cannot hide a timing gap");
	ok &= expect(!queue.read(0).frames && !queue.read(4097).frames && !queue.consume(0),
	             "invalid reads and releases reject");
	return ok;
}
bool journals()
{
	QTemporaryDir directory(QDir::tempPath() + "/audio-duplex-journals-XXXXXX");
	if (!expect(directory.isValid(), "private journal fixture directory created"))
		return false;
	bool ok = true;
	for (bool complete : {false, true}) {
		AudioSession session;
		session.sampleRate = 1000;
		for (int i = 0; i < 2; ++i) {
			AudioSessionTrack track;
			track.id = track.name = QString::number(i);
			session.tracks.append(track);
		}
		AudioDuplexPass pass;
		pass.punchFirst = 10;
		pass.punchEnd = 53;
		pass.inputChannels = 3;
		pass.blockFrames = 7;
		pass.arms = {{"0", 2, {2, 0}}, {"1", 1, {1, 0}}};
		AudioDuplexProcessor processor;
		AudioDuplexCaptureQueue queue;
		QString error;
		const std::array<int, 2> channels{2, 1};
		if (!expect(processor.prepare(session, pass, &error) && queue.prepare(pass.punchFirst, channels, 41),
		            "journal producer and queue prepare"))
			return false;
		std::array<AudioTakeWriter, 2> writers;
		std::array<QString, 2> paths;
		for (int arm = 0; arm < 2; ++arm) {
			paths[size_t(arm)] = directory.filePath(QString("%1-%2.vstake").arg(complete).arg(arm));
			AudioTakeMetadata metadata;
			metadata.name = "Duplex fixture";
			metadata.sampleRate = 1000;
			metadata.inputChannels = 3;
			metadata.channelMap = arm ? QVector<int>{1} : QVector<int>{2, 0};
			metadata.position = pass.punchFirst;
			metadata.latencyFrames = 0; // Capture is already placed; never apply it twice.
			metadata.trackId = pass.arms[arm].trackId;
			ok &= expect(writers[size_t(arm)].open(paths[size_t(arm)], metadata, &error), error.toUtf8().constData());
		}
		for (int at = 0; at < (complete ? 96 : 32); at += 32) {
			std::array<float, 96> input{};
			std::array<float, 64> output{};
			for (int f = 0; f < 32; ++f)
				for (int c = 0; c < 3; ++c)
					input[size_t(f * 3 + c)] = float((at + f) * 4 + c);
			const auto state = processor.process(input, output, {10 + at / 1000., 10.007 + at / 1000.}, queue);
			ok &= expect(state == AudioDuplexProgress::State::Running || state == AudioDuplexProgress::State::Complete,
			             "synthetic device publishes a contiguous journal pass");
			// Explicitly outside process: disk operations never run on callback.
			for (auto block = queue.read(); block.frames; block = queue.read()) {
				for (int arm = 0; arm < 2; ++arm)
					ok &= expect(writers[size_t(arm)].append(block.samples[size_t(arm)], &error),
					             error.toUtf8().constData());
				ok &= queue.consume(block.frames);
			}
		}
		processor.stop();
		for (int arm = 0; arm < 2; ++arm) {
			if (complete)
				ok &= writers[size_t(arm)].finish(&error);
			writers[size_t(arm)].close();
			const auto info = inspectAudioTake(paths[size_t(arm)]);
			const auto expectedFrames = complete ? 43 : 15;
			ok &= expect(info.recoverable() && info.complete == complete && info.frames == expectedFrames &&
			                 info.metadata.position == 10 && info.metadata.latencyFrames == 0,
			             "complete and interrupted takes retain independently verified placement and prefixes");
			const QVector<int> selected = arm ? QVector<int>{0} : QVector<int>{0, 1};
			const auto read =
			    readAudioTakeRange(paths[size_t(arm)], info.prefixSha256, 0, info.frames, selected, !complete);
			ok &= expect(read.succeeded(), "journal review uses the existing digest-checked take reader");
			if (!read.succeeded())
				return false;
			for (int f = 0; f < expectedFrames; ++f)
				for (int c = 0; c < channels[size_t(arm)]; ++c) {
					const int inputChannel = pass.arms[arm].channelMap[size_t(c)];
					ok &= expect(read.audio.clip.samples[f * channels[size_t(arm)] + c] ==
					                 float((17 + f) * 4 + inputChannel),
					             "recoverable take contains the aligned raw input, including values over full scale");
				}
			const auto imported =
			    importAudioSessionSource(session, read.audio, pass.arms[arm].trackId, info.metadata.position);
			ok &= expect(
			    imported.succeeded() && imported.session.tracks[arm].regions.last().position == pass.punchFirst,
			    "reviewed duplex take enters the ordinary session source/clip service at its corrected position");
		}
	}
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = boundary();
	for (int capacity : {1, 3, 4096, 65536, 262144})
		ok &= concurrent(capacity);
	ok &= journals();
	return ok ? 0 : 1;
}
