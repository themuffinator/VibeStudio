#include "core/audio_export.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <cmath>
#include <random>

namespace vibestudio
{
namespace
{
bool fail(QString *error, const QString &message)
{
	if (error) {
		*error = message;
	}
	return false;
}
} // namespace

QString audioWavFormatId(AudioWavFormat format)
{
	switch (format) {
	case AudioWavFormat::Pcm8:
		return QStringLiteral("pcm8");
	case AudioWavFormat::Pcm16:
		return QStringLiteral("pcm16");
	case AudioWavFormat::Pcm24:
		return QStringLiteral("pcm24");
	case AudioWavFormat::Pcm32:
		return QStringLiteral("pcm32");
	case AudioWavFormat::Float32:
		return QStringLiteral("float32");
	}
	return {};
}

int audioWavBits(AudioWavFormat format)
{
	switch (format) {
	case AudioWavFormat::Pcm8:
		return 8;
	case AudioWavFormat::Pcm16:
		return 16;
	case AudioWavFormat::Pcm24:
		return 24;
	case AudioWavFormat::Pcm32:
	case AudioWavFormat::Float32:
		return 32;
	}
	return 0;
}

bool parseAudioWavFormat(const QString &id, AudioWavFormat *format)
{
	for (const auto value : {AudioWavFormat::Pcm8, AudioWavFormat::Pcm16, AudioWavFormat::Pcm24, AudioWavFormat::Pcm32,
	                         AudioWavFormat::Float32}) {
		if (id == audioWavFormatId(value)) {
			if (format) {
				*format = value;
			}
			return true;
		}
	}
	return false;
}

QByteArray encodeAudioWav(const AudioClip &clip, const AudioWavOptions &options, QString *error,
                          const AudioWorkControl &control)
{
	return AudioWavEncoder(options).encode(clip, error, control);
}

AudioWavEncoder::AudioWavEncoder(const AudioWavOptions &options) : m_options(options), m_random(options.ditherSeed) {}

QByteArray AudioWavEncoder::encode(const AudioClip &clip, QString *error, const AudioWorkControl &control)
{
	const auto &options = m_options;
	if (error) {
		error->clear();
	}
	const auto cancelled = [&]() {
		if (control.cancelled && control.cancelled()) {
			fail(error, QCoreApplication::translate("VibeStudioAudio", "WAV encoding cancelled."));
			return true;
		}
		return false;
	};
	if (cancelled()) {
		return {};
	}
	const QString problem = validateAudioClip(clip);
	if (!problem.isEmpty()) {
		fail(error, problem);
		return {};
	}
	const int bits = audioWavBits(options.format);
	const bool floating = options.format == AudioWavFormat::Float32;
	if (!bits || (floating && options.dither)) {
		fail(error, QCoreApplication::translate(
		                "VibeStudioAudio", "Choose a supported WAV precision. Dither applies only to integer PCM."));
		return {};
	}
	// RIFF/WAVE and WAVEFORMATEXTENSIBLE layouts follow Microsoft's public
	// specifications, reviewed 2026-10-04; links and dates are in docs/CREDITS.md.
	// No speaker positions are inferred for unnamed multichannel documents.
	const bool extensible = clip.channels > 2 || (!floating && bits > 16);
	const int formatSize = extensible ? 40 : floating ? 18 : 16;
	const int dataChunk = 20 + formatSize + (floating ? 12 : 0);
	const int dataOffset = dataChunk + 8;
	const qsizetype dataSize = clip.samples.size() * (bits / 8);
	QByteArray bytes(dataOffset + dataSize + (dataSize & 1), '\0');
	const auto u16 = [&](int offset, quint16 value) { qToLittleEndian(value, bytes.data() + offset); };
	const auto u32 = [&](int offset, quint32 value) { qToLittleEndian(value, bytes.data() + offset); };
	bytes.replace(0, 4, "RIFF");
	bytes.replace(8, 8, "WAVEfmt ");
	u32(4, quint32(bytes.size() - 8));
	u32(16, formatSize);
	u16(20, extensible ? 0xfffe : floating ? 3 : 1);
	u16(22, quint16(clip.channels));
	u32(24, quint32(clip.sampleRate));
	u32(28, quint32(clip.sampleRate * clip.channels * (bits / 8)));
	u16(32, quint16(clip.channels * (bits / 8)));
	u16(34, quint16(bits));
	if (extensible) {
		u16(36, 22);
		u16(38, quint16(bits));
		u32(40, clip.channels == 1 ? 4 : clip.channels == 2 ? 3 : 0);
		bytes.replace(
		    44, 16,
		    QByteArray::fromHex(floating ? "0300000000001000800000aa00389b71" : "0100000000001000800000aa00389b71"));
	}
	if (floating) {
		bytes.replace(20 + formatSize, 4, "fact");
		u32(24 + formatSize, 4);
		u32(28 + formatSize, quint32(clip.frameCount()));
	}
	bytes.replace(dataChunk, 4, "data");
	u32(dataChunk + 4, quint32(dataSize));
	auto random = m_random;
	const double scale = std::ldexp(1.0, bits - 1);
	const qint64 minimum = -(qint64(1) << (bits - 1)), maximum = -minimum - 1;
	char *output = bytes.data() + dataOffset;
	for (qsizetype index = 0; index < clip.samples.size(); ++index) {
		if ((index & 4095) == 0 && cancelled()) {
			return {};
		}
		if (floating) {
			qToLittleEndian(std::bit_cast<quint32>(clip.samples[index]), output + index * 4);
			continue;
		}
		double value = std::clamp(double(clip.samples[index]), -1.0, 1.0) * scale;
		if (options.dither) {
			// Difference of independent uniform [0,1) values: triangular noise
			// in (-1,1) LSB, independent of std::uniform_real_distribution's
			// implementation and reproducible across standard-library vendors.
			const double first = double(random() >> 11) * 0x1.0p-53;
			const double second = double(random() >> 11) * 0x1.0p-53;
			value += first - second;
		}
		const qint64 quantized = std::clamp<qint64>(std::llround(value), minimum, maximum);
		const quint32 word = bits == 8 ? quint32(quantized + 128) : quint32(quantized);
		for (int byte = 0; byte < bits / 8; ++byte) {
			output[index * (bits / 8) + byte] = char((word >> (byte * 8)) & 0xff);
		}
	}
	if (cancelled()) {
		return {};
	}
	QString markerError;
	const QByteArray markers =
	    encodeWavAudioMarkers(clip.markers, clip.frameCount(), clip.sampleRate, options.markers, &markerError);
	if (!markerError.isEmpty()) {
		fail(error, markerError);
		return {};
	}
	bytes += markers;
	u32(4, quint32(bytes.size() - 8));
	m_random = random;
	return bytes;
}

bool saveAudioWav(const AudioClip &clip, const AudioWavOptions &options, const QString &path, bool overwrite,
                  const QString &protectedPath, QString *error, bool dryRun)
{
	const QByteArray bytes = encodeAudioWav(clip, options, error);
	return !bytes.isEmpty() &&
	       writeAudioExportBytes(bytes, path, {QStringLiteral("wav")}, overwrite, {protectedPath}, error, dryRun);
}

bool writeAudioExportBytes(const QByteArray &bytes, const QString &path, const QStringList &suffixes, bool overwrite,
                           const QStringList &protectedPaths, QString *error, bool dryRun)
{
	if (error) {
		error->clear();
	}
	if (bytes.isEmpty() || bytes.size() > AudioInputByteLimit) {
		return fail(error, QCoreApplication::translate("VibeStudioAudio",
		                                               "Encoded audio is empty or exceeds the export limit."));
	}
	const auto checkDestination = [&]() {
		const QFileInfo info(path);
		if (path.trimmed().isEmpty() || !suffixes.contains(info.suffix(), Qt::CaseInsensitive)) {
			return fail(error, QCoreApplication::translate("VibeStudioAudio",
			                                               "Choose an output file with one of these extensions: %1.")
			                       .arg(suffixes.join(QStringLiteral(", "))));
		}
		for (const QString &protectedPath : protectedPaths) {
			if (!protectedPath.isEmpty() && audioPathsReferToSameFile(path, protectedPath)) {
				return fail(error,
				            QCoreApplication::translate(
				                "VibeStudioAudio", "Export to a new path to preserve the source sound or package."));
			}
		}
		if (info.isSymLink() || info.isDir() || (!overwrite && info.exists())) {
			return fail(error, QCoreApplication::translate("VibeStudioAudio",
			                                               "The output exists or is a link. Choose another file or "
			                                               "explicitly allow overwriting a regular file."));
		}
		return true;
	};
	if (!checkDestination()) {
		return false;
	}
	if (dryRun) {
		return checkDestination();
	}
	QLockFile lock(QFileInfo(path).absoluteFilePath() + QStringLiteral(".vibestudio-export.lock"));
	if (!lock.tryLock(0)) {
		return fail(error, QCoreApplication::translate(
		                       "VibeStudioAudio",
		                       "Another export may be writing this sound, or the destination is not writable."));
	}
	if (!checkDestination()) {
		return false;
	}
	QSaveFile file(QFileInfo(path).absoluteFilePath());
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
		return fail(error,
		            QCoreApplication::translate("VibeStudioAudio", "Unable to save audio: %1").arg(file.errorString()));
	}
	if (!checkDestination()) {
		file.cancelWriting();
		return false;
	}
	if (!file.commit()) {
		return fail(error,
		            QCoreApplication::translate("VibeStudioAudio", "Unable to save audio: %1").arg(file.errorString()));
	}
	return true;
}

