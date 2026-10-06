#include "core/audio_reverb.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibestudio
{
namespace
{
// Topology and 44.1 kHz delay tuning derived from Freeverb Components:
// Written by Jezar at Dreampoint, June 2000. This code is public domain.
// https://github.com/sinshu/freeverb/tree/cfcea55553fb59ac57ebf2a237f72cad4296f2b0/Components
// Original VibeStudio changes: sample-rate/room scaling, per-comb decay,
// frequency damping, stereo-preserving excitation, predelay, true allpass
// sections, double precision and constant-time reset. See docs/CREDITS.md.
constexpr std::array<int, 12> tuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617, 556, 441, 341, 225};
std::array<double, 26> lengths(int rate, double room, double preDelayMs)
{
	std::array<double, 26> result{};
	result[0] = result[1] = preDelayMs * rate / 1000;
	for (size_t channel = 0; channel < 2; ++channel)
		for (size_t i = 0; i < tuning.size(); ++i)
			result[2 + channel * 12 + i] = std::max(1.0, (tuning[i] + 23 * channel) * rate * room / 44100);
	return result;
}
double flush(double value) { return std::abs(value) < 1e-300 ? 0 : value; }
} // namespace
size_t AudioReverb::memoryBytes(int rate, double room, double preDelayMs)
{
	size_t bytes = sizeof(AudioReverb);
	for (const auto size : lengths(rate, room, preDelayMs))
		bytes += (size > 0 ? size_t(std::ceil(size)) + 1 : 0) * sizeof(double);
	return bytes;
}
double AudioReverb::Line::read(double current) const
{
	const size_t whole = size_t(delay);
	const double fraction = delay - double(whole);
	const auto history = [&](size_t back) {
		if (back == 0)
			return current;
		return back <= valid ? samples[(cursor + samples.size() - back) % samples.size()] : 0.0;
	};
	return history(whole) * (1 - fraction) + history(whole + 1) * fraction;
}
void AudioReverb::Line::write(double sample)
{
	samples[cursor] = sample;
	if (++cursor == samples.size())
		cursor = 0;
	valid = std::min(samples.size(), valid + 1);
}
void AudioReverb::prepare(int rate, double room, double decay, double dampingHz, double preDelayMs, double maximumRoom,
                          double maximumPredelay)
{
	m_rate = rate;
	m_room = m_decay = m_cutoff = m_predelay = -1;
	const auto sizes = lengths(rate, std::max(room, maximumRoom), std::max(preDelayMs, maximumPredelay));
	for (size_t i = 0; i < m_lines.size(); ++i) {
		m_lines[i].samples = std::vector<double>(sizes[i] > 0 ? size_t(std::ceil(sizes[i])) + 1 : 0);
	}
	setParameters(room, decay, dampingHz, preDelayMs);
	reset();
}
void AudioReverb::setParameters(double room, double decay, double cutoff, double predelay)
{
	if (m_room != room || m_predelay != predelay) {
		const auto taps = lengths(m_rate, room, predelay);
		for (size_t i = 0; i < m_lines.size(); ++i)
			m_lines[i].delay = taps[i];
	}
	if (m_room != room || m_decay != decay)
		for (size_t channel = 0; channel < 2; ++channel)
			for (size_t i = 0; i < 8; ++i) {
				auto &line = m_lines[2 + channel * 12 + i];
				line.feedback = std::pow(.001, line.delay / (m_rate * decay));
			}
	if (m_cutoff != cutoff)
		m_damping = std::exp(-2 * std::numbers::pi * cutoff / m_rate);
	m_room = room;
	m_decay = decay;
	m_cutoff = cutoff;
	m_predelay = predelay;
}
void AudioReverb::reset()
{
	for (auto &line : m_lines) {
		line.cursor = line.valid = 0;
		line.filtered = 0;
	}
}
bool AudioReverb::process(double &left, double &right, double width, double mix)
{
	std::array<double, 2> input{left, right}, wet{};
	if (!m_lines[0].samples.empty())
		for (size_t channel = 0; channel < 2; ++channel) {
			input[channel] = m_lines[channel].read(channel == 0 ? left : right);
			m_lines[channel].write(channel == 0 ? left : right);
		}
	for (size_t channel = 0; channel < 2; ++channel) {
		const double excitation = .75 * input[channel] + .25 * input[1 - channel];
		for (size_t i = 0; i < 8; ++i) {
			auto &line = m_lines[2 + channel * 12 + i];
			const double delayed = line.read();
			line.filtered = flush((1 - m_damping) * delayed + m_damping * line.filtered);
			const double next = excitation + line.feedback * line.filtered;
			if (!std::isfinite(next))
				return false;
			line.write(next);
			wet[channel] += delayed / 8;
		}
		for (size_t i = 8; i < 12; ++i) {
			auto &line = m_lines[2 + channel * 12 + i];
			const double out = flush(line.read() - .5 * wet[channel]);
			const double next = wet[channel] + .5 * out;
			if (!std::isfinite(next))
				return false;
			line.write(next);
			wet[channel] = out;
		}
	}
	const double middle = (wet[0] + wet[1]) / 2, side = (wet[0] - wet[1]) * width / 2;
	left = (1 - mix) * left + mix * (middle + side);
	right = (1 - mix) * right + mix * (middle - side);
	return std::isfinite(left) && std::isfinite(right);
}
} // namespace vibestudio
