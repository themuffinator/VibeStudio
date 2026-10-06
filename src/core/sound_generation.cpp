#include "core/sound_generation.h"

#include "core/package_archive.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>

namespace vibestudio {

namespace {

constexpr int kSynthRate = 44100;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxVariants = 4;

// splitmix64: small, fast, and the same on every platform.
class SoundRandom {
public:
	explicit SoundRandom(quint64 seed)
		: m_state(seed)
	{
	}
	quint64 next()
	{
		quint64 z = (m_state += 0x9E3779B97F4A7C15ull);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}
	double unit()
	{
		return double(next() >> 11) * (1.0 / 9007199254740992.0);
	}
	double range(double low, double high)
	{
		return low + (high - low) * unit();
	}
	bool chance(double probability)
	{
		return unit() < probability;
	}

private:
	quint64 m_state;
};

quint64 textHash(const QString& text)
{
	quint64 hash = 1469598103934665603ull;
	for (const char byte : text.toLower().simplified().toUtf8()) {
		hash ^= quint8(byte);
		hash *= 1099511628211ull;
	}
	return hash;
}

QStringList promptWords(const QString& prompt)
{
	static const QRegularExpression separators(QStringLiteral("[^a-z0-9]+"));
	return prompt.toLower().split(separators, Qt::SkipEmptyParts);
}

// A word starting with any of the stems: "explosions" has "explo".
bool hasStem(const QStringList& words, std::initializer_list<const char*> stems)
{
	for (const QString& word : words) {
		for (const char* stem : stems) {
			if (word.startsWith(QLatin1String(stem))) {
				return true;
			}
		}
	}
	return false;
}

// Checked in order; the first kind with a word in the description wins.
QString kindForWords(const QStringList& words)
{
	if (hasStem(words, {"teleport", "warp", "portal", "respawn", "slipgate", "phase"})) {
		return QStringLiteral("teleport");
	}
	if (hasStem(words, {"explo", "blast", "boom", "bomb", "grenade", "rocket", "detonat", "nuke", "kaboom"})) {
		return QStringLiteral("explosion");
	}
	if (hasStem(words, {"laser", "blaster", "plasma", "zap", "beam", "rail", "phaser", "bfg", "lightning", "tesla", "electric", "energy"})) {
		return QStringLiteral("laser");
	}
	if (hasStem(words, {"shot", "gun", "pistol", "rifle", "chaingun", "nailgun", "bullet", "shoot", "firing", "revolver", "sniper"})) {
		return QStringLiteral("shot");
	}
	if (hasStem(words, {"powerup", "power", "quad", "invuln", "invisib", "armor", "armour", "upgrade", "haste", "regen", "megahealth", "berserk"})) {
		return QStringLiteral("powerup");
	}
	if (hasStem(words, {"pickup", "pick", "coin", "item", "ammo", "health", "medkit", "key", "collect", "bonus", "gem", "treasure"})) {
		return QStringLiteral("pickup");
	}
	if (hasStem(words, {"jump", "hop", "bounce", "leap", "spring", "launch"})) {
		return QStringLiteral("jump");
	}
	if (hasStem(words, {"death", "dead", "die", "dying", "gib", "scream", "splat", "squish"})) {
		return QStringLiteral("death");
	}
	if (hasStem(words, {"hurt", "pain", "ouch", "grunt", "wound", "injur", "groan"})) {
		return QStringLiteral("hurt");
	}
	if (hasStem(words, {"door", "gate", "hatch", "elevator", "lift", "platform", "plat", "shutter", "portcullis", "crusher", "mover", "drawbridge"})) {
		return QStringLiteral("door");
	}
	if (hasStem(words, {"switch", "button", "lever", "click", "toggle", "press", "keypad", "terminal"})) {
		return QStringLiteral("switch");
	}
	if (hasStem(words, {"foot", "step", "walk", "stomp", "boot"})) {
		return QStringLiteral("footstep");
	}
	if (hasStem(words, {"alarm", "siren", "klaxon", "alert", "warning", "beep", "buzzer", "bell"})) {
		return QStringLiteral("alarm");
	}
	if (hasStem(words, {"ambien", "hum", "drone", "wind", "rain", "water", "river", "stream", "lava", "fire", "torch", "flame", "machine", "engine",
			"generator", "reactor", "computer", "cave", "bubbl", "drip", "slime", "swamp", "hiss", "static", "atmos", "background"})) {
		return QStringLiteral("ambience");
	}
	return QStringLiteral("impact");
}

// What the description says about the sound beyond its kind.
struct Mood {
	double pitch = 1.0;
	double length = 1.0;
	bool heavy = false;
	bool light = false;
	bool metal = false;
	bool distant = false;
	bool wet = false;
	bool electric = false;
	bool stone = false;
	bool wood = false;
	bool alien = false;
};

Mood moodFrom(const QStringList& words, SoundRandom& random)
{
	Mood mood;
	mood.heavy = hasStem(words, {"heavy", "big", "huge", "giant", "massive", "large", "deep", "low", "bass", "boss", "cyber", "titan"});
	mood.light = hasStem(words, {"small", "tiny", "little", "light", "high", "short", "quick", "soft", "faint", "thin"});
	mood.metal = hasStem(words, {"metal", "steel", "iron", "clang", "chain", "mechan", "tin", "brass"});
	mood.distant = hasStem(words, {"distant", "far", "echo", "muffled", "remote"});
	mood.wet = hasStem(words, {"water", "wet", "splash", "slime", "drip", "bubbl", "underwater", "swim", "sludge", "mud"});
	mood.electric = hasStem(words, {"electric", "energy", "plasma", "laser", "zap", "buzz", "tesla", "lightning", "spark"});
	mood.stone = hasStem(words, {"stone", "rock", "concrete", "brick", "rubble", "marble"});
	mood.wood = hasStem(words, {"wood", "crate", "plank", "barrel"});
	mood.alien = hasStem(words, {"demon", "alien", "monster", "beast", "creature", "growl", "hell", "fiend", "zombie"});
	if (mood.heavy) {
		mood.pitch *= random.range(0.55, 0.75);
		mood.length *= random.range(1.2, 1.5);
	}
	if (mood.light) {
		mood.pitch *= random.range(1.3, 1.7);
		mood.length *= random.range(0.6, 0.8);
	}
	if (hasStem(words, {"slow", "long", "linger"})) {
		mood.length *= 1.4;
	}
	if (hasStem(words, {"fast", "rapid", "snappy"})) {
		mood.length *= 0.75;
	}
	return mood;
}

// One layer of a synthesized sound.
struct Voice {
	enum Wave {
		Square,
		Saw,
		Sine,
		Triangle,
		Noise,
	};
	Wave wave = Square;
	// Seconds into the sound where this layer starts.
	double start = 0.0;
	double attack = 0.0;
	double sustain = 0.1;
	double decay = 0.2;
	// Extra level at the start of the sustain, fading through it.
	double punch = 0.0;
	double frequency = 440.0;
	// Octaves per second, and its change per second.
	double slide = 0.0;
	double slideAccel = 0.0;
	// The layer falls silent once a slide takes it below this pitch.
	double minFrequency = 0.0;
	// A fraction of the pitch, and how fast it wobbles.
	double vibratoDepth = 0.0;
	double vibratoRate = 0.0;
	double duty = 0.5;
	double dutySweep = 0.0;
	// A jump in pitch after this many seconds; 0 for none.
	double arpeggioTime = 0.0;
	double arpeggioRatio = 1.0;
	// The pitch starts again every this many seconds; 0 for never.
	double repeat = 0.0;
	// Cutoffs in Hz (0 for none); the low-pass moves by octaves per second.
	double lowPass = 0.0;
	double lowPassSweep = 0.0;
	double resonance = 0.0;
	double highPass = 0.0;
	double gain = 1.0;

