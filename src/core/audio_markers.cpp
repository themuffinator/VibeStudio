#include "core/audio_markers.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}
void sortCues(AudioMarkers& markers)
{
	std::sort(markers.cues.begin(), markers.cues.end(),
	          [](const auto& a, const auto& b) { return a.frame == b.frame ? a.id < b.id : a.frame < b.frame; });
}
bool integer(const QJsonValue& value, qint64 maximum, qint64* result)
{
	const double number = value.toDouble(-1);
	if (!value.isDouble() || !std::isfinite(number) || number < 0 || number > maximum || std::floor(number) != number) {
		return false;
	}
	*result = qint64(number);
	return true;
}
quint32 u32(const QByteArray& bytes, qsizetype offset)
{
	return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
void append32(QByteArray& bytes, quint32 value)
{
	const auto at = bytes.size();
	bytes.resize(at + 4);
	qToLittleEndian(value, bytes.data() + at);
}
QByteArray chunk(const char* id, const QByteArray& payload)
{
	QByteArray result(id, 4);
	append32(result, quint32(payload.size()));
	result += payload;
	if (payload.size() & 1) {
		result += '\0';
	}
	return result;
}
} // namespace

QString validateAudioMarkers(const AudioMarkers& markers, qint64 frames)
{
	if (markers.cues.size() > AudioCueLimit) {
		return QCoreApplication::translate("AudioMarkers", "A sound supports at most 256 cue markers.");
	}
	QSet<quint32> ids;
	qsizetype nameBytes = 0;
	for (const auto& cue : markers.cues) {
		nameBytes += cue.name.toUtf8().size();
		if (cue.frame < 0 || cue.frame >= frames || ids.contains(cue.id) || cue.name.size() > AudioCueNameLimit ||
		    cue.name.contains(QChar(0)) || QString::fromUtf8(cue.name.toUtf8()) != cue.name || nameBytes > 16 * 1024) {
			return QCoreApplication::translate(
			    "AudioMarkers", "Cue IDs must be unique; frames must name existing samples. Names allow 128 "
			                    "UTF-16 code units each and 16 KiB of UTF-8 text in total, without null characters.");
		}
		ids.insert(cue.id);
	}
	if (markers.loop &&
	    (markers.loop->first < 0 || markers.loop->end <= markers.loop->first || markers.loop->end > frames)) {
		return QCoreApplication::translate("AudioMarkers",
		                                   "The forward loop requires a non-empty frame range inside the sound.");
	}
	return {};
}

QJsonObject audioMarkersJson(const AudioMarkers& markers)
{
	QJsonArray cues;
	for (const auto& cue : markers.cues) {
		cues.append(QJsonObject{{QStringLiteral("id"), qint64(cue.id)},
		                        {QStringLiteral("frame"), cue.frame},
		                        {QStringLiteral("name"), cue.name}});
	}
	QJsonObject result{{QStringLiteral("cues"), cues}, {QStringLiteral("loop"), QJsonValue::Null}};
	if (markers.loop) {
		result.insert(QStringLiteral("loop"), QJsonObject{{QStringLiteral("first"), markers.loop->first},
		                                                  {QStringLiteral("end"), markers.loop->end}});
	}
	return result;
}

bool parseAudioMarkersJson(const QJsonObject& json, qint64 frames, AudioMarkers* markers, QString* error)
{
	if (error) {
		error->clear();
	}
	AudioMarkers result;
	const auto bad = [&]() {
		return fail(error, QCoreApplication::translate("AudioMarkers", "Invalid audio marker metadata."));
	};
	if (!markers || json.size() != 2 || !json.value(QStringLiteral("cues")).isArray() ||
	    json.value(QStringLiteral("cues")).toArray().size() > AudioCueLimit || !json.contains(QStringLiteral("loop"))) {
		return bad();
	}
	for (const auto& value : json.value(QStringLiteral("cues")).toArray()) {
		const auto cue = value.toObject();
		qint64 id = 0, frame = 0;
		if (!value.isObject() || cue.size() != 3 ||
		    !integer(cue.value(QStringLiteral("id")), std::numeric_limits<quint32>::max(), &id) ||
		    !integer(cue.value(QStringLiteral("frame")), frames, &frame) ||
		    !cue.value(QStringLiteral("name")).isString()) {
			return bad();
		}
		result.cues.append({quint32(id), frame, cue.value(QStringLiteral("name")).toString()});
	}
	const auto loop = json.value(QStringLiteral("loop"));
	if (!loop.isNull()) {
		qint64 first = 0, end = 0;
		if (!loop.isObject() || loop.toObject().size() != 2 ||
		    !integer(loop.toObject().value(QStringLiteral("first")), frames, &first) ||
		    !integer(loop.toObject().value(QStringLiteral("end")), frames, &end)) {
			return bad();
		}
		result.loop = AudioLoop{first, end};
	}
	const QString problem = validateAudioMarkers(result, frames);
	if (!problem.isEmpty()) {
		return fail(error, problem);
	}
	sortCues(result);
	*markers = result;
	return true;
}

