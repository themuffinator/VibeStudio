#pragma once
#include "core/audio_session.h"

namespace vibestudio
{
struct AudioRangeEdit {
	// Half-open sample-time range. Exactly one scope: all tracks, or distinct
	// track IDs. Group links never expand that scope. The musical map is retained.
	QString operation; // clear, ripple-delete, insert-silence, repeat
	QStringList trackIds;
	bool allTracks = false;
	qint64 first = 0, end = 0;
	bool followAutomation = true;
	// Global lanes can follow only an all-track time edit.
	bool masterAutomation = false;
};
struct AudioRangeResult {
	AudioSession session;
	QString error;
	QStringList trackIds, addedRegionIds;
	qint64 first = 0, end = 0;
	[[nodiscard]] bool succeeded() const { return error.isEmpty(); }
};

// Transactional descriptor/envelope edit; samples and caller state are retained.
AudioRangeResult editAudioRange(const AudioSession &session, const AudioRangeEdit &edit);
} // namespace vibestudio
