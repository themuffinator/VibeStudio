#include "core/audio_effect_preset.h"
#include "core/audio_session_io.h"
#include "core/audio_stems.h"
#include "tests/audio_loop_test_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <iostream>
#include <utility>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *text)
{
	if (!value) {
		std::cerr << text << '\n';
	}
	return value;
}
QByteArray read(const QString &path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	if (argc != 2) {
		return EXIT_FAILURE;
	}
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root)) {
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("session-cli-XXXXXX"));
	if (!temporary.isValid()) {
		return EXIT_FAILURE;
	}
	const auto file = [&](const char *name) { return QDir(temporary.path()).filePath(QLatin1String(name)); };
	bool ok = true;
	QJsonObject last;
	const auto run = [&](const QStringList &options, int expected = 0) {
		QProcess process;
		process.setWorkingDirectory(temporary.path());
		process.start(QString::fromLocal8Bit(argv[1]), QStringList{"--cli", "--json", "--settings-file",
		                                                           file("settings.ini"), "asset", "audio-session"} +
		                                                   options);
		if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
			process.kill();
			process.waitForFinished();
			std::cerr << "CLI timed out\n";
			return false;
		}
		const auto stdoutBytes = process.readAllStandardOutput();
		last = QJsonDocument::fromJson(stdoutBytes).object();
		const bool passed =
		    process.exitStatus() == QProcess::NormalExit && process.exitCode() == expected && !last.isEmpty();
		if (!passed) {
			std::cerr << options.join(' ').toStdString() << " exit " << process.exitCode() << '\n'
			          << stdoutBytes.constData() << process.readAllStandardError().constData();
		}
		return passed;
	};
	ok &= expect(
	    run({"new", "--sample-rate", "48000", "--name", "Fixture", "--output", file("empty.vssession"), "--dry-run"}) &&
	        !QFile::exists(file("empty.vssession")),
	    "CLI dry-run creates no native file");
	ok &= expect(run({"new", "--sample-rate", "48000", "--tempo", "90", "--output", file("empty.vssession")}),
	             "CLI new session");
	AudioClip original{1, 48000, {0.25f, -0.5f, 0.75f, -1}};
	AudioWavOptions precision;
	precision.format = AudioWavFormat::Float32;
	const auto originalBytes = encodeAudioWav(original, precision);
	ok &= expect(write(file("voice.wav"), originalBytes), "write original fixture");
	ok &= expect(run({"import", file("empty.vssession"), "--input", file("voice.wav"), "--at-frame", "2", "--output",
	                  file("music.vssession")}),
	             "CLI import and position");
	const QString track = last.value("addedTrackId").toString(), clip = last.value("addedClipId").toString();
	ok &= expect(!track.isEmpty() && !clip.isEmpty(), "CLI returns stable added identities");
	if (!ok)
		return EXIT_FAILURE;
	ok &= expect(run({"edit", file("music.vssession"), "--operation", "track", "--track", track, "--pan", "-1", "--db",
	                  "-6", "--output", file("music.vssession"), "--overwrite"}),
	             "CLI guarded in-place mixer edit with negative values");
	ok &= expect(run({"automation", file("music.vssession"), "--track", track, "--gain-points", "0:0,4:-6",
	                  "--pan-points", "0:-1,5:1", "--output", file("automated.vssession")}),
	             "CLI gain and pan automation");
	ok &= expect(run({"inspect", file("automated.vssession")}) &&
	                 last.value("session").toObject().value("frames").toInteger() == 6,
	             "CLI inspection keeps exact frames");
	ok &= expect(
	    run({"transport", file("automated.vssession"), "--frames", "100", "--block-frames", "1", "--loop", "true"}),
	    "CLI device-free transport diagnostics");
	const auto stream = last.value("transport").toObject();
	ok &= expect(stream.value("framesRendered").toInteger() == 100 && stream.value("position").toInteger() == 4 &&
	                 stream.value("loops").toInteger() == 16 && !stream.value("deviceOpened").toBool(true),
	             "CLI transport reports exact loop clock without device activation");
	ok &= expect(
	    run({"transport", file("automated.vssession"), "--frames", "100", "--block-frames", "17", "--loop", "true"}) &&
	        last.value("transport").toObject() == stream,
	    "CLI signal digest and counters are block-size invariant");
	ok &= expect(run({"transport", file("automated.vssession"), "--frames", "100"}) &&
	                 last.value("transport").toObject().value("framesRendered").toInteger() == 6 &&
	                 last.value("transport").toObject().value("ended").toBool(),
	             "CLI finite range ends without synthetic tail samples");
	ok &= expect(
	    run({"transport", file("automated.vssession"), "--frames", "0"}, 2) &&
	        run({"transport", file("automated.vssession"), "--frames", "1", "--output", file("forbidden.vssession")},
	            2) &&
	        !QFile::exists(file("forbidden.vssession")),
	    "CLI transport rejects empty work and output mutation flags");
	ok &=
	    expect(run({"mixdown", file("automated.vssession"), "--wav-format", "float32", "--output", file("mix.wav")}) &&
	               last.value("mixdown").toObject().value("frames").toInteger() == 6,
	           "CLI streaming mixdown");
	AudioSession saved;
	QString error;
	ok &= expect(readAudioSession(file("automated.vssession"), &saved, nullptr, &error),
	             "independent native read of CLI result");
	const auto rendered = renderAudioSession(saved);
	ok &= expect(read(file("mix.wav")) == encodeAudioWav(rendered.clip, precision),
	             "CLI uses exactly the shared session renderer");
	const auto before = read(file("automated.vssession"));
	ok &= expect(run({"edit", file("automated.vssession"), "--operation", "region", "--track", track, "--clip", clip,
	                  "--offset-frame", "99", "--output", file("automated.vssession"), "--overwrite"},
	                 4) &&
	                 read(file("automated.vssession")) == before,
	             "invalid source trim preserves session");
	ok &= expect(run({"edit", file("automated.vssession"), "--operation", "master", "--db", "0", "--output",
	                  file("automated.vssession")},
	                 2) &&
	                 read(file("automated.vssession")) == before,
	             "in-place save requires explicit overwrite");
	ok &= expect(run({"inspect", file("automated.vssession"), "--output", file("unexpected.vssession")}, 2) &&
	                 !QFile::exists(file("unexpected.vssession")),
	             "inspect rejects write-only options");
	ok &= expect(
	    run({"new", "--sample-rate", "44100", "--sample-rate", "48000", "--output", file("duplicate.vssession")}, 2),
	    "duplicate flags rejected");
	ok &= expect(run({"automation", file("automated.vssession"), "--track", track, "--gain-points", "0:0,0:1",
	                  "--output", file("bad.vssession")},
	                 4) &&
	                 !QFile::exists(file("bad.vssession")),
	             "non-increasing automation rejected before write");
	ok &= expect(run({"mixdown", file("automated.vssession"), "--output", file("voice.wav"), "--overwrite"}, 4) &&
	                 read(file("voice.wav")) == originalBytes,
	             "source audio protected from session export");
	ok &= expect(run({"new", "--sample-rate", "24000", "--output", file("rate.vssession")}),
	             "create different-rate fixture");
	ok &= expect(
	    run({"import", file("rate.vssession"), "--input", file("voice.wav"), "--output", file("converted.vssession")},
	        4) &&
	        !QFile::exists(file("converted.vssession")),
	    "resampling is explicit");
	ok &= expect(run({"import", file("rate.vssession"), "--input", file("voice.wav"), "--resample", "--output",
	                  file("converted.vssession")}),
	             "explicit resampling reuses shared converter");
	ok &= expect(run({"edit", file("music.vssession"), "--operation", "split", "--track", track, "--clip", clip,
	                  "--at-frame", "4", "--output", file("split.vssession")}),
	             "CLI nondestructive split");
	const auto splitTracks = last.value("session").toObject().value("tracks").toArray();
	ok &= expect(splitTracks.size() == 1 && splitTracks[0].toObject().value("regions").toArray().size() == 2,
	             "CLI split retains independent regions");
	ok &= expect(read(file("voice.wav")) == originalBytes, "all CLI operations preserve original media");
	ok &= expect(run({"edit", file("music.vssession"), "--operation", "add-bus", "--name", "Dialogue", "--output",
	                  file("routed.vssession")}),
	             "CLI adds a bus through shared session edits");
	const auto bus = last.value("addedTrackId").toString();
	ok &= expect(!bus.isEmpty() &&
	                 run({"edit", file("routed.vssession"), "--operation", "routing", "--track", track, "--output-bus",
	                      bus, "--invert-left", "true", "--output", file("routed.vssession"), "--overwrite"}),
	             "CLI routes a strip to its bus");
	ok &=
	    expect(run({"edit", file("routed.vssession"), "--operation", "send", "--track", track, "--target-bus", "master",
	                "--db", "-12", "--pre-fader", "true", "--output", file("routed.vssession"), "--overwrite"}),
	           "CLI adds an independent pre-fader send");
	ok &= expect(readAudioSession(file("routed.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].routing.outputId == bus && saved.tracks[0].routing.invertLeft &&
	                 saved.tracks[0].routing.sends.size() == 1 && saved.tracks[0].routing.sends[0].preFader,
	             "native file preserves CLI routing, polarity and send state");
	ok &=
	    expect(run({"mixdown", file("routed.vssession"), "--output", file("routed.wav"), "--wav-format", "float32"}) &&
	               read(file("routed.wav")) == encodeAudioWav(renderAudioSession(saved).clip, precision),
	           "CLI routed WAV matches shared renderer exactly");
	const auto routedBefore = read(file("routed.vssession"));
	for (const auto &operation : {QStringLiteral("routing"), QStringLiteral("send"), QStringLiteral("remove-send")})
		ok &= expect(run({"edit", file("routed.vssession"), "--operation", operation, "--track", track,
		                  operation == "routing" ? "--output-bus" : "--target-bus", "id:", "--output",
		                  file("routed.vssession"), "--overwrite"},
		                 2) &&
		                 read(file("routed.vssession")) == routedBefore,
		             "empty literal bus ID is rejected without interpreting it as Master");
	ok &= expect(run({"edit", file("routed.vssession"), "--operation", "routing", "--track", bus, "--output-bus", bus,
	                  "--output", file("routed.vssession"), "--overwrite"},
	                 4) &&
	                 read(file("routed.vssession")) == routedBefore,
	             "feedback rejection leaves the prior session unchanged");
	ok &= expect(run({"edit", file("routed.vssession"), "--operation", "send", "--track", track, "--target-bus",
	                  "master", "--enabled", "false", "--output", file("routed.vssession"), "--overwrite"}) &&
	                 readAudioSession(file("routed.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].routing.sends.size() == 1 && saved.tracks[0].routing.sends[0].gainDb == -12 &&
	                 !saved.tracks[0].routing.sends[0].enabled,
	             "updating an existing send retains unspecified values without duplicating it");
	ok &= expect(run({"edit", file("routed.vssession"), "--operation", "remove-send", "--track", track, "--target-bus",
	                  "master", "--output", file("routed.vssession"), "--overwrite"}) &&
	                 readAudioSession(file("routed.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].routing.sends.isEmpty(),
	             "CLI removes exactly the selected send");
	ok &= expect(run({"edit", file("routed.vssession"), "--operation", "routing", "--track", track, "--pre-fader",
	                  "true", "--output", file("forbidden.vssession")},
	                 2),
	             "routing rejects send-only flags");
	ok &= expect(run({"effects", file("routed.vssession"), "--track", track, "--operation", "add", "--type", "delay",
	                  "--parameters", "leftMs=1,rightMs=2,mix=0.5,feedback=0.25", "--tail-seconds", "0.01", "--output",
	                  file("effects.vssession")}),
	             "CLI adds a configured track insert and tail");
	const auto delayId = last.value("addedEffectId").toString();
	ok &= expect(!delayId.isEmpty() && run({"effects", file("effects.vssession"), "--track", "master", "--operation",
	                                        "add", "--type", "limiter", "--parameters", "ceilingDb=-3", "--output",
	                                        file("effects.vssession"), "--overwrite"}),
	             "CLI creates independently identified master effects");
	ok &= expect(readAudioSession(file("effects.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].effects[0].id == delayId && saved.masterEffects.size() == 1 &&
	                 saved.effectTailSeconds == .01,
	             "CLI effect configuration survives the native format");
	ok &= expect(
	    run({"mixdown", file("effects.vssession"), "--output", file("effects.wav"), "--wav-format", "float32"}) &&
	        read(file("effects.wav")) == encodeAudioWav(renderAudioSession(saved).clip, precision),
	    "CLI stateful insert WAV and tail match the shared renderer exactly");
	const auto effectsBefore = read(file("effects.vssession"));
	ok &= expect(run({"effect-automation", file("effects.vssession"), "--track", track, "--effect", delayId,
	                  "--parameter", "mix", "--points", "0:0:smooth,2:1:step,4:0.5", "--output",
	                  file("fx-automation.vssession"), "--dry-run"}) &&
	                 !QFile::exists(file("fx-automation.vssession")),
	             "effect automation dry-run is side-effect free");
	ok &= expect(
	    run({"effect-automation", file("effects.vssession"), "--track", track, "--effect", delayId, "--parameter",
	         "mix", "--points", "0:0:smooth,2:1:step,4:0.5", "--output", file("fx-automation.vssession")}),
	    "CLI authors effect curves");
	AudioSession automatedEffects;
	ok &= expect(readAudioSession(file("fx-automation.vssession"), &automatedEffects, nullptr, &error) &&
	                 automatedEffects.tracks[0].effectAutomation.size() == 1 &&
	                 automatedEffects.tracks[0].effectAutomation[0].points[0].curve == AudioAutomationCurve::Smooth,
	             "native session retains CLI curve kinds");
	ok &= expect(run({"mixdown", file("fx-automation.vssession"), "--output", file("fx-automation.wav"), "--wav-format",
	                  "float32"}) &&
	                 read(file("fx-automation.wav")) ==
	                     encodeAudioWav(renderAudioSession(automatedEffects).clip, precision),
	             "automated CLI WAV equals the session renderer");
	const auto automationBefore = read(file("fx-automation.vssession"));
	for (const auto &points : {"0:0:future,2:1", "0:0,0:1", "0:2", "-1:0"})
		ok &= expect(
		    run({"effect-automation", file("fx-automation.vssession"), "--track", track, "--effect", delayId,
		         "--parameter", "mix", "--points", points, "--output", file("fx-automation.vssession"), "--overwrite"},
		        QString(points).contains("future") ? 2 : 4) &&
		        read(file("fx-automation.vssession")) == automationBefore,
		    "invalid automation cannot alter the native file");
	ok &= expect(
	    run({"effect-automation", file("fx-automation.vssession"), "--track", track, "--effect", delayId, "--parameter",
	         "mix", "--enabled", "false", "--output", file("fx-automation.vssession"), "--overwrite"}) &&
	        readAudioSession(file("fx-automation.vssession"), &automatedEffects, nullptr, &error) &&
	        !automatedEffects.tracks[0].effectAutomation[0].enabled &&
	        automatedEffects.tracks[0].effectAutomation[0].points.size() == 3,
	    "read/off preserves authored points");
	ok &= expect(run({"effects", file("fx-automation.vssession"), "--track", track, "--operation", "preset", "--preset",
	                  "dialogue-clean", "--output", file("fx-automation.vssession"), "--overwrite"}) &&
	                 readAudioSession(file("fx-automation.vssession"), &automatedEffects, nullptr, &error) &&
	                 automatedEffects.tracks[0].effectAutomation.isEmpty(),
	             "CLI preset replacement prunes automation targets");
	ok &= expect(run({"automation", file("fx-automation.vssession"), "--track", track, "--gain-points",
	                  "0:-6:smooth,4:0:step", "--output", file("fx-automation.vssession"), "--overwrite"}) &&
	                 readAudioSession(file("fx-automation.vssession"), &automatedEffects, nullptr, &error) &&
	                 automatedEffects.tracks[0].gainAutomation[0].curve == AudioAutomationCurve::Smooth,
	             "track gain accepts the shared curve syntax");
	for (const auto &tail : {QString("-1"), QString("61")})
		ok &= expect(run({"effects", file("effects.vssession"), "--track", "master", "--operation", "tail",
		                  "--tail-seconds", tail, "--output", file("effects.vssession"), "--overwrite"},
		                 2) &&
		                 read(file("effects.vssession")) == effectsBefore,
		             "explicit invalid tail cannot be interpreted as an omitted setting");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "set", "--effect", delayId,
	                  "--parameters", "mix=0.5,mix=0.25", "--output", file("effects.vssession"), "--overwrite"},
	                 2) &&
	                 read(file("effects.vssession")) == effectsBefore,
	             "duplicate effect parameters fail without changing the file");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "set", "--effect", delayId,
	                  "--parameters", "feedback=1", "--output", file("effects.vssession"), "--overwrite"},
	                 4) &&
	                 read(file("effects.vssession")) == effectsBefore,
	             "out-of-range effect parameters fail before publication");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "set", "--effect", delayId,
	                  "--enabled", "false", "--output", file("bypassed.vssession"), "--dry-run"}) &&
	                 !QFile::exists(file("bypassed.vssession")),
	             "effect dry-run retains native write guards");
	ok &=
	    expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "add", "--type", "gain",
	                "--index", "0", "--parameters", "gainDb=-6", "--output", file("effects.vssession"), "--overwrite"}),
	           "CLI inserts at a reviewed chain index");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "move", "--effect",
	                  delayId, "--index", "0", "--output", file("effects.vssession"), "--overwrite"}) &&
	                 readAudioSession(file("effects.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].effects[0].id == delayId,
	             "CLI reorders stable effect identities");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "remove", "--effect",
	                  delayId, "--output", file("effects.vssession"), "--overwrite"}) &&
	                 readAudioSession(file("effects.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].effects.size() == 1,
	             "CLI removes only the selected insert");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", "master", "--operation", "clear", "--output",
	                  file("effects.vssession"), "--overwrite"}) &&
	                 readAudioSession(file("effects.vssession"), &saved, nullptr, &error) &&
	                 saved.masterEffects.isEmpty() && saved.tracks[0].effects.size() == 1,
	             "clearing master preserves track chains");
	ok &= expect(run({"presets", "--sample-rate", "48000"}) && last.value("presets").toArray().size() == 9 &&
	                 last.value("processors").toArray().size() == 18,
	             "CLI lists factories and parameter bounds without a session or device");
	const auto catalog = last;
	bool structural = false;
	for (const auto &processor : catalog["processors"].toArray())
		if (processor.toObject()["type"] == "lookahead-limiter")
			for (const auto &parameter : processor.toObject()["parameters"].toArray()) {
				const auto p = parameter.toObject();
				if (p["key"] == "lookaheadMs")
					structural = p.contains("automatable") && !p["automatable"].toBool();
			}
	ok &= expect(structural, "CLI processor schema identifies fixed-latency parameter as structural");
	ok &= expect(run({"presets", "--sample-rate", "48000"}) && last == catalog,
	             "catalog is stable across repeated inspections");
	ok &= expect(run({"presets", "--output", file("invalid.vsfx")}, 2), "catalog rejects mutation options");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "preset", "--preset",
	                  "large-hall", "--output", file("hall.vssession")}) &&
	                 readAudioSession(file("hall.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].effects.size() == 1 && saved.tracks[0].effects[0].type == "reverb" &&
	                 saved.effectTailSeconds == 12,
	             "CLI factory application replaces a chain and extends its tail");
	const auto hallBefore = read(file("hall.vssession"));
	ok &= expect(run({"effects", file("hall.vssession"), "--track", track, "--operation", "save-preset", "--name",
	                  "My Hall", "--output", file("hall.vsfx"), "--dry-run"}) &&
	                 !QFile::exists(file("hall.vsfx")),
	             "preset save dry-run writes no file");
	ok &= expect(run({"effects", file("hall.vssession"), "--track", track, "--operation", "save-preset", "--name",
	                  "My Hall", "--output", file("hall.vsfx")}) &&
	                 read(file("hall.vssession")) == hallBefore,
	             "CLI preset save preserves source session");
	AudioEffectPreset stored;
	ok &= expect(readAudioEffectPreset(file("hall.vsfx"), &stored, nullptr, &error) && stored.name == "My Hall" &&
	                 stored.tailSeconds == 12,
	             "CLI writes reusable preset metadata");
	ok &= expect(run({"effects", file("effects.vssession"), "--track", track, "--operation", "preset", "--preset-file",
	                  file("hall.vsfx"), "--output", file("preset-copy.vssession")}) &&
	                 readAudioSession(file("preset-copy.vssession"), &saved, nullptr, &error) &&
	                 saved.tracks[0].effects[0].id != stored.effects[0].id &&
	                 saved.tracks[0].effects[0].parameters == stored.effects[0].parameters,
	             "CLI file preset uses fresh IDs and preserves exact parameters");
	ok &= expect(
	    run({"mixdown", file("preset-copy.vssession"), "--output", file("preset.wav"), "--wav-format", "float32"}) &&
	        read(file("preset.wav")) == encodeAudioWav(renderAudioSession(saved).clip, precision),
	    "preset reverb and full tail export match core renderer");
	ok &=
	    expect(run({"effects", file("hall.vssession"), "--track", track, "--operation", "preset", "--preset",
	                "wide-chorus", "--output", file("chorus.vssession")}) &&
	               readAudioSession(file("chorus.vssession"), &saved, nullptr, &error) && saved.effectTailSeconds == 12,
	           "loading a shorter preset never truncates an existing session tail");
	ok &= expect(
	    run({"effects", file("hall.vssession"), "--track", track, "--operation", "preset", "--preset", "large-hall",
	         "--preset-file", file("hall.vsfx"), "--output", file("hall.vssession"), "--overwrite"},
	        2) &&
	        read(file("hall.vssession")) == hallBefore,
	    "factory and file preset flags are exclusive and preserve rejected output");
	ok &= write(file("bad.vsfx"), "{\"format\":\"unknown\"}");
	ok &= expect(run({"effects", file("hall.vssession"), "--track", track, "--operation", "preset", "--preset-file",
	                  file("bad.vsfx"), "--output", file("hall.vssession"), "--overwrite"},
	                 4) &&
	                 read(file("hall.vssession")) == hallBefore,
	             "malformed preset cannot replace a session");
	ok &= expect(run({"new", "--sample-rate", "8000", "--output", file("low-rate.vssession")}) &&
	                 run({"effects", file("low-rate.vssession"), "--track", "master", "--operation", "preset",
	                      "--preset-file", file("hall.vsfx"), "--output", file("incompatible.vssession")},
	                     4) &&
	                 !QFile::exists(file("incompatible.vssession")),
	             "file preset rejects incompatible destination frequencies without publishing");
	AudioSession deliverySession;
	QString deliveryError;
	ok &= readAudioSession(file("hall.vssession"), &deliverySession, nullptr, &deliveryError);
	AudioStemExportRequest delivery;
	delivery.directory = temporary.path();
	delivery.prefix = "oracle";
	delivery.stripIds = {track};
	delivery.includeMaster = true;
	delivery.end = 20001;
	delivery.format = AudioWavFormat::Pcm24;
	delivery.dither = true;
	delivery.ditherSeed = 987;
	const auto oracle = writeAudioSessionStems(deliverySession, delivery);
	ok &= expect(oracle.succeeded && run({"stems", file("hall.vssession"), "--stem", track, "--stem", "master",
	                                      "--output", temporary.path(), "--name", "cli", "--end-frame", "20001",
	                                      "--wav-format", "pcm24", "--dither", "tpdf", "--dither-seed", "987"}),
	             "CLI selected stems and master deliver aligned integer WAVs with seeded dither");
	const auto manifest = last["delivery"].toObject();
	ok &= expect(last["completed"].toInt() == 2 && manifest["status"] == "complete",
	             "CLI reports completed delivery manifest");
	for (int i = 0; i < 2; ++i)
		ok &= expect(read(oracle.plan.files[i].path) ==
		                 read(temporary.filePath(manifest["files"].toArray()[i].toObject()["file"].toString())),
		             "CLI stem bytes equal shared renderer and dither oracle");
	ok &= expect(run({"stems", file("hall.vssession"), "--stem", track, "--stem", "master", "--output",
	                  temporary.path(), "--name", "cli"},
	                 4) &&
	                 last.contains("completed") && last["completed"].toInt() == 0,
	             "CLI failure retains structured batch results");
	ok &= expect(run({"stems", file("hall.vssession"), "--stem", track, "--stem", track, "--output", temporary.path(),
	                  "--name", "duplicates"},
	                 4) &&
	                 !QFile::exists(file("duplicates-delivery.stems.json")),
	             "duplicate selected strips reject before output");
	ok &= expect(run({"stems", file("hall.vssession"), "--stem", "master", "--output", temporary.path(), "--name",
	                  "dry-stems", "--dry-run"}) &&
	                 !QFile::exists(file("dry-stems-000-master.wav")) &&
	                 !QFile::exists(file("dry-stems-delivery.stems.json")),
	             "CLI stem dry run publishes nothing");
	ok &= expect(run({"stems", file("hall.vssession"), "--stem", track, "--tap", "pre", "--respect-solo", "true",
	                  "--output", temporary.path(), "--name", "pre", "--start-frame", "1", "--end-frame", "4"}) &&
	                 last["delivery"].toObject()["framesPerFile"].toInt() == 3,
	             "CLI pre-fader and solo policies use a common exact range");
	ok &= expect(run({"mixdown", file("hall.vssession"), "--output", file("bad-dither.wav"), "--dither", "tpdf"}, 2) &&
	                 !QFile::exists(file("bad-dither.wav")),
	             "float dither rejected");
	ok &= expect(run({"mixdown", file("hall.vssession"), "--output", file("bad-seed.wav"), "--wav-format", "pcm16",
	                  "--dither", "tpdf", "--dither-seed", "18446744073709551616"},
	                 2),
	             "overflowing seed rejected");
	ok &= expect(run({"inspect", file("hall.vssession"), "--stem", track, "--stem", "master"}, 2),
	             "repeatable stem selector remains confined to stem action");
	AudioProject latencyMedia{{2, 48000, {.125f, -.25f, .25f, .125f, -.0625f, .25f, .125f, -.125f}}, 0, 4, {}, {}, {}};
	auto latencySession = importAudioSessionSource({}, latencyMedia).session;
	const auto latencyTrack = latencySession.tracks[0].id;
	const auto latencyDry = renderAudioSession(latencySession);
	ok &= write(file("latency-source.vssession"), encodeAudioSession(latencySession));
	ok &= expect(
	    run({"effects", file("latency-source.vssession"), "--track", latencyTrack, "--operation", "add", "--type",
	         "lookahead-limiter", "--parameters", "lookaheadMs=5,ceilingDb=0", "--output", file("latency.vssession")}),
	    "CLI creates native lookahead insert");
	const auto latencyId = last["addedEffectId"].toString();
	ok &= expect(!latencyId.isEmpty() && last["processingLatency"].toObject()["frames"].toInt() == 240 &&
	                 run({"effects", file("latency.vssession"), "--track", "master", "--operation", "add", "--type",
	                      "lookahead-limiter", "--parameters", "lookaheadMs=3,ceilingDb=0", "--output",
	                      file("latency.vssession"), "--overwrite"}) &&
	                 last["processingLatency"].toObject()["frames"].toInt() == 384,
	             "CLI diagnostics expose serial track and master latency");
	const auto latencyBefore = read(file("latency.vssession"));
	ok &= expect(run({"effect-automation", file("latency.vssession"), "--track", latencyTrack, "--effect", latencyId,
	                  "--parameter", "lookaheadMs", "--points", "0:5,2:10", "--enabled", "false", "--output",
	                  file("latency.vssession"), "--overwrite"},
	                 4) &&
	                 read(file("latency.vssession")) == latencyBefore,
	             "CLI rejects even disabled latency automation without changing session");
	ok &= expect(
	    run({"mixdown", file("latency.vssession"), "--output", file("latency.wav"), "--wav-format", "float32"}) &&
	        last["mixdown"].toObject()["processingLatencyFrames"].toInt() == 384 &&
	        last["mixdown"].toObject()["frames"].toInt() == 4 &&
	        read(file("latency.wav")) == encodeAudioWav(latencyDry.clip, precision),
	    "CLI compensated WAV equals dry oracle without extra or missing samples");
	ok &= expect(
	    run({"transport", file("latency.vssession"), "--frames", "12", "--block-frames", "1", "--loop", "true"}) &&
	        last["transport"].toObject()["processingLatencyFrames"].toInt() == 384 &&
	        last["transport"].toObject()["framesRendered"].toInt() == 12 &&
	        last["transport"].toObject()["loops"].toInt() == 3,
	    "CLI transport counts authored frames across compensated short loops");
	ok &= expect(run({"stems", file("latency.vssession"), "--stem", latencyTrack, "--stem", "master", "--output",
	                  temporary.path(), "--name", "latent-stems"}),
	             "CLI exports latent strip and master stems");
	const auto latentFiles = last["delivery"].toObject()["files"].toArray();
	ok &= expect(latentFiles.size() == 2, "latency delivery has requested entries");
	for (int i = 0; i < latentFiles.size(); ++i) {
		const auto entry = latentFiles[i].toObject();
		ok &=
		    expect(entry["processingLatencyFrames"].toInt() == (i == 0 ? 384 : 240) &&
		               read(temporary.filePath(entry["file"].toString())) == encodeAudioWav(latencyDry.clip, precision),
		           "stem manifest reports target latency and independent aligned delivery bytes");
	}
	ok &= expect(run({"effects", file("latency.vssession"), "--track", latencyTrack, "--operation", "save-preset",
	                  "--name", "Lookahead", "--output", file("latency.vsfx")}) &&
	                 readAudioEffectPreset(file("latency.vsfx"), &stored, nullptr, &error) &&
	                 stored.effects[0].parameters["lookaheadMs"] == 5,
	             "CLI static presets retain exact structural lookahead values");
	// CLI hashes must represent continuous DSP, including short loops where
	// the first compensation prime already crosses several authored boundaries.
	auto loopSession = test::playbackLoopFixture(true);
	// Use exact step lanes so the oracle's expanded JSON-independent sampled
	// lanes produce bit-identical values for the CLI's canonical float digest.
	for (auto &track : loopSession.tracks) {
		for (auto &point : track.gainAutomation)
			point.curve = AudioAutomationCurve::Step;
		for (auto &point : track.panAutomation)
			point.curve = AudioAutomationCurve::Step;
		for (auto &lane : track.effectAutomation)
			for (auto &point : lane.points)
				point.curve = AudioAutomationCurve::Step;
	}
	for (auto &lane : loopSession.masterEffectAutomation)
		for (auto &point : lane.points)
			point.curve = AudioAutomationCurve::Step;
	ok &= write(file("continuous-loop.vssession"), encodeAudioSession(loopSession));
	for (const auto &range : {std::pair{23, 70}, std::pair{23, 24}}) {
		const auto expanded = test::expandPlaybackLoop(loopSession, range.first, range.second, 1200);
		const auto reference = renderAudioSession(expanded, range.first, range.first + 1021);
		QByteArray canonical(reference.clip.samples.size() * 4, '\0');
		for (qsizetype i = 0; i < reference.clip.samples.size(); ++i)
			qToLittleEndian(std::bit_cast<quint32>(reference.clip.samples[i]), canonical.data() + i * 4);
		const auto hash = QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex();
		for (const auto block : {"1", "17", "4096"}) {
			ok &= expect(run({"transport", file("continuous-loop.vssession"), "--start-frame",
			                  QString::number(range.first), "--end-frame", QString::number(range.second), "--frames",
			                  "1021", "--block-frames", block, "--loop", "true"}),
			             "CLI continuous loop diagnostics run with private settings");
			const auto report = last["transport"].toObject();
			ok &= expect(reference.succeeded() && report["sha256Float32LE"].toString().toLatin1() == hash &&
			                 report["framesRendered"].toInt() == 1021 &&
			                 report["position"].toInt() == range.first + 1021 % (range.second - range.first) &&
			                 !report["deviceOpened"].toBool(true),
			             "CLI float digest and cursor equal the independently expanded continuous arrangement");
		}
	}
	std::cout << (ok ? "Audio session CLI verification passed\n" : "Audio session CLI verification failed\n");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
