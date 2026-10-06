#include "core/audio_export.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-export-XXXXXX")));
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	const AudioClip extremes{1, 8000, {-1, -.5f, 0, .5f, 1, 2, -2}};
	struct Fixture {
		AudioWavFormat format;
		QByteArray pcm;
	};
	const QVector<Fixture> fixtures{
	    {AudioWavFormat::Pcm8, QByteArray::fromHex("004080c0ffff00")},
	    {AudioWavFormat::Pcm16, QByteArray::fromHex("008000c000000040ff7fff7f0080")},
	    {AudioWavFormat::Pcm24, QByteArray::fromHex("0000800000c0000000000040ffff7fffff7f000080")},
	    {AudioWavFormat::Pcm32, QByteArray::fromHex("00000080000000c00000000000000040ffffff7fffffff7f00000080")}};
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(archive.load(temporary.path(), &error) && staging.loadBaseArchive(archive, &error),
	             "create staging fixture");
	for (const auto& fixture : fixtures) {
		const AudioWavOptions options{fixture.format};
		const QByteArray bytes = encodeAudioWav(extremes, options, &error);
		const int header = audioWavBits(fixture.format) > 16 ? 68 : 44;
		ok &= expect(error.isEmpty() && bytes.mid(header, fixture.pcm.size()) == fixture.pcm &&
		                 qFromLittleEndian<quint32>(bytes.constData() + header - 4) == fixture.pcm.size(),
		             "integer export exactly matches independent little-endian PCM fixtures");
		ok &= expect(bytes.size() == header + fixture.pcm.size() + (fixture.pcm.size() & 1) &&
		                 qFromLittleEndian<quint32>(bytes.constData() + 4) == bytes.size() - 8 && bytes.size() % 2 == 0,
		             "RIFF sizes include the required odd-byte pad but data size excludes it");
		const auto decoded = decodeAudioClip(QStringLiteral("fixture.wav"), bytes);
		ok &= expect(decoded.succeeded() && decoded.clip.frameCount() == 7 && decoded.clip.sampleRate == 8000 &&
		                 decoded.clip.samples.first() == -1 && decoded.clip.samples[3] == .5f &&
		                 decoded.clip.samples.last() == -1,
		             "native decoder opens each integer export with the right duration and sign");
		ok &= expect(isGeneratedIntegerAudioWav(bytes) &&
		                 stageAudioWav(bytes, audioWavFormatId(fixture.format) + QStringLiteral(".wav"), &staging,
		                               false, &error),
		             "integer precision variants can join the ordinary package staging service");
		QByteArray corrupt = bytes;
		qToLittleEndian<quint32>(1, corrupt.data() + 28);
		ok &= expect(!isGeneratedIntegerAudioWav(corrupt), "invalid byte rate cannot pass the handoff validator");
		corrupt = bytes;
		qToLittleEndian<quint32>(quint32(fixture.pcm.size() + 4), corrupt.data() + header - 4);
		ok &= expect(!isGeneratedIntegerAudioWav(corrupt), "truncated PCM cannot pass the handoff validator");
	}
	AudioClip floating{2, 48000, {-0.0f, 2.5f, -3.125f, std::numeric_limits<float>::denorm_min(), .123456789f, -1}};
	const QByteArray floats = encodeAudioWav(floating, {AudioWavFormat::Float32}, &error);
	ok &= expect(floats.size() == 58 + floating.samples.size() * 4 && floats.mid(38, 4) == "fact" &&
	                 qFromLittleEndian<quint32>(floats.constData() + 46) == 3,
	             "float WAV contains its extension size and a frame-count fact chunk");
	const auto decodedFloat = decodeAudioClip(QStringLiteral("float.wav"), floats);
	ok &= expect(decodedFloat.succeeded() && decodedFloat.clip.samples.size() == floating.samples.size(),
	             "float WAV decodes");
	if (decodedFloat.succeeded()) {
		for (qsizetype index = 0; index < floating.samples.size(); ++index) {
			const auto expected = std::bit_cast<quint32>(floating.samples[index]);
			ok &= expect(qFromLittleEndian<quint32>(floats.constData() + 58 + index * 4) == expected &&
			                 std::bit_cast<quint32>(decodedFloat.clip.samples[index]) == expected,
			             "float WAV preserves exact bits, negative zero, subnormals, and unclipped headroom");
		}
	}
	for (const auto format : {AudioWavFormat::Pcm16, AudioWavFormat::Float32}) {
		const AudioClip multichannel{3, 22050, {-.5f, 0, .5f, .25f, -.25f, 0}};
		const QByteArray bytes = encodeAudioWav(multichannel, {format});
		const auto decoded = decodeAudioClip(QStringLiteral("multichannel.wav"), bytes);
		ok &= expect(qFromLittleEndian<quint16>(bytes.constData() + 20) == 0xfffe &&
		                 qFromLittleEndian<quint32>(bytes.constData() + 40) == 0 && decoded.succeeded() &&
		                 decoded.clip.samples == multichannel.samples,
		             "extensible multichannel WAV retains channel order without inventing speaker assignments");
	}
	const AudioClip silence{1, 48000, QVector<float>(65536, 0)};
	const AudioWavOptions dither{AudioWavFormat::Pcm16, true, 12345678901234567890ULL};
	const auto noisy = encodeAudioWav(silence, dither);
	ok &= expect(noisy == encodeAudioWav(silence, dither) &&
	                 noisy != encodeAudioWav(silence, {AudioWavFormat::Pcm16, true, 42}),
	             "TPDF dither is deterministic for an exact seed and differs for another seed");
	double sum = 0, squares = 0;
	for (int index = 0; index < 65536; ++index) {
		const auto sample = qFromLittleEndian<qint16>(noisy.constData() + 44 + index * 2);
		ok &= expect(std::abs(sample) <= 1, "dither on silence stays within one quantized LSB");
		sum += sample;
		squares += double(sample) * sample;
	}
	ok &= expect(std::abs(sum / 65536) < .015 && std::abs(std::sqrt(squares / 65536) - .5) < .015,
	             "independent dither statistics establish low bias and the expected quantized noise power");
	ok &= expect(encodeAudioWav(silence, {AudioWavFormat::Float32, true}, &error).isEmpty() && !error.isEmpty(),
	             "float dither is rejected instead of altering high-precision samples");
	int polls = 0;
	ok &= expect(encodeAudioWav(silence, dither, &error, {[&] { return ++polls == 5; }}).isEmpty() && polls == 5,
	             "encoding cancellation discards partial bytes");
	const QString path = QDir(temporary.path()).filePath(QStringLiteral("result.wav"));
	ok &= expect(saveAudioWav(floating, {AudioWavFormat::Float32}, path, false, {}, &error, true) &&
	                 !QFileInfo::exists(path) && !QFileInfo::exists(path + QStringLiteral(".vibestudio-export.lock")),
	             "precision dry runs create neither output nor lock file");
	ok &= expect(saveAudioWav(floating, {AudioWavFormat::Float32}, path, false, {}, &error) && read(path) == floats,
	             "atomic precision export writes the exact encoded stream");
	QLockFile lock(path + QStringLiteral(".vibestudio-export.lock"));
	ok &= expect(lock.tryLock() && !saveAudioWav(extremes, {AudioWavFormat::Pcm8}, path, true, {}, &error) &&
	                 read(path) == floats,
	             "a concurrent cooperating writer blocks replacement without altering the file");
	lock.unlock();
	ok &= expect(!saveAudioWav(extremes, {AudioWavFormat::Pcm8}, path, true, path, &error) && read(path) == floats,
	             "precision conversion never overwrites its protected source");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