	[[nodiscard]] double length() const
	{
		return attack + sustain + decay;
	}
};

void renderVoice(const Voice& voice, SoundRandom& random, QVector<float>* out)
{
	const qint64 first = qint64(std::llround(voice.start * kSynthRate));
	const qint64 count = qint64(std::ceil(voice.length() * kSynthRate));
	if (count <= 0) {
		return;
	}
	if (out->size() < first + count) {
		out->resize(first + count);
	}
	double phase = 0.0;
	std::array<double, 32> noise {};
	for (double& value : noise) {
		value = random.range(-1.0, 1.0);
	}
	double low = 0.0;
	double band = 0.0;
	double highIn = 0.0;
	double highOut = 0.0;
	const double attack = std::max(1e-6, voice.attack);
	const double sustain = std::max(1e-6, voice.sustain);
	const double decay = std::max(1e-6, voice.decay);
	for (qint64 index = 0; index < count; ++index) {
		const double t = double(index) / kSynthRate;
		double envelope = 0.0;
		if (t < voice.attack) {
			envelope = t / attack;
		} else if (t < voice.attack + voice.sustain) {
			envelope = 1.0 + voice.punch * (1.0 - (t - voice.attack) / sustain);
		} else {
			envelope = std::max(0.0, 1.0 - (t - voice.attack - voice.sustain) / decay);
		}
		const double local = voice.repeat > 0.0 ? std::fmod(t, voice.repeat) : t;
		double frequency = voice.frequency * std::exp2(voice.slide * local + 0.5 * voice.slideAccel * local * local);
		if (voice.arpeggioTime > 0.0 && local >= voice.arpeggioTime) {
			frequency *= voice.arpeggioRatio;
		}
		if (voice.vibratoDepth > 0.0) {
			frequency *= 1.0 + voice.vibratoDepth * std::sin(2.0 * kPi * voice.vibratoRate * t);
		}
		if (voice.minFrequency > 0.0 && frequency < voice.minFrequency) {
			// Fades over the last fifth of the way down, so it does not click.
			envelope *= std::clamp((frequency - 0.8 * voice.minFrequency) / (0.2 * voice.minFrequency), 0.0, 1.0);
		}
		frequency = std::clamp(frequency, 1.0, kSynthRate * 0.45);
		phase += frequency / kSynthRate;
		if (phase >= 1.0) {
			phase -= std::floor(phase);
			if (voice.wave == Voice::Noise) {
				for (double& value : noise) {
					value = random.range(-1.0, 1.0);
				}
			}
		}
		double sample = 0.0;
		switch (voice.wave) {
		case Voice::Square: {
			const double duty = std::clamp(voice.duty + voice.dutySweep * t, 0.05, 0.95);
			sample = phase < duty ? 0.5 : -0.5;
			break;
		}
		case Voice::Saw:
			sample = 1.0 - 2.0 * phase;
			break;
		case Voice::Sine:
			sample = std::sin(2.0 * kPi * phase);
			break;
		case Voice::Triangle:
			sample = 4.0 * std::abs(phase - 0.5) - 1.0;
			break;
		case Voice::Noise:
			sample = noise[std::min<size_t>(31, size_t(phase * 32.0))];
			break;
		}
		if (voice.lowPass > 0.0) {
			// A state-variable filter, kept below a sixth of the rate to stay stable.
			const double cutoff = std::clamp(voice.lowPass * std::exp2(voice.lowPassSweep * t), 20.0, kSynthRate / 6.0);
			const double f = 2.0 * std::sin(kPi * cutoff / kSynthRate);
			const double damping = 1.0 - std::clamp(voice.resonance, 0.0, 0.9);
			low += f * band;
			const double high = sample - low - damping * band;
			band += f * high;
			sample = low;
		}
		if (voice.highPass > 0.0) {
			const double rc = 1.0 / (2.0 * kPi * voice.highPass);
			const double alpha = rc / (rc + 1.0 / kSynthRate);
			highOut = alpha * (highOut + sample - highIn);
			highIn = sample;
			sample = highOut;
		}
		(*out)[first + index] += float(sample * envelope * voice.gain);
	}
}

// Stretches every time in the voices, keeping the pitch's path the same.
void stretchVoices(QVector<Voice>* voices, double factor)
{
	for (Voice& voice : *voices) {
		voice.start *= factor;
		voice.attack *= factor;
		voice.sustain *= factor;
		voice.decay *= factor;
		voice.repeat *= factor;
		voice.arpeggioTime *= factor;
		voice.slide /= factor;
		voice.slideAccel /= factor * factor;
		voice.lowPassSweep /= factor;
		voice.dutySweep /= factor;
	}
}

double voicesLength(const QVector<Voice>& voices)
{
	double length = 0.0;
	for (const Voice& voice : voices) {
		length = std::max(length, voice.start + voice.length());
	}
	return length;
}

// The kinds' voices. Ranges are chosen so that every seed gives a usable
// sound of the kind; the mood moves them.
QVector<Voice> shotVoices(SoundRandom& r, const Mood& m)
{
	Voice crack;
	crack.wave = Voice::Noise;
	crack.frequency = r.range(600.0, 1400.0) * m.pitch;
	crack.sustain = r.range(0.02, 0.05);
	crack.decay = r.range(0.2, 0.45) * m.length;
	crack.punch = r.range(0.4, 0.8);
	crack.slide = r.range(-1.5, -0.5);
	crack.lowPass = r.range(6000.0, 7000.0);
	crack.lowPassSweep = r.range(-3.0, -1.5);
	Voice body;
	body.wave = r.chance(0.5) ? Voice::Square : Voice::Saw;
	body.frequency = r.range(120.0, 220.0) * m.pitch;
	body.slide = r.range(-4.0, -2.0);
	body.sustain = r.range(0.01, 0.03);
	body.decay = r.range(0.1, 0.2) * m.length;
	body.punch = 0.5;
	body.lowPass = 2000.0;
	body.gain = 0.5;
	QVector<Voice> voices {crack, body};
	if (m.heavy || r.chance(0.3)) {
		Voice tail;
		tail.wave = Voice::Noise;
		tail.start = 0.03;
		tail.frequency = r.range(150.0, 260.0) * m.pitch;
		tail.sustain = 0.0;
		tail.decay = r.range(0.4, 0.7) * m.length;
		tail.lowPass = 1500.0;
		tail.gain = 0.3;
		voices << tail;
	}
	if (m.metal) {
		Voice ring;
		ring.wave = Voice::Sine;
		ring.frequency = r.range(900.0, 1600.0);
		ring.sustain = 0.0;
		ring.decay = 0.3 * m.length;
		ring.gain = 0.15;
		voices << ring;
	}
	return voices;
}

QVector<Voice> laserVoices(SoundRandom& r, const Mood& m)
{
	Voice zap;
	zap.wave = r.chance(0.6) ? Voice::Square : Voice::Saw;
	zap.frequency = r.range(700.0, 1600.0) * m.pitch;
	zap.slide = r.range(-6.0, -3.0);
	zap.duty = r.range(0.2, 0.5);
	zap.dutySweep = r.range(-0.6, 0.6);
	zap.sustain = r.range(0.04, 0.12) * m.length;
	zap.decay = r.range(0.12, 0.3) * m.length;
	zap.punch = r.range(0.2, 0.5);
	zap.vibratoDepth = r.chance(0.5) ? r.range(0.02, 0.08) : 0.0;
	zap.vibratoRate = r.range(20.0, 40.0);
	zap.minFrequency = 60.0;
	QVector<Voice> voices {zap};
	if (r.chance(0.5)) {
		Voice shine = zap;
		shine.wave = Voice::Sine;
		shine.frequency *= 2.0;
		shine.gain = 0.35;
		voices << shine;
	}
	if (m.electric || m.heavy) {
		Voice sizzle;
		sizzle.wave = Voice::Noise;
		sizzle.frequency = 3000.0;
		sizzle.sustain = zap.sustain;
		sizzle.decay = 0.2 * m.length;
		sizzle.highPass = 2000.0;
		sizzle.gain = 0.2;
		voices << sizzle;
	}
	return voices;
}

QVector<Voice> explosionVoices(SoundRandom& r, const Mood& m)
{
	Voice rumble;
	rumble.wave = Voice::Noise;
	rumble.frequency = r.range(60.0, 160.0) * m.pitch;
	rumble.slide = r.range(-0.6, -0.2);
	rumble.sustain = r.range(0.08, 0.2) * m.length;
	rumble.decay = r.range(0.9, 1.6) * m.length;
	rumble.punch = r.range(0.6, 1.0);
	rumble.lowPass = m.distant ? r.range(700.0, 1100.0) : r.range(1800.0, 3500.0);
	rumble.lowPassSweep = r.range(-1.6, -0.8);
	Voice thump;
	thump.wave = Voice::Sine;
	thump.frequency = r.range(55.0, 90.0) * m.pitch;
	thump.slide = r.range(-2.0, -1.0);
	thump.sustain = 0.05;
	thump.decay = 0.4 * m.length;
	thump.punch = 0.8;
	thump.gain = 0.8;
	QVector<Voice> voices {rumble, thump};
	if (!m.distant && r.chance(0.6)) {
		Voice crackle;
		crackle.wave = Voice::Noise;
		crackle.start = r.range(0.05, 0.15);
		crackle.frequency = r.range(2000.0, 4000.0);
		crackle.sustain = 0.0;
		crackle.decay = r.range(0.4, 0.8) * m.length;
		crackle.highPass = 1500.0;
		crackle.gain = 0.2;
		voices << crackle;
	}
	return voices;
}

QVector<Voice> pickupVoices(SoundRandom& r, const Mood& m)
{
	Voice chime;
	chime.wave = m.wet || r.chance(0.3) ? Voice::Sine : Voice::Square;
	chime.frequency = r.range(600.0, 1100.0) * m.pitch;
	chime.duty = r.range(0.3, 0.5);
	chime.sustain = r.range(0.04, 0.09) * m.length;
	chime.decay = r.range(0.12, 0.3) * m.length;
	chime.punch = r.range(0.3, 0.6);
	chime.arpeggioTime = r.range(0.04, 0.09) * m.length;
	static const std::array<double, 4> steps {{1.25, 1.335, 1.5, 2.0}};
	chime.arpeggioRatio = steps[size_t(r.next() % steps.size())];
	QVector<Voice> voices {chime};
	if (r.chance(0.4)) {
		Voice click;
		click.wave = Voice::Noise;
		click.frequency = 4000.0;
		click.sustain = 0.005;
		click.decay = 0.04;
		click.gain = 0.4;
		voices << click;
	}
	return voices;
}

QVector<Voice> powerupVoices(SoundRandom& r, const Mood& m)
{
	Voice rise;
	const double pick = r.unit();
	rise.wave = pick < 0.4 ? Voice::Square : pick < 0.7 ? Voice::Saw : Voice::Sine;
	rise.frequency = r.range(250.0, 500.0) * m.pitch;
	rise.slide = r.range(1.0, 2.5);
	rise.sustain = r.range(0.25, 0.45) * m.length;
	rise.decay = r.range(0.3, 0.6) * m.length;
	rise.vibratoDepth = r.range(0.05, 0.12);
	rise.vibratoRate = r.range(6.0, 12.0);
	rise.repeat = r.chance(0.6) ? r.range(0.12, 0.25) * m.length : 0.0;
	rise.duty = 0.4;
	rise.lowPass = rise.wave == Voice::Saw ? 4000.0 : 0.0;
	Voice shine = rise;
	shine.wave = Voice::Sine;
	shine.frequency *= 3.0;
	shine.gain = 0.3;
	shine.lowPass = 0.0;
	return {rise, shine};
}

QVector<Voice> jumpVoices(SoundRandom& r, const Mood& m)
{
	Voice spring;
	spring.wave = Voice::Square;
	spring.duty = r.range(0.4, 0.5);
	spring.frequency = r.range(220.0, 420.0) * m.pitch;
	spring.slide = r.range(1.2, 2.5);
	spring.sustain = r.range(0.08, 0.15) * m.length;
	spring.decay = r.range(0.1, 0.2) * m.length;
	spring.lowPass = r.range(3000.0, 6000.0);
	QVector<Voice> voices {spring};
	if (m.metal || m.heavy) {
		Voice whoosh;
		whoosh.wave = Voice::Noise;
		whoosh.frequency = 1500.0;
		whoosh.attack = 0.02;
		whoosh.sustain = 0.1 * m.length;
		whoosh.decay = 0.2 * m.length;
		whoosh.lowPass = 2500.0;
		whoosh.highPass = 400.0;
		whoosh.gain = 0.3;
		voices << whoosh;
	}
	return voices;
}

QVector<Voice> hurtVoices(SoundRandom& r, const Mood& m)
{
	Voice cry;
	cry.wave = m.alien || r.chance(0.6) ? Voice::Saw : Voice::Square;
	cry.frequency = r.range(260.0, 520.0) * m.pitch * (m.alien ? 0.6 : 1.0);
	cry.slide = r.range(-2.0, -1.0);
	cry.sustain = r.range(0.03, 0.08) * m.length;
	cry.decay = r.range(0.15, 0.3) * m.length;
	cry.punch = r.range(0.4, 0.7);
	cry.vibratoDepth = m.alien ? 0.15 : 0.05;
	cry.vibratoRate = m.alien ? 18.0 : 30.0;
	cry.lowPass = 3000.0;
	Voice hit;
	hit.wave = Voice::Noise;
	hit.frequency = r.range(800.0, 1500.0);
	hit.sustain = 0.02;
	hit.decay = 0.12 * m.length;
	hit.lowPass = 2500.0;
	hit.gain = 0.4;
	return {cry, hit};
}

QVector<Voice> deathVoices(SoundRandom& r, const Mood& m)
{
	Voice fall;
	fall.wave = Voice::Saw;
	fall.frequency = r.range(300.0, 500.0) * m.pitch * (m.alien ? 0.6 : 1.0);
	fall.slide = r.range(-1.4, -0.8);
	fall.sustain = r.range(0.2, 0.35) * m.length;
	fall.decay = r.range(0.6, 1.0) * m.length;
	fall.punch = 0.3;
	fall.vibratoDepth = m.alien ? 0.2 : r.range(0.06, 0.12);
	fall.vibratoRate = m.alien ? 14.0 : r.range(6.0, 10.0);
	fall.lowPass = 2500.0;
	Voice gore;
	gore.wave = Voice::Noise;
	gore.frequency = r.range(400.0, 900.0);
	gore.sustain = 0.1 * m.length;
	gore.decay = (m.wet ? 0.3 : 0.5) * m.length;
	gore.lowPass = 1800.0;
	gore.gain = m.wet ? 0.7 : 0.4;
	return {fall, gore};
}

QVector<Voice> doorVoices(SoundRandom& r, const Mood& m)
{
	Voice rumble;
	rumble.wave = Voice::Saw;
	rumble.frequency = r.range(45.0, 80.0) * m.pitch;
	rumble.attack = r.range(0.05, 0.12);
	rumble.sustain = r.range(0.6, 1.0) * m.length;
	rumble.decay = r.range(0.1, 0.2);
	rumble.vibratoDepth = r.range(0.02, 0.05);
	rumble.vibratoRate = r.range(5.0, 9.0);
	rumble.lowPass = r.range(400.0, 900.0);
	rumble.gain = 0.8;
	QVector<Voice> voices {rumble};
	if (m.metal || r.chance(0.6)) {
		Voice servo;
		servo.wave = Voice::Square;
		servo.frequency = r.range(110.0, 180.0) * m.pitch;
		servo.attack = 0.1;
		servo.sustain = rumble.sustain;
		servo.decay = 0.2;
		servo.duty = 0.3;
		servo.vibratoDepth = 0.03;
		servo.vibratoRate = 3.0;
		servo.lowPass = 1200.0;
		servo.gain = 0.25;
		voices << servo;
	}
	// It stops with a thud.
	const double stop = rumble.attack + rumble.sustain - 0.02;
	Voice thud;
	thud.wave = Voice::Noise;
	thud.start = stop;
	thud.frequency = r.range(120.0, 200.0) * m.pitch;
	thud.sustain = 0.02;
	thud.decay = 0.35 * m.length;
	thud.punch = 0.8;
	thud.lowPass = 900.0;
	thud.gain = 0.9;
	Voice body;
	body.wave = Voice::Sine;
	body.start = stop;
	body.frequency = r.range(60.0, 80.0) * m.pitch;
	body.sustain = 0.02;
	body.decay = 0.25 * m.length;
	body.gain = 0.7;
	voices << thud << body;
	if (m.metal) {
		Voice clank;
		clank.wave = Voice::Square;
		clank.start = stop;
		clank.frequency = r.range(300.0, 500.0);
		clank.vibratoDepth = 0.02;
		clank.vibratoRate = 7.0;
		clank.sustain = 0.0;
		clank.decay = 0.25 * m.length;
		clank.lowPass = 2500.0;
		clank.gain = 0.25;
		voices << clank;
	}
	return voices;
}

QVector<Voice> switchVoices(SoundRandom& r, const Mood& m)
{
	if (m.electric) {
		Voice beep;
		beep.wave = Voice::Sine;
		beep.frequency = r.range(1200.0, 2000.0) * m.pitch;
		beep.sustain = 0.08;
		beep.decay = 0.05;
		return {beep};
	}
	Voice click;
	click.wave = Voice::Square;
	click.frequency = r.range(900.0, 2000.0) * m.pitch;
	click.sustain = r.range(0.006, 0.015);
	click.decay = r.range(0.03, 0.07) * m.length;
	click.punch = 0.8;
	Voice snap;
	snap.wave = Voice::Noise;
	snap.frequency = 6000.0;
	snap.sustain = 0.002;
	snap.decay = 0.02;
	snap.highPass = 2000.0;
	snap.gain = 0.6;
	QVector<Voice> voices {click, snap};
	if (r.chance(0.5)) {
		Voice release = click;
		release.start = r.range(0.06, 0.1) * m.length;
		release.frequency *= 0.8;
		release.gain = 0.7;
		voices << release;
	}
	return voices;
}

QVector<Voice> teleportVoices(SoundRandom& r, const Mood& m)
{
	Voice swirl;
	swirl.wave = Voice::Sine;
	swirl.frequency = r.range(300.0, 500.0) * m.pitch;
	swirl.slide = r.range(3.0, 5.0);
	swirl.slideAccel = -r.range(6.0, 10.0);
	swirl.sustain = r.range(0.4, 0.7) * m.length;
	swirl.decay = r.range(0.4, 0.6) * m.length;
	swirl.vibratoDepth = r.range(0.1, 0.25);
	swirl.vibratoRate = r.range(8.0, 16.0);
	Voice edge = swirl;
	edge.wave = Voice::Square;
	edge.frequency *= 1.5;
	edge.duty = 0.25;
	edge.lowPass = 3000.0;
	edge.gain = 0.3;
	Voice shimmer;
	shimmer.wave = Voice::Noise;
	shimmer.frequency = 5000.0;
	shimmer.attack = 0.1;
	shimmer.sustain = 0.4 * m.length;
	shimmer.decay = 0.4 * m.length;
	shimmer.highPass = 3000.0;
	shimmer.gain = 0.25;
	return {swirl, edge, shimmer};
}

QVector<Voice> footstepVoices(SoundRandom& r, const Mood& m)
{
	Voice scuff;
	scuff.wave = Voice::Noise;
	scuff.frequency = (m.metal || m.stone ? r.range(900.0, 1400.0) : r.range(500.0, 900.0)) * m.pitch;
	scuff.sustain = r.range(0.005, 0.015);
	scuff.decay = r.range(0.06, 0.14) * m.length;
	scuff.punch = 0.5;
	scuff.lowPass = m.metal || m.stone ? r.range(2500.0, 3500.0) : r.range(1200.0, 2000.0);
	scuff.highPass = 80.0;
	Voice thump;
	thump.wave = Voice::Sine;
	thump.frequency = r.range(80.0, 120.0) * m.pitch;
	thump.sustain = 0.005;
	thump.decay = 0.06 * m.length;
	thump.gain = 0.5;
	QVector<Voice> voices {scuff, thump};
	if (m.metal) {
		Voice ring;
		ring.wave = Voice::Square;
		ring.frequency = r.range(700.0, 1100.0);
		ring.sustain = 0.0;
		ring.decay = 0.12 * m.length;
		ring.vibratoDepth = 0.01;
		ring.vibratoRate = 6.0;
		ring.lowPass = 3000.0;
		ring.gain = 0.12;
		voices << ring;
	}
	return voices;
}

QVector<Voice> impactVoices(SoundRandom& r, const Mood& m)
{
	Voice crash;
	crash.wave = Voice::Noise;
	crash.frequency = r.range(300.0, 900.0) * m.pitch;
	crash.sustain = r.range(0.01, 0.03);
	crash.decay = r.range(0.2, 0.45) * m.length;
	crash.punch = r.range(0.6, 1.0);
	crash.lowPass = m.stone || m.distant ? r.range(1200.0, 2000.0) : r.range(2000.0, 5000.0);
	crash.lowPassSweep = -2.0;
	Voice thump;
	thump.wave = Voice::Sine;
	thump.frequency = r.range(60.0, 110.0) * m.pitch;
	thump.slide = -1.5;
	thump.sustain = 0.02;
	thump.decay = 0.25 * m.length;
	thump.gain = 0.8;
	QVector<Voice> voices {crash, thump};
	if (m.metal) {
		Voice ring;
		ring.wave = Voice::Square;
		ring.frequency = r.range(400.0, 900.0);
		ring.vibratoDepth = r.range(0.01, 0.02);
		ring.vibratoRate = 5.0;
		ring.sustain = 0.0;
		ring.decay = r.range(0.5, 0.9) * m.length;
		ring.lowPass = 3500.0;
		ring.gain = 0.25;
		// An inharmonic partial makes it ring like metal, not like a note.
		Voice partial = ring;
		partial.wave = Voice::Sine;
		partial.frequency *= 1.414;
		partial.gain = 0.15;
		voices << ring << partial;
	} else if (m.wood) {
		Voice knock;
		knock.wave = Voice::Triangle;
		knock.frequency = r.range(180.0, 260.0);
		knock.sustain = 0.0;
		knock.decay = 0.15 * m.length;
		knock.gain = 0.5;
		voices << knock;
	}
	return voices;
}

// Sustained kinds repeat over `cycle`, the finished loop's length, and are
// rendered over `duration`, which adds the crossfade that joins the loop.
QVector<Voice> alarmVoices(SoundRandom& r, const Mood& m, const QStringList& words, double cycle, double duration)
{
	if (hasStem(words, {"beep", "buzzer", "bell", "warning"}) || r.chance(0.3)) {
		// Beeps on a beat that divides the loop, so the loop keeps time.
		const double period = cycle / std::max(1.0, std::round(cycle / r.range(0.35, 0.6)));
		QVector<Voice> voices;
		const double frequency = r.range(800.0, 1400.0) * m.pitch;
		for (double start = 0.0; start + 0.05 < duration; start += period) {
			Voice beep;
			beep.wave = Voice::Square;
			beep.start = start;
			beep.frequency = frequency;
			beep.sustain = std::min(period * 0.45, 0.25);
			beep.decay = 0.02;
			beep.lowPass = 4000.0;
			voices << beep;
		}
		return voices;
	}
	Voice siren;
	siren.wave = r.chance(0.5) ? Voice::Saw : Voice::Square;
	siren.frequency = r.range(500.0, 800.0) * m.pitch;
	// Whole wobbles over the loop, so it meets itself.
	siren.vibratoRate = std::max(1.0, std::round(cycle * r.range(0.5, 1.2))) / cycle;
	siren.vibratoDepth = r.range(0.25, 0.4);
	siren.sustain = duration;
	siren.decay = 0.0;
	siren.lowPass = 3500.0;
	return {siren};
}

QVector<Voice> ambienceVoices(SoundRandom& r, const Mood& m, const QStringList& words, double cycle, double duration)
{
	// Slow wobbles make whole turns over the loop.
	const auto slow = [cycle](double perSecond) {
		return std::max(1.0, std::round(cycle * perSecond)) / cycle;
	};
	QVector<Voice> voices;
	const auto bed = [duration](Voice voice) {
		voice.attack = 0.0;
		voice.sustain = duration;
		voice.decay = 0.0;
		return voice;
	};
	if (hasStem(words, {"wind", "breeze", "gust", "storm"})) {
		Voice wind;
		wind.wave = Voice::Noise;
		wind.frequency = r.range(400.0, 900.0);
		wind.vibratoDepth = 0.3;
		wind.vibratoRate = slow(0.25);
		wind.lowPass = r.range(500.0, 900.0);
		wind.resonance = 0.6;
		voices << bed(wind);
	} else if (m.wet || hasStem(words, {"rain", "river", "stream"})) {
		Voice water;
		water.wave = Voice::Noise;
		water.frequency = r.range(3000.0, 6000.0);
		water.lowPass = 3000.0;
		water.highPass = 300.0;
		water.gain = 0.5;
		voices << bed(water);
		// Bubbles and drips at random times.
		const int bubbles = int(duration * r.range(2.0, 5.0));
		for (int index = 0; index < bubbles; ++index) {
			Voice bubble;
			bubble.wave = Voice::Sine;
			bubble.start = r.range(0.0, std::max(0.0, duration - 0.15));
			bubble.frequency = r.range(300.0, 900.0);
			bubble.slide = r.range(2.0, 4.0);
			bubble.sustain = 0.02;
			bubble.decay = r.range(0.04, 0.1);
			bubble.gain = r.range(0.2, 0.5);
			voices << bubble;
		}
	} else if (hasStem(words, {"fire", "torch", "flame", "lava", "burn"})) {
		Voice roar;
		roar.wave = Voice::Noise;
		roar.frequency = r.range(1200.0, 2500.0);
		roar.lowPass = hasStem(words, {"lava"}) ? 600.0 : 1800.0;
		roar.gain = 0.6;
		voices << bed(roar);
		const int crackles = int(duration * r.range(4.0, 9.0));
		for (int index = 0; index < crackles; ++index) {
			Voice crackle;
			crackle.wave = Voice::Noise;
			crackle.start = r.range(0.0, std::max(0.0, duration - 0.05));
			crackle.frequency = 5000.0;
			crackle.sustain = 0.002;
			crackle.decay = 0.02;
			crackle.gain = r.range(0.2, 0.6);
			voices << crackle;
		}
	} else if (m.electric || hasStem(words, {"machine", "engine", "generator", "reactor", "computer", "hum", "factory"})) {
		const double mains = r.chance(0.5) ? 50.0 : 60.0;
		Voice hum;
		hum.wave = Voice::Saw;
		hum.frequency = mains * (m.heavy ? 1.0 : 2.0);
		hum.lowPass = 600.0;
		hum.gain = 0.7;
		Voice whine;
		whine.wave = Voice::Square;
		whine.frequency = hum.frequency * 2.0;
		whine.duty = 0.3;
		whine.vibratoDepth = 0.01;
		whine.vibratoRate = slow(0.3);
		whine.lowPass = 900.0;
		whine.gain = 0.25;
		Voice air;
		air.wave = Voice::Noise;
		air.frequency = 2500.0;
		air.lowPass = 1200.0;
		air.gain = 0.15;
		voices << bed(hum) << bed(whine) << bed(air);
		if (hasStem(words, {"computer", "terminal"})) {
			const int beeps = int(duration * r.range(0.5, 1.5));
			for (int index = 0; index < beeps; ++index) {
				Voice beep;
				beep.wave = Voice::Sine;
				beep.start = r.range(0.0, std::max(0.0, duration - 0.2));
				beep.frequency = r.range(900.0, 2400.0);
				beep.sustain = r.range(0.04, 0.12);
				beep.decay = 0.02;
				beep.gain = 0.25;
				voices << beep;
			}
		}
	} else {
		// A low drone: a cave, a hall, a hell.
		Voice drone;
		drone.wave = Voice::Sine;
		drone.frequency = r.range(55.0, 110.0) * m.pitch;
		drone.vibratoDepth = 0.01;
		drone.vibratoRate = slow(0.2);
		Voice fifth = drone;
		fifth.wave = Voice::Saw;
		fifth.frequency *= 3.0;
		fifth.lowPass = 800.0;
		fifth.gain = 0.2;
		Voice air;
		air.wave = Voice::Noise;
		air.frequency = 800.0;
		air.lowPass = 500.0;
		air.gain = 0.2;
		voices << bed(drone) << bed(fifth) << bed(air);
	}
	return voices;
}

QString cleanedName(const QString& text, bool upper, int maxLength)
{
	QString name;
	for (const QChar ch : text) {
		if (ch.isLetterOrNumber() && ch.unicode() < 128) {
			name += upper ? ch.toUpper() : ch.toLower();
		} else if ((ch == QLatin1Char('_') || ch == QLatin1Char(' ') || ch == QLatin1Char('-')) && !name.endsWith(QLatin1Char('_'))) {
			name += QLatin1Char('_');
		}
	}
	while (name.startsWith(QLatin1Char('_'))) {
		name.remove(0, 1);
	}
	name = name.left(maxLength);
	while (name.endsWith(QLatin1Char('_'))) {
		name.chop(1);
	}
	return name;
}

// The words that name the sound: no articles, and no adjectives when there
// are nouns enough without them.
QStringList namingWords(const QString& prompt)
{
	static const QStringList stopWords = {
		QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("the"), QStringLiteral("of"), QStringLiteral("with"), QStringLiteral("and"),
		QStringLiteral("for"), QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("at"), QStringLiteral("to"), QStringLiteral("from"),
		QStringLiteral("by"), QStringLiteral("very"), QStringLiteral("some"), QStringLiteral("sound"), QStringLiteral("sounds"), QStringLiteral("effect"),
		QStringLiteral("effects"), QStringLiteral("sfx"), QStringLiteral("like"), QStringLiteral("that"), QStringLiteral("is"),
	};
	static const QStringList adjectives = {
		QStringLiteral("heavy"), QStringLiteral("big"), QStringLiteral("huge"), QStringLiteral("small"), QStringLiteral("tiny"), QStringLiteral("light"),
		QStringLiteral("loud"), QStringLiteral("soft"), QStringLiteral("deep"), QStringLiteral("distant"), QStringLiteral("short"), QStringLiteral("long"),
		QStringLiteral("quick"), QStringLiteral("slow"), QStringLiteral("old"), QStringLiteral("rusty"), QStringLiteral("retro"),
		QStringLiteral("metal"), QStringLiteral("metallic"), QStringLiteral("steel"), QStringLiteral("iron"), QStringLiteral("wooden"),
		QStringLiteral("stone"), QStringLiteral("electric"), QStringLiteral("alien"), QStringLiteral("demonic"),
	};
	QStringList words;
	for (const QString& word : promptWords(prompt)) {
		if (!stopWords.contains(word)) {
			words << word;
		}
	}
	QStringList nouns;
	for (const QString& word : words) {
		if (!adjectives.contains(word)) {
			nouns << word;
		}
	}
	return nouns.size() >= 2 ? nouns : words;
}

bool writeBytes(const QString& path, const QByteArray& bytes, QString* error)
{
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioSoundGeneration", "Could not create the folder for %1.").arg(QDir::toNativeSeparators(path));
		}
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		if (error) {
			*error = QCoreApplication::translate("VibeStudioSoundGeneration", "Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
		}
		return false;
	}
	return true;
}

QString deliveryFormatText(const AudioDeliveryPlan& plan)
{
	if (plan.dmx) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "%1 Hz Doom DMX lump").arg(plan.sampleRate);
	}
	return QCoreApplication::translate("VibeStudioSoundGeneration", "%1 Hz %2-bit WAV").arg(plan.sampleRate).arg(audioWavBits(plan.wav.format));
}

} // namespace

