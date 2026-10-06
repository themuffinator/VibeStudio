#include "core/audio_recording.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}
bool rewrite(const QString &path, const std::function<void(QJsonObject &)> &change)
{
	auto payload = QJsonDocument::fromJson(read(path)).object().value("payload").toObject();
	change(payload);
	const auto bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
	return write(
	    path,
	    QJsonDocument(QJsonObject{{"payload", payload},
	                              {"sha256", QString::fromLatin1(
	                                             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}})
	        .toJson(QJsonDocument::Compact));
}
AudioRecordingPlan plan()
{
	AudioRecordingPlan p;
	p.name = "Recording fixture";
	p.sourceSessionPath = "provenance-only/nonexistent.vssession";
	p.pass = {5, 37, 44, 3, 4, 17, 0, .5, {{"track-a", 2, {3, 1}}, {"track-b", 1, {2, 0}}}};
	return p;
}
bool take(const QString &directory, const AudioRecordingPlan &p, int index, int frames, bool complete,
          float sample = .25f)
{
	AudioTakeWriter writer;
	QString error;
	const auto metadata = audioRecordingArmMetadata(p, index);
	const std::vector<float> samples(size_t(frames * metadata.channelMap.size()), sample);
	return writer.open(audioRecordingArmPath(directory, index), metadata, &error) && writer.append(samples, &error) &&
	       (!complete || writer.finish(&error));
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath("recording-store-XXXXXX"));
	if (!temp.isValid())
		return EXIT_FAILURE;
	bool ok = true;
	QString error;
	QByteArray hash;
	int serial = 0;
	const auto path = [&] { return QDir(temp.path()).filePath(QString::number(++serial) + ".vsrecord"); };
	const auto p = plan();
	const auto dir = path();
	ok &= expect(validateAudioRecordingPlan(p).isEmpty() && createAudioRecordingFolder(dir, p, &hash, &error),
	             "new immutable recording plan");
	const auto original = read(QDir(dir).filePath("plan.json"));
	ok &= expect(hash.size() == 32 && !createAudioRecordingFolder(dir, p, nullptr, &error) &&
	                 original == read(QDir(dir).filePath("plan.json")),
	             "existing plan is never replaced");
	auto info = inspectAudioRecording(dir);
	ok &= expect(info.planValid && !info.receiptValid && info.takes.size() == 2 && !info.takes[0].headerValid,
	             "missing journals remain visible in unfinished group");
	ok &= expect(take(dir, p, 0, 7, true) && take(dir, p, 1, 7, true), "two independent take journals");
	AudioRecordingReceipt receipt;
	receipt.outcome = AudioRecordingReceipt::Outcome::Complete;
	receipt.progress.state = AudioDuplexProgress::State::Complete;
	receipt.progress.capturedFrames = 7;
	receipt.progress.processedFrames = 101;
	receipt.progress.roundTripFrames = 29;
	receipt.progress.inputTimelineOrigin = -12;
	receipt.inputName = "Fixture input";
	receipt.outputName = "Fixture output";
	receipt.host = "Fixture host";
	receipt.packetInputTimestamp = true;
	ok &= expect(!finishAudioRecordingFolder(dir, QByteArray(32, 'x'), receipt, &error),
	             "wrong initial plan digest refused");
	ok &= expect(finishAudioRecordingFolder(dir, hash, receipt, &error, &info) && info.receiptMatches &&
	                 info.takes[1].frames == 7,
	             "final receipt reconciles independently scanned journals");
	ok &= expect(inspectAudioRecording(dir).receiptMatches && audioRecordingInfoJson(info).value("planValid").toBool(),
	             "persisted receipt round trip");
	const auto receiptPath = QDir(dir).filePath("result.json");
	const auto originalReceipt = read(receiptPath);
	ok &= expect(!finishAudioRecordingFolder(dir, hash, receipt, &error) && read(receiptPath) == originalReceipt,
	             "final receipt is new-only");
	ok &= expect(rewrite(receiptPath, [](auto &json) { json["capturedFrames"] = 6; }) &&
	                 !inspectAudioRecording(dir).receiptMatches,
	             "a rehashed false frame count is rejected");
	ok &= expect(write(receiptPath, originalReceipt + "\n") && !inspectAudioRecording(dir).receiptValid &&
	                 inspectAudioRecording(dir).takes[0].recoverable(),
	             "noncanonical receipt retains independent verified prefixes");
	ok &= expect(write(receiptPath, originalReceipt) && QFile::remove(audioRecordingArmPath(dir, 1)) &&
	                 take(dir, p, 1, 7, true, .5f) && !inspectAudioRecording(dir).receiptMatches,
	             "same-length substituted audio cannot match stored prefix digest");
	const auto partial = path();
	ok &= expect(createAudioRecordingFolder(partial, p, &hash, &error) && take(partial, p, 0, 4, false) &&
	                 take(partial, p, 1, 2, false),
	             "independent incomplete lengths can be retained");
	receipt.outcome = AudioRecordingReceipt::Outcome::Interrupted;
	receipt.progress.state = AudioDuplexProgress::State::Error;
	receipt.progress.capturedFrames = 4;
	receipt.progress.fault = AudioDuplexFault::CaptureQueueFull;
	receipt.error = "Injected failure";
	ok &= expect(finishAudioRecordingFolder(partial, hash, receipt, &error, &info) && info.receiptMatches &&
	                 info.takes[0].frames == 4 && info.takes[1].frames == 2 && !info.takes[0].complete,
	             "interrupted receipt does not invent equal arm lengths or completion");
	const auto mismatch = path();
	ok &= expect(createAudioRecordingFolder(mismatch, p, &hash, &error), "metadata fixture plan");
	auto other = p;
	other.name = "Substituted metadata";
	ok &= expect(take(mismatch, other, 0, 2, false) && !inspectAudioRecording(mismatch).takes[0].headerValid,
	             "group checks take metadata against plan");
	const auto planPath = QDir(mismatch).filePath("plan.json");
	ok &=
	    expect(rewrite(planPath, [](auto &json) { json["extra"] = 42; }) && !inspectAudioRecording(mismatch).planValid,
	           "unknown rehashed plan field rejected");
	for (int invalid = 0; invalid < 7; ++invalid) {
		auto bad = p;
		switch (invalid) {
		case 0:
			bad.pass.calibrationFrames = std::numeric_limits<qint64>::min();
			break;
		case 1:
			bad.pass.arms[1].trackId = bad.pass.arms[0].trackId;
			break;
		case 2:
			bad.pass.arms[0].channels = 3;
			break;
		case 3:
			bad.pass.arms[0].channelMap = {1, 1};
			break;
		case 4:
			bad.pass.arms[0].replacePlayback = true;
			break;
		case 5:
			bad.pass.outputGain = std::numeric_limits<double>::quiet_NaN();
			break;
		case 6:
			bad.startedUtc = {};
			break;
		}
		const auto target = path();
		ok &= expect(!createAudioRecordingFolder(target, bad, nullptr, &error) && !QFileInfo::exists(target),
		             "invalid plan creates no folder");
	}
#ifndef Q_OS_WIN
	const auto linked = QDir(temp.path()).filePath("linked");
	ok &= expect(QFile::link(dir, linked) && !inspectAudioRecording(linked).planValid &&
	                 !createAudioRecordingFolder(QDir(linked).filePath("escape.vsrecord"), p, nullptr, &error),
	             "linked folder rejected for read and write");
#endif
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
