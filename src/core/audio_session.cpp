#include "core/audio_session.h"
#include "core/audio_arrangement.h"
#include "core/audio_resample.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio
{
namespace
{
QString problem(const char *text) { return QCoreApplication::translate("AudioSession", text); }
QString identifier() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool stopped(const AudioWorkControl &control) { return control.cancelled && control.cancelled(); }
bool bounded(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }
bool validText(const QString &text, int maximum, bool empty = true)
{
	return (empty || !text.trimmed().isEmpty()) && text.size() <= maximum && text.isValidUtf16() &&
	       !text.contains(QChar(0));
}
bool validId(const QString &id) { return validText(id, 64, false); }

} // namespace

QString validateAudioSessionStructure(const AudioSession &session)
{
	if (session.sampleRate < 1 || session.sampleRate > 384000 || !bounded(session.effectTailSeconds, 0, 60) ||
	    !bounded(session.masterGainDb, -96, 24) || !validText(session.name, 256) ||
	    session.sources.size() > AudioSessionSourceLimit || session.tracks.size() > AudioSessionTrackLimit ||
	    session.groups.size() > AudioSessionGroupLimit) {
		return problem(
		    QT_TRANSLATE_NOOP("AudioSession", "Session format, tempo, master gain or item count is invalid."));
	}
	if (const auto issue = validateAudioTempoMap(session.musicalTime, session.sampleRate); !issue.isEmpty())
		return issue;
	QHash<QString, int> groupMembers;
	for (const auto &group : session.groups) {
		if (!validId(group.id) || groupMembers.contains(group.id) || !validText(group.name, 256, false))
			return problem(QT_TRANSLATE_NOOP(
			    "AudioSession", "Clip groups need unique IDs and nonempty names of at most 256 characters."));
		groupMembers.insert(group.id, 0);
	}
	QHash<QString, qint64> sources;
	qint64 sampleCount = 0;
	for (const auto &source : session.sources) {
		const auto &clip = source.audio.clip;
		if (!validId(source.id) || sources.contains(source.id) || (clip.channels != 1 && clip.channels != 2) ||
		    clip.sampleRate != session.sampleRate || clip.samples.isEmpty() ||
		    clip.samples.size() % clip.channels != 0 || clip.samples.size() > AudioSampleLimit ||
		    !validText(source.audio.sourceName, 8192) || !validText(source.audio.sourcePath, 8192) ||
		    source.audio.firstFrame < 0 || source.audio.endFrame < source.audio.firstFrame ||
		    source.audio.endFrame > clip.frameCount()) {
			return problem(QT_TRANSLATE_NOOP(
			    "AudioSession",
			    "Session media must have unique IDs and complete mono/stereo frames at the session sample rate."));
		}
		sampleCount += clip.samples.size();
		if (sampleCount > AudioSessionSampleLimit) {
			return problem(
			    QT_TRANSLATE_NOOP("AudioSession", "Session media exceeds the 256 MiB sample-storage limit."));
		}
		sources.insert(source.id, clip.frameCount());
	}
	QSet<QString> trackIds, regionIds, takeLaneIds;
	const auto masterEffectsError =
	    validateAudioEffectAutomation(session.masterEffects, session.sampleRate, session.masterEffectAutomation);
	if (!masterEffectsError.isEmpty())
		return masterEffectsError;
	const bool solo =
	    std::any_of(session.tracks.cbegin(), session.tracks.cend(), [](const auto &track) { return track.solo; });
	quint64 effectBytes =
	    audioEffectMemoryBytes(session.masterEffects, session.sampleRate, session.masterEffectAutomation);
	qsizetype effectPoints = 0;
	for (const auto &lane : session.masterEffectAutomation)
		effectPoints += lane.points.size();
	qint64 lastFrame = 0;
	bool hasTail = audioEffectsHaveTail(session.masterEffects, session.masterEffectAutomation);
	for (const auto &track : session.tracks) {
		const auto effectsError =
		    validateAudioEffectAutomation(track.effects, session.sampleRate, track.effectAutomation);
		if (!effectsError.isEmpty())
			return effectsError;
		hasTail |= audioEffectsHaveTail(track.effects, track.effectAutomation);
		effectBytes +=
		    audioEffectMemoryBytes(track.effects, session.sampleRate, track.effectAutomation) * (solo ? 2 : 1);
		for (const auto &lane : track.effectAutomation)
			effectPoints += lane.points.size();
		if (effectPoints > AudioEffectAutomationPointLimit)
			return problem(QT_TRANSLATE_NOOP("AudioSession", "Session effect automation exceeds 65536 points."));
		if (effectBytes > AudioEffectMemoryLimit)
			return problem(QT_TRANSLATE_NOOP("AudioSession",
			                                 "Session effect state exceeds 128 MiB. Reduce delay times or effects."));
		if (!validId(track.id) || trackIds.contains(track.id) || !validText(track.name, 256, false) ||
		    !bounded(track.gainDb, -96, 24) || !bounded(track.pan, -1, 1) ||
		    !validAudioAutomation(track.gainAutomation, -96, 24) || !validAudioAutomation(track.panAutomation, -1, 1)) {
			return problem(QT_TRANSLATE_NOOP(
			    "AudioSession",
			    "Track identity, mixer controls or automation is invalid. Automation frames must increase strictly."));
		}
		trackIds.insert(track.id);
		const auto validateRegion = [&](const AudioSessionRegion &region, bool retained) -> QString {
			const qint64 available = sources.value(region.sourceId, -1);
			const auto fadeSpan = region.fadeSpan ? region.fadeSpan : region.length;
			if (!validId(region.id) || regionIds.contains(region.id) || !validText(region.name, 256) || available < 0 ||
			    region.position < 0 || region.position > AudioSessionFrameLimit || region.sourceOffset < 0 ||
			    region.sourceOffset > available || region.length <= 0 ||
			    region.length > available - region.sourceOffset ||
			    region.length > AudioSessionFrameLimit - region.position || !bounded(region.gainDb, -96, 24) ||
			    region.fadeIn < 0 || region.fadeOut < 0 || region.fadeIn > fadeSpan || region.fadeOut > fadeSpan ||
			    region.fadeSpan < 0 || region.fadeSpan > AudioSampleLimit || region.fadeStart < 0 ||
			    region.fadeStart > fadeSpan || region.length > fadeSpan - region.fadeStart ||
			    (!region.fadeSpan && region.fadeStart) ||
			    (!region.groupId.isEmpty() && (retained || !groupMembers.contains(region.groupId)))) {
				return problem(QT_TRANSLATE_NOOP("AudioSession", "A clip has an invalid source, frame range, fade or "
				                                                 "gain. Source media is never extended implicitly."));
			}
			regionIds.insert(region.id);
			if (!region.groupId.isEmpty())
				++groupMembers[region.groupId];
			if (!retained)
				lastFrame = std::max(lastFrame, region.position + region.length);
			if (regionIds.size() > AudioSessionRegionLimit) {
				return problem(QT_TRANSLATE_NOOP("AudioSession", "The session exceeds 4096 clips."));
			}
			return {};
		};
		QString regionIssue;
		visitAudioTrackRegions(track, [&](const auto &region, bool retained) {
			if (regionIssue.isEmpty())
				regionIssue = validateRegion(region, retained);
		});
		if (!regionIssue.isEmpty())
			return regionIssue;
		for (const auto &lane : track.takeLanes) {
			if (track.routing.bus || !validId(lane.id) || takeLaneIds.contains(lane.id) ||
			    !validText(lane.name, 256, false) || lane.regions.isEmpty() ||
			    takeLaneIds.size() >= AudioSessionTakeLaneLimit)
				return problem(QT_TRANSLATE_NOOP(
				    "AudioSession",
				    "Take lanes need unique IDs, names, retained clips and audio tracks within the 128-lane limit."));
			takeLaneIds.insert(lane.id);
		}
	}
	for (auto count : std::as_const(groupMembers))
		if (count < 2)
			return problem(QT_TRANSLATE_NOOP("AudioSession", "Every clip group must contain at least two clips."));
	if (lastFrame && hasTail &&
	    std::ceil(session.effectTailSeconds * session.sampleRate) > double(AudioSessionFrameLimit - lastFrame))
		return problem(QT_TRANSLATE_NOOP(
		    "AudioSession",
		    "The effect tail exceeds the timeline limit. Shorten the tail or move the final clip earlier."));
	AudioRoutingPlan routing;
	if (const auto issue = prepareAudioRouting(session, &routing); !issue.isEmpty())
		return issue;
	std::array<int, AudioSessionTrackLimit> stripLatency{};
	for (qsizetype i = 0; i < session.tracks.size(); ++i)
		stripLatency[size_t(i)] = audioEffectLatencyFrames(session.tracks[i].effects, session.sampleRate);
	AudioLatencyPlan latency;
	if (const auto issue =
	        prepareAudioLatency(session, routing, std::span(stripLatency).first(size_t(session.tracks.size())),
	                            audioEffectLatencyFrames(session.masterEffects, session.sampleRate), &latency);
	    !issue.isEmpty())
		return issue;
	if (effectBytes + latency.bytes > AudioEffectMemoryLimit)
		return problem(QT_TRANSLATE_NOOP("AudioSession", "Effects and latency compensation exceed 128 MiB. Reduce "
		                                                 "lookahead, delay times, effects or parallel routes."));
	return {};
}

QString validateAudioSession(const AudioSession &session, const AudioWorkControl &control)
{
	const QString error = validateAudioSessionStructure(session);
	if (!error.isEmpty()) {
		return error;
	}
	for (const auto &source : session.sources) {
		const auto &clip = source.audio.clip;
		if (QJsonDocument(source.audio.metadata).toJson(QJsonDocument::Compact).size() > 256 * 1024) {
			return problem(QT_TRANSLATE_NOOP("AudioSession", "Session source metadata exceeds 256 KiB."));
		}
		const auto markerError = validateAudioMarkers(clip.markers, clip.frameCount());
		if (!markerError.isEmpty()) {
			return markerError;
		}
		for (qsizetype i = 0; i < clip.samples.size(); ++i) {
			if (i % 4096 == 0 && stopped(control)) {
				return problem(QT_TRANSLATE_NOOP("AudioSession", "Session operation cancelled."));
			}
			if (!std::isfinite(clip.samples[i])) {
				return problem(QT_TRANSLATE_NOOP("AudioSession", "Session media contains a non-finite sample."));
			}
		}
	}
	return stopped(control) ? problem(QT_TRANSLATE_NOOP("AudioSession", "Session operation cancelled.")) : QString();
}

qint64 audioSessionFrames(const AudioSession &session)
{
	qint64 frames = 0;
	bool tail = audioEffectsHaveTail(session.masterEffects, session.masterEffectAutomation);
	for (const auto &track : session.tracks) {
		tail |= audioEffectsHaveTail(track.effects, track.effectAutomation);
		for (const auto &region : track.regions) {
			if (region.position >= 0 && region.length > 0 && region.position <= AudioSessionFrameLimit &&
			    region.length <= AudioSessionFrameLimit - region.position) {
				frames = std::max(frames, region.position + region.length);
			}
		}
	}
	if (frames && tail && bounded(session.effectTailSeconds, 0, 60) && session.sampleRate >= 1 &&
	    session.sampleRate <= 384000)
		frames += std::min(AudioSessionFrameLimit - frames,
		                   qint64(std::ceil(session.effectTailSeconds * session.sampleRate)));
	return frames;
}

AudioSessionResult editAudioSession(const AudioSession &session, const AudioSessionEdit &edit)
{
	AudioSessionResult result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty()) {
		return result;
	}
	AudioSession next = session;
	if (edit.operation == QStringLiteral("master")) {
		next.masterGainDb = edit.gainDb;
	} else if (edit.operation == QStringLiteral("tempo-map")) {
		next.musicalTime = edit.musicalTime;
	} else if (edit.operation == QStringLiteral("effects") && edit.trackId.isEmpty()) {
		next.masterEffects = edit.effects;
		if (edit.effectAutomation)
			next.masterEffectAutomation = *edit.effectAutomation;
		else
			retainAudioEffectAutomation(next.masterEffects, &next.masterEffectAutomation);
		if (edit.effectTailSeconds != -1)
			next.effectTailSeconds = edit.effectTailSeconds;
	} else if (edit.operation == QStringLiteral("add-track") || edit.operation == QStringLiteral("add-bus")) {
		AudioSessionTrack track;
		track.id = identifier();
		track.name = edit.name;
		track.routing.bus = edit.operation == QStringLiteral("add-bus");
		result.addedTrackId = track.id;
		next.tracks.append(track);
	} else {
		auto track = std::find_if(next.tracks.begin(), next.tracks.end(),
		                          [&](const auto &value) { return value.id == edit.trackId; });
		if (track == next.tracks.end()) {
			result.error = problem(QT_TRANSLATE_NOOP("AudioSession", "The selected track no longer exists."));
			return result;
		}
		if (edit.operation == QStringLiteral("effects")) {
			track->effects = edit.effects;
			if (edit.effectAutomation)
				track->effectAutomation = *edit.effectAutomation;
			else
				retainAudioEffectAutomation(track->effects, &track->effectAutomation);
			if (edit.effectTailSeconds != -1)
				next.effectTailSeconds = edit.effectTailSeconds;
		} else if (edit.operation == QStringLiteral("remove-track")) {
			for (const auto &other : next.tracks) {
				if (other.routing.outputId == edit.trackId ||
				    std::any_of(other.routing.sends.cbegin(), other.routing.sends.cend(),
				                [&](const auto &send) { return send.targetId == edit.trackId; })) {
					result.error = problem(QT_TRANSLATE_NOOP(
					    "AudioSession", "Reroute every output and send referencing this bus before removing it."));
					return result;
				}
			}
			next.tracks.erase(track);
		} else if (edit.operation == QStringLiteral("routing")) {
			if (edit.routing.bus != track->routing.bus) {
				result.error = problem(QT_TRANSLATE_NOOP(
				    "AudioSession", "Routing cannot change a track into a bus or a bus into a track."));
				return result;
			}
			track->routing = edit.routing;
		} else if (edit.operation == QStringLiteral("track")) {
			track->name = edit.name;
			track->gainDb = edit.gainDb;
			track->pan = edit.pan;
			track->muted = edit.muted;
			track->solo = edit.solo;
		} else if (edit.operation == QStringLiteral("automation")) {
			track->gainAutomation = edit.gainAutomation;
			track->panAutomation = edit.panAutomation;
		} else {
			auto region = std::find_if(track->regions.begin(), track->regions.end(),
			                           [&](const auto &value) { return value.id == edit.regionId; });
			if (region == track->regions.end()) {
				result.error = problem(QT_TRANSLATE_NOOP("AudioSession", "The selected clip no longer exists."));
				return result;
			}
			if (edit.operation == QStringLiteral("remove-region")) {
				track->regions.erase(region);
			} else if (edit.operation == QStringLiteral("region")) {
				if (edit.resetFades || edit.fadeIn != region->fadeIn || edit.fadeOut != region->fadeOut)
					region->fadeStart = region->fadeSpan = 0;
				region->name = edit.name;
				region->position = edit.position;
				region->sourceOffset = edit.sourceOffset;
				region->length = edit.length;
				region->gainDb = edit.gainDb;
				region->fadeIn = edit.fadeIn;
				region->fadeOut = edit.fadeOut;
				region->muted = edit.muted;
			} else if (edit.operation == QStringLiteral("duplicate") || edit.operation == QStringLiteral("split")) {
				AudioArrangementEdit arrangement;
				arrangement.operation = edit.operation;
				arrangement.regionIds = {region->id};
				arrangement.linkedGroups = false;
				// Check before subtraction so an arbitrary caller cannot overflow.
				if (edit.position < 0 || edit.position > AudioSessionFrameLimit) {
					result.error =
					    problem(QT_TRANSLATE_NOOP("AudioSession", "The clip position is outside the timeline."));
					return result;
				}
				arrangement.offset = edit.position - region->position;
				arrangement.position = edit.position;
				const auto batch = editAudioArrangement(session, arrangement);
				result.error = batch.error;
				if (batch.succeeded()) {
					result.session = batch.session;
					result.addedRegionId = batch.addedRegionIds.value(0);
				}
				return result;
			} else {
				result.error = problem(QT_TRANSLATE_NOOP("AudioSession", "Unknown session edit operation."));
				return result;
			}
		}
	}
	pruneAudioSessionGroups(&next);
	result.error = validateAudioSessionStructure(next);
	if (result.error.isEmpty()) {
		result.session = std::move(next);
	}
	return result;
}

