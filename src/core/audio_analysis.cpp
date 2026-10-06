#include "core/audio_analysis.h"

// The existing r8brain-free-src 7.5 converter provides double-precision peak
// reconstruction. Preserve the same cache configuration as audio_resample.cpp.
// Upstream MIT/Ooura notices: external/audio/r8brain-free-src/VIBESTUDIO.md.
#define R8B_FILTER_CACHE_MAX 8
#define R8B_FRACBANK_CACHE_MAX 2
#include <CDSPResampler.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>
#include <ebur128.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <limits>

namespace vibestudio
{
namespace
{
constexpr std::array<const char*, 10> RoleIds{"L", "R", "C", "LFE", "Ls", "Rs", "Lb", "Rb", "Cb", "X"};
const std::array<const char*, 10> RoleNames{
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Left"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Right"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Center / mono"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Low-frequency effects"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Left surround"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Right surround"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Left back"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Right back"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Center back"),
	QT_TRANSLATE_NOOP("VibeStudioAudioAnalysis", "Excluded from loudness")};
constexpr std::array<int, 10> MeterRoles{EBUR128_LEFT, EBUR128_RIGHT, EBUR128_CENTER, EBUR128_UNUSED,
	EBUR128_LEFT_SURROUND, EBUR128_RIGHT_SURROUND, EBUR128_Mp135, EBUR128_Mm135, EBUR128_Mp180, EBUR128_UNUSED};

// libebur128 1.2.6 rewrites process-wide constants in init, including while
// other states read them. All calls, not only construction, share this lock.
// Upstream/notice record: external/audio/libebur128/VIBESTUDIO.md.
std::mutex& meterMutex() { static std::mutex mutex; return mutex; }
template<typename F> auto meterCall(F&& call)
{
	const std::lock_guard lock(meterMutex());
	return call();
}
struct MeterDeleter {
	void operator()(ebur128_state* state) const { meterCall([&]() { ebur128_destroy(&state); }); }
};
using Meter = std::unique_ptr<ebur128_state, MeterDeleter>;

bool measureTruePeak(const AudioClip& clip, AudioAnalysisResult& result, const AudioWorkControl& control)
{
	auto& analysis = result.analysis;
	analysis.truePeak = analysis.peak;
	for (auto& channel : analysis.channels) { channel.truePeak = channel.peak; }
	if (analysis.truePeakOversampling == 1) { return true; }
	const auto fail = [&](const QString& error) { result.error = error; result.analysis = {}; return false; };
	try {
		constexpr int ChunkFrames = 2048;
		const int factor = analysis.truePeakOversampling;
		r8b::CDSPResampler24 converter(clip.sampleRate, clip.sampleRate * factor, ChunkFrames, 2.0);
		// Cleared resamplers compensate their latency by omitting initial output.
		// Explicit silence on both sides retains the complete pre/post ringing.
		const qint64 padding = converter.getInputRequiredForOutput(1);
		const qint64 outputFrames = (analysis.endFrame - analysis.firstFrame + 2 * padding) * factor;
		if (padding < 1 || padding > 32768 || outputFrames > std::numeric_limits<int>::max()) {
			return fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "True-peak interpolation exceeds the processing limit."));
		}
		const qint64 requiredInput = converter.getInputRequiredForOutput(int(outputFrames));
		if (requiredInput <= 0 || requiredInput > AudioSampleLimit + 4 * 32768) {
			return fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "True-peak interpolation exceeds the processing limit."));
		}
		std::array<double, ChunkFrames> input{};
		for (int channel = 0; channel < clip.channels; ++channel) {
			if (analysis.channels[channel].peak == 0) { continue; }
			converter.clear();
			qint64 inputPosition = 0, outputPosition = 0;
			while (outputPosition < outputFrames) {
				if (control.cancelled && control.cancelled()) {
					result.cancelled = true; result.analysis = {}; return false;
				}
				const int count = int(std::min<qint64>(ChunkFrames, requiredInput - inputPosition));
				if (count <= 0) {
					return fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "Unable to finish true-peak measurement."));
				}
				for (int index = 0; index < count; ++index) {
					const qint64 frame = analysis.firstFrame + inputPosition + index - padding;
					input[size_t(index)] = frame >= analysis.firstFrame && frame < analysis.endFrame
					    ? double(clip.samples[frame * clip.channels + channel]) : 0;
				}
				double* converted = nullptr;
				const int available = converter.process(input.data(), count, converted);
				const qint64 take = std::min<qint64>(available, outputFrames - outputPosition);
				for (qint64 index = 0; index < take; ++index) {
					const double value = std::abs(converted[index]);
					if (!std::isfinite(value)) {
						return fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "The audio meter returned an invalid true-peak result."));
					}
					analysis.channels[channel].truePeak = std::max(*analysis.channels[channel].truePeak, value);
				}
				inputPosition += count; outputPosition += take;
			}
			analysis.truePeak = std::max(*analysis.truePeak, *analysis.channels[channel].truePeak);
		}
	} catch (const std::bad_alloc&) {
		return fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "Unable to allocate the audio meter. The sound is unchanged."));
	}
	return true;
}

