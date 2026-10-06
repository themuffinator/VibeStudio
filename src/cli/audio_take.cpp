#include "cli/audio_take.h"
#include "core/audio_export.h"
#include "core/audio_take.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

namespace vibestudio::cli
{
AudioSessionCliResult runAudioTake(const QStringList &arguments)
{
	const auto problem = [](const char *source) { return QCoreApplication::translate("AudioTakeCli", source); };
	const auto fail = [](int code, const QString &message) { return AudioSessionCliResult{code, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> common{"--cli", "--json", "--quiet", "--verbose"};
	const QSet<QString> flags{"--overwrite", "--dry-run", "--allow-incomplete"};
	const QSet<QString> options{"--output", "--expected-prefix-sha256", "--start-frame", "--end-frame", "--channels",
	                            "--format"};
	QSet<QString> seen;
	QHash<QString, QString> values;
	QStringList positional;
	for (qsizetype i = 1; i < arguments.size(); ++i) {
		const auto token = arguments[i];
		if (!token.startsWith('-')) {
			positional << token;
			continue;
		}
		const auto equal = token.indexOf('=');
		const auto key = equal < 0 ? token : token.left(equal);
		if (seen.contains(key))
			return fail(2, problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Repeated option: %1")).arg(key));
		seen.insert(key);
		if (common.contains(key) || flags.contains(key)) {
			if (equal >= 0)
				return fail(2, problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Flag does not accept a value: %1")).arg(key));
			continue;
		}
		if (!globals.contains(key) && !options.contains(key))
			return fail(2, problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Unknown take option: %1")).arg(key));
		QString value;
		if (equal >= 0)
			value = token.mid(equal + 1);
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith("--"))
			value = arguments[++i];
		if (value.isEmpty())
			return fail(2, problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Missing value for %1.")).arg(key));
		values.insert(key, value);
	}
	if (positional.size() != 4 || (positional[2] != "inspect" && positional[2] != "export"))
		return fail(
		    2, problem(QT_TRANSLATE_NOOP(
		           "AudioTakeCli", "Expected asset audio-take inspect|export TAKE. Export requires "
		                           "--expected-prefix-sha256, --start-frame, --end-frame, --channels and --output.")));
	const bool inspect = positional[2] == "inspect";
	if (inspect) {
		for (const auto &key : seen)
			if (!globals.contains(key) && !common.contains(key))
				return fail(2,
				            problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Inspection does not accept export options.")));
		const auto info = inspectAudioTake(positional[3]);
		if (!info.headerValid)
			return fail(3, info.error);
		return {0,
		        {},
		        {{"take", audioTakeInfoJson(info)}},
		        {info.path, QString::number(info.frames), QString::fromLatin1(info.prefixSha256.toHex()),
		         info.complete ? problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Complete take")) : info.error}};
	}
	for (const auto *key : {"--output", "--expected-prefix-sha256", "--start-frame", "--end-frame", "--channels"})
		if (!values.contains(QLatin1String(key)))
			return fail(
			    2, problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Missing required option: %1")).arg(QLatin1String(key)));
	const auto digestText = values.value("--expected-prefix-sha256");
	if (!QRegularExpression("^[0-9a-fA-F]{64}$").match(digestText).hasMatch())
		return fail(2, problem(QT_TRANSLATE_NOOP(
		                   "AudioTakeCli", "The reviewed prefix SHA-256 must contain exactly 64 hexadecimal digits.")));
	bool firstOk = false, endOk = false;
	const auto first = values.value("--start-frame").toLongLong(&firstOk),
	           end = values.value("--end-frame").toLongLong(&endOk);
	QVector<int> channels;
	QSet<int> unique;
	for (const auto &item : values.value("--channels").split(',')) {
		bool ok = false;
		const auto channel = item.toLongLong(&ok);
		if (!ok || channel < 1 || channel > 8 || unique.contains(int(channel)))
			return fail(
			    2, problem(QT_TRANSLATE_NOOP(
			           "AudioTakeCli", "Choose distinct stored channel numbers from 1 to 8, separated by commas.")));
		channels.append(int(channel - 1));
		unique.insert(int(channel));
	}
	const auto format = values.value("--format", "project");
	if (!firstOk || !endOk || first < 0 || end <= first || end > AudioTakeFrameLimit ||
	    (format != "project" && format != "wav"))
		return fail(2, problem(QT_TRANSLATE_NOOP("AudioTakeCli",
		                                         "Choose a valid nonempty frame range and format project or wav.")));
	const auto result = readAudioTakeRange(positional[3], QByteArray::fromHex(digestText.toLatin1()), first, end,
	                                       channels, seen.contains("--allow-incomplete"));
	if (!result.succeeded())
		return fail(3, result.error);
	const bool dry = seen.contains("--dry-run"), overwrite = seen.contains("--overwrite");
	const auto output = values.value("--output");
	QString error;
	if (format == "project") {
		AudioProjectSaveRequest request;
		request.path = output;
		request.overwrite = overwrite;
		request.dryRun = dry;
		request.protectedPath = result.info.path;
		const auto saved = writeAudioProject(result.audio, request);
		if (!saved.succeeded)
			return fail(3, saved.error);
	} else {
		AudioWavOptions precision;
		precision.format = AudioWavFormat::Float32;
		if (!saveAudioWav(result.audio.clip, precision, output, overwrite, result.info.path, &error, dry))
			return fail(3, error);
	}
	return {0,
	        {},
	        {{"take", audioTakeInfoJson(result.info)},
	         {"firstFrame", first},
	         {"endFrame", end},
	         {"channels", channels.size()},
	         {"output", output},
	         {"written", !dry},
	         {"format", format}},
	        {dry ? problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Take export validated; no output written."))
	             : problem(QT_TRANSLATE_NOOP("AudioTakeCli", "Take range exported: %1")).arg(output)}};
}
} // namespace vibestudio::cli
