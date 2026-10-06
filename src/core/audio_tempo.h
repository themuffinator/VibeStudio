#pragma once

#include "core/audio_automation.h"
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <vector>

namespace vibestudio
{
// Quarter-note ticks are independent of the time signature. Audio and its
// automation remain sample-anchored; editing this map never stretches media.
inline constexpr qint64 AudioTicksPerQuarter = 960;
inline constexpr qint64 AudioMusicalTickLimit = AudioAutomationFrameLimit * 6400;
inline constexpr int AudioTempoChangeLimit = 4096;

struct AudioTempoChange {
	qint64 tick = 0;
	double bpm = 120;
	bool operator==(const AudioTempoChange &) const = default;
};
struct AudioMeterChange {
	qint64 bar = 1;
	int beatsPerBar = 4;
	int beatUnit = 4;
	bool operator==(const AudioMeterChange &) const = default;
};
struct AudioTempoMap {
	double tempo = 120;
	int beatsPerBar = 4;
	int beatUnit = 4;
	// Strictly increasing, excluding the origin (tick 0 / bar 1).
	QVector<AudioTempoChange> tempoChanges;
	QVector<AudioMeterChange> meterChanges;
	bool operator==(const AudioTempoMap &) const = default;
};
struct AudioMusicalPosition {
	qint64 bar = 0; // One-based bars and beats; bar 0 is invalid.
	int beat = 1;
	qint64 tick = 0; // Zero-based ticks within the notated beat.
	bool operator==(const AudioMusicalPosition &) const = default;
};
enum class AudioMusicalGrid { Bar, Beat, HalfBeat, QuarterBeat, BeatTriplet };

// Prepared, bounded tempo/meter lookup. Integrate in extended precision and
// round only the final absolute frame, never each beat or tempo segment.
// Conversions return -1 / an invalid position for out-of-range arguments.
class AudioTempoTimeline {
  public:
	QString prepare(const AudioTempoMap &map, int sampleRate);
	[[nodiscard]] bool ready() const { return !m_tempos.empty(); }
	[[nodiscard]] qint64 frameAtTick(qint64 tick) const;
	[[nodiscard]] qint64 tickAtFrame(qint64 frame) const;
	[[nodiscard]] qint64 tickAtPosition(const AudioMusicalPosition &position) const;
	[[nodiscard]] AudioMusicalPosition positionAtTick(qint64 tick) const;
	[[nodiscard]] AudioMusicalPosition positionAtFrame(qint64 frame) const;
	[[nodiscard]] qint64 frameAtPosition(const AudioMusicalPosition &position) const;
	[[nodiscard]] double tempoAtFrame(qint64 frame) const;
	[[nodiscard]] AudioMeterChange meterAtFrame(qint64 frame) const;
	// Nearest grid line by frame distance, ties later. Stepping finds the
	// strictly previous/next representable grid frame, across tempo and meter.
	[[nodiscard]] qint64 snapFrame(qint64 frame, AudioMusicalGrid grid) const;
	[[nodiscard]] qint64 stepFrame(qint64 frame, AudioMusicalGrid grid, int direction) const;

  private:
	struct Tempo {
		qint64 tick;
		long double frame, framesPerTick;
		double bpm;
	};
	struct Meter {
		qint64 bar, tick;
		int beats, unit;
	};
	long double exactTick(long double frame) const;
	qint64 gridTick(qint64 tick, AudioMusicalGrid grid, int direction) const;
	std::vector<Tempo> m_tempos;
	std::vector<Meter> m_meters;
};

QString validateAudioTempoMap(const AudioTempoMap &map, int sampleRate);
QJsonObject audioTempoMapToJson(const AudioTempoMap &map);
bool audioTempoMapFromJson(const QJsonObject &json, AudioTempoMap *map);
QString audioMusicalPositionText(const AudioMusicalPosition &position);
bool parseAudioMusicalPosition(const QString &text, AudioMusicalPosition *position);
} // namespace vibestudio
