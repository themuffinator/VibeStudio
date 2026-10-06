#pragma once
#include "core/audio_session.h"

namespace vibestudio::test
{
inline AudioEffect loopLookahead(int frames)
{
	auto effect = makeAudioEffect("lookahead-limiter", 1000);
	effect.parameters["lookaheadMs"] = frames;
	return effect;
}
inline AudioSession playbackLoopFixture(bool solo = false)
{
	AudioSession session;
	session.sampleRate = 1000;
	session.effectTailSeconds = 0;
	AudioSessionSource source;
	source.id = "loop-source";
	source.audio.clip = {2, 1000, QVector<float>(200)};
	source.audio.endFrame = 100;
	for (int frame = 0; frame < 100; ++frame) {
		source.audio.clip.samples[frame * 2] = float(.003 + frame * .0001);
		source.audio.clip.samples[frame * 2 + 1] = float(-.003 - frame * .0002);
	}
	session.sources = {source};
	for (int i = 0; i < 4; ++i) {
		AudioSessionTrack track;
		track.id = track.name = QString::number(i);
		track.routing.bus = i >= 2;
		track.routing.outputId = i < 2 ? "2" : i == 2 ? "3" : QString();
		track.solo = solo && (i == 0 || i == 3);
		if (i < 2)
			track.regions = {{QString("clip-%1").arg(i), "Clip", source.id, 0, 0, 100}};
		if (i == 0 || i == 2)
			track.routing.sends = {{QString(), -18, .2, true, true}};
		track.routing.swapChannels = i == 1;
		track.routing.invertLeft = i == 1;
		track.gainDb = i == 1 ? -9 : 0;
		track.gainAutomation = {{0, -3}, {23, 2, AudioAutomationCurve::Smooth}, {68, -9}};
		track.panAutomation = {{0, -.5}, {23, .7}, {68, -.2}};
		auto gain = makeAudioEffect("gain", 1000);
		track.effects = {loopLookahead(i == 0 ? 7 : i == 3 ? 2 : 3), gain};
		track.effectAutomation = {{gain.id, "gainDb", true, {{0, -6}, {23, 3}, {50, -1}, {68, -12}}}};
		if (i == 2) {
			auto delay = makeAudioEffect("delay", 1000);
			delay.parameters["leftMs"] = 11;
			delay.parameters["rightMs"] = 13;
			track.effects << delay << makeAudioEffect("reverb", 1000) << makeAudioEffect("tremolo", 1000);
			track.effectAutomation << AudioEffectAutomationLane{
			    delay.id, "mix", true, {{0, .2}, {23, .8}, {68, .4, AudioAutomationCurve::Step}}};
		}
		session.tracks << track;
	}
	auto gain = makeAudioEffect("gain", 1000);
	session.masterEffects = {loopLookahead(5), gain};
	session.masterEffectAutomation = {{gain.id, "gainDb", true, {{0, -3}, {23, 1}, {68, -8}}}};
	return session;
}

// Independent oracle: build ordinary linear media and sampled step envelopes.
// No loop-mapping or transport implementation is used. These fixtures have one
// full-length, unfaded source region per audio track; all other processing stays
// intact, so the usual finite renderer provides the continuous DSP reference.
inline AudioSession expandPlaybackLoop(const AudioSession &original, qint64 first, qint64 end, int frames)
{
	AudioSession session = original;
	const auto authored = [&](qint64 frame) { return frame < end ? frame : first + (frame - end) % (end - first); };
	const auto expandLane = [&](const QVector<AudioAutomationPoint> &points) {
		QVector<AudioAutomationPoint> expanded;
		if (!points.isEmpty())
			for (int frame = 0; frame < frames; ++frame)
				expanded << AudioAutomationPoint{frame, audioAutomationValue(points, authored(frame), 0),
				                                 AudioAutomationCurve::Step};
		return expanded;
	};
	session.sources[0].audio.clip.samples.resize(frames * 2);
	session.sources[0].audio.endFrame = frames;
	for (int frame = 0; frame < frames; ++frame)
		for (int channel = 0; channel < 2; ++channel)
			session.sources[0].audio.clip.samples[frame * 2 + channel] =
			    original.sources[0].audio.clip.samples[authored(frame) * 2 + channel];
	for (auto &track : session.tracks) {
		for (auto &region : track.regions)
			region.length = frames;
		track.gainAutomation = expandLane(track.gainAutomation);
		track.panAutomation = expandLane(track.panAutomation);
		for (auto &lane : track.effectAutomation)
			lane.points = expandLane(lane.points);
	}
	for (auto &lane : session.masterEffectAutomation)
		lane.points = expandLane(lane.points);
	return session;
}
} // namespace vibestudio::test