QVector<SoundGameProfile> soundGameProfiles()
{
	return {
		{QStringLiteral("doom"), QCoreApplication::translate("VibeStudioSoundGeneration", "Doom"), AudioDeliveryPreset::Doom, true},
		{QStringLiteral("quake"), QCoreApplication::translate("VibeStudioSoundGeneration", "Quake"), AudioDeliveryPreset::Quake, false},
		{QStringLiteral("quake2"), QCoreApplication::translate("VibeStudioSoundGeneration", "Quake II"), AudioDeliveryPreset::Quake2, false},
		{QStringLiteral("quake3"), QCoreApplication::translate("VibeStudioSoundGeneration", "Quake III Arena"), AudioDeliveryPreset::Quake3, false},
		{QStringLiteral("generic"), QCoreApplication::translate("VibeStudioSoundGeneration", "Any game (44.1 kHz WAV)"), AudioDeliveryPreset::Wav, false},
	};
}

QStringList soundGameProfileIds()
{
	QStringList ids;
	for (const SoundGameProfile& profile : soundGameProfiles()) {
		ids << profile.id;
	}
	return ids;
}

bool soundGameProfileForId(const QString& id, SoundGameProfile* profile)
{
	QString key = id.trimmed().toLower();
	if (key == QStringLiteral("doom2") || key == QStringLiteral("doom-wad") || key == QStringLiteral("freedoom")) {
		key = QStringLiteral("doom");
	} else if (key == QStringLiteral("q1") || key == QStringLiteral("quake1")) {
		key = QStringLiteral("quake");
	} else if (key == QStringLiteral("q2")) {
		key = QStringLiteral("quake2");
	} else if (key == QStringLiteral("q3") || key == QStringLiteral("q3a") || key == QStringLiteral("quake3arena")) {
		key = QStringLiteral("quake3");
	} else if (key == QStringLiteral("wav") || key.isEmpty()) {
		key = QStringLiteral("generic");
	}
	for (const SoundGameProfile& candidate : soundGameProfiles()) {
		if (candidate.id == key) {
			if (profile) {
				*profile = candidate;
			}
			return true;
		}
	}
	return false;
}

