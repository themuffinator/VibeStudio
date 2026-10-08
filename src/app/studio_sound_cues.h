#pragma once

// Sound cues: short tones that report a task's result without sight. The
// pitch contour carries the meaning, rising when work finishes, level for a
// warning or a cancellation, falling for a failure, so the three stay apart
// for people who cannot name notes. Every cue is under half a second, is
// synthesized here (no audio asset ships), and goes with words in the status
// bar, the Activity list, and any screen reader announcement.

#include <QByteArray>
#include <QObject>
#include <QStringList>

namespace vibestudio {

class AudioPlayback;

enum class SoundCue {
	Success,
	Warning,
	Failure,
};

// "success", "warning", "failure".
QString soundCueId(SoundCue cue);

// The cue as a 16-bit mono PCM WAV at 44.1 kHz.
QByteArray soundCueWave(SoundCue cue);

// Plays cues through Qt Multimedia when the build has it; otherwise the
// platform's alert sound. VIBESTUDIO_SOUND_CUES=log records cue ids instead
// of playing them, for tests.
class StudioSoundCues final : public QObject {
	Q_OBJECT

public:
	explicit StudioSoundCues(QObject* parent = nullptr);

	// False when only the platform's alert sound is available.
	bool playsTones() const;
	void play(SoundCue cue, int volumePercent);

private:
	AudioPlayback* m_playback = nullptr;
	bool m_logOnly = false;
};

// Cue ids recorded while VIBESTUDIO_SOUND_CUES=log, oldest first.
QStringList loggedSoundCuesForTesting();
void clearLoggedSoundCuesForTesting();

} // namespace vibestudio
