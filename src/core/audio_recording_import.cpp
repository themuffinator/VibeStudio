#include "core/audio_recording_import.h"
#include "core/audio_arrangement.h"
#include "core/audio_range.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace vibestudio
{
namespace
{
QString importText(const char *source) { return QCoreApplication::translate("AudioRecordingImport", source); }
QString selectionError()
{
	return importText(QT_TRANSLATE_NOOP(
	    "AudioRecordingImport",
	    "Choose reviewed takes, valid frame/channel ranges and existing audio tracks within the session limits."));
}
bool interrupted(const AudioWorkControl &control) { return control.cancelled && control.cancelled(); }
bool integer(const QJsonObject &json, const char *key, qint64 *value, qint64 maximum = AudioSessionFrameLimit)
{
	const auto item = json.value(QLatin1String(key));
	const double number = item.toDouble(-1);
	if (!item.isDouble() || !std::isfinite(number) || number < 0 || number > double(maximum) ||
	    std::floor(number) != number)
		return false;
	*value = qint64(number);
	return true;
}
QByteArray hash(const QJsonValue &value)
{
	const auto text = value.toString();
	if (!QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(text).hasMatch())
		return {};
	return QByteArray::fromHex(text.toLatin1());
}
} // namespace

AudioRecordingImportPlan prepareAudioRecordingImport(const AudioSession &session, const AudioRecordingInfo &info,
                                                     const AudioRecordingImportRequest &request,
                                                     const AudioWorkControl &control)
{
	AudioRecordingImportPlan result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty())
		return result;
	if (interrupted(control)) {
		result.cancelled = true;
		return result;
	}
	if (request.selections.isEmpty() || request.selections.size() > AudioSessionSourceLimit ||
	    request.expectedPlanSha256.size() != 32 ||
	    (!request.expectedReceiptSha256.isEmpty() && request.expectedReceiptSha256.size() != 32) ||
	    request.crossfadeFrames < 0 || request.crossfadeFrames > AudioSampleLimit || request.crossfadeFrames == 1 ||
	    (!request.comp && request.crossfadeFrames != 0)) {
		result.error = selectionError();
		return result;
	}
	if (!info.planValid || !validateAudioRecordingPlan(info.plan).isEmpty() ||
	    info.takes.size() != info.plan.pass.arms.size() || info.planSha256 != request.expectedPlanSha256 ||
	    info.receiptSha256 != request.expectedReceiptSha256) {
		result.error = importText(QT_TRANSLATE_NOOP(
		    "AudioRecordingImport",
		    "The recording plan or final receipt changed. Inspect the folder and review the import again."));
		return result;
	}
	if (!request.allowInterrupted &&
	    (!info.receiptMatches || info.receipt.outcome == AudioRecordingReceipt::Outcome::Interrupted)) {
		result.error = importText(QT_TRANSLATE_NOOP(
		    "AudioRecordingImport",
		    "Explicitly accept the verified prefixes before importing an interrupted or unverified recording pass."));
		return result;
	}
	if (info.plan.sampleRate != session.sampleRate) {
		result.error = importText(QT_TRANSLATE_NOOP("AudioRecordingImport",
		                                            "The recording and session sample rates must match. Export "
		                                            "individual takes for explicit rate conversion."));
		return result;
	}
	qint64 samples = 0;
	for (const auto &source : session.sources)
		samples += source.audio.clip.samples.size();
	QSet<qint64> pairs;
	const auto passFrames = info.plan.pass.punchEnd - info.plan.pass.punchFirst;
	for (const auto &selection : request.selections) {
		const qint64 pair = qint64(selection.loopPass) * AudioDuplexArmLimit + selection.arm;
		if (selection.arm < 0 || selection.arm >= info.takes.size() || selection.loopPass < 0 ||
		    selection.loopPass >= info.plan.pass.loopPasses || (!request.comp && pairs.contains(pair))) {
			result.error = selectionError();
			return result;
		}
		pairs.insert(pair);
		const auto &take = info.takes[selection.arm];
		const auto track = std::find_if(session.tracks.cbegin(), session.tracks.cend(),
		                                [&](const auto &value) { return value.id == selection.trackId; });
		QSet<int> channels;
		for (int channel : selection.channels) {
			if (channel < 0 || channel >= take.metadata.channelMap.size() || channels.contains(channel)) {
				result.error = selectionError();
				return result;
			}
			channels.insert(channel);
		}
		if (!take.recoverable() || take.prefixSha256 != selection.expectedPrefixSha256 ||
		    (!take.complete && !request.allowInterrupted) || selection.first < 0 || selection.end <= selection.first ||
		    selection.end > take.frames - passFrames * selection.loopPass || selection.end > passFrames ||
		    selection.channels.isEmpty() || selection.channels.size() > 2 ||
		    selection.end - selection.first > AudioSampleLimit / selection.channels.size() || selection.position < 0 ||
		    selection.position > AudioSessionFrameLimit - (selection.end - selection.first) ||
		    track == session.tracks.cend() || track->routing.bus) {
			result.error = selectionError();
			return result;
		}
		result.slices.append({selection, 0, 0});
	}
	if (request.comp) {
		QVector<int> order(result.slices.size());
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [&](int a, int b) {
			const auto &x = result.slices[a].selection, &y = result.slices[b].selection;
			return x.trackId == y.trackId ? x.position < y.position : x.trackId < y.trackId;
		});
		for (qsizetype i = 1; i < order.size(); ++i) {
			auto &before = result.slices[order[i - 1]], &after = result.slices[order[i]];
			const auto &cutBefore = request.selections[order[i - 1]];
			if (before.selection.trackId != after.selection.trackId)
				continue;
			const auto boundary = cutBefore.position + cutBefore.end - cutBefore.first;
			if (after.selection.position < boundary) {
				result.error = importText(QT_TRANSLATE_NOOP(
				    "AudioRecordingImport",
				    "Comp sections on the same destination track must not overlap. Trim or remove a section."));
				return result;
			}
			if (after.selection.position != boundary || request.crossfadeFrames == 0)
				continue;
			const auto &take = info.takes[cutBefore.arm];
			const auto available = std::min(passFrames, take.frames - passFrames * cutBefore.loopPass);
			const auto fade = request.crossfadeFrames;
			if (fade > available - cutBefore.end || fade > after.selection.end - after.selection.first ||
			    before.selection.channels.size() != after.selection.channels.size()) {
				result.error = importText(QT_TRANSLATE_NOOP(
				    "AudioRecordingImport",
				    "A comp crossfade needs matching channel counts, enough audio after the outgoing cut and enough "
				    "frames in the next section. Shorten the crossfade or adjust the cuts."));
				return result;
			}
			before.selection.end += fade;
			before.fadeOut = after.fadeIn = fade;
		}
	}
	for (const auto &slice : result.slices) {
		const auto &selection = slice.selection;
		const qint64 count = (selection.end - selection.first) * selection.channels.size();
		if (selection.end - selection.first > AudioSampleLimit / selection.channels.size() ||
		    count > AudioSessionSampleLimit - samples) {
			result.error = selectionError();
			return result;
		}
		samples += count;
	}
	if (session.sources.size() + request.selections.size() > AudioSessionSourceLimit) {
		result.error = selectionError();
		return result;
	}
	return result;
}

