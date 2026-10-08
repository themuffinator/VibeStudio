// Sound cues: each is a well-formed WAV, short, at a known peak, free of
// clicks at its ends, and its pitch contour (rising, level, falling) is the
// one that carries its meaning. Played cues are logged, not sounded.

#include "app/studio_sound_cues.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

quint16 read16(const QByteArray& bytes, int offset)
{
	return qFromLittleEndian<quint16>(bytes.constData() + offset);
}

quint32 read32(const QByteArray& bytes, int offset)
{
	return qFromLittleEndian<quint32>(bytes.constData() + offset);
}

// Frequency from zero crossings: the tone's overtone never adds a crossing.
double crossingFrequency(const QVector<qint16>& samples, int first, int end)
{
	int crossings = 0;
	for (int index = first + 1; index < end; ++index) {
		if ((samples[index - 1] < 0) != (samples[index] < 0)) {
			++crossings;
		}
	}
	return double(crossings) / 2.0 / (double(end - first) / 44100.0);
}

bool checkCue(SoundCue cue, int contour)
{
	bool ok = true;
	const QByteArray wave = soundCueWave(cue);
	ok &= expect(wave.size() > 44, "A cue should hold samples after its header.");
	if (wave.size() <= 44) {
		return false;
	}
	ok &= expect(wave.startsWith("RIFF") && wave.mid(8, 8) == "WAVEfmt " && wave.mid(36, 4) == "data", "A cue should be a RIFF WAVE file.");
	ok &= expect(read32(wave, 4) == quint32(wave.size() - 8), "The RIFF size should cover the file.");
	ok &= expect(read16(wave, 20) == 1 && read16(wave, 22) == 1 && read32(wave, 24) == 44100 && read16(wave, 34) == 16,
		"A cue should be 16-bit mono PCM at 44.1 kHz.");
	ok &= expect(read32(wave, 40) == quint32(wave.size() - 44), "The data size should cover the samples.");

	QVector<qint16> samples;
	for (int offset = 44; offset + 1 < wave.size(); offset += 2) {
		samples.push_back(qint16(read16(wave, offset)));
	}
	const double seconds = double(samples.size()) / 44100.0;
	ok &= expect(seconds > 0.2 && seconds < 0.5, "A cue should last between 0.2 and 0.5 seconds.");
	int peak = 0;
	for (qint16 sample : std::as_const(samples)) {
		peak = std::max(peak, std::abs(int(sample)));
	}
	ok &= expect(peak > 16000 && peak < 16500, "A cue should peak at half of full scale, before the volume.");
	ok &= expect(std::abs(int(samples.first())) < 200 && std::abs(int(samples.last())) < 200, "A cue should start and end near silence.");

	// The two notes sit either side of the silent rest between them.
	int restStart = -1;
	int restEnd = -1;
	for (int index = 0, run = 0; index < samples.size(); ++index) {
		run = samples[index] == 0 ? run + 1 : 0;
		if (run == 200 && restStart < 0) {
			restStart = index - 199;
		}
		if (restStart >= 0 && restEnd < 0 && run == 0 && index > restStart) {
			restEnd = index;
		}
	}
	ok &= expect(restStart > 0 && restEnd > restStart, "A cue should have a rest between its two notes.");
	if (restStart > 0 && restEnd > restStart) {
		const double first = crossingFrequency(samples, 0, restStart);
		const double second = crossingFrequency(samples, restEnd, int(samples.size()));
		if (contour > 0) {
			ok &= expect(second > first * 1.2, "The finished cue should rise.");
		} else if (contour < 0) {
			ok &= expect(second < first / 1.2, "The failed cue should fall.");
		} else {
			ok &= expect(std::abs(second - first) < first * 0.05, "The warning cue should stay level.");
		}
	}
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("VIBESTUDIO_SOUND_CUES", "log");
	QCoreApplication app(argc, argv);
	bool ok = true;
	ok &= checkCue(SoundCue::Success, 1);
	ok &= checkCue(SoundCue::Warning, 0);
	ok &= checkCue(SoundCue::Failure, -1);
	ok &= expect(soundCueWave(SoundCue::Success) != soundCueWave(SoundCue::Failure), "Each cue should sound different.");
	ok &= expect(soundCueId(SoundCue::Warning) == QStringLiteral("warning"), "Cue ids are stable.");

	clearLoggedSoundCuesForTesting();
	StudioSoundCues cues;
	ok &= expect(cues.playsTones(), "The logging stand-in counts as tones.");
	cues.play(SoundCue::Success, 60);
	cues.play(SoundCue::Failure, 0);
	cues.play(SoundCue::Warning, 250);
	ok &= expect(loggedSoundCuesForTesting() == QStringList({QStringLiteral("success"), QStringLiteral("warning")}),
		"Played cues should be logged, and a silent one skipped.");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