QStringList soundGenerationKindIds()
{
	return {
		QStringLiteral("shot"),
		QStringLiteral("laser"),
		QStringLiteral("explosion"),
		QStringLiteral("pickup"),
		QStringLiteral("powerup"),
		QStringLiteral("jump"),
		QStringLiteral("hurt"),
		QStringLiteral("death"),
		QStringLiteral("door"),
		QStringLiteral("switch"),
		QStringLiteral("teleport"),
		QStringLiteral("footstep"),
		QStringLiteral("impact"),
		QStringLiteral("alarm"),
		QStringLiteral("ambience"),
	};
}

QString soundGenerationKindDisplayName(const QString& kind)
{
	if (kind == QStringLiteral("shot")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Gunshot");
	}
	if (kind == QStringLiteral("laser")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Energy weapon");
	}
	if (kind == QStringLiteral("explosion")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Explosion");
	}
	if (kind == QStringLiteral("pickup")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Pickup");
	}
	if (kind == QStringLiteral("powerup")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Power-up");
	}
	if (kind == QStringLiteral("jump")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Jump");
	}
	if (kind == QStringLiteral("hurt")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Pain");
	}
	if (kind == QStringLiteral("death")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Death");
	}
	if (kind == QStringLiteral("door")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Door or lift");
	}
	if (kind == QStringLiteral("switch")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Switch");
	}
	if (kind == QStringLiteral("teleport")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Teleport");
	}
	if (kind == QStringLiteral("footstep")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Footstep");
	}
	if (kind == QStringLiteral("impact")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Impact");
	}
	if (kind == QStringLiteral("alarm")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Alarm");
	}
	if (kind == QStringLiteral("ambience")) {
		return QCoreApplication::translate("VibeStudioSoundGeneration", "Ambience (loop)");
	}
	return kind;
}