AudioRecordingImportResult importAudioRecording(const AudioSession &session, const AudioRecordingImportRequest &request,
                                                const AudioWorkControl &control)
{
	AudioRecordingImportResult result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty())
		return result;
	result.recording = inspectAudioRecording(request.directory, control);
	const auto &info = result.recording;
	const auto plan = prepareAudioRecordingImport(session, info, request, control);
	if (!plan.succeeded()) {
		result.error = plan.error;
		result.cancelled = plan.cancelled;
		return result;
	}
	const auto passFrames = info.plan.pass.punchEnd - info.plan.pass.punchFirst;
	QVector<AudioProject> selected(plan.slices.size());
	// Each journal is scanned once for all its selected ranges. Never publish
	// any decoded prefix unless the entire batch matches its reviewed digest.
	for (int arm = 0; arm < info.takes.size(); ++arm) {
		QVector<AudioTakeReadRange> ranges;
		QVector<int> indices;
		for (int i = 0; i < plan.slices.size(); ++i) {
			const auto &selection = plan.slices[i].selection;
			if (selection.arm == arm) {
				ranges.append({passFrames * selection.loopPass + selection.first,
				               passFrames * selection.loopPass + selection.end, selection.channels});
				indices << i;
			}
		}
		if (ranges.isEmpty())
			continue;
		auto batch = readAudioTakeRanges(audioRecordingArmPath(info.directory, arm), info.takes[arm].prefixSha256,
		                                 ranges, request.allowInterrupted, control);
		if (!batch.succeeded()) {
			result.error = batch.error;
			result.cancelled = batch.info.cancelled || interrupted(control);
			return result;
		}
		for (qsizetype i = 0; i < indices.size(); ++i)
			selected[indices[i]] = std::move(batch.audio[i]);
	}
	AudioSession next = session;
	// Clear all requested old clip windows before adding anything. Multiple
	// arms mapped to one track cannot accidentally remove each other's import.
	for (const auto &slice : plan.slices) {
		const auto &selection = slice.selection;
		if (interrupted(control)) {
			result.cancelled = true;
			return result;
		}
		if (!selection.replaceExisting)
			continue;
		AudioRangeEdit clear;
		clear.operation = QStringLiteral("clear");
		clear.trackIds = {selection.trackId};
		clear.first = selection.position;
		clear.end = selection.position + selection.end - selection.first;
		clear.followAutomation = false;
		const auto edited = editAudioRange(next, clear);
		if (!edited.succeeded()) {
			result.error = edited.error;
			return result;
		}
		next = edited.session;
	}
	QStringList regions, sources, tracks;
	for (qsizetype i = 0; i < plan.slices.size(); ++i) {
		const auto &slice = plan.slices[i];
		const auto &selection = slice.selection;
		if (interrupted(control)) {
			result.cancelled = true;
			return result;
		}
		auto &audio = selected[i];
		if (info.plan.pass.loopPasses > 1)
			audio.sourceName = importText(QT_TRANSLATE_NOOP("AudioRecordingImport", "%1 · Pass %2"))
			                       .arg(audio.sourceName)
			                       .arg(selection.loopPass + 1);
		if (request.comp) {
			const auto &cut = request.selections[i];
			audio.metadata.insert("recordingComp",
			                      QJsonObject{{"arm", cut.arm + 1},
			                                  {"loopPass", cut.loopPass + 1},
			                                  {"first", cut.first},
			                                  {"end", cut.end},
			                                  {"position", cut.position},
			                                  {"fadeIn", slice.fadeIn},
			                                  {"fadeOut", slice.fadeOut},
			                                  {"planSha256", QString::fromLatin1(info.planSha256.toHex())}});
		}
		const auto imported =
		    importAudioSessionSource(next, audio, selection.trackId, selection.position, false, control);
		if (!imported.succeeded()) {
			result.error = imported.error;
			result.cancelled = imported.cancelled;
			return result;
		}
		next = imported.session;
		for (auto &track : next.tracks)
			if (track.id == selection.trackId)
				for (auto &region : track.regions)
					if (region.id == imported.addedRegionId) {
						region.fadeIn = slice.fadeIn;
						region.fadeOut = slice.fadeOut;
					}
		regions << imported.addedRegionId;
		sources << next.sources.last().id;
		tracks << selection.trackId;
	}
	QString group;
	if (request.groupRegions && regions.size() > 1) {
		AudioArrangementEdit link;
		link.operation = QStringLiteral("group");
		link.regionIds = regions;
		link.name = info.plan.name;
		const auto grouped = editAudioArrangement(next, link);
		if (!grouped.succeeded()) {
			result.error = grouped.error;
			return result;
		}
		next = grouped.session;
		group = grouped.groupId;
	}
	if (interrupted(control)) {
		result.cancelled = true;
		return result;
	}
	result.session = std::move(next);
	result.regionIds = regions;
	result.sourceIds = sources;
	result.trackIds = tracks;
	result.groupId = group;
	return result;
}

