#include "core/audio_clip.h"
#include "core/audio_delivery.h"
#include "core/audio_project.h"
#include "core/audio_resample.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
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
quint32 u32(const QByteArray& bytes, qsizetype at) { return qFromLittleEndian<quint32>(bytes.constData() + at); }
qsizetype findChunk(const QByteArray& bytes, const QByteArray& name)
{
	for (qsizetype at = 12; at + 8 <= bytes.size();) {
		if (bytes.mid(at, 4) == name) {
			return at;
		}
		const auto size = u32(bytes, at + 4);
		at += 8 + size + (size & 1);
	}
	return -1;
}
QByteArray chunk(const QByteArray& name, QByteArray payload)
{
	QByteArray out = name + QByteArray(4, '\0');
	qToLittleEndian<quint32>(quint32(payload.size()), out.data() + 4);
	out += payload;
	if (payload.size() & 1) {
		out += '\0';
	}
	return out;
}
QByteArray appendChunk(QByteArray wav, const QByteArray& extra)
{
	wav += extra;
	qToLittleEndian<quint32>(quint32(wav.size() - 8), wav.data() + 4);
	return wav;
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
	bool ok = true;
	QString error;
	AudioClip clip{1, 22050, QVector<float>(20, 0.125f)};
	clip.markers = {
	    {{7, 2, QStringLiteral("Start")}, {9, 5, QString::fromUtf8("风 café")}, {11, 12, QStringLiteral("End")}},
	    AudioLoop{4, 16}};
	ok &= expect(validateAudioClip(clip).isEmpty(), "typed cue and loop model validates complete frames");
	AudioClip invalidPlan = clip;
	invalidPlan.markers.cues[0].frame = std::numeric_limits<qint64>::max();
	ok &= expect(!planAudioDelivery(invalidPlan, {AudioDeliveryPreset::Quake, {}}).error.isEmpty(),
	             "delivery validates marker bounds before any time-mapping arithmetic");
	const auto trimmed = applyAudioEdit(clip, {QStringLiteral("trim"), 3, 14});
	ok &= expect(trimmed.succeeded() &&
	                 trimmed.clip.markers.cues ==
	                     QVector<AudioCue>{{9, 2, QString::fromUtf8("风 café")}, {11, 9, QStringLiteral("End")}} &&
	                 trimmed.clip.markers.loop == AudioLoop{1, 11},
	             "trim rebases retained cues and intersects the loop");
	const auto removed = applyAudioEdit(clip, {QStringLiteral("delete"), 3, 8});
	ok &= expect(removed.succeeded() && removed.clip.markers.cues.size() == 2 &&
	                 removed.clip.markers.cues[1].frame == 7 && removed.clip.markers.loop == AudioLoop{3, 11},
	             "delete drops affected cues and collapses/retimes loop boundaries");
	ok &= expect(applyAudioEdit(clip, {QStringLiteral("delete"), 0, 20}).clip.markers.empty(),
	             "delete all removes all authored markers");
	const auto inserted = insertAudioSilence(clip, 4, 3);
	ok &= expect(inserted.succeeded() && inserted.clip.markers.loop == AudioLoop{7, 19} &&
	                 inserted.clip.markers.cues[1].frame == 8,
	             "insertion at loop start shifts the loop and following cues");
	ok &= expect(insertAudioSilence(clip, 16, 3).clip.markers.loop == AudioLoop{4, 16} &&
	                 insertAudioSilence(clip, 6, 3).clip.markers.loop == AudioLoop{4, 19},
	             "loop end excludes boundary insertion while insertion inside expands it");
	const auto reversed = applyAudioEdit(clip, {QStringLiteral("reverse"), 0, 20});
	ok &= expect(reversed.succeeded() && reversed.clip.markers.cues[0].id == 11 &&
	                 reversed.clip.markers.cues[0].frame == 7 && reversed.clip.markers.loop == AudioLoop{4, 16},
	             "reverse maps cue sample positions and both loop boundaries");
	ok &= expect(!applyAudioEdit(clip, {QStringLiteral("reverse"), 2, 8}).clip.markers.loop &&
	                 applyAudioEdit(clip, {QStringLiteral("reverse"), 6, 8}).clip.markers.loop == clip.markers.loop,
	             "partial loop overlap clears a non-contiguous loop; reversing strictly inside preserves its extent");
	AudioClip incoming{1, 22050, QVector<float>(3, 0.25f)};
	incoming.markers = {{{7, 0, QStringLiteral("Insert")}}, AudioLoop{0, 3}};
	const auto pasted = replaceAudioRange(clip, 4, 16, incoming);
	ok &= expect(
	    pasted.succeeded() && pasted.clip.markers.cues.size() == 2 && pasted.clip.markers.cues[1].id == 1 &&
	        pasted.clip.markers.cues[1].frame == 4 && pasted.clip.markers.loop == AudioLoop{4, 7},
	    "replacement preserves a loop over replacement audio and remaps conflicting incoming IDs deterministically");
	const auto mixed = mixAudioClip(clip, 18, incoming);
	ok &= expect(mixed.succeeded() && mixed.clip.markers.cues.size() == 4 &&
	                 mixed.clip.markers.loop == clip.markers.loop && mixed.clip.markers.cues.last().frame == 18,
	             "mix imports cues and retains the destination loop");
	const auto resampled = resampleAudioClip(clip, 44100);
	ok &= expect(resampled.succeeded() && resampled.clip.markers.cues[1].frame == 10 &&
	                 resampled.clip.markers.loop == AudioLoop{8, 32},
	             "resampling maps cues and loop boundaries in time");
	ok &= expect(!resampleAudioMarkers({{}, AudioLoop{1, 2}}, 20, 1, 22050, 1102).loop,
	             "a collapsed resampled loop is removed");
	for (const auto& op : {QStringLiteral("gain"), QStringLiteral("fade-in"), QStringLiteral("invert"),
	                       QStringLiteral("silence"), QStringLiteral("stereo")}) {
		ok &= expect(applyAudioEdit(clip, {op}).clip.markers == clip.markers,
		             "nonstructural edits and channel conversion preserve markers");
	}
	AudioProject project{clip, 0, 20, QStringLiteral("Markers"), {}, {{QStringLiteral("custom"), 17}}};
	const QByteArray native = encodeAudioProject(project, &error);
	AudioProject decodedProject;
	ok &= expect(!native.isEmpty() && u32(native, 8) == 2 && decodeAudioProject(native, &decodedProject, &error) &&
	                 decodedProject.clip.markers == clip.markers && decodedProject.metadata == project.metadata,
	             "native version 2 preserves typed markers and opaque metadata");
	project.clip.markers = {};
	ok &= expect(u32(encodeAudioProject(project), 8) == 1, "marker-free native documents remain version 1 compatible");
	AudioMarkers parsed;
	ok &= expect(parseAudioMarkersJson(audioMarkersJson(clip.markers), 20, &parsed, &error) && parsed == clip.markers,
	             "typed JSON round-trip preserves cue IDs and Unicode names");
	auto unsupported = audioMarkersJson(clip.markers);
	unsupported.insert(QStringLiteral("loop"), QJsonObject{{QStringLiteral("first"), 1},
	                                                       {QStringLiteral("end"), 10},
	                                                       {QStringLiteral("type"), QStringLiteral("backward")}});
	ok &= expect(!parseAudioMarkersJson(unsupported, 20, &parsed, &error),
	             "unknown loop properties cannot silently request different semantics");
	QByteArray mislabeled = native;
	qToLittleEndian<quint32>(1, mislabeled.data() + 8);
	mislabeled.chop(32);
	mislabeled += QCryptographicHash::hash(mislabeled, QCryptographicHash::Sha256);
	ok &= expect(!decodeAudioProject(mislabeled, &decodedProject, &error),
	             "version 1 cannot silently ignore typed markers from a version 2 file");
	for (auto bad : QVector<AudioMarkers>{{{{1, 20, {}}}, {}},
	                                      {{{1, -1, {}}}, {}},
	                                      {{{1, 1, {}}, {1, 2, {}}}, {}},
	                                      {{}, AudioLoop{4, 4}},
	                                      {{}, AudioLoop{0, 21}},
	                                      {{{1, 0, QString(129, QLatin1Char('x'))}}, {}}}) {
		ok &= expect(!validateAudioMarkers(bad, 20).isEmpty(), "invalid frames, loop spans, IDs and labels fail");
	}
	const auto wave = encodeAudioWav(clip, {AudioWavFormat::Float32}, &error);
	const auto sample = findChunk(wave, "smpl"), cue = findChunk(wave, "cue "), charset = findChunk(wave, "CSET");
	ok &= expect(sample >= 0 && cue >= 0 && charset >= 0 && u32(wave, sample + 8 + 44) == 4 &&
	                 u32(wave, sample + 8 + 48) == 15 && u32(wave, cue + 8) == 3 &&
	                 qFromLittleEndian<quint16>(wave.constData() + charset + 8) == 65001,
	             "independent byte offsets prove inclusive smpl end, cue count and UTF-8 CSET");
	const auto decoded = decodeAudioClip(QStringLiteral("markers.wav"), wave);
	ok &= expect(decoded.succeeded() && decoded.clip.markers == clip.markers && decoded.clip.samples == clip.samples,
	             "WAV markers and Unicode labels round-trip without changing samples");
	const auto quake = encodeAudioWav(clip, {AudioWavFormat::Pcm16, false, 0, AudioWavMarkers::Quake}, &error);
	const auto quakeCue = findChunk(quake, "cue "), list = findChunk(quake, "LIST");
	ok &= expect(quakeCue >= 0 && list >= 0 && u32(quake, quakeCue + 32) == 4 && u32(quake, list + 24) == 12 &&
	                 quake.mid(list + 28, 4) == "mark",
	             "original Quake/II reader offsets see exactly the authored loop start and length");
	ok &= expect(isGeneratedIntegerAudioWav(quake) &&
	                 decodeAudioClip(QStringLiteral("quake.wav"), quake).clip.markers == clip.markers,
	             "staging validation accepts bounded markers and legacy loop sentinels are not duplicated as cues");
	QByteArray legacy = quake;
	const auto samplerAt = findChunk(legacy, "smpl");
	legacy.remove(samplerAt, 8 + u32(legacy, samplerAt + 4));
	qToLittleEndian<quint32>(quint32(legacy.size() - 8), legacy.data() + 4);
	ok &= expect(decodeAudioClip(QStringLiteral("legacy.wav"), legacy).clip.markers == clip.markers,
	             "an independent legacy cue/ltxt loop imports without an smpl chunk");
	QByteArray aligned = wave;
	qToLittleEndian<quint32>(8, aligned.data() + cue + 8 + 4 + 16);
	ok &= expect(decodeAudioClip(QStringLiteral("aligned.wav"), aligned).clip.markers == clip.markers,
	             "the 1994 PCM aligned byte offset agrees with the absolute sample position");
	qToLittleEndian<quint32>(7, aligned.data() + cue + 8 + 4 + 16);
	ok &= expect(!decodeAudioClip(QStringLiteral("bad-offset.wav"), aligned).succeeded(),
	             "conflicting byte and sample offsets fail instead of moving a cue");
	ok &= expect(encodeAudioWav(clip, {AudioWavFormat::Pcm16, false, 0, AudioWavMarkers(99)}).isEmpty(),
	             "invalid marker output options fail even when the caller has no error sink");
	AudioClip bounded = clip;
	bounded.markers.cues.clear();
	for (int index = 0; index < AudioCueLimit; ++index) {
		bounded.markers.cues.append({quint32(index + 1), index % 20, QString(64, QLatin1Char('x'))});
	}
	parseAudioMarkersJson(audioMarkersJson(bounded.markers), 20, &bounded.markers);
	const auto maximum = encodeAudioWav(bounded, {AudioWavFormat::Pcm16, false, 0, AudioWavMarkers::Quake}, &error);
	ok &= expect(!maximum.isEmpty() &&
	                 decodeAudioClip(QStringLiteral("maximum.wav"), maximum).clip.markers == bounded.markers,
	             "all 256 authored cues plus a legacy loop sentinel and 16 KiB of names round-trip at the limits");
	ok &= expect(!mixAudioClip(bounded, 0, incoming).succeeded() && bounded.markers.cues.size() == AudioCueLimit,
	             "mixing beyond the cue limit fails without modifying the caller");
	bounded.markers.cues[0].name += QLatin1Char('x');
	ok &= expect(!validateAudioClip(bounded).isEmpty(),
	             "total UTF-8 label bytes are bounded independently of per-name length");
	AudioClip oneShot = clip;
	oneShot.markers.loop.reset();
	const auto quakeOneShot = renderAudioDelivery(oneShot, {AudioDeliveryPreset::Quake, {}});
	ok &= expect(quakeOneShot.succeeded() && findChunk(quakeOneShot.bytes, "cue ") < 0 &&
	                 quakeOneShot.plan.outputCues == 0 && !quakeOneShot.plan.markerSummary.isEmpty(),
	             "Quake delivery explicitly omits cue-only metadata so one-shots cannot accidentally loop");
	const auto quakeDelivery = renderAudioDelivery(clip, {AudioDeliveryPreset::Quake2, {}});
	ok &= expect(quakeDelivery.succeeded() && quakeDelivery.plan.outputLoop &&
	                 decodeAudioClip(QStringLiteral("q2.wav"), quakeDelivery.bytes).clip.markers == clip.markers,
	             "Quake II delivery preserves compatible loop and cues");
	AudioClip doom = clip;
	doom.samples.resize(80);
	const auto dmx = renderAudioDelivery(doom, {AudioDeliveryPreset::Doom, {}});
	ok &= expect(dmx.succeeded() && !dmx.plan.outputLoop && dmx.plan.outputCues == 0 && isGeneratedAudioDmx(dmx.bytes),
	             "DMX explicitly drops metadata without disturbing sample encoding");
	QByteArray invalid = wave;
	qToLittleEndian<quint32>(1, invalid.data() + sample + 8 + 40);
	ok &= expect(!decodeAudioClip(QStringLiteral("backward.wav"), invalid).succeeded(),
	             "unsupported loop modes fail clearly instead of silently changing playback semantics");
	invalid = wave;
	qToLittleEndian<quint32>(9999999, invalid.data() + cue + 8);
	ok &= expect(!decodeAudioClip(QStringLiteral("oversized.wav"), invalid).succeeded(),
	             "hostile cue counts cannot allocate");
	invalid = appendChunk(wave, chunk("smpl", wave.mid(sample + 8, u32(wave, sample + 4))));
	ok &= expect(!decodeAudioClip(QStringLiteral("duplicate.wav"), invalid).succeeded(),
	             "duplicate sampler metadata is rejected");
	invalid = appendChunk(wave, chunk("LIST", QByteArrayLiteral("adtl") + QByteArray(AudioMarkerByteLimit, 'x')));
	ok &= expect(!decodeAudioClip(QStringLiteral("oversized-list.wav"), invalid).succeeded() &&
	                 !isGeneratedIntegerAudioWav(invalid),
	             "associated marker bytes are bounded");
	ok &= expect(applyAudioEdit(clip, {QStringLiteral("delete"), 0, 4}, {[]() { return true; }}).cancelled &&
	                 clip.markers.loop == AudioLoop{4, 16},
	             "cancelled edits never mutate the caller's marker state");
	const QString root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT", QDir::currentPath());
	QDir().mkpath(root);
	QTemporaryDir temp(QDir(root).filePath(QStringLiteral("audio-markers-XXXXXX")));
	if (!expect(temp.isValid(), "create bounded marker fixtures in the selected test area")) {
		return 1;
	}
	project.clip = clip;
	const QString browserInput = temp.filePath(QStringLiteral("browser-float.wav"));
	const QString browserOutput = temp.filePath(QStringLiteral("browser-pcm16.wav"));
	ok &= expect(write(browserInput, wave), "write package-browser conversion fixture");
	PackageArchive browser;
	ok &= expect(browser.load(temp.path(), &error), "open independent browser fixture folder");
	const auto browserExport =
	    exportPackageAudioToWav(browser, QStringLiteral("browser-float.wav"), browserOutput, false, false);
	ok &= expect(browserExport.error.isEmpty() &&
	                 decodeAudioClip(browserOutput, read(browserOutput)).clip.markers == clip.markers,
	             "Audio browser PCM conversion preserves typed cues and loops");
	const QString legacyInput = temp.filePath(QStringLiteral("browser-quake.wav"));
	const QString legacyOutput = temp.filePath(QStringLiteral("browser-quake16.wav"));
	ok &= expect(write(legacyInput, encodeAudioWav(clip, {AudioWavFormat::Pcm8, false, 0, AudioWavMarkers::Quake})) &&
	                 browser.load(temp.path(), &error),
	             "prepare a legacy Quake conversion fixture");
	const auto legacyExport =
	    exportPackageAudioToWav(browser, QStringLiteral("browser-quake.wav"), legacyOutput, false, false);
	const auto legacyBytes = read(legacyOutput);
	ok &= expect(legacyExport.error.isEmpty() &&
	                 decodeAudioClip(legacyOutput, legacyBytes).clip.markers == clip.markers &&
	                 findChunk(legacyBytes, "cue ") >= 0 && u32(legacyBytes, findChunk(legacyBytes, "cue ") + 32) == 4,
	             "browser conversion retains the legacy first-cue loop convention");
	const QString recovered =
	    writeAudioRecovery(project, temp.path(), QUuid::createUuid().toString(QUuid::WithoutBraces), &error);
	ok &= expect(!recovered.isEmpty() && readAudioProject(recovered, &decodedProject, nullptr, &error) &&
	                 decodedProject.clip.markers == clip.markers,
	             "recovery checkpoints preserve typed markers through the versioned project service");
	if (argc > 1) {
		QByteArray stdoutBytes;
		const auto cli = [&](const QString& action, const QStringList& args, int expected) {
			QProcess process;
			process.setWorkingDirectory(temp.path());
			process.start(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath(),
			              QStringList{QStringLiteral("--cli"), QStringLiteral("asset"), action} + args +
			                  QStringList{QStringLiteral("--json")});
			const bool finished = process.waitForFinished(30000);
			stdoutBytes = process.readAllStandardOutput();
			const bool passed =
			    finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected;
			if (!passed) {
				std::cerr << stdoutBytes.constData() << process.readAllStandardError().constData();
			}
			return passed;
		};
		const QString input = temp.filePath(QStringLiteral("source.wav"));
		const QString manifest = temp.filePath(QStringLiteral("markers.json"));
		const QString output = temp.filePath(QStringLiteral("marked.vsaudio"));
		AudioClip plain = clip;
		plain.markers = {};
		const auto original = encodeAudioWav(plain, {AudioWavFormat::Float32});
		ok &= expect(write(input, original) && write(manifest, QJsonDocument(audioMarkersJson(clip.markers)).toJson()),
		             "write independent CLI source and marker manifest");
		const QStringList changes{input, QStringLiteral("--markers"), manifest, QStringLiteral("--output"), output};
		ok &= expect(cli(QStringLiteral("audio-markers"), changes + QStringList{QStringLiteral("--dry-run")}, 0) &&
		                 !QFileInfo::exists(output) &&
		                 QJsonDocument::fromJson(stdoutBytes)
		                         .object()
		                         .value(QStringLiteral("audioMarkers"))
		                         .toObject()
		                         .value(QStringLiteral("markers"))
		                         .toObject() == audioMarkersJson(clip.markers),
		             "marker dry run reports the entire proposed metadata and writes no destination");
		ok &= expect(cli(QStringLiteral("audio-markers"), changes, 0) &&
		                 readAudioProject(output, &decodedProject, nullptr, &error) &&
		                 decodedProject.clip.markers == clip.markers && decodedProject.clip.samples == plain.samples &&
		                 read(input) == original,
		             "CLI marker authoring changes no source samples and saves exact native metadata");
		ok &= expect(cli(QStringLiteral("audio-markers"),
		                 {temp.path(), QStringLiteral("--entry"), QStringLiteral("source.wav")}, 0),
		             "package-folder marker inspection uses the common decoder");
		ok &= expect(cli(QStringLiteral("audio-markers"), changes, 4),
		             "existing marker outputs require explicit overwrite");
		ok &= expect(cli(QStringLiteral("audio-markers"), {input, QStringLiteral("--markers"), manifest}, 2),
		             "marker changes require a separate output");
		ok &= expect(cli(QStringLiteral("audio-markers"),
		                 changes + QStringList{QStringLiteral("--loop-type"), QStringLiteral("backward")}, 2),
		             "unsupported CLI marker options fail explicitly");
		ok &= expect(cli(QStringLiteral("audio-markers"),
		                 {input, QStringLiteral("--markers"), manifest, QStringLiteral("--markers"), manifest,
		                  QStringLiteral("--output"), output},
		                 2),
		             "repeated marker options are not silently ignored");
		ok &= expect(cli(QStringLiteral("audio-markers"),
		                 {output, QStringLiteral("--markers=") + manifest, QStringLiteral("--output"), input,
		                  QStringLiteral("--overwrite")},
		                 4) &&
		                 read(input) == original,
		             "native provenance protects the original sound during explicit overwrite");
		const QString edited = temp.filePath(QStringLiteral("edited.vsaudio"));
		ok &= expect(cli(QStringLiteral("audio-edit"),
		                 {output, QStringLiteral("--operation"), QStringLiteral("trim"),
		                  QStringLiteral("--start-frame"), QStringLiteral("3"), QStringLiteral("--end-frame"),
		                  QStringLiteral("14"), QStringLiteral("--output"), edited},
		                 0) &&
		                 readAudioProject(edited, &decodedProject, nullptr, &error) &&
		                 decodedProject.clip.markers == trimmed.clip.markers,
		             "CLI edits transform markers through the same service as GUI edits");
		const QString delivery = temp.filePath(QStringLiteral("quake2.wav"));
		ok &= expect(
		    cli(QStringLiteral("audio-export"),
		        {output, QStringLiteral("--preset"), QStringLiteral("quake2"), QStringLiteral("--output"), delivery},
		        0) &&
		        decodeAudioClip(delivery, read(delivery)).clip.markers == clip.markers &&
		        QJsonDocument::fromJson(stdoutBytes)
		            .object()
		            .value(QStringLiteral("audioExport"))
		            .toObject()
		            .value(QStringLiteral("outputLoop"))
		            .toBool(),
		    "CLI game export preserves the loop and exposes its delivery disposition");
		ok &=
		    expect(write(manifest, QJsonDocument(unsupported).toJson()) &&
		               cli(QStringLiteral("audio-markers"), changes + QStringList{QStringLiteral("--overwrite")}, 4) &&
		               read(input) == original,
		           "invalid manifests cannot mutate a source or replace the existing document");
	}
	return ok ? 0 : 1;
}