QString soundKindFromPrompt(const QString& prompt)
{
	return kindForWords(promptWords(prompt));
}

double soundGenerationDefaultDuration(const QString& kind)
{
	if (kind == QStringLiteral("ambience")) {
		return 4.0;
	}
	if (kind == QStringLiteral("alarm")) {
		return 2.0;
	}
	if (kind == QStringLiteral("explosion") || kind == QStringLiteral("door") || kind == QStringLiteral("teleport") || kind == QStringLiteral("death")) {
		return 1.4;
	}
	if (kind == QStringLiteral("powerup")) {
		return 0.9;
	}
	if (kind == QStringLiteral("switch") || kind == QStringLiteral("footstep")) {
		return 0.2;
	}
	return 0.5;
}

bool soundGenerationKindLoops(const QString& kind)
{
	return kind == QStringLiteral("ambience") || kind == QStringLiteral("alarm");
}

SoundGenerationSpec normalizedSoundGenerationSpec(const SoundGenerationSpec& spec)
{
	SoundGenerationSpec normalized = spec;
	normalized.prompt = spec.prompt.trimmed();
	SoundGameProfile profile;
	normalized.game = soundGameProfileForId(spec.game, &profile) ? profile.id : QStringLiteral("quake");
	normalized.kind = spec.kind.trimmed().toLower();
	if (!soundGenerationKindIds().contains(normalized.kind)) {
		normalized.kind = soundKindFromPrompt(normalized.prompt);
	}
	normalized.name = spec.name.trimmed();
	QString folder;
	for (const QString& part : spec.folder.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
		const QString cleaned = cleanedName(part, false, 24);
		if (!cleaned.isEmpty()) {
			folder += (folder.isEmpty() ? QString() : QStringLiteral("/")) + cleaned;
		}
	}
	normalized.folder = folder.isEmpty() ? QStringLiteral("vibestudio") : folder;
	normalized.durationSeconds = spec.durationSeconds > 0.0 ? std::clamp(spec.durationSeconds, 0.1, 30.0) : 0.0;
	normalized.promptInfluence = spec.promptInfluence < 0.0 ? -1.0 : std::clamp(spec.promptInfluence, 0.0, 1.0);
	normalized.variants = std::clamp(spec.variants, 1, kMaxVariants);
	if (normalized.seed < 0) {
		// The same description always makes the same sound.
		normalized.seed = qint64(textHash(normalized.prompt + QLatin1Char('|') + normalized.kind) & 0x7fffffffull);
	}
	return normalized;
}