AudioMarkers trimAudioMarkers(const AudioMarkers& markers, qint64 first, qint64 end)
{
	AudioMarkers result;
	for (auto cue : markers.cues) {
		if (cue.frame >= first && cue.frame < end) {
			cue.frame -= first;
			result.cues.append(cue);
		}
	}
	if (markers.loop) {
		const auto a = std::max(first, markers.loop->first), b = std::min(end, markers.loop->end);
		if (b > a) {
			result.loop = AudioLoop{a - first, b - first};
		}
	}
	sortCues(result);
	return result;
}

AudioMarkers replaceAudioMarkers(const AudioMarkers& markers, qint64 first, qint64 end, qint64 insertedFrames)
{
	AudioMarkers result;
	const qint64 delta = insertedFrames - (end - first);
	for (auto cue : markers.cues) {
		if (cue.frame >= first && cue.frame < end) {
			continue;
		}
		if (cue.frame >= end) {
			cue.frame += delta;
		}
		result.cues.append(cue);
	}
	if (markers.loop) {
		auto loop = *markers.loop;
		if (loop.first >= end) {
			loop.first += delta;
			loop.end += delta;
		} else if (loop.end > first) {
			loop.first = std::min(loop.first, first);
			loop.end = loop.end > end ? loop.end + delta : first + insertedFrames;
		}
		if (loop.end > loop.first) {
			result.loop = loop;
		}
	}
	sortCues(result);
	return result;
}

AudioMarkers reverseAudioMarkers(const AudioMarkers& markers, qint64 first, qint64 end)
{
	auto result = markers;
	for (auto& cue : result.cues) {
		if (cue.frame >= first && cue.frame < end) {
			cue.frame = first + end - 1 - cue.frame;
		}
	}
	if (result.loop) {
		const auto loop = *result.loop;
		if (loop.first >= first && loop.end <= end) {
			result.loop = AudioLoop{first + end - loop.end, first + end - loop.first};
		} else if (loop.first < end && loop.end > first && !(loop.first <= first && loop.end >= end)) {
			result.loop.reset();
		}
	}
	sortCues(result);
	return result;
}

AudioMarkers resampleAudioMarkers(const AudioMarkers& markers, qint64 oldFrames, qint64 newFrames, int oldRate,
                                  int newRate)
{
	AudioMarkers result = markers;
	const auto mapped = [&](qint64 frame) {
		return frame == oldFrames ? newFrames : std::min(newFrames, (frame * newRate + oldRate / 2) / oldRate);
	};
	for (auto& cue : result.cues) {
		cue.frame = std::min(std::max<qint64>(0, newFrames - 1), mapped(cue.frame));
	}
	if (result.loop) {
		result.loop = AudioLoop{mapped(result.loop->first), mapped(result.loop->end)};
		if (result.loop->end <= result.loop->first) {
			result.loop.reset();
		}
	}
	sortCues(result);
	return result;
}

AudioMarkers mergeAudioMarkers(AudioMarkers target, const AudioMarkers& incoming, qint64 offset)
{
	QSet<quint32> ids;
	for (const auto& cue : target.cues) {
		ids.insert(cue.id);
	}
	for (auto cue : incoming.cues) {
		if (ids.contains(cue.id)) {
			cue.id = 1;
			while (ids.contains(cue.id)) {
				++cue.id;
			}
		}
		ids.insert(cue.id);
		cue.frame += offset;
		target.cues.append(cue);
	}
	if (!target.loop && incoming.loop) {
		target.loop = AudioLoop{incoming.loop->first + offset, incoming.loop->end + offset};
	}
	sortCues(target);
	return target;
}

