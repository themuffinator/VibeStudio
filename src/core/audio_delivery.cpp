#include "core/audio_delivery.h"

#include "core/audio_resample.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <cmath>

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

QString audioDeliveryPresetId(AudioDeliveryPreset preset)
{
	switch (preset) {
	case AudioDeliveryPreset::Wav:
		return QStringLiteral("wav");
	case AudioDeliveryPreset::Doom:
		return QStringLiteral("doom");
	case AudioDeliveryPreset::Quake:
		return QStringLiteral("quake");
	case AudioDeliveryPreset::Quake2:
		return QStringLiteral("quake2");
	case AudioDeliveryPreset::Quake3:
		return QStringLiteral("quake3");
	}
	return {};
}

QString audioDeliveryPresetLabel(AudioDeliveryPreset preset)
{
	switch (preset) {
	case AudioDeliveryPreset::Wav:
		return QCoreApplication::translate("VibeStudioAudio", "WAV (custom precision)");
	case AudioDeliveryPreset::Doom:
		return QCoreApplication::translate("VibeStudioAudio", "Doom sound effect");
	case AudioDeliveryPreset::Quake:
		return QCoreApplication::translate("VibeStudioAudio", "Quake sound effect");
	case AudioDeliveryPreset::Quake2:
		return QCoreApplication::translate("VibeStudioAudio", "Quake II sound effect");
	case AudioDeliveryPreset::Quake3:
		return QCoreApplication::translate("VibeStudioAudio", "Quake III sound effect");
	}
	return {};
}

bool parseAudioDeliveryPreset(const QString& id, AudioDeliveryPreset* preset)
{
	for (const auto value : {AudioDeliveryPreset::Wav, AudioDeliveryPreset::Doom, AudioDeliveryPreset::Quake,
	                         AudioDeliveryPreset::Quake2, AudioDeliveryPreset::Quake3}) {
		if (id == audioDeliveryPresetId(value)) {
			if (preset) {
				*preset = value;
			}
			return true;
		}
	}
	return false;
}

AudioDeliveryPlan planAudioDelivery(const AudioClip& clip, const AudioDeliveryOptions& options)
{
	AudioDeliveryPlan plan;
	if (clip.channels < 1 || clip.channels > 8 || clip.sampleRate < 1 || clip.sampleRate > 384000 ||
	    clip.samples.isEmpty() || clip.samples.size() > AudioSampleLimit || clip.samples.size() % clip.channels ||
	    audioDeliveryPresetId(options.preset).isEmpty() || audioWavBits(options.wav.format) == 0) {
		plan.error = QCoreApplication::translate(
		    "VibeStudioAudio", "Choose a valid delivery preset and a sound containing complete frames.");
		return plan;
	}
	plan.error = validateAudioMarkers(clip.markers, clip.frameCount());
	if (!plan.error.isEmpty()) {
		return plan;
	}
	plan.wav = options.wav;
	plan.dmx = options.preset == AudioDeliveryPreset::Doom;
	plan.channels = options.preset == AudioDeliveryPreset::Wav ? clip.channels : 1;
	// Original engine SFX readers require mono PCM. The Quake III reader warns
	// for rates other than 22050 and for 8-bit samples. Presets are conservative
	// delivery choices, not claims about every port or music path. Behaviour
	// references (GPL-2.0-or-later), reviewed 2026-10-04: id Software's released
	// Quake/WinQuake, Quake-2/client, and Quake-III-Arena/code/client snd_mem.c.
	// No upstream code is copied; exact links and licences are in docs/CREDITS.md.
	switch (options.preset) {
	case AudioDeliveryPreset::Wav:
		plan.sampleRate = clip.sampleRate;
		break;
	case AudioDeliveryPreset::Doom:
	case AudioDeliveryPreset::Quake:
		plan.sampleRate = 11025;
		plan.wav.format = AudioWavFormat::Pcm8;
		plan.wav.markers = plan.dmx ? AudioWavMarkers::Omit : AudioWavMarkers::Quake;
		break;
	case AudioDeliveryPreset::Quake2:
	case AudioDeliveryPreset::Quake3:
		plan.sampleRate = 22050;
		plan.wav.format = AudioWavFormat::Pcm16;
		plan.wav.markers =
		    options.preset == AudioDeliveryPreset::Quake2 ? AudioWavMarkers::Quake : AudioWavMarkers::Standard;
		break;
	}
	plan.frames = std::max<qint64>(1, audioFrameAtSampleRate(clip.frameCount(), clip.sampleRate, plan.sampleRate));
	if (plan.frames > AudioSampleLimit / plan.channels) {
		plan.error = QCoreApplication::translate("VibeStudioAudio", "Delivery conversion exceeds the sample limit.");
	} else if (plan.dmx && plan.frames < 17) {
		plan.error = QCoreApplication::translate("VibeStudioAudio",
		                                         "Doom delivery needs at least 17 playable frames after resampling.");
	} else if (plan.wav.format == AudioWavFormat::Float32 && plan.wav.dither) {
		plan.error = QCoreApplication::translate("VibeStudioAudio", "Float WAV output does not use dither.");
	}
	const auto mapped =
	    resampleAudioMarkers(clip.markers, clip.frameCount(), plan.frames, clip.sampleRate, plan.sampleRate);
	const bool omit =
	    plan.wav.markers == AudioWavMarkers::Omit || (plan.wav.markers == AudioWavMarkers::Quake && !mapped.loop);
	plan.outputCues = omit ? 0 : int(mapped.cues.size());
	plan.outputLoop = !omit && mapped.loop.has_value();
	if (!clip.markers.empty()) {
		plan.markerSummary = QCoreApplication::translate("VibeStudioAudio", "Cue markers: %1 → %2. Forward loop: %3.")
		                         .arg(clip.markers.cues.size())
		                         .arg(plan.outputCues)
		                         .arg(plan.outputLoop ? QCoreApplication::translate("VibeStudioAudio", "included")
		                                              : QCoreApplication::translate("VibeStudioAudio", "not included"));
		if (plan.dmx) {
			plan.markerSummary +=
			    QLatin1Char(' ') + QCoreApplication::translate("VibeStudioAudio", "DMX has no marker metadata.");
		} else if (plan.wav.markers == AudioWavMarkers::Quake) {
			plan.markerSummary +=
			    QLatin1Char(' ') +
			    QCoreApplication::translate("VibeStudioAudio",
			                                "Quake/II cues require a loop; the engine plays only through its end.");
		} else if (options.preset == AudioDeliveryPreset::Quake3) {
			plan.markerSummary += QLatin1Char(' ') +
			                      QCoreApplication::translate("VibeStudioAudio",
			                                                  "Original Quake III does not use embedded loop markers.");
		}
	}
	return plan;
}

