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
} // namespace

QStringList audioArrangementSelection(const AudioSession &session, const QStringList &ids, bool linkedGroups,
                                      QString *error)
{
	if (error)
		error->clear();
	QSet<QString> wanted(ids.cbegin(), ids.cend()), found, groups;
	if (ids.isEmpty() || ids.size() > AudioSessionRegionLimit || wanted.size() != ids.size()) {
		if (error)
			*error = QCoreApplication::translate("AudioArrangement",
			                                     "Select distinct, existing clips for the arrangement edit.");
		return {};
	}
	for (const auto &track : session.tracks)
		for (const auto &region : track.regions)
			if (wanted.contains(region.id)) {
				found.insert(region.id);
				if (linkedGroups && !region.groupId.isEmpty())
					groups.insert(region.groupId);
			}
	if (found.size() != wanted.size()) {
		if (error)
			*error = QCoreApplication::translate("AudioArrangement", "A selected clip no longer exists.");
		return {};
	}
	QStringList result;
	for (const auto &track : session.tracks)
		for (const auto &region : track.regions)
			if (wanted.contains(region.id) || groups.contains(region.groupId))
				result.append(region.id);
	return result;
}

void pruneAudioSessionGroups(AudioSession *session)
{
	QHash<QString, int> counts;
	for (const auto &track : std::as_const(session->tracks))
		for (const auto &region : track.regions)
			if (!region.groupId.isEmpty())
				++counts[region.groupId];
	for (auto &track : session->tracks)
		for (auto &region : track.regions)
			if (!region.groupId.isEmpty() && counts.value(region.groupId) < 2)
				region.groupId.clear();
	session->groups.erase(std::remove_if(session->groups.begin(), session->groups.end(),
	                                     [&](const auto &group) { return counts.value(group.id) < 2; }),
	                      session->groups.end());
}

AudioArrangementResult editAudioArrangement(const AudioSession &session, const AudioArrangementEdit &edit)
{
	AudioArrangementResult result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty())
		return result;
	const auto selected = audioArrangementSelection(session, edit.regionIds, edit.linkedGroups, &result.error);
	if (!result.error.isEmpty())
		return result;
	const QStringList operations{"move", "duplicate", "remove", "split", "group", "ungroup", "gain", "fades", "mute"};
	if (!operations.contains(edit.operation)) {
		result.error = QCoreApplication::translate("AudioArrangement", "Unknown arrangement edit operation.");
		return result;
	}
	if ((edit.operation == "move" || edit.operation == "duplicate") &&
	    (edit.offset < -AudioSessionFrameLimit || edit.offset > AudioSessionFrameLimit)) {
		result.error = QCoreApplication::translate("AudioArrangement", "The movement exceeds the timeline limit.");
		return result;
	}
	if (edit.operation == "split" && (edit.position < 0 || edit.position > AudioSessionFrameLimit)) {
		result.error = QCoreApplication::translate("AudioArrangement", "The split cursor is outside the timeline.");
		return result;
	}
	const QSet<QString> targets(selected.cbegin(), selected.cend());
	AudioSession next = session;
	QString groupId;
	if (edit.operation == "group") {
		if (selected.size() < 2) {
			result.error = QCoreApplication::translate("AudioArrangement", "A clip group needs at least two clips.");
			return result;
		}
		// Renaming an exact existing group keeps its identity. A partial selection
		// with linking off creates a new group, leaving the rest independent.
		for (const auto &group : session.groups) {
			QSet<QString> members;
			for (const auto &track : session.tracks)
				for (const auto &region : track.regions)
					if (region.groupId == group.id)
						members.insert(region.id);
			if (members == targets) {
				groupId = group.id;
				break;
			}
		}
		if (groupId.isEmpty()) {
			groupId = identifier();
			next.groups.append({groupId, edit.name});
		} else
			for (auto &group : next.groups)
				if (group.id == groupId)
					group.name = edit.name;
	}
	QHash<QString, QString> copiedGroups;
	QStringList added;
	for (auto &track : next.tracks) {
		QVector<AudioSessionRegion> copies;
		for (auto &region : track.regions) {
			if (!targets.contains(region.id))
				continue;
			if (edit.operation == "move" || edit.operation == "duplicate") {
				if (edit.offset < -region.position ||
				    edit.offset > AudioSessionFrameLimit - region.position - region.length) {
					result.error = QCoreApplication::translate(
					    "AudioArrangement",
					    "Moving this selection would put a clip outside the timeline. No clips were changed.");
					return result;
				}
				if (edit.operation == "move")
					region.position += edit.offset;
				else {
					auto copy = region;
					copy.id = identifier();
					copy.position += edit.offset;
					copies.append(copy);
				}
			} else if (edit.operation == "split" && edit.position > region.position &&
			           edit.position < region.position + region.length) {
				const auto leftLength = edit.position - region.position;
				if (region.fadeIn || region.fadeOut || region.fadeSpan)
					region.fadeSpan = region.fadeSpan ? region.fadeSpan : region.length;
				auto right = region;
				right.id = identifier();
				right.position = edit.position;
				right.sourceOffset += leftLength;
				right.length -= leftLength;
				if (right.fadeSpan)
					right.fadeStart += leftLength;
				region.length = leftLength;
				copies.append(right);
			} else if (edit.operation == "gain")
				region.gainDb = edit.gainDb;
			else if (edit.operation == "fades") {
				region.fadeIn = edit.fadeIn;
				region.fadeOut = edit.fadeOut;
				region.fadeStart = region.fadeSpan = 0;
			} else if (edit.operation == "mute")
				region.muted = edit.muted;
			else if (edit.operation == "group")
				region.groupId = groupId;
			else if (edit.operation == "ungroup")
				region.groupId.clear();
		}
		for (auto &copy : copies) {
			added.append(copy.id);
			if (!copy.groupId.isEmpty()) {
				if (!copiedGroups.contains(copy.groupId))
					copiedGroups.insert(copy.groupId, identifier());
				copy.groupId = copiedGroups.value(copy.groupId);
			}
		}
		track.regions.append(copies);
		if (edit.operation == "remove")
			track.regions.erase(std::remove_if(track.regions.begin(), track.regions.end(),
			                                   [&](const auto &region) { return targets.contains(region.id); }),
			                    track.regions.end());
	}
	for (const auto &group : session.groups)
		if (copiedGroups.contains(group.id))
			next.groups.append({copiedGroups.value(group.id), group.name});
	if (edit.operation == "split" && added.isEmpty()) {
		result.error = QCoreApplication::translate(
		    "AudioArrangement", "The split cursor must be strictly inside at least one selected clip.");
		return result;
	}
	pruneAudioSessionGroups(&next);
	result.error = validateAudioSessionStructure(next);
	if (result.error.isEmpty()) {
		result.session = std::move(next);
		result.addedRegionIds = added;
		result.selectedRegionIds = edit.operation == "remove" ? QStringList{} : added.isEmpty() ? selected : added;
		result.groupId = groupId;
	}
	return result;
}
} // namespace vibestudio
