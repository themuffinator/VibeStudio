#include "core/audio_range.h"
#include "core/audio_arrangement.h"
#include <QCoreApplication>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace vibestudio
{
namespace
{
QString identifier() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// Copy sampled intervals, retaining the original interpolation domain at both
// cut boundaries. The next point can deliberately jump at a splice without
// changing the samples preceding it. No curve fitting or resampling is used.
bool transformLane(QVector<AudioAutomationPoint> *lane, const AudioRangeEdit &edit)
{
	if (lane->isEmpty())
		return true;
	const auto &source = *lane;
	QVector<AudioAutomationPoint> next;
	const auto append = [&](AudioAutomationPoint point, qint64 frame) {
		if (frame < 0 || frame > AudioAutomationFrameLimit || next.size() >= AudioAutomationPointLimit)
			return false;
		point.frame = frame;
		next.append(point);
		return true;
	};
	const auto interval = [&](qint64 first, qint64 end, qint64 position) {
		if (first == end)
			return true;
		if (!append(audioAutomationBoundary(source, first, 0), position))
			return false;
		for (const auto &point : source)
			if (point.frame > first && point.frame < end &&
			    !append(audioAutomationBoundary(source, point.frame, 0), position + point.frame - first))
				return false;
		return true;
	};
	const auto length = edit.end - edit.first;
	constexpr auto tail = AudioAutomationFrameLimit + 1;
	if (edit.operation == "ripple-delete") {
		if (!interval(0, edit.first, 0) || !interval(edit.end, tail, edit.first))
			return false;
	} else if (edit.operation == "insert-silence") {
		if (!interval(0, edit.first, 0) ||
		    !append({edit.first, audioAutomationValue(source, edit.first, 0), AudioAutomationCurve::Step},
		            edit.first) ||
		    !interval(edit.first, tail, edit.end))
			return false;
	} else if (edit.operation == "repeat") {
		if (!interval(0, edit.end, 0) || !interval(edit.first, edit.end, edit.end) ||
		    !interval(edit.end, tail, edit.end + length))
			return false;
	}
	if (!next.isEmpty())
		next.last().shape = {};
	*lane = std::move(next);
	return true;
}

AudioSessionRegion slice(const AudioSessionRegion &source, qint64 first, qint64 end, qint64 position)
{
	auto result = source;
	const auto offset = first - source.position;
	result.position = position;
	result.sourceOffset += offset;
	result.length = end - first;
	if (source.fadeSpan || ((offset || result.length != source.length) && (source.fadeIn || source.fadeOut))) {
		result.fadeSpan = source.fadeSpan ? source.fadeSpan : source.length;
		result.fadeStart += offset;
	}
	return result;
}
} // namespace

AudioRangeResult editAudioRange(const AudioSession &session, const AudioRangeEdit &edit)
{
	AudioRangeResult result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty())
		return result;
	const auto fail = [&](const QString &error) {
		result.error = error;
		return result;
	};
	if (!QStringList{"clear", "ripple-delete", "insert-silence", "repeat"}.contains(edit.operation))
		return fail(QCoreApplication::translate("AudioRange", "Unknown time-range operation."));
	if (edit.first < 0 || edit.end <= edit.first || edit.end > AudioSessionFrameLimit)
		return fail(QCoreApplication::translate("AudioRange", "Choose a nonempty range within the timeline."));
	const auto length = edit.end - edit.first;
	if (edit.operation == "repeat" && length > AudioSessionFrameLimit - edit.end)
		return fail(QCoreApplication::translate("AudioRange", "The repeated range exceeds the timeline limit."));
	QSet<QString> targets(edit.trackIds.cbegin(), edit.trackIds.cend());
	if ((edit.allTracks && !targets.isEmpty()) ||
	    (!edit.allTracks && (targets.isEmpty() || targets.size() != edit.trackIds.size())))
		return fail(QCoreApplication::translate("AudioRange", "Choose all tracks or distinct, existing tracks."));
	if (edit.masterAutomation && (!edit.allTracks || !edit.followAutomation || edit.operation == "clear"))
		return fail(QCoreApplication::translate(
		    "AudioRange", "Master automation requires an all-track time edit with automation following."));
	QStringList selected;
	for (const auto &track : session.tracks)
		if (edit.allTracks || targets.contains(track.id))
			selected.append(track.id);
	if (selected.isEmpty() || (!edit.allTracks && selected.size() != targets.size()))
		return fail(QCoreApplication::translate("AudioRange",
		                                        "A selected track no longer exists, or the session has no tracks."));
	targets = QSet<QString>(selected.cbegin(), selected.cend());
	AudioSession next = session;
	QHash<QString, QString> copiedGroups;
	QStringList added;
	for (auto &track : next.tracks) {
		if (!targets.contains(track.id))
			continue;
		QVector<AudioSessionRegion> regions;
		for (const auto &region : std::as_const(track.regions)) {
			bool retained = false;
			const auto part = [&](qint64 first, qint64 end, qint64 shift, bool copy = false) {
				first = std::max(first, region.position);
				end = std::min(end, region.position + region.length);
				if (end <= first)
					return;
				auto item = slice(region, first, end, first + shift);
				if (retained || copy) {
					item.id = identifier();
					added.append(item.id);
				}
				if (!copy)
					retained = true;
				else if (!item.groupId.isEmpty()) {
					if (!copiedGroups.contains(item.groupId))
						copiedGroups.insert(item.groupId, identifier());
					item.groupId = copiedGroups.value(item.groupId);
				}
				regions.append(std::move(item));
			};
			if (edit.operation == "clear" || edit.operation == "ripple-delete") {
				part(0, edit.first, 0);
				part(edit.end, AudioSessionFrameLimit, edit.operation == "clear" ? 0 : -length);
			} else {
				const auto at = edit.operation == "repeat" ? edit.end : edit.first;
				part(0, at, 0);
				part(at, AudioSessionFrameLimit, length);
				if (edit.operation == "repeat")
					part(edit.first, edit.end, length, true);
			}
		}
		track.regions = std::move(regions);
		if (edit.followAutomation && edit.operation != "clear") {
			bool valid = transformLane(&track.gainAutomation, edit) && transformLane(&track.panAutomation, edit);
			for (auto &lane : track.effectAutomation)
				valid = valid && transformLane(&lane.points, edit);
			if (!valid)
				return fail(QCoreApplication::translate(
				    "AudioRange",
				    "Following automation exceeds the timeline or point limit. The session is unchanged."));
		}
	}
	if (edit.masterAutomation)
		for (auto &lane : next.masterEffectAutomation)
			if (!transformLane(&lane.points, edit))
				return fail(QCoreApplication::translate(
				    "AudioRange",
				    "Following automation exceeds the timeline or point limit. The session is unchanged."));
	for (const auto &group : session.groups)
		if (copiedGroups.contains(group.id))
			next.groups.append({copiedGroups.value(group.id), group.name});
	pruneAudioSessionGroups(&next);
	result.error = validateAudioSessionStructure(next);
	if (result.error.isEmpty()) {
		result.session = std::move(next);
		result.trackIds = selected;
		result.addedRegionIds = added;
		result.first = edit.operation == "repeat" ? edit.end : edit.first;
		result.end = edit.operation == "ripple-delete" ? edit.first : result.first + length;
	}
	return result;
}
} // namespace vibestudio
