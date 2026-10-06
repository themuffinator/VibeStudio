#pragma once

#include "core/audio_clip.h"

#include <QJsonObject>

namespace vibestudio
{

inline constexpr qint64 AudioProjectByteLimit = AudioSampleLimit * 4 + 256 * 1024 + 48;

struct AudioProject {
	AudioClip clip;
	qint64 firstFrame = 0;
	qint64 endFrame = 0;
	QString sourceName;
	// Provenance only. Reading/restoring a project never grants write access here.
	QString sourcePath;
	QJsonObject metadata;
};

struct AudioProjectIdentity {
	QString path;
	QString canonicalPath;
	QByteArray sha256;
};

struct AudioProjectSaveRequest {
	QString path;
	bool overwrite = false;
	bool dryRun = false;
	AudioProjectIdentity expected;
	QString protectedPath;
};

struct AudioProjectSaveReport {
	bool succeeded = false;
	bool written = false;
	bool conflict = false;
	AudioProjectIdentity identity;
	QString error;
};

// VibeStudio-owned .vsaudio format: 8-byte magic, LE version/JSON length,
// bounded JSON, interleaved IEEE float32 LE samples, SHA-256 of preceding bytes.
// There is no quantization, automatic source write-back, or executable content.
QByteArray encodeAudioProject(const AudioProject& project, QString* error = nullptr,
                              const AudioWorkControl& control = {});
bool decodeAudioProject(const QByteArray& bytes, AudioProject* project, QString* error = nullptr,
                        const AudioWorkControl& control = {});
bool readAudioProject(const QString& path, AudioProject* project, AudioProjectIdentity* identity = nullptr,
                      QString* error = nullptr, const AudioWorkControl& control = {});
AudioProjectSaveReport writeAudioProject(const AudioProject& project, const AudioProjectSaveRequest& request,
                                         const AudioWorkControl& control = {});

// Only UUID-named files directly inside this recovery directory are managed.
QString audioRecoveryPath(const QString& directory, const QString& id);
QString writeAudioRecovery(const AudioProject& project, const QString& directory, const QString& id,
                           QString* error = nullptr);
bool removeAudioRecovery(const QString& directory, const QString& id, QString* error = nullptr);

} // namespace vibestudio
