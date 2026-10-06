#pragma once

#include "core/audio_project.h"
#include "core/audio_session_io.h"
#include <QDateTime>
#include <memory>

class QLockFile;

namespace vibestudio
{
enum class AudioRecoveryKind { Waveform, Session };
QString audioRecoveryKindId(AudioRecoveryKind kind);

// Recovery envelopes carry provenance separately from the strict native session
// schema. Embedded media make the draft independent of the original installation.
struct AudioSessionRecovery {
	AudioSession session;
	QString sourcePath;
	QDateTime writtenUtc;
};
QString audioSessionRecoveryPath(const QString &directory, const QString &id);
QString writeAudioSessionRecovery(const AudioSession &session, const QString &sourcePath, const QString &directory,
                                  const QString &id, QString *error = nullptr);
bool readAudioSessionRecovery(const QString &path, const QByteArray &expectedSha256, AudioSessionRecovery *recovery,
                              QString *error = nullptr, const AudioWorkControl &control = {});
bool removeAudioSessionRecovery(const QString &directory, const QString &id, QString *error = nullptr);

inline constexpr int AudioRecoveryCountLimit = 32;
inline constexpr qint64 AudioRecoveryStorageLimit = 512 * 1024 * 1024;
inline constexpr int AudioRecoveryScanLimit = 256;

struct AudioRecoveryInfo {
	AudioRecoveryKind kind = AudioRecoveryKind::Waveform;
	QString id, path, sourceName, sourcePath;
	QDateTime writtenUtc, modifiedUtc;
	qint64 bytes = 0, frames = 0;
	int channels = 0, sampleRate = 0;
	int tracks = 0, clips = 0;
	QByteArray sha256;
	QString error;
	bool sessionFilePresent = false;
	[[nodiscard]] bool verified() const { return error.isEmpty() && sha256.size() == 32; }
};

struct AudioRecoveryInventory {
	QVector<AudioRecoveryInfo> records;
	qint64 totalBytes = 0;
	bool truncated = false, cancelled = false;
	QString error;
};

QString audioRecoveryDirectory();
// Metadata-only startup discovery: no sample payloads or recorded sources are
// opened. Records are candidates, not verified recovery documents.
AudioRecoveryInventory discoverAudioRecoveries(const QString &directory, const AudioWorkControl &control = {});
// Read-only, top-level UUID records only; full verification is bounded by the
// storage budget. Invalid records remain visible. No recorded source is opened.
AudioRecoveryInventory listAudioRecoveries(const QString &directory, const AudioWorkControl &control = {});
QJsonObject audioRecoveryInventoryJson(const AudioRecoveryInventory &inventory);
// A live editor holds this lease until retirement/destruction. Stale process
// locks use Qt's portable QLockFile handling; recorded PIDs are never trusted.
std::unique_ptr<QLockFile> acquireAudioRecoverySession(const QString &directory, const QString &id,
                                                       QString *error = nullptr,
                                                       AudioRecoveryKind kind = AudioRecoveryKind::Waveform);
// Explicit user discard, guarded by the digest shown in the inventory and by
// the session/file/folder locks. No recursive or age-based deletion exists.
bool discardAudioRecovery(const QString &directory, const QString &id, const QByteArray &expectedSha256, bool dryRun,
                          QString *error = nullptr, AudioRecoveryKind kind = AudioRecoveryKind::Waveform);

} // namespace vibestudio