void measureProgramme(const AudioClip& clip, AudioAnalysisResult& result,
                      const AudioAnalysisOptions& options, const AudioWorkControl& control)
{
	auto& analysis = result.analysis;
	const auto fail = [&](const QString& error) { result.error = error; result.analysis = {}; };
	const auto cancelled = [&]() {
		if (control.cancelled && control.cancelled()) {
			result.cancelled = true; result.analysis = {}; return true;
		}
		return false;
	};
	analysis.channelMap = options.channelMap;
	if (analysis.channelMap.isEmpty()) {
		if (clip.channels == 1) { analysis.channelMap = {AudioChannelRole::Center}; }
		if (clip.channels == 2) { analysis.channelMap = {AudioChannelRole::Left, AudioChannelRole::Right}; }
	} else {
		QSet<int> used;
		bool valid = analysis.channelMap.size() == clip.channels;
		for (const auto role : analysis.channelMap) {
			const int index = static_cast<int>(role);
			valid &= index >= 0 && index < int(RoleIds.size());
			if (role != AudioChannelRole::Unused && role != AudioChannelRole::LowFrequency) {
				valid &= !used.contains(index); used.insert(index);
			}
		}
		if (!valid) {
			fail(QCoreApplication::translate("VibeStudioAudioAnalysis",
			    "Loudness needs one valid role per channel, without repeated speaker positions."));
			return;
		}
	}
	analysis.loudnessBlockFrames = 4 * ((clip.sampleRate + 5) / 10);
	if (clip.sampleRate < 8000) {
		analysis.truePeakStatus = QStringLiteral("unsupported-sample-rate");
		analysis.truePeakMessage = QCoreApplication::translate(
		    "VibeStudioAudioAnalysis", "True peak and loudness require a sample rate of at least 8000 Hz. Sample statistics remain available.");
		analysis.loudnessStatus = options.measureLoudness ? analysis.truePeakStatus : QStringLiteral("disabled");
		analysis.loudnessMessage = options.measureLoudness ? analysis.truePeakMessage : QCoreApplication::translate(
		    "VibeStudioAudioAnalysis", "Integrated loudness was not requested.");
		return;
	}
	analysis.truePeakOversampling = clip.sampleRate < 96000 ? 8 : clip.sampleRate < 192000 ? 4 : clip.sampleRate < 384000 ? 2 : 1;
	const qint64 frames = analysis.endFrame - analysis.firstFrame;
	if (!options.measureLoudness) {
		analysis.loudnessStatus = QStringLiteral("disabled");
		analysis.loudnessMessage = QCoreApplication::translate("VibeStudioAudioAnalysis", "Integrated loudness was not requested.");
	} else if (analysis.channelMap.isEmpty()) {
		analysis.loudnessStatus = QStringLiteral("channel-map-required");
		analysis.loudnessMessage = QCoreApplication::translate("VibeStudioAudioAnalysis",
		    "Choose speaker roles to measure surround loudness; the document does not retain a speaker layout.");
	} else if (frames < analysis.loudnessBlockFrames) {
		analysis.loudnessStatus = QStringLiteral("insufficient-duration");
		analysis.loudnessMessage = QCoreApplication::translate("VibeStudioAudioAnalysis",
		    "Integrated loudness needs a complete metering block: %1 frames (about 400 ms). The selected range is shorter.").arg(analysis.loudnessBlockFrames);
	} else {
		analysis.loudnessStatus = QStringLiteral("measured");
	}
	if (!frames) {
		analysis.truePeakStatus = QStringLiteral("empty-range");
		analysis.truePeakMessage = QCoreApplication::translate("VibeStudioAudioAnalysis", "There are no samples in this range.");
		return;
	}
	analysis.truePeakStatus = QStringLiteral("measured");
	const auto belowGate = [&]() {
		analysis.loudnessStatus = QStringLiteral("below-gate");
		analysis.loudnessMessage = QCoreApplication::translate("VibeStudioAudioAnalysis",
		    "No complete loudness block exceeds the absolute gate of -70 LUFS.");
	};
	if (analysis.peak == 0) {
		analysis.truePeak = 0;
		for (auto& channel : analysis.channels) { channel.truePeak = 0; }
		if (analysis.loudnessStatus == QStringLiteral("measured")) { belowGate(); }
		return;
	}
	if (cancelled()) { return; }
	if (!measureTruePeak(clip, result, control) || cancelled()) { return; }
	if (analysis.loudnessStatus != QStringLiteral("measured")) { return; }
	Meter loudness(meterCall([&]() { return ebur128_init(unsigned(clip.channels), unsigned(clip.sampleRate), EBUR128_MODE_I); }));
	if (!loudness) {
		fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "Unable to allocate the audio meter. The sound is unchanged."));
		return;
	}
	for (int channel = 0; channel < clip.channels; ++channel) {
		const int code = meterCall([&]() {
			return ebur128_set_channel(loudness.get(), unsigned(channel), MeterRoles[size_t(analysis.channelMap[channel])]);
		});
		if (code != EBUR128_SUCCESS) {
			fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "The audio meter rejected the channel map."));
			return;
		}
	}
	// Original samples preserve the absolute loudness gate. True-peak padding
	// is deliberately absent from the programme's duration and block boundaries.
	constexpr qint64 ChunkFrames = 2048;
	for (qint64 first = analysis.firstFrame; first < analysis.endFrame; first += ChunkFrames) {
		if (cancelled()) { return; }
		const auto count = std::min(ChunkFrames, analysis.endFrame - first);
		const int status = meterCall([&]() {
			return ebur128_add_frames_float(loudness.get(), clip.samples.constData() + first * clip.channels, size_t(count));
		});
		if (status != EBUR128_SUCCESS) {
			fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "Audio metering failed while processing the selected range."));
			return;
		}
	}
	if (cancelled()) { return; }
	if (loudness) {
		double value = 0, threshold = 0;
		const int status = meterCall([&]() {
			const int code = ebur128_loudness_global(loudness.get(), &value);
			return code == EBUR128_SUCCESS ? ebur128_relative_threshold(loudness.get(), &threshold) : code;
		});
		if (status != EBUR128_SUCCESS || std::isnan(value) || value == std::numeric_limits<double>::infinity()) {
			fail(QCoreApplication::translate("VibeStudioAudioAnalysis", "The audio meter returned an invalid loudness result.")); return;
		}
		if (std::isfinite(value)) {
			analysis.integratedLufs = value;
			if (std::isfinite(threshold)) { analysis.relativeThresholdLufs = threshold; }
		} else { belowGate(); }
	}
	cancelled();
}

