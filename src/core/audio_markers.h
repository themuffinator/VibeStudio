#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>

namespace vibestudio
{
inline constexpr int AudioCueLimit = 256;
inline constexpr int AudioCueNameLimit = 128;
inline constexpr int AudioMarkerByteLimit = 64 * 1024;

struct AudioCue {
	quint32 id = 0;
	qint64 frame = 0;
	QString name;
	bool operator==(const AudioCue&) const = default;
};
struct AudioLoop {
	qint64 first = 0, end = 0; // Forward infinite sustain, end exclusive.
	bool operator==(const AudioLoop&) const = default;
};
struct AudioMarkers {
	QVector<AudioCue> cues;
	std::optional<AudioLoop> loop;
	[[nodiscard]] bool empty() const { return cues.isEmpty() && !loop; }
	bool operator==(const AudioMarkers&) const = default;
};

QString validateAudioMarkers(const AudioMarkers& markers, qint64 frames);
QJsonObject audioMarkersJson(const AudioMarkers& markers);
bool parseAudioMarkersJson(const QJsonObject& json, qint64 frames, AudioMarkers* markers, QString* error = nullptr);
AudioMarkers trimAudioMarkers(const AudioMarkers& markers, qint64 first, qint64 end);
AudioMarkers replaceAudioMarkers(const AudioMarkers& markers, qint64 first, qint64 end, qint64 insertedFrames);
AudioMarkers reverseAudioMarkers(const AudioMarkers& markers, qint64 first, qint64 end);
AudioMarkers resampleAudioMarkers(const AudioMarkers& markers, qint64 oldFrames, qint64 newFrames, int oldRate,
                                  int newRate);
// Existing loop wins. Incoming cue IDs are retained where possible and otherwise
// assigned the smallest unused positive ID. Callers validate the merged limits.
AudioMarkers mergeAudioMarkers(AudioMarkers target, const AudioMarkers& incoming, qint64 offset);

enum class AudioWavMarkers { Standard, Quake, Omit };
// RIFF cue/adtl/CSET/smpl and the original Quake/II cue+ltxt convention.
// Parsing is bounded, skips PCM bytes, and never partially replaces the output.
bool decodeWavAudioMarkers(const QByteArray& wav, qint64 frames, AudioMarkers* markers, QString* error = nullptr,
                           AudioWavMarkers* sourceMode = nullptr);
QByteArray encodeWavAudioMarkers(const AudioMarkers& markers, qint64 frames, int sampleRate, AudioWavMarkers mode,
                                 QString* error = nullptr);
} // namespace vibestudio
