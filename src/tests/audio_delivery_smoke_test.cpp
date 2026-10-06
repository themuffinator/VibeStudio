#include "core/audio_delivery.h"
#include "core/audio_project.h"
#include "core/package_staging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <cmath>
#include <iostream>

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
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-delivery-XXXXXX")));
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	AudioClip exact{1, 11025, {-1, -0.5f, 0, 0.5f, 1, 2, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}};
	const AudioClip original = exact;
	const auto doom = renderAudioDelivery(exact, {AudioDeliveryPreset::Doom, {}});
	const QByteArray pcm = QByteArray::fromHex("004080c0ffff00808080808080808080ff");
	ok &= expect(doom.succeeded() && doom.bytes.size() == 57 &&
	                 doom.bytes.first(8) == QByteArray::fromHex("0300112b31000000") && doom.bytes.mid(24, 17) == pcm &&
	                 doom.bytes.mid(8, 16) == QByteArray(16, '\0') && doom.bytes.last(16) == QByteArray(16, char(255)),
	             "DMX header, exact playable bytes, padded count, and endpoint padding match independent fixtures");
	ok &= expect(isGeneratedAudioDmx(doom.bytes) && doom.samplesAboveFullScale == 2 && doom.peak == 2 &&
	                 exact.samples == original.samples,
	             "delivery reports saturation without changing the source samples");
	const auto decoded = decodeAudioClip(QStringLiteral("DSFIXTUR"), doom.bytes);
	ok &= expect(decoded.succeeded() && decoded.clip.frameCount() == 17 && decoded.clip.sampleRate == 11025 &&
	                 decoded.clip.channels == 1 && decoded.clip.samples[0] == -1 &&
	                 decoded.clip.samples[16] == 127.0f / 128,
	             "DMX round trip excludes both padding regions and keeps playback length");
	AudioClip shortClip = exact;
	shortClip.samples.resize(16);
	ok &= expect(!renderAudioDelivery(shortClip, {AudioDeliveryPreset::Doom, {}}).succeeded(),
	             "too-short DMX cannot produce a silently unplayable sound");
	for (int index : {0, 2, 4, 8, 56}) {
		QByteArray invalid = doom.bytes;
		invalid[index] = 0;
		if (invalid == doom.bytes) {
			invalid[index] = 1;
		}
		// Changing a valid nonzero rate is allowed; explicitly zero its field.
		if (index == 2) {
			invalid[3] = 0;
		}
		ok &= expect(!isGeneratedAudioDmx(invalid), "malformed generated DMX headers and padding are rejected");
	}
	AudioClip stereo{2, 44100, {}};
	stereo.samples.resize(4410 * 2);
	for (int frame = 0; frame < 4410; ++frame) {
		const double wave = std::sin(2 * 3.14159265358979323846 * 500 * frame / 44100);
		stereo.samples[frame * 2] = float(0.8 * wave);
		stereo.samples[frame * 2 + 1] = float(0.4 * wave);
	}
	for (const auto preset : {AudioDeliveryPreset::Doom, AudioDeliveryPreset::Quake, AudioDeliveryPreset::Quake2,
	                          AudioDeliveryPreset::Quake3}) {
		const auto delivered = renderAudioDelivery(stereo, {preset, {}});
		const bool low = preset == AudioDeliveryPreset::Doom || preset == AudioDeliveryPreset::Quake;
		const int rate = low ? 11025 : 22050;
		const auto result = decodeAudioClip(preset == AudioDeliveryPreset::Doom ? QStringLiteral("tone.dmx")
		                                                                        : QStringLiteral("tone.wav"),
		                                    delivered.bytes);
		ok &= expect(delivered.succeeded() && result.succeeded() && result.clip.channels == 1 &&
		                 result.clip.sampleRate == rate && result.clip.frameCount() == (low ? 1103 : 2205),
		             "game delivery mixes and resamples with the advertised frame count and format");
		if (!result.succeeded()) {
			continue;
		}
		double squares = 0;
		int count = 0;
		for (int frame = 200; frame < result.clip.frameCount() - 200; ++frame) {
			const double residual =
			    result.clip.samples[frame] - 0.6 * std::sin(2 * 3.14159265358979323846 * 500 * frame / rate);
			squares += residual * residual;
			++count;
		}
		ok &= expect(std::sqrt(squares / count) < (low ? 0.0025 : 0.00002),
		             "delivery has equal-weight mono amplitude and quantization-bounded passband error");
		if (preset != AudioDeliveryPreset::Doom) {
			ok &= expect(delivered.bytes.mid(20, 4) == QByteArray::fromHex("01000100") &&
			                 qFromLittleEndian<quint16>(delivered.bytes.constData() + 34) == (low ? 8 : 16),
			             "Quake presets use legacy mono PCM headers accepted by original engine readers");
		}
	}
	int polls = 0;
	const auto cancelled = renderAudioDelivery(stereo, {AudioDeliveryPreset::Doom, {}}, {[&]() { return ++polls >= 3; }});
	ok &= expect(cancelled.cancelled && cancelled.bytes.isEmpty(), "cancelled delivery produces no partial output");
	const auto tpdf = renderAudioDelivery(exact, {AudioDeliveryPreset::Doom, {AudioWavFormat::Pcm16, true, 42}});
	const auto wav = encodeAudioWav(exact, {AudioWavFormat::Pcm8, true, 42});
	ok &= expect(tpdf.succeeded() && tpdf.bytes.mid(24, 17) == wav.mid(44, 17),
	             "DMX and WAV apply exactly one identical deterministic PCM8 dither step");
	ok &=
	    expect(!renderAudioDelivery(exact, {static_cast<AudioDeliveryPreset>(-1), {}}).succeeded() &&
	               !renderAudioDelivery(exact, {AudioDeliveryPreset::Wav, {AudioWavFormat::Float32, true}}).succeeded(),
	           "invalid delivery options fail before output");
	const QString output = QDir(temporary.path()).filePath(QStringLiteral("DSFIXTUR.dmx"));
	ok &= expect(writeAudioDelivery(doom, output, false, {}, &error, true) && !QFileInfo::exists(output) &&
	                 !QFileInfo::exists(output + QStringLiteral(".vibestudio-export.lock")),
	             "delivery dry run creates neither output nor lock");
	ok &= expect(writeAudioDelivery(doom, output, false, {}, &error) && read(output) == doom.bytes,
	             "delivery commits exact prepared bytes");
	ok &= expect(!writeAudioDelivery(tpdf, output, true, {QString(), output}, &error) && read(output) == doom.bytes,
	             "every protected source path is enforced even with overwrite");
	ok &= expect(!writeAudioDelivery(doom, output + QStringLiteral(".wav"), false, {}, &error),
	             "DMX cannot masquerade as a WAV file");
	QLockFile lock(output + QStringLiteral(".vibestudio-export.lock"));
	ok &= expect(lock.tryLock(0) && !writeAudioDelivery(tpdf, output, true, {}, &error) && read(output) == doom.bytes,
	             "competing delivery cannot replace an active writer's file");
	lock.unlock();
	const QString base = QDir(temporary.path()).filePath(QStringLiteral("base.wad"));
	const QByteArray emptyWad = QByteArray::fromHex("50574144000000000c000000");
	PackageArchive archive;
	PackageStagingModel staging;
	ok &= expect(write(base, emptyWad) && archive.load(base, &error) && staging.loadBaseArchive(archive, &error),
	             "load an independently generated empty Doom WAD");
	ok &= expect(stageAudioDelivery(doom.bytes, QStringLiteral("DSFIXTUR"), &staging, false, &error),
	             "stage generated DMX in a Doom WAD");
	const int operations = staging.operations().size();
	ok &= expect(!stageAudioDelivery(tpdf.bytes, QStringLiteral("DSFIXTUR"), &staging, false, &error) &&
	                 staging.operations().size() == operations,
	             "replacement collision is transactional");
	ok &= expect(!stageAudioDelivery(doom.bytes, QStringLiteral("THINGS"), &staging, true, &error) &&
	                 !stageAudioDelivery(doom.bytes, QStringLiteral("sound/DSFIXTUR.lmp"), &staging, true, &error),
	             "DMX staging cannot target structural WAD lumps or file paths");
	ok &= expect(stageAudioDelivery(tpdf.bytes, QStringLiteral("DSFIXTUR"), &staging, true, &error),
	             "explicit replacement uses the normal pending package plan");
	PackageStagingArchive pending(staging);
	ok &= expect(!pending.entries().isEmpty() &&
	                 assetPreviewKindForEntry(pending.entries().first().virtualPath,
	                                          pending.entries().first().typeHint) == AssetPreviewKind::Audio,
	             "pending generated Doom lumps retain audio classification without an extension");
	ok &= expect(assetPreviewKindForPath(QStringLiteral("loose.dmx")) == AssetPreviewKind::Audio,
	             "loose DMX delivery routes to the audio surface");
	QByteArray staged;
	ok &= expect(pending.readEntryBytes(QStringLiteral("DSFIXTUR"), &staged, &error) && staged == tpdf.bytes &&
	                 read(base) == emptyWad,
	             "pending readers see generated bytes without modifying the source WAD");
	PackageWriteRequest request;
	ok &= expect(staging.addBytes(doom.bytes, QStringLiteral("WIND"), &error),
	             "add a header-detected sound without the Doom name prefix");
	request.format = PackageArchiveFormat::Wad;
	request.destinationPath = QDir(temporary.path()).filePath(QStringLiteral("delivered.wad"));
	const auto saved = staging.writeArchive(request);
	PackageArchive written;
	ok &= expect(saved.succeeded() && written.load(request.destinationPath, &error) &&
	                 written.readEntryBytes(QStringLiteral("DSFIXTUR"), &staged, &error) && staged == tpdf.bytes,
	             "package save preserves exact DMX delivery bytes");
	PackageStagingModel reopened;
	ok &= expect(reopened.loadBaseArchive(written, &error), "reopen WAD with header-detected sound");
	bool foundHeaderSound = false;
	for (const auto& entry : PackageStagingArchive(reopened).entries()) {
		if (entry.virtualPath == QStringLiteral("WIND")) {
			foundHeaderSound = assetPreviewKindForEntry(entry.virtualPath, entry.typeHint) == AssetPreviewKind::Audio;
		}
	}
	ok &=
	    expect(foundHeaderSound, "pending WAD views preserve header-based classification independently of lump names");
	PackageStagingModel blocked = reopened;
	blocked.addBytes(QByteArray("unrelated invalid collision"), QStringLiteral("WIND"), &error);
	const auto inspectable = packagePlannedArchive(blocked);
	ok &= expect(!blocked.summary().canSave && inspectable.isOpen() &&
	                 inspectable.readEntryBytes(QStringLiteral("DSFIXTUR"), &staged, &error) && staged == tpdf.bytes,
	             "unrelated save conflicts do not hide or replace readable pending audio");
	PackageArchive textureWad;
	PackageStagingModel textureStage;
	const QString texturePath = QDir(temporary.path()).filePath(QStringLiteral("textures.wad"));
	ok &= expect(write(texturePath, QByteArray::fromHex("57414432000000000c000000")) &&
	                 textureWad.load(texturePath, &error) && textureStage.loadBaseArchive(textureWad, &error) &&
	                 !stageAudioDelivery(doom.bytes, QStringLiteral("DSFIXTUR"), &textureStage, false, &error),
	             "Quake texture WADs are not Doom sound containers");
	if (argc > 1) {
		const QString native = QDir(temporary.path()).filePath(QStringLiteral("input.vsaudio"));
		ok &= expect(
		    writeAudioProject({stereo, 0, stereo.frameCount(), QStringLiteral("tone"), output, {}},
		                      {native, false, false, {}, {}}).succeeded,
		    "write native CLI delivery fixture");
		const QString cliOutput = QDir(temporary.path()).filePath(QStringLiteral("cli.dmx"));
		QByteArray stdoutBytes;
		const auto cli = [&](QStringList args, int expected) {
			QProcess process;
			process.setWorkingDirectory(temporary.path());
			process.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(), QStringList{QStringLiteral("--cli"), QStringLiteral("asset"),
			                                                           QStringLiteral("audio-export")} +
			                                                   args + QStringList{QStringLiteral("--json")});
			const bool finished = process.waitForFinished(30000);
			stdoutBytes = process.readAllStandardOutput();
			if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != expected) {
				std::cerr << stdoutBytes.constData() << process.readAllStandardError().constData();
				return false;
			}
			return true;
		};
		QStringList args{native,
		                 QStringLiteral("--preset"),
		                 QStringLiteral("doom"),
		                 QStringLiteral("--output"),
		                 cliOutput,
		                 QStringLiteral("--dither"),
		                 QStringLiteral("tpdf"),
		                 QStringLiteral("--dither-seed"),
		                 QStringLiteral("42")};
		ok &= expect(cli(args + QStringList{QStringLiteral("--dry-run")}, 0) && !QFileInfo::exists(cliOutput),
		             "CLI dry run converts and reports without creating a file");
		const auto report =
		    QJsonDocument::fromJson(stdoutBytes).object().value(QStringLiteral("audioExport")).toObject();
		ok &= expect(report.value(QStringLiteral("sampleRate")).toInt() == 11025 &&
		                 report.value(QStringLiteral("channels")).toInt() == 1 &&
		                 report.value(QStringLiteral("frames")).toInt() == 1103 &&
		                 report.value(QStringLiteral("ditherSeed")).toString() == QStringLiteral("42") &&
		                 !report.value(QStringLiteral("written")).toBool(),
		             "CLI JSON describes the actual converted delivery");
		ok &= expect(
		    cli(args, 0) &&
		        read(cliOutput) ==
		            renderAudioDelivery(stereo, {AudioDeliveryPreset::Doom, {AudioWavFormat::Pcm16, true, 42}}).bytes,
		    "CLI and core delivery bytes are identical");
		ok &= expect(cli(args, 1), "CLI existing outputs require explicit overwrite");
		ok &= expect(cli(args + QStringList{QStringLiteral("--wav-format"), QStringLiteral("pcm24")}, 2),
		             "fixed game presets reject misleading precision overrides");
		ok &= expect(
		    cli({native, QStringLiteral("--preset"), QStringLiteral("unknown"), QStringLiteral("--output"), cliOutput},
		        2),
		    "unknown presets have a usage exit code");
		ok &= expect(cli({native, QStringLiteral("--preset"), QStringLiteral("doom"), QStringLiteral("--output"),
		                  output, QStringLiteral("--overwrite")},
		                 1) &&
		                 read(output) == doom.bytes,
		             "native provenance protects the original audio even during explicit overwrite");
		ok &= expect(cli({cliOutput, QStringLiteral("--preset"), QStringLiteral("doom"), QStringLiteral("--output"),
		                  cliOutput, QStringLiteral("--overwrite")},
		                 1),
		             "CLI input DMX cannot be overwritten by delivery");
		ok &= expect(cli({request.destinationPath, QStringLiteral("--entry"), QStringLiteral("DSFIXTUR"),
		                  QStringLiteral("--output"), QDir(temporary.path()).filePath(QStringLiteral("from-wad.wav")),
		                  QStringLiteral("--wav-format"), QStringLiteral("float32")},
		                 0),
		             "CLI delivery reads staged-and-saved WAD sounds through the shared decoder");
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