// Original implementation of Microsoft's RIFF 1991 and RIFFNEW 1994 layouts.
// Quake/II's first cue and adtl/ltxt "mark" behavior is referenced from id's
// GPL-2.0-or-later snd_mem.c; no engine code copied. See docs/CREDITS.md.
QByteArray encodeWavAudioMarkers(const AudioMarkers& markers, qint64 frames, int sampleRate, AudioWavMarkers mode,
                                 QString* error)
{
	if (error) {
		error->clear();
	}
	const QString problem = validateAudioMarkers(markers, frames);
	if (mode != AudioWavMarkers::Standard && mode != AudioWavMarkers::Quake && mode != AudioWavMarkers::Omit) {
		fail(error, QCoreApplication::translate("AudioMarkers", "Invalid WAV marker output mode."));
		return {};
	}
	if (!problem.isEmpty() || sampleRate < 1) {
		fail(error,
		     problem.isEmpty() ? QCoreApplication::translate("AudioMarkers", "Invalid marker sample rate.") : problem);
		return {};
	}
	if (mode == AudioWavMarkers::Omit || markers.empty()) {
		return {};
	}
	if (mode == AudioWavMarkers::Quake && !markers.loop) {
		return {};
	} // First cue would otherwise enable an unintended engine loop.
	QByteArray result;
	QVector<AudioCue> cues = markers.cues;
	quint32 loopId = 1;
	QSet<quint32> ids;
	for (const auto& cue : cues) {
		ids.insert(cue.id);
	}
	while (ids.contains(loopId)) {
		++loopId;
	}
	if (markers.loop) {
		QByteArray sampler(36, '\0');
		qToLittleEndian<quint32>(quint32((1000000000LL + sampleRate / 2) / sampleRate), sampler.data() + 8);
		qToLittleEndian<quint32>(60, sampler.data() + 12);
		qToLittleEndian<quint32>(1, sampler.data() + 28);
		for (quint32 value : {loopId, 0u, quint32(markers.loop->first), quint32(markers.loop->end - 1), 0u, 0u}) {
			append32(sampler, value);
		}
		result += chunk("smpl", sampler);
		if (mode == AudioWavMarkers::Quake) {
			cues.prepend({loopId, markers.loop->first, {}});
		}
	}
	if (!cues.isEmpty()) {
		QByteArray charset(8, '\0');
		qToLittleEndian<quint16>(65001, charset.data());
		result += chunk("CSET", charset);
		QByteArray points;
		append32(points, quint32(cues.size()));
		QByteArray associated("adtl", 4);
		if (mode == AudioWavMarkers::Quake) {
			QByteArray length;
			append32(length, loopId);
			append32(length, quint32(markers.loop->end - markers.loop->first));
			length += QByteArrayLiteral("mark");
			length += QByteArray(8, '\0');
			associated += chunk("ltxt", length);
		}
		for (const auto& cue : cues) {
			append32(points, cue.id);
			append32(points, quint32(cue.frame));
			points += QByteArrayLiteral("data");
			append32(points, 0);
			append32(points, 0);
			append32(points, quint32(cue.frame));
			if (!cue.name.isEmpty()) {
				QByteArray label;
				append32(label, cue.id);
				label += cue.name.toUtf8();
				label += '\0';
				associated += chunk("labl", label);
			}
		}
		result += chunk("cue ", points);
		if (associated.size() > 4) {
			result += chunk("LIST", associated);
		}
	}
	return result;
}