AudioRecordingAuditionResult prepareAudioRecordingAudition(const AudioSession &session,
                                                           const AudioRecordingImportRequest &request,
                                                           bool includeBacking, const AudioWorkControl &control)
{
	AudioRecordingAuditionResult result;
	result.imported = importAudioRecording(session, request, control);
	if (!result.imported.succeeded())
		return result;
	const QSet<QString> selected(result.imported.regionIds.cbegin(), result.imported.regionIds.cend());
	qint64 first = AudioSessionFrameLimit, end = 0;
	for (auto &track : result.imported.session.tracks) {
		for (const auto &region : track.regions)
			if (selected.contains(region.id)) {
				first = std::min(first, region.position);
				end = std::max(end, region.position + region.length);
			}
		if (!includeBacking) {
			track.regions.erase(std::remove_if(track.regions.begin(), track.regions.end(),
			                                   [&](const auto &region) { return !selected.contains(region.id); }),
			                    track.regions.end());
			for (auto &region : track.regions)
				region.groupId.clear();
		}
	}
	if (!includeBacking) {
		result.imported.session.groups.clear();
		result.imported.groupId.clear();
	}
	if (interrupted(control)) {
		result.imported.session = {};
		result.imported.regionIds.clear();
		result.imported.sourceIds.clear();
		result.imported.trackIds.clear();
		result.imported.groupId.clear();
		result.imported.cancelled = true;
		return result;
	}
	result.first = first;
	result.end = end;
	return result;
}

