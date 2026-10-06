#include "core/audio_tempo.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool tempoValid(double bpm) { return std::isfinite(bpm) && bpm >= 20 && bpm <= 400; }
bool meterValid(int beats, int unit)
{
	return beats >= 1 && beats <= 32 && unit >= 1 && unit <= 32 && (unit & (unit - 1)) == 0;
}
QString problem(const char *message) { return QCoreApplication::translate("AudioTempo", message); }
qint64 rounded(long double value)
{
	// Windows uses double precision for long double. Treat numerical noise at
	// an exact half-sample as a tie, consistently rounding toward the future.
	const auto tolerance = 4 * std::numeric_limits<long double>::epsilon() * std::max(1.L, std::abs(value));
	return qint64(std::floor(value + .5L + tolerance));
}
bool integer(const QJsonObject &object, const char *key, qint64 low, qint64 high, qint64 *result)
{
	const auto value = object.value(QLatin1String(key));
	const double number = value.toDouble(-1);
	if (!value.isDouble() || !std::isfinite(number) || number < double(low) || number > double(high) ||
	    number != std::floor(number))
		return false;
	*result = qint64(number);
	return true;
}
} // namespace

QString AudioTempoTimeline::prepare(const AudioTempoMap &map, int sampleRate)
{
	m_tempos.clear();
	m_meters.clear();
	const auto invalid = [this](const char *text) {
		m_tempos.clear();
		m_meters.clear();
		return problem(text);
	};
	if (sampleRate < 1 || sampleRate > 384000 || !tempoValid(map.tempo) || !meterValid(map.beatsPerBar, map.beatUnit) ||
	    map.tempoChanges.size() > AudioTempoChangeLimit || map.meterChanges.size() > AudioTempoChangeLimit)
		return invalid(QT_TRANSLATE_NOOP("AudioTempo",
		                                 "Tempo must be 20–400 BPM; meters use 1–32 beats and a power-of-two beat "
		                                 "unit from 1 to 32. Each map allows 4096 changes."));
	const auto duration = [sampleRate](double bpm) {
		return static_cast<long double>(sampleRate) * 60 / (bpm * AudioTicksPerQuarter);
	};
	m_tempos.reserve(size_t(map.tempoChanges.size()) + 1);
	m_tempos.push_back({0, 0, duration(map.tempo), map.tempo});
	long double compensation = 0;
	for (const auto &change : map.tempoChanges) {
		const auto &last = m_tempos.back();
		if (change.tick <= last.tick || change.tick > AudioMusicalTickLimit || !tempoValid(change.bpm))
			return invalid(QT_TRANSLATE_NOOP(
			    "AudioTempo",
			    "Tempo changes need increasing positive quarter-note ticks and finite 20–400 BPM values."));
		const long double increment = (change.tick - last.tick) * last.framesPerTick - compensation;
		const long double frame = last.frame + increment;
		compensation = (frame - last.frame) - increment;
		if (frame > AudioAutomationFrameLimit)
			return invalid(
			    QT_TRANSLATE_NOOP("AudioTempo", "A tempo or meter change exceeds the session timeline limit."));
		m_tempos.push_back({change.tick, frame, duration(change.bpm), change.bpm});
	}
	m_meters.reserve(size_t(map.meterChanges.size()) + 1);
	m_meters.push_back({1, 0, map.beatsPerBar, map.beatUnit});
	for (const auto &change : map.meterChanges) {
		const auto &last = m_meters.back();
		const qint64 barTicks = AudioTicksPerQuarter * 4 / last.unit * last.beats;
		if (change.bar <= last.bar || change.bar - last.bar > (AudioMusicalTickLimit - last.tick) / barTicks ||
		    !meterValid(change.beatsPerBar, change.beatUnit))
			return invalid(QT_TRANSLATE_NOOP(
			    "AudioTempo", "Meter changes need increasing bar numbers after bar 1 and valid time signatures."));
		const qint64 tick = last.tick + (change.bar - last.bar) * barTicks;
		if (frameAtTick(tick) < 0)
			return invalid(
			    QT_TRANSLATE_NOOP("AudioTempo", "A tempo or meter change exceeds the session timeline limit."));
		m_meters.push_back({change.bar, tick, change.beatsPerBar, change.beatUnit});
	}
	return {};
}

