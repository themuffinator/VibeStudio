#include "core/audio_tempo.h"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
		std::cerr << message << '\n';
	return condition;
}
bool timing()
{
	bool ok = true;
	AudioTempoMap map;
	map.tempoChanges = {{3840, 60}, {9600, 180}};
	map.meterChanges = {{3, 7, 8}, {5, 3, 4}};
	AudioTempoTimeline time;
	ok &= expect(time.prepare(map, 48000).isEmpty(), "prepare mixed tempo and meter");
	for (const auto &[tick, frame] : {std::pair<qint64, qint64>{0, 0},
	                                  {3840, 96000},
	                                  {7680, 288000},
	                                  {9600, 384000},
	                                  {11040, 408000},
	                                  {14400, 464000}})
		ok &= expect(time.frameAtTick(tick) == frame && time.tickAtFrame(frame) == tick,
		             "independent piecewise integration oracle");
	ok &= expect(time.frameAtPosition({3, 1, 0}) == 288000 && time.frameAtPosition({4, 1, 0}) == 408000 &&
	                 time.frameAtPosition({5, 1, 0}) == 464000,
	             "bar boundaries cross 4/4, 7/8 and 3/4");
	ok &= expect(time.tickAtPosition({4, 7, 479}) == 14399 &&
	                 time.positionAtTick(14400) == AudioMusicalPosition{5, 1, 0} &&
	                 time.frameAtPosition({4, 8, 0}) == -1 && time.frameAtPosition({4, 7, 480}) == -1 &&
	                 time.frameAtPosition({0, 1, 0}) == -1,
	             "notated beat/tick bounds follow each meter");
	ok &= expect(time.tempoAtFrame(95999) == 120 && time.tempoAtFrame(96000) == 60 && time.tempoAtFrame(384000) == 180,
	             "tempo boundary is effective at its rounded absolute frame");
	ok &= expect(time.snapFrame(120000, AudioMusicalGrid::Beat) == 144000 &&
	                 time.stepFrame(96000, AudioMusicalGrid::Beat, -1) == 72000 &&
	                 time.stepFrame(96000, AudioMusicalGrid::Beat, 1) == 144000 &&
	                 time.stepFrame(288000, AudioMusicalGrid::Beat, -1) == 240000 &&
	                 time.stepFrame(288000, AudioMusicalGrid::Beat, 1) == 312000 &&
	                 time.stepFrame(288000, AudioMusicalGrid::BeatTriplet, 1) == 296000 &&
	                 time.stepFrame(408000, AudioMusicalGrid::Bar, 1) == 464000,
	             "snap by frame distance, ties later, variable tempo and meter-aware stepping");
	// A tempo change inside a beat changes the temporal midpoint, not the grid's tick positions.
	map.tempoChanges = {{480, 60}};
	map.meterChanges.clear();
	ok &= expect(time.prepare(map, 48000).isEmpty() && time.frameAtTick(960) == 36000 &&
	                 time.snapFrame(17999, AudioMusicalGrid::Beat) == 0 &&
	                 time.snapFrame(18000, AudioMusicalGrid::Beat) == 36000,
	             "off-beat tempo change uses actual frame-distance snapping");
	map.tempo = 137.5;
	map.tempoChanges.clear();
	for (int i = 1; i <= AudioTempoChangeLimit; ++i)
		map.tempoChanges.append({qint64(i) * 12345, i % 2 ? 82.5 : 137.5});
	ok &= expect(time.prepare(map, 44100).isEmpty(), "maximum map size accepted");
	qint64 numerator = 0;
	for (int i = 1; i <= AudioTempoChangeLimit; ++i) {
		numerator += 12345LL * 44100 * (i % 2 ? 3 : 5);
		ok &= expect(time.frameAtTick(qint64(i) * 12345) == (numerator + 3300) / 6600,
		             "4096 segments retain fractional frames instead of accumulating beat rounding");
	}
	for (const int rate : {1, 17, 1000, 44100, 48000, 384000}) {
		map = {};
		map.tempo = 400;
		map.beatsPerBar = 32;
		map.beatUnit = 32;
		ok &= expect(time.prepare(map, rate).isEmpty(), "all supported rates and smallest notated beat");
		for (const qint64 frame : {0LL, 1LL, 2LL, 7000LL, AudioAutomationFrameLimit - 1, AudioAutomationFrameLimit}) {
			const auto tick = time.tickAtFrame(frame);
			const auto previous = time.stepFrame(frame, AudioMusicalGrid::QuarterBeat, -1);
			const auto next = time.stepFrame(frame, AudioMusicalGrid::QuarterBeat, 1);
			ok &=
			    expect(time.frameAtTick(tick) >= 0, "every displayed position remains navigable at the timeline limit");
			ok &= expect(tick >= 0 && previous >= 0 && previous <= frame && next >= frame &&
			                 next <= AudioAutomationFrameLimit,
			             "bounded low-rate and near-limit grid navigation");
		}
		if (rate == 1)
			ok &= expect(time.stepFrame(1, AudioMusicalGrid::QuarterBeat, 1) == 2 &&
			                 time.stepFrame(1, AudioMusicalGrid::QuarterBeat, -1) == 0,
			             "many ticks per sample do not trap grid stepping on duplicate frames");
	}
	AudioMusicalPosition position;
	ok &= expect(
	    parseAudioMusicalPosition("123.7.479", &position) && audioMusicalPositionText(position) == "123.7.479" &&
	        !parseAudioMusicalPosition("0.1.0", &position) && !parseAudioMusicalPosition("2.1.0junk", &position) &&
	        !parseAudioMusicalPosition("999999999999999999999.1.0", &position),
	    "strict musical position grammar");
	for (int bad = 0; bad < 10; ++bad) {
		map = {};
		if (bad == 0)
			map.tempo = std::numeric_limits<double>::quiet_NaN();
		if (bad == 1)
			map.beatUnit = 3;
		if (bad == 2)
			map.tempoChanges = {{1, 120}, {1, 90}};
		if (bad == 3)
			map.tempoChanges = {{0, 120}};
		if (bad == 4)
			map.tempoChanges = {{AudioMusicalTickLimit, 120}};
		if (bad == 5)
			map.meterChanges = {{1, 3, 4}};
		if (bad == 6)
			map.meterChanges = {{4, 3, 4}, {3, 4, 4}};
		if (bad == 7)
			map.meterChanges = {{std::numeric_limits<qint64>::max(), 3, 4}};
		if (bad == 8)
			map.tempoChanges.fill({1, 120}, AudioTempoChangeLimit + 1);
		if (bad == 9)
			map.meterChanges = {{2, 0, 4}};
		ok &= expect(!time.prepare(map, 48000).isEmpty() && !time.ready() && time.frameAtTick(0) == -1,
		             "invalid map clears prepared state and rejects arithmetic/order/range faults");
	}
	return ok;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const bool ok = timing();
	std::cout << (ok ? "Audio tempo timing verification passed\n" : "Audio tempo timing verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
