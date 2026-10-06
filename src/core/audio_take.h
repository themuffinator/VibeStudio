#pragma once

#include "core/audio_project.h"
#include <QDateTime>
#include <memory>
#include <span>

namespace vibestudio
{
inline constexpr int AudioTakeBlockFrames = 4096;
inline constexpr qint64 AudioTakeByteLimit = 64LL * 1024 * 1024 * 1024;
inline constexpr qint64 AudioTakeFrameLimit = 384000LL * 60 * 60 * 24;

struct AudioTakeMetadata {
	QString name;
	int sampleRate = 48000;
	int inputChannels = 1;
	// Zero-based hardware channel numbers in stored-channel order. Only these
	// channels are recorded; ordering is explicit and duplicates are rejected.
	QVector<int> channelMap{0};
	qint64 position = 0;
	qint64 latencyFrames = 0; // Positive compensation places imported audio earlier.
	QString trackId, sourceSessionPath, deviceName;
	QDateTime startedUtc = QDateTime::currentDateTimeUtc();
};
QString validateAudioTakeMetadata(const AudioTakeMetadata &metadata);
QJsonObject audioTakeMetadataJson(const AudioTakeMetadata &metadata);

struct AudioTakeInfo {
	AudioTakeMetadata metadata;
	QString path;
	qint64 bytes = 0, verifiedBytes = 0, frames = 0;
	QByteArray prefixSha256;
	bool headerValid = false, complete = false, cancelled = false;
	QString error;
	[[nodiscard]] bool recoverable() const
	{
		return headerValid && frames > 0 && prefixSha256.size() == 32 && !cancelled;
	}
};
QJsonObject audioTakeInfoJson(const AudioTakeInfo &info);

// Append-only .vstake journal. Creates a new file; never overwrites or resumes an
// existing file. Each complete record is chained to its predecessor by SHA-256
// and flushed to storage. Destroying without finish leaves a recoverable prefix.
// All methods have one owner thread. Use a bounded queue ahead of a disk worker;
// append/finish perform blocking I/O and must never run on an audio callback.
class AudioTakeWriter {
  public:
	AudioTakeWriter();
	~AudioTakeWriter();
	AudioTakeWriter(const AudioTakeWriter &) = delete;
	AudioTakeWriter &operator=(const AudioTakeWriter &) = delete;
	bool open(const QString &path, const AudioTakeMetadata &metadata, QString *error);
	bool append(std::span<const float> interleaved, QString *error);
	bool finish(QString *error);
	void close();
	[[nodiscard]] qint64 frames() const;

  private:
	struct Private;
	std::unique_ptr<Private> d;
};

// Verification reads bounded blocks and stops at the first incomplete/damaged
// record. The digest identifies the verified prefix, not ignored trailing bytes.
// Source-session paths are provenance only and are never opened by this reader.
AudioTakeInfo inspectAudioTake(const QString &path, const AudioWorkControl &control = {});
struct AudioTakeReadResult {
	AudioTakeInfo info;
	AudioProject audio;
	QString error;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && audio.clip.channels > 0 && !info.cancelled; }
};
struct AudioTakeReadRange {
	qint64 first = 0, end = 0;
	QVector<int> channels;
};
struct AudioTakeBatchReadResult {
	AudioTakeInfo info;
	QVector<AudioProject> audio;
	QString error;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !audio.isEmpty() && !info.cancelled; }
};
// One verification scan supplies all ranges, in request order. Up to 128 ranges,
// each within the per-source limit and together within 64M interleaved samples.
// Nothing is published if any range, channel or reviewed digest fails.
AudioTakeBatchReadResult readAudioTakeRanges(const QString &path, const QByteArray &expectedPrefixSha256,
                                             const QVector<AudioTakeReadRange> &ranges, bool allowIncomplete = false,
                                             const AudioWorkControl &control = {});
// Select stored channels, not hardware indices. Range is [first,end). A reviewed
// digest is required; interrupted/damaged prefixes require explicit acceptance.
// Materialization respects the existing waveform per-source sample limit.
AudioTakeReadResult readAudioTakeRange(const QString &path, const QByteArray &expectedPrefixSha256, qint64 first,
                                       qint64 end, const QVector<int> &channels, bool allowIncomplete = false,
                                       const AudioWorkControl &control = {});
} // namespace vibestudio