QString audioDeliveryDescription(const AudioDeliveryPlan& plan)
{
	if (!plan.error.isEmpty()) {
		return plan.error;
	}
	return QCoreApplication::translate("VibeStudioAudio", "%1 Hz · %2 channels · %3 · %4 frames")
	           .arg(plan.sampleRate)
	           .arg(plan.channels)
	           .arg(plan.dmx ? QStringLiteral("DMX PCM8") : audioWavFormatId(plan.wav.format))
	           .arg(plan.frames) +
	       (plan.markerSummary.isEmpty() ? QString() : QLatin1Char('\n') + plan.markerSummary);
}

AudioDeliveryResult renderAudioDelivery(const AudioClip& clip, const AudioDeliveryOptions& options,
                                        const AudioWorkControl& control)
{
	AudioDeliveryResult result;
	result.plan = planAudioDelivery(clip, options);
	result.error = result.plan.error;
	if (!result.error.isEmpty()) {
		return result;
	}
	const auto cancelled = [&]() {
		if (control.cancelled && control.cancelled()) {
			result.cancelled = true;
			result.bytes.clear();
			return true;
		}
		return false;
	};
	if (cancelled()) {
		return result;
	}
	result.error = validateAudioClip(clip);
	if (!result.error.isEmpty()) {
		return result;
	}
	AudioClip converted = clip;
	if (converted.channels != result.plan.channels) {
		auto mono = applyAudioEdit(converted, {QStringLiteral("mono")}, control);
		if (!mono.succeeded()) {
			result.error = mono.error;
			result.cancelled = mono.cancelled;
			return result;
		}
		converted = std::move(mono.clip);
	}
	if (converted.sampleRate != result.plan.sampleRate) {
		auto resampled = resampleAudioClip(converted, result.plan.sampleRate, control);
		if (!resampled.succeeded()) {
			result.error = resampled.error;
			result.cancelled = resampled.cancelled;
			return result;
		}
		converted = std::move(resampled.clip);
	}
	for (qsizetype index = 0; index < converted.samples.size(); ++index) {
		if ((index & 4095) == 0 && cancelled()) {
			return result;
		}
		const double magnitude = std::abs(double(converted.samples[index]));
		result.peak = std::max(result.peak, magnitude);
		result.samplesAboveFullScale += magnitude > 1.0;
	}
	result.bytes = encodeAudioWav(converted, result.plan.wav, &result.error, control);
	if (cancelled() || !result.error.isEmpty()) {
		return result;
	}
	if (result.plan.dmx) {
		// DMX format 3: LE u16 format/rate, u32 padded sample count, and PCM8.
		// Chocolate Doom 3.1.0 CacheSFX in src/i_sdlsound.c (GPL-2.0-or-later)
		// skips 16 bytes at each end and rejects counts <= 48. Behaviour only;
		// no code copied. Padding repeats the adjacent quantized endpoint.
		const qsizetype frames = converted.frameCount();
		QByteArray dmx(frames + 40, '\0');
		qToLittleEndian<quint16>(3, dmx.data());
		qToLittleEndian<quint16>(quint16(converted.sampleRate), dmx.data() + 2);
		qToLittleEndian<quint32>(quint32(frames + 32), dmx.data() + 4);
		std::copy_n(result.bytes.constData() + 44, frames, dmx.data() + 24);
		std::fill_n(dmx.data() + 8, 16, dmx[24]);
		std::fill_n(dmx.data() + 24 + frames, 16, dmx[23 + frames]);
		result.bytes = std::move(dmx);
	}
	cancelled();
	return result;
}