AudioSessionResult importAudioSessionSource(const AudioSession &session, const AudioProject &source,
                                            const QString &trackId, qint64 position, bool resample,
                                            const AudioWorkControl &control)
{
	AudioSessionResult result;
	result.error = validateAudioSessionStructure(session);
	if (!result.error.isEmpty()) {
		return result;
	}
	if (source.clip.channels != 1 && source.clip.channels != 2) {
		result.error = problem(QT_TRANSLATE_NOOP(
		    "AudioSession",
		    "Session import currently requires mono or stereo media. Convert channels in the Audio Editor first."));
		return result;
	}
	AudioProject media = source;
	result.error = validateAudioClip(media.clip);
	if (!result.error.isEmpty()) {
		return result;
	}
	if (media.clip.sampleRate != session.sampleRate) {
		if (!resample) {
			result.error = problem(QT_TRANSLATE_NOOP(
			    "AudioSession", "The source sample rate differs. Explicitly enable resampling to the session rate."));
			return result;
		}
		const auto converted = resampleAudioClip(media.clip, session.sampleRate, control);
		if (!converted.succeeded()) {
			result.error = converted.error;
			result.cancelled = converted.cancelled;
			return result;
		}
		media.clip = converted.clip;
	}
	media.firstFrame = 0;
	media.endFrame = media.clip.frameCount();
	AudioSession next = session;
	QString destination = trackId;
	if (destination.isEmpty()) {
		AudioSessionTrack track;
		track.id = identifier();
		track.name = source.sourceName.left(256).trimmed();
		if (track.name.isEmpty()) {
			track.name = problem(QT_TRANSLATE_NOOP("AudioSession", "Audio track"));
		}
		destination = track.id;
		next.tracks.append(track);
		result.addedTrackId = destination;
	}
	auto track = std::find_if(next.tracks.begin(), next.tracks.end(),
	                          [&](const auto &value) { return value.id == destination; });
	if (track == next.tracks.end()) {
		result.error = problem(QT_TRANSLATE_NOOP("AudioSession", "The import destination track no longer exists."));
		return result;
	}
	if (track->routing.bus) {
		result.error =
		    problem(QT_TRANSLATE_NOOP("AudioSession", "Import clips into an audio track; buses receive routed audio."));
		return result;
	}
	AudioSessionSource asset{identifier(), std::move(media)};
	AudioSessionRegion region;
	region.id = identifier();
	region.sourceId = asset.id;
	region.name = asset.audio.sourceName.left(256);
	region.position = position;
	region.length = asset.audio.clip.frameCount();
	result.addedRegionId = region.id;
	track->regions.append(region);
	next.sources.append(std::move(asset));
	result.error = validateAudioSession(next, control);
	result.cancelled = stopped(control);
	if (result.succeeded()) {
		result.session = std::move(next);
	}
	return result;
}

