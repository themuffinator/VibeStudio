#include "core/audio_recording_review.h"
#include "core/audio_session_io.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}
QByteArray bytes(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool put(const QString &path, const QByteArray &value)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(value) == value.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath("recording-review-XXXXXX"));
	if (!temp.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temp.path()).filePath(QLatin1String(name)); };
	AudioSession base;
	AudioSessionTrack track;
	track.id = "voice";
	track.name = "Voice";
	base.tracks = {track};
	const auto baseBytes = encodeAudioSession(base);
	AudioRecordingPlan plan;
	plan.name = "Saved review fixture";
	plan.pass.punchEnd = 12;
	plan.pass.loopPasses = 2;
	plan.pass.inputChannels = 2;
	plan.pass.arms = {{track.id, 2, {0, 1}}};
	QByteArray planHash;
	QString error;
	const auto folder = path("takes.vsrecord");
	bool ok = expect(createAudioRecordingFolder(folder, plan, &planHash, &error), "create review recording");
	AudioTakeWriter writer;
	QVector<float> samples;
	for (int i = 0; i < 48; ++i)
		samples << float(i + 1) / 64;
	const auto journal = audioRecordingArmPath(folder, 0);
	ok &= expect(writer.open(journal, audioRecordingArmMetadata(plan, 0), &error) &&
	                 writer.append(std::span<const float>(samples.constData(), size_t(samples.size())), &error) &&
	                 writer.finish(&error),
	             "write review takes");
	writer.close();
	AudioRecordingReceipt receipt;
	receipt.outcome = AudioRecordingReceipt::Outcome::Complete;
	receipt.progress.state = AudioDuplexProgress::State::Complete;
	receipt.progress.capturedFrames = 24;
	AudioRecordingInfo info;
	ok &= expect(finishAudioRecordingFolder(folder, planHash, receipt, &error, &info), "finish review recording");
	if (!ok)
		return EXIT_FAILURE;
	const auto originalJournal = bytes(journal);
	AudioRecordingImportRequest review;
	review.directory = folder;
	review.expectedPlanSha256 = info.planSha256;
	review.expectedReceiptSha256 = info.receiptSha256;
	review.comp = true;
	review.crossfadeFrames = 3;
	review.selections = {{0, info.takes[0].prefixSha256, 0, 4, 0, {1, 0}, track.id, true, 0},
	                     {0, info.takes[0].prefixSha256, 4, 8, 4, {0, 1}, track.id, false, 1},
	                     {0, info.takes[0].prefixSha256, 8, 12, 8, {0, 1}, track.id, true, 0}};
	ok &= expect(QDir().mkpath(path("reviews")), "create review destination");
	AudioProjectSaveRequest output;
	output.path = path("reviews/comp.json");
	output.dryRun = true;
	const auto dry = writeAudioRecordingReview(base, review, output);
	ok &= expect(dry.succeeded && !dry.written && !QFileInfo::exists(output.path) &&
	                 !QFileInfo::exists(output.path + ".lock"),
	             "dry run verifies without writing a file or lock");
	output.dryRun = false;
	auto saved = writeAudioRecordingReview(base, review, output);
	ok &= expect(saved.succeeded && saved.written && saved.identity.sha256.size() == 32,
	             "atomic save returns the committed review identity");
	const auto firstBytes = bytes(output.path);
	ok &= expect(QJsonDocument::fromJson(firstBytes).object()["directory"].toString() == "../takes.vsrecord",
	             "save uses a relative recording path anchored to its own directory");
	auto opened = openAudioRecordingReview(base, output.path);
	ok &= expect(opened.succeeded() &&
	                 audioRecordingImportRequestJson(opened.review) == audioRecordingImportRequestJson(review) &&
	                 opened.identity.sha256 == saved.identity.sha256,
	             "reopening restores every cut, pass, channel order, destination, replacement and fade choice");
	const auto original = prepareAudioRecordingAudition(base, review);
	const auto restored = prepareAudioRecordingAudition(base, opened.review);
	ok &= expect(original.succeeded() && restored.succeeded() &&
	                 renderAudioSession(original.imported.session, 0, 12).clip.samples ==
	                     renderAudioSession(restored.imported.session, 0, 12).clip.samples,
	             "saved comp audition reproduces the exact sample sequence");
	ok &= expect(!writeAudioRecordingReview(base, review, output).succeeded && bytes(output.path) == firstBytes,
	             "unreviewed existing files cannot be overwritten implicitly");
	output.overwrite = true;
	output.expected = saved.identity;
	review.groupRegions = false;
	saved = writeAudioRecordingReview(base, review, output);
	ok &= expect(saved.succeeded && saved.identity.sha256 != output.expected.sha256,
	             "reviewed update preserves identity conflict checking");
	const auto updatedBytes = bytes(output.path);
	ok &= expect(writeAudioRecordingReview(base, review, output).conflict && bytes(output.path) == updatedBytes,
	             "stale output revision cannot replace a newer review");
	output.expected = saved.identity;
	const auto cancelled = writeAudioRecordingReview(base, review, output, {[] { return true; }});
	ok &= expect(!cancelled.succeeded && bytes(output.path) == updatedBytes,
	             "cancelled save retains the committed review");
	const auto cancelledOpen = openAudioRecordingReview(base, output.path, {[] { return true; }});
	ok &= expect(cancelledOpen.cancelled && cancelledOpen.review.selections.isEmpty() &&
	                 !cancelledOpen.recording.planValid,
	             "cancelled open publishes no partial reviewed state");
	for (const auto &protectedPath : {QDir(folder).filePath("plan.json"), QDir(folder).filePath("result.json")}) {
		AudioProjectSaveRequest protect;
		protect.path = protectedPath;
		protect.overwrite = true;
		const auto before = bytes(protectedPath);
		ok &= expect(!writeAudioRecordingReview(base, review, protect).succeeded && bytes(protectedPath) == before,
		             "recording plan and receipt cannot become review outputs");
	}
	auto stale = review;
	stale.selections[0].expectedPrefixSha256 = QByteArray(32, 'x');
	ok &= expect(!writeAudioRecordingReview(base, stale, output).succeeded && bytes(output.path) == updatedBytes,
	             "stale source hash prevents a review save");
	auto missingTrack = base;
	missingTrack.tracks[0].id = "other";
	ok &= expect(!openAudioRecordingReview(missingTrack, output.path).succeeded(),
	             "saved targets must exist in the current session");
	AudioRecordingImportRequest sentinel = review;
	AudioProjectIdentity sentinelIdentity = saved.identity;
	ok &= expect(put(path("bad.json"), "{broken") &&
	                 !readAudioRecordingReview(path("bad.json"), &sentinel, &sentinelIdentity, &error) &&
	                 audioRecordingImportRequestJson(sentinel) == audioRecordingImportRequestJson(review) &&
	                 sentinelIdentity.sha256 == saved.identity.sha256,
	             "malformed JSON leaves caller selections and identity untouched");
	ok &= expect(put(path("oversized.json"), QByteArray(AudioRecordingReviewByteLimit + 1, ' ')) &&
	                 !readAudioRecordingReview(path("oversized.json"), &sentinel, nullptr, &error),
	             "review reads enforce the bounded file size");
	for (int version : {1, 2}) {
		auto legacy = review;
		legacy.comp = false;
		legacy.crossfadeFrames = 0;
		legacy.selections = {review.selections[0]};
		if (version == 2)
			legacy.selections << review.selections[1];
		AudioProjectSaveRequest target;
		target.path = path(version == 1 ? "v1.json" : "v2.json");
		const auto result = writeAudioRecordingReview(base, legacy, target);
		const auto reopened = openAudioRecordingReview(base, target.path);
		ok &= expect(result.succeeded && reopened.succeeded() &&
		                 QJsonDocument::fromJson(bytes(target.path)).object()["version"].toInt() == version &&
		                 audioRecordingImportRequestJson(reopened.review) == audioRecordingImportRequestJson(legacy),
		             "older review versions retain complete selection semantics");
	}
	// A moved bundle needs no current-directory assumptions or absolute journal path.
	ok &= expect(QDir().mkpath(path("moved/reviews")) && QDir().mkpath(path("moved/takes.vsrecord")),
	             "create moved bundle");
	for (const auto &name : {QString("plan.json"), QString("result.json"), QString("arm-01.vstake")})
		ok &= expect(QFile::copy(QDir(folder).filePath(name), QDir(path("moved/takes.vsrecord")).filePath(name)),
		             "copy independent fixture files");
	ok &= expect(QFile::copy(output.path, path("moved/reviews/comp.json")) &&
	                 openAudioRecordingReview(base, path("moved/reviews/comp.json")).succeeded(),
	             "moving the review and retained recording together preserves reopening");
	ok &= expect(bytes(journal) == originalJournal && encodeAudioSession(base) == baseBytes,
	             "saving and reopening never edit source journals or the session");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
