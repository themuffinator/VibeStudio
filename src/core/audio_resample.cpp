#include "core/audio_resample.h"

// Sample rate converter designed by Aleksey Vaneev of Voxengo.
// r8brain-free-src 7.5, cb2abb9977efe2471979b380ed95daa56ab4fdb9 (MIT),
// with Takuya Ooura's permissively licensed FFT. See docs/CREDITS.md and
// external/audio/r8brain-free-src/VIBESTUDIO.md. Upstream headers are unchanged.
#define R8B_FILTER_CACHE_MAX 8
#define R8B_FRACBANK_CACHE_MAX 2
#include <CDSPResampler.h>

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <vector>

namespace vibestudio
{

qint64 audioFrameAtSampleRate(qint64 frame, int sourceRate, int targetRate)
{
	if (frame < 0 || frame > AudioSampleLimit || sourceRate < 1 || sourceRate > 384000 || targetRate < 1 ||
	    targetRate > 384000) {
		return -1;
	}
	return (frame * targetRate + sourceRate / 2) / sourceRate;
}

AudioClipResult resampleAudioClip(const AudioClip& clip, int targetRate, const AudioWorkControl& control)
{
	AudioClipResult result;
	const auto cancelled = [&]() {
		if (control.cancelled && control.cancelled()) {
			result.cancelled = true;
			result.clip = {};
			return true;
		}
		return false;
	};
	if (cancelled()) {
		return result;
	}
	result.error = validateAudioClip(clip, true);
	if (!result.error.isEmpty()) {
		return result;
	}
	if (targetRate < 1 || targetRate > 384000) {
		result.error = QCoreApplication::translate("VibeStudioAudio", "Choose a sample rate between 1 and 384000 Hz.");
		return result;
	}
	if (targetRate == clip.sampleRate || clip.samples.isEmpty()) {
		result.clip = clip;
		result.clip.sampleRate = targetRate;
		cancelled();
		return result;
	}
	const qint64 outputFrames =
	    std::max<qint64>(1, audioFrameAtSampleRate(clip.frameCount(), clip.sampleRate, targetRate));
	if (outputFrames > AudioSampleLimit / clip.channels) {
		result.error =
		    QCoreApplication::translate("VibeStudioAudio", "Resampling would exceed the 16777216-sample limit.");
		return result;
	}
	try {
		// Limit intermediate buffers when upsampling. The minimum block size is
		// one input frame even for a very large ratio. Process channels separately
		// with identical cleared filter state to keep their timing aligned.
		const int blockSize = static_cast<int>(std::clamp<qint64>(8192LL * clip.sampleRate / targetRate, 1, 4096));
		r8b::CDSPResampler24 converter(clip.sampleRate, targetRate, blockSize, 2.0);
		const qint64 requiredInput = converter.getInputRequiredForOutput(static_cast<int>(outputFrames));
		// Very low destination rates may need long filter padding. Bound total
		// work across channels as well as the stored output, before allocating it.
		if (requiredInput <= 0 || requiredInput > 4 * AudioSampleLimit / clip.channels) {
			result.error = QCoreApplication::translate(
			    "VibeStudioAudio", "This rate conversion exceeds the processing limit. Choose a closer sample rate.");
			return result;
		}
		result.clip = {clip.channels, targetRate, QVector<float>(outputFrames * clip.channels)};
		result.clip.markers =
		    resampleAudioMarkers(clip.markers, clip.frameCount(), outputFrames, clip.sampleRate, targetRate);
		std::vector<double> input(blockSize);
		float* destination = result.clip.samples.data();
		const float* source = clip.samples.constData();
		for (int channel = 0; channel < clip.channels; ++channel) {
			converter.clear();
			qint64 inputPosition = 0, outputPosition = 0;
			while (outputPosition < outputFrames) {
				if (cancelled()) {
					return result;
				}
				const int count = static_cast<int>(std::min<qint64>(blockSize, requiredInput - inputPosition));
				if (count <= 0) {
					result.clip = {};
					result.error = QCoreApplication::translate(
					    "VibeStudioAudio", "Sample rate conversion ended before the required frame count.");
					return result;
				}
				for (int index = 0; index < count; ++index) {
					const qint64 frame = inputPosition + index;
					input[index] = frame < clip.frameCount() ? source[frame * clip.channels + channel] : 0.0;
				}
				double* converted = nullptr;
				const int available = converter.process(input.data(), count, converted);
				const qint64 take = std::min<qint64>(available, outputFrames - outputPosition);
				for (qint64 index = 0; index < take; ++index) {
					const double value = converted[index];
					if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
						result.clip = {};
						result.error = QCoreApplication::translate(
						    "VibeStudioAudio", "Resampled audio exceeds floating-point headroom. Reduce gain first.");
						return result;
					}
					destination[(outputPosition + index) * clip.channels + channel] = static_cast<float>(value);
				}
				inputPosition += count;
				outputPosition += take;
			}
		}
		cancelled();
	} catch (const std::bad_alloc&) {
		result.clip = {};
		result.error = QCoreApplication::translate("VibeStudioAudio", "Not enough memory to resample this sound.");
	}
	return result;
}

} // namespace vibestudio