// Compensated sums retain small contributions in long, high-headroom clips.
// Squaring any finite float and summing the bounded input fits in a double.
struct Sum {
	double value = 0, correction = 0;
	void add(double term)
	{
		const double corrected = term - correction;
		const double next = value + corrected;
		correction = (next - value) - corrected;
		value = next;
	}
};

QJsonValue decibels(double level)
{
	return level > 0 ? QJsonValue(20.0 * std::log10(level)) : QJsonValue(QJsonValue::Null);
}

QJsonValue position(qint64 frame) { return frame >= 0 ? QJsonValue(frame) : QJsonValue(QJsonValue::Null); }
QJsonValue optionalNumber(const std::optional<double>& value) { return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null); }
QJsonValue optionalDecibels(const std::optional<double>& value) { return value ? decibels(*value) : QJsonValue(QJsonValue::Null); }
} // namespace

QString audioChannelRoleId(AudioChannelRole role)
{
	const int index = static_cast<int>(role);
	return index >= 0 && index < int(RoleIds.size()) ? QString::fromLatin1(RoleIds[size_t(index)]) : QString();
}
QString audioChannelRoleName(AudioChannelRole role)
{
	const int index = static_cast<int>(role);
	return index >= 0 && index < int(RoleNames.size()) ? QCoreApplication::translate("VibeStudioAudioAnalysis", RoleNames[size_t(index)]) : QString();
}
bool parseAudioChannelRole(const QString& id, AudioChannelRole* role)
{
	for (size_t index = 0; index < RoleIds.size(); ++index) {
		if (id.trimmed().compare(QString::fromLatin1(RoleIds[index]), Qt::CaseInsensitive) == 0) {
			if (role) { *role = static_cast<AudioChannelRole>(index); }
			return true;
		}
	}
	return false;
}

