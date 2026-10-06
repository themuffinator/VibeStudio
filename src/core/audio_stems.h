#pragma once
#include "core/audio_session_io.h"

namespace vibestudio
{
struct AudioStemExportRequest {
	QString directory, prefix;
	QStringList stripIds, protectedPaths;
	bool includeMaster = false;
	bool respectSolo = false;
	bool overwrite = false;
	bool dryRun = false;
	qint64 first = 0, end = -1;
	AudioSessionRenderTarget::Tap tap = AudioSessionRenderTarget::Tap::PostFader;
	AudioWavFormat format = AudioWavFormat::Float32;
	bool dither = false;
	quint64 ditherSeed = 0;
	// Worker callback: zero-based file index, file count, rendered/total frames.
	std::function<void(int, int, qint64, qint64)> progress;
};
struct AudioStemFile {
	QString name, path;
	AudioSessionRenderTarget target;
	quint64 ditherSeed = 0;
};
struct AudioStemPlan {
	QVector<AudioStemFile> files;
	QString manifestPath, error;
	qint64 first = 0, end = 0;
};
struct AudioStemExportReport {
	AudioStemPlan plan;
	QVector<AudioSessionMixdownReport> files;
	AudioProjectSaveReport manifestSaved;
	QJsonObject manifest;
	QString error;
	bool succeeded = false, cancelled = false;
	int completed = 0; // Committed WAVs, or fully rendered WAVs in a dry run.
};
// Bounded metadata-only planning; no media scan, output writes or large reads.
AudioStemPlan planAudioSessionStems(const AudioSession &session, const AudioStemExportRequest &request);
// Preflight every destination before publishing an in-progress manifest. WAVs
// commit individually. Cancellation keeps completed files and performs a final
// bounded manifest update; crashes leave an explicitly incomplete manifest.
AudioStemExportReport writeAudioSessionStems(const AudioSession &session, const AudioStemExportRequest &request,
                                             const AudioWorkControl &control = {});
} // namespace vibestudio
