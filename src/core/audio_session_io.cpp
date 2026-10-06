#include "core/audio_session_io.h"
#include "core/audio_publication.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vibestudio
{
namespace
{
QString message(const char *text) { return QCoreApplication::translate("AudioSession", text); }
const QByteArray magic = QByteArrayLiteral("VSMIX\r\n\x1a");
constexpr qsizetype metadataLimit = 16 * 1024 * 1024;
bool fail(QString *error, const QString &text)
{
	if (error) {
		*error = text;
	}
	return false;
}
bool cancelled(const AudioWorkControl &control, QString *error)
{
	if (!control.cancelled || !control.cancelled()) {
		return false;
	}
	fail(error, message(QT_TRANSLATE_NOOP("AudioSession", "Session operation cancelled.")));
	return true;
}
QString resolved(const QString &path)
{
	const QFileInfo info(path);
	if (info.exists()) {
		return info.canonicalFilePath();
	}
	const QString parent = info.dir().canonicalPath();
	return parent.isEmpty() ? info.absoluteFilePath() : QDir(parent).filePath(info.fileName());
}
bool number(const QJsonObject &json, const char *key, double minimum, double maximum, double *value)
{
	const auto item = json.value(QLatin1String(key));
	*value = item.toDouble(std::numeric_limits<double>::quiet_NaN());
	return item.isDouble() && std::isfinite(*value) && *value >= minimum && *value <= maximum;
}
bool integer(const QJsonObject &json, const char *key, qint64 minimum, qint64 maximum, qint64 *value)
{
	double parsed = 0;
	if (!number(json, key, double(minimum), double(maximum), &parsed) || std::floor(parsed) != parsed) {
		return false;
	}
	*value = qint64(parsed);
	return true;
}
bool textField(const QJsonObject &json, const char *key, QString *value)
{
	const auto item = json.value(QLatin1String(key));
	if (!item.isString()) {
		return false;
	}
	*value = item.toString();
	return true;
}
bool boolean(const QJsonObject &json, const char *key, bool *value)
{
	const auto item = json.value(QLatin1String(key));
	if (!item.isBool()) {
		return false;
	}
	*value = item.toBool();
	return true;
}
QStringList protectedSources(const AudioSession &session)
{
	QStringList paths;
	for (const auto &source : session.sources) {
		if (!source.audio.sourcePath.isEmpty()) {
			paths << source.audio.sourcePath;
		}
	}
	return paths;
}

} // namespace

QByteArray encodeAudioSession(const AudioSession &session, QString *error, const AudioWorkControl &control)
{
	if (error) {
		error->clear();
	}
	const QString invalid = validateAudioSession(session, control);
	if (!invalid.isEmpty()) {
		fail(error, invalid);
		return {};
	}
	QJsonObject root = audioSessionSummary(session);
	QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
	QVector<QByteArray> media;
	qsizetype payloadBytes = 0;
	for (qsizetype i = 0; i < session.sources.size(); ++i) {
		auto bytes = encodeAudioProject(session.sources[i].audio, error, control);
		if (bytes.isEmpty()) {
			return {};
		}
		payloadBytes += bytes.size();
		if (payloadBytes > AudioSessionByteLimit) {
			fail(error, message(QT_TRANSLATE_NOOP("AudioSession", "Session file exceeds its storage limit.")));
			return {};
		}
		auto object = sources[i].toObject();
		object.insert(QStringLiteral("bytes"), bytes.size());
		sources[i] = object;
		media.append(std::move(bytes));
	}
	root.insert(QStringLiteral("sources"), sources);
	const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
	if (json.size() > metadataLimit || 48 + json.size() + payloadBytes > AudioSessionByteLimit) {
		fail(error, message(QT_TRANSLATE_NOOP("AudioSession", "Session metadata or total storage exceeds its limit.")));
		return {};
	}
	QByteArray result(16, '\0');
	result.replace(0, 8, magic);
	const bool haveLanes = std::any_of(session.tracks.cbegin(), session.tracks.cend(),
	                                   [](const auto &track) { return !track.takeLanes.isEmpty(); });
	qToLittleEndian<quint32>(haveLanes ? 8 : 7, result.data() + 8);
	qToLittleEndian<quint32>(quint32(json.size()), result.data() + 12);
	result.reserve(48 + json.size() + payloadBytes);
	result.append(json);
	for (const auto &bytes : media) {
		if (cancelled(control, error)) {
			return {};
		}
		result.append(bytes);
	}
	result.append(QCryptographicHash::hash(result, QCryptographicHash::Sha256));
	if (cancelled(control, error)) {
		return {};
	}
	return result;
}

bool decodeAudioSession(const QByteArray &bytes, AudioSession *session, QString *error, const AudioWorkControl &control)
{
	if (error) {
		error->clear();
	}
	if (!session || bytes.size() < 50 || bytes.size() > AudioSessionByteLimit || !bytes.startsWith(magic) ||
	    (qFromLittleEndian<quint32>(bytes.constData() + 8) != 1 &&
	     qFromLittleEndian<quint32>(bytes.constData() + 8) != 2 &&
	     qFromLittleEndian<quint32>(bytes.constData() + 8) != 3 &&
	     qFromLittleEndian<quint32>(bytes.constData() + 8) != 4 &&
	     qFromLittleEndian<quint32>(bytes.constData() + 8) != 5 &&
	     qFromLittleEndian<quint32>(bytes.constData() + 8) != 6 &&
	     qFromLittleEndian<quint32>(bytes.constData() + 8) != 7 &&
	     qFromLittleEndian<quint32>(bytes.constData() + 8) != 8)) {
		return fail(error, message(QT_TRANSLATE_NOOP("AudioSession",
		                                             "Not a supported, bounded VibeStudio multitrack session.")));
	}
	const quint32 jsonSize = qFromLittleEndian<quint32>(bytes.constData() + 12);
	const auto version = qFromLittleEndian<quint32>(bytes.constData() + 8);
	if (jsonSize > (version < 4 ? 4 * 1024 * 1024 : metadataLimit) || jsonSize > bytes.size() - 48 ||
	    QCryptographicHash::hash(QByteArrayView(bytes.constData(), bytes.size() - 32), QCryptographicHash::Sha256) !=
	        bytes.last(32)) {
		return fail(error, message(QT_TRANSLATE_NOOP("AudioSession", "Session length or checksum is invalid.")));
	}
	const auto json = QJsonDocument::fromJson(bytes.mid(16, jsonSize));
	const auto root = json.object();
	AudioSession next;
	qint64 rate = 0, beats = 0, frames = 0;
	const auto invalid = [&]() {
		return fail(error, message(QT_TRANSLATE_NOOP(
		                       "AudioSession", "Session metadata is incomplete, invalid or exceeds its limits.")));
	};
	// A new field needs a new version so opening/saving cannot discard unknown
	// arrangement or processing state. Opaque source-project metadata is retained.
	if (!json.isObject() ||
	    root.size() != (version < 3   ? 8
	                    : version < 5 ? 9
	                    : version < 6 ? 10
	                                  : 11) ||
	    !textField(root, "name", &next.name) || !integer(root, "sampleRate", 1, 384000, &rate) ||
	    !integer(root, "beatsPerBar", 1, 32, &beats) || !integer(root, "frames", 0, AudioSessionFrameLimit, &frames) ||
	    !number(root, "tempo", 20, 400, &next.musicalTime.tempo) ||
	    !number(root, "masterGainDb", -96, 24, &next.masterGainDb) || !root.value("sources").isArray() ||
	    root.value("sources").toArray().size() > AudioSessionSourceLimit || !root.value("tracks").isArray() ||
	    root.value("tracks").toArray().size() > AudioSessionTrackLimit) {
		return invalid();
	}
	next.sampleRate = int(rate);
	next.musicalTime.beatsPerBar = int(beats);
	if (version >= 5) {
		auto timing = root.value("timing").toObject();
		if (!root.value("timing").isObject() || timing.size() != 3 || timing.contains("tempo") ||
		    timing.contains("beatsPerBar"))
			return invalid();
		timing.insert("tempo", next.musicalTime.tempo);
		timing.insert("beatsPerBar", next.musicalTime.beatsPerBar);
		if (!audioTempoMapFromJson(timing, &next.musicalTime))
			return invalid();
	}
	if (version >= 6) {
		const auto groups = root.value("groups");
		if (!groups.isArray() || groups.toArray().size() > AudioSessionGroupLimit)
			return invalid();
		for (const auto &value : groups.toArray()) {
			const auto item = value.toObject();
			AudioSessionGroup group;
			if (!value.isObject() || item.size() != 2 || !textField(item, "id", &group.id) ||
			    !textField(item, "name", &group.name))
				return invalid();
			next.groups.append(group);
		}
	}
	if (version >= 3) {
		const auto effects = root.value("effects").toObject();
		if (effects.size() != (version == 3 ? 2 : 3) ||
		    !audioEffectsFromJson(effects.value("master"), &next.masterEffects) ||
		    (version >= 4 &&
		     !audioEffectAutomationFromJson(effects.value("automation"), &next.masterEffectAutomation, version < 7)) ||
		    !number(effects, "tailSeconds", 0, 60, &next.effectTailSeconds))
			return invalid();
	}
	qsizetype offset = 16 + jsonSize;
	qint64 totalSamples = 0;
	for (const auto &value : root.value("sources").toArray()) {
		const auto object = value.toObject();
		AudioSessionSource source;
		qint64 length = 0, sourceFrames = 0, channels = 0;
		QString sourceName, sourcePath;
		if (!value.isObject() || object.size() != 6 || !textField(object, "id", &source.id) ||
		    !textField(object, "name", &sourceName) || !textField(object, "path", &sourcePath) ||
		    !integer(object, "bytes", 52, AudioProjectByteLimit, &length) ||
		    !integer(object, "channels", 1, 2, &channels) ||
		    !integer(object, "frames", 1, AudioSampleLimit / channels, &sourceFrames) ||
		    length > bytes.size() - 32 - offset || sourceFrames * channels > AudioSessionSampleLimit - totalSamples) {
			return invalid();
		}
		totalSamples += sourceFrames * channels;
		if (!decodeAudioProject(bytes.mid(offset, length), &source.audio, error, control)) {
			return false;
		}
		if (source.audio.clip.channels != channels || source.audio.clip.frameCount() != sourceFrames ||
		    source.audio.sourceName != sourceName || source.audio.sourcePath != sourcePath) {
			return invalid();
		}
		offset += length;
		next.sources.append(std::move(source));
	}
	if (offset != bytes.size() - 32) {
		return invalid();
	}
	int regionCount = 0;
	for (const auto &value : root.value("tracks").toArray()) {
		const auto object = value.toObject();
		AudioSessionTrack track;
		if (!value.isObject() ||
		    object.size() != (version == 1   ? 9
		                      : version == 2 ? 10
		                      : version == 3 ? 11
		                      : version < 8  ? 12
		                                     : 13) ||
		    !textField(object, "id", &track.id) || !textField(object, "name", &track.name) ||
		    !number(object, "gainDb", -96, 24, &track.gainDb) || !number(object, "pan", -1, 1, &track.pan) ||
		    !boolean(object, "muted", &track.muted) || !boolean(object, "solo", &track.solo) ||
		    !audioAutomationFromJson(object.value("gainAutomation"), &track.gainAutomation, -96, 24, version < 4,
		                             version < 7) ||
		    !audioAutomationFromJson(object.value("panAutomation"), &track.panAutomation, -1, 1, version < 4,
		                             version < 7) ||
		    !object.value("regions").isArray()) {
			return invalid();
		}
		if (version >= 2 && (!object.value("routing").isObject() ||
		                     !audioRoutingFromJson(object.value("routing").toObject(), &track.routing)))
			return invalid();
		if (version >= 3 && !audioEffectsFromJson(object.value("effects"), &track.effects))
			return invalid();
		if (version >= 4 &&
		    !audioEffectAutomationFromJson(object.value("effectAutomation"), &track.effectAutomation, version < 7))
			return invalid();
		const auto readRegions = [&](const QJsonValue &values, QVector<AudioSessionRegion> *regions) {
			if (!values.isArray())
				return false;
			for (const auto &regionValue : values.toArray()) {
				if (++regionCount > AudioSessionRegionLimit || !regionValue.isObject()) {
					return false;
				}
				const auto item = regionValue.toObject();
				AudioSessionRegion region;
				if (item.size() != (version < 6 ? 10 : 13) || !textField(item, "id", &region.id) ||
				    !textField(item, "name", &region.name) || !textField(item, "sourceId", &region.sourceId) ||
				    !integer(item, "position", 0, AudioSessionFrameLimit, &region.position) ||
				    !integer(item, "sourceOffset", 0, AudioSampleLimit, &region.sourceOffset) ||
				    !integer(item, "length", 1, AudioSampleLimit, &region.length) ||
				    !integer(item, "fadeIn", 0, AudioSampleLimit, &region.fadeIn) ||
				    !integer(item, "fadeOut", 0, AudioSampleLimit, &region.fadeOut) ||
				    !number(item, "gainDb", -96, 24, &region.gainDb) || !boolean(item, "muted", &region.muted)) {
					return false;
				}
				if (version >= 6 && (!textField(item, "groupId", &region.groupId) ||
				                     !integer(item, "fadeStart", 0, AudioSampleLimit, &region.fadeStart) ||
				                     !integer(item, "fadeSpan", 0, AudioSampleLimit, &region.fadeSpan)))
					return false;
				regions->append(region);
			}
			return true;
		};
		if (!readRegions(object.value("regions"), &track.regions))
			return invalid();
		if (version >= 8) {
			const auto lanes = object.value("takeLanes");
			if (!lanes.isArray() || lanes.toArray().size() > AudioSessionTakeLaneLimit)
				return invalid();
			for (const auto &value : lanes.toArray()) {
				const auto item = value.toObject();
				AudioSessionTakeLane lane;
				if (!value.isObject() || item.size() != 3 || !textField(item, "id", &lane.id) ||
				    !textField(item, "name", &lane.name) || !readRegions(item.value("regions"), &lane.regions))
					return invalid();
				track.takeLanes.append(lane);
			}
		}

		next.tracks.append(track);
	}
	const QString issue = validateAudioSession(next, control);
	if (!issue.isEmpty()) {
		return fail(error, issue);
	}
	if (frames != audioSessionFrames(next)) {
		return invalid();
	}
	*session = std::move(next);
	return true;
}

bool readAudioSession(const QString &path, AudioSession *session, AudioProjectIdentity *identity, QString *error,
                      const AudioWorkControl &control)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > AudioSessionByteLimit) {
		return fail(error, message(QT_TRANSLATE_NOOP("AudioSession", "Session file is unreadable or too large.")));
	}
	const auto bytes = file.read(AudioSessionByteLimit + 1);
	if (file.error() != QFileDevice::NoError) {
		return fail(error, file.errorString());
	}
	if (!decodeAudioSession(bytes, session, error, control)) {
		return false;
	}
	if (identity) {
		*identity = {QFileInfo(path).absoluteFilePath(), resolved(path),
		             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)};
	}
	return true;
}

