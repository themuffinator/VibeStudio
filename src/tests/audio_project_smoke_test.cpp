#include "core/audio_project.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>

#include <bit>
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
bool write(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray checked(QByteArray bytes)
{
	bytes.chop(32);
	return bytes + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
bool identical(const AudioProject& a, const AudioProject& b)
{
	if (a.clip.channels != b.clip.channels || a.clip.sampleRate != b.clip.sampleRate ||
	    a.clip.samples.size() != b.clip.samples.size() || a.firstFrame != b.firstFrame || a.endFrame != b.endFrame ||
	    a.sourceName != b.sourceName || a.sourcePath != b.sourcePath || a.metadata != b.metadata) {
		return false;
	}
	for (qsizetype i = 0; i < a.clip.samples.size(); ++i) {
		if (std::bit_cast<quint32>(a.clip.samples[i]) != std::bit_cast<quint32>(b.clip.samples[i])) {
			return false;
		}
	}
	return true;
}
} // namespace

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("audio-project-XXXXXX")));
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	bool ok = true;
	QString error;
	AudioProject original{
	    {2, 48000, {0.123456789f, -0.0f, 2.25f, -3.125f, std::numeric_limits<float>::denorm_min(), 0.25f}},
	    1,
	    3,
	    QString::fromUtf8("风 — fixture.wav"),
	    QDir(temp.path()).filePath(QStringLiteral("source.wav")),
	    {{QStringLiteral("note"), QStringLiteral("preserved")}}};
	const QByteArray bytes = encodeAudioProject(original, &error);
	AudioProject emptyProject{createAudioClip(44100, 2).clip, 0, 0, QStringLiteral("Empty"), {}, {}};
	AudioProject emptyRoundTrip;
	ok &= expect(decodeAudioProject(encodeAudioProject(emptyProject), &emptyRoundTrip, &error) &&
	                 identical(emptyProject, emptyRoundTrip),
	             "native documents preserve empty audio with a defined format");
	AudioProject decoded;
	ok &= expect(!bytes.isEmpty() && decodeAudioProject(bytes, &decoded, &error) && identical(original, decoded),
	             "lossless float bits, headroom, subnormals, negative zero, selection, and metadata survive");
	const quint32 jsonLength = qFromLittleEndian<quint32>(bytes.constData() + 12);
	ok &= expect(bytes.first(8) == QByteArrayLiteral("VSAUD\r\n\x1a") &&
	                 qFromLittleEndian<quint32>(bytes.constData() + 8) == 1 &&
	                 bytes.mid(16 + jsonLength + 8, 8) == QByteArray::fromHex("00001040000048c0"),
	             "independent wire fixture proves LE float32 order and container version");
	for (int length : {0, 8, 15, 47, int(bytes.size()) - 1}) {
		AudioProject keep = original;
		ok &= expect(!decodeAudioProject(bytes.first(length), &keep, &error) && identical(keep, original),
		             "truncation rejects without mutating the caller's project");
	}
	QByteArray invalid = bytes;
	invalid[16 + jsonLength] ^= 1;
	ok &= expect(!decodeAudioProject(invalid, &decoded, &error), "payload corruption fails checksum");
	invalid = bytes;
	qToLittleEndian<quint32>(99, invalid.data() + 8);
	ok &= expect(!decodeAudioProject(checked(invalid), &decoded, &error), "future version is explicitly rejected");
	invalid = bytes;
	qToLittleEndian<quint32>(0xffffffffu, invalid.data() + 12);
	ok &= expect(!decodeAudioProject(checked(invalid), &decoded, &error), "hostile length cannot allocate");
	invalid = bytes;
	qToLittleEndian<quint32>(0x7f800000u, invalid.data() + 16 + jsonLength);
	ok &= expect(!decodeAudioProject(checked(invalid), &decoded, &error), "a checksum-valid infinity is rejected");
	invalid = bytes;
	invalid.replace(16, jsonLength, QByteArray(jsonLength, ' '));
	ok &= expect(!decodeAudioProject(checked(invalid), &decoded, &error), "malformed JSON fails safely");
	invalid = bytes;
	invalid.insert(invalid.size() - 32, QByteArray(4, '\0'));
	ok &= expect(!decodeAudioProject(checked(invalid), &decoded, &error),
	             "trailing payload cannot hide outside frame count");
	ok &= expect(encodeAudioProject(original, &error, {[]() { return true; }}).isEmpty() &&
	                 !decodeAudioProject(bytes, &decoded, &error, {[]() { return true; }}),
	             "encoding and decoding cooperate with cancellation");
	AudioProject bad = original;
	bad.firstFrame = 4;
	ok &= expect(encodeAudioProject(bad, &error).isEmpty(), "selection must fit the sound");
	bad = original;
	bad.metadata.insert(QStringLiteral("excess"), QString(256 * 1024, QLatin1Char('x')));
	ok &= expect(encodeAudioProject(bad, &error).isEmpty(), "metadata allocation is bounded");
	const QString path = QDir(temp.path()).filePath(QStringLiteral("sound.vsaudio"));
	AudioProjectSaveRequest request{path, false, false, {}, {}};
	request.dryRun = true;
	ok &= expect(writeAudioProject(original, request).succeeded && !QFileInfo::exists(path) &&
	                 !QFileInfo::exists(path + QStringLiteral(".lock")),
	             "dry run creates no file or lock");
	request.dryRun = false;
	const auto saved = writeAudioProject(original, request);
	ok &= expect(saved.succeeded && saved.written && read(path) == bytes, "atomic project save");
	ok &= expect(writeAudioProject(original, request).conflict, "an existing destination requires review");
	AudioProjectIdentity identity;
	ok &= expect(readAudioProject(path, &decoded, &identity, &error) && identical(original, decoded) &&
	                 identity.sha256 == saved.identity.sha256,
	             "read establishes a content identity");
	request.expected = identity;
	AudioProject edited = original;
	edited.clip.samples[0] = 0.987654321f;
	const auto updated = writeAudioProject(edited, request);
	ok &= expect(updated.succeeded && read(path) == encodeAudioProject(edited),
	             "guarded save replaces the reviewed revision");
	ok &= expect(writeAudioProject(original, request).conflict && read(path) == encodeAudioProject(edited),
	             "stale identity cannot overwrite an external edit even with equal byte length");
	request.expected = updated.identity;
	{
		QLockFile lock(path + QStringLiteral(".lock"));
		ok &= expect(lock.tryLock(0) && writeAudioProject(original, request).conflict,
		             "concurrent writer lock prevents conflicting commit");
	}
	request.protectedPath = path;
	ok &= expect(!writeAudioProject(original, request).succeeded, "source protection takes priority over overwrite");
	request.protectedPath.clear();
	ok &= expect(!writeAudioProject(original, request, {[]() { return true; }}).written &&
	                 read(path) == encodeAudioProject(edited),
	             "cancellation never modifies the old file");
	// Simulate an external writer during serialization, before the final guard.
	bool changed = false;
	const auto racing = writeAudioProject(original, request, {[&]() {
		                                      if (!changed) {
			                                      changed = true;
			                                      write(path, bytes);
		                                      }
		                                      return false;
	                                      }});
	ok &= expect(racing.conflict && read(path) == bytes, "a change while preparing output is preserved");
	request.path = QDir(temp.path()).filePath(QStringLiteral("elsewhere.vsaudio"));
	ok &= expect(writeAudioProject(original, request).conflict, "expected identity cannot authorize another path");
	const QString recoveryDir = QDir(temp.path()).filePath(QStringLiteral("recovery"));
	const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString checkpoint = writeAudioRecovery(original, recoveryDir, id, &error);
	ok &= expect(!checkpoint.isEmpty() && readAudioProject(checkpoint, &decoded, nullptr, &error) &&
	                 decoded.clip.samples == original.clip.samples &&
	                 decoded.metadata.contains(QStringLiteral("recoveryWrittenUtc")),
	             "recovery is a checked lossless snapshot with local provenance");
	ok &= expect(audioRecoveryPath(recoveryDir, QStringLiteral("../escape")).isEmpty() &&
	                 !removeAudioRecovery(recoveryDir, QStringLiteral("../escape"), &error),
	             "recovery identifiers cannot escape their directory");
	ok &= expect(removeAudioRecovery(recoveryDir, id, &error) && !QFileInfo::exists(checkpoint),
	             "retired recovery is removed");
	if (argc > 1) {
		QJsonObject lastReport;
		const auto cli = [&](const QStringList& args, int expected,
		                     const QString& action = QStringLiteral("audio-project")) {
			QProcess child;
			child.setWorkingDirectory(temp.path());
			child.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(),
			            QStringList{QStringLiteral("--cli"), QStringLiteral("--json"),
			                        QStringLiteral("--settings-file"),
			                        QDir(temp.path()).filePath(QStringLiteral("cli-settings.ini")),
			                        QStringLiteral("asset"), action} +
			                args);
			const bool done = child.waitForFinished(30000);
			const auto output = child.readAllStandardOutput();
			lastReport = QJsonDocument::fromJson(output).object();
			if (!done || child.exitCode() != expected) {
				std::cerr << output.constData() << child.readAllStandardError().constData();
			}
			return expect(done && child.exitCode() == expected && QJsonDocument::fromJson(output).isObject(),
			              "audio project CLI contract");
		};
		const QString copy = QDir(temp.path()).filePath(QStringLiteral("copy.vsaudio"));
		ok &= cli({path}, 0);
		ok &= cli({path, QStringLiteral("--output"), copy, QStringLiteral("--dry-run")}, 0);
		ok &= expect(!QFileInfo::exists(copy), "CLI project dry run creates no file");
		ok &= cli({path, QStringLiteral("--output"), copy}, 0);
		ok &= expect(read(copy) == bytes, "CLI project copy preserves precision and metadata");
		ok &= cli({path, QStringLiteral("--output"), copy}, 4);
		ok &= cli({path, QStringLiteral("--output"), path, QStringLiteral("--overwrite")}, 4);
		const QString reversedPath = QDir(temp.path()).filePath(QStringLiteral("reversed.vsaudio"));
		ok &= cli(
		    {path, QStringLiteral("--operation"), QStringLiteral("reverse"), QStringLiteral("--output"), reversedPath},
		    0, QStringLiteral("audio-edit"));
		AudioProject reversed;
		ok &= expect(readAudioProject(reversedPath, &reversed, nullptr, &error) &&
		                 reversed.clip.samples ==
		                     applyAudioEdit(original.clip, {QStringLiteral("reverse")}).clip.samples &&
		                 reversed.metadata == original.metadata && reversed.firstFrame == 0 &&
		                 reversed.endFrame == original.clip.frameCount(),
		             "CLI native editing preserves float precision, headroom, and metadata");
		const QString restoredPath = QDir(temp.path()).filePath(QStringLiteral("recovered.vsaudio"));
		const QString resampledPath = QDir(temp.path()).filePath(QStringLiteral("resampled.vsaudio"));
		const QString floatPath = QDir(temp.path()).filePath(QStringLiteral("float.wav"));
		ok &= cli(
		    {path, QStringLiteral("--output"), floatPath, QStringLiteral("--wav-format"), QStringLiteral("float32")},
		    0);
		const auto floatExport = decodeAudioClip(floatPath, read(floatPath));
		ok &= expect(floatExport.succeeded() && floatExport.clip.samples == original.clip.samples &&
		                 lastReport.value(QStringLiteral("audioProject"))
		                         .toObject()
		                         .value(QStringLiteral("wavFormat"))
		                         .toString() == QStringLiteral("float32"),
		             "CLI project export preserves float headroom and reports its encoding");
		ok &=
		    cli({path, QStringLiteral("--output"), floatPath, QStringLiteral("--wav-format"), QStringLiteral("float32"),
		         QStringLiteral("--dither"), QStringLiteral("tpdf"), QStringLiteral("--overwrite")},
		        2);
		ok &= expect(read(floatPath).size() > 58, "invalid float dither leaves the existing output present");
		const QString precisePath = QDir(temp.path()).filePath(QStringLiteral("precise.wav"));
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("gain"), QStringLiteral("--db"),
		           QStringLiteral("0"), QStringLiteral("--output"), precisePath, QStringLiteral("--wav-format"),
		           QStringLiteral("pcm24"), QStringLiteral("--dither"), QStringLiteral("tpdf"),
		           QStringLiteral("--dither-seed"), QStringLiteral("18446744073709551615")},
		          0, QStringLiteral("audio-edit"));
		const auto precisionReport = lastReport.value(QStringLiteral("audioEdit")).toObject();
		ok &= expect(precisionReport.value(QStringLiteral("bitsPerSample")).toInt() == 24 &&
		                 precisionReport.value(QStringLiteral("ditherSeed")).toString() ==
		                     QStringLiteral("18446744073709551615") &&
		                 decodeAudioClip(precisePath, read(precisePath)).succeeded(),
		             "CLI integer precision and full-width seed are honored");
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("gain"), QStringLiteral("--output"),
		           reversedPath, QStringLiteral("--wav-format"), QStringLiteral("pcm24")},
		          2, QStringLiteral("audio-edit"));
		const QStringList conversion{path,
		                             QStringLiteral("--operation"),
		                             QStringLiteral("resample"),
		                             QStringLiteral("--sample-rate"),
		                             QStringLiteral("96000"),
		                             QStringLiteral("--output"),
		                             resampledPath};
		ok &= cli(conversion + QStringList{QStringLiteral("--dry-run")}, 0, QStringLiteral("audio-edit"));
		ok &= expect(!QFileInfo::exists(resampledPath) && lastReport.value(QStringLiteral("audioEdit"))
		                                                          .toObject()
		                                                          .value(QStringLiteral("outputFrames"))
		                                                          .toInteger() == 6,
		             "resample dry run reports the new frame count without writing output");
		ok &= cli(conversion, 0, QStringLiteral("audio-edit"));
		AudioProject converted;
		ok &=
		    expect(readAudioProject(resampledPath, &converted, nullptr, &error) && converted.clip.sampleRate == 96000 &&
		               converted.clip.frameCount() == 6 && converted.clip.channels == 2 && converted.firstFrame == 2 &&
		               converted.endFrame == 6 && converted.metadata == original.metadata &&
		               converted.sourcePath == original.sourcePath && read(path) == bytes,
		           "CLI resampling preserves provenance and metadata, maps selection by time, and protects its source");
		ok &= cli(conversion + QStringList{QStringLiteral("--start-frame"), QStringLiteral("0")}, 2,
		          QStringLiteral("audio-edit"));
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("resample"), QStringLiteral("--output"),
		           resampledPath},
		          2, QStringLiteral("audio-edit"));
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("resample"), QStringLiteral("--sample-rate"),
		           QStringLiteral("0"), QStringLiteral("--output"), resampledPath},
		          2, QStringLiteral("audio-edit"));
		const QString recoveryInput = writeAudioRecovery(original, recoveryDir, id, &error);
		ok &= cli({recoveryInput, QStringLiteral("--output"), restoredPath}, 0);
		AudioProject restored;
		ok &= expect(
		    readAudioProject(restoredPath, &restored, nullptr, &error) && identical(restored, original) &&
		        QFileInfo::exists(recoveryInput),
		    "CLI recovery preserves the original checkpoint and removes recovery-only metadata from the new project");
		const auto local = [&](const char* name) { return QDir(temp.path()).filePath(QString::fromLatin1(name)); };
		const QString emptyPath = local("new-empty.vsaudio"), silentPath = local("silence.wav");
		const QStringList newArgs{QStringLiteral("--sample-rate"), QStringLiteral("48000"),
		                          QStringLiteral("--channels"),    QStringLiteral("2"),
		                          QStringLiteral("--output"),      emptyPath};
		ok &= cli(newArgs + QStringList{QStringLiteral("--dry-run")}, 0, QStringLiteral("audio-new"));
		ok &= expect(
		    !QFileInfo::exists(emptyPath) &&
		        !lastReport.value(QStringLiteral("audioNew")).toObject().value(QStringLiteral("written")).toBool(),
		    "new audio dry run has no file or written claim");
		ok &= cli(newArgs, 0, QStringLiteral("audio-new"));
		AudioProject newDocument;
		ok &= expect(readAudioProject(emptyPath, &newDocument, nullptr, &error) && newDocument.clip.samples.isEmpty() &&
		                 newDocument.clip.sampleRate == 48000 && newDocument.clip.channels == 2,
		             "CLI creates an empty native document with its requested format");
		ok &= cli(newArgs, 4, QStringLiteral("audio-new"));
		ok &= cli({QStringLiteral("--output"), silentPath}, 4, QStringLiteral("audio-new"));
		ok &= expect(!QFileInfo::exists(silentPath), "empty WAV creation fails without writing");
		ok &= cli({QStringLiteral("--output"), silentPath, QStringLiteral("--sample-rate"), QStringLiteral("8000"),
		           QStringLiteral("--channels"), QStringLiteral("2"), QStringLiteral("--frames"), QStringLiteral("4")},
		          0, QStringLiteral("audio-new"));
		const auto silence = decodeAudioClip(silentPath, read(silentPath));
		ok &= expect(silence.succeeded() && silence.clip.samples == QVector<float>(8, 0.0f) &&
		                 silence.clip.sampleRate == 8000 && silence.clip.channels == 2,
		             "CLI creates the requested silent frame count without channel drift");
		ok &= cli({QStringLiteral("--output"), local("invalid.vsaudio"), QStringLiteral("--sample-rate"),
		           QStringLiteral("0")},
		          2, QStringLiteral("audio-new"));
		const QString assembled = local("assembled.vsaudio");
		const QStringList pasteArgs{emptyPath,
		                            QStringLiteral("--operation"),
		                            QStringLiteral("paste"),
		                            QStringLiteral("--paste-input"),
		                            path,
		                            QStringLiteral("--output"),
		                            assembled};
		ok &= cli(pasteArgs + QStringList{QStringLiteral("--dry-run")}, 0, QStringLiteral("audio-edit"));
		ok &= expect(!QFileInfo::exists(assembled), "CLI paste dry run creates no file");
		ok &= cli(pasteArgs, 0, QStringLiteral("audio-edit"));
		AudioProject authored;
		ok &= expect(
		    readAudioProject(assembled, &authored, nullptr, &error) && authored.clip.samples == original.clip.samples &&
		        authored.firstFrame == 0 && authored.endFrame == 3 &&
		        lastReport.value(QStringLiteral("audioEdit"))
		                .toObject()
		                .value(QStringLiteral("insertedFrames"))
		                .toInteger() == 3,
		    "CLI paste into an empty project preserves float samples, selects insertion, and reports its size");
		const QString insertedPath = local("inserted.vsaudio");
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("insert-silence"),
		           QStringLiteral("--start-frame"), QStringLiteral("1"), QStringLiteral("--frames"),
		           QStringLiteral("2"), QStringLiteral("--output"), insertedPath},
		          0, QStringLiteral("audio-edit"));
		ok &= expect(readAudioProject(insertedPath, &authored, nullptr, &error) && authored.clip.frameCount() == 5 &&
		                 authored.clip.samples.mid(2, 4) == QVector<float>(4, 0.0f) &&
		                 authored.clip.samples.mid(6) == original.clip.samples.mid(2) && authored.firstFrame == 1 &&
		                 authored.endFrame == 3,
		             "CLI silence insertion shifts complete frames and selects the inserted range");
		const QString mixPath = local("mixed.vsaudio");
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("mix"), QStringLiteral("--paste-input"), path,
		           QStringLiteral("--start-frame"), QStringLiteral("2"), QStringLiteral("--output"), mixPath},
		          0, QStringLiteral("audio-edit"));
		ok &= expect(readAudioProject(mixPath, &authored, nullptr, &error) && authored.clip.frameCount() == 5 &&
		                 authored.clip.samples[5] == 0.25f && authored.clip.samples[6] == 2.25f &&
		                 authored.clip.samples[7] == -3.125f && authored.firstFrame == 2 && authored.endFrame == 5,
		             "CLI mix retains headroom and extends with all source channels");
		const QString deletedPath = local("deleted.vsaudio");
		ok &= cli(
		    {path, QStringLiteral("--operation"), QStringLiteral("delete"), QStringLiteral("--output"), deletedPath}, 0,
		    QStringLiteral("audio-edit"));
		ok &= expect(readAudioProject(deletedPath, &authored, nullptr, &error) && authored.clip.samples.isEmpty() &&
		                 authored.firstFrame == 0 && authored.endFrame == 0,
		             "CLI delete-all saves an empty native document");
		ok &= cli({emptyPath, QStringLiteral("--operation"), QStringLiteral("paste"), QStringLiteral("--paste-input"),
		           silentPath, QStringLiteral("--output"), assembled, QStringLiteral("--overwrite")},
		          4, QStringLiteral("audio-edit"));
		ok &= expect(readAudioProject(assembled, &authored, nullptr, &error) &&
		                 authored.clip.samples == original.clip.samples,
		             "format mismatch cannot overwrite an existing output");
		ok &= cli({emptyPath, QStringLiteral("--operation"), QStringLiteral("paste"), QStringLiteral("--paste-input"),
		           path, QStringLiteral("--output"), path, QStringLiteral("--overwrite")},
		          1, QStringLiteral("audio-edit"));
		ok &= expect(read(path) == bytes, "CLI paste never overwrites its secondary input");
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("paste"), QStringLiteral("--output"), assembled},
		          2, QStringLiteral("audio-edit"));
		ok &= cli({path, QStringLiteral("--operation"), QStringLiteral("mix"), QStringLiteral("--paste-input"), path,
		           QStringLiteral("--end-frame"), QStringLiteral("1"), QStringLiteral("--output"), assembled},
		          2, QStringLiteral("audio-edit"));
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