AudioAnalysisResult analyzeAudioClip(const AudioClip& clip, qint64 firstFrame, qint64 endFrame,
                                     const AudioWorkControl& control, const AudioAnalysisOptions& options)
{
	AudioAnalysisResult result;
	const auto cancelled = [&]() {
		if (control.cancelled && control.cancelled()) {
			result.cancelled = true;
			result.analysis = {};
			return true;
		}
		return false;
	};
	if (cancelled()) {
		return result;
	}
	if (clip.channels < 1 || clip.channels > 8 || clip.sampleRate < 1 || clip.sampleRate > 384000 ||
	    clip.samples.size() > AudioSampleLimit || clip.samples.size() % clip.channels != 0) {
		result.error = QCoreApplication::translate(
		    "VibeStudioAudioAnalysis",
		    "Analysis requires complete audio frames within the editor's format and sample limits.");
		return result;
	}
	if (endFrame == -1) {
		endFrame = clip.frameCount();
	}
	if (firstFrame < 0 || endFrame < firstFrame || endFrame > clip.frameCount()) {
		result.error = QCoreApplication::translate(
		    "VibeStudioAudioAnalysis",
		    "The analysis range must be inside the sound, with its end at or after its start.");
		return result;
	}
	// Validate the whole document even when only a selection is being measured.
	for (qsizetype sample = 0; sample < clip.samples.size(); ++sample) {
		if (sample % 4096 == 0 && cancelled()) {
			return result;
		}
		if (!std::isfinite(clip.samples[sample])) {
			result.error =
			    QCoreApplication::translate("VibeStudioAudioAnalysis", "Audio contains a non-finite sample.");
			return result;
		}
	}
	auto& analysis = result.analysis;
	analysis.sampleRate = clip.sampleRate;
	analysis.firstFrame = firstFrame;
	analysis.endFrame = endFrame;
	analysis.channels.resize(clip.channels);
	std::array<Sum, 8> sums{}, squares{};
	std::array<qint64, 8> runs{};
	for (qint64 frame = firstFrame; frame < endFrame; ++frame) {
		if ((frame - firstFrame) % 4096 == 0 && cancelled()) {
			return result;
		}
		for (int channel = 0; channel < clip.channels; ++channel) {
			const double value = clip.samples[frame * clip.channels + channel];
			const double absolute = std::abs(value);
			auto& stats = analysis.channels[channel];
			if (frame == firstFrame) {
				stats.minimum = stats.maximum = value;
			}
			stats.minimum = std::min(stats.minimum, value);
			stats.maximum = std::max(stats.maximum, value);
			if (stats.peakFrame < 0 || absolute > stats.peak) {
				stats.peak = absolute;
				stats.peakFrame = frame;
			}
			sums[channel].add(value);
			squares[channel].add(value * value);
			if (absolute > 1) {
				++stats.samplesAboveFullScale;
				++runs[channel];
				stats.longestAboveFullScaleRun = std::max(stats.longestAboveFullScaleRun, runs[channel]);
				if (stats.firstAboveFullScaleFrame < 0) {
					stats.firstAboveFullScaleFrame = frame;
				}
			} else {
				runs[channel] = 0;
				if (absolute == 1) {
					++stats.samplesAtFullScale;
				}
			}
		}
	}
	Sum allSquares;
	const qint64 frames = endFrame - firstFrame;
	for (int channel = 0; channel < clip.channels; ++channel) {
		auto& stats = analysis.channels[channel];
		if (frames) {
			stats.dc = sums[channel].value / frames;
			stats.rms = std::sqrt(std::max(0.0, squares[channel].value / frames));
		}
		analysis.peak = std::max(analysis.peak, stats.peak);
		analysis.samplesAboveFullScale += stats.samplesAboveFullScale;
		allSquares.add(squares[channel].value);
	}
	if (frames) {
		analysis.rms = std::sqrt(std::max(0.0, allSquares.value / (frames * clip.channels)));
	}
	if (!cancelled()) { measureProgramme(clip, result, options, control); }
	return result;
}

