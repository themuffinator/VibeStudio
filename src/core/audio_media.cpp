#include "core/audio_media.h"
#include "core/audio_resample.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cstring>

namespace vibestudio
{
namespace
{
bool stopped(const AudioWorkControl &control) { return control.cancelled && control.cancelled(); }
QString issue(const char *text) { return QCoreApplication::translate("AudioMedia", text); }
bool sameSamples(const AudioClip &a, const AudioClip &b, const AudioWorkControl &control)
{
	if (a.sampleRate != b.sampleRate || a.channels != b.channels || a.samples.size() != b.samples.size())
		return false;
	for (qsizetype i = 0; i < a.samples.size(); i += 4096) {
		if (stopped(control))
			return false;
		const auto count = std::min(qsizetype(4096), a.samples.size() - i);
		if (std::memcmp(a.samples.constData() + i, b.samples.constData() + i, size_t(count) * sizeof(float)))
			return false;
	}
	return true;
}
bool readFile(const QString &path, QByteArray *bytes, AudioProjectIdentity *identity, QString *error,
              const AudioWorkControl &control)
{
	const QFileInfo before(path);
	QFile file(path);
	if (!before.isFile() || !file.open(QIODevice::ReadOnly) || file.size() < 1 || file.size() > AudioInputByteLimit) {
		*error = issue(QT_TRANSLATE_NOOP("AudioMedia", "Choose a readable, regular audio file of at most 128 MiB."));
		return false;
	}
	const auto canonical = before.canonicalFilePath();
	QCryptographicHash hash(QCryptographicHash::Sha256);
	qint64 read = 0;
	while (!file.atEnd()) {
		if (stopped(control))
			return false;
		const auto block = file.read(1024 * 1024);
		if (block.isEmpty() || file.error() != QFileDevice::NoError || block.size() > AudioInputByteLimit - read) {
			*error = issue(
			    QT_TRANSLATE_NOOP("AudioMedia", "The input could not be read completely or exceeded its size limit."));
			return false;
		}
		read += block.size();
		hash.addData(block);
		if (bytes)
			bytes->append(block);
	}
	const QFileInfo after(path);
	if (read != before.size() || !after.isFile() || after.size() != before.size() ||
	    after.lastModified() != before.lastModified() || after.canonicalFilePath() != canonical) {
		*error = issue(QT_TRANSLATE_NOOP("AudioMedia", "The input changed while it was being read. Review it again."));
		return false;
	}
	*identity = {before.absoluteFilePath(), canonical, hash.result()};
	return !stopped(control);
}
} // namespace

AudioMediaInventory inspectAudioMedia(const AudioSession &session, const AudioWorkControl &control)
{
	AudioMediaInventory result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty())
		return result;
	for (const auto &source : session.sources) {
		if (stopped(control)) {
			result.cancelled = true;
			result.sources.clear();
			return result;
		}
		AudioMediaUsage item;
		item.id = source.id;
		item.name = source.audio.sourceName;
		item.path = source.audio.sourcePath;
		item.channels = source.audio.clip.channels;
		item.frames = source.audio.clip.frameCount();
		item.bytes = source.audio.clip.samples.size() * qint64(sizeof(float));
		const QFileInfo file(item.path);
		item.availability = item.path.isEmpty()  ? "embedded-only"
		                    : !file.exists()     ? "missing"
		                    : !file.isFile()     ? "not-file"
		                    : !file.isReadable() ? "unreadable"
		                                         : "available";
		for (const auto &track : session.tracks)
			visitAudioTrackRegions(track, [&](const auto &region, bool retained) {
				if (region.sourceId == source.id) {
					++item.clips;
					if (retained)
						++item.takeClips;
					item.requiredFrames = std::max(item.requiredFrames, region.sourceOffset + region.length);
					if (!item.tracks.contains(track.id))
						item.tracks.append(track.id);
				}
			});
		result.sources.append(item);
	}
	result.cancelled = stopped(control);
	if (result.cancelled)
		result.sources.clear();
	return result;
}
QJsonObject audioMediaInventoryJson(const AudioMediaInventory &inventory)
{
	QJsonArray sources;
	qint64 bytes = 0, unusedBytes = 0;
	for (const auto &item : inventory.sources) {
		bytes += item.bytes;
		if (!item.clips)
			unusedBytes += item.bytes;
		sources.append(QJsonObject{{"id", item.id},
		                           {"name", item.name},
		                           {"path", item.path},
		                           {"availability", item.availability},
		                           {"channels", item.channels},
		                           {"frames", item.frames},
		                           {"sampleBytes", item.bytes},
		                           {"clips", item.clips},
		                           {"takeClips", item.takeClips},
		                           {"requiredFrames", item.requiredFrames},
		                           {"tracks", QJsonArray::fromStringList(item.tracks)}});
	}
	return {{"sources", sources}, {"sampleBytes", bytes}, {"unusedSampleBytes", unusedBytes}};
}
AudioMediaCandidate readAudioMediaCandidate(const QString &path, const AudioWorkControl &control)
{
	AudioMediaCandidate result;
	QByteArray bytes;
	if (!readFile(path, &bytes, &result.identity, &result.error, control)) {
		result.cancelled = stopped(control);
		return result;
	}
	if (bytes.startsWith(QByteArrayLiteral("VSAUD\r\n\x1a"))) {
		if (!decodeAudioProject(bytes, &result.audio, &result.error, control)) {
			result.cancelled = stopped(control);
			return result;
		}
	} else {
		const auto decoded = decodeAudioClip(path, bytes, control);
		if (!decoded.succeeded()) {
			result.error = decoded.error;
			result.cancelled = decoded.cancelled;
			return result;
		}
		result.audio.clip = decoded.clip;
		result.audio.endFrame = decoded.clip.frameCount();
		result.audio.sourceName = QFileInfo(path).fileName();
	}
	// The reviewed file itself is the new provenance, including native projects.
	result.audio.sourcePath = result.identity.canonicalPath;
	if (result.audio.clip.channels != 1 && result.audio.clip.channels != 2)
		result.error = issue(QT_TRANSLATE_NOOP("AudioMedia", "Session media requires mono or stereo audio."));
	if (result.audio.clip.samples.isEmpty())
		result.error = issue(QT_TRANSLATE_NOOP("AudioMedia", "Session sources cannot be empty."));
	result.cancelled = stopped(control);
	return result;
}
AudioMediaResult editAudioMedia(const AudioSession &session, const AudioMediaEdit &edit,
                                const AudioMediaCandidate &candidate, bool verifyFile, const AudioWorkControl &control)
{
	AudioMediaResult result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty())
		return result;
	const auto fail = [&](const QString &text) {
		result.error = text;
		return result;
	};
	if (!QStringList{"rename", "relink", "replace", "remove", "prune"}.contains(edit.operation))
		return fail(issue(QT_TRANSLATE_NOOP("AudioMedia", "Unknown media operation.")));
	const bool replacing = edit.operation == "replace", relinking = edit.operation == "relink";
	if ((!replacing && edit.resample) || (edit.operation != "rename" && !edit.name.isEmpty()))
		return fail(issue(QT_TRANSLATE_NOOP("AudioMedia", "These options do not apply to the media operation.")));
	QSet<QString> ids(edit.sourceIds.cbegin(), edit.sourceIds.cend()), found;
	if (edit.operation == "prune"
	        ? !ids.isEmpty()
	        : ids.isEmpty() || ids.size() != edit.sourceIds.size() || (edit.operation != "remove" && ids.size() != 1))
		return fail(
		    issue(QT_TRANSLATE_NOOP("AudioMedia", "Choose distinct existing sources, or prune all unused sources.")));
	QSet<QString> used;
	for (const auto &track : session.tracks)
		visitAudioTrackRegions(track, [&](const auto &region, bool) { used.insert(region.sourceId); });
	for (const auto &source : session.sources) {
		if (edit.operation == "prune" && !used.contains(source.id))
			ids.insert(source.id);
		if (ids.contains(source.id))
			found.insert(source.id);
	}
	if (found != ids)
		return fail(issue(QT_TRANSLATE_NOOP("AudioMedia", "A selected source no longer exists.")));
	if (edit.operation == "remove")
		for (const auto &id : ids)
			if (used.contains(id))
				return fail(issue(QT_TRANSLATE_NOOP(
				    "AudioMedia", "Used sources cannot be removed. Remove or replace their clips first.")));
	if (edit.operation == "rename" && (edit.name.trimmed().isEmpty() || edit.name.size() > 256 ||
	                                   !edit.name.isValidUtf16() || edit.name.contains(QChar(0))))
		return fail(issue(QT_TRANSLATE_NOOP("AudioMedia", "Choose a nonempty source name of at most 256 characters.")));
	if (replacing || relinking) {
		if (!candidate.succeeded())
			return fail(issue(
			    QT_TRANSLATE_NOOP("AudioMedia", "Review a valid input file before replacing or relinking media.")));
		if (verifyFile) {
			AudioProjectIdentity current;
			if (!readFile(candidate.identity.path, nullptr, &current, &result.error, control)) {
				result.cancelled = stopped(control);
				return result;
			}
			if (current.sha256 != candidate.identity.sha256 ||
			    current.canonicalPath != candidate.identity.canonicalPath)
				return fail(issue(
				    QT_TRANSLATE_NOOP("AudioMedia", "The reviewed file changed. Review it again before applying.")));
		}
	}
	AudioSession next = session;
	for (auto &source : next.sources) {
		if (!ids.contains(source.id))
			continue;
		if (edit.operation == "rename")
			source.audio.sourceName = edit.name;
		if (relinking || replacing) {
			AudioProject audio = candidate.audio;
			result.identicalSamples = sameSamples(source.audio.clip, audio.clip, control);
			if (stopped(control)) {
				result.cancelled = true;
				return result;
			}
			if (relinking) {
				if (!result.identicalSamples)
					return fail(
					    issue(QT_TRANSLATE_NOOP("AudioMedia", "Relinking requires identical sample rate, channels and "
					                                          "sample data. Use Replace for different audio.")));
				source.audio.sourcePath = candidate.identity.canonicalPath;
			} else {
				if (audio.clip.channels != source.audio.clip.channels)
					return fail(
					    issue(QT_TRANSLATE_NOOP("AudioMedia", "Replacement must preserve the source channel count. "
					                                          "Convert channels in the Audio Editor first.")));
				if (audio.clip.sampleRate != session.sampleRate) {
					if (!edit.resample)
						return fail(issue(QT_TRANSLATE_NOOP(
						    "AudioMedia",
						    "Explicitly enable resampling to replace media at a different sample rate.")));
					const auto converted = resampleAudioClip(audio.clip, session.sampleRate, control);
					if (!converted.succeeded()) {
						result.error = converted.error;
						result.cancelled = converted.cancelled;
						return result;
					}
					audio.clip = converted.clip;
					result.resampled = true;
				}
				bool tooShort = false;
				for (const auto &track : session.tracks)
					visitAudioTrackRegions(track, [&](const auto &region, bool) {
						tooShort |= region.sourceId == source.id &&
						            region.sourceOffset + region.length > audio.clip.frameCount();
					});
				if (tooShort)
					return fail(issue(QT_TRANSLATE_NOOP(
					    "AudioMedia",
					    "The replacement is too short for an active or retained take clip's source range.")));
				const auto oldId = source.id;
				source.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
				audio.sourceName = source.audio.sourceName;
				audio.sourcePath = candidate.identity.canonicalPath;
				audio.firstFrame = 0;
				audio.endFrame = audio.clip.frameCount();
				source.audio = std::move(audio);
				result.addedSourceId = source.id;
				result.removedSourceIds.append(oldId);
				for (auto &track : next.tracks)
					visitAudioTrackRegions(track, [&](auto &region, bool) {
						if (region.sourceId == oldId) {
							region.sourceId = source.id;
							result.affectedRegionIds.append(region.id);
						}
					});
			}
		}
	}
	if (edit.operation == "remove" || edit.operation == "prune") {
		for (const auto &source : session.sources)
			if (ids.contains(source.id))
				result.removedSourceIds.append(source.id);
		next.sources.erase(std::remove_if(next.sources.begin(), next.sources.end(),
		                                  [&](const auto &source) { return ids.contains(source.id); }),
		                   next.sources.end());
	}
	result.error = validateAudioSession(next, control);
	result.cancelled = stopped(control);
	if (result.succeeded())
		result.session = std::move(next);
	return result;
}
} // namespace vibestudio
