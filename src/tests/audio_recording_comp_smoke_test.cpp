#include "core/audio_recording_import.h"
#include "core/audio_session_io.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <algorithm>
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
QByteArray bytes(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
float sample(int pass, int frame, int channel) { return float((pass ? 32 : 4) + frame * 2 + channel) / 128; }
AudioSession baseSession()
{
	AudioSession base;
	base.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "backing";
	source.audio.clip = {2, 48000, QVector<float>(40, .125f)};
	source.audio.endFrame = 20;
	base.sources = {source};
	AudioSessionTrack track;
	track.id = track.name = "voice";
	track.regions = {{"original", "Backing", source.id, 0, 0, 20}};
	base.tracks = {track};
	return base;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath("recording-comp-XXXXXX"));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	const auto directory = QDir(temporary.path()).filePath("comp.vsrecord");
	AudioRecordingPlan plan;
	plan.name = "Comp fixture";
	plan.pass.punchFirst = 3;
	plan.pass.punchEnd = 15;
	plan.pass.loopPasses = 2;
	plan.pass.inputChannels = 2;
	plan.pass.arms = {{"voice", 2, {0, 1}}};
	QByteArray digest;
	QString error;
	bool ok = expect(createAudioRecordingFolder(directory, plan, &digest, &error), "create comp fixture");
	AudioTakeWriter writer;
	std::vector<float> samples;
	for (int pass = 0; pass < 2; ++pass)
		for (int frame = 0; frame < 12; ++frame)
			for (int channel = 0; channel < 2; ++channel)
				samples.push_back(sample(pass, frame, channel));
	const auto journal = audioRecordingArmPath(directory, 0);
	ok &= expect(writer.open(journal, audioRecordingArmMetadata(plan, 0), &error) && writer.append(samples, &error) &&
	                 writer.finish(&error),
	             "write two dry passes");
	writer.close();
	AudioRecordingReceipt receipt;
	receipt.outcome = AudioRecordingReceipt::Outcome::Complete;
	receipt.progress.state = AudioDuplexProgress::State::Complete;
	receipt.progress.capturedFrames = 24;
	AudioRecordingInfo info;
	ok &= expect(finishAudioRecordingFolder(directory, digest, receipt, &error, &info), "seal comp fixture");
	if (!ok)
		return EXIT_FAILURE;
	const auto originalJournal = bytes(journal);
	const auto originalReceipt = bytes(QDir(directory).filePath("result.json"));
	const auto base = baseSession();
	const auto originalSession = encodeAudioSession(base);
	AudioRecordingImportRequest request;
	request.directory = directory;
	request.expectedPlanSha256 = info.planSha256;
	request.expectedReceiptSha256 = info.receiptSha256;
	request.comp = true;
	request.crossfadeFrames = 3;
	request.selections = {{0, info.takes[0].prefixSha256, 0, 4, 3, {0, 1}, "voice", true, 0},
	                      {0, info.takes[0].prefixSha256, 4, 8, 7, {0, 1}, "voice", true, 1},
	                      {0, info.takes[0].prefixSha256, 8, 12, 11, {0, 1}, "voice", true, 0}};
	const auto prepared = prepareAudioRecordingImport(base, info, request);
	ok &= expect(prepared.succeeded() && prepared.slices[0].selection.end == 7 &&
	                 prepared.slices[1].selection.end == 11 && prepared.slices[2].selection.end == 12 &&
	                 prepared.slices[0].fadeOut == 3 && prepared.slices[1].fadeIn == 3 &&
	                 prepared.slices[1].fadeOut == 3 && prepared.slices[2].fadeIn == 3,
	             "adjacent cuts expand outgoing handles and create complementary fades");
	const auto imported = importAudioRecording(base, request);
	ok &= expect(imported.succeeded() && imported.session.sources.size() == 4 && imported.regionIds.size() == 3 &&
	                 imported.session.groups.size() == 1,
	             "repeat sections from the same pass import atomically");
	if (!imported.succeeded()) {
		std::cerr << imported.error.toStdString() << '\n';
		return EXIT_FAILURE;
	}
	const auto rendered = renderAudioSession(imported.session, 0, 20);
	QVector<float> expected;
	for (int frame = 0; frame < 20; ++frame) {
		const int local = frame - 3;
		for (int channel = 0; channel < 2; ++channel) {
			float value = .125f;
			if (local >= 0 && local < 12) {
				value = sample(local >= 4 && local < 8 ? 1 : 0, local, channel);
				if (local >= 4 && local <= 6) {
					const float w = float(local - 4) / 2;
					value = sample(0, local, channel) * (1 - w) + sample(1, local, channel) * w;
				} else if (local >= 8 && local <= 10) {
					const float w = float(local - 8) / 2;
					value = sample(1, local, channel) * (1 - w) + sample(0, local, channel) * w;
				}
			}
			expected << value;
		}
	}
	ok &= expect(rendered.succeeded() && rendered.clip.samples == expected,
	             "sample-exact independent oracle covers both crossfades and retained backing outside the comp");
	const auto provenance = imported.session.sources[2].audio.metadata;
	ok &= expect(provenance["takeFirstFrame"].toInteger() == 16 && provenance["takeEndFrame"].toInteger() == 23 &&
	                 provenance["recordingComp"].toObject()["end"].toInteger() == 8 &&
	                 provenance["recordingComp"].toObject()["loopPass"].toInt() == 2,
	             "provenance retains the reviewed cut and actual unwrapped handle interval");
	AudioSession restored;
	ok &= expect(decodeAudioSession(encodeAudioSession(imported.session), &restored, &error) &&
	                 encodeAudioSession(restored) == encodeAudioSession(imported.session) &&
	                 renderAudioSession(restored, 0, 20).clip.samples == expected,
	             "native session round trip preserves editable clips, grouping, fades, provenance and audio");
	AudioRecordingImportRequest parsed;
	{
		auto backing = base;
		auto room = backing.tracks[0];
		room.id = room.name = "room";
		room.regions[0].id = "room-backing";
		backing.tracks << room;
		const auto before = encodeAudioSession(backing);
		const auto context = prepareAudioRecordingAudition(backing, request);
		const auto isolated = prepareAudioRecordingAudition(backing, request, false);
		ok &= expect(context.succeeded() && isolated.succeeded() && context.first == 3 && context.end == 15 &&
		                 isolated.first == 3 && isolated.end == 15 && encodeAudioSession(backing) == before,
		             "audition prepares reviewed clips over their precise span without adopting the import");
		if (context.succeeded() && isolated.succeeded()) {
			auto onlyClips = expected.mid(6, 24);
			auto withBacking = onlyClips;
			for (auto &value : withBacking)
				value += .125f;
			ok &= expect(renderAudioSession(context.imported.session, 3, 15).clip.samples == withBacking &&
			                 renderAudioSession(isolated.imported.session, 3, 15).clip.samples == onlyClips &&
			                 validateAudioSessionStructure(isolated.imported.session).isEmpty(),
			             "context includes other tracks while isolated audition retains only selected clip samples");
		}
		backing.tracks[0].muted = true;
		backing.masterGainDb = -6;
		const auto muted = prepareAudioRecordingAudition(backing, request, false);
		ok &= expect(muted.succeeded() && muted.imported.session.masterGainDb == -6 &&
		                 muted.imported.session.tracks[0].muted &&
		                 renderAudioSession(muted.imported.session, 3, 15).clip.samples == QVector<float>(24, 0),
		             "isolated audition preserves mixer settings including deliberate mute");
		const auto stopped = prepareAudioRecordingAudition(backing, request, false, {[] { return true; }});
		ok &= expect(!stopped.succeeded() && stopped.imported.cancelled && stopped.imported.session.sources.isEmpty(),
		             "cancelled audition cannot expose a playable snapshot");
	}
	const auto json = audioRecordingImportRequestJson(request);
	ok &= expect(json["version"].toInt() == 3 && json["comp"].toBool() &&
	                 json["selections"].toArray()[1].toObject()["loopPass"].toInt() == 2 &&
	                 parseAudioRecordingImportRequest(json, &parsed, &error) &&
	                 audioRecordingImportRequestJson(parsed) == json,
	             "version-three review round trips comp intent and one-based pass selection");
	for (const auto &field : {QStringLiteral("comp"), QStringLiteral("crossfadeFrames"), QStringLiteral("version")}) {
		auto invalid = json;
		invalid.remove(field);
		ok &= expect(!parseAudioRecordingImportRequest(invalid, &parsed, &error),
		             "v3 review requires every schema field");
	}
	auto unordered = request;
	std::swap(unordered.selections[0], unordered.selections[2]);
	const auto reordered = importAudioRecording(base, unordered);
	ok &= expect(reordered.succeeded() && renderAudioSession(reordered.session, 0, 20).clip.samples == expected &&
	                 reordered.session.sources[1].audio.metadata["takeFirstFrame"].toInteger() == 8,
	             "planning sorts cuts per destination but preserves requested source order");
	auto gaps = request;
	gaps.selections[1].position = 20;
	gaps.selections[2].position = 30;
	const auto gapPlan = prepareAudioRecordingImport(base, info, gaps);
	ok &= expect(gapPlan.succeeded() && gapPlan.slices[0].fadeOut == 0 && gapPlan.slices[1].fadeIn == 0,
	             "disjoint sections remain unfaded");
	for (int fault = 0; fault < 10; ++fault) {
		auto invalid = request;
		switch (fault) {
		case 0:
			invalid.comp = false;
			invalid.crossfadeFrames = 0;
			break;
		case 1:
			invalid.selections[1].position = 6;
			break;
		case 2:
			invalid.crossfadeFrames = 5;
			break;
		case 3:
			invalid.selections[1].channels = {0};
			break;
		case 4:
			invalid.crossfadeFrames = 1;
			break;
		case 5:
			invalid.crossfadeFrames = -1;
			break;
		case 6:
			invalid.crossfadeFrames = std::numeric_limits<qint64>::max();
			break;
		case 7:
			invalid.selections[2].expectedPrefixSha256 = QByteArray(32, 'x');
			break;
		case 8:
			invalid.selections[0].first = 8;
			invalid.selections[0].end = 12;
			break;
		case 9:
			invalid.expectedReceiptSha256.clear();
			break;
		}
		const auto rejected = importAudioRecording(base, invalid);
		ok &= expect(!rejected.succeeded() && rejected.session.sources.isEmpty() && rejected.regionIds.isEmpty(),
		             "invalid comp, stale hashes and missing handles never publish partial imports");
	}
	auto partial = info;
	partial.takes[0].frames = 21;
	ok &= expect(!prepareAudioRecordingImport(base, partial, request).succeeded(),
	             "outgoing handle cannot cross the available prefix of a stopped final pass");
	auto hard = request;
	hard.crossfadeFrames = 0;
	const auto hardPlan = prepareAudioRecordingImport(base, info, hard);
	ok &= expect(hardPlan.succeeded() && hardPlan.slices[0].selection.end == 4 && hardPlan.slices[1].fadeIn == 0,
	             "zero crossfade keeps hard cuts exact");
	{
		auto full = base;
		AudioSessionSource reserve;
		reserve.audio.clip = {1, 48000, QVector<float>(AudioSampleLimit - 17, 0)};
		reserve.audio.endFrame = reserve.audio.clip.frameCount();
		for (int i = 0; i < 4; ++i) {
			reserve.id = QStringLiteral("reserve-%1").arg(i);
			full.sources.append(reserve); // Implicitly shared samples; independent source accounting.
		}
		ok &= expect(prepareAudioRecordingImport(full, info, hard).succeeded() &&
		                 !prepareAudioRecordingImport(full, info, request).succeeded(),
		             "aggregate sample budget includes crossfade handles before materialization");
	}
	{
		auto largeInfo = info;
		largeInfo.plan.pass.punchEnd = 3 + AudioSampleLimit / 2 + 2;
		largeInfo.takes[0].frames = 2 * (largeInfo.plan.pass.punchEnd - 3);
		auto large = request;
		large.crossfadeFrames = 2;
		large.selections.resize(2);
		large.selections[0].end = AudioSampleLimit / 2;
		large.selections[1].first = 0;
		large.selections[1].end = 2;
		large.selections[1].position = 3 + AudioSampleLimit / 2;
		ok &= expect(!prepareAudioRecordingImport(base, largeInfo, large).succeeded(),
		             "handles cannot expand one stereo source beyond its sample budget");
		large.crossfadeFrames = 0;
		ok &= expect(prepareAudioRecordingImport(base, largeInfo, large).succeeded(),
		             "an exactly bounded source remains valid with hard cuts");
	}
	{
		auto many = hard;
		many.selections.clear();
		for (int i = 0; i < AudioSessionSourceLimit - 1; ++i) {
			auto section = hard.selections[0];
			section.position = i * 10;
			many.selections << section;
		}
		ok &= expect(prepareAudioRecordingImport(base, info, many).succeeded(),
		             "repeated sections admit exactly the remaining source capacity");
		auto extra = many.selections.last();
		extra.position += 10;
		many.selections << extra;
		ok &= expect(!prepareAudioRecordingImport(base, info, many).succeeded(),
		             "preflight counts existing sources in the section limit");
	}
	{
		auto tracks = base;
		AudioSessionTrack other;
		other.id = other.name = "other";
		tracks.tracks << other;
		auto simultaneous = request;
		simultaneous.selections.resize(2);
		simultaneous.selections[1].trackId = other.id;
		simultaneous.selections[1].position = 3;
		const auto independent = prepareAudioRecordingImport(tracks, info, simultaneous);
		ok &= expect(independent.succeeded() && independent.slices[0].fadeOut == 0,
		             "separate target tracks can have simultaneous sections without crossfading each other");
	}
	int polls = 0;
	const auto cancelled = importAudioRecording(base, request, {[&] { return ++polls > 8; }});
	ok &= expect(cancelled.cancelled && cancelled.session.sources.isEmpty() && cancelled.regionIds.isEmpty(),
	             "cancellation during verification or assembly exposes no replacement snapshot");
	ok &=
	    expect(bytes(journal) == originalJournal && bytes(QDir(directory).filePath("result.json")) == originalReceipt &&
	               encodeAudioSession(base) == originalSession,
	           "comping retains all recording files and the original session");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
