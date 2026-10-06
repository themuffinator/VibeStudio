#include "core/audio_recording.h"
#include "core/audio_recording_import.h"
#include "core/audio_recording_review.h"
#include "core/audio_session_io.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (argc != 2 || root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath("recording-cli-XXXXXX"));
	if (!temp.isValid())
		return EXIT_FAILURE;
	const auto path = [&](const char *name) { return QDir(temp.path()).filePath(QLatin1String(name)); };
	const auto folder = path("group.vsrecord");
	AudioRecordingPlan plan;
	plan.name = "CLI recording";
	plan.pass.punchEnd = 10;
	plan.pass.arms = {{"track", 1, {0, 1}}};
	QString error;
	QByteArray hash;
	bool ok = expect(createAudioRecordingFolder(folder, plan, &hash, &error), "CLI fixture plan");
	AudioTakeWriter writer;
	ok &= expect(writer.open(audioRecordingArmPath(folder, 0), audioRecordingArmMetadata(plan, 0), &error) &&
	                 writer.append(std::array<float, 3>{.25f, -.5f, 1.5f}, &error),
	             "CLI retained take fixture");
	writer.close();
	QJsonObject last;
	ok &= expect(QDir().mkpath(path("working-directory")), "create separate CLI working directory");
	const auto run = [&](const QStringList &options, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(path("working-directory"));
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file",
		                                                           path("settings.ini"), "asset", "audio-recording"} +
		                                                   options);
		if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
			process.kill();
			process.waitForFinished();
			return false;
		}
		const auto bytes = process.readAllStandardOutput();
		last = QJsonDocument::fromJson(bytes).object();
		const bool passed =
		    process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && !last.isEmpty();
		if (!passed)
			std::cerr << bytes.constData() << process.readAllStandardError().constData();
		return passed;
	};
	ok &= expect(run({"inspect", folder}) && last["recording"].toObject()["planValid"].toBool() &&
	                 !last["recording"].toObject()["receiptValid"].toBool() &&
	                 last["recording"].toObject()["takes"].toArray()[0].toObject()["frames"].toInteger() == 3,
	             "CLI inspection exposes missing receipt and actual retained prefix");
	AudioRecordingReceipt receipt;
	receipt.progress.state = AudioDuplexProgress::State::Stopped;
	receipt.progress.capturedFrames = 3;
	receipt.error = "Interrupted fixture";
	ok &= expect(finishAudioRecordingFolder(folder, hash, receipt, &error) && run({"inspect", folder}) &&
	                 last["recording"].toObject()["receiptMatches"].toBool(),
	             "CLI shares full receipt reconciliation");
	for (const auto &options : {QStringList{"inspect", folder, "--overwrite"}, QStringList{"inspect", folder, "--json"},
	                            QStringList{"inspect", folder, "--verbose=yes"}, QStringList{"export", folder},
	                            QStringList{"inspect", folder, "extra"}, QStringList{"inspect", folder, "--locale"}})
		ok &= expect(run(options, 2), "strict inspection options reject mutations, duplicates and malformed values");
	ok &= expect(run({"inspect", path("absent.vsrecord")}, 3) && !last["recording"].toObject()["planValid"].toBool(),
	             "missing plan reports a structured read failure");
	ok &= expect(inspectAudioRecording(folder).receiptMatches && !QFileInfo::exists(path("absent.vsrecord")),
	             "inspection never changes recordings or creates missing folders");
	AudioSession session;
	AudioSessionTrack track;
	track.id = "track";
	track.name = "Recorded voice";
	session.tracks = {track};
	AudioProjectSaveRequest save;
	save.path = path("base.vssession");
	ok &= expect(writeAudioSession(session, save).succeeded, "write CLI import destination fixture");
	AudioProjectIdentity identity;
	ok &= expect(readAudioSession(save.path, &session, &identity, &error), "read reviewed base session hash");
	const auto info = inspectAudioRecording(folder);
	AudioRecordingImportRequest review;
	review.directory = "group.vsrecord"; // Relative to the review file, independent of CLI working directory.
	review.expectedPlanSha256 = info.planSha256;
	review.expectedReceiptSha256 = info.receiptSha256;
	review.selections = {{0, info.takes[0].prefixSha256, 1, 3, 15, {0}, "track", false}};
	const auto reviewPath = path("review.json");
	const auto writeReview = [&] {
		QFile file(reviewPath);
		const auto bytes = QJsonDocument(audioRecordingImportRequestJson(review)).toJson();
		return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
	};
	const QStringList importing{"import", save.path, "--review", reviewPath, "--output", path("imported.vssession")};
	ok &= expect(writeReview() && run(importing, 3) && !QFileInfo::exists(path("imported.vssession")),
	             "CLI requires explicit interrupted pass acceptance before writing");
	review.allowInterrupted = true;
	ok &= expect(writeReview() && run(importing + QStringList{"--dry-run"}) && last["dryRun"].toBool() &&
	                 !last["written"].toBool() && !QFileInfo::exists(path("imported.vssession")),
	             "CLI validates full import without writing a dry-run output");
	ok &= expect(run(importing + QStringList{"--expected-session-sha256", QString(64, '0')}, 3) &&
	                 !QFileInfo::exists(path("imported.vssession")),
	             "stale reviewed session prevents import");
	ok &= expect(
	    run(importing + QStringList{"--expected-session-sha256", QString::fromLatin1(identity.sha256.toHex())}) &&
	        last["written"].toBool(),
	    "CLI commits reviewed import through guarded session writer");
	AudioSession imported;
	ok &= expect(readAudioSession(path("imported.vssession"), &imported, nullptr, &error) &&
	                 imported.sources.size() == 1 &&
	                 imported.sources[0].audio.clip.samples == QVector<float>{-.5f, 1.5f} &&
	                 imported.tracks[0].regions[0].position == 15,
	             "CLI imports exact selected frames at reviewed placement");
	ok &= expect(run(importing, 4), "CLI never silently overwrites an existing output");
	ok &= expect(run(importing + QStringList{"--overwrite"}), "explicit overwrite supports a reviewed separate output");
	ok &= expect(readAudioSession(path("imported.vssession"), &imported, nullptr, &error),
	             "retain the last committed output identity before testing failed replacement");
	const QStringList inPlace{"import", save.path, "--review", reviewPath, "--output", save.path};
	ok &= expect(run(inPlace, 2) && run(inPlace + QStringList{"--overwrite", "--expected-session-sha256",
	                                                          QString::fromLatin1(identity.sha256.toHex())}),
	             "in-place import requires overwrite and guards source identity");
	ok &= expect(run(inPlace + QStringList{"--overwrite", "--expected-session-sha256",
	                                       QString::fromLatin1(identity.sha256.toHex())},
	                 3),
	             "old source hash cannot replay an in-place import");
	review.selections[0].expectedPrefixSha256 = QByteArray(32, 'x');
	ok &= expect(writeReview() && run(importing + QStringList{"--overwrite"}, 3),
	             "stale take hash prevents output replacement");
	AudioSession retained;
	ok &= expect(readAudioSession(path("imported.vssession"), &retained, nullptr, &error) &&
	                 encodeAudioSession(retained) == encodeAudioSession(imported),
	             "failed review preserves previous output bytes");
	for (const auto &options :
	     {QStringList{"import", save.path}, importing + QStringList{"--review", reviewPath},
	      importing + QStringList{"--dry-run=true"}, importing + QStringList{"--expected-session-sha256", "bad"}})
		ok &= expect(run(options, 2), "CLI import rejects missing, duplicate and malformed options");
	{
		const auto loops = path("loops.vsrecord");
		plan.pass.punchEnd = 3;
		plan.pass.loopPasses = 2;
		ok &= expect(createAudioRecordingFolder(loops, plan, &hash, &error), "create CLI loop fixture");
		AudioTakeWriter loopWriter;
		ok &= expect(loopWriter.open(audioRecordingArmPath(loops, 0), audioRecordingArmMetadata(plan, 0), &error) &&
		                 loopWriter.append(std::array<float, 6>{1, 2, 3, 4, 5, 6}, &error) && loopWriter.finish(&error),
		             "write CLI loop journal");
		loopWriter.close();
		receipt = {};
		receipt.outcome = AudioRecordingReceipt::Outcome::Complete;
		receipt.progress.state = AudioDuplexProgress::State::Complete;
		receipt.progress.capturedFrames = 6;
		AudioRecordingInfo loopInfo;
		ok &= expect(finishAudioRecordingFolder(loops, hash, receipt, &error, &loopInfo) && run({"inspect", loops}) &&
		                 last["recording"].toObject()["plan"].toObject()["loopPasses"].toInt() == 2,
		             "CLI inspection reports the persisted loop count");
		review.directory = "loops.vsrecord";
		review.expectedPlanSha256 = loopInfo.planSha256;
		review.expectedReceiptSha256 = loopInfo.receiptSha256;
		review.selections = {{0, loopInfo.takes[0].prefixSha256, 1, 3, 1, {0}, "track", false, 1}};
		ok &= expect(writeReview() &&
		                 run({"import", save.path, "--review", reviewPath, "--output", path("loop-import.vssession")}),
		             "CLI imports a reviewed second pass through version-two review JSON");
		ok &= expect(readAudioSession(path("loop-import.vssession"), &retained, nullptr, &error) &&
		                 retained.sources.last().audio.clip.samples == QVector<float>{5, 6} &&
		                 retained.tracks[0].regions.last().position == 1,
		             "CLI and GUI share exact pass-local range placement");
		review.comp = true;
		review.crossfadeFrames = 2;
		review.selections = {{0, loopInfo.takes[0].prefixSha256, 0, 1, 30, {0}, "track", true, 0},
		                     {0, loopInfo.takes[0].prefixSha256, 1, 3, 31, {0}, "track", true, 1}};
		const QStringList compImport{"import", save.path, "--review", reviewPath, "--output", path("comp.vssession")};
		ok &= expect(writeReview() && run(compImport + QStringList{"--dry-run"}) &&
		                 !QFileInfo::exists(path("comp.vssession")) && run(compImport),
		             "CLI validates and commits version-three comp review through the existing import command");
		ok &= expect(readAudioSession(path("comp.vssession"), &retained, nullptr, &error) &&
		                 retained.sources[retained.sources.size() - 2].audio.clip.samples == QVector<float>{1, 2, 3} &&
		                 retained.sources.last().audio.clip.samples == QVector<float>{5, 6} &&
		                 retained.tracks[0].regions[retained.tracks[0].regions.size() - 2].fadeOut == 2 &&
		                 retained.tracks[0].regions.last().fadeIn == 2,
		             "CLI comp preserves exact handle audio and complementary region fades");
		const auto beforeComp = encodeAudioSession(retained);
		ok &= expect(QDir().mkpath(path("saved-reviews")), "create saved CLI review directory");
		const QStringList saveReview{"save-review", save.path,  "--review",
		                             reviewPath,    "--output", path("saved-reviews/comp.json")};
		ok &= expect(run(saveReview + QStringList{"--dry-run"}) && last["dryRun"].toBool() &&
		                 !QFileInfo::exists(path("saved-reviews/comp.json")) && run(saveReview) &&
		                 last["written"].toBool() && last["reviewSha256"].toString().size() == 64,
		             "CLI can verify and save an editable comp review without importing it");
		AudioRecordingImportRequest savedReview;
		ok &= expect(readAudioRecordingReview(path("saved-reviews/comp.json"), &savedReview, nullptr, &error) &&
		                 savedReview.directory == loops && savedReview.selections.size() == 2 &&
		                 savedReview.crossfadeFrames == 2 &&
		                 run({"preview", save.path, "--review", path("saved-reviews/comp.json"), "--output",
		                      path("saved-preview.wav")}),
		             "saved review rebases relative paths and feeds the existing preview command from another working "
		             "directory");
		ok &= expect(run(saveReview, 4) && run(saveReview + QStringList{"--overwrite"}) &&
		                 run(saveReview + QStringList{"--isolated"}, 2),
		             "saved review follows overwrite and strict option rules");
		const QStringList inPlaceReview{"save-review", save.path,
		                                "--review",    path("saved-reviews/comp.json"),
		                                "--output",    path("saved-reviews/comp.json")};
		ok &= expect(run(inPlaceReview, 2) && run(inPlaceReview + QStringList{"--overwrite"}) &&
		                 run(saveReview + QStringList{"--overwrite", "--expected-session-sha256", QString(64, '0')}, 3),
		             "review updates require explicit overwrite and honor the base-session revision guard");
		const QStringList preview{"preview", save.path, "--review", reviewPath, "--output", path("preview.wav")};
		ok &= expect(run(preview + QStringList{"--dry-run"}) && last["first"].toInteger() == 30 &&
		                 last["end"].toInteger() == 33 && !QFileInfo::exists(path("preview.wav")) && run(preview),
		             "recording preview shares reviewed timing and supports no-write verification");
		QFile previewFile(path("preview.wav"));
		ok &= expect(previewFile.open(QIODevice::ReadOnly), "open exported preview WAV");
		const auto previewBytes = previewFile.readAll();
		previewFile.close();
		const auto decoded = decodeAudioClip("preview.wav", previewBytes);
		const auto expectedPreview = renderAudioSession(retained, 30, 33);
		ok &= expect(decoded.succeeded() && decoded.clip.samples == expectedPreview.clip.samples &&
		                 last["includeBacking"].toBool() && last["frames"].toInteger() == 3,
		             "float32 CLI preview matches the committed comp render exactly");
		ok &= expect(run(preview, 4) && run(preview + QStringList{"--overwrite", "--isolated"}) &&
		                 !last["includeBacking"].toBool(),
		             "isolated preview uses normal guarded output rules");
		ok &=
		    expect(run(importing + QStringList{"--isolated"}, 2) && run(preview + QStringList{"--isolated=true"}, 2) &&
		               run(preview + QStringList{"--expected-session-sha256", QString(64, '0')}, 3),
		           "preview flags and reviewed session identity are strict");
		const QStringList protectedPreview{"preview",  save.path, "--review",   reviewPath,
		                                   "--output", save.path, "--overwrite"};
		ok &= expect(run(protectedPreview, 4), "preview cannot replace its source session");
		review.selections[1].expectedPrefixSha256 = QByteArray(32, 'x');
		ok &= expect(writeReview() && run(compImport + QStringList{"--overwrite"}, 3) &&
		                 readAudioSession(path("comp.vssession"), &retained, nullptr, &error) &&
		                 encodeAudioSession(retained) == beforeComp,
		             "a stale comp section hash preserves the previously committed output");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