bool writeAudioDelivery(const AudioDeliveryResult& result, const QString& path, bool overwrite,
                        const QStringList& protectedPaths, QString* error, bool dryRun)
{
	if (!result.succeeded()) {
		return fail(error, result.error.isEmpty()
		                       ? QCoreApplication::translate("VibeStudioAudio",
		                                                     "Prepare a complete audio delivery before saving.")
		                       : result.error);
	}
	return writeAudioExportBytes(result.bytes, path,
	                             result.plan.dmx ? QStringList{QStringLiteral("dmx"), QStringLiteral("lmp")}
	                                             : QStringList{QStringLiteral("wav")},
	                             overwrite, protectedPaths, error, dryRun);
}

bool isGeneratedAudioDmx(const QByteArray& bytes)
{
	if (bytes.size() < 57 || bytes.size() > AudioSampleLimit + 40 ||
	    qFromLittleEndian<quint16>(bytes.constData()) != 3 || qFromLittleEndian<quint16>(bytes.constData() + 2) == 0 ||
	    qFromLittleEndian<quint32>(bytes.constData() + 4) != bytes.size() - 8) {
		return false;
	}
	for (int index = 0; index < 16; ++index) {
		if (bytes[8 + index] != bytes[24] || bytes[bytes.size() - 16 + index] != bytes[bytes.size() - 17]) {
			return false;
		}
	}
	return true;
}

bool stageAudioDelivery(const QByteArray& bytes, const QString& virtualPath, PackageStagingModel* staging, bool replace,
                        QString* error)
{
	if (!isGeneratedAudioDmx(bytes)) {
		return stageAudioWav(bytes, virtualPath, staging, replace, error);
	}
	if (!staging || !staging->isLoaded() || staging->sourceFormat() != PackageArchiveFormat::Wad ||
	    (staging->sourceWadMagic() != QStringLiteral("PWAD") && staging->sourceWadMagic() != QStringLiteral("IWAD"))) {
		return fail(error,
		            QCoreApplication::translate("VibeStudioAudio", "Open a Doom IWAD or PWAD to stage a DMX sound."));
	}
	static const QRegularExpression soundName(QStringLiteral("^DS[A-Z0-9_]{1,6}$"));
	if (!soundName.match(virtualPath).hasMatch()) {
		return fail(error, QCoreApplication::translate("VibeStudioAudio",
		                                               "Use a Doom sound lump name: DS followed by 1–6 uppercase "
		                                               "letters, digits, or underscores; no extension."));
	}
	PackageStagingModel next = *staging;
	if (!next.addBytes(bytes, virtualPath, error,
	                   replace ? PackageStageConflictResolution::ReplaceExisting
	                           : PackageStageConflictResolution::Block)) {
		return false;
	}
	if (next.summary().blockingCount > staging->summary().blockingCount) {
		return fail(error, QCoreApplication::translate(
		                       "VibeStudioAudio",
		                       "The sound conflicts with the package. Review the name and replacement option."));
	}
	*staging = std::move(next);
	return true;
}

} // namespace vibestudio
