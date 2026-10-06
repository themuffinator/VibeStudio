#include "core/audio_latency.h"
#include "core/audio_session.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>

namespace vibestudio
{
QString prepareAudioLatency(const AudioSession &session, const AudioRoutingPlan &routing,
                            std::span<const int> stripLatency, int masterLatency, AudioLatencyPlan *result,
                            std::span<const bool> included, int target, bool preFader)
{
	if (result)
		*result = {};
	const int count = int(session.tracks.size());
	const auto invalid = [] {
		return QCoreApplication::translate("AudioLatency",
		                                   "Processing latency exceeds the supported graph or memory bounds.");
	};
	if (count > AudioSessionTrackLimit || stripLatency.size() != size_t(count) || routing.nodes.size() != count ||
	    routing.order.size() != count || (!included.empty() && included.size() < size_t(count)) || target < -1 ||
	    target >= count || masterLatency < 0 || masterLatency > AudioProcessingLatencyLimit)
		return invalid();
	AudioLatencyPlan plan;
	plan.nodes.resize(count);
	const auto active = [&](int index) {
		return index >= 0 && index < count && (included.empty() || included[size_t(index)]);
	};
	if (target >= 0 && !active(target))
		return invalid();
	const auto destinationActive = [&](int index) { return index < 0 ? target < 0 : active(index); };
	std::array<bool, AudioSessionTrackLimit> visited{};
	for (int index : routing.order) {
		if (index < 0 || index >= count || visited[size_t(index)] || stripLatency[size_t(index)] < 0 ||
		    stripLatency[size_t(index)] > AudioProcessingLatencyLimit)
			return invalid();
		visited[size_t(index)] = true;
		auto &node = plan.nodes[index];
		const auto &edge = routing.nodes[index];
		const auto &route = session.tracks[index].routing;
		if (edge.sends.size() != route.sends.size())
			return invalid();
		node.sendDelays.resize(route.sends.size());
		if (!active(index))
			continue;
		if (node.input > AudioProcessingLatencyLimit - stripLatency[size_t(index)])
			return invalid();
		node.output = node.input + stripLatency[size_t(index)];
		const auto forward = [&](int destination, int latency) {
			if (destination < -1 || destination >= count || (destination >= 0 && visited[size_t(destination)]))
				return false;
			if (!destinationActive(destination))
				return true;
			if (destination < 0)
				plan.masterInput = std::max(plan.masterInput, latency);
			else
				plan.nodes[destination].input = std::max(plan.nodes[destination].input, latency);
			return true;
		};
		if (route.outputEnabled && !forward(edge.output, node.output))
			return invalid();
		for (qsizetype i = 0; i < route.sends.size(); ++i)
			if (route.sends[i].enabled && !forward(edge.sends[i], route.sends[i].preFader ? node.input : node.output))
				return invalid();
	}
	if (target < 0) {
		if (plan.masterInput > AudioProcessingLatencyLimit - masterLatency)
			return invalid();
		plan.total = plan.masterInput + masterLatency;
	} else
		plan.total = preFader ? plan.nodes[target].input : plan.nodes[target].output;
	for (int index = 0; index < count; ++index) {
		if (!active(index))
			continue;
		auto &node = plan.nodes[index];
		const auto &edge = routing.nodes[index];
		const auto &route = session.tracks[index].routing;
		const auto delay = [&](int destination, int latency) {
			if (!destinationActive(destination))
				return 0;
			const int frames = (destination < 0 ? plan.masterInput : plan.nodes[destination].input) - latency;
			plan.bytes += quint64(frames) * 2 * sizeof(double) * (routing.solo ? 2 : 1);
			return frames;
		};
		if (route.outputEnabled)
			node.outputDelay = delay(edge.output, node.output);
		for (qsizetype i = 0; i < route.sends.size(); ++i)
			if (route.sends[i].enabled)
				node.sendDelays[i] = delay(edge.sends[i], route.sends[i].preFader ? node.input : node.output);
	}
	if (plan.bytes > AudioEffectMemoryLimit)
		return invalid();
	if (result)
		*result = std::move(plan);
	return {};
}
bool AudioLatencyLine::prepare(int frames)
{
	m_audio = std::vector<double>{};
	if (frames < 0 || frames > AudioProcessingLatencyLimit)
		return false;
	m_audio.resize(size_t(frames) * 2);
	reset();
	return true;
}
QJsonObject audioSessionLatencyReport(const AudioSession &session)
{
	if (const auto error = validateAudioSessionStructure(session); !error.isEmpty())
		return {{"error", error}};
	AudioRoutingPlan routing;
	prepareAudioRouting(session, &routing);
	std::array<int, AudioSessionTrackLimit> latency{};
	for (qsizetype i = 0; i < session.tracks.size(); ++i)
		latency[size_t(i)] = audioEffectLatencyFrames(session.tracks[i].effects, session.sampleRate);
	const int master = audioEffectLatencyFrames(session.masterEffects, session.sampleRate);
	AudioLatencyPlan plan;
	const auto error =
	    prepareAudioLatency(session, routing, std::span(latency).first(size_t(session.tracks.size())), master, &plan);
	if (!error.isEmpty())
		return {{"error", error}};
	QJsonArray strips;
	for (qsizetype i = 0; i < session.tracks.size(); ++i) {
		const auto &node = plan.nodes[i];
		QJsonArray sends;
		for (int frames : node.sendDelays)
			sends.append(frames);
		strips.append(QJsonObject{{"id", session.tracks[i].id},
		                          {"inputFrames", node.input},
		                          {"insertFrames", latency[size_t(i)]},
		                          {"outputFrames", node.output},
		                          {"outputCompensationFrames", node.outputDelay},
		                          {"sendCompensationFrames", sends}});
	}
	return {{"frames", plan.total},
	        {"milliseconds", double(plan.total) * 1000 / session.sampleRate},
	        {"masterInputFrames", plan.masterInput},
	        {"masterInsertFrames", master},
	        {"compensationBytes", qint64(plan.bytes)},
	        {"strips", strips}};
}
void AudioLatencyLine::reset() { m_cursor = m_valid = 0; }
std::array<double, 2> AudioLatencyLine::tick(double left, double right)
{
	const auto frames = m_audio.size() / 2;
	if (!frames)
		return {left, right};
	const auto output = m_valid == frames ? std::array<double, 2>{m_audio[m_cursor * 2], m_audio[m_cursor * 2 + 1]}
	                                      : std::array<double, 2>{};
	m_audio[m_cursor * 2] = left;
	m_audio[m_cursor * 2 + 1] = right;
	m_cursor = (m_cursor + 1) % frames;
	m_valid = std::min(m_valid + 1, frames);
	return output;
}
} // namespace vibestudio
