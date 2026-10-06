#pragma once
#include "core/audio_session.h"
#include <QStringList>

namespace vibestudio
{
struct AudioArrangementEdit {
	// move, duplicate, remove, split, group, ungroup, gain, fades, mute.
	QString operation;
	QStringList regionIds;
	bool linkedGroups = true;
	qint64 offset = 0;   // Signed timeline delta for move/duplicate; track automation stays put.
	qint64 position = 0; // Split cursor. Selected clips not crossing it are retained.
	double gainDb = 0;
	qint64 fadeIn = 0, fadeOut = 0;
	bool muted = false;
	QString name;
};

struct AudioArrangementResult {
	AudioSession session;
	QString error;
	QStringList selectedRegionIds;
	QStringList addedRegionIds;
	QString groupId;
	[[nodiscard]] bool succeeded() const { return error.isEmpty(); }
};

// Stable session order, unique existing targets, optionally expanded to linked
// group members across tracks. Empty/missing/duplicate targets are errors.
QStringList audioArrangementSelection(const AudioSession &session, const QStringList &ids, bool linkedGroups,
                                      QString *error);
// Small descriptors only. No media rewrite or partial mutation on failure.
AudioArrangementResult editAudioArrangement(const AudioSession &session, const AudioArrangementEdit &edit);
// Used after clip/track removal too. A singleton becomes an ordinary clip.
void pruneAudioSessionGroups(AudioSession *session);
} // namespace vibestudio
