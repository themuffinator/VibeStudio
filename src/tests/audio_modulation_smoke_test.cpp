#include "core/audio_effects.h"
#include "core/audio_session_io.h"
#include <QCoreApplication>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
		std::cerr << message << '\n';
	return value;
}
bool close(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }
double energy(const std::vector<double> &samples, size_t first = 0)
{
	double sum = 0;
	for (size_t i = first * 2; i < samples.size(); ++i)
		sum += samples[i] * samples[i];
	return sum;
}
std::vector<double> impulse(int frames, bool antiphase = false)
{
	std::vector<double> result(size_t(frames) * 2);
	result[0] = 1;
	result[1] = antiphase ? -1 : 0;
	return result;
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	bool ok = true;
	QString error;
	for (int rate : {1, 8000, 48000, 384000})
		for (const auto &type : {"reverb", "chorus", "flanger", "tremolo", "phaser"}) {
			auto effect = makeAudioEffect(type, rate);
			AudioEffectsProcessor processor;
			auto samples = impulse(4096);
			ok &= expect(processor.prepare({effect}, rate, &error) && processor.process(samples),
			             "default new effect remains finite across supported rates");
			processor.reset();
			samples.assign(samples.size(), 0);
			ok &= expect(processor.process(samples) && energy(samples) == 0,
			             "reset masks new effect histories without clearing buffers");
		}
	// Independent amplitude modulation formula, including channel phase.
	auto tremolo = makeAudioEffect("tremolo", 1000);
	tremolo.parameters["rateHz"] = 7;
	tremolo.parameters["depth"] = .8;
	tremolo.parameters["stereoPhaseDeg"] = 180;
	AudioEffectsProcessor trem;
	std::vector<double> constant(8000, .25);
	ok &= trem.prepare({tremolo}, 1000, &error) && trem.process(constant);
	for (size_t frame = 0; frame < constant.size() / 2; ++frame) {
		const double sine = std::sin(2 * std::numbers::pi * 7 * frame / 1000);
		ok &= expect(close(constant[frame * 2], .25 * (.6 - .4 * sine)) &&
		                 close(constant[frame * 2 + 1], .25 * (.6 + .4 * sine)),
		             "tremolo matches sample-clock phase oracle");
	}
	// A linear ramp interpolates to its exact fractional coordinate, independently of ring layout.
	for (const auto &type : {"chorus", "flanger"}) {
		auto effect = makeAudioEffect(type, 1000);
		effect.parameters["minDelayMs"] = 2.5;
		effect.parameters["depthMs"] = 3;
		effect.parameters["rateHz"] = 3;
		effect.parameters["feedback"] = 0;
		effect.parameters["mix"] = 1;
		std::vector<double> ramp(16000);
		for (size_t frame = 0; frame < ramp.size() / 2; ++frame) {
			ramp[frame * 2] = double(frame) / 10000;
			ramp[frame * 2 + 1] = -double(frame) / 20000;
		}
		AudioEffectsProcessor processor;
		ok &= processor.prepare({effect}, 1000, &error) && processor.process(ramp);
		bool matches = true;
		for (size_t frame = 8; frame < ramp.size() / 2; ++frame)
			for (size_t channel = 0; channel < 2; ++channel) {
				const double delay =
				    2.5 +
				    1.5 * (1 + std::sin(2 * std::numbers::pi * 3 * frame / 1000 + channel * std::numbers::pi / 2));
				matches &= close(ramp[frame * 2 + channel], (double(frame) - delay) / (channel ? -20000 : 10000));
			}
		ok &= expect(matches, "swept fractional delay matches independently interpolated input");
		effect.parameters["depthMs"] = 0;
		effect.parameters["minDelayMs"] = 2;
		effect.parameters["feedback"] = -.5;
		auto echo = impulse(20);
		ok &= processor.prepare({effect}, 1000, &error) && processor.process(echo);
		for (size_t frame = 0; frame < 20; ++frame)
			ok &=
			    expect(close(echo[frame * 2], frame >= 2 && frame % 2 == 0 ? std::pow(-.5, double(frame / 2) - 1) : 0),
			           "negative modulation feedback has alternating echo polarity");
	}
	// Static allpass transfer response H(z)=(a+z^-1)/(1+a*z^-1), four stages.
	for (double frequency : {100.0, 1000.0, 7000.0})
		for (double mix : {.5, 1.0}) {
			auto effect = makeAudioEffect("phaser", 48000);
			effect.parameters["depthOctaves"] = 0;
			effect.parameters["feedback"] = 0;
			effect.parameters["mix"] = mix;
			std::vector<double> samples(192000);
			for (size_t i = 0; i < samples.size() / 2; ++i)
				samples[i * 2] = samples[i * 2 + 1] = .2 * std::sin(2 * std::numbers::pi * frequency * i / 48000);
			const double dryEnergy = energy(samples, 48000);
			std::complex<double> response{1, 0}, z = std::polar(1.0, -2 * std::numbers::pi * frequency / 48000);
			for (double multiplier : {.5, 1.0, 1.5, 2.0}) {
				const double tangent = std::tan(std::numbers::pi * 1000 * multiplier / 48000),
				             a = (tangent - 1) / (tangent + 1);
				response *= (a + z) / (1.0 + a * z);
			}
			AudioEffectsProcessor processor;
			ok &= processor.prepare({effect}, 48000, &error) && processor.process(samples);
			ok &= expect(close(std::sqrt(energy(samples, 48000) / dryEnergy), std::abs(1 - mix + mix * response), 1e-8),
			             "phaser agrees with independent complex transfer function");
		}
	auto reverb = makeAudioEffect("reverb", 44100);
	reverb.parameters["mix"] = 1;
	reverb.parameters["preDelayMs"] = 0;
	AudioEffectsProcessor room;
	auto reflection = impulse(44100);
	ok &= room.prepare({reverb}, 44100, &error) && room.process(reflection);
	ok &= expect(energy(std::vector<double>(reflection.begin(), reflection.begin() + 2232)) == 0 &&
	                 close(reflection[2232], .75 / 128) && reflection[2233] == 0,
	             "first left reflection has independently predicted comb delay and allpass gain");
	reverb.parameters["preDelayMs"] = 10;
	auto predelayed = impulse(44100);
	ok &= room.prepare({reverb}, 44100, &error) && room.process(predelayed);
	ok &= expect(std::equal(reflection.begin(), reflection.end() - 882, predelayed.begin() + 882),
	             "pre-delay shifts every wet sample by exactly 441 frames");
	reverb.parameters["width"] = 0;
	auto mono = impulse(44100);
	ok &= room.prepare({reverb}, 44100, &error) && room.process(mono);
	bool monoEqual = true;
	for (size_t i = 0; i < mono.size(); i += 2)
		monoEqual &= mono[i] == mono[i + 1];
	ok &= expect(monoEqual, "zero width produces identical wet channels");
	reverb.parameters["width"] = 1;
	auto opposite = impulse(44100, true);
	ok &= room.prepare({reverb}, 44100, &error) && room.process(opposite);
	ok &= expect(energy(opposite) > 0, "antiphase stereo input still excites reverb");
	std::array<double, 2> late{};
	for (size_t i = 0; i < 2; ++i) {
		reverb.parameters["decaySeconds"] = i ? 4 : .2;
		auto decay = impulse(88200);
		ok &= room.prepare({reverb}, 44100, &error) && room.process(decay);
		late[i] = energy(decay, 44100);
	}
	ok &= expect(late[1] > late[0] * 1000, "long decay retains more late-tail energy");
	for (size_t i = 0; i < 2; ++i) {
		reverb.parameters["dampingHz"] = i ? 18000 : 100;
		auto damped = impulse(88200);
		ok &= room.prepare({reverb}, 44100, &error) && room.process(damped);
		late[i] = energy(damped, 44100);
	}
	ok &= expect(late[1] > late[0] * 2, "strong damping removes more late impulse energy");
	AudioSession excessive;
	excessive.sampleRate = 384000;
	for (int track = 0; track < 16; ++track) {
		AudioSessionTrack strip;
		strip.id = strip.name = QString::number(track);
		for (int insert = 0; insert < 8; ++insert) {
			auto effect = makeAudioEffect("reverb", excessive.sampleRate);
			effect.parameters["roomSize"] = 2;
			effect.parameters["preDelayMs"] = 250;
			strip.effects.append(effect);
		}
		excessive.tracks.append(strip);
	}
	ok &= expect(!validateAudioSessionStructure(excessive).isEmpty(),
	             "aggregate reverb delay storage participates in 128 MiB session admission");
	// Full chain ordering/state continuity, exact across arbitrary processing blocks and native round trips.
	AudioEffectChain chain;
	for (const auto &type : {"reverb", "chorus", "flanger", "phaser", "tremolo"})
		chain.append(makeAudioEffect(type, 48000));
	std::vector<double> source(131072);
	for (size_t i = 0; i < source.size(); ++i)
		source[i] = .05 * std::sin(double(i) * .117);
	AudioEffectsProcessor whole;
	auto reference = source;
	ok &= whole.prepare(chain, 48000, &error) && whole.process(reference);
	for (size_t block : {size_t(1), size_t(7), size_t(256), size_t(4096)}) {
		AudioEffectsProcessor pieces;
		auto rendered = source;
		ok &= pieces.prepare(chain, 48000, &error);
		for (size_t i = 0; i < rendered.size(); i += block * 2)
			ok &= pieces.process(std::span<double>(rendered).subspan(i, std::min(block * 2, rendered.size() - i)));
		ok &= expect(rendered == reference, "new effect chain is sample-exact at every tested block size");
		pieces.reset();
		auto replay = source;
		ok &= pieces.process(replay);
		ok &= expect(replay == reference, "reset restarts modulation phase and every delay history");
	}
	AudioProject project;
	project.clip = {2, 48000, {1, 1, 0, 0}};
	auto session = importAudioSessionSource({}, project).session;
	session.masterEffects = chain;
	AudioSession decoded;
	ok &= expect(decodeAudioSession(encodeAudioSession(session, &error), &decoded, &error) &&
	                 decoded.masterEffects == chain,
	             "native v3 preserves new processor descriptors");
	ok &= expect(audioSessionFrames(session) == 96002, "new effects extend default session by its configured tail");
	for (const auto &type : {"reverb", "chorus", "flanger", "phaser"}) {
		auto effect = makeAudioEffect(type, 8000);
		for (const auto &parameter : audioEffectParameters(type, 8000))
			effect.parameters[parameter.key] = parameter.maximum;
		auto extreme = impulse(240000);
		AudioEffectsProcessor processor;
		ok &= expect(processor.prepare({effect}, 8000, &error) && processor.process(extreme) && energy(extreme) < 1e12,
		             "extreme effect parameters stay finite and bounded over thirty seconds");
	}
	return ok ? 0 : 1;
}