bool AudioSessionRenderer::prepare(const AudioSession &session, QString *error, const AudioWorkControl &control,
                                   const AudioSessionRenderTarget &target, AudioMeterWindow metering,
                                   AudioSessionRenderClock clock, AudioTimelineLoop loop)
{
	m_ready = false;
	m_clock = clock;
	m_loop = loop;
	m_meters.prepare(0, session.sampleRate, {});
	m_meterEnd = metering.end;
	m_meterTail = 0;
	m_sourceIndices.clear();
	m_session = {};
	m_routing = {};
	m_latency = {};
	m_routeDelays = std::vector<RouteDelays>{};
	m_primeOutput = std::vector<float>{};
	m_primeScratch = std::vector<double>{};
	m_effectProcessors = std::vector<AudioEffectsProcessor>{};
	m_masterProcessor = {};
	m_effectsActive = false;
	m_nextFrame = -1;
	m_targetIndex = -1;
	m_preFaderTarget = target.tap == AudioSessionRenderTarget::Tap::PreFader;
	m_included.fill(target.stripId.isEmpty());
	const auto issue = validateAudioSession(session, control);
	if (error) {
		*error = issue;
	}
	if (!issue.isEmpty()) {
		return false;
	}
	if (!loop.valid() || (clock != AudioSessionRenderClock::Compensated && clock != AudioSessionRenderClock::Live) ||
	    (clock == AudioSessionRenderClock::Live &&
	     (!target.stripId.isEmpty() || target.tap != AudioSessionRenderTarget::Tap::PostFader))) {
		if (error)
			*error = problem(QT_TRANSLATE_NOOP("AudioSession", "Live rendering requires the master render target."));
		return false;
	}
	if (metering.enabled && (metering.first < 0 || metering.end <= metering.first ||
	                         metering.end > AudioSessionFrameLimit || !target.stripId.isEmpty())) {
		if (error)
			*error = problem(QT_TRANSLATE_NOOP(
			    "AudioSession", "Metering requires a nonempty timeline range and the master render target."));
		return false;
	}
	if (clock == AudioSessionRenderClock::Compensated && loop.enabled)
		metering.end = AudioLoopClockLimit;
	m_meters.prepare(int(session.tracks.size()) + 1, session.sampleRate, metering);
	m_session = session;
	if (const auto routingError = prepareAudioRouting(session, &m_routing); !routingError.isEmpty()) {
		if (error)
			*error = routingError;
		return false;
	}
	if (clock == AudioSessionRenderClock::Live || loop.end > loop.first)
		m_routing.advanced = true;
	if (target.tap != AudioSessionRenderTarget::Tap::PostFader &&
	    target.tap != AudioSessionRenderTarget::Tap::PreFader) {
		if (error)
			*error = problem(QT_TRANSLATE_NOOP("AudioSession", "Choose a supported render signal point."));
		return false;
	}
	if (!target.stripId.isEmpty()) {
		for (qsizetype i = 0; i < session.tracks.size(); ++i)
			if (session.tracks[i].id == target.stripId)
				m_targetIndex = int(i);
		if (m_targetIndex < 0) {
			if (error)
				*error = problem(QT_TRANSLATE_NOOP("AudioSession", "The render track or bus does not exist."));
			return false;
		}
		m_included[size_t(m_targetIndex)] = true;
		// Reverse topological order identifies every enabled upstream path once.
		// Unrelated/downstream processing cannot alter or overflow a strip tap.
		for (auto it = m_routing.order.crbegin(); it != m_routing.order.crend(); ++it) {
			const auto &route = session.tracks[*it].routing;
			const auto &node = m_routing.nodes[*it];
			if (route.outputEnabled && node.output >= 0 && m_included[size_t(node.output)])
				m_included[size_t(*it)] = true;
			for (qsizetype i = 0; i < route.sends.size(); ++i)
				if (route.sends[i].enabled && node.sends[i] >= 0 && m_included[size_t(node.sends[i])])
					m_included[size_t(*it)] = true;
		}
		m_routing.advanced = true;
	} else if (m_preFaderTarget) {
		if (error)
			*error = problem(QT_TRANSLATE_NOOP("AudioSession", "Pre-fader rendering requires a track or bus."));
		return false;
	}
	for (int i = 0; i < session.sources.size(); ++i) {
		m_sourceIndices.insert(session.sources[i].id, i);
	}
	m_effectsActive = m_targetIndex < 0 && audioEffectsEnabled(session.masterEffects);
	auto postRequired = m_included;
	if (m_targetIndex >= 0) {
		for (qsizetype i = 0; i < session.tracks.size(); ++i) {
			const auto &route = session.tracks[i].routing;
			const auto &node = m_routing.nodes[i];
			postRequired[size_t(i)] = i == m_targetIndex && !m_preFaderTarget;
			if (!m_included[size_t(i)] || i == m_targetIndex)
				continue;
			postRequired[size_t(i)] = route.outputEnabled && node.output >= 0 && m_included[size_t(node.output)];
			for (qsizetype j = 0; j < route.sends.size(); ++j)
				postRequired[size_t(i)] |= route.sends[j].enabled && !route.sends[j].preFader && node.sends[j] >= 0 &&
				                           m_included[size_t(node.sends[j])];
		}
	}
	for (qsizetype i = 0; i < session.tracks.size(); ++i)
		if (postRequired[size_t(i)])
			m_effectsActive |= audioEffectsEnabled(session.tracks[i].effects);
	std::array<int, AudioSessionTrackLimit> stripLatency{};
	for (qsizetype i = 0; i < session.tracks.size(); ++i)
		if (postRequired[size_t(i)])
			stripLatency[size_t(i)] = audioEffectLatencyFrames(session.tracks[i].effects, session.sampleRate);
	if (const auto latencyError = prepareAudioLatency(
	        session, m_routing, std::span(stripLatency).first(size_t(session.tracks.size())),
	        m_targetIndex < 0 ? audioEffectLatencyFrames(session.masterEffects, session.sampleRate) : 0, &m_latency,
	        m_included, m_targetIndex, m_preFaderTarget);
	    !latencyError.isEmpty()) {
		if (error)
			*error = latencyError;
		return false;
	}
	if (m_latency.total || m_latency.bytes) {
		const int domains = m_routing.solo ? 2 : 1;
		m_routeDelays.resize(size_t(session.tracks.size()) * domains);
		for (qsizetype i = 0; i < session.tracks.size(); ++i)
			for (int domain = 0; domain < domains; ++domain) {
				auto &delays = m_routeDelays[size_t(i) * domains + domain];
				const auto &node = m_latency.nodes[i];
				delays.output.prepare(node.outputDelay);
				delays.sends.resize(size_t(node.sendDelays.size()));
				for (qsizetype j = 0; j < node.sendDelays.size(); ++j)
					delays.sends[size_t(j)].prepare(node.sendDelays[j]);
			}
	}
	if (m_effectsActive) {
		m_routing.advanced = true;
		const int domains = m_routing.solo ? 2 : 1;
		m_effectProcessors.resize(size_t(session.tracks.size()) * domains);
		for (qsizetype i = 0; i < session.tracks.size(); ++i)
			for (int domain = 0; domain < domains; ++domain) {
				if (!postRequired[size_t(i)])
					continue;
				if (stopped(control)) {
					if (error)
						*error = problem(QT_TRANSLATE_NOOP("AudioSession", "Session operation cancelled."));
					return false;
				}
				if (!m_effectProcessors[size_t(i) * domains + domain].prepare(
				        session.tracks[i].effects, session.sampleRate, error, session.tracks[i].effectAutomation,
				        m_latency.nodes[i].input, loop))
					return false;
			}
		if (m_targetIndex < 0 &&
		    !m_masterProcessor.prepare(session.masterEffects, session.sampleRate, error, session.masterEffectAutomation,
		                               m_latency.masterInput, loop))
			return false;
	}
	if (metering.enabled)
		for (const auto &node : m_latency.nodes)
			m_meterTail = std::max(m_meterTail, node.output - m_latency.total);
	if (clock == AudioSessionRenderClock::Compensated && (m_latency.total || m_meterTail)) {
		m_primeOutput.resize(2048);
		m_primeScratch.resize(scratchSamples(1024));
	}
	m_ready = true;
	return true;
}