SoundGenerationSpec soundGenerationVariantSpec(const SoundGenerationSpec& spec, int index, int count)
{
	SoundGenerationSpec variant = normalizedSoundGenerationSpec(spec);
	variant.variants = 1;
	if (count <= 1) {
		return variant;
	}
	variant.seed = (variant.seed + qint64(index) * 7919) & 0x7fffffff;
	SoundGameProfile profile;
	soundGameProfileForId(variant.game, &profile);
	const QString base = soundGenerationName(normalizedSoundGenerationSpec(spec));
	variant.name = profile.lumps ? base.left(7) + QString::number(index + 1) : QStringLiteral("%1_%2").arg(base).arg(index + 1);
	return variant;
}

QString soundGenerationName(const SoundGenerationSpec& requested)
{
	SoundGameProfile profile;
	soundGameProfileForId(requested.game, &profile);
	if (profile.lumps) {
		QString name = cleanedName(requested.name, true, 8);
		name.remove(QLatin1Char('_'));
		if (!name.isEmpty()) {
			return name.startsWith(QStringLiteral("DS")) ? name.left(8) : (QStringLiteral("DS") + name).left(8);
		}
		// The first word's first four letters, then the next words' first ones.
		QString letters;
		const QStringList words = namingWords(requested.prompt);
		for (int index = 0; index < words.size() && letters.size() < 6; ++index) {
			const QString word = cleanedName(words.at(index), true, 8).remove(QLatin1Char('_'));
			letters += word.left(index == 0 ? 4 : 6 - letters.size());
		}
		if (letters.isEmpty()) {
			letters = cleanedName(soundKindFromPrompt(requested.prompt), true, 6);
		}
		return QStringLiteral("DS") + letters.left(6);
	}
	QString name = cleanedName(requested.name, false, 32);
	if (name.isEmpty()) {
		name = cleanedName(namingWords(requested.prompt).mid(0, 3).join(QLatin1Char('_')), false, 24);
	}
	return name.isEmpty() ? cleanedName(requested.kind.isEmpty() ? soundKindFromPrompt(requested.prompt) : requested.kind, false, 24) : name;
}

QString soundGenerationVirtualPath(const SoundGenerationSpec& spec)
{
	const SoundGenerationSpec normalized = normalizedSoundGenerationSpec(spec);
	SoundGameProfile profile;
	soundGameProfileForId(normalized.game, &profile);
	const QString name = soundGenerationName(normalized);
	if (profile.lumps) {
		return name;
	}
	if (profile.id == QStringLiteral("generic")) {
		return name + QStringLiteral(".wav");
	}
	return QStringLiteral("sound/%1/%2.wav").arg(normalized.folder, name);
}

QString soundGenerationReference(const SoundGenerationSpec& spec)
{
	const SoundGenerationSpec normalized = normalizedSoundGenerationSpec(spec);
	const QString path = soundGenerationVirtualPath(normalized);
	// Quake and Quake II name sounds from inside sound/; Quake III with it.
	if (normalized.game == QStringLiteral("quake") || normalized.game == QStringLiteral("quake2")) {
		return path.mid(6);
	}
	return path;
}

QString soundGenerationPrompt(const SoundGenerationSpec& spec)
{
	// In English: the model reads it.
	const SoundGenerationSpec normalized = normalizedSoundGenerationSpec(spec);
	QString look = QStringLiteral("A video game sound effect");
	if (normalized.game == QStringLiteral("doom")) {
		look = QStringLiteral("A sound effect in the style of 1993's Doom: crunchy, punchy, low-fidelity digitized audio");
	} else if (normalized.game == QStringLiteral("quake")) {
		look = QStringLiteral("A sound effect in the style of 1996's Quake: dark, gritty, industrial");
	} else if (normalized.game == QStringLiteral("quake2")) {
		look = QStringLiteral("A sound effect in the style of 1997's Quake II: industrial, military sci-fi");
	} else if (normalized.game == QStringLiteral("quake3")) {
		look = QStringLiteral("A sound effect in the style of 1999's Quake III Arena: punchy, clean arena sci-fi");
	}
	static const QHash<QString, QString> kinds = {
		{QStringLiteral("shot"), QStringLiteral("a single gunshot with a sharp attack and a short tail")},
		{QStringLiteral("laser"), QStringLiteral("an energy weapon firing, a bright zap falling in pitch")},
		{QStringLiteral("explosion"), QStringLiteral("an explosion with a hard punch and a rumbling tail")},
		{QStringLiteral("pickup"), QStringLiteral("an item pickup, a short bright chime")},
		{QStringLiteral("powerup"), QStringLiteral("a power-up, a rising shimmering tone")},
		{QStringLiteral("jump"), QStringLiteral("a jump, a short springy whoosh")},
		{QStringLiteral("hurt"), QStringLiteral("a short, harsh pain grunt or hit")},
		{QStringLiteral("death"), QStringLiteral("a death, a falling cry or a wet gib")},
		{QStringLiteral("door"), QStringLiteral("a door or lift moving, then stopping with a thud")},
		{QStringLiteral("switch"), QStringLiteral("a switch or button, a crisp mechanical click")},
		{QStringLiteral("teleport"), QStringLiteral("a teleport, a swirling rising whoosh")},
		{QStringLiteral("footstep"), QStringLiteral("a single footstep")},
		{QStringLiteral("impact"), QStringLiteral("an impact on a hard surface")},
		{QStringLiteral("alarm"), QStringLiteral("an alarm or siren")},
		{QStringLiteral("ambience"), QStringLiteral("a steady ambient background")},
	};
	const QString description = normalized.prompt.isEmpty() ? kinds.value(normalized.kind) : normalized.prompt;
	// An effect is wanted on its own; an ambience is the background itself.
	const QString clean = normalized.kind == QStringLiteral("ambience") ? QStringLiteral("No music, no speech; it starts at once")
																		 : QStringLiteral("Close and dry, no music, no speech, no background noise; it starts at once");
	QString text = QStringLiteral("%1. %2: %3. %4").arg(description, look, kinds.value(normalized.kind), clean);
	text += normalized.loop ? QStringLiteral(" and loops seamlessly.") : QStringLiteral(" and ends cleanly.");
	return text;
}

AudioClip synthesizeSound(const SoundGenerationSpec& requested)
{
	const SoundGenerationSpec spec = normalizedSoundGenerationSpec(requested);
	SoundRandom random(quint64(spec.seed) * 0x9E3779B97F4A7C15ull + 0x5EEDull);
	const QStringList words = promptWords(spec.prompt);
	const Mood mood = moodFrom(words, random);
	// Ambience and alarms are built to the length; the rest have their own.
	const bool sustained = spec.kind == QStringLiteral("ambience") || spec.kind == QStringLiteral("alarm");
	const double wanted = spec.durationSeconds > 0.0 ? spec.durationSeconds : soundGenerationDefaultDuration(spec.kind);
	// A loop is made longer by the crossfade that joins its ends.
	const double length = spec.loop ? wanted + std::min(0.5, wanted / 4.0) : wanted;
	QVector<Voice> voices;
	if (spec.kind == QStringLiteral("shot")) {
		voices = shotVoices(random, mood);
	} else if (spec.kind == QStringLiteral("laser")) {
		voices = laserVoices(random, mood);
	} else if (spec.kind == QStringLiteral("explosion")) {
		voices = explosionVoices(random, mood);
	} else if (spec.kind == QStringLiteral("pickup")) {
		voices = pickupVoices(random, mood);
	} else if (spec.kind == QStringLiteral("powerup")) {
		voices = powerupVoices(random, mood);
	} else if (spec.kind == QStringLiteral("jump")) {
		voices = jumpVoices(random, mood);
	} else if (spec.kind == QStringLiteral("hurt")) {
		voices = hurtVoices(random, mood);
	} else if (spec.kind == QStringLiteral("death")) {
		voices = deathVoices(random, mood);
	} else if (spec.kind == QStringLiteral("door")) {
		voices = doorVoices(random, mood);
	} else if (spec.kind == QStringLiteral("switch")) {
		voices = switchVoices(random, mood);
	} else if (spec.kind == QStringLiteral("teleport")) {
		voices = teleportVoices(random, mood);
	} else if (spec.kind == QStringLiteral("footstep")) {
		voices = footstepVoices(random, mood);
	} else if (spec.kind == QStringLiteral("alarm")) {
		voices = alarmVoices(random, mood, words, wanted, length);
	} else if (spec.kind == QStringLiteral("ambience")) {
		voices = ambienceVoices(random, mood, words, wanted, length);
	} else {
		voices = impactVoices(random, mood);
	}
	if (!sustained && spec.durationSeconds > 0.0) {
		const double natural = voicesLength(voices);
		if (natural > 0.0) {
			stretchVoices(&voices, std::clamp(length / natural, 0.25, 8.0));
		}
	}
	AudioClip clip;
	clip.channels = 1;
	clip.sampleRate = kSynthRate;
	for (const Voice& voice : voices) {
		renderVoice(voice, random, &clip.samples);
	}
	if (spec.loop) {
		// Exactly the length asked for, plus the crossfade, so the loop is too.
		clip.samples.resize(qint64(std::llround(length * kSynthRate)));
	}
	return clip;
}

