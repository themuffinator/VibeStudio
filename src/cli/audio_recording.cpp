#include "cli/audio_recording.h"
#include "core/audio_recording.h"
#include "core/audio_recording_import.h"
#include "core/audio_recording_review.h"
#include "core/audio_session_io.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace vibestudio::cli
{
AudioSessionCliResult runAudioRecording(const QStringList &arguments)
{
	const auto problem = [](const char *source) { return QCoreApplication::translate("AudioRecordingCli", source); };
	const auto fail = [](const QString &message) { return AudioSessionCliResult{2, message, {}, {}}; };
	const QSet<QString> globals{"--settings-file", "--locale", "--catalog-root"};
	const QSet<QString> common{"--cli", "--json", "--quiet", "--verbose"};
	QSet<QString> flags = common;
	flags.unite({"--dry-run", "--overwrite", "--isolated"});
	const QSet<QString> options{"--review", "--output", "--expected-session-sha256"};
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
			return fail(problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Repeated option: %1")).arg(key));
		seen.insert(key);
		if (flags.contains(key)) {
			if (equal >= 0)
				return fail(
				    problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Flag does not accept a value: %1")).arg(key));
			continue;
		}
		if (!globals.contains(key) && !options.contains(key))
			return fail(problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Unknown recording option: %1")).arg(key));
		QString value;
		if (equal >= 0)
			value = token.mid(equal + 1);
		else if (i + 1 < arguments.size() && !arguments[i + 1].startsWith("--"))
			value = arguments[++i];
		if (value.isEmpty())
			return fail(problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Missing value for %1.")).arg(key));
		values.insert(key, value);
	}
	if (positional.size() != 4 || (positional[0] != "asset" && positional[0] != "assets") ||
	    positional[1] != "audio-recording" ||
	    (positional[2] != "inspect" && positional[2] != "import" && positional[2] != "preview" &&
	     positional[2] != "save-review"))
		return fail(problem(QT_TRANSLATE_NOOP("AudioRecordingCli",
		                                      "Expected asset audio-recording inspect FOLDER.vsrecord or import "
		                                      "SESSION --review PLAN.json --output SESSION.vssession, or preview "
		                                      "SESSION --review PLAN.json --output PREVIEW.wav, or save-review "
		                                      "SESSION --review PLAN.json --output REVIEW.json.")));
	const bool preview = positional[2] == "preview";
	const bool saveReview = positional[2] == "save-review";
	if (seen.contains("--isolated") && !preview)
		return fail(
		    problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "--isolated is only available for recording preview.")));
	if (positional[2] == "import" || preview || saveReview) {
		if (!values.contains("--review") || !values.contains("--output"))
			return fail(problem(QT_TRANSLATE_NOOP(
			    "AudioRecordingCli", "Recording import, preview or save-review requires --review and --output.")));
		AudioRecordingImportRequest request;
		AudioProjectIdentity reviewIdentity;
		QString error;
		if (!readAudioRecordingReview(values.value("--review"), &request, &reviewIdentity, &error))
			return {3, error, {}, {}};
		AudioSession session;
		AudioProjectIdentity identity;
		if (!readAudioSession(positional[3], &session, &identity, &error))
			return {3, error, {}, {}};
		if (values.contains("--expected-session-sha256")) {
			const auto expected = values.value("--expected-session-sha256");
			if (!QRegularExpression("^[0-9a-fA-F]{64}$").match(expected).hasMatch())
				return fail(problem(QT_TRANSLATE_NOOP(
				    "AudioRecordingCli", "The reviewed session SHA-256 must contain exactly 64 hexadecimal digits.")));
			if (QByteArray::fromHex(expected.toLatin1()) != identity.sha256)
				return {3,
				        problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "The source session changed after review.")),
				        {},
				        {}};
		}
		if (saveReview) {
			AudioProjectSaveRequest output;
			output.path = values.value("--output");
			output.overwrite = seen.contains("--overwrite");
			output.dryRun = seen.contains("--dry-run");
			output.protectedPath = positional[3];
			if (audioPathsReferToSameFile(output.path, reviewIdentity.path)) {
				if (!output.overwrite)
					return fail(problem(
					    QT_TRANSLATE_NOOP("AudioRecordingCli", "Updating the review file requires --overwrite.")));
				output.expected = reviewIdentity;
			}
			const auto saved = writeAudioRecordingReview(session, request, output);
			if (!saved.succeeded)
				return {4, saved.error, {}, {}};
			return {
			    0,
			    {},
			    {{"review", audioRecordingImportRequestJson(request)},
			     {"output", output.path},
			     {"written", saved.written},
			     {"dryRun", output.dryRun},
			     {"reviewSha256", QString::fromLatin1(saved.identity.sha256.toHex())},
			     {"sourceSessionSha256", QString::fromLatin1(identity.sha256.toHex())}},
			    {output.dryRun
			         ? problem(
			               QT_TRANSLATE_NOOP("AudioRecordingCli", "Recording review validated; no output was written."))
			         : problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Recording review saved: %1")).arg(output.path)}};
		}
		const auto audition = preview ? prepareAudioRecordingAudition(session, request, !seen.contains("--isolated"))
		                              : AudioRecordingAuditionResult{};
		const auto imported = preview ? audition.imported : importAudioRecording(session, request);
		if (!imported.succeeded())
			return {3, imported.error, {{"recordingImport", audioRecordingImportResultJson(imported)}}, {}};
		AudioProjectSaveRequest output;
		output.path = values.value("--output");
		output.dryRun = seen.contains("--dry-run");
		output.overwrite = seen.contains("--overwrite");
		output.protectedPath = positional[3];
		if (!preview && audioPathsReferToSameFile(positional[3], output.path)) {
			if (!output.overwrite)
				return fail(problem(QT_TRANSLATE_NOOP(
				    "AudioRecordingCli",
				    "Updating the source session requires --overwrite and a matching on-disk revision.")));
			output.expected = identity;
			output.protectedPath.clear();
		}
		auto protectedPaths = audioRecordingProtectedPaths(request.directory);
		protectedPaths << reviewIdentity.path;
		for (const auto &source : session.sources)
			protectedPaths << source.audio.sourcePath;
		if (preview) {
			AudioSessionMixdown mix;
			mix.output = output;
			mix.protectedPaths = protectedPaths;
			mix.first = audition.first;
			mix.end = audition.end;
			const auto rendered = writeAudioSessionMixdown(imported.session, mix);
			if (!rendered.saved.succeeded)
				return {4, rendered.saved.error, {}, {}};
			return {0,
			        {},
			        {{"recordingPreview", audioRecordingImportResultJson(imported)},
			         {"output", output.path},
			         {"written", rendered.saved.written},
			         {"dryRun", output.dryRun},
			         {"first", audition.first},
			         {"end", audition.end},
			         {"frames", rendered.frames},
			         {"includeBacking", !seen.contains("--isolated")},
			         {"sampleRate", session.sampleRate},
			         {"peak", rendered.peak},
			         {"samplesAboveFullScale", rendered.samplesAboveFullScale},
			         {"sourceSessionSha256", QString::fromLatin1(identity.sha256.toHex())}},
			        {output.dryRun ? problem(QT_TRANSLATE_NOOP("AudioRecordingCli",
			                                                   "Recording preview validated; no output was written."))
			                       : problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Recording preview exported: %1"))
			                             .arg(output.path)}};
		}
		const auto saved = writeAudioSession(imported.session, output, {}, protectedPaths);
		if (!saved.succeeded)
			return {4, saved.error, {}, {}};
		return {
		    0,
		    {},
		    {{"recordingImport", audioRecordingImportResultJson(imported)},
		     {"output", output.path},
		     {"written", saved.written},
		     {"dryRun", output.dryRun},
		     {"sourceSessionSha256", QString::fromLatin1(identity.sha256.toHex())}},
		    {output.dryRun
		         ? problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Recording import validated; no output was written."))
		         : problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "Recorded takes imported: %1")).arg(output.path)}};
	}
	for (const auto &key : seen)
		if (!globals.contains(key) && !common.contains(key))
			return fail(problem(
			    QT_TRANSLATE_NOOP("AudioRecordingCli", "Recording inspection does not accept import options.")));
	const auto info = inspectAudioRecording(positional[3]);
	QStringList lines{info.directory};
	for (const auto &take : info.takes)
		lines << problem(QT_TRANSLATE_NOOP("AudioRecordingCli", "%1: %2 verified frames; SHA-256 %3"))
		             .arg(take.path)
		             .arg(take.frames)
		             .arg(QString::fromLatin1(take.prefixSha256.toHex()));
	if (!info.error.isEmpty())
		lines << info.error;
	return {info.planValid ? 0 : 3,
	        info.planValid ? QString{} : info.error,
	        {{"recording", audioRecordingInfoJson(info)}},
	        lines};
}
} // namespace vibestudio::cli
