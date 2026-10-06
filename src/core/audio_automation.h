#pragma once
#include <QJsonArray>
#include <QString>
#include <QVector>

namespace vibestudio
{
inline constexpr qint64 AudioAutomationFrameLimit = 384000LL * 60 * 60 * 24;
inline constexpr int AudioAutomationPointLimit = 4096;
enum class AudioAutomationCurve : quint8 { Linear, Step, Smooth };
struct AudioAutomationShape {
	// Zero frames is an ordinary segment between adjacent point values. A
	// positive span retains original endpoints when a time edit cuts a curve.
	qint64 offset = 0, frames = 0;
	double first = 0, last = 0;
	bool operator==(const AudioAutomationShape &) const = default;
};
struct AudioAutomationPoint {
	qint64 frame = 0;
	double value = 0;
	// Interpolation from this point to the following point; endpoints hold.
	AudioAutomationCurve curve = AudioAutomationCurve::Linear;
	AudioAutomationShape shape = {};
	bool operator==(const AudioAutomationPoint &) const = default;
};
QString audioAutomationCurveId(AudioAutomationCurve curve);
QString audioAutomationCurveName(AudioAutomationCurve curve);
bool parseAudioAutomationCurve(const QString &id, AudioAutomationCurve *curve);
bool validAudioAutomation(const QVector<AudioAutomationPoint> &points, double low, double high);
double audioAutomationSegment(const AudioAutomationPoint &left, const AudioAutomationPoint &right, qint64 frame);
double audioAutomationValue(const QVector<AudioAutomationPoint> &points, qint64 frame, double fallback);
// Exact sampled boundary with an outgoing window into its original segment.
// Input is a valid lane; frame is within the supported timeline.
AudioAutomationPoint audioAutomationBoundary(const QVector<AudioAutomationPoint> &points, qint64 frame,
                                             double fallback);
QJsonArray audioAutomationToJson(const QVector<AudioAutomationPoint> &points);
bool audioAutomationFromJson(const QJsonValue &value, QVector<AudioAutomationPoint> *points, double low, double high,
                             bool legacyLinear = false, bool legacyShape = false);
} // namespace vibestudio
