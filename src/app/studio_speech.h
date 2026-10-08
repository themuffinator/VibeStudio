#pragma once

// Spoken feedback through the speech engine the operating system provides.
//
// Windows speaks through the Speech API (SAPI 5), which every desktop Windows
// ships with. macOS speaks through `say`, and Linux through Speech Dispatcher's
// `spd-say` or, failing that, eSpeak NG. Nothing leaves the machine: the studio
// never hands text to a cloud voice service from here. When no engine is found
// the studio stays fully usable, every spoken message already has its visual
// and log equivalent, and unavailableReason() says why nothing is heard.

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace vibestudio {

struct SpeechVoice {
	// Stable for the engine: what the voice preference stores.
	QString id;
	QString name;
	// BCP 47 language tag when the engine reports one ("en-US"), else empty.
	QString language;
};

struct SpeechSettings {
	// An id from voices(); empty speaks with the engine's default voice.
	QString voiceId;
	// -10 (slowest) to 10 (fastest); 0 is the engine's normal rate.
	int rate = 0;
	// -10 (lowest) to 10 (highest); 0 is the voice's normal pitch.
	int pitch = 0;
	// 0 to 100.
	int volume = 100;
};

// What an engine can change, so Settings can say which controls apply.
struct SpeechCapabilities {
	bool voices = false;
	bool rate = false;
	bool pitch = false;
	bool volume = false;
};

class SpeechBackend;

class StudioSpeech final {
public:
	StudioSpeech();
	~StudioSpeech();
	StudioSpeech(const StudioSpeech&) = delete;
	StudioSpeech& operator=(const StudioSpeech&) = delete;

	[[nodiscard]] bool available() const;
	// The engine in words ("Windows Speech API"), or empty when there is none.
	[[nodiscard]] QString engineName() const;
	// Why nothing can be spoken, in words, when available() is false.
	[[nodiscard]] QString unavailableReason() const;
	[[nodiscard]] SpeechCapabilities capabilities() const;
	[[nodiscard]] QVector<SpeechVoice> voices() const;

	void setSettings(const SpeechSettings& settings);
	[[nodiscard]] SpeechSettings settings() const;

	// Speaks without blocking, cutting off anything still being spoken: the
	// newest message is the one that matters. Returns false when nothing could
	// be started.
	bool say(const QString& text);
	// Speaks and waits up to timeoutMs for the engine to finish; for the CLI,
	// which has no event loop to keep the words going.
	bool sayAndWait(const QString& text, int timeoutMs);
	void stop();
	[[nodiscard]] bool isSpeaking() const;

private:
	std::unique_ptr<SpeechBackend> m_backend;
	SpeechSettings m_settings;
};

// The rate, pitch, and volume a preference may hold, clamped into range.
[[nodiscard]] int normalizedSpeechRate(int rate);
[[nodiscard]] int normalizedSpeechPitch(int pitch);
[[nodiscard]] int normalizedSpeechVolume(int volume);
// The sentence Settings, setup, and `accessibility speak --test` say.
[[nodiscard]] QString speechTestPhrase();

// What the silent "log" engine (VIBESTUDIO_SPEECH_ENGINE=log) was asked to
// say, oldest first. Tests listen through these; no other engine records.
[[nodiscard]] QStringList loggedSpeechForTesting();
void clearLoggedSpeechForTesting();

} // namespace vibestudio
