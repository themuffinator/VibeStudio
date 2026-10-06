#pragma once
#include "core/audio_duplex.h"
#include "core/audio_take.h"

namespace vibestudio
{
// A .vsrecord folder owns fixed, relative arm-01.vstake ... arm-08.vstake
// journals. Session paths are provenance only and are never opened by readers.
struct AudioRecordingPlan {
	QString name, sourceSessionPath, inputDeviceName;
	int sampleRate = 48000;
	QDateTime startedUtc = QDateTime::currentDateTimeUtc();
	AudioDuplexPass pass;
};
struct AudioRecordingReceipt {
	enum class Outcome { Complete, Stopped, Interrupted };
	Outcome outcome = Outcome::Interrupted;
	AudioDuplexProgress progress;
	QString error, inputName, outputName, host;
	double inputLatencySeconds = 0, outputLatencySeconds = 0;
	bool packetInputTimestamp = false;
};
struct AudioRecordingInfo {
	QString directory, error;
	AudioRecordingPlan plan;
	QByteArray planSha256, receiptSha256;
	bool planValid = false, receiptValid = false;
	AudioRecordingReceipt receipt;
	QVector<AudioTakeInfo> takes;
	// True only when the final receipt agrees with every independently scanned
	// journal. Missing or damaged receipts never hide recoverable take prefixes.
	bool receiptMatches = false;
	QJsonObject receiptRecord;
};
QString validateAudioRecordingPlan(const AudioRecordingPlan &plan);
AudioTakeMetadata audioRecordingArmMetadata(const AudioRecordingPlan &plan, int arm);
QString audioRecordingArmPath(const QString &directory, int arm);
// Creates a new folder and durable, immutable plan.json; never replaces files.
// A partial creation is retained for inspection. Caller owns its journals.
bool createAudioRecordingFolder(const QString &directory, const AudioRecordingPlan &plan, QByteArray *planSha256,
                                QString *error);
// New-only result.json, bound to the initial plan and verified journal prefixes.
// Run after all writers have closed. No claim of multi-file atomic completion.
bool finishAudioRecordingFolder(const QString &directory, const QByteArray &planSha256,
                                const AudioRecordingReceipt &receipt, QString *error,
                                AudioRecordingInfo *finalInfo = nullptr);
AudioRecordingInfo inspectAudioRecording(const QString &directory, const AudioWorkControl &control = {});
QJsonObject audioRecordingInfoJson(const AudioRecordingInfo &info);
} // namespace vibestudio
