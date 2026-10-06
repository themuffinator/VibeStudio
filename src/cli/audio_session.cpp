#include "cli/audio_session.h"
#include "core/audio_arrangement.h"
#include "core/audio_effect_preset.h"
#include "core/audio_media.h"
#include "core/audio_range.h"
#include "core/audio_recovery_store.h"
#include "core/audio_session_io.h"
#include "core/audio_stems.h"
#include "core/audio_transport.h"
#include <QCryptographicHash>
#include <QtEndian>
#include <bit>
#include <vector>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace vibestudio::cli
{
namespace
{
bool parseAutomationPoints(const QString &text, QVector<AudioAutomationPoint> *points, QString *error)
{
	points->clear();
	if (text == "none")
		return true;
	const auto entries = text.split(',');
	if (entries.size() > AudioAutomationPointLimit) {
		*error = QCoreApplication::translate("AudioSessionCli", "Automation exceeds 4096 points.");
		return false;
	}
	for (const auto &entry : entries) {
		const auto fields = entry.split(':');
		bool timeOk = false, valueOk = false;
		const auto time = fields.value(0).toLongLong(&timeOk);
		const double value = fields.value(1).toDouble(&valueOk);
		AudioAutomationCurve curve = AudioAutomationCurve::Linear;
		if ((fields.size() != 2 && fields.size() != 3) || !timeOk || !valueOk || !std::isfinite(value) ||
		    (fields.size() == 3 && !parseAudioAutomationCurve(fields[2], &curve))) {
			*error = QCoreApplication::translate(
			    "AudioSessionCli",
			    "Automation uses frame:value[:linear|step|smooth] points separated by commas, or none.");
			return false;
		}
		points->append({time, value, curve});
	}
	return true;
}
bool parseMeter(const QString &text, int *beats, int *unit)
{
	static const QRegularExpression syntax(QStringLiteral("^([1-9][0-9]*)/([1-9][0-9]*)$"));
	const auto match = syntax.match(text);
	bool first = false, second = false;
	if (!match.hasMatch())
		return false;
	*beats = match.captured(1).toInt(&first);
	*unit = match.captured(2).toInt(&second);
	return first && second;
}
bool parseTempoOptions(const QHash<QString, QString> &values, AudioTempoMap *map)
{
	if (values.contains("--meter") && !parseMeter(values["--meter"], &map->beatsPerBar, &map->beatUnit))
		return false;
	if (values.contains("--tempo-points")) {
		map->tempoChanges.clear();
		const auto text = values["--tempo-points"];
		const auto entries = text == "none" ? QStringList() : text.split(',');
		if (entries.size() > AudioTempoChangeLimit)
			return false;
		for (const auto &entry : entries) {
			const auto fields = entry.split(':');
			bool tickOk = false, bpmOk = false;
			const auto tick = fields.value(0).toLongLong(&tickOk);
			const auto bpm = fields.value(1).toDouble(&bpmOk);
			if (fields.size() != 2 || !tickOk || !bpmOk)
				return false;
			map->tempoChanges.append({tick, bpm});
		}
	}
	if (values.contains("--meter-points")) {
		map->meterChanges.clear();
		const auto text = values["--meter-points"];
		const auto entries = text == "none" ? QStringList() : text.split(',');
		if (entries.size() > AudioTempoChangeLimit)
			return false;
		for (const auto &entry : entries) {
			const auto fields = entry.split(':');
			bool barOk = false;
			AudioMeterChange change;
			change.bar = fields.value(0).toLongLong(&barOk);
			if (fields.size() != 2 || !barOk || !parseMeter(fields.value(1), &change.beatsPerBar, &change.beatUnit))
				return false;
			map->meterChanges.append(change);
		}
	}
	return true;
}
} // namespace
AudioSessionCliResult runAudioSession(const QStringList &arguments)
{
	const auto failure = [](int code, const QString &text) { return AudioSessionCliResult{code, text, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> commonFlags{"--cli", "--json", "--quiet", "--verbose"};
	const QSet<QString> flags{"--overwrite", "--dry-run", "--resample", "--all-tracks"};
	const QSet<QString> options{"--output",    "--input",      "--sample-rate",     "--tempo",        "--name",
	                            "--track",     "--clip",       "--operation",       "--at-frame",     "--offset-frame",
	                            "--frames",    "--fade-in",    "--fade-out",        "--db",           "--pan",
	                            "--mute",      "--solo",       "--gain-points",     "--pan-points",   "--start-frame",
	                            "--end-frame", "--wav-format", "--expected-sha256", "--block-frames", "--loop",
	                            "--stem",      "--tap",        "--respect-solo",    "--dither",       "--dither-seed"};
	const QSet<QString> routingOptions{"--output-bus", "--invert-left", "--invert-right", "--swap-channels",
	                                   "--target-bus", "--pre-fader",   "--enabled"};
	const QSet<QString> effectOptions{"--effect", "--type",        "--parameters", "--index", "--tail-seconds",
	                                  "--preset", "--preset-file", "--parameter",  "--points"};
	const QSet<QString> timingOptions{"--meter",       "--tempo-points", "--meter-points",
	                                  "--at-position", "--at-tick",      "--snap"};
	const QSet<QString> arrangementOptions{"--select-clip", "--linked-groups", "--offset-frames", "--reset-fades"};
	const QSet<QString> rangeOptions{"--select-track", "--follow-automation", "--master-automation"};
	const QSet<QString> mediaOptions{"--source"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional, stems, selectedClips, selectedTracks, selectedSources;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const QString token = arguments[i];
		if (!token.startsWith('-')) {
			positional << token;
			continue;
		}
		const auto equal = token.indexOf('=');
		const QString key = equal < 0 ? token : token.left(equal);
		if (seen.contains(key) && key != "--stem" && key != "--select-clip" && key != "--select-track" &&
		    key != "--source") {
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Repeated option: %1").arg(key));
		}
		seen.insert(key);
		if (commonFlags.contains(key) || flags.contains(key)) {
			if (equal >= 0) {
				return failure(
				    2, QCoreApplication::translate("AudioSessionCli", "Flag does not accept a value: %1").arg(key));
			}
			continue;
		}
		if (!options.contains(key) && !routingOptions.contains(key) && !effectOptions.contains(key) &&
		    !timingOptions.contains(key) && !arrangementOptions.contains(key) && !rangeOptions.contains(key) &&
		    !mediaOptions.contains(key) && !globals.contains(key)) {
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Unknown session option: %1").arg(key));
		}
		QString value;
		if (equal >= 0) {
			value = token.mid(equal + 1);
		} else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith("--")) {
			value = arguments[++i];
		}
		if (value.isEmpty()) {
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Missing value for %1.").arg(key));
		}
		values.insert(key, value);
		if (key == "--stem")
			stems.append(value);
		if (key == "--select-clip")
			selectedClips.append(value);
		if (key == "--select-track")
			selectedTracks.append(value);
		if (key == "--source")
			selectedSources.append(value);
	}
	if (positional.size() < 3) {
		return failure(2, QCoreApplication::translate(
		                      "AudioSessionCli",
		                      "Expected asset audio-session "
		                      "new|inspect|import|media|edit|arrange|range|effects|effect-automation|presets|"
		                      "automation|tempo-map|position|mixdown|stems|recover|transport|meters [session]."));
	}
	const auto action = positional[2];
	const bool creating = action == QStringLiteral("new"), inspecting = action == QStringLiteral("inspect"),
	           rendering = action == QStringLiteral("mixdown"), recovering = action == QStringLiteral("recover"),
	           diagnosing = action == QStringLiteral("transport"), catalog = action == QStringLiteral("presets");
	const bool savingPreset = action == "effects" && values.value("--operation") == "save-preset";
	const bool exportingStems = action == "stems";
	const bool positioning = action == "position";
	const bool inspectingMedia = action == "media" && values.value("--operation") == "inspect";
	const bool metering = action == "meters";
	if (positional.size() != (creating || catalog ? 3 : 4)) {
		return failure(2, QCoreApplication::translate("AudioSessionCli", "Unexpected number of session paths."));
	}
	QSet<QString> allowed, required;
	if (!inspecting && !diagnosing && !catalog && !positioning && !inspectingMedia && !metering) {
		allowed << "--output" << "--overwrite" << "--dry-run";
		required << "--output";
	}
	if (catalog) {
		allowed << "--sample-rate";
	} else if (creating) {
		allowed << "--sample-rate" << "--tempo" << "--name" << "--meter" << "--tempo-points" << "--meter-points";
	} else if (action == "tempo-map") {
		allowed << "--tempo" << "--meter" << "--tempo-points" << "--meter-points";
		if (!seen.contains("--tempo") && !seen.contains("--meter") && !seen.contains("--tempo-points") &&
		    !seen.contains("--meter-points"))
			return failure(2, QCoreApplication::translate("AudioSessionCli",
			                                              "Specify a tempo, meter or replacement change list."));
	} else if (positioning) {
		allowed << "--at-frame" << "--at-position" << "--at-tick" << "--snap";
		if (int(seen.contains("--at-frame")) + int(seen.contains("--at-position")) + int(seen.contains("--at-tick")) !=
		    1)
			return failure(2, QCoreApplication::translate(
			                      "AudioSessionCli",
			                      "Choose exactly one --at-frame, --at-position bar.beat.tick or --at-tick value."));
	} else if (inspecting) {
	} else if (metering) {
		allowed << "--start-frame" << "--end-frame" << "--block-frames";
	} else if (diagnosing) {
		allowed << "--start-frame" << "--end-frame" << "--frames" << "--block-frames" << "--loop";
		required << "--frames";
	} else if (recovering) {
		allowed << "--expected-sha256";
		required << "--expected-sha256";
	} else if (action == QStringLiteral("import")) {
		allowed << "--input" << "--track" << "--at-frame" << "--resample";
		required << "--input";
	} else if (action == "media") {
		allowed << "--operation";
		required << "--operation";
		const auto operation = values.value("--operation");
		if (!QStringList{"inspect", "rename", "relink", "replace", "remove", "prune"}.contains(operation))
			return failure(2, QCoreApplication::translate(
			                      "AudioSessionCli",
			                      "Media operation must be inspect, rename, relink, replace, remove or prune."));
		if (operation != "inspect" && operation != "prune") {
			allowed << "--source";
			required << "--source";
		}
		if (operation == "rename") {
			allowed << "--name";
			required << "--name";
		}
		if (operation == "relink" || operation == "replace") {
			allowed << "--input" << "--expected-sha256";
			required << "--input";
		}
		if (operation == "replace")
			allowed << "--resample";
	} else if (action == QStringLiteral("automation")) {
		allowed << "--track" << "--gain-points" << "--pan-points";
		required << "--track";
	} else if (action == QStringLiteral("effect-automation")) {
		allowed << "--track" << "--effect" << "--parameter" << "--points" << "--enabled";
		required << "--track" << "--effect" << "--parameter";
		if (!seen.contains("--points") && !seen.contains("--enabled"))
			return failure(
			    2, QCoreApplication::translate("AudioSessionCli", "Specify automation points or its enabled state."));
	} else if (action == QStringLiteral("effects")) {
		allowed << "--track" << "--operation" << "--tail-seconds";
		required << "--track" << "--operation";
		const auto operation = values.value("--operation");
		if (operation == "add") {
			allowed << "--type" << "--parameters" << "--enabled" << "--index";
			required << "--type";
		} else if (operation == "set") {
			allowed << "--effect" << "--parameters" << "--enabled";
			required << "--effect";
		} else if (operation == "remove" || operation == "move") {
			allowed << "--effect";
			required << "--effect";
			if (operation == "move") {
				allowed << "--index";
				required << "--index";
			}
		} else if (operation == "preset") {
			allowed << "--preset" << "--preset-file";
			if (seen.contains("--preset") == seen.contains("--preset-file"))
				return failure(
				    2, QCoreApplication::translate("AudioSessionCli",
				                                   "Choose exactly one --preset factory ID or --preset-file path."));
		} else if (operation == "save-preset") {
			allowed << "--name";
			required << "--name";
		} else if (operation == "tail")
			required << "--tail-seconds";
		else if (operation != "clear")
			return failure(
			    2, QCoreApplication::translate(
			           "AudioSessionCli",
			           "Effects operation must be add, set, remove, move, clear, tail, preset or save-preset."));
	} else if (rendering || exportingStems) {
		allowed << "--start-frame" << "--end-frame" << "--wav-format" << "--dither" << "--dither-seed";
		if (exportingStems) {
			allowed << "--stem" << "--tap" << "--respect-solo" << "--name";
			required << "--stem";
		}
	} else if (action == QStringLiteral("range")) {
		allowed << "--operation" << "--start-frame" << "--end-frame" << "--all-tracks" << "--select-track";
		required << "--operation" << "--start-frame" << "--end-frame";
		if (seen.contains("--all-tracks") == seen.contains("--select-track"))
			return failure(2, QCoreApplication::translate("AudioSessionCli",
			                                              "Choose --all-tracks or one or more --select-track IDs."));
		const auto operation = values.value("--operation");
		if (!QStringList{"clear", "ripple-delete", "insert-silence", "repeat"}.contains(operation))
			return failure(
			    2, QCoreApplication::translate(
			           "AudioSessionCli", "Range operation must be clear, ripple-delete, insert-silence or repeat."));
		if (operation != "clear")
			allowed << "--follow-automation" << "--master-automation";
	} else if (action == QStringLiteral("arrange")) {
		allowed << "--operation" << "--select-clip" << "--linked-groups";
		required << "--operation" << "--select-clip";
		const auto operation = values.value("--operation");
		QStringList fields;
		if (operation == "move" || operation == "duplicate")
			fields << "--offset-frames";
		else if (operation == "split")
			fields << "--at-frame";
		else if (operation == "group")
			fields << "--name";
		else if (operation == "gain")
			fields << "--db";
		else if (operation == "fades")
			fields << "--fade-in" << "--fade-out";
		else if (operation == "mute")
			fields << "--mute";
		else if (operation != "remove" && operation != "ungroup")
			return failure(2, QCoreApplication::translate("AudioSessionCli",
			                                              "Arrangement operation must be move, duplicate, remove, "
			                                              "split, group, ungroup, gain, fades or mute."));
		for (const auto &field : fields) {
			allowed.insert(field);
			required.insert(field);
		}
	} else if (action == QStringLiteral("edit")) {
		required << "--operation";
		allowed << "--operation";
		const auto operation = values.value("--operation");
		if (operation == QStringLiteral("add-track") || operation == QStringLiteral("add-bus")) {
			allowed << "--name";
			required << "--name";
		} else if (operation == QStringLiteral("master")) {
			allowed << "--db";
			required << "--db";
		} else if (operation == QStringLiteral("remove-track")) {
			allowed << "--track";
			required << "--track";
		} else if (operation == QStringLiteral("track")) {
			allowed << "--track" << "--name" << "--db" << "--pan" << "--mute" << "--solo";
			required << "--track";
		} else if (operation == QStringLiteral("routing")) {
			allowed << "--track" << "--output-bus" << "--invert-left" << "--invert-right" << "--swap-channels";
			required << "--track";
		} else if (operation == QStringLiteral("send") || operation == QStringLiteral("remove-send")) {
			allowed << "--track" << "--target-bus";
			required << "--track" << "--target-bus";
			if (operation == QStringLiteral("send"))
				allowed << "--db" << "--pan" << "--pre-fader" << "--enabled";
		} else if (operation == QStringLiteral("region")) {
			allowed << "--reset-fades";
			allowed << "--track" << "--clip" << "--name" << "--at-frame" << "--offset-frame" << "--frames"
			        << "--fade-in" << "--fade-out" << "--db" << "--mute";
			required << "--track" << "--clip";
		} else if (operation == QStringLiteral("duplicate") || operation == QStringLiteral("split")) {
			allowed << "--track" << "--clip" << "--at-frame";
			required << "--track" << "--clip" << "--at-frame";
		} else if (operation == QStringLiteral("remove-region")) {
			allowed << "--track" << "--clip";
			required << "--track" << "--clip";
		} else {
			return failure(
			    2, QCoreApplication::translate("AudioSessionCli", "Unknown or missing session edit operation."));
		}
	} else {
		return failure(2, QCoreApplication::translate("AudioSessionCli", "Unknown session command."));
	}
	for (const auto &key : seen) {
		if (!globals.contains(key) && !commonFlags.contains(key) && !allowed.contains(key)) {
			return failure(
			    2,
			    QCoreApplication::translate("AudioSessionCli", "Option %1 does not apply to this operation.").arg(key));
		}
	}
	for (const auto &key : required) {
		if (!seen.contains(key)) {
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Required option: %1").arg(key));
		}
	}
	QString parseError;
	const auto frame = [&](const char *key, qint64 fallback) {
		if (!values.contains(QLatin1String(key))) {
			return fallback;
		}
		bool ok = false;
		const qint64 value = values.value(QLatin1String(key)).toLongLong(&ok);
		if (!ok || value < 0 || value > AudioSessionFrameLimit) {
			parseError =
			    QCoreApplication::translate("AudioSessionCli", "Invalid frame value: %1").arg(QLatin1String(key));
		}
		return value;
	};
	const auto real = [&](const char *key, double fallback) {
		if (!values.contains(QLatin1String(key))) {
			return fallback;
		}
		bool ok = false;
		const double value = values.value(QLatin1String(key)).toDouble(&ok);
		if (!ok || !std::isfinite(value)) {
			parseError =
			    QCoreApplication::translate("AudioSessionCli", "Invalid numeric value: %1").arg(QLatin1String(key));
		}
		return value;
	};
	const auto boolean = [&](const char *key, bool fallback) {
		if (!values.contains(QLatin1String(key))) {
			return fallback;
		}
		const auto value = values.value(QLatin1String(key));
		if (value != QStringLiteral("true") && value != QStringLiteral("false")) {
			parseError =
			    QCoreApplication::translate("AudioSessionCli", "Use true or false for %1.").arg(QLatin1String(key));
		}
		return value == QStringLiteral("true");
	};
	if (catalog) {
		const auto rate = frame("--sample-rate", 48000);
		if (!parseError.isEmpty())
			return failure(2, parseError);
		if (rate < 1 || rate > 384000)
			return failure(2, QCoreApplication::translate("AudioSessionCli",
			                                              "Session sample rate must be between 1 and 384000 Hz."));
		QJsonArray presets, processors;
		AudioSessionCliResult result;
		for (const auto &descriptor : audioEffectFactoryPresets()) {
			auto preset = makeAudioEffectPreset(descriptor.id, int(rate));
			for (qsizetype i = 0; i < preset.effects.size(); ++i)
				preset.effects[i].id = descriptor.id + '-' + QString::number(i);
			presets.append(QJsonObject{{"id", descriptor.id},
			                           {"name", descriptor.name},
			                           {"description", descriptor.description},
			                           {"tailSeconds", preset.tailSeconds},
			                           {"effects", audioEffectsToJson(preset.effects)}});
			result.lines << descriptor.id + ": " + descriptor.name + " — " + descriptor.description;
		}
		for (const auto &type : audioEffectTypes()) {
			QJsonArray parameters;
			for (const auto &parameter : audioEffectParameters(type, int(rate)))
				parameters.append(QJsonObject{{"key", parameter.key},
				                              {"label", parameter.label},
				                              {"minimum", parameter.minimum},
				                              {"maximum", parameter.maximum},
				                              {"default", parameter.initial},
				                              {"automatable", parameter.automatable},
				                              {"decimals", parameter.decimals}});
			processors.append(QJsonObject{{"type", type}, {"name", audioEffectName(type)}, {"parameters", parameters}});
		}
		result.payload = {
		    {"operation", action}, {"sampleRate", rate}, {"presets", presets}, {"processors", processors}};
		return result;
	}
	AudioSession session;
	AudioEffectPreset presetToSave;
	AudioProjectIdentity identity;
	QString recoveredSourcePath;
	QString error;
	const QString input = creating ? QString() : positional[3];
	if (creating) {
		const qint64 rate = frame("--sample-rate", 48000);
		if (rate < 1 || rate > 384000) {
			return failure(2, QCoreApplication::translate("AudioSessionCli",
			                                              "Session sample rate must be between 1 and 384000 Hz."));
		}
		session.sampleRate = int(rate);
		session.musicalTime.tempo = real("--tempo", 120);
		session.name = values.value("--name");
	} else if (recovering) {
		const auto digest = values.value("--expected-sha256");
		if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(digest).hasMatch()) {
			return failure(2, QCoreApplication::translate(
			                      "AudioSessionCli", "Recovery requires the reviewed 64-character SHA-256 digest."));
		}
		AudioSessionRecovery recovery;
		if (!readAudioSessionRecovery(input, QByteArray::fromHex(digest.toLatin1()), &recovery, &error)) {
			return failure(4, error);
		}
		session = std::move(recovery.session);
		recoveredSourcePath = recovery.sourcePath;
	} else if (!readAudioSession(input, &session, &identity, &error)) {
		return failure(4, error);
	}
	// Keep original provenance protected even if this command removes or relinks it.
	if (metering) {
		const auto first = frame("--start-frame", 0), end = frame("--end-frame", audioSessionFrames(session));
		const auto block = frame("--block-frames", 4096);
		if (!parseError.isEmpty())
			return failure(2, parseError);
		if (end <= first || block < 1 || block > 65536)
			return failure(
			    2, QCoreApplication::translate("AudioSessionCli",
			                                   "Choose a nonempty meter range and a block size of 1–65536 frames."));
		const auto report = measureAudioSessionMeters(session, first, end, int(block));
		if (!report.succeeded())
			return failure(4, report.error);
		AudioSessionCliResult result;
		result.payload = {
		    {"operation", action}, {"meters", audioMeterReportToJson(session, report)}, {"deviceOpened", false}};
		for (int i = 0; i < report.meters.count; ++i) {
			const auto &tap = report.meters.strips[size_t(i)].post;
			const auto db = [](double value) {
				return value > 0 ? QString::number(20 * std::log10(value), 'f', 2) : QStringLiteral("−∞");
			};
			result.lines << QCoreApplication::translate(
			                    "AudioSessionCli",
			                    "%1 · Peak L/R: %2 / %3 dBFS · RMS L/R: %4 / %5 dBFS · Over-range L/R: %6 / %7")
			                    .arg(i == session.tracks.size() ? QCoreApplication::translate("AudioMeters", "Master")
			                                                    : session.tracks[i].name,
			                         db(tap.maximum[0]), db(tap.maximum[1]), db(tap.integratedRms[0]),
			                         db(tap.integratedRms[1]))
			                    .arg(tap.samplesAboveFullScale[0])
			                    .arg(tap.samplesAboveFullScale[1]);
		}
		return result;
	}
	QStringList protectedMediaPaths{recoveredSourcePath, values.value("--preset-file"), values.value("--input")};
	for (const auto &source : session.sources)
		protectedMediaPaths.append(source.audio.sourcePath);
	if (inspectingMedia) {
		const auto inventory = inspectAudioMedia(session);
		if (!inventory.succeeded())
			return failure(4, inventory.error);
		AudioSessionCliResult result;
		result.payload = {{"operation", action}, {"media", audioMediaInventoryJson(inventory)}};
		for (const auto &source : inventory.sources)
			result.lines << QCoreApplication::translate("AudioSessionCli", "%1 · %2 · %3 clips · %4 frames · %5")
			                    .arg(source.id, source.name)
			                    .arg(source.clips)
			                    .arg(source.frames)
			                    .arg(source.availability);
		return result;
	}
	if (creating || action == "tempo-map") {
		AudioSessionEdit edit;
		edit.operation = "tempo-map";
		edit.musicalTime = session.musicalTime;
		edit.musicalTime.tempo = real("--tempo", edit.musicalTime.tempo);
		if (!parseError.isEmpty())
			return failure(2, parseError);
		if (!parseTempoOptions(values, &edit.musicalTime))
			return failure(2, QCoreApplication::translate(
			                      "AudioSessionCli",
			                      "Use --meter numerator/denominator, --tempo-points tick:bpm,... and "
			                      "--meter-points bar:numerator/denominator,...; none clears a change list."));
		// Creation has no prior valid map to edit. All paths validate the same
		// replacement before committing any destination.
		if (creating)
			session.musicalTime = edit.musicalTime;
		else {
			const auto changed = editAudioSession(session, edit);
			if (!changed.succeeded())
				return failure(4, changed.error);
			session = changed.session;
		}
	}
	AudioSessionResult change;
	AudioArrangementResult arrangement;
	AudioRangeResult rangeEdit;
	AudioMediaResult mediaEdit;
	AudioMediaCandidate mediaCandidate;
	QString addedEffectId;
	if (action == "media") {
		AudioMediaEdit edit;
		edit.operation = values.value("--operation");
		edit.sourceIds = selectedSources;
		edit.name = values.value("--name");
		edit.resample = seen.contains("--resample");
		if (edit.operation == "relink" || edit.operation == "replace") {
			const auto digest = values.value("--expected-sha256");
			if (seen.contains("--expected-sha256") &&
			    !QRegularExpression(QStringLiteral("^[0-9a-fA-F]{64}$")).match(digest).hasMatch())
				return failure(
				    2, QCoreApplication::translate("AudioSessionCli", "Expected a 64-character SHA-256 file digest."));
			mediaCandidate = readAudioMediaCandidate(values.value("--input"));
			if (!mediaCandidate.succeeded())
				return failure(4, mediaCandidate.error);
			if (!digest.isEmpty() && mediaCandidate.identity.sha256 != QByteArray::fromHex(digest.toLatin1()))
				return failure(4, QCoreApplication::translate(
				                      "AudioSessionCli", "The input file differs from the reviewed SHA-256 digest."));
		}
		mediaEdit = editAudioMedia(session, edit, mediaCandidate);
		if (!mediaEdit.succeeded())
			return failure(4, mediaEdit.error);
		session = mediaEdit.session;
	}
	if (action == QStringLiteral("import")) {
		const QString sourcePath = values.value("--input");
		QFile file(sourcePath);
		if (!file.open(QIODevice::ReadOnly) || file.size() > AudioInputByteLimit) {
			return failure(
			    3, QCoreApplication::translate("AudioSessionCli", "Audio import is unreadable or exceeds 128 MiB."));
		}
		const auto bytes = file.read(AudioInputByteLimit + 1);
		if (file.error() != QFileDevice::NoError) {
			return failure(3, file.errorString());
		}
		AudioProject media;
		if (bytes.startsWith(QByteArrayLiteral("VSAUD\r\n\x1a"))) {
			if (!decodeAudioProject(bytes, &media, &error)) {
				return failure(4, error);
			}
		} else {
			const auto decoded = decodeAudioClip(sourcePath, bytes);
			if (!decoded.succeeded()) {
				return failure(4, decoded.error);
			}
			media.clip = decoded.clip;
			media.sourceName = QFileInfo(sourcePath).fileName();
			media.sourcePath = QFileInfo(sourcePath).absoluteFilePath();
		}
		const auto at = frame("--at-frame", 0);
		if (!parseError.isEmpty()) {
			return failure(2, parseError);
		}
		change = importAudioSessionSource(session, media, values.value("--track"), at, seen.contains("--resample"));
		if (!change.succeeded()) {
			return failure(4, change.error);
		}
		session = change.session;
	} else if (action == QStringLiteral("effects") || action == QStringLiteral("effect-automation")) {
		AudioSessionEdit edit;
		edit.operation = "effects";
		const auto target = values.value("--track");
		if (target == "id:")
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Use a nonempty track ID after id:."));
		edit.trackId = target == "master" ? QString() : target.startsWith("id:") ? target.mid(3) : target;
		if (edit.trackId.isEmpty())
			edit.effects = session.masterEffects;
		else {
			const auto track = std::find_if(session.tracks.cbegin(), session.tracks.cend(),
			                                [&](const auto &value) { return value.id == edit.trackId; });
			if (track == session.tracks.cend())
				return failure(4, QCoreApplication::translate("AudioSessionCli", "The effects track does not exist."));
			edit.effects = track->effects;
		}
		const auto operation = action == "effect-automation" ? QString("automation") : values.value("--operation");
		int selected = -1;
		for (int i = 0; i < edit.effects.size(); ++i)
			if (edit.effects[i].id == values.value("--effect"))
				selected = i;
		if ((operation == "set" || operation == "remove" || operation == "move" || operation == "automation") &&
		    selected < 0)
			return failure(4, QCoreApplication::translate("AudioSessionCli", "The selected effect does not exist."));
		int index = int(edit.effects.size());
		if (values.contains("--index")) {
			bool valid = false;
			index = values.value("--index").toInt(&valid);
			if (!valid || index < 0 || index > edit.effects.size() - (operation == "move" ? 1 : 0))
				return failure(2, QCoreApplication::translate("AudioSessionCli",
				                                              "Choose a zero-based effect index within the chain."));
		}
		double presetTail = -1;
		if (operation == "automation") {
			const auto key = values.value("--parameter");
			if (!edit.effects[selected].parameters.contains(key))
				return failure(4,
				               QCoreApplication::translate("AudioSessionCli", "The effect parameter does not exist."));
			AudioEffectAutomation lanes;
			if (edit.trackId.isEmpty())
				lanes = session.masterEffectAutomation;
			else
				for (const auto &track : session.tracks)
					if (track.id == edit.trackId)
						lanes = track.effectAutomation;
			auto existing = std::find_if(lanes.begin(), lanes.end(), [&](const auto &lane) {
				return lane.effectId == edit.effects[selected].id && lane.parameter == key;
			});
			AudioEffectAutomationLane lane = existing == lanes.end()
			                                     ? AudioEffectAutomationLane{edit.effects[selected].id, key, true, {}}
			                                     : *existing;
			if (seen.contains("--points") &&
			    !parseAutomationPoints(values.value("--points"), &lane.points, &parseError))
				return failure(2, parseError);
			lane.enabled = boolean("--enabled", lane.enabled);
			if (!parseError.isEmpty())
				return failure(2, parseError);
			if (existing != lanes.end())
				lanes.erase(existing);
			if (!lane.points.isEmpty())
				lanes.append(lane);
			edit.effectAutomation = std::move(lanes);
		} else if (operation == "preset") {
			AudioEffectPreset preset;
			if (seen.contains("--preset-file")) {
				if (!readAudioEffectPreset(values.value("--preset-file"), &preset, nullptr, &error))
					return failure(4, error);
			} else {
				preset = makeAudioEffectPreset(values.value("--preset"), session.sampleRate, &error);
				if (!error.isEmpty())
					return failure(4, error);
			}
			if (!instantiateAudioEffectPreset(preset, session.sampleRate, &edit.effects, &error))
				return failure(4, error);
			presetTail = std::max(session.effectTailSeconds, preset.tailSeconds);
		} else if (operation == "clear")
			edit.effects.clear();
		else if (operation == "remove")
			edit.effects.removeAt(selected);
		else if (operation == "move")
			edit.effects.move(selected, index);
		else if (operation == "add" || operation == "set") {
			auto effect = operation == "add" ? makeAudioEffect(values.value("--type"), session.sampleRate)
			                                 : edit.effects[selected];
			effect.enabled = boolean("--enabled", effect.enabled);
			if (values.contains("--parameters")) {
				QSet<QString> parameters;
				for (const auto &pair : values.value("--parameters").split(',')) {
					const auto fields = pair.split('=');
					bool valid = false;
					const double value = fields.value(1).toDouble(&valid);
					if (fields.size() != 2 || !valid || !std::isfinite(value) || parameters.contains(fields[0]) ||
					    !effect.parameters.contains(fields[0]))
						return failure(
						    2, QCoreApplication::translate(
						           "AudioSessionCli",
						           "Parameters require distinct supported key=value pairs separated by commas."));
					parameters.insert(fields[0]);
					effect.parameters[fields[0]] = value;
				}
			}
			if (operation == "add") {
				addedEffectId = effect.id;
				edit.effects.insert(index, effect);
			} else
				edit.effects[selected] = effect;
		}
		edit.effectTailSeconds = real("--tail-seconds", presetTail);
		if (!parseError.isEmpty())
			return failure(2, parseError);
		if (values.contains("--tail-seconds") && (edit.effectTailSeconds < 0 || edit.effectTailSeconds > 60))
			return failure(
			    2, QCoreApplication::translate("AudioSessionCli", "Effect tail must be between 0 and 60 seconds."));
		if (savingPreset) {
			presetToSave = {values.value("--name"), session.sampleRate,
			                edit.effectTailSeconds < 0 ? session.effectTailSeconds : edit.effectTailSeconds,
			                edit.effects};
		} else {
			change = editAudioSession(session, edit);
			if (!change.succeeded())
				return failure(4, change.error);
			session = change.session;
		}
	} else if (action == QStringLiteral("range")) {
		AudioRangeEdit edit;
		edit.operation = values.value("--operation");
		edit.trackIds = selectedTracks;
		edit.allTracks = seen.contains("--all-tracks");
		edit.first = frame("--start-frame", 0);
		edit.end = frame("--end-frame", 0);
		edit.followAutomation = boolean("--follow-automation", edit.operation != "clear");
		edit.masterAutomation = boolean("--master-automation", edit.allTracks && edit.followAutomation);
		if (!parseError.isEmpty())
			return failure(2, parseError);
		rangeEdit = editAudioRange(session, edit);
		if (!rangeEdit.succeeded())
			return failure(4, rangeEdit.error);
		session = rangeEdit.session;
	} else if (action == QStringLiteral("arrange")) {
		AudioArrangementEdit edit;
		edit.operation = values.value("--operation");
		edit.regionIds = selectedClips;
		edit.linkedGroups = boolean("--linked-groups", true);
		edit.position = frame("--at-frame", 0);
		edit.gainDb = real("--db", 0);
		edit.fadeIn = frame("--fade-in", 0);
		edit.fadeOut = frame("--fade-out", 0);
		edit.muted = boolean("--mute", false);
		edit.name = values.value("--name");
		if (seen.contains("--offset-frames")) {
			bool valid = false;
			const auto offset = values.value("--offset-frames");
			edit.offset = offset.toLongLong(&valid);
			if (!valid || !QRegularExpression(QStringLiteral("^[+-]?[0-9]+$")).match(offset).hasMatch())
				return failure(2, QCoreApplication::translate("AudioSessionCli",
				                                              "Movement requires a signed integer number of frames."));
		}
		if (!parseError.isEmpty())
			return failure(2, parseError);
		arrangement = editAudioArrangement(session, edit);
		if (!arrangement.succeeded())
			return failure(4, arrangement.error);
		session = arrangement.session;
	} else if (action == QStringLiteral("edit") || action == QStringLiteral("automation")) {
		AudioSessionEdit edit;
		edit.operation = action == QStringLiteral("automation") ? action : values.value("--operation");
		edit.trackId = values.value("--track");
		edit.regionId = values.value("--clip");
		for (const auto &track : session.tracks) {
			if (track.id != edit.trackId) {
				continue;
			}
			edit.name = track.name;
			edit.gainDb = track.gainDb;
			edit.pan = track.pan;
			edit.muted = track.muted;
			edit.solo = track.solo;
			edit.gainAutomation = track.gainAutomation;
			edit.panAutomation = track.panAutomation;
			edit.routing = track.routing;
			for (const auto &region : track.regions) {
				if (region.id != edit.regionId) {
					continue;
				}
				edit.name = region.name;
				edit.gainDb = region.gainDb;
				edit.muted = region.muted;
				edit.position = region.position;
				edit.sourceOffset = region.sourceOffset;
				edit.length = region.length;
				edit.fadeIn = region.fadeIn;
				edit.fadeOut = region.fadeOut;
			}
		}
		edit.name = values.value("--name", edit.name);
		edit.gainDb = real("--db", edit.gainDb);
		edit.pan = real("--pan", edit.pan);
		edit.muted = boolean("--mute", edit.muted);
		edit.resetFades = boolean("--reset-fades", false);
		edit.solo = boolean("--solo", edit.solo);
		edit.position = frame("--at-frame", edit.position);
		edit.sourceOffset = frame("--offset-frame", edit.sourceOffset);
		edit.length = frame("--frames", edit.length);
		edit.fadeIn = frame("--fade-in", edit.fadeIn);
		edit.fadeOut = frame("--fade-out", edit.fadeOut);
		const auto busId = [&](const QString &text) {
			if (text == QStringLiteral("id:"))
				parseError = QCoreApplication::translate("AudioSessionCli", "Use a nonempty bus ID after id:.");
			return text.startsWith("id:") ? text.mid(3) : text == QStringLiteral("master") ? QString() : text;
		};
		if (edit.operation == QStringLiteral("routing")) {
			if (values.contains("--output-bus")) {
				const auto output = values.value("--output-bus");
				edit.routing.outputEnabled = output != QStringLiteral("none");
				if (edit.routing.outputEnabled)
					edit.routing.outputId = busId(output);
			}
			edit.routing.invertLeft = boolean("--invert-left", edit.routing.invertLeft);
			edit.routing.invertRight = boolean("--invert-right", edit.routing.invertRight);
			edit.routing.swapChannels = boolean("--swap-channels", edit.routing.swapChannels);
		} else if (edit.operation == QStringLiteral("send") || edit.operation == QStringLiteral("remove-send")) {
			const auto target = busId(values.value("--target-bus"));
			if (!parseError.isEmpty())
				return failure(2, parseError);
			auto existing = std::find_if(edit.routing.sends.begin(), edit.routing.sends.end(),
			                             [&](const auto &send) { return send.targetId == target; });
			if (edit.operation == QStringLiteral("remove-send")) {
				if (existing == edit.routing.sends.end())
					return failure(4,
					               QCoreApplication::translate("AudioSessionCli", "The selected send does not exist."));
				edit.routing.sends.erase(existing);
			} else {
				AudioSend send = existing == edit.routing.sends.end() ? AudioSend{} : *existing;
				send.targetId = target;
				send.gainDb = real("--db", send.gainDb);
				send.pan = real("--pan", send.pan);
				send.preFader = boolean("--pre-fader", send.preFader);
				send.enabled = boolean("--enabled", send.enabled);
				if (existing == edit.routing.sends.end())
					edit.routing.sends.append(send);
				else
					*existing = send;
			}
			edit.operation = "routing";
		}
		const auto points = [&](const QString &key, QVector<AudioAutomationPoint> *destination) {
			if (values.contains(key))
				parseAutomationPoints(values.value(key), destination, &parseError);
		};
		points("--gain-points", &edit.gainAutomation);
		points("--pan-points", &edit.panAutomation);
		if (action == QStringLiteral("automation") && !seen.contains("--gain-points") &&
		    !seen.contains("--pan-points")) {
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Specify gain or pan automation points."));
		}
		if (!parseError.isEmpty()) {
			return failure(2, parseError);
		}
		change = editAudioSession(session, edit);
		if (!change.succeeded()) {
			return failure(4, change.error);
		}
		session = change.session;
	}
	if (!parseError.isEmpty()) {
		return failure(2, parseError);
	}
	const auto issue = validateAudioSessionStructure(session);
	if (!issue.isEmpty()) {
		return failure(4, issue);
	}
	AudioSessionCliResult result;
	AudioWavFormat deliveryFormat = AudioWavFormat::Float32;
	bool deliveryDither = false;
	quint64 deliverySeed = 0;
	if (rendering || exportingStems) {
		if (seen.contains("--wav-format") && !parseAudioWavFormat(values.value("--wav-format"), &deliveryFormat))
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Unsupported WAV precision."));
		const auto dither = values.value("--dither", "none");
		if (dither != "none" && dither != "tpdf")
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Dither must be none or tpdf."));
		deliveryDither = dither == "tpdf";
		if (seen.contains("--dither-seed")) {
			bool valid = false;
			const auto seed = values.value("--dither-seed");
			deliverySeed = seed.toULongLong(&valid);
			if (!valid || !QRegularExpression("^[0-9]+$").match(seed).hasMatch() || !deliveryDither)
				return failure(2, QCoreApplication::translate(
				                      "AudioSessionCli",
				                      "Dither seed requires enabled dither and an unsigned 64-bit decimal integer."));
		}
		if (!parseError.isEmpty())
			return failure(2, parseError);
		if (deliveryDither && deliveryFormat == AudioWavFormat::Float32)
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Dither requires integer PCM."));
	}
	if (exportingStems) {
		AudioStemExportRequest request;
		request.directory = values.value("--output");
		request.prefix = values.value("--name");
		request.protectedPaths = {input};
		request.overwrite = seen.contains("--overwrite");
		request.dryRun = seen.contains("--dry-run");
		request.first = frame("--start-frame", 0);
		request.end = frame("--end-frame", -1);
		request.respectSolo = boolean("--respect-solo", false);
		request.format = deliveryFormat;
		request.dither = deliveryDither;
		request.ditherSeed = deliverySeed;
		const auto tap = values.value("--tap", "post");
		if (tap != "pre" && tap != "post")
			return failure(2, QCoreApplication::translate("AudioSessionCli", "Stem signal point must be pre or post."));
		request.tap = tap == "pre" ? AudioSessionRenderTarget::Tap::PreFader : AudioSessionRenderTarget::Tap::PostFader;
		for (const auto &id : stems) {
			if (id == "master") {
				if (request.includeMaster)
					return failure(2,
					               QCoreApplication::translate("AudioSessionCli", "Select the master mix only once."));
				request.includeMaster = true;
			} else
				request.stripIds << (id.startsWith("id:") ? id.mid(3) : id);
		}
		if (!parseError.isEmpty())
			return failure(2, parseError);
		const auto report = writeAudioSessionStems(session, request);
		result.payload = {{"delivery", report.manifest},
		                  {"manifest", report.plan.manifestPath},
		                  {"completed", report.completed},
		                  {"dryRun", request.dryRun}};
		result.exitCode = report.succeeded ? 0 : 4;
		result.error = report.error;
		result.lines << QCoreApplication::translate("AudioSessionCli", "Stem delivery: %1 of %2 files; manifest: %3.")
		                    .arg(report.completed)
		                    .arg(report.plan.files.size())
		                    .arg(report.plan.manifestPath);
		return result;
	}
	result.payload = {{"operation", action},
	                  {"session", audioSessionSummary(session)},
	                  {"processingLatency", audioSessionLatencyReport(session)}};
	if (positioning) {
		AudioTempoTimeline timeline;
		if (const auto invalid = timeline.prepare(session.musicalTime, session.sampleRate); !invalid.isEmpty())
			return failure(4, invalid);
		qint64 target = -1;
		if (seen.contains("--at-frame"))
			target = frame("--at-frame", 0);
		else if (seen.contains("--at-tick")) {
			bool parsed = false;
			const auto tick = values["--at-tick"].toLongLong(&parsed);
			if (parsed)
				target = timeline.frameAtTick(tick);
		} else {
			AudioMusicalPosition position;
			if (parseAudioMusicalPosition(values["--at-position"], &position))
				target = timeline.frameAtPosition(position);
		}
		if (!parseError.isEmpty())
			return failure(2, parseError);
		if (target < 0)
			return failure(2, QCoreApplication::translate(
			                      "AudioSessionCli",
			                      "The musical position is invalid for this meter or exceeds the timeline limit."));
		const auto position = timeline.positionAtFrame(target);
		const auto meter = timeline.meterAtFrame(target);
		QJsonObject report{{"frame", target},
		                   {"tick", timeline.tickAtFrame(target)},
		                   {"position", audioMusicalPositionText(position)},
		                   {"bar", position.bar},
		                   {"beat", position.beat},
		                   {"tickInBeat", position.tick},
		                   {"bpm", timeline.tempoAtFrame(target)},
		                   {"beatsPerBar", meter.beatsPerBar},
		                   {"beatUnit", meter.beatUnit},
		                   {"ticksPerQuarter", AudioTicksPerQuarter}};
		if (seen.contains("--snap")) {
			const QStringList grids{"bar", "beat", "half-beat", "quarter-beat", "beat-triplet"};
			const auto index = grids.indexOf(values["--snap"]);
			if (index < 0)
				return failure(
				    2, QCoreApplication::translate("AudioSessionCli",
				                                   "Snap must be bar, beat, half-beat, quarter-beat or beat-triplet."));
			const auto grid = AudioMusicalGrid(index);
			const auto snapped = timeline.snapFrame(target, grid);
			report.insert("snappedFrame", snapped);
			report.insert("snappedPosition", audioMusicalPositionText(timeline.positionAtFrame(snapped)));
			report.insert("previousFrame", timeline.stepFrame(target, grid, -1));
			report.insert("nextFrame", timeline.stepFrame(target, grid, 1));
		}
		result.payload.insert("musicalPosition", report);
		result.lines << QCoreApplication::translate("AudioSessionCli", "Position %1 · Frame %2 · %3 BPM · %4/%5")
		                    .arg(audioMusicalPositionText(position))
		                    .arg(target)
		                    .arg(timeline.tempoAtFrame(target))
		                    .arg(meter.beatsPerBar)
		                    .arg(meter.beatUnit);
		return result;
	}
	if (action == "arrange") {
		result.payload.insert("selection", QJsonArray::fromStringList(arrangement.selectedRegionIds));
		result.payload.insert("addedClipIds", QJsonArray::fromStringList(arrangement.addedRegionIds));
		if (!arrangement.groupId.isEmpty())
			result.payload.insert("groupId", arrangement.groupId);
	}
	if (action == "range") {
		result.payload.insert("rangeEdit",
		                      QJsonObject{{"operation", values.value("--operation")},
		                                  {"tracks", QJsonArray::fromStringList(rangeEdit.trackIds)},
		                                  {"first", rangeEdit.first},
		                                  {"end", rangeEdit.end},
		                                  {"addedClipIds", QJsonArray::fromStringList(rangeEdit.addedRegionIds)}});
	}
	if (action == "media") {
		result.payload.insert(
		    "mediaEdit", QJsonObject{{"operation", values.value("--operation")},
		                             {"addedSourceId", mediaEdit.addedSourceId},
		                             {"removedSourceIds", QJsonArray::fromStringList(mediaEdit.removedSourceIds)},
		                             {"affectedClipIds", QJsonArray::fromStringList(mediaEdit.affectedRegionIds)},
		                             {"identicalSamples", mediaEdit.identicalSamples},
		                             {"resampled", mediaEdit.resampled},
		                             {"inputSha256", QString::fromLatin1(mediaCandidate.identity.sha256.toHex())}});
		result.payload.insert("media", audioMediaInventoryJson(inspectAudioMedia(session)));
	}
	if (!addedEffectId.isEmpty())
		result.payload.insert("addedEffectId", addedEffectId);
	if (!change.addedTrackId.isEmpty()) {
		result.payload.insert("addedTrackId", change.addedTrackId);
	}
	if (!change.addedRegionId.isEmpty()) {
		result.payload.insert("addedClipId", change.addedRegionId);
	}
	if (diagnosing) {
		AudioTransportRange range{frame("--start-frame", 0), frame("--end-frame", audioSessionFrames(session)),
		                          boolean("--loop", false)};
		const auto requested = frame("--frames", 0), blockFrames = frame("--block-frames", 1024);
		if (!parseError.isEmpty())
			return failure(2, parseError);
		if (requested < 1 || requested > AudioSampleLimit || blockFrames < 1 || blockFrames > 65536) {
			return failure(2,
			               QCoreApplication::translate(
			                   "AudioSessionCli",
			                   "Transport diagnostics require 1–16777216 frames and a block size of 1–65536 frames."));
		}
		AudioTransport transport;
		if (!transport.prepare(session, range, int(blockFrames), &error))
			return failure(4, error);
		transport.play();
		std::vector<float> samples(size_t(blockFrames) * 2);
		QByteArray canonical(qsizetype(blockFrames * 8), '\0');
		QCryptographicHash digest(QCryptographicHash::Sha256);
		std::array<float, 2> peak{};
		qint64 processed = 0, overs = 0;
		while (processed < requested && transport.state() == AudioTransport::State::Playing) {
			const auto block =
			    transport.process({samples.data(), size_t(std::min(requested - processed, blockFrames)) * 2});
			if (block.status != AudioSessionRenderer::BlockStatus::Ready)
				return failure(4, QCoreApplication::translate("AudioSessionCli", "Transport rendering failed."));
			for (int i = 0; i < block.frames * 2; ++i)
				qToLittleEndian(std::bit_cast<quint32>(samples[size_t(i)]), canonical.data() + i * 4);
			digest.addData(QByteArrayView(canonical.constData(), block.frames * 8));
			processed += block.frames;
			overs += qint64(block.samplesAboveFullScale);
			for (size_t channel = 0; channel < 2; ++channel)
				peak[channel] = std::max(peak[channel], block.peak[channel]);
		}
		result.payload.insert("transport",
		                      QJsonObject{{"framesRequested", requested},
		                                  {"framesRendered", processed},
		                                  {"position", transport.position()},
		                                  {"processingLatencyFrames", transport.processingLatencyFrames()},
		                                  {"loops", qint64(transport.loops())},
		                                  {"peakLeft", peak[0]},
		                                  {"peakRight", peak[1]},
		                                  {"samplesAboveFullScale", overs},
		                                  {"sha256Float32LE", QString::fromLatin1(digest.result().toHex())},
		                                  {"ended", transport.state() == AudioTransport::State::Ended},
		                                  {"deviceOpened", false}});
		result.lines << QCoreApplication::translate(
		                    "AudioSessionCli",
		                    "Transport rendered %1 frames; cursor %2; loops %3. No audio device was opened.")
		                    .arg(processed)
		                    .arg(transport.position())
		                    .arg(transport.loops());
	}
	if (!inspecting && !diagnosing) {
		AudioProjectSaveRequest output;
		output.path = values.value("--output");
		output.overwrite = seen.contains("--overwrite");
		output.dryRun = seen.contains("--dry-run");
		output.protectedPath = input;
		if (!rendering && !recovering && !savingPreset && !input.isEmpty() &&
		    audioPathsReferToSameFile(input, output.path)) {
			if (!output.overwrite) {
				return failure(
				    2, QCoreApplication::translate(
				           "AudioSessionCli",
				           "Updating the source session requires --overwrite and a matching on-disk revision."));
			}
			output.expected = identity;
			output.protectedPath.clear();
		}
		AudioProjectSaveReport saved;
		if (savingPreset) {
			QStringList protectedPaths;
			for (const auto &source : session.sources)
				protectedPaths << source.audio.sourcePath;
			saved = writeAudioEffectPreset(presetToSave, output, protectedPaths);
			result.payload.insert("preset", QJsonObject{{"name", presetToSave.name},
			                                            {"sampleRate", presetToSave.sampleRate},
			                                            {"tailSeconds", presetToSave.tailSeconds},
			                                            {"effects", audioEffectsToJson(presetToSave.effects)}});
		} else if (rendering) {
			AudioSessionMixdown mixdown;
			mixdown.output = output;
			mixdown.first = frame("--start-frame", 0);
			mixdown.end = frame("--end-frame", -1);
			mixdown.format = deliveryFormat;
			mixdown.dither = deliveryDither;
			mixdown.ditherSeed = deliverySeed;
			if (!parseError.isEmpty()) {
				return failure(2, parseError);
			}
			const auto mixed = writeAudioSessionMixdown(session, mixdown);
			saved = mixed.saved;
			result.payload.insert("mixdown", QJsonObject{{"frames", mixed.frames},
			                                             {"processingLatencyFrames", mixed.processingLatencyFrames},
			                                             {"peak", mixed.peak},
			                                             {"samplesAboveFullScale", mixed.samplesAboveFullScale},
			                                             {"format", audioWavFormatId(mixdown.format)},
			                                             {"dither", deliveryDither},
			                                             {"ditherSeed", QString::number(deliverySeed)}});
		} else {
			saved = writeAudioSession(session, output, {}, protectedMediaPaths);
		}
		if (!saved.succeeded) {
			return failure(4, saved.error);
		}
		result.payload.insert("output", output.path);
		result.payload.insert("written", saved.written);
		result.payload.insert("dryRun", output.dryRun);
	}
	result.lines << QCoreApplication::translate("AudioSessionCli",
	                                            "Session tracks: %1; sample rate: %2 Hz; frames: %3.")
	                    .arg(session.tracks.size())
	                    .arg(session.sampleRate)
	                    .arg(audioSessionFrames(session));
	if (!inspecting && !diagnosing) {
		result.lines
		    << (seen.contains("--dry-run")
		            ? QCoreApplication::translate("AudioSessionCli", "Dry run passed; no output was written.")
		            : QCoreApplication::translate("AudioSessionCli", "Output saved: %1").arg(values.value("--output")));
	}
	return result;
}
} // namespace vibestudio::cli
