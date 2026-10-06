#include "core/audio_take_lanes.h"
#include "core/audio_range.h"
#include <QCoreApplication>
#include <QUuid>
#include <algorithm>

namespace vibestudio
{
namespace
{
QString text(const char *source) { return QCoreApplication::translate("AudioTakeLanes", source); }
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVector<AudioSessionRegion> slice(const QVector<AudioSessionRegion> &regions, qint64 first, qint64 end, qint64 position)
{
	QVector<AudioSessionRegion> result;
	for (const auto &region : regions) {
		const auto start = std::max(first, region.position);
		const auto stop = std::min(end, region.position + region.length);
		if (stop <= start)
			continue;
		auto cut = region;
		cut.id = id();
		cut.groupId.clear();
		cut.sourceOffset += start - region.position;
		cut.fadeSpan = region.fadeSpan ? region.fadeSpan : region.length;
		cut.fadeStart += start - region.position;
		cut.position = position + start - first;
		cut.length = stop - start;
		result.append(cut);
	}
	return result;
}
} // namespace
AudioTakeLaneResult editAudioTakeLane(const AudioSession &session, const AudioTakeLaneEdit &edit)
{
	AudioTakeLaneResult result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty())
		return result;
	const auto fail = [&](const char *source) {
		result.error = text(source);
		return result;
	};
	if (!QStringList{"capture", "rename", "remove", "promote"}.contains(edit.operation))
		return fail(QT_TRANSLATE_NOOP("AudioTakeLanes", "Choose capture, rename, remove or promote for take lanes."));
	AudioSession next = session;
	auto track = std::find_if(next.tracks.begin(), next.tracks.end(),
	                          [&](const auto &value) { return value.id == edit.trackId; });
	if (track == next.tracks.end() || track->routing.bus)
		return fail(QT_TRANSLATE_NOOP("AudioTakeLanes", "Choose an existing audio track for take lanes."));
	if ((edit.operation == "capture" || edit.operation == "rename") &&
	    (edit.name.trimmed().isEmpty() || edit.name.size() > 256 || !edit.name.isValidUtf16() ||
	     edit.name.contains(QChar(0))))
		return fail(QT_TRANSLATE_NOOP("AudioTakeLanes", "Choose a take lane name of 1–256 characters."));
	const bool ranged = edit.operation == "capture" || edit.operation == "promote";
	if (ranged && (edit.first < 0 || edit.end <= edit.first || edit.end > AudioSessionFrameLimit || edit.position < 0 ||
	               edit.position > AudioSessionFrameLimit - (edit.end - edit.first)))
		return fail(
		    QT_TRANSLATE_NOOP("AudioTakeLanes", "Choose a nonempty take range and placement within the timeline."));
	if (edit.operation == "capture") {
		AudioSessionTakeLane lane;
		lane.id = id();
		lane.name = edit.name;
		lane.regions = slice(track->regions, edit.first, edit.end, edit.first);
		if (lane.regions.isEmpty())
			return fail(QT_TRANSLATE_NOOP("AudioTakeLanes", "The selected range contains no clips to retain."));
		result.laneId = lane.id;
		track->takeLanes.append(lane);
	} else {
		auto lane = std::find_if(track->takeLanes.begin(), track->takeLanes.end(),
		                         [&](const auto &value) { return value.id == edit.laneId; });
		if (lane == track->takeLanes.end())
			return fail(QT_TRANSLATE_NOOP("AudioTakeLanes", "The selected take lane no longer exists."));
		result.laneId = lane->id;
		if (edit.operation == "rename")
			lane->name = edit.name;
		else if (edit.operation == "remove")
			track->takeLanes.erase(lane);
		else {
			const auto promoted = slice(lane->regions, edit.first, edit.end, edit.position);
			if (promoted.isEmpty())
				return fail(
				    QT_TRANSLATE_NOOP("AudioTakeLanes", "The selected take range contains no clips to promote."));
			if (edit.replaceExisting) {
				AudioRangeEdit clear;
				clear.operation = "clear";
				clear.trackIds = {edit.trackId};
				clear.first = edit.position;
				clear.end = edit.position + edit.end - edit.first;
				clear.followAutomation = false;
				const auto cleared = editAudioRange(next, clear);
				if (!cleared.succeeded()) {
					result.error = cleared.error;
					return result;
				}
				next = cleared.session;
				track = std::find_if(next.tracks.begin(), next.tracks.end(),
				                     [&](const auto &value) { return value.id == edit.trackId; });
			}
			for (const auto &region : promoted) {
				track->regions.append(region);
				result.regionIds.append(region.id);
			}
		}
	}
	result.error = validateAudioSessionStructure(next);
	if (result.succeeded())
		result.session = std::move(next);
	else {
		result.laneId.clear();
		result.regionIds.clear();
	}
	return result;
}
} // namespace vibestudio