bool isGeneratedIntegerAudioWav(const QByteArray &bytes)
{
	if (bytes.size() < 46 || bytes.size() > 68 + AudioSampleLimit * 4 + AudioMarkerByteLimit ||
	    bytes.first(4) != "RIFF" || bytes.mid(8, 8) != "WAVEfmt ") {
		return false;
	}
	const auto u16 = [&](int offset) { return qFromLittleEndian<quint16>(bytes.constData() + offset); };
	const auto u32 = [&](int offset) { return qFromLittleEndian<quint32>(bytes.constData() + offset); };
	const bool extended = u32(16) == 40;
	if (u32(4) != bytes.size() - 8 || (u32(16) != 16 && !extended) || u16(20) != (extended ? 0xfffe : 1)) {
		return false;
	}
	const int channels = u16(22), bits = u16(34), stride = channels * (bits / 8);
	const int header = extended ? 68 : 44;
	if (bytes.size() < header || channels < 1 || channels > 8 || u32(24) < 1 || u32(24) > 384000 ||
	    (bits != 8 && bits != 16 && bits != 24 && bits != 32) || u16(32) != stride || u32(28) != u32(24) * stride ||
	    bytes.mid(header - 8, 4) != "data") {
		return false;
	}
	if (extended && (u16(36) != 22 || u16(38) != bits ||
	                 u32(40) != (channels == 1   ? 4u
	                             : channels == 2 ? 3u
	                                             : 0u) ||
	                 bytes.mid(44, 16) != QByteArray::fromHex("0100000000001000800000aa00389b71"))) {
		return false;
	}
	const qint64 dataSize = u32(header - 4);
	const qint64 afterData = header + dataSize + (dataSize & 1);
	if (dataSize <= 0 || dataSize % stride != 0 || dataSize / (bits / 8) > AudioSampleLimit ||
	    afterData > bytes.size() || bytes.size() - afterData > AudioMarkerByteLimit ||
	    ((dataSize & 1) && bytes[header + dataSize] != '\0')) {
		return false;
	}
	AudioMarkers markers;
	return decodeWavAudioMarkers(bytes, dataSize / stride, &markers);
}

} // namespace vibestudio