qint64 AudioTempoTimeline::frameAtTick(qint64 tick) const
{
	if (!ready() || tick < 0 || tick > AudioMusicalTickLimit)
		return -1;
	const auto it =
	    std::upper_bound(m_tempos.begin(), m_tempos.end(), tick, [](qint64 t, const Tempo &s) { return t < s.tick; });
	const auto &segment = *std::prev(it);
	const auto frame = segment.frame + (tick - segment.tick) * segment.framesPerTick;
	return frame <= AudioAutomationFrameLimit ? rounded(frame) : -1;
}
long double AudioTempoTimeline::exactTick(long double frame) const
{
	const auto it = std::upper_bound(m_tempos.begin(), m_tempos.end(), frame,
	                                 [](long double f, const Tempo &s) { return f < s.frame; });
	const auto &segment = *std::prev(it);
	return segment.tick + (frame - segment.frame) / segment.framesPerTick;
}
qint64 AudioTempoTimeline::tickAtFrame(qint64 frame) const
{
	if (!ready() || frame < 0 || frame > AudioAutomationFrameLimit)
		return -1;
	auto tick = std::clamp<qint64>(rounded(exactTick(frame)), 0, AudioMusicalTickLimit);
	// The nearest unconstrained tick may lie just beyond the final frame.
	// Keep a displayed end position usable by the inverse navigation command.
	if (tick > 0 && frameAtTick(tick) < 0)
		--tick;
	return tick;
}
qint64 AudioTempoTimeline::tickAtPosition(const AudioMusicalPosition &position) const
{
	if (!ready() || position.bar < 1 || position.beat < 1 || position.tick < 0)
		return -1;
	const auto it = std::upper_bound(m_meters.begin(), m_meters.end(), position.bar,
	                                 [](qint64 b, const Meter &s) { return b < s.bar; });
	const auto &meter = *std::prev(it);
	const qint64 beatTicks = AudioTicksPerQuarter * 4 / meter.unit, barTicks = beatTicks * meter.beats;
	if (position.beat > meter.beats || position.tick >= beatTicks ||
	    position.bar - meter.bar > (AudioMusicalTickLimit - meter.tick) / barTicks)
		return -1;
	const auto tick =
	    meter.tick + (position.bar - meter.bar) * barTicks + (position.beat - 1) * beatTicks + position.tick;
	return tick <= AudioMusicalTickLimit ? tick : -1;
}
AudioMusicalPosition AudioTempoTimeline::positionAtTick(qint64 tick) const
{
	if (!ready() || m_meters.empty() || tick < 0 || tick > AudioMusicalTickLimit)
		return {};
	const auto it =
	    std::upper_bound(m_meters.begin(), m_meters.end(), tick, [](qint64 t, const Meter &s) { return t < s.tick; });
	const auto &meter = *std::prev(it);
	const qint64 beatTicks = AudioTicksPerQuarter * 4 / meter.unit, barTicks = beatTicks * meter.beats;
	const auto offset = tick - meter.tick;
	return {meter.bar + offset / barTicks, int(offset % barTicks / beatTicks) + 1, offset % beatTicks};
}
AudioMusicalPosition AudioTempoTimeline::positionAtFrame(qint64 frame) const
{
	return positionAtTick(tickAtFrame(frame));
}
qint64 AudioTempoTimeline::frameAtPosition(const AudioMusicalPosition &position) const
{
	return frameAtTick(tickAtPosition(position));
}
double AudioTempoTimeline::tempoAtFrame(qint64 frame) const
{
	if (!ready() || frame < 0 || frame > AudioAutomationFrameLimit)
		return 0;
	const auto it = std::upper_bound(m_tempos.begin(), m_tempos.end(), frame,
	                                 [](qint64 f, const Tempo &s) { return f < rounded(s.frame); });
	return std::prev(it)->bpm;
}
AudioMeterChange AudioTempoTimeline::meterAtFrame(qint64 frame) const
{
	const auto tick = tickAtFrame(frame);
	if (tick < 0 || m_meters.empty())
		return {};
	const auto it =
	    std::upper_bound(m_meters.begin(), m_meters.end(), tick, [](qint64 t, const Meter &s) { return t < s.tick; });
	const auto &meter = *std::prev(it);
	return {meter.bar, meter.beats, meter.unit};
}
qint64 AudioTempoTimeline::gridTick(qint64 tick, AudioMusicalGrid grid, int direction) const
{
	if (tick < 0 || tick > AudioMusicalTickLimit || m_meters.empty())
		return -1;
	const auto it =
	    std::upper_bound(m_meters.begin(), m_meters.end(), tick, [](qint64 t, const Meter &s) { return t < s.tick; });
	const auto &meter = *std::prev(it);
	qint64 step = AudioTicksPerQuarter * 4 / meter.unit;
	switch (grid) {
	case AudioMusicalGrid::Bar:
		step *= meter.beats;
		break;
	case AudioMusicalGrid::Beat:
		break;
	case AudioMusicalGrid::HalfBeat:
		step /= 2;
		break;
	case AudioMusicalGrid::QuarterBeat:
		step /= 4;
		break;
	case AudioMusicalGrid::BeatTriplet:
		step /= 3;
		break;
	}
	const auto floor = meter.tick + (tick - meter.tick) / step * step;
	if (direction <= 0)
		return floor;
	const auto next = it == m_meters.end() ? floor + step : std::min(floor + step, it->tick);
	return next <= AudioMusicalTickLimit ? next : -1;
}
qint64 AudioTempoTimeline::stepFrame(qint64 frame, AudioMusicalGrid grid, int direction) const
{
	if (!ready() || frame < 0 || frame > AudioAutomationFrameLimit || !direction)
		return -1;
	auto tick = gridTick(tickAtFrame(frame), grid, 0);
	// At 1 Hz several grid ticks share a frame. Advancing through them is
	// bounded by 400 BPM / 1 Hz and the finest supported subdivision (30 ticks).
	for (int tries = 0; tick >= 0 && tries < 1024; ++tries) {
		const auto candidate = frameAtTick(tick);
		if (candidate >= 0 && (direction > 0 ? candidate > frame : candidate < frame))
			return candidate;
		if (direction > 0) {
			if (candidate < 0)
				break;
			tick = gridTick(tick, grid, 1);
		} else
			tick = gridTick(tick - 1, grid, 0);
	}
	return frame; // No representable grid line within the bounded timeline.
}
qint64 AudioTempoTimeline::snapFrame(qint64 frame, AudioMusicalGrid grid) const
{
	if (!ready() || frame < 0 || frame > AudioAutomationFrameLimit)
		return -1;
	const auto near = frameAtTick(gridTick(tickAtFrame(frame), grid, 0));
	if (near == frame)
		return frame;
	const auto before = stepFrame(frame, grid, -1), after = stepFrame(frame, grid, 1);
	if (after == frame)
		return before;
	if (before == frame)
		return after;
	return after - frame <= frame - before ? after : before;
}
QString validateAudioTempoMap(const AudioTempoMap &map, int sampleRate)
{
	AudioTempoTimeline timeline;
	return timeline.prepare(map, sampleRate);
}
QJsonObject audioTempoMapToJson(const AudioTempoMap &map)
{
	QJsonArray tempos, meters;
	for (const auto &change : map.tempoChanges)
		tempos.append(QJsonObject{{"tick", change.tick}, {"bpm", change.bpm}});
	for (const auto &change : map.meterChanges)
		meters.append(
		    QJsonObject{{"bar", change.bar}, {"beatsPerBar", change.beatsPerBar}, {"beatUnit", change.beatUnit}});
	return {{"tempo", map.tempo},
	        {"beatsPerBar", map.beatsPerBar},
	        {"beatUnit", map.beatUnit},
	        {"tempoChanges", tempos},
	        {"meterChanges", meters}};
}
bool audioTempoMapFromJson(const QJsonObject &json, AudioTempoMap *map)
{
	AudioTempoMap next;
	qint64 beats = 0, unit = 0;
	if (!map || json.size() != 5 || !json["tempo"].isDouble() || !tempoValid(json["tempo"].toDouble()) ||
	    !integer(json, "beatsPerBar", 1, 32, &beats) || !integer(json, "beatUnit", 1, 32, &unit) ||
	    !meterValid(int(beats), int(unit)) || !json["tempoChanges"].isArray() || !json["meterChanges"].isArray() ||
	    json["tempoChanges"].toArray().size() > AudioTempoChangeLimit ||
	    json["meterChanges"].toArray().size() > AudioTempoChangeLimit)
		return false;
	next.tempo = json["tempo"].toDouble();
	next.beatsPerBar = int(beats);
	next.beatUnit = int(unit);
	for (const auto &value : json["tempoChanges"].toArray()) {
		const auto point = value.toObject();
		qint64 tick = 0;
		if (!value.isObject() || point.size() != 2 || !integer(point, "tick", 1, AudioMusicalTickLimit, &tick) ||
		    !point["bpm"].isDouble() || !tempoValid(point["bpm"].toDouble()))
			return false;
		next.tempoChanges.append({tick, point["bpm"].toDouble()});
	}
	for (const auto &value : json["meterChanges"].toArray()) {
		const auto point = value.toObject();
		qint64 bar = 0;
		if (!value.isObject() || point.size() != 3 || !integer(point, "bar", 2, AudioMusicalTickLimit, &bar) ||
		    !integer(point, "beatsPerBar", 1, 32, &beats) || !integer(point, "beatUnit", 1, 32, &unit) ||
		    !meterValid(int(beats), int(unit)))
			return false;
		next.meterChanges.append({bar, int(beats), int(unit)});
	}
	// Validate ordering and arithmetic at the longest possible musical range;
	// the owning session additionally validates against its actual sample rate.
	if (!validateAudioTempoMap(next, 1).isEmpty())
		return false;
	*map = std::move(next);
	return true;
}
QString audioMusicalPositionText(const AudioMusicalPosition &position)
{
	return position.bar > 0 ? QStringLiteral("%1.%2.%3").arg(position.bar).arg(position.beat).arg(position.tick)
	                        : QString();
}
bool parseAudioMusicalPosition(const QString &text, AudioMusicalPosition *position)
{
	static const QRegularExpression syntax(QStringLiteral("^([1-9][0-9]*)\\.([1-9][0-9]*)\\.([0-9]+)$"));
	const auto match = syntax.match(text);
	if (!position || !match.hasMatch())
		return false;
	bool barOk = false, beatOk = false, tickOk = false;
	AudioMusicalPosition next{match.captured(1).toLongLong(&barOk), match.captured(2).toInt(&beatOk),
	                          match.captured(3).toLongLong(&tickOk)};
	if (!barOk || !beatOk || !tickOk || next.bar > AudioMusicalTickLimit || next.beat > 32 ||
	    next.tick >= AudioTicksPerQuarter * 4)
		return false;
	*position = next;
	return true;
}
} // namespace vibestudio
