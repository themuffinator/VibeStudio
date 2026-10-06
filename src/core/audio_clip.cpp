#include "core/audio_clip.h"
#include "core/audio_export.h"

#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace vibestudio
{
namespace
{

bool fail(QString* error, const QString& message)
{
	if (error) {
		*error = message;
	}
	return false;
}

} // namespace

bool audioPathsReferToSameFile(const QString& first, const QString& second)
{
	const auto resolved = [](const QString& path) {
		const QFileInfo info(path);
		const auto canonical = info.canonicalFilePath();
		return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
	};
#ifdef Q_OS_WIN
	if (resolved(first).compare(resolved(second), Qt::CaseInsensitive) == 0) return true;
	const auto native = [](const QString& path) { return std::filesystem::path(path.toStdWString()); };
#else
	if (resolved(first) == resolved(second)) return true;
	const auto native = [](const QString& path) { return std::filesystem::path(QFile::encodeName(path).constData()); };
#endif
	// Canonical names resolve symlinks; filesystem identity also detects hard links.
	std::error_code error;
	return std::filesystem::equivalent(native(first), native(second), error) && !error;
}

qint64 AudioClip::frameCount() const { return channels > 0 ? samples.size() / channels : 0; }
qint64 AudioClip::durationMs() const
{
	return sampleRate > 0 ? (frameCount() * 1000 + sampleRate - 1) / sampleRate : 0;
}

QString validateAudioClip(const AudioClip& clip, bool allowEmpty)
{
	if (clip.channels < 1 || clip.channels > 8 || clip.sampleRate < 1 || clip.sampleRate > 384000) {
		return QCoreApplication::translate("VibeStudioAudio",
		                                   "Audio requires 1–8 channels and a sample rate between 1 and 384000 Hz.");
	}
	if ((!allowEmpty && clip.samples.isEmpty()) || clip.samples.size() > AudioSampleLimit ||
	    clip.samples.size() % clip.channels != 0) {
		return QCoreApplication::translate("VibeStudioAudio",
		                                   "Audio must contain complete frames and at most 16777216 samples.");
	}
	for (float value : clip.samples) {
		if (!std::isfinite(value)) {
			return QCoreApplication::translate("VibeStudioAudio", "Audio contains a non-finite sample.");
		}
	}
	return validateAudioMarkers(clip.markers, clip.frameCount());
}

AudioClipResult applyAudioEdit(const AudioClip& clip, const AudioEdit& edit, const AudioWorkControl& control)
{
	AudioClipResult result;
	result.error = validateAudioClip(clip);
	if (!result.error.isEmpty()) {
		return result;
	}
	const qint64 end = edit.endFrame == -1 ? clip.frameCount() : edit.endFrame;
	if (edit.firstFrame < 0 || end > clip.frameCount() || end <= edit.firstFrame) {
		result.error =
		    QCoreApplication::translate("VibeStudioAudio", "Select a non-empty frame range within the sound.");
		return result;
	}
	const QString op = edit.operation;
	const QStringList operations = {
	    QStringLiteral("trim"),    QStringLiteral("delete"),   QStringLiteral("silence"), QStringLiteral("reverse"),
	    QStringLiteral("fade-in"), QStringLiteral("fade-out"), QStringLiteral("gain"),    QStringLiteral("normalize"),
	    QStringLiteral("mono"),    QStringLiteral("stereo"),   QStringLiteral("invert"),  QStringLiteral("remove-dc")};
	if (!operations.contains(op)) {
		result.error = QCoreApplication::translate("VibeStudioAudio", "Unknown audio edit: %1.").arg(op);
		return result;
	}
	if ((op == QStringLiteral("gain") || op == QStringLiteral("normalize")) &&
	    (!std::isfinite(edit.decibels) || edit.decibels < -96.0 ||
	     edit.decibels > (op == QStringLiteral("normalize") ? 0.0 : 24.0))) {
		result.error = QCoreApplication::translate(
		    "VibeStudioAudio", "Gain must be between -96 and +24 dB; normalization must be between -96 and 0 dBFS.");
		return result;
	}
	if (op == QStringLiteral("mono") && (edit.firstFrame != 0 || end != clip.frameCount())) {
		result.error = QCoreApplication::translate(
		    "VibeStudioAudio", "Mono conversion applies to the whole sound. Select all frames first.");
		return result;
	}
	if (op == QStringLiteral("stereo") && (clip.channels != 1 || edit.firstFrame != 0 || end != clip.frameCount() ||
	                                       clip.frameCount() > AudioSampleLimit / 2)) {
		result.error = QCoreApplication::translate(
		    "VibeStudioAudio", "Stereo conversion requires a whole mono sound within the sample limit.");
		return result;
	}
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
	const qint64 firstSample = edit.firstFrame * clip.channels;
	const qint64 endSample = end * clip.channels;
	result.clip = clip;
	if (op == QStringLiteral("trim")) {
		result.clip.samples = clip.samples.mid(firstSample, endSample - firstSample);
		result.clip.markers = trimAudioMarkers(clip.markers, edit.firstFrame, end);
	} else if (op == QStringLiteral("delete")) {
		result.clip.samples.remove(firstSample, endSample - firstSample);
		result.clip.markers = replaceAudioMarkers(clip.markers, edit.firstFrame, end, 0);
	} else if (op == QStringLiteral("mono")) {
		result.clip.channels = 1;
		result.clip.samples.resize(clip.frameCount());
		for (qint64 frame = 0; frame < clip.frameCount(); ++frame) {
			if (frame % 4096 == 0 && cancelled()) {
				return result;
			}
			double sum = 0;
			for (int channel = 0; channel < clip.channels; ++channel) {
				sum += clip.samples[frame * clip.channels + channel];
			}
			result.clip.samples[frame] = static_cast<float>(sum / clip.channels);
		}
	} else if (op == QStringLiteral("stereo")) {
		result.clip.channels = 2;
		result.clip.samples.resize(clip.frameCount() * 2);
		for (qint64 frame = 0; frame < clip.frameCount(); ++frame) {
			if (frame % 4096 == 0 && cancelled()) {
				return result;
			}
			result.clip.samples[frame * 2] = result.clip.samples[frame * 2 + 1] = clip.samples[frame];
		}
	} else {
		double gain = std::pow(10.0, edit.decibels / 20.0);
		QVector<double> means(clip.channels, 0.0);
		if (op == QStringLiteral("remove-dc")) {
			for (qint64 frame = edit.firstFrame; frame < end; ++frame) {
				if (frame % 4096 == 0 && cancelled()) {
					return result;
				}
				for (int channel = 0; channel < clip.channels; ++channel) {
					means[channel] += clip.samples[frame * clip.channels + channel];
				}
			}
			for (double& mean : means) {
				mean /= end - edit.firstFrame;
			}
		}
		if (op == QStringLiteral("normalize")) {
			double peak = 0;
			for (qint64 sample = firstSample; sample < endSample; ++sample) {
				if (sample % 4096 == 0 && cancelled()) {
					return result;
				}
				peak = std::max(peak, std::abs(static_cast<double>(clip.samples[sample])));
			}
			gain = peak > 0 ? gain / peak : 1.0;
		}
		for (qint64 frame = edit.firstFrame; frame < end; ++frame) {
			if (frame % 4096 == 0 && cancelled()) {
				return result;
			}
			double scale = gain;
			if (op == QStringLiteral("fade-in") || op == QStringLiteral("fade-out")) {
				const double ramp = end - edit.firstFrame > 1
				                        ? static_cast<double>(frame - edit.firstFrame) / (end - edit.firstFrame - 1)
				                        : 0;
				scale = op == QStringLiteral("fade-in") ? ramp : (end - edit.firstFrame > 1 ? 1.0 - ramp : 0.0);
			}
			for (int channel = 0; channel < clip.channels; ++channel) {
				const qint64 index = frame * clip.channels + channel;
				if (op == QStringLiteral("reverse")) {
					result.clip.samples[index] =
					    clip.samples[(end - 1 - (frame - edit.firstFrame)) * clip.channels + channel];
				} else if (op == QStringLiteral("silence")) {
					result.clip.samples[index] = 0;
				} else if (op == QStringLiteral("invert")) {
					result.clip.samples[index] = -clip.samples[index];
				} else if (op == QStringLiteral("remove-dc")) {
					result.clip.samples[index] = static_cast<float>(clip.samples[index] - means[channel]);
				} else {
					result.clip.samples[index] = static_cast<float>(clip.samples[index] * scale);
				}
			}
		}
	}
	if (op == QStringLiteral("reverse")) {
		result.clip.markers = reverseAudioMarkers(clip.markers, edit.firstFrame, end);
	}
	if (!cancelled()) {
		result.error = validateAudioClip(result.clip, true);
	}
	return result;
}

AudioClipResult createAudioClip(int sampleRate, int channels, qint64 frames, const AudioWorkControl& control)
{
	AudioClipResult result;
	result.clip = {channels, sampleRate, {}};
	result.error = validateAudioClip(result.clip, true);
	if (!result.error.isEmpty()) {
		return result;
	}
	if (frames < 0 || frames > AudioSampleLimit / channels) {
		result.error =
		    QCoreApplication::translate("VibeStudioAudio", "The requested sound length exceeds the sample limit.");
		return result;
	}
	if (control.cancelled && control.cancelled()) {
		result.cancelled = true;
		result.clip = {};
		return result;
	}
	result.clip.samples.fill(0.0f, frames * channels);
	return result;
}

AudioClipResult replaceAudioRange(const AudioClip& clip, qint64 first, qint64 end, const AudioClip& insertion,
                                  const AudioWorkControl& control)
{
	AudioClipResult result;
	result.error = validateAudioClip(clip, true);
	if (result.error.isEmpty()) {
		result.error = validateAudioClip(insertion, true);
	}
	if (!result.error.isEmpty()) {
		return result;
	}
	if (clip.channels != insertion.channels || clip.sampleRate != insertion.sampleRate) {
		result.error = QCoreApplication::translate(
		    "VibeStudioAudio",
		    "Pasted audio must have the same sample rate and channel count. Convert it before pasting.");
		return result;
	}
	if (first < 0 || end < first || end > clip.frameCount()) {
		result.error = QCoreApplication::translate("VibeStudioAudio", "The paste range is outside the sound.");
		return result;
	}
	const qint64 frames = clip.frameCount() - (end - first) + insertion.frameCount();
	if (frames > AudioSampleLimit / clip.channels) {
		result.error = QCoreApplication::translate("VibeStudioAudio", "Pasting would exceed the sample limit.");
		return result;
	}
	result.clip = {clip.channels, clip.sampleRate, {}};
	result.clip.markers = mergeAudioMarkers(replaceAudioMarkers(clip.markers, first, end, insertion.frameCount()),
	                                        insertion.markers, first);
	result.error = validateAudioMarkers(result.clip.markers, frames);
	if (!result.error.isEmpty()) {
		return result;
	}
	result.clip.samples.resize(frames * clip.channels);
	const qint64 insertEnd = first + insertion.frameCount();
	for (qint64 frame = 0; frame < frames; ++frame) {
		if (frame % 4096 == 0 && control.cancelled && control.cancelled()) {
			result.cancelled = true;
			result.clip = {};
			return result;
		}
		for (int channel = 0; channel < clip.channels; ++channel) {
			result.clip.samples[frame * clip.channels + channel] =
			    frame < first       ? clip.samples[frame * clip.channels + channel]
			    : frame < insertEnd ? insertion.samples[(frame - first) * clip.channels + channel]
			                        : clip.samples[(frame - insertEnd + end) * clip.channels + channel];
		}
	}
	if (control.cancelled && control.cancelled()) {
		result.cancelled = true;
		result.clip = {};
	}
	return result;
}

AudioClipResult mixAudioClip(const AudioClip& clip, qint64 first, const AudioClip& source,
                             const AudioWorkControl& control)
{
	AudioClipResult result;
	result.error = validateAudioClip(clip, true);
	if (result.error.isEmpty()) {
		result.error = validateAudioClip(source);
	}
	if (!result.error.isEmpty()) {
		return result;
	}
	if (clip.channels != source.channels || clip.sampleRate != source.sampleRate) {
		result.error = QCoreApplication::translate(
		    "VibeStudioAudio",
		    "Mixed audio must have the same sample rate and channel count. Convert it before mixing.");
		return result;
	}
	if (first < 0 || first > clip.frameCount() || first + source.frameCount() > AudioSampleLimit / clip.channels) {
		result.error = QCoreApplication::translate("VibeStudioAudio",
		                                           "The mix position or resulting length is outside the sound limits.");
		return result;
	}
	result.clip = clip;
	result.clip.samples.resize(std::max(clip.frameCount(), first + source.frameCount()) * clip.channels);
	result.clip.markers = mergeAudioMarkers(clip.markers, source.markers, first);
	for (qint64 frame = 0; frame < source.frameCount(); ++frame) {
		if (frame % 4096 == 0 && control.cancelled && control.cancelled()) {
			result.cancelled = true;
			result.clip = {};
			return result;
		}
		for (int channel = 0; channel < clip.channels; ++channel) {
			const qint64 at = (first + frame) * clip.channels + channel;
			result.clip.samples[at] = static_cast<float>(static_cast<double>(result.clip.samples[at]) +
			                                             source.samples[frame * clip.channels + channel]);
		}
	}
	result.error = validateAudioClip(result.clip, true);
	return result;
}

AudioClipResult insertAudioSilence(const AudioClip& clip, qint64 first, qint64 frames, const AudioWorkControl& control)
{
	auto silence = createAudioClip(clip.sampleRate, clip.channels, frames, control);
	if (!silence.succeeded()) {
		return silence;
	}
	return replaceAudioRange(clip, first, first, silence.clip, control);
}

AssetAudioPeaks audioClipPeaks(const AudioClip& clip, int buckets)
{
	AssetAudioPeaks peaks;
	peaks.error = validateAudioClip(clip, true);
	if (!peaks.error.isEmpty()) {
		return peaks;
	}
	peaks.channels = clip.channels;
	peaks.sampleRate = clip.sampleRate;
	peaks.bitsPerSample = 32;
	peaks.frameCount = clip.frameCount();
	peaks.durationMs = clip.durationMs();
	peaks.bucketCount = static_cast<int>(std::min<qint64>(std::clamp(buckets, 1, 65536), clip.frameCount()));
	peaks.peaks.resize(peaks.bucketCount * clip.channels * 2);
	for (int channel = 0; channel < clip.channels; ++channel) {
		for (int bucket = 0; bucket < peaks.bucketCount; ++bucket) {
			float low = clip.samples[(clip.frameCount() * bucket / peaks.bucketCount) * clip.channels + channel];
			float high = low;
			for (qint64 frame = clip.frameCount() * bucket / peaks.bucketCount;
			     frame < clip.frameCount() * (bucket + 1) / peaks.bucketCount; ++frame) {
				const float value = clip.samples[frame * clip.channels + channel];
				low = std::min(low, value);
				high = std::max(high, value);
			}
			const int index = (channel * peaks.bucketCount + bucket) * 2;
			peaks.peaks[index] = low;
			peaks.peaks[index + 1] = high;
		}
	}
	peaks.valid = true;
	return peaks;
}

QByteArray encodeAudioWav(const AudioClip& clip, QString* error)
{
	return encodeAudioWav(clip, AudioWavOptions{}, error);
}

bool saveAudioWav(const AudioClip& clip, const QString& path, bool overwrite, const QString& protectedPath,
                  QString* error, bool dryRun)
{
	return saveAudioWav(clip, AudioWavOptions{}, path, overwrite, protectedPath, error, dryRun);
}

bool stageAudioWav(const QByteArray& wav, const QString& virtualPath, PackageStagingModel* staging, bool replace,
                   QString* error)
{
	if (!staging || !staging->isLoaded() || staging->sourceFormat() == PackageArchiveFormat::Wad) {
		return fail(error, QCoreApplication::translate("VibeStudioAudio",
		                                               "Open a folder, PAK, ZIP, or PK3 package to stage a "
		                                               "WAV sound. Doom WAD output requires DMX encoding."));
	}
	if (QFileInfo(virtualPath).suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) != 0) {
		return fail(error, QCoreApplication::translate("VibeStudioAudio", "The package sound path must end in .wav."));
	}
	// Fixed canonical headers can be checked without scanning samples on the UI thread.
	if (!isGeneratedIntegerAudioWav(wav)) {
		return fail(error, QCoreApplication::translate("VibeStudioAudio",
		                                               "The generated integer PCM WAV could not be validated."));
	}
	// Apply to a value copy so path/collision failures cannot alter the live plan.
	PackageStagingModel next = *staging;
	if (!next.addBytes(wav, virtualPath, error,
	                   replace ? PackageStageConflictResolution::ReplaceExisting
	                           : PackageStageConflictResolution::Block)) {
		return false;
	}
	if (next.summary().blockingCount > staging->summary().blockingCount) {
		return fail(error,
		            QCoreApplication::translate(
		                "VibeStudioAudio",
		                "This path conflicts with the package. Enable Replace existing entry or choose another path."));
	}
	*staging = std::move(next);
	return true;
}

} // namespace vibestudio