AudioProjectSaveReport writeAudioSession(const AudioSession &session, const AudioProjectSaveRequest &request,
                                         const AudioWorkControl &control, const QStringList &protectedPaths)
{
	return commitAudioOutput(request, QStringLiteral("vssession"), protectedSources(session) + protectedPaths,
	                         AudioSessionByteLimit, control, [&](const AudioOutputEmit &write, QString *error) {
		                         const auto bytes = encodeAudioSession(session, error, control);
		                         return !bytes.isEmpty() && write(bytes);
	                         });
}

AudioSessionMixdownReport writeAudioSessionMixdown(const AudioSession &session, const AudioSessionMixdown &request,
                                                   const AudioWorkControl &control)
{
	AudioSessionMixdownReport report;
	AudioSessionRenderer renderer;
	if (!renderer.prepare(session, &report.saved.error, control, request.target)) {
		return report;
	}
	report.processingLatencyFrames = renderer.processingLatencyFrames();
	const qint64 end = request.end == -1 ? renderer.frameCount() : request.end;
	if (request.first < 0 || end <= request.first || end > AudioSessionFrameLimit) {
		report.saved.error = message(QT_TRANSLATE_NOOP("AudioSession", "Select a nonempty session mix range."));
		return report;
	}
	const int bits = audioWavBits(request.format);
	if (bits == 0 || (request.dither && request.format == AudioWavFormat::Float32)) {
		report.saved.error = message(QT_TRANSLATE_NOOP("AudioSession", "Unsupported mixdown precision."));
		return report;
	}
	AudioWavOptions options;
	options.format = request.format;
	options.dither = request.dither;
	options.ditherSeed = request.ditherSeed;
	AudioWavEncoder encoder(options);
	QByteArray header = encodeAudioWav(AudioClip{2, session.sampleRate, {0, 0}}, options, &report.saved.error);
	if (header.isEmpty()) {
		return report;
	}
	// Inspect the existing encoder's canonical chunks instead of duplicating its
	// RIFF/WAVE format definition (specification credits live with audio_export).
	int dataOffset = -1, factOffset = -1;
	for (int pos = 12; pos + 8 <= header.size();) {
		const quint32 size = qFromLittleEndian<quint32>(header.constData() + pos + 4);
		if (header.mid(pos, 4) == "fact") {
			factOffset = pos + 8;
		}
		if (header.mid(pos, 4) == "data") {
			dataOffset = pos + 8;
			break;
		}
		if (size > quint32(header.size() - pos - 8)) {
			break;
		}
		pos += 8 + int(size) + int(size & 1);
	}
	const qint64 frames = end - request.first, dataBytes = frames * 2 * (bits / 8);
	if (dataOffset < 0 || dataBytes + dataOffset - 8 > std::numeric_limits<quint32>::max()) {
		report.saved.error = message(
		    QT_TRANSLATE_NOOP("AudioSession", "This mix exceeds the RIFF WAV size limit. Export a shorter range."));
		return report;
	}
	header.truncate(dataOffset);
	qToLittleEndian<quint32>(quint32(dataBytes + dataOffset - 8), header.data() + 4);
	qToLittleEndian<quint32>(quint32(dataBytes), header.data() + dataOffset - 4);
	if (factOffset >= 0) {
		qToLittleEndian<quint32>(quint32(frames), header.data() + factOffset);
	}
	report.saved = commitAudioOutput(
	    request.output, QStringLiteral("wav"), protectedSources(session) + request.protectedPaths,
	    qint64(std::numeric_limits<quint32>::max()) + 8, control, [&](const AudioOutputEmit &write, QString *error) {
		    if (!write(header)) {
			    return false;
		    }
		    for (qint64 position = request.first; position < end;) {
			    const int count = int(std::min<qint64>(16384, end - position));
			    const auto block = renderer.renderBlock(position, count, control);
			    if (!block.succeeded()) {
				    return fail(error, block.cancelled
				                           ? message(QT_TRANSLATE_NOOP("AudioSession", "Session operation cancelled."))
				                           : block.error);
			    }
			    for (float value : block.clip.samples) {
				    report.peak = std::max(report.peak, std::abs(double(value)));
				    if (std::abs(value) > 1) {
					    ++report.samplesAboveFullScale;
				    }
			    }
			    const auto bytes = encoder.encode(block.clip, error, control);
			    if (bytes.isEmpty() || !write(bytes.mid(dataOffset, count * 2 * (bits / 8)))) {
				    return false;
			    }
			    position += count;
			    if (request.progress) {
				    request.progress(position - request.first, frames);
			    }
		    }
		    return true;
	    });
	if (report.saved.succeeded) {
		report.frames = frames;
	}
	return report;
}

} // namespace vibestudio
