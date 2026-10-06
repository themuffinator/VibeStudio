#pragma once
#include "core/audio_session.h"

namespace vibestudio
{
struct AudioMediaUsage {
	QString id, name, path, availability;
	int channels = 0, clips = 0, takeClips = 0; // clips includes retained alternatives.
	qint64 frames = 0, bytes = 0, requiredFrames = 0;
	QStringList tracks;
};
struct AudioMediaInventory {
	QVector<AudioMediaUsage> sources;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled; }
};
// File status is a point-in-time availability check, not content verification.
// Missing provenance does not make the embedded snapshot unavailable for audio.
AudioMediaInventory inspectAudioMedia(const AudioSession &session, const AudioWorkControl &control = {});
QJsonObject audioMediaInventoryJson(const AudioMediaInventory &inventory);

struct AudioMediaCandidate {
	AudioProject audio;
	AudioProjectIdentity identity;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && identity.sha256.size() == 32; }
};
// Reads one bounded regular audio/native-project file. The immutable decoded
// snapshot and exact file digest are retained for review and later revalidation.
AudioMediaCandidate readAudioMediaCandidate(const QString &path, const AudioWorkControl &control = {});

struct AudioMediaEdit {
	QString operation = {}; // rename, relink, replace, remove, prune
	QStringList sourceIds = {};
	QString name = {};
	bool resample = false;
	bool operator==(const AudioMediaEdit &) const = default;
};
struct AudioMediaResult {
	AudioSession session;
	QString error, addedSourceId;
	QStringList affectedRegionIds, removedSourceIds;
	bool cancelled = false, identicalSamples = false, resampled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled; }
};
// Replacement creates a new immutable source identity and retargets every clip
// using the old identity; region/track/group IDs and sample offsets remain intact.
// Relinking changes only provenance and requires identical decoded samples.
// verifyFile rechecks the candidate's reviewed file identity before adoption.
AudioMediaResult editAudioMedia(const AudioSession &session, const AudioMediaEdit &edit,
                                const AudioMediaCandidate &candidate = {}, bool verifyFile = true,
                                const AudioWorkControl &control = {});
} // namespace vibestudio