QJsonObject audioAnalysisJson(const AudioAnalysis& analysis)
{
	QJsonArray channels;
	for (qsizetype index = 0; index < analysis.channels.size(); ++index) {
		const auto& stats = analysis.channels[index];
		channels.append(
		    QJsonObject{{QStringLiteral("channel"), index + 1},
		                {QStringLiteral("minimum"), stats.minimum},
		                {QStringLiteral("maximum"), stats.maximum},
		                {QStringLiteral("peak"), stats.peak},
		                {QStringLiteral("peakDbfs"), decibels(stats.peak)},
		                {QStringLiteral("truePeak"), optionalNumber(stats.truePeak)},
		                {QStringLiteral("truePeakDbtp"), optionalDecibels(stats.truePeak)},
		                {QStringLiteral("peakFrame"), position(stats.peakFrame)},
		                {QStringLiteral("rms"), stats.rms},
		                {QStringLiteral("rmsDbfs"), decibels(stats.rms)},
		                {QStringLiteral("dc"), stats.dc},
		                {QStringLiteral("dcPercent"), stats.dc * 100},
		                {QStringLiteral("samplesAboveFullScale"), stats.samplesAboveFullScale},
		                {QStringLiteral("samplesAtFullScale"), stats.samplesAtFullScale},
		                {QStringLiteral("firstAboveFullScaleFrame"), position(stats.firstAboveFullScaleFrame)},
		                {QStringLiteral("longestAboveFullScaleRun"), stats.longestAboveFullScaleRun}});
	}
	QJsonArray roles;
	for (const auto role : analysis.channelMap) { roles.append(audioChannelRoleId(role)); }
	return {{QStringLiteral("schemaVersion"), 1},
	        {QStringLiteral("sampleRate"), analysis.sampleRate},
	        {QStringLiteral("channelCount"), analysis.channels.size()},
	        {QStringLiteral("firstFrame"), analysis.firstFrame},
	        {QStringLiteral("endFrame"), analysis.endFrame},
	        {QStringLiteral("frames"), analysis.endFrame - analysis.firstFrame},
	        {QStringLiteral("peak"), analysis.peak},
	        {QStringLiteral("peakDbfs"), decibels(analysis.peak)},
	        {QStringLiteral("rms"), analysis.rms},
	        {QStringLiteral("rmsDbfs"), decibels(analysis.rms)},
	        {QStringLiteral("samplesAboveFullScale"), analysis.samplesAboveFullScale},
	        {QStringLiteral("channels"), channels},
	        {QStringLiteral("truePeak"), optionalNumber(analysis.truePeak)},
	        {QStringLiteral("truePeakDbtp"), optionalDecibels(analysis.truePeak)},
	        {QStringLiteral("truePeakStatus"), analysis.truePeakStatus},
	        {QStringLiteral("truePeakMessage"), analysis.truePeakMessage},
	        {QStringLiteral("truePeakOversampling"), analysis.truePeakOversampling},
	        {QStringLiteral("truePeakMethod"), QStringLiteral("r8brain-free-src-7.5; 2% transition; zero-extended selection; at least sample peak")},
	        {QStringLiteral("loudness"), QJsonObject{
	            {QStringLiteral("method"), QStringLiteral("libebur128-1.2.6; BS.1770 integrated loudness")},
	            {QStringLiteral("status"), analysis.loudnessStatus},
	            {QStringLiteral("message"), analysis.loudnessMessage},
	            {QStringLiteral("integratedLufs"), optionalNumber(analysis.integratedLufs)},
	            {QStringLiteral("relativeThresholdLufs"), optionalNumber(analysis.relativeThresholdLufs)},
	            {QStringLiteral("absoluteGateLufs"), -70},
	            {QStringLiteral("relativeGateLu"), -10},
	            {QStringLiteral("blockFrames"), analysis.loudnessBlockFrames},
	            {QStringLiteral("channelMap"), roles}}},
	        {QStringLiteral("measurement"), QStringLiteral("sample-peak; unweighted RMS including DC")}};
}

} // namespace vibestudio