QJsonObject audioRecordingImportRequestJson(const AudioRecordingImportRequest &request)
{
	QJsonArray selections;
	const bool looped = request.comp || std::any_of(request.selections.cbegin(), request.selections.cend(),
	                                                [](const auto &selection) { return selection.loopPass != 0; });
	for (const auto &selection : request.selections) {
		QJsonArray channels;
		for (int channel : selection.channels)
			channels.append(qint64(channel) + 1);
		QJsonObject item{{"arm", qint64(selection.arm) + 1},
		                 {"prefixSha256", QString::fromLatin1(selection.expectedPrefixSha256.toHex())},
		                 {"first", selection.first},
		                 {"end", selection.end},
		                 {"channels", channels},
		                 {"trackId", selection.trackId},
		                 {"position", selection.position},
		                 {"replaceExisting", selection.replaceExisting}};
		if (looped)
			item.insert("loopPass", qint64(selection.loopPass) + 1);
		selections.append(item);
	}
	QJsonObject result{{"format", "VibeStudioRecordingImport"},
	                   {"version", request.comp ? 3
	                               : looped     ? 2
	                                            : 1},
	                   {"directory", request.directory},
	                   {"planSha256", QString::fromLatin1(request.expectedPlanSha256.toHex())},
	                   {"receiptSha256", QString::fromLatin1(request.expectedReceiptSha256.toHex())},
	                   {"allowInterrupted", request.allowInterrupted},
	                   {"groupRegions", request.groupRegions},
	                   {"selections", selections}};
	if (request.comp) {
		result.insert("comp", true);
		result.insert("crossfadeFrames", request.crossfadeFrames);
	}
	return result;
}
bool parseAudioRecordingImportRequest(const QJsonObject &json, AudioRecordingImportRequest *request, QString *error)
{
	const auto fail = [&] {
		if (error)
			*error = selectionError();
		return false;
	};
	if (!request)
		return fail();
	AudioRecordingImportRequest parsed;
	parsed.directory = json.value("directory").toString();
	parsed.expectedPlanSha256 = hash(json.value("planSha256"));
	parsed.expectedReceiptSha256 = hash(json.value("receiptSha256"));
	parsed.allowInterrupted = json.value("allowInterrupted").toBool();
	parsed.groupRegions = json.value("groupRegions").toBool();
	if (parsed.directory.isEmpty() || parsed.directory.size() > 8192 || !parsed.directory.isValidUtf16() ||
	    parsed.directory.contains(QChar(0)) || parsed.expectedPlanSha256.size() != 32)
		return fail();
	const auto selections = json.value("selections").toArray();
	parsed.comp = json.value("version").toInt() == 3;
	const bool looped = parsed.comp || json.value("version").toInt() == 2;
	if (parsed.comp && !integer(json, "crossfadeFrames", &parsed.crossfadeFrames, AudioSampleLimit))
		return fail();
	if (selections.isEmpty() || selections.size() > (looped ? AudioSessionSourceLimit : AudioDuplexArmLimit))
		return fail();
	for (const auto &value : selections) {
		AudioRecordingSelection selection;
		const auto item = value.toObject();
		if (looped) {
			qint64 pass;
			if (!integer(item, "loopPass", &pass, AudioDuplexLoopPassLimit) || pass < 1)
				return fail();
			selection.loopPass = int(pass - 1);
		}
		qint64 arm;
		if (!integer(item, "arm", &arm, AudioDuplexArmLimit) || arm < 1 || !integer(item, "first", &selection.first) ||
		    !integer(item, "end", &selection.end) || !integer(item, "position", &selection.position))
			return fail();
		selection.arm = int(arm - 1);
		selection.expectedPrefixSha256 = hash(item.value("prefixSha256"));
		selection.trackId = item.value("trackId").toString();
		selection.replaceExisting = item.value("replaceExisting").toBool();
		if (selection.expectedPrefixSha256.size() != 32 || selection.trackId.isEmpty() ||
		    selection.trackId.size() > 64 || !selection.trackId.isValidUtf16() || selection.trackId.contains(QChar(0)))
			return fail();
		const auto channels = item.value("channels").toArray();
		if (channels.isEmpty() || channels.size() > 2)
			return fail();
		for (const auto &channel : channels) {
			const auto number = channel.toDouble(-1);
			if (number != 1 && number != 2)
				return fail();
			selection.channels << int(number - 1);
		}
		parsed.selections.append(selection);
	}
	if (audioRecordingImportRequestJson(parsed) != json)
		return fail();
	*request = std::move(parsed);
	if (error)
		error->clear();
	return true;
}
QJsonObject audioRecordingImportResultJson(const AudioRecordingImportResult &result)
{
	return {{"recording", audioRecordingInfoJson(result.recording)},
	        {"error", result.error},
	        {"cancelled", result.cancelled},
	        {"groupId", result.groupId},
	        {"regionIds", QJsonArray::fromStringList(result.regionIds)},
	        {"sourceIds", QJsonArray::fromStringList(result.sourceIds)},
	        {"trackIds", QJsonArray::fromStringList(result.trackIds)}};
}
QStringList audioRecordingProtectedPaths(const QString &directory)
{
	const QDir folder(directory);
	QStringList paths{folder.absoluteFilePath("plan.json"), folder.absoluteFilePath("result.json")};
	for (int arm = 0; arm < AudioDuplexArmLimit; ++arm)
		paths << audioRecordingArmPath(folder.absolutePath(), arm);
	return paths;
}
} // namespace vibestudio