GeneratedSound processGeneratedSound(const AudioClip& raw, const SoundGenerationSpec& requested, const QString& source)
{
	const SoundGenerationSpec spec = normalizedSoundGenerationSpec(requested);
	GeneratedSound sound;
	sound.name = soundGenerationName(spec);
	sound.virtualPath = soundGenerationVirtualPath(spec);
	sound.reference = soundGenerationReference(spec);
	sound.source = source;
	sound.seed = spec.seed;
	if (raw.channels < 1 || raw.sampleRate < 1 || raw.samples.size() < raw.channels) {
		sound.error = QCoreApplication::translate("VibeStudioSoundGeneration", "The sound is empty.");
		return sound;
	}
	// Mono: games play effects in mono, placed in the world.
	AudioClip clip;
	clip.channels = 1;
	clip.sampleRate = raw.sampleRate;
	const qint64 frames = raw.frameCount();
	clip.samples.resize(frames);
	double mean = 0.0;
	for (qint64 frame = 0; frame < frames; ++frame) {
		double sum = 0.0;
		for (int channel = 0; channel < raw.channels; ++channel) {
			sum += raw.samples.at(frame * raw.channels + channel);
		}
		clip.samples[frame] = float(sum / raw.channels);
		mean += clip.samples[frame];
	}
	// No DC offset: it wastes headroom and thumps when the sound starts.
	mean /= double(std::max<qint64>(1, frames));
	double peak = 0.0;
	for (float& sample : clip.samples) {
		sample = float(sample - mean);
		peak = std::max(peak, double(std::abs(sample)));
	}
	if (peak < 1e-5) {
		sound.error = QCoreApplication::translate("VibeStudioSoundGeneration", "The sound is silent.");
		return sound;
	}
	const int rate = clip.sampleRate;
	if (!spec.loop) {
		// Silence off both ends, keeping a moment before the start and after
		// the end, then short fades so neither end clicks.
		const double threshold = peak * 0.004;
		qint64 first = 0;
		while (first < clip.samples.size() && std::abs(clip.samples.at(first)) < threshold) {
			++first;
		}
		qint64 last = clip.samples.size() - 1;
		while (last > first && std::abs(clip.samples.at(last)) < threshold) {
			--last;
		}
		first = std::max<qint64>(0, first - rate / 250);
		last = std::min<qint64>(clip.samples.size() - 1, last + rate / 33);
		clip.samples = clip.samples.mid(first, last - first + 1);
		if (spec.durationSeconds > 0.0 && clip.samples.size() > qint64(spec.durationSeconds * rate * 1.5)) {
			// A model that ran long is cut back, with room for its tail.
			clip.samples.resize(qint64(spec.durationSeconds * rate * 1.5));
			sound.notes << QCoreApplication::translate("VibeStudioSoundGeneration", "The sound ran long and was cut to %1 seconds.")
							   .arg(QString::number(spec.durationSeconds * 1.5, 'f', 1));
		}
		const qint64 fadeIn = std::min<qint64>(rate / 500, clip.samples.size() / 4);
		const qint64 fadeOut = std::min<qint64>(rate / 50, clip.samples.size() / 4);
		for (qint64 index = 0; index < fadeIn; ++index) {
			clip.samples[index] *= float(double(index) / double(fadeIn));
		}
		for (qint64 index = 0; index < fadeOut; ++index) {
			clip.samples[clip.samples.size() - 1 - index] *= float(double(index) / double(fadeOut));
		}
	} else {
		// A seamless loop: the end crossfades into the start, at equal power,
		// and the games are told to loop from the first sample.
		// Half a second, or a fifth of a shorter sound: what the synthesizer
		// adds to a loop for this, so its loops come out the length asked for.
		const qint64 count = clip.samples.size();
		const qint64 fade = std::min<qint64>(rate / 2, count / 5);
		if (fade >= 32) {
			// Ends that match (a hum, a beat) cross at equal gain, ends that do
			// not (noise) at equal power; most sounds fall between, by how alike
			// the two ends are.
			double both = 0.0;
			double headEnergy = 0.0;
			double tailEnergy = 0.0;
			for (qint64 index = 0; index < fade; ++index) {
				const double head = clip.samples.at(index);
				const double tail = clip.samples.at(count - fade + index);
				both += head * tail;
				headEnergy += head * head;
				tailEnergy += tail * tail;
			}
			const double correlation = headEnergy > 0.0 && tailEnergy > 0.0 ? std::clamp(both / std::sqrt(headEnergy * tailEnergy), 0.0, 1.0) : 0.0;
			for (qint64 index = 0; index < fade; ++index) {
				const double x = double(index) / double(fade);
				const double in = correlation * x + (1.0 - correlation) * std::sin(0.5 * kPi * x);
				const double out = correlation * (1.0 - x) + (1.0 - correlation) * std::cos(0.5 * kPi * x);
				clip.samples[index] = float(clip.samples.at(index) * in + clip.samples.at(count - fade + index) * out);
			}
			clip.samples.resize(count - fade);
		}
		clip.markers.loop = AudioLoop {0, clip.frameCount()};
	}
	// Normalized to -1 dBFS.
	peak = 0.0;
	for (const float sample : clip.samples) {
		peak = std::max(peak, double(std::abs(sample)));
	}
	const double gain = peak > 1e-9 ? 0.8913 / peak : 1.0;
	for (float& sample : clip.samples) {
		sample = float(sample * gain);
	}
	sound.clip = clip;
	sound.durationMsecs = clip.durationMs();

	SoundGameProfile profile;
	soundGameProfileForId(spec.game, &profile);
	AudioDeliveryOptions options;
	options.preset = profile.preset;
	options.wav.dither = true;
	options.wav.ditherSeed = quint64(spec.seed);
	sound.delivery = renderAudioDelivery(clip, options);
	if (sound.delivery.succeeded() && sound.delivery.peak > 1e-6) {
		// Resampling to the game's rate moves the peak: filtering takes off
		// the sharpest edges, and the filter's ringing can overshoot. The
		// game's copy is aimed at -1 dBFS again; the working copy stays as is.
		const double delivered = 20.0 * std::log10(sound.delivery.peak);
		if (std::abs(delivered + 1.0) > 0.5) {
			AudioClip aimed = clip;
			const float correction = float(std::pow(10.0, (-1.0 - delivered) / 20.0));
			for (float& sample : aimed.samples) {
				sample *= correction;
			}
			sound.delivery = renderAudioDelivery(aimed, options);
		}
	}
	if (!sound.delivery.succeeded()) {
		sound.error = sound.delivery.error.isEmpty() ? QCoreApplication::translate("VibeStudioSoundGeneration", "The sound could not be converted for the game.")
													 : sound.delivery.error;
		return sound;
	}
	sound.peakDecibels = sound.delivery.peak > 0.0 ? 20.0 * std::log10(sound.delivery.peak) : -96.0;
	if (spec.loop && profile.id == QStringLiteral("doom")) {
		sound.notes << QCoreApplication::translate("VibeStudioSoundGeneration", "Doom plays sounds once; the loop is kept only in the working copy.");
	}
	if (!spec.loop && sound.durationMsecs > 4000 && profile.id != QStringLiteral("generic")) {
		sound.notes << QCoreApplication::translate("VibeStudioSoundGeneration", "%1 seconds is long for an effect; the game holds a channel for all of it.")
						   .arg(QString::number(sound.durationMsecs / 1000.0, 'f', 1));
	}
	sound.ok = true;
	return sound;
}

GeneratedSound processGeneratedSoundBytes(const QByteArray& encoded, const QString& mimeType, const SoundGenerationSpec& spec, const QString& source)
{
	QString extension = QStringLiteral("mp3");
	if (encoded.startsWith("RIFF") || mimeType.contains(QStringLiteral("wav"))) {
		extension = QStringLiteral("wav");
	} else if (encoded.startsWith("fLaC") || mimeType.contains(QStringLiteral("flac"))) {
		extension = QStringLiteral("flac");
	} else if (encoded.startsWith("OggS") || mimeType.contains(QStringLiteral("ogg"))) {
		extension = QStringLiteral("ogg");
	}
	const AudioClipResult decoded = decodeAudioClip(QStringLiteral("generated.%1").arg(extension), encoded);
	if (!decoded.succeeded()) {
		GeneratedSound sound;
		sound.source = source;
		sound.error = QCoreApplication::translate("VibeStudioSoundGeneration", "The model's sound could not be read: %1").arg(decoded.error);
		return sound;
	}
	return processGeneratedSound(decoded.clip, spec, source);
}

