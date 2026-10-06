#include "core/audio_automation.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
double shapeValue(const AudioAutomationShape &shape, AudioAutomationCurve curve, qint64 local)
{
	const auto at = shape.offset + local;
	if (curve == AudioAutomationCurve::Linear)
		return shape.first + (shape.last - shape.first) * double(at) / double(shape.frames);
	double amount = double(at) / double(shape.frames);
	amount = amount * amount * (3 - 2 * amount);
	return shape.first + (shape.last - shape.first) * amount;
}
bool whole(const QJsonValue &value, qint64 *result)
{
	const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
	if (!std::isfinite(number) || number < 0 || number > AudioAutomationFrameLimit || std::floor(number) != number)
		return false;
	*result = qint64(number);
	return true;
}
} // namespace
QString audioAutomationCurveId(AudioAutomationCurve curve)
{
	switch (curve) {
	case AudioAutomationCurve::Linear:
		return "linear";
	case AudioAutomationCurve::Step:
		return "step";
	case AudioAutomationCurve::Smooth:
		return "smooth";
	}
	return {};
}
QString audioAutomationCurveName(AudioAutomationCurve curve)
{
	switch (curve) {
	case AudioAutomationCurve::Linear:
		return QCoreApplication::translate("AudioAutomation", "Linear");
	case AudioAutomationCurve::Step:
		return QCoreApplication::translate("AudioAutomation", "Step");
	case AudioAutomationCurve::Smooth:
		return QCoreApplication::translate("AudioAutomation", "Smooth");
	}
	return {};
}
bool parseAudioAutomationCurve(const QString &id, AudioAutomationCurve *curve)
{
	if (!curve)
		return false;
	for (const auto value : {AudioAutomationCurve::Linear, AudioAutomationCurve::Step, AudioAutomationCurve::Smooth})
		if (audioAutomationCurveId(value) == id) {
			*curve = value;
			return true;
		}
	return false;
}
bool validAudioAutomation(const QVector<AudioAutomationPoint> &points, double low, double high)
{
	if (points.size() > AudioAutomationPointLimit)
		return false;
	qint64 previous = -1;
	for (qsizetype i = 0; i < points.size(); ++i) {
		const auto &point = points[i];
		if (point.frame <= previous || point.frame > AudioAutomationFrameLimit || !std::isfinite(point.value) ||
		    point.value < low || point.value > high || audioAutomationCurveId(point.curve).isEmpty())
			return false;
		const auto &shape = point.shape;
		if (shape.frames == 0) {
			if (shape != AudioAutomationShape{})
				return false;
		} else {
			if (shape.frames < 1 || shape.frames > AudioAutomationFrameLimit || shape.offset < 0 ||
			    shape.offset >= shape.frames || point.curve == AudioAutomationCurve::Step || i + 1 == points.size() ||
			    points[i + 1].frame <= point.frame || points[i + 1].frame > AudioAutomationFrameLimit ||
			    points[i + 1].frame - point.frame > shape.frames - shape.offset || !std::isfinite(shape.first) ||
			    !std::isfinite(shape.last) || shape.first < low || shape.first > high || shape.last < low ||
			    shape.last > high)
				return false;
			const auto at = shapeValue(shape, point.curve, 0);
			if (!std::isfinite(at) ||
			    std::abs(at - point.value) > 1e-12 * std::max({1.0, std::abs(at), std::abs(point.value)}))
				return false;
		}
		previous = point.frame;
	}
	return true;
}
double audioAutomationSegment(const AudioAutomationPoint &left, const AudioAutomationPoint &right, qint64 frame)
{
	if (frame >= right.frame)
		return right.value;
	if (frame <= left.frame || left.curve == AudioAutomationCurve::Step)
		return left.value;
	if (left.shape.frames)
		return shapeValue(left.shape, left.curve, frame - left.frame);
	if (left.curve == AudioAutomationCurve::Linear)
		return left.value + (right.value - left.value) * double(frame - left.frame) / double(right.frame - left.frame);
	double amount = double(frame - left.frame) / double(right.frame - left.frame);
	if (left.curve == AudioAutomationCurve::Smooth)
		amount = amount * amount * (3 - 2 * amount);
	return left.value + (right.value - left.value) * amount;
}
double audioAutomationValue(const QVector<AudioAutomationPoint> &points, qint64 frame, double fallback)
{
	if (points.isEmpty())
		return fallback;
	const auto after = std::upper_bound(points.cbegin(), points.cend(), frame,
	                                    [](qint64 time, const auto &point) { return time < point.frame; });
	if (after == points.cbegin())
		return points.first().value;
	if (after == points.cend())
		return points.last().value;
	return audioAutomationSegment(*(after - 1), *after, frame);
}
AudioAutomationPoint audioAutomationBoundary(const QVector<AudioAutomationPoint> &points, qint64 frame, double fallback)
{
	AudioAutomationPoint result{frame, audioAutomationValue(points, frame, fallback), AudioAutomationCurve::Step};
	const auto after = std::upper_bound(points.cbegin(), points.cend(), frame,
	                                    [](qint64 time, const auto &point) { return time < point.frame; });
	if (after == points.cbegin() || after == points.cend())
		return result;
	const auto &left = *(after - 1);
	result.curve = left.curve;
	if (left.curve != AudioAutomationCurve::Step) {
		result.shape = left.shape.frames ? left.shape
		                                 : AudioAutomationShape{0, after->frame - left.frame, left.value, after->value};
		result.shape.offset += frame - left.frame;
	}
	return result;
}
QJsonArray audioAutomationToJson(const QVector<AudioAutomationPoint> &points)
{
	QJsonArray result;
	for (const auto &point : points) {
		QJsonValue shape;
		if (point.shape.frames)
			shape = QJsonObject{{"offset", point.shape.offset},
			                    {"frames", point.shape.frames},
			                    {"first", point.shape.first},
			                    {"last", point.shape.last}};
		result.append(QJsonObject{{"frame", point.frame},
		                          {"value", point.value},
		                          {"curve", audioAutomationCurveId(point.curve)},
		                          {"segment", shape}});
	}
	return result;
}
bool audioAutomationFromJson(const QJsonValue &value, QVector<AudioAutomationPoint> *points, double low, double high,
                             bool legacyLinear, bool legacyShape)
{
	if (!points || !value.isArray() || value.toArray().size() > AudioAutomationPointLimit)
		return false;
	QVector<AudioAutomationPoint> next;
	for (const auto &item : value.toArray()) {
		const auto object = item.toObject();
		AudioAutomationPoint point;
		if (!item.isObject() ||
		    object.size() != (legacyLinear  ? 2
		                      : legacyShape ? 3
		                                    : 4) ||
		    !whole(object.value("frame"), &point.frame) || !object.value("value").isDouble() ||
		    (!legacyLinear && (!object.value("curve").isString() ||
		                       !parseAudioAutomationCurve(object.value("curve").toString(), &point.curve))))
			return false;
		if (!legacyLinear && !legacyShape) {
			const auto segment = object.value("segment");
			if (!segment.isNull()) {
				const auto shape = segment.toObject();
				if (!segment.isObject() || shape.size() != 4 || !whole(shape.value("offset"), &point.shape.offset) ||
				    !whole(shape.value("frames"), &point.shape.frames) || !point.shape.frames ||
				    !shape.value("first").isDouble() || !shape.value("last").isDouble())
					return false;
				point.shape.first = shape.value("first").toDouble();
				point.shape.last = shape.value("last").toDouble();
			}
		}
		point.value = object.value("value").toDouble();
		next.append(point);
	}
	if (!validAudioAutomation(next, low, high))
		return false;
	*points = std::move(next);
	return true;
}
} // namespace vibestudio