void AudioSessionRenderer::resetProcessing()
{
	m_nextFrame = -1;
	for (auto &processor : m_effectProcessors)
		processor.reset();
	m_masterProcessor.reset();
	for (auto &delays : m_routeDelays) {
		delays.output.reset();
		for (auto &send : delays.sends)
			send.reset();
	}
}

bool AudioSessionRenderer::setLoopEnabled(bool enabled)
{
	if (!m_ready || m_clock != AudioSessionRenderClock::Compensated || m_loop.end <= m_loop.first)
		return false;
	if (m_loop.enabled == enabled)
		return true;
	m_loop.enabled = enabled;
	for (auto &processor : m_effectProcessors)
		processor.setLoopEnabled(enabled);
	m_masterProcessor.setLoopEnabled(enabled);
	m_meters.setEnd(enabled ? AudioLoopClockLimit : m_meterEnd);
	resetProcessing();
	resetMetering();
	return true;
}

AudioSessionRenderer::BlockStatus AudioSessionRenderer::renderInto(qint64 first, std::span<float> output,
                                                                   std::span<double> scratch,
                                                                   const AudioWorkControl &control)
{
	const AudioSession &session = m_session;
	const auto fail = [&](BlockStatus status) {
		std::fill(output.begin(), output.end(), 0.0f);
		resetProcessing();
		resetMetering();
		return status;
	};
	const auto frameLimit = m_loop.enabled ? AudioLoopClockLimit : AudioSessionFrameLimit;
	if (!m_ready || m_clock != AudioSessionRenderClock::Compensated || output.empty() || output.size() % 2 ||
	    output.size() > 131072 || scratch.size() < scratchSamples(int(output.size() / 2)) || first < 0 ||
	    first > frameLimit - qint64(output.size() / 2)) {
		return fail(BlockStatus::InvalidRange);
	}
	if (stopped(control)) {
		return fail(BlockStatus::Cancelled);
	}
	if (first != m_nextFrame)
		m_meters.setOrigin(first);
	if (m_routing.advanced) {
		if (first != m_nextFrame) {
			resetProcessing();
			// Establish one physical origin for every processor, including an
			// initially silent bus. Warm-up consumes future context, never preroll.
			for (auto &processor : m_effectProcessors)
				processor.reset(first);
			m_masterProcessor.reset(first);
			for (int primed = 0; primed < m_latency.total;) {
				const int count = std::min(1024, m_latency.total - primed);
				const auto status = renderRoutedSpan(first + primed, std::span(m_primeOutput).first(size_t(count) * 2),
				                                     m_primeScratch, control, {}, frameLimit);
				if (status != BlockStatus::Ready)
					return fail(status);
				primed += count;
			}
		}
		const auto status = renderRoutedSpan(first + m_latency.total, output, scratch, control, {}, frameLimit);
		if (status != BlockStatus::Ready)
			return fail(status);
		m_nextFrame = first + qint64(output.size() / 2);
		return status;
	}
	const int frames = int(output.size() / 2);
	auto sum = scratch.first(output.size());
	std::fill(sum.begin(), sum.end(), 0.0);
	const auto count = output.size();
	const bool metering = m_meters.enabled();
	auto pre = metering ? scratch.subspan(count, count) : std::span<double>();
	auto post = metering ? scratch.subspan(count * 2, count) : std::span<double>();
	auto masterPre = metering ? scratch.subspan(count * 3, count) : std::span<double>();
	std::fill(masterPre.begin(), masterPre.end(), 0.0);
	const bool anySolo =
	    std::any_of(session.tracks.cbegin(), session.tracks.cend(), [](const auto &track) { return track.solo; });
	for (qsizetype index = 0; index < session.tracks.size(); ++index) {
		const auto &track = session.tracks[index];
		std::fill(pre.begin(), pre.end(), 0.0);
		std::fill(post.begin(), post.end(), 0.0);
		if (track.muted || (anySolo && !track.solo)) {
			m_meters.process(int(index), false, first, pre);
			m_meters.process(int(index), true, first, post);
			continue;
		}
		for (const auto &region : track.regions) {
			if (region.muted) {
				continue;
			}
			const qint64 begin = std::max(first, region.position),
			             end = std::min(first + frames, region.position + region.length);
			if (begin >= end) {
				continue;
			}
			const auto &media = session.sources[m_sourceIndices.value(region.sourceId)].audio.clip;
			const double staticGain = std::pow(10.0, (track.gainDb + region.gainDb + session.masterGainDb) / 20.0);
			const double clipGain = metering ? std::pow(10.0, region.gainDb / 20.0) : 0;
			for (qint64 frame = begin; frame < end; ++frame) {
				if ((frame - begin) % 1024 == 0 && stopped(control)) {
					return fail(BlockStatus::Cancelled);
				}
				const qint64 local = frame - region.position;
				const double fade = audioSessionRegionFade(region, local);
				const double gain = staticGain * fade *
				                    (track.gainAutomation.isEmpty()
				                         ? 1
				                         : std::pow(10.0, audioAutomationValue(track.gainAutomation, frame, 0) / 20.0));
				const double pan = audioAutomationValue(track.panAutomation, frame, track.pan);
				const qint64 read = (region.sourceOffset + local) * media.channels;
				const qint64 write = (frame - first) * 2;
				// Mono equal-power panning has -3.0103 dB at center. Stereo balance
				// retains unity at center, attenuating only the opposite channel.
				const double left = pan >= 1              ? 0
				                    : media.channels == 1 ? std::cos((pan + 1) * std::numbers::pi / 4)
				                    : pan <= 0            ? 1
				                                          : std::cos(pan * std::numbers::pi / 2);
				const double right = pan <= -1             ? 0
				                     : media.channels == 1 ? std::sin((pan + 1) * std::numbers::pi / 4)
				                     : pan >= 0            ? 1
				                                           : std::cos(pan * std::numbers::pi / 2);
				sum[write] += double(media.samples[read]) * gain * left;
				sum[write + 1] += double(media.samples[read + (media.channels == 1 ? 0 : 1)]) * gain * right;
				if (metering) {
					const double l = double(media.samples[read]) * clipGain * fade;
					const double r = double(media.samples[read + (media.channels == 1 ? 0 : 1)]) * clipGain * fade;
					const double centre = media.channels == 1 ? std::numbers::sqrt2 / 2 : 1;
					const double trim =
					    std::pow(10.0, (track.gainDb + audioAutomationValue(track.gainAutomation, frame, 0)) / 20.0);
					pre[write] += l * centre;
					pre[write + 1] += r * centre;
					post[write] += l * trim * left;
					post[write + 1] += r * trim * right;
				}
			}
		}
		m_meters.process(int(index), false, first, pre);
		m_meters.process(int(index), true, first, post);
		if (metering)
			for (size_t i = 0; i < count; ++i)
				masterPre[i] += post[i];
	}
	for (size_t i = 0; i < sum.size(); ++i) {
		if (!std::isfinite(sum[i]) || std::abs(sum[i]) > std::numeric_limits<float>::max()) {
			return fail(BlockStatus::Overflow);
		}
		output[i] = float(sum[i]);
	}
	m_meters.process(int(session.tracks.size()), false, first, masterPre);
	m_meters.process(int(session.tracks.size()), true, first, std::span<const float>(output));
	m_nextFrame = first + frames;
	return stopped(control) ? fail(BlockStatus::Cancelled) : BlockStatus::Ready;
}