SoundGenerationWriteReport writeGeneratedSound(const GeneratedSound& sound, const SoundGenerationSpec& requested, const SoundGenerationOutput& output)
{
	SoundGenerationWriteReport report;
	const auto fail = [&report](const QString& message) {
		report.ok = false;
		report.error = message;
		return report;
	};
	if (!sound.ok) {
		return fail(sound.error.isEmpty() ? QCoreApplication::translate("VibeStudioSoundGeneration", "There is no finished sound to write.") : sound.error);
	}
	const SoundGenerationSpec spec = normalizedSoundGenerationSpec(requested);
	SoundGameProfile profile;
	soundGameProfileForId(spec.game, &profile);
	const QString folder = QDir::cleanPath(output.folder.trimmed().isEmpty() ? QDir::currentPath() : output.folder.trimmed());
	report.reference = sound.reference;
	QString error;
	if (profile.lumps) {
		const QString wadPath = output.wadPath.trimmed().isEmpty() ? QDir(folder).filePath(QStringLiteral("wads/vibestudio_sounds.wad")) : output.wadPath.trimmed();
		PackageStagingModel staging;
		const bool exists = QFileInfo::exists(wadPath);
		if (exists) {
			PackageArchive archive;
			if (!archive.load(wadPath, &error)) {
				return fail(QCoreApplication::translate("VibeStudioSoundGeneration", "Could not open %1: %2").arg(QDir::toNativeSeparators(wadPath), error));
			}
			if (!staging.loadBaseArchive(archive, &error)) {
				return fail(error);
			}
			if (!output.replaceExisting) {
				for (const PackageEntry& entry : archive.entries()) {
					if (entry.virtualPath.compare(sound.name, Qt::CaseInsensitive) == 0) {
						report.alreadyExists = true;
						return fail(QCoreApplication::translate("VibeStudioSoundGeneration", "%1 already holds a sound called %2.").arg(QDir::toNativeSeparators(wadPath), sound.name));
					}
				}
			}
		} else if (!staging.createEmpty(PackageArchiveFormat::Wad, QStringLiteral("PWAD"), &error)) {
			return fail(error);
		}
		if (!stageAudioDelivery(sound.delivery.bytes, sound.name, &staging, output.replaceExisting, &error)) {
			return fail(QCoreApplication::translate("VibeStudioSoundGeneration", "Could not add %1 to %2: %3").arg(sound.name, QDir::toNativeSeparators(wadPath), error));
		}
		if (!output.dryRun) {
			QDir().mkpath(QFileInfo(wadPath).absolutePath());
		}
		PackageWriteRequest request;
		request.format = PackageArchiveFormat::Wad;
		request.destinationPath = wadPath;
		request.allowOverwrite = exists;
		request.allowInPlaceOverwrite = exists;
		request.dryRun = output.dryRun;
		const PackageWriteReport written = staging.writeArchive(request);
		if (!written.succeeded()) {
			return fail(QCoreApplication::translate("VibeStudioSoundGeneration", "Could not write %1: %2")
							.arg(QDir::toNativeSeparators(wadPath), (written.blockedMessages + written.warnings).join(QStringLiteral(" "))));
		}
		report.writtenPaths << wadPath;
		report.notes << QCoreApplication::translate("VibeStudioSoundGeneration",
			"Doom plays %1 where a DEHACKED patch or a port's SNDINFO names it; a stock name such as DSPISTOL replaces that sound outright.").arg(sound.name);
	} else {
		const QString path = QDir(folder).filePath(sound.virtualPath);
		if (QFileInfo::exists(path) && !output.replaceExisting) {
			report.alreadyExists = true;
			return fail(QCoreApplication::translate("VibeStudioSoundGeneration", "%1 already exists.").arg(QDir::toNativeSeparators(path)));
		}
		if (!output.dryRun && !QDir().mkpath(QFileInfo(path).absolutePath())) {
			return fail(QCoreApplication::translate("VibeStudioSoundGeneration", "Could not create the folder for %1.").arg(QDir::toNativeSeparators(path)));
		}
		if (!writeAudioDelivery(sound.delivery, path, output.replaceExisting, {}, &error, output.dryRun)) {
			return fail(error);
		}
		report.writtenPaths << path;
		if (profile.id == QStringLiteral("quake")) {
			report.notes << QCoreApplication::translate("VibeStudioSoundGeneration", "QuakeC plays it as \"%1\": precache_sound, then sound.").arg(sound.reference);
		} else if (profile.id == QStringLiteral("quake2") || profile.id == QStringLiteral("quake3")) {
			report.notes << QCoreApplication::translate("VibeStudioSoundGeneration", "A target_speaker plays it with its noise key set to \"%1\".").arg(sound.reference);
		}
	}

	// What made it, for review and to make it again.
	QJsonObject provenance = output.provenance;
	provenance.insert(QStringLiteral("schema"), QStringLiteral("vibestudio.generated-sound/1"));
	provenance.insert(QStringLiteral("createdUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	provenance.insert(QStringLiteral("spec"), soundGenerationSpecJson(spec));
	provenance.insert(QStringLiteral("sound"), generatedSoundJson(sound));
	provenance.insert(QStringLiteral("outputs"), QJsonArray::fromStringList(report.writtenPaths));
	provenance.insert(QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(sound.delivery.bytes, QCryptographicHash::Sha256).toHex()));
	const QString recordPath = QDir(folder).filePath(QStringLiteral(".vibestudio/generated/sounds/%1.json").arg(sound.name));
	if (!output.dryRun && !writeBytes(recordPath, QJsonDocument(provenance).toJson(QJsonDocument::Indented), &error)) {
		return fail(error);
	}
	report.writtenPaths << recordPath;
	report.ok = true;
	return report;
}

QJsonObject soundGenerationSpecJson(const SoundGenerationSpec& spec)
{
	return QJsonObject {
		{QStringLiteral("prompt"), spec.prompt},
		{QStringLiteral("game"), spec.game},
		{QStringLiteral("kind"), spec.kind},
		{QStringLiteral("name"), spec.name},
		{QStringLiteral("folder"), spec.folder},
		{QStringLiteral("durationSeconds"), spec.durationSeconds},
		{QStringLiteral("loop"), spec.loop},
		{QStringLiteral("promptInfluence"), spec.promptInfluence},
		{QStringLiteral("seed"), double(spec.seed)},
		{QStringLiteral("variants"), spec.variants},
	};
}

QJsonObject generatedSoundJson(const GeneratedSound& sound)
{
	QJsonObject object {
		{QStringLiteral("ok"), sound.ok},
		{QStringLiteral("name"), sound.name},
		{QStringLiteral("virtualPath"), sound.virtualPath},
		{QStringLiteral("reference"), sound.reference},
		{QStringLiteral("source"), sound.source},
		{QStringLiteral("seed"), double(sound.seed)},
		{QStringLiteral("durationMs"), double(sound.durationMsecs)},
		{QStringLiteral("loop"), sound.clip.markers.loop.has_value()},
		{QStringLiteral("notes"), QJsonArray::fromStringList(sound.notes)},
	};
	if (!sound.error.isEmpty()) {
		object.insert(QStringLiteral("error"), sound.error);
	}
	if (sound.delivery.succeeded()) {
		object.insert(QStringLiteral("sampleRate"), sound.delivery.plan.sampleRate);
		object.insert(QStringLiteral("format"), sound.delivery.plan.dmx ? QStringLiteral("dmx") : QStringLiteral("wav-%1").arg(audioWavBits(sound.delivery.plan.wav.format)));
		object.insert(QStringLiteral("bytes"), double(sound.delivery.bytes.size()));
		object.insert(QStringLiteral("peakDb"), std::round(sound.peakDecibels * 10.0) / 10.0);
	}
	return object;
}

QJsonObject soundGenerationWriteReportJson(const SoundGenerationWriteReport& report)
{
	QJsonObject object {
		{QStringLiteral("ok"), report.ok},
		{QStringLiteral("written"), QJsonArray::fromStringList(report.writtenPaths)},
		{QStringLiteral("reference"), report.reference},
		{QStringLiteral("notes"), QJsonArray::fromStringList(report.notes)},
	};
	if (!report.error.isEmpty()) {
		object.insert(QStringLiteral("error"), report.error);
		object.insert(QStringLiteral("alreadyExists"), report.alreadyExists);
	}
	return object;
}

QString generatedSoundSummary(const GeneratedSound& sound)
{
	if (!sound.ok) {
		return sound.error;
	}
	const QString seconds = QString::number(sound.durationMsecs / 1000.0, 'f', 2);
	const QString format = deliveryFormatText(sound.delivery.plan);
	return sound.clip.markers.loop
		? QCoreApplication::translate("VibeStudioSoundGeneration", "%1: %2 s loop, %3, peak %4 dBFS").arg(sound.name, seconds, format, QString::number(sound.peakDecibels, 'f', 1))
		: QCoreApplication::translate("VibeStudioSoundGeneration", "%1: %2 s, %3, peak %4 dBFS").arg(sound.name, seconds, format, QString::number(sound.peakDecibels, 'f', 1));
}

} // namespace vibestudio
