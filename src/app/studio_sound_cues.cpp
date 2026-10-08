#include "app/studio_sound_cues.h"

#include "app/audio_playback.h"

#include <QApplication>
#include <QVector>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio {

namespace {

constexpr int kSampleRate = 44100;
// Loudest sample, before the volume preference: -6 dBFS.
constexpr double kPeak = 0.5;

struct Note {
	double frequency;  // Hz; 0 is a rest
	int milliseconds;
};

QStringList& soundCueLog()
{
	static QStringList log;
	return log;
}

// A soft chime: a fast attack, an exponential decay, and a quiet octave
// overtone, with a short release so no note ends in a click.
void appendNote(QVector<double>& samples, const Note& note)
{
	const int count = kSampleRate * note.milliseconds / 1000;
	if (note.frequency <= 0.0) {
		samples.insert(samples.size(), count, 0.0);
		return;
	}
	const int attack = kSampleRate * 6 / 1000;
	const int release = kSampleRate * 8 / 1000;
	for (int index = 0; index < count; ++index) {
		double envelope = std::exp(-3.0 * double(index) / count);
		if (index < attack) {
			envelope *= double(index) / attack;
		}
		if (index > count - release) {
			envelope *= double(count - index) / release;
		}
		const double phase = 2.0 * std::numbers::pi * note.frequency * double(index) / kSampleRate;
		samples.push_back(envelope * (0.8 * std::sin(phase) + 0.2 * std::sin(2.0 * phase)));
	}
}

QVector<Note> notesFor(SoundCue cue)
{
	switch (cue) {
	case SoundCue::Success:
		// E5 up to A5.
		return {{659.26, 90}, {0.0, 20}, {880.00, 170}};
	case SoundCue::Warning:
		// C5 twice.
		return {{523.25, 110}, {0.0, 60}, {523.25, 110}};
	case SoundCue::Failure:
		// A4 down to E4.
		return {{440.00, 110}, {0.0, 20}, {329.63, 230}};
	}
	return {};
}

void appendLittleEndian16(QByteArray& bytes, quint16 value)
{
	const quint16 little = qToLittleEndian(value);
	bytes.append(reinterpret_cast<const char*>(&little), sizeof(little));
}

void appendLittleEndian32(QByteArray& bytes, quint32 value)
{
	const quint32 little = qToLittleEndian(value);
	bytes.append(reinterpret_cast<const char*>(&little), sizeof(little));
}

} // namespace

QString soundCueId(SoundCue cue)
{
	switch (cue) {
	case SoundCue::Success:
		return QStringLiteral("success");
	case SoundCue::Warning:
		return QStringLiteral("warning");
	case SoundCue::Failure:
		return QStringLiteral("failure");
	}
	return QStringLiteral("success");
}

QByteArray soundCueWave(SoundCue cue)
{
	QVector<double> samples;
	for (const Note& note : notesFor(cue)) {
		appendNote(samples, note);
	}
	double loudest = 0.0;
	for (double sample : std::as_const(samples)) {
		loudest = std::max(loudest, std::abs(sample));
	}
	const double gain = loudest > 0.0 ? kPeak / loudest : 0.0;

	const quint32 dataBytes = quint32(samples.size()) * 2;
	QByteArray wave;
	wave.reserve(int(44 + dataBytes));
	wave.append("RIFF", 4);
	appendLittleEndian32(wave, 36 + dataBytes);
	wave.append("WAVEfmt ", 8);
	appendLittleEndian32(wave, 16);              // fmt chunk size
	appendLittleEndian16(wave, 1);               // PCM
	appendLittleEndian16(wave, 1);               // mono
	appendLittleEndian32(wave, kSampleRate);
	appendLittleEndian32(wave, kSampleRate * 2); // bytes per second
	appendLittleEndian16(wave, 2);               // bytes per frame
	appendLittleEndian16(wave, 16);              // bits per sample
	wave.append("data", 4);
	appendLittleEndian32(wave, dataBytes);
	for (double sample : std::as_const(samples)) {
		const auto value = qint16(std::lround(std::clamp(sample * gain, -1.0, 1.0) * 32767.0));
		appendLittleEndian16(wave, quint16(value));
	}
	return wave;
}

StudioSoundCues::StudioSoundCues(QObject* parent)
	: QObject(parent)
	, m_logOnly(qEnvironmentVariable("VIBESTUDIO_SOUND_CUES") == QStringLiteral("log"))
{
	if (!m_logOnly) {
		m_playback = new AudioPlayback(this);
	}
}

bool StudioSoundCues::playsTones() const
{
	return m_logOnly || (m_playback && m_playback->available());
}

void StudioSoundCues::play(SoundCue cue, int volumePercent)
{
	const int volume = std::clamp(volumePercent, 0, 100);
	if (volume == 0) {
		return;
	}
	if (m_logOnly) {
		soundCueLog().push_back(soundCueId(cue));
		return;
	}
	if (!m_playback || !m_playback->available()) {
		QApplication::beep();
		return;
	}
	m_playback->setVolume(float(volume) / 100.0f);
	const QByteArray wave = soundCueWave(cue);
	const qint64 durationMs = qint64(wave.size() - 44) * 1000 / (kSampleRate * 2);
	if (!m_playback->startMedia(wave, QStringLiteral("cue.wav"), durationMs)) {
		QApplication::beep();
	}
}

QStringList loggedSoundCuesForTesting()
{
	return soundCueLog();
}

void clearLoggedSoundCuesForTesting()
{
	soundCueLog().clear();
}

} // namespace vibestudio