AudioSessionRenderer::BlockStatus AudioSessionRenderer::renderLiveInto(qint64 first, std::span<float> output,
                                                                       std::span<double> scratch,
                                                                       std::span<const AudioSessionLiveInput> inputs,
                                                                       const AudioWorkControl &control,
                                                                       qint64 playbackEnd)
{
	const AudioSession &session = m_session;
	const auto fail = [&](BlockStatus status) {
		std::fill(output.begin(), output.end(), 0.0f);
		resetProcessing();
		resetMetering();
		return status;
	};
	if (!m_ready || m_clock != AudioSessionRenderClock::Live || output.empty() || output.size() % 2 ||
	    output.size() > 131072 || scratch.size() < scratchSamples(int(output.size() / 2)) || first < 0 ||
	    first > AudioSessionFrameLimit + m_latency.total - qint64(output.size() / 2) ||
	    inputs.size() > size_t(session.tracks.size()) || playbackEnd < 0 || playbackEnd > AudioSessionFrameLimit)
		return fail(BlockStatus::InvalidRange);
	const int frames = int(output.size() / 2);
	std::array<bool, AudioSessionTrackLimit> assigned{};
	for (const auto &input : inputs) {
		if (input.trackIndex < 0 || input.trackIndex >= session.tracks.size() ||
		    session.tracks[input.trackIndex].routing.bus || assigned[size_t(input.trackIndex)] ||
		    (input.channels != 1 && input.channels != 2) || input.samples.empty() ||
		    input.samples.size() % size_t(input.channels) || input.offsetFrames < 0 || input.offsetFrames >= frames ||
		    input.samples.size() / size_t(input.channels) > size_t(frames - input.offsetFrames) ||
		    first >
		        AudioSessionFrameLimit - input.offsetFrames - qint64(input.samples.size() / size_t(input.channels)) ||
		    !std::isfinite(input.gain) || input.gain < 0 || input.gain > 16)
			return fail(BlockStatus::InvalidRange);
		assigned[size_t(input.trackIndex)] = true;
		for (size_t at = 0; at < input.samples.size(); ++at) {
			if (at % 2048 == 0 && stopped(control))
				return fail(BlockStatus::Cancelled);
			if (!std::isfinite(input.samples[at]))
				return fail(BlockStatus::Overflow);
		}
	}
	if (stopped(control))
		return fail(BlockStatus::Cancelled);
	if (first != m_nextFrame) {
		resetProcessing();
		m_meters.setOrigin(first);
		for (auto &processor : m_effectProcessors)
			processor.reset(first);
		m_masterProcessor.reset(first);
	}
	const auto status = renderRoutedSpan(first, output, scratch, control, inputs, playbackEnd);
	if (status != BlockStatus::Ready)
		return fail(status);
	m_nextFrame = first + frames;
	return stopped(control) ? fail(BlockStatus::Cancelled) : BlockStatus::Ready;
}

