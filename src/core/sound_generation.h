#pragma once

// Generated sound effects for idTech games: a description in, a game-ready
// sound out.
//
// The sound comes from a model (ElevenLabs' sound effects, through
// core/ai_audio_transport.h) or from the built-in synthesizer, which needs no
// AI and no network. The synthesizer's kinds of sound (shot, explosion,
// pickup, powerup, jump, hurt, and so on) and its voice model (a waveform,
// a pitch slide, vibrato, an attack-sustain-punch-decay envelope, filters)
// follow DrPetter's sfxr (MIT; credited in docs/CREDITS.md); no sfxr code is
// used. Words in the description shape it ("heavy", "metal", "distant"), and
// the same description and seed always make the same sound.
//
// Either way the sound is made mono, trimmed of silence, faded, normalized,
// given a seamless loop where asked (a loop marker the games read), and
// delivered through core/audio_delivery.h in the game's own format: a DMX
// lump in a PWAD for Doom, 11 kHz 8-bit WAV for Quake, 22 kHz 16-bit WAV for
// Quake II and III, each with a record of what made it.

#include "core/audio_clip.h"
#include "core/audio_delivery.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vibestudio {

struct SoundGameProfile {
	// doom, quake, quake2, quake3, or generic.
	QString id;
	QString displayName;
	AudioDeliveryPreset preset = AudioDeliveryPreset::Wav;
	// Doom's sounds are DS lumps in a WAD; the others are files under sound/.
	bool lumps = false;
};

[[nodiscard]] QVector<SoundGameProfile> soundGameProfiles();
[[nodiscard]] QStringList soundGameProfileIds();
// Takes the ids and a few aliases (doom2, q1, q2, q3, wav).
bool soundGameProfileForId(const QString& id, SoundGameProfile* profile);

// The kinds of sound the synthesizer makes and the model is told about.
[[nodiscard]] QStringList soundGenerationKindIds();
[[nodiscard]] QString soundGenerationKindDisplayName(const QString& kind);
// The kind a description reads as ("rocket explosion" is an explosion), or
// "impact" when nothing in it says.
[[nodiscard]] QString soundKindFromPrompt(const QString& prompt);
// Seconds the synthesizer makes of a kind when no length is asked for.
[[nodiscard]] double soundGenerationDefaultDuration(const QString& kind);
// Ambience and alarms loop unless asked otherwise.
[[nodiscard]] bool soundGenerationKindLoops(const QString& kind);

struct SoundGenerationSpec {
	QString prompt;
	QString game = QStringLiteral("quake");
	// Empty reads it from the prompt.
	QString kind;
	// Empty makes one from the prompt.
	QString name;
	// The folder under sound/ for Quake-family games.
	QString folder = QStringLiteral("vibestudio");
	// 0 takes the kind's own length (synthesizer) or the model's choice.
	double durationSeconds = 0.0;
	bool loop = false;
	// How closely the model follows the description, 0 to 1; negative leaves
	// the provider's default.
	double promptInfluence = -1.0;
	// -1 makes one from the description.
	qint64 seed = -1;
	int variants = 1;
};

// Known ids, bounded numbers, a kind and a seed filled in.
[[nodiscard]] SoundGenerationSpec normalizedSoundGenerationSpec(const SoundGenerationSpec& spec);
// One variant of several: its own seed and name.
[[nodiscard]] SoundGenerationSpec soundGenerationVariantSpec(const SoundGenerationSpec& spec, int index, int count);
// What the game file or lump is called: DSDOORSL for Doom, door_slam for the rest.
[[nodiscard]] QString soundGenerationName(const SoundGenerationSpec& spec);
// Where it goes in a package: DSDOORSL, or sound/vibestudio/door_slam.wav.
[[nodiscard]] QString soundGenerationVirtualPath(const SoundGenerationSpec& spec);
// How maps and scripts name it: vibestudio/door_slam.wav for Quake and Quake
// II (under sound/), sound/vibestudio/door_slam.wav for Quake III, the lump
// name for Doom.
[[nodiscard]] QString soundGenerationReference(const SoundGenerationSpec& spec);
// The model's prompt, in English: the model reads it.
[[nodiscard]] QString soundGenerationPrompt(const SoundGenerationSpec& spec);

// The built-in synthesizer: mono, 44100 Hz.
[[nodiscard]] AudioClip synthesizeSound(const SoundGenerationSpec& spec);

struct GeneratedSound {
	bool ok = false;
	QString error;
	QString name;
	QString virtualPath;
	QString reference;
	// "synth", or "ai:<connector>/<model>".
	QString source;
	qint64 seed = -1;
	// The finished working sound: mono, at the source's rate, markers set.
	AudioClip clip;
	// The game's bytes.
	AudioDeliveryResult delivery;
	double peakDecibels = 0.0;
	qint64 durationMsecs = 0;
	QStringList notes;
};

// Mono, trimmed, faded, normalized to -1 dBFS, looped where asked, then
// delivered in the game's format.
[[nodiscard]] GeneratedSound processGeneratedSound(const AudioClip& raw, const SoundGenerationSpec& spec, const QString& source);
// The same for a model's encoded answer (MP3, WAV, FLAC, or Ogg Vorbis).
[[nodiscard]] GeneratedSound processGeneratedSoundBytes(const QByteArray& encoded, const QString& mimeType, const SoundGenerationSpec& spec,
	const QString& source);

struct SoundGenerationOutput {
	// The project or output folder.
	QString folder;
	// Doom: the PWAD the lump goes into; empty uses wads/vibestudio_sounds.wad
	// under the folder.
	QString wadPath;
	bool replaceExisting = false;
	bool dryRun = false;
	// More to record beside the spec: connector, model, prompt sent.
	QJsonObject provenance;
};

struct SoundGenerationWriteReport {
	bool ok = false;
	QString error;
	// Refused because the file or lump is there and replacing was not allowed.
	bool alreadyExists = false;
	QStringList writtenPaths;
	QString reference;
	QStringList notes;
};

// Writes the sound where its game reads it, then a record of what made it to
// .vibestudio/generated/sounds/<name>.json.
SoundGenerationWriteReport writeGeneratedSound(const GeneratedSound& sound, const SoundGenerationSpec& spec, const SoundGenerationOutput& output);

[[nodiscard]] QJsonObject soundGenerationSpecJson(const SoundGenerationSpec& spec);
[[nodiscard]] QJsonObject generatedSoundJson(const GeneratedSound& sound);
[[nodiscard]] QJsonObject soundGenerationWriteReportJson(const SoundGenerationWriteReport& report);
// Name, length, format, and peak, for lists and the CLI.
[[nodiscard]] QString generatedSoundSummary(const GeneratedSound& sound);

} // namespace vibestudio
