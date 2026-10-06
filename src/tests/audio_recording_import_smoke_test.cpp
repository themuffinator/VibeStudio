#include "core/audio_recording_import.h"
#include "core/audio_session_io.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
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
AudioSession session()
{
	AudioSession s;
	s.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "backing";
	source.audio.clip = {1, 48000, QVector<float>(20, .125f)};
	source.audio.endFrame = 20;
	s.sources = {source};
	for (int i = 0; i < 2; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		track.regions = {{"clip" + track.id, "Backing", source.id, 0, 0, 20}};
		s.tracks << track;
	}
	return s;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temp(QDir(root).filePath("recording-import-XXXXXX"));
	if (!temp.isValid())
		return EXIT_FAILURE;
	const auto directory = QDir(temp.path()).filePath("fixture.vsrecord");
	AudioRecordingPlan plan;
	plan.name = "Grouped take";
	plan.pass.punchFirst = 5;
	plan.pass.punchEnd = 9;
	plan.pass.inputChannels = 4;
	plan.pass.arms = {{"0", 2, {3, 1}}, {"1", 1, {2, 0}}};
	QByteArray digest;
	QString error;
	bool ok = expect(createAudioRecordingFolder(directory, plan, &digest, &error), "create grouped import fixture");
	for (int i = 0; i < 2; ++i) {
		AudioTakeWriter writer;
		const std::vector<float> samples =
		    i ? std::vector<float>{.11f, .12f, .13f, .14f} : std::vector<float>{1, 2, 3, 4, 5, 6, 7, 8};
		ok &= expect(writer.open(audioRecordingArmPath(directory, i), audioRecordingArmMetadata(plan, i), &error) &&
		                 writer.append(samples, &error) && writer.finish(&error),
		             "write grouped import arms");
	}
	AudioRecordingReceipt receipt;
	receipt.outcome = AudioRecordingReceipt::Outcome::Complete;
	receipt.progress.state = AudioDuplexProgress::State::Complete;
	receipt.progress.capturedFrames = 4;
	AudioRecordingInfo info;
	ok &= expect(finishAudioRecordingFolder(directory, digest, receipt, &error, &info), "seal grouped fixture");
	AudioRecordingImportRequest request;
	request.directory = directory;
	request.expectedPlanSha256 = info.planSha256;
	request.expectedReceiptSha256 = info.receiptSha256;
	request.selections = {{0, info.takes[0].prefixSha256, 1, 4, 6, {1, 0}, "0", true},
	                      {1, info.takes[1].prefixSha256, 0, 2, 10, {0}, "1", false}};
	const auto original = session();
	const auto before = encodeAudioSession(original);
	const auto imported = importAudioRecording(original, request);
	ok &= expect(imported.succeeded() && imported.session.sources.size() == 3 && imported.regionIds.size() == 2 &&
	                 imported.session.groups.size() == 1 && !imported.groupId.isEmpty() &&
	                 encodeAudioSession(original) == before,
	             "selected arms become one grouped replacement snapshot without mutating original");
	if (imported.succeeded()) {
		ok &=
		    expect(imported.session.sources[1].audio.clip.samples == QVector<float>{4, 3, 6, 5, 8, 7} &&
		               imported.session.sources[2].audio.clip.samples == QVector<float>{.11f, .12f} &&
		               imported.session.tracks[0].regions.size() == 3 && imported.session.tracks[1].regions.size() == 2,
		           "reviewed subranges and stored-channel order are exact; only requested old window is replaced");
		const auto &track = imported.session.tracks[0];
		ok &= expect(track.regions[0].position == 0 && track.regions[0].length == 6 && track.regions[1].position == 9 &&
		                 track.regions[1].sourceOffset == 9 && track.regions[1].length == 11 &&
		                 track.regions[2].position == 6 && track.regions[2].length == 3,
		             "punch replacement preserves exact left and right source windows");
	}
	auto sameTrack = request;
	sameTrack.selections[1].trackId = "0";
	sameTrack.selections[1].position = 6;
	sameTrack.selections[1].replaceExisting = true;
	const auto layered = importAudioRecording(original, sameTrack);
	ok &= expect(layered.succeeded() && layered.session.tracks[0].regions.size() == 4 && layered.regionIds.size() == 2,
	             "all original clip clears precede imports when two arms share a destination");
	AudioRecordingImportRequest parsed;
	const auto json = audioRecordingImportRequestJson(request);
	ok &= expect(parseAudioRecordingImportRequest(json, &parsed, &error) &&
	                 audioRecordingImportRequestJson(parsed) == json &&
	                 json["selections"].toArray()[0].toObject()["channels"].toArray() == QJsonArray{2, 1},
	             "review JSON uses one-based arm/channel choices and round trips");
	auto unknown = json;
	unknown["extra"] = true;
	ok &= expect(!parseAudioRecordingImportRequest(unknown, &parsed, &error), "unknown review fields rejected");
	for (int fault = 0; fault < 10; ++fault) {
		auto invalid = request;
		switch (fault) {
		case 0:
			invalid.expectedPlanSha256 = QByteArray(32, 'x');
			break;
		case 1:
			invalid.expectedReceiptSha256.clear();
			break;
		case 2:
			invalid.selections[1].expectedPrefixSha256 = QByteArray(32, 'x');
			break;
		case 3:
			invalid.selections[1].arm = 0;
			break;
		case 4:
			invalid.selections[1].trackId = "missing";
			break;
		case 5:
			invalid.selections[0].channels = {0, 0};
			break;
		case 6:
			invalid.selections[0].position = std::numeric_limits<qint64>::max();
			break;
		case 7:
			invalid.selections[0].first = std::numeric_limits<qint64>::min();
			break;
		case 8:
			invalid.selections[0].end = 5;
			break;
		case 9:
			invalid.selections[1].arm = std::numeric_limits<int>::max();
			break;
		}
		const auto rejected = importAudioRecording(original, invalid);
		ok &= expect(!rejected.succeeded() && rejected.session.sources.isEmpty() && rejected.regionIds.isEmpty() &&
		                 encodeAudioSession(original) == before,
		             "invalid review never exposes a partial import or alters source session");
	}
	const auto cancelled = importAudioRecording(original, request, {[] { return true; }});
	ok &= expect(cancelled.cancelled && cancelled.session.sources.isEmpty(),
	             "cancelled import has no replacement snapshot");
	ok &= expect(QFile::remove(QDir(directory).filePath("result.json")), "remove owned receipt fixture");
	ok &= expect(!importAudioRecording(original, request).succeeded(), "receipt identity change requires new review");
	request.expectedReceiptSha256.clear();
	ok &= expect(!importAudioRecording(original, request).succeeded(),
	             "missing final receipt requires explicit interrupted-pass acceptance");
	request.allowInterrupted = true;
	ok &= expect(importAudioRecording(original, request).succeeded(),
	             "accepted independently verified prefixes remain recoverable without a receipt");
	{
		const auto loops = QDir(temp.path()).filePath("loops.vsrecord");
		plan.pass.loopPasses = 3;
		ok &= expect(createAudioRecordingFolder(loops, plan, &digest, &error), "create version-two loop plan");
		for (int arm = 0; arm < 2; ++arm) {
			AudioTakeWriter writer;
			std::vector<float> samples;
			for (int frame = 0; frame < 10; ++frame)
				for (int channel = 0; channel < plan.pass.arms[arm].channels; ++channel)
					samples.push_back(float(frame * 10 + channel));
			ok &= expect(writer.open(audioRecordingArmPath(loops, arm), audioRecordingArmMetadata(plan, arm), &error) &&
			                 writer.append(samples, &error) && writer.finish(&error),
			             "write two full loop passes and a partial third");
		}
		receipt.outcome = AudioRecordingReceipt::Outcome::Stopped;
		receipt.progress.state = AudioDuplexProgress::State::Stopped;
		receipt.progress.capturedFrames = 10;
		ok &= expect(finishAudioRecordingFolder(loops, digest, receipt, &error, &info),
		             "clean Stop seals partial final loop pass");
		info = inspectAudioRecording(loops);
		ok &= expect(info.receiptMatches && info.plan.pass.loopPasses == 3 &&
		                 audioRecordingInfoJson(info)["plan"].toObject()["version"].toInt() == 2,
		             "loop count persists and receipt reconciles total frames across passes");
		AudioRecordingImportRequest chosen;
		chosen.directory = loops;
		chosen.expectedPlanSha256 = info.planSha256;
		chosen.expectedReceiptSha256 = info.receiptSha256;
		chosen.selections = {{0, info.takes[0].prefixSha256, 1, 4, 6, {1, 0}, "0", true, 1},
		                     {0, info.takes[0].prefixSha256, 0, 2, 5, {0}, "1", false, 2}};
		const auto selected = importAudioRecording(original, chosen);
		ok &= expect(selected.succeeded() && selected.session.sources.size() == 3 &&
		                 selected.session.sources[1].audio.clip.samples == QVector<float>{51, 50, 61, 60, 71, 70} &&
		                 selected.session.sources[2].audio.clip.samples == QVector<float>{80, 90} &&
		                 selected.session.tracks[1].regions.last().position == 5 && selected.session.groups.size() == 1,
		             "distinct passes from one arm import exact local ranges and retain reviewed timeline placement");
		const auto loopJson = audioRecordingImportRequestJson(chosen);
		ok &= expect(loopJson["version"].toInt() == 2 && parseAudioRecordingImportRequest(loopJson, &parsed, &error) &&
		                 parsed.selections[0].loopPass == 1 && parsed.selections[1].loopPass == 2,
		             "version-two review JSON preserves one-based pass choices");
		for (int invalidCase = 0; invalidCase < 5; ++invalidCase) {
			auto invalid = chosen;
			if (invalidCase == 0)
				invalid.selections[1].loopPass = 1;
			else if (invalidCase == 1)
				invalid.selections[1].end = 3;
			else if (invalidCase == 2)
				invalid.selections[0].loopPass = 3;
			else if (invalidCase == 3)
				invalid.selections[0].loopPass = -1;
			else
				invalid.selections[0].loopPass = std::numeric_limits<int>::max();
			const auto rejected = importAudioRecording(original, invalid);
			ok &= expect(!rejected.succeeded() && rejected.session.sources.isEmpty() &&
			                 encodeAudioSession(original) == before,
			             "duplicate, unavailable and partial-pass overreads reject the entire import");
		}
		auto unknown = loopJson;
		unknown["version"] = 4;
		ok &=
		    expect(!parseAudioRecordingImportRequest(unknown, &parsed, &error), "future review versions are rejected");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