AudioSessionRenderer::BlockStatus AudioSessionRenderer::renderRoutedSpan(qint64 first, std::span<float> output,
                                                                         std::span<double> scratch,
                                                                         const AudioWorkControl &control,
                                                                         std::span<const AudioSessionLiveInput> inputs,
                                                                         qint64 playbackEnd)
{
	const int frames = int(output.size() / 2);
	for (int at = 0; at < frames;) {
		int count = frames - at;
		if (m_loop.enabled)
			count = int(std::min<qint64>(count, m_loop.end - audioLoopFrame(first + at, m_loop)));
		std::array<AudioSessionLiveInput, AudioSessionTrackLimit> windows{};
		int assignedInputs = 0;
		for (const auto &input : inputs) {
			const int begin = std::max(at, input.offsetFrames);
			const int end =
			    std::min(at + count, input.offsetFrames + int(input.samples.size() / size_t(input.channels)));
			if (begin < end)
				windows[size_t(assignedInputs++)] = {
				    input.trackIndex,
				    input.channels,
				    input.samples.subspan(size_t(begin - input.offsetFrames) * size_t(input.channels),
				                          size_t(end - begin) * size_t(input.channels)),
				    begin - at,
				    input.gain,
				    input.replacePlayback};
		}
		const auto status = renderRouted(first + at, output.subspan(size_t(at * 2), size_t(count * 2)), scratch,
		                                 control, std::span(windows).first(size_t(assignedInputs)), playbackEnd);
		if (status != BlockStatus::Ready)
			return status;
		at += count;
	}
	return BlockStatus::Ready;
}

