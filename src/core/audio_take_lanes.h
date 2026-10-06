#pragma once
#include "core/audio_session.h"

namespace vibestudio
{
struct AudioTakeLaneEdit {
	QString operation; // capture, rename, remove, promote
	QString trackId, laneId, name;
	qint64 first = 0, end = 0; // Half-open timeline range for capture/promote.
	qint64 position = 0;       // Destination frame for promote.
	bool replaceExisting = true;
};
struct AudioTakeLaneResult {
	AudioSession session;
	QString error, laneId;
	QStringList regionIds;
	[[nodiscard]] bool succeeded() const { return error.isEmpty(); }
};
// Capture retains a copy of the active arrangement range. Promote inserts a
// clipped lane range at position, optionally clearing that entire target range.
// Both retain fade-domain windows, gain, mute and immutable source identities.
// Alternatives and automation are unchanged; active clip group links are not
// copied into a take lane or its independently promoted clips.
AudioTakeLaneResult editAudioTakeLane(const AudioSession &session, const AudioTakeLaneEdit &edit);
} // namespace vibestudio
