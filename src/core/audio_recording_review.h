#pragma once
#include "core/audio_recording_import.h"

namespace vibestudio
{
inline constexpr qint64 AudioRecordingReviewByteLimit = 128 * 1024;

// Reads existing v1/v2/v3 import JSON and resolves its recording directory
// relative to this file. Neither caller state nor output identity changes on failure.
bool readAudioRecordingReview(const QString &path, AudioRecordingImportRequest *review,
                              AudioProjectIdentity *identity = nullptr, QString *error = nullptr,
                              const AudioWorkControl &control = {});

struct AudioRecordingReviewResult {
	AudioRecordingImportRequest review;
	AudioRecordingInfo recording;
	AudioProjectIdentity identity;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && recording.planValid; }
};
// Verifies journals and preflights every selection against the current session.
// It does not decode selected samples, open a device or change the session.
AudioRecordingReviewResult openAudioRecordingReview(const AudioSession &session, const QString &path,
                                                    const AudioWorkControl &control = {});

// Saves canonical review JSON after rechecking recording hashes and selections.
// The recording path is relative to the output parent when representable.
// Use a loaded identity to guard an update. Session/source/journal paths remain
// protected through the shared atomic output service, including dry runs.
AudioProjectSaveReport writeAudioRecordingReview(const AudioSession &session, const AudioRecordingImportRequest &review,
                                                 const AudioProjectSaveRequest &output,
                                                 const AudioWorkControl &control = {},
                                                 const QStringList &protectedPaths = {});
} // namespace vibestudio