AudioSessionRenderer::BlockStatus AudioSessionRenderer::finishMetering(qint64 end, const AudioWorkControl &control)
{
	// Disconnected strips can have more latency than the audible master path.
	// Drain only those late taps; the meter window rejects all out-of-range data.
	for (int flushed = 0; flushed < m_meterTail;) {
		const int frames = std::min(1024, m_meterTail - flushed);
		const auto status = renderRouted(end + m_latency.total + flushed,
		                                 std::span(m_primeOutput).first(size_t(frames) * 2), m_primeScratch, control);
		if (status != BlockStatus::Ready) {
			resetMetering();
			return status;
		}
		flushed += frames;
	}
	return BlockStatus::Ready;
}

AudioClipResult AudioSessionRenderer::renderBlock(qint64 first, int frames, const AudioWorkControl &control)
{
	AudioClipResult result;
	const auto frameLimit = m_loop.enabled ? AudioLoopClockLimit : AudioSessionFrameLimit;
	if (!m_ready || first < 0 || frames < 1 || frames > 65536 || first > frameLimit - frames) {
		resetProcessing();
		resetMetering();
		result.error = problem(QT_TRANSLATE_NOOP(
		    "AudioSession", "Prepare a valid session and choose a block of 1–65536 frames within the timeline."));
		return result;
	}
	result.clip = {2, m_session.sampleRate, QVector<float>(frames * 2)};
	QVector<double> scratch(qsizetype(scratchSamples(frames)));
	const auto status = renderInto(first, {result.clip.samples.data(), size_t(frames * 2)},
	                               {scratch.data(), size_t(scratch.size())}, control);
	if (status != BlockStatus::Ready) {
		result.clip = {};
		result.cancelled = status == BlockStatus::Cancelled;
		if (!result.cancelled) {
			result.error = problem(QT_TRANSLATE_NOOP(
			    "AudioSession", "The mix exceeds floating-point headroom. Reduce track or master gain."));
		}
	}
	return result;
}

