#pragma once

#include "core/audio_clip.h"

#include <QJsonObject>
#include <optional>

namespace vibestudio
{

enum class AudioChannelRole { Left, Right, Center, LowFrequency, LeftSurround, RightSurround, LeftBack, RightBack, BackCenter, Unused };
QString audioChannelRoleId(AudioChannelRole role);
QString audioChannelRoleName(AudioChannelRole role);
bool parseAudioChannelRole(const QString& id, AudioChannelRole* role);

struct AudioAnalysisOptions {
	// Empty selects mono/stereo by channel count. Surround layouts require review.
	QVector<AudioChannelRole> channelMap;
	bool measureLoudness = true;
};

struct AudioChannelStatistics {
	double minimum = 0, maximum = 0, peak = 0, rms = 0, dc = 0;
	std::optional<double> truePeak;
	qint64 peakFrame = -1;
	// Full scale is exactly +/-1 in the editable float representation.
	qint64 samplesAboveFullScale = 0, samplesAtFullScale = 0;
	qint64 firstAboveFullScaleFrame = -1, longestAboveFullScaleRun = 0;
};

struct AudioAnalysis {
	int sampleRate = 0;
	qint64 firstFrame = 0, endFrame = 0;
	QVector<AudioChannelStatistics> channels;
	double peak = 0, rms = 0;
	qint64 samplesAboveFullScale = 0;
	std::optional<double> truePeak;
	QString truePeakStatus, truePeakMessage;
	int truePeakOversampling = 0;
	std::optional<double> integratedLufs, relativeThresholdLufs;
	QString loudnessStatus, loudnessMessage;
	QVector<AudioChannelRole> channelMap;
	qint64 loudnessBlockFrames = 0;
};

struct AudioAnalysisResult {
	AudioAnalysis analysis;
	QString error;
	bool cancelled = false;
	[[nodiscard]] bool succeeded() const { return error.isEmpty() && !cancelled && !analysis.channels.isEmpty(); }
};

// Read-only sample statistics for [firstFrame, endFrame); -1 means clip end.
// Peak positions are absolute frames, with the earliest frame winning ties.
// RMS is unweighted and includes DC. True peak uses a separately zero-extended
// selection. Integrated loudness uses complete 400 ms blocks and reviewed roles.
// Empty ranges are valid and have zero levels, no peak position, and no events.
AudioAnalysisResult analyzeAudioClip(const AudioClip& clip, qint64 firstFrame = 0, qint64 endFrame = -1,
                                     const AudioWorkControl& control = {}, const AudioAnalysisOptions& options = {});
// Silence dBFS and absent frame positions are JSON null, never NaN/Infinity.
QJsonObject audioAnalysisJson(const AudioAnalysis& analysis);

} // namespace vibestudio
