#pragma once
#include "core/audio_range.h"
#include "tests/audio_arrangement_fixture.h"
namespace vibestudio::test
{
inline AudioSession rangeFixture(bool routed = false)
{
	auto session = arrangementFixture();
	session.name = "Range fixture";
	session.musicalTime = {90, 7, 8, {{3360, 150}}, {{5, 3, 4}}};
	if (routed) {
		for (auto &track : session.tracks)
			track.routing.outputId = "bus";
		AudioSessionTrack bus;
		bus.id = bus.name = "bus";
		bus.routing.bus = true;
		session.tracks.append(bus);
	}
	using Curve = AudioAutomationCurve;
	for (auto &track : session.tracks) {
		track.gainAutomation = {{13, -12, Curve::Smooth}, {109, 0}, {151, -6, Curve::Step}, {230, -2}};
		track.panAutomation = {{0, -.5}, {87, .6, Curve::Smooth}, {198, 0}};
		if (routed) {
			track.effects = {makeAudioEffect("gain", session.sampleRate)};
			track.effectAutomation = {{track.effects[0].id, "gainDb", true, {{5, -9, Curve::Smooth}, {179, 3}}}};
		}
	}
	if (routed) {
		session.masterEffects = {makeAudioEffect("gain", session.sampleRate)};
		session.masterEffectAutomation = {
		    {session.masterEffects[0].id, "gainDb", true, {{0, -4, Curve::Smooth}, {241, 0}}}};
	}
	return session;
}
inline AudioRangeEdit rangeEdit(const QString &operation, qint64 first = 60, qint64 end = 83)
{
	AudioRangeEdit result;
	result.operation = operation;
	result.first = first;
	result.end = end;
	result.allTracks = true;
	result.masterAutomation = operation != "clear";
	return result;
}
} // namespace vibestudio::test