AudioClipResult renderAudioSession(const AudioSession &session, qint64 first, qint64 end,
                                   const AudioWorkControl &control)
{
	AudioClipResult result;
	AudioSessionRenderer renderer;
	if (!renderer.prepare(session, &result.error, control)) {
		result.cancelled = stopped(control);
		return result;
	}
	if (end == -1) {
		end = renderer.frameCount();
	}
	if (first < 0 || end <= first || end > AudioSessionFrameLimit || end - first > AudioSampleLimit / 2) {
		result.error =
		    problem(QT_TRANSLATE_NOOP("AudioSession", "Choose a nonempty mix range within 8388608 stereo frames for "
		                                              "waveform editing. Export a longer mix as streaming WAV."));
		return result;
	}
	result.clip = {2, session.sampleRate, QVector<float>((end - first) * 2)};
	for (qint64 position = first; position < end;) {
		const int count = int(std::min<qint64>(16384, end - position));
		const auto block = renderer.renderBlock(position, count, control);
		if (!block.succeeded()) {
			return block;
		}
		std::copy(block.clip.samples.cbegin(), block.clip.samples.cend(),
		          result.clip.samples.begin() + (position - first) * 2);
		position += count;
	}
	return result;
}

QJsonObject audioSessionSummary(const AudioSession &session)
{
	auto timing = audioTempoMapToJson(session.musicalTime);
	timing.remove("tempo");
	timing.remove("beatsPerBar");
	const bool haveLanes = std::any_of(session.tracks.cbegin(), session.tracks.cend(),
	                                   [](const auto &track) { return !track.takeLanes.isEmpty(); });
	const auto regionJson = [](const QVector<AudioSessionRegion> &clips) {
		QJsonArray regions;
		for (const auto &region : clips) {
			regions.append(QJsonObject{{"id", region.id},
			                           {"name", region.name},
			                           {"sourceId", region.sourceId},
			                           {"position", region.position},
			                           {"sourceOffset", region.sourceOffset},
			                           {"length", region.length},
			                           {"gainDb", region.gainDb},
			                           {"fadeIn", region.fadeIn},
			                           {"fadeOut", region.fadeOut},
			                           {"muted", region.muted},
			                           {"groupId", region.groupId},
			                           {"fadeStart", region.fadeStart},
			                           {"fadeSpan", region.fadeSpan}});
		}
		return regions;
	};
	QJsonArray tracks;
	for (const auto &track : session.tracks) {
		const auto regions = regionJson(track.regions);

		tracks.append(QJsonObject{{"id", track.id},
		                          {"name", track.name},
		                          {"gainDb", track.gainDb},
		                          {"pan", track.pan},
		                          {"muted", track.muted},
		                          {"solo", track.solo},
		                          {"gainAutomation", audioAutomationToJson(track.gainAutomation)},
		                          {"panAutomation", audioAutomationToJson(track.panAutomation)},
		                          {"routing", audioRoutingToJson(track.routing)},
		                          {"effects", audioEffectsToJson(track.effects)},
		                          {"effectAutomation", audioEffectAutomationToJson(track.effectAutomation)},
		                          {"regions", regions}});
		if (haveLanes) {
			QJsonArray lanes;
			for (const auto &lane : track.takeLanes)
				lanes.append(QJsonObject{{"id", lane.id}, {"name", lane.name}, {"regions", regionJson(lane.regions)}});
			auto object = tracks.last().toObject();
			object.insert("takeLanes", lanes);
			tracks[tracks.size() - 1] = object;
		}
	}
	QJsonArray sources;
	for (const auto &source : session.sources) {
		sources.append(QJsonObject{{"id", source.id},
		                           {"name", source.audio.sourceName},
		                           {"path", source.audio.sourcePath},
		                           {"channels", source.audio.clip.channels},
		                           {"frames", source.audio.clip.frameCount()}});
	}
	QJsonArray groups;
	for (const auto &group : session.groups)
		groups.append(QJsonObject{{"id", group.id}, {"name", group.name}});
	return {{"groups", groups},
	        {"effects", QJsonObject{{"master", audioEffectsToJson(session.masterEffects)},
	                                {"tailSeconds", session.effectTailSeconds},
	                                {"automation", audioEffectAutomationToJson(session.masterEffectAutomation)}}},
	        {"name", session.name},
	        {"sampleRate", session.sampleRate},
	        {"tempo", session.musicalTime.tempo},
	        {"beatsPerBar", session.musicalTime.beatsPerBar},
	        {"timing", timing},
	        {"masterGainDb", session.masterGainDb},
	        {"frames", audioSessionFrames(session)},
	        {"sources", sources},
	        {"tracks", tracks}};
}

} // namespace vibestudio
