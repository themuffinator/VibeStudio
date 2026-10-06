#include "core/asset_tools.h"
#include "core/audio_decode.h"
#include "core/audio_export.h"
#include "core/audio_project.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>

using namespace vibestudio;
namespace
{
QByteArray read(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool expect(bool value, const QString& message)
{
	if (!value) {
		std::cerr << message.toStdString() << '\n';
	}
	return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool integration(const QDir& fixtures, const QDir& output, const QString& executable)
{
	bool ok = true;
	QString error;
	PackageArchive archive;
	ok &= expect(archive.load(fixtures.absolutePath(), &error),
	             QStringLiteral("Open compressed fixture package: ") + error);
	QJsonObject report;
	const auto cli = [&](const QString& command, const QStringList& arguments, int code = 0) {
		QProcess child;
		child.setWorkingDirectory(output.absolutePath());
		child.start(executable, QStringList{QStringLiteral("--cli"), QStringLiteral("--json"),
		                                    QStringLiteral("--settings-file"),
		                                    output.filePath(QStringLiteral("settings.ini")),
		                                    QStringLiteral("asset"), command} +
		                            arguments);
		const bool finished = child.waitForFinished(30000);
		const auto bytes = child.readAllStandardOutput();
		report = QJsonDocument::fromJson(bytes).object();
		if (!finished || child.exitCode() != code) {
			std::cerr << bytes.constData() << child.readAllStandardError().constData();
		}
		return expect(finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == code &&
		                  !report.isEmpty(),
		              command + QStringLiteral(" exit and JSON contract"));
	};
	for (const auto& name : {QStringLiteral("flac-stereo24.flac"), QStringLiteral("mp3-vbr.mp3"),
	                         QStringLiteral("vorbis-6ch.ogg")}) {
		const auto source = fixtures.filePath(name);
		const auto original = read(source);
		const auto decoded = decodeAudioClip(name, original);
		const auto nativePath = output.filePath(name + QStringLiteral(".vsaudio"));
		const auto wavPath = output.filePath(name + QStringLiteral(".wav"));
		const auto dry = exportPackageAudioToWav(archive, name, wavPath, true);
		ok &= expect(dry.succeeded() && !dry.written && !QFileInfo::exists(wavPath),
		             name + QStringLiteral(" browser export dry run"));
		const auto cancelled =
		    exportPackageAudioToWav(archive, name, wavPath, false, false, [] { return true; });
		ok &= expect(cancelled.cancelled && !cancelled.succeeded() && !QFileInfo::exists(wavPath),
		             name + QStringLiteral(" browser cancellation"));
		int polls = 0;
		const auto during =
		    exportPackageAudioToWav(archive, name, wavPath, false, false, [&polls] { return ++polls >= 10; });
		ok &= expect(during.cancelled && !during.succeeded() && !QFileInfo::exists(wavPath),
		             name + QStringLiteral(" browser in-flight cancellation"));
		const auto exported = exportPackageAudioToWav(archive, name, wavPath);
		ok &= expect(exported.succeeded() && exported.converted &&
		                 read(wavPath) == encodeAudioWav(decoded.clip),
		             name + QStringLiteral(" browser shares exact PCM16 delivery"));
		ok &= expect(!exportPackageAudioToWav(archive, name, source, false, true).succeeded() &&
		                 read(source) == original,
		             name + QStringLiteral(" browser protects its source"));
		ok &= cli(QStringLiteral("audio-project"),
		          {source, QStringLiteral("--output"), nativePath, QStringLiteral("--dry-run")});
		ok &= expect(!QFileInfo::exists(nativePath) && !report.value(QStringLiteral("audioProject"))
		                                                    .toObject()
		                                                    .value(QStringLiteral("importWarnings"))
		                                                    .toArray()
		                                                    .isEmpty(),
		             name + QStringLiteral(" CLI dry run reports omitted tags"));
		ok &= cli(QStringLiteral("audio-project"), {source, QStringLiteral("--output"), nativePath});
		AudioProject project;
		ok &= expect(readAudioProject(nativePath, &project, nullptr, &error) &&
		                 project.clip.samples == decoded.clip.samples &&
		                 !project.metadata.value(QStringLiteral("importWarnings")).toArray().isEmpty(),
		             name + QStringLiteral(" native sample and import warning persistence"));
		ok &= cli(QStringLiteral("audio-analyze"), {source});
		ok &= expect(!report.value(QStringLiteral("audioAnalysis"))
		                  .toObject()
		                  .value(QStringLiteral("importWarnings"))
		                  .toArray()
		                  .isEmpty(),
		             name + QStringLiteral(" analysis reports import limitations"));
		ok &= cli(QStringLiteral("audio-analyze"), {nativePath});
		ok &= expect(!report.value(QStringLiteral("audioAnalysis"))
		                  .toObject()
		                  .value(QStringLiteral("importWarnings"))
		                  .toArray()
		                  .isEmpty(),
		             name + QStringLiteral(" native analysis retains import warnings"));
		const auto editedPath = output.filePath(name + QStringLiteral("-edited.vsaudio"));
		ok &= cli(QStringLiteral("audio-edit"),
		          {source, QStringLiteral("--operation"), QStringLiteral("invert"),
		           QStringLiteral("--output"), editedPath});
		ok &= expect(readAudioProject(editedPath, &project, nullptr, &error) &&
		                 project.clip.samples ==
		                     applyAudioEdit(decoded.clip, {QStringLiteral("invert")}).clip.samples &&
		                 !project.metadata.value(QStringLiteral("importWarnings")).toArray().isEmpty(),
		             name + QStringLiteral(" CLI compressed edit preserves decoded "
		                                   "precision and warnings"));
		const auto deliveredPath = output.filePath(name + QStringLiteral("-float.wav"));
		ok &= cli(QStringLiteral("audio-export"),
		          {source, QStringLiteral("--preset"), QStringLiteral("wav"), QStringLiteral("--wav-format"),
		           QStringLiteral("float32"), QStringLiteral("--output"), deliveredPath});
		ok &= expect(decodeAudioClip(deliveredPath, read(deliveredPath)).clip.samples == decoded.clip.samples,
		             name + QStringLiteral(" CLI float delivery retains decoded samples"));
		ok &= cli(QStringLiteral("audio-markers"), {source});
		ok &= expect(!report.value(QStringLiteral("audioMarkers"))
		                  .toObject()
		                  .value(QStringLiteral("importWarnings"))
		                  .toArray()
		                  .isEmpty(),
		             name + QStringLiteral(" marker inspection reports unimported metadata"));
		const auto packagePath = output.filePath(name + QStringLiteral("-package.wav"));
		ok &= cli(QStringLiteral("audio-wav"),
		          {fixtures.absolutePath(), name, QStringLiteral("--output"), packagePath});
		ok &= expect(read(packagePath) == read(wavPath) && report.value(QStringLiteral("audioExport"))
		                                                       .toObject()
		                                                       .value(QStringLiteral("converted"))
		                                                       .toBool(),
		             name + QStringLiteral(" package CLI shares browser output"));
		const auto brokenPath = output.filePath(name + QStringLiteral("-broken"));
		const auto refusedPath = output.filePath(name + QStringLiteral("-refused.vsaudio"));
		ok &= expect(write(brokenPath, original.first(original.size() / 2)),
		             QStringLiteral("Write truncated import fixture"));
		ok &= cli(QStringLiteral("audio-project"), {brokenPath, QStringLiteral("--output"), refusedPath}, 4);
		ok &= expect(!QFileInfo::exists(refusedPath) && read(source) == original,
		             name + QStringLiteral(" failed import leaves output absent and source unchanged"));
	}
	return ok;
}
} // namespace
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (argc < 2) {
		return 2;
	}
	const QDir fixtures(QString::fromLocal8Bit(argv[1]));
	const auto cases = QJsonDocument::fromJson(read(fixtures.filePath(QStringLiteral("manifest.json"))))
	                       .object()
	                       .value(QStringLiteral("cases"))
	                       .toArray();
	if (cases.size() != 16) {
		return 3;
	}
	bool ok = true;
	for (const auto& entry : cases) {
		const auto item = entry.toObject();
		const auto name = item.value(QStringLiteral("file")).toString();
		const auto bytes = read(fixtures.filePath(name));
		const auto oracle = read(fixtures.filePath(item.value(QStringLiteral("oracle")).toString()));
		ok &=
		    expect(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) ==
		               item.value(QStringLiteral("sha256")).toString(),
		           name + QStringLiteral(" fixture hash"));
		ok &= expect(
		    QString::fromLatin1(QCryptographicHash::hash(oracle, QCryptographicHash::Sha256).toHex()) ==
		        item.value(QStringLiteral("oracleSha256")).toString(),
		    name + QStringLiteral(" oracle hash"));
		const auto decoded = decodeAudioClip(name, bytes);
		if (!expect(decoded.succeeded(), name + QStringLiteral(": ") + decoded.error)) {
			ok = false;
			continue;
		}
		const auto& clip = decoded.clip;
		ok &= expect(clip.channels == item.value(QStringLiteral("channels")).toInt() &&
		                 clip.sampleRate == item.value(QStringLiteral("sampleRate")).toInt(),
		             name + QStringLiteral(" format"));
		ok &= expect(clip.frameCount() == item.value(QStringLiteral("frames")).toInteger(),
		             name + QStringLiteral(" frame count: ") + QString::number(clip.frameCount()));
		if (oracle.size() == clip.samples.size() * 4) {
			double maxError = 0;
			for (qsizetype i = 0; i < clip.samples.size(); ++i) {
				const quint32 bits = qFromLittleEndian<quint32>(oracle.constData() + i * 4);
				float value = 0;
				std::memcpy(&value, &bits, 4);
				maxError = std::max(maxError, std::abs(double(value) - clip.samples[i]));
			}
			ok &=
			    expect(maxError <= item.value(QStringLiteral("tolerance")).toDouble(),
			           name + QStringLiteral(" max oracle difference: ") + QString::number(maxError, 'g', 9));
			std::cout << name.toStdString() << ": " << clip.frameCount() << " frames, maximum difference "
			          << maxError << '\n';
		} else {
			ok = false;
		}
		const auto cancelled = decodeAudioClip(name, bytes, {[] { return true; }});
		ok &= expect(cancelled.cancelled && cancelled.clip.samples.isEmpty(),
		             name + QStringLiteral(" pre-cancel"));
		int polls = 0;
		const auto during = decodeAudioClip(name, bytes, {[&polls] { return ++polls >= 9; }});
		ok &= expect(during.cancelled && during.clip.samples.isEmpty(),
		             name + QStringLiteral(" in-flight cancellation"));
		const auto starved = decodeCompressedAudio(bytes, {}, {AudioSampleLimit, 64});
		ok &= expect(!starved.succeeded() && !starved.error.isEmpty() && starved.clip.samples.isEmpty(),
		             name + QStringLiteral(" allocation exhaustion"));
		for (size_t memory : {8192u, 65536u, 262144u}) {
			const auto partial = decodeCompressedAudio(bytes, {}, {AudioSampleLimit, memory});
			ok &= expect(partial.succeeded() ? partial.clip.samples == clip.samples
			                                 : !partial.error.isEmpty() && partial.clip.samples.isEmpty(),
			             name + QStringLiteral(" partial setup allocation exhaustion: ") +
			                 QString::number(memory));
		}
		const auto tooMany = decodeCompressedAudio(bytes, {}, {clip.samples.size() - 1, 16 * 1024 * 1024});
		ok &= expect(!tooMany.succeeded() && tooMany.clip.samples.isEmpty(),
		             name + QStringLiteral(" output sample budget"));
		const auto exactBudget = decodeCompressedAudio(bytes, {}, {clip.samples.size(), 16 * 1024 * 1024});
		ok &= expect(exactBudget.succeeded() && exactBudget.clip.samples == clip.samples,
		             name + QStringLiteral(" recovery after allocation exhaustion "
		                                   "and exact sample limit"));
		for (const int removed : {1, 17, int(bytes.size() / 2)}) {
			const auto shortInput = decodeAudioClip(name, bytes.first(bytes.size() - removed));
			ok &= expect(!shortInput.succeeded() && shortInput.clip.samples.isEmpty(),
			             name + QStringLiteral(" truncated input: ") + QString::number(removed));
		}
		if (name.endsWith(QLatin1String(".ogg")) || name.endsWith(QLatin1String(".flac"))) {
			auto corrupt = bytes;
			corrupt[corrupt.size() - 20] = char(uchar(corrupt[corrupt.size() - 20]) ^ 16);
			ok &= expect(!decodeAudioClip(name, corrupt).succeeded(), name + QStringLiteral(" corruption"));
		}
	}
	std::array<std::future<bool>, 3> concurrent;
	for (auto& future : concurrent) {
		future = std::async(std::launch::async, [fixtures] {
			const auto bytes = read(fixtures.filePath(QStringLiteral("vorbis-8ch.ogg")));
			for (int i = 0; i < 8; ++i) {
				if (decodeCompressedAudio(bytes, {}, {AudioSampleLimit, 64}).succeeded() ||
				    !decodeCompressedAudio(bytes).succeeded()) {
					return false;
				}
			}
			return true;
		});
	}
	for (auto& future : concurrent) {
		ok &= expect(future.get(), QStringLiteral("Concurrent independent decoder allocation regions"));
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("audio-decode-XXXXXX")));
	if (argc > 2 && temporary.isValid()) {
		ok &= integration(fixtures, QDir(temporary.path()), QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath());
	} else {
		ok = false;
	}
	return ok ? 0 : 1;
}
