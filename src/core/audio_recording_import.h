#pragma once
#include "core/audio_recording.h"

namespace vibestudio
{
struct AudioRecordingSelection {
	int arm = 0;
	QByteArray expectedPrefixSha256;
	qint64 first = 0, end = 0, position = 0;
	QVector<int> channels; // Zero-based stored channels, not hardware inputs.
	QString trackId;
	bool replaceExisting = false;
	int loopPass = 0; // Zero-based pass; first/end are local to this pass.
};
struct AudioRecordingImportRequest {
	QString directory;
	QByteArray expectedPlanSha256, expectedReceiptSha256;
	QVector<AudioRecordingSelection> selections;
	bool allowInterrupted = false, groupRegions = true;
	bool comp = false;
	qint64 crossfadeFrames = 0;
};
struct AudioRecordingImportSlice {
	AudioRecordingSelection selection;
	qint64 fadeIn = 0, fadeOut = 0;
};
struct AudioRecordingImportPlan {
	QVector<AudioRecordingImportSlice> slices;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && !slices.isEmpty(); }
};
// Pure preflight against an inspected snapshot. Comp sections cannot overlap on
// a destination track. At adjacent cuts the outgoing take supplies crossfade
// handles after its chosen end; both clips receive complementary linear fades.
AudioRecordingImportPlan prepareAudioRecordingImport(const AudioSession &session, const AudioRecordingInfo &info,
                                                     const AudioRecordingImportRequest &request,
                                                     const AudioWorkControl &control = {});
struct AudioRecordingImportResult {
	AudioSession session;
	AudioRecordingInfo recording;
	QString error, groupId;
	QStringList regionIds, sourceIds, trackIds;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled; }
};
// Creates one complete replacement snapshot or no session on failure. Callers
// bind their immutable base snapshot to a document revision before adoption.
// All selected files are reverified; unselected journals are never materialized.
AudioRecordingImportResult importAudioRecording(const AudioSession &session, const AudioRecordingImportRequest &request,
                                                const AudioWorkControl &control = {});
struct AudioRecordingAuditionResult {
	AudioRecordingImportResult imported;
	qint64 first = 0, end = 0;
	[[nodiscard]] bool succeeded() const { return imported.succeeded() && end > first; }
};
// Prepare the exact reviewed import without adopting it. Isolated audition
// removes backing clips, retaining track/bus/master processing and mute/solo.
// The half-open range spans the imported clips, including crossfade handles;
// it adds neither preroll nor a trailing tail. Source/session files are retained.
AudioRecordingAuditionResult prepareAudioRecordingAudition(const AudioSession &session,
                                                           const AudioRecordingImportRequest &request,
                                                           bool includeBacking = true,
                                                           const AudioWorkControl &control = {});
// Version-1 reviews select the first pass. Version 2 adds one-based loopPass.
// Version 3 adds comp=true and crossfadeFrames, allowing multiple nonoverlapping
// sections of the same arm/pass. Other import modes retain distinct pairs.
// Arm/stored-channel numbers are one-based; half-open frame
// ranges are zero-based and local to the selected pass. Unknown fields fail.
QJsonObject audioRecordingImportRequestJson(const AudioRecordingImportRequest &request);
bool parseAudioRecordingImportRequest(const QJsonObject &json, AudioRecordingImportRequest *request, QString *error);
QJsonObject audioRecordingImportResultJson(const AudioRecordingImportResult &result);
QStringList audioRecordingProtectedPaths(const QString &directory);
} // namespace vibestudio