bool decodeWavAudioMarkers(const QByteArray& wav, qint64 frames, AudioMarkers* markers, QString* error,
                           AudioWavMarkers* sourceMode)
{
	if (error) {
		error->clear();
	}
	const auto bad = [&]() {
		return fail(error, QCoreApplication::translate(
		                       "AudioMarkers", "WAV markers are malformed, exceed limits, or use unsupported loop/text "
		                                       "metadata. Supported loops are one forward infinite sustain range."));
	};
	if (!markers || wav.size() < 12 || wav.first(4) != "RIFF" || wav.mid(8, 4) != "WAVE" ||
	    qint64(u32(wav, 4)) + 8 > wav.size()) {
		return bad();
	}
	AudioMarkers result;
	QMap<quint32, QByteArray> labels;
	QMap<quint32, quint32> loopLengths;
	std::optional<quint32> firstCue;
	bool seenCues = false, seenSampler = false, seenCharset = false;
	quint16 codePage = 0;
	quint16 blockAlign = 0;
	QVector<QPair<quint32, quint32>> blockOffsets;
	qint64 markerBytes = 0;
	const qint64 end = qint64(u32(wav, 4)) + 8;
	for (qint64 at = 12; at < end;) {
		if (at + 8 > end) {
			return bad();
		}
		const qint64 size = u32(wav, at + 4), data = at + 8, next = data + size + (size & 1);
		if (next > end) {
			return bad();
		}
		const auto id = wav.mid(at, 4);
		if (id == "cue " || id == "smpl" || id == "CSET" || (id == "LIST" && size >= 4 && wav.mid(data, 4) == "adtl")) {
			markerBytes += size + 8;
			if (markerBytes > AudioMarkerByteLimit) {
				return bad();
			}
		}
		if (id == "fmt " && size >= 16) {
			blockAlign = qFromLittleEndian<quint16>(wav.constData() + data + 12);
		} else if (id == "CSET") {
			if (seenCharset || size != 8) {
				return bad();
			}
			seenCharset = true;
			codePage = qFromLittleEndian<quint16>(wav.constData() + data);
		} else if (id == "cue ") {
			if (seenCues || size < 4) {
				return bad();
			}
			seenCues = true;
			const auto count = u32(wav, data);
			if (count > AudioCueLimit + 1 || size != 4 + qint64(count) * 24) {
				return bad();
			}
			for (quint32 index = 0; index < count; ++index) {
				const qint64 p = data + 4 + index * 24;
				if (wav.mid(p + 8, 4) != "data" || u32(wav, p + 12) != 0) {
					return bad();
				}
				if (u32(wav, p + 16) != 0) {
					blockOffsets.append({u32(wav, p + 20), u32(wav, p + 16)});
				}
				if (index == 0) {
					firstCue = u32(wav, p);
				}
				result.cues.append({u32(wav, p), u32(wav, p + 20), {}});
			}
		} else if (id == "smpl") {
			if (seenSampler || size < 36) {
				return bad();
			}
			seenSampler = true;
			const auto count = u32(wav, data + 28);
			if (count > 1 || size != 36 + count * 24 || u32(wav, data + 32) != 0) {
				return bad();
			}
			if (count) {
				if (u32(wav, data + 40) || u32(wav, data + 52) || u32(wav, data + 56)) {
					return bad();
				}
				result.loop = AudioLoop{u32(wav, data + 44), qint64(u32(wav, data + 48)) + 1};
			}
		} else if (id == "LIST" && size >= 4 && wav.mid(data, 4) == "adtl") {
			for (qint64 sub = data + 4; sub < data + size;) {
				if (sub + 8 > data + size) {
					return bad();
				}
				const qint64 length = u32(wav, sub + 4), payload = sub + 8, after = payload + length + (length & 1);
				if (after > data + size) {
					return bad();
				}
				if (wav.mid(sub, 4) == "labl") {
					if (length < 5 || length > 4 + AudioCueNameLimit * 4 + 1 || labels.contains(u32(wav, payload)) ||
					    wav[payload + length - 1] != '\0') {
						return bad();
					}
					labels.insert(u32(wav, payload), wav.mid(payload + 4, length - 5));
				} else if (wav.mid(sub, 4) == "ltxt" && length >= 20 && wav.mid(payload + 8, 4) == "mark") {
					if (loopLengths.contains(u32(wav, payload))) {
						return bad();
					}
					loopLengths.insert(u32(wav, payload), u32(wav, payload + 4));
				}
				sub = after;
			}
		}
		at = next;
	}
	if (!labels.isEmpty() && codePage != 0 && codePage != 1004 && codePage != 28591 && codePage != 65001) {
		return bad();
	}
	// The 1994 PCM convention records the same position as aligned bytes and
	// sample offset; the original convention leaves dwBlockStart zero. Reject
	// conflicting byte positions, and allow fmt after cue in the RIFF order.
	for (const auto& [frame, offset] : blockOffsets) {
		if (blockAlign == 0 || quint64(frame) * blockAlign != offset) {
			return bad();
		}
	}
	QSet<quint32> ids;
	for (auto& cue : result.cues) {
		if (ids.contains(cue.id)) {
			return bad();
		}
		ids.insert(cue.id);
		const auto text = labels.take(cue.id);
		cue.name = codePage == 65001 ? QString::fromUtf8(text) : QString::fromLatin1(text);
		if (codePage == 65001 && cue.name.toUtf8() != text) {
			return bad();
		}
	}
	if (!labels.isEmpty()) {
		return bad();
	}
	if (!loopLengths.isEmpty()) {
		if (loopLengths.size() != 1 || !firstCue || !loopLengths.contains(*firstCue) || result.cues.isEmpty()) {
			return bad();
		}
		const AudioLoop legacy{result.cues.first().frame, result.cues.first().frame + loopLengths.value(*firstCue)};
		if (result.loop && *result.loop != legacy) {
			return bad();
		}
		result.loop = legacy;
		// The first cue describes the legacy loop. Keep it as an authored cue
		// only when it has a label; unlabeled loop sentinels are not duplicated.
		if (result.cues.first().name.isEmpty()) {
			result.cues.removeFirst();
		}
	}
	const QString problem = validateAudioMarkers(result, frames);
	if (!problem.isEmpty()) {
		return fail(error, problem);
	}
	sortCues(result);
	*markers = result;
	if (sourceMode) {
		*sourceMode = loopLengths.isEmpty() ? AudioWavMarkers::Standard : AudioWavMarkers::Quake;
	}
	return true;
}
} // namespace vibestudio
