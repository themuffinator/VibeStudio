#include "core/audio_take.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <array>
#include <bit>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *text)
{
	if (!condition)
		std::cerr << text << '\n';
	return condition;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray sum(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
void u32(QByteArray &bytes, quint32 value)
{
	for (int i = 0; i < 4; ++i)
		bytes.append(char((value >> (i * 8)) & 255));
}
void u64(QByteArray &bytes, quint64 value)
{
	for (int i = 0; i < 8; ++i)
		bytes.append(char((value >> (i * 8)) & 255));
}
QByteArray record(QByteArray &digest, int sequence, qint64 first, const QVector<float> &values, bool done = false)
{
	QByteArray bytes = done ? QByteArrayLiteral("DONE") : QByteArrayLiteral("DATA");
	u32(bytes, quint32(sequence));
	u64(bytes, quint64(first));
	u32(bytes, quint32(values.size() / 2));
	u32(bytes, quint32(values.size() * 4));
	for (float value : values)
		u32(bytes, std::bit_cast<quint32>(value));
	digest = sum(digest + bytes);
	return bytes + digest;
}
QByteArray fixture(QByteArray *prefixDigest, int *headerSize, int *prefixSize)
{
	// Independent wire fixture: no production writer or metadata serializer.
	const QByteArray metadata =
	    R"({"name":"Fixture","sampleRate":48000,"inputChannels":3,"channelMap":[2,0],"position":1234,"latencyFrames":48,"trackId":"voice","sourceSessionPath":"recorded-only.vssession","deviceName":"Fixture input","startedUtc":"2026-10-05T12:00:00.000Z"})";
	QByteArray bytes = QByteArrayLiteral("VSTAK\r\n\x1a");
	u32(bytes, 1);
	u32(bytes, quint32(metadata.size()));
	bytes += metadata;
	QByteArray digest = sum(bytes);
	bytes += digest;
	*headerSize = int(bytes.size());
	bytes += record(digest, 0, 0, {.25f, -.5f, .75f, -1, 1.5f, -2, 0, .125f});
	*prefixDigest = digest;
	*prefixSize = int(bytes.size());
	bytes += record(digest, 1, 4, {1, 2, 3, 4});
	bytes += record(digest, 2, 6, {}, true);
	return bytes;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--capture-child") {
		AudioTakeWriter writer;
		AudioTakeMetadata metadata;
		metadata.name = "Interrupted fixture";
		QString error;
		const std::array<float, 3> samples{.25f, -.5f, .75f};
		if (!writer.open(QString::fromLocal8Bit(argv[2]), metadata, &error) || !writer.append(samples, &error))
			return 2;
		std::cout << "capture-ready\n" << std::flush;
		QTimer::singleShot(60000, &application, &QCoreApplication::quit);
		return application.exec();
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("take-core-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QLatin1String(name)); };
	QByteArray prefixDigest;
	int headerSize = 0, prefixSize = 0;
	const auto bytes = fixture(&prefixDigest, &headerSize, &prefixSize);
	bool ok = expect(write(path("independent.vstake"), bytes), "write independent journal fixture");
	const auto info = inspectAudioTake(path("independent.vstake"));
	ok &= expect(info.complete && info.error.isEmpty() && info.frames == 6 &&
	                 info.metadata.channelMap == QVector<int>{2, 0} && info.verifiedBytes == bytes.size(),
	             "independent complete take validates exact metadata and frames");
	const auto selected = readAudioTakeRange(info.path, info.prefixSha256, 1, 5, {1, 0});
	ok &= expect(selected.audio.metadata.value("takeHardwareChannels").toArray() == QJsonArray{0, 2} &&
	                 selected.audio.metadata.value("takeStoredChannels").toArray() == QJsonArray{1, 0},
	             "range import retains actual stored and hardware channel provenance");
	ok &= expect(selected.succeeded() &&
	                 selected.audio.clip.samples == QVector<float>{-1, .75f, -2, 1.5f, .125f, 0, 2, 1},
	             "cross-block range and channel permutation preserve exact independent samples");
	const QVector<AudioTakeReadRange> ranges{{4, 6, {1}}, {1, 5, {1, 0}}, {0, 2, {0}}};
	const auto batch = readAudioTakeRanges(info.path, info.prefixSha256, ranges);
	ok &= expect(batch.succeeded() && batch.audio.size() == 3 && batch.audio[0].clip.samples == QVector<float>{2, 4} &&
	                 batch.audio[1].clip.samples == selected.audio.clip.samples &&
	                 batch.audio[2].clip.samples == QVector<float>{.25f, .75f} &&
	                 batch.audio[0].metadata["takeFirstFrame"].toInteger() == 4 &&
	                 batch.audio[0].metadata["takeHardwareChannels"].toArray() == QJsonArray{0},
	             "batched overlapping and reordered ranges retain exact samples, ordering and provenance");
	for (int fault = 0; fault < 4; ++fault) {
		auto invalid = ranges;
		auto hash = info.prefixSha256;
		if (fault == 0)
			invalid[2].end = 7;
		if (fault == 1)
			invalid[2].channels = {2};
		if (fault == 2)
			hash = QByteArray(32, 'x');
		int polls = 0;
		const auto rejected =
		    readAudioTakeRanges(info.path, hash, invalid, false, {[&] { return fault == 3 && ++polls > 1; }});
		ok &= expect(!rejected.succeeded() && rejected.audio.isEmpty(),
		             "one failed range, stale digest or cancellation discards the entire batch");
	}
	ok &= expect(!readAudioTakeRange(info.path, QByteArray(32, 'x'), 0, 1, {0}).succeeded() &&
	                 !readAudioTakeRange(info.path, info.prefixSha256, 0, 7, {0}).succeeded() &&
	                 !readAudioTakeRange(info.path, info.prefixSha256, 0, 1, {0, 0}).succeeded() &&
	                 !readAudioTakeRange(info.path, info.prefixSha256, 0, 1, {2}).succeeded(),
	             "changed digest, unavailable range and invalid channel mappings cannot import");
	for (int cut = 0; cut < bytes.size(); ++cut) {
		ok &= write(path("truncated.vstake"), bytes.left(cut));
		const auto partial = inspectAudioTake(path("truncated.vstake"));
		ok &= expect(!partial.complete && partial.frames <= 6 && (cut >= prefixSize || partial.frames == 0),
		             "every truncation rejects uncommitted samples");
	}
	ok &= write(path("truncated.vstake"), bytes.left(prefixSize));
	const auto interrupted = inspectAudioTake(path("truncated.vstake"));
	ok &= expect(interrupted.recoverable() && interrupted.frames == 4 && interrupted.prefixSha256 == prefixDigest &&
	                 !readAudioTakeRange(interrupted.path, prefixDigest, 0, 4, {0}).succeeded() &&
	                 readAudioTakeRange(interrupted.path, prefixDigest, 0, 4, {0}, true).succeeded(),
	             "verified incomplete prefix needs explicit recovery acceptance");
	auto damaged = bytes;
	damaged[prefixSize + 25] = char(damaged[prefixSize + 25] ^ 1);
	ok &= write(path("damaged.vstake"), damaged);
	const auto corrupt = inspectAudioTake(path("damaged.vstake"));
	ok &= expect(!corrupt.complete && corrupt.frames == 4 && corrupt.prefixSha256 == prefixDigest &&
	                 readAudioTakeRange(corrupt.path, prefixDigest, 0, 4, {0}, true).succeeded(),
	             "corruption stops recovery at the prior chained block");
	damaged = bytes;
	damaged[8] = 2;
	ok &= write(path("version.vstake"), damaged);
	ok &= expect(!inspectAudioTake(path("version.vstake")).headerValid, "unknown journal version fails");
	damaged = bytes.left(headerSize);
	QByteArray digest = damaged.right(32);
	damaged += record(digest, 0, 1, {0, 0});
	ok &= write(path("gap.vstake"), damaged);
	ok &= expect(inspectAudioTake(path("gap.vstake")).frames == 0, "valid checksum cannot hide a timing gap");
	damaged = bytes.left(headerSize);
	digest = damaged.right(32);
	damaged += record(digest, 0, 0, {std::numeric_limits<float>::infinity(), 0});
	ok &= write(path("nonfinite.vstake"), damaged);
	ok &=
	    expect(inspectAudioTake(path("nonfinite.vstake")).frames == 0, "valid checksum cannot admit non-finite input");
	int calls = 0;
	ok &= expect(inspectAudioTake(info.path, {[&] { return ++calls > 1; }}).cancelled,
	             "stream verification is cancellable between blocks");
	AudioTakeMetadata metadata;
	metadata.name = "Writer fixture";
	metadata.inputChannels = 3;
	metadata.channelMap = {2, 0};
	AudioTakeWriter writer;
	QString error;
	const std::array<float, 4> signal{.25f, -.5f, .75f, -1};
	ok &= expect(writer.open(path("recorded.vstake"), metadata, &error) && writer.append(signal, &error) &&
	                 writer.frames() == 2 && writer.finish(&error),
	             "writer creates and flushes a complete journal");
	const auto written = inspectAudioTake(path("recorded.vstake"));
	ok &= expect(written.complete && written.frames == 2 &&
	                 readAudioTakeRange(written.path, written.prefixSha256, 0, 2, {0, 1}).audio.clip.samples ==
	                     QVector<float>{.25f, -.5f, .75f, -1},
	             "writer round trip retains independent signal");
	const auto original = read(written.path);
	ok &= expect(!writer.open(written.path, metadata, &error) && read(written.path) == original,
	             "recording never overwrites an existing take");
	metadata.latencyFrames = std::numeric_limits<qint64>::min();
	ok &= expect(!validateAudioTakeMetadata(metadata).isEmpty(),
	             "minimum signed compensation is rejected without overflow");
	QProcess child;
	child.setWorkingDirectory(temporary.path());
	child.start(application.applicationFilePath(), {"--capture-child", path("interrupted.vstake")});
	bool ready = child.waitForStarted(10000);
	QByteArray message;
	for (int i = 0; ready && i < 10 && !message.contains("capture-ready"); ++i) {
		child.waitForReadyRead(1000);
		message += child.readAllStandardOutput();
	}
	ready &= message.contains("capture-ready");
	child.kill(); // Only the fixture process created above, after its flush signal.
	child.waitForFinished(10000);
	const auto killed = inspectAudioTake(path("interrupted.vstake"));
	ok &= expect(ready && killed.recoverable() && !killed.complete && killed.frames == 3 &&
	                 readAudioTakeRange(killed.path, killed.prefixSha256, 0, 3, {0}, true).audio.clip.samples ==
	                     QVector<float>{.25f, -.5f, .75f},
	             "actual child-process interruption retains flushed sample blocks");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
