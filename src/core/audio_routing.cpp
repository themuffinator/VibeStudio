#include "core/audio_routing.h"
#include "core/audio_session.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace vibestudio
{
namespace
{
QString problem(const char *text) { return QCoreApplication::translate("AudioRouting", text); }
bool bounded(double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; }
bool validTarget(const QString &value)
{
	return value.size() <= 64 && value.isValidUtf16() && !value.contains(QChar(0));
}
double leftBalance(double pan) { return pan >= 1 ? 0 : pan <= 0 ? 1 : std::cos(pan * std::numbers::pi / 2); }
double rightBalance(double pan) { return pan <= -1 ? 0 : pan >= 0 ? 1 : std::cos(pan * std::numbers::pi / 2); }
} // namespace

QString prepareAudioRouting(const AudioSession &session, AudioRoutingPlan *result)
{
	if (result)
		*result = {};
	if (session.tracks.size() > AudioSessionTrackLimit)
		return problem(QT_TRANSLATE_NOOP("AudioRouting", "The routing graph exceeds 64 tracks and buses."));
	AudioRoutingPlan plan;
	const int count = int(session.tracks.size());
	plan.nodes.resize(count);
	QHash<QString, int> ids;
	for (int i = 0; i < count; ++i) {
		const auto &track = session.tracks[i];
		if (track.id.trimmed().isEmpty() || !validTarget(track.id) || ids.contains(track.id))
			return problem(QT_TRANSLATE_NOOP("AudioRouting", "Routing requires unique, valid track IDs."));
		ids.insert(track.id, i);
		if (track.routing.bus) {
			plan.nodes[i].busIndex = plan.buses++;
			if (!track.regions.isEmpty())
				return problem(QT_TRANSLATE_NOOP("AudioRouting", "Buses cannot contain audio clips."));
		}
		plan.advanced |= track.routing != AudioTrackRouting{};
		plan.solo |= track.solo;
	}
	if (plan.buses > AudioSessionBusLimit)
		return problem(QT_TRANSLATE_NOOP("AudioRouting", "The session exceeds 32 buses."));
	QVector<QVector<int>> edges(count);
	QVector<int> indegree(count, 0);
	for (int i = 0; i < count; ++i) {
		const auto &routing = session.tracks[i].routing;
		const auto target = [&](const QString &id, int *index) {
			if (!validTarget(id))
				return false;
			*index = id.isEmpty() ? -1 : ids.value(id, -1);
			if (id.isEmpty())
				return true;
			if (*index < 0 || !session.tracks[*index].routing.bus)
				return false;
			edges[i].append(*index);
			++indegree[*index];
			return true;
		};
		if (!target(routing.outputId, &plan.nodes[i].output))
			return problem(
			    QT_TRANSLATE_NOOP("AudioRouting", "Every output must target the master or an existing bus."));
		if (routing.sends.size() > AudioSessionSendLimit)
			return problem(QT_TRANSLATE_NOOP("AudioRouting", "A track or bus can have at most eight sends."));
		QSet<QString> sent;
		for (const auto &send : routing.sends) {
			int index = -1;
			if (!bounded(send.gainDb, -96, 24) || !bounded(send.pan, -1, 1) || sent.contains(send.targetId) ||
			    !target(send.targetId, &index))
				return problem(QT_TRANSLATE_NOOP(
				    "AudioRouting",
				    "Sends require distinct master/bus targets, gain from -96 to +24 dB and balance from -1 to +1."));
			sent.insert(send.targetId);
			plan.nodes[i].sends.append(index);
		}
	}
	std::array<bool, AudioSessionTrackLimit> visited{};
	for (int processed = 0; processed < count; ++processed) {
		int selected = -1;
		for (int i = 0; i < count; ++i)
			if (!visited[i] && indegree[i] == 0) {
				selected = i;
				break;
			}
		if (selected < 0)
			return problem(QT_TRANSLATE_NOOP(
			    "AudioRouting",
			    "Routing contains a feedback cycle. Remove the cycle, including disabled sends and outputs."));
		visited[selected] = true;
		plan.order.append(selected);
		for (int target : edges[selected])
			--indegree[target];
	}
	for (auto it = plan.order.crbegin(); it != plan.order.crend(); ++it) {
		const auto &track = session.tracks[*it];
		auto &node = plan.nodes[*it];
		node.reachesSolo = track.solo;
		if (track.routing.outputEnabled && node.output >= 0)
			node.reachesSolo |= plan.nodes[node.output].reachesSolo;
		for (qsizetype i = 0; i < track.routing.sends.size(); ++i)
			if (track.routing.sends[i].enabled && node.sends[i] >= 0)
				node.reachesSolo |= plan.nodes[node.sends[i]].reachesSolo;
	}
	if (result)
		*result = std::move(plan);
	return {};
}

QJsonObject audioRoutingToJson(const AudioTrackRouting &routing)
{
	QJsonArray sends;
	for (const auto &send : routing.sends)
		sends.append(QJsonObject{{"targetId", send.targetId},
		                         {"gainDb", send.gainDb},
		                         {"pan", send.pan},
		                         {"preFader", send.preFader},
		                         {"enabled", send.enabled}});
	return {{"bus", routing.bus},
	        {"outputId", routing.outputId},
	        {"outputEnabled", routing.outputEnabled},
	        {"invertLeft", routing.invertLeft},
	        {"invertRight", routing.invertRight},
	        {"swapChannels", routing.swapChannels},
	        {"sends", sends}};
}
bool audioRoutingFromJson(const QJsonObject &object, AudioTrackRouting *routing)
{
	if (!routing || object.size() != 7 || !object.value("bus").isBool() || !object.value("outputId").isString() ||
	    !object.value("outputEnabled").isBool() || !object.value("invertLeft").isBool() ||
	    !object.value("invertRight").isBool() || !object.value("swapChannels").isBool() ||
	    !object.value("sends").isArray() || object.value("sends").toArray().size() > AudioSessionSendLimit)
		return false;
	AudioTrackRouting next;
	next.bus = object.value("bus").toBool();
	next.outputId = object.value("outputId").toString();
	if (!validTarget(next.outputId))
		return false;
	next.outputEnabled = object.value("outputEnabled").toBool();
	next.invertLeft = object.value("invertLeft").toBool();
	next.invertRight = object.value("invertRight").toBool();
	next.swapChannels = object.value("swapChannels").toBool();
	for (const auto &value : object.value("sends").toArray()) {
		const auto item = value.toObject();
		if (!value.isObject() || item.size() != 5 || !item.value("targetId").isString() ||
		    !item.value("gainDb").isDouble() || !item.value("pan").isDouble() || !item.value("preFader").isBool() ||
		    !item.value("enabled").isBool())
			return false;
		AudioSend send{item.value("targetId").toString(), item.value("gainDb").toDouble(), item.value("pan").toDouble(),
		               item.value("preFader").toBool(), item.value("enabled").toBool()};
		if (!validTarget(send.targetId) || !bounded(send.gainDb, -96, 24) || !bounded(send.pan, -1, 1))
			return false;
		next.sends.append(send);
	}
	*routing = std::move(next);
	return true;
}

size_t AudioSessionRenderer::scratchSamples(int frames) const
{
	if (frames < 1 || frames > 65536)
		return 0;
	// Master, one pre-fader strip, one post-fader strip, and each bus input.
	// Under solo, pending and already-audible contributions remain separate.
	return size_t(frames) * 2 *
	       (m_routing.advanced ? size_t((m_effectsActive ? 5 : 3) + m_routing.buses * (m_routing.solo ? 2 : 1) +
	                                    (m_meters.enabled() ? 2 : 0) + (m_targetIndex >= 0 ? 1 : 0))
	                           : (m_meters.enabled() ? 4 : 1));
}

AudioSessionRenderer::BlockStatus AudioSessionRenderer::renderRouted(qint64 first, std::span<float> output,
                                                                     std::span<double> scratch,
                                                                     const AudioWorkControl &control,
                                                                     std::span<const AudioSessionLiveInput> inputs,
                                                                     qint64 playbackEnd)
{
	const AudioSession &session = m_session;
	const auto fail = [&](BlockStatus status) {
		std::fill(output.begin(), output.end(), 0.0f);
		return status;
	};
	const auto cancelled = [&] { return control.cancelled && control.cancelled(); };
	const auto count = output.size();
	const int frames = int(count / 2), domains = m_routing.solo ? 2 : 1;
	const auto authoredFirst = audioLoopFrame(first, m_loop);
	const auto authoredEnd = authoredFirst + std::clamp<qint64>(playbackEnd - first, 0, frames);
	std::array<const AudioSessionLiveInput *, AudioSessionTrackLimit> liveInputs{};
	for (const auto &input : inputs)
		liveInputs[size_t(input.trackIndex)] = &input;
	std::fill(scratch.begin(), scratch.begin() + qsizetype(scratchSamples(frames)), 0.0);
	auto master = scratch.first(count), pre = scratch.subspan(count, count), post = scratch.subspan(count * 2, count);
	auto audibleDry = m_effectsActive ? scratch.subspan(count * 3, count) : std::span<double>();
	auto audibleWet = m_effectsActive ? scratch.subspan(count * 4, count) : std::span<double>();
	const size_t meterAt = size_t((m_effectsActive ? 5 : 3) + m_routing.buses * domains) * count;
	auto meterPre = m_meters.enabled() ? scratch.subspan(meterAt, count) : std::span<double>();
	auto meterPost = m_meters.enabled() ? scratch.subspan(meterAt + count, count) : std::span<double>();
	auto target = m_targetIndex >= 0 ? scratch.subspan(scratchSamples(frames) - count, count) : master;
	const auto bus = [&](int index, int domain) {
		return scratch.subspan(size_t((m_effectsActive ? 5 : 3) + index * domains + domain) * count, count);
	};
	const auto processEffects = [&](AudioEffectsProcessor &processor, std::span<double> samples) {
		for (size_t at = 0; at < samples.size(); at += 2048) {
			if (cancelled())
				return BlockStatus::Cancelled;
			if (!processor.process(samples.subspan(at, std::min(size_t(2048), samples.size() - at)),
			                       first + qint64(at / 2)))
				return BlockStatus::Overflow;
		}
		return BlockStatus::Ready;
	};
	for (int index : m_routing.order) {
		if (!m_included[size_t(index)])
			continue;
		if (cancelled())
			return fail(BlockStatus::Cancelled);
		const auto &track = session.tracks[index];
		const auto &route = track.routing;
		const auto &node = m_routing.nodes[index];
		const auto *live = liveInputs[size_t(index)];
		std::fill(meterPre.begin(), meterPre.end(), 0.0);
		std::fill(meterPost.begin(), meterPost.end(), 0.0);
		const auto capture = [&] {
			m_meters.process(index, false, first - m_latency.nodes[index].input, meterPre);
			m_meters.process(index, true, first - m_latency.nodes[index].output, meterPost);
		};
		if (track.muted) {
			capture();
			continue; // Mute wins, including pre-fader sends.
		}
		if (route.bus && m_routing.solo && track.solo) {
			auto pending = bus(node.busIndex, 0), audible = bus(node.busIndex, 1);
			for (size_t i = 0; i < count; ++i) {
				audible[i] += pending[i];
				pending[i] = 0;
			}
		}
		const bool residual = m_effectsActive && route.bus && m_routing.solo && !track.solo && node.reachesSolo &&
		                      !m_effectProcessors[size_t(index) * domains].empty();
		// Process the audible branch first. For nonlinear inserts, pending is
		// F(audible + pending) - F(audible), not F(pending). Thus a downstream
		// solo bus receives the actual combined dynamics/delay result while a
		// parallel master route receives only the independently audible branch.
		for (int pass = 0; pass < domains; ++pass) {
			const int domain = domains - pass - 1;
			const bool audible = !m_routing.solo || domain == 1;
			if (!audible && (!node.reachesSolo || track.solo))
				continue;
			if (!route.bus && m_routing.solo && domain != int(track.solo))
				continue;
			std::fill(pre.begin(), pre.end(), 0.0);
			std::fill(post.begin(), post.end(), 0.0);
			if (route.bus) {
				auto input = bus(node.busIndex, domain);
				for (int frame = 0; frame < frames; ++frame) {
					if (frame % 1024 == 0 && cancelled())
						return fail(BlockStatus::Cancelled);
					const auto at = size_t(frame * 2);
					pre[at] = input[at + (route.swapChannels ? 1 : 0)] * (route.invertLeft ? -1 : 1);
					pre[at + 1] = input[at + (route.swapChannels ? 0 : 1)] * (route.invertRight ? -1 : 1);
					const auto time =
					    audioLoopFrame(std::max<qint64>(0, first + frame - m_latency.nodes[index].input), m_loop);
					const double gain =
					    std::pow(10.0, (track.gainDb + audioAutomationValue(track.gainAutomation, time, 0)) / 20);
					const double pan = audioAutomationValue(track.panAutomation, time, track.pan);
					post[at] = pre[at] * gain * leftBalance(pan);
					post[at + 1] = pre[at + 1] * gain * rightBalance(pan);
				}
			} else
				for (const auto &region : track.regions) {
					if (region.muted)
						continue;
					const auto &media = session.sources[m_sourceIndices.value(region.sourceId)].audio.clip;
					const qint64 begin = std::max(authoredFirst, region.position),
					             end = std::min(authoredEnd, region.position + region.length);
					const double clipGain = std::pow(10.0, region.gainDb / 20);
					for (qint64 frame = begin; frame < end; ++frame) {
						if ((frame - begin) % 1024 == 0 && cancelled())
							return fail(BlockStatus::Cancelled);
						if (live && live->replacePlayback && frame >= authoredFirst + live->offsetFrames &&
						    frame < authoredFirst + live->offsetFrames +
						                qint64(live->samples.size() / size_t(live->channels)))
							continue;
						const auto local = frame - region.position;
						const double fade = audioSessionRegionFade(region, local);
						const auto read = (region.sourceOffset + local) * media.channels;
						const bool mono = media.channels == 1;
						const double left = media.samples[read + (mono || !route.swapChannels ? 0 : 1)] * clipGain *
						                    fade * (route.invertLeft ? -1 : 1);
						const double right = media.samples[read + (mono || route.swapChannels ? 0 : 1)] * clipGain *
						                     fade * (route.invertRight ? -1 : 1);
						const double gain =
						    std::pow(10.0, (track.gainDb + audioAutomationValue(track.gainAutomation, frame, 0)) / 20);
						const double pan = audioAutomationValue(track.panAutomation, frame, track.pan);
						const auto at = size_t((frame - authoredFirst) * 2);
						const double centre = mono ? std::numbers::sqrt2 / 2 : 1;
						pre[at] += left * centre;
						pre[at + 1] += right * centre;
						post[at] +=
						    left * gain *
						    (mono ? (pan >= 1 ? 0 : std::cos((pan + 1) * std::numbers::pi / 4)) : leftBalance(pan));
						post[at + 1] +=
						    right * gain *
						    (mono ? (pan <= -1 ? 0 : std::sin((pan + 1) * std::numbers::pi / 4)) : rightBalance(pan));
					}
				}
			if (live) {
				const bool mono = live->channels == 1;
				const int inputFrames = int(live->samples.size() / size_t(live->channels));
				for (int frame = 0; frame < inputFrames; ++frame) {
					if (frame % 1024 == 0 && cancelled())
						return fail(BlockStatus::Cancelled);
					const auto read = size_t(frame * live->channels);
					const double left = live->samples[read + (mono || !route.swapChannels ? 0 : 1)] * live->gain *
					                    (route.invertLeft ? -1 : 1);
					const double right = live->samples[read + (mono || route.swapChannels ? 0 : 1)] * live->gain *
					                     (route.invertRight ? -1 : 1);
					const auto time = audioLoopFrame(first + live->offsetFrames + frame, m_loop);
					const double gain =
					    std::pow(10.0, (track.gainDb + audioAutomationValue(track.gainAutomation, time, 0)) / 20);
					const double pan = audioAutomationValue(track.panAutomation, time, track.pan);
					const auto at = size_t((live->offsetFrames + frame) * 2);
					const double centre = mono ? std::numbers::sqrt2 / 2 : 1;
					pre[at] += left * centre;
					pre[at + 1] += right * centre;
					post[at] += left * gain *
					            (mono ? (pan >= 1 ? 0 : std::cos((pan + 1) * std::numbers::pi / 4)) : leftBalance(pan));
					post[at + 1] +=
					    right * gain *
					    (mono ? (pan <= -1 ? 0 : std::sin((pan + 1) * std::numbers::pi / 4)) : rightBalance(pan));
				}
			}
			if (index == m_targetIndex && m_preFaderTarget)
				for (size_t i = 0; i < count; ++i)
					target[i] += pre[i];
			if (m_effectsActive && !(index == m_targetIndex && m_preFaderTarget)) {
				if (residual && domain == 1)
					std::copy(post.begin(), post.end(), audibleDry.begin());
				if (residual && domain == 0)
					for (size_t i = 0; i < count; ++i)
						post[i] += audibleDry[i];
				const auto status = processEffects(m_effectProcessors[size_t(index) * domains + domain], post);
				if (status != BlockStatus::Ready)
					return fail(status);
				if (residual && domain == 1)
					std::copy(post.begin(), post.end(), audibleWet.begin());
				if (residual && domain == 0)
					for (size_t i = 0; i < count; ++i)
						post[i] -= audibleWet[i];
			}
			// Include pending contributions which reach a downstream solo bus.
			// Summing the nonlinear residual with its audible branch recovers
			// the actual combined strip output on that selected path.
			if (index == m_targetIndex && !m_preFaderTarget)
				for (size_t i = 0; i < count; ++i)
					target[i] += post[i];
			if (m_meters.enabled())
				for (size_t i = 0; i < count; ++i) {
					meterPre[i] += pre[i];
					meterPost[i] += post[i];
				}
			// Keep both signal domains separate until a selected bus is crossed.
			// This prevents parallel routes from bypassing bus solo isolation.
			const auto forward = [&](int target, std::span<const double> samples, double gain, double pan,
			                         AudioLatencyLine *delay) {
				if (m_targetIndex >= 0 && (target < 0 || !m_included[size_t(target)]))
					return;
				if (!audible && (target < 0 || !m_routing.nodes[target].reachesSolo))
					return;
				auto destination = target < 0 ? master : bus(m_routing.nodes[target].busIndex, domain);
				const double left = gain * leftBalance(pan), right = gain * rightBalance(pan);
				for (size_t i = 0; i < count; i += 2) {
					const auto delayed = delay ? delay->tick(samples[i], samples[i + 1])
					                           : std::array<double, 2>{samples[i], samples[i + 1]};
					destination[i] += delayed[0] * left;
					destination[i + 1] += delayed[1] * right;
				}
			};
			auto *delays = m_routeDelays.empty() ? nullptr : &m_routeDelays[size_t(index) * domains + domain];
			if (route.outputEnabled)
				forward(node.output, post, 1, 0, delays ? &delays->output : nullptr);
			for (qsizetype i = 0; i < route.sends.size(); ++i) {
				const auto &send = route.sends[i];
				if (send.enabled)
					forward(node.sends[i], send.preFader ? pre : post, std::pow(10.0, send.gainDb / 20), send.pan,
					        delays ? &delays->sends[size_t(i)] : nullptr);
			}
		}
		capture();
	}
	m_meters.process(int(session.tracks.size()), false, first - m_latency.masterInput, master);
	const double gain = m_targetIndex < 0 ? std::pow(10.0, m_session.masterGainDb / 20) : 1;
	if (m_effectsActive && m_targetIndex < 0) {
		for (auto &sample : master)
			sample *= gain;
		const auto status = processEffects(m_masterProcessor, master);
		if (status != BlockStatus::Ready)
			return fail(status);
	}
	for (size_t i = 0; i < count; ++i) {
		if (i % 2048 == 0 && cancelled())
			return fail(BlockStatus::Cancelled);
		const double sample = target[i] * (m_effectsActive ? 1 : gain);
		if (!std::isfinite(sample) || std::abs(sample) > std::numeric_limits<float>::max())
			return fail(BlockStatus::Overflow);
		output[i] = float(sample);
	}
	m_meters.process(int(session.tracks.size()), true, first - m_latency.total, std::span<const float>(output));
	return BlockStatus::Ready;
}
} // namespace vibestudio
